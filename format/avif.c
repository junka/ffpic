#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "file.h"
#include "avif.h"


static int
AVIF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct avif_ftyp h;
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
    AVIF * a = calloc(1, sizeof(AVIF));
    struct pic *p = calloc(1, sizeof(struct pic));
    p->pic = a;
    FILE *f = fopen(filename, "rb");
    fclose(f);

    return p;
}

static void 
AVIF_free(struct pic *p)
{
    AVIF * a = (AVIF *)p->pic;
    
    free(a);
    free(p);
}


static void
AVIF_info(FILE *f, struct pic* p)
{
    fprintf(f, "AVIF file format:\n");
    fprintf(f, "-----------------------\n");
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