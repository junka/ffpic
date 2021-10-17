#ifndef _UTILS_H_
#define _UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

void hexdump(FILE *f, const char *title, const void *buf, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif /*_UTILS_H_*/