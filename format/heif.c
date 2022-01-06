#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "heif.h"
#include "file.h"
#include "basemedia.h"

static char* heif_types[] = {
    "heic",
    "heix",
    "hevc",
    "hevx"
};

static int
HEIF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct ftyp_box h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_brand == TYPE2UINT("ftyp")) {
        for (int i = 0; i < sizeof(heif_types)/sizeof(heif_types[0]); i ++) {
            if (h.minor_version == TYPE2UINT(heif_types[i])) {
                return 0;
            }
        }
    }

    return -EINVAL;
}

static void
read_iprp_box(FILE *f, struct iprp_box *b)
{
    fread(b, 12, 1, f);
    b->size = ntohl(b->size);
}


static void
read_meta_box(FILE *f)
{
    struct box meta, b;
    uint32_t type = read_box(f, &meta, -1);
    if (type != TYPE2UINT("meta")) {
        printf("error, it is not a meta after ftyp\n");
        return;
    }
    struct hdlr_box hdlr;
    struct pitm_box pitm;
    struct iloc_box iloc;
    struct iinf_box iinf;
    struct iprp_box iprp;
    uint32_t version;
    fread(&version, 4, 1, f);
    meta.size -= 12;
    while (meta.size) {
        type = read_box(f, &b, meta.size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case TYPE2UINT("hdlr"):
            read_hdlr_box(f, &hdlr);
            break;
        case TYPE2UINT("pitm"):
            read_pitm_box(f, &pitm);
            break;
        case TYPE2UINT("iloc"):
            read_iloc_box(f, &iloc);
            break;
        case TYPE2UINT("iinf"):
            read_iinf_box(f, &iinf);
            break;
        case TYPE2UINT("iprp"):
            read_iprp_box(f, &iprp);
            break;
        default:
            break;
        }
        meta.size -= b.size;

        printf("%s\n", UINT2TYPE(type));
    }
    
}

static struct pic* 
HEIF_load(const char *filename) {
    HEIF * h = calloc(1, sizeof(HEIF));
    struct pic *p = calloc(1, sizeof(struct pic));
    p->pic = h;
    FILE *f = fopen(filename, "rb");
    read_ftyp(f, &h->ftyp);
    read_meta_box(f);

    fclose(f);

    return p;
}

static void
HEIF_free(struct pic *p)
{
    HEIF * h = (HEIF *)p->pic;
    
    free(h);
    free(p);
}


static void
HEIF_info(FILE *f, struct pic* p)
{
    HEIF * h = (HEIF *)p->pic;
    fprintf(f, "HEIF file format:\n");
    fprintf(f, "-----------------------\n");
    char *s = UINT2TYPE(h->ftyp.minor_version);
    fprintf(f, "\tbrand: %s\n", s);
    free(s);

}




static struct file_ops heif_ops = {
    .name = "HEIF",
    .probe = HEIF_probe,
    .load = HEIF_load,
    .free = HEIF_free,
    .info = HEIF_info,
};

void HEIF_init(void)
{
    file_ops_register(&heif_ops);
}



