#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

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
    b->size = ntohl(b->size);
    if (b->size == 1) {
        //large size
        uint32_t large_size;
        uint32_t value;
        fread(&large_size, 4, 1, f);
        fread(&value, 4, 1, f);
        if (large_size) {
            printf("should error here for now\n");
        }
        b->size = ntohl(value);
    } else if (b->size == 0) {
        //last box, read to end of box
        b->size = blen;
    }
    return b->type;
}


void
read_ftyp(FILE *f, void *d)
{
    struct ftyp_box *ftyp = (struct ftyp_box*)d;
    fread(ftyp, 12, 1, f);
    ftyp->size = ntohl(ftyp->size);
    if (ftyp->size > 12) {
        ftyp->compatible_brands = malloc(4 * ((ftyp->size - 12)>>2));
        fread(ftyp->compatible_brands, 4, ((ftyp->size - 12)>>2), f);
    }
}

void
read_meta_box(FILE *f)
{

}

void 
print_box(FILE *f, struct box *b)
{
    char *s = UINT2TYPE(b->type);
    fprintf(f, "size %d, type \"%s\"\n", b->size, s);
    free(s);
}


void
read_mvhd_box(FILE *f, struct mvhd_box *b)
{
    fread(b, 4, 3, f);
    b->size = ntohl(b->size);
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
    b->size = ntohl(b->size);
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
    b->size = ntohl(b->size);
    uint8_t a = fgetc(f);
    b->offset_size = a >> 4;
    b->length_size = a & 0xf;
    a = fgetc(f);
    b->base_offset_size = a >> 4;
    fread(&b->item_count, 2, 1, f);
    b->item_count = ntohs(b->item_count);
    b->items = malloc(sizeof(struct item_location) * b->item_count);
    for (int i = 0; i < b->item_count; i ++) {
        fread(b->items + i, 2, 2, f);
        if (b->base_offset_size == 8) {
            fread(&b->items[i].base_offset, 8, 1, f);
        } else if (b->base_offset_size == 4) {
            fread(&b->items[i].base_offset, 4, 1, f);
        }
        fread(&b->items[i].extent_count, 2, 1, f);
        b->items[i].extent_count = ntohs(b->items[i].extent_count);
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
    b->size = ntohl(b->size);
    b->item_id = ntohs(b->item_id);
}

void
read_iinf_box(FILE *f, struct iinf_box *b)
{
    fread(b, 14, 1, f);
    b->size = ntohl(b->size);
    b->entry_count = ntohs(b->entry_count);
    b->item_infos = malloc(sizeof(struct infe_box) * b->entry_count);
    for (int i = 0; i < b->entry_count; i ++) {
        fread(b->item_infos + i, 16, 1, f);
        b->item_infos[i].size = ntohl(b->item_infos[i].size);
        fseek(f, b->item_infos[i].size - 16, SEEK_CUR);
    }
}

