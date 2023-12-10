
#ifndef _FILE_H_
#define _FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
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
    int format;
    void *pic;
};

struct pic *pic_alloc(size_t size);
void pic_free(struct pic *p);

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
