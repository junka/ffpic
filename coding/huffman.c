#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "huffman.h"
#include "bitstream.h"
#include "vlog.h"

VLOG_REGISTER(huffman, INFO)

#define MAX_BIT_LENGTH 16 /* largest bitlen used by any tree type */
#define MAX_SYMBOLS 256   /* largest number of symbols used by any tree type */

huffman_tree* 
huffman_tree_init(void)
{
    huffman_tree* tree = malloc(sizeof(huffman_tree));
    memset(tree->fast[0], 0xFF, FAST_HF_SIZE);
    memset(tree->slow_codec, 0, sizeof(uint16_t*)*(MAX_BIT_LEN - FAST_HF_BITS));
    memset(tree->slow_symbol, 0, sizeof(uint16_t*)*(MAX_BIT_LEN - FAST_HF_BITS));
    return tree;
}

void 
huffman_cleanup(huffman_tree* tree)
{
    for (int i = 0; i < MAX_BIT_LEN - 8; i ++) {
        if (tree->slow_codec[i]) {
            free(tree->slow_codec[i]);
        }
        if (tree->slow_symbol[i]) {
            free(tree->slow_symbol[i]);
        }
    }
    free(tree);
}

void
huffman_dump_table(FILE *f, huffman_tree* tree)
{
    fprintf(f, "%p: %s %d\n", (void *)tree, tree->dc_ac == 0 ? "DC": "AC", tree->tid);
    fprintf(f, "fast lookup table:\n");
    for (int i = 0; i < FAST_HF_SIZE; i ++) {
        fprintf(f, "\t%d\t%x\t%d\n", i, tree->fast[0][i], tree->fast[1][i]);
    }
    fprintf(f, "slow lookup table:\n");
    for (int i = FAST_HF_BITS + 1; i <= MAX_BIT_LEN; i ++) {
        for (int j = 0; j < tree->slow_cnt[i-FAST_HF_BITS-1]; j ++) {
            fprintf(f, "\t%d\t%x\t%d\n", tree->slow_codec[i-FAST_HF_BITS-1][j],
                     tree->slow_symbol[i-FAST_HF_BITS-1][j], i);
        }
    }
}

/* given the code lengths, generate the tree 
 * maxbitlen is the maximum bits that a code in the tree can have. return value is error. */
void 
huffman_build_lookup_table(huffman_tree* tree, uint8_t dc, uint8_t id, const uint8_t count[16], const uint8_t syms[])
{
    uint16_t coding[MAX_SYMBOLS];
    uint8_t bitlen[MAX_SYMBOLS];
    int bits, n = 0, i = 0;
    int maxbitlen = 0, n_codes = 0;

    tree->dc_ac = dc;
    tree->tid = id;
    /* initialize local vectors */
    memset(coding, 0xFFFF, sizeof(coding));
    memset(bitlen, 0, sizeof(bitlen));

    for (int j = MAX_BIT_LENGTH - 1; j >= 0; j--) {
        if ( count[j] != 0 ) {
            maxbitlen = j+1;
            break;
        }
    }
    tree->maxbitlen = maxbitlen;
    bits = maxbitlen;
    if(maxbitlen > FAST_HF_BITS)
        maxbitlen = FAST_HF_BITS;

    //we need slow table when max bits greater than 8
    for (int j = FAST_HF_BITS + 1; j <= bits; j ++) {
        tree->slow_cnt[j - FAST_HF_BITS - 1] = count[j-1];
        tree->slow_codec[j - FAST_HF_BITS - 1] = malloc(count[j-1] * sizeof(uint16_t));
        tree->slow_symbol[j - FAST_HF_BITS - 1] = malloc(count[j-1] * sizeof(uint16_t));
    }

    /* rule 1: start coding from 0 */
    int cod = 0;
    for (i = 0; i < MAX_BIT_LENGTH; i ++) {
        /* rule 2: same bit len codecs are continuous */
        for (int k = 0; k < count[i]; k++) {
            coding[n_codes] = cod;
            bitlen[n_codes] = i + 1;
            n_codes ++;
            cod += 1;
        }
        /* rule 3: codec for len+1 should start from 2 * (codec + 1) */
        cod <<= 1;
    }
    tree->n_codes = n_codes;

    for (i = 0; i < n_codes; i++) {
        /* build fast lookup table */
        if (bitlen[i] <= FAST_HF_BITS) {
            int l = ((int)1 << (FAST_HF_BITS - bitlen[i]));
            int cd = coding[i] << (FAST_HF_BITS - bitlen[i]);
            while (l--) {
                tree->fast[0][cd] = syms[i];
                tree->fast[1][cd] = bitlen[i];
                cd += 1;
            }
        } else {
            /* build slow lookup list */
            if (bitlen[i] > bitlen[i - 1]) {
                n = 0;
            }
            tree->slow_codec[bitlen[i] - FAST_HF_BITS - 1][n] = coding[i];
            tree->slow_symbol[bitlen[i] - FAST_HF_BITS - 1][n] = syms[i];
            n ++;
        }
    }
}

static int 
decode_symbol(struct bits_vec * v, huffman_tree* tree) {
    int ct = 0xFF;
    int c = -1, bl = 0;
    if (EOF_BITS(v, FAST_HF_BITS)) {
        VERR(huffman, "end of stream %ld, %ld\n", v->ptr - v->start, v->len);
        return -1;
    }
    c = READ_BITS(v, FAST_HF_BITS);
    if (c == -1) {
        VERR(huffman, "invalid bits read\n");
    }

    /* looup fast table first */
    if (FAST_HF_BITS) {
        ct = tree->fast[0][c];
        if (ct != 0xFF) {
            /* step back n bits if decoded symbol bit len less than we read*/
            STEP_BACK(v, FAST_HF_BITS - tree->fast[1][c]);
            return ct;
        }
    }
    if (tree->maxbitlen >= FAST_HF_BITS) {
        /* fast table miss, try slow table then */
        bl = FAST_HF_BITS + 1; /* which mean bitlen */
        while (bl <= tree->maxbitlen) {
            c = ((c << 1) | READ_BIT(v));
            for (int i =0; i < tree->slow_cnt[bl - FAST_HF_BITS -1]; i ++) {
                if ( c == tree->slow_codec[bl - FAST_HF_BITS -1][i]) {
                    VDBG(huffman, "offset %d decode %d, bitlen %d", v->offset, c, bl);
                    return tree->slow_symbol[bl - FAST_HF_BITS -1][i];
                }
            }
            bl ++;
        }
    }
    VERR(huffman, "why here, bl %d , maxbitlen %d, c %x", bl, tree->maxbitlen, c);
    return -1;
}

static struct bits_vec *vec = NULL;

void
huffman_decode_start(uint8_t *in, int inbytelen)
{
    if (vec) {
        free(vec);
    }
    vec = bits_vec_alloc(in, inbytelen, BITS_MSB);
}

int
huffman_decode_symbol(huffman_tree* tree)
{
    return decode_symbol(vec, tree);
}

int 
huffman_read_symbol(int n)
{
    if (EOF_BITS(vec, n)) {
        return -1;
    }
    int ret = READ_BITS(vec, n);
    return ret;
}

void
huffman_reset_stream(void)
{
    RESET_BORDER(vec);
}

void
huffman_decode_end(void)
{
    VDBG(huffman, "len %ld, now consume %ld", vec->len, vec->ptr - vec->start);
    free(vec);
    vec = NULL;
}
