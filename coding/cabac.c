#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "cabac.h"
#include "bitstream.h"
#include "vlog.h"

VLOG_REGISTER(cabac, DEBUG);

static void
bool_load_bytes(cabac_dec *br)
{
    uint64_t read = 0;
    if (EOF_BITS(br->bits, 8)) {
        VERR(cabac, "why here!");
    }
    read = READ_BITS(br->bits, 8);
    br->value = read | (br->value << 8);
    br->count += 8;
}


uint32_t
cabac_dec_bit(struct bits_vec *bits, int prob)
{
#if 0
    cabac_dec *dec;
    static uint8_t LPSTable[64][4];
    uint32_t lps = LPSTable[][( dec->range >> 6 ) - 4];
    dec->range -= lps;
    scaledRange = dec->range << 7;
    if (dec->value < scaledRange) {

    } else {
        
    }


    int s = *state;
    int RangeLPS= ff_h264_lps_range[2*(c->range&0xC0) + s];
    int bit, lps_mask;

    c->range -= RangeLPS;
    lps_mask= ((c->range<<(CABAC_BITS+1)) - c->low)>>31;

    c->low -= (c->range<<(CABAC_BITS+1)) & lps_mask;
    c->range += (RangeLPS - c->range) & lps_mask;

    s^=lps_mask;
    *state= (ff_h264_mlps_state+128)[s];
    bit= s&1;

    lps_mask= ff_h264_norm_shift[c->range];
    c->range<<= lps_mask;
    c->low  <<= lps_mask;
    if(!(c->low & CABAC_MASK))
        refill2(c);

#endif
    return 0;
}



