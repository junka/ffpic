/* The hevc picture is coded at the block aligned size and the sps
 * conformance window crops it back to the size the container advertises.
 * These cases pin that crop, and make sure the sei blocks real encoders
 * write are skipped instead of spinning on a byte sized counter. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorspace.h"
#include "hevc.h"

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
    uint16_t type;

    if (!nal) {
        return -1;
    }
    nal[0] = (uint8_t)(PREFIX_SEI_NUT << 1);
    nal[1] = 1;

    type = parse_nalu(nal, LEN, &pixels, NULL);
    free(nal);
    if (type != PREFIX_SEI_NUT || pixels != NULL) {
        printf("sei nal parsed as type %u\n", (unsigned)type);
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
                           HEIGHT, CTB);

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

int main(void)
{
    if (test_crop()) {
        return -1;
    }
    if (test_sei_nal()) {
        return -1;
    }
    if (test_conversion_bounds()) {
        return -1;
    }
    return 0;
}
