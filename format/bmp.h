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

struct bmp_info_header1 {
    uint32_t size; // Size of this header (in bytes), 12
    uint16_t  width; // width of bitmap in pixels
    uint16_t  height; // height of bitmap in pixels
    uint16_t planes;    // No. of planes for the target device, this is always 1
    uint16_t bit_count; // No. of bits per pixel, 1, 4, 8, 24
};

struct bmp_info_header {
    uint32_t size;                  // Size of this header (in bytes), 40
    int32_t width;                  // width of bitmap in pixels
    int32_t height;                 // height of bitmap in pixels
                                    //   (if positive, bottom-up, with origin in lower left corner)
                                    //   (if negative, top-down, with origin in upper left corner)
    uint16_t planes;                // No. of planes for the target device, this is always 1
    uint16_t bit_count;             // No. of bits per pixel, 1, 4, 8, 24
    uint32_t compression;           // 0 or 3 - uncompressed. 
    uint32_t size_image;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
};

struct bmp_info_header2 {
    uint32_t size; // Size of this header (in bytes), 64 bytes
    int32_t width; // width of bitmap in pixels
    int32_t height; // height of bitmap in pixels
    uint16_t planes;    // No. of planes for the target device, this is always 1
    uint16_t bit_count; // No. of bits per pixel, 1, 4, 8, 24
    uint32_t compression; // 0 or 3 - uncompressed.
    uint32_t size_image;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
    uint16_t units;
    uint16_t reserved;
    uint16_t recording;
    uint16_t rendering;
    uint32_t size1;
    uint32_t size2;
    uint32_t color_encoding;
    uint32_t identifier;
};

/* BITMAPV4HEADER */
struct bmp_color_header {
    uint32_t red_mask;         // Bit mask for the red channel
    uint32_t green_mask;       // Bit mask for the green channel
    uint32_t blue_mask;        // Bit mask for the blue channel
    uint32_t alpha_mask;       // Bit mask for the alpha channel
    uint32_t color_space_type; // Default "sRGB" (0x73524742)
    uint32_t unused[16];       // Unused data for sRGB color space
};

// struct bmp_v5_header {
// };

enum halftoning_algrithm {
   NONE = 0,
   ERROR_DIFFUSION = 1,
   PANDA = 2,
   SUPER_CIRCLE = 3
};

struct bmp_info_header3 {
    uint16_t resolutions;
    uint16_t padding;
    uint16_t fill_direction;
    uint16_t halftoning_alg;
    uint32_t halftoning_para1;
    uint32_t halftoning_para2;
    uint32_t color_encoding;
    uint32_t app_identifier;
};


struct bmp_color_entry {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
};

#pragma pack(pop)

typedef struct BMP {
    struct bmp_file_header file_header;
    union {
        struct bmp_info_header1 v1;
        struct bmp_info_header v2;
    } dib;
    struct bmp_color_header color;
    struct bmp_color_entry * palette;
    uint8_t *data;
} BMP;

void BMP_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_BMP_H_*/
