#ifndef _OP_CL_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__
// the umbrella header also pulls in the deprecated GCL and GL extensions,
// which the Apple SDK annotates with _Nullable/_Nonnull; -pedantic rejects
// those annotations, so include just the core API
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

void opcl_amd_init(void);
void opcl_amd_uninit(void);

#ifdef __cplusplus
}
#endif

#endif
