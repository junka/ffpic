#ifndef _PSD_H_
#define _PSD_H_

#ifdef __cplusplus
extern "C" {
#endif

enum resource_id {
    OBSOLETE = 0x03E8,
    MACINTOSH_PRINT_MANAGER = 0x03E9,
    INDEXED_COLOR_TABLE = 0x03EB,
    RESOLUTION_INFO = 0x03ED,
};

enum color_mode {
    COLOR_MODE_BITMAP = 0,
    COLOR_MODE_GRESCALE = 1,
    COLOR_MODE_INDEXED = 2,
    COLOR_MODE_RGB = 3,
    COLOR_MODE_CMYK = 4,
    COLOR_MODE_MUTICHANNEL = 7,
    COLOR_MODE_DUOTONE = 8,
    COLOR_MODE_LAB = 9,
};

#pragma pack(push, 2)

struct psd_file_header {
    uint32_t signature; /* always '8PS' */
    uint16_t version;   /* always 1 */
    uint16_t reserv[3]; /* must be zero*/
    uint16_t chan_num;  /* 1 to 56*/
    uint32_t height;
    uint32_t width;
    uint16_t depth;
    uint16_t mode;  /*color mode: 0 bitmap, 1 greyscale, 2 index, 3 rgb*/
};

struct color_mode_data {
    uint32_t length;
    uint8_t *data;
};


/* 0x03ED */
struct resolution_info {
    uint32_t horizontal;
    uint16_t horizontal_unit;
    uint16_t width_unit;
    uint32_t vertical;
    uint16_t vertical_unit;
    uint16_t height_unit;
};

/* 0x0408 */
struct grid_and_guides_header {
    uint32_t version;
    uint32_t horizontal;
    uint32_t vertical;
    uint32_t guide_count;
    struct grid_and_guides_block
    {
        uint32_t location;
        uint8_t direction;
    } * b;
    
};

/* 0x040F */
/* 0z0431 */
struct color_sample_header {
    uint32_t version;
    uint32_t num;
    struct color_sample_block
    {
        uint32_t version;
        uint32_t horizontal;
        uint32_t vertical;
        uint16_t color_space;
        uint16_t depth;
    } *b ;
    
};

struct image_resource_block {
    uint32_t signature; /* 8BIM */
    uint16_t id;
    uint8_t *name;
    uint32_t size;
    uint8_t *data; /* this depends on type */
};

struct image_resource {
    uint32_t length;
    int num;
    // struct image_resource_block *block;

    struct resolution_info resolution;

};

struct channel_image {
    uint16_t compression; /* 0 raw data, 1 RLE, 2 ZIP */
    uint8_t *data;
};

struct channel_info {
    int16_t id; /*  0 = red, 1 = green, -1 = transparency mask;
                    -2 = user supplied layer mask,
                    -3 real user supplied layer mask*/
    uint32_t len;
};

struct layer_mask_data {
    uint32_t size;
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
    uint8_t default_color;

    uint8_t relative : 1;
    uint8_t mask_disable : 1;
    uint8_t invert : 1;
    uint8_t other : 1;
    uint8_t applied : 1;
    uint8_t reserv :3;

    uint8_t mask_parameter; /* Only present if bit 4 of Flags set above. */
    uint8_t *mask;      /* Mask Parameters bit flags present as follows:
                            bit 0 = user mask density, 1 byte
                            bit 1 = user mask feather, 8 byte, double
                            bit 2 = vector mask density, 1 byte
                            bit 3 = vector mask feather, 8 bytes, double */
    uint16_t padding;   /* Only present if size = 20. Otherwise the following is present */
    uint8_t real_flag;
    uint8_t real_user_mask;

    uint32_t real_top;
    uint32_t real_left;
    uint32_t real_bottom;
    uint32_t real_right;
    
};

struct layer_blending_range_data {
    uint32_t length;
    uint32_t source;
    uint32_t dest;
    struct {
        uint32_t src;
        uint32_t dst;
    } * ranges;
};


struct channel_record {
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
    uint16_t num;
    struct channel_info *info;
    uint32_t blend;
    uint32_t blend_key;
    uint8_t opacity;
    uint8_t clipping;
    uint8_t transparency : 1;
    uint8_t visible : 1;
    uint8_t obsolete : 1;
    uint8_t use_bit4 : 1;
    uint8_t irrelevant : 1;
    uint8_t reserv :3;
    uint8_t filter;
    uint32_t extra_len;
    struct layer_mask_data mask_data;
    struct layer_blending_range_data blend_data;
    uint8_t *name; /* padded to a multiple of 4 bytes */
};

struct layer_info {
    uint32_t length;
    uint16_t count;
    struct channel_record *records;
    struct channel_image *chan_data;
};

struct layer_mask {
    uint32_t length;
    uint16_t overlay_cs;
    uint16_t cs_component[4];
    uint16_t opacity; /* 0 transparent, 100 opaque */
    uint8_t kind;   /* 0 color */
    uint8_t *filter;
};

struct extra_layer_info {
    uint32_t signature; /* '8BIM' or '8B64' */
    uint32_t key;       /* 4-char code */
    uint32_t length;
    uint8_t *data;
};


struct layer_and_mask {
    uint32_t length;
    struct layer_info info;

    struct layer_mask mask;
    int extra_num;
    struct extra_layer_info *extra;
};


#pragma pack(pop)

typedef struct {
    struct psd_file_header h;
    struct color_mode_data color;
    struct image_resource res;
    
    struct layer_and_mask layer;

    uint16_t compression;
    uint8_t *data;
} PSD;

void PSD_init(void);

#ifdef __cplusplus
}
#endif


#endif /*_PSD_H_*/