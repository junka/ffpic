#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"

struct bits_vec * 
init_bits_vec(uint8_t *buff, int len)
{
	struct bits_vec *vec = (struct bits_vec *)malloc(sizeof(struct bits_vec));
	vec->start = vec->ptr = buff;
	vec->offset = 0;
	vec->len = len;
	return vec;
}

int 
eof_bits(struct bits_vec *v, int n)
{
	if (v->ptr + n/8 - v->start > v->len) {
		return 1;
	} else if (v->ptr + n/8 - v->start == v->len) {
		if (v->offset + n % 8 > 8) {
			return 1;
		}
	}
	return 0;
}

void 
step_back(struct bits_vec *v, int n)
{
	while(n --) {
		if (v->offset == 0) {
			v->ptr --;
			v->offset = 7;
		} else {
			v->offset --;
		}
	}
}

int 
read_bits(struct bits_vec *v, int n)
{
	uint8_t read = 0;
	int ret = 0;
	if (v->offset > 0)
	{
		ret = *(v->ptr);
		ret &= ((1 << (8 - v->offset)) - 1);
		read = 8 - v->offset;
		v->ptr ++;
	}
	while (read < n) {
		ret = (ret << 8) | *(v->ptr);
		v->ptr ++;
		read += 8;
	}
	if (read > 8)
		ret >>= ((read - n) % 8);
	else 
		ret >>= ((read - n) % 8);
	ret &= ((1 << n) -1);
	if((n + v->offset) % 8) {
		v->ptr --;
	}
	v->offset = ((v->offset + (n%8))%8);
	return ret;
}

int 
read_bit(struct bits_vec *v)
{
	if (v->ptr - v->start > v->len)
		return -1;
	uint8_t ret = (*(v->ptr) >> (7 - v->offset)) & 0x1;
	v->offset ++;
	if (v->offset == 8) {
		v->ptr ++;
		v->offset = 0;
	}
	return ret;
}

#if 0
int 
read_bits(struct bits_vec *v, int n)
{
	int ret = 0;
	for (int i = 0; i < n; i++) {
		int a = read_bit(v);
		if (a == -1) {
			return -1;
		}
		ret = (ret<<1 | a);
	}
	return ret;
}
#endif

void
skip_bits(struct bits_vec *v, int n)
{
	uint8_t skip  = 0;
	while (skip < n) {
		v->ptr ++;
		skip += 8;
	}
	v->offset = 8 - (skip - n - v->offset);
}

void
reset_bits_boundary(struct bits_vec *v)
{
	if (v->offset) {
		v->ptr ++;
		v->offset = 0;
	}
}



