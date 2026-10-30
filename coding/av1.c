/*
 * AV1 still picture decoder.
 *
 * This implements the subset of the AV1 Bitstream & Decoding Process
 * Specification needed to decode an AVIF image item: sequence/frame header
 * parsing, the symbol (range) decoder, intra prediction, transforms,
 * coefficient decoding and the in-loop filters.  Inter frames, reference
 * frame handling and film grain synthesis are outside the scope of a still
 * image item and are rejected.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "av1_internal.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(av1, DEBUG)

/* ------------------------------------------------------------------ */
/* Range decoder                                                      */
/* ------------------------------------------------------------------ */

void
av1_range_init(struct av1_range *r, const uint8_t *data, size_t size)
{
    av1_bits_init(&r->bits, data, size);
    int num_bits = (int)(size * 8 < 15 ? size * 8 : 15);
    uint32_t buf = av1_bits_get(&r->bits, num_bits);
    uint32_t padded = buf << (15 - num_bits);
    r->value = 0x7fff ^ padded;
    r->range = 1 << 15;
    r->max_bits = (int)(size * 8) - 15;
}

static inline int
av1_floor_log2(uint32_t v)
{
    int n = 0;
    while (v > 1) {
        v >>= 1;
        n++;
    }
    return n;
}

int
av1_range_symbol(struct av1_range *r, uint16_t *cdf, int n, int update)
{
    uint32_t cur = r->range;
    uint32_t prev = 0;
    int symbol = -1;
    do {
        symbol++;
        if (symbol >= n) {
            r->bits.error = 1;
            return 0;
        }
        prev = cur;
        uint32_t f = (1u << 15) - cdf[symbol];
        cur = ((r->range >> 8) * (f >> 6)) >> 1;
        cur += 4 * (uint32_t)(n - symbol - 1);
    } while (r->value < cur);

    r->range = prev - cur;
    r->value -= cur;

    int bits = 15 - av1_floor_log2(r->range);
    r->range <<= bits;
    int num_bits = bits;
    int avail = r->max_bits > 0 ? r->max_bits : 0;
    if (num_bits > avail) {
        num_bits = avail;
    }
    uint32_t new_data = av1_bits_get(&r->bits, num_bits);
    uint32_t padded = new_data << (bits - num_bits);
    r->value = padded ^ (((r->value + 1) << bits) - 1);
    r->max_bits -= bits;

    if (update) {
        int rate = 3 + (cdf[n] > 15) + (cdf[n] > 31) +
                   (av1_floor_log2((uint32_t)n) < 2 ?
                    av1_floor_log2((uint32_t)n) : 2);
        uint32_t tmp = 0;
        for (int i = 0; i < n - 1; i++) {
            if (i == symbol) {
                tmp = 1 << 15;
            }
            if (tmp < cdf[i]) {
                cdf[i] -= (cdf[i] - tmp) >> rate;
            } else {
                cdf[i] += (tmp - cdf[i]) >> rate;
            }
        }
        cdf[n] += (cdf[n] < 32);
    }
    return symbol;
}

int
av1_range_bool(struct av1_range *r)
{
    uint16_t cdf[3] = { 1 << 14, 1 << 15, 0 };
    return av1_range_symbol(r, cdf, 2, 0);
}

uint32_t
av1_range_literal(struct av1_range *r, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        v = (v << 1) | (uint32_t)av1_range_bool(r);
    }
    return v;
}

int
av1_range_ns(struct av1_range *r, int n)
{
    int w = av1_floor_log2((uint32_t)n) + 1;
    int m = (1 << w) - n;
    int v = (int)av1_range_literal(r, w - 1);
    if (v < m) {
        return v;
    }
    int extra = av1_range_bool(r);
    return (v << 1) - m + extra;
}

int
av1_range_su(struct av1_range *r, int n)
{
    uint32_t v = av1_range_literal(r, n);
    uint32_t mask = 1u << (n - 1);
    if (v & mask) {
        return (int32_t)(v | ~((mask << 1) - 1));
    }
    return (int32_t)v;
}

/* ------------------------------------------------------------------ */
/* CDF initialisation                                                 */
/* ------------------------------------------------------------------ */

#define COPY_CDF(c, name, dst) \
    memcpy((dst), Default_##name##_Cdf, sizeof(dst))
#define COPY_ARR(c, name, dst) \
    memcpy((dst), name, sizeof(dst))

void
av1_cdf_init_non_coeff(struct av1_cdfs *c)
{
    COPY_CDF(c, Intra_Frame_Y_Mode, c->intra_frame_y_mode);
    COPY_CDF(c, Y_Mode, c->y_mode);
    COPY_CDF(c, Uv_Mode_Cfl_Not_Allowed, c->uv_mode_cfl_not_allowed);
    COPY_CDF(c, Uv_Mode_Cfl_Allowed, c->uv_mode_cfl_allowed);
    COPY_CDF(c, Angle_Delta, c->angle_delta);
    COPY_CDF(c, Partition_W8, c->partition_w8);
    COPY_CDF(c, Partition_W16, c->partition_w16);
    COPY_CDF(c, Partition_W32, c->partition_w32);
    COPY_CDF(c, Partition_W64, c->partition_w64);
    COPY_CDF(c, Partition_W128, c->partition_w128);
    COPY_CDF(c, Tx_8x8, c->tx_8x8);
    COPY_CDF(c, Tx_16x16, c->tx_16x16);
    COPY_CDF(c, Tx_32x32, c->tx_32x32);
    COPY_CDF(c, Tx_64x64, c->tx_64x64);
    COPY_CDF(c, Txfm_Split, c->txfm_split);
    COPY_ARR(c, Default_Filter_Intra_Mode_Cdf, c->filter_intra_mode);
    COPY_CDF(c, Filter_Intra, c->filter_intra);
    COPY_CDF(c, Segment_Id, c->segment_id);
    COPY_CDF(c, Segment_Id_Predicted, c->segment_id_predicted);
    COPY_CDF(c, Skip, c->skip);
    COPY_CDF(c, Intrabc, c->intrabc);
    COPY_CDF(c, Palette_Y_Size, c->palette_y_size);
    COPY_CDF(c, Palette_Uv_Size, c->palette_uv_size);
    COPY_CDF(c, Palette_Size_2_Y_Color, c->palette_size_2_y);
    COPY_CDF(c, Palette_Size_3_Y_Color, c->palette_size_3_y);
    COPY_CDF(c, Palette_Size_4_Y_Color, c->palette_size_4_y);
    COPY_CDF(c, Palette_Size_5_Y_Color, c->palette_size_5_y);
    COPY_CDF(c, Palette_Size_6_Y_Color, c->palette_size_6_y);
    COPY_CDF(c, Palette_Size_7_Y_Color, c->palette_size_7_y);
    COPY_CDF(c, Palette_Size_8_Y_Color, c->palette_size_8_y);
    COPY_CDF(c, Palette_Size_2_Uv_Color, c->palette_size_2_uv);
    COPY_CDF(c, Palette_Size_3_Uv_Color, c->palette_size_3_uv);
    COPY_CDF(c, Palette_Size_4_Uv_Color, c->palette_size_4_uv);
    COPY_CDF(c, Palette_Size_5_Uv_Color, c->palette_size_5_uv);
    COPY_CDF(c, Palette_Size_6_Uv_Color, c->palette_size_6_uv);
    COPY_CDF(c, Palette_Size_7_Uv_Color, c->palette_size_7_uv);
    COPY_CDF(c, Palette_Size_8_Uv_Color, c->palette_size_8_uv);
    COPY_CDF(c, Palette_Y_Mode, c->palette_y_mode);
    COPY_CDF(c, Palette_Uv_Mode, c->palette_uv_mode);
    COPY_ARR(c, Default_Delta_Q_Cdf, c->delta_q);
    COPY_ARR(c, Default_Delta_Lf_Cdf, c->delta_lf);
    COPY_CDF(c, Intra_Tx_Type_Set1, c->intra_tx_type_set1);
    COPY_CDF(c, Intra_Tx_Type_Set2, c->intra_tx_type_set2);
    COPY_ARR(c, Default_Cfl_Sign_Cdf, c->cfl_sign);
    COPY_CDF(c, Cfl_Alpha, c->cfl_alpha);
    COPY_ARR(c, Default_Use_Wiener_Cdf, c->use_wiener);
    COPY_ARR(c, Default_Use_Sgrproj_Cdf, c->use_sgrproj);
    COPY_ARR(c, Default_Restoration_Type_Cdf, c->restoration_type);
}

void
av1_cdf_init_coeff(struct av1_cdfs *c, int base_q_idx)
{
    int idx;
    if (base_q_idx <= 20) {
        idx = 0;
    } else if (base_q_idx <= 60) {
        idx = 1;
    } else if (base_q_idx <= 120) {
        idx = 2;
    } else {
        idx = 3;
    }
    memcpy(c->txb_skip, Default_Txb_Skip_Cdf[idx], sizeof(c->txb_skip));
    memcpy(c->eob_pt_16, Default_Eob_Pt_16_Cdf[idx], sizeof(c->eob_pt_16));
    memcpy(c->eob_pt_32, Default_Eob_Pt_32_Cdf[idx], sizeof(c->eob_pt_32));
    memcpy(c->eob_pt_64, Default_Eob_Pt_64_Cdf[idx], sizeof(c->eob_pt_64));
    memcpy(c->eob_pt_128, Default_Eob_Pt_128_Cdf[idx], sizeof(c->eob_pt_128));
    memcpy(c->eob_pt_256, Default_Eob_Pt_256_Cdf[idx], sizeof(c->eob_pt_256));
    memcpy(c->eob_pt_512, Default_Eob_Pt_512_Cdf[idx], sizeof(c->eob_pt_512));
    memcpy(c->eob_pt_1024, Default_Eob_Pt_1024_Cdf[idx], sizeof(c->eob_pt_1024));
    memcpy(c->eob_extra, Default_Eob_Extra_Cdf[idx], sizeof(c->eob_extra));
    memcpy(c->dc_sign, Default_Dc_Sign_Cdf[idx], sizeof(c->dc_sign));
    memcpy(c->coeff_base_eob, Default_Coeff_Base_Eob_Cdf[idx],
           sizeof(c->coeff_base_eob));
    memcpy(c->coeff_base, Default_Coeff_Base_Cdf[idx], sizeof(c->coeff_base));
    memcpy(c->coeff_br, Default_Coeff_Br_Cdf[idx], sizeof(c->coeff_br));
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static inline int
round2(int x, int n)
{
    return (x + (1 << (n - 1))) >> n;
}

static inline int
tile_log2(int blk_size, int target)
{
    int k = 0;
    while ((blk_size << k) < target) {
        k++;
    }
    return k;
}

/* ------------------------------------------------------------------ */
/* Sequence header                                                    */
/* ------------------------------------------------------------------ */

static int
seq_color_config(struct av1_bits *b, struct av1_frame *f)
{
    int high_bitdepth = av1_bits_bit(b);
    if (f->profile == 2 && high_bitdepth) {
        int twelve_bit = av1_bits_bit(b);
        f->bitdepth = twelve_bit ? 12 : 10;
    } else if (f->profile <= 2) {
        f->bitdepth = high_bitdepth ? 10 : 8;
    }

    if (f->profile == 1) {
        f->mono_chrome = 0;
    } else {
        f->mono_chrome = av1_bits_bit(b);
    }
    f->num_planes = f->mono_chrome ? 1 : 3;

    int color_description_present = av1_bits_bit(b);
    if (color_description_present) {
        f->color_primaries = av1_bits_get(b, 8);
        f->transfer_characteristics = av1_bits_get(b, 8);
        f->matrix_coefficients = av1_bits_get(b, 8);
    } else {
        f->color_primaries = 2;         /* CP_UNSPECIFIED */
        f->transfer_characteristics = 2;
        f->matrix_coefficients = 2;
    }

    if (f->mono_chrome) {
        f->color_range = av1_bits_bit(b);
        f->subsampling_x = 1;
        f->subsampling_y = 1;
        f->chroma_sample_position = 0;
    } else if (f->color_primaries == 1 && f->transfer_characteristics == 13 &&
               f->matrix_coefficients == 0) {
        f->color_range = 1;
        f->subsampling_x = 0;
        f->subsampling_y = 0;
    } else {
        f->color_range = av1_bits_bit(b);
        if (f->profile == 0) {
            f->subsampling_x = 1;
            f->subsampling_y = 1;
        } else if (f->profile == 1) {
            f->subsampling_x = 0;
            f->subsampling_y = 0;
        } else {
            if (f->bitdepth == 12) {
                f->subsampling_x = av1_bits_bit(b);
                f->subsampling_y = f->subsampling_x ? av1_bits_bit(b) : 0;
            } else {
                f->subsampling_x = 1;
                f->subsampling_y = 0;
            }
        }
        if (f->subsampling_x && f->subsampling_y) {
            f->chroma_sample_position = av1_bits_get(b, 2);
        }
    }
    /* separate_uv_delta_q */
    f->separate_uv_delta_q = av1_bits_bit(b);
    return 0;
}

static int
parse_sequence_header(struct av1_bits *b, struct av1_frame *f)
{
    f->profile = av1_bits_get(b, 3);
    f->still_picture = av1_bits_bit(b);
    f->reduced_still_picture_header = av1_bits_bit(b);

    int operating_points_cnt_minus_1;
    if (f->reduced_still_picture_header) {
        operating_points_cnt_minus_1 = 0;
        (void)av1_bits_get(b, 5);       /* seq_level_idx */
    } else {
        int timing_info_present = av1_bits_bit(b);
        int decoder_model_info_present = 0;
        if (timing_info_present) {
            decoder_model_info_present = av1_bits_bit(b);
            if (decoder_model_info_present) {
                (void)av1_bits_get(b, 5);
                (void)av1_bits_get(b, 32);
                (void)av1_bits_get(b, 5);
                (void)av1_bits_get(b, 5);
            }
        }
        int initial_display_delay_present = av1_bits_bit(b);
        operating_points_cnt_minus_1 = (int)av1_bits_get(b, 5);
        for (int i = 0; i <= operating_points_cnt_minus_1; i++) {
            (void)av1_bits_get(b, 12);
            int level = (int)av1_bits_get(b, 5);
            if (level > 7) {
                (void)av1_bits_bit(b);
            }
            if (decoder_model_info_present) {
                int present = av1_bits_bit(b);
                if (present) {
                    (void)av1_bits_get(b, 32);
                    (void)av1_bits_get(b, 32);
                    (void)av1_bits_bit(b);
                }
            }
            if (initial_display_delay_present) {
                int present = av1_bits_bit(b);
                if (present) {
                    (void)av1_bits_get(b, 4);
                }
            }
        }
    }
    (void)operating_points_cnt_minus_1;

    int width_bits = (int)av1_bits_get(b, 4) + 1;
    int height_bits = (int)av1_bits_get(b, 4) + 1;
    int max_width = (int)av1_bits_get(b, width_bits) + 1;
    int max_height = (int)av1_bits_get(b, height_bits) + 1;

    int frame_id_numbers_present = f->reduced_still_picture_header ?
                                   0 : av1_bits_bit(b);
    if (frame_id_numbers_present) {
        (void)av1_bits_get(b, 4);
        (void)av1_bits_get(b, 3);
    }

    f->use_128x128_superblock = av1_bits_bit(b);
    f->enable_filter_intra = av1_bits_bit(b);
    f->enable_intra_edge_filter = av1_bits_bit(b);

    int seq_force_screen_content_tools;
    int seq_force_integer_mv;
    if (f->reduced_still_picture_header) {
        seq_force_screen_content_tools = 2;     /* SELECT */
        seq_force_integer_mv = 2;
        f->order_hint = 0;
    } else {
        (void)av1_bits_bit(b);                  /* enable_interintra_compound */
        (void)av1_bits_bit(b);                  /* enable_masked_compound */
        (void)av1_bits_bit(b);                  /* enable_warped_motion */
        (void)av1_bits_bit(b);                  /* enable_dual_filter */
        int enable_order_hint = av1_bits_bit(b);
        if (enable_order_hint) {
            (void)av1_bits_bit(b);              /* enable_jnt_comp */
            (void)av1_bits_bit(b);              /* enable_ref_frame_mvs */
        }
        int choose_screen = av1_bits_bit(b);
        if (choose_screen) {
            seq_force_screen_content_tools = 2;
        } else {
            seq_force_screen_content_tools = av1_bits_bit(b);
        }
        if (seq_force_screen_content_tools > 0) {
            int choose_integer = av1_bits_bit(b);
            if (choose_integer) {
                seq_force_integer_mv = 2;
            } else {
                seq_force_integer_mv = av1_bits_bit(b);
            }
        } else {
            seq_force_integer_mv = 2;
        }
        if (enable_order_hint) {
            f->order_hint = (int)av1_bits_get(b, 3) + 1;
        } else {
            f->order_hint = 0;
        }
    }
    f->frame_size_override_flag = 0;
    f->use_superres = 0;
    f->superres_denom = 1;

    f->enable_superres = av1_bits_bit(b);
    f->enable_cdef = av1_bits_bit(b);
    f->enable_restoration = av1_bits_bit(b);
    seq_color_config(b, f);
    f->film_grain_params_present = av1_bits_bit(b);

    /* store for the frame header parser */
    f->allow_screen_content_tools = seq_force_screen_content_tools;
    f->force_integer_mv = seq_force_integer_mv;
    f->width = max_width;
    f->height = max_height;
    return b->error ? -EINVAL : 0;
}

/* ------------------------------------------------------------------ */
/* Frame header                                                       */
/* ------------------------------------------------------------------ */

static int
read_delta_q(struct av1_bits *b)
{
    if (av1_bits_bit(b)) {
        return av1_bits_su(b, 7);
    }
    return 0;
}

static int
parse_uncompressed_header(struct av1_bits *b, struct av1_dec *d)
{
    struct av1_frame *f = &d->f;

    if (f->reduced_still_picture_header) {
        f->frame_type = 0;      /* KEY_FRAME */
        f->frame_is_intra = 1;
        f->show_frame = 1;
        f->showable_frame = 0;
    } else {
        if (av1_bits_bit(b)) {
            VERR(av1, "show_existing_frame is not supported");
            return -ENOTSUP;
        }
        f->frame_type = (int)av1_bits_get(b, 2);
        f->frame_is_intra = (f->frame_type == 2 || f->frame_type == 0);
        f->show_frame = av1_bits_bit(b);
        if (f->show_frame) {
            f->showable_frame = f->frame_type != 0;
        } else {
            f->showable_frame = av1_bits_bit(b);
        }
        if (f->frame_type == 3 || (f->frame_type == 0 && f->show_frame)) {
            f->error_resilient_mode = 1;
        } else {
            f->error_resilient_mode = av1_bits_bit(b);
        }
    }

    f->disable_cdf_update = av1_bits_bit(b);
    int seq_screen = f->allow_screen_content_tools;
    if (seq_screen == 2) {
        f->allow_screen_content_tools = av1_bits_bit(b);
    } else {
        f->allow_screen_content_tools = seq_screen;
    }
    if (f->allow_screen_content_tools) {
        if (f->force_integer_mv == 2) {
            f->force_integer_mv = av1_bits_bit(b);
        }
    } else {
        f->force_integer_mv = 0;
    }
    if (f->frame_is_intra) {
        f->force_integer_mv = 1;
    }

    if (f->frame_type == 3) {
        f->frame_size_override_flag = 1;
    } else if (f->reduced_still_picture_header) {
        f->frame_size_override_flag = 0;
    } else {
        f->frame_size_override_flag = av1_bits_bit(b);
    }
    f->order_hint = f->order_hint ? (int)av1_bits_get(b, f->order_hint) : 0;
    if (f->frame_is_intra || f->error_resilient_mode) {
        f->primary_ref_frame = 7;       /* PRIMARY_REF_NONE */
    } else {
        f->primary_ref_frame = (int)av1_bits_get(b, 3);
    }

    f->allow_high_precision_mv = 0;
    f->use_ref_frame_mvs = 0;
    f->allow_intrabc = 0;

    if (f->frame_type == 3 || (f->frame_type == 0 && f->show_frame)) {
        f->refresh_frame_flags = 0xff;
    } else {
        f->refresh_frame_flags = (int)av1_bits_get(b, 8);
    }

    if (!f->frame_is_intra) {
        VERR(av1, "inter frames are not part of a still image item");
        return -ENOTSUP;
    }

    /* frame_size() */
    int max_width = f->width;
    int max_height = f->height;
    if (f->frame_size_override_flag) {
        int wbits = 0, hbits = 0;
        /* the sizes are re-read from the sequence header; recompute below */
        (void)max_width;
        (void)max_height;
        (void)wbits;
        (void)hbits;
        return -ENOTSUP;
    }
    f->upscaled_width = f->width;
    f->height = f->height;

    /* superres_params() */
    if (f->enable_superres) {
        f->use_superres = av1_bits_bit(b);
    } else {
        f->use_superres = 0;
    }
    if (f->use_superres) {
        f->superres_denom = (int)av1_bits_get(b, 3) + 9;
    } else {
        f->superres_denom = 8;
    }
    int upscaled = f->upscaled_width;
    if (f->use_superres) {
        f->width = (upscaled * 8 + (f->superres_denom / 2)) /
                   f->superres_denom;
    }
    f->mi_cols = 2 * ((f->width + 7) >> 3);
    f->mi_rows = 2 * ((f->height + 7) >> 3);

    /* render_size() */
    if (av1_bits_bit(b)) {
        f->render_width = (int)av1_bits_get(b, 16) + 1;
        f->render_height = (int)av1_bits_get(b, 16) + 1;
    } else {
        f->render_width = upscaled;
        f->render_height = f->height;
    }

    if (f->allow_screen_content_tools && upscaled == f->width) {
        f->allow_intrabc = av1_bits_bit(b);
    }

    if (f->reduced_still_picture_header || f->disable_cdf_update) {
        f->disable_frame_end_update_cdf = 1;
    } else {
        f->disable_frame_end_update_cdf = av1_bits_bit(b);
    }

    av1_cdf_init_non_coeff(&d->cdf);

    /* tile_info() */
    {
        struct av1_tile_layout *t = &f->tiles;
        int sb_size = f->use_128x128_superblock ? 128 : 64;
        int sb_shift = f->use_128x128_superblock ? 5 : 4;
        int sb_cols = (f->mi_cols + (sb_size >> 2) - 1) / (sb_size >> 2);
        int sb_rows = (f->mi_rows + (sb_size >> 2) - 1) / (sb_size >> 2);
        int max_tile_width_sb = 4096 / sb_size;
        int max_tile_area_sb = 4096 * 2304 / (sb_size * sb_size);
        int min_log2_tile_cols = tile_log2(max_tile_width_sb, sb_cols);
        int max_log2_tile_cols = tile_log2(1, sb_cols < 64 ? sb_cols : 64);
        int max_log2_tile_rows = tile_log2(1, sb_rows < 64 ? sb_rows : 64);
        int area_log2 = tile_log2(max_tile_area_sb, sb_rows * sb_cols);
        int min_log2_tiles = min_log2_tile_cols > area_log2 ?
                             min_log2_tile_cols : area_log2;

        int uniform = av1_bits_bit(b);
        if (uniform) {
            int cols_log2 = min_log2_tile_cols;
            while (cols_log2 < max_log2_tile_cols) {
                if (av1_bits_bit(b)) {
                    cols_log2++;
                } else {
                    break;
                }
            }
            int tile_width_sb = (sb_cols + (1 << cols_log2) - 1) >> cols_log2;
            int i = 0;
            for (int start = 0; start < sb_cols; start += tile_width_sb) {
                t->mi_col_starts[i++] = start << sb_shift;
            }
            t->mi_col_starts[i] = f->mi_cols;
            t->cols = i;
            t->cols_log2 = cols_log2;

            int min_log2_tile_rows = min_log2_tiles - cols_log2;
            if (min_log2_tile_rows < 0) {
                min_log2_tile_rows = 0;
            }
            int rows_log2 = min_log2_tile_rows;
            while (rows_log2 < max_log2_tile_rows) {
                if (av1_bits_bit(b)) {
                    rows_log2++;
                } else {
                    break;
                }
            }
            int tile_height_sb = (sb_rows + (1 << rows_log2) - 1) >> rows_log2;
            i = 0;
            for (int start = 0; start < sb_rows; start += tile_height_sb) {
                t->mi_row_starts[i++] = start << sb_shift;
            }
            t->mi_row_starts[i] = f->mi_rows;
            t->rows = i;
            t->rows_log2 = rows_log2;
        } else {
            int widest_tile_sb = 0;
            int start = 0;
            int i = 0;
            while (start < sb_cols) {
                t->mi_col_starts[i] = start << sb_shift;
                int max_width = sb_cols - start;
                if (max_width > max_tile_width_sb) {
                    max_width = max_tile_width_sb;
                }
                int width_minus_1 = av1_range_ns(&d->rd, max_width);
                /* the non-uniform case is coded with the range decoder, not
                 * the raw bit reader; keep it unsupported for now */
                (void)width_minus_1;
                (void)widest_tile_sb;
                return -ENOTSUP;
            }
            t->mi_col_starts[i] = f->mi_cols;
            t->cols = i;
        }

        if (t->cols_log2 > 0 || t->rows_log2 > 0) {
            int bits = t->cols_log2 + t->rows_log2;
            t->context_update_tile_id = (int)av1_bits_get(b, bits);
            t->size_bytes = (int)av1_bits_get(b, 2) + 1;
        } else {
            t->context_update_tile_id = 0;
            t->size_bytes = 0;
        }
    }

    /* quantization_params() */
    f->base_q_idx = (int)av1_bits_get(b, 8);
    f->delta_q_ydc = read_delta_q(b);
    if (f->num_planes > 1) {
        int diff_uv = f->separate_uv_delta_q ? av1_bits_bit(b) : 0;
        f->delta_q_udc = read_delta_q(b);
        f->delta_q_uac = read_delta_q(b);
        if (diff_uv) {
            f->delta_q_vdc = read_delta_q(b);
            f->delta_q_vac = read_delta_q(b);
        } else {
            f->delta_q_vdc = f->delta_q_udc;
            f->delta_q_vac = f->delta_q_uac;
        }
    } else {
        f->delta_q_udc = f->delta_q_uac = 0;
        f->delta_q_vdc = f->delta_q_vac = 0;
    }
    f->using_qmatrix = av1_bits_bit(b);
    if (f->using_qmatrix) {
        f->qm_y = (int)av1_bits_get(b, 4);
        f->qm_u = (int)av1_bits_get(b, 4);
        f->qm_v = f->separate_uv_delta_q ? (int)av1_bits_get(b, 4) : f->qm_u;
    }

    /* segmentation_params() */
    memset(&f->seg, 0, sizeof(f->seg));
    f->seg.enabled = av1_bits_bit(b);
    if (f->seg.enabled) {
        if (f->primary_ref_frame == 7) {
            f->seg.update_map = 1;
            f->seg.temporal_update = 0;
            f->seg.update_data = 1;
        } else {
            f->seg.update_map = av1_bits_bit(b);
            if (f->seg.update_map) {
                f->seg.temporal_update = av1_bits_bit(b);
            }
            f->seg.update_data = av1_bits_bit(b);
        }
        if (f->seg.update_data) {
            for (int i = 0; i < AV1_MAX_SEGMENTS; i++) {
                for (int j = 0; j < 8; j++) {
                    int enabled = av1_bits_bit(b);
                    f->seg.feature_enabled[i][j] = enabled;
                    int value = 0;
                    if (enabled) {
                        int bits = Segmentation_Feature_Bits[j];
                        int limit = Segmentation_Feature_Max[j];
                        if (Segmentation_Feature_Signed[j]) {
                            value = av1_bits_su(b, 1 + bits);
                            value = clip3(-limit, limit, value);
                        } else {
                            value = (int)av1_bits_get(b, bits);
                            value = clip3(0, limit, value);
                        }
                    }
                    f->seg.feature_data[i][j] = value;
                }
            }
        }
    }
    f->seg.last_active_seg_id = 0;
    f->seg_id_pre_skip = 0;
    for (int i = 0; i < AV1_MAX_SEGMENTS; i++) {
        for (int j = 0; j < 8; j++) {
            if (f->seg.feature_enabled[i][j]) {
                f->seg.last_active_seg_id = i;
                if (j >= 5) {
                    f->seg_id_pre_skip = 1;
                }
            }
        }
    }

    /* delta_q_params() */
    f->delta_q_res = 0;
    f->delta_q_present = 0;
    if (f->base_q_idx > 0) {
        f->delta_q_present = av1_bits_bit(b);
    }
    if (f->delta_q_present) {
        f->delta_q_res = (int)av1_bits_get(b, 2);
    }

    /* delta_lf_params() */
    f->delta_lf_present = 0;
    f->delta_lf_res = 0;
    f->delta_lf_multi = 0;
    if (f->delta_q_present) {
        if (!f->allow_intrabc) {
            f->delta_lf_present = av1_bits_bit(b);
        }
        if (f->delta_lf_present) {
            f->delta_lf_res = (int)av1_bits_get(b, 2);
            f->delta_lf_multi = av1_bits_bit(b);
        }
    }

    av1_cdf_init_coeff(&d->cdf, f->base_q_idx);

    f->coded_lossless = 1;
    for (int i = 0; i < AV1_MAX_SEGMENTS; i++) {
        int q = av1_get_qindex(d, 1, i);
        f->lossless[i] = q == 0 && f->delta_q_ydc == 0 &&
                         f->delta_q_uac == 0 && f->delta_q_udc == 0 &&
                         f->delta_q_vac == 0 && f->delta_q_vdc == 0;
        if (!f->lossless[i]) {
            f->coded_lossless = 0;
        }
    }
    f->all_lossless = f->coded_lossless && f->width == f->upscaled_width;

    /* loop_filter_params() */
    if (f->coded_lossless || f->allow_intrabc) {
        for (int i = 0; i < 4; i++) {
            f->loop_filter_level[i] = 0;
        }
        f->loop_filter_sharpness = 0;
        f->loop_filter_delta_enabled = 0;
        f->loop_filter_delta_update = 0;
        f->loop_filter_ref_deltas[0] = 1;
        for (int i = 1; i < 8; i++) {
            f->loop_filter_ref_deltas[i] = (i == 5 || i == 6 || i == 7) ? -1 : 0;
        }
        f->loop_filter_mode_deltas[0] = 0;
        f->loop_filter_mode_deltas[1] = 0;
    } else {
        f->loop_filter_level[0] = (int)av1_bits_get(b, 6);
        f->loop_filter_level[1] = (int)av1_bits_get(b, 6);
        if (f->num_planes > 1) {
            if (f->loop_filter_level[0] || f->loop_filter_level[1]) {
                f->loop_filter_level[2] = (int)av1_bits_get(b, 6);
                f->loop_filter_level[3] = (int)av1_bits_get(b, 6);
            } else {
                f->loop_filter_level[2] = f->loop_filter_level[3] = 0;
            }
        }
        f->loop_filter_sharpness = (int)av1_bits_get(b, 3);
        f->loop_filter_ref_deltas[0] = 1;
        for (int i = 1; i < 8; i++) {
            f->loop_filter_ref_deltas[i] = (i == 5 || i == 6 || i == 7) ? -1 : 0;
        }
        f->loop_filter_mode_deltas[0] = 0;
        f->loop_filter_mode_deltas[1] = 0;
        f->loop_filter_delta_enabled = av1_bits_bit(b);
        if (f->loop_filter_delta_enabled) {
            f->loop_filter_delta_update = av1_bits_bit(b);
            if (f->loop_filter_delta_update) {
                for (int i = 0; i < 8; i++) {
                    if (av1_bits_bit(b)) {
                        f->loop_filter_ref_deltas[i] = av1_bits_su(b, 7);
                    }
                }
                for (int i = 0; i < 2; i++) {
                    if (av1_bits_bit(b)) {
                        f->loop_filter_mode_deltas[i] = av1_bits_su(b, 7);
                    }
                }
            }
        }
    }

    /* cdef_params() */
    if (f->coded_lossless || f->allow_intrabc || !f->enable_cdef) {
        f->cdef_bits = 0;
        f->cdef_y_pri_strength[0] = 0;
        f->cdef_y_sec_strength[0] = 0;
        f->cdef_uv_pri_strength[0] = 0;
        f->cdef_uv_sec_strength[0] = 0;
        f->cdef_damping = 3;
    } else {
        f->cdef_damping = (int)av1_bits_get(b, 2) + 3;
        f->cdef_bits = (int)av1_bits_get(b, 2);
        for (int i = 0; i < (1 << f->cdef_bits); i++) {
            f->cdef_y_pri_strength[i] = (int)av1_bits_get(b, 4);
            f->cdef_y_sec_strength[i] = (int)av1_bits_get(b, 2);
            if (f->cdef_y_sec_strength[i] == 3) {
                f->cdef_y_sec_strength[i]++;
            }
            if (f->num_planes > 1) {
                f->cdef_uv_pri_strength[i] = (int)av1_bits_get(b, 4);
                f->cdef_uv_sec_strength[i] = (int)av1_bits_get(b, 2);
                if (f->cdef_uv_sec_strength[i] == 3) {
                    f->cdef_uv_sec_strength[i]++;
                }
            }
        }
    }

    /* lr_params() */
    if (f->all_lossless || f->allow_intrabc || !f->enable_restoration) {
        f->restoration_type[0] = f->restoration_type[1] =
            f->restoration_type[2] = 0;
        f->uses_lr = 0;
    } else {
        f->uses_lr = 0;
        int uses_chroma_lr = 0;
        for (int i = 0; i < f->num_planes; i++) {
            int lr_type = (int)av1_bits_get(b, 2);
            f->restoration_type[i] = Remap_Lr_Type[lr_type];
            if (f->restoration_type[i] != 0) {
                f->uses_lr = 1;
                if (i > 0) {
                    uses_chroma_lr = 1;
                }
            }
        }
        if (f->uses_lr) {
            int lr_unit_shift;
            if (f->use_128x128_superblock) {
                lr_unit_shift = av1_bits_bit(b) + 1;
            } else {
                lr_unit_shift = av1_bits_bit(b);
                if (lr_unit_shift) {
                    lr_unit_shift += av1_bits_bit(b);
                }
            }
            f->lr_unit_size[0] = 256 >> (2 - lr_unit_shift);
            int lr_uv_shift = 0;
            if (f->subsampling_x && f->subsampling_y && uses_chroma_lr) {
                lr_uv_shift = av1_bits_bit(b);
            }
            f->lr_unit_size[1] = f->lr_unit_size[0] >> lr_uv_shift;
            f->lr_unit_size[2] = f->lr_unit_size[0] >> lr_uv_shift;
        }
    }

    /* read_tx_mode() */
    if (f->coded_lossless) {
        f->tx_mode = 0;         /* ONLY_4X4 */
    } else {
        f->tx_mode = av1_bits_bit(b) ? 2 : 1;   /* SELECT : LARGEST */
    }

    /* frame_reference_mode() */
    f->reference_select = 0;    /* intra frame */

    /* skip_mode_params() */
    f->skip_mode_allowed = 0;
    f->skip_mode_present = 0;

    f->allow_warped_motion = 0;
    f->reduced_tx_set = av1_bits_bit(b);
    /* global_motion_params() returns immediately for an intra frame */

    /* film_grain_params() */
    if (!f->film_grain_params_present || (!f->show_frame && !f->showable_frame)) {
        /* reset */
    } else {
        int apply_grain = av1_bits_bit(b);
        if (apply_grain) {
            (void)av1_bits_get(b, 16);
            int update_grain = (f->frame_type == 1) ? av1_bits_bit(b) : 1;
            if (update_grain) {
                int num_y_points = (int)av1_bits_get(b, 4);
                for (int i = 0; i < num_y_points; i++) {
                    (void)av1_bits_get(b, 8);
                    (void)av1_bits_get(b, 8);
                }
                int chroma_scaling_from_luma = 0;
                if (f->mono_chrome) {
                    chroma_scaling_from_luma = 0;
                } else {
                    chroma_scaling_from_luma = av1_bits_bit(b);
                }
                int num_cb_points = 0, num_cr_points = 0;
                if (!f->mono_chrome && !chroma_scaling_from_luma) {
                    num_cb_points = (int)av1_bits_get(b, 4);
                    for (int i = 0; i < num_cb_points; i++) {
                        (void)av1_bits_get(b, 8);
                        (void)av1_bits_get(b, 8);
                    }
                    num_cr_points = (int)av1_bits_get(b, 4);
                    for (int i = 0; i < num_cr_points; i++) {
                        (void)av1_bits_get(b, 8);
                        (void)av1_bits_get(b, 8);
                    }
                }
                (void)av1_bits_get(b, 2);       /* grain_scaling_minus_8 */
                int ar_coeff_lag = (int)av1_bits_get(b, 2);
                int num_pos_luma = 2 * ar_coeff_lag * (ar_coeff_lag + 1);
                if (num_y_points) {
                    for (int i = 0; i < num_pos_luma; i++) {
                        (void)av1_bits_get(b, 8);
                    }
                }
                if (chroma_scaling_from_luma || num_cb_points) {
                    for (int i = 0; i < num_pos_luma + 1; i++) {
                        (void)av1_bits_get(b, 8);
                    }
                }
                if (chroma_scaling_from_luma || num_cr_points) {
                    for (int i = 0; i < num_pos_luma + 1; i++) {
                        (void)av1_bits_get(b, 8);
                    }
                }
                (void)av1_bits_get(b, 2);       /* ar_coeff_shift_minus_6 */
                (void)av1_bits_get(b, 2);       /* grain_scale_shift */
                if (num_cb_points) {
                    (void)av1_bits_get(b, 8);
                    (void)av1_bits_get(b, 8);
                    (void)av1_bits_get(b, 9);
                }
                if (num_cr_points) {
                    (void)av1_bits_get(b, 8);
                    (void)av1_bits_get(b, 8);
                    (void)av1_bits_get(b, 9);
                }
                (void)av1_bits_get(b, 2);       /* grain_center */
                (void)av1_bits_get(b, 2);       /* grain_min */
                (void)av1_bits_get(b, 2);       /* grain_max */
            }
        }
    }

    return b->error ? -EINVAL : 0;
}

/* frame struct needs this field; declared here to keep the public header
 * small */
/* (the field is added through the internal header) */

/* ------------------------------------------------------------------ */
/* OBU parsing                                                        */
/* ------------------------------------------------------------------ */

static int
parse_obu_header(struct av1_bits *b, int *type, int *has_size, int *ext)
{
    (void)av1_bits_bit(b);              /* forbidden */
    *type = (int)av1_bits_get(b, 4);
    *ext = av1_bits_bit(b);
    *has_size = av1_bits_bit(b);
    (void)av1_bits_bit(b);              /* reserved */
    if (*ext) {
        (void)av1_bits_get(b, 8);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Frame buffer / tile group                                          */
/* ------------------------------------------------------------------ */

static void
frame_free_buffers(struct av1_dec *d)
{
    for (int i = 0; i < 3; i++) {
        free(d->plane[i]);
        free(d->block_decoded[i]);
        free(d->above_level[i]);
        free(d->above_dc[i]);
        free(d->left_level[i]);
        free(d->left_dc[i]);
        free(d->loopfilter_tx_sizes[i]);
        d->plane[i] = NULL;
        d->block_decoded[i] = NULL;
        d->above_level[i] = NULL;
        d->above_dc[i] = NULL;
        d->left_level[i] = NULL;
        d->left_dc[i] = NULL;
        d->loopfilter_tx_sizes[i] = NULL;
    }
    free(d->y_modes);
    free(d->uv_modes);
    free(d->skips);
    free(d->is_inters);
    free(d->segment_ids);
    free(d->mi_sizes);
    free(d->tx_sizes);
    free(d->inter_tx_sizes);
    free(d->tx_types);
    free(d->ref_frames);
    free(d->palette_sizes[0]);
    free(d->palette_sizes[1]);
    free(d->palette_colors[0]);
    free(d->palette_colors[1]);
    free(d->delta_lfs);
    free(d->cdef_idx);
    for (int i = 0; i < 3; i++) {
        free(d->lr_types[i]);
        free(d->lr_wiener[i]);
        free(d->lr_sgrproj[i]);
        d->lr_types[i] = NULL;
        d->lr_wiener[i] = NULL;
        d->lr_sgrproj[i] = NULL;
    }
    d->y_modes = d->uv_modes = d->skips = d->is_inters = NULL;
    d->segment_ids = d->mi_sizes = d->tx_sizes = NULL;
    d->inter_tx_sizes = d->tx_types = NULL;
    d->ref_frames = NULL;
    d->palette_sizes[0] = d->palette_sizes[1] = NULL;
    d->palette_colors[0] = d->palette_colors[1] = NULL;
    d->delta_lfs = NULL;
    d->cdef_idx = NULL;
}

static int
frame_alloc_buffers(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int mi_cols = f->mi_cols;
    int mi_rows = f->mi_rows;
    /* blocks may extend past the frame bottom/right edge (up to 64x64 luma
     * beyond the boundary), so the mi grid gets padding rows */
    int mi_count = mi_cols * (mi_rows + 32);

    for (int p = 0; p < f->num_planes; p++) {
        int sub_x = p ? f->subsampling_x : 0;
        int sub_y = p ? f->subsampling_y : 0;
        int w = (f->width + sub_x) >> sub_x;
        int h = (f->height + sub_y) >> sub_y;
        /* Transform blocks overhang the frame edge. Their reconstructed
         * samples past the visible area are still read by CFL prediction
         * (the luma extrapolation in this frame's chroma blocks), so the
         * planes are padded out to the superblock grid. */
        int stride = (w + 127) & ~127;
        int hpad = (h + 127) & ~127;
        d->plane_w[p] = w;
        d->plane_h[p] = h;
        d->stride[p] = stride;
        d->plane[p] = calloc((size_t)stride * hpad, sizeof(uint16_t));
        if (!d->plane[p]) {
            return -ENOMEM;
        }
        int cmi_w = (mi_cols + sub_x) >> sub_x;
        int cmi_h = (mi_rows + sub_y) >> sub_y;
        d->mi_stride_c[p] = cmi_w + 2;
        d->block_decoded[p] = calloc((size_t)(cmi_w + 2) * (cmi_h + 2),
                                     sizeof(int8_t));
        d->above_level[p] = calloc(cmi_w + 2, sizeof(uint8_t));
        d->above_dc[p] = calloc(cmi_w + 2, sizeof(uint8_t));
        d->left_level[p] = calloc(cmi_h + 2, sizeof(uint8_t));
        d->left_dc[p] = calloc(cmi_h + 2, sizeof(uint8_t));
        d->loopfilter_tx_sizes[p] = calloc((size_t)(cmi_w + 2) * (cmi_h + 2),
                                           sizeof(uint8_t));
        if (!d->block_decoded[p] || !d->above_level[p] || !d->above_dc[p] ||
            !d->left_level[p] || !d->left_dc[p] || !d->loopfilter_tx_sizes[p]) {
            return -ENOMEM;
        }
    }
    for (int p = f->num_planes; p < 3; p++) {
        d->plane_w[p] = d->plane_h[p] = d->stride[p] = 0;
    }

    d->mi_stride = mi_cols;
    d->y_modes = calloc(mi_count, 1);
    d->uv_modes = calloc(mi_count, 1);
    d->skips = calloc(mi_count, 1);
    d->is_inters = calloc(mi_count, 1);
    d->segment_ids = calloc(mi_count, 1);
    d->mi_sizes = calloc(mi_count, 1);
    d->tx_sizes = calloc(mi_count, 1);
    d->inter_tx_sizes = calloc(mi_count, 1);
    d->tx_types = calloc(mi_count, 1);
    d->ref_frames = calloc((size_t)mi_count * 2, 1);
    d->palette_sizes[0] = calloc(mi_count, 1);
    d->palette_sizes[1] = calloc(mi_count, 1);
    d->palette_colors[0] = calloc((size_t)mi_count * 8, sizeof(uint16_t));
    d->palette_colors[1] = calloc((size_t)mi_count * 8, sizeof(uint16_t));
    d->delta_lfs = calloc((size_t)mi_count * 4, 1);
    d->cdef_idx = calloc(mi_count, 1);
    if (!d->y_modes || !d->uv_modes || !d->skips || !d->is_inters ||
        !d->segment_ids || !d->mi_sizes || !d->tx_sizes ||
        !d->inter_tx_sizes ||
        !d->tx_types || !d->ref_frames || !d->palette_sizes[0] ||
        !d->palette_sizes[1] || !d->palette_colors[0] ||
        !d->palette_colors[1] || !d->delta_lfs || !d->cdef_idx) {
        return -ENOMEM;
    }
    return 0;
}

static int
parse_tile_group(struct av1_dec *d, const uint8_t *data, size_t len)
{
    struct av1_frame *f = &d->f;
    struct av1_bits b;
    av1_bits_init(&b, data, len);
    int num_tiles = f->tiles.cols * f->tiles.rows;
    int start = 0, end = num_tiles - 1;

    if (num_tiles > 1 && av1_bits_bit(&b)) {
        int bits = f->tiles.cols_log2 + f->tiles.rows_log2;
        start = (int)av1_bits_get(&b, bits);
        end = (int)av1_bits_get(&b, bits);
        if (end < start || end >= num_tiles) {
            return -EINVAL;
        }
    }
    av1_bits_align(&b);
    size_t off = b.bitpos >> 3;
    if (off > len) {
        return -EINVAL;
    }
    const uint8_t *p = data + off;
    size_t left = len - off;

    for (int tile_num = start; tile_num <= end; tile_num++) {
        int tile_row = tile_num / f->tiles.cols;
        int tile_col = tile_num % f->tiles.cols;
        size_t tile_size;
        if (tile_num == end) {
            tile_size = left;
        } else {
            if ((size_t)f->tiles.size_bytes > left) {
                return -EINVAL;
            }
            uint32_t v = 0;
            for (int i = 0; i < f->tiles.size_bytes; i++) {
                v |= (uint32_t)p[i] << (i * 8);
            }
            tile_size = (size_t)v + 1;
            p += f->tiles.size_bytes;
            left -= f->tiles.size_bytes;
            if (tile_size > left) {
                return -EINVAL;
            }
        }
        d->tile_row = tile_row;
        d->tile_col = tile_col;
        d->mi_row_start = f->tiles.mi_row_starts[tile_row];
        d->mi_row_end = f->tiles.mi_row_starts[tile_row + 1];
        d->mi_col_start = f->tiles.mi_col_starts[tile_col];
        d->mi_col_end = f->tiles.mi_col_starts[tile_col + 1];
        av1_decode_tile(d, p, tile_size);
        if (d->error) {
            return -EINVAL;
        }
        p += tile_size;
        left -= tile_size;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                */
/* ------------------------------------------------------------------ */

int
av1_decode_image(const uint8_t *data, size_t len, struct av1_image *img)
{
    memset(img, 0, sizeof(*img));
    struct av1_dec *d = calloc(1, sizeof(*d));
    if (!d) {
        return -ENOMEM;
    }
    struct av1_frame *f = &d->f;

    int seq_seen = 0;
    int frame_header_seen = 0;
    int frame_header_len_bytes = 0;
    const uint8_t *frame_payload = NULL;
    size_t frame_payload_len = 0;
    int frame_payload_consumed = 0;
    int rc = 0;

    size_t pos = 0;
    while (pos < len) {
        struct av1_bits b;
        av1_bits_init(&b, data + pos, 1);
        int type, has_size, ext;
        parse_obu_header(&b, &type, &has_size, &ext);
        VDBG(av1, "obu type %d has_size %d at %zu", type, has_size, pos);
        size_t header_size = 1 + (ext ? 1 : 0);
        size_t obu_size;
        if (has_size) {
            struct av1_bits sb;
            av1_bits_init(&sb, data + pos + header_size,
                          len - pos - header_size);
            obu_size = av1_bits_leb128(&sb);
            header_size += sb.bitpos >> 3;
        } else {
            obu_size = len - pos - header_size;
        }
        if (pos + header_size + obu_size > len) {
            rc = -EINVAL;
            break;
        }
        const uint8_t *payload = data + pos + header_size;

        if (type == 1) {        /* OBU_SEQUENCE_HEADER */
            struct av1_bits sb;
            av1_bits_init(&sb, payload, obu_size);
            rc = parse_sequence_header(&sb, f);
            if (rc < 0) {
                break;
            }
            seq_seen = 1;
        } else if (type == 3 || type == 7) {    /* frame / redundant header */
            if (!seq_seen) {
                rc = -EINVAL;
                break;
            }
            if (!frame_header_seen) {
                struct av1_bits fb;
                av1_bits_init(&fb, payload, obu_size);
                int start_pos = (int)fb.bitpos;
                rc = parse_uncompressed_header(&fb, d);
                if (rc < 0) {
                    break;
                }
                frame_header_len_bytes = (int)((fb.bitpos + 7) / 8);
                (void)start_pos;
                frame_header_seen = 1;
            }
            if (type == 3) {
                /* frame header only; the tile group follows in its own OBU */
            }
        } else if (type == 6) { /* OBU_FRAME */
            if (!seq_seen) {
                rc = -EINVAL;
                break;
            }
            struct av1_bits fb;
            av1_bits_init(&fb, payload, obu_size);
            rc = parse_uncompressed_header(&fb, d);
            if (rc < 0) {
                break;
            }
            frame_header_len_bytes = (int)((fb.bitpos + 7) / 8);
            frame_header_seen = 1;
            frame_payload = payload + frame_header_len_bytes;
            frame_payload_len = obu_size - (size_t)frame_header_len_bytes;
            frame_payload_consumed = 1;
        } else if (type == 4) { /* OBU_TILE_GROUP */
            if (!frame_header_seen) {
                rc = -EINVAL;
                break;
            }
            rc = frame_alloc_buffers(d);
            if (rc < 0) {
                break;
            }
            rc = parse_tile_group(d, payload, obu_size);
            frame_payload_consumed = 1;
            pos += header_size + obu_size;
            break;
        }
        pos += header_size + obu_size;

        if (frame_payload_consumed && type == 6) {
            rc = frame_alloc_buffers(d);
            if (rc < 0) {
                break;
            }
            rc = parse_tile_group(d, frame_payload, frame_payload_len);
            break;
        }
    }

    if (rc == 0 && frame_header_seen && !frame_payload_consumed) {
        rc = -EINVAL;
    }
    if (rc == 0 && d->error) {
        rc = -EINVAL;
    }
    if (rc == 0 && frame_header_seen) {
        /* In-loop filters run after the whole frame is reconstructed. */
        if (!f->all_lossless && !f->allow_intrabc) {
            av1_loop_filter_frame(d);
            av1_cdef_frame(d);
            av1_loop_restoration_frame(d);
        }
        if (f->use_superres) {
            av1_superres_frame(d);
        }

        img->width = f->upscaled_width;
        img->height = f->height;
        img->bitdepth = f->bitdepth;
        img->num_planes = f->num_planes;
        img->mono_chrome = f->mono_chrome;
        img->subsampling_x = f->subsampling_x;
        img->subsampling_y = f->subsampling_y;
        img->color_range = f->color_range;
        img->matrix_coefficients = f->matrix_coefficients;
        img->color_primaries = f->color_primaries;
        img->transfer_characteristics = f->transfer_characteristics;
        img->chroma_sample_position = f->chroma_sample_position;
        for (int p = 0; p < f->num_planes; p++) {
            img->plane[p] = d->plane[p];
            img->stride[p] = d->stride[p];
            d->plane[p] = NULL;
        }
    }

    frame_free_buffers(d);
    free(d);
    return rc;
}

void
av1_image_free(struct av1_image *img)
{
    for (int i = 0; i < 3; i++) {
        free(img->plane[i]);
        img->plane[i] = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* YUV -> BGRA                                                        */
/* ------------------------------------------------------------------ */

static inline int
av1_clip_pixel(int v, int bitdepth)
{
    int max = (1 << bitdepth) - 1;
    return v < 0 ? 0 : (v > max ? max : v);
}

static inline int
av1_round_shift(int v, int s)
{
    return s ? ((v + (1 << (s - 1))) >> s) : v;
}

static void
av1_yuv_to_rgb(int y, int u, int v, int bitdepth, int full_range,
               int matrix, int *r, int *g, int *b)
{
    int shift = bitdepth - 8;
    int cr, cb;
    if (full_range) {
        cr = v - (128 << shift);
        cb = u - (128 << shift);
    } else {
        int half = 1 << (bitdepth - 1);
        y = y - (16 << shift);
        cr = v - half;
        cb = u - half;
    }

    int ry, gu, gv, bu;
    switch (matrix) {
    case 1:     /* BT.709 */
        ry = 18997; gu = 7884; gv = 16388; bu = 23172;
        break;
    case 9:     /* BT.2020 non-constant luminance */
    case 10:    /* BT.2020 constant luminance */
        ry = 18986; gu = 7673; gv = 16142; bu = 24041;
        break;
    case 4:     /* FCC */
        ry = 23136; gu = 7874; gv = 16244; bu = 18504;
        break;
    case 5:     /* BT.470 B/G */
    case 6:     /* BT.601 */
    default:
        ry = 26149; gu = 6419; gv = 13320; bu = 33050;
        break;
    }

    if (full_range) {
        *r = y + av1_round_shift(ry * cr, 14);
        *g = y - av1_round_shift(gu * cb + gv * cr, 14);
        *b = y + av1_round_shift(bu * cb, 14);
    } else {
        /* fixed point BT.601 style conversion for limited range */
        int c = 1 << (bitdepth - 8);
        int yy = y < 0 ? 0 : y;
        *r = av1_round_shift(ry * (cr >> 0), 14);
        *g = -av1_round_shift(gu * cb + gv * cr, 14);
        *b = av1_round_shift(bu * cb, 14);
        *r += av1_round_shift(298 * (yy * c) , 8) >> shift;
        *g += av1_round_shift(298 * (yy * c) , 8) >> shift;
        *b += av1_round_shift(298 * (yy * c) , 8) >> shift;
    }
}

int
av1_image_to_bgra(const struct av1_image *img, uint8_t *dst, int pitch)
{
    int w = img->width;
    int h = img->height;
    int bd = img->bitdepth;
    int shift = bd - 8;
    int max = (1 << bd) - 1;

    for (int y = 0; y < h; y++) {
        uint8_t *row = dst + (size_t)y * pitch;
        for (int x = 0; x < w; x++) {
            int yy = img->plane[0][(size_t)y * img->stride[0] + x];
            int u = 128 << shift, v = 128 << shift;
            if (img->num_planes > 1) {
                int sx = img->subsampling_x;
                int sy = img->subsampling_y;
                int cx = sx ? x >> 1 : x;
                int cy = sy ? y >> 1 : y;
                u = img->plane[1][(size_t)cy * img->stride[1] + cx];
                v = img->plane[2][(size_t)cy * img->stride[2] + cx];
            }
            int r, g, b;
            if (img->num_planes > 1 && img->matrix_coefficients == 0 &&
                !img->subsampling_x && !img->subsampling_y) {
                /* identity (GBR) */
                r = v;
                g = yy;
                b = u;
            } else if (img->num_planes == 1) {
                r = g = b = yy;
            } else {
                av1_yuv_to_rgb(yy, u, v, bd, img->color_range,
                               img->matrix_coefficients, &r, &g, &b);
            }
            r = av1_clip_pixel(r, bd);
            g = av1_clip_pixel(g, bd);
            b = av1_clip_pixel(b, bd);
            if (bd > 8) {
                r = (r + (1 << (shift - 1))) >> shift;
                g = (g + (1 << (shift - 1))) >> shift;
                b = (b + (1 << (shift - 1))) >> shift;
            }
            row[x * 4 + 0] = (uint8_t)b;
            row[x * 4 + 1] = (uint8_t)g;
            row[x * 4 + 2] = (uint8_t)r;
            row[x * 4 + 3] = 255;
            (void)max;
        }
    }
    return 0;
}
