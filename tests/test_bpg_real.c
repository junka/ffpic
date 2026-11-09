/* BPG container regression test.
 *
 * Fixture: tests/data/bpg_grad420.bpg — 128x128 yuv420p 8-bit gradient,
 * x265 coded IDR, BPG header without VUI (matrix_coeffs falls back to 2 → BT.601
 * in both hevc.c and ffmpeg).
 *
 * Checks:
 *   1. file_probe accepts the magic, file_load succeeds, dimensions match.
 *   2. Decoded BGRA buffer MD5 matches the reference hash (computed once from
 *      our decoder on a known-good run).  BT.601 default + limited-range TV +
 *      hevc.c BT.601 color conversion must be byte-stable.
 *   3. BPG_probe rejects truncated header (magic present, ue7 reads run past EOF).
 *   4. BPG_probe accepts magic but BPG_load rejects a file whose picture_data_len
 *      claims more HEVC than fits in the container (parsing layer catches it).
 *   5. BPG_load rejects a magic-valid file whose HEVC payload contains no start
 *      codes at all (zero NALs → zero slices → clean refusal, no crash).
 *   6. alpha1 (auxiliary HEVC plane, nuh_layer_id == 1) is decoded and merged
 *      into the alpha channel; RGB must match a colour-only decode of the same
 *      stream and the alpha channel must match a monochrome-only decode of the
 *      auxiliary stream.  Covered for 8-bit (BGRA) and 10-bit (BGRA64).
 *   7. alpha1 && alpha2 (premultiplied) un-divides RGB by alpha exactly as
 *      libbpg's alpha_divide8 does; the alpha channel is unchanged.
 *   8. alpha2 without alpha1 (the "W plane") is refused cleanly.
 *   9. alpha1 signalled but no alpha plane present in the stream → refused.
 *
 * The alpha fixtures (tests/data/bpg_alpha*.bpg) are a genuine *synthetic*
 * alpha1 file: the colour NALs come from bpg_grad420.bpg and the auxiliary
 * plane is a real monochrome HEVC stream with its nuh_layer_id set to 1, which
 * is exactly the BPG alpha1 signalling (libbpg build_modified_hevc sets the
 * same bit).  They are NOT bpgenc output: bpgenc moves VPS/SPS into the
 * hevc_header "modified SPS" blob, which this loader does not reconstruct, so
 * plain bpgenc files (with or without alpha) remain unsupported.
 *
 * The bad fixtures are built in-test so tests/data stays under 50 KB per file.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "file.h"
#include "colorspace.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

/* ---- ue7 encoder for building bad fixtures in-test ---- */
static int
write_ue7(uint8_t *buf, int max, uint64_t v)
{
    int used = 0;
    uint8_t parts[10];
    int n = 0;
    if (v == 0) {
        if (max < 1) return -1;
        buf[used++] = 0;
        return used;
    }
    while (v) {
        parts[n++] = v & 0x7f;
        v >>= 7;
    }
    /* MSB-first, all but last have high bit set */
    for (int i = n - 1; i >= 0; i--) {
        if (used >= max) return -1;
        buf[used++] = (uint8_t)(parts[i] | (i > 0 ? 0x80 : 0));
    }
    return used;
}

/* ---- RFC 1321 reference MD5 (same as test_hevc_tiles_real.c) ---- */
static int
md5_hex(const uint8_t *buf, size_t len, char out[33])
{
    static const uint32_t T[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };
    static const uint32_t s[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    uint64_t bit_len = (uint64_t)len * 8;
    size_t pad = (len % 64 < 56) ? (56 - len % 64) : (120 - len % 64);
    size_t total = len + pad + 8;
    uint8_t *full = malloc(total);
    if (!full) return -1;
    memcpy(full, buf, len);
    full[len] = 0x80;
    memset(full + len + 1, 0, pad);
    for (int i = 0; i < 8; i++)
        full[len + pad + i] = (uint8_t)(bit_len >> (i * 8));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            M[i] = (uint32_t)full[off + i*4]
                 | ((uint32_t)full[off + i*4 + 1] << 8)
                 | ((uint32_t)full[off + i*4 + 2] << 16)
                 | ((uint32_t)full[off + i*4 + 3] << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t f, g;
            if (i < 16)      { f = (B & C) | ((~B) & D);         g = i; }
            else if (i < 32) { f = (D & B) | ((~D) & C);         g = (5*i + 1) % 16; }
            else if (i < 48) { f = B ^ C ^ D;                    g = (3*i + 5) % 16; }
            else             { f = C ^ (B | (~D));               g = (7*i) % 16; }
            f = f + A + T[i] + M[g];
            uint32_t tmp = D; D = C; C = B;
            B = B + ((f << s[i]) | (f >> (32 - s[i])));
            A = tmp;
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    free(full);
    uint8_t digest[16];
    memcpy(digest, &a0, 4); memcpy(digest + 4, &b0, 4);
    memcpy(digest + 8, &c0, 4); memcpy(digest + 12, &d0, 4);
    for (int i = 0; i < 16; i++)
        sprintf(out + i*2, "%02x", digest[i]);
    out[32] = 0;
    return 0;
}

/* Build a BPG fixture into tmp dir.  Returns path on success, NULL on error.
 * picture_data_len = 0 means "to EOF".  hevc_data is verbatim. */
static char *
build_tmp_bpg(const char *suffix, int pixel_format, int bit_depth,
              int width, int height, int64_t picture_data_len,
              int hevc_hdr_len, int alpha1, int alpha2,
              const uint8_t *hevc_data, size_t hevc_len)
{
    static uint8_t buf[4096];
    int used = 0;
    buf[used++] = 0x42; buf[used++] = 0x50; buf[used++] = 0x47; buf[used++] = 0xfb;
    /* BPG header flags are MSB-first (see libbpg bpg_decode_header):
     * flags1 = [pixel_format:3][alpha1_flag:1][bit_depth_minus_8:4]
     * flags2 = [color_space:4][ext:1][alpha2_flag:1][limited:1][anim:1] */
    uint8_t flags1 = (uint8_t)(((pixel_format & 7) << 5) |
                               ((alpha1 & 1) << 4) |
                               ((bit_depth - 8) & 0x0f));
    uint8_t flags2 = (uint8_t)((alpha2 & 1) << 2);
    buf[used++] = flags1;
    buf[used++] = flags2;
    int n;
    n = write_ue7(buf + used, sizeof(buf) - used, width);
    if (n < 0) { return NULL; }
    used += n;
    n = write_ue7(buf + used, sizeof(buf) - used, height);
    if (n < 0) { return NULL; }
    used += n;
    n = write_ue7(buf + used, sizeof(buf) - used,
                  picture_data_len < 0 ? 0 : (uint64_t)picture_data_len);
    if (n < 0) { return NULL; }
    used += n;
    n = write_ue7(buf + used, sizeof(buf) - used, hevc_hdr_len);
    if (n < 0) { return NULL; }
    used += n;
    if (hevc_hdr_len > 0) {
        memset(buf + used, 0, hevc_hdr_len);
        used += hevc_hdr_len;
    }
    /* alpha files carry a second hevc_header block (BPG spec
     * hevc_header_and_data: one for the alpha plane, one for the colour). */
    if (alpha1 || alpha2) {
        n = write_ue7(buf + used, sizeof(buf) - used, hevc_hdr_len);
        if (n < 0) { return NULL; }
        used += n;
        if (hevc_hdr_len > 0) {
            memset(buf + used, 0, hevc_hdr_len);
            used += hevc_hdr_len;
        }
    }
    if (hevc_len > 0) {
        memcpy(buf + used, hevc_data, hevc_len);
        used += (int)hevc_len;
    }
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/bpg_test_%s.bpg", suffix);
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    if (fwrite(buf, 1, (size_t)used, f) != (size_t)used) { fclose(f); return NULL; }
    fclose(f);
    return path[0] ? path : NULL;
}

/* Decode one of the extra fixtures and check that it produced real pixels
 * with an opaque alpha channel.  This is the regression for 4:0:0 streams
 * (the luma plane used to be copied into alpha) and for 16 bit 4:4:4. */
static int
check_extra_fixture(const char *name, int want_depth)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_DATA_DIR, name);
    struct file_ops *ops = file_probe(path);
    if (!ops) {
        printf("FAIL[%s]: probe refused\n", name);
        return -1;
    }
    struct pic *p = file_load(ops, path, 0);
    if (!p) {
        printf("FAIL[%s]: load returned NULL\n", name);
        return -1;
    }
    int rc = 0;
    if (p->width != 64 || p->height != 64 || p->depth != want_depth) {
        printf("FAIL[%s]: %dx%d depth %d, want 64x64 depth %d\n", name,
               p->width, p->height, p->depth, want_depth);
        rc = -1;
    } else {
        int bpp = p->depth >> 3;
        uint32_t amax = bpp == 4 ? 0xFFu : 0xFFFFu;
        long nonzero = 0;
        for (int y = 0; y < p->height; y++) {
            uint8_t *row = (uint8_t *)p->pixels + (size_t)y * p->pitch;
            for (int x = 0; x < p->width; x++) {
                uint8_t *px = row + (size_t)x * bpp;
                uint32_t a;
                if (bpp == 4) {
                    a = px[3];
                    if (px[0] || px[1] || px[2]) nonzero++;
                } else {
                    const uint16_t *q = (const uint16_t *)px;
                    a = q[3];
                    if (q[0] || q[1] || q[2]) nonzero++;
                }
                if (a != amax) {
                    printf("FAIL[%s]: alpha %u at %d,%d\n", name, a, x, y);
                    rc = -1;
                    goto out;
                }
            }
        }
        if (nonzero == 0) {
            printf("FAIL[%s]: decoded to an all-zero picture\n", name);
            rc = -1;
        } else {
            printf("OK[%s]: %dx%d depth %d, %ld nonzero pixels\n", name,
                   p->width, p->height, p->depth, nonzero);
        }
    }
out:
    file_free(ops, p);
    return rc;
}

/* ---- alpha1 decode/merge regression helpers ---- */

struct bpg_loaded {
    struct file_ops *ops;
    struct pic *p;
};

static int
load_fixture(const char *name, struct bpg_loaded *out)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_DATA_DIR, name);
    out->ops = file_probe(path);
    if (!out->ops) { printf("FAIL[%s]: probe refused\n", name); return -1; }
    out->p = file_load(out->ops, path, 0);
    if (!out->p) { printf("FAIL[%s]: load returned NULL\n", name); return -1; }
    return 0;
}

static void
unload_fixture(struct bpg_loaded *f)
{
    if (f->p)
        file_free(f->ops, f->p);
    f->p = NULL;
}

/* libbpg alpha_divide8 (see libbpg.c comp_divide8/alpha_divide8) */
static int
ref_divide8(unsigned v, unsigned a)
{
    unsigned inv;
    if (a == 0)
        return 255;
    if (v >= a)
        return 255;
    inv = ((255u << 16) + a / 2 + 128) / a;
    return (int)((v * inv + (1u << 15)) >> 16);
}

/* Straight alpha: colour bytes must equal the colour-only reference decode,
 * alpha channel must equal channel 0 of the monochrome-only reference. */
static int
check_straight_alpha(const char *name, const char *rgb_ref, const char *gray_ref)
{
    struct bpg_loaded a = {0}, c = {0}, g = {0};
    int rc = 0;
    if (load_fixture(name, &a) || load_fixture(rgb_ref, &c) ||
        load_fixture(gray_ref, &g)) {
        rc = -1; goto out;
    }
    if (a.p->depth != 32 || a.p->format != CS_PIXELFORMAT_BGRA_BYTES) {
        printf("FAIL[%s]: depth %d format %d, want 8-bit BGRA\n", name,
               a.p->depth, a.p->format);
        rc = -1; goto out;
    }
    if (a.p->width != c.p->width || a.p->height != c.p->height ||
        a.p->width != g.p->width || a.p->height != g.p->height) {
        printf("FAIL[%s]: size mismatch\n", name);
        rc = -1; goto out;
    }
    long rgb_bad = 0, alpha_bad = 0, alpha_nz = 0, rgb_nz = 0;
    int amin = 999, amax = -1;
    for (int y = 0; y < a.p->height; y++) {
        uint8_t *pa = (uint8_t *)a.p->pixels + (size_t)y * a.p->pitch;
        uint8_t *pc = (uint8_t *)c.p->pixels + (size_t)y * c.p->pitch;
        uint8_t *pg = (uint8_t *)g.p->pixels + (size_t)y * g.p->pitch;
        for (int x = 0; x < a.p->width; x++) {
            if (pa[x*4+0] != pc[x*4+0] || pa[x*4+1] != pc[x*4+1] ||
                pa[x*4+2] != pc[x*4+2])
                rgb_bad++;
            if (pa[x*4+3] != pg[x*4+0])
                alpha_bad++;
            if (pa[x*4+3]) alpha_nz++;
            if (pa[x*4+0] || pa[x*4+1] || pa[x*4+2]) rgb_nz++;
            if (pa[x*4+3] < amin) amin = pa[x*4+3];
            if (pa[x*4+3] > amax) amax = pa[x*4+3];
        }
    }
    if (rgb_bad || alpha_bad || alpha_nz == 0 || rgb_nz == 0 ||
        amin >= amax) {
        printf("FAIL[%s]: rgb_bad=%ld alpha_bad=%ld alpha_nz=%ld rgb_nz=%ld "
               "alpha %d..%d\n", name, rgb_bad, alpha_bad, alpha_nz, rgb_nz,
               amin, amax);
        rc = -1;
    } else {
        printf("OK[%s]: alpha1 merged, rgb==%s, alpha==%s (alpha %d..%d)\n",
               name, rgb_ref, gray_ref, amin, amax);
    }
out:
    unload_fixture(&a); unload_fixture(&c); unload_fixture(&g);
    return rc;
}

/* Premultiplied: alpha must match the straight decode, RGB must equal the
 * straight RGB un-divided by alpha with libbpg's rounding. */
static int
check_premul_alpha(const char *name, const char *straight)
{
    struct bpg_loaded a = {0}, s = {0};
    int rc = 0;
    if (load_fixture(name, &a) || load_fixture(straight, &s)) { rc = -1; goto out; }
    long bad = 0, a0 = 0;
    for (int y = 0; y < a.p->height; y++) {
        uint8_t *pa = (uint8_t *)a.p->pixels + (size_t)y * a.p->pitch;
        uint8_t *ps = (uint8_t *)s.p->pixels + (size_t)y * s.p->pitch;
        for (int x = 0; x < a.p->width; x++) {
            unsigned av = ps[x*4+3];
            if (av == 0) a0++;
            if (pa[x*4+3] != av) bad++;
            for (int ch = 0; ch < 3; ch++) {
                int exp = ref_divide8(ps[x*4+ch], av);
                if (pa[x*4+ch] != exp) bad++;
            }
        }
    }
    if (bad) {
        printf("FAIL[%s]: %ld mismatches vs %s un-divided\n", name, bad, straight);
        rc = -1;
    } else {
        printf("OK[%s]: premultiplied RGB un-divided by alpha (%ld zero-alpha px)\n",
               name, a0);
    }
out:
    unload_fixture(&a); unload_fixture(&s);
    return rc;
}

/* >8 bit alpha1: BGRA64, RGB must equal the colour-only reference, alpha must
 * equal channel 0 of the monochrome-only reference. */
static int
check_alpha16(const char *name, const char *rgb_ref, const char *gray_ref)
{
    struct bpg_loaded a = {0}, c = {0}, g = {0};
    int rc = 0;
    if (load_fixture(name, &a) || load_fixture(rgb_ref, &c) ||
        load_fixture(gray_ref, &g)) {
        rc = -1; goto out;
    }
    if (a.p->depth != 64 || a.p->format != CS_PIXELFORMAT_BGRA64) {
        printf("FAIL[%s]: depth %d format %d, want BGRA64\n", name,
               a.p->depth, a.p->format);
        rc = -1; goto out;
    }
    long rgb_bad = 0, alpha_bad = 0;
    uint16_t amin = 0xffff, amax = 0;
    for (int y = 0; y < a.p->height; y++) {
        uint16_t *pa = (uint16_t *)((uint8_t *)a.p->pixels + (size_t)y * a.p->pitch);
        uint16_t *pc = (uint16_t *)((uint8_t *)c.p->pixels + (size_t)y * c.p->pitch);
        uint16_t *pg = (uint16_t *)((uint8_t *)g.p->pixels + (size_t)y * g.p->pitch);
        for (int x = 0; x < a.p->width; x++) {
            if (pa[x*4+0] != pc[x*4+0] || pa[x*4+1] != pc[x*4+1] ||
                pa[x*4+2] != pc[x*4+2])
                rgb_bad++;
            if (pa[x*4+3] != pg[x*4+0])
                alpha_bad++;
            if (pa[x*4+3] < amin) amin = pa[x*4+3];
            if (pa[x*4+3] > amax) amax = pa[x*4+3];
        }
    }
    if (rgb_bad || alpha_bad || amin >= amax) {
        printf("FAIL[%s]: rgb_bad=%ld alpha_bad=%ld range %u..%u\n", name,
               rgb_bad, alpha_bad, amin, amax);
        rc = -1;
    } else {
        printf("OK[%s]: alpha1 merged at 10-bit (range %u..%u)\n", name, amin, amax);
    }
out:
    unload_fixture(&a); unload_fixture(&c); unload_fixture(&g);
    return rc;
}

/* libbpg alpha_divide16 (see libbpg.c comp_divide16/alpha_divide16) */
static int
ref_divide16(unsigned v, unsigned a)
{
    unsigned inv;
    if (a == 0)
        return 65535;
    if (v >= a)
        return 65535;
    inv = ((65535u << 15) + a / 2) / a;
    return (int)((v * inv + (1u << 14)) >> 15);
}

static int
check_premul_alpha16(const char *name, const char *straight)
{
    struct bpg_loaded a = {0}, s = {0};
    int rc = 0;
    if (load_fixture(name, &a) || load_fixture(straight, &s)) { rc = -1; goto out; }
    long bad = 0, a0 = 0;
    for (int y = 0; y < a.p->height; y++) {
        uint16_t *pa = (uint16_t *)((uint8_t *)a.p->pixels + (size_t)y * a.p->pitch);
        uint16_t *ps = (uint16_t *)((uint8_t *)s.p->pixels + (size_t)y * s.p->pitch);
        for (int x = 0; x < a.p->width; x++) {
            unsigned av = ps[x*4+3];
            if (av == 0) a0++;
            if (pa[x*4+3] != av) bad++;
            for (int ch = 0; ch < 3; ch++) {
                int exp = ref_divide16(ps[x*4+ch], av);
                if (pa[x*4+ch] != exp) bad++;
            }
        }
    }
    if (bad) {
        printf("FAIL[%s]: %ld mismatches vs %s un-divided\n", name, bad, straight);
        rc = -1;
    } else {
        printf("OK[%s]: premultiplied BGRA64 un-divided (%ld zero-alpha px)\n",
               name, a0);
    }
out:
    unload_fixture(&a); unload_fixture(&s);
    return rc;
}

int main(void)
{
    int rc = 0;
    file_ops_init();

    /* ============================================================== *
     * TEST 1: Real BPG — probe, dimensions, BGRA MD5                  *
     * ============================================================== */
    char path[512];
    snprintf(path, sizeof(path), "%s/bpg_grad420.bpg", TEST_DATA_DIR);

    struct file_ops *ops = file_probe(path);
    if (!ops) {
        printf("FAIL[real]: file_probe refused %s\n", path);
        rc = -1; goto out;
    }
    struct pic *p = file_load(ops, path, 0);
    if (!p) {
        printf("FAIL[real]: file_load returned NULL for valid BPG\n");
        rc = -1; goto out;
    }
    if (p->width != 128 || p->height != 128) {
        printf("FAIL[real]: dims %dx%d want 128x128\n", p->width, p->height);
        rc = -1; goto free_real;
    }
    if (p->format != CS_PIXELFORMAT_BGRA_BYTES) {
        printf("FAIL[real]: format %d want BGRA byte order\n", p->format);
        rc = -1; goto free_real;
    }

    /* Compute pinned MD5 of our BGRA output.  This is an in-tree known-good
     * value (from our decoder), not an ffmpeg reference — BT.601 default
     * matrix + limited range is shared by both, but the HEVC encoder's SAO /
     * deblocking between x265 and ffmpeg differ enough that MD5 will never
     * match exactly.  The hash verifies output stability on CI. */
    const char OUR_REF_MD5[] = "652fda5ba8ef9e52d2358afae71658b3";
    char got[33];
    int pitch = p->pitch > 0 ? p->pitch : p->width * 4;
    size_t bgra_bytes = (size_t)pitch * (size_t)p->height;
    if (md5_hex(p->pixels, bgra_bytes, got) != 0) {
        printf("FAIL[real]: md5 computation OOM\n");
        rc = -1; goto free_real;
    }
    if (strcmp(got, OUR_REF_MD5) != 0) {
        printf("FAIL[real]: MD5 mismatch got %s want %s\n", got, OUR_REF_MD5);
        rc = -1;
    } else {
        printf("OK[real]: %dx%d BGRA bytes MD5 %s (pitch=%d)\n",
               p->width, p->height, got, pitch);
    }

free_real:
    file_free(ops, p);

    if (check_extra_fixture("bpg_gray.bpg", 32) != 0) {
        rc = -1;
    }
    if (check_extra_fixture("bpg_grad444_10bit.bpg", 64) != 0) {
        rc = -1;
    }

    /* ============================================================== *
     * TEST 2: Magic correct, picture_data_len over EOF → reject       *
     * ============================================================== */
    {
        uint8_t tiny_hevc[] = { 0x00, 0x00, 0x00, 0x01, 0x26, 0xaf, 0x09 };
        char *bad = build_tmp_bpg("oversize", 1, 8, 16, 16,
                                  (int64_t)1 << 30, 0, 0, 0,
                                  tiny_hevc, sizeof(tiny_hevc));
        if (!bad) { rc = -1; goto out; }
        ops = file_probe(bad);
        if (ops) {
            /* Probe passes (magic OK), but load must fail */
            struct pic *pb = file_load(ops, bad, 0);
            if (pb) {
                printf("FAIL[oversize]: load should have rejected picture_data_len=2^30\n");
                pic_free(pb); rc = -1;
            } else {
                printf("OK[oversize]: file_probe passes but file_load rejects cleanly\n");
            }
        } else {
            /* If probe is stricter, that's also fine */
            printf("OK[oversize]: file_probe refuses (conservative)\n");
        }
        unlink(bad);
    }

    /* ============================================================== *
     * TEST 3: Magic correct, HEVC has ZERO start codes → reject       *
     * ============================================================== */
    {
        uint8_t garbage[80];
        memset(garbage, 0xde, sizeof(garbage)); /* byte 0xde = 11011110, never 00 */
        char *bad = build_tmp_bpg("nonal", 1, 8, 16, 16, 0, 0, 0, 0,
                                  garbage, sizeof(garbage));
        if (!bad) { rc = -1; goto out; }
        ops = file_probe(bad);
        if (!ops) {
            printf("FAIL[nonal]: file_probe accepted by magic earlier, now refused\n");
            rc = -1;
        } else {
            struct pic *pb = file_load(ops, bad, 0);
            if (pb) {
                printf("FAIL[nonal]: load should reject zero-NAL HEVC\n");
                pic_free(pb); rc = -1;
            } else {
                printf("OK[nonal]: file_load refuses zero-NAL payload cleanly\n");
            }
        }
        unlink(bad);
    }

    /* ============================================================== *
     * TEST 4a: alpha1 signalled but no alpha plane in the stream      *
     *          → load must refuse (do not silently drop alpha)        *
     * ============================================================== */
    {
        uint8_t fake_hevc[] = { 0x00, 0x00, 0x00, 0x01, 0x40, 0x01 };
        char *bad = build_tmp_bpg("alpha_no_plane", 1, 8, 16, 16, 0, 0, 1, 0,
                                  fake_hevc, sizeof(fake_hevc));
        if (!bad) { rc = -1; goto out; }
        ops = file_probe(bad);
        if (!ops) {
            printf("FAIL[alpha_no_plane]: probe should accept magic\n");
            rc = -1;
        } else {
            struct pic *pb = file_load(ops, bad, 0);
            if (pb) {
                printf("FAIL[alpha_no_plane]: load must refuse when no alpha plane decodes\n");
                pic_free(pb); rc = -1;
            } else {
                printf("OK[alpha_no_plane]: alpha1 without an alpha plane refused\n");
            }
        }
        unlink(bad);
    }

    /* ============================================================== *
     * TEST 4b: W plane (alpha2 without alpha1) → load must refuse     *
     * ============================================================== */
    {
        uint8_t fake_hevc[] = { 0x00, 0x00, 0x00, 0x01, 0x40, 0x01 };
        char *bad = build_tmp_bpg("wplane", 1, 8, 16, 16, 0, 0, 0, 1,
                                  fake_hevc, sizeof(fake_hevc));
        if (!bad) { rc = -1; goto out; }
        ops = file_probe(bad);
        if (!ops) {
            printf("FAIL[wplane]: probe should accept magic\n");
            rc = -1;
        } else {
            struct pic *pb = file_load(ops, bad, 0);
            if (pb) {
                printf("FAIL[wplane]: load must refuse the W plane\n");
                pic_free(pb); rc = -1;
            } else {
                printf("OK[wplane]: alpha2 without alpha1 (W plane) refused\n");
            }
        }
        unlink(bad);
    }

    /* ============================================================== *
     * TEST 4c: alpha1 fixtures (synthetic, see the file header)       *
     * ============================================================== */
    if (check_straight_alpha("bpg_alpha.bpg", "bpg_grad420.bpg",
                             "bpg_alpha_ref_gray.bpg") != 0)
        rc = -1;
    if (check_premul_alpha("bpg_alpha_premul.bpg", "bpg_alpha.bpg") != 0)
        rc = -1;
    if (check_alpha16("bpg_alpha10.bpg", "bpg_alpha10_ref.bpg",
                      "bpg_alpha10_ref_gray.bpg") != 0)
        rc = -1;
    if (check_premul_alpha16("bpg_alpha10_premul.bpg", "bpg_alpha10.bpg") != 0)
        rc = -1;

    /* ============================================================== *
     * TEST 5: Truncated header → probe may still pass magic, but      *
     *         parse_header must fail                                  *
     * ============================================================== */
    {
        /* 4 bytes of magic only — no ue7 fields */
        uint8_t trunc[6] = { 0x42, 0x50, 0x47, 0xfb, 0x01, 0x00 };
        FILE *f = fopen("/tmp/bpg_test_trunc.bpg", "wb");
        if (!f) { rc = -1; goto out; }
        fwrite(trunc, 1, sizeof(trunc), f);
        fclose(f);
        ops = file_probe("/tmp/bpg_test_trunc.bpg");
        if (ops) {
            struct pic *pb = file_load(ops, "/tmp/bpg_test_trunc.bpg", 0);
            if (pb) {
                printf("FAIL[trunc]: load should refuse truncated header\n");
                pic_free(pb); rc = -1;
            } else {
                printf("OK[trunc]: file_load refuses truncated header cleanly\n");
            }
        } else {
            printf("OK[trunc]: file_probe itself refused (conservative)\n");
        }
        unlink("/tmp/bpg_test_trunc.bpg");
    }

out:
    file_ops_deinit();
    return rc;
}
