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

static void
decode_mdat(HEIF * h, struct mdat_box *b)
{
    
}

static struct pic*
HEIF_load(const char *filename) {
    HEIF * h = calloc(1, sizeof(HEIF));
    struct pic *p = calloc(1, sizeof(struct pic));
    p->pic = h;
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size -= read_ftyp(f, &h->ftyp);
    h->mdat = malloc(sizeof(struct mdat_box));
    struct box b;
    while (size) {
        uint32_t type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case TYPE2UINT("meta"):
            read_meta_box(f, &h->meta);
            break;
        case TYPE2UINT("mdat"):
            h->mdat_num ++;
            h->mdat = realloc(h->mdat, h->mdat_num * sizeof(struct mdat_box));
            read_mdat_box(f, h->mdat + h->mdat_num - 1);
            break;
        default:
            break;
        }
        size -= b.size;
        printf("%s, read %d, left %d\n", UINT2TYPE(type), b.size, size);
    }
    fclose(f);

    // extract some info from meta box
    for (int i = 0; i < 2; i ++) {
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            p->width = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width;
            p->height = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height;
        }
    }

    // process mdata
    decode_mdat(h, h->mdat);


    return p;
}

static void
HEIF_free(struct pic *p)
{
    HEIF * h = (HEIF *)p->pic;

    if (h->ftyp.compatible_brands)
        free(h->ftyp.compatible_brands);

    struct meta_box *m = &h->meta;
    if (m->hdlr.name)
        free(m->hdlr.name);
    for (int i = 0; i < m->iloc.item_count; i ++) {
        free(m->iloc.items[i].extents);
    }
    free(m->iloc.items);

    for (int i = 0; i < m->iinf.entry_count; i ++) {
        if (m->iinf.item_infos[i].content_encoding)
            free(m->iinf.item_infos[i].content_encoding);
    }
    free(m->iinf.item_infos);

    for (int i = 0; i < 2; i ++) {
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("hvcC")) {
            struct hvcC_box *hvcc = (struct hvcC_box *)m->iprp.ipco.property[i];
            for (int j = 0; j < hvcc->num_of_arrays; j ++) {
                free(hvcc->nal_arrays[j].nals);                
            }
            free(hvcc->nal_arrays);
        }
        free(m->iprp.ipco.property[i]);
    }

    for (int i = 0; i < m->iprp.ipma.entry_count; i ++) {
        free(m->iprp.ipma.entries[i].association);
    }
    free(m->iprp.ipma.entries);

    for (int i = 0; i < m->iref.refs_count; i ++) {
        free(m->iref.refs[i].to_item_ids);
    }
    free(m->iref.refs);

    for (int i = 0; i < h->mdat_num; i ++) {
        free(h->mdat[i].data);
    }
    free(h->mdat);
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
    s1 = UINT2TYPE(h->meta.hdlr.handler_type);
    fprintf(f, " pre_define=%d,handle_type=\"%s\"", h->meta.hdlr.pre_defined, s1);
    free(s1);
    if (h->meta.hdlr.name) {
        fprintf(f, ",name=%s", h->meta.hdlr.name);
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.pitm);
    fprintf(f, " item_id=%d", h->meta.pitm.item_id);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iloc);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        fprintf(f, "\t\t");
        if (h->meta.iloc.version == 1) {
            fprintf(f, "construct_method=%d,", h->meta.iloc.items[i].construct_method);
        }
        fprintf(f, "item_id=%d,data_ref_id=%d,base_offset=%lld,extent_count=%d\n", 
            h->meta.iloc.items[i].item_id, h->meta.iloc.items[i].data_ref_id,
            h->meta.iloc.items[i].base_offset, h->meta.iloc.items[i].extent_count);
        for (int j = 0; j < h->meta.iloc.items[i].extent_count; j ++) {
            fprintf(f, "\t\t\t");
            if (h->meta.iloc.version == 1) {
                fprintf(f, "extent_id=%lld,", h->meta.iloc.items[i].extents[j].extent_index);
            }
            fprintf(f, "extent_offset=%lld,extent_length=%lld\n", h->meta.iloc.items[i].extents[j].extent_offset,
                h->meta.iloc.items[i].extents[j].extent_length);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iinf);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iinf.entry_count; i ++) {
        fprintf(f, "\t\t");
        print_box(f, &h->meta.iinf.item_infos[i]);
        s1 = UINT2TYPE(h->meta.iinf.item_infos[i].item_type);
        fprintf(f, " item_id=%d,item_protection_index=%d,item_type=%s",
            h->meta.iinf.item_infos[i].item_id, h->meta.iinf.item_infos[i].item_protection_index,
            s1);
        free(s1);
        fprintf(f, "\n");
    }

    fprintf(f, "\t");
    print_box(f, &h->meta.iprp);
    fprintf(f, "\n\t");
    fprintf(f, "\t");
    print_box(f, &h->meta.iprp.ipco);
    fprintf(f, "\n\t");
    for (int i = 0; i < 2; i ++) {
        fprintf(f, "\t\t");
        print_box(f, h->meta.iprp.ipco.property[i]);
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            fprintf(f, ", width %d, height %d", 
                ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width,
                ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("hvcC")) {

        }
        fprintf(f, "\n\t");
    }
    fprintf(f, "\t");
    print_box(f, &h->meta.iprp.ipma);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iref);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iref.refs_count; i ++) {
        fprintf(f, "\t\t");
        fprintf(f, "from_item_id=%d,ref_count=%d", h->meta.iref.refs[i].from_item_id,
           h->meta.iref.refs[i].ref_count);
        for (int j = 0; j < h->meta.iref.refs[i].ref_count; j ++) {
            fprintf(f, ",to_item=%d",  h->meta.iref.refs[i].to_item_ids[j]);
        }
        fprintf(f, "\n");
    }
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



