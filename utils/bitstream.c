#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"
#include "vlog.h"

#if 0
static const uint8_t BitReverseTable256[] = {
  0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0, 
  0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 
  0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, 
  0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC, 
  0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 
  0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
  0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6, 
  0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
  0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
  0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9, 
  0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
  0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
  0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3, 
  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
  0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, 
  0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};
#endif

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

int bits_vec_left_bits(struct bits_vec *v)
{
  return (v->start+v->len - v->ptr)*8 - v->offset;
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
  if (v->ptr - v->start > (long)v->len) {
    printf("bits longer than expect\n");
    exit(-1);
    return -1;
  }
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
          "stream start %p, current %ld, len %d, bits in use %d\n",
          (void *)v->start, v->ptr - v->start, v->len, v->offset);
}

void bits_vec_reinit_cur(struct bits_vec *v) {
  assert(v->offset == 0);
  v->len -= (v->ptr - v->start);
  v->start = v->ptr;
}

//--- below is for bitstream writer ---
#define DELTA_PART (4096)
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
    if (v->len + 1 >= v->reserve) {
        v->start = v->buff = realloc(v->buff, v->len + DELTA_PART);
        v->ptr = v->buff + v->len;
        v->reserve += DELTA_PART;
    }
    v->offset = 0;
    v->len++;
    v->ptr++;
    if (*(v->ptr-1) == 0xFF) {
        *(v->ptr) = 0;
        v->ptr++;
        v->len++;
    }
  }
}

void bits_vec_write_bits(struct bits_vec *v, int a, int n) {
  int val = a;
  if (v->offset == 0) {
    if ((v->len + n/8 + (n%8?1:0)) >= v->reserve) {
        v->start = v->buff = realloc(v->buff, v->len + DELTA_PART);
        v->ptr = v->buff + v->len;
        v->reserve += DELTA_PART;
    }
    uint8_t b;
    while (n >=8) {
        if (v->msb) {
          b = (val >> (n - 8)) & 0xFF;
          val -= (b << (n-8));
        } else {
          b = (val)&0xFF;
          val >>= 8;
        }
        *v->ptr = b;
        v->ptr++;
        v->len++;
        if (b == 0xFF) {
          *(v->ptr) = 0;
          v->ptr++;
          v->len++;
        }
        n-=8;
    }
    if (n>0) {
        if (v->msb) {
          b = (val << (8 - n)) & 0xFF;
        } else {
          b = (val) & 0xFF;
        }
        *v->ptr = b;
        v->offset = n;
    }
  } else {
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
}

void bits_vec_align_byte(struct bits_vec *v) {
  if (v->offset > 0) {
    bits_vec_write_bits(v, 0, 8 - v->offset);
  }
}
