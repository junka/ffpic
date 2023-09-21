#ifndef _CABAC_H_
#define _CABAC_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>

#define STATE_BITS (6)
#define STATE_NUM  ((1 << STATE_BITS) * 2) // *2 for MPS = [0|1]

typedef struct cabac_dec {
    uint64_t value;
    uint32_t range;     //[127, 254]
    int count;
    uint8_t state;  //state need 6 bits, put mps or lps at the least bit
    struct bits_vec *bits;
    uint8_t* LPSTable[64];
    uint8_t* RenormTable;
    int bypass;
} cabac_dec;

cabac_dec * cabac_dec_init(struct bits_vec*);

int cabac_dec_bin(cabac_dec *br);

cabac_dec *cabac_lookup(struct bits_vec *v);

#define CABAC(v) cabac_dec_bin(cabac_lookup(v))

#ifdef __cplusplus
}
#endif

#endif /*_CABAC_H_*/
