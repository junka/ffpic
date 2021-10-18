
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "file.h"
#include "tiff.h"

int 
TIFF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct tiff_file_header h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.byteorder == 0x4D4D || h.byteorder == 0x4949) {
        if (h.version == 42) {
            return 0;
        }
    }

    return -EINVAL;
}


void
read_de(struct tiff_directory_entry *de, FILE *f)
{
    
    fread(de, 12, 1, f);
    int tlen = 0;
    switch(de->type) {
        case TAG_BYTE:
            tlen = 1 * de->len;
            break;
        case TAG_SHORT:
            tlen = 2 * de->len;
            break;
        case TAG_LONG:
            tlen = 4 * de->len;
            break;
        case TAG_RATIONAL:
            break;
        case TAG_ASCII:
            break;
        default:
            break;
    }
    if (de->len <=4 ) {
        de->value = (uint8_t *)&de->offset;
    }
    else
    {
        de->value = malloc(de->len);
        fseek(f, de->offset, SEEK_SET);
        fread(de->value, de->len, 1, f);
    }
    switch(de->type) {
        case TAG_BYTE:
            break;
        case TAG_SHORT:
            break;
        case TAG_LONG:
            break;
        case TAG_RATIONAL:
            break;
        case TAG_ASCII:
            break;
        default:
            break;
    }

}

int 
read_ifd(TIFF *t, FILE *f)
{
    t->ifd_num ++;
    if (t->ifd == NULL)
        t->ifd = malloc(sizeof(struct tiff_file_directory));
    else {
        t->ifd = realloc(t->ifd, t->ifd_num * sizeof(struct tiff_file_directory));
    }
    fread(&t->ifd[t->ifd_num-1].num, 2, 1, f);
    t->ifd[t->ifd_num-1].de = malloc(sizeof(struct tiff_directory_entry) * t->ifd[t->ifd_num-1].num);
    for (int i = 0; i < t->ifd[t->ifd_num-1].num; i++) {
        read_de(t->ifd[t->ifd_num-1].de + i, f);
    }
    fread(&t->ifd[t->ifd_num-1].next_offset, 4, 1, f);
    if(t->ifd[t->ifd_num-1].next_offset) {
        fseek(f, t->ifd[t->ifd_num-1].next_offset, SEEK_SET);
        read_ifd(t, f);
    }

    return 0;
}

int read_int_from_de(struct tiff_directory_entry *de)
{
    switch(de->type) {
        case TAG_BYTE:
        case TAG_SHORT:
        case TAG_LONG:
            return de->offset;
        default:
            break;
    }
    return *de->value;
}

static void 
tiff_compose_image_from_de(TIFF *t)
{
    for (int i = 0; i < t->ifd_num; i ++) {
        for(int j = 0; j < t->ifd[i].num; j ++) {
            switch (t->ifd[i].de[j].tag) {
                case TID_IMAGEWIDTH:
                    t->ifd[i].width = read_int_from_de(&t->ifd[i].de[j]);
                    break;
                case TID_IMAGEHEIGHT:
                    t->ifd[i].height = read_int_from_de(&t->ifd[i].de[j]);
                    break;
                case TID_SAMPLESPERPIXEL:
                    t->ifd[i].depth = read_int_from_de(&t->ifd[i].de[j]);
                    break;
                case TID_COMPRESSION:
                    t->ifd[i].compression = read_int_from_de(&t->ifd[i].de[j]);
                    break;
                default:
                    break;
            }
        }
    }
}

struct pic*
TIFF_load(const char *filename)
{
    TIFF * t = (TIFF *)malloc(sizeof(TIFF));
    struct pic *p = malloc(sizeof(struct pic));
    FILE *f = fopen(filename, "rb");
    p->pic = t;
    fread(&t->ifh, sizeof(struct tiff_file_header), 1, f);
    fseek(f, t->ifh.start_offset, SEEK_SET);
    read_ifd(t, f);
    
    fclose(f);
    tiff_compose_image_from_de(t);

    return p;
}

void 
TIFF_free(struct pic * p)
{
    TIFF * t = (TIFF *)p->pic;
    free(t);
    if (t->ifd)
        free(t->ifd);
    free(p);
}

void
TIFF_info(FILE *f, struct pic* p)
{
    TIFF * t = (TIFF *)p->pic;
    fprintf(f, "TIFF file format:\n");
    fprintf(f, "\tbyte order: %s\n", t->ifh.byteorder == 0x4D4D ? 
                    "big endian": "little endian");
    fprintf(f, "\tIFD num: %d\n", t->ifd_num);
    fprintf(f, "----------------------------------\n");
    fprintf(f, "\tfirst IFD: DE num %d\n", t->ifd->num);
    fprintf(f, "\tfirst IFD: width %d, height %d, depth %d, compression %d\n",
         t->ifd->width, t->ifd->height, t->ifd->depth, t->ifd->compression);
    fprintf(f, "\n");
}

static struct file_ops png_ops = {
    .name = "TIFF",
    .probe = TIFF_probe,
    .load = TIFF_load,
    .free = TIFF_free,
    .info = TIFF_info,
};

void 
TIFF_init(void)
{
    file_ops_register(&png_ops);
}