#include <stdint.h>
#include <string.h>

#include "accl.h"
#include "x86.h"

#ifdef __SSE2__

// Transpose two 4x4 16b matrices
static void
x86_idct_1dx4_sse2(const __m128i *const in0,
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

static void x86_idct_4x4_sse2(int16_t *in, int16_t *dst, int bitdepth) {
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
        x86_idct_1dx4_sse2(&tmp0, &tmp1, &tmp2, &tmp3, &T0, &T1, &T2, &T3);
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
      const __m128i shifted0 = _mm_srai_epi16(tmp0, 3);
      const __m128i shifted1 = _mm_srai_epi16(tmp1, 3);
      const __m128i shifted2 = _mm_srai_epi16(tmp2, 3);
      const __m128i shifted3 = _mm_srai_epi16(tmp3, 3);

      // Transpose the two 4x4.
      x86_idct_1dx4_sse2(&shifted0, &shifted1, &shifted2, &shifted3, &T0, &T1,
                         &T2, &T3);
    }

    // Add inverse transform to 'dst' and store.
    {
      const __m128i zero = _mm_setzero_si128();
      // Load the reference(s).
      __m128i dst0, dst1, dst2, dst3;
      __m128i dst_w0, dst_w1;

      // Load 16 * 8 = 128 which is just two lines
      dst_w0 = _mm_loadu_si128((__m128i *)dst);
      dst_w1 = _mm_loadu_si128((__m128i *)(dst + 8));

      // Convert to 16b.
      dst0 = _mm_unpacklo_epi16(dst_w0, zero);
      dst1 = _mm_unpackhi_epi16(dst_w0, zero);
      dst2 = _mm_unpacklo_epi16(dst_w1, zero);
      dst3 = _mm_unpackhi_epi16(dst_w1, zero);
      // Add the inverse transform(s).
      dst0 = _mm_add_epi16(dst0, T0);
      dst1 = _mm_add_epi16(dst1, T1);
      dst2 = _mm_add_epi16(dst2, T2);
      dst3 = _mm_add_epi16(dst3, T3);

      // Store four bytes/pixels per line.
      _mm_storeu_si64((__m128i* )dst, dst0);
      _mm_storel_epi64((__m128i *)(dst + 4), dst1);
      _mm_storeu_si64((__m128i *)(dst + 8), dst2);
      _mm_storel_epi64((__m128i *)(dst + 12), dst3);
    }
}

static struct accl_ops sse2_accl = {
    .idct_4x4 = x86_idct_4x4_sse2,
    .type = SIMD_TYPE_SSE2,
};

void x86_sse2_init(void) { 
    accl_ops_register(&sse2_accl); 
}

#endif