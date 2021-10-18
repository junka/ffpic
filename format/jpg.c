#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "file.h"
#include "jpg.h"

int JPG_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    uint16_t soi, eoi;
    int len = fread(&soi, 2, 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fseek(f, -2, SEEK_END);
    len = fread(&eoi, 2, 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (SOI == soi && eoi == EOI)
        return 0;
    
    return -EINVAL;
}

struct pic* 
JPG_load(const char *filename)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    JPG *j = calloc(1, sizeof(JPG));
    p->pic = j;
    return p;
}

void 
JPG_free(struct pic *p)
{
    JPG *j = (JPG *)p->pic;
    free(j);
    free(p);
}

void 
JPG_info(FILE *f, struct pic* p)
{
    JPG *j = (JPG *)p->pic;
    fprintf(f, "JPEG file format\n");
}


static struct file_ops jpg_ops = {
    .name = "JPG",
    .probe = JPG_probe,
    .load = JPG_load,
    .free = JPG_free,
    .info = JPG_info,
};

void 
JPG_init(void)
{
    file_ops_register(&jpg_ops);
}
