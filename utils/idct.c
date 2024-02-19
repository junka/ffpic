#include <stdint.h>

#include "utils.h"
#include "idct.h"

#if 1
// got from hevc spec 8.6.4.2
static void
idct_1d_4_16bit(const int16_t *in, int16_t out[4], int mincoeff, int maxcoeff, int bdShift)
{
    const int8_t transMatrix[4][4] = {
    {29, 55, 74, 84},
    {74, 74, 0, -74},
    {84, -29, -74, 55},
    {55, -84, 74, -29}
    };

    int last_nz = 0;
    for (int i = 3; i >= 0; i--) {
        if (in[i]) {
            last_nz = i;
            break;
        }
    }
    for (int i = 0; i < 4; i++) {
        int tmp = 0;
        for (int j = 0; j <= last_nz; j++) {
            tmp += transMatrix[j][i] * in[j];
        }
        out[i] = clip3(mincoeff, maxcoeff, (tmp + (bdShift - 1)) >> bdShift);
    }
}
// hevc spec 8.6.4.1
void idct_4x4_hevc(const int16_t *in, int16_t *out, int bitdepth, bool epp)
{

    int bdShift = MAX(20 - bitdepth, (epp ? 11 : 0));
    int mincoeff = -(1 << (epp ? MAX(15, bitdepth + 6) : 15));
    int maxcoeff = (1 << (epp ? MAX(15, bitdepth + 6) : 15)) - 1;
    int16_t tmp[4];
    int16_t e[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[j] = in[i + j * 4];
        }
        idct_1d_4_16bit(tmp, e[i], mincoeff, maxcoeff, 7);
    }
    for (int j = 0; j < 4; j ++) {
        for (int i = 0; i < 4; i ++) {
            tmp[i] = e[i][j];
        }
        idct_1d_4_16bit(tmp, out + j * 4, mincoeff, maxcoeff, bdShift);
    }
}
#endif
void idct_1d_4_8bits(const uint8_t *in, uint8_t out[4], int mincoeff,
                     int maxcoeff, int bdShift) {
    const int8_t transMatrix[4][4] = {{29, 55, 74, 84},
                                      {74, 74, 0, -74},
                                      {84, -29, -74, 55},
                                      {55, -84, 74, -29}};

    int last_nz = 0;
    for (int i = 3; i >= 0; i--) {
        if (in[i]) {
            last_nz = i;
            break;
        }
    }
    for (int i = 0; i < 4; i++) {
        int tmp = 0;
        for (int j = 0; j <= last_nz; j++) {
            tmp += transMatrix[j][i] * in[j];
        }
        out[i] = clip3(mincoeff, maxcoeff, (tmp + (bdShift - 1)) >> bdShift);
    }
}
void idct_4x4_8(const void *input, void *output, int bitdepth) {
    uint8_t *in = (uint8_t *)input;
    uint8_t *out = (uint8_t *)output;
    uint8_t tmp[4];
    uint8_t e[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[j] = in[i + j * 4];
        }
        idct_1d_4_8bits(tmp, e[i], 0, 255, 7);
    }
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            tmp[i] = e[i][j];
        }
        idct_1d_4_8bits(tmp, out + j * 4, 0, 255, 7);
    }
}

static void idct_4x4_16(void *input, int bitdepth)
{
    int16_t *in = (int16_t *)input;
    int16_t *out = (int16_t *)input;
    // int bdShift = 20 - bitdepth;
    // int mincoeff = - (1 << 15);
    // int maxcoeff = (1 <<15) - 1;
    // int16_t tmp[4];
    // int16_t e[4][4];
    // for (int i = 0; i < 4; i++) {
    //     for (int j = 0; j < 4; j++) {
    //         tmp[j] = in[i + j * 4];
    //     }
    //     idct_1d_4(tmp, e[i], mincoeff, maxcoeff, 7);
    // }
    // for (int j = 0; j < 4; j++) {
    //     for (int i = 0; i < 4; i++) {
    //         tmp[i] = e[i][j];
    //     }
    //     idct_1d_4(tmp, out + j * 4, mincoeff, maxcoeff, bdShift);
    // }
#if 1
    static const int c1 = 20091;
    static const int c2 = 35468;
    int tmp[16];
    int i;
    for (i = 0; i < 4; ++i) {
        const int a0 = in[0 + i] + in[8 + i];
        const int a1 = in[0 + i] - in[8 + i];
        const int a2 =
            ((in[4 + i] * c2) >> 16) - in[12 + i] - ((in[12 + i] * c1) >> 16);
        const int a3 =
            in[4 + i] + ((in[4 + i] * c1) >> 16) + ((in[12 + i] * c2) >> 16);
        tmp[0 + i] = a0 + a3;
        tmp[12 + i] = a0 - a3;
        tmp[4 + i] = a1 + a2;
        tmp[8 + i] = a1 - a2;
    }
    for (i = 0; i < 4; ++i) {
        const int a0 = tmp[0 + i * 4] + tmp[2 + i * 4];
        const int a1 = tmp[0 + i * 4] - tmp[2 + i * 4];
        const int a2 = (tmp[1 + i * 4] * c2 >> 16) - tmp[3 + i * 4] -
                        (tmp[3 + i * 4] * c1 >> 16);
        const int a3 = tmp[1 + i * 4] + (tmp[1 + i * 4] * c1 >> 16) +
                        (tmp[3 + i * 4] * c2 >> 16);
        out[4 * i + 0] = (a0 + a3 + 4) >> 3;
        out[4 * i + 3] = (a0 - a3 + 4) >> 3;
        out[4 * i + 1] = (a1 + a2 + 4) >> 3;
        out[4 * i + 2] = (a1 - a2 + 4) >> 3;
    }
#endif
}

#if 0
static inline uint8_t
descale_and_clamp(int x, int shift)
{
    x += (1UL << (shift - 1));
    if (x < 0)
        x = (x >> shift) | ((~(0UL)) << (32 - (shift)));
    else
        x >>= shift;
    x += 128;
    return clamp(x, 255);
}

/* keep it slow, but high quality */
void
idct_float(int *buf, int *output, int stride)
{
    const float m0 = 2.0 * cos(1.0 / 16.0 * 2.0 * M_PI);
    const float m1 = 2.0 * cos(2.0 / 16.0 * 2.0 * M_PI);
    const float m3 = 2.0 * cos(2.0 / 16.0 * 2.0 * M_PI);
    const float m5 = 2.0 * cos(3.0 / 16.0 * 2.0 * M_PI);
    const float m2 = m0 - m5;
    const float m4 = m0 + m5;
    const float s0 = cos(0.0 / 16.0 * M_PI) / sqrt(8);
    const float s1 = cos(1.0 / 16.0 * M_PI) / 2.0;
    const float s2 = cos(2.0 / 16.0 * M_PI) / 2.0;
    const float s3 = cos(3.0 / 16.0 * M_PI) / 2.0;
    const float s4 = cos(4.0 / 16.0 * M_PI) / 2.0;
    const float s5 = cos(5.0 / 16.0 * M_PI) / 2.0;
    const float s6 = cos(6.0 / 16.0 * M_PI) / 2.0;
    const float s7 = cos(7.0 / 16.0 * M_PI) / 2.0;
    
    int *inptr;
    float *wsptr;
    int *outptr;
    int ctr;
    float workspace[64]; /* buffers data between passes */

    /* Pass 1: process columns from input, store into work array. */
    inptr = buf;

    wsptr = workspace;
    for (ctr = 8; ctr > 0; ctr--) {
        const float g0 = inptr[0 * 8] * s0;
        const float g1 = inptr[4 * 8] * s4;
        const float g2 = inptr[2 * 8] * s2;
        const float g3 = inptr[6 * 8] * s6;
        const float g4 = inptr[5 * 8] * s5;
        const float g5 = inptr[1 * 8] * s1;
        const float g6 = inptr[7 * 8] * s7;
        const float g7 = inptr[3 * 8] * s3;

        /* Even part */
        const float f0 = g0;
        const float f1 = g1;
        const float f2 = g2;
        const float f3 = g3;
        const float f4 = g4 - g7;
        const float f5 = g5 + g6;
        const float f6 = g5 - g6;
        const float f7 = g4 + g7;

        const float e0 = f0;
        const float e1 = f1;
        const float e2 = f2 - f3;
        const float e3 = f2 + f3;
        const float e4 = f4;
        const float e5 = f5 - f7;
        const float e6 = f6;
        const float e7 = f5 + f7;
        const float e8 = f4 + f6;

        const float d0 = e0;
        const float d1 = e1;
        const float d2 = e2 * m1;
        const float d3 = e3;
        const float d4 = e4 * m2;
        const float d5 = e5 * m3;
        const float d6 = e6 * m4;
        const float d7 = e7;
        const float d8 = e8 * m5;

        const float c0 = d0 + d1;
        const float c1 = d0 - d1;
        const float c2 = d2 - d3;
        const float c3 = d3;
        const float c4 = d4 + d8;
        const float c5 = d5 + d7;
        const float c6 = d6 - d8;
        const float c7 = d7;
        const float c8 = c5 - c6;

        const float b0 = c0 + c3;
        const float b1 = c1 + c2;
        const float b2 = c1 - c2;
        const float b3 = c0 - c3;
        const float b4 = c4 - c8;
        const float b5 = c8;
        const float b6 = c6 - c7;
        const float b7 = c7;

        wsptr[0 * 8] = b0 + b7;
        wsptr[1 * 8] = b1 + b6;
        wsptr[2 * 8] = b2 + b5;
        wsptr[3 * 8] = b3 + b4;
        wsptr[4 * 8] = b3 - b4;
        wsptr[5 * 8] = b2 - b5;
        wsptr[6 * 8] = b1 - b6;
        wsptr[7 * 8] = b0 - b7;

        inptr++;            /* advance pointers to next column */
        wsptr++;
    }
    
    /* Pass 2: process rows from work array, store into output array. */
    /* Note that we must descale the results by a factor of 8 == 2**3. */

    wsptr = workspace;
    outptr = output;
    for (ctr = 0; ctr < 8; ctr++) {
        /* Rows of zeroes can be exploited in the same way as we did with columns.
        * However, the column calculation has created many nonzero AC terms, so
        * the simplification applies less often (typically 5% to 10% of the time).
        * And testing floats for zero is relatively expensive, so we don't bother.
        */

        /* Even part */
        const float g0 = wsptr[0] * s0;
        const float g1 = wsptr[4] * s4;
        const float g2 = wsptr[2] * s2;
        const float g3 = wsptr[6] * s6;
        const float g4 = wsptr[5] * s5;
        const float g5 = wsptr[1] * s1;
        const float g6 = wsptr[7] * s7;
        const float g7 = wsptr[3] * s3;

        const float f0 = g0;
        const float f1 = g1;
        const float f2 = g2;
        const float f3 = g3;
        const float f4 = g4 - g7;
        const float f5 = g5 + g6;
        const float f6 = g5 - g6;
        const float f7 = g4 + g7;

        const float e0 = f0;
        const float e1 = f1;
        const float e2 = f2 - f3;
        const float e3 = f2 + f3;
        const float e4 = f4;
        const float e5 = f5 - f7;
        const float e6 = f6;
        const float e7 = f5 + f7;
        const float e8 = f4 + f6;

        const float d0 = e0;
        const float d1 = e1;
        const float d2 = e2 * m1;
        const float d3 = e3;
        const float d4 = e4 * m2;
        const float d5 = e5 * m3;
        const float d6 = e6 * m4;
        const float d7 = e7;
        const float d8 = e8 * m5;

        const float c0 = d0 + d1;
        const float c1 = d0 - d1;
        const float c2 = d2 - d3;
        const float c3 = d3;
        const float c4 = d4 + d8;
        const float c5 = d5 + d7;
        const float c6 = d6 - d8;
        const float c7 = d7;
        const float c8 = c5 - c6;

        const float b0 = c0 + c3;
        const float b1 = c1 + c2;
        const float b2 = c1 - c2;
        const float b3 = c0 - c3;
        const float b4 = c4 - c8;
        const float b5 = c8;
        const float b6 = c6 - c7;
        const float b7 = c7;

        /* Final output stage:
         No scale down for quality
         and range-limit */
        outptr[0] = descale_and_clamp((int)(b0 + b7), 0);
        outptr[7] = descale_and_clamp((int)(b0 - b7), 0);
        outptr[1] = descale_and_clamp((int)(b1 + b6), 0);
        outptr[6] = descale_and_clamp((int)(b1 - b6), 0);
        outptr[2] = descale_and_clamp((int)(b2 + b5), 0);
        outptr[5] = descale_and_clamp((int)(b2 - b5), 0);
        outptr[4] = descale_and_clamp((int)(b3 + b4), 0);
        outptr[3] = descale_and_clamp((int)(b3 - b4), 0);
        
        wsptr += 8;        /* advance pointer to next row */
        outptr += stride;
    }
}
#endif

// idct_transform[8*x+u] = alpha(u)*cos((2*x+1)*u*M_PI/16)*sqrt(2), with fixed 13
// bit precision, where alpha(0) = 1/sqrt(2) and alpha(u) = 1 for u > 0.
// Some coefficients are off by +-1 to mimick libjpeg's behaviour.
static const int idct_transform[64] = {
    8192,  11363,  10703, 9633, 8192, 6437, 4433, 2260,
    8192,  9633, 4433, -2259, -8192, -11362, -10704, -6436,
    8192, 6437, -4433, -11362, -8192, 2261, 10704, 9633,
    8192, 2260, -10703, -6436, 8192, 9633, -4433, -11363,
    8192, -2260, -10703, 6436, 8192, -9633, -4433, 11363,
    8192, -6437, -4433, 11362, -8192, -2261, 10704, -9633,
    8192, -9633, 4433,  2259, -8192, 11362, -10704, 6436,
    8192, -11363, 10703, -9633, 8192, -6437, 4433, -2260,
};

static inline void 
idct_1d_8_8bit(const uint8_t *in, const int stride, int out[8]) {
    for (int i = 0; i < 8; i++) {
        int tmp = 0;
        for (int u = 0; u < 8; ++u) {
            tmp += idct_transform[8 * i + u] * in[u * stride];
        }
        out[i] = tmp;
    }
}

static inline void 
idct_1d_8(const int16_t *in, const int stride, int out[8]) {
    for (int i = 0; i < 8; i++) {
        out[i] = 0;
        for (int u = 0; u < 8; ++u) {
            out[i] += idct_transform[8 * i + u] * in[u * stride];
        }
    }
#if 0
    // which can be expanded to below
    // int tmp0, tmp1, tmp2, tmp3, tmp4;

    // tmp1 = idct_transform[0] * in[0];
    // out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = tmp1;

    // tmp0 = in[stride];
    // tmp1 = idct_transform[1] * tmp0;
    // tmp2 = idct_transform[9] * tmp0;
    // tmp3 = idct_transform[17] * tmp0;
    // tmp4 = idct_transform[25] * tmp0;
    // out[0] += tmp1;
    // out[1] += tmp2;
    // out[2] += tmp3;
    // out[3] += tmp4;
    // out[4] -= tmp4;
    // out[5] -= tmp3;
    // out[6] -= tmp2;
    // out[7] -= tmp1;

    // tmp0 = in[2 * stride];
    // tmp1 = idct_transform[2] * tmp0;
    // tmp2 = idct_transform[10] * tmp0;
    // out[0] += tmp1;
    // out[1] += tmp2;
    // out[2] -= tmp2;
    // out[3] -= tmp1;
    // out[4] -= tmp1;
    // out[5] -= tmp2;
    // out[6] += tmp2;
    // out[7] += tmp1;

    // tmp0 = in[3 * stride];
    // tmp1 = idct_transform[3] * tmp0;
    // tmp2 = idct_transform[11] * tmp0;
    // tmp3 = idct_transform[19] * tmp0;
    // tmp4 = idct_transform[27] * tmp0;
    // out[0] += tmp1;
    // out[1] += tmp2;
    // out[2] += tmp3;
    // out[3] += tmp4;
    // out[4] -= tmp4;
    // out[5] -= tmp3;
    // out[6] -= tmp2;
    // out[7] -= tmp1;

    // tmp0 = in[4 * stride];
    // tmp1 = idct_transform[4] * tmp0;
    // out[0] += tmp1;
    // out[1] -= tmp1;
    // out[2] -= tmp1;
    // out[3] += tmp1;
    // out[4] += tmp1;
    // out[5] -= tmp1;
    // out[6] -= tmp1;
    // out[7] += tmp1;

    // tmp0 = in[5 * stride];
    // tmp1 = idct_transform[5] * tmp0;
    // tmp2 = idct_transform[13] * tmp0;
    // tmp3 = idct_transform[21] * tmp0;
    // tmp4 = idct_transform[29] * tmp0;
    // out[0] += tmp1;
    // out[1] += tmp2;
    // out[2] += tmp3;
    // out[3] += tmp4;
    // out[4] -= tmp4;
    // out[5] -= tmp3;
    // out[6] -= tmp2;
    // out[7] -= tmp1;

    // tmp0 = in[6 * stride];
    // tmp1 = idct_transform[6] * tmp0;
    // tmp2 = idct_transform[14] * tmp0;
    // out[0] += tmp1;
    // out[1] += tmp2;
    // out[2] -= tmp2;
    // out[3] -= tmp1;
    // out[4] -= tmp1;
    // out[5] -= tmp2;
    // out[6] += tmp2;
    // out[7] += tmp1;

    // tmp0 = in[7 * stride];
    // tmp1 = idct_transform[7] * tmp0;
    // tmp2 = idct_transform[15] * tmp0;
    // tmp3 = idct_transform[23] * tmp0;
    // tmp4 = idct_transform[31] * tmp0;
    // out[0] += tmp1;
    // out[1] += tmp2;
    // out[2] += tmp3;
    // out[3] += tmp4;
    // out[4] -= tmp4;
    // out[5] -= tmp3;
    // out[6] -= tmp2;
    // out[7] -= tmp1;
#endif
}

static void idct_8x8_8(void *block, int depth) {
    uint8_t *in = (uint8_t *)block;
    uint8_t *out = (uint8_t *)block;
    uint8_t colidcts[64];
    const int kColScale = 11;
    const int kColRound = 1 << (kColScale - 1);
    for (int x = 0; x < 8; ++x) {
        int colbuf[8] = {0};
        idct_1d_8_8bit(in + x, 8, colbuf);
        for (int y = 0; y < 8; ++y) {
            colidcts[8 * y + x] = (colbuf[y] + kColRound) >> kColScale;
        }
    }
    const int kRowScale = 18;
    const int kRowRound = 257 << (kRowScale - 1); // includes offset by 128
    for (int y = 0; y < 8; ++y) {
        int rowbuf[8] = {0};
        idct_1d_8_8bit(&colidcts[8 * y], 1, rowbuf);
        for (int x = 0; x < 8; ++x) {
            out[y * 8 + x] = clamp(((rowbuf[x] + kRowRound) >> kRowScale), 255);
        }
    }
}

static void idct_8x8_16(void *block, int depth) {
    int16_t *in = (int16_t *)block;
    int16_t *out = (int16_t *)block;
    int16_t colidcts[64];
    const int kColScale = 11;
    const int kColRound = 1 << (kColScale - 1);
    for (int x = 0; x < 8; ++x) {
        int colbuf[8] = {0};
        idct_1d_8(in + x, 8, colbuf);
        for (int y = 0; y < 8; ++y) {
            colidcts[8 * y + x] = (colbuf[y] + kColRound) >> kColScale;
        }
    }
    const int kRowScale = 18;
    const int kRowRound = 257 << (kRowScale - 1); // includes offset by 128
    for (int y = 0; y < 8; ++y) {
        int rowbuf[8] = {0};
        idct_1d_8(&colidcts[8 * y], 1, rowbuf);
        for (int x = 0; x < 8; ++x) {
            out[y * 8 + x] = clamp(((rowbuf[x] + kRowRound) >> kRowScale), 255);
        }
    }
}

void dct_float(float *data) {
    float tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    float tmp10, tmp11, tmp12, tmp13;
    float z1, z2, z3, z4, z5, z11, z13;
    float *dataptr;

    /* Pass 1: process rows. */

    dataptr = data;
    for (int ctr = 0; ctr < 8; ctr++) {
        /* Load data into workspace */
        tmp0 = dataptr[0] + dataptr[7];
        tmp7 = dataptr[0] - dataptr[7];
        tmp1 = dataptr[1] + dataptr[6];
        tmp6 = dataptr[1] - dataptr[6];
        tmp2 = dataptr[2] + dataptr[5];
        tmp5 = dataptr[2] - dataptr[5];
        tmp3 = dataptr[3] + dataptr[4];
        tmp4 = dataptr[3] - dataptr[4];

        /* Even part */

        tmp10 = tmp0 + tmp3; /* phase 2 */
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        /* Apply unsigned->signed conversion */
        dataptr[0] = tmp10 + tmp11; /* phase 3 */
        dataptr[4] = tmp10 - tmp11;

        z1 = (tmp12 + tmp13) * ((float)0.707106781); /* c4 */
        dataptr[2] = tmp13 + z1;                     /* phase 5 */
        dataptr[6] = tmp13 - z1;

        /* Odd part */

        tmp10 = tmp4 + tmp5; /* phase 2 */
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;

        /* The rotator is modified from fig 4-8 to avoid extra negations. */
        z5 = (tmp10 - tmp12) * ((float)0.382683433); /* c6 */
        z2 = ((float)0.541196100) * tmp10 + z5;      /* c2-c6 */
        z4 = ((float)1.306562965) * tmp12 + z5;      /* c2+c6 */
        z3 = tmp11 * ((float)0.707106781);           /* c4 */

        z11 = tmp7 + z3; /* phase 5 */
        z13 = tmp7 - z3;

        dataptr[5] = z13 + z2; /* phase 6 */
        dataptr[3] = z13 - z2;
        dataptr[1] = z11 + z4;
        dataptr[7] = z11 - z4;

        dataptr += 8; /* advance pointer to next row */
    }

    /* Pass 2: process columns. */

    dataptr = data;
    for (int ctr = 8 - 1; ctr >= 0; ctr--) {
        tmp0 = dataptr[8 * 0] + dataptr[8 * 7];
        tmp7 = dataptr[8 * 0] - dataptr[8 * 7];
        tmp1 = dataptr[8 * 1] + dataptr[8 * 6];
        tmp6 = dataptr[8 * 1] - dataptr[8 * 6];
        tmp2 = dataptr[8 * 2] + dataptr[8 * 5];
        tmp5 = dataptr[8 * 2] - dataptr[8 * 5];
        tmp3 = dataptr[8 * 3] + dataptr[8 * 4];
        tmp4 = dataptr[8 * 3] - dataptr[8 * 4];

        /* Even part */

        tmp10 = tmp0 + tmp3; /* phase 2 */
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        dataptr[8 * 0] = tmp10 + tmp11; /* phase 3 */
        dataptr[8 * 4] = tmp10 - tmp11;

        z1 = (tmp12 + tmp13) * ((float)0.707106781); /* c4 */
        dataptr[8 * 2] = tmp13 + z1;           /* phase 5 */
        dataptr[8 * 6] = tmp13 - z1;

        /* Odd part */

        tmp10 = tmp4 + tmp5; /* phase 2 */
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;

        /* The rotator is modified from fig 4-8 to avoid extra negations. */
        z5 = (tmp10 - tmp12) * ((float)0.382683433); /* c6 */
        z2 = ((float)0.541196100) * tmp10 + z5;      /* c2-c6 */
        z4 = ((float)1.306562965) * tmp12 + z5;      /* c2+c6 */
        z3 = tmp11 * ((float)0.707106781);           /* c4 */

        z11 = tmp7 + z3; /* phase 5 */
        z13 = tmp7 - z3;

        dataptr[8 * 5] = z13 + z2; /* phase 6 */
        dataptr[8 * 3] = z13 - z2;
        dataptr[8 * 1] = z11 + z4;
        dataptr[8 * 7] = z11 - z4;

        dataptr++; /* advance pointer to next column */
    }
}

static const int dct_transform[64] = {
    // 2896,  2896,  2896,  2896,  2896,  2896,  2896,  2896,
    // 4017,  3405, 2275,  799,   -799,  -2275, -3405, -4017,
    // 3784,  1567,  -1567, -3784, -3784, -1567, 1567,  3784,
    // 3405,  -799,  -4017, -2275, 2275,  4017, 799,   -3405,
    // 2896,  -2896, -2896, 2896,  2896,  -2896, -2896, 2896,
    // 2275,  -4017, 799,   3405,  -3405, -799,  4017,  -2275,
    // 1567,  -3784, 3784,  -1567, -1567, 3784,  -3784, 1567,
    // 799,   -2275, 3405,  -4017, 4017,  -3405, 2275,  -799,
    5792,     5792,     5792,     5792,     5792,     5792,     5792,     5792, 
    8034,     6811,     4551,     1598,    -1598,    -4551,    -6811,    -8034, 
    7568,     3134,    -3134,    -7568,    -7568,    -3134,     3134,     7568, 
    6811,    -1598,    -8034,    -4551,     4551,     8034,     1598,    -6811, 
    5792,    -5792,    -5792,     5792,     5792,    -5792,    -5792,     5792, 
    4551,    -8034,     1598,     6811,    -6811,    -1598,     8034,    -4551, 
    3134,    -7568,     7568,    -3134,    -3134,     7568,    -7568,     3134, 
    1598,    -4551,     6811,    -8034,     8034,    -6811,     4551,    -1598,
};

static inline void 
dct_1d_8(const int16_t *in, const int stride, int out[8]) {
    for (int i = 0; i < 8; i++){
        out[i] = 0;
        for (int u = 0; u < 8; ++u) {
            out[i] += dct_transform[8 * i + u] * in[u * stride];
        }
        out[i] >>= 1;
    }
}

static void
fdct_8x8_8(void *data, int bitdepth) {
    int16_t *block = (int16_t *)data;
    int16_t rowdcts[64];
    const int kRowScale = 11;
    const int kRowRound = (1 << (kRowScale - 1));
    //do for each row
    for (int y = 0; y < 8; y++) {
        int rowbuf[8];
        dct_1d_8(block + y * 8, 1, rowbuf);
        for (int x = 0; x < 8; x++) {
            rowdcts[y * 8 + x] = (rowbuf[x] + kRowRound) >> kRowScale;
            // coldcts[y * 8 + x] = colbuff[y];
        }
    }
    // printf("intermedia:\n");
    // for (int i = 0; i < 8; i++) {
    //     for (int j = 0; j < 8; j++) {
    //         printf("%d ", rowdcts[8 * i + j]);
    //     }
    //     printf("\n");
    // }
    // printf("\n");
    const int kColScale = 12;
    const int kColRound = 1 << (kColScale - 1);
    for (int x = 0; x < 8; x++) {
        int colbuf[8] = {0};
        dct_1d_8(&rowdcts[x], 8, colbuf);
        for (int y = 0; y < 8; y++) {
            block[y * 8 + x] = ((colbuf[y] + kColRound) >> kColScale);
            // out[y * stride + x] = rowbuf[x];
        }
    }
}

enum dct_type {
    DCT_BITLEN_8 = 0,
    DCT_BITLEN_16 = 1,

    DCT_BITLEN_MAX,
};

static const struct dct_ops dops[DCT_BITLEN_MAX] = {
    {
        .bitdepth = 8,
        .idct_8x8 = idct_8x8_8,
        .fdct_8x8 = fdct_8x8_8,
    },
    {
        .bitdepth = 16,
        .idct_4x4 = idct_4x4_16,
        .idct_8x8 = idct_8x8_16,
    },
};

const struct dct_ops *get_dct_ops(int component_bits)
{
    return &dops[(component_bits - 1) / 8];
}
