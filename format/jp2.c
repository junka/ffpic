#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "basemedia.h"
#include "file.h"
#include "jp2.h"
#include "vlog.h"
#include "utils.h"

VLOG_REGISTER(jp2, DEBUG)

/* unlike FFREAD_BOX_ST in basemedia.h, a mismatched box type is reported to
 * the caller instead of aborting on a truncated or corrupt file */
#define JP2_READ_BOX_ST(b, f, fourcc) \
    do { \
        read_box(f, (b)); \
        if ((b)->type != (fourcc)) { \
            return -EINVAL; \
        } \
    } while (0)

#define JP2_READ_BOX_FULL(b, f, fourcc) \
    do { \
        read_full_box(f, (b)); \
        if ((b)->type != (fourcc)) { \
            return -EINVAL; \
        } \
    } while (0)


static int
JP2_probe(FILE *f)
{
    if (f == NULL) {
        return -ENOENT;
    }
    struct jp2_signature_box h;
    read_box(f, &h);

    if (h.type == TYPE2UINT("jP  ") || 
        h.type == TYPE2UINT("jP2 ")) {
        return 0;
    }

    return -EINVAL;
}

int
read_jp2_ihdr_box(FILE *f, struct jp2_ihdr_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('i', 'h', 'd', 'r'));
    // b->vers = read_u16(f);
    // b->num_comp = read_u16(f);
    b->height = read_u32(f);
    b->width = read_u32(f);
    b->num_comp = read_u16(f);
    b->bpc = read_u8(f);
    b->c = read_u8(f);
    b->unkc = read_u8(f);
    b->ipr = read_u8(f);
    return b->size;
}

int
read_jp2_colr_box(FILE *f, struct jp2_colr_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('c', 'o', 'l', 'r'));
    FFREAD_IGNORE(&b->method, 3, 1, f);
    if (b->method == ENUMERATED_COLORSPACE) {
        FFREAD_IGNORE(&b->enum_cs, 4, 1, f);
        b->enum_cs = SWAP(b->enum_cs);
        if (b->enum_cs == 16) {
            // sRGB
        } else if (b->enum_cs == 17) {
            // greyscale
        } else if (b->enum_cs == 18) {
            // sYCC
        } else {
            printf("invalid enum color space\n");
        }
    } else if (b->method == RESTRICT_ICC_PROFILE) {
        // printf("icc len %d\n", len - 3);
    }
    return b->size;
}

static int
read_jp2_bpcc_box(FILE *f, struct jp2_bpcc_box *b, int num)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('b', 'p', 'c', 'c'));
    /* one byte per component, and never past the end of the box */
    if (b->size < 8 || num <= 0 || (uint64_t)num > b->size - 8) {
        return b->size;
    }
    b->bits_per_comp = malloc(num);
    if (b->bits_per_comp) {
        FFREAD_IGNORE(b->bits_per_comp, 1, num, f);
    }
    return b->size;
}

int
read_jp2_cmap_box(FILE *f, struct jp2_cmap_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('c', 'm', 'a', 'p'));
    if (b->size < 12) {
        return b->size;
    }
    int n = (int)((b->size - 8) / 4);
    b->cmp = calloc(n, 2);
    b->mtyp = calloc(n, 1);
    b->pcol = calloc(n, 1);
    if (b->cmp == NULL || b->mtyp == NULL || b->pcol == NULL) {
        return b->size;
    }
    for (int i = 0; i < n; i++) {
        b->cmp[i] = read_u16(f);
        b->mtyp[i] = read_u8(f);
        b->pcol[i] = read_u8(f);
    }
    return b->size;
}

int
read_jp2_cdef_box(FILE *f, struct jp2_cdef_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('c', 'd', 'e', 'f'));
    if (b->size < 10) {
        return b->size;
    }
    /* each entry is 6 bytes, so never trust the count past the box end */
    int room = (int)((b->size - 10) / 6);
    uint16_t n_comp = read_u16(f);
    if (n_comp > room) {
        n_comp = (uint16_t)room;
    }
    b->num_comp = n_comp;
    if (b->num_comp == 0) {
        return b->size;
    }
    b->comp_id = calloc(b->num_comp, 2);
    b->comp_type = calloc(b->num_comp, 2);
    b->comp_assoc = calloc(b->num_comp, 2);
    if (b->comp_id == NULL || b->comp_type == NULL || b->comp_assoc == NULL) {
        return b->size;
    }
    for (int i = 0; i < b->num_comp; i++) {
        b->comp_id[i] = read_u16(f);
        b->comp_type[i] = read_u16(f);
        b->comp_assoc[i] = read_u16(f);
    }
    return b->size;
}

int
read_jp2_pclr_box(FILE *f, struct jp2_pclr_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('p', 'c', 'l', 'r'));
    /* NE(2) NPC(1) then one bit-depth byte per palette column */
    if (b->size < 11) {
        return b->size;
    }
    b->num_entry = read_u16(f);
    b->num_channel = read_u8(f);
    if (b->num_entry == 0 || b->num_channel == 0) {
        return b->size;
    }
    b->bit_depth = calloc(b->num_channel, 1);
    if (b->bit_depth == NULL) {
        return b->size;
    }
    FFREAD_IGNORE(b->bit_depth, 1, b->num_channel, f);

    /* every entry stores one value per column, ceil(depth / 8) bytes wide */
    int bytes_per_entry = 0;
    for (int j = 0; j < b->num_channel; j++) {
        bytes_per_entry += (b->bit_depth[j] + 7) >> 3;
    }
    uint64_t need = 11 + (uint64_t)b->num_channel +
                    (uint64_t)b->num_entry * bytes_per_entry;
    if (bytes_per_entry == 0 || need > b->size) {
        return b->size;
    }
    b->entries = calloc((size_t)b->num_entry * b->num_channel, sizeof(uint32_t));
    if (b->entries == NULL) {
        return b->size;
    }
    for (int i = 0; i < b->num_entry; i ++) {
        for (int j = 0; j < b->num_channel; j ++) {
            int n = (b->bit_depth[j] + 7) >> 3;
            uint32_t v = 0;
            for (int k = 0; k < n; k++) {
                v = (v << 8) | read_u8(f);
            }
            b->entries[(size_t)i * b->num_channel + j] = v;
        }
    }
    return b->size;
}

static int
read_jp2_resc_box(FILE *f, struct jp2_resc_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('r', 'e', 's', 'c'));
    b->vrcn = read_u16(f);
    b->vrcd = read_u16(f);
    b->hrcn = read_u16(f);
    b->hrcd = read_u16(f);
    b->vrce = read_u8(f);
    b->hrce = read_u8(f);
    return b->size;
}

static int
read_jp2_resd_box(FILE *f, struct jp2_resd_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('r', 'e', 's', 'd'));
    b->vrdn = read_u16(f);
    b->vrdd = read_u16(f);
    b->hrdn = read_u16(f);
    b->hrdd = read_u16(f);
    b->vrde = read_u8(f);
    b->hrde = read_u8(f);
    return b->size;
}

int
read_jp2_res_box(FILE *f, struct jp2_res_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('r', 'e', 's', ' '));
    int64_t s = (int64_t)b->size - 8;
    while (s > 0) {
        long before = ftell(f);
        struct box p;
        probe_box(f, &p);
        switch (p.type) {
            case RESC:
                read_jp2_resc_box(f, &b->resc);
                break;
            case RESD:
                read_jp2_resd_box(f, &b->resd);
                break;
            default:
                if (p.size < 8) {
                    return b->size;
                }
                fseek(f, (long)(p.size - 8), SEEK_CUR);
                break;
        }
        /* a corrupt or zero sized box must not keep the loop spinning */
        if (ftell(f) <= before) {
            break;
        }
        s -= (int64_t)p.size;
    }
    return b->size;
}

int 
read_jp2_url_box(FILE * f, struct jp2_url_box *b)
{
    JP2_READ_BOX_FULL(b, f, FOURCC2UINT('u', 'r', 'l', ' '));
    read_till_null(f, &b->location);
    return b->size;
}

int
read_jp2_ulst_box(FILE * f, struct jp2_ulst_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('u', 'l', 's', 't'));
    if (b->size < 10) {
        return b->size;
    }
    b->num_uuid = read_u16(f);
    if ((uint64_t)b->num_uuid * sizeof(uint128_t) > b->size - 10) {
        b->num_uuid = (uint16_t)((b->size - 10) / sizeof(uint128_t));
    }
    if (b->num_uuid == 0) {
        return b->size;
    }
    b->id = malloc(b->num_uuid * sizeof(uint128_t));
    if (b->id == NULL) {
        b->num_uuid = 0;
        return b->size;
    }
    for (int i = 0; i < b->num_uuid; i ++) {
        FFREAD_IGNORE(b->id + i, sizeof(uint128_t), 1, f);
    }
    return b->size;
}

int
read_jp2_uinf_box(FILE * f, struct jp2_uinf_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('u', 'i', 'n', 'f'));
    // read ulst
    read_jp2_ulst_box(f, &b->ulst);
    // read url
    read_jp2_url_box(f, &b->url);
    return b->size;
}

//may ignore this part
int
read_jp2_xml_box(FILE *f, struct jp2_xml_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('x', 'm', 'l', ' '));
    if (b->size <= 8) {
        return b->size;
    }
    b->data = malloc(b->size - 8);
    if (b->data == NULL) {
        return b->size;
    }
    FFREAD_IGNORE(b->data, b->size - 8, 1, f);
    return b->size;
}

int
read_jp2_uuid_box(FILE *f, struct jp2_uuid_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('u', 'u', 'i', 'd'));
    if (b->size < 24) {
        return b->size;
    }
    FFREAD(&b->id, 16, 1, f);
    b->data = malloc(b->size - 24);
    if (b->data == NULL) {
        return b->size;
    }
    FFREAD_IGNORE(b->data, b->size - 24, 1, f);
    return b->size;
}


uint16_t 
read_marker(FILE *f)
{
    int c = fgetc(f);
    if (c != 0xFF) {
        if (c != EOF) {
            printf("marker 0x%x\n", c);
        }
        return 0;
    }
    c = fgetc(f);
    if (c == EOF) {
        return 0;
    }
    return MARK((uint8_t)c);
}

int
read_siz(JP2* j, FILE *f)
{
    int l = 0;
    struct siz *s = &j->main_h.siz;
    FFREAD_IGNORE(s, 38, 1, f);
    s->length = SWAP(s->length);
    s->cap = SWAP(s->cap);
    s->width = SWAP(s->width);
    s->height = SWAP(s->height);
    s->left = SWAP(s->left);
    s->top = SWAP(s->top);
    s->tile_width = SWAP(s->tile_width);
    s->tile_height = SWAP(s->tile_height);
    s->tile_left = SWAP(s->tile_left);
    s->tile_top = SWAP(s->tile_top);
    s->component_num = SWAP(s->component_num);

    l += 38;
    if (s->component_num == 0 || s->component_num > 16384) {
        return -EINVAL;
    }
    s->comps = calloc(s->component_num, sizeof(struct scomponent));
    if (s->comps == NULL) {
        return -ENOMEM;
    }
    FFREAD_IGNORE(s->comps, sizeof(struct scomponent), s->component_num, f);
    l += sizeof(struct scomponent)*s->component_num;
    return l;
}

int
read_cod(JP2 *j, FILE *f)
{
    int l = 0;
    FFREAD_IGNORE(&j->main_h.cod, 7, 1, f);
    j->main_h.cod.length = SWAP(j->main_h.cod.length);
    j->main_h.cod.layers_num = SWAP(j->main_h.cod.layers_num);
    l += 7;
    FFREAD_IGNORE(&j->main_h.cod.p, 5, 1, f);
    l += 5;

    if (j->main_h.cod.entropy) {
        j->main_h.cod.p.precinct_size = malloc(j->main_h.cod.p.decomp_level_num+1);
        FFREAD_IGNORE(j->main_h.cod.p.precinct_size, j->main_h.cod.p.decomp_level_num+1, 1, f);
        l += (j->main_h.cod.p.decomp_level_num + 1);
    }
    return l;
}

int
read_cap(JP2 *j, FILE *f)
{
    int l = 0;
    FFREAD_IGNORE(&j->main_h.cap, 6, 1, f);
    l += 6;
    j->main_h.cap.length = SWAP(j->main_h.cap.length);
    j->main_h.cap.bitmap = SWAP(j->main_h.cap.bitmap);
    int ext_len = (int)j->main_h.cap.length - 6;
    if (ext_len > 0) {
        j->main_h.cap.extensions = calloc(ext_len, 1);
        if (j->main_h.cap.extensions == NULL) {
            return -ENOMEM;
        }
        FFREAD_IGNORE(j->main_h.cap.extensions, ext_len, 1, f);
        l += ext_len;
    }
    return l;
}

int
read_cme(JP2 *j, FILE *f)
{
    int l = 0;
    FFREAD_IGNORE(&j->main_h.cme, 4, 1, f);
    j->main_h.cme.length = SWAP(j->main_h.cme.length);
    j->main_h.cme.use = SWAP(j->main_h.cme.use);
    if (j->main_h.cme.length > 4) {
        int str_len = (int)j->main_h.cme.length - 4;
        j->main_h.cme.str = calloc(str_len + 1, 1);
        if (j->main_h.cme.str == NULL) {
            return -ENOMEM;
        }
        FFREAD_IGNORE(j->main_h.cme.str, str_len, 1, f);
    }
    l += j->main_h.cme.length;
    return l;
}

int
read_qcd(JP2 *j, FILE *f)
{
    struct qcd *q = &j->main_h.qcd;
    int l = 0;
    int table_len;

    FFREAD_IGNORE(q, 3, 1, f);
    l += 3;
    q->length = SWAP(q->length);
    VDBG(jp2, "quant type %d, guard bits %d, len %d, decomp %d\n",
           q->quant_type, q->guard_num, q->length,
           j->main_h.cod.p.decomp_level_num);
    if (q->quant_type == 0) {
        //no quantization, one byte per subband
        table_len = 3*j->main_h.cod.p.decomp_level_num + 1;
    } else if (q->quant_type == 1) {
        //scalar derived, one exponent/mantissa pair shared by all subbands
        table_len = 2;
    } else if (q->quant_type == 2) {
        //scalar expounded, one exponent/mantissa pair per subband
        table_len = 6*j->main_h.cod.p.decomp_level_num + 2;
    } else {
        printf("invalid quant type %d\n", q->quant_type);
        return l;
    }
    //the marker length has to win over the value derived from the header
    if (q->length < 3) {
        return l;
    }
    if (table_len > q->length - 3) {
        table_len = q->length - 3;
    }
    if (table_len <= 0) {
        return l;
    }
    q->table = calloc(table_len, 1);
    if (q->table == NULL) {
        return -ENOMEM;
    }
    FFREAD_IGNORE(q->table, table_len, 1, f);
    return l + table_len;
}


int
read_sot(JP2 *j, FILE *f)
{
    int l = 0;
    struct tile_header * t;

    if (j->tile_nums == 0) {
        j->tiles = calloc(1, sizeof(struct tile_header));
        if (j->tiles == NULL) {
            return -ENOMEM;
        }
        t = j->tiles;
    } else {
        struct tile_header *n = realloc(j->tiles,
                sizeof(struct tile_header) * (1 + j->tile_nums));
        if (n == NULL) {
            return -ENOMEM;
        }
        j->tiles = n;
        t = &j->tiles[j->tile_nums];
        memset(t, 0, sizeof(*t));
    }
    j->tile_nums ++;

    /* record tile starting point to calc len in sod */
    t->sot.offset_start = ftell(f) - 2;
    FFREAD_IGNORE(&t->sot, 10, 1, f);
    l += 10;
    t->sot.length = SWAP(t->sot.length);
    t->sot.tile_id = SWAP(t->sot.tile_id);
    t->sot.tile_size = SWAP(t->sot.tile_size);
    return l;
}

//last marker in a tile-part header, end with next SOT or EOC
//the coded image data is not needed to report the metadata, the tile-part
//length carried by SOT is enough to jump to the next marker segment
int
read_sod(JP2 *j, FILE *f) {
    int start = ftell(f);
    struct tile_header *t = &j->tiles[j->tile_nums - 1];
    long len = (long)t->sot.tile_size - (start - (long)t->sot.offset_start);
    if (len < 0) {
        len = 0;
    }
    VDBG(jp2, "tile id %d, tile data len %ld", t->sot.tile_id, len);
    if (len > 0 && fseek(f, len, SEEK_CUR) != 0) {
        return -EINVAL;
    }
    return ftell(f) - start;
}

// see A.6.6
int read_poc(JP2 *j, FILE *f)
{
    FFREAD_IGNORE(&j->main_h.poc, 3, 1, f);
    j->main_h.poc.length = SWAP(j->main_h.poc.length);
    if (j->main_h.siz.component_num < 257) {
        j->main_h.poc.comp_start_index = fgetc(f);
    } else {
        j->main_h.poc.comp_start_index = read_u16(f);
    }
    j->main_h.poc.layer_end_index = read_u16(f);
    j->main_h.poc.res_end_index = fgetc(f);
    if (j->main_h.siz.component_num < 257) {
        j->main_h.poc.comp_end_index = fgetc(f);
    } else {
        j->main_h.poc.comp_end_index = read_u16(f);
    }
    j->main_h.poc.progression_order = fgetc(f);
    return 0;
}

#ifndef NDEBUG
static const char *
jp2_marker_name(uint16_t mark)
{
    int id = (mark >> 8) & 0xFF;
    switch(id) {
        case 0x4F:
            return "SOC";
        case 0x50:
            return "CAP";
        case 0x51:
            return "SIZ";
        case 0x52:
            return "COD";
        case 0x53:
            return "COC";
        case 0x55:
            return "TLM";
        case 0x56:
            return "PRF";
        case 0x57:
            return "PLM";
        case 0x58:
            return "PLT";
        case 0x5C:
            return "QCD";
        case 0x5D:
            return "QCC";
        case 0x5E:
            return "RGN";
        case 0x5F:
            return "POC";
        case 0x60:
            return "PPM";
        case 0x61:
            return "PPT";
        case 0x63:
            return "CRG";
        case 0x64:
            return "CME";
        case 0x90:
            return "SOT";
        case 0x91:
            return "SOP";
        case 0x92:
            return "EPH";
        case 0x93:
            return "SOD";
        case 0xD9:
            return "EOC";
        default:
            return "unkown";
    }
}
#endif

void 
read_stream(JP2 *j, FILE *f)
{
    struct box b;
    read_box(f, &b);
    uint16_t mark;
    mark = read_marker(f);
    if (mark != SOC) {
        printf("not a valid start of codestream\n");
        return;
    }
    while(1) {
        mark = read_marker(f);
        if (mark == 0) {
            /* eof or garbage between marker segments: stop scanning */
            return;
        }
        VDBG(jp2, "marker %x, name %s", SWAP(mark), jp2_marker_name(mark));
        switch (mark) {
            case SOD:
                if (read_sod(j, f) < 0) {
                    return;
                }
                break;
            case SIZ:
                if (read_siz(j, f) < 0) {
                    return;
                }
                break;
            case CAP:
                if (read_cap(j, f) < 0) {
                    return;
                }
                break;
            case COD:
                read_cod(j, f);
                break;
            case QCD:
                read_qcd(j, f);
                break;
            case CME:
                if (read_cme(j, f) < 0) {
                    return;
                }
                break;
            case SOT:
                if (read_sot(j, f) < 0) {
                    return;
                }
                break;
            case POC:
                read_poc(j, f);
                break;
            // case TLM:
            //     read_tlm(j, f);
            //     break;
            case EOC:
                //may be absent in case corrupt
                VERR(jp2, "end of code stream  %ld", ftell(f));
                return;
            default:
                /* an unhandled marker segment, the rest of the codestream
                 * cannot be parsed reliably: stop here instead of looping */
                printf("marker %x\n", SWAP(mark));
                return;
        }
    }
}

int read_jp2h_box(FILE *f, struct jp2h_box *b)
{
    JP2_READ_BOX_ST(b, f, FOURCC2UINT('j', 'p', '2', 'h'));
    read_jp2_ihdr_box(f, &b->ihdr);
    int64_t size = (int64_t)b->size - (int64_t)b->ihdr.size - 8;
    while(size > 0) {
        long before = ftell(f);
        struct box p;
        uint32_t type = probe_box(f, &p);
        VDBG(jp2, "JP2H %s", type2name(type));
        switch(type) {
        // case IHDR:
        //     read_jp2_ihdr_box(f, &b->ihdr);
        //     break;
        case BPCC:
            read_jp2_bpcc_box(f, &b->bpcc, b->ihdr.num_comp);
            break;
        case COLR:
            {
                struct jp2_colr_box *n;
                if (b->n_colr == 0) {
                    n = calloc(1, sizeof(struct jp2_colr_box));
                } else {
                    n = realloc(b->colr, sizeof(struct jp2_colr_box) * (b->n_colr + 1));
                    if (n) {
                        memset(n + b->n_colr, 0, sizeof(struct jp2_colr_box));
                    }
                }
                if (n == NULL) {
                    return b->size;
                }
                b->colr = n;
                read_jp2_colr_box(f, b->colr + b->n_colr);
                b->n_colr ++;
            }
            break;
        case PCLR:
            read_jp2_pclr_box(f, &b->pclr);
            break;
        case RES:
            read_jp2_res_box(f, &b->res);
            break;
        case CDEF:
            read_jp2_cdef_box(f, &b->cdef);
            break;
        case CMAP:
            read_jp2_cmap_box(f, &b->cmap);
            break;
        default:
            /* unknown sub box: skip its payload, and stop if the header
             * is corrupt so the loop cannot spin forever */
            if (p.size < 8) {
                return b->size;
            }
            fseek(f, (long)(p.size - 8), SEEK_CUR);
            break;
        }
        if (ftell(f) <= before) {
            break;
        }
        size -= (int64_t)p.size;
    }
    return b->size;
}

static struct pic* 
JP2_load(const char *filename, int skip_flag UNUSED)
{
    struct pic *p = pic_alloc(sizeof(JP2));
    JP2 *j = p->pic;
    struct jp2_signature_box h;
    FILE * f = fopen(filename, "rb");
    if (f == NULL) {
        pic_free(p);
        return NULL;
    }
    fseek(f, 0 , SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    //first box, JP signature box
    read_box(f, &h);
    h.magic = read_u32(f);
    if (h.magic != 0x0D0A870A) {
        fclose(f);
        pic_free(p);
        return NULL;
    }

    // second box, ftyp box
    read_ftyp(f, (void *)&(j->ftyp));

    // common process for other boxes
    while(ftell(f) < size) {
        long before = ftell(f);
        struct box b;
        uint32_t type = probe_box(f, &b);
        VDBG(jp2, "box type %s", type2name(type));
        switch (type) {
        case JP2H:
            read_jp2h_box(f, &j->jp2h);
            break;
        case JP2C:
            read_stream(j, f);
            break;
        case UINF:
            {
                struct jp2_uinf_box *n;
                if (j->uinf_num == 0) {
                    n = calloc(1, sizeof(struct jp2_uinf_box));
                } else {
                    n = realloc(j->uinf, sizeof(struct jp2_uinf_box) * (j->uinf_num + 1));
                    if (n) {
                        memset(n + j->uinf_num, 0, sizeof(struct jp2_uinf_box));
                    }
                }
                if (n == NULL) {
                    break;
                }
                j->uinf = n;
                read_jp2_uinf_box(f, j->uinf + j->uinf_num);
                j->uinf_num++;
            }
            break;
        case XML:
            {
                struct jp2_xml_box *n;
                if (j->xml_num == 0) {
                    n = calloc(1, sizeof(struct jp2_xml_box));
                } else {
                    n = realloc(j->xml, sizeof(struct jp2_xml_box) * (j->xml_num + 1));
                    if (n) {
                        memset(n + j->xml_num, 0, sizeof(struct jp2_xml_box));
                    }
                }
                if (n == NULL) {
                    break;
                }
                j->xml = n;
                read_jp2_xml_box(f, j->xml + j->xml_num);
                j->xml_num ++;
            }
            break;
        case UUID:
            {
                struct jp2_uuid_box *n;
                if (j->uuid_num == 0) {
                    n = calloc(1, sizeof(struct jp2_uuid_box));
                } else {
                    n = realloc(j->uuid, sizeof(struct jp2_uuid_box) * (j->uuid_num + 1));
                    if (n) {
                        memset(n + j->uuid_num, 0, sizeof(struct jp2_uuid_box));
                    }
                }
                if (n == NULL) {
                    break;
                }
                j->uuid = n;
                read_jp2_uuid_box(f, j->uuid + j->uuid_num);
                j->uuid_num ++;
            }
            break;
        default:
            printf("unkown marker \"%s\"\n", UINT2TYPE(type));
            if (b.size < 8) {
                break;
            }
            fseek(f, b.size - 8, SEEK_CUR);
            break;
        }
        /* corrupt or zero sized box: stop instead of looping on the spot */
        if (ftell(f) <= before) {
            break;
        }
    }
    fclose(f);

    return p;
}

static void 
JP2_free(struct pic *p)
{
    JP2 *j = (JP2 *) p->pic;
    if(j->jp2h.ihdr.num_comp > 0) {
        
    }
    for (int i = 0; i < j->jp2h.n_colr; i++) {
        free(j->jp2h.colr[i].iccpro.iccp);
    }
    free(j->jp2h.colr);
    free(j->jp2h.bpcc.bits_per_comp);
    free(j->jp2h.pclr.bit_depth);
    free(j->jp2h.pclr.entries);
    free(j->jp2h.cmap.cmp);
    free(j->jp2h.cmap.mtyp);
    free(j->jp2h.cmap.pcol);
    free(j->jp2h.cdef.comp_id);
    free(j->jp2h.cdef.comp_type);
    free(j->jp2h.cdef.comp_assoc);

    for (int i = 0; i <j->uinf_num; i ++) {
        if(j->uinf[i].ulst.num_uuid) {
            free(j->uinf[i].ulst.id);
        }
        if (j->uinf[i].url.location) {
            free(j->uinf[i].url.location);
        }
    }
    if (j->uinf)
        free(j->uinf);
    for (int i = 0; i< j->xml_num; i++) {
        if (j->xml[i].data)
            free(j->xml[i].data);
    }
    if (j->xml)
        free(j->xml);
    for (int i = 0; i < j->uuid_num; i++) {
        if (j->uuid[i].data)
            free(j->uuid[i].data);
    }
    if (j->uuid)
        free(j->uuid);
    if (j->tiles)
        free(j->tiles);
    if (j->main_h.siz.comps) {
        free(j->main_h.siz.comps);
    }
    if (j->main_h.cod.p.precinct_size) {
        free(j->main_h.cod.p.precinct_size);
    }
    if (j->main_h.qcd.table) {
        free(j->main_h.qcd.table);
    }
    if (j->main_h.cap.extensions) {
        free(j->main_h.cap.extensions);
    }
    if (j->main_h.cme.str) {
        free(j->main_h.cme.str);
    }
    pic_free(p);
}

static void
JP2_info(FILE *f, struct pic* p)
{
    JP2 *j = (JP2 *) p->pic;
    fprintf(f, "JPEG2000 file formart\n");
    fprintf(f, "---------------------------\n");
    fprintf(f, "\tIHDR height %d, width %d, num_comp %d, bpc %d, compression %d\n",
                j->jp2h.ihdr.height, j->jp2h.ihdr.width, j->jp2h.ihdr.num_comp, j->jp2h.ihdr.bpc+1, j->jp2h.ihdr.c);
    if (j->jp2h.bpcc.size == 0) {
        fprintf(f, "\tbitsperpixel %d\n", j->jp2h.ihdr.bpc);
    } else {
        for (int i = 0; i < j->jp2h.ihdr.num_comp; i++) {
            fprintf(f, "\tbitsperpixel %d\n", j->jp2h.bpcc.bits_per_comp[i] & 0x7F);
        }
    }
    for (int i = 0; i < j->jp2h.n_colr; i++) {
        struct jp2_colr_box *colr = &j->jp2h.colr[i];
        fprintf(f, "\tCOLR method %d, precedence %d, approximation %d, colorspace %d\n",
                colr->method, colr->precedence, colr->approx, colr->enum_cs);
        if (colr->method == RESTRICT_ICC_PROFILE) {
            //fprintf profile
        }
    }
    for (int i = 0; i < j->uinf_num; i ++) {
        fprintf(f, "\tulst num_uuid %d:\n", j->uinf[i].ulst.num_uuid);
        for (int k = 0; k < j->uinf[i].ulst.num_uuid; k++) {
            fprintf(f, "\t\t%016" PRIx64 "%016" PRIx64 "\n",
                    j->uinf[i].ulst.id[k].v[0],
                    j->uinf[i].ulst.id[k].v[1]);
        }
        fprintf(f, "\turl %s\n", j->uinf[i].url.location ? j->uinf[i].url.location : "");
    }
    // for (int i = 0; i < j->xml_num; i++) {
    //     fprintf(f, "\t xml %d: %s\n", i, j->xml[i].data);
    // }
    fprintf(f, "---------------------------\n");
    fprintf(f, "\tSIZ len %d, cap %d, left %d, top %d\n", j->main_h.siz.length,
            j->main_h.siz.cap, j->main_h.siz.left, j->main_h.siz.top);
    fprintf(f, "\tSIZ tile_height %d, tile_width %d, tile_left %d, tile_top %d\n", j->main_h.siz.tile_height,
            j->main_h.siz.tile_width, j->main_h.siz.tile_left, j->main_h.siz.tile_top);
    fprintf(f, "\tSIZ height %d, width %d, num_comp %d\n", j->main_h.siz.height,
                j->main_h.siz.width, j->main_h.siz.component_num);
    for (int i = 0; i < j->main_h.siz.component_num; i++) {
        if (j->main_h.siz.comps == NULL) {
            break;
        }
        fprintf(f, "\tdepth %d, horizon %d, vertical %d\n", j->main_h.siz.comps[i].depth+1,
                j->main_h.siz.comps[i].horizontal_separation,
                j->main_h.siz.comps[i].vertical_separation);
    }
    fprintf(f,
            "\tCOD len %d, entropy %d, sop %d, eph %d, progression_order %d, layers %d\n",
            j->main_h.cod.length, j->main_h.cod.entropy, j->main_h.cod.sop,
            j->main_h.cod.eph, j->main_h.cod.progression_order, j->main_h.cod.layers_num);
    fprintf(f,
            "\tCOD multiple_transform %d, decomp_level_num %d, "
            "code_block_width %d, code_block_height %d, wavelet transform %d\n",
            j->main_h.cod.multiple_transform, j->main_h.cod.p.decomp_level_num,
            j->main_h.cod.p.code_block_width + 2,
            j->main_h.cod.p.code_block_height + 2, j->main_h.cod.p.transform);
    fprintf(f,
            "\tCOD selctive_arithmetic %d, reset_on_boundary %d, termination "
            "%d, vertical_context %d, predictable_termination %d, "
            "segmentation_symbol %d\n",
            j->main_h.cod.p.selctive_arithmetic,
            j->main_h.cod.p.reset_on_boundary, j->main_h.cod.p.termination,
            j->main_h.cod.p.vertical_context,
            j->main_h.cod.p.predictable_termination, j->main_h.cod.p.segmentation_symbol);
    if (j->main_h.cod.entropy && j->main_h.cod.p.precinct_size) {
        for (int i = 0; i < j->main_h.cod.p.decomp_level_num+1; i ++) {
            fprintf(f, "\tCOD precinct witdh %d, height %d\n",
                    j->main_h.cod.p.precinct_size[i] & 0xF,
                    (j->main_h.cod.p.precinct_size[i]>>4) & 0xF);
        }
    }
    fprintf(f, "\tQCD len %d, guard_num %d quant_type %d\n", j->main_h.qcd.length,
            j->main_h.qcd.guard_num, j->main_h.qcd.quant_type);
    int table_len = 0;
    if (j->main_h.qcd.quant_type == 0) {
        table_len = 3 * j->main_h.cod.p.decomp_level_num + 1;
    } else if (j->main_h.qcd.quant_type == 1) {
        table_len = 2;
    } else if (j->main_h.qcd.quant_type == 2) {
        table_len = 6 * j->main_h.cod.p.decomp_level_num + 2;
    }
    if (j->main_h.qcd.length >= 3 && table_len > j->main_h.qcd.length - 3) {
        table_len = j->main_h.qcd.length - 3;
    }
    fprintf(f, "\t");
    if (j->main_h.qcd.table) {
        for (int i = 0; i < table_len; i++) {
            fprintf(f, " %x", j->main_h.qcd.table[i]);
        }
    }
    fprintf(f, "\n");
    fprintf(f, "\tCME len %d, use %d, %s\n", j->main_h.cme.length,
            j->main_h.cme.use,
            j->main_h.cme.str ? (const char *)j->main_h.cme.str : "");
    fprintf(f, "\ttile num %d\n", j->tile_nums);
    for (int i = 0; i < j->tile_nums; i ++) {
        fprintf(f, "\tSOT tile id %d, size %d, part_index %d, part_nums %d\n",
                j->tiles[i].sot.tile_id, j->tiles[i].sot.tile_size,
                j->tiles[i].sot.tile_part_index,
                j->tiles[i].sot.tile_part_nums);
    }
}

static struct file_ops jp2_ops = {
    .name = "JP2",
    .probe = JP2_probe,
    .load = JP2_load,
    .free = JP2_free,
    .info = JP2_info,
};

void 
JP2_init(void)
{
    file_ops_register(&jp2_ops);
}
