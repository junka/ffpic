#include <stdint.h>
#include <string.h>

#include "predict.h"

static const uint8_t clip1[255 + 511 + 1] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c,
    0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
    0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c,
    0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80,
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c,
    0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
    0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
    0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0,
    0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

#define BPS (32)
#define YUV_SIZE (BPS * 17 + BPS * 9)
#define Y_OFF (BPS * 1 + 8)
#define U_OFF (Y_OFF + BPS * 16 + BPS)
#define V_OFF (U_OFF + 16)

//------------------------------------------------------------------------------
// Transforms (Paragraph 14.4)
static inline uint8_t clip_8b(int v) {
  return (!(v & ~0xff)) ? v : (v < 0) ? 0 : 255;
}

#define STORE(x, y, v)                                                         \
  dst[(x) + (y)*BPS] = clip_8b(dst[(x) + (y)*BPS] + ((v) >> 3))

#define STORE2(y, dc, d, c)                                                    \
  do {                                                                         \
    const int DC = (dc);                                                       \
    STORE(0, y, DC + (d));                                                     \
    STORE(1, y, DC + (c));                                                     \
    STORE(2, y, DC - (c));                                                     \
    STORE(3, y, DC - (d));                                                     \
  } while (0)

#define MUL1(a) ((((a)*20091) >> 16) + (a))
#define MUL2(a) (((a)*35468) >> 16)

static inline void TrueMotion(uint8_t *dst, int size) {
  const uint8_t *top = dst - BPS;
  const uint8_t *const clip0 = clip1 + 255 - top[-1];

  for (int y = 0; y < size; ++y) {
    const uint8_t *const clip = clip0 + dst[-1];
    int x;
    for (x = 0; x < size; ++x) {
      dst[x] = clip[top[x]];
    }
    dst += BPS;
  }
}

static void TransformOne_C(const int16_t *in, uint8_t *dst) {
  int C[4 * 4], *tmp;
  int i;
  tmp = C;
  for (i = 0; i < 4; ++i) {                   // vertical pass
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

static const uint16_t kScan[16] = {
    0 + 0 * BPS,  4 + 0 * BPS,  8 + 0 * BPS,  12 + 0 * BPS,
    0 + 4 * BPS,  4 + 4 * BPS,  8 + 4 * BPS,  12 + 4 * BPS,
    0 + 8 * BPS,  4 + 8 * BPS,  8 + 8 * BPS,  12 + 8 * BPS,
    0 + 12 * BPS, 4 + 12 * BPS, 8 + 12 * BPS, 12 + 12 * BPS};

static int CheckMode(int mb_x, int mb_y, int mode) {
  if (mode == B_DC_PRED) {
    if (mb_x == 0) {
      return (mb_y == 0) ? B_DC_PRED_NOTOPLEFT : B_DC_PRED_NOLEFT;
    } else {
      return (mb_y == 0) ? B_DC_PRED_NOTOP : B_DC_PRED;
    }
  }
  return mode;
}

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
  if (bits & 0xff) {           // any non-zero coeff at all?
    if (bits & 0xaa) {         // any non-zero AC coefficient?
      TransformUV_C(src, dst); // note we don't use the AC3 variant for U/V
    } else {
      TransformDCUV_C(src, dst);
    }
  }
}

#define DST(x, y) dst[(x) + (y)*BPS]
#define AVG3(a, b, c) ((uint8_t)(((a) + 2 * (b) + (c) + 2) >> 2))
#define AVG2(a, b) (((a) + (b) + 1) >> 1)

static void VE4_C(uint8_t *dst) { // vertical
  const uint8_t *top = dst - BPS;
  const uint8_t vals[4] = {
      AVG3(top[-1], top[0], top[1]), AVG3(top[0], top[1], top[2]),
      AVG3(top[1], top[2], top[3]), AVG3(top[2], top[3], top[4])};
  int i;
  for (i = 0; i < 4; ++i) {
    memcpy(dst + i * BPS, vals, sizeof(vals));
  }
}

static void HE4_C(uint8_t *dst) { // horizontal
  const int A = dst[-1 - BPS];
  const int B = dst[-1];
  const int C = dst[-1 + BPS];
  const int D = dst[-1 + 2 * BPS];
  const int E = dst[-1 + 3 * BPS];
  uint32_t sr = (uint32_t)0x01010101U * AVG3(A, B, C);
  memcpy(dst + 0 * BPS, &sr, 4);
  sr = (uint32_t)0x01010101U * AVG3(B, C, D);
  memcpy(dst + 1 * BPS, &sr, 4);
  sr = (uint32_t)0x01010101U * AVG3(C, D, E);
  memcpy(dst + 2 * BPS, &sr, 4);
  sr = (uint32_t)0x01010101U * AVG3(D, E, E);
  memcpy(dst + 3 * BPS, &sr, 4);
}

static void DC4_C(uint8_t *dst) { // DC
  uint32_t dc = 4;
  int i;
  for (i = 0; i < 4; ++i)
    dc += dst[i - BPS] + dst[-1 + i * BPS];
  dc >>= 3;
  for (i = 0; i < 4; ++i)
    memset(dst + i * BPS, dc, 4);
}

static void RD4_C(uint8_t *dst) { // Down-right
  const int I = dst[-1 + 0 * BPS];
  const int J = dst[-1 + 1 * BPS];
  const int K = dst[-1 + 2 * BPS];
  const int L = dst[-1 + 3 * BPS];
  const int X = dst[-1 - BPS];
  const int A = dst[0 - BPS];
  const int B = dst[1 - BPS];
  const int C = dst[2 - BPS];
  const int D = dst[3 - BPS];
  DST(0, 3) = AVG3(J, K, L);
  DST(1, 3) = DST(0, 2) = AVG3(I, J, K);
  DST(2, 3) = DST(1, 2) = DST(0, 1) = AVG3(X, I, J);
  DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) = AVG3(A, X, I);
  DST(3, 2) = DST(2, 1) = DST(1, 0) = AVG3(B, A, X);
  DST(3, 1) = DST(2, 0) = AVG3(C, B, A);
  DST(3, 0) = AVG3(D, C, B);
}

static void LD4_C(uint8_t *dst) { // Down-Left
  const int A = dst[0 - BPS];
  const int B = dst[1 - BPS];
  const int C = dst[2 - BPS];
  const int D = dst[3 - BPS];
  const int E = dst[4 - BPS];
  const int F = dst[5 - BPS];
  const int G = dst[6 - BPS];
  const int H = dst[7 - BPS];
  DST(0, 0) = AVG3(A, B, C);
  DST(1, 0) = DST(0, 1) = AVG3(B, C, D);
  DST(2, 0) = DST(1, 1) = DST(0, 2) = AVG3(C, D, E);
  DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3(D, E, F);
  DST(3, 1) = DST(2, 2) = DST(1, 3) = AVG3(E, F, G);
  DST(3, 2) = DST(2, 3) = AVG3(F, G, H);
  DST(3, 3) = AVG3(G, H, H);
}

static void VR4_C(uint8_t *dst) { // Vertical-Right
  const int I = dst[-1 + 0 * BPS];
  const int J = dst[-1 + 1 * BPS];
  const int K = dst[-1 + 2 * BPS];
  const int X = dst[-1 - BPS];
  const int A = dst[0 - BPS];
  const int B = dst[1 - BPS];
  const int C = dst[2 - BPS];
  const int D = dst[3 - BPS];
  DST(0, 0) = DST(1, 2) = AVG2(X, A);
  DST(1, 0) = DST(2, 2) = AVG2(A, B);
  DST(2, 0) = DST(3, 2) = AVG2(B, C);
  DST(3, 0) = AVG2(C, D);

  DST(0, 3) = AVG3(K, J, I);
  DST(0, 2) = AVG3(J, I, X);
  DST(0, 1) = DST(1, 3) = AVG3(I, X, A);
  DST(1, 1) = DST(2, 3) = AVG3(X, A, B);
  DST(2, 1) = DST(3, 3) = AVG3(A, B, C);
  DST(3, 1) = AVG3(B, C, D);
}

static void VL4_C(uint8_t *dst) { // Vertical-Left
  const int A = dst[0 - BPS];
  const int B = dst[1 - BPS];
  const int C = dst[2 - BPS];
  const int D = dst[3 - BPS];
  const int E = dst[4 - BPS];
  const int F = dst[5 - BPS];
  const int G = dst[6 - BPS];
  const int H = dst[7 - BPS];
  DST(0, 0) = AVG2(A, B);
  DST(1, 0) = DST(0, 2) = AVG2(B, C);
  DST(2, 0) = DST(1, 2) = AVG2(C, D);
  DST(3, 0) = DST(2, 2) = AVG2(D, E);

  DST(0, 1) = AVG3(A, B, C);
  DST(1, 1) = DST(0, 3) = AVG3(B, C, D);
  DST(2, 1) = DST(1, 3) = AVG3(C, D, E);
  DST(3, 1) = DST(2, 3) = AVG3(D, E, F);
  DST(3, 2) = AVG3(E, F, G);
  DST(3, 3) = AVG3(F, G, H);
}

static void HU4_C(uint8_t *dst) { // Horizontal-Up
  const int I = dst[-1 + 0 * BPS];
  const int J = dst[-1 + 1 * BPS];
  const int K = dst[-1 + 2 * BPS];
  const int L = dst[-1 + 3 * BPS];
  DST(0, 0) = AVG2(I, J);
  DST(2, 0) = DST(0, 1) = AVG2(J, K);
  DST(2, 1) = DST(0, 2) = AVG2(K, L);
  DST(1, 0) = AVG3(I, J, K);
  DST(3, 0) = DST(1, 1) = AVG3(J, K, L);
  DST(3, 1) = DST(1, 2) = AVG3(K, L, L);
  DST(3, 2) = DST(2, 2) = DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = L;
}

static void HD4_C(uint8_t *dst) { // Horizontal-Down
  const int I = dst[-1 + 0 * BPS];
  const int J = dst[-1 + 1 * BPS];
  const int K = dst[-1 + 2 * BPS];
  const int L = dst[-1 + 3 * BPS];
  const int X = dst[-1 - BPS];
  const int A = dst[0 - BPS];
  const int B = dst[1 - BPS];
  const int C = dst[2 - BPS];

  DST(0, 0) = DST(2, 1) = AVG2(I, X);
  DST(0, 1) = DST(2, 2) = AVG2(J, I);
  DST(0, 2) = DST(2, 3) = AVG2(K, J);
  DST(0, 3) = AVG2(L, K);

  DST(3, 0) = AVG3(A, B, C);
  DST(2, 0) = AVG3(X, A, B);
  DST(1, 0) = DST(3, 1) = AVG3(I, X, A);
  DST(1, 1) = DST(3, 2) = AVG3(J, I, X);
  DST(1, 2) = DST(3, 3) = AVG3(K, J, I);
  DST(1, 3) = AVG3(L, K, J);
}

static void TM4_C(uint8_t *dst) { TrueMotion(dst, 4); }
static void TM8uv_C(uint8_t *dst) { TrueMotion(dst, 8); }
static void TM16_C(uint8_t *dst) { TrueMotion(dst, 16); }

// helper for chroma-DC predictions
static inline void Put8x8uv(uint8_t value, uint8_t *dst) {
  for (int j = 0; j < 8; ++j) {
    memset(dst + j * BPS, value, 8);
  }
}

static void DC8uv_C(uint8_t *dst) { // DC
  int dc0 = 8;
  for (int i = 0; i < 8; ++i) {
    dc0 += dst[i - BPS] + dst[-1 + i * BPS];
  }
  Put8x8uv(dc0 >> 4, dst);
}

static void VE8uv_C(uint8_t *dst) { // vertical
  int j;
  for (j = 0; j < 8; ++j) {
    memcpy(dst + j * BPS, dst - BPS, 8);
  }
}

static void HE8uv_C(uint8_t *dst) { // horizontal
  int j;
  for (j = 0; j < 8; ++j) {
    memset(dst, dst[-1], 8);
    dst += BPS;
  }
}

static void DC8uvNoTop_C(uint8_t *dst) { // DC with no top samples
  int dc0 = 4;
  int i;
  for (i = 0; i < 8; ++i) {
    dc0 += dst[-1 + i * BPS];
  }
  Put8x8uv(dc0 >> 3, dst);
}

static void DC8uvNoLeft_C(uint8_t *dst) { // DC with no left samples
  int dc0 = 4;
  for (int i = 0; i < 8; ++i) {
    dc0 += dst[i - BPS];
  }
  Put8x8uv(dc0 >> 3, dst);
}

static void DC8uvNoTopLeft_C(uint8_t *dst) { // DC with nothing
  Put8x8uv(0x80, dst);
}

//------------------------------------------------------------------------------
// 16x16
static void VE16_C(uint8_t *dst) { // vertical
  for (int j = 0; j < 16; ++j) {
    memcpy(dst + j * BPS, dst - BPS, 16);
  }
}

static void HE16_C(uint8_t *dst) { // horizontal
  for (int j = 16; j > 0; --j) {
    memset(dst, dst[-1], 16);
    dst += BPS;
  }
}

static inline void Put16(int v, uint8_t *dst) {
  for (int j = 0; j < 16; ++j) {
    memset(dst + j * BPS, v, 16);
  }
}

static void DC16_C(uint8_t *dst) { // DC
  int DC = 16;
  for (int j = 0; j < 16; ++j) {
    DC += dst[-1 + j * BPS] + dst[j - BPS];
  }
  Put16(DC >> 5, dst);
}

static void DC16NoTop_C(uint8_t *dst) { // DC with top samples not available
  int DC = 8;
  for (int j = 0; j < 16; ++j) {
    DC += dst[-1 + j * BPS];
  }
  Put16(DC >> 4, dst);
}

static void DC16NoLeft_C(uint8_t *dst) { // DC with left samples not available
  int DC = 8;
  for (int i = 0; i < 16; ++i) {
    DC += dst[i - BPS];
  }
  Put16(DC >> 4, dst);
}

static void DC16NoTopLeft_C(uint8_t *dst) { // DC with no top and left samples
  Put16(0x80, dst);
}

typedef void (*PredFunc)(uint8_t *dst);

PredFunc PredLuma4[NUM_BMODES] = {
    DC4_C,
    TM4_C,
    VE4_C,
    HE4_C,
    RD4_C,
    VR4_C,
    LD4_C,
    VL4_C,
    HD4_C,
    HU4_C
};

PredFunc PredChroma8[NUM_B_DC_MODES] = {
    DC8uv_C,
    TM8uv_C,
    VE8uv_C,
    HE8uv_C,
    DC8uvNoTop_C,
    DC8uvNoLeft_C,
    DC8uvNoTopLeft_C,
};

PredFunc PredLuma16[NUM_B_DC_MODES] = {
    DC16_C,
    TM16_C,
    VE16_C,
    HE16_C,
    DC16NoTop_C,
    DC16NoLeft_C,
    DC16NoTopLeft_C
};
