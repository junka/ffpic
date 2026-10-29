/* The hevc picture is coded at the block aligned size and the sps
 * conformance window crops it back to the size the container advertises.
 * These cases pin that crop, and make sure the sei blocks real encoders
 * write are skipped instead of spinning on a byte sized counter. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "colorspace.h"
#include "file.h"
#include "hevc.h"
#include "idct.h"
#include "vlog.h"

static int
check_crop(const struct sps *sps, int x, int y, int width, int height)
{
    int gx = -1, gy = -1, gw = -1, gh = -1;

    hevc_conformance_crop(sps, &gx, &gy, &gw, &gh);
    if (gx != x || gy != y || gw != width || gh != height) {
        printf("crop got %d,%d %dx%d, expected %d,%d %dx%d\n", gx, gy, gw,
               gh, x, y, width, height);
        return -1;
    }
    return 0;
}

static int
test_crop(void)
{
    struct sps sps;

    memset(&sps, 0, sizeof(sps));
    sps.pic_width_in_luma_samples = 320;
    sps.pic_height_in_luma_samples = 216;
    sps.chroma_format_idc = CHROMA_420;
    sps.SubWidthC = 2;
    sps.SubHeightC = 2;

    if (check_crop(&sps, 0, 0, 320, 216)) {
        return -1;
    }

    /* two chroma lines of padding at the bottom, the display size is 212 */
    sps.conf_win_bottom_offset = 2;
    if (check_crop(&sps, 0, 0, 320, 212)) {
        return -1;
    }

    /* cropping is in chroma samples, so the luma offset doubles */
    sps.conf_win_left_offset = 1;
    sps.conf_win_right_offset = 1;
    sps.conf_win_top_offset = 1;
    if (check_crop(&sps, 2, 2, 316, 210)) {
        return -1;
    }

    sps.chroma_format_idc = CHROMA_444;
    sps.SubWidthC = 1;
    sps.SubHeightC = 1;
    sps.conf_win_bottom_offset = 1;
    if (check_crop(&sps, 1, 1, 318, 214)) {
        return -1;
    }

    return 0;
}

/* real streams carry a few hundred bytes of sei, which must be skipped, not
 * walked a byte at a time with a wrapping counter */
static int
test_sei_nal(void)
{
    enum { PAYLOAD = 1000, LEN = PAYLOAD + 2 };
    uint8_t *nal = calloc(1, LEN);
    uint8_t *pixels = NULL;
    int type;

    if (!nal) {
        return -1;
    }
    nal[0] = (uint8_t)(PREFIX_SEI_NUT << 1);
    nal[1] = 1;

    type = parse_nalu(nal, LEN, pixels, 0, 0, NULL, NULL);
    free(nal);
    if (type != PREFIX_SEI_NUT || pixels != NULL) {
        printf("sei nal parsed as type %u\n", (unsigned)type);
        return -1;
    }
    return 0;
}

/* malformed nals are rejected instead of aborting, and a slice is not fed to
 * a null pixel buffer */
static int
test_bad_nal(void)
{
    uint8_t short_nal[1] = {0};
    uint8_t forbidden_bit[2] = {0x80, 0};
    uint8_t slice[2] = {(uint8_t)(IDR_W_RADL << 1), 1};
    uint8_t *pixels = NULL;

    if (parse_nalu(short_nal, sizeof(short_nal), pixels, 0, 0, NULL, NULL) != -EINVAL ||
        parse_nalu(forbidden_bit, sizeof(forbidden_bit), pixels, 0, 0, NULL, NULL) != -EINVAL ||
        parse_nalu(slice, sizeof(slice), pixels, 0, 0, NULL, NULL) != -EINVAL) {
        printf("a malformed nal unit was not rejected\n");
        return -1;
    }
    return 0;
}

/* the destination is the display size; a partial ctb row must not write past
 * its last pixel */
static int
test_conversion_bounds(void)
{
    enum { WIDTH = 320, HEIGHT = 212, CODED_H = 216, CTB = 64, GUARD = 64 };
    int y_stride = WIDTH;
    int uv_stride = WIDTH / 2;
    int pitch = ((((WIDTH + 15) >> 4) * 16 * 32 + 31) >> 5) << 2;
    int size = CODED_H * y_stride;
    int16_t *planes = calloc((size_t)size * 2, sizeof(int16_t));
    uint8_t *dst = malloc((size_t)pitch * HEIGHT + GUARD);
    int rc = 0;

    if (!planes || !dst) {
        free(planes);
        free(dst);
        return -1;
    }
    memset(dst, 0xAA, (size_t)pitch * HEIGHT + GUARD);

    YUV420_to_BGRA32_16bit(dst, pitch, planes, planes + size,
                           planes + size * 3 / 2, y_stride, uv_stride, WIDTH,
                           HEIGHT, CTB, 0, 2);

    for (int i = pitch * HEIGHT; i < pitch * HEIGHT + GUARD; i++) {
        if (dst[i] != 0xAA) {
            printf("guard byte %d was written\n", i - pitch * HEIGHT);
            rc = -1;
            break;
        }
    }
    if (dst[pitch * HEIGHT - 1] != 0xFF) {
        printf("last pixel alpha is %d\n", dst[pitch * HEIGHT - 1]);
        rc = -1;
    }

    free(planes);
    free(dst);
    return rc;
}

/* one luma and one chroma sample are enough to pin the matrix and the range
 * handling; rgb comes back as b, g, r in the first three bytes */
static int
check_convert(const char *name, int y, int u, int v, int full_range,
              int matrix, int want_b, int want_g, int want_r)
{
    enum { W = 2, H = 2, PITCH = 64 };
    int16_t planes[8];
    uint8_t dst[PITCH * H];
    int16_t *luma = planes;
    int16_t *cb = planes + W * H;
    int16_t *cr = cb + 2;

    for (int i = 0; i < W * H; i++) {
        luma[i] = (int16_t)y;
    }
    cb[0] = (int16_t)u;
    cr[0] = (int16_t)v;

    YUV420_to_BGRA32_16bit(dst, PITCH, luma, cb, cr, W, 1, W, H, 64,
                           full_range, matrix);
    if (dst[0] != want_b || dst[1] != want_g || dst[2] != want_r ||
        dst[3] != 0xFF) {
        printf("%s: got %d,%d,%d,%d expected %d,%d,%d,255\n", name, dst[0],
               dst[1], dst[2], dst[3], want_b, want_g, want_r);
        return -1;
    }
    return 0;
}

static int
test_color_matrix(void)
{
    /* limited bt.601: 16 is black, 235 is white */
    if (check_convert("limited black", 16, 128, 128, 0, 2, 0, 0, 0) ||
        check_convert("limited white", 235, 128, 128, 0, 2, 255, 255, 255) ||
        check_convert("limited gray", 126, 128, 128, 0, 2, 128, 128, 128) ||
        /* the chroma planes are centred on 128 and scaled by 255/224 */
        check_convert("limited chroma", 100, 200, 128, 0, 2, 243, 70, 98) ||
        /* full range leaves the samples alone */
        check_convert("full gray", 128, 128, 128, 1, 2, 128, 128, 128) ||
        check_convert("full chroma", 128, 128, 255, 1, 2, 128, 37, 255)) {
        return -1;
    }
    /* unspecified and bt.601 must agree, bt.709 weights green differently */
    if (check_convert("unspecified", 100, 200, 128, 1, 2, 228, 75, 100) ||
        check_convert("bt601 matrix", 100, 200, 128, 1, 6, 228, 75, 100) ||
        check_convert("bt709 matrix", 100, 200, 128, 1, 1, 234, 87, 100)) {
        return -1;
    }
    return 0;
}

/* The 4x4 intra luma residual uses the dst-vii instead of the dct, and both
 * of its passes add 1 << (shift - 1) before shifting. Dropping that term
 * lowers every output by about one, and the bias then spreads through the
 * intra prediction of the following blocks. */
static int
test_dst_scalar(void)
{
    static const int16_t dc_in[16] = {1000};
    static const int16_t dc_out[16] = {2, 3, 4, 5, 3, 6, 8, 9,
                                       4, 8, 10, 12, 5, 9, 12, 13};
    static const int16_t ac_out[16] = {-3, -3, 0, 3, -3, -3, 0, 3,
                                       0,  0,  0, 0, 3,  3,  0, -3};
    int16_t in[16] = {0};
    int16_t out[16];

    idct_4x4_hevc(dc_in, out, 8, false);
    if (memcmp(out, dc_out, sizeof(out))) {
        printf("dst dc output differs\n");
        return -1;
    }

    in[5] = -300;
    idct_4x4_hevc(in, out, 8, false);
    if (memcmp(out, ac_out, sizeof(out))) {
        printf("dst ac output differs\n");
        return -1;
    }
    return 0;
}

/* Real x265 still images wrapped in a minimal heif container, each paired
 * with the bgra frame ffmpeg's own hevc decoder produces.  Both files come
 * from tests/gen_hevc_samples.py.  The samples cover sao on and off, tiles,
 * wavefront processing, ctb 16, an odd size that goes through the
 * conformance window crop, coarse quantization, lossless coding and 4:0:0.
 * Lossy coding moves pixels, so every sample carries its own tolerance and
 * the whole frame is compared, not just a few points. */
static int
test_real_samples(void)
{
    static const struct {
        const char *name;
        int w, h, tol;
        int mono;
    } cases[] = {
        {"hevc_gradient", 96, 96, 16, 0},
        {"hevc_noise", 96, 96, 16, 0},
        {"hevc_odd", 100, 62, 16, 0},
        {"hevc_ctb16", 96, 96, 16, 0},
        {"hevc_tiles", 128, 96, 16, 0},
        {"hevc_wpp", 128, 96, 16, 0},
        {"hevc_nosao", 96, 96, 16, 0},
        {"hevc_highqp", 96, 96, 16, 0},
        {"hevc_lossless", 64, 64, 4, 0},
        {"hevc_mono", 96, 96, 8, 1},
    };
    int rc = 0;

    file_ops_init();

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char path[512], refpath[512];
        snprintf(path, sizeof(path), "%s/%s.heic", TEST_DATA_DIR,
                 cases[i].name);
        snprintf(refpath, sizeof(refpath), "%s/%s.ref", TEST_DATA_DIR,
                 cases[i].name);

        struct file_ops *ops = file_probe(path);
        if (!ops || strcmp(ops->name, "HEIF")) {
            printf("%s: not detected as HEIF\n", path);
            rc = -1;
            continue;
        }
        struct pic *p = file_load(ops, path, 0);
        if (!p) {
            printf("%s: did not decode\n", path);
            rc = -1;
            continue;
        }
        if (p->width != cases[i].w || p->height != cases[i].h) {
            printf("%s: decoded as %dx%d, expected %dx%d\n", path, p->width,
                   p->height, cases[i].w, cases[i].h);
            rc = -1;
            goto next;
        }

        FILE *f = fopen(refpath, "rb");
        if (!f) {
            printf("%s: cannot open the reference\n", refpath);
            rc = -1;
            goto next;
        }
        size_t ref_size = (size_t)cases[i].w * cases[i].h * 4;
        uint8_t *ref = malloc(ref_size);
        if (!ref || fread(ref, 1, ref_size, f) != ref_size) {
            printf("%s: could not read the reference\n", refpath);
            free(ref);
            fclose(f);
            rc = -1;
            goto next;
        }
        fclose(f);

        int maxdiff = 0;
        int bad = 0;
        for (int y = 0; y < cases[i].h && !bad; y++) {
            uint8_t *row = (uint8_t *)p->pixels + (size_t)y * p->pitch;
            const uint8_t *rrow = ref + (size_t)y * cases[i].w * 4;
            for (int x = 0; x < cases[i].w && !bad; x++) {
                for (int c = 0; c < 3; c++) {
                    int d = abs(row[x * 4 + c] - rrow[x * 4 + c]);
                    if (d > maxdiff) {
                        maxdiff = d;
                    }
                    if (d > cases[i].tol) {
                        printf("%s: (%d,%d) channel %d is %d, reference %d, "
                               "max diff %d exceeds %d\n", path, x, y, c,
                               row[x * 4 + c], rrow[x * 4 + c], d,
                               cases[i].tol);
                        bad = 1;
                        rc = -1;
                        break;
                    }
                }
                if (cases[i].mono) {
                    if (row[x * 4] != row[x * 4 + 1] ||
                        row[x * 4 + 1] != row[x * 4 + 2]) {
                        printf("%s: (%d,%d) grey came out %d %d %d\n", path, x,
                               y, row[x * 4], row[x * 4 + 1],
                               row[x * 4 + 2]);
                        bad = 1;
                        rc = -1;
                        break;
                    }
                } else if (row[x * 4 + 3] != 0xFF) {
                    printf("%s: (%d,%d) alpha %d, expected 255\n", path, x, y,
                           row[x * 4 + 3]);
                    bad = 1;
                    rc = -1;
                    break;
                }
            }
        }
        free(ref);
        if (!bad) {
            printf("%s: ok, max channel diff %d\n", cases[i].name, maxdiff);
        }
next:
        file_free(ops, p);
    }
    return rc;
}

int main(void)
{
    vlog_init();
    vlog_set_global_level(VLOG_DEBUG);
    if (test_crop()) {
        return -1;
    }
    if (test_sei_nal()) {
        return -1;
    }
    if (test_bad_nal()) {
        return -1;
    }
    if (test_conversion_bounds()) {
        return -1;
    }
    if (test_color_matrix()) {
        return -1;
    }
    if (test_dst_scalar()) {
        return -1;
    }
    if (test_real_samples()) {
        return -1;
    }
    return 0;
}
