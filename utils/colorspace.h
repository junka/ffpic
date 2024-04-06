#ifndef _COLORSPACE_H_
#define _COLORSPACE_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "byteorder.h"

void YUV420_to_BGRA32(uint8_t *ptr, int pitch, uint8_t *yout, uint8_t *uout,
                      uint8_t *vout, int y_stride, int uv_stride, int mbrows,
                      int mbcols);

void YUV420_to_BGRA32_16bit(uint8_t *ptr, int pitch, int16_t *yout,
                            int16_t *uout, int16_t *vout, int y_stride,
                            int uv_stride, int mbrows, int mbcols, int ctbsize);

void BGR24_to_YUV420(uint8_t *ptr, int pitch, int16_t *Y, int16_t *U,
                      int16_t *V);

void BGRA32_to_YUV420(uint8_t *ptr, int pitch, int16_t *Y, int16_t *U, int16_t *V);

void YUV400_to_BGRA32_8bit(uint8_t *ptr, int pitch, uint8_t *yout,
                            int y_stride, int mbrows, int mbcols, int ctbsize);

void YUV400_to_BGRA32_16bit(uint8_t *ptr, int pitch, int16_t *yout,
                            int y_stride, int mbrows, int mbcols, int ctbsize);

struct cs_ops {
    void (*YUV_to_BGRA32)(uint8_t* dst, int pitch, void *Y, void *U, void *V, int vertical, int horizontal);

    void (*YUV420_to_BGRA32)(uint8_t* dst, int pitch, void *Y, void *U, void *V);
};

const struct cs_ops * get_cs_ops(int component_bits);

// from sdl, but for compile reason, put it here

/** Pixel type. */
typedef enum
{
    CS_PIXELTYPE_UNKNOWN,
    CS_PIXELTYPE_INDEX1,
    CS_PIXELTYPE_INDEX4,
    CS_PIXELTYPE_INDEX8,
    CS_PIXELTYPE_PACKED8,
    CS_PIXELTYPE_PACKED16,
    CS_PIXELTYPE_PACKED32,
    CS_PIXELTYPE_ARRAYU8,
    CS_PIXELTYPE_ARRAYU16,
    CS_PIXELTYPE_ARRAYU32,
    CS_PIXELTYPE_ARRAYF16,
    CS_PIXELTYPE_ARRAYF32
} CS_PixelType;
/** Bitmap pixel order, high bit -> low bit. */
typedef enum {
    CS_BITMAPORDER_NONE,
    CS_BITMAPORDER_4321,
    CS_BITMAPORDER_1234
} CS_BitmapOrder;
/** Packed component order, high bit -> low bit. */
typedef enum
{
    CS_PACKEDORDER_NONE,
    CS_PACKEDORDER_XRGB,
    CS_PACKEDORDER_RGBX,
    CS_PACKEDORDER_ARGB,
    CS_PACKEDORDER_RGBA,
    CS_PACKEDORDER_XBGR,
    CS_PACKEDORDER_BGRX,
    CS_PACKEDORDER_ABGR,
    CS_PACKEDORDER_BGRA
} CS_PackedOrder;
/** Array component order, low byte -> high byte. */
/* !!! FIXME: in 2.1, make these not overlap differently with
   !!! FIXME:  CS_PACKEDORDER_*, so we can simplify CS_ISPIXELFORMAT_ALPHA */
typedef enum
{
    CS_ARRAYORDER_NONE,
    CS_ARRAYORDER_RGB,
    CS_ARRAYORDER_RGBA,
    CS_ARRAYORDER_ARGB,
    CS_ARRAYORDER_BGR,
    CS_ARRAYORDER_BGRA,
    CS_ARRAYORDER_ABGR
} CS_ArrayOrder;
/** Packed component layout. */
typedef enum
{
    CS_PACKEDLAYOUT_NONE,
    CS_PACKEDLAYOUT_332,
    CS_PACKEDLAYOUT_4444,
    CS_PACKEDLAYOUT_1555,
    CS_PACKEDLAYOUT_5551,
    CS_PACKEDLAYOUT_565,
    CS_PACKEDLAYOUT_8888,
    CS_PACKEDLAYOUT_2101010,
    CS_PACKEDLAYOUT_1010102
} CS_PackedLayout;

#define CS_DEFINE_PIXELFORMAT(type, order, layout, bits, bytes)               \
  ((1 << 28) | ((type) << 24) | ((order) << 20) | ((layout) << 16) |           \
   ((bits) << 8) | ((bytes) << 0))

#define CS_static_cast(type, expression) ((type)(expression))
#define CS_FOURCC(A, B, C, D)                                                  \
  ((CS_static_cast(uint32_t, CS_static_cast(uint8_t, (A))) << 0) |             \
   (CS_static_cast(uint32_t, CS_static_cast(uint8_t, (B))) << 8) |             \
   (CS_static_cast(uint32_t, CS_static_cast(uint8_t, (C))) << 16) |            \
   (CS_static_cast(uint32_t, CS_static_cast(uint8_t, (D))) << 24))

#define CS_DEFINE_PIXELFOURCC(A, B, C, D) CS_FOURCC(A, B, C, D)
typedef enum
{
    CS_PIXELFORMAT_UNKNOWN,
    CS_PIXELFORMAT_INDEX1LSB =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_INDEX1, CS_BITMAPORDER_4321, 0,
                               1, 0),
    CS_PIXELFORMAT_INDEX1MSB =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_INDEX1, CS_BITMAPORDER_1234, 0,
                               1, 0),
    CS_PIXELFORMAT_INDEX4LSB =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_INDEX4, CS_BITMAPORDER_4321, 0,
                               4, 0),
    CS_PIXELFORMAT_INDEX4MSB =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_INDEX4, CS_BITMAPORDER_1234, 0,
                               4, 0),
    CS_PIXELFORMAT_INDEX8 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_INDEX8, 0, 0, 8, 1),
    CS_PIXELFORMAT_RGB332 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED8, CS_PACKEDORDER_XRGB,
                               CS_PACKEDLAYOUT_332, 8, 1),
    CS_PIXELFORMAT_XRGB4444 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_XRGB,
                               CS_PACKEDLAYOUT_4444, 12, 2),
    CS_PIXELFORMAT_RGB444 = CS_PIXELFORMAT_XRGB4444,
    CS_PIXELFORMAT_XBGR4444 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_XBGR,
                               CS_PACKEDLAYOUT_4444, 12, 2),
    CS_PIXELFORMAT_BGR444 = CS_PIXELFORMAT_XBGR4444,
    CS_PIXELFORMAT_XRGB1555 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_XRGB,
                               CS_PACKEDLAYOUT_1555, 15, 2),
    CS_PIXELFORMAT_RGB555 = CS_PIXELFORMAT_XRGB1555,
    CS_PIXELFORMAT_XBGR1555 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_XBGR,
                               CS_PACKEDLAYOUT_1555, 15, 2),
    CS_PIXELFORMAT_BGR555 = CS_PIXELFORMAT_XBGR1555,
    CS_PIXELFORMAT_ARGB4444 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_ARGB,
                               CS_PACKEDLAYOUT_4444, 16, 2),
    CS_PIXELFORMAT_RGBA4444 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_RGBA,
                               CS_PACKEDLAYOUT_4444, 16, 2),
    CS_PIXELFORMAT_ABGR4444 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_ABGR,
                               CS_PACKEDLAYOUT_4444, 16, 2),
    CS_PIXELFORMAT_BGRA4444 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_BGRA,
                               CS_PACKEDLAYOUT_4444, 16, 2),
    CS_PIXELFORMAT_ARGB1555 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_ARGB,
                               CS_PACKEDLAYOUT_1555, 16, 2),
    CS_PIXELFORMAT_RGBA5551 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_RGBA,
                               CS_PACKEDLAYOUT_5551, 16, 2),
    CS_PIXELFORMAT_ABGR1555 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_ABGR,
                               CS_PACKEDLAYOUT_1555, 16, 2),
    CS_PIXELFORMAT_BGRA5551 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_BGRA,
                               CS_PACKEDLAYOUT_5551, 16, 2),
    CS_PIXELFORMAT_RGB565 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_XRGB,
                               CS_PACKEDLAYOUT_565, 16, 2),
    CS_PIXELFORMAT_BGR565 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED16, CS_PACKEDORDER_XBGR,
                               CS_PACKEDLAYOUT_565, 16, 2),
    CS_PIXELFORMAT_RGB24 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_ARRAYU8, CS_ARRAYORDER_RGB, 0,
                               24, 3),
    CS_PIXELFORMAT_BGR24 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_ARRAYU8, CS_ARRAYORDER_BGR, 0,
                               24, 3),
    CS_PIXELFORMAT_XRGB8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_XRGB,
                               CS_PACKEDLAYOUT_8888, 24, 4),
    CS_PIXELFORMAT_RGB888 = CS_PIXELFORMAT_XRGB8888,
    CS_PIXELFORMAT_RGBX8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_RGBX,
                               CS_PACKEDLAYOUT_8888, 24, 4),
    CS_PIXELFORMAT_XBGR8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_XBGR,
                               CS_PACKEDLAYOUT_8888, 24, 4),
    CS_PIXELFORMAT_BGR888 = CS_PIXELFORMAT_XBGR8888,
    CS_PIXELFORMAT_BGRX8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_BGRX,
                               CS_PACKEDLAYOUT_8888, 24, 4),
    CS_PIXELFORMAT_ARGB8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_ARGB,
                               CS_PACKEDLAYOUT_8888, 32, 4),
    CS_PIXELFORMAT_RGBA8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_RGBA,
                               CS_PACKEDLAYOUT_8888, 32, 4),
    CS_PIXELFORMAT_ABGR8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_ABGR,
                               CS_PACKEDLAYOUT_8888, 32, 4),
    CS_PIXELFORMAT_BGRA8888 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_BGRA,
                               CS_PACKEDLAYOUT_8888, 32, 4),
    CS_PIXELFORMAT_ARGB2101010 =
        CS_DEFINE_PIXELFORMAT(CS_PIXELTYPE_PACKED32, CS_PACKEDORDER_ARGB,
                               CS_PACKEDLAYOUT_2101010, 32, 4),

    /* Aliases for RGBA byte arrays of color data, for the current platform */
#if BYTE_ORDER == LITTLE_ENDIAN
    CS_PIXELFORMAT_RGBA32 = CS_PIXELFORMAT_RGBA8888,
    CS_PIXELFORMAT_ARGB32 = CS_PIXELFORMAT_ARGB8888,
    CS_PIXELFORMAT_BGRA32 = CS_PIXELFORMAT_BGRA8888,
    CS_PIXELFORMAT_ABGR32 = CS_PIXELFORMAT_ABGR8888,
#else
    CS_PIXELFORMAT_RGBA32 = CS_PIXELFORMAT_ABGR8888,
    CS_PIXELFORMAT_ARGB32 = CS_PIXELFORMAT_BGRA8888,
    CS_PIXELFORMAT_BGRA32 = CS_PIXELFORMAT_ARGB8888,
    CS_PIXELFORMAT_ABGR32 = CS_PIXELFORMAT_RGBA8888,
#endif

    CS_PIXELFORMAT_YV12 =      /**< Planar mode: Y + V + U  (3 planes) */
        CS_DEFINE_PIXELFOURCC('Y', 'V', '1', '2'),
    CS_PIXELFORMAT_IYUV =      /**< Planar mode: Y + U + V  (3 planes) */
        CS_DEFINE_PIXELFOURCC('I', 'Y', 'U', 'V'),
    CS_PIXELFORMAT_YUY2 =      /**< Packed mode: Y0+U0+Y1+V0 (1 plane) */
        CS_DEFINE_PIXELFOURCC('Y', 'U', 'Y', '2'),
    CS_PIXELFORMAT_UYVY =      /**< Packed mode: U0+Y0+V0+Y1 (1 plane) */
        CS_DEFINE_PIXELFOURCC('U', 'Y', 'V', 'Y'),
    CS_PIXELFORMAT_YVYU =      /**< Packed mode: Y0+V0+Y1+U0 (1 plane) */
        CS_DEFINE_PIXELFOURCC('Y', 'V', 'Y', 'U'),
    CS_PIXELFORMAT_NV12 =      /**< Planar mode: Y + U/V interleaved  (2 planes) */
        CS_DEFINE_PIXELFOURCC('N', 'V', '1', '2'),
    CS_PIXELFORMAT_NV21 =      /**< Planar mode: Y + V/U interleaved  (2 planes) */
        CS_DEFINE_PIXELFOURCC('N', 'V', '2', '1'),
    CS_PIXELFORMAT_EXTERNAL_OES =      /**< Android video texture format */
        CS_DEFINE_PIXELFOURCC('O', 'E', 'S', ' ')
} CS_PixelFormatEnum;

uint32_t CS_MasksToPixelFormatEnum(int bpp, uint32_t rmask, uint32_t gmask,
                                   uint32_t bmask, uint32_t amask);

const char *CS_GetPixelFormatName(uint32_t format);

void blend_BGRA32_8bit_alpha(uint8_t *fg, uint8_t *bg, int pitch, int height);

#ifdef __cplusplus
}
#endif

#endif
