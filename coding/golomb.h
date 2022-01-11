#ifndef _GOLOMB_H_
#define _GOLOMB_H_

#ifdef __cplusplus
extern "C"{
#endif

struct golomb_dec {
    uint64_t value;
    int count;
    int kexp;
    struct bits_vec *bits;
};

/* init a kth exp golomb decoder */
struct golomb_dec *golomb_init(uint8_t * start, int len, int k);

void golomb_free(struct golomb_dec *dec);

uint16_t golomb_decode_unsigned_value(struct golomb_dec *dec);

int16_t golomb_decode_signed_value(struct golomb_dec *dec);


#define GOLOMB_UE(d)   golomb_decode_unsigned_value(d)

#define GOLOMB_SE(d)   golomb_decode_signed_value(d)


#ifdef __cplusplus
}
#endif

#endif /*_GOLOMB_H_*/