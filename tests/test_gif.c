/* test_gif.c — GIF animation regression: frame count, duration_ms, pixel MD5
 *
 * Two minimal 8×8 samples:
 *   gif_anim_transparent.gif   2 frames, transparency + Netscape loop
 *   gif_anim_dispose3.gif      3 frames, disposal = 1 / 3 / 1
 *
 * Pixel MD5s computed against ffmpeg reference (ffmpeg -pix_fmt bgra).
 * ffpic does not link OpenSSL — RFC 1321 reference MD5 embedded below.
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "gif.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "./tests/data"
#endif

/* ====== RFC 1321 MD5 (reference, ~60 LOC) ====== */

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buffer[64];
} MD5_CTX;

static void md5_init(MD5_CTX *c) {
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
    c->count = 0;
}

#define F(x,y,z) (((x)&(y)) | ((~(x))&(z)))
#define G(x,y,z) (((x)&(z)) | ((y)&(~(z))))
#define H(x,y,z) ((x)^(y)^(z))
#define I(x,y,z) ((y)^((x)|(~(z))))
#define ROT(x,n) (((x)<<(n)) | ((x)>>(32-(n))))

static void md5_transform(uint32_t s[4], const uint8_t input[64]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
        x[i] = (uint32_t)input[i*4] | ((uint32_t)input[i*4+1]<<8)
             | ((uint32_t)input[i*4+2]<<16) | ((uint32_t)input[i*4+3]<<24);
    uint32_t a=s[0],b=s[1],c=s[2],d=s[3];
    #define STEP(fn,A,B,C,D,x,s,ac) { A += fn(B,C,D) + x + ac; A = ROT(A,s) + B; }
    STEP(F,a,b,c,d,x[ 0], 7,0xd76aa478); STEP(F,d,a,b,c,x[ 1],12,0xe8c7b756);
    STEP(F,c,d,a,b,x[ 2],17,0x242070db); STEP(F,b,c,d,a,x[ 3],22,0xc1bdceee);
    STEP(F,a,b,c,d,x[ 4], 7,0xf57c0faf); STEP(F,d,a,b,c,x[ 5],12,0x4787c62a);
    STEP(F,c,d,a,b,x[ 6],17,0xa8304613); STEP(F,b,c,d,a,x[ 7],22,0xfd469501);
    STEP(F,a,b,c,d,x[ 8], 7,0x698098d8); STEP(F,d,a,b,c,x[ 9],12,0x8b44f7af);
    STEP(F,c,d,a,b,x[10],17,0xffff5bb1); STEP(F,b,c,d,a,x[11],22,0x895cd7be);
    STEP(F,a,b,c,d,x[12], 7,0x6b901122); STEP(F,d,a,b,c,x[13],12,0xfd987193);
    STEP(F,c,d,a,b,x[14],17,0xa679438e); STEP(F,b,c,d,a,x[15],22,0x49b40821);
    STEP(G,a,b,c,d,x[ 1], 5,0xf61e2562); STEP(G,d,a,b,c,x[ 6], 9,0xc040b340);
    STEP(G,c,d,a,b,x[11],14,0x265e5a51); STEP(G,b,c,d,a,x[ 0],20,0xe9b6c7aa);
    STEP(G,a,b,c,d,x[ 5], 5,0xd62f105d); STEP(G,d,a,b,c,x[10], 9,0x02441453);
    STEP(G,c,d,a,b,x[15],14,0xd8a1e681); STEP(G,b,c,d,a,x[ 4],20,0xe7d3fbc8);
    STEP(G,a,b,c,d,x[ 9], 5,0x21e1cde6); STEP(G,d,a,b,c,x[14], 9,0xc33707d6);
    STEP(G,c,d,a,b,x[ 3],14,0xf4d50d87); STEP(G,b,c,d,a,x[ 8],20,0x455a14ed);
    STEP(G,a,b,c,d,x[13], 5,0xa9e3e905); STEP(G,d,a,b,c,x[ 2], 9,0xfcefa3f8);
    STEP(G,c,d,a,b,x[ 7],14,0x676f02d9); STEP(G,b,c,d,a,x[12],20,0x8d2a4c8a);
    STEP(H,a,b,c,d,x[ 5], 4,0xfffa3942); STEP(H,d,a,b,c,x[ 8],11,0x8771f681);
    STEP(H,c,d,a,b,x[11],16,0x6d9d6122); STEP(H,b,c,d,a,x[14],23,0xfde5380c);
    STEP(H,a,b,c,d,x[ 1], 4,0xa4beea44); STEP(H,d,a,b,c,x[ 4],11,0x4bdecfa9);
    STEP(H,c,d,a,b,x[ 7],16,0xf6bb4b60); STEP(H,b,c,d,a,x[10],23,0xbebfbc70);
    STEP(H,a,b,c,d,x[13], 4,0x289b7ec6); STEP(H,d,a,b,c,x[ 0],11,0xeaa127fa);
    STEP(H,c,d,a,b,x[ 3],16,0xd4ef3085); STEP(H,b,c,d,a,x[ 6],23,0x04881d05);
    STEP(H,a,b,c,d,x[ 9], 4,0xd9d4d039); STEP(H,d,a,b,c,x[12],11,0xe6db99e5);
    STEP(H,c,d,a,b,x[15],16,0x1fa27cf8); STEP(H,b,c,d,a,x[ 2],23,0xc4ac5665);
    STEP(I,a,b,c,d,x[ 0], 6,0xf4292244); STEP(I,d,a,b,c,x[ 7],10,0x432aff97);
    STEP(I,c,d,a,b,x[14],15,0xab9423a7); STEP(I,b,c,d,a,x[ 5],21,0xfc93a039);
    STEP(I,a,b,c,d,x[12], 6,0x655b59c3); STEP(I,d,a,b,c,x[ 3],10,0x8f0ccc92);
    STEP(I,c,d,a,b,x[10],15,0xffeff47d); STEP(I,b,c,d,a,x[ 1],21,0x85845dd1);
    STEP(I,a,b,c,d,x[ 8], 6,0x6fa87e4f); STEP(I,d,a,b,c,x[15],10,0xfe2ce6e0);
    STEP(I,c,d,a,b,x[ 6],15,0xa3014314); STEP(I,b,c,d,a,x[13],21,0x4e0811a1);
    STEP(I,a,b,c,d,x[ 4], 6,0xf7537e82); STEP(I,d,a,b,c,x[11],10,0xbd3af235);
    STEP(I,c,d,a,b,x[ 2],15,0x2ad7d2bb); STEP(I,b,c,d,a,x[ 9],21,0xeb86d391);
    #undef STEP
    s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d;
}

static void md5_update(MD5_CTX *c, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t idx = (c->count >> 3) & 0x3f;
    c->count += (uint64_t)len << 3;
    size_t fill = 64 - idx;
    size_t i = 0;
    if (len < fill) { memcpy(c->buffer + idx, p, len); return; }
    if (fill) { memcpy(c->buffer + idx, p, fill); md5_transform(c->state, c->buffer); i = fill; }
    for (; i + 64 <= len; i += 64) md5_transform(c->state, p + i);
    if (i < len) memcpy(c->buffer, p + i, len - i);
}

static void md5_final(uint8_t out[16], MD5_CTX *c) {
    uint8_t bits[8];
    for (int i = 0; i < 8; i++) bits[i] = (uint8_t)(c->count >> (8 * i));
    size_t idx = (c->count >> 3) & 0x3f;
    size_t pad = (idx < 56) ? (56 - idx) : (120 - idx);
    static const uint8_t PAD[64] = { 0x80 };
    md5_update(c, PAD, pad);
    md5_update(c, bits, 8);
    for (int i = 0; i < 4; i++) {
        out[4*i+0] = (uint8_t)(c->state[i] & 0xff);
        out[4*i+1] = (uint8_t)((c->state[i] >> 8) & 0xff);
        out[4*i+2] = (uint8_t)((c->state[i] >> 16) & 0xff);
        out[4*i+3] = (uint8_t)((c->state[i] >> 24) & 0xff);
    }
}

static void md5_hex(const uint8_t in[16], char out[33]) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) { out[i*2] = hx[in[i]>>4]; out[i*2+1] = hx[in[i]&0xf]; }
    out[32] = 0;
}

static void frame_md5_bgra(const struct pic *p, char out[33]) {
    /* Walk pitch-padded rows, take first w*4 BGRA bytes each row */
    int w = p->width, h = p->height;
    MD5_CTX c; md5_init(&c);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)p->pixels + (size_t)y * p->pitch;
        md5_update(&c, row, (size_t)w * 4);
    }
    uint8_t digest[16]; md5_final(digest, &c);
    md5_hex(digest, out);
}

/* ====== per-test helper ====== */

struct want_frame {
    uint32_t duration_ms;
    char     md5[33];
};

static int
check_gif(const char *fname, int want_frames, const struct want_frame *want)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_DATA_DIR, fname);

    struct file_ops *ops = file_probe(path);
    if (!ops || strcmp(ops->name, "GIF")) {
        printf("%s: not detected as GIF\n", fname);
        return -1;
    }

    struct pic *p = file_load(ops, path, 0);
    if (p != NULL) {
        /* single-frame shortcut path — check it then free */
        int rc = 0;
        char got_md5[33]; frame_md5_bgra(p, got_md5);
        if (want_frames != 1) { printf("%s: unexpected single-frame path\n", fname); rc = -1; }
        if (!rc && p->duration_ms != want[0].duration_ms) {
            printf("%s frame0: duration_ms %u, want %u\n", fname, p->duration_ms, want[0].duration_ms); rc = -1;
        }
        if (!rc && strcmp(got_md5, want[0].md5)) {
            printf("%s frame0: MD5 %s, want %s\n", fname, got_md5, want[0].md5); rc = -1;
        }
        file_free(ops, p);
        return rc;
    }

    /* multi-frame — drain queue */
    struct pic *frames[16] = {0};
    int n = 0;
    while ((p = file_dequeue_pic()) != NULL) {
        if (n >= 16) { printf("%s: too many frames (queue didn't bound?)\n", fname); break; }
        frames[n++] = p;
    }

    int rc = 0;
    if (n != want_frames) {
        printf("%s: frame count %d, want %d\n", fname, n, want_frames); rc = -1;
    }

    for (int i = 0; i < n && i < want_frames; i++) {
        char got_md5[33]; frame_md5_bgra(frames[i], got_md5);
        if (frames[i]->duration_ms != want[i].duration_ms) {
            printf("%s frame%d: duration_ms %u, want %u\n", fname, i,
                   frames[i]->duration_ms, want[i].duration_ms); rc = -1;
        }
        if (strcmp(got_md5, want[i].md5)) {
            printf("%s frame%d: MD5 %s, want %s\n", fname, i, got_md5, want[i].md5); rc = -1;
        }
    }

    for (int i = 0; i < n; i++) file_free(ops, frames[i]);
    return rc;
}

int main(void) {
    int rc = 0;
    file_ops_init();

    /* Sample A: transparent.gif — 2 frames, 50ms each
     * ffmpeg ref MD5 (BGRA8 raw): frame0 ae0ea90b7cb20e7005421f25cf3336e5
     *                            frame1 691f9acbbd3b6c403676733885e2dd52
     * GCE delay_time=5 → duration_ms = 5*10 = 50 */
    static const struct want_frame want_transparent[2] = {
        {  50, "ae0ea90b7cb20e7005421f25cf3336e5" },
        {  50, "691f9acbbd3b6c403676733885e2dd52" },
    };
    if (check_gif("gif_anim_transparent.gif", 2, want_transparent))
        rc = -1; else printf("  gif_anim_transparent.gif: PASS (2 frames, dur 50/50)\n");

    /* Sample B: dispose3.gif — 3 frames, 500ms each, disposal=1/3/1
     * ffmpeg ref MD5: frame0 ae0ea90b7cb20e7005421f25cf3336e5
     *                 frame1 5bcaf7a44d281f86b57fc3b26b52d538
     *                 frame2 2a683c6ddc2ff1f1338b7d8554cc7cae
     * GCE delay_time=50 → duration_ms = 50*10 = 500 */
    static const struct want_frame want_dispose3[3] = {
        { 500, "ae0ea90b7cb20e7005421f25cf3336e5" },
        { 500, "5bcaf7a44d281f86b57fc3b26b52d538" },
        { 500, "2a683c6ddc2ff1f1338b7d8554cc7cae" },
    };
    if (check_gif("gif_anim_dispose3.gif", 3, want_dispose3))
        rc = -1; else printf("  gif_anim_dispose3.gif:    PASS (3 frames, dur 500 each, dispose=1/3/1)\n");

    if (!rc) printf("\nAll GIF animation regression tests passed.\n");
    return rc;
}
