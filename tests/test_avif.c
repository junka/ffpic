/* Real libaom still images in the AVIF container, each paired with the
 * bgra frame ffmpeg's own av1 decoder produces.  Both files come from
 * tests/gen_avif_samples.py.  The samples cover loop filtering, tiles, an
 * odd size that goes through the conformance window crop, coarse
 * quantization, lossless coding, 4:4:4, 4:0:0, 10 and 12 bit output, and
 * the container cases: a generic HEIF major brand, a split item, an alpha
 * item and a 32 bit primary item id.  Lossy coding moves pixels, so every
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
        /* derived containers: these decode to the mixed picture */
        {"avif_mif1", 100, 62, 6, 0, 0, 0},
        {"avif_extents", 100, 62, 6, 0, 0, 0},
        {"avif_aux", 100, 62, 6, 0, 0, 0},
        {"avif_pitm32", 100, 62, 6, 0, 0, 0},
        {"avif_alpha", 100, 62, 6, 0, 1, 0},
        /* the gamma curve bends the existing rounding, so allow a bit more */
        {"avif_icc", 100, 62, 8, 0, 0, 0},
        {"avif_icc10", 96, 96, 1024, 0, 0, 1},
        {"avif_alpha10", 96, 96, 1024, 0, 1, 1},
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
            ops->info(inf, p);
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

int main(void)
{
    vlog_init();
    if (test_real_samples()) {
        return -1;
    }
    return 0;
}
