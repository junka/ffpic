#ifndef _HEIF_H_
#define _HEIF_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"

#pragma pack(push, 1)

/* HEVC configuration item see 14496-15 */
struct nalus {
    uint16_t unit_length; 
    uint64_t nal_unit;  //size depends on unit_length
};

struct nal_arr {
    uint8_t array_completeness:1;
    uint8_t reserved:1;
    uint8_t nal_unit_type:6;
    uint16_t numNalus;
    struct nalus * nals;
};

/* HEVC Decoder Configuration Record
 * See 14496-15 8.3.3.1 */
struct hvcC_box {
    BOX_ST;
    uint8_t configurationVersion;
    uint8_t general_profile_space:2;
    uint8_t general_tier_flag:1;
    uint8_t general_profile_idc:5;

    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags: 48;
    uint64_t general_level_idc:8;
    uint64_t reserved:4;
    uint64_t min_spatial_segmentation_idc1:4;
    uint8_t min_spatial_segmentation_idc2;

    uint8_t rsd1:6;
    uint8_t parallelismType:2;

    uint8_t rsd2:6;
    uint8_t chroma_format_idc:2;

    uint8_t rsd3:5;
    uint8_t bit_depth_luma_minus8:3;

    uint8_t rsd4:5;
    uint8_t bit_depth_chroma_minus8:3;

    uint16_t avgframerate;

    uint8_t constantframerate:2;
    uint8_t numtemporalLayers:3;
    uint8_t temporalIdNested:1;
    uint8_t lengthSizeMinusOne:2;

    uint8_t num_of_arrays;
    struct nal_arr *nal_arrays;
};

/* see IEC 23008-12 6.5.3 */
struct ispe_box {
    FULL_BOX_ST;
    uint32_t image_width;
    uint32_t image_height;
};

/* may have hvcC, ispe */
struct ipco_box {
    BOX_ST;
    struct box *property[2];
};

//item property association
struct ipma_item {
    uint32_t item_id;
    uint8_t association_count;
    uint16_t* association;
};

struct ipma_box {
    FULL_BOX_ST;
    uint32_t entry_count;
    struct ipma_item *entries; 
};


/* see iso_ico 230008-12 9.3*/
struct iprp_box {
    BOX_ST;
    struct ipco_box ipco;
    struct ipma_box ipma;
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

typedef struct {
    struct ftyp_box ftyp;
    struct meta_box meta;
} HEIF;



void HEIF_init(void);


#ifdef __cplusplus
}
#endif

#endif