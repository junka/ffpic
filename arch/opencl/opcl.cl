
__kernel void idct_4x4(__global short *in, __global short *out, __global char *transMatrix, uint depth)
{
    int bdShift = 20 - depth;
    int x = get_local_id(0);
    int y = get_local_id(1);

    int dimx = get_local_size(0);
    int dimy = get_local_size(1);

    int groupx = get_group_id(0);
    int groupy = get_group_id(1);

    int globalx = get_global_id(0);
    int globaly = get_global_id(1);

    short tmp[16];
    int val = 0;
    for (int i = 0; i < 4; i++) {
        val += transMatrix[i + x * 4] * in[4 * globaly + i];
    }
    tmp[y + globaly * dimx] = clamp((val+(7-1))>>7, -(1<<15), 1<<14);

    barrier(CLK_LOCAL_MEM_FENCE);

    val = 0;
    for (int i = 0; i < 4; i++) {
        val += transMatrix[i + x * 4] * tmp[4 * globaly + i];
    }
    out[x + globaly * dimx] = clamp(((val+(bdShift-1))>>bdShift), -(1<<15), 1<<14);
}

