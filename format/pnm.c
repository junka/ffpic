#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "file.h"
#include "pnm.h"

int 
PNM_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct file_header sig;
    size_t len = fread(&sig, sizeof(struct file_header), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if(sig.magic != 'P') {
        return -EINVAL;
    }
    for (uint8_t i = 1; i <= 7; i++) {
        if (sig.version == i + '0') {
            return 0;
        }
    }
    return -EINVAL;
}

 
uint8_t 
read_skip_delimeter(FILE *f)
{
    uint8_t c = fgetc(f);
    while (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
        c = fgetc(f);
    }
    return c;
}


int 
read_int_till_delimeter(FILE *f) {
    int r = 0;
    uint8_t c = read_skip_delimeter(f);
    while(c != ' ' && c != '\n' && c != '\t' && c != '\r') {
        r = r * 10;
        r += c - '0';
        c = fgetc(f);
    }
    return r;
}

void 
read_ppm_bin_data(PNM* m, FILE *f)
{
    int off = ftell(f);
    fseek(f, 0, SEEK_END);
    int last = ftell(f);
    fseek(f, off, SEEK_SET);
    m->data = malloc((last - off)/3 *4);

    //RGB color need do reorder for display
    for(int i = 0; i < last-off / 3; i++) {
        fread(m->data + 4*i + 2, 1, 1, f);
        fread(m->data + 4*i + 1, 1, 1, f);
        fread(m->data + 4*i, 1, 1, f);
    }
}

struct pic* PNM_load(const char *filename)
{
    struct pic * p = calloc(sizeof(struct pic), 1);
    PNM *m = (PNM *)calloc(sizeof(PNM), 1);
    p->pic = m;
    FILE *f = fopen(filename, "rb");
    fread(&m->pn, sizeof(struct file_header), 1, f);
    m->width = read_int_till_delimeter(f);
    m->height = read_int_till_delimeter(f);
    uint8_t v = m->pn.version - '0';
    if(v == 2 || v == 5 || v == 3 || v == 6) {
        m->color_size = read_int_till_delimeter(f);
    }
    //ppm binary
    if (v == 6) {
        read_ppm_bin_data(m, f);
    }
    fclose(f);
    p->width = m->width;
    p->height = m->height;
    p->depth = 32;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = m->data;
    return p;
}


void PNM_free(struct pic *p)
{
    PNM *m = (PNM *)p->pic;
    free(m->data);
    free(m);
    free(p);
}


void 
PNM_info(FILE *f, struct pic* p)
{
    PNM *m = (PNM *)p->pic;
    fprintf(f, "PNM file formart:\n");
    fprintf(f, "P%c:\n", m->pn.version);
    fprintf(f, "-------------------------\n");
    fprintf(f, "\twidth %d: height %d\n", m->width, m->height);
    fprintf(f, "\tcolor max value %d\n", m->color_size);
}


static struct file_ops pnm_ops = {
    .name = "PNM",
    .probe = PNM_probe,
    .load = PNM_load,
    .free = PNM_free,
    .info = PNM_info,
};

void 
PNM_init(void)
{
    file_ops_register(&pnm_ops);
}