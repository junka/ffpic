#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <sys/queue.h>

typedef struct display{
    const char* name;
    int width;
    int height;
    void *private;
    int (*init)(const char* title, int w, int h);
    int (*uninit)(void);
    int (*draw_pixels)(void *buff, int left, int top, int width, int height, int depth, int pitch,
            uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask);
    TAILQ_ENTRY(display) next;
} display_t;


struct display* display_get(const char *name);

int display_init(struct display* d, const char* title, int w, int h);

int display_uninit(struct display* d);

int display_show(struct display* d, void *buff, int left, int top, 
        int width, int height, int depth, int pitch, uint32_t rmask, 
        uint32_t gmask, uint32_t bmask, uint32_t amask);

void display_register(struct display* d);

void display_init_all(void);

#ifdef __cplusplus
}
#endif

#endif /*_DISPLAY_H_*/
