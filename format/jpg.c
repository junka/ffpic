#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <arpa/inet.h>

#include "file.h"
#include "jpg.h"
#include "utils.h"

#include "huffman.h"

struct decoder {
    int prev_dc;
    huffman_tree *dc;
    huffman_tree *ac;
    uint16_t *quant;
    int buf[64];
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
        printf("fail to open %s\n", filename);
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
        printf("marker 0x%x\n", c);
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
            printf("invalid dqt id\n");
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
        // printf("ac %d, id %d, %02x\n", ac, id, *(uint8_t *)&d);
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


static uint8_t
clamp(int i)
{
	if (i < 0)
		return 0;
	else if (i > 255)
		return 255;
	else
		return i;
}

static inline uint8_t
descale_and_clamp(int x, int shift)
{
    x += (1UL << (shift - 1));
    if (x < 0)
        x = (x >> shift) | ((~(0UL)) << (32 - (shift)));
    else
        x >>= shift;
    x += 128;
    if (x > 255)
        return 255;
    else if (x < 0)
        return 0;
    else
        return x;
}

/* keep it slow, but high quality */
void
idct_float(struct decoder *d, int *output, int stride)
{
    const float m0 = 2.0 * cos(1.0 / 16.0 * 2.0 * M_PI);
    const float m1 = 2.0 * cos(2.0 / 16.0 * 2.0 * M_PI);
    const float m3 = 2.0 * cos(2.0 / 16.0 * 2.0 * M_PI);
    const float m5 = 2.0 * cos(3.0 / 16.0 * 2.0 * M_PI);
    const float m2 = m0 - m5;
    const float m4 = m0 + m5;
    const float s0 = cos(0.0 / 16.0 * M_PI) / sqrt(8);
    const float s1 = cos(1.0 / 16.0 * M_PI) / 2.0;
    const float s2 = cos(2.0 / 16.0 * M_PI) / 2.0;
    const float s3 = cos(3.0 / 16.0 * M_PI) / 2.0;
    const float s4 = cos(4.0 / 16.0 * M_PI) / 2.0;
    const float s5 = cos(5.0 / 16.0 * M_PI) / 2.0;
    const float s6 = cos(6.0 / 16.0 * M_PI) / 2.0;
    const float s7 = cos(7.0 / 16.0 * M_PI) / 2.0;
    
    int *inptr;
    float *wsptr;
    int *outptr;
    int ctr;
    float workspace[64]; /* buffers data between passes */

    /* Pass 1: process columns from input, store into work array. */
    inptr = d->buf;

    wsptr = workspace;
    for (ctr = 8; ctr > 0; ctr--) {
        const float g0 = inptr[0 * 8] * s0;
        const float g1 = inptr[4 * 8] * s4;
        const float g2 = inptr[2 * 8] * s2;
        const float g3 = inptr[6 * 8] * s6;
        const float g4 = inptr[5 * 8] * s5;
        const float g5 = inptr[1 * 8] * s1;
        const float g6 = inptr[7 * 8] * s7;
        const float g7 = inptr[3 * 8] * s3;

        /* Even part */
        const float f0 = g0;
        const float f1 = g1;
        const float f2 = g2;
        const float f3 = g3;
        const float f4 = g4 - g7;
        const float f5 = g5 + g6;
        const float f6 = g5 - g6;
        const float f7 = g4 + g7;

        const float e0 = f0;
        const float e1 = f1;
        const float e2 = f2 - f3;
        const float e3 = f2 + f3;
        const float e4 = f4;
        const float e5 = f5 - f7;
        const float e6 = f6;
        const float e7 = f5 + f7;
        const float e8 = f4 + f6;

        const float d0 = e0;
        const float d1 = e1;
        const float d2 = e2 * m1;
        const float d3 = e3;
        const float d4 = e4 * m2;
        const float d5 = e5 * m3;
        const float d6 = e6 * m4;
        const float d7 = e7;
        const float d8 = e8 * m5;

        const float c0 = d0 + d1;
        const float c1 = d0 - d1;
        const float c2 = d2 - d3;
        const float c3 = d3;
        const float c4 = d4 + d8;
        const float c5 = d5 + d7;
        const float c6 = d6 - d8;
        const float c7 = d7;
        const float c8 = c5 - c6;

        const float b0 = c0 + c3;
        const float b1 = c1 + c2;
        const float b2 = c1 - c2;
        const float b3 = c0 - c3;
        const float b4 = c4 - c8;
        const float b5 = c8;
        const float b6 = c6 - c7;
        const float b7 = c7;

        wsptr[0 * 8] = b0 + b7;
        wsptr[1 * 8] = b1 + b6;
        wsptr[2 * 8] = b2 + b5;
        wsptr[3 * 8] = b3 + b4;
        wsptr[4 * 8] = b3 - b4;
        wsptr[5 * 8] = b2 - b5;
        wsptr[6 * 8] = b1 - b6;
        wsptr[7 * 8] = b0 - b7;

        inptr++;			/* advance pointers to next column */
        wsptr++;
    }
    
    /* Pass 2: process rows from work array, store into output array. */
    /* Note that we must descale the results by a factor of 8 == 2**3. */

    wsptr = workspace;
    outptr = output;
    for (ctr = 0; ctr < 8; ctr++) {
        /* Rows of zeroes can be exploited in the same way as we did with columns.
        * However, the column calculation has created many nonzero AC terms, so
        * the simplification applies less often (typically 5% to 10% of the time).
        * And testing floats for zero is relatively expensive, so we don't bother.
        */

        /* Even part */
        const float g0 = wsptr[0] * s0;
        const float g1 = wsptr[4] * s4;
        const float g2 = wsptr[2] * s2;
        const float g3 = wsptr[6] * s6;
        const float g4 = wsptr[5] * s5;
        const float g5 = wsptr[1] * s1;
        const float g6 = wsptr[7] * s7;
        const float g7 = wsptr[3] * s3;

        const float f0 = g0;
        const float f1 = g1;
        const float f2 = g2;
        const float f3 = g3;
        const float f4 = g4 - g7;
        const float f5 = g5 + g6;
        const float f6 = g5 - g6;
        const float f7 = g4 + g7;

        const float e0 = f0;
        const float e1 = f1;
        const float e2 = f2 - f3;
        const float e3 = f2 + f3;
        const float e4 = f4;
        const float e5 = f5 - f7;
        const float e6 = f6;
        const float e7 = f5 + f7;
        const float e8 = f4 + f6;

        const float d0 = e0;
        const float d1 = e1;
        const float d2 = e2 * m1;
        const float d3 = e3;
        const float d4 = e4 * m2;
        const float d5 = e5 * m3;
        const float d6 = e6 * m4;
        const float d7 = e7;
        const float d8 = e8 * m5;

        const float c0 = d0 + d1;
        const float c1 = d0 - d1;
        const float c2 = d2 - d3;
        const float c3 = d3;
        const float c4 = d4 + d8;
        const float c5 = d5 + d7;
        const float c6 = d6 - d8;
        const float c7 = d7;
        const float c8 = c5 - c6;

        const float b0 = c0 + c3;
        const float b1 = c1 + c2;
        const float b2 = c1 - c2;
        const float b3 = c0 - c3;
        const float b4 = c4 - c8;
        const float b5 = c8;
        const float b6 = c6 - c7;
        const float b7 = c7;

        /* Final output stage:
         No scale down for quality
         and range-limit */
        outptr[0] = descale_and_clamp((int)(b0 + b7), 0);
        outptr[7] = descale_and_clamp((int)(b0 - b7), 0);
        outptr[1] = descale_and_clamp((int)(b1 + b6), 0);
        outptr[6] = descale_and_clamp((int)(b1 - b6), 0);
        outptr[2] = descale_and_clamp((int)(b2 + b5), 0);
        outptr[5] = descale_and_clamp((int)(b2 - b5), 0);
        outptr[4] = descale_and_clamp((int)(b3 + b4), 0);
        outptr[3] = descale_and_clamp((int)(b3 - b4), 0);
        
        wsptr += 8;		/* advance pointer to next row */
        outptr += stride;
    }
}

//decode vec to decoder buf
bool 
decode_data_unit(struct decoder *d)
{
    int dc, ac;
    int *p = d->buf;
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
            printf("invalid ac value\n");
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
		p[i] *= d->quant[i];
	}
    return true;
}

int 
init_decoder(JPG* j, struct decoder *d, uint8_t comp_id)
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

void 
reset_decoder(struct decoder *d)
{
    d->prev_dc = 0;
}

void
destroy_decoder(struct decoder *d)
{
    huffman_cleanup(d->dc);
    huffman_cleanup(d->ac);
    d->quant = NULL;
    free(d);
}


#define SCALEBITS       10
#define ONE_HALF        (1UL << (SCALEBITS - 1))
#define FIX(x)          ((int)((x) * (1UL<<SCALEBITS) + 0.5))

static void
YCbCr_to_BGRA32(JPG *j, int* Y, int* Cb, int* Cr, uint8_t* ptr)
{
#if 0
    fprintf(stdout, "YCbCr :\n");
    for (int i = 0; i < 16; i ++) {
        for (int j = 0; j < 16; j ++) {
            fprintf(stdout, "%04x ", Y[i*16 + j]);
        }
        fprintf(stdout, "\n");
    }
#endif
    uint8_t *p, *p2;
    int offset_to_next_row;
    int width = ((j->sof.width + 7) >> 3) << 3;
    int pitch = width * 4;

    int v = j->sof.colors[0].vertical;
    int h = j->sof.colors[0].horizontal;
    p = ptr;
    if (v == 2)
        p2 = ptr + pitch;
    offset_to_next_row = pitch * v - 8 * h * 4;

    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < 8; k++) {
            int y, cb, cr;
            int add_r, add_g, add_b;
            int r, g , b;
            
            //all YCbCr have been added by 128 in idct
            cb = *Cb++ - 128;
            cr = *Cr++ - 128;
            add_r =                      FIX(1.40200) * cr  + ONE_HALF;
            add_g = -FIX(0.34414) * cb - FIX(0.71414) * cr  + ONE_HALF;
            add_b = FIX(1.77200) * cb + ONE_HALF;
            for (int i = 0; i < h; i ++) {
                y = (*Y++) << SCALEBITS;
                b = (y + add_b) >> SCALEBITS;
                g = (y + add_g) >> SCALEBITS;
                r = (y + add_r) >> SCALEBITS;
                *p++ = clamp(b);
                *p++ = clamp(g);
                *p++ = clamp(r);
                *p++ = 0xff;    //alpha
            }
            if (v == 2) {
                for (int j = 0; j < h; j ++) {
                    y = (Y[8 * h - 1*h + j]) << SCALEBITS;
                    b = (y + add_b) >> SCALEBITS;
                    g = (y + add_g) >> SCALEBITS;
                    r = (y + add_r) >> SCALEBITS;
                    *p2++ = clamp(b);
                    *p2++ = clamp(g);
                    *p2++ = clamp(r);
                    *p2++ = 0xff;
                }
            }
        }
        if (v == 2)
            Y += 8 * h;
        p += offset_to_next_row;
        if (v == 2)
            p2 += offset_to_next_row;
    }
}


static int 
read_next_rst_marker(struct decoder *d)
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
            printf("wrong boudary\n");
            return -1;
        }

        marker = m;
        if ((0xD0 + d->last_rst_marker_seen) == (marker)) {
            rst_marker_found = 1;
            printf("reset marker found\n");
        } else if ((marker) >= 0xD0 && MARKER(marker) <= 0xD7)
            printf("Wrong Reset marker found, abording\n");
        else if (MARKER(marker) == EOI)
            return 0;
    }

    d->last_rst_marker_seen++;
    d->last_rst_marker_seen &= 7;

    return 0;
}

void
JPG_decode_image(JPG* j, uint8_t* data, int len) {

    // each component owns a decoder
    struct decoder *d[4];
    int yn = j->sof.colors[0].horizontal * j->sof.colors[0].vertical;

    //components_num is 1 or 3
    for (int i = 0; i < j->sof.components_num; i ++) {
        d[i] = malloc(sizeof(struct decoder));
        init_decoder(j, d[i], i);
    }

    // huffman_dump_table(d[0]->dc);
    // huffman_dump_table(d[0]->ac);

    //stride value from dc
    int ystride = j->sof.colors[0].vertical * 8;  //means lines per mcu
    int xstride = j->sof.colors[0].horizontal * 8;  //means rows per mcu

    uint8_t *ptr;
    int width = ((j->sof.width + 7) >> 3) << 3; //algin to 8
    int height = ((j->sof.height + 7) >> 3) << 3;
    int pitch = width * 4;
    int bytes_blockline = pitch * ystride;
    int bytes_mcu = xstride * 4;

    int restarts = j->dri.interval;

    j->data = malloc(pitch * height);
#if 0
    hexdump(stdout, "jpg raw data", data, 166);
#endif
    huffman_decode_start(data, len);

    int Y[3][64*4], *Cr, *Cb;
    int dummy[64] = {0};

    for (int y = 0; y < height / ystride; y++) {
        //block start
        ptr = j->data + y * bytes_blockline;
        for (int x = 0; x < width; x += xstride) {
            for (uint i = 0; i < j->sof.components_num; ++i) {
                int v = j->sof.colors[i].vertical;
                int h = j->sof.colors[i].horizontal;
                for (int vi = 0; vi < v; vi ++) {
                    for (int hi = 0; hi < h; hi ++) {
                        decode_data_unit(d[i]);
                        idct_float(d[i], &Y[i][64 * vi * h + 8 * hi], h * 8);
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

            YCbCr_to_BGRA32(j, Y[0], Cb, Cr, ptr);

            if (restarts > 0) {
                restarts --;
                if (restarts == 0) {
                    restarts = j->dri.interval;
                    for (int i = 0; i < j->sof.components_num; i ++) {
                        reset_decoder(d[i]);
                    }
                    huffman_reset_stream();
                    //read_next_rst_marker(d[0]);
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
#if 1
    //clear padding color or to background
    for (int i = 0; i < height; i++) {
        for (int k = j->sof.width; k < width; k++) {
            j->data[i *pitch + k * 4] = 0xFF;
            j->data[i *pitch + k * 4 + 1] = 0xFF;
            j->data[i *pitch + k * 4 + 2] = 0xFF;
            j->data[i *pitch + k * 4 + 3] = 0xFF;
        }
    }
    for (int i = j->sof.height; i < height; i++) {
        for (int k = 0; k < j->sof.width; k++) {
            j->data[i *pitch + k * 4] = 0xFF;
            j->data[i *pitch + k * 4 + 1] = 0xFF;
            j->data[i *pitch + k * 4 + 2] = 0xFF;
            j->data[i *pitch + k * 4 + 3] = 0xFF;
        }
    }
#endif
}

void 
read_compressed_image(JPG* j, FILE *f)
{
    int width = ((j->sof.width + 7) >> 3) << 3;
    int height = ((j->sof.height + 7) >> 3) << 3;
    size_t pos = ftell(f);
    fseek(f, 0, SEEK_END);
    size_t last = ftell(f);
    fseek(f, pos, SEEK_SET);
    uint8_t* compressed = malloc(last - pos);
    uint8_t prev , c = fgetc(f);
    int l = 0;
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
        } else if (prev == 0xFF && (c >= 0xD0 || c <= 0xD7)) {
            prev = 0;
            c = fgetc(f);
        } else if (prev == 0xFF && c == 0xFF) {

        } else {
            printf("invalid %x %x\n", prev, c);
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
    struct pic *p = calloc(1, sizeof(struct pic));
    JPG *j = calloc(1, sizeof(JPG));
    j->data = NULL;
    p->pic = j;
    FILE *f = fopen(filename, "rb");
    uint16_t soi, m, len;
    fread(&soi, 2, 1, f);
    m = read_marker_skip_null(f);
    //0xFFFF means eof
    while (m != EOI && m != 0 && m!= 0xFFFF) {
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
                printf("app1 exif\n");
            default:
                printf("marker %x\n", ntohs(m));
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
    p->height = ((j->sof.height + 7) >> 3) << 3;
    p->depth = 32;
    p->pitch = ((p->width * 32 + 32 - 1) >> 5) << 2;
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
    free(j);
    free(p);
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
                    printf("\t\t");
                }
                fprintf(f, "%d ", j->dqt[i].tdata[k]);
                if (((k + 1) % 8) == 0) {
                    printf("\n");
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
                int len = 0, n = 0;
                for (int k = 0; k < 16; k ++) {
                    fprintf(f, "%02d ",  j->dht[ac][i].num_codecs[k]);
                    len += j->dht[ac][i].num_codecs[k];
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
