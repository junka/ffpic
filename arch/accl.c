#include <stdint.h>

#include "accl.h"
#include "x86.h"

#ifdef ENABLE_OPENCL
#include "opcl.h"
#endif

TAILQ_HEAD(accl_ops_list, accl_ops);

static struct accl_ops_list ops_list = TAILQ_HEAD_INITIALIZER(ops_list);

void accl_ops_register(struct accl_ops *ops) {
  TAILQ_INSERT_TAIL(&ops_list, ops, next);
}

void accl_ops_init(void)
{
#ifdef __AVX2__
    x86_avx2_init();
#endif
#ifdef __SSE2__
    x86_sse2_init();
#endif
#ifdef ENABLE_OPENCL
    opcl_amd_init();
#endif
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
