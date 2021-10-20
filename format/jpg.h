#ifndef _JPG_H_
#define _JPG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define _LITTLE_ENDIAN_

#ifdef _LITTLE_ENDIAN_
#define _MARKER(x, v) (v<<8|x)
#else
#defien _MARKER(x, v) (x<<8|v)
#endif

#define MARKER(v) _MARKER(0xFF, v)

#define SOF0 MARKER(0xC0)
#define SOF2 MARKER(0xC2)

#define DHT MARKER(0xC4)

#define SOI MARKER(0xD8)
#define EOI MARKER(0xD9)
#define SOS MARKER(0xDA)
#define DQT MARKER(0xDB)
#define DRI MARKER(0xDD)

#define APP0 MARKER(0xE0)
#define APP1 MARKER(0xE1)
#define APP2 MARKER(0xE2)
#define APP3 MARKER(0xE3)
#define APP4 MARKER(0xE4)


#define COM MARKER(0xFE)
 
#pragma pack(push, 1)

struct jpg_color {
    uint8_t Y;
    uint8_t Cb;
    uint8_t Cr;
};

struct start_frame {
    uint16_t len;
    uint8_t accur;
    uint16_t height;
    uint16_t width;
    uint8_t color_num; // 3 for YCbCr or YIQ , and 4 for CMYK
    struct jpg_color *colors;
};

struct jfif {
    uint16_t len;
    uint8_t identifier[5];
    uint16_t major : 8;
    uint16_t minor : 8;
    uint8_t unit;
    uint16_t xdensity;
    uint16_t ydensity;
    uint8_t xthumbnail;
    uint8_t ythumbnail;
    uint8_t *data;
};

struct dqt {
    uint16_t len;
    uint8_t qulity:4;   /* 0 means 8bits, 1 means 16bits*/
    uint8_t id:4;       /* 0 - 3 */
    uint64_t *tdata;
};

struct dht {
    uint16_t len;
    uint8_t table_class : 4;
    uint8_t huffman_id: 4;
    uint8_t num_codecs[16];
    uint8_t* data;

};

struct scan_header {
    uint8_t component_selector;
    uint8_t DC_entropy:4;
    uint8_t AC_entropy:4;
};

struct start_of_scan {
    uint16_t len;
    uint8_t nums;
    struct scan_header* sheaders;
    uint8_t predictor_start;
    uint8_t predictor_end;
    uint8_t approx_bits_h:4;
    uint8_t approx_bits_l:4;

};

struct comment_segment {
    uint16_t len;
    uint8_t *data;
};

#pragma pack(pop)

typedef struct {
    struct start_frame sof;
    struct jfif app0;
    struct dqt dqt[4];
    struct dht dht[2][16];
    struct start_of_scan sos;
    struct comment_segment comment;
}JPG;


void JPG_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_JPG_H_*/