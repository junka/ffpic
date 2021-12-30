#ifndef _HEIF_H_
#define _HEIF_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"

#pragma pack(push, 1)



struct heif_ftyp {
    uint32_t len;
    uint32_t major_brand;
    uint32_t minor_version;
    uint8_t *extended_compatible;
};

#pragma pack(pop)

typedef struct {


} HEIF;



void HEIF_init(void);


#ifdef __cplusplus
}
#endif

#endif