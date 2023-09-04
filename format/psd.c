#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include "file.h"
#include "psd.h"
#include "vlog.h"

VLOG_REGISTER(psd, INFO);

static int
PSD_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(psd, "fail to open %s", filename);
        return -ENOENT;
    }
    struct psd_file_header h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    h.version = ntohs(h.version);

    if ((!strncmp((char *)&h.signature, "8BPS", 4)) && (h.version == 1)) {
        return 0;
    }
    return -EINVAL;
}

static void
read_color_mode_data(PSD *s, FILE *f)
{
    fread(&s->color, 4, 1, f);
    s->color.length = ntohl(s->color.length);
    if (s->color.length) {
        //TBD
    }
}

static int
read_str_till_null(FILE *f, char * str)
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
read_image_resource_block(PSD *s, FILE *f)
{
    char name[32];
    // if (s->res.num == 0)
    //     s->res.block = malloc(sizeof(struct image_resource_block));
    // else
    //     s->res.block = realloc(s->res.block, (s->res.num + 1) * sizeof(struct image_resource_block));
    s->res.num ++;
    // fread(&s->res.block[s->res.num-1], 6, 1, f);
    uint32_t sig; /* 8BIM */
    uint16_t id;
    uint32_t size;
    fread(&sig, 4, 1, f);
    fread(&id, 2, 1, f);
    id = ntohs(id);
    // s->res.block[s->res.num-1].id = ntohs(s->res.block[s->res.num-1].id);
    int l = read_str_till_null(f, name);
    if (l % 2) {
        fgetc(f);
        l += 1;
    }
    l += 6;
    fread(&size, 4, 1, f);
    size = ntohl(size);
    l += 4;
    uint8_t *d;

    switch (id) {
        case RESOLUTION_INFO:
            fread(&s->res.resolution, sizeof(struct resolution_info), 1, f);
            break;
        default:
            d = malloc(size);
            fread(d, size, 1, f);
            free(d);
            break;
    }
    if (size % 2) {
        fgetc(f);
        l += 1;
    }

    return l + size;
}

static void
read_image_resource(PSD *s, FILE *f)
{
    fread(&s->res, 4, 1, f);
    s->res.length = ntohl(s->res.length);
    int l = s->res.length;
    while (l) {
        l -= read_image_resource_block(s, f);
    }
}

static void
read_layer_record(struct channel_record *r, FILE *f)
{
    fread(r, 16+2, 1, f);
    r->top = ntohl(r->top);
    r->left = ntohl(r->left);
    r->bottom = ntohl(r->bottom);
    r->right = ntohl(r->right);
    r->num = ntohs(r->num);

    r->info = malloc(r->num * 6);
    fread(r->info, 6* r->num, 1, f);
    for (int i = 0; i < r->num; i ++) {
        r->info[i].id = ntohs(r->info[i].id);
        r->info[i].len = ntohl(r->info[i].len);
    }

    fread(&r->blend, 16, 1, f);

    r->extra_len = ntohl(r->extra_len);

    fread(&r->mask_data, 4, 1, f);
    r->mask_data.size = ntohl(r->mask_data.size);
    if (r->mask_data.size > 0) {
        fread(&r->mask_data.top, 18, 1, f);
        r->mask_data.top = ntohl(r->mask_data.top);
        r->mask_data.left = ntohl(r->mask_data.left);
        r->mask_data.bottom = ntohl(r->mask_data.bottom);
        r->mask_data.right = ntohl(r->mask_data.right);
        VDBG(psd, "reserv %x\n", r->mask_data.reserv);
        if (r->mask_data.applied == 1) {
            r->mask_data.mask_parameter = fgetc(f);
        }
        if (r->mask_data.size == 20) {
            r->mask_data.mask_parameter = fgetc(f);
            if (r->mask_data.mask_parameter & 0x1) {
                fgetc(f);
            } else if (r->mask_data.mask_parameter & 0x4) {
                fgetc(f);
            } else {
                fgetc(f);
                fgetc(f);
                fgetc(f);
                fgetc(f);
                fgetc(f);
                fgetc(f);
                fgetc(f);
                fgetc(f);
            }
        } else {
            fread(&r->mask_data.real_flag, 18, 1, f);
            r->mask_data.real_top = ntohl(r->mask_data.real_top);
            r->mask_data.real_left = ntohl(r->mask_data.real_left);
            r->mask_data.real_bottom = ntohl(r->mask_data.real_bottom);
            r->mask_data.real_right = ntohl(r->mask_data.real_right);
        }
    }

    fread(&r->blend_data, 4, 1, f);
    r->blend_data.length = ntohl(r->blend_data.length);

    VDBG(psd, "extra_len %d , %d\n", r->extra_len, r->blend_data.length);
    if (r->blend_data.length) {
        fread(&r->blend_data.source, 8, 1, f);
        r->blend_data.ranges = malloc(8 * r->num);
        fread(r->blend_data.ranges, 8, r->num, f);
    }
    int len_left = r->extra_len - r->mask_data.size - 4 - r->blend_data.length - 4;
    r->name = malloc(len_left);
    fread(r->name, len_left, 1, f);
    VDBG(psd, "%d name %s\n",len_left , r->name);
}

static void
read_channel_image(struct channel_record *r, struct channel_image *c, FILE *f)
{
    fread(&c->compression, 2, 1, f);
    c->compression = ntohs(c->compression);
    if (c->compression == 0) {
        int tsize = 0, offset = 0;
        for (int i = 0; i < r->num; i ++) {
            tsize += r->info[i].len;
        }
        c->data = malloc(tsize);
        for (int i = 0; i < r->num; i ++) {
            VDBG(psd, "tsize %d, %d\n", tsize, ntohl(r->info[i].len));
            fread(c->data + offset, r->info[i].len, 1, f);
            offset += r->info[i].len;
            VDBG(psd, "offset %lx\n", ftell(f));
        }
    } else {
        //TBD
    }
}

static void
read_extra_layer_info(PSD *s, FILE *f)
{
    uint32_t sig;
    fread(&sig, 4, 1, f);
    fseek(f, -4, SEEK_CUR);
    if (strcmp((char *)&sig, "8BIM") == 0 || strcmp((char *)&sig, "8B64") == 0) {
        if (s->layer.extra_num == 0) {
            s->layer.extra = malloc(sizeof(struct extra_layer_info));
        } else {
            s->layer.extra = realloc(s->layer.extra, (s->layer.extra_num + 1) *sizeof(struct extra_layer_info));
        }
        fread(&s->layer.extra + s->layer.extra_num, 12, 1, f);
        s->layer.extra[s->layer.extra_num].length = ntohl(s->layer.extra[s->layer.extra_num].length);
        VDBG(psd, "left %d", s->layer.mask.length);
        if (s->layer.extra[s->layer.extra_num].length) {
            s->layer.extra[s->layer.extra_num].data = malloc(s->layer.extra[s->layer.extra_num].length);
            fread(s->layer.extra[s->layer.extra_num].data, s->layer.extra[s->layer.extra_num].length, 1, f);
        }
        s->layer.extra_num ++;
    }
}


static void
read_layer_and_mask(PSD *s, FILE *f)
{
    fread(&s->layer.length, 4, 1, f);
    s->layer.length = ntohl(s->layer.length);
    fread(&s->layer.info, 6, 1, f);
    s->layer.info.length = ntohl(s->layer.info.length);
    s->layer.info.count = ntohs(s->layer.info.count);
    s->layer.info.records = malloc(sizeof(struct channel_record) * s->layer.info.count);
    s->layer.info.chan_data = malloc(sizeof(struct channel_image) * s->layer.info.count);

    for (int i = 0; i < s->layer.info.count; i ++) {
        read_layer_record(&s->layer.info.records[i], f);
    }

    for (int i = 0; i < s->layer.info.count; i ++) {
        read_channel_image(&s->layer.info.records[i], &s->layer.info.chan_data[i], f);
    }

    /* FIXME */
    fseek(f, -2, SEEK_CUR);
    //read mask info
    fread(&s->layer.mask, 4, 1, f);
    s->layer.mask.length = ntohl(s->layer.mask.length);
    VDBG(psd, "left %d, offset 0x%lx", s->layer.mask.length, ftell(f));
    if (s->layer.mask.length) {
        fread(&s->layer.mask.overlay_cs, 13, 1, f);
        fgetc(f);
    }

    // read extra layer info
    read_extra_layer_info(s, f);
}

static void
read_image_chan(PSD *s, FILE *f, int chan, int size)
{
    int i = 0;
    int id = 2 - chan; //RGB -> BGR
    while (i < size) {
        s->data[4*i + id] = fgetc(f);
        i ++;
    }
}

static void
read_image_data(PSD *s, FILE *f)
{
    fread(&s->compression, 2, 1, f);
    s->compression = ntohs(s->compression);
    if (s->compression == 0) {
        int size = 0;
        for (int i = 0; i < s->layer.info.count; i ++) {
            size += (s->layer.info.records[i].bottom - s->layer.info.records[i].top) * (s->layer.info.records[i].right - s->layer.info.records[i].left);
        }
        s->data = malloc(size * 4);
        for (int i = 0; i < s->h.chan_num; i ++) {
            read_image_chan(s, f, i, size);
        }
    }
}


static struct pic* 
PSD_load(const char* filename)
{
    struct pic * p = pic_alloc(sizeof(PSD));
    PSD * s = p->pic;
    FILE *f = fopen(filename, "rb");
    fread(&s->h, sizeof(struct psd_file_header), 1, f);
    s->h.height = ntohl(s->h.height);
    s->h.width = ntohl(s->h.width);
    s->h.depth = ntohs(s->h.depth);
    s->h.chan_num = ntohs(s->h.chan_num);
    s->h.version = ntohs(s->h.version);
    s->h.mode = ntohs(s->h.mode);

    p->depth = 32;
    p->width = ((s->h.width + 3) >> 2) << 2;
    p->height = s->h.height;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;

    read_color_mode_data(s, f);

    read_image_resource(s, f);

    read_layer_and_mask(s, f);

    read_image_data(s, f);
    p->pixels = s->data;
    fclose(f);

    return p;
}

static void 
PSD_free(struct pic* p)
{
    PSD * s = (PSD *)p->pic;
    if (s->data)
        free(s->data);
    if (s->color.data)
        free(s->color.data);
    if (s->layer.extra)
        free(s->layer.extra);
    if (s->layer.info.count) {
        for (int i = 0; i < s->layer.info.count; i ++) {
            free(s->layer.info.records[i].info);
            free(s->layer.info.records[i].name);
            if (s->layer.info.chan_data[i].data)
                free(s->layer.info.chan_data[i].data);
        }
        free(s->layer.info.records);
        free(s->layer.info.chan_data);
    }
    pic_free(p);
}


static void 
PSD_info(FILE* f, struct pic* p)
{
    PSD * s = (PSD *)p->pic;
    fprintf(f, "PSD file formart:\n");
    fprintf(f, "-------------------------------------\n");
    fprintf(f, "\twidth %d, height %d\n", s->h.width, s->h.height);
    fprintf(f, "\tdepth %d, channels %d, mode %d\n", s->h.depth, s->h.chan_num, s->h.mode);
    fprintf(f, "\tlayer count %d\n", s->layer.info.count);
    for (int i = 0; i < s->layer.info.count; i ++) {
        fprintf(f, "\tlayer %s channel %d\n", s->layer.info.records[i].name, s->layer.info.records[i].num);
        for (int j = 0; j < s->layer.info.records[i].num; j ++) {
            fprintf(f, "\t\tid %x, len %d\n", s->layer.info.records[i].info[j].id, s->layer.info.records[i].info[j].len);
        }
    }
}



static struct file_ops psd_ops = {
    .name = "PSD",
    .probe = PSD_probe,
    .load = PSD_load,
    .free = PSD_free,
    .info = PSD_info,
};

void PSD_init(void)
{
    file_ops_register(&psd_ops);
}