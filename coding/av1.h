#ifndef _AV1_H_
#define _AV1_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* AV1 still picture decoding, following the AV1 Bitstream & Decoding
 * Process Specification.  Only the intra/key frame tools needed by AVIF
 * image items are implemented; inter prediction and reference frames are
 * not part of a still image item.  Film grain is synthesised when the
 * sequence and frame headers ask for it. */

struct av1_image {
    int width;                  /* upscaled / display width */
    int height;                 /* frame height */
    int bitdepth;
    int num_planes;
    int mono_chrome;
    int subsampling_x;
    int subsampling_y;
    int color_range;            /* 1 = full range */
    int matrix_coefficients;
    int color_primaries;
    int transfer_characteristics;
    int chroma_sample_position;
    uint16_t *plane[3];
    int stride[3];              /* in samples */

    /* OBU_METADATA HDR, copied from the bitstream when present.
     * Zeroed fields mean "not signalled" and the caller should fall back
     * to the legacy 10000 / 1000 cd/m2 assumptions in the tone mapper. */
    int hdr_cll_present;
    uint16_t max_content_light_level;
    uint16_t max_frame_average_light_level;
    int hdr_mdcv_present;
    uint32_t luminance_max;
    uint32_t luminance_min;
};

/* Decodes a low overhead AV1 bitstream (one still picture) into planar
 * 16 bit samples.  Returns 0 on success. */
int av1_decode_image(const uint8_t *data, size_t len, struct av1_image *img);

void av1_image_free(struct av1_image *img);

/* Converts the decoded planes to packed BGRA (the byte order used by the
 * RGBA8888 pictures in this project).  dst has room for pitch * height
 * bytes. */
int av1_image_to_bgra(const struct av1_image *img, uint8_t *dst, int pitch);

/* The same conversion at full 16 bit per channel, for 10 and 12 bit
 * streams.  The samples are scaled to the 0..65535 range. */
int av1_image_to_rgba16(const struct av1_image *img, uint8_t *dst, int pitch);

#ifdef __cplusplus
}
#endif

#endif /* _AV1_H_ */
