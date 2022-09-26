#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "utils.h"
#include "hevc.h"
#include "cabac.h"


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

static void
parse_pps_range_extension(struct bits_vec *v, struct pps *pps, struct pps_range_extension *pps_range)
{
    if (pps->transform_skip_enabled_flag)
            pps_range->log2_max_transform_skip_block_size_minus2 = GOL_UE(v);
        pps_range->cross_component_prediction_enabled_flag = READ_BIT(v);
        pps_range->chroma_qp_offset_list_enabled_flag = READ_BIT(v);
        if (pps_range->chroma_qp_offset_list_enabled_flag) {
            pps_range->diff_cu_chroma_qp_offset_depth = GOL_UE(v);
            pps_range->chroma_qp_offset_list_len_minus1 = GOL_UE(v);
            for (int i = 0; i <= pps_range->chroma_qp_offset_list_len_minus1; i ++) {
                pps_range->cb_qp_offset_list[i] = GOL_SE(v);
                pps_range->cr_qp_offset_list[i] = GOL_SE(v);
            }
        }
        pps_range->log2_sao_offset_scale_luma = GOL_UE(v);
        pps_range->log2_sao_offset_scale_chroma = GOL_UE(v);
}

static void
parse_pps_multilayer_extension(struct bits_vec *v, struct pps_multilayer_extension *multilayer)
{
    multilayer->poc_reset_info_present_flag = READ_BIT(v);
    multilayer->pps_infer_scaling_list_flag = READ_BIT(v);
    if (multilayer->pps_infer_scaling_list_flag) {
        multilayer->pps_scaling_list_ref_layer_id = READ_BITS(v, 6);
    }
    multilayer->num_ref_loc_offsets = GOL_UE(v);
    for (int i = 0; i < multilayer->num_ref_loc_offsets; i ++) {
        multilayer->reflayer[i].ref_loc_offset_layer_id = READ_BITS(v, 6);
        multilayer->reflayer[i].scaled_ref_layer_offset_present_flag = READ_BIT(v);
        if (multilayer->reflayer[i].scaled_ref_layer_offset_present_flag) {
            multilayer->reflayer[i].scaled_ref_layer_left_offset = GOL_SE(v);
            multilayer->reflayer[i].scaled_ref_layer_top_offset = GOL_SE(v);
            multilayer->reflayer[i].scaled_ref_layer_right_offset = GOL_SE(v);
            multilayer->reflayer[i].scaled_ref_layer_bottom_offset = GOL_SE(v);
        }
        multilayer->reflayer[i].ref_region_offset_present_flag = READ_BIT(v);
        if (multilayer->reflayer[i].ref_region_offset_present_flag) {
            multilayer->reflayer[i].ref_region_left_offset = GOL_SE(v);
            multilayer->reflayer[i].ref_region_top_offset = GOL_SE(v);
            multilayer->reflayer[i].ref_region_right_offset = GOL_SE(v);
            multilayer->reflayer[i].ref_region_bottom_offset = GOL_SE(v);
        }
        multilayer->reflayer[i].resample_phase_set_present_flag = READ_BIT(v);
        if (multilayer->reflayer[i].resample_phase_set_present_flag) {
            multilayer->reflayer[i].phase_hor_luma = GOL_UE(v);
            multilayer->reflayer[i].phase_ver_luma = GOL_UE(v);
            multilayer->reflayer[i].phase_hor_chroma_plus8 = GOL_UE(v);
            multilayer->reflayer[i].phase_ver_chroma_plus8 = GOL_UE(v);
        }
    }
    multilayer->colour_mapping_enabled_flag = READ_BIT(v);
    if (multilayer->colour_mapping_enabled_flag) {
        multilayer->color_map = malloc(sizeof(struct color_mapping_table));
        parse_color_mapping_table(v, multilayer->color_map);
    }
}

static void
parse_pps_3d_extension(struct bits_vec *v, struct pps_3d_extension *pps_3d_ext)
{
    pps_3d_ext->dlts_present_flag = READ_BIT(v);
    if (pps_3d_ext->dlts_present_flag) {
        pps_3d_ext->pps_depth_layers_minus1 = READ_BITS(v, 6);
        pps_3d_ext->pps_bit_depth_for_depth_layers_minus8 = READ_BITS(v, 4);
        for (int i = 0; i < pps_3d_ext->pps_depth_layers_minus1; i ++) {
            pps_3d_ext->pps_3d_layers[i].dlt_flag = READ_BIT(v);
            if (pps_3d_ext->pps_3d_layers[i].dlt_flag) {
                pps_3d_ext->pps_3d_layers[i].dlt_pred_flag = READ_BIT(v);
                if (!pps_3d_ext->pps_3d_layers[i].dlt_pred_flag) {
                    pps_3d_ext->pps_3d_layers[i].dlt_val_flags_present_flag = READ_BIT(v);
                }
                if (pps_3d_ext->pps_3d_layers[i].dlt_val_flags_present_flag) {
                    for (int j = 0; j < (1 << (pps_3d_ext->pps_bit_depth_for_depth_layers_minus8 + 8)) - 1; j ++) {
                        pps_3d_ext->pps_3d_layers[i].dlt_value_flag[j] = READ_BIT(v);
                    }
                } else {
                    //delta_dlt
                    int vl = pps_3d_ext->pps_bit_depth_for_depth_layers_minus8 + 8;
                    pps_3d_ext->pps_3d_layers[i].num_val_delta_dlt = READ_BITS(v, vl);
                    if (pps_3d_ext->pps_3d_layers[i].num_val_delta_dlt > 0) {
                        if (pps_3d_ext->pps_3d_layers[i].num_val_delta_dlt > 1) {
                            pps_3d_ext->pps_3d_layers[i].max_diff = READ_BITS(v, vl);
                        }
                        if (pps_3d_ext->pps_3d_layers[i].num_val_delta_dlt > 2 &&
                                pps_3d_ext->pps_3d_layers[i].max_diff > 0) {
                            pps_3d_ext->pps_3d_layers[i].min_diff_minus1 = READ_BITS(v, log2ceil(pps_3d_ext->pps_3d_layers[i].max_diff + 1));
                        }
                        pps_3d_ext->pps_3d_layers[i].delta_dlt_val0 = READ_BITS(v, vl);
                        if (pps_3d_ext->pps_3d_layers[i].max_diff > (pps_3d_ext->pps_3d_layers[i].min_diff_minus1 + 1)) {
                            for (int k = 1; k < pps_3d_ext->pps_3d_layers[i].num_val_delta_dlt; k ++) {
                                pps_3d_ext->pps_3d_layers[i].delta_val_diff_minus_min[k] = READ_BITS(v, log2ceil(pps_3d_ext->pps_3d_layers[i].max_diff - pps_3d_ext->pps_3d_layers[i].min_diff_minus1));
                            }
                        }
                    }
                }
            }
        }
    }    
}

static void
parse_pps_scc_extension(struct bits_vec *v, struct pps_scc_extension *pps_scc_ext)
{
    pps_scc_ext->pps_curr_pic_ref_enabled_flag = READ_BIT(v);
    pps_scc_ext->residual_adaptive_colour_transform_enabled_flag = READ_BIT(v);
    if (pps_scc_ext->residual_adaptive_colour_transform_enabled_flag) {
        pps_scc_ext->pps_slice_act_qp_offsets_present_flag = READ_BIT(v);
        pps_scc_ext->pps_act_y_qp_offset_plus5 = GOL_SE(v);
        pps_scc_ext->pps_act_cb_qp_offset_plus5 = GOL_SE(v);
        pps_scc_ext->pps_act_cr_qp_offset_plus3 = GOL_SE(v);
    }
    pps_scc_ext->pps_palette_predictor_initializers_present_flag = READ_BIT(v);
    if (pps_scc_ext->pps_palette_predictor_initializers_present_flag) {
        pps_scc_ext->pps_num_palette_predictor_initializers = GOL_UE(v);
        if (pps_scc_ext->pps_num_palette_predictor_initializers) {
            pps_scc_ext->monochrome_palette_flag = READ_BIT(v);
            pps_scc_ext->luma_bit_depth_entry_minus8 = GOL_UE(v);
            if (!pps_scc_ext->monochrome_palette_flag) {
                pps_scc_ext->chroma_bit_depth_entry_minus8 = GOL_UE(v);
            }
            int numComps = pps_scc_ext->monochrome_palette_flag? 1 : 3;
            for (int comp = 0; comp < numComps; comp ++) {
                for (int i = 0; i < pps_scc_ext->pps_num_palette_predictor_initializers; i ++) {
                    if (i == 0) {
                        pps_scc_ext->pps_palette_predictor_initializer[comp][i] = READ_BITS(v, pps_scc_ext->luma_bit_depth_entry_minus8 + 8);
                    } else {
                        pps_scc_ext->pps_palette_predictor_initializer[comp][i] = READ_BITS(v, pps_scc_ext->chroma_bit_depth_entry_minus8 + 8);
                    }
                }
            }
        }
    }
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
            pps->column_width_minus1 =malloc(pps->num_tile_columns_minus1 *4);
            for (int i = 0; i < pps->num_tile_columns_minus1; i ++) {
                pps->column_width_minus1[i] = GOL_UE(v);
            }
            pps->row_height_minus1 =malloc(pps->num_tile_rows_minus1 *4);
            for (int i = 0; i< pps->num_tile_rows_minus1; i ++) {
                pps->row_height_minus1[i] = GOL_UE(v);
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
        pps->pps_range_ext = malloc(sizeof(struct pps_range_extension));
        parse_pps_range_extension(v, pps, pps->pps_range_ext);
    }
    if (pps->pps_multilayer_extension_flag) {
        pps->pps_multilayer_ext = malloc(sizeof(struct pps_multilayer_extension));
        parse_pps_multilayer_extension(v, pps->pps_multilayer_ext);
    }
    if (pps->pps_3d_extension_flag) {
        pps->pps_3d_ext = malloc(sizeof(struct pps_3d_extension));
        parse_pps_3d_extension(v, pps->pps_3d_ext);
    }
    if (pps->pps_scc_extension_flag) {
        pps->pps_scc_ext = malloc(sizeof(struct pps_scc_extension));
        parse_pps_scc_extension(v, pps->pps_scc_ext);
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
parse_lt_ref_set(struct bits_vec *v, struct sps *sps, struct lt_ref_pic_set *lt)
{
    lt->lt_ref_pic_poc_lsb_sps = malloc(sps->num_long_term_ref_pics_sps);
    lt->used_by_curr_pic_lt_sps_flag = malloc(sps->num_long_term_ref_pics_sps);
    for (int i = 0; i < sps->num_long_term_ref_pics_sps; i ++) {
        lt->lt_ref_pic_poc_lsb_sps[i] = READ_BITS(v, 8);
        lt->used_by_curr_pic_lt_sps_flag[i] = READ_BIT(v);
    }
}
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

static void
parse_sps_range_ext(struct sps_range_extension *range, struct bits_vec *v)
{
    range->transform_skip_rotation_enabled_flag = READ_BIT(v);
    range->transform_skip_context_enabled_flag = READ_BIT(v);
    range->implicit_rdpcm_enabled_flag = READ_BIT(v);
    range->explicit_rdpcm_enabled_flag = READ_BIT(v);
    range->extended_precision_processing_flag = READ_BIT(v);
    range->intra_smoothing_disabled_flag = READ_BIT(v);
    range->high_precision_offsets_enabled_flag = READ_BIT(v);
    range->persistent_rice_adaptation_enabled_flag = READ_BIT(v);
    range->cabac_bypass_alignment_enabled_flag = READ_BIT(v);
}

static void
parse_sps_3d_ext(struct sps_3d_extension *sps3d, int d, struct bits_vec *v)
{
    sps3d->iv_di_mc_enabled_flag = READ_BIT(v);
    sps3d->iv_mv_scal_enabled_flag = READ_BIT(v);
    if (d == 0) {
        sps3d->log2_ivmc_sub_pb_size_minus3 = GOL_UE(v);
        sps3d->iv_res_pred_enabled_flag = READ_BIT(v);
        sps3d->depth_ref_enabled_flag = READ_BIT(v);
        sps3d->vsp_mc_enabled_flag = READ_BIT(v);
        sps3d->dbbp_enabled_flag = READ_BIT(v);
    } else {
        // sps3d->iv_di_mc_enabled_flag1 = READ_BIT(v);
        // sps3d->iv_mv_scal_enabled_flag1 = READ_BIT(v);
        sps3d->tex_mc_enabled_flag = READ_BIT(v);
        sps3d->log2_texmc_sub_pb_size_minus3 = GOL_UE(v);
        sps3d->intra_contour_enabled_flag = READ_BIT(v);
        sps3d->intra_dc_only_wedge_enabled_flag = READ_BIT(v);
        sps3d->cqt_cu_part_pred_enabled_flag = READ_BIT(v);
        sps3d->inter_dc_only_enabled_flag = READ_BIT(v);
        sps3d->skip_intra_enabled_flag = READ_BIT(v);
    }
}

static void
parse_sps_scc_ext(struct sps_scc_extension *scc, struct bits_vec *v, int numComps)
{
    scc->sps_curr_pic_ref_enabled_flag = READ_BIT(v);
    scc->palette_mode_enabled_flag = READ_BIT(v);
    if (scc->palette_mode_enabled_flag) {
        scc->palette_max_size = GOL_UE(v);
        scc->delta_palette_max_predictor_size = GOL_UE(v);
        scc->sps_palette_predictor_initializers_present_flag = READ_BIT(v);
        if (scc->sps_palette_predictor_initializers_present_flag) {
            scc->sps_num_palette_predictor_initializers_minus1 = GOL_UE(v);
            for (int comp = 0; comp < numComps; comp++ ) {
                for (int i = 0; i <= scc->sps_num_palette_predictor_initializers_minus1; i++) {
                    scc->sps_palette_predictor_initializer[comp][i] = READ_BITS(v, 8);  // variable
                }
            }
        }
    }
    scc->motion_vector_resolution_control_idc = READ_BITS(v, 2);
    scc->intra_boundary_filtering_disabled_flag = READ_BIT(v);
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
        //sps_max_sub_layer_minus1 should less than 7, otherwise get from vps
        sps->sps_max_sub_layer_minus1 = (sps->sps_max_sub_layer_minus1 == 7 ) ? vps->vps_max_sub_layers_minus1 : sps->sps_max_sub_layer_minus1;
    }
    assert(sps->sps_max_sub_layer_minus1 > 7);
    int multilayer_ext_sps_flag = (h->nuh_layer_id != 0 && sps->sps_max_sub_layer_minus1 == 7);
    if (!multilayer_ext_sps_flag) {
        //almost must go here
        sps->sps_temporal_id_nesting_flag = READ_BIT(v);
        // when sps_temporal_id_nesting_flag should be 1 if sps_max_sub_layers_minus1 is 0
        printf("sps_max_sub_layer_minus1 %d\n", sps->sps_max_sub_layer_minus1);
        parse_profile_tier_level(v, &sps->sps_profile_tier_level, 1, sps->sps_max_sub_layer_minus1);
    }
    sps->sps_seq_parameter_set_id = GOL_UE(v);
    //less than 16
    printf("sps: sps_seq_parameter_set_id %d\n", sps->sps_seq_parameter_set_id);
    
    if (multilayer_ext_sps_flag) {
        // uint8_t update_rep_format_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            sps->sps_rep_format_idx = READ_BITS(v, 8);
        }
    } else {
        sps->chroma_format_idc = GOL_UE(v);
        if (sps->chroma_format_idc == CHROMA_444) {
            sps->separate_colour_plane_flag = READ_BIT(v);
        }
        printf("chroma_format_idc %d, separate_colour_plane_flag %d\n",
            sps->chroma_format_idc, sps->separate_colour_plane_flag);
        sps->pic_width_in_luma_samples = GOL_UE(v);
        sps->pic_height_in_luma_samples = GOL_UE(v);
        printf("pic_width_in_luma_samples %d, pic_height_in_luma_samples %d\n",
            sps->pic_width_in_luma_samples, sps->pic_height_in_luma_samples);
        // sps->conformance_window_flag = READ_BIT(v);
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
    //should be less than 12
    if (LIKELY(!multilayer_ext_sps_flag)) {
        // uint8_t sps_sub_layer_ordering_info_present_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            // all sub layer
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
        if (UNLIKELY(multilayer_ext_sps_flag)) {
            sps_infer_scaling_list_flag = READ_BIT(v);
        }
        if (UNLIKELY(sps_infer_scaling_list_flag)) {
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
        sps->pcm = malloc(sizeof(struct pcm));
        sps->pcm->pcm_sample_bit_depth_luma_minus1 = READ_BITS(v, 4);
        sps->pcm->pcm_sample_bit_depth_chroma_minus1 = READ_BITS(v, 4);
        sps->pcm->log2_min_pcm_luma_coding_block_size_minus3 = GOL_UE(v);
        sps->pcm->log2_diff_max_min_pcm_luma_coding_block_size = GOL_UE(v);
        sps->pcm->pcm_loop_filter_disabled_flag = READ_BIT(v);
    }
    sps->num_short_term_ref_pic_sets = GOL_UE(v);
    // should be less than 64
    if (sps->num_short_term_ref_pic_sets) {
        sps->sps_st_ref = calloc(sps->num_short_term_ref_pic_sets, sizeof(struct st_ref_pic_set));
        for (int i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
            parse_st_ref_set(v, sps->sps_st_ref + i, i, sps->num_short_term_ref_pic_sets);
        }
    }
    sps->long_term_ref_pics_present_flag = READ_BIT(v);
    if (sps->long_term_ref_pics_present_flag) {
        sps->num_long_term_ref_pics_sps = GOL_UE(v);
        sps->sps_lt_ref = malloc(sizeof(struct lt_ref_pic_set));
        parse_lt_ref_set(v, sps, sps->sps_lt_ref);
    }
    sps->sps_temporal_mvp_enabled_flag = READ_BIT(v);
    sps->strong_intra_smoothing_enabled_flag = READ_BIT(v);
    sps->vui_parameters_present_flag = READ_BIT(v);
    if (sps->vui_parameters_present_flag) {
        sps->vui = malloc(sizeof(struct vui_parameters));
        parse_vui(v, sps->vui, sps->sps_max_sub_layer_minus1);
    }
    // read extension flags
    sps->sps_extension_present_flag = READ_BIT(v);
    if (sps->sps_extension_present_flag) {
        sps->sps_range_extension_flag = READ_BIT(v);
        sps->sps_multilayer_extension_flag = READ_BIT(v);
        sps->sps_3d_extension_flag = READ_BIT(v);
        sps->sps_scc_extension_flag = READ_BIT(v);
        sps->sps_extension_4bits = READ_BITS(v, 4);
    }
    if (sps->sps_range_extension_flag) {
        sps->sps_range_ext = malloc(sizeof(struct sps_range_extension));
        parse_sps_range_ext(sps->sps_range_ext, v);
    }
    if (sps->sps_multilayer_extension_flag) {
        sps->sps_multilayer_ext.inter_view_mv_vert_constraint_flag = READ_BIT(v);
    }
    if (sps->sps_3d_extension_flag) {
        parse_sps_3d_ext(sps->sps_3d_ext, 0, v);
        parse_sps_3d_ext(sps->sps_3d_ext+1, 1, v);
    }
    if (sps->sps_scc_extension_flag) {
        sps->sps_scc_ext = malloc(sizeof(struct sps_scc_extension));
        int numComps = sps->chroma_format_idc == 0 ? 1: 3;
        parse_sps_scc_ext(sps->sps_scc_ext, v, numComps);
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
    int OlsIdxToLsIdx[], int MaxSubLayersInLayerSetMinus1[],
    int NecessaryLayerFlag[][64])
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
                for (int k = 0; k < vps->NumLayersInIdList[currLsIdx]; k++ ) {
                    if (NecessaryLayerFlag[i][k] && (vps->vps_base_layer_internal_flag ||
                        (vps->LayerSetLayerIdList[currLsIdx][k] != 0 ))) {
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
parse_vps_vui_bsp_hrd_params(struct bits_vec *v, struct vps_vui_bsp_hrd_params *hrd, struct vps *vps,
        int vps_num_hrd_parameters, int NumOutputLayerSets,
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
                    for (int r = 0; r < vps->NumLayersInIdList[OlsIdxToLsIdx[h]]; r++ ) {
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

static void
parse_vps_vui(struct bits_vec *v, struct vps_vui* vui, struct vps *vps, int NumLayerSets,
        int MaxSubLayersInLayerSetMinus1[], int NumOutputLayerSets,
        int *OlsIdxToLsIdx, int SubPicHrdFlag, int* NumDirectRefLayers, int IdDirectRefLayer[][64])
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
        parse_vps_vui_bsp_hrd_params(v, vui->vui_hrd, vps, vps->vps_timing_info->vps_num_hrd_parameters,
            NumOutputLayerSets, OlsIdxToLsIdx, MaxSubLayersInLayerSetMinus1, SubPicHrdFlag);
    }
    vui->base_layer_parameter_set_compatibility_flag = malloc(MaxLayersMinus1);
    for (int i = 0; i < MaxLayersMinus1; i++) {
        if (vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]] == 0) {
            vui->base_layer_parameter_set_compatibility_flag[i] = READ_BIT(v);
        }
    }
}

static void
parse_vps_timing_info(struct bits_vec *v, struct vps *vps, struct vps_timing_info *vps_tim)
{
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

static void
parse_vps_extension(struct bits_vec *v, struct vps *vps, struct vps_extension *vps_ext)
{
    if (vps->vps_max_layers_minus1 > 0 && vps->vps_base_layer_internal_flag) {
        vps_ext->vps_ext_profile_tier_level = malloc(sizeof(struct profile_tier_level));
        parse_profile_tier_level(v, vps_ext->vps_ext_profile_tier_level, 0, vps->vps_max_sub_layers_minus1);
    }
    vps_ext->splitting_flag = READ_BIT(v);
    int NumScalabilityTypes = 0;
    int MaxLayersMinus1 = MIN(62, vps->vps_max_layers_minus1);
    for (int i = 0; i < 16; i ++) {
        /* dimensions:
            index 0: texture or depth,  depthLayerFlag
            index 1: multiview,         viewoderIdx
            index 2: spatial/quality scalability,  dependencyId
            index 3: auxiliary,          auxid
            index 4-15: reserved
        */
        uint8_t scalability_mask_flag = READ_BIT(v);
        vps_ext->scalability_mask_flag |= (scalability_mask_flag << i);
        NumScalabilityTypes += scalability_mask_flag;
    }
    vps_ext->dimension_id_len_minus1 = malloc(NumScalabilityTypes - vps_ext->splitting_flag);
    for (int j = 0; j < NumScalabilityTypes - vps_ext->splitting_flag; j ++) {
        vps_ext->dimension_id_len_minus1[j] = READ_BITS(v, 3);
    }
    // vps_ext->vps_nuh_layer_id_present_flag = READ_BIT(v);
    uint8_t vps_nuh_layer_id_present_flag = READ_BIT(v);
    if (vps_nuh_layer_id_present_flag) {
        vps_ext->layer_id_in_nuh = malloc(MaxLayersMinus1 + 1);
    }
    if (!vps_ext->splitting_flag) {
        vps_ext->dimension_id = malloc(sizeof(uint8_t *) * (MaxLayersMinus1+1));
    }
    for (int i = 1; i <= MaxLayersMinus1; i ++) {
        if(vps_nuh_layer_id_present_flag) {
            vps_ext->layer_id_in_nuh[i] = READ_BITS(v, 6);
            vps->LayerIdxInVps[vps_ext->layer_id_in_nuh[i]] = i;
        }
        if (!vps_ext->splitting_flag) {
            vps_ext->dimension_id[i] = malloc(NumScalabilityTypes);
            for (int j = 0; j < NumScalabilityTypes; j ++) {
                vps_ext->dimension_id[i][j] = READ_BITS(v, vps_ext->dimension_id_len_minus1[j]+1);
            }
        }
    }


    int SubPicHrdFlag = 0;
    vps_ext->NumViews = 1;
    uint8_t ScalabilityId[64][16];
    uint8_t DependencyId[64];
    uint8_t AuxId[64];

    for (int i = 0; i <= MaxLayersMinus1; i ++) {
        uint8_t iNuhLId = vps_ext->layer_id_in_nuh[i];
        for (int smIdx = 0, j = 0; smIdx < 16; smIdx ++) {
            if (vps_ext->scalability_mask_flag &(1 <<smIdx)) {
                ScalabilityId[i][smIdx] = vps_ext->dimension_id[i][j++];
            } else {
                ScalabilityId[i][smIdx] = 0;
            }
        }
        vps->DepthLayerFlag[iNuhLId] = ScalabilityId[i][0];
        vps->ViewOrderIdx[iNuhLId] = ScalabilityId[i][1];
        DependencyId[iNuhLId] = ScalabilityId[i][2];
        AuxId[iNuhLId] = ScalabilityId[i][3];
        if (i > 0) {
            int newViewFlag = 1;
            for (int j = 0; j < i; j ++) {
                if (vps->ViewOrderIdx[iNuhLId] == vps->ViewOrderIdx[vps_ext->layer_id_in_nuh[j]]) {
                    newViewFlag = 0;
                }
            }
            vps_ext->NumViews += newViewFlag;
        }

    }
    vps_ext->view_id_len = READ_BITS(v, 4);
    if (vps_ext->view_id_len) {
        for (int i = 0 ; i < vps_ext->NumViews; i ++) {
            vps_ext->view_id_val[i] = READ_BITS(v, vps_ext->view_id_len);
        }
    }
    for (int i = 1; i <= MaxLayersMinus1; i ++) {
        for (int j = 0; j < i ; j ++) {
            vps_ext->direct_dependency_flag[i] |= (READ_BIT(v) << j);
        }
    }

    int DependencyFlag[64][64];
    // (F-4)
    for (int i = 0; i <= MaxLayersMinus1; i++) {
        for (int j = 0; j <= MaxLayersMinus1; j++ ) {
            DependencyFlag[i][j] = ((vps_ext->direct_dependency_flag[i] >> j) & 0x1);
            for (int k = 0; k < i; k++ ) {
                if(((vps_ext->direct_dependency_flag[i]>>k) & 0x1) && DependencyFlag[k][j]) {
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
        int iNuhLId = vps_ext->layer_id_in_nuh[i];
        int d = 0, r = 0, p = 0;
        for (int j = 0; j <= MaxLayersMinus1; j++ ) {
            int jNuhLid = vps_ext->layer_id_in_nuh[j];
            if ((vps_ext->direct_dependency_flag[i] >> j) & 0x01 ) {
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
        //see (I-8)
        vps->NumRefListLayers[iNuhLId] = 0;
        for(int j = 0; j < vps->NumDirectRefLayers[ iNuhLId ]; j++ ) {
            int jNuhLId = IdDirectRefLayer[iNuhLId][j];
            if( vps->DepthLayerFlag[ iNuhLId ] == vps->DepthLayerFlag[ jNuhLId ] ) {
                vps->IdRefListLayer[iNuhLId][vps->NumRefListLayers[iNuhLId]++] = jNuhLId;
            }
        }
    }

    // (F-6)
    int TreePartitionLayerIdList[64][64];
    int NumLayersInTreePartition[64];
    int layerIdInListFlag[64] = {0};
    int k = 0;
    for (int i = 0; i <= MaxLayersMinus1; i++) {
        int iNuhLId = vps_ext->layer_id_in_nuh[i];
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
        vps_ext->num_add_layer_sets = GOL_UE(v);
    }
    // (F-7)
    int NumLayerSets = vps->vps_num_layer_sets_minus1 + 1 + vps_ext->num_add_layer_sets;
    int OlsIdxToLsIdx[64];
    int NumOutputLayersInOutputLayerSet[64];
    int OlsHighestOutputLayerId[1024];
    int NecessaryLayerFlag[64][64];
    int NumNecessaryLayers[64];
    vps_ext->highest_layer_idx_plus1 = malloc(sizeof(uint16_t *) * vps_ext->num_add_layer_sets);
    for (int i = 0; i < vps_ext->num_add_layer_sets; i ++) {
        vps_ext->highest_layer_idx_plus1[i] = malloc(sizeof(uint16_t) * NumIndependentLayers);
        for (int j = 1; j < NumIndependentLayers; j ++) {
            vps_ext->highest_layer_idx_plus1[i][j] = READ_BITS(v, log2ceil(NumLayersInTreePartition[j]+1));
        }
        // (F-9)
        int layerNum = 0;
        int lsIdx = vps->vps_num_layer_sets_minus1 + 1 + i;
        for(int treeIdx = 1; treeIdx < NumIndependentLayers; treeIdx++ ) {
            for(int layerCnt = 0; layerCnt < vps_ext->highest_layer_idx_plus1[i][treeIdx]; layerCnt++) {
                vps->LayerSetLayerIdList[lsIdx][layerNum++] = TreePartitionLayerIdList[treeIdx][layerCnt];
            }
        }
        vps->NumLayersInIdList[lsIdx] = layerNum;
    }

    // uint8_t vps_sub_layers_max_minus1_present_flag = READ_BIT(v);
    // if (vps_sub_layers_max_minus1_present_flag) {
    if (READ_BIT(v)) {
        vps_ext->sub_layers_vps_max_minus1 = malloc(MaxLayersMinus1);
        for (int i = 0; i < MaxLayersMinus1; i ++) {
            vps_ext->sub_layers_vps_max_minus1[i] = READ_BITS(v, 3);
        }
    }

    //(F-10)
    int MaxSubLayersInLayerSetMinus1[64];
    for (int i = 0; i < NumLayerSets; i++ ) {
        int maxSlMinus1 = 0;
        for(int k = 0; k < vps->NumLayersInIdList[i]; k++ ) {
            int lId = vps->LayerSetLayerIdList[i][k];
            maxSlMinus1 = MAX(maxSlMinus1, vps_ext->sub_layers_vps_max_minus1[vps->LayerIdxInVps[lId]]);
            MaxSubLayersInLayerSetMinus1[i] = maxSlMinus1;
        }
    }

    // uint8_t max_tid_ref_present_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        for (int i = 0; i < MaxLayersMinus1 - 1; i ++) {
            for (int j = i + 1; j <= MaxLayersMinus1; j ++) {
                if ((vps_ext->direct_dependency_flag[i]>>j) & 0x1) {
                    vps_ext->max_tid_il_ref_pics_plus1[i][j] = READ_BITS(v, 3);
                }
            }
        }
    }
    vps_ext->default_ref_layers_active_flag = READ_BIT(v);
    vps_ext->vps_num_profile_tier_level_minus1 = GOL_UE(v);
    for (int i = vps->vps_base_layer_internal_flag ? 2 : 1;
        i <= vps_ext->vps_num_profile_tier_level_minus1; i++ ) {
        uint8_t vps_profile_present_flag = READ_BIT(v);
        parse_profile_tier_level(v, vps_ext->vps_ext_profile_tier_level, vps_profile_present_flag, vps->vps_max_sub_layers_minus1);
    }
    if( NumLayerSets > 1 ) {
        vps_ext->num_add_olss = GOL_UE(v);
        vps_ext->default_output_layer_idc = READ_BITS(v, 2);
    }
    int defaultOutputLayerIdc = MIN(vps_ext->default_output_layer_idc, 2);
    int NumOutputLayerSets = vps_ext->num_add_olss + NumLayerSets;


    vps_ext->layer_set_idx_for_ols_minus1 = malloc(sizeof(uint16_t) * NumOutputLayerSets);
    vps_ext->output_layer_flag = malloc(sizeof(uint8_t *) * NumOutputLayerSets);
    vps_ext->profile_tier_level_idx = malloc(sizeof(uint16_t *) * NumOutputLayerSets);
    vps_ext->alt_output_layer_flag = malloc(NumOutputLayerSets);
    for (int i = 1; i < NumOutputLayerSets; i++ ) {
        if( NumLayerSets > 2 && i >= NumLayerSets ) {
            vps_ext->layer_set_idx_for_ols_minus1[i] = READ_BITS(v, log2ceil(NumLayerSets-1));
            // OlsIdxToLsIdx[i] = (i < NumLayerSets) ? i : (layer_set_idx_for_ols_minus1[i] + 1);
        }
        // (F-11)
        OlsIdxToLsIdx[i] = (i < NumLayerSets) ? i : (vps_ext->layer_set_idx_for_ols_minus1[i] + 1); 
        if (i > vps->vps_num_layer_sets_minus1 || defaultOutputLayerIdc == 2) {
            vps_ext->output_layer_flag[i] = malloc(vps->NumLayersInIdList[OlsIdxToLsIdx[i]]);
            for (int j = 0; j < vps->NumLayersInIdList[OlsIdxToLsIdx[i]]; j++) {
                vps_ext->output_layer_flag[i][j] = READ_BIT(v);
            }
        }
        // (F-12)
        if (i >= ((defaultOutputLayerIdc == 2) ? 0: (vps->vps_num_layer_sets_minus1 + 1))) {
            NumOutputLayersInOutputLayerSet[i] = 0;
            for (int j = 0; j < vps->NumLayersInIdList[OlsIdxToLsIdx[i]]; j++ ) {
                NumOutputLayersInOutputLayerSet[i] += vps_ext->output_layer_flag[i][j];
                // OutputLayerFlag[i][j] = vps_ext->output_layer_flag[i][j];
                if (vps_ext->output_layer_flag[i][j]) {
                    OlsHighestOutputLayerId[i] = vps->LayerSetLayerIdList[OlsIdxToLsIdx[i]][j];
                }
            }
        }
        //(F-13)
        for (int olsIdx = 0; olsIdx < NumOutputLayerSets; olsIdx++ ) {
            int lsIdx = OlsIdxToLsIdx[olsIdx];
            for (int lsLayerIdx = 0; lsLayerIdx < vps->NumLayersInIdList[lsIdx]; lsLayerIdx++) {
                NecessaryLayerFlag[olsIdx][lsLayerIdx ] = 0;
            }
            for (int lsLayerIdx = 0; lsLayerIdx < vps->NumLayersInIdList[lsIdx]; lsLayerIdx++ ) {
                if (vps_ext->output_layer_flag[olsIdx][lsLayerIdx]) {
                    NecessaryLayerFlag[olsIdx][lsLayerIdx] = 1;
                    int currLayerId = vps->LayerSetLayerIdList[lsIdx][lsLayerIdx];
                    for (int rLsLayerIdx = 0; rLsLayerIdx < lsLayerIdx; rLsLayerIdx++) {
                        int refLayerId = vps->LayerSetLayerIdList[lsIdx][rLsLayerIdx];
                        if (DependencyFlag[vps->LayerIdxInVps[currLayerId]][vps->LayerIdxInVps[refLayerId]]) {
                            NecessaryLayerFlag[olsIdx][rLsLayerIdx] = 1;
                        }
                    }
                }
            }
            NumNecessaryLayers[olsIdx] = 0;
            for (int lsLayerIdx = 0; lsLayerIdx < vps->NumLayersInIdList[lsIdx]; lsLayerIdx++) {
                NumNecessaryLayers[olsIdx] += NecessaryLayerFlag[olsIdx][lsLayerIdx];
            }
        }

        vps_ext->profile_tier_level_idx[i] = malloc(vps->NumLayersInIdList[OlsIdxToLsIdx[i]] * sizeof(uint16_t));
        for (int j = 0; j < vps->NumLayersInIdList[OlsIdxToLsIdx[i]]; j++ ) {
            if (NecessaryLayerFlag[i][j] && vps_ext->vps_num_profile_tier_level_minus1 > 0) {
                vps_ext->profile_tier_level_idx[i][j] = READ_BITS(v, log2ceil(vps_ext->vps_num_profile_tier_level_minus1 + 1));
            }
        }
        if( NumOutputLayersInOutputLayerSet[i] == 1 && vps->NumDirectRefLayers[OlsHighestOutputLayerId[i]] > 0 ) {
            vps_ext->alt_output_layer_flag[i] = READ_BIT(v);
        }
    }
    vps_ext->vps_num_rep_formats_minus1 = GOL_UE(v);
    vps_ext->vps_rep_format = malloc(sizeof(struct vps_rep_format) * vps_ext->vps_num_rep_formats_minus1);
    for(int i = 0; i <= vps_ext->vps_num_rep_formats_minus1; i++ ) {
        parse_rep_format(v, vps_ext->vps_rep_format + i);
    }
    uint8_t rep_format_idx_present_flag = 0;
    if (vps_ext->vps_num_rep_formats_minus1 > 0 ) {
        rep_format_idx_present_flag = READ_BIT(v);
    }
    if (rep_format_idx_present_flag) {
        vps_ext->vps_rep_format_idx = malloc( sizeof(uint16_t) *(MaxLayersMinus1 +1));
        for (int i = vps->vps_base_layer_internal_flag ? 1 : 0; i <= MaxLayersMinus1; i++ ) {
            vps_ext->vps_rep_format_idx[i] = READ_BITS(v, log2ceil(vps_ext->vps_num_rep_formats_minus1 + 1));
        }
    }
    vps_ext->max_one_active_ref_layer_flag = READ_BIT(v);
    vps_ext->vps_poc_lsb_aligned_flag = READ_BIT(v);
    vps_ext->poc_lsb_not_present_flag = malloc(MaxLayersMinus1+1);
    for (int i = 1; i <= MaxLayersMinus1; i++) {
        if (vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]] == 0 ) {
            vps_ext->poc_lsb_not_present_flag[i] = READ_BIT(v);
        }
    }

    vps_ext->dpb_size = malloc(sizeof(struct dpb_size));
    parse_dpb_size(v, vps_ext->dpb_size, vps, NumOutputLayerSets, OlsIdxToLsIdx,
         MaxSubLayersInLayerSetMinus1, NecessaryLayerFlag);

    vps_ext->direct_dep_type_len_minus2 = GOL_UE(v);
    // uint8_t direct_dependency_all_layers_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        vps_ext->direct_dependency_all_layers_type = READ_BITS(v, vps_ext->direct_dep_type_len_minus2 + 2);
    } else {
        vps_ext->direct_dependency_type = malloc(sizeof(uint32_t *) * (MaxLayersMinus1+1));
        for (int i = vps->vps_base_layer_internal_flag ? 1 : 2; i <= MaxLayersMinus1; i++ ) {
            vps_ext->direct_dependency_type[i] = malloc(i * sizeof(uint32_t));
            for (int j = vps->vps_base_layer_internal_flag ? 0 : 1; j < i; j++ ) {
                if ((vps_ext->direct_dependency_flag[i]>>j) &0x1) {
                    vps_ext->direct_dependency_type[i][j] = READ_BITS(v, vps_ext->direct_dep_type_len_minus2 + 2);
                }
            }
        }
        /* modification from I.7.4.3.1.1*/
    }
    /* see I-7 */
    int idx = 0;
    vps->ViewOIdxList[idx++] = 0;
    for (int i = 0; i <= MaxLayersMinus1; i ++) {
        int lId = vps->vps_ext->layer_id_in_nuh[i];
        int newViewFlag = 1;
        for (int j = 0; j < i; j++) {
            if (vps->ViewOrderIdx[vps->vps_ext->layer_id_in_nuh[i]] ==
                vps->ViewOrderIdx[vps->vps_ext->layer_id_in_nuh[j]]) {
                newViewFlag = 0;
            }
        }
        if (newViewFlag) {
            vps->ViewOIdxList[idx++] = vps->ViewOrderIdx[lId];
        }
    }


    uint32_t vps_non_vui_extension_length = GOL_UE(v);
    vps_ext->vps_non_vui_extension_data_byte = malloc(vps_non_vui_extension_length);
    for(int i = 0; i < vps_non_vui_extension_length; i++) {
        vps_ext->vps_non_vui_extension_data_byte[i] = READ_BITS(v, 8);
    }
    // uint8_t vps_vui_present_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        while(!BYTE_ALIGNED(v)) {
            SKIP_BITS(v, 1);
        }
        vps_ext->vps_vui = malloc(sizeof(struct vps_vui));
        parse_vps_vui(v, vps_ext->vps_vui, vps, NumLayerSets,
            MaxSubLayersInLayerSetMinus1, NumOutputLayerSets,
            OlsIdxToLsIdx, SubPicHrdFlag, vps->NumDirectRefLayers, IdDirectRefLayer);
    }

    while (more_rbsp_data(v)) {
        SKIP_BITS(v, 1);
    }
}

/* see  7.3.2.1.7 */
static void
parse_vps_3d_extension(struct bits_vec *v, struct vps *vps, struct vps_3d_extension *vps_3d_ext)
{
    int i, j;
    vps_3d_ext->cp_precision = GOL_UE(v);
    for (int n = 1; n < vps->vps_ext->NumViews; n ++) {
        i = vps->ViewOIdxList[n];
        vps_3d_ext->cp[i].num_cp = READ_BITS(v, 6);
        if (vps_3d_ext->cp[i].num_cp) {
            vps_3d_ext->cp[i].cp_in_slice_segment_header_flag = READ_BIT(v);
            for (int m = 0; m < vps_3d_ext->cp[i].num_cp; m++) {
                vps_3d_ext->cp[i].cp_ref_voi[m] = GOL_UE(v);
                if (vps_3d_ext->cp[i].cp_in_slice_segment_header_flag) {
                    j = vps_3d_ext->cp[i].cp_ref_voi[m];
                    vps_3d_ext->cp[i].vps_cp_scale[j] = GOL_SE(v);
                    vps_3d_ext->cp[i].vps_cp_off[j] = GOL_SE(v);
                    vps_3d_ext->cp[i].vps_cp_inv_scale_plus_scale[j] = GOL_SE(v);
                    vps_3d_ext->cp[i].vps_cp_inv_off_plus_off[j] = GOL_SE(v);
                }
                /* see (I-12) */
                vps->CpPresentFlag[i][vps_3d_ext->cp[i].cp_ref_voi[m]] = 1;
            }
        }
    }
}

//video parameter set F.7.3.2.1 , I.7.3.2.1
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
    vps->vps_max_sub_layers_minus1 = READ_BITS(v, 3);
    vps->vps_temporal_id_nesting_flag = READ_BIT(v);

    uint16_t vps_reserved_0xffff_16bits = READ_BITS(v, 16);
    if (vps_reserved_0xffff_16bits != 0xFFFF) {
        printf("reserved bits 0x%x\n", vps_reserved_0xffff_16bits);
    }

    parse_profile_tier_level(v, &vps->vps_profile_tier_level, 1, vps->vps_max_sub_layers_minus1);
    printf("vps_max_sub_layers_minus1 %d\n", vps->vps_max_sub_layers_minus1);
    vps->vps_sub_layer_ordering_info_present_flag = READ_BIT(v);

    for (int i = vps->vps_sub_layer_ordering_info_present_flag ? 0: vps->vps_max_sub_layers_minus1;
            i <= vps->vps_max_sub_layers_minus1; i ++) {
        vps->vps_sublayers[i].vps_max_dec_pic_buffering_minus1 = GOL_UE(v);
        vps->vps_sublayers[i].vps_max_num_reorder_pics = GOL_UE(v);
        vps->vps_sublayers[i].vps_max_latency_increase_plus1 = GOL_UE(v);
    }

    vps->vps_max_layer_id = READ_BITS(v, 6);
    vps->vps_num_layer_sets_minus1 = GOL_UE(v);

    printf("vps_num_layer_sets_minus1 %d\n", vps->vps_num_layer_sets_minus1);

    for (int i = 1; i <= vps->vps_num_layer_sets_minus1; i ++) {
        int n = 0;
        for (int j = 0; j <= vps->vps_max_layer_id; j ++) {
            uint8_t layer_id_included_flag = READ_BIT(v);
            vps->layer_id_included_flag[i] |= (layer_id_included_flag << j);
            //(7-3)
            if (layer_id_included_flag) {
                vps->LayerSetLayerIdList[i][n++] = j; 
            }
        }
        vps->NumLayersInIdList[i] = n;
    }
    // vps->vps_timing_info_present_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        vps->vps_timing_info = malloc(sizeof(struct vps_timing_info));
        parse_vps_timing_info(v, vps, vps->vps_timing_info);
    }

    // vps->vps_extension_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        while (!BYTE_ALIGNED(v)) {
            SKIP_BITS(v, 1);
        }
        vps->vps_ext = malloc(sizeof(struct vps_extension));
        parse_vps_extension(v, vps, vps->vps_ext);
        /* 3d extension flag */
        if (READ_BIT(v)) {
            while (!BYTE_ALIGNED(v)) {
                SKIP_BITS(v, 1);
            }
            vps->vps_3d_ext = malloc(sizeof(struct vps_3d_extension));
            parse_vps_3d_extension(v, vps, vps->vps_3d_ext);
        }
        /* vps_extension3_flag */
        if (READ_BIT(v)) {
            while (more_rbsp_data(v)) {
                SKIP_BITS(v, 1);
            }
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
    slice->ref_pic_list_modification_flag_l0 = READ_BIT(v);
    if (slice->ref_pic_list_modification_flag_l0) {
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
calc_sps_param(struct sps *sps, struct slice_segment_header *slice)
{
        //see table 6-1
    static int subwidth[4] = {1, 2, 2, 1};
    static int subheight[4] = {1, 2, 1, 1};
    slice->SubWidthC = subwidth[sps->chroma_format_idc];
    slice->SubHeightC = subheight[sps->chroma_format_idc];
    if (sps->chroma_format_idc == 3 && sps->separate_colour_plane_flag) {
        slice->SubWidthC = 1;
        slice->SubHeightC = 1;
    }
    //see 7-10
    slice->MinCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus3 + 3;
    //see 7-11
    slice->CtbLog2SizeY = slice->MinCbLog2SizeY + sps->log2_diff_max_min_luma_coding_block_size;
    //see 7-12
    slice->MinCbSizeY = 1 << slice->MinCbLog2SizeY;
    //see 7-13
    slice->CtbSizeY = 1 << slice->CtbLog2SizeY;
    //see 7-14
    slice->PicWidthInMinCbsY = sps->pic_width_in_luma_samples / slice->MinCbSizeY;
    //see 7-15
    slice->PicWidthInCtbsY = divceil(sps->pic_width_in_luma_samples, slice->CtbSizeY);
    //see 7-16
    slice->PicHeightInMinCbsY = sps->pic_height_in_luma_samples / slice->MinCbSizeY;
    //see 7-17
    slice->PicHeightInCtbsY = divceil(sps->pic_height_in_luma_samples, slice->CtbSizeY);
    //see 7-18
    slice->PicSizeInMinCbsY = slice->PicWidthInMinCbsY * slice->PicHeightInMinCbsY;
    //see 7-19
    slice->PicSizeInCtbsY = slice->PicWidthInCtbsY * slice->PicHeightInCtbsY;
    //see 7-20
    slice->PicSizeInSamplesY = sps->pic_width_in_luma_samples * sps->pic_height_in_luma_samples;
    //see 7-21
    slice->PicWidthInSamplesC = sps->pic_width_in_luma_samples / slice->SubWidthC;
    //see 7-22
    slice->PicHeightInSamplesC = sps->pic_height_in_luma_samples / slice->SubHeightC;
    //see 7-23, 7-24
    if (sps->chroma_format_idc == 0 || sps->separate_colour_plane_flag == 1) {
        slice->CtbWidthC = 0;
        slice->CtbHeightC = 0;
    } else {
        slice->CtbWidthC = slice->CtbSizeY / slice->SubWidthC;
        slice->CtbHeightC = slice->CtbSizeY / slice->SubHeightC;
    }
    slice->MinTbLog2SizeY = sps->log2_min_luma_transform_block_size_minus2 + 2;
    slice->MaxTbLog2SizeY = sps->log2_min_luma_transform_block_size_minus2 + 2
                        + sps->log2_diff_max_min_luma_transform_block_size;
    slice->ChromaArrayType = (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc);

}


static int
parse_slice_segment_header(struct hevc_nalu_header *headr, uint8_t *data, uint16_t len,
        struct hevc_param_set * hps, uint32_t *SliceAddrRs)
{
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;

    uint8_t slice_temporal_mvp_enabled_flag = 0;
    struct slice_segment_header *slice = malloc(sizeof(*slice));
    struct bits_vec *v = bits_vec_alloc(data, len, BITS_MSB);
    uint8_t first_slice_segment_in_pic_flag = READ_BIT(v);
    if (headr->nal_unit_type >= BLA_W_LP && headr->nal_unit_type <= RSV_IRAP_VCL23) {
        slice->no_output_of_prior_pics_flag = READ_BIT(v);
    }
    slice->slice_pic_parameter_set_id = GOL_UE(v);
    printf("first_slice_segment_in_pic_flag %d\n", first_slice_segment_in_pic_flag);
    printf("slice_pic_parameter_set_id %d\n", slice->slice_pic_parameter_set_id);
    printf("no_output_of_prior_pics_flag %d\n", slice->no_output_of_prior_pics_flag);

    calc_sps_param(sps, slice);
    uint8_t dependent_slice_segment_flag = 0;
    if (!first_slice_segment_in_pic_flag) {
        if (pps->dependent_slice_segments_enabled_flag) {
            dependent_slice_segment_flag = READ_BIT(v);
        }
        slice->slice_segment_address = READ_BITS(v, log2ceil(slice->PicSizeInCtbsY));
    }
    slice->CuQpDeltaVal = 0;
    if (!dependent_slice_segment_flag) {
        //see 7.4.7.1 dependent_slice_segment_flag
        *SliceAddrRs = slice->slice_segment_address;
        //FIXME

        // int i = 0;
        // if (pps->num_extra_slice_header_bits > i) {
        //     i ++;
        //     // uint8_t discardable_flag = READ_BIT(v);
        // }
        // if (pps->num_extra_slice_header_bits > i) {
        //     i ++;
        //     uint8_t cross_layer_bla_flag = READ_BIT(v);
        // }
        // for (int i = 0; i < pps->num_extra_slice_header_bits; i++ ){
        //     // slice_reserved_flag[ i ] = READ_BIT(v);
        //     SKIP_BITS(v, 1);
        // }
        if (pps->num_extra_slice_header_bits)
            SKIP_BITS(v, pps->num_extra_slice_header_bits);
        slice->slice_type = GOL_UE(v);
        printf("slice_type %d\n", slice->slice_type);
        if (pps->output_flag_present_flag) {
            slice->pic_output_flag = READ_BIT(v);
        }
        if (sps->separate_colour_plane_flag == 1 ) {
            slice->colour_plane_id = READ_BITS(v, 2);
        }
        if (vps->vps_ext && headr->nuh_layer_id > 0 && !vps->vps_ext->poc_lsb_not_present_flag[vps->LayerIdxInVps[headr->nuh_layer_id]] ||
            (headr->nal_unit_type!= IDR_W_RADL && headr->nal_unit_type != IDR_N_LP)) {
            slice->slice_pic_order_cnt_lsb = READ_BITS(v, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
            printf("slice_pic_order_cnt_lsb %d\n", slice->slice_pic_order_cnt_lsb);
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
                        PocLsbLt[i] = sps->sps_lt_ref->lt_ref_pic_poc_lsb_sps[slice->terms[i].lt_idx_sps];
                        UsedByCurrPicLt[i] = sps->sps_lt_ref->used_by_curr_pic_lt_sps_flag[slice->terms[i].lt_idx_sps];
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
        if (vps->vps_ext) {
            if (headr->nuh_layer_id > 0 && !vps->vps_ext->default_ref_layers_active_flag &&
                    vps->NumDirectRefLayers[headr->nuh_layer_id] > 0) {
                slice->inter_layer_pred_enabled_flag = READ_BIT(v);
                if (slice->inter_layer_pred_enabled_flag && vps->NumDirectRefLayers[headr->nuh_layer_id] > 1) {
                    if (!vps->vps_ext->max_one_active_ref_layer_flag) {
                        slice->num_inter_layer_ref_pics_minus1 = READ_BITS(v, log2ceil(vps->NumRefListLayers[headr->nuh_layer_id]));
                    }
                    //see I-32, F-52
                    int j = 0;
                    // int refLayerPicIdc[64];
                    for(int i = 0; i < vps->NumRefListLayers[headr->nuh_layer_id]; i++ ) {
                        int TemporalId = headr->nuh_temporal_id_plus1 - 1;
                        int refLayerIdx = vps->LayerIdxInVps[vps->IdRefListLayer[headr->nuh_layer_id][i]];
                        if( vps->vps_ext->sub_layers_vps_max_minus1[refLayerIdx] >= TemporalId && 
                            ( TemporalId == 0 || vps->vps_ext->max_tid_il_ref_pics_plus1[refLayerIdx][vps->LayerIdxInVps[headr->nuh_layer_id]] > TemporalId))
                            slice->refLayerPicIdc[j++] = i;
                    }
                    slice->numRefLayerPics = j;
                    //see I-33, F-53
                    // int NumActiveRefLayerPics;
                    if (headr->nuh_layer_id == 0 || slice->numRefLayerPics == 0)
                        slice->NumActiveRefLayerPics = 0;
                    else if (vps->vps_ext->default_ref_layers_active_flag )
                        slice->NumActiveRefLayerPics = slice->numRefLayerPics;
                    else if (!slice->inter_layer_pred_enabled_flag)
                        slice->NumActiveRefLayerPics = 0;
                    else if (vps->vps_ext->max_one_active_ref_layer_flag || vps->NumRefListLayers[headr->nuh_layer_id ] == 1 )
                        slice->NumActiveRefLayerPics = 1;
                    else
                        slice->NumActiveRefLayerPics = slice->num_inter_layer_ref_pics_minus1 + 1;
                    if (slice->NumActiveRefLayerPics != vps->NumDirectRefLayers[headr->nuh_layer_id]) {
                        slice->inter_layer_pred_layer_idc = malloc(sizeof(uint32_t) * slice->NumActiveRefLayerPics);
                        for (int i = 0; i < slice->NumActiveRefLayerPics; i++) {
                            slice->inter_layer_pred_layer_idc[i] = READ_BITS(v, log2ceil(vps->NumRefListLayers[headr->nuh_layer_id]));
                        }
                    }
                }
            }
        }
        //see I.7.4.7.1
        int DepthFlag = vps->DepthLayerFlag[headr->nuh_layer_id];
        //see I.7.4.7.1
        int inCmpPredAvailFlag = 0;
        int allRefCmpLayersAvailFlag = 1;
        if (allRefCmpLayersAvailFlag) {
            if (DepthFlag == 0) {
                //see (I-17)
                inCmpPredAvailFlag = sps->sps_3d_ext[DepthFlag].vsp_mc_enabled_flag ||
                                     sps->sps_3d_ext[DepthFlag].dbbp_enabled_flag ||
                                     sps->sps_3d_ext[DepthFlag].depth_ref_enabled_flag;
            } else {
                //see (I-18)
                inCmpPredAvailFlag = sps->sps_3d_ext[DepthFlag].intra_contour_enabled_flag ||
                                     sps->sps_3d_ext[DepthFlag].cqt_cu_part_pred_enabled_flag ||
                                     sps->sps_3d_ext[DepthFlag].tex_mc_enabled_flag;
            }
        }
        if (inCmpPredAvailFlag) {
            slice->in_comp_pred_flag = READ_BIT(v);
        }
        slice->ChromaArrayType = (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc);
        // uint8_t slice_sao_luma_flag = 0;
        // uint8_t slice_sao_chroma_flag = 0;
        if (sps->sample_adaptive_offset_enabled_flag) {
            slice->slice_sao_luma_flag = READ_BIT(v);
            if (slice->ChromaArrayType != 0) {
                slice->slice_sao_chroma_flag = READ_BIT(v);
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
            if (pps->pps_scc_ext && pps->pps_scc_ext->pps_curr_pic_ref_enabled_flag) {
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
                // parse_pred_weight_table();
            } else if (!DepthFlag && vps->NumRefListLayers[headr->nuh_layer_id] > 0) {
                slice->slice_ic_enabled_flag = READ_BIT(v);
                if (slice->slice_ic_enabled_flag) {
                    uint8_t slice_ic_disabled_merge_zero_idx_flag = READ_BIT(v);
                }
            }
            slice->five_minus_max_num_merge_cand = GOL_UE(v);
            if (sps->sps_scc_ext && sps->sps_scc_ext->motion_vector_resolution_control_idc == 2) {
                slice->use_integer_mv_flag = READ_BIT(v);
            }
        }
        slice->slice_qp_delta = GOL_SE(v);
        if (pps->pps_slice_chroma_qp_offsets_present_flag) {
            slice->slice_cb_qp_offset = GOL_SE(v);
            slice->slice_cr_qp_offset = GOL_SE(v);
            printf("slice_cb_qp_offset %d\n", slice->slice_cb_qp_offset);
            printf("slice_cr_qp_offset %d\n", slice->slice_cr_qp_offset);
        }
        if (pps->pps_scc_ext && pps->pps_scc_ext->pps_slice_act_qp_offsets_present_flag) {
            slice->slice_act_y_qp_offset = GOL_SE(v);
            slice->slice_act_cb_qp_offset = GOL_SE(v);
            slice->slice_act_cr_qp_offset = GOL_SE(v);
        }
        if (pps->pps_range_ext && pps->pps_range_ext->chroma_qp_offset_list_enabled_flag) {
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
                ( slice->slice_sao_luma_flag || slice->slice_sao_chroma_flag ||
                !slice_deblocking_filter_disabled_flag )) {
            slice->slice_loop_filter_across_slices_enabled_flag = READ_BIT(v);
        }
    }
    if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag) {
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
        if (pps->pps_multilayer_ext->poc_reset_info_present_flag ) {
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
        if (poc_msb_cycle_val_present_flag) {
            slice->poc_msb_cycle_val = GOL_UE(v);
        }
        while (more_data_in_slice_segment_header_extension(cur_pos, v, slice)) {
            // slice_segment_header_extension_data_bit = READ_BIT(v);
            SKIP_BITS(v, 1);
        }
    }

    byte_alignment(v);
    // hexdump(stdout, "left", "", v->ptr, 32);
    bits_vec_free(v);
    return v->ptr - v->start;
}


static void
parse_intra_mode_ext(struct bits_vec *v, struct hevc_slice *hslice, struct intra_mode_ext *ext,
                     struct hevc_param_set * hps, int x0, int y0, int log2PbSize)
{
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct hevc_nalu_header *h = hslice->nalu;
    struct slice_segment_header *slice = hslice->slice;

    if (log2PbSize < 6) {
        ext->no_dim_flag = CABAC(v);
    }
    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[h->nuh_layer_id];
    //see I-25
    int IntraContourEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_contour_enabled_flag && slice->in_comp_pred_flag;
    //see I-26
    int IntraDcOnlyWedgeEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_dc_only_wedge_enabled_flag;
    if (!ext->no_dim_flag && IntraDcOnlyWedgeEnabledFlag && IntraContourEnabledFlag) {
        ext->depth_intra_mode_idx_flag = CABAC(v);
    }
    if (!ext->no_dim_flag && !ext->depth_intra_mode_idx_flag) {
        ext->wedge_full_tab_idx = CABAC(v);
    }
}

static int
PicOrderCnt(struct slice_segment_header *slice, struct sps *sps, int picX)
{
    int prevTid0Pic = 0;//has TemporalId == 0 and not RASL, RADL, SLNR
    int PicOrderCntMsb;

    //see 8.3.1
    int prevPicOrderCntMsb = 0;// = PicOrderCntMsb;
    int prevPicOrderCntLsb = 0;
    //see (7-8)
    int MaxPicOrderCntLsb = 2 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    //see (8-1)
    if ((slice->slice_pic_order_cnt_lsb < prevPicOrderCntLsb) &&
        ((prevPicOrderCntLsb - slice->slice_pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2))) {
        PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
    } else if ((slice->slice_pic_order_cnt_lsb > prevPicOrderCntLsb ) &&
        ((slice->slice_pic_order_cnt_lsb - prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2))) {
        PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
    } else {
        PicOrderCntMsb = prevPicOrderCntMsb;
    }
    //see (8-2)
    int PicOrderCntVal = PicOrderCntMsb + slice->slice_pic_order_cnt_lsb;
    return PicOrderCntVal;
}

static int
DiffPicOrderCnt(struct slice_segment_header *slice, struct sps *sps, int picA, int picB)
{
    return PicOrderCnt(slice, sps, picA) - PicOrderCnt(slice, sps, picB);
}

static void
parse_cu_extension(struct bits_vec *v, struct hevc_slice *hslice,
                    struct cu_extension *ext, struct cu *cu, struct hevc_param_set * hps,
                    int x0, int y0, int log2CbSize, int NumPicTotalCurr)
{
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;

    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[headr->nuh_layer_id];
    //see (I-23)
    int DbbpEnabledFlag = sps->sps_3d_ext[DepthFlag].dbbp_enabled_flag && slice->in_comp_pred_flag;

#if 0
    int NumRpsCurrTempList0 = MAX(slice->num_ref_idx_l0_active_minus1 + 1, NumPicTotalCurr);
    //see (8-8)/ f-65
    int rIdx = 0;
    while (rIdx < NumRpsCurrTempList0) {
        for (int i = 0; i < NumPocStCurrBefore && rIdx < NumRpsCurrTempList0; rIdx++, i++ )
            RefPicListTemp0[rIdx] = RefPicSetStCurrBefore[i];
        for (int i = 0; i < NumPocStCurrAfter && rIdx < NumRpsCurrTempList0; rIdx++, i++ )
            RefPicListTemp0[rIdx] = RefPicSetStCurrAfter[i];
        for (int i = 0; i < NumPocLtCurr && rIdx < NumRpsCurrTempList0; rIdx++, i++ )
            RefPicListTemp0[rIdx] = RefPicSetLtCurr[i];
        if (pps->pps_scc_ext->pps_curr_pic_ref_enabled_flag)
            RefPicListTemp0[rIdx++] = currPic;
    }
    //see (8-9) / f-66
    for (rIdx = 0; rIdx <= slice->num_ref_idx_l0_active_minus1; rIdx++)
        RefPicList0[rIdx] = slice->ref_pic_list_modification_flag_l0 ? RefPicListTemp0[list_entry_l0[rIdx]] : RefPicListTemp0[rIdx];
    if (pps->pps_scc_ext->pps_curr_pic_ref_enabled_flag && !slice->ref_pic_list_modification_flag_l0 &&
            NumRpsCurrTempList0 > (slice->num_ref_idx_l0_active_minus1 + 1));
        RefPicList0[slice->num_ref_idx_l0_active_minus1] = currPic;

    //see I.8.3.5
    //for x in the range of 0 to 1, inclusive, the following: P
    int RpRefIdxL0 = -1, RpRefIdxL1 = -1;
    int RpRefPicAvailFlagL0 = 0, RpRefPicAvailFlagL1 = 0;
    for (int x = 0; x <= ((slice->slice_type == SLICE_TYPE_B) ? 1 : 0); x++) {
        int minPocDiff = (1 << 15) - 1;
        if (x == 0) {
            for (int i = 0; i <= slice->num_ref_idx_l0_active_minus1; i ++) {
                int pocDiff = ABS(DiffPicOrderCnt(slice, sps, CurrPic, RefPicList0[i]));
                if (pocDiff !=0 && pocDiff < minPocDiff) {
                    minPocDiff = pocDiff;
                    RpRefIdxL0 = 0;
                    RpRefPicAvailFlagL0 = 1;
                }
            }
        } else if (x == 1) {
            for (int i = 0; i <= slice->num_ref_idx_l1_active_minus1; i ++) {
                int pocDiff = ABS(DiffPicOrderCnt(slice, sps, CurrPic, RefPicList1[i]));
                if (pocDiff !=0 && pocDiff < minPocDiff) {
                    minPocDiff = pocDiff;
                    RpRefIdxL1 = 0;
                    RpRefPicAvailFlagL1 = 1;
                }
            }
        }
    }
    //see I-29
    int InterDcOnlyEnabledFlag = sps->sps_3d_ext[DepthFlag].inter_dc_only_enabled_flag;
    //see I-21
    int IvResPredEnabledFlag = vps->NumRefListLayers[headr->nuh_layer_id] > 0 && sps->sps_3d_ext[DepthFlag].iv_res_pred_enabled_flag;
    //see 
    int IntraDcOnlyWedgeEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_dc_only_wedge_enabled_flag;

    int DispAvailFlag = 1;//TODO

    //see (I-48)
    int icCuEnableFlag;
    if (merge_flag == 1) {
        icCuEnableFlag = ((merge_idx != 0) || !slice_ic_disabled_merge_zero_idx_flag);
    } else {
        icCuEnableFlag = (inter_pred_idc != PRED_L0 && refViewIdxL1 != ViewIdx) ||
            (inter_pred_idc != PRED_L1 && refViewIdxL0 != ViewIdx);
    }
    //see (I-61)
    int RpRefPicAvailFlag = (RpRefPicAvailFlagL0 || RpRefPicAvailFlagL1) && DispAvailFlag;
    if (cu->skip_intra_flag) {
        ext->skip_intra_mode_idx = CABAC(v);
    } else {
        if (!cu->cu_skip_flag[x0][y0]) {
            if (DbbpEnabledFlag && DispAvailFlag && log2CbSize > 3 &&
                (cu->PartMode == PART_2NxN || cu->PartMode == PART_Nx2N)) {
                ext->dbbp_flag = CABAC(v);
            }
            if ((cu->CuPredMode[x0][y0] == MODE_INTRA ? IntraDcOnlyWedgeEnabledFlag :
                InterDcOnlyEnabledFlag) && cu->PartMode == PART_2Nx2N) {
                ext->dc_only_flag = CABAC(v);
            }
        }
        if (cu->CuPredMode[x0][y0] != MODE_INTRA && cu->PartMode == PART_2Nx2N) {
            if (IvResPredEnabledFlag && RpRefPicAvailFlag ) {
                ext->iv_res_pred_weight_idx = CABAC(v);
            }
            if (slice->slice_ic_enabled_flag && icCuEnableFlag &&
                ext->iv_res_pred_weight_idx == 0) {
                ext->illu_comp_flag = CABAC(v);
            }
        }
    }
#endif
}

static void
parse_sao(struct bits_vec *v, struct slice_segment_header *slice, struct sao *sao, uint32_t ***SaoTypeIdx, uint32_t rx, uint32_t ry,
    uint32_t *CtbAddrRsToTs, uint32_t CtbAddrInTs, uint32_t CtbAddrInRs, uint32_t SliceAddrRs,
    uint32_t *TileId)
{
    if (rx > 0 ) {
        int leftCtbInSliceSeg = CtbAddrInRs > SliceAddrRs;
        int leftCtbInTile = TileId[CtbAddrInTs] == TileId[CtbAddrRsToTs[CtbAddrInRs - 1]];
        if (leftCtbInSliceSeg && leftCtbInTile) {
            sao->sao_merge_left_flag = CABAC(v);
        }
    }
    if (ry > 0 && !sao->sao_merge_left_flag) {
        int upCtbInSliceSeg = (CtbAddrInRs - slice->PicWidthInCtbsY) >= SliceAddrRs;
        int upCtbInTile = (TileId[CtbAddrInTs] ==
                TileId[CtbAddrRsToTs[CtbAddrInRs - slice->PicWidthInCtbsY]]);
        if (upCtbInSliceSeg && upCtbInTile) {
            sao->sao_merge_up_flag = CABAC(v);
        }
    }
    if (!sao->sao_merge_up_flag && !sao->sao_merge_left_flag) {
        for (int cIdx = 0; cIdx < (slice->ChromaArrayType != 0 ? 3 : 1 ); cIdx++) {
            if ((slice->slice_sao_luma_flag && cIdx == 0) ||
                (slice->slice_sao_chroma_flag && cIdx > 0)) {
                if (cIdx == 0) {
                    sao->sao_type_idx_luma = CABAC(v);
                    SaoTypeIdx[cIdx][rx][ry] = sao->sao_type_idx_luma;
                } else if (cIdx == 1) {
                    sao->sao_type_idx_chroma = CABAC(v);
                    SaoTypeIdx[cIdx][rx][ry] = sao->sao_type_idx_chroma;
                }
                if (SaoTypeIdx[cIdx][rx][ry] != 0 ) {
                    for (int i = 0; i < 4; i++) {
                        sao->sao_offset_abs[cIdx][rx][ry][i] = CABAC(v);
                    }
                    if (SaoTypeIdx[cIdx][rx][ry] == 1) {
                        for (int i = 0; i < 4; i++ ) {
                            if (sao->sao_offset_abs[cIdx][rx][ry][i] != 0 ) {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = CABAC(v);
                            } else {
                                if (sao->sao_merge_left_flag) {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx-1][ry][i];
                                } else if (sao->sao_merge_up_flag) {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx][ry-1][i];
                                } else if (SaoTypeIdx[cIdx][rx][ry] == 2) {
                                    if (i == 0|| i == 1) {
                                        sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                                    } {
                                        sao->sao_offset_sign[cIdx][rx][ry][i] = 1;
                                    }
                                } else {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                                }
                            }
                        }
                        sao->sao_band_position[cIdx][rx][ry] = CABAC(v);
                    } else {
                        if (cIdx == 0) {
                            sao->sao_eo_class_luma = CABAC(v);
                        }
                        if (cIdx == 1) {
                            sao->sao_eo_class_chroma = CABAC(v);
                        }
                        for (int i = 0; i < 4; i++ ) {
                            if (sao->sao_merge_left_flag) {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx-1][ry][i];
                            } else if (sao->sao_merge_up_flag) {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx][ry-1][i];
                            } else if (SaoTypeIdx[cIdx][rx][ry] == 2) {
                                if (i == 0|| i == 1) {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                                } {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = 1;
                                }
                            } else {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < 4; i++) {
                        if (sao->sao_merge_left_flag) {
                            sao->sao_offset_abs[cIdx][rx][ry][i] = sao->sao_offset_abs[cIdx][rx-1][ry][i];
                            sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx-1][ry][i];
                        } else if (sao->sao_merge_up_flag) {
                            sao->sao_offset_abs[cIdx][rx][ry][i] = sao->sao_offset_abs[cIdx][rx][ry-1][i];
                            sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx][ry-1][i];
                        } else {
                            sao->sao_offset_abs[cIdx][rx][ry][i] = 0;
                            if (SaoTypeIdx[cIdx][rx][ry] == 2) {
                                if (i == 0|| i == 1) {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                                } {
                                    sao->sao_offset_sign[cIdx][rx][ry][i] = 1;
                                }
                            } else {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                            }
                        }
                    }
                }
            } else {
                //see Table 7-8
                if (sao->sao_merge_left_flag) {
                    SaoTypeIdx[cIdx][rx][ry] = SaoTypeIdx[cIdx][rx - 1][ry];
                } else if (sao->sao_merge_up_flag) {
                    SaoTypeIdx[cIdx][rx][ry] = SaoTypeIdx[cIdx][rx][ry - 1];
                }
                for (int i = 0; i < 4; i++) {
                    if (sao->sao_merge_left_flag) {
                        sao->sao_offset_abs[cIdx][rx][ry][i] = sao->sao_offset_abs[cIdx][rx-1][ry][i];
                        sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx-1][ry][i];
                    } else if (sao->sao_merge_up_flag) {
                        sao->sao_offset_abs[cIdx][rx][ry][i] = sao->sao_offset_abs[cIdx][rx][ry-1][i];
                        sao->sao_offset_sign[cIdx][rx][ry][i] = sao->sao_offset_sign[cIdx][rx][ry-1][i];
                    } else {
                        sao->sao_offset_abs[cIdx][rx][ry][i] = 0;
                        if (SaoTypeIdx[cIdx][rx][ry] == 2) {
                            if (i == 0|| i == 1) {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                            } {
                                sao->sao_offset_sign[cIdx][rx][ry][i] = 1;
                            }
                        } else {
                            sao->sao_offset_sign[cIdx][rx][ry][i] = 0;
                        }
                    }
                }
            }
        }
    }
}


static void
parse_delta_qp(struct bits_vec *v, struct slice_segment_header *slice, struct pps *pps)
{
    if (pps->cu_qp_delta_enabled_flag && !slice->IsCuQpDeltaCoded ) {
        slice->IsCuQpDeltaCoded = 1;
        uint32_t cu_qp_delta_abs = CABAC(v);
        if (cu_qp_delta_abs) {
            uint8_t cu_qp_delta_sign_flag = CABAC(v);
            slice->CuQpDeltaVal = cu_qp_delta_abs * ( 1 - 2 * cu_qp_delta_sign_flag );
        }
    }
}

/*see 7.3.8.12 */
static void
parse_cross_comp_pred(struct bits_vec *v, struct cross_comp_pred *cross, int x0, int y0, int c)
{
    cross->log2_res_scale_abs_plus1 = CABAC(v);
    if (cross->log2_res_scale_abs_plus1 != 0) {
        cross->res_scale_sign_flag = CABAC(v);
    }
}

#define PALETTE_MAX_SIZE (2048)
struct palette_coding {
    // uint32_t palette_predictor_run;
    uint8_t PalettePredictorEntryReuseFlags[PALETTE_MAX_SIZE];
    uint32_t num_signalled_palette_entries;
    uint32_t new_palette_entries[3][PALETTE_MAX_SIZE];//tobe
    uint8_t palette_escape_val_present_flag;
    uint32_t num_palette_indices_minus1;

    uint8_t copy_above_indices_for_final_run_flag;
    uint8_t palette_transpose_flag;

    uint8_t copy_above_palette_indices_flag;

    // uint32_t palette_idx_idc;
    uint32_t PaletteIndexIdc[PALETTE_MAX_SIZE];

    uint32_t palette_run_prefix;
    uint32_t palette_run_suffix;
    // uint32_t palette_escape_val;
    uint32_t PaletteEscapeVal[3][64][64];

    uint8_t CopyAboveIndicesFlag[64][64];
    uint32_t PaletteIndexMap[64][64];
};


//6.5.2


//9.3.2.3 initialization process for palette predictor entries
static void
init_palette_predictor_entries(struct slice_segment_header *slice, struct pps *pps, struct sps *sps) {
    int numComps = (slice->ChromaArrayType == 0) ? 1 : 3; //9-8
    if (pps->pps_scc_ext && pps->pps_scc_ext->pps_palette_predictor_initializers_present_flag) {
        slice->ppe.PredictorPaletteSize = pps->pps_scc_ext->pps_num_palette_predictor_initializers;
        //see (9-9)
        for (int comp = 0; comp < numComps; comp++) {
            for (int i = 0; i < slice->ppe.PredictorPaletteSize; i++ ) 
            slice->ppe.PredictorPaletteEntries[comp][i] = pps->pps_scc_ext->pps_palette_predictor_initializer[comp][i];
        }
    } else if (sps->sps_scc_ext && sps->sps_scc_ext->sps_palette_predictor_initializers_present_flag) {
        slice->ppe.PredictorPaletteSize = sps->sps_scc_ext->sps_num_palette_predictor_initializers_minus1 + 1;
        //see (9-9)
        for (int comp = 0; comp < numComps; comp++) {
            for (int i = 0; i < slice->ppe.PredictorPaletteSize; i++ ) 
            slice->ppe.PredictorPaletteEntries[comp][i] = sps->sps_scc_ext->sps_palette_predictor_initializer[comp][i];
        }
    } else {
        slice->ppe.PredictorPaletteSize = 0;
    }
}


static void
parse_chroma_qp_offset(struct bits_vec *v, struct slice_segment_header *slice,
                    struct pps *pps, struct chroma_qp_offset *qpoff)
{
    if (slice->cu_chroma_qp_offset_enabled_flag && !slice->IsCuChromaQpOffsetCoded) {
        qpoff->cu_chroma_qp_offset_flag = CABAC(v);
        slice->IsCuChromaQpOffsetCoded = 1;
        if (qpoff->cu_chroma_qp_offset_flag && pps->pps_range_ext->chroma_qp_offset_list_len_minus1 > 0) {
            qpoff->cu_chroma_qp_offset_idx = CABAC(v);
            assert(qpoff->cu_chroma_qp_offset_idx <= pps->pps_range_ext->chroma_qp_offset_list_len_minus1);
        } else {
            qpoff->cu_chroma_qp_offset_idx = 0;
        }
        if (qpoff->cu_chroma_qp_offset_flag) {
            //see (7-87)
            qpoff->CuQpOffsetCb = pps->pps_range_ext->cb_qp_offset_list[qpoff->cu_chroma_qp_offset_idx];
            //see (7-88)
            qpoff->CuQpOffsetCr = pps->pps_range_ext->cr_qp_offset_list[qpoff->cu_chroma_qp_offset_idx];
        } else{
            qpoff->CuQpOffsetCb = 0;
            qpoff->CuQpOffsetCr = 0;
        }
    }
}


/*see 6.5.3 (6-11)*/
static int **
init_up_right_scan_order(int blkSize)
{
    int i = 0;
    int x = 0, y =0;
    bool stopLoop = false;
    int **diagScan = malloc(sizeof(int *) * blkSize);
    for (int j = 0; j <blkSize; j++) {
        diagScan[j] = malloc(sizeof(int) * blkSize);
    }
    while( !stopLoop ) {
        while( y >= 0 ) {
            if( x < blkSize && y < blkSize) {
                diagScan[i][0] = x;
                diagScan[i][1] = y;
                i ++;
            }
            y--;
            x++;
        }
        y = x;
        x = 0;
        if (i >= blkSize *blkSize) {
            stopLoop = true;
        }
    }
    return diagScan;
}

/*see 6.5.4 (6-12)*/
static int **
init_horizontal_scan_order(int blkSize)
{
    int i =0;
    int **horScan = malloc(sizeof(int *) * blkSize);
    for (int j = 0; j <blkSize; j++) {
        horScan[j] = malloc(sizeof(int) * blkSize);
    }
    for (int y = 0; y < blkSize; y ++) {
        for (int x = 0; x < blkSize; x ++) {
            horScan[i][0] = x;
            horScan[i][1] = y;
            i ++;
        }
    }
    return horScan;
}

/*see 6.5.5 (6-13)*/
static int **
init_vertical_scan_order(int blkSize)
{
    int i =0;
    int **verScan = malloc(sizeof(int *) * blkSize);
    for (int j = 0; j <blkSize; j++) {
        verScan[j] = malloc(sizeof(int) * blkSize);
    }
    for (int x = 0; x < blkSize; x ++) {
        for (int y = 0; y < blkSize; y ++) {
            verScan[i][0] = x;
            verScan[i][1] = y;
            i ++;
        }
    }
    return verScan;
}

/*see 6.5.6 (6-14)*/
static int **
init_traverse_scan_order(int blkSize)
{
    int i = 0;
    int **travScan = malloc(sizeof(int *) * blkSize);
    for (int j = 0; j <blkSize; j++) {
        travScan[j] = malloc(sizeof(int) * blkSize);
    }
    for (int y = 0; y < blkSize; y++) {
        if (y % 2 == 0) {
            for (int x = 0; x < blkSize; x++ ) {
                travScan[i][0] = x;
                travScan[i][1] = y;
                i ++;
            }
        } else {
            for (int x = blkSize - 1; x >= 0; x--) {
                travScan[i][0] = x;
                travScan[i][1] = y;
                i ++;
            }
        }
    }
    return travScan;
}


//see 8.4.4.2.7 Decoding process for palette mode, 
//also see 7.3.8.13 Palette syntax
static void
parse_palette_coding(struct bits_vec *v, struct palette_coding* pc, struct cu *cu,
    struct slice_segment_header *slice, struct pps *pps, struct sps *sps, int x0, int y0, int nCbS)
{
    int numComps = (slice->ChromaArrayType == 0) ? 1 : 3;

    int palettePredictionFinished = 0;
    int NumPredictedPaletteEntries = 0;
    int PredictorPaletteSize = slice->ppe.PredictorPaletteSize;

    for (int predictorEntryIdx = 0; predictorEntryIdx < PredictorPaletteSize &&
        !palettePredictionFinished && NumPredictedPaletteEntries < sps->sps_scc_ext->palette_max_size;
        predictorEntryIdx++) {
        int palette_predictor_run = CABAC(v);
        if (palette_predictor_run != 1) {
            if (palette_predictor_run > 1) {
                predictorEntryIdx += palette_predictor_run - 1;
            }
            pc->PalettePredictorEntryReuseFlags[predictorEntryIdx] = 1;
            NumPredictedPaletteEntries ++;
        } else {
            palettePredictionFinished = 1;
        }
    }
    if (NumPredictedPaletteEntries < sps->sps_scc_ext->palette_max_size) {
        pc->num_signalled_palette_entries = CABAC(v);
    }

    int CurrentPaletteEntries[3][128];
    int newPredictorPaletteEntries[3][128];

    //see (7-81) // between 0 , palette_max_size
    int CurrentPaletteSize = NumPredictedPaletteEntries + pc->num_signalled_palette_entries;
    //see 7-82 
    for (int i = 0; i < PredictorPaletteSize; i++ ) {
        if (pc->PalettePredictorEntryReuseFlags[i]) {
            for (int cIdx = 0; cIdx < numComps; cIdx++) {
                CurrentPaletteEntries[cIdx][NumPredictedPaletteEntries] = slice->ppe.PredictorPaletteEntries[cIdx][i];
                NumPredictedPaletteEntries++;
            }
        }
    }

    for (int cIdx = 0; cIdx < numComps; cIdx++ ) {
        for(int i = 0; i < pc->num_signalled_palette_entries; i++ ) {
            pc->new_palette_entries[cIdx][i] = CABAC(v);
            CurrentPaletteEntries[cIdx][NumPredictedPaletteEntries + i] = pc->new_palette_entries[cIdx][i];
        }
    }

    //see 8-79
    for (int i = 0; i < CurrentPaletteSize; i++) {
        for (int cIdx = 0; cIdx < numComps; cIdx++) {
            newPredictorPaletteEntries[cIdx][i] = CurrentPaletteEntries[cIdx][i];
        }
    }
    int newPredictorPaletteSize = CurrentPaletteSize;


    for (int i = 0; i < PredictorPaletteSize && newPredictorPaletteSize < slice->ppe.PaletteMaxPredictorSize; i++) {
        if (!pc->PalettePredictorEntryReuseFlags[i]) {
            for (int cIdx = 0; cIdx < numComps; cIdx++ ) {
                newPredictorPaletteEntries[ cIdx ][ newPredictorPaletteSize ] =
                    slice->ppe.PredictorPaletteEntries[cIdx][i];
            }
            newPredictorPaletteSize ++;
        }
    }
    for (int cIdx = 0; cIdx < numComps; cIdx++ ) {
        for (int i = 0; i < newPredictorPaletteSize; i++ ) {
            slice->ppe.PredictorPaletteEntries[cIdx][i] = newPredictorPaletteEntries[cIdx][i];
        }
    }
    slice->ppe.PredictorPaletteSize = newPredictorPaletteSize;




    if (CurrentPaletteSize != 0 ) {
        pc->palette_escape_val_present_flag = CABAC(v);
    }
    
    int MaxPaletteIndex = CurrentPaletteSize - 1 + pc->palette_escape_val_present_flag;
    
    if (MaxPaletteIndex > 0) {
        pc->num_palette_indices_minus1 = CABAC(v);
        int adjust = 0;
        for (int i = 0; i <= pc->num_palette_indices_minus1; i++ ) {
            if (MaxPaletteIndex - adjust > 0 ) {
                int palette_idx_idc = CABAC(v);
                pc->PaletteIndexIdc[i] = palette_idx_idc;
            }
            adjust = 1;
        }
        pc->copy_above_indices_for_final_run_flag = CABAC(v);
        pc->palette_transpose_flag = CABAC(v);
    }
    if (pc->palette_escape_val_present_flag) {
        parse_delta_qp(v, slice, pps);
        if (!cu->cu_transquant_bypass_flag) {
            struct chroma_qp_offset *qpoff = malloc(sizeof(*qpoff));
            parse_chroma_qp_offset(v, slice, pps, qpoff);
        }
    }
    int remainingNumIndices = pc->num_palette_indices_minus1 + 1;
    int PaletteScanPos = 0;
    int CurrPaletteIndex;
    int runPos;
    int log2BlockSize = log2floor(nCbS);
    int xC, yC, xR, yR, xcPrev, ycPrev;
    int ** ScanOrder[6][4];


    if (log2BlockSize >= 0 && log2BlockSize < 6) {
        if (log2BlockSize < 4) {
            ScanOrder[log2BlockSize][0] = init_up_right_scan_order(1 << log2BlockSize);
            ScanOrder[log2BlockSize][1] = init_horizontal_scan_order(1 << log2BlockSize);
            ScanOrder[log2BlockSize][2] = init_vertical_scan_order(1 << log2BlockSize);
        }
        if (log2BlockSize > 1) {
            ScanOrder[log2BlockSize][3] = init_traverse_scan_order(1 << log2BlockSize);
        }
    }
    while (PaletteScanPos < nCbS * nCbS ) {
        xC = x0 + ScanOrder[log2BlockSize][3][PaletteScanPos][0];
        yC = y0 + ScanOrder[log2BlockSize][3][PaletteScanPos][1];
        if (PaletteScanPos > 0) {
            xcPrev = x0 + ScanOrder[log2BlockSize][3][PaletteScanPos - 1][0];
            ycPrev = y0 + ScanOrder[log2BlockSize][3][PaletteScanPos - 1][1];
        }
        int PaletteRunMinus1 = nCbS * nCbS - PaletteScanPos - 1;
        int RunToEnd = 1;
        pc->CopyAboveIndicesFlag[xC][yC] = 0;
        if (MaxPaletteIndex > 0) {
            if (PaletteScanPos >= nCbS && pc->CopyAboveIndicesFlag[xcPrev][ycPrev] == 0 ) {
                if (remainingNumIndices > 0 && PaletteScanPos < nCbS * nCbS - 1 ) {
                    uint8_t copy_above_palette_indices_flag = CABAC(v);
                    pc->CopyAboveIndicesFlag[xC][yC] = copy_above_palette_indices_flag;
                } else {
                    if ( PaletteScanPos == nCbS * nCbS - 1 && remainingNumIndices > 0 ) {
                        pc->CopyAboveIndicesFlag[xC][yC] = 0;
                    } else {
                        pc->CopyAboveIndicesFlag[xC][yC] = 1;
                    }
                }
            }
        }
        if (pc->CopyAboveIndicesFlag[xC][yC] == 0) {
            int currNumIndices = pc->num_palette_indices_minus1 + 1 - remainingNumIndices;
            CurrPaletteIndex = pc->PaletteIndexIdc[currNumIndices];
        }
        if (MaxPaletteIndex > 0) {
            if (pc->CopyAboveIndicesFlag[xC][yC] == 0) {
                remainingNumIndices -= 1;
            }
            if (remainingNumIndices > 0 || pc->CopyAboveIndicesFlag[xC][yC] != pc->copy_above_indices_for_final_run_flag) {
                int PaletteMaxRunMinus1 = nCbS * nCbS - PaletteScanPos -1 -remainingNumIndices - pc->copy_above_indices_for_final_run_flag;
                RunToEnd = 0;
                if (PaletteMaxRunMinus1 > 0) {
                    pc->palette_run_prefix = CABAC(v);
                    if ((pc->palette_run_prefix > 1) && (PaletteMaxRunMinus1 !=
                        (1 << (pc->palette_run_prefix - 1)))) {
                        pc->palette_run_suffix = CABAC(v);
                    }
                }
            }
        }
        runPos = 0;
        while (runPos <= PaletteRunMinus1) {
            xR = x0 + ScanOrder[log2BlockSize][3][PaletteScanPos][0];
            yR = y0 + ScanOrder[log2BlockSize][3][PaletteScanPos][1];
            if (pc->CopyAboveIndicesFlag[xC][yC] == 0 ) {
                pc->CopyAboveIndicesFlag[xR][yR] = 0;
                pc->PaletteIndexMap[xR][yR] = CurrPaletteIndex;
            } else {
                pc->CopyAboveIndicesFlag[xR][yR] = 1;
                pc->PaletteIndexMap[xR][yR] = pc->PaletteIndexMap[xR][yR - 1];
            }
            runPos ++;
            PaletteScanPos ++;
        }
    }
    if (pc->palette_escape_val_present_flag) {
        for (int cIdx = 0; cIdx < numComps; cIdx++) {
            for (int sPos = 0; sPos < nCbS * nCbS; sPos++) {
                xC = x0 + ScanOrder[log2BlockSize][3][sPos][0];
                yC = y0 + ScanOrder[log2BlockSize][3][sPos][1];
                if (pc->PaletteIndexMap[xC][yC] == MaxPaletteIndex ) {
                    if (cIdx == 0 || (xC % 2 == 0 && yC % 2 == 0 &&
                        slice->ChromaArrayType == 1 ) || (xC % 2 == 0 &&
                        !pc->palette_transpose_flag && slice->ChromaArrayType == 2 ) ||
                        (yC % 2 == 0 && pc->palette_transpose_flag &&
                        slice->ChromaArrayType == 2 ) || slice->ChromaArrayType == 3 ) {
                        int palette_escape_val= CABAC(v);
                        pc->PaletteEscapeVal[cIdx][xC][yC] = palette_escape_val;
                    }
                }
            }
        }
    }
}


//most important
static void
//parse_residual_coding(struct bits_vec *v, struct cu *cu, struct pps *pps, struct sps *sps,
parse_residual_coding(struct bits_vec *v, int x0, int y0, int log2TrafoSize, int cIdx)
{}
#if 0
{
    if (pps->transform_skip_enabled_flag && !cu->cu_transquant_bypass_flag &&
         (log2TrafoSize <= Log2MaxTransformSkipSize)) {
        transform_skip_flag[x0][y0][cIdx] = CABAC(v);
    }
    if (cu->CuPredMode[x0][y0] == MODE_INTER && sps->sps_range_ext->explicit_rdpcm_enabled_flag &&
        (transform_skip_flag[x0][y0][cIdx] || cu->cu_transquant_bypass_flag)) {
        explicit_rdpcm_flag[x0][y0][cIdx] = CABAC(v);
        if (explicit_rdpcm_flag[x0][y0][cIdx]) {
            explicit_rdpcm_dir_flag[x0][y0][cIdx] = CABAC(v);
        }
    }
    last_sig_coeff_x_prefix = CABAC(v);
    last_sig_coeff_y_prefix = CABAC(v);
    if (last_sig_coeff_x_prefix > 3) {
        last_sig_coeff_x_suffix = CABAC(v);
    }
    if (last_sig_coeff_y_prefix > 3) {
        last_sig_coeff_y_suffix = CABAC(v);
    }
    int lastScanPos = 16;
    int lastSubBlock = (1 << (log2TrafoSize - 2)) * (1 << (log2TrafoSize - 2)) - 1;
    int xS, xC, yS, yC;
    do {
        if (lastScanPos == 0) {
            lastScanPos = 16;
            lastSubBlock --;
        }
        lastScanPos--;
        xS = ScanOrder[log2TrafoSize - 2][scanIdx][lastSubBlock][0];
        yS = ScanOrder[log2TrafoSize - 2][scanIdx][lastSubBlock][1];
        xC = ( xS << 2 ) + ScanOrder[2][scanIdx][lastScanPos][0] ;
        yC = ( yS << 2 ) + ScanOrder[2][scanIdx][lastScanPos][1];
    } while((xC != LastSignificantCoeffX ) || (yC != LastSignificantCoeffY));

    for (int i = lastSubBlock; i >= 0; i--) {
        xS = ScanOrder[log2TrafoSize - 2][scanIdx][i][0];
        yS = ScanOrder[log2TrafoSize - 2][scanIdx][i][1];
        escapeDataPresent = 0;
        int inferSbDcSigCoeffFlag = 0;
        if ((i < lastSubBlock) && (i > 0)) {
            coded_sub_block_flag[xS][yS] = CABAC(v);
            inferSbDcSigCoeffFlag = 1;
        }
        for (int n = ( i == lastSubBlock ) ? lastScanPos - 1 : 15; n >= 0; n-- ) {
            xC = ( xS << 2 ) + ScanOrder[2][scanIdx][n][0] ;
            yC = ( yS << 2 ) + ScanOrder[2][scanIdx][n][1];
            if( coded_sub_block_flag[xS][yS] && (n > 0 || !inferSbDcSigCoeffFlag ) ) {
                sig_coeff_flag[xC][yC] = CABAC(v);
                if (sig_coeff_flag[xC][yC]) {
                    inferSbDcSigCoeffFlag = 0;
                }
            }
        }
        firstSigScanPos = 16;
        lastSigScanPos = -1;
        numGreater1Flag = 0;
        lastGreater1ScanPos = -1;
        for( n = 15; n >= 0; n-- ) {
            xC = ( xS << 2 ) + ScanOrder[ 2 ][ scanIdx ][ n ][ 0 ] ;
            yC = ( yS << 2 ) + ScanOrder[ 2 ][ scanIdx ][ n ][ 1 ];
            if( sig_coeff_flag[ xC ][ yC ] ) {
                if( numGreater1Flag < 8 ) {
                    coeff_abs_level_greater1_flag[n] = CABAC(v);
                    numGreater1Flag ++;
                    if( coeff_abs_level_greater1_flag[ n ] && lastGreater1ScanPos == -1 ) {
                        lastGreater1ScanPos = n;
                    } else if( coeff_abs_level_greater1_flag[ n ] ) {
                        escapeDataPresent = 1;
                    }
                } else {
                    escapeDataPresent = 1;
                }
                if( lastSigScanPos == -1 ) {
                    lastSigScanPos = n;
                }
                firstSigScanPos = n;
            }
        }
        if (cu->cu_transquant_bypass_flag ||
        (cu->CuPredMode[x0][y0] == MODE_INTRA &&
        sps->sps_range_ext->implicit_rdpcm_enabled_flag && transform_skip_flag[x0][y0][cIdx] &&
        ( predModeIntra == 10 || predModeIntra == 26 )) ||
        explicit_rdpcm_flag[x0][y0][cIdx]) {
            signHidden = 0;
        } else {
            signHidden = lastSigScanPos - firstSigScanPos > 3;
        }
        if( lastGreater1ScanPos != -1 ) {
            coeff_abs_level_greater2_flag[lastGreater1ScanPos] = CABAC(v);
            if (coeff_abs_level_greater2_flag[lastGreater1ScanPos]) {
                escapeDataPresent = 1;
            }
        }
        for (int n = 15; n >= 0; n--) {
            xC = (xS << 2) + ScanOrder[2][scanIdx][n][0];
            yC = (yS << 2) + ScanOrder[2][scanIdx][n][1];
            if (sig_coeff_flag[xC][yC] && (!sign_data_hiding_enabled_flag || !signHidden || (n != firstSigScanPos))) {
                coeff_sign_flag[n] = CABAC(v);
            }
        }
        numSigCoeff = 0;
        sumAbsLevel = 0;
        for ( n = 15; n >= 0; n-- ) {
            xC = (xS << 2) + ScanOrder[2][scanIdx][n][0];
            yC = (yS << 2) + ScanOrder[2][scanIdx][n][1];
            if( sig_coeff_flag[xC][yC]) {
                baseLevel = 1 + coeff_abs_level_greater1_flag[n] + coeff_abs_level_greater2_flag[n];
                if( baseLevel == (( numSigCoeff < 8 ) ? ( (n == lastGreater1ScanPos) ? 3 : 2 ) : 1 ) ) {
                    coeff_abs_level_remaining[n] = CABAC(v);
                }
                TransCoeffLevel[x0][y0][cIdx][xC][yC] = (coeff_abs_level_remaining[n] + baseLevel) * (1 - 2*coeff_sign_flag[n]);
                if (sign_data_hiding_enabled_flag && signHidden) {
                    sumAbsLevel += (coeff_abs_level_remaining[n] + baseLevel);
                    if ((n == firstSigScanPos) && (( sumAbsLevel % 2 ) == 1)) {
                        TransCoeffLevel[x0][y0][cIdx][xC][yC] = -TransCoeffLevel[x0][y0][cIdx][xC][yC];
                    }
                }
                numSigCoeff++;
            }
        }
    }
}
#endif

/* see 7.3.8.10 */
static void
parse_transform_unit(struct bits_vec *v, struct cu *cu, struct slice_segment_header *slice, 
    struct vps *vps, struct pps *pps,
    int x0, int y0, int xBase, int yBase, int log2TrafoSize, int trafoDepth, int blkIdx)
{
    struct trans_tree *tt = cu->tt;
    struct trans_unit *tu = cu->tu;
    uint8_t ChromaArrayType = slice->ChromaArrayType;
    int log2TrafoSizeC = MAX(2, log2TrafoSize - (ChromaArrayType == 3 ? 0 : 1));
    int cbfDepthC = trafoDepth - ((ChromaArrayType != 3 && log2TrafoSize == 2) ? 1 : 0);
    int xC = (ChromaArrayType != 3 && log2TrafoSize == 2) ? xBase : x0;
    int yC = (ChromaArrayType != 3 && log2TrafoSize == 2) ? yBase : y0;
    int cbfLuma = tt->cbf_luma[x0][y0][trafoDepth];
    int cbfChroma = tt->cbf_cb[xC][yC][cbfDepthC] ||
        tt->cbf_cr[xC][yC][cbfDepthC] || (ChromaArrayType == 2 &&
        (tt->cbf_cb[xC][yC + ( 1 << log2TrafoSizeC)][cbfDepthC] ||
            tt->cbf_cr[xC][yC + ( 1 << log2TrafoSizeC)][cbfDepthC]));
    if( cbfLuma || cbfChroma ) {
        int xP = ( x0 >> slice->MinCbLog2SizeY ) << slice->MinCbLog2SizeY;
        int yP = ( y0 >> slice->MinCbLog2SizeY ) << slice->MinCbLog2SizeY;
        int nCbS = 1 << slice->MinCbLog2SizeY;
        if (pps->pps_scc_ext->residual_adaptive_colour_transform_enabled_flag &&
            (cu->CuPredMode[ x0 ][ y0 ] == MODE_INTER ||
            (cu->PartMode == PART_2Nx2N &&
            cu->intra_chroma_pred_mode[x0][y0] == 4 ) ||
            (cu->intra_chroma_pred_mode[xP][yP] == 4 &&
            cu->intra_chroma_pred_mode[xP + nCbS/2][yP] == 4 &&
            cu->intra_chroma_pred_mode[xP][yP + nCbS/2] == 4 &&
            cu->intra_chroma_pred_mode[xP + nCbS/2][yP + nCbS/2] == 4))) {
            tu->tu_residual_act_flag[x0][y0] = CABAC(v);
        }
        parse_delta_qp(v, slice, pps);
        if (cbfChroma && !cu->cu_transquant_bypass_flag) {
            struct chroma_qp_offset *qpoff = malloc(sizeof(*qpoff));
            parse_chroma_qp_offset(v, slice, pps, qpoff);
        }
        if (cbfLuma) {
            parse_residual_coding(v, x0, y0, log2TrafoSize, 0);
        }
        if (log2TrafoSize > 2 || ChromaArrayType == 3) {
            if (pps->pps_range_ext->cross_component_prediction_enabled_flag && cbfLuma &&
                (cu->CuPredMode[x0][y0] == MODE_INTER ||
                cu->intra_chroma_pred_mode[ x0][ y0 ] == 4)) {
                parse_cross_comp_pred(v, &tu->ccp, x0, y0, 0);
            }
            for (int tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++) {
                if (tt->cbf_cb[x0][y0 + (tIdx << log2TrafoSizeC)][trafoDepth]) {
                    parse_residual_coding(v, x0, y0 + (tIdx << log2TrafoSizeC), log2TrafoSizeC, 1);
                }
            }
            if (pps->pps_range_ext->cross_component_prediction_enabled_flag &&
                cbfLuma && (cu->CuPredMode[x0][y0] == MODE_INTER ||
                cu->intra_chroma_pred_mode[x0][y0] == 4)) {
                parse_cross_comp_pred(v, &tu->ccp, x0, y0, 1);
            }
            for (int tIdx = 0; tIdx < ( ChromaArrayType == 2 ? 2 : 1 ); tIdx++ ) {
                if (tt->cbf_cr[x0][y0 + (tIdx << log2TrafoSizeC)][trafoDepth]) {
                    parse_residual_coding(v, x0, y0 + (tIdx << log2TrafoSizeC), log2TrafoSizeC, 2);
                }
            }
        } else if (blkIdx == 3) {
            for (int tIdx = 0; tIdx < ( ChromaArrayType == 2 ? 2 : 1 ); tIdx++ ) {
                if (tt->cbf_cb[xBase][yBase + (tIdx << log2TrafoSizeC)][trafoDepth - 1]) {
                    parse_residual_coding(v, xBase, yBase + (tIdx << log2TrafoSizeC), log2TrafoSize, 1);
                }
            }
            for (int tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++) {
                if (tt-> cbf_cr[xBase][yBase + (tIdx << log2TrafoSizeC)][trafoDepth - 1]) {
                    parse_residual_coding(v, xBase, yBase + (tIdx << log2TrafoSizeC), log2TrafoSize, 2);
                }
            }
        }
    }
}

/* see table 9-22 */
static void
init_value_for_cbf(int* cbf_cb)
{
    int initv_cb[15] = {
        94, 138, 182, 154, 149, 107, 167, 154,
        149, 92, 167, 154, 154, 154, 154};
    for (int i = 0; i < 15; i ++) {
        cbf_cb[i] = initv_cb[i];
    }
}

/* see 7.3.8.8 */
static void
transform_tree(struct bits_vec *v, struct cu *cu, struct slice_segment_header *slice, 
    struct vps *vps, struct pps *pps, struct sps *sps,
    int x0, int y0, int xBase, int yBase, int log2TrafoSize, int trafoDepth, int blkIdx)
{
    struct trans_tree *tt = cu->tt;
    if (log2TrafoSize <= slice->MaxTbLog2SizeY &&
        log2TrafoSize > slice->MinTbLog2SizeY &&
        trafoDepth < cu->MaxTrafoDepth && !(cu->IntraSplitFlag && (trafoDepth == 0))) {
        tt->split_transform_flag[x0][y0][trafoDepth] = CABAC(v);
    }
    if ((log2TrafoSize > 2 && slice->ChromaArrayType != 0) || slice->ChromaArrayType == 3) {
        if (trafoDepth == 0 || tt->cbf_cb[xBase][yBase][trafoDepth - 1]) {
            tt->cbf_cb[x0][y0][trafoDepth] = CABAC(v);
            if(slice->ChromaArrayType == 2 && (!tt->split_transform_flag[x0][y0][trafoDepth] || log2TrafoSize == 3)) {
                tt->cbf_cb[x0][y0 + ( 1 << (log2TrafoSize - 1))][trafoDepth] = CABAC(v);
            }
        }
        if (trafoDepth == 0 || tt->cbf_cr[xBase][yBase][trafoDepth - 1]) {
            tt->cbf_cr[x0][y0][trafoDepth] = CABAC(v);
            if (slice->ChromaArrayType == 2 && (!tt->split_transform_flag[x0][y0][trafoDepth] || log2TrafoSize == 3)) {
                tt->cbf_cr[x0][y0 + (1 << (log2TrafoSize - 1))][trafoDepth] = CABAC(v);
            }
        }
    }
    if (tt->split_transform_flag[x0][y0][trafoDepth] ) {
        int x1 = x0 + ( 1 << ( log2TrafoSize - 1));
        int y1 = y0 + ( 1 << ( log2TrafoSize - 1));
        transform_tree(v, cu, slice, vps, pps, sps, x0, y0, x0, y0, log2TrafoSize - 1, trafoDepth + 1, 0);
        transform_tree(v, cu, slice, vps, pps, sps, x1, y0, x0, y0, log2TrafoSize - 1, trafoDepth + 1, 1);
        transform_tree(v, cu, slice, vps, pps, sps, x0, y1, x0, y0, log2TrafoSize - 1, trafoDepth + 1, 2);
        transform_tree(v, cu, slice, vps, pps, sps, x1, y1, x0, y0, log2TrafoSize - 1, trafoDepth + 1, 3);
    } else {
        if (cu->CuPredMode[x0][y0] == MODE_INTRA || trafoDepth != 0 ||
            tt->cbf_cb[x0][y0][trafoDepth] || tt->cbf_cr[x0][y0][trafoDepth] ||
            (slice->ChromaArrayType == 2 &&
            (tt->cbf_cb[x0][y0 + (1 << (log2TrafoSize - 1))][trafoDepth] ||
            tt->cbf_cr[x0][y0 + (1 << (log2TrafoSize - 1))][trafoDepth]))) {
            tt->cbf_luma[x0][y0][trafoDepth] = CABAC(v);
        }
        parse_transform_unit(v, cu, slice, vps, pps, x0, y0, xBase, yBase, log2TrafoSize, trafoDepth, blkIdx );
    }
}



static void
parse_mvd_coding(struct bits_vec *v, struct mvd_coding *mvd, int x0, int y0, int refList)
{
    mvd->abs_mvd_greater0_flag[0] = CABAC(v);
    mvd->abs_mvd_greater0_flag[1] = CABAC(v);
    if (mvd->abs_mvd_greater0_flag[0]) {
        mvd->abs_mvd_greater1_flag[0] = CABAC(v);
    }
    if (mvd->abs_mvd_greater0_flag[1]) {
        mvd->abs_mvd_greater1_flag[1] = CABAC(v);
    }
    if (mvd->abs_mvd_greater0_flag[0]) {
        if (mvd->abs_mvd_greater1_flag[0]) {
            mvd->abs_mvd_minus2[0] = CABAC(v);
        }
        mvd->mvd_sign_flag[0] = CABAC(v);
    }
    if (mvd->abs_mvd_greater0_flag[1]) {
        if (mvd->abs_mvd_greater1_flag[1]) {
            mvd->abs_mvd_minus2[1] = CABAC(v);
        }
        mvd->mvd_sign_flag[1] = CABAC(v);
    }
}


static void
parse_prediction_unit(struct bits_vec *v, struct slice_segment_header *slice, struct cu *cu,
                    struct hevc_nalu_header *h, struct hevc_param_set *hps, int x0, int y0, int nxCbS, int nyCbS)
{
    struct vps *vps = hps->vps;
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    struct predication_unit *pu = &cu->pu[x0][y0];

    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[h->nuh_layer_id];
    int ViewIdx = vps->ViewOrderIdx[h->nuh_layer_id];

    uint8_t cpAvailableFlag = 1;
    /* see (I-34) */
    /* changed IdRefListLayer to IdDirectRefLayer in F-54 */
    for (int i = 0; i < slice->NumActiveRefLayerPics; i++) {
        slice->RefPicLayerId[i] = vps->IdRefListLayer[vps->vps_ext->layer_id_in_nuh[i]][slice->inter_layer_pred_layer_idc[i]];
    }

    int numCurCmpLIds = DepthFlag ? 1 : slice->NumActiveRefLayerPics;



    for (int i =0; i < numCurCmpLIds; i ++) {
        int curCmpLIds = DepthFlag ? h->nuh_layer_id : slice->RefPicLayerId[i];
        int inCmpRefViewIdcs = vps->ViewOrderIdx[curCmpLIds];
        if (vps->CpPresentFlag[ViewIdx][inCmpRefViewIdcs] == 0)
            cpAvailableFlag = 0;
    }
    //see I-19
    uint8_t IvDiMcEnabledFlag = vps->NumRefListLayers[h->nuh_layer_id] > 0 && sps->sps_3d_ext[DepthFlag].iv_di_mc_enabled_flag;
    //see I-25
    uint8_t TexMcEnabledFlag = sps->sps_3d_ext[DepthFlag].tex_mc_enabled_flag && slice->in_comp_pred_flag;
    //see I-22
    uint8_t VspMcEnabledFlag = vps->NumRefListLayers[h->nuh_layer_id] > 0 &&
        sps->sps_3d_ext[DepthFlag].vsp_mc_enabled_flag && slice->in_comp_pred_flag && cpAvailableFlag;
    //see I-35
    int NumExtraMergeCand = IvDiMcEnabledFlag || TexMcEnabledFlag || VspMcEnabledFlag;
    /*MaxNumMergeCand should be range of 1 to 5 + NumExtraMergeCand */
    //see I-36
    int MaxNumMergeCand = 5 + NumExtraMergeCand - slice->five_minus_max_num_merge_cand;
    pu->mvd = malloc(sizeof(struct mvd_coding));
    if (cu->cu_skip_flag[x0][y0]) {
        if (MaxNumMergeCand > 1) {
            pu->merge_idx = CABAC(v);
        } else {
            //when merge_idx is not present, it is inferred to be 0
            pu->merge_idx = 0;
        }
    } else {/*MODE_INTER*/
        pu->merge_flag = CABAC(v);
        if (pu->merge_flag) {
            if (MaxNumMergeCand > 1) {
                pu->merge_idx = CABAC(v);
            }
        } else {
            if (slice->slice_type == SLICE_TYPE_B) {
                pu->inter_pred_idc = CABAC(v);
            }
            if (pu->inter_pred_idc != PRED_L1) {
                if (slice->num_ref_idx_l0_active_minus1 > 0) {
                    pu->ref_idx_l0 = CABAC(v);
                }
                if (pu->mvd == NULL) {
                    pu->mvd = malloc(sizeof(struct mvd_coding));
                }
                parse_mvd_coding(v, pu->mvd, x0, y0, 0);
                pu->mvp_l0_flag = CABAC(v);
            }
            if (pu->inter_pred_idc != PRED_L0) {
                if (slice->num_ref_idx_l1_active_minus1 > 0) {
                    pu->ref_idx_l1 = CABAC(v);
                }
                if (slice->mvd_l1_zero_flag && pu->inter_pred_idc == PRED_BI) {
                    pu->MvdL1[x0][y0][0] = 0;
                    pu->MvdL1[x0][y0][1] = 0;
                } else {
                    if (pu->mvd == NULL) {
                        pu->mvd = malloc(sizeof(struct mvd_coding));
                    }
                    parse_mvd_coding(v, pu->mvd, x0, y0, 1);
                }
                pu->mvp_l1_flag = CABAC(v);
            }
        }
    }
}


static void
parse_pcm_sample(struct bits_vec *v, struct pcm_sample *pcm, struct slice_segment_header * slice,
                 struct sps *sps, int x0, int y0, int log2CbSize)
{
    int PcmBitDepthY = sps->pcm->pcm_sample_bit_depth_luma_minus1 + 1;
    int PcmBitDepthC = sps->pcm->pcm_sample_bit_depth_chroma_minus1 + 1;


    pcm->pcm_sample_luma = malloc(4 * (1<<(log2CbSize << 1)));
    for (int i = 0; i < 1<<(log2CbSize << 1); i ++) {
        pcm->pcm_sample_luma[i] = READ_BITS(v, PcmBitDepthY);
    }
    if (slice->ChromaArrayType != 0) {
        pcm->pcm_sample_chroma = malloc(4 *((2 << (log2CbSize << 1)) / (slice->SubWidthC * slice->SubHeightC)));
        for (int i = 0; i < ((2 << (log2CbSize << 1)) / (slice->SubWidthC * slice->SubHeightC)); i++) {
            pcm->pcm_sample_chroma[i] = READ_BITS(v, PcmBitDepthC);
        }
    }
}

static void
parse_coding_unit(struct bits_vec *v, struct cu *cu, struct hevc_slice *hslice,
        struct hevc_param_set *hps, int x0, int y0, int log2CbSize)
{
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;

    int MaxTbLog2SizeY = sps->log2_min_luma_transform_block_size_minus2 + 2 + sps->log2_diff_max_min_luma_transform_block_size;
    int Log2MinIpcmCbSizeY = sps->pcm->log2_min_pcm_luma_coding_block_size_minus3 + 3;
    int Log2MaxIpcmCbSizeY = sps->pcm->log2_diff_max_min_pcm_luma_coding_block_size + Log2MinIpcmCbSizeY;
    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[headr->nuh_layer_id];
    //see (I-30)
    int SkipIntraEnabledFlag =  sps->sps_3d_ext[DepthFlag].skip_intra_enabled_flag;
    uint8_t ChromaArrayType = (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc);
    //see 7-10
    uint32_t MinCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus3 + 3;
    if (pps->transquant_bypass_enabled_flag) {
        cu->cu_transquant_bypass_flag = CABAC(v);
    }
    if (slice->slice_type != SLICE_TYPE_I) {
        cu->cu_skip_flag[x0][y0] = CABAC(v);
    }
    
    //nCbS specify the size of current coding block
    int nCbS = (1 << log2CbSize);
    if (cu->cu_skip_flag[x0][y0]) {
        parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS, nCbS);
    } else if (SkipIntraEnabledFlag) {
        cu->skip_intra_flag[x0][y0] = CABAC(v);
    }

    if (!cu->cu_skip_flag[x0][y0] && !cu->skip_intra_flag[x0][y0]) {
        if (slice->slice_type != SLICE_TYPE_I) {
            uint8_t pred_mode_flag = CABAC(v);
            if (pred_mode_flag == 0) {
                cu->CuPredMode[x0][y0] = MODE_INTER;
            } else {
                cu->CuPredMode[x0][y0] = MODE_INTRA;
            }
        }
        //if pred_mode_flag not present
        if (slice->slice_type == SLICE_TYPE_I) {
            cu->CuPredMode[x0][y0] = MODE_INTRA;
        } else {
            if (cu->cu_skip_flag[x0][y0] == 1) {
                cu->CuPredMode[x0][y0] = MODE_SKIP;
            }
        }

        if (sps->sps_scc_ext->palette_mode_enabled_flag &&
            cu->CuPredMode[x0][y0] == MODE_INTRA &&
            log2CbSize <= MaxTbLog2SizeY ) {
            cu->palette_mode_flag[x0][y0] = CABAC(v);
        } else {
            cu->palette_mode_flag[x0][y0] = 0;
        }
        if (cu->palette_mode_flag[x0][y0]) {
            struct palette_coding* pc = malloc(sizeof(*pc));
            parse_palette_coding(v, pc, cu, slice, pps, sps, x0, y0, nCbS);
        } else {
            if (cu->CuPredMode[x0][y0] != MODE_INTRA ||
                log2CbSize == MinCbLog2SizeY ) {
                int part_mode = CABAC(v);
                if (cu->CuPredMode[x0][y0] == MODE_INTRA) {
                    assert(part_mode < 2);
                } else {
                    if (log2CbSize > MinCbLog2SizeY && sps->amp_enabled_flag == 1) {
                        assert((part_mode >= 0 && part_mode <=2) || (part_mode >= 4 && part_mode<= 7));
                    } else if ((log2CbSize > MinCbLog2SizeY && sps->amp_enabled_flag == 0) || log2CbSize == 3) {
                        assert(part_mode >= 0 && part_mode <=2);
                    } else if (log2CbSize > 3 && log2CbSize == MinCbLog2SizeY) {
                        assert(part_mode >= 0 && part_mode <=3);
                    }
                }
                //see table7-10
                if (cu->CuPredMode[x0][y0] == MODE_INTRA) {
                    cu->PartMode = (part_mode == 0) ? PART_2Nx2N : PART_NxN;
                    cu->IntraSplitFlag = (part_mode == 0) ? 0 : 1;
                } else if (cu->CuPredMode[x0][y0] == MODE_INTER) {
                    cu->PartMode = part_mode;// already aligned the value be define
                    cu->IntraSplitFlag = 0;
                }
            } else {
                // if part_mode is not preset
                cu->PartMode = PART_2Nx2N;
                cu->IntraSplitFlag = 0;
            }
            

            if (cu->CuPredMode[x0][y0] == MODE_INTRA) {
                if( cu->PartMode == PART_2Nx2N && sps->pcm_enabled_flag &&
                    log2CbSize >= Log2MinIpcmCbSizeY &&
                    log2CbSize <= Log2MaxIpcmCbSizeY) {
                    cu->pcm_flag[x0][y0] = CABAC(v);
                }
                if (cu->pcm_flag[x0][y0]) {
                    cu->pcm = malloc(sizeof(struct pcm_sample));
                    while (!BYTE_ALIGNED(v)) {
                        SKIP_BITS(v, 1);
                    }
                    parse_pcm_sample(v, cu->pcm, slice, sps, x0, y0, log2CbSize);
                } else {
                    int pbOffset = (cu->PartMode == PART_NxN) ? (nCbS / 2) : nCbS;
                    for (int j = 0; j < nCbS; j = j + pbOffset) {
                        for (int i = 0; i < nCbS; i = i + pbOffset) {
                            cu->prev_intra_luma_pred_flag[x0 + i][y0 + j] = CABAC(v);
                        }
                    }
                    for (int j = 0; j < nCbS; j = j + pbOffset) {
                        for (int i = 0; i < nCbS; i = i + pbOffset) {
                            if (cu->prev_intra_luma_pred_flag[x0 + i][y0 + j]) {
                                cu->mpm_idx[x0 + i][y0 + j] = CABAC(v);
                            } else {
                                cu->rem_intra_luma_pred_mode[x0 + i][y0 + j] = CABAC(v);
                            }
                        }
                    }
                    if( ChromaArrayType == 3 ) {
                        for (int j = 0; j < nCbS; j = j + pbOffset ) {
                            for (int i = 0; i < nCbS; i = i + pbOffset ) {
                                cu->intra_chroma_pred_mode[x0 + i][y0 + j] = CABAC(v);
                            }
                        }
                    } else if(ChromaArrayType != 0) {
                        cu->intra_chroma_pred_mode[x0][y0] = CABAC(v);
                    }
                }
            } else {
                if( cu->PartMode == PART_2Nx2N )
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS, nCbS);
                else if( cu->PartMode == PART_2NxN ) {
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS, nCbS / 2 );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0 + ( nCbS / 2 ), nCbS, nCbS / 2 );
                } else if( cu->PartMode == PART_Nx2N ) {
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS / 2, nCbS );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0 + ( nCbS / 2 ), y0, nCbS / 2, nCbS );
                } else if( cu->PartMode == PART_2NxnU ) {
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS, nCbS / 4 );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0 + ( nCbS / 4 ), nCbS, nCbS * 3 / 4 );
                } else if( cu->PartMode == PART_2NxnD ) {
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS, nCbS * 3 / 4 );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0 + ( nCbS * 3 / 4 ), nCbS, nCbS / 4 );
                } else if( cu->PartMode == PART_nLx2N ) {
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS / 4, nCbS );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0 + ( nCbS / 4 ), y0, nCbS * 3 / 4, nCbS );
                } else if( cu->PartMode == PART_nRx2N ) {
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS * 3 / 4, nCbS );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0 + ( nCbS * 3 / 4 ), y0, nCbS / 4, nCbS );
                } else { /* PART_NxN */
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0, nCbS / 2, nCbS / 2 );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0 + ( nCbS / 2 ), y0, nCbS / 2, nCbS / 2 );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0, y0 + ( nCbS / 2 ), nCbS / 2, nCbS / 2 );
                    parse_prediction_unit(v, slice, cu, headr, hps, x0 + ( nCbS / 2 ), y0 + ( nCbS / 2 ), nCbS / 2, nCbS / 2 );
                }
            }
            if (!cu->pcm_flag[x0][y0]) {
                if (cu->CuPredMode[x0][y0] != MODE_INTRA &&
                    !(cu->PartMode == PART_2Nx2N && cu->pu[x0][y0].merge_flag)) {
                    cu->rqt_root_cbf = CABAC(v);
                }
                if (cu->rqt_root_cbf) {
                    cu->MaxTrafoDepth = ( cu->CuPredMode[x0][y0] == MODE_INTRA ?
                        (sps->max_transform_hierarchy_depth_intra + cu->IntraSplitFlag) :
                        sps->max_transform_hierarchy_depth_inter);
                    transform_tree(v, cu, slice, vps, pps, sps, x0, y0, x0, y0, log2CbSize, 0, 0 );
                }
            }
        }
    }
}

static void
coding_quadtree(struct bits_vec *v, struct hevc_slice *hslice,
                struct hevc_param_set *hps, int x0, int y0, int log2CbSize, int cqtDepth)
{
    struct slice_segment_header *slice = hslice->slice;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;
    //see 7-36
    int Log2MinCuQpDeltaSize = slice->CtbLog2SizeY - pps->diff_cu_qp_delta_depth;
    //see 7-39
    int Log2MinCuChromaQpOffsetSize = slice->CtbLog2SizeY - pps->pps_range_ext->diff_cu_chroma_qp_offset_depth;
    uint32_t split_cu_flag = 0;

    if (x0 + ( 1 << log2CbSize ) <= sps->pic_width_in_luma_samples &&
        y0 + ( 1 << log2CbSize ) <= sps->pic_height_in_luma_samples &&
        log2CbSize > slice->MinCbLog2SizeY) {
        split_cu_flag = CABAC(v);
    } else {
        //see 7.4.9.4
        //if split_cu_flag is not present
        if (log2CbSize > slice->MinCbLog2SizeY)
            split_cu_flag = 1;
    }
    if (pps->cu_qp_delta_enabled_flag && log2CbSize >= Log2MinCuQpDeltaSize ) {
        slice->IsCuQpDeltaCoded = 0;
        slice->CuQpDeltaVal = 0;
    }
    if (slice->cu_chroma_qp_offset_enabled_flag &&
        log2CbSize >= Log2MinCuChromaQpOffsetSize) {
        slice->IsCuChromaQpOffsetCoded = 0;
    }
    if (split_cu_flag) {
        int x1 = x0 + (1 << (log2CbSize - 1 ));
        int y1 = y0 + (1 << (log2CbSize - 1 ));
        coding_quadtree(v, hslice, hps, x0, y0, log2CbSize - 1, cqtDepth + 1);
        if (x1 < sps->pic_width_in_luma_samples)
            coding_quadtree(v, hslice, hps, x1, y0, log2CbSize - 1, cqtDepth + 1);
        if (y1 < sps->pic_height_in_luma_samples )
            coding_quadtree(v, hslice, hps, x0, y1, log2CbSize - 1, cqtDepth + 1);
        if (x1 < sps->pic_width_in_luma_samples && y1 < sps->pic_height_in_luma_samples )
            coding_quadtree(v, hslice, hps, x1, y1, log2CbSize - 1, cqtDepth + 1);
    } else {
        struct cu *cu = malloc(sizeof(struct cu));
        cu->CtDepth[x0][y0] = cqtDepth;
        parse_coding_unit(v, cu, hslice, hps, x0, y0, log2CbSize);
    }
}

static void
coding_tree_unit(struct bits_vec *v, struct hevc_slice *hslice,
    struct hevc_param_set *hps, uint32_t *CtbAddrRsToTs, uint32_t CtbAddrInTs,
    uint32_t CtbAddrInRs, uint32_t SliceAddrRs, uint32_t *TileId)
{
    struct slice_segment_header *slice = hslice->slice;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;
    struct sao sao;
    uint32_t xCtb = (CtbAddrInRs % slice->PicWidthInCtbsY) << slice->CtbLog2SizeY;
    uint32_t yCtb = (CtbAddrInRs / slice->PicWidthInCtbsY) << slice->CtbLog2SizeY;
    uint32_t **SaoTypeIdx[4];
    for (int cIdx = 0; cIdx < (slice->ChromaArrayType != 0 ? 3 : 1 ); cIdx++) {
        SaoTypeIdx[cIdx] = malloc(sizeof(uint32_t *) * (xCtb >> slice->CtbLog2SizeY));
        for (int x = 0; x < (xCtb >> slice->CtbLog2SizeY); x ++) {
            SaoTypeIdx[cIdx][x] = malloc(sizeof(uint32_t) * (yCtb >> slice->CtbLog2SizeY));
        }
    }
    if (slice->slice_sao_luma_flag || slice->slice_sao_chroma_flag)
        parse_sao(v, slice, &sao, SaoTypeIdx, xCtb >> slice->CtbLog2SizeY, yCtb >> slice->CtbLog2SizeY, CtbAddrRsToTs,
         CtbAddrInTs, CtbAddrInRs, SliceAddrRs, TileId);
    coding_quadtree(v, hslice, hps, xCtb, yCtb, slice->CtbLog2SizeY, 0);
}

/*see (6-10) */
static int **
init_zscan_array(struct slice_segment_header *slice, struct sps *sps, uint32_t *CtbAddrRsToTs)
{
    int **MinTbAddrZs;
    MinTbAddrZs = malloc(slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY) * sizeof(int *));
    for (int x = 0; x < slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY); x ++) {
        MinTbAddrZs[x] = malloc(sizeof(int) * slice->PicHeightInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY));
    }
    for (int y = 0; y < (slice->PicHeightInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY)); y++ ) {
        for (int x = 0; x < (slice->PicWidthInCtbsY << (slice->CtbLog2SizeY -slice->MinTbLog2SizeY)); x++) {
            int tbX = (x << slice->MinTbLog2SizeY) >> slice->CtbLog2SizeY;
            int tbY = (y << slice->MinTbLog2SizeY) >> slice->CtbLog2SizeY;
            int ctbAddrRs = slice->PicWidthInCtbsY * tbY + tbX;
            MinTbAddrZs[x][y] = CtbAddrRsToTs[ctbAddrRs] << ((slice->CtbLog2SizeY - slice->MinTbLog2SizeY) * 2);
            int m, p;
            for (int i = 0, p = 0; i < (slice->CtbLog2SizeY - slice->MinTbLog2SizeY); i++) {
                m = 1 << i;
                p += (m & x ? m * m : 0) + (m & y ? 2 * m * m : 0);
            }
            MinTbAddrZs[x][y] += p;
        }
    }
    return MinTbAddrZs;
}


static void
parse_slice_segment_data(struct hevc_slice *hslice, uint8_t *data, uint16_t len,
        struct hevc_param_set *hps, uint32_t SliceAddrRs)
{
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;

    uint8_t end_of_slice_segment_flag = 0;
    //see para 6.5.1: CTB raster and tile scanning conversion process
    //see (6-3)
    uint32_t *colWidth = malloc(4 * (pps->num_tile_columns_minus1 + 1));
    if (pps->uniform_spacing_flag )
        for (int i = 0; i <= pps->num_tile_columns_minus1; i++)
            colWidth[i] = ((i + 1) * slice->PicWidthInCtbsY) / (pps->num_tile_columns_minus1 + 1) -
                        (i * slice->PicWidthInCtbsY) / (pps->num_tile_columns_minus1 + 1);
    else {
        colWidth[pps->num_tile_columns_minus1] = slice->PicWidthInCtbsY;
        for (int i = 0; i < pps->num_tile_columns_minus1; i++) {
            colWidth[i] = pps->column_width_minus1[i] + 1;
            colWidth[pps->num_tile_columns_minus1] -= colWidth[i];
        }
    }
    //see (6-4)
    uint32_t *rowHeight = malloc(4 * (pps->num_tile_rows_minus1 + 1));
    if (pps->uniform_spacing_flag)
        for (int j = 0; j <= pps->num_tile_rows_minus1; j++)
            rowHeight[j] = ((j + 1) * slice->PicHeightInCtbsY) / (pps->num_tile_rows_minus1 + 1) -
                            (j * slice->PicHeightInCtbsY) / (pps->num_tile_rows_minus1 + 1);
    else {
        rowHeight[pps->num_tile_rows_minus1] = slice->PicHeightInCtbsY;
        for (int j = 0; j < pps->num_tile_rows_minus1; j++ ) {
            rowHeight[j] = pps->row_height_minus1[j] + 1;
            rowHeight[pps->num_tile_rows_minus1] -= rowHeight[j];
        }
    }
    uint32_t *colBd = malloc(4 *(pps->num_tile_columns_minus1 + 1));
    //see (6-5)
    colBd[0] = 0;
    for (int i = 0; i <= pps->num_tile_columns_minus1; i++) {
        colBd[i + 1] = colBd[i] + colWidth[i];
    }
    //see (6-6)
    uint32_t *rowBd = malloc(4 * (pps->num_tile_rows_minus1 + 1));
    rowBd[0] = 0;
    for (int j = 0; j <= pps->num_tile_rows_minus1; j++) {
        rowBd[j + 1] = rowBd[j] + rowHeight[j];
    }
    //see (6-7)
    uint32_t *CtbAddrRsToTs = malloc(4 * slice->PicSizeInCtbsY);
    for (int ctbAddrRs = 0; ctbAddrRs < slice->PicSizeInCtbsY; ctbAddrRs++ ) {
        int tbX = ctbAddrRs % slice->PicWidthInCtbsY;
        int tbY = ctbAddrRs / slice->PicWidthInCtbsY;
        int tileX, tileY;
        for (int i = 0; i <= pps->num_tile_columns_minus1; i++ ) {
            if (tbX >= colBd[i]) {
                tileX = i;
            }
        }
        for (int j = 0; j <= pps->num_tile_rows_minus1; j++ ) {
            if( tbY >= rowBd[j]) {
                tileY = j;
            }
        }
        CtbAddrRsToTs[ctbAddrRs] = 0;
        for (int i = 0; i < tileX; i++) {
            CtbAddrRsToTs[ctbAddrRs] += rowHeight[tileY] * colWidth[i];
        }
        for (int j = 0; j < tileY; j++) {
            CtbAddrRsToTs[ctbAddrRs] += slice->PicWidthInCtbsY * rowHeight[j];
        }
        CtbAddrRsToTs[ctbAddrRs] += ((tbY - rowBd[tileY]) * colWidth[tileX] + tbX - colBd[tileX]);
    }
    //see (6-8)
    uint32_t *CtbAddrTsToRs = malloc(4 * slice->PicSizeInCtbsY);
    for (int ctbAddrRs = 0; ctbAddrRs < slice->PicSizeInCtbsY; ctbAddrRs++ )
        CtbAddrTsToRs[CtbAddrRsToTs[ctbAddrRs]] = ctbAddrRs;
    //see (6-9)
    uint32_t *TileId = malloc(4 * slice->PicSizeInCtbsY);
    for (int j = 0, tileIdx = 0; j <= pps->num_tile_rows_minus1; j++) {
        for (int i = 0; i <= pps->num_tile_columns_minus1; i++, tileIdx++) {
            for (int y = rowBd[j]; y < rowBd[j + 1]; y++) {
                for(int x = colBd[i]; x < colBd[i + 1]; x++) {
                    TileId[CtbAddrRsToTs[y * slice->PicWidthInCtbsY+ x]] = tileIdx;
                }
            }
        }
    }
    //see 6.5.2 z-scan order array initialization process
    //see (6-10)
    int **MinTbAddrZs = init_zscan_array(slice, sps, CtbAddrRsToTs);



    //see 7.4.7.1 slice_segment_address
    uint32_t CtbAddrInRs = slice->slice_segment_address;
    uint32_t CtbAddrInTs = CtbAddrRsToTs[CtbAddrInRs];

    struct bits_vec *v = bits_vec_alloc(data, len, BITS_MSB);
    do {
        coding_tree_unit(v, hslice, hps, CtbAddrRsToTs, CtbAddrInTs, CtbAddrInRs,
             SliceAddrRs, TileId);
        end_of_slice_segment_flag = CABAC(v);
        CtbAddrInTs ++;
        CtbAddrInRs = CtbAddrTsToRs[CtbAddrInTs];
        if (!end_of_slice_segment_flag && ((pps->tiles_enabled_flag && TileId[CtbAddrInTs] != TileId[CtbAddrInTs - 1]) ||
                (pps->entropy_coding_sync_enabled_flag && (CtbAddrInRs % slice->PicWidthInCtbsY == 0 ||
                TileId[CtbAddrInTs] != TileId[CtbAddrRsToTs[CtbAddrInRs - 1]])))) {
            uint8_t end_of_subset_one_bit = CABAC(v); //should be equal to 1
            byte_alignment(v);
        }
    } while (!end_of_slice_segment_flag);
}


struct vps *first_vps = NULL;
struct sps *first_sps = NULL;
struct pps *first_pps = NULL;

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
    uint32_t SliceAddrRs;
    struct hevc_param_set hps = {
        .vps = first_vps,
        .pps = first_pps,
        .sps = first_sps,
    };
    // printf("rbsp %d: %02x %02x %02x %02x\n", nrbsp, rbsp[0], rbsp[1], rbsp[2],rbsp[3]);
    switch (h.nal_unit_type) {
    case VPS_NUT:
        first_vps = parse_vps(&h, rbsp, nrbsp);
        break;
    case SPS_NUT:
        first_sps = parse_sps(&h, rbsp, nrbsp, first_vps);
        break;
    case PPS_NUT:
        first_pps = parse_pps(&h, rbsp, nrbsp);
        break;
    case IDR_N_LP:
        parse_slice_segment_header(&h, rbsp, nrbsp, &hps, &SliceAddrRs);
        break;
    case PREFIX_SEI_NUT:
    case SUFFIX_SEI_NUT:
        parse_sei(&h, rbsp, nrbsp);
        break;
    default:
        break;
    }
}
