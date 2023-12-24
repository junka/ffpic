
__kernel void idct_4x4(__global short *in, __global short *out, __global char *transMatrix, __local short* intern, uint depth)
{
    int bdShift = 20 - depth;
    int x = get_local_id(0);
    int y = get_local_id(1);

    // int dimx = get_local_size(0);
    // int dimy = get_local_size(1);

    // int groupx = get_group_id(0);
    // int groupy = get_group_id(1);

    int globalx = get_global_id(0);
    int globaly = get_global_id(1);

    int idx = globaly * 4 + globalx;

    int val = 0;
    for (int i = 0; i < 4; i++) {
        val += transMatrix[x + i * 4] * in[4 * globaly + i];
    }
    intern[y + 4 * x] = clamp((val+(7-1))>>7, -(1<<15), 1<<14);

    barrier(CLK_LOCAL_MEM_FENCE);

    val = 0;
    for (int i = 0; i < 4; i++) {
        val += transMatrix[i + x * 4] * intern[4 * i + y];
    }
    out[idx] = clamp(((val+(bdShift-1))>>bdShift), -(1<<15), 1<<14);
}

