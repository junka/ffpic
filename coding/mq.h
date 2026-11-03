#ifndef _MQ_H_
#define _MQ_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include <stddef.h>

#define MQ_CTX_NUM 19

/* ISO/IEC 15444-1 Table C.2 entry, transition targets are table indices */
struct mq_state {
    uint16_t qeval;
    uint8_t mps;
    uint8_t nmps;
    uint8_t nlps;
};

struct mq_dec {
    uint32_t a, c, ct;
    const uint8_t *buf;
    size_t len, pos;        /* byte index instead of pointer, no past-end math */
    const struct mq_state *ctx[MQ_CTX_NUM];
};

void mq_dec_init(struct mq_dec *m, const uint8_t *buf, size_t len);
void mq_dec_reset(struct mq_dec *m);
uint32_t mq_dec_decode(struct mq_dec *m, int ctxno);

#ifdef __cplusplus
}
#endif

#endif /*_MQ_H_*/
