/* Colour management: an ICC matrix/TRC profile and the CICP fallbacks have
 * to bring the decoded samples into sRGB.  The expected values below were
 * computed independently with the ICC/CIE formulas; the ICC file itself
 * comes from tests/gen_avif_samples.py. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorspace.h"

static int
near(int got, int want, int tol)
{
    return abs(got - want) <= tol;
}

static int
test_icc(void)
{
    char path[512];
    FILE *f;
    long len;
    unsigned char *data;
    struct cs_profile *p;
    unsigned char px[4] = {0, 0, 128, 255};   /* B, G, R, A */
    int rc = 0;

    snprintf(path, sizeof(path), "%s/icc_gamma18.icc", TEST_DATA_DIR);
    f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)len);
    if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
        printf("cannot read %s\n", path);
        fclose(f);
        free(data);
        return -1;
    }
    fclose(f);

    p = cs_profile_from_icc(data, (size_t)len);
    free(data);
    if (!p) {
        printf("the gamma 1.8 profile did not parse\n");
        return -1;
    }
    if (cs_profile_is_identity(p)) {
        printf("the gamma 1.8 profile is treated as identity\n");
        rc = -1;
    }
    cs_profile_apply_bgra(p, px, 1, 1, 4);
    /* 128/255 ^ 1.8 = 0.289, sRGB encoded is 0.574 -> 146 */
    if (!near(px[2], 146, 1) || px[0] != 0 || px[1] != 0 ||
        px[3] != 255) {
        printf("grey through the profile came out %d %d %d %d\n",
               px[0], px[1], px[2], px[3]);
        rc = -1;
    }
    cs_profile_free(p);
    return rc;
}

static int
test_cicp(void)
{
    struct cs_profile *p;
    unsigned char px[4] = {51, 77, 153, 255};   /* B, G, R, A */
    int rc = 0;

    /* unspecified primaries and transfer follow the AV1 defaults */
    p = cs_profile_from_cicp(2, 2);
    if (!p || !cs_profile_is_identity(p)) {
        printf("unspecified CICP is not identity\n");
        rc = -1;
    }
    cs_profile_free(p);

    /* BT.2020 primaries with the sRGB transfer */
    p = cs_profile_from_cicp(9, 13);
    if (!p || cs_profile_is_identity(p)) {
        printf("BT.2020 primaries are treated as identity\n");
        return -1;
    }
    cs_profile_apply_bgra(p, px, 1, 1, 4);
    /* (153,77,51) in BT.2020 maps to (185,59,43) in sRGB */
    if (!near(px[2], 185, 2) || !near(px[1], 59, 2) ||
        !near(px[0], 43, 2)) {
        printf("BT.2020 colour came out %d %d %d\n", px[0], px[1], px[2]);
        rc = -1;
    }
    cs_profile_free(p);

    /* PQ needs tone mapping, which is not implemented */
    if (cs_profile_from_cicp(9, 16) != NULL) {
        printf("PQ transfer was reported as supported\n");
        rc = -1;
    }

    /* a non-profile blob must not parse */
    if (cs_profile_from_icc("junk", 4) != NULL) {
        printf("a junk ICC blob parsed\n");
        rc = -1;
    }
    return rc;
}

int
main(void)
{
    if (test_icc()) {
        return -1;
    }
    if (test_cicp()) {
        return -1;
    }
    printf("colorspace: ok\n");
    return 0;
}
