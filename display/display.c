#include <stdio.h>
#include <stdint.h>

#include "display.h"
#include "bmpwriter.h"
#include "sdl_screen.h"

TAILQ_HEAD(device_list, display);

static struct device_list display_list = TAILQ_HEAD_INITIALIZER(display_list);

struct display* 
display_get(const char *name)
{
    struct display* d;
    TAILQ_FOREACH(d, &display_list, next) {
        if(strcmp(d->name, name) == 0) {
            return d;
        }
    }
    return NULL;
}

int 
display_init(struct display* d, const char* title, int w, int h)
{
    if (d->init)
        d->init(title, w, h);
    return 0;
}

int 
display_uninit(struct display* d)
{
    if (d->uninit)
        d->uninit();
    return 0;
}

int 
display_show(struct display* d, void *buff, int left, int top, 
        int width, int height, int depth, int pitch, uint32_t rmask, 
        uint32_t gmask, uint32_t bmask, uint32_t amask)
{
    if (d->draw_pixels)
        d->draw_pixels(buff, left, top, width, height, depth, pitch, rmask, gmask, bmask, amask);
    return 0;
}

void 
display_register(struct display* d)
{
    TAILQ_INSERT_TAIL(&display_list, d, next);
}

void 
display_init_all(void)
{
    bmp_writer_register();
    sdl_screen_register();
}
