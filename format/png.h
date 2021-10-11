#ifndef _PNG_H_
#define _PNG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* according to https://www.w3.org/TR/2003/REC-PNG-20031110/#7Integers-and-byte-order
 *   All png integers are in network byte orders
 */

#define CHARS2UINT(x) (x[0]|x[1]<<8|x[2]<<16|x[3]<<24)


#pragma pack(push, 1)

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

struct png_palette {
    struct color * palette;
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

typedef struct {
    struct png_file_header sig;
    struct png_ihdr ihdr;

    int size;
    uint8_t *data;

    uint32_t n_text;
    struct textual_data *textual;
    uint32_t n_ctext;
    struct compressed_textual_data *ctextual;
    uint32_t n_itext;
    struct international_textual_data *itextual;

} PNG;

#pragma pack(pop)

void PNG_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_PNG_H_*/