#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>

#include "utils.h"
#include "boolentropy.h"
#include "bitstream.h"

const uint8_t 
vp8_norm[256] __attribute__((aligned(16))) =
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
bool_tree_init(uint8_t* start, int len)
{
    bool_tree *br = (bool_tree *)malloc(sizeof(bool_tree));
    br->value = 0;
    br->range = 255;
    br->count = 0;
    br->bits = bits_vec_alloc(start, len, BITS_LSB);;

    return br;
}

void
bool_tree_free(bool_tree *bt)
{
    if (bt->bits)
        bits_vec_free(bt->bits);
    free(bt);
}

void
bool_load_bytes(bool_tree *br)
{
    uint64_t read = 0;
    for (int i = 0; i < BITS >> 3; i ++) {
        read <<= 8;
        read |= READ_BITS(br->bits, 8);
    }
    br->value = read | (br->value << BITS);
    br->count += BITS;
}

int
bool_decode_alt(bool_tree *br, int prob)
{
    if (br->count <= 0) {
        bool_load_bytes(br);
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
     if (br->count <= 0) {
        bool_load_bytes(br);
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

    const int shift = 7 ^ log2floor(range);
    range <<= shift;
    br->count -= shift;
    br->range = range;

    return bit;
}

