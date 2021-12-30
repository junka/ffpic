#ifndef _AVIF_H_
#define _AVIF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"


struct avif_ftyp {
    uint32_t len;
    uint32_t major_brand;
    uint32_t minor_version;
    uint8_t *extended_compatible;
};


typedef struct {


} AVIF;



void AVIF_init(void);


#ifdef __cplusplus
}
#endif

#endif