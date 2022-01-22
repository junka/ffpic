#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "hevc.h"


static void
rbsp_trailing_bits(struct bits_vec *v)
{
    //should be 1
    uint8_t rbsp_stop_one_bit = READ_BIT(v);
    while (!BYTE_ALIGNED(v)) {
        SKIP_BITS(v, 1);
    }
}

static void
byte_alignment(struct bits_vec *v)
{
    uint8_t alignment_bit_equal_to_one = READ_BIT(v);
    while (!BYTE_ALIGNED(v)) {
        SKIP_BITS(v, 1);
    }
}

static bool
more_rbsp_data(struct bits_vec *v)
{
    if (EOF_BITS(v, 1)) {
        return false;
    }
    if (TEST_BIT(v) == 1) {
        return false;
    }
    return true;
}

/* see 7.3.4 */
static void
parse_scaling_list_data(struct bits_vec *v, struct scaling_list_data *sld)
{
    for (int sizeid = 0; sizeid < 4; sizeid ++) {
        for (int mid = 0; mid < 6; mid += (sizeid == 3)?3:1) {
            sld->scaling_list_pred_mode_flag[sizeid][mid] = READ_BIT(v);
            if (!sld->scaling_list_pred_mode_flag[sizeid][mid]) {
                sld->scaling_list_pred_matrix_id_delta[sizeid][mid] = GOL_UE(v);
            } else {
                uint8_t nextcoef = 8;
                sld->coefNum = MIN(64, 1 << (4 + (sizeid<<1)));
                if (sizeid > 1) {
                    sld->scaling_list_dc_coef_minus8[sizeid-2][mid] = GOL_SE(v);
                    nextcoef = sld->scaling_list_dc_coef_minus8[sizeid-2][mid] + 8;
                }
                for (int i = 0; i < sld->coefNum; i ++) {
                    sld->scaling_list_delta_coef[i] = GOL_SE(v);
                    nextcoef = (nextcoef + sld->scaling_list_delta_coef[i] + 256) % 256;
                    sld->scalinglist[sizeid][mid][i] = nextcoef;
                }
            }
        }
    }
}

// see 7.3.3 
static void
parse_profile_tier_level(struct bits_vec *v, struct profile_tier_level *ptl,
        uint8_t profilepresentFlag, uint8_t maxNumSubLayersMinus1)
{
    printf("profile flag %d, num %d\n", profilepresentFlag, maxNumSubLayersMinus1);
    if (profilepresentFlag) {
        ptl->general_profile_space = READ_BITS(v, 2);
        ptl->general_tier_flag = READ_BIT(v);
        ptl->general_profile_idc = READ_BITS(v, 5);
        for (int i = 0; i < 32; i ++) {
            ptl->general_profile_compatibility_flag |= READ_BIT(v) << i;
        }
        printf("general_profile_idc %d\n", ptl->general_profile_idc);
        printf("general_profile_compatibility_flag 0x%x\n", ptl->general_profile_compatibility_flag);
        int k = 0;
        ptl->general_progressive_source_flag = READ_BIT(v);
        ptl->general_interlaced_source_flag = READ_BIT(v);
        ptl->general_non_packed_constraint_flag = READ_BIT(v);
        ptl->general_frame_only_constraint_flag = READ_BIT(v);
        k += 4;
        if (ptl->general_profile_idc == 4 || (ptl->general_profile_compatibility_flag & (1 << 4)) ||
            ptl->general_profile_idc == 5 || (ptl->general_profile_compatibility_flag & (1 << 5)) ||
            ptl->general_profile_idc == 6 || (ptl->general_profile_compatibility_flag & (1 << 6)) ||
            ptl->general_profile_idc == 7 || (ptl->general_profile_compatibility_flag & (1 << 7)) ||
            ptl->general_profile_idc == 8 || (ptl->general_profile_compatibility_flag & (1 << 8)) ||
            ptl->general_profile_idc == 9 || (ptl->general_profile_compatibility_flag & (1 << 9)) ||
            ptl->general_profile_idc == 10 || (ptl->general_profile_compatibility_flag & (1 << 10)) ||
            ptl->general_profile_idc == 11 || (ptl->general_profile_compatibility_flag & (1 << 11)))
        {
            ptl->general_max_12bit_constraint_flag = READ_BIT(v);
            ptl->general_max_10bit_constraint_flag = READ_BIT(v);
            ptl->general_max_8bit_constraint_flag = READ_BIT(v);
            ptl->general_max_422chroma_constraint_flag = READ_BIT(v);
            ptl->general_max_420chroma_constraint_flag = READ_BIT(v);
            ptl->general_max_monochrome_constraint_flag = READ_BIT(v);
            ptl->general_intra_constraint_flag = READ_BIT(v);
            ptl->general_one_picture_only_constraint_flag = READ_BIT(v);
            ptl->general_lower_bit_rate_constraint_flag = READ_BIT(v);
            k += 9;
            if (ptl->general_profile_idc == 5 || (ptl->general_profile_compatibility_flag & (1 << 5)) ||
                ptl->general_profile_idc == 9 || (ptl->general_profile_compatibility_flag & (1 << 9)) ||
                ptl->general_profile_idc == 10 || (ptl->general_profile_compatibility_flag & (1 << 10)) ||
                ptl->general_profile_idc == 11 || (ptl->general_profile_compatibility_flag & (1 << 11))) {
                ptl->general_max_14bit_constraint_flag = READ_BIT(v);
                SKIP_BITS(v, 33);
                k += 34;
            } else {
                SKIP_BITS(v, 34);
                k += 34;
            }
        } else if (ptl->general_profile_idc == 2 || (ptl->general_profile_compatibility_flag & (1 << 2))) {
            SKIP_BITS(v, 7);
            ptl->general_one_picture_only_constraint_flag = READ_BIT(v);
            SKIP_BITS(v, 35);
            k += 43;
        } else {
            SKIP_BITS(v, 43);
            k += 43;
        }
        if (ptl->general_profile_idc == 1 || (ptl->general_profile_compatibility_flag & (1 << 1)) ||
            ptl->general_profile_idc == 2 || (ptl->general_profile_compatibility_flag & (1 << 2)) ||
            ptl->general_profile_idc == 3 || (ptl->general_profile_compatibility_flag & (1 << 3)) ||
            ptl->general_profile_idc == 4 || (ptl->general_profile_compatibility_flag & (1 << 4)) ||
            ptl->general_profile_idc == 5 || (ptl->general_profile_compatibility_flag & (1 << 5)) ||
            ptl->general_profile_idc == 9 || (ptl->general_profile_compatibility_flag & (1 << 9)) ||
            ptl->general_profile_idc == 11 || (ptl->general_profile_compatibility_flag & (1 << 11)))
        {
            ptl->general_inbld_flag = READ_BIT(v);
            printf("general_inbld_flag %d\n", ptl->general_inbld_flag);
        } else {
            SKIP_BITS(v, 1);
        }
        k += 1;
        printf("read flag bits %d\n", k);
        ptl->general_level_idc = READ_BITS(v, 8);
        for (int i = 0; i < maxNumSubLayersMinus1; i ++) {
            ptl->sub_layer_flag[i].sub_layer_profile_present_flag = READ_BIT(v);
            ptl->sub_layer_flag[i].sub_layer_level_present_flag = READ_BIT(v);
        }
        if (maxNumSubLayersMinus1 > 0) {
            for (int i = maxNumSubLayersMinus1; i < 8; i ++) {
                SKIP_BITS(v, 2);
            }
        }
        if (maxNumSubLayersMinus1 > 0) {
            ptl->sublayers = malloc(maxNumSubLayersMinus1 * sizeof(struct sub_layer));
            for (int i = 0; i < maxNumSubLayersMinus1; i ++) {
                if (ptl->sub_layer_flag[i].sub_layer_profile_present_flag) {
                    ptl->sublayers[i].sub_layer_profile_space = READ_BITS(v, 2);
                    ptl->sublayers[i].sub_layer_tier_flag = READ_BIT(v);
                    ptl->sublayers[i].sub_layer_profile_idc = READ_BITS(v, 5);
                    for (int j = 0; j < 32; j ++) {
                        ptl->sublayers[i].sub_layer_profile_compatibility_flag |= READ_BIT(v) << j;
                    }
                    ptl->sublayers[i].sub_layer_progressive_source_flag = READ_BIT(v);
                    ptl->sublayers[i].sub_layer_interlaced_source_flag = READ_BIT(v);
                    ptl->sublayers[i].sub_layer_non_packed_constraint_flag = READ_BIT(v);
                    ptl->sublayers[i].sub_layer_frame_only_constraint_flag = READ_BIT(v);
                    if (ptl->sublayers[i].sub_layer_profile_idc == 4 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 4)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 5 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 5)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 6 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 6)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 7 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 7)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 8 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 8)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 9 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 9)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 10 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 10)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 11 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 11))) 
                    {
                        ptl->sublayers[i].sub_layer_max_12bit_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_max_10bit_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_max_8bit_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_max_422chroma_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_max_420chroma_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_intra_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_one_picture_only_constraint_flag = READ_BIT(v);
                        ptl->sublayers[i].sub_layer_lower_bit_rate_constraint_flag = READ_BIT(v);
                        if (ptl->sublayers[i].sub_layer_profile_idc == 5 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 5)) ||
                            ptl->sublayers[i].sub_layer_profile_idc == 9 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 9)) ||
                            ptl->sublayers[i].sub_layer_profile_idc == 10 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 10)) ||
                            ptl->sublayers[i].sub_layer_profile_idc == 11 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 11)))
                        {
                            ptl->sublayers[i].sub_layer_max_14bit_constraint_flag = READ_BIT(v);
                            SKIP_BITS(v, 33);
                        } else {
                            SKIP_BITS(v, 34);
                        }
                    } else if (ptl->sublayers[i].sub_layer_profile_idc == 2 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 2))) {
                        SKIP_BITS(v, 7);
                        ptl->sublayers[i].sub_layer_one_picture_only_constraint_flag = READ_BIT(v);
                        SKIP_BITS(v, 35);
                    } else {
                        SKIP_BITS(v, 43);
                    }
                    if (ptl->sublayers[i].sub_layer_profile_idc == 1 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 1)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 2 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 2)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 3 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 3)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 4 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 4)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 5 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 5)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 9 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 9)) ||
                        ptl->sublayers[i].sub_layer_profile_idc == 11 || (ptl->sublayers[i].sub_layer_profile_compatibility_flag & (1 << 11)))
                    {
                        ptl->sublayers[i].sub_layer_inbld_flag = READ_BIT(v);
                    } else {
                        SKIP_BITS(v, 1);
                    }
                }
                if (ptl->sub_layer_flag[i].sub_layer_level_present_flag) {
                    ptl->sublayers[i].sub_layer_level_idc = READ_BITS(v, 8);
                }
            }
        }
    }
}

static void
parse_sub_layer_hrd_parameters(struct bits_vec *v, struct sub_layer_hrd_parameters * sub,
        int cpb_cnt_minus1, int sub_pic_hrd_params_present_flag)
{
    sub->CpbCnt = cpb_cnt_minus1 + 1;
    sub->cpb = malloc(sizeof(struct cpb)* sub->CpbCnt);
    for (int i = 0; i < sub->CpbCnt; i ++) {
        sub->cpb[i].bit_rate_value_minus1 = GOL_UE(v);
        sub->cpb[i].cpb_size_value_minus1 = GOL_UE(v);
        if (sub_pic_hrd_params_present_flag) {
            sub->cpb[i].cpb_size_du_value_minus1 = GOL_UE(v);
            sub->cpb[i].bit_rate_du_value_minus1 = GOL_UE(v);
        }
        sub->cpb[i].cbr_flag = READ_BIT(v);
    }
}

static void
parse_hrd_parameters(struct bits_vec *v, struct hrd_parameters *hrd,
                    uint8_t commonInfPresentFlag, int maxNumSubLayersMinus1)
{
    if (commonInfPresentFlag) {
        hrd->nal_hrd_parameters_present_flag = READ_BIT(v);
        hrd->vcl_hrd_parameters_present_flag = READ_BIT(v);
        if (hrd->nal_hrd_parameters_present_flag || hrd->vcl_hrd_parameters_present_flag) {
            hrd->sub_pic_hrd_params_present_flag = READ_BIT(v);
            if (hrd->sub_pic_hrd_params_present_flag) {
                hrd->tick_divisor_minus2 = READ_BITS(v, 8);
                hrd->du_cpb_removal_delay_increment_length_minus1 = READ_BITS(v, 5);
                hrd->sub_pic_cpb_params_in_pic_timing_sei_flag = READ_BIT(v);
                hrd->dpb_output_delay_du_length_minus1 = READ_BITS(v, 5);
            }
            hrd->bit_rate_scale = READ_BITS(v, 4);
            hrd->cpb_size_scale = READ_BITS(v, 4);
            if (hrd->sub_pic_hrd_params_present_flag) {
                hrd->cpb_size_du_scale = READ_BITS(v, 4);
            }
            hrd->initial_cpb_removal_delay_length_minus1 = READ_BITS(v, 5);
            hrd->au_cpb_removal_delay_length_minus1 = READ_BITS(v, 5);
            hrd->dpb_output_delay_length_minus1 = READ_BITS(v, 5);
        }
    }
    for (int i = 0; i < maxNumSubLayersMinus1; i ++) {
        hrd->hrd_layer[i].fixed_pic_rate_general_flag = READ_BIT(v);
        if (!hrd->hrd_layer[i].fixed_pic_rate_general_flag) {
            hrd->hrd_layer[i].fixed_pic_rate_within_cvs_flag = READ_BIT(v);
            if (hrd->hrd_layer[i].fixed_pic_rate_within_cvs_flag) {
                hrd->hrd_layer[i].elemental_duration_in_tc_minus1 = GOL_UE(v);
            }
        } else {
            hrd->hrd_layer[i].low_delay_hrd_flag =  READ_BIT(v);
            if (!hrd->hrd_layer[i].low_delay_hrd_flag) {
                hrd->hrd_layer[i].cpb_cnt_minus1 = GOL_UE(v);
            }
        }
        if (hrd->nal_hrd_parameters_present_flag) {
            hrd->hrd_layer[i].nal_hrd = malloc(sizeof(struct sub_layer_hrd_parameters));
            parse_sub_layer_hrd_parameters(v, hrd->hrd_layer[i].nal_hrd, hrd->hrd_layer[i].cpb_cnt_minus1, hrd->sub_pic_hrd_params_present_flag);
        }
        if (hrd->vcl_hrd_parameters_present_flag) {
            hrd->hrd_layer[i].vcl_hrd = malloc(sizeof(struct sub_layer_hrd_parameters));
            parse_sub_layer_hrd_parameters(v, hrd->hrd_layer[i].vcl_hrd, hrd->hrd_layer[i].cpb_cnt_minus1, hrd->sub_pic_hrd_params_present_flag);

        }   
    }
}

static void
parse_vui(struct bits_vec *v, struct vui_parameters *vui, int sps_max_sub_layer_minus1)
{
    vui->aspect_ratio_info_present_flag = READ_BIT(v);
    if (vui->aspect_ratio_info_present_flag) {
        vui->aspect_ratio_idc = READ_BITS(v, 8);
        if (vui->aspect_ratio_idc == EXTENDED_SAR) {
            vui->sar_width = READ_BITS(v, 16);
            vui->sar_height = READ_BITS(v, 16);
        }
    }
    vui->overscan_info_present_flag = READ_BIT(v);
    if (vui->overscan_info_present_flag) {
        vui->overscan_appropriate_flag = READ_BIT(v);
    }
    vui->video_signal_type_present_flag = READ_BIT(v);
    if (vui->video_signal_type_present_flag) {
        vui->video_format = READ_BITS(v, 3);
        vui->video_full_range_flag = READ_BIT(v);
        vui->colour_description_present_flag = READ_BIT(v);
        if (vui->colour_description_present_flag) {
            vui->colour_primaries = READ_BITS(v, 8);
            vui->transfer_characteristics = READ_BITS(v, 8);
            vui->matrix_coeffs = READ_BITS(v, 8);
        }
    }
    vui->chroma_loc_info_present_flag = READ_BIT(v);
    if (vui->chroma_loc_info_present_flag) {
        vui->chroma_sample_loc_type_top_field = GOL_UE(v);
        vui->chroma_sample_loc_type_bottom_field = GOL_UE(v);
    }
    vui->neutral_chroma_indication_flag = READ_BIT(v);
    vui->field_seq_flag = READ_BIT(v);
    vui->frame_field_info_present_flag = READ_BIT(v);
    vui->default_display_window_flag = READ_BIT(v);
    if (vui->default_display_window_flag) {
        vui->def_disp_win_left_offset = GOL_UE(v);
        vui->def_disp_win_right_offset = GOL_UE(v);
        vui->def_disp_win_top_offset = GOL_UE(v);
        vui->def_disp_win_bottom_offset = GOL_UE(v);
    }
    vui->vui_timing_info_present_flag = READ_BIT(v);
    if (vui->vui_timing_info_present_flag) {
        vui->vui_num_units_in_tick = READ_BITS(v, 32);
        vui->vui_time_scale = READ_BITS(v, 32);
        vui->vui_poc_proportional_to_timing_flag = READ_BIT(v);
        if (vui->vui_poc_proportional_to_timing_flag) {
            vui->vui_num_ticks_poc_diff_one_minus1 = GOL_UE(v);
        }
        vui->vui_hrd_parameters_present_flag = READ_BIT(v);
        if (vui->vui_hrd_parameters_present_flag) {
            vui->vui_hrd_para = malloc(sizeof(struct hrd_parameters));
            parse_hrd_parameters(v, vui->vui_hrd_para, 1, sps_max_sub_layer_minus1);
        }
        vui->bitstream_restriction_flag = READ_BIT(v);
        if (vui->bitstream_restriction_flag) {
            vui->tiles_fixed_structure_flag = READ_BIT(v);
            vui->motion_vectors_over_pic_boundaries_flag = READ_BIT(v);
            vui->restricted_ref_pic_lists_flag = READ_BIT(v);
            vui->min_spatial_segmentation_idc = GOL_UE(v);
            vui->max_bytes_per_pic_denom = GOL_UE(v);
            vui->max_bits_per_min_cu_denom = GOL_UE(v);
            vui->log2_max_mv_length_horizontal = GOL_UE(v);
            vui->log2_max_mv_length_vertical = GOL_UE(v);
        }
    }
}

static void
parse_color_mapping_table(struct bits_vec *v, struct color_mapping_table *t)
{
    t->num_cm_ref_layers_minus1 = GOL_UE(v);
    for (int i = 0; i <= t->num_cm_ref_layers_minus1; i ++) {
        t->cm_ref_layer_id[i] = READ_BITS(v, 6);
    }
    t->cm_octant_depth = READ_BITS(v, 2);
    t->cm_y_part_num_log2 = READ_BITS(v, 2);
    t->luma_bit_depth_cm_input_minus8 = GOL_UE(v);
    t->chroma_bit_depth_cm_input_minus8 = GOL_UE(v);
    t->luma_bit_depth_cm_output_minus8 = GOL_UE(v);
    t->chroma_bit_depth_cm_output_minus8 = GOL_UE(v);
    t->cm_res_quant_bits = READ_BITS(v, 2);
    t->cm_delta_flc_bits_minus1 = READ_BITS(v, 2);
    if (t->cm_octant_depth == 1) {
        t->cm_adapt_threshold_u_delta = GOL_SE(v);
        t->cm_adapt_threshold_v_delta = GOL_SE(v);
    }
    // color _mapping 
}

static struct pps*
parse_pps(struct hevc_nalu_header *h, uint8_t *data, uint16_t len)
{
    struct bits_vec *v = bits_vec_alloc(data, len, BITS_MSB);
    struct pps *pps = malloc(sizeof(struct pps));
    pps->pps_pic_parameter_set_id = GOL_UE(v);
    pps->pps_seq_parameter_set_id = GOL_UE(v);
    pps->dependent_slice_segments_enabled_flag = READ_BIT(v);
    pps->output_flag_present_flag = READ_BIT(v);
    pps->num_extra_slice_header_bits = READ_BITS(v, 3);
    pps->sign_data_hiding_enabled_flag = READ_BIT(v);
    pps->cabac_init_present_flag = READ_BIT(v);
    pps->num_ref_idx_l0_default_active_minus1 = GOL_UE(v);
    pps->num_ref_idx_l1_default_active_minus1 = GOL_UE(v);
    pps->init_qp_minus26 = GOL_SE(v);
    pps->constrained_intra_pred_flag = READ_BIT(v);
    pps->transform_skip_enabled_flag = READ_BIT(v);
    pps->cu_qp_delta_enabled_flag = READ_BIT(v);
    if (pps->cu_qp_delta_enabled_flag) {
        pps->diff_cu_qp_delta_depth = GOL_UE(v);
    }
    pps->pps_cb_qp_offset = GOL_SE(v);
    pps->pps_cr_qp_offset = GOL_SE(v);
    pps->pps_slice_chroma_qp_offsets_present_flag = READ_BIT(v);
    pps->weighted_pred_flag = READ_BIT(v);
    pps->weighted_bipred_flag = READ_BIT(v);
    pps->transquant_bypass_enabled_flag = READ_BIT(v);
    pps->tiles_enabled_flag = READ_BIT(v);
    pps->entropy_coding_sync_enabled_flag = READ_BIT(v);
    if (pps->tiles_enabled_flag) {
        pps->num_tile_columns_minus1 = GOL_UE(v);
        pps->num_tile_rows_minus1 = GOL_UE(v);
        pps->uniform_spacing_flag = READ_BIT(v);
        if (!pps->uniform_spacing_flag) {
            for (int i = 0; i < pps->num_tile_columns_minus1; i ++) {
                // pps->column_width_minus1[i] = GOL_UE(v);
            }
            for (int i = 0; i< pps->num_tile_rows_minus1; i ++) {
                // pps->row_height_minus1[i] = GOL_UE(v);
            }
        }
        pps->loop_filter_across_tiles_enabled_flag = READ_BIT(v);
    }
    pps->pps_loop_filter_across_slices_enabled_flag = READ_BIT(v);
    pps->deblocking_filter_control_present_flag = READ_BIT(v);
    if (pps->deblocking_filter_control_present_flag) {
        pps->deblocking_filter_override_enabled_flag = READ_BIT(v);
        pps->pps_deblocking_filter_disabled_flag = READ_BIT(v);
        if (!pps->pps_deblocking_filter_disabled_flag) {
            pps->pps_beta_offset_div2 = GOL_SE(v);
            pps->pps_tc_offset_div2 = GOL_SE(v);
        }
    }
    pps->pps_scaling_list_data_present_flag = READ_BIT(v);
    if (pps->pps_scaling_list_data_present_flag)  {
        parse_scaling_list_data(v, &pps->list_data);
    }
    pps->lists_modification_present_flag = READ_BIT(v);
    pps->log2_parallel_merge_level_minus2 = GOL_UE(v);
    pps->slice_segment_header_extension_present_flag = READ_BIT(v);
    pps->pps_extension_present_flag = READ_BIT(v);
    if (pps->pps_extension_present_flag) {
        pps->pps_range_extension_flag = READ_BIT(v);
        pps->pps_multilayer_extension_flag = READ_BIT(v);
        pps->pps_3d_extension_flag = READ_BIT(v);
        pps->pps_scc_extension_flag = READ_BIT(v);
        pps->pps_extension_4bits = READ_BITS(v, 4);
    }
    if (pps->pps_range_extension_flag) {
        if (pps->transform_skip_enabled_flag)
            pps->pps_range_ext.log2_max_transform_skip_block_size_minus2 = GOL_UE(v);
        pps->pps_range_ext.cross_component_prediction_enabled_flag = READ_BIT(v);
        pps->pps_range_ext.chroma_qp_offset_list_enabled_flag = READ_BIT(v);
        if (pps->pps_range_ext.chroma_qp_offset_list_enabled_flag) {
            pps->pps_range_ext.diff_cu_chroma_qp_offset_depth = GOL_UE(v);
            pps->pps_range_ext.chroma_qp_offset_list_len_minus1 = GOL_UE(v);
            for (int i = 0; i <= pps->pps_range_ext.chroma_qp_offset_list_len_minus1; i ++) {
                pps->pps_range_ext.cb_qp_offset_list[i] = GOL_SE(v);
                pps->pps_range_ext.cr_qp_offset_list[i] = GOL_SE(v);
            }
        }
        pps->pps_range_ext.log2_sao_offset_scale_luma = GOL_UE(v);
        pps->pps_range_ext.log2_sao_offset_scale_chroma = GOL_UE(v);
    }
    if (pps->pps_multilayer_extension_flag) {
        pps->pps_multiplayer_ext.poc_reset_info_present_flag = READ_BIT(v);
        pps->pps_multiplayer_ext.pps_infer_scaling_list_flag = READ_BIT(v);
        if (pps->pps_multiplayer_ext.pps_infer_scaling_list_flag) {
            pps->pps_multiplayer_ext.pps_scaling_list_ref_layer_id = READ_BITS(v, 6);
        }
        pps->pps_multiplayer_ext.num_ref_loc_offsets = GOL_UE(v);
        for (int i = 0; i < pps->pps_multiplayer_ext.num_ref_loc_offsets; i ++) {
            pps->pps_multiplayer_ext.reflayer[i].ref_loc_offset_layer_id = READ_BITS(v, 6);
            pps->pps_multiplayer_ext.reflayer[i].scaled_ref_layer_offset_present_flag = READ_BIT(v);
            if (pps->pps_multiplayer_ext.reflayer[i].scaled_ref_layer_offset_present_flag) {
                pps->pps_multiplayer_ext.reflayer[i].scaled_ref_layer_left_offset = GOL_SE(v);
                pps->pps_multiplayer_ext.reflayer[i].scaled_ref_layer_top_offset = GOL_SE(v);
                pps->pps_multiplayer_ext.reflayer[i].scaled_ref_layer_right_offset = GOL_SE(v);
                pps->pps_multiplayer_ext.reflayer[i].scaled_ref_layer_bottom_offset = GOL_SE(v);
            }
            pps->pps_multiplayer_ext.reflayer[i].ref_region_offset_present_flag = READ_BIT(v);
            if (pps->pps_multiplayer_ext.reflayer[i].ref_region_offset_present_flag) {
                pps->pps_multiplayer_ext.reflayer[i].ref_region_left_offset = GOL_SE(v);
                pps->pps_multiplayer_ext.reflayer[i].ref_region_top_offset = GOL_SE(v);
                pps->pps_multiplayer_ext.reflayer[i].ref_region_right_offset = GOL_SE(v);
                pps->pps_multiplayer_ext.reflayer[i].ref_region_bottom_offset = GOL_SE(v);
            }
            pps->pps_multiplayer_ext.reflayer[i].resample_phase_set_present_flag = READ_BIT(v);
            if (pps->pps_multiplayer_ext.reflayer[i].resample_phase_set_present_flag) {
                pps->pps_multiplayer_ext.reflayer[i].phase_hor_luma = GOL_UE(v);
                pps->pps_multiplayer_ext.reflayer[i].phase_ver_luma = GOL_UE(v);
                pps->pps_multiplayer_ext.reflayer[i].phase_hor_chroma_plus8 = GOL_UE(v);
                pps->pps_multiplayer_ext.reflayer[i].phase_ver_chroma_plus8 = GOL_UE(v);
            }
        }
        pps->pps_multiplayer_ext.colour_mapping_enabled_flag = READ_BIT(v);
        if (pps->pps_multiplayer_ext.colour_mapping_enabled_flag) {
            parse_color_mapping_table(v, &pps->pps_multiplayer_ext.color_map);
        }
    }
    if (pps->pps_3d_extension_flag) {
        pps->pps_3d_ext.dlts_present_flag = READ_BIT(v);
        if (pps->pps_3d_ext.dlts_present_flag) {
            pps->pps_3d_ext.pps_depth_layers_minus1 = READ_BITS(v, 6);
            pps->pps_3d_ext.pps_bit_depth_for_depth_layers_minus8 = READ_BITS(v, 4);
            for (int i = 0; i < pps->pps_3d_ext.pps_depth_layers_minus1; i ++) {
                pps->pps_3d_ext.pps_3d_layers[i].dlt_flag = READ_BIT(v);
                if (pps->pps_3d_ext.pps_3d_layers[i].dlt_flag) {
                    pps->pps_3d_ext.pps_3d_layers[i].dlt_pred_flag = READ_BIT(v);
                    if (!pps->pps_3d_ext.pps_3d_layers[i].dlt_pred_flag) {
                        pps->pps_3d_ext.pps_3d_layers[i].dlt_val_flags_present_flag = READ_BIT(v);
                    }
                    if (pps->pps_3d_ext.pps_3d_layers[i].dlt_val_flags_present_flag) {
                        for (int j = 0; j < (1 << (pps->pps_3d_ext.pps_bit_depth_for_depth_layers_minus8 + 8)) - 1; j ++) {
                            pps->pps_3d_ext.pps_3d_layers[i].dlt_value_flag[j] = READ_BIT(v);
                        }
                    } else {
                        //delta_dlt
                        int vl = pps->pps_3d_ext.pps_bit_depth_for_depth_layers_minus8 + 8;
                        pps->pps_3d_ext.pps_3d_layers[i].num_val_delta_dlt = READ_BITS(v, vl);
                        if (pps->pps_3d_ext.pps_3d_layers[i].num_val_delta_dlt > 0) {
                            if (pps->pps_3d_ext.pps_3d_layers[i].num_val_delta_dlt > 1) {
                                pps->pps_3d_ext.pps_3d_layers[i].max_diff = READ_BITS(v, vl);
                            }
                            if (pps->pps_3d_ext.pps_3d_layers[i].num_val_delta_dlt > 2 &&
                                    pps->pps_3d_ext.pps_3d_layers[i].max_diff > 0) {
                                pps->pps_3d_ext.pps_3d_layers[i].min_diff_minus1 = READ_BITS(v, log2ceil(pps->pps_3d_ext.pps_3d_layers[i].max_diff + 1));
                            }
                            pps->pps_3d_ext.pps_3d_layers[i].delta_dlt_val0 = READ_BITS(v, vl);
                            if (pps->pps_3d_ext.pps_3d_layers[i].max_diff > (pps->pps_3d_ext.pps_3d_layers[i].min_diff_minus1 + 1)) {
                                for (int k = 1; k < pps->pps_3d_ext.pps_3d_layers[i].num_val_delta_dlt; k ++) {
                                    pps->pps_3d_ext.pps_3d_layers[i].delta_val_diff_minus_min[k] = READ_BITS(v, log2ceil(pps->pps_3d_ext.pps_3d_layers[i].max_diff - pps->pps_3d_ext.pps_3d_layers[i].min_diff_minus1));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (pps->pps_scc_extension_flag) {
        pps->pps_scc_ext.pps_curr_pic_ref_enabled_flag = READ_BIT(v);
        pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag = READ_BIT(v);
        if (pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag) {
            pps->pps_scc_ext.pps_slice_act_qp_offsets_present_flag = READ_BIT(v);
            pps->pps_scc_ext.pps_act_y_qp_offset_plus5 = GOL_SE(v);
            pps->pps_scc_ext.pps_act_cb_qp_offset_plus5 = GOL_SE(v);
            pps->pps_scc_ext.pps_act_cr_qp_offset_plus3 = GOL_SE(v);
        }
        pps->pps_scc_ext.pps_palette_predictor_initializers_present_flag = READ_BIT(v);
        if (pps->pps_scc_ext.pps_palette_predictor_initializers_present_flag) {
            pps->pps_scc_ext.pps_num_palette_predictor_initializers = GOL_UE(v);
            if (pps->pps_scc_ext.pps_num_palette_predictor_initializers) {
                pps->pps_scc_ext.monochrome_palette_flag = READ_BIT(v);
                pps->pps_scc_ext.luma_bit_depth_entry_minus8 = GOL_UE(v);
                if (!pps->pps_scc_ext.monochrome_palette_flag) {
                    pps->pps_scc_ext.chroma_bit_depth_entry_minus8 = GOL_UE(v);
                }
                int numComps = pps->pps_scc_ext.monochrome_palette_flag? 1 : 3;
                for (int comp = 0; comp < numComps; comp ++) {
                    for (int i = 0; i < pps->pps_scc_ext.pps_num_palette_predictor_initializers; i ++) {
                        if (i == 0) {
                            pps->pps_scc_ext.pps_palette_predictor_initializer[comp][i] = READ_BITS(v, pps->pps_scc_ext.luma_bit_depth_entry_minus8 + 8);
                        } else {
                            pps->pps_scc_ext.pps_palette_predictor_initializer[comp][i] = READ_BITS(v, pps->pps_scc_ext.chroma_bit_depth_entry_minus8 + 8);
                        }
                    }
                }
            }
        }
    }
    if (pps->pps_extension_4bits) {
        while(more_rbsp_data(v)) {
            uint8_t pps_extension_data_flag = READ_BIT(v);
        }
    }
    rbsp_trailing_bits(v);
    bits_vec_free(v);
    return pps;
}

// struct global_st_ref_set_t {
//     int num_short_term_ref_pic_sets;
//     uint32_t *NumNegativePics;
//     uint32_t *NumPositivePics;
//     uint8_t *UsedByCurrPicS0;
//     uint8_t *UsedByCurrPicS1;
// };

static void
parse_st_ref_set(struct bits_vec *v, struct st_ref_pic_set *st, int idx, int num_short_term_ref_pic_sets)
{
    printf("st_ref_set %d, %d\n", idx, num_short_term_ref_pic_sets);
    if (idx != 0) {
        st->inter_ref_pic_set_prediction_flag = READ_BIT(v);
    }
    if (st->inter_ref_pic_set_prediction_flag) {
        if (idx == num_short_term_ref_pic_sets) {
            st->delta_idx_minus1 = GOL_UE(v);
        }
        st->delta_rps_sign = READ_BIT(v);
        st->abs_delta_rps_minus1 = GOL_UE(v);
        int ref_idx = idx - (st->delta_idx_minus1 + 1);
        struct st_ref_pic_set *ref_st = st - idx + ref_idx;
        int num_delta_procs = ref_st->num_negative_pics + ref_st->num_positive_pics;
        for (int j = 0; j < num_delta_procs; j ++) {
            st->ref_used[j].used_by_curr_pic_flag = READ_BIT(v);
            if (st->ref_used[j].used_by_curr_pic_flag) {
                st->ref_used[j].use_delta_flag = READ_BIT(v);
            }
        }
    } else {
        st->num_negative_pics = GOL_UE(v);
        st->num_positive_pics = GOL_UE(v);
        //see 7-63
        // NumNegativePics[idx] = st->num_negative_pics;
        //see 7-64
        // NumPositivePics[idx] = st->num_positive_pics;
        for (int i = 0; i < st->num_negative_pics; i ++) {
            st->delta_poc_s0_minus1[i] = GOL_UE(v);
            st->used_by_curr_pic_s0_flag[i] = READ_BIT(v);
            //see 7-65
            // UsedByCurrPicS0[idx][i] = used_by_curr_pic_s0_flag[i];
        }
        for (int i = 0; i < st->num_positive_pics; i ++) {
            st->delta_poc_s1_minus1[i] = GOL_UE(v);
            st->used_by_curr_pic_s1_flag[i] = READ_BIT(v);
            //see 7-66
            // UsedByCurrPicS1[idx][i] = used_by_curr_pic_s1_flag[i];
        }
    }
}

//see F.7.3.2.2.1
static struct sps *
parse_sps(struct hevc_nalu_header * h, uint8_t *data, uint16_t len, struct vps *vps)
{
    // hexdump(stdout, "sps:", data, len);
    struct bits_vec *v = bits_vec_alloc(data, len, BITS_MSB);
    struct sps *sps = malloc(sizeof(struct sps));
    sps->sps_video_parameter_set_id = READ_BITS(v, 4);
    printf("sps: sps_video_parameter_set_id %d\n", sps->sps_video_parameter_set_id);
    // if nuh_layer_id = 0 , else it means sps_ext_or_max_sub_layers_minus1
    sps->sps_max_sub_layer_minus1 = READ_BITS(v, 3);
    if (h->nuh_layer_id != 0) {
        sps->sps_max_sub_layer_minus1 = (sps->sps_max_sub_layer_minus1 == 7 ) ? vps->vps_max_sub_layers_minus1 : sps->sps_max_sub_layer_minus1;
    }
    int multilayer_ext_sps_flag = (h->nuh_layer_id != 0 && sps->sps_max_sub_layer_minus1 == 7);
    if (!multilayer_ext_sps_flag) {
        sps->sps_temporal_id_nesting_flag = READ_BIT(v);
        printf("sps_max_sub_layer_minus1 %d\n", sps->sps_max_sub_layer_minus1);
        parse_profile_tier_level(v, &sps->sps_profile_tier_level, 1, sps->sps_max_sub_layer_minus1);
    }
    sps->sps_seq_parameter_set_id = GOL_UE(v);
    printf("sps: sps_seq_parameter_set_id %d\n", sps->sps_seq_parameter_set_id);
    if (multilayer_ext_sps_flag) {
        uint8_t update_rep_format_flag = READ_BIT(v);
        if (update_rep_format_flag) {
            sps->sps_rep_format_idx = READ_BITS(v, 8);
        }
    } else {
        sps->chroma_format_idc = GOL_UE(v);
        if (sps->chroma_format_idc == 3) {
            sps->separate_colour_plane_flag = READ_BIT(v);
        }
        printf("chroma_format_idc %d, separate_colour_plane_flag %d\n",
            sps->chroma_format_idc, sps->separate_colour_plane_flag);
        sps->pic_width_in_luma_samples = GOL_UE(v);
        sps->pic_height_in_luma_samples = GOL_UE(v);
        // sps->conformance_window_flag = READ_BIT(v);
        printf("pic_width_in_luma_samples %d, pic_height_in_luma_samples %d\n",
            sps->pic_width_in_luma_samples, sps->pic_height_in_luma_samples);
        if (READ_BIT(v)) {
            sps->conf_win_left_offset = GOL_UE(v);
            sps->conf_win_right_offset = GOL_UE(v);
            sps->conf_win_top_offset = GOL_UE(v);
            sps->conf_win_bottom_offset = GOL_UE(v);
        }
        sps->bit_depth_luma_minus8 = GOL_UE(v);
        sps->bit_depth_chroma_minus8 = GOL_UE(v);
        printf("bit_depth_luma_minus8 %d, bit_depth_chroma_minus8 %d\n",
            sps->bit_depth_luma_minus8, sps->bit_depth_chroma_minus8);
    }

    sps->log2_max_pic_order_cnt_lsb_minus4 = GOL_UE(v);
    
    if (!multilayer_ext_sps_flag) {
        // uint8_t sps_sub_layer_ordering_info_present_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            for (int i = 0; i <= sps->sps_max_sub_layer_minus1; i ++) {
                sps->sps_sublayers[i].sps_max_dec_pic_buffering_minus1 = GOL_UE(v);
                sps->sps_sublayers[i].sps_max_num_reorder_pics = GOL_UE(v);
                sps->sps_sublayers[i].sps_max_latency_increase_plus1 = GOL_UE(v);
            }
        }
    }
    
    sps->log2_min_luma_coding_block_size_minus3 = GOL_UE(v);
    sps->log2_diff_max_min_luma_coding_block_size = GOL_UE(v);
    sps->log2_min_luma_transform_block_size_minus2 = GOL_UE(v);
    sps->log2_diff_max_min_luma_transform_block_size = GOL_UE(v);
    sps->max_transform_hierarchy_depth_inter = GOL_UE(v);
    sps->max_transform_hierarchy_depth_intra = GOL_UE(v);
    
    // sps->scaling_list_enabled_flag = READ_BIT(v);
    // if (sps->scaling_list_enabled_flag) {
    if (READ_BIT(v)) {
        // sps->sps_scaling_list_data_present_flag = READ_BIT(v);
        // if (sps->sps_scaling_list_data_present_flag) {
        uint8_t sps_infer_scaling_list_flag = 0;
        if (multilayer_ext_sps_flag) {
            sps_infer_scaling_list_flag = READ_BIT(v);
        }
        if (sps_infer_scaling_list_flag) {
            sps->sps_scaling_list_ref_layer_id = READ_BITS(v, 6);
        } else {
            if (READ_BIT(v)) {
                sps->list_data = malloc(sizeof(struct scaling_list_data));
                parse_scaling_list_data(v, sps->list_data);
            }
        }
    }
    sps->amp_enabled_flag = READ_BIT(v);
    sps->sample_adaptive_offset_enabled_flag = READ_BIT(v);
    sps->pcm_enabled_flag = READ_BIT(v);
    if (sps->pcm_enabled_flag) {
        sps->pcm_sample_bit_depth_luma_minus1 = READ_BITS(v, 4);
        sps->pcm_sample_bit_depth_chroma_minus1 = READ_BITS(v, 4);
        sps->log2_min_pcm_luma_coding_block_size_minus3 = GOL_UE(v);
        sps->log2_diff_max_min_pcm_luma_coding_block_size = GOL_UE(v);
        sps->pcm_loop_filter_disabled_flag = READ_BIT(v);
    }
    sps->num_short_term_ref_pic_sets = GOL_UE(v);
    if (sps->num_short_term_ref_pic_sets) {
        sps->sps_st_ref = calloc(sps->num_short_term_ref_pic_sets, sizeof(struct st_ref_pic_set));
        for(int i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
            parse_st_ref_set(v, sps->sps_st_ref + i, i, sps->num_short_term_ref_pic_sets);
        }
    }
    sps->long_term_ref_pics_present_flag = READ_BIT(v);
    if (sps->long_term_ref_pics_present_flag) {
        sps->num_long_term_ref_pics_sps = GOL_UE(v);
        sps->lt_ref_pic_poc_lsb_sps = malloc(sps->num_long_term_ref_pics_sps);
        sps->used_by_curr_pic_lt_sps_flag = malloc(sps->num_long_term_ref_pics_sps);
        for (int i = 0; i < sps->num_long_term_ref_pics_sps; i ++) {
            sps->lt_ref_pic_poc_lsb_sps[i] = READ_BITS(v, 8);
            sps->used_by_curr_pic_lt_sps_flag[i] = READ_BIT(v);
        }
    }
    sps->sps_temporal_mvp_enabled_flag = READ_BIT(v);
    sps->strong_intra_smoothing_enabled_flag = READ_BIT(v);
    sps->vui_parameters_present_flag = READ_BIT(v);
    if (sps->vui_parameters_present_flag) {
        sps->vui = malloc(sizeof(struct vui_parameters));
        parse_vui(v, sps->vui, sps->sps_max_sub_layer_minus1);
    }
    sps->sps_extension_present_flag = READ_BIT(v);
    if (sps->sps_extension_present_flag) {
        sps->sps_range_extension_flag = READ_BIT(v);
        sps->sps_multilayer_extension_flag = READ_BIT(v);
        sps->sps_3d_extension_flag = READ_BIT(v);
        sps->sps_scc_extension_flag = READ_BIT(v);
        sps->sps_extension_4bits = READ_BITS(v, 4);
    }
    if (sps->sps_range_extension_flag) {
        sps->sps_range_ext.transform_skip_rotation_enabled_flag = READ_BIT(v);
        sps->sps_range_ext.transform_skip_context_enabled_flag = READ_BIT(v);
        sps->sps_range_ext.implicit_rdpcm_enabled_flag = READ_BIT(v);
        sps->sps_range_ext.explicit_rdpcm_enabled_flag = READ_BIT(v);
        sps->sps_range_ext.extended_precision_processing_flag = READ_BIT(v);
        sps->sps_range_ext.intra_smoothing_disabled_flag = READ_BIT(v);
        sps->sps_range_ext.high_precision_offsets_enabled_flag = READ_BIT(v);
        sps->sps_range_ext.persistent_rice_adaptation_enabled_flag = READ_BIT(v);
        sps->sps_range_ext.cabac_bypass_alignment_enabled_flag = READ_BIT(v);
    }
    if (sps->sps_multilayer_extension_flag) {
        sps->sps_multilayer_ext.inter_view_mv_vert_constraint_flag = READ_BIT(v);
    }
    if (sps->sps_3d_extension_flag) {
        sps->sps_3d_ext.iv_di_mc_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.iv_mv_scal_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.log2_ivmc_sub_pb_size_minus3 = GOL_UE(v);
        sps->sps_3d_ext.iv_res_pred_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.depth_ref_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.vsp_mc_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.dbbp_enabled_flag = READ_BIT(v);

        sps->sps_3d_ext.iv_di_mc_enabled_flag1 = READ_BIT(v);
        sps->sps_3d_ext.iv_mv_scal_enabled_flag1 = READ_BIT(v);
        sps->sps_3d_ext.tex_mc_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.log2_texmc_sub_pb_size_minus3 = GOL_UE(v);
        sps->sps_3d_ext.intra_contour_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.intra_dc_only_wedge_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.cqt_cu_part_pred_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.inter_dc_only_enabled_flag = READ_BIT(v);
        sps->sps_3d_ext.skip_intra_enabled_flag = READ_BIT(v);
    }
    if (sps->sps_scc_extension_flag) {
        sps->sps_scc_ext.sps_curr_pic_ref_enabled_flag = READ_BIT(v);
        sps->sps_scc_ext.palette_mode_enabled_flag = READ_BIT(v);
        if (sps->sps_scc_ext.palette_mode_enabled_flag) {
            sps->sps_scc_ext.palette_max_size = GOL_UE(v);
            sps->sps_scc_ext.delta_palette_max_predictor_size = GOL_UE(v);
            sps->sps_scc_ext.sps_palette_predictor_initializers_present_flag = READ_BIT(v);
            if (sps->sps_scc_ext.sps_palette_predictor_initializers_present_flag) {
                sps->sps_scc_ext.sps_num_palette_predictor_initializers_minus1 = GOL_UE(v);
                int numComps = sps->chroma_format_idc == 0 ? 1: 3;
                for (int comp = 0; comp < numComps; comp++ ) {
                    for (int i = 0; i <= sps->sps_scc_ext.sps_num_palette_predictor_initializers_minus1; i++) {
                        sps->sps_scc_ext.sps_palette_predictor_initializer[comp][i] = READ_BITS(v, 8);  // variable
                    }
                }
            }
        }
        sps->sps_scc_ext.motion_vector_resolution_control_idc = READ_BITS(v, 2);
        sps->sps_scc_ext.intra_boundary_filtering_disabled_flag = READ_BIT(v);
    }
    if (sps->sps_extension_4bits) {
        while(more_rbsp_data(v)) {
            uint8_t sps_extension_data_flag = READ_BIT(v);
        }
    }
    rbsp_trailing_bits(v);
    bits_vec_free(v);
    return sps;
}


// see F7.3.2.1.2
static void
parse_rep_format(struct bits_vec *v, struct vps_rep_format * rep)
{
    rep->pic_width_vps_in_luma_samples = READ_BITS(v, 16);
    rep->pic_height_vps_in_luma_samples = READ_BITS(v, 16);
    rep->chroma_and_bit_depth_vps_present_flag = READ_BIT(v);
    if (rep->chroma_and_bit_depth_vps_present_flag) {
        rep->chroma_format_vps_idc = READ_BITS(v, 2);
        if (rep->chroma_format_vps_idc == 3) {
            rep->separate_colour_plane_vps_flag = READ_BIT(v);
        }
        rep->bit_depth_vps_luma_minus8 = READ_BITS(v, 4);
        rep->bit_depth_vps_chroma_minus8 = READ_BITS(v, 4);
    }
    rep->conformance_window_vps_flag = READ_BIT(v);
    if (rep->conformance_window_vps_flag) {
        rep->conf_win_vps_left_offset = GOL_UE(v);
        rep->conf_win_vps_right_offset = GOL_UE(v);
        rep->conf_win_vps_top_offset = GOL_UE(v);
        rep->conf_win_vps_bottom_offset = GOL_UE(v);
    }
}


static void
parse_dpb_size(struct bits_vec *v, struct dpb_size* d, struct vps* vps, int NumOutputLayerSets,
    int OlsIdxToLsIdx[], int MaxSubLayersInLayerSetMinus1[], int NumLayersInIdList[],
    int **NecessaryLayerFlag, int **LayerSetLayerIdList)
{
    struct vps_extension *vps_ext = vps->vps_ext;
    // const struct vps *vps = CONTAIN_OF(vps_ext, struct vps, vps_ext);
    for (int i = 0; i < NumOutputLayerSets; i ++) {
        int currLsIdx = OlsIdxToLsIdx[i];
        uint8_t sub_layer_flag_info_present_flag = READ_BIT(v);
        for (int j = 0; j <= MaxSubLayersInLayerSetMinus1[currLsIdx]; j ++) {
            uint8_t sub_layer_dpb_info_present_flag = 0;
            if (j > 0 && sub_layer_flag_info_present_flag) {
                sub_layer_dpb_info_present_flag = READ_BIT(v);
            }
            if (sub_layer_dpb_info_present_flag) {
                for (int k = 0; k < NumLayersInIdList[currLsIdx]; k++ ) {
                    if (NecessaryLayerFlag[i][k] && (vps->vps_base_layer_internal_flag ||
                        (LayerSetLayerIdList[currLsIdx][k] != 0 ))) {
                        // d->max_vps_dec_pic_buffering_minus1[i][k][j] = GOL_UE(v);
                        GOL_UE(v);
                    }
                }
                d->max_vps_num_reorder_pics[i][j] = GOL_UE(v);
                d->max_vps_latency_increase_plus1[i][j] = GOL_UE(v);
            }
        }
    }
}

static void
parse_video_signal_info(struct bits_vec *v, struct video_signal_info* vsi)
{
    vsi->video_vps_format = READ_BITS(v, 3);
    vsi->video_full_range_vps_flag = READ_BIT(v);
    vsi->colour_primaries_vps = READ_BITS(v, 8);
    vsi->transfer_characteristics_vps = READ_BITS(v, 8);
    vsi->matrix_coeffs_vps = READ_BITS(v, 8);
}


static void
parse_vps_vui_bsp_hrd_params(struct bits_vec *v, struct vps_vui_bsp_hrd_params *hrd,
        int vps_num_hrd_parameters, int NumOutputLayerSets, int *NumLayersInIdList,
        int* OlsIdxToLsIdx, int *MaxSubLayersInLayerSetMinus1, int SubPicHrdFlag)
{
    hrd->vps_num_add_hrd_params = GOL_UE(v);
    hrd->vps_vui_bsp_hrd = malloc(sizeof(struct hrd_parameters)*hrd->vps_num_add_hrd_params);
    for (int i = vps_num_hrd_parameters; i < vps_num_hrd_parameters + hrd->vps_num_add_hrd_params; i++) {
        uint8_t cprms_add_present_flag = 0;
        if (i > 0) {
            cprms_add_present_flag = READ_BIT(v);
        }
        uint32_t num_sub_layer_hrd_minus1 = GOL_UE(v);
        parse_hrd_parameters(v, hrd->vps_vui_bsp_hrd + i, cprms_add_present_flag,
                num_sub_layer_hrd_minus1);
    }
    if (hrd->vps_num_add_hrd_params + vps_num_hrd_parameters > 0) {
        hrd->num_partitions_in_scheme_minus1 = malloc(sizeof(uint32_t *)*NumOutputLayerSets);
        for (int h = 1; h < NumOutputLayerSets; h ++) {
            uint32_t num_signalled_partitioning_schemes = GOL_UE(v);
            hrd->num_partitions_in_scheme_minus1[h] = malloc(sizeof(uint32_t) * num_signalled_partitioning_schemes);
            for (int j = 1; j < num_signalled_partitioning_schemes; j++) {
                hrd->num_partitions_in_scheme_minus1[h][j] = GOL_UE(v);
                for(int k = 0; k <= hrd->num_partitions_in_scheme_minus1[h][j]; k++ ) {
                    for (int r = 0; r < NumLayersInIdList[OlsIdxToLsIdx[h]]; r++ ) {
                        // layer_included_in_partition_flag[h][j][k][r] = READ_BIT(v);
                        SKIP_BITS(v, 1);
                    }
                }
            }
            uint32_t BpBitRate[64][2][2][2][2];
            uint32_t BpbSize[64][2][2][2][2];
            for (int i = 0; i < num_signalled_partitioning_schemes + 1; i ++) {
                for(int t = 0; t <= MaxSubLayersInLayerSetMinus1[OlsIdxToLsIdx[h]]; t++ ) {
                    uint32_t num_bsp_schedules_minus1 = GOL_UE(v);
                    for (int j = 0; j < num_bsp_schedules_minus1; j ++) {
                        for (int k =0; k < hrd->num_partitions_in_scheme_minus1[h][i]; j ++) {
                            if (vps_num_hrd_parameters + hrd->vps_num_add_hrd_params > 1) {
                                // bsp_hrd_idx[h][i][t][j][k] = 
                                uint32_t bsIdx = READ_BITS(v, log2ceil(vps_num_hrd_parameters + hrd->vps_num_add_hrd_params));
                                uint32_t brDu = ((hrd->vps_vui_bsp_hrd->hrd_layer[t].nal_hrd->cpb[bsIdx].bit_rate_du_value_minus1 + 1) << (6 + hrd->vps_vui_bsp_hrd->bit_rate_scale));
                                uint32_t brPu = ((hrd->vps_vui_bsp_hrd->hrd_layer[t].nal_hrd->cpb[bsIdx].bit_rate_value_minus1 + 1) << (6 + hrd->vps_vui_bsp_hrd->bit_rate_scale));
                                uint32_t cpbSizeDu = ((hrd->vps_vui_bsp_hrd->hrd_layer[t].nal_hrd->cpb[bsIdx].cpb_size_du_value_minus1 + 1) << (4 + hrd->vps_vui_bsp_hrd->cpb_size_du_scale));
                                uint32_t cpbSizePu = ((hrd->vps_vui_bsp_hrd->hrd_layer[t].nal_hrd->cpb[bsIdx].cpb_size_value_minus1 + 1) << ( 4 + hrd->vps_vui_bsp_hrd->cpb_size_scale));
                                BpBitRate[h][i][t][j][k] = SubPicHrdFlag ? brDu : brPu;
                                BpbSize[h][i][t][j][k] = SubPicHrdFlag? cpbSizeDu : cpbSizePu;
                            }
                            // bsp_sched_idx[][][][] = GOL_UE(v);
                            GOL_UE(v);
                        }
                    }
                }
            }
        }
    }
}

// int cal_bitrate_bps(int x)
// {
//     return (x & (1<< 14 - 1) * 
// }

static void
parse_vps_vui(struct bits_vec *v, struct vps_vui* vui, struct vps *vps, int NumLayerSets,
        int MaxSubLayersInLayerSetMinus1[], int NumOutputLayerSets, int* NumLayersInIdList,
        int *OlsIdxToLsIdx, int SubPicHrdFlag, int* NumDirectRefLayers, int **IdDirectRefLayer)
{
    struct vps_extension* vps_ext = vps->vps_ext;
    // struct vps *vps = CONTAIN_OF(vps_ext, struct vps, vps_ext);
    int MaxLayersMinus1 = MIN(62, vps->vps_max_layers_minus1);

    // uint8_t cross_layer_pic_type_aligned_flag = READ_BIT(v);
    uint8_t cross_layer_irap_aligned_flag = 0;
    if (!READ_BIT(v)) {
        cross_layer_irap_aligned_flag = READ_BIT(v);
    }
    if (cross_layer_irap_aligned_flag) {
        vui->all_layers_idr_aligned_flag = READ_BIT(v);
    }
    vui->bit_rate_present_vps_flag = READ_BIT(v);
    vui->pic_rate_present_vps_flag = READ_BIT(v);
    if (vui->bit_rate_present_vps_flag || vui->pic_rate_present_vps_flag) {
        vui->avg_bit_rate = malloc(sizeof(uint16_t *) * NumLayerSets);
        vui->max_bit_rate = malloc(sizeof(uint16_t *) * NumLayerSets);
        vui->constant_pic_rate_idc = malloc(sizeof(uint16_t *) * NumLayerSets);
        vui->avg_pic_rate = malloc(sizeof(uint16_t *) * NumLayerSets);
        for (int i = vps->vps_base_layer_internal_flag ? 0 : 1; i < NumLayerSets; i++) {
            vui->avg_bit_rate[i] = malloc(2 * (MaxSubLayersInLayerSetMinus1[i] + 1));
            vui->max_bit_rate[i] = malloc(2 * (MaxSubLayersInLayerSetMinus1[i] + 1));
            vui->constant_pic_rate_idc[i] = malloc(1 * (MaxSubLayersInLayerSetMinus1[i] + 1));
            vui->avg_pic_rate[i] = malloc(2 * (MaxSubLayersInLayerSetMinus1[i] + 1));
            for(int j = 0; j <= MaxSubLayersInLayerSetMinus1[i]; j++ ) {
                uint8_t bit_rate_present_flag = 0;
                uint8_t pic_rate_present_flag = 0;
                if (vui->bit_rate_present_vps_flag) {
                    bit_rate_present_flag = READ_BIT(v);
                }
                if (vui->pic_rate_present_vps_flag) {
                    pic_rate_present_flag = READ_BIT(v);
                }
                if (bit_rate_present_flag) {
                    vui->avg_bit_rate[i][j] = READ_BITS(v, 16);
                    vui->max_bit_rate[i][j] = READ_BITS(v, 16);
                }
                if (pic_rate_present_flag) {
                    vui->constant_pic_rate_idc[i][j] = READ_BITS(v, 2);
                    vui->avg_pic_rate[i][j] = READ_BITS(v, 16);
                }
                
            }
        }
    }
    uint8_t video_signal_info_idx_present_flag = READ_BIT(v);
    if (video_signal_info_idx_present_flag) {
        vui->vps_num_video_signal_info_minus1 = READ_BITS(v, 4);
    }
    vui->vps_video_signal_info = malloc(sizeof(struct video_signal_info) * vui->vps_num_video_signal_info_minus1);
    for (int i = 0; i <= vui->vps_num_video_signal_info_minus1; i++ ) {
        parse_video_signal_info(v, vui->vps_video_signal_info + i);
    }
    if (video_signal_info_idx_present_flag && vui->vps_num_video_signal_info_minus1 > 0 ) {
        vui->vps_video_signal_info_idx = malloc(MaxLayersMinus1);
        for(int i = vps->vps_base_layer_internal_flag ? 0 : 1; i <= MaxLayersMinus1; i++ ){
            vui->vps_video_signal_info_idx[i] = READ_BITS(v, 4);
        }
    }
    // uint8_t tiles_not_in_use_flag = READ_BIT(v);
    if (!READ_BIT(v)) {
        uint8_t *tiles_in_use_flag = malloc(MaxLayersMinus1);

        vui->loop_filter_not_across_tiles_flag = malloc(MaxLayersMinus1);
        for (int i = vps->vps_base_layer_internal_flag ? 0 : 1; i <= MaxLayersMinus1; i++ ) {
            tiles_in_use_flag[i] = READ_BIT(v);
            if(tiles_in_use_flag[i]) {
                vui->loop_filter_not_across_tiles_flag[i] = READ_BIT(v);
            }
        }
        vui->tile_boundaries_aligned_flag = malloc(sizeof(uint8_t *) * MaxLayersMinus1);
        for (int i = vps->vps_base_layer_internal_flag ? 1 : 2; i <= MaxLayersMinus1; i++) {
            vui->tile_boundaries_aligned_flag[i] = malloc(vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]]);
            for (int j = 0; j < vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]]; j++ ) {
                int layerIdx = vps->LayerIdxInVps[IdDirectRefLayer[vps_ext->layer_id_in_nuh[i]][j]];
                if( tiles_in_use_flag[i] && tiles_in_use_flag[layerIdx]) {
                    vui->tile_boundaries_aligned_flag[i][j] = READ_BIT(v);
                }
            }
        }
        free(tiles_in_use_flag);
    }
    // wpp_not_in_use_flag = READ_BIT(v);
    if (!READ_BIT(v)) {
        vui->wpp_in_use_flag = malloc(MaxLayersMinus1);
        for (int i = vps->vps_base_layer_internal_flag ? 0 : 1; i <= MaxLayersMinus1; i++ ) {
            vui->wpp_in_use_flag[i] = READ_BIT(v);
        }
    }
    vui->single_layer_for_non_irap_flag = READ_BIT(v);
    vui->higher_layer_irap_skip_flag = READ_BIT(v);
    vui->ilp_restricted_ref_layers_flag = READ_BIT(v);
    if (vui->ilp_restricted_ref_layers_flag) {
        vui->min_horizontal_ctu_offset_plus1 = malloc(sizeof(uint32_t *) * MaxLayersMinus1);
        for (int i = 0; i < MaxLayersMinus1; i++) {
            vui->min_horizontal_ctu_offset_plus1[i] = malloc(sizeof(uint32_t) * vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]]); 
            for (int j = 0; j < vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]]; j++) {
                if( vps->vps_base_layer_internal_flag || IdDirectRefLayer[vps_ext->layer_id_in_nuh[i]][j] > 0 ) {
                    // vui->min_spatial_segment_offset_plus1[i][j] = GOL_UE(v);
                    if (GOL_UE(v)) {
                        // ctu_based_offset_enabled_flag[i][j] = READ_BIT(v);
                        if (READ_BIT(v)) {
                            vui->min_horizontal_ctu_offset_plus1[i][j] = GOL_UE(v);
                        }
                    }
                }
            }
        }
    }
    // uint8_t vps_vui_bsp_hrd_present_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        vui->vui_hrd = malloc(sizeof(struct vps_vui_bsp_hrd_params));
        parse_vps_vui_bsp_hrd_params(v, vui->vui_hrd, vps->vps_timing_info->vps_num_hrd_parameters,
            NumOutputLayerSets, NumLayersInIdList, OlsIdxToLsIdx, MaxSubLayersInLayerSetMinus1, SubPicHrdFlag);
    }
    vui->base_layer_parameter_set_compatibility_flag = malloc(MaxLayersMinus1);
    for (int i = 0; i < MaxLayersMinus1; i++) {
        if (vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]] == 0) {
            vui->base_layer_parameter_set_compatibility_flag[i] = READ_BIT(v);
        }
    }
}

//video parameter set F.7.3.2.1 
static struct vps *
parse_vps(struct hevc_nalu_header *headr, uint8_t *data, uint16_t len)
{
    printf("vps: len %d\n", len);
    // hexdump(stdout, "vps: ", data, len);
    uint8_t* p = data;
    struct vps *vps = malloc(sizeof(struct vps));
    struct bits_vec *v = bits_vec_alloc(data, len, BITS_MSB);
    vps->vps_video_parameter_set_id = READ_BITS(v, 4);
    printf("vps id %d\n", vps->vps_video_parameter_set_id);
    vps->vps_base_layer_internal_flag = READ_BIT(v);
    vps->vps_base_layer_available_flag = READ_BIT(v);
    vps->vps_max_layers_minus1 = READ_BITS(v, 6);
    int MaxLayersMinus1 = MIN(62, vps->vps_max_layers_minus1);
    vps->vps_max_sub_layers_minus1 = READ_BITS(v, 3);
    vps->vps_temporal_id_nesting_flag = READ_BIT(v);

    uint16_t vps_reserved_0xffff_16bits = READ_BITS(v, 16);
    if (vps_reserved_0xffff_16bits != 0xFFFF) {
        printf("reserved bits 0x%x\n", vps_reserved_0xffff_16bits);
    }

    parse_profile_tier_level(v, &vps->vps_profile_tier_level, 1, vps->vps_max_sub_layers_minus1);
    printf("vps_max_sub_layers_minus1 %d\n", vps->vps_max_sub_layers_minus1);
    vps->vps_sub_layer_ordering_info_present_flag = READ_BIT(v);
    if (vps->vps_sub_layer_ordering_info_present_flag) {
        for (int i = 0; i <= vps->vps_max_sub_layers_minus1; i ++) {
            vps->vps_sublayers[i].vps_max_dec_pic_buffering_minus1 = GOL_UE(v);
            vps->vps_sublayers[i].vps_max_num_reorder_pics = GOL_UE(v);
            vps->vps_sublayers[i].vps_max_latency_increase_plus1 = GOL_UE(v);
        }
    }
    vps->vps_max_layer_id = READ_BITS(v, 6);
    vps->vps_num_layer_sets_minus1 = GOL_UE(v);
    int LayerSetLayerIdList[1024][64];
    int NumLayersInIdList[64];
    printf("vps_num_layer_sets_minus1 %d\n", vps->vps_num_layer_sets_minus1);
    vps->layer_id_included_flag = malloc(sizeof(uint64_t) * (vps->vps_num_layer_sets_minus1 + 1));
    for (int i = 1; i <= vps->vps_num_layer_sets_minus1; i ++) {
        int n = 0;
        for (int j = 0; j <= vps->vps_max_layer_id; j ++) {
            uint8_t layer_id_included_flag = READ_BIT(v);
            vps->layer_id_included_flag[i] |= (layer_id_included_flag << j);
            //(7-3)
            if (layer_id_included_flag) {
                LayerSetLayerIdList[i][n++] = j; 
            }
        }
        NumLayersInIdList[i] = n;
    }
    // vps->vps_timing_info_present_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        struct vps_timing_info *vps_tim = malloc(sizeof(struct vps_timing_info));
        vps->vps_timing_info = vps_tim;
        vps_tim->vps_num_units_in_tick = READ_BITS(v, 32);
        vps_tim->vps_time_scale = READ_BITS(v, 32);
        // vps_tim->vps_poc_proportional_to_timing_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            vps_tim->vps_num_ticks_poc_diff_one_minus1 = GOL_UE(v);
        }
        vps_tim->vps_num_hrd_parameters = GOL_UE(v);
        printf("vps hrd num %d\n", vps_tim->vps_num_hrd_parameters);
        for (int i = 0; i < vps_tim->vps_num_hrd_parameters; i ++) {
            vps_tim->hrd_layer_set_idx[i] =  GOL_UE(v);
            if (i > 0) {
                vps_tim->cprms_present_flag[i] = READ_BIT(v);
            }
            vps_tim->vps_hrd_para[i] = malloc(sizeof(struct hrd_parameters));
            parse_hrd_parameters(v, vps_tim->vps_hrd_para[i], vps_tim->cprms_present_flag[i], vps->vps_max_sub_layers_minus1);
        }
    }

    // vps->vps_extension_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        while (!BYTE_ALIGNED(v)) {
            SKIP_BITS(v, 1);
        }
        vps->vps_ext = malloc(sizeof(struct vps_extension));

        if (vps->vps_max_layers_minus1 > 0 && vps->vps_base_layer_internal_flag) {
            vps->vps_ext->vps_ext_profile_tier_level = malloc(sizeof(struct profile_tier_level));
            parse_profile_tier_level(v, vps->vps_ext->vps_ext_profile_tier_level, 0, vps->vps_max_sub_layers_minus1);
        }
        vps->vps_ext->splitting_flag = READ_BIT(v);
        int NumScalabilityTypes = 0;
        for (int i = 0; i < 16; i ++) {
            /* dimensions:
               index 0: texture or depth,  depthLayerFlag
               index 1: multiview,         viewoderIdx
               index 2: spatial/quality scalability,  dependencyId
               index 3: auxiliary,          auxid
               index 4-15: reserved
            */
            uint8_t scalability_mask_flag = READ_BIT(v);
            vps->vps_ext->scalability_mask_flag |= (scalability_mask_flag << i);
            NumScalabilityTypes += scalability_mask_flag;
        }
        vps->vps_ext->dimension_id_len_minus1 = malloc(NumScalabilityTypes - vps->vps_ext->splitting_flag);
        for (int j = 0; j < NumScalabilityTypes - vps->vps_ext->splitting_flag; j ++) {
            vps->vps_ext->dimension_id_len_minus1[j] = READ_BITS(v, 3);
        }
        // vps->vps_ext->vps_nuh_layer_id_present_flag = READ_BIT(v);
        uint8_t vps_nuh_layer_id_present_flag = READ_BIT(v);
        if (vps_nuh_layer_id_present_flag) {
            vps->vps_ext->layer_id_in_nuh = malloc(MaxLayersMinus1+1);
        }
        if (!vps->vps_ext->splitting_flag) {
            vps->vps_ext->dimension_id = malloc(sizeof(uint8_t *) * (MaxLayersMinus1+1));
        }
        for (int i = 1; i <= MaxLayersMinus1; i ++) {
            if(vps_nuh_layer_id_present_flag) {
                vps->vps_ext->layer_id_in_nuh[i] = READ_BITS(v, 6);
                vps->LayerIdxInVps[vps->vps_ext->layer_id_in_nuh[i]] = i;
            }
            if (!vps->vps_ext->splitting_flag) {
                vps->vps_ext->dimension_id[i] = malloc(NumScalabilityTypes);
                for (int j = 0; j < NumScalabilityTypes; j ++) {
                    vps->vps_ext->dimension_id[i][j] = READ_BITS(v, vps->vps_ext->dimension_id_len_minus1[j]+1);
                }
            }
        }


        int SubPicHrdFlag = 0;
        int NumViews = 1;
        uint8_t ScalabilityId[64][16];
        uint8_t DepthLayerFlag[64];
        uint8_t ViewOrderIdx[64];
        uint8_t DependencyId[64];
        uint8_t AuxId[64];

        for (int i = 0; i <= MaxLayersMinus1; i ++) {
            uint8_t lID = vps->vps_ext->layer_id_in_nuh[i];
            for (int smIdx = 0, j = 0; smIdx < 16; smIdx ++) {
                if (vps->vps_ext->scalability_mask_flag &(1 <<smIdx)) {
                    ScalabilityId[i][smIdx] = vps->vps_ext->dimension_id[i][j++];
                } else {
                    ScalabilityId[i][smIdx] = 0;
                }
            }
            DepthLayerFlag[lID] = ScalabilityId[i][0];
            ViewOrderIdx[lID] = ScalabilityId[i][1];
            DependencyId[lID] = ScalabilityId[i][2];
            AuxId[lID] = ScalabilityId[i][3];
            if (i > 0) {
                int newViewFlag = 1;
                for (int j = 0; j < i; j ++) {
                    if (ViewOrderIdx[lID] == ViewOrderIdx[vps->vps_ext->layer_id_in_nuh[j]]) {
                        newViewFlag = 0;
                    }
                }
                NumViews += newViewFlag;
            }

        }
        vps->vps_ext->view_id_len = READ_BITS(v, 4);
        if (vps->vps_ext->view_id_len) {
            for (int i = 0 ; i < NumViews; i ++) {
                vps->vps_ext->view_id_val[i] = READ_BITS(v, vps->vps_ext->view_id_len);
            }
        }
        for (int i = 1; i <= MaxLayersMinus1; i ++) {
            for (int j = 0; j < i ; j ++) {
                vps->vps_ext->direct_dependency_flag[i] |= (READ_BIT(v) << j);
            }
        }

        int DependencyFlag[64][64];
        // (F-4)
        for (int i = 0; i <= MaxLayersMinus1; i++) {
            for (int j = 0; j <= MaxLayersMinus1; j++ ) {
                DependencyFlag[i][j] = ((vps->vps_ext->direct_dependency_flag[i] >> j) & 0x1);
                for (int k = 0; k < i; k++ ) {
                    if(((vps->vps_ext->direct_dependency_flag[i]>>k) & 0x1) && DependencyFlag[k][j]) {
                        DependencyFlag[i][j] = 1;
                    }
                }
            }
        }

        int NumRefLayers[64];
        int NumPredictedLayers[64];
        int IdDirectRefLayer[64][64];
        int IdRefLayer[64][64];
        int IdPredictedLayer[64][64];


        // (F-5)
        for (int i = 0; i <= MaxLayersMinus1; i++ ) {
            int iNuhLId = vps->vps_ext->layer_id_in_nuh[i];
            int d = 0, r = 0, p = 0;
            for (int j = 0; j <= MaxLayersMinus1; j++ ) {
                int jNuhLid = vps->vps_ext->layer_id_in_nuh[j];
                if ((vps->vps_ext->direct_dependency_flag[i] >> j) & 0x01 ) {
                    IdDirectRefLayer[iNuhLId][ d++] = jNuhLid;
                }
                if (DependencyFlag[i][j]) {
                    IdRefLayer[iNuhLId][r++] = jNuhLid;
                }
                if (DependencyFlag[j][i]) {
                    IdPredictedLayer[iNuhLId][p++] = jNuhLid;
                }
            }
            vps->NumDirectRefLayers[iNuhLId] = d;
            NumRefLayers[iNuhLId] = r;
            NumPredictedLayers[iNuhLId ] = p;
            //see I-8
            vps->NumRefListLayers[ iNuhLId ] = 0;
            for(int j = 0; j < vps->NumDirectRefLayers[ iNuhLId ]; j++ ) {
                int jNuhLId = IdDirectRefLayer[iNuhLId][j];
                if( DepthLayerFlag[ iNuhLId ] == DepthLayerFlag[ jNuhLId ] ) {
                    vps->IdRefListLayer[ iNuhLId ][ vps->NumRefListLayers[iNuhLId]++ ] = jNuhLId;
                }
            }
        }

        // (F-6)
        int TreePartitionLayerIdList[64][64];
        int NumLayersInTreePartition[64];
        int layerIdInListFlag[64] = {0};
        int k = 0;
        for (int i = 0; i <= MaxLayersMinus1; i++) {
            int iNuhLId = vps->vps_ext->layer_id_in_nuh[i];
            if( vps->NumDirectRefLayers[iNuhLId] == 0 ) {
                TreePartitionLayerIdList[k][0] = iNuhLId;
                int h = 1;
                for(int j = 0; j < NumPredictedLayers[iNuhLId]; j++) {
                    int predLId = IdPredictedLayer[iNuhLId][j];
                    if(!layerIdInListFlag[ predLId ]) {
                        TreePartitionLayerIdList[k][h++] = predLId;
                        layerIdInListFlag[ predLId ] = 1;
                    }
                }
                NumLayersInTreePartition[k++] = h;
            }
        }
        int NumIndependentLayers = k;

        if (NumIndependentLayers > 1) {
            vps->vps_ext->num_add_layer_sets = GOL_UE(v);
        }
        // (F-7)
        int NumLayerSets = vps->vps_num_layer_sets_minus1 + 1 + vps->vps_ext->num_add_layer_sets;
        
        int NumLayersInIdList[1024];
        vps->vps_ext->highest_layer_idx_plus1 = malloc(sizeof(uint16_t *) * vps->vps_ext->num_add_layer_sets);
        for (int i = 0; i < vps->vps_ext->num_add_layer_sets; i ++) {
            vps->vps_ext->highest_layer_idx_plus1[i] = malloc(sizeof(uint16_t) * NumIndependentLayers);
            for (int j = 1; j < NumIndependentLayers; j ++) {
                vps->vps_ext->highest_layer_idx_plus1[i][j] = READ_BITS(v, log2ceil(NumLayersInTreePartition[j]+1));
            }
            // (F-9)
            int layerNum = 0;
            int lsIdx = vps->vps_num_layer_sets_minus1 + 1 + i;
            for(int treeIdx = 1; treeIdx < NumIndependentLayers; treeIdx++ ) {
                for(int layerCnt = 0; layerCnt < vps->vps_ext->highest_layer_idx_plus1[i][treeIdx]; layerCnt++) {
                    LayerSetLayerIdList[lsIdx][layerNum++] = TreePartitionLayerIdList[treeIdx][layerCnt];
                }
            }
            NumLayersInIdList[lsIdx] = layerNum;
        }

        // uint8_t vps_sub_layers_max_minus1_present_flag = READ_BIT(v);
        // if (vps_sub_layers_max_minus1_present_flag) {
        if (READ_BIT(v)) {
            vps->vps_ext->sub_layers_vps_max_minus1 = malloc(MaxLayersMinus1);
            for (int i = 0; i < MaxLayersMinus1; i ++) {
                vps->vps_ext->sub_layers_vps_max_minus1[i] = READ_BITS(v, 3);
            }
        }

        //(F-10)
        int MaxSubLayersInLayerSetMinus1[64];
        for (int i = 0; i < NumLayerSets; i++ ) {
            int maxSlMinus1 = 0;
            for(int k = 0; k < NumLayersInIdList[i]; k++ ) {
                int lId = LayerSetLayerIdList[i][k];
                maxSlMinus1 = MAX(maxSlMinus1, vps->vps_ext->sub_layers_vps_max_minus1[vps->LayerIdxInVps[lId]]);
                MaxSubLayersInLayerSetMinus1[i] = maxSlMinus1;
            }
        }

        // uint8_t max_tid_ref_present_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            for (int i = 0; i < MaxLayersMinus1 - 1; i ++) {
                for (int j = i + 1; j <= MaxLayersMinus1; j ++) {
                    if ((vps->vps_ext->direct_dependency_flag[i]>>j) & 0x1) {
                        vps->vps_ext->max_tid_il_ref_pics_plus1[i][j] = READ_BITS(v, 3);
                    }
                }
            }
        }
        vps->vps_ext->default_ref_layers_active_flag = READ_BIT(v);
        vps->vps_ext->vps_num_profile_tier_level_minus1 = GOL_UE(v);
        for (int i = vps->vps_base_layer_internal_flag ? 2 : 1;
            i <= vps->vps_ext->vps_num_profile_tier_level_minus1; i++ ) {
            uint8_t vps_profile_present_flag = READ_BIT(v);
            parse_profile_tier_level(v, vps->vps_ext->vps_ext_profile_tier_level, vps_profile_present_flag, vps->vps_max_sub_layers_minus1);
        }
        if( NumLayerSets > 1 ) {
            vps->vps_ext->num_add_olss = GOL_UE(v);
            vps->vps_ext->default_output_layer_idc = READ_BITS(v, 2);
        }
        int defaultOutputLayerIdc = MIN(vps->vps_ext->default_output_layer_idc, 2);
        int NumOutputLayerSets = vps->vps_ext->num_add_olss + NumLayerSets;
        int OlsIdxToLsIdx[64];
        int NumOutputLayersInOutputLayerSet[64];
        int OlsHighestOutputLayerId[1024];
        int NecessaryLayerFlag[64][64];
        int NumNecessaryLayers[64];
        int LayerSetLayerIdList[64][64];

        vps->vps_ext->layer_set_idx_for_ols_minus1 = malloc(sizeof(uint16_t) * NumOutputLayerSets);
        vps->vps_ext->output_layer_flag = malloc(sizeof(uint8_t *) * NumOutputLayerSets);
        vps->vps_ext->profile_tier_level_idx = malloc(sizeof(uint16_t *) * NumOutputLayerSets);
        vps->vps_ext->alt_output_layer_flag = malloc(NumOutputLayerSets);
        for (int i = 1; i < NumOutputLayerSets; i++ ) {
            if( NumLayerSets > 2 && i >= NumLayerSets ) {
                vps->vps_ext->layer_set_idx_for_ols_minus1[i] = READ_BITS(v, log2ceil(NumLayerSets-1));
                // OlsIdxToLsIdx[i] = (i < NumLayerSets) ? i : (layer_set_idx_for_ols_minus1[i] + 1);
            }
            // (F-11)
            OlsIdxToLsIdx[i] = (i < NumLayerSets) ? i : (vps->vps_ext->layer_set_idx_for_ols_minus1[i] + 1); 
            if (i > vps->vps_num_layer_sets_minus1 || defaultOutputLayerIdc == 2) {
                vps->vps_ext->output_layer_flag[i] = malloc(NumLayersInIdList[OlsIdxToLsIdx[i]]);
                for (int j = 0; j < NumLayersInIdList[OlsIdxToLsIdx[i]]; j++) {
                    vps->vps_ext->output_layer_flag[i][j] = READ_BIT(v);
                }
            }
            // (F-12)
            if (i >= ((defaultOutputLayerIdc == 2) ? 0: (vps->vps_num_layer_sets_minus1 + 1))) {
                NumOutputLayersInOutputLayerSet[i] = 0;
                for (int j = 0; j < NumLayersInIdList[OlsIdxToLsIdx[i]]; j++ ) {
                    NumOutputLayersInOutputLayerSet[i] += vps->vps_ext->output_layer_flag[i][j];
                    // OutputLayerFlag[i][j] = vps->vps_ext->output_layer_flag[i][j];
                    if (vps->vps_ext->output_layer_flag[i][j]) {
                        OlsHighestOutputLayerId[i] = LayerSetLayerIdList[OlsIdxToLsIdx[i]][j];
                    }
                }
            }
            //(F-13)
            for (int olsIdx = 0; olsIdx < NumOutputLayerSets; olsIdx++ ) {
                int lsIdx = OlsIdxToLsIdx[olsIdx];
                for (int lsLayerIdx = 0; lsLayerIdx < NumLayersInIdList[lsIdx]; lsLayerIdx++) {
                    NecessaryLayerFlag[olsIdx][lsLayerIdx ] = 0;
                }
                for (int lsLayerIdx = 0; lsLayerIdx < NumLayersInIdList[lsIdx]; lsLayerIdx++ ) {
                    if (vps->vps_ext->output_layer_flag[olsIdx][lsLayerIdx]) {
                        NecessaryLayerFlag[olsIdx][lsLayerIdx] = 1;
                        int currLayerId = LayerSetLayerIdList[lsIdx][lsLayerIdx];
                        for (int rLsLayerIdx = 0; rLsLayerIdx < lsLayerIdx; rLsLayerIdx++) {
                            int refLayerId = LayerSetLayerIdList[lsIdx][rLsLayerIdx];
                            if (DependencyFlag[vps->LayerIdxInVps[currLayerId]][vps->LayerIdxInVps[refLayerId]]) {
                                NecessaryLayerFlag[olsIdx][rLsLayerIdx] = 1;
                            }
                        }
                    }
                }
                NumNecessaryLayers[olsIdx] = 0;
                for (int lsLayerIdx = 0; lsLayerIdx < NumLayersInIdList[lsIdx]; lsLayerIdx++) {
                    NumNecessaryLayers[olsIdx] += NecessaryLayerFlag[olsIdx][lsLayerIdx];
                }
            }

            vps->vps_ext->profile_tier_level_idx[i] = malloc(NumLayersInIdList[OlsIdxToLsIdx[i]] * sizeof(uint16_t));
            for (int j = 0; j < NumLayersInIdList[OlsIdxToLsIdx[i]]; j++ ) {
                if (NecessaryLayerFlag[i][j] && vps->vps_ext->vps_num_profile_tier_level_minus1 > 0) {
                    vps->vps_ext->profile_tier_level_idx[i][j] = READ_BITS(v, log2ceil(vps->vps_ext->vps_num_profile_tier_level_minus1 + 1));
                }
            }
            if( NumOutputLayersInOutputLayerSet[i] == 1 && vps->NumDirectRefLayers[OlsHighestOutputLayerId[i]] > 0 ) {
                vps->vps_ext->alt_output_layer_flag[i] = READ_BIT(v);
            }
        }
        vps->vps_ext->vps_num_rep_formats_minus1 = GOL_UE(v);
        vps->vps_ext->vps_rep_format = malloc(sizeof(struct vps_rep_format) * vps->vps_ext->vps_num_rep_formats_minus1);
        for(int i = 0; i <= vps->vps_ext->vps_num_rep_formats_minus1; i++ ) {
            parse_rep_format(v, vps->vps_ext->vps_rep_format + i);
        }
        uint8_t rep_format_idx_present_flag = 0;
        if (vps->vps_ext->vps_num_rep_formats_minus1 > 0 ) {
            rep_format_idx_present_flag = READ_BIT(v);
        }
        if (rep_format_idx_present_flag) {
            vps->vps_ext->vps_rep_format_idx = malloc( sizeof(uint16_t) *(MaxLayersMinus1 +1));
            for (int i = vps->vps_base_layer_internal_flag ? 1 : 0; i <= MaxLayersMinus1; i++ ) {
                vps->vps_ext->vps_rep_format_idx[i] = READ_BITS(v, log2ceil(vps->vps_ext->vps_num_rep_formats_minus1 + 1));
            }
        }
        vps->vps_ext->max_one_active_ref_layer_flag = READ_BIT(v);
        vps->vps_ext->vps_poc_lsb_aligned_flag = READ_BIT(v);
        vps->vps_ext->poc_lsb_not_present_flag = malloc(MaxLayersMinus1+1);
        for (int i = 1; i <= MaxLayersMinus1; i++) {
            if (vps->NumDirectRefLayers[vps->vps_ext->layer_id_in_nuh[i]] == 0 ) {
                vps->vps_ext->poc_lsb_not_present_flag[i] = READ_BIT(v);
            }
        }

        vps->vps_ext->dpb_size = malloc(sizeof(struct dpb_size));
        parse_dpb_size(v, vps->vps_ext->dpb_size, vps, NumOutputLayerSets, OlsIdxToLsIdx, MaxSubLayersInLayerSetMinus1, NumLayersInIdList, NecessaryLayerFlag, LayerSetLayerIdList);

        vps->vps_ext->direct_dep_type_len_minus2 = GOL_UE(v);
        // uint8_t direct_dependency_all_layers_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            vps->vps_ext->direct_dependency_all_layers_type = READ_BITS(v, vps->vps_ext->direct_dep_type_len_minus2 + 2);
        } else {
            vps->vps_ext->direct_dependency_type = malloc(sizeof(uint32_t *) * (MaxLayersMinus1+1));
            for (int i = vps->vps_base_layer_internal_flag ? 1 : 2; i <= MaxLayersMinus1; i++ ) {
                vps->vps_ext->direct_dependency_type[i] = malloc(i * sizeof(uint32_t));
                for (int j = vps->vps_base_layer_internal_flag ? 0 : 1; j < i; j++ ) {
                    if ((vps->vps_ext->direct_dependency_flag[i]>>j) &0x1) {
                        vps->vps_ext->direct_dependency_type[i][j] = READ_BITS(v, vps->vps_ext->direct_dep_type_len_minus2 + 2);
                    }
                }
            }
        }
        uint32_t vps_non_vui_extension_length = GOL_UE(v);
        vps->vps_ext->vps_non_vui_extension_data_byte = malloc(vps_non_vui_extension_length);
        for(int i = 0; i < vps_non_vui_extension_length; i++) {
            vps->vps_ext->vps_non_vui_extension_data_byte[i] = READ_BITS(v, 8);
        }
        // uint8_t vps_vui_present_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            while(!BYTE_ALIGNED(v)) {
                SKIP_BITS(v, 1);
            }
            vps->vps_ext->vps_vui = malloc(sizeof(struct vps_vui));
            parse_vps_vui(v, vps->vps_ext->vps_vui, vps, NumLayerSets,
                MaxSubLayersInLayerSetMinus1, NumOutputLayerSets, NumLayersInIdList,
                OlsIdxToLsIdx, SubPicHrdFlag, vps->NumDirectRefLayers, IdDirectRefLayer);
        }

        while (more_rbsp_data(v)) {
            SKIP_BITS(v, 1);
        }
    }
    rbsp_trailing_bits(v);
    bits_vec_free(v);
    return vps;
}

static struct sei *
parse_sei(struct hevc_nalu_header *headr, uint8_t *data, uint16_t len)
{
    struct sei * sei = malloc(sizeof(struct sei));
    do {
        sei->num ++;
        if (sei->msg == NULL)
            sei->msg = malloc(sizeof(struct sei_msg));
        else
            sei->msg = realloc(sei->msg, sizeof(struct sei_msg) * sei->num);
        struct sei_msg *m = &sei->msg[sei->num - 1];
        switch (m->last_paylod_type) {
            case 0:
            break;
            default:
            break;
        }
    } while(1);
    return sei;
}

struct slice_long_term {
    uint8_t lt_idx_sps;
    uint8_t poc_lsb_lt;
    uint8_t used_by_curr_pic_lt_flag:1;
    uint8_t delta_poc_msb_present_flag:1;
    GUE(delta_poc_msb_cycle_lt);
};

struct slice_segment_header {
    uint8_t no_output_of_prior_pics_flag:1;

    GUE(slice_pic_parameter_set_id);

    uint32_t slice_segment_address;

    GUE(slice_type);
    uint8_t pic_output_flag:1;
    uint8_t colour_plane_id:2;

    uint32_t slice_pic_order_cnt_lsb;

    struct st_ref_pic_set *st;

    uint32_t short_term_ref_pic_set_idx;
    GUE(num_long_term_sps);
    GUE(num_long_term_pics);

    struct slice_long_term* terms;

    uint32_t num_inter_layer_ref_pics_minus1;
    uint32_t *inter_layer_pred_layer_idc;

    GUE(num_ref_idx_l0_active_minus1);
    GUE(num_ref_idx_l1_active_minus1);

    uint32_t *list_entry_l0;
    uint32_t *list_entry_l1;

    uint8_t mvd_l1_zero_flag:1;
    uint8_t cabac_init_flag:1;

    GUE(collocated_ref_idx);

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
    uint32_t *entry_point_offset_minus1;
    GUE(slice_segment_header_extension_length);
    uint8_t poc_reset_period_id:6;
    uint8_t full_poc_reset_flag:1;
    uint32_t poc_lsb_val;

    GUE(poc_msb_cycle_val);
};

/* see F.7.2 */
bool
more_data_in_slice_segment_header_extension(uint64_t pos, struct bits_vec *v, struct slice_segment_header *slice)
{
    uint64_t cur_pos = (v->ptr - v->start) * 8 + 7 - v->offset;
    if (cur_pos - pos < slice->slice_segment_header_extension_length * 8) {
        return true;
    }
    return false;
}

void
ref_pic_lists_modification(struct bits_vec *v, struct slice_segment_header *slice, int NumPicTotalCurr)
{
    uint8_t ref_pic_list_modification_flag_l0 = READ_BIT(v);
    if (ref_pic_list_modification_flag_l0) {
        slice->list_entry_l0 = malloc((slice->num_ref_idx_l0_active_minus1 +1)* 4);
        for (int i = 0; i <= slice->num_ref_idx_l0_active_minus1; i++ ) {
            slice->list_entry_l0[i] = READ_BITS(v, log2ceil(NumPicTotalCurr));
        }
    }
    if (slice->slice_type == SLICE_TYPE_B) {
        uint8_t ref_pic_list_modification_flag_l1 = READ_BIT(v);
        if (ref_pic_list_modification_flag_l1) {
            slice->list_entry_l1 = malloc((slice->num_ref_idx_l1_active_minus1+1) *4);
            for (int i = 0; i <= slice->num_ref_idx_l1_active_minus1; i++ ) {
                slice->list_entry_l1[i] = READ_BITS(v, log2ceil(NumPicTotalCurr));
            }
        }
    }

}

static void
parse_slice_segment_header(struct hevc_nalu_header *headr, uint8_t *data, uint16_t len,
        struct vps *vps, struct pps *pps, struct sps *sps)
{
    uint8_t slice_temporal_mvp_enabled_flag = 0;
    struct slice_segment_header *slice = malloc(sizeof(*slice));
    struct bits_vec *v = bits_vec_alloc(data, len, BITS_MSB);
    uint8_t first_slice_segment_in_pic_flag = READ_BIT(v);
    if (headr->nal_unit_type > BLA_W_LP && headr->nal_unit_type <= RSV_IRAP_VCL23) {
        slice->no_output_of_prior_pics_flag = READ_BIT(v);
    }
    slice->slice_pic_parameter_set_id = GOL_UE(v);
    uint8_t dependent_slice_segment_flag = 0;
    if (!first_slice_segment_in_pic_flag) {
        if (pps->dependent_slice_segments_enabled_flag) {
            dependent_slice_segment_flag = READ_BIT(v);
        }
        //see 7-10
        uint32_t MinCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus3 + 3;
        //see 7-11
        uint32_t CtbLog2SizeY = MinCbLog2SizeY + sps->log2_diff_max_min_luma_coding_block_size;
        //see 7-13
        uint32_t CtbSizeY = 1 << CtbLog2SizeY;
        //see 7-15
        uint32_t PicWidthInCtbsY = divceil(sps->pic_width_in_luma_samples, CtbSizeY);
        //see 7-17
        uint32_t PicHeightInCtbsY = divceil( sps->pic_height_in_luma_samples, CtbSizeY);
        //see 7-19
        uint32_t PicSizeInCtbsY = PicWidthInCtbsY * PicHeightInCtbsY;
        slice->slice_segment_address = READ_BITS(v, log2ceil(PicSizeInCtbsY));
    }
    uint32_t CuQpDeltaVal = 0;
    if (!dependent_slice_segment_flag) {
        // int i = 0;
        // if (pps->num_extra_slice_header_bits > i) {
        //     i ++;
        //     // uint8_t discardable_flag = READ_BIT(v);
        // }
        // if (pps->num_extra_slice_header_bits > i) {
        //     i ++;
        //     uint8_t cross_layer_bla_flag = READ_BIT(v);
        // }
        for (int i = 0; i < pps->num_extra_slice_header_bits; i++ ){
            // slice_reserved_flag[ i ] = READ_BIT(v);
            SKIP_BITS(v, 1);
        }
        slice->slice_type = GOL_UE(v);
        if (pps->output_flag_present_flag) {
            slice->pic_output_flag = READ_BIT(v);
        }
        if (sps->separate_colour_plane_flag == 1 ) {
            slice->colour_plane_id = READ_BITS(v, 2);
        }
        if (headr->nuh_layer_id > 0 && !vps->vps_ext->poc_lsb_not_present_flag[vps->LayerIdxInVps[headr->nuh_layer_id]] ||
            (headr->nal_unit_type!= IDR_W_RADL && headr->nal_unit_type != IDR_N_LP)) {
            slice->slice_pic_order_cnt_lsb = READ_BITS(v, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
        }
        uint8_t *PocLsbLt;
        uint8_t *UsedByCurrPicLt;
        uint8_t short_term_ref_pic_set_sps_flag;
        if (headr->nal_unit_type != IDR_W_RADL && headr->nal_unit_type != IDR_N_LP) {
            short_term_ref_pic_set_sps_flag = READ_BIT(v);
            if (!short_term_ref_pic_set_sps_flag) {
                slice->st = malloc(sizeof(struct st_ref_pic_set));
                parse_st_ref_set(v, slice->st, sps->num_short_term_ref_pic_sets, sps->num_short_term_ref_pic_sets);
            } else if (sps->num_short_term_ref_pic_sets > 1) {
                slice->short_term_ref_pic_set_idx = READ_BITS(v, log2ceil(sps->num_short_term_ref_pic_sets));
            }

            if (sps->long_term_ref_pics_present_flag) {
                if (sps->num_long_term_ref_pics_sps > 0) {
                    slice->num_long_term_sps = GOL_UE(v);
                }
                slice->num_long_term_pics = GOL_UE(v);
                PocLsbLt = malloc(MAX(sps->num_long_term_ref_pics_sps, slice->num_long_term_sps + slice->num_long_term_pics));
                UsedByCurrPicLt = malloc(MAX(sps->num_long_term_ref_pics_sps, slice->num_long_term_sps + slice->num_long_term_pics));
                slice->terms = malloc((slice->num_long_term_sps + slice->num_long_term_pics)* sizeof(struct slice_long_term));
                for (int i = 0; i < slice->num_long_term_sps + slice->num_long_term_pics; i ++) {
                    if (i < slice->num_long_term_sps) {
                        if (sps->num_long_term_ref_pics_sps > 1) {
                            slice->terms[i].lt_idx_sps = READ_BITS(v, log2ceil(sps->num_long_term_ref_pics_sps));
                        }
                        PocLsbLt[i] = sps->lt_ref_pic_poc_lsb_sps[slice->terms[i].lt_idx_sps];
                        UsedByCurrPicLt[i] = sps->used_by_curr_pic_lt_sps_flag[slice->terms[i].lt_idx_sps];
                    } else {
                        slice->terms[i].poc_lsb_lt = READ_BITS(v, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
                        slice->terms[i].used_by_curr_pic_lt_flag = READ_BIT(v);
                        PocLsbLt[i] = slice->terms[i].poc_lsb_lt;
                        UsedByCurrPicLt[i] = slice->terms[i].used_by_curr_pic_lt_flag;
                    }
                    uint8_t delta_poc_msb_present_flag = READ_BIT(v);
                    if (delta_poc_msb_present_flag) {
                        slice->terms[i].delta_poc_msb_cycle_lt = GOL_UE(v);
                    }
                }
            }
            if (sps->sps_temporal_mvp_enabled_flag) {
                slice_temporal_mvp_enabled_flag = READ_BIT(v);
            }
        }
        if (headr->nuh_layer_id > 0 && !vps->vps_ext->default_ref_layers_active_flag &&
                vps->NumDirectRefLayers[headr->nuh_layer_id] > 0) {
            uint8_t inter_layer_pred_enabled_flag = READ_BIT(v);
            if (inter_layer_pred_enabled_flag && vps->NumDirectRefLayers[headr->nuh_layer_id] > 1) {
                if (!vps->vps_ext->max_one_active_ref_layer_flag) {
                    slice->num_inter_layer_ref_pics_minus1 = READ_BITS(v, log2ceil(vps->NumRefListLayers[headr->nuh_layer_id]));
                }
                //see I-32, F-52
                int j = 0;
                int refLayerPicIdc[64];
                for(int i = 0; i < vps->NumRefListLayers[headr->nuh_layer_id]; i++ ) {
                    int TemporalId = headr->nuh_temporal_id_plus1 - 1;
                    int refLayerIdx = vps->LayerIdxInVps[vps->IdRefListLayer[headr->nuh_layer_id][i]];
                    if( vps->vps_ext->sub_layers_vps_max_minus1[refLayerIdx] >= TemporalId && 
                        ( TemporalId == 0 || vps->vps_ext->max_tid_il_ref_pics_plus1[refLayerIdx][vps->LayerIdxInVps[headr->nuh_layer_id]] > TemporalId))
                        refLayerPicIdc[j++] = i;
                }
                int numRefLayerPics = j;
                //see I-33, F-53
                int NumActiveRefLayerPics;
                if (headr->nuh_layer_id == 0 || numRefLayerPics == 0)
                    NumActiveRefLayerPics = 0;
                else if (vps->vps_ext->default_ref_layers_active_flag )
                    NumActiveRefLayerPics = numRefLayerPics;
                else if (!inter_layer_pred_enabled_flag)
                    NumActiveRefLayerPics = 0;
                else if (vps->vps_ext->max_one_active_ref_layer_flag || vps->NumRefListLayers[headr->nuh_layer_id ] == 1 )
                    NumActiveRefLayerPics = 1;
                else
                    NumActiveRefLayerPics = slice->num_inter_layer_ref_pics_minus1 + 1;
                if (NumActiveRefLayerPics != vps->NumDirectRefLayers[headr->nuh_layer_id]) {
                    slice->inter_layer_pred_layer_idc = malloc(sizeof(uint32_t) * NumActiveRefLayerPics);
                    for (int i = 0; i < NumActiveRefLayerPics; i++) {
                        slice->inter_layer_pred_layer_idc[i] = READ_BITS(v, log2ceil(vps->NumRefListLayers[headr->nuh_layer_id]));
                    }
                }
            }
        }
        uint8_t ChromaArrayType = (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc);
        uint8_t slice_sao_luma_flag = 0;
        uint8_t slice_sao_chroma_flag = 0;
        if (sps->sample_adaptive_offset_enabled_flag) {
            slice_sao_luma_flag = READ_BIT(v);
            if (ChromaArrayType != 0) {
                slice_sao_chroma_flag = READ_BIT(v);
            }
        }
        if (slice->slice_type == SLICE_TYPE_P || slice->slice_type == SLICE_TYPE_B) {
            uint8_t num_ref_idx_active_override_flag = READ_BIT(v);
            if (num_ref_idx_active_override_flag) {
                slice->num_ref_idx_l0_active_minus1 = GOL_UE(v);
                if (slice->slice_type == SLICE_TYPE_B) {
                    slice->num_ref_idx_l1_active_minus1 = GOL_UE(v);
                }
            }
            int CurrRpsIdx;
            if (short_term_ref_pic_set_sps_flag) {
                CurrRpsIdx = slice->short_term_ref_pic_set_idx;
            } else {
                CurrRpsIdx = sps->num_short_term_ref_pic_sets;
            }
            //see (7-57)
            int NumPicTotalCurr = 0;
            int NumNegativePics, NumPositivePics;
            if (CurrRpsIdx < sps->num_short_term_ref_pic_sets) {
                NumNegativePics = sps->sps_st_ref[CurrRpsIdx].num_negative_pics;
                NumPositivePics = sps->sps_st_ref[CurrRpsIdx].num_positive_pics;
            } else {
                NumNegativePics = slice->st->num_negative_pics;
                NumPositivePics = slice->st->num_positive_pics;
            }
            for (int i = 0; i < NumNegativePics; i++ )
                if (sps->sps_st_ref[CurrRpsIdx].used_by_curr_pic_s0_flag[i]) {
                    NumPicTotalCurr ++;
                }
            for (int i = 0; i < NumPositivePics; i++)
                if (sps->sps_st_ref[CurrRpsIdx].used_by_curr_pic_s1_flag[i]) {
                    NumPicTotalCurr ++;
                }
            for (int i = 0; i < slice->num_long_term_sps + slice->num_long_term_pics; i++ )
                if(UsedByCurrPicLt[i])
                    NumPicTotalCurr++;
            if (pps->pps_scc_ext.pps_curr_pic_ref_enabled_flag) {
                NumPicTotalCurr++;
            }

            if (pps->lists_modification_present_flag && NumPicTotalCurr > 1 ) {
                ref_pic_lists_modification(v, slice, NumPicTotalCurr);
            }
            if (slice->slice_type == SLICE_TYPE_B) {
                slice->mvd_l1_zero_flag = READ_BIT(v);
            }
            if (pps->cabac_init_present_flag) {
                slice->cabac_init_flag = READ_BIT(v);
            }
            if (slice_temporal_mvp_enabled_flag) {
                uint8_t collocated_from_l0_flag = 0;
                if (slice->slice_type == SLICE_TYPE_B) {
                    collocated_from_l0_flag = READ_BIT(v);
                }
                if ((collocated_from_l0_flag && slice->num_ref_idx_l0_active_minus1 > 0) ||
                    (!collocated_from_l0_flag && slice->num_ref_idx_l1_active_minus1 > 0)) {
                    slice->collocated_ref_idx = GOL_UE(v);
                }
            }
            if ((pps->weighted_pred_flag && slice->slice_type == SLICE_TYPE_P) ||
                (pps->weighted_bipred_flag && slice->slice_type == SLICE_TYPE_B)) {
                // pred_weight_table();
            }
            slice->five_minus_max_num_merge_cand = GOL_UE(v);
            if (sps->sps_scc_ext.motion_vector_resolution_control_idc == 2) {
                slice->use_integer_mv_flag = READ_BIT(v);
            }
        }
        slice->slice_qp_delta = GOL_SE(v);
        if (pps->pps_slice_chroma_qp_offsets_present_flag) {
            slice->slice_cb_qp_offset = GOL_SE(v);
            slice->slice_cr_qp_offset = GOL_SE(v);
        }
        if (pps->pps_scc_ext.pps_slice_act_qp_offsets_present_flag) {
            slice->slice_act_y_qp_offset = GOL_SE(v);
            slice->slice_act_cb_qp_offset = GOL_SE(v);
            slice->slice_act_cr_qp_offset = GOL_SE(v);
        }
        if (pps->pps_range_ext.chroma_qp_offset_list_enabled_flag) {
            slice->cu_chroma_qp_offset_enabled_flag = READ_BIT(v);
        }
        uint8_t deblocking_filter_override_flag = 0;
        if (pps->deblocking_filter_override_enabled_flag) {
            deblocking_filter_override_flag = READ_BIT(v);
        }
        uint8_t slice_deblocking_filter_disabled_flag = 0;
        if (deblocking_filter_override_flag) {
            slice_deblocking_filter_disabled_flag = READ_BIT(v);
            if (!slice_deblocking_filter_disabled_flag) {
                slice->slice_beta_offset_div2 = GOL_SE(v);
                slice->slice_tc_offset_div2 = GOL_SE(v);
            }
        }
        if (pps->pps_loop_filter_across_slices_enabled_flag &&
                ( slice_sao_luma_flag || slice_sao_chroma_flag ||
                !slice_deblocking_filter_disabled_flag )) {
            slice->slice_loop_filter_across_slices_enabled_flag = READ_BIT(v);
        }
    }
    if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag ) {
        slice->num_entry_point_offsets = GOL_UE(v);
        if (slice->num_entry_point_offsets > 0) {
            uint32_t offset_len_minus1 = GOL_UE(v);
            slice->entry_point_offset_minus1 = malloc(sizeof(uint32_t) * slice->num_entry_point_offsets);
            for (int i = 0; i < slice->num_entry_point_offsets; i++) {
                slice->entry_point_offset_minus1[i] = READ_BITS(v, offset_len_minus1 + 1);
            }
        }
    }
    if (pps->slice_segment_header_extension_present_flag) {
        slice->slice_segment_header_extension_length = GOL_UE(v);
        uint64_t cur_pos = (v->ptr - v->start) * 8 + 7 - v->offset;
        uint8_t poc_reset_idc = 0;
        if (pps->pps_multiplayer_ext.poc_reset_info_present_flag ) {
            poc_reset_idc = READ_BITS(v, 2);
        }
        if (poc_reset_idc != 0) {
            slice->poc_reset_period_id = READ_BITS(v, 6);
        }
        if( poc_reset_idc == 3 ) {
            slice->full_poc_reset_flag = READ_BIT(v);
            slice->poc_lsb_val = READ_BITS(v, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
        }
        //see (F-1)
        bool CraOrBlaPicFlag = (headr->nal_unit_type == BLA_W_LP || headr->nal_unit_type == BLA_N_LP ||
                headr->nal_unit_type == BLA_W_RADL || headr->nal_unit_type == CRA_NUT);
        //see (F-55)
        bool PocMsbValRequiredFlag = CraOrBlaPicFlag && 
            (!vps->vps_ext->vps_poc_lsb_aligned_flag ||
            (vps->vps_ext->vps_poc_lsb_aligned_flag && vps->NumDirectRefLayers[headr->nuh_layer_id] == 0));
        uint8_t poc_msb_cycle_val_present_flag = 0;
        if (!PocMsbValRequiredFlag && vps->vps_ext->vps_poc_lsb_aligned_flag) {
            poc_msb_cycle_val_present_flag = READ_BIT(v);
        }
        if( poc_msb_cycle_val_present_flag ) {
            slice->poc_msb_cycle_val = GOL_UE(v);
        }
        while (more_data_in_slice_segment_header_extension(cur_pos, v, slice)) {
            // slice_segment_header_extension_data_bit = READ_BIT(v);
            SKIP_BITS(v, 1);
        }
    }

    byte_alignment(v);

    bits_vec_free(v);
}



struct vps *first_vps = NULL;

void
parse_nalu(uint8_t *data, uint16_t len)
{
    struct hevc_nalu_header h;
    h = *(struct hevc_nalu_header*)data;
    printf("f %d type %d, layer id %d, tid %d\n", h.forbidden_zero_bit, 
        h.nal_unit_type, h.nuh_layer_id, h.nuh_temporal_id_plus1);
    
    // hexdump(stdout, "nalu: ", data, len);
    uint8_t *rbsp = malloc(len);
    
    // See 7.3.1.1
    uint16_t nrbsp = 0;
    uint8_t *p = data + 2;
    for (int l = 2; l < len; l ++) {
        if ((l + 2 < len) && ((p[0]<<16| p[1]<<8| p[2]) == 0x3)) {
            rbsp[nrbsp++] = p[0];
            rbsp[nrbsp++] = p[1];
            l += 2;
            /* skip third byte */
            p += 3;
        } else {
            rbsp[nrbsp++] = p[0];
            p ++;
        }
    }
    // printf("rbsp %d: %02x %02x %02x %02x\n", nrbsp, rbsp[0], rbsp[1], rbsp[2],rbsp[3]);
    switch (h.nal_unit_type) {
    case VPS_NUT:
        first_vps = parse_vps(&h, rbsp, nrbsp);
        break;
    case SPS_NUT:
        parse_sps(&h, rbsp, nrbsp, first_vps);
        break;
    case PPS_NUT:
        parse_pps(&h, rbsp, nrbsp);
        break;
    case PREFIX_SEI_NUT:
    case SUFFIX_SEI_NUT:
        parse_sei(&h, rbsp, nrbsp);
        break;
    default:
        break;
    }
}
