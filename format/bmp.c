#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "bmp.h"
#include "mfile.h"
#include "file.h"
#include "utils.h"
#include "vlog.h"
#include "colorspace.h"

VLOG_REGISTER(bmp, DEBUG)

const char *bmptype[] = {
    "BM", // Windows 3.1x, 95, NT, ... etc
    "BA", // OS/2 Bitmap Array
    "CI", // OS/2 Color Icon
    "CP", // OS/2 Color Pointer
    "IC", // OS/2 Icon
    "PT", // OS/2 Pointer
};

static int 
BMP_probe(FILE *f)
{
    if (f == NULL) {
        return -ENOENT;
    }
    struct bmp_file_header file_h;
    int len = fread(&file_h, sizeof(file_h), 1, f);
    if (len < 1) {
        return -EBADF;
    }

    for (unsigned long i = 0; i < sizeof(bmptype)/sizeof(bmptype[0]); i++) {
        if (!memcmp(&file_h.file_type, bmptype[i], 2)) {
            return 0;
        }
    }
    return -EINVAL;
}

static int
RLE8_decode(uint8_t *data, int len, uint8_t *out, int pitch, int height, int depth,
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

static int
RLE4_decode(uint8_t *data, int len, uint8_t *out, int pitch, int height, int depth,
            struct bmp_color_entry *ct)
{
    int p = 0;
    uint8_t px = 0;
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

static void
read_pixels(struct pic *p, FILE *f)
{
    int height = ABS(p->height);
    int width = (p->width + 3) >> 2 << 2;
    BMP *b = p->pic;
    /* A positive height stores the rows bottom up, a negative one top down */
    for (int k = 0; k < height; k++) {
        int i = p->height > 0 ? height - 1 - k : k;
        FFREAD_IGNORE(b->data + p->pitch * i, p->depth / 8, width, f);
        for (int j = 0; j < width; j++) {
            if (p->depth == 32) {
                b->data[p->pitch * i + j * p->depth / 8 + 3] = 0xFF;
            }
        }
    }
}

static void
read_color_index(struct pic *p, int bit_count, FILE *f)
{
    BMP *b = p->pic;
    int height = ABS(p->height);
    // byte per line is aligned to 4 in bmp file
    int width = (p->width + 3) >> 2 << 2;

    for (int k = 0; k < height; k++) {
        int i = p->height > 0 ? height - 1 - k : k;
        for (int j = 0; j < width; j++) {
            uint8_t px = 0, pxlo = 0;
            if (bit_count == 8) {
                px = fgetc(f);
            } else if (bit_count == 4) {
                px = fgetc(f);
                pxlo = px & 0xF;
                px = (px >> 4) & 0xF;
            }
            /* colors_used may shrink the table below what the bit count
             * allows, so an index can point past the entries we read
             */
            const struct bmp_color_entry *clr;
            if (b->palette && px < b->palette_size) {
                clr = b->palette + px;
            } else {
                clr = &(const struct bmp_color_entry){0, 0, 0, 0xFF};
            }
            b->data[p->pitch * i + j * p->depth / 8] = clr->blue;
            b->data[p->pitch * i + j * p->depth / 8 + 1] = clr->green;
            b->data[p->pitch * i + j * p->depth / 8 + 2] = clr->red;
            if (p->depth == 32) {
                b->data[p->pitch * i + j * p->depth / 8 + 3] = clr->alpha;
            }
            if (bit_count == 4) {
                if (b->palette && pxlo < b->palette_size) {
                    clr = b->palette + pxlo;
                } else {
                    clr = &(const struct bmp_color_entry){0, 0, 0, 0xFF};
                }
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

static void BMP_free(struct pic* p);

static struct pic*
BMP_load(const char* filename, int skip_flag UNUSED)
{
    struct pic *p = pic_alloc(sizeof(BMP));
    BMP *b = p->pic;
    FILE *f = mfile_open(filename);
    if (!f) {
        BMP_free(p);
        return NULL;
    }
    FFREAD_IGNORE(&b->file_header, sizeof(struct bmp_file_header), 1, f);
    uint32_t size;
    FFREAD_IGNORE(&size, 4, 1, f);
    fseek(f, -4, SEEK_CUR);
    /* the info header size must be 12 (OS/2 v1) or >= 40 (Windows and later);
     * anything else is a malformed BMP we cannot interpret */
    if (size != 12 && size < 40) {
        VERR(bmp, "dib header size %u not supported", size);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    FFREAD_IGNORE(&b->dib, size, 1, f);

    /* --- parse width / height and sanity-check dimensions --- */
    int32_t w;
    int32_t h;
    uint16_t bit_count;
    uint32_t compression = 0;
    uint32_t size_image = 0;
    uint32_t colors_used = 0;

    if (size == 12) {
        bit_count = b->dib.v1.bit_count;
        w = (int32_t)b->dib.v1.width;
        h = (int32_t)b->dib.v1.height;
    } else {
        bit_count = b->dib.v2.bit_count;
        w = b->dib.v2.width;
        h = b->dib.v2.height;
        compression = b->dib.v2.compression;
        size_image = b->dib.v2.size_image;
        colors_used = b->dib.v2.colors_used;
    }

    /* bit_count must be a known multiple of 8 (or 1,2,4 for indexed) */
    if (bit_count != 1 && bit_count != 2 && bit_count != 4 && bit_count != 8 &&
        bit_count != 16 && bit_count != 24 && bit_count != 32) {
        VERR(bmp, "unsupported bit count %u", bit_count);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    /* width is stored unsigned in v1, signed in v2; both must be positive and
     * small enough that later (width + 3) doesn't overflow int */
    if (w <= 0 || w > 0x40000) {
        VERR(bmp, "invalid width %d", w);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    /* height may be negative (top-down storage); take the absolute value */
    if (h == 0 || h == INT32_MIN) {
        VERR(bmp, "invalid height %d", h);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    uint32_t abs_h = (uint32_t)((h < 0) ? -h : h);
    if (abs_h > 0x40000) {
        VERR(bmp, "height %d too large", h);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    /* reject anything that would blow up the pitch or allocation size */
    if ((uint64_t)w * (uint64_t)abs_h > ((uint64_t)1 << 28)) {
        VERR(bmp, "image dimensions too large %u*%u", w, abs_h);
        fclose(f);
        BMP_free(p);
        return NULL;
    }

    p->depth = bit_count;
    p->width = w;
    p->height = h;

    if (size == 12) {
        if (b->dib.v1.bit_count <= 8) {
            p->depth = 24;
            /* bit_count is at most 8 here, so 1u << bit_count is bounded to 256 */
            uint32_t color_num = 1u << b->dib.v1.bit_count;
            b->palette = malloc(sizeof(struct bmp_color_entry) * color_num);
            if (b->palette == NULL) {
                fclose(f);
                BMP_free(p);
                return NULL;
            }
            b->palette_size = (int)color_num;
            VDBG(bmp, "palette");
            for (uint32_t i = 0; i < color_num; i++) {
                FFREAD_IGNORE(b->palette + i, 4, 1, f);
                VDBG(bmp, "\t %d %d %d, %d", b->palette[i].blue, b->palette[i].green,
                     b->palette[i].red, b->palette[i].alpha);
            }
        }
    } else if (size >= 40) {
        if (compression == BI_BITFIELDS) {
            FFREAD_IGNORE(&b->color, 12, 1, f);
            p->format = CS_MasksToPixelFormatEnum(
                p->depth, b->color.red_mask, b->color.green_mask,
                b->color.blue_mask, b->color.alpha_mask);
        } else if (compression == BI_ALPHABITFIELDS) {
            FFREAD_IGNORE(&b->color, 16, 1, f);
            p->format = CS_MasksToPixelFormatEnum(
                p->depth, b->color.red_mask, b->color.green_mask,
                b->color.blue_mask, b->color.alpha_mask);
        }

        if (bit_count <= 8) {
            p->depth = 24;
            uint32_t max_colors = 1u << bit_count;
            uint32_t color_num = colors_used ? colors_used : max_colors;
            /* colors_used cannot promise more entries than the bit count
             * allows, and a bogus count must not size the allocation */
            if (color_num > max_colors) {
                VERR(bmp, "colors_used %u exceeds the %u entry table",
                     color_num, max_colors);
                color_num = max_colors;
            }
            b->palette = malloc(sizeof(struct bmp_color_entry) * color_num);
            if (b->palette == NULL) {
                fclose(f);
                BMP_free(p);
                return NULL;
            }
            b->palette_size = (int)color_num;
            FFREAD_IGNORE(b->palette, 4, color_num, f);
            VDBG(bmp, "palette");
            for (uint32_t i = 0; i < color_num; i++) {
                VDBG(bmp, "\t %d %d %d, %d", b->palette[i].blue, b->palette[i].green,
                     b->palette[i].red, b->palette[i].alpha);
            }
        }
    }

    if (p->depth == 24) {
        p->format = CS_PIXELFORMAT_BGR24;
    } else if (p->depth == 32) {
        p->format = CS_PIXELFORMAT_ARGB32;
    }
    VINFO(bmp, "bitcount %d pixel format %s", p->depth, CS_GetPixelFormatName(p->format));

    fseek(f, b->file_header.offset_data, SEEK_SET);

    /* pitch must be a multiple of 4 bytes, so round width up first; do the
     * calculation in size_t and check for overflow before casting back to int.
     * Use the final p->depth (which palette modes may have promoted to 24) */
    uint64_t aligned_w = ((uint64_t)w + 3) & ~(uint64_t)3;
    uint64_t pitch64 = aligned_w * (uint64_t)p->depth / 8;
    if (pitch64 == 0 || pitch64 > (uint64_t)INT_MAX) {
        VERR(bmp, "pitch %llu invalid", (unsigned long long)pitch64);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    /* total buffer: abs(height) * pitch, also in size_t */
    uint64_t buf64 = (uint64_t)abs_h * pitch64;
    if (buf64 == 0 || buf64 > (uint64_t)SIZE_MAX) {
        VERR(bmp, "pixel buffer size %llu invalid", (unsigned long long)buf64);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    p->pitch = (int)pitch64;
    b->data = malloc((size_t)buf64);
    if (!b->data) {
        VERR(bmp, "cannot allocate %llu bytes", (unsigned long long)buf64);
        fclose(f);
        BMP_free(p);
        return NULL;
    }
    memset(b->data, 0, (size_t)buf64);

    if (size == 12) {
        if (bit_count > 8) {
            read_pixels(p, f);
        } else {
            VDBG(bmp, "width %d pitch %d, bit %d", p->width, p->pitch, bit_count);
            read_color_index(p, bit_count, f);
        }
    } else if (size >= 40) {
        if (bit_count > 8) {
            read_pixels(p, f);
        } else {
            if (compression == BI_RLE8 || compression == BI_RLE4) {
                /* a compressed payload must be at least 2 bytes (one run) and
                 * smaller than the image can possibly uncompress to; anything
                 * else is bogus */
                if (size_image < 2 || size_image == 0) {
                    VERR(bmp, "rle compressed size %u invalid", size_image);
                    free(b->data);
                    b->data = NULL;
                    fclose(f);
                    BMP_free(p);
                    return NULL;
                }
                uint8_t *compressed = malloc(size_image ? size_image : 1);
                if (!compressed) {
                    free(b->data);
                    b->data = NULL;
                    fclose(f);
                    BMP_free(p);
                    return NULL;
                }
                FFREAD_IGNORE(compressed, size_image, 1, f);
                if (compression == BI_RLE8) {
                    RLE8_decode(compressed, (int)size_image, b->data, p->pitch,
                                p->height, p->depth, b->palette);
                } else {
                    RLE4_decode(compressed, (int)size_image, b->data, p->pitch,
                                p->height, p->depth, b->palette);
                }
                free(compressed);
            } else {
                VDBG(bmp, "width %d pitch %d", p->width, p->pitch);
                read_color_index(p, bit_count, f);
            }
        }
    }

    fclose(f);
    p->pixels = b->data;
    p->height = ABS(p->height);
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
    fprintf(f, "\tinfo header size: %d\n", b->dib.v2.size);
    if (b->dib.v1.size == 12) {
        fprintf(f, "\timage width: %d\n", b->dib.v1.width);
        fprintf(f, "\timage height: %d\n", b->dib.v1.height);
        fprintf(f, "\timage planes: %d\n", b->dib.v1.planes);
        fprintf(f, "\tbits per pixel: %d\n", b->dib.v1.bit_count);
    } else if (b->dib.v1.size == 40) {
        fprintf(f, "\timage width: %d\n", b->dib.v2.width);
        fprintf(f, "\timage height: %d\n", b->dib.v2.height);
        fprintf(f, "\timage planes: %d\n", b->dib.v2.planes);
        fprintf(f, "\tbits per pixel: %d\n", b->dib.v2.bit_count);
        fprintf(f, "\tcompression: %s\n", compression_str[b->dib.v2.compression]);
        fprintf(f, "\tsize_image: %d\n", b->dib.v2.size_image);
        fprintf(f, "\tx resolution: %d\n", b->dib.v2.x_pixels_per_meter);
        fprintf(f, "\ty resolution: %d\n", b->dib.v2.y_pixels_per_meter);
        fprintf(f, "\tcolors_used: %d\n", b->dib.v2.colors_used);
        fprintf(f, "\tcolors_important: %d\n", b->dib.v2.colors_important);
        if (b->palette) {
            fprintf(f, "\tpalette nubmer: %d\n", 1 << b->dib.v2.bit_count);
        }
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
    info_p->height = h;
    info_p->planes = 1;
    info_p->bit_count = 32;
    info_p->compression = 0;
    info_p->size_image = w * h * 4;
    info_p->x_pixels_per_meter = 0x60;
    info_p->y_pixels_per_meter = 0x60;
    // a 32bpp bi_rgb bitmap carries no colour table
    info_p->colors_used = 0;
    info_p->colors_important = 0;

    return d_ptr;
}

void
BMP_encode(struct pic *p, const char * fname)
{
    FILE *fd = fopen(fname, "w");
    // the header written below is always 32bpp, so every row has to leave with
    // four bytes a pixel even when the decoder held three
    int pitch = p->width * 4;
    int bytes = p->depth >> 3;
    uint8_t *data = alloc_bmp_with_head(p->width, p->height);
    long file_length = 54L + (long)pitch * p->height;
    uint8_t *out = data + 54;
    uint8_t *b = (uint8_t *)p->pixels;

    for (int i = 0; i < p->height; i++) {
        // a positive height means the rows are stored bottom up
        uint8_t *line = out + (p->height - 1 - i) * pitch;
        uint8_t *src = b + (long)i * p->pitch;
        for (int x = 0; x < p->width; x++) {
            line[x * 4 + 0] = src[x * bytes + 0];
            line[x * 4 + 1] = src[x * bytes + 1];
            line[x * 4 + 2] = src[x * bytes + 2];
            line[x * 4 + 3] = bytes > 3 ? src[x * bytes + 3] : 0xFF;
        }
    }
    fwrite(data, file_length, 1, fd);
    fclose(fd);
    free(data);
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
