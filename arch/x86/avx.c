#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "utils.h"
#include "accl.h"
#include "x86.h"

#ifdef __AVX2__

static inline __m256i clip_avx2(__m256i v, __m256i debias, int32_t shift) {
    __m256i truncable = _mm256_add_epi32(v, debias);
    return _mm256_srai_epi32(truncable, shift);
}

__m256i x86_idct_1dx4_avx2(const __m256i left, __m256i right, int shift) {
    const __m256i debias = _mm256_set1_epi32((1 << (shift - 1)));

    // 64bits holds 4 elements 16bit val which can be taken as a row
    // 2, 0, 2, 0 in 64bits/row, [8, 9, 10, 11, 0, 1, 2, 3, 8, 9, 10, 11, 0, 1, 2, 3]
    __m256i right_los = _mm256_permute4x64_epi64(right, _MM_SHUFFLE(2, 0, 2, 0));
    // 3, 1, 3, 1 in 64bits/row, [12, 13, 14, 15, 4, 5, 6, 7, 12, 13, 14, 15, 4, 5, 6, 7]
    __m256i right_his = _mm256_permute4x64_epi64(right, _MM_SHUFFLE(3, 1, 3, 1));

    // interleave 16bit element as from row 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1
    // each of below read half 32bits, cols_up hold col 0, 1, cols_dn hold col 2, 3
    // [8, 12, 9, 13, 10, 14, 11, 15, 8, 12, 9, 13, 10, 14, 11, 15]
    __m256i right_cols_up = _mm256_unpacklo_epi16(right_los, right_his);
    // [0, 4, 1, 5, 2, 6, 3, 7, 0, 4, 1, 5, 2, 6, 3, 7]
    __m256i right_cols_dn = _mm256_unpackhi_epi16(right_los, right_his);

    // all from col 0, 1
    // 32bits holds 2 elements, all 2-pair, row (0, 1), (8, 9)
    // [0, 1, 0, 1, 0, 1, 0, 1, 8, 9, 8, 9, 8, 9, 8, 9]
    __m256i left_slice1 = _mm256_shuffle_epi32(left, _MM_SHUFFLE(0, 0, 0, 0));
    // 32bits holds 2 elements, all 2-pair, row (4, 5), (12, 13)
    // [4, 5, 4, 5, 4, 5, 4, 5, 12, 13, 12, 13, 12, 13, 12, 13]
    __m256i left_slice3 = _mm256_shuffle_epi32(left, _MM_SHUFFLE(2, 2, 2, 2));

    // all from col 2, 3
    // 32bits holds 2 elements, all 2-pair, row (2, 3),(10, 11)
    // [2, 3, 2, 3, 2, 3, 2, 3, 10, 11, 10, 11, 10, 11, 10, 11]
    __m256i left_slice2 = _mm256_shuffle_epi32(left, _MM_SHUFFLE(1, 1, 1, 1));
    // 32bits holds 2 elements, all 2-pair, row (6, 7) ,(14, 15)
    // [6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15]
    __m256i left_slice4 = _mm256_shuffle_epi32(left, _MM_SHUFFLE(3, 3, 3, 3));

    // 8 elements fot 32bit results
    // [r0 * c8 + r1 * c12, r0 * c9 + r1 * c13, r0 * c10 + r1 * c14, r0 * c11 + r1 * c15,
    //  r8 * c8 + r9 * c12, r8 * c9 + r9 * c13, r8 * c10 + r9 * c14, r8 * c11 + r9 * c15]
    __m256i prod1 = _mm256_madd_epi16(left_slice1, right_cols_up);
    // [r4 * c8 + r5 * c12, r4 * c9 + r5 * c13, r4 * c10 + r5 * c14, r4 * c11 + r5 * c15,
    //  r12 * c + r13 * c12, r12 * c9 + r13 * c13, r12 * c10 + r13 * c14, r12 * c11 + r13 * c15]
    __m256i prod3 = _mm256_madd_epi16(left_slice3, right_cols_up);

    // [r2 * c0 + r3 * c4, r2 * c1 + r3 * c5, r2 * c2 + r3 * c6, r2 * c3 + r3 * c7,
    //  r10 * c0 + r11 * c4, r10 * c1 + r11 * c4, r10 * c2 + r11 * c6, r10 * c3 + r11 * c7]
    __m256i prod2 = _mm256_madd_epi16(left_slice2, right_cols_dn);
    __m256i prod4 = _mm256_madd_epi16(left_slice4, right_cols_dn);

    // 8 length 32bit,
    // [r0 * c8 + r1 * c12 + r2 * c0 + r3 * c4, r0 * c9 + r1 * c13 + r2 * c1 + r3 * c5,
    //  r0 * c10 + r1 * c14 + r2 * c2 + r3 * c6 , r0 * c11 + r1 * c15 + r2 * c3 + r3 * c7,
    //  r8 * c8 + r9 * c12 + r10 * c0 + r11 * c4, r8 * c9 + r9 * c13 + r10 * c1 + r11 * c4,
    //  r8 * c10 + r9 * c14 + r10 * c2 + r11 * c6, r8 * c11 + r9 * c15 + r10 * c3 + r11 * c7]
    __m256i rows_up = _mm256_add_epi32(prod1, prod2);
    __m256i rows_dn = _mm256_add_epi32(prod3, prod4);

    __m256i rows_up_tr = clip_avx2(rows_up, debias, shift);
    __m256i rows_dn_tr = clip_avx2(rows_dn, debias, shift);

    __m256i result = _mm256_packs_epi32(rows_up_tr, rows_dn_tr);
    return result;
}

static void x86_idct_4x4_avx2(int16_t *in, int16_t *out, int bitdepth)
{
    int bdShift = 20 - bitdepth;

    const int16_t transMatrix[4][4] = {{29, 55, 74, 84},
                                        {74, 74, 0, -74},
                                        {84, -29, -74, 55},
                                        {55, -84, 74, -29}};
    const int16_t transMatrixT[4][4] = {{29, 74, 84, 55},
                                        {55, 74, -29, -84},
                                        {74, 0, -74, 74},
                                        {84, -74, 55, -29}};

    __m256i tran =
        _mm256_load_si256((const __m256i *)transMatrix); // 16 * 16 = 256 bit size
    __m256i tranT = _mm256_load_si256((const __m256i *)transMatrixT);
    __m256i input = _mm256_load_si256((const __m256i *)in);
    __m256i tmp = x86_idct_1dx4_avx2(tranT, input, 7);
    __m256i ret = x86_idct_1dx4_avx2(tmp, tran, bdShift);
    _mm256_store_si256((__m256i *)out, ret);
}

struct accl_ops avx_accl = {
    .idct_4x4 = x86_idct_4x4_avx2,
    .type = SIMD_TYPE_AVX2,
};


void 
x86_avx2_init(void)
{
    accl_ops_register(&avx_accl);
}

#endif