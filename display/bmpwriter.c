#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "display.h"
#include "bmpwriter.h"
#include "utils.h"


struct bmp_private {
    FILE *fp;
    uint8_t *data;
};

struct bmp_private bmp_pri;


uint8_t* alloc_bmp_with_head(uint32_t w, uint32_t h)
{
    struct bmp_header *bmp_p;
    long file_length = (w * h * (32>>3));
    uint8_t* d_ptr = (uint8_t*)malloc(file_length + 54);

    bmp_p = (struct bmp_header *)d_ptr;
    bmp_p->file_type = 0x4D42;
    bmp_p->file_size = 54+ w *h *4;
    bmp_p->reserved1 = 0x0;
    bmp_p->reserved2 = 0x0;
    bmp_p->offset = 0x36;

    //bmp info head
    bmp_p->biSize = 0x28;
    bmp_p->biWidth = w;
    bmp_p->biHeight = -h;
    bmp_p->biPlanes = 0x01;
    bmp_p->biBitCount = 32;
    bmp_p->biCompression = 0;
    bmp_p->biSizeImage = w*h*4;
    bmp_p->biXPelsPerMeter = 0x60;
    bmp_p->biYPelsPerMeter = 0x60;
    bmp_p->biClrUsed = 2;
    bmp_p->biClrImportant = 0;
    
    return d_ptr;
}


int
bmp_writer_init(const char* title, int w, int h)
{
    char name[128];
    snprintf(name, 128,  "%s.bmp", title);
    bmp_pri.fp = fopen(name, "w");
    bmp_pri.data = alloc_bmp_with_head(w, h);

    return 0;
}

int 
bmp_writer_puts(void *buff, int left UNUSED, int top UNUSED, int width, int height, int depth, int pitch UNUSED,
            uint32_t rmask UNUSED, uint32_t gmask UNUSED, uint32_t bmask UNUSED, uint32_t amask UNUSED)
{

    FILE *fd = bmp_pri.fp;
    long file_length = (width * height * (depth>>3)) + 54;
    uint8_t *file_p = bmp_pri.data;
    uint8_t *file_p_tmp = NULL;
    uint8_t *b = (uint8_t *)buff;

    file_p_tmp = file_p;
    file_p_tmp += 54;
    for (int i = 0; i < height; i ++) {
        for (int j = 0; j < width; j ++) {
            memcpy(file_p_tmp, b, 4);
            b += 4;
            file_p_tmp += 4;
        }
    }
    fwrite(file_p, file_length, 1, fd);
    return 0;
}

static int 
bmp_writer_uninit(void)
{
    free(bmp_pri.data);
    fclose(bmp_pri.fp);
    return 0;
}

display_t bmp_writer = {
    .name = "bmpwriter",
    .width = 0,
    .height = 0,
    .init = bmp_writer_init,
    .uninit = bmp_writer_uninit,
    .draw_pixels = bmp_writer_puts,
};

void bmp_writer_register(void)
{
    display_register(&bmp_writer);
}
