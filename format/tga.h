#ifndef _TGA_H_
#define _TGA_H_

#ifdef __cplusplus
extern "C" {
#endif

enum IMAGE_TYPE {
    IMAGE_NONE = 0,
    IMAGE_UNCOMPRESS_INDEXED = 1,
    IMAGE_UNCONPRESS_RGB = 2,
    IMAGE_UNCONPRESS_GREY = 3,
    IMAGE_RLE_INDEXED = 9,
    IMAGE_RLE_RGB = 10,
    IMAGE_RLE_GREY = 11,
    IMAGE_DEFLATE_INDEXED = 32,
    IMAGE_DEFLATE_QUADTREE = 33,
};


#pragma pack(push, 1)

struct tga_header {
    uint8_t ident_size;     //Length of the image ID field (0-255)
    uint8_t color_map_type; // 0 is none , 1 has colormap
    uint8_t image_type;     //Compression and color types see enum IMAGE_TYPE 
    uint16_t color_map_first_index;
    uint16_t color_map_length;
    uint8_t color_map_entry_size;
    uint16_t xstart;
    uint16_t ystart;
    uint16_t width;
    uint16_t height;
    uint8_t bits_depth; //per pixels, 8, 16. 24. 32
    uint8_t descriptor; //vh 
};


struct tga_footer {
    uint32_t extension_offset;
    uint32_t developer_offset;
    uint8_t signature[16];  //contain "TRUEVISION-XFILE"
    uint8_t end;            //contain '.'
    uint8_t zero;           //contain 0
};

struct tga_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

#pragma pack(pop)

typedef struct {
    struct tga_header head;
    struct tga_footer foot;
    struct tga_color *cmap;
    uint8_t *data;
}TGA;


void TGA_init(void);


#ifdef __cplusplus
}
#endif

#endif /*_TGA_H_*/