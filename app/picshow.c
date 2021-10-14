#include <stdint.h>
#include <stdio.h>

#include "screen.h"
#include "file.h"

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

    struct pic *g = ops->load(filename);
    snprintf(title, 128, "%s (%d * %d)", filename, g->width, g->height);
    if (g->width < 180 || g->height < 60) {
        scrn.init(title, 480, 360);
    } else {
        scrn.init(title, g->width, g->height);
    }
    ret = pic_draw(g->pixels, g->width, g->height, g->depth, g->pitch, g->rmask, g->gmask, g->bmask, g->amask);
    if (ret) {
        printf("fail to draw\n");
    }
    scrn.uninit();
    ops->free(g);
    return 0;
}