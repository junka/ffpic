#ifndef _BITSTREAM_H_
#define _BITSTREAM_H_

#ifdef __cplusplus
extern "C" {
#endif


enum bits_order {
    BITS_LSB = 0,
    BITS_MSB = 1,
};

struct bits_vec {
    uint8_t *start;     /* bitstream start position */
    uint8_t *ptr;       /* current byte pointer */
    uint8_t offset;     /* bits left in a byte for current reader */
    size_t len;         /* total bitstream length in bytes */
    uint8_t msb;        /* indicate bits read from which side */
};

/* Allocate a bitstream reader */
struct bits_vec * bits_vec_alloc(uint8_t *buff, int len, uint8_t msb);
/* free a bitstream reader */
void bit_vec_free(struct bits_vec *v);

/* end of stream */
int eof_bits(struct bits_vec *v, int n);

/* make the stream ptr back n bits */
void step_back(struct bits_vec *v, int n);

/* read a bit from the stream */
int read_bit(struct bits_vec *v);

/* read a bit from the stream */
int read_bits(struct bits_vec *v, int n);

/* skip several bits without caring the value */
void skip_bits(struct bits_vec *v, int n);

/* skip all bits left int current byte */
void reset_bits_border(struct bits_vec *v);

/* read bits and add the value with base */
int read_bits_base(struct bits_vec *v, int n, int base);

#define READ_BIT(v) read_bit(v)
#define READ_BITS(v, n) read_bits(v, n)
#define SKIP_BITS(v, n) skip_bits(v, n)
#define STEP_BACK(v, n) step_back(v, n)
#define RESET_BORDER(v) reset_bits_border(v)
#define EOF_BITS(v, n) eof_bits(v, n)
#define READ_BITS_BASE(v, n, b) read_bits_base(v, n, b)

#ifdef __cplusplus
}
#endif


#endif /*_BITSTREAM_H_*/