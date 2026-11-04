/* Real multi-tile HEVC bitstream regression.
 *
 * Sample: hevc_tiles_real.265 — x265 coded 4096x1714 IDR, 4×4 tiles (16 tiles,
 * 15 entry_point_offsets).  1193 bytes Annex-B, VPS/SPS/PPS prefix + single IDR
 * slice.  The fixture exercises the tile boundary path in parse_slice_segment_data,
 * where ffpic must honour both the entry_point_offset jump AND the emulation
 * prevention correction (skipped 0x03 bytes inside 00 00 03 patterns).
 *
 * Reference: computed once with `ffmpeg -pix_fmt bgra -f rawvideo`, MD5 pinned
 * below.  A full 28 MB BGRA frame is too big for tests/data/, so we pin the hash.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hevc.h"
#include "vlog.h"

enum { W = 4096, H = 1714, PITCH = W * 4 };
static int starts[8192];

static void
split_annexb(const uint8_t *data, int len, int *num)
{
    int i = 0, count = 0;
    while (i < len - 2) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            starts[count++] = i + 3;
            i += 3;
            continue;
        }
        if (i < len - 3 && data[i] == 0 && data[i + 1] == 0 &&
            data[i + 2] == 0 && data[i + 3] == 1) {
            starts[count++] = i + 4;
            i += 4;
            continue;
        }
        i++;
    }
    *num = count;
}

/* MD5 of ffmpeg's BGRA output for hevc_tiles_real.265. */
static const char REF_MD5[] = "7c63d04a908726353194ff39102977b7";

static const char *
md5_hex(const uint8_t *buf, size_t len)
{
    /* Simple FNV-1a... no, we need real MD5. Use OpenSSL or roll our own?
     * Actually, let's just link against libssl if available, or use a tiny
     * public-domain MD5 implementation.  To keep the test self-contained
     * without new deps, we implement the RFC 1321 reference MD5. */
    static const uint32_t T[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };
    static const uint32_t s[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    uint8_t bufpad[64];
    uint64_t bit_len = (uint64_t)len * 8;
    size_t pad = (len % 64 < 56) ? (56 - len % 64) : (120 - len % 64);
    size_t total = len + pad + 8;
    uint8_t *full = malloc(total);
    if (!full) return "OOM";
    memcpy(full, buf, len);
    full[len] = 0x80;
    memset(full + len + 1, 0, pad);
    for (int i = 0; i < 8; i++)
        full[len + pad + i] = (uint8_t)(bit_len >> (i * 8));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            M[i] = (uint32_t)full[off + i*4]
                 | ((uint32_t)full[off + i*4 + 1] << 8)
                 | ((uint32_t)full[off + i*4 + 2] << 16)
                 | ((uint32_t)full[off + i*4 + 3] << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t f, g;
            if (i < 16)      { f = (B & C) | ((~B) & D);         g = i; }
            else if (i < 32) { f = (D & B) | ((~D) & C);         g = (5*i + 1) % 16; }
            else if (i < 48) { f = B ^ C ^ D;                    g = (3*i + 5) % 16; }
            else             { f = C ^ (B | (~D));               g = (7*i) % 16; }
            f = f + A + T[i] + M[g];
            uint32_t tmp = D; D = C; C = B;
            B = B + ((f << s[i]) | (f >> (32 - s[i])));
            A = tmp;
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    free(full);
    static char out[33];
    uint8_t digest[16];
    memcpy(digest, &a0, 4); memcpy(digest + 4, &b0, 4);
    memcpy(digest + 8, &c0, 4); memcpy(digest + 12, &d0, 4);
    for (int i = 0; i < 16; i++)
        sprintf(out + i*2, "%02x", digest[i]);
    out[32] = 0;
    return out;
}

int main(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/hevc_tiles_real.265", TEST_DATA_DIR);

    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    uint8_t *data = malloc((size_t)sz);
    if (!data || fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(data); return -1;
    }
    fclose(f);

    int num_starts = 0;
    split_annexb(data, (int)sz, &num_starts);
    if (num_starts == 0) {
        printf("%s: no Annex-B start codes\n", path);
        free(data); return -1;
    }

    struct hevc_param_set hps;
    memset(&hps, 0, sizeof(hps));
    uint8_t *bgra = calloc((size_t)PITCH * H, 1);
    if (!bgra) { free(data); return -1; }

    int slice_count = 0;
    for (int i = 0; i < num_starts; i++) {
        int next = (i + 1 < num_starts) ? starts[i + 1] : (int)sz;
        int nalu_len = next - starts[i];
        uint8_t *nalu = data + starts[i];
        if (nalu_len < 2) continue;
        int nunit_type = (nalu[0] >> 1) & 0x3F;
        int is_slice = (nunit_type >= 1 && nunit_type <= 9) ||
                       (nunit_type >= 16 && nunit_type <= 21) ||
                       nunit_type == 38;
        int rc = parse_nalu(nalu, nalu_len, is_slice ? bgra : NULL,
                            PITCH, H, NULL, &hps, 32);
        if (is_slice) slice_count++;
    }

    if (slice_count == 0) {
        printf("%s: no slice decoded\n", path);
        free(bgra); free(data); return -1;
    }

    const char *got = md5_hex(bgra, (size_t)PITCH * H);
    int rc = 0;
    if (strcmp(got, REF_MD5) != 0) {
        printf("%s: MD5 mismatch got %s want %s\n", path, got, REF_MD5);
        rc = -1;
    } else {
        printf("%s: ok, MD5 %s, %d slice(s)\n", path, got, slice_count);
    }

    free_hevc_param_set(&hps);
    free(bgra); free(data);
    return rc;
}
