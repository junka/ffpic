#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "predict.h"
#include "utils.h"

#define BPS (32)
#define YUV_SIZE (BPS * 17 + BPS * 9)
#define Y_OFF (BPS * 1 + 8)
#define U_OFF (Y_OFF + BPS * 16 + BPS)
#define V_OFF (U_OFF + 16)

#define DST(y, x) dst[x + y*stride]
//------------------------------------------------------------------------------
// Transforms (Paragraph 14.4)
static inline uint8_t clip_8b(int v) {
    return (!(v & ~0xff)) ? v : (v < 0) ? 0 : 255;
}

#define STORE(x, y, v)                                                         \
    dst[(x) + (y)*BPS] = clip_8b(dst[(x) + (y)*BPS] + ((v) >> 3))

#define STORE2(y, dc, d, c)                                                    \
    do {                                                                       \
        const int DC = (dc);                                                   \
        STORE(0, y, DC + (d));                                                 \
        STORE(1, y, DC + (c));                                                 \
        STORE(2, y, DC - (c));                                                 \
        STORE(3, y, DC - (d));                                                 \
    } while (0)

#define MUL1(a) ((((a)*20091) >> 16) + (a))
#define MUL2(a) (((a)*35468) >> 16)

static inline void pred_TM(uint8_t *dst, uint8_t *top, uint8_t *left, int size, int stride) {

    for (int i = 0; i < size; ++i) {
        int j;
        for (j = 0; j < size; ++j) {
            DST(i, j) = clamp(left[i] + top[j] - top[-1], 255);
        }
    }
}

static void TransformOne_C(const int16_t *in, uint8_t *dst) {
    int C[4 * 4], *tmp;
    int i;
    tmp = C;
    for (i = 0; i < 4; ++i) {                     // vertical pass
        const int a = in[0] + in[8];              // [-4096, 4094]
        const int b = in[0] - in[8];              // [-4095, 4095]
        const int c = MUL2(in[4]) - MUL1(in[12]); // [-3783, 3783]
        const int d = MUL1(in[4]) + MUL2(in[12]); // [-3785, 3781]
        tmp[0] = a + d;                           // [-7881, 7875]
        tmp[1] = b + c;                           // [-7878, 7878]
        tmp[2] = b - c;                           // [-7878, 7878]
        tmp[3] = a - d;                           // [-7877, 7879]
        tmp += 4;
        in++;
    }
    // Each pass is expanding the dynamic range by ~3.85 (upper bound).
    // The exact value is (2. + (20091 + 35468) / 65536).
    // After the second pass, maximum interval is [-3794, 3794], assuming
    // an input in [-2048, 2047] interval. We then need to add a dst value
    // in the [0, 255] range.
    // In the worst case scenario, the input to clip_8b() can be as large as
    // [-60713, 60968].
    tmp = C;
    for (i = 0; i < 4; ++i) { // horizontal pass
        const int dc = tmp[0] + 4;
        const int a = dc + tmp[8];
        const int b = dc - tmp[8];
        const int c = MUL2(tmp[4]) - MUL1(tmp[12]);
        const int d = MUL1(tmp[4]) + MUL2(tmp[12]);
        STORE(0, 0, a + d);
        STORE(1, 0, b + c);
        STORE(2, 0, b - c);
        STORE(3, 0, a - d);
        tmp++;
        dst += BPS;
    }
}

// Simplified transform when only in[0], in[1] and in[4] are non-zero
static void TransformAC3_C(const int16_t *in, uint8_t *dst) {
    const int a = in[0] + 4;
    const int c4 = MUL2(in[4]);
    const int d4 = MUL1(in[4]);
    const int c1 = MUL2(in[1]);
    const int d1 = MUL1(in[1]);
    STORE2(0, a + d4, d1, c1);
    STORE2(1, a + c4, d1, c1);
    STORE2(2, a - c4, d1, c1);
    STORE2(3, a - d4, d1, c1);
}

#undef MUL1
#undef MUL2
#undef STORE2

static void TransformTwo_C(const int16_t *in, uint8_t *dst, int do_two) {
    TransformOne_C(in, dst);
    if (do_two) {
        TransformOne_C(in + 16, dst + 4);
    }
}

static void TransformUV_C(const int16_t *in, uint8_t *dst) {
    TransformTwo_C(in + 0 * 16, dst, 1);
    TransformTwo_C(in + 2 * 16, dst + 4 * BPS, 1);
}

static void TransformDC_C(const int16_t *in, uint8_t *dst) {
    const int DC = in[0] + 4;
    int i, j;
    for (j = 0; j < 4; ++j) {
        for (i = 0; i < 4; ++i) {
            STORE(i, j, DC);
        }
    }
}

static void TransformDCUV_C(const int16_t *in, uint8_t *dst) {
    if (in[0 * 16])
        TransformDC_C(in + 0 * 16, dst);
    if (in[1 * 16])
        TransformDC_C(in + 1 * 16, dst + 4);
    if (in[2 * 16])
        TransformDC_C(in + 2 * 16, dst + 4 * BPS);
    if (in[3 * 16])
        TransformDC_C(in + 3 * 16, dst + 4 * BPS + 4);
}

//------------------------------------------------------------------------------
// Main reconstruction function.

static inline void DoTransform(uint32_t bits, const int16_t *const src,
                               uint8_t *const dst) {
    switch (bits >> 30) {
    case 3:
        TransformTwo_C(src, dst, 0);
        break;
    case 2:
        TransformAC3_C(src, dst);
        break;
    case 1:
        TransformDC_C(src, dst);
        break;
    default:
        break;
    }
}

static void DoUVTransform(uint32_t bits, const int16_t *const src,
                          uint8_t *const dst) {
    if (bits & 0xff) {     // any non-zero coeff at all?
        if (bits & 0xaa) { // any non-zero AC coefficient?
            TransformUV_C(src,
                          dst); // note we don't use the AC3 variant for U/V
        } else {
            TransformDCUV_C(src, dst);
        }
    }
}

#define AVG3(a, b, c)  ((uint8_t)(((uint32_t)(a) + ((uint32_t)(b) * 2) + (c) + 2) >> 2))
#define AVG2(a, b) (((a) + (b) + 1) >> 1)
#define AVG3P(p) AVG3((*(p-1)), (*(p)), (*(p+1)))
#define AVG2P(p) AVG2(*(p), *(p+1))
#define DC_ROUND(dc, shf) ((dc + (1 << (shf - 1))) >> shf)

// h264 8.3.1.2.1
// rfc6383 12.3

//===== 4X4 luma ==========================================
static void pred_B_DC(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x UNUSED, int y UNUSED) {
    // DC
    uint32_t dc = 0;
    int i;
    // for (i = 0; i < 4; ++i)
    //   dc += dst[i - BPS] + dst[-1 + i * BPS];
    // dc >>= 3;
    for (i = 0; i < 4; i++) {
        dc += left[i] + top[i];
    }

    // if (x > 0) {
    //   for (int i = 0; i < 4; i++) {
    //       dc += left[i];
    //   }
    // }
    // if (y > 0) {
    //   for (int i = 0; i < 4; i++) {
    //       dc += top[i];
    //   }
    // }
    // if (x == 0 && y == 0) {
    //   dc = 0x80;
    // } else if (y == 0 || x == 0) {
    //   dc = DC_ROUND(dc, 2);
    // } else {
    //   dc = DC_ROUND(dc, 3);
    // }

    dc = DC_ROUND(dc, 3);
    for (i = 0; i < 4; ++i)
        memset(dst + i * stride, dc, 4);
}

static void pred_B_TM(uint8_t *dst, uint8_t *top, uint8_t *left, int stride,
                     int x UNUSED, int y UNUSED) {
    pred_TM(dst, top, left, 4, stride);
}

static void pred_B_VE(uint8_t *dst, uint8_t *top, uint8_t *left UNUSED,
                      int stride, int x UNUSED, int y UNUSED) {
    /*
    | P  | A | B | C | D | E | F | G | H |
    |----|---|---|---|---|---|---|---|---|
    | L0 | | | | | | | | |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L1 | | | | | | | | |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L2 | | | | | | | | |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L3 | | | | | | | | |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    */
    // const uint8_t *top = dst - BPS;
    const uint8_t vals[4] = {
        AVG3P(top),
        AVG3P(top + 1),
        AVG3P(top + 2),
        AVG3P(top + 3),
    };
    int i;
    for (i = 0; i < 4; ++i) {
        memcpy(dst + i * stride, vals, sizeof(vals));
    }
}

static void pred_B_HE(uint8_t *dst, uint8_t *top, uint8_t *left,
                      int stride, int x UNUSED, int y UNUSED) {
    /*  horizontal
    | P  | A | B | C | D | E | F | G | H |
    |----|---|---|---|---|---|---|---|---|
    | L0 | - | - | - | - |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L1 | - | - | - | - |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L2 | - | - | - | - |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L3 | - | - | - | - |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    */
    uint32_t sr = (uint32_t)0x01010101U * AVG3(top[-1], left[0], left[1]);
    memcpy(dst + 0 * stride, &sr, 4);
    sr = (uint32_t)0x01010101U * AVG3P(left + 1);
    memcpy(dst + 1 * stride, &sr, 4);
    sr = (uint32_t)0x01010101U * AVG3P(left + 2);
    memcpy(dst + 2 * stride, &sr, 4);
    sr = (uint32_t)0x01010101U *
         AVG3(left[2], left[3], left[3]); // overflow left filled with above
    memcpy(dst + 3 * stride, &sr, 4);
}

static void pred_B_LD(uint8_t *dst, uint8_t *top, uint8_t *left UNUSED,
                      int stride, int x, int y) {
    /* Down-Left
    | P  | A | B | C | D | E | F | G | H |
    |----|---|---|---|---|---|---|---|---|
    | L0 | / | / | / | / |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L1 | / | / | / | / |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L2 | / | / | / | / |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L3 | / | / | / | / |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    */
    DST(0, 0) = AVG3P(top + 1);
    DST(1, 0) = DST(0, 1) = AVG3P(top + 2);
    DST(2, 0) = DST(1, 1) = DST(0, 2) = AVG3P(top + 3);
    DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3P(top + 4);
    DST(3, 1) = DST(2, 2) = DST(1, 3) = AVG3P(top + 5);
    DST(3, 2) = DST(2, 3) = AVG3P(top + 6);
    DST(3, 3) = AVG3(top[6], top[7], top[7]);
}

static void pred_B_RD(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x UNUSED, int y UNUSED) {
    /* Down-right
    | X  | A | B | C | D | E | F | G | H |
    |----|---|---|---|---|---|---|---|---|
    | I  | \ | \ | \ | \ |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | J  | \ | \ | \ | \ |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | K  | \ | \ | \ | \ |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L  | \ | \ | \ | \ |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    */
    // const int I = dst[-1 + 0 * BPS];
    // const int J = dst[-1 + 1 * BPS];
    // const int K = dst[-1 + 2 * BPS];
    // const int L = dst[-1 + 3 * BPS];
    // const int X = dst[-1 - BPS];
    // const int A = dst[0 - BPS];
    // const int B = dst[1 - BPS];
    // const int C = dst[2 - BPS];
    // const int D = dst[3 - BPS];
    DST(0, 3) = AVG3P(top + 2);
    DST(1, 3) = DST(0, 2) = AVG3P(top + 1);
    DST(2, 3) = DST(1, 2) = DST(0, 1) = AVG3P(top);
    DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) =
        AVG3(top[0], *(top - 1), left[0]);
    DST(3, 2) = DST(2, 1) = DST(1, 0) = AVG3(*(left + 1), *left, *(top - 1));
    DST(3, 1) = DST(2, 0) = AVG3P(left + 1);
    DST(3, 0) = AVG3P(left + 2);
}

static void pred_B_VR(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x UNUSED, int y UNUSED) {
    /* Vertical-Right
    | X  | A | B | C | D | E | F | G | H |
    |----|---|---|---|---|---|---|---|---|
    | I  |\  |\  |\  |\  |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | J  |\  |\  |\  |\  |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | K  |\  |\  |\  |\  |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L  |\  |\  |\  |\  |   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    */
    DST(0, 0) = DST(2, 1) = AVG2P(top - 1);
    DST(0, 1) = DST(2, 2) = AVG2P(top);
    DST(0, 2) = DST(2, 3) = AVG2P(top + 1);
    DST(0, 3) = AVG2P(top + 2);

    DST(1, 0) = DST(3, 1) = AVG3(left[0], top[-1], top[0]);
    DST(1, 1) = DST(3, 2) = AVG3P(top);
    DST(1, 2) = DST(3, 3) = AVG3P(top + 1);
    DST(1, 3) = AVG3P(top + 2);

    DST(3, 0) = AVG3P(left + 2);
    DST(2, 0) = AVG3(left[0], left[1], top[-1]);
}

static void pred_B_VL(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x UNUSED, int y UNUSED) {
    /* Vertical-Left (1/3 step)
    | P  | A | B | C | D | E | F | G | H |
    |----|---|---|---|---|---|---|---|---|
    | L0 |  /|  /|  /|  /|  /|   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L1 |  /|  /|  /|  /|   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L2 |  /|  /|  /|  /|   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    | L3 |  /|  /|  /|  /|   |   |   |   |
    |----|---|---|---|---|---|---|---|---|
    */
    DST(0, 0) = AVG2P(top);
    DST(1, 0) = AVG3P(top + 1);
    DST(2, 0) = DST(0, 1) = AVG2P(top + 1);
    DST(3, 0) = DST(1, 1) = AVG3P(top + 2);
    DST(2, 1) = DST(0, 2) = AVG2P(top + 2);
    DST(3, 1) = DST(1, 2) = AVG3P(top + 3);
    DST(2, 2) = DST(0, 3) = AVG2P(top + 3);
    DST(3, 2) = DST(1, 3) = AVG3P(top + 4);
    DST(2, 3) = AVG3P(top + 5);
    DST(3, 3) = AVG3P(top + 6);
}

static void pred_B_HD(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x UNUSED, int y UNUSED) {
    // Horizontal-Down
    DST(0, 0) = DST(1, 2) = AVG2(left[0], *(top - 1));
    DST(1, 0) = DST(2, 2) = AVG2P(left);
    DST(2, 0) = DST(3, 2) = AVG2P(left + 1);
    DST(3, 0) = AVG2P(left + 2);

    DST(0, 3) = AVG3P(top + 1);
    DST(0, 2) = AVG3P(top);
    DST(0, 1) = DST(1, 3) = AVG3(left[0], *(top - 1), top[0]);
    DST(1, 1) = DST(2, 3) = AVG3(left[0], left[1], top[-1]);
    DST(2, 1) = DST(3, 3) = AVG3P(left + 1);
    DST(3, 1) = AVG3P(left + 2);
}

static void pred_B_HU(uint8_t *dst, uint8_t *top UNUSED, uint8_t *left,
                      int stride, int x UNUSED, int y UNUSED) {
    // Horizontal-Up
    DST(0, 0) = AVG2P(left);
    DST(0, 1) = AVG3P(left + 1);
    DST(0, 2) = DST(1, 0) = AVG2P(left + 1);
    DST(0, 3) = DST(1, 1) = AVG3P(left + 2);
    DST(1, 2) = DST(2, 0) = AVG2P(left + 2);
    DST(1, 3) = DST(2, 1) = AVG3(left[2], left[3], left[3]);
    DST(2, 2) = DST(2, 3) = DST(3, 0) = DST(3, 1) = DST(3, 2) = DST(3, 3) = left[3];
}

//====== 8X8 Chrome ============================================

// helper for chroma-DC predictions
#define Put8x8uv(value, dst, stride)                                           \
    {                                                                          \
        for (int j = 0; j < 8; ++j) {                                          \
            memset(dst + j * stride, value, 8);                                \
        }                                                                      \
    }

static void pred_DC_UV(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x, int y) {
    // DC
    int dc = 0;
    if (x > 0) {
      for (int i = 0; i < 8; i++) {
          dc += left[i];
      }
    }
    if (y > 0) {
      for (int i = 0; i < 8; i++) {
          dc += top[i];
      }
    }
    if (x == 0 && y == 0) {
      dc = 0x80;
    } else if (y == 0 || x == 0) {
      dc = DC_ROUND(dc, 3);
    } else {
      dc = DC_ROUND(dc, 4);
    }
    Put8x8uv(dc, dst, stride);
}

static void pred_TM_UV(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x, int y) {
    pred_TM(dst, top, left, 8, stride);
}

static void pred_VE_UV(uint8_t *dst, uint8_t *top, uint8_t *left UNUSED,
                       int stride, int x UNUSED, int y UNUSED) {
    // vertical
    int j;
    for (j = 0; j < 8; ++j) {
        memcpy(dst + j * stride, top, 8);
    }
}

static void pred_HE_UV(uint8_t *dst, uint8_t *top UNUSED, uint8_t *left,
                       int stride, int x UNUSED, int y UNUSED) {
    // horizontal
    int j;
    for (j = 0; j < 8; ++j) {
        memset(dst, left[j], 8);
        dst += stride;
    }
}

//------------------------------------------------------------------------------
// 16x16
static void pred_DC_16(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x, int y) {
    // DC
    int dc = 0, j = 0;
    if (y > 0) {
      for (j = 0; j < 16; ++j) {
          dc += top[j];
      }
    }
    if (x > 0) {
      for (j = 0; j < 16; ++j) {
          dc += left[j];
      }
    }
    if (x == 0 && y == 0) {
      dc = 0x80;
    } else if (y == 0 || x == 0) {
      dc = DC_ROUND(dc, 4);
    } else {
      dc = DC_ROUND(dc, 5);
    }
    for (j = 0; j < 16; ++j) {
        memset(dst + j * stride, (uint8_t)dc, 16);
    }
}

static void pred_VE_16(uint8_t *dst, uint8_t *top UNUSED, uint8_t *left,
                       int stride, int x UNUSED, int y UNUSED) {
    // vertical
    for (int j = 0; j < 16; ++j) {
        memcpy(dst + j * stride, dst - stride, 16);
    }
}

static void pred_HE_16(uint8_t *dst, uint8_t *top, uint8_t *left UNUSED,
                       int stride, int x UNUSED, int y UNUSED) {
    // horizontal
    for (int j = 16; j > 0; --j) {
        memset(dst, dst[-1], 16);
        dst += stride;
    }
}

static void pred_TM_16(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x, int y) {
    pred_TM(dst, top, left, 16, stride);
}

typedef void (*PredFunc)(uint8_t *dst, uint8_t *top, uint8_t *left, int stride, int x, int y);

static PredFunc PredLuma4[NUM_BMODES] = {
    pred_B_DC, pred_B_TM,
    pred_B_VE, pred_B_HE,
    pred_B_RD, pred_B_VR,
    pred_B_LD, pred_B_VL,
    pred_B_HD, pred_B_HU
};

static PredFunc PredChroma8[4] = {
    pred_DC_UV,
    pred_TM_UV,
    pred_VE_UV,
    pred_HE_UV,
};

static PredFunc PredLuma16[4] = {
    pred_DC_16,
    pred_TM_16,
    pred_VE_16,
    pred_HE_16,
};

void pred_luma(int ymode, uint8_t imodes[16], uint8_t *dst, int stride, int x, int y) {
    assert(ymode <= NUM_PRED_MODES);

    uint8_t left_default[16] = {
        129, 129, 129, 129, 129, 129, 129, 129, 
        129, 129, 129, 129, 129, 129, 129, 129
    };
    uint8_t top_default[21] = {
        127, 127, 127, 127, 127, 127, 127, 127,
        127, 127, 127, 127, 127, 127, 127, 127,
        127, 127, 127, 127, 127,
    };

    uint8_t *top = &top_default[1]; // so we can access top[-1]
    uint8_t *left = &left_default[0];
    uint8_t *topright = &top_default[5];
#ifndef NDEBUG
    int dx = 17, dy = 85;
#endif


    if (ymode == B_PRED) {
        /* 4X4 subblock shoul be like below
        -----------------------------------------
        | y x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        -----------------------------------------
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        -----------------------------------------
        | x x x x | A x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        -----------------------------------------
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        | x x x x | x x x x | x x x x | x x x x |
        -----------------------------------------
        */
        for (int n = 0; n < 16; ++n) {
            PredFunc lumabfunc = PredLuma4[imodes[n]];
            // for example n = 9, x = (9 % 4) * 4; y = (9 / 4) * 4;
            int xs = (n % 4);
            int ys = (n / 4);
            if (x > 0 || xs > 0) {
              for (int m = 0; m < 4; m ++) {
                  left[m] = *(dst + (ys * 4 + m) * stride + xs * 4 - 1);
              }
            } else {
              memset(left_default, 129, 4);
            }
#ifndef NDEBUG
            if (y == dy && x == dx) {
                  printf("left: ");
              for (int m = 0; m < 4; m ++) {
                  printf("%02x ", left[m]);
              }
              printf("\n");
            }
#endif
            if (y == 0 && ys == 0) {
                memset(top_default, 127, 9);
                // top[-1] = 129;
            } else if (y > 0 && ys == 0) {
                memcpy(top, dst + xs * 4 - stride, 8);
                if (xs > 0 || x > 0) {
                    top[-1] = *(dst + xs * 4 - stride - 1);
                } else {
                    top[-1] = 129;
                }
                // fill the top when sub-block overflow for only ys == 0, other subblock shares that from subblock 3
                if (x == ((stride / 16) - 1) && xs == 3) {
                    memset(topright, 127, 4);
                }
            } else {
                // if (x == 0 && xs == 0) {
                //     memcpy(top, dst + (ys * 4)* stride - stride, 8);
                //     top[-1] = 129;
                // } else if ((x > 0 && xs == 0) || (xs > 0 && xs < 3)) {
                //     memcpy(top - 1, dst + (ys * 4)* stride + xs * 4 - stride
                //     - 1, 9);
                // } else if (xs == 3) {
                //     memcpy(top-1, dst - stride -1, 5);
                //     memset(topright, 127, 4);
                // }
                // use the right value from xs == 3 and ys == 0
                memcpy(top, dst + (ys * 4)* stride + xs * 4 - stride, 4);
                if (xs == 3) {
                    memcpy(topright, dst + 3 * 4 - stride + 4, 4);
                } else if (xs < 3) {
                    memcpy(topright, dst + (ys * 4)* stride + xs * 4 - stride + 4, 4);
                }
                if (xs == 0 && x == 0) {
                    top[-1] = 129;
                } else {
                    top[-1] = *(dst + (ys * 4) * stride + xs * 4 - stride -1);
                }
            }
#ifndef NDEBUG
            if (y == dy && x == dx) {
              printf("top from -1 ");
              for (int m = -1; m < 8; m ++) {
                printf("%02x ", top[m]);
                if (m == 7) {
                  printf("\n");
                }
              }
            }
#endif

            lumabfunc(dst + ys * stride * 4 + xs * 4, top, left, stride, x, y);
#ifndef NDEBUG
            if (y == dy && x == dx) {
                printf("%d %d: mode %d ", ys, xs, imodes[n]);
                mb_dump(stdout, "", dst + ys * stride * 4 + xs * 4, 4, stride);
            }
#endif
        }
    } else {
        if (x > 0) {
            //fill left with value from prev left block value
            for (int i = 0; i < 16; i ++) {
                left[i] = *(dst + stride * i - 1);
            }
        }
        if (y > 0) {
            if (x > 0) {
                memcpy(top - 1, dst - stride - 1, 17);
            } else {
                memcpy(top, dst - stride, 16);
                top[-1] = 129;
            }
        }

#ifndef NDEBUG
        if (x == dx && y == dy) {
            printf("left: ");
            for (int i = 0; i < 16; i ++) {
                printf("%02x ", left[i]);
            }
            printf("\n");
        }
        if (x == dx && y == dy) {
          printf("top from -1: ");
          for (int i = -1; i < 16; i ++) {
            printf("%02x ", top[i]);
          }
          printf("\n");
        }
#endif
        /* 16X16 */
        PredFunc lumafunc = PredLuma16[ymode];
        lumafunc(dst, top, left, stride, x, y);
#ifndef NDEBUG
        if (x == dx && y == dy) {
            printf("%d %d: mode %d ", y, x, ymode);
            mb_dump(stdout, "", dst, 16, stride);
        }
#endif
    }
}

void pred_chrome(int imode, uint8_t *uout, uint8_t *vout, int stride,
                 int x, int y) {
    assert(imode < NUM_BMODES);
    PredFunc chromafunc = PredChroma8[imode];

    // U
    chromafunc(uout, NULL, NULL, stride, x, y);

    // V
    chromafunc(vout, NULL, NULL, stride, x, y);
}