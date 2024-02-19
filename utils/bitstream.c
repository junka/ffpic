#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"
#include "vlog.h"

struct bits_vec *bits_vec_alloc(uint8_t *buff, int len, uint8_t msb) {
  struct bits_vec *vec = (struct bits_vec *)malloc(sizeof(struct bits_vec));
  vec->start = vec->ptr = buff;
  vec->buff = buff;
  vec->offset = 0;
  vec->len = len;
  vec->msb = msb;
  return vec;
}

void bits_vec_free(struct bits_vec *v) {
  if (v->buff) {
    free(v->buff);
  }
  free(v);
}

int bits_vec_eof_bits(struct bits_vec *v, int n) {
  if (v->ptr + (v->offset + n) / 8 - v->start > (long)v->len) {
    return 1;
  } else if (v->ptr + (v->offset + n) / 8 - v->start == (long)v->len) {
    if (v->offset + n % 8 > 8) {
      return 1;
    }
  }
  return 0;
}

void bits_vec_step_back(struct bits_vec *v, int n) {
  while (n--) {
    if (v->offset == 0) {
      v->ptr--;
      v->offset = 7;
    } else {
      v->offset--;
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

int bits_vec_read_bit(struct bits_vec *v) {
  if (v->ptr - v->start > (long)v->len)
    return -1;
  int ret, shift;
  if (v->msb)
    shift = 7 - v->offset;
  else
    shift = v->offset;
  ret = (*(v->ptr) >> shift) & 0x1;
  v->offset++;
  if (v->offset == 8) {
    v->ptr++;
    v->offset = 0;
  }
  return ret;
}

int bits_vec_read_bits(struct bits_vec *v, int n) {
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

void bits_vec_skip_bits(struct bits_vec *v, int n) {
  uint8_t skip = 0;
  int bytes = n / 8;
  while (skip < bytes) {
    v->ptr++;
    skip += 1;
  }
  v->offset += n % 8;
  if (v->offset >= 8) {
    v->ptr++;
    v->offset -= 8;
  }
}

int bits_vec_aligned(struct bits_vec *v) {
  if (v->offset == 0) {
    return 1;
  }
  return 0;
}

int bits_vec_test_bit(struct bits_vec *v) {
  int shift;
  if (v->msb)
    shift = 7 - v->offset;
  else
    shift = v->offset;
  return (*(v->ptr) >> shift) & 0x1;
}

void bits_vec_reset_border(struct bits_vec *v) {
  if (v->offset) {
    v->ptr++;
    v->offset = 0;
  }
}

int bits_vec_position(struct bits_vec *v) {
  return (v->ptr - v->start) * 8 + v->offset;
}

/* Read a num bit value from stream and add base */
int bits_vec_read_bits_base(struct bits_vec *v, int n, int base) {
  return base + (n ? bits_vec_read_bits(v, n) : 0);
}

void bits_vec_dump(struct bits_vec *v) {
  fprintf(vlog_get_stream(),
          "stream start %p, current %ld, len %ld, bits in use %d\n",
          (void *)v->start, v->ptr - v->start, v->len, v->offset);
}

void bits_vec_reinit_cur(struct bits_vec *v) {
  assert(v->offset == 0);
  v->len -= (v->ptr - v->start);
  v->start = v->ptr;
}

//--- below is for bitstream writer ---
#define DELTA_PART (1024)
struct bits_vec *bits_writer_reserve(uint8_t msb) {
  struct bits_vec *vec = (struct bits_vec *)malloc(sizeof(struct bits_vec));
  vec->buff = vec->start = vec->ptr = malloc(2048);
  vec->offset = 0;
  vec->len = 0;
  vec->msb = msb;
  return vec;
}

void bits_vec_write_bit(struct bits_vec *v, int8_t a) {
  int shift;
  if (v->msb)
    shift = 7 - v->offset;
  else
    shift = v->offset;
  *(v->ptr) |= ((a & 0x1) << shift);
  v->offset++;
  if (v->offset == 8) {
    if (v->len % DELTA_PART == 0) {
      v->start = v->buff = realloc(v->buff, v->len + DELTA_PART);
      v->ptr = v->buff + v->len;
    }
    v->offset = 0;
    v->len++;
    v->ptr++;
  }
}

void bits_vec_write_bits(struct bits_vec *v, int8_t a, int n) {
  int8_t val = a;
  for (int i = 0; i < n; i++) {
    int8_t b;
    if (v->msb) {
      b = (val >> (n - 1 - i)) & 0x1;
    } else {
      b = (val >> i) & 0x1;
    }
    bits_vec_write_bit(v, b);
  }
}

void bits_vec_align_byte(struct bits_vec *v) {
  if (v->offset > 0) {
    bits_vec_write_bits(v, 0, 8 - v->offset);
  }
}
