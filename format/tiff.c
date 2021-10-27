
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "file.h"
#include "tiff.h"

static int 
TIFF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct tiff_file_header h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.byteorder == 0x4D4D || h.byteorder == 0x4949) {
        if (h.version == 42) {
            return 0;
        }
    }

    return -EINVAL;
}

static int 
get_tag_type_size(uint16_t type)
{
    static int types[] = {
        [TAG_BYTE] = 1,
        [TAG_ASCII] = 1,
        [TAG_SBYTE] = 1,
        [TAG_UNDEFINE] = 1,
        [TAG_SHORT] = 2,
        [TAG_SSHORT] = 2,
        [TAG_LONG] = 4,
        [TAG_SLONG] = 4,
        [TAG_FLOAT] = 4,
        [TAG_RATIONAL] = 8,
        [TAG_SRATIONAL] = 8,
        [TAG_DOUBLE] = 8,
    };
    if (type > 12) {
        return 0;
    }
    return types[type];
}

static void
read_de(struct tiff_directory_entry *de, FILE *f)
{
    fread(de, 12, 1, f);
    int tlen = get_tag_type_size(de->type) * de->num;

    if (tlen <= 4 ) {
        // printf("TAG %d TYPE %d, NUM %d, off %x\n", de->tag, de->type, de->num, de->offset);
        de->value = NULL;
    } else {
        // printf("TAG %d TYPE %d, NUM %d, off %x\n", de->tag, de->type, de->num, de->offset);
        de->value = malloc(tlen);
        long pos = ftell(f);
        fseek(f, de->offset, SEEK_SET);
        fread(de->value, tlen, 1, f);
        fseek(f, pos, SEEK_SET);
    }
}

static int 
read_ifd(TIFF *t, FILE *f)
{
    t->ifd_num ++;
    if (t->ifd == NULL)
        t->ifd = calloc(1, sizeof(struct tiff_file_directory));
    else {
        t->ifd = realloc(t->ifd, t->ifd_num * sizeof(struct tiff_file_directory));
    }
    fread(&t->ifd[t->ifd_num-1].num, 2, 1, f);
    t->ifd[t->ifd_num-1].de = malloc(sizeof(struct tiff_directory_entry) * t->ifd[t->ifd_num-1].num);
    for (int i = 0; i < t->ifd[t->ifd_num-1].num; i++) {
        read_de(t->ifd[t->ifd_num-1].de + i, f);
    }
    fread(&t->ifd[t->ifd_num-1].next_offset, 4, 1, f);
    if(t->ifd[t->ifd_num-1].next_offset) {
        printf("next ifd offset %x\n", t->ifd[t->ifd_num-1].next_offset);
        fseek(f, t->ifd[t->ifd_num-1].next_offset, SEEK_SET);
        read_ifd(t, f);
    }

    return 0;
}

static void 
read_int_from_de(struct tiff_directory_entry *de, uint32_t* val)
{
    if (get_tag_type_size(de->type) * de->num > 4) {
        for (int i = 0; i < de->num; i ++) {
            switch (de->type) {
                case TAG_BYTE:
                case TAG_SBYTE:
                    *(val +i) = de->value[i];
                    break;
                case TAG_SHORT:
                case TAG_SSHORT:
                    *(val +i) = ((uint16_t)(de->value[i*2] << 8) | de->value[i*2 + 1]);
                    break;
                case TAG_LONG:
                case TAG_SLONG:
                    *(val +i) = ((uint32_t)(de->value[i*4]<<24) | de->value[i*4 + 1] << 16 
                            | de->value[i*4 + 2] << 8 | de->value[i*4 + 3]);
                    break;
                default:
                    break;
            }
        }
    } else if (de->num == 1) {
        switch (de->type) {
            case TAG_BYTE:
            case TAG_SBYTE:
                *val = (de->offset);
                break;
            case TAG_SHORT:
            case TAG_SSHORT:
                *val = (de->offset);
                break;
            case TAG_LONG:
            case TAG_SLONG:
                *val = (de->offset);
                break;
            default:
                break;
        }
    }
}

static void
read_image_data(TIFF *t, uint8_t *v)
{
    int width = ((t->ifd[0].width + 3) >> 2) << 2;
    int height = t->ifd[0].height;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
    if(t->data == NULL) {
        t->data = malloc(pitch * height);
    }
    if (t->ifd[0].compression == 1) {
        // memcpy(t->data, v, )
    }

}

static void 
tiff_compose_image_from_de(TIFF *t)
{
    for (int i = 0; i < t->ifd_num; i ++) {
        for(int j = 0; j < t->ifd[i].num; j ++) {
            switch (t->ifd[i].de[j].tag) {
                case TID_IMAGEWIDTH:
                    read_int_from_de(&t->ifd[i].de[j], &(t->ifd[i].width));
                    break;
                case TID_IMAGEHEIGHT:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].height);
                    break;
                case TID_BITSPERSAMPLE:
                    read_int_from_de(&t->ifd[i].de[j], t->ifd[i].bitpersample);
                    break;
                case TID_SAMPLESPERPIXEL:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].depth);
                    break;
                case TID_COMPRESSION:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].compression);
                    break;
                case TID_FILLORDER:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].bit_order);
                    break;
                case TID_ORIENTATION:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].orientation);
                    break;
                case TID_ROWSPERSTRIP:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].rows_per_strip);
                    break;
                case TID_STRIPOFFSETS:
                    // read_image_data(t, t->ifd[i].de[j].value);
                    t->ifd[i].strip_offsets = calloc(t->ifd[i].de[j].num, sizeof(uint32_t));
                    read_int_from_de(&t->ifd[i].de[j], t->ifd[i].strip_offsets);
                    break;
                case TID_STRIPBYTECOUNTS:
                    t->ifd[i].strip_byte_counts = malloc(t->ifd[i].num * sizeof(uint32_t));
                    read_int_from_de(&t->ifd[i].de[j], t->ifd[i].strip_byte_counts);
                    break;
                case TID_PLANARCONFIGURATION:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].pixel_store);
                    break;
                case TID_PHOTOMETRICINTERPRETATION:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].whiteblack);
                    break;
                default:
                    printf("TAG %d TYPE %d\n", t->ifd[i].de[j].tag, t->ifd[i].de[j].type);
                    break;
            }
        }
    }
}


static struct pic*
TIFF_load(const char *filename)
{
    TIFF * t = (TIFF *)calloc(1, sizeof(TIFF));
    t->ifd = NULL;
    struct pic *p = calloc(1, sizeof(struct pic));
    FILE *f = fopen(filename, "rb");
    p->pic = t;
    p->depth = 32;
    fread(&t->ifh, sizeof(struct tiff_file_header), 1, f);
    fseek(f, t->ifh.start_offset, SEEK_SET);
    read_ifd(t, f);

    tiff_compose_image_from_de(t);
    fclose(f);
    p->width = ((t->ifd[0].width + 3) >> 2) << 2;
    p->height = t->ifd[0].height;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = t->data;

    return p;
}

static void 
TIFF_free(struct pic * p)
{
    TIFF * t = (TIFF *)p->pic;

    for(int i = 0; i < t->ifd_num; i ++){
        for(int j = 0; j < t->ifd[i].num; j++) {
            if (t->ifd[i].de[j].value)
                free(t->ifd[i].de[j].value);
        }
        free(t->ifd[i].de);
        free(t->ifd[i].strip_offsets);
        free(t->ifd[i].strip_byte_counts);
    }
    if (t->ifd)
        free(t->ifd);
    if (t->data)
        free(t->data);

    free(t);
    free(p);
}

static void
TIFF_info(FILE *f, struct pic* p)
{
    TIFF * t = (TIFF *)p->pic;
    fprintf(f, "TIFF file format:\n");
    fprintf(f, "\tbyte order: %s\n", t->ifh.byteorder == 0x4D4D ? 
                    "big endian": "little endian");
    fprintf(f, "\tIFD num: %d\n", t->ifd_num);
    fprintf(f, "----------------------------------\n");
    fprintf(f, "\tfirst IFD: DE num %d\n", t->ifd->num);
    fprintf(f, "\tfirst IFD: width %d, height %d, depth %d, compression %d\n",
         t->ifd->width, t->ifd->height, t->ifd->depth, t->ifd->compression);
    fprintf(f, "\tfirst IFD: orientation %d, bitorder %d, pixel store as %d, white %d\n",
         t->ifd->orientation, t->ifd->bit_order, t->ifd->pixel_store, t->ifd->whiteblack);
    fprintf(f, "\tfirst IFD: bitpersample %d, %d, %d, rows_per_strip %d\n", t->ifd->bitpersample[0],
         t->ifd->bitpersample[1],t->ifd->bitpersample[2], t->ifd->rows_per_strip);
    fprintf(f, "\n");
}

static struct file_ops png_ops = {
    .name = "TIFF",
    .probe = TIFF_probe,
    .load = TIFF_load,
    .free = TIFF_free,
    .info = TIFF_info,
};

void 
TIFF_init(void)
{
    file_ops_register(&png_ops);
}