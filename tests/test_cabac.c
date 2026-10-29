/* The P/B context initialization (initType 1 and 2) is dead code for
 * still-image decoding, which only ever runs I slices.  The initValue rows
 * below come from the HEVC spec and were cross-checked against ffmpeg's
 * hevc decoder.  The expected (state, mps) pairs are recomputed here from
 * the 9.3.2.2 formula instead of being copied from the decoder, so a
 * transcription error in cabac.c turns into a mismatch.  The 3D-extension
 * rows have no ffmpeg counterpart; they only pin the current values as a
 * regression anchor. */

#include <stdio.h>

#include "cabac.h"

struct ctx_case {
    const char *name;
    int tid;
    /* indexed by initType - 1 */
    int value[2];
};

static const struct ctx_case cases[] = {
    {"cu_skip_flag[0]", CTX_TYPE_CU_SKIP_FLAG + 0, {197, 197}},
    {"cu_skip_flag[1]", CTX_TYPE_CU_SKIP_FLAG + 1, {185, 185}},
    {"cu_skip_flag[2]", CTX_TYPE_CU_SKIP_FLAG + 2, {201, 201}},
    {"pred_mode_flag", CTX_TYPE_CU_PRED_MODE_FLAG, {149, 134}},
    {"rqt_root_cbf", CTX_TYPE_CU_RQT_ROOT_CBF, {79, 79}},
    {"merge_flag", CTX_TYPE_PU_MERGE_FLAG, {110, 154}},
    {"merge_idx", CTX_TYPE_PU_MERGE_IDX, {122, 137}},
    {"inter_pred_idc[0]", CTX_TYPE_PU_INTER_PRED_IDC + 0, {95, 95}},
    {"inter_pred_idc[1]", CTX_TYPE_PU_INTER_PRED_IDC + 1, {79, 79}},
    {"inter_pred_idc[2]", CTX_TYPE_PU_INTER_PRED_IDC + 2, {63, 63}},
    {"inter_pred_idc[3]", CTX_TYPE_PU_INTER_PRED_IDC + 3, {31, 31}},
    {"inter_pred_idc[4]", CTX_TYPE_PU_INTER_PRED_IDC + 4, {31, 31}},
    {"ref_idx[0]", CTX_TYPE_PU_REF_IDX + 0, {153, 153}},
    {"ref_idx[1]", CTX_TYPE_PU_REF_IDX + 1, {153, 153}},
    {"mvp_flag", CTX_TYPE_PU_MVP_FLAG, {168, 168}},
    {"abs_mvd_greater0_flag", CTX_TYPE_MV_ABS_MVD_GREATER0, {140, 169}},
    {"abs_mvd_greater1_flag", CTX_TYPE_MV_ABS_MVD_GREATER1, {198, 198}},
    {"explicit_rdpcm_flag[0]", CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM + 0,
     {139, 139}},
    {"explicit_rdpcm_flag[1]", CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM + 1,
     {139, 139}},
    {"explicit_rdpcm_dir_flag[0]",
     CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM_DIR + 0, {139, 139}},
    {"explicit_rdpcm_dir_flag[1]",
     CTX_TYPE_RESIDUAL_CODING_EXPLICIT_RDPCM_DIR + 1, {139, 139}},
    /* no ffmpeg counterpart, values pinned as-is */
    {"iv_res_pred_weight_idx[0]", CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX + 0,
     {162, 162}},
    {"iv_res_pred_weight_idx[1]", CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX + 1,
     {153, 153}},
    {"iv_res_pred_weight_idx[2]", CTX_TYPE_3D_IV_RES_PRED_WEIGHT_IDX + 2,
     {162, 162}},
    {"illu_comp_flag", CTX_TYPE_3D_ILLU_COMP_FLAG, {154, 154}},
};

/* 9.3.2.2 initialization process for context variables */
static void
expect_model(int init_value, int qpy, uint8_t *state, uint8_t *mps)
{
    int slope_idx = init_value >> 4;
    int offset_idx = init_value & 15;
    int m = slope_idx * 5 - 45;
    int n = (offset_idx << 3) - 16;
    int q = qpy < 0 ? 0 : (qpy > 51 ? 51 : qpy);
    int pre_ctx_state = ((m * q) >> 4) + n;

    if (pre_ctx_state < 1) {
        pre_ctx_state = 1;
    } else if (pre_ctx_state > 126) {
        pre_ctx_state = 126;
    }
    if (pre_ctx_state <= 63) {
        *state = (uint8_t)(63 - pre_ctx_state);
        *mps = 0;
    } else {
        *state = (uint8_t)(pre_ctx_state - 64);
        *mps = 1;
    }
}

static int
test_init_types(void)
{
    static const int qpys[] = {0, 26, 51};
    int rc = 0;

    for (int init_type = 1; init_type <= 2; init_type++) {
        for (size_t q = 0; q < sizeof(qpys) / sizeof(qpys[0]); q++) {
            int qpy = qpys[q];

            cabac_init_models(qpy, init_type);
            for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                uint8_t state, mps, want_state, want_mps;

                cabac_ctx_model(cases[i].tid, &state, &mps);
                expect_model(cases[i].value[init_type - 1], qpy, &want_state,
                             &want_mps);
                if (state != want_state || mps != want_mps) {
                    printf("%s: initType %d qpy %d got (state %d, mps %d), "
                           "expected (%d, %d)\n", cases[i].name, init_type,
                           qpy, state, mps, want_state, want_mps);
                    rc = -1;
                }
            }
        }
    }
    return rc;
}

/* initType 1 and 2 must produce different contexts where the spec says so;
 * identical rows would hide a swapped index */
static int
test_types_differ(void)
{
    static const struct {
        int tid;
        const char *name;
    } differing[] = {
        {CTX_TYPE_CU_PRED_MODE_FLAG, "pred_mode_flag"},
        {CTX_TYPE_PU_MERGE_FLAG, "merge_flag"},
        {CTX_TYPE_MV_ABS_MVD_GREATER0, "abs_mvd_greater0_flag"},
    };
    int rc = 0;

    for (size_t i = 0; i < sizeof(differing) / sizeof(differing[0]); i++) {
        uint8_t state[2], mps[2];

        for (int init_type = 1; init_type <= 2; init_type++) {
            cabac_init_models(26, init_type);
            cabac_ctx_model(differing[i].tid, &state[init_type - 1],
                            &mps[init_type - 1]);
        }
        if (state[0] == state[1] && mps[0] == mps[1]) {
            printf("%s: initType 1 and 2 initialized identically\n",
                   differing[i].name);
            rc = -1;
        }
    }
    return rc;
}

int main(void)
{
    if (test_init_types()) {
        return -1;
    }
    if (test_types_differ()) {
        return -1;
    }
    return 0;
}
