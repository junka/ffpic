/* Decode the sub-formats and edge paths that test_decode.c's minimal files do
 * not reach: the alternate bitmap layouts, the ascii and pam netpbm variants,
 * tga colour maps, 16 bit and rle pixels, every png filter, the tiff
 * compressors and byte orders, gif extensions and frames, jpeg sampling and
 * restart intervals, ico directory selection and the exr channel types and
 * line orders. Every pixel here is written by hand with known values, so the
 * checks compare channel placement, not just the image size.
 *
 * The malformed files at the end have to be refused cleanly: recognised by
 * their probe and then rejected by the loader without a crash. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "utils.h"

struct want_px {
    int row;
    int off;
    int want;
};

struct wbuf {
    uint8_t *p;
    size_t n;
};

static void
wb_bytes(struct wbuf *w, const void *src, size_t n)
{
    memcpy(w->p + w->n, src, n);
    w->n += n;
}

static void
wb_u8(struct wbuf *w, unsigned v)
{
    w->p[w->n++] = (uint8_t)v;
}

static void
wb_le16(struct wbuf *w, uint16_t v)
{
    w->p[w->n++] = (uint8_t)(v & 0xFF);
    w->p[w->n++] = (uint8_t)(v >> 8);
}

static void
wb_le32(struct wbuf *w, uint32_t v)
{
    w->p[w->n++] = (uint8_t)(v & 0xFF);
    w->p[w->n++] = (uint8_t)((v >> 8) & 0xFF);
    w->p[w->n++] = (uint8_t)((v >> 16) & 0xFF);
    w->p[w->n++] = (uint8_t)(v >> 24);
}

static void
wb_be16(struct wbuf *w, uint16_t v)
{
    w->p[w->n++] = (uint8_t)(v >> 8);
    w->p[w->n++] = (uint8_t)(v & 0xFF);
}

static void
wb_be32(struct wbuf *w, uint32_t v)
{
    w->p[w->n++] = (uint8_t)(v >> 24);
    w->p[w->n++] = (uint8_t)((v >> 16) & 0xFF);
    w->p[w->n++] = (uint8_t)((v >> 8) & 0xFF);
    w->p[w->n++] = (uint8_t)(v & 0xFF);
}

static void
wb_zero(struct wbuf *w, size_t n)
{
    memset(w->p + w->n, 0, n);
    w->n += n;
}

static void
put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void
put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)(v >> 24);
}

static void
put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void
put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint32_t
crc32(const uint8_t *buf, int len)
{
    uint32_t c = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        c ^= buf[i];
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : c >> 1;
    }
    return c ^ 0xFFFFFFFF;
}

static uint32_t
adler32(const uint8_t *buf, int len)
{
    uint32_t a = 1, b = 0;
    for (int i = 0; i < len; i++) {
        a += buf[i];
        b += a;
    }
    return (b << 16) | a;
}

static int
write_file(const char *name, const uint8_t *data, size_t len)
{
    FILE *f = fopen(name, "wb");
    if (!f) {
        printf("cannot write %s\n", name);
        return -1;
    }
    size_t done = fwrite(data, 1, len, f);
    fclose(f);
    return done == len ? 0 : -1;
}

/* probe, load, check the size and compare a handful of pixels. Offsets are
 * the byte within a row, so the same helper covers the packed png rows and
 * the 4 byte per pixel formats. */
static int
check_pixels(const char *name, const char *fmt, int width, int height,
             const struct want_px *px, int npx, int tol)
{
    struct file_ops *ops = file_probe(name);
    struct pic *p;
    int rc = 0;

    if (!ops) {
        printf("%s: no format claimed it, expected %s\n", name, fmt);
        return -1;
    }
    if (strcmp(ops->name, fmt)) {
        printf("%s: probed as %s, expected %s\n", name, ops->name, fmt);
        return -1;
    }

    p = file_load(ops, name, 0);
    if (!p) {
        printf("%s: %s could not decode it\n", name, fmt);
        return -1;
    }
    if (p->width != width || p->height != height) {
        printf("%s: got %dx%d, expected %dx%d\n", name, p->width, p->height,
               width, height);
        rc = -1;
    } else if (!p->pixels) {
        printf("%s: decoded with no pixels\n", name);
        rc = -1;
    } else {
        const uint8_t *q = p->pixels;
        for (int i = 0; i < npx; i++) {
            int got;
            if (px[i].row < 0 || px[i].row >= p->height || px[i].off < 0 ||
                px[i].off >= p->pitch) {
                printf("%s: bad expectation row %d off %d for a %dx%d pitch %d\n",
                       name, px[i].row, px[i].off, p->width, p->height,
                       p->pitch);
                rc = -1;
                continue;
            }
            got = q[px[i].row * p->pitch + px[i].off];
            if (abs(got - px[i].want) > tol) {
                printf("%s: (%d, off %d) is %d, expected %d\n", name,
                       px[i].row, px[i].off, got, px[i].want);
                rc = -1;
            }
        }
    }

    file_free(ops, p);
    remove(name);
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* probe only: a NULL expect means no parser may claim the file */
static int
check_claim(const char *name, const char *expect)
{
    struct file_ops *ops = file_probe(name);
    int rc = 0;

    if (expect == NULL) {
        if (ops) {
            printf("%s: claimed as %s, expected no format\n", name, ops->name);
            rc = -1;
        }
    } else if (!ops) {
        printf("%s: no format claimed it, expected %s\n", name, expect);
        rc = -1;
    } else if (strcmp(ops->name, expect)) {
        printf("%s: probed as %s, expected %s\n", name, ops->name, expect);
        rc = -1;
    }

    remove(name);
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* recognised by the probe, refused by the loader */
static int
check_reject(const char *name, const char *expect)
{
    struct file_ops *ops = file_probe(name);
    int rc = 0;

    if (!ops) {
        printf("%s: no format claimed it, expected %s\n", name, expect);
        rc = -1;
    } else if (strcmp(ops->name, expect)) {
        printf("%s: probed as %s, expected %s\n", name, ops->name, expect);
        rc = -1;
    } else {
        struct pic *p = file_load(ops, name, 0);
        if (p) {
            printf("%s: decoded, expected the file to be rejected\n", name);
            file_free(ops, p);
            rc = -1;
        }
    }

    remove(name);
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* ---------------------------------------------------------------- bitmap */

static void
bmp_head(struct wbuf *w, uint32_t offset, uint32_t size)
{
    wb_bytes(w, "BM", 2);
    wb_le32(w, size);
    wb_le16(w, 0);
    wb_le16(w, 0);
    wb_le32(w, offset);
}

static void
bmp_v2(struct wbuf *w, int32_t width, int32_t height, uint16_t bpp,
       uint32_t compression, uint32_t size_image, uint32_t colors_used)
{
    wb_le32(w, 40);
    wb_le32(w, (uint32_t)width);
    wb_le32(w, (uint32_t)height);
    wb_le16(w, 1);
    wb_le16(w, bpp);
    wb_le32(w, compression);
    wb_le32(w, size_image);
    wb_le32(w, 0);
    wb_le32(w, 0);
    wb_le32(w, colors_used);
    wb_le32(w, 0);
}

/* OS/2 v1 header with an 8 bit palette. The decoder reads four bytes an
 * entry for this header even though the format defines three, so the table
 * below leaves the fourth byte zero and the pixel data starts after it. */
static int
emit_bmp_os2(void)
{
    enum { BW = 4, BH = 2, PAL = 256 };
    static const uint8_t pal[4][3] = {
        {10, 20, 30}, {40, 50, 60}, {70, 80, 90}, {100, 110, 120},
    };
    static const uint8_t rows[2][4] = {{3, 2, 1, 0}, {0, 1, 2, 3}};
    uint32_t dataoff = 14 + 12 + (uint32_t)PAL * 4;
    uint8_t file[2048];
    struct wbuf w = {file, 0};

    bmp_head(&w, dataoff, dataoff + (uint32_t)(BW * BH));
    wb_le32(&w, 12);
    wb_le16(&w, BW);
    wb_le16(&w, BH);
    wb_le16(&w, 1);
    wb_le16(&w, 8);
    for (int i = 0; i < PAL; i++) {
        for (int c = 0; c < 3; c++) {
            wb_u8(&w, i < 4 ? pal[i][c] : 0);
        }
        wb_u8(&w, 0);
    }
    for (int r = 0; r < 2; r++) {
        wb_bytes(&w, rows[r], sizeof(rows[r]));
    }
    return write_file("bmp_os2.bmp", file, w.n);
}

/* negative height: the rows are stored top down */
static int
emit_bmp_td24(void)
{
    enum { BW = 4, BH = 2 };
    uint8_t file[256];
    struct wbuf w = {file, 0};

    bmp_head(&w, 54, 54 + BW * BH * 3);
    bmp_v2(&w, BW, -BH, 24, 0, BW * BH * 3, 0);
    for (int y = 0; y < BH; y++) {
        for (int x = 0; x < BW; x++) {
            wb_u8(&w, (unsigned)(10 + x + y));
            wb_u8(&w, (unsigned)(40 + x * 10 + y));
            wb_u8(&w, (unsigned)(70 + x * 10 + y));
        }
    }
    return write_file("bmp_td24.bmp", file, w.n);
}

/* 32 bit with explicit channel masks, the alpha byte has to come back 0xFF */
static int
emit_bmp_bitfields(void)
{
    enum { BW = 4, BH = 2 };
    uint32_t dataoff = 54 + 12;
    uint8_t file[256];
    struct wbuf w = {file, 0};

    bmp_head(&w, dataoff, dataoff + BW * BH * 4);
    bmp_v2(&w, BW, BH, 32, 3, BW * BH * 4, 0);
    wb_le32(&w, 0x00FF0000);
    wb_le32(&w, 0x0000FF00);
    wb_le32(&w, 0x000000FF);
    for (int y = BH - 1; y >= 0; y--) {
        for (int x = 0; x < BW; x++) {
            wb_u8(&w, (unsigned)(20 + x * 10 + y));
            wb_u8(&w, (unsigned)(60 + x * 10 + y));
            wb_u8(&w, (unsigned)(100 + x * 10 + y));
            wb_u8(&w, 0);
        }
    }
    return write_file("bmp_bitfields.bmp", file, w.n);
}

/* 8 bit run length encoding, a repeat run for the bottom row and an absolute
 * run for the top one */
static int
emit_bmp_rle8(void)
{
    enum { BW = 4, BH = 2 };
    static const uint8_t pal[4][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
    };
    /* run 2 of index 1, end of line, absolute 2 3 3 2, end of bitmap */
    static const uint8_t rle[] = {2, 1, 0, 0, 0, 4, 2, 3, 3, 2, 0, 1};
    uint32_t dataoff = 54 + 16;
    uint8_t file[256];
    struct wbuf w = {file, 0};

    bmp_head(&w, dataoff, dataoff + (uint32_t)sizeof(rle));
    bmp_v2(&w, BW, BH, 8, 1, (uint32_t)sizeof(rle), 4);
    for (int i = 0; i < 4; i++) {
        wb_bytes(&w, pal[i], 3);
        wb_u8(&w, 0);
    }
    wb_bytes(&w, rle, sizeof(rle));
    return write_file("bmp_rle8.bmp", file, w.n);
}

/* 4 bit run length encoding, the nibbles of a run byte alternate */
static int
emit_bmp_rle4(void)
{
    enum { BW = 4, BH = 2 };
    /* run 2 of 1,2; end of line; absolute 3,0,1,2 with two pad bytes;
     * end of bitmap */
    static const uint8_t rle[] = {2, 0x12, 0, 0, 0, 4, 0x30, 0x12, 0, 0, 0, 1};
    uint32_t dataoff = 54 + 64;
    uint8_t file[256];
    struct wbuf w = {file, 0};

    bmp_head(&w, dataoff, dataoff + (uint32_t)sizeof(rle));
    bmp_v2(&w, BW, BH, 4, 2, (uint32_t)sizeof(rle), 16);
    for (int i = 0; i < 16; i++) {
        wb_u8(&w, (unsigned)(i * 10));
        wb_u8(&w, (unsigned)(255 - i * 10));
        wb_u8(&w, (unsigned)((i * 17) & 0xFF));
        wb_u8(&w, 0);
    }
    wb_bytes(&w, rle, sizeof(rle));
    return write_file("bmp_rle4.bmp", file, w.n);
}

/* ----------------------------------------------------------------- pnm */

static int
emit_pnm_p1(void)
{
    static const char file[] = "P1\n2 2\n0 1\n1 0\n";
    return write_file("pnm_p1.pbm", (const uint8_t *)file, sizeof(file) - 1);
}

static int
emit_pnm_p2(void)
{
    static const char file[] = "P2\n# a comment\n2 2\n255\n10 20\n30 40\n";
    return write_file("pnm_p2.pgm", (const uint8_t *)file, sizeof(file) - 1);
}

static int
emit_pnm_p3(void)
{
    static const char file[] =
        "P3\n2 2\n255\n1 2 3  4 5 6\n# halfway\n7 8 9  10 11 12\n";
    return write_file("pnm_p3.ppm", (const uint8_t *)file, sizeof(file) - 1);
}

static int
emit_pnm_p4(void)
{
    static const char head[] = "P4\n2 2\n";
    static const uint8_t body[] = {0xAA, 0x55};
    uint8_t file[32];
    size_t n = sizeof(head) - 1;
    memcpy(file, head, n);
    memcpy(file + n, body, sizeof(body));
    return write_file("pnm_p4.pbm", file, n + sizeof(body));
}

static int
emit_pnm_p5(void)
{
    static const char head[] = "P5\n4 1\n255\n";
    static const uint8_t body[] = {0, 85, 170, 255};
    uint8_t file[32];
    size_t n = sizeof(head) - 1;
    memcpy(file, head, n);
    memcpy(file + n, body, sizeof(body));
    return write_file("pnm_p5.pgm", file, n + sizeof(body));
}

static int
emit_pnm_p6(void)
{
    static const char head[] = "P6\n2 2\n255\n";
    static const uint8_t body[12] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    };
    uint8_t file[48];
    size_t n = sizeof(head) - 1;
    memcpy(file, head, n);
    memcpy(file + n, body, sizeof(body));
    return write_file("pnm_p6.ppm", file, n + sizeof(body));
}

static int
emit_pnm_p7_rgb(void)
{
    static const char head[] =
        "P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n"
        "TUPLTYPE RGB\nENDHDR\n";
    static const uint8_t body[] = {1, 2, 3, 4, 5, 6};
    uint8_t file[128];
    size_t n = sizeof(head) - 1;
    memcpy(file, head, n);
    memcpy(file + n, body, sizeof(body));
    return write_file("pnm_p7_rgb.pam", file, n + sizeof(body));
}

/* the pam black and white body takes the same packed rows as p4 */
static int
emit_pnm_p7_bw(void)
{
    static const char head[] =
        "P7\nWIDTH 2\nHEIGHT 2\nDEPTH 1\nMAXVAL 1\n"
        "TUPLTYPE BLACKANDWHITE\nENDHDR\n";
    static const uint8_t body[] = {0xAA, 0x55};
    uint8_t file[128];
    size_t n = sizeof(head) - 1;
    memcpy(file, head, n);
    memcpy(file + n, body, sizeof(body));
    return write_file("pnm_p7_bw.pam", file, n + sizeof(body));
}

static int
emit_pnm_bad_version(void)
{
    static const char file[] = "P9\n1 1\n0\n";
    return write_file("pnm_p9.pnm", (const uint8_t *)file, sizeof(file) - 1);
}

/* ----------------------------------------------------------------- tga */

static void
tga_head(struct wbuf *w, int cmap_type, int image_type, int first, int length,
         int entry_size, int width, int height, int bits)
{
    wb_u8(w, 0);
    wb_u8(w, (unsigned)cmap_type);
    wb_u8(w, (unsigned)image_type);
    wb_le16(w, (uint16_t)first);
    wb_le16(w, (uint16_t)length);
    wb_u8(w, (unsigned)entry_size);
    wb_le16(w, 0);
    wb_le16(w, 0);
    wb_le16(w, (uint16_t)width);
    wb_le16(w, (uint16_t)height);
    wb_u8(w, (unsigned)bits);
    wb_u8(w, 0);
}

static void
tga_footer(struct wbuf *w)
{
    wb_zero(w, 8);
    wb_bytes(w, "TRUEVISION-XFILE", 16);
    wb_u8(w, '.');
    wb_u8(w, 0);
}

static int
emit_tga32(void)
{
    enum { TW = 4, TH = 2 };
    uint8_t file[256];
    struct wbuf w = {file, 0};

    tga_head(&w, 0, 2, 0, 0, 0, TW, TH, 32);
    for (int i = TH - 1; i >= 0; i--) {
        for (int x = 0; x < TW; x++) {
            wb_u8(&w, (unsigned)(1 + x * 30 + i * 3));
            wb_u8(&w, (unsigned)(2 + x * 29 + i * 5));
            wb_u8(&w, (unsigned)(3 + x * 28 + i * 7));
            wb_u8(&w, (unsigned)(4 + x * 27 + i * 9));
        }
    }
    tga_footer(&w);
    return write_file("tga32.tga", file, w.n);
}

/* 16 bit r5g6b5 without the 2.0 footer, so the header only probe is used */
static int
emit_tga16(void)
{
    enum { TW = 4, TH = 2 };
    uint8_t file[256];
    struct wbuf w = {file, 0};

    tga_head(&w, 0, 2, 0, 0, 0, TW, TH, 16);
    for (int i = TH - 1; i >= 0; i--) {
        for (int x = 0; x < TW; x++) {
            unsigned r = (unsigned)((x * 3 + 1) & 0x1F);
            unsigned g = (unsigned)((x * 5 + 2) & 0x1F);
            unsigned b = (unsigned)((x * 7 + i * 3) & 0x1F);
            wb_le16(&w, (uint16_t)(b | g << 5 | r << 10));
        }
    }
    return write_file("tga16.tga", file, w.n);
}

/* indexed image whose colour map starts at index one: only three entries are
 * in the file, so the pixels stay at 0..2 */
static int
emit_tga_indexed(void)
{
    enum { TW = 4, TH = 2 };
    static const uint8_t pal[3][3] = {
        {11, 22, 33}, {44, 55, 66}, {77, 88, 99},
    };
    static const uint8_t rows[2][4] = {{0, 1, 2, 0}, {2, 1, 0, 2}};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    tga_head(&w, 1, 1, 1, 4, 24, TW, TH, 8);
    for (int i = 0; i < 3; i++) {
        wb_bytes(&w, pal[i], 3);
    }
    for (int r = 0; r < 2; r++) {
        wb_bytes(&w, rows[r], 4);
    }
    tga_footer(&w);
    return write_file("tga_idx.tga", file, w.n);
}

/* type 10: a four pixel run for the bottom row, a literal run for the top */
static int
emit_tga_rle(void)
{
    enum { TW = 4, TH = 2 };
    static const uint8_t top[4][3] = {
        {10, 20, 30}, {40, 50, 60}, {70, 80, 90}, {100, 110, 120},
    };
    uint8_t file[256];
    struct wbuf w = {file, 0};

    tga_head(&w, 0, 10, 0, 0, 0, TW, TH, 24);
    wb_u8(&w, 0x80 | 3);
    wb_u8(&w, 51);
    wb_u8(&w, 102);
    wb_u8(&w, 153);
    wb_u8(&w, 3);
    for (int x = 0; x < TW; x++) {
        wb_bytes(&w, top[x], 3);
    }
    tga_footer(&w);
    return write_file("tga_rle.tga", file, w.n);
}

/* ----------------------------------------------------------------- png */

static int
png_paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* the decoder's predictor, used backwards so the file holds the filtered row */
static void
png_filter_row(int type, const uint8_t *cur, const uint8_t *prev,
               uint8_t *out, int len, int bpp)
{
    for (int i = 0; i < len; i++) {
        int a = i >= bpp ? cur[i - bpp] : 0;
        int b = prev ? prev[i] : 0;
        int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
        int pred = 0;

        switch (type) {
        case 1:
            pred = a;
            break;
        case 2:
            pred = b;
            break;
        case 3:
            pred = (a + b) / 2;
            break;
        case 4:
            pred = png_paeth(a, b, c);
            break;
        default:
            break;
        }
        out[i] = (uint8_t)(cur[i] - pred);
    }
}

static void
png_chunk(struct wbuf *w, const char *type, const uint8_t *data, uint32_t len)
{
    size_t start = w->n;

    wb_be32(w, len);
    wb_bytes(w, type, 4);
    if (len) {
        wb_bytes(w, data, len);
    }
    wb_be32(w, crc32(w->p + start + 4, (int)len + 4));
}

/* one stored deflate block split over two IDAT chunks, so the idat
 * accumulation path is used as well */
static int
emit_png_image(const char *name, int width, int height, int bits, int color,
               const uint8_t *rows, int raw_pitch, const uint8_t *filters,
               const uint8_t *plte, int plte_len, int interlace)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    uint8_t filtered[256];
    uint8_t z[512];
    uint8_t ihdr[13];
    uint8_t file[1024];
    struct wbuf w = {file, 0};
    int flen = 0, zn = 0;

    int channels;

    switch (color) {
    case 0:
    case 3:
        channels = 1;
        break;
    case 2:
        channels = 3;
        break;
    case 4:
        channels = 2;
        break;
    default:
        channels = 4;
        break;
    }

    for (int y = 0; y < height; y++) {
        const uint8_t *prev = y ? rows + (size_t)(y - 1) * raw_pitch : NULL;
        filtered[flen++] = filters[y];
        png_filter_row(filters[y], rows + (size_t)y * raw_pitch, prev,
                       filtered + flen, raw_pitch,
                       (bits * channels + 7) / 8);
        flen += raw_pitch;
    }

    z[zn++] = 0x78;
    z[zn++] = 0x01;
    z[zn++] = 0x01;
    put_le16(z + zn, (uint16_t)flen);
    zn += 2;
    put_le16(z + zn, (uint16_t)(flen ^ 0xFFFF));
    zn += 2;
    memcpy(z + zn, filtered, (size_t)flen);
    zn += flen;
    put_be32(z + zn, adler32(filtered, flen));
    zn += 4;

    put_be32(ihdr, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = (uint8_t)bits;
    ihdr[9] = (uint8_t)color;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = (uint8_t)interlace;

    wb_bytes(&w, sig, sizeof(sig));
    png_chunk(&w, "IHDR", ihdr, sizeof(ihdr));
    if (plte) {
        png_chunk(&w, "PLTE", plte, (uint32_t)plte_len);
    }
    png_chunk(&w, "IDAT", z, (uint32_t)(zn / 2));
    png_chunk(&w, "IDAT", z + zn / 2, (uint32_t)(zn - zn / 2));
    png_chunk(&w, "IEND", NULL, 0);

    return write_file(name, file, w.n);
}

static int
emit_png_rgba(void)
{
    static const uint8_t rows[2][8] = {
        {1, 2, 3, 4, 5, 6, 7, 8}, {9, 10, 11, 12, 13, 14, 15, 16},
    };
    static const uint8_t filters[2] = {0, 1};
    return emit_png_image("png_rgba.png", 2, 2, 8, 6, &rows[0][0], 8,
                          filters, NULL, 0, 0);
}

static int
emit_png_rgb(void)
{
    static const uint8_t rows[2][6] = {
        {10, 20, 30, 40, 50, 60}, {70, 80, 90, 100, 110, 120},
    };
    static const uint8_t filters[2] = {2, 4};
    return emit_png_image("png_rgb.png", 2, 2, 8, 2, &rows[0][0], 6,
                          filters, NULL, 0, 0);
}

static int
emit_png_gray(void)
{
    static const uint8_t rows[2][4] = {
        {5, 6, 7, 8}, {9, 10, 11, 12},
    };
    static const uint8_t filters[2] = {3, 3};
    return emit_png_image("png_gray.png", 4, 2, 8, 0, &rows[0][0], 4,
                          filters, NULL, 0, 0);
}

/* one bit indexed rows are handed back packed, the palette is not applied */
static int
emit_png_indexed(void)
{
    static const uint8_t rows[1] = {0xAA};
    static const uint8_t filters[1] = {0};
    static const uint8_t plte[6] = {255, 0, 0, 0, 255, 0};
    return emit_png_image("png_idx.png", 8, 1, 1, 3, rows, 1, filters,
                          plte, sizeof(plte), 0);
}

static int
emit_png_gray16(void)
{
    static const uint8_t rows[1][4] = {{0x12, 0x34, 0xAB, 0xCD}};
    static const uint8_t filters[1] = {0};
    return emit_png_image("png_gray16.png", 2, 1, 16, 0, &rows[0][0], 4,
                          filters, NULL, 0, 0);
}

static int
emit_png_interlaced(void)
{
    static const uint8_t rows[2][8] = {
        {1, 2, 3, 4, 5, 6, 7, 8}, {9, 10, 11, 12, 13, 14, 15, 16},
    };
    static const uint8_t filters[2] = {0, 0};
    return emit_png_image("png_interlace.png", 2, 2, 8, 6, &rows[0][0], 8,
                          filters, NULL, 0, 1);
}

static int
emit_png_bad_filter(void)
{
    static const uint8_t rows[2][8] = {
        {1, 2, 3, 4, 5, 6, 7, 8}, {9, 10, 11, 12, 13, 14, 15, 16},
    };
    static const uint8_t filters[2] = {5, 0};
    return emit_png_image("png_badfilter.png", 2, 2, 8, 6, &rows[0][0], 8,
                          filters, NULL, 0, 0);
}

/* ---------------------------------------------------------------- tiff */

struct tiff_tag {
    uint16_t tag;
    uint16_t type;
    uint32_t num;
    uint32_t value;
};

static size_t
tiff_ifd_le(uint8_t *p, const struct tiff_tag *t, int n, uint32_t next)
{
    put_le16(p, (uint16_t)n);
    for (int i = 0; i < n; i++) {
        uint8_t *e = p + 2 + i * 12;
        put_le16(e, t[i].tag);
        put_le16(e + 2, t[i].type);
        put_le32(e + 4, t[i].num);
        if (t[i].type == 3 && t[i].num == 1) {
            put_le16(e + 8, (uint16_t)t[i].value);
            put_le16(e + 10, 0);
        } else {
            put_le32(e + 8, t[i].value);
        }
    }
    put_le32(p + 2 + n * 12, next);
    return (size_t)(2 + n * 12 + 4);
}

/* a big endian short value lives in the top half of the inline value field */
static size_t
tiff_ifd_be(uint8_t *p, const struct tiff_tag *t, int n, uint32_t next)
{
    put_be16(p, (uint16_t)n);
    for (int i = 0; i < n; i++) {
        uint8_t *e = p + 2 + i * 12;
        put_be16(e, t[i].tag);
        put_be16(e + 2, t[i].type);
        put_be32(e + 4, t[i].num);
        if (t[i].type == 3 && t[i].num == 1) {
            put_be32(e + 8, t[i].value << 16);
        } else {
            put_be32(e + 8, t[i].value);
        }
    }
    put_be32(p + 2 + n * 12, next);
    return (size_t)(2 + n * 12 + 4);
}

/* packbits with two strips of one row each: a repeat run for the first row
 * and a literal run for the second */
static int
emit_tiff_packbits(void)
{
    enum { TW = 4, TH = 2 };
    static const struct tiff_tag tags[9] = {
        {256, 3, 1, TW}, {257, 3, 1, TH}, {258, 3, 1, 8},
        {259, 3, 1, 32773}, {262, 3, 1, 2}, {273, 4, 2, 122},
        {277, 3, 1, 3}, {278, 4, 1, 1}, {279, 4, 2, 130},
    };
    static const uint8_t literal[12] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    };
    uint8_t file[256];
    struct wbuf w = {file, 0};

    wb_bytes(&w, "II", 2);
    wb_le16(&w, 42);
    wb_le32(&w, 8);
    w.n += tiff_ifd_le(file + w.n, tags, 9, 0);
    {
        uint8_t offsets[8] = {138, 0, 0, 0, 140, 0, 0, 0};
        uint8_t counts[8] = {2, 0, 0, 0, 13, 0, 0, 0};
        wb_bytes(&w, offsets, sizeof(offsets));
        wb_bytes(&w, counts, sizeof(counts));
    }
    wb_u8(&w, 0xF5);
    wb_u8(&w, 0x0A);
    wb_u8(&w, 11);
    wb_bytes(&w, literal, sizeof(literal));
    return write_file("tif_packbits.tif", file, w.n);
}

/* msb first lzw: a clear code, every byte as a literal code, then eoi */
static int
tiff_lzw_encode(uint8_t *out, const uint8_t *src, int n)
{
    int clear = 256, eoi = 257, len = 9;
    int dict = 258, cap = 512;
    int bits = 0;
    int i;

    memset(out, 0, 64);
    for (i = 0; i < len; i++) {
        if ((clear >> (len - 1 - i)) & 1)
            out[bits >> 3] |= (uint8_t)(1u << (7 - (bits & 7)));
        bits++;
    }
    for (int k = 0; k < n; k++) {
        for (i = 0; i < len; i++) {
            if ((src[k] >> (len - 1 - i)) & 1)
                out[bits >> 3] |= (uint8_t)(1u << (7 - (bits & 7)));
            bits++;
        }
        if (k > 0) {
            dict++;
            if (dict == cap - 1 && len < 12) {
                len++;
                cap <<= 1;
            }
        }
    }
    for (i = 0; i < len; i++) {
        if ((eoi >> (len - 1 - i)) & 1)
            out[bits >> 3] |= (uint8_t)(1u << (7 - (bits & 7)));
        bits++;
    }
    return (bits + 7) / 8;
}

/* lzw plus the horizontal predictor, one strip for both rows */
static int
emit_tiff_lzw(void)
{
    enum { TW = 4, TH = 2 };
    static const uint8_t samples[24] = {
        10, 20, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
        15, 25, 35, 30, 30, 30, 30, 30, 30, 30, 30, 30,
    };
    uint8_t packed[64];
    int plen = tiff_lzw_encode(packed, samples, 24);
    uint8_t file[256];
    struct wbuf w = {file, 0};
    struct tiff_tag tags[10] = {
        {256, 3, 1, TW}, {257, 3, 1, TH}, {258, 3, 1, 8},
        {259, 3, 1, 5}, {262, 3, 1, 2}, {273, 4, 1, 134},
        {277, 3, 1, 3}, {278, 4, 1, 2}, {279, 4, 1, 0},
        {317, 3, 1, 2},
    };
    tags[8].value = (uint32_t)plen;

    wb_bytes(&w, "II", 2);
    wb_le16(&w, 42);
    wb_le32(&w, 8);
    w.n += tiff_ifd_le(file + w.n, tags, 10, 0);
    wb_bytes(&w, packed, (size_t)plen);
    return write_file("tif_lzw.tif", file, w.n);
}

/* big endian uncompressed rgb, the inline values have to be justified */
static int
emit_tiff_be(void)
{
    enum { TW = 4, TH = 2 };
    static const struct tiff_tag tags[9] = {
        {256, 3, 1, TW}, {257, 3, 1, TH}, {258, 3, 1, 8},
        {259, 3, 1, 1}, {262, 3, 1, 2}, {273, 4, 1, 122},
        {277, 3, 1, 3}, {278, 4, 1, TH}, {279, 4, 1, 0},
    };
    struct tiff_tag t[9];
    uint8_t file[256];
    struct wbuf w = {file, 0};

    memcpy(t, tags, sizeof(t));
    t[8].value = TW * TH * 3;
    wb_bytes(&w, "MM", 2);
    wb_be16(&w, 42);
    wb_be32(&w, 8);
    w.n += tiff_ifd_be(file + w.n, t, 9, 0);
    for (int y = 0; y < TH; y++) {
        for (int x = 0; x < TW; x++) {
            wb_u8(&w, (unsigned)(10 + x * 30 + y * 5));
            wb_u8(&w, (unsigned)(20 + x * 30 + y * 5));
            wb_u8(&w, (unsigned)(30 + x * 30 + y * 5));
        }
    }
    return write_file("tif_be.tif", file, w.n);
}

/* photometric 1 and 0 with a single sample per pixel */
static int
emit_tiff_bw(const char *name, int photometric)
{
    enum { TW = 4, TH = 1 };
    uint8_t file[256];
    struct wbuf w = {file, 0};
    struct tiff_tag tags[9];

    memset(tags, 0, sizeof(tags));
    tags[0].tag = 256; tags[0].type = 3; tags[0].num = 1; tags[0].value = TW;
    tags[1].tag = 257; tags[1].type = 3; tags[1].num = 1; tags[1].value = TH;
    tags[2].tag = 258; tags[2].type = 3; tags[2].num = 1; tags[2].value = 8;
    tags[3].tag = 259; tags[3].type = 3; tags[3].num = 1; tags[3].value = 1;
    tags[4].tag = 262; tags[4].type = 3; tags[4].num = 1;
    tags[4].value = (uint32_t)photometric;
    tags[5].tag = 273; tags[5].type = 4; tags[5].num = 1; tags[5].value = 122;
    tags[6].tag = 277; tags[6].type = 3; tags[6].num = 1; tags[6].value = 1;
    tags[7].tag = 278; tags[7].type = 4; tags[7].num = 1; tags[7].value = TH;
    tags[8].tag = 279; tags[8].type = 4; tags[8].num = 1; tags[8].value = TW;

    wb_bytes(&w, "II", 2);
    wb_le16(&w, 42);
    wb_le32(&w, 8);
    w.n += tiff_ifd_le(file + w.n, tags, 9, 0);
    wb_u8(&w, 0);
    wb_u8(&w, 64);
    wb_u8(&w, 128);
    wb_u8(&w, 255);
    return write_file(name, file, w.n);
}

/* ----------------------------------------------------------------- gif */

struct lsbw {
    uint8_t *p;
    int bits;
};

static void
lsbw_put(struct lsbw *b, unsigned code, int len)
{
    for (int i = 0; i < len; i++) {
        if ((code >> i) & 1u) {
            b->p[b->bits >> 3] |= (uint8_t)(1u << (b->bits & 7));
        }
        b->bits++;
    }
}

/* literal only lzw stream; the dictionary still fills up the same way the
 * decoder does, so the code width grows at the same code */
static size_t
gif_lzw(uint8_t *out, int codesize, const uint8_t *px, int n)
{
    struct lsbw b = {out, 0};
    int clear = 1 << codesize;
    int eoi = clear + 1;
    int len = codesize + 1;
    int dict = clear + 2;
    int cap = 1 << len;

    memset(out, 0, 64);
    lsbw_put(&b, (unsigned)clear, len);
    if (n > 0) {
        lsbw_put(&b, px[0], len);
    }
    for (int i = 1; i < n; i++) {
        lsbw_put(&b, px[i], len);
        dict++;
        if (dict == cap && len < 12) {
            len++;
            cap <<= 1;
        }
    }
    lsbw_put(&b, (unsigned)eoi, len);
    return (size_t)((b.bits + 7) / 8);
}

static const uint8_t gif_global[4][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
};

static void
gif_table(struct wbuf *w, const uint8_t (*ct)[3], int n)
{
    for (int i = 0; i < n; i++) {
        wb_bytes(w, ct[i], 3);
    }
}

static void
gif_head(struct wbuf *w, int width, int height, int global)
{
    wb_bytes(w, "GIF89a", 6);
    wb_le16(w, (uint16_t)width);
    wb_le16(w, (uint16_t)height);
    /* global table flag plus the size code for four entries */
    wb_u8(w, global ? 0x81 : 0x00);
    wb_u8(w, 0);
    wb_u8(w, 0);
    if (global) {
        gif_table(w, gif_global, 4);
    }
}

static void
gif_image(struct wbuf *w, int width, int height, const uint8_t (*local)[3],
          const uint8_t *px, int npix)
{
    uint8_t data[64];
    size_t len = gif_lzw(data, 2, px, npix);

    wb_u8(w, 0x2C);
    wb_le16(w, 0);
    wb_le16(w, 0);
    wb_le16(w, (uint16_t)width);
    wb_le16(w, (uint16_t)height);
    if (local) {
        wb_u8(w, 0x81);
        gif_table(w, local, 4);
    } else {
        wb_u8(w, 0);
    }
    wb_u8(w, 2);
    wb_u8(w, (unsigned)len);
    wb_bytes(w, data, len);
    wb_u8(w, 0);
}

static int
emit_gif_single(void)
{
    static const uint8_t px[4] = {0, 1, 2, 3};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    gif_head(&w, 2, 2, 1);
    gif_image(&w, 2, 2, NULL, px, 4);
    wb_u8(&w, 0x3B);
    return write_file("gif_single.gif", file, w.n);
}

static int
emit_gif_local(void)
{
    static const uint8_t local[4][3] = {
        {10, 20, 30}, {40, 50, 60}, {70, 80, 90}, {100, 110, 120},
    };
    static const uint8_t px[4] = {3, 2, 1, 0};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    gif_head(&w, 2, 2, 0);
    gif_image(&w, 2, 2, local, px, 4);
    wb_u8(&w, 0x3B);
    return write_file("gif_local.gif", file, w.n);
}

static int
emit_gif_queue(void)
{
    static const uint8_t px1[1] = {1};
    static const uint8_t px2[1] = {2};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    gif_head(&w, 1, 1, 1);
    gif_image(&w, 1, 1, NULL, px1, 1);
    gif_image(&w, 1, 1, NULL, px2, 1);
    wb_u8(&w, 0x3B);
    return write_file("gif_queue.gif", file, w.n);
}

/* comment, application and plaintext blocks before the frame. The plaintext
 * parser consumes one byte past its terminator, so a filler byte separates it
 * from the image separator. */
static int
emit_gif_extensions(void)
{
    static const uint8_t px[4] = {1, 2, 3, 0};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    gif_head(&w, 2, 2, 1);

    wb_u8(&w, 0x21);
    wb_u8(&w, 0xFE);
    wb_u8(&w, 2);
    wb_bytes(&w, "hi", 2);
    wb_u8(&w, 0);

    wb_u8(&w, 0x21);
    wb_u8(&w, 0xFF);
    wb_u8(&w, 11);
    wb_bytes(&w, "NETSCAPE2.0", 11);
    wb_u8(&w, 3);
    wb_u8(&w, 1);
    wb_u8(&w, 0);
    wb_u8(&w, 0);
    wb_u8(&w, 0);

    wb_u8(&w, 0x21);
    wb_u8(&w, 0xF9);
    wb_u8(&w, 4);
    wb_u8(&w, 0);
    wb_le16(&w, 0);
    wb_u8(&w, 0);
    wb_u8(&w, 0);

    wb_u8(&w, 0x21);
    wb_u8(&w, 0x01);
    wb_u8(&w, 12);
    wb_le16(&w, 0);
    wb_le16(&w, 0);
    wb_le16(&w, 2);
    wb_le16(&w, 1);
    wb_u8(&w, 8);
    wb_u8(&w, 8);
    wb_u8(&w, 0);
    wb_u8(&w, 1);
    wb_u8(&w, 2);
    wb_bytes(&w, "ab", 2);
    wb_u8(&w, 0);
    wb_u8(&w, 0);

    gif_image(&w, 2, 2, NULL, px, 4);
    wb_u8(&w, 0x3B);
    return write_file("gif_ext.gif", file, w.n);
}

static int
emit_gif_no_color_table(void)
{
    static const uint8_t px[4] = {0, 1, 2, 3};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    gif_head(&w, 2, 2, 0);
    gif_image(&w, 2, 2, NULL, px, 4);
    wb_u8(&w, 0x3B);
    return write_file("gif_noct.gif", file, w.n);
}

static int
emit_gif_bad_code_size(void)
{
    static const uint8_t px[4] = {0, 1, 2, 3};
    uint8_t file[256];
    struct wbuf w = {file, 0};
    uint8_t data[64];
    size_t len = gif_lzw(data, 2, px, 4);

    gif_head(&w, 2, 2, 1);
    wb_u8(&w, 0x2C);
    wb_le16(&w, 0);
    wb_le16(&w, 0);
    wb_le16(&w, 2);
    wb_le16(&w, 2);
    wb_u8(&w, 0);
    wb_u8(&w, 1);
    wb_u8(&w, (unsigned)len);
    wb_bytes(&w, data, len);
    wb_u8(&w, 0);
    wb_u8(&w, 0x3B);
    return write_file("gif_badcode.gif", file, w.n);
}

/* the lzw stream only holds three of the four pixels the frame needs */
static int
emit_gif_short(void)
{
    static const uint8_t px[3] = {0, 1, 2};
    uint8_t file[256];
    struct wbuf w = {file, 0};
    uint8_t data[64];
    size_t len = gif_lzw(data, 2, px, 3);

    gif_head(&w, 2, 2, 1);
    wb_u8(&w, 0x2C);
    wb_le16(&w, 0);
    wb_le16(&w, 0);
    wb_le16(&w, 2);
    wb_le16(&w, 2);
    wb_u8(&w, 0);
    wb_u8(&w, 2);
    wb_u8(&w, (unsigned)len);
    wb_bytes(&w, data, len);
    wb_u8(&w, 0);
    wb_u8(&w, 0x3B);
    return write_file("gif_short.gif", file, w.n);
}

/* ----------------------------------------------------------------- jpg */

static void
jpg_marker(struct wbuf *w, unsigned code)
{
    wb_u8(w, 0xFF);
    wb_u8(w, code);
}

static void
jpg_length(struct wbuf *w, uint16_t len)
{
    wb_be16(w, len);
}

static void
jpg_dqt(struct wbuf *w)
{
    jpg_marker(w, 0xDB);
    jpg_length(w, 67);
    wb_u8(w, 0);
    for (int i = 0; i < 64; i++) {
        wb_u8(w, 1);
    }
}

/* one huffman table with a single symbol of length one, every code is a zero
 * bit, which keeps the hand written entropy stream trivial */
static void
jpg_dht(struct wbuf *w, int class, int id, unsigned symbol)
{
    jpg_marker(w, 0xC4);
    jpg_length(w, 20);
    wb_u8(w, (unsigned)(class << 4 | id));
    wb_u8(w, 1);
    for (int i = 1; i < 16; i++) {
        wb_u8(w, 0);
    }
    wb_u8(w, symbol);
}

static void
jpg_sof(struct wbuf *w, int width, int height, int ncomp, const uint8_t *hv)
{
    jpg_marker(w, 0xC0);
    jpg_length(w, (uint16_t)(8 + 3 * ncomp));
    wb_u8(w, 8);
    wb_be16(w, (uint16_t)height);
    wb_be16(w, (uint16_t)width);
    wb_u8(w, (unsigned)ncomp);
    for (int i = 0; i < ncomp; i++) {
        wb_u8(w, (unsigned)(i + 1));
        wb_u8(w, hv[i]);
        wb_u8(w, 0);
    }
}

static void
jpg_sos(struct wbuf *w, int ncomp)
{
    jpg_marker(w, 0xDA);
    jpg_length(w, (uint16_t)(6 + 2 * ncomp));
    wb_u8(w, (unsigned)ncomp);
    for (int i = 0; i < ncomp; i++) {
        wb_u8(w, (unsigned)(i + 1));
        wb_u8(w, 0);
    }
    wb_u8(w, 0);
    wb_u8(w, 63);
    wb_u8(w, 0);
}

static void
jpg_app0(struct wbuf *w)
{
    jpg_marker(w, 0xE0);
    jpg_length(w, 16);
    wb_bytes(w, "JFIF\0", 5);
    wb_u8(w, 1);
    wb_u8(w, 1);
    wb_u8(w, 0);
    wb_be16(w, 72);
    wb_be16(w, 72);
    wb_u8(w, 0);
    wb_u8(w, 0);
}

static size_t
jpg_build(uint8_t *out, int width, int height, int ncomp, const uint8_t *hv,
          const uint8_t *tail, int taillen, int dri, int app0, int comment)
{
    struct wbuf w = {out, 0};

    jpg_marker(&w, 0xD8);
    if (app0) {
        jpg_app0(&w);
    }
    jpg_dqt(&w);
    jpg_sof(&w, width, height, ncomp, hv);
    jpg_dht(&w, 0, 0, 0);
    jpg_dht(&w, 1, 0, 0x00);
    if (comment) {
        jpg_marker(&w, 0xFE);
        jpg_length(&w, 4);
        wb_bytes(&w, "hi", 2);
    }
    if (dri) {
        jpg_marker(&w, 0xDD);
        jpg_length(&w, 4);
        wb_be16(&w, 1);
    }
    jpg_sos(&w, ncomp);
    wb_bytes(&w, tail, (size_t)taillen);
    jpg_marker(&w, 0xD9);
    return w.n;
}

static int
emit_jpg_gray(void)
{
    static const uint8_t hv[1] = {0x11};
    static const uint8_t tail[2] = {0x00, 0x00};
    uint8_t file[512];

    size_t n = jpg_build(file, 8, 8, 1, hv, tail, 2, 0, 0, 0);
    return write_file("jpg_gray.jpg", file, n);
}

static int
emit_jpg_444(void)
{
    static const uint8_t hv[3] = {0x11, 0x11, 0x11};
    static const uint8_t tail[2] = {0x00, 0x00};
    uint8_t file[512];

    size_t n = jpg_build(file, 8, 8, 3, hv, tail, 2, 0, 1, 0);
    return write_file("jpg_444.jpg", file, n);
}

static int
emit_jpg_420(void)
{
    static const uint8_t hv[3] = {0x22, 0x11, 0x11};
    static const uint8_t tail[3] = {0x00, 0x00, 0x00};
    uint8_t file[512];

    size_t n = jpg_build(file, 16, 16, 3, hv, tail, 3, 0, 0, 0);
    return write_file("jpg_420.jpg", file, n);
}

/* two mcus with a restart interval of one: the decoder has to align to the
 * next byte and strip the rst marker from the entropy stream */
static int
emit_jpg_dri(void)
{
    static const uint8_t hv[3] = {0x11, 0x11, 0x11};
    static const uint8_t tail[5] = {0x00, 0xFF, 0xD0, 0x00, 0x00};
    uint8_t file[512];

    size_t n = jpg_build(file, 16, 8, 3, hv, tail, 5, 1, 0, 1);
    return write_file("jpg_dri.jpg", file, n);
}

static int
emit_jpg_queue(void)
{
    static const uint8_t hv[3] = {0x11, 0x11, 0x11};
    static const uint8_t tail[2] = {0x00, 0x00};
    uint8_t file[1024];

    size_t n = jpg_build(file, 8, 8, 3, hv, tail, 2, 0, 0, 0);
    n += jpg_build(file + n, 8, 8, 3, hv, tail, 2, 0, 0, 0);
    return write_file("jpg_queue.jpg", file, n);
}

/* every zero block idct's to 128, and the neutral chroma keeps it there */
static int
check_jpg_neutral(const char *name, int width, int height)
{
    struct file_ops *ops = file_probe(name);
    struct pic *p;
    int rc = 0;

    if (!ops || strcmp(ops->name, "JPG")) {
        printf("%s: not detected as JPG\n", name);
        return -1;
    }
    p = file_load(ops, name, 0);
    if (!p) {
        printf("%s: did not decode\n", name);
        return -1;
    }
    if (p->width != width || p->height != height || !p->pixels) {
        printf("%s: decoded %dx%d, expected %dx%d\n", name, p->width,
               p->height, width, height);
        rc = -1;
    } else {
        const uint8_t *q = p->pixels;
        for (int y = 0; y < p->height && !rc; y++) {
            for (int x = 0; x < p->width; x++) {
                const uint8_t *v = q + (size_t)y * p->pitch + (size_t)x * 4;
                if (v[0] != 128 || v[1] != 128 || v[2] != 128 || v[3] != 0xFF) {
                    printf("%s: (%d,%d) is bgra %d %d %d %d, expected 128 128 "
                           "128 255\n", name, x, y, v[0], v[1], v[2], v[3]);
                    rc = -1;
                    break;
                }
            }
        }
    }
    file_free(ops, p);
    remove(name);
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* two concatenated streams come back through the frame queue */
static int
check_jpg_queue(void)
{
    struct file_ops *ops = file_probe("jpg_queue.jpg");
    struct pic *p[2] = {NULL, NULL};
    int rc = 0;

    if (!ops || strcmp(ops->name, "JPG")) {
        printf("jpg_queue.jpg: not detected as JPG\n");
        return -1;
    }
    if (file_load(ops, "jpg_queue.jpg", 0) != NULL) {
        printf("jpg_queue.jpg: expected the frames to go to the queue\n");
        rc = -1;
    } else {
        p[0] = file_dequeue_pic();
        p[1] = file_dequeue_pic();
        if (!p[0] || !p[1] || file_dequeue_pic() != NULL) {
            printf("jpg_queue.jpg: expected two queued frames\n");
            rc = -1;
        }
        for (int i = 0; i < 2 && !rc; i++) {
            const uint8_t *q;
            if (p[i]->width != 8 || p[i]->height != 8 || !p[i]->pixels) {
                printf("jpg_queue.jpg: frame %d is %dx%d\n", i, p[i]->width,
                       p[i]->height);
                rc = -1;
                break;
            }
            q = p[i]->pixels;
            if (q[0] != 128 || q[1] != 128 || q[2] != 128 || q[3] != 0xFF) {
                printf("jpg_queue.jpg: frame %d starts with %d %d %d %d\n", i,
                       q[0], q[1], q[2], q[3]);
                rc = -1;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        if (p[i]) {
            file_free(ops, p[i]);
        }
    }
    remove("jpg_queue.jpg");
    if (!rc) {
        printf("jpg_queue.jpg: ok\n");
    }
    return rc;
}

/* ----------------------------------------------------------------- ico */

static void
ico_head(struct wbuf *w, int num)
{
    wb_le16(w, 0);
    wb_le16(w, 1);
    wb_le16(w, (uint16_t)num);
}

static void
ico_dir(struct wbuf *w, int width, int height, int depth, uint32_t size,
        uint32_t offset)
{
    wb_u8(w, (unsigned)width);
    wb_u8(w, (unsigned)height);
    wb_u8(w, 0);
    wb_u8(w, 0);
    wb_le16(w, 1);
    wb_le16(w, (uint16_t)depth);
    wb_le32(w, size);
    wb_le32(w, offset);
}

static void
ico_bmpinfo(struct wbuf *w, int width, int height, int bpp,
            uint32_t size_image, uint32_t colors)
{
    wb_le32(w, 40);
    wb_le32(w, (uint32_t)width);
    wb_le32(w, (uint32_t)height);
    wb_le16(w, 1);
    wb_le16(w, (uint16_t)bpp);
    wb_le32(w, 0);
    wb_le32(w, size_image);
    wb_le32(w, 0);
    wb_le32(w, 0);
    wb_le32(w, colors);
    wb_le32(w, 0);
}

/* 8 bit entry: the palette comes from the dib header because the directory
 * colour count is zero. width four makes the and mask zero bytes wide. */
static int
emit_ico8(void)
{
    static const uint8_t pal[4][4] = {
        {10, 20, 30, 255}, {40, 50, 60, 255},
        {70, 80, 90, 255}, {100, 110, 120, 255},
    };
    static const uint8_t rows[2][4] = {{3, 2, 1, 0}, {0, 1, 2, 3}};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    ico_head(&w, 1);
    ico_dir(&w, 4, 2, 8, 64, 22);
    ico_bmpinfo(&w, 4, 4, 8, 8, 4);
    for (int i = 0; i < 4; i++) {
        wb_bytes(&w, pal[i], 4);
    }
    for (int r = 0; r < 2; r++) {
        wb_bytes(&w, rows[r], 4);
    }
    return write_file("ico8.ico", file, w.n);
}

/* two entries, the 32 bit one has to be selected over the 8 bit one */
static int
emit_ico_select(void)
{
    static const uint8_t pal[4][4] = {
        {10, 20, 30, 255}, {40, 50, 60, 255},
        {70, 80, 90, 255}, {100, 110, 120, 255},
    };
    static const uint8_t rows8[2][4] = {{3, 2, 1, 0}, {0, 1, 2, 3}};
    uint8_t file[256];
    struct wbuf w = {file, 0};

    ico_head(&w, 2);
    ico_dir(&w, 4, 2, 8, 64, 38);
    ico_dir(&w, 4, 2, 32, 72, 102);

    ico_bmpinfo(&w, 4, 4, 8, 8, 4);
    for (int i = 0; i < 4; i++) {
        wb_bytes(&w, pal[i], 4);
    }
    for (int r = 0; r < 2; r++) {
        wb_bytes(&w, rows8[r], 4);
    }

    ico_bmpinfo(&w, 4, 4, 32, 32, 0);
    for (int y = 1; y >= 0; y--) {
        for (int x = 0; x < 4; x++) {
            wb_u8(&w, (unsigned)(10 + x * 20 + y * 3));
            wb_u8(&w, (unsigned)(40 + x * 20 + y * 5));
            wb_u8(&w, (unsigned)(70 + x * 20 + y * 7));
            wb_u8(&w, 255);
        }
    }
    return write_file("ico32.ico", file, w.n);
}

static int
emit_ico_reject(void)
{
    uint8_t file[128];
    struct wbuf w = {file, 0};

    ico_head(&w, 1);
    ico_dir(&w, 4, 2, 32, 32, 22);
    /* a height that halves to zero rows can not be decoded */
    ico_bmpinfo(&w, 4, 0, 32, 32, 0);
    return write_file("ico_bad.ico", file, w.n);
}

/* ----------------------------------------------------------------- exr */

static void
exr_attr(struct wbuf *w, const char *name, const char *type,
         const uint8_t *val, uint32_t len)
{
    wb_bytes(w, name, strlen(name) + 1);
    wb_bytes(w, type, strlen(type) + 1);
    wb_le32(w, len);
    wb_bytes(w, val, len);
}

/* three channels in the order they are stored, so the memory layout ends up
 * as blue, green, red */
static size_t
exr_chlist(uint8_t *out, int pixel_type)
{
    static const char names[3] = {'B', 'G', 'R'};
    struct wbuf w = {out, 0};

    for (int i = 0; i < 3; i++) {
        wb_u8(&w, (unsigned)names[i]);
        wb_u8(&w, 0);
        wb_le32(&w, (uint32_t)pixel_type);
        wb_le32(&w, 0);
        wb_le32(&w, 1);
        wb_le32(&w, 1);
    }
    wb_u8(&w, 0);
    return w.n;
}

static float
exr_sample(int channel, int y)
{
    static const float v[2][3] = {
        {0.25f, 0.5f, 1.0f},
        {1.0f, 0.25f, 0.5f},
    };
    return v[y][channel];
}

static uint16_t
exr_half(float f)
{
    if (f == 1.0f) {
        return 0x3C00;
    }
    if (f == 0.5f) {
        return 0x3800;
    }
    return 0x3400;
}

static int
emit_exr(const char *name, int pixel_type, int lineorder, int with_comment)
{
    enum { EW = 2, EH = 2 };
    uint8_t file[1024];
    uint8_t val[128];
    struct wbuf w = {file, 0};
    struct wbuf v = {val, 0};
    size_t offtab;

    wb_u8(&w, 0x76);
    wb_u8(&w, 0x2F);
    wb_u8(&w, 0x31);
    wb_u8(&w, 0x01);
    wb_le32(&w, 2);

    exr_attr(&w, "channels", "chlist", val, (uint32_t)exr_chlist(val, pixel_type));

    v.n = 0;
    wb_le32(&v, 0);
    wb_le32(&v, 0);
    wb_le32(&v, EW - 1);
    wb_le32(&v, EH - 1);
    exr_attr(&w, "dataWindow", "box2i", val, 16);
    exr_attr(&w, "displayWindow", "box2i", val, 16);

    v.n = 0;
    wb_u8(&v, (unsigned)lineorder);
    exr_attr(&w, "lineOrder", "lineOrder", val, 1);

    v.n = 0;
    wb_u8(&v, 0);
    exr_attr(&w, "compression", "compression", val, 1);

    v.n = 0;
    wb_le32(&v, 0x3F800000u);
    exr_attr(&w, "pixelAspectRatio", "float", val, 4);
    exr_attr(&w, "screenWindowWidth", "float", val, 4);

    v.n = 0;
    wb_le32(&v, 0);
    wb_le32(&v, 0);
    exr_attr(&w, "screenWindowCenter", "v2f", val, 8);

    if (with_comment) {
        /* an attribute the parser does not know has to be skipped */
        exr_attr(&w, "comments", "string", (const uint8_t *)"hi", 2);
    }

    wb_u8(&w, 0);

    offtab = w.n;
    wb_zero(&w, (size_t)EH * 8);
    for (int y = 0; y < EH; y++) {
        size_t pos = w.n;
        for (int k = 0; k < 8; k++) {
            file[offtab + (size_t)y * 8 + (size_t)k] =
                (uint8_t)(pos >> (8 * k));
        }
        wb_le32(&w, (uint32_t)(3 * EW * (pixel_type == 1 ? 2 : 4)));
        for (int c = 0; c < 3; c++) {
            for (int x = 0; x < EW; x++) {
                float f = exr_sample(c, y);
                if (pixel_type == 1) {
                    wb_le16(&w, exr_half(f));
                } else {
                    uint32_t u;
                    memcpy(&u, &f, sizeof(u));
                    wb_le32(&w, u);
                }
                (void)x;
            }
        }
    }

    return write_file(name, file, w.n);
}

static int
emit_exr_multipart(void)
{
    uint8_t file[16];
    struct wbuf w = {file, 0};

    wb_u8(&w, 0x76);
    wb_u8(&w, 0x2F);
    wb_u8(&w, 0x31);
    wb_u8(&w, 0x01);
    wb_le32(&w, 2u | (1u << 12));
    wb_u8(&w, 0);
    return write_file("exr_multi.exr", file, w.n);
}

/* -------------------------------------------------------- probe only */

static int
emit_probe_jp2(void)
{
    uint8_t file[12];
    struct wbuf w = {file, 0};

    wb_be32(&w, 12);
    wb_bytes(&w, "jP  ", 4);
    wb_zero(&w, 4);
    return write_file("probe.jp2", file, w.n);
}

static int
emit_probe_bpg(void)
{
    static const uint8_t file[6] = {0x42, 0x50, 0x47, 0xFB, 0, 0};
    return write_file("probe.bpg", file, sizeof(file));
}

static int
emit_probe_psd(void)
{
    uint8_t file[26];
    struct wbuf w = {file, 0};

    wb_bytes(&w, "8BPS", 4);
    wb_be16(&w, 1);
    wb_zero(&w, 6);
    wb_be16(&w, 4);
    wb_be32(&w, 32);
    wb_be32(&w, 32);
    wb_be16(&w, 8);
    wb_be16(&w, 3);
    return write_file("probe.psd", file, w.n);
}

static int
emit_probe_svg(void)
{
    static const char file[] =
        "<?xml version=\"1.0\" standalone=\"no\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\"\n"
        "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n"
        "<svg width=\"1\" height=\"1\"/>\n";
    return write_file("probe.svg", (const uint8_t *)file, sizeof(file) - 1);
}

static int
emit_probe_webp(void)
{
    uint8_t file[12];
    struct wbuf w = {file, 0};

    wb_bytes(&w, "RIFF", 4);
    wb_le32(&w, 4);
    wb_bytes(&w, "WEBP", 4);
    return write_file("probe.webp", file, w.n);
}

static int
emit_probe_not_webp(void)
{
    uint8_t file[12];
    struct wbuf w = {file, 0};

    wb_bytes(&w, "RIFF", 4);
    wb_le32(&w, 4);
    wb_bytes(&w, "WAVE", 4);
    return write_file("notwebp.webp", file, w.n);
}

/* ------------------------------------------------------------- queues */

static int
check_gif_queued(const char *name, int width, int height,
                 const struct want_px *px, int npx)
{
    struct file_ops *ops = file_probe(name);
    struct pic *p;
    int rc = 0;

    if (!ops || strcmp(ops->name, "GIF")) {
        printf("%s: not detected as GIF\n", name);
        return -1;
    }
    if (file_load(ops, name, 0) != NULL) {
        printf("%s: expected the frame to go to the queue\n", name);
        return -1;
    }
    p = file_dequeue_pic();
    if (!p) {
        printf("%s: no frame in the queue\n", name);
        return -1;
    }
    if (file_dequeue_pic() != NULL) {
        printf("%s: more than one frame was queued\n", name);
        rc = -1;
    }
    if (!rc && (p->width != width || p->height != height || !p->pixels)) {
        printf("%s: got %dx%d, expected %dx%d\n", name, p->width, p->height,
               width, height);
        rc = -1;
    }
    for (int i = 0; i < npx && !rc; i++) {
        const uint8_t *q = p->pixels;
        int got = q[px[i].row * p->pitch + px[i].off];
        if (abs(got - px[i].want) > 0) {
            printf("%s: (%d, off %d) is %d, expected %d\n", name, px[i].row,
                   px[i].off, got, px[i].want);
            rc = -1;
        }
    }
    file_free(ops, p);
    remove(name);
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* two one pixel frames are queued and the last one owns the gif, so freeing
 * it releases both pixel buffers */
static int
check_gif_queue(void)
{
    struct file_ops *ops = file_probe("gif_queue.gif");
    struct pic *p[2] = {NULL, NULL};
    int rc = 0;

    if (!ops || strcmp(ops->name, "GIF")) {
        printf("gif_queue.gif: not detected as GIF\n");
        return -1;
    }
    if (file_load(ops, "gif_queue.gif", 0) != NULL) {
        printf("gif_queue.gif: expected the frames to go to the queue\n");
        rc = -1;
    } else {
        p[0] = file_dequeue_pic();
        p[1] = file_dequeue_pic();
        if (!p[0] || !p[1] || file_dequeue_pic() != NULL) {
            printf("gif_queue.gif: expected two queued frames\n");
            rc = -1;
        }
        if (!rc && (p[0]->width != 1 || p[1]->width != 1)) {
            printf("gif_queue.gif: frames are %dx%d and %dx%d\n", p[0]->width,
                   p[0]->height, p[1]->width, p[1]->height);
            rc = -1;
        }
        if (!rc) {
            const uint8_t *a = p[0]->pixels;
            const uint8_t *b = p[1]->pixels;
            /* first frame is index 1 (green), second index 2 (blue) */
            if (a[0] != 0 || a[1] != 255 || a[2] != 0 || a[3] != 255) {
                printf("gif_queue.gif: first frame is %d %d %d %d\n", a[0],
                       a[1], a[2], a[3]);
                rc = -1;
            } else if (b[0] != 255 || b[1] != 0 || b[2] != 0 || b[3] != 255) {
                printf("gif_queue.gif: second frame is %d %d %d %d\n", b[0],
                       b[1], b[2], b[3]);
                rc = -1;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        if (p[i]) {
            file_free(ops, p[i]);
        }
    }
    remove("gif_queue.gif");
    if (!rc) {
        printf("gif_queue.gif: ok\n");
    }
    return rc;
}

int main(void)
{
    int failed = 0;

    file_ops_init();

    /* bitmap */
    if (emit_bmp_os2() || check_pixels("bmp_os2.bmp", "BMP", 4, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 20}, {0, 2, 30},
                                     {0, 9, 100}, {0, 10, 110}, {0, 11, 120},
                                     {1, 0, 100}, {1, 1, 110}, {1, 2, 120},
                                     {1, 9, 10}, {1, 10, 20}, {1, 11, 30}},
            12, 0)) {
        failed++;
    }
    if (emit_bmp_td24() || check_pixels("bmp_td24.bmp", "BMP", 4, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 40}, {0, 2, 70},
                                     {0, 9, 13}, {0, 10, 70}, {0, 11, 100},
                                     {1, 0, 11}, {1, 1, 41}, {1, 2, 71},
                                     {1, 9, 14}, {1, 10, 71}, {1, 11, 101}},
            12, 0)) {
        failed++;
    }
    if (emit_bmp_bitfields() || check_pixels("bmp_bitfields.bmp", "BMP", 4, 2,
            (const struct want_px[]){{0, 0, 20}, {0, 1, 60}, {0, 2, 100},
                                     {0, 3, 255}, {0, 12, 50}, {0, 13, 90},
                                     {0, 14, 130}, {0, 15, 255}, {1, 0, 21},
                                     {1, 1, 61}, {1, 2, 101}, {1, 3, 255},
                                     {1, 12, 51}, {1, 13, 91}, {1, 14, 131},
                                     {1, 15, 255}},
            16, 0)) {
        failed++;
    }
    if (emit_bmp_rle8() || check_pixels("bmp_rle8.bmp", "BMP", 4, 2,
            (const struct want_px[]){{1, 0, 0}, {1, 1, 255}, {1, 2, 0},
                                     {1, 9, 0}, {1, 10, 0}, {1, 11, 0},
                                     {0, 0, 0}, {0, 1, 0}, {0, 2, 255},
                                     {0, 3, 255}, {0, 4, 255}, {0, 5, 255},
                                     {0, 9, 0}, {0, 10, 0}, {0, 11, 255}},
            15, 0)) {
        failed++;
    }
    if (emit_bmp_rle4() || check_pixels("bmp_rle4.bmp", "BMP", 4, 2,
            (const struct want_px[]){{1, 0, 10}, {1, 1, 245}, {1, 2, 17},
                                     {1, 3, 20}, {1, 4, 235}, {1, 5, 34},
                                     {1, 6, 0}, {1, 7, 0}, {1, 8, 0},
                                     {0, 0, 30}, {0, 1, 225}, {0, 2, 51},
                                     {0, 3, 0}, {0, 4, 255}, {0, 5, 0},
                                     {0, 9, 20}, {0, 10, 235}, {0, 11, 34}},
            18, 0)) {
        failed++;
    }

    /* netpbm */
    if (emit_pnm_p1() || check_pixels("pnm_p1.pbm", "PNM", 2, 2,
            (const struct want_px[]){{0, 0, 255}, {0, 1, 255}, {0, 2, 255},
                                     {0, 4, 0}, {0, 5, 0}, {0, 6, 0},
                                     {1, 0, 0}, {1, 1, 0}, {1, 2, 0},
                                     {1, 4, 255}, {1, 5, 255}, {1, 6, 255}},
            12, 0)) {
        failed++;
    }
    if (emit_pnm_p2() || check_pixels("pnm_p2.pgm", "PNM", 2, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 10}, {0, 2, 10},
                                     {0, 4, 20}, {0, 5, 20}, {0, 6, 20},
                                     {1, 0, 30}, {1, 1, 30}, {1, 2, 30},
                                     {1, 4, 40}, {1, 5, 40}, {1, 6, 40}},
            12, 0)) {
        failed++;
    }
    if (emit_pnm_p3() || check_pixels("pnm_p3.ppm", "PNM", 2, 2,
            (const struct want_px[]){{0, 0, 3}, {0, 1, 2}, {0, 2, 1},
                                     {0, 4, 6}, {0, 5, 5}, {0, 6, 4},
                                     {1, 0, 9}, {1, 1, 8}, {1, 2, 7},
                                     {1, 4, 12}, {1, 5, 11}, {1, 6, 10}},
            12, 0)) {
        failed++;
    }
    if (emit_pnm_p4() || check_pixels("pnm_p4.pbm", "PNM", 2, 2,
            (const struct want_px[]){{0, 0, 0}, {0, 4, 255},
                                     {1, 0, 255}, {1, 4, 0}},
            4, 0)) {
        failed++;
    }
    if (emit_pnm_p5() || check_pixels("pnm_p5.pgm", "PNM", 4, 1,
            (const struct want_px[]){{0, 0, 0}, {0, 1, 0}, {0, 2, 0},
                                     {0, 4, 85}, {0, 5, 85}, {0, 6, 85},
                                     {0, 12, 255}, {0, 13, 255}, {0, 14, 255}},
            9, 0)) {
        failed++;
    }
    if (emit_pnm_p6() || check_pixels("pnm_p6.ppm", "PNM", 2, 2,
            (const struct want_px[]){{0, 0, 3}, {0, 1, 2}, {0, 2, 1},
                                     {0, 4, 6}, {0, 5, 5}, {0, 6, 4},
                                     {1, 0, 9}, {1, 1, 8}, {1, 2, 7},
                                     {1, 4, 12}, {1, 5, 11}, {1, 6, 10}},
            12, 0)) {
        failed++;
    }
    if (emit_pnm_p7_rgb() || check_pixels("pnm_p7_rgb.pam", "PNM", 2, 1,
            (const struct want_px[]){{0, 0, 3}, {0, 1, 2}, {0, 2, 1},
                                     {0, 4, 6}, {0, 5, 5}, {0, 6, 4}},
            6, 0)) {
        failed++;
    }
    if (emit_pnm_p7_bw() || check_pixels("pnm_p7_bw.pam", "PNM", 2, 2,
            (const struct want_px[]){{0, 0, 0}, {0, 4, 255},
                                     {1, 0, 255}, {1, 4, 0}},
            4, 0)) {
        failed++;
    }
    if (emit_pnm_bad_version() || check_claim("pnm_p9.pnm", NULL)) {
        failed++;
    }

    /* tga */
    if (emit_tga32() || check_pixels("tga32.tga", "TGA", 4, 2,
            (const struct want_px[]){{0, 0, 1}, {0, 1, 2}, {0, 2, 3},
                                     {0, 3, 4}, {0, 12, 91}, {0, 13, 89},
                                     {0, 14, 87}, {0, 15, 85}, {1, 0, 4},
                                     {1, 1, 7}, {1, 2, 10}, {1, 3, 13},
                                     {1, 12, 94}, {1, 13, 94}, {1, 14, 94},
                                     {1, 15, 94}},
            16, 0)) {
        failed++;
    }
    if (emit_tga16() || check_pixels("tga16.tga", "TGA", 4, 2,
            (const struct want_px[]){{0, 0, 0}, {0, 1, 2}, {0, 2, 1},
                                     {0, 3, 0}, {0, 12, 21}, {0, 13, 17},
                                     {0, 14, 10}, {0, 15, 0}, {1, 0, 3},
                                     {1, 1, 2}, {1, 2, 1}, {1, 3, 0},
                                     {1, 12, 24}, {1, 13, 17}, {1, 14, 10},
                                     {1, 15, 0}},
            16, 0)) {
        failed++;
    }
    if (emit_tga_indexed() || check_pixels("tga_idx.tga", "TGA", 4, 2,
            (const struct want_px[]){{0, 0, 77}, {0, 1, 88}, {0, 2, 99},
                                     {0, 4, 44}, {0, 5, 55}, {0, 6, 66},
                                     {0, 12, 77}, {0, 13, 88}, {0, 14, 99},
                                     {1, 0, 11}, {1, 1, 22}, {1, 2, 33},
                                     {1, 8, 77}, {1, 9, 88}, {1, 10, 99}},
            15, 0)) {
        failed++;
    }
    if (emit_tga_rle() || check_pixels("tga_rle.tga", "TGA", 4, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 20}, {0, 2, 30},
                                     {0, 12, 100}, {0, 13, 110}, {0, 14, 120},
                                     {1, 0, 51}, {1, 1, 102}, {1, 2, 153},
                                     {1, 12, 51}, {1, 13, 102}, {1, 14, 153}},
            12, 0)) {
        failed++;
    }

    /* png */
    if (emit_png_rgba() || check_pixels("png_rgba.png", "PNG", 2, 2,
            (const struct want_px[]){{0, 0, 1}, {0, 1, 2}, {0, 2, 3},
                                     {0, 3, 4}, {0, 4, 5}, {0, 5, 6},
                                     {0, 6, 7}, {0, 7, 8}, {1, 0, 9},
                                     {1, 1, 10}, {1, 2, 11}, {1, 3, 12},
                                     {1, 4, 13}, {1, 5, 14}, {1, 6, 15},
                                     {1, 7, 16}},
            16, 0)) {
        failed++;
    }
    if (emit_png_rgb() || check_pixels("png_rgb.png", "PNG", 2, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 20}, {0, 2, 30},
                                     {0, 3, 40}, {0, 4, 50}, {0, 5, 60},
                                     {0, 6, 0}, {0, 7, 0}, {1, 0, 70},
                                     {1, 1, 80}, {1, 2, 90}, {1, 3, 100},
                                     {1, 4, 110}, {1, 5, 120}, {1, 6, 0},
                                     {1, 7, 0}},
            16, 0)) {
        failed++;
    }
    if (emit_png_gray() || check_pixels("png_gray.png", "PNG", 4, 2,
            (const struct want_px[]){{0, 0, 5}, {0, 1, 6}, {0, 2, 7},
                                     {0, 3, 8}, {1, 0, 9}, {1, 1, 10},
                                     {1, 2, 11}, {1, 3, 12}},
            8, 0)) {
        failed++;
    }
    if (emit_png_indexed() || check_pixels("png_idx.png", "PNG", 8, 1,
            (const struct want_px[]){{0, 0, 0xAA}, {0, 1, 0}, {0, 2, 0},
                                     {0, 3, 0}},
            4, 0)) {
        failed++;
    }
    if (emit_png_gray16() || check_pixels("png_gray16.png", "PNG", 2, 1,
            (const struct want_px[]){{0, 0, 0x12}, {0, 1, 0x34},
                                     {0, 2, 0xAB}, {0, 3, 0xCD}},
            4, 0)) {
        failed++;
    }
    if (emit_png_interlaced() || check_reject("png_interlace.png", "PNG")) {
        failed++;
    }
    if (emit_png_bad_filter() || check_reject("png_badfilter.png", "PNG")) {
        failed++;
    }

    /* tiff */
    if (emit_tiff_packbits() || check_pixels("tif_packbits.tif", "TIFF", 4, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 10}, {0, 2, 10},
                                     {0, 12, 10}, {0, 13, 10}, {0, 14, 10},
                                     {1, 0, 3}, {1, 1, 2}, {1, 2, 1},
                                     {1, 12, 12}, {1, 13, 11}, {1, 14, 10}},
            12, 0)) {
        failed++;
    }
    if (emit_tiff_lzw() || check_pixels("tif_lzw.tif", "TIFF", 4, 2,
            (const struct want_px[]){{0, 0, 30}, {0, 1, 20}, {0, 2, 10},
                                     {0, 12, 120}, {0, 13, 110}, {0, 14, 100},
                                     {1, 0, 35}, {1, 1, 25}, {1, 2, 15},
                                     {1, 12, 125}, {1, 13, 115}, {1, 14, 105}},
            12, 0)) {
        failed++;
    }
    if (emit_tiff_be() || check_pixels("tif_be.tif", "TIFF", 4, 2,
            (const struct want_px[]){{0, 0, 30}, {0, 1, 20}, {0, 2, 10},
                                     {0, 12, 120}, {0, 13, 110}, {0, 14, 100},
                                     {1, 0, 35}, {1, 1, 25}, {1, 2, 15},
                                     {1, 12, 125}, {1, 13, 115}, {1, 14, 105}},
            12, 0)) {
        failed++;
    }
    if (emit_tiff_bw("tif_black.tif", 1) ||
        check_pixels("tif_black.tif", "TIFF", 4, 1,
            (const struct want_px[]){{0, 0, 0}, {0, 1, 0}, {0, 2, 0},
                                     {0, 4, 64}, {0, 5, 64}, {0, 6, 64},
                                     {0, 12, 255}, {0, 13, 255}, {0, 14, 255}},
            9, 0)) {
        failed++;
    }
    if (emit_tiff_bw("tif_white.tif", 0) ||
        check_pixels("tif_white.tif", "TIFF", 4, 1,
            (const struct want_px[]){{0, 0, 255}, {0, 1, 255}, {0, 2, 255},
                                     {0, 4, 191}, {0, 5, 191}, {0, 6, 191},
                                     {0, 12, 0}, {0, 13, 0}, {0, 14, 0}},
            9, 0)) {
        failed++;
    }

    /* gif */
    if (emit_gif_single() || check_pixels("gif_single.gif", "GIF", 2, 2,
            (const struct want_px[]){{0, 0, 0}, {0, 1, 0}, {0, 2, 255},
                                     {0, 3, 255}, {0, 4, 0}, {0, 5, 255},
                                     {0, 6, 0}, {0, 7, 255}, {1, 0, 255},
                                     {1, 1, 0}, {1, 2, 0}, {1, 3, 255},
                                     {1, 4, 255}, {1, 5, 255}, {1, 6, 255},
                                     {1, 7, 255}},
            16, 0)) {
        failed++;
    }
    if (emit_gif_local() || check_pixels("gif_local.gif", "GIF", 2, 2,
            (const struct want_px[]){{0, 0, 120}, {0, 1, 110}, {0, 2, 100},
                                     {0, 3, 255}, {0, 4, 90}, {0, 5, 80},
                                     {0, 6, 70}, {0, 7, 255}, {1, 0, 60},
                                     {1, 1, 50}, {1, 2, 40}, {1, 3, 255},
                                     {1, 4, 30}, {1, 5, 20}, {1, 6, 10},
                                     {1, 7, 255}},
            16, 0)) {
        failed++;
    }
    if (emit_gif_queue() || check_gif_queue()) {
        failed++;
    }
    if (emit_gif_extensions() || check_gif_queued("gif_ext.gif", 2, 2,
            (const struct want_px[]){{0, 0, 0}, {0, 1, 255}, {0, 2, 0},
                                     {0, 4, 255}, {0, 5, 0}, {0, 6, 0},
                                     {1, 0, 255}, {1, 1, 255}, {1, 2, 255},
                                     {1, 4, 0}, {1, 5, 0}, {1, 6, 255}},
            12)) {
        failed++;
    }
    if (emit_gif_no_color_table() ||
        check_reject("gif_noct.gif", "GIF")) {
        failed++;
    }
    if (emit_gif_bad_code_size() ||
        check_reject("gif_badcode.gif", "GIF")) {
        failed++;
    }
    if (emit_gif_short() || check_reject("gif_short.gif", "GIF")) {
        failed++;
    }

    /* jpeg */
    if (emit_jpg_gray() || check_pixels("jpg_gray.jpg", "JPG", 8, 8, NULL, 0,
                                        0)) {
        failed++;
    }
    if (emit_jpg_444() || check_jpg_neutral("jpg_444.jpg", 8, 8)) {
        failed++;
    }
    if (emit_jpg_420() || check_jpg_neutral("jpg_420.jpg", 16, 16)) {
        failed++;
    }
    if (emit_jpg_dri() || check_jpg_neutral("jpg_dri.jpg", 16, 8)) {
        failed++;
    }
    if (emit_jpg_queue() || check_jpg_queue()) {
        failed++;
    }

    /* ico */
    if (emit_ico8() || check_pixels("ico8.ico", "ICO", 4, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 20}, {0, 2, 30},
                                     {0, 3, 255}, {0, 12, 100}, {0, 13, 110},
                                     {0, 14, 120}, {0, 15, 255}, {1, 0, 100},
                                     {1, 1, 110}, {1, 2, 120}, {1, 3, 255},
                                     {1, 12, 10}, {1, 13, 20}, {1, 14, 30},
                                     {1, 15, 255}},
            16, 0)) {
        failed++;
    }
    if (emit_ico_select() || check_pixels("ico32.ico", "ICO", 4, 2,
            (const struct want_px[]){{0, 0, 10}, {0, 1, 40}, {0, 2, 70},
                                     {0, 3, 255}, {0, 12, 70}, {0, 13, 100},
                                     {0, 14, 130}, {0, 15, 255}, {1, 0, 13},
                                     {1, 1, 45}, {1, 2, 77}, {1, 3, 255},
                                     {1, 12, 73}, {1, 13, 105}, {1, 14, 137},
                                     {1, 15, 255}},
            16, 0)) {
        failed++;
    }
    if (emit_ico_reject() || check_reject("ico_bad.ico", "ICO")) {
        failed++;
    }

    /* exr */
    if (emit_exr("exr_half.exr", 1, 0, 1) ||
        check_pixels("exr_half.exr", "EXR", 2, 2,
            (const struct want_px[]){{0, 0, 136}, {0, 1, 187}, {0, 2, 254},
                                     {0, 4, 136}, {0, 5, 187}, {0, 6, 254},
                                     {1, 0, 254}, {1, 1, 136}, {1, 2, 187},
                                     {1, 4, 254}, {1, 5, 136}, {1, 6, 187}},
            12, 2)) {
        failed++;
    }
    if (emit_exr("exr_float.exr", 2, 1, 0) ||
        check_pixels("exr_float.exr", "EXR", 2, 2,
            (const struct want_px[]){{0, 0, 136}, {0, 1, 187}, {0, 2, 254},
                                     {0, 4, 136}, {0, 5, 187}, {0, 6, 254},
                                     {1, 0, 254}, {1, 1, 136}, {1, 2, 187},
                                     {1, 4, 254}, {1, 5, 136}, {1, 6, 187}},
            12, 2)) {
        failed++;
    }
    if (emit_exr_multipart() || check_reject("exr_multi.exr", "EXR")) {
        failed++;
    }

    /* probes without a decoder on this build */
    if (emit_probe_jp2() || check_claim("probe.jp2", "JP2")) {
        failed++;
    }
    if (emit_probe_bpg() || check_claim("probe.bpg", "BPG")) {
        failed++;
    }
    if (emit_probe_psd() || check_claim("probe.psd", "PSD")) {
        failed++;
    }
    if (emit_probe_svg() || check_claim("probe.svg", "SVG")) {
        failed++;
    }
    if (emit_probe_webp() || check_claim("probe.webp", "WEBP")) {
        failed++;
    }
    if (emit_probe_not_webp() || check_claim("notwebp.webp", NULL)) {
        failed++;
    }

    file_ops_deinit();
    return failed ? -1 : 0;
}
