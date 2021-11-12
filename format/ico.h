#ifndef _ICO_H_
#define _ICO_H_

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)

struct ico_header {
    uint16_t rsv_zero;  /* reserved, must be 0 */
    uint16_t type;      /* 1 for icno(.ico) , 2 for cursor (.CUR) image */
    uint16_t num;       /* number of images */
};

struct ico_directory {
    uint8_t width;      /* 0 if 256 pixels*/
    uint8_t height;     /* 0 if 256 pixels*/
    uint8_t color_num;
    uint8_t reserved;
    uint16_t planes;    /* Color planes when in .ICO format, should be 0 or 1, or the X hotspot when in .CUR format */
    uint16_t depth;     /* Bits per pixel when in .ICO format, or the Y hotspot when in .CUR format */
    uint32_t size;
    uint32_t offset;
};

struct icobmp_info_header {
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

struct icobmp_color_entry {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
};

struct ico_image_data {
    struct icobmp_info_header bmpinfo;
    struct icobmp_color_entry *color;
    uint8_t* data;
    // uint8_t* and_data;
};

#pragma pack(pop)

typedef struct{
    struct ico_header head;
    struct ico_directory* dir;
    struct ico_image_data *images;

} ICO;


void ICO_init(void);

#ifdef __cplusplus
}
#endif


#endif /*_ICO_H_*/