#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>

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

uint16_t 
read_marker_skip_null(FILE *f)
{
    uint8_t c = fgetc(f);
    if (c != 0xFF) {
        printf("marker 0x%x\n", c);
        return 0;
    }
    while (c == 0xFF) {
        c = fgetc(f);
    }
    return MARKER(c);
}

void 
idct(int input[][8], int output[][8]) {
    static double cosine[8][8] = {
        {0.707107,  0.980785,  0.923880,  0.831470,  0.707107,  0.555570,  0.382683,  0.195090},
        {0.707107,  0.831470,  0.382683, -0.195090, -0.707107, -0.980785, -0.923880, -0.555570},
        {0.707107,  0.555570, -0.382683, -0.980785, -0.707107,  0.195090,  0.923880,  0.831470},
        {0.707107,  0.195090, -0.923880, -0.555570,  0.707107,  0.831470, -0.382683, -0.980785},
        {0.707107, -0.195090, -0.923880,  0.555570,  0.707107, -0.831470, -0.382683,  0.980785},
        {0.707107, -0.555570, -0.382683,  0.980785, -0.707107, -0.195090,  0.923880, -0.831470},
        {0.707107, -0.831470,  0.382683,  0.195090, -0.707107,  0.980785, -0.923880,  0.555570},
        {0.707107, -0.980785,  0.923880, -0.831470,  0.707107, -0.555570,  0.382683, -0.195090}
    };
    for( int i  = 0; i < 8; i++ ) {
        for( int j = 0; j < 8; j++ ) {
            double tmp = 0.0;
            for( int u = 0; u < 8; u++ ) {
                for( int v = 0; v < 8; v++ ) {
                    tmp += (double)input[u][v] * cosine[i][u] * cosine[j][v];
                }
            }
            tmp /= 4;
            output[i][j] = (int)tmp;
        }
    }
}

void 
read_dqt(JPG *j, FILE *f)
{
    uint8_t c;
    uint16_t len;
    fread(&len, 2, 1, f);
    c = fgetc(f);
    uint8_t id = (c & 0xF);
    j->dqt[id].len = len;
    j->dqt[id].id = id;
    j->dqt[id].qulity = (c >> 4);
    j->dqt[id].tdata = malloc((j->dqt[id].qulity + 1) << 6);
    fread(j->dqt[id].tdata, 64, j->dqt[id].qulity + 1, f);
}

void
read_dht(JPG* j, FILE *f)
{
    struct dht d;
    fread(&d, 3, 1, f);
    uint8_t ac = d.table_class;
    uint8_t id = d.huffman_id;
    j->dht[ac][id].len = ntohs(d.len);
    j->dht[ac][id].table_class = d.table_class;
    j->dht[ac][id].huffman_id = d.huffman_id;
    fread(j->dht[ac][id].num_codecs, 16, 1, f);
    int len = 0;
    for (int i =0; i < 16; i ++) {
        len += j->dht[ac][id].num_codecs[i];
    }
    j->dht[ac][id].data = malloc(len);
    fread(j->dht[ac][id].data, len, 1, f);
}

void
convert_YCbCr_to_RGB(uint8_t src[3], uint8_t dst[3])
{
    uint8_t Y, Cr, Cb, r, g, b;
    Y = src[0];
    Cb = src[1];
    Cr = src[2];
    Cr = Cr - 128 ;
    Cb = Cb - 128 ;
    r = Y + ((Cr >> 2) + (Cr >> 3) + (Cr >> 5)) ;
    g = Y - ((Cb >> 2) + (Cb >> 4) + (Cb >> 5)) - ((Cr >> 1) + (Cr >> 3) + (Cr >> 4) + (Cr >> 5));
    b = Y + (Cb + (Cb >> 1) + (Cb >> 2) + (Cb >> 6)) ;
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
}

void 
read_compressed_image(JPG* j, FILE *f)
{
    uint8_t* compressed = malloc(j->sof.width * j->sof.height *j->sof.color_num);
    uint8_t prev , c = fgetc(f);
    int i = 0;
    do {
        prev = c;
        c = fgetc(f);
        if (prev != 0xFF)
            compressed[i++] = prev;
        else if (prev == 0xFF && c == 0) {
            compressed[i++] = 0xFF;
            prev = 0;
            c = fgetc(f);
        }
    } while( prev != 0xFF || (prev == 0xFF && c == 0));
    printf("compressed len %d\n", i);
    fseek(f, -2, SEEK_CUR);
}


void 
read_sos(JPG* j, FILE *f)
{
    fread(&j->sos, 3, 1, f);
    j->sos.sheaders = malloc(sizeof(struct scan_header) * j->sos.nums);
    fread(j->sos.sheaders, sizeof(struct scan_header), j->sos.nums, f);
    uint8_t c = fgetc(f);
    j->sos.predictor_start = c;
    c = fgetc(f);
    j->sos.predictor_end = c;
    c = fgetc(f);
    j->sos.approx_bits_h = c >> 4;
    j->sos.approx_bits_l = c & 0xF;
    read_compressed_image(j, f);
}

struct pic* 
JPG_load(const char *filename)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    JPG *j = calloc(1, sizeof(JPG));
    p->pic = j;
    FILE *f = fopen(filename, "rb");
    uint16_t soi, m, len;
    fread(&soi, 2, 1, f);
    m = read_marker_skip_null(f);
    while (m != EOI && m != 0) {
        switch(m) {
            case SOF0:
                fread(&j->sof, 8, 1, f);
                j->sof.len = ntohs(j->sof.len);
                j->sof.height = ntohs(j->sof.height);
                j->sof.width = ntohs(j->sof.width);
                j->sof.colors = malloc(j->sof.color_num*3);
                fread(j->sof.colors, 3, j->sof.color_num, f);
                break;
            case APP0:
                fread(&j->app0, 16, 1, f);
                if (j->app0.xthumbnail*j->app0.ythumbnail) {
                    j->app0.data = malloc(3*j->app0.xthumbnail*j->app0.ythumbnail);
                    fread(j->app0.data, 3, j->app0.xthumbnail*j->app0.ythumbnail, f);
                }
                break;
            // case SOF2:
            //     break;
            case DHT:
                read_dht(j, f);
                break;
            case DQT:
                read_dqt(j, f);
                break;
            case SOS:
                read_sos(j, f);
                break;
            case COM:
                fread(&j->comment, 2, 1, f);
                j->comment.len = ntohs(j->comment.len);
                j->comment.data = malloc(j->comment.len-2);
                fread(j->comment.data, j->comment.len-2, 1, f);
                break;
            default:
                fread(&len, 2, 1, f);
                fseek(f, len-2, SEEK_CUR);
                break;
        }
        m = read_marker_skip_null(f);
    }

    return p;
}

void 
JPG_free(struct pic *p)
{
    JPG *j = (JPG *)p->pic;
    if (j->comment.data)
        free(j->comment.data);
    if (j->app0.data)
        free(j->app0.data);
    if (j->sof.colors)
        free(j->sof.colors);
    for (uint8_t ac = 0; ac < 2; ac ++) {
        for (uint8_t i = 0; i < 16; i++) {
            if (j->dht[ac][i].huffman_id == i && j->dht[ac][i].table_class == ac) {
                free(j->dht[ac][i].data);
            }
        }
    }
    for (uint8_t i = 0; i < 4; i++) {
        if (j->dqt[i].id == i) {
            free(j->dqt[i].tdata);
        }
    }
    free(j);
    free(p);
}

void 
JPG_info(FILE *f, struct pic* p)
{
    JPG *j = (JPG *)p->pic;
    fprintf(f, "JPEG file format\n");
    fprintf(f, "\twidth %d, height %d\n", j->sof.width, j->sof.height);
    fprintf(f, "\tdepth %d, color_depth %d\n", j->sof.accur, j->sof.color_num);
    fprintf(f, "\tAPP0: %s version %d.%d\n", j->app0.identifier, j->app0.major, j->app0.minor);
    fprintf(f, "\tAPP0: xdensity %d, ydensity %d %s\n", j->app0.xdensity, j->app0.ydensity,
        j->app0.unit == 0 ? "pixel aspect ratio" : 
        ( j->app0.unit == 1 ? "dots per inch": (j->app0.unit == 2 ?"dots per cm":"")));
    for (uint8_t i = 0; i < 4; i++) {
        if (j->dqt[i].id == i) {
            fprintf(f, "\tDQT %d: precision %d\n", i, j->dqt[i].qulity);
        }
    }
    fprintf(f, "-----------------------\n");
    for (uint8_t ac = 0; ac < 2; ac ++) {
        for (uint8_t i = 0; i < 16; i++) {
            if (j->dht[ac][i].huffman_id == i && j->dht[ac][i].table_class == ac) {
                fprintf(f, "\t%s DHT %d:", (j->dht[ac][i].table_class == 0 ? "DC" : "AC"), i);
                int len = 0;
                for(int k= 0; k < 16; k ++) {
                    fprintf(f, " %02d",  j->dht[ac][i].num_codecs[k]);
                    len += j->dht[ac][i].num_codecs[k];
                }
                fprintf(f, "\n");
    #if 1
                fprintf(f, "\t\t");
                for(int k= 0; k < len; k ++) {
                    fprintf(f, "%x ",  j->dht[ac][i].data[k]);
                }
                fprintf(f, "\n");
    #endif
            }
        }
    }
    if (j->comment.data) {
        fprintf(f, "\tComment: %s\n", j->comment.data);
    }
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
