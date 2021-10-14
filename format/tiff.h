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
};

struct tiff_file_header {
    uint16_t byteorder;
    uint16_t version;
    uint32_t start_offset;
};

#pragma pack(pop)

typedef struct {
    struct tiff_file_header ifh;
    struct tiff_file_directory *ifd;
} TIFF;


void TIFF_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_TIFF_H_*/