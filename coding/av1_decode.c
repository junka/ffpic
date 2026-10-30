/*
 * AV1 tile decoding: decode_tile, decode_partition, decode_block,
 * mode info, palette, tx sizes, coefficients.
 *
 * Follows the AV1 Bitstream & Decoding Process Specification.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "av1_internal.h"
#include "utils.h"
#include "vlog.h"

extern int vlog_av1;

#define SIG_COEF_CONTEXTS       42
#define SIG_COEF_CONTEXTS_2D    26
#define SIG_COEF_CONTEXTS_EOB   4
#define NUM_BASE_LEVELS         2
#define COEFF_BASE_RANGE        12
#define PALETTE_COLORS          8
#define MAX_LOOP_FILTER         63
#define MAX_ANGLE_DELTA         3
#define DELTA_Q_SMALL           3
#define DELTA_LF_SMALL          3
#define SGRPROJ_PRJ_SUBEXP_K    4
#define SGRPROJ_PRJ_BITS        7
#define SUPERRES_NUM            8
#define RESTORE_NONE            0
#define RESTORE_WIENER          1
#define RESTORE_SGRPROJ         2
#define RESTORE_SWITCHABLE      3
#define TX_MODE_SELECT          2

enum {
    PARTITION_NONE, PARTITION_HORZ, PARTITION_VERT, PARTITION_SPLIT,
    PARTITION_HORZ_A, PARTITION_HORZ_B, PARTITION_VERT_A, PARTITION_VERT_B,
    PARTITION_HORZ_4, PARTITION_VERT_4
};

enum {
    SEG_LVL_ALT_Q = 0, SEG_LVL_ALT_LF_Y_V, SEG_LVL_ALT_LF_Y_H,
    SEG_LVL_REF_FRAME, SEG_LVL_SKIP, SEG_LVL_GLOBALMV
};

/* Intra/Inter set values overlap on purpose: the is_inter flag selects which
 * table (Tx_Type_In_Set_Intra vs Inter) the value indexes into. */
#define TX_SET_DCTONLY  0
#define TX_SET_INTRA_1  1
#define TX_SET_INTRA_2  2
#define TX_SET_INTER_1  1
#define TX_SET_INTER_2  2
#define TX_SET_INTER_3  3

enum {
    TX_CLASS_2D = 0, TX_CLASS_HORIZ = 1, TX_CLASS_VERT = 2
};

static int block_width(int bsize);
static int block_height(int bsize);
static int decode_partition(struct av1_dec *d, int r, int c, int bsize);
static int decode_block(struct av1_dec *d, int r, int c, int sub_size);
static void transform_block(struct av1_dec *d, int plane, int base_x,
                            int base_y, int tx_sz, int x, int y);
static int coeffs(struct av1_dec *d, int plane, int start_x, int start_y,
                  int tx_sz);
static void transform_type(struct av1_dec *d, int x4, int y4, int tx_sz);
int av1_compute_tx_type(struct av1_dec *d, int plane, int tx_size, int x4,
                        int y4);
static int get_tx_class(int tx_type);
static int palette_tokens(struct av1_dec *d);
static int palette_mode_info(struct av1_dec *d);
static int read_tx_size(struct av1_dec *d, int allow_select);
static int read_block_tx_size(struct av1_dec *d);
static int residual(struct av1_dec *d);
static void predict_chroma_from_luma(struct av1_dec *d, int plane,
                                     int start_x, int start_y, int tx_sz);

static int
block_width(int bsize)
{
    return 4 << Mi_Width_Log2[bsize];
}

static int
block_height(int bsize)
{
    return 4 << Mi_Height_Log2[bsize];
}

static int
ceil_log2(int v)
{
    int n = 0;
    while ((1 << n) < v) {
        n++;
    }
    return n;
}

static int
clip1(struct av1_dec *d, int v)
{
    int max = (1 << d->f.bitdepth) - 1;
    return v < 0 ? 0 : (v > max ? max : v);
}

static int
round2(int x, int n)
{
    if (n == 0) {
        return x;
    }
    return (x + (1 << (n - 1))) >> n;
}

static int
round2_signed(int x, int n)
{
    if (x > 0) {
        return (x + (1 << (n - 1))) >> n;
    }
    if (x < 0) {
        return -((-x + (1 << (n - 1))) >> n);
    }
    return 0;
}

static int
get_tx_class(int tx_type)
{
    if (tx_type == V_DCT || tx_type == V_ADST || tx_type == V_FLIPADST) {
        return TX_CLASS_VERT;
    }
    if (tx_type == H_DCT || tx_type == H_ADST || tx_type == H_FLIPADST) {
        return TX_CLASS_HORIZ;
    }
    return TX_CLASS_2D;
}

/* ------------------------------------------------------------------ */
/* Symbol helpers                                                      */
/* ------------------------------------------------------------------ */

static int
sym(struct av1_dec *d, uint16_t *cdf, int n)
{
    int v = av1_range_symbol(&d->rd, cdf, n, !d->f.disable_cdf_update);
    if (d->rd.bits.error && !d->error) {
        ptrdiff_t off = (uint16_t *)cdf - (uint16_t *)&d->cdf;
        VERR(av1, "range error n=%d off=%td cdf=%u,%u,%u,%u val=%u range=%u",
             n, off, cdf[0], cdf[1], cdf[2], cdf[3],
             d->rd.value, d->rd.range);
        d->error = 1;
    }
    return v;
}

static int
lit(struct av1_dec *d, int n)
{
    return (int)av1_range_literal(&d->rd, n);
}

static int
ns(struct av1_dec *d, int n)
{
    return av1_range_ns(&d->rd, n);
}

/* decode_subexp_bool: L()/NS() literals only, no CDF adaptation */
static int
subexp_bool(struct av1_dec *d, int num_syms, int k)
{
    int i = 0, mk = 0;
    while (1) {
        int b2 = i ? k + i - 1 : k;
        int a = 1 << b2;
        if (num_syms <= mk + 3 * a) {
            return ns(d, num_syms - mk) + mk;
        }
        if (lit(d, 1)) {
            i++;
            mk += a;
        } else {
            return lit(d, b2) + mk;
        }
    }
}

static int
inverse_recenter(int r, int v)
{
    if (v > 2 * r) {
        return v;
    }
    if (v & 1) {
        return r - ((v + 1) >> 1);
    }
    return r + (v >> 1);
}

static int
unsigned_subexp_ref_bool(struct av1_dec *d, int mx, int k, int r)
{
    int v = subexp_bool(d, mx, k);
    if ((r << 1) <= mx) {
        return inverse_recenter(r, v);
    }
    return mx - 1 - inverse_recenter(mx - 1 - r, v);
}

static int
signed_subexp_ref_bool(struct av1_dec *d, int low, int high, int k, int r)
{
    return unsigned_subexp_ref_bool(d, high - low, k, r - low) + low;
}

/* ------------------------------------------------------------------ */
/* Context helpers                                                     */
/* ------------------------------------------------------------------ */

static int
is_inside(struct av1_dec *d, int r, int c)
{
    return c >= d->mi_col_start && c < d->mi_col_end &&
           r >= d->mi_row_start && r < d->mi_row_end;
}

static int
bd_idx(struct av1_dec *d, int plane, int r4, int c4)
{
    int sub_x = plane ? d->f.subsampling_x : 0;
    int sub_y = plane ? d->f.subsampling_y : 0;
    int w = (d->f.mi_cols + sub_x) >> sub_x;
    int h = (d->f.mi_rows + sub_y) >> sub_y;
    if (r4 < -1 || c4 < -1 || r4 > h || c4 > w) {
        return -1;
    }
    return (r4 + 1) * d->mi_stride_c[plane] + (c4 + 1);
}

static int
bd_get(struct av1_dec *d, int plane, int r4, int c4)
{
    int idx = bd_idx(d, plane, r4, c4);
    return idx < 0 ? 1 : d->block_decoded[plane][idx];
}

static void
bd_set(struct av1_dec *d, int plane, int r4, int c4, int v)
{
    int idx = bd_idx(d, plane, r4, c4);
    if (idx >= 0) {
        d->block_decoded[plane][idx] = (int8_t)v;
    }
}

static void
clear_above_context(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    for (int plane = 0; plane < f->num_planes; plane++) {
        int sub_x = plane ? f->subsampling_x : 0;
        int w = (f->mi_cols + sub_x) >> sub_x;
        memset(d->above_level[plane], 0, (size_t)w + 2);
        memset(d->above_dc[plane], 0, (size_t)w + 2);
    }
}

static void
clear_left_context(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    for (int plane = 0; plane < f->num_planes; plane++) {
        int sub_y = plane ? f->subsampling_y : 0;
        int h = (f->mi_rows + sub_y) >> sub_y;
        memset(d->left_level[plane], 0, (size_t)h + 2);
        memset(d->left_dc[plane], 0, (size_t)h + 2);
    }
}

static void
reset_block_context(struct av1_dec *d, int bw4, int bh4)
{
    struct av1_frame *f = &d->f;
    for (int plane = 0; plane < 1 + 2 * d->has_chroma; plane++) {
        int sub_x = plane ? f->subsampling_x : 0;
        int sub_y = plane ? f->subsampling_y : 0;
        for (int i = d->mi_col >> sub_x; i < ((d->mi_col + bw4) >> sub_x); i++) {
            d->above_level[plane][i] = 0;
            d->above_dc[plane][i] = 0;
        }
        for (int i = d->mi_row >> sub_y; i < ((d->mi_row + bh4) >> sub_y); i++) {
            d->left_level[plane][i] = 0;
            d->left_dc[plane][i] = 0;
        }
    }
}

static void
clear_cdef(struct av1_dec *d, int r, int c)
{
    int cdef_size4 = Num_4x4_Blocks_Wide[BLOCK_64X64];
    int stride = d->mi_stride;
    struct av1_frame *f = &d->f;
    d->cdef_idx[r * stride + c] = -1;
    if (f->use_128x128_superblock) {
        if (r < f->mi_rows && c + cdef_size4 < f->mi_cols) {
            d->cdef_idx[r * stride + c + cdef_size4] = -1;
        }
        if (r + cdef_size4 < f->mi_rows) {
            d->cdef_idx[(r + cdef_size4) * stride + c] = -1;
            if (c + cdef_size4 < f->mi_cols) {
                d->cdef_idx[(r + cdef_size4) * stride + c + cdef_size4] = -1;
            }
        }
    }
}

static void
clear_block_decoded_flags(struct av1_dec *d, int r, int c, int sb_size4)
{
    struct av1_frame *f = &d->f;
    for (int plane = 0; plane < f->num_planes; plane++) {
        int sub_x = plane ? f->subsampling_x : 0;
        int sub_y = plane ? f->subsampling_y : 0;
        int sb_width4 = (d->mi_col_end - c) >> sub_x;
        int sb_height4 = (d->mi_row_end - r) >> sub_y;
        for (int y = -1; y <= (sb_size4 >> sub_y); y++) {
            for (int x = -1; x <= (sb_size4 >> sub_x); x++) {
                int v;
                if (y < 0 && x < sb_width4) {
                    v = 1;
                } else if (x < 0 && y < sb_height4) {
                    v = 1;
                } else {
                    v = 0;
                }
                bd_set(d, plane, r + y, c + x, v);
            }
        }
        bd_set(d, plane, r + (sb_size4 >> sub_y), c - 1, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Segment / qindex helpers                                            */
/* ------------------------------------------------------------------ */

int
av1_seg_feature_active(struct av1_dec *d, int feature)
{
    return d->f.seg.enabled &&
           d->f.seg.feature_enabled[d->segment_id][feature];
}

int
av1_get_qindex(struct av1_dec *d, int ignore_delta_q, int segment_id)
{
    struct av1_frame *f = &d->f;
    if (f->seg.enabled &&
        f->seg.feature_enabled[segment_id][SEG_LVL_ALT_Q]) {
        int data = f->seg.feature_data[segment_id][SEG_LVL_ALT_Q];
        int qindex = f->base_q_idx + data;
        if (!ignore_delta_q && f->delta_q_present) {
            qindex = d->current_q_index + data;
        }
        return clip3(0, 255, qindex);
    }
    if (!ignore_delta_q && f->delta_q_present) {
        return d->current_q_index;
    }
    return f->base_q_idx;
}

int
av1_plane_residual_size(struct av1_dec *d, int bsize, int plane)
{
    int sub_x = plane ? d->f.subsampling_x : 0;
    int sub_y = plane ? d->f.subsampling_y : 0;
    return Subsampled_Size[bsize][sub_x][sub_y];
}

int
av1_find_tx_size(int w, int h)
{
    for (int tx = 0; tx < TX_SIZES_ALL; tx++) {
        if (Tx_Width[tx] == w && Tx_Height[tx] == h) {
            return tx;
        }
    }
    return TX_4X4;
}

int
av1_get_tx_set(struct av1_dec *d, int tx_sz, int *intra)
{
    int tx_sqr = Tx_Size_Sqr[tx_sz];
    int tx_sqr_up = Tx_Size_Sqr_Up[tx_sz];
    *intra = !d->is_inter;
    if (tx_sqr_up > TX_32X32) {
        return TX_SET_DCTONLY;
    }
    if (d->is_inter) {
        if (d->f.reduced_tx_set || tx_sqr_up == TX_32X32) {
            return TX_SET_INTER_3;
        }
        return tx_sqr == TX_16X16 ? TX_SET_INTER_2 : TX_SET_INTER_1;
    }
    if (tx_sqr_up == TX_32X32) {
        return TX_SET_DCTONLY;
    }
    if (d->f.reduced_tx_set || tx_sqr == TX_16X16) {
        return TX_SET_INTRA_2;
    }
    return TX_SET_INTRA_1;
}

static int
get_tx_size(struct av1_dec *d, int plane, int tx_sz)
{
    if (plane == 0) {
        return tx_sz;
    }
    int uv_tx = Max_Tx_Size_Rect[av1_plane_residual_size(d, d->mi_size, plane)];
    if (Tx_Width[uv_tx] == 64 || Tx_Height[uv_tx] == 64) {
        if (Tx_Width[uv_tx] == 16) {
            return TX_16X32;
        }
        if (Tx_Height[uv_tx] == 16) {
            return TX_32X16;
        }
        return TX_32X32;
    }
    return uv_tx;
}

static int
neg_deinterleave(int diff, int ref, int max)
{
    if (!ref) {
        return diff;
    }
    if (ref >= max - 1) {
        return max - diff - 1;
    }
    if (2 * ref < max) {
        if (diff <= 2 * ref) {
            if (diff & 1) {
                return ref + ((diff + 1) >> 1);
            }
            return ref - (diff >> 1);
        }
        return diff;
    }
    if (diff <= 2 * (max - ref - 1)) {
        if (diff & 1) {
            return ref + ((diff + 1) >> 1);
        }
        return ref - (diff >> 1);
    }
    return max - (diff + 1);
}

/* ------------------------------------------------------------------ */
/* Segment id / cdef / deltas                                          */
/* ------------------------------------------------------------------ */

static int
intra_segment_id(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    if (f->seg.enabled) {
        int prev_ul = -1, prev_u = -1, prev_l = -1, pred, ctx;
        if (d->avail_u && d->avail_l) {
            prev_ul = d->segment_ids[(d->mi_row - 1) * d->mi_stride +
                                     d->mi_col - 1];
        }
        if (d->avail_u) {
            prev_u = d->segment_ids[(d->mi_row - 1) * d->mi_stride +
                                    d->mi_col];
        }
        if (d->avail_l) {
            prev_l = d->segment_ids[d->mi_row * d->mi_stride + d->mi_col - 1];
        }
        if (prev_u == -1) {
            pred = prev_l == -1 ? 0 : prev_l;
        } else if (prev_l == -1) {
            pred = prev_u;
        } else {
            pred = prev_ul == prev_u ? prev_u : prev_l;
        }
        if (d->skip) {
            d->segment_id = pred;
        } else {
            if (prev_ul < 0) {
                ctx = 0;
            } else if (prev_ul == prev_u && prev_ul == prev_l) {
                ctx = 2;
            } else if (prev_ul == prev_u || prev_ul == prev_l ||
                       prev_u == prev_l) {
                ctx = 1;
            } else {
                ctx = 0;
            }
            int s = sym(d, d->cdf.segment_id[ctx], AV1_MAX_SEGMENTS);
            if (d->error) {
                return -EINVAL;
            }
            d->segment_id = neg_deinterleave(s, pred,
                                             f->seg.last_active_seg_id + 1);
        }
    } else {
        d->segment_id = 0;
    }
    if (d->segment_id >= AV1_MAX_SEGMENTS || d->segment_id < 0) {
        VERR(av1, "segment_id out of range");
        return -EINVAL;
    }
    d->lossless = f->lossless[d->segment_id];
    return 0;
}

static int
read_cdef(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    if (d->skip || f->coded_lossless || !f->enable_cdef || f->allow_intrabc) {
        return 0;
    }
    int cdef_size4 = Num_4x4_Blocks_Wide[BLOCK_64X64];
    int cdef_mask4 = ~(cdef_size4 - 1);
    int r = d->mi_row & cdef_mask4;
    int c = d->mi_col & cdef_mask4;
    int stride = d->mi_stride;
    if (d->cdef_idx[r * stride + c] == -1) {
        int v = lit(d, f->cdef_bits);
        int w4 = Num_4x4_Blocks_Wide[d->mi_size];
        int h4 = Num_4x4_Blocks_High[d->mi_size];
        for (int i = r; i < r + h4; i += cdef_size4) {
            for (int j = c; j < c + w4; j += cdef_size4) {
                if (i < f->mi_rows && j < f->mi_cols) {
                    d->cdef_idx[i * stride + j] = (int8_t)v;
                }
            }
        }
    }
    return 0;
}

static int
read_delta_qindex(struct av1_dec *d)
{
    VDBG(av1, "dq");
    struct av1_frame *f = &d->f;
    int sb_size = f->use_128x128_superblock ? BLOCK_128X128 : BLOCK_64X64;
    if (d->mi_size == sb_size && d->skip) {
        return 0;
    }
    if (!d->read_deltas) {
        return 0;
    }
    int delta_q_abs = sym(d, d->cdf.delta_q, 4);
    if (delta_q_abs == DELTA_Q_SMALL) {
        int rem = lit(d, 3) + 1;
        int bits = lit(d, rem);
        delta_q_abs = bits + (1 << rem) + 1;
    }
    if (delta_q_abs) {
        int sign = lit(d, 1);
        int reduced = sign ? -delta_q_abs : delta_q_abs;
        d->current_q_index = clip3(1, 255,
                                   d->current_q_index +
                                   (reduced << f->delta_q_res));
    }
    return 0;
}

static int
read_delta_lf(struct av1_dec *d)
{
    VDBG(av1, "dlf");
    struct av1_frame *f = &d->f;
    int sb_size = f->use_128x128_superblock ? BLOCK_128X128 : BLOCK_64X64;
    if (d->mi_size == sb_size && d->skip) {
        return 0;
    }
    if (!d->read_deltas || !f->delta_lf_present) {
        return 0;
    }
    int frame_lf_count = 1;
    if (f->delta_lf_multi) {
        frame_lf_count = f->num_planes > 1 ? 4 : 2;
    }
    for (int i = 0; i < frame_lf_count; i++) {
        int abs = sym(d, d->cdf.delta_lf, 4);
        if (abs == DELTA_LF_SMALL) {
            int rem = lit(d, 3);
            int n = rem + 1;
            int bits = lit(d, n);
            abs = bits + (1 << n) + 1;
        }
        if (abs) {
            int sign = lit(d, 1);
            int reduced = sign ? -abs : abs;
            d->delta_lf[i] = clip3(-MAX_LOOP_FILTER, MAX_LOOP_FILTER,
                                   d->delta_lf[i] +
                                   (reduced << f->delta_lf_res));
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */

static int
get_palette_cache(struct av1_dec *d, int plane, uint16_t *cache)
{
    int above_n = 0, left_n = 0;
    if ((d->mi_row * 4) % 64) {
        above_n = d->palette_sizes[plane][
            (d->mi_row - 1) * d->mi_stride + d->mi_col];
    }
    if (d->avail_l) {
        left_n = d->palette_sizes[plane][
            d->mi_row * d->mi_stride + d->mi_col - 1];
    }
    const uint16_t *above = &d->palette_colors[plane][
        ((d->mi_row - 1) * d->mi_stride + d->mi_col) * 8];
    const uint16_t *left = &d->palette_colors[plane][
        (d->mi_row * d->mi_stride + d->mi_col - 1) * 8];
    int above_idx = 0, left_idx = 0, n = 0;
    while (above_idx < above_n && left_idx < left_n) {
        uint16_t above_c = above[above_idx];
        uint16_t left_c = left[left_idx];
        if (left_c < above_c) {
            if (n == 0 || left_c != cache[n - 1]) {
                cache[n++] = left_c;
            }
            left_idx++;
        } else {
            if (n == 0 || above_c != cache[n - 1]) {
                cache[n++] = above_c;
            }
            above_idx++;
            if (left_c == above_c) {
                left_idx++;
            }
        }
    }
    while (above_idx < above_n) {
        uint16_t v = above[above_idx++];
        if (n == 0 || v != cache[n - 1]) {
            cache[n++] = v;
        }
    }
    while (left_idx < left_n) {
        uint16_t v = left[left_idx++];
        if (n == 0 || v != cache[n - 1]) {
            cache[n++] = v;
        }
    }
    return n;
}

static void
sort_u16(uint16_t *a, int lo, int hi)
{
    for (int i = lo + 1; i <= hi; i++) {
        uint16_t key = a[i];
        int j = i - 1;
        while (j >= lo && a[j] > key) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

/* decode palette_color_idx_y/uv; returns the color index via ColorOrder */
static int
palette_color_idx(struct av1_dec *d, int plane, uint8_t *map, int map_stride,
                  int r, int c, int n)
{
    int scores[PALETTE_COLORS];
    int order[PALETTE_COLORS];
    for (int i = 0; i < PALETTE_COLORS; i++) {
        scores[i] = 0;
        order[i] = i;
    }
    if (c > 0) {
        scores[map[r * map_stride + c - 1]] += 2;
    }
    if (r > 0 && c > 0) {
        scores[map[(r - 1) * map_stride + c - 1]] += 1;
    }
    if (r > 0) {
        scores[map[(r - 1) * map_stride + c]] += 2;
    }
    for (int i = 0; i < PALETTE_NUM_NEIGHBORS; i++) {
        int max_score = scores[i];
        int max_idx = i;
        for (int j = i + 1; j < n; j++) {
            if (scores[j] > max_score) {
                max_score = scores[j];
                max_idx = j;
            }
        }
        if (max_idx != i) {
            int max_order = order[max_idx];
            for (int k = max_idx; k > i; k--) {
                scores[k] = scores[k - 1];
                order[k] = order[k - 1];
            }
            scores[i] = max_score;
            order[i] = max_order;
        }
    }
    int hash = 0;
    for (int i = 0; i < PALETTE_NUM_NEIGHBORS; i++) {
        hash += scores[i] * Palette_Color_Hash_Multipliers[i];
    }
    int ctx = Palette_Color_Context[hash];
    uint16_t *cdf;
    switch (n) {
    case 2:
        cdf = plane ? d->cdf.palette_size_2_uv[ctx] :
              d->cdf.palette_size_2_y[ctx];
        break;
    case 3:
        cdf = plane ? d->cdf.palette_size_3_uv[ctx] :
              d->cdf.palette_size_3_y[ctx];
        break;
    case 4:
        cdf = plane ? d->cdf.palette_size_4_uv[ctx] :
              d->cdf.palette_size_4_y[ctx];
        break;
    case 5:
        cdf = plane ? d->cdf.palette_size_5_uv[ctx] :
              d->cdf.palette_size_5_y[ctx];
        break;
    case 6:
        cdf = plane ? d->cdf.palette_size_6_uv[ctx] :
              d->cdf.palette_size_6_y[ctx];
        break;
    case 7:
        cdf = plane ? d->cdf.palette_size_7_uv[ctx] :
              d->cdf.palette_size_7_y[ctx];
        break;
    default:
        cdf = plane ? d->cdf.palette_size_8_uv[ctx] :
              d->cdf.palette_size_8_y[ctx];
        break;
    }
    int idx = sym(d, cdf, n);
    return order[idx];
}

static int
palette_tokens(struct av1_dec *d)
{
    VDBG(av1, "pal_tok psy=%d psu=%d", d->palette_size_y, d->palette_size_uv);
    struct av1_frame *f = &d->f;
    int bw = block_width(d->mi_size);
    int bh = block_height(d->mi_size);
    int on_w = bw < (d->mi_col_end - d->mi_col) * 4 ?
               bw : (d->mi_col_end - d->mi_col) * 4;
    int on_h = bh < (d->mi_row_end - d->mi_row) * 4 ?
               bh : (d->mi_row_end - d->mi_row) * 4;

    if (d->palette_size_y) {
        d->color_map_y[0][0] = (uint8_t)ns(d, d->palette_size_y);
        for (int i = 1; i < on_h + on_w - 1; i++) {
            int j_start = i < on_w - 1 ? i : on_w - 1;
            int j_end = i - on_h + 1 > 0 ? i - on_h + 1 : 0;
            for (int j = j_start; j >= j_end; j--) {
                int idx = palette_color_idx(d, 0, &d->color_map_y[0][0], 64,
                                            i - j, j, d->palette_size_y);
                d->color_map_y[i - j][j] = (uint8_t)idx;
            }
        }
        for (int i = 0; i < on_h; i++) {
            for (int j = on_w; j < bw; j++) {
                d->color_map_y[i][j] = d->color_map_y[i][on_w - 1];
            }
        }
        for (int i = on_h; i < bh; i++) {
            for (int j = 0; j < bw; j++) {
                d->color_map_y[i][j] = d->color_map_y[on_h - 1][j];
            }
        }
    }
    if (d->palette_size_uv) {
        d->color_map_uv[0][0] = (uint8_t)ns(d, d->palette_size_uv);
        int cbw = bw >> f->subsampling_x;
        int cbh = bh >> f->subsampling_y;
        int con_w = on_w >> f->subsampling_x;
        int con_h = on_h >> f->subsampling_y;
        if (cbw < 4) {
            cbw += 2;
            con_w += 2;
        }
        if (cbh < 4) {
            cbh += 2;
            con_h += 2;
        }
        for (int i = 1; i < con_h + con_w - 1; i++) {
            int j_start = i < con_w - 1 ? i : con_w - 1;
            int j_end = i - con_h + 1 > 0 ? i - con_h + 1 : 0;
            for (int j = j_start; j >= j_end; j--) {
                int idx = palette_color_idx(d, 1, &d->color_map_uv[0][0], 64,
                                            i - j, j, d->palette_size_uv);
                d->color_map_uv[i - j][j] = (uint8_t)idx;
            }
        }
        for (int i = 0; i < con_h; i++) {
            for (int j = con_w; j < cbw; j++) {
                d->color_map_uv[i][j] = d->color_map_uv[i][con_w - 1];
            }
        }
        for (int i = con_h; i < cbh; i++) {
            for (int j = 0; j < cbw; j++) {
                d->color_map_uv[i][j] = d->color_map_uv[con_h - 1][j];
            }
        }
    }
    return 0;
}

static int
palette_mode_info(struct av1_dec *d)
{
    VDBG(av1, "pal_mi");
    struct av1_frame *f = &d->f;
    int bsize_ctx = Mi_Width_Log2[d->mi_size] + Mi_Height_Log2[d->mi_size] - 2;
    uint16_t cache[8];
    int bitdepth = f->bitdepth;

    if (d->y_mode == DC_PRED) {
        int ctx = 0;
        if (d->avail_u && d->palette_sizes[0][
                (d->mi_row - 1) * d->mi_stride + d->mi_col] > 0) {
            ctx += 1;
        }
        if (d->avail_l && d->palette_sizes[0][
                d->mi_row * d->mi_stride + d->mi_col - 1] > 0) {
            ctx += 1;
        }
        int has = sym(d, d->cdf.palette_y_mode[bsize_ctx][ctx], 2);
        if (has) {
            d->palette_size_y =
                sym(d, d->cdf.palette_y_size[bsize_ctx], 7) + 2;
            int cache_n = get_palette_cache(d, 0, cache);
            int idx = 0;
            for (int i = 0; i < cache_n && idx < d->palette_size_y; i++) {
                if (lit(d, 1)) {
                    d->palette_colors_y[idx++] = cache[i];
                }
            }
            if (idx < d->palette_size_y) {
                d->palette_colors_y[idx++] = (uint16_t)lit(d, bitdepth);
            }
            int bits = 0;
            if (idx < d->palette_size_y) {
                bits = bitdepth - 3 + lit(d, 2);
            }
            while (idx < d->palette_size_y) {
                int delta = lit(d, bits) + 1;
                int v = clip1(d, d->palette_colors_y[idx - 1] + delta);
                d->palette_colors_y[idx] = (uint16_t)v;
                int range = (1 << bitdepth) - v - 1;
                int b = ceil_log2(range + 1);
                bits = bits < b ? bits : b;
                idx++;
            }
            sort_u16(d->palette_colors_y, 0, d->palette_size_y - 1);
        }
    }
    if (d->has_chroma && d->uv_mode == DC_PRED) {
        int ctx = d->palette_size_y > 0 ? 1 : 0;
        int has = sym(d, d->cdf.palette_uv_mode[ctx], 2);
        if (has) {
            d->palette_size_uv =
                sym(d, d->cdf.palette_uv_size[bsize_ctx], 7) + 2;
            int cache_n = get_palette_cache(d, 1, cache);
            int idx = 0;
            for (int i = 0; i < cache_n && idx < d->palette_size_uv; i++) {
                if (lit(d, 1)) {
                    d->palette_colors_u[idx++] = cache[i];
                }
            }
            if (idx < d->palette_size_uv) {
                d->palette_colors_u[idx++] = (uint16_t)lit(d, bitdepth);
            }
            int bits = 0;
            if (idx < d->palette_size_uv) {
                bits = bitdepth - 3 + lit(d, 2);
            }
            while (idx < d->palette_size_uv) {
                int delta = lit(d, bits) + 1;
                int v = clip1(d, d->palette_colors_u[idx - 1] + delta);
                d->palette_colors_u[idx] = (uint16_t)v;
                int range = (1 << bitdepth) - v;
                int b = ceil_log2(range + 1);
                bits = bits < b ? bits : b;
                idx++;
            }
            sort_u16(d->palette_colors_u, 0, d->palette_size_uv - 1);

            if (lit(d, 1)) {
                bits = bitdepth - 4 + lit(d, 2);
                int max_val = 1 << bitdepth;
                d->palette_colors_v[0] = (uint16_t)lit(d, bitdepth);
                for (int i = 1; i < d->palette_size_uv; i++) {
                    int delta = lit(d, bits);
                    if (delta && lit(d, 1)) {
                        delta = -delta;
                    }
                    int val = d->palette_colors_v[i - 1] + delta;
                    if (val < 0) {
                        val += max_val;
                    }
                    if (val >= max_val) {
                        val -= max_val;
                    }
                    d->palette_colors_v[i] = (uint16_t)clip1(d, val);
                }
            } else {
                for (int i = 0; i < d->palette_size_uv; i++) {
                    d->palette_colors_v[i] = (uint16_t)lit(d, bitdepth);
                }
            }
        }
    }
    return 0;
}

static int
filter_intra_mode_info(struct av1_dec *d)
{
    VDBG(av1, "filt");
    struct av1_frame *f = &d->f;
    d->use_filter_intra = 0;
    d->filter_intra_mode = 0;
    if (f->enable_filter_intra && d->y_mode == DC_PRED &&
        d->palette_size_y == 0 &&
        block_width(d->mi_size) <= 32 && block_height(d->mi_size) <= 32) {
        if (sym(d, d->cdf.filter_intra[d->mi_size], 2)) {
            d->use_filter_intra = 1;
            d->filter_intra_mode = sym(d, d->cdf.filter_intra_mode, 5);
        }
    }
    return 0;
}

static int
read_cfl_alphas(struct av1_dec *d)
{
    int signs = sym(d, d->cdf.cfl_sign, 8);
    VDBG(av1, "cfl signs %d", signs);
    int sign_u = (signs + 1) / 3;
    int sign_v = (signs + 1) % 3;
    d->cfl_alpha_signs = signs;
    if (sign_u != 0) {
        int ctx = (sign_u - 1) * 3 + sign_v;
        int v = sym(d, d->cdf.cfl_alpha[ctx], 16);
        d->cfl_alpha_u = 1 + v;
        if (sign_u == 1) {
            d->cfl_alpha_u = -d->cfl_alpha_u;
        }
    } else {
        d->cfl_alpha_u = 0;
    }
    if (sign_v != 0) {
        int ctx = (sign_v - 1) * 3 + sign_u;
        int v = sym(d, d->cdf.cfl_alpha[ctx], 16);
        d->cfl_alpha_v = 1 + v;
        if (sign_v == 1) {
            d->cfl_alpha_v = -d->cfl_alpha_v;
        }
    } else {
        d->cfl_alpha_v = 0;
    }
    return 0;
}

static int
intra_frame_mode_info(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int ret;

    d->skip = 0;
    if (f->seg_id_pre_skip) {
        ret = intra_segment_id(d);
        if (ret) {
            return ret;
        }
    }
    d->skip_mode = 0;
    {
        int seg_force_skip = f->seg.enabled &&
            (f->seg.feature_enabled[d->segment_id][SEG_LVL_SKIP] ||
             f->seg.feature_enabled[d->segment_id][SEG_LVL_REF_FRAME] ||
             f->seg.feature_enabled[d->segment_id][SEG_LVL_GLOBALMV]);
        if (seg_force_skip) {
            d->skip = 1;
        } else {
            int ctx = 0;
            if (d->avail_u) {
                ctx += d->skips[(d->mi_row - 1) * d->mi_stride + d->mi_col];
            }
            if (d->avail_l) {
                ctx += d->skips[d->mi_row * d->mi_stride + d->mi_col - 1];
            }
            d->skip = sym(d, d->cdf.skip[ctx], 2);
        }
    }
    if (!f->seg_id_pre_skip) {
        ret = intra_segment_id(d);
        if (ret) {
            return ret;
        }
    }
    ret = read_cdef(d);
    if (ret) {
        return ret;
    }
    ret = read_delta_qindex(d);
    if (ret) {
        return ret;
    }
    ret = read_delta_lf(d);
    if (ret) {
        return ret;
    }
    d->read_deltas = 0;

    d->use_intrabc = 0;
    d->is_inter = 0;
    if (f->allow_intrabc) {
        if (sym(d, d->cdf.intrabc, 2)) {
            VERR(av1, "intrabc blocks are not supported");
            return -ENOTSUP;
        }
    }

    int above_mode = DC_PRED, left_mode = DC_PRED;
    if (d->avail_u) {
        above_mode = d->y_modes[(d->mi_row - 1) * d->mi_stride + d->mi_col];
    }
    if (d->avail_l) {
        left_mode = d->y_modes[d->mi_row * d->mi_stride + d->mi_col - 1];
    }
    d->y_mode = sym(d, d->cdf.intra_frame_y_mode
                    [Intra_Mode_Context[above_mode]]
                    [Intra_Mode_Context[left_mode]], 13);

    d->angle_delta_y = 0;
    if (d->mi_size >= BLOCK_8X8 &&
        d->y_mode >= V_PRED && d->y_mode <= D67_PRED) {
        d->angle_delta_y =
            sym(d, d->cdf.angle_delta[d->y_mode - V_PRED], 7) - MAX_ANGLE_DELTA;
    }

    if (d->has_chroma) {
        int cfl_allowed;
        if (d->lossless &&
            av1_plane_residual_size(d, d->mi_size, 1) == BLOCK_4X4) {
            cfl_allowed = 1;
        } else if (!d->lossless && block_width(d->mi_size) <= 32 &&
                   block_height(d->mi_size) <= 32) {
            cfl_allowed = 1;
        } else {
            cfl_allowed = 0;
        }
        d->uv_mode = sym(d, cfl_allowed ?
                         d->cdf.uv_mode_cfl_allowed[d->y_mode] :
                         d->cdf.uv_mode_cfl_not_allowed[d->y_mode],
                         cfl_allowed ? 14 : 13);
        if (d->uv_mode == UV_CFL_PRED) {
            ret = read_cfl_alphas(d);
            if (ret) {
                return ret;
            }
        }
        d->angle_delta_uv = 0;
        if (d->mi_size >= BLOCK_8X8 &&
            d->uv_mode >= V_PRED && d->uv_mode <= D67_PRED) {
            d->angle_delta_uv =
                sym(d, d->cdf.angle_delta[d->uv_mode - V_PRED], 7) -
                MAX_ANGLE_DELTA;
        }
    } else {
        d->uv_mode = DC_PRED;
    }

    d->palette_size_y = 0;
    d->palette_size_uv = 0;
    if (d->mi_size >= BLOCK_8X8 && block_width(d->mi_size) <= 64 &&
        block_height(d->mi_size) <= 64 && f->allow_screen_content_tools) {
        ret = palette_mode_info(d);
        if (ret) {
            return ret;
        }
    }
    return filter_intra_mode_info(d);
}

/* ------------------------------------------------------------------ */
/* Tx size                                                             */
/* ------------------------------------------------------------------ */

static int
get_above_tx_width(struct av1_dec *d, int row, int col)
{
    if (row == d->mi_row) {
        if (!d->avail_u) {
            return 64;
        }
        int above = (row - 1) * d->mi_stride + col;
        if (d->skips[above] && d->is_inters[above]) {
            return block_width(d->mi_sizes[above]);
        }
    }
    return Tx_Width[d->inter_tx_sizes[(row - 1) * d->mi_stride + col]];
}

static int
get_left_tx_height(struct av1_dec *d, int row, int col)
{
    if (col == d->mi_col) {
        if (!d->avail_l) {
            return 64;
        }
        int left = row * d->mi_stride + col - 1;
        if (d->skips[left] && d->is_inters[left]) {
            return block_height(d->mi_sizes[left]);
        }
    }
    return Tx_Height[d->inter_tx_sizes[row * d->mi_stride + col - 1]];
}

static int
read_tx_size(struct av1_dec *d, int allow_select)
{
    struct av1_frame *f = &d->f;
    if (d->lossless) {
        d->tx_size = TX_4X4;
        return 0;
    }
    int max_rect = Max_Tx_Size_Rect[d->mi_size];
    int max_depth = Max_Tx_Depth[d->mi_size];
    d->tx_size = max_rect;
    if (d->mi_size > BLOCK_4X4 && allow_select && f->tx_mode == TX_MODE_SELECT) {
        int max_tx_w = Tx_Width[max_rect];
        int max_tx_h = Tx_Height[max_rect];
        int above_w, left_h;
        if (d->avail_u && d->is_inters[(d->mi_row - 1) * d->mi_stride +
                                       d->mi_col]) {
            above_w = block_width(d->mi_sizes[(d->mi_row - 1) * d->mi_stride +
                                              d->mi_col]);
        } else if (d->avail_u) {
            above_w = get_above_tx_width(d, d->mi_row, d->mi_col);
        } else {
            above_w = 0;
        }
        if (d->avail_l && d->is_inters[d->mi_row * d->mi_stride +
                                       d->mi_col - 1]) {
            left_h = block_height(d->mi_sizes[d->mi_row * d->mi_stride +
                                              d->mi_col - 1]);
        } else if (d->avail_l) {
            left_h = get_left_tx_height(d, d->mi_row, d->mi_col);
        } else {
            left_h = 0;
        }
        int ctx = (above_w >= max_tx_w) + (left_h >= max_tx_h);
        VDBG(av1, "txd-pre r%d c%d sz%d above_w %d left_h %d max %dx%d ctx %d",
             d->mi_row, d->mi_col, d->mi_size, above_w, left_h,
             max_tx_w, max_tx_h, ctx);
        uint16_t *cdf;
        int n;
        if (max_depth == 4) {
            cdf = d->cdf.tx_64x64[ctx];
            n = 3;
        } else if (max_depth == 3) {
            cdf = d->cdf.tx_32x32[ctx];
            n = 3;
        } else if (max_depth == 2) {
            cdf = d->cdf.tx_16x16[ctx];
            n = 3;
        } else {
            cdf = d->cdf.tx_8x8[ctx];
            n = 2;
        }
        int depth = sym(d, cdf, n);
        for (int i = 0; i < depth; i++) {
            d->tx_size = Split_Tx_Size[d->tx_size];
        }
    }
    return 0;
}

static int
read_block_tx_size(struct av1_dec *d)
{
    int bw4 = Num_4x4_Blocks_Wide[d->mi_size];
    int bh4 = Num_4x4_Blocks_High[d->mi_size];
    /* the var-tx branch is only reachable for inter (intrabc) blocks, which
     * are rejected; always take the plain path */
    int ret = read_tx_size(d, !d->skip || !d->is_inter);
    if (ret) {
        return ret;
    }
    for (int row = d->mi_row; row < d->mi_row + bh4; row++) {
        for (int col = d->mi_col; col < d->mi_col + bw4; col++) {
            d->inter_tx_sizes[row * d->mi_stride + col] =
                (uint8_t)d->tx_size;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Transform type                                                      */
/* ------------------------------------------------------------------ */

int
av1_compute_tx_type(struct av1_dec *d, int plane, int tx_sz, int block_x4,
                    int block_y4)
{
    int tx_sqr_up = Tx_Size_Sqr_Up[tx_sz];

    if (d->lossless || tx_sqr_up > TX_32X32) {
        return DCT_DCT;
    }
    int intra;
    int tx_set = av1_get_tx_set(d, tx_sz, &intra);
    if (plane == 0) {
        return d->tx_types[block_y4 * d->mi_stride + block_x4];
    }
    int tx_type = Mode_To_Txfm[d->uv_mode];
    if (!Tx_Type_In_Set_Intra[tx_set][tx_type]) {
        return DCT_DCT;
    }
    return tx_type;
}

static void
transform_type(struct av1_dec *d, int x4, int y4, int tx_sz)
{
    VDBG(av1, "ttype x%d y%d", x4, y4);
    int intra;
    int set = av1_get_tx_set(d, tx_sz, &intra);
    int tx_type = DCT_DCT;

    int qidx = d->f.seg.enabled ? av1_get_qindex(d, 1, d->segment_id) :
               d->f.base_q_idx;
    if (set > 0 && qidx > 0) {
        int intra_dir = d->use_filter_intra ?
            Filter_Intra_Mode_To_Intra_Dir[d->filter_intra_mode] : d->y_mode;
        if (set == TX_SET_INTRA_1) {
            int v = sym(d, d->cdf.intra_tx_type_set1
                        [Tx_Size_Sqr[tx_sz]][intra_dir], 7);
            tx_type = Tx_Type_Intra_Inv_Set1[v];
        } else {
            int v = sym(d, d->cdf.intra_tx_type_set2
                        [Tx_Size_Sqr[tx_sz]][intra_dir], 5);
            tx_type = Tx_Type_Intra_Inv_Set2[v];
        }
    }
    int w4 = Tx_Width[tx_sz] >> 2;
    int h4 = Tx_Height[tx_sz] >> 2;
    for (int i = 0; i < w4; i++) {
        for (int j = 0; j < h4; j++) {
            d->tx_types[(y4 + j) * d->mi_stride + x4 + i] =
                (uint8_t)tx_type;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Coefficients                                                        */
/* ------------------------------------------------------------------ */

static int
get_coeff_base_ctx(struct av1_dec *d, int tx_sz, int plane, int block_x4,
                   int block_y4, int pos, int c, int is_eob)
{
    int adj = Adjusted_Tx_Size[tx_sz];
    int bwl = Tx_Width_Log2[adj];
    int width = 1 << bwl;
    int height = Tx_Height[adj];
    int tx_type = av1_compute_tx_type(d, plane, tx_sz, block_x4, block_y4);

    if (is_eob) {
        if (c == 0) {
            return SIG_COEF_CONTEXTS - 4;
        }
        if (c <= (height << bwl) / 8) {
            return SIG_COEF_CONTEXTS - 3;
        }
        if (c <= (height << bwl) / 4) {
            return SIG_COEF_CONTEXTS - 2;
        }
        return SIG_COEF_CONTEXTS - 1;
    }
    int tx_cls = get_tx_class(tx_type);
    int row = pos >> bwl;
    int col = pos - (row << bwl);
    int mag = 0;
    for (int idx = 0; idx < SIG_REF_DIFF_OFFSET_NUM; idx++) {
        int ref_row = row + Sig_Ref_Diff_Offset[tx_cls][idx][0];
        int ref_col = col + Sig_Ref_Diff_Offset[tx_cls][idx][1];
        if (ref_row >= 0 && ref_col >= 0 &&
            ref_row < height && ref_col < width) {
            int aq = d->quant[(ref_row << bwl) + ref_col];
            if (aq < 0) {
                aq = -aq;
            }
            mag += aq < 3 ? aq : 3;
        }
    }
    int ctx = ((mag + 1) >> 1) < 4 ? ((mag + 1) >> 1) : 4;
    if (tx_cls == TX_CLASS_2D) {
        if (row == 0 && col == 0) {
            return 0;
        }
        int r = row < 4 ? row : 4;
        int ccl = col < 4 ? col : 4;
        return ctx + Coeff_Base_Ctx_Offset[tx_sz][r][ccl];
    }
    int idx = tx_cls == TX_CLASS_VERT ? row : col;
    idx = idx < 2 ? idx : 2;
    return ctx + Coeff_Base_Pos_Ctx_Offset[idx];
}

static const uint16_t *
get_scan(struct av1_dec *d, int tx_sz)
{
    if (tx_sz == TX_16X64) {
        return Default_Scan_16x32;
    }
    if (tx_sz == TX_64X16) {
        return Default_Scan_32x16;
    }
    if (Tx_Size_Sqr_Up[tx_sz] == TX_64X64) {
        return Default_Scan_32x32;
    }
    int t = d->plane_tx_type;
    if (t == IDTX) {
        goto default_scan;
    }
    int prefer_row = (t == V_DCT || t == V_ADST || t == V_FLIPADST);
    int prefer_col = (t == H_DCT || t == H_ADST || t == H_FLIPADST);
    if (prefer_row) {
        switch (tx_sz) {
        case TX_4X4:    return Mrow_Scan_4x4;
        case TX_4X8:    return Mrow_Scan_4x8;
        case TX_8X4:    return Mrow_Scan_8x4;
        case TX_8X8:    return Mrow_Scan_8x8;
        case TX_8X16:   return Mrow_Scan_8x16;
        case TX_16X8:   return Mrow_Scan_16x8;
        case TX_16X16:  return Mrow_Scan_16x16;
        case TX_4X16:   return Mrow_Scan_4x16;
        default:        return Mrow_Scan_16x4;
        }
    }
    if (prefer_col) {
        switch (tx_sz) {
        case TX_4X4:    return Mcol_Scan_4x4;
        case TX_4X8:    return Mcol_Scan_4x8;
        case TX_8X4:    return Mcol_Scan_8x4;
        case TX_8X8:    return Mcol_Scan_8x8;
        case TX_8X16:   return Mcol_Scan_8x16;
        case TX_16X8:   return Mcol_Scan_16x8;
        case TX_16X16:  return Mcol_Scan_16x16;
        case TX_4X16:   return Mcol_Scan_4x16;
        default:        return Mcol_Scan_16x4;
        }
    }
default_scan:
    switch (tx_sz) {
    case TX_4X4:    return Default_Scan_4x4;
    case TX_4X8:    return Default_Scan_4x8;
    case TX_8X4:    return Default_Scan_8x4;
    case TX_8X8:    return Default_Scan_8x8;
    case TX_8X16:   return Default_Scan_8x16;
    case TX_16X8:   return Default_Scan_16x8;
    case TX_16X16:  return Default_Scan_16x16;
    case TX_16X32:  return Default_Scan_16x32;
    case TX_32X16:  return Default_Scan_32x16;
    case TX_4X16:   return Default_Scan_4x16;
    case TX_16X4:   return Default_Scan_16x4;
    case TX_8X32:   return Default_Scan_8x32;
    case TX_32X8:   return Default_Scan_32x8;
    default:        return Default_Scan_32x32;
    }
}

static int
coeffs(struct av1_dec *d, int plane, int start_x, int start_y, int tx_sz)
{
    VDBG(av1, "coeffs p%d %d,%d", plane, start_x, start_y);
    struct av1_frame *f = &d->f;
    int x4 = start_x >> 2;
    int y4 = start_y >> 2;
    int w4 = Tx_Width[tx_sz] >> 2;
    int h4 = Tx_Height[tx_sz] >> 2;
    int tx_sz_ctx = (Tx_Size_Sqr[tx_sz] + Tx_Size_Sqr_Up[tx_sz] + 1) >> 1;
    int ptype = plane > 0;
    int eob = 0, cul_level = 0, dc_category = 0;
    const uint16_t *scan;
    int sub_x = plane ? f->subsampling_x : 0;
    int sub_y = plane ? f->subsampling_y : 0;
    int max_x4 = f->mi_cols >> sub_x;
    int max_y4 = f->mi_rows >> sub_y;

    memset(d->quant, 0, sizeof(d->quant));

    int bsize = av1_plane_residual_size(d, d->mi_size, plane);
    int bw = block_width(bsize);
    int bh = block_height(bsize);
    int all_zero_ctx;
    if (plane == 0) {
        int top = 0, left = 0;
        for (int k = 0; k < w4; k++) {
            if (x4 + k < max_x4 && d->above_level[plane][x4 + k] > top) {
                top = d->above_level[plane][x4 + k];
            }
        }
        for (int k = 0; k < h4; k++) {
            if (y4 + k < max_y4 && d->left_level[plane][y4 + k] > left) {
                left = d->left_level[plane][y4 + k];
            }
        }
        top = top < 255 ? top : 255;
        left = left < 255 ? left : 255;
        if (bw == Tx_Width[tx_sz] && bh == Tx_Height[tx_sz]) {
            all_zero_ctx = 0;
        } else if (top == 0 && left == 0) {
            all_zero_ctx = 1;
        } else if (top == 0 || left == 0) {
            int m = top > left ? top : left;
            all_zero_ctx = 2 + (m > 3);
        } else {
            int mx = top > left ? top : left;
            int mn = top < left ? top : left;
            if (mx <= 3) {
                all_zero_ctx = 4;
            } else if (mn <= 3) {
                all_zero_ctx = 5;
            } else {
                all_zero_ctx = 6;
            }
        }
    } else {
        int above = 0, left = 0;
        for (int i = 0; i < w4; i++) {
            if (x4 + i < max_x4) {
                above |= d->above_level[plane][x4 + i];
                above |= d->above_dc[plane][x4 + i];
            }
        }
        for (int i = 0; i < h4; i++) {
            if (y4 + i < max_y4) {
                left |= d->left_level[plane][y4 + i];
                left |= d->left_dc[plane][y4 + i];
            }
        }
        all_zero_ctx = (above != 0) + (left != 0) + 7;
        if (bw * bh > Tx_Width[tx_sz] * Tx_Height[tx_sz]) {
            all_zero_ctx += 3;
        }
    }

    int all_zero = sym(d, d->cdf.txb_skip[tx_sz_ctx][all_zero_ctx], 2);
    if (all_zero) {
        if (plane == 0) {
            for (int i = 0; i < w4; i++) {
                for (int j = 0; j < h4; j++) {
                    d->tx_types[(y4 + j) * d->mi_stride + x4 + i] = DCT_DCT;
                }
            }
        }
    } else {
        if (plane == 0) {
            transform_type(d, x4, y4, tx_sz);
        }
        d->plane_tx_type = av1_compute_tx_type(d, plane, tx_sz, x4, y4);
        scan = get_scan(d, tx_sz);

        int log2w = Tx_Width_Log2[tx_sz];
        int log2h = Tx_Height_Log2[tx_sz];
        int eob_multisize = (log2w < 5 ? log2w : 5) +
                            (log2h < 5 ? log2h : 5) - 4;
        uint16_t *eob_cdf;
        int eob_syms;
        switch (eob_multisize) {
        case 0:
            eob_cdf = d->cdf.eob_pt_16[ptype][0];
            eob_syms = 5;
            break;
        case 1:
            eob_cdf = d->cdf.eob_pt_32[ptype][0];
            eob_syms = 6;
            break;
        case 2:
            eob_cdf = d->cdf.eob_pt_64[ptype][0];
            eob_syms = 7;
            break;
        case 3:
            eob_cdf = d->cdf.eob_pt_128[ptype][0];
            eob_syms = 8;
            break;
        case 4:
            eob_cdf = d->cdf.eob_pt_256[ptype][0];
            eob_syms = 9;
            break;
        case 5:
            eob_cdf = d->cdf.eob_pt_512[ptype];
            eob_syms = 10;
            break;
        default:
            eob_cdf = d->cdf.eob_pt_1024[ptype];
            eob_syms = 11;
            break;
        }
        if (eob_multisize <= 4) {
            eob_cdf = eob_multisize == 0 ? d->cdf.eob_pt_16[ptype]
                          [get_tx_class(d->plane_tx_type) == TX_CLASS_2D ? 0 : 1] :
                      eob_multisize == 1 ? d->cdf.eob_pt_32[ptype]
                          [get_tx_class(d->plane_tx_type) == TX_CLASS_2D ? 0 : 1] :
                      eob_multisize == 2 ? d->cdf.eob_pt_64[ptype]
                          [get_tx_class(d->plane_tx_type) == TX_CLASS_2D ? 0 : 1] :
                      eob_multisize == 3 ? d->cdf.eob_pt_128[ptype]
                          [get_tx_class(d->plane_tx_type) == TX_CLASS_2D ? 0 : 1] :
                      d->cdf.eob_pt_256[ptype]
                          [get_tx_class(d->plane_tx_type) == TX_CLASS_2D ? 0 : 1];
        }
        int eob_pt = sym(d, eob_cdf, eob_syms) + 1;
        eob = eob_pt < 2 ? eob_pt : ((1 << (eob_pt - 2)) + 1);
        int eob_shift = eob_pt - 3 < -1 ? -1 : eob_pt - 3;
        if (eob_shift >= 0) {
            int extra = sym(d, d->cdf.eob_extra[tx_sz_ctx][ptype]
                            [eob_pt - 3], 2);
            if (extra) {
                eob += 1 << eob_shift;
            }
            for (int i = 1; i < (eob_pt - 2 > 0 ? eob_pt - 2 : 0); i++) {
                int shift = (eob_pt - 2 > 0 ? eob_pt - 2 : 0) - 1 - i;
                if (lit(d, 1)) {
                    eob += 1 << shift;
                }
            }
        }

        for (int c = eob - 1; c >= 0; c--) {
            int pos = scan[c];
            int level;
            if (c == eob - 1) {
                int ctx = get_coeff_base_ctx(d, tx_sz, plane, x4, y4,
                                             pos, c, 1);
                ctx = ctx - SIG_COEF_CONTEXTS + SIG_COEF_CONTEXTS_EOB;
                level = sym(d, d->cdf.coeff_base_eob[tx_sz_ctx][ptype][ctx],
                            3) + 1;
            } else {
                int ctx = get_coeff_base_ctx(d, tx_sz, plane, x4, y4,
                                             pos, c, 0);
                level = sym(d, d->cdf.coeff_base[tx_sz_ctx][ptype][ctx], 4);
            }
            if (level > NUM_BASE_LEVELS) {
                for (int idx = 0; idx < COEFF_BASE_RANGE / (BR_CDF_SIZE - 1);
                     idx++) {
                    int br_ctx;
                    {
                        int adj = Adjusted_Tx_Size[tx_sz];
                        int bwl = Tx_Width_Log2[adj];
                        int txw = Tx_Width[adj];
                        int txh = Tx_Height[adj];
                        int row = pos >> bwl;
                        int col = pos - (row << bwl);
                        int tx_cls = get_tx_class(d->plane_tx_type);
                        int mag = 0;
                        for (int k = 0; k < 3; k++) {
                            int rr = row + Mag_Ref_Offset_With_Tx_Class
                                     [tx_cls][k][0];
                            int rc = col + Mag_Ref_Offset_With_Tx_Class
                                     [tx_cls][k][1];
                            if (rr >= 0 && rc >= 0 && rr < txh &&
                                rc < (1 << bwl)) {
                                int q = d->quant[rr * txw + rc];
                                int lim = COEFF_BASE_RANGE +
                                          NUM_BASE_LEVELS + 1;
                                mag += q < lim ? q : lim;
                            }
                        }
                        mag = ((mag + 1) >> 1) < 6 ? ((mag + 1) >> 1) : 6;
                        if (pos == 0) {
                            br_ctx = mag;
                        } else if (tx_cls == TX_CLASS_2D) {
                            br_ctx = (row < 2 && col < 2) ? mag + 7 :
                                     mag + 14;
                        } else if (tx_cls == TX_CLASS_HORIZ) {
                            br_ctx = col == 0 ? mag + 7 : mag + 14;
                        } else {
                            br_ctx = row == 0 ? mag + 7 : mag + 14;
                        }
                    }
                    int br = sym(d, d->cdf.coeff_br
                                 [tx_sz_ctx < TX_32X32 ? tx_sz_ctx : TX_32X32]
                                 [ptype][br_ctx], BR_CDF_SIZE);
                    level += br;
                    if (br < BR_CDF_SIZE - 1) {
                        break;
                    }
                }
            }
            d->quant[pos] = (int16_t)level;
        }

        for (int c = 0; c < eob; c++) {
            int pos = scan[c];
            int sign = 0;
            if (d->quant[pos] != 0) {
                if (c == 0) {
                    int dc_sign_ctx = 0;
                    int dc_sign = 0;
                    for (int k = 0; k < w4; k++) {
                        if (x4 + k < max_x4) {
                            int s = d->above_dc[plane][x4 + k];
                            if (s == 1) {
                                dc_sign--;
                            } else if (s == 2) {
                                dc_sign++;
                            }
                        }
                    }
                    for (int k = 0; k < h4; k++) {
                        if (y4 + k < max_y4) {
                            int s = d->left_dc[plane][y4 + k];
                            if (s == 1) {
                                dc_sign--;
                            } else if (s == 2) {
                                dc_sign++;
                            }
                        }
                    }
                    dc_sign_ctx = dc_sign < 0 ? 1 : (dc_sign > 0 ? 2 : 0);
                    sign = sym(d, d->cdf.dc_sign[ptype][dc_sign_ctx], 2);
                } else {
                    sign = lit(d, 1);
                }
            }
            if (d->quant[pos] > NUM_BASE_LEVELS + COEFF_BASE_RANGE) {
                int length = 0;
                do {
                    length++;
                    if (lit(d, 1)) {
                        break;
                    }
                } while (length < 20);
                int x = 1;
                for (int i = length - 2; i >= 0; i--) {
                    x = (x << 1) | lit(d, 1);
                }
                d->quant[pos] = (int16_t)(x + COEFF_BASE_RANGE +
                                          NUM_BASE_LEVELS);
            }
            if (pos == 0 && d->quant[pos] > 0) {
                dc_category = sign ? 1 : 2;
            }
            d->quant[pos] &= 0xFFFFF;
            cul_level += d->quant[pos];
            if (sign) {
                d->quant[pos] = (int16_t)-d->quant[pos];
            }
        }
        cul_level = cul_level < 63 ? cul_level : 63;
    }

    for (int i = 0; i < w4; i++) {
        d->above_level[plane][x4 + i] = (uint8_t)cul_level;
        d->above_dc[plane][x4 + i] = (uint8_t)dc_category;
    }
    for (int i = 0; i < h4; i++) {
        d->left_level[plane][y4 + i] = (uint8_t)cul_level;
        d->left_dc[plane][y4 + i] = (uint8_t)dc_category;
    }
    return eob;
}

/* ------------------------------------------------------------------ */
/* Chroma from luma                                                    */
/* ------------------------------------------------------------------ */

static void
predict_chroma_from_luma(struct av1_dec *d, int plane, int start_x,
                         int start_y, int tx_sz)
{
    struct av1_frame *f = &d->f;
    int w = Tx_Width[tx_sz];
    int h = Tx_Height[tx_sz];
    int sub_x = f->subsampling_x;
    int sub_y = f->subsampling_y;
    int alpha = plane == 1 ? d->cfl_alpha_u : d->cfl_alpha_v;
    int16_t l[64][64];
    int luma_avg = 0;

    for (int i = 0; i < h; i++) {
        int luma_y = (start_y + i) << sub_y;
        if (luma_y > d->max_luma_h - (1 << sub_y)) {
            luma_y = d->max_luma_h - (1 << sub_y);
        }
        for (int j = 0; j < w; j++) {
            int luma_x = (start_x + j) << sub_x;
            if (luma_x > d->max_luma_w - (1 << sub_x)) {
                luma_x = d->max_luma_w - (1 << sub_x);
            }
            int t = 0;
            for (int dy = 0; dy <= sub_y; dy++) {
                for (int dx = 0; dx <= sub_x; dx++) {
                    t += d->plane[0][(luma_y + dy) * d->stride[0] +
                                     luma_x + dx];
                }
            }
            int v = t << (3 - sub_x - sub_y);
            l[i][j] = (int16_t)v;
            luma_avg += v;
        }
    }
    luma_avg = round2(luma_avg, Tx_Width_Log2[tx_sz] + Tx_Height_Log2[tx_sz]);

    /* Spec 7.11.5 writes the whole transform block.  The samples in the
     * block overhang past the frame edge are read back by the DC prediction
     * of the neighbouring blocks below and to the right. */
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            uint16_t *p = &d->plane[plane][(start_y + i) * d->stride[plane] +
                                           start_x + j];
            int dc = *p;
            int scaled = round2_signed(alpha * (l[i][j] - luma_avg), 6);
            *p = (uint16_t)clip1(d, dc + scaled);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Transform block / residual                                          */
/* ------------------------------------------------------------------ */

static void
transform_block(struct av1_dec *d, int plane, int base_x, int base_y,
                int tx_sz, int x, int y)
{
    struct av1_frame *f = &d->f;
    int sub_x = plane ? f->subsampling_x : 0;
    int sub_y = plane ? f->subsampling_y : 0;
    int start_x = base_x + 4 * x;
    int start_y = base_y + 4 * y;
    int row = (start_y << sub_y) >> 2;
    int col = (start_x << sub_x) >> 2;
    int step_x = Tx_Width[tx_sz] >> 2;
    int step_y = Tx_Height[tx_sz] >> 2;
    int max_x = (f->mi_cols * 4) >> sub_x;
    int max_y = (f->mi_rows * 4) >> sub_y;
    int b_row4 = row >> sub_y;
    int b_col4 = col >> sub_x;

    if (start_x >= max_x || start_y >= max_y) {
        return;
    }
    if (!d->is_inter) {
        if ((plane == 0 && d->palette_size_y) ||
            (plane != 0 && d->palette_size_uv)) {
            av1_predict_palette(d, plane, start_x, start_y, x, y, tx_sz);
        } else {
            int is_cfl = plane > 0 && d->uv_mode == UV_CFL_PRED;
            int mode;
            if (plane == 0) {
                mode = d->use_filter_intra ?
                    FILTER_DC_PRED + d->filter_intra_mode : d->y_mode;
            } else {
                mode = is_cfl ? DC_PRED : d->uv_mode;
            }
            av1_predict_intra(d, plane, start_x, start_y, tx_sz,
                              (plane == 0 ? d->avail_l :
                               d->avail_l_chroma) || x > 0,
                              (plane == 0 ? d->avail_u :
                               d->avail_u_chroma) || y > 0,
                              bd_get(d, plane, b_row4 - 1, b_col4 + step_x),
                              bd_get(d, plane, b_row4 + step_y, b_col4 - 1),
                              mode, Tx_Width_Log2[tx_sz],
                              Tx_Height_Log2[tx_sz]);
            if (is_cfl) {
                predict_chroma_from_luma(d, plane, start_x, start_y, tx_sz);
            }
        }
        if (plane == 0) {
            d->max_luma_w = start_x + step_x * 4;
            d->max_luma_h = start_y + step_y * 4;
        }
    }
    if (!d->skip) {
        int eob = coeffs(d, plane, start_x, start_y, tx_sz);
        VDBG(av1, "tb %d,%d p%d eob %d", start_x, start_y, plane, eob);
        if (eob > 0) {
            av1_reconstruct(d, plane, start_x, start_y, tx_sz, eob);
        }
    }
    int lf_stride = d->mi_stride_c[plane];
    int lf_cols = (f->mi_cols + sub_x) >> sub_x;
    if (tx_sz < 0 || tx_sz > 18) {
        VERR(av1, "bad tx_sz %d plane %d mi_size %d b4 %d,%d", tx_sz, plane,
             d->mi_size, b_row4, b_col4);
    }
    for (int i = 0; i < step_y; i++) {
        for (int j = 0; j < step_x; j++) {
            int r4 = b_row4 + i;
            int c4 = b_col4 + j;
            if (r4 < ((f->mi_rows + sub_y) >> sub_y) &&
                c4 < lf_cols) {
                d->loopfilter_tx_sizes[plane][(r4 + 1) * lf_stride + c4 + 1] =
                    (uint8_t)tx_sz;
            }
            bd_set(d, plane, r4, c4, 1);
        }
    }
}

static int
residual(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int sb_mask = f->use_128x128_superblock ? 31 : 15;
    int width_chunks = block_width(d->mi_size) > 64 ?
                       block_width(d->mi_size) >> 6 : 1;
    int height_chunks = block_height(d->mi_size) > 64 ?
                        block_height(d->mi_size) >> 6 : 1;
    int mi_size_chunk = (width_chunks > 1 || height_chunks > 1) ?
                        BLOCK_64X64 : d->mi_size;

    for (int chunk_y = 0; chunk_y < height_chunks; chunk_y++) {
        for (int chunk_x = 0; chunk_x < width_chunks; chunk_x++) {
            int mi_row_chunk = d->mi_row + (chunk_y << 4);
            int mi_col_chunk = d->mi_col + (chunk_x << 4);
            for (int plane = 0; plane < 1 + d->has_chroma * 2; plane++) {
                int tx_sz = d->lossless ? TX_4X4 :
                            get_tx_size(d, plane, d->tx_size);
                int step_x = Tx_Width[tx_sz] >> 2;
                int step_y = Tx_Height[tx_sz] >> 2;
                int plane_sz = av1_plane_residual_size(d, mi_size_chunk, plane);
                int num4x4_w = Num_4x4_Blocks_Wide[plane_sz];
                int num4x4_h = Num_4x4_Blocks_High[plane_sz];
                int sub_x = plane ? f->subsampling_x : 0;
                int sub_y = plane ? f->subsampling_y : 0;
                int base_x_block = (d->mi_col >> sub_x) * 4;
                int base_y_block = (d->mi_row >> sub_y) * 4;
                for (int y = 0; y < num4x4_h; y += step_y) {
                    for (int x = 0; x < num4x4_w; x += step_x) {
                        transform_block(d, plane, base_x_block, base_y_block,
                                        tx_sz,
                                        x + ((chunk_x << 4) >> sub_x),
                                        y + ((chunk_y << 4) >> sub_y));
                    }
                }
                (void)sb_mask;
                (void)mi_row_chunk;
                (void)mi_col_chunk;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Block / partition / tile                                            */
/* ------------------------------------------------------------------ */

static int
decode_block(struct av1_dec *d, int r, int c, int sub_size)
{
    struct av1_frame *f = &d->f;
    int ret;

    d->mi_row = r;
    d->mi_col = c;
    d->mi_size = sub_size;
    d->bw4 = Num_4x4_Blocks_Wide[sub_size];
    d->bh4 = Num_4x4_Blocks_High[sub_size];
    if (d->bh4 == 1 && f->subsampling_y && (r & 1) == 0) {
        d->has_chroma = 0;
    } else if (d->bw4 == 1 && f->subsampling_x && (c & 1) == 0) {
        d->has_chroma = 0;
    } else {
        d->has_chroma = f->num_planes > 1;
    }
    d->avail_u = is_inside(d, r - 1, c);
    d->avail_l = is_inside(d, r, c - 1);
    d->avail_u_chroma = d->avail_u;
    d->avail_l_chroma = d->avail_l;
    if (d->has_chroma) {
        if (f->subsampling_y && d->bh4 == 1) {
            d->avail_u_chroma = is_inside(d, r - 2, c);
        }
        if (f->subsampling_x && d->bw4 == 1) {
            d->avail_l_chroma = is_inside(d, r, c - 2);
        }
    } else {
        d->avail_u_chroma = 0;
        d->avail_l_chroma = 0;
    }

    ret = intra_frame_mode_info(d);
    if (ret) {
        return ret;
    }
    VDBG(av1, "block %d,%d sz %d ymode %d uv %d skip %d", r, c, sub_size,
         d->y_mode, d->uv_mode, d->skip);
    ret = palette_tokens(d);
    if (ret) {
        return ret;
    }
    ret = read_block_tx_size(d);
    if (ret) {
        return ret;
    }
    if (d->skip) {
        reset_block_context(d, d->bw4, d->bh4);
    }
    for (int y = 0; y < d->bh4; y++) {
        for (int x = 0; x < d->bw4; x++) {
            int mi = (r + y) * d->mi_stride + c + x;
            d->y_modes[mi] = (uint8_t)d->y_mode;
            if (d->has_chroma) {
                d->uv_modes[mi] = (uint8_t)d->uv_mode;
            }
            d->ref_frames[mi * 2 + 0] = 0;      /* INTRA_FRAME */
            d->ref_frames[mi * 2 + 1] = 8;      /* NONE_FRAME */
        }
    }
    ret = residual(d);
    if (ret) {
        return ret;
    }
    for (int y = 0; y < d->bh4; y++) {
        for (int x = 0; x < d->bw4; x++) {
            int mi = (r + y) * d->mi_stride + c + x;
            d->is_inters[mi] = (uint8_t)d->is_inter;
            d->skips[mi] = (uint8_t)d->skip;
            d->tx_sizes[mi] = (uint8_t)d->tx_size;
            d->mi_sizes[mi] = (uint8_t)sub_size;
            d->segment_ids[mi] = (uint8_t)d->segment_id;
            d->palette_sizes[0][mi] = (uint8_t)d->palette_size_y;
            d->palette_sizes[1][mi] = (uint8_t)d->palette_size_uv;
            for (int i = 0; i < d->palette_size_y; i++) {
                d->palette_colors[0][mi * 8 + i] = d->palette_colors_y[i];
            }
            for (int i = 0; i < d->palette_size_uv; i++) {
                d->palette_colors[1][mi * 8 + i] = d->palette_colors_u[i];
            }
            for (int i = 0; i < AV1_FRAME_LF_COUNT; i++) {
                d->delta_lfs[mi * 4 + i] = (int8_t)d->delta_lf[i];
            }
        }
    }
    return 0;
}

static int
decode_partition(struct av1_dec *d, int r, int c, int bsize)
{
    if (r >= d->f.mi_rows || c >= d->f.mi_cols) {
        return 0;
    }
    d->avail_u = is_inside(d, r - 1, c);
    d->avail_l = is_inside(d, r, c - 1);
    int num4x4 = Num_4x4_Blocks_Wide[bsize];
    int half_block = num4x4 >> 1;
    int quarter_block = half_block >> 1;
    int has_rows = (r + half_block) < d->f.mi_rows;
    int has_cols = (c + half_block) < d->f.mi_cols;
    int partition;

    if (bsize < BLOCK_8X8) {
        partition = PARTITION_NONE;
    } else if (has_rows && has_cols) {
        int bsl = Mi_Width_Log2[bsize];
        int above = d->avail_u &&
            (Mi_Width_Log2[d->mi_sizes[(r - 1) * d->mi_stride + c]] < bsl);
        int left = d->avail_l &&
            (Mi_Height_Log2[d->mi_sizes[r * d->mi_stride + c - 1]] < bsl);
        int ctx = left * 2 + above;
        uint16_t *pcdf;
        int n;
        switch (bsl) {
        case 1:
            pcdf = d->cdf.partition_w8[ctx];
            n = 4;
            break;
        case 2:
            pcdf = d->cdf.partition_w16[ctx];
            n = 10;
            break;
        case 3:
            pcdf = d->cdf.partition_w32[ctx];
            n = 10;
            break;
        case 4:
            pcdf = d->cdf.partition_w64[ctx];
            n = 10;
            break;
        default:
            pcdf = d->cdf.partition_w128[ctx];
            n = 8;
            break;
        }
        partition = sym(d, pcdf, n);
    } else if (has_cols) {
        uint16_t split_or_vert[3];
        int bsl = Mi_Width_Log2[bsize];
        int above = d->avail_u &&
            (Mi_Width_Log2[d->mi_sizes[(r - 1) * d->mi_stride + c]] < bsl);
        int left = d->avail_l &&
            (Mi_Height_Log2[d->mi_sizes[r * d->mi_stride + c - 1]] < bsl);
        int ctx = left * 2 + above;
        uint16_t *pcdf;
        switch (bsl) {
        case 2: pcdf = d->cdf.partition_w16[ctx]; break;
        case 3: pcdf = d->cdf.partition_w32[ctx]; break;
        case 4: pcdf = d->cdf.partition_w64[ctx]; break;
        default: pcdf = d->cdf.partition_w128[ctx]; break;
        }
        int psum = 0;
        static const int vert_idxs[] = {
            PARTITION_VERT, PARTITION_SPLIT, PARTITION_HORZ_A,
            PARTITION_VERT_A, PARTITION_VERT_B
        };
        for (unsigned i = 0; i < sizeof(vert_idxs) / sizeof(vert_idxs[0]);
             i++) {
            psum += pcdf[vert_idxs[i]] - pcdf[vert_idxs[i] - 1];
        }
        if (bsize != BLOCK_128X128) {
            psum += pcdf[PARTITION_VERT_4] - pcdf[PARTITION_VERT_4 - 1];
        }
        split_or_vert[0] = (uint16_t)((1 << 15) - psum);
        split_or_vert[1] = 1 << 15;
        split_or_vert[2] = 0;
        int v = sym(d, split_or_vert, 2);
        partition = v ? PARTITION_SPLIT : PARTITION_HORZ;
    } else if (has_rows) {
        uint16_t split_or_horz[3];
        int bsl = Mi_Width_Log2[bsize];
        int above = d->avail_u &&
            (Mi_Width_Log2[d->mi_sizes[(r - 1) * d->mi_stride + c]] < bsl);
        int left = d->avail_l &&
            (Mi_Height_Log2[d->mi_sizes[r * d->mi_stride + c - 1]] < bsl);
        int ctx = left * 2 + above;
        uint16_t *pcdf;
        switch (bsl) {
        case 2: pcdf = d->cdf.partition_w16[ctx]; break;
        case 3: pcdf = d->cdf.partition_w32[ctx]; break;
        case 4: pcdf = d->cdf.partition_w64[ctx]; break;
        default: pcdf = d->cdf.partition_w128[ctx]; break;
        }
        int psum = 0;
        static const int horz_idxs[] = {
            PARTITION_HORZ, PARTITION_SPLIT, PARTITION_HORZ_A,
            PARTITION_HORZ_B, PARTITION_VERT_A
        };
        for (unsigned i = 0; i < sizeof(horz_idxs) / sizeof(horz_idxs[0]);
             i++) {
            psum += pcdf[horz_idxs[i]] - pcdf[horz_idxs[i] - 1];
        }
        if (bsize != BLOCK_128X128) {
            psum += pcdf[PARTITION_HORZ_4] - pcdf[PARTITION_HORZ_4 - 1];
        }
        split_or_horz[0] = (uint16_t)((1 << 15) - psum);
        split_or_horz[1] = 1 << 15;
        split_or_horz[2] = 0;
        int v = sym(d, split_or_horz, 2);
        partition = v ? PARTITION_SPLIT : PARTITION_VERT;
    } else {
        partition = PARTITION_SPLIT;
    }

    int sub_size = Partition_Subsize[partition][bsize];
    VDBG(av1, "part %d,%d bsz %d -> %d", r, c, bsize, partition);
    int split_size = Partition_Subsize[PARTITION_SPLIT][bsize];
    int ret = 0;

    if (partition == PARTITION_NONE) {
        ret = decode_block(d, r, c, sub_size);
    } else if (partition == PARTITION_HORZ) {
        ret = decode_block(d, r, c, sub_size);
        if (!ret && has_rows) {
            ret = decode_block(d, r + half_block, c, sub_size);
        }
    } else if (partition == PARTITION_VERT) {
        ret = decode_block(d, r, c, sub_size);
        if (!ret && has_cols) {
            ret = decode_block(d, r, c + half_block, sub_size);
        }
    } else if (partition == PARTITION_SPLIT) {
        ret = decode_partition(d, r, c, sub_size);
        if (!ret) ret = decode_partition(d, r, c + half_block, sub_size);
        if (!ret) ret = decode_partition(d, r + half_block, c, sub_size);
        if (!ret) ret = decode_partition(d, r + half_block, c + half_block,
                                         sub_size);
    } else if (partition == PARTITION_HORZ_A) {
        ret = decode_block(d, r, c, split_size);
        if (!ret) ret = decode_block(d, r, c + half_block, split_size);
        if (!ret) ret = decode_block(d, r + half_block, c, sub_size);
    } else if (partition == PARTITION_HORZ_B) {
        ret = decode_block(d, r, c, sub_size);
        if (!ret) ret = decode_block(d, r + half_block, c, split_size);
        if (!ret) ret = decode_block(d, r + half_block, c + half_block,
                                     split_size);
    } else if (partition == PARTITION_VERT_A) {
        ret = decode_block(d, r, c, split_size);
        if (!ret) ret = decode_block(d, r + half_block, c, split_size);
        if (!ret) ret = decode_block(d, r, c + half_block, sub_size);
    } else if (partition == PARTITION_VERT_B) {
        ret = decode_block(d, r, c, sub_size);
        if (!ret) ret = decode_block(d, r, c + half_block, split_size);
        if (!ret) ret = decode_block(d, r + half_block, c + half_block,
                                     split_size);
    } else if (partition == PARTITION_HORZ_4) {
        ret = decode_block(d, r + quarter_block * 0, c, sub_size);
        if (!ret) ret = decode_block(d, r + quarter_block * 1, c, sub_size);
        if (!ret) ret = decode_block(d, r + quarter_block * 2, c, sub_size);
        if (!ret && r + quarter_block * 3 < d->f.mi_rows) {
            ret = decode_block(d, r + quarter_block * 3, c, sub_size);
        }
    } else {
        ret = decode_block(d, r, c + quarter_block * 0, sub_size);
        if (!ret) ret = decode_block(d, r, c + quarter_block * 1, sub_size);
        if (!ret) ret = decode_block(d, r, c + quarter_block * 2, sub_size);
        if (!ret && c + quarter_block * 3 < d->f.mi_cols) {
            ret = decode_block(d, r, c + quarter_block * 3, sub_size);
        }
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* Loop restoration syntax (read_lr / read_lr_unit)                    */
/* ------------------------------------------------------------------ */

static int
count_units_in_frame(int unit_size, int n)
{
    return (n + unit_size - 1) / unit_size;
}

static int
read_lr_unit(struct av1_dec *d, int plane, int unit_row, int unit_col)
{
    struct av1_frame *f = &d->f;
    int restoration_type;
    int stride = d->lr_unit_stride;
    int unit = unit_row * stride + unit_col;

    if (f->restoration_type[plane] == RESTORE_WIENER) {
        restoration_type = sym(d, d->cdf.use_wiener, 2) ? RESTORE_WIENER :
                           RESTORE_NONE;
    } else if (f->restoration_type[plane] == RESTORE_SGRPROJ) {
        restoration_type = sym(d, d->cdf.use_sgrproj, 2) ? RESTORE_SGRPROJ :
                           RESTORE_NONE;
    } else {
        restoration_type = sym(d, d->cdf.restoration_type, 4);
    }
    d->lr_types[plane][unit] = (uint8_t)restoration_type;

    if (restoration_type == RESTORE_WIENER) {
        for (int pass = 0; pass < 2; pass++) {
            int first_coeff = 0;
            if (plane) {
                first_coeff = 1;
                d->lr_wiener[plane][unit * 6 + pass * 3 + 0] = 0;
            }
            for (int j = first_coeff; j < 3; j++) {
                int min = Wiener_Taps_Min[j];
                int max = Wiener_Taps_Max[j];
                int k = Wiener_Taps_K[j];
                int v = signed_subexp_ref_bool(d, min, max + 1, k,
                                               d->lr_ref_wiener[plane][pass][j]);
                d->lr_wiener[plane][unit * 6 + pass * 3 + j] = (int8_t)v;
                d->lr_ref_wiener[plane][pass][j] = v;
            }
        }
    } else if (restoration_type == RESTORE_SGRPROJ) {
        int set = lit(d, SGRPROJ_PARAMS_BITS);
        d->lr_sgrproj[plane][unit * 3 + 0] = (int8_t)set;
        for (int i = 0; i < 2; i++) {
            int radius = Sgr_Params[set][i * 2];
            int min = Sgrproj_Xqd_Min[i];
            int max = Sgrproj_Xqd_Max[i];
            int v;
            if (radius) {
                v = signed_subexp_ref_bool(d, min, max + 1,
                                           SGRPROJ_PRJ_SUBEXP_K,
                                           d->lr_ref_sgr[plane][i]);
            } else {
                v = 0;
                if (i == 1) {
                    v = clip3(min, max, (1 << SGRPROJ_PRJ_BITS) -
                              d->lr_ref_sgr[plane][0]);
                }
            }
            d->lr_sgrproj[plane][unit * 3 + 1 + i] = (int8_t)v;
            d->lr_ref_sgr[plane][i] = v;
        }
    }
    return 0;
}

static int
read_lr(struct av1_dec *d, int r, int c, int sb_size)
{
    VDBG(av1, "read_lr %d,%d", r, c);
    struct av1_frame *f = &d->f;
    if (f->allow_intrabc) {
        return 0;
    }
    int w = Num_4x4_Blocks_Wide[sb_size];
    int h = Num_4x4_Blocks_High[sb_size];

    for (int plane = 0; plane < f->num_planes; plane++) {
        if (f->restoration_type[plane] == RESTORE_NONE) {
            continue;
        }
        int sub_x = plane ? f->subsampling_x : 0;
        int sub_y = plane ? f->subsampling_y : 0;
        int unit_size = f->lr_unit_size[plane];
        int unit_rows = count_units_in_frame(unit_size,
                                             round2(f->height, sub_y));
        int unit_cols = count_units_in_frame(unit_size,
                                             round2(f->upscaled_width, sub_x));
        int unit_row_start = (r * (4 >> sub_y) + unit_size - 1) / unit_size;
        int unit_row_end = unit_rows <
            ((r + h) * (4 >> sub_y) + unit_size - 1) / unit_size ?
            unit_rows : ((r + h) * (4 >> sub_y) + unit_size - 1) / unit_size;
        int numerator, denominator;
        if (f->use_superres) {
            numerator = (4 >> sub_x) * f->superres_denom;
            denominator = unit_size * SUPERRES_NUM;
        } else {
            numerator = 4 >> sub_x;
            denominator = unit_size;
        }
        int unit_col_start = (c * numerator + denominator - 1) / denominator;
        int unit_col_end = unit_cols <
            ((c + w) * numerator + denominator - 1) / denominator ?
            unit_cols : ((c + w) * numerator + denominator - 1) / denominator;
        for (int unit_row = unit_row_start; unit_row < unit_row_end;
             unit_row++) {
            for (int unit_col = unit_col_start; unit_col < unit_col_end;
                 unit_col++) {
                read_lr_unit(d, plane, unit_row, unit_col);
            }
        }
    }
    return 0;
}

static int
lr_alloc_units(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    if (d->lr_types[0] || d->lr_types[1] || d->lr_types[2]) {
        return 0;
    }
    for (int plane = 0; plane < f->num_planes; plane++) {
        if (f->restoration_type[plane] == RESTORE_NONE) {
            continue;
        }
        int sub_x = plane ? f->subsampling_x : 0;
        int sub_y = plane ? f->subsampling_y : 0;
        int unit_size = f->lr_unit_size[plane];
        int rows = count_units_in_frame(unit_size, round2(f->height, sub_y));
        int cols = count_units_in_frame(unit_size,
                                        round2(f->upscaled_width, sub_x));
        d->lr_unit_rows[plane] = rows;
        d->lr_unit_cols[plane] = cols;
        d->lr_unit_stride = cols;
        d->lr_types[plane] = calloc((size_t)rows * cols, 1);
        d->lr_wiener[plane] = calloc((size_t)rows * cols * 6, 1);
        d->lr_sgrproj[plane] = calloc((size_t)rows * cols * 3, 1);
        if (!d->lr_types[plane] || !d->lr_wiener[plane] ||
            !d->lr_sgrproj[plane]) {
            return -ENOMEM;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tile                                                                */
/* ------------------------------------------------------------------ */

void
av1_decode_tile(struct av1_dec *d, const uint8_t *data, size_t len)
{
    struct av1_frame *f = &d->f;
    int sb_size = f->use_128x128_superblock ? BLOCK_128X128 : BLOCK_64X64;
    int sb_size4 = Num_4x4_Blocks_Wide[sb_size];
    int ret = 0;

    av1_range_init(&d->rd, data, len);
    d->have_range = 1;
    av1_cdf_init_non_coeff(&d->cdf);
    av1_cdf_init_coeff(&d->cdf, f->base_q_idx);
    d->current_q_index = f->base_q_idx;
    memset(d->delta_lf, 0, sizeof(d->delta_lf));
    d->max_luma_w = 0;
    d->max_luma_h = 0;
    for (int plane = 0; plane < f->num_planes; plane++) {
        for (int pass = 0; pass < 2; pass++) {
            d->lr_ref_sgr[plane][pass] = Sgrproj_Xqd_Mid[pass];
            for (int i = 0; i < 3; i++) {
                d->lr_ref_wiener[plane][pass][i] = Wiener_Taps_Mid[i];
            }
        }
    }
    ret = lr_alloc_units(d);
    if (ret) {
        d->error = 1;
        return;
    }

    clear_above_context(d);

    for (int r = d->mi_row_start; r < d->mi_row_end && !ret;
         r += sb_size4) {
        clear_left_context(d);
        for (int c = d->mi_col_start; c < d->mi_col_end; c += sb_size4) {
            d->read_deltas = f->delta_q_present;
            clear_cdef(d, r, c);
            clear_block_decoded_flags(d, r, c, sb_size4);
            ret = read_lr(d, r, c, sb_size);
            if (ret) {
                break;
            }
            ret = decode_partition(d, r, c, sb_size);
            if (ret || d->error || d->rd.bits.error) {
                break;
            }
        }
    }
    if (ret && !d->error) {
        VERR(av1, "tile decode failed: %d", ret);
        d->error = 1;
    }
}
