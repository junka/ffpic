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
    struct bits_vec *v = bits_writer_reserve(BITS_MSB);

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

int main(void)
{
    if (test_reader()) {
        return -1;
    }
    if (test_writer()) {
        return -1;
    }
    return 0;
}
