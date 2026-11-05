/* Robustness regression tests for JPG / BMP malformed input handling.
 *
 * Every test case constructs a byte buffer inline, writes it into a temp
 * file, then asks the library to decode it.  We assert that every one of
 * the four P0 bugs fixed earlier is now cleanly rejected -- no crash,
 * no hang (enforced by alarm()), no leakage.
 *
 * We also cover the palette-BMP pitch bug that the fix incidentally
 * restored, with tiny legitimate BMP payloads where we can check pixel
 * values directly. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#include "file.h"

static jmp_buf g_alarm_env;
static volatile sig_atomic_t g_alarm_fired;

static void on_alarm(int sig)
{
    (void)sig;
    g_alarm_fired = 1;
    longjmp(g_alarm_env, 1);
}

/* Run `fn` with `sec` seconds of wall-clock budget; returns 0 on success,
 * 1 on timeout, whatever fn returns otherwise.  On timeout SIGALRM fires
 * and we longjmp out of fn, so fn must not hold locks or be in the middle
 * of a system call -- it's only decode_heavy work here. */
static int with_timeout(int sec, int (*fn)(void *), void *arg)
{
    struct sigaction sa = {0};
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    g_alarm_fired = 0;
    alarm((unsigned)sec);
    int rc = 0;
    int jumped = setjmp(g_alarm_env);
    if (jumped == 0) {
        rc = fn(arg);
        alarm(0);
        sigaction(SIGALRM,
                  &(struct sigaction){ .sa_handler = SIG_DFL }, NULL);
    } else {
        /* longjmp landed here because alarm() fired */
        rc = 2;
    }
    sigaction(SIGALRM, &(struct sigaction){ .sa_handler = SIG_DFL }, NULL);
    return rc;
}

/* -------------------------------------------------------------------- */

static char *
write_temp(const char *name_tpl, const uint8_t *buf, size_t n)
{
    char path[L_tmpnam + 32];
    snprintf(path, sizeof(path), "/tmp/%s", name_tpl);
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    if (w != n) { unlink(path); return NULL; }
    return strdup(path);
}

/* helper: write a 2-byte big-endian value (JPEG markers use BE) */
static void w16be(uint8_t *p, unsigned off, unsigned v)
{
    p[off] = (uint8_t)(v >> 8);
    p[off + 1] = (uint8_t)(v & 0xFF);
}

/* helper: write a 2-byte little-endian value (BMP DIB header is LE) */
static void w16le(uint8_t *p, unsigned off, unsigned v)
{
    p[off] = (uint8_t)(v & 0xFF);
    p[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

/* helper: write a 4-byte little-endian value */
static void w32le(uint8_t *p, unsigned off, unsigned v)
{
    p[off] = (uint8_t)(v & 0xFF);
    p[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    p[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    p[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

/* -------------------------------------------------------------------- */
/* Test 1: JPEG SOF width=height=0x7FFFFFFF -- must reject before any   */
/*         overflowed multiplication reaches malloc.                    */

static int test_jpg_sof_huge_dims(void *unused)
{
    (void)unused;
    /* SOI + SOF0 + components=1 + colors[1] + SOS-minimal + EOI.
     * The library reads components_num right after width/height;
     * components_num stays 1 (safe), but width/height are 65535 --
     * both the probe and the old load path would walk into the bug. */
    uint8_t buf[64];
    unsigned n = 0;
    buf[n++] = 0xFF; buf[n++] = 0xD8;                  /* SOI */
    buf[n++] = 0xFF; buf[n++] = 0xC0;                  /* SOF0 */
    w16be(buf, n, 2 + 1 + 2 + 2 + 1 + 6); n += 2;      /* len */
    buf[n++] = 8;                                      /* precision */
    w16be(buf, n, 0x7FFFFFFF & 0xFFFF); n += 2;        /* height (uint16 in file) */
    w16be(buf, n, 0x7FFFFFFF & 0xFFFF); n += 2;        /* width */
    buf[n++] = 1;                                      /* components_num=1 */
    /* colors[1]: cid=1, hv=0x11, qt=0 */
    buf[n++] = 1; buf[n++] = 0x11; buf[n++] = 0;
    buf[n++] = 0xFF; buf[n++] = 0xD9;                  /* EOI (no SOS, will cleanly not decode) */

    char *path = write_temp("rob_jpg_sof_huge.jpg", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    struct pic *p = ops ? file_load(ops, path, 0) : NULL;
    free(path);
    if (p) {
        printf("  FAIL: huge dims returned a pic instead of NULL\n");
        file_free(ops, p);
        return -1;
    }
    printf("  ok: huge dims rejected cleanly\n");
    return 0;
}

/* Test 2a: JPEG components_num=250 -- overruns colors[4] array */

static int test_jpg_sof_components_250(void *unused)
{
    (void)unused;
    uint8_t buf[16 + 250 * 6];
    unsigned n = 0;
    buf[n++] = 0xFF; buf[n++] = 0xD8;
    buf[n++] = 0xFF; buf[n++] = 0xC0;
    w16be(buf, n, 2 + 1 + 2 + 2 + 1 + 250 * 6); n += 2;
    buf[n++] = 8;
    w16be(buf, n, 96); n += 2;
    w16be(buf, n, 96); n += 2;
    buf[n++] = 250;                                   /* components_num way too big */
    for (int i = 0; i < 250; i++) {
        buf[n++] = (uint8_t)i;                        /* cid */
        buf[n++] = 0x11;                              /* hv */
        buf[n++] = 0;                                 /* qt */
    }
    buf[n++] = 0xFF; buf[n++] = 0xD9;

    char *path = write_temp("rob_jpg_comp_250.jpg", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    struct pic *p = ops ? file_load(ops, path, 0) : NULL;
    free(path);
    if (p) {
        printf("  FAIL: components_num=250 returned a pic\n");
        file_free(ops, p);
        return -1;
    }
    printf("  ok: components_num=250 rejected\n");
    return 0;
}

/* Test 2b: JPEG components_num=0 -- same protection, zero alloc */

static int test_jpg_sof_components_0(void *unused)
{
    (void)unused;
    uint8_t buf[24];
    unsigned n = 0;
    buf[n++] = 0xFF; buf[n++] = 0xD8;
    buf[n++] = 0xFF; buf[n++] = 0xC0;
    w16be(buf, n, 11); n += 2;
    buf[n++] = 8;
    w16be(buf, n, 96); n += 2;
    w16be(buf, n, 96); n += 2;
    buf[n++] = 0;                                     /* components_num=0 */
    buf[n++] = 0xFF; buf[n++] = 0xD9;

    char *path = write_temp("rob_jpg_comp_0.jpg", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    struct pic *p = ops ? file_load(ops, path, 0) : NULL;
    free(path);
    if (p) {
        printf("  FAIL: components_num=0 returned a pic\n");
        file_free(ops, p);
        return -1;
    }
    printf("  ok: components_num=0 rejected\n");
    return 0;
}

/* Test 3: JPEG DHT with segment length 50000.  The old code would burn
 *         50 000 iterations reading garbage past EOF; with alarm() the
 *         test verifies we don't hang.
 *
 *         The file must contain a SOF so that load() reaches the DHT
 *         parsing path -- a file with only SOI + DHT + EOI would skip
 *         read_dht entirely and exit cleanly, which doesn't exercise the
 *         buggy loop. */

static int test_jpg_dht_huge(void *unused)
{
    (void)unused;
    uint8_t buf[64];
    unsigned n = 0;
    buf[n++] = 0xFF; buf[n++] = 0xD8;                  /* SOI */
    /* SOF0: 96x96, 3 components */
    buf[n++] = 0xFF; buf[n++] = 0xC0;
    w16be(buf, n, 2 + 1 + 2 + 2 + 1 + 6); n += 2;
    buf[n++] = 8;
    w16be(buf, n, 96); n += 2;
    w16be(buf, n, 96); n += 2;
    buf[n++] = 3;
    buf[n++] = 1; buf[n++] = 0x11; buf[n++] = 0;
    buf[n++] = 2; buf[n++] = 0x11; buf[n++] = 1;
    buf[n++] = 3; buf[n++] = 0x11; buf[n++] = 1;
    /* DHT with bogus len=50000 and minimal body (1 class byte + 16 code-lengths)
     * -> the tlen loop would run ~49 998 times on the old code. */
    buf[n++] = 0xFF; buf[n++] = 0xC4;
    w16be(buf, n, 50000); n += 2;
    buf[n++] = 0x00;
    memset(buf + n, 0, 16); n += 16;
    buf[n++] = 0xFF; buf[n++] = 0xD9;

    char *path = write_temp("rob_jpg_dht_huge.jpg", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    if (!ops) {
        free(path);
        printf("  FAIL: probe should have passed\n");
        return -1;
    }
    /* fix verification: the old code would spin 50 000 iterations on the
     * bogus tlen; with the feof/ferror guard added the loop drops out as
     * soon as the file ends, so load returns normally -- no hang, no
     * crash, no SIGSEGV.  Either NULL or a partially decoded pic is OK;
     * what matters is we don't burn CPU. */
    struct pic *p = file_load(ops, path, 0);
    free(path);
    if (p) {
        file_free(ops, p);
    }
    printf("  ok: DHT huge segment did not hang\n");
    return 0;
}

/* Test 3b: JPEG DHT with Tc=15.  Only Tc=0 (DC) and Tc=1 (AC) are legal
 *         per ITU-T T.81 4.8.2; the parser used to index j->dht[15][id],
 *         writing far past the 2x16 table array and corrupting the heap.
 *         Under ASan/UBSan this is an immediate heap-buffer-overflow, and
 *         the malformed table must now be rejected. */

static int test_jpg_dht_bad_class(void *unused)
{
    (void)unused;
    uint8_t buf[64];
    unsigned n = 0;
    buf[n++] = 0xFF; buf[n++] = 0xD8;                  /* SOI */
    /* SOF0: 96x96, 3 components */
    buf[n++] = 0xFF; buf[n++] = 0xC0;
    w16be(buf, n, 2 + 1 + 2 + 2 + 1 + 6); n += 2;
    buf[n++] = 8;
    w16be(buf, n, 96); n += 2;
    w16be(buf, n, 96); n += 2;
    buf[n++] = 3;
    buf[n++] = 1; buf[n++] = 0x11; buf[n++] = 0;
    buf[n++] = 2; buf[n++] = 0x11; buf[n++] = 1;
    buf[n++] = 3; buf[n++] = 0x11; buf[n++] = 1;
    /* DHT: length is consistent (1 selector + 16 counters), but Tc=15. */
    buf[n++] = 0xFF; buf[n++] = 0xC4;
    w16be(buf, n, 2 + 1 + 16); n += 2;
    buf[n++] = 0xF0;                                   /* Tc=15, Th=0 */
    memset(buf + n, 0, 16); n += 16;
    buf[n++] = 0xFF; buf[n++] = 0xD9;                  /* EOI */

    char *path = write_temp("rob_jpg_dht_bad_class.jpg", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    struct pic *p = ops ? file_load(ops, path, 0) : NULL;
    free(path);
    if (p) {
        /* no tables were stored, so a pic here would mean the bad table was
         * accepted; either way free it before bailing out */
        printf("  FAIL: DHT with Tc=15 was accepted\n");
        file_free(ops, p);
        return -1;
    }
    printf("  ok: DHT with Tc=15 rejected without out-of-bounds access\n");
    return 0;
}

/* Test 4: BMP width=height=0x7FFFFFFF -- old code overflows int on pitch */

static int test_bmp_huge_dims(void *unused)
{
    (void)unused;
    uint8_t buf[66];
    unsigned n = 0;
    /* file header */
    buf[n++] = 'B'; buf[n++] = 'M';
    w32le(buf, n, 54); n += 4;                         /* file_size */
    w32le(buf, n, 0);  n += 4;                         /* reserved */
    w32le(buf, n, 54); n += 4;                         /* offset_data */
    /* info header */
    w32le(buf, n, 40); n += 4;                         /* size=40 */
    w32le(buf, n, 0x7FFFFFFF); n += 4;                 /* width */
    w32le(buf, n, 0x7FFFFFFF); n += 4;                 /* height */
    w16le(buf, n, 1); n += 2;                          /* planes (LE) */
    w16le(buf, n, 24); n += 2;                         /* bit_count=24 (LE) */
    w32le(buf, n, 0); n += 4;                          /* compression */
    w32le(buf, n, 0); n += 4;                          /* size_image */
    w32le(buf, n, 2835); n += 4;                       /* x dpi */
    w32le(buf, n, 2835); n += 4;                       /* y dpi */
    w32le(buf, n, 0); n += 4;                          /* colors_used */
    w32le(buf, n, 0); n += 4;                          /* colors_important */

    char *path = write_temp("rob_bmp_huge.bmp", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    struct pic *p = ops ? file_load(ops, path, 0) : NULL;
    free(path);
    if (p) {
        printf("  FAIL: BMP huge dims returned a pic\n");
        file_free(ops, p);
        return -1;
    }
    printf("  ok: BMP huge dims rejected\n");
    return 0;
}

/* -------------------------------------------------------------------- */
/* Bonus 1: palette BMP (8-bit, 4x4) should decode correctly.  This     */
/*         exercises the old pitch bug where palette modes promoted     */
/*         bit_count but pitch still used bit_count (= 8) instead of    */
/*         the final p->depth (= 24).  A regression here would read     */
/*         garbage past the allocated buffer.                           */

static int test_bmp_palette_8bit(void *unused)
{
    (void)unused;
    enum { W = 4, H = 4 };
    uint8_t buf[96];
    /* palette (2 entries, BGRA per BMP file layout) */
    static const uint8_t pal0[4] = {10, 20, 30, 0};        /* blue=10 green=20 red=30 -> buf (10,20,30) */
    static const uint8_t pal1[4] = {100, 110, 120, 0};     /* blue=100 green=110 red=120 -> buf (100,110,120) */
    /* BMP stores rows bottom-up; we want visual row 0 (top) to be
     * [0, 1, 0, 1] so it must come last in the pixel array. */
    static const uint8_t pixels[H][W] = {
        {1, 0, 1, 0},     /* bottom row of image */
        {0, 1, 0, 1},
        {1, 1, 0, 0},
        {0, 0, 1, 1},     /* top row of image */
    };

    unsigned n = 0;
    unsigned offset_data = 54 + 2 * 4;                     /* file+DIB + 2 palette entries */
    unsigned row_pad = (4 - W % 4) % 4;                    /* 0 since W=4 */
    unsigned pixel_bytes = (W + row_pad) * H;
    unsigned file_size = offset_data + pixel_bytes;

    /* file header */
    buf[n++] = 'B'; buf[n++] = 'M';
    w32le(buf, n, file_size); n += 4;
    w32le(buf, n, 0); n += 4;
    w32le(buf, n, offset_data); n += 4;
    /* info header */
    w32le(buf, n, 40); n += 4;
    w32le(buf, n, W); n += 4;
    w32le(buf, n, H); n += 4;
    w16le(buf, n, 1); n += 2;
    w16le(buf, n, 8); n += 2;
    w32le(buf, n, 0); n += 4;                              /* BI_RGB */
    w32le(buf, n, pixel_bytes); n += 4;
    w32le(buf, n, 2835); n += 4;
    w32le(buf, n, 2835); n += 4;
    w32le(buf, n, 2); n += 4;                              /* colors_used = 2 */
    w32le(buf, n, 0); n += 4;
    /* palette */
    memcpy(buf + n, pal0, 4); n += 4;
    memcpy(buf + n, pal1, 4); n += 4;
    /* pixel data (bottom-up) */
    for (int y = 0; y < H; y++) {
        memcpy(buf + n, pixels[y], W);
        n += W + row_pad;
    }

    char *path = write_temp("rob_bmp_palette8.bmp", buf, n);
    if (!path) return -1;
    struct file_ops *ops = file_probe(path);
    struct pic *p = ops ? file_load(ops, path, 0) : NULL;
    free(path);
    if (!p) {
        printf("  FAIL: palette 8-bit BMP did not load\n");
        return -1;
    }
    if (p->width != W || p->height != H) {
        printf("  FAIL: palette BMP decoded as %dx%d\n", p->width, p->height);
        file_free(ops, p);
        return -1;
    }
    /* visual top-left (0,0): top row of image = last row in file, but read
     * into b->data at i=0 by read_color_index (k=height-1 -> i=0).
     * pixels[3][0] == 0 -> palette[0] -> buf BGR(10,20,30) */
    uint8_t *px = (uint8_t *)p->pixels;
    uint8_t *px00 = px + 0 * p->pitch + 0;
    if (px00[0] != 10 || px00[1] != 20 || px00[2] != 30) {
        printf("  FAIL: palette (0,0) = BGR(%d,%d,%d) expected (10,20,30)\n",
               px00[0], px00[1], px00[2]);
        file_free(ops, p);
        return -1;
    }
    /* visual top-right (3,0): pixels[3][3] = 1 -> palette[1] -> buf BGR(100,110,120) */
    uint8_t *px30 = px + 0 * p->pitch + 3 * 3;
    if (px30[0] != 100 || px30[1] != 110 || px30[2] != 120) {
        printf("  FAIL: palette (3,0) = BGR(%d,%d,%d) expected (100,110,120)\n",
               px30[0], px30[1], px30[2]);
        file_free(ops, p);
        return -1;
    }
    /* visual bottom-left (0,3): bottom row of image = first row in file, read
     * into b->data at i=height-1=3.
     * pixels[0][0] = 1 -> palette[1] -> buf BGR(100,110,120) */
    uint8_t *px03 = px + (H - 1) * p->pitch + 0;
    if (px03[0] != 100 || px03[1] != 110 || px03[2] != 120) {
        printf("  FAIL: palette (0,3) = BGR(%d,%d,%d) expected (100,110,120)\n",
               px03[0], px03[1], px03[2]);
        file_free(ops, p);
        return -1;
    }
    file_free(ops, p);
    printf("  ok: palette 8-bit BMP decodes with correct pixels\n");
    return 0;
}

/* -------------------------------------------------------------------- */

int main(void)
{
    file_ops_init();

    struct case_ {
        const char *name;
        int (*fn)(void *);
    } cases[] = {
        {"JPG SOF huge dims",        test_jpg_sof_huge_dims},
        {"JPG components_num=250",   test_jpg_sof_components_250},
        {"JPG components_num=0",     test_jpg_sof_components_0},
        {"JPG DHT huge segment",     test_jpg_dht_huge},
        {"JPG DHT table class 15",   test_jpg_dht_bad_class},
        {"BMP huge 0x7FFFFFFF",      test_bmp_huge_dims},
        {"BMP 8-bit palette pixels", test_bmp_palette_8bit},
    };

    int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        printf("test %zu: %s ...\n", i, cases[i].name);
        int rc = with_timeout(5, cases[i].fn, NULL);
        if (rc == 2) {
            printf("  FAIL: TIMEOUT (alarm fired)\n");
            failed++;
        } else if (rc != 0) {
            failed++;
        }
    }

    printf("%d/%zu failed\n", failed, sizeof(cases) / sizeof(cases[0]));
    return failed ? 1 : 0;
}
