#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#include "hevc.h"
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
        "heis",
        "heim",
        "hevc",
        "hevx",
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
        // VDBG(heif, "len %d minor_version %s", len , type2name(h.minor_version));
        if (len > 12 && (h.minor_version == TYPE2UINT("mif1")||h.minor_version == TYPE2UINT("msf1"))) {
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

void free_hvcc_box(struct box *bn)
{
    struct hvcC_box *b = (struct hvcC_box*)bn;
    for (int i = 0; i < b->num_of_arrays; i++) {
        if (b->nal_arrays[i].numNalus) {
            free(b->nal_arrays[i].xps);
        }
    }
    if (b->nal_arrays) {
        free(b->nal_arrays);
    }
}

int
read_hvcc_box(FILE *f, struct box **bn)
{
    if (*bn == NULL) {
        *bn = (struct box *)calloc(1, sizeof(struct hvcC_box));
    }
    struct hvcC_box *b = (struct hvcC_box *)(*bn);

    FFREAD_BOX_ST(b, f, FOURCC2UINT('h', 'v', 'c', 'C'));
    VDBG(heif, "hvcC: size %" PRIu64 "", b->size);
    // b->configurationVersion = fgetc(f);
    FFREAD(&b->configurationVersion, 23, 1, f);
    
    b->general_profile_compatibility_flags = SWAP(b->general_profile_compatibility_flags);
    b->avgframerate = SWAP(b->avgframerate);

    b->nal_arrays = calloc(b->num_of_arrays, sizeof(struct nal_arr));

    VDBG(heif, "hvcC: general_profile_idc %d, flags 0x%x", b->general_profile_idc,
                b->general_profile_compatibility_flags);
    VDBG(heif, "hvcC: num_of_arrays %d", b->num_of_arrays);
    for (int i = 0; i < b->num_of_arrays; i ++) {
        FFREAD(b->nal_arrays + i, 1, 1, f);

        b->nal_arrays[i].numNalus = read_u16(f);
        VDBG(heif, "nalu: numNalus %d, type %d, completeness %d",
             b->nal_arrays[i].numNalus, b->nal_arrays[i].nal_unit_type,
             b->nal_arrays[i].array_completeness);
        struct nalus *nals = calloc(b->nal_arrays[i].numNalus, sizeof(struct nalus));
        if (b->nal_arrays[i].nal_unit_type == VPS_NUT) {
            b->nal_arrays[i].xps = calloc(b->nal_arrays[i].numNalus, sizeof(struct vps*));
        } else if (b->nal_arrays[i].nal_unit_type == PPS_NUT) {
            b->nal_arrays[i].xps = calloc(b->nal_arrays[i].numNalus, sizeof(struct pps*));
        } else if (b->nal_arrays[i].nal_unit_type == SPS_NUT) {
            b->nal_arrays[i].xps = calloc(b->nal_arrays[i].numNalus, sizeof(struct sps*));
        }
        for (int j = 0; j < b->nal_arrays[i].numNalus; j++) {
            nals[j].unit_length = read_u16(f);
            VDBG(heif, "hvcc: nal unit length %d", nals[j].unit_length);
            nals[j].nal_units = malloc(nals[j].unit_length);
            FFREAD(nals[j].nal_units, 1, nals[j].unit_length, f);
            parse_nalu(nals[j].nal_units, nals[j].unit_length, (uint8_t **)(((void **)(b->nal_arrays[i].xps))+j), &b->hps);
            free(nals[j].nal_units);
        }
        free(nals);
    }
    return b->size;
}

int read_HEVCSampleEntry(FILE *f, struct SampleEntry **b)
{
    if (*b == NULL) {
        *b = calloc(1, sizeof(struct HEVCSampleEntry));
    }
    struct HEVCSampleEntry *e = (struct HEVCSampleEntry *)*b;
    read_VisualSampleEntry(f, &e->sample);
    assert(e->sample.entry.type == FOURCC2UINT('h', 'v', 'c', '1') ||
           e->sample.entry.type == FOURCC2UINT('h', 'e', 'v', '1'));
    read_hvcc_box(f, (struct box **)(&e->config));
    return 0;
}

struct heif_item *
find_item_by_id(HEIF *h, int id)
{
    for (int i = 0; i < h->meta.iloc.item_count; i++) {
        if (h->items[i].item->item_id == id) {
            return h->items+i;
        }
    }
    return NULL;
}

struct box *
get_ipma_item_from_item_id(HEIF *h, int id, uint32_t fourcc)
{
    for (uint32_t i = 0; i < h->meta.iprp.ipma.entry_count; i++) {
        for (int j = 0; j < h->meta.iprp.ipma.entries[i].association_count; j++) {
            if (h->meta.iprp.ipma.entries[i].item_id == (uint32_t)id) {
                for (int k = 0; k < h->meta.iprp.ipma.entries[i].association_count; k++) {
                    // int essential = h->meta.iprp.ipma.entries[i].property_index[k] >> 15;
                    int property_index = h->meta.iprp.ipma.entries[i].property_index[k] & 0x7FFF;
                    struct box *b = (struct box *)h->meta.iprp.ipco.property[property_index-1];
                    if (b->type == fourcc) {
                        return h->meta.iprp.ipco.property[property_index-1];
                    }
                }
            }
        }
    }
    return NULL;
}

struct hvcC_box *
get_hvcc_from_item_id(HEIF *h, int id)
{
    return (struct hvcC_box *)get_ipma_item_from_item_id(h, id, FOURCC2UINT('h', 'v', 'c', 'C'));
}

struct auxC_box *
get_auxc_from_item_id(HEIF *h, int id)
{
    return (struct auxC_box *)get_ipma_item_from_item_id(h, id, FOURCC2UINT('a', 'u', 'x', 'C'));

}

int get_primary_item_id(HEIF *h)
{
    //if no pitm, then return -1;
    if (h->meta.pitm.type != FOURCC2UINT('p', 'i', 't', 'm')) {
        return -1;
    }
    return h->meta.pitm.item_id;
}

struct ispe_box *
get_ispe_by_item_id(HEIF *h, int id)
{
    for (uint32_t i = 0; i < h->meta.iprp.ipma.entry_count; i++) {
        if (h->meta.iprp.ipma.entries[i].item_id == (uint32_t)id) {
            for (int k = 0; k < h->meta.iprp.ipma.entries[i].association_count; k++) {
                // int essential = h->meta.iprp.ipma.entries[i].property_index[k] >> 15;
                int property_index = h->meta.iprp.ipma.entries[i].property_index[k] & 0x7FFF;
                struct box *bb = h->meta.iprp.ipco.property[property_index - 1];
                if (bb->type == FOURCC2UINT('i', 's', 'p', 'e')) {
                    return (struct ispe_box *)bb;
                }
            }
            break;
        }
    }
    return NULL;
}

static void
pre_read_item(HEIF * h, FILE *f, uint32_t idx)
{
    // for now make sure, extent_count = 1 work
    assert(h->items[idx].item->extent_count == 1);
    if (h->items[idx].item->extent_count == 1) {
        h->items[idx].length = h->items[idx].item->extents[0].extent_length;
        h->items[idx].data = malloc(h->items[idx].length);
        VDBG(heif, "read item %d, length %" PRIu64 ", method %d", idx,
             h->items[idx].length, h->items[idx].item->construct_method);
        if (h->items[idx].item->construct_method == 0) {//file offset
            fseek(f, h->items[idx].item->base_offset + h->items[idx].item->extents[0].extent_offset, SEEK_SET),
            fread(h->items[idx].data, h->items[idx].length, 1, f);
        } else if (h->items[idx].item->construct_method == 1) {//idat offset
            memcpy(h->items[idx].data, h->meta.idat.data + h->items[idx].item->base_offset + h->items[idx].item->extents[0].extent_offset, h->items[idx].length);
            // hexdump(stdout, "idat", "", h->items[idx].data, h->items[idx].length);
        } else if (h->items[idx].item->construct_method == 2) { // item offset

        }
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

void decode_hvc1(uint8_t *data, uint64_t len, uint8_t **pixels,
                 struct hevc_param_set *hps) {
    // hexdump(stdout, "coded ", "", data, 256);
    uint8_t *p = data;
    while (len > 0) {
        int sample_len = p[0] << 24 | p[1] << 16| p[2] << 8| p[3];
        len -= 4;
        p += 4;
        parse_nalu(p, sample_len, pixels, hps);
        len -= sample_len;
        p += sample_len;
    }
}

static struct pic *
HEIF_load_one(int width, int height, uint8_t *data, int length, struct hevc_param_set *hps) {
    struct pic *p = pic_alloc(sizeof(HEIF));
    // HEIF *h = p->pic;
    p->width = width;
    p->height = height;
    p->width = ((p->width + 3) >> 2) << 2;
    p->depth = 32;
    p->pitch = ((((p->width + 15) >> 4) * 16 * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = malloc(p->pitch * p->height);
    p->format = CS_PIXELFORMAT_RGBA8888;
    decode_hvc1(data, length, (uint8_t **)&p->pixels, hps);
    return p;
}

static int
decode_grid_items(HEIF *h, struct heif_item *it, struct pic *p)
{
    int num = 0;
    struct grid *ig = malloc(sizeof(struct grid));
    ig->version = it->data[0];
    ig->flags = it->data[1];
    ig->row_minus_one = it->data[2];
    ig->columns_minus_one = it->data[3];
    // 8 bytes, top, left, width, height in 2 bytes
    if ((ig->flags & 1) == 0) {
        assert(it->length == 8);
        uint16_t width = *(uint16_t *)(it->data + 4);
        uint16_t height = *(uint16_t *)(it->data + 6);
        ig->output_width = SWAP(width);
        ig->output_height = SWAP(height);
    } else {
        assert(it->length == 12);
        uint32_t width = *(uint32_t *)(it->data + 4);
        uint32_t height = *(uint32_t *)(it->data + 8);
        ig->output_width = SWAP(width);
        ig->output_height = SWAP(height);
    }
    VDBG(heif, "grid width %d, height %d", ig->output_width, ig->output_height);
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *ref = &h->meta.iref.refs[i];
        assert(ref->ref_count == (ig->row_minus_one + 1) * (ig->columns_minus_one + 1));
        if (ref->from_item_id == it->item->item_id && ref->type == TYPE2UINT("dimg")) {
            for (int j = 0; j < ref->ref_count; j++) {
                struct heif_item *item = find_item_by_id(h, ref->to_item_ids[j]);
                assert(item->type == TYPE2UINT("hvc1"));
                struct hvcC_box *conf = get_hvcc_from_item_id(h, ref->to_item_ids[j]);
                decode_hvc1(item->data, item->length, (uint8_t **)&p->pixels, &conf->hps);
                num ++;
            }
        }
    }
    free(ig);
    return num;
}

static int
decode_primary_item(HEIF *h, struct pic *p)
{
    int num = 0;
    int primary_id = get_primary_item_id(h);
    if (primary_id < 0) {
        //no primary
        return num;
    }
    VINFO(heif, "primary id %d", primary_id);
    int pri_index = 0;
    for (pri_index = 0; pri_index < (int)h->meta.iloc.item_count; pri_index++) {
        if (h->items[pri_index].item->item_id == primary_id) {
            break;
        }
    }

    if (pri_index >= h->meta.iloc.item_count) {
        return 0;
    }
    struct heif_item *pri_item = &h->items[pri_index];
    if (pri_item->type == TYPE2UINT("grid")) {
        // grid must have iref and decode the refered data
        num += decode_grid_items(h, pri_item, p);
    } else if (pri_item->type == TYPE2UINT("hvc1")) {
        VINFO(heif, "primary loc at %" PRIu64, h->items[pri_index].item->base_offset);
        struct hvcC_box *conf = get_hvcc_from_item_id(h, primary_id);
        decode_hvc1(pri_item->data, pri_item->length, (uint8_t **)&p->pixels, &conf->hps);
        num ++;
    }
    return num;
}

static int
decode_reference_items_for_primary(HEIF *h, int primary_id, struct pic *p UNUSED)
{
    struct itemtype_ref_box *ref;
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        // uint32_t from_id = h->meta.iref.refs[i].from_item_id;
        // VDBG(heif, "from_id %d", from_id);
        ref = &h->meta.iref.refs[i];
        for (int j = 0; j < ref->ref_count; j++) {
            int id = ref->to_item_ids[j];
            if (id == primary_id) {
                struct heif_item *r = find_item_by_id(h, ref->from_item_id);
                if (r->type == TYPE2UINT("hvc1") && ref->type == TYPE2UINT("auxl")) {
                    struct hvcC_box *conf = get_hvcc_from_item_id(h, r->item->item_id);
                    struct auxC_box *auxc = get_auxc_from_item_id(h, r->item->item_id);
                    assert(auxc != NULL);
                    if ((strcmp(auxc->aux_type, "urn:mpeg:hevc:2015:auxid:1") == 0) ||
                        (strcmp(auxc->aux_type, "urn:mpeg:avc:2015:auxid:1") == 0) ||
                        (strcmp(auxc->aux_type, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"))) {
                        // alpha channel
                        VDBG(heif, "decoding auxl image: alpha plane");
                        struct pic *p1 = HEIF_load_one(p->width, p->height, r->data, r->length, &conf->hps);
                        blend_BGRA32_8bit_alpha((uint8_t*)(p->pixels), (uint8_t*)p1->pixels, p->pitch, p->height);
                        pic_free(p1);
                    } else if ((strcmp(auxc->aux_type, "urn:mpeg:hevc:2015:auxid:1") == 0) ||
                        (strcmp(auxc->aux_type, "urn:mpeg:avc:2015:auxid:1") == 0)) {
                        //depth channel
                        VDBG(heif, "decoding auxl image: depth plane");
                    }
                    
                } else if (r->type == TYPE2UINT("Exif") && ref->type == TYPE2UINT("cdsc")) {
                    VDBG(heif, "exif %" PRIu64 "", r->length);
                    struct exif_block ex;
                    ex.exif_tiff_header_offset = ((uint32_t)r->data[0]<<24|r->data[1]<<16|r->data[2]<<8|r->data[3]);
                    ex.payload = r->data+5;
                    VDBG(heif, "exif tiff %d payload %s", ex.exif_tiff_header_offset, ex.payload);
                }
            }
        }
    }
    return 0;
}

static int
decode_items(HEIF *h, FILE *f, struct pic *p)
{
    int num = 0;
    for (int i = 0; i < h->item_num; i++) {
        pre_read_item(h, f, i);
    }

    int primary_id = get_primary_item_id(h);
    if (primary_id >= 0) {
        num = decode_primary_item(h, p);
        num += decode_reference_items_for_primary(h, primary_id, p);
        return num;
    }
    // we may have no primary id
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        VDBG(heif, "decoding %s", type2name(h->items[i].type));
        if (h->items[i].type == TYPE2UINT("Exif")) {
            VDBG(heif, "exif %" PRIu64, h->items[i].length);
        } else if (h->items[i].type == TYPE2UINT("mime")) {
            // exif mime, skip it
            // hexdump(stdout, "exif:", "", h->items[i].data, h->items[i].length);
            VDBG(heif, "mime %" PRIu64, h->items[i].length);
        } else if (h->items[i].type == TYPE2UINT("hvc1")) {
            //take it as real coded data
            VINFO(heif, "decoding id 0x%p len %" PRIu64, (void*)h->items[i].data, h->items[i].length);
            if (num == 1) {
                file_enqueue_pic(p);
            }
            VINFO(heif, "decoding sub items, width %d, height %d", p->width, p->height);
            struct hvcC_box *conf = get_hvcc_from_item_id(h, h->items[i].item->item_id);
            struct pic *p1 = HEIF_load_one(p->width, p->height, h->items[i].data, h->items[i].length, &conf->hps);
            file_enqueue_pic(p1);
            num++;
        } else if (h->items[i].type == TYPE2UINT("grid")) {
            decode_grid_items(h, h->items + i, p);
        }
    }
    return num;
}

static int
decode_moov(FILE *f, struct moov_box *b)
{
    int n = 0;
    uint32_t offset, size;
    for (int i = 0; i <b->trak_num; i++) {
        // chunk coun
        uint32_t chunk_count = b->trak[i].mdia.minf.stbl.stco.entry_count;
        int width = (int)fix16_16(b->trak[i].tkhd.width);
        int height = (int)fix16_16(b->trak[i].tkhd.height);
        assert(chunk_count == b->trak[i].mdia.minf.stbl.stsc.entry_count);
        // printf("width %d, height %d, chunk count %d\n", width, height, chunk_count);
        for (uint32_t j = 0; j < chunk_count; j++) {
            offset = b->trak[i].mdia.minf.stbl.stco.chunk_offset[j];
            int sample_num = b->trak[i].mdia.minf.stbl.stsc.sample_per_chunk[j];
            // printf("sample num %d\n", sample_num);
            n += sample_num;
            // int first = b->trak[i].mdia.minf.stbl.stsc.first_chunk[j];
            fseek(f, offset, SEEK_SET);
            for (int k = 0; k < sample_num; k++) {
                size = b->trak[i].mdia.minf.stbl.stsz.entry_size[k];
                uint8_t *data = malloc(size);
                fread(data, size, 1, f);
                // printf("load one for %d, length %d\n", k, size);
                struct pic *p1 = HEIF_load_one(width, height, data,size, NULL);
                file_enqueue_pic(p1);
                // free(data);
            }
        }
    }
    return n;
}

static struct pic*
HEIF_load(const char *filename, int skip_flag)
{
    struct pic *p = pic_alloc(sizeof(HEIF));
    HEIF *h = p->pic;
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    int64_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size -= read_ftyp(f, &h->ftyp);
    while (size > 0) {
        struct box b;
        uint32_t type = probe_box(f, &b);
        VDBG(heif, "TOP: %s, left %"PRIu64", box %"PRIu64"", type2name(type), size, b.size);
        switch (type) {
        case FOURCC2UINT('m', 'e', 't', 'a'):
            read_meta_box(f, &h->meta);
            break;
        case FOURCC2UINT('m', 'o', 'o', 'v'):
            h->moov = realloc(h->moov, (h->moov_num + 1)*sizeof(struct moov_box));
            read_moov_box(f, h->moov + h->moov_num);
            h->moov_num++;
            break;
        case FOURCC2UINT('m', 'd', 'a', 't'):
            // h->mdat_num++;
            // if (h->mdat_num == 1) {
            //     h->mdat = calloc(1, sizeof(struct mdat_box));
            // } else {
            //     h->mdat = realloc(h->mdat, h->mdat_num * sizeof(struct mdat_box));
            // }
            // read_mdat_box(f, h->mdat + h->mdat_num - 1);
            fseek(f, b.size, SEEK_CUR);
            break;
        default:
            fseek(f, b.size, SEEK_CUR);
            break;
        }
        size -= b.size;
        
        VDBG(heif, "%s, read %" PRIu64 ", left %" PRIu64 "", UINT2TYPE(type),
             b.size, size);
    }

    // extract some info from meta box
    if (get_primary_item_id(h) != -1) {
        struct ispe_box *ispe = get_ispe_by_item_id(h, get_primary_item_id(h));
        p->height = ispe->image_height;
        p->width = ispe->image_width;
        VDBG(heif, "width %d, height %d", p->width, p->height);
    }
    // for (int i = 0; i < h->meta.iprp.ipco.n_property; i++) {
    //     VDBG(heif, "iprp ipco %s", type2name(h->meta.iprp.ipco.property[i]->type));
    //     if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
    //         p->width = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width;
    //         p->height = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height;
    //         VDBG(heif, "width %d, height %d", p->width, p->height);
    //     }
    // }
    h->item_num = h->meta.iloc.item_count;
    h->items = calloc(h->meta.iloc.item_count, sizeof(struct heif_item));
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        h->items[i].item = &h->meta.iloc.items[i];
        for (int j = 0; j < (int)h->meta.iinf.entry_count; j++) {
            if (h->meta.iinf.item_infos[j].item_id == h->items[i].item->item_id) {
                h->items[i].type = h->meta.iinf.item_infos[j].item_type;
                VDBG(heif, "item %d, type %s", h->items[i].item->item_id,
                       type2name(h->items[i].type));
            }
        }
    }

    p->width = ((p->width + 3) >> 2) << 2;
    p->depth = 32;
    p->pitch = ((((p->width + 15) >> 4) * 16 * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = malloc(p->pitch * p->height);
    p->format = CS_PIXELFORMAT_RGB888;
    int n = 0;
    if (!skip_flag) {
        n = decode_items(h, f, p);

        for (int i = 0; i < h->moov_num; i++) {
            n += decode_moov(f, h->moov + i);
        }
    }

    fclose(f);
    if (n  == 1) {
        return p;
    }
    return NULL;
}

static void
HEIF_free(struct pic *p)
{
    HEIF * h = (HEIF *)p->pic;
    // free_hevc_param_set();
    if (h->ftyp.compatible_brands)
        free(h->ftyp.compatible_brands);

    struct meta_box *m = &h->meta;
    free_meta_box(m);

    for (int i = 0; i < h->moov_num; i++) {
        free_moov_box(h->moov + i);
    }
    free(h->moov);

    if (h->items) {
        for (int i = 0; i < h->item_num; i++) {
            free(h->items[i].data);
        }
        free(h->items);
    }

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
    fprintf(f, " item_id=%d", get_primary_item_id(h));
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iloc);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        fprintf(f, "\t\t");
        if (h->meta.iloc.version == 1) {
            fprintf(f, "construct_method=%d,", h->meta.iloc.items[i].construct_method);
        }
        fprintf(f, "item_id=%d,data_ref_id=%d,base_offset=%"PRIu64",extent_count=%d\n",
                h->meta.iloc.items[i].item_id, h->meta.iloc.items[i].data_ref_id,
                h->meta.iloc.items[i].base_offset, h->meta.iloc.items[i].extent_count);
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
    for (int i = 0; i < h->meta.iprp.ipco.n_property; i++) {
        fprintf(f, "\t\t");
        print_box(f, h->meta.iprp.ipco.property[i]);
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            struct ispe_box *ispe = (struct ispe_box *)h->meta.iprp.ipco.property[i];
            fprintf(f, ", width %d, height %d", ispe->image_width, ispe->image_height);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("pixi")) {
            struct pixi_box *pixi = (struct pixi_box *)h->meta.iprp.ipco.property[i];
            fprintf(f, ", num_channels %d", pixi->num_channels);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("clap")) {
            struct clap_box *clap = (struct clap_box *)h->meta.iprp.ipco.property[i];
            fprintf(f, ", cleanApertureHeightD %d, cleanApertureHeightN %d, "
                    "cleanApertureWidthD %d, cleanApertureWidthN %d,",
                    clap->cleanApertureHeightD, clap->cleanApertureHeightN,
                    clap->cleanApertureWidthD, clap->cleanApertureWidthN);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("irot")) {
            struct irot_box *irot = (struct irot_box *)h->meta.iprp.ipco.property[i];
            fprintf(f, ", angle %d", irot->angle);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("hvcC")) {
            struct hvcC_box * hvcc = (struct hvcC_box *)(h->meta.iprp.ipco.property[i]);

            fprintf(f, " config version %d, profile_space %d, tier_flag %d, profile_idc %d\n", hvcc->configurationVersion,
                    hvcc->general_profile_space, hvcc->general_tier_flag, hvcc->general_profile_idc);
            uint16_t min_spatial_segmentation_idc = hvcc->min_spatial_segmentation_idc;
            fprintf(f, "\t\t\t\tgeneral_profile_compatibility_flags 0x%x\n",
                hvcc->general_profile_compatibility_flags);
            fprintf(f, "\t\t\t\tmin_spatial_segmentation_idc %d, general_level_idc %d\n",
                min_spatial_segmentation_idc, hvcc->general_level_idc);
            fprintf(
                f, "\t\t\t\tgeneral_constraint_indicator_flags 0x%" PRIx64 "\n",
                (uint64_t)hvcc->general_constraint_indicator_flags[0] << 40 |
                    (uint64_t)hvcc->general_constraint_indicator_flags[1] << 32 |
                    hvcc->general_constraint_indicator_flags[2] << 24 |
                    hvcc->general_constraint_indicator_flags[3] << 16 |
                    hvcc->general_constraint_indicator_flags[4] << 8 |
                    hvcc->general_constraint_indicator_flags[5]);

            fprintf(f, "\t\t\t\tparallelismType %d, chroma_format_idc %d, bit_depth_luma_minus8 %d\n\t\t\t\tbit_depth_chroma_minus8 %d\n", 
                hvcc->parallelismType, hvcc->chroma_format_idc, hvcc->bit_depth_luma_minus8, hvcc->bit_depth_chroma_minus8);
            fprintf(f, "\t\t\t\tavgframerate %d, constantframerate %d\n\t\t\t\tnumtemporalLayers %d, temporalIdNested %d, lengthSizeMinusOne %d\n",
                hvcc->avgframerate, hvcc->constantframerate, hvcc->numtemporalLayers, hvcc->temporalIdNested, hvcc->lengthSizeMinusOne);
            for (int j = 0; j < hvcc->num_of_arrays; j ++) {
                fprintf(f, "\t\t\t\t");
                fprintf(f, "completeness %d, nal_unit_type %d", hvcc->nal_arrays[j].array_completeness, hvcc->nal_arrays[j].nal_unit_type);
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
            fprintf(f, "\t\t\tessential %d, id %d \n", ipma->property_index[j] >> 15, ipma->property_index[j] & 0x7fff);
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
    if (h->moov_num) {
        for (int i = 0; i < h->moov_num; i++) {
            fprintf(f, "\t");
            struct moov_box *m = h->moov+i;
            print_box(f, m);
            fprintf(f, "\n");
            fprintf(f, "\ttrak num %d\n", m->trak_num);
            for (int i = 0; i < m->trak_num; i++) {
                fprintf(f, "\t\t");
                print_box(f, m->trak + i);
                fprintf(f, "\n\t\t");
                print_box(f, &m->trak[i].mdia);
                fprintf(f, "\n\t\t");
                fprintf(f, "\thandler type \"%s\"", type2name(m->trak[i].mdia.hdlr.handler_type));
                fprintf(f, ": %s\n\t\t", m->trak[i].mdia.hdlr.name);
                print_box(f, &m->trak[i].mdia.minf.stbl);
                fprintf(f, "\n\t\tchunk count: %d\n", m->trak[i].mdia.minf.stbl.stco.entry_count);
                for (uint32_t j = 0; j< m->trak[i].mdia.minf.stbl.stco.entry_count; j++) {
                    fprintf(f, "\t\toffset: 0x%x\n", m->trak[i].mdia.minf.stbl.stco.chunk_offset[j]);
                    fprintf(f, "\t\tsize: 0x%x\n", m->trak[i].mdia.minf.stbl.stsz.entry_size[j]);

                }
            }
        }
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



