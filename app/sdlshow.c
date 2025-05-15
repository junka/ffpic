#include <stdint.h>
#include <stdio.h>
#include <unistd.h> 
#include <fcntl.h>

#include "colorspace.h"
#include "display.h"
#include "sdl_screen.h"
#include "file.h"
#include "gif.h"
#include "vlog.h"
#include "accl.h"

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480

#if 0
void *
display_main(void *arg)
{
    int ret;
    struct pic *p;
    char title[128];
    //char *filename = (char*)arg;
    struct file_ops *ops = (struct file_ops *)arg;
    // snprintf(title, 128, "%s (%d * %d)", "display", p->width, p->height);

    struct display *d = display_get("SDL2");
    // if (p->width < 180 || p->height < 60) {
    //     display_init(d, title, 480, 360);
    // } else {
    //     display_init(d, title, p->width, p->height);
    // }

    // int left, top;
    // top = p->top;
    // left = p->left;
    // if (p->width < 180 || p->height < 60) {
    //     top = top ? top : 360 / 2 - p->height / 2;
    //     left = left ? left : 480 / 2 - p->width / 2;
    // }
    // ret = display_show(d, p->pixels, left, top, p->width, p->height, p->depth,
    //                    p->pitch, p->format);
    // if (ret) {
    //     printf("fail to draw\n");
    //     goto quit;
    // }

    while ((p = file_dequeue_pic())) {

        int left, top;
        top = p->top;
        left = p->left;
        if (p->width < 180 || p->height < 60) {
            top = top ? top : 360 / 2 - p->height / 2;
            left = left ? left : 480 / 2 - p->width / 2;
        }
        pic_poll_block(true);
        SDL_Delay(10);
        display_show(d, p->pixels, left, top, p->width, p->height, p->depth,
                    p->pitch, p->format);
        if (ret) {
            printf("fail to draw\n");
            goto quit;
        }
        file_free(ops, p);
        // file_enqueue_pic(p);
    }
quit:
    display_uninit(d);
    if (p) {
        file_free(ops, p);
    }
    return NULL;
}
#endif

int 
main(int argc, const char *argv[])
{
    int ret;
    if (argc < 2) {
        printf("Please input a valid picture path\n");
        return -1;
    }
    const char *filename = argv[1];
    if (0 != access(filename, F_OK|R_OK)) {
        printf("File not exist or can not read\n");
        return -1;
    }
    char title[128];
#ifndef NDEBUG
    FILE *logf = fopen("picinfo.log", "w+");
    vlog_init();
    vlog_openlog_stream(logf);
#endif
    file_ops_init();
    accl_ops_init();
    sdl_screen_register();
    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not support\n");
        return -1;
    }
    struct display *d = display_get("SDL2");
    struct pic *p = file_load(ops, filename, 0);
    if (!p) {
        p = file_dequeue_pic();
        if (!p) {
            printf("No decoded image\n");
            goto quit;
        }
        file_enqueue_pic(p);
    }

#if 0
    // pthread_t tid;
    // pthread_create(&tid, NULL, display_main, (void *)ops);
    // pthread_join(tid, NULL);
#endif

    snprintf(title, 128, "%s (%d * %d)", filename, p->width, p->height);

    if (p->width < 180 || p->height < 60) {
        display_init(d, title, 480, 360);
    } else {
        display_init(d, title, p->width, p->height);
    }

    int left, top;
    top = p->top;
    left = p->left;
    if (p->width < 180 || p->height < 60) {
        top = top ? top : 360/2 - p->height / 2;
        left = left ? left : 480 /2 - p->width / 2;
    }
    ret = display_show(d, p->pixels, left, top, p->width, p->height, p->depth, p->pitch, p->format);
    if (ret) {
        printf("fail to draw\n");
        goto quit;
    }
    while ((p = file_dequeue_pic())) {
        pic_poll_block(true);
        SDL_Delay(10);
        display_show(d, p->pixels, left, top, p->width, p->height, p->depth,
                     p->pitch, p->format);
        if (ret) {
          printf("fail to draw\n");
          goto quit;
        }
        file_free(ops, p);
        // file_enqueue_pic(p);
    }

    accl_ops_uninit();
    pic_poll_block(false);

quit:
    display_uninit(d);
    if (p) {
        file_free(ops, p);
    }
#ifndef NDEBUG
    vlog_uninit();
#endif
    return 0;
}
