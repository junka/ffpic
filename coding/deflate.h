#ifndef _DEFLATE_H_
#define _DEFLATE_H_

#ifdef __cplusplus
extern "C"{
#endif

void deflate_decode(const uint8_t* compressed, int compressed_length, uint8_t* decompressed, int * dec_len);

#ifdef __cplusplus
}
#endif

#endif /*_DEFLATE_H_*/