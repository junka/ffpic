#ifndef _COLORSPACE_H_
#define _COLORSPACE_H_

#ifdef __cplusplus
extern "C" {
#endif

void YCbCr_to_BGRA32(uint8_t *ptr, int pitch, int16_t *Y, int16_t *Cb,
                     int16_t *Cr, int v, int h);

void YUV420_to_BGRA32(uint8_t *ptr, int pitch, uint8_t *yout, uint8_t *uout,
                      uint8_t *vout, int y_stride, int uv_stride, int mbrows,
                      int mbcols);
#ifdef __cplusplus
}
#endif

#endif