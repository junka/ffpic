#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/cdefs.h>

#include "basemedia.h"
#include "colorspace.h"
#include "crc.h"
#include "deflate.h"
#include "file.h"
#include "png.h"
#include "queue.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(png, DEBUG)

#define CRC_ASSER(a, b) b = SWAP(b);  assert(a == b);

static int
PNG_probe(FILE *f)
{
    if (f == NULL) {
        return -ENOENT;
    }
    struct png_file_header sig;
    FFREAD(&sig, sizeof(struct png_file_header), 1, f);

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

    if (buff == NULL || len <= 0) {
        return -1;
    }

    for (;;) {
        int c = fgetc(f);
        if (c == EOF) {
            return -1;
        }
        if (c == '\0') {
            break;
        }
        if (n >= len - 1) {
            /* the field is longer than the caller can store: drain it up to
             * the separator and report an error instead of overflowing
             */
            while ((c = fgetc(f)) != EOF && c != '\0') {
            }
            return -1;
        }
        buff[n ++] = (uint8_t)c;
    }
    buff[n] = '\0';
    return n + 1;
}

static int 
calc_png_bits_per_pixel(PNG *p)
{
    int channels;

    switch (p->ihdr.color_type) {
    case GREYSCALE:
    case INDEXEDCOLOR:
        channels = 1;
        break;
    case TRUECOLOR:
        channels = 3;
        break;
    case GREYSCALE_ALPHA:
        channels = 2;
        break;
    case TRUECOLOR_ALPHA:
        channels = 4;
        break;
    default:
        return 0;
    }
    return p->ihdr.bit_depth * channels;
}

static int 
calc_image_raw_size(PNG *p)
{
    int depth = calc_png_bits_per_pixel(p);
    uint64_t raw_pitch, size;

    if (depth <= 0) {
        return -1;
    }
    raw_pitch = ((uint64_t)p->ihdr.width * depth + 7) / 8;
    size = (raw_pitch + 1) * p->ihdr.height;
    if (size > INT_MAX) {
        return -1;
    }
    return (int)size;
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

static int
PNG_unfilter(PNG *p, const uint8_t *buf, int buflen)
{
    int depth = calc_png_bits_per_pixel(p);
    int bytewidth;
    uint64_t raw_pitch, out_pitch, need, out_size;
    uint8_t *prev, *cur;

    if (depth <= 0) {
        return -1;
    }
    bytewidth = (depth + 7) / 8;

    raw_pitch = ((uint64_t)p->ihdr.width * depth + 7) / 8;
    out_pitch = (((uint64_t)p->ihdr.width * depth + 31) >> 5) << 2;
    need = (raw_pitch + 1) * p->ihdr.height;
    out_size = out_pitch * p->ihdr.height;

    if (raw_pitch > INT_MAX || out_pitch > INT_MAX || out_size > INT_MAX ||
        need > (uint64_t)buflen) {
        return -1;
    }

    prev = calloc(1, (size_t)raw_pitch);
    cur = malloc((size_t)raw_pitch);
    if (prev == NULL || cur == NULL) {
        free(prev);
        free(cur);
        return -1;
    }

    for (uint32_t y = 0; y < p->ihdr.height; y++) {
        size_t pos = (size_t)(raw_pitch + 1) * y;
        uint8_t filterType = buf[pos];
        uint8_t *dst = p->data + (size_t)out_pitch * y;

        if (filterType > FILTER_PAETH) {
            free(prev);
            free(cur);
            return -1;
        }
        unfilter_scanline(cur, &buf[pos + 1], prev, bytewidth, filterType,
                          (unsigned long)raw_pitch);
        /* rows handed to the rest of the library are aligned to 4 bytes */
        memset(dst, 0, (size_t)out_pitch);
        memcpy(dst, cur, (size_t)raw_pitch);

        uint8_t *tmp = prev;
        prev = cur;
        cur = tmp;
    }

    free(prev);
    free(cur);
    return 0;
}

static uint32_t
read_ihdr(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    int valid_depth = 0;
    uint8_t bit_depth, color_type, compression, filter, interlace;

    if (length != sizeof(struct png_ihdr) ||
        b->ihdr.width != 0 || b->ihdr.height != 0) {
        VERR(png, "invalid IHDR chunk");
        b->malformed = 1;
        return crc32;
    }

    if (READ_FAIL(&b->ihdr, sizeof(struct png_ihdr), 1, f)) {
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, (uint8_t *)&b->ihdr, sizeof(struct png_ihdr));
    b->ihdr.width = SWAP(b->ihdr.width);
    b->ihdr.height = SWAP(b->ihdr.height);

    bit_depth = b->ihdr.bit_depth;
    color_type = b->ihdr.color_type;
    compression = b->ihdr.compression;
    filter = b->ihdr.filter;
    interlace = b->ihdr.interlace;

    switch (color_type) {
    case GREYSCALE:
        valid_depth = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
                      bit_depth == 8 || bit_depth == 16;
        break;
    case TRUECOLOR:
    case GREYSCALE_ALPHA:
    case TRUECOLOR_ALPHA:
        valid_depth = bit_depth == 8 || bit_depth == 16;
        break;
    case INDEXEDCOLOR:
        valid_depth = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
                      bit_depth == 8;
        break;
    default:
        break;
    }

    if (b->ihdr.width == 0 || b->ihdr.height == 0 ||
        b->ihdr.width > INT_MAX || b->ihdr.height > INT_MAX ||
        !valid_depth || compression != 0 || filter != 0 || interlace > 1) {
        VERR(png, "unsupported IHDR %ux%u depth %d color %d",
             b->ihdr.width, b->ihdr.height, bit_depth, color_type);
        b->malformed = 1;
    } else if (interlace != 0) {
        /* this decoder only implements the non-interlaced layout */
        VERR(png, "interlaced PNG is not supported");
        b->malformed = 1;
    }
    return crc32;
}

static uint32_t
read_plte(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    /* 3 bytes per entry, at most 256 entries */
    if (length == 0 || length % sizeof(struct color) != 0 || length > 3 * 256) {
        VERR(png, "invalid PLTE length %u", length);
        b->malformed = 1;
        return crc32;
    }
    free(b->palette);
    b->palette = malloc(length);
    if (b->palette == NULL ||
        fread(b->palette, 1, length, f) != length) {
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, (uint8_t *)b->palette, (int)length);
    return crc32;
}

static uint32_t
read_gama(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    if (length != sizeof(uint32_t)) {
        VERR(png, "invalid gAMA length %u", length);
        b->malformed = 1;
        return crc32;
    }
    FFREAD(&b->gamma, sizeof(uint32_t), 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&b->gamma, (int)length);
    b->gamma = SWAP(b->gamma);
    return crc32;
}

static struct textual_data *
png_add_text(PNG *b)
{
    struct textual_data *t;

    t = realloc(b->textual, (b->n_text + 1) * sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    b->textual = t;
    t = &b->textual[b->n_text];
    memset(t, 0, sizeof(*t));
    b->n_text++;
    return t;
}

static struct compressed_textual_data *
png_add_ctext(PNG *b)
{
    struct compressed_textual_data *t;

    t = realloc(b->ctextual, (b->n_ctext + 1) * sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    b->ctextual = t;
    t = &b->ctextual[b->n_ctext];
    memset(t, 0, sizeof(*t));
    b->n_ctext++;
    return t;
}

static struct international_textual_data *
png_add_itext(PNG *b)
{
    struct international_textual_data *t;

    t = realloc(b->itextual, (b->n_itext + 1) * sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    b->itextual = t;
    t = &b->itextual[b->n_itext];
    memset(t, 0, sizeof(*t));
    b->n_itext++;
    return t;
}

static uint32_t
read_iccp(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80];
    uint8_t method;
    uint8_t *profile = NULL;
    uint32_t profile_len;
    int i;

    i = read_char_till_null(f, keybuff, 80);
    if (i <= 0 || (uint32_t)i > length) {
        VERR(png, "invalid iCCP profile name");
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, keybuff, i);

    if ((uint32_t)i == length) {
        VERR(png, "iCCP chunk has no compression method");
        b->malformed = 1;
        return crc32;
    }
    method = (uint8_t)fgetc(f);
    if (method != 0) {
        VERR(png, "unsupported iCCP compression method %d", method);
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, &method, 1);

    profile_len = length - (uint32_t)i - 1;
    if (profile_len == 0) {
        VERR(png, "empty iCCP profile");
        b->malformed = 1;
        return crc32;
    }
    profile = malloc(profile_len);
    if (profile == NULL ||
        fread(profile, 1, profile_len, f) != profile_len) {
        free(profile);
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, profile, (int)profile_len);

    free(b->icc.name);
    b->icc.name = NULL;
    free(b->icc.compression_profile);
    b->icc.compression_profile = NULL;
    b->icc.name = malloc((size_t)i);
    if (b->icc.name == NULL) {
        free(profile);
        b->malformed = 1;
        return crc32;
    }
    memcpy(b->icc.name, keybuff, (size_t)i);
    b->icc.compression_method = method;
    b->icc.compression_profile = profile;
    return crc32;
}

static uint32_t
read_chrm(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    if (length != sizeof(struct chromaticities_white_point)) {
        VERR(png, "invalid cHRM length %u", length);
        b->malformed = 1;
        return crc32;
    }
    FFREAD(&b->cwp, sizeof(struct chromaticities_white_point), 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&b->cwp, (int)length);
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
    struct textual_data *t;
    uint32_t text_len;
    int i;

    i = read_char_till_null(f, keybuff, sizeof(keybuff));
    if (i <= 1 || (uint32_t)i > length) {
        VERR(png, "invalid tEXt keyword");
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, keybuff, i);

    t = png_add_text(b);
    if (t == NULL) {
        b->malformed = 1;
        return crc32;
    }
    t->keyword = malloc((size_t)i);
    if (t->keyword == NULL) {
        b->malformed = 1;
        return crc32;
    }
    memcpy(t->keyword, keybuff, (size_t)i);

    text_len = length - (uint32_t)i;
    t->text = malloc((size_t)text_len + 1);
    if (t->text == NULL ||
        fread(t->text, 1, text_len, f) != text_len) {
        b->malformed = 1;
        return crc32;
    }
    t->text[text_len] = '\0';
    crc32 = update_crc(crc32, (uint8_t *)t->text, (int)text_len);
    return crc32;
}

static uint32_t
read_itxt(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80], langbuff[80], transbuff[80];
    struct international_textual_data *t;
    uint8_t compression_flag, compression_method;
    uint32_t text_len, used;
    int i, j, k;

    i = read_char_till_null(f, keybuff, sizeof(keybuff));
    if (i <= 1 || (uint32_t)i + 2 > length) {
        VERR(png, "invalid iTXt keyword");
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, keybuff, i);

    compression_flag = (uint8_t)fgetc(f);
    compression_method = (uint8_t)fgetc(f);
    crc32 = update_crc(crc32, &compression_flag, 1);
    crc32 = update_crc(crc32, &compression_method, 1);
    if (compression_flag > 1 ||
        (compression_flag == 1 && compression_method != 0)) {
        VERR(png, "unsupported iTXt compression %d:%d", compression_flag,
             compression_method);
        b->malformed = 1;
        return crc32;
    }

    j = read_char_till_null(f, langbuff, sizeof(langbuff));
    if (j < 1 || (uint32_t)i + 2 + (uint32_t)j > length) {
        VERR(png, "invalid iTXt language tag");
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, langbuff, j);

    k = read_char_till_null(f, transbuff, sizeof(transbuff));
    used = (uint32_t)i + 2 + (uint32_t)j + (uint32_t)k;
    if (k < 1 || used > length) {
        VERR(png, "invalid iTXt translated keyword");
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, transbuff, k);

    t = png_add_itext(b);
    if (t == NULL) {
        b->malformed = 1;
        return crc32;
    }
    t->keyword = malloc((size_t)i);
    t->language_tag = malloc((size_t)j);
    t->translated_keyword = malloc((size_t)k);
    text_len = length - used;
    t->text = malloc((size_t)text_len + 1);
    if (t->keyword == NULL || t->language_tag == NULL ||
        t->translated_keyword == NULL || t->text == NULL) {
        b->malformed = 1;
        return crc32;
    }
    memcpy(t->keyword, keybuff, (size_t)i);
    memcpy(t->language_tag, langbuff, (size_t)j);
    memcpy(t->translated_keyword, transbuff, (size_t)k);
    t->compression_flag = compression_flag;
    t->compression_method = compression_method;

    if (fread(t->text, 1, text_len, f) != text_len) {
        b->malformed = 1;
        return crc32;
    }
    t->text[text_len] = '\0';
    crc32 = update_crc(crc32, (uint8_t *)t->text, (int)text_len);
    return crc32;
}

static uint32_t
read_ztxt(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t keybuff[80];
    struct compressed_textual_data *t;
    uint8_t method;
    uint32_t comp_len;
    int i;

    i = read_char_till_null(f, keybuff, sizeof(keybuff));
    if (i <= 1 || (uint32_t)i + 1 > length) {
        VERR(png, "invalid zTXt keyword");
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, keybuff, i);

    method = (uint8_t)fgetc(f);
    if (method != 0) {
        VERR(png, "unsupported zTXt compression method %d", method);
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, &method, 1);

    t = png_add_ctext(b);
    if (t == NULL) {
        b->malformed = 1;
        return crc32;
    }
    t->keyword = malloc((size_t)i);
    if (t->keyword == NULL) {
        b->malformed = 1;
        return crc32;
    }
    memcpy(t->keyword, keybuff, (size_t)i);
    t->compression_method = method;

    comp_len = length - (uint32_t)i - 1;
    t->compressed_text = malloc((size_t)comp_len + 1);
    if (t->compressed_text == NULL ||
        fread(t->compressed_text, 1, comp_len, f) != comp_len) {
        b->malformed = 1;
        return crc32;
    }
    t->compressed_text[comp_len] = '\0';
    crc32 = update_crc(crc32, (uint8_t *)t->compressed_text, (int)comp_len);
    return crc32;
}

static uint32_t
read_hist(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    if (length == 0 || length % 2 != 0 || length > 2 * 256) {
        VERR(png, "invalid hIST length %u", length);
        b->malformed = 1;
        return crc32;
    }
    free(b->freqs);
    b->freqs = malloc(length);
    if (b->freqs == NULL ||
        fread(b->freqs, 1, length, f) != length) {
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, (uint8_t *)b->freqs, (int)length);
    return crc32;
}

static uint32_t
read_bkgd(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    VDBG(png, "color %d, length %d", b->ihdr.color_type, length);
    if (b->ihdr.color_type == GREYSCALE ||
        b->ihdr.color_type == GREYSCALE_ALPHA) {
        if (length != 2) {
            goto malformed;
        }
        FFREAD(&b->bcolor.greyscale, 2, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor.greyscale, 2);
        b->bcolor.greyscale = SWAP(b->bcolor.greyscale);
    } else if (b->ihdr.color_type == TRUECOLOR ||
               b->ihdr.color_type == TRUECOLOR_ALPHA) {
        if (length != 6) {
            goto malformed;
        }
        FFREAD(&b->bcolor.red, 2, 1, f);
        FFREAD(&b->bcolor.green, 2, 1, f);
        FFREAD(&b->bcolor.blue, 2, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor.red, 2);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor.green, 2);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor.blue, 2);
        b->bcolor.red = SWAP(b->bcolor.red);
        b->bcolor.green = SWAP(b->bcolor.green);
        b->bcolor.blue = SWAP(b->bcolor.blue);
    } else if (b->ihdr.color_type == INDEXEDCOLOR) {
        if (length != 1) {
            goto malformed;
        }
        FFREAD(&b->bcolor.palette, 1, 1, f);
        crc32 = update_crc(crc32, (uint8_t *)&b->bcolor.palette, 1);
    }
    return crc32;

malformed:
    VERR(png, "invalid bKGD length %u", length);
    b->malformed = 1;
    return crc32;
}

static uint32_t
read_time(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    if (length != sizeof(struct last_modification)) {
        VERR(png, "invalid tIME length %u", length);
        b->malformed = 1;
        return crc32;
    }
    FFREAD(&b->last_mod, sizeof(struct last_modification), 1, f);
    crc32 = update_crc(crc32, (uint8_t *)&(b->last_mod), (int)length);
    return crc32;
}

static uint32_t
read_idat(PNG *b, FILE *f, uint32_t crc32, uint32_t length)
{
    uint8_t *buff;

    if (length == 0) {
        return crc32;
    }
    if (length > INT_MAX || b->compressed_size > INT_MAX - (int)length) {
        VERR(png, "IDAT stream too large");
        b->malformed = 1;
        return crc32;
    }
    buff = realloc(b->compressed, (size_t)b->compressed_size + length);
    if (buff == NULL) {
        b->malformed = 1;
        return crc32;
    }
    b->compressed = buff;
    if (fread(b->compressed + b->compressed_size, 1, length, f) != length) {
        b->malformed = 1;
        return crc32;
    }
    crc32 = update_crc(crc32, b->compressed + b->compressed_size, (int)length);
    b->compressed_size += (int)length;
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

static void PNG_free(struct pic *p);

static struct pic* 
PNG_load(const char* filename, int skip_flag)
{
    struct pic *p = pic_alloc(sizeof(struct PNG));
    PNG * b = p->pic;
    FILE *f = NULL;
    uint32_t chunk_type = 0;
    uint32_t length = 0;
    uint8_t *data = NULL;
    uint8_t *udata = NULL;
    uint32_t crc32, crc;
    uint64_t out_pitch, out_size;
    int depth, decomp_len;

    f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(png, "cannot open %s", filename);
        goto fail;
    }

    if (READ_FAIL(&b->sig, sizeof(struct png_file_header), 1, f)) {
        VERR(png, "fail to read png file header");
        goto fail;
    }

    length = read_u32(f);
    if (READ_FAIL(&chunk_type, sizeof(uint32_t), 1, f)) {
        goto fail;
    }

    while (chunk_type != (uint32_t)CHARS2UINT("IEND")) {
        if (length > INT_MAX) {
            VERR(png, "chunk length %u is too large", length);
            b->malformed = 1;
            goto fail;
        }
        crc32 = init_crc32((uint8_t*)&chunk_type, sizeof(uint32_t));
        VDBG(png, "get chunk type %s", type2name(chunk_type));
        switch (chunk_type) {
            case CHUNK_TYPE_IHDR:
                crc32 = read_ihdr(b, f, crc32, length);
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
                    if (data == NULL || fread(data, length, 1, f) != 1) {
                        b->malformed = 1;
                        goto fail;
                    }
                    crc32 = update_crc(crc32, (uint8_t*)data, (int)length);
                    free(data);
                    data = NULL;
                }
                break;
        }

        /* a chunk parser that could not store its data fails the whole load */
        if (b->malformed) {
            goto fail;
        }
        if (READ_FAIL(&crc, sizeof(uint32_t), 1, f)) {
            goto fail;
        }
        crc32 = finish_crc32(crc32);
        CRC_ASSER(crc32, crc);
        length = read_u32(f);
        if (READ_FAIL(&chunk_type, sizeof(uint32_t), 1, f)) {
            goto fail;
        }
    }
    /* check iEND chunk */
    if (read_iend(f) != 0) {
        goto fail;
    }
    fclose(f);
    f = NULL;

    depth = calc_png_bits_per_pixel(b);
    b->size = calc_image_raw_size(b);
    out_pitch = (((uint64_t)b->ihdr.width * depth + 31) >> 5) << 2;
    out_size = out_pitch * b->ihdr.height;
    VDBG(png, "compressed size %d, pre allocate %d", b->compressed_size, b->size);
    if (depth <= 0 || b->size <= 0 || b->ihdr.width == 0 ||
        b->ihdr.height == 0 || out_pitch > INT_MAX || out_size > INT_MAX) {
        VERR(png, "invalid image size");
        goto fail;
    }

    if (!skip_flag) {
        /* zlib header plus adler32 trailer */
        if (b->compressed == NULL || b->compressed_size < 6) {
            VERR(png, "missing or too short IDAT stream");
            goto fail;
        }
        decomp_len = b->size;
        udata = malloc((size_t)b->size);
        if (udata == NULL) {
            goto fail;
        }
        deflate_decode(b->compressed, b->compressed_size, udata, &decomp_len);
        if (decomp_len != b->size) {
            VERR(png, "decompressed %d bytes out of %d expected", decomp_len,
                 b->size);
            goto fail;
        }
        free(b->compressed);
        b->compressed = NULL;
        b->compressed_size = 0;

        b->data = malloc((size_t)out_size);
        if (b->data == NULL) {
            goto fail;
        }
        if (PNG_unfilter(b, udata, decomp_len) != 0) {
            goto fail;
        }
        free(udata);
        udata = NULL;
    }
    p->width = b->ihdr.width;
    p->height = b->ihdr.height;
    p->depth = depth;
    p->pixels = b->data;
    // PNG in RGB/RGBA order ?
    if (p->depth == 32) {
        p->format = CS_PIXELFORMAT_ABGR8888;
        // CS_MasksToPixelFormatEnum(p->depth, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
    } else if (p->depth <= 24) {
        p->format = CS_MasksToPixelFormatEnum(p->depth, 0, 0, 0, 0x000000FF);
    }
    VDBG(png, "depth %d, format %s", p->depth, CS_GetPixelFormatName(p->format));
    p->pitch = (int)out_pitch;
    return p;

fail:
    if (f) {
        fclose(f);
    }
    free(data);
    free(udata);
    PNG_free(p);
    return NULL;
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
            if (b->itextual[i].language_tag) {
                free(b->itextual[i].language_tag);
            }
            if (b->itextual[i].translated_keyword) {
                free(b->itextual[i].translated_keyword);
            }
            if (b->itextual[i].text) {
                free(b->itextual[i].text);
            }
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
                b->bcolor.red, b->bcolor.green, b->bcolor.blue);
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
