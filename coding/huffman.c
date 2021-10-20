#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "huffman.h"

#define MAX_BIT_LENGTH 16 /* largest bitlen used by any tree type */
#define MAX_SYMBOLS 256 /* largest number of symbols used by any tree type */

#define READ_BIT(x, i)	((x[(i) >> 3] >> ((i) & 0x7)) & 0x1)

static inline uint8_t  __read_bits(uint8_t *x, uint8_t i, uint8_t n)
{
	uint8_t ret = 0;
	for (uint8_t d = 0; d < n; d ++) {
		ret |= READ_BIT(x, i+d) << d;
	}
	return ret;
}

#define READ_BITS(x, i, n) __read_bits(x, i, n)

void 
huffman_tree_init(huffman_tree* tree, int* buffer, int numcodes, int maxbitlen)
{
	tree->tree2d = buffer;
	tree->tree1d = NULL;
	tree->lengths = NULL;

	tree->numcodes = numcodes;
	tree->maxbitlen = maxbitlen;
}

void 
huffman_cleanup(huffman_tree* tree)
{
	free(tree->tree2d);
	free(tree->tree1d);
	free(tree->lengths);
}

/* given the code lengths (as stored in the PNG file), generate the tree as defined by Deflate.
 * maxbitlen is the maximum bits that a code in the tree can have. return value is error. */
void huffman_tree_create_lengths(huffman_tree* tree, const unsigned *bitlen)
{
	unsigned tree1d[MAX_SYMBOLS];
	unsigned blcount[MAX_BIT_LENGTH];
	unsigned nextcode[MAX_BIT_LENGTH+1];
	unsigned bits, n, i;
	unsigned nodefilled = 0;	/*up to which node it is filled */
	unsigned treepos = 0;	/*position in the tree (1 of the numcodes columns) */

	/* initialize local vectors */
	memset(blcount, 0, sizeof(blcount));
	memset(nextcode, 0, sizeof(nextcode));

	/*step 1: count number of instances of each code length */
	for (bits = 0; bits < tree->numcodes; bits++) {
		blcount[bitlen[bits]]++;
	}

	/*step 2: generate the nextcode values */
	for (bits = 1; bits <= tree->maxbitlen; bits++) {
		nextcode[bits] = (nextcode[bits - 1] + blcount[bits - 1]) << 1;
	}

	/*step 3: generate all the codes */
	for (n = 0; n < tree->numcodes; n++) {
		if (bitlen[n] != 0) {
			tree1d[n] = nextcode[bitlen[n]]++;
		}
	}

	/*convert tree1d[] to tree2d[][]. In the 2D array, a value of 32767 means uninited, a value >= numcodes is an address to another bit, a value < numcodes is a code. The 2 rows are the 2 possible bit values (0 or 1), there are as many columns as codes - 1
	   a good huffmann tree has N * 2 - 1 nodes, of which N - 1 are internal nodes. Here, the internal nodes are stored (what their 0 and 1 option point to). There is only memory for such good tree currently, if there are more nodes (due to too long length codes), error 55 will happen */
	for (n = 0; n < tree->numcodes * 2; n++) {
		tree->tree2d[n] = 32767;	/*32767 here means the tree2d isn't filled there yet */
	}

	for (n = 0; n < tree->numcodes; n++) {	/*the codes */
		for (i = 0; i < bitlen[n]; i++) {	/*the bits for this code */
			unsigned char bit = (unsigned char)((tree1d[n] >> (bitlen[n] - i - 1)) & 1);
			/* check if oversubscribed */
			if (treepos > tree->numcodes - 2) {
				return;
			}

			if (tree->tree2d[2 * treepos + bit] == 32767) {	/*not yet filled in */
				if (i + 1 == bitlen[n]) {	/*last bit */
					tree->tree2d[2 * treepos + bit] = n;	/*put the current code in it */
					treepos = 0;
				} else {	/*put address of the next step in here, first that address has to be found of course (it's just nodefilled + 1)... */
					nodefilled++;
					tree->tree2d[2 * treepos + bit] = nodefilled + tree->numcodes;	/*addresses encoded with numcodes added to it */
					treepos = nodefilled;
				}
			} else {
				treepos = tree->tree2d[2 * treepos + bit] - tree->numcodes;
			}
		}
	}

	for (n = 0; n < tree->numcodes * 2; n++) {
		if (tree->tree2d[n] == 32767) {
			tree->tree2d[n] = 0;	/*remove possible remaining 32767's */
		}
	}
}



int
huffman_decode_symbol(uint8_t *in, int *bp, huffman_tree* codetree, int inbitlength)
{
	unsigned treepos = 0, ct;
	uint8_t bit;
	for (;;) {
		/* error: end of input memory reached without endcode */
		// if (((*bp) & 0x07) == 0 && ((*bp) >> 3) > inbitlength) {
		// 	return 0;
		// }
		if (*bp >= inbitlength) {
			return -1;
		}

		bit = READ_BIT(in, *bp);
		(*bp)++;

		ct = codetree->tree2d[(treepos << 1) | bit];
		if (ct < codetree->numcodes) {
			/* get decoded symbol */
			return ct;
		}

		treepos = ct - codetree->numcodes;
		if (treepos >= codetree->numcodes) {
			return -1;
		}
	}
}

