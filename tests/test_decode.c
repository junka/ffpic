/* Decode a set of small images built here, byte by byte, so the format
 * parsers get exercised under the sanitizer build. The streams are written
 * from hand rather than with the encoders in this repository, so a broken
 * encoder cannot hide a broken decoder.
 *
 * The check is that the right parser was picked and that it produced an
 * image of the right size without touching memory it does not own. Pixel
 * values are deliberately not compared for the hand written streams: they
 * only carry a container. The one exception is data/quad.heic, a real HEVC
 * stream, where the decoded colours are the whole point. */

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
put_be16(uint8_t *p, uint16_t v) {
    p[0] = v >> 8;
    p[1] = v & 0xFF;
}

static void
put_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void
put_box_header(uint8_t *p, const char *type, uint32_t size) {
    put_be32(p, size);
    memcpy(p + 4, type, 4);
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

/* Probe only. AVIF has no AV1 decoder yet and HEIF needs a real HEVC slice,
 * neither of which can be written out by hand here, so these pin format
 * detection. A NULL expect means no parser may claim the file. */
static int
check_claim(const char *name, const char *expect) {
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

/* A file the parser recognises but cannot turn into a picture must be
 * rejected cleanly: no abort, no garbage pic handed back. */
static int
check_reject(const char *name, const char *expect) {
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
    size_t n = (size_t)snprintf(file, sizeof(file), "P5\n%d %d\n255\n", W, H);
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
    size_t n = (size_t)snprintf(file, sizeof(file), "P6\n%d %d\n255\n", W, H);
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

/* the probe reads bytes 4 and 8 as a box type and a brand, so both have to
 * carry the fourcc the loader looks for */
static void
put_avif_ftyp(uint8_t *p) {
    put_box_header(p, "ftyp", 24);
    memcpy(p + 8, "avif", 4);      /* major brand */
    put_be32(p + 12, 0);           /* minor version */
    memcpy(p + 16, "mif1", 4);     /* compatible brands */
    memcpy(p + 20, "avif", 4);
}

static int
emit_avif(void) {
    /* AVIF has no AV1 decoder here, so decode_av01 only walks the sample
     * length and the pixels come back untouched. What this pins is the meta
     * box parse: item table, property list and the ispe driven size. */
    enum { FTYP = 24, META = 117, MDAT = 12, TOTAL = FTYP + META + MDAT };
    uint8_t *file = calloc(1, TOTAL);
    size_t at = FTYP;

    put_avif_ftyp(file);

    put_box_header(file + at, "meta", META);
    put_be32(file + at + 8, 0);            /* version and flags */

    /* iinf: one item, type av01 */
    put_box_header(file + at + 12, "iinf", 35);
    put_be32(file + at + 20, 0);           /* version and flags */
    put_be16(file + at + 24, 1);           /* entry_count */
    put_box_header(file + at + 26, "infe", 21);
    put_be32(file + at + 34, 2 << 24);     /* version 2, flags 0 */
    put_be16(file + at + 38, 1);           /* item_id */
    put_be16(file + at + 40, 0);           /* item_protection_index */
    memcpy(file + at + 42, "av01", 4);
    /* the item name is the empty string, feed the null terminator */
    at += 12 + 35;

    /* iloc: version 0, 4 byte offsets and lengths, one extent in mdat */
    put_box_header(file + at, "iloc", 34);
    put_be32(file + at + 8, 0);            /* version and flags */
    file[at + 12] = 0x44;                  /* offset size 4, length size 4 */
    file[at + 13] = 0x40;                  /* base offset size 4, no index */
    put_be16(file + at + 14, 1);           /* item_count */
    put_be16(file + at + 16, 1);           /* item_id */
    put_be16(file + at + 18, 0);           /* data_ref_id */
    put_be32(file + at + 20, FTYP + META + 8); /* base offset, mdat payload */
    put_be16(file + at + 24, 1);           /* extent_count */
    put_be32(file + at + 26, 0);           /* extent_offset */
    put_be32(file + at + 30, 4);           /* extent_length */
    at += 34;

    /* iprp: a single ispe property, which is where the size comes from */
    put_box_header(file + at, "iprp", 36);
    put_box_header(file + at + 8, "ipco", 28);
    put_box_header(file + at + 16, "ispe", 20);
    put_be32(file + at + 24, 0);           /* version and flags */
    put_be32(file + at + 28, W);
    put_be32(file + at + 32, H);
    at += 36;

    /* mdat last: the top level loop does not seek past it, only its size
     * has to land on the end of the file */
    put_box_header(file + at, "mdat", MDAT);
    at += MDAT;

    int rc = write_file("decode.avif", file, TOTAL);
    free(file);
    return rc;
}

static int
emit_avif_probe(void) {
    uint8_t file[24];
    put_avif_ftyp(file);
    return write_file("probe.avif", file, sizeof(file));
}

static int
emit_heif_probe(void) {
    /* a short ftyp whose only brand is heic */
    uint8_t file[16];
    put_box_header(file, "ftyp", 16);
    memcpy(file + 8, "heic", 4);
    put_be32(file + 12, 0);
    return write_file("probe.heic", file, sizeof(file));
}

static int
emit_heif_probe_mif1(void) {
    /* mif1 files are only accepted when a heif brand is listed after it */
    uint8_t file[24];
    put_box_header(file, "ftyp", 24);
    memcpy(file + 8, "mif1", 4);
    put_be32(file + 12, 0);
    memcpy(file + 16, "heic", 4);
    memcpy(file + 20, "mif1", 4);
    return write_file("probe_mif1.heic", file, sizeof(file));
}

static int
emit_ftyp_truncated(void) {
    /* ftyp promises two compatible brands but the file stops after one */
    uint8_t file[20];
    put_box_header(file, "ftyp", 24);
    memcpy(file + 8, "mif1", 4);
    put_be32(file + 12, 0);
    memcpy(file + 16, "heic", 4);
    return write_file("trunc.heic", file, sizeof(file));
}

static int
emit_heif_grid(void) {
    /* a grid item whose extent is shorter than the 8 byte grid header: the
     * loader has to reject it rather than abort on the size check */
    enum { FTYP = 24, META = 151, MDAT = 14, TOTAL = FTYP + META + MDAT };
    uint8_t *file = calloc(1, TOTAL);
    size_t at = FTYP;

    put_box_header(file, "ftyp", FTYP);
    memcpy(file + 8, "mif1", 4);
    put_be32(file + 12, 0);
    memcpy(file + 16, "heic", 4);
    memcpy(file + 20, "mif1", 4);

    put_box_header(file + at, "meta", META);
    put_be32(file + at + 8, 0);
    at += 12;

    put_box_header(file + at, "pitm", 14);
    put_be32(file + at + 8, 0);
    put_be16(file + at + 12, 1);           /* the grid item is primary */
    at += 14;

    /* iinf: one item of type grid */
    put_box_header(file + at, "iinf", 35);
    put_be32(file + at + 8, 0);
    put_be16(file + at + 12, 1);           /* entry_count */
    put_box_header(file + at + 14, "infe", 21);
    put_be32(file + at + 22, 2 << 24);     /* version 2, flags 0 */
    put_be16(file + at + 26, 1);           /* item_id */
    put_be16(file + at + 28, 0);           /* item_protection_index */
    memcpy(file + at + 30, "grid", 4);
    at += 35;

    /* iloc: one item, one 6 byte extent in mdat */
    put_box_header(file + at, "iloc", 34);
    put_be32(file + at + 8, 0);
    file[at + 12] = 0x44;
    file[at + 13] = 0x40;
    put_be16(file + at + 14, 1);           /* item_count */
    put_be16(file + at + 16, 1);           /* item_id */
    put_be16(file + at + 18, 0);           /* data_ref_id */
    put_be32(file + at + 20, FTYP + META + 8);
    put_be16(file + at + 24, 1);           /* extent_count */
    put_be32(file + at + 26, 0);           /* extent_offset */
    put_be32(file + at + 30, 6);           /* extent_length */
    at += 34;

    /* iprp: an ispe for the grid item, associated through ipma */
    put_box_header(file + at, "iprp", 56);
    put_box_header(file + at + 8, "ipco", 28);
    put_box_header(file + at + 16, "ispe", 20);
    put_be32(file + at + 24, 0);
    put_be32(file + at + 28, W);
    put_be32(file + at + 32, H);
    put_box_header(file + at + 36, "ipma", 20);
    put_be32(file + at + 44, 0);           /* version 0, flags 0 */
    put_be32(file + at + 48, 1);           /* entry_count */
    put_be16(file + at + 52, 1);           /* item_id */
    file[at + 54] = 1;                     /* association_count */
    file[at + 55] = 1;                     /* property index 1 */
    at += 56;

    put_box_header(file + at, "mdat", MDAT);
    at += MDAT;

    int rc = write_file("reject.heic", file, TOTAL);
    free(file);
    return rc;
}

/* data/quad.heic is a 32x32 HEVC still produced with
 *     sips -s format heic quad.ppm --out quad.heic
 * from four 16x16 quadrants: red, green, blue, white. Lossy coding moves the
 * values a little, so compare with a tolerance instead of exact bytes. */
static int
check_heif_sample(void) {
    const char *name = TEST_DATA_DIR "/quad.heic";
    static const int xs[4] = {8, 24, 8, 24};
    static const int ys[4] = {8, 8, 24, 24};
    static const int want[4][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
    };
    enum { TOL = 56 };
    int rc = 0;

    struct file_ops *ops = file_probe(name);
    if (!ops || strcmp(ops->name, "HEIF")) {
        printf("%s: not detected as HEIF\n", name);
        return -1;
    }

    struct pic *p = file_load(ops, name, 0);
    if (!p) {
        printf("%s: did not decode\n", name);
        return -1;
    }
    if (p->width != 32 || p->height != 32) {
        printf("%s: decoded as %dx%d, expected 32x32\n", name, p->width,
               p->height);
        rc = -1;
    } else {
        for (int i = 0; i < 4; i++) {
            uint8_t *q = (uint8_t *)p->pixels + ys[i] * p->pitch + xs[i] * 4;
            int b = q[0], g = q[1], r = q[2], a = q[3];
            if (abs(r - want[i][0]) > TOL || abs(g - want[i][1]) > TOL ||
                abs(b - want[i][2]) > TOL) {
                printf("%s: (%d,%d) is bgra %d %d %d %d, expected %d %d %d\n",
                       name, xs[i], ys[i], b, g, r, a, want[i][2], want[i][1],
                       want[i][0]);
                rc = -1;
            } else if (a != 0xFF) {
                printf("%s: (%d,%d) alpha %d, expected 255\n", name, xs[i],
                       ys[i], a);
                rc = -1;
            }
        }
    }

    file_free(ops, p);
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* data/quad_grid.heic is data/quad.heic wrapped in a 2x2 grid: the four dimg
 * tiles all point at the same 32x32 stream, so every 32x32 cell of the 64x64
 * canvas has to show the red, green, blue and white quadrants again. */
static int
check_heif_grid(void) {
    const char *name = TEST_DATA_DIR "/quad_grid.heic";
    static const int quad[4][2] = {{8, 8}, {24, 8}, {8, 24}, {24, 24}};
    static const int want[4][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
    };
    enum { TOL = 56, CELL = 32 };
    int rc = 0;

    struct file_ops *ops = file_probe(name);
    if (!ops || strcmp(ops->name, "HEIF")) {
        printf("%s: not detected as HEIF\n", name);
        return -1;
    }

    /* a grid is one derived image, not one frame per tile */
    struct pic *p = file_load(ops, name, 0);
    if (!p) {
        printf("%s: did not decode\n", name);
        return -1;
    }
    if (p->width != 2 * CELL || p->height != 2 * CELL) {
        printf("%s: decoded as %dx%d, expected %dx%d\n", name, p->width,
               p->height, 2 * CELL, 2 * CELL);
        rc = -1;
    } else {
        for (int cy = 0; cy < 2 && !rc; cy++) {
            for (int cx = 0; cx < 2 && !rc; cx++) {
                for (int q = 0; q < 4; q++) {
                    int x = cx * CELL + quad[q][0];
                    int y = cy * CELL + quad[q][1];
                    uint8_t *px = (uint8_t *)p->pixels +
                                  (size_t)y * p->pitch + (size_t)x * 4;
                    int b = px[0], g = px[1], r = px[2], a = px[3];
                    if (abs(r - want[q][0]) > TOL ||
                        abs(g - want[q][1]) > TOL ||
                        abs(b - want[q][2]) > TOL || a != 0xFF) {
                        printf("%s: cell (%d,%d) pixel (%d,%d) is bgra "
                               "%d %d %d %d, expected %d %d %d 255\n",
                               name, cx, cy, x, y, b, g, r, a,
                               want[q][2], want[q][1], want[q][0]);
                        rc = -1;
                        break;
                    }
                }
            }
        }
    }

    file_free(ops, p);
    if (file_dequeue_pic() != NULL) {
        printf("%s: a grid was queued as extra frames\n", name);
        rc = -1;
    }
    if (!rc) {
        printf("%s: ok\n", name);
    }
    return rc;
}

/* the ispe box is what the container allocates for; a stream whose sps crops
 * to a larger picture has to be refused instead of overrunning it */
static int
check_heif_small_ispe(void)
{
    const char *src = TEST_DATA_DIR "/quad.heic";
    const char *name = "small_ispe.heic";
    FILE *f = fopen(src, "rb");
    uint8_t *buf;
    long size;
    int found = 0;
    int rc = 0;

    if (!f) {
        printf("%s: cannot open\n", src);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    for (long i = 4; i + 16 <= size; i++) {
        if (memcmp(buf + i, "ispe", 4) == 0) {
            /* version and flags sit between the type and the two sizes */
            put_be32(buf + i + 8, 16);
            put_be32(buf + i + 12, 16);
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("%s: no ispe box\n", src);
        free(buf);
        return -1;
    }
    if (write_file(name, buf, (size_t)size)) {
        free(buf);
        return -1;
    }
    free(buf);

    struct file_ops *ops = file_probe(name);
    if (!ops || strcmp(ops->name, "HEIF")) {
        printf("%s: not detected as HEIF\n", name);
        rc = -1;
    } else {
        struct pic *p = file_load(ops, name, 0);
        if (p) {
            printf("%s: decoded a picture larger than its ispe\n", name);
            file_free(ops, p);
            rc = -1;
        }
        if (file_dequeue_pic() != NULL) {
            printf("%s: a frame was queued\n", name);
            rc = -1;
        }
    }
    remove(name);
    if (!rc) {
        printf("%s: ok\n", name);
    }
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
        {emit_avif, "decode.avif", "AVIF"},
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

    struct {
        int (*emit)(void);
        const char *name;
        const char *format;
        int reject;
    } probes[] = {
        {emit_avif_probe, "probe.avif", "AVIF", 0},
        {emit_heif_probe, "probe.heic", "HEIF", 0},
        {emit_heif_probe_mif1, "probe_mif1.heic", "HEIF", 0},
        {emit_ftyp_truncated, "trunc.heic", NULL, 0},
        {emit_heif_grid, "reject.heic", "HEIF", 1},
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (probes[i].emit()) {
            printf("%s: could not write\n", probes[i].name);
            failed++;
            continue;
        }
        int rc = probes[i].reject ?
                 check_reject(probes[i].name, probes[i].format) :
                 check_claim(probes[i].name, probes[i].format);
        if (rc) {
            failed++;
        }
    }

    if (check_heif_sample()) {
        failed++;
    }

    if (check_heif_grid()) {
        failed++;
    }

    if (check_heif_small_ispe()) {
        failed++;
    }

    file_ops_deinit();
    return failed ? -1 : 0;
}
