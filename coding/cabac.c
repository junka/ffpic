#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "cabac.h"
#include "bitstream.h"
#include "vlog.h"

VLOG_REGISTER(cabac, DEBUG)


//see Table 9-53 state transition
static uint8_t NextStateMPS[STATE_NUM] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
    98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113,
    114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 124, 125, 126, 127
};

static uint8_t NextStateLPS[STATE_NUM] = {
    1, 0, 0, 1, 2, 3, 4, 5, 4, 5, 8, 9, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 19, 18, 19, 22, 23, 22, 23, 24, 25,
    26, 27, 26, 27, 30, 31, 30, 31, 32, 33, 32, 33, 36, 37, 36, 37,
    38, 39, 38, 39, 42, 43, 42, 43, 44, 45, 44, 45, 46, 47, 48, 49,
    48, 49, 50, 51, 52, 53, 52, 53, 54, 55, 54, 55, 56, 57, 58, 59,
    58, 59, 60, 61, 60, 61, 60, 61, 62, 63, 64, 65, 64, 65, 66, 67,
    66, 67, 66, 67, 68, 69, 68, 69, 70, 71, 70, 71, 70, 71, 72, 73,
    72, 73, 72, 73, 74, 75, 74, 75, 74, 75, 76, 77, 76, 77, 126, 127
};

//see Table 9-52 Lps table on (state, range)
uint8_t LPSTable[1 << STATE_BITS][4] = {
  {128, 176, 208, 240},
  {128, 167, 197, 227},
  {128, 158, 187, 216},
  {123, 150, 178, 205},
  {116, 142, 169, 195},
  {111, 135, 160, 185},
  {105, 128, 152, 175},
  {100, 122, 144, 166},
  { 95, 116, 137, 158},
  { 90, 110, 130, 150},
  { 85, 104, 123, 142},
  { 81,  99, 117, 135},
  { 77,  94, 111, 128},
  { 73,  89, 105, 122},
  { 69,  85, 100, 116},
  { 66,  80,  95, 110},
  { 62,  76,  90, 104},
  { 59,  72,  86,  99},
  { 56,  69,  81,  94},
  { 53,  65,  77,  89},
  { 51,  62,  73,  85},
  { 48,  59,  69,  80},
  { 46,  56,  66,  76},
  { 43,  53,  63,  72},
  { 41,  50,  59,  69},
  { 39,  48,  56,  65},
  { 37,  45,  54,  62},
  { 35,  43,  51,  59},
  { 33,  41,  48,  56},
  { 32,  39,  46,  53},
  { 30,  37,  43,  50},
  { 29,  35,  41,  48},
  { 27,  33,  39,  45},
  { 26,  31,  37,  43},
  { 24,  30,  35,  41},
  { 23,  28,  33,  39},
  { 22,  27,  32,  37},
  { 21,  26,  30,  35},
  { 20,  24,  29,  33},
  { 19,  23,  27,  31},
  { 18,  22,  26,  30},
  { 17,  21,  25,  28},
  { 16,  20,  23,  27},
  { 15,  19,  22,  25},
  { 14,  18,  21,  24},
  { 14,  17,  20,  23},
  { 13,  16,  19,  22},
  { 12,  15,  18,  21},
  { 12,  14,  17,  20},
  { 11,  14,  16,  19},
  { 11,  13,  15,  18},
  { 10,  12,  15,  17},
  { 10,  12,  14,  16},
  { 9,  11,  13,  15},
  { 9,  11,  12,  14},
  { 8,  10,  12,  14},
  { 8,   9,  11,  13},
  { 7,   9,  11,  12},
  { 7,   9,  10,  12},
  { 7,   8,  10,  11},
  { 6,   8,   9,  11},
  { 6,   7,   9,  10},
  { 6,   7,   8,   9},
  { 2,   2,   2,   2},
};

static uint8_t RenormTable[32] = {
    6,  5,  4,  4,
    3,  3,  3,  3,
    2,  2,  2,  2,
    2,  2,  2,  2,
    1,  1,  1,  1,
    1,  1,  1,  1,
    1,  1,  1,  1,
    1,  1,  1,  1
};

static int dec_num = 0;
static cabac_dec **dec_list = NULL;

cabac_dec *
cabac_lookup(struct bits_vec *v)
{
    // cabac_dec *d;
    for (int i = 0; i < dec_num; i ++) {
        if (dec_list[i]->bits == v) {
            return dec_list[i];
        }
    }
    return NULL;
}

cabac_dec *
cabac_dec_init(struct bits_vec *v)
{
    cabac_dec *dec = malloc(sizeof(*dec));
    dec->bits = v;
    dec->count = -8;
    dec->value = READ_BITS(v, 8) << 8;
    dec->value |= READ_BITS(v, 8);
    dec->range = 510;
    dec->bypass = 0;

    for (int i = 0; i < 64; i ++) {
        dec->LPSTable[i] = LPSTable[i];
    }
    dec->RenormTable = RenormTable;
    if (!dec_list) {
        dec_list = malloc(sizeof(*dec_list));
    } else {
        dec_list = realloc(dec_list, sizeof(*dec_list)*(dec_num+1));
    }
    dec_list[dec_num++] = dec;
    return dec;
}

//see Figure 9-7
static void
renormD(cabac_dec *dec, uint32_t scaledRange)
{
    if (scaledRange < (256 << 7)) {
        dec->range = scaledRange >> 6;
        dec->value += dec->value;
        if (++dec->count == 0) {
            dec->count = -8;
            dec->value += READ_BITS(dec->bits, 8);
        }
    }
}

void
cabac_dec_free(cabac_dec *dec)
{
    int i = 0;
    for (i = 0; i < dec_num; i ++) {
        if (dec_list[i] == dec) {
            break;
        }
    }
    if (i < dec_num) {
        for (; i < dec_num - 1; i ++) {
            dec_list[i] = dec_list[i+1];
        }
        dec_list[dec_num - 1] = NULL;
        dec_num --;
        free(dec);
    }
}

static int
cabac_dec_bypass(cabac_dec *dec)
{
    int binVal = 0;
    /*Figure 9-8 */
    dec->value += dec->value;
    if (++dec->count >=0) {
        dec->count = -8;
        dec->value += READ_BITS(dec->bits, 8);
    }
    uint32_t scaledRange = dec->range << 7;
    if (dec->value >= scaledRange) {
        binVal = 1;
        dec->value -= scaledRange;
    }

    return binVal;
}

static int UNUSED
cabac_dec_terminate(cabac_dec *dec)
{
    /*Figure 9-9 */
    int binVal = 0;
    dec->range -= 2;
    uint32_t scaledRange = dec->range << 7;
    if (dec->value >= scaledRange) {
        binVal = 1;
    } else {
        binVal = 0;
        renormD(dec, scaledRange);
    }
    return binVal;
}

//see 9.3.4.3
static int
cabac_dec_decision(cabac_dec *dec)
{
    int binVal;
    uint8_t state = dec->state;
    uint32_t rangelps = dec->LPSTable[state >> 1][(dec->range >> 6) - 4];
    dec->range -= rangelps;
    uint32_t scaledRange = dec->range << 7;
    if (dec->value < scaledRange) {
        //MPS (Most Probable Symbol)
        binVal = dec->state & 0x1;
        dec->state = NextStateMPS[state];
        renormD(dec, scaledRange);
    } else {
        //LPS (Least Probable Symbol)
        binVal = 1 - dec->state & 0x1;
        int numbits = dec->RenormTable[rangelps>>3];
        dec->state = NextStateLPS[state];
        dec->value = (dec->value - scaledRange) << numbits;
        dec->range = rangelps << numbits;
        dec->count += numbits;
    
        if (dec->count >= 0) {
            dec->value += READ_BITS(dec->bits, 8) << dec->count;
            dec->count -= 8;
        }
    }

    return binVal;
}

int
cabac_dec_bin(cabac_dec *dec)
{
    if (dec->bypass) {
        return cabac_dec_bypass(dec);
    }

    return cabac_dec_decision(dec);
}

