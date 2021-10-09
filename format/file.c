#include <stdio.h>

#include "file.h"
#include "bmp.h"
#include "gif.h"

TAILQ_HEAD(file_ops_list, file_ops);

static struct file_ops_list ops_list = TAILQ_HEAD_INITIALIZER(ops_list);

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

struct pic *file_load(struct file_ops* ops, const char *filename)
{
    return ops->load(filename);
}

void file_free(struct file_ops* ops, struct pic *f)
{
    if(ops->free)
        ops->free(f);
}

void file_info(struct file_ops* ops, struct pic *p)
{
    if (ops->info)
        ops->info(stdout, p);
}

void 
file_ops_register(struct file_ops* ops)
{
    TAILQ_INSERT_TAIL(&ops_list, ops, next);
}

void
file_ops_init(void)
{
    BMP_init();
    GIF_init();
}