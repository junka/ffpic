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
print_box(FILE *f, struct box *b)
{
    char *s = UINT2TYPE(b->type);
    fprintf(f, "size %d, type \"%s\"\n", b->size, s);
    free(s);
}