
#ifndef _FILE_H_
#define _FILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <sys/queue.h>

#include "queue.h"

struct file_ops {
    const char *name;
    const char *alias;
    int (*probe)(const char *filename);
    struct pic* (*load)(const char *filename, int skip_flag);
    void (*free)(struct pic *p);
    void (*info)(FILE *f, struct pic* p);
    void (*encode)(struct pic *p, const char *fname);
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

/**
 * The function "file_load" loads a picture file using the specified file
 * operations and returns a pointer to the loaded picture.
 *
 * @param ops The `ops` parameter is a pointer to a struct `file_ops`. This
 * struct likely contains function pointers to various file operations, such as
 * `load`, `save`, etc.
 * @param filename A string representing the name of the file to be loaded.
 *
 * @return a pointer to a struct pic.
 *         return NULL if we have multiple pics and put all pics in a queue
 */
struct pic *file_load(struct file_ops *ops, const char *filename, int skip_flag);
void file_free(struct file_ops* ops, struct pic *p);
void file_info(struct file_ops *ops, struct pic *p);
struct file_ops *file_find_codec(const char *name);

//
struct pic *file_dequeue_pic(void);
bool file_enqueue_pic(struct pic *p);

void file_ops_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_FILE_H_*/
