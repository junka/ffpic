#include <stdint.h>

#include "utils.h"
#include "colorspace.h"
#include "vlog.h"

VLOG_REGISTER(clr, DEBUG)

// #define SCALEBITS 10
// #define ONE_HALF (1UL << (SCALEBITS - 1))
// #define FIX(x) ((int)((x) * (1UL << SCALEBITS) + 0.5))

static void
YUV_to_BGRA32_16bit(uint8_t *ptr, int pitch, void *Y, void *U,
                    void *V, int v, int h)
{
    int16_t *sY = (int16_t *)Y;
    int16_t *sU = (int16_t *)U;
    int16_t *sV = (int16_t *)V;
    uint8_t *p = ptr;
    int16_t yy, uu, vv;

    for (int i = 0; i < 8 * v; i++) {
        for (int k = 0; k < 8 * h; k++) {
            int y, cb, cr;
            int add_r, add_g, add_b;
            int r, g, b;

            // The Y[64 * 4] store the Y componets, but not in the 16 * 16 array, just four 8 * 8 array
            yy = sY[((i / 8) * h + (k / 8)) * 64 + (i % 8) * 8 + k % 8];
            uu = sU[(i / v) * 8 + (k / h)] - 128;
            vv = sV[(i / v) * 8 + (k / h)] - 128;

            //see JFIF3
            // add_r = FIX(1.40200) * vv + ONE_HALF;
            // add_g = -FIX(0.34414) * uu - FIX(0.71414) * vv + ONE_HALF;
            // add_b = FIX(1.772) * uu + ONE_HALF;

            // y = (yy) << SCALEBITS;
            // b = (y + add_b) >> SCALEBITS;
            // g = (y + add_g) >> SCALEBITS;
            // r = (y + add_r) >> SCALEBITS;

            r = clamp(yy + 1.280 * vv, 255);
            g = clamp(yy - 0.215 * uu - 0.381 * vv, 255);
            b = clamp(yy + 2.128 * uu, 255);
            p[4 * k] = b;
            p[4 * k + 1] = g;
            p[4 * k + 2] = r;
            p[4 * k + 3] = 0xff; // alpha
        }
        p += pitch;
    }
}

static void
YUV_to_BGRA32_8bit(uint8_t *ptr, int pitch, void *Y, void *U, void *V,
                   int v, int h)
{
    uint8_t *sY = Y;
    uint8_t *sU = U;
    uint8_t *sV = V;
    uint8_t *p = ptr;
    int yy, uu, vv;
    int add_r, add_g, add_b;
    int r, g, b;

    for (int i = 0; i < 8 * v; i++) {
        for (int k = 0; k < 8 * h; k++) {

            // The Y[64 * 4] store the Y componets, but not in the 16 * 16
            // array, just four 8 * 8 array
            yy = sY[((i / 8) * h + (k / 8)) * 64 + (i % 8) * 8 + k % 8];
            uu = sU[(i / v) * 8 + (k / h)] - 128;
            vv = sV[(i / v) * 8 + (k / h)] - 128;

            // see JFIF3 page 3
            // add_r = FIX(1.40200) * vv + ONE_HALF;
            // add_g = -FIX(0.34414) * uu - FIX(0.71414) * vv + ONE_HALF;
            // add_b = FIX(1.772) * uu + ONE_HALF;

            // y = (yy) << SCALEBITS;
            // b = (y + add_b) >> SCALEBITS;
            // g = (y + add_g) >> SCALEBITS;
            // r = (y + add_r) >> SCALEBITS;

            r = clamp(yy + 1.280 * vv, 255);
            g = clamp(yy - 0.215 * uu - 0.381 * vv, 255);
            b = clamp(yy + 2.128 * uu, 255);
            p[4 * k] = b;
            p[4 * k + 1] = g;
            p[4 * k + 2] = r;
            p[4 * k + 3] = 0xff; // alpha
        }
        p += pitch;
    }
}

void BGRA32_to_YUV420(uint8_t *ptr, int pitch, int16_t *Y, int16_t *U, int16_t *V)
{
    int r, g, b;
    uint8_t *p = ptr;
    uint8_t *p2 = ptr;
    for (int k = 0; k < 4; k ++) {
        p = p2;
        if (k == 1) {
            p2 = ptr + pitch * 8;
        } else {
            p2 = p + 8 * 4;
        }
        for (int j = 0; j < 8; j ++) {
            for (int i = 0; i < 8; i ++) {
                b = *(p + 4 * i);
                g = *(p + 4 * i + 1);
                r = *(p + 4 * i + 2);
                Y[j *8 + i] = 0.299 * r + 0.587 * g + 0.114 * b;
                if (k == 0) {
                    U[j * 8 + i] = -0.1687 * r - 0.3313 * g + 0.5 * b + 128;
                    V[j * 8 + i] = 0.5 * r - 0.4187 * g - 0.0813 * b + 128;
                }
            }
            p += pitch;
        }
        Y += 64;
    }
}

void YUV420_to_BGRA32(uint8_t *ptr, int pitch, uint8_t *yout, uint8_t *uout,
                      uint8_t *vout, int y_stride, int uv_stride, int mbrows,
                      int mbcols) {
    uint8_t *p = ptr, *p2 = ptr;
    int width = mbcols << 4;
    int right_space = pitch - width * 4;
    uint8_t *Y, *U, *V;
    int16_t yy, u, v;
    uint8_t r, g, b;

    for (int y = 0; y < mbrows; y++) {
        for (int x = 0; x < mbcols; x++) {
            Y = yout + (y_stride * y + x) * 16;
            U = uout + 8 * (uv_stride * y + x);
            V = vout + 8 * (uv_stride * y + x);
            p = p2;
            p2 = p + 16 * 4;
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    yy = Y[i * y_stride + j];
                    u = U[(i / 2) * uv_stride + (j / 2)] - 128;
                    v = V[(i / 2) * uv_stride + (j / 2)] - 128;
                    // r = clamp(yy + 1.4075 * v, 255);
                    // g = clamp(yy - 0.3455 * u - 0.7169 * v, 255);
                    // b = clamp(yy + 1.779 * u, 255);
                    r = clamp(yy + 1.28 * v, 255);
                    g = clamp(yy - 0.215 * u - 0.381 * v, 255);
                    b = clamp(yy + 2.128 * u, 255);
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
            p2 = p + ctbsize * 4;
            for (int i = 0; i < ctbsize; i++) {
                for (int j = 0; j < ctbsize; j++) {
                    yy = Y[i * y_stride + j];
                    u = U[(i / 2) * uv_stride + (j / 2)] - 128;
                    v = V[(i / 2) * uv_stride + (j / 2)] - 128;
                    // r = clamp(yy + 1.4075 * v, 255);
                    // g = clamp(yy - 0.3455 * u - 0.7169 * v, 255);
                    // b = clamp(yy + 1.779 * u, 255);
                    r = clamp(yy + 1.280 * v, 255);
                    g = clamp(yy - 0.215 * u - 0.381 * v, 255);
                    b = clamp(yy + 2.128 * u, 255);
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

enum cs_bits_type {
  CS_BITLEN_8 = 0,
  CS_BITLEN_16 = 1,

  CS_BITLEN_MAX,
};

static const struct cs_ops cops[CS_BITLEN_MAX] = {
    {
        .YUV_to_BGRA32 = YUV_to_BGRA32_8bit,
    },
    {
        .YUV_to_BGRA32 = YUV_to_BGRA32_16bit,
    },
};

const struct cs_ops *get_cs_ops(int component_bits)
{
    return &cops[(component_bits - 1) / 8];
}

uint32_t CS_MasksToPixelFormatEnum(int bpp, uint32_t rmask, uint32_t gmask,
                                    uint32_t bmask, uint32_t amask) {
    switch (bpp) {
    case 1:
        /* SDL defaults to MSB ordering */
        return CS_PIXELFORMAT_INDEX1MSB;
    case 4:
        /* SDL defaults to MSB ordering */
        return CS_PIXELFORMAT_INDEX4MSB;
    case 8:
        if (rmask == 0) {
            return CS_PIXELFORMAT_INDEX8;
        }
        if (rmask == 0xE0 && gmask == 0x1C && bmask == 0x03 && amask == 0x00) {
            return CS_PIXELFORMAT_RGB332;
        }
        break;
    case 12:
        if (rmask == 0) {
            return CS_PIXELFORMAT_RGB444;
        }
        if (rmask == 0x0F00 && gmask == 0x00F0 && bmask == 0x000F &&
            amask == 0x0000) {
            return CS_PIXELFORMAT_RGB444;
        }
        if (rmask == 0x000F && gmask == 0x00F0 && bmask == 0x0F00 &&
            amask == 0x0000) {
            return CS_PIXELFORMAT_BGR444;
        }
        break;
    case 15:
        if (rmask == 0) {
            return CS_PIXELFORMAT_RGB555;
        }
    /* fallthrough */
    case 16:
        if (rmask == 0) {
            return CS_PIXELFORMAT_RGB565;
        }
        if (rmask == 0x7C00 && gmask == 0x03E0 && bmask == 0x001F &&
            amask == 0x0000) {
            return CS_PIXELFORMAT_RGB555;
        }
        if (rmask == 0x001F && gmask == 0x03E0 && bmask == 0x7C00 &&
            amask == 0x0000) {
            return CS_PIXELFORMAT_BGR555;
        }
        if (rmask == 0x0F00 && gmask == 0x00F0 && bmask == 0x000F &&
            amask == 0xF000) {
            return CS_PIXELFORMAT_ARGB4444;
        }
        if (rmask == 0xF000 && gmask == 0x0F00 && bmask == 0x00F0 &&
            amask == 0x000F) {
            return CS_PIXELFORMAT_RGBA4444;
        }
        if (rmask == 0x000F && gmask == 0x00F0 && bmask == 0x0F00 &&
            amask == 0xF000) {
            return CS_PIXELFORMAT_ABGR4444;
        }
        if (rmask == 0x00F0 && gmask == 0x0F00 && bmask == 0xF000 &&
            amask == 0x000F) {
            return CS_PIXELFORMAT_BGRA4444;
        }
        if (rmask == 0x7C00 && gmask == 0x03E0 && bmask == 0x001F &&
            amask == 0x8000) {
            return CS_PIXELFORMAT_ARGB1555;
        }
        if (rmask == 0xF800 && gmask == 0x07C0 && bmask == 0x003E &&
            amask == 0x0001) {
            return CS_PIXELFORMAT_RGBA5551;
        }
        if (rmask == 0x001F && gmask == 0x03E0 && bmask == 0x7C00 &&
            amask == 0x8000) {
            return CS_PIXELFORMAT_ABGR1555;
        }
        if (rmask == 0x003E && gmask == 0x07C0 && bmask == 0xF800 &&
            amask == 0x0001) {
            return CS_PIXELFORMAT_BGRA5551;
        }
        if (rmask == 0xF800 && gmask == 0x07E0 && bmask == 0x001F &&
            amask == 0x0000) {
            return CS_PIXELFORMAT_RGB565;
        }
        if (rmask == 0x001F && gmask == 0x07E0 && bmask == 0xF800 &&
            amask == 0x0000) {
            return CS_PIXELFORMAT_BGR565;
        }
        if (rmask == 0x003F && gmask == 0x07C0 && bmask == 0xF800 &&
            amask == 0x0000) {
            /* Technically this would be BGR556, but Witek says this works in
             * bug 3158 */
            return CS_PIXELFORMAT_RGB565;
        }
        break;
    case 24:
        switch (rmask) {
        case 0:
        case 0x00FF0000:
#if CS_BYTEORDER == CS_BIG_ENDIAN
            return CS_PIXELFORMAT_RGB24;
#else
            return CS_PIXELFORMAT_BGR24;
#endif
        case 0x000000FF:
#if CS_BYTEORDER == CS_BIG_ENDIAN
            return CS_PIXELFORMAT_BGR24;
#else
            return CS_PIXELFORMAT_RGB24;
#endif
        }
    case 32:
        if (rmask == 0) {
            return CS_PIXELFORMAT_RGB888;
        }
        if (rmask == 0x00FF0000 && gmask == 0x0000FF00 && bmask == 0x000000FF &&
            amask == 0x00000000) {
            return CS_PIXELFORMAT_RGB888;
        }
        if (rmask == 0xFF000000 && gmask == 0x00FF0000 && bmask == 0x0000FF00 &&
            amask == 0x00000000) {
            return CS_PIXELFORMAT_RGBX8888;
        }
        if (rmask == 0x000000FF && gmask == 0x0000FF00 && bmask == 0x00FF0000 &&
            amask == 0x00000000) {
            return CS_PIXELFORMAT_BGR888;
        }
        if (rmask == 0x0000FF00 && gmask == 0x00FF0000 && bmask == 0xFF000000 &&
            amask == 0x00000000) {
            return CS_PIXELFORMAT_BGRX8888;
        }
        if (rmask == 0x00FF0000 && gmask == 0x0000FF00 && bmask == 0x000000FF &&
            amask == 0xFF000000) {
            return CS_PIXELFORMAT_ARGB8888;
        }
        if (rmask == 0xFF000000 && gmask == 0x00FF0000 && bmask == 0x0000FF00 &&
            amask == 0x000000FF) {
            return CS_PIXELFORMAT_RGBA8888;
        }
        if (rmask == 0x000000FF && gmask == 0x0000FF00 && bmask == 0x00FF0000 &&
            amask == 0xFF000000) {
            return CS_PIXELFORMAT_ABGR8888;
        }
        if (rmask == 0x0000FF00 && gmask == 0x00FF0000 && bmask == 0xFF000000 &&
            amask == 0x000000FF) {
            return CS_PIXELFORMAT_BGRA8888;
        }
        if (rmask == 0x3FF00000 && gmask == 0x000FFC00 && bmask == 0x000003FF &&
            amask == 0xC0000000) {
            return CS_PIXELFORMAT_ARGB2101010;
        }
    }
    return CS_PIXELFORMAT_UNKNOWN;
}

const char *CS_GetPixelFormatName(uint32_t format) {
    switch (format) {
#define CASE(X)                                                                \
  case X:                                                                      \
    return #X;
        CASE(CS_PIXELFORMAT_INDEX1LSB)
        CASE(CS_PIXELFORMAT_INDEX1MSB)
        CASE(CS_PIXELFORMAT_INDEX4LSB)
        CASE(CS_PIXELFORMAT_INDEX4MSB)
        CASE(CS_PIXELFORMAT_INDEX8)
        CASE(CS_PIXELFORMAT_RGB332)
        CASE(CS_PIXELFORMAT_RGB444)
        CASE(CS_PIXELFORMAT_BGR444)
        CASE(CS_PIXELFORMAT_RGB555)
        CASE(CS_PIXELFORMAT_BGR555)
        CASE(CS_PIXELFORMAT_ARGB4444)
        CASE(CS_PIXELFORMAT_RGBA4444)
        CASE(CS_PIXELFORMAT_ABGR4444)
        CASE(CS_PIXELFORMAT_BGRA4444)
        CASE(CS_PIXELFORMAT_ARGB1555)
        CASE(CS_PIXELFORMAT_RGBA5551)
        CASE(CS_PIXELFORMAT_ABGR1555)
        CASE(CS_PIXELFORMAT_BGRA5551)
        CASE(CS_PIXELFORMAT_RGB565)
        CASE(CS_PIXELFORMAT_BGR565)
        CASE(CS_PIXELFORMAT_RGB24)
        CASE(CS_PIXELFORMAT_BGR24)
        CASE(CS_PIXELFORMAT_RGB888)
        CASE(CS_PIXELFORMAT_RGBX8888)
        CASE(CS_PIXELFORMAT_BGR888)
        CASE(CS_PIXELFORMAT_BGRX8888)
        CASE(CS_PIXELFORMAT_ARGB8888)
        CASE(CS_PIXELFORMAT_RGBA8888)
        CASE(CS_PIXELFORMAT_ABGR8888)
        CASE(CS_PIXELFORMAT_BGRA8888)
        CASE(CS_PIXELFORMAT_ARGB2101010)
        CASE(CS_PIXELFORMAT_YV12)
        CASE(CS_PIXELFORMAT_IYUV)
        CASE(CS_PIXELFORMAT_YUY2)
        CASE(CS_PIXELFORMAT_UYVY)
        CASE(CS_PIXELFORMAT_YVYU)
        CASE(CS_PIXELFORMAT_NV12)
        CASE(CS_PIXELFORMAT_NV21)
#undef CASE
    default:
        return "CS_PIXELFORMAT_UNKNOWN";
    }
}


// Hue/Saturation/Value
void BGRA32_TO_HSV(uint8_t *ptr, int pitch, int height, uint16_t* h, uint8_t *s, uint8_t *v)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < pitch; x += 4) {
            uint8_t b = ptr[x + y * pitch];
            uint8_t g = ptr[x + 1 + y * pitch];
            uint8_t r = ptr[x + 2 + y * pitch];
            uint8_t cmax = MAX(MAX(b, g), r);
            uint8_t cmin = MIN(MIN(b, g), r);
            if (cmax == cmin) {
                *h = 0;
            } else if (cmax == r) {
                if (g >= b) {
                    *h = 60 * (g - b)/(cmax - cmin);
                } else {
                    *h = 60 * (g - b) / (cmax - cmin) + 360;
                }
            } else if (cmax == g) {
                *h = 60 * (b - r) / (cmax - cmin) + 120;
            } else if (cmax == b) {
                *h = 60 * (r - g) / (cmax - cmin) + 240;
            }
            h++;
            *s++ = (cmax == 0) ? 0 : 255 - 255 * cmin/cmax;
            *v++ = cmax;
        }
    }
}
