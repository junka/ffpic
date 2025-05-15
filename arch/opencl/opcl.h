#ifndef _OP_CL_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

void opcl_amd_init(void);
void opcl_amd_uninit(void);

#ifdef __cplusplus
}
#endif

#endif
