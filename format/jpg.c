#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "bitstream.h"
#include "byteorder.h"
#include "file.h"
#include "jpg.h"
#include "queue.h"
#include "utils.h"
#include "huffman.h"
#include "vlog.h"
#include "idct.h"
#include "colorspace.h"

VLOG_REGISTER(jpg, INFO)

struct jpg_decoder {
    int prev_dc;
    huffman_tree *dc;
    huffman_tree *ac;
    uint16_t *quant;
    uint8_t last_rst_marker_seen;
};

static const uint8_t zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10,
    17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static int
JPG_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(jpg, "fail to open %s", filename);
        return -ENOENT;
    }
    uint16_t soi, eoi;
    int len = fread(&soi, 2, 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fseek(f, -2, SEEK_END);
    len = fread(&eoi, 2, 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (SOI == soi && eoi == EOI)
        return 0;

    return -EINVAL;
}

static uint16_t
read_marker_skip_null(FILE *f)
{
    uint8_t c = fgetc(f);
    if (c != 0xFF) {
        VDBG(jpg, "marker 0x%x at %lu", c, ftell(f));
        return 0;
    }
    while (c == 0xFF && !feof(f)) {
        c = fgetc(f);
    }
    return MARKER(c);
}


static void
read_dqt(JPG *j, FILE *f)
{
    uint8_t c;
    uint16_t tlen;
    fread(&tlen, 2, 1, f);
    tlen = SWAP(tlen);
    tlen -= 2;
    while (tlen) {
        c = fgetc(f);
        tlen -= 1;
        uint8_t id = (c & 0xF);
        uint8_t precision = (c >> 4) & 0xF;
        if (id > 3) {
            VERR(jpg, "invalid dqt id %d", id);
        }
        j->dqt[id].id = id;
        j->dqt[id].precision = precision;
        for (int i = 0; i < 64; i ++) {
            fread(&j->dqt[id].tdata[zigzag[i]], precision + 1, 1, f);
            if (precision == 1) {
                j->dqt[id].tdata[zigzag[i]] = SWAP(j->dqt[id].tdata[zigzag[i]]);
            }
        }
        tlen -= ((precision + 1) << 6);
        VDBG(jpg, "dqt left %d", tlen);
    }
}

static void
write_dqt(int id, const uint16_t *quant, FILE *f)
{
    //precision = 0, use 8bit
    uint16_t mark = DQT;
    uint8_t c = id & 0xF;
    uint16_t tlen = 64 + 1 + 2;
    fwrite(&mark, 2, 1, f);
    tlen = SWAP(tlen);
    fwrite(&tlen, 2, 1, f);
    fwrite(&c, 1, 1, f);
    for (int i = 0; i < 64; i++) {
        uint8_t q = quant[zigzag[i]];
        fputc(q, f);
    }
}

static void
read_dht(JPG* j, FILE *f)
{
    uint16_t tlen;
    fread(&tlen, 2, 1, f);
    tlen = SWAP(tlen);
    tlen -= 2;
    while (tlen)
    {
        struct dht d;
        fread(&d, 1, 1, f);
        uint8_t ac = d.table_class;
        uint8_t id = d.huffman_id;
        VDBG(jpg, "ac %d, id %d, %02x", ac, id, *(uint8_t *)&d);
        j->dht[ac][id].table_class = d.table_class;
        j->dht[ac][id].huffman_id = d.huffman_id;
        tlen --;
        fread(j->dht[ac][id].num_codecs, 16, 1, f);
        tlen -= 16;
        int len = 0;
        for (int i =0; i < 16; i ++) {
            len += j->dht[ac][id].num_codecs[i];
        }
        j->dht[ac][id].data = malloc(len);
        fread(j->dht[ac][id].data, len, 1, f);
        j->dht[ac][id].len = len;
        tlen -= len;
    }
}

// see ITU-T81 Table K.3 and K.5
const uint8_t y_dc_count[16] = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
};
const uint8_t y_dc_sym[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t y_ac_count[16] = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125,
};
const uint8_t y_ac_sym[] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
// see ITU-T81 Table K.4 and K.6
const uint8_t uv_dc_count[16] = {0, 3, 1, 1, 1, 1, 1, 1,
                                 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t uv_dc_sym[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t uv_ac_count[16] = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119,
};
const uint8_t uv_ac_sym[] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

static void
write_dht(int id, int class, const uint8_t count[16], const uint8_t *sym, FILE *f)
{
    // precision = 0, use 8bit
    uint16_t mark = DHT;
    uint8_t c = (id & 0xF) | (class << 4);
    fwrite(&mark, 2, 1, f);
    int total = 0;
    for(int i = 0; i < 16; i++) {
        total += count[i];
    }
    uint16_t tlen = 16 + total + 1 + 2;
    tlen = SWAP(tlen);
    fwrite(&tlen, 2, 1, f);
    fputc(c, f);
    fwrite(count, 1, 16, f);
    fwrite(sym, 1, total, f);
}

static int 
get_vlc(int code, int bitlen)
{
    if (bitlen == 0)
        return 0;
    // if msb is not 0, a negtiva value
    if ((code << 1) < (1 << bitlen))
        return code + 1 - (1 << bitlen);
    // else is code itself
    return code;
}

static int
encode_vlc(int code)
{
    int abscode = code > 0 ? code : -code;
    int mask = 1 << 15;
    if (abscode == 0) {
        return 0;
    }
    int i = 15;
    while (i && !(abscode & mask)) {
        mask >>= 1;
        i --;
    }
    return i + 1;
}

static void
dequant_data_unit(struct jpg_decoder *d, int16_t dstbuf[64], int16_t srcbuf[64], int end)
{
    for (int i = 0; i <= end; i++) {
        dstbuf[i] = srcbuf[i] * d->quant[i];
    }
}

static bool
decode_data_unit(struct huffman_codec *hdec, struct jpg_decoder *d, int16_t buf[64], int start, int end, int high, int low, int *skip)
{
    int dc, ac;
    huffman_tree *dc_tree = d->dc;
    huffman_tree *ac_tree = d->ac;
    if (start == 0 && high != 0) {
        dc = READ_BIT(hdec->v);
        buf[0] |= dc << low;
    } else if (start == 0 && high == 0) {
        dc = huffman_decode_symbol(hdec, dc_tree);
        if (dc == -1) {
            VERR(jpg, "invalid dc value");
            return false;
        }
        if (dc > 11) {
            VERR(jpg, "dc length greater than 11");
            return false;
        }
        // VDBG(jpg, "DC read %d bits", dc);
        dc = get_vlc(READ_BITS(hdec->v, dc), dc);
        dc += d->prev_dc;
        d->prev_dc = dc;
        buf[0] = dc << low;
        // VDBG(jpg, "DC read value is %d", dc);
        // fprintf(vlog_get_stream(), "DC Now:\n ");
        // for (int i = 0; i < 8; i++) {
        //     for (int j = 0; j < 8; j++) {
        //         fprintf(vlog_get_stream(), "%d ", buf[i * 8 + j]);
        //     }
        //     fprintf(vlog_get_stream(), "\n");
        // }
    }
    if (end > 0) {
        int positive;
        int negative;
#if 0
        fprintf(vlog_get_stream(), "AC before:\n ");
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                fprintf(vlog_get_stream(), "%d ", buf[i * 8 + j]);
            }
            fprintf(vlog_get_stream(), "\n");
        }
#endif
        int i = start;
        // read AC coff
        if (start == 0) {
            // baseline, just increment
            i = 1;
        } else {
            if (high == 0) {
                // ac first visit
                if (*skip > 0) {
                    (*skip) -= 1;
                    return true;
                }
            } else {
                // ac refine
                positive = 1 << low;
                negative = (-1) << high;
                if ((*skip) > 0) {
                    for (; i <= end; i++) {
                        if (buf[zigzag[i]]!= 0) {
                            if (READ_BIT(hdec->v) == 1) {
                                if ((buf[zigzag[i]] & positive) == 0) {
                                    if (buf[zigzag[i]] >= 0) {
                                        buf[zigzag[i]] += positive;
                                    } else {
                                        buf[zigzag[i]] += negative;
                                    }
                                }
                            }
                        }
                    }
                    (*skip) -= 1;
                    return true;
                }
            }
        }
        for (; i <= end;) {
            ac = huffman_decode_symbol(hdec, ac_tree);
            if (ac == -1) {
                VERR(jpg, "invalid ac value for %d", i);
                return false;
            }
            int lead_zero = (ac >> 4) & 0xF;
            ac = (ac & 0xF);

            if (ac == EOB) {
                //skip 16 zero
                if (lead_zero == 15)
                    lead_zero ++;
                //fill all left ac as zero
                else {
                    if (lead_zero == 0 && start == 0) { 
                        lead_zero = 64 - i;
                    }
                    if (start > 0) {
                        if (high == 0) {
                            //for ac first 0-14
                            *skip = (1<< lead_zero)-1;
                            *skip += READ_BITS(hdec->v, lead_zero);
                        } else {
                            //for ac refine
                            *skip = (1 << lead_zero);
                            *skip += READ_BITS(hdec->v, lead_zero);
                        }
                    }
                }
            } else if (ac != 0 && high > 0) {
                if (READ_BIT(hdec->v) == 1) {
                    ac = positive;
                } else {
                    ac = negative;
                }
            }

            while (lead_zero > 0 && i <= end) {
                if(high && buf[zigzag[i]]!=0) {
                    if (READ_BIT(hdec->v) == 1) {
                        if ((buf[zigzag[i]] & positive) == 0) {
                            buf[zigzag[i]] += positive;
                        } else {
                            buf[zigzag[i]] += negative;
                        }
                    }
                } else if (high) {
                    lead_zero --;
                } else {
                    buf[zigzag[i]] = 0;
                    lead_zero --;
                }
                i++;
            }

            if (ac) {
                if (high) {
                    buf[zigzag[i++]] = ac;
                } else {
                    // VDBG(jpg, "AC read %d bits", ac);
                    ac = get_vlc(READ_BITS(hdec->v, ac), ac);
                    // VDBG(jpg, "AC read value is %d", ac);
                    buf[zigzag[i++]] = ac << low;
                }
            }

        }
#if 0
        fprintf(vlog_get_stream(), "after \n ");
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                fprintf(vlog_get_stream(), "%d ", buf[i * 8 + j]);
            }
            fprintf(vlog_get_stream(), "\n");
        }
#endif
    }

    return true;
}

static int 
init_decoder(JPG* j, struct jpg_decoder *d, uint8_t comp_id)
{
    if (comp_id >= j->sof.components_num) {
        return -1;
    }
    huffman_tree * dc_tree = huffman_tree_init();
    huffman_tree * ac_tree = huffman_tree_init();
    int dc_ht_id = j->sos.comps[comp_id].DC_entropy;
    int ac_ht_id = j->sos.comps[comp_id].AC_entropy;
    struct huffman_symbol *dcsym = huffman_symbol_alloc(j->dht[0][dc_ht_id].num_codecs, j->dht[0][dc_ht_id].data);
    struct huffman_symbol *acsym = huffman_symbol_alloc(j->dht[1][ac_ht_id].num_codecs, j->dht[1][ac_ht_id].data);
    huffman_build_lookup_table(dc_tree, dc_ht_id, dcsym);
    huffman_build_lookup_table(ac_tree, ac_ht_id, acsym);
    // huffman_dump_table(vlog_get_stream(), dc_tree);
    // huffman_dump_table(vlog_get_stream(), ac_tree);

    d->prev_dc = 0;
    d->dc = dc_tree;
    d->ac = ac_tree;
    d->quant = j->dqt[j->sof.colors[comp_id].qt_id].tdata;
    d->last_rst_marker_seen = 0;

    return 0;
}

static void
reset_decoder(struct jpg_decoder *d)
{
    d->prev_dc = 0;
}

static void
destroy_decoder(struct jpg_decoder *d)
{
    huffman_cleanup(d->dc);
    huffman_cleanup(d->ac);
    d->quant = NULL;
    free(d);
}

static void
JPG_decode_scan(JPG* j, uint8_t *rawdata, int len)
{
    uint8_t *data = rawdata;
    if (!rawdata || !len) {
        return ;
    }
    memset(j->data, 0, j->data_len);
    // each component owns a decoder, could be CMYK
    const struct dct_ops *dct = get_dct_ops(16);
    const struct cs_ops *cs_bgr = get_cs_ops(16);

    struct jpg_decoder *d[4];
    //components_num is 1 or 3
    for (int i = 0; i < j->sof.components_num; i ++) {
        d[i] = malloc(sizeof(struct jpg_decoder));
        init_decoder(j, d[i], i);
    }

    // stride value from dc
    int yvertical = j->sof.colors[0].vertical;
    int yhorizontal = j->sof.colors[0].horizontal;
    int ystride =  yvertical* 8;  //means lines per mcu
    int xstride =  yhorizontal * 8;  //means rows per mcu

    uint8_t *ptr;
    int width = ((j->sof.width + 7) >> 3) << 3; //algin to 8
    int height = j->sof.height; //((j->sof.height + 7) >> 3) << 3;
    int pitch = width * 4;

    int restarts = j->dri.interval;

    int start = j->sos.predictor_start;
    int end = j->sos.predictor_end;
    int high = j->sos.approx_bits_h;
    int low = j->sos.approx_bits_l;

#if 0
    hexdump(stdout, "jpg raw data", "", data, 166);
#endif
    struct huffman_codec * hdec = huffman_codec_init(data, len);

    int16_t Y[3][64*4], *U, *V;
    int16_t dummy[64] = {0};
    uint8_t table_cid[4] = {0};
    int16_t *yuv[3];
    int skip = 0;
    for (uint8_t i = 0; i < j->sof.components_num; ++i) {
        // int v = j->sof.colors[i].vertical;
        // int h = j->sof.colors[i].horizontal;
        table_cid[j->sof.colors[i].cid] = i;
        // assert(h * v <= 4);
    }

    for (int y = 0; y < height; y += ystride) {
        ptr = j->data + y * pitch;
        for (int x = 0; x < width; x += xstride) {
            VDBG(jpg, "(%d, %d) width %d MCU index %d", x, y, width, y / 8 * (width) / 8 + x / 8);
            // for YUV420, get 4 DCU for Y and 1 DCU for U and 1 DCU for V
            yuv[0] = j->yuv[0] + 64 * (y / 8 * (width) / 8 + x/8);
            yuv[1] = j->yuv[1] + 64 * (y / 8 * (width) / 8 + x/8);
            yuv[2] = j->yuv[2] + 64 * (y / 8 * (width) / 8 + x/8);

            for (uint8_t i = 0; i < j->sos.nums; ++i) {
                int cid = table_cid[j->sos.comps[i].component_selector];
                int v = j->sof.colors[cid].vertical;
                int h = j->sof.colors[cid].horizontal;

                for (int vi = 0; vi < v && y + vi * 8 < height; vi ++) {
                    for (int hi = 0; hi < h && x + hi * 8 < width; hi ++) {
                        VDBG(jpg, "decode at (%d, %d) [%d, %d] for %d", x, y, hi, vi, cid);
                        if (!decode_data_unit(hdec, d[cid], &yuv[cid][64 * (vi * h + hi)], start, end, high, low, &skip)) {
                            // those MCU at the edge could be incomplete
                            VDBG(jpg, "fail at (%d, %d) [%d, %d] for %d", x, y, hi, vi, cid);
                            continue;
                        }
                        // memcpy(&Y[cid][64 * (vi * h + hi)], , 64 * 2);
                        // dequant_data_unit(d[cid], &Y[cid][64 * (vi * h + hi)], &yuv[cid][64 * (vi * h + hi)], start, end);
                        // dct->idct_8x8(&Y[cid][64 * (vi * h + hi)], 8);
                    }
                }
            }
            for (uint8_t k = 0; k < j->sof.components_num; k++) {
                int v = j->sof.colors[k].vertical;
                int h = j->sof.colors[k].horizontal;
                for (int vi = 0; vi < v && y + vi * 8 < height; vi ++) {
                    for (int hi = 0; hi < h && x + hi * 8 < width; hi ++) {
                        dequant_data_unit(d[k], &Y[k][64 * (vi * h + hi)],
                                          &yuv[k][64 * (vi * h + hi)], end);
                        dct->idct_8x8(&Y[k][64 * (vi * h + hi)], 8);
                    }
                }
            }

            if (j->sof.components_num == 1) {
                U = dummy;
                V = dummy;
            } else {
                U = Y[1];
                V = Y[2];
            }

            cs_bgr->YUV_to_BGRA32(ptr, pitch, Y[0], U, V, yvertical, yhorizontal);

            if (restarts > 0) {
                restarts --;
                if (restarts == 0) {
                    restarts = j->dri.interval;
                    for (int i = 0; i < j->sof.components_num; i ++) {
                        reset_decoder(d[i]);
                    }
                    huffman_reset_stream(hdec);
                    skip = 0;
                    // read_next_rst_marker(d[0]);
                }
            }
            ptr += xstride * 4; // skip to next
        }
    }

    // hexdump(stdout, "jpg decode data", j->data, 160);

    for (int i = 0; i < j->sof.components_num; i ++) {
        destroy_decoder(d[i]);
    }

    huffman_codec_free(hdec);
}

static uint8_t *
read_compressed_scan(FILE *f, int *len)
{
    size_t pos = ftell(f);
    while (1) {
        uint8_t c = fgetc(f);
        if (c == 0xFF) {
            c = fgetc(f);
            if (c == 0xD9 || c == 0xC4 || c == 0xDA) {
                break;
            }
        }
    }

    size_t last = ftell(f) - 2; //remove EOI length, still enough long
    fseek(f, pos, SEEK_SET);
    uint8_t* compressed = malloc(last - pos);
    uint8_t prev , c = fgetc(f);
    int l = 0, cosum = 1;
    VDBG(jpg, "read image from 0x%zx to 0x%zx", pos, last);
    while (cosum < (int)(last - pos)) {
        /* take 0xFF00 as 0xFF */
        /* and skip rst marker */
        prev = c;
        c = fgetc(f);
        cosum ++;
        if (prev != 0xFF) {
            compressed[l++] = prev;
        }
        // else if (prev == 0xFF && c == 0xD9) {
        //     break;
        // }
        else if (prev == 0xFF && c == 0) {
            compressed[l++] = 0xFF;
            prev = 0;
            c = fgetc(f);
            cosum++;
        } else if (prev == 0xFF && (c >= 0xD0 && c <= 0xD7)) {
            prev = 0;
            c = fgetc(f);
            cosum++;
        } else if (prev == 0xFF && c == 0xFF) {

        } else {
            VERR(jpg, "invalid %x %x", prev, c);
        }
    }
    VDBG(jpg, "real vs alloc: %d vs %ld", l, last-pos);
    *len = l;
    return compressed;
}

static void
read_sos(JPG* j, FILE *f, bool skip_flag)
{
    fread(&j->sos, 3, 1, f);
    fread(j->sos.comps, sizeof(struct comp_sel), j->sos.nums, f);
    VDBG(jpg, "component %d", j->sos.nums);
    fread(&j->sos.predictor_start, 3, 1, f);
    VINFO(jpg, "sos spectual selection start %d, end %d", j->sos.predictor_start,
         j->sos.predictor_end);
    VINFO(jpg, "sos successive approximation bits high %d, low %d", j->sos.approx_bits_h,
         j->sos.approx_bits_l);
    int len;
    uint8_t* rawdata = read_compressed_scan(f, &len);
    if (!skip_flag) {
        JPG_decode_scan(j, rawdata, len);
    }
}

static void
write_sos(FILE *f)
{
    uint16_t mark = SOS;
    fwrite(&mark, 2, 1, f);
    uint16_t len = 12;
    len = SWAP(len);
    fwrite(&len, 2, 1, f);
    uint8_t num = 3;
    fwrite(&num, 1, 1, f);
    struct comp_sel c[3] = {
        {1, 0, 0},
        {2, 1, 1},
        {3, 1, 1}
    };
    fwrite(c, sizeof(struct comp_sel), 3, f);
    uint8_t spe_start = 0, spe_end = 63, approx = 0;
    fwrite(&spe_start, 1, 1, f);
    fwrite(&spe_end, 1, 1, f);
    fwrite(&approx, 1, 1, f);
}

static void
read_sof(JPG *j, FILE *f)
{
    fread(&j->sof, 8, 1, f);
    j->sof.len = SWAP(j->sof.len);
    j->sof.height = SWAP(j->sof.height);
    j->sof.width = SWAP(j->sof.width);
    fread(j->sof.colors, sizeof(struct jpg_component), j->sof.components_num,
          f);
    VDBG(jpg, "height %d, width %d, comp %d", j->sof.height, j->sof.width,
         j->sof.components_num);
    for (int i = 0; i < j->sof.components_num; i++) {
        VDBG(jpg, "cid %d vertical %d, horizon %d, quatation id %d",
             j->sof.colors[i].cid, j->sof.colors[i].vertical,
             j->sof.colors[i].horizontal, j->sof.colors[i].qt_id);
    }
}

static void
write_sof(uint16_t height, uint16_t width, FILE *f, int vert, int horizon)
{
    uint16_t mark = SOF0;
    uint16_t len = 17;
    uint8_t precision = 8; 
    uint8_t components_num = 3;
    fwrite(&mark, 2, 1, f);
    len = SWAP(len);
    fwrite(&len, 2, 1, f);
    fwrite(&precision, 1, 1, f);
    height = SWAP(height);
    fwrite(&height, 2, 1, f);
    width = SWAP(width);
    fwrite(&width, 2, 1, f);
    fwrite(&components_num, 1, 1, f);
    struct jpg_component com[3] = {
        {1, vert, horizon, 0},
        {2, 1, 1, 1},
        {3, 1, 1, 1},
    };
    fwrite(com, sizeof(struct jpg_component), 3, f);
}

static void
write_soi(FILE *f)
{
    uint16_t mark = SOI;
    fwrite(&mark, 2, 1, f);
}

static void
write_eoi(FILE *f)
{
    uint16_t mark = EOI;
    fwrite(&mark, 2, 1, f);
}

static void
read_app0(JPG *j, FILE *f)
{
    fread(&j->app0, 16, 1, f);
    j->app0.len = SWAP(j->app0.len);
    j->app0.xdensity = SWAP(j->app0.xdensity);
    j->app0.ydensity = SWAP(j->app0.ydensity);
    if (j->app0.xthumbnail * j->app0.ythumbnail) {
        j->app0.data = malloc(3 * j->app0.xthumbnail * j->app0.ythumbnail);
        fread(j->app0.data, 3, j->app0.xthumbnail * j->app0.ythumbnail, f);
    }
}

static void
write_app0(FILE *f)
{
    uint16_t marker = APP0;
    fwrite(&marker, 2, 1, f);
    uint16_t len = 16;
    len = SWAP(len);
    fwrite(&len, 2, 1, f);
    uint8_t id[5] = "JFIF";
    fwrite(id, 5, 1, f);
    uint16_t version = 0x0101;
    fwrite(&version, 2, 1, f);
    uint8_t unit = 0;
    fwrite(&unit, 1, 1, f);
    uint16_t density = 1;
    density = SWAP(density);
    fwrite(&density, 2, 1, f);
    fwrite(&density, 2, 1, f);
    uint8_t thumbnail = 0;
    fwrite(&thumbnail, 1, 1, f);
    fwrite(&thumbnail, 1, 1, f);
}

static struct pic *
JPG_load_one(FILE *f, int skip_flag)
{
    struct pic *p = pic_alloc(sizeof(JPG));
    JPG *j = p->pic;
    j->data = NULL;
    uint16_t soi, m, len;
    fread(&soi, 2, 1, f);
    if (soi != SOI) {
        return NULL;
    }
    m = read_marker_skip_null(f);
    // int num_sos = 0;
    // 0xFFFF means eof
    while (m != EOI && m != 0 && m != 0xFFFF) {
        switch (m) {
        case SOF0:
        case SOF1:
        case SOF2:
            VDBG(jpg, "SOFn");
            read_sof(j, f);
            p->width = ((j->sof.width + 7) >> 3) << 3;
            p->height = j->sof.height;
            p->depth = 32;
            p->pitch = ((p->width * 32 + 32 - 1) >> 5) << 2;
            j->data_len = p->pitch * p->height;
            j->data = malloc(p->pitch * (p->height+7)/8*8);
            j->yuv[0] = calloc(1, (p->height + 7)/8 * ((p->width+7)>>3) * 64 * sizeof(int16_t));
            j->yuv[1] = calloc(1, (p->height +7)/8 * ((p->width+7)>>3) * 64 * sizeof(int16_t));
            j->yuv[2] = calloc(1, (p->height+7)/8 * ((p->width+7)>>3) * 64 * sizeof(int16_t));
            break;
        case APP0:
            VDBG(jpg, "APP0");
            read_app0(j, f);
            break;
        case DHT:
            VDBG(jpg, "DHT");
            read_dht(j, f);
            break;
        case DQT:
            VDBG(jpg, "DQT");
            read_dqt(j, f);
            break;
        case SOS:
            VDBG(jpg, "SOS");
            read_sos(j, f, skip_flag);
            p->format = CS_PIXELFORMAT_RGB888;
            p->pixels = j->data;
            // num_sos++;
            // if (num_sos==3) {
            //     return p;
            // }
            break;
        case COM:
            VDBG(jpg, "COM");
            fread(&j->comment, 2, 1, f);
            j->comment.len = SWAP(j->comment.len);
            j->comment.data = malloc(j->comment.len - 2);
            fread(j->comment.data, j->comment.len - 2, 1, f);
            break;
        case DRI:
            VDBG(jpg, "DRI");
            fread(&j->dri, sizeof(struct dri), 1, f);
            j->dri.interval = SWAP(j->dri.interval);
            break;
        case APP1:
            VDBG(jpg, "app1 exif");
        default:
            VDBG(jpg, "skip marker %x", SWAP(m));
            fread(&len, 2, 1, f);
            len = SWAP(len);
            fseek(f, len - 2, SEEK_CUR);
            break;
        }
        VDBG(jpg, "offset at 0x%zx", ftell(f));
        m = read_marker_skip_null(f);
    }
    VDBG(jpg, "done one image %lu", ftell(f));

    p->format = CS_PIXELFORMAT_RGB888;
    p->pixels = j->data;

    return p;
}

static struct pic *
JPG_load(const char *filename, int skip_flag)
{
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, 0, SEEK_SET);
    int num = 1;
    struct pic *p = NULL;
    int load_one_flag = (skip_flag >> 1);
    p = JPG_load_one(f, skip_flag);
    while (!load_one_flag && ftell(f) < end) {
        file_enqueue_pic(p);
        p = JPG_load_one(f, skip_flag);
        num ++;
    }
    fclose(f);
    if (num == 1) {
        return p;
    } else {
        file_enqueue_pic(p);
    }
    return NULL;
}

static void 
JPG_free(struct pic *p)
{
    JPG *j = (JPG *)p->pic;
    if (j->comment.data)
        free(j->comment.data);
    if (j->app0.data)
        free(j->app0.data);
    for (uint8_t ac = 0; ac < 2; ac ++) {
        for (uint8_t i = 0; i < 16; i++) {
            if (j->dht[ac][i].huffman_id == i && j->dht[ac][i].table_class == ac) {
                free(j->dht[ac][i].data);
            }
        }
    }
    if (j->data && j->data_len) {
        free(j->data);
    }
    for (int i = 0; i < 3; i++) {
        if (j->yuv[i]) {
            free(j->yuv[i]);
        }
    }
    pic_free(p);
}

static void 
JPG_info(FILE *f, struct pic* p)
{
    JPG *j = (JPG *)p->pic;
    fprintf(f, "JPEG file format\n");
    fprintf(f, "\twidth %d, height %d\n", j->sof.width, j->sof.height);
    fprintf(f, "\tprecision %d, components num %d\n", j->sof.precision, j->sof.components_num);
    for (int i = 0; i < j->sof.components_num; i ++) {
        fprintf(f, "\t cid %d vertical %d, horizon %d, quatation id %d\n", j->sof.colors[i].cid, j->sof.colors[i].vertical, 
            j->sof.colors[i].horizontal, j->sof.colors[i].qt_id);
    }
    if (j->app0.len) {
        fprintf(f, "\tAPP0: %s version %d.%d\n", j->app0.identifier, j->app0.major, j->app0.minor);
        fprintf(f, "\tAPP0: xdensity %d, ydensity %d %s\n", j->app0.xdensity, j->app0.ydensity,
            j->app0.unit == 0 ? "pixel aspect ratio" : 
            ( j->app0.unit == 1 ? "dots per inch": (j->app0.unit == 2 ? "dots per cm":"")));
    }
    fprintf(f, "-----------------------\n");
    for (uint8_t i = 0; i < 4; i++) {
        if (j->dqt[i].id == i) {
            fprintf(f, "DQT %d: precision %d\n", i, j->dqt[i].precision);
            for (int k = 0; k < 64; k ++ ) {
                if ((k % 8) == 0) {
                    fprintf(f, "\t\t");
                }
                fprintf(f, "%d ", j->dqt[i].tdata[k]);
                if (((k + 1) % 8) == 0) {
                    fprintf(f, "\n");
                }
            }
            fprintf(f, "\n");
        }
    }
    fprintf(f, "-----------------------\n");
    for (uint8_t ac = 0; ac < 2; ac ++) {
        for (uint8_t i = 0; i < 16; i++) {
            if (j->dht[ac][i].huffman_id == i && j->dht[ac][i].table_class == ac) {
                fprintf(f, "%s DHT %d: ", (j->dht[ac][i].table_class == DHT_CLASS_DC ? "DC" : "AC"), i);
                int n = 0;
                for (int k = 0; k < 16; k ++) {
                    fprintf(f, "%02d ",  j->dht[ac][i].num_codecs[k]);
                    // len += j->dht[ac][i].num_codecs[k];
                }
                fprintf(f, "\n");
                for (int k = 0; k < 16; k ++) {
                    fprintf(f, "%d: ", k + 1);
                    int s = 0;
                    while (s < j->dht[ac][i].num_codecs[k]) {
                        fprintf(f, "%x ",  j->dht[ac][i].data[n++]);
                        s ++;
                    }
                    fprintf(f, "\n");
                }
                fprintf(f, "\n");
            }
        }
    }
    fprintf(f, "-----------------------\n");
    fprintf(f, "\tsos: component num %d\n", j->sos.nums);
    for (int i = 0; i < j->sos.nums; i ++) {
        fprintf(f, "\t component id %d DC %d, AC %d\n", j->sos.comps[i].component_selector,
             j->sos.comps[i].DC_entropy, j->sos.comps[i].AC_entropy);
    }
    fprintf(f, "\t spectral selection start %d, end %d\n", j->sos.predictor_start,
            j->sos.predictor_end);
    fprintf(f, "\t successive approximation bit high %d, low %d\n",
            j->sos.approx_bits_h, j->sos.approx_bits_l);
    if (j->dri.interval) {
        fprintf(f, "-----------------------\n");
        fprintf(f, "\tDRI interval %d\n", j->dri.interval);
    }

    if (j->comment.data) {
        fprintf(f, "-----------------------\n");
        fprintf(f, "\tComment: %s\n", j->comment.data);
    }
    fprintf(f, "-----------------------\n");
    fprintf(f, "MacroBlock num: %d * %d\n", ((j->sof.width + 7)/ 8), ((j->sof.height + 7) / 8));
}

static const uint16_t y_quant[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
static const uint16_t uv_quant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

static int init_encoder(struct jpg_decoder *d, uint8_t comp_id,
                        struct huffman_symbol *dc_sym,
                        struct huffman_symbol *ac_sym) {
    // see ITU-T81 Table K.1 and K.2, for Y:UV 2:1

    huffman_tree *dc_tree = huffman_tree_init();
    huffman_tree *ac_tree = huffman_tree_init();
    huffman_build_lookup_table(dc_tree, comp_id, dc_sym);
    huffman_build_lookup_table(ac_tree, comp_id, ac_sym);
    // huffman_dump_table(vlog_get_stream(), dc_tree);
    // huffman_dump_table(vlog_get_stream(), ac_tree);

    d->prev_dc = 0;
    d->dc = dc_tree;
    d->ac = ac_tree;
    if (comp_id == 0) {
        d->quant = (uint16_t *)y_quant;
    } else {
        d->quant = (uint16_t *)uv_quant;
    }
    d->last_rst_marker_seen = 0;

    return 0;
}

static void
encode_data_unit(struct huffman_codec *hdec, struct jpg_decoder *d,
                 int16_t *buf)
{
    int diff = buf[0] - d->prev_dc;
    VINFO(jpg, " %d, %d, %d", diff, buf[0], d->prev_dc);
    d->prev_dc = buf[0];

    // F.1.4.1, Figure F.4
    int bitlen = encode_vlc(diff);
    huffman_encode_symbol(hdec, d->dc, bitlen);
    if (diff < 0) {
        diff = (1 << bitlen) + diff - 1;
    }
    if (bitlen) {
        VINFO(jpg, "write %d in %d bits", diff, bitlen);
        WRITE_BITS(hdec->v, diff, bitlen);
    }
    // fprintf(vlog_get_stream(), "after quant \n");
    // for (int i = 0; i < 64; i++) {
    //     fprintf(vlog_get_stream(), "%d ", buf[i]);
    // }
    // fprintf(vlog_get_stream(), "\n");

    int last_nz = 63;
    while(buf[last_nz] == 0 && last_nz > 0) {
        last_nz--;
    }

    for (int i = 1; i <= last_nz; i++) {
        int j = i;
        while (buf[j] == 0 && j <= last_nz) {
            j ++;
        }
        int lead_zero = j - i;
        for (int n = 0; n < lead_zero / 16; n++) {
            VINFO(jpg, "AC write 0xF0");
            huffman_encode_symbol(hdec, d->ac, 0xF0);
        }
        lead_zero %= 16;
        int aclen = encode_vlc(buf[j]);
        VINFO(jpg, "AC for lastnz %d buf[%d] %d, lead zero %d", last_nz, j, buf[j], lead_zero);
        huffman_encode_symbol(hdec, d->ac, lead_zero << 4 | aclen);
        VINFO(jpg, "AC write %d in %d bits", buf[j], aclen);
        WRITE_BITS(hdec->v, buf[j], aclen);
        i = j;
    }

    if (last_nz != 63) {
        huffman_encode_symbol(hdec, d->ac, EOB);
    }

}

static void
push_and_quant(struct huffman_codec *hdec, struct jpg_decoder *d,
               int16_t buf[64])
{
    int16_t data[64];
    // do quant and zigzag
    int i = 0;
    float q;
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            q = (float)(d->quant[zigzag[i]] * 100 + 50) / 100;
            if (q < 1) {q = 1;}
            else if (q > 255) {q = 255;}
            q = 1.0f / ((float)q);
            data[i] = (int16_t)(buf[zigzag[i]] * q + 16384.5) - 16384;
            i++;
        }
    }
    // }
    // fprintf(vlog_get_stream(), "after quant \n");
    // for (int i = 0; i < 8; i++) {
    //     for (int j = 0; j < 8; j++) {
    //         fprintf(vlog_get_stream(), "%d ", data[i * 8 + j]);
    //     }
    //     fprintf(vlog_get_stream(), "\n");
    // }

    encode_data_unit(hdec, d, data);
    // for (int i = 0; i < 64; i++) {
    //     yuv[i] = data[i];
    // }
}

void
write_compress_data(uint8_t *data, int len, FILE *f)
{
    fwrite(data, len, 1, f);
}

void JPG_encode(struct pic *p, const char *fname)
{
    // int16_t *Y = malloc(p->height * p->pitch * 2);
    // int16_t *U = malloc(p->height * p->pitch / 2);
    // int16_t *V = malloc(p->height * p->pitch / 2);
    // int16_t coeff[64];
    int virt = 2;
    int horiz = 2;
    int y_stride = 8 * virt;
    int x_stride = 8 * horiz;
    struct huffman_codec *hdec = huffman_codec_init(NULL, 0);
    struct jpg_decoder *d[3];

    struct huffman_symbol *y_dc = huffman_symbol_alloc((uint8_t *)y_dc_count, (uint8_t *)y_dc_sym);
    struct huffman_symbol *y_ac = huffman_symbol_alloc((uint8_t *)y_ac_count, (uint8_t *)y_ac_sym);
    struct huffman_symbol *uv_dc = huffman_symbol_alloc((uint8_t *)uv_dc_count, (uint8_t *)uv_dc_sym);
    struct huffman_symbol *uv_ac = huffman_symbol_alloc((uint8_t *)uv_ac_count, (uint8_t *)uv_ac_sym);

    FILE *f = fopen(fname, "wb");
    write_soi(f);
    for (int i = 0; i < 3; i++) {
        d[i] = malloc(sizeof(struct jpg_decoder));
        if (i == 0) {
            init_encoder(d[i], 0, y_dc, y_ac);
        } else {
            init_encoder(d[i], 1, uv_dc, uv_ac);
        }
    }
    write_app0(f);
    write_dqt(0, y_quant, f);
    write_dqt(1, uv_quant, f);
    write_sof(p->height, p->width, f, virt, horiz);
    write_dht(0, 0, y_dc_count, y_dc_sym, f);
    write_dht(0, 1, y_ac_count, y_ac_sym, f);
    write_dht(1, 0, uv_dc_count, uv_dc_sym, f);
    write_dht(1, 1, uv_ac_count, uv_ac_sym, f);

    int16_t Y[64 * 4], U[64], V[64];
    // prepare huffman code table, planar yuv
    // uint8_t *yuv = malloc(p->height * p->width * 3 / 2);
    // uint8_t *u = yuv + p->height * p->width;
    // uint8_t *v = u + p->height * p->width/4;

    //divide and transform in block size, 420
    printf("vert %d, horiz %d\n",
           p->height / y_stride + ((p->height % y_stride) ? 1 : 0),
           p->width / x_stride + ((p->width % x_stride) ? 1 : 0));
    const struct dct_ops *dct = get_dct_ops(8);

    for (int y = 0; y < p->height; y += y_stride) {
        for (int x = 0; x < p->width; x += x_stride) {
            // including downsample
            BGR24_to_YUV420((uint8_t *)p->pixels + y * p->pitch + x*3, p->pitch, Y, U, V);
            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 8; j++) {
                    fprintf(vlog_get_stream(), "%d ", Y[i * 8 + j]);
                }
                fprintf(vlog_get_stream(), "\n");
            }
            fprintf(vlog_get_stream(), "\n");
            dct->fdct_8x8(Y);

            fprintf(vlog_get_stream(), "Block Y0: \n");
            for (int i = 0; i < 8; i++) {
                for (int j = 0; j < 8; j++) {
                    fprintf(vlog_get_stream(), "%d ", Y[i * 8 + j]);
                }
                fprintf(vlog_get_stream(), "\n");
            }

            VINFO(jpg, "encode unit at (%d, %d)", x, y);
            push_and_quant(hdec, d[0], Y);

            if (x + 8 < p->width) {
                dct->fdct_8x8(Y + 64);
                push_and_quant(hdec, d[0], Y + 64);
            }

            if (y + 8 < p->height) {
                dct->fdct_8x8(Y + 128);
                push_and_quant(hdec, d[0], Y + 128);
            }

            if ((x + 8 < p->width) && (y + 8 < p->height)) {
                for (int i = 0; i < 8; i++) {
                    for (int j = 0; j < 8; j++) {
                        fprintf(vlog_get_stream(), "%d ", Y[192+i * 8 + j]);
                    }
                    fprintf(vlog_get_stream(), "\n");
                }
                fprintf(vlog_get_stream(), "\n");
                VINFO(jpg, "encode unit at (%d, %d)", x+8, y+8);
                dct->fdct_8x8(Y + 192);
                push_and_quant(hdec, d[0], Y + 192);
            }
            dct->fdct_8x8(U);
            push_and_quant(hdec, d[1], U);

            dct->fdct_8x8(V);
            push_and_quant(hdec, d[2], V);
        }
    }
    ALIGN_BYTE(hdec->v);

    for (int i = 0; i < 3; i++) {
        destroy_decoder(d[i]);
    }

    write_sos(f);
    // write compressed data here
    bits_vec_dump(hdec->v);
    HEXDUMP(vlog_get_stream(), "This ", "", hdec->v->start, 32);
    write_compress_data(hdec->v->start, hdec->v->len, f);
    write_eoi(f);
    fclose(f);
    huffman_codec_free(hdec);
}

static struct file_ops jpg_ops = {
    .name = "JPG",
    .alias = "JPEG",
    .probe = JPG_probe,
    .load = JPG_load,
    .free = JPG_free,
    .info = JPG_info,
    .encode = JPG_encode,
};

void 
JPG_init(void)
{
    file_ops_register(&jpg_ops);
}
