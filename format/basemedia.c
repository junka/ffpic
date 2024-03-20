#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "byteorder.h"
#include "basemedia.h"
#include "utils.h"

#include "vlog.h"

VLOG_REGISTER(basemedia, DEBUG)

uint8_t
read_u8(FILE *f)
{
    return (uint8_t)fgetc(f);
}

uint16_t
read_u16(FILE *f)
{
    uint16_t a = (uint16_t)fgetc(f) << 8 | fgetc(f);
    return a;
}

uint32_t
read_u32(FILE *f)
{
    uint32_t a = (uint32_t)fgetc(f) << 24 | fgetc(f) << 16 |
         fgetc(f) << 8 | fgetc(f);
    return a;
}

uint64_t
read_u64(FILE *f)
{
    uint64_t a;
    if (fread(&a, 8, 1, f)!= 1) {
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
read_box(FILE *f, void * d)
{
    struct box* b = (struct box*)d;
    b->size = read_u32(f);
    FFREAD(&b->type, 4, 1, f);
    if (b->size == 1) {
        //large size
        b->size = read_u64(f);
    } else if (b->size == 0) {
        // last box, read to end of file
        size_t cur = ftell(f);
        fseek(f, 0, SEEK_END);
        size_t end = ftell(f);
        fseek(f, cur, SEEK_SET);
        b->size = end - cur + 8;
    }
    return b->type;
}

uint32_t
probe_box(FILE *f, void *d)
{
    struct box *b = (struct box *)d;
    b->size = read_u32(f);
    FFREAD(&b->type, 4, 1, f);
    if (b->size == 1) {
        // large size
        b->size = read_u64(f);
        fseek(f, -8, SEEK_CUR);
    } else if (b->size == 0) {
        // last box, read to end of file
        size_t cur = ftell(f);
        fseek(f, 0, SEEK_END);
        size_t end = ftell(f);
        fseek(f, cur, SEEK_SET);
        b->size = end - cur + 8;
    }
    fseek(f, -8, SEEK_CUR);
    return b->type;
}

uint32_t
read_full_box(FILE *f, void * d)
{
    uint32_t type = read_box(f, d);
    struct full_box* b = (struct full_box*)d;
    b->version = fgetc(f);
    b->flags = fgetc(f) << 16 | fgetc(f) <<8| fgetc(f);
    return type;
}


int
read_ftyp(FILE *f, void *d)
{
    struct ftyp_box *ftyp = (struct ftyp_box*)d;

    FFREAD(ftyp, 12, 1, f);
    ftyp->size = SWAP(ftyp->size);
    VDBG(basemedia, "size %d, %s", ftyp->size, type2name(ftyp->major_brand));
    if (ftyp->size > 12) {
        ftyp->compatible_brands = malloc(((ftyp->size - 12)));
        FFREAD(ftyp->compatible_brands, 4, ((ftyp->size - 12)>>2), f);
    }
    return ftyp->size;
}

void 
print_box(FILE *f, void *d)
{
    struct box *b = (struct box *) d;
    if (b->size || b->type)
        fprintf(f, "\"%s\":%" PRIu64 "", UINT2TYPE(b->type), b->size);
}


int
read_mvhd_box(FILE *f, struct mvhd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('m', 'v', 'h', 'd'));
    VDBG(basemedia, "mvhd size %" PRIu64 ", version %d", b->size, b->version);
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
    VDBG(basemedia, "rate %d, volume %d, trak_id %d", b->rate, b->volume, b->next_track_id);
    return b->size;
}


int
read_hdlr_box(FILE *f, struct hdlr_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('h', 'd', 'l', 'r'));
    VDBG(basemedia, "HDLR: size %"PRIu64"", b->size);
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
    VDBG(basemedia, "iloc: item count %d", b->item_count);

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
    VDBG(basemedia, "PITM: primary %d", b->item_id);
    return b->size;
}

int
read_till_null(FILE *f, char **str)
{
    int i = 0;
    while (fgetc(f) != '\0') {
        i ++;
    }
    int len = i+1;
    char *buf = malloc(i+1);
    fseek(f, -i-1, SEEK_CUR);
    char c;
    i = 0;
    while ((c = fgetc(f)) != 0) {
        buf[i++] = c;
    }
    buf[i] = '\0';
    *str = buf;
    return len;
}

static int
read_infe_box(FILE *f, struct infe_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'n', 'f', 'e'));
    if (b->version == 0 || b->version == 1) {
        b->item_id = read_u16(f);
        b->item_protection_index = read_u16(f);
        read_till_null(f, &b->item_name);
        read_till_null(f, &b->content_type);
        read_till_null(f, &b->content_encoding);
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
        int64_t l = read_till_null(f, &b->item_name);
        VDBG(basemedia, "infe name %s, type %s, id %d", b->item_name, type2name(b->item_type), b->item_id);
        if (b->item_type == TYPE2UINT("mime")) {
            read_till_null(f, &b->content_type);
            read_till_null(f, &b->content_encoding);
            // fgetc(f);
        } else if (b->item_type == TYPE2UINT("uri ")) {
            read_till_null(f, &b->content_type);
        } else {
            if (((int64_t)b->size - 20 > l) && b->version == 2) {
                fseek(f, b->size - 20 -l, SEEK_CUR);
            } else if (((int64_t)b->size > 22 + l) && b->version == 3) {
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
    VDBG(basemedia, "IINF: entry_count %d", b->entry_count);
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

    return b->size;
}

static int
read_itemtype_box(FILE *f, struct itemtype_ref_box *b, int version)
{
    read_box(f, b);
    if (version == 0) {
        b->from_item_id = read_u16(f);
    } else if (version == 1) {
        b->from_item_id = read_u32(f);
    }
    b->ref_count = read_u16(f);
    VDBG(basemedia, "from_item_id %d, itemtype count %d",
           b->from_item_id, b->ref_count);
    b->to_item_ids = calloc(b->ref_count, sizeof(uint32_t));
    for (uint16_t i = 0; i < b->ref_count; i ++) {
        if (version == 0) {
            b->to_item_ids[i] = read_u16(f);
        } else if (version == 1) {
            b->to_item_ids[i] = read_u32(f);
        }
        VDBG(basemedia, "itemtype to_item_ids %d", b->to_item_ids[i]);
    }

    return b->size;
}

int
read_iref_box(FILE *f, struct iref_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'r', 'e', 'f'));
    int sz = b->size - 12;

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
    VDBG(basemedia, "ISPE: width %d, height %d", b->image_width, b->image_height);
    return b->size;
}

static int
read_pixi_box(FILE *f, struct pixi_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('p', 'i', 'x', 'i'));
    b->num_channels = read_u8(f);
    b->bits_per_channel = malloc(b->num_channels);
    FFREAD(b->bits_per_channel, 1, b->num_channels, f);
    VDBG(basemedia, "PIXI: num channels %d", b->num_channels);
    return b->size;
}

static int
read_colr_box(FILE *f, struct colr_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('c', 'o', 'l', 'r'));
    FFREAD(&b->color_type, 4, 1, f);
    VDBG(basemedia, "COLR: %s", type2name(b->color_type));
    if (b->color_type == FOURCC2UINT('n', 'c', 'l', 'x')) {
        // on screen colors
        b->color_primaries = read_u16(f);
        b->transfer_characteristics = read_u16(f);
        b->matrix_coefficients = read_u16(f);
        b->full_range_flag = fgetc(f) >> 7;
    } else if (b->color_type == FOURCC2UINT('r', 'I', 'C', 'C') || b->color_type == FOURCC2UINT('p', 'r', 'o', 'f')) {
        // ICC profile
        int len = b->size - 4 - 8;
        VDBG(basemedia, "profile len %d", len);
        // uint8_t *data = malloc(len);
        // hexdump(stdout, "icc profile", "", data, len);
        // free(data);
        fseek(f, len, SEEK_CUR);
    }
    return b->size;
}

int read_auxc_box(FILE *f, struct auxC_box *b) {

    FFREAD_BOX_FULL(b, f, FOURCC2UINT('a', 'u', 'x', 'C'));
    int l = read_till_null(f, &b->aux_type);
    b->aux_subtype = malloc(b->size - l - 12);
    VDBG(basemedia, "aux_type %s", b->aux_type);
    FFREAD(b->aux_subtype, 1, b->size - l - 12, f);
    // VDBG(heif, "aux_subtype %s", b->aux_subtype);
    return b->size;
}

int
read_clap_box(FILE *f, struct clap_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('c', 'l', 'a', 'p'));
    b->cleanApertureWidthN = read_u32(f);
    b->cleanApertureWidthD = read_u32(f);
    b->cleanApertureHeightN = read_u32(f);
    b->cleanApertureHeightD = read_u32(f);
    b->horizOffN = read_u32(f);
    b->horizOffD = read_u32(f);
    b->vertOffN = read_u32(f);
    b->vertOffD = read_u32(f);
    return b->size;
}

int
read_irot_box(FILE *f, struct irot_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('i', 'r', 'o', 't'));
    b->angle = read_u8(f) & 0x3;
    return b->size;
}

extern int read_hvcc_box(FILE *f, struct box **bn);
extern int read_av1c_box(FILE *f, struct box **bn);

static int
read_ipco_box(FILE *f, struct ipco_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('i', 'p', 'c', 'o'));
    int64_t s = b->size - 8;
    int n = 0;
    VDBG(basemedia, "IPCO: total length %" PRIu64 "", s);
    while (s > 0) {
        struct box p;
        struct box *cc = NULL;
        uint32_t type = probe_box(f, &p);
        VDBG(basemedia, "IPCO: type %s, size %" PRIu64 "", UINT2TYPE(type), p.size);
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
                cc = calloc(1, sizeof(struct ispe_box));
                s -= read_ispe_box(f, (struct ispe_box *)cc);
                b->property[n++] = (struct box *)cc;
                break;
            case FOURCC2UINT('p', 'i', 'x', 'i'):
                cc = calloc(1, sizeof(struct pixi_box));
                s -= read_pixi_box(f, (struct pixi_box *)cc);
                b->property[n++] = (struct box *)cc;
                break;
            case FOURCC2UINT('c', 'o', 'l', 'r'):
                cc = calloc(1, sizeof(struct colr_box));
                s -= read_colr_box(f, (struct colr_box *)cc);
                b->property[n++] = (struct box *)cc;
                break;
            case FOURCC2UINT('a', 'u', 'x', 'C'):
                cc = calloc(1, sizeof(struct auxC_box));
                s -= read_auxc_box(f, (struct auxC_box *)cc);
                b->property[n++] = (struct box *)cc;
                break;
            case FOURCC2UINT('c', 'l', 'a', 'p'):
                cc = calloc(1, sizeof(struct clap_box));
                s -= read_clap_box(f, (struct clap_box*)cc);
                b->property[n++] = (struct box *)cc;
                break;
            case FOURCC2UINT('i', 'r', 'o', 't'):
                cc = calloc(1, sizeof(struct irot_box));
                s -= read_irot_box(f, (struct irot_box*)cc);
                b->property[n++] = (struct box *)cc;
                break;
            default:
                s -= (p.size);
                fseek(f, p.size, SEEK_CUR);
                break;
        }
    }
    b->n_property = n;
    VDBG(basemedia, "IPCO: property %d", b->n_property);
    return b->size;
}

static int
read_ipma_box(FILE *f, struct ipma_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('i', 'p', 'm', 'a'));
    b->entry_count = read_u32(f);
    b->entries = calloc(b->entry_count, sizeof(struct ipma_item));
    VDBG(basemedia, "IPMA: entry_count %d", b->entry_count);
    for (uint32_t i = 0; i < b->entry_count; i ++) {
        if (b->version < 1) {
            b->entries[i].item_id = read_u16(f);
        } else {
            b->entries[i].item_id = read_u32(f);
        }
        b->entries[i].association_count = read_u8(f);
        VDBG(basemedia, "IPMA: item_id %d, association_count %d", b->entries[i].item_id, b->entries[i].association_count);
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
            VDBG(basemedia, "IPMA: essential %d, association %d",
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
    int64_t s = b->size - 8;

    while (s > 0) {
        struct box p;
        uint32_t type = probe_box(f, &p);
        VDBG(basemedia, "IPRP: type %s, size %" PRIu64 "", UINT2TYPE(type), p.size);
        switch (type) {
            case FOURCC2UINT('i', 'p', 'c', 'o'):
                s -= read_ipco_box(f, &b->ipco);
                break;
            case FOURCC2UINT('i', 'p', 'm', 'a'):
                s -= read_ipma_box(f, &b->ipma);
                break;
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
    VDBG(basemedia, "MDAT: %s, size %" PRIu64 "", type2name(b->type), b->size);
    b->data = malloc(b->size - 8);
    // VDBG(basemedia, "mdat size %d", b->size - 8);
    FFREAD(b->data, 1, b->size - 8, f);
    // hexdump(stdout, "mdat ", "", b->data, 256);
    return b->size;
}

int read_dref_box(FILE *f, struct dref_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('d', 'r', 'e', 'f'));
    b->entry_count = read_u32(f);
    // VDBG(basemedia, "dinf %d, dref %d, count %d", b->size, b->size, b->entry_count);
    b->entries = calloc(b->entry_count, sizeof(struct DataEntryBox));
    for (int i = 0; i < (int)b->entry_count; i++) {
        FFREAD(b->entries + i, 4, 3, f);
        b->entries[i].size = SWAP(b->entries[i].size);
        // VDBG(basemedia, "data entry size %d, type %s", b->entries[i].size,
        //        type2name(b->entries[i].type));
        if (b->entries[i].type == TYPE2UINT("url ")) {
            b->entries[i].location = malloc(b->entries[i].size - 12);
            // VDBG(basemedia, "%s", b->entries[i].location);
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
    VDBG(basemedia, "TKHD: width %f, height %f", fix16_16(b->width),
           fix16_16(b->height));
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

int read_VisualSampleEntry(FILE *f, struct VisualSampleEntry *e) {
    read_box(f, &e->entry);
    FFREAD(e->entry.reserved, 1, 6, f);
    e->entry.data_reference_index = read_u16(f);
    e->pre_defined = read_u16(f);
    e->reserved = read_u16(f);
    FFREAD(e->pre_defined1, 4, 3, f);
    e->width = read_u16(f);
    e->height = read_u16(f);
    e->horizresolution = read_u32(f);
    e->vertresolution = read_u32(f);
    e->reserved1 = read_u32(f);
    e->frame_count = read_u16(f);
    FFREAD(e->compressorname, 1, 32, f);
    e->depth = read_u16(f);
    e->pre_defined2 = read_u16(f);
    assert(e->pre_defined2 == -1);
    // probe_box(f, void *d)

    return 0;
}

extern int read_HEVCSampleEntry(FILE *f, struct SampleEntry **e);

int read_stsd_box(FILE *f, struct stsd_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 'd'));
    b->entry_count = read_u32(f);
    for (uint32_t i = 0; i < b->entry_count; i++) {
        struct box p;
        probe_box(f, &p);
        struct SampleEntry *e = NULL;
        if (p.type == FOURCC2UINT('h', 'v', 'c', '1')) {
            read_HEVCSampleEntry(f, &e);
        } else {
            fseek(f, p.size, SEEK_CUR);
        }
        b->entries[i] = e;
    }
    return b->size;
}

int read_stdp_box(FILE *f, struct stdp_box *b, int sample_count)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 'd', 'p'));
    b->priority = calloc(sample_count, 2);
    for (int i = 0; i < sample_count; i++) {
        b->priority[i] = read_u16(f);
    }
    return b->size;
}

int read_stts_box(FILE *f, struct stts_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 't', 's'));
    b->entry_count = read_u32(f);
    VDBG(basemedia, "%s size %" PRIu64 ", entry_count %d", type2name(b->type), b->size,
           b->entry_count);
    if (b->entry_count) {
        b->sample_count = calloc(b->entry_count, 4);
        b->sample_delta = calloc(b->entry_count, 4);
        for (uint32_t i = 0; i < b->entry_count; i++) {
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
    for (uint32_t i = 0; i < b->entry_count; i++) {
        b->sample_count[i] = read_u32(f);
        b->sample_offset[i] = read_u32(f);
    }
    return b->size;
}

int read_stsc_box(FILE *f, struct stsc_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 'c'));
    b->entry_count = read_u32(f);
    VDBG(basemedia, "%s size %" PRIu64 ", entry_count %d", type2name(b->type), b->size,
           b->entry_count);

    b->first_chunk = calloc(b->entry_count, 4);
    b->sample_per_chunk = calloc(b->entry_count, 4);
    b->sample_description_index = calloc(b->entry_count, 4);
    for (uint32_t i = 0; i < b->entry_count; i++) {
        b->first_chunk[i] = read_u32(f);
        b->sample_per_chunk[i] = read_u32(f);
        b->sample_description_index[i] = read_u32(f);
        VDBG(basemedia, "first chunk %d, sample per chunk %d, sample description index %d", b->first_chunk[i], b->sample_per_chunk[i], b->sample_description_index[i]);
    }
    return b->size;
}

int read_stco_box(FILE *f, struct stco_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 'c', 'o'));
    b->entry_count = read_u32(f);
    VDBG(basemedia, "%s size %" PRIu64 ", entry_count %d", type2name(b->type), b->size,
           b->entry_count);

    b->chunk_offset = calloc(b->entry_count, 4);
    for (uint32_t i = 0; i < b->entry_count; i++) {
        b->chunk_offset[i] = read_u32(f);
    }
    return b->size;
}

int read_stsz_box(FILE *f, struct stsz_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 'z'));
    b->sample_size = read_u32(f);
    b->sample_count = read_u32(f);
    VDBG(basemedia, "%s size %" PRIu64 ", sample_size %d, sample_count %d",
           type2name(b->type), b->size, b->sample_size, b->sample_count);

    b->entry_size = calloc(b->sample_count, 4);
    for (uint32_t i = 0; i < b->sample_count; i++) {
        b->entry_size[i] = (b->sample_size == 0) ? read_u32(f) : b->sample_size;
        VDBG(basemedia, "entry size %d", b->entry_size[i]);
    }

    return b->size;
}

int read_stss_box(FILE *f, struct stss_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('s', 't', 's', 's'));
    b->entry_count = read_u32(f);
    b->sample_number = calloc(b->entry_count, 4);
    for (uint32_t i = 0; i < b->entry_count; i++) {
        b->sample_number[i] = read_u32(f);
    }
    return b->size;
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
    VDBG(basemedia, "SGPD: version %d, name %s, default_length %d", b->version,
           type2name(b->grouping_type), b->default_length);

    b->entry_count = read_u32(f);
    VDBG(basemedia, "size %" PRIu64 ", name %s, count %d", b->size,
           type2name(b->grouping_type), b->entry_count);
    b->description_length = calloc(b->entry_count, 4);
    // b->entries = calloc(b->entry_count, sizeof(struct SampleGroupEntry));
    for (uint32_t i = 0; i < b->entry_count; i++) {
        if (b->version == 1) {
            if (b->default_length == 0) {
                b->description_length[i] = read_u32(f);
            } else {
                b->description_length[i] = b->default_length;
            }
        }
        // SampleGroupEntry
        // read_SampleGroupEntry(f, b->entries + i, b->description_length[i]);
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
    VDBG(basemedia, "size %" PRIu64 ", name %s, count %d", b->size,
           type2name(b->grouping_type), b->entry_count);
    if (b->entry_count) {
        b->sample_count = calloc(b->entry_count, 4);
        b->group_description_index = calloc(b->entry_count, 4);
        for (uint32_t i = 0; i < b->entry_count; i++) {
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
    int64_t s = b->size - 8 - b->stsd.size;
    while (s > 0) {
        struct box p;
        uint32_t type = probe_box(f, &p);
        VDBG(basemedia, "STBL: %s left %"PRIu64"", type2name(type), s);
        switch (type) {
        case FOURCC2UINT('s', 't', 'd', 'p'):
            read_stdp_box(f, &b->stdp, b->stsz.sample_count);
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
    int64_t s = b->size - 8;
    while (s > 0) {
        struct box p;
        uint32_t type = probe_box(f, &p);
        VDBG(basemedia, "MINF: %s left %"PRIu64"", type2name(type), s);
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

int read_elng_box(FILE *f, struct elng_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('e', 'l', 'n', 'g'));
    read_till_null(f, &b->extended_language);
    return b->size;
}

int read_mdia_box(FILE *f, struct mdia_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('m', 'd', 'i', 'a'));
    read_mdhd_box(f, &b->mdhd);
    read_hdlr_box(f, &b->hdlr);
    struct box p;
    uint32_t type = probe_box(f, &p);
    if (type == FOURCC2UINT('e', 'l', 'n', 'g')) {
        read_elng_box(f, &b->elng);
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
read_grpl_box(FILE *f, struct grpl_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('g', 'r', 'p', 'l'));
    int s = b->size - 8;
    int n = 0;
    while (s) {
        struct EntityToGroupBox *p = malloc(sizeof(struct EntityToGroupBox));
        read_full_box(f, p);
        p->group_id = read_u32(f);
        p->num_entities_in_group = read_u32(f);
        VDBG(basemedia, "group_id %d, type %s, num_entities_in_group %d",
               p->group_id, type2name(p->type), p->num_entities_in_group);
        p->entity_id = calloc(p->num_entities_in_group, 4);
        for (uint32_t i = 0; i < p->num_entities_in_group; i++) {
            p->entity_id[i] = read_u32(f);
        }
        b->entities[n++] = p;
        s -= p->size;
    }
    b->num_entity = n;
    return b->size;
}

void free_grpl_box(struct grpl_box *b)
{
    for (int i = 0; i < b->num_entity; i++) {
        free(b->entities[i]->entity_id);
        free(b->entities[i]);
    }
}

int
read_meta_box(FILE *f, struct meta_box *meta)
{
    FFREAD_BOX_FULL(meta, f, FOURCC2UINT('m', 'e', 't', 'a'));
    int64_t size = meta->size - 12;
    while (size > 0) {
        struct box b;
        uint32_t type = probe_box(f, &b);
        VDBG(basemedia, "META: %s, size %" PRIu64 "", UINT2TYPE(type), b.size);
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
        case FOURCC2UINT('g', 'r', 'p', 'l'):
            read_grpl_box(f, &meta->grpl);
            break;
        default:
            fseek(f, b.size, SEEK_CUR);
            break;
        }
        size -= b.size;
        VDBG(basemedia, "META: %s, read %" PRIu64 ", left %"PRIu64"", UINT2TYPE(type), b.size, size);
    }
    return meta->size;
}

int read_trak_box(FILE *f, struct trak_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('t', 'r', 'a', 'k'));
    read_tkhd_box(f, &b->tkhd);
    int64_t s = b->size - 8 - b->tkhd.size;
    while (s) {
        struct box p;
        uint32_t type = probe_box(f, &p);
        VDBG(basemedia, "TRAK: %s, left %"PRIu64"", type2name(type), s);
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

    int64_t s = b->size - b->mvhd.size - 8;
    while (s) {
        struct box p;
        uint32_t type = probe_box(f, &p);
        VDBG(basemedia, "MOOV: %s left %"PRIu64"", type2name(type), s);
        switch (type) {
            case FOURCC2UINT('t', 'r', 'a', 'k'):
                b->trak_num ++;
                if (b->trak_num == 1) {
                    b->trak = calloc(1, sizeof(struct trak_box));
                } else {
                    b->trak = realloc(b->trak, sizeof(struct trak_box) * b->trak_num);
                }
                read_trak_box(f, b->trak + b->trak_num - 1);
                break;
            default:
                fseek(f, p.size, SEEK_CUR);
                break;
        }
        s -= (p.size);
    }
    return b->size;
}

void free_hdlr_box(struct hdlr_box *b)
{
    if (b->name) {
        free(b->name);
    }
}

void free_dref_box(struct dref_box *b)
{
    if (b->entry_count) {
        for (uint32_t i = 0; i < b->entry_count; i++) {
            struct DataEntryBox *e = b->entries + i;
            free(e->name);
            free(e->location);
        }
        free(b->entries);
    }
}

void free_dinf_box(struct dinf_box *b)
{
    free_dref_box(&b->dref);
}

void free_iloc_box(struct iloc_box *b)
{
    if (b->item_count) {
        for (int i = 0; i < b->item_count; i++) {
            free(b->items[i].extents);
        }
        free(b->items);
    }
}


void free_iinf_box(struct iinf_box *b)
{
    if (b->entry_count) {
        for (uint32_t i = 0; i < b->entry_count; i++) {
            if (b->item_infos[i].item_name)
                free(b->item_infos[i].item_name);
            if (b->item_infos[i].content_type)
                free(b->item_infos[i].content_type);
            if (b->item_infos[i].content_encoding)
                free(b->item_infos[i].content_encoding);
        }
        free(b->item_infos);
    }
}

void free_idat_box(struct idat_box *b)
{
    if (b->data) {
        free(b->data);
    }
}

void free_iref_box(struct iref_box *b)
{
    if (b->refs_count) {
        for (int i = 0; i < b->refs_count; i++) {
            free(b->refs[i].to_item_ids);
        }
        free(b->refs);
    }
}

void free_pixi_box(struct pixi_box *b) {
    if (b->num_channels)
        free(b->bits_per_channel);
}

void free_auxc_box(struct auxC_box *b) {
    if (b->aux_type)
        free(b->aux_type);
    if (b->aux_subtype)
        free(b->aux_subtype);
}

extern void free_hvcc_box(struct box *b);

void free_ipco_box(struct ipco_box *b)
{
    for (int i = 0; i < b->n_property; i ++) {
        if (b->property[i]) {
            if (b->property[i]->type == FOURCC2UINT('h', 'v', 'c', 'C')) {
                free_hvcc_box(b->property[i]);
            } else if (b->property[i]->type == FOURCC2UINT('p', 'i', 'x', 'i')) {
                free_pixi_box((struct pixi_box *)b->property[i]);
            } else if(b->property[i]->type == FOURCC2UINT('a', 'u', 'x', 'C')) {
                free_auxc_box((struct auxC_box *)b->property[i]);
            }
            free(b->property[i]);
        }
    }
}

void free_ipma_box(struct ipma_box *b)
{
    for (uint32_t i = 0; i < b->entry_count; i++) {
        if (b->entries[i].association_count) {
            free(b->entries[i].property_index);
        }
    }
    free(b->entries);
}


void free_iprp_box(struct iprp_box *b)
{
    free_ipco_box(&b->ipco);
    free_ipma_box(&b->ipma);
    free_pixi_box(&b->pixi);
}

void free_meta_box(struct meta_box *b)
{
    free_hdlr_box(&b->hdlr);
    free_dinf_box(&b->dinf);
    free_iloc_box(&b->iloc);
    free_iinf_box(&b->iinf);
    free_iprp_box(&b->iprp);
    free_idat_box(&b->idat);
    free_iref_box(&b->iref);
    free_grpl_box(&b->grpl);
}

void free_elng_box(struct elng_box *b)
{
    if (b->extended_language) {
        free(b->extended_language);
    }
}

void free_stsd_box(struct stsd_box *b)
{
    for (uint32_t i = 0; i < b->entry_count; i++) {
        free(b->entries[i]);
    }
}

void free_stts_box(struct stts_box *b)
{
    if (b->entry_count) {
        free(b->sample_count);
        free(b->sample_delta);
    }
}

void free_ctts_box(struct ctts_box *b) {
    if (b->entry_count) {
        free(b->sample_count);
        free(b->sample_offset);
    }
}

void free_stsc_box(struct stsc_box *b) {
    if (b->entry_count) {
        free(b->first_chunk);
        free(b->sample_per_chunk);
        free(b->sample_description_index);
    }
}

void free_stco_box(struct stco_box *b)
{
    if (b->entry_count) {
        free(b->chunk_offset);
    }
}
void free_stss_box(struct stss_box *b) {
    if (b->entry_count) {
        free(b->sample_number);
    }
}

void free_stdp_box(struct stdp_box *b)
{
    if (b->priority) {
        free(b->priority);
    }
}

void free_sbgp_box(struct sbgp_box* b)
{
    if (b->entry_count) {
        free(b->group_description_index);
        free(b->sample_count);
    }
}

void free_sgpd_box(struct sgpd_box *b)
{
    if (b->entry_count) {
        free(b->description_length);
        // free(b->entries);
    }
}

void free_stbl_box(struct stbl_box *b) {
    free_stsd_box(&b->stsd);
    free_stts_box(&b->stts);
    free_stsc_box(&b->stsc);
    free_stco_box(&b->stco);
    free_stss_box(&b->stss);
    free_ctts_box(&b->ctts);
    free_stdp_box(&b->stdp);
    free_sbgp_box(&b->sbgp);
    free_sgpd_box(&b->sgpd);
}

void free_minf_box(struct minf_box *b)
{
    free_dinf_box(&b->dinf);
    free_stbl_box(&b->stbl);
}

void free_mdia_box(struct mdia_box *b)
{
    free_hdlr_box(&b->hdlr);
    free_elng_box(&b->elng);
    free_minf_box(&b->minf);
}

void free_tref_box(struct tref_box *b)
{
    if (b->ref_type.track_ids)
        free(b->ref_type.track_ids);
}

void free_trak_box(struct trak_box *b)
{
    free_mdia_box(&b->mdia);
    free_tref_box(&b->tref);
}

void free_moov_box(struct moov_box *b)
{
    for (int i = 0; i < b->trak_num; i++) {
        free_trak_box(&b->trak[i]);
    }
    free(b->trak);
    if (b->meta) {
        free_meta_box(b->meta);
        free(b->meta);
    }
    if (b->udta) {
        free(b->udta);
    }
}
