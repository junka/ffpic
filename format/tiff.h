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
    TAG_LONG = 4,       // 4 byte
    TAG_RATIONAL = 5,   // 8 byte
    TAG_SBYTE = 6,      
    TAG_UNDEFINE = 7,   // 1 byte
    TAG_SSHORT = 8,     // 2 byte
    TAG_SLONG = 9,      // 4 byte
    TAG_SRATIONAL = 10, // 
    TAG_FLOAT = 11,     // 4 byte
    TAG_DOUBLE = 12,    // 8 byte
};

enum tiff_compression_type {
    COMPRESSION_NONE = 1,
    COMPRESSION_HUFFMAN_RLE = 2, //grey
    COMPRESSION_BI_LEVEL = 3,   //grey
    COMPRESSION_FAX_ENC = 4,    //grey
    COMPRESSION_LZW = 5,
    COMPRESSION_OJPEG = 6,
    COMPRESSION_JPEG = 7,
    COMPRESSION_ADOBE_DEFLATE = 8,
    COMPRESSION_JBIG = 9,
    COMPRESSION_JBIG2 = 10,
    COMPRESSION_PACKBITS = 32773,
    COMPRESSION_DEFLATE = 32946,
    COMPRESSION_DCS = 32947,
    COMPRESSION_JP2000 = 34712,
};

struct tiff_directory_entry {
    uint16_t tag;
    uint16_t type;
    uint32_t num;
    uint32_t offset;
    uint8_t *value;
};

enum orientation_dir {
    ORI_TOP_LEFT = 1, //default baseline
    ORI_TOP_RIGHT = 2,
    ORI_BOTTOM_RIGHT = 3,
    ORI_BOTTOM_LEFT = 4,
    ORI_LEFT_TOP = 5,
    ORI_RIGHT_TOP = 6,
    ORI_RIGHT_BOTTOM = 7,
    ORI_LEFT_BOTTOM = 8
};

enum tiff_metric {
    METRIC_WhiteIsZero = 0,
    METRIC_BlackIsZero = 1,
    METRIC_RGB = 2,
    METRIC_RGB_Palette = 3,
    METRIC_Tranparency_Mask = 4,
    METRIC_CMYK = 5,
    METRIC_YCbCr = 6,
    METRIC_CIELab = 8,
};

#define MAX_DESC_LEN (128)
struct tiff_file_directory {
    uint16_t num;
    struct tiff_directory_entry * de;
    uint32_t next_offset;

    //keep below as uint32_t aligned
    uint32_t height;
    uint32_t width;
    uint32_t depth;
    uint32_t compression;
    uint32_t bitpersample[3];
    uint32_t bit_order;
    uint32_t orientation;
    uint32_t subfile;
    uint32_t pixel_store; // 1 as chunky , 2 as planar; default is 1
    uint32_t metric; // 0 as white is zero, 1 as black is zero, 2 rgb , 3 pallete
    uint32_t predictor; // 1 : no prediction;  2: horizontal differencing
    uint32_t rows_per_strip;
    uint32_t strips_num;
    uint32_t* strip_offsets;
    uint32_t* strip_byte_counts;

    char description[MAX_DESC_LEN];

    unsigned char *data;
};

struct tiff_file_header {
    uint16_t byteorder;
    uint16_t version;
    uint32_t start_offset;
};

#pragma pack(pop)

#define TID_NEWSUBFILETYPE 254
#define TID_IMAGEWIDTH 256
#define TID_IMAGEHEIGHT 257
#define TID_BITSPERSAMPLE 258
#define TID_COMPRESSION 259
#define TID_PHOTOMETRICINTERPRETATION 262
#define TID_THRESHOLDING 263
#define TID_CELLLENGTH 265
#define TID_FILLORDER 266
#define TID_IMAGEDESCRIPTION 270
#define TID_MAKE 271
#define TID_MODEL 272
#define TID_STRIPOFFSETS 273
#define TID_ORIENTATION 274
#define TID_SAMPLESPERPIXEL 277
#define TID_ROWSPERSTRIP 278
#define TID_STRIPBYTECOUNTS 279
#define TID_MINSAMPLEVALUE 280
#define TID_MAXSAMPLEVALUE 281
#define TID_XRESOLUTION 282
#define TID_YRESOLUTION 283
#define TID_PLANARCONFIGURATION 284
#define TID_FREEOFFSETS 288
#define TID_FREEBYTECOUNTS 289
#define TID_GREYRESPONSUNIT 290
#define TID_GREYRESPONSECURVE 291
#define TID_T4OPTIONS 292
#define TID_RESOLUTION_UNIT 296

#define TID_DOCUMENTNAME 269
#define TID_PAGENAME 285
#define TID_PAGENUMBER 297

#define TID_SOFTWARE 305
#define TID_DATETIME 306
#define TID_ARTIST 315
#define TID_HOSTCOMPUTER 316

//LZW can benefit
#define TID_PREDICTOR 317
#define TID_COLORMAP 320

#define TID_WHITEPOINT 318
#define TID_PRIMARYCHROMATICITIES 319

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

#define TID_COPYRIGHT 33432


typedef struct {
    struct tiff_file_header ifh;
    int ifd_num;
    struct tiff_file_directory *ifd;
} TIFF;


void TIFF_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_TIFF_H_*/