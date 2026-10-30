#ifndef _AV1_INTERNAL_H_
#define _AV1_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "av1.h"
#include "av1_tables.h"

#define AV1_MAX_TILE_COLS 64
#define AV1_MAX_TILE_ROWS 64
#define AV1_REFS_PER_FRAME 7
#define AV1_NUM_REF_FRAMES 8
#define AV1_INTERP_FILTERS 3
#define AV1_FRAME_LF_COUNT 4
#define AV1_MAX_SEGMENTS 8
#define AV1_MI_SIZE 4
#define AV1_MI_SIZE_LOG2 2
#define AV1_MAX_SB_SIZE 128

/* ------------------------------------------------------------------ */
/* Bit reader: AV1 header bits are read MSB first.                    */
/* ------------------------------------------------------------------ */

struct av1_bits {
    const uint8_t *data;
    size_t size;            /* bytes */
    size_t bitpos;
    int error;
};

static inline void
av1_bits_init(struct av1_bits *b, const uint8_t *data, size_t size)
{
    b->data = data;
    b->size = size;
    b->bitpos = 0;
    b->error = 0;
}

static inline int
av1_bits_left(const struct av1_bits *b)
{
    return (int)(b->size * 8 - b->bitpos);
}

static inline uint32_t
av1_bits_get(struct av1_bits *b, int n)
{
    uint32_t v = 0;
    if (n <= 0) {
        return 0;
    }
    if (b->bitpos + (size_t)n > b->size * 8) {
        b->error = 1;
        b->bitpos = b->size * 8;
        return 0;
    }
    for (int i = 0; i < n; i++) {
        v = (v << 1) |
            ((b->data[b->bitpos >> 3] >> (7 - (b->bitpos & 7))) & 1);
        b->bitpos++;
    }
    return v;
}

static inline int
av1_bits_bit(struct av1_bits *b)
{
    return (int)av1_bits_get(b, 1);
}

static inline int32_t
av1_bits_su(struct av1_bits *b, int n)
{
    uint32_t v = av1_bits_get(b, n);
    uint32_t mask = (n > 0) ? (1u << (n - 1)) : 0;
    if (mask && (v & mask)) {
        return (int32_t)(v | ~((mask << 1) - 1));
    }
    return (int32_t)v;
}

static inline void
av1_bits_align(struct av1_bits *b)
{
    b->bitpos = (b->bitpos + 7) & ~(size_t)7;
    if (b->bitpos > b->size * 8) {
        b->bitpos = b->size * 8;
    }
}

static inline uint32_t
av1_bits_leb128(struct av1_bits *b)
{
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t byte = av1_bits_get(b, 8);
        v |= (byte & 0x7f) << (i * 7);
        if (!(byte & 0x80)) {
            break;
        }
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* Range (symbol) decoder                                             */
/* ------------------------------------------------------------------ */

struct av1_range {
    uint32_t value;         /* SymbolValue */
    uint32_t range;         /* SymbolRange */
    int max_bits;           /* SymbolMaxBits */
    struct av1_bits bits;
};

void av1_range_init(struct av1_range *r, const uint8_t *data, size_t size);
int av1_range_symbol(struct av1_range *r, uint16_t *cdf, int n, int update);
int av1_range_bool(struct av1_range *r);

/* ------------------------------------------------------------------ */
/* CDF state                                                          */
/* ------------------------------------------------------------------ */

struct av1_cdfs {
    uint16_t intra_frame_y_mode[5][5][14];
    uint16_t y_mode[4][14];
    uint16_t uv_mode_cfl_not_allowed[13][14];
    uint16_t uv_mode_cfl_allowed[13][15];
    uint16_t angle_delta[8][8];
    uint16_t partition_w8[4][5];
    uint16_t partition_w16[4][11];
    uint16_t partition_w32[4][11];
    uint16_t partition_w64[4][11];
    uint16_t partition_w128[4][9];
    uint16_t tx_8x8[3][3];
    uint16_t tx_16x16[3][4];
    uint16_t tx_32x32[3][4];
    uint16_t tx_64x64[3][4];
    uint16_t txfm_split[21][3];
    uint16_t filter_intra_mode[6];
    uint16_t filter_intra[22][3];
    uint16_t segment_id[3][9];
    uint16_t segment_id_predicted[3][3];
    uint16_t skip[3][3];
    uint16_t intrabc[3];
    uint16_t palette_y_size[7][8];
    uint16_t palette_uv_size[7][8];
    uint16_t palette_size_2_y[5][3];
    uint16_t palette_size_3_y[5][4];
    uint16_t palette_size_4_y[5][5];
    uint16_t palette_size_5_y[5][6];
    uint16_t palette_size_6_y[5][7];
    uint16_t palette_size_7_y[5][8];
    uint16_t palette_size_8_y[5][9];
    uint16_t palette_size_2_uv[5][3];
    uint16_t palette_size_3_uv[5][4];
    uint16_t palette_size_4_uv[5][5];
    uint16_t palette_size_5_uv[5][6];
    uint16_t palette_size_6_uv[5][7];
    uint16_t palette_size_7_uv[5][8];
    uint16_t palette_size_8_uv[5][9];
    uint16_t palette_y_mode[7][3][3];
    uint16_t palette_uv_mode[2][3];
    uint16_t delta_q[5];
    uint16_t delta_lf[5];
    uint16_t intra_tx_type_set1[2][13][8];
    uint16_t intra_tx_type_set2[3][13][6];
    uint16_t cfl_sign[9];
    uint16_t cfl_alpha[6][17];
    uint16_t use_wiener[3];
    uint16_t use_sgrproj[3];
    uint16_t restoration_type[4];
    uint16_t txb_skip[5][13][3];
    uint16_t eob_pt_16[2][2][6];
    uint16_t eob_pt_32[2][2][7];
    uint16_t eob_pt_64[2][2][8];
    uint16_t eob_pt_128[2][2][9];
    uint16_t eob_pt_256[2][2][10];
    uint16_t eob_pt_512[2][11];
    uint16_t eob_pt_1024[2][12];
    uint16_t eob_extra[5][2][9][3];
    uint16_t dc_sign[2][3][3];
    uint16_t coeff_base_eob[5][2][4][4];
    uint16_t coeff_base[5][2][42][5];
    uint16_t coeff_br[5][2][21][5];
};

void av1_cdf_init_non_coeff(struct av1_cdfs *c);
void av1_cdf_init_coeff(struct av1_cdfs *c, int base_q_idx);

uint32_t av1_range_literal(struct av1_range *r, int n);
int av1_range_ns(struct av1_range *r, int n);
int av1_range_su(struct av1_range *r, int n);

/* ------------------------------------------------------------------ */
/* Frame state                                                        */
/* ------------------------------------------------------------------ */

struct av1_segmentation {
    int enabled;
    int update_map;
    int temporal_update;
    int update_data;
    int feature_enabled[AV1_MAX_SEGMENTS][8];
    int feature_data[AV1_MAX_SEGMENTS][8];
    int last_active_seg_id;
};

struct av1_tile_layout {
    int cols, rows;
    int cols_log2, rows_log2;
    int size_bytes;
    int context_update_tile_id;
    int mi_col_starts[AV1_MAX_TILE_COLS + 1];
    int mi_row_starts[AV1_MAX_TILE_ROWS + 1];
};

struct av1_frame {
    int profile;
    int bitdepth;
    int mono_chrome;
    int num_planes;
    int subsampling_x, subsampling_y;
    int separate_uv_delta_q;
    int color_range;
    int color_primaries, transfer_characteristics, matrix_coefficients;
    int chroma_sample_position;

    int width, height;              /* FrameWidth / FrameHeight */
    int upscaled_width;             /* UpscaledWidth */
    int render_width, render_height;
    int mi_cols, mi_rows;

    int use_128x128_superblock;
    int enable_filter_intra;
    int enable_intra_edge_filter;
    int enable_superres;
    int enable_cdef;
    int enable_restoration;

    int reduced_still_picture_header;
    int still_picture;
    int frame_type;
    int frame_is_intra;
    int show_frame, showable_frame;
    int error_resilient_mode;
    int disable_cdf_update;
    int disable_frame_end_update_cdf;
    int allow_screen_content_tools;
    int force_integer_mv;
    int allow_intrabc;
    int allow_high_precision_mv;
    int use_ref_frame_mvs;
    int reference_select;
    int allow_warped_motion;
    int reduced_tx_set;
    int primary_ref_frame;
    int refresh_frame_flags;
    int order_hint;

    int frame_size_override_flag;
    int use_superres;
    int superres_denom;

    int base_q_idx;
    int delta_q_present, delta_q_res;
    int delta_lf_present, delta_lf_res, delta_lf_multi;
    int delta_q_ydc, delta_q_udc, delta_q_uac, delta_q_vdc, delta_q_vac;
    int using_qmatrix;
    int qm_y, qm_u, qm_v;

    int coded_lossless;
    int all_lossless;
    int lossless[AV1_MAX_SEGMENTS];
    int seg_id_pre_skip;

    struct av1_segmentation seg;

    int tx_mode;

    int loop_filter_level[4];
    int loop_filter_sharpness;
    int loop_filter_delta_enabled;
    int loop_filter_delta_update;
    int loop_filter_ref_deltas[8];
    int loop_filter_mode_deltas[2];

    int cdef_bits;
    int cdef_damping;
    int cdef_y_pri_strength[8];
    int cdef_y_sec_strength[8];
    int cdef_uv_pri_strength[8];
    int cdef_uv_sec_strength[8];

    int restoration_type[3];
    int lr_unit_size[3];
    int uses_lr;

    int skip_mode_present;
    int skip_mode_allowed;

    int film_grain_params_present;

    struct av1_tile_layout tiles;
};

/* ------------------------------------------------------------------ */
/* Per-tile decoding state                                            */
/* ------------------------------------------------------------------ */

struct av1_dec {
    struct av1_frame f;
    struct av1_cdfs cdf;

    struct av1_range rd;
    int have_range;

    uint16_t *plane[3];
    int stride[3];
    int plane_w[3], plane_h[3];

    int tile_col, tile_row;
    int mi_col_start, mi_row_start, mi_col_end, mi_row_end;

    int current_q_index;
    int delta_lf[AV1_FRAME_LF_COUNT];
    int read_deltas;

    /* Current block/mode info, 4x4 granularity. */
    int mi_row, mi_col;
    int mi_size;
    int bw4, bh4;
    int has_chroma;
    int avail_u, avail_l, avail_u_chroma, avail_l_chroma;
    int skip, skip_mode, is_inter, use_intrabc;
    int segment_id;
    int lossless;
    int y_mode, uv_mode;
    int angle_delta_y, angle_delta_uv;

    int palette_size_y, palette_size_uv;
    uint16_t palette_colors_y[8], palette_colors_u[8], palette_colors_v[8];
    uint8_t color_map_y[64][64];
    uint8_t color_map_uv[64][64];

    int use_filter_intra;
    int filter_intra_mode;
    int cfl_alpha_signs;
    int cfl_alpha_u, cfl_alpha_v;

    int tx_size;
    int plane_tx_type;
    int eob;
    int16_t quant[1024];
    int32_t dequant[64][64];
    int32_t residual[64][64];
    int max_luma_w, max_luma_h;     /* MaxLumaW / MaxLumaH (CFL input) */

    /* Per 4x4 (or plane sub-sampled) frame maps. */
    int mi_stride;
    int mi_stride_c[3];
    uint8_t *y_modes;               /* [mi_rows][mi_stride] */
    uint8_t *uv_modes;
    uint8_t *skips;
    uint8_t *is_inters;
    uint8_t *segment_ids;
    uint8_t *mi_sizes;
    uint8_t *tx_sizes;
    uint8_t *inter_tx_sizes;
    uint8_t *tx_types;
    int8_t *ref_frames;             /* [mi][2] */
    uint8_t *palette_sizes[2];
    uint16_t *palette_colors[2];
    int8_t *delta_lfs;              /* [mi][4] */
    int8_t *cdef_idx;

    int8_t *block_decoded[3];       /* (mi+2) x (mi+2) with offset */
    uint8_t *above_level[3];
    uint8_t *above_dc[3];
    uint8_t *left_level[3];
    uint8_t *left_dc[3];
    /* LoopfilterTxSizes: per-plane, bordered, indexed
     * [(row4 + 1) * mi_stride_c[plane] + col4 + 1]. */
    uint8_t *loopfilter_tx_sizes[3];

    /* Loop restoration units.  lr_types/lr_wiener/lr_sgrproj are allocated
     * by av1_decode_tile, indexed [unitRow * lr_unit_stride + unitCol]. */
    int lr_unit_cols[3], lr_unit_rows[3], lr_unit_stride;
    uint8_t *lr_types[3];
    int8_t *lr_wiener[3];           /* 6 signed taps per unit */
    int8_t *lr_sgrproj[3];          /* sgr param + 2 xqd per unit */

    /* Loop restoration reference coefficients (per tile). */
    int lr_ref_wiener[3][2][3];
    int lr_ref_sgr[3][2];

    int error;
};

/* av1.c */
int av1_decode_tiles(struct av1_dec *d, const uint8_t *data, size_t len,
                     int *consumed);

/* av1_decode.c */
void av1_decode_tile(struct av1_dec *d, const uint8_t *data, size_t len);
int av1_decode_partition(struct av1_dec *d, int r, int c, int bsize);
int av1_decode_block(struct av1_dec *d, int r, int c, int bsize);
int av1_get_qindex(struct av1_dec *d, int ignore_delta_q, int segment_id);
int av1_seg_feature_active(struct av1_dec *d, int feature);
int av1_plane_residual_size(struct av1_dec *d, int bsize, int plane);
int av1_find_tx_size(int w, int h);
int av1_get_tx_set(struct av1_dec *d, int tx_size, int *intra);

/* av1_recon.c */
void av1_predict_intra(struct av1_dec *d, int plane, int x, int y, int tx_size,
                       int have_left, int have_above, int have_above_right,
                       int have_below_left, int mode, int log2w, int log2h);
void av1_reconstruct(struct av1_dec *d, int plane, int x, int y, int tx_size,
                     int eob);
int av1_compute_tx_type(struct av1_dec *d, int plane, int tx_size, int x4,
                        int y4);
void av1_predict_palette(struct av1_dec *d, int plane, int start_x,
                         int start_y, int x, int y, int tx_size);

/* av1_filter.c */
void av1_loop_filter_frame(struct av1_dec *d);
void av1_cdef_frame(struct av1_dec *d);
void av1_loop_restoration_frame(struct av1_dec *d);
void av1_superres_frame(struct av1_dec *d);

#endif /* _AV1_INTERNAL_H_ */
