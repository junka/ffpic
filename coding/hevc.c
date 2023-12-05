#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "bitstream.h"
#include "vlog.h"
#include "utils.h"
#include "hevc.h"
#include "cabac.h"
#include "predict.h"
#include "colorspace.h"

VLOG_REGISTER(hevc, DEBUG)

struct picture {
    int16_t *Y;
    int16_t *U;
    int16_t *V;
    int y_stride;
    int uv_stride;

    uint8_t *IntraPredModeY;
    uint8_t *IntraPredModeC;

    uint8_t *CuPredMode;
    uint8_t *pcm_flag;
    uint8_t *qpy;
    uint8_t *split_transform_flag;
    // uint8_t *log2CbSize;

    int ctu_num;
    struct ctu* ctus[1024];
};

static void
rbsp_trailing_bits(struct bits_vec *v)
{
    //should be 1
    // uint8_t rbsp_stop_one_bit = READ_BIT(v);
    assert(READ_BIT(v) == 1);
    while (!BYTE_ALIGNED(v)) {
        SKIP_BITS(v, 1);
    }
}

static void
byte_alignment(struct bits_vec *v)
{
    // uint8_t alignment_bit_equal_to_one = READ_BIT(v);
    assert(READ_BIT(v) == 1);
    while (!BYTE_ALIGNED(v)) {
        SKIP_BITS(v, 1);
        VDBG(hevc, "skip 1 bit");
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
// table 7-5, 7-6, default value
const uint8_t ScalingList[4][6][64] = {
    {{16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
     {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
     {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
     {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
     {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
     {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16}},
    {
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
    },
    {
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
    },
    {
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 16, 17, 16, 17, 18,
         17, 18, 18, 17, 18, 21, 19, 20, 21, 20, 19, 21, 24, 22, 22, 24,
         24, 22, 22, 24, 25, 25, 27, 30, 27, 25, 25, 29, 31, 35, 35, 31,
         29, 36, 41, 44, 41, 36, 47, 54, 54, 47, 65, 70, 65, 88, 88, 115},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
        {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18,
         18, 18, 18, 18, 18, 20, 20, 20, 20, 20, 20, 20, 24, 24, 24, 24,
         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 28, 28, 28, 28, 28,
         28, 33, 33, 33, 33, 33, 41, 41, 41, 41, 54, 54, 54, 71, 71, 91},
    }
};

/* see 7.3.4 */
static void
parse_scaling_list_data(struct bits_vec *v, struct scaling_list_data *sld)
{
    VINFO(hevc, "parse_scaling_list_data");

    int refMatrixId;
    // table 7-3, sizeid 0 means 4X4, 1 means 8X8, 2 means 16X16, 3 means 32X32
    for (int sizeid = 0; sizeid < 4; sizeid ++) {
        for (int mid = 0; mid < 6; mid += (sizeid == 3)?3:1) {
            sld->scaling_list_pred_mode_flag[sizeid][mid] = READ_BIT(v);
            if (!sld->scaling_list_pred_mode_flag[sizeid][mid]) {
                sld->scaling_list_pred_matrix_id_delta[sizeid][mid] = GOL_UE(v);
                if (sizeid <= 2) {
                    assert(sld->scaling_list_pred_matrix_id_delta[sizeid][mid] <= mid);
                } else {
                    assert(sld->scaling_list_pred_matrix_id_delta[sizeid][mid] <= mid/3);
                }
                refMatrixId = mid - sld->scaling_list_pred_matrix_id_delta[sizeid][mid] *
                              (sizeid == 3 ? 3 : 1);
                if (sld->scaling_list_pred_matrix_id_delta[sizeid][mid] == 0) {
                    for (int i = 0; i < MIN(63, (1 << (4 + (sizeid << 1))) - 1); i++) {
                      sld->scalinglist[sizeid][mid][i] = ScalingList[sizeid][mid][i];
                    }
                } else {
                    for (int i = 0; i < MIN(63, (1 << (4 + (sizeid << 1))) - 1); i++) {
                      sld->scalinglist[sizeid][mid][i] =  ScalingList[sizeid][refMatrixId][i];
                    }
                }

                if (sld->scaling_list_pred_matrix_id_delta[sizeid][mid] == 0 &&
                    sizeid > 1) {
                    sld->scaling_list_dc_coef_minus8[sizeid - 2][mid] = 8;
                } else if (sld->scaling_list_pred_matrix_id_delta[sizeid][mid] != 0 &&
                           sizeid > 1) {
                    sld->scaling_list_dc_coef_minus8[sizeid - 2][mid] =
                        sld->scaling_list_dc_coef_minus8[sizeid - 2][refMatrixId];
                }
            } else {
                uint8_t nextcoef = 8;
                sld->coefNum = MIN(64, 1 << (4 + (sizeid<<1)));
                if (sizeid > 1) {
                    sld->scaling_list_dc_coef_minus8[sizeid-2][mid] = GOL_SE(v);

                    // refMatrixId = mid - sld->scaling_list_pred_matrix_id_delta[sizeid][mid] *
                    //           (sizeid == 3 ? 3 : 1);
                    // if (sld->scaling_list_pred_matrix_id_delta[sizeid][mid] != 0) {
                    //     sld->scaling_list_dc_coef_minus8[sizeid - 2][mid] =
                    //         sld->scaling_list_dc_coef_minus8[sizeid - 2][refMatrixId];
                    // }
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

static void 
init_scaling_factor(struct slice_segment_header *slice,struct scaling_list_data *sld)
{
    // if (sizeid == 0) {
        for (int mid = 0; mid < 6; mid++) {
            for (int i = 0; i < 16; i++) {
                int x = slice->ScanOrder[2][0][i].x;
                int y = slice->ScanOrder[2][0][i].y;
                slice->ScalingFactor[0][mid][x][y] = sld->scalinglist[0][mid][i];
            }
        }
    // } else if (sizeid == 1) {
        for (int mid = 0; mid < 6; mid++) {
            for (int i = 0; i < 64; i++) {
                int x = slice->ScanOrder[3][0][i].x;
                int y = slice->ScanOrder[3][0][i].y;
                slice->ScalingFactor[1][mid][x][y] = sld->scalinglist[1][mid][i];
            }
        }
    // } else if (sizeid == 2) {
        for (int mid = 0; mid < 6; mid++) {
            for (int i = 0; i < 64; i++) {
                int x = slice->ScanOrder[3][0][i].x;
                int y = slice->ScanOrder[3][0][i].y;
                for (int j = 0; j < 2; j++) {
                    for (int k = 0; k < 2; k++) {
                      slice->ScalingFactor[2][mid][x * 2 + k][y * 2 + j] = ScalingList[2][mid][i];
                      slice->ScalingFactor[2][mid][0][0] = sld->scaling_list_dc_coef_minus8[0][mid] + 8;
                    }
                }
            }
        }
    // } else if (sizeid == 3) {
        for (int mid = 0; mid < 4; mid += 3) {
            for (int i = 0; i < 64; i++) {
                int x = slice->ScanOrder[3][0][i].x;
                int y = slice->ScanOrder[3][0][i].y;
                for (int j = 0; j < 4; j++) {
                    for (int k = 0; k < 4; k++) {
                      slice->ScalingFactor[3][mid][x * 4 + k][y * 4 + j] = ScalingList[3][mid][i];
                      slice->ScalingFactor[3][mid][0][0] = sld->scaling_list_dc_coef_minus8[1][mid] + 8;
                    }
                }
            }
        }
    // }
    if (slice->ChromaArrayType == 3) {
        for (int mid = 1; mid == 1 || mid == 2 || mid == 4 || mid == 5;
             mid += 1) {
            for (int i = 0; i < 64; i++) {
                int x = slice->ScanOrder[3][0][i].x;
                int y = slice->ScanOrder[3][0][i].y;
                for (int j = 0; j < 4; j++) {
                    for (int k = 0; k < 4; k++) {
                      slice->ScalingFactor[3][mid][x * 4 + k][y * 4 + j] =
                          ScalingList[2][mid][i];
                    }
                }
            }
            slice->ScalingFactor[3][mid][0][0] =
                sld->scaling_list_dc_coef_minus8[0][mid] + 8;
        }
    }
}

/* see (I-10)*/
static int
ViewIdxfromvps(int picX)
{
    return picX;
}
/* see (I-11)*/
static int
ViewIdVal(struct vps* vps, int picX)
{
    return vps->vps_ext->view_id_val[ViewIdxfromvps(picX)];
}


/* See Annex A: Profiles, tiers and levels*/
#define JUDGE_PROFILE(name, value) ((name##_idc == value) || (name##_compatibility_flag & (1 << value)))

// see 7.3.3 
static void
parse_profile_tier_level(struct bits_vec *v, struct profile_tier_level *ptl,
        uint8_t profilepresentFlag, uint8_t maxNumSubLayersMinus1)
{
    VDBG(hevc, "profile flag %d, num %d", profilepresentFlag, maxNumSubLayersMinus1);
    if (profilepresentFlag) {
        ptl->general_profile_space = READ_BITS(v, 2);
        ptl->general_tier_flag = READ_BIT(v);
        ptl->general_profile_idc = READ_BITS(v, 5);
        for (int i = 0; i < 32; i ++) {
            ptl->general_profile_compatibility_flag |= (READ_BIT(v) << i);
        }
        VDBG(hevc, "general_profile %d idc %d, flag 0x%x", ptl->general_profile_space, ptl->general_profile_idc, ptl->general_profile_compatibility_flag);
        ptl->general_progressive_source_flag = READ_BIT(v);
        ptl->general_interlaced_source_flag = READ_BIT(v);
        ptl->general_non_packed_constraint_flag = READ_BIT(v);
        ptl->general_frame_only_constraint_flag = READ_BIT(v);
#if 0
        if (JUDGE_PROFILE(ptl->general_profile, 4) || JUDGE_PROFILE(ptl->general_profile, 5) ||
            JUDGE_PROFILE(ptl->general_profile, 6) || JUDGE_PROFILE(ptl->general_profile, 7) ||
            JUDGE_PROFILE(ptl->general_profile, 8) || JUDGE_PROFILE(ptl->general_profile, 9) ||
            JUDGE_PROFILE(ptl->general_profile, 10) || JUDGE_PROFILE(ptl->general_profile, 11))
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
            if (JUDGE_PROFILE(ptl->general_profile, 5) || JUDGE_PROFILE(ptl->general_profile, 9) ||
                JUDGE_PROFILE(ptl->general_profile, 10) || JUDGE_PROFILE(ptl->general_profile, 11)) {
                ptl->general_max_14bit_constraint_flag = READ_BIT(v);
                SKIP_BITS(v, 33);
            } else {
                SKIP_BITS(v, 34);
            }
        } else if (JUDGE_PROFILE(ptl->general_profile, 2)) {
            SKIP_BITS(v, 7);
            ptl->general_one_picture_only_constraint_flag = READ_BIT(v);
            VDBG(hevc, "one_picture_only_constraint %d\n", ptl->general_one_picture_only_constraint_flag);
            SKIP_BITS(v, 35);
        } else {
            SKIP_BITS(v, 43);
        }
        if (JUDGE_PROFILE(ptl->general_profile, 1) || JUDGE_PROFILE(ptl->general_profile, 2) ||
            JUDGE_PROFILE(ptl->general_profile, 3) || JUDGE_PROFILE(ptl->general_profile, 4) ||
            JUDGE_PROFILE(ptl->general_profile, 5) || JUDGE_PROFILE(ptl->general_profile, 9) ||
            JUDGE_PROFILE(ptl->general_profile, 11))
        {
            ptl->general_inbld_flag = READ_BIT(v);
            VDBG(hevc, "general_inbld_flag %d", ptl->general_inbld_flag);
        } else {
            SKIP_BITS(v, 1);
        }
#else
        SKIP_BITS(v, 44);
#endif
    }
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
    ptl->sublayers = calloc(maxNumSubLayersMinus1 + 1, sizeof(struct sub_layer));
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
#if 0
            if (JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 4) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 5) ||
                JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 6) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 7) ||
                JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 8) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 9) ||
                JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 10) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 11))
            {
                ptl->sublayers[i].sub_layer_max_12bit_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_max_10bit_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_max_8bit_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_max_422chroma_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_max_420chroma_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_max_monochrome_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_intra_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_one_picture_only_constraint_flag = READ_BIT(v);
                ptl->sublayers[i].sub_layer_lower_bit_rate_constraint_flag = READ_BIT(v);
                if (JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 5) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 9) ||
                    JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 10) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 11))
                {
                    ptl->sublayers[i].sub_layer_max_14bit_constraint_flag = READ_BIT(v);
                    SKIP_BITS(v, 33);
                } else {
                    SKIP_BITS(v, 34);
                }
            } else if (JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 2)) {
                SKIP_BITS(v, 7);
                ptl->sublayers[i].sub_layer_one_picture_only_constraint_flag = READ_BIT(v);
                SKIP_BITS(v, 35);
            } else {
                SKIP_BITS(v, 43);
            }
            if (JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 1) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 2) ||
                JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 3) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 4) ||
                JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 5) || JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 9) ||
                JUDGE_PROFILE(ptl->sublayers[i].sub_layer_profile, 11))
            {
                ptl->sublayers[i].sub_layer_inbld_flag = READ_BIT(v);
            } else {
                SKIP_BITS(v, 1);
            }
#else
            SKIP_BITS(v, 44);
#endif
        }
        if (ptl->sub_layer_flag[i].sub_layer_level_present_flag) {
            ptl->sublayers[i].sub_layer_level_idc = READ_BITS(v, 8);
        }
    }
}

static void
parse_sub_layer_hrd_parameters(struct bits_vec *v, struct sub_layer_hrd_parameters * sub,
        int cpb_cnt_minus1, int sub_pic_hrd_params_present_flag)
{
    sub->CpbCnt = cpb_cnt_minus1 + 1;
    sub->cpb = calloc(sub->CpbCnt, sizeof(struct cpb));
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
            hrd->hrd_layer[i].nal_hrd = calloc(1, sizeof(struct sub_layer_hrd_parameters));
            parse_sub_layer_hrd_parameters(v, hrd->hrd_layer[i].nal_hrd, hrd->hrd_layer[i].cpb_cnt_minus1, hrd->sub_pic_hrd_params_present_flag);
        }
        if (hrd->vcl_hrd_parameters_present_flag) {
            hrd->hrd_layer[i].vcl_hrd = calloc(1, sizeof(struct sub_layer_hrd_parameters));
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
        float max_framerate = (float)vui->vui_time_scale / (float)vui->vui_num_units_in_tick;
        VDBG(hevc, "max_framerate %f", max_framerate);
        vui->vui_poc_proportional_to_timing_flag = READ_BIT(v);
        if (vui->vui_poc_proportional_to_timing_flag) {
            vui->vui_num_ticks_poc_diff_one_minus1 = GOL_UE(v);
        }
        vui->vui_hrd_parameters_present_flag = READ_BIT(v);
        if (vui->vui_hrd_parameters_present_flag) {
            vui->vui_hrd_para = calloc(1, sizeof(struct hrd_parameters));
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
    for (uint32_t i = 0; i <= t->num_cm_ref_layers_minus1; i ++) {
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
parse_pps_range_extension(struct bits_vec *v, struct pps *pps)
{
    struct pps_range_extension *pps_range = &pps->pps_range_ext;
    if (pps->transform_skip_enabled_flag) {
        pps_range->log2_max_transform_skip_block_size_minus2 = GOL_UE(v);
    } else {
        pps_range->log2_max_transform_skip_block_size_minus2 = 0;
    }
    pps_range->cross_component_prediction_enabled_flag = READ_BIT(v);
    pps_range->chroma_qp_offset_list_enabled_flag = READ_BIT(v);
    if (pps_range->chroma_qp_offset_list_enabled_flag) {
        pps_range->diff_cu_chroma_qp_offset_depth = GOL_UE(v);
        pps_range->chroma_qp_offset_list_len_minus1 = GOL_UE(v);
        for (uint32_t i = 0; i <= pps_range->chroma_qp_offset_list_len_minus1; i ++) {
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
    for (uint32_t i = 0; i < multilayer->num_ref_loc_offsets; i ++) {
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
        multilayer->color_map = calloc(1, sizeof(struct color_mapping_table));
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
        for (int8_t i = 0; i < pps_3d_ext->pps_depth_layers_minus1; i ++) {
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
                for (uint32_t i = 0; i < pps_scc_ext->pps_num_palette_predictor_initializers; i ++) {
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
parse_pps(struct bits_vec *v)
{
    struct pps *pps = calloc(1, sizeof(struct pps));
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
    VDBG(hevc, "tiles_enabled_flag %d, entropy_coding_sync_enabled_flag %d",
         pps->tiles_enabled_flag, pps->entropy_coding_sync_enabled_flag);
    if (pps->tiles_enabled_flag) {
        pps->num_tile_columns_minus1 = GOL_UE(v);
        pps->num_tile_rows_minus1 = GOL_UE(v);
        VDBG(hevc, "num_tile_rows_minus1 %d, num_tile_columns_minus1 %d",
             pps->num_tile_rows_minus1, pps->num_tile_columns_minus1);
        pps->uniform_spacing_flag = READ_BIT(v);
        if (!pps->uniform_spacing_flag) {
            pps->column_width_minus1 = calloc(pps->num_tile_columns_minus1, 4);
            for (uint32_t i = 0; i < pps->num_tile_columns_minus1; i ++) {
                pps->column_width_minus1[i] = GOL_UE(v);
            }
            pps->row_height_minus1 = calloc(pps->num_tile_rows_minus1, 4);
            for (uint32_t i = 0; i< pps->num_tile_rows_minus1; i ++) {
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
        // pps->pps_range_ext = calloc(1, sizeof(struct pps_range_extension));
        parse_pps_range_extension(v, pps);
    }
    if (pps->pps_multilayer_extension_flag) {
        pps->pps_multilayer_ext = calloc(1, sizeof(struct pps_multilayer_extension));
        parse_pps_multilayer_extension(v, pps->pps_multilayer_ext);
    }
    if (pps->pps_3d_extension_flag) {
        pps->pps_3d_ext = calloc(1, sizeof(struct pps_3d_extension));
        parse_pps_3d_extension(v, pps->pps_3d_ext);
    }
    if (pps->pps_scc_extension_flag) {
        parse_pps_scc_extension(v, &pps->pps_scc_ext);
    }
    if (pps->pps_extension_4bits) {
        while(more_rbsp_data(v)) {
            // uint8_t pps_extension_data_flag = READ_BIT(v);
            //for now, no meaningful use for pps_extension_data_flag, just skip
            SKIP_BITS(v, 1);
        }
    }
    rbsp_trailing_bits(v);
    return pps;
}

static void
parse_lt_ref_set(struct bits_vec *v, struct sps *sps, struct lt_ref_pic_set *lt)
{
    lt->lt_ref_pic_poc_lsb_sps = calloc(sps->num_long_term_ref_pics_sps, 1);
    lt->used_by_curr_pic_lt_sps_flag = calloc(sps->num_long_term_ref_pics_sps, 1);
    for (uint32_t i = 0; i < sps->num_long_term_ref_pics_sps; i ++) {
        lt->lt_ref_pic_poc_lsb_sps[i] = READ_BITS(v, 8);
        lt->used_by_curr_pic_lt_sps_flag[i] = READ_BIT(v);
    }
}

/* 7.4.8  may be present in an SPS or in a slice header. Depending on whether the
 *   syntax structure is included in a slice header or an SPS 
 *   – If present in a slice header, the st_ref_pic_set( stRpsIdx ) syntax structure specifies the short-term RPS of the current
        picture (the picture containing the slice), and the following applies:
        – The content of the st_ref_pic_set( stRpsIdx ) syntax structure shall be the same in all slice headers of the current
        picture.
        – The value of stRpsIdx shall be equal to the syntax element num_short_term_ref_pic_sets in the active SPS.
        – The short-term RPS of the current picture is also referred to as the num_short_term_ref_pic_sets-th candidate
        short-term RPS in the semantics specified in the remainder of this clause.
    – Otherwise (present in an SPS), the st_ref_pic_set( stRpsIdx ) syntax structure specifies a candidate short-term RPS,
        and the term "the current picture" in the semantics specified in the remainder of this clause refers to each picture that
        has short_term_ref_pic_set_idx equal to stRpsIdx in a CVS that has the SPS as the active SPS.
    */
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
        for (uint32_t i = 0; i < st->num_negative_pics; i ++) {
            st->delta_poc_s0_minus1[i] = GOL_UE(v);
            st->used_by_curr_pic_s0_flag[i] = READ_BIT(v);
            //see 7-65
            // UsedByCurrPicS0[idx][i] = used_by_curr_pic_s0_flag[i];
        }
        for (uint32_t i = 0; i < st->num_positive_pics; i ++) {
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
                for (uint32_t i = 0; i <= scc->sps_num_palette_predictor_initializers_minus1; i++) {
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
parse_sps(struct hevc_nalu_header * h, struct bits_vec *v, struct vps *vps)
{
    // hexdump(stdout, "sps:", data, len);
    struct sps *sps = calloc(1, sizeof(struct sps));
    sps->sps_video_parameter_set_id = READ_BITS(v, 4);
    VDBG(hevc, "sps: sps_video_parameter_set_id %d", sps->sps_video_parameter_set_id);
    if (h->nuh_layer_id == 0) {
        sps->sps_max_sub_layer_minus1 = READ_BITS(v, 3);
    } else {
        sps->sps_ext_or_max_sub_layers_minus1 = READ_BITS(v, 3);
        //nuh_layer_id != 0 means the value is sps_ext_or_max_sub_layers_minus1
        //sps_max_sub_layer_minus1 should less than 7, otherwise get from vps
        if (sps->sps_ext_or_max_sub_layers_minus1 == 7) {
            sps->sps_max_sub_layer_minus1 = vps->vps_max_sub_layers_minus1;
        } else {
            sps->sps_max_sub_layer_minus1 = sps->sps_ext_or_max_sub_layers_minus1;
        }
    }
    assert(sps->sps_max_sub_layer_minus1 < 7);
    int MultiLayerExtSpsFlag = (h->nuh_layer_id != 0 && sps->sps_max_sub_layer_minus1 == 7);
    if (!MultiLayerExtSpsFlag) {
        //almost must go here
        sps->sps_temporal_id_nesting_flag = READ_BIT(v);
        // when sps_temporal_id_nesting_flag should be 1 if sps_max_sub_layers_minus1 is 0
        VDBG(hevc, "sps_max_sub_layer_minus1 %d", sps->sps_max_sub_layer_minus1);
        parse_profile_tier_level(v, &sps->sps_profile_tier_level, 1, sps->sps_max_sub_layer_minus1);
    }
    sps->sps_seq_parameter_set_id = GOL_UE(v);
    //less than 16
    VDBG(hevc, "sps: sps_seq_parameter_set_id %d", sps->sps_seq_parameter_set_id);
    
    if (MultiLayerExtSpsFlag) {
        // uint8_t update_rep_format_flag = READ_BIT(v);
        if (READ_BIT(v)) {
            sps->sps_rep_format_idx = READ_BITS(v, 8);
        }
    } else {
        sps->chroma_format_idc = GOL_UE(v);
        if (sps->chroma_format_idc == CHROMA_444) {
            sps->separate_colour_plane_flag = READ_BIT(v);
        }
        VDBG(hevc, "chroma_format_idc %d, separate_colour_plane_flag %d",
            sps->chroma_format_idc, sps->separate_colour_plane_flag);
        sps->pic_width_in_luma_samples = GOL_UE(v);
        sps->pic_height_in_luma_samples = GOL_UE(v);
        VDBG(hevc, "pic_width_in_luma_samples %d, pic_height_in_luma_samples %d",
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
        VDBG(hevc, "bit_depth_luma_minus8 %d, bit_depth_chroma_minus8 %d",
            sps->bit_depth_luma_minus8, sps->bit_depth_chroma_minus8);
    }

    sps->log2_max_pic_order_cnt_lsb_minus4 = GOL_UE(v);
    //should be less than 12
    if (LIKELY(!MultiLayerExtSpsFlag)) {
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
    
    sps->scaling_list_enabled_flag = READ_BIT(v);
    if (sps->scaling_list_enabled_flag) {
        uint8_t sps_infer_scaling_list_flag = 0;
        if (UNLIKELY(MultiLayerExtSpsFlag)) {
            sps_infer_scaling_list_flag = READ_BIT(v);
        }
        if (UNLIKELY(sps_infer_scaling_list_flag)) {
            sps->sps_scaling_list_ref_layer_id = READ_BITS(v, 6);
        } else {
            sps->sps_scaling_list_data_present_flag = READ_BIT(v);
            if (sps->sps_scaling_list_data_present_flag) {
                sps->list_data = calloc(1, sizeof(struct scaling_list_data));
                parse_scaling_list_data(v, sps->list_data);
            }
        }
    }
    sps->amp_enabled_flag = READ_BIT(v);
    sps->sample_adaptive_offset_enabled_flag = READ_BIT(v);
    sps->pcm_enabled_flag = READ_BIT(v);
    if (sps->pcm_enabled_flag) {
        sps->pcm = calloc(1, sizeof(struct pcm));
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
        for (uint32_t i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
            parse_st_ref_set(v, sps->sps_st_ref + i, i, sps->num_short_term_ref_pic_sets);
        }
    }
    sps->long_term_ref_pics_present_flag = READ_BIT(v);
    if (sps->long_term_ref_pics_present_flag) {
        sps->num_long_term_ref_pics_sps = GOL_UE(v);
        sps->sps_lt_ref = calloc(1, sizeof(struct lt_ref_pic_set));
        parse_lt_ref_set(v, sps, sps->sps_lt_ref);
    }
    sps->sps_temporal_mvp_enabled_flag = READ_BIT(v);
    sps->strong_intra_smoothing_enabled_flag = READ_BIT(v);
    sps->vui_parameters_present_flag = READ_BIT(v);
    if (sps->vui_parameters_present_flag) {
        sps->vui = calloc(1, sizeof(struct vui_parameters));
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
        // sps->sps_range_ext = calloc(1, sizeof(struct sps_range_extension));
        parse_sps_range_ext(&sps->sps_range_ext, v);
    }
    VDBG(hevc, "transform_skip_rotation_enabled_flag %d",
         sps->sps_range_ext.transform_skip_rotation_enabled_flag);
    if (sps->sps_multilayer_extension_flag) {
        sps->sps_multilayer_ext.inter_view_mv_vert_constraint_flag = READ_BIT(v);
    }
    if (sps->sps_3d_extension_flag) {
        parse_sps_3d_ext(sps->sps_3d_ext, 0, v);
        parse_sps_3d_ext(sps->sps_3d_ext+1, 1, v);
    }
    if (sps->sps_scc_extension_flag) {
        int numComps = sps->chroma_format_idc == 0 ? 1: 3;
        parse_sps_scc_ext(&sps->sps_scc_ext, v, numComps);
    }
    if (sps->sps_extension_4bits) {
        while(more_rbsp_data(v)) {
            // uint8_t sps_extension_data_flag = READ_BIT(v);
            SKIP_BITS(v, 1);
        }
    }
    rbsp_trailing_bits(v);
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

struct rps {
    int* PocStCurrBefore;
    int* PocStCurrAfter;
    int* PocStFoll;
    int* PocLtCurr;
    int* PocLtFoll;
    int NumPocStCurrBefore;
    int NumPocStCurrAfter;
    int NumPocStFoll;
    int NumPocLtCurr;
    int NumPocLtFoll;

    int* RefPicList0;
    int* RefPicList1;

    int* CurrDeltaPocMsbPresentFlag;
    int *FollDeltaPocMsbPresentFlag;

    int *RefPicSetLtCurr;
    int *RefPicSetLtFoll;
    int *RefPicSetStCurrBefore;
    int *RefPicSetStCurrAfter;
    int *RefPicSetStFoll;

    int RpRefPicAvailFlag;
};

enum rps_marking {
    UNUSED_FOR_REFERENCE = -1,
    USED_FOR_SHORT_TERM_REFERENCE = 0,
    USED_FOR_LONG_TERM_REFERENCE = 1,
};


static int
PicOrderCnt(struct hevc_slice *hslice, struct sps *sps, int picX, bool IRAP)
{
    /* Random access point pictures, where a decoder may start decoding a coded video sequence.
      These are referred to as Intra Random Access Pictures (IRAP). Three IRAP picture types exist:
      Instantaneous Decoder Refresh (IDR), Clean Random Access (CRA), and Broken Link Access (BLA).
      The decoding process for a coded video sequence always starts at an IRAP. */
    int prevTid0Pic = 0;//has TemporalId == 0 and not RASL, RADL, SLNR
    int PicOrderCntMsb;
    int PicOrderCntVal = 0;

    struct slice_segment_header *slice = hslice->slice;
    //see 8.3.1
    int prevPicOrderCntMsb = 0;// = PicOrderCntMsb;
    int prevPicOrderCntLsb = 0;
    //see (7-8)
    int MaxPicOrderCntLsb = 2 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    if (IRAP) {
        PicOrderCntMsb = 0;
    } else {
        //see (8-1)
        if ((slice->slice_pic_order_cnt_lsb < (uint32_t)prevPicOrderCntLsb) &&
            ((prevPicOrderCntLsb - slice->slice_pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2))) {
            PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
        } else if ((slice->slice_pic_order_cnt_lsb > (uint32_t)prevPicOrderCntLsb ) &&
            ((slice->slice_pic_order_cnt_lsb - prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2))) {
            PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
        } else {
            PicOrderCntMsb = prevPicOrderCntMsb;
        }
        //for BLA 
        uint16_t pic_type = hslice->nalu->nal_unit_type;
        if (pic_type == BLA_N_LP ||pic_type == BLA_W_LP || pic_type == BLA_W_RADL) {
            PicOrderCntMsb = 0;
        }
        //see (8-2)
        PicOrderCntVal = PicOrderCntMsb + slice->slice_pic_order_cnt_lsb;
    }
    return PicOrderCntVal;
}

static int
DiffPicOrderCnt(struct hevc_slice *hslice, struct sps *sps, int picA, int picB)
{
    return PicOrderCnt(hslice, sps, picA, true) - PicOrderCnt(hslice, sps, picB, true);
}

static bool
is_ref_pic_in_dpb(int picX, int PicOrderCntVal, int Poc, int layerid, int currPicLayerId)
{
    if (PicOrderCntVal == Poc && layerid == currPicLayerId) {
        return true;
    }
    return false;
}

/* see 8.3.2 
 This process is invoked once per picture, after decoding of a slice header
 but prior to the decoding of any coding unit and prior to the decoding process
 for reference picture list construction 
*/
static struct rps *
process_reference_picture_set(bool idr, struct hevc_slice *hslice, struct hevc_param_set * hps)
{
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct rps *rps = calloc(1, sizeof(*rps));
    int currPicLayerId = headr->nuh_layer_id;
    int picX = 0;

    //see (7-8)
    int MaxPicOrderCntLsb = 2 << (hps->sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    int PicOrderCntVal = PicOrderCnt(hslice, hps->sps, picX, false);
    /* Five lists of picture order count values are constructed to derive the RPS.
     These five lists are PocStCurrBefore, PocStCurrAfter, PocStFoll, PocLtCurr and PocLtFoll,
     with NumPocStCurrBefore, NumPocStCurrAfter, NumPocStFoll, NumPocLtCurr and NumPocLtFoll
     number of elements, respectively.
     If the current picture is an IDR picture, PocStCurrBefore, PocStCurrAfter,
     PocStFoll, PocLtCurr and PocLtFoll are all set to be empty, and NumPocStCurrBefore, 
     NumPocStCurrAfter, NumPocStFoll, NumPocLtCurr and NumPocLtFoll are all set equal to 0.*/
    if (idr) {
        rps->PocStCurrBefore = NULL;
        rps->PocStCurrAfter = NULL;
        rps->PocStFoll = NULL;
        rps->PocLtCurr = NULL;
        rps->PocLtFoll = NULL;

        rps->NumPocStCurrBefore = 0;
        rps->NumPocStCurrAfter = 0;
        rps->NumPocStFoll = 0;
        rps->NumPocLtCurr = 0;
        rps->NumPocLtFoll = 0;
    } else {

        /*value  CurrRpsIdx in the range of 0 to num_short_term_ref_pic_sets - 1, */
        /* see (8-5) */
        int i = 0, j = 0, k = 0;
        for (; i < slice->NumNegativePics[slice->CurrRpsIdx] ; i++ ) {
            if (slice->UsedByCurrPicS0[slice->CurrRpsIdx][i]) {
                rps->PocStCurrBefore[j++] = PicOrderCntVal + slice->DeltaPocS0[slice->CurrRpsIdx][i];
            } else {
                rps->PocStFoll[k++] = PicOrderCntVal + slice->DeltaPocS0[slice->CurrRpsIdx][i];
            }
        }
        rps->NumPocStCurrBefore = j;
        for (i = 0, j = 0; i < slice->NumPositivePics[slice->CurrRpsIdx]; i++) {
            if (slice->UsedByCurrPicS1[slice->CurrRpsIdx][i]) {
                rps->PocStCurrAfter[j++] = PicOrderCntVal + slice->DeltaPocS1[slice->CurrRpsIdx][i];
            } else {
                rps->PocStFoll[k++] = PicOrderCntVal + slice->DeltaPocS1[slice->CurrRpsIdx][i];
            }
        }
        rps->NumPocStCurrAfter = j;
        rps->NumPocStFoll = k;
        for (i = 0, j = 0, k = 0; i < (int)slice->num_long_term_sps + (int)slice->num_long_term_pics; i++) {
            int pocLt = slice->PocLsbLt[i];
            if (slice->terms[i].delta_poc_msb_present_flag) {
                pocLt += PicOrderCntVal - slice->DeltaPocMsbCycleLt[i] * MaxPicOrderCntLsb -
                    (PicOrderCntVal & ( MaxPicOrderCntLsb - 1));
            }
            if (slice->UsedByCurrPicLt[i]) {
                rps->PocLtCurr[j] = pocLt;
                rps->CurrDeltaPocMsbPresentFlag[j++] = slice->terms[i].delta_poc_msb_present_flag;
            } else {
                rps->PocLtFoll[k] = pocLt;
                rps->FollDeltaPocMsbPresentFlag[k++] = slice->terms[i].delta_poc_msb_present_flag;
            }
        }
        rps->NumPocLtCurr = j;
        rps->NumPocLtFoll = k;
    }

    /* (8-6)*/
    /* step 1 */
    for (int i = 0; i < rps->NumPocLtCurr; i++) {
        if (!rps->CurrDeltaPocMsbPresentFlag[i]) {
            if (is_ref_pic_in_dpb(picX, PicOrderCntVal & (MaxPicOrderCntLsb-1), rps->PocLtCurr[i], hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
                rps->RefPicSetLtCurr[i] = picX;
            } else {
                rps->RefPicSetLtCurr[i] = -1; //means no reference picture
            }
        } else {
            if (is_ref_pic_in_dpb(picX, PicOrderCntVal, rps->PocLtCurr[i], hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
                rps->RefPicSetLtCurr[i] = picX;
            } else {
                rps->RefPicSetLtCurr[i] = -1;
            }
        }
    }
    for (int i = 0; i < rps->NumPocLtFoll; i ++) {
        if (!rps->FollDeltaPocMsbPresentFlag[i]) {
            if (is_ref_pic_in_dpb(picX, PicOrderCntVal & (MaxPicOrderCntLsb-1), rps->PocLtFoll[i],hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
                rps->RefPicSetLtFoll[i] = picX;
            } else {
                rps->RefPicSetLtFoll[i] = -1;
            }
        } else {
            if (is_ref_pic_in_dpb(picX, PicOrderCntVal, rps->PocLtFoll[i], hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
                rps->RefPicSetLtFoll[i] = picX;
            } else {
                rps->RefPicSetLtFoll[i] = -1;
            }
        }
    }
    /* step 2 */
    /* All reference pictures that are included in RefPicSetLtCurr or RefPicSetLtFoll
     and have nuh_layer_id equal to currPicLayerId are marked as "used for long-term reference".*/
    
    /* step 3*/
    for (int i = 0; i < rps->NumPocStCurrBefore; i++ ) {
        if (is_ref_pic_in_dpb(picX, PicOrderCntVal & (MaxPicOrderCntLsb-1), rps->PocStCurrBefore[i], hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
            rps->RefPicSetStCurrBefore[i] = picX;
        } else {
            rps->RefPicSetStCurrBefore[i] = -1;
        }
    }
    for (int i = 0; i < rps->NumPocStCurrAfter; i++ ) {
        if (is_ref_pic_in_dpb(picX, PicOrderCntVal, rps->PocStCurrAfter[i], hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
            rps->RefPicSetStCurrAfter[i] = picX;
        } else {
            rps->RefPicSetStCurrAfter[i] = -1;
        }
    }
    for (int i = 0; i < rps->NumPocStFoll; i++ ) {
        if (is_ref_pic_in_dpb(picX, PicOrderCntVal, rps->PocStFoll[i], hps->vps->vps_ext->layer_id_in_nuh[i], currPicLayerId)) {
            rps->RefPicSetStFoll[i] = picX;
        } else {
            rps->RefPicSetStFoll[i] = -1;
        }
    }
    /*step 4*/
    /*All reference pictures in the DPB that are not included in RefPicSetLtCurr, RefPicSetLtFoll,
    RefPicSetStCurrBefore, RefPicSetStCurrAfter, or RefPicSetStFoll and have nuh_layer_id equal to
    currPicLayerId are marked as "unused for reference"*/

    return rps;
}
#if 0
/* 8.3.3 
 This process is invoked once per coded picture when the current picture is a BLA picture
 or is a CRA picture with NoRaslOutputFlag equal to 1.*/
static void
process_generating_unavailable_reference_pictures(struct hevc_nalu_header *headr, struct slice_segment_header *slice, struct rps *rps)
{
    if (headr->nal_unit_type == BLA_N_LP || headr->nal_unit_type == BLA_W_LP ||
     headr->nal_unit_type == BLA_W_RADL || headr->nal_unit_type == CRA_NUT) {
        for (int i = 0; i < rps->NumPocStFoll; i ++) {
            PicOrderCntVal = rps->PocStFoll[i];
            PicOutputFlag = 0;
            USED_FOR_SHORT_TERM_REFERENCE;
            rps->RefPicSetStFoll[i] = ;
        }
        for (int i = 0; i < rps->NumPocLtFoll; i ++) {
            rps->RefPicSetLtFoll[i] = UNUSED_FOR_REFERENCE;
            PicOrderCntVal = rps->PocLtFoll[i];
            USED_FOR_LONG_TERM_REFERENCE
            rps->RefPicSetLtFoll[i] = ;
        }
     }
}

//8.3.3.2
static void
process_gerneration_of_one_unavailable_picture()
{
    luma = 1 << (BitDepth -1);
    if (ChromaArrayType != 0) {
        cr = cb = 1 << (BitDepth -1);
    }

}
#endif
/*see 8.3.4
This process is invoked at the beginning of the decoding process for each P or B slice */
static void
process_reference_picture_lists_construction(struct hevc_slice *hslice, struct hevc_param_set * hps)
{
    struct rps *rps = hslice->rps;
    struct slice_segment_header *slice = hslice->slice;
    if (slice->slice_type == SLICE_TYPE_I) {
        return ;
    }
    int NumRpsCurrTempList0 = MAX((int)(slice->num_ref_idx_l0_active_minus1) + 1, slice->NumPicTotalCurr);
    //see (8-8)/ f-65 , F.8.3.4 Decoding process for reference picture lists construction
    int rIdx = 0;
    int currPic = 0;
    int * RefPicListTemp0 = calloc(NumRpsCurrTempList0, sizeof(int));
    while (rIdx < NumRpsCurrTempList0) {
        for (int i = 0; i < rps->NumPocStCurrBefore && rIdx < NumRpsCurrTempList0; rIdx++, i++)
            RefPicListTemp0[rIdx] = rps->RefPicSetStCurrBefore[i];
        for (int i = 0; i < rps->NumPocStCurrAfter && rIdx < NumRpsCurrTempList0; rIdx++, i++)
            RefPicListTemp0[rIdx] = rps->RefPicSetStCurrAfter[i];
        for (int i = 0; i < rps->NumPocLtCurr && rIdx < NumRpsCurrTempList0; rIdx++, i++)
            RefPicListTemp0[rIdx] = rps->RefPicSetLtCurr[i];
        if (hps->pps->pps_scc_ext.pps_curr_pic_ref_enabled_flag)
            RefPicListTemp0[rIdx++] = currPic;
    }
    //see (8-9) / f-66
    for (rIdx = 0; rIdx <= (int)slice->num_ref_idx_l0_active_minus1; rIdx++) {
        rps->RefPicList0[rIdx] = slice->ref_pic_list_modification_flag_l0 ? RefPicListTemp0[slice->list_entry_l0[rIdx]] : RefPicListTemp0[rIdx];
    }
    if (hps->pps->pps_scc_ext.pps_curr_pic_ref_enabled_flag && !slice->ref_pic_list_modification_flag_l0 &&
            NumRpsCurrTempList0 > (slice->num_ref_idx_l0_active_minus1 + 1)) {
        rps->RefPicList0[slice->num_ref_idx_l0_active_minus1] = currPic;
    }
    if (slice->slice_type == SLICE_TYPE_B) {
        int NumRpsCurrTempList1 = MAX((int)slice->num_ref_idx_l1_active_minus1 + 1, slice->NumPicTotalCurr);
        int * RefPicListTemp1 = calloc(NumRpsCurrTempList1, sizeof(int));
        /* (8-10) */
        rIdx = 0;
        while (rIdx < NumRpsCurrTempList1) {
            for (int i = 0; i < rps->NumPocStCurrAfter && rIdx < NumRpsCurrTempList1; rIdx++, i++ )
                RefPicListTemp1[rIdx] = rps->RefPicSetStCurrAfter[i];
            for (int i = 0; i < rps->NumPocStCurrBefore && rIdx < NumRpsCurrTempList1; rIdx++, i++ )
                RefPicListTemp1[rIdx] = rps->RefPicSetStCurrBefore[i];
            for (int i = 0; i < rps->NumPocLtCurr && rIdx < NumRpsCurrTempList1; rIdx++, i++ )
                RefPicListTemp1[rIdx] = rps->RefPicSetLtCurr[i];
            if (hps->pps->pps_scc_ext.pps_curr_pic_ref_enabled_flag)
                RefPicListTemp1[rIdx++] = currPic;
        }
        /* (8-11) */
        for( rIdx = 0; rIdx <= (int)slice->num_ref_idx_l1_active_minus1; rIdx++) {
            rps->RefPicList1[rIdx] = slice->ref_pic_list_modification_flag_l1 ? RefPicListTemp1[slice->list_entry_l1[rIdx]] :
                RefPicListTemp1[rIdx];
        }

        free(RefPicListTemp1);
    }

    free(RefPicListTemp0);
}

/* 8.3.5,
  at the beginning of the decoding process for each P or B slice,
   after decoding of the slice header as well as the invocation of the
   decoding process for reference picture set as specified in clause 8.3.2
*/

//I.8.3.5, when the current slice is a P or B slice.
static void
process_target_reference_index_for_residual_predication(struct hevc_slice *hslice, struct hevc_param_set * hps)
{
    struct rps *rps = hslice->rps;
    struct slice_segment_header *slice = hslice->slice;
    if (slice->slice_type == SLICE_TYPE_I) {
        return;
    }

    int RpRefPicAvailFlagL0 = 0, RpRefPicAvailFlagL1 = 0;
    struct sps *sps = hps->sps;
    int RpRefIdxL0 = -1, RpRefIdxL1 = -1;
    int CurrPic = 0;

    for (int x = 0; x <= ((slice->slice_type == SLICE_TYPE_B) ? 1 : 0); x++) {
        int minPocDiff = (1 << 15) - 1;
        if (x == 0) {
            for (uint32_t i = 0; i <= slice->num_ref_idx_l0_active_minus1; i ++) {
                int pocDiff = ABS(DiffPicOrderCnt(hslice, sps, CurrPic, rps->RefPicList0[i]));
                if (pocDiff !=0 && pocDiff < minPocDiff) {
                    minPocDiff = pocDiff;
                    RpRefIdxL0 = 0;
                    RpRefPicAvailFlagL0 = 1;
                }
            }
        } else if (x == 1) {
            for (uint32_t i = 0; i <= slice->num_ref_idx_l1_active_minus1; i++) {
                int pocDiff = ABS(DiffPicOrderCnt(hslice, sps, CurrPic, rps->RefPicList1[i]));
                if (pocDiff !=0 && pocDiff < minPocDiff) {
                    minPocDiff = pocDiff;
                    RpRefIdxL1 = 0;
                    RpRefPicAvailFlagL1 = 1;
                }
            }
        }
    }
    //see I.8.3.2
    int DispAvailFlag = 1;//TODO
    //see (I-61)
    rps->RpRefPicAvailFlag = (RpRefPicAvailFlagL0 || RpRefPicAvailFlagL1) && DispAvailFlag;
}

static void
parse_dpb_size(struct bits_vec *v, struct dpb_size* d, struct vps* vps, int NumOutputLayerSets,
    int OlsIdxToLsIdx[], int MaxSubLayersInLayerSetMinus1[],
    int NecessaryLayerFlag[][64])
{
    // struct vps_extension *vps_ext = vps->vps_ext;
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
    hrd->vps_vui_bsp_hrd = calloc(hrd->vps_num_add_hrd_params, sizeof(struct hrd_parameters));
    for (int i = vps_num_hrd_parameters; i < vps_num_hrd_parameters + (int)hrd->vps_num_add_hrd_params; i++) {
        uint8_t cprms_add_present_flag = 0;
        if (i > 0) {
            cprms_add_present_flag = READ_BIT(v);
        }
        uint32_t num_sub_layer_hrd_minus1 = GOL_UE(v);
        parse_hrd_parameters(v, hrd->vps_vui_bsp_hrd + i, cprms_add_present_flag,
                num_sub_layer_hrd_minus1);
    }
    if (hrd->vps_num_add_hrd_params + vps_num_hrd_parameters > 0) {
        hrd->num_partitions_in_scheme_minus1 = calloc(NumOutputLayerSets, sizeof(uint32_t *));
        for (int h = 1; h < NumOutputLayerSets; h ++) {
            uint32_t num_signalled_partitioning_schemes = GOL_UE(v);
            hrd->num_partitions_in_scheme_minus1[h] = calloc(num_signalled_partitioning_schemes, sizeof(uint32_t));
            for (uint32_t j = 1; j < num_signalled_partitioning_schemes; j++) {
                hrd->num_partitions_in_scheme_minus1[h][j] = GOL_UE(v);
                for(uint32_t k = 0; k <= hrd->num_partitions_in_scheme_minus1[h][j]; k++) {
                    for (int r = 0; r < vps->NumLayersInIdList[OlsIdxToLsIdx[h]]; r++ ) {
                        // layer_included_in_partition_flag[h][j][k][r] = READ_BIT(v);
                        SKIP_BITS(v, 1);
                    }
                }
            }
            uint32_t BpBitRate[64][2][2][2][2];
            uint32_t BpbSize[64][2][2][2][2];
            for (uint32_t i = 0; i < num_signalled_partitioning_schemes + 1; i ++) {
                for(int t = 0; t <= MaxSubLayersInLayerSetMinus1[OlsIdxToLsIdx[h]]; t++ ) {
                    uint32_t num_bsp_schedules_minus1 = GOL_UE(v);
                    for (uint32_t j = 0; j < num_bsp_schedules_minus1; j ++) {
                        for (uint32_t k =0; k < hrd->num_partitions_in_scheme_minus1[h][i]; j ++) {
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
        vui->avg_bit_rate = calloc(NumLayerSets, sizeof(uint16_t *));
        vui->max_bit_rate = calloc(NumLayerSets, sizeof(uint16_t *));
        vui->constant_pic_rate_idc = calloc(NumLayerSets, sizeof(uint16_t *));
        vui->avg_pic_rate = calloc(NumLayerSets, sizeof(uint16_t *));
        for (int i = vps->vps_base_layer_internal_flag ? 0 : 1; i < NumLayerSets; i++) {
            vui->avg_bit_rate[i] = calloc((MaxSubLayersInLayerSetMinus1[i] + 1), sizeof(uint16_t));
            vui->max_bit_rate[i] = calloc((MaxSubLayersInLayerSetMinus1[i] + 1), sizeof(uint16_t));
            vui->constant_pic_rate_idc[i] = calloc((MaxSubLayersInLayerSetMinus1[i] + 1), sizeof(uint8_t));
            vui->avg_pic_rate[i] = calloc((MaxSubLayersInLayerSetMinus1[i] + 1), sizeof(uint16_t));
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
    vui->vps_video_signal_info = calloc(vui->vps_num_video_signal_info_minus1, sizeof(struct video_signal_info));
    for (int i = 0; i <= vui->vps_num_video_signal_info_minus1; i++ ) {
        parse_video_signal_info(v, vui->vps_video_signal_info + i);
    }
    if (video_signal_info_idx_present_flag && vui->vps_num_video_signal_info_minus1 > 0 ) {
        vui->vps_video_signal_info_idx = calloc(MaxLayersMinus1, 1);
        for(int i = vps->vps_base_layer_internal_flag ? 0 : 1; i <= MaxLayersMinus1; i++ ){
            vui->vps_video_signal_info_idx[i] = READ_BITS(v, 4);
        }
    }
    // uint8_t tiles_not_in_use_flag = READ_BIT(v);
    if (!READ_BIT(v)) {
        uint8_t *tiles_in_use_flag = calloc(MaxLayersMinus1, 1);

        vui->loop_filter_not_across_tiles_flag = calloc(MaxLayersMinus1, 1);
        for (int i = vps->vps_base_layer_internal_flag ? 0 : 1; i <= MaxLayersMinus1; i++ ) {
            tiles_in_use_flag[i] = READ_BIT(v);
            if(tiles_in_use_flag[i]) {
                vui->loop_filter_not_across_tiles_flag[i] = READ_BIT(v);
            }
        }
        vui->tile_boundaries_aligned_flag = calloc(MaxLayersMinus1, sizeof(uint8_t *));
        for (int i = vps->vps_base_layer_internal_flag ? 1 : 2; i <= MaxLayersMinus1; i++) {
            vui->tile_boundaries_aligned_flag[i] = calloc(vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]], 1);
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
        vui->wpp_in_use_flag = calloc(MaxLayersMinus1, 1);
        for (int i = vps->vps_base_layer_internal_flag ? 0 : 1; i <= MaxLayersMinus1; i++ ) {
            vui->wpp_in_use_flag[i] = READ_BIT(v);
        }
    }
    vui->single_layer_for_non_irap_flag = READ_BIT(v);
    vui->higher_layer_irap_skip_flag = READ_BIT(v);
    vui->ilp_restricted_ref_layers_flag = READ_BIT(v);
    if (vui->ilp_restricted_ref_layers_flag) {
        vui->min_horizontal_ctu_offset_plus1 = calloc(MaxLayersMinus1, sizeof(uint32_t *));
        for (int i = 0; i < MaxLayersMinus1; i++) {
            vui->min_horizontal_ctu_offset_plus1[i] = calloc(vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]], sizeof(uint32_t)); 
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
        vui->vui_hrd = calloc(1, sizeof(struct vps_vui_bsp_hrd_params));
        parse_vps_vui_bsp_hrd_params(v, vui->vui_hrd, vps, vps->vps_timing_info->vps_num_hrd_parameters,
            NumOutputLayerSets, OlsIdxToLsIdx, MaxSubLayersInLayerSetMinus1, SubPicHrdFlag);
    }
    vui->base_layer_parameter_set_compatibility_flag = calloc(MaxLayersMinus1, 1);
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
    VDBG(hevc, "vps hrd num %d", vps_tim->vps_num_hrd_parameters);
    for (uint32_t i = 0; i < vps_tim->vps_num_hrd_parameters; i ++) {
        vps_tim->hrd_layer_set_idx[i] =  GOL_UE(v);
        if (i > 0) {
            vps_tim->cprms_present_flag[i] = READ_BIT(v);
        }
        vps_tim->vps_hrd_para[i] = calloc(1, sizeof(struct hrd_parameters));
        parse_hrd_parameters(v, vps_tim->vps_hrd_para[i], vps_tim->cprms_present_flag[i], vps->vps_max_sub_layers_minus1);
    }
}

static void
parse_vps_extension(struct bits_vec *v, struct vps *vps, struct vps_extension *vps_ext)
{
    if (vps->vps_max_layers_minus1 > 0 && vps->vps_base_layer_internal_flag) {
        vps_ext->vps_ext_profile_tier_level = calloc(1, sizeof(struct profile_tier_level));
        parse_profile_tier_level(v, vps_ext->vps_ext_profile_tier_level, 0, vps->vps_max_sub_layers_minus1);
    }
    vps_ext->splitting_flag = READ_BIT(v);
    int NumScalabilityTypes = 0;
    int MaxLayersMinus1 = MIN(62, vps->vps_max_layers_minus1);
    for (int i = 0; i < 16; i ++) {
        /* Table F.1 dimensions:
            index 0: texture or depth,  depthLayerFlag
            index 1: multiview,         viewoderIdx
            index 2: spatial/quality scalability,  dependencyId
            index 3: auxiliary,          auxid
            index 4-15: reserved
        */
        int scalability_mask_flag = READ_BIT(v);
        // printf("%d: %d\n", i, scalability_mask_flag);
        vps_ext->scalability_mask_flag |= (scalability_mask_flag << i);
        NumScalabilityTypes += scalability_mask_flag;
    }
    assert(NumScalabilityTypes<=16);
    vps_ext->dimension_id_len_minus1 = calloc(NumScalabilityTypes - vps_ext->splitting_flag, 1);
    for (int j = 0; j < NumScalabilityTypes - vps_ext->splitting_flag; j ++) {
        vps_ext->dimension_id_len_minus1[j] = READ_BITS(v, 3);
    }
    /*(F-2)*/
    int dimBitOffset[16];
    dimBitOffset[0] = 0;
    for (int j = 1; j < NumScalabilityTypes; j ++) {
        for (int dimIdx = 0; dimIdx < j; dimIdx ++) {
            dimBitOffset[j] += (vps_ext->dimension_id_len_minus1[dimIdx] + 1);
        }
    }
    assert(5 - dimBitOffset[NumScalabilityTypes-1] == vps_ext->dimension_id_len_minus1[NumScalabilityTypes-1]);
    assert(dimBitOffset[NumScalabilityTypes] == 6);
    assert(NumScalabilityTypes > 0);
    assert(dimBitOffset[NumScalabilityTypes-1] < 6);

    // vps_ext->vps_nuh_layer_id_present_flag = READ_BIT(v);
    uint8_t vps_nuh_layer_id_present_flag = READ_BIT(v);
    if (vps_nuh_layer_id_present_flag) {
        vps_ext->layer_id_in_nuh = calloc(MaxLayersMinus1 + 1, 1);
    }
    if (!vps_ext->splitting_flag) {
        vps_ext->dimension_id = calloc((MaxLayersMinus1+1), sizeof(uint8_t *));
    }
    for (int i = 0; i <= MaxLayersMinus1; i ++) {
        if(vps_nuh_layer_id_present_flag && i > 0) {
            vps_ext->layer_id_in_nuh[i] = READ_BITS(v, 6);
            assert(vps_ext->layer_id_in_nuh[i] > vps_ext->layer_id_in_nuh[i-1]);
        } else {
            vps_ext->layer_id_in_nuh[i] = i;
        }
        vps->LayerIdxInVps[vps_ext->layer_id_in_nuh[i]] = i;

        vps_ext->dimension_id[i] = calloc(NumScalabilityTypes, sizeof(uint8_t));
        for (int j = 0; j < NumScalabilityTypes; j ++) {
            if (!vps_ext->splitting_flag) {
                if (i ==0) {
                    vps_ext->dimension_id[i][j] = 0;
                } else {
                    vps_ext->dimension_id[i][j] = READ_BITS(v, vps_ext->dimension_id_len_minus1[j]+1);
                }
            } else {
                vps_ext->dimension_id[i][j] = ((vps_ext->layer_id_in_nuh[i]&((1 << dimBitOffset[j+1])-1)) >> dimBitOffset[j]);
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
            if (vps_ext->scalability_mask_flag & (1 << smIdx)) {
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
    vps_ext->highest_layer_idx_plus1 = calloc(vps_ext->num_add_layer_sets, sizeof(uint16_t *));
    for (uint32_t i = 0; i < vps_ext->num_add_layer_sets; i ++) {
        vps_ext->highest_layer_idx_plus1[i] = calloc(NumIndependentLayers, sizeof(uint16_t));
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
        vps_ext->sub_layers_vps_max_minus1 = calloc(MaxLayersMinus1, 1);
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
    for (uint32_t i = vps->vps_base_layer_internal_flag ? 2 : 1;
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


    vps_ext->layer_set_idx_for_ols_minus1 = calloc(NumOutputLayerSets, sizeof(uint16_t));
    vps_ext->output_layer_flag = calloc(NumOutputLayerSets, sizeof(uint8_t *));
    vps_ext->profile_tier_level_idx = calloc(NumOutputLayerSets, sizeof(uint16_t *));
    vps_ext->alt_output_layer_flag = calloc(NumOutputLayerSets, 1);
    for (int i = 1; i < NumOutputLayerSets; i++ ) {
        if( NumLayerSets > 2 && i >= NumLayerSets ) {
            vps_ext->layer_set_idx_for_ols_minus1[i] = READ_BITS(v, log2ceil(NumLayerSets-1));
            // OlsIdxToLsIdx[i] = (i < NumLayerSets) ? i : (layer_set_idx_for_ols_minus1[i] + 1);
        }
        // (F-11)
        OlsIdxToLsIdx[i] = (i < NumLayerSets) ? i : (vps_ext->layer_set_idx_for_ols_minus1[i] + 1); 
        if (i > (int)vps->vps_num_layer_sets_minus1 || defaultOutputLayerIdc == 2) {
            vps_ext->output_layer_flag[i] = calloc(vps->NumLayersInIdList[OlsIdxToLsIdx[i]], 1);
            for (int j = 0; j < vps->NumLayersInIdList[OlsIdxToLsIdx[i]]; j++) {
                vps_ext->output_layer_flag[i][j] = READ_BIT(v);
            }
        }
        // (F-12)
        if (i >= (int)((defaultOutputLayerIdc == 2) ? 0: (vps->vps_num_layer_sets_minus1 + 1))) {
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

        vps_ext->profile_tier_level_idx[i] = calloc(vps->NumLayersInIdList[OlsIdxToLsIdx[i]], sizeof(uint16_t));
        for (int j = 0; j < vps->NumLayersInIdList[OlsIdxToLsIdx[i]]; j++ ) {
            if (NecessaryLayerFlag[i][j] && vps_ext->vps_num_profile_tier_level_minus1 > 0) {
                vps_ext->profile_tier_level_idx[i][j] = READ_BITS(v, log2ceil(vps_ext->vps_num_profile_tier_level_minus1 + 1));
            }
        }
        if( NumOutputLayersInOutputLayerSet[i] == 1 && vps->NumDirectRefLayers[OlsHighestOutputLayerId[i]] > 0) {
            vps_ext->alt_output_layer_flag[i] = READ_BIT(v);
        }
    }
    vps_ext->vps_num_rep_formats_minus1 = GOL_UE(v);
    vps_ext->vps_rep_format = calloc(vps_ext->vps_num_rep_formats_minus1, sizeof(struct vps_rep_format));
    for(uint32_t i = 0; i <= vps_ext->vps_num_rep_formats_minus1; i++ ) {
        parse_rep_format(v, vps_ext->vps_rep_format + i);
    }
    uint8_t rep_format_idx_present_flag = 0;
    if (vps_ext->vps_num_rep_formats_minus1 > 0 ) {
        rep_format_idx_present_flag = READ_BIT(v);
    }
    if (rep_format_idx_present_flag) {
        vps_ext->vps_rep_format_idx = calloc((MaxLayersMinus1 +1), sizeof(uint16_t));
        for (int i = vps->vps_base_layer_internal_flag ? 1 : 0; i <= MaxLayersMinus1; i++ ) {
            vps_ext->vps_rep_format_idx[i] = READ_BITS(v, log2ceil(vps_ext->vps_num_rep_formats_minus1 + 1));
        }
    }
    vps_ext->max_one_active_ref_layer_flag = READ_BIT(v);
    vps_ext->vps_poc_lsb_aligned_flag = READ_BIT(v);
    vps_ext->poc_lsb_not_present_flag = calloc(MaxLayersMinus1+1, 1);
    for (int i = 1; i <= MaxLayersMinus1; i++) {
        if (vps->NumDirectRefLayers[vps_ext->layer_id_in_nuh[i]] == 0 ) {
            vps_ext->poc_lsb_not_present_flag[i] = READ_BIT(v);
        }
    }

    vps_ext->dpb_size = calloc(1, sizeof(struct dpb_size));
    parse_dpb_size(v, vps_ext->dpb_size, vps, NumOutputLayerSets, OlsIdxToLsIdx,
         MaxSubLayersInLayerSetMinus1, NecessaryLayerFlag);

    vps_ext->direct_dep_type_len_minus2 = GOL_UE(v);
    // uint8_t direct_dependency_all_layers_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        vps_ext->direct_dependency_all_layers_type = READ_BITS(v, vps_ext->direct_dep_type_len_minus2 + 2);
    } else {
        vps_ext->direct_dependency_type = calloc(MaxLayersMinus1+1, sizeof(uint32_t *));
        for (int i = vps->vps_base_layer_internal_flag ? 1 : 2; i <= MaxLayersMinus1; i++ ) {
            vps_ext->direct_dependency_type[i] = calloc(i, sizeof(uint32_t));
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
    vps_ext->vps_non_vui_extension_data_byte = calloc(vps_non_vui_extension_length, 1);
    for(uint32_t i = 0; i < vps_non_vui_extension_length; i++) {
        vps_ext->vps_non_vui_extension_data_byte[i] = READ_BITS(v, 8);
    }
    // uint8_t vps_vui_present_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        while(!BYTE_ALIGNED(v)) {
            SKIP_BITS(v, 1);
        }
        vps_ext->vps_vui = calloc(1, sizeof(struct vps_vui));
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
parse_vps(struct bits_vec *v)
{
    // VDBG(hevc, "vps: len %d", len);
    // hexdump(stdout, "vps: ", data, len);
    struct vps *vps = calloc(1, sizeof(struct vps));
    vps->vps_video_parameter_set_id = READ_BITS(v, 4);
    VDBG(hevc, "vps id %d", vps->vps_video_parameter_set_id);
    vps->vps_base_layer_internal_flag = READ_BIT(v);
    vps->vps_base_layer_available_flag = READ_BIT(v);
    vps->vps_max_layers_minus1 = READ_BITS(v, 6);
    VDBG(hevc, "vps_max_layers_minus1 %d", vps->vps_max_layers_minus1);
    vps->vps_max_sub_layers_minus1 = READ_BITS(v, 3);
    vps->vps_temporal_id_nesting_flag = READ_BIT(v);
    VDBG(hevc, "vps_max_sub_layers_minus1 %d", vps->vps_max_sub_layers_minus1);
    VDBG(hevc, "vps_temporal_id_nesting_flag %d", vps->vps_temporal_id_nesting_flag);

    // uint16_t vps_reserved_0xffff_16bits = READ_BITS(v, 16);
    assert(READ_BITS(v, 16) == 0xFFFF);

    parse_profile_tier_level(v, &vps->vps_profile_tier_level, 1, vps->vps_max_sub_layers_minus1);

    vps->vps_sub_layer_ordering_info_present_flag = READ_BIT(v);
    VDBG(hevc, "vps_sub_layer_ordering_info_present_flag %d", vps->vps_sub_layer_ordering_info_present_flag);

    for (int i = vps->vps_sub_layer_ordering_info_present_flag ? 0: vps->vps_max_sub_layers_minus1;
            i <= vps->vps_max_sub_layers_minus1; i ++) {
        vps->vps_sublayers[i].vps_max_dec_pic_buffering_minus1 = GOL_UE(v);
        vps->vps_sublayers[i].vps_max_num_reorder_pics = GOL_UE(v);
        vps->vps_sublayers[i].vps_max_latency_increase_plus1 = GOL_UE(v);
    }

    vps->vps_max_layer_id = READ_BITS(v, 6);
    vps->vps_num_layer_sets_minus1 = GOL_UE(v);

    VDBG(hevc, "vps_max_layer_id %d", vps->vps_max_layer_id);
    VDBG(hevc, "vps_num_layer_sets_minus1 %d", vps->vps_num_layer_sets_minus1);

    for (uint32_t i = 1; i <= vps->vps_num_layer_sets_minus1; i ++) {
        int n = 0;
        for (uint32_t j = 0; j <= vps->vps_max_layer_id; j ++) {
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
        vps->vps_timing_info = calloc(1, sizeof(struct vps_timing_info));
        parse_vps_timing_info(v, vps, vps->vps_timing_info);
    }
    // vps->vps_extension_flag = READ_BIT(v);
    if (READ_BIT(v)) {
        while (!BYTE_ALIGNED(v)) {
            SKIP_BITS(v, 1);
        }
        vps->vps_ext = calloc(1, sizeof(struct vps_extension));
        parse_vps_extension(v, vps, vps->vps_ext);
        /* 3d extension flag */
        if (READ_BIT(v)) {
            while (!BYTE_ALIGNED(v)) {
                SKIP_BITS(v, 1);
            }
            vps->vps_3d_ext = calloc(1, sizeof(struct vps_3d_extension));
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
    return vps;
}

static struct sei *
parse_sei(struct bits_vec *v)
{
    VDBG(hevc, "SEI");
    struct sei * sei = calloc(1, sizeof(struct sei));
    SKIP_BITS(v, v->len * 8);
    // do {
    //     sei->num ++;
    //     if (sei->msg == NULL)
    //         sei->msg = calloc(1, sizeof(struct sei_msg));
    //     else
    //         sei->msg = realloc(sei->msg, sizeof(struct sei_msg) * sei->num);
    //     struct sei_msg *m = &sei->msg[sei->num - 1];
    //     switch (m->last_paylod_type) {
    //         case 0:
    //         break;
    //         default:
    //         break;
    //     }
    // } while(1);
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
        slice->list_entry_l0 = calloc((slice->num_ref_idx_l0_active_minus1 +1)* 4, 1);
        for (uint32_t i = 0; i <= slice->num_ref_idx_l0_active_minus1; i++ ) {
            slice->list_entry_l0[i] = READ_BITS(v, log2ceil(NumPicTotalCurr));
        }
    }
    if (slice->slice_type == SLICE_TYPE_B) {
        slice->ref_pic_list_modification_flag_l1 = READ_BIT(v);
        if (slice->ref_pic_list_modification_flag_l1) {
            slice->list_entry_l1 = calloc((slice->num_ref_idx_l1_active_minus1+1) *4, 1);
            for (uint32_t i = 0; i <= slice->num_ref_idx_l1_active_minus1; i++ ) {
                slice->list_entry_l1[i] = READ_BITS(v, log2ceil(NumPicTotalCurr));
            }
        }
    }

}


static void
calc_sps_param_to_slice(struct sps *sps, struct slice_segment_header *slice)
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
    VDBG(hevc, "log2_min_luma_coding_block_size_minus3 %d", sps->log2_min_luma_coding_block_size_minus3);
    VDBG(hevc, "log2_diff_max_min_luma_coding_block_size %d", sps->log2_diff_max_min_luma_coding_block_size);
    
    slice->MinCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus3 + 3;
    //see 7-11
    slice->CtbLog2SizeY = slice->MinCbLog2SizeY + sps->log2_diff_max_min_luma_coding_block_size;
    //see 7-12
    slice->MinCbSizeY = 1 << slice->MinCbLog2SizeY;
    //see 7-13
    slice->CtbSizeY = 1 << slice->CtbLog2SizeY;
    VDBG(hevc, "CtbSizeY %d", slice->CtbSizeY);
    //see 7-14
    slice->PicWidthInMinCbsY = sps->pic_width_in_luma_samples / slice->MinCbSizeY;
    //see 7-15
    slice->PicWidthInCtbsY = divceil(sps->pic_width_in_luma_samples, slice->CtbSizeY);

    VDBG(hevc, "pic_height_in_luma_samples %d", sps->pic_height_in_luma_samples);
    //see 7-16
    slice->PicHeightInMinCbsY = sps->pic_height_in_luma_samples / slice->MinCbSizeY;
    //see 7-17
    slice->PicHeightInCtbsY = divceil(sps->pic_height_in_luma_samples, slice->CtbSizeY);
    //see 7-18
    slice->PicSizeInMinCbsY = slice->PicWidthInMinCbsY * slice->PicHeightInMinCbsY;
    //see 7-19
    slice->PicSizeInCtbsY = slice->PicWidthInCtbsY * slice->PicHeightInCtbsY;
    VDBG(hevc, "PicWidthInCtbsY %d, PicHeightInCtbsY %d, PicSizeInCtbsY %d", slice->PicWidthInCtbsY, slice->PicHeightInCtbsY, slice->PicSizeInCtbsY);
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

/*see 6.5.3 (6-11)*/
static scanpos *init_up_right_scan_order(int blkSize) {
    int i = 0;
    int x = 0, y = 0;
    bool stopLoop = false;
    scanpos *diagScan = malloc(sizeof(scanpos) * blkSize * blkSize);

    while (!stopLoop) {
        while (y >= 0) {
            if (x < blkSize && y < blkSize) {
                diagScan[i].x = x;
                diagScan[i].y = y;
                i++;
            }
            y--;
            x++;
        }
        y = x;
        x = 0;
        if (i >= blkSize * blkSize) {
            stopLoop = true;
        }
    }
    return diagScan;
}

/*see 6.5.4 (6-12)*/
static scanpos *init_horizontal_scan_order(int blkSize) {
    int i = 0;
    scanpos *horScan = malloc(sizeof(scanpos) * blkSize * blkSize);

    for (int y = 0; y < blkSize; y++) {
        for (int x = 0; x < blkSize; x++) {
            horScan[i].x = x;
            horScan[i].y = y;
            i++;
        }
    }
    return horScan;
}

/*see 6.5.5 (6-13)*/
static scanpos *init_vertical_scan_order(int blkSize) {
    int i = 0;
    scanpos *verScan = malloc(sizeof(scanpos) * blkSize * blkSize);

    for (int x = 0; x < blkSize; x++) {
        for (int y = 0; y < blkSize; y++) {
            verScan[i].x = x;
            verScan[i].y = y;
            i++;
        }
    }
    return verScan;
}

/*see 6.5.6 (6-14)*/
static scanpos *init_traverse_scan_order(int blkSize) {
    int i = 0;
    scanpos *travScan = malloc(sizeof(scanpos) * blkSize * blkSize);

    for (int y = 0; y < blkSize; y++) {
        if (y % 2 == 0) {
            for (int x = 0; x < blkSize; x++) {
                travScan[i].x = x;
                travScan[i].y = y;
                i++;
            }
        } else {
            for (int x = blkSize - 1; x >= 0; x--) {
                travScan[i].x = x;
                travScan[i].y = y;
                i++;
            }
        }
    }
    return travScan;
}

//see I.7.3.6.1
static struct slice_segment_header *
parse_slice_segment_header(struct bits_vec *v, struct hevc_nalu_header *headr,
        struct hevc_param_set * hps, uint32_t *SliceAddrRs)
{
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;
    struct slice_segment_header *slice = calloc(1, sizeof(*slice));

    VINFO(hevc, "parse_slice_segment_header");
    //init ScanOrder table
    for (int log2blocksize = 0; log2blocksize < 6; log2blocksize++) {
        if (log2blocksize >= 0 && log2blocksize < 6) {
            if (log2blocksize < 4) {
                slice->ScanOrder[log2blocksize][0] =
                    init_up_right_scan_order(1 << log2blocksize);
                slice->ScanOrder[log2blocksize][1] =
                    init_horizontal_scan_order(1 << log2blocksize);
                slice->ScanOrder[log2blocksize][2] =
                    init_vertical_scan_order(1 << log2blocksize);
            }
            if (log2blocksize >= 2) {
                slice->ScanOrder[log2blocksize][3] =
                    init_traverse_scan_order(1 << log2blocksize);
            }
        }
    }

    // the first VCL NAL unit of the coded picture shall have first_slice_segment_in_pic_flag = 1 
    uint8_t first_slice_segment_in_pic_flag = READ_BIT(v);
    if (headr->nal_unit_type >= BLA_W_LP && headr->nal_unit_type <= RSV_IRAP_VCL23) {
        slice->no_output_of_prior_pics_flag = READ_BIT(v);
    }
    slice->slice_pic_parameter_set_id = GOL_UE(v);
    VDBG(hevc, "first_slice_segment_in_pic_flag %d", first_slice_segment_in_pic_flag);
    VDBG(hevc, "slice_pic_parameter_set_id %d", slice->slice_pic_parameter_set_id);
    VDBG(hevc, "no_output_of_prior_pics_flag %d", slice->no_output_of_prior_pics_flag);

    calc_sps_param_to_slice(sps, slice);
    //see 7-36
    slice->Log2MinCuQpDeltaSize = slice->CtbLog2SizeY - pps->diff_cu_qp_delta_depth;
    //see 7-39
    slice->Log2MinCuChromaQpOffsetSize = slice->CtbLog2SizeY - pps->pps_range_ext.diff_cu_chroma_qp_offset_depth;
    VDBG(hevc, "Log2MinCuQpDeltaSize %d, Log2MinCuChromaQpOffsetSize %d",
         slice->Log2MinCuQpDeltaSize, slice->Log2MinCuChromaQpOffsetSize);

    slice->dependent_slice_segment_flag = 0;
    if (!first_slice_segment_in_pic_flag) {
        if (pps->dependent_slice_segments_enabled_flag) {
            slice->dependent_slice_segment_flag = READ_BIT(v);
        }
        slice->slice_segment_address = READ_BITS(v, log2ceil(slice->PicSizeInCtbsY));
    } else {
        slice->slice_segment_address = 0;
    }
    VDBG(hevc, "PicSizeInCtbsY %d, bits %d, slice_segment_address %d", slice->PicSizeInCtbsY, log2ceil(slice->PicSizeInCtbsY), slice->slice_segment_address);

    assert(slice->slice_segment_address < (uint32_t)slice->PicSizeInCtbsY);
    slice->CuQpDeltaVal = 0;
    VDBG(hevc,
         "dependent_slice_segments_enabled_flag %d, "
         "dependent_slice_segment_flag %d",
         pps->dependent_slice_segments_enabled_flag,
         slice->dependent_slice_segment_flag);
    if (slice->dependent_slice_segment_flag) {
        *SliceAddrRs = pps->CtbAddrTsToRs[pps->CtbAddrRsToTs[slice->slice_segment_address] - 1];
    }
    if (!slice->dependent_slice_segment_flag) {
        //see 7.4.7.1 dependent_slice_segment_flag
        *SliceAddrRs = slice->slice_segment_address;
        //FIXME
        // //see I.7.3.6.1
        // int i = 0;
        // VDBG(hevc, "num_extra_slice_header_bits %d", pps->num_extra_slice_header_bits);
        // if (pps->num_extra_slice_header_bits > i) {
        //     i ++;
        //     uint8_t discardable_flag = READ_BIT(v);
        // }
        // if (pps->num_extra_slice_header_bits > i) {
        //     i ++;
        //     uint8_t cross_layer_bla_flag = READ_BIT(v);
        // }
        // for (int i = 0; i < pps->num_extra_slice_header_bits; i++ ){
        //     // slice_reserved_flag[ i ] = READ_BIT(v);
        //     SKIP_BITS(v, 1);
        // }
        if (pps->num_extra_slice_header_bits) {
            SKIP_BITS(v, pps->num_extra_slice_header_bits);
        }

        slice->slice_type = GOL_UE(v);
        VDBG(hevc, "slice_type %d, nal_type %d", slice->slice_type, headr->nal_unit_type);

        //make an assumation here, cause we have pic only
        assert(slice->slice_type == SLICE_TYPE_I);
        if (pps->output_flag_present_flag) {
            slice->pic_output_flag = READ_BIT(v);
        } else {
            slice->pic_output_flag = 1;
        }
        if (sps->separate_colour_plane_flag == 1) {
            slice->colour_plane_id = READ_BITS(v, 2);
            // 0, 1, 2 refers to Y, Cb, Cr
            VDBG(hevc, "color_plane_id %d", slice->colour_plane_id);
        }
        if ((vps->vps_ext && headr->nuh_layer_id > 0 && !vps->vps_ext->poc_lsb_not_present_flag[vps->LayerIdxInVps[headr->nuh_layer_id]]) ||
            (headr->nal_unit_type!= IDR_W_RADL && headr->nal_unit_type != IDR_N_LP)) {
            slice->slice_pic_order_cnt_lsb = READ_BITS(v, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
            VDBG(hevc, "slice_pic_order_cnt_lsb %d\n", slice->slice_pic_order_cnt_lsb);
        }

        if (headr->nal_unit_type != IDR_W_RADL && headr->nal_unit_type != IDR_N_LP) {
            slice->short_term_ref_pic_set_sps_flag = READ_BIT(v);
            if (!slice->short_term_ref_pic_set_sps_flag) {
                slice->st = calloc(1, sizeof(struct st_ref_pic_set));
                parse_st_ref_set(v, slice->st, sps->num_short_term_ref_pic_sets, sps->num_short_term_ref_pic_sets);
            } else if (sps->num_short_term_ref_pic_sets > 1) {
                slice->short_term_ref_pic_set_idx = READ_BITS(v, log2ceil(sps->num_short_term_ref_pic_sets));
            }
            /* see 7.4.7.1 short_term_ref_pic_set_idx */
            if (slice->short_term_ref_pic_set_sps_flag) {
                slice->CurrRpsIdx = slice->short_term_ref_pic_set_idx;
            } else {
                slice->CurrRpsIdx = sps->num_short_term_ref_pic_sets;
            }
            VDBG(hevc, "long_term_ref_pics_present_flag %d", sps->long_term_ref_pics_present_flag);
            if (sps->long_term_ref_pics_present_flag) {
                if (sps->num_long_term_ref_pics_sps > 0) {
                    slice->num_long_term_sps = GOL_UE(v);
                }
                slice->num_long_term_pics = GOL_UE(v);
                slice->PocLsbLt = calloc(MAX(sps->num_long_term_ref_pics_sps, slice->num_long_term_sps + slice->num_long_term_pics), 1);
                slice->UsedByCurrPicLt = calloc(MAX(sps->num_long_term_ref_pics_sps, slice->num_long_term_sps + slice->num_long_term_pics), 1);
                slice->terms = calloc((slice->num_long_term_sps + slice->num_long_term_pics), sizeof(struct slice_long_term));
                slice->DeltaPocMsbCycleLt = calloc(slice->num_long_term_sps, sizeof(int));
                VDBG(hevc, "num_long_term_sps %d, num_long_term_pics %d", slice->num_long_term_sps, slice->num_long_term_pics);
                for (uint32_t i = 0; i < slice->num_long_term_sps + slice->num_long_term_pics; i ++) {
                    if (i < slice->num_long_term_sps) {
                        if (sps->num_long_term_ref_pics_sps > 1) {
                            slice->terms[i].lt_idx_sps = READ_BITS(v, log2ceil(sps->num_long_term_ref_pics_sps));
                        }
                        slice->PocLsbLt[i] = sps->sps_lt_ref->lt_ref_pic_poc_lsb_sps[slice->terms[i].lt_idx_sps];
                        slice->UsedByCurrPicLt[i] = sps->sps_lt_ref->used_by_curr_pic_lt_sps_flag[slice->terms[i].lt_idx_sps];
                    } else {
                        slice->terms[i].poc_lsb_lt = READ_BITS(v, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
                        slice->terms[i].used_by_curr_pic_lt_flag = READ_BIT(v);
                        slice->PocLsbLt[i] = slice->terms[i].poc_lsb_lt;
                        slice->UsedByCurrPicLt[i] = slice->terms[i].used_by_curr_pic_lt_flag;
                    }
                    slice->terms[i].delta_poc_msb_present_flag = READ_BIT(v);
                    if (slice->terms[i].delta_poc_msb_present_flag) {
                        slice->terms[i].delta_poc_msb_cycle_lt = GOL_UE(v);
                    }
                    /* see (7-52) */
                    if (i == 0 || i == slice->num_long_term_sps) {
                        slice->DeltaPocMsbCycleLt[i] = slice->terms[i].delta_poc_msb_cycle_lt;
                    } else {
                        slice->DeltaPocMsbCycleLt[i] = slice->terms[i].delta_poc_msb_cycle_lt+ 
                                slice->DeltaPocMsbCycleLt[i - 1];
                    }
                }
            }
            if (sps->sps_temporal_mvp_enabled_flag) {
                slice->slice_temporal_mvp_enabled_flag = READ_BIT(v);
            } else {
                slice->slice_temporal_mvp_enabled_flag = 0;
            }
        }

        struct st_ref_pic_set *st;
        if (!slice->short_term_ref_pic_set_sps_flag) {
            slice->st = calloc(1, sizeof(struct st_ref_pic_set));
            st = slice->st;
        } else {
            st = sps->sps_st_ref + slice->short_term_ref_pic_set_idx;
        }
        int stRpsIdx = slice->CurrRpsIdx;
        /*see (7-59)*/
        int RefRpsIdx = stRpsIdx - (st->delta_idx_minus1 + 1);
        int deltaRps = (1 - 2 * st->delta_rps_sign) *(st->abs_delta_rps_minus1 + 1);
        if (st->inter_ref_pic_set_prediction_flag == 1) {
            /* (7-61)*/
            int i = 0;
            for (int j = slice->NumPositivePics[ RefRpsIdx ] - 1; j >= 0; j--) {
                int dPoc = slice->DeltaPocS1[ RefRpsIdx ][ j ] + deltaRps;
                if (dPoc < 0 && st->ref_used[slice->NumNegativePics[RefRpsIdx]+j].use_delta_flag) {
                    slice->DeltaPocS0[stRpsIdx][i] = dPoc;
                    slice->UsedByCurrPicS0[stRpsIdx][i++] = st->ref_used[slice->NumNegativePics[RefRpsIdx] + j].used_by_curr_pic_flag;
                }
            }
            if (deltaRps < 0 && st->ref_used[slice->NumDeltaPocs[RefRpsIdx]].use_delta_flag) {
                slice->DeltaPocS0[stRpsIdx][i] = deltaRps;
                slice->UsedByCurrPicS0[ stRpsIdx ][ i++ ] = st->ref_used[slice->NumDeltaPocs[ RefRpsIdx]].used_by_curr_pic_flag;
            }
            for (int j = 0; j < slice->NumNegativePics[ RefRpsIdx ]; j++ ) {
                int dPoc = slice->DeltaPocS0[ RefRpsIdx ][ j ] + deltaRps;
                if (dPoc < 0 && st->ref_used[j].use_delta_flag) {
                    slice->DeltaPocS0[ stRpsIdx ][ i ] = dPoc;
                    slice->UsedByCurrPicS0[ stRpsIdx ][ i++ ] = st->ref_used[j].used_by_curr_pic_flag;
                }
            }
            slice->NumNegativePics[stRpsIdx] = i;
            /*(7-62)*/
            i = 0;
            for (int j = slice->NumNegativePics[RefRpsIdx]-1; j >= 0; j--) {
                int dPoc = slice->DeltaPocS0[RefRpsIdx][j] + deltaRps;
                if (dPoc > 0 && st->ref_used[j].use_delta_flag) {
                    slice->DeltaPocS1[stRpsIdx][i] = dPoc;
                    slice->UsedByCurrPicS1[stRpsIdx][i++] = st->ref_used[j].used_by_curr_pic_flag;
                }
            }
            if (deltaRps > 0 && st->ref_used[slice->NumDeltaPocs[RefRpsIdx]].use_delta_flag) {
                slice->DeltaPocS1[ stRpsIdx ][ i ] = deltaRps;
                slice->UsedByCurrPicS1[ stRpsIdx ][ i++ ] = st->ref_used[slice->NumDeltaPocs[RefRpsIdx]].used_by_curr_pic_flag;
            }
            for (int j = 0; j < slice->NumPositivePics[ RefRpsIdx ]; j++) {
                int dPoc = slice->DeltaPocS1[ RefRpsIdx ][ j ] + deltaRps;
                if (dPoc > 0 && st->ref_used[slice->NumNegativePics[RefRpsIdx] + j].use_delta_flag) {
                    slice->DeltaPocS1[stRpsIdx][i] = dPoc;
                    slice->UsedByCurrPicS1[stRpsIdx][i++] =
                        st->ref_used[slice->NumNegativePics[ RefRpsIdx ] + j ].used_by_curr_pic_flag;
                }
            }
            slice->NumPositivePics[ stRpsIdx ] = i;
        } else {
            /* (7-63) */
            slice->NumNegativePics[stRpsIdx] = st->num_negative_pics;
            /* (7-64) */
            slice->NumPositivePics[stRpsIdx] = st->num_positive_pics;
            /* (7-65)*/
            for (uint32_t i = 0; i < st->num_negative_pics; i ++) {
                slice->UsedByCurrPicS0[stRpsIdx][i] = st->used_by_curr_pic_s0_flag[i];
                slice->UsedByCurrPicS1[stRpsIdx][i] = st->used_by_curr_pic_s1_flag[i];
                if (i == 0) {
                    /*(7-67)*/
                    slice->DeltaPocS0[stRpsIdx][i] = -(st->delta_poc_s0_minus1[i] + 1);
                    slice->DeltaPocS1[stRpsIdx][i] = st->delta_poc_s1_minus1[i] + 1; 
                } else {
                    slice->DeltaPocS0[stRpsIdx][i] = slice->DeltaPocS0[stRpsIdx][i - 1] - (st->delta_poc_s0_minus1[i] + 1);
                    slice->DeltaPocS1[stRpsIdx][i] = slice->DeltaPocS1[stRpsIdx][i - 1] + (st->delta_poc_s1_minus1[i] + 1);
                }
            }
            
        }
        /* (7-71) */
        slice->NumDeltaPocs[stRpsIdx] = slice->NumNegativePics[stRpsIdx] + slice->NumPositivePics[stRpsIdx];

        //see (7-57)
        for (int i = 0; i < slice->NumNegativePics[slice->CurrRpsIdx]; i++ )
            if (sps->sps_st_ref[slice->CurrRpsIdx].used_by_curr_pic_s0_flag[i]) {//TODO
                slice->NumPicTotalCurr ++;
            }
        for (int i = 0; i < slice->NumPositivePics[slice->CurrRpsIdx]; i++) {
            if (sps->sps_st_ref[slice->CurrRpsIdx].used_by_curr_pic_s1_flag[i]) {
                slice->NumPicTotalCurr ++;
            }
        }
        for (uint32_t i = 0; i < slice->num_long_term_sps + slice->num_long_term_pics; i++) {
            if(slice->UsedByCurrPicLt[i]) {
                slice->NumPicTotalCurr++;
            }
        }
        if (pps->pps_scc_extension_flag && pps->pps_scc_ext.pps_curr_pic_ref_enabled_flag) {
            slice->NumPicTotalCurr++;
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
                        int TemporalId = headr->nuh_temporal_id;
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
                        slice->inter_layer_pred_layer_idc = calloc(slice->NumActiveRefLayerPics, sizeof(uint32_t));
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
        VDBG(hevc, "inCmpPredAvailFlag %d", inCmpPredAvailFlag);
        if (inCmpPredAvailFlag) {
            slice->in_comp_pred_flag = READ_BIT(v);
        }
        slice->ChromaArrayType = (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc);
        slice->slice_sao_luma_flag = 0;
        slice->slice_sao_chroma_flag = 0;

        VDBG(hevc, "sample_adaptive_offset_enabled_flag %d", sps->sample_adaptive_offset_enabled_flag);
        VDBG(hevc, "ChromaArrayType %d", slice->ChromaArrayType);
        
        if (sps->sample_adaptive_offset_enabled_flag) {
            slice->slice_sao_luma_flag = READ_BIT(v);
            if (slice->ChromaArrayType != 0) {
                slice->slice_sao_chroma_flag = READ_BIT(v);
            } else {
                slice->slice_sao_chroma_flag = 0;
            }
        } else {
            slice->slice_sao_luma_flag = 0;
        }
        VDBG(hevc, "slice_sao_luma_flag %d", slice->slice_sao_luma_flag);
        VDBG(hevc, "slice_sao_chroma_flag %d", slice->slice_sao_chroma_flag);
        if (slice->slice_type == SLICE_TYPE_P || slice->slice_type == SLICE_TYPE_B) {
            uint8_t num_ref_idx_active_override_flag = READ_BIT(v);
            if (num_ref_idx_active_override_flag) {
                slice->num_ref_idx_l0_active_minus1 = GOL_UE(v);
                if (slice->slice_type == SLICE_TYPE_B) {
                    slice->num_ref_idx_l1_active_minus1 = GOL_UE(v);
                } else {
                    slice->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
                }
            } else {
                slice->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
                slice->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
            }

            VDBG(hevc, "lists_modification_present_flag %d", pps->lists_modification_present_flag);
            VDBG(hevc, "NumPicTotalCurr %d", slice->NumPicTotalCurr);
            if (pps->lists_modification_present_flag && slice->NumPicTotalCurr > 1 ) {
                ref_pic_lists_modification(v, slice, slice->NumPicTotalCurr);
            }
            if (slice->slice_type == SLICE_TYPE_B) {
                slice->mvd_l1_zero_flag = READ_BIT(v);
            }
            if (pps->cabac_init_present_flag) {
                slice->cabac_init_flag = READ_BIT(v);
            }
            VDBG(hevc, "mvd_l1_zero_flag %d", slice->mvd_l1_zero_flag);
            VDBG(hevc, "cabac_init_present_flag %d, cabac_init_flag %d", pps->cabac_init_present_flag, slice->cabac_init_flag);

            VDBG(hevc, "slice_temporal_mvp_enabled_flag %d", slice->slice_temporal_mvp_enabled_flag);
            if (slice->slice_temporal_mvp_enabled_flag) {
                uint8_t collocated_from_l0_flag = 0;
                if (slice->slice_type == SLICE_TYPE_B) {
                    collocated_from_l0_flag = READ_BIT(v);
                } else {
                    collocated_from_l0_flag = 1;
                }
                if ((collocated_from_l0_flag && slice->num_ref_idx_l0_active_minus1 > 0) ||
                    (!collocated_from_l0_flag && slice->num_ref_idx_l1_active_minus1 > 0)) {
                    slice->collocated_ref_idx = GOL_UE(v);
                }
            }

            VDBG(hevc, "weighted_pred_flag %d", pps->weighted_pred_flag);
            VDBG(hevc, "weighted_bipred_flag %d", pps->weighted_bipred_flag);
            VDBG(hevc, "NumRefListLayers %d", vps->NumRefListLayers[headr->nuh_layer_id]);
            if ((pps->weighted_pred_flag && slice->slice_type == SLICE_TYPE_P) ||
                (pps->weighted_bipred_flag && slice->slice_type == SLICE_TYPE_B)) {
                    printf("need pred_weight_table\n");
                    assert(0);
                // parse_pred_weight_table();
            } else if (!DepthFlag && vps->NumRefListLayers[headr->nuh_layer_id] > 0) {
                slice->slice_ic_enabled_flag = READ_BIT(v);
                if (slice->slice_ic_enabled_flag) {
                    slice->slice_ic_disabled_merge_zero_idx_flag = READ_BIT(v);
                }
            }
            slice->five_minus_max_num_merge_cand = GOL_UE(v);
            VDBG(hevc, "five_minus_max_num_merge_cand %d", slice->five_minus_max_num_merge_cand);

            if (sps->sps_scc_ext.motion_vector_resolution_control_idc == 2) {
                slice->use_integer_mv_flag = READ_BIT(v);
            }
        }

        slice->slice_qp_delta = GOL_SE(v);
        VDBG(hevc, "slice_qp_delta %d", slice->slice_qp_delta);

        if (pps->pps_slice_chroma_qp_offsets_present_flag) {
            slice->slice_cb_qp_offset = GOL_SE(v);
            slice->slice_cr_qp_offset = GOL_SE(v);
            VDBG(hevc, "slice_cb_qp_offset %d", slice->slice_cb_qp_offset);
            VDBG(hevc, "slice_cr_qp_offset %d", slice->slice_cr_qp_offset);
        } else {
            slice->slice_cb_qp_offset = 0;
            slice->slice_cr_qp_offset = 0;
        }
        if (pps->pps_scc_ext.pps_slice_act_qp_offsets_present_flag) {
            slice->slice_act_y_qp_offset = GOL_SE(v);
            slice->slice_act_cb_qp_offset = GOL_SE(v);
            slice->slice_act_cr_qp_offset = GOL_SE(v);
        } else {
            slice->slice_act_y_qp_offset = 0;
            slice->slice_act_cb_qp_offset = 0;
            slice->slice_act_cr_qp_offset = 0;
        }

        if (pps->pps_range_ext.chroma_qp_offset_list_enabled_flag) {
            slice->cu_chroma_qp_offset_enabled_flag = READ_BIT(v);
        } else {
            slice->cu_chroma_qp_offset_enabled_flag = 0;
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
        } else {
            slice_deblocking_filter_disabled_flag = pps->pps_deblocking_filter_disabled_flag;
        }
        VDBG(hevc, "slice_deblocking_filter_disabled_flag %d", slice_deblocking_filter_disabled_flag);
        if (pps->pps_loop_filter_across_slices_enabled_flag &&
                (slice->slice_sao_luma_flag || slice->slice_sao_chroma_flag ||
                !slice_deblocking_filter_disabled_flag )) {
            slice->slice_loop_filter_across_slices_enabled_flag = READ_BIT(v);
        } else {
            slice->slice_loop_filter_across_slices_enabled_flag = pps->pps_loop_filter_across_slices_enabled_flag;
        }
        // VDBG(hevc, "cp_in_slice_segment_header_flag[ViewIdx] %d", vps->vps_3d_ext->cp[vps->ViewOrderIdx[headr->nuh_layer_id]].cp_in_slice_segment_header_flag);
        // if (cp_in_slice_segment_header_flag[ViewIdx]) {
        //     for (int m = 0; m < num_cp[ViewIdx][m]; m++) {
        //         j = cp_ref_voi[ViewIdx][m];
        //         cp_scale[j] = GOL_SE(v);
        //         cp_off[j] = GOL_SE(v);
        //         cp_inv_scale_plus_scale[j] = GOL_SE(v);
        //         cp_inv_off_plus_off[j] = GOL_SE(v);
        //     }
        // }
    }

    if (pps->tiles_enabled_flag || pps->entropy_coding_sync_enabled_flag) {
        slice->num_entry_point_offsets = GOL_UE(v);
        VDBG(hevc, "num_entry_point_offsets %d",
             slice->num_entry_point_offsets);
        if (slice->num_entry_point_offsets > 0) {
            uint32_t offset_len_minus1 = GOL_UE(v);
            VDBG(hevc, "offset_len_minus1 %d", offset_len_minus1);
            slice->entry_point_offset = calloc(slice->num_entry_point_offsets, sizeof(uint32_t));
            for (uint32_t i = 0; i < slice->num_entry_point_offsets; i++) {
                // see 7-55, 7-56, here entry_point_offset is firstByte for i > 0
                slice->entry_point_offset[i] = READ_BITS(v, offset_len_minus1 + 1) + 1;
                if (i > 0) {
                    slice->entry_point_offset[i] +=
                        slice->entry_point_offset[i - 1];
                }
                VDBG(hevc, "entry_point_offset %d",
                     slice->entry_point_offset[i]);
            }
        }
    } else {
        slice->num_entry_point_offsets = 0;
    }
    VDBG(hevc, "slice_segment_header_extension_present_flag %d", pps->slice_segment_header_extension_present_flag);
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

    return slice;
}
#ifdef ENABLE_3D
static void parse_intra_mode_ext(cabac_dec *d,
                                 struct hevc_slice *hslice, struct cu *cu,
                                 struct hevc_param_set *hps, int x0, int y0,
                                 int log2PbSize) {
    struct intra_mode_ext *ext = &cu->intra_ext[x0][y0];
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct hevc_nalu_header *h = hslice->nalu;
    struct slice_segment_header *slice = hslice->slice;

    if (log2PbSize < 6) {
        ext->no_dim_flag = CABAC(d, CTX_TYPE_3D_NO_DIM_FLAG);
    } else {
        //see I.7.4.9.5.1, when not present, should be 1
        ext->no_dim_flag = 1;
    }
    //see I.6.6 derivation process for a wedgelet partition pattern table
    //TODO
    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[h->nuh_layer_id];
    //see I-25
    int IntraContourEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_contour_enabled_flag && slice->in_comp_pred_flag;
    //see I-26
    int IntraDcOnlyWedgeEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_dc_only_wedge_enabled_flag;
    if (!ext->no_dim_flag && IntraDcOnlyWedgeEnabledFlag && IntraContourEnabledFlag) {
        ext->depth_intra_mode_idx_flag =
            CABAC(d, CTX_TYPE_3D_DEPTH_INTRA_MODE_IDX_FLAG);
    }
    if (!ext->no_dim_flag && !ext->depth_intra_mode_idx_flag) {
        ext->wedge_full_tab_idx = CABAC_FL(d, 1);
    }
}
static void parse_cu_extension(cabac_dec *d, struct hevc_slice *hslice,
                               struct cu *cu, struct hevc_param_set *hps,
                               int x0, int y0, int log2CbSize,
                               int NumPicTotalCurr) {
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    // struct pps *pps = hps->pps;

    struct cu_extension *ext = &cu->ext[x0][y0];

    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[headr->nuh_layer_id];
    int ViewIdx = vps->ViewOrderIdx[headr->nuh_layer_id];
    //see (I-23)
    int DbbpEnabledFlag = sps->sps_3d_ext[DepthFlag].dbbp_enabled_flag && slice->in_comp_pred_flag;

    int picx = 0;
    int PicOrderCntVal = PicOrderCnt(hslice, sps, picx, (headr->nal_unit_type >= BLA_W_LP && headr->nal_unit_type <= RSV_IRAP_VCL23));
    
    //see I-29
    int InterDcOnlyEnabledFlag = sps->sps_3d_ext[DepthFlag].inter_dc_only_enabled_flag;
    //see I-21
    int IvResPredEnabledFlag = vps->NumRefListLayers[headr->nuh_layer_id] > 0 && sps->sps_3d_ext[DepthFlag].iv_res_pred_enabled_flag;
    //see 
    int IntraDcOnlyWedgeEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_dc_only_wedge_enabled_flag;

    int DispAvailFlag = 1;//TODO

   
    if (cu->skip_intra_flag[x0][y0]) {
        ext->skip_intra_mode_idx = CABAC(d, CTX_TYPE_3D_SKIP_INTRA_MODE_IDX);
    } else {
        if (!(cu->CuPredMode == MODE_SKIP)) {
            if (DbbpEnabledFlag && DispAvailFlag && log2CbSize > 3 &&
                (cu->PartMode == PART_2NxN || cu->PartMode == PART_Nx2N)) {
                ext->dbbp_flag = CABAC(d, CTX_TYPE_3D_DBBP_FLAG);
            }
            if ((cu->CuPredMode == MODE_INTRA ? IntraDcOnlyWedgeEnabledFlag :
                InterDcOnlyEnabledFlag) && cu->PartMode == PART_2Nx2N) {
                ext->dc_only_flag = CABAC(d, CTX_TYPE_3D_DC_ONLY_FLAG);
            }
        }
         /* see (I-48)(I-49) */
        if (cu->CuPredMode != MODE_INTRA) {
            struct rps *rps = hslice->rps;
            int icCuEnableFlag;
            if (cu->pu[x0][y0].merge_flag == 1) {
                icCuEnableFlag = ((cu->pu[x0][y0].merge_idx != 0) || !slice->slice_ic_disabled_merge_zero_idx_flag);
            } else {
                int refViewIdxL0 = ViewIdxfromvps(rps->RefPicList0[cu->pu[x0][y0].ref_idx_l0]);
                int refViewIdxL1 = ViewIdxfromvps(rps->RefPicList1[cu->pu[x0][y0].ref_idx_l1]);
                icCuEnableFlag = (cu->pu[x0][y0].inter_pred_idc != PRED_L0 && refViewIdxL1 != ViewIdx) ||
                    (cu->pu[x0][y0].inter_pred_idc != PRED_L1 && refViewIdxL0 != ViewIdx);
            }
            if (cu->PartMode == PART_2Nx2N) {
                if (IvResPredEnabledFlag && rps->RpRefPicAvailFlag ) {
                    ext->iv_res_pred_weight_idx =
                        CABAC(d, CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX);
                }
                if (slice->slice_ic_enabled_flag && icCuEnableFlag &&
                    ext->iv_res_pred_weight_idx == 0) {
                    ext->illu_comp_flag = CABAC(d, CTX_TYPE_3D_ILLU_COMP_FLAG);
                }
            }
        }
    }
}
/* I.7.3.8.5.3 */
static void
parse_depth_dcs(cabac_dec *d, struct cu *cu, int x0, int y0, int log2CbSize)
{
    int nCbS = (1 <<log2CbSize);
    int pbOffset = (cu->PartMode == PART_NxN && cu->CuPredMode ==MODE_INTRA) ? (nCbS /2) : nCbS;
    
    int DcOnlyFlag = cu->ext[x0][y0].dc_only_flag;
    int DimFlag = !cu->intra_ext[x0][y0].no_dim_flag;

    for (int j = 0; j < nCbS; j = j+ pbOffset) {
        for (int k = 0; k < nCbS; k = k + pbOffset) {
            if (DimFlag || DcOnlyFlag) {
                if (cu->CuPredMode == MODE_INTRA && DcOnlyFlag) {
                    cu->ext[x0 + k][y0 + j].depth_dc_present_flag =
                        CABAC(d, CTX_TYPE_3D_DEPTH_DC_PRESENT_FLAG);
                }
                int dcNumSeg = DimFlag ? 2: 1;
                if ( cu->ext[x0+k][y0+j].depth_dc_present_flag) {
                    for (int i = 0; i < dcNumSeg; i++ ) {
                        cu->ext[x0 + k][y0 + j].depth_dc_abs[i] =
                            CABAC(d, CTX_TYPE_3D_DEPTH_DC_ABS);
                        if ((cu->ext[x0+k][y0+j].depth_dc_abs[i] - dcNumSeg + 2) > 0) {
                           cu->ext[x0+k][y0+j]. depth_dc_sign_flag[i] = CABAC_BP(d);
                        }
                    }
                }
            }
        }
    }
}
#endif

//see 7.3.8.3 sample adaptive offset
static struct sao *parse_sao(cabac_dec *d,
                             struct sps *sps, struct slice_segment_header *slice,
                             uint32_t rx, uint32_t ry, int *CtbAddrRsToTs,
                             int CtbAddrInTs, int CtbAddrInRs,
                             int SliceAddrRs, int *TileId) {
    struct sao * sao = calloc(1, sizeof(*sao));

    uint8_t sao_merge_left_flag = 0;
    uint8_t sao_merge_up_flag = 0;
    uint8_t sao_eo_class_luma = 0;
    uint8_t sao_eo_class_chroma = 0;
    uint8_t sao_offset_abs[3][4];
    uint8_t sao_offset_sign[3][4];
    uint8_t sao_band_position[3];
    VDBG(hevc, "parsing sao (%d, %d):", rx, ry);
    if (rx > 0 ) {
        bool leftCtbInSliceSeg = (CtbAddrInRs > SliceAddrRs);
        bool leftCtbInTile = (TileId[CtbAddrInTs] == TileId[CtbAddrRsToTs[CtbAddrInRs - 1]]);
        if (leftCtbInSliceSeg && leftCtbInTile) {
            sao_merge_left_flag = CABAC(d, CTX_TYPE_SAO_MERGE);
            VDBG(hevc, "sao_merge_left_flag %d", sao_merge_up_flag);
        }
    }
    if (ry > 0 && !sao_merge_left_flag) {
        bool upCtbInSliceSeg = ((CtbAddrInRs - slice->PicWidthInCtbsY) >= SliceAddrRs);
        bool upCtbInTile = (TileId[CtbAddrInTs] ==
                TileId[CtbAddrRsToTs[CtbAddrInRs - slice->PicWidthInCtbsY]]);
        if (upCtbInSliceSeg && upCtbInTile) {
            sao_merge_up_flag = CABAC(d, CTX_TYPE_SAO_MERGE);
            VDBG(hevc, "sao_merge_up_flag %d", sao_merge_up_flag);
        }
    }
    if (!sao_merge_up_flag && !sao_merge_left_flag) {
        for (int cIdx = 0; cIdx < (slice->ChromaArrayType != 0 ? 3 : 1 ); cIdx++) {
            if ((slice->slice_sao_luma_flag && cIdx == 0) ||
                (slice->slice_sao_chroma_flag && cIdx > 0)) {
                if (cIdx == 0) {
                    uint8_t sao_type_idx_luma =
                        CABAC_TR(d, CTX_TYPE_SAO_TYPE_INDEX, 2, 0, NULL);
                    VDBG(hevc, "sao_type_idx_luma %d", sao_type_idx_luma);
                    sao->SaoTypeIdx[0][rx][ry] = sao_type_idx_luma;
                } else if (cIdx == 1) {
                    uint8_t sao_type_idx_chroma =
                        CABAC_TR(d, CTX_TYPE_SAO_TYPE_INDEX, 2, 0, NULL);
                    VDBG(hevc, "sao_type_idx_chroma %d", sao_type_idx_chroma);
                    sao->SaoTypeIdx[1][rx][ry] = sao_type_idx_chroma;
                    sao->SaoTypeIdx[2][rx][ry] = sao_type_idx_chroma;
                }
                VDBG(hevc, "cIdx %d, SaoTypeIdx %d", cIdx, sao->SaoTypeIdx[cIdx][rx][ry]);
                if (sao->SaoTypeIdx[cIdx][rx][ry] != 0 ) {
                    for (int i = 0; i < 4; i++) {
                        int bitdepth = ((i == 0) ? (sps->bit_depth_luma_minus8 + 8)
                                         : (sps->bit_depth_chroma_minus8 + 8));
                        int cMax = (1 << (MIN(bitdepth, 10) - 5)) - 1;
                        sao_offset_abs[cIdx][i] =
                            CABAC_TR(d, CTX_TYPE_ALL_BYPASS, cMax, 0, NULL);
                        VDBG(hevc, "cMax %d, sao_offset_abs[%d][%d][%d][%d] %d", cMax,
                             cIdx, rx, ry, i,
                             sao_offset_abs[cIdx][i]);
                    }
                    if (sao->SaoTypeIdx[cIdx][rx][ry] == 1) {
                        for (int i = 0; i < 4; i++ ) {
                            if (sao_offset_abs[cIdx][i] != 0) {
                                sao_offset_sign[cIdx][i] = CABAC_BP(d);
                            } else {
                                // if sao_offset_sign not present
                                // TODO: FIXME
                                if (sao_merge_left_flag) {
                                    // sao_offset_sign[cIdx][i] = sao_offset_sign[cIdx][rx-1][ry][i];
                                } else if (sao_merge_up_flag) {
                                    // sao_offset_sign[cIdx][i] = sao_offset_sign[cIdx][rx][ry-1][i];
                                } else if (sao->SaoTypeIdx[cIdx][rx][ry] == 2) {
                                    if (i == 0|| i == 1) {
                                        sao_offset_sign[cIdx][i] = 0;
                                    } {
                                        sao_offset_sign[cIdx][i] = 1;
                                    }
                                } else {
                                    sao_offset_sign[cIdx][i] = 0;
                                }
                            }
                            VDBG(hevc, "sao_offset_sign %d", sao_offset_sign[cIdx][i]);
                        }
                        sao_band_position[cIdx] = CABAC_FL(d, (1<<5)-1);
                        VDBG(hevc, "sao_band_position %d", sao_band_position[cIdx]);
                    } else {
                        if (cIdx == 0) {
                            sao_eo_class_luma = CABAC_FL(d, (1 << 2) -1);
                        } else if (cIdx == 1) {
                            sao_eo_class_chroma = CABAC_FL(d, (1 << 2) - 1);
                        }
                        VDBG(hevc,
                             "sao_eo_class_luma %d, sao_eo_class_chroma %d",
                             sao_eo_class_luma, sao_eo_class_chroma);
                        for (int i = 0; i < 4; i++ ) {
                            if (sao_merge_left_flag) {
                                // sao_offset_sign[cIdx][rx][ry][i] = sao_offset_sign[cIdx][rx-1][ry][i];
                            } else if (sao_merge_up_flag) {
                                // sao_offset_sign[cIdx][rx][ry][i] = sao_offset_sign[cIdx][rx][ry-1][i];
                            } else if (sao->SaoTypeIdx[cIdx][rx][ry] == 2) {
                                if (i == 0|| i == 1) {
                                    sao_offset_sign[cIdx][i] = 0;
                                } {
                                    sao_offset_sign[cIdx][i] = 1;
                                }
                            } else {
                                sao_offset_sign[cIdx][i] = 0;
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < 4; i++) {
                        if (sao_merge_left_flag) {
                            // sao_offset_abs[cIdx][i] = sao_offset_abs[cIdx][rx-1][ry][i];
                            // sao_offset_sign[cIdx][i] = sao_offset_sign[cIdx][rx-1][ry][i];
                        } else if (sao_merge_up_flag) {
                            // sao_offset_abs[cIdx][i] = sao_offset_abs[cIdx][rx][ry-1][i];
                            // sao_offset_sign[cIdx][i] = sao_offset_sign[cIdx][rx][ry-1][i];
                        } else {
                            sao_offset_abs[cIdx][i] = 0;
                            if (sao->SaoTypeIdx[cIdx][rx][ry] == 2) {
                                if (i == 0|| i == 1) {
                                    sao_offset_sign[cIdx][i] = 0;
                                } {
                                    sao_offset_sign[cIdx][i] = 1;
                                }
                            } else {
                                sao_offset_sign[cIdx][i] = 0;
                            }
                        }
                    }
                }
            } else {
                assert(0);
                //see Table 7-8
                if (sao_merge_left_flag) {
                    sao->SaoTypeIdx[cIdx][rx][ry] = sao->SaoTypeIdx[cIdx][rx - 1][ry];
                } else if (sao_merge_up_flag) {
                    sao->SaoTypeIdx[cIdx][rx][ry] = sao->SaoTypeIdx[cIdx][rx][ry - 1];
                } else {
                    sao->SaoTypeIdx[cIdx][rx][ry] = 0;
                }
                for (int i = 0; i < 4; i++) {
                    if (sao_merge_left_flag) {
                        // sao_offset_abs[cIdx][i] = sao_offset_abs[cIdx][rx-1][ry][i];
                        // sao_offset_sign[cIdx][i] = sao_offset_sign[cIdx][rx-1][ry][i];
                    } else if (sao_merge_up_flag) {
                        // sao_offset_abs[cIdx][i] = sao_offset_abs[cIdx][rx][ry-1][i];
                        // sao_offset_sign[cIdx][i] = sao_offset_sign[cIdx][rx][ry-1][i];
                    } else {
                        sao_offset_abs[cIdx][i] = 0;
                        if (sao->SaoTypeIdx[cIdx][rx][ry] == 2) {
                            if (i == 0|| i == 1) {
                                sao_offset_sign[cIdx][i] = 0;
                            } {
                                sao_offset_sign[cIdx][i] = 1;
                            }
                        } else {
                            sao_offset_sign[cIdx][i] = 0;
                        }
                    }
                }
            }
        }
    }
    return sao;
}

//see table 9-48
int qp_delta_abs_ctxInc_from_binIdx(int ctx_idx, int binIdx) {
    if (binIdx > 0) {
        return ctx_idx + 1;
    }
    return ctx_idx; 
}

static void parse_delta_qp(cabac_dec *d, struct slice_segment_header *slice,
                           struct pps *pps) {
    VDBG(hevc, "cu_qp_delta_enabled_flag %d, IsCuQpDeltaCoded %d",
         pps->cu_qp_delta_enabled_flag, slice->IsCuQpDeltaCoded);
    if (pps->cu_qp_delta_enabled_flag && !slice->IsCuQpDeltaCoded ) {
        slice->IsCuQpDeltaCoded = 1;
        // see 9.3.3.10
        uint32_t cu_qp_delta_abs =
            CABAC_TR(d, CTX_TYPE_DELTA_QP_CU_QP_DELTA_ABS, 5, 0,
                     qp_delta_abs_ctxInc_from_binIdx);
        if (cu_qp_delta_abs > 4) {
            cu_qp_delta_abs = cabac_dec_egk(d, 0, 32, 32) + 5;
        }
        VDBG(hevc, "cu_qp_delta_abs %d", cu_qp_delta_abs);
        uint8_t cu_qp_delta_sign_flag = 0;
        if (cu_qp_delta_abs) {
            cu_qp_delta_sign_flag =
                CABAC_BP(d);
            VDBG(hevc, "cu_qp_delta_sign_flag %d", cu_qp_delta_sign_flag);
        }
        slice->CuQpDeltaVal = cu_qp_delta_abs * (1 - 2 * cu_qp_delta_sign_flag);
    }
}

static int ctxInc_for_res_scale_sign_flag(int ctx_idx, int binIdx) { 
    return ctx_idx  + binIdx;
}
/*see 7.3.8.12 */
static void
parse_cross_comp_pred(cabac_dec *d, struct cross_comp_pred *cross, int x0, int y0, int c)
{
    VDBG(hevc,"cross_comp_pred");
    //FIXME
    int res_scale_sign_flag = 0;
    int log2_res_scale_abs_plus1 = CABAC_TR(d,
        CTX_TYPE_CROSS_COMP_LOG2_RES_SCALE_ABS+4*c,
        4, 0,
        ctxInc_for_res_scale_sign_flag);
    if (log2_res_scale_abs_plus1 != 0) {
        res_scale_sign_flag =
            CABAC(d, CTX_TYPE_CROSS_COMP_RES_SCALE_SIGN);
    }
    if (log2_res_scale_abs_plus1 == 0) {
        cross->ResScaleVal[c+1] = 0;
    } else {
        cross->ResScaleVal[c + 1] =
            (1 << (log2_res_scale_abs_plus1 - 1)) *
            (1 - 2 * res_scale_sign_flag);
    }

}

/* 6.4.1 */
static bool
process_zscan_order_block_availablity(struct slice_segment_header *slice,
                                      struct hevc_param_set *hps, int xCurr,
                                      int yCurr, int xNbY, int yNbY) {
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    bool availableN;
    int minBlockAddrN;
    /* (6-1) */
    int minBlockAddrCurr = pps->MinTbAddrZs[xCurr >> slice->MinTbLog2SizeY]
                                           [yCurr >> slice->MinTbLog2SizeY];
    if (xNbY < 0 || yNbY < 0 || xNbY >= sps->pic_width_in_luma_samples ||
        yNbY >= sps->pic_height_in_luma_samples) {
        minBlockAddrN = -1;
    } else {
        /* xNbY and yNbY are inside the picture boundaries */
        /* (6-2) */
        minBlockAddrN = pps->MinTbAddrZs[xNbY >> slice->MinTbLog2SizeY]
                                        [yNbY >> slice->MinTbLog2SizeY];
    }
    if (minBlockAddrN < 0 || minBlockAddrN > minBlockAddrCurr) {
        return false;
    }
    // VDBG(hevc, "minBlockAddrCurr %d, minBlockAddrN %d", minBlockAddrCurr,
    //      minBlockAddrN);
    // VDBG(hevc,
    //      "slice_segment_address %x, "
    //      "CtbAddrTsToRs[CtbAddrRsToTs[minBlockAddrN]] %x, "
    //      "CtbAddrTsToRs[CtbAddrRsToTs[minBlockAddrCurr]] %x",
    //      slice->slice_segment_address,
    //      pps->CtbAddrTsToRs[pps->CtbAddrRsToTs[minBlockAddrN]],
    //      pps->CtbAddrTsToRs[pps->CtbAddrRsToTs[minBlockAddrCurr]]);
    int addrRsCurr = (xCurr >> slice->CtbLog2SizeY) + (yCurr >> slice->CtbLog2SizeY) * slice->PicWidthInCtbsY;
    int addrRsN = (xNbY >> slice->CtbLog2SizeY) + (yNbY >> slice->CtbLog2SizeY) * slice->PicWidthInCtbsY;
    // VDBG(hevc, "CtbLog2SizeY %d (%d, %d) addrRsCurr %d, (%d, %d) addrRsN %d",
    //      slice->CtbLog2SizeY, xCurr, yCurr, addrRsCurr, xNbY, yNbY, addrRsN);
    // FIXME for more then one slice, should compare SliceAddrRs
    //  if (addrRsCurr != addrRsN) {
    //      return false
    //  }

    if (pps->TileId[pps->CtbAddrRsToTs[addrRsCurr]] !=
            pps->TileId[pps->CtbAddrRsToTs[addrRsN]]) {
        availableN = false;
    } else {
        availableN = true;
    }
    return availableN;
}

static uint8_t get_CuPredMode(struct slice_segment_header *slice,
                              struct picture *p, int x, int y) {
    return p->CuPredMode[(x>>slice->MinCbLog2SizeY) + (y >> slice->MinCbLog2SizeY) * slice->PicWidthInMinCbsY];
}

static void set_CuPredMode(struct slice_segment_header *slice,
                           struct picture *p, int x0, int y0, int log2CbSize, int CuPredMode)
{
    int CbSize = 1 << (log2CbSize - slice->MinCbLog2SizeY);
    int CuIdx = (x0 >> slice->MinCbLog2SizeY) +
                (y0 >> slice->MinCbLog2SizeY) * slice->PicWidthInMinCbsY;
    for (int y = 0; y < CbSize; y++) {
        for (int x = 0; x < CbSize; x++) {
            p->CuPredMode[CuIdx + x + y * slice->PicWidthInMinCbsY] =
                CuPredMode;
        }
    }
}
static uint8_t get_pcm_flag(struct slice_segment_header *slice, struct picture *p,
                          int x, int y) {
    return p->pcm_flag[(x >> slice->MinCbLog2SizeY) +
                     (y >> slice->MinCbLog2SizeY) * slice->PicWidthInMinCbsY];
}

static void set_pcm_flag(struct slice_segment_header *slice,
                           struct picture *p, int x0, int y0, int log2CbSize,
                           int pcm_flag) {
    int CbSize = 1 << (log2CbSize - slice->MinCbLog2SizeY);
    int CuIdx = (x0 >> slice->MinCbLog2SizeY) +
                (y0 >> slice->MinCbLog2SizeY) * slice->PicWidthInMinCbsY;
    for (int y = 0; y < CbSize; y++) {
        for (int x = 0; x < CbSize; x++) {
            p->CuPredMode[CuIdx + x + y * slice->PicWidthInMinCbsY] =
                pcm_flag;
        }
    }
}
static uint8_t get_IntraPredModeY(struct slice_segment_header *slice,
                                  struct picture *p, int x, int y) {
    int Log2MinPUSize = slice->MinCbLog2SizeY - 1;
    int PicWidthInMinPUs = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - Log2MinPUSize);
    uint8_t v = p->IntraPredModeY[(x >> Log2MinPUSize) + (y >> Log2MinPUSize) * PicWidthInMinPUs];
    return v;
}

static void set_IntraPredModeY(struct slice_segment_header *slice,
                               int log2PbSize, struct picture *p, int x0, int y0,
                               int IntraPredModeY) {
    int Log2MinPUSize = slice->MinCbLog2SizeY - 1;
    int PbSize = 1 << (log2PbSize - Log2MinPUSize);
    int PicWidthInMinPUs = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - Log2MinPUSize);
    int PuIdx = (x0 >> Log2MinPUSize) + (y0 >> Log2MinPUSize) * PicWidthInMinPUs;
    for (int y = 0; y < PbSize; y++) {
        for (int x = 0; x < PbSize; x++) {
            p->IntraPredModeY[PuIdx + x + y * PicWidthInMinPUs] = IntraPredModeY;
        }
    }
}

static uint8_t get_IntraPredModeC(struct slice_segment_header *slice,
                                  struct picture *p, int x, int y) {
    int Log2MinPUSize = slice->MinCbLog2SizeY - 1;
    int PicWidthInMinPUs = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - Log2MinPUSize);
    uint8_t v = p->IntraPredModeC[(x >> Log2MinPUSize) + (y >> Log2MinPUSize) * PicWidthInMinPUs];
    return v & 0x3F;
}

static void set_IntraPredModeC(struct slice_segment_header *slice,
                               int log2PbSize, struct picture *p, int x0, int y0,
                               int IntraPredModeC,
                               bool intra_chroma_pred_mode4) {
    uint8_t v = IntraPredModeC;
    if (intra_chroma_pred_mode4) {
        v |= 0x80;
    }
    int Log2MinPUSize = slice->MinCbLog2SizeY - 1;
    int PbSize = 1 << (log2PbSize - Log2MinPUSize);
    int PicWidthInMinPUs = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - Log2MinPUSize);
    int PuIdx = (x0 >> Log2MinPUSize) + (y0 >> Log2MinPUSize) * PicWidthInMinPUs;
    for (int y =0; y < PbSize; y++) {
        for(int x = 0; x < PbSize; x++) {
            p->IntraPredModeC[PuIdx + x + y * PicWidthInMinPUs] = v;
        }
    }
}

static bool get_intra_chroma_pred_mode4(struct slice_segment_header *slice, struct picture *p, int x, int y) {
    int Log2MinPUSize = slice->MinCbLog2SizeY - 1;
    int PicWidthInMinPUs = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - Log2MinPUSize);
    uint8_t v = p->IntraPredModeC[(x >> Log2MinPUSize) + (y >> Log2MinPUSize) * PicWidthInMinPUs];

    return  (v & 0x80) ? true: false;
}

/* 6.4.2 */
/*
 * @param:
 * @return: the availability of the neighbouring prediction block covering the
 * location (xNbY, yNbY)
 */
static bool process_predication_block_availablity(
    struct slice_segment_header *slice, struct picture *p, struct hevc_param_set *hps,
    int xCb, int yCb, int nCbS, int xPb, int yPb, int nPbW,
    int nPbH, int partIdx, int xNbY, int yNbY) {
    bool availableN;
    bool sameCb;
    if (xCb <= xNbY && yCb <= yNbY && (xCb + nCbS) > xNbY &&
        (yCb + nCbS) > yNbY) {
        sameCb = true;
    } else {
        sameCb = false;
    }

    if (sameCb == false) {
        availableN = process_zscan_order_block_availablity(slice, hps, xPb, yPb,
                                                           xNbY, yNbY);
    } else if ((nPbW << 1) == nCbS && (nPbH << 1) == nCbS && partIdx == 1 &&
               (yCb + nPbH) <= yNbY && (xCb + nPbW) > xNbY) {
        availableN = false;
    } else {
        availableN = true;
    }
    if (availableN && get_CuPredMode(slice, p, xNbY, yNbY) == MODE_INTRA) {
        availableN = false;
    }

    return availableN;
}

// see 8.6.3 Scaling process for transform coefficients
static void scale_transform_coefficients(struct sps *sps, struct cu *cu,
                                         struct slice_segment_header *slice,
                                         struct picture *p,
                                         struct trans_tree *tt,
                                         int transform_skip_flag, int xTbY,
                                         int yTbY, int nTbS, int cIdx, int qP,
                                         int16_t *d) {
    const int levelScale[] = {40, 45, 51, 57, 64, 72};
    int log2TransformRange, bdShift, coeffMin, coeffMax;
    int BitDepthY = sps->bit_depth_luma_minus8 + 8;
    int BitDepthC = sps->bit_depth_chroma_minus8 + 8;
    struct sps_range_extension *sre = &sps->sps_range_ext;
    // see 7-27, 7-30
    int CoeffMinY = -(1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthY + 6) : 15));
    int CoeffMinC = -(1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthC + 6) : 15));
    int CoeffMaxY = (1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthY + 6) : 15)) - 1;
    int CoeffMaxC = (1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthC + 6) : 15)) - 1;

    VDBG(hevc, "CoeffMinY %d, CoeffMaxY %d, CoeffMinC %d, CoeffMaxC %d",
         CoeffMinY, CoeffMaxY, CoeffMinC, CoeffMaxC);
    // see 8-300 , 8-307
    if (cIdx == 0) {
        log2TransformRange =
            sps->sps_range_ext.extended_precision_processing_flag
                ? MAX(15, BitDepthY + 6)
                : 15;
        bdShift = BitDepthY + log2floor(nTbS) + 10 - log2TransformRange;
        coeffMin = CoeffMinY;
        coeffMax = CoeffMaxY;
    } else {
        log2TransformRange =
            sps->sps_range_ext.extended_precision_processing_flag
                ? MAX(15, BitDepthC + 6)
                : 15;
        bdShift = BitDepthC + log2floor(nTbS) + 10 - log2TransformRange;
        coeffMin = CoeffMinC;
        coeffMax = CoeffMaxC;
        VDBG(hevc, "bdShift %d, ", bdShift);
    }
    // scaling factor
    VDBG(hevc, "nTbS %d, scaling_list_enabled_flag %d", nTbS, sps->scaling_list_enabled_flag);
    int m = 16;
    int sizeid = log2floor(nTbS) - 2;
    assert(sizeid < 4);
    int mid = (get_CuPredMode(slice, p, xTbY, yTbY) == MODE_INTRA) ? cIdx : cIdx - 3;
    if (sps->scaling_list_enabled_flag == 0 ||
        (transform_skip_flag == 1 && nTbS > 4)) {
        for (int y = 0; y < nTbS; y++) {
            for (int x = 0; x < nTbS; x++) {
                // scaled transform coefficient
                d[x + y * nTbS] =
                    clip3(coeffMin, coeffMax,
                        ((tt->TransCoeffLevel[cIdx][xTbY + x - tt->xT0][yTbY + y - tt->yT0] *
                        m * levelScale[qP % 6] << (qP / 6)) + (1 << (bdShift - 1))) >> bdShift);
            }
        }
    } else {
        for (int y = 0; y < nTbS; y++) {
            for (int x = 0; x < nTbS; x++) {
                m = slice->ScalingFactor[sizeid][mid][x][y];
                d[x + y * nTbS] =
                    clip3(coeffMin, coeffMax,
                          ((tt->TransCoeffLevel[cIdx][xTbY + x - tt->xT0][yTbY + y - tt->yT0] *
                          m * levelScale[qP % 6]<< (qP / 6)) + (1 << (bdShift - 1))) >> bdShift);
            }
        }
    }

    VDBG(hevc, "scale_transform_coefficients");
    for (int j = 0; j < nTbS; j++) {
        for (int i = 0; i < nTbS; i++) {
            vlog(VLOG_DEBUG, vlog_hevc, " %d", d[i + j * nTbS]);
        }
        vlog(VLOG_DEBUG, vlog_hevc,"\n");
    }
}

// see 8.6.4.2
static void transformation(int nTbS, int trType, int16_t *x, int *y) {
    const int8_t transMatrix[4][4] = {
        {29, 55, 74, 84},
        {74, 74, 0, -74},
        {84, -29, -74, 55},
        {55, -84, 74, -29}
    };
    const int8_t transMatrixCol[32][32] = {
        {64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64},
        {90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46, 38, 31, 22, 13,  4,  -4,-13,-22,-31,-38,-46,-54,-61,-67,-73,-78,-82,-85,-88,-90,-90},
        {90, 87, 80, 70, 57, 43, 25,  9, -9,-25,-43,-57,-70,-80,-87,-90,  -90,-87,-80,-70,-57,-43,-25, -9,  9, 25, 43, 57, 70, 80, 87, 90},
        {90, 82, 67, 46, 22, -4,-31,-54,-73,-85,-90,-88,-78,-61,-38,-13,  13, 38, 61, 78, 88, 90, 85, 73, 54, 31,  4,-22,-46,-67,-82,-90},
        {89, 75, 50, 18,-18,-50,-75,-89,-89,-75,-50,-18, 18, 50, 75, 89,  89, 75, 50, 18,-18,-50,-75,-89,-89,-75,-50,-18, 18, 50, 75, 89},
        {88, 67, 31,-13,-54,-82,-90,-78,-46, -4, 38, 73, 90, 85, 61, 22,  -22,-61,-85,-90,-73,-38,  4, 46, 78, 90, 82, 54, 13,-31,-67,-88},
        {87, 57,  9,-43,-80,-90,-70,-25, 25, 70, 90, 80, 43, -9,-57,-87,  -87,-57, -9, 43, 80, 90, 70, 25,-25,-70,-90,-80,-43,  9, 57, 87},
        {85, 46,-13,-67,-90,-73,-22, 38, 82, 88, 54, -4,-61,-90,-78,-31,  31, 78, 90, 61,  4,-54,-88,-82,-38, 22, 73, 90, 67, 13,-46,-85},
        {83, 36,-36,-83,-83,-36, 36, 83, 83, 36,-36,-83,-83,-36, 36, 83,  83, 36,-36,-83,-83,-36, 36, 83, 83, 36,-36,-83,-83,-36, 36, 83},
        {82, 22,-54,-90,-61, 13, 78, 85, 31,-46,-90,-67,  4, 73, 88, 38,  -38,-88,-73, -4, 67, 90, 46,-31,-85,-78,-13, 61, 90, 54,-22,-82},
        {80,  9,-70,-87,-25, 57, 90, 43,-43,-90,-57, 25, 87, 70, -9,-80, -80, -9, 70, 87, 25,-57,-90,-43, 43, 90, 57,-25,-87,-70,  9, 80},
        {78, -4,-82,-73, 13, 85, 67,-22,-88,-61, 31, 90, 54,-38,-90,-46, 46, 90, 38,-54,-90,-31, 61, 88, 22,-67,-85,-13, 73, 82,  4,-78},
        {75,-18,-89,-50, 50, 89, 18,-75,-75, 18, 89, 50,-50,-89,-18, 75, 75,-18,-89,-50, 50, 89, 18,-75,-75, 18, 89, 50,-50,-89,-18, 75},
        {73,-31,-90,-22, 78, 67,-38,-90,-13, 82, 61,-46,-88, -4, 85, 54, -54,-85,  4, 88, 46,-61,-82, 13, 90, 38,-67,-78, 22, 90, 31,-73},
        {70,-43,-87,  9, 90, 25,-80,-57, 57, 80,-25,-90, -9, 87, 43,-70, -70, 43, 87, -9,-90,-25, 80, 57,-57,-80, 25, 90,  9,-87,-43, 70},
        {67,-54,-78, 38, 85,-22,-90,  4, 90, 13,-88,-31, 82, 46,-73,-61, 61, 73,-46,-82, 31, 88,-13,-90, -4, 90, 22,-85,-38, 78, 54,-67},
        {64,-64,-64, 64, 64,-64,-64, 64, 64,-64,-64, 64, 64,-64,-64, 64, 64,-64,-64, 64, 64,-64,-64, 64, 64,-64,-64, 64, 64,-64,-64, 64},
        {61,-73,-46, 82, 31,-88,-13, 90, -4,-90, 22, 85,-38,-78, 54, 67, -67,-54, 78, 38,-85,-22, 90,  4,-90, 13, 88,-31,-82, 46, 73,-61},
        {57,-80,-25, 90, -9,-87, 43, 70,-70,-43, 87,  9,-90, 25, 80,-57, -57, 80, 25,-90,  9, 87,-43,-70, 70, 43,-87, -9, 90,-25,-80, 57},
        {54,-85, -4, 88,-46,-61, 82, 13,-90, 38, 67,-78,-22, 90,-31,-73, 73, 31,-90, 22, 78,-67,-38, 90,-13,-82, 61, 46,-88,  4, 85,-54},
        {50,-89, 18, 75,-75,-18, 89,-50,-50, 89,-18,-75, 75, 18,-89, 50, 50,-89, 18, 75,-75,-18, 89,-50,-50, 89,-18,-75, 75, 18,-89, 50},
        {46,-90, 38, 54,-90, 31, 61,-88, 22, 67,-85, 13, 73,-82,  4, 78, -78, -4, 82,-73,-13, 85,-67,-22, 88,-61,-31, 90,-54,-38, 90,-46},
        {43,-90, 57, 25,-87, 70,  9,-80, 80, -9,-70, 87,-25,-57, 90,-43, -43, 90,-57,-25, 87,-70, -9, 80,-80,  9, 70,-87, 25, 57,-90, 43},
        {38,-88, 73, -4,-67, 90,-46,-31, 85,-78, 13, 61,-90, 54, 22,-82, 82,-22,-54, 90,-61,-13, 78,-85, 31, 46,-90, 67,  4,-73, 88,-38},
        {36,-83, 83,-36,-36, 83,-83, 36, 36,-83, 83,-36,-36, 83,-83, 36, 36,-83, 83,-36,-36, 83,-83, 36, 36,-83, 83,-36,-36, 83,-83, 36},
        {31,-78, 90,-61,  4, 54,-88, 82,-38,-22, 73,-90, 67,-13,-46, 85, -85, 46, 13,-67, 90,-73, 22, 38,-82, 88,-54, -4, 61,-90, 78,-31},
        {25,-70, 90,-80, 43,  9,-57, 87,-87, 57, -9,-43, 80,-90, 70,-25, -25, 70,-90, 80,-43, -9, 57,-87, 87,-57,  9, 43,-80, 90,-70, 25},
        {22,-61, 85,-90, 73,-38, -4, 46,-78, 90,-82, 54,-13,-31, 67,-88, 88,-67, 31, 13,-54, 82,-90, 78,-46,  4, 38,-73, 90,-85, 61,-22},
        {18,-50, 75,-89, 89,-75, 50,-18,-18, 50,-75, 89,-89, 75,-50, 18, 18,-50, 75,-89, 89,-75, 50,-18,-18, 50,-75, 89,-89, 75,-50, 18},
        {13,-38, 61,-78, 88,-90, 85,-73, 54,-31,  4, 22,-46, 67,-82, 90, -90, 82,-67, 46,-22, -4, 31,-54, 73,-85, 90,-88, 78,-61, 38,-13},
        {9,-25, 43,-57, 70,-80, 87,-90, 90,-87, 80,-70, 57,-43, 25, -9,  -9, 25,-43, 57,-70, 80,-87, 90,-90, 87,-80, 70,-57, 43,-25,  9},
        {4,-13, 22,-31, 38,-46, 54,-61, 67,-73, 78,-82, 85,-88, 90,-90,  90,-90, 88,-85, 82,-78, 73,-67, 61,-54, 46,-38, 31,-22, 13, -4}
    };

    // save unnecessary multiply with zero
    int last_nz = 0;
    for (int i = nTbS -1; i>=0; i--) {
        if (x[i]) {
            last_nz = i;
            break;
        }
    }

    if (trType == 1) {
        for (int i = 0; i < nTbS; i++) {
            y[i] = 0;
            for (int j = 0; j <= last_nz; j++) {
                y[i] += transMatrix[j][i] * x[j];
            }
        }
    } else {
        for (int i = 0; i < nTbS; i++) {
            y[i] = 0;
            for (int j = 0; j <= last_nz; j++) {
                y[i] += transMatrixCol[j * (1 << (5 - log2floor(nTbS)))][i] * x[j];
                // fprintf(vlog_get_stream(), "(j %d, i %d: %d * %d)",
                //         (1 << (5 - log2floor(nTbS)))* j
                //             , i, transMatrixCol[j * (1 << (5 - log2floor(nTbS)))][i],
                //         x[j]);
            }
            // fprintf(vlog_get_stream(), "%d\n", y[i]);
        }
    }
}

// see 8.6.4.1 Transformation process for scaled transform coefficients
static int transform_scaled_coeffients(struct sps *sps, struct slice_segment_header *slice, struct picture *p, 
                                       struct cu *cu, int xTbY,
                                       int yTbY, int nTbS, int cIdx,
                                       int16_t* d, int16_t* r) {
    int BitDepthY = sps->bit_depth_luma_minus8 + 8;
    int BitDepthC = sps->bit_depth_chroma_minus8 + 8;
    int bitdepth = (cIdx == 0) ? BitDepthY : BitDepthC;
    struct sps_range_extension *sre = &sps->sps_range_ext;
    int bdShift = MAX(20 - bitdepth,(sps->sps_range_ext.extended_precision_processing_flag ? 11 : 0));

    // see 7-27, 7-30
    int CoeffMinY = -(1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthY + 6) : 15));
    int CoeffMinC = -(1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthC + 6) : 15));
    int CoeffMaxY = (1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthY + 6) : 15)) - 1;
    int CoeffMaxC = (1 << (sre->extended_precision_processing_flag ? MAX(15, BitDepthC + 6) : 15)) - 1;
    int coeffMin = CoeffMinY;
    int coeffMax = CoeffMaxY;
    if (cIdx > 0) {
        coeffMin = CoeffMinC;
        coeffMax = CoeffMaxC;
    }
    int trType = 0;
    if (get_CuPredMode(slice, p, xTbY, yTbY) == MODE_INTRA && nTbS == 4 && cIdx == 0) {
        trType = 1;
    }
    VDBG(hevc, "trType %d, nTbS %d, coeffMin %d,coeffMax %d", trType, nTbS,
         coeffMin, coeffMax);
    // Each (vertical) column of scaled transform coefficients s d[x][y]  is
    // transformed by invoking the one-dimensional transformation process
    int e[32];
    int16_t tmp[32];
    int16_t g[32][32];
    for (int x = 0; x < nTbS; x++) {
        for (int y = 0; y < nTbS; y++) {
            tmp[y] = d[x+y*nTbS];
            // fprintf(vlog_get_stream(), " %d", tmp[y]);
        }
        // fprintf(vlog_get_stream(), "\n");
        // invoke 8.6.4.2
        transformation(nTbS, trType, tmp, e);
        for (int y = 0; y < nTbS; y++) {
            g[x][y] = clip3(coeffMin, coeffMax, (e[y]+64)>>7);
        }
    }
    // VDBG(hevc, "transform DTCV %d ", nTbS);
    // for (int j = 0; j < nTbS; j++) {
    //     for (int i = 0; i < nTbS; i++) {
    //         fprintf(vlog_get_stream(), " %d", g[i][j]);
    //     }
    //     fprintf(vlog_get_stream(), "\n");
    // }

    // Each (horizontal) row of the resulting array g[x][y] is transformed to
    // r[x][y] by invoking the one-dimensional transformation
    for (int y = 0; y < nTbS; y++) {
        int out[32];
        for (int x = 0; x < nTbS; x++) {
            tmp[x] = g[x][y];
            // fprintf(vlog_get_stream(), " %d", tmp[x]);
        }
        // fprintf(vlog_get_stream(), "\n");
        // invoke 8.6.4.2
        transformation(nTbS, trType, tmp, out);
        for (int x = 0; x < nTbS; x++) {
            r[x + y * nTbS] = (out[x] +(1 << (bdShift - 1))) >> bdShift;
        }
    }

    return 0;
}

// 8.6.5 Residual modification process for blocks using a transform bypass
static void
residual_modification_transform_bypass(int mDir, int nTbs, int16_t *r)
{
    if (mDir == 0) { // horizontal direction
        for (int y = 1; y < nTbs; y++) {
            for (int x = 0; x < nTbs; x++) {
                r[x + nTbs * y] += r[x - 1 + nTbs * y];
            }
        }
    } else { // vertical direction
        for (int y = 1; y < nTbs; y++) {
            for (int x = 0; x < nTbs; x++) {
                r[x + nTbs * y] += r[x + nTbs * (y - 1)];
            }
        }
    }
}

// 8.6.6 Residual modification process for transform blocks using cross-component
// prediction
static void
residual_modification_transform_cross_prediction(struct sps *sps,struct cu *cu, int xTbY, int yTbY, int nTbS, int cIdx, int16_t *rY, int16_t* r)
{
    int BitDepthY = sps->bit_depth_luma_minus8 + 8;
    int BitDepthC = sps->bit_depth_chroma_minus8 + 8;
    int ResScaleVal = cu->ccp[xTbY][yTbY].ResScaleVal[cIdx];
    for (int y = 0; y < nTbS; y++) {
        for (int x = 0; x < nTbS; x++) {
            r[x + nTbS * y] += (ResScaleVal * ((rY[x + nTbS *y] << BitDepthC) >> BitDepthY)) >> 3;
        }
    }
}


struct quant_pixel {
    int q_y;
    int q_cb;
    int q_cr;
};

static uint8_t get_split_transform_flag(struct slice_segment_header *slice,
                                        struct picture *p, int xTb, int yTb,
                                        int trafoDepth) {
    int PicWidthInTbsY = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY);
    int PicHeightInTbsY = slice->PicHeightInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY);

    return p->split_transform_flag[(xTb >> slice->MinTbLog2SizeY) +
                                   (yTb >> slice->MinTbLog2SizeY) *
                                       PicWidthInTbsY] & (1 << trafoDepth);
}

static void set_split_transform_flag(struct slice_segment_header *slice,
                                     struct picture *p, int xTb, int yTb,
                                     int trafoDepth, int split_transform_flag) {
    int PicWidthInTbsY = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY);
    int PicHeightInTbsY = slice->PicHeightInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY);
    p->split_transform_flag[(xTb >> slice->MinTbLog2SizeY) +
                            (yTb >> slice->MinTbLog2SizeY) * PicWidthInTbsY] |= (split_transform_flag << trafoDepth);
}

static uint8_t get_qpy(struct slice_segment_header *slice,
                           struct picture *p, int x, int y) {
    return p->qpy[(x >> slice->MinCbLog2SizeY) +
                     (y >> slice->MinCbLog2SizeY) * slice->PicWidthInMinCbsY];
}

static void set_qpy(struct slice_segment_header *slice, struct picture *p,
                    int x0, int y0, int log2CbSize, int qpy) {
    int CbSize = 1 << (log2CbSize - slice->MinCbLog2SizeY);
    int CuIdx = (x0 >> slice->MinCbLog2SizeY) +
                (y0 >> slice->MinCbLog2SizeY) * slice->PicWidthInMinCbsY;

    VDBG(hevc, "set_qpy (%d, %d)-(%d, %d): %d", x0, y0, x0 + (1 << log2CbSize),
         y0 + (1 << log2CbSize), qpy);
    for (int y = 0; y < CbSize; y++) {
        for (int x = 0; x < CbSize; x++) {
            p->qpy[CuIdx + x + y * slice->PicWidthInMinCbsY] = qpy;
        }
    }
}

// 8.6.1 derivation process for quatization parameters
static struct quant_pixel
quatization_parameters(int xCb, int yCb,
                       struct hevc_param_set *hps,
                       struct slice_segment_header *slice, struct cu *cu,
                       struct trans_tree *tt, struct picture *p) {

    // See table 8-10, qpc for ChromaArrayType equal to 1
    static const int qpc_from_qpi[] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 29, 30, 31, 32, 33, 33,
        34, 34, 35, 35, 36, 36, 37, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
        48, 49, 50, 51
    };
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    int qPY_prev, qPY_pred, left_qpy, top_qpy;
    static int last_xQg = -1, last_yQg = -1;
    static struct quant_pixel last_q;
    static struct quant_pixel prev_q;

    bool first_quant_group_in_slice = false;
    bool first_quant_group_in_tile = false;
    bool first_quant_group_in_row = false;

    // see 7-54, SliceQpY should be in the range of [-QpBdOffsetY, +51]
    int SliceQpY = pps->init_qp_minus26 + 26 + slice->slice_qp_delta;
    int QpBdOffsetY = 6 * sps->bit_depth_luma_minus8;
    int QpBdOffsetC = 6 * sps->bit_depth_chroma_minus8;
    int xQg = xCb - (xCb & ((1 << slice->Log2MinCuQpDeltaSize) - 1));
    int yQg = yCb - (yCb & ((1 << slice->Log2MinCuQpDeltaSize) - 1));

    if (xQg == last_xQg && yQg == last_yQg) {
        VDBG(hevc, "last q %d (%d, %d)", last_q.q_y, xQg, yQg);
        // set_qpy(slice, p, xCb, yCb, cu->log2CbSize, last_q.q_y - QpBdOffsetY);
        // return last_q;
    } else {
        last_xQg = xQg;
        last_yQg = yQg;
        prev_q = last_q;
    }
    // struct tu *tu = &tt->tus[tt->tu_num-1];

    int firstCtbaddrRs = slice->slice_segment_address;
    int xSinSlice = slice->CtbSizeY * (firstCtbaddrRs % slice->PicWidthInCtbsY);
    int ySinSlice = slice->CtbSizeY * (firstCtbaddrRs / slice->PicWidthInCtbsY);
    if (xSinSlice == xQg && ySinSlice == yQg) {
        first_quant_group_in_slice = true;
    }
    VDBG(hevc, "first qg (%d %d) %d", xSinSlice, ySinSlice,
         first_quant_group_in_slice);
    if (xQg == 0 && (yQg & ((1<<slice->CtbLog2SizeY)-1))==0) {
        first_quant_group_in_row = true;
    }
    if (pps->tiles_enabled_flag) {
        if ((xQg & (slice->CtbSizeY-1)) == 0 &&
            (yQg & (slice->CtbSizeY-1)) == 0) {
            //FIXME
            first_quant_group_in_tile = true;
        }
    }

    VDBG(hevc, "first qg %d %d %d (%d, %d)", first_quant_group_in_slice,
         first_quant_group_in_tile, first_quant_group_in_row, xCb, yCb);
    if (first_quant_group_in_slice || first_quant_group_in_tile ||
        (first_quant_group_in_row &&
         (pps->entropy_coding_sync_enabled_flag == 1))) {
        qPY_prev = SliceQpY;
    } else {
        qPY_prev = prev_q.q_y;
    }
    VDBG(hevc, "xQg, yQg(%d, %d) qPY_prev %d, MinTbLog2SizeY %d", xQg,
         yQg, qPY_prev, slice->MinTbLog2SizeY);
    bool availableA = process_zscan_order_block_availablity(slice, hps, xCb,
                                                            yCb, xQg - 1, yQg);
    int qPY_A = 0, qPY_B = 0;
    // int CtbAddrInRs = 
    // (xQg >> slice->CtbLog2SizeY) + (yQg >> slice->CtbLog2SizeY) * slice->PicWidthInCtbsY;
    // int CtbAddrInTs = pps->CtbAddrRsToTs[CtbAddrInRs];
    int CtbAddrInTs = p->ctus[p->ctu_num - 1]->CtbAddrInTs;

    int xTmp = (xQg - 1) >> slice->MinTbLog2SizeY;
    int yTmp = yQg >> slice->MinTbLog2SizeY;
    if (availableA == true) {
        int minTbAddrA = pps->MinTbAddrZs[xTmp][yTmp];
        int ctbAddrA = minTbAddrA >> (2 * (slice->CtbLog2SizeY - slice->MinTbLog2SizeY));
        if ( ctbAddrA == CtbAddrInTs) {
            qPY_A = get_qpy(slice, p, xQg - 1, yQg); // qpy of (xQg - 1, yQg)
        } else {
            qPY_A = qPY_prev;
        }
    } else {
        qPY_A = qPY_prev;
    }
    bool availableB = process_zscan_order_block_availablity(slice, hps, xCb,
                                                            yCb, xQg, yQg - 1);
    xTmp = (xQg) >> slice->MinTbLog2SizeY;
    yTmp = (yQg - 1) >> slice->MinTbLog2SizeY;
    if (availableB == true) {
        int minTbAddrB = pps->MinTbAddrZs[xTmp][yTmp];
        int ctbAddrB = minTbAddrB >> (2 * (slice->CtbLog2SizeY - slice->MinTbLog2SizeY));
        if(ctbAddrB == CtbAddrInTs) {
            VDBG(hevc, "ctbAddrB %d, (%d, %d)", ctbAddrB,
                 xQg >> slice->MinCbLog2SizeY, (yQg -1) >> slice->MinCbLog2SizeY);
            qPY_B = get_qpy(slice, p, xQg, yQg - 1); // qpy of (xQg, yQg-1)
        } else {
            qPY_B = qPY_prev;
        }
    } else {
        qPY_B = qPY_prev;
    }
    qPY_pred = (qPY_A + qPY_B + 1) >> 1;
    VDBG(hevc,
         "avalaibleA %d, avalaibleB %d, qPY_A %d, qPY_B %d, qpy_pred %d, "
         "QpBdOffsetY %d",
         availableA, availableB, qPY_A, qPY_B, qPY_pred, QpBdOffsetY);
    int qPcb, qPcr;
    int Qpy = ((qPY_pred + slice->CuQpDeltaVal + 52 + 2 * (QpBdOffsetY)) %(52 + QpBdOffsetY)) - QpBdOffsetY;
    VDBG(hevc, "Qpy %d, CuQpDeltaVal %d, log2CbSize %d, %d", Qpy,
         slice->CuQpDeltaVal, cu->log2CbSize, slice->CtbLog2SizeY);

    set_qpy(slice, p, xCb, yCb, cu->log2CbSize, Qpy);
    int Qp_Y = Qpy + QpBdOffsetY;
    int Qp_Cb;
    int Qp_Cr;
    if (slice->ChromaArrayType != 0) {
        VDBG(hevc, "tu_residual_act_flag %d, Qp_Y %d, CuQpDeltaVal %d, "
             "CuQpOffsetCb %d, CuQpOffsetCr %d",
             tt->tu_residual_act_flag[xCb - tt->xT0][yCb - tt->yT0], Qp_Y,
             slice->CuQpDeltaVal, cu->CuQpOffsetCb, cu->CuQpOffsetCr);
        if (tt->tu_residual_act_flag[xCb - tt->xT0][yCb-tt->yT0] == 0) {
            qPcb = clip3(-QpBdOffsetC, 57,
                         Qpy + pps->pps_cb_qp_offset +
                             slice->slice_cb_qp_offset + cu->CuQpOffsetCb);
            qPcr = clip3(-QpBdOffsetC, 57,
                         Qpy + pps->pps_cr_qp_offset +
                             slice->slice_cr_qp_offset + cu->CuQpOffsetCr);
            VDBG(hevc,
                 "qPiCb %d, qPiCr %d, pps_cb_qp_offset %d slice_cb_qp_offset %d",
                 qPcb, qPcr, pps->pps_cb_qp_offset, slice->slice_cb_qp_offset);
        } else {
            int PpsActQpOffsetCb =
                    pps->pps_scc_ext.pps_act_cb_qp_offset_plus5 - 5;
            int PpsActQpOffsetCr =
                    pps->pps_scc_ext.pps_act_cr_qp_offset_plus3 - 3;
            
            qPcb = clip3(-QpBdOffsetC, 57,
                         Qpy + PpsActQpOffsetCb +
                             slice->slice_act_cb_qp_offset + cu->CuQpOffsetCb);
            qPcr = clip3(-QpBdOffsetC, 57,
                         Qpy + PpsActQpOffsetCr +
                             slice->slice_act_cr_qp_offset + cu->CuQpOffsetCr);
        }
        if (slice->ChromaArrayType == 1) {
            qPcb = qpc_from_qpi[qPcb];
            qPcr = qpc_from_qpi[qPcr];
        } else {
            qPcb = MIN(qPcb, 51);
            qPcr = MIN(qPcr, 51);
        }
        Qp_Cb = qPcb + QpBdOffsetC;
        Qp_Cr = qPcr + QpBdOffsetC;
    }
    last_q.q_y = Qp_Y;
    last_q.q_cb = Qp_Cb;
    last_q.q_cr = Qp_Cr;
    VDBG(hevc, "qp (%d, %d, %d)", Qp_Y, Qp_Cb, Qp_Cr);

    return last_q;
}

// see 8.6.2 Scaling and transformation process
static int scale_and_transform(struct cu *cu, int transform_skip_flag,
                              struct hevc_param_set *hps,
                              struct slice_segment_header *slice,
                              struct trans_tree *tt, int xTbY, int yTbY,
                              int trafoDepth, int cIdx, int nTbS, int16_t* r, struct picture *p) {
    struct pps *pps = hps->pps;
    int qP;
    int PpsActQpOffsetY = pps->pps_scc_ext.pps_act_y_qp_offset_plus5 - 5;
    struct sps *sps = hps->sps;
    int QpBdOffsetY = 6 * sps->bit_depth_luma_minus8;
    struct quant_pixel q =
        quatization_parameters(xTbY, yTbY, hps, slice, cu, tt, p);
    // see 8-291, 8-292
    if (cIdx == 0) {
        qP = clip3(0, 51 + QpBdOffsetY,
                   q.q_y + (tt->tu_residual_act_flag[xTbY-tt->xT0][yTbY-tt->yT0]
                                ? PpsActQpOffsetY + slice->slice_act_y_qp_offset
                                : 0));
    } else if (cIdx == 1) {
        qP = q.q_cb;
    } else {
        qP = q.q_cr;
    }
    VDBG(hevc, "(%d, %d) qP %d", xTbY, yTbY, qP);
    int BitDepthY = sps->bit_depth_luma_minus8 + 8;
    int BitDepthC = sps->bit_depth_chroma_minus8 + 8;
    int bitdepth = (cIdx == 0) ? BitDepthY : BitDepthC;
    
    int rotateCoeffs = 0;

    if (sps->sps_range_ext.transform_skip_rotation_enabled_flag == 1 &&
        nTbS == 4 && get_CuPredMode(slice, p, xTbY, yTbY) == MODE_INTRA) {
        rotateCoeffs = 1;
    }

    if (cu->cu_transquant_bypass_flag == 1) {
        for (int y = 0; y < nTbS; y++) {
            for (int x = 0; x < nTbS; x++) {
                //see 8-297
                if (rotateCoeffs == 1) {
                    r[x+y*nTbS] = tt->TransCoeffLevel[cIdx][xTbY + nTbS - x - 1 - tt->xT0]
                                                 [yTbY + nTbS - y - 1 - tt->yT0];
                } else {
                    r[x + y * nTbS] =
                        tt->TransCoeffLevel[cIdx][xTbY + x - tt->xT0]
                                           [yTbY + y - tt->yT0];
                }
            }
        }
    } else {
        int tsShift = 5 + log2floor(nTbS);
        int16_t d[32*32];
        // step 1, invoke 8.6.3
        scale_transform_coefficients(sps, cu, slice, p, tt, transform_skip_flag, xTbY, yTbY, nTbS,
                                     cIdx, qP, d);

        // step 2
        if (transform_skip_flag == 1) {
            for (int y = 0; y < nTbS; y++) {
                for (int x = 0; x < nTbS; x++) {
                    r[x + y*nTbS] =
                        (rotateCoeffs ? d[nTbS - x - 1 + (nTbS - y - 1)*nTbS] : d[x+y*nTbS]) << tsShift;
                }
            }
        } else {
            // invoke 8.6.4
            transform_scaled_coeffients(sps, slice, p, cu, xTbY, yTbY, nTbS, cIdx, d, r);
        }

        // step 3, move this step in transform_scaled_coeffients so int16 array
        // can hold the right value range
        //  for (int y = 0; y < nTbS; y++) {
        //      for (int x = 0; x < nTbS; x++) {
        //          r[x + y * nTbS] =
        //              (r[x + y * nTbS] + (1 << (bdShift - 1))) >> bdShift;
        //      }
        //  }
#if 0
        VDBG(hevc, "scale_and_transform %d ", nTbS);
        for (int j = 0; j < nTbS; j++) {
            for (int i = 0; i < nTbS; i++) {
                fprintf(vlog_get_stream(), " %d", r[i + j * nTbS]);
            }
            fprintf(vlog_get_stream(), "\n");
        }
#endif
    }

    return 0;
}
#define Clip1Y(x) clip3(0, (1 << BitDepthY) - 1, (x))
#define Clip1C(x) clip3(0, (1 << BitDepthC) - 1, (x))
// 8.6.7
static void
construct_pic_pior_to_filtering(struct sps *sps, int xCurr, int yCurr, int nCurrSw, int nCurrSh,
                                int cIdx, int16_t *predSamples, int16_t *resSamples,
                                int16_t *recSamples, int stride) {
    int BitDepthY = sps->bit_depth_luma_minus8 + 8;
    int BitDepthC = sps->bit_depth_chroma_minus8 + 8;
    if (cIdx == 0) {
        for (int i = 0; i < nCurrSw; i++) {
            for (int j = 0; j < nCurrSh; j++) {
                recSamples[xCurr + i + stride * (yCurr + j)] = Clip1Y(
                    predSamples[i + j * nCurrSw] + resSamples[i + j * nCurrSw]);
            }
        }
    } else {
        for (int i = 0; i < nCurrSw; i++) {
            for (int j = 0; j < nCurrSh; j++) {
                recSamples[xCurr + i + stride * (yCurr + j)] = Clip1C(
                    predSamples[i + j * nCurrSw] + resSamples[i + j * nCurrSw]);
            }
        }
    }
}

// 8.4.4.2.2
static void reference_sample_substitution(struct sps *sps, int16_t *left,
                                          int16_t *top, int nTbS, int cIdx,
                                          int unavailable, int8_t *unavaibleL,
                                          int8_t *unavaibleT) {
    // bit_depth_luma_minus8 in the range 0 - 8
    // bit_depth_chroma_minus8 in the range 0 - 8
    // so bitDepth - 1 should use 15bit at most which a int16_t could hold
    int bitDepth = (cIdx == 0) ? sps->bit_depth_luma_minus8 + 8
                               : sps->bit_depth_chroma_minus8 + 8;
    VDBG(hevc, "unavailable %d, %d", unavailable, 4 * nTbS + 1);
    if (unavailable == 4*nTbS + 1) {
        for (int i = 0; i < nTbS * 2; i++) {
            left[i] = 1 << (bitDepth - 1);
        }
        for (int i = -1; i < nTbS * 2; i++) {
            top[i] = 1 << (bitDepth - 1);
        }
    } else {
        VDBG(hevc, "unavaibleL");
        for (int i = 0; i < 2 * nTbS; i++) {
            fprintf(vlog_get_stream(), "%x ", unavaibleL[i]);
        }
        fprintf(vlog_get_stream(), "\n");
        VDBG(hevc, "unavaibleT: %x", unavaibleT[-1]);
        for (int i = 0; i < 2 * nTbS; i++) {
            fprintf(vlog_get_stream(), "%x ", unavaibleT[i]);
        }
        fprintf(vlog_get_stream(), "\n");
        if (unavaibleL[nTbS * 2 - 1]) {
            int y;
            for (y = nTbS*2-1; y >= 0; y--) {
                if (unavaibleL[y] == 0) {
                    left[nTbS*2-1] = left[y];
                    break;
                }
            }
            if (y < 0) {
                for (int x = -1; x < nTbS * 2; x++) {
                    if (unavaibleT[x] == 0) {
                        left[nTbS*2-1] = top[x];
                        break;
                    }
                }
            }
        }

        for (int y = nTbS * 2 - 2; y >= 0; y--) {
            if (unavaibleL[y]) {
                left[y] = left[y + 1];
            }
        }
        if (unavaibleT[-1]) {
            top[-1] = left[0];
        }
        for (int x = 0; x < nTbS * 2; x++) {
            if (unavaibleT[x]) {
                top[x] = top[x-1];
            }
        }
    }
    // now all samples are marked as "available"    VDBG(hevc, "left: %d", 2 *
    // nTbS);
    VDBG(hevc, "left: %d", 2 * nTbS);
    for (int i = 0; i < 2 * nTbS; i++) {
        fprintf(vlog_get_stream(), "%x ", left[i]);
    }
    fprintf(vlog_get_stream(), "\n");
    VDBG(hevc, "top: %x", top[-1]);
    for (int i = 0; i < 2 * nTbS; i++) {
        fprintf(vlog_get_stream(), "%x ", top[i]);
    }
    fprintf(vlog_get_stream(), "\n");
}

//8.4.4.2.3
static void
filtering_neighbouring_samples(struct sps *sps, int predModeIntra, int cIdx, int nTbS, int16_t* left, int16_t *top)
{

    const int intraHorVerDistThres[] = {7, 1, 0};
    int filterFlag = -1;
    int minDistVerHor;
    if (predModeIntra == INTRA_DC || nTbS == 4) {
        filterFlag = 0;
    } else {
        minDistVerHor = MIN(ABS(predModeIntra-26), ABS(predModeIntra-10));
        if (minDistVerHor > intraHorVerDistThres[log2floor(nTbS/8)]) {
            filterFlag = 1;
        } else {
            filterFlag = 0;
        }
    }
    VDBG(hevc, "predModeIntra %d, filterFlag %d, %d, %d", predModeIntra,
         filterFlag, minDistVerHor, log2floor(nTbS / 8));
    if (filterFlag == 1) {
        int biIntFlag = 0;
        int BitDepthY = sps->bit_depth_luma_minus8 + 8;
        if (sps->strong_intra_smoothing_enabled_flag == 1 && cIdx == 0 &&
            nTbS == 32 &&
            ABS(top[-1] + top[nTbS * 2 - 1] - 2 * top[nTbS - 1]) <
                (1 << (BitDepthY - 5)) &&
            ABS(top[-1] + left[nTbS * 2 - 1] - 2 * left[nTbS - 1]) <
                (1 << (BitDepthY - 5))) {
            biIntFlag = 1;
        }

        int16_t fleft[64];
        int16_t mftop[65];
        assert(nTbS <= 32);
        int16_t *ftop = mftop + 1;
        if (biIntFlag == 1) {
            ftop[-1] = top[-1];
            for (int y = 0; y < 63; y++) {
                fleft[y] = (top[-1] * (63 - y) + (y + 1) * left[63] + 32) >> 6;
            }
            fleft[63] = left[63];
            for (int x = 0; x < 63; x++) {
                ftop[x] = (top[-1] * (63 - x) + (x + 1) * top[63] + 32) >> 6;
            }
            ftop[63] = top[63];
            memcpy(top - 1, mftop, 65 * sizeof(int16_t));
            memcpy(left, fleft, 64 * sizeof(int16_t));
        } else {
            ftop[-1] = (left[0] + 2 * top[-1] + top[0] + 2) >> 2;
            fleft[0] = (left[1] + 2 * left[0] + top[-1] + 2) >> 2;
            for (int y = 1; y < nTbS * 2 - 1; y++) {
                fleft[y] = (left[y + 1] + 2 * left[y] + left[y - 1] + 2) >> 2;
            }
            fleft[nTbS * 2 - 1] = left[nTbS * 2 - 1];
            for (int x = 0; x < nTbS * 2 - 1; x++) {
                ftop[x] = (top[x - 1] + 2 * top[x] + top[x + 1] + 2) >> 2;
            }
            ftop[nTbS * 2 - 1] = top[nTbS * 2 - 1];
            memcpy(top - 1, mftop, (nTbS * 2 + 1) * sizeof(int16_t));
            memcpy(left, fleft, nTbS * 2 * sizeof(int16_t));
        }
    }
    VDBG(hevc, "filtering left: ");
    for (int i = 0; i < 2*nTbS; i++) {
        fprintf(vlog_get_stream(), "%x ", left[i]);
    }
    fprintf(vlog_get_stream(), "\n");
    VDBG(hevc, "filtering top: %x", top[-1]);
    for (int i = 0; i < 2*nTbS; i++) {
        fprintf(vlog_get_stream(), "%x ", top[i]);
    }
    fprintf(vlog_get_stream(), "\n");
}

// see 8.4.4.2.7
static void decode_palette_mode(struct hevc_param_set *hps,
                                struct slice_segment_header *slice,
                                struct cu *cu, struct trans_tree *tt, int xCb,
                                int yCb, int cIdx, int nCbSX, int nCbSY,
                                int16_t *recSamples, struct picture *p) {
    int nSubWidth, nSubHeight;
    if (cIdx == 0) {
        nSubWidth = 1;
        nSubHeight = 1;
    }
    else {
        nSubWidth = slice->SubWidthC;
        nSubHeight = slice->SubHeightC;
    }
    for (int y = 0; y < nCbSY; y++) {
        for (int x = 0; x < nCbSX; x++) {
            int xL = cu->pc[xCb][yCb]->palette_transpose_flag ? y * nSubHeight
                                                              : x * nSubWidth;
            int yL = cu->pc[xCb][yCb]->palette_transpose_flag ? x * nSubWidth
                                                              : y * nSubHeight;
            int bIsEscapeSample = 0;
            if (cu->PaletteIndexMap[xCb + xL - xCb][yCb + yL - yCb] ==
                    cu->pc[xCb][yCb]->MaxPaletteIndex &&
                cu->pc[xCb][yCb]->palette_escape_val_present_flag == 1) {
                bIsEscapeSample = 1;
            }
            if (!bIsEscapeSample) {
                recSamples[x + nCbSX * y] =
                    slice->ppe
                        .PredictorPaletteEntries[cIdx]
                                                [cu->PaletteIndexMap[xL][yL]];
            } else if (cu->cu_transquant_bypass_flag == 1) {
                recSamples[x + nCbSX *y] = cu->PaletteEscapeVal[cIdx][xL][yL];
            } else {
                //invoke 8.6.1
                struct quant_pixel qp =
                    quatization_parameters(xCb, yCb, hps, slice, cu, tt, p);
                int qP;
                if (cIdx == 0) {
                    qP = MAX(0, qp.q_y);
                } else if (cIdx == 1) {
                    qP = MAX(0, qp.q_cb);
                } else if (cIdx == 2) {
                    qP = MAX(0, qp.q_cr);
                }
                int bitDepth = (cIdx == 0)
                                   ? (hps->sps->bit_depth_luma_minus8 + 8)
                                   : (hps->sps->bit_depth_chroma_minus8 + 8);
                const int levelScale[] = {40, 45, 51, 57, 64, 72};
                int tmpVal = ((cu->PaletteEscapeVal[cIdx][xL][yL] * levelScale[qP % 6]) << ((qP / 6) + 32)) >> 6;
                recSamples[x + nCbSX*y] = clip3(0, (1 << bitDepth) - 1, tmpVal);
            }
        }
    }
#if 0
    // called in parsing process
    // see 8-79
    int numComps = ( slice->ChromaArrayType == 0 ) ? 1 : 3;
    for (int i = 0; i < CurrentPaletteSize; i++ ) {
        for( cIdx = 0; cIdx < numComps; cIdx++ ) {
            newPredictorPaletteEntries[cIdx][i] = CurrentPaletteEntries[cIdx][i];
        }
    }

    int newPredictorPaletteSize = CurrentPaletteSize;
    for (int i = 0; i < PredictorPaletteSize && newPredictorPaletteSize < PaletteMaxPredictorSize; i++ ){
        if( !PalettePredictorEntryReuseFlags[i]) {
            for( cIdx = 0; cIdx < numComps; cIdx++) {
                newPredictorPaletteEntries[cIdx][newPredictorPaletteSize] =
                    PredictorPaletteEntries[cIdx][i];
            }
            newPredictorPaletteSize++;
        }
    }
    for (cIdx = 0; cIdx < numComps; cIdx++) {
        for (int i = 0; i < newPredictorPaletteSize; i++) {
            PredictorPaletteEntries[cIdx][i] =
                newPredictorPaletteEntries[cIdx][i];
        }
    }
    PredictorPaletteSize = newPredictorPaletteSize;
#endif
}

// 8.4.4.2.1
/**
 * The function performs intra-sample prediction for a given coding unit in a video slice, using
 * neighboring samples and various prediction modes.
 * 
 * @param slice A pointer to the slice segment header structure.
 * @param hps A structure containing the HEVC parameter set information.
 * @param cu A structure representing a coding unit in the HEVC (High Efficiency Video Coding)
 * standard. It contains information about the coding mode, prediction mode, and other parameters for a
 * specific block in the video frame.
 * @param xTbCmp The variable `xTbCmp` represents the x-coordinate of the current coding unit (CU) in
 * the component (luma or chroma) being processed. It is used to calculate the x-coordinate of
 * neighboring CUs for intra-sample prediction.
 * @param yTbCmp The parameter `yTbCmp` represents the vertical position of the current coding unit
 * (CU) in terms of the number of transform blocks (Tb) for the Y component. It is used to calculate
 * the vertical position of neighboring CUs for intra-sample prediction.
 * @param predModeIntra The parameter "predModeIntra" represents the prediction mode for intra coding.
 * It determines the type of intra prediction used for encoding a particular block. The possible values
 * for "predModeIntra" are:
 * @param nTbS The parameter "nTbS" represents the size of the coding unit (CU) in terms of the number
 * of transform blocks (Tb) in each dimension. It is used to determine the number of iterations in the
 * for loops that iterate over the transform blocks in the CU.
 * @param cIdx The parameter "cIdx" represents the chroma component index. It is used to determine the
 * position of the current chroma component within the picture. If cIdx is 0, it means the current
 * component is the luma component (Y). If cIdx is 1 or 2,
 * @param predSamples A pointer to the array where the predicted samples will be stored.
 * @param stride The `stride` parameter represents the number of bytes between the start of each row in
 * the `predSamples` array. It is used to calculate the memory offset for accessing the prediction
 * samples in the `dst` array.
 */
static void intra_sample_prediction(struct slice_segment_header *slice,
                                    struct hevc_param_set *hps, struct cu *cu,
                                    int xTbCmp, int yTbCmp, int predModeIntra,
                                    int nTbS, int cIdx, int16_t *predSamples,
                                    struct picture *p) {
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    int unavaible = 0;
    int8_t unavaibleL[64] = {0}, unavaibleA[65] = {0};
    int8_t *unavaibleT = unavaibleA + 1;
    int16_t left_default[64] = {0};
    int16_t top_default[65] = {0};
    int16_t *top = top_default + 1;
    int16_t *left = left_default;
    int16_t *dst = (cIdx == 0) ? p->Y: (cIdx == 1 ? p->U : p->V);
    int stride = (cIdx == 0) ? p->y_stride : p->uv_stride;

    int xTbY = (cIdx == 0) ? xTbCmp : xTbCmp * slice->SubWidthC;
    int yTbY = (cIdx == 0) ? yTbCmp : yTbCmp * slice->SubHeightC;
    VDBG(hevc, "intra_sample_prediction %d (%d, %d), (%d, %d) %d", predModeIntra,
         xTbCmp, yTbCmp, xTbY, yTbY, nTbS);
    for (int x = -1; x < nTbS * 2; x++) {
        int xNbCmp = xTbCmp + x;
        int yNbCmp = yTbCmp - 1;
        int xNbY = (cIdx == 0) ? xNbCmp : xNbCmp * slice->SubWidthC;
        int yNbY = (cIdx == 0) ? yNbCmp : yNbCmp * slice->SubHeightC;
        bool availableN = process_zscan_order_block_availablity(
            slice, hps, xTbY, yTbY, xNbY, yNbY);

        if (availableN == false || (get_CuPredMode(slice, p, xNbY,yNbY) != MODE_INTRA &&
                                    pps->constrained_intra_pred_flag == 1)) {
            // mark unavaible;
            // VDBG(hevc,
            //      "available top (%d, %d) %d, CuPredMode %d",
            //      xNbCmp, yNbCmp, availableN, get_CuPredMode(ctu, xNbY,yNbY));
            unavaible ++;
            unavaibleT[x] = 1;
        } else {
            // VDBG(hevc, "assign top (%d, %d) %d", xNbCmp, yNbCmp,
            //     dst[xNbCmp + yNbCmp * stride]);
            top[x] = dst[xNbCmp + yNbCmp*stride];
        }
    }
    for (int y = 0; y < nTbS*2; y++) {
        int xNbCmp = xTbCmp - 1;
        int yNbCmp = yTbCmp + y;
        int xNbY = (cIdx == 0) ? xNbCmp : xNbCmp * slice->SubWidthC;
        int yNbY = (cIdx == 0) ? yNbCmp : yNbCmp * slice->SubHeightC;
        bool availableN = process_zscan_order_block_availablity(
            slice, hps, xTbY, yTbY, xNbY, yNbY);
        // VDBG(hevc, "nTbS %d, xNbY yNbY(%d, %d), aval %d", nTbS, xNbY, yNbY,
        //      availableN);
        if (availableN == false ||
            (get_CuPredMode(slice, p, xNbY, yNbY) != MODE_INTRA &&
             pps->constrained_intra_pred_flag == 1)) {
            // mark unavaible;
            // VDBG(hevc, "available left (%d, %d) %d, CuPredMode %d",
            //      xNbY, yNbY, availableN, get_CuPredMode(ctu, xNbY,yNbY));
            unavaible++;
            unavaibleL[y] = 1;
        } else {
            // VDBG(hevc, "assign left (%d, %d) %d", xNbCmp, yNbCmp,
            //         dst[xNbCmp + yNbCmp * stride]);
            left[y] = dst[xNbCmp + yNbCmp * stride];
        }
    }
    VDBG(hevc, "from image left: %d", 2 * nTbS);
    for (int i = 0; i < 2 * nTbS; i++) {
        fprintf(vlog_get_stream(), "%x ", left[i]);
    }
    fprintf(vlog_get_stream(), "\n");
    VDBG(hevc, "from image top: %x", top[-1]);
    for (int i = 0; i < 2 * nTbS; i++) {
        fprintf(vlog_get_stream(), "%x ", top[i]);
    }
    fprintf(vlog_get_stream(), "\n");
    if (unavaible > 0 && predModeIntra != INTRA_SINGLE) {
        // 8.4.4.2.2 invoked
        reference_sample_substitution(sps, left, top, nTbS, cIdx, unavaible,
                                      unavaibleL, unavaibleT);
    }

    if (sps->sps_range_ext.intra_smoothing_disabled_flag == 0 &&
        (cIdx == 0 || slice->ChromaArrayType == 3)) {
        // 8.4.4.2.3 invoked
        filtering_neighbouring_samples(sps, predModeIntra, cIdx, nTbS, left, top);
    }
    if (predModeIntra == INTRA_PLANAR) {
        hevc_intra_planar((uint16_t *)predSamples, (uint16_t *)left,
                          (uint16_t *)top, nTbS, nTbS);
    } else if (predModeIntra == INTRA_DC) {
        hevc_intra_DC((uint16_t *)predSamples, (uint16_t *)left,
                      (uint16_t *)top, nTbS, nTbS, cIdx,
                      sps->sps_scc_ext.intra_boundary_filtering_disabled_flag);
    } else {
        int disableIntraBoundaryFilter = 0;
        if (sps->sps_scc_ext.intra_boundary_filtering_disabled_flag==1 ) {
            disableIntraBoundaryFilter= 1;
        } else if (sps->sps_range_ext.implicit_rdpcm_enabled_flag == 1 &&
            cu->cu_transquant_bypass_flag == 1) {
            disableIntraBoundaryFilter = 1;
        }
        hevc_intra_angular((uint16_t *)predSamples, (uint16_t *)left,
                           (uint16_t *)top, nTbS, nTbS, cIdx, predModeIntra,
                           disableIntraBoundaryFilter,
                           sps->bit_depth_luma_minus8 + 8);
    }
#if 1
    VDBG(hevc, "predication %d (%d, %d)-(%d, %d) %d", nTbS, xTbCmp, yTbCmp,
         xTbY, yTbY, predModeIntra);
    for (int j = 0; j < nTbS; j++) {
        for (int i = 0; i < nTbS; i++) {
            fprintf(vlog_get_stream(), " %x", predSamples[i + j * nTbS]);
        }
        fprintf(vlog_get_stream(), "\n");
    }
#endif
}

// 8.4.4.1, resSamplesRec is the reconstructed residual samples
void decode_intra_block(struct slice_segment_header *slice, struct cu *cu,
                        struct hevc_param_set *hps, int xTb0, int yTb0,
                        int log2TrafoSize, int trafoDepth, int predModeIntra,
                        int cIdx, int controlParaAct, struct picture *p) {
    int xTbY, yTbY;
    xTbY = (cIdx == 0) ? xTb0 : xTb0 * slice->SubWidthC;
    yTbY = (cIdx == 0) ? yTb0 : yTb0 * slice->SubHeightC;
    int splitFlag = 0;
    int split_transform_flag = get_split_transform_flag(slice, p, xTbY, yTbY, trafoDepth);
    if (cIdx == 0) {
        splitFlag = split_transform_flag;
    } else if (split_transform_flag == 1 && log2TrafoSize > 2) {
        splitFlag = 1;
    }

    VDBG(hevc, "(%d, %d) splitFlag %d, cIdx %d, trafoDepth %d, log2TrafoSize %d", xTbY,
         yTbY, splitFlag, cIdx, trafoDepth, log2TrafoSize);
    if (splitFlag == 1) {
        int xTb1, yTb1;
        if (cIdx == 0 || slice->ChromaArrayType != 2) {
            xTb1 = xTb0 + (1 << (log2TrafoSize - 1));
            yTb1 = yTb0 + (1 << (log2TrafoSize - 1));
        } else {
            xTb1 = xTb0 + (1 << (log2TrafoSize - 1));
            yTb1 = yTb0 + (2 << (log2TrafoSize - 1));
        }
        decode_intra_block(slice, cu, hps, xTb0, yTb0, log2TrafoSize - 1, trafoDepth + 1,
                            predModeIntra, cIdx, controlParaAct, p);
        decode_intra_block(slice, cu, hps, xTb1, yTb0, log2TrafoSize - 1,
                           trafoDepth + 1, predModeIntra, cIdx, controlParaAct,
                           p);
        decode_intra_block(slice, cu, hps, xTb0, yTb1, log2TrafoSize - 1,
                           trafoDepth + 1, predModeIntra, cIdx, controlParaAct,
                           p);
        decode_intra_block(slice, cu, hps, xTb1, yTb1, log2TrafoSize - 1,
                           trafoDepth + 1, predModeIntra, cIdx, controlParaAct,
                           p);
    } else {
        for (int blkIdx = 0; blkIdx <=((cIdx > 0 && slice->ChromaArrayType == 2) ? 1 : 0); blkIdx++) {
            int nTbS = 1 << log2TrafoSize;
            int yTbOffset = blkIdx * nTbS;
            int yTbOffsetY = yTbOffset * slice->SubHeightC;
            int residualDpcm = 0;
            int16_t predSamples[64 * 64];
            int16_t resSamples[32*32];
            struct trans_tree *tt = &cu->tt;
            int transform_skip_flag =
                tt->transform_skip_flag[cIdx][xTbY-tt->xT0][yTbY + yTbOffsetY- tt->yT0];

            VDBG(hevc, "controlParaAct %d, predModeIntra %d, blkIdx %d", controlParaAct, predModeIntra, blkIdx);
            // step 4
            if (controlParaAct != 2) {
                if (hps->sps->sps_range_ext.implicit_rdpcm_enabled_flag == 1 &&
                    (transform_skip_flag == 1 ||
                    cu->cu_transquant_bypass_flag == 1) &&
                    (predModeIntra == 10 || predModeIntra == 26)) {
                    residualDpcm = 1;
                }
            }

            if (controlParaAct != 1) {
                // step 5, invoke 8.4.4.2.1
                intra_sample_prediction(slice, hps, cu, xTb0, yTb0 + yTbOffsetY,
                                        predModeIntra, nTbS, cIdx, predSamples,
                                        p);
            }
            if (controlParaAct != 2) {
                // step 6,  8.6.2
                scale_and_transform(cu, transform_skip_flag,
                                hps, slice, &cu->tt, xTbY, yTbY + yTbOffsetY,
                                trafoDepth, cIdx, nTbS, resSamples, p);
                
                //step 7
                VDBG(hevc, "residualDpcm %d", residualDpcm);
                if (residualDpcm == 1) {
                    // 8.6.5 directional residual modification
                    residual_modification_transform_bypass(predModeIntra / 26, nTbS,
                                                        resSamples);
                }
                //step 8
                if (hps->pps->pps_range_ext.cross_component_prediction_enabled_flag == 1 &&
                    slice->ChromaArrayType == 3 && cIdx != 0) {
                    //  8.6.6 residual modification process
                    residual_modification_transform_cross_prediction(
                        hps->sps, cu, xTbY, yTbY, nTbS, cIdx, resSamples,
                        resSamples);
                }
            }
            // step 9
            int16_t *dst = (cIdx == 0) ? p->Y : (cIdx == 1 ? p->U : p->V);
            int stride = (cIdx == 0) ? p->y_stride : p->uv_stride;
            int xTbInCb, yTbInCb;
            if (controlParaAct != 0) {
                int nCbS = 1 << (log2TrafoSize + trafoDepth);
                xTbInCb = xTb0 & (nCbS - 1);
                yTbInCb = yTb0 & (nCbS - 1);
            }
            // step 10
            if (controlParaAct == 2) {
                for (int y = 0; y < nTbS; y++) {
                    for (int x = 0; x < nTbS; y++) {
                        resSamples[x + nTbS * y] =
                            dst[x + xTbInCb + stride * (y + yTbInCb)];
                    }
                }
            }
            // step 11
            if (controlParaAct == 1) {
                for (int y = 0; y < nTbS; y++) {
                    for (int x = 0; x < nTbS; y++) {
                        // TBD, did not find resSamplesArray declare
                        resSamples[x + xTbInCb + nTbS * (y + yTbInCb)] =
                            resSamples[x + nTbS * y];
                    }
                }
            } else {
                // in 8.6.7 ( xTb0, yTb0 + yTbOffset )
                // int16_t recSamples[64*64]; // reconstructed sample array as output
                //add predSamples and resSamples to recSamples
                construct_pic_pior_to_filtering(hps->sps, xTb0, yTb0+yTbOffset, nTbS, nTbS, cIdx, predSamples, resSamples, dst, stride);
                VDBG(hevc, "construct_pic_pior_to_filtering %d ", nTbS);
                for (int j = 0; j < nTbS; j++) {
                    for (int i = 0; i < nTbS; i++) {
                        fprintf(vlog_get_stream(), " %x",
                                dst[xTb0 + i + (yTb0 + yTbOffset + j) * stride]);
                    }
                    fprintf(vlog_get_stream(), "\n");
                }
            }
        }
    }
}

/* 8.4.2 and (I.8.4.2) */
static int8_t process_luma_intra_prediction_mode(
    struct slice_segment_header *slice, struct cu *cu,
    struct hevc_param_set *hps, int xPb, int yPb, int mpm_idx,
    int prev_intra_luma_pred_flag, int rem_intra_luma_pred_mode, struct picture *p) {
    int candModeList[3] = {0};
    int candIntraPredModeA;
    int candIntraPredModeB;
    int8_t IntraPredModeY;
#ifdef ENABLE_3D
    int DimFlag = !cu->intra_ext[xPb][yPb].no_dim_flag;
    VDBG(hevc, "skip_intra_flag (%d, %d) %d, DimFlag %d", xPb, yPb,
         cu->skip_intra_flag[xPb][yPb], DimFlag);
    assert(cu->skip_intra_flag[xPb][yPb] == 0 && DimFlag == 0);
    if (cu->skip_intra_flag[xPb][yPb] == 1) {
        if (cu->ext[xPb][yPb].skip_intra_mode_idx == 0) {
            cu->IntraPredModeY[xPb][yPb] = INTRA_ANGULAR26;
        } else if (cu->ext[xPb][yPb].skip_intra_mode_idx == 1) {
            cu->IntraPredModeY[xPb][yPb] = INTRA_ANGULAR10;
        } else {
            cu->IntraPredModeY[xPb][yPb] = INTRA_SINGLE;
        }
    } else if (DimFlag == 1) {
        if (cu->intra_ext[xPb][yPb].depth_intra_mode_idx_flag == 0) {
            cu->IntraPredModeY[xPb][yPb] = INTRA_WEDGE;
        } else if (cu->intra_ext[xPb][yPb].depth_intra_mode_idx_flag == 1) {
            cu->IntraPredModeY[xPb][yPb] = INTRA_CONTOUR;
        }
    } else if (cu->skip_intra_flag[xPb][yPb] == 0 && DimFlag == 0) {
#endif
        int xNbA = xPb - 1;
        int yNbA = yPb;
        int xNbB = xPb;
        int yNbB = yPb - 1;

        /*step 2*/
        bool availableA = process_zscan_order_block_availablity(
            slice, hps, xPb, yPb, xNbA, yNbA);
        VDBG(hevc, "availableA (%d, %d), %d get_IntraPredModeY %d, cuPredMode %d, pcm_flag %d", xNbA, yNbA,
             availableA, get_IntraPredModeY(slice, p, xNbA, yNbA), get_CuPredMode(slice, p, xNbA, yNbA),
             get_pcm_flag(slice, p, xNbA, yNbA));
        if (availableA == false) {
            candIntraPredModeA = INTRA_DC;
        } else if (get_CuPredMode(slice, p, xNbA, yNbA) != MODE_INTRA ||
                   get_pcm_flag(slice, p, xNbA, yNbA) == 1) {
            candIntraPredModeA = INTRA_DC;
        } else if (get_IntraPredModeY(slice, p, xNbA, yNbA) > 34) {
            candIntraPredModeA = INTRA_DC;
        } else {
            candIntraPredModeA = get_IntraPredModeY(slice, p, xNbA, yNbA);
        }

        bool availableB = process_zscan_order_block_availablity(
            slice, hps, xPb, yPb, xNbB, yNbB);
        VDBG(hevc, "availableB (%d, %d), %d", xNbB, yNbB, availableB);

        if (availableB == false) {
            candIntraPredModeB = INTRA_DC;
        } else if (get_CuPredMode(slice, p, xNbB, yNbB) != MODE_INTRA ||
                   get_pcm_flag(slice, p, xNbB, yNbB) == 1) {
            candIntraPredModeB = INTRA_DC;
        } else if (yPb - 1 <
                   ((yPb >> slice->CtbLog2SizeY) << slice->CtbLog2SizeY)) {
            candIntraPredModeB = INTRA_DC;
        } else {
            candIntraPredModeB = get_IntraPredModeY(slice, p, xNbB, yNbB);
        }
#ifdef ENABLE_3D
    }
#endif
    VDBG(hevc, "candIntraPredModeA %d, candIntraPredModeB %d (%d, %d)",
         candIntraPredModeA, candIntraPredModeB, xPb, yPb);
    /*step 3*/
    if (candIntraPredModeB == candIntraPredModeA) {
        if (candIntraPredModeA < 2) {
            /*see (8-21)(I-62)*/
            candModeList[0] = INTRA_PLANAR;
            /*see (8-22)(I-63)*/
            candModeList[1] = INTRA_DC;
            /*see (8-23)(I-64)*/
            candModeList[2] = INTRA_ANGULAR26;
        } else {
            /*see (8-24)(I-65)*/
            candModeList[0] = candIntraPredModeA;
            /*see (8-25)(I-66)*/
            candModeList[1] = 2 + ((candIntraPredModeA + 29) % 32);
            /*see (8-26)(I-67)*/
            candModeList[2] = 2 + ((candIntraPredModeA - 2 + 1) % 32);
        }
    } else {
        /*see (8-27)(I-68)*/
        candModeList[0] = candIntraPredModeA;
        /*see (8-28)(I-69)*/
        candModeList[1] = candIntraPredModeB;
        if (candModeList[0] != INTRA_PLANAR &&
            candModeList[1] != INTRA_PLANAR) {
            candModeList[2] = INTRA_PLANAR;
        } else if (candModeList[0] != INTRA_DC &&
                   candModeList[1] != INTRA_DC) {
            candModeList[2] = INTRA_DC;
        } else {
            candModeList[2] = INTRA_ANGULAR26;
        }
    }
    VDBG(hevc, "candModeList %d, %d, %d", candModeList[0], candModeList[1],
         candModeList[2]);
    /* step 4 */
    if (prev_intra_luma_pred_flag== 1) {
        IntraPredModeY = candModeList[mpm_idx];
    } else {
        if (candModeList[0] > candModeList[1]) {
            /* (8-29)(I-70) */
            swap(&candModeList[0], &candModeList[1]);
        }
        if (candModeList[0] > candModeList[2]) {
            /* (8-30)(I-71) */
            swap(&candModeList[0], &candModeList[2]);
        }
        if (candModeList[1] > candModeList[2]) {
            /* (8-31)(I-72) */
            swap(&candModeList[1], &candModeList[2]);
        }
        IntraPredModeY = rem_intra_luma_pred_mode;
        for (int i = 0; i < 3; i++) {
            if (IntraPredModeY >= candModeList[i]) {
                IntraPredModeY += 1;
            }
        }
    }
    VDBG(hevc, "process_luma_intra_prediction_mode %d", IntraPredModeY);
    return IntraPredModeY;
}

/* 8.4.3
This process is only invoked when ChromaArrayType is not equal to 0.
*/
static int8_t process_chroma_intra_prediction_mode(struct slice_segment_header* slice, struct hevc_param_set *hps,
                                                   int xPb, int yPb,
                                                   int intra_chroma_pred_mode,
                                                   struct picture *p) {
    uint8_t ChromaArrayType = (hps->sps->separate_colour_plane_flag == 1
                                   ? 0
                                   : hps->sps->chroma_format_idc);
    int modeIdx;
    int IntraPredModeY = get_IntraPredModeY(slice, p, xPb, yPb);
    int IntraPredModeC;
    /* table 8-2 */
    if (intra_chroma_pred_mode == 0) {
        switch (IntraPredModeY) {
        case 0:
            modeIdx = 34;
            break;
        default:
            modeIdx = 0;
            break;
        }
    } else if (intra_chroma_pred_mode == 1) {
        switch (IntraPredModeY) {
        case 26:
            modeIdx = 34;
            break;
        default:
            modeIdx = 26;
            break;
        }
    } else if (intra_chroma_pred_mode == 2) {
        switch (IntraPredModeY) {
        case 10:
            modeIdx = 34;
            break;
        default:
            modeIdx = 10;
            break;
        }
    } else if (intra_chroma_pred_mode == 3) {
        switch (IntraPredModeY) {
        case 1:
            modeIdx = 34;
            break;
        default:
            modeIdx = 1;
            break;
        }
    } else if (intra_chroma_pred_mode == 4) {
        modeIdx = IntraPredModeY;
    }

    /* table 8-3 */
    const int modeIdxToIntraPred[] = {
        0,  1,  2,  2,  2,  2,  3,  5,  7,  8,  10, 12, 13, 15, 17, 18, 19, 20,
        21, 22, 23, 23, 24, 24, 25, 25, 26, 27, 27, 28, 28, 29, 29, 30, 31
    };
    if (ChromaArrayType == 2) {
        IntraPredModeC = modeIdxToIntraPred[modeIdx];
    } else {
        IntraPredModeC = modeIdx;
    }
    return IntraPredModeC;
}

// see 8.4.1
static void decode_cu_coded_intra_prediction_mode(
    struct slice_segment_header *slice, struct cu *cu,
    struct hevc_param_set *hps, struct trans_tree *tt,
    int xCb, int yCb, int xBase, int yBase, int log2CbSize, struct picture *p) {
    // invoke 8.6.1
    struct quant_pixel qp = quatization_parameters(xBase, yBase, hps, slice, cu, tt, p);
    int nCbS = 1 << log2CbSize;
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    int16_t recSamples[64*64];
    int16_t resSamplesL[64*64], resSamplesCb[64*64], resSamplesCr[64*64];

    VDBG(hevc, "decode TU in CU %d", nCbS);
    // residual sample arrays resSamplesL, resSamplesCb, and resSamplesCr 
    // store the residual samples of the current coding unit
    if (get_pcm_flag(slice, p, xCb, yCb) == 1) {
        int PcmBitDepthY = sps->pcm->pcm_sample_bit_depth_luma_minus1 + 1;
        int BitDepthY = sps->bit_depth_luma_minus8 + 8;
        for (int j = 0; j < nCbS; j ++) {
            for (int i = 0; i < nCbS; i++) {
                p->Y[xCb + i + (yCb + j) * p->y_stride] = cu->pcm->pcm_sample_luma[(nCbS*j)+i] << (BitDepthY - PcmBitDepthY);
            }
        }
    } else if (!get_pcm_flag(slice, p, xCb, yCb) &&
               cu->palette_mode_flag == 1) {
        //invoke 8.4.4.2.7
        decode_palette_mode(hps, slice, cu, tt, xCb, yCb, 0, nCbS, nCbS, recSamples, p);
        for (int j = 0; j < nCbS; j++) {
            for (int i = 0; i < nCbS; i++) {
                if (cu->pc[xCb][yCb]->palette_transpose_flag == 1) {
                    p->Y[xCb + i+(yCb + j)*p->y_stride] = recSamples[j + i*nCbS];
                } else {
                    p->Y[xCb + i+(yCb + j)*p->y_stride] = recSamples[i + j*nCbS];
                }
            }
        }
    } else if (!get_pcm_flag(slice, p, xCb, yCb) && !cu->palette_mode_flag &&
               cu->IntraSplitFlag == 0) {
        VDBG(hevc, "Decoding luma samples pred %d",
             get_IntraPredModeY(slice, p, xCb, yCb));

        // 8.4.2 invoked for calculate IntraPredModeY, which is precomputed
        // process_luma_intra_prediction_mode(slice, ctu, cu, hps, xCb, yCb);
        if (pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag == 1) {
            //invoke 8.4.4.1 for three component
            decode_intra_block(slice, cu, hps, xCb, yCb, log2CbSize, 0,
                               get_IntraPredModeY(slice, p, xCb, yCb), 0, 1, p);
            decode_intra_block(slice, cu, hps, xCb, yCb, log2CbSize, 0,
                               get_IntraPredModeC(slice, p, xCb, yCb), 1, 1, p);
            decode_intra_block(slice, cu, hps, xCb, yCb, log2CbSize, 0,
                               get_IntraPredModeC(slice, p, xCb, yCb), 2, 1, p);
            // invoke 8.6.8
            // which is only valid for ChromaArrayType == 3
            //TBD
            assert(0);
        }
        //invoke 8.4.4.1
        decode_intra_block(
            slice, cu, hps, xCb, yCb, log2CbSize, 0,
            get_IntraPredModeY(slice, p, xCb, yCb), 0,
            pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag ? 2 : 0,
            p);

        // for (int j = 0; j < nCbS; j++) {
        //     for (int i = 0; i < nCbS; i++) {
        //         fprintf(vlog_get_stream(), " %d",
        //                 resSamplesL[i + j * 64]);
        //     }
        //     fprintf(vlog_get_stream(), "\n");
        // }
    } else if (!get_pcm_flag(slice, p, xCb, yCb) && !cu->palette_mode_flag &&
               cu->IntraSplitFlag == 1) {
        VDBG(hevc, "decode TU with IntraSplitFlag");
        for (int blkIdx = 0; blkIdx < 4; blkIdx ++) {
            int xPb = xCb + (nCbS >> 1) * (blkIdx % 2);
            int yPb = yCb + (nCbS >> 1) * (blkIdx / 2);
            //invoke 8.4.2 for calculate IntraPredModeY, which is precomputed
            // process_luma_intra_prediction_mode(slice, ctu, cu, hps, xPb, yPb);
            if (pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag == 1) {
                //invoke 8.4.4.1
                decode_intra_block(slice, cu, hps, xPb, yPb,
                                   log2CbSize - 1, 1,
                                   get_IntraPredModeY(slice, p, xPb, yPb), 0, 1, p);
                decode_intra_block(slice, cu, hps, xPb, yPb, log2CbSize - 1,
                                   1, get_IntraPredModeC(slice, p, xPb, yPb), 1, 1, p);
                decode_intra_block(slice, cu, hps, xPb, yPb, log2CbSize - 1,
                                   1, get_IntraPredModeC(slice, p, xPb, yPb), 2, 1, p);
                // invoke 8.6.8
                // which is only valid for ChromaArrayType == 3
                // TBD
                assert(0);
            }
            decode_intra_block(
                slice, cu, hps, xPb, yPb, log2CbSize - 1, 1,
                get_IntraPredModeY(slice, p, xPb, yPb), 0,
                pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag ? 2 : 0, p);
        }
    }
    if (slice->ChromaArrayType != 0) {
        int log2CbSizeC = log2CbSize - (slice->ChromaArrayType == 3 ? 0 : 1);
        int BitDepthY = sps->bit_depth_luma_minus8 + 8;
        int BitDepthC = sps->bit_depth_chroma_minus8 + 8;
        int pcm_flag = get_pcm_flag(slice, p, xCb, yCb);
        if (pcm_flag == 1) {
            int PcmBitDepthY = sps->pcm->pcm_sample_bit_depth_luma_minus1 + 1;
            int PcmBitDepthC = sps->pcm->pcm_sample_bit_depth_chroma_minus1 + 1;
            for (int i = 0; i < nCbS / slice->SubWidthC - 1; i++) {
                for (int j = 0; j < nCbS / slice->SubHeightC - 1; j++) {
                    p->U[xCb / slice->SubWidthC + i+ (yCb / slice->SubHeightC + j)*p->uv_stride] =
                        cu->pcm->pcm_sample_chroma[(nCbS / slice->SubWidthC * j) +i] << (BitDepthC - PcmBitDepthC);
                    p->V[xCb / slice->SubWidthC + i+(yCb / slice->SubHeightC + j)*p->uv_stride] =
                        cu->pcm->pcm_sample_chroma[(nCbS / slice->SubWidthC * ( j + nCbS / slice->SubHeightC)) +i] << (BitDepthC - PcmBitDepthC);
                }
            }
        } else if (!pcm_flag && cu->palette_mode_flag == 1) {
            int nCbsX = (cu->pc[xCb][yCb]->palette_transpose_flag == 1)
                            ? (nCbS / slice->SubHeightC)
                            : (nCbS / slice->SubWidthC);
            int nCbsY = (cu->pc[xCb][yCb]->palette_transpose_flag == 1)
                            ? (nCbS / slice->SubWidthC)
                            : (nCbS / slice->SubHeightC);
            //invoke 8.4.4.2.7
            decode_palette_mode(hps, slice, cu, tt, xCb, yCb, 1, nCbsX, nCbsY, recSamples, p);
            if (cu->pc[xCb][yCb]->palette_transpose_flag == 1) {
                for (int y = 0; y < nCbS / slice->SubHeightC; y++) {
                    for (int x = 0; x < nCbS / slice->SubWidthC; x++) {
                        p->V[xCb / slice->SubWidthC + x+(yCb / slice->SubHeightC + y)*p->uv_stride] =
                               recSamples[y + x * nCbS];
                    }
                }
            } else {
                for (int y = 0; y < nCbS / slice->SubHeightC; y++) {
                    for (int x = 0; x < nCbS / slice->SubWidthC; x++) {
                        p->V[xCb / slice->SubWidthC + x+ (yCb / slice->SubHeightC + y)*p->uv_stride] =
                            recSamples[x + nCbS * y];
                    }
                }
            }
        } else if (!pcm_flag && !cu->palette_mode_flag &&
                   (cu->IntraSplitFlag == 0 || slice->ChromaArrayType != 3)) {
            // invoke 8.4.3
            VDBG(hevc, "Decoding chroma Cb samples");
            // process_chroma_intra_prediction_mode(slice, cu, hps, xCb, yCb);
            // invoke 8.4.4.1
            decode_intra_block(
                slice, cu, hps, xCb / slice->SubWidthC,
                yCb / slice->SubHeightC, log2CbSizeC, 0, get_IntraPredModeC(slice, p,xCb, yCb), 1,
                pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag ? 2 : 0, p);

            VDBG(hevc, "Decoding chroma Cr samples");
            decode_intra_block(
                slice, cu, hps, xCb / slice->SubWidthC,
                yCb / slice->SubHeightC, log2CbSizeC, 0, get_IntraPredModeC(slice, p, xCb, yCb), 2,
                pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag ? 2 : 0, p);

        } else if (!pcm_flag && !cu->palette_mode_flag &&
                   (cu->IntraSplitFlag == 1 && slice->ChromaArrayType == 3)) {
            for (int blkIdx = 0; blkIdx < 4; blkIdx++) {
                int xPb = xCb + (nCbS >> 1) * (blkIdx % 2);
                int yPb = yCb + (nCbS >> 1) * (blkIdx / 2);
                // invoke 8.4.3
                // process_chroma_intra_prediction_mode(slice, cu, hps, xPb, yPb);
                // invoke 8.4.4.1
                decode_intra_block(
                    slice, cu, hps, xPb, yPb, log2CbSizeC - 1, 1,
                    get_IntraPredModeC(slice, p, xPb, yPb), 2,
                    pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag ? 2 : 0,
                    p);
                decode_intra_block(
                    slice, cu, hps, xPb, yPb, log2CbSizeC - 1, 1,
                    get_IntraPredModeC(slice, p, xPb, yPb), 2,
                    pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag?2:0,
                    p);
            }
        }
    }
}

//9.3.2.3 initialization process for palette predictor entries
static void
init_palette_predictor_entries(struct slice_segment_header *slice, struct pps *pps, struct sps *sps) {
    int numComps = (slice->ChromaArrayType == 0) ? 1 : 3; //9-8
    if (pps->pps_scc_ext.pps_palette_predictor_initializers_present_flag) {
        slice->ppe.PredictorPaletteSize = pps->pps_scc_ext.pps_num_palette_predictor_initializers;
        //see (9-9)
        for (int comp = 0; comp < numComps; comp++) {
            for (int i = 0; i < slice->ppe.PredictorPaletteSize; i++ ) 
            slice->ppe.PredictorPaletteEntries[comp][i] = pps->pps_scc_ext.pps_palette_predictor_initializer[comp][i];
        }
    } else if (sps->sps_scc_ext.sps_palette_predictor_initializers_present_flag) {
        slice->ppe.PredictorPaletteSize = sps->sps_scc_ext.sps_num_palette_predictor_initializers_minus1 + 1;
        //see (9-9)
        for (int comp = 0; comp < numComps; comp++) {
            for (int i = 0; i < slice->ppe.PredictorPaletteSize; i++ ) 
            slice->ppe.PredictorPaletteEntries[comp][i] = sps->sps_scc_ext.sps_palette_predictor_initializer[comp][i];
        }
    } else {
        slice->ppe.PredictorPaletteSize = 0;
    }
}


static void
parse_chroma_qp_offset(cabac_dec *d, struct slice_segment_header *slice,
                    struct pps *pps, struct cu *cu, struct chroma_qp_offset *qpoff)
{
    VDBG(hevc,
         "cu_chroma_qp_offset_enabled_flag %d, "
         "IsCuChromaQpOffsetCoded %d",
         slice->cu_chroma_qp_offset_enabled_flag,
         slice->IsCuChromaQpOffsetCoded);
    if (slice->cu_chroma_qp_offset_enabled_flag && !slice->IsCuChromaQpOffsetCoded) {
        qpoff->cu_chroma_qp_offset_flag =
            CABAC(d, CTX_TYPE_CHROMA_QP_OFFSET_CU_CHROME_QP_OFFSET_FLAG);
        slice->IsCuChromaQpOffsetCoded = 1;
        if (qpoff->cu_chroma_qp_offset_flag && pps->pps_range_ext.chroma_qp_offset_list_len_minus1 > 0) {
            qpoff->cu_chroma_qp_offset_idx =
                CABAC(d, CTX_TYPE_CHROMA_QP_OFFSET_CU_CHROME_QP_OFFSET_IDX);
            VDBG(hevc, "cu_chroma_qp_offset_idx %d",
                 qpoff->cu_chroma_qp_offset_idx);
            assert(qpoff->cu_chroma_qp_offset_idx <= pps->pps_range_ext.chroma_qp_offset_list_len_minus1);
        } else {
            qpoff->cu_chroma_qp_offset_idx = 0;
        }
        if (qpoff->cu_chroma_qp_offset_flag) {
            //see (7-87)
            cu->CuQpOffsetCb = pps->pps_range_ext.cb_qp_offset_list[qpoff->cu_chroma_qp_offset_idx];
            //see (7-88)
            cu->CuQpOffsetCr = pps->pps_range_ext.cr_qp_offset_list[qpoff->cu_chroma_qp_offset_idx];
        } else {
            cu->CuQpOffsetCb = 0;
            cu->CuQpOffsetCr = 0;
        }
    }
}

//also see 7.3.8.13 Palette syntax
static void parse_palette_coding(cabac_dec *d, struct palette_coding *pc,
                                 struct cu *cu,
                                 struct slice_segment_header *slice,
                                 struct pps *pps, struct sps *sps, int x0,
                                 int y0, int nCbS) {
    int numComps = (slice->ChromaArrayType == 0) ? 1 : 3;
    VDBG(hevc, "parse_palette_coding");

    int palettePredictionFinished = 0;
    int NumPredictedPaletteEntries = 0;
    int PredictorPaletteSize = slice->ppe.PredictorPaletteSize;

    for (int predictorEntryIdx = 0; predictorEntryIdx < PredictorPaletteSize &&
        !palettePredictionFinished && NumPredictedPaletteEntries < sps->sps_scc_ext.palette_max_size;
        predictorEntryIdx++) {
        int palette_predictor_run = CABAC_BP(d);
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
    if (NumPredictedPaletteEntries < (int)sps->sps_scc_ext.palette_max_size) {
        pc->num_signalled_palette_entries = CABAC_BP(d);
    }

    int CurrentPaletteEntries[3][128];
    int newPredictorPaletteEntries[3][128];
    //7-35
    slice->ppe.PaletteMaxPredictorSize =
        sps->sps_scc_ext.palette_max_size +
        sps->sps_scc_ext.delta_palette_max_predictor_size;

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
        for(uint32_t i = 0; i < pc->num_signalled_palette_entries; i++ ) {
            pc->new_palette_entries[cIdx][i] = CABAC_BP(d);
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
                newPredictorPaletteEntries[cIdx][newPredictorPaletteSize] =
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

    if (CurrentPaletteSize != 0) {
        pc->palette_escape_val_present_flag = CABAC_BP(d);
    }

    pc->MaxPaletteIndex = CurrentPaletteSize - 1 + pc->palette_escape_val_present_flag;

    if (pc->MaxPaletteIndex > 0) {
        pc->num_palette_indices_minus1 =
            CABAC(d, CTX_TYPE_PALETTE_CODING_COPY_ABOVE_PALLETTE_INDICES);
        int adjust = 0;
        for (uint32_t i = 0; i <= pc->num_palette_indices_minus1; i++ ) {
            if (pc->MaxPaletteIndex - adjust > 0) {
                //see 9.3.3.13 binarization for palette_index_idx
                int palette_idx_idc = CABAC_TB(d, pc->MaxPaletteIndex);
                pc->PaletteIndexIdc[i] = palette_idx_idc;
            }
            adjust = 1;
        }
        pc->copy_above_indices_for_final_run_flag =
            CABAC(d, CTX_TYPE_PALETTE_CODING_COPY_ABOVE_INDICES_FOR_FINAL_RUN);
        pc->palette_transpose_flag =
            CABAC(d, CTX_TYPE_PALETTE_CODING_PALETTE_TRANSPOSE_FLAG);
    }
    if (pc->palette_escape_val_present_flag) {
        parse_delta_qp(d, slice, pps);
        if (!cu->cu_transquant_bypass_flag) {
            struct chroma_qp_offset *qpoff = calloc(1, sizeof(*qpoff));
            parse_chroma_qp_offset(d, slice, pps, cu, qpoff);
        }
    }
    int remainingNumIndices = pc->num_palette_indices_minus1 + 1;
    int PaletteScanPos = 0;
    int CurrPaletteIndex;
    int runPos;
    int log2BlockSize = log2floor(nCbS);
    int xC, yC, xR, yR, xcPrev, ycPrev;
    uint8_t CopyAboveIndicesFlag[64][64] = {0};

    while (PaletteScanPos < nCbS * nCbS ) {
        xC = x0 + slice->ScanOrder[log2BlockSize][3][PaletteScanPos].x;
        yC = y0 + slice->ScanOrder[log2BlockSize][3][PaletteScanPos].y;
        if (PaletteScanPos > 0) {
            xcPrev = x0 + slice->ScanOrder[log2BlockSize][3][PaletteScanPos - 1].x;
            ycPrev = y0 + slice->ScanOrder[log2BlockSize][3][PaletteScanPos - 1].y;
        }
        int PaletteRunMinus1 = nCbS * nCbS - PaletteScanPos - 1;
        int RunToEnd = 1;
        CopyAboveIndicesFlag[xC][yC] = 0;
        if (pc->MaxPaletteIndex > 0) {
            if (PaletteScanPos >= nCbS && CopyAboveIndicesFlag[xcPrev][ycPrev] == 0) {
                if (remainingNumIndices > 0 && PaletteScanPos < nCbS * nCbS - 1) {
                    uint8_t copy_above_palette_indices_flag = CABAC(
                        d, CTX_TYPE_PALETTE_CODING_COPY_ABOVE_PALLETTE_INDICES);
                    CopyAboveIndicesFlag[xC][yC] = copy_above_palette_indices_flag;
                } else {
                    if ( PaletteScanPos == nCbS * nCbS - 1 && remainingNumIndices > 0 ) {
                        CopyAboveIndicesFlag[xC][yC] = 0;
                    } else {
                        CopyAboveIndicesFlag[xC][yC] = 1;
                    }
                }
            }
        }

        if (CopyAboveIndicesFlag[xC][yC] == 0) {
            int currNumIndices = pc->num_palette_indices_minus1 + 1 - remainingNumIndices;
            CurrPaletteIndex = pc->PaletteIndexIdc[currNumIndices];
        }
        //7-83
        int adjustedRefPaletteIndex = pc->MaxPaletteIndex + 1;
        int log2BlkSize = log2floor(nCbS) - 2;
        if (PaletteScanPos > 0) {
            int xcPrev =
                x0 + slice->ScanOrder[log2BlkSize][3][PaletteScanPos - 1].x;
            int ycPrev =
                y0 + slice->ScanOrder[log2BlkSize][3][PaletteScanPos - 1].y;
            if (CopyAboveIndicesFlag[xcPrev][ycPrev] == 0) {
                adjustedRefPaletteIndex = cu->PaletteIndexMap[xcPrev-x0][ycPrev-y0];
            } else {
                adjustedRefPaletteIndex = cu->PaletteIndexMap[xC-x0][yC - 1-y0];
            }
        }
        if (CopyAboveIndicesFlag[xC][yC] == 0) {
            if (CurrPaletteIndex >= adjustedRefPaletteIndex) {
                CurrPaletteIndex++;
            }
        }
        if (pc->MaxPaletteIndex > 0) {
            if (CopyAboveIndicesFlag[xC][yC] == 0) {
                remainingNumIndices -= 1;
            }
            int palette_run_prefix, palette_run_suffix;
            if (remainingNumIndices > 0 || CopyAboveIndicesFlag[xC][yC] != pc->copy_above_indices_for_final_run_flag) {
                int PaletteMaxRunMinus1 = nCbS * nCbS - PaletteScanPos -1 -remainingNumIndices - pc->copy_above_indices_for_final_run_flag;
                RunToEnd = 0;
                if (PaletteMaxRunMinus1 > 0) {
                    palette_run_prefix =
                        CABAC(d, CTX_TYPE_PALETTE_CODING_PALETTE_RUN_PREFIX);
                    PaletteRunMinus1 = palette_run_prefix;
                    if ((palette_run_prefix > 1) && (PaletteMaxRunMinus1 !=
                        (1 << (palette_run_prefix - 1)))) {
                        palette_run_suffix = CABAC_BP(d);
                        PaletteRunMinus1 = (1 << (palette_run_prefix - 1)) +
                                           palette_run_suffix;
                    }
                } else {
                    PaletteRunMinus1 = 0;
                }
            }
        }
        runPos = 0;
        while (runPos <= PaletteRunMinus1) {
            xR = x0 + slice->ScanOrder[log2BlockSize][3][PaletteScanPos].x;
            yR = y0 + slice->ScanOrder[log2BlockSize][3][PaletteScanPos].y;
            if (CopyAboveIndicesFlag[xC][yC] == 0 ) {
                CopyAboveIndicesFlag[xR][yR] = 0;
                cu->PaletteIndexMap[xR-x0][yR-y0] = CurrPaletteIndex;
            } else {
                CopyAboveIndicesFlag[xR][yR] = 1;
                cu->PaletteIndexMap[xR-x0][yR-y0] = cu->PaletteIndexMap[xR-x0][yR - 1-y0];
            }
            runPos ++;
            PaletteScanPos ++;
        }
    }
    if (pc->palette_escape_val_present_flag) {
        for (int cIdx = 0; cIdx < numComps; cIdx++) {
            for (int sPos = 0; sPos < nCbS * nCbS; sPos++) {
                xC = x0 + slice->ScanOrder[log2BlockSize][3][sPos].x;
                yC = y0 + slice->ScanOrder[log2BlockSize][3][sPos].y;
                if (cu->PaletteIndexMap[xC-x0][yC-y0] == pc->MaxPaletteIndex ) {
                    if (cIdx == 0 || (xC % 2 == 0 && yC % 2 == 0 &&
                        slice->ChromaArrayType == 1 ) || (xC % 2 == 0 &&
                        !pc->palette_transpose_flag && slice->ChromaArrayType == 2 ) ||
                        (yC % 2 == 0 && pc->palette_transpose_flag &&
                        slice->ChromaArrayType == 2 ) || slice->ChromaArrayType == 3 ) {
                        int palette_escape_val = CABAC_FL(
                            d, ((cIdx == 0) ? (sps->bit_depth_luma_minus8 + 8) : (sps->bit_depth_chroma_minus8 +8)));
                        // quantized escape-coded sample value

                        cu->PaletteEscapeVal[cIdx][xC-x0][yC-y0] = palette_escape_val;
                    }
                }
            }
        }
    }
}

//see 9.3.4.2.3
int ctxOffset, ctxShift;
static int ctx_for_last_sig_coeff_prefix(int ctx_idx, int binIdx) {
    // int ctxOffset, ctxShift;
    // if (cIdx == 0) {
    //     ctxOffset = 3 * (log2TrafoSize - 2) + ((log2TrafoSize - 1) >> 2);
    //     ctxShift = (log2TrafoSize + 1) >> 2;
    // } else {
    //     ctxOffset = 15;
    //     ctxShift = log2TrafoSize - 2;
    // }
    int ctxInc = (binIdx >>ctxShift) + ctxOffset;
    return ctx_idx + ctxInc;
}

static int ctx_for_sig_coeff_flag(struct cu *cu,
                                  struct slice_segment_header *slice,
                                  struct sps *sps, int log2TrafoSize, int cIdx,
                                  int transform_skip_flag, int scanIdx, int x0,
                                  int y0, int xC, int yC,
                                  int coded_sub_block_flag[8][8]) {
    // see 9.3.4.2.5

    static const int ctxIdMap[15] = {0, 1, 4, 5, 2, 3, 4, 5,
                                     6, 6, 8, 8, 7, 7, 8};
    int sigCtx;
    if (sps->sps_range_ext.transform_skip_context_enabled_flag &&
        (transform_skip_flag == 1 ||
         cu->cu_transquant_bypass_flag == 1)) {
        sigCtx = (cIdx == 0) ? 42 : 16;
    } else if (log2TrafoSize == 2) {
        sigCtx = ctxIdMap[(yC << 2) + xC];
    } else if (xC + yC == 0) {
        sigCtx = 0;
    } else {
        int xS, yS;
        xS = xC >> 2;
        yS = yC >> 2;
        int prevCsbf = 0;
        if (xS < (1 << (log2TrafoSize - 2)) - 1) {
            prevCsbf += coded_sub_block_flag[xS+1][yS];
        }
        if (yS < (1 << (log2TrafoSize - 2)) - 1) {
            prevCsbf += (coded_sub_block_flag[xS][yS+1] << 1);
        }
        // VDBG(hevc, "xS yS(%d, %d) %d %d, prevCsbf %d", xS, yS,
        //      coded_sub_block_flag[xS+1][yS],
        //      coded_sub_block_flag[xS][yS+1], prevCsbf);
        int xP = xC & 3;
        int yP = yC & 3;
        assert(prevCsbf < 4);
        if (prevCsbf == 0) {
            sigCtx = (xP + yP == 0) ? 2 : (xP + yP < 3) ? 1 : 0;
        } else if (prevCsbf == 1) {
            sigCtx = (yP == 0) ? 2 : (yP == 1) ? 1 : 0;
        } else if (prevCsbf == 2) {
            sigCtx = (xP == 0) ? 2 : (xP == 1) ? 1 : 0;
        } else {
            sigCtx = 2;
        }
        if (cIdx == 0) {
            if (xS + yS > 0) {
                sigCtx += 3;
            }
            if (log2TrafoSize == 3) {
                sigCtx += (scanIdx == 0) ? 9 : 15;
            } else {
                sigCtx += 21;
            }
        } else {
            if (log2TrafoSize == 3) {
                sigCtx += 9;
            } else {
                sigCtx += 12;
            }
        }
    }
    // VDBG(hevc, "cIdx %d, sigCtx %d", cIdx, sigCtx);
    int sigInc = ((cIdx == 0) ? sigCtx : (27 + sigCtx));
    if (sigInc > 125) {
        //for 126, 127
        sigInc = sigInc - 126 + 42;
    }
    return sigInc;
}

static int ctx_for_coeff_abs_level_greater1(int cIdx, int scanBlockIdx,
                                            bool firstCtx, bool firstSubBlock,
                                            int coeff_abs_level_greater1_flag, int *ctxinc2) {
    // see 9.3.4.2.6
    int ctxSet, lastGreater1Ctx, lastGreater1Flag, greater1Ctx;
    static int prev_ctxSet = 0, prev_greater1Ctx = 0;
    if (firstCtx) {
        // if invoked for the first time
        if (scanBlockIdx == 0 || cIdx > 0) {
            ctxSet = 0;
        } else {
            ctxSet = 2;
        }
        if (firstSubBlock) {
            lastGreater1Ctx = 1;
        } else {
            lastGreater1Ctx = prev_greater1Ctx;
            if (lastGreater1Ctx > 0) {
                lastGreater1Flag = coeff_abs_level_greater1_flag;
                if (lastGreater1Flag == 1) {
                    lastGreater1Ctx = 0;
                } else {
                    lastGreater1Ctx += 1;
                }
            }
        }
        VDBG(hevc, "lastGreater1Ctx %d", lastGreater1Ctx);
        if (lastGreater1Ctx == 0) {
            ctxSet += 1;
        }
        greater1Ctx = 1;
    }
    else {
        ctxSet = prev_ctxSet;
        greater1Ctx = prev_greater1Ctx;
        if (greater1Ctx > 0) {
            lastGreater1Flag = coeff_abs_level_greater1_flag;
            if (lastGreater1Flag == 1) {
                greater1Ctx = 0;
            } else {
                greater1Ctx += 1;
            }
        }
    }
    int ctxInc = (ctxSet * 4) + MIN(3, greater1Ctx);
    if (cIdx > 0) {
        ctxInc += 16;
    }
    prev_ctxSet = ctxSet;
    prev_greater1Ctx = greater1Ctx;

    // see 9.3.4.2.7
    *ctxinc2 = (cIdx > 0) ? ctxSet + 4: ctxSet;

    //  VDBG(hevc, "scanBlockIdx %d ctxSet %d, cIdx %d, ctxInc %d, greater1Ctx
    //  %d",
    //       scanBlockIdx, ctxSet, cIdx, ctxInc, greater1Ctx);
    return ctxInc;
}


//see 9.3.4.2.4
static int ctx_for_coded_sub_block_flag(struct slice_segment_header *slice,
                                        int xS, int yS, int log2TrafoSize,
                                        int cIdx, int coded_sub_block_flag[8][8]) {
    int csbfCtx = 0;
    if (xS < (1<<(log2TrafoSize-2))-1) {
        csbfCtx += coded_sub_block_flag[xS + 1][yS];
    }
    if (yS < (1 << (log2TrafoSize - 2)) - 1) {
        csbfCtx += coded_sub_block_flag[xS][yS + 1];
    }
    int ctxInc = MIN(csbfCtx, 1);
    if (cIdx > 0) {
        ctxInc += 2;
    }
    return ctxInc;
}

static int StatCoeff[4] = {0};

/*see 7.3.8.11 */
static void
parse_residual_coding(cabac_dec *d, struct cu *cu, struct trans_tree *tt,
                      struct slice_segment_header *slice,
                      struct hevc_param_set *hps, int x0, int y0,
                      int log2TrafoSize, int cIdx, struct picture *p) {
    // struct vps *vps = hps->vps;
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    int coded_sub_block_flag[8][8] = {0};
    int explicit_rdpcm_flag[4] = {0};
    int explicit_rdpcm_dir_flag[4] = {0};

    //see 7-38
    int Log2MaxTransformSkipSize = pps->pps_range_ext.log2_max_transform_skip_block_size_minus2 + 2;
    // VDBG(hevc, "parse_residual_coding Log2MaxTransformSkipSize %d, "
    //      "transform_skip_enabled_flag %d, cu_transquant_bypass_flag %d, "
    //      "log2TrafoSize %d",
    //      Log2MaxTransformSkipSize, pps->transform_skip_enabled_flag,
    //      cu->cu_transquant_bypass_flag, log2TrafoSize);
    if (pps->transform_skip_enabled_flag && !cu->cu_transquant_bypass_flag &&
        (log2TrafoSize <= Log2MaxTransformSkipSize)) {
        //see table 9-4
        int ctxInc = (cIdx == 0) ? 0 : 1;
        tt->transform_skip_flag[cIdx][x0 - tt->xT0][y0 - tt->yT0] =
            CABAC(d, CTX_TYPE_RESIDUAL_CODING_TRANSFORM_SKIP + ctxInc);
        VDBG(hevc, "transform_skip_flag %d", tt->transform_skip_flag[cIdx][x0-tt->xT0][y0-tt->yT0]);
    }
    if (cu->CuPredMode == MODE_INTER &&
        sps->sps_range_ext.explicit_rdpcm_enabled_flag &&
        (tt->transform_skip_flag[cIdx][x0-tt->xT0][y0-tt->yT0] ||
         cu->cu_transquant_bypass_flag)) {
        //we will not go here for heif
        assert(0);
        explicit_rdpcm_flag[cIdx] =
            CABAC(d, CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM);
        if (explicit_rdpcm_flag[cIdx]) {
            explicit_rdpcm_dir_flag[cIdx] =
                CABAC(d, CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM_DIR);
        }
    }

    /* see (7-74) (7-75)  (7-76) (7-77)*/
    int LastSignificantCoeffX;
    int LastSignificantCoeffY;

    //see 9.3.4.2.3
    if (cIdx == 0) {
        ctxOffset = 3 * (log2TrafoSize - 2) + ((log2TrafoSize - 1) >> 2);
        ctxShift = (log2TrafoSize + 1) >> 2;
    } else {
        ctxOffset = 15;
        ctxShift = log2TrafoSize - 2;
    }

    int last_sig_coeff_x_prefix =
        CABAC_TR(d, CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_X_PREFIX,
                 (log2TrafoSize << 1) - 1, 0, ctx_for_last_sig_coeff_prefix);
    VDBG(hevc, "last_significant_coeff_x_prefix %d", last_sig_coeff_x_prefix);
    int last_sig_coeff_y_prefix =
        CABAC_TR(d, CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_Y_PREFIX,
                 (log2TrafoSize << 1) - 1, 0, ctx_for_last_sig_coeff_prefix);
    VDBG(hevc, "last_significant_coeff_y_prefix %d", last_sig_coeff_y_prefix);
    if (last_sig_coeff_x_prefix > 3) {
        int last_sig_coeff_x_suffix = CABAC_FL(d,
            (1 << ((last_sig_coeff_x_prefix >> 1) - 1)) - 1);
        // VDBG(hevc, "last_sig_coeff_x_suffix %d", last_sig_coeff_x_suffix);
        LastSignificantCoeffX =
            (1 << ((last_sig_coeff_x_prefix >> 1) - 1)) *
                (2 + (last_sig_coeff_x_prefix & 1)) + last_sig_coeff_x_suffix;
    } else {
        LastSignificantCoeffX = last_sig_coeff_x_prefix;
    }

    if (last_sig_coeff_y_prefix > 3) {
         int last_sig_coeff_y_suffix = CABAC_FL(d,
            (1 << ((last_sig_coeff_y_prefix >> 1) - 1)) - 1);
        // VDBG(hevc, "last_sig_coeff_y_suffix %d", last_sig_coeff_y_suffix);
        LastSignificantCoeffY =
            (1 << ((last_sig_coeff_y_prefix >> 1) - 1)) *
                (2 + (last_sig_coeff_y_prefix & 1)) +
            last_sig_coeff_y_suffix;
    } else {
        LastSignificantCoeffY = last_sig_coeff_y_prefix;
    }
    VDBG(hevc, "LastSignificantCoeffX %d, LastSignificantCoeffY %d",
         LastSignificantCoeffX, LastSignificantCoeffY);

    int xS, xC, yS, yC;
    //see 7.4.9.11
    int predModeIntra = 0, scanIdx;
    if (cu->CuPredMode == MODE_INTRA &&
        (log2TrafoSize == 2 || (log2TrafoSize == 3 && cIdx == 0) ||
         (log2TrafoSize == 3 && CHROMA_444 == slice->ChromaArrayType))) {
        predModeIntra = (cIdx == 0) ? get_IntraPredModeY(slice, p, x0, y0)
                                    : get_IntraPredModeC(slice, p, x0, y0);

        if (predModeIntra >= 6 && predModeIntra <= 14) {
            scanIdx = 2;
        } else if (predModeIntra >= 22 && predModeIntra <= 30) {
            scanIdx = 1;
        } else {
            scanIdx = 0;
        }
    } else {
        scanIdx = 0;
    }
    /*see(7-78)*/
    if (scanIdx == 2) {
        swap(&LastSignificantCoeffX, &LastSignificantCoeffY);
    }
    VDBG(hevc, "scanIdx %d, LastSignificantCoeffX %d, LastSignificantCoeffY %d",
         scanIdx, LastSignificantCoeffX, LastSignificantCoeffY);

    // int escapeDataPresent;
    int lastScanPos = 16;
    int lastSubBlock =
        (1 << (log2TrafoSize - 2)) * (1 << (log2TrafoSize - 2)) - 1;
    VDBG(hevc, "lastSubBlock %d", lastSubBlock);
    do {
        if (lastScanPos == 0) {
            lastScanPos = 16;
            lastSubBlock --;
        }
        lastScanPos --;
        xS = slice->ScanOrder[log2TrafoSize - 2][scanIdx][lastSubBlock].x;
        yS = slice->ScanOrder[log2TrafoSize - 2][scanIdx][lastSubBlock].y;
        xC = ( xS << 2 ) + slice->ScanOrder[2][scanIdx][lastScanPos].x;
        yC = ( yS << 2 ) + slice->ScanOrder[2][scanIdx][lastScanPos].y;
    } while((xC != LastSignificantCoeffX ) || (yC != LastSignificantCoeffY));
    // find the lastSubBlock and lastScanPos and accordingly xC, yC
    // VDBG(hevc, "xC %d, yC %d, lastSubBlock %d, lastScanPos %d", xC, yC,
    //      lastSubBlock, lastScanPos);

    bool firstCtxOfSubBlock = true, firstSubBlock = true;
    int prev_coeff_abs_level_greater1_flag = 0;

    for (int i = lastSubBlock; i >= 0; i--) {
        int coeff_abs_level_greater1_flag[16] = {0};
        int sig_coeff_flag[16] = {0};
        int coeff_sign_flag[16] = {0};
        int coeff_abs_level_greater2_flag[16] = {0};

        xS = slice->ScanOrder[log2TrafoSize - 2][scanIdx][i].x;
        yS = slice->ScanOrder[log2TrafoSize - 2][scanIdx][i].y;
        int escapeDataPresent = 0;
        int inferSbDcSigCoeffFlag = 0;

        // read this code twice!! coded_sub_block_flag specify the sub-block (4X4) array at location (xS, yS)
        // of 16 transform coefficient levels. 0 means all levels are 0.
        if ((i < lastSubBlock) && (i > 0)) {
            //see 9.3.4.2.4
            int ctxInc = ctx_for_coded_sub_block_flag(
                slice, xS, yS, log2TrafoSize, cIdx, coded_sub_block_flag);
            // rc[xS][yS].coded_sub_block_flag
            coded_sub_block_flag[xS][yS] = CABAC(
                d, CTX_TYPE_RESIDUAL_CODING_CODED_SUB_BLOCK_FLAG + ctxInc);
            inferSbDcSigCoeffFlag = 1;
            VDBG(hevc, "(%d, %d)coded_sub_block_flag %d", xS, yS,
                 coded_sub_block_flag[xS][yS]);
        } else if ((xS == 0 && yS ==0)|| xS == (LastSignificantCoeffX>>2) && yS == (LastSignificantCoeffY >>2)) {
            // edge pos for i == 0 and lastSubBlock ?
            coded_sub_block_flag[xS][yS] = 1;
        }
        VDBG(hevc, "coded_sub_block_flag (%d, %d) %d", xS, yS,
             coded_sub_block_flag[xS][yS]);

        int lastCoeff;
        int nz = 0;
        // HIGHLIGHT
        if (i == lastSubBlock) {
            lastCoeff = lastScanPos - 1;
            // did read it from spec, when sig_coeff_flag not present
            // resides outside of for-loop so should be put advance
            // rc[LastSignificantCoeffX][LastSignificantCoeffY].sig_coeff_flag = 1;
            // LastSignificantCoeffX, LastSignificantCoeffY is in the lastCoeff+1
            // so start from this last nz value
            sig_coeff_flag[lastCoeff+1]=1;
            nz = 1;
        } else {
            lastCoeff = 15;
        }
#if 0
        VDBG(hevc, "lastCoeff %d", lastCoeff);
        for (int n = nz; n >= 0; n--) {
            xC = (xS << 2) + slice->ScanOrder[2][scanIdx][n].x;
            yC = (yS << 2) + slice->ScanOrder[2][scanIdx][n].y;
            if (xC == LastSignificantCoeffX && yC == LastSignificantCoeffY) {
                VDBG(hevc, "last n %d", n);
            }
            if (xC == x0 && yC == y0) {
                VDBG(hevc, "first pos n %d", n);
            }
        }
#endif
        // decode all coeff flag, except last one which already got
        for (int n = lastCoeff; n >= 0; n--) {
            xC = (xS << 2) + slice->ScanOrder[2][scanIdx][n].x;
            yC = (yS << 2) + slice->ScanOrder[2][scanIdx][n].y;
            // VDBG(hevc, "n %d, xC, yC(%d, %d), coded_sub_block_flag %d", n, xC,
            //      yC, coded_sub_block_flag[xS][yS]);
            if (coded_sub_block_flag[xS][yS] &&
                (n > 0 || !inferSbDcSigCoeffFlag)) {
                // see 9.3.4.2.5
                int sigInc = ctx_for_sig_coeff_flag(
                    cu, slice, sps, log2TrafoSize, cIdx,
                    tt->transform_skip_flag[cIdx][xC - tt->xT0][yC - tt->yT0],
                    scanIdx, x0, y0, xC, yC, coded_sub_block_flag);
                // rc[xC][yC].sig_coeff_flag =
                sig_coeff_flag[n] = CABAC(d, CTX_TYPE_RESIDUAL_CODING_SIG_COEFF_FLAG + sigInc);
                // VDBG(hevc, "sigInc %d, xC, yC(%d, %d), sig_coeff_flag %d", sigInc,
                //      xC, yC, sig_coeff_flag[n]);
                if (sig_coeff_flag[n]) {
                    inferSbDcSigCoeffFlag = 0;
                    nz ++;
                }
            } else {
                // if sig_coeff_flag not present, inferred as
                if ((((xC & 3) == 0) && ((yC & 3) == 0) &&
                     inferSbDcSigCoeffFlag == 1 &&
                     coded_sub_block_flag[xS][yS] == 1)) {
                    // rc[xC][yC].sig_coeff_flag = 1;
                    sig_coeff_flag[n] = 1;
                    nz ++;
                }
                // VDBG(hevc, "sig_coeff_flag %d", sig_coeff_flag[n]);
            }
        }
        int firstSigScanPos = 16;
        int lastSigScanPos = -1;
        int numGreater1Flag = 0;
        int lastGreater1ScanPos = -1;
        firstCtxOfSubBlock = true;
        // sig_coeff_flag specifies for coeff at (xC, yC) in current block whether
        // the coeff is zero or not
        int ctxInc_greater2;
        // VDBG(hevc, "nz %d", nz);

        // since we put sig_coeff_flag in a array, and all calculated scan pos
        // from ScanOrder[2][scanIdx][n], just use the index and ignore the pos
        // cause they should be the same
        for (int n = 15; n >= 0; n--) {
            xC = ( xS << 2 ) + slice->ScanOrder[2][scanIdx][n].x;
            yC = ( yS << 2 ) + slice->ScanOrder[2][scanIdx][n].y;
            // VDBG(hevc, "n %d (%d, %d) sig_coeff_flag %d", n, xC, yC, sig_coeff_flag[n]);
            //use indexed sig_coeff_flag
            if (sig_coeff_flag[n]) {
                if (numGreater1Flag < 8) {
                    int ctxInc = ctx_for_coeff_abs_level_greater1(
                        cIdx, i, firstCtxOfSubBlock, firstSubBlock,
                        prev_coeff_abs_level_greater1_flag, &ctxInc_greater2);
                    coeff_abs_level_greater1_flag[n] = CABAC(
                        d, CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER1 +
                               ctxInc);
                    firstSubBlock = false;
                    firstCtxOfSubBlock = false;
                    prev_coeff_abs_level_greater1_flag =
                        coeff_abs_level_greater1_flag[n];
                    // VDBG(hevc, "ctxInc %d, (%d, %d) coeff_abs_level_greater1_flag %d",
                    //      ctxInc, xC, yC, coeff_abs_level_greater1_flag[n]);
                    numGreater1Flag ++;
                    if (coeff_abs_level_greater1_flag[n] &&
                        lastGreater1ScanPos == -1) {
                        lastGreater1ScanPos = n;
                    } else if (coeff_abs_level_greater1_flag[n]) {
                        escapeDataPresent = 1;
                    }
                } else {
                    escapeDataPresent = 1;
                }
                if (lastSigScanPos == -1) {
                    lastSigScanPos = n;
                }
                firstSigScanPos = n;
            }
        }
        int signHidden = 0;
        if (cu->cu_transquant_bypass_flag ||
            (cu->CuPredMode == MODE_INTRA &&
             sps->sps_range_ext.implicit_rdpcm_enabled_flag &&
             tt->transform_skip_flag[cIdx][x0 - tt->xT0][y0 - tt->yT0] &&
             (predModeIntra == 10 || predModeIntra == 26)) ||
            explicit_rdpcm_flag[cIdx]) {
            signHidden = 0;
        } else {
            signHidden = lastSigScanPos - firstSigScanPos > 3;
        }
        // VDBG(hevc, "signHidden %d", signHidden);

        if (lastGreater1ScanPos != -1) {
            coeff_abs_level_greater2_flag[lastGreater1ScanPos] =
                CABAC(d, CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER2 +
                             ctxInc_greater2);
            if (coeff_abs_level_greater2_flag[lastGreater1ScanPos]) {
                escapeDataPresent = 1;
            }
            // VDBG(hevc,
            //      "(%d, %d) lastGreater1ScanPos %d, coeff_abs_level_greater2_flag %d",
            //      lastGreater1ScanPos, x0, y0, coeff_abs_level_greater2_flag[lastGreater1ScanPos]);
        }
        // see 9-20, 9-21, 9-22
        int sbType = 0;
        if (cu->cu_transquant_bypass_flag == 0 &&
            tt->transform_skip_flag[cIdx][x0 - tt->xT0][y0 - tt->yT0] == 0) {
            sbType = 2 * (cIdx == 0 ? 1: 0);
        } else {
            sbType = 2 *(cIdx ==0? 1: 0)+ 1;
        }

        for (int n = 15; n >= 0; n--) {
            xC = (xS << 2) + slice->ScanOrder[2][scanIdx][n].x;
            yC = (yS << 2) + slice->ScanOrder[2][scanIdx][n].y;
            // VDBG(hevc,
            //      "(%d, %d) sig_coeff_flag %d, sign_data_hiding_enabled_flag "
            //      "%d, firstSigScanPos %d",
            //      xC, yC, sig_coeff_flag[n],
            //      pps->sign_data_hiding_enabled_flag, firstSigScanPos);
            if (sig_coeff_flag[n] &&
                (!pps->sign_data_hiding_enabled_flag || !signHidden ||
                 (n != firstSigScanPos))) {
                coeff_sign_flag[n] = CABAC_BP(d);
                // VDBG(hevc, "xC, yC (%d, %d) coeff_sign_flag[%d] %d", xC, yC, n,
                //      coeff_sign_flag[n]);
            }
        }
        int numSigCoeff = 0;
        int sumAbsLevel = 0;
        int cRiceParam = 0;
        if (sps->sps_range_ext.persistent_rice_adaptation_enabled_flag) {
            cRiceParam = StatCoeff[sbType] / 4;
        }
        bool firstAbsLevelRemaining = true;

        int coeff_abs_level_remaining[16] = {0};
        for (int n = 15; n >= 0; n--) {
            xC = (xS << 2) + slice->ScanOrder[2][scanIdx][n].x;
            yC = (yS << 2) + slice->ScanOrder[2][scanIdx][n].y;
            // VDBG(hevc, "(%d %d)sig_coeff_flag %d, coeff_abs_level_greater1_flag %d, n %d",
            //     xC, yC, sig_coeff_flag[n],
            //     coeff_abs_level_greater1_flag[n], n);
            if (sig_coeff_flag[n]) {
                int baseLevel = 1 + coeff_abs_level_greater1_flag[n] +
                                coeff_abs_level_greater2_flag[n];
                if (baseLevel == (( numSigCoeff < 8) ? ( (n == lastGreater1ScanPos) ? 3 : 2) : 1)) {
                    //see 9.3.3.11 binarization process for coeff_abs_level_remaining
                    int prefix = -1;
                    int code = 0;
                    do {
                        prefix ++;
                        code = CABAC_BP(d);
                    } while(code);
                    // VDBG(hevc, "prefix %d, cRice %d", prefix, cRiceParam);
                    if (prefix <= 3) {
                        if (cRiceParam > 0) {
                            code = CABAC_FL(d, (1 << cRiceParam) - 1);
                        }
                        coeff_abs_level_remaining[n] = (prefix << cRiceParam) + code;
                    } else {
                        if (prefix - 3 + cRiceParam > 0) {
                            code = CABAC_FL(d, (1 <<(prefix - 3 + cRiceParam)) -1);
                        }
                        coeff_abs_level_remaining[n] =
                            (((1 << (prefix - 3)) + 2) << cRiceParam) + code;
                    }

                    if (sps->sps_range_ext.persistent_rice_adaptation_enabled_flag) {
                        if (baseLevel + coeff_abs_level_remaining[n] > 3 *(1<< cRiceParam)) {
                            cRiceParam ++;
                        }
                    } else {
                        if (baseLevel + coeff_abs_level_remaining[n] > 3 *(1<< cRiceParam)) {
                            cRiceParam ++;
                            if (cRiceParam > 4) {
                                cRiceParam = 4;
                            }
                        }
                    }
                    // see 9-23
                    if (coeff_abs_level_remaining[n] >= (3 << (StatCoeff[sbType] / 4))) {
                        StatCoeff[sbType]++;
                    } else if (2 * coeff_abs_level_remaining[n] < (1 << (StatCoeff[sbType] / 4)) && StatCoeff[sbType] > 0) {
                        StatCoeff[sbType]--;
                    }
                    firstAbsLevelRemaining = false;
                    VDBG(hevc, "coeff_abs_level_remaining %d", coeff_abs_level_remaining[n]);
                }
                //x0, y0 is the top left luma sample
                //xC, yC is the location within the block
                tt->TransCoeffLevel[cIdx][x0 + xC - tt->xT0][y0 + yC - tt->yT0] =
                    (coeff_abs_level_remaining[n] + baseLevel) *
                    (1 - 2 * coeff_sign_flag[n]);
                if (pps->sign_data_hiding_enabled_flag && signHidden) {
                    sumAbsLevel +=
                        (coeff_abs_level_remaining[n] + baseLevel);
                    if ((n == firstSigScanPos) && ((sumAbsLevel % 2) == 1)) {
                        tt->TransCoeffLevel[cIdx][x0 + xC - cu->x0]
                                           [y0 + yC - cu->y0] =
                            -tt->TransCoeffLevel[cIdx][x0 + xC - cu->x0]
                                                [y0 + yC - cu->y0];
                    }
                }
                VINFO(hevc, "predModeIntra %d TU (%d, %d) %d log2TrafoSize %d",
                      predModeIntra, x0 + xC, y0 + yC,
                      tt->TransCoeffLevel[cIdx][x0 + xC - cu->x0]
                                         [y0 + yC - cu->y0],
                      log2TrafoSize);
                numSigCoeff++;
            }
        }
    }

    VINFO(hevc, "end of residual coding");
}

/* see 7.3.8.10 */
/**
 * The function "parse_transform_unit" is used to parse and decode the transform unit in a video coding
 * process.
 * 
 * @param d A pointer to a cabac_dec structure.
 * @param cu A pointer to a struct representing a coding unit in the HEVC video coding standard.
 * @param tt A pointer to a struct representing the transform tree.
 * @param slice A pointer to a struct slice_segment_header, which contains information about the
 * current slice segment in the HEVC video stream.
 * @param hps hevc_param_set structure containing various HEVC parameters
 * @param x0 The x-coordinate of the top-left corner of the current transform unit (TU) within the
 * coding unit (CU).
 * @param y0 The parameter "y0" represents the starting vertical coordinate of the transform unit
 * within the coding unit. It is used to calculate the position of the transform unit within the frame.
 * @param xBase The x-coordinate of the top-left corner of the current coding unit (CU) in the current
 * coding tree unit (CTU).
 * @param yBase The parameter `yBase` represents the starting y-coordinate of the current transform
 * unit within the coding unit. It is used to calculate the actual y-coordinate of the chroma
 * components within the transform unit.
 * @param log2TrafoSize The variable "log2TrafoSize" represents the logarithm base 2 of the transform
 * size. It determines the size of the transform unit being parsed.
 * @param trafoDepth The variable "trafoDepth" represents the depth of the transform unit in the coding
 * tree. It indicates the level of the transform unit within the hierarchical structure of the video
 * coding process.
 * @param blkIdx The parameter "blkIdx" represents the index of the current block within the coding
 * unit (CU). It is used to determine the position of the block within the CU and is used for various
 * calculations and operations within the function. 0-3
 */
static void parse_transform_unit(cabac_dec *d, struct cu *cu,
                                 struct trans_tree *tt,
                                 struct slice_segment_header *slice,
                                 struct hevc_param_set *hps, int x0, int y0,
                                 int xBase, int yBase, int log2TrafoSize,
                                 int trafoDepth, int blkIdx, struct picture *p,
                                 int cbf_luma, int cbf_cb, int cbf_cr) {
    struct pps *pps = hps->pps;
    uint8_t ChromaArrayType = slice->ChromaArrayType;

    tt->tu_num++;
    VDBG(hevc, "tu (%d, %d) %d, %d", x0, y0, 1 << log2TrafoSize,
         tt->tu_num);
    int log2TrafoSizeC = MAX(2, log2TrafoSize - (ChromaArrayType == 3 ? 0 : 1));
    int cbfDepthC = trafoDepth - ((ChromaArrayType != 3 && log2TrafoSize == 2) ? 1 : 0);
    int xC = (ChromaArrayType != 3 && log2TrafoSize == 2) ? xBase : x0;
    int yC = (ChromaArrayType != 3 && log2TrafoSize == 2) ? yBase : y0;
    int cbfLuma = cbf_luma;
    int cbfChroma = (cbf_cb & 0x1) || (cbf_cr & 0x1)|| (ChromaArrayType == 2 &&
        (cbf_cb & 0x2 || cbf_cr & 0x2));
    VDBG(hevc, "parse_transform_unit x0 %d, y0 %d, xBase %d, yBase %d, blkIdx %d, "
         "cbf_luma %d, cbf_cb %d, log2TrafoSize %d",
         x0, y0, xBase, yBase, blkIdx, cbfLuma, cbfChroma,
         log2TrafoSize);
    cu->blkIdx = blkIdx;

    if (cbfLuma || cbfChroma) {
        int xP = ( x0 >> slice->MinCbLog2SizeY ) << slice->MinCbLog2SizeY;
        int yP = ( y0 >> slice->MinCbLog2SizeY ) << slice->MinCbLog2SizeY;
        int nCbS = 1 << slice->MinCbLog2SizeY;
        if (pps->pps_scc_ext.residual_adaptive_colour_transform_enabled_flag &&
            (cu->CuPredMode == MODE_INTER ||
             (cu->PartMode == PART_2Nx2N &&
              get_intra_chroma_pred_mode4(slice, p, x0, y0)) ||
             (get_intra_chroma_pred_mode4(slice, p, xP, yP) &&
              get_intra_chroma_pred_mode4(slice, p, xP + nCbS / 2,yP) &&
              get_intra_chroma_pred_mode4(slice, p, xP, yP + nCbS / 2) &&
              get_intra_chroma_pred_mode4(slice, p, xP + nCbS / 2,yP + nCbS / 2)))) {
            tt->tu_residual_act_flag[x0-tt->xT0][y0-tt->yT0] =
                CABAC(d, CTX_TYPE_TU_RESIDUAL_ACT_FLAG);
            VDBG(hevc, "tu_residual_act_flag %d",
                 tt->tu_residual_act_flag[x0-tt->xT0][y0-tt->yT0]);
        }
        parse_delta_qp(d, slice, pps);
        if (cbfChroma && !cu->cu_transquant_bypass_flag &&
            !slice->IsCuChromaQpOffsetCoded) {
            struct chroma_qp_offset *qpoff = calloc(1, sizeof(*qpoff));
            parse_chroma_qp_offset(d, slice, pps, cu, qpoff);
        }
        if (cbfLuma) {
            VDBG(hevc, "luma");
            parse_residual_coding(d, cu, tt, slice, hps, x0, y0,
                                  log2TrafoSize, 0, p);
        }
        if (log2TrafoSize > 2 || ChromaArrayType == 3) {
            if (pps->pps_range_ext.cross_component_prediction_enabled_flag &&
                cbfLuma &&
                (cu->CuPredMode == MODE_INTER ||
                 get_intra_chroma_pred_mode4(slice, p, x0, y0))) {
                parse_cross_comp_pred(d, &cu->ccp[x0][y0], x0, y0, 0);
            }
            for (int tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++) {
                if (cbf_cb & (1 << tIdx)) {
                    parse_residual_coding(d, cu, tt, slice, hps, x0,
                                          y0 + (tIdx << log2TrafoSizeC),
                                          log2TrafoSizeC, 1, p);
                }
            }
            if (pps->pps_range_ext.cross_component_prediction_enabled_flag &&
                cbfLuma && (cu->CuPredMode == MODE_INTER ||
                get_intra_chroma_pred_mode4(slice, p, x0, y0))) {
                parse_cross_comp_pred(d, &cu->ccp[x0][y0], x0, y0, 1);
            }
            for (int tIdx = 0; tIdx < ( ChromaArrayType == 2 ? 2 : 1 ); tIdx++ ) {
                //[trafoDepth][x0 - tt->xT0][y0 - tt->yT0 + (tIdx << log2TrafoSizeC)]
                if (cbf_cr & (1 << tIdx)) {
                    parse_residual_coding(d, cu, tt, slice, hps, x0,
                                          y0 + (tIdx << log2TrafoSizeC),
                                          log2TrafoSizeC, 2, p);
                }
            }
        } else if (blkIdx == 3) {
            for (int tIdx = 0; tIdx < ( ChromaArrayType == 2 ? 2 : 1 ); tIdx++ ) {
                //[trafoDepth - 1][xBase - tt->xT0][yBase - tt->yT0 + (tIdx << log2TrafoSizeC)]
                if (cbf_cb & (1 << tIdx)) {
                    parse_residual_coding(d, cu, tt, slice, hps, xBase,
                                          yBase + (tIdx << log2TrafoSizeC),
                                          log2TrafoSize, 1, p);
                }
            }
            for (int tIdx = 0; tIdx < (ChromaArrayType == 2 ? 2 : 1); tIdx++) {
                //[trafoDepth - 1][xBase - tt->xT0][yBase - tt->yT0 + (tIdx << log2TrafoSizeC)]
                if (cbf_cr & (1 << tIdx)) {
                    parse_residual_coding(d, cu, tt, slice, hps, xBase,
                                          yBase + (tIdx << log2TrafoSizeC),
                                          log2TrafoSize, 2, p);
                }
            }
        }
    }

    // for (int j = 0; j < (1 << log2TrafoSize); j++) {
    //     for (int i = 0; i < (1 << log2TrafoSize); i++) {
    //         fprintf(vlog_get_stream(), " %d",
    //                 dst[i + j * (1 << log2TrafoSize)]);
    //     }
    //     fprintf(vlog_get_stream(), "\n");
    // }
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
static void parse_transform_tree(cabac_dec *d, struct cu *cu,
    struct slice_segment_header *slice,
    struct hevc_param_set *hps, int x0, int y0, int xBase, int yBase,
    int log2TrafoSize, int trafoDepth, int blkIdx, struct picture *p, int parent_cbf_cb, int parent_cbf_cr) 
{
    VINFO(hevc, "parse_transform_tree %d (%d, %d) (%d, %d) parent %d, %d",
          1 << log2TrafoSize, x0, y0, xBase, yBase, parent_cbf_cb,
          parent_cbf_cr);
    struct trans_tree *tt = &cu->tt;
    int split_transform_flag = 0;
    int cbf_cb = -1, cbf_cr = -1; // bit 0: top block, bit 1: bottom block
    //see 7.4.9.8
    if (log2TrafoSize <= slice->MaxTbLog2SizeY &&
        log2TrafoSize > slice->MinTbLog2SizeY &&
        trafoDepth < cu->MaxTrafoDepth && !(cu->IntraSplitFlag && (trafoDepth == 0))) {
        split_transform_flag =
            CABAC(d, CTX_TYPE_TU_SPLIT_TRANSFORM_FLAG + 5 - log2TrafoSize);
    } else {
        int interSplitFlag =
            (hps->sps->max_transform_hierarchy_depth_inter == 0 &&
             get_CuPredMode(slice, p, x0, y0) == MODE_INTER &&
             cu->PartMode != PART_2Nx2N && trafoDepth == 0)? 1: 0;
        if (log2TrafoSize > slice->MaxTbLog2SizeY || interSplitFlag == 1 ||
            (cu->IntraSplitFlag == 1 && trafoDepth == 0)) {
            split_transform_flag = 1;
        }
    }
    if (split_transform_flag) {
        VDBG(hevc, "split_transform_flag(%d, %d, %d) %d", x0, y0, trafoDepth, split_transform_flag);
        set_split_transform_flag(slice, p, x0, y0, trafoDepth, split_transform_flag);
    }
    if ((log2TrafoSize > 2 && slice->ChromaArrayType != 0) || slice->ChromaArrayType == 3) {
        if (trafoDepth == 0 || parent_cbf_cb) {
            //[trafoDepth][x0][y0]
            cbf_cb = CABAC(d, CTX_TYPE_TU_CBF_CBCR + trafoDepth);
            VDBG(hevc, "cbf_cb %d, ChromaArrayType %d", cbf_cb, slice->ChromaArrayType);
            if (slice->ChromaArrayType == 2 &&
                (!split_transform_flag || log2TrafoSize == 3)) {
                //[trafoDepth][x0][y0 + (1 << (log2TrafoSize - 1))]
                cbf_cb |= (CABAC(d, CTX_TYPE_TU_CBF_CBCR + trafoDepth) << 1);
                VDBG(hevc, "cbf_cb %d", cbf_cb);
            }
        }
        if (trafoDepth == 0 || parent_cbf_cr) {
            //[trafoDepth][x][y]
            cbf_cr = CABAC(d, CTX_TYPE_TU_CBF_CBCR + trafoDepth);
            VDBG(hevc, "cbf_cr %d", cbf_cr);
            if (slice->ChromaArrayType == 2 && (!split_transform_flag || log2TrafoSize == 3)) {
                //[trafoDepth][x][y + (1 << (log2TrafoSize - 1))]
                cbf_cr |= (CABAC(d, CTX_TYPE_TU_CBF_CBCR + trafoDepth) << 1);
                VDBG(hevc, "cbf_cr %d", cbf_cr);
            }
        }
    }
    // if not present, use parent value
    if (cbf_cb < 0) {
        if (trafoDepth > 0 && log2TrafoSize == 2) {
            cbf_cb = parent_cbf_cb;
        } else {
            cbf_cb = 0;
        }
    }
    if (cbf_cr < 0) {
        if (trafoDepth > 0 && log2TrafoSize == 2) {
            cbf_cr = parent_cbf_cr;
        } else {
            cbf_cr = 0;
        }
    }
    if (split_transform_flag) {
        int x1 = x0 + ( 1 << ( log2TrafoSize - 1));
        int y1 = y0 + ( 1 << ( log2TrafoSize - 1));
        parse_transform_tree(d, cu, slice, hps, x0, y0, x0, y0,
                             log2TrafoSize - 1, trafoDepth + 1, 0, p, cbf_cb,
                             cbf_cr);
        parse_transform_tree(d, cu, slice, hps, x1, y0, x0, y0,
                            log2TrafoSize - 1, trafoDepth + 1, 1, p,
                            cbf_cb, cbf_cr);
        parse_transform_tree(d, cu, slice, hps, x0, y1, x0, y0,
                             log2TrafoSize - 1, trafoDepth + 1, 2, p, cbf_cb,
                             cbf_cr);
        parse_transform_tree(d, cu, slice, hps, x1, y1, x0, y0,
                             log2TrafoSize - 1, trafoDepth + 1, 3, p, cbf_cb,
                             cbf_cr);
    } else {
        int cbf_luma = 1;
        // no tt split
        if (cu->CuPredMode == MODE_INTRA || trafoDepth != 0 || (cbf_cb & 0x1) ||
            (cbf_cr & 0x1) || (slice->ChromaArrayType == 2 && /*[trafoDepth][x][y + (1 << (log2TrafoSize - 1))]*/
             (cbf_cb & 0x2 || cbf_cr & 0x2))) {
            cbf_luma =
                CABAC(d, CTX_TYPE_TU_CBF_LUMA + ((trafoDepth == 0) ? 1 : 0));
            VDBG(hevc, "cbf_luma %d", cbf_luma);
        }
        parse_transform_unit(d, cu, tt, slice, hps, x0, y0, xBase, yBase,
                             log2TrafoSize, trafoDepth, blkIdx, p, cbf_luma,
                             cbf_cb, cbf_cr);
    }
    // decode the transform unit including split tree from the top trafo level
    if (trafoDepth == 0) {
        decode_cu_coded_intra_prediction_mode(slice, cu, hps, tt, x0, y0, xBase,
                                              yBase, log2TrafoSize, p);
    }
}

static void parse_mvd_coding(cabac_dec *d, struct mvd_coding *mvd, int x0,
                             int y0, int refList) {
    mvd->abs_mvd_greater0_flag[0] = CABAC(d, CTX_TYPE_MV_ABS_MVD_GREATER0);
    mvd->abs_mvd_greater0_flag[1] = CABAC(d, CTX_TYPE_MV_ABS_MVD_GREATER0);
    if (mvd->abs_mvd_greater0_flag[0]) {
        mvd->abs_mvd_greater1_flag[0] = CABAC(d, CTX_TYPE_MV_ABS_MVD_GREATER1);
    }
    if (mvd->abs_mvd_greater0_flag[1]) {
        mvd->abs_mvd_greater1_flag[1] = CABAC(d, CTX_TYPE_MV_ABS_MVD_GREATER1);
    }
    if (mvd->abs_mvd_greater0_flag[0]) {
        if (mvd->abs_mvd_greater1_flag[0]) {
            mvd->abs_mvd_minus2[0] = GOL_UE(d->bits);
        }
        mvd->mvd_sign_flag[0] = CABAC_BP(d);
    }
    if (mvd->abs_mvd_greater0_flag[1]) {
        if (mvd->abs_mvd_greater1_flag[1]) {
            mvd->abs_mvd_minus2[1] = GOL_UE(d->bits);
        }
        mvd->mvd_sign_flag[1] = CABAC_BP(d);
    }
}

static void parse_prediction_unit(cabac_dec *d,
                                  struct slice_segment_header *slice,
                                  struct cu *cu, struct hevc_nalu_header *h,
                                  struct hevc_param_set *hps, int x0, int y0,
                                  int nxCbS, int nyCbS) {
    struct vps *vps = hps->vps;
    struct pps *pps = hps->pps;
    struct sps *sps = hps->sps;
    struct predication_unit *pu = &cu->pu[x0][y0];
    VDBG(hevc, "parse_prediction_unit");

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
    pu->mvd = calloc(1, sizeof(struct mvd_coding));
    if (cu->CuPredMode==MODE_SKIP) {
        if (MaxNumMergeCand > 1) {
            pu->merge_idx = CABAC(d, CTX_TYPE_PU_MERGE_IDX);
        } else {
            //when merge_idx is not present, it is inferred to be 0
            pu->merge_idx = 0;
        }
    } else {/*MODE_INTER*/
#if 0
        pu->merge_flag = CABAC(d, CTX_TYPE_PU_MERGE_FLAG);
        if (pu->merge_flag) {
            if (MaxNumMergeCand > 1) {
                pu->merge_idx = CABAC(d, CTX_TYPE_PU_MERGE_IDX);
            }
        } else {
            if (slice->slice_type == SLICE_TYPE_B) {
                pu->inter_pred_idc = CABAC(d, CTX_TYPE_PU_INTER_PRED_IDC + nPbW);
            }
            if (pu->inter_pred_idc != PRED_L1) {
                if (slice->num_ref_idx_l0_active_minus1 > 0) {
                    pu->ref_idx_l0 = CABAC(d, CTX_TYPE_PU_REF_IDX);
                }
                if (pu->mvd == NULL) {
                    pu->mvd = calloc(1, sizeof(struct mvd_coding));
                }
                parse_mvd_coding(d, pu->mvd, x0, y0, 0);
                pu->mvp_l0_flag = CABAC(d, CTX_TYPE_PU_MVP_FLAG);
            }
            if (pu->inter_pred_idc != PRED_L0) {
                if (slice->num_ref_idx_l1_active_minus1 > 0) {
                    pu->ref_idx_l1 = CABAC(d, CTX_TYPE_PU_REF_IDX);
                }
                if (slice->mvd_l1_zero_flag && pu->inter_pred_idc == PRED_BI) {
                    pu->MvdL1[x0][y0][0] = 0;
                    pu->MvdL1[x0][y0][1] = 0;
                } else {
                    if (pu->mvd == NULL) {
                        pu->mvd = calloc(1, sizeof(struct mvd_coding));
                    }
                    parse_mvd_coding(d, pu->mvd, x0, y0, 1);
                }
                pu->mvp_l1_flag = CABAC(d, CTX_TYPE_PU_MVP_FLAG);
            }
        }
#endif
    }
}

//see 7.3.8.7 PCM sample sytax
static void parse_pcm_sample(struct bits_vec *v, struct pcm_sample *pcm,
                             struct slice_segment_header *slice,
                             struct sps *sps, int x0, int y0, int log2CbSize) {
    int PcmBitDepthY = sps->pcm->pcm_sample_bit_depth_luma_minus1 + 1;
    int PcmBitDepthC = sps->pcm->pcm_sample_bit_depth_chroma_minus1 + 1;


    pcm->pcm_sample_luma = calloc((1<<(log2CbSize << 1)), 4);
    for (int i = 0; i < 1<<(log2CbSize << 1); i ++) {
        pcm->pcm_sample_luma[i] = READ_BITS(v, PcmBitDepthY);
    }
    if (slice->ChromaArrayType != 0) {
        pcm->pcm_sample_chroma = calloc(((2 << (log2CbSize << 1)) / (slice->SubWidthC * slice->SubHeightC)), 4);
        for (int i = 0; i < ((2 << (log2CbSize << 1)) / (slice->SubWidthC * slice->SubHeightC)); i++) {
            pcm->pcm_sample_chroma[i] = READ_BITS(v, PcmBitDepthC);
        }
    }
}

int ctu_depth(struct picture *p, int x, int y) {
    for(int j = p->ctu_num -1; j >=0; j--) {
        struct ctu *ctu = p->ctus[j];
        for (int i = ctu->cu_num - 1; i >= 0; i--) {
            struct cu *cu = ctu->cu[i];
            int x0 = cu->x0;
            int xM = x0 + cu->nCbS - 1;
            if (x>=x0 && x<= xM) {
                int y0 = cu->y0;
                int yM = y0 + cu->nCbS - 1;
                if (y >= y0 && y<= yM) {
                    VDBG(hevc, "(%d, %d)-(%d, %d) %d", x0, y0, xM, yM, cu->CtDepth);
                    return cu->CtDepth;
                }
            }
        }
    }
    assert(0);
    return 0;
}

// see Table 9-49, for split_cu_flag and cu_skip_flag
int get_split_cu_flag_ctxInc(struct slice_segment_header *slice,
                            struct hevc_param_set *hps, struct picture *p,
                            int CtDepth, int x0, int y0) {
    int availableL =
        process_zscan_order_block_availablity(slice, hps, x0, y0, x0 - 1, y0);
    int availableA =
        process_zscan_order_block_availablity(slice, hps, x0, y0, x0, y0 - 1);
    int condL = 0;
    int condA = 0;
    if (availableL && ctu_depth(p, x0 - 1,y0) > CtDepth) {
        condL = 1;
    }
    if (availableA && ctu_depth(p, x0, y0 - 1) > CtDepth) {
        condA = 1;
    }
    VDBG(hevc, "availableL %d, availableA %d ctDepth %d,", availableL,
         availableA, CtDepth);
    return ((condL && availableL) + (condA && availableA));
}

int get_cu_skip_flag_ctxInc(struct slice_segment_header *slice, struct picture *p,
                            struct hevc_param_set *hps,
                            int x0, int y0) {
    int availableL =
        process_zscan_order_block_availablity(slice, hps, x0, y0, x0 - 1, y0);
    int availableA =
        process_zscan_order_block_availablity(slice, hps, x0, y0, x0, y0 - 1);
    int condL = 0;
    int condA = 0;

    if (availableL && (get_CuPredMode(slice, p, x0 - 1, y0)==MODE_SKIP)) {
        condL = 1;
    }
    if (availableA && (get_CuPredMode(slice, p, x0, y0 - 1)==MODE_SKIP)) {
        condA = 1;
    }
    return ((condL && availableL) + (condA && availableA));
}


//see I.7.3.8.5
static struct cu *parse_coding_unit(cabac_dec *d, struct ctu *ctu,
                                    struct hevc_slice *hslice,
                                    struct hevc_param_set *hps, int x0, int y0,
                                    int log2CbSize, int cqtDepth, struct picture *p)
{
    struct cu *cu = calloc(1, sizeof(struct cu));
    cu->CtDepth = cqtDepth;
    ctu->cu[ctu->cu_num++] = cu;
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;

    // nCbS specify the size of current coding block, number of samples
    int nCbS = (1<< log2CbSize);
    cu->x0 = x0;
    cu->y0 = y0;
    cu->log2CbSize = log2CbSize;
    cu->nCbS = nCbS;
    //see I.7.4.7.1
    int DepthFlag = vps->DepthLayerFlag[headr->nuh_layer_id];
    //see (I-30)
    int SkipIntraEnabledFlag =  sps->sps_3d_ext[DepthFlag].skip_intra_enabled_flag;
    uint8_t ChromaArrayType = (sps->separate_colour_plane_flag == 1 ? 0 : sps->chroma_format_idc);

    //see 7-10
    uint32_t MinCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus3 + 3;
    if (pps->transquant_bypass_enabled_flag) {
        cu->cu_transquant_bypass_flag =
            CABAC(d, CTX_TYPE_CU_TRANSQUANT_BYPASS_FLAG);
    }
    assert(slice->slice_type == SLICE_TYPE_I);
    int cu_skip_flag = 0, pcm_flag = 0;
    if (slice->slice_type != SLICE_TYPE_I) {
        cu_skip_flag =
            CABAC(d, CTX_TYPE_CU_SKIP_FLAG +
                         get_cu_skip_flag_ctxInc(slice, p, hps, x0, y0));
    }

    VDBG(hevc, "(%d, %d) nCbS %d, cu_skip_flag %d, SkipIntraEnabledFlag %d, cqtDepth %d", x0, y0, nCbS,
         cu_skip_flag, SkipIntraEnabledFlag, cqtDepth);
    if (cu_skip_flag) {
        parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS, nCbS);
    }
#ifdef ENABLE_3D
    else if (SkipIntraEnabledFlag) {
        cu->skip_intra_flag[x0][y0] = CABAC(d, CTX_TYPE_3D_SKIP_INTRA_FLAG);
    }
#endif
    if (!cu_skip_flag
#ifdef ENABLE_3D
        && !cu->skip_intra_flag[x0][y0]
#endif
    ) {
        if (slice->slice_type != SLICE_TYPE_I) {
            uint8_t pred_mode_flag = CABAC(d, CTX_TYPE_CU_PRED_MODE_FLAG);
            if (pred_mode_flag == 0) {
                cu->CuPredMode = MODE_INTER;
            } else {
                cu->CuPredMode = MODE_INTRA;
            }
        } else {
            cu->CuPredMode = MODE_INTRA;
        }
    } else {
        //if pred_mode_flag not present
        if (slice->slice_type == SLICE_TYPE_I
#ifdef ENABLE_3D
            || cu->skip_intra_flag[x0][y0] == 1
#endif
        ) {
            cu->CuPredMode = MODE_INTRA;
        } else {
            cu->CuPredMode = MODE_SKIP;
        }
    }
    set_CuPredMode(slice, p, x0, y0, log2CbSize, cu->CuPredMode);

    // int CqtCuPartPredEnabledFlag = sps->sps_3d_ext[DepthFlag].cqt_cu_part_pred_enabled_flag && slice->in_comp_pred_flag &&
    //     slice->slice_type != SLICE_TYPE_I && !( headr->nal_unit_type >= BLA_W_LP && headr->nal_unit_type <= RSV_IRAP_VCL23 );
    // int predPartModeFlag;
    // if (CqtCuPartPredEnabledFlag == 1) {
    //     int log2TextCbSize = log2CbSize;
    //     int partTextMode = PartMode;
    //     predPartModeFlag = ((log2TextCbSize == log2CbSize) && (partTextMode == PART_2Nx2N));
    // } else {
    //     predPartModeFlag = 0;
    // }
    if (!cu_skip_flag
#ifdef ENABLE_3D
        && !cu->skip_intra_flag[x0][y0]
#endif
    ) {
        if (sps->sps_scc_ext.palette_mode_enabled_flag &&
            cu->CuPredMode == MODE_INTRA &&
            log2CbSize <= slice->MaxTbLog2SizeY) {
            cu->palette_mode_flag =
                CABAC(d, CTX_TYPE_CU_PALETTE_MODE_FLAG);
            VDBG(hevc, "palette_mode_flag %d", cu->palette_mode_flag);
        } else {
            cu->palette_mode_flag = 0;
        }
        VDBG(hevc, "palette_mode_flag %d", cu->palette_mode_flag);
        if (cu->palette_mode_flag) {
            cu->pc[x0][y0] = calloc(1, sizeof(struct palette_coding));
            parse_palette_coding(d, cu->pc[x0][y0], cu, slice, pps, sps, x0, y0, nCbS);
        } else {
            if ((cu->CuPredMode != MODE_INTRA) ||
                (log2CbSize == MinCbLog2SizeY)) {
                //FIXME table 9-45
                int part_mode;
                if (cu->CuPredMode == MODE_INTRA) {
                    int bit0 = CABAC(d, CTX_TYPE_CU_PART_MODE);
                    part_mode = (bit0 == 1? PART_2Nx2N:PART_NxN);
                } else {
                    int bit0 = CABAC(d, CTX_TYPE_CU_PART_MODE);
                    if (bit0) {
                        part_mode = PART_2Nx2N;
                    } else {
                        int bit1 = CABAC(d, CTX_TYPE_CU_PART_MODE+1);
                        if (log2CbSize > MinCbLog2SizeY && sps->amp_enabled_flag == 0) {
                            part_mode = (bit1? PART_2Nx2N: PART_NxN);
                        } else if ((log2CbSize > MinCbLog2SizeY && sps->amp_enabled_flag == 1)) {
                            int bit3 = CABAC(d, CTX_TYPE_CU_PART_MODE + 3);
                            if (bit3) {
                                part_mode = bit1 ? PART_2Nx2N: PART_NxN;
                            }
                            int bit4 = CABAC_BP(d);
                            if (bit1 && bit4) {
                                part_mode = PART_2Nx2N;
                            }
                            if (bit1 && !bit4) {
                                part_mode = PART_NxN;
                            }
                            if (!bit1 && !bit4) {
                                part_mode = PART_nLx2N;
                            }
                            if (!bit1 && bit4) {
                                part_mode = PART_nRx2N;
                            }
                        } else if (log2CbSize < MinCbLog2SizeY) {
                            if (bit1) {
                                part_mode = PART_2NxN;
                            }
                            if (log2CbSize == 3) {
                                part_mode = PART_Nx2N;
                            } else {
                                int bit2 = CABAC(d, CTX_TYPE_CU_PART_MODE+2);
                                part_mode = PART_NxN - bit2;
                            }
                        }
                    }
                }
                cu->PartMode = part_mode;
                //see table7-10
                if (cu->CuPredMode == MODE_INTRA) {
                    cu->PartMode = (part_mode == 0) ? PART_2Nx2N : PART_NxN;
                    cu->IntraSplitFlag = (part_mode == 0) ? 0 : 1;
                } else if (cu->CuPredMode == MODE_INTER) {
                    cu->PartMode = part_mode;// already aligned the value be define
                    cu->IntraSplitFlag = 0;
                }
            } else {
                // if part_mode is not preset
                cu->PartMode = PART_2Nx2N;
                cu->IntraSplitFlag = 0;
            }
            // for a picture, make sure it is MODE_INTRA
            VINFO(hevc, "CuPredMode (%d, %d) %d, cu->PartMode %d", x0, y0,
                  cu->CuPredMode, cu->PartMode);

            assert(cu->CuPredMode == MODE_INTRA);
            assert(cu->PartMode == PART_2Nx2N || cu->PartMode == PART_NxN);
            if (cu->CuPredMode == MODE_INTRA) {
                if (sps->pcm_enabled_flag) {
                    int Log2MinIpcmCbSizeY = sps->pcm->log2_min_pcm_luma_coding_block_size_minus3 + 3;
                    int Log2MaxIpcmCbSizeY = sps->pcm->log2_diff_max_min_pcm_luma_coding_block_size + Log2MinIpcmCbSizeY;
                    if (cu->PartMode == PART_2Nx2N &&
                        log2CbSize >= Log2MinIpcmCbSizeY &&
                        log2CbSize <= Log2MaxIpcmCbSizeY) {
                        pcm_flag = cabac_dec_terminate(d);
                    }
                }
                VDBG(hevc, "pcm_enabled_flag %d pcm_flag %d", sps->pcm_enabled_flag, pcm_flag);
                if (pcm_flag) {
                    set_pcm_flag(slice, p, x0, y0, log2CbSize, pcm_flag);
                    cu->pcm = calloc(1, sizeof(struct pcm_sample));
                    while (!BYTE_ALIGNED(d->bits)) {
                        //pcm_alignment_zero_bit
                        SKIP_BITS(d->bits, 1);
                    }
                    parse_pcm_sample(d->bits, cu->pcm, slice, sps, x0, y0, log2CbSize);
                } else {
                    int pbOffset = (cu->PartMode == PART_NxN) ? (nCbS / 2) : nCbS;
                    int log2PbSize = log2CbSize - ((cu->PartMode == PART_NxN) ? 1 : 0);

                    int prev_intra_luma_pred_flag[4];
                    int mpm_idx[4], rem_intra_luma_pred_mode[4];
                    int intra_chroma_pred_mode[4];
                    int pred_idx = 0;

                    for (int j = 0; j < nCbS; j = j + pbOffset) {
#ifdef ENABLE_3D
                        //see I.7.4.7.1
                        int DepthFlag = vps->DepthLayerFlag[hslice->nalu->nuh_layer_id];
                        //see I-25
                        int IntraContourEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_contour_enabled_flag && slice->in_comp_pred_flag;
                        //see I-26
                        int IntraDcOnlyWedgeEnabledFlag = sps->sps_3d_ext[DepthFlag].intra_dc_only_wedge_enabled_flag;
#endif
                        for (int i = 0; i < nCbS; i = i + pbOffset) {
#ifdef ENABLE_3D
                            if (IntraDcOnlyWedgeEnabledFlag || IntraContourEnabledFlag) {
                                parse_intra_mode_ext(d, hslice, cu, hps, x0+i, y0+j, log2PbSize);
                            } else {
                                //see I.7.4.9.5.1 when not present, no_dim_flag should be 1
                                cu->intra_ext[x0 + i][y0 + j].no_dim_flag = 1;
                            }
                            assert(cu->intra_ext[x0 + i][y0 + j].no_dim_flag==1);
                            if (cu->intra_ext[x0+i][y0+j].no_dim_flag) {
#endif
                                prev_intra_luma_pred_flag[pred_idx++] =
                                    CABAC(d,
                                        CTX_TYPE_CU_PREV_INTRA_LUMA_PRED_FLAG);
                                VDBG(hevc, "prev_intra_luma_pred_flag %d",
                                     prev_intra_luma_pred_flag[pred_idx-1]);
                            }
                        }
#ifdef ENABLE_3D
                    }
#endif
                    pred_idx = 0;
                    for (int j = 0; j < nCbS; j = j + pbOffset) {
                        for (int i = 0; i < nCbS; i = i + pbOffset) {
                            if (prev_intra_luma_pred_flag[pred_idx]) {
                                int t;
                                for (t = 0; t < 2; t++) {
                                    if (CABAC_BP(d) == 0) {
                                        mpm_idx[pred_idx] = t;
                                        break;
                                    }
                                }
                                if (t >= 2) {
                                     mpm_idx[pred_idx] = 2;
                                }
                                VDBG(hevc, "pred_idx %d mpm_idx %d",
                                     pred_idx, mpm_idx[pred_idx]);
                            } else {
                                rem_intra_luma_pred_mode[pred_idx] =
                                    CABAC_FL(d, (1<<5)-1);
                                VDBG(
                                    hevc,
                                    "pred_idx %d rem_intra_luma_pred_mode %d",
                                    pred_idx, rem_intra_luma_pred_mode[pred_idx]);
                            }

                            int predY = process_luma_intra_prediction_mode(
                                slice, cu, hps, x0 + i, y0 + j,
                                mpm_idx[pred_idx],
                                prev_intra_luma_pred_flag[pred_idx],
                                rem_intra_luma_pred_mode[pred_idx], p);
                            set_IntraPredModeY(slice, log2PbSize, p, x0 + i,
                                               y0 + j, predY);
                            pred_idx++;
                        }
                    }
                    pred_idx = 0;
                    //chroma 444
                    if (ChromaArrayType == 3) {
                        for (int j = 0; j < nCbS; j = j + pbOffset ) {
                            for (int i = 0; i < nCbS; i = i + pbOffset) {
                                int prefix = CABAC(d, CTX_TYPE_CU_INTRA_CHROME_PRED_MODE);
                                if (prefix == 0) {
                                    intra_chroma_pred_mode[pred_idx] = 4;
                                } else {
                                    intra_chroma_pred_mode[pred_idx] = CABAC_FL(d, 3);
                                }
                                VDBG(hevc, "intra_chroma_pred_mode %d",
                                     intra_chroma_pred_mode[pred_idx]);

                                int IntraPredModeC =process_chroma_intra_prediction_mode(slice,
                                    hps, x0+i, y0+j,
                                    intra_chroma_pred_mode[pred_idx], p);
                                set_IntraPredModeC(
                                    slice, log2PbSize, p, x0 + i, y0 + j,
                                    IntraPredModeC,
                                    intra_chroma_pred_mode[pred_idx] == 4);
                                pred_idx ++;
                            }

                        }
                    } else if(ChromaArrayType != 0) {
                        //see 9.3.3.8, and table 9-46
                        int prefix = CABAC(d, CTX_TYPE_CU_INTRA_CHROME_PRED_MODE);
                        if (prefix == 0) {
                            intra_chroma_pred_mode[0] = 4;
                        } else {
                            intra_chroma_pred_mode[0] =
                                CABAC_FL(d, 3);
                        }
                        VDBG(hevc, "prefix %d, intra_chroma_pred_mode %d",
                             prefix, intra_chroma_pred_mode[0]);

                        int IntraPredModeC = process_chroma_intra_prediction_mode(slice,
                            hps, x0, y0, intra_chroma_pred_mode[0], p);
                        set_IntraPredModeC(slice, log2PbSize, p, x0, y0, IntraPredModeC, intra_chroma_pred_mode[0] == 4);
                    }
                }
            } else {
                //inter, which is not possible a I frame
                if( cu->PartMode == PART_2Nx2N )
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS, nCbS);
                else if( cu->PartMode == PART_2NxN ) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS, nCbS / 2 );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0 + ( nCbS / 2 ), nCbS, nCbS / 2 );
                } else if( cu->PartMode == PART_Nx2N ) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS / 2, nCbS );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0 + ( nCbS / 2 ), y0, nCbS / 2, nCbS );
                } else if( cu->PartMode == PART_2NxnU ) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS, nCbS / 4 );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0 + ( nCbS / 4 ), nCbS, nCbS * 3 / 4 );
                } else if( cu->PartMode == PART_2NxnD ) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS, nCbS * 3 / 4 );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0 + ( nCbS * 3 / 4 ), nCbS, nCbS / 4 );
                } else if( cu->PartMode == PART_nLx2N ) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS / 4, nCbS );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0 + ( nCbS / 4 ), y0, nCbS * 3 / 4, nCbS );
                } else if( cu->PartMode == PART_nRx2N ) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS * 3 / 4, nCbS );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0 + ( nCbS * 3 / 4 ), y0, nCbS / 4, nCbS );
                } else if (cu->PartMode == PART_NxN) {
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0, nCbS / 2, nCbS / 2 );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0 + ( nCbS / 2 ), y0, nCbS / 2, nCbS / 2 );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0, y0 + ( nCbS / 2 ), nCbS / 2, nCbS / 2 );
                    parse_prediction_unit(d, slice, cu, headr, hps, x0 + ( nCbS / 2 ), y0 + ( nCbS / 2 ), nCbS / 2, nCbS / 2 );
                } else {
                    //unreachable code
                    assert(0);
                }
            }
        }
    }
    // VDBG(hevc, "log2CbSize %d, nCbS %d", log2CbSize, cu->nCbS);
    // I.7.3.8.5 for 3D high efficiency video coding
    // we should skip this function since it is not really get supported
#ifdef ENABLE_3D
    parse_cu_extension(d, hslice, cu, hps, x0, y0, log2CbSize, slice->NumPicTotalCurr);
    int DcOnlyFlag = cu->ext[x0][y0].dc_only_flag;
    if (DcOnlyFlag || (!cu->skip_intra_flag[x0][y0] && cu->CuPredMode == MODE_INTRA))
        parse_depth_dcs(d, cu, x0, y0, log2CbSize);
#endif

    if (!cu_skip_flag && !pcm_flag
#ifdef ENABLE_3D
        && !cu->skip_intra_flag[x0][y0] && !DcOnlyFlag
#endif
    ) {
            if (cu->CuPredMode != MODE_INTRA &&
                !(cu->PartMode == PART_2Nx2N && cu->pu[x0][y0].merge_flag)) {
                cu->rqt_root_cbf = CABAC(d, CTX_TYPE_CU_RQT_ROOT_CBF);
            } else {
                // for inter blocks with 2NX2N, must be 1
#ifdef ENABLE_3D
                cu->rqt_root_cbf = !DcOnlyFlag;
#endif
                cu->rqt_root_cbf = 1;
            }
            VINFO(hevc, "rqt_root_cbf %d", cu->rqt_root_cbf);
            if (cu->rqt_root_cbf) {
                cu->MaxTrafoDepth = (cu->CuPredMode == MODE_INTRA ?
                    (sps->max_transform_hierarchy_depth_intra + cu->IntraSplitFlag) :
                    sps->max_transform_hierarchy_depth_inter);
                cu->tt.xT0 = x0;
                cu->tt.yT0 = y0;
                parse_transform_tree(d, cu, slice, hps, x0, y0, x0, y0,
                                     log2CbSize, 0, 0, p,
                                     slice->ChromaArrayType != 0 ? 1 : 0,
                                     slice->ChromaArrayType != 0 ? 1 : 0);
                VDBG(hevc, "CU has tu num %d", cu->tt.tu_num);
#if 0
                for (int i = 0; i < 64; i ++) {
                    for (int j = 0; j < 64; j++) {
                        fprintf(vlog_get_stream(), "%d ", cu->tt.TransCoeffLevel[0][i][j]);
                    }
                    fprintf(vlog_get_stream(), "\n");
                }
#endif
            }
    }
    return cu;
}

static void
coding_quadtree(cabac_dec *d, struct ctu *ctu, struct hevc_slice *hslice,
                struct hevc_param_set *hps, int x0,
                int y0, int log2CbSize, int cqtDepth, struct picture *p)
{
    struct slice_segment_header *slice = hslice->slice;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;
    uint8_t split_cu_flag = 0;

    if (x0 + ( 1 << log2CbSize ) <= sps->pic_width_in_luma_samples &&
        y0 + ( 1 << log2CbSize ) <= sps->pic_height_in_luma_samples &&
        log2CbSize > slice->MinCbLog2SizeY) {
        int ctxInc =
            get_split_cu_flag_ctxInc(slice, hps, p, cqtDepth, x0, y0);
        VDBG(hevc, "split_cu_flag ctxInc %d", ctxInc);
        split_cu_flag = CABAC(d, CTX_TYPE_SPLIT_CU_FLAG + ctxInc);
    } else {
        //see 7.4.9.4
        //if split_cu_flag is not present
        if (log2CbSize > slice->MinCbLog2SizeY)
            split_cu_flag = 1;
    }
    if (pps->cu_qp_delta_enabled_flag && log2CbSize >= slice->Log2MinCuQpDeltaSize ) {
        slice->IsCuQpDeltaCoded = 0;
        slice->CuQpDeltaVal = 0;
    }
    if (slice->cu_chroma_qp_offset_enabled_flag &&
        log2CbSize >= slice->Log2MinCuChromaQpOffsetSize) {
        slice->IsCuChromaQpOffsetCoded = 0;
    }
    VDBG(hevc, "split_cu_flag %d", split_cu_flag);
    if (split_cu_flag) {
        int x1 = x0 + (1 << (log2CbSize - 1 ));
        int y1 = y0 + (1 << (log2CbSize - 1 ));
        coding_quadtree(d, ctu, hslice, hps, x0, y0, log2CbSize - 1, cqtDepth + 1, p);
        if (x1 < sps->pic_width_in_luma_samples)
            coding_quadtree(d, ctu, hslice, hps, x1, y0, log2CbSize - 1, cqtDepth + 1, p);
        if (y1 < sps->pic_height_in_luma_samples )
            coding_quadtree(d, ctu, hslice, hps, x0, y1, log2CbSize - 1, cqtDepth + 1, p);
        if (x1 < sps->pic_width_in_luma_samples && y1 < sps->pic_height_in_luma_samples )
            coding_quadtree(d, ctu, hslice, hps, x1, y1, log2CbSize - 1, cqtDepth + 1, p);
    } else {
        parse_coding_unit(d, ctu, hslice, hps, x0, y0, log2CbSize, cqtDepth, p);
    }
    VDBG(hevc, "CU num %d", ctu->cu_num);
}

static struct ctu *
coding_tree_unit(cabac_dec *d, struct hevc_slice *hslice,
                 struct hevc_param_set *hps,
                 int CtbAddrInTs, int CtbAddrInRs,
                 int SliceAddrRs, int *TileId,
                 struct picture *p)
{
    struct ctu *ctu = calloc(1, sizeof(*ctu));
    ctu->CtbAddrInTs = CtbAddrInTs;
    p->ctus[p->ctu_num++] = ctu;
    VDBG(hevc, "ctu num %d", p->ctu_num);
    struct slice_segment_header *slice = hslice->slice;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;
    
    uint32_t xCtb = (CtbAddrInRs % slice->PicWidthInCtbsY) << slice->CtbLog2SizeY;
    uint32_t yCtb = (CtbAddrInRs / slice->PicWidthInCtbsY) << slice->CtbLog2SizeY;

    VDBG(hevc,
         "(%d, %d) CtbAddrInRs %d , slice_sao_luma_flag %d, "
         "slice_sao_chroma_flag %d",
         xCtb, yCtb, CtbAddrInRs, slice->slice_sao_luma_flag,
         slice->slice_sao_chroma_flag);
    if (slice->slice_sao_luma_flag || slice->slice_sao_chroma_flag) {
        ctu->sao = parse_sao(d, sps, slice, xCtb >> slice->CtbLog2SizeY, yCtb >> slice->CtbLog2SizeY, pps->CtbAddrRsToTs,
         CtbAddrInTs, CtbAddrInRs, SliceAddrRs, TileId);
    }
    coding_quadtree(d, ctu, hslice, hps, xCtb, yCtb, slice->CtbLog2SizeY, 0, p);
    return ctu;
}

/*see (6-10) */
static int **
init_zscan_array(struct slice_segment_header *slice, struct sps *sps, int *CtbAddrRsToTs)
{
    int PicWidthInTbsY = slice->PicWidthInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY);
    int PicHeightInTbsY = slice->PicHeightInCtbsY << (slice->CtbLog2SizeY - slice->MinTbLog2SizeY);
    VDBG(hevc, "MinTbAddrZs: PicWidthInTbsY %d PicHeightInTbsY %d", PicWidthInTbsY, PicHeightInTbsY);
    int **MinTbAddrZs;
    MinTbAddrZs = calloc(PicWidthInTbsY, sizeof(int *));
    for (int x = 0; x < PicWidthInTbsY; x ++) {
        MinTbAddrZs[x] = calloc(PicHeightInTbsY, sizeof(int));
    }
    for (int y = 0; y < PicHeightInTbsY; y++ ) {
        for (int x = 0; x < PicWidthInTbsY; x++) {
            int tbX = (x << slice->MinTbLog2SizeY) >> slice->CtbLog2SizeY;
            int tbY = (y << slice->MinTbLog2SizeY) >> slice->CtbLog2SizeY;
            int ctbAddrRs = slice->PicWidthInCtbsY * tbY + tbX;
            MinTbAddrZs[x][y] = CtbAddrRsToTs[ctbAddrRs] << ((slice->CtbLog2SizeY - slice->MinTbLog2SizeY) * 2);
            int m, p = 0;
            for (int i = 0; i < (slice->CtbLog2SizeY - slice->MinTbLog2SizeY); i++) {
                m = 1 << i;
                p += (m & x ? m * m : 0) + (m & y ? 2 * m * m : 0);
            }
            MinTbAddrZs[x][y] += p;
            // fprintf(vlog_get_stream(), "%d ", MinTbAddrZs[x][y]);
        }
        // fprintf(vlog_get_stream(), "\n");
    }
    return MinTbAddrZs;
}

/* see 7.3.8.1 */
static void
parse_slice_segment_data(struct bits_vec *v, struct hevc_slice *hslice,
        struct hevc_param_set *hps, int SliceAddrRs, struct picture *p)
{
    struct slice_segment_header *slice = hslice->slice;
    struct hevc_nalu_header *headr = hslice->nalu;
    struct vps *vps = hps->vps;
    struct sps *sps = hps->sps;
    struct pps *pps = hps->pps;

    uint8_t end_of_slice_segment_flag = 0;
    //see para 6.5.1: CTB raster and tile scanning conversion process
    //see (6-3)
    uint32_t *colWidth = calloc((pps->num_tile_columns_minus1 + 1), 4);
    if (pps->uniform_spacing_flag) {
        for (uint32_t i = 0; i <= pps->num_tile_columns_minus1; i++)
            colWidth[i] = ((i + 1) * slice->PicWidthInCtbsY) / (pps->num_tile_columns_minus1 + 1) -
                        (i * slice->PicWidthInCtbsY) / (pps->num_tile_columns_minus1 + 1);
    } else {
        colWidth[pps->num_tile_columns_minus1] = slice->PicWidthInCtbsY;
        for (uint32_t i = 0; i < pps->num_tile_columns_minus1; i++) {
            colWidth[i] = pps->column_width_minus1[i] + 1;
            colWidth[pps->num_tile_columns_minus1] -= colWidth[i];
        }
    }
    //see (6-4)
    uint32_t *rowHeight = calloc((pps->num_tile_rows_minus1 + 1), 4);
    if (pps->uniform_spacing_flag) {
        for (int j = 0; j <= pps->num_tile_rows_minus1; j++)
            rowHeight[j] = ((j + 1) * slice->PicHeightInCtbsY) / (pps->num_tile_rows_minus1 + 1) -
                            (j * slice->PicHeightInCtbsY) / (pps->num_tile_rows_minus1 + 1);
    } else {
        rowHeight[pps->num_tile_rows_minus1] = slice->PicHeightInCtbsY;
        for (int j = 0; j < pps->num_tile_rows_minus1; j++ ) {
            rowHeight[j] = pps->row_height_minus1[j] + 1;
            rowHeight[pps->num_tile_rows_minus1] -= rowHeight[j];
        }
    }
    uint32_t *colBd = calloc((pps->num_tile_columns_minus1 + 1), 4);
    //see (6-5)
    colBd[0] = 0;
    for (int i = 0; i <= pps->num_tile_columns_minus1; i++) {
        colBd[i + 1] = colBd[i] + colWidth[i];
    }
    //see (6-6)
    uint32_t *rowBd = calloc((pps->num_tile_rows_minus1 + 1), 4);
    rowBd[0] = 0;
    for (int j = 0; j <= pps->num_tile_rows_minus1; j++) {
        rowBd[j + 1] = rowBd[j] + rowHeight[j];
    }
    //see (6-7)
    pps->CtbAddrRsToTs = calloc(slice->PicSizeInCtbsY, 4);
    for (int ctbAddrRs = 0; ctbAddrRs < slice->PicSizeInCtbsY; ctbAddrRs++ ) {
        int tbX = ctbAddrRs % slice->PicWidthInCtbsY;
        int tbY = ctbAddrRs / slice->PicWidthInCtbsY;
        int tileX = -1, tileY = -1;
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
        pps->CtbAddrRsToTs[ctbAddrRs] = 0;
        for (int i = 0; i < tileX; i++) {
            pps->CtbAddrRsToTs[ctbAddrRs] += rowHeight[tileY] * colWidth[i];
        }
        for (int j = 0; j < tileY; j++) {
            pps->CtbAddrRsToTs[ctbAddrRs] += slice->PicWidthInCtbsY * rowHeight[j];
        }
        pps->CtbAddrRsToTs[ctbAddrRs] += ((tbY - rowBd[tileY]) * colWidth[tileX] + tbX - colBd[tileX]);

    }
    //see (6-8)
    pps->CtbAddrTsToRs = calloc(slice->PicSizeInCtbsY, 4);
    for (int ctbAddrRs = 0; ctbAddrRs < slice->PicSizeInCtbsY; ctbAddrRs++ )
        pps->CtbAddrTsToRs[pps->CtbAddrRsToTs[ctbAddrRs]] = ctbAddrRs;
    //see (6-9)
    pps->TileId = calloc(slice->PicSizeInCtbsY, sizeof(uint32_t));
    for (int j = 0, tileIdx = 0; j <= pps->num_tile_rows_minus1; j++) {
        for (int i = 0; i <= pps->num_tile_columns_minus1; i++, tileIdx++) {
            for (int y = rowBd[j]; y < rowBd[j + 1]; y++) {
                for(int x = colBd[i]; x < colBd[i + 1]; x++) {
                    pps->TileId[pps->CtbAddrRsToTs[y * slice->PicWidthInCtbsY+ x]] = tileIdx;
                }
            }
        }
    }

    //see 6.5.2 z-scan order array initialization process
    //see (6-10)
    pps->MinTbAddrZs = init_zscan_array(slice, sps, pps->CtbAddrRsToTs);

    int slice_qpy = pps->init_qp_minus26 + 26 + slice->slice_qp_delta;

    cabac_init_models(slice_qpy, 0);

    /* invoke at the beginning of the decoding process */
    hslice->rps =
    process_reference_picture_set((headr->nal_unit_type == IDR_W_RADL ||
                                  headr->nal_unit_type == IDR_N_LP),
                                  hslice, hps);
    process_reference_picture_lists_construction(hslice, hps);
    process_target_reference_index_for_residual_predication(hslice, hps);

    //see 7.4.7.1 slice_segment_address
    int CtbAddrInRs = slice->slice_segment_address;
    int CtbAddrInTs = pps->CtbAddrRsToTs[CtbAddrInRs];
    VDBG(hevc, "starting CtbAddrInTs %d", CtbAddrInTs);
    int i = 0;
    cabac_dec *d;
    bool first_ctu_in_tile = true, first_ctu_in_row = true, first_ctu_in_slice_segment = true;
    do {
        // see 7-55, 7-56, but slice->entry_point_offset is alreay accumlated
        int firstByte = (i > 0) ? slice->entry_point_offset[i-1]: 0;
        int lastByte = (i < slice->num_entry_point_offsets) ? (slice->entry_point_offset[i] - firstByte): v->len;
        // bits_vec_dump(v);
        VDBG(hevc, "CtbAddrInTs %u, CtbAddrInRs %u, TileId %d", CtbAddrInTs,
             CtbAddrInRs, pps->TileId[CtbAddrInTs]);

        uint32_t xCtb = (CtbAddrInRs % slice->PicWidthInCtbsY) << slice->CtbLog2SizeY;
        uint32_t yCtb = (CtbAddrInRs / slice->PicWidthInCtbsY) << slice->CtbLog2SizeY;
        //see Figure 9-3
        if (first_ctu_in_tile || first_ctu_in_slice_segment) {
            // reset cabac
            first_ctu_in_tile = false;
            first_ctu_in_slice_segment = false;
            d = cabac_dec_init(v);
        } else if ((CtbAddrInRs % slice->PicWidthInCtbsY == 0) && pps->entropy_coding_sync_enabled_flag) {
            //load cabac context with upper-right CTU if it is avaibale and at the start of a line
            int availableFlagT = process_zscan_order_block_availablity(slice, hps, xCtb, yCtb, xCtb + slice->CtbSizeY, yCtb - slice->CtbSizeY);
            if (availableFlagT) {
                sync_process_for_cabac_context(StatCoeff);
            } else {
                // reset cabac
            }
        } else if (first_ctu_in_slice_segment &&
                   slice->dependent_slice_segment_flag) {
            first_ctu_in_slice_segment = false;
        }

        struct ctu * ctu = coding_tree_unit(d, hslice, hps, CtbAddrInTs, CtbAddrInRs, SliceAddrRs, pps->TileId, p);

        // store the second CTU state in a row
        // see Figure 9-4
        if (pps->entropy_coding_sync_enabled_flag &&
            (CtbAddrInRs % slice->PicWidthInCtbsY == 1 ||
                (CtbAddrInRs > 1 &&
                pps->TileId[pps->CtbAddrRsToTs[CtbAddrInRs - 2]] !=
                                        pps->TileId[CtbAddrInTs]))) {
            VDBG(hevc, "storage process for cabac context");
            storage_process_for_cabac_context(StatCoeff);
        }

        end_of_slice_segment_flag = cabac_dec_terminate(d);
        VDBG(hevc, "end_of_slice_segment_flag %d", end_of_slice_segment_flag);
        //bits_vec_dump(v);
        CtbAddrInTs ++;
        CtbAddrInRs = pps->CtbAddrTsToRs[CtbAddrInTs];
        VDBG(hevc, "CtbAddrInTs %d TileId[CtbAddrInTs] %d, "
             "CtbAddrRsToTs[CtbAddrInRs - 1] %d, "
             "TileId[pps->CtbAddrRsToTs[CtbAddrInRs - 1]] %d",
             CtbAddrInTs, pps->TileId[CtbAddrInTs],
             pps->CtbAddrRsToTs[CtbAddrInRs - 1],
             pps->TileId[pps->CtbAddrRsToTs[CtbAddrInRs - 1]]);
        if (!end_of_slice_segment_flag && ((pps->tiles_enabled_flag && pps->TileId[CtbAddrInTs] != pps->TileId[CtbAddrInTs - 1]) ||
                (pps->entropy_coding_sync_enabled_flag && (CtbAddrInRs % slice->PicWidthInCtbsY == 0 ||
                pps->TileId[CtbAddrInTs] != pps->TileId[pps->CtbAddrRsToTs[CtbAddrInRs - 1]])))) {
            uint8_t end_of_subset_one_bit = cabac_dec_terminate(d); // should be equal to 1

            assert(end_of_subset_one_bit == 1);
            bits_vec_dump(v);
            VDBG(hevc,"firstByte %d, lastByte %d, len %zu", firstByte, lastByte, (v->ptr - v->start));

            assert(lastByte == v->ptr - v->start);
            cabac_dec_reset(d);
            i ++;
            // end of entry_point_offset

        }
    } while (!end_of_slice_segment_flag);
    cabac_dec_free(d);
}

static void parse_slice_segment_layer(struct hevc_nalu_header *headr,
                                      struct bits_vec *v,
                                      struct hevc_param_set *hps,
                                      uint8_t **pixels) {
    uint32_t SliceAddrRs;

    struct hevc_slice hslice = {
        .nalu = headr,
    };

    hslice.slice = parse_slice_segment_header(v, headr, hps, &SliceAddrRs);
    VDBG(hevc, "scaling_list_enabled_flag %d", hps->sps->scaling_list_enabled_flag);
    if (hps->sps->scaling_list_enabled_flag) {
        if (hps->pps->pps_scaling_list_data_present_flag) {
            init_scaling_factor(hslice.slice, &hps->pps->list_data);
        } else if (hps->sps->sps_scaling_list_data_present_flag) {
            init_scaling_factor(hslice.slice, hps->sps->list_data);
        }
    }
    // bits_vec_dump(v);
    //we have process the segment header done, reinit for segment_data, move pointer
    bits_vec_reinit_cur(v);
    // bits_vec_dump(v);
    int width = hps->sps->pic_width_in_luma_samples;
    int height = ((hps->sps->pic_height_in_luma_samples + 3 )>>2)<<2;
    int y_stride = ((width + 3) >> 2) << 2;
    int uv_stride = y_stride >> 1;
    int16_t *Y = malloc(height * y_stride);
    int16_t *U = malloc(height * uv_stride);
    int16_t *V = malloc(height * uv_stride);
    struct picture p = {
        .Y = Y,
        .U = U,
        .V = V,
        .y_stride = y_stride,
        .uv_stride = uv_stride,
    };
    int Log2MinPUSize = hslice.slice->MinCbLog2SizeY - 1;
    int PicWidthInMinPUs = hslice.slice->PicWidthInCtbsY
                           << (hslice.slice->CtbLog2SizeY - Log2MinPUSize);
    int PicHeightInMinPUs = hslice.slice->PicHeightInCtbsY
                            << (hslice.slice->CtbLog2SizeY - Log2MinPUSize);
    p.IntraPredModeY =
        calloc(PicWidthInMinPUs * PicHeightInMinPUs, sizeof(uint8_t));
    p.IntraPredModeC =
        calloc(PicWidthInMinPUs * PicHeightInMinPUs, sizeof(uint8_t));
    p.CuPredMode = calloc(hslice.slice->PicWidthInMinCbsY *
                              hslice.slice->PicHeightInMinCbsY,
                          sizeof(uint8_t));
    p.pcm_flag = calloc(hslice.slice->PicWidthInMinCbsY *
                            hslice.slice->PicHeightInMinCbsY,
                        sizeof(uint8_t));
    p.qpy = calloc(hslice.slice->PicWidthInMinCbsY *
                       hslice.slice->PicHeightInMinCbsY,
                   sizeof(uint8_t));

    int PicWidthInTbsY = hslice.slice->PicWidthInCtbsY
        << (hslice.slice->CtbLog2SizeY - hslice.slice->MinTbLog2SizeY);
    int PicHeightInTbsY = hslice.slice->PicHeightInCtbsY
        << (hslice.slice->CtbLog2SizeY - hslice.slice->MinTbLog2SizeY);

    p.split_transform_flag = calloc(PicWidthInTbsY * PicHeightInTbsY, sizeof(uint8_t));
    parse_slice_segment_data(v, &hslice, hps, SliceAddrRs, &p);
    rbsp_trailing_bits(v);

    // YUV420_to_BGRA32(*pixels, ((y_stride * 32 + 32 - 1) >> 5) << 2,
    //                  Y, U, V, y_stride, uv_stride, width, height);
}

struct hevc_param_set hps;

void
parse_nalu(uint8_t *data, int len, uint8_t **pixels)
{
    struct hevc_nalu_header h;
    h.forbidden_zero_bit = (data[0] & 0x80) >> 7;
    h.nal_unit_type = (data[0] & 0x7E) >> 1;
    h.nuh_layer_id = (((data[0] & 1) << 5) | ((data[1] & 0xF8) >> 3));
    h.nuh_temporal_id = data[1] & 0x7 - 1;
    VDBG(hevc, "len %d f %d type %d, layer id %d, temp id %d", len, h.forbidden_zero_bit,
        h.nal_unit_type, h.nuh_layer_id, h.nuh_temporal_id);
    assert(h.forbidden_zero_bit == 0);

    uint8_t *rbsp = malloc(len - 2);

    // See 7.3.1.1
    int nrbsp = 0;
    uint8_t *p = data + 2;
    for (int l = 2; l < len; l ++) {
        if ((l + 2 < len) && ((p[0]<<16| p[1]<<8| p[2]) == 0x3)) {
            rbsp[nrbsp++] = p[0];
            rbsp[nrbsp++] = p[1];
            l += 2;
            /* skip third byte */
            p += 3;
        } else {
            rbsp[nrbsp++] = *p;
            p++;
        }
    }

    struct bits_vec *v = bits_vec_alloc(rbsp, nrbsp, BITS_MSB);

    switch (h.nal_unit_type) {
    case IDR_W_RADL:
    case IDR_N_LP:
        // hexdump(stdout, "data: ", "", data, 32);
        // printf("nrbsp %d\n", nrbsp);
        // hexdump(stdout, "rbsp: ", "", rbsp, 32);
        parse_slice_segment_layer(&h, v, &hps, pixels);
        break;
    case VPS_NUT:
        hps.vps = parse_vps(v);
        break;
    case SPS_NUT:
        hps.sps = parse_sps(&h, v, hps.vps);
        if (!hps.sps) {
            VABORT(hevc, "can no parse sps correctly");
        }
        break;
    case PPS_NUT:
        hps.pps = parse_pps(v);
        if (!hps.pps) {
            VABORT(hevc, "can no parse pps correctly");
        }
        break;
    case PREFIX_SEI_NUT:
    case SUFFIX_SEI_NUT:
        /* this is not necessary for decoding a picture */
        parse_sei(v);
        break;
    default:
        VDBG(hevc, "unhandle nal_unit_type %d", h.nal_unit_type);
        break;
    }
    bits_vec_free(v);
}
