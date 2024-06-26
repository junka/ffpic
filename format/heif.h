#ifndef _HEIF_H_
#define _HEIF_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "basemedia.h"
#include "hevc.h"

#pragma pack(push, 1)


/* HEVC Decoder Configuration Record
 * See 14496-15 8.3.3.1 */
struct hvcC_box {
    BOX_ST;
    uint8_t configurationVersion;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t general_profile_idc:5;
    uint8_t general_tier_flag:1;
    uint8_t general_profile_space:2;
#else
    uint8_t general_profile_space:2;
    uint8_t general_tier_flag:1;
    uint8_t general_profile_idc:5;
#endif
    uint32_t general_profile_compatibility_flags;

    uint8_t general_constraint_indicator_flags[6];

    uint8_t general_level_idc;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint16_t min_spatial_segmentation_idc:12;
    uint16_t reserved:4;
#else
    uint8_t reserved:4;
    uint8_t min_spatial_segmentation_idc1:12;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t parallelismType:2;
    uint8_t rsd1:6;
#else
    uint8_t rsd1:6;
    uint8_t parallelismType:2;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t chroma_format_idc:2;
    uint8_t rsd2:6;
#else
    uint8_t rsd2:6;
    uint8_t chroma_format_idc:2;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t bit_depth_luma_minus8:3;
    uint8_t rsd3:5;
#else
    uint8_t rsd3:5;
    uint8_t bit_depth_luma_minus8:3;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t bit_depth_chroma_minus8:3;
    uint8_t rsd4:5;
#else
    uint8_t rsd4:5;
    uint8_t bit_depth_chroma_minus8:3;
#endif

    uint16_t avgframerate;
#if BYTE_ORDER == LITTLE_ENDIAN
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

    struct hevc_param_set hps;
};

// 14496-15 8.4
struct HEVCSampleEntry {
    struct VisualSampleEntry sample;
    struct hvcC_box config;
    //MPEG4ExtensionDescriptorsBox; // optional
};

#pragma pack(pop)


struct exif_block {
    uint32_t exif_tiff_header_offset;
    uint8_t *payload;
};


struct heif_item {
    const struct item_location *item; //just a ref to iloc
    uint32_t type;
    uint64_t length;
    uint8_t *data;
};

typedef struct {
    struct ftyp_box ftyp;
    struct meta_box meta;

    int moov_num;
    struct moov_box *moov; //zero or more

    // we don't have to read mdat directly, iloc will tell us
    // int mdat_num;
    // struct mdat_box *mdat; //zero or more

    int item_num;
    struct heif_item *items;
} HEIF;

void HEIF_init(void);

#ifdef __cplusplus
}
#endif

#endif
