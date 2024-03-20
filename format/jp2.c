#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "basemedia.h"
#include "bitstream.h"
#include "file.h"
#include "jp2.h"
#include "vlog.h"
#include "utils.h"

VLOG_REGISTER(jp2, DEBUG)


static int
JP2_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct jp2_signature_box h;
    read_box(f, &h);

    fclose(f);
    if (h.type == TYPE2UINT("jP  ") || 
        h.type == TYPE2UINT("jP2 ")) {
        return 0;
    }

    return -EINVAL;
}

int
read_jp2_ihdr_box(FILE *f, struct jp2_ihdr_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('i', 'h', 'd', 'r'));
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('c', 'o', 'l', 'r'));
    fread(&b->method, 3, 1, f);
    if (b->method == ENUMERATED_COLORSPACE) {
        fread(&b->enum_cs, 4, 1, f);
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('b', 'p', 'c', 'c'));
    b->bits_per_comp = malloc(num);
    fread(b->bits_per_comp, 1, num, f);
    return b->size;
}

int
read_jp2_cmap_box(FILE *f, struct jp2_cmap_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('c', 'm', 'a', 'p'));
    int n = (b->size - 8) / 4;
    b->cmp = calloc(n, 2);
    b->mtyp = calloc(n, 1);
    b->pcol = calloc(n, 1);
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('c', 'd', 'e', 'f'));
    b->num_comp = read_u16(f);
    b->comp_id = calloc(b->num_comp, 2);
    b->comp_type = calloc(b->num_comp, 2);
    b->comp_assoc = calloc(b->num_comp, 2);
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('p', 'c', 'l', 'r'));
    fread(&b->num_entry, 5, 1, f);
    b->num_entry = SWAP(b->num_entry);
    b->num_channel = fgetc(f);
    b->palette_input = SWAP(b->palette_input);
    b->component = calloc(b->num_channel, 2);
    b->depth = malloc(b->num_channel);
    fread(b->component, 2, b->num_channel, f);
    fread(b->depth, 1, b->num_channel, f);
    for (int i = 0; i < b->num_entry; i ++) {
        for (int j = 0; j < b->num_channel; j ++) {
            int n = (b->entries[j] + 8) >> 3;
            fread(b->entries + i * b->num_entry + j, n, 1, f);
        }
    }
    return b->size;
}

static int
read_jp2_resc_box(FILE *f, struct jp2_resc_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('r', 'e', 's', 'c'));
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('r', 'e', 's', 'd'));
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('r', 'e', 's', ' '));
    int64_t s = b->size - 8;
    while (s) {
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
                break;
        }
        s -= p.size;
    }
    return b->size;
}

int 
read_jp2_url_box(FILE * f, struct jp2_url_box *b)
{
    FFREAD_BOX_FULL(b, f, FOURCC2UINT('u', 'r', 'l', ' '));
    read_till_null(f, &b->location);
    return b->size;
}

int
read_jp2_ulst_box(FILE * f, struct jp2_ulst_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('u', 'l', 's', 't'));
    b->num_uuid = read_u16(f);
    b->id = malloc(b->num_uuid * sizeof(uint128_t));
    for (int i = 0; i < b->num_uuid; i ++) {
        fread(b->id + i, sizeof(uint128_t), 1, f);
    }
    return b->size;
}

int
read_jp2_uinf_box(FILE * f, struct jp2_uinf_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('u', 'i', 'n', 'f'));
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
    FFREAD_BOX_ST(b, f, FOURCC2UINT('x', 'm', 'l', ' '));
    b->data = malloc(b->size - 8);
    FFREAD(b->data, 1, b->size - 8, f);
    return b->size;
}

int
read_jp2_uuid_box(FILE *f, struct jp2_uuid_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('u', 'u', 'i', 'd'));
    FFREAD(&b->id, 16, 1, f);
    b->data = malloc(b->size - 8 - 16);
    FFREAD(b->data, 1, b->size - 24, f);
    return b->size;
}


uint16_t 
read_marker(FILE *f)
{
    uint8_t c = fgetc(f);
    if (c != 0xFF) {
        printf("marker 0x%x\n", c);
        return 0;
    }
    if (c == 0xFF && !feof(f)) {
        c = fgetc(f);
    }
    return MARK(c);
}

int
read_siz(JP2* j, FILE *f)
{
    int l = 0;
    struct siz *s = &j->main_h.siz;
    fread(s, 38, 1, f);
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
    s->comps = malloc(sizeof(struct scomponent)*s->component_num);
    fread(s->comps, sizeof(struct scomponent), s->component_num, f);
    l += sizeof(struct scomponent)*s->component_num;

    int width = ((s->width + 7) >> 3) << 3;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
    j->data = malloc(pitch * s->height);
    return l;
}

int
read_cod(JP2 *j, FILE *f)
{
    int l = 0;
    fread(&j->main_h.cod, 7, 1, f);
    j->main_h.cod.length = SWAP(j->main_h.cod.length);
    j->main_h.cod.layers_num = SWAP(j->main_h.cod.layers_num);
    l += 7;
    fread(&j->main_h.cod.p, 5, 1, f);
    l += 5;

    if (j->main_h.cod.entropy) {
        j->main_h.cod.p.precinct_size = malloc(j->main_h.cod.p.decomp_level_num+1);
        fread(j->main_h.cod.p.precinct_size, j->main_h.cod.p.decomp_level_num+1, 1, f);
        l += (j->main_h.cod.p.decomp_level_num + 1);
    }
    return l;
}

int
read_cap(JP2 *j, FILE *f)
{
    int l = 0;
    fread(&j->main_h.cap, 6, 1, f);
    l += 6;
    j->main_h.cap.length = SWAP(j->main_h.cap.length);
    j->main_h.cap.bitmap = SWAP(j->main_h.cap.bitmap);
    j->main_h.cap.extensions = malloc(j->main_h.cap.length - 6);
    fread(j->main_h.cap.extensions, 2, j->main_h.cap.length - 6/2, f);
    l += j->main_h.cap.length - 6;
    return l;
}

int
read_cme(JP2 *j, FILE *f)
{
    int l = 0;
    fread(&j->main_h.cme, 4, 1, f);
    j->main_h.cme.length = SWAP(j->main_h.cme.length);
    j->main_h.cme.use = SWAP(j->main_h.cme.use);
    if (j->main_h.cme.length - 4) {
        j->main_h.cme.str = malloc(j->main_h.cme.length - 4 + 1);
        fread(j->main_h.cme.str, j->main_h.cme.length - 4, 1, f);
        j->main_h.cme.str[j->main_h.cme.length - 4] = '\0';
    }
    l += j->main_h.cme.length;
    return l;
}

int
read_qcd(JP2 *j, FILE *f)
{
    int l = 0;
    fread(&j->main_h.qcd, 3, 1, f);
    l += 3;
    j->main_h.qcd.length = SWAP(j->main_h.qcd.length);
    VDBG(jp2, "guard num %d table len %d, decomp %d\n", j->main_h.qcd.guard_num,
           j->main_h.qcd.length - 3, j->main_h.cod.p.decomp_level_num);
    if (j->main_h.qcd.guard_num == 0) {
        j->main_h.qcd.table = malloc(3*j->main_h.cod.p.decomp_level_num + 1);
        fread(j->main_h.qcd.table, 3*j->main_h.cod.p.decomp_level_num + 1, 1, f);
        l += 3*j->main_h.cod.p.decomp_level_num + 1;
    } else if (j->main_h.qcd.guard_num == 1) {  //scalar
        j->main_h.qcd.table = malloc(2);
        fread(j->main_h.qcd.table, 2, 1, f);
        l += 2;
    } else if (j->main_h.qcd.guard_num == 2) {
        j->main_h.qcd.table = malloc(6*j->main_h.cod.p.decomp_level_num + 2);
        fread(j->main_h.qcd.table, 6*j->main_h.cod.p.decomp_level_num + 2, 1, f);
        l += 6 * j->main_h.cod.p.decomp_level_num + 2;
    } else {
        printf("invalid guard num");
    }
    return l;
}


int
read_sot(JP2 *j, FILE *f)
{
    int l = 0;
    struct tile_header * t;

    if (j->tile_nums == 0) {
        j->tiles = malloc(sizeof(struct tile_header));
        t = j->tiles;
    } else {
        j->tiles = realloc(j->tiles, sizeof(struct tile_header) * (1 + j->tile_nums));
        t = &j->tiles[j->tile_nums];
    }
    j->tile_nums ++;

    /* record tile starting point to calc len in sod */
    t->sot.offset_start = ftell(f) - 2;
    fread(&t->sot, 10, 1, f);
    l += 10;
    t->sot.length = SWAP(t->sot.length);
    t->sot.tile_id = SWAP(t->sot.tile_id);
    t->sot.tile_size = SWAP(t->sot.tile_size);
    return l;
}

struct tag_tree {
    struct tag_tree *parent;
    int value;
    int low;
};

// 2-D target
int tag_tree_decode(struct bits_vec *v, uint32_t leafno, int thresh)
{
    struct tag_tree* tree[32] = {NULL};
    struct tag_tree *node = tree[leafno];
    struct tag_tree **pos = tree;
    while(node->parent) {
        *pos++ = node;
        node = node->parent;
    }
    int low = 0;
    while (true) {
        if (node->low < low) {
            node->low = low;
        } else {
            low = node->low;
        }
        while (low < thresh && low < node->value) {
            if (READ_BIT(v)) {
                node->value = low;
                break;
            }
            low ++;
        }
        node->low = low;
        if (pos == tree) {
            break;
        }
        node = *--pos;
    }
    return node->value;
}

//see B.10 packet header information coding
void read_packet_header(struct bits_vec *v, int tile_id UNUSED) {
    // start process packet header: B.10
    //  - zero length packet;
    //  - code-block inclusion;
    //  - zero bit-plane information
    //  - number of coding passes
    //  - length of the code-block compressed image data from a given code-block
    // see B.10.3, first bit denotes whether the packet has a length of zero
    // int layers_num = j->main_h.cod.layers_num;
    int zl = READ_BIT(v);
    int inclusion UNUSED;
    // int bitplane;
    // int pass = 0;
    // VDBG(jp2, "layers_num %d zero length packet %d", layers_num, zl);
    if (zl) {
        inclusion = tag_tree_decode(v, 0, 0);
    } else {
        inclusion = READ_BIT(v);
    }
    VDBG(jp2, "zl %d, inclusion %d", zl, inclusion);
}

//last marker in a tile-part header, end with next SOT or EOC
//a tile has sevaral bands, a bands contains
int
read_sod(JP2 *j, FILE *f) {
    /* read till eoc */
    struct sop sop;
    // uint8_t *p = j->data;
    int start = ftell(f);
    int tile_id = j->tiles[j->tile_nums - 1].sot.tile_id;
    int len = j->tiles[j->tile_nums-1].sot.tile_size -
        (start - j->tiles[j->tile_nums-1].sot.offset_start);
    uint8_t *data = malloc(len);
    fread(data, len, 1, f);
    struct bits_vec *v =
        bits_vec_alloc(data, len, BITS_MSB);
    VDBG(jp2, "tile id %d, total tile len %d, at %p", tile_id, len, (void *)data);
    if (j->main_h.cod.sop) {
        //if SOP enabled, start with a SOP
        assert((data[1]<<8|data[0]) == SOP);
        data += 2;
        SKIP_BITS(v, 16);
        sop.length = (data[0] << 8 | data[1]);
        data += 2;
        assert(sop.length == 4);
        sop.seq_num = (data[0] << 8 | data[1]);
        data += 2;
    }
    // if we have ppm in main header or ppt in tile header
    if (j->main_h.ppm.index) {

    }
    read_packet_header(v, tile_id);
    if (j->main_h.cod.eph) {
        // if EPH is enabled
    }

    int l = ftell(f) - start;
    VDBG(jp2, "a tile %d", l);
    bits_vec_free(v);
    return l;
}

// see A.6.6
int read_poc(JP2 *j, FILE *f)
{
    fread(&j->main_h.poc, 3, 1, f);
    j->main_h.poc.length = SWAP(j->main_h.poc.length);
    if (j->main_h.siz.component_num < 257) {
        j->main_h.poc.comp_start_index = fgetc(f);
    } else {
        j->main_h.poc.comp_start_index = fgetc(f) << 8 | fgetc(f);
    }
    j->main_h.poc.layer_end_index = fgetc(f) << 8 | fgetc(f);
    j->main_h.poc.res_end_index = fgetc(f);
    if (j->main_h.siz.component_num < 257) {
        j->main_h.poc.comp_end_index = fgetc(f);
    } else {
        j->main_h.poc.comp_end_index = fgetc(f) << 8 | fgetc(f);
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
        VDBG(jp2, "marker %x, name %s", SWAP(mark), jp2_marker_name(mark));
        switch (mark) {
            case SOD:
                read_sod(j, f);
                break;
            case SIZ:
                read_siz(j, f);
                break;
            case CAP:
                read_cap(j, f);
                break;
            case COD:
                read_cod(j, f);
                break;
            case QCD:
                read_qcd(j, f);
                break;
            case CME:
                read_cme(j, f);
                break;
            case SOT:
                read_sot(j, f);
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
                printf("marker %x\n", SWAP(mark));
                fseek(f, 0, SEEK_END);
                fgetc(f);
                break;
        }
    }
}

int read_jp2h_box(FILE *f, struct jp2h_box *b)
{
    FFREAD_BOX_ST(b, f, FOURCC2UINT('j', 'p', '2', 'h'));
    read_jp2_ihdr_box(f, &b->ihdr);
    int64_t size = b->size - b->ihdr.size - 8;
    while(size) {
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
            if (b->n_colr == 0) {
                b->colr = malloc(sizeof(struct jp2_colr_box));
            } else {
                b->colr = realloc(b->colr, sizeof(struct jp2_colr_box) * (b->n_colr + 1));
            }
            read_jp2_colr_box(f, b->colr + b->n_colr);
            b->n_colr ++;
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
            break;
        }
        size -= p.size;
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
    fseek(f, 0 , SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    //first box, JP signature box
    read_box(f, &h);
    h.magic = read_u32(f);
    assert(h.magic == 0x0D0A870A);

    // second box, ftyp box
    read_ftyp(f, (void *)&(j->ftyp));

    // common process for other boxes
    while(ftell(f) < size) {
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
            if (j->uinf_num == 0) {
                j->uinf = malloc(sizeof(struct jp2_uinf_box));
            } else {
                j->uinf = realloc(j->uinf, sizeof(struct jp2_uinf_box) * (j->uinf_num + 1));
            }
            read_jp2_uinf_box(f, j->uinf + j->uinf_num);
            j->uinf_num++;
            break;
        case XML:
            if (j->xml_num == 0) {
                j->xml = malloc(sizeof(struct jp2_xml_box));
            } else {
                j->xml = realloc(j->uinf, sizeof(struct jp2_xml_box) * (j->xml_num + 1));
            }
            read_jp2_xml_box(f, j->xml+j->xml_num);
            j->xml_num ++;
            break;
        case UUID:
            if (j->uuid_num == 0) {
                j->uuid = malloc(sizeof(struct jp2_uuid_box));
            } else {
                j->uuid = realloc(j->uuid, sizeof(struct jp2_uuid_box) * (j->uuid_num + 1));
            }
            read_jp2_uuid_box(f, j->uuid + j->uuid_num);
            j->uuid_num ++;
            break;
        default:
            printf("unkown marker \"%s\"\n", UINT2TYPE(type));
            fseek(f, b.size - 8, SEEK_CUR);
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
    if (j->main_h.siz.comps) {
        free(j->main_h.siz.comps);
    }
    if (j->main_h.cod.p.precinct_size) {
        free(j->main_h.cod.p.precinct_size);
    }
    if (j->main_h.qcd.table) {
        free(j->main_h.qcd.table);
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
        fprintf(f, "\turl %s\n", j->uinf[i].url.location);
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
    if (j->main_h.cod.entropy) {
        for (int i = 0; i < j->main_h.cod.p.decomp_level_num+1; i ++) {
            fprintf(f, "\tCOD precinct witdh %d, height %d\n",
                    j->main_h.cod.p.precinct_size[i] & 0xF,
                    (j->main_h.cod.p.precinct_size[i]>>4) & 0xF);
        }
    }
    fprintf(f, "\tQCD len %d, guard_num %d quant_type %d\n", j->main_h.qcd.length,
            j->main_h.qcd.guard_num, j->main_h.qcd.quant_type);
    int table_len = 0;
    if (j->main_h.qcd.guard_num == 0) {
        table_len = 3 * j->main_h.cod.p.decomp_level_num + 1;
    } else if (j->main_h.qcd.guard_num == 1) {
        table_len = 2;
    } else if (j->main_h.qcd.guard_num == 2) {
        table_len = 6 * j->main_h.cod.p.decomp_level_num + 2;
    }
    fprintf(f, "\t");
    for (int i = 0; i < table_len; i++) {
        fprintf(f, " %x", j->main_h.qcd.table[i]);
    }
    fprintf(f, "\n");
    fprintf(f, "\tCME len %d, use %d, %s\n", j->main_h.cme.length,
            j->main_h.cme.use, j->main_h.cme.str);
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
