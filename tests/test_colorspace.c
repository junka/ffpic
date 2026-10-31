/* Colour management: an ICC matrix/TRC profile, the CICP fallbacks and the
 * PQ/HLG tone mapper have to bring the decoded samples into sRGB.  The
 * expected values below were computed independently with the ICC/CIE and
 * BT.2100 formulas; the ICC file itself comes from
 * tests/gen_avif_samples.py. */

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

    /* a non-profile blob must not parse */
    if (cs_profile_from_icc("junk", 4) != NULL) {
        printf("a junk ICC blob parsed\n");
        rc = -1;
    }
    return rc;
}

static int
test_hdr(void)
{
    struct cs_profile *p;
    uint16_t px[8];
    int rc = 0;

    /* 1000 and 203 cd/m2 in PQ, both grey.  The content peak is the first
     * sample, so it maps to SDR white and 203 cd/m2 maps to 55400. */
    p = cs_profile_from_cicp(9, 16);
    if (!p || cs_profile_is_identity(p)) {
        printf("PQ transfer is not mapped\n");
        return -1;
    }
    px[0] = px[1] = px[2] = 49271;
    px[3] = 65535;
    px[4] = px[5] = px[6] = 38055;
    px[7] = 65535;
    cs_profile_apply_bgra16(p, (unsigned char *)px, 2, 1, 16);
    if (!near(px[0], 65535, 2) || !near(px[4], 55400, 2) ||
        px[3] != 65535 || px[7] != 65535) {
        printf("PQ tone map came out %d and %d\n", px[0], px[4]);
        rc = -1;
    }
    cs_profile_free(p);

    /* HLG: 75% signal is the 203 cd/m2 reference white and 50% is
     * 50.7 cd/m2, which maps to 42484. */
    p = cs_profile_from_cicp(9, 18);
    if (!p || cs_profile_is_identity(p)) {
        printf("HLG transfer is not mapped\n");
        return -1;
    }
    px[0] = px[1] = px[2] = 49151;
    px[3] = 65535;
    px[4] = px[5] = px[6] = 32768;
    px[7] = 65535;
    cs_profile_apply_bgra16(p, (unsigned char *)px, 2, 1, 16);
    if (!near(px[0], 65535, 2) || !near(px[4], 42484, 2) ||
        px[3] != 65535 || px[7] != 65535) {
        printf("HLG tone map came out %d and %d\n", px[0], px[4]);
        rc = -1;
    }
    cs_profile_free(p);
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
    if (test_hdr()) {
        return -1;
    }
    printf("colorspace: ok\n");
    return 0;
}
