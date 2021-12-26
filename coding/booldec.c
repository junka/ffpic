#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>

#include "utils.h"
#include "booldec.h"
#include "bitstream.h"
#include "vlog.h"

VLOG_REGISTER(booldec, DEBUG);

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

static void
bool_load_bytes(bool_dec *br)
{
    uint64_t read = 0;
    if (EOF_BITS(br->bits, 8)) {
        VERR(booldec, "why here!");
    }
    read = READ_BITS(br->bits, 8);
    br->value = read | (br->value << 8);
    br->count += 8;
}

bool_dec *
bool_dec_init(uint8_t* start, int len)
{
    bool_dec *br = (bool_dec *)malloc(sizeof(bool_dec));
    br->value = 0;
    br->range = 255;
    br->count = -8;
    br->bits = bits_vec_alloc(start, len, BITS_LSB);
    bool_load_bytes(br);
    return br;
}

void
bool_dec_free(bool_dec *bt)
{
    if (bt->bits)
        bits_vec_free(bt->bits);
    free(bt);
}

uint32_t
bool_decode_alt(bool_dec *br, int prob)
{
    if (br->count < 0) {
        bool_load_bytes(br);
    }
    uint32_t range = br->range - 1;
    int pos = br->count;
    uint32_t split = (range * prob) >> 8;
    uint32_t value = br->value >> pos;
    int bit;
    if (value > split) {
        range -= split + 1;
        br->value -= (uint64_t)(split + 1) << pos;
        bit = 1;
    } else {
        range = split;
        bit = 0;
    }
    if (range < 0x7E) {
        int shift = vp8_norm[range];
        range <<= shift;
        value <<= shift;
        br->count -= shift;
    }
    br->range = range + 1;
    return bit;
}

uint32_t
bool_dec_bit(bool_dec *br, int prob)
{
    if (br->count <= 0) {
        bool_load_bytes(br);
    }

    uint32_t range = br->range - 1;
    int pos = br->count;
    uint32_t split = (range * prob) >> 8;
    uint32_t value = br->value >> pos;
    int bit = (value > split);
    if (bit) {
        range -= split;
        br->value -= (uint64_t)(split + 1) << pos;
    } else {
        range = split + 1;
    }

    const int shift = 7 ^ log2floor(range);
    range <<= shift;
    br->count -= shift;
    br->range = range;
    return bit;
}

// simplified version of dec_bit for prob=0x80 (note shift is always 1 here)
uint32_t
bool_dec_bit_half(bool_dec *br, int v)
{
    if (br->count < 0) {
        bool_load_bytes(br);
    }
    uint32_t range = br->range - 1;
    int pos = br->count;
    uint32_t split = range >> 1;
    uint32_t value = br->value >> pos;
    int32_t mask = (int32_t)(split - value) >> 31; // -1 or 0
    range += mask;
    range |= 1;
    br->count -= 1;
    br->range = range + 1;
    br->value -= (uint64_t)((split + 1) & mask) << pos;
    return (v ^ mask) - mask;
}

uint32_t
bool_dec_bits(bool_dec *br, int nums)
{
    uint32_t v = 0;
    while (nums-- > 0) {
        v |= BOOL_BIT(br) << nums;
    }
    return v;
}
