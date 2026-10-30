/*
 * AV1 still picture decoder: intra prediction, inverse transforms and
 * block reconstruction.
 *
 * Implements the spec processes from sections 7.11 (intra prediction,
 * directional/edge filtering, chroma from luma, palette), 7.12.3
 * (reconstruct) and 7.13 (1D/2D inverse transforms).
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "av1_internal.h"
#include "utils.h"
#include "vlog.h"

#define ANGLE_STEP 3
#define INTRA_FILTER_SCALE_BITS 4

/* Edge buffers are indexed from -EDGE_OFF to 2*(w+h)-2 with w+h <= 128. */
#define EDGE_OFF 4
#define EDGE_LEN (2 * 128 + 12)

#define SINPI_1_9 1321
#define SINPI_2_9 2482
#define SINPI_3_9 3344
#define SINPI_4_9 3803

static int64_t
round2(int64_t v, int n)
{
    if (n == 0) {
        return v;
    }
    return (v + (1LL << (n - 1))) >> n;
}

static int
round2_signed(int v, int n)
{
    if (v > 0) {
        return (int)round2(v, n);
    }
    if (v < 0) {
        return -(int)round2(-(int64_t)v, n);
    }
    return 0;
}

static int64_t
clip64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ------------------------------------------------------------------ */
/* Intra edge handling (spec 7.11.2.7 - 7.11.2.12)                     */
/* ------------------------------------------------------------------ */

static int
edge_filter_strength(int w, int h, int filter_type, int delta)
{
    int d = ABS(delta);
    int blk_wh = w + h;
    int strength = 0;

    if (filter_type == 0) {
        if (blk_wh <= 8) {
            if (d >= 56) {
                strength = 1;
            }
        } else if (blk_wh <= 12) {
            if (d >= 40) {
                strength = 1;
            }
        } else if (blk_wh <= 16) {
            if (d >= 40) {
                strength = 1;
            }
        } else if (blk_wh <= 24) {
            if (d >= 8) {
                strength = 1;
            }
            if (d >= 16) {
                strength = 2;
            }
            if (d >= 32) {
                strength = 3;
            }
        } else if (blk_wh <= 32) {
            strength = 1;
            if (d >= 4) {
                strength = 2;
            }
            if (d >= 32) {
                strength = 3;
            }
        } else {
            strength = 3;
        }
    } else {
        if (blk_wh <= 8) {
            if (d >= 40) {
                strength = 1;
            }
            if (d >= 64) {
                strength = 2;
            }
        } else if (blk_wh <= 16) {
            if (d >= 20) {
                strength = 1;
            }
            if (d >= 48) {
                strength = 2;
            }
        } else if (blk_wh <= 24) {
            if (d >= 4) {
                strength = 3;
            }
        } else {
            strength = 3;
        }
    }
    return strength;
}

static int
edge_upsample_sel(int w, int h, int filter_type, int delta)
{
    int d = ABS(delta);
    int blk_wh = w + h;

    if (d <= 0 || d >= 40) {
        return 0;
    }
    return filter_type == 0 ? blk_wh <= 16 : blk_wh <= 8;
}

static void
intra_edge_filter(int32_t *buf, int sz, int strength)
{
    int32_t edge[130];
    int i, j;

    if (strength == 0) {
        return;
    }
    for (i = 0; i < sz; i++) {
        edge[i] = buf[i - 1];
    }
    for (i = 1; i < sz; i++) {
        int s = 0;
        for (j = 0; j < INTRA_EDGE_TAPS; j++) {
            int k = clip3(0, sz - 1, i - 2 + j);
            s += Intra_Edge_Kernel[strength - 1][j] * edge[k];
        }
        buf[i - 1] = (s + 8) >> 4;
    }
}

static void
intra_edge_upsample(int32_t *buf, int num_px, int bitdepth)
{
    int32_t dup[132];
    int maxv = (1 << bitdepth) - 1;
    int i, s;

    dup[0] = buf[-1];
    for (i = -1; i < num_px; i++) {
        dup[i + 2] = buf[i];
    }
    dup[num_px + 2] = buf[num_px - 1];

    buf[-2] = dup[0];
    for (i = 0; i < num_px; i++) {
        s = -dup[i] + 9 * dup[i + 1] + 9 * dup[i + 2] - dup[i + 3];
        buf[2 * i - 1] = clamp(round2(s, 4), maxv);
        buf[2 * i] = dup[i + 2];
    }
}

static int
av1_is_smooth(struct av1_dec *d, int row, int col, int plane)
{
    int mode;

    if (plane == 0) {
        mode = d->y_modes[row * d->mi_stride + col];
    } else {
        if (d->ref_frames[(row * d->mi_stride + col) * 2] > 0) {
            return 0;
        }
        mode = d->uv_modes[row * d->mi_stride + col];
    }
    return mode == SMOOTH_PRED || mode == SMOOTH_V_PRED ||
           mode == SMOOTH_H_PRED;
}

/* Spec 7.11.2.8 intra filter type process. */
static int
av1_filter_type(struct av1_dec *d, int plane)
{
    int sx = d->f.subsampling_x, sy = d->f.subsampling_y;
    int above_smooth = 0, left_smooth = 0;

    if ((plane == 0) ? d->avail_u : d->avail_u_chroma) {
        int r = d->mi_row - 1, c = d->mi_col;
        if (plane > 0) {
            if (sx && !(d->mi_col & 1)) {
                c++;
            }
            if (sy && (d->mi_row & 1)) {
                r--;
            }
        }
        above_smooth = av1_is_smooth(d, r, c, plane);
    }
    if ((plane == 0) ? d->avail_l : d->avail_l_chroma) {
        int r = d->mi_row, c = d->mi_col - 1;
        if (plane > 0) {
            if (sx && (d->mi_col & 1)) {
                c--;
            }
            if (sy && !(d->mi_row & 1)) {
                r++;
            }
        }
        left_smooth = av1_is_smooth(d, r, c, plane);
    }
    return above_smooth || left_smooth;
}

/* ------------------------------------------------------------------ */
/* Non directional intra prediction modes                              */
/* ------------------------------------------------------------------ */

static void
pred_paeth(int32_t pred[64][64], const int32_t *above, const int32_t *left,
           int w, int h)
{
    int i, j;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int base = above[j] + left[i] - above[-1];
            int p_left = ABS(base - left[i]);
            int p_top = ABS(base - above[j]);
            int p_top_left = ABS(base - above[-1]);

            if (p_left <= p_top && p_left <= p_top_left) {
                pred[i][j] = left[i];
            } else if (p_top <= p_top_left) {
                pred[i][j] = above[j];
            } else {
                pred[i][j] = above[-1];
            }
        }
    }
}

static void
pred_dc(int32_t pred[64][64], const int32_t *above, const int32_t *left,
        int have_left, int have_above, int log2w, int log2h, int bitdepth)
{
    int w = 1 << log2w, h = 1 << log2h;
    int maxv = (1 << bitdepth) - 1;
    int sum = 0, avg, k;

    if (have_left && have_above) {
        for (k = 0; k < h; k++) {
            sum += left[k];
        }
        for (k = 0; k < w; k++) {
            sum += above[k];
        }
        sum += (w + h) >> 1;
        avg = sum / (w + h);
    } else if (have_left) {
        for (k = 0; k < h; k++) {
            sum += left[k];
        }
        avg = clamp((sum + (h >> 1)) >> log2h, maxv);
    } else if (have_above) {
        for (k = 0; k < w; k++) {
            sum += above[k];
        }
        avg = clamp((sum + (w >> 1)) >> log2w, maxv);
    } else {
        avg = 1 << (bitdepth - 1);
    }
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            pred[i][j] = avg;
        }
    }
}

static const int16_t *const sm_weight_tab[5] = {
    Sm_Weights_Tx_4x4, Sm_Weights_Tx_8x8, Sm_Weights_Tx_16x16,
    Sm_Weights_Tx_32x32, Sm_Weights_Tx_64x64
};

static void
pred_smooth(int32_t pred[64][64], const int32_t *above, const int32_t *left,
            int mode, int log2w, int log2h, int w, int h)
{
    const int16_t *wx = sm_weight_tab[log2w - 2];
    const int16_t *wy = sm_weight_tab[log2h - 2];
    int i, j;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int v;

            if (mode == SMOOTH_PRED) {
                v = wy[i] * above[j] + (256 - wy[i]) * left[h - 1] +
                    wx[j] * left[i] + (256 - wx[j]) * above[w - 1];
                pred[i][j] = (int32_t)round2(v, 9);
            } else if (mode == SMOOTH_V_PRED) {
                v = wy[i] * above[j] + (256 - wy[i]) * left[h - 1];
                pred[i][j] = (int32_t)round2(v, 8);
            } else {
                v = wx[j] * left[i] + (256 - wx[j]) * above[w - 1];
                pred[i][j] = (int32_t)round2(v, 8);
            }
        }
    }
}

/* Spec 7.11.2.3 recursive intra prediction process. */
static void
pred_filter_intra(int32_t pred[64][64], const int32_t *above,
                  const int32_t *left, int fim, int w, int h)
{
    int w4 = w >> 2, h2 = h >> 1;
    int i2, j4, i1, j1, i;

    for (i2 = 0; i2 < h2; i2++) {
        for (j4 = 0; j4 < w4; j4++) {
            int32_t p[7];

            for (i = 0; i < 5; i++) {
                if (i2 == 0) {
                    p[i] = above[(j4 << 2) + i - 1];
                } else if (j4 == 0 && i == 0) {
                    p[i] = left[(i2 << 1) - 1];
                } else {
                    p[i] = pred[(i2 << 1) - 1][(j4 << 2) + i - 1];
                }
            }
            for (i = 5; i < 7; i++) {
                if (j4 == 0) {
                    p[i] = left[(i2 << 1) + i - 5];
                } else {
                    p[i] = pred[(i2 << 1) + i - 5][(j4 << 2) - 1];
                }
            }
            for (i1 = 0; i1 < 2; i1++) {
                for (j1 = 0; j1 < 4; j1++) {
                    int64_t pr = 0;
                    int k;
                    for (k = 0; k < 7; k++) {
                        pr += (int64_t)Intra_Filter_Taps[fim][(i1 << 2) + j1][k] *
                              p[k];
                    }
                    pred[(i2 << 1) + i1][(j4 << 2) + j1] =
                        clamp(round2_signed((int)pr, INTRA_FILTER_SCALE_BITS),
                              (1 << 30) - 1);
                }
            }
        }
    }
}

/* Spec 7.11.2.4 directional intra prediction process. */
static void
pred_directional(struct av1_dec *d, int plane, int x, int y,
                 int have_left, int have_above, int mode, int w, int h,
                 int max_x, int max_y, int32_t pred[64][64],
                 int32_t *above, int32_t *left)
{
    int angle_delta = plane == 0 ? d->angle_delta_y : d->angle_delta_uv;
    int p_angle = Mode_To_Angle[mode] + angle_delta * ANGLE_STEP;
    int filter_type = 0;
    int ups_above = 0, ups_left = 0;
    int i, j;

    if (d->f.enable_intra_edge_filter) {
        if (p_angle != 90 && p_angle != 180) {
            if (p_angle > 90 && p_angle < 180 && w + h >= 24) {
                int corner = (int)round2((int64_t)left[0] * 5 +
                                         (int64_t)above[-1] * 6 +
                                         (int64_t)above[0] * 5, 4);
                left[-1] = above[-1] = corner;
            }
            filter_type = av1_filter_type(d, plane);
            if (have_above) {
                int strength = edge_filter_strength(w, h, filter_type,
                                                    p_angle - 90);
                int num_px = MIN(w, max_x - x + 1) + (p_angle < 90 ? h : 0) + 1;
                intra_edge_filter(above, num_px, strength);
            }
            if (have_left) {
                int strength = edge_filter_strength(w, h, filter_type,
                                                    p_angle - 180);
                int num_px = MIN(h, max_y - y + 1) + (p_angle > 180 ? w : 0) + 1;
                intra_edge_filter(left, num_px, strength);
            }
        }
        ups_above = edge_upsample_sel(w, h, filter_type, p_angle - 90);
        if (ups_above) {
            intra_edge_upsample(above, w + (p_angle < 90 ? h : 0),
                                d->f.bitdepth);
        }
        ups_left = edge_upsample_sel(w, h, filter_type, p_angle - 180);
        if (ups_left) {
            intra_edge_upsample(left, h + (p_angle > 180 ? w : 0),
                                d->f.bitdepth);
        }
    }

    if (p_angle < 90) {
        int dx = Dr_Intra_Derivative[p_angle];
        int max_base_x = (w + h - 1) << ups_above;

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                int idx = (i + 1) * dx;
                int base = (idx >> (6 - ups_above)) + (j << ups_above);
                int shift = ((idx * (1 << ups_above)) >> 1) & 0x1f;

                if (base < max_base_x) {
                    pred[i][j] = (int32_t)round2(
                        (int64_t)above[base] * (32 - shift) +
                        (int64_t)above[base + 1] * shift, 5);
                } else {
                    pred[i][j] = above[max_base_x];
                }
            }
        }
    } else if (p_angle < 180) {
        int dx = Dr_Intra_Derivative[180 - p_angle];
        int dy = Dr_Intra_Derivative[p_angle - 90];

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                int idx = (j << 6) - (i + 1) * dx;
                int base = idx >> (6 - ups_above);

                if (base >= -(1 << ups_above)) {
                    int shift = ((idx * (1 << ups_above)) >> 1) & 0x1f;
                    pred[i][j] = (int32_t)round2(
                        (int64_t)above[base] * (32 - shift) +
                        (int64_t)above[base + 1] * shift, 5);
                } else {
                    int shift;
                    idx = (i << 6) - (j + 1) * dy;
                    base = idx >> (6 - ups_left);
                    shift = ((idx * (1 << ups_left)) >> 1) & 0x1f;
                    pred[i][j] = (int32_t)round2(
                        (int64_t)left[base] * (32 - shift) +
                        (int64_t)left[base + 1] * shift, 5);
                }
            }
        }
    } else if (p_angle > 180) {
        int dy = Dr_Intra_Derivative[270 - p_angle];

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                int idx = (j + 1) * dy;
                int base = (idx >> (6 - ups_left)) + (i << ups_left);
                int shift = ((idx * (1 << ups_left)) >> 1) & 0x1f;

                pred[i][j] = (int32_t)round2(
                    (int64_t)left[base] * (32 - shift) +
                    (int64_t)left[base + 1] * shift, 5);
            }
        }
    } else if (p_angle == 90) {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                pred[i][j] = above[j];
            }
        }
    } else {
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                pred[i][j] = left[i];
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1D inverse transforms (spec 7.13.2)                                 */
/* ------------------------------------------------------------------ */

static int
brev(int num_bits, int x)
{
    int t = 0, i;

    for (i = 0; i < num_bits; i++) {
        t |= ((x >> i) & 1) << (num_bits - 1 - i);
    }
    return t;
}

static int
cos128(int angle)
{
    int a = angle & 255;

    if (a <= 64) {
        return Cos128_Lookup[a];
    }
    if (a <= 128) {
        return -Cos128_Lookup[128 - a];
    }
    if (a <= 192) {
        return -Cos128_Lookup[a - 128];
    }
    return Cos128_Lookup[256 - a];
}

static int
sin128(int angle)
{
    return cos128(angle - 64);
}

/* Butterfly rotation B(a, b, angle, flip). */
static void
bfly(int32_t *t, int a, int b, int angle, int flip)
{
    int c = cos128(angle), s = sin128(angle);
    int64_t x = (int64_t)t[a] * c - (int64_t)t[b] * s;
    int64_t y = (int64_t)t[a] * s + (int64_t)t[b] * c;
    int32_t ta = (int32_t)round2(x, 12);
    int32_t tb = (int32_t)round2(y, 12);

    if (flip) {
        int32_t tmp = ta;
        ta = tb;
        tb = tmp;
    }
    t[a] = ta;
    t[b] = tb;
}

/* Hadamard rotation H(a, b, flip, r). */
static void
hadm(int32_t *t, int a, int b, int flip, int r)
{
    int64_t lo = -(1LL << (r - 1)), hi = (1LL << (r - 1)) - 1;
    int64_t x, y;

    if (flip) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    x = t[a];
    y = t[b];
    t[a] = (int32_t)clip64(x + y, lo, hi);
    t[b] = (int32_t)clip64(x - y, lo, hi);
}

static void
inv_dct(int32_t *t, int n, int r)
{
    int32_t copy[64];
    int i, j;

    memcpy(copy, t, sizeof(int32_t) * (size_t)(1 << n));
    for (i = 0; i < (1 << n); i++) {
        t[i] = copy[brev(n, i)];
    }

    if (n == 6) {
        for (i = 0; i < 16; i++) {
            bfly(t, 32 + i, 63 - i, 63 - 4 * brev(4, i), 0);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 8; i++) {
            bfly(t, 16 + i, 31 - i, 6 + (brev(3, 7 - i) << 3), 0);
        }
    }
    if (n == 6) {
        for (i = 0; i < 16; i++) {
            hadm(t, 32 + i * 2, 33 + i * 2, i & 1, r);
        }
    }
    if (n >= 4) {
        for (i = 0; i < 4; i++) {
            bfly(t, 8 + i, 15 - i, 12 + (brev(2, 3 - i) << 4), 0);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 8; i++) {
            hadm(t, 16 + 2 * i, 17 + 2 * i, i & 1, r);
        }
    }
    if (n == 6) {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 2; j++) {
                bfly(t, 62 - i * 4 - j, 33 + i * 4 + j,
                     60 - 16 * brev(2, i) + 64 * j, 1);
            }
        }
    }
    if (n >= 3) {
        for (i = 0; i < 2; i++) {
            bfly(t, 4 + i, 7 - i, 56 - 32 * i, 0);
        }
    }
    if (n >= 4) {
        for (i = 0; i < 4; i++) {
            hadm(t, 8 + 2 * i, 9 + 2 * i, i & 1, r);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                bfly(t, 30 - 4 * i - j, 17 + 4 * i + j,
                     24 + (j << 6) + ((1 - i) << 5), 1);
            }
        }
    }
    if (n == 6) {
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 2; j++) {
                hadm(t, 32 + i * 4 + j, 35 + i * 4 - j, i & 1, r);
            }
        }
    }
    for (i = 0; i < 2; i++) {
        bfly(t, 2 * i, 2 * i + 1, 32 + 16 * i, 1 - i);
    }
    if (n >= 3) {
        for (i = 0; i < 2; i++) {
            hadm(t, 4 + 2 * i, 5 + 2 * i, i, r);
        }
    }
    if (n >= 4) {
        for (i = 0; i < 2; i++) {
            bfly(t, 14 - i, 9 + i, 48 + 64 * i, 1);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 2; j++) {
                hadm(t, 16 + 4 * i + j, 19 + 4 * i - j, i & 1, r);
            }
        }
    }
    if (n == 6) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 4; j++) {
                bfly(t, 61 - i * 8 - j, 34 + i * 8 + j,
                     56 - i * 32 + (j >> 1) * 64, 1);
            }
        }
    }
    for (i = 0; i < 2; i++) {
        hadm(t, i, 3 - i, 0, r);
    }
    if (n >= 3) {
        bfly(t, 6, 5, 32, 1);
    }
    if (n >= 4) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                hadm(t, 8 + 4 * i + j, 11 + 4 * i - j, i, r);
            }
        }
    }
    if (n >= 5) {
        for (i = 0; i < 4; i++) {
            bfly(t, 29 - i, 18 + i, 48 + (i >> 1) * 64, 1);
        }
    }
    if (n == 6) {
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                hadm(t, 32 + 8 * i + j, 39 + 8 * i - j, i & 1, r);
            }
        }
    }
    if (n >= 3) {
        for (i = 0; i < 4; i++) {
            hadm(t, i, 7 - i, 0, r);
        }
    }
    if (n >= 4) {
        for (i = 0; i < 2; i++) {
            bfly(t, 13 - i, 10 + i, 32, 1);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 4; j++) {
                hadm(t, 16 + i * 8 + j, 23 + i * 8 - j, i, r);
            }
        }
    }
    if (n == 6) {
        for (i = 0; i < 8; i++) {
            bfly(t, 59 - i, 36 + i, i < 4 ? 48 : 112, 1);
        }
    }
    if (n >= 4) {
        for (i = 0; i < 8; i++) {
            hadm(t, i, 15 - i, 0, r);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 4; i++) {
            bfly(t, 27 - i, 20 + i, 32, 1);
        }
    }
    if (n == 6) {
        for (i = 0; i < 8; i++) {
            hadm(t, 32 + i, 47 - i, 0, r);
            hadm(t, 48 + i, 63 - i, 1, r);
        }
    }
    if (n >= 5) {
        for (i = 0; i < 16; i++) {
            hadm(t, i, 31 - i, 0, r);
        }
    }
    if (n == 6) {
        for (i = 0; i < 8; i++) {
            bfly(t, 55 - i, 40 + i, 32, 1);
        }
        for (i = 0; i < 32; i++) {
            hadm(t, i, 63 - i, 0, r);
        }
    }
}

static void
adst_input_perm(int32_t *t, int n)
{
    int32_t copy[64];
    int n0 = 1 << n, i;

    memcpy(copy, t, sizeof(int32_t) * (size_t)n0);
    for (i = 0; i < n0; i++) {
        t[i] = copy[(i & 1) ? (i - 1) : (n0 - i - 1)];
    }
}

static void
adst_output_perm(int32_t *t, int n)
{
    int32_t copy[64];
    int n0 = 1 << n, i;

    memcpy(copy, t, sizeof(int32_t) * (size_t)n0);
    for (i = 0; i < n0; i++) {
        int a = (i >> 3) & 1;
        int b = ((i >> 2) & 1) ^ ((i >> 3) & 1);
        int c = ((i >> 1) & 1) ^ ((i >> 2) & 1);
        int e = (i & 1) ^ ((i >> 1) & 1);
        int idx = ((e << 3) | (c << 2) | (b << 1) | a) >> (4 - n);

        t[i] = (i & 1) ? -copy[idx] : copy[idx];
    }
}

/* Spec 7.13.2.6 inverse ADST4 process. */
static void
inv_adst4(int32_t *t)
{
    int64_t s[7], x[4];

    s[0] = SINPI_1_9 * t[0];
    s[1] = SINPI_2_9 * t[0];
    s[2] = SINPI_3_9 * t[1];
    s[3] = SINPI_4_9 * t[2];
    s[4] = SINPI_1_9 * t[2];
    s[5] = SINPI_2_9 * t[3];
    s[6] = SINPI_4_9 * t[3];

    s[0] += s[3];
    s[1] -= s[4];
    s[3] = s[2];
    s[2] = SINPI_3_9 * (t[0] - t[2] + t[3]);

    s[0] += s[5];
    s[1] -= s[6];

    x[0] = s[0] + s[3];
    x[1] = s[1] + s[3];
    x[2] = s[2];
    x[3] = s[0] + s[1];
    x[3] -= s[3];

    t[0] = (int32_t)round2(x[0], 12);
    t[1] = (int32_t)round2(x[1], 12);
    t[2] = (int32_t)round2(x[2], 12);
    t[3] = (int32_t)round2(x[3], 12);
}

static void
inv_adst8(int32_t *t, int r)
{
    int i, j;

    adst_input_perm(t, 3);
    for (i = 0; i < 4; i++) {
        bfly(t, 2 * i, 2 * i + 1, 60 - 16 * i, 1);
    }
    for (i = 0; i < 4; i++) {
        hadm(t, i, 4 + i, 0, r);
    }
    for (i = 0; i < 2; i++) {
        bfly(t, 4 + 3 * i, 5 + i, 48 - 32 * i, 1);
    }
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 2; i++) {
            hadm(t, 4 * j + i, 2 + 4 * j + i, 0, r);
        }
    }
    for (i = 0; i < 2; i++) {
        bfly(t, 2 + 4 * i, 3 + 4 * i, 32, 1);
    }
    adst_output_perm(t, 3);
}

static void
inv_adst16(int32_t *t, int r)
{
    int i, j;

    adst_input_perm(t, 4);
    for (i = 0; i < 8; i++) {
        bfly(t, 2 * i, 2 * i + 1, 62 - 8 * i, 1);
    }
    for (i = 0; i < 8; i++) {
        hadm(t, i, 8 + i, 0, r);
    }
    for (i = 0; i < 2; i++) {
        bfly(t, 8 + 2 * i, 9 + 2 * i, 56 - 32 * i, 1);
        bfly(t, 13 + 2 * i, 12 + 2 * i, 8 + 32 * i, 1);
    }
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 4; i++) {
            hadm(t, 8 * j + i, 4 + 8 * j + i, 0, r);
        }
    }
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 2; i++) {
            bfly(t, 4 + 8 * j + 3 * i, 5 + 8 * j + i, 48 - 32 * i, 1);
        }
    }
    for (j = 0; j < 4; j++) {
        for (i = 0; i < 2; i++) {
            hadm(t, 4 * j + i, 2 + 4 * j + i, 0, r);
        }
    }
    for (i = 0; i < 4; i++) {
        bfly(t, 2 + 4 * i, 3 + 4 * i, 32, 1);
    }
    adst_output_perm(t, 4);
}

static void
inv_adst(int32_t *t, int n, int r)
{
    if (n == 2) {
        inv_adst4(t);
    } else if (n == 3) {
        inv_adst8(t, r);
    } else {
        inv_adst16(t, r);
    }
}

/* Spec 7.13.2.10 inverse WHT process (lossless). */
static void
inv_wht(int32_t *t, int shift)
{
    int a = t[0] >> shift;
    int c = t[1] >> shift;
    int d = t[2] >> shift;
    int b = t[3] >> shift;
    int e;

    a += c;
    d -= b;
    e = (a - d) >> 1;
    b = e - b;
    c = e - c;
    a -= b;
    d += c;
    t[0] = a;
    t[1] = b;
    t[2] = c;
    t[3] = d;
}

static void
inv_identity(int32_t *t, int n)
{
    int i;

    if (n == 2) {
        for (i = 0; i < 4; i++) {
            t[i] = (int32_t)round2((int64_t)t[i] * 5793, 12);
        }
    } else if (n == 3) {
        for (i = 0; i < 8; i++) {
            t[i] *= 2;
        }
    } else if (n == 4) {
        for (i = 0; i < 16; i++) {
            t[i] = (int32_t)round2((int64_t)t[i] * 11586, 12);
        }
    } else if (n == 5) {
        for (i = 0; i < 32; i++) {
            t[i] *= 4;
        }
    }
}

/* 0 = DCT, 1 = ADST/FLIPADST, 2 = identity. */
static int
tx1d_class(int type, int is_row)
{
    switch (type) {
    case DCT_DCT:
        return 0;
    case ADST_DCT:
        return is_row ? 0 : 1;
    case ADST_ADST:
    case FLIPADST_FLIPADST:
    case ADST_FLIPADST:
    case FLIPADST_ADST:
        return 1;
    case V_ADST:
        return is_row ? 2 : 1;
    case H_ADST:
        return is_row ? 1 : 2;
    case DCT_ADST:
    case DCT_FLIPADST:
        return is_row ? 1 : 0;
    case FLIPADST_DCT:
        return is_row ? 0 : 1;
    case V_DCT:
        return is_row ? 2 : 0;
    case H_DCT:
        return is_row ? 0 : 2;
    case V_FLIPADST:
        return is_row ? 2 : 1;
    case H_FLIPADST:
        return is_row ? 1 : 2;
    default:
        return 2;
    }
}

/* Spec 7.13.3 2D inverse transform process. */
static void
inv_tx_2d(struct av1_dec *d, int tx_size, int32_t res[64][64])
{
    int32_t t[64];
    int log2w = Tx_Width_Log2[tx_size];
    int log2h = Tx_Height_Log2[tx_size];
    int w = 1 << log2w, h = 1 << log2h;
    int row_shift = d->lossless ? 0 : Transform_Row_Shift[tx_size];
    int col_shift = d->lossless ? 0 : 4;
    int row_clamp = d->f.bitdepth + 8;
    int col_clamp = MAX(d->f.bitdepth + 6, 16);
    int lo = -(1 << (col_clamp - 1)), hi = (1 << (col_clamp - 1)) - 1;
    int type = d->plane_tx_type;
    int i, j, cls;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            t[j] = (i < 32 && j < 32) ? d->dequant[i][j] : 0;
        }
        if ((log2w - log2h == 1) || (log2h - log2w == 1)) {
            for (j = 0; j < w; j++) {
                t[j] = (int32_t)round2((int64_t)t[j] * 2896, 12);
            }
        }
        if (d->lossless) {
            inv_wht(t, 2);
        } else {
            cls = tx1d_class(type, 1);
            if (cls == 0) {
                inv_dct(t, log2w, row_clamp);
            } else if (cls == 1) {
                inv_adst(t, log2w, row_clamp);
            } else {
                inv_identity(t, log2w);
            }
        }
        for (j = 0; j < w; j++) {
            res[i][j] = clip3(lo, hi, (int)round2(t[j], row_shift));
        }
    }

    for (j = 0; j < w; j++) {
        for (i = 0; i < h; i++) {
            t[i] = res[i][j];
        }
        if (d->lossless) {
            inv_wht(t, 0);
        } else {
            cls = tx1d_class(type, 0);
            if (cls == 0) {
                inv_dct(t, log2h, col_clamp);
            } else if (cls == 1) {
                inv_adst(t, log2h, col_clamp);
            } else {
                inv_identity(t, log2h);
            }
        }
        for (i = 0; i < h; i++) {
            res[i][j] = (int)round2(t[i], col_shift);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

void
av1_predict_intra(struct av1_dec *d, int plane, int x, int y, int tx_size,
                  int have_left, int have_above, int have_above_right,
                  int have_below_left, int mode, int log2w, int log2h)
{
    uint16_t *p = d->plane[plane];
    int stride = d->stride[plane];
    int bitdepth = d->f.bitdepth;
    /* Edge clamping is bounded by the (4x4-aligned) mi grid, not the
     * visible frame size: samples in the block overhang past the frame
     * edge are part of the prediction for the pixels that are visible. */
    int max_x = ((d->f.mi_cols * 4) >> (plane ? d->f.subsampling_x : 0)) - 1;
    int max_y = ((d->f.mi_rows * 4) >> (plane ? d->f.subsampling_y : 0)) - 1;
    int w = 1 << log2w, h = 1 << log2h;
    int32_t pred[64][64];
    int32_t above[EDGE_LEN], left[EDGE_LEN];
    int32_t *above_row = above + EDGE_OFF;
    int32_t *left_col = left + EDGE_OFF;
    int i, j;

    (void)tx_size;

    /* Build AboveRow and LeftCol per spec 7.11.1. */
    if (!have_above && have_left) {
        int v = p[(size_t)y * stride + x - 1];
        for (i = 0; i < w + h; i++) {
            above_row[i] = v;
        }
    } else if (!have_above && !have_left) {
        int v = (1 << (bitdepth - 1)) - 1;
        for (i = 0; i < w + h; i++) {
            above_row[i] = v;
        }
    } else {
        int above_limit = MIN(max_x, x + (have_above_right ? 2 * w : w) - 1);
        for (i = 0; i < w + h; i++) {
            above_row[i] = p[(size_t)(y - 1) * stride +
                             MIN(above_limit, x + i)];
        }
    }
    if (have_above && have_left) {
        above_row[-1] = p[(size_t)(y - 1) * stride + x - 1];
    } else if (have_above) {
        above_row[-1] = p[(size_t)(y - 1) * stride + x];
    } else if (have_left) {
        above_row[-1] = p[(size_t)y * stride + x - 1];
    } else {
        above_row[-1] = 1 << (bitdepth - 1);
    }

    if (!have_left && have_above) {
        int v = p[(size_t)(y - 1) * stride + x];
        for (i = 0; i < w + h; i++) {
            left_col[i] = v;
        }
    } else if (!have_left && !have_above) {
        int v = (1 << (bitdepth - 1)) + 1;
        for (i = 0; i < w + h; i++) {
            left_col[i] = v;
        }
    } else {
        int left_limit = MIN(max_y, y + (have_below_left ? 2 * h : h) - 1);
        for (i = 0; i < w + h; i++) {
            left_col[i] = p[(size_t)MIN(left_limit, y + i) * stride + x - 1];
        }
    }
    left_col[-1] = above_row[-1];

    /* Replicate the last valid sample so that reads one entry past
     * the spec range (zero weighted) stay in bounds. */
    for (i = w + h; i < 2 * (w + h) + 6; i++) {
        above_row[i] = above_row[w + h - 1];
        left_col[i] = left_col[w + h - 1];
    }

    if (mode >= FILTER_DC_PRED) {
        pred_filter_intra(pred, above_row, left_col,
                          mode - FILTER_DC_PRED, w, h);
    } else if (mode >= V_PRED && mode <= D67_PRED) {
        pred_directional(d, plane, x, y, have_left, have_above, mode,
                         w, h, max_x, max_y, pred, above_row, left_col);
    } else if (mode == SMOOTH_PRED || mode == SMOOTH_V_PRED ||
               mode == SMOOTH_H_PRED) {
        pred_smooth(pred, above_row, left_col, mode, log2w, log2h, w, h);
    } else if (mode == DC_PRED) {
        pred_dc(pred, above_row, left_col, have_left, have_above,
                log2w, log2h, bitdepth);
    } else {
        pred_paeth(pred, above_row, left_col, w, h);
    }

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            p[(size_t)(y + i) * stride + x + j] = (uint16_t)pred[i][j];
        }
    }
}

/* Spec 7.12.2: get_dc_quant / get_ac_quant. */
static int
get_dc_quant(struct av1_dec *d, int plane)
{
    struct av1_frame *f = &d->f;
    int qindex = av1_get_qindex(d, 0, d->segment_id);
    int b;

    if (plane == 0) {
        b = qindex + f->delta_q_ydc;
    } else if (plane == 1) {
        b = qindex + f->delta_q_udc;
    } else {
        b = qindex + f->delta_q_vdc;
    }
    return Dc_Qlookup[(f->bitdepth - 8) >> 1][clip3(0, 255, b)];
}

static int
get_ac_quant(struct av1_dec *d, int plane)
{
    struct av1_frame *f = &d->f;
    int qindex = av1_get_qindex(d, 0, d->segment_id);
    int b;

    if (plane == 0) {
        b = qindex;
    } else if (plane == 1) {
        b = qindex + f->delta_q_uac;
    } else {
        b = qindex + f->delta_q_vac;
    }
    return Ac_Qlookup[(f->bitdepth - 8) >> 1][clip3(0, 255, b)];
}

void
av1_reconstruct(struct av1_dec *d, int plane, int x, int y, int tx_size,
                int eob)
{
    uint16_t *p = d->plane[plane];
    int stride = d->stride[plane];
    int w = Tx_Width[tx_size], h = Tx_Height[tx_size];
    int maxv = (1 << d->f.bitdepth) - 1;
    int type = d->plane_tx_type;
    int flip_ud = type == FLIPADST_DCT || type == FLIPADST_ADST ||
                  type == V_FLIPADST || type == FLIPADST_FLIPADST;
    int flip_lr = type == DCT_FLIPADST || type == ADST_FLIPADST ||
                  type == H_FLIPADST || type == FLIPADST_FLIPADST;
    int i, j;

    if (eob == 0) {
        return;
    }

    /* Spec 7.12.3 step 1: dequantization. */
    {
        struct av1_frame *f = &d->f;
        int log2w = Tx_Width_Log2[tx_size], log2h = Tx_Height_Log2[tx_size];
        int tw = MIN(32, 1 << log2w), th = MIN(32, 1 << log2h);
        int dq_denom;
        int seg_qm = 15, qm_off = Qm_Offset[tx_size];
        int dc = get_dc_quant(d, plane), ac = get_ac_quant(d, plane);
        int lo = -(1 << (7 + d->f.bitdepth));
        int hi = (1 << (7 + d->f.bitdepth)) - 1;
        int i, j;

        switch (tx_size) {
        case TX_32X32: case TX_16X32: case TX_32X16:
        case TX_16X64: case TX_64X16:
            dq_denom = 2;
            break;
        case TX_64X64: case TX_32X64: case TX_64X32:
            dq_denom = 4;
            break;
        default:
            dq_denom = 1;
            break;
        }
        if (f->using_qmatrix && type < IDTX) {
            /* SegQMLevel[plane][segment_id] per 5.9.2 */
            seg_qm = f->lossless[d->segment_id] ? 15
                     : plane == 0 ? f->qm_y : plane == 1 ? f->qm_u : f->qm_v;
        }
        for (i = 0; i < th; i++) {
            for (j = 0; j < tw; j++) {
                int q = (i == 0 && j == 0) ? dc : ac;
                int q2 = q;
                int64_t dq, adq;
                int sign;

                if (seg_qm < 15) {
                    q2 = (int)round2((int64_t)q *
                                     Quantizer_Matrix[seg_qm][plane > 0]
                                     [qm_off + i * tw + j], 5);
                }
                dq = (int64_t)d->quant[i * tw + j] * q2;
                sign = dq < 0 ? -1 : 1;
                adq = sign * (int64_t)((dq < 0 ? -dq : dq) & 0xFFFFFF) /
                      dq_denom;
                d->dequant[i][j] = (int32_t)clip64(adq, lo, hi);
            }
        }
    }

    inv_tx_2d(d, tx_size, d->residual);

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int xx = flip_lr ? w - j - 1 : j;
            int yy = flip_ud ? h - i - 1 : i;
            size_t off = (size_t)(y + yy) * stride + x + xx;
            int v = p[off] + d->residual[i][j];

            p[off] = (uint16_t)clamp(v, maxv);
        }
    }
}

void
av1_predict_palette(struct av1_dec *d, int plane, int start_x, int start_y,
                    int x, int y, int tx_sz)
{
    const uint8_t *map;
    const uint16_t *colors;
    int w = Tx_Width[tx_sz];
    int h = Tx_Height[tx_sz];

    if (plane == 0) {
        map = &d->color_map_y[0][0];
        colors = d->palette_colors_y;
    } else if (plane == 1) {
        map = &d->color_map_uv[0][0];
        colors = d->palette_colors_u;
    } else {
        map = &d->color_map_uv[0][0];
        colors = d->palette_colors_v;
    }

    for (int i = 0; i < h; i++) {
        int py = start_y + i;

        for (int j = 0; j < w; j++) {
            int px = start_x + j;

            d->plane[plane][(size_t)py * d->stride[plane] + px] =
                colors[map[(y * 4 + i) * 64 + (x * 4 + j)]];
        }
    }
}
