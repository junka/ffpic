#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "idct.h"
#include "utils.h"

#define DCTSIZE (8)
#define RIGHT_SHIFT(x, n) ((x) >> (n))
#define CONST_BITS 8

/* Some C compilers fail to reduce "FIX(constant)" at compile time, thus
 * causing a lot of useless floating-point operations at run time.
 * To get around this we use the following pre-calculated constants.
 * If you change CONST_BITS you may want to add appropriate values.
 * (With a reasonable C compiler, you can just rely on the FIX() macro...)
 */

#if CONST_BITS == 8
#define FIX_0_382683433 ((int32_t)98)  /* FIX(0.382683433) */
#define FIX_0_541196100 ((int32_t)139) /* FIX(0.541196100) */
#define FIX_0_707106781 ((int32_t)181) /* FIX(0.707106781) */
#define FIX_1_306562965 ((int32_t)334) /* FIX(1.306562965) */
#else
#define FIX_0_382683433 FIX(0.382683433)
#define FIX_0_541196100 FIX(0.541196100)
#define FIX_0_707106781 FIX(0.707106781)
#define FIX_1_306562965 FIX(1.306562965)
#endif

/* We can gain a little more speed, with a further compromise in accuracy,
 * by omitting the addition in a descaling shift.  This yields an incorrectly
 * rounded result half the time...
 */

#ifndef USE_ACCURATE_ROUNDING
#undef DESCALE
#define DESCALE(x, n) RIGHT_SHIFT(x, n)
#endif

/* Multiply a int16_t variable by an int32_t constant, and immediately
 * descale to yield a int16_t result.
 */

#define MULTIPLY(var, const) ((int16_t)DESCALE((var) * (const), CONST_BITS))

static void row_fdct(int16_t *data) {
  int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  int tmp10, tmp11, tmp12, tmp13;
  int z1, z2, z3, z4, z5, z11, z13;
  int16_t *dataptr;
  int ctr;

  /* Pass 1: process rows. */

  dataptr = data;
  for (ctr = DCTSIZE - 1; ctr >= 0; ctr--) {
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

    // 0, 7, 3, 4, 1, 6, 2, 5
    dataptr[0] = tmp10 + tmp11; /* phase 3 */
    // printf("data0 is %d\n", dataptr[0]);
    // 0, 7, 3, 4, -1, -6, -2, -5
    dataptr[4] = tmp10 - tmp11;

    // c4 * (1 + 6 - 2 - 5 + 0 + 7 - 3 - 4)
    z1 = MULTIPLY(tmp12 + tmp13, FIX_0_707106781); /* c4 */
    dataptr[2] = tmp13 + z1;                       /* phase 5 */
    dataptr[6] = tmp13 - z1;

    /* Odd part */

    tmp10 = tmp4 + tmp5; /* phase 2 */
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    /* The rotator is modified from fig 4-8 to avoid extra negations. */
    z5 = MULTIPLY(tmp10 - tmp12, FIX_0_382683433); /* c6 */
    z2 = MULTIPLY(tmp10, FIX_0_541196100) + z5;    /* c2-c6 */
    z4 = MULTIPLY(tmp12, FIX_1_306562965) + z5;    /* c2+c6 */
    z3 = MULTIPLY(tmp11, FIX_0_707106781);         /* c4 */

    z11 = tmp7 + z3; /* phase 5 */
    z13 = tmp7 - z3;

    dataptr[5] = z13 + z2; /* phase 6 */
    dataptr[3] = z13 - z2;
    dataptr[1] = z11 + z4;
    dataptr[7] = z11 - z4;

    dataptr += DCTSIZE; /* advance pointer to next row */
  }
}

/*
 * Perform the forward DCT on one block of samples.
 */

void ff_fdct_ifast(int16_t *data) {
  int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  int tmp10, tmp11, tmp12, tmp13;
  int z1, z2, z3, z4, z5, z11, z13;
  int16_t *dataptr;
  int ctr;

  row_fdct(data);
  printf("intermedia fast:\n");
  for (int i = 0; i < DCTSIZE; i++) {
    for (int j = 0; j < DCTSIZE; j++) {
      printf("%d ", data[DCTSIZE * i + j]);
    }
    printf("\n");
  }
  printf("\n");

  /* Pass 2: process columns. */

  dataptr = data;
  for (ctr = DCTSIZE - 1; ctr >= 0; ctr--) {
    tmp0 = dataptr[DCTSIZE * 0] + dataptr[DCTSIZE * 7];
    tmp7 = dataptr[DCTSIZE * 0] - dataptr[DCTSIZE * 7];
    tmp1 = dataptr[DCTSIZE * 1] + dataptr[DCTSIZE * 6];
    tmp6 = dataptr[DCTSIZE * 1] - dataptr[DCTSIZE * 6];
    tmp2 = dataptr[DCTSIZE * 2] + dataptr[DCTSIZE * 5];
    tmp5 = dataptr[DCTSIZE * 2] - dataptr[DCTSIZE * 5];
    tmp3 = dataptr[DCTSIZE * 3] + dataptr[DCTSIZE * 4];
    tmp4 = dataptr[DCTSIZE * 3] - dataptr[DCTSIZE * 4];

    /* Even part */

    tmp10 = tmp0 + tmp3; /* phase 2 */
    tmp13 = tmp0 - tmp3;
    tmp11 = tmp1 + tmp2;
    tmp12 = tmp1 - tmp2;

    dataptr[DCTSIZE * 0] = tmp10 + tmp11; /* phase 3 */
    dataptr[DCTSIZE * 4] = tmp10 - tmp11;

    z1 = MULTIPLY(tmp12 + tmp13, FIX_0_707106781); /* c4 */
    dataptr[DCTSIZE * 2] = tmp13 + z1;             /* phase 5 */
    dataptr[DCTSIZE * 6] = tmp13 - z1;

    /* Odd part */

    tmp10 = tmp4 + tmp5; /* phase 2 */
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    /* The rotator is modified from fig 4-8 to avoid extra negations. */
    z5 = MULTIPLY(tmp10 - tmp12, FIX_0_382683433); /* c6 */
    z2 = MULTIPLY(tmp10, FIX_0_541196100) + z5;    /* c2-c6 */
    z4 = MULTIPLY(tmp12, FIX_1_306562965) + z5;    /* c2+c6 */
    z3 = MULTIPLY(tmp11, FIX_0_707106781);         /* c4 */

    z11 = tmp7 + z3; /* phase 5 */
    z13 = tmp7 - z3;

    dataptr[DCTSIZE * 5] = z13 + z2; /* phase 6 */
    dataptr[DCTSIZE * 3] = z13 - z2;
    dataptr[DCTSIZE * 1] = z11 + z4;
    dataptr[DCTSIZE * 7] = z11 - z4;

    dataptr++; /* advance pointer to next column */
  }
}

#define alpha(u) ((u == 0) ? (1.0f/sqrt(8)) : 0.5)

void fdct2d8x8(int16_t *data) {
  int u, v;
  int x, y, i;
  float buf[64];
  float temp;
  for (u = 0; u < 8; u++) {
    for (v = 0; v < 8; v++) {
        temp = 0;
        for (x = 0; x < 8; x++) {
          for (y = 0; y < 8; y++) {
            temp += (data[y * 8 + x]) *
                    (float)cos((2.0f * x + 1.0f) / 16.0f * u * M_PI) *
                    (float)cos((2.0f * y + 1.0f) / 16.0f * v * M_PI);
          }
        }
        buf[v * 8 + u] = (alpha(u) * alpha(v) * temp);
    }
  }
  for (i = 0; i < 64; i++)
    data[i] = buf[i];
}

int test_dct() {
    int16_t data[] = {
        117, 115, 112, 112, 110, 108, 103, 101, 117, 115, 113, 113, 111,
        108, 103, 99,  116, 116, 115, 113, 111, 108, 102, 98,  116, 116,
        116, 114, 111, 107, 101, 97,  116, 117, 117, 115, 111, 104, 99,
        97,  117, 118, 117, 114, 109, 102, 98,  97,  118, 118, 117, 112,
        106, 100, 98,  98,  118, 118, 117, 111, 104, 99,  98,  97,
    };
    int16_t data1[] = {
        117, 115, 112, 112, 110, 108, 103, 101, 117, 115, 113, 113, 111,
        108, 103, 99,  116, 116, 115, 113, 111, 108, 102, 98,  116, 116,
        116, 114, 111, 107, 101, 97,  116, 117, 117, 115, 111, 104, 99,
        97,  117, 118, 117, 114, 109, 102, 98,  97,  118, 118, 117, 112,
        106, 100, 98,  98,  118, 118, 117, 111, 104, 99,  98,  97,
    };

    const struct dct_ops *dct = get_dct_ops(8);
    int16_t out[64];
    dct->fdct_8x8(data);
    // for (int i = 0; i < 64; i ++) {
    //   data1[i] -= 128;
    // }
    fdct2d8x8(data1);
    // jpeg_fdct(data);
    // ff_fdct_ifast(data1);
    // for (int i = 0; i < 8; i++) {
    //     for (int j = 0; j < 8; j++) {
    //         printf("%d ", (int)out[i * 8 + j]);
    //     }
    //     printf("\n");
    // }
    // printf("\n");

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            printf("%d ", (int)data[i * 8 + j]);
        }
        printf("\n");
    }
    printf("\n");

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            printf("%d ", (int)data1[i * 8 + j]);
        }
        printf("\n");
    }
    printf("\n");

    for (int i = 0; i < 64; i ++) {
        if (ABS(data[i]-data1[i])>1) {
            printf("not match %d: %d vs %d\n", i, (int16_t)(data[i]), data1[i]);
            return -1;
        }
    }
    return 0;
}


int main()
{

    return test_dct();
}
