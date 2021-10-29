
#ifndef _FILE_H_
#define _FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/queue.h>

struct file_ops {
    const char *name;
    int (*probe)(const char *filename);
    struct pic* (*load)(const char *filename);
    void (*free)(struct pic *p);
    void (*info)(FILE *f, struct pic* p);
    TAILQ_ENTRY(file_ops) next;
};

struct pic {
    void *pixels;
    int left;
    int top;
    int width;
    int height;
    int depth;
    int pitch;  //bytes per line
    uint32_t rmask;
    uint32_t gmask;
    uint32_t bmask;
    uint32_t amask;
    void *pic;
};

void file_ops_register(struct file_ops* ops);

struct file_ops* file_probe(const char *filename);
struct pic *file_load(struct file_ops* ops, const char *filename);
void file_free(struct file_ops* ops, struct pic *p);
void file_info(struct file_ops* ops, struct pic *p);

void file_ops_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_FILE_H_*/