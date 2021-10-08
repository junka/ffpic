#include <stdint.h>
#include <stdio.h>

#include "screen.h"
#include "gif.h"

extern Screen scrn;

int main(int argc, const char *argv[])
{
    int ret;
    const char *filename = argv[1];
    GIF *g = GIF_Load(filename);
    pic_register_screens();
    scrn.init(g->width, g->height);
    printf("aaaa\n");
    printf("number %d width %d, height %d\n", g->graphic_count, g->width, g->height);
    ret = pic_draw(g->graphics->image->data, g->graphics->image->width, g->graphics->image->height);
    if (ret) {
        printf("fail to draw");
    }
    scrn.uninit();
    GIF_Free(g);
    return 0;
}