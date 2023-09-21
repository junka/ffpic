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
    // size_t extra_size = 0;
    struct tga_footer tailer;
    fseek(f, -sizeof(struct tga_footer), SEEK_END);
    // size_t filesize = ftell(f) + sizeof(struct tga_footer);
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
    // extra_size += header.ident_size;
    // if (header.color_map_type == 1) {
    //     extra_size += header.color_map_length * (header.color_map_entry_size >> 3);
    // }
    //judge by section size
    if (header.bits_depth>>3 > 0 && header.bits_depth%8 == 0) 
        return 0;
   
    return -EINVAL;
}

static void 
read_color_map(TGA *t, FILE *f)
{
    int num = t->head.color_map_length;
    //a non-zero first entry index allows to store only a required part of the color map in the file
    if (t->head.color_map_first_index) {
        num -= t->head.color_map_first_index;
    }
    t->cmap = malloc(num * t->head.color_map_entry_size>>3);
    for (int i = 0; i < num; i ++) {
        fread(t->cmap + i, t->head.color_map_entry_size/8, 1, f);
    }
}

static void 
read_uncompress_data(TGA *t, FILE *f)
{
    int pitch = (((((t->head.width + 3) >> 2) << 2) * 32 + 32 - 1) >> 5) << 2;
    if (t->head.image_type == IMAGE_UNCONPRESS_RGB) {
        for (int i = t->head.height-1; i >= 0; i --) {
            if (t->head.bits_depth >16) { //24, 32
                for (int j = 0; j < t->head.width; j ++) {
                    fread(t->data + pitch * i + j * 4, t->head.bits_depth>>3, 1, f);
                }
            } else if (t->head.bits_depth == 16) {
                //take it as RGB555
                for (int j = 0; j < t->head.width; j ++) {
                    uint16_t svalue;
                    fread(&svalue, 2, 1, f);
                    t->data[pitch * i + j * 4] = (svalue >> 10) & 0x1F;
                    t->data[pitch * i + j * 4 + 1] = (svalue >> 5) & 0x1F;
                    t->data[pitch * i + j * 4 + 2] = (svalue) & 0x1F;
                }
            }
        }
    } else if (t->head.image_type == IMAGE_UNCOMPRESS_INDEXED) {
        for (int i = t->head.height-1; i >= 0; i --) {
            for (int j = 0; j < t->head.width; j ++) {
                int k = fgetc(f);
                t->data[pitch * i + j * 4] = t->cmap[k].r;
                t->data[pitch * i + j * 4 + 1] = t->cmap[k].g;
                t->data[pitch * i + j * 4 + 2] = t->cmap[k].b;
            }
        }
    }
}

static void 
read_compress_data(TGA *t, FILE *f)
{
    uint32_t vl;
    int rl = 0;
    int raw = 0;
    int pitch = (((((t->head.width + 3) >> 2) << 2) * 32 + 32 - 1) >> 5) << 2;
    if (t->head.image_type == IMAGE_RLE_RGB) {
        for (int i = t->head.height - 1; i >= 0; i --) {
            for (int j = 0; j < t->head.width; j ++) {
                if (t->head.bits_depth > 16) {
                    if (rl == 0 && raw == 0) {
                        vl = fgetc(f);
                        if (vl & 0x80) { //RLE
                            rl = (vl & 0x7F) + 1;
                            fread(&vl, (t->head.bits_depth>>3), 1, f);
                        } else { //RAW
                            raw = (vl + 1);
                        }
                    }
                    if (rl == 0 && raw != 0) {
                        fread(t->data +pitch * i + j * 4, (t->head.bits_depth>>3), 1, f);
                        raw --;
                    } else if (rl != 0 && raw == 0) {
                        *(uint32_t *)(t->data + pitch * i + j * 4) = vl;
                        rl --;
                    }
                
                }
            }
        }
    }
}

static struct pic* 
TGA_load(const char *filename)
{
    struct pic *p = pic_alloc(sizeof(TGA));
    TGA *t = p->pic;
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
        case IMAGE_UNCOMPRESS_INDEXED:
        case IMAGE_UNCONPRESS_GREY:
            read_uncompress_data(t, f);
            break;
        case IMAGE_RLE_RGB:
            read_compress_data(t, f);
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
    if(t->cmap)
        free(t->cmap);
    pic_free(p);
}

static void 
TGA_info(FILE *f, struct pic* p)
{
    TGA *t = (TGA *)p->pic;
    fprintf(f, "TGA file formart:\n");
    fprintf(f, "-------------------------\n");
    fprintf(f, "\twidth %d: height %d\n", t->head.width, t->head.height);
    fprintf(f, "\tdepth %d format %d\n", t->head.bits_depth, t->head.image_type);
    if (t->head.color_map_type) {
        fprintf(f, "\tcolormap size %d, entry bits %d\n", t->head.color_map_length, t->head.color_map_entry_size);
        fprintf(f, "\tcolormap first index at %u\n", t->head.color_map_first_index);
    }
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
