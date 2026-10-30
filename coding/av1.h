#ifndef _AV1_H_
#define _AV1_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* AV1 still picture decoding, following the AV1 Bitstream & Decoding
 * Process Specification.  Only the intra/key frame tools needed by AVIF
 * image items are implemented; inter prediction, reference frames and film
 * grain are not part of a still image item. */

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
};

/* Decodes a low overhead AV1 bitstream (one still picture) into planar
 * 16 bit samples.  Returns 0 on success. */
int av1_decode_image(const uint8_t *data, size_t len, struct av1_image *img);

void av1_image_free(struct av1_image *img);

/* Converts the decoded planes to packed BGRA (the byte order used by the
 * RGBA8888 pictures in this project).  dst has room for pitch * height
 * bytes. */
int av1_image_to_bgra(const struct av1_image *img, uint8_t *dst, int pitch);

#ifdef __cplusplus
}
#endif

#endif /* _AV1_H_ */
