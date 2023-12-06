#include <stdint.h>

#include "utils.h"
#include "colorspace.h"
#include "vlog.h"

VLOG_REGISTER(clr, DEBUG)

#define SCALEBITS 10
#define ONE_HALF (1UL << (SCALEBITS - 1))
#define FIX(x) ((int)((x) * (1UL << SCALEBITS) + 0.5))

void YCbCr_to_BGRA32(uint8_t *ptr, int pitch, int16_t *Y, int16_t *Cb,
                     int16_t *Cr, int v, int h) {
#if 0
    VDBG(clr, "YCbCr :");
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            VDBG(clr, "%04x ", Y[i * 16 + j]);
        }
        VDBG(clr, "\n");
    }
#endif
    uint8_t *p, *p2;
    int offset_to_next_row;

    p = ptr;
    if (v == 2)
        p2 = ptr + pitch;
    offset_to_next_row = pitch * v - 8 * h * 4;

    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < 8; k++) {
            int y, cb, cr;
            int add_r, add_g, add_b;
            int r, g, b;

            // all YCbCr have been added by 128 in idct
            cb = *Cb++ - 128;
            cr = *Cr++ - 128;
            add_r = FIX(1.40200) * cr + ONE_HALF;
            add_g = -FIX(0.34414) * cb - FIX(0.71414) * cr + ONE_HALF;
            add_b = FIX(1.77200) * cb + ONE_HALF;
            for (int i = 0; i < h; i++) {
                y = (*Y++) << SCALEBITS;
                b = (y + add_b) >> SCALEBITS;
                g = (y + add_g) >> SCALEBITS;
                r = (y + add_r) >> SCALEBITS;
                *p++ = clamp(b, 255);
                *p++ = clamp(g, 255);
                *p++ = clamp(r, 255);
                *p++ = 0xff; // alpha
            }
            if (v == 2) {
                for (int j = 0; j < h; j++) {
                    y = (Y[8 * h - 1 * h + j]) << SCALEBITS;
                    b = (y + add_b) >> SCALEBITS;
                    g = (y + add_g) >> SCALEBITS;
                    r = (y + add_r) >> SCALEBITS;
                    *p2++ = clamp(b, 255);
                    *p2++ = clamp(g, 255);
                    *p2++ = clamp(r, 255);
                    *p2++ = 0xff;
                }
            }
        }
        if (v == 2)
            Y += 8 * h;
        p += offset_to_next_row;
        if (v == 2)
            p2 += offset_to_next_row;
    }
}

void YUV420_to_BGRA32(uint8_t *ptr, int pitch, uint8_t *yout, uint8_t *uout,
                      uint8_t *vout, int y_stride, int uv_stride, int mbrows,
                      int mbcols) {
    uint8_t *p = ptr, *p2 = ptr;
    int width = mbcols * 16;
    int right_space = pitch - width * 4;
    uint8_t *Y, *U, *V;
    int16_t yy, u, v;
    uint8_t r, g, b;

    for (int y = 0; y < mbrows; y++) {
        for (int x = 0; x < mbcols; x++) {
            Y = yout + y_stride * y * 16 + x * 16;
            U = uout + 8 * uv_stride * y + x * 8;
            V = vout + 8 * uv_stride * y + x * 8;
            p = p2;
            for (int i = 0; i < 16; i++) {
                if (i == 0) {
                    p2 = p + 16 * 4;
                }
                for (int j = 0; j < 16; j++) {
                    yy = Y[i * y_stride + j] - 16;
                    u = U[(i / 2) * uv_stride + (j / 2)] - 128;
                    v = V[(i / 2) * uv_stride + (j / 2)] - 128;
                    r = clamp(yy + 1.4075 * v, 255);
                    g = clamp(yy - 0.3455 * u - 0.7169 * v, 255);
                    b = clamp(yy + 1.779 * u, 255);
                    p[4 * j] = b;
                    p[4 * j + 1] = g;
                    p[4 * j + 2] = r;
                    p[4 * j + 3] = 0xFF;
                }
                p += pitch;
            }
        }
        p2 = p - pitch + 16 * 4 + right_space;
    }
}

void YUV420_to_BGRA32_16bit(uint8_t *ptr, int pitch, int16_t *yout, int16_t *uout,
                      int16_t *vout, int y_stride, int uv_stride, int mbrows,
                      int mbcols, int ctbsize) {
    uint8_t *p = ptr, *p2 = ptr;
    int width = mbcols * ctbsize;
    int right_space = pitch - width * 4;
    int16_t *Y, *U, *V;
    int16_t yy, u, v;
    uint8_t r, g, b;

    for (int y = 0; y < mbrows; y++) {
        for (int x = 0; x < mbcols; x++) {
            Y = yout + y_stride * y * ctbsize + x * ctbsize;
            U = uout + ctbsize / 2 * uv_stride * y + x * ctbsize/2;
            V = vout + ctbsize / 2 * uv_stride * y + x * ctbsize/2;
            p = p2;
            for (int i = 0; i < ctbsize; i++) {
                if (i == 0) {
                    p2 = p + ctbsize * 4;
                }
                for (int j = 0; j < ctbsize; j++) {
                    yy = Y[i * y_stride + j] - ctbsize;
                    u = U[(i / 2) * uv_stride + (j / 2)] - 128;
                    v = V[(i / 2) * uv_stride + (j / 2)] - 128;
                    r = clamp(yy + 1.4075 * v, 255);
                    g = clamp(yy - 0.3455 * u - 0.7169 * v, 255);
                    b = clamp(yy + 1.779 * u, 255);
                    p[4 * j] = b;
                    p[4 * j + 1] = g;
                    p[4 * j + 2] = r;
                    p[4 * j + 3] = 0xFF;
                }
                p += pitch;
            }
        }
        p2 = p - pitch + ctbsize * 4 + right_space;
    }
}