#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if 0
#include "coding.h"
#include "huffman.h"


void strreverse(char* begin, char* end) {
	char aux;
	while(end>begin)
		aux=*end, *end--=*begin, *begin++=aux;
}
	
void itoa(int value, char* str, int base) {
	static char num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	char* wstr=str;
	int sign;	
	div_t res;

	// Validate base
	if (base<2 || base>35){ *wstr='\0'; return; }

	// Take care of sign
	if ((sign=value) < 0) value = -value;

	// Conversion. Number is reversed.
	do {
		res = div(value,base);
		*wstr++ = num[res.rem];
        value=res.quot;
	}while(value);

	if(sign<0) *wstr++='-';
	*wstr='\0';
	// Reverse string	
	strreverse(str,wstr-1);
	
}


int main()
{
    uint8_t dc[16]={0, 2, 2, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t syms[] = {3, 4, 2, 5, 1, 6, 7, 0, 8, 9};
    huffman_tree *tree = huffman_tree_init();
    huffman_build_lookup_table(tree, 0, 0, dc, syms);
    uint8_t data[] = {0xFC , 0xFF, 0xE2 , 0xAF , 0xEF , 0xF3 , 0x15, 0x7F};
    huffman_decode_start(data, 8);
    int a = huffman_decode_symbol(tree);
    huffman_decode_end();
    printf("maxbitlen %d, numofcodes %d\n", tree->maxbitlen, tree->numcodes);
    huffman_cleanup(tree);
    printf("decode first dc %d\n", a);

    
    return 0;
}
#endif
#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "assert.h"
#include "stdlib.h"

#define jpeg_printf

#define getbit(k, i) (((k)>>(i))&1)
#define high(k)      ((k)>>4)
#define low(k)       ((k)&0xf)

#ifndef _DEBUG
#undef  assert
#define assert(x) if (!(x)){ return 0; }
#endif

typedef unsigned char uint8;

typedef struct color_t
{
    uint8_t y;
    uint8_t cr;
    uint8_t cb;
}color;

typedef struct _huffman_node {
    struct _huffman_node* l;
    struct _huffman_node* r;
    int w;
}huffman_node;

static huffman_node*
huffman_node_create() {
    huffman_node* p;

    p = (huffman_node*)malloc(sizeof(huffman_node));
    p->l = 0;
    p->r = 0;
    p->w = -1;
    return p;
}

static void
huffman_node_visit(huffman_node* p, int c, int l) {
    int i;
    if (p == 0) {
        return;
    }

    if (p->l == 0 && p->r == 0) {
        for (i = l - 1; i >= 0; i--) {
            if (getbit(c, i)) {
                printf("1");
            }
            else {
                printf("0");
            }
        }
        printf(" 0x%02x\n", p->w);
    }
    else {
        huffman_node_visit(p->l, c<<1, l + 1);
        huffman_node_visit(p->r, (c<<1) + 1, l + 1);
    }
}

static void
huffman_node_release(huffman_node* node) {
    if (node != 0) {
        huffman_node_release(node->l);
        huffman_node_release(node->r);
        free(node);
    }
}

typedef struct _huffman {
    huffman_node* r;
    huffman_node* p;

    int l;
    int c;
}huffman;

#define huffman_reset(h) h->p = h->r

static huffman*
huffman_create() {
    huffman* h;
    
    h = (huffman*)malloc(sizeof(huffman));
    h->r = huffman_node_create();
    h->l = -1;
    return h;
}

static int
huffman_match(huffman* h, int x) {
    if (x) {
        h->p = h->p->r;
    }
    else {
        h->p = h->p->l;
    }

    if (h->p == 0) {
        return -1;
    }
    return h->p->l == 0 && h->p->r == 0; 
}

static void
huffman_insert(huffman* h, int l, int v) {
    int i;
    huffman_node* p;

    if (h->l == -1) {
        h->c = 0;
        h->l = l;
    }
    else {
        h->c = (h->c + 1)<<(l - h->l);
        h->l = l;
    }

    p = h->r;
    for (i = l - 1; i >= 0; i--) {
        if (getbit(h->c, i)) {
            if (p->r == 0) {
                p->r = huffman_node_create();
            }
            p = p->r;
        }
        else {
            if (p->l == 0) {
                p->l = huffman_node_create();
            }
            p = p->l;
        }
    }
    p->w = v;
}

static void
huffman_print(huffman* h) {
    huffman_node_visit(h->r, 0, 0);
}

static void
huffman_release(huffman* h) {
    if (h != 0) {
        huffman_node_release(h->r);
        free(h);
    }
}

typedef struct _color_comp {
    int qi;
    int di;
    int ai;
    int df;
}color_comp;

typedef struct _jpeg {
    int width;
    int height;

    uint8_t qt[4][64];

    huffman* dh[4];
    huffman* ah[4];

    color_comp cc[4];

    color* pixels;

    uint8* data;
    int    size;

    int ei;
    int di;
    int rr;
}jpeg;

static int
jpeg_decode_huffman(jpeg* jpg, uint8* p) {
    huffman* h;
    uint8* q;
    int i, j, l, k, f, d;
    int t[16];

    q = jpg->data + jpg->size;
    assert(p + 2 <= q);
    l = (p[0]<<8) + p[1];
    assert(p + l <= q);
    for (k = 2; k < l;) {
        d = low(p[k]);
        f = high(p[k]);
        assert(d <= 3 && f <= 1);

        if (f) {
            h = jpg->ah[d];
        }
        else {
            h = jpg->dh[d];
        }

        k++;
        assert(p + k + 16 <= q && k + 16 <= l);
        for (i = 0; i < 16; i++) {
            t[i] = p[k++];
        }

        for (i = 0; i < 16; i++) {
            assert(p + k + t[i] <= q && k + t[i] <= l);
            for (j = 0; j < t[i]; j++) {
                huffman_insert(h, i + 1, p[k++]);
            }
        }
    }
    return 1;
}

static int
jpeg_prepare(jpeg* jpg) {
    int i, j, l, p, id, qi, tp, s;
    uint8* data;
    
    data = jpg->data;
    if (data[0] != 0xff || data[1] != 0xd8) {
        return 0;
    }

    s = jpg->size;
    for (i = 2; i < s;) {
        if (data[i++] == 0xff) {
            assert(i + 3 <= s);

            switch (data[i++]) {
            case 0xdb: {
                    l = (data[i]<<8) + data[i + 1];
                    assert(i + l <= s);
                    for (p = i + 2; p < i + l;) {
                        id = low(data[p]);
                        assert(id <= 3 && high(data[p]) == 0);
                        p++;
                        assert(p + 64 <= s && p + 64 <= i + l);
                        for (j = 0; j < 64; j++) {
                            jpg->qt[id][j] = data[p++];
                        }
                    }
                }
                break;
            case 0xc0: {
                    l = (data[i]<<8) + data[i + 1];
                    assert(l == 17 && i + 17 <= s && data[i + 2] == 8);

                    p = i + 3;
                    jpg->height = (data[p]<<8) + data[p + 1];
                    p += 2;
                    jpg->width = (data[p]<<8) + data[p + 1];
                    p += 2;
                    assert(low(jpg->height) == 0 && low(jpg->width) == 0 && data[p] == 3);
                    p++;
                    
                    id = data[p];
                    qi = data[p + 2];
                    assert(id == 1 && data[p + 1] == 0x22 && qi <= 3);
                    jpg->cc[id].qi = qi;
                    p += 3;

                    id = data[p];
                    qi = data[p + 2];
                    assert(id == 2 && data[p + 1] == 0x11 && qi <= 3);
                    jpg->cc[id].qi = qi;
                    p += 3;

                    id = data[p];
                    qi = data[p + 2];
                    assert(id == 3 && data[p + 1] == 0x11 && qi <= 3);
                    jpg->cc[id].qi = qi;
                }
                break;
            case 0xc4:
                if (!jpeg_decode_huffman(jpg, data + i)) {
                    return 0;
                }
                break;
            case 0xdd: {
                    assert(0);
                }
            case 0xda: {
                    l = (data[i]<<8) + data[i + 1];
                    assert(l == 12 && i + l <= s);
                    
                    p = i + 2;
                    assert(data[p] == 3);
                    p++;

                    id = data[p];
                    tp = data[p + 1];
                    assert(id == 1 && low(tp) <= 3 &&high(tp) <= 3);
                    jpg->cc[id].ai = low(tp);
                    jpg->cc[id].di = high(tp);
                    p += 2;

                    id = data[p];
                    tp = data[p + 1];
                    assert(id == 2 && low(tp) <= 3 &&high(tp) <= 3);
                    jpg->cc[id].ai = low(tp);
                    jpg->cc[id].di = high(tp);
                    p += 2;

                    id = data[p];
                    tp = data[p + 1];
                    assert(id == 3 && low(tp) <= 3 &&high(tp) <= 3);
                    jpg->cc[id].ai = low(tp);
                    jpg->cc[id].di = high(tp);

                    jpg->ei = i + l;
                    return 1;
                }
                break;
            default:
                break;
            }
            i += (data[i]<<8) + data[i + 1];
        }
    }
    return 0;
}
void jpeg_free(void* ctx);

void* 
jpeg_load(const char* file) {
    FILE* fd;
    jpeg* jpg;
    int i;

    jpg = (jpeg*)malloc(sizeof(jpeg));
    memset(jpg, 0, sizeof(jpeg));

    for (i = 0; i < 4; i++) {
        jpg->dh[i] = huffman_create();
        jpg->ah[i] = huffman_create();
    }

    fd = fopen(file, "rb");
    if (fd == 0) {
        jpeg_free(jpg);
        return 0;
    }

    //文件读写
    fseek(fd, 0 ,SEEK_END);
    jpg->size = ftell(fd);
    jpg->data = (uint8_t*)malloc(jpg->size * sizeof(uint8_t));
    
    fseek(fd, 0, SEEK_SET);
    fread(jpg->data, jpg->size, 1, fd);
    fclose(fd);

    if (!jpeg_prepare(jpg)) {
        jpeg_free(jpg);
        return 0;
    }
    return jpg;
}

void
jpeg_free(void* ctx) {
    int i;
    jpeg* jpg = (jpeg*)ctx;
    if (jpg->pixels != 0) {
        free(jpg->pixels);
    }
    if (jpg->data != 0) {
        free(jpg->data);
    }

    for (i = 0; i < 4; i++) {
        huffman_release(jpg->dh[i]);
        huffman_release(jpg->ah[i]);
    }
    free(jpg);
}

int
jpeg_query(void* ctx, int* width, int* height) {
    jpeg* jpg = (jpeg*)ctx;
    *width  = jpg->width;
    *height = jpg->height;
    return 1;
}

//-1 结束
//-2 错误
static
int jpeg_nextbyte(jpeg* jpg) {
    int x;
    if (jpg->ei >= jpg->size) {
        return -1;
    }

    x = jpg->data[jpg->ei];
    if (x != 0xff) {
        jpg->ei++;
        return x;
    }
    else {
        jpg->ei++;
        if (jpg->ei >= jpg->size) {
            return -1;
        }

        x = jpg->data[jpg->ei];
        if (x == 0) {
            jpg->ei++;
            return 0xff;
        }
        else if (x == 0xff)
        {
            return jpeg_nextbyte(jpg);
        }
        else if (x < 0xd0 || x > 0xd9) {
            jpg->ei++;
            return x;
        }
        else {
            return -2;
        }
    }
    return -2;
}

static int
jpeg_nextbit(jpeg* jpg) {
    int x;
    if (jpg->di == 0 && (jpg->rr = jpeg_nextbyte(jpg)) < 0) {
        return -1;
    }
    x = getbit(jpg->rr, 7 - jpg->di);
    jpg->di = (jpg->di + 1) % 8;
    return x;
}

static int
jpeg_decode_byte(jpeg* jpg, huffman* h) {
    int x, m;
    huffman_reset(h);

    while ((x = jpeg_nextbit(jpg)) >= 0) {
        m = huffman_match(h, x);
        if (m == -1) {
            return -1;
        }
        else if (m) {
            jpeg_printf("x%x ", h->p->w);
            return h->p->w;
        }
    }
    return -1;
}

static int
jpeg_read(jpeg* jpg, int len, int* x) {
    int i, v, s, b;

    if ((s = jpeg_nextbit(jpg)) < 0) {
        return 0;
    }

    v = 0;
    for (i = 1; i < len; i++) {
        if ((b = jpeg_nextbit(jpg)) < 0) {
            return 0;
        }
        v = (v<<1) + b;
    }
    if (s == 0) {
        *x = -(1<<len) + 1 + v;
    }
    else {
        *x =  (1<<(len - 1)) + v;
    }
    jpeg_printf("r%d ", *x);
    return 1;
}

//region copy from stb_image.cpp
#pragma region idct

uint8_t clamp(int x)
{
    if ((unsigned int) x > 255) {
        if (x < 0) return 0;
        if (x > 255) return 255;
    }
    return (uint8) x;
}

#define f2f(x)  (int) (((x) * 4096 + 0.5))
#define fsh(x)  ((x) << 12)

#define IDCT_1D(s0,s1,s2,s3,s4,s5,s6,s7)        \
    int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3; \
    p2 = s2;                                    \
    p3 = s6;                                    \
    p1 = (p2+p3) * f2f(0.5411961f);             \
    t2 = p1 + p3*f2f(-1.847759065f);            \
    t3 = p1 + p2*f2f( 0.765366865f);            \
    p2 = s0;                                    \
    p3 = s4;                                    \
    t0 = fsh(p2+p3);                            \
    t1 = fsh(p2-p3);                            \
    x0 = t0+t3;                                 \
    x3 = t0-t3;                                 \
    x1 = t1+t2;                                 \
    x2 = t1-t2;                                 \
    t0 = s7;                                    \
    t1 = s5;                                    \
    t2 = s3;                                    \
    t3 = s1;                                    \
    p3 = t0+t2;                                 \
    p4 = t1+t3;                                 \
    p1 = t0+t3;                                 \
    p2 = t1+t2;                                 \
    p5 = (p3+p4)*f2f( 1.175875602f);            \
    t0 = t0*f2f( 0.298631336f);                 \
    t1 = t1*f2f( 2.053119869f);                 \
    t2 = t2*f2f( 3.072711026f);                 \
    t3 = t3*f2f( 1.501321110f);                 \
    p1 = p5 + p1*f2f(-0.899976223f);            \
    p2 = p5 + p2*f2f(-2.562915447f);            \
    p3 = p3*f2f(-1.961570560f);                 \
    p4 = p4*f2f(-0.390180644f);                 \
    t3 += p1+p4;                                \
    t2 += p2+p3;                                \
    t1 += p2+p4;                                \
    t0 += p1+p3;

static void idct_block(uint8 *out, int data[64])
{
    int i,val[64],*v=val;
    uint8 *o;
    int *d = data;

    for (i=0; i < 8; ++i,++d, ++v) {
        if (d[8]==0 && d[16]==0 && d[24]==0 && d[32]==0
            && d[40]==0 && d[48]==0 && d[56]==0) {
                int dcterm = d[0]<< 2;
                v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dcterm;
        } else {
            IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512; x1 += 512; x2 += 512; x3 += 512;
            v[ 0] = (x0+t3) >> 10;
            v[56] = (x0-t3) >> 10;
            v[ 8] = (x1+t2) >> 10;
            v[48] = (x1-t2) >> 10;
            v[16] = (x2+t1) >> 10;
            v[40] = (x2-t1) >> 10;
            v[24] = (x3+t0) >> 10;
            v[32] = (x3-t0) >> 10;
        }
    }

    for (i=0, v=val, o=out; i < 8; ++i,v+=8,o+=8) {
        IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128<<17);
        x1 += 65536 + (128<<17);
        x2 += 65536 + (128<<17);
        x3 += 65536 + (128<<17);

        o[0] = clamp((x0+t3) >> 17);
        o[7] = clamp((x0-t3) >> 17);
        o[1] = clamp((x1+t2) >> 17);
        o[6] = clamp((x1-t2) >> 17);
        o[2] = clamp((x2+t1) >> 17);
        o[5] = clamp((x2-t1) >> 17);
        o[3] = clamp((x3+t0) >> 17);
        o[4] = clamp((x3-t0) >> 17);
    }
}

#pragma endregion

static
int invZ[64] = {
        0,   1,  8, 16,  9,  2,  3, 10,
        17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63};

static int
jpeg_decode_block(jpeg* jpg, uint8* block, int ci) {
    int cc[64];
    int aa[64];
    huffman *dh, *ah;
    uint8* qt;
    int i, t, r, v;

    dh = jpg->dh[jpg->cc[ci].di];
    ah = jpg->ah[jpg->cc[ci].ai];
    qt = jpg->qt[jpg->cc[ci].qi];
    memset(cc, 0, 64 * sizeof(cc[0]));

    //直流分量
    jpeg_printf("\nd:%d\n", jpg->cc[ci].di);
    if ((t = jpeg_decode_byte(jpg, dh)) < 0) {
        return 0;
    }
    if (t > 0) {
        assert(jpeg_read(jpg, t, &r));
        cc[0] = r;
    }

    cc[0] += jpg->cc[ci].df;
    jpg->cc[ci].df = cc[0];
    cc[0] = (short)cc[0];

    //交流分量
    jpeg_printf("\na:%d\n", jpg->cc[ci].ai);
    for (i = 1; i < 64;) {
        if ((r = jpeg_decode_byte(jpg, ah)) < 0) {
            return 0;
        }

        if (low(r) == 0) {
            if (r != 0xf0) {
                break;
            }
            i += 16;
        }
        else {
            i += high(r);
            assert(jpeg_read(jpg, low(r), &v));
            if (i < 64) {
                cc[i++] = v;
            }
        }
    }

    //反量化
    for (i = 0; i < 64; i++) {
        cc[i] *= qt[i];
    }

    //反Zig
    for (i = 0; i < 64; i++) {
        aa[invZ[i]] = cc[i];
    }

    //反离散余弦变换
    idct_block(block, aa);
    return 1;
}

#define div16(x) ((uint8) ((x) >> 4))
#define div4(x) ((uint8) ((x) >> 2))

color* jpeg_data(void* ctx) {
    jpeg* jpg = (jpeg*)ctx;
    return jpg->pixels;
}

static int
jpeg_process_mcu(jpeg* jpg, int l, int t, 
            uint8* py, uint8* pcr, uint8* pcb) {
    int i, w;
    uint8 c[64];

    w = jpg->width;
    assert(jpeg_decode_block(jpg, c, 1));
    for (i = 0; i < 64; i++) {
        py[(t + i / 8) * w + l + i % 8] = c[i];
    }
    assert(jpeg_decode_block(jpg, c, 1));
    for (i = 0; i < 64; i++) {
        py[(t + i / 8) * w + l + 8 + i % 8] = c[i];
    }
    assert(jpeg_decode_block(jpg, c, 1));
    for (i = 0; i < 64; i++) {
        py[(t + 8 + i / 8) * w + l + i % 8] = c[i];
    }
    assert(jpeg_decode_block(jpg, c, 1));
    for (i = 0; i < 64; i++) {
        py[(t + 8 + i / 8) * w + l + 8 + i % 8] = c[i];
    }
    assert(jpeg_decode_block(jpg, c, 2));
    for (i = 0; i < 64; i++) {
        pcr[(t /2 + i / 8) * w / 2 + l / 2 + i % 8] = c[i];
    }
    assert(jpeg_decode_block(jpg, c, 3));
    for (i = 0; i < 64; i++) {
        pcb[(t / 2 + i / 8) * w / 2  + l / 2 + i % 8] = c[i];
    }
    return 1;

}

//resample copy from stbi_image.cpp
#pragma region resample

typedef struct {
    uint8_t *line0,*line1;
    int hs,vs;   // expansion factor in each axis
    int w_lores; // horizontal pixels pre-expansion 
    int ystep;   // how far through vertical expansion we are
    int ypos;    // which pre-expansion row we're on
} resample;

static void
resampleCr(color* colors, uint8* in_near, uint8* in_far, int w) {
    int i, t0, t1;

    t1 = 3 * in_near[0] + in_far[0];
    colors[0].cr = div4(t1 + 2);
    for (i=1; i < w; ++i) {
        t0 = t1;
        t1 = 3 * in_near[i] + in_far[i];
        colors[i * 2-1].cr = div16(3*t0 + t1 + 8);
        colors[i * 2].cr   = div16(3*t1 + t0 + 8);
    }
    colors[w * 2 - 1].cr = div4(t1+2);
}

static void
resampleCb(color* colors, uint8* in_near, uint8* in_far, int w) {
    int i, t0, t1;

    t1 = 3 * in_near[0] + in_far[0];
    colors[0].cb = div4(t1 + 2);
    for (i=1; i < w; ++i) {
        t0 = t1;
        t1 = 3 * in_near[i] + in_far[i];
        colors[i * 2-1].cb = div16(3*t0 + t1 + 8);
        colors[i * 2].cb   = div16(3*t1 + t0 + 8);
    }
    colors[w * 2 - 1].cb = div4(t1+2);
}
#pragma endregion

int
jpeg_decode(void* ctx) {
    uint8 *py, *pcr, *pcb;
    int w, h, i, j;
    jpeg* p;
    color* px;
    resample sample;

    p   = (jpeg*)ctx;
    w   = p->width;
    h   = p->height;
    py  = (uint8*)malloc(w * h * sizeof(uint8));
    pcr = (uint8*)malloc(w * h * sizeof(uint8) / 4);
    pcb = (uint8*)malloc(w * h * sizeof(uint8) / 4);

    for (i = 0; i < h; i += 16) {
        for (j = 0; j < w; j += 16) {
            if (!jpeg_process_mcu(p, j,
                      i, py, pcr, pcb)) {
                free(py); free(pcr); free(pcb);
                return 0;
            }
        }
    }

    px = (color*)malloc(w * h * sizeof(color));
    for (i = w * h - 1; i >= 0; i--) {
        px[i].y = py[i];
    }

    sample.hs = sample.vs = 2;
    sample.w_lores = w / 2;
    sample.ystep = 1;
    sample.line0 = pcr;
    sample.line1 = pcr;
    sample.ypos  = 0;

    for (i = 0; i < h; i++) {
        color* out = px + i * w;
        int y_bot = sample.ystep >= (sample.vs >> 1);
        resampleCr(out, 
             y_bot ? sample.line1 : sample.line0,
             y_bot ? sample.line0 : sample.line1,
             sample.w_lores);

        if (++sample.ystep >= sample.vs) {
            sample.ystep = 0;
            sample.line0 = sample.line1;
            if (++sample.ypos < h / 2)
                sample.line1 += sample.w_lores;
        }
    }

    sample.ystep = 1;
    sample.line0 = pcb;
    sample.line1 = pcb;
    sample.ypos  = 0;

    for (i = 0; i < h; i++) {
        color* out = px + i * w;
        int y_bot = sample.ystep >= (sample.vs >> 1);
        resampleCb(out, 
            y_bot ? sample.line1 : sample.line0,
            y_bot ? sample.line0 : sample.line1,
            sample.w_lores);

        if (++sample.ystep >= sample.vs) {
            sample.ystep = 0;
            sample.line0 = sample.line1;
            if (++sample.ypos < h / 2)
                sample.line1 += sample.w_lores;
        }
    }
    free(py); free(pcr); free(pcb);
    p->pixels = px;
    return 1;
}

#if 0
void jpeg_save(const char* file,int desW, int desH, color* colors)
{
    int desBufSize = ((desW * BITCOUNT + 31) / 32) * 4 * desH;
    int desLineSize = ((desW * BITCOUNT + 31) / 32) * 4;    
    BYTE *desBuf = new BYTE[desBufSize];

    for (int i = 0; i < desH; i++)
    {
        int x = i * desLineSize;
        for (int j = 0; j < desW; j++)
        {
            int y_fixed = (colors->y << 16) + 32768; // rounding
            int r,g,b;
            int cr = colors->cr - 128;
            int cb = colors->cb - 128;
            r = y_fixed + cb*float2fixed(1.40200f);
            g = y_fixed - cb*float2fixed(0.71414f) - cr*float2fixed(0.34414f);
            b = y_fixed                            + cr*float2fixed(1.77200f);
            r >>= 16;
            g >>= 16;
            b >>= 16;
            if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
            if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
            if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }

            desBuf[x++] = b;
            desBuf[x++] = g;
            desBuf[x++] = r;

            colors++;
        }
    }

    HFILE hfile = _lcreat(file.c_str,0);
    BITMAPFILEHEADER nbmfHeader = { 0 };    
    nbmfHeader.bfType = 0x4D42;
    nbmfHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + desBufSize;
    nbmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    BITMAPINFOHEADER bmi = { 0 };
    bmi.biSize=sizeof(BITMAPINFOHEADER); 
    bmi.biWidth = desW; 
    bmi.biHeight= -desH; 
    bmi.biPlanes=1;
    bmi.biBitCount=BITCOUNT;

    _lwrite(hfile,(LPCSTR)&nbmfHeader,sizeof(BITMAPFILEHEADER));
    _lwrite(hfile,(LPCSTR)&bmi,sizeof(BITMAPINFOHEADER));
    _lwrite(hfile,(LPCSTR)desBuf,desBufSize);
    _lclose(hfile);
}
#endif 


int main() 
{
    return 0;
}