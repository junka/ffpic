#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#include "vlog.h"
#include "utils.h"
#include "heif.h"
#include "file.h"
#include "byteorder.h"
#include "basemedia.h"
#include "bitstream.h"
#include "colorspace.h"

VLOG_REGISTER(heif, DEBUG)

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
                for (int i = 0; i < (int)(sizeof(heif_types)/sizeof(heif_types[0])); i ++) {
                    if (h.compatible_brands[j] == TYPE2UINT(heif_types[i])) {
                        return 0;
                    }
                }
            }
            free(h.compatible_brands);
        } else {
            for (int i = 0; i < (int)(sizeof(heif_types)/sizeof(heif_types[0])); i ++) {
                if (h.minor_version == TYPE2UINT(heif_types[i])) {
                    return 0;
                }
            }
        }
    }

    return -EINVAL;
}


static int
read_hvcc_box(FILE *f, struct box **bn)
{
    struct hvcC_box *b = malloc(sizeof(struct hvcC_box));
    if (*bn == NULL) {
        *bn = (struct box *)b;
    }
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);

    fread(&b->configurationVersion, 23, 1, f);
    
    b->general_profile_compatibility_flags = SWAP(b->general_profile_compatibility_flags);
    b->general_constraint_indicator_flags_h = SWAP(b->general_constraint_indicator_flags_h);
    b->general_constraint_indicator_flags_l = SWAP(b->general_constraint_indicator_flags_l);
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
            parse_nalu(b->nal_arrays[i].nals[j].nal_units, b->nal_arrays[i].nals[j].unit_length, NULL);
        }
    }
    return b->size;
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
    struct dinf_box dinf;
    while (size > 0) {
        type = read_box(f, &b, size);
        VDBG(heif, "%s: size %d", UINT2TYPE(type), b.size);
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
        case FOURCC2UINT('i', 'i', 'n','f'):
            read_iinf_box(f, &meta->iinf);
            break;
        case FOURCC2UINT('i', 'p', 'r', 'p'):
            read_iprp_box(f, &meta->iprp, &read_hvcc_box);
            break;
        case FOURCC2UINT('i', 'r', 'e', 'f'):
            read_iref_box(f, &meta->iref);
            break;
        case FOURCC2UINT('d', 'i', 'n', 'f'):
            read_dinf_box(f, &dinf);
            break;
        default:
            fseek(f, b.size, SEEK_CUR);
            break;
        }
        size -= b.size;
        VDBG(heif, "%s, left %d", UINT2TYPE(type), size);
    }
    
}

static void
pre_read_item(HEIF * h, FILE *f, uint32_t idx)
{
    // for now make sure, extent_count = 1 work
    if (h->items[idx].item->extent_count == 1) {
        h->items[idx].length = h->items[idx].item->extents[0].extent_length;
        h->items[idx].data = malloc(h->items[idx].length);
        fseek(f, h->items[idx].item->base_offset + h->items[idx].item->extents[0].extent_offset, SEEK_SET),
        fread(h->items[idx].data, h->items[idx].length, 1, f);
    } else {
        h->items[idx].data = malloc(1);
        uint64_t total = 0;
        for (int i = 0; i < h->items[idx].item->extent_count; i ++) {
            h->items[idx].data = realloc(h->items[idx].data, h->items[idx].item->extents[i].extent_length);
            fseek(f, h->items[idx].item->base_offset + h->items[idx].item->extents[i].extent_offset, SEEK_SET),
            fread(h->items[idx].data, h->items[idx].item->extents[i].extent_length, 1, f);
            total += h->items[idx].item->extents[i].extent_length;
        }
        h->items[idx].length = total;
    }
}

void
decode_hvc1(HEIF * h, uint8_t *data, uint64_t len, uint8_t** pixels)
{
    // hexdump(stdout, "coded ", "", data, 256);
    uint8_t *p = data;
    while (len > 0) {
        int sample_len = p[0] << 24 | p[1] << 16| p[2] << 8| p[3];
        len -= 4;
        p += 4;
        parse_nalu(p, sample_len, pixels);
        len -= sample_len;
        p += sample_len;
    }
}

static void
decode_items(HEIF *h, FILE *f, uint8_t **pixels)
{
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        pre_read_item(h, f, i);
        if (h->items[i].type == TYPE2UINT("mime")) {
            // exif mime, skip it
            // hexdump(stdout, "exif:", "", h->items[i].data, h->items[i].length);
            VDBG(heif, "exif %" PRIu64, h->items[i].length);
        } else if (h->items[i].type == TYPE2UINT("hvc1")) {
            //take it as real coded data
            // VINFO(heif, "decoding id 0x%p len %" PRIu64, h->items[i].data, h->items[i].length);
            decode_hvc1(h, h->items[i].data, h->items[i].length, pixels);
        }
    }
}

static struct pic*
HEIF_load(const char *filename, int skip_flag)
{
    struct pic *p = pic_alloc(sizeof(HEIF));
    HEIF *h = p->pic;
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size -= read_ftyp(f, &h->ftyp);
    // h->mdat = malloc(sizeof(struct mdat_box));
    struct box b;
    while (size > 0) {
        uint32_t type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case FOURCC2UINT('m', 'e', 't', 'a'):
            read_meta_box(f, &h->meta);
            break;
        case FOURCC2UINT('m', 'd', 'a', 't'):
            h->mdat_num ++;
            // h->mdat = realloc(h->mdat, h->mdat_num * sizeof(struct mdat_box));
            // read_mdat_box(f, h->mdat + h->mdat_num - 1);
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
        h->items[i].item = &h->meta.iloc.items[i];
        for (int j = 0; j < (int)h->meta.iinf.entry_count; j++) {
            if (h->meta.iinf.item_infos[j].item_id == h->items[i].item->item_id) {
                h->items[i].type = h->meta.iinf.item_infos[j].item_type;
            }
        }
    }
    uint16_t primary_id = h->meta.pitm.item_id;
    VINFO(heif, "primary id %d", h->meta.pitm.item_id);
    for (int i = 0; i < (int)h->meta.iloc.item_count; i++) {
        if (h->meta.iloc.items[i].item_id == primary_id) {
            VINFO(heif, "primary loc at %" PRIu64, h->meta.iloc.items[i].base_offset);
            break;
        }
    }
    // process mdata
    VINFO(heif, "mdat_num %d", h->mdat_num);
    p->width = ((p->width + 3) >> 2) << 2;
    p->depth = 32;
    p->pitch = ((((p->width + 15) >> 4) * 16 * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = malloc(p->pitch * p->height);
    p->format = CS_PIXELFORMAT_RGB888;
    decode_items(h, f, (uint8_t **)&p->pixels);

    fclose(f);

    return p;
}

static void
HEIF_free(struct pic *p)
{
    HEIF * h = (HEIF *)p->pic;
    free_hevc_param_set();
    if (h->ftyp.compatible_brands)
        free(h->ftyp.compatible_brands);

    struct meta_box *m = &h->meta;
    if (m->hdlr.name)
        free(m->hdlr.name);
    for (int i = 0; i < m->iloc.item_count; i ++) {
        free(m->iloc.items[i].extents);
    }
    free(m->iloc.items);

    for (int i = 0; i < (int)m->iinf.entry_count; i++) {
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

    for (int i = 0; i < (int)m->iprp.ipma.entry_count; i++) {
        free(m->iprp.ipma.entries[i].association);
    }
    free(m->iprp.ipma.entries);

    for (int i = 0; i < m->iref.refs_count; i ++) {
        free(m->iref.refs[i].to_item_ids);
    }
    free(m->iref.refs);

    // for (int i = 0; i < h->mdat_num; i ++) {
    //     free(h->mdat[i].data);
    // }
    // free(h->mdat);
    free(h->items);

    free(p->pixels);
    pic_free(p);
}


static void
HEIF_info(FILE *f, struct pic* p)
{
    HEIF * h = (HEIF *)p->pic;
    fprintf(f, "HEIF file format:\n");
    fprintf(f, "-----------------------\n");
    char *s1 = UINT2TYPE(h->ftyp.minor_version);
    fprintf(f, "\tbrand: %s,", s1);
    s1 = UINT2TYPE(h->ftyp.compatible_brands[1]);
    fprintf(f, " compatible %s\n", s1);
    fprintf(f, "\theight: %d, width: %d\n", p->height, p->width);

    fprintf(f, "\tmeta box --------------\n");
    fprintf(f, "\t");
    print_box(f, &h->meta.hdlr);
    s1 = UINT2TYPE(h->meta.hdlr.handler_type);
    fprintf(f, " pre_define=%d,handle_type=\"%s\"", h->meta.hdlr.pre_defined, s1);
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
        fprintf(f,
                "item_id=%d,data_ref_id=%d,base_offset=%"PRIu64",extent_count=%d\n",
                h->meta.iloc.items[i].item_id,
                h->meta.iloc.items[i].data_ref_id,
                h->meta.iloc.items[i].base_offset,
                h->meta.iloc.items[i].extent_count);
        for (int j = 0; j < h->meta.iloc.items[i].extent_count; j ++) {
            fprintf(f, "\t\t\t");
            if (h->meta.iloc.version == 1) {
                fprintf(f, "extent_id=%"PRIu64",", h->meta.iloc.items[i].extents[j].extent_index);
            }
            fprintf(f, "extent_offset=%" PRIu64 ",extent_length=%" PRIu64 "\n",
                    h->meta.iloc.items[i].extents[j].extent_offset,
                    h->meta.iloc.items[i].extents[j].extent_length);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iinf);
    fprintf(f, "\n");
    for (int i = 0; i < (int)h->meta.iinf.entry_count; i++) {
        fprintf(f, "\t\t");
        print_box(f, &h->meta.iinf.item_infos[i]);
        s1 = UINT2TYPE(h->meta.iinf.item_infos[i].item_type);
        fprintf(f, " item_id=%d,item_protection_index=%d,item_type=%s",
            h->meta.iinf.item_infos[i].item_id, h->meta.iinf.item_infos[i].item_protection_index,
            s1);
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
            fprintf(
                f, "\t\t\t\tgeneral_constraint_indicator_flags 0x%" PRIx64 "\n",
                (uint64_t)hvcc->general_constraint_indicator_flags_h << 16 |
                    hvcc->general_constraint_indicator_flags_l);

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
    for (int i = 0; i < (int)h->meta.iprp.ipma.entry_count; i++) {
        struct ipma_item *ipma = &h->meta.iprp.ipma.entries[i];
        fprintf(f, "\t\titem %d: association_count %d\n", ipma->item_id, ipma->association_count);
        for (int j = 0; j < ipma->association_count; j ++) {
            fprintf(f, "\t\t\tessential %d, id %d \n", ipma->association[j] >> 15, ipma->association[j] & 0x7fff);
        }
    }
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



