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
    fread(&j->siz, 38, 1, f);
    j->siz.length = ntohs(j->siz.length);
    j->siz.cap = ntohs(j->siz.cap);
    j->siz.width = ntohl(j->siz.width);
    j->siz.height = ntohl(j->siz.height);
    j->siz.left = ntohl(j->siz.left);
    j->siz.top = ntohl(j->siz.top);
    j->siz.tile_width = ntohl(j->siz.tile_width);
    j->siz.tile_height = ntohl(j->siz.tile_height);
    j->siz.tile_left = ntohl(j->siz.tile_left);
    j->siz.tile_top = ntohl(j->siz.tile_top);
    j->siz.component_num = ntohs(j->siz.component_num);

    l += 38;
    j->siz.comps = malloc(sizeof(struct scomponent)*j->siz.component_num);
    fread(j->siz.comps, sizeof(struct scomponent), j->siz.component_num, f);
    l += sizeof(struct scomponent)*j->siz.component_num;
    return l;
}

int
read_cod(JP2 *j, FILE *f)
{
    int l = 0;
    fread();
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
    l -= 2;
    while(l) {
        mark = read_marker(f);
        l -= 2;
        switch (mark) {
            case SOD:
                break;
            case SIZ:
                l -= read_siz(j, f);
                break;
            case COD:
                l -= read_cod();
            default:
                printf("marker %x\n", ntohs(mark));
                fseek(f, 0, SEEK_END);
                fgetc(f);
                l = 0;
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
            s = UINT2TYPE(type);
            printf("unkown marker \"%s\"\n", s);
            free(s);
            fseek(f, b.size - 8, SEEK_CUR);
            break;
    }
}

static struct pic* 
JP2_load(const char *filename) {
    struct pic *p = malloc(sizeof(struct pic));
    JP2 *j = malloc(sizeof(JP2));
    p->pic = j;
    struct jp2_signature_box h;
    FILE * f = fopen(filename, "rb");

    //first box, JP box
    fread(&h, sizeof(h), 1, f);

    //second box, ftyp box
    read_ftyp(f, (void *)&(j->ftyp));

    // common process for other boxes
    while(!feof(f)) {
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
    free(j);
    free(p);
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
    fprintf(f, "\tSIZ height %d, width %d, num_comp %d\n", j->siz.height, 
                j->siz.width, j->siz.component_num);

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