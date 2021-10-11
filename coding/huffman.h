#ifndef _HUFFMAN_H_
#define _HUFFMAN_H_

#ifdef __cplusplus
extern "C"{
#endif

typedef struct huffman_tree {
	unsigned* tree2d;
	unsigned maxbitlen;	/*maximum number of bits a single code can get */
	unsigned numcodes;	/*number of symbols in the alphabet = number of codes */
} huffman_tree;


void 
huffman_tree_init(huffman_tree* tree, unsigned* buffer, unsigned numcodes, unsigned maxbitlen);

void huffman_tree_create_lengths(huffman_tree* tree, const unsigned *bitlen);

unsigned 
huffman_decode_symbol(const unsigned char *in, unsigned long *bp, 
                    const huffman_tree* codetree, unsigned long inlength);

#ifdef __cplusplus
}
#endif

#endif /*_HUFFMAN_H_*/