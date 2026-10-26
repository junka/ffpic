#ifndef _LZW_H_
#define _LZW_H_

#ifdef __cplusplus
extern "C"{
#endif

/* return the decoded length, or -1 if the parameters are invalid or the
 * output would exceed out_capacity
 */
int lzw_decode_gif(int legacy, int code_size, const uint8_t* compressed, 
        int compressed_length, uint8_t* decompressed, int out_capacity);

int lzw_decode_tiff(int legacy, int codesize, const uint8_t *compressed, 
        int compressed_length, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /*_LZW_H_*/
