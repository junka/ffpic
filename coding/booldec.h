#ifndef _BOOL_DEC_H_
#define _BOOL_DEC_H_

#ifdef __cplusplus
extern "C"{
#endif
#include <stdint.h>

#define BOOL_VALUE_SIZE ((int)sizeof(size_t) * CHAR_BIT)

/*This is meant to be a large, positive constant that can still be efficiently
   loaded as an immediate (on platforms like ARM, for example).
  Even relatively modest values like 100 would work fine.*/
#define VP8_LOTS_OF_BITS (0x40000000)

typedef struct bool_dec {
    uint64_t value;
    uint32_t range;     //[127, 254]
    int count;
    struct bits_vec *bits;
} bool_dec;

bool_dec *bool_dec_init(uint8_t* start, int len);

void bool_dec_free(bool_dec *bt);

uint32_t bool_dec_bit(bool_dec *br, int probability);

uint32_t bool_dec_bits(bool_dec *br, int nums);

/* similar to bool_dec_bits but cosume one more bit as sign */
uint32_t bool_dec_signed_bits(bool_dec *br, int nums);
int bool_dec_tree(struct bool_dec *br, const int8_t *t, const uint8_t *p,
                  int start);

uint32_t bool_dec_bit_alt(bool_dec *br, int probability);

uint32_t bool_dec_bit_half(bool_dec *br, int v);

#define BOOL_BIT(br)  bool_dec_bit(br, 0x80)
#define BOOL_BITS(br, n) bool_dec_bits(br, n)
#define BOOL_SBITS(br, n) bool_dec_signed_bits(br, n)

#define BOOL_DECODE(br, p) bool_dec_bit(br, p)
#define BOOL_DECODE_ALT(br, p) bool_dec_bit_alt(br, p)
#define BOOL_SIGNED(br, v)  bool_dec_bit_half(br, v)

#define BOOL_TREE(br, t, p) bool_dec_tree(br, t, p, 0)

#ifdef __cplusplus
}
#endif

#endif /*_BOOL_DEC_H_*/