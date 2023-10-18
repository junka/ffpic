#ifndef _IDCT_H_
#define _IDCT_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

void idct_4x4(const int16_t *in, int16_t *out);

void idct_8x8(int16_t in[64], int16_t *out, int stride);

#ifdef __cplusplus
}
#endif

#endif