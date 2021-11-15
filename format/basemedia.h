#ifndef _BASEMEDIA_H_
#define _BASEMEDIA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>


#define TYPE2UINT(x) (x[0]|x[1]<<8|x[2]<<16|x[3]<<24)

static char * type2name(uint32_t type)
{
    char* name = malloc(5);
    char c1 = (type >> 24) & 0xFF;
    char c2 = (type >> 16) & 0xFF;
    char c3 = (type >> 8) & 0xFF;
    char c4 = (type) & 0xFF;
    sprintf(name, "%c%c%c%c", c4, c3, c2, c1);
    name[4]= '\0';
    return name;
}

#define UINT2TYPE(x) type2name(x)

#define FTYP TYPE2UINT("ftyp")
#define META TYPE2UINT("meta")
#define MDAT TYPE2UINT("mdat")
//prededined box type should be list in each file type header

#pragma pack(push, 2)

struct box {
    uint32_t size;
    uint32_t type;
    uint8_t buf[0];
};

struct ftyp_box {
    uint32_t size;
    uint32_t major_brand;
    uint32_t minor_version;
    uint32_t *compatible_brands;
};

#pragma pack(pop)

uint32_t read_box(FILE *f, void * d, int len);

void read_ftyp(FILE *f, void *d);

void print_box(FILE *f, struct box *b);
#ifdef __cplusplus
}
#endif

#endif /*_BASEMEDIA_H_*/