#ifndef __X86_H__
#define __X86_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <immintrin.h>

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
