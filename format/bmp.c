#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "bmp.h"
#include "file.h"

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
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct bmp_file_header file_h;
    int len = fread(&file_h, sizeof(file_h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);

    for (int i = 0; i < sizeof(bmptype)/sizeof(bmptype[0]); i++) {
        if (!memcmp(&file_h.file_type, bmptype[i], 2)) {
            return 0;
        }
    }
    return -EINVAL;
}

static struct pic* 
BMP_load(const char* filename)
{
    BMP * b = calloc(sizeof(BMP), 1);
    FILE *f = fopen(filename, "rb");
    struct pic *p = malloc(sizeof(struct pic));
    fread(&b->file_header, sizeof(struct bmp_file_header ), 1, f);
    fread(&b->dib, sizeof(struct bmp_info_header), 1, f);
    //FIXME: header length and later struct
    if (!memcmp(&b->file_header.file_type, "BM", 2)) {
        if (b->dib.compression == BI_BITFIELDS) {
            fread(&b->color, 12, 1, f);
            p->amask = b->color.alpha_mask;
            p->rmask = b->color.red_mask;
            p->gmask = b->color.green_mask;
            p->bmask = b->color.blue_mask;
        }
        else if (b->dib.compression == BI_ALPHABITFIELDS) {
            fread(&b->color, 16, 1, f);
            p->amask = b->color.alpha_mask;
            p->rmask = b->color.red_mask;
            p->gmask = b->color.green_mask;
            p->bmask = b->color.blue_mask;
        }
    }
    p->depth = b->dib.bit_count;
    if (b->dib.bit_count <= 8) {
        p->depth = 32;
        int color_num = b->dib.colors_used ? b->dib.colors_used : (1 << b->dib.bit_count);
        b->palette = malloc(sizeof(struct bmp_color_entry) * color_num);
        fread(b->palette, 4, color_num, f);
    }

    fseek(f, b->file_header.offset_data, SEEK_SET);

    // width must be multiple of four bytes
    p->width = ((b->dib.width + 3) >> 2) << 2;
    p->height = b->dib.height;
    p->pitch = ((p->width * p->depth + 31) >> 5) << 2;
    b->data = malloc(b->dib.height * p->pitch);
    p->pic = b;

    int upper = b->dib.height > 0 ? b->dib.height -1 : 0;
    int bottom = b->dib.height > 0 ? 0 : 1 - b->dib.height;
    int delta = b->dib.height > 0 ? -1 : 1;
    if (b->dib.bit_count > 8) {
        //For read bmp pic data from bottom to up
        for (int i = upper; i >= bottom; i += delta) {
            fread(b->data + p->pitch * i, p->pitch, 1, f);
        }
    } else {
        for (int i = upper; i >= bottom; i += delta) {
            for(int j = 0; j < p->width; j ++) {
                uint8_t px = fgetc(f);
                b->data[p->pitch * i + j*p->depth/8] = b->palette[px].blue;
                b->data[p->pitch * i + j*p->depth/8 + 1] = b->palette[px].green;
                b->data[p->pitch * i + j*p->depth/8 + 2] = b->palette[px].red;
                b->data[p->pitch * i + j*p->depth/8 + 3] = b->palette[px].alpha;
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
    free(bmp);
    free(p);
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
    for (int i = 0; i < sizeof(btypes)/sizeof(btypes[0]); i++) {
        if (!memcmp(&b->file_header.file_type, btypes[i].name, 2)) {
            fprintf(f, "file type: %s\n", btypes[i].desc);
            break;
        }
    }
    fprintf(f, "\tfile size: %d\n", b->file_header.file_size);
    fprintf(f, "\tpixels starting address: %d\n", b->file_header.offset_data);
    fprintf(f, "-------------------------------------\n");
    fprintf(f, "\tinfo header size: %d\n", b->dib.size);
    fprintf(f, "\timage width: %d\n", b->dib.width);
    fprintf(f, "\timage height: %d\n", b->dib.height);
    fprintf(f, "\timage planes: %d\n", b->dib.planes);
    fprintf(f, "\tbits per pixel: %d\n", b->dib.bit_count);
    fprintf(f, "\tcompression: %d\n", b->dib.compression);
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

static struct file_ops bmp_ops = {
    .name = "BMP",
    .probe = BMP_probe,
    .load = BMP_load,
    .free = BMP_free,
    .info = BMP_info,
};

void 
BMP_init(void)
{
    file_ops_register(&bmp_ops);
}
