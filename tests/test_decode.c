/* Decode a set of small images built here, byte by byte, so the format
 * parsers get exercised under the sanitizer build. The streams are written
 * from hand rather than with the encoders in this repository, so a broken
 * encoder cannot hide a broken decoder.
 *
 * The check is that the right parser was picked and that it produced an
 * image of the right size without touching memory it does not own. Pixel
 * values are deliberately not compared: they are the job of the format
 * tests, and pinning them here would make a decoder fix look like a
 * regression. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "utils.h"

#define W 4
#define H 2

static uint8_t
pixel_r(int x, int y) { return (uint8_t)((x * 40 + y) & 0xFF); }
static uint8_t
pixel_g(int x UNUSED, int y) { return (uint8_t)((y * 80) & 0xFF); }
static uint8_t
pixel_b(int x, int y) { return (uint8_t)(((x + y) * 30) & 0xFF); }

static void
put_le16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

static void
put_le32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = v >> 24;
}

static void
put_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* written out rather than reused from utils/crc.c, which is under test here */
static uint32_t
crc32(const uint8_t *buf, int len) {
    uint32_t c = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        c ^= buf[i];
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : c >> 1;
    }
    return c ^ 0xFFFFFFFF;
}

static uint32_t
adler32(const uint8_t *buf, int len) {
    uint32_t a = 1, b = 0;
    for (int i = 0; i < len; i++) {
        a += buf[i];
        b += a;
    }
    return (b << 16) | a;
}

static int
write_file(const char *name, const uint8_t *data, size_t len) {
    FILE *f = fopen(name, "wb");
    if (!f) {
        printf("cannot write %s\n", name);
        return -1;
    }
    size_t done = fwrite(data, 1, len, f);
    fclose(f);
    return done == len ? 0 : -1;
}

/* probe picks a parser by content, then decode it and check what came back */
static int
check(const char *name, const char *expect, int width, int height) {
    struct file_ops *ops = file_probe(name);
    if (!ops) {
        printf("%s: no format claimed it, expected %s\n", name, expect);
        return -1;
    }
    if (strcmp(ops->name, expect)) {
        printf("%s: probed as %s, expected %s\n", name, ops->name, expect);
        return -1;
    }

    struct pic *p = file_load(ops, name, 0);
    if (!p) {
        printf("%s: %s could not decode it\n", name, expect);
        return -1;
    }
    if (p->width != width || p->height != height) {
        printf("%s: got %dx%d, expected %dx%d\n", name, p->width, p->height,
               width, height);
        file_free(ops, p);
        return -1;
    }
    if (!p->pixels) {
        printf("%s: decoded with no pixels\n", name);
        file_free(ops, p);
        return -1;
    }

    file_free(ops, p);
    remove(name);
    printf("%s: ok\n", name);
    return 0;
}

static int
emit_bmp(void) {
    int row = W * 3;
    int pad = (4 - row % 4) % 4;
    size_t size = (size_t)(row + pad) * H;
    uint8_t *img = calloc(1, size);
    /* positive height means the rows are stored bottom up */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *d = img + (H - 1 - y) * (row + pad) + x * 3;
            d[0] = pixel_b(x, y);
            d[1] = pixel_g(x, y);
            d[2] = pixel_r(x, y);
        }
    }

    uint8_t f[54];
    memcpy(f, "BM", 2);
    put_le32(f + 2, (uint32_t)(54 + size));
    put_le32(f + 10, 54);
    put_le32(f + 14, 40);
    put_le32(f + 18, W);
    put_le32(f + 22, H);
    put_le16(f + 26, 1);
    put_le16(f + 28, 24);
    put_le32(f + 34, (uint32_t)size);

    uint8_t *file = malloc(54 + size);
    memcpy(file, f, 54);
    memcpy(file + 54, img, size);
    int rc = write_file("decode.bmp", file, 54 + size);
    free(img);
    free(file);
    return rc;
}

static int
emit_pbm(void) {
    /* 2x2, one packed byte per row, a set bit is black */
    static const uint8_t file[] = {'P', '4', '\n', '2', ' ', '2', '\n',
                                   0xAA, 0x55};
    return write_file("decode.pbm", file, sizeof(file));
}

static int
emit_pgm(void) {
    char file[16 + W * H];
    size_t n = (size_t)sprintf(file, "P5\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            file[n++] = pixel_r(x, y);
        }
    }
    return write_file("decode.pgm", (const uint8_t *)file, n);
}

static int
emit_ppm(void) {
    char file[16 + W * H * 3];
    size_t n = (size_t)sprintf(file, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            file[n++] = pixel_r(x, y);
            file[n++] = pixel_g(x, y);
            file[n++] = pixel_b(x, y);
        }
    }
    return write_file("decode.ppm", (const uint8_t *)file, n);
}

static int
emit_tga(void) {
    /* the probe only accepts a tga that carries the 2.0 extension footer */
    uint8_t footer[26];
    memset(footer, 0, sizeof(footer));
    memcpy(footer + 8, "TRUEVISION-XFILE", 16);
    footer[24] = '.';

    uint8_t file[18 + W * H * 3 + sizeof(footer)];
    memset(file, 0, sizeof(file));
    file[2] = 2;                  /* uncompressed true colour */
    put_le16(file + 12, W);
    put_le16(file + 14, H);
    file[16] = 24;                /* bits per pixel */

    uint8_t *body = file + 18;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            body[(y * W + x) * 3 + 0] = pixel_b(x, y);
            body[(y * W + x) * 3 + 1] = pixel_g(x, y);
            body[(y * W + x) * 3 + 2] = pixel_r(x, y);
        }
    }
    memcpy(body + W * H * 3, footer, sizeof(footer));
    return write_file("decode.tga", file, sizeof(file));
}

static void
png_chunk(uint8_t *p, const char *type, const uint8_t *data, uint32_t len) {
    put_be32(p, len);
    memcpy(p + 4, type, 4);
    if (len) {
        memcpy(p + 8, data, len);
    }
    put_be32(p + 8 + len, crc32(p + 4, (int)(len + 4)));
}

static int
emit_png(void) {
    /* truecolour 8 bit, every scanline filter type 0 */
    uint32_t rawlen = (uint32_t)H * (1 + W * 3);
    uint8_t raw[1 + W * 3];
    /* zlib header, block header, len and nlen, the raw rows, then adler32 */
    uint8_t *deflate = malloc(2 + 1 + 4 + rawlen + 4);
    size_t n = 0;

    deflate[n++] = 0x78;          /* zlib header, 32k window */
    deflate[n++] = 0x01;          /* check bits, no preset dictionary */
    deflate[n++] = 0x01;          /* final block, stored */
    put_le16(deflate + n, (uint16_t)rawlen);
    n += 2;
    put_le16(deflate + n, (uint16_t)(rawlen ^ 0xFFFF));
    n += 2;

    uint32_t adler_input_pos = n;
    for (int y = 0; y < H; y++) {
        raw[0] = 0;
        for (int x = 0; x < W; x++) {
            raw[1 + x * 3 + 0] = pixel_r(x, y);
            raw[1 + x * 3 + 1] = pixel_g(x, y);
            raw[1 + x * 3 + 2] = pixel_b(x, y);
        }
        memcpy(deflate + n, raw, sizeof(raw));
        n += sizeof(raw);
    }
    put_be32(deflate + n, adler32(deflate + adler_input_pos, (int)rawlen));
    n += 4;

    uint8_t ihdr[13];
    put_be32(ihdr, W);
    put_be32(ihdr + 4, H);
    ihdr[8] = 8;                  /* bit depth */
    ihdr[9] = 2;                  /* colour type, truecolour */
    memset(ihdr + 10, 0, 3);

    size_t len = 8 + 12 + 13 + 12 + n + 12;
    uint8_t *file = malloc(len);
    size_t at = 0;
    memcpy(file, "\x89PNG\r\n\x1a\n", 8);
    at += 8;
    png_chunk(file + at, "IHDR", ihdr, sizeof(ihdr));
    at += 12 + sizeof(ihdr);
    png_chunk(file + at, "IDAT", deflate, (uint32_t)n);
    at += 12 + n;
    png_chunk(file + at, "IEND", NULL, 0);
    at += 12;

    int rc = write_file("decode.png", file, at);
    free(deflate);
    free(file);
    return rc;
}

static int
emit_tiff(void) {
    /* little endian, uncompressed rgb, every IFD value short enough to be
     * stored inline in the 4 byte value field */
    uint32_t datalen = (uint32_t)(W * H * 3);
    uint32_t dataoff = 8 + 2 + 12 * 9 + 4;
    /* tag, type, count, value */
    static const uint32_t de[9][4] = {
        {256, 3, 1, W},          /* image width */
        {257, 3, 1, H},          /* image length */
        {258, 3, 1, 8},          /* bits per sample */
        {259, 3, 1, 1},          /* no compression */
        {262, 3, 1, 2},          /* photometric rgb */
        {273, 4, 1, 0},          /* strip offsets, filled in below */
        {277, 3, 1, 3},          /* samples per pixel */
        {278, 3, 1, H},          /* rows per strip */
        {279, 4, 1, 0},          /* strip byte counts, filled in below */
    };

    size_t len = dataoff + datalen;
    uint8_t *file = calloc(1, len);
    memcpy(file, "II", 2);
    put_le16(file + 2, 42);
    put_le32(file + 4, 8);
    put_le16(file + 8, 9);
    for (int i = 0; i < 9; i++) {
        uint8_t *e = file + 10 + i * 12;
        uint32_t value = de[i][3];
        if (de[i][0] == 273) {
            value = dataoff;
        }
        if (de[i][0] == 279) {
            value = datalen;
        }
        put_le16(e, (uint16_t)de[i][0]);
        put_le16(e + 2, (uint16_t)de[i][1]);
        put_le32(e + 4, de[i][2]);
        put_le32(e + 8, value);
    }

    uint8_t *body = file + dataoff;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            body[(y * W + x) * 3 + 0] = pixel_r(x, y);
            body[(y * W + x) * 3 + 1] = pixel_g(x, y);
            body[(y * W + x) * 3 + 2] = pixel_b(x, y);
        }
    }

    int rc = write_file("decode.tif", file, len);
    free(file);
    return rc;
}

int main(void)
{
    int failed = 0;

    file_ops_init();

    struct {
        int (*emit)(void);
        const char *name;
        const char *format;
    } cases[] = {
        {emit_bmp, "decode.bmp", "BMP"},
        {emit_pbm, "decode.pbm", "PNM"},
        {emit_pgm, "decode.pgm", "PNM"},
        {emit_ppm, "decode.ppm", "PNM"},
        {emit_tga, "decode.tga", "TGA"},
        {emit_png, "decode.png", "PNG"},
        {emit_tiff, "decode.tif", "TIFF"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].emit()) {
            printf("%s: could not write\n", cases[i].name);
            failed++;
            continue;
        }
        /* pbm is 2x2 so the rest of the fixtures keep their own size */
        int width = strcmp(cases[i].name, "decode.pbm") ? W : 2;
        int height = strcmp(cases[i].name, "decode.pbm") ? H : 2;
        if (check(cases[i].name, cases[i].format, width, height)) {
            failed++;
        }
    }

    file_ops_deinit();
    return failed ? -1 : 0;
}
