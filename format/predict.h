#ifndef _PREDICT_H_
#define _PREDICT_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
/* VP8 and H264 have the same predication algrithm */

// intra prediction modes
enum {
    // 4 * 4 intra modes
    B_DC_PRED = 0, // 4x4 modes
    B_TM_PRED = 1,
    B_VE_PRED = 2,
    B_HE_PRED = 3,
    B_RD_PRED = 4,
    B_VR_PRED = 5,
    B_LD_PRED = 6,
    B_VL_PRED = 7,
    B_HD_PRED = 8,
    B_HU_PRED = 9,
    NUM_BMODES = B_HU_PRED + 1 - B_DC_PRED, // = 10
};

enum {
    // 16X16 intra mode: Luma16 or UV modes
    DC_PRED = B_DC_PRED,
    TM_PRED = B_TM_PRED,
    V_PRED = B_VE_PRED,
    H_PRED = B_HE_PRED,
    B_PRED = 4, // refined I4x4 mode
    NUM_PRED_MODES = 5,
};


void
pred_luma(int16_t *coff, int ymode, uint8_t imodes[16], uint8_t *dst,
          int stride, int x, int y);

void pred_chrome(int16_t *coff, int imode, uint8_t *uout, uint8_t *vout,
                 int stride, int x, int y);


void hevc_intra_planar(uint16_t *dst, uint16_t *left, uint16_t *top,
                       int nTbS, int stride);

void hevc_intra_DC(uint16_t *dst, uint16_t *left, uint16_t *top, int nTbS,
                   int stride, int cIdx,
                   int intra_boundary_filtering_disabled_flag);

void hevc_intra_angular(uint16_t *dst, uint16_t *left, uint16_t *top, int nTbS,
                        int stride, int cIdx, int predModeIntra,
                        int disableIntraBoundaryFilter, int bitdepth);



#ifdef __cplusplus
}
#endif

#endif /*_PREDICT_H_*/
