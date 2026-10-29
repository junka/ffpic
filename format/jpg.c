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
JPG_probe(FILE *f)
{
    if (f == NULL) {
        return -ENOENT;
    }
    uint16_t soi, eoi;
    int len = fread(&soi, 2, 1, f);
    if (len < 1) {
        return -EBADF;
    }
    fseek(f, -2, SEEK_END);
    len = fread(&eoi, 2, 1, f);
    if (len < 1) {
        return -EBADF;
    }
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
    FFREAD_IGNORE(&tlen, 2, 1, f);
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
            FFREAD_IGNORE(&j->dqt[id].tdata[zigzag[i]], precision + 1, 1, f);
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
    FFREAD_IGNORE(&tlen, 2, 1, f);
    tlen = SWAP(tlen);
    tlen -= 2;
    while (tlen)
    {
        struct dht d;
        FFREAD_IGNORE(&d, 1, 1, f);
        uint8_t ac = d.table_class;
        uint8_t id = d.huffman_id;
        VDBG(jpg, "ac %d, id %d, %02x", ac, id, *(uint8_t *)&d);
        j->dht[ac][id].table_class = d.table_class;
        j->dht[ac][id].huffman_id = d.huffman_id;
        tlen --;
        FFREAD_IGNORE(j->dht[ac][id].num_codecs, 16, 1, f);
        tlen -= 16;
        int len = 0;
        for (int i =0; i < 16; i ++) {
            len += j->dht[ac][id].num_codecs[i];
        }
        // a progressive image may redefine a table between scans
        free(j->dht[ac][id].data);
        j->dht[ac][id].data = malloc(len);
        FFREAD_IGNORE(j->dht[ac][id].data, len, 1, f);
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
dequant_data_unit(const uint16_t *quant, int16_t dstbuf[64], const int16_t srcbuf[64])
{
    for (int i = 0; i < 64; i++) {
        dstbuf[i] = srcbuf[i] * quant[i];
    }
}

static void
refine_coefficient(struct huffman_codec *hdec, int16_t *coef, int positive,
                   int negative)
{
    if (READ_BIT(hdec->v) == 1) {
        if (!(*coef & positive)) {
            *coef += *coef > 0 ? positive : negative;
        }
    }
}

/* Decode one data unit. A baseline scan runs start = 0, end = 63, high = 0.
 * A progressive scan hands over its spectral band and successive
 * approximation bits, and eobrun carries the end-of-band run across blocks
 * and MCUs until a restart marker or the end of the scan. */
static bool
decode_data_unit(struct huffman_codec *hdec, struct jpg_decoder *d, int16_t buf[64],
                 int start, int end, int high, int low, int *eobrun)
{
    const int positive = 1 << low;
    const int negative = -positive;

    if (start == 0) {
        if (high == 0) {
            int dc = huffman_decode_symbol(hdec, d->dc);
            if (dc < 0) {
                VERR(jpg, "invalid dc value");
                return false;
            }
            if (dc > 11) {
                VERR(jpg, "dc length greater than 11");
                return false;
            }
            if (dc) {
                d->prev_dc += get_vlc(READ_BITS(hdec->v, dc), dc);
            }
            buf[0] = d->prev_dc * positive;
        } else {
            if (READ_BIT(hdec->v)) {
                buf[0] |= positive;
            }
        }
        if (end == 0) {
            return true;
        }
    }

    int i = start ? start : 1;
    if (high == 0) {
        /* first visit of the band: only coefficients not seen before */
        if (*eobrun > 0) {
            (*eobrun)--;
            return true;
        }
        while (i <= end) {
            int ac = huffman_decode_symbol(hdec, d->ac);
            if (ac < 0) {
                VERR(jpg, "invalid ac value for %d", i);
                return false;
            }
            int lead_zero = (ac >> 4) & 0xF;
            int bits = ac & 0xF;

            if (bits == 0) {
                if (lead_zero == 15) {
                    // ZRL, sixteen zeros
                    i += 16;
                    continue;
                }
                // the run covers this block and the eobrun blocks after it
                *eobrun = (1 << lead_zero) - 1;
                if (lead_zero) {
                    *eobrun += READ_BITS(hdec->v, lead_zero);
                }
                return true;
            }
            i += lead_zero;
            if (i > end) {
                VERR(jpg, "ac run %d past the band end %d", lead_zero, end);
                return false;
            }
            buf[zigzag[i]] = get_vlc(READ_BITS(hdec->v, bits), bits) * positive;
            i++;
        }
        return true;
    }

    /* refinement visit: coefficients already set only get a correction bit */
    if (*eobrun > 0) {
        for (; i <= end; i++) {
            if (buf[zigzag[i]]) {
                refine_coefficient(hdec, &buf[zigzag[i]], positive, negative);
            }
        }
        (*eobrun)--;
        return true;
    }

    while (i <= end) {
        int ac = huffman_decode_symbol(hdec, d->ac);
        if (ac < 0) {
            VERR(jpg, "invalid ac value for %d", i);
            return false;
        }
        int lead_zero = (ac >> 4) & 0xF;
        int bits = ac & 0xF;
        int fresh = 0;

        if (bits == 0) {
            if (lead_zero != 15) {
                *eobrun = 1 << lead_zero;
                if (lead_zero) {
                    *eobrun += READ_BITS(hdec->v, lead_zero);
                }
                for (; i <= end; i++) {
                    if (buf[zigzag[i]]) {
                        refine_coefficient(hdec, &buf[zigzag[i]], positive, negative);
                    }
                }
                (*eobrun)--;
                return true;
            }
            // ZRL, sixteen zeros
        } else {
            if (bits != 1) {
                VERR(jpg, "invalid refinement size %d", bits);
                return false;
            }
            fresh = READ_BIT(hdec->v) ? positive : negative;
        }

        while (i <= end) {
            int16_t *coef = &buf[zigzag[i]];
            if (*coef) {
                refine_coefficient(hdec, coef, positive, negative);
            } else {
                if (lead_zero == 0) {
                    break;
                }
                lead_zero--;
            }
            i++;
        }
        if (fresh) {
            if (i > end) {
                VERR(jpg, "refined coefficient past the band end %d", end);
                return false;
            }
            buf[zigzag[i]] = fresh;
        }
        i++;
    }

    return true;
}

static huffman_tree *
build_huffman_tree(struct dht *table, int id)
{
    if (!table->data || table->len <= 0) {
        return NULL;
    }
    huffman_tree *tree = huffman_tree_init();
    struct huffman_symbol *sym =
        huffman_symbol_alloc(table->num_codecs, table->data);
    huffman_build_lookup_table(tree, id, sym);
    free(sym->syms);
    free(sym);
    return tree;
}

/* The scan carries its own huffman table selectors, and a component that only
 * appears in another scan may not have the table at all: only build what this
 * scan is going to use. */
static int
init_decoder(JPG *j, struct jpg_decoder *d, int comp_id, int dc_id, int ac_id,
             bool need_dc, bool need_ac)
{
    memset(d, 0, sizeof(*d));
    if (need_dc) {
        d->dc = build_huffman_tree(&j->dht[DHT_CLASS_DC][dc_id], dc_id);
        if (!d->dc) {
            return -1;
        }
    }
    if (need_ac) {
        d->ac = build_huffman_tree(&j->dht[DHT_CLASS_AC][ac_id], ac_id);
        if (!d->ac) {
            return -1;
        }
    }
    d->quant = j->dqt[j->sof.colors[comp_id].qt_id & 3].tdata;
    d->prev_dc = 0;
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
free_scan_decoder(struct jpg_decoder *d)
{
    if (d->dc) {
        huffman_cleanup(d->dc);
        d->dc = NULL;
    }
    if (d->ac) {
        huffman_cleanup(d->ac);
        d->ac = NULL;
    }
}

static void
component_sampling(const JPG *j, int cid, int *h, int *v)
{
    *h = j->sof.colors[cid].horizontal;
    *v = j->sof.colors[cid].vertical;
    if (*h <= 0) {
        *h = 1;
    }
    if (*v <= 0) {
        *v = 1;
    }
}

static void
scan_restart(JPG *j, struct jpg_decoder dec[4], bool used[4],
             struct huffman_codec *hdec, int *eobrun, int *restarts)
{
    *restarts = j->dri.interval;
    for (int i = 0; i < 4; i++) {
        if (used[i]) {
            reset_decoder(&dec[i]);
        }
    }
    huffman_reset_stream(hdec);
    *eobrun = 0;
}

/* Decode one scan into the coefficient buffers. Everything is turned into
 * pixels once, after the last scan, so scans may visit the blocks in any
 * order and cover any band. */
static void
JPG_decode_scan(JPG *j, uint8_t *rawdata, int len)
{
    /* the scan data is handed over to the huffman codec below, which frees it */
    if (!rawdata) {
        return;
    }
    if (!len || !j->yuv[0] || j->sof.components_num < 1) {
        free(rawdata);
        return;
    }
    int nscan = j->sos.nums;
    if (nscan < 1 || nscan > 4) {
        VERR(jpg, "invalid scan component count %d", nscan);
        free(rawdata);
        return;
    }
    int start = j->sos.predictor_start;
    int end = j->sos.predictor_end;
    int high = j->sos.approx_bits_h;
    int low = j->sos.approx_bits_l;
    if (start > end || end > 63) {
        VERR(jpg, "invalid spectral band %d-%d", start, end);
        free(rawdata);
        return;
    }

    int ncomp = j->sof.components_num > 3 ? 3 : j->sof.components_num;
    int table_cid[256];
    for (int i = 0; i < 256; i++) {
        table_cid[i] = -1;
    }
    for (int i = 0; i < ncomp; i++) {
        table_cid[j->sof.colors[i].cid] = i;
    }

    struct jpg_decoder dec[4];
    bool used[4] = {false, false, false, false};
    int cid_of_scan[4] = {0, 0, 0, 0};
    memset(dec, 0, sizeof(dec));

    for (int i = 0; i < nscan; i++) {
        int cid = table_cid[j->sos.comps[i].component_selector];
        if (cid < 0 || cid >= ncomp) {
            VERR(jpg, "invalid scan component id %d",
                 j->sos.comps[i].component_selector);
            goto cleanup;
        }
        cid_of_scan[i] = cid;
        if (!used[cid]) {
            used[cid] = true;
            if (init_decoder(j, &dec[cid], cid, j->sos.comps[i].DC_entropy,
                             j->sos.comps[i].AC_entropy,
                             start == 0 && high == 0, end > 0) != 0) {
                VERR(jpg, "missing huffman table for component %d", cid);
                goto cleanup;
            }
        }
    }

    struct huffman_codec *hdec = huffman_codec_init(rawdata, len);
    int eobrun = 0;
    int restarts = j->dri.interval;

    if (nscan > 1) {
        /* interleaved: every MCU carries h*v blocks of each scan component */
        for (int my = 0; my < j->mcu_rows; my++) {
            for (int mx = 0; mx < j->mcu_cols; mx++) {
                for (int i = 0; i < nscan; i++) {
                    int cid = cid_of_scan[i];
                    int h, v;
                    component_sampling(j, cid, &h, &v);
                    for (int hy = 0; hy < v; hy++) {
                        for (int hx = 0; hx < h; hx++) {
                            int idx = (my * j->vmax + hy) * j->bg_w +
                                      mx * j->hmax + hx;
                            if (!decode_data_unit(hdec, &dec[cid],
                                                  j->yuv[cid] + 64 * idx,
                                                  start, end, high, low,
                                                  &eobrun)) {
                                goto scan_done;
                            }
                        }
                    }
                }
                if (restarts > 0 && --restarts == 0) {
                    scan_restart(j, dec, used, hdec, &eobrun, &restarts);
                }
            }
        }
    } else {
        /* non interleaved: one block per MCU, walked on the component's own
         * block grid */
        int cid = cid_of_scan[0];
        int h, v;
        component_sampling(j, cid, &h, &v);
        int comp_w = (j->sof.width * h + j->hmax - 1) / j->hmax;
        int comp_h = (j->sof.height * v + j->vmax - 1) / j->vmax;
        int bx_n = (comp_w + 7) >> 3;
        int by_n = (comp_h + 7) >> 3;
        for (int by = 0; by < by_n; by++) {
            for (int bx = 0; bx < bx_n; bx++) {
                int idx = (by / v * j->vmax + by % v) * j->bg_w +
                          bx / h * j->hmax + bx % h;
                if (!decode_data_unit(hdec, &dec[cid], j->yuv[cid] + 64 * idx,
                                      start, end, high, low, &eobrun)) {
                    goto scan_done;
                }
                if (restarts > 0 && --restarts == 0) {
                    scan_restart(j, dec, used, hdec, &eobrun, &restarts);
                }
            }
        }
    }

scan_done:
    huffman_codec_free(hdec);
    goto dec_cleanup;
cleanup:
    free(rawdata);
dec_cleanup:
    for (int i = 0; i < 4; i++) {
        if (used[i]) {
            free_scan_decoder(&dec[i]);
        }
    }
}

/* The final pixels are built after every scan landed its coefficients. */
static void
JPG_flush_image(JPG *j, int pitch)
{
    if (!j->data || !j->yuv[0] || !j->sof.width || !j->sof.height) {
        return;
    }
    memset(j->data, 0, j->data_len);

    const struct dct_ops *dct = get_dct_ops(16);
    const struct cs_ops *cs_bgr = get_cs_ops(16);
    int ncomp = j->sof.components_num > 3 ? 3 : j->sof.components_num;
    int yh, yv;
    component_sampling(j, 0, &yh, &yv);

    int16_t blk[3][64 * 4];
    /* neutral chroma for the single component case */
    int16_t neutral[64];
    for (int i = 0; i < 64; i++) {
        neutral[i] = 128;
    }

    for (int my = 0; my < j->mcu_rows; my++) {
        for (int mx = 0; mx < j->mcu_cols; mx++) {
            for (int k = 0; k < ncomp; k++) {
                int h, v;
                component_sampling(j, k, &h, &v);
                const uint16_t *quant =
                    j->dqt[j->sof.colors[k].qt_id & 3].tdata;
                for (int vi = 0; vi < v; vi++) {
                    for (int hi = 0; hi < h; hi++) {
                        int idx = (my * j->vmax + vi) * j->bg_w +
                                  mx * j->hmax + hi;
                        dequant_data_unit(quant, &blk[k][64 * (vi * h + hi)],
                                          j->yuv[k] + 64 * idx);
                        dct->idct_8x8(&blk[k][64 * (vi * h + hi)], 8);
                    }
                }
            }

            uint8_t *ptr = j->data + (size_t)(my * 8 * yv) * pitch +
                           (size_t)(mx * 8 * yh) * 4;
            void *U = ncomp == 1 ? (void *)neutral : (void *)blk[1];
            void *V = ncomp == 1 ? (void *)neutral : (void *)blk[2];
            cs_bgr->YUV_to_BGRA32(ptr, pitch, blk[0], U, V, yv, yh);
        }
    }
}

/* Entropy coded segments run until the next marker. Byte stuffing and the
 * restart markers are removed here, and the stream is left at the marker so
 * the main loop can parse it. A scan of a progressive image may be followed
 * by any marker (usually DHT or SOS), not only EOI. */
static uint8_t *
read_compressed_scan(FILE *f, int *len)
{
    long pos = ftell(f);
    long last = pos;
    while (1) {
        int c = fgetc(f);
        if (c == EOF) {
            last = ftell(f);
            break;
        }
        if (c != 0xFF) {
            continue;
        }
        int n = fgetc(f);
        while (n == 0xFF) {
            n = fgetc(f);
        }
        if (n == EOF) {
            last = ftell(f);
            break;
        }
        if (n == 0x00 || (n >= 0xD0 && n <= 0xD7)) {
            // stuffed 0xFF or a restart marker inside the entropy data
            continue;
        }
        last = ftell(f) - 2;
        break;
    }

    long avail = last > pos ? last - pos : 0;
    uint8_t *compressed = malloc(avail ? (size_t)avail : 1);
    if (!compressed) {
        return NULL;
    }
    VDBG(jpg, "read image from 0x%lx to 0x%lx", pos, last);
    fseek(f, pos, SEEK_SET);
    int l = 0;
    for (long i = 0; i < avail; i++) {
        int c = fgetc(f);
        if (c == EOF) {
            break;
        }
        if (c != 0xFF) {
            compressed[l++] = (uint8_t)c;
            continue;
        }
        int n = fgetc(f);
        i++;
        if (n == 0x00) {
            // stuffed 0xff
            compressed[l++] = 0xFF;
        } else if (n != EOF && !(n >= 0xD0 && n <= 0xD7)) {
            VERR(jpg, "invalid marker %x in scan data", n);
            compressed[l++] = 0xFF;
            compressed[l++] = (uint8_t)n;
        }
        // restart markers are dropped, they only align the stream
    }
    fseek(f, last, SEEK_SET);
    VDBG(jpg, "real vs alloc: %d vs %ld", l, avail);
    *len = l;
    return compressed;
}

static void
read_sos(JPG* j, FILE *f, bool skip_flag)
{
    FFREAD_IGNORE(&j->sos, 3, 1, f);
    int nscan = j->sos.nums > 4 ? 4 : j->sos.nums;
    FFREAD_IGNORE(j->sos.comps, sizeof(struct comp_sel), nscan, f);
    if (j->sos.nums > nscan) {
        // malformed scan header, keep the file position in sync
        VERR(jpg, "scan component count %d", j->sos.nums);
        fseek(f, (j->sos.nums - nscan) * sizeof(struct comp_sel), SEEK_CUR);
    }
    VDBG(jpg, "component %d", j->sos.nums);
    FFREAD_IGNORE(&j->sos.predictor_start, 3, 1, f);
    VINFO(jpg, "sos spectual selection start %d, end %d", j->sos.predictor_start,
         j->sos.predictor_end);
    VINFO(jpg, "sos successive approximation bits high %d, low %d", j->sos.approx_bits_h,
         j->sos.approx_bits_l);
    int len = 0;
    uint8_t* rawdata = read_compressed_scan(f, &len);
    if (!rawdata) {
        return;
    }
    if (skip_flag) {
        free(rawdata);
    } else {
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
    FFREAD_IGNORE(&j->sof, 8, 1, f);
    j->sof.len = SWAP(j->sof.len);
    j->sof.height = SWAP(j->sof.height);
    j->sof.width = SWAP(j->sof.width);
    FFREAD_IGNORE(j->sof.colors, sizeof(struct jpg_component), j->sof.components_num,
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
    FFREAD_IGNORE(&j->app0, 16, 1, f);
    j->app0.len = SWAP(j->app0.len);
    j->app0.xdensity = SWAP(j->app0.xdensity);
    j->app0.ydensity = SWAP(j->app0.ydensity);
    if ((j->app0.xthumbnail * j->app0.ythumbnail) > 0) {
        j->app0.data = malloc(3 * j->app0.xthumbnail * j->app0.ythumbnail);
        FFREAD_IGNORE(j->app0.data, 3, j->app0.xthumbnail * j->app0.ythumbnail, f);
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
    FFREAD_IGNORE(&soi, 2, 1, f);
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
            p->width = j->sof.width;
            p->height = j->sof.height;
            p->depth = 32;
            {
                /* the scan decoder works in whole MCUs, so the buffers have
                 * to cover the last, partially filled MCU as well */
                j->hmax = 1;
                j->vmax = 1;
                for (int i = 0; i < j->sof.components_num; i++) {
                    int h = j->sof.colors[i].horizontal;
                    int v = j->sof.colors[i].vertical;
                    if (h > j->hmax) {
                        j->hmax = h;
                    }
                    if (v > j->vmax) {
                        j->vmax = v;
                    }
                }
                j->mcu_cols = (j->sof.width + 8 * j->hmax - 1) / (8 * j->hmax);
                j->mcu_rows = (j->sof.height + 8 * j->vmax - 1) / (8 * j->vmax);
                j->bg_w = j->mcu_cols * j->hmax;
                j->bg_h = j->mcu_rows * j->vmax;

                p->pitch = j->bg_w * 8 * 4;
                j->data_len = p->pitch * j->bg_h * 8;
                j->data = malloc(j->data_len);
                size_t yuv_len = (size_t)j->bg_w * j->bg_h * 64;
                j->yuv[0] = calloc(yuv_len ? yuv_len : 1, sizeof(int16_t));
                j->yuv[1] = calloc(yuv_len ? yuv_len : 1, sizeof(int16_t));
                j->yuv[2] = calloc(yuv_len ? yuv_len : 1, sizeof(int16_t));
            }
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
            FFREAD_IGNORE(&j->comment, 2, 1, f);
            j->comment.len = SWAP(j->comment.len);
            j->comment.data = malloc(j->comment.len - 2);
            FFREAD_IGNORE(j->comment.data, j->comment.len - 2, 1, f);
            break;
        case DRI:
            VDBG(jpg, "DRI");
            FFREAD_IGNORE(&j->dri, sizeof(struct dri), 1, f);
            j->dri.interval = SWAP(j->dri.interval);
            break;
        case APP1:
            VDBG(jpg, "app1 exif");
            // fall through
        default:
            VDBG(jpg, "skip marker %x", SWAP(m));
            FFREAD_IGNORE(&len, 2, 1, f);
            len = SWAP(len);
            fseek(f, len - 2, SEEK_CUR);
            break;
        }
        VDBG(jpg, "offset at 0x%zx", ftell(f));
        m = read_marker_skip_null(f);
    }
    VDBG(jpg, "done one image %lu", ftell(f));

    if (!skip_flag) {
        JPG_flush_image(j, p->pitch);
    }

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
