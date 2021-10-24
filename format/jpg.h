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


#define EOB (0x00)

 
#pragma pack(push, 1)

struct jpg_component {
    uint8_t cid; /* component Id (1 = Y, 2 = Cb, 3 = Cr, 4 = I, 5 = Q)*/
    uint8_t vertical : 4;  /* sampling factors , bit 0-3 vertical., 4-7 horizontal.*/
    uint8_t horizontal : 4;
    uint8_t qt_id;  /* quantization table number */
};

struct start_frame {
    uint16_t len;
    uint8_t precision;  /*This is in bits/sample, usually 8 (12 and 16 not supported by most software).*/
    uint16_t height;
    uint16_t width;
    uint8_t components_num; // 1 for grey scaled, 3 for YCbCr or YIQ , and 4 for CMYK
    struct jpg_component *colors;
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
    uint8_t precision:4;   /* 0 means 8bits, 1 means 16bits*/
    uint8_t id:4;       /* 0 - 3 */
    uint16_t *tdata;
};

struct dht {
    uint16_t len;
    uint8_t table_class : 4;
    uint8_t huffman_id: 4;
    uint8_t num_codecs[16];
    uint8_t* data;

};

struct comp_sel {
    uint8_t component_selector;
    uint8_t DC_entropy:4;
    uint8_t AC_entropy:4;
};

struct start_of_scan {
    uint16_t len;
    uint8_t nums; //same with start_frame components_num, 1 for grey, 3 for YCbCr, 4 for CMYK
    struct comp_sel* comps;
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
    uint8_t *data;
}JPG;


void JPG_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_JPG_H_*/