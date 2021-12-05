#ifndef _BITSTREAM_H_
#define _BITSTREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

struct bits_vec {
	uint8_t *start;
	uint8_t *ptr;
	uint8_t offset;
	size_t len;
};

struct bits_vec * init_bits_vec(uint8_t *buff, int len);

int eof_bits(struct bits_vec *v, int n);

void step_back(struct bits_vec *v, int n);

int read_bits(struct bits_vec *v, int n);

int read_bit(struct bits_vec *v);

void skip_bits(struct bits_vec *v, int n);

void reset_bits_boundary(struct bits_vec *v);


#define READ_BIT(v) read_bit(v)
#define READ_BITS(v, n) read_bits(v, n)
#define SKIP_BITS(v, n) skip_bits(v, n)
#define STEP_BACK(v, n) step_back(v, n)
#define RESET_BOUNDARY(v) reset_bits_boundary(v)
#define EOF_BITS(v, n) eof_bits(v, n)

#ifdef __cplusplus
}
#endif


#endif /*_BITSTREAM_H_*/