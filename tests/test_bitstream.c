#include <stdint.h>
#include <stdio.h>

#include "bitstream.h"


int test_reader(void)
{
    uint8_t *data = malloc(4);
    data[0] = 0x57; // 0b01010111
    data[1] = 0x83;
    data[2] = 0x71;
    data[3] = 0xa9;

    struct bits_vec *v = bits_vec_alloc(data, 4, BITS_MSB);
    if (!v) {
        return -1;
    }
    // test the first 4 bits
    if (READ_BIT(v) != 0) {
        goto read_err;
    }
    if (READ_BIT(v) != 1) {
        goto read_err;
    }
    if (READ_BIT(v) != 0) {
        goto read_err;
    }
    if (READ_BIT(v) != 1) {
        goto read_err;
    }
    // test the second 4 bits in first byte
    if (READ_BITS(v, 4) != 7) {
        goto read_err;
    }
    // should be byte aligned
    if (!BYTE_ALIGNED(v)) {
        goto read_err;
    }
    // test read bits over two bytes
    SKIP_BITS(v, 5);
    if (READ_BITS(v, 4) != 6) {
        goto read_err;
    }
    if (READ_BITS(v, 9) != (0x71 * 4 + 2)) {
        goto read_err;
    }
    // test step back
    STEP_BACK(v, 2);
    if (READ_BITS(v, 4) != 0xa) {
        goto read_err;
    }
    if (TEST_BIT(v) == 0) {
        goto read_err;
    }
    // test if eof bits logic
    if (!EOF_BITS(v, 5)) {
        goto read_err;
    }
    if (EOF_BITS(v, 4)) {
        goto read_err;
    }
    if (BYTE_ALIGNED(v)) {
        goto read_err;
    }
    // test helper function READ_BITS_BASE
    if (READ_BITS_BASE(v, 4, 10) != 19) {
        goto read_err;
    }
    bits_vec_free(v);

    // test it again with LSB bit order
    data = malloc(4);
    data[0] = 0x57; // 0b01010111
    data[1] = 0x83;
    data[2] = 0x71;
    data[3] = 0xa9;

    v = bits_vec_alloc(data, 4, BITS_LSB);
    if (READ_BIT(v) != 1) {
        goto read_err;
    }
    if (READ_BIT(v) != 1) {
        goto read_err;
    }
    if (READ_BIT(v) != 1) {
        goto read_err;
    }
    if (READ_BIT(v) != 0) {
        goto read_err;
    }
    // test the second 4 bits in first byte
    if (READ_BITS(v, 4) != 5) {
        goto read_err;
    }
    // should be byte aligned
    if (!BYTE_ALIGNED(v)) {
        goto read_err;
    }
    // test read bits over two bytes
    SKIP_BITS(v, 5);
    if (READ_BITS(v, 4) != 0xC) {
        goto read_err;
    }
    if (READ_BITS(v, 9) != ((0x70 >> 1) + 128)) {
        goto read_err;
    }
    // test step back
    STEP_BACK(v, 2);
    if (READ_BITS(v, 4) != 0x9) {
        goto read_err;
    }
    if (TEST_BIT(v) == 1) {
        goto read_err;
    }
    // test if eof bits logic
    if (!EOF_BITS(v, 5)) {
        goto read_err;
    }
    if (EOF_BITS(v, 4)) {
        goto read_err;
    }
    if (BYTE_ALIGNED(v)) {
        goto read_err;
    }
    // test helper function READ_BITS_BASE
    if (READ_BITS_BASE(v, 4, 10) != 20) {
        goto read_err;
    }
    bits_vec_free(v);
    return 0;
read_err:
    bits_vec_free(v);
    return -1;
}

int test_writer(void)
{
    struct bits_vec *v = bits_writer_reserve(BITS_MSB, 0);  /* nostuff=0: keep original stuffing behavior for bitstream writer test */

    WRITE_BIT(v, 1);
    WRITE_BIT(v, 1);
    WRITE_BIT(v, 0);
    WRITE_BIT(v, 0);
    if (v->offset != 4) {
        goto write_err;
    }
    WRITE_BITS(v, 5, 4);
    if (v->offset != 0) {
        goto write_err;
    }
    if (*(v->ptr-1) != 0xC5) {
        goto write_err;
    }

    bits_vec_free(v);
    return 0;
write_err:
    bits_vec_free(v);
    return -1;
}

/* a skip can span more than one byte's worth of counter: real hevc streams
 * carry sei blocks of several hundred bytes */
int test_skip_long(void)
{
    enum { LEN = 1100, BITS = 8000 };
    uint8_t *data = calloc(1, LEN);
    struct bits_vec *v;

    if (!data) {
        return -1;
    }
    v = bits_vec_alloc(data, LEN, BITS_MSB);
    if (!v) {
        free(data);
        return -1;
    }

    SKIP_BITS(v, BITS);
    if (bits_vec_position(v) != BITS) {
        goto skip_err;
    }
    if (READ_BITS(v, 8) != 0) {
        goto skip_err;
    }
    if (bits_vec_position(v) != BITS + 8) {
        goto skip_err;
    }

    bits_vec_free(v);
    return 0;
skip_err:
    bits_vec_free(v);
    return -1;
}

/* The reader pulls whole bytes into a 64 bit window, so a read is a shift
 * instead of a bit at a time loop.  Drive a deterministic pseudo random
 * sequence of reads through it and compare every result against a plain bit
 * at a time model of the same stream, so the window cannot silently drift
 * from the definition (including the zero padding past the end of the
 * buffer, which coders rely on when they peek a lookup key). */
static int
model_read(const uint8_t *data, int len, long pos, int n, int msb)
{
    uint32_t ret = 0;
    for (int i = 0; i < n; i++) {
        long p = pos + i;
        int bit = 0;
        if (p < (long)len * 8) {
            int shift = msb ? 7 - (int)(p % 8) : (int)(p % 8);
            bit = (data[p / 8] >> shift) & 1;
        }
        if (msb) {
            ret = (ret << 1) | (uint32_t)bit;
        } else {
            ret |= (uint32_t)bit << i;
        }
    }
    return (int)ret;
}

static int
test_reader_model(int msb)
{
    enum { LEN = 24, STEPS = 50000 };
    uint8_t *data = malloc(LEN);
    uint32_t s = 0x2545F491u;
    struct bits_vec *v;
    long pos = 0;

    if (!data) {
        return -1;
    }
    for (int i = 0; i < LEN; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        data[i] = (s % 5 == 0) ? 0xFF : ((s % 7 == 0) ? 0x00 : (uint8_t)s);
    }
    v = bits_vec_alloc(data, LEN, (uint8_t)msb);

    for (int step = 0; step < STEPS; step++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        int op = (int)(s % 100);
        int n, got, want;

        if (op < 45) {              /* a plain read */
            n = 1 + (int)(s % 32);
            got = READ_BITS(v, n);
            want = model_read(data, LEN, pos, n, msb);
            if (got != want) {
                printf("read %d bits at %ld: got %d want %d\n", n, pos, got, want);
                goto err;
            }
            pos += n;
        } else if (op < 70) {       /* look without moving, then drop */
            n = 1 + (int)(s % 17);
            got = PEEK_BITS(v, n);
            want = model_read(data, LEN, pos, n, msb);
            if (got != want) {
                printf("peek %d bits at %ld: got %d want %d\n", n, pos, got, want);
                goto err;
            }
            if (bits_vec_position(v) != pos) {
                printf("peek moved the cursor\n");
                goto err;
            }
            CONSUME_BITS(v, n);
            pos += n;
        } else if (op < 85) {       /* a skip, past the end is allowed too */
            n = 1 + (int)(s % 90);
            SKIP_BITS(v, n);
            pos += n;
        } else if (op < 95) {       /* walk back inside the stream */
            n = pos > 0 ? 1 + (int)(s % (uint32_t)(pos < 20 ? pos : 20)) : 0;
            if (n > 0) {
                STEP_BACK(v, n);
                pos -= n;
            }
        } else {                    /* toss the rest of the current byte */
            long next = pos + (8 - pos % 8) % 8;
            RESET_BORDER(v);
            pos = next;
        }

        if (bits_vec_position(v) != pos) {
            printf("step %d op %d left the cursor at %d, model says %ld\n",
                   step, op, bits_vec_position(v), pos);
            goto err;
        }
        if (pos > (long)LEN * 8) {
            /* keep the cursor near the stream, otherwise skips run away */
            STEP_BACK(v, pos - 4);
            pos = 4;
        }
    }

    bits_vec_free(v);
    return 0;
err:
    bits_vec_free(v);
    return -1;
}

int main(void)
{
    if (test_reader()) {
        return -1;
    }
    if (test_writer()) {
        return -1;
    }
    if (test_skip_long()) {
        return -1;
    }
    if (test_reader_model(BITS_MSB)) {
        return -1;
    }
    if (test_reader_model(BITS_LSB)) {
        return -1;
    }
    return 0;
}
