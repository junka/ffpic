/*
 * AV1 still picture decoder - in-loop filters.
 *
 * Implements the loop filter (deblocking), CDEF, loop restoration and
 * superres upscaling processes of sections 7.14, 7.15, 7.16 and the
 * upscaling part of the frame reconstruction pipeline of the AV1 spec,
 * following the normative pseudo code.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "av1_internal.h"
#include "utils.h"
#include "vlog.h"

#define SR_SCALE_BITS 14
#define SR_SCALE_MASK ((1 << SR_SCALE_BITS) - 1)
#define SR_EXTRA_BITS 8
#define SR_FILTER_BITS 7

#define SGR_PRJ_BITS 7
#define SGR_RST_BITS 4
#define SGR_MTABLE_BITS 20
#define SGR_RECIP_BITS 12
#define SGR_SGR_BITS 8

/* Defined by av1.c via VLOG_REGISTER(av1, DEBUG). */
extern int vlog_av1;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static inline int
imin(int a, int b)
{
    return a < b ? a : b;
}

static inline int
clip3i(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline int
clip1i(int v, int bd)
{
    int m = (1 << bd) - 1;
    return v < 0 ? 0 : v > m ? m : v;
}

/* Spec Round2(value, n): n must be >= 1. */
static inline int
round2i(int v, int n)
{
    return (v + (1 << (n - 1))) >> n;
}

static inline int64_t
round2_64(int64_t v, int n)
{
    return (v + ((int64_t)1 << (n - 1))) >> n;
}

/* Round2 by a power of two shift, s may be 0. */
static inline int
r2s(int v, int s)
{
    return s ? (v + (1 << (s - 1))) >> s : v;
}

static int
floor_log2_u(uint32_t v)
{
    int n = 0;
    while ((v >>= 1) != 0) {
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Loop filter (spec 7.14)                                             */
/* ------------------------------------------------------------------ */

static void
lf_narrow(uint16_t *p, int stride, int x, int y, int dx, int dy, int hev,
          int bd)
{
    int sh = bd - 8;
    int base = 0x80 << sh;
    int lo = -(1 << (bd - 1));
    int hi = (1 << (bd - 1)) - 1;
    int ps1 = p[(size_t)(y - 2 * dy) * stride + x - 2 * dx] - base;
    int ps0 = p[(size_t)(y - dy) * stride + x - dx] - base;
    int qs0 = p[(size_t)y * stride + x] - base;
    int qs1 = p[(size_t)(y + dy) * stride + x + dx] - base;
    int filter, filter1, filter2;

    filter = hev ? clip3i(ps1 - qs1, lo, hi) : 0;
    filter = clip3i(filter + 3 * (qs0 - ps0), lo, hi);
    filter1 = clip3i(filter + 4, lo, hi) >> 3;
    filter2 = clip3i(filter + 3, lo, hi) >> 3;
    p[(size_t)y * stride + x] =
        (uint16_t)(clip3i(qs0 - filter1, lo, hi) + base);
    p[(size_t)(y - dy) * stride + x - dx] =
        (uint16_t)(clip3i(ps0 + filter2, lo, hi) + base);
    if (!hev) {
        int fr = round2i(filter1, 1);
        p[(size_t)(y + dy) * stride + x + dx] =
            (uint16_t)(clip3i(qs1 - fr, lo, hi) + base);
        p[(size_t)(y - 2 * dy) * stride + x - 2 * dx] =
            (uint16_t)(clip3i(ps1 + fr, lo, hi) + base);
    }
}

static void
lf_wide(uint16_t *p, int stride, int x, int y, int dx, int dy, int log2size,
        int plane)
{
    int n = (log2size == 4) ? 6 : (plane == 0) ? 3 : 2;
    int n2 = (log2size == 3 && plane == 0) ? 0 : 1;
    int f[12];
    int i, j;

    for (i = -n; i < n; i++) {
        int t = 0;
        for (j = -n; j <= n; j++) {
            int off = clip3i(i + j, -(n + 1), n);
            int tap = (abs(j) <= n2) ? 2 : 1;
            t += p[(size_t)(y + off * dy) * stride + x + off * dx] * tap;
        }
        f[i + n] = round2i(t, log2size);
    }
    for (i = -n; i < n; i++) {
        p[(size_t)(y + i * dy) * stride + x + i * dx] = (uint16_t)f[i + n];
    }
}

/* Sample filtering process (spec 7.14.6).  The edge position guarantees
 * that all sample accesses stay inside the plane for the given size. */
static void
lf_sample(struct av1_dec *d, int plane, int x, int y, int limit, int blimit,
          int thresh, int dx, int dy, int size, int bd)
{
    uint16_t *p = d->plane[plane];
    int stride = d->stride[plane];
    int sh = bd - 8;
    int lim = limit << sh;
    int blim = blimit << sh;
    int thr = thresh << sh;
    int flat_thr = 1 << sh;
    int q0 = p[(size_t)y * stride + x];
    int q1 = p[(size_t)(y + dy) * stride + x + dx];
    int q2 = p[(size_t)(y + 2 * dy) * stride + x + 2 * dx];
    int q3 = p[(size_t)(y + 3 * dy) * stride + x + 3 * dx];
    int p0 = p[(size_t)(y - dy) * stride + x - dx];
    int p1 = p[(size_t)(y - 2 * dy) * stride + x - 2 * dx];
    int p2 = p[(size_t)(y - 3 * dy) * stride + x - 3 * dx];
    int p3 = p[(size_t)(y - 4 * dy) * stride + x - 4 * dx];
    int len = (size == 4) ? 4 : (plane != 0) ? 6 : (size == 8) ? 8 : 16;
    int mask = 0;
    int flat_mask = 1;
    int flat_mask2 = 1;
    int hev;

    mask |= abs(p1 - p0) > lim;
    mask |= abs(q1 - q0) > lim;
    mask |= 2 * abs(p0 - q0) + abs(p1 - q1) / 2 > blim;
    if (len >= 6) {
        mask |= abs(p2 - p1) > lim;
        mask |= abs(q2 - q1) > lim;
    }
    if (len >= 8) {
        mask |= abs(p3 - p2) > lim;
        mask |= abs(q3 - q2) > lim;
    }
    if (mask) {
        return;
    }
    hev = (abs(p1 - p0) > thr) || (abs(q1 - q0) > thr);

    if (size >= 8) {
        int m = 0;
        m |= abs(p1 - p0) > flat_thr;
        m |= abs(q1 - q0) > flat_thr;
        m |= abs(p2 - p0) > flat_thr;
        m |= abs(q2 - q0) > flat_thr;
        if (len >= 8) {
            m |= abs(p3 - p0) > flat_thr;
            m |= abs(q3 - q0) > flat_thr;
        }
        flat_mask = !m;
    }
    if (size >= 16) {
        int p4 = p[(size_t)(y - 5 * dy) * stride + x - 5 * dx];
        int p5 = p[(size_t)(y - 6 * dy) * stride + x - 6 * dx];
        int p6 = p[(size_t)(y - 7 * dy) * stride + x - 7 * dx];
        int q4 = p[(size_t)(y + 4 * dy) * stride + x + 4 * dx];
        int q5 = p[(size_t)(y + 5 * dy) * stride + x + 5 * dx];
        int q6 = p[(size_t)(y + 6 * dy) * stride + x + 6 * dx];
        int m = 0;
        m |= abs(p6 - p0) > flat_thr;
        m |= abs(q6 - q0) > flat_thr;
        m |= abs(p5 - p0) > flat_thr;
        m |= abs(q5 - q0) > flat_thr;
        m |= abs(p4 - p0) > flat_thr;
        m |= abs(q4 - q0) > flat_thr;
        flat_mask2 = !m;
    }

    if (size == 4 || !flat_mask) {
        lf_narrow(p, stride, x, y, dx, dy, hev, bd);
    } else if (size <= 8 || !flat_mask2) {
        lf_wide(p, stride, x, y, dx, dy, 3, plane);
    } else {
        lf_wide(p, stride, x, y, dx, dy, 4, plane);
    }
}

/* Reduce the filter size so that all taps stay inside the plane.
 * Returns 0 when not even the 4-tap filter fits. */
static int
lf_reduce_size(int size, int pos, int last, int is_chroma)
{
    int left_req, right_req;

    while (size > 0) {
        switch (size) {
        case 16:
            left_req = 7;
            right_req = 6;
            break;
        case 8:
            left_req = 4;
            right_req = 3;
            break;
        case 6:
            left_req = 3;
            right_req = 2;
            break;
        default:
            left_req = 2;
            right_req = 1;
            break;
        }
        if (pos >= left_req && pos + right_req <= last) {
            return size;
        }
        if (size == 16) {
            size = 8;
        } else if (size == 8) {
            size = is_chroma ? 6 : 4;
        } else if (size == 6) {
            size = 4;
        } else {
            size = 0;
        }
    }
    return 0;
}

static int
lf_skip_at(struct av1_dec *d, int y4, int x4)
{
    y4 = clip3i(y4, 0, d->f.mi_rows - 1);
    x4 = clip3i(x4, 0, d->f.mi_cols - 1);
    return d->skips[(size_t)y4 * d->mi_stride + x4];
}

static void
lf_edge(struct av1_dec *d, int plane, int pass, int row, int col)
{
    struct av1_frame *f = &d->f;
    int subx = plane ? f->subsampling_x : 0;
    int suby = plane ? f->subsampling_y : 0;
    int dx = pass ? 0 : 1;
    int dy = pass ? 1 : 0;
    int x = col * AV1_MI_SIZE;
    int y = row * AV1_MI_SIZE;
    int row_a, col_a, xp, yp;
    int pw = d->plane_w[plane];
    int ph = d->plane_h[plane];
    int tx_sz, prev_tx_sz, base, size, lvl, i;
    int limit, blimit, thresh, shift;

    if (pass == 0 && x == 0) {
        return;
    }
    if (pass == 1 && y == 0) {
        return;
    }
    if (x >= f->width || y >= f->height) {
        return;
    }
    row_a = row | suby;
    col_a = col | subx;
    xp = x >> subx;
    yp = y >> suby;

    {
        int sc = d->mi_stride_c[plane];
        int trow = imin(row_a >> suby, f->mi_rows);
        int tcol = imin(col_a >> subx, f->mi_cols);
        int prow = imin((row_a - (dy << suby)) >> suby, f->mi_rows);
        int pcol = imin((col_a - (dx << subx)) >> subx, f->mi_cols);
        tx_sz = d->loopfilter_tx_sizes[plane][(size_t)(trow + 1) * sc + tcol + 1];
        prev_tx_sz =
            d->loopfilter_tx_sizes[plane][(size_t)(prow + 1) * sc + pcol + 1];
    }

    if (pass == 0) {
        if (xp % Tx_Width[tx_sz] != 0) {
            return;
        }
    } else {
        if (yp % Tx_Height[tx_sz] != 0) {
            return;
        }
    }

    base = (pass == 0) ? imin(Tx_Width[prev_tx_sz], Tx_Width[tx_sz])
                       : imin(Tx_Height[prev_tx_sz], Tx_Height[tx_sz]);
    size = (plane == 0) ? imin(16, base) : imin(8, base);
    size = lf_reduce_size(size, pass == 0 ? xp : yp,
                          (pass == 0 ? pw : ph) - 1, plane != 0);
    if (size == 0) {
        return;
    }

    /* Adaptive filter strength (spec 7.14.4/7.14.5).  delta_lfs holds the
     * parsed per-block delta only; the base level comes from the frame
     * header.  Segment SEG_LVL_ALT_LF and ref/mode deltas are not handled
     * here (intra-only streams, loop_filter_delta_enabled = 0). */
    i = (plane == 0) ? pass : plane + 1;
    {
        int r = imin(row_a, f->mi_rows - 1);
        int c = imin(col_a, f->mi_cols - 1);
        lvl = clip3i(d->delta_lfs[((size_t)r * d->mi_stride + c) * 4 + i] +
                     f->loop_filter_level[i], 0, MAX_LOOP_FILTER);
        if (lvl == 0) {
            int pr = clip3i(row_a - (dy << suby), 0, f->mi_rows - 1);
            int pc = clip3i(col_a - (dx << subx), 0, f->mi_cols - 1);
            lvl = clip3i(d->delta_lfs[((size_t)pr * d->mi_stride + pc) * 4 + i] +
                         f->loop_filter_level[i], 0, MAX_LOOP_FILTER);
        }
        if (f->loop_filter_delta_enabled) {
            /* spec 7.14.5: all blocks here are intra, so only the
             * INTRA_FRAME ref delta applies. */
            int nshift = lvl >> 5;
            lvl = clip3i(lvl + (f->loop_filter_ref_deltas[0] << nshift),
                         0, MAX_LOOP_FILTER);
        }
    }
    if (lvl <= 0) {
        return;
    }

    shift = (f->loop_filter_sharpness > 4) ? 2
            : (f->loop_filter_sharpness > 0) ? 1 : 0;
    if (f->loop_filter_sharpness > 0) {
        limit = clip3i(lvl >> shift, 1, 9 - f->loop_filter_sharpness);
    } else {
        limit = (lvl >> shift) > 1 ? (lvl >> shift) : 1;
    }
    blimit = 2 * (lvl + 2) + limit;
    thresh = lvl >> 4;

    for (i = 0; i < AV1_MI_SIZE; i++) {
        int sx = xp + dy * i;
        int sy = yp + dx * i;
        if (sx >= pw || sy >= ph) {
            continue;
        }
        lf_sample(d, plane, sx, sy, limit, blimit, thresh, dx, dy, size,
                  f->bitdepth);
    }
}

void
av1_loop_filter_frame(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int plane, pass, row, col;

    VDBG(av1, "loop filter %dx%d planes %d", f->width, f->height,
         f->num_planes);
    for (plane = 0; plane < f->num_planes; plane++) {
        int subx = plane ? f->subsampling_x : 0;
        int suby = plane ? f->subsampling_y : 0;
        if (plane > 0 && f->loop_filter_level[1 + plane] == 0) {
            continue;
        }
        for (pass = 0; pass < 2; pass++) {
            int rstep = plane ? (1 << suby) : 1;
            int cstep = plane ? (1 << subx) : 1;
            for (row = 0; row < f->mi_rows; row += rstep) {
                for (col = 0; col < f->mi_cols; col += cstep) {
                    lf_edge(d, plane, pass, row, col);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* CDEF (spec 7.15)                                                    */
/* ------------------------------------------------------------------ */

static void
cdef_find_dir(struct av1_dec *d, int x0, int y0, int *ydir_out, int *var_out)
{
    const uint16_t *p = d->plane[0];
    int stride = d->stride[0];
    int pw = d->plane_w[0];
    int ph = d->plane_h[0];
    int sh = d->f.bitdepth - 8;
    int partial[8][15] = {{0}};
    int64_t cost[8] = {0};
    int64_t best = 0;
    int ydir = 0;
    int i, j;

    for (i = 0; i < 8; i++) {
        int yy = imin(y0 + i, ph - 1);
        for (j = 0; j < 8; j++) {
            int xx = imin(x0 + j, pw - 1);
            int v = (p[(size_t)yy * stride + xx] >> sh) - 128;
            partial[0][i + j] += v;
            partial[1][i + j / 2] += v;
            partial[2][i] += v;
            partial[3][3 + i - j / 2] += v;
            partial[4][7 + i - j] += v;
            partial[5][3 - i / 2 + j] += v;
            partial[6][j] += v;
            partial[7][i / 2 + j] += v;
        }
    }
    for (i = 0; i < 8; i++) {
        cost[2] += (int64_t)partial[2][i] * partial[2][i];
        cost[6] += (int64_t)partial[6][i] * partial[6][i];
    }
    cost[2] *= Div_Table[8];
    cost[6] *= Div_Table[8];
    for (i = 0; i < 7; i++) {
        cost[0] += ((int64_t)partial[0][i] * partial[0][i] +
                    (int64_t)partial[0][14 - i] * partial[0][14 - i]) *
                   Div_Table[i + 1];
        cost[4] += ((int64_t)partial[4][i] * partial[4][i] +
                    (int64_t)partial[4][14 - i] * partial[4][14 - i]) *
                   Div_Table[i + 1];
    }
    cost[0] += (int64_t)partial[0][7] * partial[0][7] * Div_Table[8];
    cost[4] += (int64_t)partial[4][7] * partial[4][7] * Div_Table[8];
    for (i = 1; i < 8; i += 2) {
        for (j = 0; j < 5; j++) {
            cost[i] += (int64_t)partial[i][3 + j] * partial[i][3 + j];
        }
        cost[i] *= Div_Table[8];
        for (j = 0; j < 3; j++) {
            cost[i] += ((int64_t)partial[i][j] * partial[i][j] +
                        (int64_t)partial[i][10 - j] * partial[i][10 - j]) *
                       Div_Table[2 * j + 2];
        }
    }
    for (i = 0; i < 8; i++) {
        if (cost[i] > best) {
            best = cost[i];
            ydir = i;
        }
    }
    *ydir_out = ydir;
    *var_out = (int)((best - cost[(ydir + 4) & 7]) >> 10);
}

static int
cdef_constrain(int diff, int threshold, int damping)
{
    int damping_adj, ad, sign;

    if (threshold == 0) {
        return 0;
    }
    damping_adj = damping - floor_log2_u((uint32_t)threshold);
    if (damping_adj < 0) {
        damping_adj = 0;
    }
    sign = diff < 0 ? -1 : 1;
    ad = abs(diff);
    return sign * clip3i(threshold - (ad >> damping_adj), 0, ad);
}

/* CDEF filter process (spec 7.15.3) for one 8x8 (or sub-sampled) block.
 * All reads come from src (a copy of the pre-CDEF plane, spec CurrFrame);
 * results are written to the live plane (spec CdefFrame). */
static void
cdef_filter_block(struct av1_dec *d, int plane, const uint16_t *src, int x0,
                  int y0, int w, int h, int pri_str, int sec_str, int damping,
                  int dir)
{
    uint16_t *p = d->plane[plane];
    int stride = d->stride[plane];
    int pw = d->plane_w[plane];
    int ph = d->plane_h[plane];
    int sh = d->f.bitdepth - 8;
    uint16_t buf[12][12];
    uint8_t avail[12][12];
    int pri_tap_idx = (pri_str >> sh) & 1;
    int i, j, k, sign, dir_off, by, bx;

    for (by = 0; by < h + 4; by++) {
        int yy = y0 + by - 2;
        for (bx = 0; bx < w + 4; bx++) {
            int xx = x0 + bx - 2;
            if (xx >= 0 && xx < pw && yy >= 0 && yy < ph) {
                buf[by][bx] = src[(size_t)yy * stride + xx];
                avail[by][bx] = 1;
            } else {
                avail[by][bx] = 0;
            }
        }
    }
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int yy = y0 + i;
            int xx = x0 + j;
            int v, max, min, sum, rnd;
            if (yy >= ph || xx >= pw) {
                continue;
            }
            v = buf[i + 2][j + 2];
            max = v;
            min = v;
            sum = 0;
            for (k = 0; k < 2; k++) {
                for (sign = -1; sign <= 1; sign += 2) {
                    int oy = Cdef_Directions[dir][k][0];
                    int ox = Cdef_Directions[dir][k][1];
                    int sy, sx, s;
                    sy = i + 2 + sign * oy;
                    sx = j + 2 + sign * ox;
                    if (avail[sy][sx]) {
                        s = buf[sy][sx];
                        sum += Cdef_Pri_Taps[pri_tap_idx][k] *
                               cdef_constrain(s - v, pri_str, damping);
                        if (s > max) {
                            max = s;
                        }
                        if (s < min) {
                            min = s;
                        }
                    }
                    for (dir_off = -2; dir_off <= 2; dir_off += 4) {
                        int d2 = (dir + dir_off) & 7;
                        int oy2 = Cdef_Directions[d2][k][0];
                        int ox2 = Cdef_Directions[d2][k][1];
                        int sy2 = i + 2 + sign * oy2;
                        int sx2 = j + 2 + sign * ox2;
                        if (avail[sy2][sx2]) {
                            s = buf[sy2][sx2];
                            sum += Cdef_Sec_Taps[pri_tap_idx][k] *
                                   cdef_constrain(s - v, sec_str, damping);
                            if (s > max) {
                                max = s;
                            }
                            if (s < min) {
                                min = s;
                            }
                        }
                    }
                }
            }
            rnd = (8 + sum - (sum < 0)) >> 4;
            p[(size_t)yy * stride + xx] = (uint16_t)clip3i(v + rnd, min, max);
        }
    }
}

void
av1_cdef_frame(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int sh = f->bitdepth - 8;
    int r, c, pl;
    uint16_t *src[3] = {NULL, NULL, NULL};

    if (!d->cdef_idx) {
        return;
    }
    /* Spec reads unfiltered samples (CurrFrame) and writes CdefFrame, so
     * neighbouring 8x8 blocks must not see each other's filtered output. */
    for (pl = 0; pl < f->num_planes; pl++) {
        src[pl] = malloc((size_t)d->stride[pl] * d->plane_h[pl] *
                         sizeof(uint16_t));
        if (!src[pl]) {
            int i;
            for (i = 0; i < pl; i++) {
                free(src[i]);
            }
            VERR(av1, "cdef: cannot allocate source copy");
            d->error = 1;
            return;
        }
        memcpy(src[pl], d->plane[pl],
               (size_t)d->stride[pl] * d->plane_h[pl] * sizeof(uint16_t));
    }
    VDBG(av1, "cdef %dx%d damp %d idx0 %d yp %d ys %d up %d us %d",
         f->width, f->height, f->cdef_damping,
         d->cdef_idx[0], f->cdef_y_pri_strength[d->cdef_idx[0]],
         f->cdef_y_sec_strength[d->cdef_idx[0]],
         f->cdef_uv_pri_strength[d->cdef_idx[0]],
         f->cdef_uv_sec_strength[d->cdef_idx[0]]);
    for (r = 0; r < f->mi_rows; r += 2) {
        for (c = 0; c < f->mi_cols; c += 2) {
            int base_r = r & ~15;
            int base_c = c & ~15;
            int idx = d->cdef_idx[(size_t)base_r * d->mi_stride + base_c];
            int x0 = c * AV1_MI_SIZE;
            int y0 = r * AV1_MI_SIZE;
            int skip, ydir, var, dir, pri, sec, var_str, damping;

            /* cdef_idx is int8_t: -1 (0xFF as written) skips CDEF. */
            if (idx < 0) {
                continue;
            }
            skip = lf_skip_at(d, r, c) && lf_skip_at(d, r + 1, c) &&
                   lf_skip_at(d, r, c + 1) && lf_skip_at(d, r + 1, c + 1);
            if (skip) {
                continue;
            }
            cdef_find_dir(d, x0, y0, &ydir, &var);
            pri = f->cdef_y_pri_strength[idx] << sh;
            sec = f->cdef_y_sec_strength[idx] << sh;
            dir = pri ? ydir : 0;
            var_str = (var >> 6) ? imin(floor_log2_u((uint32_t)(var >> 6)), 12)
                                 : 0;
            pri = var ? (pri * (4 + var_str) + 8) >> 4 : 0;
            damping = f->cdef_damping + sh;
            cdef_filter_block(d, 0, src[0], x0, y0, 8, 8, pri, sec, damping,
                              dir);
            if (f->num_planes > 1) {
                int subx = f->subsampling_x;
                int suby = f->subsampling_y;
                int pri_uv = f->cdef_uv_pri_strength[idx] << sh;
                int sec_uv = f->cdef_uv_sec_strength[idx] << sh;
                int dir_uv = pri_uv ? Cdef_Uv_Dir[subx][suby][ydir] : 0;
                int damp_uv = f->cdef_damping + sh - 1;
                cdef_filter_block(d, 1, src[1], x0 >> subx, y0 >> suby,
                                  8 >> subx, 8 >> suby, pri_uv, sec_uv,
                                  damp_uv, dir_uv);
                cdef_filter_block(d, 2, src[2], x0 >> subx, y0 >> suby,
                                  8 >> subx, 8 >> suby, pri_uv, sec_uv,
                                  damp_uv, dir_uv);
            }
        }
    }
    for (pl = 0; pl < f->num_planes; pl++) {
        free(src[pl]);
    }
}

/* ------------------------------------------------------------------ */
/* Loop restoration (spec 7.17)                                        */
/* ------------------------------------------------------------------ */

/* Get source sample process (spec 7.17.6).  The pre-CDEF frame is not
 * retained by this decoder, so samples outside the current stripe are
 * taken from the CDEF output as well; coordinates are clipped to the
 * plane extent. */
static uint16_t
lr_get(const uint16_t *src, int stride, int pw, int ph, int x, int y)
{
    x = clip3i(x, 0, pw - 1);
    y = clip3i(y, 0, ph - 1);
    return src[(size_t)y * stride + x];
}

static void
lr_make_wiener(const int8_t *coeff, int *filter)
{
    int i;
    filter[3] = 128;
    for (i = 0; i < 3; i++) {
        int c = coeff[i];
        filter[i] = c;
        filter[6 - i] = c;
        filter[3] -= 2 * c;
    }
}

static void
lr_wiener_block(struct av1_dec *d, int plane, const uint16_t *src, int x,
                int y, int w, int h, const int8_t *taps, int pw, int ph)
{
    int bd = d->f.bitdepth;
    int stride = d->stride[plane];
    uint16_t *out = d->plane[plane];
    int ir0 = (bd == 12) ? 5 : 3;
    int ir1 = (bd == 12) ? 9 : 11;
    int offset = 1 << (bd + SR_FILTER_BITS - ir0 - 1);
    int limit = (1 << (bd + 1 + SR_FILTER_BITS - ir0)) - 1;
    int vfilter[7], hfilter[7];
    int32_t intermediate[16][8];
    int r, c, t;

    lr_make_wiener(taps, vfilter);
    lr_make_wiener(taps + 3, hfilter);

    for (r = 0; r < h + 6; r++) {
        for (c = 0; c < w; c++) {
            int64_t s = 0;
            int v;
            for (t = 0; t < 7; t++) {
                s += (int64_t)hfilter[t] *
                     lr_get(src, stride, pw, ph, x + c + t - 3, y + r - 3);
            }
            v = round2i((int)s, ir0);
            intermediate[r][c] = clip3i(v, -offset, limit - offset);
        }
    }
    for (r = 0; r < h; r++) {
        for (c = 0; c < w; c++) {
            int64_t s = 0;
            int v;
            for (t = 0; t < 7; t++) {
                s += (int64_t)vfilter[t] * intermediate[r + t][c];
            }
            v = round2i((int)s, ir1);
            out[(size_t)(y + r) * stride + x + c] = (uint16_t)clip1i(v, bd);
        }
    }
}

/* Box filter process (spec 7.17.3) for one 4x4 block. */
static void
lr_box_filter(struct av1_dec *d, const uint16_t *src, int plane, int x, int y,
              int w, int h, int r, int eps, int pass, int32_t out[8][8],
              int pw, int ph)
{
    int bd = d->f.bitdepth;
    int stride = d->stride[plane];
    int n = (2 * r + 1) * (2 * r + 1);
    int n2e = n * n * eps;
    int64_t s = ((1 << SGR_MTABLE_BITS) + n2e / 2) / n2e;
    int one_over_n = ((1 << SGR_RECIP_BITS) + n / 2) / n;
    int32_t a_tab[8][8];
    int32_t b_tab[8][8];
    int bi, bj, dy, dx, i, j;

    for (bi = -1; bi <= h; bi++) {
        for (bj = -1; bj <= w; bj++) {
            int64_t a = 0, b = 0, d2, pv, z, b2;
            int a2;
            for (dy = -r; dy <= r; dy++) {
                for (dx = -r; dx <= r; dx++) {
                    int c = lr_get(src, stride, pw, ph, x + bj + dx,
                                   y + bi + dy);
                    a += (int64_t)c * c;
                    b += c;
                }
            }
            a = round2_64(a, 2 * (bd - 8));
            d2 = round2_64(b, bd - 8);
            pv = a * n - d2 * d2;
            if (pv < 0) {
                pv = 0;
            }
            z = round2_64(pv * s, SGR_MTABLE_BITS);
            if (z >= 255) {
                a2 = 256;
            } else if (z == 0) {
                a2 = 1;
            } else {
                a2 = (int)(((z << SGR_SGR_BITS) + z / 2) / (z + 1));
            }
            b2 = (int64_t)((1 << SGR_SGR_BITS) - a2) * b * one_over_n;
            a_tab[bi + 1][bj + 1] = a2;
            b_tab[bi + 1][bj + 1] = (int32_t)round2_64(b2, SGR_RECIP_BITS);
        }
    }
    for (i = 0; i < h; i++) {
        int shift = (pass == 0 && (i & 1)) ? 4 : 5;
        for (j = 0; j < w; j++) {
            int64_t a = 0, b = 0, v;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int weight;
                    if (pass == 0) {
                        weight = ((i + dy) & 1) ? ((dx == 0) ? 6 : 5) : 0;
                    } else {
                        weight = (dx == 0 || dy == 0) ? 4 : 3;
                    }
                    a += (int64_t)weight * a_tab[i + dy + 1][j + dx + 1];
                    b += (int64_t)weight * b_tab[i + dy + 1][j + dx + 1];
                }
            }
            v = a * lr_get(src, stride, pw, ph, x + j, y + i) + b;
            out[i][j] = (int32_t)round2_64(v, SGR_SGR_BITS + shift -
                                                SGR_RST_BITS);
        }
    }
}

/* Self guided filter process (spec 7.17.2) for one 4x4 block. */
static void
lr_sgr_block(struct av1_dec *d, int plane, const uint16_t *src, int x, int y,
             int w, int h, int set, int w0, int w1, int pw, int ph)
{
    int bd = d->f.bitdepth;
    int stride = d->stride[plane];
    uint16_t *out = d->plane[plane];
    const int16_t *params = Sgr_Params[set];
    int r0 = params[0];
    int r1 = params[2];
    int w2 = (1 << SGR_PRJ_BITS) - w0 - w1;
    int32_t flt0[8][8] = {{0}};
    int32_t flt1[8][8] = {{0}};
    int i, j;

    if (r0) {
        lr_box_filter(d, src, plane, x, y, w, h, r0, params[1], 0, flt0, pw,
                      ph);
    }
    if (r1) {
        lr_box_filter(d, src, plane, x, y, w, h, r1, params[3], 1, flt1, pw,
                      ph);
    }
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            int64_t u = (int64_t)lr_get(src, stride, pw, ph, x + j, y + i)
                        << SGR_RST_BITS;
            int64_t v = (int64_t)w1 * u;
            int s;
            v += (int64_t)w0 * (r0 ? flt0[i][j] : (int32_t)u);
            v += (int64_t)w2 * (r1 ? flt1[i][j] : (int32_t)u);
            s = (int)round2_64(v, SGR_RST_BITS + SGR_PRJ_BITS);
            out[(size_t)(y + i) * stride + x + j] = (uint16_t)clip1i(s, bd);
        }
    }
}

void
av1_loop_restoration_frame(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int p;

    if (!f->uses_lr) {
        return;
    }
    VDBG(av1, "loop restoration");
    for (p = 0; p < f->num_planes; p++) {
        int type = f->restoration_type[p];
        int unit_size = f->lr_unit_size[p];
        int pw = d->plane_w[p];
        int ph = d->plane_h[p];
        int stride = d->stride[p];
        int subx = p ? f->subsampling_x : 0;
        int suby = p ? f->subsampling_y : 0;
        int step_x = AV1_MI_SIZE >> subx;
        int step_y = AV1_MI_SIZE >> suby;
        uint16_t *src;
        int x, y;

        if (type == RESTORE_NONE || unit_size <= 0 ||
            d->lr_unit_rows[p] <= 0 || d->lr_unit_cols[p] <= 0 ||
            step_x <= 0 || step_y <= 0) {
            continue;
        }
        src = malloc((size_t)stride * ph * sizeof(uint16_t));
        if (!src) {
            VERR(av1, "loop restoration: cannot allocate source copy");
            d->error = 1;
            return;
        }
        for (y = 0; y < ph; y++) {
            memcpy(src + (size_t)y * stride, d->plane[p] + (size_t)y * stride,
                   (size_t)pw * sizeof(uint16_t));
        }
        for (y = 0; y < ph; y += step_y) {
            for (x = 0; x < pw; x += step_x) {
                int yl = y << suby;
                int w = imin(step_x, pw - x);
                int h = imin(step_y, ph - y);
                int unit_row = imin(d->lr_unit_rows[p] - 1,
                                    ((yl + 8) >> suby) / unit_size);
                int unit_col = imin(d->lr_unit_cols[p] - 1, x / unit_size);
                int unit = unit_row * d->lr_unit_stride + unit_col;
                int rtype = type;

                if (type == RESTORE_SWITCHABLE) {
                    rtype = (d->lr_sgrproj[p][unit * 3 + 0] == 25)
                                ? RESTORE_WIENER
                                : RESTORE_SGRPROJ;
                }
                if (rtype == RESTORE_WIENER) {
                    if (d->lr_wiener[p]) {
                        lr_wiener_block(d, p, src, x, y, w, h,
                                        d->lr_wiener[p] + unit * 6, pw, ph);
                    }
                } else if (rtype == RESTORE_SGRPROJ) {
                    if (d->lr_sgrproj[p]) {
                        lr_sgr_block(d, p, src, x, y, w, h,
                                     d->lr_sgrproj[p][unit * 3 + 0],
                                     d->lr_sgrproj[p][unit * 3 + 1],
                                     d->lr_sgrproj[p][unit * 3 + 2], pw, ph);
                    }
                }
            }
        }
        free(src);
    }
}

/* ------------------------------------------------------------------ */
/* Superres upscaling (spec 7.16 upscaling process)                    */
/* ------------------------------------------------------------------ */

void
av1_superres_frame(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    int p;

    if (!f->use_superres) {
        return;
    }
    VDBG(av1, "superres %d -> %d", f->width, f->upscaled_width);
    for (p = 0; p < f->num_planes; p++) {
        int subx = p ? f->subsampling_x : 0;
        int down_w = r2s(f->width, subx);
        int up_w = r2s(f->upscaled_width, subx);
        int ph = d->plane_h[p];
        int pw = d->plane_w[p];
        int mi_w = f->mi_cols >> subx;
        int max_x = imin(mi_w * AV1_MI_SIZE - 1, pw - 1);
        int step_x, err, initial_subpel_x, y;
        uint16_t *tmp;

        if (up_w <= down_w) {
            continue;
        }
        tmp = malloc((size_t)up_w * sizeof(uint16_t));
        if (!tmp) {
            VERR(av1, "superres: cannot allocate row buffer");
            d->error = 1;
            return;
        }
        step_x = ((down_w << SR_SCALE_BITS) + up_w / 2) / up_w;
        err = up_w * step_x - (down_w << SR_SCALE_BITS);
        initial_subpel_x =
            ((-((up_w - down_w) << (SR_SCALE_BITS - 1)) + up_w / 2) / up_w +
             (1 << (SR_EXTRA_BITS - 1)) - err / 2) &
            SR_SCALE_MASK;
        for (y = 0; y < ph; y++) {
            uint16_t *row = d->plane[p] + (size_t)y * d->stride[p];
            int x, k;
            for (x = 0; x < up_w; x++) {
                int src_x = -(1 << SR_SCALE_BITS) + initial_subpel_x +
                            x * step_x;
                int src_x_px = src_x >> SR_SCALE_BITS;
                int subpel = (src_x & SR_SCALE_MASK) >> SR_EXTRA_BITS;
                int sum = 0;
                for (k = 0; k < 8; k++) {
                    int sx = clip3i(src_x_px + k - 3, 0, max_x);
                    sum += row[sx] * Upscale_Filter[subpel][k];
                }
                tmp[x] = (uint16_t)clip1i(round2i(sum, SR_FILTER_BITS),
                                          f->bitdepth);
            }
            memcpy(row, tmp, (size_t)up_w * sizeof(uint16_t));
        }
        free(tmp);
        d->plane_w[p] = up_w;
    }
}
