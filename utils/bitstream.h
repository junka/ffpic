#ifndef _BITSTREAM_H_
#define _BITSTREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

/* this needs special care */
enum bits_order {
    BITS_LSB = 0,   /* bool decoder order */
    BITS_MSB = 1,   /* huffman stream order */
};

struct bits_vec {
    uint8_t *buff;
    uint8_t *start;     /* bitstream start position */
    uint8_t *ptr;       /* current byte pointer */
    uint8_t offset;     /* bits left in a byte for current reader */
    size_t len;         /* total bitstream length in bytes */
    uint8_t msb;        /* indicate bits read from which side */
};

/* Allocate a bitstream reader */
struct bits_vec * bits_vec_alloc(uint8_t *buff, int len, uint8_t msb);

/* free a bitstream reader */
void bits_vec_free(struct bits_vec *v);

/* end of stream */
int bits_vec_eof_bits(struct bits_vec *v, int n);

/* make the stream ptr back n bits */
void bits_vec_step_back(struct bits_vec *v, int n);

/* read a bit from the stream */
int bits_vec_read_bit(struct bits_vec *v);

/* read a bit from the stream */
int bits_vec_read_bits(struct bits_vec *v, int n);

/* skip several bits without caring the value */
void bits_vec_skip_bits(struct bits_vec *v, int n);

/* skip all bits left int current byte */
void bits_vec_reset_border(struct bits_vec *v);

/* read bits and add the value with base */
int bits_vec_read_bits_base(struct bits_vec *v, int n, int base);

struct bits_vec * bits_writer_reserve(uint8_t msb);

void bits_vec_write_bit(struct bits_vec *v, int8_t a);
void bits_vec_write_bits(struct bits_vec *v, int8_t a, int n);

/* debug function for stream info */
void bits_vec_dump(struct bits_vec *v);

/* bit stream start at a new byte */
int bits_vec_aligned(struct bits_vec *v);

int bits_vec_test_bit(struct bits_vec *v);

int bits_vec_position(struct bits_vec *v);

void bits_vec_reinit_cur(struct bits_vec *v);

/* macro define for bitstream helper */
#define READ_BIT(v) bits_vec_read_bit(v)
#define READ_BITS(v, n) bits_vec_read_bits(v, n)
#define SKIP_BITS(v, n) bits_vec_skip_bits(v, n)
#define STEP_BACK(v, n) bits_vec_step_back(v, n)
#define RESET_BORDER(v) bits_vec_reset_border(v)
#define EOF_BITS(v, n) bits_vec_eof_bits(v, n)
#define READ_BITS_BASE(v, n, b) bits_vec_read_bits_base(v, n, b)
#define BYTE_ALIGNED(v) bits_vec_aligned(v)
#define TEST_BIT(v) bits_vec_test_bit(v)

#define WRITE_BIT(v, a) bits_vec_write_bit(v, a)
#define WRITE_BITS(v, a, n) bits_vec_write_bits(v, a, n)

#ifdef __cplusplus
}
#endif

#endif /*_BITSTREAM_H_*/
