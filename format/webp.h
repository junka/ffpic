#ifndef _WEBP_H_
#define _WEBP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CHUNCK_HEADER(c) (uint32_t)(c[3]<<24|c[2]<<16|c[1]<<8|c[0])
#define READ_UINT24(a)  (a[2]<<16 | a[1]<<8 | a[0])

#pragma pack(push, 1)

struct riff_chunck {
    uint32_t fourcc;
    uint32_t size;
    uint32_t payload;
};

struct webp_header {
    uint32_t riff;  /* RIFF ascii code */
    uint32_t file_size;
    uint32_t webp;  /* WEBP ascii code */
};

struct webp_vp8x {
    uint32_t vp8x; /* VP8X ascii code */
    uint32_t resv:2;
    uint32_t icc:1;
    uint32_t alpha:1;
    uint32_t exif_metadata:1;
    uint32_t xmp_metadata:1;
    uint32_t animation:1;
    uint32_t reserved:25;
    uint8_t canvas_width[3];
    uint8_t canvas_height[3];
};

struct webp_anim {
    uint32_t anim;  /* ANIM ascii code */
    uint32_t background;
    uint16_t loop_count;
};

struct webp_anmf {
    uint32_t anmf; /* ANMF ascii code */
    uint8_t frameX[3];
    uint8_t frameY[3];
    uint8_t width[3];
    uint8_t height[3];
    uint8_t duration[3];
    uint8_t resvd:6;
    uint8_t blending:1;
    uint8_t disposal:1;
};


struct webp_alpha {
    uint32_t alph;  /* ALPH ascii code */
    uint8_t rsv:2;
    uint8_t preprocess:2;
    uint8_t filter:2;
    uint8_t compression:2;
    uint8_t *bitstream;
};

struct webp_color {
    uint32_t iccp; /* ICCP ascii code */

};

enum vp8_version {
    VP8_BICUBIC_NORMAL = 0,
    VP8_BILINEAR_SIMPLE,
    VP8_BILINEAR_NONE,
    VP8_NONE,
};

typedef enum { KEY_FRAME = 0, INTER_FRAME = 1 } FRAME_TYPE;

struct vp8_frame_tag {
    uint8_t frame_type : 1;
    uint8_t version : 3;    /* reconstruction, loop filter */
                            /* 0: bicubic, normal */
                            /* 1: bilinear, simple */
                            /* 2: bilinear, none */
    uint8_t show_frame : 1;
    uint8_t size_h:3;
    uint16_t size;
};

enum scale_type {
    VP8_SCALE_NONE = 0,
    VP8_SCALE_5_4,
    VP8_SCALE_5_3,
    VP8_SCALE_2,
};

struct vp8_key_frame_extra {
    uint8_t start1;
    uint8_t start2;
    uint8_t start3;
    uint16_t horizontal : 2;    /* see enum scale_type */
    uint16_t width : 14;
    uint16_t vertical : 2;      /* see enum scale_type */
    uint16_t height : 14;
};

struct vp8_key_cs {
    uint8_t color_space:1;   /* 0 for YUV */
    uint8_t clamp:1;    /*0 clamp the reconstructed pixel values to between 0 and 255 (inclusive). */
};

struct vp8_quantizer_update {
    uint8_t quantizer_update:1;
    uint8_t quantizer_update_value;
};

struct vp8_loop_filter_update {
    uint8_t loop_filter_update: 1;
    uint8_t lf_update_value : 7;
};

struct vp8_segment_prob_update {
    uint8_t segment_prob_update: 1;
    uint8_t segment_prob;
};

struct vp8_update_segmentation {
    uint8_t segmentation_enabled:1;
    uint8_t update_mb_segmentation_map:1;
    uint8_t update_segment_feature_data:1;
    uint8_t segment_feature_mode:1;

    struct vp8_quantizer_update quant[4];

    struct vp8_loop_filter_update lf[4];

    struct vp8_segment_prob_update segment_prob[3];

};

struct vp8_mb_lf_adjustments {
    uint8_t loop_filter_adj_enable:1;
    uint8_t mode_ref_lf_delta_update_flag:1;
    int8_t mode_ref_lf_delta_update[4];
    int8_t mb_mode_delta_update[4];
};

struct vp8_quant_indice {
    uint8_t y_ac_qi;

    int8_t y_dc_delta;
    int8_t y2_dc_delta;
    int8_t y2_ac_delta;
    int8_t uv_dc_delta;
    int8_t uv_ac_delta;
};

struct vp8_key_frame_header {
    struct vp8_key_cs cs_and_clamp;
  
    struct vp8_update_segmentation segmentation;
    uint8_t filter_type:1;
    uint8_t loop_filter_level : 6;
    uint8_t sharpness_level : 3;
    struct vp8_mb_lf_adjustments mb_lf_adjustments;

    // uint8_t log2_nbr_of_dct_partitions: 2;
    uint8_t nbr_partitions;

    struct vp8_quant_indice quant_indice;

    uint8_t refresh_entropy_probs:1;

    uint8_t coeff_prob[4][8][3][11];
    uint8_t mb_no_skip_coeff: 1;
    uint8_t prob_skip_false;

};

struct macro_block {
    int16_t coeffs[384];   // 384 coeffs = (16+4+4) * 4*4
    uint8_t is_i4x4;       // true if intra4x4
    uint8_t imodes[16];    // one 16x16 mode (#0) or sixteen 4x4 modes
    uint8_t uvmode;        // chroma prediction mode
    // bit-wise info about the content of each sub-4x4 blocks (in decoding order).
    // Each of the 4x4 blocks for y/u/v is associated with a 2b code according to:
    //   code=0 -> no coefficient
    //   code=1 -> only DC
    //   code=2 -> first three coefficients are non-zero
    //   code=3 -> more than three coefficients are non-zero
    // This allows to call specialized transform functions.
    uint32_t non_zero_y;
    uint32_t non_zero_uv;
    uint8_t dither;      // local dithering strength (deduced from non_zero_*)
    uint8_t skip;
    uint8_t segment;
};


#pragma pack(pop)


// intra prediction modes
enum {
    B_DC_PRED = 0,   // 4x4 modes
    B_TM_PRED = 1,
    B_VE_PRED = 2,
    B_HE_PRED = 3,
    B_RD_PRED = 4,
    B_VR_PRED = 5,
    B_LD_PRED = 6,
    B_VL_PRED = 7,
    B_HD_PRED = 8,
    B_HU_PRED = 9,
    NUM_BMODES = B_HU_PRED + 1 - B_DC_PRED,  // = 10

    // Luma16 or UV modes
    DC_PRED = B_DC_PRED, V_PRED = B_VE_PRED,
    H_PRED = B_HE_PRED, TM_PRED = B_TM_PRED,
    B_PRED = NUM_BMODES,   // refined I4x4 mode
    NUM_PRED_MODES = 4,

    // special modes
    B_DC_PRED_NOTOP = 4,
    B_DC_PRED_NOLEFT = 5,
    B_DC_PRED_NOTOPLEFT = 6,
    NUM_B_DC_MODES = 7 
};

enum { 
    MB_FEATURE_TREE_PROBS = 3,
    NUM_MB_SEGMENTS = 4,
    NUM_REF_LF_DELTAS = 4,
    NUM_MODE_LF_DELTAS = 4,    // I4x4, ZERO, *, SPLIT
    MAX_NUM_PARTITIONS = 8,
    // Probabilities
    NUM_TYPES = 4,   // 0: i16-AC,  1: i16-DC,  2:chroma-AC,  3:i4-AC
    NUM_BANDS = 8,
    NUM_CTX = 3,
    NUM_PROBAS = 11
};


#define MAX_PARTI_NUM (4)

struct partition {
    uint32_t start;     // offset in the file
    uint32_t len;       // partition length
};

typedef struct {
    struct webp_header header;
    struct webp_vp8x vp8x;
    struct vp8_frame_tag fh;
    struct vp8_key_frame_extra fi;
    struct vp8_key_frame_header k;
    struct partition p[MAX_PARTI_NUM];
} WEBP;

void WEBP_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_WEBP_H_*/