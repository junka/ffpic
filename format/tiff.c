
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
read_de(TIFF * t, FILE *f)
{
    struct tiff_directory_entry de;
    fread(&de, 12, 1, f);
    int tlen = 0;
    switch(de.type) {
        case TAG_BYTE:
            tlen = 1 * de.len;
            break;
        case TAG_SHORT:
            tlen = 2 * de.len;
            break;
        case TAG_LONG:
            tlen = 4 * de.len;
            break;
        case TAG_RATIONAL:
            break;
        case TAG_ASCII:
            break;
        default:
            break;
    }
    if (de.len <=4 ) {
        de.value = (uint8_t *)&de.offset;
    }
    else
    {
        de.value = malloc(de.len);
        fseek(f, de.offset, SEEK_SET);
        fread(de.value, de.len, 1, f);
    }
    switch(de.type) {
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
    t->ifd = malloc(sizeof(struct tiff_file_directory));
    fread(&t->ifd->num, 2, 1, f);
    for (int i = 0; i < t->ifd->num; i++) {
        read_de(t, f);
    }
    fread(&t->ifd->next_offset, 4, 1, f);
    if(t->ifd->next_offset) {
        fseek(f, t->ifd->next_offset, SEEK_SET);
        return 1;
    }
    return 0;
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
    int next = read_ifd(t, f);
    while (next) {
        next = read_ifd(t, f);
    }

    return p;
}

void 
TIFF_free(struct pic * p)
{
    TIFF * t = (TIFF *)p->pic;
    free(t);
    free(p);
}

void
TIFF_info(FILE *f, struct pic* p)
{
    TIFF * t = (TIFF *)p->pic;
    fprintf(f, "TIFF file format:\n");
    fprintf(f, "\tbyte order: %s\n", t->ifh.byteorder == 0x4D4D ? 
                    "big endian": "little endian");
    fprintf(f, "----------------------------------\n");
    fprintf(f, "\tifd %d\n", t->ifd->num);
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