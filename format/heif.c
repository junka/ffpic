#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "heif.h"
#include "file.h"

static uint32_t types[] = {
    
};

static int
HEIF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct heif_ftyp h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_brand == TYPE2UINT("ftyp")) {
        if(h.minor_version == TYPE2UINT("mif1"))
        return 0;
    }

    return -EINVAL;
}


static void
read_box(FILE *f, int tlen)
{
    uint32_t len;
    uint32_t type;
    fread(&len, 4, 1, f);
    fread(&type, 4, 1, f);
    len = ntohl(len);
    printf("len %d , total len %d, %s\n",len, tlen, (uint8_t *)&type);
    fseek(f, len - 4, SEEK_CUR);
}

static void
read_meta_box(FILE *f, int tlen)
{
    
}

static void
read_filetype_box(FILE *f)
{
    struct heif_ftyp typ;
    fread(&typ, 12, 1, f);
    typ.len = ntohl(typ.len);
    typ.extended_compatible = malloc(typ.len - 12);
    fread(typ.extended_compatible, typ.len - 12, 1, f);
    read_box(f, typ.len - 12);
    
}

static struct pic* 
HEIF_load(const char *filename) {
    HEIF * h = calloc(1, sizeof(HEIF));
    struct pic *p = calloc(1, sizeof(struct pic));
    p->pic = h;
    FILE *f = fopen(filename, "rb");
    read_filetype_box(f);

    fclose(f);

    return p;
}

static void 
HEIF_free(struct pic *p)
{
    HEIF * h = (HEIF *)p->pic;
    
    free(h);
    free(p);
}


static void
HEIF_info(FILE *f, struct pic* p)
{
    fprintf(f, "HEIF file format:\n");
    fprintf(f, "-----------------------\n");
}




static struct file_ops heif_ops = {
    .name = "HEIF",
    .probe = HEIF_probe,
    .load = HEIF_load,
    .free = HEIF_free,
    .info = HEIF_info,
};

void HEIF_init(void)
{
    file_ops_register(&heif_ops);
}



