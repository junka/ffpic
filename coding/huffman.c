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
	if (v->ptr + n/8 - v->start > v->len) {
		return true;
	} else if (v->ptr + n/8 - v->start == v->len) {
		if (v->offset + n % 8 > 8) {
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
	if (v->offset > 0)
	{
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
		ret >>= ((read - n) % 8);
	else 
		ret >>= ((read - n) % 8);
	ret &= ((1 << n) -1);
	if((n + v->offset) % 8) {
		v->ptr --;
	}
	v->offset = ((v->offset + (n%8))%8);
	return ret;
}

int 
read_bit(struct bits_vec *v)
{
	if (v->ptr - v->start > v->len)
		return -1;
	uint8_t ret = (*(v->ptr) >> (7 - v->offset)) & 0x1;
	v->offset ++;
	if (v->offset == 8) {
		v->ptr ++;
		v->offset = 0;
	}
	return ret;
}

#if 0
int 
read_bits(struct bits_vec *v, int n)
{
	int ret = 0;
	for (int i = 0; i < n; i++) {
		int a = read_bit(v);
		if (a == -1) {
			return -1;
		}
		ret = (ret<<1 | a);
	}
	return ret;
}
#endif

void
skip_bits(struct bits_vec *v, int n)
{
	uint8_t skip  = 0;
	while (skip < n) {
		v->ptr ++;
		skip += 8;
	}
	v->offset = 8 - (skip - n - v->offset);
}

void
reset_bits_boundary(struct bits_vec *v)
{
	if (v->offset) {
		v->ptr ++;
		v->offset = 0;
	}
}


#define READ_BIT(v) read_bit(v)
#define READ_BITS(v, n) read_bits(v, n)
#define SKIP_BITS(v, n) skip_bits(v, n)
#define STEP_BACK(v, n) step_back(v, n)


huffman_tree* 
huffman_tree_init()
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
huffman_dump_table(huffman_tree* tree)
{
	printf("%p: %s %d\n", tree, tree->dc_ac == 0 ? "DC": "AC", tree->tid);
	printf("fast lookup table:\n");
	for (int i = 0; i < FAST_HF_SIZE; i ++) {
		printf("\t%d\t%x\t%d\n", i, tree->fast[0][i], tree->fast[1][i]);
	}
	printf("slow lookup table:\n");
	for (int i = FAST_HF_BITS + 1; i <= MAX_BIT_LEN; i ++) {
		for (int j = 0; j < tree->slow_cnt[i-FAST_HF_BITS-1]; j ++) {
			printf("\t%d\t%x\t%d\n", tree->slow_codec[i-FAST_HF_BITS-1][j],
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
	unsigned bits, n = 0, i = 0;
	int maxbitlen = 0, n_codes = 0;
    int k = 0;

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
	int pos = 0, ct = 0xFF;
	int c = -1, cbits = 0, bl = 0;
	if (eof_bits(v, FAST_HF_BITS)) {
		printf("end of stream %ld, %ld\n", v->ptr - v->start, v->len);
		return -1;
	}
	c = READ_BITS(v, FAST_HF_BITS);
	if (c == -1) {
		printf("invalid bits read\n");
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
		//fast table miss, try slow table then
		bl = FAST_HF_BITS + 1;// which mean bitlen
		while (bl <= tree->maxbitlen) {
			c = ((c << 1) | READ_BIT(v));
			for (int i =0; i < tree->slow_cnt[bl - FAST_HF_BITS -1]; i ++) {
				if ( c == tree->slow_codec[bl - FAST_HF_BITS -1][i]) {
					// printf("offset %d decode %d, bitlen %d\n", v->offset, c, bl);
					return tree->slow_symbol[bl - FAST_HF_BITS -1][i];
				}
			}
			bl ++;
		}
	}
	printf("why here, bl %d , maxbitlen %d, c %x\n", bl, tree->maxbitlen, c);
	return -1;
}

static struct bits_vec *vec = NULL;

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
	if (eof_bits(vec, n)) {
		return -1;
	}
	int ret = READ_BITS(vec, n);
	return ret;
}

void
huffman_reset_stream(void)
{
	reset_bits_boundary(vec);
}

void
huffman_decode_end(void)
{
    printf("len %ld, now consume %ld\n", vec->len, vec->ptr-vec->start);
	free(vec);
}
