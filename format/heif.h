#ifndef _HEIF_H_
#define _HEIF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"


/* HEVC ITU-T H.265 High efficiency video coding */

#pragma pack(push, 1)


/* HEVC configuration item see 14496-15 */
struct nalus {
    uint16_t unit_length;
    uint8_t *nal_units;  //size depends on unit_length * 8
};

//see ISO/IEC 23008-2
enum hevc_nal_unit_type {
    NALU_CODED_SLICE_TRAL_N = 0,
    NALU_CODED_SLICE_TRAL_R = 1,
    NALU_CODED_SLICE_TSA_N = 2,
    NALU_CODED_SLICE_TSA_R = 3,
    NALU_CODED_SLICE_STSA_N = 4,
    NALU_CODED_SLICE_STSA_R = 5,
    NALU_CODED_SLICE_RADL_N = 6,
    NALU_CODED_SLICE_DLP = 7,
    NALU_CODED_SLICE_RASL_N = 8,
    NALU_CODED_SLICE_TFD = 9,

    NALU_RESERVED_10,
    NALU_RESERVED_11,
    NALU_RESERVED_12,
    NALU_RESERVED_13,
    NALU_RESERVED_14,
    NALU_RESERVED_15,

    NALU_CODED_SLICE_BLA,
    NALU_CODED_SLICE_BLANT,
    NALU_CODED_SLICE_BLA_N_LP,
    NALU_CODED_SLICE_IDR,
    NALU_CODED_SLICE_IDR_N_LP,
    NALU_CODED_SLICE_CRA,
    NALU_RESERVED_22,
    NALU_RESERVED_23,
    NALU_RESERVED_24,
    NALU_RESERVED_25,
    NALU_RESERVED_26,
    NALU_RESERVED_27,
    NALU_RESERVED_28,
    NALU_RESERVED_29,
    NALU_RESERVED_30,
    NALU_RESERVED_31,

    NALU_VPS,
    NALU_SPS,
    NALU_PPS,
    NALU_ACCESS_UNIT_DELEMITER,
    NALU_EOS,
    NALU_EOB,
    NALU_FILLER_DATA,
    NALU_SEI,
    NALU_SEI_SUFFFIX,

    NALU_RESERVED_41,
    NALU_RESERVED_42,
    NALU_RESERVED_43,
    NALU_RESERVED_44,
    NALU_RESERVED_45,
    NALU_RESERVED_46,
    NALU_RESERVED_47,

    NALU_UNSPECIFIED_48,
    NALU_UNSPECIFIED_49,
    NALU_UNSPECIFIED_50,
    NALU_UNSPECIFIED_51,
    NALU_UNSPECIFIED_52,
    NALU_UNSPECIFIED_53,
    NALU_UNSPECIFIED_54,
    NALU_UNSPECIFIED_55,
    NALU_UNSPECIFIED_56,
    NALU_UNSPECIFIED_57,
    NALU_UNSPECIFIED_58,
    NALU_UNSPECIFIED_59,
    NALU_UNSPECIFIED_60,
    NALU_UNSPECIFIED_61,
    NALU_UNSPECIFIED_62,
    NALU_UNSPECIFIED_63,

    NALU_INVALID,
};

struct hevc_nalu_header {
// #ifdef LITTLE_ENDIAN
//     uint16_t nuh_temporal_id_plus1:3;
//     uint16_t nuh_layer_id:6;
//     uint16_t nal_unit_type:6;
//     uint16_t forbidden_zero_bit:1;
// #else
    uint16_t forbidden_zero_bit:1;
    uint16_t nal_unit_type:6;
    uint16_t nuh_layer_id:6;
    uint16_t nuh_temporal_id_plus1:3;
// #endif
};

struct hevc_nalu {
    struct hevc_nalu_header nalu_h;

};

/* the nal_unit_type is restricted to VPS,SPS,PPS, prefix SEI, suffix SEI */
struct nal_arr {
#ifdef LITTLE_ENDIAN
    uint8_t nal_unit_type:6;
    uint8_t reserved:1;
    uint8_t array_completeness:1;
#else
    uint8_t array_completeness:1;
    uint8_t reserved:1;
    uint8_t nal_unit_type:6;
#endif
    uint16_t numNalus;
    struct nalus * nals;
};

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

#ifdef LITTLE_ENDIAN
    uint64_t min_spatial_segmentation_idc1:4;
    uint64_t reserved:4;
    uint64_t general_level_idc:8;
    uint64_t general_constraint_indicator_flags: 48;
#else
    uint64_t general_constraint_indicator_flags: 48;
    uint64_t general_level_idc:8;
    uint64_t reserved:4;
    uint64_t min_spatial_segmentation_idc1:4;
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


struct heif_item {
    const struct item_location *item;
    uint64_t length;
    uint8_t *data;
};

typedef struct {
    struct ftyp_box ftyp;
    struct meta_box meta;
    int mdat_num;
    struct mdat_box *mdat;

    struct heif_item *items;
} HEIF;



void HEIF_init(void);


#ifdef __cplusplus
}
#endif

#endif