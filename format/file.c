#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "file.h"
#include "bmp.h"
#include "gif.h"
#include "png.h"
#include "queue.h"
#include "tiff.h"
#include "pnm.h"
#include "jpg.h"
#include "tga.h"
#include "webp.h"
#include "heif.h"
#include "ico.h"
#include "jp2.h"
#include "exr.h"
#include "psd.h"
#include "svg.h"
#include "avif.h"
#include "bpg.h"

TAILQ_HEAD(file_ops_list, file_ops);

static struct file_ops_list ops_list = TAILQ_HEAD_INITIALIZER(ops_list);

static struct ring_queue *rq = NULL;

struct file_ops* 
file_probe(const char *filename)
{
    struct file_ops* ops;
    TAILQ_FOREACH(ops, &ops_list, next) {
        if(ops->probe(filename) == 0)
            return ops;
    }
    return NULL;
}

struct pic * file_load(struct file_ops *ops, const char *filename, int skip_flag) {
    rq = ring_alloc(64);
    return ops->load(filename, skip_flag);
}

struct pic *
file_dequeue_pic(void)
{
    struct pic *p = (struct pic *)ring_dequeue(rq);
    return p;
}

bool file_enqueue_pic(struct pic *p) {
    return ring_enqueue(rq, (void *)p);
}

void 
file_free(struct file_ops* ops, struct pic *f)
{
    if(ops->free)
        ops->free(f);
}

void 
file_info(struct file_ops* ops, struct pic *p)
{
    if (ops->info)
        ops->info(stderr, p);
}

void 
file_ops_register(struct file_ops* ops)
{
    TAILQ_INSERT_TAIL(&ops_list, ops, next);
}

struct file_ops *
file_find_codec(const char *name)
{
    struct file_ops *ops;
    TAILQ_FOREACH(ops, &ops_list, next) {
        if (strcasecmp(ops->name, name) == 0 || (ops->alias && strcasecmp(ops->alias, name) == 0)) {
            return ops;
        }
    }
    return NULL;
}

void
file_ops_init(void)
{
    BMP_init();
    GIF_init();
    PNG_init();
    TIFF_init();
    PNM_init();
    JPG_init();
    HEIF_init();
    WEBP_init();
    BPG_init();
    TGA_init();
    ICO_init();
    JP2_init();
    EXR_init();
    PSD_init();
    SVG_init();
    AVIF_init();
}

struct pic *pic_alloc(size_t size)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    p->pic = calloc(1, size);
    p->refcnt = 0;
    return p;
}

struct pic *pic_ref(struct pic * p)
{
    p->refcnt++;
    return p;
}

void pic_free(struct pic *p)
{
    if (p->refcnt == 0) {
        free(p->pic);
        free(p);
    } else {
        p->refcnt--;
    }
}
