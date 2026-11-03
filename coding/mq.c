#include <stdint.h>
#include <stddef.h>

#include "mq.h"

/*
 * MQ coder state table from ISO/IEC 15444-1 Table C.2, laid out as OpenJPEG
 * does: states[2*i+0] is the MPS=0 entry, states[2*i+1] the MPS=1 entry, and
 * the MPS switch is encoded by nlps pointing at the opposite-MPS entry.
 */
static const struct mq_state mq_states[47 * 2] = {
    {0x5601, 0, 2, 3},
    {0x5601, 1, 3, 2},
    {0x3401, 0, 4, 12},
    {0x3401, 1, 5, 13},
    {0x1801, 0, 6, 18},
    {0x1801, 1, 7, 19},
    {0x0ac1, 0, 8, 24},
    {0x0ac1, 1, 9, 25},
    {0x0521, 0, 10, 58},
    {0x0521, 1, 11, 59},
    {0x0221, 0, 76, 66},
    {0x0221, 1, 77, 67},
    {0x5601, 0, 14, 13},
    {0x5601, 1, 15, 12},
    {0x5401, 0, 16, 28},
    {0x5401, 1, 17, 29},
    {0x4801, 0, 18, 28},
    {0x4801, 1, 19, 29},
    {0x3801, 0, 20, 28},
    {0x3801, 1, 21, 29},
    {0x3001, 0, 22, 34},
    {0x3001, 1, 23, 35},
    {0x2401, 0, 24, 36},
    {0x2401, 1, 25, 37},
    {0x1c01, 0, 26, 40},
    {0x1c01, 1, 27, 41},
    {0x1601, 0, 58, 42},
    {0x1601, 1, 59, 43},
    {0x5601, 0, 30, 29},
    {0x5601, 1, 31, 28},
    {0x5401, 0, 32, 28},
    {0x5401, 1, 33, 29},
    {0x5101, 0, 34, 30},
    {0x5101, 1, 35, 31},
    {0x4801, 0, 36, 32},
    {0x4801, 1, 37, 33},
    {0x3801, 0, 38, 34},
    {0x3801, 1, 39, 35},
    {0x3401, 0, 40, 36},
    {0x3401, 1, 41, 37},
    {0x3001, 0, 42, 38},
    {0x3001, 1, 43, 39},
    {0x2801, 0, 44, 38},
    {0x2801, 1, 45, 39},
    {0x2401, 0, 46, 40},
    {0x2401, 1, 47, 41},
    {0x2201, 0, 48, 42},
    {0x2201, 1, 49, 43},
    {0x1c01, 0, 50, 44},
    {0x1c01, 1, 51, 45},
    {0x1801, 0, 52, 46},
    {0x1801, 1, 53, 47},
    {0x1601, 0, 54, 48},
    {0x1601, 1, 55, 49},
    {0x1401, 0, 56, 50},
    {0x1401, 1, 57, 51},
    {0x1201, 0, 58, 52},
    {0x1201, 1, 59, 53},
    {0x1101, 0, 60, 54},
    {0x1101, 1, 61, 55},
    {0x0ac1, 0, 62, 56},
    {0x0ac1, 1, 63, 57},
    {0x09c1, 0, 64, 58},
    {0x09c1, 1, 65, 59},
    {0x08a1, 0, 66, 60},
    {0x08a1, 1, 67, 61},
    {0x0521, 0, 68, 62},
    {0x0521, 1, 69, 63},
    {0x0441, 0, 70, 64},
    {0x0441, 1, 71, 65},
    {0x02a1, 0, 72, 66},
    {0x02a1, 1, 73, 67},
    {0x0221, 0, 74, 68},
    {0x0221, 1, 75, 69},
    {0x0141, 0, 76, 70},
    {0x0141, 1, 77, 71},
    {0x0111, 0, 78, 72},
    {0x0111, 1, 79, 73},
    {0x0085, 0, 80, 74},
    {0x0085, 1, 81, 75},
    {0x0049, 0, 82, 76},
    {0x0049, 1, 83, 77},
    {0x0025, 0, 84, 78},
    {0x0025, 1, 85, 79},
    {0x0015, 0, 86, 80},
    {0x0015, 1, 87, 81},
    {0x0009, 0, 88, 82},
    {0x0009, 1, 89, 83},
    {0x0005, 0, 90, 84},
    {0x0005, 1, 91, 85},
    {0x0001, 0, 90, 86},
    {0x0001, 1, 91, 87},
    {0x5601, 0, 92, 92},
    {0x5601, 1, 93, 93},
};

static uint8_t
mq_byte(const struct mq_dec *m, size_t i)
{
    return i < m->len ? m->buf[i] : 0xFF;
}

/*
 * BYTEIN (C.3.4): pull one byte into c, keeping the 0xFF stuffing rule.
 * Bytes past the end of the buffer read as 0xFF, which makes the state
 * machine stop exactly like OpenJPEG's artificial 0xFF 0xFF marker.
 */
static void
mq_dec_bytein(struct mq_dec *m)
{
    uint32_t l_c = mq_byte(m, m->pos + 1);

    if (mq_byte(m, m->pos) == 0xFF) {
        if (l_c > 0x8F) {
            m->c += 0xFF00;
            m->ct = 8;
        } else {
            m->pos++;
            m->c += l_c << 9;
            m->ct = 7;
        }
    } else {
        m->pos++;
        m->c += l_c << 8;
        m->ct = 8;
    }
}

/* RENORMD (C.3.3): normalize a, feeding bytes as needed */
static void
mq_dec_renorm(struct mq_dec *m)
{
    do {
        if (m->ct == 0) {
            mq_dec_bytein(m);
        }
        m->a <<= 1;
        m->c <<= 1;
        m->ct--;
    } while (m->a < 0x8000);
}

void
mq_dec_init(struct mq_dec *m, const uint8_t *buf, size_t len)
{
    m->buf = buf;
    m->len = len;
    m->pos = 0;
    m->c = (uint32_t)mq_byte(m, 0) << 16;
    mq_dec_bytein(m);
    m->c <<= 7;
    m->ct -= 7;
    m->a = 0x8000;
    mq_dec_reset(m);
}

void
mq_dec_reset(struct mq_dec *m)
{
    int i;

    for (i = 0; i < MQ_CTX_NUM; i++) {
        m->ctx[i] = &mq_states[0];
    }
    /*
     * OpenJPEG's opj_mqc_setstate(ctxno, msb=0, prob) selects
     * &states[msb + prob * 2], so the initial contexts are table entries
     * 8 (ZC, probability state 4), 6 (AGG, state 3) and 92 (UNI, state 46).
     * T1_CTXNO_AGG is 17 and T1_CTXNO_UNI is 18.
     */
    m->ctx[0] = &mq_states[8];  /* T1_CTXNO_ZC  */
    m->ctx[17] = &mq_states[6]; /* T1_CTXNO_AGG */
    m->ctx[18] = &mq_states[92];/* T1_CTXNO_UNI */
}

/* DECODE (C.3.5), with MPS_EXCHANGE / LPS_EXCHANGE inlined */
uint32_t
mq_dec_decode(struct mq_dec *m, int ctxno)
{
    uint32_t d, qe;
    const struct mq_state *st = m->ctx[ctxno];

    qe = st->qeval;
    m->a -= qe;
    if ((m->c >> 16) < qe) {
        /* LPS exchange */
        if (m->a < qe) {
            m->a = qe;
            d = st->mps;
            st = &mq_states[st->nmps];
        } else {
            m->a = qe;
            d = (uint32_t)st->mps ^ 1;
            st = &mq_states[st->nlps];
        }
        mq_dec_renorm(m);
    } else {
        m->c -= qe << 16;
        if ((m->a & 0x8000) == 0) {
            /* MPS exchange */
            if (m->a < qe) {
                d = (uint32_t)st->mps ^ 1;
                st = &mq_states[st->nlps];
            } else {
                d = st->mps;
                st = &mq_states[st->nmps];
            }
            mq_dec_renorm(m);
        } else {
            d = st->mps;
        }
    }
    m->ctx[ctxno] = st;

    return d;
}
