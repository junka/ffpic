#ifndef _CABAC_H_
#define _CABAC_H_

#ifdef __cplusplus
extern "C"{
#endif

#define STATE_NUM  ((1 << 6) * 2)

typedef struct cabac_dec {
    uint64_t value;
    uint32_t range;     //[127, 254]
    int count;
    struct bits_vec *bits;
} cabac_dec;


uint32_t cabac_dec_bit(struct bits_vec *bits, int prob);

#define CABAC(v) cabac_dec_bit(v, 0)

#ifdef __cplusplus
}
#endif

#endif /*_CABAC_H_*/