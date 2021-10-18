#ifndef _TIFF_H_
#define _TIFF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#pragma pack(push, 1)

enum tag_type {
    TAG_BYTE = 1,
    TAG_ASCII = 2,
    TAG_SHORT = 3,
    TAG_LONG = 4,
    TAG_RATIONAL = 5,
};

struct tiff_directory_entry {
    uint16_t tag;
    uint16_t type;
    uint32_t len;
    uint32_t offset;
    uint8_t *value;
};


struct tiff_file_directory {
    uint16_t num;
    struct tiff_directory_entry * de;
    uint32_t next_offset;


    int height;
    int width;
    int depth;
    int compression;
};

struct tiff_file_header {
    uint16_t byteorder;
    uint16_t version;
    uint32_t start_offset;
};

#pragma pack(pop)

#define TID_IMAGEWIDTH 256
#define TID_IMAGEHEIGHT 257
#define TID_BITSPERSAMPLE 258
#define TID_COMPRESSION 259
#define TID_PHOTOMETRICINTERPRETATION 262
#define TID_FILLORDER 266
#define TID_STRIPOFFSETS 273
#define TID_SAMPLESPERPIXEL 277
#define TID_ROWSPERSTRIP 278
#define TID_STRIPBYTECOUNTS 279
#define TID_PLANARCONFIGUATION 284 
#define TID_T4OPTIONS 292
#define TID_PREDICTOR 317 
#define TID_COLORMAP 320


#define TID_TILEWIDTH 322
#define TID_TILELENGTH 323 
#define TID_TILEOFFSETS 324 
#define TID_TILEBYTECOUNTS 325
#define TID_EXTRASAMPLES 338
#define TID_SAMPLEFORMAT 339 
#define TID_SMINSAMPLEVALUE 340
#define TID_SMAXSAMPLEVALUE 341
#define TID_YCBCRCOEFFICIENTS 529
#define TID_YCBCRSUBSAMPLING 530 
#define TID_YCBCRPOSITIONING 531 

typedef struct {
    struct tiff_file_header ifh;
    int ifd_num;
    struct tiff_file_directory *ifd;

    int height;
    int width;
    int depth;
    int compression;
} TIFF;


void TIFF_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_TIFF_H_*/