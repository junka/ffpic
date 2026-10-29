/* The bool decoder has no encoder counterpart in this repository and is
 * only exercised indirectly through the webp samples.  This test drives it
 * against an independent libvpx-style boolean encoder: a fixed
 * pseudo-random bit sequence at varied probabilities, the multi-bit
 * helpers, and a tree walk.  The two state machines are written
 * independently, so any divergence shows up as a wrong bit. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "booldec.h"
#include "utils.h"

#define SEQ_BITS 400

static const int probs[] = {128, 255, 1, 200, 25, 230};

struct benc {
    uint8_t buf[256];
    int pos;
    uint32_t low;
    uint32_t range;
    int count;
};

static void benc_init(struct benc *e)
{
    memset(e->buf, 0, sizeof(e->buf));
    e->pos = 0;
    e->low = 0;
    e->range = 255;
    e->count = -24;
}

/* libvpx vpx_dsp/bitwriter.h vpx_write, verbatim */
static void benc_bit(struct benc *e, int bit, int prob)
{
    uint32_t split = 1 + (((e->range - 1) * (uint32_t)prob) >> 8);
    uint32_t range = split;
    uint32_t low = e->low;
    int count = e->count;
    int shift;

    if (bit) {
        low += split;
        range = e->range - split;
    }
    shift = 7 - log2floor(range);
    range <<= shift;
    count += shift;

    if (count >= 0) {
        int offset = shift - count;
        if ((low << (offset - 1)) & 0x80000000) {
            int x = e->pos - 1;
            while (x >= 0 && e->buf[x] == 0xff) {
                e->buf[x] = 0;
                x--;
            }
            e->buf[x] += 1;
        }
        e->buf[e->pos++] = (uint8_t)(low >> (24 - offset));
        low <<= offset;
        shift = count;
        low &= 0xffffff;
        count -= 8;
    }

    low <<= shift;
    e->low = low;
    e->range = range;
    e->count = count;
}

static void benc_done(struct benc *e)
{
    for (int i = 0; i < 32; i++) {
        benc_bit(e, 0, 128);
    }
}

static uint32_t lcg(uint32_t *state)
{
    *state = (*state * 1103515245u + 12345u) & 0x7fffffffu;
    return *state;
}

static int test_bit_sequence(struct benc *e)
{
    uint32_t seed = 20261029u;
    int bits[SEQ_BITS];
    int used[SEQ_BITS];
    int rc = 0;

    for (int i = 0; i < SEQ_BITS; i++) {
        bits[i] = (int)(lcg(&seed) & 1);
        used[i] = probs[lcg(&seed) % (sizeof(probs) / sizeof(probs[0]))];
        benc_bit(e, bits[i], used[i]);
    }

    benc_done(e);
    /* the decoder takes ownership of the buffer */
    uint8_t *data = malloc((size_t)e->pos);
    memcpy(data, e->buf, (size_t)e->pos);
    bool_dec *br = bool_dec_init(data, e->pos);
    for (int i = 0; i < SEQ_BITS; i++) {
        int got = (int)bool_dec_bit(br, used[i]);
        if (got != bits[i]) {
            printf("bool bit %d is %d, expected %d\n", i, got, bits[i]);
            rc = -1;
            break;
        }
    }
    bool_dec_free(br);
    return rc;
}

static int test_multi_bit_helpers(struct benc *e)
{
    /* bool_dec_bits takes the bits most significant first; each helper
     * consumes its own group, so encode two */
    const unsigned magnitude = 0x2d;  /* 0b101101 */
    const unsigned signed_mag = 0x19; /* 0b011001 */
    int rc = 0;

    for (int i = 5; i >= 0; i--) {
        benc_bit(e, (magnitude >> i) & 1, 128);
    }
    for (int i = 5; i >= 0; i--) {
        benc_bit(e, (signed_mag >> i) & 1, 128);
    }
    benc_bit(e, 1, 128);  /* sign: negative */

    benc_done(e);
    /* the decoder takes ownership of the buffer */
    uint8_t *data = malloc((size_t)e->pos);
    memcpy(data, e->buf, (size_t)e->pos);
    bool_dec *br = bool_dec_init(data, e->pos);
    if (bool_dec_bits(br, 6) != magnitude) {
        printf("bool_dec_bits mismatch\n");
        rc = -1;
    }
    if ((int)bool_dec_signed_bits(br, 6) != -(int)signed_mag) {
        printf("bool_dec_signed_bits mismatch\n");
        rc = -1;
    }
    bool_dec_free(br);
    return rc;
}

static int test_tree(struct benc *e)
{
    /* 1 -> 0, 2 -> 10, 3 -> 11 */
    static const int8_t tree[] = {-1, 2, -2, -3};
    static const uint8_t tree_probs[] = {128, 160};
    static const int symbols[] = {3, 1, 2};
    int rc = 0;

    /* symbol 3 -> 11, 1 -> 0, 2 -> 10 */
    benc_bit(e, 1, tree_probs[0]);
    benc_bit(e, 1, tree_probs[1]);
    benc_bit(e, 0, tree_probs[0]);
    benc_bit(e, 1, tree_probs[0]);
    benc_bit(e, 0, tree_probs[1]);

    benc_done(e);
    /* the decoder takes ownership of the buffer */
    uint8_t *data = malloc((size_t)e->pos);
    memcpy(data, e->buf, (size_t)e->pos);
    bool_dec *br = bool_dec_init(data, e->pos);
    for (size_t s = 0; s < sizeof(symbols) / sizeof(symbols[0]); s++) {
        int got = bool_dec_tree(br, tree, tree_probs, 0);
        if (got != symbols[s]) {
            printf("tree symbol %zu is %d, expected %d\n", s, got, symbols[s]);
            rc = -1;
            break;
        }
    }
    bool_dec_free(br);
    return rc;
}

/* every test encodes into its own buffer, the decoder consumes one buffer
 * each; a shared encoder would carry the arithmetic coder state across */
int main(void)
{
    struct benc e;

    benc_init(&e);
    if (test_bit_sequence(&e)) {
        return -1;
    }
    printf("bit sequence done\n");

    benc_init(&e);
    if (test_multi_bit_helpers(&e)) {
        return -1;
    }
    printf("multi bit helpers done\n");

    benc_init(&e);
    if (test_tree(&e)) {
        return -1;
    }
    printf("tree done\n");
    return 0;
}
