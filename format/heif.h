#ifndef _HEIF_H_
#define _HEIF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hevc.h"

#pragma pack(push, 1)


/* HEVC Decoder Configuration Record
 * See 14496-15 8.3.3.1 */
struct hvcC_box {
    BOX_ST;
    uint8_t configurationVersion;
#ifdef LITTLE_ENDIAN
    uint8_t general_profile_idc:5;
    uint8_t general_tier_flag:1;
    uint8_t general_profile_space:2;
#else
    uint8_t general_profile_space:2;
    uint8_t general_tier_flag:1;
    uint8_t general_profile_idc:5;
#endif
    uint32_t general_profile_compatibility_flags;

    uint32_t general_constraint_indicator_flags_h;
    uint16_t general_constraint_indicator_flags_l;

    uint8_t general_level_idc;
#ifdef LITTLE_ENDIAN
    uint8_t min_spatial_segmentation_idc1:4;
    uint8_t reserved:4;
#else
    uint8_t reserved:4;
    uint8_t min_spatial_segmentation_idc1:4;
#endif

    uint8_t min_spatial_segmentation_idc2;

#ifdef LITTLE_ENDIAN
    uint8_t parallelismType:2;
    uint8_t rsd1:6;
#else
    uint8_t rsd1:6;
    uint8_t parallelismType:2;
#endif

#ifdef LITTLE_ENDIAN
    uint8_t chroma_format_idc:2;
    uint8_t rsd2:6;
#else
    uint8_t rsd2:6;
    uint8_t chroma_format_idc:2;
#endif

#ifdef LITTLE_ENDIAN
    uint8_t bit_depth_luma_minus8:3;
    uint8_t rsd3:5;
#else
    uint8_t rsd3:5;
    uint8_t bit_depth_luma_minus8:3;
#endif

#ifdef LITTLE_ENDIAN
    uint8_t bit_depth_chroma_minus8:3;
    uint8_t rsd4:5;
#else
    uint8_t rsd4:5;
    uint8_t bit_depth_chroma_minus8:3;
#endif

    uint16_t avgframerate;
#ifdef LITTLE_ENDIAN
    uint8_t lengthSizeMinusOne:2;
    uint8_t temporalIdNested:1;
    uint8_t numtemporalLayers:3;
    uint8_t constantframerate:2;
#else
    uint8_t constantframerate:2;
    uint8_t numtemporalLayers:3;
    uint8_t temporalIdNested:1;
    uint8_t lengthSizeMinusOne:2;
#endif
    uint8_t num_of_arrays;
    struct nal_arr *nal_arrays;
};

#pragma pack(pop)

/* see 8.11.1 */
struct meta_box {
    FULL_BOX_ST;
    struct hdlr_box hdlr;
    struct pitm_box pitm;
    struct iloc_box iloc;
    struct iinf_box iinf;
    struct iprp_box iprp;
    struct iref_box iref;
};

struct heif_item {
    const struct item_location *item;
    uint32_t type;
    uint64_t length;
    uint8_t *data;
};

typedef struct {
    struct ftyp_box ftyp;
    struct meta_box meta;
    int mdat_num;
    // struct mdat_box *mdat;

    struct heif_item *items;
} HEIF;



void HEIF_init(void);

struct pic* HEIF_load(const char *filename);

#ifdef __cplusplus
}
#endif

#endif
