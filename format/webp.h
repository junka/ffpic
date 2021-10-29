#ifndef _WEBP_H_
#define _WEBP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CHUNCK_HEADER(c) (c[3]<<24|c[2]<<16|c[1]<<8|c[0])
#define READ_UINT24(a)  (a[2]<<16 | a[1]<<8 | a[0])

#pragma pack(push, 1)

struct riff_chunck {
    uint32_t fourcc;
    uint32_t size;
    uint32_t payload;
};

struct webp_header {
    uint32_t riff;  /* RIFF ascii code */
    uint32_t file_size;
    uint32_t webp;  /* WEBP ascii code */
};

struct webp_vp8x {
    uint32_t vp8x; /* VP8X ascii code */
    uint32_t resv:2;
    uint32_t icc:1;
    uint32_t alpha:1;
    uint32_t exif_metadata:1;
    uint32_t xmp_metadata:1;
    uint32_t animation:1;
    uint32_t reserved:25;
    uint8_t canvas_width[3];
    uint8_t canvas_height[3];
};

struct webp_anim {
    uint32_t anim;  /* ANIM ascii code */
    uint32_t background;
    uint16_t loop_count;
};

struct webp_anmf {
    uint32_t anmf; /* ANMF ascii code */
    uint8_t frameX[3];
    uint8_t frameY[3];
    uint8_t width[3];
    uint8_t height[3];
    uint8_t duration[3];
    uint8_t resvd:6;
    uint8_t blending:1;
    uint8_t disposal:1;
};


struct webp_alpha {
    uint32_t alph;  /* ALPH ascii code */
    uint8_t rsv:2;
    uint8_t preprocess:2;
    uint8_t filter:2;
    uint8_t compression:2;
    uint8_t *bitstream;
};

struct webp_color {
    uint32_t iccp; /* ICCP ascii code */

};

#pragma pack(pop)

typedef struct {
    struct webp_header header;
    struct webp_vp8x vp8x;
} WEBP;

void WEBP_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_WEBP_H_*/