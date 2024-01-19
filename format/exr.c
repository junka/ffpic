#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "colorspace.h"
#include "exr.h"
#include "file.h"
#include "vlog.h"

VLOG_REGISTER(exr, INFO)

static int 
EXR_probe(const char* filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(exr, "fail to open %s", filename);
        return -ENOENT;
    }
    uint32_t magic;
    int len = fread(&magic, sizeof(magic), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (ntohl(magic) == 0x762f3101) {
        return 0;
    }

    return -EINVAL;
}

int
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

void
read_chlist(EXR *e, FILE *f, int size)
{
    char name[32];
    int i = 0, s;
    e->chnls = malloc(sizeof(struct exr_chlist));
    while (size-1) {
        if (i > 0) {
            e->chnls = realloc(e->chnls, sizeof(struct exr_chlist) * (i+1));
        }
        s = read_str_till_null(f, name);
        e->chnls[i].name = malloc(s);
        memcpy(e->chnls[i].name, name, s);
        size -= s;
        fread(&e->chnls[i].pixel_type, 16, 1, f);
        i ++;
        size -= 16;
    }
    e->chnl_num = i;
    fgetc(f);
}

void
read_attribute_value(EXR *e, FILE *f, char *name, int size)
{
    if (!strcmp("compression", name)) {
        e->compression = fgetc(f);
    } else if (!strcmp("envmap", name)) {
        e->envmap = fgetc(f);
    } else if (!strcmp("channels", name)) {
        read_chlist(e, f, size);
    } else if (!strcmp("dataWindow", name)) {
        fread(&e->data_win, size, 1, f);
    } else if (!strcmp("displayWindow", name)) {
        fread(&e->display_win, size, 1, f);
    } else if (!strcmp("lineOrder", name)) {
        e->lineorder = fgetc(f);
    } else if (!strcmp("pixelAspectRatio", name)) {
        fread(&e->aspect_ratio, 4, 1, f);
    } else if (!strcmp("screenWindowWidth", name)) {
        fread(&e->win_width, sizeof(float), 1, f);
    } else if (!strcmp("screenWindowCenter", name)) {
        fread(&e->win_center, sizeof(struct exr_v2f), 1, f);
    }
    else {
        uint8_t *d = malloc(size);
        fread(d, size, 1, f);
        free(d);
    }
}

void
read_attribute(EXR *e, FILE *f)
{
    char name[256], type[32];
    int size;
    read_str_till_null(f, name);
    read_str_till_null(f, type);
    fread(&size, 4, 1, f);
    VINFO(exr, "%s: %s: %d", name, type, size);
    read_attribute_value(e, f, name, size);
}

bool
test_next_null(FILE *f)
{
    uint8_t c = fgetc(f);
    if (c == 0) {
        return true;
    } else {
        fseek(f, -1, SEEK_CUR);
        return false;
    }
}

union fp32
{
    uint32_t u;
    float f;
};

static float 
uint16_cov_float(uint16_t value)
{
    const union fp32 magic = { (254U - 15U) << 23 };
    const union fp32 was_infnan = { (127U + 16U) << 23 };
    union fp32 out;

    out.u = (value & 0x7FFFU) << 13;   /* exponent/mantissa bits */
    out.f *= magic.f;                  /* exponent adjust */
    if (out.f >= was_infnan.f)         /* make sure Inf/NaN survive */
    {
        out.u |= 255U << 23;
    }
    out.u |= (value & 0x8000U) << 16;  /* sign bit */

    return out.f;
}

static uint8_t 
exr_to_rgb(float v)
{
    if (v <= 0.0031308)
        return (uint8_t)((v * 12.92) * 255.0);
    else
        return (uint8_t)((1.055*(pow(v, (1.0/2.4))-0.055) * 255.0));
}

float
read_pixel(FILE *f, int type)
{
    int data_len[3] = {4, 2, 4};
    if (type == 2) {
        float data;
        fread(&data, data_len[type], 1, f);
        return data;
    } else if (type == 1) {
        uint16_t data;
        fread(&data, data_len[type], 1, f);
        float b = uint16_cov_float(data);
        return b;
    } else {
        uint32_t data;
        fread(&data, data_len[type], 1, f);
        return data;
    }

}

static struct pic* 
EXR_load(const char* filename)
{
    struct pic * p = pic_alloc(sizeof(EXR));
    EXR * e = p->pic;
    FILE *f = fopen(filename, "rb");
    fread(&e->h, sizeof(struct exr_header), 1, f);

    /* read headers */
    while (!test_next_null(f)) {
        read_attribute(e, f);
    }

    uint32_t width, height;
    // uint8_t *data;
    width = e->data_win.xMax - e->data_win.xMin + 1;
    height = e->data_win.yMax - e->data_win.yMin + 1;

    p->width = width;
    p->height = height;
    p->depth = 32;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    
    e->data = malloc(height * p->pitch);
    // int data_len;
    uint64_t* offset;
    /* read offset table */
    if (!e->h.multipart) {
        offset = (uint64_t*)malloc(sizeof(uint64_t) * height);
        fread(offset, sizeof(uint64_t), height, f);
    } else {
        //TBD
    }

    /*read pixel data*/
    uint32_t data_size;
    int ystart = 0, yend = 0, ydelta = 0;
    if (INCREASING_Y == e->lineorder) {
        ystart = 0;
        ydelta = 1;
        yend = height - 1;
    } else if (DECREASING_Y == e->lineorder) {
        ystart = height - 1;
        ydelta = -1;
        yend = 0;
    }
    for (int y = ystart; (ydelta * (yend - y)) >= 0; y += ydelta) {
        fseek(f, offset[y], SEEK_SET);
        fread(&data_size, 4, 1, f);
        for (int i = 0; i < e->chnl_num; i ++) {
            for (uint32_t x = 0; x < width; x ++) {
                float a = read_pixel(f, e->chnls[i].pixel_type);
                e->data[p->pitch * y + 4 * x + i] = exr_to_rgb(a);
            }
        }
    }

    fclose(f);
    free(offset);
    p->pixels = e->data;
    p->format = CS_PIXELFORMAT_RGB888;

    return p;
}


static void 
EXR_free(struct pic* p)
{
    EXR * e = (EXR *)p->pic;
    for (int i = 0; i < e->chnl_num; i ++) {
        free(e->chnls[i].name);
    }
    if (e->chnls)
        free(e->chnls);
    if (e->data)
        free(e->data);
    pic_free(p);
}


static void 
EXR_info(FILE* f, struct pic* p)
{
    EXR * e = (EXR *)p->pic;
    fprintf(f, "EXR file formart:\n");
    fprintf(f, "-------------------------------------\n");
    fprintf(f, "\tversion %d, %s %s\n", e->h.version,
            e->h.single_tile ? "singlepart" : (!e->h.no_image &&
            !e->h.multipart) ? "singlepart" : "multipart", e->h.long_name?"long_name":"no_long_name");
    fprintf(f, "\tcompression %d, lineOrder %d\n", e->compression, e->lineorder);
    fprintf(f, "\tdata win: x %d - %d , y %d - %d\n", e->data_win.xMin, e->data_win.xMax,
                            e->data_win.yMin, e->data_win.yMax);
    fprintf(f, "\tdisplay win: x %d - %d , y %d - %d\n", e->display_win.xMin, e->display_win.xMax,
                            e->display_win.yMin, e->display_win.yMax);
    fprintf(f, "\taspect ratio %f\n", e->aspect_ratio);
    fprintf(f, "\twin width %f, center %f,%f\n", e->win_width, e->win_center.v[0], e->win_center.v[1]);
    for (int i = 0; i < e->chnl_num; i ++) {
        fprintf(f, "\tchannel list %s: type %d, linear %d, xsampling %d, ysampling %d\n", e->chnls[i].name,
              e->chnls[i].pixel_type, e->chnls[i].plinear, e->chnls[i].xsampling, e->chnls[i].ysampling);
    }

}


static struct file_ops exr_ops = {
    .name = "EXR",
    .probe = EXR_probe,
    .load = EXR_load,
    .free = EXR_free,
    .info = EXR_info,
};

void EXR_init(void)
{
    file_ops_register(&exr_ops);
}
