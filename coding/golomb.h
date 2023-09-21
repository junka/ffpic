#ifndef _GOLOMB_H_
#define _GOLOMB_H_

#ifdef __cplusplus
extern "C"{
#endif

#include "bitstream.h"

/* a kth exp golomb decoder for a bit stream reader */

uint32_t golomb_decode_unsigned_value(struct bits_vec *v, int kexp);

int32_t golomb_decode_signed_value(struct bits_vec *v, int kexp);


#define GOL_UE(d)   golomb_decode_unsigned_value(d, 0)
#define GOL_SE(d)   golomb_decode_signed_value(d, 0)


#define GUE(x)  uint32_t x
#define GSE(x)  int32_t x



#ifdef __cplusplus
}
#endif

#endif /*_GOLOMB_H_*/
