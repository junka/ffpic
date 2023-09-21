#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "byteorder.h"
#include "basemedia.h"


uint8_t
read_u8(FILE *f)
{
    return fgetc(f);
}

uint16_t
read_u16(FILE *f)
{
    uint16_t a;
    fread(&a, 2, 1, f);
    a = SWAP(a);
    return a;
}

uint32_t
read_u32(FILE *f)
{
    uint32_t a;
    fread(&a, 4, 1, f);
    a = SWAP(a);
    return a;
}

uint64_t
read_u64(FILE *f)
{
    uint64_t a;
    fread(&a, 4, 1, f);
    a = SWAP(a);
    return a;
}

/**
 aligned(8) class Box (unsigned int(32) boxtype, 
    optional unsigned int(8)[16] extended_type) { 
    unsigned int(32) size; 
    unsigned int(32) type = boxtype; 
    if (size==1) { 
        unsigned int(64) largesize; 
    } else if (size==0) { 
        // box extends to end of file 
    }
    if (boxtype==‘uuid’) { 
        unsigned int(8)[16] usertype = extended_type;
    }
 }*/
uint32_t
read_box(FILE *f, void * d, int blen)
{
    struct box* b = (struct box*)d;
    fread(b, sizeof(struct box), 1, f);
    b->size = SWAP(b->size);
    if (b->size == 1) {
        //large size
        uint32_t large_size;
        uint32_t value;
        fread(&large_size, 4, 1, f);
        fread(&value, 4, 1, f);
        if (large_size) {
            printf("should error here for now\n");
        }
        b->size = SWAP(value);
    } else if (b->size == 0) {
        //last box, read to end of box
        b->size = blen;
    }
    return b->type;
}

uint32_t
read_full_box(FILE *f, void * d, int blen)
{
    uint32_t type = read_box(f, d, blen);
    struct full_box* b = (struct full_box*)d;
    b->version = fgetc(f);
    b->flags = fgetc(f) << 16 | fgetc(f) <<8| fgetc(f);
    return type;
}


int
read_ftyp(FILE *f, void *d)
{
    struct ftyp_box *ftyp = (struct ftyp_box*)d;
    int len;
    len = fread(ftyp, 12, 1, f);
    if (len < 1) {
        return -1;
    }
    ftyp->size = SWAP(ftyp->size);
    if (ftyp->size > 12) {
        ftyp->compatible_brands = malloc(((ftyp->size - 12)));
        len = fread(ftyp->compatible_brands, 4, ((ftyp->size - 12)>>2), f);
        if ((uint32_t)len < ((ftyp->size - 12)>>2)) {
            return -1;
        }
    }
    return ftyp->size;
}

void 
print_box(FILE *f, void *d)
{
    struct box *b = (struct box *) d;
    if (b->size || b->type)
        fprintf(f, "\"%s\":%d", UINT2TYPE(b->type), b->size);
}


void
read_mvhd_box(FILE *f, struct mvhd_box *b)
{
    fread(b, 4, 3, f);
    b->size = SWAP(b->size);
    if (b->version == 0) {
        fread(&b->ctime, 4, 1, f);
        fread(&b->mtime, 4, 1, f);
        fread(&b->timescale, 4, 1, f);
        fread(&b->duration, 4, 1, f);
    } else if (b->version == 1) {
        fread(&b->ctime, 28, 1, f);
    }
    fread(&b->rate, 80, 1, f);
}


void
read_hdlr_box(FILE *f, struct hdlr_box *b)
{
    fread(b, 4, 8, f);
    b->size = SWAP(b->size);
    if (b->size - 32 - 1) {
        b->name = malloc(b->size - 32 -1);
        fread(b->name, b->size - 32, 1, f);
    } else {
        fseek(f, 1, SEEK_CUR);
        b->name = NULL;
    }
}


void
read_iloc_box(FILE *f, struct iloc_box *b)
{
    fread(b, 4, 3, f);
    b->size = SWAP(b->size);
    uint8_t a = read_u8(f);
    b->offset_size = a >> 4;
    b->length_size = a & 0xf;
    a = read_u8(f);
    b->base_offset_size = a >> 4;
    b->index_size = a & 0xf;

    b->item_count = read_u16(f);

    b->items = malloc(sizeof(struct item_location) * b->item_count);
    for (int i = 0; i < b->item_count; i ++) {
        b->items[i].item_id = read_u16(f);
        if (b->version == 1) {
            b->items[i].construct_method = read_u16(f);
        }
        b->items[i].data_ref_id = read_u16(f);
        if (b->base_offset_size == 4) {
            b->items[i].base_offset =  read_u32(f);
        } else if (b->base_offset_size == 8) {
            b->items[i].base_offset =  read_u64(f);
        }
        b->items[i].extent_count = read_u16(f);
        b->items[i].extents = malloc(sizeof(struct item_extent) * b->items[i].extent_count);
        for (int j = 0; j < b->items[i].extent_count; j ++) {
            if (b->version == 1 && b->index_size > 0) {
                if (b->index_size == 4) {
                    b->items[i].extents[j].extent_index = read_u32(f);
                } else if (b->index_size == 8) {
                    b->items[i].extents[j].extent_index = read_u64(f);
                } 
            }
            if (b->offset_size == 4) {
                b->items[i].extents[j].extent_offset = read_u32(f);
            } else if (b->offset_size == 8) {
                b->items[i].extents[j].extent_offset = read_u64(f);
            }
            if (b->length_size == 4) {
                b->items[i].extents[j].extent_length = read_u32(f);
            } else if (b->length_size == 8) {
                b->items[i].extents[j].extent_length = read_u64(f);
            }
        }
    }
}



void
read_pitm_box(FILE *f, struct pitm_box *b)
{
    fread(b, 14, 1, f);
    b->size = SWAP(b->size);
    b->item_id = SWAP(b->item_id);
}

int
read_till_null(FILE *f, char *str)
{
    int i = 0;
    char c = fgetc(f);
    i ++;
    while (c != '\0') {
        str[i-1] = c;
        c = fgetc(f);
        i ++;
    }
    str[i-1] = '\0';
    return i;
}

static int
read_infe_box(FILE *f, struct infe_box *b)
{
    fread(b, 12, 1, f);
    b->size = SWAP(b->size);
    if (b->version == 0 || b->version == 1) {
        fread(&b->item_id, 2, 1, f);
        b->item_id = SWAP(b->item_id);
        fread(&b->item_protection_index, 2, 1, f);
        b->item_protection_index = SWAP(b->item_protection_index);
        read_till_null(f, b->item_name);
        read_till_null(f, b->content_type);
    }
    if (b->version == 1) {
        fread(&b->item_type, 4, 1, f);
        b->item_type = SWAP(b->item_type);

    } else if (b->version == 2) {
        fread(&b->item_id, 2, 1, f);
        b->item_id = SWAP(b->item_id);
        fread(&b->item_protection_index, 2, 1, f);
        b->item_protection_index = SWAP(b->item_protection_index);
        fread(&b->item_type, 4, 1, f);
        int l = read_till_null(f, b->item_name);
        if (b->item_type == TYPE2UINT("mime")) {
            int m = read_till_null(f, b->content_type);
            b->content_encoding = malloc(b->size - 20 - l - m);
            read_till_null(f, b->content_encoding);
            // fgetc(f);
        } else if (b->item_type == TYPE2UINT("uri ")) {
            read_till_null(f, b->content_type);
        }
    }
    return b->size;
}

void
read_iinf_box(FILE *f, struct iinf_box *b)
{
    fread(b, 12, 1, f);
    b->size = SWAP(b->size);
    if (b->version > 0) {
        fread(&b->entry_count, 4, 1, f);
        b->entry_count = SWAP(b->entry_count);
    } else {
        uint16_t count;
        fread(&count, 2, 1, f);
        b->entry_count = SWAP(count);
    }
    b->item_infos = malloc(sizeof(struct infe_box) * b->entry_count);
    for (uint32_t i = 0; i < b->entry_count; i ++) {
        read_infe_box(f, b->item_infos + i);
    }
}


void
read_sinf_box(FILE *f, struct sinf_box *b)
{
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);
    fread(&b->original_format, 12, 1, f);
    b->original_format.size = SWAP(b->original_format.size);

    // fread(&b->IPMP_descriptors, 12, 1, f);
    // b->IPMP_descriptors.size = SWAP(b->IPMP_descriptors.size);
    // b->IPMP_descriptors.ipmp_descs = malloc(b->IPMP_descriptors.size - 12);
    // fread(b->IPMP_descriptors.ipmp_descs, b->IPMP_descriptors.size - 12, 1, f);

    fread(&b->scheme_type_box, 20, 1, f);
    b->scheme_type_box.size = SWAP(b->scheme_type_box.size);
    if (b->scheme_type_box.size - 20) {
        b->scheme_type_box.scheme_uri = malloc(b->scheme_type_box.size - 20);
        fread(b->scheme_type_box.scheme_uri, 1, b->scheme_type_box.size - 20, f);
    }

    // fread(b->info)
    // read_box()
}

static int
read_itemtype_box(FILE *f, struct itemtype_ref_box *b)
{
    fread(b, 12, 1, f);
    b->size = SWAP(b->size);
    b->from_item_id = SWAP(b->from_item_id);
    b->ref_count = SWAP(b->ref_count);
    b->to_item_ids = malloc(b->ref_count * 2);
    fread(b->to_item_ids, b->ref_count * 2, 1, f);
    for (uint32_t i = 0; i < b->ref_count; i ++) {
        b->to_item_ids[i] = SWAP(b->to_item_ids[i]);
    }

    return b->size;
}

void
read_iref_box(FILE *f, struct iref_box *b)
{
    fread(b, 12, 1, f);
    b->size = SWAP(b->size);
    int sz = b->size - 12;
    int n = 1;
    b->refs = malloc(sizeof(struct itemtype_ref_box));
    while (sz) {
        b->refs = realloc(b->refs, sizeof(struct itemtype_ref_box) * n);
        int l = read_itemtype_box(f, b->refs);
        sz -= l;
        n ++;
    }
    b->refs_count = n - 1;
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
read_pixi_box(FILE *f, struct pixi_box *b)
{
    fread(b, 13, 1, f);
    b->size = SWAP(b->size);
    b->bits_per_channel = malloc(b->num_channels);
    fread(b->bits_per_channel, b->num_channels, 1, f);
    return b->size;
}

static int
read_ipco_box(FILE *f, struct ipco_box *b, read_box_callback cb)
{
    struct box * cc = NULL;
    struct ispe_box *ispe;
    struct pixi_box *pixi;
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);
    int s = b->size - 8;
    int n = 0;
    while (s > 0) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("type %s, size %d\n", UINT2TYPE(type), p.size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case TYPE2UINT("hvcC"):
                s -= cb(f, &cc);
                b->property[n++] = cc;
                break;
            case TYPE2UINT("av1C"):
                s -= cb(f, &cc);
                b->property[n++] = cc;
                break;
            case TYPE2UINT("ispe"):
                ispe = malloc(sizeof(struct ispe_box));
                s -= read_ispe_box(f, ispe);
                b->property[n++] = (struct box *)ispe;
                break;
            case TYPE2UINT("pixi"):
                pixi = malloc(sizeof(struct pixi_box));
                s -= read_pixi_box(f, pixi);
                b->property[n++] = (struct box *)pixi;
                break;
            default:
                // assert(0);
                s -= p.size;
                fseek(f, p.size, SEEK_CUR);
                break;
        }
    }
    b->n_prop = n;
    return b->size;
}

static void
read_ipma_box(FILE *f, struct ipma_box *b)
{
    fread(b, 16, 1, f);
    b->size = SWAP(b->size);
    b->entry_count = SWAP(b->entry_count);
    b->entries = malloc(b->entry_count * sizeof(struct ipma_item));
    for (uint32_t i = 0; i < b->entry_count; i ++) {
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
                b->entries[i].association[j] = SWAP(b->entries[i].association[j]);
            } else {
                b->entries[i].association[j] = fgetc(f);
                if (b->entries[i].association[j] & 0x80) {
                    b->entries[i].association[j] = (0x8000 | (b->entries[i].association[j] & 0x7F));
                }
            }
        }
    }
}

void
read_iprp_box(FILE *f, struct iprp_box *b, read_box_callback cb)
{
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);
    read_ipco_box(f, &b->ipco, cb);
    read_ipma_box(f, &b->ipma);
}


void
read_mdat_box(FILE *f, struct mdat_box *b)
{
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);
    // fseek(f, b->size - 8, SEEK_CUR);
    b->data = malloc(b->size - 8);
    // printf("mdat size %d\n", b->size - 8);
    fread(b->data, 1, b->size - 8, f);
    // hexdump(stdout, "mdat ", "", b->data, 256);
}
