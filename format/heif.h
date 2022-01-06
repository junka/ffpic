#ifndef _HEIF_H_
#define _HEIF_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"

#pragma pack(push, 1)

/* see iso_ico 230008-12 9.3*/
struct iprp_box {
    uint32_t size;
    uint32_t type;
    uint32_t version: 8;
    uint32_t flags : 24;

};

struct ipco_box {
    uint32_t size;
    uint32_t type;
};

#pragma pack(pop)

typedef struct {
    struct ftyp_box ftyp;

} HEIF;



void HEIF_init(void);


#ifdef __cplusplus
}
#endif

#endif