#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "file.h"
#include "jp2.h"


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
    j->jp2h.ihdr.width = ntohl(j->jp2h.ihdr.width);
    j->jp2h.ihdr.height = ntohl(j->jp2h.ihdr.height);
    j->jp2h.ihdr.num_comp = ntohs(j->jp2h.ihdr.num_comp);
    j->jp2h.ihdr.comps = malloc(sizeof(struct jp2_component) * j->jp2h.ihdr.num_comp);
}

void
read_colr(JP2 *j, FILE *f, uint32_t len)
{
    fread(&j->jp2h.colr, 3, 1, f);
    if (j->jp2h.colr.method == ENUMERATED_COLORSPACE) {
        fread(&j->jp2h.colr.enum_cs, 4, 1, f);
        j->jp2h.colr.enum_cs = ntohl(j->jp2h.colr.enum_cs);
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
    for (int i = 0; i < j->jp2h.ihdr.num_comp; i ++) {
        j->jp2h.ihdr.comps[i].bpcc = fgetc(f);
    }
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
    j->uuid_info[j->uinf_num-1].ulst.num_uuid = ntohs(j->uuid_info[j->uinf_num-1].ulst.num_uuid);
    j->uuid_info[j->uinf_num-1].ulst.id[0] = malloc(j->uuid_info[j->uinf_num-1].ulst.num_uuid * 8);
    j->uuid_info[j->uinf_num-1].ulst.id[1] = malloc(j->uuid_info[j->uinf_num-1].ulst.num_uuid * 8);
    for (int i = 0; i < j->uuid_info[j->uinf_num-1].ulst.num_uuid; i ++) {
        fread(j->uuid_info[j->uinf_num-1].ulst.id[0]+i, 8, 1, f);
        fread(j->uuid_info[j->uinf_num-1].ulst.id[1]+i, 8, 1, f);
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
    s->length = ntohs(s->length);
    s->cap = ntohs(s->cap);
    s->width = ntohl(s->width);
    s->height = ntohl(s->height);
    s->left = ntohl(s->left);
    s->top = ntohl(s->top);
    s->tile_width = ntohl(s->tile_width);
    s->tile_height = ntohl(s->tile_height);
    s->tile_left = ntohl(s->tile_left);
    s->tile_top = ntohl(s->tile_top);
    s->component_num = ntohs(s->component_num);

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
    j->main_h.cod.length = ntohs(j->main_h.cod.length);
    j->main_h.cod.layers_num = ntohs(j->main_h.cod.layers_num);
    l += 7;
    fread(&j->main_h.cod.p, 5, 1, f);
    l += 5;

    if (j->main_h.cod.entropy) {
        j->main_h.cod.p.precinct_size = malloc(j->main_h.cod.p.decomp_level_num + 1);
        fread(j->main_h.cod.p.precinct_size, j->main_h.cod.p.decomp_level_num + 1, 1, f);
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
    j->main_h.cap.length = ntohs(j->main_h.cap.length);
    j->main_h.cap.bitmap = ntohl(j->main_h.cap.bitmap);
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
    j->main_h.cme.length = ntohs(j->main_h.cme.length);
    j->main_h.cme.use = ntohs(j->main_h.cme.use);
    j->main_h.cme.str = malloc(j->main_h.cme.length - 4);
    fread(&j->main_h.cme.str, j->main_h.cme.length - 4, 1, f);
    l += j->main_h.cme.length;
    return l;
}

int
read_qcd(JP2 *j, FILE *f)
{
    int l = 0;
    fread(&j->main_h.qcd, 3, 1, f);
    l += 3;
    j->main_h.qcd.length = ntohs(j->main_h.qcd.length);
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

    /* record tile starting point to calc len in cod */
    t->sot.offset_start = ftell(f) - 2;
    fread(&t->sot, 10, 1, f);
    l += 10;
    t->sot.length = ntohs(t->sot.length);
    t->sot.tile_id = ntohs(t->sot.tile_id);
    t->sot.tile_size = ntohl(t->sot.tile_size);
    return l;
}

int
read_sop(JP2 *j, FILE *f)
{
    int l = 0;
    int ti = j->tile_nums - 1;
    fread(&j->tiles[ti].sop, 4, 1, f);
    l += 4;
    j->tiles[ti].sop.length = ntohs(j->tiles[ti].sop.length);
    j->tiles[ti].sop.seq_num = ntohs(j->tiles[ti].sop.seq_num);
    return l;
}

int
read_data(JP2 *j, FILE *f) {
    /* read till eoc */
    struct sop sop;
    uint8_t *p = j->data;
    int start = ftell(f);
    int len = j->tiles[j->tile_nums-1].sot.tile_size -
        (start - j->tiles[j->tile_nums-1].sot.offset_start);

    // printf("total len %d\n", len);
    int prev = -1, cur = 0;
    while (len > 0) {
        cur = fgetc(f);
        len --;
        if (prev == 0xFF) {
            if (cur == 0xD9) {
                printf("0xFF 0xD9\n");
                break;
            } else if (cur == 0x91) {
                /* SOP */
                fread(&sop, sizeof(struct sop), 1, f);
                len -= sizeof(struct sop);
                sop.length = ntohs(sop.length);
                sop.seq_num = ntohs(sop.seq_num);
                // printf("SOP SEQ %d\n", sop.seq_num);
                prev = fgetc(f);
                len --;
                continue;
            } else if (cur == 0x92) {
                /* EPH */
                // printf("EPH SEQ %d\n", sop.seq_num);
                prev = fgetc(f);
                len --;
                continue;
            }
            // printf("len %d, %02x %02x\n", len, prev, cur);
        }
        if (prev > 0) {
            *p++ = prev;
        }
        prev = cur;
    }
    if (prev != 0xFF)
        *p++ = prev;
    else
        fseek(f, -1, SEEK_CUR);
    int l = ftell(f) - start;
    // printf("a tile %d\n", l);
    return l;
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

        // printf("marker %x\n", ntohs(mark));
        switch (mark) {
            case SOD:
                read_data(j, f);
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
            case EOC:
                //may be absent in case corrupt
                printf("end of code stream %d, %ld\n", l, ftell(f));
                return;
            default:
                printf("marker %x\n", ntohs(mark));
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
    char* s;
    uint32_t type = read_box(f, &b, len);
    switch (type) {
        case JP2H:
            read_next_box(j, f,  b.size - 8);
            break;
        case IHDR:
            read_ihdr(j, f);
            break;
        case BPCC:
            read_bpcc(j, f);
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
    pic_free(p);
}

static void
JP2_info(FILE *f, struct pic* p)
{
    JP2 *j = (JP2 *) p->pic;
    fprintf(f, "JPEG2000 file formart\n");
    fprintf(f, "---------------------------\n");
    fprintf(f, "\theight %d, width %d, num_comp %d\n", j->jp2h.ihdr.height,
                j->jp2h.ihdr.width, j->jp2h.ihdr.num_comp);
    for (int i = 0; i < j->jp2h.ihdr.num_comp; i ++) {
        fprintf(f, "\tbitsperpixel %d\n", j->jp2h.ihdr.comps[i].bpcc);
    }
    for (int i = 0; i < j->uinf_num; i ++) {
        fprintf(f, "\turl %s\n", j->uuid_info[i].url.location);
    }
    fprintf(f, "---------------------------\n");
    fprintf(f, "\tSIZ height %d, width %d, num_comp %d\n", j->main_h.siz.height, 
                j->main_h.siz.width, j->main_h.siz.component_num);
    fprintf(f, "\ttile num %d\n", j->tile_nums);

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
