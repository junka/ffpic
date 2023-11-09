#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "cabac.h"
#include "bitstream.h"
#include "vlog.h"

VLOG_REGISTER(cabac, DEBUG)


//see Table 9-53 state transition for next stat of lsp and mps
static uint8_t NextStateMPS[STATE_NUM] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 62, 63
};

static uint8_t NextStateLPS[STATE_NUM] = {
    0,  0,  1,  2,  2,  4,  4,  5,  6,  7,  8,  9,  9,  11, 11, 12,
    13, 13, 15, 15, 16, 16, 18, 18, 19, 19, 21, 21, 22, 22, 23, 24,
    24, 25, 26, 26, 27, 27, 28, 29, 29, 30, 30, 30, 31, 32, 32, 33,
    33, 33, 34, 34, 35, 35, 35, 36, 36, 36, 37, 37, 37, 38, 38, 63
};

//see Table 9-52 Lps table on (state, range)
static uint8_t LPSTable[1 << STATE_BITS][RANGE_NUM] = {
    {128, 176, 208, 240}, {128, 167, 197, 227}, {128, 158, 187, 216},
    {123, 150, 178, 205}, {116, 142, 169, 195}, {111, 135, 160, 185},
    {105, 128, 152, 175}, {100, 122, 144, 166}, {95, 116, 137, 158},
    {90, 110, 130, 150},  {85, 104, 123, 142},  {81, 99, 117, 135},
    {77, 94, 111, 128},   {73, 89, 105, 122},   {69, 85, 100, 116},
    {66, 80, 95, 110},    {62, 76, 90, 104},    {59, 72, 86, 99},
    {56, 69, 81, 94},     {53, 65, 77, 89},     {51, 62, 73, 85},
    {48, 59, 69, 80},     {46, 56, 66, 76},     {43, 53, 63, 72},
    {41, 50, 59, 69},     {39, 48, 56, 65},     {37, 45, 54, 62},
    {35, 43, 51, 59},     {33, 41, 48, 56},     {32, 39, 46, 53},
    {30, 37, 43, 50},     {29, 35, 41, 48},     {27, 33, 39, 45},
    {26, 31, 37, 43},     {24, 30, 35, 41},     {23, 28, 33, 39},
    {22, 27, 32, 37},     {21, 26, 30, 35},     {20, 24, 29, 33},
    {19, 23, 27, 31},     {18, 22, 26, 30},     {17, 21, 25, 28},
    {16, 20, 23, 27},     {15, 19, 22, 25},     {14, 18, 21, 24},
    {14, 17, 20, 23},     {13, 16, 19, 22},     {12, 15, 18, 21},
    {12, 14, 17, 20},     {11, 14, 16, 19},     {11, 13, 15, 18},
    {10, 12, 15, 17},     {10, 12, 14, 16},     {9, 11, 13, 15},
    {9, 11, 12, 14},      {8, 10, 12, 14},      {8, 9, 11, 13},
    {7, 9, 11, 12},       {7, 9, 10, 12},       {7, 8, 10, 11},
    {6, 8, 9, 11},        {6, 7, 9, 10},        {6, 7, 8, 9},
    {2, 2, 2, 2},
};

struct ctx_model {
    uint8_t mpsbit : 1;
    uint8_t state : 7; // state need 6 bits, put mps or lps at the least bit

    // below is need for TR
    uint8_t bypass; // 6 bits 1 for bypass, greater than 5
                    //  should keep the same with 6th
    uint8_t bypass_len;
};

static int initValue_sao_merge[3] = {153, 153, 153};
static int initValue_sao_type_idx[3] = {200, 185, 160};
static int initValue_split_cu_flag[3][3] = {
    {139, 141, 157},
    {107, 139, 126},
    {107, 139, 126},
};
static int initValue_cu_transquant_bypass_flag[3] = {154, 154, 154};
static int initValue_cu_skip_flag[2][3] = {
    {197, 185, 201},
    {197, 185, 201},
};
static int initValue_palette_mode_flag[3] = {154, 154, 154};
static int initValue_pred_mode_flag[2] = {149, 134};
static int initValue_part_mode[9] = {184, 154, 139, 154, 154,
                                     154, 139, 154, 154};
static int initValue_prev_intra_luma_pred_flag[3] = {184, 154, 183};
static int initValue_intra_chrome_pred_mode[3] = {63, 152, 152};
static int initValue_rqt_root_cbf[2] = {79, 79};

static int initValue_tu_residual_act_flag[3] = {154, 154, 154};
static int initValue_merge_flag[2] = {110, 154};
static int initValue_merge_index[2] = {122, 137};
static int initValue_inter_pred_idc[2][5] = {95, 79, 63, 31, 31, 95, 79, 63, 31, 31};
static int initValue_ref_idx[2][2] = {{153, 153}, {153, 153}};
static int initValue_mvp_flag[2] = {168, 168};
static int initValue_split_transform_flag[3][3] = {
    {153, 138, 138},
    {124, 138, 94},
    {224, 167, 122}
};
static int initValue_cbf_luma[3][2] = {
    {111, 141},
    {153,111,},
    {153, 111}
};
// where the last element for each initType is not 4 but 12, 13, 14 
static int initValue_cbf_cb_cr[3][5] = {
    {94, 138, 182, 154, 154},
    {149, 107, 167, 154, 154},
    {149, 92, 167, 154, 154}
};
static int initValue_abs_mvd_greater0_flag[2] = {140, 169};
static int initValue_abs_mvd_greater1_flag[2] = {198, 198};
static int initValue_log2_res_scale_abs_plus1[3][8] = {
    {154, 154, 154, 154, 154, 154, 154, 154},
    {154, 154, 154, 154, 154, 154, 154, 154},
    {154, 154, 154, 154, 154, 154, 154, 154}
};
static int initValue_res_scale_sign_flag[3][2] = {
    {154, 154},
    {154, 154},
    {154, 154}
};
static int initValue_transform_skip_flag[3][2] = {
    {139, 139},
    {139, 139},
    {139, 139}
};
static int initValue_explicit_rdpcm_flag[2][3] = {
    {139, 139, 139},
    {139, 139, 139},
};
static int initValue_explicit_rdpcm_dir_flag[2][3] = {
    {139, 139, 139},
    {139, 139, 139},
};
static int initValue_last_sig_coeff_x_prefix[3][18] = {
    {110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111, 79, 108, 123, 63},
    {125, 110, 94, 110, 95, 79, 125, 111, 110, 78, 110, 111, 111, 95, 94, 108, 123, 108},
    {125, 110, 124, 110, 95, 94, 125, 111, 111, 79, 125, 126, 111, 111, 79, 108, 123, 93}
};

static int initValue_last_sig_coeff_y_prefix[3][18] = {
    {110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111, 79, 108, 123, 63},
    {125, 110, 94, 110, 95, 79, 125, 111, 110, 78, 110, 111, 111, 95, 94, 108, 123, 108},
    {125, 110, 124, 110, 95, 94, 125, 111, 111, 79, 125, 126, 111, 111, 79, 108, 123, 93}
};
static int initValue_coded_sub_block_flag[3][4] = {
    {91, 171, 134, 141},
    {121, 140, 61, 154},
    {121, 140, 61, 154}
};
static int initValue_sig_coeff_flag[3][42] = {
    {
        111, 111, 125, 110, 110, 94,  124, 108, 124, 107, 125, 141, 179, 153,
        125, 107, 125, 141, 179, 153, 125, 107, 125, 141, 179, 153, 125, 140,
        139, 182, 182, 152, 136, 152, 136, 153, 136, 139, 111, 136, 139, 111,
    },
    {
        155, 154, 139, 153, 139, 123, 123, 63,  153, 166, 183, 140, 136, 153,
        154, 166, 183, 140, 136, 153, 154, 166, 183, 140, 136, 153, 154, 170,
        153, 123, 123, 107, 121, 107, 121, 167, 151, 183, 140, 151, 183, 140,
        
    },
    {
        170, 154, 139, 153, 139, 123, 123, 63,  124, 166, 183, 140, 136, 153,
        154, 166, 183, 140, 136, 153, 154, 166, 183, 140, 136, 153, 154, 170,
        153, 138, 138, 122, 121, 122, 121, 167, 151, 183, 140, 151, 183, 140,
        
    },
};
static int initValue_sig_coeff_flag1[3][2] = {
    {141, 111}, {140, 140}, {140, 140}
};

static int initValue_coeff_abs_level_greater1_flag[3][24] = {
    {
        140, 92,  137, 138, 140, 152, 138, 139, 153, 74,  149, 92,
        139, 107, 122, 152, 140, 179, 166, 182, 140, 227, 122, 197
    }, 
    {
        154, 196, 196, 167, 154, 152, 167, 182, 182, 134, 149, 136,
        153, 121, 136, 137, 169, 194, 166, 167, 154, 167, 137, 182
    },
    {
        154, 196, 167, 167, 154, 152, 167, 182, 182, 134, 149, 136,
        153, 121, 136, 122, 169, 208, 166, 167, 154, 152, 167, 182
    }
};

static int initValue_coeff_abs_level_greater2_flags[3][6] = {
    {138, 153, 136, 167, 152, 152},
    {107, 167, 91, 122, 107, 167},
    {107, 167, 91,  107, 107, 167}
};
static int initValue_palette_run_prefix[3][8] = {
    {154, 154, 154, 154, 154, 154, 154, 154},
    {154, 154, 154, 154, 154, 154, 154, 154},
    {154, 154, 154, 154, 154, 154, 154, 154}
};
static int initValue_copy_above_palette_indices_flag[3] = {154, 154, 154};
static int initValue_copy_above_indeces_for_final_run_flag[3] = {154, 154, 154};
static int initValue_palette_transpose_flag[3] = {154, 154, 154};
static int initValue_cu_qp_delta_abs[3][2] = {
    {154, 154},
    {154, 154},
    {154, 154}
};
static int initValue_cu_chroma_qp_offset_flag[3] = {154, 154, 154};
static int initValue_cu_chroma_qp_offset_idx[3] = {154, 154, 154};

//extension for 3d
static int initValue_skip_intra_flag[3] = {185, 185, 185};
static int initValue_no_dim_flag[3] = {154, 141, 155};
static int initValue_depth_intra_mode_idx_flag[3] = {154, 154, 154};
static int initValue_skip_intra_mode_idx[3] = {137, 137, 137};
static int initValue_dbbp_flag[3] = {154, 154, 154};
static int initValue_dc_only_flag[3] = {154, 154, 154};
static int initValue_iv_res_pred_weight_idx[2][3] = {
    {162, 153, 162},
    {162, 153, 162}
};
static int initValue_illu_comp_flag[2] = {154, 154};
static int initValue_depth_dc_present_flag[3] = {0, 0, 64};
static int initValue_depth_dc_abs[3] = {154, 154, 154};

// see 9.9.3.2.2
// see table 9-4 to 9-35
// see table I.4, to I.14
//assume we only have slice_type == I, which initType = 0 (see 9-7)
static struct ctx_model ctx_table[CTX_TYPE_MAX_NUM];


static void init_bypass_flag(struct ctx_model *ctx, uint8_t flags, uint8_t len)
{
    ctx->bypass = flags;
    ctx->bypass_len = len;
}

static void init_model_ctx(struct ctx_model *ctx, int qpy, int initValue) {
    // see 9-4
    int slopeIdx = initValue >> 4;
    int offsetIdx = initValue & 15;
    // see 9-5
    int m = slopeIdx * 5 - 45;
    int n = (offsetIdx << 3) - 16;
    //see 9-6
    int preCtxState = clip3(1, 126, ((m * clip3(0, 51, qpy))>>4)+n);
    ctx->mpsbit = (preCtxState <= 63) ? 0 : 1;
    ctx->state = ctx->mpsbit ? (preCtxState - 64) : (63 - preCtxState);
    assert(ctx->state <= 62);
}


void cabac_init_models(int qpy, int initType)
{
    //see table 9-48
    struct ctx_model *ctx = ctx_table;
    init_bypass_flag(ctx + CTX_TYPE_ALL_BYPASS, 0x3F, 6);
    init_model_ctx(ctx + CTX_TYPE_SAO_MERGE, qpy,
                       initValue_sao_merge[initType]);

    init_model_ctx(ctx + CTX_TYPE_SAO_TYPE_INDEX, qpy,
                   initValue_sao_type_idx[initType]);
    init_bypass_flag(ctx + CTX_TYPE_SAO_TYPE_INDEX, 1<<1, 2);
    for (int i = 0; i < 3; i ++) {
        init_model_ctx(ctx+CTX_TYPE_SPLIT_CU_FLAG + i, qpy,
                       initValue_split_cu_flag[initType][i]);
        VDBG(cabac, "state %d, bit %d", ctx[CTX_TYPE_SPLIT_CU_FLAG+i].state,
             ctx[CTX_TYPE_SPLIT_CU_FLAG+i].mpsbit);
    }
    init_model_ctx(ctx + CTX_TYPE_CU_TRANSQUANT_BYPASS_FLAG, qpy,
                   initValue_cu_transquant_bypass_flag[initType]);

    init_model_ctx(ctx + CTX_TYPE_CU_PALETTE_MODE_FLAG, qpy,
                   initValue_palette_mode_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_CU_PART_MODE, qpy,
                   initValue_part_mode[initType]);
    init_bypass_flag(ctx + CTX_TYPE_CU_PART_MODE, 1 << 3, 4);
    init_model_ctx(ctx + CTX_TYPE_CU_PREV_INTRA_LUMA_PRED_FLAG, qpy,
                   initValue_prev_intra_luma_pred_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_CU_INTRA_CHROME_PRED_MODE, qpy,
                   initValue_intra_chrome_pred_mode[initType]);
    init_bypass_flag(ctx + CTX_TYPE_CU_INTRA_CHROME_PRED_MODE, 3 << 1, 3);
    for (int i = 0; i < 3; i++) {
        init_model_ctx(ctx + CTX_TYPE_TU_SPLIT_TRANSFORM_FLAG + i, qpy,
                       initValue_split_transform_flag[initType][i]);
    }
    for (int i = 0; i < 2; i++) {
        init_model_ctx(ctx + CTX_TYPE_TU_CBF_LUMA + i, qpy,
                       initValue_cbf_luma[initType][i]);
    }
    for (int i = 0; i < 5; i++) {
        init_model_ctx(ctx + CTX_TYPE_TU_CBF_CBCR + i, qpy,
                       initValue_cbf_cb_cr[initType][i]);
    }
    for (int i = 0; i < 8; i++) {
        init_model_ctx(ctx + CTX_TYPE_CROSS_COMP_LOG2_RES_SCALE_ABS + i, qpy,
                       initValue_log2_res_scale_abs_plus1[initType][i]);
    }
    for (int i = 0; i < 2; i++) {
        init_model_ctx(ctx + CTX_TYPE_CROSS_COMP_RES_SCALE_SIGN + i, qpy,
                       initValue_res_scale_sign_flag[initType][i]);
    }
    for (int i = 0; i < 2; i++) {
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_TRANSFORM_SKIP + i, qpy,
                       initValue_transform_skip_flag[initType][i]);
    }
    for (int i = 0; i < 18; i++) {
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_X_PREFIX + i,
                       qpy, initValue_last_sig_coeff_x_prefix[initType][i]);
        init_bypass_flag(ctx + CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_X_PREFIX + i, 0, 6);
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_Y_PREFIX + i,
                       qpy, initValue_last_sig_coeff_y_prefix[initType][i]);
        init_bypass_flag(ctx + CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_Y_PREFIX + i, 0, 6);
    }
    for (int i = 0; i < 4; i++) {
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_CODED_SUB_BLOCK_FLAG + i,
                       qpy, initValue_coded_sub_block_flag[initType][i]);
    }
    for (int i = 0; i < 42; i++) {
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_SIG_COEFF_FLAG + i, qpy,
                       initValue_sig_coeff_flag[initType][i]);
    }
    // notice that 43, 44 is 126, 127
    for (int i = 0; i < 2; i ++) {
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_SIG_COEFF_FLAG + i + 42, qpy,
                       initValue_sig_coeff_flag1[initType][i]);
    }
    for (int i = 0; i < 24; i++) {
        init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER1 + i,
                       qpy,
                       initValue_coeff_abs_level_greater1_flag[initType][i]);
    }
    for (int i = 0; i < 6; i++) {
        init_model_ctx(
            ctx + CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER2 + i, qpy,
            initValue_coeff_abs_level_greater2_flags[initType][i]);
    }
    for (int i = 0; i < 8; i++) {
        init_model_ctx(ctx + CTX_TYPE_PALETTE_CODING_PALETTE_RUN_PREFIX + i, qpy,
                       initValue_palette_run_prefix[initType][i]);
    }
    init_model_ctx(ctx + CTX_TYPE_PALETTE_CODING_COPY_ABOVE_PALLETTE_INDICES,
                   qpy, initValue_copy_above_palette_indices_flag[initType]);
    init_model_ctx(
        ctx + CTX_TYPE_PALETTE_CODING_COPY_ABOVE_INDICES_FOR_FINAL_RUN, qpy,
        initValue_copy_above_indeces_for_final_run_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_PALETTE_CODING_PALETTE_TRANSPOSE_FLAG, qpy,
                   initValue_palette_transpose_flag[initType]);
    for (int i = 0; i < 2; i++) {
        init_model_ctx(ctx + CTX_TYPE_DELTA_QP_CU_QP_DELTA_ABS + i, qpy,
                       initValue_cu_qp_delta_abs[initType][i]);
        init_bypass_flag(ctx + CTX_TYPE_DELTA_QP_CU_QP_DELTA_ABS + i, 1 << 5,
                         6);
    }
    init_model_ctx(ctx + CTX_TYPE_CHROMA_QP_OFFSET_CU_CHROME_QP_OFFSET_FLAG,
                   qpy, initValue_cu_chroma_qp_offset_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_CHROMA_QP_OFFSET_CU_CHROME_QP_OFFSET_IDX, qpy,
                   initValue_cu_chroma_qp_offset_idx[initType]);

    init_model_ctx(ctx + CTX_TYPE_TU_RESIDUAL_ACT_FLAG, qpy,
                   initValue_tu_residual_act_flag[initType]);
    if (initType > 0) {
        for (int i = 0; i < 3; i++) {
          init_model_ctx(ctx + CTX_TYPE_CU_SKIP_FLAG + i, qpy,
                         initValue_cu_skip_flag[initType - 1][i]);
        }
        init_model_ctx(ctx + CTX_TYPE_CU_PRED_MODE_FLAG, qpy,
                    initValue_pred_mode_flag[initType - 1]);
        init_model_ctx(ctx + CTX_TYPE_CU_RQT_ROOT_CBF, qpy,
                    initValue_rqt_root_cbf[initType - 1]);
        init_model_ctx(ctx + CTX_TYPE_PU_MERGE_FLAG, qpy,
                    initValue_merge_flag[initType - 1]);
        init_model_ctx(ctx + CTX_TYPE_PU_MERGE_IDX, qpy,
                    initValue_merge_index[initType - 1]);
        for (int i = 0; i < 5; i++) {
            init_model_ctx(ctx + CTX_TYPE_PU_INTER_PRED_IDC + i, qpy,
                        initValue_inter_pred_idc[initType - 1][i]);
        }
        for (int i = 0; i < 2; i++) {
            init_model_ctx(ctx + CTX_TYPE_PU_REF_IDX + i, qpy,
                           initValue_ref_idx[initType - 1][i]);
        }
        init_model_ctx(ctx + CTX_TYPE_PU_MVP_FLAG, qpy,
                    initValue_mvp_flag[initType - 1]);

        init_model_ctx(ctx + CTX_TYPE_MV_ABS_MVD_GREATER0, qpy,
                    initValue_abs_mvd_greater0_flag[initType - 1]);

        init_model_ctx(ctx + CTX_TYPE_MV_ABS_MVD_GREATER1,
                    qpy, initValue_abs_mvd_greater1_flag[initType - 1]);
        for (int i = 0; i < 3; i++) {
            init_model_ctx(ctx + CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM + i,
                           qpy, initValue_explicit_rdpcm_flag[initType - 1][i]);
            init_model_ctx(
                ctx + CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM_DIR + i, qpy,
                initValue_explicit_rdpcm_dir_flag[initType - 1][i]);
        }
    }

    //extensions for 3D hevc
    init_model_ctx(ctx + CTX_TYPE_3D_SKIP_INTRA_FLAG, qpy, initValue_skip_intra_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_3D_NO_DIM_FLAG, qpy,
                   initValue_no_dim_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_3D_DEPTH_INTRA_MODE_IDX_FLAG, qpy,
                   initValue_depth_intra_mode_idx_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_3D_SKIP_INTRA_MODE_IDX, qpy,
                   initValue_skip_intra_mode_idx[initType]);

    init_model_ctx(ctx + CTX_TYPE_3D_DBBP_FLAG, qpy,
                   initValue_dbbp_flag[initType]);

    init_model_ctx(ctx + CTX_TYPE_3D_DC_ONLY_FLAG, qpy,
                   initValue_dc_only_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_3D_DEPTH_DC_PRESENT_FLAG, qpy,
                   initValue_depth_dc_present_flag[initType]);
    init_model_ctx(ctx + CTX_TYPE_3D_DEPTH_DC_ABS, qpy,
                   initValue_depth_dc_abs[initType]);
    if (initType > 0) {
        for (int i = 0; i < 3; i++) {
            init_model_ctx(ctx + CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX + i, qpy,
                        initValue_iv_res_pred_weight_idx[initType - 1][i]);
        }
        init_model_ctx(ctx + CTX_TYPE_3D_ILLU_COMP_FLAG, qpy,
                    initValue_illu_comp_flag[initType - 1]);
    }
}

cabac_dec *
cabac_dec_init(struct bits_vec *v)
{
    cabac_dec *dec = malloc(sizeof(*dec));
    dec->bits = v;
    dec->count = 8;
    dec->value = READ_BITS(v, 16);
    dec->range = 510;

    return dec;
}

//see Figure 9-7
static void
renormD(cabac_dec *dec)
{
    // static uint8_t RenormTable[32] = {
    //     6,  5,  4,  4,
    //     3,  3,  3,  3,
    //     2,  2,  2,  2,
    //     2,  2,  2,  2,
    //     1,  1,  1,  1,
    //     1,  1,  1,  1,
    //     1,  1,  1,  1,
    //     1,  1,  1,  1
    // };

    //scaledRange is dec->range << 7
    while ((dec->range) < (256)) {
        dec->range = dec->range << 1;
        dec->value <<= 1;
        //refill value when all read out
        if (--dec->count <= 0) {
            dec->count += 8;
            dec->value += READ_BITS(dec->bits, 8);
        }
    }
}

void
cabac_dec_free(cabac_dec *dec)
{
    // int i = 0;
    // for (i = 0; i < dec_num; i ++) {
    //     if (dec_list[i] == dec) {
    //         break;
    //     }
    // }
    // if (i < dec_num) {
    //     for (; i < dec_num - 1; i ++) {
    //         dec_list[i] = dec_list[i+1];
    //     }
    //     dec_list[dec_num - 1] = NULL;
    //     dec_num --;
    //     free(dec);
    // }
    // bits_vec_free(dec->bits);
    free(dec);
}

//see Figure 9-8
int
cabac_dec_bypass(cabac_dec *dec)
{
    int binVal = 0;
    /*Figure 9-8 */
    dec->value <<= 1;
    if (--dec->count <= 0) {
        dec->count += 8;
        dec->value += READ_BITS(dec->bits, 8);
    }
    // we have the last 7 bits for buffer TBD
    uint32_t scaledRange = dec->range << 7;
    VDBG(cabac, "scaledRange %d, value %d", scaledRange, dec->value);
    if (dec->value >= scaledRange) {
        binVal = 1;
        dec->value -= scaledRange;
    }
    VDBG(cabac, "bypass v %x r %x c %d: binVal %d", dec->value, dec->range,
         dec->count, binVal);
    return binVal;
}

int cabac_dec_bypass_n(cabac_dec *dec, int n)
{
    int val = 0;
    while (n--) {
        val <<= 1;
        val |= cabac_dec_bypass(dec); 
    }
    return val;
}

//see 9.3.3.5 FL
int cabac_dec_bypass_fl(cabac_dec *dec, int max)
{
    int fl = log2ceil(max + 1);
    return cabac_dec_bypass_n(dec, fl);
}

//see 9.3.3.6
int cabac_dec_bypass_tb(cabac_dec *dec, int max)
{
    //see 9-17
    int n = max + 1;
    int k = log2floor(n);
    int u = (1 << (k+1)) - n;
    int v = 0;
    v = cabac_dec_bypass_n(dec, k);
    if (v >= u) {
        v = (v << 1) | cabac_dec_bypass(dec);
        v -= u;
    }
    return v;
}


int
cabac_dec_terminate(cabac_dec *dec)
{
    /*Figure 9-9 */
    int binVal = 0;
    dec->range -= 2;
    uint32_t scaledRange = dec->range << 7;
    if (dec->value >= scaledRange) {
        binVal = 1;
    } else {
        binVal = 0;
        renormD(dec);
    }
    VDBG(cabac, "term bit %d, value %x, range %x", binVal, dec->value, dec->range);
    return binVal;
}

// see 9.3.4.3
int
cabac_dec_decision(cabac_dec *dec, int ctx_tid)
{
    struct ctx_model *m = ctx_table+ctx_tid;
    int binVal;
    uint8_t state = m->state;
    uint32_t rangelps = LPSTable[state][(dec->range >> 6) & 3];
    VDBG(cabac, "decision tid %d, state %d,%d ", ctx_tid, state,
         (dec->range >> 6) - 4);
    dec->range -= rangelps;
    uint32_t scaledRange = dec->range << 7;
    VDBG(cabac, "decision state %d,%d, rlps %d, v %x scr %x, count %d", state,
         (dec->range >> 6) & 3, rangelps, dec->value, scaledRange, dec->count);
    if (dec->value < scaledRange) {
        //MPS (Most Probable Symbol)
        binVal = m->mpsbit;
        m->state = NextStateMPS[state];
    } else {
        //LPS (Least Probable Symbol)
        binVal = 1 - m->mpsbit;
        m->state = NextStateLPS[state];
        // int numbits = RenormTable[rangelps>>3];
        // dec->value = (dec->value - scaledRange) << numbits;
        // dec->range = rangelps << numbits;
        // dec->count -= numbits;
        dec->value = dec->value - scaledRange;
        dec->range = rangelps;
        if (state == 0) {
            m->mpsbit = 1 - m->mpsbit;
        }
    }
    renormD(dec);
    VDBG(cabac, "decision v %x r %x state %d: binVal %d", dec->value, dec->range,
         m->state, binVal);

    return binVal;
}

int
cabac_dec_bin(cabac_dec *dec, int tid, int bypass)
{
    if (bypass) {
        return cabac_dec_bypass(dec);
    }

    return cabac_dec_decision(dec, tid);
}


static inline int
ctx_bypass_flags(int ctx_idx, int bin_idx) {
    struct ctx_model *m = &ctx_table[ctx_idx];
    if (bin_idx > 5) {
        bin_idx = 5;
    }
    if (bin_idx >= m->bypass_len) {
        return -1;
    }
    return !!(m->bypass & (1 << bin_idx));
}

static int ctx_only_one(int ctx_idx, int binIdx) {
    return ctx_idx;
}

// see 9.3.3.2
// it is varibale length
// most ctxIdc start with 0 and followed by na or bypass,
// another common pattern is that ctxInc is the binIdx sequence 0, 1, 2
// all other patterns should be handled differenctly, like 0, 1, 1, 1 (cu_qp_delta_abs), or 0,1,3(part_mode)
int cabac_dec_tr(cabac_dec *dec, int tid, int cMax, int cRiceParam,
                 cabac_get_ctxInc ctxInc_cb) {
    struct ctx_model *m;
    cabac_get_ctxInc cb = ctxInc_cb;
    if (!cb) {
        cb = ctx_only_one;
    }
    // see 9-11
    int t = cMax >> cRiceParam;
    int prefix = 0, suffix = 0, i = 0;
    int binIdx = 0;
    VDBG(cabac, "tid %d, (binIdx %d)flag %d, range %x, value %x", tid,
         binIdx, ctx_bypass_flags(cb(tid, binIdx), binIdx),
         dec->range, dec->value);
    while (ctx_bypass_flags(cb(tid, binIdx), binIdx) >= 0 &&
           cabac_dec_bin(dec, cb(tid, binIdx),
                         ctx_bypass_flags(cb(tid, binIdx), binIdx)) == 1 &&
           prefix < t) {
        binIdx++;
        prefix++;
    }
    VDBG(cabac, "prefix %d, t %d, binIdx %d(%d)", prefix, t, binIdx,
         cb(tid, binIdx) - tid);
    if (prefix >= t) {
        // the bin string length cMax >> cRiceParam with all bins equal to 1
        return cMax;
    } else {
        binIdx++;
        // if prefix < cMax >> cRiceParam, the prefix bin string
        // is a bit string of length prefix + 1, then bins for binIdx less than prefix are equal to 1
        // the bin
        // assert(ctx_bypass_flags(cb(tid, binIdx), binIdx) == 1);
        // when cMax > symbolVal and cRiceParam > 0, suffix is present
        if (cRiceParam > 0) {
            // VDBG(cabac, "(%d,binIdx %d)flag %d, state %d, range %x, value %x",
            //      m->bypass, binIdx, ctx_bypass_flags(tid, binIdx), m->state, dec->range,
            //      dec->value);
            suffix = cabac_dec_bypass_fl(dec, (1 << cRiceParam) - 1);

            VDBG(cabac, "suffix %d, t %d, binIdx %d(%d)", suffix, t, binIdx,
                 cb(tid, binIdx) - tid);
        }
        //see 9-11, reverse the equation
        return  (prefix << cRiceParam) + suffix;
    }
}

//see 9.3.3.4, 9-16
int cabac_dec_egk(cabac_dec *dec, int kth, int max_pre_ext_len,
                  int trunc_suffix_len) {
    int pre_ext_len = 0;
    int escape_length;
    int val = 0;
    while ((pre_ext_len < max_pre_ext_len) && cabac_dec_bypass(dec))
        pre_ext_len++;
    if (pre_ext_len == max_pre_ext_len)
        escape_length = trunc_suffix_len;
    else
        escape_length = pre_ext_len + kth;
    while (escape_length-- > 0) {
        val = (val << 1) + cabac_dec_bypass(dec);
    }
    val += ((1 << pre_ext_len) - 1) << kth;
    return val;
}

void cabac_dec_reset(cabac_dec *dec)
{
    dec->range = 510;
}