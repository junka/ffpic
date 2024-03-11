#ifndef _PNG_H_
#define _PNG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* according to https://www.w3.org/TR/2003/REC-PNG-20031110/#7Integers-and-byte-order
 *   All png integers are in network byte orders
 */

#define FOUR2UINT(a, b, c, d)  (uint32_t)(a | b << 8| c << 16 | d << 24)

#define CHARS2UINT(x) FOUR2UINT(x[0], x[1], x[2], x[3])


#pragma pack(push, 1)

enum png_chuck_type {
    CHUNK_TYPE_IHDR = FOUR2UINT('I', 'H', 'D', 'R'),
    CHUNK_TYPE_PLTE = FOUR2UINT('P', 'L', 'T', 'E'),
    CHUNK_TYPE_IDAT = FOUR2UINT('I', 'D', 'A', 'T'),
    CHUNK_TYPE_GAMA = FOUR2UINT('g', 'A', 'M', 'A'),
    CHUNK_TYPE_ICCP = FOUR2UINT('i', 'C', 'C', 'P'),
    CHUNK_TYPE_CHRM = FOUR2UINT('c', 'H', 'R', 'M'),
    CHUNK_TYPE_TEXT = FOUR2UINT('t', 'E', 'X', 't'),
    CHUNK_TYPE_ITXT = FOUR2UINT('i', 'T', 'X', 't'),
    CHUNK_TYPE_ZTXT = FOUR2UINT('z', 'T', 'X', 't'),
    CHUNK_TYPE_HIST = FOUR2UINT('h', 'I', 'S', 'T'),
    CHUNK_TYPE_BKGD = FOUR2UINT('b', 'K', 'G', 'D'),
    CHUNK_TYPE_TIME = FOUR2UINT('t', 'I', 'M', 'E'),
};

struct png_file_header {
    uint8_t signature[4];
    uint8_t version[4];
};

enum color_t {
    GREYSCALE = 0,
    TRUECOLOR = 2,
    INDEXEDCOLOR = 3,
    GREYSCALE_ALPHA = 4,
    TRUECOLOR_ALPHA = 6,
};

struct png_ihdr {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t compression;
    uint8_t filter;
    uint8_t interlace;
};

struct color{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct png_image_trailer {
    uint32_t length; // 00 00 00 00
    uint32_t end;    // 49 45 4E 44
    uint32_t crc;    // AE 42 60 82
};

struct textual_data {
    char * keyword;
    // uint8_t sep;
    char * text;
};

struct compressed_textual_data {
    char *keyword;
    // uint8_t sep;
    uint8_t compression_method;
    char *compressed_text;
};

struct international_textual_data {
    char *keyword;
    // uint8_t sep;
    uint8_t compression_flag;
    uint8_t compression_method;
    char * language_tag;
    // uint8_t sep;
    char * translated_keyword;
    // uint8_t sep;
    char *text;
};



// struct data_block {
//     uint32_t length;
//     uint32_t chunk_type;
//     uint8_t *chunk_data;
//     uint32_t crc;
// };

struct icc_profile {
    char *name;
    uint8_t compression_method;
    uint8_t* compression_profile;
};

struct chromaticities_white_point {
    uint32_t white_x;
    uint32_t white_y;
    uint32_t red_x;
    uint32_t red_y;
    uint32_t green_x;
    uint32_t green_y;
    uint32_t blue_x;
    uint32_t blue_y;
};

union background_color {
    uint16_t greyscale;
    struct {
      uint16_t red;
      uint16_t green;
      uint16_t blue;
    } rgb;
    uint8_t palette;
};

struct suggested_palette {
    char * name;
    uint8_t sample_depth;
};

struct last_modification {
    uint16_t year;
    uint8_t mon;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
};

#pragma pack(pop)

enum filter_type {
    FILTER_NONE = 0,
    FILTER_SUB = 1,
    FILTER_UP = 2,
    FILTER_AVERAGE = 3,
    FILTER_PAETH = 4
};


typedef struct PNG {
    struct png_file_header sig;
    struct png_ihdr ihdr;
    
    struct color * palette;
    
    int size;
    uint8_t *data;

    int compressed_size;
    uint8_t *compressed;

    union background_color bcolor;
    struct chromaticities_white_point cwp;
    uint32_t gamma;
    struct icc_profile icc;

    int n_text;
    struct textual_data *textual;
    int n_ctext;
    struct compressed_textual_data *ctextual;
    int n_itext;
    struct international_textual_data *itextual;

    uint16_t* freqs;
    struct last_modification last_mod;
} PNG;



void PNG_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_PNG_H_*/
