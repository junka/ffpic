#ifndef __X86_H__
#define __X86_H__

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#endif

#ifdef __AVX2__
void x86_avx2_init(void);
#endif
#ifdef __SSE2__
void x86_sse2_init(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
