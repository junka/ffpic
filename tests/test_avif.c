/* Real libaom still images in the AVIF container, each paired with the
 * bgra frame ffmpeg's own av1 decoder produces.  Both files come from
 * tests/gen_avif_samples.py.  The samples cover loop filtering, tiles, an
 * odd size that goes through the conformance window crop, coarse
 * quantization, lossless coding, 4:4:4, 4:0:0, 10 and 12 bit output, film
 * grain synthesis, the container cases (a generic HEIF major brand, a split
 * item, an alpha item, a 32 bit primary item id, a sequence header kept in
 * av1C configOBUs, and Exif/XMP/thumbnail metadata), the transformative
 * properties irot, imir and clap, and PQ/HLG streams tone mapped to SDR.
 * Lossy coding moves pixels, so every
 * sample carries its own tolerance and the whole frame is compared, not
 * just a few points.  High bit depth pictures are compared in 16 bit
 * units, and the check also rejects an 8 bit picture widened to 16 bits. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorspace.h"
#include "file.h"
#include "vlog.h"

/* What the metadata report must say for each metadata fixture. */
static int
check_metadata_report(const char *name, const char *txt)
{
    if (strcmp(name, "avif_meta") == 0) {
        if (!strstr(txt, "orientation 6") ||
            !strstr(txt, "application/rdf+xml") ||
            !strstr(txt, "thumbnail item 4")) {
            return -1;
        }
    } else if (strcmp(name, "avif_meta2") == 0) {
        /* the non-RDF mime item is not XMP */
        if (!strstr(txt, "orientation 8") || strstr(txt, "xmp item")) {
            return -1;
        }
    }
    return 0;
}

static int
test_real_samples(void)
{
    static const struct {
        const char *name;
        int w, h, tol;
        int mono, alpha, wide;
    } cases[] = {
        {"avif_gradient", 96, 96, 6, 0, 0, 0},
        {"avif_noise", 96, 96, 6, 0, 0, 0},
        {"avif_mixed", 100, 62, 6, 0, 0, 0},
        {"avif_tiles", 256, 192, 6, 0, 0, 0},
        {"avif_highqp", 96, 96, 6, 0, 0, 0},
        {"avif_lossless", 64, 64, 4, 0, 0, 0},
        {"avif_yuv444", 96, 96, 6, 0, 0, 0},
        {"avif_mono", 96, 96, 6, 1, 0, 0},
        /* 16 bit output: the tolerance is in 16 bit units */
        {"avif_tenbit", 96, 96, 1024, 0, 0, 1},
        {"avif_twelvebit", 96, 96, 1024, 0, 0, 1},
        /* film grain synthesis, including a partial grain stripe */
        {"avif_grain1", 96, 100, 6, 0, 0, 0},
        {"avif_grain15", 96, 100, 6, 0, 0, 0},
        {"avif_grain16", 96, 100, 6, 0, 0, 0},
        {"avif_grain5_10", 96, 100, 1024, 0, 0, 1},
        /* derived containers: these decode to the mixed picture */
        {"avif_mif1", 100, 62, 6, 0, 0, 0},
        {"avif_extents", 100, 62, 6, 0, 0, 0},
        {"avif_aux", 100, 62, 6, 0, 0, 0},
        {"avif_pitm32", 100, 62, 6, 0, 0, 0},
        /* the sequence header only in the av1C configOBUs */
        {"avif_cfgobu", 100, 62, 6, 0, 0, 0},
        /* Exif, XMP and a thumbnail item beside the primary picture */
        {"avif_meta", 100, 62, 6, 0, 0, 0},
        /* big-endian Exif with an offset, and a mime item that is not XMP */
        {"avif_meta2", 100, 62, 6, 0, 0, 0},
        {"avif_alpha", 100, 62, 6, 0, 1, 0},
        /* the gamma curve bends the existing rounding, so allow a bit more */
        {"avif_icc", 100, 62, 8, 0, 0, 0},
        {"avif_icc10", 96, 96, 1024, 0, 0, 1},
        {"avif_alpha10", 96, 96, 1024, 0, 1, 1},
        /* transformative properties, applied in ipma association order */
        {"avif_irot90", 62, 100, 6, 0, 0, 0},
        {"avif_irot180", 100, 62, 6, 0, 0, 0},
        {"avif_irot270", 62, 100, 6, 0, 0, 0},
        {"avif_imir0", 100, 62, 6, 0, 0, 0},
        {"avif_imir1", 100, 62, 6, 0, 0, 0},
        {"avif_clap", 60, 40, 6, 0, 0, 0},
        {"avif_clapneg", 60, 40, 6, 0, 0, 0},
        {"avif_rotimir", 62, 100, 6, 0, 0, 0},
        {"avif_irot90_10", 96, 96, 1024, 0, 0, 1},
        /* HDR: PQ and HLG tone mapped to SDR sRGB; a few out-of-gamut
         * samples clip differently where the PQ curve is very steep */
        {"avif_pq10", 96, 96, 2048, 0, 0, 1},
        {"avif_hlg10", 96, 96, 2048, 0, 0, 1},
        /* an explicit BT.709 matrix, not the unspecified fallback */
        {"avif_bt709", 96, 96, 8, 0, 0, 0},
    };
    int rc = 0;

    file_ops_init();

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char path[512], refpath[512];
        snprintf(path, sizeof(path), "%s/%s.avif", TEST_DATA_DIR,
                 cases[i].name);
        snprintf(refpath, sizeof(refpath), "%s/%s.ref", TEST_DATA_DIR,
                 cases[i].name);

        struct file_ops *ops = file_probe(path);
        if (!ops || strcmp(ops->name, "AVIF")) {
            printf("%s: not detected as AVIF\n", path);
            rc = -1;
            continue;
        }
        struct pic *p = file_load(ops, path, 0);
        if (!p) {
            printf("%s: did not decode\n", path);
            rc = -1;
            continue;
        }
        int want_fmt = cases[i].wide ? CS_PIXELFORMAT_BGRA64
                                     : CS_PIXELFORMAT_BGRA_BYTES;
        if (p->format != want_fmt) {
            printf("%s: format %d, expected %d\n", path, p->format, want_fmt);
            rc = -1;
            goto next;
        }
        if (p->width != cases[i].w || p->height != cases[i].h) {
            printf("%s: decoded as %dx%d, expected %dx%d\n", path, p->width,
                   p->height, cases[i].w, cases[i].h);
            rc = -1;
            goto next;
        }

        /* The metadata report walks the parsed boxes; keep it covered so a
         * broken box tree cannot crash the picinfo front end. */
        FILE *inf = tmpfile();
        if (inf) {
            long sz;
            ops->info(inf, p);
            fflush(inf);
            sz = ftell(inf);
            if (sz > 0 && strncmp(cases[i].name, "avif_meta", 9) == 0) {
                char *txt = malloc((size_t)sz + 1);
                rewind(inf);
                if (!txt || fread(txt, 1, (size_t)sz, inf) != (size_t)sz) {
                    rc = -1;
                } else {
                    txt[sz] = 0;
                    if (check_metadata_report(cases[i].name, txt)) {
                        printf("%s: metadata report is wrong\n", path);
                        rc = -1;
                    }
                }
                free(txt);
            }
            fclose(inf);
        }

        FILE *f = fopen(refpath, "rb");
        if (!f) {
            printf("%s: cannot open the reference\n", refpath);
            rc = -1;
            goto next;
        }
        size_t bpp = cases[i].wide ? 8 : 4;
        size_t ref_size = (size_t)cases[i].w * cases[i].h * bpp;
        uint8_t *ref = malloc(ref_size);
        if (!ref || fread(ref, 1, ref_size, f) != ref_size) {
            printf("%s: could not read the reference\n", refpath);
            free(ref);
            fclose(f);
            rc = -1;
            goto next;
        }
        fclose(f);

        int maxdiff = 0;
        int bad = 0;
        long deep = 0, color_samples = 0;
        for (int y = 0; y < cases[i].h && !bad; y++) {
            uint8_t *row = (uint8_t *)p->pixels + (size_t)y * p->pitch;
            const uint8_t *rrow = ref + (size_t)y * cases[i].w * bpp;
            for (int x = 0; x < cases[i].w && !bad; x++) {
                int ours[4], theirs[4];
                int channels = cases[i].alpha ? 4 : 3;
                if (cases[i].wide) {
                    const uint16_t *o = (const uint16_t *)(row + (size_t)x * 8);
                    const uint16_t *r =
                        (const uint16_t *)(rrow + (size_t)x * 8);
                    /* ours is B,G,R,A; the reference is R,G,B,A */
                    ours[0] = o[0]; ours[1] = o[1];
                    ours[2] = o[2]; ours[3] = o[3];
                    theirs[0] = r[2]; theirs[1] = r[1];
                    theirs[2] = r[0]; theirs[3] = r[3];
                    color_samples += 3;
                    for (int c = 0; c < 3; c++) {
                        if ((ours[c] & 0xFF) != (ours[c] >> 8)) {
                            deep++;
                        }
                    }
                } else {
                    for (int c = 0; c < 4; c++) {
                        ours[c] = row[x * 4 + c];
                        theirs[c] = rrow[x * 4 + c];
                    }
                }
                for (int c = 0; c < channels; c++) {
                    int d = abs(ours[c] - theirs[c]);
                    if (d > maxdiff) {
                        maxdiff = d;
                    }
                    if (d > cases[i].tol) {
                        printf("%s: (%d,%d) channel %d is %d, reference %d, "
                               "max diff %d exceeds %d\n", path, x, y, c,
                               ours[c], theirs[c], d, cases[i].tol);
                        bad = 1;
                        rc = -1;
                        break;
                    }
                }
                if (cases[i].mono) {
                    if (ours[0] != ours[1] || ours[1] != ours[2]) {
                        printf("%s: (%d,%d) grey came out %d %d %d\n", path, x,
                               y, ours[0], ours[1], ours[2]);
                        bad = 1;
                        rc = -1;
                        break;
                    }
                } else if (!cases[i].alpha &&
                           ours[3] != (cases[i].wide ? 0xFFFF : 0xFF)) {
                    printf("%s: (%d,%d) alpha %d, expected %d\n", path, x, y,
                           ours[3], cases[i].wide ? 0xFFFF : 0xFF);
                    bad = 1;
                    rc = -1;
                    break;
                }
            }
        }
        /* A picture widened from 8 bits has 0xXX repeating in each 16 bit
         * sample; a real high bit depth conversion does not. */
        if (!bad && cases[i].wide && deep * 2 <= color_samples) {
            printf("%s: only %ld of %ld samples carry more than 8 bits\n",
                   path, deep, color_samples);
            bad = 1;
            rc = -1;
        }
        free(ref);
        if (!bad) {
            printf("%s: ok, max channel diff %d\n", cases[i].name, maxdiff);
        }
next:
        file_free(ops, p);
    }
    return rc;
}

/* A protected item cannot be decoded without its key; the loader must fall
 * back to the container view and still report the protection. */
static int
test_protected(void)
{
    char path[512];
    struct file_ops *ops;
    struct pic *p;
    FILE *inf;
    long sz;
    char *txt;
    int rc = 0;

    snprintf(path, sizeof(path), "%s/avif_prot.avif", TEST_DATA_DIR);
    ops = file_probe(path);
    if (!ops || strcmp(ops->name, "AVIF")) {
        printf("%s: not detected as AVIF\n", path);
        return -1;
    }
    p = file_load(ops, path, 0);
    if (!p) {
        printf("%s: a protected item broke the load\n", path);
        return -1;
    }
    if (p->width != 100 || p->height != 62) {
        printf("%s: protected picture came out %dx%d\n", path, p->width,
               p->height);
        rc = -1;
    }
    inf = tmpfile();
    if (!inf) {
        file_free(ops, p);
        return -1;
    }
    ops->info(inf, p);
    fflush(inf);
    sz = ftell(inf);
    txt = malloc((size_t)(sz > 0 ? sz : 0) + 1);
    rewind(inf);
    if (txt && sz > 0 && fread(txt, 1, (size_t)sz, inf) == (size_t)sz) {
        txt[sz] = 0;
        if (!strstr(txt, "protection entries 1") ||
            !strstr(txt, "item_protection_index=1")) {
            printf("%s: protection was not reported\n", path);
            rc = -1;
        }
    } else {
        rc = -1;
    }
    free(txt);
    fclose(inf);
    file_free(ops, p);
    return rc;
}

/* tests/data/avif_grid.avif is avif_lossless.avif wrapped as a 2x2 'grid'
 * derived image.  Every quadrant must equal the single-tile picture. */
static int
test_grid(void)
{
    const char *grid_path = TEST_DATA_DIR "/avif_grid.avif";
    const char *tile_path = TEST_DATA_DIR "/avif_lossless.avif";
    struct file_ops *gops = file_probe(grid_path);
    struct file_ops *tops = file_probe(tile_path);
    int rc = 0;

    if (!gops || strcmp(gops->name, "AVIF") ||
        !tops || strcmp(tops->name, "AVIF")) {
        printf("grid: probe failed\n");
        return -1;
    }
    struct pic *g = file_load(gops, grid_path, 0);
    struct pic *t = file_load(tops, tile_path, 0);
    if (!g || !t) {
        printf("grid: load failed\n");
        rc = -1;
        goto out;
    }
    if (g->width != 128 || g->height != 128 ||
        t->width != 64 || t->height != 64) {
        printf("grid: got %dx%d from a 64x64 tile, expected 128x128\n",
               g->width, g->height);
        rc = -1;
        goto out;
    }
    for (int y = 0; y < 128 && !rc; y++) {
        uint8_t *grow = (uint8_t *)g->pixels + (size_t)y * g->pitch;
        uint8_t *trow = (uint8_t *)t->pixels + (size_t)(y % 64) * t->pitch;
        for (int x = 0; x < 128 && !rc; x++) {
            if (memcmp(grow + (size_t)x * 4,
                       trow + (size_t)(x % 64) * 4, 4) != 0) {
                printf("grid: pixel %d,%d differs from the tile\n", x, y);
                rc = -1;
            }
        }
    }
    if (!rc) {
        printf("OK[avif_grid]: 2x2 grid composed from four tiles\n");
    }
out:
    if (g) file_free(gops, g);
    if (t) file_free(tops, t);
    return rc;
}

/* tests/data/avif_anim.avif is an all-intra AVIF sequence whose major brand
 * is 'avis' and whose compatible brands do NOT list avis: the loader has to
 * look at the major brand to take the track path.  The first frame is
 * returned, the rest are queued. */
static int
test_animation(void)
{
    const char *path = TEST_DATA_DIR "/avif_anim.avif";
    struct file_ops *ops = file_probe(path);
    int rc = 0, frames = 0, distinct = 0;
    uint64_t prev = 0;
    struct pic *p, *q;

    if (!ops || strcmp(ops->name, "AVIF")) {
        printf("%s: not detected as AVIF\n", path);
        return -1;
    }
    p = file_load(ops, path, 0);
    if (!p) {
        printf("%s: the first animation frame was not returned\n", path);
        rc = -1;
    }
    while ((q = p ? p : file_dequeue_pic()) != NULL) {
        frames++;
        if (q->width != 64 || q->height != 48 || q->duration_ms != 125) {
            printf("%s: frame %d is %dx%d duration %u\n", path, frames,
                   q->width, q->height, q->duration_ms);
            rc = -1;
        } else {
            uint64_t h = 1469598103934665603ULL;
            uint8_t *px = (uint8_t *)q->pixels;
            for (size_t i = 0; i < (size_t)q->pitch * q->height; i++) {
                h = (h ^ px[i]) * 1099511628211ULL;
            }
            if (prev && h != prev) {
                distinct++;
            }
            prev = h;
        }
        file_free(ops, q);
        p = NULL;
    }
    if (frames != 8) {
        printf("%s: %d frames, expected 8\n", path, frames);
        rc = -1;
    }
    if (distinct == 0) {
        printf("%s: every frame is identical\n", path);
        rc = -1;
    }
    if (!rc) {
        printf("OK[avif_anim]: %d frames, 125 ms each\n", frames);
    }
    return rc;
}

/* tests/data/avif_anim_alpha.avif carries a second, auxv-handled AV1 track
 * with the alpha plane.  The first frame is returned and the rest queued;
 * every frame must have non-opaque alpha. */
static int
check_alpha_frame(const char *path, struct pic *p, int frame, int *rc)
{
    if (!p) {
        printf("%s: frame %d is missing\n", path, frame);
        *rc = -1;
        return -1;
    }
    if (p->width != 64 || p->height != 48 || p->duration_ms != 125) {
        printf("%s: frame %d is %dx%d duration %u\n", path, frame,
               p->width, p->height, p->duration_ms);
        *rc = -1;
    }
    int amin = 255, amax = 0;
    long color = 0;
    for (int y = 0; y < p->height; y++) {
        uint8_t *row = (uint8_t *)p->pixels + (size_t)y * p->pitch;
        for (int x = 0; x < p->width; x++) {
            uint8_t *px = row + (size_t)x * 4;
            if (px[0] || px[1] || px[2]) color++;
            if (px[3] < amin) amin = px[3];
            if (px[3] > amax) amax = px[3];
        }
    }
    if (amin == amax || amin == 255) {
        printf("%s: frame %d alpha is [%d,%d], the alpha track was not "
               "composited\n", path, frame, amin, amax);
        *rc = -1;
    }
    if (color == 0) {
        printf("%s: frame %d has no colour samples\n", path, frame);
        *rc = -1;
    }
    return 0;
}

static int
test_animation_alpha(void)
{
    const char *path = TEST_DATA_DIR "/avif_anim_alpha.avif";
    struct file_ops *ops = file_probe(path);
    int rc = 0, frames = 0;
    struct pic *p, *q;

    if (!ops || strcmp(ops->name, "AVIF")) {
        printf("%s: not detected as AVIF\n", path);
        return -1;
    }
    p = file_load(ops, path, 0);
    if (!p) {
        printf("%s: the first animation frame was not returned\n", path);
        rc = -1;
    }
    while ((q = p ? p : file_dequeue_pic()) != NULL) {
        frames++;
        check_alpha_frame(path, q, frames, &rc);
        file_free(ops, q);
        p = NULL;
    }
    if (frames != 8) {
        printf("%s: %d frames, expected 8\n", path, frames);
        rc = -1;
    }
    if (!rc) {
        printf("OK[avif_anim_alpha]: %d frames with composed alpha\n",
               frames);
    }
    return rc;
}

static struct pic *
first_animation_frame(const char *name, struct file_ops **ops_out)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", TEST_DATA_DIR, name);
    *ops_out = file_probe(path);
    if (!*ops_out)
        return NULL;
    struct pic *p = file_load(*ops_out, path, 0);
    if (!p) {
        p = file_dequeue_pic();
    }
    return p;
}

static void
free_animation_queue(struct file_ops *ops)
{
    struct pic *q;

    while ((q = file_dequeue_pic()) != NULL) {
        file_free(ops, q);
    }
}

static int
check_animation_dims(const char *name, int want_w, int want_h)
{
    struct file_ops *ops = NULL;
    struct pic *p = first_animation_frame(name, &ops);
    int frames = 1, rc = 0;

    if (!p) {
        printf("%s: no frame decoded\n", name);
        free_animation_queue(ops);
        return -1;
    }
    for (;;) {
        if (p->width != want_w || p->height != want_h ||
            p->duration_ms != 125) {
            printf("%s: frame %d is %dx%d duration %u, want %dx%d 125\n",
                   name, frames, p->width, p->height, p->duration_ms,
                   want_w, want_h);
            rc = -1;
        }
        file_free(ops, p);
        p = file_dequeue_pic();
        if (!p)
            break;
        frames++;
    }
    if (frames != 8) {
        printf("%s: %d frames, expected 8\n", name, frames);
        rc = -1;
    }
    if (!rc) {
        printf("OK[%s]: 8 frames, %dx%d, 125 ms\n", name, want_w, want_h);
    }
    return rc;
}

static int
check_animation_rotation(void)
{
    struct file_ops *bops = NULL, *rops = NULL;
    struct pic *base = first_animation_frame("avif_anim.avif", &bops);
    /* drain the rest of the first sequence before loading the second, or
     * its frames would be handed out as the rotated file's first frame */
    if (base) free_animation_queue(bops);
    struct pic *rot = first_animation_frame("avif_anim_rot.avif", &rops);
    int rc = 0;

    if (!base || !rot || base->width != 64 || base->height != 48 ||
        rot->width != 48 || rot->height != 64) {
        printf("rotation: unexpected frame sizes\n");
        rc = -1;
        goto out;
    }
    for (int y = 0; y < 64 && !rc; y += 7) {
        for (int x = 0; x < 48 && !rc; x += 5) {
            const uint8_t *a = (uint8_t *)rot->pixels +
                               (size_t)y * rot->pitch + (size_t)x * 4;
            const uint8_t *b = (uint8_t *)base->pixels +
                               (size_t)(47 - x) * base->pitch +
                               (size_t)y * 4;
            if (memcmp(a, b, 4) != 0) {
                printf("rotation: pixel %d,%d does not match the "
                       "unrotated frame\n", x, y);
                rc = -1;
            }
        }
    }
    if (!rc) {
        printf("OK[avif_anim_rot]: 90 degree display matrix applied\n");
    }
out:
    if (base) file_free(bops, base);
    if (rot) file_free(rops, rot);
    free_animation_queue(bops ? bops : rops);
    return rc;
}

static int
test_animation_variants(void)
{
    int rc = 0;

    if (check_animation_dims("avif_anim_odd.avif", 62, 48))
        rc = -1;
    if (check_animation_dims("avif_anim_rot.avif", 48, 64))
        rc = -1;
    if (check_animation_rotation())
        rc = -1;
    return rc;
}

int main(void)
{
    vlog_init();
    if (test_real_samples()) {
        return -1;
    }
    if (test_grid()) {
        return -1;
    }
    if (test_animation()) {
        return -1;
    }
    if (test_animation_alpha()) {
        return -1;
    }
    if (test_animation_variants()) {
        return -1;
    }
    if (test_protected()) {
        return -1;
    }
    return 0;
}
