#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "golomb.h"
#include "vlog.h"

VLOG_REGISTER(golomb, DEBUG)

//h264 and h265 use 0th order exp
//kth order exp-golomb see h266 9.2
uint32_t 
golomb_decode_unsigned_value(struct bits_vec *v, int kexp)
{
    uint32_t code = 0; 
    int zero_count = -1;
    uint8_t bit_count = 0;
    uint8_t bit = 0;

    /* count leading zero bits */
    while (bit == 0 && zero_count < 32) {
        bit = READ_BIT(v);
        zero_count ++;
    }

    /* the bits num for info */
    bit_count = zero_count + kexp;

    /* An exp-golomb code needs one leading 1 plus bit_count suffix bits.
     * Shifting a 32-bit value by bit_count >= 32 is undefined, and such a
     * code cannot be represented in the uint32_t result either, so treat it
     * as a malformed stream and saturate.  Valid codes take the same path
     * as before (bit_count <= 31). */
    if (kexp < 0 || kexp >= 32 || bit_count >= 32) {
        VERR(golomb, "exp-golomb code width exceeds 32 bits");
        return UINT32_MAX;
    }

    code = (uint32_t)(((uint64_t)1 << bit_count) - ((uint64_t)1 << kexp)
        + (uint32_t)READ_BITS(v, bit_count));

    return code;
}

int32_t
golomb_decode_signed_value(struct bits_vec *v, int kexp)
{
    uint32_t code = golomb_decode_unsigned_value(v, kexp);
    int code_signed  = (int)((code + 1) >> 1);

    if ((code & 0x1) == 0) {
        code_signed = -code_signed;
    }

    return code_signed;
}
