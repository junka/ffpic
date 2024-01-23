#ifndef _JPG_H_
#define _JPG_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "byteorder.h"
#include <stdint.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#define _MARKER(x, v) (v<<8|x)
#else
#defien _MARKER(x, v) (x<<8|v)
#endif

#define MARKER(v) _MARKER(0xFF, v)

//baseline DCT-based JPEG
#define SOF0 MARKER(0xC0)
#define SOF1 MARKER(0xC1)
/* usually unsupported below */
//Progressive DCT-based JPEG
#define SOF2 MARKER(0xC2)
#define SOF3 MARKER(0xC3)

#define SOF5 MARKER(0xC5)
#define SOF6 MARKER(0xC6)
#define SOF7 MARKER(0xC7)

//for arithmetic coding, usually unsupported
#define SOF9 MARKER(0xC9)
#define SOF10 MARKER(0xCA)
#define SOF11 MARKER(0xCB)

#define SOF13 MARKER(0xCD)
#define SOF14 MARKER(0xCE)
#define SOF15 MARKER(0xCF)

//undefined/reserved (causes decoding error)
// #define JPG     MARKER(0xC8)
// ignore (skip)
#define JPG0    MARKER(0xF0)
#define JPG13   MARKER(0xFD)

#define DHT MARKER(0xC4)

//Define Arithmetic Table
#define DAC MARKER(0xCC)

//RSTn for resync, may be ignored
#define RST0 MARKER(0xD0)
#define RST1 MARKER(0xD1)
#define RST2 MARKER(0xD2)
#define RST3 MARKER(0xD3)
#define RST4 MARKER(0xD4)
#define RST5 MARKER(0xD5)
#define RST6 MARKER(0xD6)
#define RST7 MARKER(0xD7)


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
    /* sampling factors , bit 0-3 vertical., 4-7 horizontal. */
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t vertical : 4;
    uint8_t horizontal : 4;
#else
    uint8_t horizontal : 4;
    uint8_t vertical : 4;
#endif
    uint8_t qt_id;  /* quantization table number */
};

struct sof {
    uint16_t len;
    uint8_t precision;  /*This is in bits/sample, usually 8 (12 and 16 not supported by most software).*/
    uint16_t height;
    uint16_t width;
    uint8_t components_num; // 1 for grey scaled, 3 for YCbCr or YIQ , and 4 for CMYK
    struct jpg_component colors[4];
};

struct jfif_app0 {
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
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t precision:4;   /* 0 means 8bits, 1 means 16bits*/
    uint8_t id : 4;        /* 0 - 3 */
#else
    uint8_t id : 4;          /* 0 - 3 */
    uint8_t precision : 4;   /* 0 means 8bits, 1 means 16bits*/
#endif
    uint16_t tdata[64]; // this should work for both precision 0, 1
};

struct dht {
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t huffman_id: 4;   //low 4 bits
    uint8_t table_class : 4; //high 4 bits
#else
    uint8_t table_class : 4; // high 4 bits
    uint8_t huffman_id : 4;  // low 4 bits
#endif
    uint8_t num_codecs[16];
    uint16_t len;
    uint8_t* data;

};

struct comp_sel {
    uint8_t component_selector;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t AC_entropy : 4; // low 4 bits
    uint8_t DC_entropy : 4; // high 4 bits
#else
    uint8_t DC_entropy : 4;  // high 4 bits
    uint8_t AC_entropy : 4;  // low 4 bits
#endif
};

struct start_of_scan {
    uint16_t len;
    uint8_t nums; //same with start_frame components_num, 1 for grey, 3 for YCbCr, 4 for CMYK
    struct comp_sel comps[4];
    uint8_t predictor_start;
    uint8_t predictor_end;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t approx_bits_l : 4;
    uint8_t approx_bits_h : 4;
#else
    uint8_t approx_bits_h : 4;
    uint8_t approx_bits_l:4;
#endif
};

struct comment_segment {
    uint16_t len;
    uint8_t *data;
};

struct dri {
    uint16_t len;
    uint16_t interval; /* This is in units of MCU blocks, means that every n MCU
                        blocks a RSTn marker can be found. The first marker will
                        be RST0, then RST1 etc, after RST7 repeating from RST0.*/
};

//APP1
struct exif {
    uint16_t len;
    uint8_t exif[6]; /*exif ascii*/
    struct tiff_header {
        uint16_t byteorder;
        uint16_t version;
        uint32_t start_offset;
    } tiff;
    //
};

#pragma pack(pop)

typedef struct {
    struct sof sof;
    struct jfif_app0 app0;
    struct dqt dqt[4];
    struct dht dht[2][16];
    struct start_of_scan sos;
    struct dri dri;
    struct comment_segment comment;

    int data_len; //compressed huffman data len
    uint8_t *data;
}JPG;


void JPG_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_JPG_H_*/
