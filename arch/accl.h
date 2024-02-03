#ifndef __ACCL_H__
#define __ACCL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/queue.h>

typedef void (*accl_idct)(int16_t *in, int16_t *out, int bitdepth);

enum simd_type {
    SIMD_TYPE_SSE2 = 1,
    SIMD_TYPE_AVX2 = 2,
    GPU_TYPE_OPENCL = 25,
};

struct accl_ops {
    void (*idct_4x4)(int16_t *in, int bitdepth);
    void (*idct_8x8)(int16_t *in, int bitdepth);
    enum simd_type type;
    TAILQ_ENTRY(accl_ops) next;
};

void accl_ops_register(struct accl_ops *ops);

void accl_ops_init(void);

struct accl_ops *accl_first_available(void);

struct accl_ops *accl_find(int type);

#ifdef __cplusplus
}
#endif

#endif
