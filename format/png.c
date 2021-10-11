#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include "png.h"
#include "file.h"
#include "deflate.h"

static int 
PNG_probe(const char* filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct png_file_header sig;
    size_t len = fread(&sig, sizeof(struct png_file_header), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
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

int calc_image_raw_size(PNG *p)
{
    int color_len;
    switch(p->ihdr.color_type) {
        case GREYSCALE:
            color_len = 1;
            break;
        case TRUECOLOR:
            color_len = 3;
            break;
        case GREYSCALE_ALPHA:
            color_len = 2;
            break;
        case TRUECOLOR_ALPHA:
            color_len = 4;
            break;
        default:
            color_len = 0;
            break;
    }
    return p->ihdr.width * (p->ihdr.height * (color_len * p->ihdr.bit_depth) + 7)/8 + p->ihdr.height;
}

static struct pic* 
PNG_load(const char* filename)
{
    const char *chunk_types[] = {
        "IHDR",
        "cHRM",
        "gAMA",
        "sBIT",
        "PLTE",
        "bKGD",
        "hIST",
        "tRNS",
        "oFFs",
        "pHYs",
        "sCAL",
        "IDAT",
        "tIME",
        "tEXt",
        "zTXt",
        "fRAc",
        "gIFg",
        "gIFx",
        "IEND"
    };
    PNG * b = malloc(sizeof(PNG));
    b->n_ctext = 0;
    b->n_text = 0;
    b->n_itext = 0;
    b->size = 0;
    b->data = NULL;
    FILE *f = fopen(filename, "rb");
    fread(&b->sig, sizeof(struct png_file_header), 1, f);

    uint8_t nullbyte;
    uint32_t length;
    uint32_t chunk_type = 0;
    uint8_t keybuff[80];
    uint8_t t, i = 0, j = 0, k = 0;

    uint8_t *data = NULL, *compressed = NULL;
    int compressed_size = 0;

    b->size = calc_image_raw_size(b);
    b->data = malloc(b->size);

    fread(&length, 1, sizeof(uint32_t), f);
    fread(&chunk_type, 1, sizeof(uint32_t), f);
    length = ntohl(length);
    while(length && chunk_type != CHARS2UINT("IEND")) {
        switch (chunk_type) {
            case CHARS2UINT("IHDR"):
                fread(&b->ihdr, 1, sizeof(struct png_ihdr), f);
                b->ihdr.width = ntohl(b->ihdr.width);
                b->ihdr.height = ntohl(b->ihdr.height);
                break;
            case CHARS2UINT("IDAT"):
                compressed_size += length;
                if (compressed_size== length) {
                    compressed = malloc(compressed_size);
                    fread(compressed, 1, length, f);
                } else {
                    compressed = realloc(compressed, compressed_size);
                    fread(compressed + compressed_size - length , 1, length, f);
                }
                deflate_decode(compressed, compressed_size, b->data, &b->size);
                break;
            case CHARS2UINT("tEXt"):
                i = read_char_till_null(f, keybuff, 80);
                if (b->n_text ++) {
                    b->textual = realloc(b->textual, b->n_text * sizeof(struct textual_data));
                    (b->textual+b->n_text-1)->keyword = malloc(i);
                    memcpy((b->textual+b->n_text-1)->keyword, keybuff, i);
                    if (length - i) {
                        (b->textual+b->n_text-1)->text = malloc(length-i);
                        fread((b->textual+b->n_text-1)->text, 1, length-i, f);
                    } else {
                        (b->textual+ b->n_text -1)->text = NULL;
                    }
                } else {
                    b->textual = malloc(sizeof(struct textual_data));
                    b->textual->keyword = malloc(i);
                    memcpy(b->textual->keyword, keybuff, i);
                    if (length - i) {
                        b->textual->text = malloc(length - i);
                        fread(b->textual->text, 1, length - i, f);
                    } else {
                        b->textual->text = NULL;
                    }
                }
                break;
            case CHARS2UINT("iTXt"):
                
                i = read_char_till_null(f, keybuff, 80);
                if (b->n_itext ++) {
                    b->itextual = realloc(b->itextual, b->n_text * sizeof(struct international_textual_data));
                    (b->itextual+b->n_itext-1)->keyword = malloc(i);
                    memcpy((b->itextual+b->n_itext-1)->keyword, keybuff, i);
                    (b->itextual+b->n_itext-1)->compression_flag = fgetc(f);
                    (b->itextual+b->n_itext-1)->compression_method = fgetc(f);
                    j = read_char_till_null(f, keybuff, 80);
                    if (j <= 1) {
                        (b->itextual+b->n_itext-1)->language_tag = NULL;
                    } else {
                        (b->itextual+b->n_itext-1)->language_tag = malloc(j);
                        memcpy((b->itextual+b->n_itext-1)->language_tag, keybuff, j);
                    }
                    k = read_char_till_null(f, keybuff, 80);
                    if (k <= 1) {
                        (b->itextual+b->n_itext-1)->translated_keyword = NULL;
                    } else {
                        (b->itextual+b->n_itext-1)->translated_keyword = malloc(k);
                        memcpy((b->itextual+b->n_itext-1)->translated_keyword, keybuff, k);
                    }
                    if (length - i - j - k) {
                        (b->itextual+b->n_itext-1)->text = malloc(length-i-j-k);
                        fread((b->itextual+b->n_itext-1)->text, 1, length-i-j-k, f);
                    } else {
                        (b->itextual+ b->n_itext -1)->text = NULL;
                    }
                } else {
                    b->itextual = malloc(sizeof(struct international_textual_data));
                    b->itextual->keyword = malloc(i);
                    memcpy(b->itextual->keyword, keybuff, i);
                    b->itextual->compression_flag = fgetc(f);
                    b->itextual->compression_method = fgetc(f);
                    j = read_char_till_null(f, keybuff, 80);
                    if (j <= 1) {
                        b->itextual->language_tag = NULL;
                    } else {
                        b->itextual->language_tag = malloc(j);
                        memcpy(b->itextual->language_tag, keybuff, j);
                    }
                    k = read_char_till_null(f, keybuff, 80);
                    if (k <= 1) {
                        b->itextual->translated_keyword = NULL;
                    } else {
                        b->itextual->translated_keyword = malloc(k);
                        memcpy(b->itextual->translated_keyword, keybuff, k);
                    }
                    if (length - i -j - k) {
                        b->itextual->text = malloc(length - i - j - k);
                        fread(b->itextual->text, 1, length - i - j - k, f);
                    } else {
                        b->itextual->text = NULL;
                    }
                }
                break;
            case CHARS2UINT("zTXt"):
                i = read_char_till_null(f, keybuff, 80);
                if (b->n_itext ++) {
                    b->ctextual = realloc(b->ctextual, b->n_ctext * sizeof(struct compressed_textual_data));
                    (b->ctextual+b->n_ctext-1)->keyword = malloc(i);
                    memcpy((b->ctextual+b->n_ctext-1)->keyword, keybuff, i);
                    if (length - i -1) {
                        (b->ctextual+b->n_ctext-1)->compressed_text = malloc(length-i-1);
                        fread((b->ctextual+b->n_ctext-1)->compressed_text, 1, length-i-1, f);
                    } else {
                        (b->ctextual+ b->n_ctext -1)->compressed_text = NULL;
                    }
                }else{
                    b->ctextual = malloc(sizeof(struct compressed_textual_data));
                    b->ctextual->keyword = malloc(i);
                    memcpy(b->ctextual->keyword, keybuff, i);
                    b->ctextual->compression_method = fgetc(f);
                    if (length - i - 1) {
                        b->ctextual->compressed_text = malloc(length - i - 1);
                        fread(b->ctextual->compressed_text, 1, length - i -1 , f);
                    } else {
                        b->ctextual->compressed_text = NULL;
                    }
                }
                break;
            default:
                data = malloc(length);
                fread(data, length, 1, f);
                break;
        }
            
        uint32_t crc;
        fread(&crc, sizeof(uint32_t), 1, f);
        fread(&length, sizeof(uint32_t), 1, f);
        length = ntohl(length);
        fread(&chunk_type, sizeof(uint32_t), 1, f);
    }
    fclose(f);
    struct pic *p = malloc(sizeof(struct pic));
    p->pic = b;
    p->width = b->ihdr.width;
    p->height = b->ihdr.height;
    p->depth = b->ihdr.bit_depth;
    p->pixels = b->data;
    p->rmask = 0;
    p->gmask = 0;
    p->bmask = 0;
    p->amask = 0xFF;
    p->pitch = ((b->ihdr.width * b->ihdr.bit_depth + 31) >> 5) << 2;
    return p;
}

static void 
PNG_free(struct pic* p)
{
    PNG *b = (PNG *)(p->pic);
    free(b);
    free(p);
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