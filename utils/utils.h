#ifndef _UTILS_H_
#define _UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

void hexdump(FILE *f, const char *title, const void *buf, unsigned int len);

void mcu_dump(FILE *f, const char *title, const int *buf);

int log2floor(uint32_t n);


/* keep input v between 0 to M */
static inline int clamp(int v, int M)
{
    return v < 0 ? 0 : v > M ? M : v;
}

#define HEXDUMP(f, t, b, l) hexdump(f, t, b, l)

#ifdef __cplusplus
}
#endif

#endif /*_UTILS_H_*/