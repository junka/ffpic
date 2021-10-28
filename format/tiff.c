
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "file.h"
#include "tiff.h"
#include "deflate.h"

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
        de->value = NULL;
    } else {
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
                    *(val + i) = ((uint16_t)(de->value[i*2 + 1] << 8) | de->value[i*2]);
                    break;
                case TAG_LONG:
                case TAG_SLONG:
                    *(val +i) = ((uint32_t)(de->value[i*4 + 3]<<24) | de->value[i*4 + 2] << 16 
                            | de->value[i*4 + 1] << 8 | de->value[i*4]);
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
                // printf("TAG %d TYPE %d NUM %d OFFSET %d\n", de->tag, de->type, de->num, de->offset);
                break;
            default:
                break;
        }
    }
}

static void
read_strip(TIFF *t, struct tiff_file_directory *ifd, int id, FILE *f)
{
    int width = ((ifd->width + 3) >> 2) << 2;
    int height = ifd->height;
    int pitch = ((width * 32 + 31) >> 5) << 2;
    fseek(f, ifd->strip_offsets[id], SEEK_SET);
    uint8_t *raw = malloc(ifd->strip_byte_counts[id]);
    uint8_t *decode;
    int size, n = 0;
    fread(raw, ifd->strip_byte_counts[id], 1, f);
    if (ifd->compression == COMPRESSION_NONE) {
        decode = raw;
        raw = NULL;
    } else if (ifd->compression == COMPRESSION_LZW) {
        // if (ifd->predictor == 2)
        decode = malloc(ifd->rows_per_strip * pitch);
        int a = inflate_decode(raw, ifd->strip_byte_counts[id], decode, &size);
        free(raw);
        printf("ret %d, ori %u size %d\n", a, ifd->strip_byte_counts[id], size);
    } else if (ifd->compression == COMPRESSION_PACKBITS) {
        decode = malloc(ifd->rows_per_strip * pitch);
        int i = 0, j =0, rep;
        while (i < ifd->strip_byte_counts[id]) {
            if (raw[i] < 128) {
                rep = (raw[i] + 1);
                i ++;
                memcpy(decode + j, raw + i , rep);
                j += rep;
                i += rep;
            } else if (raw[i] > 128 ) {
                rep = 257 - raw[i];
                i ++;
                while (rep > 0) {
                    *(decode + j) = raw[i];
                    rep --;
                    j ++;
                }
                i ++;
            } else {
                i ++;
            }
        }
        free(raw);
    }

    for (int i = 0; i < ifd->rows_per_strip; i ++) {
        for (int j = 0; j < width; j ++) {
            if (ifd->metric == 2) {
                for (int k = 0; k < ifd->depth; k ++) {
                    ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4 + ifd->depth - k -1] = decode[n++];
                }
            } else if (ifd->metric == 1 && ifd->depth == 1) {
                ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4] = decode[n];
                ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4 + 1] = decode[n];
                ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4 + 2] = decode[n];
                n ++;
            } else if (ifd->metric == 0 && ifd->depth == 1) {
                //0 as white
                ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4] = 0xFF - decode[n];
                ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4 + 1] = 0xFF - decode[n];
                ifd->data[id * ifd->rows_per_strip * pitch  + i * pitch + j * 4 + 2] = 0xFF - decode[n];
                n ++;
            }
        }
    }
    free(decode);
}


static void
read_image_data(TIFF *t, FILE *f)
{
    for (int n = 0; n < t->ifd_num; n ++) {
        int width = ((t->ifd[n].width + 3) >> 2) << 2;
        int height = t->ifd[n].height;
        int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
        if(t->ifd[n].data == NULL) {
            t->ifd[n].data = malloc(pitch * height);
        }

        for (int i = 0; i < t->ifd[n].strips_num; i ++) {
            read_strip(t, &t->ifd[n], i, f);
        }
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
                    t->ifd[i].strips_num = t->ifd[i].de[j].num;
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
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].metric);
                    break;
                case TID_NEWSUBFILETYPE:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].subfile);
                    printf("subfile %d\n", t->ifd[i].subfile);
                    break;
                case TID_PREDICTOR:
                    read_int_from_de(&t->ifd[i].de[j], &t->ifd[i].predictor);
                    printf("TID_PREDICTOR %d\n", t->ifd[i].predictor);
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

    read_image_data(t, f);

    fclose(f);
    p->width = ((t->ifd[0].width + 3) >> 2) << 2;
    p->height = t->ifd[0].height;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = t->ifd[0].data;
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
        if (t->ifd[i].data)
            free(t->ifd[i].data);
    }
    if (t->ifd)
        free(t->ifd);

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
    for(int n = 0; n < t->ifd_num; n ++) {
        fprintf(f, "----------------------------------\n");
        fprintf(f, "\t%dth IFD: DE num %d\n", n, t->ifd[n].num);
        fprintf(f, "\t%dth IFD: width %d, height %d, depth %d, compression %d\n",
            n, t->ifd[n].width, t->ifd[n].height, t->ifd[n].depth, t->ifd[n].compression);
        fprintf(f, "\t%dth IFD: orientation %d, bitorder %d, pixel store as %d, metric %d\n",
            n, t->ifd[n].orientation, t->ifd[n].bit_order, t->ifd[n].pixel_store, t->ifd[n].metric);
        fprintf(f, "\t%dth IFD: bitpersample %d, %d, %d, rows_per_strip %d\n", n, t->ifd[n].bitpersample[0],
            t->ifd[n].bitpersample[1],t->ifd[n].bitpersample[2], t->ifd[n].rows_per_strip);
        fprintf(f, "\t%dth IFD: predictor %d\n", n, t->ifd[n].predictor);
        for(int i = 0; i < t->ifd[n].strips_num; i ++) {
            fprintf(f, "\tstrip offset: %d, bytes %d\n", t->ifd[n].strip_offsets[i], t->ifd[n].strip_byte_counts[i]);
        }
    }
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