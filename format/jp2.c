#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "bitstream.h"
#include "file.h"
#include "jp2.h"
#include "vlog.h"

VLOG_REGISTER(jp2, DEBUG)

void read_next_box(JP2 *j, FILE *f, int len);


static int
JP2_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct jp2_signature_box h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_type == TYPE2UINT("jP  ") || 
        h.major_type == TYPE2UINT("jP2 ")) {
        return 0;
    }

    return -EINVAL;
}

void
read_ihdr(JP2 *j, FILE *f)
{
    fread(&j->jp2h.ihdr, 14, 1, f);
    j->jp2h.ihdr.width = SWAP(j->jp2h.ihdr.width);
    j->jp2h.ihdr.height = SWAP(j->jp2h.ihdr.height);
    j->jp2h.ihdr.num_comp = SWAP(j->jp2h.ihdr.num_comp);
    // j->jp2h.ihdr.comps = malloc(sizeof(struct jp2_bpc) * j->jp2h.ihdr.num_comp);
}

void
read_colr(JP2 *j, FILE *f, uint32_t len)
{
    fread(&j->jp2h.colr, 3, 1, f);
    if (j->jp2h.colr.method == ENUMERATED_COLORSPACE) {
        fread(&j->jp2h.colr.enum_cs, 4, 1, f);
        j->jp2h.colr.enum_cs = SWAP(j->jp2h.colr.enum_cs);
        if (j->jp2h.colr.enum_cs == 16) {
            // sRGB
        } else if (j->jp2h.colr.enum_cs == 17) {
            // greyscale
        } else if (j->jp2h.colr.enum_cs == 18) {
            // sYCC
        } else {
            printf("invalid enum color space\n");
        }
    } else if (j->jp2h.colr.method == RESTRICT_ICC_PROFILE) {
        printf("icc len %d\n", len - 3);
    }
}

void
read_bpcc(JP2 *j, FILE *f)
{
    j->jp2h.ihdr.comps = malloc(sizeof(struct jp2_bpc) * j->jp2h.ihdr.num_comp);
    fread(j->jp2h.ihdr.comps, sizeof(struct jp2_bpc), j->jp2h.ihdr.num_comp, f);
}

void
read_cmap(JP2 *j, FILE *f)
{
    // for (int i = 0; i < j->; i ++) {
    //     j->jp2h
    // }
}

void
read_cdef(JP2 *j, FILE *f)
{
    fread(&j->jp2h.cdef, 1, 1, f);
}

void
read_pclr(JP2 *j, FILE *f)
{
    fread(&j->jp2h.pclr.num_entry, 2, 1, f);
    j->jp2h.pclr.num_channel = fgetc(f);
    j->jp2h.pclr.channels = malloc(j->jp2h.pclr.num_channel);
    j->jp2h.pclr.entries = malloc(sizeof(uint32_t) * j->jp2h.pclr.num_channel * j->jp2h.pclr.num_entry);
    fread(j->jp2h.pclr.channels, 1, j->jp2h.pclr.num_channel, f);
    for (int i = 0; i < j->jp2h.pclr.num_entry; i ++) {
        for (int k = 0; k < j->jp2h.pclr.num_channel; k ++) {
            int n = (j->jp2h.pclr.channels[k].size + 8) >> 3;
            fread(j->jp2h.pclr.entries + i * j->jp2h.pclr.num_entry + k, n, 1, f);
        }
    }
}


void
read_resc(JP2 *j, FILE *f)
{

}

void 
read_resolution(JP2* j, FILE *f)
{

}

void 
read_url(JP2* j, FILE * f, int len)
{
    fread(&j->uuid_info[j->uinf_num-1].url, 4, 1, f);
    j->uuid_info[j->uinf_num-1].url.location = malloc(len - 4);
    fread(j->uuid_info[j->uinf_num-1].url.location, len - 4, 1, f);
}

void 
read_ulst(JP2* j, FILE * f)
{
    fread(&j->uuid_info[j->uinf_num-1].ulst.num_uuid, 2, 1, f);
    j->uuid_info[j->uinf_num-1].ulst.num_uuid = SWAP(j->uuid_info[j->uinf_num-1].ulst.num_uuid);
    j->uuid_info[j->uinf_num-1].ulst.id = malloc(j->uuid_info[j->uinf_num-1].ulst.num_uuid * sizeof(uint128_t));
    for (int i = 0; i < j->uuid_info[j->uinf_num-1].ulst.num_uuid; i ++) {
        fread(j->uuid_info[j->uinf_num-1].ulst.id + i, sizeof(uint128_t), 1, f);
    }
}

void
read_uinf(JP2* j, FILE * f, int len)
{
    if (j->uinf_num == 0) {
        j->uuid_info = malloc(sizeof(struct jp2_uinf));
    } else {
        j->uuid_info = realloc(j->uuid_info, sizeof(struct jp2_uinf) * (j->uinf_num+1));
    }
    j->uinf_num ++;
    
    // read ulst
    read_next_box(j, f, len);
    // read url
    len = len - 8 - 2 - j->uuid_info[j->uinf_num-1].ulst.num_uuid * 8;
    read_next_box(j, f, len);
}

//may ignore this part
void
read_xml(JP2 *j, FILE *f, int len)
{
    if (j->xml_num == 0) {
        j->xml = malloc(sizeof(struct jp2_xml));
    } else {
        j->xml = realloc(j->xml, sizeof(struct jp2_xml) * (j->xml_num + 1));
    }
    j->xml_num ++;
    j->xml[j->xml_num-1].data = malloc(len);
    fread(j->xml[j->xml_num-1].data, len, 1, f);
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
    printf("guard num %d table len %d, decomp %d\n", j->main_h.qcd.guard_num,
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
    struct tag_tree* tree[32];
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
void read_packet_header(JP2 *j, struct bits_vec *v, int tile_id) {
    // start process packet header: B.10
    //  - zero length packet;
    //  - code-block inclusion;
    //  - zero bit-plane information
    //  - number of coding passes
    //  - length of the code-block compressed image data from a given code-block
    // see B.10.3, first bit denotes whether the packet has a length of zero
    int layers_num = j->main_h.cod.layers_num;
    int zl = READ_BIT(v);
    int inclusion;
    // int bitplane;
    // int pass = 0;
    VDBG(jp2, "layers_num %d zero length packet %d", layers_num, zl);
    if (zl) {
        inclusion = tag_tree_decode(v, 0, 0);
    } else {
        inclusion = READ_BIT(v);
    }
    printf("zl %d, inclusion %d\n", zl, inclusion);
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
    read_packet_header(j, v, tile_id);
    if (j->main_h.cod.eph) {
        // if EPH is enabled
    }

    int l = ftell(f) - start;
    VDBG(jp2, "a tile %d\n", l);
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

static const char * jp2_marker_name(uint16_t mark)
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

void 
read_stream(JP2 *j, FILE *f, int l)
{
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
                VERR(jp2, "end of code stream %d, %ld\n", l, ftell(f));
                return;
            default:
                printf("marker %x\n", SWAP(mark));
                fseek(f, 0, SEEK_END);
                fgetc(f);
                break;
        }
    }
}

void 
read_next_box(JP2 *j, FILE *f, int len)
{
    struct box b;
    uint32_t type = read_box(f, &b, len);
    VDBG(jp2, "box type %s", type2name(type));
    switch (type) {
        case JP2H:
            read_next_box(j, f, b.size - 8);
            break;
        case IHDR:
            read_ihdr(j, f);
            break;
        case BPCC:
            read_bpcc(j, f);
            break;
        case COLR:
            read_colr(j, f, b.size - 8);
            break;
        case PCLR:
            read_pclr(j, f);
            break;
        // case RES:
        //     read_resolution(j, f);
        //     break;
        // case RESC:
        //     read_resc(j, f);
        //     break;
        case JP2C:
            read_stream(j, f, b.size - 8);
            break;
        case UINF:
            read_uinf(j, f, b.size - 8);
            break;
        case ULST:
            read_ulst(j, f);
            break;
        case URL:
            read_url(j, f, b.size - 8);
            break;
        case XML:
            read_xml(j, f, b.size - 8);
            break;
        default:
            printf("unkown marker \"%s\"\n", UINT2TYPE(type));
            fseek(f, b.size - 8, SEEK_CUR);
            break;
    }
}

static struct pic* 
JP2_load(const char *filename)
{
    struct pic *p = pic_alloc(sizeof(JP2));
    JP2 *j = p->pic;
    struct jp2_signature_box h;
    FILE * f = fopen(filename, "rb");
    fseek(f, 0 , SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    //first box, JP box
    fread(&h, sizeof(h), 1, f);

    //second box, ftyp box
    read_ftyp(f, (void *)&(j->ftyp));

    // common process for other boxes
    while(ftell(f) < size) {
        read_next_box(j, f, -1);
    }
    fclose(f);

    return p;
}

static void 
JP2_free(struct pic *p)
{
    JP2 *j = (JP2 *) p->pic;
    if(j->jp2h.ihdr.num_comp > 0) {
        if (j->jp2h.ihdr.comps)
            free(j->jp2h.ihdr.comps);
    }
    if (j->jp2h.colr.method == 2) {
        free(j->jp2h.colr.iccpro.iccp);
    }
    for (int i = 0; i <j->uinf_num; i ++) {
        if(j->uuid_info[i].ulst.num_uuid) {
            free(j->uuid_info[i].ulst.id);
        }
        if (j->uuid_info[i].url.location) {
            free(j->uuid_info[i].url.location);
        }
    }
    if (j->uuid_info)
        free(j->uuid_info);
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
    for (int i = 0; j->jp2h.ihdr.comps && i < j->jp2h.ihdr.num_comp; i++) {
        fprintf(f, "\tbitsperpixel %d\n", j->jp2h.ihdr.comps[i].value_minus_one+1);
    }
    fprintf(f, "\tCOLR method %d, precedence %d, approximation %d, colorspace %d\n",
            j->jp2h.colr.method, j->jp2h.colr.precedence, j->jp2h.colr.approx, j->jp2h.colr.enum_cs);
    if (j->jp2h.colr.method == RESTRICT_ICC_PROFILE) {
        //fprintf profile
    }
    for (int i = 0; i < j->uinf_num; i ++) {
        fprintf(f, "\tulst num_uuid %d:\n", j->uuid_info[i].ulst.num_uuid);
        for (int k = 0; k < j->uuid_info[i].ulst.num_uuid; k++) {
            fprintf(f, "\t\t%016" PRIx64 "%016" PRIx64 "\n",
                    j->uuid_info[i].ulst.id[k].v[0],
                    j->uuid_info[i].ulst.id[k].v[1]);
        }
        fprintf(f, "\turl %s\n", j->uuid_info[i].url.location);
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
