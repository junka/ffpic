#ifndef _LZW_H_
#define _LZW_H_

#ifdef __cplusplus
extern "C"{
#endif

void lzw_decode(int lzw_code_size, const uint8_t* compressed, 
        int compressed_length, uint8_t* decompressed);

#ifdef __cplusplus
}
#endif

#endif /*_LZW_H_*/