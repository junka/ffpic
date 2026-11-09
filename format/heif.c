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

static void HEIF_free(struct pic *p);

static int
HEIF_probe(FILE *f)
{
    static char* heif_types[] = {
        "heic",
        "heix",
        "heis",
        "heim",
        "hevc",
        "hevx",
    };

    if (f == NULL) {
        return -ENOENT;
    }
    struct ftyp_box h = {0};
    int len = read_ftyp(f, &h);
    int matched = 0;
    if (len >= 0 && h.major_brand == TYPE2UINT("ftyp")) {
        // VDBG(heif, "len %d minor_version %s", len , type2name(h.minor_version));
        if (len > 12 && (h.minor_version == TYPE2UINT("mif1")||h.minor_version == TYPE2UINT("msf1"))) {
            for (int j = 0; j < (len -12)>>2 && !matched; j ++) {
                for (int i = 0; i < (int)(sizeof(heif_types)/sizeof(heif_types[0])); i ++) {
                    if (h.compatible_brands[j] == TYPE2UINT(heif_types[i])) {
                        matched = 1;
                        break;
                    }
                }
            }
        } else {
            for (int i = 0; i < (int)(sizeof(heif_types)/sizeof(heif_types[0])); i ++) {
                if (h.minor_version == TYPE2UINT(heif_types[i])) {
                    matched = 1;
                    break;
                }
            }
        }
    }
    // read_ftyp allocated this, so every exit has to release it
    free(h.compatible_brands);

    if (len < 0) {
        return -EBADF;
    }
    return matched ? 0 : -EINVAL;
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
    free_hevc_param_set(&b->hps);
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
            void **slot = b->nal_arrays[i].xps
                              ? (void **)b->nal_arrays[i].xps + j
                              : NULL;
            /* a bad parameter set leaves the slot empty; the slice header
             * then fails its lookup instead of the loader aborting */
            if (parse_nalu(nals[j].nal_units, nals[j].unit_length, NULL, 0, 0,
                           slot, &b->hps, 0) < 0) {
                VERR(heif, "failed to parse a parameter set");
            }
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
    long entry_start = ftell(f);
    read_VisualSampleEntry(f, &e->sample);
    assert(e->sample.entry.type == FOURCC2UINT('h', 'v', 'c', '1') ||
           e->sample.entry.type == FOURCC2UINT('h', 'e', 'v', '1'));
    read_hvcc_box(f, (struct box **)(&e->config));
    fseek(f, entry_start + (long)e->sample.entry.size, SEEK_SET);
    return 0;
}

struct heif_item *
find_item_by_id(HEIF *h, uint32_t id)
{
    for (int i = 0; i < h->meta.iloc.item_count; i++) {
        if (h->items[i].item->item_id == id) {
            return h->items+i;
        }
    }
    return NULL;
}

struct box *
get_ipma_item_from_item_id(HEIF *h, uint32_t id, uint32_t fourcc)
{
    for (uint32_t i = 0; i < h->meta.iprp.ipma.entry_count; i++) {
        for (int j = 0; j < h->meta.iprp.ipma.entries[i].association_count; j++) {
            if (h->meta.iprp.ipma.entries[i].item_id == id) {
                for (int k = 0; k < h->meta.iprp.ipma.entries[i].association_count; k++) {
                    // int essential = h->meta.iprp.ipma.entries[i].property_index[k] >> 15;
                    int property_index = h->meta.iprp.ipma.entries[i].property_index[k] & 0x7FFF;
                    if (property_index < 1 ||
                        property_index > (int)h->meta.iprp.ipco.n_property) {
                        continue;
                    }
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
get_hvcc_from_item_id(HEIF *h, uint32_t id)
{
    return (struct hvcC_box *)get_ipma_item_from_item_id(h, id, FOURCC2UINT('h', 'v', 'c', 'C'));
}

struct auxC_box *
get_auxc_from_item_id(HEIF *h, uint32_t id)
{
    return (struct auxC_box *)get_ipma_item_from_item_id(h, id, FOURCC2UINT('a', 'u', 'x', 'C'));

}

int has_primary_item(HEIF *h)
{
    return h->meta.pitm.type == FOURCC2UINT('p', 'i', 't', 'm');
}

uint32_t get_primary_item_id(HEIF *h)
{
    return h->meta.pitm.item_id;
}

static struct infe_box *
heif_item_info(HEIF *h, uint32_t id)
{
    for (uint32_t i = 0; i < h->meta.iinf.entry_count; i++) {
        if (h->meta.iinf.item_infos[i].item_id == id) {
            return &h->meta.iinf.item_infos[i];
        }
    }
    return NULL;
}

/* An item carrying a protection index is encrypted; the payload cannot be
 * decoded without the key, so the decoder must lay off it. */
static bool
heif_item_protected(HEIF *h, uint32_t id)
{
    struct infe_box *infe = heif_item_info(h, id);

    return infe && infe->item_protection_index != 0;
}

static int
heif_item_index(HEIF *h, uint32_t id)
{
    for (int i = 0; i < h->meta.iloc.item_count; i++) {
        if (h->items[i].item->item_id == id) {
            return i;
        }
    }
    return -1;
}

static int
heif_ref_targets(const struct itemtype_ref_box *r, uint32_t id)
{
    for (int i = 0; i < r->ref_count; i++) {
        if (r->to_item_ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

static uint64_t
heif_item_size(HEIF *h, uint32_t id)
{
    int idx = heif_item_index(h, id);
    uint64_t total = 0;

    if (idx < 0) {
        return 0;
    }
    for (int i = 0; i < h->items[idx].item->extent_count; i++) {
        total += h->items[idx].item->extents[i].extent_length;
    }
    return total;
}

/* XMP is a mime item whose content type is the RDF/XML one; other mime
 * items describe the picture differently. */
static bool
heif_is_xmp(HEIF *h, uint32_t id)
{
    struct infe_box *infe = heif_item_info(h, id);

    return infe && infe->content_type &&
           strncmp(infe->content_type, "application/rdf+xml", 19) == 0;
}

struct ispe_box *
get_ispe_by_item_id(HEIF *h, uint32_t id)
{
    for (uint32_t i = 0; i < h->meta.iprp.ipma.entry_count; i++) {
        if (h->meta.iprp.ipma.entries[i].item_id == id) {
            for (int k = 0; k < h->meta.iprp.ipma.entries[i].association_count; k++) {
                // int essential = h->meta.iprp.ipma.entries[i].property_index[k] >> 15;
                int property_index = h->meta.iprp.ipma.entries[i].property_index[k] & 0x7FFF;
                if (property_index < 1 ||
                    property_index > (int)h->meta.iprp.ipco.n_property) {
                    continue;
                }
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

/* Concatenate every extent into one item payload.  Extents are stored back
 * to back in the file (method 0) or in the meta idat box (method 1); method
 * 2 addresses other items, which cannot be resolved into a byte range. */
static void
pre_read_item(HEIF * h, FILE *f, uint32_t idx)
{
    const struct item_location *loc = h->items[idx].item;
    uint64_t total = 0, off = 0;

    h->items[idx].data = NULL;
    h->items[idx].length = 0;
    if (loc->extent_count <= 0) {
        return;
    }
    for (int i = 0; i < loc->extent_count; i++) {
        total += loc->extents[i].extent_length;
    }
    h->items[idx].data = malloc(total ? (size_t)total : 1);
    if (!h->items[idx].data) {
        VERR(heif, "cannot allocate %" PRIu64 " bytes for item %u", total,
             loc->item_id);
        return;
    }
    for (int i = 0; i < loc->extent_count; i++) {
        const struct item_extent *e = &loc->extents[i];
        uint64_t at = loc->base_offset + e->extent_offset;

        if (e->extent_length == 0) {
            continue;
        }
        if (loc->construct_method == 0) {
            if (fseek(f, (long)at, SEEK_SET) != 0 ||
                fread(h->items[idx].data + off, 1,
                      (size_t)e->extent_length, f) !=
                    (size_t)e->extent_length) {
                VERR(heif, "cannot read item %u extent %d", loc->item_id, i);
                goto fail;
            }
        } else if (loc->construct_method == 1) {
            uint64_t idat_len =
                h->meta.idat.data ? (uint64_t)h->meta.idat.size - 8 : 0;

            if (at > idat_len || e->extent_length > idat_len - at) {
                VERR(heif, "item %u extent %d runs past the idat box",
                     loc->item_id, i);
                goto fail;
            }
            memcpy(h->items[idx].data + off, h->meta.idat.data + at,
                   (size_t)e->extent_length);
        } else {
            VWARN(heif, "item %u uses construction method %u, not supported",
                  loc->item_id, (unsigned)loc->construct_method);
            goto fail;
        }
        off += e->extent_length;
    }
    h->items[idx].length = off;
    VDBG(heif, "read item %u, length %" PRIu64 ", method %u, extents %d",
         loc->item_id, h->items[idx].length, (unsigned)loc->construct_method,
         loc->extent_count);
    return;
fail:
    free(h->items[idx].data);
    h->items[idx].data = NULL;
    h->items[idx].length = 0;
}

int decode_hvc1(uint8_t *data, uint64_t len, uint8_t *pixels, int pitch,
                int height, struct hevc_param_set *hps, int out_bits) {
    // hexdump(stdout, "coded ", "", data, 256);
    uint8_t *p = data;
    while (len >= 4) {
        uint32_t sample_len = (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
        p += 4;
        len -= 4;
        if (sample_len > len) {
            VERR(heif, "sample length %u runs past the item", sample_len);
            return -EINVAL;
        }
        if (parse_nalu(p, (int)sample_len, pixels, pitch, height, NULL, hps,
                       out_bits) < 0) {
            VERR(heif, "failed to parse nal unit");
            return -EINVAL;
        }
        p += sample_len;
        len -= sample_len;
    }
    if (len != 0) {
        VERR(heif, "%" PRIu64 " trailing bytes in the item", len);
        return -EINVAL;
    }
    return 0;
}

/* Exif and XMP describe the primary item through cdsc; a thumbnail points
 * at it through thmb. */
static void
heif_collect_metadata(HEIF *h, uint32_t primary, FILE *f)
{
    if (!h->items) {
        return;
    }
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *r = &h->meta.iref.refs[i];
        int idx;
        uint32_t type;

        if ((r->type != TYPE2UINT("cdsc") &&
             r->type != TYPE2UINT("thmb")) ||
            !primary || !heif_ref_targets(r, primary)) {
            continue;
        }
        idx = heif_item_index(h, r->from_item_id);
        if (idx < 0) {
            continue;
        }
        type = h->items[idx].type;
        if (r->type == TYPE2UINT("thmb")) {
            if (!h->thumb_id) {
                h->thumb_id = r->from_item_id;
            }
        } else if (type == TYPE2UINT("Exif")) {
            if (!h->exif_id) {
                h->exif_id = r->from_item_id;
            }
        } else if (type == TYPE2UINT("mime")) {
            if (!h->xmp_id && heif_is_xmp(h, r->from_item_id)) {
                h->xmp_id = r->from_item_id;
            }
        }
    }
    /* tolerate writers that omit the cdsc reference */
    if (!h->exif_id) {
        for (int i = 0; i < h->meta.iloc.item_count; i++) {
            if (h->items[i].type == TYPE2UINT("Exif")) {
                h->exif_id = h->items[i].item->item_id;
                break;
            }
        }
    }
    if (!h->xmp_id) {
        for (int i = 0; i < h->meta.iloc.item_count; i++) {
            uint32_t id = h->items[i].item->item_id;

            if (h->items[i].type == TYPE2UINT("mime") &&
                heif_is_xmp(h, id)) {
                h->xmp_id = id;
                break;
            }
        }
    }
    if (h->exif_id) {
        int idx = heif_item_index(h, h->exif_id);

        if (idx >= 0) {
            pre_read_item(h, f, idx);
            if (h->items[idx].data && h->items[idx].length) {
                h->exif_orientation =
                    media_exif_orientation(h->items[idx].data,
                                           h->items[idx].length);
            }
            free(h->items[idx].data);
            h->items[idx].data = NULL;
        }
    }
}

static void
heif_pic_free(struct pic *p)
{
    free(p->pixels);
    pic_free(p);
}

/* hvcC carries the sequence bit depth; anything above 8 keeps the precision
 * in 16 bit channels. */
static int
heif_output_bits(const struct hvcC_box *conf)
{
    return conf && conf->bit_depth_luma_minus8 > 0 ? 64 : 32;
}

static struct pic *
heif_pic_alloc(int width, int height, int bits)
{
    if (width <= 0 || height <= 0) {
        VERR(heif, "item has no usable size: %dx%d", width, height);
        return NULL;
    }
    struct pic *p = pic_alloc(sizeof(HEIF));
    p->width = ((width + 3) >> 2) << 2;
    p->height = height;
    p->depth = bits;
    p->pitch = ((p->width + 15) >> 4) * 16 * (p->depth >> 3);
    /* the width is padded to a multiple of four, so zero the padding too */
    p->pixels = calloc(1, (size_t)p->pitch * p->height);
    p->format = bits > 32 ? CS_PIXELFORMAT_BGRA64
                            : CS_PIXELFORMAT_BGRA_BYTES;
    if (!p->pixels) {
        pic_free(p);
        return NULL;
    }
    return p;
}

static struct pic *
HEIF_load_one(int width, int height, uint8_t *data, int length,
              struct hevc_param_set *hps, int bits) {
    struct pic *p = heif_pic_alloc(width, height, bits);
    if (!p) {
        return NULL;
    }
    if (decode_hvc1(data, length, p->pixels, p->pitch, p->height, hps, bits) < 0) {
        VERR(heif, "failed to decode a %dx%d item", p->width, p->height);
        heif_pic_free(p);
        return NULL;
    }
    return p;
}

static int
read_grid_header(struct heif_item *it, struct grid *ig)
{
    if (it->length < 4) {
        return -EINVAL;
    }
    ig->version = it->data[0];
    ig->flags = it->data[1];
    ig->row_minus_one = it->data[2];
    ig->columns_minus_one = it->data[3];
    if ((ig->flags & 1) == 0) {
        if (it->length < 8) {
            return -EINVAL;
        }
        uint16_t width = *(uint16_t *)(it->data + 4);
        uint16_t height = *(uint16_t *)(it->data + 6);
        ig->output_width = SWAP(width);
        ig->output_height = SWAP(height);
    } else {
        if (it->length < 12) {
            return -EINVAL;
        }
        uint32_t width = *(uint32_t *)(it->data + 4);
        uint32_t height = *(uint32_t *)(it->data + 8);
        ig->output_width = SWAP(width);
        ig->output_height = SWAP(height);
    }
    return 0;
}

/* the dimg reference lists the tiles in raster order, so tile j belongs at
 * (j % columns, j / columns) on the grid canvas */
static int
decode_grid_items(HEIF *h, struct heif_item *it, struct pic *p)
{
    struct grid ig;
    if (read_grid_header(it, &ig) < 0) {
        VERR(heif, "malformed grid item");
        return 0;
    }
    int rows = ig.row_minus_one + 1;
    int cols = ig.columns_minus_one + 1;
    VDBG(heif, "grid %dx%d, %dx%d tiles", ig.output_width, ig.output_height,
         rows, cols);
    if (ig.output_width == 0 || ig.output_height == 0 ||
        ig.output_width > (uint32_t)p->width ||
        ig.output_height > (uint32_t)p->height) {
        VERR(heif, "grid %ux%u does not fit a %dx%d canvas", ig.output_width,
             ig.output_height, p->width, p->height);
        return 0;
    }
    int tile_w = ((int)ig.output_width + cols - 1) / cols;
    int tile_h = ((int)ig.output_height + rows - 1) / rows;

    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *ref = &h->meta.iref.refs[i];
        if (ref->ref_count != rows * cols) {
            continue;
        }
        if (ref->from_item_id != it->item->item_id ||
            ref->type != TYPE2UINT("dimg")) {
            continue;
        }
        int decoded = 0;
        for (int j = 0; j < ref->ref_count; j++) {
            struct heif_item *item = find_item_by_id(h, ref->to_item_ids[j]);
            struct hvcC_box *conf =
                get_hvcc_from_item_id(h, ref->to_item_ids[j]);
            struct ispe_box *ispe =
                get_ispe_by_item_id(h, ref->to_item_ids[j]);
            if (!item || item->type != TYPE2UINT("hvc1") || !conf || !ispe ||
                !item->data || item->length == 0) {
                VERR(heif, "grid tile %d is not a decodable hvc1 item", j);
                continue;
            }
            struct pic *tile = HEIF_load_one((int)ispe->image_width,
                                             (int)ispe->image_height,
                                             item->data, item->length,
                                             &conf->hps,
                                             heif_output_bits(conf));
            if (!tile) {
                VERR(heif, "failed to decode grid tile %d", j);
                continue;
            }
            int x = (j % cols) * tile_w;
            int y = (j / cols) * tile_h;
            int copy_w = tile_w;
            int copy_h = tile_h;
            if (copy_w > (int)ig.output_width - x) {
                copy_w = (int)ig.output_width - x;
            }
            if (copy_h > (int)ig.output_height - y) {
                copy_h = (int)ig.output_height - y;
            }
            if (copy_w > (int)ispe->image_width) {
                copy_w = (int)ispe->image_width;
            }
            if (copy_h > (int)ispe->image_height) {
                copy_h = (int)ispe->image_height;
            }
            int bpp = p->depth >> 3;
            if (tile->depth != p->depth) {
                VERR(heif, "grid tile %d is %d-bit, grid is %d-bit", j,
                     tile->depth, p->depth);
                heif_pic_free(tile);
                return 0;
            }
            for (int r = 0; r < copy_h; r++) {
                memcpy((uint8_t *)p->pixels + (size_t)(y + r) * p->pitch +
                           (size_t)x * bpp,
                       (uint8_t *)tile->pixels + (size_t)r * tile->pitch,
                       (size_t)copy_w * bpp);
            }
            heif_pic_free(tile);
            decoded++;
        }
        if (decoded != rows * cols) {
            VERR(heif, "only %d of %d grid tiles decoded", decoded,
                 rows * cols);
            return 0;
        }
        return 1;
    }
    VERR(heif, "grid item %d has no matching dimg reference", it->item->item_id);
    return 0;
}

static int
decode_primary_item(HEIF *h, struct pic *p)
{
    int num = 0;
    if (!has_primary_item(h)) {
        //no primary
        return num;
    }
    uint32_t primary_id = get_primary_item_id(h);
    VINFO(heif, "primary id %u", primary_id);
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
    if (heif_item_protected(h, primary_id)) {
        VWARN(heif, "item %u is protected, not decoding", primary_id);
        return 0;
    }
    if (!pri_item->data || pri_item->length == 0) {
        VERR(heif, "primary item %u has no payload", primary_id);
        return 0;
    }
    if (pri_item->type == TYPE2UINT("grid")) {
        // grid must have iref and decode the refered data
        num += decode_grid_items(h, pri_item, p);
    } else if (pri_item->type == TYPE2UINT("hvc1")) {
        VINFO(heif, "primary loc at %" PRIu64, h->items[pri_index].item->base_offset);
        struct hvcC_box *conf = get_hvcc_from_item_id(h, primary_id);
        if (!conf) {
            VERR(heif, "no hvcC for the primary item");
            return 0;
        }
        if (decode_hvc1(pri_item->data, pri_item->length, p->pixels, p->pitch,
                        p->height, &conf->hps, p->depth) < 0) {
            VERR(heif, "failed to decode the primary item");
            return 0;
        }
        num ++;
    }
    return num;
}

/* Auxiliary types from ISO/IEC 23008-12 and the CICP systems URNs: auxid 1
 * carries alpha, auxid 2 carries depth. */
static bool
heif_aux_is_alpha(const char *aux_type)
{
    return strcmp(aux_type, "urn:mpeg:hevc:2015:auxid:1") == 0 ||
           strcmp(aux_type, "urn:mpeg:avc:2015:auxid:1") == 0 ||
           strcmp(aux_type, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha") == 0;
}

static bool
heif_aux_is_depth(const char *aux_type)
{
    return strcmp(aux_type, "urn:mpeg:hevc:2015:auxid:2") == 0 ||
           strcmp(aux_type, "urn:mpeg:avc:2015:auxid:2") == 0 ||
           strcmp(aux_type, "urn:mpeg:mpegB:cicp:systems:auxiliary:depth") == 0;
}

/* A prem reference from the colour item to its alpha item says the stored
 * colour samples are already multiplied by alpha. */
static bool
heif_alpha_is_premultiplied(const HEIF *h, uint32_t color_id,
                            uint32_t alpha_id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        const struct itemtype_ref_box *r = &h->meta.iref.refs[i];
        if (r->type != FOURCC2UINT('p', 'r', 'e', 'm') ||
            r->from_item_id != color_id) {
            continue;
        }
        for (int j = 0; j < r->ref_count; j++) {
            if (r->to_item_ids[j] == alpha_id) {
                return true;
            }
        }
    }
    return false;
}

/* Only the alpha channel comes from the auxiliary plane.  The packed output
 * carries straight alpha, so a premultiplied colour item gets its samples
 * divided back by alpha. */
static void
heif_apply_alpha(struct pic *p, const struct pic *alpha, bool premultiplied)
{
    int w = p->width < alpha->width ? p->width : alpha->width;
    int h = p->height < alpha->height ? p->height : alpha->height;
    int wide = p->depth > 32;
    int amax = wide ? 65535 : 255;

    if (w != p->width || h != p->height) {
        VWARN(heif, "alpha plane %dx%d does not match the colour item %dx%d",
              alpha->width, alpha->height, p->width, p->height);
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *src = (const uint8_t *)alpha->pixels +
                             (size_t)y * alpha->pitch;
        uint8_t *dst = (uint8_t *)p->pixels + (size_t)y * p->pitch;
        for (int x = 0; x < w; x++) {
            int a;
            uint8_t *px = dst + (size_t)x * (p->depth >> 3);

            if (wide) {
                a = ((const uint16_t *)src)[x * 4 + 0];
                ((uint16_t *)px)[3] = (uint16_t)a;
            } else {
                a = src[x * 4 + 0];
                px[3] = (uint8_t)a;
            }
            if (!premultiplied) {
                continue;
            }
            for (int c = 0; c < 3; c++) {
                int v = a ? (wide ? ((uint16_t *)px)[c] : px[c]) : 0;

                if (a) {
                    v = (v * amax + a / 2) / a;
                    if (v > amax) {
                        v = amax;
                    }
                }
                if (wide) {
                    ((uint16_t *)px)[c] = (uint16_t)v;
                } else {
                    px[c] = (uint8_t)v;
                }
            }
        }
    }
}

/* Container colour information wins over the CICP codes in the HEVC VUI. */
static struct cs_profile *
heif_color_profile(HEIF *h, uint32_t item_id)
{
    struct colr_box *colr = (struct colr_box *)get_ipma_item_from_item_id(
        h, item_id, FOURCC2UINT('c', 'o', 'l', 'r'));

    if (colr) {
        if (colr->icc_data && colr->icc_len) {
            struct cs_profile *cp =
                cs_profile_from_icc(colr->icc_data, colr->icc_len);
            if (cp) {
                return cp;
            }
            VWARN(heif, "the embedded ICC profile is not supported");
        }
        if (colr->color_type == FOURCC2UINT('n', 'c', 'l', 'x')) {
            struct cs_profile *cp =
                cs_profile_from_cicp(colr->color_primaries,
                                     colr->transfer_characteristics);
            if (cp) {
                return cp;
            }
        }
    }
    struct hvcC_box *conf = get_hvcc_from_item_id(h, item_id);
    if (!conf) {
        return NULL;
    }
    for (int i = 0; i < 16; i++) {
        struct sps *sps = conf->hps.sps[i];
        if (sps && sps->vui && sps->vui->colour_description_present_flag) {
            return cs_profile_from_cicp(sps->vui->colour_primaries,
                                        sps->vui->transfer_characteristics);
        }
    }
    return NULL;
}

/* Applied to the colour samples before the alpha plane is merged, so the
 * alpha channel is never touched by the colour transform. */
static void
heif_apply_color(HEIF *h, uint32_t item_id, struct pic *p)
{
    struct cs_profile *cp = heif_color_profile(h, item_id);
    if (!cp) {
        return;
    }
    if (!cs_profile_is_identity(cp)) {
        if (p->depth > 32) {
            cs_profile_apply_bgra16(cp, (uint8_t *)p->pixels, p->width,
                                    p->height, p->pitch);
        } else {
            cs_profile_apply_bgra(cp, (uint8_t *)p->pixels, p->width,
                                  p->height, p->pitch);
        }
    }
    cs_profile_free(cp);
}

static int
decode_reference_items_for_primary(HEIF *h, uint32_t primary_id, struct pic *p)
{
    struct itemtype_ref_box *ref;
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        // uint32_t from_id = h->meta.iref.refs[i].from_item_id;
        // VDBG(heif, "from_id %d", from_id);
        ref = &h->meta.iref.refs[i];
        for (int j = 0; j < ref->ref_count; j++) {
            uint32_t id = ref->to_item_ids[j];
            if (id == primary_id) {
                struct heif_item *r = find_item_by_id(h, ref->from_item_id);
                if (!r) {
                    continue;
                }
                if (r->type == TYPE2UINT("hvc1") && ref->type == TYPE2UINT("auxl")) {
                    struct hvcC_box *conf = get_hvcc_from_item_id(h, r->item->item_id);
                    struct auxC_box *auxc = get_auxc_from_item_id(h, r->item->item_id);
                    if (!conf || !auxc || !auxc->aux_type) {
                        continue;
                    }
                    if (heif_aux_is_alpha(auxc->aux_type)) {
                        if (heif_item_protected(h, r->item->item_id)) {
                            VWARN(heif, "alpha item %u is protected, skipping",
                                  r->item->item_id);
                            continue;
                        }
                        if (!r->data || r->length == 0) {
                            VWARN(heif, "alpha item %u has no payload",
                                  r->item->item_id);
                            continue;
                        }
                        bool prem = heif_alpha_is_premultiplied(
                            h, primary_id, r->item->item_id);
                        struct ispe_box *ispe =
                            get_ispe_by_item_id(h, r->item->item_id);
                        int aw = ispe ? (int)ispe->image_width : p->width;
                        int ah = ispe ? (int)ispe->image_height : p->height;
                        VDBG(heif, "decoding auxl image: alpha plane%s",
                             prem ? " (premultiplied)" : "");
                        struct pic *p1 = HEIF_load_one(aw, ah, r->data,
                                                       r->length, &conf->hps,
                                                       p->depth);
                        if (!p1) {
                            VWARN(heif, "cannot decode the alpha item");
                            continue;
                        }
                        heif_apply_alpha(p, p1, prem);
                        heif_pic_free(p1);
                    } else if (heif_aux_is_depth(auxc->aux_type)) {
                        VDBG(heif, "skipping auxl image: depth plane");
                    } else {
                        VDBG(heif, "skipping auxl image of type %s",
                             auxc->aux_type);
                    }
                } else if (r->type == TYPE2UINT("Exif") && ref->type == TYPE2UINT("cdsc")) {
                    VDBG(heif, "exif %" PRIu64 "", r->length);
                    // the exif payload starts after the 4-byte tiff header offset
                    VDBG(heif, "exif tiff %d payload %s",
                         (int)((uint32_t)r->data[0] << 24 | r->data[1] << 16 |
                               r->data[2] << 8 | r->data[3]),
                         r->data + 5);
                }
            }
        }
    }
    return 0;
}

/* tiles of a dimg grid are not pictures of their own; the grid composes them */
static bool
is_grid_tile(HEIF *h, uint32_t id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *ref = &h->meta.iref.refs[i];
        if (ref->type != TYPE2UINT("dimg")) {
            continue;
        }
        for (int j = 0; j < ref->ref_count; j++) {
            if (ref->to_item_ids[j] == id) {
                return true;
            }
        }
    }
    return false;
}

/* An auxiliary item is the source of an auxl reference; it is a plane of
 * another picture, never a picture to show on its own. */
static bool
is_aux_item(HEIF *h, uint32_t id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *ref = &h->meta.iref.refs[i];
        if (ref->type == TYPE2UINT("auxl") && ref->from_item_id == id) {
            return true;
        }
    }
    return false;
}

/* irot, imir and clap rewrite the packed picture after the alpha plane has
 * been merged, in the order the properties are associated in ipma. */
static void
heif_apply_transforms(HEIF *h, uint32_t item_id, struct pic *p)
{
    struct packed_frame frame = {
        .pixels = p->pixels,
        .width = p->width,
        .height = p->height,
        .pitch = p->pitch,
        .bpp = p->depth >> 3,
    };

    if (packed_apply_transforms(&h->meta.iprp, item_id, &frame) < 0) {
        return;
    }
    p->pixels = frame.pixels;
    p->width = frame.width;
    p->height = frame.height;
    p->pitch = frame.pitch;
}

/* A thumbnail is referenced by the picture it depicts, not a picture of its
 * own to show. */
static bool
is_thumbnail(HEIF *h, uint32_t id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *ref = &h->meta.iref.refs[i];
        if (ref->type == TYPE2UINT("thmb") && ref->from_item_id == id) {
            return true;
        }
    }
    return false;
}

/* A grid is composed of tiles; the first tile's hvcC tells the output
 * depth of the composed picture. */
static int
heif_grid_output_bits(HEIF *h, uint32_t grid_id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        struct itemtype_ref_box *ref = &h->meta.iref.refs[i];

        if (ref->type != TYPE2UINT("dimg") ||
            ref->from_item_id != grid_id) {
            continue;
        }
        for (int j = 0; j < ref->ref_count; j++) {
            struct hvcC_box *conf =
                get_hvcc_from_item_id(h, ref->to_item_ids[j]);
            if (conf) {
                return heif_output_bits(conf);
            }
        }
    }
    return 32;
}

static int
decode_items(HEIF *h, FILE *f, struct pic *p)
{
    int num = 0;
    for (int i = 0; i < h->item_num; i++) {
        pre_read_item(h, f, i);
    }

    if (has_primary_item(h)) {
        uint32_t primary_id = get_primary_item_id(h);

        num = decode_primary_item(h, p);
        if (!num) {
            return 0;
        }
        heif_apply_color(h, primary_id, p);
        num += decode_reference_items_for_primary(h, primary_id, p);
        heif_apply_transforms(h, primary_id, p);
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
            if (is_grid_tile(h, h->items[i].item->item_id) ||
                is_aux_item(h, h->items[i].item->item_id) ||
                is_thumbnail(h, h->items[i].item->item_id) ||
                heif_item_protected(h, h->items[i].item->item_id)) {
                continue;
            }
            //take it as real coded data
            VINFO(heif, "decoding id 0x%p len %" PRIu64, (void*)h->items[i].data, h->items[i].length);
            struct hvcC_box *conf = get_hvcc_from_item_id(h, h->items[i].item->item_id);
            struct ispe_box *ispe = get_ispe_by_item_id(h, h->items[i].item->item_id);
            if (!conf || !ispe || !h->items[i].data ||
                h->items[i].length == 0) {
                continue;
            }
            VINFO(heif, "decoding sub items, width %u, height %u", ispe->image_width, ispe->image_height);
            struct pic *p1 = HEIF_load_one((int)ispe->image_width, (int)ispe->image_height,
                                           h->items[i].data, h->items[i].length,
                                           &conf->hps, heif_output_bits(conf));
            if (!p1) {
                continue;
            }
            heif_apply_color(h, h->items[i].item->item_id, p1);
            heif_apply_transforms(h, h->items[i].item->item_id, p1);
            file_enqueue_pic(p1);
            num++;
        } else if (h->items[i].type == TYPE2UINT("grid")) {
            if (is_thumbnail(h, h->items[i].item->item_id) ||
                heif_item_protected(h, h->items[i].item->item_id)) {
                continue;
            }
            struct grid ig;
            if (read_grid_header(h->items + i, &ig) < 0) {
                continue;
            }
            struct pic *p1 = heif_pic_alloc((int)ig.output_width,
                                            (int)ig.output_height,
                                            heif_grid_output_bits(
                                                h, h->items[i].item->item_id));
            if (!p1) {
                continue;
            }
            if (decode_grid_items(h, h->items + i, p1)) {
                heif_apply_color(h, h->items[i].item->item_id, p1);
                heif_apply_transforms(h, h->items[i].item->item_id, p1);
                file_enqueue_pic(p1);
                num++;
            } else {
                heif_pic_free(p1);
            }
        }
    }
    return num;
}

static uint32_t heif_stsc_spc(const struct stsc_box *stsc, uint32_t ci) {
    uint32_t one = ci + 1, r = 0;
    for (uint32_t i = 0; i < stsc->entry_count; i++)
        if (stsc->first_chunk[i] <= one) r = stsc->sample_per_chunk[i];
    return r;
}

static int
decode_moov(FILE *f, struct moov_box *b)
{
    int total = 0;
    for (int t = 0; t < b->trak_num; t++) {
        struct trak_box *trak = &b->trak[t];
        struct stbl_box *stbl = &trak->mdia.minf.stbl;
        struct mdhd_box *mdhd = &trak->mdia.mdhd;
        if (stbl->stsz.sample_count == 0 || stbl->stco.entry_count == 0) continue;
        /* an alpha track is an auxiliary plane of the colour track, not a
         * frame of its own */
        if (trak->mdia.hdlr.handler_type == FOURCC2UINT('a','u','x','v')) {
            VDBG(heif, "skipping auxiliary video track %d", t);
            continue;
        }
        int width = (int)fix16_16(trak->tkhd.width);
        int height = (int)fix16_16(trak->tkhd.height);
        struct hevc_param_set *hps = NULL; int out_bits = 8; int stsd_idx = -1;
        for (uint32_t s = 0; s < stbl->stsd.entry_count; s++) {
            struct SampleEntry *e = stbl->stsd.entries[s];
            if (e && (e->type == FOURCC2UINT('h','v','c','1') || e->type == FOURCC2UINT('h','e','v','1'))) {
                hps = &((struct HEVCSampleEntry*)e)->config.hps;
                out_bits = heif_output_bits(&((struct HEVCSampleEntry*)e)->config);
                stsd_idx = (int)s; break;
            }
        }
        if (stsd_idx < 0) continue;
        uint32_t timescale = mdhd->timescale ? mdhd->timescale : 1;
        uint32_t si = 0;
        for (uint32_t ci = 0; ci < stbl->stco.entry_count && si < stbl->stsz.sample_count; ci++) {
            uint64_t chunk_off = stbl->stco.chunk_offset[ci];
            uint32_t spc = heif_stsc_spc(&stbl->stsc, ci);
            if (spc == 0) spc = stbl->stsz.sample_count - si;
            uint64_t cum = chunk_off;
            for (uint32_t k = 0; k < spc && si < stbl->stsz.sample_count; k++) {
                uint32_t sz = stbl->stsz.entry_size[si];
                fseek(f, cum, SEEK_SET);
                uint8_t *data = malloc(sz ? sz : 1);
                if (sz > 0) FFREAD_IGNORE(data, sz, 1, f);
                struct pic *p1 = HEIF_load_one(width, height, data, sz, hps, out_bits);
                free(data);
                if (!p1) {
                    VERR(heif, "track %d sample %u failed to decode; "
                         "animation requires all-intra samples", t, si);
                    si = stbl->stsz.sample_count;
                    break;
                }
                if (p1) {
                    uint64_t dt = 0;
                    if (stbl->stts.entry_count > 0) {
                        uint32_t a = 0;
                        for (uint32_t ei = 0; ei < stbl->stts.entry_count; ei++) {
                            a += stbl->stts.sample_count[ei];
                            if (si < a) { dt = stbl->stts.sample_delta[ei]; break; }
                        }
                    }
                    uint32_t dms = (uint32_t)((dt * 1000 + timescale/2) / timescale);
                    pic_set_duration(p1, dms);
                    file_enqueue_pic(p1); total++;
                }
                cum += sz; si++;
            }
        }
    }
    return total;
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
    if (has_primary_item(h)) {
        struct ispe_box *ispe = get_ispe_by_item_id(h, get_primary_item_id(h));
        if (ispe) {
            p->height = ispe->image_height;
            p->width = ispe->image_width;
            VDBG(heif, "width %d, height %d", p->width, p->height);
        }
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

    if (!skip_flag && has_primary_item(h) &&
        (p->width <= 0 || p->height <= 0)) {
        VERR(heif, "no image size for the primary item");
        HEIF_free(p);
        return NULL;
    }

    p->width = ((p->width + 3) >> 2) << 2;
    p->depth = 32;
    if (has_primary_item(h)) {
        uint32_t pid = get_primary_item_id(h);
        struct hvcC_box *conf = get_hvcc_from_item_id(h, pid);

        p->depth = conf ? heif_output_bits(conf)
                        : heif_grid_output_bits(h, pid);
    }
    p->pitch = ((p->width + 15) >> 4) * 16 * (p->depth >> 3);
    p->pixels = calloc(1, (size_t)p->pitch * p->height);
    p->format = p->depth > 32 ? CS_PIXELFORMAT_BGRA64
                              : CS_PIXELFORMAT_BGRA_BYTES;
    heif_collect_metadata(h, has_primary_item(h) ? get_primary_item_id(h) : 0,
                          f);
    int n = 0;
    if (!skip_flag) {
        n = decode_items(h, f, p);

        for (int i = 0; i < h->moov_num; i++) {
            n += decode_moov(f, h->moov + i);
        }
    }

    fclose(f);
    /* with a primary item the image is decoded into p; extra frames are
     * already queued. without one every frame was queued and p only holds
     * the container, so it must not be freed after being handed to the
     * queue. */
    if (has_primary_item(h)) {
        if (n == 1) {
            return p;
        }
        if (n > 1) {
            file_enqueue_pic(p);
            return NULL;
        }
        /* A protected primary cannot be decrypted, so the container view
         * (zeroed pixels at the ispe size) is all that can be returned. */
        if (heif_item_protected(h, get_primary_item_id(h))) {
            VWARN(heif, "no decodable primary item, using the ispe size %dx%d",
                  p->width, p->height);
            if (p->pixels) {
                memset(p->pixels, 0, (size_t)p->pitch * p->height);
            }
            return p;
        }
    }
    HEIF_free(p);
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
    /* frames decoded from a file without a primary item carry an empty
     * container, so the compatible brand list may be absent */
    int nbrands = ((int)h->ftyp.size - 12) / 4;
    if (h->ftyp.compatible_brands && nbrands >= 2) {
        s1 = UINT2TYPE(h->ftyp.compatible_brands[1]);
        fprintf(f, " compatible %s\n", s1);
    } else {
        fprintf(f, "\n");
    }
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
    fprintf(f, " item_id=%u", get_primary_item_id(h));
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
                    (uint64_t)hvcc->general_constraint_indicator_flags[2] << 24 |
                    (uint64_t)hvcc->general_constraint_indicator_flags[3] << 16 |
                    (uint64_t)hvcc->general_constraint_indicator_flags[4] << 8 |
                    (uint64_t)hvcc->general_constraint_indicator_flags[5]);

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
    if (h->exif_id || h->xmp_id || h->thumb_id ||
        h->meta.ipro.protection_count) {
        fprintf(f, "\tmetadata:\n");
        if (h->exif_id) {
            fprintf(f, "\t\texif item %u, %" PRIu64
                    " bytes, orientation %d\n", h->exif_id,
                    heif_item_size(h, h->exif_id), h->exif_orientation);
        }
        if (h->xmp_id) {
            struct infe_box *infe = heif_item_info(h, h->xmp_id);
            fprintf(f, "\t\txmp item %u, %" PRIu64 " bytes, type %s\n",
                    h->xmp_id, heif_item_size(h, h->xmp_id),
                    infe && infe->content_type ? infe->content_type : "?");
        }
        if (h->thumb_id) {
            fprintf(f, "\t\tthumbnail item %u, %" PRIu64 " bytes\n",
                    h->thumb_id, heif_item_size(h, h->thumb_id));
        }
        if (h->meta.ipro.protection_count) {
            fprintf(f, "\t\tprotection entries %d\n",
                    h->meta.ipro.protection_count);
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
                    fprintf(f, "\t\toffset: 0x%lx\n", (unsigned long)m->trak[i].mdia.minf.stbl.stco.chunk_offset[j]);
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



