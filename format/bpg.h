#ifndef _BPG_H_
#define _BPG_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include "golomb.h"

#pragma pack(push, 1)

struct bpg_file {
    uint32_t file_magic;

#ifdef LITTLE_ENDIAN
    uint8_t bit_depth_minus_8 : 4;
    uint8_t alpha1_flag : 1;
    uint8_t pixel_format : 3;
#else
    uint8_t pixel_format : 3;
    uint8_t alpha1_flag : 1;
    uint8_t bit_depth_minus_8 : 4;
#endif

#ifdef LITTLE_ENDIAN
    uint8_t animation_flag : 1;
    uint8_t limited_range_flag : 1;
    uint8_t alpha2_flag : 1;
    uint8_t extension_present_flag : 1;
    uint8_t color_space : 4;
#else
    uint8_t color_space:4;
    uint8_t extension_present_flag:1;
    uint8_t alpha2_flag:1;
    uint8_t limited_range_flag:1;
    uint8_t animation_flag:1;
#endif

};

struct hevc_header {
    uint32_t hevc_header_length;

    GUE(log2_min_luma_coding_block_size_minus3);
    GUE(log2_diff_max_min_luma_coding_block_size);
    GUE(log2_min_luma_transform_block_size_minus2);
    GUE(log2_diff_max_min_luma_transform_block_size);
    GUE(max_transform_hierarchy_depth_intra);

    uint8_t sample_adaptive_offset_enabled_flag:1;
    uint8_t pcm_enabled_flag:1;
    uint8_t strong_intra_smoothing_enabled_flag:1;
    uint8_t sps_extension_present_flag:1;

};


#pragma pack(pop)


typedef struct {
    struct bpg_file head;

    uint32_t picture_width;
    uint32_t picture_height;
    uint32_t picture_data_length;

    struct hevc_header hevc;
} BPG;

void BPG_init(void);

struct pic *BPG_load(const char *filename);

#ifdef __cplusplus
}
#endif

#endif
