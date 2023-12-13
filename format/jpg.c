#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <arpa/inet.h>

#include "file.h"
#include "jpg.h"
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
    // int16_t buf[64];
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
        VDBG(jpg, "marker 0x%x", c);
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
    tlen = ntohs(tlen);
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
                j->dqt[id].tdata[zigzag[i]] = ntohs(j->dqt[id].tdata[zigzag[i]]);
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
    tlen = ntohs(tlen);
    tlen -= 2;
    while (tlen)
    {
        struct dht d;
        fread(&d, 1, 1, f);
        uint8_t ac = d.table_class;
        uint8_t id = d.huffman_id;
        VDBG(jpg, "ac %d, id %d, %02x\n", ac, id, *(uint8_t *)&d);
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


//decode vec to decoder buf
bool 
decode_data_unit(struct jpg_decoder *d, int16_t buf[64])
{
    int dc, ac;
    int16_t *p = buf;
    huffman_tree *dc_tree = d->dc;
    huffman_tree *ac_tree = d->ac;

    dc = huffman_decode_symbol(dc_tree);
    dc = get_vlc(huffman_read_symbol(dc), dc);
    dc += d->prev_dc;
    d->prev_dc = dc;
    p[0] = dc;

    // read AC coff
    for (int i = 1; i < 64;) {
        ac = huffman_decode_symbol(ac_tree);
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
            p[zigzag[i++]] = 0;
            lead_zero --;
        }

        if (ac) {
            ac = get_vlc(huffman_read_symbol(ac), ac);
            p[zigzag[i++]] = ac;
        }
    }

    for (int i = 0; i < 64; i++) {
        // VINFO(jpg, "%d: %d, quant %d, -> %d", i, p[i], d->quant[i], p[i]* d->quant[i]);
        p[i] *= d->quant[i];
    }
    return true;
}

static int 
init_decoder(JPG* j, struct jpg_decoder *d, uint8_t comp_id)
{
    if (comp_id >= j->sof.components_num)
        return -1;
    huffman_tree * dc_tree = huffman_tree_init();
    huffman_tree * ac_tree = huffman_tree_init();
    int dc_ht_id = j->sos.comps[comp_id].DC_entropy;
    int ac_ht_id = j->sos.comps[comp_id].AC_entropy;
    huffman_build_lookup_table(dc_tree, 0, dc_ht_id, j->dht[0][dc_ht_id].num_codecs, j->dht[0][dc_ht_id].data);
    huffman_build_lookup_table(ac_tree, 1, ac_ht_id, j->dht[1][ac_ht_id].num_codecs, j->dht[1][ac_ht_id].data);

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

void
JPG_decode_image(JPG* j, uint8_t* data, int len) {

    // each component owns a decoder, could be CMYK
    struct jpg_decoder *d[4];

    //components_num is 1 or 3
    for (int i = 0; i < j->sof.components_num; i ++) {
        d[i] = malloc(sizeof(struct jpg_decoder));
        init_decoder(j, d[i], i);
    }

    // huffman_dump_table(d[0]->dc);
    // huffman_dump_table(d[0]->ac);

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
    hexdump(stdout, "jpg raw data", data, 166);
#endif
    huffman_decode_start(data, len);

    int16_t Y[3][64*4], *Cr, *Cb;
    int16_t dummy[64] = {0};
    int16_t buf[64];

    for (int y = 0; y <= height / ystride; y++) {
        //block start
        ptr = j->data + y * bytes_blockline;
        for (int x = 0; x < width; x += xstride) {
            for (uint i = 0; i < j->sof.components_num; ++i) {
                int v = j->sof.colors[i].vertical;
                int h = j->sof.colors[i].horizontal;
                for (int vi = 0; vi < v; vi ++) {
                    for (int hi = 0; hi < h; hi ++) {
                        decode_data_unit(d[i], buf);
                        // idct_float(d[i], &Y[i][64 * vi * h + 8 * hi], h * 8);
                        idct_8x8(buf, &Y[i][64 * vi * h + 8 * hi], h * 8);
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

            YCbCr_to_BGRA32(ptr, pitch, Y[0], Cb, Cr, j->sof.colors[0].vertical,
                            j->sof.colors[0].horizontal);

            if (restarts > 0) {
                restarts --;
                if (restarts == 0) {
                    restarts = j->dri.interval;
                    for (int i = 0; i < j->sof.components_num; i ++) {
                        reset_decoder(d[i]);
                    }
                    huffman_reset_stream();
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

    huffman_decode_end();
}

void 
read_compressed_image(JPG* j, FILE *f)
{
    // int width = ((j->sof.width + 7) >> 3) << 3;
    // int height = ((j->sof.height + 7) >> 3) << 3;
    size_t pos = ftell(f);
    fseek(f, 0, SEEK_END);
    size_t last = ftell(f);
    fseek(f, pos, SEEK_SET);
    uint8_t* compressed = malloc(last - pos);
    uint8_t prev , c = fgetc(f);
    int l = 0;
    VDBG(jpg, "read image from 0x%zx", pos);
    do {
        /* take 0xFF00 as 0xFF */
        /* and skip rst marker */
        prev = c;
        c = fgetc(f);
        if (prev != 0xFF)
            compressed[l++] = prev;
        else if (prev == 0xFF && c == 0xD9) {
            break;
        }
        else if (prev == 0xFF && c == 0) {
            compressed[l++] = 0xFF;
            prev = 0;
            c = fgetc(f);
        } else if (prev == 0xFF && (c >= 0xD0 && c <= 0xD7)) {
            prev = 0;
            c = fgetc(f);
        } else if (prev == 0xFF && c == 0xFF) {

        } else {
            VERR(jpg, "invalid %x %x", prev, c);
        }
    } while(!feof(f));

    j->data = compressed;
    j->data_len = l;
    fseek(f, -2, SEEK_CUR);
}


void 
read_sos(JPG* j, FILE *f)
{
    fread(&j->sos, 3, 1, f);
    for (int i = 0; i < j->sos.nums; i ++) {
        fread(&j->sos.comps[i], sizeof(struct comp_sel), 1, f);
    }
    uint8_t c = fgetc(f);
    j->sos.predictor_start = c;
    c = fgetc(f);
    j->sos.predictor_end = c;
    c = fgetc(f);
    j->sos.approx_bits_h = c >> 4;
    j->sos.approx_bits_l = c & 0xF;
    read_compressed_image(j, f);
}

static struct pic* 
JPG_load(const char *filename)
{
    struct pic *p = pic_alloc(sizeof(JPG));
    JPG *j = p->pic;
    j->data = NULL;
    FILE *f = fopen(filename, "rb");
    uint16_t soi, m, len;
    fread(&soi, 2, 1, f);
    m = read_marker_skip_null(f);
    //0xFFFF means eof
    while (m != EOI && m != 0 && m!= 0xFFFF) {

        VDBG(jpg, "offset at 0x%zx", ftell(f));
        switch(m) {
            case SOF0:
                fread(&j->sof, 8, 1, f);
                j->sof.len = ntohs(j->sof.len);
                j->sof.height = ntohs(j->sof.height);
                j->sof.width = ntohs(j->sof.width);
                j->sof.colors = malloc(j->sof.components_num * sizeof(struct jpg_component));
                fread(j->sof.colors, sizeof(struct jpg_component), j->sof.components_num, f);
                break;
            case APP0:
                fread(&j->app0, 16, 1, f);
                if (j->app0.xthumbnail*j->app0.ythumbnail) {
                    j->app0.data = malloc(3*j->app0.xthumbnail*j->app0.ythumbnail);
                    fread(j->app0.data, 3, j->app0.xthumbnail*j->app0.ythumbnail, f);
                }
                break;
            case DHT:
                read_dht(j, f);
                break;
            case DQT:
                read_dqt(j, f);
                break;
            case SOS:
                VDBG(jpg, "sos");
                read_sos(j, f);
                break;
            case COM:
                fread(&j->comment, 2, 1, f);
                j->comment.len = ntohs(j->comment.len);
                j->comment.data = malloc(j->comment.len-2);
                fread(j->comment.data, j->comment.len-2, 1, f);
                break;
            case DRI:
                fread(&j->dri, sizeof(struct dri ), 1, f);
                j->dri.interval = ntohs(j->dri.interval);
                break;
            case APP1:
                VDBG(jpg, "app1 exif");
            default:
                VDBG(jpg, "marker %x", ntohs(m));
                fread(&len, 2, 1, f);
                len = ntohs(len);
                fseek(f, len-2, SEEK_CUR);
                break;
        }
        m = read_marker_skip_null(f);
    }

    fclose(f);
    uint8_t *compressed = j->data;
    j->data = NULL;
    JPG_decode_image(j, compressed, j->data_len);
    free(compressed);
    p->width = ((j->sof.width + 7) >> 3) << 3;
    p->height = j->sof.height;// ((j->sof.height + 7) >> 3) << 3;
    p->depth = 32;
    p->pitch = ((p->width * 32 + 32 - 1) >> 5) << 2;
    p->format = CS_PIXELFORMAT_RGB888;
    p->pixels = j->data;

    return p;
}

static void 
JPG_free(struct pic *p)
{
    JPG *j = (JPG *)p->pic;
    if (j->comment.data)
        free(j->comment.data);
    if (j->app0.data)
        free(j->app0.data);
    if (j->sof.colors)
        free(j->sof.colors);
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
                fprintf(f, "%s DHT %d: ", (j->dht[ac][i].table_class == 0 ? "DC" : "AC"), i);
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


static struct file_ops jpg_ops = {
    .name = "JPG",
    .probe = JPG_probe,
    .load = JPG_load,
    .free = JPG_free,
    .info = JPG_info,
};

void 
JPG_init(void)
{
    file_ops_register(&jpg_ops);
}
