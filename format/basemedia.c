#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "byteorder.h"
#include "basemedia.h"



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


void
read_ftyp(FILE *f, void *d)
{
    struct ftyp_box *ftyp = (struct ftyp_box*)d;
    fread(ftyp, 12, 1, f);
    ftyp->size = SWAP(ftyp->size);
    if (ftyp->size > 12) {
        ftyp->compatible_brands = malloc(((ftyp->size - 12)));
        fread(ftyp->compatible_brands, 4, ((ftyp->size - 12)>>2), f);
    }
}

void 
print_box(FILE *f, void *d)
{
    struct box *b = (struct box *) d;
    char *s = UINT2TYPE(b->type);
    fprintf(f, "\"%s\":%d", s, b->size);
    free(s);
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
    uint8_t a = fgetc(f);
    b->offset_size = a >> 4;
    b->length_size = a & 0xf;
    a = fgetc(f);
    b->base_offset_size = a >> 4;
    fread(&b->item_count, 2, 1, f);
    b->item_count = SWAP(b->item_count);
    b->items = malloc(sizeof(struct item_location) * b->item_count);
    for (int i = 0; i < b->item_count; i ++) {
        fread(b->items + i, 2, 2, f);
        if (b->base_offset_size == 8) {
            fread(&b->items[i].base_offset, 8, 1, f);
        } else if (b->base_offset_size == 4) {
            fread(&b->items[i].base_offset, 4, 1, f);
        }
        fread(&b->items[i].extent_count, 2, 1, f);
        b->items[i].extent_count = SWAP(b->items[i].extent_count);
        b->items[i].extents = malloc(sizeof(struct item_extent) * b->items[i].extent_count);
        for (int j = 0; j < b->items[i].extent_count; j ++) {
            if (b->offset_size == 8) {
                fread(b->items[i].extents + j, 8, 1, f);
            } else if (b->offset_size == 4) {
                fread(b->items[i].extents + j, 4, 1, f);
            }
            if (b->length_size == 8) {
                fread(&b->items[i].extents[j].extent_length, 8, 1, f);
            } else if (b->length_size == 4) {
                fread(&b->items[i].extents[j].extent_length, 4, 1, f);
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
    fread(b, 14, 1, f);
    b->size = SWAP(b->size);
    b->entry_count = SWAP(b->entry_count);
    b->item_infos = malloc(sizeof(struct infe_box) * b->entry_count);
    for (int i = 0; i < b->entry_count; i ++) {
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
    fread(b, 4, 1, f);
    b->size = SWAP(b->size);
    b->from_item_id = SWAP(b->from_item_id);
    b->ref_count = SWAP(b->ref_count);
    b->to_item_ids = malloc(b->ref_count * 2);
    fread(b->to_item_ids, b->ref_count * 2, 1, f);
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
}
