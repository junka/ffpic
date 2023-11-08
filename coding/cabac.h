#ifndef _CABAC_H_
#define _CABAC_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>

#define STATE_BITS (6)
#define STATE_NUM  (1 << STATE_BITS)
#define RANGE_NUM (4)

typedef struct cabac_dec {
    uint32_t value;
    uint32_t range;     //[127, 254]
    int count;

    struct bits_vec *bits;
} cabac_dec;

enum ctx_index_type {
  CTX_TYPE_ALL_BYPASS = 0,
  CTX_TYPE_SAO_MERGE = 1,
  CTX_TYPE_SAO_TYPE_INDEX,

  CTX_TYPE_SPLIT_CU_FLAG,

  CTX_TYPE_CU_TRANSQUANT_BYPASS_FLAG = CTX_TYPE_SPLIT_CU_FLAG + 3,
  CTX_TYPE_CU_SKIP_FLAG,
  CTX_TYPE_CU_PALETTE_MODE_FLAG = CTX_TYPE_CU_SKIP_FLAG + 3,
  CTX_TYPE_CU_PRED_MODE_FLAG,
  CTX_TYPE_CU_PART_MODE,
  CTX_TYPE_CU_PREV_INTRA_LUMA_PRED_FLAG,
  CTX_TYPE_CU_INTRA_CHROME_PRED_MODE,
  CTX_TYPE_CU_RQT_ROOT_CBF,

  CTX_TYPE_PU_MERGE_FLAG,
  CTX_TYPE_PU_MERGE_IDX,
  CTX_TYPE_PU_INTER_PRED_IDC,
  CTX_TYPE_PU_REF_IDX = CTX_TYPE_PU_INTER_PRED_IDC + 5,
  CTX_TYPE_PU_MVP_FLAG,

  CTX_TYPE_TU_SPLIT_TRANSFORM_FLAG,
  CTX_TYPE_TU_CBF_LUMA = CTX_TYPE_TU_SPLIT_TRANSFORM_FLAG + 3,
  CTX_TYPE_TU_CBF_CBCR = CTX_TYPE_TU_CBF_LUMA + 2,

  CTX_TYPE_MV_ABS_MVD_GREATER0 = CTX_TYPE_TU_CBF_CBCR + 5,
  CTX_TYPE_MV_ABS_MVD_GREATER1,

  CTX_TYPE_CROSS_COMP_LOG2_RES_SCALE_ABS,
  CTX_TYPE_CROSS_COMP_RES_SCALE_SIGN =
      CTX_TYPE_CROSS_COMP_LOG2_RES_SCALE_ABS + 8,

  CTX_TYPE_RESIDUAL_CODING_TRANSFORM_SKIP =
      CTX_TYPE_CROSS_COMP_RES_SCALE_SIGN + 2,
  CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM =
      CTX_TYPE_RESIDUAL_CODING_TRANSFORM_SKIP + 2,
  CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM_DIR =
      CTX_TYPE_RESIDUAL_CODING_TRANSFORM_SKIP + 3,
  CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_X_PREFIX =
      CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM_DIR + 3,
  CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_Y_PREFIX =
      CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_X_PREFIX + 18,
  CTX_TYPE_RESIDUAL_CODING_CODED_SUB_BLOCK_FLAG =
      CTX_TYPE_RESIDUAL_CODING_LAST_SIG_COEFF_Y_PREFIX + 18,
  CTX_TYPE_RESIDUAL_CODING_SIG_COEFF_FLAG =
      CTX_TYPE_RESIDUAL_CODING_CODED_SUB_BLOCK_FLAG + 4,
  CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER1 =
      CTX_TYPE_RESIDUAL_CODING_SIG_COEFF_FLAG + 44,
  CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER2 =
      CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER1 + 24,

  CTX_TYPE_PALETTE_CODING_PALETTE_RUN_PREFIX =
      CTX_TYPE_RESIDUAL_CODING_COEFF_ABS_LEVEL_GREATER2 + 6,
  CTX_TYPE_PALETTE_CODING_COPY_ABOVE_PALLETTE_INDICES =
      CTX_TYPE_PALETTE_CODING_PALETTE_RUN_PREFIX + 8,
  CTX_TYPE_PALETTE_CODING_COPY_ABOVE_INDICES_FOR_FINAL_RUN,
  CTX_TYPE_PALETTE_CODING_PALETTE_TRANSPOSE_FLAG,

  CTX_TYPE_DELTA_QP_CU_QP_DELTA_ABS,

  CTX_TYPE_CHROMA_QP_OFFSET_CU_CHROME_QP_OFFSET_FLAG =
      CTX_TYPE_DELTA_QP_CU_QP_DELTA_ABS + 2,
  CTX_TYPE_CHROMA_QP_OFFSET_CU_CHROME_QP_OFFSET_IDX,

  // extension for 3D hevc
  CTX_TYPE_3D_SKIP_INTRA_FLAG,
  CTX_TYPE_3D_NO_DIM_FLAG,
  CTX_TYPE_3D_DEPTH_INTRA_MODE_IDX_FLAG,
  CTX_TYPE_3D_SKIP_INTRA_MODE_IDX,
  CTX_TYPE_3D_DBBP_FLAG,
  CTX_TYPE_3D_DC_ONLY_FLAG,
  CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX,
  CTX_TYPE_3D_ILLU_COMP_FLAG = CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX + 3,
  CTX_TYPE_3D_DEPTH_DC_PRESENT_FLAG,
  CTX_TYPE_3D_DEPTH_DC_ABS,

  CTX_TYPE_MAX_NUM,
};

cabac_dec * cabac_dec_init(struct bits_vec*);

typedef int (*cabac_get_ctxInc) (int ctx_idx, int binIdx);

int cabac_dec_bin(cabac_dec *br, int ctx_id, int bypass);

int cabac_dec_decision(cabac_dec *dec, int ctx_idx);
int cabac_dec_terminate(cabac_dec *dec);
int cabac_dec_bypass(cabac_dec *dec);
int cabac_dec_bypass_n(cabac_dec *dec, int n);
int cabac_dec_bypass_tb(cabac_dec *dec, int max);
int cabac_dec_bypass_fl(cabac_dec *dec, int max);
int cabac_dec_egk(cabac_dec *dec, int kth, int max_pre_ext_len,
                  int trunc_suffix_len);
void cabac_dec_free(cabac_dec *dec);

void cabac_init_models(int qpy, int initType);//here initType should alway be 0

#define CABAC(br, tid) cabac_dec_decision(br, tid)
#define CABAC_BP(br) cabac_dec_bypass(br)
#define CABAC_FL(br, max) cabac_dec_bypass_fl(br, max)

int cabac_dec_tr(cabac_dec *dec, int tid, int cMax, int cRiceParam,
                 cabac_get_ctxInc ctxInc);
#define CABAC_TR(br, tid, cMax, cRiceParam, cb) cabac_dec_tr(br, tid, cMax, cRiceParam, cb)

#define CABAC_TB(br, max) cabac_dec_bypass_tb(br, max)

#ifdef __cplusplus
}
#endif

#endif /*_CABAC_H_*/
