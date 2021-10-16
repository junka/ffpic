#include <stdint.h>
#include <stdio.h>
#include <unistd.h> 

#include "screen.h"
#include "file.h"
#include "gif.h"

extern Screen scrn;

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
    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not support\n");
        return -1;
    }
    int left, top;
    struct pic *p = ops->load(filename);
    snprintf(title, 128, "%s (%d * %d)", filename, p->width, p->height);
    if (p->width < 180 || p->height < 60) {
        scrn.init(title, 480, 360);
    } else {
        scrn.init(title, p->width, p->height);
    }
    top = p->top;
    left = p->left;
    if (p->width < 180 || p->height < 60) {
        top = top ? top : 360/2 - p->height/2;
        left = left ? left : 480 /2 - p->width/2;
    }
    ret = pic_draw(p->pixels, left, top, p->width, p->height, p->depth,
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
                pic_draw(g->graphics[i].image->data, g->graphics[i].image->image_dsc.left, 
                        g->graphics[i].image->image_dsc.top, g->graphics[i].image->image_dsc.width,
                        g->graphics[i].image->image_dsc.height, p->depth,
                        (((g->graphics[i].image->image_dsc.width * p->depth + p->depth -1)>>5)<<2), 0, 0, 0, 0xFF);
            }
        }
    }

    pic_poll_block(false);

quit:
    scrn.uninit();
    ops->free(p);
    return 0;
}