#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"
#include "golomb.h"
#include "vlog.h"

VLOG_REGISTER(golomb, DEBUG);

static void
golomb_load_bytes(struct golomb_dec *dec)
{
    uint64_t read = 0;
    if (EOF_BITS(dec->bits, 8)) {
        VERR(golomb, "why here!");
    }
    read = READ_BITS(dec->bits, 8);
    dec->value = read | (dec->value << 8);
    dec->count += 8;
}

struct golomb_dec *
golomb_init(uint8_t * start, int len, int k)
{
    struct golomb_dec *dec = (struct golomb_dec *)malloc(sizeof(struct golomb_dec));
    dec->kexp = k;
    dec->bits = bits_vec_alloc(start, len, BITS_LSB);
    dec->count = -8;
    dec->value = 0;
    return dec;
}

void
golomb_free(struct golomb_dec *dec)
{
    if (dec->bits)
        bits_vec_free(dec->bits);
    free(dec);
}

uint16_t 
golomb_decode_unsigned_value(struct golomb_dec *dec)
{
    uint16_t result = 0; 
    uint8_t zero_count = 0;
    uint8_t bit_count = 0;

    if (dec->count < 0) {
        golomb_load_bytes(dec);
    }
    int pos = dec->count;
    uint32_t value = dec->value >> pos;

    /* count leading zero bits */
    while (!(value & 0x1)) {
        zero_count ++;
        value >>= 1;
    }

    /* the bits num for info */
    bit_count = zero_count + dec->kexp + 1;

    for (uint8_t i = 0; i < bit_count; i++) {
        result <<= 1;
        result |= value & 0x1;
        value  >>= 1;
    }
    /* code num */
    if (dec->kexp > 0)
        result -= (2 << (dec->kexp - 1));
    else
        result -= 1;

    /* the bits used */
    dec->count -= (bit_count + zero_count);

    return result;
}

int16_t
golomb_decode_signed_value(struct golomb_dec *dec)
{
    int16_t result = 0;
    uint8_t zero_count = 0;
    uint8_t bit_count = 0;
    int16_t sign = 0;

    if (dec->count < 0) {
        golomb_load_bytes(dec);
    }
    int pos = dec->count;
    uint32_t value = dec->value >> pos;

    /* count leading zero bits */
    while (!(value & 0x1)) {
        zero_count ++;
        value >>= 1;
    }

    bit_count = zero_count + dec->kexp + 1;
    for (uint8_t i = 0; i < bit_count; i++) {
        result <<= 1;
        result |= value & 0x1;
        value >>= 1;
    }
    /* code num */
    if (dec->kexp > 0)
        result -= (2 << (dec->kexp - 1));
    else
        result -= 1;


    /* Remove the lowest bit as our sign bit. */
    sign = 1 - 2 * (result & 0x1);
    result = sign * ((result >> 1) & 0x7FFF);

    /* Defend against overflow on min int16. */
    bit_count += zero_count;
    if (bit_count > 0x20) {
        result |= 0x8000;
    }
    
    dec->count -= (bit_count + zero_count);

    return result;
}