#include <stdint.h>
#include <stdio.h>
#include <unistd.h> 

#include "display.h"
#include "sdl_screen.h"
#include "file.h"
#include "gif.h"

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480

int 
main(int argc, const char *argv[])
{
    int ret;
    if (argc < 2) {
        printf("Please input a valid picture path\n");
        return -1;
    }
    const char *filename = argv[1];
    char title[128];

    file_ops_init();
    display_init_all();
    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not support\n");
        return -1;
    }
    int left, top;
    struct pic *p = file_load(ops, filename);
    snprintf(title, 128, "%s (%d * %d)", filename, p->width, p->height);

    struct display *d = display_get("SDL2");
    if (p->width < 180 || p->height < 60) {
        display_init(d, title, 480, 360);
    } else {
        display_init(d, title, p->width, p->height);
    }
    top = p->top;
    left = p->left;
    if (p->width < 180 || p->height < 60) {
        top = top ? top : 360/2 - p->height/2;
        left = left ? left : 480 /2 - p->width/2;
    }
    ret = display_show(d, p->pixels, left, top, p->width, p->height, p->depth,
                 p->pitch, p->rmask, p->gmask, p->bmask, p->amask);
    if (ret) {
        printf("fail to draw\n");
        goto quit;
    }
    if (!strcmp(ops->name, "GIF")) {
        GIF *g = (GIF *)p->pic;
        if (g->graphic_count > 1) {
            for(int i = 1; i < g->graphic_count; i ++) {
                pic_poll_block(true);
                SDL_Delay(g->graphics[i].control->delay_time * 10);
                display_show(d, g->graphics[i].image->data, g->graphics[i].image->image_dsc.left, 
                        g->graphics[i].image->image_dsc.top, g->graphics[i].image->image_dsc.width,
                        g->graphics[i].image->image_dsc.height, p->depth,
                        (((g->graphics[i].image->image_dsc.width * p->depth + p->depth -1)>>5)<<2), 0, 0, 0, 0xFF);
            }
        }
    }

    pic_poll_block(false);

quit:
    display_uninit(d);
    file_free(ops, p);
    return 0;
}