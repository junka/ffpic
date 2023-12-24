#include <OpenCL/OpenCL.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include "accl.h"
#include "opcl.h"

#include "opcl.cl_xx.h"

//const size_t source_size = sizeof(source_str);

static cl_device_id device_id; // device ID
static cl_context context;     // context
static cl_command_queue queue; // command queue
static cl_program program;     // program
static cl_kernel kernel4;       // kernel
void opcl_idct_4x4_amd(int16_t *in, int16_t *out, int bitdepth)
{
    const char transMatrix[16] = {
        29, 55, 74, 84,
        74, 74, 0, -74,
        84, -29, -74, 55,
        55, -84, 74, -29
    };
    int size = 16 * sizeof(int16_t);
    cl_int ret;

    cl_mem dct = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 16, (void *)transMatrix, &ret);
    assert(ret == CL_SUCCESS);
    cl_mem block_data = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size, in, &ret);
    assert(ret == CL_SUCCESS);
    cl_mem block_out = clCreateBuffer(context, CL_MEM_WRITE_ONLY, size, NULL, &ret);
    assert(ret == CL_SUCCESS);

    // cl_event evt;
    // clEnqueueWriteBuffer(queue, block_data, CL_FALSE, 0, size, in, 0, NULL, &evt);

    // ret = clFlush(queue);
    // ret = clWaitForEvents(1, &evt);

    ret = clSetKernelArg(kernel4, 0, sizeof(cl_mem), (void *)&block_data);
    ret |= clSetKernelArg(kernel4, 1, sizeof(cl_mem), (void *)&block_out);
    ret |= clSetKernelArg(kernel4, 2, sizeof(cl_mem), (void *)&dct);
    ret |= clSetKernelArg(kernel4, 4, sizeof(cl_uint), (void *)&bitdepth);
    if (ret != CL_SUCCESS) {
      clReleaseMemObject(block_data);
      clReleaseMemObject(block_out);
      clReleaseKernel(kernel4);
      clReleaseProgram(program);
      clReleaseCommandQueue(queue);
      clReleaseContext(context);
      assert(ret == CL_SUCCESS);
    }
    size_t global_size[2] = {4, 4};
    size_t local_size[2] = {4, 1};
    ret = clEnqueueNDRangeKernel(queue, kernel4, 2, NULL, global_size,
                                 local_size, 0, NULL, NULL);

    if (ret != CL_SUCCESS) {
        printf("Error enqueueing kernel: %d\n", ret);
        clReleaseMemObject(block_data);
        clReleaseMemObject(block_out);
        clReleaseKernel(kernel4);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return;
    }
    ret = clEnqueueReadBuffer(queue, block_out, CL_TRUE, 0, size,
                              out, 0, NULL, NULL);
    if (ret != CL_SUCCESS) {
        printf("Error reading output buffer: %d\n", ret);
        clReleaseMemObject(block_data);
        clReleaseMemObject(block_out);
        clReleaseKernel(kernel4);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return ;
    }

    clReleaseMemObject(block_data);
    clReleaseMemObject(block_out);
    clReleaseKernel(kernel4);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
}

struct accl_ops opcl_accl = {
    .idct_4x4 = opcl_idct_4x4_amd,
    .type = GPU_TYPE_OPENCL,
};

// on Macos we would have AMD Radeon Pro 560X 4 GB
void opcl_amd_init(void) {
    const char *prog_src = (char *)&srcstr[0];
    cl_platform_id platform_ids[8];  // OpenCL platform
    cl_uint num_platforms;
    cl_device_id did;
    cl_int ret;
    char dname[512];
    cl_int err = clGetPlatformIDs(8, platform_ids, &num_platforms);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "clGetPlatformIDs failed (error %d)\n", err);
        return ;
    }
    for (cl_uint i = 0; i < num_platforms; i++) {
        const cl_platform_id pid = platform_ids[i];
        clGetPlatformInfo(pid, CL_PLATFORM_NAME, 500, dname, NULL);
        printf("Platform #%u: %s\n", i + 1, dname);
        clGetPlatformInfo(pid, CL_PLATFORM_VERSION, 500, dname, NULL);
        printf("Version: %s\n", dname);

        cl_device_id dev_ids[8];
        cl_uint num_devices;

        clGetDeviceIDs(pid, CL_DEVICE_TYPE_GPU, 8, dev_ids,
                                        &num_devices);
        if (err != CL_SUCCESS) {
            fprintf(stderr, "clGetDeviceIDs failed (error %d)\n", err);
            return;
        } else {
            for (cl_uint j = 0; j < num_devices; j++) {
                did = dev_ids[j];

                clGetDeviceInfo(did, CL_DEVICE_NAME, sizeof(dname), dname, NULL);
                if (strstr(dname, "AMD")) {
                    // choose high-performace gpu automatically
                    printf("Device #%u: Name: %s\n", j + 1, dname);
                    device_id = did;
                    break;
                }
            }
        }
    }

    clGetDeviceInfo(did, CL_DEVICE_VERSION, sizeof(dname), dname, NULL);
    printf("\tVersion: %s\n", dname);

    cl_uint max_cu;
    clGetDeviceInfo(did, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(max_cu),
                    &max_cu, NULL);
    printf("\tCompute Units: %u\n", max_cu);

    cl_ulong local_mem_size;
    clGetDeviceInfo(did, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(cl_ulong),
                    &local_mem_size, 0);
    printf("\tLocal Memory Size: %"PRIu64"\n", local_mem_size);

    cl_ulong const_mem_size;
    clGetDeviceInfo(did, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE,
                    sizeof(cl_ulong), &const_mem_size, 0);
    printf("\tConstant Memory Size: %"PRIu64"\n", const_mem_size);
    context = clCreateContext(NULL, 1, &device_id, NULL, NULL, &ret);
    if (ret != CL_SUCCESS) {
        fprintf(stderr, "clCreateContext failed (error %d)\n", err);
        return ;
    }

    queue = clCreateCommandQueue(context, device_id, 0, &ret);
    if (ret != CL_SUCCESS) {
        clReleaseContext(context);
        fprintf(stderr, "clCreateCommandQueue failed (error %d)\n", ret);
        return;
    }
    size_t len = srcstr_len;
    printf("%d: %s\n", srcstr_len, prog_src);
    program = clCreateProgramWithSource(context, 1, &prog_src,
                                        (const size_t *)&len, &ret);
    if (ret != CL_SUCCESS) {
        printf("clCreateProgramWithSource failed err %d\n", ret);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        assert(ret == CL_SUCCESS);
        return;
    }
    ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);
    if (ret != CL_SUCCESS) {
        size_t len;
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
        char *buffer = (char *)malloc(sizeof(char) * (len + 1));
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, len, buffer, &len);
        printf("Build log is %s\n", buffer);
        free(buffer);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        assert(ret == CL_SUCCESS);
        return;
    }

    kernel4 = clCreateKernel(program, "idct_4x4", &ret);
    if (ret != CL_SUCCESS) {
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        assert(ret == CL_SUCCESS);
        return;
    }

    accl_ops_register(&opcl_accl);
}
