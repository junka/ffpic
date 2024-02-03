#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include "bitstream.h"
#include "file.h"
#include "jpg.h"
#include "queue.h"
#include "utils.h"
#include "huffman.h"
#include "vlog.h"
#include "idct.h"
#include "colorspace.h"

VLOG_REGISTER(jpg, DEBUG)

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

uint16_t 
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


void 
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
            VERR(jpg, "invalid dqt id");
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
    }
}

void
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
write_vlc(int code)
{
    int abscode = code ? code : -code;
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

//decode vec to decoder buf
static bool
decode_data_unit(struct huffman_codec * hdec, struct jpg_decoder *d, int16_t buf[64])
{
    int dc, ac;
    huffman_tree *dc_tree = d->dc;
    huffman_tree *ac_tree = d->ac;

    dc = huffman_decode_symbol(hdec, dc_tree);
    if (dc == -1) {
        VERR(jpg, "invalid dc value");
        return false;
    }
    // if (dc != 0) {
    //     int code = huffman_read_symbol(hdec, dc);
    //     if ((code << 1) < (1 << dc)) {
    //         dc = code + 1 - (1 << dc);
    //     } else {
    //         dc = code;
    //     }
    // }
    dc = get_vlc(huffman_read_symbol(hdec, dc), dc);
    dc += d->prev_dc;
    d->prev_dc = dc;
    buf[0] = dc;

    // read AC coff
    for (int i = 1; i < 64;) {
        ac = huffman_decode_symbol(hdec, ac_tree);
        if (ac == -1) {
            VERR(jpg, "invalid ac value");
            return false;
        }
        int lead_zero = (ac >> 4) & 0xF;
        ac = (ac & 0xF);

        if (ac == EOB) {
            //skip 16 zero
            if (lead_zero == 15)
                lead_zero ++;
            //fill all left ac as zero
            else if (lead_zero == 0) 
                lead_zero = 64 - i;
        }

        while (lead_zero > 0) {
            buf[zigzag[i++]] = 0;
            lead_zero --;
        }

        if (ac) {
            ac = get_vlc(huffman_read_symbol(hdec, ac), ac);
            buf[zigzag[i++]] = ac;
        }
    }
    // after dequant here, the values will be greater than 255, can not hold in uint8_t
    for (int i = 0; i < 64; i++) {
        buf[i] *= d->quant[i];
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


#if 0
static int 
read_next_rst_marker(struct jpg_decoder *d)
{
    int rst_marker_found = 0;
    int marker;

    int m = huffman_read_symbol(8);
    /* Parse marker */
    while (!rst_marker_found) {
        while (m != 0xff && m != -1) {
            m = huffman_read_symbol(8);
        }
        /* Skip any padding ff byte (this is normal) */
        m = huffman_read_symbol(8);
        while (m == 0xff && m!= -1) {
            m = huffman_read_symbol(8);
        }
        if (m == -1) {
            VERR(jpg, "wrong boudary");
            return -1;
        }

        marker = m;
        if ((0xD0 + d->last_rst_marker_seen) == (marker)) {
            rst_marker_found = 1;
            VERR(jpg, "reset marker found");
        } else if ((marker) >= 0xD0 && MARKER(marker) <= 0xD7) {
            VERR(jpg, "Wrong Reset marker found, abording");
        } else if (MARKER(marker) == EOI)
            return 0;
    }

    d->last_rst_marker_seen++;
    d->last_rst_marker_seen &= 7;

    return 0;
}
#endif

static void
JPG_decode_image(JPG* j)
{
    uint8_t *data = j->data;
    int len = j->data_len;
    j->data = NULL;
    // each component owns a decoder, could be CMYK
    struct jpg_decoder *d[4];
    const struct dct_ops *dct = get_dct_ops(16);
    const struct cs_ops *cs_bgr = get_cs_ops(16);

    //components_num is 1 or 3
    for (int i = 0; i < j->sof.components_num; i ++) {
        d[i] = malloc(sizeof(struct jpg_decoder));
        init_decoder(j, d[i], i);
    }
    // huffman_dump_table(stdout, d[0]->dc);
    // huffman_dump_table(stdout, d[0]->ac);

    //stride value from dc
    int ystride = j->sof.colors[0].vertical * 8;  //means lines per mcu
    int xstride = j->sof.colors[0].horizontal * 8;  //means rows per mcu

    uint8_t *ptr;
    int width = ((j->sof.width + 7) >> 3) << 3; //algin to 8
    int height = j->sof.height; //((j->sof.height + 7) >> 3) << 3;
    int pitch = width * 4;
    int bytes_blockline = pitch * ystride;
    int bytes_mcu = xstride * 4;

    int restarts = j->dri.interval;

    j->data = malloc(pitch * height);
#if 0
    hexdump(stdout, "jpg raw data", "", data, 166);
#endif
    struct huffman_codec * hdec = huffman_codec_init(data, len);

    int16_t Y[3][64*4], *Cr, *Cb;
    int16_t dummy[64] = {0};

    for (uint8_t i = 0; i < j->sof.components_num; ++i) {
        int v = j->sof.colors[i].vertical;
        int h = j->sof.colors[i].horizontal;
        assert(h * v <= 4);
    }
    for (int y = 0; y <= height / ystride; y++) {
        //block start
        ptr = j->data + y * bytes_blockline;
        for (int x = 0; x < width; x += xstride) {
            // for YUV420, get 4 DCU for Y and 1 DCU for U and 1 DCU for V
            for (uint8_t i = 0; i < j->sof.components_num; ++i) {
                int v = j->sof.colors[i].vertical;
                int h = j->sof.colors[i].horizontal;
                for (int vi = 0; vi < v; vi ++) {
                    for (int hi = 0; hi < h; hi ++) {
                        if (!decode_data_unit(hdec, d[i], &Y[i][64 * (vi * h + hi)])) {
                            VDBG(jpg, "fail at %d %d", x, y);
                            exit(-1);
                        }
                        dct->idct_8x8(&Y[i][64 * (vi * h + hi)], 8);
                    }
                }
            }

            if (j->sof.components_num == 1) {
                Cr = dummy;
                Cb = dummy;
            } else {
                Cb = Y[1];
                Cr = Y[2];
            }

            cs_bgr->YUV_to_BGRA32(ptr, pitch, Y[0], Cb, Cr, j->sof.colors[0].vertical,
                   j->sof.colors[0].horizontal);

            if (restarts > 0) {
                restarts --;
                if (restarts == 0) {
                    restarts = j->dri.interval;
                    for (int i = 0; i < j->sof.components_num; i ++) {
                        reset_decoder(d[i]);
                    }
                    huffman_reset_stream(hdec);
                    // read_next_rst_marker(d[0]);
                }
            }
            ptr += bytes_mcu; //skip to next 
        }
    }
    // hexdump(stdout, "jpg decode data", j->data, 160);

    for (int i = 0; i < j->sof.components_num; i ++) {
        destroy_decoder(d[i]);
    }

    huffman_codec_free(hdec);
}

static void
read_compressed_image(JPG* j, FILE *f)
{
    size_t pos = ftell(f);
    while (1) {
        uint8_t c = fgetc(f);
        if (c == 0xFF) {
            c = fgetc(f);
            if (c == 0xD9) {
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
    j->data = compressed;
    j->data_len = l;
}


void 
read_sos(JPG* j, FILE *f)
{
    fread(&j->sos, 3, 1, f);
    fread(j->sos.comps, sizeof(struct comp_sel), j->sos.nums, f);
    fread(&j->sos.predictor_start, 3, 1, f);
    read_compressed_image(j, f);
}

static struct pic *
JPG_load_one(FILE *f)
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
    // 0xFFFF means eof
    while (m != EOI && m != 0 && m != 0xFFFF) {
        switch (m) {
        case SOF0:
            VDBG(jpg, "SOF0");
            fread(&j->sof, 8, 1, f);
            j->sof.len = SWAP(j->sof.len);
            j->sof.height = SWAP(j->sof.height);
            j->sof.width = SWAP(j->sof.width);
            fread(j->sof.colors, sizeof(struct jpg_component),
                      j->sof.components_num, f);
            break;
        case APP0:
            VDBG(jpg, "APP0");
            fread(&j->app0, 16, 1, f);
            if (j->app0.xthumbnail * j->app0.ythumbnail) {
                j->app0.data =
                    malloc(3 * j->app0.xthumbnail * j->app0.ythumbnail);
                fread(j->app0.data, 3, j->app0.xthumbnail * j->app0.ythumbnail,
                      f);
            }
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
            read_sos(j, f);
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
    VDBG(jpg, "done one image");
    JPG_decode_image(j);
    p->width = ((j->sof.width + 7) >> 3) << 3;
    p->height = j->sof.height;
    p->depth = 32;
    p->pitch = ((p->width * 32 + 32 - 1) >> 5) << 2;
    p->format = CS_PIXELFORMAT_RGB888;
    p->pixels = j->data;

    return p;
}

static struct pic *JPG_load(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    int num = 1;
    struct pic *p = NULL;
    p = JPG_load_one(f);
    while (!feof(f)) {
        file_enqueue_pic(p);
        p = JPG_load_one(f);
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
    if (j->data) {
        free(j->data);
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
    fprintf(f, "\tAPP0: %s version %d.%d\n", j->app0.identifier, j->app0.major, j->app0.minor);
    fprintf(f, "\tAPP0: xdensity %d, ydensity %d %s\n", j->app0.xdensity, j->app0.ydensity,
        j->app0.unit == 0 ? "pixel aspect ratio" : 
        ( j->app0.unit == 1 ? "dots per inch": (j->app0.unit == 2 ? "dots per cm":"")));

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
    fprintf(f, "\t start %d, end %d\n", j->sos.predictor_start, j->sos.predictor_end);
    if (j->dri.interval) {
        fprintf(f, "-----------------------\n");
        fprintf(f, "\tDRI interval %d\n", j->dri.interval);
    }

    if (j->comment.data) {
        fprintf(f, "-----------------------\n");
        fprintf(f, "\tComment: %s\n", j->comment.data);
    }
}

static int init_encoder(JPG *j, struct jpg_decoder *d, uint8_t comp_id,
                        struct huffman_symbol *dc_sym,
                        struct huffman_symbol *ac_sym) {
    // see ITU-T81 Table K.1 and K.2, for Y:UV 2:1
    const uint16_t y_quant[8*8] = {
        16, 11, 10, 16, 24,  40,  51,  61,
        12, 12, 14, 19, 26,  58,  60,  55,
        14, 13, 16, 24, 40,  57,  69,  56,
        14, 17, 22, 29, 51,  87,  80,  62,
        18, 22, 37, 56, 68,  109, 103, 77,
        24, 35, 55, 64, 81,  104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101,
        72, 92, 95, 98, 112, 100, 103, 99
    };
    const uint16_t uv_quant[8*8] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
    };

    huffman_tree *dc_tree = huffman_tree_init();
    huffman_tree *ac_tree = huffman_tree_init();
    huffman_build_lookup_table(dc_tree, 0, dc_sym);
    huffman_build_lookup_table(ac_tree, 0, ac_sym);

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

static void encode_data_unit(struct huffman_codec *hdec, struct jpg_decoder *d,
                             int16_t *buf)
{
    uint8_t diff = buf[0] - d->prev_dc;
    d->prev_dc = buf[0];

    // F.1.4.1, Figure F.4
    int bitlen = write_vlc(diff);
    huffman_encode_symbol_8bit(hdec, d->dc, bitlen);
    if (diff < 0) {
        diff = (1 << bitlen) + diff - 1;
    }
    WRITE_BITS(hdec->v, diff, bitlen);

    int last_nz = 63;
    for (int i = 63; i > 1; i--) {
        if (buf[i] != 0) {
            last_nz = i;
            break;
        }
    }

    for (int i = 1; i < last_nz;) {
        int j = i;
        int lead_zero = 0;
        while (buf[j] == 0) {
            lead_zero ++;
            j ++;
        }
        for (int n = 0; n < lead_zero / 16; n++) {
            huffman_encode_symbol_8bit(hdec, d->ac, EOB);
        }
        lead_zero %= 16;
        int aclen = write_vlc(buf[j]);
        huffman_encode_symbol_8bit(hdec, d->ac, lead_zero << 4 | aclen);
        i = j;
    }

    if (last_nz != 63) {
        huffman_encode_symbol_8bit(hdec, d->ac, EOB);
    }

}

static void
push_and_quant(struct huffman_codec *hdec, struct jpg_decoder *d,
               uint8_t *yuv, int16_t buf[64])
{
    int16_t data[64];
    // do quant and zigzag
    for (int i = 0; i < 64; i++) {
        data[i] = buf[zigzag[i]] / d->quant[zigzag[i]];
    }
    encode_data_unit(hdec, d, data);
    for (int i = 0; i < 64; i++) {
        yuv[i] = data[i];
    }
}

void JPG_encode(struct pic *p)
{
    // int16_t *Y = malloc(p->height * p->pitch * 2);
    // int16_t *U = malloc(p->height * p->pitch / 2);
    // int16_t *V = malloc(p->height * p->pitch / 2);
    int16_t coeff[64];
    int y_stride = 16;
    int x_stride = 16;
    struct huffman_codec *hdec = huffman_codec_init(NULL, 0);
    struct jpg_decoder *d[3];

    // see ITU-T81 Table K.3 and K.5
    const uint8_t y_dc_count[16] = {
        0, 1, 5, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    };
    const uint8_t y_dc_sym[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
    };
    const uint8_t y_ac_count[16] = {
        0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125,
    };
    const uint8_t y_ac_sym[] = {
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
        0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
        0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
        0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
        0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
        0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
        0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
        0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
        0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
        0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
        0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
        0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
        0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
        0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
        0xf9, 0xfa
    };
    // see ITU-T81 Table K.4 and K.6
    const uint8_t uv_dc_count[16] = {
        0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
    };
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
        0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa
    };
    struct huffman_symbol *y_dc = huffman_symbol_alloc((uint8_t *)y_dc_count, (uint8_t *)y_dc_sym);
    struct huffman_symbol *y_ac = huffman_symbol_alloc((uint8_t *)y_ac_count, (uint8_t *)y_ac_sym);
    struct huffman_symbol *uv_dc = huffman_symbol_alloc((uint8_t *)uv_dc_count, (uint8_t *)uv_dc_sym);
    struct huffman_symbol *uv_ac = huffman_symbol_alloc((uint8_t *)uv_ac_count, (uint8_t *)uv_ac_sym);

    for (int i = 0; i < 3; i++) {
        d[i] = malloc(sizeof(struct jpg_decoder));
        if (i == 0) {
            init_encoder(p->pic, d[i], i, y_dc, y_ac);
        } else {
            init_encoder(p->pic, d[i], i, uv_dc, uv_ac);
        }
    }

    int16_t Y[64 * 4], U[64], V[64];
    // prepare huffman code table, planar yuv
    uint8_t *yuv = malloc(p->height * p->width * 3 / 2);
    uint8_t *u = yuv + p->height * p->width;
    uint8_t *v = u + p->height * p->width/4;

    //divide and transform in block size, 420
    for (int y = 0; y < p->height / y_stride; y++) {
        for (int x = 0; x < p->width / x_stride; x ++) {
            // including downsample
            BGRA32_to_YUV420((uint8_t *)p->pixels + y * y_stride * p->pitch + x * x_stride, p->pitch,
                             Y, U, V);

            dct_8x8(Y, coeff, 16);
            push_and_quant(hdec, d[0], yuv + (y * p->width / 4 + x * 4) * 64, coeff);
            dct_8x8(Y + 64, coeff, 16);
            push_and_quant(hdec,d[0], yuv + (y * p->width / 4 + x * 4 + 1) * 64, coeff);
            dct_8x8(Y + 128, coeff, 16);
            push_and_quant(hdec, d[0], yuv + (y * p->width / 4 + x * 4 + 2) * 64, coeff);
            dct_8x8(Y + 192, coeff, 16);
            push_and_quant(hdec, d[0], yuv + (y * p->width / 4 + x * 4 + 3) * 64, coeff);

            dct_8x8(U, coeff, 8);
            push_and_quant(hdec, d[1], u + (y * p->width / x_stride + x) * 64,
                           coeff);

            dct_8x8(V, coeff, 8);
            push_and_quant(hdec, d[2], v + (y * p->width / x_stride + x) * 64,
                           coeff);
        }
    }
    ALIGN_BYTE(hdec->v);

    bits_vec_dump(hdec->v);

    for (int i = 0; i < 3; i++) {
        destroy_decoder(d[i]);
    }

    huffman_codec_free(hdec);
}

static struct file_ops jpg_ops = {
    .name = "JPG",
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
