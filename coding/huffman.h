#ifndef _HUFFMAN_H_
#define _HUFFMAN_H_

#ifdef __cplusplus
extern "C"{
#endif

typedef struct huffman_tree {
	int* tree2d;
	unsigned* tree1d;
	uint8_t* lengths; /*the lengths of the codes of the 1d-tree*/
	int maxbitlen;	/*maximum number of bits a single code can get */
	int numcodes;	/*number of symbols in the alphabet = number of codes */
} huffman_tree;


void huffman_tree_init(huffman_tree* tree, int* buffer, int numcodes, int maxbitlen);

void huffman_tree_create_lengths(huffman_tree* tree, const unsigned *bitlen);

int huffman_decode_symbol(uint8_t *in, int *bp, huffman_tree* codetree, int inbitlength);

#ifdef __cplusplus
}
#endif

#endif /*_HUFFMAN_H_*/