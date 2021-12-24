#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "ico.h"
#include "file.h"
#include "vlog.h"

VLOG_REGISTER(ico, INFO);

static int
ICO_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(ico, "fail to open %s\n", filename);
        return -ENOENT;
    }
    struct ico_header head;
    int len = fread(&head, sizeof(struct ico_header), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (head.rsv_zero == 0 && (head.type == 1 || head.type == 2))
        return 0;

    return -EINVAL;
}

static struct pic* 
ICO_load(const char *filename)
{
    struct pic *p = (struct pic *)malloc(sizeof(struct pic));
    ICO *c = (ICO *)malloc(sizeof(ICO));
    p->pic = c;
    p->depth = 32;
    FILE *f = fopen(filename, "rb");
    fread(&c->head, sizeof(struct ico_header), 1, f);
    c->dir = (struct ico_directory *)malloc(sizeof(struct ico_directory) * c->head.num);
    c->images = (struct ico_image_data*)malloc(sizeof(struct ico_image_data) * c->head.num);
    for (int i = 0; i < c->head.num; i ++) {
        fread(c->dir + i, sizeof(struct ico_directory), 1, f);
    }

    for (int i = 0; i < c->head.num; i++) {
        fseek(f, c->dir[i].offset, SEEK_SET);
        fread(&c->images[i].bmpinfo, sizeof(struct icobmp_info_header), 1, f);
        /* for some reason, we can not trust the color_num in directory or in bmpinfo 
           check both of them */
        int color_n = c->dir[i].color_num == 0 ? c->images[i].bmpinfo.colors_used : 0;
        if (color_n > 0) {
            c->images[i].color = malloc(sizeof(struct icobmp_color_entry) * color_n);
            fread(c->images[i].color, sizeof(struct icobmp_color_entry), color_n, f);
        }
        int width = ((c->dir[i].width + 3) >> 2) << 2;
        int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
        c->images[i].data = malloc((c->images[i].bmpinfo.height >> 1) * pitch);
        int upper = c->dir[i].height > 0 ? c->dir[i].height - 1 : 0;
        int bottom = c->dir[i].height > 0 ? 0 : 1 - c->dir[i].height;
        int delta = c->dir[i].height > 0 ? -1 : 1;
        if (c->images[i].bmpinfo.bit_count <= 8 && color_n > 0) {
            for (int j = upper; j >= bottom; j += delta) {
                for (int k = 0; k < c->dir[i].width; k ++) {
                    uint8_t a = fgetc(f);
                    if (c->images[i].bmpinfo.bit_count == 8) {
                        c->images[i].data[pitch * j + k * 4] = c->images[i].color[a].blue;
                        c->images[i].data[pitch * j + k * 4 + 1] = c->images[i].color[a].green;
                        c->images[i].data[pitch * j + k * 4 + 2] = c->images[i].color[a].red;
                        c->images[i].data[pitch * j + k * 4 + 3] = c->images[i].color[a].alpha;
                    } else if (c->images[i].bmpinfo.bit_count == 4) {
                        c->images[i].data[pitch * j + k * 4] = c->images[i].color[(a >> 4) &0xF].blue;
                        c->images[i].data[pitch * j + k * 4 + 1] = c->images[i].color[(a >> 4) & 0xF].green;
                        c->images[i].data[pitch * j + k * 4 + 2] = c->images[i].color[(a >> 4) & 0xF].red;
                        c->images[i].data[pitch * j + k * 4 + 3] = c->images[i].color[(a >> 4) & 0xF].alpha;
                        k ++;
                        c->images[i].data[pitch * j + k * 4] = c->images[i].color[a &0xF].blue;
                        c->images[i].data[pitch * j + k * 4 + 1] = c->images[i].color[a & 0xF].green;
                        c->images[i].data[pitch * j + k * 4 + 2] = c->images[i].color[a & 0xF].red;
                        c->images[i].data[pitch * j + k * 4 + 3] = c->images[i].color[a & 0xF].alpha;
                    } else if (c->images[i].bmpinfo.bit_count == 1) {
                        VERR(ico, "do we really get this?\n");
                    }
                }
            }
        } else if (c->images[i].bmpinfo.bit_count > 8 && color_n == 0)  {
            for (int j = upper; j >= bottom; j += delta) {
                for (int k = 0; k < c->dir[i].width; k ++) {
                    // for (int n = 0; n < (c->dir[i].depth >> 3); n ++) {
                    //    c->images[i].data[pitch * j + k * 4 + n] = (uint8_t)fgetc(f);
                    // }
                    fread(c->images[i].data + pitch * j + k * 4, (c->dir[i].depth >> 3), 1, f);
                }
            }
            
        }
        int and_with = ((c->dir[i].width/8 + 3) >> 2) << 2;
        for (int j = upper; j >= bottom; j += delta) {
            for (int k = 0; k < and_with; k ++) {
                uint8_t v = fgetc(f);
                for (int l = 0; l < 8; l ++) {
                    if (k * 8 + l < c->dir[i].width) {
                        for (int m = 0; m < (c->dir[i].depth >> 3); m ++) {
                            c->images[i].data[pitch * j + (k * 8 + l) * 4 + m] ^= (((v >> (7 - l)) & 0x01)? 0xFF : 0);
                        }
                    }
                }
            }
        }
    }
    fclose(f);

    /* select the most high qualit for now */
    int select = 0;
    int dep = 0;
    int width = 0;
    if (c->head.num > 1) {
        for (int i = 0; i < c->head.num; i ++) {
            if (c->dir[i].depth > dep) {
                select = i;
                dep = c->dir[i].depth;
                width = c->dir[i].width;
            } else if (c->dir[i].depth == dep) {
                if (c->dir[i].width > width) {
                    select = i;
                    dep = c->dir[i].depth;
                    width = c->dir[i].width;
                }
            }
        }
        VINFO(ico, "multiple images, select %dth image", select + 1);
    }
    p->width = ((c->dir[select].width + 3) >> 2) << 2;
    p->height = c->dir[select].height;
    p->depth = 32;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    if (c->dir[select].depth > 8 && (c->dir[select].color_num == 0? c->images[select].bmpinfo.colors_used:0))
        p->depth = c->dir[select].depth;
    p->pixels = c->images[select].data;
    // #include "utils.h"
    // hexdump(stdout, "decoded data", p->pixels, 160);

    return p;
}

static void 
ICO_free(struct pic *p)
{
    ICO *c = (ICO *)p->pic;
    for (int i = 0; i < c->head.num; i ++) {
        if (c->images[i].bmpinfo.colors_used > 0) {
            if (c->images[i].color)
                free(c->images[i].color);
        }
        if (c->images[i].data)
            free(c->images[i].data);
    }
    if (c->images)
        free(c->images);
    if (c->dir)
        free(c->dir);
    free(c);
    free(p);
}

static void 
ICO_info(FILE *f, struct pic* p)
{
    ICO *c = (ICO *)p->pic;
    fprintf(f, "%s file format\n", c->head.type == 1 ? "ICO" : "CUR");
    fprintf(f, "\timage nums: %d\n", c->head.num);
    for (int i = 0; i < c->head.num; i ++) {
        fprintf(f, "\t%dth image:\n", i + 1);
        fprintf(f, "\twidth %d, height %d\n", c->dir[i].width, c->dir[i].height);
        fprintf(f, "\tdir color_num %d, planes %d\n", c->dir[i].color_num, c->dir[i].planes);
        fprintf(f, "\tdir depth %d, size %d, offset %d\n", c->dir[i].depth, c->dir[i].size, c->dir[i].offset);
        fprintf(f, "\tbmp depth %d, compression %d\n", c->images[i].bmpinfo.bit_count,
                                            c->images[i].bmpinfo.compression);
        fprintf(f, "\tbmp width %d, height %d, color_used %d\n", c->images[i].bmpinfo.width,
                                            c->images[i].bmpinfo.height, c->images[i].bmpinfo.colors_used);
        fprintf(f, "-------------------------\n");
    }
}

static struct file_ops ico_ops = {
    .name = "ICO",
    .probe = ICO_probe,
    .load = ICO_load,
    .free = ICO_free,
    .info = ICO_info,
};

void 
ICO_init(void)
{
    file_ops_register(&ico_ops);
}