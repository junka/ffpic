#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"
#include "golomb.h"
#include "vlog.h"

VLOG_REGISTER(golomb, DEBUG);

//h264 and h265 use 0th order exp
//kth order exp-golomb see h266 9.2

struct golomb_dec *
golomb_init(uint8_t * start, int len, int k)
{
    struct golomb_dec *dec = (struct golomb_dec *)malloc(sizeof(struct golomb_dec));
    dec->kexp = k;
    dec->bits = bits_vec_alloc(start, len, BITS_LSB);
    return dec;
}

void
golomb_free(struct golomb_dec *dec)
{
    if (dec->bits)
        bits_vec_free(dec->bits);
    free(dec);
}

uint32_t 
golomb_decode_unsigned_value(struct golomb_dec *dec)
{
    uint32_t code = 0; 
    uint8_t zero_count = -1;
    uint8_t bit_count = 0;
    uint8_t bit;

    /* count leading zero bits */
    while (bit == 0) {
        bit = READ_BIT(dec->bits);
        zero_count ++;
    }

    /* the bits num for info */
    bit_count = zero_count + dec->kexp;

    code = (1 << bit_count) - (1 << dec->kexp)
        + READ_BITS(dec->bits, bit_count);

    return code;
}

int32_t
golomb_decode_signed_value(struct golomb_dec *dec)
{
    uint32_t code = golomb_decode_unsigned_value(dec);
    int code_signed  = (int)((code + 1) >> 1);

    if ((code & 0x1) == 0) {
        code_signed = -code_signed;
    }

    return code_signed;
}