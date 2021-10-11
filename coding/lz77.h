#ifndef _LZ77_H_
#define _LZ77_H_

#ifdef __cplusplus
extern "C"{
#endif

int lz77_decode(const uint8_t* compressed, int compressed_length, uint8_t* decompressed);

#ifdef __cplusplus
}
#endif

#endif /*_LZ77_H_*/