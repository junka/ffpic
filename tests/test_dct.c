#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "idct.h"

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

void jpeg_fdct(float *data) {
  float tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  float tmp10, tmp11, tmp12, tmp13;
  float z1, z2, z3, z4, z5, z11, z13;
  float *dataptr;
  int ctr;

  /* Pass 1: process rows. */

  dataptr = data;
  for (ctr = 0; ctr < DCTSIZE; ctr++) {
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

    dataptr += DCTSIZE; /* advance pointer to next row */
  }

    printf("intermedia:\n");
  for (int i = 0; i < DCTSIZE; i++) {
    for (int j = 0; j < DCTSIZE; j++) {
        printf("%d ", (int)(data[DCTSIZE *i +j]));
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

    z1 = (tmp12 + tmp13) * ((float)0.707106781); /* c4 */
    dataptr[DCTSIZE * 2] = tmp13 + z1;           /* phase 5 */
    dataptr[DCTSIZE * 6] = tmp13 - z1;

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

    dataptr[DCTSIZE * 5] = z13 + z2; /* phase 6 */
    dataptr[DCTSIZE * 3] = z13 - z2;
    dataptr[DCTSIZE * 1] = z11 + z4;
    dataptr[DCTSIZE * 7] = z11 - z4;

    dataptr++; /* advance pointer to next column */
  }
}


int test_dct() {
    int16_t data[] = {
        117, 115, 112, 112, 110, 108, 103, 101, 117, 115, 113, 113, 111,
        108, 103, 99,  116, 116, 115, 113, 111, 108, 102, 98,  116, 116,
        116, 114, 111, 107, 101, 97,  116, 117, 117, 115, 111, 104, 99,
        97,  117, 118, 117, 114, 109, 102, 98,  97,  118, 118, 117, 112,
        106, 100, 98,  98,  118, 118, 117, 111, 104, 99,  98,  97,
    };
    int16_t expect[] = {
        6991, 611, -116, -5, 1,  -1, 0, 0,  58, -157, -37, 101, 0,  0,  1,  -4,
        -27,  -6,  84,   -5, 2,  6,  0, -1, 4,  2,    -3,  -7,  17, -5, -2, 0,
        -3,   -3,  0,    -1, -1, 1,  0, 0,  0,  -4,   7,   0,   -2, -1, 1,  0,
        0,    1,   0,    0,  -2, -1, 0, 0,  0,  0,    0,   -1,  0,  0,  0,  0,
    };

    int16_t data1[] = {224 , 224 , 223 , 222 , 222 , 223 , 223 , 223 , 
        224 , 224 , 223 , 222 , 221 , 222 , 223 , 223 , 
        225 , 223 , 222 , 220 , 220 , 220 , 222 , 222 , 
        225 , 224 , 222 , 219 , 218 , 219 , 220 , 221 , 
        226 , 224 , 221 , 218 , 217 , 217 , 218 , 219 , 
        226 , 223 , 221 , 218 , 216 , 214 , 215 , 217 , 
        225 , 223 , 221 , 216 , 215 , 214 , 215 , 215 , 
        225 , 223 , 220 , 216 , 215 , 214 , 214 , 213
    };
    int16_t expect1[] = {
        14102, 187, 114, -2, -6, 0, 1 ,2 ,
        175 ,-172, -35, 0 ,-5, 1 ,-1, -2 ,
        -5, 1, -33, 9, -9, 0, -1, 0 ,
        1, 11, 0, -7, 8, -2, 1, 0 ,
        2, 0, 0, 8, -6, -3, 0, -1, 
        -1, -2, 2, -1, -2, 0, 1, 0, 
        -2, 2, 0, 1, 5, 0, 0, 0, 
        0, -1, 1, 1, 0, 0, 0, 0,
    };

    const struct dct_ops *dct = get_dct_ops(8);
    int16_t out[64];
    dct->fdct_8x8(data1, 8);
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
            printf("%d ", (int)data1[i * 8 + j]);
        }
        printf("\n");
    }
    printf("\n");

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            printf("%d ", (int)expect1[i * 8 + j]);
        }
        printf("\n");
    }
    printf("\n");

    for (int i = 0; i < 64; i ++) {
        if ((int16_t)(data[i]) != expect[i]) {
            printf("not match %d: %d vs %d\n", i, (int16_t)(out[i]), expect[i]);
            return -1;
        }
    }
    return 0;
}


int main()
{

    return test_dct();
}
