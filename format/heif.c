#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "heif.h"
#include "file.h"
#include "byteorder.h"
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

static int
read_hvcc_box(FILE *f, struct hvcC_box *b)
{
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);

    printf("hvcc %d\n", b->size);
    fread(&b->configurationVersion, 23, 1, f);
    b->nal_arrays = malloc(b->num_of_arrays * sizeof(struct nal_arr));
    for (int i = 0; i < b->num_of_arrays; i ++) {
        fread(b->nal_arrays + i, 3, 1, f);
        b->nal_arrays[i].numNalus = SWAP(b->nal_arrays[i].numNalus);
        b->nal_arrays[i].nals = malloc(b->nal_arrays[i].numNalus * sizeof(struct nalus));
        for (int j = 0; j < b->nal_arrays[i].numNalus; j ++) {
            fread(b->nal_arrays[i].nals+j, 2, 1, f);
            b->nal_arrays[i].nals[j].unit_length = SWAP(b->nal_arrays[i].nals[j].unit_length);
            fread(&b->nal_arrays[i].nals[j].unit_length, b->nal_arrays[i].nals[j].unit_length, 1, f);
        }
    }
    return b->size;
}

static int
read_ispe_box(FILE *f, struct ispe_box *b)
{
    fread(b, 20, 1, f);
    b->size = SWAP(b->size);
    b->image_height = SWAP(b->image_height);
    b->image_width = SWAP(b->image_width);
    return b->size;
}

static int
read_ipco_box(FILE *f, struct ipco_box *b)
{
    struct hvcC_box *hvcc;
    struct ispe_box *ispe;
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);
    int s = b->size - 8;
    int n = 0;
    while (s) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case TYPE2UINT("hvcC"):
                hvcc = malloc(sizeof(struct hvcC_box));
                s -= read_hvcc_box(f, hvcc);
                b->property[n++] = (struct box *)hvcc;
                break;
            case TYPE2UINT("ispe"):
                ispe = malloc(sizeof(struct ispe_box));
                s -= read_ispe_box(f, ispe);
                b->property[n++] = (struct box *)ispe;
                break;
            default:
                break;
        }
        // printf("ipco left %d\n", s);
    }
    return b->size;
}

static void
read_ipma_box(FILE *f, struct ipma_box *b)
{
    fread(b, 16, 1, f);
    b->size = SWAP(b->size);
    b->entry_count = SWAP(b->entry_count);
    b->entries = malloc(b->entry_count * sizeof(struct ipma_item));
    for (int i = 0; i < b->entry_count; i ++) {
        if (b->version < 1) {
            fread(b->entries + i, 2, 1, f);
        } else {
            fread(b->entries + i, 4, 1, f);
        }
        b->entries[i].association_count = fgetc(f);
        b->entries[i].association = malloc(2 * b->entries[i].association_count);
        for (int j = 0; j < b->entries[i].association_count; j ++) {
            if (b->flags & 0x1) {
                fread(b->entries[i].association + j, 2, 1, f);
            } else {
                fread(b->entries[i].association + j, 1, 1, f);
            }
        }
    }
}

static void
read_iprp_box(FILE *f, struct iprp_box *b)
{
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);
    read_ipco_box(f, &b->ipco);
    read_ipma_box(f, &b->ipma);
}


static void
read_meta_box(FILE *f, struct meta_box *meta)
{
    struct box b;
    uint32_t type = read_full_box(f, meta, -1);
    if (type != TYPE2UINT("meta")) {
        printf("error, it is not a meta after ftyp\n");
        return;
    }
    int size = meta->size -= 12;
    while (size) {
        type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case TYPE2UINT("hdlr"):
            read_hdlr_box(f, &meta->hdlr);
            break;
        case TYPE2UINT("pitm"):
            read_pitm_box(f, &meta->pitm);
            break;
        case TYPE2UINT("iloc"):
            read_iloc_box(f, &meta->iloc);
            break;
        case TYPE2UINT("iinf"):
            read_iinf_box(f, &meta->iinf);
            break;
        case TYPE2UINT("iprp"):
            read_iprp_box(f, &meta->iprp);
            break;
        case TYPE2UINT("iref"):
            read_iref_box(f, &meta->iref);
            break;
        default:
            break;
        }
        size -= b.size;
        // printf("%s, left %d\n", UINT2TYPE(type), size);
    }
    
}

static struct pic*
HEIF_load(const char *filename) {
    HEIF * h = calloc(1, sizeof(HEIF));
    struct pic *p = calloc(1, sizeof(struct pic));
    p->pic = h;
    FILE *f = fopen(filename, "rb");
    read_ftyp(f, &h->ftyp);
    read_meta_box(f, &h->meta);

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
    char *s1 = UINT2TYPE(h->ftyp.minor_version);
    char *s2 = UINT2TYPE(h->ftyp.compatible_brands[1]);
    fprintf(f, "\tbrand: %s, compatible %s\n", s1, s2);
    free(s1);
    free(s2);

    fprintf(f, "meta box --------------\n");
    fprintf(f, "\t");
    print_box(f, &h->meta.hdlr);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.pitm);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iloc);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iinf);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iprp);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iref);
    fprintf(f, "\n");
    fprintf(f, "mdat box --------------\n");
    
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



