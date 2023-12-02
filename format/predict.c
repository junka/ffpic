#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "predict.h"
#include "utils.h"

#define DST(y, x) dst[x + y*stride]


static inline void pred_TM(uint8_t *dst, uint8_t *top, uint8_t *left, int size, int stride) {

    for (int i = 0; i < size; ++i) {
        int j;
        for (j = 0; j < size; ++j) {
            DST(i, j) = clamp(left[i] + top[j] - top[-1], 255);
        }
    }
}

//------------------------------------------------------------------------------
// Main reconstruction function.

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

    DST(3, 0) = AVG3P(left + 1);
    DST(2, 0) = AVG3(left[1], left[0], top[-1]);
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
    DST(0, 1) = DST(1, 3) = AVG3(left[0], top[-1], top[0]);
    DST(1, 1) = DST(2, 3) = AVG3(left[1], left[0], top[-1]);
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

static void add_residue_subblock(int16_t *coff, uint8_t *dst, int stride)
{
    /* each subblock is 4*4 size, it has 16 coffs in order */
    int16_t *c = coff;
    uint8_t *d = dst;
    for (int l = 0; l < 16; l++) {
        d[l % 4] = clamp((*c++) + d[l % 4], 255);
        if (l % 4 == 3) {
          d += stride;
        }
    }
}

static void add_luma_block(int16_t *coff, uint8_t *yout, int y_stride)
{
    int16_t *c = coff;
    uint8_t *y = yout;
    /* each X is 4X4 subblock, it has 16 coffs in order
        XXXX
        XXXX
        XXXX
        XXXX
    */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          y = yout + i * 4 * y_stride + j * 4;
          add_residue_subblock(c, y, y_stride);
          c += 16;
        }
    }
}

static void add_chrome_block(int16_t *coff, uint8_t *uvout, int uv_stride)
{
    int16_t *c = coff;
    uint8_t *uv;
    /* each X is 4X4 subblock, it has 16 coffs in order
        XX
        XX
    */
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            uv = uvout + i * 4 * uv_stride + j * 4;
            add_residue_subblock(c, uv, uv_stride);
            c += 16;
        }
    }
}
void pred_luma(int16_t *coff, int ymode, uint8_t imodes[16], uint8_t *dst, int stride, int x, int y) {
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
    int dx = 999, dy = 999;
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
                if (x == ((stride / 16) - 1) && xs == 3) {
                    memset(topright, 127, 4);
                }
            } else {
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
            add_residue_subblock(coff+16*n, dst + ys * stride * 4 + xs * 4, stride);
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
            mb_dump(stdout, "pred", dst, 16, stride);
            // mb_dump(stdout, "coff", coff, 16, 16);
            fprintf(stdout, "coff:\n");
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    fprintf(stdout, "%03d ", *(coff + 16*i + j));
                }
                fprintf(stdout, "\n");
            }
        }
#endif
        add_luma_block(coff, dst, stride);
    }
}

void pred_chrome(int16_t *coff, int imode, uint8_t *uout, uint8_t *vout, int stride,
                 int x, int y) {
    assert(imode < NUM_BMODES);
    PredFunc chromafunc = PredChroma8[imode];

    uint8_t leftu_default[8] = {129, 129, 129, 129, 129, 129, 129, 129};
    uint8_t topu_default[9] = {
        127, 127, 127, 127, 127, 127, 127, 127, 127
    };
    uint8_t leftv_default[8] = {129, 129, 129, 129, 129, 129, 129, 129};
    uint8_t topv_default[9] = {127, 127, 127, 127, 127, 127, 127, 127, 127};

    uint8_t *topu = &topu_default[1]; // so we can access top[-1]
    uint8_t *leftu = &leftu_default[0];
    uint8_t *topv = &topv_default[1]; // so we can access top[-1]
    uint8_t *leftv = &leftv_default[0];
#ifndef NDEBUG
    int dx = 999, dy = 999;
#endif
    if (x > 0) {
        // fill left with value from prev left block value
        for (int i = 0; i < 8; i++) {
            leftu[i] = *(uout + stride * i - 1);
            leftv[i] = *(vout + stride * i - 1);
        }
    }
    if (y > 0) {
        if (x > 0) {
            memcpy(topu - 1, uout - stride - 1, 9);
            memcpy(topv - 1, vout - stride - 1, 9);
        } else {
            memcpy(topu, uout - stride, 8);
            memcpy(topv, vout - stride, 8);
            topu[-1] = 129;
            topv[-1] = 129;
        }
    }

    // U
    chromafunc(uout, topu, leftu, stride, x, y);
    add_chrome_block(coff, uout, stride);
#ifndef NDEBUG
        if (x == dx && y == dy) {
        printf("%d %d: mode %d ", y, x, imode);
        mb_dump(stdout, "", uout, 8, stride);
    }
#endif
    // V
    chromafunc(vout, topv, leftv, stride, x, y);
    add_chrome_block(coff+64, vout, stride);
#ifndef NDEBUG
    if (x == dx && y == dy) {
        mb_dump(stdout, "", vout, 8, stride);
    }
#endif
}

//--------------------------------------------
// below is hevc pred

// see 8.4.4.2.4
void hevc_intra_planar(uint16_t *dst, uint16_t *left, uint16_t *top,
                       int nTbS, int stride) // both take top-left included
{
    int log2v = log2floor(nTbS);
    for (int y = 0; y < nTbS; y ++) {
        for (int x = 0; x < nTbS; x++) {
            DST(y, x) = ((nTbS - 1 - x) * left[y] + (x+1) * top[nTbS] + (nTbS -1 - y) * top[x] + (y+1) * left[nTbS] + nTbS) >> (log2v+1);
            // if (y == 0 && x == 0) {
            //     fprintf(stdout,
            //             "(%d, %d) %x: (%d * %d + %d * %d + %d * %d + %d * %d + "
            //             "%d)>> (%d+1)\n",
            //             x, y, DST(y, x), (nTbS - 1 - x), left[y], (x + 1),
            //             top[nTbS], (nTbS - 1 - y), top[x], (y + 1), left[nTbS],
            //             nTbS, log2v);
            // }
        }
    }
}

//see 8.4.4.2.5
void hevc_intra_DC(uint16_t *dst, uint16_t *left, uint16_t *top, int nTbS,
                   int stride, int cIdx,
                   int intra_boundary_filtering_disabled_flag) {
    uint32_t dc = 0;
    int i;
    for (i = 0; i < nTbS; i++) {
        dc += left[i] + top[i];
    }
    dc = DC_ROUND(dc, (log2floor(nTbS) + 1));

    if (cIdx == 0 && nTbS < 32 && intra_boundary_filtering_disabled_flag == 0) {
        DST(0, 0) = ((left[0] + 2 *dc + top[0] + 2) >> 2);
        for (int x = 1; x < nTbS; x++) {
            DST(0, x) = (top[x] + 3* dc + 2) >> 2;
        }
        for (int y = 1; y < nTbS; y++) {
            DST(y, 0) = (left[y] + 3 * dc + 2) >> 2;
        }
        for (int y = 1; y < nTbS; y++) {
            for (int x = 1; x < nTbS; x++) {
                DST(y, x) = dc;
            }
        }
    } else {
        for (int y = 0; y < nTbS; y++) {
            for (int x = 0; x < nTbS; x++) {
                DST(y, x) = dc;
            }
        }
    }
}

//see 8.4.4.2.6
void hevc_intra_angular(uint16_t *dst, uint16_t *left, uint16_t *top, int nTbS,
                        int stride, int cIdx, int predModeIntra,
                        int disableIntraBoundaryFilter, int bitdepth)
{
    assert(predModeIntra < 35 && predModeIntra > 1);
    const int intraPredAngle[] = {32,  26,  21,  17,  13,  9,   5,   2,   0,
                                  -2,  -5,  -9,  -13, -17, -21, -26, -32, -26,
                                  -21, -17, -13, -9,  -5,  -2,  0,   2,   5,
                                  9,   13,  17,  21,  26,  32};
    const int invAngle[] = {-4096, -1638, -910, -630,  -482,
                                         -390,  -315,  -256, -315,  -390,
                                         -482,  -630,  -910, -1638, -4096};

    int ref_arr[67];
    int *ref = ref_arr + 33;

    if (predModeIntra >= 18) {
        for (int x = 0; x <= nTbS; x++) {
            ref[x] = top[x-1];
        }
        int angle = intraPredAngle[predModeIntra - 2];
        if (angle < 0) {
            if (((nTbS * angle) >> 5) < -1) {
                for (int x = -1; x >= ((angle *nTbS) >> 5); x--) {
                    if (((x * invAngle[predModeIntra-11] + 128) >> 8) == 0) {
                        ref[x] = top[-1];
                    } else {
                        ref[x] = left[-1 + ((x * invAngle[predModeIntra-11] + 128) >> 8)];
                    }
                }
            }
        } else {
            for (int x = nTbS+1; x <= 2*nTbS; x++) {
                ref[x] = top[x - 1];
            }
        }

        for (int y = 0; y < nTbS; y++) {
            int iIdx = (y + 1) * angle >> 5;
            int iFact = ((y + 1) * angle) & 31;
            for (int x = 0; x < nTbS; x++) {
                if (iFact != 0) {
                    DST(y, x) = ((32-iFact)*ref[x+iIdx+1] + iFact*ref[x+iIdx+2] + 16)>>5;
                } else {
                    DST(y, x) = ref[x+iIdx + 1];
                }

                if (predModeIntra == 26 && cIdx == 0 && nTbS < 32 &&
                    disableIntraBoundaryFilter == 0 && x == 0) {
                    DST(y, x) = clip3(0, (1 << (bitdepth))-1, top[x] + ((left[y]-top[-1])>>1));
                }
            }
        }
    } else {
        ref[0] = top[-1];
        for (int x = 1; x <= nTbS; x++) {
            ref[x] = left[x-1];
        }
        int angle = intraPredAngle[predModeIntra - 2];
        if (angle < 0) {
            if ((nTbS * angle >> 5) < -1) {
                for (int x = -1; x >= ((angle * nTbS) >> 5); x--) {
                    ref[x] = top[-1 + ((x * invAngle[predModeIntra-11] + 128)>> 8)];
                }
            }
        } else {
            for (int x = nTbS+1; x <= 2*nTbS; x++) {
                ref[x] = left[x-1];
            }
        }

        for (int x = 0; x < nTbS; x++) {
            int iIdx = (x + 1) * angle >> 5;
            int iFact = ((x + 1) * angle) & 31;
            for (int y = 0; y < nTbS; y++) {
                if (iFact != 0) {
                    DST(y, x) = ((32 - iFact) *ref[y + iIdx +1] + iFact * ref[y + iIdx + 2] + 16)>>5;
                } else {
                    DST(y, x) = ref[y + iIdx + 1];
                }

                if (predModeIntra == 10 && cIdx == 0 && nTbS < 32 &&
                    disableIntraBoundaryFilter == 0 && y == 0) {
                    DST(y, x) = clip3(0, (1 << (bitdepth)) - 1,
                                      left[y] + ((top[x] - top[-1]) >> 1));
                }
            }
        }
    }
}
