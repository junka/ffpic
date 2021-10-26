#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "file.h"
#include "tga.h"

static int 
TGA_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    size_t extra_size = 0;
    struct tga_footer tailer;
    fseek(f, -sizeof(struct tga_footer), SEEK_END);
    size_t filesize = ftell(f) + sizeof(struct tga_footer);
    size_t len = fread(&tailer, sizeof(struct tga_footer), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    if(tailer.end == '.' && tailer.zero == 0 &&
        !memcmp(tailer.signature, "TRUEVISION-XFILE", 16)) {
        fclose(f);
        return 0;
    }

    //no optional footer, read header 
    struct tga_header header;
    fseek(f, 0, SEEK_SET);
    len = fread(&header, sizeof(struct tga_header), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    extra_size += header.ident_size;
    if (header.color_map_type == 1) {
        extra_size += header.color_map_length * (header.color_map_entry_size >> 3);
    }
    //judge by section size
    if ((header.bits_depth >> 3) * header.height * header.width
            + sizeof(struct tga_header) == filesize)
            return 0;
   
    return -EINVAL;
}

static void 
read_color_map(TGA *t, FILE *f)
{
    fseek(f, t->head.color_map_start, SEEK_SET);
    // fread(t-> t->head.color_map_entry_size, f);
}

static void 
read_uncompress_data(TGA *t, FILE *f)
{
    if (t->head.image_type == IMAGE_UNCONPRESS_RGB) {
        int pitch = (((((t->head.width + 3) >> 2) << 2) * 32 + 32 - 1) >> 5) << 2;
        for (int i = t->head.height-1; i >= 0; i --) {
            // fread(t->data + pitch * i, pitch, 1, f);
            for (int j = 0; j < t->head.width; j ++) {
                fread(t->data + pitch * i + j * 4, t->head.bits_depth>>3, 1, f);
            }
        }
    }
}

static struct pic* 
TGA_load(const char *filename)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    TGA *t = malloc(sizeof(TGA));
    p->pic = t;
    FILE *f = fopen(filename, "rb");
    fread(&t->head, sizeof(struct tga_header), 1, f);
    p->depth = 32;
    p->width = ((t->head.width + 3) >> 2) << 2;
    p->height = t->head.height;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->left = t->head.xstart;
    p->top = t->head.ystart;
    t->data = malloc(p->height * p->pitch);
    if (t->head.color_map_type) {
        read_color_map(t, f);
    }
    switch (t->head.image_type) {
        case IMAGE_UNCONPRESS_RGB:
            read_uncompress_data(t, f);
            break;
        default:
            break;
    }
    fclose(f);
    p->pixels = t->data;
    return p;
}

static void 
TGA_free(struct pic *p)
{
    TGA *t = (TGA *)p->pic;
    free(t);
    free(p);
}

static void 
TGA_info(FILE *f, struct pic* p)
{
    TGA *t = (TGA *)p->pic;
    fprintf(f, "TGA file formart:\n");
    fprintf(f, "-------------------------\n");
    fprintf(f, "\twidth %d: height %d\n", t->head.width, t->head.height);
    fprintf(f, "\tcolor map size %d\n", t->head.color_map_length);
}

static struct file_ops tga_ops = {
    .name = "TGA",
    .probe = TGA_probe,
    .load = TGA_load,
    .free = TGA_free,
    .info = TGA_info,
};

void TGA_init(void)
{
    file_ops_register(&tga_ops);
}