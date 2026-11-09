#include <stdint.h>
#include <string.h>

#include "accl.h"
#include "idct.h"
#include "x86.h"
#include "utils.h"

#ifdef __SSE2__

// Transpose two 4x4 16b matrices
static void
x86_idct_1dx4_sse2_16bit(const __m128i *const in0,
                  const __m128i *const in1,
                  const __m128i *const in2,
                  const __m128i *const in3, __m128i *const out0,
                  __m128i *const out1, __m128i *const out2,
                  __m128i *const out3) {
    // Transpose the two 4x4.
    // a00 a01 a02 a03   b00 b01 b02 b03
    // a10 a11 a12 a13   b10 b11 b12 b13
    // a20 a21 a22 a23   b20 b21 b22 b23
    // a30 a31 a32 a33   b30 b31 b32 b33
    const __m128i transpose0_0 = _mm_unpacklo_epi16(*in0, *in1);
    const __m128i transpose0_1 = _mm_unpacklo_epi16(*in2, *in3);
    const __m128i transpose0_2 = _mm_unpackhi_epi16(*in0, *in1);
    const __m128i transpose0_3 = _mm_unpackhi_epi16(*in2, *in3);
    // a00 a10 a01 a11   a02 a12 a03 a13
    // a20 a30 a21 a31   a22 a32 a23 a33
    // b00 b10 b01 b11   b02 b12 b03 b13
    // b20 b30 b21 b31   b22 b32 b23 b33
    const __m128i transpose1_0 = _mm_unpacklo_epi32(transpose0_0, transpose0_1);
    const __m128i transpose1_1 = _mm_unpacklo_epi32(transpose0_2, transpose0_3);
    const __m128i transpose1_2 = _mm_unpackhi_epi32(transpose0_0, transpose0_1);
    const __m128i transpose1_3 = _mm_unpackhi_epi32(transpose0_2, transpose0_3);
    // a00 a10 a20 a30 a01 a11 a21 a31
    // b00 b10 b20 b30 b01 b11 b21 b31
    // a02 a12 a22 a32 a03 a13 a23 a33
    // b02 b12 a22 b32 b03 b13 b23 b33
    *out0 = _mm_unpacklo_epi64(transpose1_0, transpose1_1);
    *out1 = _mm_unpackhi_epi64(transpose1_0, transpose1_1);
    *out2 = _mm_unpacklo_epi64(transpose1_2, transpose1_3);
    *out3 = _mm_unpackhi_epi64(transpose1_2, transpose1_3);
    // a00 a10 a20 a30   b00 b10 b20 b30
    // a01 a11 a21 a31   b01 b11 b21 b31
    // a02 a12 a22 a32   b02 b12 b22 b32
    // a03 a13 a23 a33   b03 b13 b23 b33
}

static void x86_idct_4x4_sse2_16bit(int16_t *in, int bitdepth UNUSED) {
    // This implementation makes use of 16-bit fixed point versions of two
    // multiply constants:
    //    K1 = sqrt(2) * cos (pi/8) ~= 85627 / 2^16
    //    K2 = sqrt(2) * sin (pi/8) ~= 35468 / 2^16
    //
    // To be able to use signed 16-bit integers, we use the following trick to
    // have constants within range:
    // - Associated constants are obtained by subtracting the 16-bit fixed point
    //   version of one:
    //      k = K - (1 << 16)  =>  K = k + (1 << 16)
    //      K1 = 85267  =>  k1 =  20091
    //      K2 = 35468  =>  k2 = -30068
    // - The multiplication of a variable by a constant become the sum of the
    //   variable and the multiplication of that variable by the associated
    //   constant:
    //      (x * K) >> 16 = (x * (k + (1 << 16))) >> 16 = ((x * k ) >> 16) + x
    const __m128i k1 = _mm_set1_epi16(20091);
    const __m128i k2 = _mm_set1_epi16(-30068);
    __m128i T0, T1, T2, T3;

    // Load and concatenate the transform coefficients (we'll do two transforms
    // in parallel). In the case of only one transform, the second half of the
    // vectors will just contain random value we'll never use nor store.
    __m128i in0, in1, in2, in3;
    {
        in0 = _mm_loadl_epi64((const __m128i *)&in[0]);
        in1 = _mm_loadl_epi64((const __m128i *)&in[4]);
        in2 = _mm_loadl_epi64((const __m128i *)&in[8]);
        in3 = _mm_loadl_epi64((const __m128i *)&in[12]);
      // a00 a10 a20 a30   x x x x
      // a01 a11 a21 a31   x x x x
      // a02 a12 a22 a32   x x x x
      // a03 a13 a23 a33   x x x x
    }

    // Vertical pass and subsequent transpose.
    {
        // First pass
        // in [0 + i] + in [8 + i]
        const __m128i a = _mm_add_epi16(in0, in2);
        // in [0 + i] - in [8 + i]
        const __m128i b = _mm_sub_epi16(in0, in2);
        // c = MUL(in1, K2) - MUL(in3, K1) = MUL(in1, k2) - MUL(in3, k1) + in1 -
        // in3
        const __m128i c1 = _mm_mulhi_epi16(in1, k2);
        const __m128i c2 = _mm_mulhi_epi16(in3, k1);
        const __m128i c3 = _mm_sub_epi16(in1, in3);
        const __m128i c4 = _mm_sub_epi16(c1, c2);
        const __m128i c = _mm_add_epi16(c3, c4);
        // d = MUL(in1, K1) + MUL(in3, K2) = MUL(in1, k1) + MUL(in3, k2) + in1 +
        // in3
        const __m128i d1 = _mm_mulhi_epi16(in1, k1);
        const __m128i d2 = _mm_mulhi_epi16(in3, k2);
        const __m128i d3 = _mm_add_epi16(in1, in3);
        const __m128i d4 = _mm_add_epi16(d1, d2);
        const __m128i d = _mm_add_epi16(d3, d4);

        // Second pass.
        const __m128i tmp0 = _mm_add_epi16(a, d);
        const __m128i tmp1 = _mm_add_epi16(b, c);
        const __m128i tmp2 = _mm_sub_epi16(b, c);
        const __m128i tmp3 = _mm_sub_epi16(a, d);

        // Transpose the two 4x4.
        x86_idct_1dx4_sse2_16bit(&tmp0, &tmp1, &tmp2, &tmp3, &T0, &T1, &T2, &T3);
    }

    // Horizontal pass and subsequent transpose.
    {
      // First pass, c and d calculations are longer because of the "trick"
      // multiplications.
      const __m128i four = _mm_set1_epi16(4);
      const __m128i dc = _mm_add_epi16(T0, four);
      const __m128i a = _mm_add_epi16(dc, T2);
      const __m128i b = _mm_sub_epi16(dc, T2);
      // c = MUL(T1, K2) - MUL(T3, K1) = MUL(T1, k2) - MUL(T3, k1) + T1 - T3
      const __m128i c1 = _mm_mulhi_epi16(T1, k2);
      const __m128i c2 = _mm_mulhi_epi16(T3, k1);
      const __m128i c3 = _mm_sub_epi16(T1, T3);
      const __m128i c4 = _mm_sub_epi16(c1, c2);
      const __m128i c = _mm_add_epi16(c3, c4);
      // d = MUL(T1, K1) + MUL(T3, K2) = MUL(T1, k1) + MUL(T3, k2) + T1 + T3
      const __m128i d1 = _mm_mulhi_epi16(T1, k1);
      const __m128i d2 = _mm_mulhi_epi16(T3, k2);
      const __m128i d3 = _mm_add_epi16(T1, T3);
      const __m128i d4 = _mm_add_epi16(d1, d2);
      const __m128i d = _mm_add_epi16(d3, d4);

      // Second pass.
      const __m128i tmp0 = _mm_add_epi16(a, d);
      const __m128i tmp1 = _mm_add_epi16(b, c);
      const __m128i tmp2 = _mm_sub_epi16(b, c);
      const __m128i tmp3 = _mm_sub_epi16(a, d);
      const __m128i rounding = _mm_set1_epi16(4); // 2^(3-1)
      const __m128i shifted0 = _mm_srai_epi16(_mm_add_epi16(tmp0, rounding), 3);
      const __m128i shifted1 = _mm_srai_epi16(_mm_add_epi16(tmp1, rounding), 3);
      const __m128i shifted2 = _mm_srai_epi16(_mm_add_epi16(tmp2, rounding), 3);
      const __m128i shifted3 = _mm_srai_epi16(_mm_add_epi16(tmp3, rounding), 3);

      // Transpose the two 4x4.
      x86_idct_1dx4_sse2_16bit(&shifted0, &shifted1, &shifted2, &shifted3, &T0,
                               &T1, &T2, &T3);
    }

    /* Store the residual.  This used to load the block back and add it to
     * the result, which is libwebp's TransformOne shape (prediction plus
     * residual) - but the accl_ops contract is a bare inverse transform, so
     * it added every block's own coefficients to its residual.  There is
     * nothing to add here either way. */
    {
      _mm_storeu_si64((__m128i *)in, T0);
      _mm_storeu_si64((__m128i *)(in + 4), T1);
      _mm_storeu_si64((__m128i *)(in + 8), T2);
      _mm_storeu_si64((__m128i *)(in + 12), T3);
    }
}

/* -------------------------------------------------------------------------
 * 8x8 inverse transform: the same 13 bit matrix (idct_transform_p13) that the
 * scalar idct_8x8_16 multiplies out, done with SSE2.
 *
 * The block is row major, in[u] is the u-th row of eight int16, and a pass
 * computes out[y][c] = sum_u M[y][u] * in[u][c].  With the eight columns in
 * the eight 16 bit lanes a pass is eight scalar-times-vector products, and
 * _mm_madd_epi16 - the only 16x16 -> 32 multiply SSE2 has - does two taps per
 * instruction once the taps are interleaved in pairs.  The two passes chain
 * without a transpose: the first pass emits eight rows of eight lanes, which
 * is exactly what the second pass reads.
 *
 * The scalar rounds each pass into an int16 (its colidcts array) and the next
 * pass reads that sign extended, so the same two shifts happen here, which
 * keeps a block bit for bit what the scalar path produced.
 */
#define IDCT8_COL_SHIFT 11
#define IDCT8_COL_ROUND (1 << (IDCT8_COL_SHIFT - 1))
#define IDCT8_ROW_SHIFT 18
#define IDCT8_ROW_ROUND (257 << (IDCT8_ROW_SHIFT - 1))

/* idct8_mat[y * 4 + p] is the matrix pair (M[y][2p], M[y][2p + 1]) broadcast
 * to every lane: for the pass whose lanes are the eight columns.  The row
 * pass wants four different outputs in the four lanes instead, so its pairs
 * are interleaved across them in idct8_rowmat. */
static __m128i idct8_mat[32];
static __m128i idct8_rowmat[8];
static int idct8_consts_ready;

static void
x86_idct8x8_sse2_consts(void)
{
    if (idct8_consts_ready) {
        return;
    }
    for (int y = 0; y < 8; y++) {
        for (int p = 0; p < 4; p++) {
            const uint32_t lo = (uint16_t)idct_transform_p13[8 * y + 2 * p];
            const uint32_t hi = (uint16_t)idct_transform_p13[8 * y + 2 * p + 1];

            idct8_mat[y * 4 + p] = _mm_set1_epi32((int)(lo | (hi << 16)));
        }
    }
    for (int g = 0; g < 2; g++) {
        for (int p = 0; p < 4; p++) {
            int16_t v[8];

            for (int k = 0; k < 4; k++) {
                v[2 * k] =
                    (int16_t)idct_transform_p13[8 * (4 * g + k) + 2 * p];
                v[2 * k + 1] =
                    (int16_t)idct_transform_p13[8 * (4 * g + k) + 2 * p + 1];
            }
            idct8_rowmat[g * 4 + p] = _mm_loadu_si128((const __m128i *)v);
        }
    }
    idct8_consts_ready = 1;
}

/* One pass with the eight columns in the lanes: the matrix is shared. */
static void
x86_idct8_pass_cols(const __m128i in[8], __m128i out[8][2])
{
    __m128i lo[4], hi[4];

    for (int p = 0; p < 4; p++) {
        lo[p] = _mm_unpacklo_epi16(in[2 * p], in[2 * p + 1]);
        hi[p] = _mm_unpackhi_epi16(in[2 * p], in[2 * p + 1]);
    }
    for (int y = 0; y < 8; y++) {
        __m128i a = _mm_setzero_si128();
        __m128i b = _mm_setzero_si128();

        for (int p = 0; p < 4; p++) {
            a = _mm_add_epi32(a, _mm_madd_epi16(lo[p], idct8_mat[y * 4 + p]));
            b = _mm_add_epi32(b, _mm_madd_epi16(hi[p], idct8_mat[y * 4 + p]));
        }
        out[y][0] = a;
        out[y][1] = b;
    }
}

/* One row: the eight taps sit in one register and the eight outputs are eight
 * dot products, so the taps are broadcast and the matrix is interleaved. */
static void
x86_idct8_pass_row(const __m128i tap, __m128i out[2])
{
    int16_t v[8];
    __m128i pair[4];

    _mm_storeu_si128((__m128i *)v, tap);
    for (int p = 0; p < 4; p++) {
        const uint32_t lo = (uint16_t)v[2 * p];
        const uint32_t hi = (uint16_t)v[2 * p + 1];

        pair[p] = _mm_set1_epi32((int)(lo | (hi << 16)));
    }
    for (int g = 0; g < 2; g++) {
        __m128i a = _mm_setzero_si128();

        for (int p = 0; p < 4; p++) {
            a = _mm_add_epi32(a,
                              _mm_madd_epi16(pair[p], idct8_rowmat[g * 4 + p]));
        }
        out[g] = a;
    }
}

static void x86_idct_8x8_sse2_16bit(int16_t *in, int bitdepth UNUSED) {
    const __m128i col_round = _mm_set1_epi32(IDCT8_COL_ROUND);
    const __m128i row_round = _mm_set1_epi32(IDCT8_ROW_ROUND);
    __m128i rows[8], acc[8][2];

    x86_idct8x8_sse2_consts();
    for (int y = 0; y < 8; y++) {
        rows[y] = _mm_loadu_si128((const __m128i *)(in + y * 8));
    }

    /* A block whose AC terms are all zero comes out flat, and the scalar
     * short circuits that case, so this has to as well or the "optimised"
     * path would be slower than the one it replaces on flat images.  The
     * formula is the scalar's, which is exact for this input. */
    {
        const __m128i ac_mask = _mm_set_epi16(-1, -1, -1, -1, -1, -1, -1, 0);
        __m128i any = _mm_and_si128(rows[0], ac_mask);

        for (int y = 1; y < 8; y++) {
            any = _mm_or_si128(any, rows[y]);
        }
        if (_mm_movemask_epi8(_mm_cmpeq_epi16(any, _mm_setzero_si128())) ==
            0xffff) {
            const int c = ((int)in[0] * 8192 + IDCT8_COL_ROUND) >>
                          IDCT8_COL_SHIFT;
            const int16_t px = (int16_t)clamp(
                (8192 * c + IDCT8_ROW_ROUND) >> IDCT8_ROW_SHIFT, 65535);

            for (int i = 0; i < 64; i++) {
                in[i] = px;
            }
            return;
        }
    }

    x86_idct8_pass_cols(rows, acc);
    for (int y = 0; y < 8; y++) {
        __m128i a = _mm_srai_epi32(_mm_add_epi32(acc[y][0], col_round),
                                   IDCT8_COL_SHIFT);
        __m128i b = _mm_srai_epi32(_mm_add_epi32(acc[y][1], col_round),
                                   IDCT8_COL_SHIFT);
        /* the scalar stores these two in an int16, and the row pass reads
         * them sign extended again */
        a = _mm_srai_epi32(_mm_slli_epi32(a, 16), 16);
        b = _mm_srai_epi32(_mm_slli_epi32(b, 16), 16);
        acc[y][0] = _mm_packs_epi32(a, b);
        rows[y] = acc[y][0];
    }

    for (int y = 0; y < 8; y++) {
        __m128i o[2];
        __m128i a, b;

        x86_idct8_pass_row(rows[y], o);
        a = _mm_srai_epi32(_mm_add_epi32(o[0], row_round), IDCT8_ROW_SHIFT);
        b = _mm_srai_epi32(_mm_add_epi32(o[1], row_round), IDCT8_ROW_SHIFT);
        /* clamp(x, 65535) with negatives floored to 0, as the scalar does;
         * anything past the 8 bit range gets clamped again on the way out */
        a = _mm_andnot_si128(_mm_srai_epi32(a, 31), a);
        b = _mm_andnot_si128(_mm_srai_epi32(b, 31), b);
        _mm_storeu_si128((__m128i *)(in + y * 8), _mm_packs_epi32(a, b));
    }
}

static struct accl_ops sse2_accl_16bit = {
    .idct_4x4 = x86_idct_4x4_sse2_16bit,
    .idct_8x8 = x86_idct_8x8_sse2_16bit,
    .type = SIMD_TYPE_SSE2,
};

void x86_sse2_init(void) { 
    accl_ops_register(&sse2_accl_16bit); 
}

#endif
