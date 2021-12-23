#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>

#include "boolentropy.h"
#include "bitstream.h"

const unsigned char vp8_norm[256] __attribute__((aligned(16))) =
{
    0, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define BITS (56)

bool_tree * 
bool_tree_init(struct bits_vec *v)
{
    if (!v)
        return NULL;
    bool_tree *br =  malloc(sizeof(bool_tree));

    br->value = 0;
    br->range = 255;
    br->count = -8;
    br->bits = v;

    /* Populate the buffer */
    uint64_t read = READ_BITS(v, BITS);
    br->value = read | (br->value << BITS);
    br->count += BITS;

    return br;
}

int bool_decode_alt(bool_tree *br, int prob)
{
    if (br->count < 0) {
        uint64_t read = READ_BITS(br->bits, BITS);
        br->value = read | (br->value << BITS);
        br->count += BITS;
    }
    uint32_t range = br->range - 1;
    int pos = br->count;
    uint32_t split = (range * prob) >> 8;
    uint32_t value = br->value >> pos;
    int bit;
    if (value > split) {
        range -= split + 1;
        br->value -= (split + 1) << pos;
        bit = 1;
    } else {
        range = split;
        bit = 0;
    }
    if (range < 0x7e) {
        int shift = vp8_norm[range];
        range <<= shift;
        value <<= shift;
        br->count -= shift;
    }
    br->range = range + 1;
    return bit;
}

int 
bool_decode(bool_tree *br, int prob)
{
     if (br->count < 0) {
        uint64_t read = READ_BITS(br->bits, BITS);
        br->value = read | (br->value << BITS);
        br->count += BITS;
    }
    uint32_t range = br->range - 1;
    int pos = br->count;
    uint32_t split = (range * prob) >> 8;
    uint32_t value = br->value >> pos;
    int bit = value >> split;
    if (bit) {
        range -= split;
        br->value -= (split + 1) << pos;
    } else {
        range = split + 1;
    }

    const int shift = 7 ^ (31 ^ __builtin_clz(range));
    range <<= shift;
    br->count -= shift;
    br->range = range;

    return bit;
}

