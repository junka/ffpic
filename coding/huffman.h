#ifndef _HUFFMAN_H_
#define _HUFFMAN_H_

#include "bitstream.h"
#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include <stdio.h>

#define MAX_BIT_LEN (16)
#define FAST_HF_BITS (8)
#define FAST_HF_SIZE (1<<FAST_HF_BITS)

typedef struct huffman_tree {
    uint8_t tid;
    uint8_t fast_bitlen[FAST_HF_SIZE];    /* lookup table for 8 bit codec*/
    uint8_t fast_symbol[FAST_HF_SIZE];
    uint8_t fast_codec[FAST_HF_SIZE];                     // for encoding
    uint8_t fast_codbits[FAST_HF_SIZE];                   // for encoding
    uint16_t* slow_codec[MAX_BIT_LEN - FAST_HF_BITS];     /* symbols length over 8 bits */
    uint16_t* slow_symbol[MAX_BIT_LEN - FAST_HF_BITS];
    uint8_t slow_cnt[MAX_BIT_LEN - FAST_HF_BITS];
    int maxbitlen;       	/*maximum number of bits a single code can get */
    int n_codes;       		/*number of symbols in the alphabet = number of codes */
} huffman_tree;

struct huffman_symbol {
    uint8_t count[16];
    uint8_t* syms;
};

struct huffman_codec {
    struct bits_vec *v;
};

struct huffman_codec* huffman_codec_init(uint8_t *in, int len);

/* init huffman tree */
huffman_tree* huffman_tree_init(void);

/* destroy huffman tree */
void huffman_cleanup(huffman_tree *t);

struct huffman_symbol *huffman_symbol_alloc(uint8_t count[16],
                                            uint8_t *syms);

/* build the huffman table */
void huffman_build_lookup_table(huffman_tree *tree, uint8_t id,
                                    struct huffman_symbol *symbols);

/* dump huffman table, for debug use */
void huffman_dump_table(FILE *f, huffman_tree *tree);

int huffman_decode_symbol(struct huffman_codec *codec, huffman_tree *codetree);

int huffman_read_symbol(struct huffman_codec *codec, int n);

/* start read bitstream from next byte boundary */
void huffman_reset_stream(struct huffman_codec *codec);

void huffman_codec_free(struct huffman_codec *codec);

struct huffman_tree *huffman_scan_buff(uint8_t *data, int len, int id);

int huffman_write_symbol(void);

int huffman_encode_symbol_8bit(struct huffman_codec *codec, struct huffman_tree *tree, uint8_t ch);

int huffman_encode_done(struct huffman_codec *codec, uint8_t **ptr);

#ifdef __cplusplus
}
#endif

#endif /*_HUFFMAN_H_*/
