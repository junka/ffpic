#ifndef _IDCT_H_
#define _IDCT_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>

/*
  void * type should uint8_t * or int16_t * which work for bitdepth
  for less than 8 or greater than 8, basicly we have two ops type
*/
struct dct_ops {
    int bitdepth;
    void (*idct_4x4)(void *in, int bitdepth);
    void (*idct_8x8)(void *in, int bitdepth);

    void (*dct_4x4)(const void *in, void *out, int bitdepth);
    void (*dct_8x8)(const void *in, void *out, int bitdepth);
};

const struct dct_ops *get_dct_ops(int component_bits);

void idct_4x4_hevc(const int16_t *in, int16_t *out, int bitdepth, bool epp);

void dct_8x8(const int16_t *block, int16_t *out, int stride);

#ifdef __cplusplus
}
#endif

#endif
