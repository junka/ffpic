#ifndef _AVIF_H_
#define _AVIF_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "byteorder.h"
#include "basemedia.h"

#pragma pack(push, 1)

/* AV1CodecConfigurationBox */
struct av1C_box {
    BOX_ST;
#ifdef LITTLE_ENDIAN
    uint8_t version:7;
    uint8_t marker:1;

    uint8_t seq_level_idx_0:5;
    uint8_t seq_profile:3;

    uint8_t chroma_sample_position:2;
    uint8_t chroma_subsampling_y:1;
    uint8_t chroma_subsampling_x:1;
    uint8_t monochrome:1;
    uint8_t twelve_bit:1;
    uint8_t high_bitdepth:1;
    uint8_t seq_tier_0:1;

    uint8_t initial_presentation_delay_minus_one:4;
    uint8_t initial_presentation_delay_present:1;
    uint8_t reserved:3;

#else
    uint8_t marker:1;
    uint8_t version:7;

    uint8_t seq_profile:3;
    uint8_t seq_level_idx_0:5;

    uint8_t seq_tier_0:1;
    uint8_t high_bitdepth:1;
    uint8_t twelve_bit:1;
    uint8_t monochrome:1;
    uint8_t chroma_subsampling_x:1;
    uint8_t chroma_subsampling_y:1;
    uint8_t chroma_sample_position:2;

    uint8_t reserved:3;
    uint8_t initial_presentation_delay_present:1;
    uint8_t initial_presentation_delay_minus_one:4;
#endif

    uint8_t *configOBUs;

};


#pragma pack(pop)

struct av1_meta_box {
    FULL_BOX_ST;
    struct hdlr_box hdlr;
    struct pitm_box pitm;
    struct iloc_box iloc;
    struct iinf_box iinf;
    struct iprp_box iprp;
    struct iref_box iref;
};

typedef struct {
    struct ftyp_box ftyp;
    struct av1_meta_box meta;
    int mdat_num;
    struct mdat_box *mdat;

    // struct avif_item *items;
} AVIF;



void AVIF_init(void);


#ifdef __cplusplus
}
#endif

#endif