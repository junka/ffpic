#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"

struct bits_vec * 
bits_vec_alloc(uint8_t *buff, int len, uint8_t msb)
{
    struct bits_vec *vec = (struct bits_vec *)malloc(sizeof(struct bits_vec));
    vec->start = vec->ptr = buff;
    vec->offset = 0;
    vec->len = len;
    vec->msb = msb;
    return vec;
}

void
bits_vec_free(struct bits_vec *v)
{
    if (v->start)
        free(v->start);
    free(v);
}

int
bits_vec_eof_bits(struct bits_vec *v, int n)
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
bits_vec_step_back(struct bits_vec *v, int n)
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
#if 0
int 
read_bits(struct bits_vec *v, int n)
{
    uint8_t read = 0;
    int ret = 0, shift;
    if (v->offset > 0)
    {
        ret = *(v->ptr);
        if (v->msb)
            ret &= ((1 << (8 - v->offset)) - 1);
        else
            ret >>= v->offset;
        read = 8 - v->offset;
        v->ptr ++;
    }
    while (read < n) {
        ret = (ret << 8) | *(v->ptr);
        v->ptr ++;
        read += 8;
    }
    if (read > 8) {
        if (v->msb) {
            ret >>= ((read - n) % 8);
        } else {
            ret &= (((1 << read) - 1) - ((1 << 8) - 1));
            ret >>= ((read - n) % 8);
            ret |= (*(v->ptr - 1) & ((1 << ((read - n) % 8)) -1));
        }

    }
    if (v->msb)
        ret &= ((1 << n) -1);

    if((n + v->offset) % 8) {
        v->ptr --;
    }
    v->offset = ((v->offset + (n % 8)) % 8);
    return ret;
}
#endif

int 
bits_vec_read_bit(struct bits_vec *v)
{
    if (v->ptr - v->start > v->len)
        return -1;
    int ret, shift;
    if (v->msb)
        shift = 7 - v->offset;
    else
        shift = v->offset;
    ret = (*(v->ptr) >> shift) & 0x1;
    v->offset ++;
    if (v->offset == 8) {
        v->ptr ++;
        v->offset = 0;
    }
    return ret;
}

#if 1
int 
bits_vec_read_bits(struct bits_vec *v, int n)
{
    int ret = 0;
    for (int i = 0; i < n; i++) {
        int a = bits_vec_read_bit(v);
        if (a == -1) {
            return -1;
        }
        if (v->msb)
            ret = ((ret << 1) | a);
        else
            ret |= (a << i);
    }
    return ret;
}
#endif

void
bits_vec_skip_bits(struct bits_vec *v, int n)
{
    uint8_t skip = 0;
    int bytes = n / 8;
    while (skip < bytes) {
        v->ptr ++;
        skip += 1;
    }
    v->offset += n%8;
    if (v->offset >= 8) {
        v->ptr ++;
         v->offset -= 8;
    }
}

int
bits_vec_aligned(struct bits_vec *v)
{
    if (v->offset == 0) {
        return 1;
    }
    return 0;
}

int
bits_vec_test_bit(struct bits_vec *v)
{
    int shift;
    if (v->msb)
        shift = 7 - v->offset;
    else
        shift = v->offset;
    return (*(v->ptr) >> shift) & 0x1;
}

void
bits_vec_reset_border(struct bits_vec *v)
{
    if (v->offset) {
        v->ptr ++;
        v->offset = 0;
    }
}

int bits_vec_position(struct bits_vec *v)
{
    return (v->ptr - v->start) * 8 + v->offset;
}

/* Read a num bit value from stream and add base */
int
bits_vec_read_bits_base(struct bits_vec *v, int n, int base)
{
    return base + (n ? bits_vec_read_bits(v, n) : 0);
}


void
bits_vec_dump(struct bits_vec *v)
{
    printf("stream start %p, current %ld, len %ld, bits in use %d\n", 
        v->start, v->ptr - v->start, v->len, v->offset);
}