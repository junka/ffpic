#ifndef _AV1_TABLES_H_
#define _AV1_TABLES_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum av1_block_size {
    BLOCK_4X4, BLOCK_4X8, BLOCK_8X4, BLOCK_8X8,
    BLOCK_8X16, BLOCK_16X8, BLOCK_16X16, BLOCK_16X32,
    BLOCK_32X16, BLOCK_32X32, BLOCK_32X64, BLOCK_64X32,
    BLOCK_64X64, BLOCK_64X128, BLOCK_128X64, BLOCK_128X128,
    BLOCK_4X16, BLOCK_16X4, BLOCK_8X32, BLOCK_32X8,
    BLOCK_16X64, BLOCK_64X16, BLOCK_INVALID
};

enum av1_tx_size {
    TX_4X4, TX_8X8, TX_16X16, TX_32X32, TX_64X64,
    TX_4X8, TX_8X4, TX_8X16, TX_16X8, TX_16X32, TX_32X16,
    TX_32X64, TX_64X32, TX_4X16, TX_16X4, TX_8X32, TX_32X8,
    TX_16X64, TX_64X16
};

enum av1_intra_mode {
    DC_PRED, V_PRED, H_PRED, D45_PRED, D135_PRED, D113_PRED,
    D157_PRED, D203_PRED, D67_PRED, SMOOTH_PRED, SMOOTH_V_PRED,
    SMOOTH_H_PRED, PAETH_PRED, UV_CFL_PRED,
    FILTER_DC_PRED, FILTER_V_PRED, FILTER_H_PRED, FILTER_D157_PRED,
    FILTER_PAETH_PRED
};

enum av1_tx_type {
    DCT_DCT, ADST_DCT, DCT_ADST, ADST_ADST,
    FLIPADST_DCT, DCT_FLIPADST, FLIPADST_FLIPADST, ADST_FLIPADST,
    FLIPADST_ADST, IDTX, V_DCT, H_DCT, V_ADST, H_ADST,
    V_FLIPADST, H_FLIPADST
};

#ifdef __cplusplus
}
#endif

#ifndef BLOCK_SIZES
#define BLOCK_SIZES 22
#endif
#ifndef BLOCK_SIZE_GROUPS
#define BLOCK_SIZE_GROUPS 4
#endif
#ifndef BR_CDF_SIZE
#define BR_CDF_SIZE 4
#endif
#ifndef BWD_REFS
#define BWD_REFS 3
#endif
#ifndef CFL_ALPHABET_SIZE
#define CFL_ALPHABET_SIZE 16
#endif
#ifndef CFL_ALPHA_CONTEXTS
#define CFL_ALPHA_CONTEXTS 6
#endif
#ifndef CFL_JOINT_SIGNS
#define CFL_JOINT_SIGNS 8
#endif
#ifndef CLASS0_SIZE
#define CLASS0_SIZE 2
#endif
#ifndef COEFF_BASE_RANGE
#define COEFF_BASE_RANGE 12
#endif
#ifndef COEFF_CDF_Q_CTXS
#define COEFF_CDF_Q_CTXS 4
#endif
#ifndef COMPOUND_IDX_CONTEXTS
#define COMPOUND_IDX_CONTEXTS 6
#endif
#ifndef COMPOUND_MODES
#define COMPOUND_MODES 8
#endif
#ifndef COMPOUND_MODE_CONTEXTS
#define COMPOUND_MODE_CONTEXTS 8
#endif
#ifndef COMPOUND_TYPES
#define COMPOUND_TYPES 2
#endif
#ifndef COMP_GROUP_IDX_CONTEXTS
#define COMP_GROUP_IDX_CONTEXTS 6
#endif
#ifndef COMP_INTER_CONTEXTS
#define COMP_INTER_CONTEXTS 5
#endif
#ifndef COMP_REF_TYPE_CONTEXTS
#define COMP_REF_TYPE_CONTEXTS 5
#endif
#ifndef DC_SIGN_CONTEXTS
#define DC_SIGN_CONTEXTS 3
#endif
#ifndef DELTA_LF_SMALL
#define DELTA_LF_SMALL 3
#endif
#ifndef DELTA_Q_SMALL
#define DELTA_Q_SMALL 3
#endif
#ifndef DIRECTIONAL_MODES
#define DIRECTIONAL_MODES 8
#endif
#ifndef DIV_LUT_NUM
#define DIV_LUT_NUM 257
#endif
#ifndef DRL_MODE_CONTEXTS
#define DRL_MODE_CONTEXTS 3
#endif
#ifndef EOB_COEF_CONTEXTS
#define EOB_COEF_CONTEXTS 9
#endif
#ifndef FRAME_LF_COUNT
#define FRAME_LF_COUNT 4
#endif
#ifndef FWD_REFS
#define FWD_REFS 4
#endif
#ifndef INTERINTRA_MODES
#define INTERINTRA_MODES 4
#endif
#ifndef INTERP_FILTERS
#define INTERP_FILTERS 3
#endif
#ifndef INTERP_FILTER_CONTEXTS
#define INTERP_FILTER_CONTEXTS 16
#endif
#ifndef INTRA_EDGE_KERNELS
#define INTRA_EDGE_KERNELS 3
#endif
#ifndef INTRA_EDGE_TAPS
#define INTRA_EDGE_TAPS 5
#endif
#ifndef INTRA_FILTER_MODES
#define INTRA_FILTER_MODES 5
#endif
#ifndef INTRA_MODES
#define INTRA_MODES 13
#endif
#ifndef INTRA_MODE_CONTEXTS
#define INTRA_MODE_CONTEXTS 5
#endif
#ifndef IS_INTER_CONTEXTS
#define IS_INTER_CONTEXTS 4
#endif
#ifndef LEVEL_CONTEXTS
#define LEVEL_CONTEXTS 21
#endif
#ifndef MAX_ANGLE_DELTA
#define MAX_ANGLE_DELTA 3
#endif
#ifndef MAX_FRAME_DISTANCE
#define MAX_FRAME_DISTANCE 31
#endif
#ifndef MAX_LOOP_FILTER
#define MAX_LOOP_FILTER 63
#endif
#ifndef MAX_REF_MV_STACK_SIZE
#define MAX_REF_MV_STACK_SIZE 8
#endif
#ifndef MAX_SB_SIZE
#define MAX_SB_SIZE 128
#endif
#ifndef MAX_SEGMENTS
#define MAX_SEGMENTS 8
#endif
#ifndef MAX_TX_DEPTH
#define MAX_TX_DEPTH 2
#endif
#ifndef MAX_TX_SIZE
#define MAX_TX_SIZE 64
#endif
#ifndef MI_SIZE
#define MI_SIZE 4
#endif
#ifndef MOTION_MODES
#define MOTION_MODES 3
#endif
#ifndef MV_CLASSES
#define MV_CLASSES 11
#endif
#ifndef MV_CONTEXTS
#define MV_CONTEXTS 2
#endif
#ifndef MV_JOINTS
#define MV_JOINTS 4
#endif
#ifndef MV_OFFSET_BITS
#define MV_OFFSET_BITS 10
#endif
#ifndef NEW_MV_CONTEXTS
#define NEW_MV_CONTEXTS 6
#endif
#ifndef NUM_BASE_LEVELS
#define NUM_BASE_LEVELS 2
#endif
#ifndef PALETTE_BLOCK_SIZE_CONTEXTS
#define PALETTE_BLOCK_SIZE_CONTEXTS 7
#endif
#ifndef PALETTE_COLORS
#define PALETTE_COLORS 8
#endif
#ifndef PALETTE_COLOR_CONTEXTS
#define PALETTE_COLOR_CONTEXTS 5
#endif
#ifndef PALETTE_MAX_COLOR_CONTEXT_HASH
#define PALETTE_MAX_COLOR_CONTEXT_HASH 8
#endif
#ifndef PALETTE_NUM_NEIGHBORS
#define PALETTE_NUM_NEIGHBORS 3
#endif
#ifndef PALETTE_SIZES
#define PALETTE_SIZES 7
#endif
#ifndef PALETTE_UV_MODE_CONTEXTS
#define PALETTE_UV_MODE_CONTEXTS 2
#endif
#ifndef PALETTE_Y_MODE_CONTEXTS
#define PALETTE_Y_MODE_CONTEXTS 3
#endif
#ifndef PARTITION_CONTEXTS
#define PARTITION_CONTEXTS 4
#endif
#ifndef PLANE_TYPES
#define PLANE_TYPES 2
#endif
#ifndef REF_CONTEXTS
#define REF_CONTEXTS 3
#endif
#ifndef REF_MV_CONTEXTS
#define REF_MV_CONTEXTS 6
#endif
#ifndef RESTORE_NONE
#define RESTORE_NONE 0
#endif
#ifndef RESTORE_SGRPROJ
#define RESTORE_SGRPROJ 2
#endif
#ifndef RESTORE_SWITCHABLE
#define RESTORE_SWITCHABLE 3
#endif
#ifndef RESTORE_WIENER
#define RESTORE_WIENER 1
#endif
#ifndef SEGMENT_ID_CONTEXTS
#define SEGMENT_ID_CONTEXTS 3
#endif
#ifndef SEGMENT_ID_PREDICTED_CONTEXTS
#define SEGMENT_ID_PREDICTED_CONTEXTS 3
#endif
#ifndef SEG_LVL_MAX
#define SEG_LVL_MAX 8
#endif
#ifndef SGRPROJ_PARAMS_BITS
#define SGRPROJ_PARAMS_BITS 4
#endif
#ifndef SIG_COEF_CONTEXTS
#define SIG_COEF_CONTEXTS 42
#endif
#ifndef SIG_COEF_CONTEXTS_2D
#define SIG_COEF_CONTEXTS_2D 26
#endif
#ifndef SIG_COEF_CONTEXTS_EOB
#define SIG_COEF_CONTEXTS_EOB 4
#endif
#ifndef SIG_REF_DIFF_OFFSET_NUM
#define SIG_REF_DIFF_OFFSET_NUM 5
#endif
#ifndef SINGLE_REFS
#define SINGLE_REFS 7
#endif
#ifndef SKIP_CONTEXTS
#define SKIP_CONTEXTS 3
#endif
#ifndef SKIP_MODE_CONTEXTS
#define SKIP_MODE_CONTEXTS 3
#endif
#ifndef SUBPEL_BITS
#define SUBPEL_BITS 4
#endif
#ifndef SUPERRES_FILTER_BITS
#define SUPERRES_FILTER_BITS 6
#endif
#ifndef SUPERRES_FILTER_OFFSET
#define SUPERRES_FILTER_OFFSET 3
#endif
#ifndef SUPERRES_FILTER_SHIFTS
#define SUPERRES_FILTER_SHIFTS 64
#endif
#ifndef SUPERRES_FILTER_TAPS
#define SUPERRES_FILTER_TAPS 8
#endif
#ifndef TXB_SKIP_CONTEXTS
#define TXB_SKIP_CONTEXTS 13
#endif
#ifndef TXFM_PARTITION_CONTEXTS
#define TXFM_PARTITION_CONTEXTS 21
#endif
#ifndef TX_SET_TYPES_INTER
#define TX_SET_TYPES_INTER 4
#endif
#ifndef TX_SET_TYPES_INTRA
#define TX_SET_TYPES_INTRA 3
#endif
#ifndef TX_SIZES
#define TX_SIZES 5
#endif
#ifndef TX_SIZES_ALL
#define TX_SIZES_ALL 19
#endif
#ifndef TX_SIZE_CONTEXTS
#define TX_SIZE_CONTEXTS 3
#endif
#ifndef TX_TYPES
#define TX_TYPES 16
#endif
#ifndef UNIDIR_COMP_REFS
#define UNIDIR_COMP_REFS 4
#endif
#ifndef UV_INTRA_MODES_CFL_ALLOWED
#define UV_INTRA_MODES_CFL_ALLOWED 14
#endif
#ifndef UV_INTRA_MODES_CFL_NOT_ALLOWED
#define UV_INTRA_MODES_CFL_NOT_ALLOWED 13
#endif
#ifndef WEDGE_TYPES
#define WEDGE_TYPES 16
#endif
#ifndef WIENER_COEFFS
#define WIENER_COEFFS 3
#endif
#ifndef ZERO_MV_CONTEXTS
#define ZERO_MV_CONTEXTS 2
#endif

extern const uint8_t Segmentation_Feature_Bits[8];
extern const uint8_t Segmentation_Feature_Signed[8];
extern const uint8_t Segmentation_Feature_Max[8];
extern const uint8_t Remap_Lr_Type[4];
extern const int8_t Wiener_Taps_Mid[3];
extern const int8_t Sgrproj_Xqd_Mid[2];
extern const uint8_t Max_Tx_Depth[22];
extern const uint8_t Subsampled_Size[22][2][2];
extern const uint8_t Tx_Type_In_Set_Intra[3][16];
extern const uint8_t Tx_Type_Intra_Inv_Set1[7];
extern const uint8_t Tx_Type_Intra_Inv_Set2[5];
extern const int8_t Wiener_Taps_Min[3];
extern const int8_t Wiener_Taps_Max[3];
extern const int8_t Wiener_Taps_K[3];
extern const int8_t Sgrproj_Xqd_Min[2];
extern const int8_t Sgrproj_Xqd_Max[2];
extern const uint8_t Intra_Edge_Kernel[3][5];
extern const int16_t Dc_Qlookup[3][256];
extern const int16_t Ac_Qlookup[3][256];
extern const uint16_t Qm_Offset[TX_SIZES_ALL];
extern const uint8_t Quantizer_Matrix[15][2][3344];
extern const int16_t Cos128_Lookup[65];
extern const uint8_t Transform_Row_Shift[19];
extern const uint8_t Cdef_Uv_Dir[2][2][8];
extern const int16_t Div_Table[9];
extern const uint8_t Cdef_Pri_Taps[2][2];
extern const uint8_t Cdef_Sec_Taps[2][2];
extern const int8_t Cdef_Directions[8][2][2];
extern const int16_t Upscale_Filter[64][8];
extern const int16_t Sgr_Params[16][4];
extern const int16_t Coeff_Base_Ctx_Offset[19][5][5];
extern const int16_t Coeff_Base_Pos_Ctx_Offset[3];
extern const int16_t Mag_Ref_Offset_With_Tx_Class[3][3][2];
extern const int8_t Filter_Intra_Mode_To_Intra_Dir[5];
extern const uint16_t Default_Scan_4x4[16];
extern const uint16_t Default_Scan_4x8[32];
extern const uint16_t Default_Scan_8x4[32];
extern const uint16_t Default_Scan_8x8[64];
extern const uint16_t Default_Scan_8x16[128];
extern const uint16_t Default_Scan_16x8[128];
extern const uint16_t Default_Scan_16x16[256];
extern const uint16_t Default_Scan_16x32[512];
extern const uint16_t Default_Scan_32x16[512];
extern const uint16_t Default_Scan_32x32[1024];
extern const uint16_t Default_Scan_4x16[64];
extern const uint16_t Default_Scan_16x4[64];
extern const uint16_t Default_Scan_8x32[256];
extern const uint16_t Default_Scan_32x8[256];
extern const uint16_t Mcol_Scan_4x4[16];
extern const uint16_t Mcol_Scan_4x8[32];
extern const uint16_t Mcol_Scan_8x4[32];
extern const uint16_t Mcol_Scan_8x8[64];
extern const uint16_t Mcol_Scan_8x16[128];
extern const uint16_t Mcol_Scan_16x8[128];
extern const uint16_t Mcol_Scan_16x16[256];
extern const uint16_t Mcol_Scan_4x16[64];
extern const uint16_t Mcol_Scan_16x4[64];
extern const uint16_t Mrow_Scan_4x4[16];
extern const uint16_t Mrow_Scan_4x8[32];
extern const uint16_t Mrow_Scan_8x4[32];
extern const uint16_t Mrow_Scan_8x8[64];
extern const uint16_t Mrow_Scan_8x16[128];
extern const uint16_t Mrow_Scan_16x8[128];
extern const uint16_t Mrow_Scan_16x16[256];
extern const uint16_t Mrow_Scan_4x16[64];
extern const uint16_t Mrow_Scan_16x4[64];
extern const uint8_t Mi_Width_Log2[22];
extern const uint8_t Mi_Height_Log2[22];
extern const uint8_t Num_4x4_Blocks_Wide[22];
extern const uint8_t Num_4x4_Blocks_High[22];
extern const uint8_t Size_Group[22];
extern const uint8_t Max_Tx_Size_Rect[22];
extern const uint8_t Partition_Subsize[10][22];
extern const uint8_t Split_Tx_Size[19];
extern const uint8_t Mode_To_Txfm[14];
extern const int8_t Palette_Color_Context[9];
extern const uint8_t Palette_Color_Hash_Multipliers[3];
extern const int16_t Sm_Weights_Tx_4x4[4];
extern const int16_t Sm_Weights_Tx_8x8[8];
extern const int16_t Sm_Weights_Tx_16x16[16];
extern const int16_t Sm_Weights_Tx_32x32[32];
extern const int16_t Sm_Weights_Tx_64x64[64];
extern const uint8_t Mode_To_Angle[13];
extern const int16_t Dr_Intra_Derivative[90];
extern const int16_t Intra_Filter_Taps[5][8][7];
extern const uint8_t Tx_Size_Sqr[19];
extern const uint8_t Tx_Size_Sqr_Up[19];
extern const uint8_t Tx_Width[19];
extern const uint8_t Tx_Height[19];
extern const uint8_t Tx_Width_Log2[19];
extern const uint8_t Tx_Height_Log2[19];
extern const int16_t Sig_Ref_Diff_Offset[3][5][2];
extern const uint8_t Adjusted_Tx_Size[19];
extern const uint8_t Intra_Mode_Context[13];
extern const uint16_t Default_Intra_Frame_Y_Mode_Cdf[5][5][14];
extern const uint16_t Default_Y_Mode_Cdf[4][14];
extern const uint16_t Default_Uv_Mode_Cfl_Not_Allowed_Cdf[13][14];
extern const uint16_t Default_Uv_Mode_Cfl_Allowed_Cdf[13][15];
extern const uint16_t Default_Angle_Delta_Cdf[8][8];
extern const uint16_t Default_Intrabc_Cdf[3];
extern const uint16_t Default_Partition_W8_Cdf[4][5];
extern const uint16_t Default_Partition_W16_Cdf[4][11];
extern const uint16_t Default_Partition_W32_Cdf[4][11];
extern const uint16_t Default_Partition_W64_Cdf[4][11];
extern const uint16_t Default_Partition_W128_Cdf[4][9];
extern const uint16_t Default_Tx_8x8_Cdf[3][3];
extern const uint16_t Default_Tx_16x16_Cdf[3][4];
extern const uint16_t Default_Tx_32x32_Cdf[3][4];
extern const uint16_t Default_Tx_64x64_Cdf[3][4];
extern const uint16_t Default_Txfm_Split_Cdf[21][3];
extern const uint16_t Default_Filter_Intra_Mode_Cdf[6];
extern const uint16_t Default_Filter_Intra_Cdf[22][3];
extern const uint16_t Default_Segment_Id_Cdf[3][9];
extern const uint16_t Default_Segment_Id_Predicted_Cdf[3][3];
extern const uint16_t Default_Skip_Cdf[3][3];
extern const uint16_t Default_Palette_Y_Size_Cdf[7][8];
extern const uint16_t Default_Palette_Uv_Size_Cdf[7][8];
extern const uint16_t Default_Palette_Size_2_Y_Color_Cdf[5][3];
extern const uint16_t Default_Palette_Size_3_Y_Color_Cdf[5][4];
extern const uint16_t Default_Palette_Size_4_Y_Color_Cdf[5][5];
extern const uint16_t Default_Palette_Size_5_Y_Color_Cdf[5][6];
extern const uint16_t Default_Palette_Size_6_Y_Color_Cdf[5][7];
extern const uint16_t Default_Palette_Size_7_Y_Color_Cdf[5][8];
extern const uint16_t Default_Palette_Size_8_Y_Color_Cdf[5][9];
extern const uint16_t Default_Palette_Size_2_Uv_Color_Cdf[5][3];
extern const uint16_t Default_Palette_Size_3_Uv_Color_Cdf[5][4];
extern const uint16_t Default_Palette_Size_4_Uv_Color_Cdf[5][5];
extern const uint16_t Default_Palette_Size_5_Uv_Color_Cdf[5][6];
extern const uint16_t Default_Palette_Size_6_Uv_Color_Cdf[5][7];
extern const uint16_t Default_Palette_Size_7_Uv_Color_Cdf[5][8];
extern const uint16_t Default_Palette_Size_8_Uv_Color_Cdf[5][9];
extern const uint16_t Default_Palette_Y_Mode_Cdf[7][3][3];
extern const uint16_t Default_Palette_Uv_Mode_Cdf[2][3];
extern const uint16_t Default_Delta_Q_Cdf[5];
extern const uint16_t Default_Delta_Lf_Cdf[5];
extern const uint16_t Default_Intra_Tx_Type_Set1_Cdf[2][13][8];
extern const uint16_t Default_Intra_Tx_Type_Set2_Cdf[3][13][6];
extern const uint16_t Default_Cfl_Sign_Cdf[9];
extern const uint16_t Default_Cfl_Alpha_Cdf[6][17];
extern const uint16_t Default_Use_Wiener_Cdf[3];
extern const uint16_t Default_Use_Sgrproj_Cdf[3];
extern const uint16_t Default_Restoration_Type_Cdf[4];
extern const uint16_t Default_Txb_Skip_Cdf[4][5][13][3];
extern const uint16_t Default_Eob_Pt_16_Cdf[4][2][2][6];
extern const uint16_t Default_Eob_Pt_32_Cdf[4][2][2][7];
extern const uint16_t Default_Eob_Pt_64_Cdf[4][2][2][8];
extern const uint16_t Default_Eob_Pt_128_Cdf[4][2][2][9];
extern const uint16_t Default_Eob_Pt_256_Cdf[4][2][2][10];
extern const uint16_t Default_Eob_Pt_512_Cdf[4][2][11];
extern const uint16_t Default_Eob_Pt_1024_Cdf[4][2][12];
extern const uint16_t Default_Eob_Extra_Cdf[4][5][2][9][3];
extern const uint16_t Default_Dc_Sign_Cdf[4][2][3][3];
extern const uint16_t Default_Coeff_Base_Eob_Cdf[4][5][2][4][4];
extern const uint16_t Default_Coeff_Base_Cdf[4][5][2][42][5];
extern const uint16_t Default_Coeff_Br_Cdf[4][5][2][21][5];

#ifdef __cplusplus
}
#endif

#endif
