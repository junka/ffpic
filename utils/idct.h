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

    void (*fdct_4x4)(void *in, int bitdepth);
    void (*fdct_8x8)(void *in, int bitdepth);
};

const struct dct_ops *get_dct_ops(int component_bits);

void idct_4x4_hevc(const int16_t *in, int16_t *out, int bitdepth, bool epp);

void dct_float(float *data);

#ifdef __cplusplus
}
#endif

#endif
