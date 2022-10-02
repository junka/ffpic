#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vlog.h"
#include "utils.h"
#include "heif.h"
#include "file.h"
#include "byteorder.h"
#include "basemedia.h"
#include "bitstream.h"

VLOG_REGISTER(heif, DEBUG);

static int
HEIF_probe(const char *filename)
{
    static char* heif_types[] = {
        "heic",
        "heix",
        "hevc",
        "hevx"
    };

    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(heif, "fail to open %s\n", filename);
        return -ENOENT;
    }
    struct ftyp_box h;
    int len = read_ftyp(f, &h);
    if (len < 0) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_brand == TYPE2UINT("ftyp")) {
        
        if (len > 12 && h.minor_version == TYPE2UINT("mif1")) {
            for (int j = 0; j < (len -12)>>2; j ++) {
                for (int i = 0; i < sizeof(heif_types)/sizeof(heif_types[0]); i ++) {
                    if (h.compatible_brands[j] == TYPE2UINT(heif_types[i])) {
                        return 0;
                    }
                }
            }
        } else {
            for (int i = 0; i < sizeof(heif_types)/sizeof(heif_types[0]); i ++) {
                if (h.minor_version == TYPE2UINT(heif_types[i])) {
                    return 0;
                }
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

    fread(&b->configurationVersion, 23, 1, f);
    
    b->general_profile_compatibility_flags = SWAP(b->general_profile_compatibility_flags);
    b->avgframerate = SWAP(b->avgframerate);
    b->nal_arrays = malloc(b->num_of_arrays * sizeof(struct nal_arr));

    VDBG(heif, "hvcC: general_profile_idc %d, flags 0x%x", b->general_profile_idc,
                b->general_profile_compatibility_flags);
    VDBG(heif, "hvcC: num_of_arrays %d", b->num_of_arrays);
    for (int i = 0; i < b->num_of_arrays; i ++) {
        fread(b->nal_arrays + i, 1, 1, f);
        fread(&b->nal_arrays[i].numNalus, 2, 1, f);
        b->nal_arrays[i].numNalus = SWAP(b->nal_arrays[i].numNalus);
        VDBG(heif, "nalu: numNalus %d, type %d", b->nal_arrays[i].numNalus, b->nal_arrays[i].nal_unit_type);
        b->nal_arrays[i].nals = malloc(b->nal_arrays[i].numNalus * sizeof(struct nalus));
        for (int j = 0; j < b->nal_arrays[i].numNalus; j ++) {
            fread(&b->nal_arrays[i].nals[j].unit_length, 2, 1, f);
            b->nal_arrays[i].nals[j].unit_length = SWAP(b->nal_arrays[i].nals[j].unit_length);
            b->nal_arrays[i].nals[j].nal_units = malloc(b->nal_arrays[i].nals[j].unit_length);
            fread(b->nal_arrays[i].nals[j].nal_units, 1, b->nal_arrays[i].nals[j].unit_length, f);
            parse_nalu(b->nal_arrays[i].nals[j].nal_units, b->nal_arrays[i].nals[j].unit_length);
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
        VDBG(heif, "ipco left %d", s);
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
        VERR(heif, "error, it is not a meta after ftyp");
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
        VDBG(heif, "%s, left %d", UINT2TYPE(type), size);
    }
    
}

static void
read_item(HEIF * h, FILE *f, uint32_t id)
{
    fseek(f, h->items[id-1].item->base_offset, SEEK_SET);
    if (h->items[id-1].item->extent_count == 1) {
        h->items[id-1].length = h->items[id-1].item->extents[0].extent_length;
        h->items[id-1].data = malloc(h->items[id-1].item->extents[0].extent_length);
        fread(h->items[id-1].data, h->items[id-1].item->extents[0].extent_length, 1, f);
    } else {
        h->items[id-1].data = malloc(h->items[id-1].item->extents[0].extent_length);
        uint64_t total = 0;
        for (int i = 0; i < h->items[id-1].item->extent_count; i ++) {
            total += h->items[id-1].item->extents[i].extent_length;
            h->items[id-1].data = realloc(h->items[id-1].data, total);
            fread(h->items[id-1].data + h->items[id-1].item->extents[i].extent_offset,
                h->items[id-1].item->extents[i].extent_length, 1, f);
        }
        h->items[id-1].length = total;
    }
}

void
decode_hvc1(HEIF * h, uint8_t *data, uint64_t len)
{
    // hexdump(stdout, "mdat ", "", data, 256);
    uint8_t *p = data;
    while (len) {
        uint32_t sample_len = data[0] << 24 | data[1] << 16| data[2] << 8| data[3];
        len -= 4;
        p += 4;
        parse_nalu(p, sample_len);
        len -= sample_len;
        p += sample_len;
    }
}

static void
decode_items(HEIF * h, FILE *f, struct mdat_box *b)
{
    for (int i = 0; i < h->meta.iinf.entry_count; i ++) {
        read_item(h, f, h->meta.iinf.item_infos[i].item_id);
        if (h->meta.iinf.item_infos[i].item_type == TYPE2UINT("mime")) {
            // exif mime
            // printf("length %lld, %s\n", h->items[h->meta.iinf.item_infos[i].item_id-1].item->extents[0].extent_length,
            //     h->items[h->meta.iinf.item_infos[i].item_id-1].data);
        } else {
            //take it as real coded data
            decode_hvc1(h, h->items[h->meta.iinf.item_infos[i].item_id-1].data,
                h->items[h->meta.iinf.item_infos[i].item_id-1].length);
            
        }
    }
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
        VDBG(heif, "%s, read %d, left %d", UINT2TYPE(type), b.size, size);
    }

    // extract some info from meta box
    for (int i = 0; i < 2; i ++) {
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            p->width = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width;
            p->height = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height;
        }
    }
    h->items = malloc(h->meta.iloc.item_count * sizeof(struct heif_item));
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        h->items[h->meta.iloc.items[i].item_id - 1].item = &h->meta.iloc.items[i];
    }

    // process mdata
    VINFO(heif, "mdat_num %d", h->mdat_num);
    decode_items(h, f, h->mdat);

    fclose(f);

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
                for (int k = 0; k < hvcc->nal_arrays[j].numNalus; k ++) {
                    free(hvcc->nal_arrays[j].nals[k].nal_units);
                }
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
    free(h->items);
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

    fprintf(f, "\tmeta box --------------\n");
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
            struct hvcC_box * hvcc = (struct hvcC_box *)(h->meta.iprp.ipco.property[i]);

            fprintf(f, " config version %d, profile_space %d, tier_flag %d, profile_idc %d\n", hvcc->configurationVersion,
                    hvcc->general_profile_space, hvcc->general_tier_flag, hvcc->general_profile_idc);
            uint16_t min_spatial_segmentation_idc = hvcc->min_spatial_segmentation_idc1 << 8 | hvcc->min_spatial_segmentation_idc2;
            fprintf(f, "\t\t\t\tgeneral_profile_compatibility_flags 0x%x\n",
                hvcc->general_profile_compatibility_flags);
            fprintf(f, "\t\t\t\tmin_spatial_segmentation_idc %d, general_level_idc %d\n",
                min_spatial_segmentation_idc, hvcc->general_level_idc);
            fprintf(f, "\t\t\t\tgeneral_constraint_indicator_flags 0x%llx\n", hvcc->general_constraint_indicator_flags);
            
            fprintf(f, "\t\t\t\tparallelismType %d, chroma_format_idc %d, bit_depth_luma_minus8 %d\n\t\t\t\tbit_depth_chroma_minus8 %d\n", 
                hvcc->parallelismType, hvcc->chroma_format_idc, hvcc->bit_depth_luma_minus8, hvcc->bit_depth_chroma_minus8);
            fprintf(f, "\t\t\t\tavgframerate %d, constantframerate %d\n\t\t\t\tnumtemporalLayers %d, temporalIdNested %d, lengthSizeMinusOne %d\n",
                hvcc->avgframerate, hvcc->constantframerate, hvcc->numtemporalLayers, hvcc->temporalIdNested, hvcc->lengthSizeMinusOne);
            for (int j = 0; j < hvcc->num_of_arrays; j ++) {
                fprintf(f, "\t\t\t\t");
                fprintf(f, "completeness %d, nal_unit_type %d", hvcc->nal_arrays[j].array_completeness, hvcc->nal_arrays[j].nal_unit_type);
                for (int k = 0; k < hvcc->nal_arrays[j].numNalus; k ++) {
                    fprintf(f, ", unit_length %d\n", hvcc->nal_arrays[j].nals[k].unit_length);
                    hexdump(f, "\t\t\t\tnalu: ", "\t\t\t\t", hvcc->nal_arrays[j].nals[k].nal_units, hvcc->nal_arrays[j].nals[k].unit_length);
                }
                fprintf(f, "\n");
            }
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
        s1 = UINT2TYPE(h->meta.iref.refs[i].type);
        fprintf(f, "ref_type=%s,from_item_id=%d,ref_count=%d", s1,
            h->meta.iref.refs[i].from_item_id,
            h->meta.iref.refs[i].ref_count);
        free(s1);
        for (int j = 0; j < h->meta.iref.refs[i].ref_count; j ++) {
            fprintf(f, ",to_item=%d",  h->meta.iref.refs[i].to_item_ids[j]);
        }
        fprintf(f, "\n");
    }
    
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



