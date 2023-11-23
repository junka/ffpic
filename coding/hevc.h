#ifndef _HEVC_H_
#define _HEVC_H_

#ifdef __cplusplus
extern "C"{
#endif

#include "byteorder.h"
#include "golomb.h"
#include "basemedia.h"

/* HEVC ITU-T H.265 High efficiency video coding */

enum pred_mode {
    MODE_INTER = 0,
    MODE_INTRA = 1,
    MODE_SKIP = 2,
};

/* see table I.2 */
enum inter_pred_mode {
    INTRA_PLANAR = 0,
    INTRA_DC = 1,
    INTRA_ANGULAR2 = 2,
    INTRA_ANGULAR3 = 3,
    INTRA_ANGULAR4 = 4,
    INTRA_ANGULAR5 = 5,
    INTRA_ANGULAR6 = 6,
    INTRA_ANGULAR7 = 7,
    INTRA_ANGULAR8 = 8,
    INTRA_ANGULAR9 = 9,
    INTRA_ANGULAR10 = 10,
    INTRA_ANGULAR11 = 11,
    INTRA_ANGULAR12 = 12,
    INTRA_ANGULAR13 = 13,
    INTRA_ANGULAR14 = 14,
    INTRA_ANGULAR15 = 15,
    INTRA_ANGULAR16 = 16,
    INTRA_ANGULAR17 = 17,
    INTRA_ANGULAR18 = 18,
    INTRA_ANGULAR19 = 19,
    INTRA_ANGULAR20 = 20,
    INTRA_ANGULAR21 = 21,
    INTRA_ANGULAR22 = 22,
    INTRA_ANGULAR23 = 23,
    INTRA_ANGULAR24 = 24,
    INTRA_ANGULAR25 = 25,
    INTRA_ANGULAR26 = 26,
    INTRA_ANGULAR27 = 27,
    INTRA_ANGULAR28 = 28,
    INTRA_ANGULAR29 = 29,
    INTRA_ANGULAR30 = 30,
    INTRA_ANGULAR31 = 31,
    INTRA_ANGULAR32 = 32,
    INTRA_ANGULAR33 = 33,
    INTRA_ANGULAR34 = 34,
    INTRA_WEDGE = 35,
    INTRA_CONTOUR = 36,
    INTRA_SINGLE = 37,
};


#pragma pack(push, 1)

/* HEVC configuration item see 14496-15 */
struct nalus {
    uint16_t unit_length;
    uint8_t *nal_units;  //size depends on unit_length * 8
};

//see ISO/IEC 23008-2
// see Table 7.1 – NAL unit type codes
enum hevc_nal_unit_type {
    TRAIL_N = 0,
    TRAIL_R = 1,
    TSA_N = 2,
    TSA_R = 3,
    STSA_N = 4,
    STSA_R = 5,
    RADL_N = 6,
    RADL_R = 7,
    RASL_N = 8,
    RASL_R = 9,

    RSV_VCL_N10 = 10,
    RSV_VCL_R11,
    RSV_VCL_N12 = 12,
    RSV_VCL_R13,
    RSV_VCL_N14 = 14,
    RSV_VCL_R15,

    BLA_W_LP = 16,
    BLA_W_RADL = 17,
    BLA_N_LP = 18,
    IDR_W_RADL = 19,
    IDR_N_LP = 20,

    CRA_NUT = 21,
    RSV_IRAP_VCL22,
    RSV_IRAP_VCL23,
    RSV_VCL24,
    RSV_VCL25,
    RSV_VCL26,
    RSV_VCL27,
    RSV_VCL28,
    RSV_VCL29,
    RSV_VCL30,
    RSV_VCL31,

    VPS_NUT = 32,
    SPS_NUT = 33,
    PPS_NUT = 34,
    AUD_NUT = 35,
    EOS_NUT = 36,
    EOB_NUT = 37,
    FD_NUT = 38,
    PREFIX_SEI_NUT = 39,
    SUFFIX_SEI_NUT = 40,

    RSV_NVCL41,
    RSV_NVCL42,
    RSV_NVCL43,
    RSV_NVCL44,
    RSV_NVCL45,
    RSV_NVCL46,
    RSV_NVCL47,

    UNSPEC48,
    UNSPEC49,
    UNSPEC50,
    UNSPEC51,
    UNSPEC52,
    UNSPEC53,
    UNSPEC54,
    UNSPEC55,
    UNSPEC56,
    UNSPEC57,
    UNSPEC58,
    UNSPEC59,
    UNSPEC60,
    UNSPEC61,
    UNSPEC62,
    UNSPEC63,

    MAX_NALU,
};

enum slice_type {
    SLICE_TYPE_B = 0,
    SLICE_TYPE_P = 1,
    SLICE_TYPE_I = 2,
};

struct hevc_nalu_header {
    uint16_t forbidden_zero_bit:1;
    uint16_t nal_unit_type:6;
    uint16_t nuh_layer_id:6;
    uint16_t nuh_temporal_id:3;
};

struct hevc_nalu {
    struct hevc_nalu_header nalu_h;

};


struct sub_layer {
    uint8_t sub_layer_profile_space:2;
    uint8_t sub_layer_tier_flag:1;
    uint8_t sub_layer_profile_idc: 5;

    uint32_t sub_layer_profile_compatibility_flag;

    uint8_t sub_layer_progressive_source_flag:1;
    uint8_t sub_layer_interlaced_source_flag:1;
    uint8_t sub_layer_non_packed_constraint_flag:1;
    uint8_t sub_layer_frame_only_constraint_flag:1;

    uint8_t sub_layer_max_12bit_constraint_flag:1;
    uint8_t sub_layer_max_10bit_constraint_flag:1;
    uint8_t sub_layer_max_8bit_constraint_flag:1;
    uint8_t sub_layer_max_422chroma_constraint_flag:1;
    uint8_t sub_layer_max_420chroma_constraint_flag:1;
    uint8_t sub_layer_max_monochrome_constraint_flag:1;
    uint8_t sub_layer_intra_constraint_flag:1;
    uint8_t sub_layer_one_picture_only_constraint_flag:1;
    uint8_t sub_layer_lower_bit_rate_constraint_flag:1;

    uint8_t sub_layer_max_14bit_constraint_flag:1;

    uint8_t sub_layer_inbld_flag:1;

    uint8_t sub_layer_level_idc;

};

//see 7.3.3 profile, tier and level syntax
struct profile_tier_level {
    uint8_t general_profile_space:2;
    uint8_t general_tier_flag :1;
    uint8_t general_profile_idc: 5;

    uint32_t general_profile_compatibility_flag;
    
    uint8_t general_progressive_source_flag :1;
    uint8_t general_interlaced_source_flag:1;
    uint8_t general_non_packed_constraint_flag:1;
    uint8_t general_frame_only_constraint_flag:1;

    uint8_t general_max_12bit_constraint_flag:1;
    uint8_t general_max_10bit_constraint_flag:1;
    uint8_t general_max_8bit_constraint_flag:1;
    uint8_t general_max_422chroma_constraint_flag:1;
    uint8_t general_max_420chroma_constraint_flag:1;
    uint8_t general_max_monochrome_constraint_flag:1;
    uint8_t general_intra_constraint_flag:1;
    uint8_t general_one_picture_only_constraint_flag:1;
    uint8_t general_lower_bit_rate_constraint_flag:1;
    
    uint8_t general_max_14bit_constraint_flag:1;

    uint8_t general_inbld_flag:1;

    uint8_t general_level_idc;

    struct sub_layer_flag {
        uint8_t sub_layer_profile_present_flag:1;
        uint8_t sub_layer_level_present_flag:1;
    } sub_layer_flag[8];

    uint8_t maxNumSubLayersMinus1;
    struct sub_layer *sublayers;
};

//see 7.3.4
struct scaling_list_data {
    uint8_t scaling_list_pred_mode_flag[4][6];
    GUE(scaling_list_pred_matrix_id_delta)[4][6];
    GSE(scaling_list_dc_coef_minus8)[2][6];

    uint8_t coefNum;
    GSE(scaling_list_delta_coef)[64];

    uint8_t scalinglist[4][6][64];
};

struct cpb {
    GUE(bit_rate_value_minus1);
    GUE(cpb_size_value_minus1);
    GUE(cpb_size_du_value_minus1);
    GUE(bit_rate_du_value_minus1);
    uint8_t cbr_flag:1;
};
struct sub_layer_hrd_parameters {
    int CpbCnt;
    struct cpb* cpb;
};

struct hrd_parameters {
    uint8_t nal_hrd_parameters_present_flag:1;
    uint8_t vcl_hrd_parameters_present_flag:1;
    uint8_t sub_pic_hrd_params_present_flag:1;

    uint8_t tick_divisor_minus2;
    uint8_t du_cpb_removal_delay_increment_length_minus1:5;
    uint8_t sub_pic_cpb_params_in_pic_timing_sei_flag:1;
    uint8_t dpb_output_delay_du_length_minus1:5;

    uint8_t bit_rate_scale:4;
    uint8_t cpb_size_scale:4;

    uint8_t cpb_size_du_scale:4;
    uint8_t initial_cpb_removal_delay_length_minus1:5;
    uint8_t au_cpb_removal_delay_length_minus1:5;
    uint8_t dpb_output_delay_length_minus1:5;

    struct hrd_sublayer {
        uint8_t fixed_pic_rate_general_flag:1;
        uint8_t fixed_pic_rate_within_cvs_flag:1;
        GUE(elemental_duration_in_tc_minus1);
        uint8_t low_delay_hrd_flag:1;
        GUE(cpb_cnt_minus1);
        struct sub_layer_hrd_parameters *nal_hrd;
        struct sub_layer_hrd_parameters *vcl_hrd;
    } hrd_layer[8];

};


struct vps_timing_info {
    uint32_t vps_num_units_in_tick;
    uint32_t vps_time_scale;
    // uint8_t vps_poc_proportional_to_timing_flag:1;
    GUE(vps_num_ticks_poc_diff_one_minus1);
    GUE(vps_num_hrd_parameters);

    GUE(hrd_layer_set_idx)[1024];
    uint8_t cprms_present_flag[1024];

    struct hrd_parameters *vps_hrd_para[1024];
};

struct video_signal_info {
    uint8_t video_vps_format:3;
    uint8_t video_full_range_vps_flag:1;
    uint8_t colour_primaries_vps;
    uint8_t transfer_characteristics_vps;
    uint8_t matrix_coeffs_vps;
};

struct vps_rep_format {
    uint16_t pic_width_vps_in_luma_samples;
    uint16_t pic_height_vps_in_luma_samples;
    uint8_t chroma_and_bit_depth_vps_present_flag:1;
    uint8_t chroma_format_vps_idc:2;
    uint8_t separate_colour_plane_vps_flag:1;
    uint8_t bit_depth_vps_luma_minus8: 4;
    uint8_t bit_depth_vps_chroma_minus8: 4;

    uint8_t conformance_window_vps_flag:1;

    GUE(conf_win_vps_left_offset);
    GUE(conf_win_vps_right_offset);
    GUE(conf_win_vps_top_offset);
    GUE(conf_win_vps_bottom_offset);
};


struct dpb_size {
    // uint8_t sub_layer_flag_info_present_flag[];
    //sub_layer_dpb_info_present_flag[][]
    
    //the k-th layer in the i-th OLS for the CVS, that need
    // to be stored in the DPB when HighestTid is equal to j
    // GUE(max_vps_dec_pic_buffering_minus1)[i][k][j];

    GUE(max_vps_num_reorder_pics)[64][2];
    GUE(max_vps_latency_increase_plus1)[64][2];
};


struct vps_vui_bsp_hrd_params
{
    GUE(vps_num_add_hrd_params);
    struct hrd_parameters * vps_vui_bsp_hrd;
    // cprms_add_present_flag[1024];
    uint32_t **num_partitions_in_scheme_minus1;
    
    // bsp_hrd_idx[][][][][];
    // bsp_sched_idx[][][][][];
};

struct vps_vui {
    //cross_layer_pic_type_aligned_flag:1
    //cross_layer_irap_aligned_flag:1
    uint8_t all_layers_idr_aligned_flag:1;
    uint8_t bit_rate_present_vps_flag:1;
    uint8_t pic_rate_present_vps_flag:1;

    // uint8_t bit_rate_present_flag[][];
    // uint8_t pic_rate_present_flag[][];

    uint16_t** avg_bit_rate;
    uint16_t** max_bit_rate;

    uint8_t** constant_pic_rate_idc;
    uint16_t** avg_pic_rate;

    // uint8_t video_signal_info_idx_present_flag:1;
    uint8_t vps_num_video_signal_info_minus1:4;

    struct video_signal_info *vps_video_signal_info;

    uint8_t* vps_video_signal_info_idx;

    uint8_t* loop_filter_not_across_tiles_flag;

    uint8_t** tile_boundaries_aligned_flag;

    uint8_t* wpp_in_use_flag;

    uint8_t single_layer_for_non_irap_flag:1;
    uint8_t higher_layer_irap_skip_flag:1;
    uint8_t ilp_restricted_ref_layers_flag:1;

    // GUE(min_spatial_segment_offset_plus1)[][];
    // GUE(min_horizontal_ctu_offset_plus1)[][];
    uint32_t **min_horizontal_ctu_offset_plus1;

    struct vps_vui_bsp_hrd_params *vui_hrd;

    uint8_t* base_layer_parameter_set_compatibility_flag;
};


//see F.7.3.2.1.1
struct vps_extension {
    struct profile_tier_level *vps_ext_profile_tier_level;
    uint8_t splitting_flag:1;
    uint16_t scalability_mask_flag;
    uint8_t* dimension_id_len_minus1;
    // uint8_t vps_nuh_layer_id_present_flag:1;

    uint8_t* layer_id_in_nuh;
    uint8_t** dimension_id;

    uint8_t view_id_len:4;
    uint16_t view_id_val[64];
    uint64_t direct_dependency_flag[64];

    int NumViews;

    GUE(num_add_layer_sets);

    uint16_t** highest_layer_idx_plus1;
    uint8_t* sub_layers_vps_max_minus1;
    uint8_t max_tid_il_ref_pics_plus1[64][64];
    uint8_t default_ref_layers_active_flag;
    GUE(vps_num_profile_tier_level_minus1);
    // uint8_t vps_profile_present_flag[64];
    GUE(num_add_olss);
    uint8_t default_output_layer_idc:2;

    uint16_t *layer_set_idx_for_ols_minus1;

    uint8_t **output_layer_flag;
    uint16_t **profile_tier_level_idx;//variable bits
    
    uint8_t *alt_output_layer_flag;

    GUE(vps_num_rep_formats_minus1);

    struct vps_rep_format *vps_rep_format;

    uint16_t *vps_rep_format_idx;//variable bits

    uint8_t max_one_active_ref_layer_flag:1;
    uint8_t vps_poc_lsb_aligned_flag:1;

    uint8_t *poc_lsb_not_present_flag;

    struct dpb_size *dpb_size;
    
    GUE(direct_dep_type_len_minus2);

    uint32_t direct_dependency_all_layers_type; // variable bits
    uint32_t** direct_dependency_type; // variable bits

    // GUE(vps_non_vui_extension_length);
    uint8_t *vps_non_vui_extension_data_byte;

    struct vps_vui * vps_vui;
    
};

//see I.7.3.2.1.7
struct vps_3d_extension {
    GUE(cp_precision);
    struct {
        uint8_t num_cp:6;
        uint8_t cp_in_slice_segment_header_flag:1;
        GUE(cp_ref_voi)[64];
        GSE(vps_cp_scale)[64];
        GSE(vps_cp_off)[64];
        GSE(vps_cp_inv_scale_plus_scale)[64];
        GSE(vps_cp_inv_off_plus_off)[64];
    } cp[64];
};


//see 7.3.2.1 video parameter set RBSP syntax
struct vps {
    uint16_t vps_video_parameter_set_id:4;
    uint16_t vps_base_layer_internal_flag:1;
    uint16_t vps_base_layer_available_flag:1;
    uint16_t vps_max_layers_minus1:6;
    uint16_t vps_max_sub_layers_minus1:3;
    uint16_t vps_temporal_id_nesting_flag:1;

    // uint16_t vps_reserved_0xffff_16bits;

    struct profile_tier_level vps_profile_tier_level;
    uint8_t vps_sub_layer_ordering_info_present_flag:1;
    struct vps_sublayer {
        GUE(vps_max_dec_pic_buffering_minus1);
        GUE(vps_max_num_reorder_pics);
        GUE(vps_max_latency_increase_plus1);
    } vps_sublayers[8];

    uint8_t vps_max_layer_id:6; //less than 63
    GUE(vps_num_layer_sets_minus1); //less than 1023

    uint64_t layer_id_included_flag[1024];

    // uint8_t vps_timing_info_present_flag:1;
    struct vps_timing_info * vps_timing_info;

    // uint8_t vps_extension_flag:1;
    struct vps_extension *vps_ext;

    struct vps_3d_extension *vps_3d_ext;

    int ViewOIdxList[64];
    int CpPresentFlag[64][64];

    int LayerSetLayerIdList[64][64];
    int NumLayersInIdList[64];

    int NumDirectRefLayers[64];
    int LayerIdxInVps[64];
    int IdRefListLayer[64][64];
    int NumRefListLayers[64];

    uint8_t DepthLayerFlag[64];
    uint8_t ViewOrderIdx[64];
};


struct pps_range_extension {
    GUE(log2_max_transform_skip_block_size_minus2);
    uint8_t cross_component_prediction_enabled_flag:1;
    uint8_t chroma_qp_offset_list_enabled_flag:1;
    GUE(diff_cu_chroma_qp_offset_depth);
    GUE(chroma_qp_offset_list_len_minus1); // range of 0 - 5

    GSE(cb_qp_offset_list)[6];
    GSE(cr_qp_offset_list)[6];
    GUE(log2_sao_offset_scale_luma);
    GUE(log2_sao_offset_scale_chroma);
};

//see 7.3.2.3.5
struct color_mapping_table {
    GUE(num_cm_ref_layers_minus1); // 0 - 61
    uint8_t cm_ref_layer_id[64];
    uint8_t cm_octant_depth:2;
    uint8_t cm_y_part_num_log2:2;
    GUE(luma_bit_depth_cm_input_minus8);
    GUE(chroma_bit_depth_cm_input_minus8);
    GUE(luma_bit_depth_cm_output_minus8);
    GUE(chroma_bit_depth_cm_output_minus8);
    uint8_t cm_res_quant_bits:2;
    uint8_t cm_delta_flc_bits_minus1:2;
    GSE(cm_adapt_threshold_u_delta);
    GSE(cm_adapt_threshold_v_delta);
};

struct pps_multilayer_extension {
    uint8_t poc_reset_info_present_flag:1;
    uint8_t pps_infer_scaling_list_flag:1;
    uint8_t pps_scaling_list_ref_layer_id:6;

    GUE(num_ref_loc_offsets);

    struct ref_layer {

        uint8_t ref_loc_offset_layer_id:6;
        uint8_t scaled_ref_layer_offset_present_flag:1;

        GSE(scaled_ref_layer_left_offset);
        GSE(scaled_ref_layer_top_offset);
        GSE(scaled_ref_layer_right_offset);
        GSE(scaled_ref_layer_bottom_offset);

        uint8_t ref_region_offset_present_flag:1;

        GSE(ref_region_left_offset);
        GSE(ref_region_top_offset);
        GSE(ref_region_right_offset);
        GSE(ref_region_bottom_offset);

        uint8_t resample_phase_set_present_flag:1;
        GUE(phase_hor_luma);
        GUE(phase_ver_luma);
        GUE(phase_hor_chroma_plus8);
        GUE(phase_ver_chroma_plus8);
    } reflayer[64];
    uint8_t colour_mapping_enabled_flag:1;
    struct color_mapping_table *color_map;
};

struct pps_3d_extension {
    uint8_t dlts_present_flag:1;
    uint8_t pps_depth_layers_minus1:6;
    uint8_t pps_bit_depth_for_depth_layers_minus8:4; //bit_depth_luma_minus8 : 0 ~ 8
    struct pps_3d_layer {
        uint8_t dlt_flag:1;
        uint8_t dlt_pred_flag:1;
        uint8_t dlt_val_flags_present_flag:1;
        uint8_t dlt_value_flag[65535];// depthMaxValue equal to ( 1 << ( pps_bit_depth_for_depth_layers_minus8 + 8 ) ) − 1.

        uint16_t num_val_delta_dlt; //pps_bit_depth_for_depth_layers_minus8 + 8 bits
        uint16_t max_diff;          //pps_bit_depth_for_depth_layers_minus8 + 8 bits
        uint32_t min_diff_minus1;    //ceil (log2(max_diff + 1)) bits
        uint16_t delta_dlt_val0;     //pps_bit_depth_for_depth_layers_minus8 + 8 bits
        uint32_t delta_val_diff_minus_min[65536];  //ceil (log2(max_diff - min_diff_minus1 )) bits
    } pps_3d_layers[64];
};

struct pps_scc_extension {
    uint8_t pps_curr_pic_ref_enabled_flag:1;
    uint8_t residual_adaptive_colour_transform_enabled_flag:1;

    uint8_t pps_slice_act_qp_offsets_present_flag:1;
    GSE(pps_act_y_qp_offset_plus5);
    GSE(pps_act_cb_qp_offset_plus5);
    GSE(pps_act_cr_qp_offset_plus3);

    uint8_t pps_palette_predictor_initializers_present_flag:1;
    GUE(pps_num_palette_predictor_initializers);    //less than PaletteMaxPredictorSize which less than 128
    uint8_t monochrome_palette_flag:1;
    GUE(luma_bit_depth_entry_minus8);
    GUE(chroma_bit_depth_entry_minus8);
    uint16_t pps_palette_predictor_initializer[3][128]; /* [0][i] luma_bit_depth_entry_minus8 + 8 bits
                                                            [1][i] and [2][i] chroma_bit_depth_entry_minus8 +8 bits */
};

// see 7.3.2.3 picture parameter set
struct pps {
    GUE(pps_pic_parameter_set_id);
    GUE(pps_seq_parameter_set_id);
    uint8_t dependent_slice_segments_enabled_flag:1;
    uint8_t output_flag_present_flag:1;
    uint8_t num_extra_slice_header_bits:3;
    uint8_t sign_data_hiding_enabled_flag:1;
    uint8_t cabac_init_present_flag:1;
    GUE(num_ref_idx_l0_default_active_minus1);
    GUE(num_ref_idx_l1_default_active_minus1);
    GSE(init_qp_minus26);
    uint8_t constrained_intra_pred_flag:1;
    uint8_t transform_skip_enabled_flag:1;
    uint8_t cu_qp_delta_enabled_flag:1;
    GUE(diff_cu_qp_delta_depth);
    GSE(pps_cb_qp_offset);
    GSE(pps_cr_qp_offset);
    uint8_t pps_slice_chroma_qp_offsets_present_flag:1;
    uint8_t weighted_pred_flag:1;
    uint8_t weighted_bipred_flag:1;
    uint8_t transquant_bypass_enabled_flag:1;
    uint8_t tiles_enabled_flag:1;
    uint8_t entropy_coding_sync_enabled_flag:1;
    GUE(num_tile_columns_minus1);
    GUE(num_tile_rows_minus1);
    uint8_t uniform_spacing_flag:1;
    
    uint32_t *column_width_minus1;
    uint32_t *row_height_minus1;
    uint8_t loop_filter_across_tiles_enabled_flag:1;

    uint8_t pps_loop_filter_across_slices_enabled_flag:1;
    uint8_t deblocking_filter_control_present_flag:1;
    uint8_t deblocking_filter_override_enabled_flag:1;
    uint8_t pps_deblocking_filter_disabled_flag:1;
    GSE(pps_beta_offset_div2);
    GSE(pps_tc_offset_div2);

    uint8_t pps_scaling_list_data_present_flag:1;
    struct scaling_list_data list_data;

    uint8_t lists_modification_present_flag:1;
    GUE(log2_parallel_merge_level_minus2);
    uint8_t slice_segment_header_extension_present_flag:1;
    uint8_t pps_extension_present_flag:1;
    uint8_t pps_range_extension_flag:1;
    uint8_t pps_multilayer_extension_flag:1;
    uint8_t pps_3d_extension_flag:1;
    uint8_t pps_scc_extension_flag:1;
    uint8_t pps_extension_4bits:4;
    // pps range extension, see 7.3.2.3.2
    struct pps_range_extension pps_range_ext;
    // pps multilayer extension
    struct pps_multilayer_extension *pps_multilayer_ext;
    // pps 3d extension 
    struct pps_3d_extension *pps_3d_ext;
    // pps scc extension
    struct pps_scc_extension pps_scc_ext;
    // pps extension 4bits
    // uint8_t pps_extension_data_flag:1;

    int * CtbAddrTsToRs;
    int *CtbAddrRsToTs;
    int *TileId;
    int ** MinTbAddrZs;
};


#define EXTENDED_SAR 255
//see annex E
struct vui_parameters {
    uint8_t aspect_ratio_info_present_flag:1;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;

    uint8_t overscan_info_present_flag:1;
    uint8_t overscan_appropriate_flag:1;
    uint8_t video_signal_type_present_flag:1;
    uint8_t video_format:3;
    uint8_t video_full_range_flag:1;
    uint8_t colour_description_present_flag:1;

    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coeffs;

    uint8_t chroma_loc_info_present_flag:1;
    GUE(chroma_sample_loc_type_top_field);
    GUE(chroma_sample_loc_type_bottom_field);

    uint8_t neutral_chroma_indication_flag:1;
    uint8_t field_seq_flag:1;
    uint8_t frame_field_info_present_flag:1;
    uint8_t default_display_window_flag:1;

    GUE(def_disp_win_left_offset);
    GUE(def_disp_win_right_offset);
    GUE(def_disp_win_top_offset);
    GUE(def_disp_win_bottom_offset);

    uint8_t vui_timing_info_present_flag:1;
    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;
    uint8_t vui_poc_proportional_to_timing_flag:1;
    GUE(vui_num_ticks_poc_diff_one_minus1);

    uint8_t vui_hrd_parameters_present_flag:1;

    struct hrd_parameters *vui_hrd_para;

    uint8_t bitstream_restriction_flag:1;
    uint8_t tiles_fixed_structure_flag:1;
    uint8_t motion_vectors_over_pic_boundaries_flag:1;
    uint8_t restricted_ref_pic_lists_flag:1;
    GUE(min_spatial_segmentation_idc);
    GUE(max_bytes_per_pic_denom);
    GUE(max_bits_per_min_cu_denom);
    GUE(log2_max_mv_length_horizontal);
    GUE(log2_max_mv_length_vertical);

};

/* short term reference picture set syntax */
struct st_ref_pic_set {
    uint8_t inter_ref_pic_set_prediction_flag:1;
    GUE(delta_idx_minus1);

    uint8_t delta_rps_sign:1;
    GUE(abs_delta_rps_minus1);
    struct {
        uint8_t used_by_curr_pic_flag:1;
        uint8_t use_delta_flag:1;
    } ref_used[16];

    GUE(num_negative_pics);
    GUE(num_positive_pics);
    GUE(delta_poc_s0_minus1)[16];
    uint8_t used_by_curr_pic_s0_flag[16];

    GUE(delta_poc_s1_minus1)[16];
    uint8_t used_by_curr_pic_s1_flag[16];
};

/* long term reference picture set syntax */
struct lt_ref_pic_set {
    uint8_t* lt_ref_pic_poc_lsb_sps; // variable
    uint8_t* used_by_curr_pic_lt_sps_flag;
};

// sps range extension, see 7.3.2.2.2
struct sps_range_extension {
    uint8_t transform_skip_rotation_enabled_flag:1;
    uint8_t transform_skip_context_enabled_flag:1;
    uint8_t implicit_rdpcm_enabled_flag:1;
    uint8_t explicit_rdpcm_enabled_flag:1;
    uint8_t extended_precision_processing_flag:1;
    uint8_t intra_smoothing_disabled_flag:1;
    uint8_t high_precision_offsets_enabled_flag:1;
    uint8_t persistent_rice_adaptation_enabled_flag:1;
    uint8_t cabac_bypass_alignment_enabled_flag:1;
};

struct sps_3d_extension {
    uint8_t iv_di_mc_enabled_flag:1;
    uint8_t iv_mv_scal_enabled_flag:1;
    GUE(log2_ivmc_sub_pb_size_minus3);
    uint8_t iv_res_pred_enabled_flag:1;
    uint8_t depth_ref_enabled_flag:1;
    uint8_t vsp_mc_enabled_flag:1;
    uint8_t dbbp_enabled_flag:1;

    // uint8_t iv_di_mc_enabled_flag1:1;
    // uint8_t iv_mv_scal_enabled_flag1:1;
    uint8_t tex_mc_enabled_flag:1;
    GUE(log2_texmc_sub_pb_size_minus3);
    uint8_t intra_contour_enabled_flag:1;
    uint8_t intra_dc_only_wedge_enabled_flag:1;
    uint8_t cqt_cu_part_pred_enabled_flag:1;
    uint8_t inter_dc_only_enabled_flag:1;
    uint8_t skip_intra_enabled_flag:1;
};

// sps scc extension , see 7.3.2.2.3
struct sps_scc_extension {
    uint8_t sps_curr_pic_ref_enabled_flag:1;
    uint8_t palette_mode_enabled_flag:1;
    GUE(palette_max_size);
    GUE(delta_palette_max_predictor_size);
    uint8_t sps_palette_predictor_initializers_present_flag:1;
    GUE(sps_num_palette_predictor_initializers_minus1);
    uint8_t sps_palette_predictor_initializer[3][128];

    uint8_t motion_vector_resolution_control_idc:2;
    uint8_t intra_boundary_filtering_disabled_flag:1;
};

enum chroma_format {
  CHROMA_400        = 0,
  CHROMA_420        = 1,
  CHROMA_422        = 2,
  CHROMA_444        = 3
};

struct pcm {
    uint8_t pcm_sample_bit_depth_luma_minus1:4;
    uint8_t pcm_sample_bit_depth_chroma_minus1:4;

    GUE(log2_min_pcm_luma_coding_block_size_minus3);
    GUE(log2_diff_max_min_pcm_luma_coding_block_size);
    
    uint8_t pcm_loop_filter_disabled_flag;
};

#define SPS_EXT_FLAG_NUM (8)

//see 7.3.2.2 sequence parameter set RBSP syntax
struct sps {
    uint8_t sps_video_parameter_set_id:4;
    uint8_t sps_ext_or_max_sub_layers_minus1:3;
    uint8_t sps_temporal_id_nesting_flag:1;

    uint8_t sps_max_sub_layer_minus1; //less than 7

    struct profile_tier_level sps_profile_tier_level;

    GUE(sps_seq_parameter_set_id);
    // uint8_t update_rep_format_flag:1;
    uint8_t sps_rep_format_idx;

    GUE(chroma_format_idc);
    
    uint8_t separate_colour_plane_flag:1;

    GUE(pic_width_in_luma_samples);
    GUE(pic_height_in_luma_samples);

    //conformance windown info
    // uint8_t conformance_window_flag:1;
    GUE(conf_win_left_offset);
    GUE(conf_win_right_offset);
    GUE(conf_win_top_offset);
    GUE(conf_win_bottom_offset);

    GUE(bit_depth_luma_minus8);
    GUE(bit_depth_chroma_minus8);
    GUE(log2_max_pic_order_cnt_lsb_minus4);

    // uint8_t sps_sub_layer_ordering_info_present_flag:1;
    struct sps_sublayer {
        GUE(sps_max_dec_pic_buffering_minus1);
        GUE(sps_max_num_reorder_pics);
        GUE(sps_max_latency_increase_plus1);
    } sps_sublayers[8];

    GUE(log2_min_luma_coding_block_size_minus3);
    GUE(log2_diff_max_min_luma_coding_block_size);
    GUE(log2_min_luma_transform_block_size_minus2);
    GUE(log2_diff_max_min_luma_transform_block_size);
    GUE(max_transform_hierarchy_depth_inter);
    GUE(max_transform_hierarchy_depth_intra);
    
    uint8_t scaling_list_enabled_flag:1;
    uint8_t sps_scaling_list_ref_layer_id:6;
    uint8_t sps_scaling_list_data_present_flag:1;

    struct scaling_list_data *list_data;

    uint8_t amp_enabled_flag:1;
    uint8_t sample_adaptive_offset_enabled_flag:1;

    uint8_t pcm_enabled_flag:1; //use pcm
    struct pcm *pcm;

    GUE(num_short_term_ref_pic_sets); //less than 64
    struct st_ref_pic_set *sps_st_ref;

    uint8_t long_term_ref_pics_present_flag: 1;
    GUE(num_long_term_ref_pics_sps);
    struct lt_ref_pic_set *sps_lt_ref;

    uint8_t sps_temporal_mvp_enabled_flag:1;
    uint8_t strong_intra_smoothing_enabled_flag:1;

    uint8_t vui_parameters_present_flag:1;
    struct vui_parameters *vui;

    uint8_t sps_extension_present_flag:1;
    // SPS_EXT_FLAG_NUM
    uint8_t sps_range_extension_flag:1;
    uint8_t sps_multilayer_extension_flag:1;
    uint8_t sps_3d_extension_flag:1;
    uint8_t sps_scc_extension_flag:1;
    uint8_t sps_extension_4bits:4;

    // sps range extension, see 7.3.2.2.2
    struct sps_range_extension sps_range_ext;
    // sps multiplayer extension
    struct sps_multilayer_extension {
        uint8_t inter_view_mv_vert_constraint_flag:1;
    } sps_multilayer_ext;

    // sps 3d extension 
    struct sps_3d_extension sps_3d_ext[2];
    // sps scc extension , see 7.3.2.2.3
    struct sps_scc_extension sps_scc_ext;
    // sps extension 4bits
    // rbsp tailling
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


struct sei_msg {
    uint8_t last_paylod_type;
    uint8_t last_paylod_size;

};

struct sei {
    int num;
    struct sei_msg * msg;
};


//sample adaptive offset
struct sao {
    uint8_t sao_merge_left_flag;
    uint8_t sao_merge_up_flag;
    uint8_t sao_offset_abs[3][64][64][4];
    uint8_t sao_offset_sign[3][64][64][4];
    uint8_t sao_band_position[3][64][64];
    uint8_t sao_eo_class_luma;
    uint8_t sao_eo_class_chroma;

    // uint32_t sao_type_idx_luma;
    // uint32_t sao_type_idx_chroma;
    uint8_t SaoTypeIdx[3][64][64];
};



struct slice_long_term {
    uint8_t lt_idx_sps;
    uint8_t poc_lsb_lt;
    uint8_t used_by_curr_pic_lt_flag:1;
    uint8_t delta_poc_msb_present_flag:1;
    GUE(delta_poc_msb_cycle_lt);
};

struct palette_predictor_entries {
    int PaletteMaxPredictorSize; /* see 7-35  
                                = palette_max_size + delta_palette_max_predictor_size 
                                when delta_palette_max_predictor_size not present, it should be 0
                                When an active SPS for the base layer has palette_mode_enabled_flag equal to 1,
                                palette_max_size shall be less than or equal to 64 and 
                                PaletteMaxPredictorSize shall be less than or equal to 128 */
    int PredictorPaletteSize;
    int PredictorPaletteEntries[3][128]; /*  [nComp][PaletteMaxPredictorSize] */
};

typedef struct {
    int x;
    int y;
} scanpos;

struct slice_segment_header {
    uint8_t ChromaArrayType;//= (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc)

    uint8_t no_output_of_prior_pics_flag : 1;
    uint8_t dependent_slice_segment_flag : 1;

    GUE(slice_pic_parameter_set_id);
    uint32_t slice_segment_address;

    GUE(slice_type);
    uint8_t pic_output_flag:1;
    uint8_t colour_plane_id:2; // 0-2 means: Y, Cb, Cr

    uint32_t slice_pic_order_cnt_lsb;
    
    uint8_t short_term_ref_pic_set_sps_flag;

    struct st_ref_pic_set *st;

    uint32_t short_term_ref_pic_set_idx;
    int CurrRpsIdx;

    GUE(num_long_term_sps);
    GUE(num_long_term_pics);

    uint8_t *PocLsbLt;
    uint8_t *UsedByCurrPicLt;
    struct slice_long_term* terms;

    int* DeltaPocMsbCycleLt;

    uint8_t slice_temporal_mvp_enabled_flag;

    uint8_t inter_layer_pred_enabled_flag;
    uint32_t num_inter_layer_ref_pics_minus1;
    int NumActiveRefLayerPics;
    int refLayerPicIdc[64];
    int numRefLayerPics;
    int RefPicLayerId[64];

    uint32_t *inter_layer_pred_layer_idc;

    uint8_t in_comp_pred_flag:1;

    uint8_t slice_sao_luma_flag:1;
    uint8_t slice_sao_chroma_flag:1;
    

    GUE(num_ref_idx_l0_active_minus1);
    GUE(num_ref_idx_l1_active_minus1);

    int DeltaPocS0[64][64];
    int DeltaPocS1[64][64];
    int UsedByCurrPicS0[64][64];
    int UsedByCurrPicS1[64][64];
    int NumPositivePics[64];
    int NumNegativePics[64];
    int NumDeltaPocs[64];
    int NumPicTotalCurr;

    uint8_t ref_pic_list_modification_flag_l0;
    uint32_t *list_entry_l0;

    uint8_t ref_pic_list_modification_flag_l1;
    uint32_t *list_entry_l1;

    uint8_t mvd_l1_zero_flag:1;
    uint8_t cabac_init_flag:1;

    GUE(collocated_ref_idx);

    uint8_t slice_ic_enabled_flag;
    uint8_t slice_ic_disabled_merge_zero_idx_flag;

    GUE(five_minus_max_num_merge_cand);
    uint8_t use_integer_mv_flag:1;

    GSE(slice_qp_delta);
    GSE(slice_cb_qp_offset);
    GSE(slice_cr_qp_offset);

    GSE(slice_act_y_qp_offset);
    GSE(slice_act_cb_qp_offset);
    GSE(slice_act_cr_qp_offset);
    uint8_t cu_chroma_qp_offset_enabled_flag:1;

    GSE(slice_beta_offset_div2);
    GSE(slice_tc_offset_div2);

    uint8_t slice_loop_filter_across_slices_enabled_flag:1;

    GUE(num_entry_point_offsets);
    // GUE(offset_len_minus1);
    uint32_t *entry_point_offset;
    GUE(slice_segment_header_extension_length);
    uint8_t poc_reset_period_id:6;
    uint8_t full_poc_reset_flag:1;
    uint32_t poc_lsb_val;

    GUE(poc_msb_cycle_val);

    // struct residual_coding rc[64][64];

    //Coding Tree Block
    int SubWidthC, SubHeightC;

    int MinCbLog2SizeY;
    int CtbLog2SizeY;
    int MinCbSizeY;
    int CtbSizeY;

    int PicWidthInCtbsY;
    int PicHeightInCtbsY;

    int PicWidthInMinCbsY;
    int PicHeightInMinCbsY;

    int PicSizeInMinCbsY;
    int PicSizeInCtbsY;

    int PicSizeInSamplesY;

    int PicWidthInSamplesC;
    int PicHeightInSamplesC;

    int CtbWidthC, CtbHeightC;

    int MinTbLog2SizeY, MaxTbLog2SizeY;

    //code quadtree
    int IsCuQpDeltaCoded;
    int CuQpDeltaVal;
    int IsCuChromaQpOffsetCoded;

    int Log2MinCuQpDeltaSize;
    int Log2MinCuChromaQpOffsetSize;

    //palette_predictor_entries
    struct palette_predictor_entries ppe;

    // [log2BlockSize][scanIdx][sPos][sComp]
    // log2BlockSize range 2-5, 
    // scanIdx 0-2: 0 for up-right, 1 for horizontal, 2 for vertical, 3 for traverse
    // sPos range 0 - blkSize * blkSize,
    scanpos* ScanOrder[6][4];

    uint8_t ScalingFactor[4][6][64][64];
};


enum inter_pred_mpde {
    PRED_L0 = 0,
    PRED_L1 = 1,
    PRED_BI = 2,
};


struct pcm_sample {
    int *pcm_sample_luma;
    int *pcm_sample_chroma;
};

struct mvd_coding {
    uint8_t abs_mvd_greater0_flag[2];
    uint8_t abs_mvd_greater1_flag[2];
    uint8_t mvd_sign_flag[2];
    uint32_t abs_mvd_minus2[2];
};

struct predication_unit {
    uint8_t mvp_l0_flag; /* the motion vector predictor index of list 0*/
    uint8_t mvp_l1_flag; /* the motion vector predictor index of list 1*/
    uint8_t merge_flag; /* whether the inter prediction parameters for the current prediction
                         unit are inferred from a neighbouring inter-predicted partition */
    int merge_idx;      /* merging candidate index of the merging candidate list */
    int inter_pred_idc; /* whether list0, list1, or bi-prediction is used for
                          the current prediction unit. see  @enum inter_pred_mpde*/
    int ref_idx_l0;     /* list 0 reference picture index for the current prediction unit */
    int ref_idx_l1;     /* list 1 reference picture index for the current prediction unit */
    struct mvd_coding *mvd;
    int MvdL1[64][64][2];
};


enum part_mode {
    PART_2Nx2N = 0,
    PART_2NxN,
    PART_Nx2N,
    PART_NxN,
    PART_2NxnU,
    PART_2NxnD,
    PART_nLx2N,
    PART_nRx2N,
    PART_NUM,
};


struct cross_comp_pred {
    // uint32_t log2_res_scale_abs_plus1;
    // uint32_t res_scale_sign_flag;
    uint32_t ResScaleVal[3];
};

struct tu {
    int x0;
    int y0;
    int nTbS;
    int qpy;
    int depth;
    uint8_t split_transform_flag;
};

struct trans_tree {
    int xT0;
    int yT0;
    // uint8_t split_transform_flag[3];
    // uint32_t cbf_cb[3][32][32];
    // uint32_t cbf_cr[3][32][32];
    // uint32_t cbf_luma[3][32][32];

    int8_t transform_skip_flag[4][32][32];
    // see 7.4.9.10
    uint8_t tu_residual_act_flag[32][32];
    int16_t TransCoeffLevel[3][32][32]; // 3 for color index, 0 for Y, 1 for Cb,
                                        // 2 for Cr
    int tu_num;
    struct tu tus[64];
};


struct intra_mode_ext {
    uint8_t no_dim_flag;
    uint8_t depth_intra_mode_idx_flag;
    uint8_t wedge_full_tab_idx;
};

#define PALETTE_MAX_SIZE (2048)
struct palette_coding {
    // uint32_t palette_predictor_run;
    uint8_t PalettePredictorEntryReuseFlags[PALETTE_MAX_SIZE];
    uint32_t num_signalled_palette_entries;
    uint32_t new_palette_entries[3][PALETTE_MAX_SIZE]; // tobe
    uint8_t palette_escape_val_present_flag;
    uint32_t num_palette_indices_minus1;

    uint8_t copy_above_indices_for_final_run_flag;
    uint8_t palette_transpose_flag;

    uint8_t copy_above_palette_indices_flag;

    // uint32_t palette_idx_idc;
    uint32_t PaletteIndexIdc[PALETTE_MAX_SIZE];
    int MaxPaletteIndex;
    // uint32_t PaletteEscapeVal[3][64][64];

    // uint8_t CopyAboveIndicesFlag[64][64];
    // uint32_t PaletteIndexMap[64][64];
};

struct cu_extension {
    uint8_t skip_intra_mode_idx;
    uint8_t dbbp_flag;
    uint8_t dc_only_flag;
    uint8_t iv_res_pred_weight_idx;
    uint8_t illu_comp_flag;

    //depth_dcs
    uint8_t depth_dc_present_flag;
    uint8_t depth_dc_abs[3];
    uint8_t depth_dc_sign_flag[8];
};

struct cu {
    int blkIdx;
    uint8_t cu_transquant_bypass_flag;
    // uint8_t cu_skip_flag;
#ifdef ENABLE_3D
    uint8_t skip_intra_flag[64][64];
#endif
    // pred_mode_flag[][]; use value below
    uint8_t CuPredMode;
    int PartMode;

    int MaxTrafoDepth;
    uint8_t IntraSplitFlag;
    uint8_t palette_mode_flag;
    struct palette_coding *pc[64][64];
    // part_mode;
    // uint8_t pcm_flag[64][64];
    struct pcm_sample *pcm;
    //pcm_alignment_zero_bit
    // uint8_t prev_intra_luma_pred_flag[64][64];
#if ENABLE_3D
    struct intra_mode_ext intra_ext[64][64];
#endif
    // uint8_t mpm_idx[64][64];
    // uint8_t rem_intra_luma_pred_mode[64][64];
    // uint8_t intra_chroma_pred_mode[64][64];
    uint8_t rqt_root_cbf;

    struct cross_comp_pred ccp[64][64];

    // uint8_t IntraPredModeY[64][64];
    // int IntraPredModeC;

    int CtDepth;
    int x0;
    int y0;
    int nCbS;
    struct cu_extension ext[64][64];

    struct predication_unit pu[8][8];

    struct trans_tree tt;

    int32_t CuQpOffsetCb;
    int32_t CuQpOffsetCr;
    int PaletteEscapeVal[3][64][64];
    uint32_t PaletteIndexMap[64][64];
    // struct trans_unit *tu;
};


struct chroma_qp_offset {
    uint8_t cu_chroma_qp_offset_flag;
    uint8_t cu_chroma_qp_offset_idx;

    // uint8_t IsCuChromaQpOffsetCoded;
};


// struct quad_tree {
//     struct cu *cu;
//     // uint8_t split_cu_flag;
// };

struct ctu {
    struct sao *sao;
    int cu_num;
    struct cu *cu[64]; // MAX is 64*64 CTU divided into 64 numbers of 8*8 cu
};


struct hevc_param_set {
    int vps_num;
    struct vps *vps;

    int sps_num;
    struct sps *sps;

    int pps_num;
    struct pps *pps;
};

struct hevc_slice {
    struct hevc_nalu_header *nalu;
    struct slice_segment_header *slice;
    struct rps *rps;
    struct ctu *ctu;
};

#pragma pack(pop)

void parse_nalu(uint8_t *data, int len);

#ifdef __cplusplus
}
#endif

#endif /*_HEVC_H_*/
