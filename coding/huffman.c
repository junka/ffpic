#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "huffman.h"

#define MAX_BIT_LENGTH 16 /* largest bitlen used by any tree type */
#define MAX_SYMBOLS 256   /* largest number of symbols used by any tree type */

struct bits_vec {
	uint8_t *start;
	uint8_t *ptr;
	uint8_t offset;
	size_t len;
};

static struct bits_vec * 
init_bits_vec(uint8_t *buff, int len)
{
	struct bits_vec *vec = malloc(sizeof(struct bits_vec));
	vec->start = vec->ptr = buff;
	vec->offset = 0;
	vec->len = len;
	return vec;
}

bool 
eof_bits(struct bits_vec *v, int n)
{
	if (v->ptr + n/8 - v->start >= v->len) {
		return true;
	} else if (v->ptr + n/8 - v->start == v->len - 1) {
		if (v->offset + n % 8 >= 8) {
			return true;
		}
	}
	return false;
}

void 
step_back(struct bits_vec *v, int n)
{
	while(n --) {
		if (v->offset == 0) {
			v->ptr --;
			v->offset = 7;
		} else {
			v->offset --;
		}
	}
}

int 
read_bits(struct bits_vec *v, int n)
{
	uint8_t read = 0;
	int ret = 0;
	if (v->offset > 0) {
		ret = *(v->ptr);
		ret &= ((1 << (8 - v->offset)) - 1);
		read = 8 - v->offset;
		v->ptr ++;
	}
	while (read < n) {
		ret = (ret << 8) | *(v->ptr);
		v->ptr ++;
		read += 8;
	}
	if (read > 8)
		ret >>= ((8 + n - read) % 8);
	else 
		ret >>= ((read - n)%8);
	ret &= ((1 << n) -1);
	// printf("ret %d, read %d\n", ret, read);
	if((n + v->offset)%8) {
		v->ptr --;
	}
	v->offset = ((v->offset + (n%8))%8);
	return ret;
}

int 
read_bit(struct bits_vec *v)
{
	int ret = (*(v->ptr) >> (7 - v->offset)) & 0x1;
	v->offset ++;
	if (v->offset%8 == 0) {
		v->offset = 0;
		v->ptr ++;
	}
	return ret;
}

void
skip_bits(struct bits_vec *v, int n)
{
	uint8_t skip  = 0;
	while (skip < n) {
		v->ptr ++;
		skip += 8;
	}
	v->offset = 8 -(skip - n - v->offset);
}

#define READ_BIT(v) read_bit(v)
#define READ_BITS(v, n) read_bits(v, n)
#define SKIP_BITS(v, n) skip_bits(v, n)
#define STEP_BACK(v, n) step_back(v, n)


huffman_tree* 
huffman_tree_init()
{
	huffman_tree* tree = malloc(sizeof(huffman_tree));
	return tree;
}

void 
huffman_cleanup(huffman_tree* tree)
{
	for (int i = 0; i < MAX_BIT_LEN - 8; i ++) {
		for (int j = 0; j < 2; j ++) {
			if (tree->slow[i][j])
				free(tree->slow[i][j]);
		}
	}
	free(tree);
}

/* given the code lengths, generate the tree 
 * maxbitlen is the maximum bits that a code in the tree can have. return value is error. */
void 
huffman_build_lookup_table(huffman_tree* tree, const uint8_t count[16], const uint8_t syms[])
{
	uint8_t coding[MAX_SYMBOLS];
	uint8_t bitlen[MAX_SYMBOLS];
	unsigned bits, n = 0, i = 0;
	int maxbitlen = 0, numofcodes = 0;
    int k = 0;

	/* initialize local vectors */
	memset(coding, 0xFF, sizeof(coding));
	memset(bitlen, 0, sizeof(bitlen));

    for (int i = MAX_BIT_LENGTH - 1; i >= 0; i--) {
        if ( count[i] != 0 ) {
            maxbitlen = i+1;
            break;
        }
    }
	tree->maxbitlen = maxbitlen;
	bits = maxbitlen;
	if(maxbitlen > 8)
		maxbitlen = 8;
	//rule 1: start coding from 0
    coding[0] = 0;
	while(count[i] == 0) {
		i ++;
	}
	// coding[numofcodes] = 0;
    for (; i < 16; i ++) {
        if (count[i] > 0) {
            if (numofcodes == 0) {
                coding[numofcodes] = 0;
            } else {
                coding[numofcodes] = (coding[numofcodes-1] + 1) << k;
            }
            k = 0;
			bitlen[numofcodes] = i + 1;
            numofcodes ++;
            for(int j = 1 ; j < count[i]; j++) {
                coding[numofcodes] = coding[numofcodes - 1] + 1;
				bitlen[numofcodes] = i + 1;
                numofcodes ++;
            }
        }
        k ++;
    }
	tree->numcodes = numofcodes;

	for(i = 0; i < numofcodes; i++) {
		// build fast lookup table
		if (bitlen[i] <= 8) {
			for (int j = 0; j < (1 << (8 - bitlen[i])); j ++) {
				tree->fast[0][((coding[i] << (8 - bitlen[i])) | j)] = syms[i];
				tree->fast[1][((coding[i] << (8 - bitlen[i])) | j)] = bitlen[i];
#if NDEBUG
				printf("i %d bitlen %d ,code %d fast %d \n", i, bitlen[i], coding[i],
					 (coding[i] << (8 - bitlen[i]))|j);
#endif
			}
		} else {
			break;
		}
	}
	//we need slow table when max bits greater than 8
	for (int j = 8; j <= bits; j ++) {
		tree->slow_cnt[j-8] = count[j];
		tree->slow[j-8][0] = malloc(count[j]);
		tree->slow[j-8][1] = malloc(count[j]);
	}
	//build slow lookup list
	for (; i < numofcodes; i ++) {
		if (bitlen[i] > bitlen[i-1]) {
			n = 0;
		}

		tree->slow[bitlen[i] - 8 - 1][0][n] = coding[i];
		tree->slow[bitlen[i] - 8 - 1][1][n] = syms[i];
		// printf("n %d, slow bitlen %d, coding %x, sym %x\n",n, bitlen[i], coding[i], syms[i]);
		n ++;
	}
}

static int 
decode_symbol(struct bits_vec * v, huffman_tree* tree) {
	unsigned pos = 0, ct;
	int c, cbits = 0, bl;
	if (eof_bits(v, 8)) {
		return -1;
	}

	c = READ_BITS(v, 8);
	ct = tree->fast[0][c];
	if (ct != 0xFF) {
		/* get decoded symbol */
		// printf("off %d decode %d, bitlen %d\n", v->offset, ct, tree->fast[1][c]);
		STEP_BACK(v, 8 - tree->fast[1][c]);
		printf("read %x index %ld off %d decode %d, bitlen %d\n", c, v->ptr-v->start, v->offset, ct, tree->fast[1][c]);
		return ct;
	}
	bl = 8;

	while (bl < tree->maxbitlen) {
		c = ( (c << 1) | READ_BIT(v));
		for (int i =0; i < tree->slow_cnt[bl-8]; i ++) {
			if ( c == tree->slow[bl-8][0][i]) {
				printf("offset %d decode %d, bitlen %d\n", v->offset, c, bl);
				return tree->slow[bl-8][1][i];
			}
		}
		bl ++;
	}
	return -1;
}

static struct bits_vec *vec;

void
huffman_decode_start(uint8_t *in, int inbytelen)
{
	vec = init_bits_vec(in, inbytelen);
}

int
huffman_decode_symbol(huffman_tree* tree)
{
	return decode_symbol(vec, tree);
}

int 
huffman_read_symbol(int n)
{
	int ret = READ_BITS(vec, n);
	// printf("read %d bits val %d\n", n, ret);
	return ret;
}

void
huffman_decode_end(void)
{
	free(vec);
}

#if 0
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
#endif
