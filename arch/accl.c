#include <stdint.h>

#include "accl.h"
#include "x86.h"

#ifdef ENABLE_OPENCL
#include "opcl.h"
#endif
#ifdef ENABLE_VULKAN
#include "vk_idct.h"
#endif

TAILQ_HEAD(accl_ops_list, accl_ops);

static struct accl_ops_list ops_list = TAILQ_HEAD_INITIALIZER(ops_list);

void accl_ops_register(struct accl_ops *ops) {
  TAILQ_INSERT_TAIL(&ops_list, ops, next);
}

static int accl_cpu_inited;

/* The CPU backends.  This is what a decoder wants: the SIMD transforms cost
 * nothing to set up, unlike the GPU ones below. */
void accl_ops_init_cpu(void)
{
    if (accl_cpu_inited) {
        return;
    }
    accl_cpu_inited = 1;
#ifdef __AVX2__
    x86_avx2_init();
#endif
#ifdef __SSE2__
    x86_sse2_init();
#endif
}

/* Everything, GPU included.  Creating an OpenCL context and a Vulkan device
 * takes about ten milliseconds and leaves a driver behind that goes on
 * stealing CPU from the process - on a 2048x2048 JPEG that was worth more
 * than the transform it accelerates - so only the applications, which ask
 * for it explicitly, get this. */
void accl_ops_init(void)
{
    accl_ops_init_cpu();
#ifdef ENABLE_OPENCL
    opcl_amd_init();
#endif
#ifdef ENABLE_VULKAN
    vulkan_init();
#endif
}

/* The fastest registered backend that can do an 8x8 inverse transform.  The
 * decoders that need one ask for this rather than naming a backend, so a new
 * one only has to register itself. */
struct accl_ops *
accl_find_idct8x8(void)
{
    struct accl_ops *ops;

    TAILQ_FOREACH(ops, &ops_list, next) {
        if (ops->idct_8x8) {
            return ops;
        }
    }
    return NULL;
}

struct accl_ops *
accl_first_available(void)
{
    struct accl_ops *ops = TAILQ_FIRST(&ops_list);
    return ops;
}

struct accl_ops *accl_find(int type) {
    struct accl_ops *ops;
    TAILQ_FOREACH(ops, &ops_list, next) {
        if ((int)ops->type == type) {
            return ops;
        }
    }
    return NULL;
}

void accl_ops_uninit(void)
{
#ifdef ENABLE_VULKAN
    vulkan_uninit();
#endif
#ifdef ENABLE_OPENCL
    opcl_amd_uninit();
#endif
}
