#ifndef _BMP_H_
#define _BMP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#pragma pack(push, 2)

struct bmp_file_header {
    uint16_t file_type;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset_data;
};

enum compression_type {
    BI_RGB = 0,
    BI_RLE8 = 1,
    BI_RLE4 = 2,
    BI_BITFIELDS = 3,
    BI_JPEG = 4,
    BI_PNG = 5,
    BI_ALPHABITFIELDS = 6,
    BI_CMYK = 11,
    BI_CMYKRLE8 = 12,
    BI_CMYKRLE4 = 13,
};

struct bmp_info_header {
    uint32_t size;                      // Size of this header (in bytes)
    int32_t width;                      // width of bitmap in pixels
    int32_t height;                     // width of bitmap in pixels
                                        //       (if positive, bottom-up, with origin in lower left corner)
                                        //       (if negative, top-down, with origin in upper left corner)
    uint16_t planes;                    // No. of planes for the target device, this is always 1
    uint16_t bit_count;                 // No. of bits per pixel
    uint32_t compression;               // 0 or 3 - uncompressed. 
    uint32_t size_image;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
};

enum halftoning_algrithm {
   NONE = 0,
   ERROR_DIFFUSION = 1,
   PANDA = 2,
   SUPER_CIRCLE = 3
};

struct bmp_info_header2 {
    uint16_t resolutions;
    uint16_t padding;
    uint16_t fill_direction;
    uint16_t halftoning_alg;
    uint32_t halftoning_para1;
    uint32_t halftoning_para2;
    uint32_t color_encoding;
    uint32_t app_identifier;
};

struct bmp_color_header {
    uint32_t red_mask;         // Bit mask for the red channel
    uint32_t green_mask;       // Bit mask for the green channel
    uint32_t blue_mask;        // Bit mask for the blue channel
    uint32_t alpha_mask;       // Bit mask for the alpha channel
    uint32_t color_space_type; // Default "sRGB" (0x73524742)
    uint32_t unused[16];       // Unused data for sRGB color space
};

#pragma pack(pop)

typedef struct BMP {
    struct bmp_file_header file_header;
    struct bmp_info_header dib;
    struct bmp_color_header color;
    void *data;
} BMP;

void BMP_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_BMP_H_*/