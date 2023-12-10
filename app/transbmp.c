#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "file.h"
#include "display.h"
#include "bmpwriter.h"

int main(int argc, char *argv[])
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
    char bmpfile[128];
    file_ops_init();
    bmp_writer_register();
    struct file_ops *ops = file_probe(filename);
    if (ops == NULL) {
        printf("file format is not support\n");
        return -1;
    }
    int left = 0, top = 0;
    struct pic *p = file_load(ops, filename);
    snprintf(bmpfile, 128, "%s (%d * %d)", filename, p->width, p->height);

    struct display *d = display_get("bmpwriter");
    display_init(d, bmpfile, p->width, p->height);
    ret = display_show(d, p->pixels, left, top, p->width, p->height, p->depth,
                 p->pitch, p->format);
    if (ret) {
        printf("fail to save to bmp\n");
        goto quit;
    }
quit:
    display_uninit(d);
    file_free(ops, p);
    return 0;

}