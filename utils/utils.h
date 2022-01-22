#ifndef _UTILS_H_
#define _UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

void hexdump(FILE *f, const char *title, const char *prefix, const void *buf, unsigned int len);

void mcu_dump(FILE *f, const char *title, const int *buf);

int log2floor(uint32_t n);
int log2ceil(uint32_t n);

uint32_t divceil(uint32_t a, uint32_t div);

/* keep input v between 0 to M */
static inline int clamp(int v, int M)
{
    return v < 0 ? 0 : v > M ? M : v;
}

#define HEXDUMP(f, t, b, l) hexdump(f, t, b, l)

#define MIN(a, b) ((a > b) ? (b): (a))
#define MAX(a, b) ((a > b) ? (a): (b))

#define OFFSET_OF(type, member) ((size_t) &((type *)0)->member)
#define CONTAIN_OF(ptr, type, member) ({\
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)( (char *)__mptr - OFFSET_OF(type, member) );})


#ifdef __cplusplus
}
#endif

#endif /*_UTILS_H_*/