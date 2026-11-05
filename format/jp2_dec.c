/*
 * Built in JPEG 2000 (ISO/IEC 15444-1) reversible decoder.
 *
 * Only the lossless path used by the sample files is implemented: one tile
 * part per tile, LRCP, a single quality layer, no SOP/EPH, no code-block
 * style flags, reversible 5-3 wavelet and the reversible component
 * transform.  Anything outside that shape is refused with -ENOTSUP and the
 * caller keeps the header only pic.
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorspace.h"
#include "file.h"
#include "jp2.h"
#include "mq.h"

#define JP2_MAX_DIM    (1 << 20)
#define JP2_MAX_LEVELS 32

/* ------------------------------------------------------------------ */
/* packet header bit reader (ISO 15444-1 Annex B / OpenJPEG bio.c)      */
/* ------------------------------------------------------------------ */

struct dec_bio {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    uint32_t b;
    int ct;
};

static void
bio_init(struct dec_bio *b, const uint8_t *buf, size_t len)
{
    b->buf = buf;
    b->len = len;
    b->pos = 0;
    b->b = 0;
    b->ct = 0;
}

/* BYTEIN.  A virtual 0xFF 0xFF marker follows the buffer, so running past
 * the end leaves the low byte zero and never advances pos. */
static void
bio_bytein(struct dec_bio *b)
{
    b->b = (b->b << 8) & 0xffff;
    b->ct = (b->b == 0xff00) ? 7 : 8;
    if (b->pos < b->len) {
        b->b |= b->buf[b->pos++];
    }
}

static int
bio_getbit(struct dec_bio *b)
{
    if (b->ct == 0) {
        bio_bytein(b);
    }
    b->ct--;
    return (int)((b->b >> b->ct) & 1);
}

static uint32_t
bio_read(struct dec_bio *b, int n)
{
    uint32_t v = 0;

    while (n-- > 0) {
        v = (v << 1) | (uint32_t)bio_getbit(b);
    }
    return v;
}

/* INALIGN: only the bytes actually consumed are counted */
static size_t
bio_inalign(struct dec_bio *b)
{
    if ((b->b & 0xff) == 0xff) {
        bio_bytein(b);
    }
    b->ct = 0;
    return b->pos;
}

/* ------------------------------------------------------------------ */
/* tag tree (ISO 15444-1 B.10.2, same layout as OpenJPEG tgt.c)         */
/* ------------------------------------------------------------------ */

struct tt_node {
    int parent;
    int value;
    int low;
};

struct tt {
    int nodes;
    struct tt_node *n;
};

static int
tt_init(struct tt *t, int w, int h)
{
    int lw[JP2_MAX_LEVELS];
    int lh[JP2_MAX_LEVELS];
    int off[JP2_MAX_LEVELS];
    int level = 0;
    int total = 0;
    int l, x, y;

    t->nodes = 0;
    t->n = NULL;
    if (w <= 0 || h <= 0) {
        return -EINVAL;
    }
    lw[0] = w;
    lh[0] = h;
    for (;;) {
        int count = lw[level] * lh[level];

        off[level] = total;
        total += count;
        if (count <= 1) {
            break;
        }
        if (++level >= JP2_MAX_LEVELS - 1) {
            return -EINVAL;
        }
        lw[level] = (lw[level - 1] + 1) / 2;
        lh[level] = (lh[level - 1] + 1) / 2;
    }
    t->n = calloc((size_t)total, sizeof(*t->n));
    if (t->n == NULL) {
        return -ENOMEM;
    }
    t->nodes = total;
    for (l = 0; l <= level; l++) {
        for (y = 0; y < lh[l]; y++) {
            for (x = 0; x < lw[l]; x++) {
                struct tt_node *node = &t->n[off[l] + y * lw[l] + x];

                node->value = 999;
                node->low = 0;
                node->parent = (l == level) ? -1 :
                    off[l + 1] + (y / 2) * lw[l + 1] + (x / 2);
            }
        }
    }
    return 0;
}

static void
tt_free(struct tt *t)
{
    free(t->n);
    t->n = NULL;
    t->nodes = 0;
}

/* walk from the leaf up to the root and back down reading one bit per
 * node while low is below threshold, exactly like opj_tgt_decode */
static int
tt_decode(struct dec_bio *bio, struct tt *t, int leaf, int threshold)
{
    int stk[JP2_MAX_LEVELS];
    int sp = 0;
    int node = leaf;
    int low = 0;

    while (t->n[node].parent >= 0) {
        if (sp >= JP2_MAX_LEVELS) {
            return -EINVAL;
        }
        stk[sp++] = node;
        node = t->n[node].parent;
    }
    for (;;) {
        struct tt_node *cur = &t->n[node];

        if (low > cur->low) {
            cur->low = low;
        } else {
            low = cur->low;
        }
        while (low < threshold && low < cur->value) {
            if (bio_getbit(bio)) {
                cur->value = low;
            } else {
                low++;
            }
        }
        cur->low = low;
        if (sp == 0) {
            break;
        }
        node = stk[--sp];
    }
    return (t->n[leaf].value < threshold) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* packet header field coders                                           */
/* ------------------------------------------------------------------ */

static int
bio_getnumpasses(struct dec_bio *b)
{
    int n;

    if (!bio_getbit(b)) {
        return 1;
    }
    if (!bio_getbit(b)) {
        return 2;
    }
    if ((n = (int)bio_read(b, 2)) != 3) {
        return 3 + n;
    }
    if ((n = (int)bio_read(b, 5)) != 31) {
        return 6 + n;
    }
    return 37 + (int)bio_read(b, 7);
}

static int
bio_getcommacode(struct dec_bio *b)
{
    int n = 0;

    while (bio_getbit(b)) {
        if (++n > 32) {
            return -1;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* tile / resolution / band / code-block geometry                       */
/* ------------------------------------------------------------------ */

struct dec_cblk {
    int bandno;
    int x0, y0, x1, y1;
    int w, h;
    int included;
    int numpasses;
    int numbps;
    int numlenbits;
    uint32_t len;
    const uint8_t *data;
    int32_t *dst;       /* band coefficient array, at this block origin */
    int dst_w;
};

struct dec_pband {
    int cw, ch;
    struct dec_cblk *cb;
};

struct dec_prec {
    /* indexed by bandno, 1..3 for detail resolutions */
    struct dec_pband pb[4];
};

struct dec_band {
    int bandno;
    int x0, y0, x1, y1;
    int w, h;
    int numbps;
    struct dec_prec *prec;  /* res->pw * res->ph entries */
    int32_t *coef;
};

struct dec_res {
    int x0, y0, x1, y1;
    int w, h;
    int numbands;
    int pw, ph;
    struct dec_band b[3];
};

struct dec_comp {
    int x0, y0, x1, y1;
    int w, h;
    int nres;
    struct dec_res *res;
    int32_t *buf;
};

struct dec_tile {
    int ncomp;
    struct dec_comp *comp;
    uint8_t *data;
    uint32_t size;
};

static int
dec_ceildiv(int a, int b)
{
    return (int)(((int64_t)a + b - 1) / b);
}

static int
dec_ceildivpow2(int a, int n)
{
    return (int)(((int64_t)a + ((int64_t)1 << n) - 1) >> n);
}

static int
dec_floordivpow2(int a, int n)
{
    return (int)((int64_t)a >> n);
}

static int
dec_floorlog2(int n)
{
    int v = 0;

    while (n > 1) {
        n >>= 1;
        v++;
    }
    return v;
}

static void
comp_free(struct dec_comp *c)
{
    int r, b, p;

    if (c->res) {
        for (r = 0; r < c->nres; r++) {
            for (b = 0; b < c->res[r].numbands; b++) {
                struct dec_band *band = &c->res[r].b[b];

                for (p = 0; band->prec && p < c->res[r].pw * c->res[r].ph;
                     p++) {
                    free(band->prec[p].pb[band->bandno].cb);
                }
                free(band->prec);
                free(band->coef);
            }
        }
        free(c->res);
    }
    free(c->buf);
    memset(c, 0, sizeof(*c));
}

/* build the code-block grid of one band precinct (tcd.c init_tile) */
static int
band_build_prec(struct dec_band *band, struct dec_res *res, int r,
                int pdx, int pdy, int cblkw, int cblkh, int64_t *budget)
{
    int tlx = dec_floordivpow2(res->x0, pdx) << pdx;
    int tly = dec_floordivpow2(res->y0, pdy) << pdy;
    int cbgxexpn = (r == 0) ? pdx : pdx - 1;
    int cbgyexpn = (r == 0) ? pdy : pdy - 1;
    int tlcbgx = (r == 0) ? tlx : dec_ceildivpow2(tlx, 1);
    int tlcbgy = (r == 0) ? tly : dec_ceildivpow2(tly, 1);
    int cwexp = (cblkw < cbgxexpn) ? cblkw : cbgxexpn;
    int chexp = (cblkh < cbgyexpn) ? cblkh : cbgyexpn;
    int px, py;

    band->prec = calloc((size_t)res->pw * res->ph, sizeof(*band->prec));
    if (band->prec == NULL) {
        return -ENOMEM;
    }
    for (py = 0; py < res->ph; py++) {
        for (px = 0; px < res->pw; px++) {
            struct dec_prec *prc = &band->prec[py * res->pw + px];
            struct dec_pband *pb = &prc->pb[band->bandno];
            int cbgxstart = tlcbgx + px * (1 << cbgxexpn);
            int cbgxend = cbgxstart + (1 << cbgxexpn);
            int cbgystart = tlcbgy + py * (1 << cbgyexpn);
            int cbgyend = cbgystart + (1 << cbgyexpn);
            int x0 = (cbgxstart > band->x0) ? cbgxstart : band->x0;
            int x1 = (cbgxend < band->x1) ? cbgxend : band->x1;
            int y0 = (cbgystart > band->y0) ? cbgystart : band->y0;
            int y1 = (cbgyend < band->y1) ? cbgyend : band->y1;
            int cgtlx, cgbrx, cgtly, cgbry;
            int kx, ky;

            if (x1 <= x0 || y1 <= y0) {
                continue;
            }
            cgtlx = dec_floordivpow2(x0, cwexp) << cwexp;
            cgbrx = dec_ceildivpow2(x1, cwexp) << cwexp;
            cgtly = dec_floordivpow2(y0, chexp) << chexp;
            cgbry = dec_ceildivpow2(y1, chexp) << chexp;
            pb->cw = (cgbrx - cgtlx) >> cwexp;
            pb->ch = (cgbry - cgtly) >> chexp;
            if (pb->cw <= 0 || pb->ch <= 0 ||
                (int64_t)pb->cw * pb->ch > *budget) {
                return -ENOTSUP;
            }
            *budget -= (int64_t)pb->cw * pb->ch;
            pb->cb = calloc((size_t)pb->cw * pb->ch, sizeof(*pb->cb));
            if (pb->cb == NULL) {
                return -ENOMEM;
            }
            for (ky = 0; ky < pb->ch; ky++) {
                for (kx = 0; kx < pb->cw; kx++) {
                    struct dec_cblk *cb = &pb->cb[ky * pb->cw + kx];
                    int cx0 = cgtlx + (kx << cwexp);
                    int cx1 = cx0 + (1 << cwexp);
                    int cy0 = cgtly + (ky << chexp);
                    int cy1 = cy0 + (1 << chexp);

                    if (cx0 < x0) {
                        cx0 = x0;
                    }
                    if (cx1 > x1) {
                        cx1 = x1;
                    }
                    if (cy0 < y0) {
                        cy0 = y0;
                    }
                    if (cy1 > y1) {
                        cy1 = y1;
                    }
                    cb->bandno = band->bandno;
                    cb->x0 = cx0;
                    cb->y0 = cy0;
                    cb->x1 = cx1;
                    cb->y1 = cy1;
                    cb->w = cx1 - cx0;
                    cb->h = cy1 - cy0;
                    cb->dst = band->coef + (cy0 - band->y0) * band->w +
                              (cx0 - band->x0);
                    cb->dst_w = band->w;
                    if (cb->w <= 0 || cb->h <= 0) {
                        return -EINVAL;
                    }
                }
            }
        }
    }
    return 0;
}

static int
comp_init(struct dec_comp *c, JP2 *j, int tx0, int ty0, int tx1, int ty1,
          int compno)
{
    struct siz *s = &j->main_h.siz;
    struct coding_para *cp = &j->main_h.cod.p;
    struct scomponent *sc = &s->comps[compno];
    struct qcd *q = &j->main_h.qcd;
    int dx = sc->horizontal_separation;
    int dy = sc->vertical_separation;
    int cblkw = (int)cp->code_block_width + 2;
    int cblkh = (int)cp->code_block_height + 2;
    int derived;
    int tbl_len;
    int nres = (int)cp->decomp_level_num + 1;
    int64_t budget = 1 << 20;   /* code blocks per tile, hard cap */
    int r, b;

    memset(c, 0, sizeof(*c));
    c->x0 = dec_ceildiv(tx0, dx);
    c->y0 = dec_ceildiv(ty0, dy);
    c->x1 = dec_ceildiv(tx1, dx);
    c->y1 = dec_ceildiv(ty1, dy);
    c->w = c->x1 - c->x0;
    c->h = c->y1 - c->y0;
    if (c->w <= 0 || c->h <= 0 || c->w > JP2_MAX_DIM || c->h > JP2_MAX_DIM) {
        return -ENOTSUP;
    }
    if ((int64_t)c->w * c->h > (1 << 26)) {
        return -ENOTSUP;
    }
    derived = 3 * (nres - 1) + 1;
    tbl_len = (int)q->length - 3;
    if (tbl_len > derived) {
        tbl_len = derived;
    }
    if (tbl_len <= 0) {
        return -EINVAL;
    }
    c->nres = nres;
    c->res = calloc((size_t)nres, sizeof(*c->res));
    if (c->res == NULL) {
        return -ENOMEM;
    }
    for (r = 0; r < nres; r++) {
        struct dec_res *res = &c->res[r];
        int level = nres - 1 - r;
        int pdx = 15;
        int pdy = 15;

        if (cp->precinct_size) {
            pdx = cp->precinct_size[r] & 0x0f;
            pdy = cp->precinct_size[r] >> 4;
        }
        if (r > 0 && (pdx < 1 || pdy < 1)) {
            return -ENOTSUP;
        }
        res->x0 = dec_ceildivpow2(c->x0, level);
        res->y0 = dec_ceildivpow2(c->y0, level);
        res->x1 = dec_ceildivpow2(c->x1, level);
        res->y1 = dec_ceildivpow2(c->y1, level);
        res->w = res->x1 - res->x0;
        res->h = res->y1 - res->y0;
        res->pw = (res->x0 == res->x1) ? 0 :
                  (dec_ceildivpow2(res->x1, pdx) -
                   dec_floordivpow2(res->x0, pdx));
        res->ph = (res->y0 == res->y1) ? 0 :
                  (dec_ceildivpow2(res->y1, pdy) -
                   dec_floordivpow2(res->y0, pdy));
        res->numbands = (r == 0) ? 1 : 3;
        for (b = 0; b < res->numbands; b++) {
            struct dec_band *band = &res->b[b];
            int bandno = (r == 0) ? 0 : b + 1;
            int idx = (r == 0) ? 0 : 3 * (r - 1) + bandno;
            int err;

            band->bandno = bandno;
            if (r == 0) {
                band->x0 = res->x0;
                band->y0 = res->y0;
                band->x1 = res->x1;
                band->y1 = res->y1;
            } else {
                int x0b = bandno & 1;
                int y0b = bandno >> 1;

                band->x0 = dec_ceildivpow2(c->x0 - x0b * (1 << level),
                                           level + 1);
                band->x1 = dec_ceildivpow2(c->x1 - x0b * (1 << level),
                                           level + 1);
                band->y0 = dec_ceildivpow2(c->y0 - y0b * (1 << level),
                                           level + 1);
                band->y1 = dec_ceildivpow2(c->y1 - y0b * (1 << level),
                                           level + 1);
            }
            band->w = band->x1 - band->x0;
            band->h = band->y1 - band->y0;
            if (idx >= tbl_len) {
                return -EINVAL;
            }
            if (q->quant_type == 0) {
                /* reversible: single byte per subband (SGcd format) */
                band->numbps = (int)(q->table[idx] >> 3) +
                               (int)q->guard_num - 1;
            } else if (q->quant_type == 1) {
                /* scalar derived: one shared pair of two bytes (big-endian) */
                uint32_t ltmp = (uint32_t)q->table[0] * 256 | q->table[1];
                band->numbps = (int)(ltmp >> 11) + (int)q->guard_num - 1;
            } else {
                /* scalar expounded: two bytes per subband, big-endian */
                uint32_t ltmp = (uint32_t)q->table[idx * 2] * 256 |
                                q->table[idx * 2 + 1];
                band->numbps = (int)(ltmp >> 11) + (int)q->guard_num - 1;
            }
            if (band->w > 0 && band->h > 0) {
                if ((int64_t)band->w * band->h > (1 << 26)) {
                    return -ENOTSUP;
                }
                band->coef = calloc((size_t)band->w * band->h,
                                    sizeof(*band->coef));
                if (band->coef == NULL) {
                    return -ENOMEM;
                }
                if (res->pw == 0 || res->ph == 0) {
                    return -ENOTSUP;
                }
                err = band_build_prec(band, res, r, pdx, pdy, cblkw, cblkh,
                                      &budget);
                if (err) {
                    return err;
                }
            }
        }
    }
    c->buf = calloc((size_t)c->w * c->h, sizeof(*c->buf));
    if (c->buf == NULL) {
        return -ENOMEM;
    }
    return 0;
}

/* parse one packet (header and body) and advance *pos past it */
static int
packet_parse(struct dec_tile *t, struct cod *cod, struct dec_res *res, int p,
             size_t *pos, struct dec_cblk **order, int order_cap, int *count)
{
    struct dec_bio bio;
    size_t start;
    size_t hdr_len;
    int present;
    int b;
    int err = 0;

    *count = 0;
    if (cod->sop) {
        if (*pos + 6 > t->size || t->data[*pos] != 0xff ||
            t->data[*pos + 1] != 0x91) {
            return -EINVAL;
        }
        *pos += 6;
    }
    if (*pos >= t->size) {
        return -EINVAL;
    }
    start = *pos;
    bio_init(&bio, t->data + start, t->size - start);
    present = bio_getbit(&bio);
    if (!present) {
        hdr_len = bio_inalign(&bio);
        *pos = start + hdr_len;
        if (cod->eph) {
            if (*pos + 2 > t->size || t->data[*pos] != 0xff ||
                t->data[*pos + 1] != 0x92) {
                return -EINVAL;
            }
            *pos += 2;
        }
        return 0;
    }

    for (b = 0; b < res->numbands; b++) {
        struct dec_band *band = &res->b[b];
        struct dec_pband *pb;
        struct tt incl;
        struct tt imsb;
        int k;

        if (band->prec == NULL || band->w <= 0 || band->h <= 0) {
            continue;
        }
        pb = &band->prec[p].pb[band->bandno];
        if (pb->cw <= 0 || pb->ch <= 0) {
            continue;
        }
        err = tt_init(&incl, pb->cw, pb->ch);
        if (err) {
            return err;
        }
        err = tt_init(&imsb, pb->cw, pb->ch);
        if (err) {
            tt_free(&incl);
            return err;
        }
        for (k = 0; k < pb->cw * pb->ch; k++) {
            struct dec_cblk *cb = &pb->cb[k];
            int inc;
            int np;
            int bitlen;
            int i;
            int got;

            if (cb->included) {
                inc = bio_getbit(&bio);
            } else {
                inc = tt_decode(&bio, &incl, k, 1);
            }
            if (inc < 0) {
                err = inc;
                goto fail;
            }
            if (!inc) {
                cb->numpasses = 0;
                continue;
            }
            if (!cb->included) {
                i = 0;
                for (;;) {
                    if (i > band->numbps + 1) {
                        err = -EINVAL;
                        goto fail;
                    }
                    got = tt_decode(&bio, &imsb, k, i);
                    if (got < 0) {
                        err = got;
                        goto fail;
                    }
                    if (got) {
                        break;
                    }
                    i++;
                }
                /* i is the threshold at which the tag tree reported the
                 * number of missing MSBs, so that value is i - 1 and the
                 * block bit-plane count is band.numbps - (i - 1) */
                cb->numbps = band->numbps - i + 1;
                cb->numlenbits = 3;
            }
            np = bio_getnumpasses(&bio);
            if (np > 109) {
                /* one segment per code block in this subset */
                err = -ENOTSUP;
                goto fail;
            }
            cb->numpasses = np;
            got = bio_getcommacode(&bio);
            if (got < 0) {
                err = -EINVAL;
                goto fail;
            }
            cb->numlenbits += got;
            bitlen = cb->numlenbits + dec_floorlog2(np);
            if (bitlen > 32) {
                err = -EINVAL;
                goto fail;
            }
            cb->len = bio_read(&bio, bitlen);
            cb->included = 1;
            if (*count >= order_cap) {
                err = -EINVAL;
                goto fail;
            }
            order[(*count)++] = cb;
        }
        tt_free(&incl);
        tt_free(&imsb);
        continue;
fail:
        tt_free(&incl);
        tt_free(&imsb);
        return err;
    }

    hdr_len = bio_inalign(&bio);
    if (hdr_len == 0) {
        return -EINVAL;
    }
    *pos = start + hdr_len;
    if (cod->eph) {
        if (*pos + 2 > t->size || t->data[*pos] != 0xff ||
            t->data[*pos + 1] != 0x92) {
            return -EINVAL;
        }
        *pos += 2;
    }
    for (b = 0; b < *count; b++) {
        struct dec_cblk *cb = order[b];

        if (cb->len == 0) {
            cb->data = NULL;
            continue;
        }
        if (cb->len > t->size - *pos) {
            return -EINVAL;
        }
        cb->data = t->data + *pos;
        *pos += cb->len;
    }
    return 0;
}

/* LRCP with a single layer: resolution, component, precinct */
static int
tile_parse_packets(struct dec_tile *t, struct cod *cod)
{
    struct dec_cblk **order;
    int order_cap = 0;
    int ci;
    int r;
    int p;
    size_t pos = 0;
    int err = 0;

    for (ci = 0; ci < t->ncomp; ci++) {
        struct dec_comp *c = &t->comp[ci];

        for (r = 0; r < c->nres; r++) {
            struct dec_res *res = &c->res[r];
            int b;

            for (b = 0; b < res->numbands; b++) {
                struct dec_band *band = &res->b[b];

                if (band->prec == NULL) {
                    continue;
                }
                for (p = 0; p < res->pw * res->ph; p++) {
                    order_cap += band->prec[p].pb[band->bandno].cw *
                                 band->prec[p].pb[band->bandno].ch;
                }
            }
        }
    }
    if (order_cap == 0) {
        return 0;
    }
    order = malloc((size_t)order_cap * sizeof(*order));
    if (order == NULL) {
        return -ENOMEM;
    }
    for (r = 0; r < t->comp[0].nres && !err; r++) {
        for (ci = 0; ci < t->ncomp && !err; ci++) {
            struct dec_comp *c = &t->comp[ci];
            struct dec_res *res = &c->res[r];
            int count;

            for (p = 0; p < res->pw * res->ph; p++) {
                err = packet_parse(t, cod, res, p, &pos, order, order_cap,
                                   &count);
                if (err) {
                    break;
                }
            }
        }
    }
    free(order);
    return err;
}

/* ------------------------------------------------------------------ */
/* Tier-1 coefficient decoder (ISO 15444-1 Annex D, OpenJPEG t1.c)      */
/* ------------------------------------------------------------------ */

struct dec_t1 {
    int w, h, orient;
    int32_t *coef;
    uint8_t *sig;
    uint8_t *sign;
    uint8_t *pi;
    uint8_t *mu;
};

static int
t1_at(const uint8_t *a, int w, int h, int x, int y)
{
    if (x < 0 || x >= w || y < 0 || y >= h) {
        return 0;
    }
    return a[y * w + x];
}

static int
t1_any_nbr(const struct dec_t1 *t, int x, int y)
{
    int dx, dy;

    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            if ((dx || dy) && t1_at(t->sig, t->w, t->h, x + dx, y + dy)) {
                return 1;
            }
        }
    }
    return 0;
}

static int
t1_ctxno_zc(const struct dec_t1 *t, int x, int y)
{
    int h = t1_at(t->sig, t->w, t->h, x - 1, y) +
            t1_at(t->sig, t->w, t->h, x + 1, y);
    int v = t1_at(t->sig, t->w, t->h, x, y - 1) +
            t1_at(t->sig, t->w, t->h, x, y + 1);
    int d = t1_at(t->sig, t->w, t->h, x - 1, y - 1) +
            t1_at(t->sig, t->w, t->h, x - 1, y + 1) +
            t1_at(t->sig, t->w, t->h, x + 1, y - 1) +
            t1_at(t->sig, t->w, t->h, x + 1, y + 1);

    if (t->orient == 3) {
        int hv = h + v;

        if (d == 0) {
            return (hv == 0) ? 0 : (hv == 1 ? 1 : 2);
        }
        if (d == 1) {
            return (hv == 0) ? 3 : (hv == 1 ? 4 : 5);
        }
        if (d == 2) {
            return (hv == 0) ? 6 : 7;
        }
        return 8;
    }
    /* HL (orient 1) correlates vertically, LH (orient 2) horizontally: the
     * LUT swaps the horizontal and vertical neighbour counts for HL only */
    if (t->orient == 1) {
        int tmp = h;

        h = v;
        v = tmp;
    }
    if (h == 0 && v == 0) {
        return (d == 0) ? 0 : (d == 1 ? 1 : 2);
    }
    if (h == 0 && v == 1) {
        return 3;
    }
    if (h == 0) {
        return 4;
    }
    if (h == 1 && v == 0) {
        return (d == 0) ? 5 : 6;
    }
    if (h == 1) {
        return 7;
    }
    return 8;
}

static void
t1_sc_dir(const struct dec_t1 *t, int x, int y, int *pos, int *neg)
{
    int i;

    if (x < 0 || x >= t->w || y < 0 || y >= t->h) {
        return;
    }
    i = y * t->w + x;
    if (t->sig[i]) {
        if (t->sign[i]) {
            (*neg)++;
        } else {
            (*pos)++;
        }
    }
}

/* significance context and the sign prediction bit */
static int
t1_sc_params(const struct dec_t1 *t, int x, int y, int *spb)
{
    int pe = 0, ne = 0, pw = 0, nw = 0, pn = 0, nn = 0, ps = 0, ns = 0;
    int hc, vc;

    t1_sc_dir(t, x + 1, y, &pe, &ne);
    t1_sc_dir(t, x - 1, y, &pw, &nw);
    t1_sc_dir(t, x, y - 1, &pn, &nn);
    t1_sc_dir(t, x, y + 1, &ps, &ns);
    hc = ((pe + pw) > 1 ? 1 : (pe + pw)) -
         ((ne + nw) > 1 ? 1 : (ne + nw));
    vc = ((pn + ps) > 1 ? 1 : (pn + ps)) -
         ((nn + ns) > 1 ? 1 : (nn + ns));
    if (hc == 0 && vc == 0) {
        *spb = 0;
    } else {
        *spb = !(hc > 0 || (hc == 0 && vc > 0));
    }
    if (hc < 0) {
        hc = -hc;
        vc = -vc;
    }
    if (hc == 0) {
        return (vc != 0) ? 1 : 0;
    }
    return (vc < 0) ? 2 : (vc == 0 ? 3 : 4);
}

static int
t1_ctxno_mag(const struct dec_t1 *t, int x, int y)
{
    if (t->mu[y * t->w + x]) {
        return 16;
    }
    return t1_any_nbr(t, x, y) ? 15 : 14;
}

/* one sample of the significance or cleanup pass */
static void
t1_full_step(struct dec_t1 *t, struct mq_dec *mq, int x, int y, int op)
{
    int idx = y * t->w + x;
    int v, neg, spb;

    if (t->sig[idx] || t->pi[idx]) {
        return;
    }
    v = (int)mq_dec_decode(mq, t1_ctxno_zc(t, x, y));
    t->pi[idx] = 1;
    if (!v) {
        return;
    }
    v = (int)mq_dec_decode(mq, 9 + t1_sc_params(t, x, y, &spb));
    neg = v ^ spb;
    t->coef[idx] = neg ? -op : op;
    t->sig[idx] = 1;
    t->sign[idx] = (uint8_t)neg;
}

/* the run-length sample of a cleanup group: significance is implied */
static void
t1_runlen_sample(struct dec_t1 *t, struct mq_dec *mq, int x, int y, int op)
{
    int idx = y * t->w + x;
    int v, neg, spb;

    v = (int)mq_dec_decode(mq, 9 + t1_sc_params(t, x, y, &spb));
    neg = v ^ spb;
    t->coef[idx] = neg ? -op : op;
    t->sig[idx] = 1;
    t->sign[idx] = (uint8_t)neg;
}

static void
t1_sigpass(struct dec_t1 *t, struct mq_dec *mq, int bpno)
{
    int one = 1 << bpno;
    int op = one | (one >> 1);
    int w = t->w;
    int h = t->h;
    int ks, i, j;

    for (ks = 0; ks + 4 <= h; ks += 4) {
        for (i = 0; i < w; i++) {
            for (j = 0; j < 4; j++) {
                int idx = (ks + j) * w + i;

                if (!t->sig[idx] && !t->pi[idx] &&
                    t1_any_nbr(t, i, ks + j)) {
                    t1_full_step(t, mq, i, ks + j, op);
                }
            }
        }
    }
    for (i = 0; i < w; i++) {
        for (j = ks; j < h; j++) {
            int idx = j * w + i;

            if (!t->sig[idx] && !t->pi[idx] && t1_any_nbr(t, i, j)) {
                t1_full_step(t, mq, i, j, op);
            }
        }
    }
}

static void
t1_refpass(struct dec_t1 *t, struct mq_dec *mq, int bpno)
{
    int poshalf = (1 << bpno) >> 1;
    int w = t->w;
    int h = t->h;
    int ks, i, j;

    for (ks = 0; ks + 4 <= h; ks += 4) {
        for (i = 0; i < w; i++) {
            for (j = 0; j < 4; j++) {
                int idx = (ks + j) * w + i;

                if (t->sig[idx] && !t->pi[idx]) {
                    int v = (int)mq_dec_decode(mq,
                                               t1_ctxno_mag(t, i, ks + j));

                    t->coef[idx] += (v ^ (t->coef[idx] < 0)) ?
                                    poshalf : -poshalf;
                    t->mu[idx] = 1;
                }
            }
        }
    }
    for (i = 0; i < w; i++) {
        for (j = ks; j < h; j++) {
            int idx = j * w + i;

            if (t->sig[idx] && !t->pi[idx]) {
                int v = (int)mq_dec_decode(mq, t1_ctxno_mag(t, i, j));

                t->coef[idx] += (v ^ (t->coef[idx] < 0)) ?
                                poshalf : -poshalf;
                t->mu[idx] = 1;
            }
        }
    }
}

static void
t1_clnpass(struct dec_t1 *t, struct mq_dec *mq, int bpno)
{
    int one = 1 << bpno;
    int op = one | (one >> 1);
    int w = t->w;
    int h = t->h;
    int ks, i, j;

    for (ks = 0; ks + 4 <= h; ks += 4) {
        for (i = 0; i < w; i++) {
            int all_zero = 1;

            for (j = 0; j < 4; j++) {
                int idx = (ks + j) * w + i;

                if (t->sig[idx] || t->pi[idx] || t1_any_nbr(t, i, ks + j)) {
                    all_zero = 0;
                    break;
                }
            }
            if (all_zero) {
                if (mq_dec_decode(mq, 17)) {
                    int r1 = (int)mq_dec_decode(mq, 18);
                    int r2 = (int)mq_dec_decode(mq, 18);
                    int runlen = (r1 << 1) | r2;

                    t1_runlen_sample(t, mq, i, ks + runlen, op);
                    for (j = runlen + 1; j < 4; j++) {
                        t1_full_step(t, mq, i, ks + j, op);
                    }
                }
            } else {
                for (j = 0; j < 4; j++) {
                    t1_full_step(t, mq, i, ks + j, op);
                }
            }
            for (j = 0; j < 4; j++) {
                t->pi[(ks + j) * w + i] = 0;
            }
        }
    }
    for (i = 0; i < w; i++) {
        for (j = ks; j < h; j++) {
            t1_full_step(t, mq, i, j, op);
        }
        for (j = ks; j < h; j++) {
            t->pi[j * w + i] = 0;
        }
    }
}

static int
t1_decode_cblk(struct dec_cblk *cb)
{
    struct dec_t1 t;
    struct mq_dec mq;
    int passno;
    int passtype = 2;
    int bpno_plus_one = cb->numbps;
    int i, j;

    if (cb->numpasses <= 0) {
        return 0;
    }
    if (cb->w <= 0 || cb->h <= 0 ||
        (int64_t)cb->w * cb->h > (1 << 22) || cb->numbps > 30) {
        return -ENOTSUP;
    }
    memset(&t, 0, sizeof(t));
    t.w = cb->w;
    t.h = cb->h;
    t.orient = cb->bandno;
    t.coef = calloc((size_t)t.w * t.h, sizeof(*t.coef));
    t.sig = calloc((size_t)t.w * t.h, 1);
    t.sign = calloc((size_t)t.w * t.h, 1);
    t.pi = calloc((size_t)t.w * t.h, 1);
    t.mu = calloc((size_t)t.w * t.h, 1);
    if (t.coef == NULL || t.sig == NULL || t.sign == NULL || t.pi == NULL ||
        t.mu == NULL) {
        free(t.coef);
        free(t.sig);
        free(t.sign);
        free(t.pi);
        free(t.mu);
        return -ENOMEM;
    }
    mq_dec_init(&mq, cb->data, cb->len);
    for (passno = 0; passno < cb->numpasses; passno++) {
        if (bpno_plus_one < 0) {
            /* the packet header claims more passes than the tag tree allows */
            free(t.coef);
            free(t.sig);
            free(t.sign);
            free(t.pi);
            free(t.mu);
            return -EINVAL;
        }
        switch (passtype) {
        case 0:
            t1_sigpass(&t, &mq, bpno_plus_one);
            break;
        case 1:
            t1_refpass(&t, &mq, bpno_plus_one);
            break;
        default:
            t1_clnpass(&t, &mq, bpno_plus_one);
            break;
        }
        if (++passtype == 3) {
            passtype = 0;
            bpno_plus_one--;
        }
    }
    for (j = 0; j < t.h; j++) {
        for (i = 0; i < t.w; i++) {
            /* Tier-1 magnitudes carry one extra fractional bit, and the
             * sign-magnitude value is halved with truncation towards zero */
            cb->dst[j * cb->dst_w + i] = t.coef[j * t.w + i] / 2;
        }
    }
    free(t.coef);
    free(t.sig);
    free(t.sign);
    free(t.pi);
    free(t.mu);
    return 0;
}

/* ------------------------------------------------------------------ */
/* inverse reversible 5-3 wavelet (ISO 15444-1 F.4 / OpenJPEG dwt.c)   */
/* ------------------------------------------------------------------ */

/* a[] holds low pass at cas + 2*i and high pass at 1 - cas + 2*i, the exact
 * interleaved layout the forward transform leaves behind */
#define IDX_CLIP(i, n) ((i) < 0 ? 0 : ((i) >= (n) ? (n) - 1 : (i)))

static void
idwt53_1d(int32_t *a, int sn, int dn, int cas)
{
    int32_t *low = a + cas;
    int32_t *high = a + (1 - cas);
    int i;

    if (dn <= 0) {
        return;
    }
    if (cas) {
        if (sn == 0) {
            /* a single high pass sample with no low pass neighbour */
            if (dn == 1) {
                a[0] /= 2;
            }
            return;
        }
        for (i = 0; i < sn; i++) {
            low[2 * i] -= (high[2 * i] +
                           high[2 * IDX_CLIP(i + 1, dn)] + 2) >> 2;
        }
        for (i = 0; i < dn; i++) {
            high[2 * i] += (low[2 * IDX_CLIP(i - 1, sn)] +
                            low[2 * IDX_CLIP(i, sn)]) >> 1;
        }
    } else {
        for (i = 0; i < sn; i++) {
            low[2 * i] -= (high[2 * IDX_CLIP(i - 1, dn)] +
                           high[2 * IDX_CLIP(i, dn)] + 2) >> 2;
        }
        for (i = 0; i < dn; i++) {
            high[2 * i] += (low[2 * i] +
                            low[2 * IDX_CLIP(i + 1, sn)]) >> 1;
        }
    }
}

/* place each subband into the component sample grid: approximations at the
 * top left, HL to the right, LH below and HH at the bottom right */
static int
comp_place_bands(struct dec_comp *c)
{
    struct dec_band *ll = &c->res[0].b[0];
    int x, y, rr, b;

    if (ll->w > 0 && ll->h > 0) {
        for (y = 0; y < ll->h; y++) {
            for (x = 0; x < ll->w; x++) {
                c->buf[(size_t)y * c->w + x] =
                    ll->coef[(size_t)y * ll->w + x];
            }
        }
    }
    for (rr = 1; rr < c->nres; rr++) {
        struct dec_res *res = &c->res[rr];
        int llw = c->res[rr - 1].w;
        int llh = c->res[rr - 1].h;

        for (b = 0; b < res->numbands; b++) {
            struct dec_band *band = &res->b[b];
            int ox = (band->bandno & 1) ? llw : 0;
            int oy = (band->bandno & 2) ? llh : 0;

            if (band->w <= 0 || band->h <= 0) {
                continue;
            }
            if (ox + band->w > c->w || oy + band->h > c->h) {
                return -EINVAL;
            }
            for (y = 0; y < band->h; y++) {
                for (x = 0; x < band->w; x++) {
                    c->buf[(size_t)(oy + y) * c->w + ox + x] =
                        band->coef[(size_t)y * band->w + x];
                }
            }
        }
    }
    return 0;
}

/* inverse 2D 5-3: horizontal rows first, then vertical columns, one
 * resolution level at a time */
static int
comp_dwt(struct dec_comp *c)
{
    int32_t *tmp;
    int maxdim = (c->w > c->h) ? c->w : c->h;
    int rr, x, y, i;

    if (c->nres <= 1) {
        return 0;
    }
    if (maxdim <= 0) {
        return -EINVAL;
    }
    tmp = malloc((size_t)maxdim * sizeof(*tmp));
    if (tmp == NULL) {
        return -ENOMEM;
    }
    for (rr = 1; rr < c->nres; rr++) {
        int llw = c->res[rr - 1].w;
        int llh = c->res[rr - 1].h;
        int curw = c->res[rr].w;
        int curh = c->res[rr].h;
        int dw = curw - llw;
        int dh = curh - llh;
        int casx = c->res[rr].x0 & 1;
        int casy = c->res[rr].y0 & 1;

        if (llw < 0 || llh < 0 || dw < 0 || dh < 0 ||
            llw + dw != curw || llh + dh != curh) {
            free(tmp);
            return -EINVAL;
        }
        for (y = 0; y < curh; y++) {
            int32_t *row = c->buf + (size_t)y * c->w;

            for (i = 0; i < llw; i++) {
                tmp[casx + 2 * i] = row[i];
            }
            for (i = 0; i < dw; i++) {
                tmp[1 - casx + 2 * i] = row[llw + i];
            }
            idwt53_1d(tmp, llw, dw, casx);
            memcpy(row, tmp, (size_t)curw * sizeof(*tmp));
        }
        for (x = 0; x < curw; x++) {
            for (i = 0; i < llh; i++) {
                tmp[casy + 2 * i] = c->buf[(size_t)i * c->w + x];
            }
            for (i = 0; i < dh; i++) {
                tmp[1 - casy + 2 * i] =
                    c->buf[(size_t)(llh + i) * c->w + x];
            }
            idwt53_1d(tmp, llh, dh, casy);
            for (y = 0; y < curh; y++) {
                c->buf[(size_t)y * c->w + x] = tmp[y];
            }
        }
    }
    free(tmp);
    return 0;
}

/* compute one subband's quantization step (OpenJPEG + ISO 15444-1 F.1.2)
 * OpenJPEG reference: tcd.c line ~1082, j2k.c line ~11370 */
static double
qcd_step(const struct qcd *q, int idx, int bandno, int precision)
{
    uint32_t l_tmp;
    int expn;
    int mant;
    int log2_gain;
    int Rb;
    double step;

    (void)bandno; /* BUG_WEIRD_TWO_INVK: decoder qmfbid==0 forces log2_gain=0 */

    if (q->quant_type == 0) {
        return 1.0; /* reversible: no quantization */
    }

    /* unpack expn (5-bit) and mant (11-bit) from the QCD entry:
     * two bytes are combined as a big-endian 16-bit value (ISO marker
     * order matches OpenJPEG opj_read_bytes default), then
     * expn = l_tmp >> 11, mant = l_tmp & 0x7ff */
    if (q->quant_type == 1) {
        l_tmp = (uint32_t)q->table[0] * 256 | q->table[1];
    } else {
        l_tmp = (uint32_t)q->table[idx * 2] * 256 |
                q->table[idx * 2 + 1];
    }
    expn = (int)(l_tmp >> 11);
    mant = (int)(l_tmp & 0x7ff);

    /* sub-band gains (Table E-1, ISO 15444-1).
     * OpenJPEG BUG_WEIRD_TWO_INVK compensation (tcd.c ~line 1074):
     * when decoding the irreversible 9-7 transform (qmfbid==0, which is
     * the only transform that reaches this code path with quant_type>0),
     * ALL subbands use log2_gain = 0 to compensate for the two_invK
     * scaling in the IDWT (dwt.c ~line 3363). */
    log2_gain = 0;
    Rb = precision + log2_gain;

    step = (1.0 + mant / 2048.0) * pow(2.0, (double)(Rb - expn));
    return step;
}

/* inverse quantization: for every coefficient in c->buf (already placed),
 * multiply by that subband's step.  Works on int32 input and produces
 * double output in buf_d (caller-allocated frame-size buffer). */
static void
comp_dequantize(const struct dec_comp *c, const struct qcd *q,
                int precision, double *buf_d)
{
    int rr, b, y, x;

    /* LL band first */
    if (c->res[0].w > 0 && c->res[0].h > 0) {
        const struct dec_band *ll = &c->res[0].b[0];
        double step = qcd_step(q, 0, 0, precision);

        for (y = 0; y < ll->h; y++) {
            for (x = 0; x < ll->w; x++) {
                int32_t v = c->buf[(size_t)y * c->w + x];

                buf_d[(size_t)y * c->w + x] = (double)v * step;
            }
        }
    }

    for (rr = 1; rr < c->nres; rr++) {
        const struct dec_res *res = &c->res[rr];
        int llw = c->res[rr - 1].w;
        int llh = c->res[rr - 1].h;

        for (b = 0; b < res->numbands; b++) {
            const struct dec_band *band = &res->b[b];
            int bandno = b + 1;
            int idx = 3 * (rr - 1) + bandno;
            double step;
            int ox, oy;

            if (band->w <= 0 || band->h <= 0) {
                continue;
            }
            ox = (bandno & 1) ? llw : 0;
            oy = (bandno & 2) ? llh : 0;
            step = qcd_step(q, idx, bandno, precision);

            for (y = 0; y < band->h; y++) {
                for (x = 0; x < band->w; x++) {
                    int32_t v = c->buf[(size_t)(oy + y) * c->w + ox + x];

                    buf_d[(size_t)(oy + y) * c->w + ox + x] =
                        (double)v * step;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* inverse irreversible 9-7 wavelet — OpenJPEG conformance             */
/* ------------------------------------------------------------------ */

static void
idwt97_1d(double *w, int sn, int dn, int cas)
{
    static const double ALPHA = -1.586134342;
    static const double BETA  = -0.052980118;
    static const double GAMMA =  0.882911075;
    static const double DELTA =  0.443506852;
    static const double OPJ_K = 1.230174105;
    static const double TWO_INVK = 1.625732422;

    int a = cas;            /* low starts at w[a]  */
    int b = 1 - cas;        /* high starts at w[b] */
    int i;

    if (dn <= 0) {
        for (i = 0; i < sn; i++)
            w[a + 2 * i] *= OPJ_K;
        return;
    }

    /* scale */
    for (i = 0; i < sn; i++)
        w[a + 2 * i] *= OPJ_K;
    for (i = 0; i < dn; i++)
        w[b + 2 * i] *= TWO_INVK;

    /* OpenJPEG step2 primitive (flat, no SIMD):
     *   fw[-1] += (fl[0] + fw[0]) * c        (first iteration)
     *   fw[-1] += (fw[-2] + fw[0]) * c        (subsequent iterations)
     *   fw[-1] += 2 * fw[-2] * c              (boundary, one extra sample) */
#define STEP2(fl_off, fw_off, end_cnt, bound, coeff)                  \
    do {                                                               \
        int _max = (end_cnt) < (bound) ? (end_cnt) : (bound);          \
        double *_fl = (w) + (fl_off);                                  \
        double *_fw = (w) + (fw_off);                                  \
        if (_max > 0) {                                                \
            _fw[-1] += (_fl[0] + _fw[0]) * (coeff);                    \
            _fw += 2;                                                  \
            for (int _k = 1; _k < _max; _k++) {                        \
                _fw[-1] += (_fw[-2] + _fw[0]) * (coeff);               \
                _fw += 2;                                              \
            }                                                          \
        }                                                              \
        if ((bound) < (end_cnt)) {                                     \
            _fw[-1] += 2.0 * _fw[-2] * (coeff);                        \
        }                                                              \
    } while (0)

    /* Inverse lifting order matches opj_v8dwt_decode (dwt.c ~3398):
     *   step2(wavelet+b, wavelet+a+1, sn, min(sn, dn-a), -DELTA)
     *   step2(wavelet+a, wavelet+b+1, dn, min(dn, sn-b), -GAMMA)
     *   step2(wavelet+b, wavelet+a+1, sn, min(sn, dn-a), -BETA)
     *   step2(wavelet+a, wavelet+b+1, dn, min(dn, sn-b), -ALPHA)
     * offets are a,b because wavelet base is w */
    STEP2(b, a + 1, sn, dn - a, -DELTA);
    STEP2(a, b + 1, dn, sn - b, -GAMMA);
    STEP2(b, a + 1, sn, dn - a, -BETA);
    STEP2(a, b + 1, dn, sn - b, -ALPHA);

#undef STEP2
}

/* inverse 2D 9-7: same cascade structure as comp_dwt, but double */
static int
comp_dwt97(struct dec_comp *c, double *buf_d)
{
    double *tmp, *tmp_anchor;
    int maxdim = (c->w > c->h) ? c->w : c->h;
    int rr, x, y, i;

    if (c->nres <= 1) {
        return 0;
    }
    if (maxdim <= 0) {
        return -EINVAL;
    }
    tmp_anchor = malloc((size_t)(maxdim + 4) * sizeof(*tmp));
    if (tmp_anchor == NULL) {
        return -ENOMEM;
    }
    /* reserve 2-element padding at the front so idwt97_1d's ptr[-1]/ptr[-2]
     * accesses don't run off the start of the buffer */
    tmp = tmp_anchor + 2;
    for (rr = 1; rr < c->nres; rr++) {
        int llw = c->res[rr - 1].w;
        int llh = c->res[rr - 1].h;
        int curw = c->res[rr].w;
        int curh = c->res[rr].h;
        int dw = curw - llw;
        int dh = curh - llh;
        int casx = c->res[rr].x0 & 1;
        int casy = c->res[rr].y0 & 1;

        if (llw < 0 || llh < 0 || dw < 0 || dh < 0 ||
            llw + dw != curw || llh + dh != curh) {
            free(tmp_anchor);
            return -EINVAL;
        }
        for (y = 0; y < curh; y++) {
            double *row = buf_d + (size_t)y * c->w;

            for (i = 0; i < llw; i++) {
                tmp[casx + 2 * i] = row[i];
            }
            for (i = 0; i < dw; i++) {
                tmp[1 - casx + 2 * i] = row[llw + i];
            }
            idwt97_1d(tmp, llw, dw, casx);
            memcpy(row, tmp, (size_t)curw * sizeof(*tmp));
        }
        for (x = 0; x < curw; x++) {
            for (i = 0; i < llh; i++) {
                tmp[casy + 2 * i] =
                    buf_d[(size_t)i * c->w + x];
            }
            for (i = 0; i < dh; i++) {
                tmp[1 - casy + 2 * i] =
                    buf_d[(size_t)(llh + i) * c->w + x];
            }
            idwt97_1d(tmp, llh, dh, casy);
            for (y = 0; y < curh; y++) {
                buf_d[(size_t)y * c->w + x] = tmp[y];
            }
        }
    }
    free(tmp_anchor);
    return 0;
}

/* copy the reconstructed tile component into the image component plane */
static int
comp_store(struct dec_comp *c, int32_t *plane, int pw, int ph, int px0,
           int py0)
{
    int ox = c->x0 - px0;
    int oy = c->y0 - py0;
    int y;

    if (ox < 0 || oy < 0 || ox + c->w > pw || oy + c->h > ph) {
        return -EINVAL;
    }
    for (y = 0; y < c->h; y++) {
        memcpy(plane + (size_t)(oy + y) * pw + ox,
               c->buf + (size_t)y * c->w,
               (size_t)c->w * sizeof(*plane));
    }
    return 0;
}

/* store the 9-7 (double) result into the tile component plane with rounding */
static int
comp_store97(const struct dec_comp *c, const double *buf_d,
             int32_t *plane, int pw, int ph, int px0, int py0)
{
    int ox = c->x0 - px0;
    int oy = c->y0 - py0;
    int y, x;

    if (ox < 0 || oy < 0 || ox + c->w > pw || oy + c->h > ph) {
        return -EINVAL;
    }
    for (y = 0; y < c->h; y++) {
        for (x = 0; x < c->w; x++) {
            double v = buf_d[(size_t)(oy + y) * pw + ox + x];

            plane[(size_t)(oy + y) * pw + ox + x] =
                (int32_t)lrint(v);
        }
    }
    return 0;
}

static void
tile_free(struct dec_tile *t)
{
    int ci;

    if (t->comp) {
        for (ci = 0; ci < t->ncomp; ci++) {
            comp_free(&t->comp[ci]);
        }
        free(t->comp);
    }
    free(t->data);
    memset(t, 0, sizeof(*t));
}

static int
tile_decode(JP2 *j, struct tile_header *th, FILE *f, long flen,
            int32_t * const *planes, const int *pw, const int *ph,
            const int *px0, const int *py0)
{
    struct siz *s = &j->main_h.siz;
    struct dec_tile t;
    uint8_t *data = NULL;
    int imgx1 = (int)(s->left + s->width);
    int imgy1 = (int)(s->top + s->height);
    int ntx, nty, id, tx0, ty0, tx1, ty1;
    int ci, r, b, p, k, err = 0;

    memset(&t, 0, sizeof(t));
    if (s->tile_width == 0 || s->tile_height == 0) {
        return -EINVAL;
    }
    ntx = dec_ceildiv(imgx1 - (int)s->tile_left, (int)s->tile_width);
    nty = dec_ceildiv(imgy1 - (int)s->tile_top, (int)s->tile_height);
    if (ntx <= 0 || nty <= 0) {
        return -EINVAL;
    }
    id = (int)th->sot.tile_id;
    if (id < 0 || id >= ntx * nty) {
        return -EINVAL;
    }
    if (th->sot.tile_part_nums != 1) {
        /* one tile part per tile keeps the LRCP packet walk valid */
        return -ENOTSUP;
    }
    tx0 = (int)s->tile_left + (id % ntx) * (int)s->tile_width;
    ty0 = (int)s->tile_top + (id / ntx) * (int)s->tile_height;
    tx1 = tx0 + (int)s->tile_width;
    ty1 = ty0 + (int)s->tile_height;
    if (tx0 < (int)s->left) {
        tx0 = (int)s->left;
    }
    if (ty0 < (int)s->top) {
        ty0 = (int)s->top;
    }
    if (tx1 > imgx1) {
        tx1 = imgx1;
    }
    if (ty1 > imgy1) {
        ty1 = imgy1;
    }
    if (tx1 <= tx0 || ty1 <= ty0) {
        return -EINVAL;
    }

    if (th->data_size == 0 || flen <= 0 ||
        (long)th->data_start > flen ||
        (long)th->data_size > flen - (long)th->data_start) {
        return -EINVAL;
    }
    data = malloc(th->data_size);
    if (data == NULL) {
        return -ENOMEM;
    }
    if (fseek(f, (long)th->data_start, SEEK_SET) != 0 ||
        fread(data, 1, th->data_size, f) != th->data_size) {
        free(data);
        return -EINVAL;
    }

    t.ncomp = (int)s->component_num;
    t.data = data;
    t.size = th->data_size;
    t.comp = calloc((size_t)t.ncomp, sizeof(*t.comp));
    if (t.comp == NULL) {
        err = -ENOMEM;
        goto fail;
    }
    for (ci = 0; ci < t.ncomp; ci++) {
        err = comp_init(&t.comp[ci], j, tx0, ty0, tx1, ty1, ci);
        if (err) {
            goto fail;
        }
    }
    err = tile_parse_packets(&t, &j->main_h.cod);
    if (err) {
        goto fail;
    }
    for (ci = 0; ci < t.ncomp; ci++) {
        struct dec_comp *c = &t.comp[ci];

        for (r = 0; r < c->nres; r++) {
            struct dec_res *res = &c->res[r];

            for (b = 0; b < res->numbands; b++) {
                struct dec_band *band = &res->b[b];

                if (band->prec == NULL) {
                    continue;
                }
                for (p = 0; p < res->pw * res->ph; p++) {
                    struct dec_pband *pb = &band->prec[p].pb[band->bandno];

                    for (k = 0; k < pb->cw * pb->ch; k++) {
                        struct dec_cblk *cb = &pb->cb[k];

                        if (cb->numpasses > 0) {
                            err = t1_decode_cblk(cb);
                            if (err) {
                                goto fail;
                            }
                        }
                    }
                }
            }
        }
        err = comp_place_bands(c);
        if (err) {
            goto fail;
        }
        if (j->main_h.cod.p.transform == 0) {
            /* 9-7 irreversible: dequantize, IDWT in double, store with rounding */
            double *buf_d = malloc((size_t)c->w * c->h * sizeof(*buf_d));
            int comp_precision = (int)s->comps[ci].depth + 1;

            if (buf_d == NULL) {
                err = -ENOMEM;
                goto fail;
            }
            comp_dequantize(c, &j->main_h.qcd, comp_precision, buf_d);
            err = comp_dwt97(c, buf_d);
            if (err) {
                free(buf_d);
                goto fail;
            }
            err = comp_store97(c, buf_d, planes[ci], pw[ci], ph[ci],
                               px0[ci], py0[ci]);
            free(buf_d);
            if (err) {
                goto fail;
            }
        } else {
            /* 5-3 reversible path (zero quantization step, integer DWT) */
            err = comp_dwt(c);
            if (err) {
                goto fail;
            }
            err = comp_store(c, planes[ci], pw[ci], ph[ci], px0[ci], py0[ci]);
            if (err) {
                goto fail;
            }
        }
    }
    tile_free(&t);
    return 0;

fail:
    tile_free(&t);
    return err;
}

static int
dec_clamp(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* public entry point                                                   */
/* ------------------------------------------------------------------ */

int
jp2_decode(struct pic *p, JP2 *j, FILE *f)
{
    struct siz *s = &j->main_h.siz;
    struct cod *cod = &j->main_h.cod;
    struct qcd *q = &j->main_h.qcd;
    int32_t *planes[3] = {NULL, NULL, NULL};
    int pw[3], ph[3], px0[3], py0[3];
    int ncomp, W, H, pitch, ci, x, y;
    long flen;
    int err = 0;

    if (p == NULL || j == NULL || f == NULL) {
        return -EINVAL;
    }
    if (s->comps == NULL || s->component_num == 0) {
        return -EINVAL;
    }
    ncomp = (int)s->component_num;
    if (ncomp != 1 && ncomp != 3) {
        return -ENOTSUP;
    }
    if (s->width == 0 || s->height == 0 ||
        s->width > JP2_MAX_DIM || s->height > JP2_MAX_DIM) {
        return -ENOTSUP;
    }
    if (s->left != 0 || s->top != 0) {
        return -ENOTSUP;
    }
    if (s->tile_width == 0 || s->tile_height == 0) {
        return -EINVAL;
    }
    if (j->tile_nums <= 0 || j->tiles == NULL || j->tile_nums > 4096) {
        return -ENOTSUP;
    }
    if (cod->p.transform != 0 && cod->p.transform != 1) {
        /* only 9-7 irreversible and 5-3 reversible wavelets */
        return -ENOTSUP;
    }
    if (cod->layers_num != 1 || cod->progression_order != 0) {
        return -ENOTSUP;
    }
    if (cod->p.decomp_level_num >= JP2_MAX_LEVELS) {
        return -ENOTSUP;
    }
    if (cod->p.code_block_width > 8 || cod->p.code_block_height > 8) {
        return -ENOTSUP;
    }
    if (cod->p.selctive_arithmetic || cod->p.reset_on_boundary ||
        cod->p.termination || cod->p.vertical_context ||
        cod->p.predictable_termination || cod->p.segmentation_symbol) {
        return -ENOTSUP;
    }
    if (q->table == NULL || (q->quant_type != 0 && q->quant_type != 1 &&
                             q->quant_type != 2)) {
        return -ENOTSUP;
    }
    if (ncomp == 1 && cod->multiple_transform) {
        return -EINVAL;
    }
    W = (int)s->width;
    H = (int)s->height;
    if ((int64_t)W * H > (1 << 26)) {
        return -ENOTSUP;
    }
    for (ci = 0; ci < ncomp; ci++) {
        struct scomponent *sc = &s->comps[ci];

        if (sc->horizontal_separation != 1 ||
            sc->vertical_separation != 1) {
            return -ENOTSUP;
        }
        if ((int)(sc->depth & 0x7f) + 1 != 8) {
            return -ENOTSUP;
        }
        pw[ci] = W;
        ph[ci] = H;
        px0[ci] = 0;
        py0[ci] = 0;
        planes[ci] = calloc((size_t)W * H, sizeof(*planes[ci]));
        if (planes[ci] == NULL) {
            err = -ENOMEM;
            goto fail;
        }
    }

    fseek(f, 0, SEEK_END);
    flen = ftell(f);
    if (flen < 0) {
        err = -EINVAL;
        goto fail;
    }

    for (ci = 0; ci < j->tile_nums; ci++) {
        err = tile_decode(j, &j->tiles[ci], f, flen, planes, pw, ph,
                          px0, py0);
        if (err) {
            goto fail;
        }
    }

    pitch = ((W * 32 + 31) >> 5) << 2;
    p->pixels = calloc(1, (size_t)pitch * H);
    if (p->pixels == NULL) {
        err = -ENOMEM;
        goto fail;
    }
    for (y = 0; y < H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)p->pixels +
                                     (size_t)y * pitch);

        for (x = 0; x < W; x++) {
            int r, g, b;

            if (ncomp == 1) {
                int v = dec_clamp(planes[0][(size_t)y * W + x] + 128,
                                  0, 255);

                r = g = b = v;
            } else if (cod->multiple_transform &&
                       cod->p.transform == 1) {
                /* inverse RCT, before the level shift */
                int yy = planes[0][(size_t)y * W + x];
                int uu = planes[1][(size_t)y * W + x];
                int vv = planes[2][(size_t)y * W + x];
                int gg = yy - ((uu + vv) >> 2);

                r = dec_clamp(vv + gg + 128, 0, 255);
                g = dec_clamp(gg + 128, 0, 255);
                b = dec_clamp(uu + gg + 128, 0, 255);
            } else if (cod->multiple_transform &&
                       cod->p.transform == 0) {
                /* inverse irreversible ICT (YUV → RGB) */
                double yy = planes[0][(size_t)y * W + x];
                double cb = planes[1][(size_t)y * W + x];
                double cr = planes[2][(size_t)y * W + x];
                int ri, gi, bi;

                ri = (int)lrint(yy + 1.402 * cr);
                gi = (int)lrint(yy - 0.34413 * cb - 0.71414 * cr);
                bi = (int)lrint(yy + 1.772 * cb);
                r = dec_clamp(ri + 128, 0, 255);
                g = dec_clamp(gi + 128, 0, 255);
                b = dec_clamp(bi + 128, 0, 255);
            } else {
                r = dec_clamp(planes[0][(size_t)y * W + x] + 128, 0, 255);
                g = dec_clamp(planes[1][(size_t)y * W + x] + 128, 0, 255);
                b = dec_clamp(planes[2][(size_t)y * W + x] + 128, 0, 255);
            }
            row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) |
                     (uint32_t)b;
        }
    }
    p->width = W;
    p->height = H;
    p->depth = 32;
    p->pitch = pitch;
    p->format = CS_PIXELFORMAT_RGB888;
    p->left = 0;
    p->top = 0;

    for (ci = 0; ci < ncomp; ci++) {
        free(planes[ci]);
    }
    return 0;

fail:
    for (ci = 0; ci < 3; ci++) {
        free(planes[ci]);
    }
    return err;
}
