#ifndef _IDCT_H_
#define _IDCT_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>

void idct_4x4(const int16_t *in, int16_t *out);

void idct_4x4_hevc(const int16_t *in, int16_t *out, int bitdepth, bool epp);

void idct_8x8(int16_t in[64], int16_t *out, int stride);

void dct_8x8(int16_t *block, int16_t *out, int stride);

#ifdef __cplusplus
}
#endif

#endif
