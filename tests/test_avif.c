/* Real libaom still images in the AVIF container, each paired with the
 * bgra frame ffmpeg's own av1 decoder produces.  Both files come from
 * tests/gen_avif_samples.py.  The samples cover loop filtering, tiles, an
 * odd size that goes through the conformance window crop, coarse
 * quantization, lossless coding, 4:4:4, 4:0:0, and the container cases:
 * a generic HEIF major brand, a split item, and an alpha item.  Lossy
 * coding moves pixels, so every sample carries its own tolerance and the
 * whole frame is compared, not just a few points. */

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
        int mono, alpha;
    } cases[] = {
        {"avif_gradient", 96, 96, 6, 0, 0},
        {"avif_noise", 96, 96, 6, 0, 0},
        {"avif_mixed", 100, 62, 6, 0, 0},
        {"avif_tiles", 256, 192, 6, 0, 0},
        {"avif_highqp", 96, 96, 6, 0, 0},
        {"avif_lossless", 64, 64, 4, 0, 0},
        {"avif_yuv444", 96, 96, 6, 0, 0},
        {"avif_mono", 96, 96, 6, 1, 0},
        /* derived containers: these decode to the mixed picture */
        {"avif_mif1", 100, 62, 6, 0, 0},
        {"avif_extents", 100, 62, 6, 0, 0},
        {"avif_aux", 100, 62, 6, 0, 0},
        {"avif_pitm32", 100, 62, 6, 0, 0},
        {"avif_alpha", 100, 62, 6, 0, 1},
        /* the gamma curve bends the existing rounding, so allow a bit more */
        {"avif_icc", 100, 62, 8, 0, 0},
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
        size_t ref_size = (size_t)cases[i].w * cases[i].h * 4;
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
        for (int y = 0; y < cases[i].h && !bad; y++) {
            uint8_t *row = (uint8_t *)p->pixels + (size_t)y * p->pitch;
            const uint8_t *rrow = ref + (size_t)y * cases[i].w * 4;
            for (int x = 0; x < cases[i].w && !bad; x++) {
                for (int c = 0; c < (cases[i].alpha ? 4 : 3); c++) {
                    int d = abs(row[x * 4 + c] - rrow[x * 4 + c]);
                    if (d > maxdiff) {
                        maxdiff = d;
                    }
                    if (d > cases[i].tol) {
                        printf("%s: (%d,%d) channel %d is %d, reference %d, "
                               "max diff %d exceeds %d\n", path, x, y, c,
                               row[x * 4 + c], rrow[x * 4 + c], d,
                               cases[i].tol);
                        bad = 1;
                        rc = -1;
                        break;
                    }
                }
                if (cases[i].mono) {
                    if (row[x * 4] != row[x * 4 + 1] ||
                        row[x * 4 + 1] != row[x * 4 + 2]) {
                        printf("%s: (%d,%d) grey came out %d %d %d\n", path, x,
                               y, row[x * 4], row[x * 4 + 1],
                               row[x * 4 + 2]);
                        bad = 1;
                        rc = -1;
                        break;
                    }
                } else if (!cases[i].alpha && row[x * 4 + 3] != 0xFF) {
                    printf("%s: (%d,%d) alpha %d, expected 255\n", path, x, y,
                           row[x * 4 + 3]);
                    bad = 1;
                    rc = -1;
                    break;
                }
            }
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
