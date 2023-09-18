#ifndef _AVIF_H_
#define _AVIF_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "byteorder.h"
#include "basemedia.h"

#pragma pack(push, 1)

struct operating_parameters_info {
    uint32_t decoder_buffer_delay;
    uint32_t encoder_buffer_delay;
    uint8_t low_delay_mode_flag:1;
};

struct operating_points {
    uint16_t operating_point_idc:12;
    uint8_t seq_level_idx:5;
    uint8_t seq_tier:1;
    uint8_t decoder_model_present_for_this_op:1;
    struct operating_parameters_info pinfo;

    uint8_t initial_display_delay_present_for_this_op:1;
    uint8_t initial_display_delay_minus_1:4;
};

struct decoder_model_info {
    uint8_t buffer_delay_length_minus_1 :5;
    uint32_t num_units_in_decoding_tick;
    uint8_t buffer_removal_time_length_minus_1:5;
    uint8_t frame_presentation_time_length_minus_1:5;
};

/*color_primaries is an integer that is defined by the
 “Color primaries” section of ISO/IEC 23091-4/ITU-T H.273.*/
enum {
    CP_BT_709 = 1,
    CP_UNSPECIFIED = 2,
    CP_BT_470_M = 4,
    CP_BT_470_B_G,
    CP_BT_601,
    CP_SMPTE_240,
    CP_GENERIC_FILM,
    CP_BT_2020,
    CP_XYZ,
    CP_SMPTE_431,
    CP_SMPTE_432,
    CP_EBU_3213 = 22,
};

/* transfer_characteristics is an integer that is defined by the 
“Transfer characteristics” section of ISO/IEC 23091-4/ITU-T H.273.*/
enum {
    TC_RESERVED_0 = 0,
    TC_BT_709 = 1,
    TC_UNSPECIFIED = 2,
    TC_RESERVED_3 = 3,
    TC_BT_470_M = 4,
    TC_BT_470_B_G = 5,
    TC_BT_601,
    TC_SMPTE_240,
    TC_LINEAR,
    TC_LOG_100,
    TC_LOG_100_SQRT10,
    TC_IEC_61966,
    TC_BT_1361,
    TC_SRGB,
    TC_BT_2020_10_BIT,
    TC_BT_2020_12_BIT,
    TC_SMPTE_2084,
    TC_SMPTE_428,
    TC_HLG,
};

/*matrix_coefficients is an integer that is defined by
 the “Matrix coefficients” section of ISO/IEC 23091-4/ITU-T H.273.*/
enum {
    MC_IDENTITY = 0,
    MC_BT_709,
    MC_UNSPECIFIED,
    MC_RESERVED_3,
    MC_FCC,
    MC_BT_470_B_G,
    MC_BT_601,
    MC_SMPTE_240,
    MC_SMPTE_YCGCO,
    MC_BT_2020_NCL,
    MC_BT_2020_CL,
    MC_SMPTE_2085,
    MC_CHROMAT_NCL,
    MC_CHROMAT_CL,
    MC_ICTCP
};

/* chroma_sample_position specifies the sample position for subsampled streams: */
enum {
    CSP_UNKNOWN = 0,
    CSP_VERTICAL,
    CSP_COLOCATED,
    CSP_RESERVED,
};

struct color_config {
    uint8_t high_bitdepth:1;
    uint8_t twelve_bit:1;
    uint8_t mono_chrome:1;
    uint8_t color_description_present_flag:1;
    uint8_t color_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t color_range:1;
    uint8_t subsampling_x:1;
    uint8_t subsampling_y:1;
    uint8_t chroma_sample_position:2;
    uint8_t separate_uv_delta_q:1;
};

/* see av1-spec 5.5.1 */
struct sequence_header_obu {
    uint8_t seq_profile:3;
    uint8_t still_picture:1;
    uint8_t reduced_still_picture_header:1;

    uint8_t seq_level_idx_0 : 5;
    uint8_t timing_info_present_flag:1;
    uint8_t decoder_model_info_present_flag:1;
    struct decoder_model_info minfo;

    uint8_t initial_display_delay_present_flag:1;
    uint8_t operating_points_cnt_minus_1: 5;
    struct operating_points * points;

    uint8_t frame_width_bits_minus_1: 4;
    uint8_t frame_height_bits_minus_1: 4;

    uint16_t max_frame_width_minus_1;
    uint16_t max_frame_height_minus_1;

    uint8_t frame_id_numbers_present_flag:1;
    uint8_t delta_frame_id_length_minus_2:4;
    uint8_t additional_frame_id_length_minus_1:3;

    uint8_t use_128x128_superblock:1;
    uint8_t enable_filter_intra:1;
    uint8_t enable_intra_edge_filter:1;
    uint8_t enable_interintra_compound:1;
    uint8_t enable_masked_compound :1;
    uint8_t enable_warped_motion:1;
    uint8_t enable_dual_filter:1;
    uint8_t enable_order_hint:1;

    uint8_t enable_jnt_comp:1;
    uint8_t enable_ref_frame_mvs:1;
    uint8_t seq_choose_screen_content_tools:1;
    uint8_t seq_force_screen_content_tools:2; // read 1 bit but could be 2 in other branch
    uint8_t seq_choose_integer_mv:1;

    uint8_t seq_force_integer_mv:2; // read 1 bit but could be 2 in other branch

    uint8_t order_hint_bits_minus_1:3;
    uint8_t enable_superres:1;
    uint8_t enable_cdef:1;
    uint8_t enable_restoration:1;
    uint8_t film_grain_params_present:1;

    struct color_config cc;

    int OrderHintBits;
    int NumPlanes;
    int BitDepth;

};

#define SELECT_SCREEN_CONTENT_TOOLS 2
#define SELECT_INTEGER_MV 2
#define SELECT_SCREEN_CONTENT_TOOLS 2


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

    struct sequence_header_obu *configOBUs;

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