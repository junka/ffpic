#ifndef _EXR_H_
#define _EXR_H_

#ifdef __cplusplus
extern "C" {
#endif

enum exr_compression {
    EXR_COMPRESSION_NONE = 0,
    EXR_COMPRESSION_RLE = 1,
    EXR_COMPRESSION_ZIPS = 2,
    EXR_COMPRESSION_ZIP = 3,
    EXR_COMPRESSION_PIZ = 4,
    EXR_COMPRESSION_PXR24 = 5,
    EXR_COMPRESSION_B44 = 6,
    EXR_COMPRESSION_B44A = 7,
    EXR_COMPRESSION_DWAA = 8,
    EXR_COMPRESSION_DWAB = 9,
};

#pragma pack(push, 1)

struct exr_header {
    uint32_t magic_number; /*first four bytes of an OpenEXR file are always
                             0x76, 0x2f, 0x31 and 0x01.*/
    uint32_t version : 8;      /*version field part which indicates whether the file is
                             single or multi-part and whether the file contains deep data.*/
    uint32_t z : 1;
    uint32_t single_tile : 1;
    uint32_t long_name : 1;
    uint32_t no_image : 1;
    uint32_t multipart : 1;
    uint32_t resv : 19;
};

struct part_attribute {
    const char * attr_name;
    const char * attr_type;
    uint32_t size;
};

struct exr_box2i {
    uint32_t xMin;
    uint32_t yMin;
    uint32_t xMax;
    uint32_t yMax;
};

struct exr_box2f {
    float xMin;
    float yMin;
    float xMax;
    float yMax;
};

enum pixel_type {
    PIXEL_TYPE_UINT = 0,
    PIXEL_TYPE_HALF = 1,
    PIXEL_TYPE_FLOAT = 2,
};

struct exr_chlist {
    char *name;
    uint32_t pixel_type;
    uint32_t plinear:8;
    uint32_t reserved:24;
    uint32_t xsampling;
    uint32_t ysampling;
};

struct exr_chromaticities {
    float redX;
    float redY;
    float greenX;
    float greenY;
    float blueX;
    float blueY;
    float whiteX;
    float whiteY;
};

struct exr_keycode {
    int32_t film_mfc_code;
    int32_t film_type;
    int32_t prefix;
    int32_t count;
    int32_t perf_offset;
    int32_t perfs_per_frame;
    int32_t perfs_per_count;
};


struct exr_preview {
    uint32_t width;
    uint32_t height;
    uint8_t* data;
};

struct exr_rational {
    int32_t  num;
    uint32_t denom; 
};

struct exr_m33f {
    float m[9];
};

struct exr_m33d {
    double m[9];
};

struct exr_m44f {
    float m[16];
};

struct exr_m44d {
    double m[16];
};

struct exr_timecode {
    uint32_t time_and_flags;
    uint32_t user_data;
};

struct exr_v2i {
    int v[2];
};

struct exr_v2f {
    float v[2];
};

struct exr_v3i {
    int v[3];
};

struct exr_v3f {
    float v[3];
};

struct exr_tiledesc {
    uint32_t x_size;
    uint32_t y_size;
    uint8_t  level_and_round;
};

#pragma pack(pop)

enum exr_line_order {
    INCREASING_Y = 0,
    DECREASING_Y = 1,
    RANDOM_Y = 2,
};

enum exr_env_map {
    ENVMAP_LATLONG = 0,
    ENVMAP_CUBE = 1,
};

typedef struct {
    struct exr_header h;
    uint32_t compression;
    int chnl_num;
    struct exr_chlist *chnls;
    struct exr_box2i data_win, display_win;
    uint8_t lineorder;
    uint8_t envmap;
    float aspect_ratio;
    struct exr_preview preview;
    struct exr_v2f win_center;
    float win_width;
    int chunkCount;

    uint8_t *data;
} EXR;

void EXR_init(void);


#ifdef __cplusplus
}
#endif


#endif /*_EXR_H_*/