#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "file.h"
#include "pnm.h"

static int 
PNM_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct file_header sig;
    size_t len = fread(&sig, sizeof(struct file_header), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if(sig.magic != 'P') {
        return -EINVAL;
    }
    for (uint8_t i = 1; i <= 7; i++) {
        if (sig.version == i + '0') {
            return 0;
        }
    }
    return -EINVAL;
}

 
static uint8_t 
read_skip_delimeter(FILE *f)
{
    uint8_t c = fgetc(f);
    while (feof(f) || c == ' ' || c == '\n' || c == '\t' || c == '\r') {
        c = fgetc(f);
    }
    return c;
}


static int 
read_int_till_delimeter(FILE *f) {
    int r = 0;
    uint8_t c = read_skip_delimeter(f);
    while(!feof(f) && c != ' ' && c != '\n' && c != '\t' && c != '\r') {
        r = r * 10;
        r += c - '0';
        c = fgetc(f);
    }
    return r;
}

static uint8_t
read_ascii_one(FILE *f) {
    uint8_t c = read_skip_delimeter(f);
    return c - '0';
}

static void 
read_ppm_bin_data(PNM* m, FILE *f)
{
    int off = ftell(f);
    fseek(f, 0, SEEK_END);
    int last = ftell(f);
    fseek(f, off, SEEK_SET);

    //RGB color need do reorder for display
    for(int i = 0; i < last-off / 3; i++) {
        fread(m->data + 4*i + 2, 1, 1, f);
        fread(m->data + 4*i + 1, 1, 1, f);
        fread(m->data + 4*i, 1, 1, f);
    }
}

static void 
read_pgm_bin_data(PNM* m, FILE *f)
{
    int off = ftell(f);
    fseek(f, 0, SEEK_END);
    int last = ftell(f);
    fseek(f, off, SEEK_SET);
    if (last - off < m->width * m->height) {
        printf("corrupt file size \n");
        return;
    }
    int i =0 , j =0;
    int pitch = ((m->width * 32 + 31) >> 5) << 2;
    while(!feof(f)) {
        uint8_t c = fgetc(f);
        m->data[j * pitch + 4 * i + 2] = c;
        m->data[j * pitch + 4 * i + 1] = c;
        m->data[j * pitch + 4 * i] = c;
        i ++;
        if(i == m->width) {
            i = 0;
            j ++;
            if (j == m->height) {
                return;
            }
        }
    }
}

static void 
read_pbm_bin_data(PNM* m, FILE *f)
{
    int off = ftell(f);
    fseek(f, 0, SEEK_END);
    int last = ftell(f);
    fseek(f, off, SEEK_SET);
    int n = 0;
    for(int i = 0; i < last-off; i++) {
        uint8_t c = fgetc(f);
        for (int j = 7; j >= 0; j --) {
            if (n < m->width) {
                if (((c >> j) & 0x01) == 0) {
                    m->data[32*i + 4*(7-j) + 2] = 0xFF;
                    m->data[32*i + 4*(7-j) + 1] = 0xFF;
                    m->data[32*i + 4*(7-j)] = 0xFF;
                } else {
                    m->data[32*i + 4*(7-j) + 2] = 0;
                    m->data[32*i + 4*(7-j) + 1] = 0;
                    m->data[32*i + 4*(7-j)] = 0;
                }
            } 
            n ++;
            if(n >= m->width && j > 0)
            {
                //do noting to skip bits
            } 
            else {
                n = 0;
            }

        }
    }
    fread(m->data , 1, last - off, f);
}

uint8_t 
read_skip_comments_line(FILE *f)
{
    int c = read_skip_delimeter(f);
    bool comment = false;
    if (c == '#') {
        comment = true;
    }
    while (comment && !feof(f)) {
        c = fgetc(f);
        if (c == '\n') {
            comment = false;
            c = read_skip_comments_line(f);
        }
    }
    return c;
}

static void
read_pbm_ascii_data(PNM *m, FILE *f)
{
    int i = 0, j = 0;
    int pitch = ((m->width * 32 + 31) >> 5) << 2;
    while(!feof(f)) {
        int c = read_ascii_one(f);
        if (c == 0) {
                m->data[j * pitch + i * 4 + 2] = 0xFF;
                m->data[j * pitch + i * 4 + 1] = 0xFF;
                m->data[j * pitch + i * 4] = 0xFF;
        }
        i ++;
        if(i == m->width) {
            i = 0;
            j ++;
            if (j == m->height)
                return;
        }
    }
}


static void
read_pgm_ascii_data(PNM *m, FILE *f)
{
    int i = 0, j = 0;
    int pitch = ((m->width * 32 + 31) >> 5) << 2;
    while(!feof(f)) {
        int grey = read_int_till_delimeter(f);
        m->data[j * pitch + i * 4 + 2] = grey;
        m->data[j * pitch + i * 4 + 1] = grey;
        m->data[j * pitch + i * 4] = grey;
        i ++;
        if(i == m->width) {
            i = 0;
            j ++;
            if (j == m->height) {
                return;
            }
        }
    }
}

static void
read_ppm_ascii_data(PNM *m, FILE *f)
{
    int i = 0, j = 0;
    int pitch = ((m->width * 32 + 31) >> 5) << 2;
    while (!feof(f)) {
        read_skip_comments_line(f);
        fseek(f, -1, SEEK_CUR);
        m->data[j * pitch + i * 4 + 2] = read_int_till_delimeter(f);
        m->data[j * pitch + i * 4 + 1] = read_int_till_delimeter(f);
        m->data[j * pitch + i * 4] = read_int_till_delimeter(f);
        i ++;
        if(i == m->width) {
            i = 0;
            j ++;
            if (j == m->height) {
                return;
            }
        }
    }
}

static int 
read_string_till_delimeter(FILE *f, char *str) {
    int r = 0;
    uint8_t c = read_skip_delimeter(f);
    while(!feof(f) && c != ' ' && c != '\n' && c != '\t' && c != '\r') {
        *str++ = c;
        c = fgetc(f);
    }
    *str = '\0';
    return 0;
}
static void
read_pam_attribe(PNM *m , FILE *f)
{
    char str[32];
    read_string_till_delimeter(f, str);
    while (strcmp(str, "ENDHDR")) {
        if (!strcmp(str, "WIDTH")) {
            m->width = read_int_till_delimeter(f);
        } else if (!strcmp(str, "HEIGHT")) {
            m->height = read_int_till_delimeter(f);
        } else if (!strcmp(str, "DEPTH")) {
            m->depth = read_int_till_delimeter(f);
        } else if (!strcmp(str, "MAXVAL")) {
            m->color_size = read_int_till_delimeter(f);
        } else if (!strcmp(str, "TUPLTYPE")) {
            read_string_till_delimeter(f, str);
            if (!strcmp(str, "BLACKANDWHITE")) {
                m->tupe_type = BLACKANDWHITE;
            } else if (!strcmp(str, "GRAYSCALE")) {
                m->tupe_type = GRAYSCALE;
            } else if (!strcmp(str, "RGB")) {
                m->tupe_type = RGB;
            } else if (!strcmp(str, "BLACKANDWHITE_ALPHA")) {
                m->tupe_type = BLACKANDWHITE_ALPHA;
            } else if (!strcmp(str, "GRAYSCALE_ALPHA")) {
                m->tupe_type = GRAYSCALE_ALPHA;
            } else if (!strcmp(str, "RGB_ALPHA")) {
                m->tupe_type = RGB_ALPHA;
            }
        }
        read_string_till_delimeter(f, str);
    }
}

static void
read_pam_data(PNM *m, FILE *f)
{
    if(m->tupe_type == BLACKANDWHITE && m->depth == 1) {
        read_pbm_bin_data(m, f);
    } else if (m->tupe_type == RGB && m->depth == 3) {
        read_ppm_bin_data(m, f);
    } else if (m->tupe_type == RGB_ALPHA && m->depth == 4)
    {

    }
}

static struct pic* 
PNM_load(const char *filename)
{
    struct pic * p = calloc(sizeof(struct pic), 1);
    PNM *m = (PNM *)calloc(sizeof(PNM), 1);
    p->pic = m;
    FILE *f = fopen(filename, "rb");
    fread(&m->pn, sizeof(struct file_header), 1, f);
    fgetc(f);
    uint8_t v = m->pn.version - '0';

    if (v == 7)
    {
        read_pam_attribe(m, f);
    }
    else
    {
        // ascii format may get comments
        if(v == 1 || v == 2 || v ==3 ) {
            read_skip_comments_line(f);
            fseek(f, -1, SEEK_CUR);
        }
        m->width = read_int_till_delimeter(f);
        if (v == 1 || v == 2 || v ==3 ) {
            read_skip_comments_line(f);
            fseek(f, -1, SEEK_CUR);
        }
        m->height = read_int_till_delimeter(f);
        if (v == 1 || v == 2 || v ==3 ) {
            read_skip_comments_line(f);
            fseek(f, -1, SEEK_CUR);
        }
        if (v == 2 || v == 5 || v == 3 || v == 6) {
            m->color_size = read_int_till_delimeter(f);
        }
    }
    p->width = m->width;
    p->height = m->height;
    p->depth = 32;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    m->data = malloc(p->pitch * p->height);

    switch (v) {
        case 7:
            read_pam_data(m, f);
            break;
        case 6:
            read_ppm_bin_data(m, f);
            break;
        case 5:
            read_pgm_bin_data(m, f);
            break;
        case 4:
            read_pbm_bin_data(m, f);
            break;
        case 3:
            read_ppm_ascii_data(m, f);
            break;
        case 2:
            read_pgm_ascii_data(m, f);
            break;
        case 1:
            read_pbm_ascii_data(m, f);
            break;
        default:
            break;
    }
    fclose(f);
    p->pixels = m->data;
    return p;
}


static void 
PNM_free(struct pic *p)
{
    PNM *m = (PNM *)p->pic;
    free(m->data);
    free(m);
    free(p);
}


static void 
PNM_info(FILE *f, struct pic* p)
{
    PNM *m = (PNM *)p->pic;
    fprintf(f, "PNM file formart:\n");
    fprintf(f, "P%c:\n", m->pn.version);
    fprintf(f, "-------------------------\n");
    fprintf(f, "\twidth %d: height %d\n", m->width, m->height);
    fprintf(f, "\tcolor max value %d\n", m->color_size);
}


static struct file_ops pnm_ops = {
    .name = "PNM",
    .probe = PNM_probe,
    .load = PNM_load,
    .free = PNM_free,
    .info = PNM_info,
};

void 
PNM_init(void)
{
    file_ops_register(&pnm_ops);
}