/* Film grain synthesis, spec section 7.18.3.  A still picture is a single
 * intra frame, so the parameters are always transmitted in the frame header
 * and there are no reference slots to load them from. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "av1_internal.h"
#include "av1_tables.h"

#define GRAIN_W 82
#define GRAIN_H 73

struct grain_ctx {
    const struct av1_frame *f;
    int bitdepth;
    int w, h;
    int subx, suby;
    int num_planes;
    int grain_min;
    int grain_max;
    uint16_t rng;
    int16_t luma[GRAIN_H][GRAIN_W];
    int16_t cb[GRAIN_H][GRAIN_W];
    int16_t cr[GRAIN_H][GRAIN_W];
    uint8_t scaling[3][256];
};

static int
clip3(int lo, int hi, int v)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Spec Round2(x, n). */
static int
round2(int x, int n)
{
    return n > 0 ? (x + (1 << (n - 1))) >> n : x;
}

/* Spec 7.18.3.2. */
static int
grain_random(struct grain_ctx *g, int bits)
{
    uint32_t r = g->rng;
    int bit = ((r >> 0) ^ (r >> 1) ^ (r >> 3) ^ (r >> 12)) & 1;

    r = (r >> 1) | ((uint32_t)bit << 15);
    g->rng = (uint16_t)r;
    return (int)((r >> (16 - bits)) & ((1u << bits) - 1));
}

/* Spec 7.18.3.3. */
static void
generate_grain(struct grain_ctx *g)
{
    const struct av1_frame *f = g->f;
    int shift = 12 - g->bitdepth + f->grain_scale_shift;
    int chroma_w = g->subx ? 44 : 82;
    int chroma_h = g->suby ? 38 : 73;
    int ar_shift = f->ar_coeff_shift_minus_6 + 6;
    int lag = f->ar_coeff_lag;

    memset(g->luma, 0, sizeof(g->luma));
    memset(g->cb, 0, sizeof(g->cb));
    memset(g->cr, 0, sizeof(g->cr));

    g->rng = (uint16_t)f->grain_seed;
    for (int y = 0; y < 73; y++) {
        for (int x = 0; x < 82; x++) {
            int v = f->num_y_points ? Gaussian_Sequence[grain_random(g, 11)]
                                    : 0;
            g->luma[y][x] = (int16_t)round2(v, shift);
        }
    }

    for (int y = 3; y < 73; y++) {
        for (int x = 3; x < 82 - 3; x++) {
            int s = 0;
            int pos = 0;
            for (int dr = -lag; dr <= 0; dr++) {
                for (int dc = -lag; dc <= lag; dc++) {
                    if (dr == 0 && dc == 0) {
                        break;
                    }
                    s += g->luma[y + dr][x + dc] * f->ar_coeffs_y[pos++];
                }
            }
            g->luma[y][x] = (int16_t)clip3(
                g->grain_min, g->grain_max,
                g->luma[y][x] + round2(s, ar_shift));
        }
    }

    if (g->num_planes == 1) {
        return;
    }

    g->rng = (uint16_t)(f->grain_seed ^ 0xb524);
    for (int y = 0; y < chroma_h; y++) {
        for (int x = 0; x < chroma_w; x++) {
            int v = (f->num_cb_points || f->chroma_scaling_from_luma)
                        ? Gaussian_Sequence[grain_random(g, 11)]
                        : 0;
            g->cb[y][x] = (int16_t)round2(v, shift);
        }
    }
    g->rng = (uint16_t)(f->grain_seed ^ 0x49d8);
    for (int y = 0; y < chroma_h; y++) {
        for (int x = 0; x < chroma_w; x++) {
            int v = (f->num_cr_points || f->chroma_scaling_from_luma)
                        ? Gaussian_Sequence[grain_random(g, 11)]
                        : 0;
            g->cr[y][x] = (int16_t)round2(v, shift);
        }
    }

    for (int y = 3; y < chroma_h; y++) {
        for (int x = 3; x < chroma_w - 3; x++) {
            int s0 = 0, s1 = 0;
            int pos = 0;
            for (int dr = -lag; dr <= 0; dr++) {
                for (int dc = -lag; dc <= lag; dc++) {
                    if (dr == 0 && dc == 0) {
                        if (f->num_y_points) {
                            int luma = 0;
                            int luma_x = ((x - 3) << g->subx) + 3;
                            int luma_y = ((y - 3) << g->suby) + 3;
                            for (int i = 0; i <= g->suby; i++) {
                                for (int j = 0; j <= g->subx; j++) {
                                    luma += g->luma[luma_y + i][luma_x + j];
                                }
                            }
                            luma = round2(luma, g->subx + g->suby);
                            s0 += luma * f->ar_coeffs_cb[pos];
                            s1 += luma * f->ar_coeffs_cr[pos];
                        }
                        break;
                    }
                    s0 += g->cb[y + dr][x + dc] * f->ar_coeffs_cb[pos];
                    s1 += g->cr[y + dr][x + dc] * f->ar_coeffs_cr[pos];
                    pos++;
                }
            }
            g->cb[y][x] = (int16_t)clip3(
                g->grain_min, g->grain_max,
                g->cb[y][x] + round2(s0, ar_shift));
            g->cr[y][x] = (int16_t)clip3(
                g->grain_min, g->grain_max,
                g->cr[y][x] + round2(s1, ar_shift));
        }
    }
}

/* Spec 7.18.3.4. */
static void
build_scaling(struct grain_ctx *g)
{
    const struct av1_frame *f = g->f;

    for (int plane = 0; plane < g->num_planes; plane++) {
        const uint8_t *values;
        const uint8_t *scales;
        int num = 0;

        if (plane == 0 || f->chroma_scaling_from_luma) {
            num = f->num_y_points;
            values = f->point_y_value;
            scales = f->point_y_scaling;
        } else if (plane == 1) {
            num = f->num_cb_points;
            values = f->point_cb_value;
            scales = f->point_cb_scaling;
        } else {
            num = f->num_cr_points;
            values = f->point_cr_value;
            scales = f->point_cr_scaling;
        }

        if (num == 0) {
            memset(g->scaling[plane], 0, 256);
            continue;
        }
        for (int x = 0; x < values[0]; x++) {
            g->scaling[plane][x] = scales[0];
        }
        for (int i = 0; i < num - 1; i++) {
            int delta_y = scales[i + 1] - scales[i];
            int delta_x = values[i + 1] - values[i];
            int delta = delta_y * ((65536 + (delta_x >> 1)) / delta_x);
            for (int x = 0; x < delta_x; x++) {
                g->scaling[plane][values[i] + x] =
                    (uint8_t)(scales[i] + ((x * delta + 32768) >> 16));
            }
        }
        for (int x = values[num - 1]; x < 256; x++) {
            g->scaling[plane][x] = scales[num - 1];
        }
    }
}

static int
scale_lut(const struct grain_ctx *g, int plane, int index)
{
    int shift = g->bitdepth - 8;
    int x = index >> shift;
    int rem = index - (x << shift);

    if (x < 0) {
        x = 0;
        rem = 0;
    } else if (x > 255) {
        x = 255;
        rem = 0;
    }
    if (g->bitdepth == 8 || x == 255) {
        return g->scaling[plane][x];
    }
    {
        int start = g->scaling[plane][x];
        int end = g->scaling[plane][x + 1];
        return start + round2((end - start) * rem, shift);
    }
}

/* One 32 luma sample high stripe of noise, spec 7.18.3.5.  stripes[0] is
 * the luma plane, stripes[1] and [2] the chroma planes. */
struct grain_stripe {
    int16_t *luma;
    int16_t *cb;
    int16_t *cr;
    int luma_stride;
    int chroma_stride;
};

static void
build_stripe(struct grain_ctx *g, struct grain_stripe *s, int luma_num)
{
    const struct av1_frame *f = g->f;
    int y_limit = (g->h + 1) / 2;
    int x_limit = (g->w + 1) / 2;
    int y = luma_num * 16;

    if (y >= y_limit) {
        return;
    }

    /* spec: RandomRegister = grain_seed ^ ... per stripe */
    g->rng = (uint16_t)(f->grain_seed ^
                        (((luma_num * 37 + 178) & 255) << 8) ^
                        ((luma_num * 173 + 105) & 255));

    for (int x_blk = 0; x_blk < x_limit; x_blk += 16) {
        int rand = grain_random(g, 8);
        int offset_x = rand >> 4;
        int offset_y = rand & 15;

        for (int plane = 0; plane < g->num_planes; plane++) {
            int subx = plane > 0 ? g->subx : 0;
            int suby = plane > 0 ? g->suby : 0;
            int plane_off_x = subx ? 6 + offset_x : 9 + offset_x * 2;
            int plane_off_y = suby ? 6 + offset_y : 9 + offset_y * 2;
            int rows = 34 >> suby;
            int16_t *stripe = plane == 0 ? s->luma
                             : plane == 1 ? s->cb
                                          : s->cr;
            int stride = plane == 0 ? s->luma_stride : s->chroma_stride;
            int16_t (*src)[GRAIN_W] =
                plane == 0 ? g->luma : plane == 1 ? g->cb : g->cr;

            for (int i = 0; i < rows; i++) {
                for (int j = 0; j < (34 >> subx); j++) {
                    int v = src[plane_off_y + i][plane_off_x + j];
                    int idx = subx ? x_blk + j : x_blk * 2 + j;

                    if (subx == 0) {
                        if (j < 2 && f->overlap_flag && x_blk > 0) {
                            int old = stripe[i * stride + idx];
                            v = j == 0 ? old * 27 + v * 17
                                       : old * 17 + v * 27;
                            v = clip3(g->grain_min, g->grain_max,
                                      round2(v, 5));
                        }
                    } else {
                        if (j == 0 && f->overlap_flag && x_blk > 0) {
                            int old = stripe[i * stride + idx];
                            v = old * 23 + v * 22;
                            v = clip3(g->grain_min, g->grain_max,
                                      round2(v, 5));
                        }
                    }
                    stripe[i * stride + idx] = (int16_t)v;
                }
            }
        }
    }
}

void
av1_apply_film_grain(struct av1_dec *d)
{
    struct av1_frame *f = &d->f;
    struct grain_ctx g;
    struct grain_stripe cur, prev;
    int bitdepth = f->bitdepth;
    int w = f->upscaled_width;
    int h = f->height;
    int subx = f->subsampling_x;
    int suby = f->subsampling_y;
    int num_planes = f->num_planes;
    int chroma_w = (w + subx) >> subx;
    int chroma_h = (h + suby) >> suby;
    int min_value, max_luma, max_chroma;
    int scaling_shift = f->grain_scaling_minus_8 + 8;
    int stripes;
    int shift = bitdepth - 8;

    if (!f->apply_grain || w <= 0 || h <= 0) {
        return;
    }

    memset(&g, 0, sizeof(g));
    g.f = f;
    g.bitdepth = bitdepth;
    g.w = w;
    g.h = h;
    g.subx = subx;
    g.suby = suby;
    g.num_planes = num_planes;
    g.grain_min = -(128 << shift);
    g.grain_max = (256 << shift) - 1 - (128 << shift);

    generate_grain(&g);
    build_scaling(&g);

    if (f->clip_to_restricted_range) {
        min_value = 16 << shift;
        max_luma = 235 << shift;
        max_chroma = f->matrix_coefficients == 0 ? max_luma : 240 << shift;
    } else {
        min_value = 0;
        max_luma = (256 << shift) - 1;
        max_chroma = max_luma;
    }

    /* a chroma block write starts at x (not x*2) when subsampled
     * horizontally, but 4:4:4 uses the luma spacing, so size for that */
    cur.luma_stride = w + 34;
    cur.chroma_stride = w + 34;
    cur.luma = malloc(sizeof(int16_t) * cur.luma_stride * 34);
    cur.cb = malloc(sizeof(int16_t) * cur.chroma_stride * 34);
    cur.cr = malloc(sizeof(int16_t) * cur.chroma_stride * 34);
    prev.luma_stride = cur.luma_stride;
    prev.chroma_stride = cur.chroma_stride;
    prev.luma = malloc(sizeof(int16_t) * cur.luma_stride * 34);
    prev.cb = malloc(sizeof(int16_t) * cur.chroma_stride * 34);
    prev.cr = malloc(sizeof(int16_t) * cur.chroma_stride * 34);
    if (!cur.luma || !cur.cb || !cur.cr ||
        !prev.luma || !prev.cb || !prev.cr) {
        goto out;
    }

    stripes = (((h + 1) / 2) + 15) / 16;
    for (int n = 0; n < stripes; n++) {
        int luma_y0 = n * 32;
        int chroma_y0 = n * (32 >> suby);
        int luma_rows = h - luma_y0;
        int chroma_rows = chroma_h - chroma_y0;
        if (luma_rows > 32) {
            luma_rows = 32;
        }
        if (chroma_rows > (32 >> suby)) {
            chroma_rows = 32 >> suby;
        }
        if (luma_rows <= 0) {
            break;
        }

        build_stripe(&g, &cur, n);

        /* Spec order: chroma first, using the unmodified luma samples. */
        for (int plane = 1; plane < num_planes; plane++) {
            uint16_t *dst = d->plane[plane];
            int stride = d->stride[plane];
            int16_t *stripe = plane == 1 ? cur.cb : cur.cr;
            int16_t *old = plane == 1 ? prev.cb : prev.cr;
            int cstride = cur.chroma_stride;
            int mult = plane == 1 ? f->cb_mult : f->cr_mult;
            int luma_mult = plane == 1 ? f->cb_luma_mult : f->cr_luma_mult;
            int offset = plane == 1 ? f->cb_offset : f->cr_offset;

            if (!(plane == 1 ? f->num_cb_points : f->num_cr_points) &&
                !f->chroma_scaling_from_luma) {
                continue;
            }
            for (int i = 0; i < chroma_rows; i++) {
                int y = chroma_y0 + i;
                uint16_t *row = dst + (size_t)y * stride;
                for (int x = 0; x < chroma_w; x++) {
                    int noise = stripe[i * cstride + x];
                    int orig = row[x];
                    int luma_x = x << subx;
                    int luma_y = y << suby;
                    int next_x = luma_x + 1 < w ? luma_x + 1 : w - 1;
                    int avg_luma;
                    int merged;

                    if (n > 0 && f->overlap_flag) {
                        int o;
                        if (suby == 0) {
                            if (i < 2) {
                                o = old[(i + 32) * cstride + x];
                                noise = i == 0 ? o * 27 + noise * 17
                                               : o * 17 + noise * 27;
                                noise = clip3(g.grain_min, g.grain_max,
                                              round2(noise, 5));
                            }
                        } else if (i == 0) {
                            o = old[16 * cstride + x];
                            noise = clip3(g.grain_min, g.grain_max,
                                          round2(o * 23 + noise * 22, 5));
                        }
                    }
                    if (subx) {
                        avg_luma = round2(d->plane[0][(size_t)luma_y *
                                                     d->stride[0] + luma_x] +
                                          d->plane[0][(size_t)luma_y *
                                                     d->stride[0] + next_x],
                                          1);
                    } else {
                        avg_luma = d->plane[0][(size_t)luma_y *
                                               d->stride[0] + luma_x];
                    }
                    if (f->chroma_scaling_from_luma) {
                        merged = avg_luma;
                    } else {
                        int combined = avg_luma * (luma_mult - 128) +
                                       orig * (mult - 128);
                        merged = clip3(0, (1 << bitdepth) - 1,
                                       (combined >> 6) +
                                           ((offset - 256) << shift));
                    }
                    noise = round2(scale_lut(&g, plane, merged) * noise,
                                   scaling_shift);
                    row[x] = (uint16_t)clip3(min_value, max_chroma,
                                             orig + noise);
                }
            }
        }

        /* Luma last, so chroma averaged the grain-free luma. */
        if (f->num_y_points) {
            for (int i = 0; i < luma_rows; i++) {
                int y = luma_y0 + i;
                uint16_t *row = d->plane[0] + (size_t)y * d->stride[0];
                for (int x = 0; x < w; x++) {
                    int noise = cur.luma[i * cur.luma_stride + x];
                    int orig = row[x];

                    if (i < 2 && n > 0 && f->overlap_flag) {
                        int o = prev.luma[(i + 32) * cur.luma_stride + x];
                        noise = i == 0 ? o * 27 + noise * 17
                                       : o * 17 + noise * 27;
                        noise = clip3(g.grain_min, g.grain_max,
                                      round2(noise, 5));
                    }
                    noise = round2(scale_lut(&g, 0, orig) * noise,
                                   scaling_shift);
                    row[x] = (uint16_t)clip3(min_value, max_luma,
                                             orig + noise);
                }
            }
        }

        {
            struct grain_stripe swap = prev;
            prev = cur;
            cur = swap;
        }
    }

out:
    free(cur.luma);
    free(cur.cb);
    free(cur.cr);
    free(prev.luma);
    free(prev.cb);
    free(prev.cr);
}
