#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include "webp.h"
#include "file.h"



static int 
WEBP_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct webp_header h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (!memcmp(&h.riff, "RIFF", 4) && !memcmp(&h.webp, "WEBP", 4)) {
        return 0;
    }

    return -EINVAL;
}

static struct pic* 
WEBP_load(const char *filename)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    WEBP *w = calloc(1, sizeof(WEBP));
    p->pic = w;
    FILE *f = fopen(filename, "rb");
    fread(&w->header, sizeof(w->header), 1, f);
    uint32_t chead;
    fread(&chead, 4, 1, f);
    printf("VP8X %x, %x\n", chead, CHUNCK_HEADER("VP8X"));
    if (chead == CHUNCK_HEADER("VP8X")) {
        fseek(f, -4, SEEK_CUR);
        fread(&w->vp8x, sizeof(struct webp_vp8x), 1, f);
    } else if (chead == CHUNCK_HEADER("VP8 ")) {
        //VP8 data chuck
    } else if (chead == CHUNCK_HEADER("VP8L")) {
        //VP8 lossless chuck
    }

    fclose(f);
    return p;
}


void 
WEBP_free(struct pic * p)
{
    WEBP * w = (WEBP *)p->pic;

    free(w);
    free(p);
}


void 
WEBP_info(FILE *f, struct pic* p)
{
    WEBP * t = (WEBP *)p->pic;
    fprintf(f, "WEBP file format:\n");
    fprintf(f, "\tfile size: %d\n", t->header.file_size);
    fprintf(f, "----------------------------------\n");
    fprintf(f, "\tVP8X icc %d, alpha %d, exif %d, xmp %d, animation %d\n",
        t->vp8x.icc, t->vp8x.alpha, t->vp8x.exif_metadata, t->vp8x.xmp_metadata, t->vp8x.animation);
    fprintf(f, "\tVP8X canvas witdth %d, height %d\n", READ_UINT24(t->vp8x.canvas_width),
            READ_UINT24(t->vp8x.canvas_height));
}


static struct file_ops webp_ops = {
    .name = "WEBP",
    .probe = WEBP_probe,
    .load = WEBP_load,
    .free = WEBP_free,
    .info = WEBP_info,
};

void WEBP_init(void)
{
    file_ops_register(&webp_ops);
}