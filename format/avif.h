#ifndef _AVIF_H_
#define _AVIF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"
#include "heif.h"

typedef struct {
    struct ftyp_box ftyp;
    struct meta_box meta;
    int mdat_num;
    struct mdat_box *mdat;

    struct heif_item *items;
} AVIF;



void AVIF_init(void);


#ifdef __cplusplus
}
#endif

#endif