#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "byteorder.h"
#include "basemedia.h"
#include "utils.h"


uint8_t
read_u8(FILE *f)
{
    return fgetc(f);
}

uint16_t
read_u16(FILE *f)
{
    uint16_t a;
    if (fread(&a, 2, 1, f)!= 1) {
        return -1;
    }
    a = SWAP(a);
    return a;
}

uint32_t
read_u32(FILE *f)
{
    uint32_t a;
    if (fread(&a, 4, 1, f)!=1) {
        return -1;
    }
    a = SWAP(a);
    return a;
}

uint64_t
read_u64(FILE *f)
{
    uint64_t a;
    if (fread(&a, 4, 1, f)!= 1) {
        return -1;
    }
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
    FFREAD(b, sizeof(struct box), 1, f);
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


int
read_mvhd_box(FILE *f, struct mvhd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('m', 'v', 'h', 'd'));
    printf("mvhd size %d, version %d\n", b->size, b->version);
    if (b->version == 0) {
        b->ctime = read_u32(f);
        b->mtime = read_u32(f);
        b->timescale = read_u32(f);
        b->duration = read_u32(f);
    } else if (b->version == 1) {
        b->ctime = read_u64(f);
        b->mtime = read_u64(f);
        b->timescale = read_u32(f);
        b->duration = read_u64(f);
    }

    b->rate = read_u32(f);
    b->volume = read_u16(f);
    b->reserved = read_u16(f);
    FFREAD(b->rsd, 4, 2, f);
    FFREAD(b->matrix, 4, 9, f);
    FFREAD(b->pre_defined, 4, 6, f);
    b->next_track_id = read_u32(f);
    printf("rate %d, volume %d, trak_id %d\n", b->rate, b->volume, b->next_track_id);
    return b->size;
}


int
read_hdlr_box(FILE *f, struct hdlr_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('h', 'd', 'l', 'r'));
    b->pre_defined = read_u32(f);
    FFREAD(&b->handler_type, 4, 1, f);
    FFREAD(b->reserved, 4, 3, f);
    if (b->size - 32 - 1) {
        b->name = malloc(b->size - 32 -1);
        FFREAD(b->name, 1, b->size - 32, f);
    } else {
        fseek(f, 1, SEEK_CUR);
        b->name = NULL;
    }
    return b->size;
}

int
read_iloc_box(FILE *f, struct iloc_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'l', 'o', 'c'));
    uint8_t a = read_u8(f);
    b->offset_size = a >> 4;
    b->length_size = a & 0xf;
    a = read_u8(f);
    b->base_offset_size = a >> 4;
    b->index_size = a & 0xf;

    b->item_count = read_u16(f);
    printf("iloc: item count %d\n", b->item_count);

    b->items = calloc(b->item_count, sizeof(struct item_location));
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
        b->items[i].extents = calloc(b->items[i].extent_count, sizeof(struct item_extent));
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
    return b->size;
}



int
read_pitm_box(FILE *f, struct pitm_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('p', 'i', 't', 'm'));
    b->item_id = read_u16(f);
    printf("PITM: primary %d\n", b->item_id);
    return b->size;
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
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'n', 'f', 'e'));
    if (b->version == 0 || b->version == 1) {
        b->item_id = read_u16(f);
        b->item_protection_index = read_u16(f);
        read_till_null(f, b->item_name);
        read_till_null(f, b->content_type);
        read_till_null(f, b->content_encoding);
    }
    if (b->version == 1) {
        FFREAD(&b->item_type, 4, 1, f);
    } else if (b->version >= 2) {
        if (b->version == 2) {
            b->item_id = read_u16(f);
        } else if (b->version == 3) {
            b->item_id = read_u32(f);
        }
        b->item_protection_index = read_u16(f);
        FFREAD(&b->item_type, 4, 1, f);
        int l = read_till_null(f, b->item_name);
        printf("infe name %s, type %s, id %d\n", b->item_name, type2name(b->item_type), b->item_id);
        if (b->item_type == TYPE2UINT("mime")) {
            int m = read_till_null(f, b->content_type);
            b->content_encoding = malloc(b->size - 20 - l - m);
            read_till_null(f, b->content_encoding);
            // fgetc(f);
        } else if (b->item_type == TYPE2UINT("uri ")) {
            read_till_null(f, b->content_type);
        } else {
            if ((b->size > 20 + l) && b->version == 2) {
                fseek(f, b->size - 20 -l, SEEK_CUR);
            } else if ((b->size > 22 + l) && b->version == 3){
                fseek(f, b->size - 22 -l, SEEK_CUR);
            }
        }
    }
    return b->size;
}

int
read_iinf_box(FILE *f, struct iinf_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'i', 'n', 'f'));
    if (b->version > 0) {
        b->entry_count = read_u32(f);
    } else {
        b->entry_count = read_u16(f);
    }
    b->item_infos = calloc(b->entry_count, sizeof(struct infe_box));
    printf("IINF: entry_count %d\n", b->entry_count);
    for (uint32_t i = 0; i < b->entry_count; i ++) {
        read_infe_box(f, b->item_infos + i);
    }
    return b->size;
}

static int
read_frma_box(FILE *f, struct frma_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('f', 'r', 'm', 'a'));
    b->data_format = read_u32(f);
    return b->size;
}

static int
read_schm_box(FILE *f, struct schm_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 'c', 'h', 'm'));
    b->scheme_type = read_u32(f);
    b->scheme_version = read_u32(f);
    b->scheme_uri = malloc(b->size - 20);
    FFREAD(b->scheme_uri, 1, b->size - 20, f);
    return b->size;
}

int
read_sinf_box(FILE *f, struct sinf_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('s', 'i', 'n', 'f'));
    read_frma_box(f, &b->original_format);

    // fread(&b->IPMP_descriptors, 12, 1, f);
    // b->IPMP_descriptors.size = SWAP(b->IPMP_descriptors.size);
    // b->IPMP_descriptors.ipmp_descs = malloc(b->IPMP_descriptors.size - 12);
    // fread(b->IPMP_descriptors.ipmp_descs, b->IPMP_descriptors.size - 12, 1, f);
    read_schm_box(f,&b->scheme_type_box);

    // fread(b->info)
    // read_box()
    return b->size;
}

static int
read_itemtype_box(FILE *f, struct itemtype_ref_box *b, int version)
{
    FFREAD(b, 4, 2, f);
    b->size = SWAP(b->size);
    if (version == 0) {
        b->from_item_id = read_u16(f);
    } else if (version == 1) {
        b->from_item_id = read_u32(f);
    }
    b->ref_count = read_u16(f);
    printf("from_item_id %d, itemtype count %d\n",
           b->from_item_id, b->ref_count);
    b->to_item_ids = calloc(b->ref_count, sizeof(uint32_t));
    for (uint16_t i = 0; i < b->ref_count; i ++) {
        if (version == 0) {
            b->to_item_ids[i] = read_u16(f);
        } else if (version == 1) {
            b->to_item_ids[i] = read_u32(f);
        }
        printf("itemtype to_item_ids %d\n", b->to_item_ids[i]);
    }

    return b->size;
}

int
read_iref_box(FILE *f, struct iref_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'r', 'e', 'f'));
    int sz = b->size - 12;
    int n = 1;

    while (sz) {
        if (b->refs_count == 0) {
            b->refs = calloc(1, sizeof(struct itemtype_ref_box));
        } else {
            b->refs = realloc(b->refs, sizeof(struct itemtype_ref_box) *
                                           (b->refs_count+1));
        }
        int l = read_itemtype_box(f, b->refs + b->refs_count, b->version);
        b->refs_count++;
        sz -= l;
    }
    return b->size;
}



static int
read_ispe_box(FILE *f, struct ispe_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 's', 'p', 'e'));
    b->image_width = read_u32(f);
    b->image_height = read_u32(f);
    printf("ISPE: width %d, height %d\n", b->image_width, b->image_height);
    return b->size;
}

static int
read_pixi_box(FILE *f, struct pixi_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('p', 'i', 'x', 'i'));
    b->num_channels = read_u8(f);
    b->bits_per_channel = malloc(b->num_channels);
    FFREAD(b->bits_per_channel, 1, b->num_channels, f);
    return b->size;
}

static int
read_colr_box(FILE *f, struct colr_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('c', 'o', 'l', 'r'));
    FFREAD(&b->color_type, 4, 1, f);
    printf("COLR: %s\n", type2name(b->color_type));
    if (b->color_type == FOURCC2UINT('n', 'c', 'l', 'x')) {
        // on screen colors
        b->color_primaries = read_u16(f);
        b->transfer_characteristics = read_u16(f);
        b->matrix_coefficients = read_u16(f);
        b->full_range_flag = fgetc(f) >> 7;
    } else if (b->color_type == FOURCC2UINT('r', 'I', 'C', 'C') || b->color_type == FOURCC2UINT('p', 'r', 'o', 'f')) {
        // ICC profile
        int len = b->size - 4 - 8;
        printf("profile len %d\n", len);
        // uint8_t *data = malloc(len);
        // fread(data, 1, len, f);
        // hexdump(stdout, "icc profile", "", data, len);
        // free(data);
        fseek(f, len, SEEK_CUR);
    }
    return b->size;
}

extern int read_hvcc_box(FILE *f, struct box **bn);
extern int read_av1c_box(FILE *f, struct box **bn);

static int
read_ipco_box(FILE *f, struct ipco_box *b)
{
    struct box * cc = NULL;
    struct ispe_box *ispe;
    struct pixi_box *pixi;
    struct colr_box *colr;

    FFREAD_BOX_ST(b, f, FOURCC2UINT('i', 'p', 'c', 'o'));
    int s = b->size - 8;
    int n = 0;
    printf("IPCO: total length %d\n", s);
    while (s > 0) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("IPCO: type %s, size %d\n", UINT2TYPE(type), p.size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case FOURCC2UINT('h', 'v', 'c', 'C'):
                s -= read_hvcc_box(f, &cc);
                b->property[n++] = cc;
                break;
            case FOURCC2UINT('a', 'v', '1', 'C'):
                s -= read_av1c_box(f, &cc);
                b->property[n++] = cc;
                break;
            case FOURCC2UINT('i', 's', 'p', 'e'):
                ispe = calloc(1, sizeof(struct ispe_box));
                s -= read_ispe_box(f, ispe);
                b->property[n++] = (struct box *)ispe;
                break;
            case FOURCC2UINT('p', 'i', 'x', 'i'):
                pixi = calloc(1, sizeof(struct pixi_box));
                s -= read_pixi_box(f, pixi);
                b->property[n++] = (struct box *)pixi;
                break;
            case FOURCC2UINT('c', 'o', 'l', 'r'):
                colr = calloc(1, sizeof(struct colr_box));
                s -= read_colr_box(f, colr);
                b->property[n++] = (struct box *)colr;
                break;
            default:
                s -= (p.size);
                fseek(f, p.size, SEEK_CUR);
                break;
        }
    }
    b->n_property = n;
    return b->size;
}

static int
read_ipma_box(FILE *f, struct ipma_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'p', 'm', 'a'));
    b->entry_count = read_u32(f);
    b->entries = calloc(b->entry_count, sizeof(struct ipma_item));
    printf("IPMA: entry_count %d\n", b->entry_count);
    for (uint32_t i = 0; i < b->entry_count; i ++) {
        if (b->version < 1) {
            b->entries[i].item_id = read_u16(f);
        } else {
            b->entries[i].item_id = read_u32(f);
        }
        b->entries[i].association_count = read_u8(f);
        printf("IPMA: item_id %d, association_count %d\n", b->entries[i].item_id, b->entries[i].association_count);
        b->entries[i].property_index = calloc(b->entries[i].association_count, 2);
        for (int j = 0; j < b->entries[i].association_count; j ++) {
            //inculding one bit for essential
            if (b->flags & 0x1) {
                b->entries[i].property_index[j] = read_u16(f);
            } else {
                b->entries[i].property_index[j] = read_u8(f);
                if (b->entries[i].property_index[j] & 0x80) {
                    b->entries[i].property_index[j] =
                        (0x8000 | (b->entries[i].property_index[j] & 0x7F));
                }
            }
            printf("IPMA: essential %d, association %d\n",
                   b->entries[i].property_index[j] >> 15,
                   b->entries[i].property_index[j] & 0x7FFF);
        }
    }
    return b->size;
}

int
read_iprp_box(FILE *f, struct iprp_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('i', 'p', 'r', 'p'));
    int s = b->size - 8;
    struct box *cc = NULL;

    while (s > 0) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("IPRP: type %s, size %d\n", UINT2TYPE(type), p.size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case FOURCC2UINT('i', 'p', 'c', 'o'):
                s -= read_ipco_box(f, &b->ipco);
                break;
            case FOURCC2UINT('i', 'p', 'm', 'a'):
                s -= read_ipma_box(f, &b->ipma);
                break;
            // case FOURCC2UINT('h', 'v', 'c', 'C'):
            //     s -= read_hvcc_box(f, &cc);
            //     break;
            case FOURCC2UINT('i', 's', 'p', 'e'):
                s -= read_ispe_box(f, &b->ispe);
                break;
            case FOURCC2UINT('p', 'i', 'x', 'i'):
                s -= read_pixi_box(f, &b->pixi);
                break;
            default:
                s -= (p.size);
                fseek(f, p.size, SEEK_CUR);
                break;
        }
    }
    return b->size;
}


int
read_mdat_box(FILE *f, struct mdat_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('m', 'd', 'a', 't'));
    // fseek(f, b->size - 8, SEEK_CUR);
    b->data = malloc(b->size - 8);
    // printf("mdat size %d\n", b->size - 8);
    FFREAD(b->data, 1, b->size - 8, f);
    // hexdump(stdout, "mdat ", "", b->data, 256);
    return b->size;
}

int read_dref_box(FILE *f, struct dref_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('d', 'r', 'e', 'f'));
    b->entry_count = read_u32(f);
    // printf("dinf %d, dref %d, count %d\n", b->size, b->size, b->entry_count);
    b->entries = calloc(b->entry_count, sizeof(struct DataEntryBox));
    for (int i = 0; i < (int)b->entry_count; i++) {
        FFREAD(b->entries + i, 4, 3, f);
        b->entries[i].size = SWAP(b->entries[i].size);
        // printf("data entry size %d, type %s\n", b->entries[i].size,
        //        type2name(b->entries[i].type));
        if (b->entries[i].type == TYPE2UINT("url ")) {
            b->entries[i].location = malloc(b->entries[i].size - 12);
            // printf("%s\n", b->entries[i].location);
            FFREAD(b->entries[i].location, 1, b->entries[i].size - 12, f);
        } else if (b->entries[i].type == TYPE2UINT("urn ")) {
            b->entries[i].name = malloc(b->entries[i].size - 12);
            FFREAD(b->entries[i].name, 1, b->entries[i].size - 12, f);
        }
    }
    return b->size;
}

int read_dinf_box(FILE *f, struct dinf_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('d', 'i', 'n', 'f'));
    read_dref_box(f, &b->dref);
    return b->size;
}

static int
read_tkhd_box(FILE *f, struct tkhd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('t', 'k', 'h', 'd'));
    if (b->version == 0) {
        b->creation_time = read_u32(f);
        b->modification_time = read_u32(f);
        b->track_id = read_u32(f);
        b->reserved = read_u32(f);
        b->duration = read_u32(f);
    } else {
        b->creation_time = read_u64(f);
        b->modification_time = read_u64(f);
        b->track_id = read_u32(f);
        b->reserved = read_u32(f);
        b->duration = read_u64(f);
    }
    FFREAD(b->rsd, 4, 2, f);
    b->layer = read_u16(f);
    b->alternate_group = read_u16(f);
    b->volume = read_u16(f);
    FFREAD(&b->reserved0, 2, 1, f);
    FFREAD(&b->matrix, 4, 9, f);
    b->width = read_u32(f);
    b->height = read_u32(f);
    printf("width %d, height %d\n",b->width, b->height);
    return b->size;
}

int read_vmhd_box(FILE *f, struct vmhd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('v', 'm', 'h', 'd'));
    b->graphicsmode = read_u16(f);
    for (int i = 0; i < 3; i++) {
        b->opcolor[i] = read_u16(f);
    }
    return b->size;
}

int
read_mdhd_box(FILE *f, struct mdhd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('m', 'd', 'h', 'd'));
    if (b->version == 0) {
        b->creation_time = read_u32(f);
        b->modification_time = read_u32(f);
        b->timescale = read_u32(f);
        b->duration = read_u32(f);
    } else {
        b->creation_time = read_u64(f);
        b->modification_time = read_u64(f);
        b->timescale = read_u32(f);
        b->duration = read_u64(f);
    }
    b->lan = read_u16(f);
    b->pre_defined = read_u16(f);
    return b->size;
}

int read_stsd_box(FILE *f, struct stsd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 'd'));
    b->entry_count = read_u32(f);
    // printf("stsd entry_count %d\n", b->entry_count);
    b->entries = calloc(b->entry_count, sizeof(struct SampleEntry));
    for (int i = 0; i < b->entry_count; i++) {
        FFREAD(b->entries + i, 4, 2, f);
        b->entries[i].size = SWAP(b->entries[i].size);
        printf("sample entry %s: %d\n", type2name(b->entries[i].type), b->size);
        if (b->entries[i].type == FOURCC2UINT('h', 'v', 'c', '1')) {
            //decode_hvc1();
        }
        fseek(f, b->entries[i].size - 8, SEEK_CUR);
    }
    return b->size;
}

int read_stdp_box(FILE *f, struct stdp_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 'd', 'p'));
    return b->size;
}

int read_stts_box(FILE *f, struct stts_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 't', 's'));
    b->entry_count = read_u32(f);
    printf("%s size %d, entry_count %d\n", type2name(b->type), b->size, b->entry_count);
    if (b->entry_count) {
        b->sample_count = calloc(b->entry_count, 4);
        b->sample_delta = calloc(b->entry_count, 4);
        for (int i = 0; i < b->entry_count; i++) {
            b->sample_count[i] = read_u32(f);
            b->sample_delta[i] = read_u32(f);
        }
    }
    return b->size;
}

int read_ctts_box(FILE *f, struct ctts_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('c', 't', 't', 's'));
    b->entry_count = read_u32(f);
    b->sample_count = calloc(b->entry_count, 4);
    b->sample_offset = calloc(b->entry_count, 4);
    for (int i = 0; i < b->entry_count; i++) {
        b->sample_count[i] = read_u32(f);
        b->sample_offset[i] = read_u32(f);
    }
    return b->size;
}

int read_stsc_box(FILE *f, struct stsc_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 'c'));
    b->entry_count = read_u32(f);
    printf("%s size %d, entry_count %d\n", type2name(b->type), b->size,
           b->entry_count);

    b->first_chunk = calloc(b->entry_count, 4);
    b->sample_per_chunk = calloc(b->entry_count, 4);
    b->sample_description_index = calloc(b->entry_count, 4);
    for (int i = 0; i < b->entry_count; i++) {
        b->first_chunk[i] = read_u32(f);
        b->sample_per_chunk[i] = read_u32(f);
        b->sample_description_index[i] = read_u32(f);
        printf("first chunk %d, sample per chunk %d, sample description index %d\n", b->first_chunk[i], b->sample_per_chunk[i], b->sample_description_index[i]);
    }
    return b->size;
}

int read_stco_box(FILE *f, struct stco_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 'c', 'o'));
    b->entry_count = read_u32(f);
    printf("%s size %d, entry_count %d\n", type2name(b->type), b->size,
           b->entry_count);

    b->chunk_offset = calloc(b->entry_count, 4);
    for (int i = 0; i < b->entry_count; i++) {
        b->chunk_offset[i] = read_u32(f);
    }
    return b->size;
}

int read_stsz_box(FILE *f, struct stsz_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 'z'));
    b->sample_size = read_u32(f);
    b->sample_count = read_u32(f);
    printf("%s size %d, sample_size %d, sample_count %d\n", type2name(b->type), b->size,
           b->sample_size, b->sample_count);
    if (b->sample_size == 0) {
        b->entry_size = calloc(b->sample_count, 4);
        for (int i = 0; i < b->sample_count; i++) {
            b->entry_size[i] = read_u32(f);
            // printf("entry size %d\n", b->entry_size[i]);
        }
    }
    return b->size;
}

int read_stss_box(FILE *f, struct stss_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 's'));
    b->entry_count = read_u32(f);
    b->sample_number = calloc(b->entry_count, 4);
    for (int i = 0; i < b->entry_count; i++) {
        b->sample_number[i] = read_u32(f);
    }
    return b->size;
}

int read_SampleGroupEntry(FILE *f, struct SampleGroupEntry *e, int len)
{
    uint32_t grouping_type;
    FFREAD(&grouping_type, 4, 1, f);
    printf("SampleGroupEntry %d: %d\n", (grouping_type), len);
    fseek(f, len - 4, SEEK_CUR);
    return 0;
}

int read_sgpd_box(FILE *f, struct sgpd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 'g', 'p', 'd'));
    FFREAD(&b->grouping_type, 4, 1, f);
    // b->grouping_type = SWAP(b->grouping_type);
    if (b->version == 1) {
        b->default_length = read_u32(f);
    } else if (b->version >= 2) {
        b->default_sample_description_index = read_u32(f);
    }
    printf("version %d, name %s, default_length %d\n", b->version,
           type2name(b->grouping_type), b->default_length);

    b->entry_count = read_u32(f);
    printf("size %d, name %s, count %d\n", b->size, type2name(b->grouping_type),
           b->entry_count);
    b->description_length = calloc(b->entry_count, 4);
    b->entries = calloc(b->entry_count, sizeof(struct SampleGroupEntry));
    for (int i = 0; i < b->entry_count; i++) {
        if (b->version == 1) {
            if (b->default_length == 0) {
                b->description_length[i] = read_u32(f);
            } else {
                b->description_length[i] = b->default_length;
            }
        }
        // SampleGroupEntry
        read_SampleGroupEntry(f, b->entries + i, b->description_length[i]);
    }
    return b->size;
}

int read_sbgp_box(FILE *f, struct sbgp_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 'b', 'g', 'p'));
    b->grouping_type = read_u32(f);
    if (b->version == 1) {
        b->grouping_type_parameter = read_u32(f);
    }
    b->entry_count = read_u32(f);
    printf("size %d, name %s, count %d\n", b->size, type2name(b->grouping_type), b->entry_count);
    if (b->entry_count) {
        b->sample_count = calloc(b->entry_count, 4);
        b->group_description_index = calloc(b->entry_count, 4);
        for (int i = 0; i < b->entry_count; i++) {
            b->sample_count[i] = read_u32(f);
            b->group_description_index[i] = read_u32(f);
        }
    }
    return b->size;
}

int read_stbl_box(FILE *f, struct stbl_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('s', 't', 'b', 'l'));
    read_stsd_box(f, &b->stsd);
    int s = b->size - 8 - b->stsd.size;
    while (s > 0) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("%s, stbl left %d\n", type2name(type), s);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case FOURCC2UINT('s', 't', 'd', 'p'):
            read_stdp_box(f, &b->stdp);
            break;
        case FOURCC2UINT('s', 't', 't', 's'):
            read_stts_box(f, &b->stts);
            break;
        case FOURCC2UINT('s', 't', 's', 'c'):
            read_stsc_box(f, &b->stsc);
            break;
        case FOURCC2UINT('s', 't', 'c', 'o'):
            read_stco_box(f, &b->stco);
            break;
        case FOURCC2UINT('c', 't', 't', 's'):
            read_ctts_box(f, &b->ctts);
            break;
        case FOURCC2UINT('s', 't', 's', 'z'):
        case FOURCC2UINT('s', 't', 'z', '2'):
            read_stsz_box(f, &b->stsz);
            break;
        case FOURCC2UINT('s', 't', 's', 's'):
            read_stss_box(f, &b->stss);
            break;
        case FOURCC2UINT('s', 'g', 'p', 'd'):
            read_sgpd_box(f, &b->sgpd);
            break;
        case FOURCC2UINT('s', 'b', 'g', 'p'):
            read_sbgp_box(f, &b->sbgp);
            break;
        default:
            fseek(f, p.size, SEEK_CUR);
            break;
        }
        s -= (p.size);
    }
    return b->size;
}

int read_minf_box(FILE *f, struct minf_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('m', 'i', 'n', 'f'));
    int s = b->size - 8;
    while (s > 0) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("%s: left %d\n", type2name(type), s);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case FOURCC2UINT('v', 'm', 'h', 'd'):
            read_vmhd_box(f, &b->vmhd);
            break;
        case FOURCC2UINT('s', 'm', 'h', 'd'):
            // read_smhd_box();
            break;
        case FOURCC2UINT('h', 'm', 'h', 'd'):
            // read_hmhd_box();
            break;
        case FOURCC2UINT('s', 't', 'h', 'd'):
            // read_sthd_box();
            break;
        case FOURCC2UINT('n', 'm', 'h', 'd'):
            // read_nmhd_box();
            break;
        case FOURCC2UINT('d', 'i', 'n', 'f'):
            read_dinf_box(f, &b->dinf);
            break;
        case FOURCC2UINT('s', 't', 'b', 'l'):
            read_stbl_box(f, &b->stbl);
            break;
        default:
            fseek(f, p.size, SEEK_CUR);
            break;
        }
        s -= (p.size);
    }
    return b->size;
}

int read_mdia_box(FILE *f, struct mdia_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('m', 'd', 'i', 'a'));
    read_mdhd_box(f, &b->mdhd);
    read_hdlr_box(f, &b->hdlr);
    int s = b->size - b->mdhd.size - b->hdlr.size - 8;
    struct box p;
    uint32_t type = read_box(f, &p, s);
    fseek(f, -8, SEEK_CUR);
    if (type == FOURCC2UINT('e', 'l', 'n', 'g')) {
        // read_elng_box (f, &b->elng);
    }
    read_minf_box(f, &b->minf);
    assert(b->size - b->minf.size - b->mdhd.size - b->hdlr.size == 8);
    return b->size;
}

int read_idat_box(FILE *f, struct idat_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('i', 'd', 'a', 't'));
    b->data = malloc(b->size - 8);
    FFREAD(b->data, 1, b->size - 8, f);
    return b->size;
}

int
read_meta_box(FILE *f, struct meta_box *meta)
{
    FFREAD_BOX_FULL(meta, f, FOURCC2UINT('m', 'e', 't', 'a'));
    assert(meta->type == TYPE2UINT("meta"));
    int size = meta->size -= 12;
    while (size > 0) {
        struct box b;
        uint32_t type = read_box(f, &b, size);
        printf("META: %s, size %d\n", UINT2TYPE(type), b.size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case FOURCC2UINT('h', 'd', 'l', 'r'):
            read_hdlr_box(f, &meta->hdlr);
            break;
        case FOURCC2UINT('p', 'i', 't', 'm'):
            read_pitm_box(f, &meta->pitm);
            break;
        case FOURCC2UINT('i', 'l', 'o', 'c'):
            read_iloc_box(f, &meta->iloc);
            break;
        case FOURCC2UINT('i', 'i', 'n', 'f'):
            read_iinf_box(f, &meta->iinf);
            break;
        case FOURCC2UINT('i', 'p', 'r', 'p'):
            read_iprp_box(f, &meta->iprp);
            break;
        case FOURCC2UINT('i', 'r', 'e', 'f'):
            read_iref_box(f, &meta->iref);
            break;
        case FOURCC2UINT('d', 'i', 'n', 'f'):
            read_dinf_box(f, &meta->dinf);
            break;
        case FOURCC2UINT('i', 'd', 'a', 't'):
            read_idat_box(f, &meta->idat);
            break;
        default:
            fseek(f, b.size, SEEK_CUR);
            break;
        }
        size -= b.size;
        printf("META: %s, read %d, left %d\n", UINT2TYPE(type), b.size, size);
    }
    return meta->size;
}

int read_trak_box(FILE *f, struct trak_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('t', 'r', 'a', 'k'));
    read_tkhd_box(f, &b->tkhd);
    int s = b->size - 8 - b->tkhd.size;
    while (s) {
        printf("trak left %d\n", s);
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("%s\n", type2name(type));
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case FOURCC2UINT('t', 'r', 'e', 'f'):
                // read_tref_box();
                break;
            case FOURCC2UINT('t', 'r', 'g', 'r'):
                // read_trgr_box();
                break;
            case FOURCC2UINT('e', 'd', 't', 's'):
                // read_edts_box();
                break;
            case FOURCC2UINT('m', 'e', 't', 'a'):
                // read_meta_box();
                break;
            case FOURCC2UINT('m', 'd', 'i', 'a'):
                read_mdia_box(f, &b->mdia);
                break;
            default:
                fseek(f, p.size, SEEK_CUR);
                break;
        }
        s -= (p.size);
    }
    return b->size;
}

int
read_moov_box(FILE *f, struct moov_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('m', 'o', 'o', 'v'));
    read_mvhd_box(f, &b->mvhd);

    int s = b->size - b->mvhd.size - 8;
    // printf("moov left size %d\n", s);
    while (s) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("%s: moov left %d\n", type2name(type), s);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case FOURCC2UINT('t', 'r', 'a', 'k'):
                b->trak_num++;
                b->trak = realloc(b->trak, sizeof(struct trak_box)*b->trak_num);
                read_trak_box(f, b->trak+b->trak_num-1);
                break;
            default:
                fseek(f, p.size, SEEK_CUR);
                break;
        }
        s -= (p.size);
    }
    return b->size;
}