#ifndef _HUFFMAN_H_
#define _HUFFMAN_H_

#ifdef __cplusplus
extern "C"{
#endif

#define MAX_BIT_LEN (16)
#define FAST_HF_SIZE (256)

typedef struct huffman_tree {
	uint8_t dc_ac;
	uint8_t tid;
	uint8_t fast[2][FAST_HF_SIZE];	/* lookup table for 8 bit codec, align all codes to 8 bits*/
									/* 0 for codes, 1 for symbols */
	uint16_t* slow[MAX_BIT_LEN - 8][2];	   /* symbols length over 8 bits */
	uint8_t slow_cnt[MAX_BIT_LEN - 8];
	int maxbitlen;	   /*maximum number of bits a single code can get */
	int numcodes;	   /*number of symbols in the alphabet = number of codes */
} huffman_tree;


huffman_tree* huffman_tree_init();

void huffman_cleanup(huffman_tree *t);

void huffman_build_lookup_table(huffman_tree* tree, uint8_t dc, uint8_t id, const uint8_t count[16], const uint8_t syms[]);

void huffman_dump_table(huffman_tree *tree);

void huffman_decode_start(uint8_t *in, int inbytelen);

int huffman_decode_symbol(huffman_tree* codetree);

int huffman_read_symbol(int n);

void huffman_decode_end(void);

#ifdef __cplusplus
}
#endif

#endif /*_HUFFMAN_H_*/