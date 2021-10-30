#ifndef _LZW_H_
#define _LZW_H_

#ifdef __cplusplus
extern "C"{
#endif

int lzw_decode_gif(int legacy, int code_size, const uint8_t* compressed, 
        int compressed_length, uint8_t* decompressed);

int lzw_decode_tiff(int legacy, int codesize, const uint8_t *compressed, 
        int compressed_length, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /*_LZW_H_*/