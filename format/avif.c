#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "vlog.h"
#include "file.h"
#include "avif.h"

VLOG_REGISTER(avif, DEBUG);

static int
AVIF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct ftyp_box h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_brand == TYPE2UINT("ftyp")) {
        if(h.minor_version == TYPE2UINT("avif"))
        return 0;
    }
    return -EINVAL;
}

static struct pic* 
AVIF_load(const char *filename) {
    struct pic* p = HEIF_load(filename);
    return p;
}

static void 
AVIF_free(struct pic *p)
{
    AVIF * a = (AVIF *)p->pic;
    pic_free(p);
}


static void
AVIF_info(FILE *f, struct pic* p)
{
    AVIF * h = (AVIF *)p->pic;
    fprintf(f, "AVIF file format:\n");
    fprintf(f, "-----------------------\n");
    char *s1 = UINT2TYPE(h->ftyp.minor_version);
    char *s2 = UINT2TYPE(h->ftyp.compatible_brands[1]);
    fprintf(f, "\tbrand: %s, compatible %s\n", s1, s2);
    free(s1);
    free(s2);
    fprintf(f, "\theight: %d, width: %d\n", p->height, p->width);
}



static struct file_ops avif_ops = {
    .name = "AVIF",
    .probe = AVIF_probe,
    .load = AVIF_load,
    .free = AVIF_free,
    .info = AVIF_info,
};

void AVIF_init(void)
{
    file_ops_register(&avif_ops);
}