#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "huffman.h"
#include "bitstream.h"
#include "vlog.h"

VLOG_REGISTER(huffman, INFO)

huffman_tree* 
huffman_tree_init(void)
{
    huffman_tree* tree = malloc(sizeof(huffman_tree));
    if (!tree) {
        return NULL;
    }
    memset(tree->fast_symbol, 0xFF, FAST_HF_SIZE * 2);
    memset(tree->fast_bitlen, 0, FAST_HF_SIZE);

    memset(tree->fast_codec, 0, FAST_HF_SIZE * 2);
    memset(tree->fast_codbits, 0, FAST_HF_SIZE);

#if SLOW_HF_BITS > 0
    memset(tree->slow_codec, 0, sizeof(uint16_t*)*(SLOW_HF_BITS));
    memset(tree->slow_symbol, 0, sizeof(uint16_t*)*(SLOW_HF_BITS));
#endif
    return tree;
}

void 
huffman_cleanup(huffman_tree* tree)
{
#if SLOW_HF_BITS > 0
    for (int i = 0; i < SLOW_HF_BITS; i++) {
        if (tree->slow_codec[i]) {
            free(tree->slow_codec[i]);
        }
        if (tree->slow_symbol[i]) {
            free(tree->slow_symbol[i]);
        }
    }
#endif
    free(tree);
}

struct huffman_symbol *huffman_symbol_alloc(uint8_t count[16], uint8_t *syms)
{
    struct huffman_symbol *sym = malloc(sizeof(struct huffman_symbol));
    memcpy(sym->count, count, 16);
    int total = 0;
    for (int i = 0; i < 16; i++) {
        total += count[i];
    }
    sym->syms = malloc(total);
    memcpy(sym->syms, syms, total);
    return sym;
}

void
huffman_dump_table(FILE *f, huffman_tree* tree)
{
    fprintf(f, "%p: tid %d\n", (void *)tree, tree->tid);
    fprintf(f, "encoding table:\n");
    for (int i = 0; i < 256; i++) {
        fprintf(f, "\t%d\t%d\t%d\n", i, tree->fast_codec[i], tree->fast_codbits[i]);
    }
    fprintf(f, "fast lookup table:\n");
    for (int i = 0; i < FAST_HF_SIZE; i ++) {
        if (tree->fast_symbol[i] == (FAST_HF_SIZE-1)) {
            break;
        }
        fprintf(f, "\t%d\t%x\t%d\n", i, tree->fast_symbol[i],
                tree->fast_bitlen[i]);
    }
#if SLOW_HF_BITS > 0
    fprintf(f, "slow lookup table:\n");
    for (int i = FAST_HF_BITS; i < MAX_BIT_LEN; i ++) {
        for (int j = 0; j < tree->slow_cnt[i-FAST_HF_BITS]; j ++) {
            fprintf(f, "\t%d\t%x\t%d\n", tree->slow_codec[i-FAST_HF_BITS][j],
                     tree->slow_symbol[i-FAST_HF_BITS][j], i+1);
        }
    }
#endif
}

/* given the code lengths, generate the tree 
 * maxbitlen is the maximum bits that a code in the tree can have. return value is error. */
void 
huffman_build_lookup_table(huffman_tree* tree, uint8_t id, struct huffman_symbol *sym)
{
    uint16_t coding[FAST_HF_SIZE];
    uint8_t bitlen[FAST_HF_SIZE];
    int bits, n = 0, i = 0;
    int maxbitlen = 0, n_codes = 0;

    tree->tid = id;
    /* initialize local vectors */
    memset(coding, 0xFFFF, sizeof(coding));
    memset(bitlen, 0, sizeof(bitlen));

    for (int j = MAX_BIT_LEN - 1; j >= 0; j--) {
        if (sym->count[j] != 0 ) {
            maxbitlen = j+1;
            break;
        }
    }
    tree->maxbitlen = maxbitlen;
    bits = maxbitlen;
    if(maxbitlen > FAST_HF_BITS)
        maxbitlen = FAST_HF_BITS;

#if SLOW_HF_BITS > 0
    //we need slow table when max bits greater than 8
    for (int j = FAST_HF_BITS; j < bits; j ++) {
        tree->slow_cnt[j - FAST_HF_BITS] = sym->count[j];
        tree->slow_codec[j - FAST_HF_BITS] = malloc(sym->count[j] * sizeof(uint16_t));
        tree->slow_symbol[j - FAST_HF_BITS] = malloc(sym->count[j] * sizeof(uint16_t));
    }
#endif

    /* rule 1: start coding from 0 */
    int cod = 0;
    for (i = 0; i < MAX_BIT_LEN; i ++) {
        /* rule 2: same bit len codecs are continuous */
        for (int k = 0; k < sym->count[i]; k++) {
            coding[n_codes] = cod;
            // printf("%d coding %d, bit %d\n",n_codes, cod, i+1);
            bitlen[n_codes] = i + 1;
            n_codes ++;
            cod += 1;
        }
        /* rule 3: codec for len+1 should start from 2 * (codec + 1) */
        cod <<= 1;
    }
    tree->n_codes = n_codes;

    for (i = 0; i < n_codes; i++) {
        /* for encoding table */
        tree->fast_codec[sym->syms[i]] = coding[i];
        tree->fast_codbits[sym->syms[i]] = bitlen[i];

        /* for decoding, build fast lookup table */
        if (bitlen[i] <= FAST_HF_BITS) {
            int l = ((int)1 << (FAST_HF_BITS - bitlen[i]));
            int cd = coding[i] << (FAST_HF_BITS - bitlen[i]);
            while (l--) {
                tree->fast_symbol[cd] = sym->syms[i];
                tree->fast_bitlen[cd] = bitlen[i];
                cd += 1;
            }
        }
#if SLOW_HF_BITS > 0
        else {
            /* build slow lookup list */
            if (bitlen[i] > bitlen[i - 1]) {
                n = 0;
            }
            tree->slow_codec[bitlen[i] - FAST_HF_BITS - 1][n] = coding[i];
            tree->slow_symbol[bitlen[i] - FAST_HF_BITS - 1][n] = sym->syms[i];
            n ++;
        }
#endif
    }
}

int
huffman_decode_symbol(struct huffman_codec *codec, huffman_tree* tree)
{
    struct bits_vec * v = codec->v;
    int ct = 0xFF;
    int c = -1, bl = 0;
    int trybits = FAST_HF_BITS;

remaining:
    if (EOF_BITS(v, trybits)) {
        if (trybits == FAST_HF_BITS) {
            trybits = bits_vec_left_bits(v);
            if (trybits > 0){
                // printf("try %d bits\n", trybits);
                goto remaining;
            }
        }
        VERR(huffman, "end of stream %ld, %d", v->ptr - v->start, v->len);
        return -1;
    }
    c = READ_BITS(v, trybits);
    if (c == -1) {
        VERR(huffman, "invalid bits read");
        return -1;
    }

    /* looup fast table first */
    ct = tree->fast_symbol[c];
    if (ct != 0xFFFF) {
        /* step back n bits if decoded symbol bit len less than we read */
        STEP_BACK(v, trybits - tree->fast_bitlen[c]);
        VDBG(huffman, "symbol %d, bitlen %d for (%x)", ct, tree->fast_bitlen[c], c);
        return ct;
    }

#if SLOW_HF_BITS > 0
    if (tree->maxbitlen >= FAST_HF_BITS) {
        /* fast table miss, try slow table then */
        bl = FAST_HF_BITS; /* which mean bitlen */
        while (bl < tree->maxbitlen) {
            c = ((c << 1) | READ_BIT(v));
            for (int i =0; i < tree->slow_cnt[bl - FAST_HF_BITS]; i ++) {
                if ( c == tree->slow_codec[bl - FAST_HF_BITS][i]) {
                    VDBG(huffman, "offset %d decode %d, bitlen %d", v->offset, c, bl);
                    return tree->slow_symbol[bl - FAST_HF_BITS][i];
                }
            }
            bl ++;
        }
    }
#endif
    VERR(huffman, "why here, bl %d , maxbitlen %d, c %x", bl, tree->maxbitlen, c);
    return -1;
}

struct huffman_codec* huffman_codec_init(uint8_t *in, int len) {
    struct huffman_codec *codec = malloc(sizeof(struct huffman_codec));
    if (in == NULL || len == 0) {
        codec->v = bits_writer_reserve(BITS_MSB);
    } else {
        codec->v = bits_vec_alloc(in, len, BITS_MSB);
    }
    return codec;
}

void
huffman_reset_stream(struct huffman_codec *codec)
{
    RESET_BORDER(codec->v);
}

void
huffman_codec_free(struct huffman_codec *codec)
{
    bits_vec_free(codec->v);
    free(codec);
}

static int
sort_symbol_descrease(int freq[FAST_HF_SIZE], uint8_t symbols[FAST_HF_SIZE])
{
    int f[FAST_HF_SIZE];
    int j = 0;
    memcpy(f, freq, sizeof(int) * FAST_HF_SIZE);
    for (j = 0; j < FAST_HF_SIZE; j++) {
        int most = 0;
        uint8_t sym = 0;
        for (int i = 0; i < FAST_HF_SIZE; i++) {
            if (most <= f[i]) {
                most = f[i];
                sym = i;
            }
        }
        if (f[sym] == 0) {
            //stop if prob is 0, all the left should be zero
            break;
        }
        symbols[j] = sym;
        // printf("%d %c %d\n", j, sym, freq[sym]);
        f[sym] = 0;
    }
    return j;
}

typedef struct hnode {
    unsigned char sym;
    unsigned int freq;
    int codelen;
    struct hnode *left;
    struct hnode *right;
} hnode;

static void free_hnode(hnode *parent) {
    hnode *left = parent->left;
    hnode *right = parent->right;
    if (left) {
        free_hnode(left);
    }
    if (right) {
        free_hnode(right);
    }
    free(parent);
}

static void
calc_huffman_codelen(hnode *parent, uint8_t count[16])
{
    hnode *left = parent->left;
    hnode *right = parent->right;
    if (!left && !right) {
        count[parent->codelen-1]++;
        // printf("%c count for %d: %d\n",parent->sym, parent->codelen, count[parent->codelen]);
    }
    if (left) {
        left->codelen = parent->codelen + 1;
        calc_huffman_codelen(left, count);
    }
    if (right) {
        right->codelen = parent->codelen + 1;
        calc_huffman_codelen(right, count);
    }
}

struct huffman_tree *huffman_scan_buff(uint8_t *data, int len, int id) {
    int freq[FAST_HF_SIZE] = {0};
    uint8_t symbols[FAST_HF_SIZE];
    for (int i = 0; i < len; i ++) {
        freq[data[i]]++;
    }
    int sym_count = sort_symbol_descrease(freq, symbols);
    uint8_t count[16] = {0}; // put codelen here
    hnode **n = malloc(sizeof(hnode *) * sym_count);
    for (int i = 0; i < sym_count; i++) {
        n[i] = malloc(sizeof(hnode));
        n[i]->left = NULL;
        n[i]->right = NULL;
        n[i]->sym = symbols[sym_count - 1 - i];
        n[i]->freq = freq[symbols[sym_count - 1 - i]];
        VDBG(huffman, "%c %d", n[i]->sym, n[i]->freq);
    }

    hnode *p;
    int start = 0;
    while (start + 1 < sym_count) {
        hnode *prev = n[start];
        hnode *next = n[start+1];
        p = malloc(sizeof(hnode));
        p->left = prev;
        p->right = next;
        p->freq = prev->freq + next->freq;

        // insert it back to the heap
        int i = start + 2;
        while (i < sym_count && p->freq > n[i]->freq) {
            n[i-1] = n[i];
            i++;
        }
        n[i-1] = p;
        start ++;
    }
    p->codelen = 0;
    calc_huffman_codelen(p, count);
    free_hnode(p);

    struct huffman_symbol *sym = huffman_symbol_alloc(count, symbols);

    struct huffman_tree *tree = huffman_tree_init();

    huffman_build_lookup_table(tree, id, sym);

    free(sym->syms);
    free(sym);
    return tree;
}

int huffman_encode_symbol(struct huffman_codec *codec, struct huffman_tree *tree, int ch)
{
    struct bits_vec *v = codec->v;
    if (tree->fast_codbits[ch]) {
        VINFO(huffman, "write [%x]%x in %d bits", ch, tree->fast_codec[ch], tree->fast_codbits[ch]);
        WRITE_BITS(v, tree->fast_codec[ch], tree->fast_codbits[ch]);
        return 0;
    }
// #if SLOW_HF_BITS > 0
//     else {
//         for (int i = FAST_HF_BITS; i < MAX_BIT_LEN; i++) {
//             for (int j = 0; j < tree->slow_cnt[i - FAST_HF_BITS]; j++) {
//                 if (tree->slow_symbol[i - FAST_HF_BITS][j] == ch) {
//                     VINFO(huffman, "write [%d]%x in %d bits", ch, tree->slow_codec[i-FAST_HF_BITS][j], i+1);
//                     WRITE_BITS(v, tree->slow_codec[i - FAST_HF_BITS][j], i+1);
//                     return 0;
//                 }
//             }
//         }
//     }
// #endif
    return -1;
}

int huffman_encode_done(struct huffman_codec *codec, uint8_t **ptr) {
    struct bits_vec *v = codec->v;
    *ptr = v->start;
    return v->len;
}
