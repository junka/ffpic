#ifndef _DEFLATE_H_
#define _DEFLATE_H_

#ifdef __cplusplus
extern "C"{
#endif

struct zlib_header {
    uint8_t compress_method : 4;
    uint8_t compression_info : 4;
    uint8_t FCHECK: 5;
    uint8_t preset_dict: 1;
    uint8_t compression_level: 2;
    //uint32_t DICTID; //only present when preset_dict is set, for png we don't have it
};

/* decode comp buffer with comp_len to decomp buffer */
int deflate_decode(uint8_t* comp, int comp_len, uint8_t* decomp, int * decomp_len);

#ifdef __cplusplus
}
#endif

#endif /*_DEFLATE_H_*/
