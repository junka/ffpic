#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/cdefs.h>

#include "colorspace.h"
#include "crc.h"
#include "deflate.h"
#include "file.h"
#include "png.h"
#include "queue.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(png, INFO)

#define CRC_ASSER(a, b) b = SWAP(b);  assert(a == b);

static int 
PNG_probe(const char* filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(png, "fail to open %s", filename);
        return -ENOENT;
    }
    struct png_file_header sig;
    FFREAD(&sig, sizeof(struct png_file_header), 1, f);

    fclose(f);
    const uint8_t png_signature[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if (!memcmp(&sig, png_signature, sizeof(png_signature))){
        return 0;
    }

    return -EINVAL;
}


int 
read_char_till_null(FILE *f, uint8_t *buff, int len)
{
    int n = 0;
    uint8_t t;
    while (((t = fgetc(f)) != '\0' ) || n >= len-1) {
        buff[n ++] = t;
    }
    buff[n ++] = '\0';
    return n;
}

static int 
calc_png_bits_per_pixel(PNG *p)
{
    int color_len [] = {
        1, 0, 3, 1, 2, 0, 4
    };
    
    // switch(p->ihdr.color_type) {
    //     case GREYSCALE:
    //         color_len = 1;
    //         break;
    //     case TRUECOLOR:
    //         color_len = 3;
    //         break;
    //     case GREYSCALE_ALPHA:
    //         color_len = 2;
    //         break;
    //     case TRUECOLOR_ALPHA:
    //         color_len = 4;
    //         break;
    //     case INDEXEDCOLOR:
    //         color_len = 1;
    //         break;
    //     default:
    //         color_len = 0;
    //         break;
    // }
    return p->ihdr.bit_depth * color_len[p->ihdr.color_type];
}

static int 
calc_image_raw_size(PNG *p)
{
    return p->ihdr.height * (((p->ihdr.width + 3)>>2)<<2) * calc_png_bits_per_pixel(p)/8 + p->ihdr.height;
}

/*Paeth predicter, used by PNG filter type 4*/
static int 
paeth_predictor(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc)
        return a;
    else if (pb <= pc)
        return b;
    else
        return c;
}

static void 
unfilter_scanline(uint8_t *recon, const uint8_t *scanline, 
        const uint8_t *precon, unsigned long bytewidth, 
        uint8_t filterType, unsigned long length)
{
    /*
       For PNG filter method 0
       unfilter a PNG image scanline by scanline. 
       when the pixels are smaller than 1 byte, the filter works byte per byte (bytewidth = 1)
       precon is the previous unfiltered scanline, recon the result, scanline the current one
       the incoming scanlines do NOT include the filtertype byte, that one is given in the parameter filterType instead
       recon and scanline MAY be the same memory address! precon must be disjoint.
     */

    unsigned long i;
    switch (filterType) {
    case FILTER_NONE:
        for (i = 0; i < length; i++)
            recon[i] = scanline[i];
        break;
    case FILTER_SUB:
        for (i = 0; i < bytewidth; i++)
            recon[i] = scanline[i];
        for (i = bytewidth; i < length; i++)
            recon[i] = scanline[i] + recon[i - bytewidth];
        break;
    case FILTER_UP:
        if (precon)
            for (i = 0; i < length; i++)
                recon[i] = scanline[i] + precon[i];
        else
            for (i = 0; i < length; i++)
                recon[i] = scanline[i];
        break;
    case FILTER_AVERAGE:
        if (precon) {
            for (i = 0; i < bytewidth; i++)
                recon[i] = scanline[i] + precon[i] / 2;
            for (i = bytewidth; i < length; i++)
                recon[i] = scanline[i] + ((recon[i - bytewidth] + precon[i]) / 2);
        } else {
            for (i = 0; i < bytewidth; i++)
                recon[i] = scanline[i];
            for (i = bytewidth; i < length; i++)
                recon[i] = scanline[i] + recon[i - bytewidth] / 2;
        }
        break;
    case FILTER_PAETH:
        if (precon) {
            for (i = 0; i < bytewidth; i++)
                recon[i] = (uint8_t)(scanline[i] + paeth_predictor(0, precon[i], 0));
            for (i = bytewidth; i < length; i++)
                recon[i] = (uint8_t)(scanline[i] + paeth_predictor(recon[i - bytewidth], precon[i], precon[i - bytewidth]));
        } else {
            for (i = 0; i < bytewidth; i++)
                recon[i] = scanline[i];
            for (i = bytewidth; i < length; i++)
                recon[i] = (uint8_t)(scanline[i] + paeth_predictor(recon[i - bytewidth], 0, 0));
        }
        break;
    default:
        break;
    }
}

static void 
remove_padding_bits(uint8_t *out, const uint8_t *in, 
            unsigned long olinebits, unsigned long ilinebits, unsigned h)
{
    /*
       After filtering there are still padding bpp if scanlines have non multiple of 8 bit amounts. They need to be removed (except at last scanline of (Adam7-reduced) image) before working with pure image buffers for the Adam7 code, the color convert code and the output to the user.
       in and out are allowed to be the same buffer, in may also be higher but still overlapping; in must have >= ilinebits*h bpp, out must have >= olinebits*h bpp, olinebits must be <= ilinebits
       also used to move bpp after earlier such operations happened, e.g. in a sequence of reduced images from Adam7
       only useful if (ilinebits - olinebits) is a value in the range 1..7
     */
    unsigned y;
    unsigned long diff = ilinebits - olinebits;
    unsigned long obp = 0, ibp = 0;    /*bit pointers */
    for (y = 0; y < h; y++) {
        unsigned long x;
        for (x = 0; x < olinebits; x++) {
            uint8_t bit = (uint8_t)((in[(ibp) >> 3] >> (7 - ((ibp) & 0x7))) & 1);
            ibp++;

            if (bit == 0)
                out[(obp) >> 3] &= (uint8_t)(~(1 << (7 - ((obp) & 0x7))));
            else
                out[(obp) >> 3] |= (1 << (7 - ((obp) & 0x7)));
            ++obp;
        }
        ibp += diff;
    }
}

static void
PNG_unfilter(PNG *p, const uint8_t *buf, int size)
{
    // uint8_t type = *buf;
    int depth = calc_png_bits_per_pixel(p);
    int bytewidth = (depth + 7) / 8;    /*bytewidth is used for filtering, is 1 when depth < 8, number of bytes per pixel otherwise */
    int pitch = (p->ihdr.width * depth + 7) / 8;

    uint8_t *prevline = 0;
    assert((1 + pitch) * p->ihdr.height <= (uint32_t)size);

    for (uint32_t y = 0; y < p->ihdr.height; y++) {
        int outindex = pitch * y;
        int pos = (1 + pitch) * y;    /*the extra filterbyte added to each row */
        uint8_t filterType = buf[pos];
        unfilter_scanline(&p->data[outindex], &buf[pos + 1], prevline, bytewidth, filterType, pitch);
        prevline = &p->data[outindex];
    }

    if (bytewidth == 1 && (int)p->ihdr.width * depth != (pitch * 8)) {
        //means get padding per line
        remove_padding_bits(p->data, buf, p->ihdr.width * depth, pitch * 8,
                            p->ihdr.height);
    }
}

static uint32_t
read_ihdr(PNG *b, FILE *f, uint32_t crc32)
{
    FFREAD(&b->ihdr, 1, sizeof(struct png_ihdr), f);
    crc32 = update_crc(crc32, (uint8_t *)&b->ihdr, sizeof(struct png_ihdr));
    b->ihdr.width = SWAP(b->ihdr.width);
    b->ihdr.height = SWAP(b->ihdr.height);
    return crc32;
}

static uint32_t
read_plte(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    b->palette = malloc(length);
    FFREAD(b->palette, sizeof(struct color), length / sizeof(struct color), f);
    crc32 = update_crc(crc32, (uint8_t *)b->palette, length);
    return crc32;
}

static uint32_t
read_gama(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    FFREAD(&b->gamma, sizeof(uint32_t), 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&b->gamma, length);
    b->gamma = SWAP(b->gamma);
    return crc32;
}

static uint32_t
read_iccp(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80];
    uint8_t i = 0;
    i = read_char_till_null(f, keybuff, 80);
    b->icc.name = malloc(i);
    memcpy(b->icc.name, keybuff, i);
    crc32 = update_crc(crc32, (uint8_t *)b->icc.name, i);
    b->icc.compression_method = fgetc(f);
    crc32 = update_crc(crc32, (uint8_t *)&b->icc.compression_method, 1);
    b->icc.compression_profile = malloc(length - i - 1);
    FFREAD(&b->icc.compression_profile, length - i - 1, 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&b->icc.compression_profile,
                       length - i - 1);
    return crc32;
}

static uint32_t
read_chrm(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    FFREAD(&b->cwp, sizeof(struct chromaticities_white_point), 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&b->cwp, length);
    b->cwp.white_x = SWAP(b->cwp.white_x);
    b->cwp.white_y = SWAP(b->cwp.white_y);
    b->cwp.red_x = SWAP(b->cwp.red_x);
    b->cwp.red_y = SWAP(b->cwp.red_y);
    b->cwp.green_x = SWAP(b->cwp.green_x);
    b->cwp.green_y = SWAP(b->cwp.green_y);
    b->cwp.blue_x = SWAP(b->cwp.blue_x);
    b->cwp.blue_y = SWAP(b->cwp.blue_y);
    return crc32;
}

static uint32_t
read_text(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80];
    uint8_t i = 0;
    i = read_char_till_null(f, keybuff, 80);
    crc32 = update_crc(crc32, (uint8_t *)keybuff, i);
    if (b->n_text++) {
        b->textual =
            realloc(b->textual, b->n_text * sizeof(struct textual_data));
        (b->textual + b->n_text - 1)->keyword = malloc(i);
        memcpy((b->textual + b->n_text - 1)->keyword, keybuff, i);
        if (length - i) {
            (b->textual + b->n_text - 1)->text = malloc(length - i);
            FFREAD((b->textual + b->n_text - 1)->text, 1, length - i, f);
            crc32 =
                update_crc(crc32, (uint8_t *)(b->textual + b->n_text - 1)->text,
                           length - i);
        } else {
            (b->textual + b->n_text - 1)->text = NULL;
        }
    } else {
        b->textual = malloc(sizeof(struct textual_data));
        b->textual->keyword = malloc(i);
        memcpy(b->textual->keyword, keybuff, i);
        if (length - i) {
            b->textual->text = malloc(length - i);
            FFREAD(b->textual->text, 1, length - i, f);
            crc32 = update_crc(crc32, (uint8_t *)b->textual->text, length - i);
        } else {
            b->textual->text = NULL;
        }
    }
    return crc32;
}

static uint32_t
read_itxt(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80];
    uint8_t i = 0, j = 0, k = 0;
    i = read_char_till_null(f, keybuff, 80);
    crc32 = update_crc(crc32, (uint8_t *)keybuff, i);
    if (b->n_itext++) {
        b->itextual = realloc(
            b->itextual, b->n_text * sizeof(struct international_textual_data));
        (b->itextual + b->n_itext - 1)->keyword = malloc(i);
        memcpy((b->itextual + b->n_itext - 1)->keyword, keybuff, i);
        (b->itextual + b->n_itext - 1)->compression_flag = fgetc(f);
        (b->itextual + b->n_itext - 1)->compression_method = fgetc(f);
        j = read_char_till_null(f, keybuff, 80);
        crc32 = update_crc(crc32, (uint8_t *)keybuff, j);
        if (j <= 1) {
            (b->itextual + b->n_itext - 1)->language_tag = NULL;
        } else {
            (b->itextual + b->n_itext - 1)->language_tag = malloc(j);
            memcpy((b->itextual + b->n_itext - 1)->language_tag, keybuff, j);
        }
        k = read_char_till_null(f, keybuff, 80);
        crc32 = update_crc(crc32, (uint8_t *)keybuff, k);
        if (k <= 1) {
            (b->itextual + b->n_itext - 1)->translated_keyword = NULL;
        } else {
            (b->itextual + b->n_itext - 1)->translated_keyword = malloc(k);
            memcpy((b->itextual + b->n_itext - 1)->translated_keyword, keybuff,
                   k);
        }
        if (length - i - j - k) {
            (b->itextual + b->n_itext - 1)->text = malloc(length - i - j - k);
            FFREAD((b->itextual + b->n_itext - 1)->text, 1, length - i - j - k,
                  f);
            crc32 = update_crc(crc32,
                               (uint8_t *)(b->itextual + b->n_itext - 1)->text,
                               length - i - j - k);
        } else {
            (b->itextual + b->n_itext - 1)->text = NULL;
        }
    } else {
        b->itextual = malloc(sizeof(struct international_textual_data));
        b->itextual->keyword = malloc(i);
        memcpy(b->itextual->keyword, keybuff, i);
        b->itextual->compression_flag = fgetc(f);
        b->itextual->compression_method = fgetc(f);
        j = read_char_till_null(f, keybuff, 80);
        crc32 = update_crc(crc32, (uint8_t *)keybuff, j);
        if (j <= 1) {
            b->itextual->language_tag = NULL;
        } else {
            b->itextual->language_tag = malloc(j);
            memcpy(b->itextual->language_tag, keybuff, j);
        }
        k = read_char_till_null(f, keybuff, 80);
        crc32 = update_crc(crc32, (uint8_t *)keybuff, k);
        if (k <= 1) {
            b->itextual->translated_keyword = NULL;
        } else {
            b->itextual->translated_keyword = malloc(k);
            memcpy(b->itextual->translated_keyword, keybuff, k);
        }
        if (length - i - j - k) {
            b->itextual->text = malloc(length - i - j - k);
            FFREAD(b->itextual->text, 1, length - i - j - k, f);
            crc32 = update_crc(crc32, (uint8_t *)b->itextual->text,
                               length - i - j - k);
        } else {
            b->itextual->text = NULL;
        }
    }
    return crc32;
}

static uint32_t
read_ztxt(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80];
    uint8_t i = 0;
    i = read_char_till_null(f, keybuff, 80);
    crc32 = update_crc(crc32, (uint8_t *)keybuff, i);
    if (b->n_ctext) {
        b->n_ctext++;
        b->ctextual = realloc(
            b->ctextual, b->n_ctext * sizeof(struct compressed_textual_data));
        (b->ctextual + b->n_ctext - 1)->keyword = malloc(i);
        memcpy((b->ctextual + b->n_ctext - 1)->keyword, keybuff, i);
        (b->ctextual + b->n_ctext - 1)->compression_method = fgetc(f);
        crc32 = update_crc(
            crc32,
            (uint8_t *)&((b->ctextual + b->n_ctext - 1)->compression_method),
            1);
        if (length - i - 1) {
            (b->ctextual + b->n_ctext - 1)->compressed_text =
                malloc(length - i - 1);
            FFREAD((b->ctextual + b->n_ctext - 1)->compressed_text, 1,
                  length - i - 1, f);
            crc32 = update_crc(
                crc32,
                (uint8_t *)(b->ctextual + b->n_ctext - 1)->compressed_text,
                length - i - 1);
        } else {
            (b->ctextual + b->n_ctext - 1)->compressed_text = NULL;
        }
    } else {
        b->ctextual = malloc(sizeof(struct compressed_textual_data));
        b->ctextual->keyword = malloc(i);
        memcpy(b->ctextual->keyword, keybuff, i);
        b->ctextual->compression_method = fgetc(f);
        crc32 =
            update_crc(crc32, (uint8_t *)&(b->ctextual->compression_method), 1);
        if (length - i - 1) {
            b->ctextual->compressed_text = malloc(length - i - 1);
            FFREAD(b->ctextual->compressed_text, 1, length - i - 1, f);
            crc32 = update_crc(crc32, (uint8_t *)(b->ctextual->compressed_text),
                               length - i - 1);
        } else {
            b->ctextual->compressed_text = NULL;
        }
        b->n_ctext = 1;
    }
    return crc32;
}

static uint32_t
read_hist(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    b->freqs = malloc(length);
    FFREAD(b->freqs, 2, length / 2, f);
    crc32 = update_crc(crc32, (uint8_t *)b->freqs, length);
    return crc32;
}

static uint32_t
read_bkgd(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    VDBG(png, "color %d, length %d", b->ihdr.color_type, length);
    if (b->ihdr.color_type == GREYSCALE ||
        b->ihdr.color_type == GREYSCALE_ALPHA) {
        FFREAD(&b->bcolor, 2, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor, length);
        b->bcolor.greyscale = SWAP(b->bcolor.greyscale);
    } else if (b->ihdr.color_type == TRUECOLOR ||
               b->ihdr.color_type == TRUECOLOR_ALPHA) {
        FFREAD(&b->bcolor, 6, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor, length);
        b->bcolor.rgb.red = SWAP(b->bcolor.rgb.red);
        b->bcolor.rgb.green = SWAP(b->bcolor.rgb.green);
        b->bcolor.rgb.blue = SWAP(b->bcolor.rgb.blue);
    } else if (b->ihdr.color_type == INDEXEDCOLOR) {
        FFREAD(&b->bcolor, 1, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor, length);
    }
    return crc32;
}

static uint32_t
read_time(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    FFREAD(&b->last_mod, sizeof(struct last_modification), 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&(b->last_mod), length);
    return crc32;
}

static uint32_t
read_idat(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    if ((uint32_t)b->compressed_size == 0) {
        b->compressed_size = length;
        b->compressed = malloc(b->compressed_size);
        FFREAD(b->compressed, length, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)b->compressed, length);
    } else {
        b->compressed_size += length;
        b->compressed = realloc(b->compressed, b->compressed_size);
        FFREAD(b->compressed + b->compressed_size - length, length, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)b->compressed + b->compressed_size - length, length);
    }
    return crc32;
}

static int
read_iend(FILE *f)
{
    uint32_t crc32, crc;
    uint32_t chunk_type = (uint32_t)CHARS2UINT("IEND");
    crc32 = init_crc32((uint8_t *)&chunk_type, sizeof(uint32_t));
    FFREAD(&crc, sizeof(uint32_t), 1, f);
    crc32 = finish_crc32(crc32);
    CRC_ASSER(crc32, crc);
    return 0;
}

static struct pic* 
PNG_load(const char* filename, int skip_flag)
{
    struct pic *p = pic_alloc(sizeof(struct PNG));
    PNG * b = p->pic;

    FILE *f = fopen(filename, "rb");
    if (READ_FAIL(&b->sig, sizeof(struct png_file_header), 1, f)) {
        printf("fail to read png file header\n");
        pic_free(p);
        return NULL;
    }

    uint32_t chunk_type = 0;
    uint32_t length;

    uint8_t *data = NULL;
    uint32_t crc32, crc;

    if (READ_FAIL(&length, sizeof(uint32_t), 1, f)) {
        return NULL;
    }
    if (READ_FAIL(&chunk_type, sizeof(uint32_t), 1, f)) {
        return NULL;
    }
    length = SWAP(length);

    while (length && chunk_type != (uint32_t)CHARS2UINT("IEND")) {
        assert(length > 0);
        crc32 = init_crc32((uint8_t*)&chunk_type, sizeof(uint32_t));
        switch (chunk_type) {
            case CHUNK_TYPE_IHDR:
                crc32 = read_ihdr(b, f, crc32);
                break;
            case CHUNK_TYPE_PLTE:
                crc32 = read_plte(b, f, crc32, length);
                break;
            case CHUNK_TYPE_IDAT:
                crc32 = read_idat(b, f, crc32, length);
                break;
            case CHUNK_TYPE_GAMA:
                crc32 = read_gama(b, f, crc32, length);
                break;
            case CHUNK_TYPE_ICCP:
                crc32 = read_iccp(b, f, crc32, length);
                break;
            case CHUNK_TYPE_CHRM:
                crc32 = read_chrm(b, f, crc32, length);
                break;
            case CHUNK_TYPE_TEXT:
                crc32 = read_text(b, f, crc32, length);
                break;
            case CHUNK_TYPE_ITXT:
                crc32 = read_itxt(b, f, crc32, length);
                break;
            case CHUNK_TYPE_ZTXT:
                crc32 = read_ztxt(b, f, crc32, length);
                break;
            case CHUNK_TYPE_HIST:
                crc32 = read_hist(b, f, crc32, length);
                break;
            case CHUNK_TYPE_BKGD:
                crc32 = read_bkgd(b, f, crc32, length);
                break;
            case CHUNK_TYPE_TIME:
                crc32 = read_time(b, f, crc32, length);
                break;
            default:
                if (length) {
                    VDBG(png, "length %d", length);
                    data = calloc(1, length);

                    assert(data);
                    fread(data, length, 1, f);
                    crc32 = update_crc(crc32, (uint8_t*)data, length);
                    free(data);
                }
                break;
        }

        fread(&crc, sizeof(uint32_t), 1, f);
        crc32 = finish_crc32(crc32);
        CRC_ASSER(crc32, crc);
        fread(&length, sizeof(uint32_t), 1, f);
        length = SWAP(length);
        fread(&chunk_type, sizeof(uint32_t), 1, f);
    }
    /* check iEND chunk */
    read_iend(f);
    fclose(f);
    b->size = calc_image_raw_size(b);
    VDBG(png, "compressed size %d, pre allocate %d\n", b->compressed_size, b->size);

    if (!skip_flag) {
        uint8_t* udata = malloc(b->size);
        int a = deflate_decode(b->compressed, b->compressed_size, udata, &b->size);
#if 0
        hexdump(stdout, "png raw data", "", compressed, 32);
        hexdump(stdout, "png decompress data", "", udata, 32);
#endif
        free(b->compressed);
        b->compressed = NULL;
        b->compressed_size = 0;
        b->data = (uint8_t*)malloc(b->size);
        VDBG(png, "ret %d, size %d\n", a, b->size);

        PNG_unfilter(b, udata, b->size);
        free(udata);
    }
    p->width = b->ihdr.width;
    p->height = b->ihdr.height;
    p->depth = calc_png_bits_per_pixel(b);
    p->pixels = b->data;
    p->format = CS_MasksToPixelFormatEnum(p->depth, 0, 0, 0, 0xFF);
    p->pitch = ((b->ihdr.width * p->depth + 31) >> 5) << 2;
    return p;
}

static void 
PNG_free(struct pic* p)
{
    PNG *b = (PNG *)(p->pic);
    if (b->palette)
        free(b->palette);
    if (b->itextual) {
        for (int i = 0; i < b->n_itext; i ++) {
            if (b->itextual[i].keyword) {
                free(b->itextual[i].keyword);
            }
            if (b->itextual[i].text) {
                free(b->itextual[i].text);
            }
        }
        if (b->itextual->language_tag) {
            free(b->itextual->language_tag);
        }
        if (b->itextual->translated_keyword) {
            free(b->itextual->translated_keyword);
        }
        free(b->itextual);
    }
    if (b->textual) {
        for (int i = 0; i < b->n_text; i++) {
            if (b->textual[i].keyword) {
                free(b->textual[i].keyword);
            }
            if (b->textual[i].text) {
                free(b->textual[i].text);
            }
        }
        free(b->textual);
    }
    if (b->ctextual) {
        for (int i = 0; i < b->n_ctext; i++) {
            if (b->ctextual[i].keyword) {
                free(b->ctextual[i].keyword);
            }
            if (b->ctextual[i].compressed_text) {
                free(b->ctextual[i].compressed_text);
            }
        }
        free(b->ctextual);
    }
    if (b->icc.name) {
        free(b->icc.name);
    }
    if (b->icc.compression_profile) {
        free(b->icc.compression_profile);
    }
    if (b->freqs)
        free(b->freqs);
    if (b->data)
        free(b->data);
    pic_free(p);
}

static void 
PNG_info(FILE* f, struct pic* p)
{
    PNG *b = (PNG *)(p->pic);
    fprintf(f, "PNG file format:\n");
    fprintf(f, "\twidth %d, height %d\n", b->ihdr.width, b->ihdr.height);
    fprintf(f, "\tdepth %d, color_type %d\n", b->ihdr.bit_depth, b->ihdr.color_type);
    fprintf(f, "\tcompression %d, filter %d\n", b->ihdr.compression, b->ihdr.filter);
    fprintf(f, "\tinterlace %d\n", b->ihdr.interlace);
    fprintf(f, "----------------------------------\n");
    if (b->n_text) {
        fprintf(f, "\tTextual data:\n");
    }
    for (int i = 0; i < b->n_text; i ++) {
        struct textual_data *t = (b->textual + i);
        fprintf(f, "\t%s:%s\n", t->keyword, t->text);
    }
    if (b->n_ctext) {
        fprintf(f, "\tCompressed Textual data:\n");
    }
    for (int i = 0; i < b->n_ctext; i ++) {
        struct compressed_textual_data *t = (b->ctextual + i);
        fprintf(f, "\t%s:%d:%s\n", t->keyword, t->compression_method, t->compressed_text);
    }
    if (b->n_itext) {
        fprintf(f, "\tInternatianal Textual data:\n");
    }
    for (int i = 0; i < b->n_itext; i ++) {
        struct international_textual_data *t = (b->itextual + i);
        if (t->compression_flag)
            fprintf(f, "\tcompression %d\n", t->compression_method);
        fprintf(f, "\t%s:%s:%s:%s\n", t->keyword, t->language_tag, t->translated_keyword, t->text);
    }
    if (b->ihdr.color_type == GREYSCALE || b->ihdr.color_type == GREYSCALE_ALPHA) {
        fprintf(f, "\tBackgroud color: GRESCALE %d\n", b->bcolor.greyscale);
    } else if (b->ihdr.color_type == TRUECOLOR ||
               b->ihdr.color_type == TRUECOLOR_ALPHA) {
        fprintf(f, "\tBackgroud color: RGB %04x, %04x, %04x\n",
                b->bcolor.rgb.red, b->bcolor.rgb.green, b->bcolor.rgb.blue);
    } else if (b->ihdr.color_type == INDEXEDCOLOR) {
        fprintf(f, "\tBackgroud color: Index %d\n", b->bcolor.palette);
    }

    fprintf(f, "\tHRM data:\n");
    fprintf(f, "\twhite x: %f, y: %f\n", (float)b->cwp.white_x / 100000,
            (float)b->cwp.white_y / 100000);
    fprintf(f, "\tred x: %f, y: %f\n", (float)b->cwp.red_x / 100000,
            (float)b->cwp.red_y / 100000);
    fprintf(f, "\tgreen x: %f, y: %f\n", (float)b->cwp.green_x / 100000,
            (float)b->cwp.green_y / 100000);
    fprintf(f, "\tblue x: %f, y: %f\n", (float)b->cwp.blue_x / 100000,
            (float)b->cwp.blue_y / 100000);

    fprintf(f, "\tGAMA : %f\n", (float)b->gamma / 100000);

    if (b->icc.name) {
        fprintf(f, "\tICC data:\n");
        fprintf(f, "\t%s:%d\n", b->icc.name, b->icc.compression_method);
    }
    if (b->last_mod.year) {
        fprintf(f, "\tLast modification time:\n");
        fprintf(f, "\t%d-%d-%d %d:%d:%d\n", b->last_mod.year, b->last_mod.mon,
            b->last_mod.day, b->last_mod.hour, b->last_mod.min, b->last_mod.sec);
    }
}

static struct file_ops png_ops = {
    .name = "PNG",
    .probe = PNG_probe,
    .load = PNG_load,
    .free = PNG_free,
    .info = PNG_info,
};

void 
PNG_init(void)
{
    file_ops_register(&png_ops);
}
