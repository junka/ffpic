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
    printf("mvhd size %d, version %d\n", b->size, b->version);
    if (b->version == 0) {
        fread(&b->ctime, 4, 1, f);
        fread(&b->mtime, 4, 1, f);
        fread(&b->timescale, 4, 1, f);
        fread(&b->duration, 4, 1, f);
    } else if (b->version == 1) {
        fread(&b->ctime, 28, 1, f);
    }
    fread(&b->rate, 4, 1, f);
    fread(&b->volume, 2, 1, f);
    fread(&b->reserved, 2, 1, f);
    fread(b->rsd, 4, 2, f);
    fread(b->matrix, 4, 9, f);
    fread(b->pre_defined, 4, 6, f);
    fread(&b->next_track_id, 4, 1, f);

    b->rate = SWAP(b->rate);
    b->volume = SWAP(b->volume);
    b->next_track_id = SWAP(b->next_track_id);
    printf("rate %d, volume %d, trak_id %d\n", b->rate, b->volume, b->next_track_id);
}


void
read_hdlr_box(FILE *f, struct hdlr_box *b)
{
    fread(b, 4, 8, f);
    assert(b->type == FOURCC2UINT('h', 'd', 'l', 'r'));
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
    assert(b->type == FOURCC2UINT('i', 'l', 'o', 'c'));
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
        // printf("type %s, size %d\n", UINT2TYPE(type), p.size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
            case FOURCC2UINT('h', 'v', 'c', 'C'):
                s -= cb(f, &cc);
                b->property[n++] = cc;
                break;
            case FOURCC2UINT('a', 'v', '1', 'C'):
                s -= cb(f, &cc);
                b->property[n++] = cc;
                break;
            case FOURCC2UINT('i', 's', 'p', 'e'):
                ispe = malloc(sizeof(struct ispe_box));
                s -= read_ispe_box(f, ispe);
                b->property[n++] = (struct box *)ispe;
                break;
            case FOURCC2UINT('p', 'i', 'x', 'i'):
                pixi = malloc(sizeof(struct pixi_box));
                s -= read_pixi_box(f, pixi);
                b->property[n++] = (struct box *)pixi;
                break;
            default:
                // assert(0);
                s -= (p.size);
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

void read_dref_box(FILE *f, struct dref_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == TYPE2UINT("dref"));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    // printf("dinf %d, dref %d, count %d\n", b->size, b->size, b->entry_count);
    b->entries = malloc(b->entry_count * sizeof(struct DataEntryBox));
    for (int i = 0; i < (int)b->entry_count; i++) {
        fread(b->entries + i, 12, 1, f);
        b->entries[i].size = SWAP(b->entries[i].size);
        // printf("data entry size %d, type %s\n", b->entries[i].size,
        //        type2name(b->entries[i].type));
        if (b->entries[i].type == TYPE2UINT("url ")) {
            b->entries[i].location = malloc(b->entries[i].size - 12);
            // printf("%s\n", b->entries[i].location);
            fread(b->entries[i].location, b->entries[i].size - 12, 1,
                  f);
        } else if (b->entries[i].type == TYPE2UINT("urn ")) {
            b->entries[i].name = malloc(b->entries[i].size - 12);
            fread(b->entries[i].name, b->entries[i].size - 12, 1, f);
        }
    }
}

void read_dinf_box(FILE *f, struct dinf_box *b)
{
    fread(b, 8, 1, f);
    assert(b->type == TYPE2UINT("dinf"));
    b->size = SWAP(b->size);
    read_dref_box(f, &b->dref);
}

static void
read_tkhd_box(FILE *f, struct tkhd_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('t', 'k', 'h', 'd'));
    b->size = SWAP(b->size);
    if (b->version == 0) {
        fread(&b->creation_time, 4, 1, f);
        fread(&b->modification_time, 4, 1, f);
        fread(&b->track_id, 4, 1, f);
        fread(&b->reserved, 4, 1, f);
        fread(&b->duration, 4, 1, f);
    } else {
        fread(&b->creation_time, 32, 1, f);
    }
    fread(b->rsd, 4, 2, f);
    fread(&b->layer, 2, 1, f);
    fread(&b->alternate_group, 2, 1, f);
    fread(&b->volume, 2, 1, f);
    fread(&b->reserved0, 2, 1, f);
    fread(&b->matrix, 4, 9, f);
    fread(&b->width, 4, 1, f);
    fread(&b->height, 4, 1, f);
    b->width = SWAP(b->width);
    b->height = SWAP(b->height);
    printf("width %d, height %d\n",b->width, b->height);
}

void read_vmhd_box(FILE *f, struct vmhd_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('v', 'm', 'h', 'd'));
    b->size = SWAP(b->size);
    fread(&b->graphicsmode, 2, 1, f);
    b->graphicsmode = SWAP(b->graphicsmode);
    fread(b->opcolor, 2, 3, f);
}

void read_mdhd_box(FILE *f, struct mdhd_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('m', 'd', 'h', 'd'));
    b->size = SWAP(b->size);
    if (b->version == 0) {
        fread(&b->creation_time, 4, 1, f);
        fread(&b->modification_time, 4, 1, f);
        fread(&b->timescale, 4, 1, f);
        fread(&b->duration, 4, 1, f);
    } else {
        fread(&b->creation_time, 28, 1, f);
    }
    fread(&b->lan, 2, 1, f);
    fread(&b->pre_defined, 2, 1, f);
}

void read_stsd_box(FILE *f, struct stsd_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 's', 'd'));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    // printf("stsd entry_count %d\n", b->entry_count);
    b->entries = malloc(sizeof(struct SampleEntry) * b->entry_count);
    for (int i = 0; i < b->entry_count; i++) {
        fread(b->entries+i, 4, 2, f);
        b->entries[i].size = SWAP(b->entries[i].size);
        printf("sample entry %s: %d\n", type2name(b->entries[i].type), b->size);
        // fread(b->entries[i].reserved, 1, 6, f);
        // fread(&b->entries[i].data_reference_index, 2, 1, f);
        // b->entries[i].data_reference_index = SWAP(b->entries[i].data_reference_index);
        // printf("data_ref_index %d\n", b->entries[i].data_reference_index);
        fseek(f, b->entries[i].size - 8, SEEK_CUR);
    }
}

void read_stdp_box(FILE *f, struct stdp_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 'd', 'p'));
    b->size = SWAP(b->size);

}

void read_stts_box(FILE *f, struct stts_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 't', 's'));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    printf("%s size %d, entry_count %d\n", type2name(b->type), b->size, b->entry_count);
    if (b->entry_count) {
        b->sample_count = malloc(4 * b->entry_count);
        b->sample_delta = malloc(4 * b->entry_count);
        for (int i = 0; i < b->entry_count; i++) {
            fread(b->sample_count + i, 4, 1, f);
            fread(b->sample_delta + i, 4, 1, f);
        }
    }
}

void read_ctts_box(FILE *f, struct ctts_box *b) {
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('c', 't', 't', 's'));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    b->sample_count = malloc(4 * b->entry_count);
    b->sample_offset = malloc(4 * b->entry_count);
    for (int i = 0; i < b->entry_count; i++) {
        fread(b->sample_count + i, 4, 1, f);
        fread(b->sample_offset + i, 4, 1, f);
    }
}

void read_stsc_box(FILE *f, struct stsc_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 's', 'c'));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    printf("%s size %d, entry_count %d\n", type2name(b->type), b->size,
           b->entry_count);

    b->first_chunk = malloc(4 * b->entry_count);
    b->sample_per_chunk = malloc(4 * b->entry_count);
    b->sample_description_index = malloc(4 * b->entry_count);
    for (int i = 0; i < b->entry_count; i++) {
        fread(b->first_chunk + i, 4, 1, f);
        fread(b->sample_per_chunk + i, 4, 1, f);
        fread(b->sample_description_index + i, 4, 1, f);
    }
}

void read_stco_box(FILE *f, struct stco_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 'c', 'o'));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    printf("%s size %d, entry_count %d\n", type2name(b->type), b->size,
           b->entry_count);

    b->chunk_offset = malloc(4 * b->entry_count);
    for (int i = 0; i < b->entry_count; i++) {
        fread(b->chunk_offset + i, 4, 1, f);
    }
}

void read_stsz_box(FILE *f, struct stsz_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 's', 'z'));
    b->size = SWAP(b->size);
    fread(&b->sample_size, 4, 1, f);
    b->sample_size = SWAP(b->sample_size);
    fread(&b->sample_count, 4, 1, f);
    b->sample_count = SWAP(b->sample_count);
    printf("%s size %d, sample_size %d, sample_count %d\n", type2name(b->type), b->size,
           b->sample_size, b->sample_count);
    if (b->sample_size == 0) {
        b->entry_size = malloc(4 * b->sample_count);
        for (int i = 0; i < b->sample_count; i++) {
            fread(b->entry_size + i, 4, 1, f);
            b->entry_size[i] = SWAP(b->entry_size[i]);
            // printf("entry size %d\n", b->entry_size[i]);
        }
    }
}

void read_stss_box(FILE *f, struct stss_box *b)
{
    fread(b, 4, 3, f);
    assert(b->type == FOURCC2UINT('s', 't', 's', 's'));
    b->size = SWAP(b->size);
    fread(&b->entry_count, 4, 1, f);
    b->entry_count = SWAP(b->entry_count);
    b->sample_number = malloc(b->entry_count * 4);
    for (int i = 0; i < b->entry_count; i++) {
        fread(b->sample_number+i, 4, 1, f);
        b->sample_number[i] = SWAP(b->sample_number[i]);
    }
}

void read_stbl_box(FILE *f, struct stbl_box *b)
{
    fread(b, 4, 2, f);
    assert(b->type == FOURCC2UINT('s', 't', 'b', 'l'));
    b->size = SWAP(b->size);
    read_stsd_box(f, &b->stsd);
    int s = b->size - 8 - b->stsd.size;
    while (s) {
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
        default:
            fseek(f, p.size, SEEK_CUR);
            break;
        }
        s -= (p.size);
    }
}
void read_minf_box(FILE *f, struct minf_box *b)
{
    fread(b, 4, 2, f);
    assert(b->type == FOURCC2UINT('m', 'i', 'n', 'f'));
    b->size = SWAP(b->size);
    int s = b->size - 8;
    while (s) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("%s\n", type2name(type));
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
}

void read_mdia_box(FILE *f, struct mdia_box *b)
{
    fread(b, 4, 2, f);
    assert(b->type == FOURCC2UINT('m', 'd', 'i', 'a'));
    b->size = SWAP(b->size);
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

}

void read_trak_box(FILE *f, struct trak_box *b)
{
    fread(b, 4, 2, f);
    b->size = SWAP(b->size);
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
}

void
read_moov_box(FILE *f, struct moov_box *b)
{
    fread(b, 4, 2, f);
    b->size = SWAP(b->size);
    read_mvhd_box(f, &b->mvhd);

    int s = b->size - b->mvhd.size - 8;
    printf("moov left size %d\n", s);
    while (s) {
        struct box p;
        uint32_t type = read_box(f, &p, s);
        printf("%s\n", type2name(type));
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
}