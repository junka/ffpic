#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "bmp.h"
#include "file.h"
#include "vlog.h"
#include "colorspace.h"

VLOG_REGISTER(bmp, DEBUG)

static int 
BMP_probe(const char* filename)
{
    const char* bmptype[] = {
        "BM",
        "BA",
        "CI",
        "CP",
        "IC",
        "PT",
    };
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(bmp, "fail to open %s\n", filename);
        return -ENOENT;
    }
    struct bmp_file_header file_h;
    int len = fread(&file_h, sizeof(file_h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);

    for (unsigned long i = 0; i < sizeof(bmptype)/sizeof(bmptype[0]); i++) {
        if (!memcmp(&file_h.file_type, bmptype[i], 2)) {
            return 0;
        }
    }
    return -EINVAL;
}

int RLE8_decode(uint8_t *data, int len, uint8_t *out, int pitch, int height, int depth,
                struct bmp_color_entry *ct) {
    int p = 0;
    int y = (height > 0) ? height - 1 : 0;
    int delta = (height > 0) ? -1 : 1;
    int x = 0;
    while (p < len) {
        uint8_t first = data[p++];
        if (first > 0) {
            uint8_t pixel = data[p++];
            for (int i = 0; i < first; i++) {
                memcpy(out + y * pitch + x * depth/8, ct + pixel, depth/8);
                x ++;
                if (x * depth / 8 >= pitch) {
                    x = 0;
                    y += delta;
                }
            }
        } else {
            uint8_t c = data[p++];
            if (c == 0) {
                y += delta;
                x = 0;
            } else if (c == 1) {
                return 0;
            } else if (c == 2) {
                x += data[p++];
                y += data[p++] * delta;
            } else {
                for (int i = 0; i < c; i++) {
                    uint8_t pixel = data[p++];
                    memcpy(out + y * pitch + x * depth/8, ct+pixel, depth/8);
                    x++;
                    if (x * depth / 8 >= pitch) {
                        x = 0;
                        y += delta;
                    }
                }
                for (int i = 0; i < (4 - c % 4) % 4; i++) {
                    p++;
                }
            }
        }
    }
    return -1;
}

int RLE4_decode(uint8_t *data, int len, uint8_t *out, int pitch, int height, int depth,
                struct bmp_color_entry *ct)
{
    int p = 0;
    uint8_t lo, hi, px;
    int y = height > 0 ? height - 1 : 0;
    int delta = (height > 0) ? -1 : 1;
    int x = 0;
    while (p < len) {
        uint8_t first = data[p++];
        if (first > 0) {
            px = data[p++];
            for (int i = 0; i < first; i++) {
                memcpy(out + y * pitch + x * depth/8, ct + (px >> 4), depth/8);
                x++;
                px = (px << 4 | px >> 4);
                if (x * depth / 8 >= pitch) {
                    x = 0;
                    y += delta;
                }
            }
        } else {
            uint8_t c = data[p++];
            if (c == 0) {
                y += delta;
                x = 0;
            } else if (c == 1) {
                return 0;
            } else if (c == 2) {
                x += data[p++];
                y += data[p++] * delta;
            } else {
                for (int i = 0; i < c; i++) {
                    if (i % 2 == 0) {
                        px = data[p++];
                    }
                    memcpy(out + y * pitch + x * depth/8, ct + (px >> 4), depth / 8);
                    x ++;
                    px = (px << 4 | px >> 4);
                    if (x * depth / 8 >= pitch) {
                        x = 0;
                        y += delta;
                    }
                }
                for (int i = 0; i < (4 - ((c + 1) / 2) % 4) % 4; i++) {
                    p ++;
                }
            }
        }
    }
    return -1;
}

static struct pic* 
BMP_load(const char* filename)
{
    struct pic *p = pic_alloc(sizeof(BMP));
    BMP *b = p->pic;
    FILE *f = fopen(filename, "rb");
    fread(&b->file_header, sizeof(struct bmp_file_header ), 1, f);
    fread(&b->dib, sizeof(struct bmp_info_header), 1, f);
    /* FIXME: header length and later struct */

    p->depth = b->dib.bit_count;
    if (!memcmp(&b->file_header.file_type, "BM", 2)) {
        if (b->dib.compression == BI_BITFIELDS) {
            fread(&b->color, 12, 1, f);
            p->format = CS_MasksToPixelFormatEnum(
                p->depth, b->color.red_mask, b->color.green_mask,
                b->color.blue_mask, b->color.alpha_mask);
        } else if (b->dib.compression == BI_ALPHABITFIELDS) {
            fread(&b->color, 16, 1, f);
            p->format = CS_MasksToPixelFormatEnum(
                p->depth, b->color.red_mask, b->color.green_mask,
                b->color.blue_mask, b->color.alpha_mask);
        }
    }
    if (b->dib.bit_count <= 8) {
        p->depth = 24;
        int color_num = b->dib.colors_used ? b->dib.colors_used : (1 << b->dib.bit_count);
        b->palette = malloc(sizeof(struct bmp_color_entry) * color_num);
        fread(b->palette, 4, color_num, f);
        VDBG(bmp, "palette");
        for (int i = 0; i < color_num; i++) {
            vlog(VLOG_DEBUG, vlog_bmp, " %d %d %d, %d\n", b->palette[i].blue,
                 b->palette[i].green, b->palette[i].red, b->palette[i].alpha);
        }
    }
    if (p->depth == 24) {
        p->format = CS_PIXELFORMAT_BGR24;
    } else if (p->depth == 32) {
        p->format = CS_PIXELFORMAT_BGRX8888;
    }
    VINFO(bmp, "bitcount %d pixel format %s", b->dib.bit_count, CS_GetPixelFormatName(p->format));

    fseek(f, b->file_header.offset_data, SEEK_SET);

    /* pitch must be multiple of four bytes */
    p->width = b->dib.width;
    p->height = b->dib.height;
    p->pitch = ((p->width + 3) >> 2 << 2) * p->depth / 8;
    b->data = malloc(p->height * p->pitch);
    memset(b->data, 0, p->height * p->pitch);

    int top = b->dib.height > 0 ? b->dib.height - 1 : 0;
    int bottom = b->dib.height > 0 ? 0 : 1 - b->dib.height;
    int delta = b->dib.height > 0 ? -1 : 1;
    // int bytes_perline = b->dib.width * b->dib.bit_count / 8;

    if (b->dib.bit_count > 8) {
        /* For read bmp pic data from bottom to up */
        for (int i = top, j = 0; i >= bottom; i += delta, j++) {
            fread(b->data + p->pitch * i, b->dib.width * p->depth / 8, 1, f);
        }
    } else {
        if (b->dib.compression == BI_RLE8) {
            uint8_t *compressed = malloc(b->dib.size_image);
            fread(compressed, b->dib.size_image, 1, f);
            RLE8_decode(compressed, b->dib.size_image, b->data, p->pitch, p->height, p->depth,
                        b->palette);
            free(compressed);
        } else if (b->dib.compression == BI_RLE4) {
            uint8_t *compressed = malloc(b->dib.size_image);
            fread(compressed, b->dib.size_image, 1, f);
            RLE4_decode(compressed, b->dib.size_image, b->data, p->pitch, p->height, p->depth,
                             b->palette);
            free(compressed);
        } else {
            VDBG(bmp, "width %d pitch %d", p->width, p->pitch);
            for (int i = top; i >= bottom; i += delta) {
                for(int j = 0; j < p->width; j ++) {
                    uint8_t px = 0, pxlo;
                    if (b->dib.bit_count == 8) {
                        px = fgetc(f);
                    } else if (b->dib.bit_count == 4) {
                        px = fgetc(f);
                        pxlo = px & 0xF;
                        px = (px >> 4) & 0xF;
                    }
                    struct bmp_color_entry *clr = b->palette + px;
                    b->data[p->pitch * i + j * p->depth / 8] = clr->blue;
                    b->data[p->pitch * i + j * p->depth / 8 + 1] = clr->green;
                    b->data[p->pitch * i + j * p->depth / 8 + 2] = clr->red;
                    if (p->depth == 32) {
                        b->data[p->pitch * i + j * p->depth / 8 + 3] = clr->alpha;
                    }
                    if (b->dib.bit_count == 4) {
                        clr = b->palette + pxlo;
                        b->data[p->pitch * i + j * p->depth / 8] = clr->blue;
                        b->data[p->pitch * i + j * p->depth / 8 + 1] = clr->green;
                        b->data[p->pitch * i + j * p->depth / 8 + 2] = clr->red;
                        if (p->depth == 32) {
                            b->data[p->pitch * i + j * p->depth / 8 + 3] = clr->alpha;
                        }
                    }
                }
            }
        }
    }
    fclose(f);
    p->pixels = b->data;
    return p;
}

static void 
BMP_free(struct pic* p)
{
    BMP * bmp = (BMP *)(p->pic);
    if(bmp->palette)
        free(bmp->palette);
    free(bmp->data);
    pic_free(p);
}

static void 
BMP_info(FILE* f, struct pic* p)
{
    struct bmp_type {
        char * name;
        char * desc;
    } btypes[] = {
        { "BM", "Windows"},
        { "BA", "OS/2 struct bitmap array"},
        { "CI", "OS/2 struct color icon"},
        { "CP", "OS/2 const color pointer"},
        { "IC", "OS/2 struct icon"},
        { "PT", "OS/2 pointer"},
    };
    BMP *b = (BMP*)(p->pic);
    fprintf(f, "BMP file formart:\n");
    for (unsigned long i = 0; i < sizeof(btypes)/sizeof(btypes[0]); i++) {
        if (!memcmp(&b->file_header.file_type, btypes[i].name, 2)) {
            fprintf(f, "file type: %s\n", btypes[i].desc);
            break;
        }
    }
    const char *compression_str[] = {
        "RGB", "RLE8",           "RLE4",     "BITFIELDS", "JPEG",
        "PNG", "ALPHABITFIELDS", "",         "",          "",
        "",   "CMYK",          "CMYKRLE8","CMYKRLE4",
    };
    fprintf(f, "\tfile size: %d\n", b->file_header.file_size);
    fprintf(f, "\tpixels starting address: %d\n", b->file_header.offset_data);
    fprintf(f, "-------------------------------------\n");
    fprintf(f, "\tinfo header size: %d\n", b->dib.size);
    fprintf(f, "\timage width: %d\n", b->dib.width);
    fprintf(f, "\timage height: %d\n", b->dib.height);
    fprintf(f, "\timage planes: %d\n", b->dib.planes);
    fprintf(f, "\tbits per pixel: %d\n", b->dib.bit_count);
    fprintf(f, "\tcompression: %s\n", compression_str[b->dib.compression]);
    fprintf(f, "\tsize_image: %d\n", b->dib.size_image);
    fprintf(f, "\tx resolution: %d\n", b->dib.x_pixels_per_meter);
    fprintf(f, "\ty resolution: %d\n", b->dib.y_pixels_per_meter);
    fprintf(f, "\tcolors_used: %d\n", b->dib.colors_used);
    fprintf(f, "\tcolors_important: %d\n", b->dib.colors_important);
    if (b->palette) {
        fprintf(f, "\tpalette nubmer: %d\n", 1 << b->dib.bit_count);
    }
    fprintf(f, "\n");
}

static uint8_t *alloc_bmp_with_head(uint32_t w, uint32_t h)
{
    struct bmp_file_header *bmp_p;
    long file_length = (w * h * (32 >> 3));
    uint8_t *d_ptr = (uint8_t *)malloc(file_length + 54);

    bmp_p = (struct bmp_file_header *)d_ptr;
    bmp_p->file_type = 0x4D42;
    bmp_p->file_size = 54 + w * h * 4;
    bmp_p->reserved1 = 0x0;
    bmp_p->reserved2 = 0x0;
    bmp_p->offset_data = 0x36;

    struct bmp_info_header *info_p =
        (struct bmp_info_header *)(d_ptr + sizeof(struct bmp_file_header));
    // bmp info head
    info_p->size = 0x28;
    info_p->width = w;
    info_p->height = -h;
    info_p->planes = 0x01;
    info_p->bit_count = 32;
    info_p->compression = 0;
    info_p->size_image = w * h * 4;
    info_p->x_pixels_per_meter = 0x60;
    info_p->y_pixels_per_meter = 0x60;
    info_p->colors_used = 2;
    info_p->colors_important = 0;

    return d_ptr;
}

void BMP_encode(struct pic *p, const char * fname)
{
    FILE *fd = fopen(fname, "w");
    uint8_t *data = alloc_bmp_with_head(p->width, p->height);
    long file_length = (p->width * p->height * (p->depth >> 3)) + 54;
    uint8_t *file_p = data;
    uint8_t *file_p_tmp = NULL;
    uint8_t *b = (uint8_t *)p->pixels;

    file_p_tmp = file_p;
    file_p_tmp += 54;
    for (int i = 0; i < p->height; i++) {
        for (int j = 0; j < p->width; j++) {
            memcpy(file_p_tmp, b, 4);
            b += 4;
            file_p_tmp += 4;
        }
    }
    fwrite(file_p, file_length, 1, fd);

    free(data);
    fclose(fd);
}

static struct file_ops bmp_ops = {
    .name = "BMP",
    .probe = BMP_probe,
    .load = BMP_load,
    .free = BMP_free,
    .info = BMP_info,
    .encode = BMP_encode,
};

void 
BMP_init(void)
{
    file_ops_register(&bmp_ops);
}
