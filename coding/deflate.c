#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "deflate.h"

struct tinf_tree {
	unsigned short counts[16]; /* Number of codes with a given length */
	unsigned short symbols[288]; /* Symbols sorted by code */
	int max_sym;
};

struct tinf_data {
	const uint8_t *source;
	const uint8_t *source_end;
	unsigned int tag;
	int bitcount;

	uint8_t *dest_start;
	uint8_t *dest;
	uint8_t *dest_end;

	struct tinf_tree ltree; /* Literal/length tree */
	struct tinf_tree dtree; /* Distance tree */
};


enum {
    BTYPE_NOCOMPRESSION = 0,
    BTYPE_COMPRESSED_WITH_FIXED_HUFFMAN = 1,
    BTYPE_COMPRESSED_WITH_DYNAMIC_HUFFMAN = 2,
};


static unsigned int read_le16(const unsigned char *p)
{
	return ((unsigned int) p[0])
	     | ((unsigned int) p[1] << 8);
}


static void tinf_refill(struct tinf_data *d, int num)
{
	assert(num >= 0 && num <= 32);

	/* Read bytes until at least num bits available */
	while (d->bitcount < num) {
		if (d->source != d->source_end) {
			d->tag |= (unsigned int) *d->source++ << d->bitcount;
		}
		d->bitcount += 8;
	}

	assert(d->bitcount <= 32);
}


static unsigned int tinf_getbits_no_refill(struct tinf_data *d, int num)
{
	unsigned int bits;

	assert(num >= 0 && num <= d->bitcount);

	/* Get bits from tag */
	bits = d->tag & ((1UL << num) - 1);

	/* Remove bits from tag */
	d->tag >>= num;
	d->bitcount -= num;

	return bits;
}

/* Get num bits from source stream */
static unsigned int tinf_getbits(struct tinf_data *d, int num)
{
	tinf_refill(d, num);
	return tinf_getbits_no_refill(d, num);
}

/* Read a num bit value from stream and add base */
static unsigned int tinf_getbits_base(struct tinf_data *d, int num, int base)
{
	return base + (num ? tinf_getbits(d, num) : 0);
}

/* Build fixed Huffman trees */
static void tinf_build_fixed_trees(struct tinf_tree *lt, struct tinf_tree *dt)
{
	int i;

	/* Build fixed literal/length tree */
	for (i = 0; i < 16; ++i) {
		lt->counts[i] = 0;
	}

	lt->counts[7] = 24;
	lt->counts[8] = 152;
	lt->counts[9] = 112;

	for (i = 0; i < 24; ++i) {
		lt->symbols[i] = 256 + i;
	}
	for (i = 0; i < 144; ++i) {
		lt->symbols[24 + i] = i;
	}
	for (i = 0; i < 8; ++i) {
		lt->symbols[24 + 144 + i] = 280 + i;
	}
	for (i = 0; i < 112; ++i) {
		lt->symbols[24 + 144 + 8 + i] = 144 + i;
	}

	lt->max_sym = 285;

	/* Build fixed distance tree */
	for (i = 0; i < 16; ++i) {
		dt->counts[i] = 0;
	}

	dt->counts[5] = 32;

	for (i = 0; i < 32; ++i) {
		dt->symbols[i] = i;
	}

	dt->max_sym = 29;
}

/* Given an array of code lengths, build a tree */
static int tinf_build_tree(struct tinf_tree *t, const unsigned char *lengths,
                           unsigned int num)
{
	unsigned short offs[16];
	unsigned int i, num_codes, available;

	assert(num <= 288);

	for (i = 0; i < 16; ++i) {
		t->counts[i] = 0;
	}

	t->max_sym = -1;

	/* Count number of codes for each non-zero length */
	for (i = 0; i < num; ++i) {
		assert(lengths[i] <= 15);

		if (lengths[i]) {
			t->max_sym = i;
			t->counts[lengths[i]]++;
		}
	}

	/* Compute offset table for distribution sort */
	for (available = 1, num_codes = 0, i = 0; i < 16; ++i) {
		unsigned int used = t->counts[i];

		/* Check length contains no more codes than available */
		if (used > available) {
			return -1;
		}
		available = 2 * (available - used);

		offs[i] = num_codes;
		num_codes += used;
	}

	/*
	 * Check all codes were used, or for the special case of only one
	 * code that it has length 1
	 */
	if ((num_codes > 1 && available > 0)
	 || (num_codes == 1 && t->counts[1] != 1)) {
		return -1;
	}

	/* Fill in symbols sorted by code */
	for (i = 0; i < num; ++i) {
		if (lengths[i]) {
			t->symbols[offs[lengths[i]]++] = i;
		}
	}

	/*
	 * For the special case of only one code (which will be 0) add a
	 * code 1 which results in a symbol that is too large
	 */
	if (num_codes == 1) {
		t->counts[1] = 2;
		t->symbols[1] = t->max_sym + 1;
	}

	return 0;
}

/* Inflate an uncompressed block of data */
static int 
tinf_inflate_nocompression_block(struct tinf_data *d)
{
	unsigned int length, invlength;

	if (d->source_end - d->source < 4) {
		return -1;
	}

	/* Get length */
	length = read_le16(d->source);

	/* Get one's complement of length */
	invlength = read_le16(d->source + 2);

    printf("no compression %d, %d\n", length, invlength);
	/* Check length */
	if (length != (~invlength & 0x0000FFFF)) {
		return -2;
	}


	d->source += 4;

	if (d->source_end - d->source < length) {
		return -3;
	}

	if (d->dest_end - d->dest < length) {
		return -4;
	}

	/* Copy block */
	while (length--) {
		*d->dest++ = *d->source++;
	}

	/* Make sure we start next block on a byte boundary */
	d->tag = 0;
	d->bitcount = 0;

	return 0;
}

/* Given a data stream and a tree, decode a symbol */
static int tinf_decode_symbol(struct tinf_data *d, const struct tinf_tree *t)
{
	int base = 0, offs = 0;
	int len;

	/*
	 * Get more bits while code index is above number of codes
	 *
	 * Rather than the actual code, we are computing the position of the
	 * code in the sorted order of codes, which is the index of the
	 * corresponding symbol.
	 *
	 * Conceptually, for each code length (level in the tree), there are
	 * counts[len] leaves on the left and internal nodes on the right.
	 * The index we have decoded so far is base + offs, and if that
	 * falls within the leaves we are done. Otherwise we adjust the range
	 * of offs and add one more bit to it.
	 */
	for (len = 1; ; ++len) {
		offs = 2 * offs + tinf_getbits(d, 1);

		assert(len <= 15);

		if (offs < t->counts[len]) {
			break;
		}

		base += t->counts[len];
		offs -= t->counts[len];
	}

	assert(base + offs >= 0 && base + offs < 288);

	return t->symbols[base + offs];
}


/* Given a stream and two trees, inflate a block of data */
static int tinf_inflate_block_data(struct tinf_data *d, struct tinf_tree *lt,
                                   struct tinf_tree *dt)
{
	/* Extra bits and base tables for length codes */
	static const unsigned char length_bits[30] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
		1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
		4, 4, 4, 4, 5, 5, 5, 5, 0, 127
	};

	static const unsigned short length_base[30] = {
		 3,  4,  5,   6,   7,   8,   9,  10,  11,  13,
		15, 17, 19,  23,  27,  31,  35,  43,  51,  59,
		67, 83, 99, 115, 131, 163, 195, 227, 258,   0
	};

	/* Extra bits and base tables for distance codes */
	static const unsigned char dist_bits[30] = {
		0, 0,  0,  0,  1,  1,  2,  2,  3,  3,
		4, 4,  5,  5,  6,  6,  7,  7,  8,  8,
		9, 9, 10, 10, 11, 11, 12, 12, 13, 13
	};

	static const unsigned short dist_base[30] = {
		   1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
		  33,   49,   65,   97,  129,  193,  257,   385,   513,   769,
		1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
	};

	for (;;) {
		int sym = tinf_decode_symbol(d, lt);

		/* Check for overflow in bit reader */
		// if (d->overflow) {
		// 	return -1;
		// }

		if (sym < 256) {
			if (d->dest == d->dest_end) {
				return -2;
			}
			*d->dest++ = sym;
		}
		else {
			int length, dist, offs;
			int i;

			/* Check for end of block */
			if (sym == 256) {
				return 0;
			}

			/* Check sym is within range and distance tree is not empty */
			if (sym > lt->max_sym || sym - 257 > 28 || dt->max_sym == -1) {
				return -1;
			}

			sym -= 257;

			/* Possibly get more bits from length code */
			length = tinf_getbits_base(d, length_bits[sym],
			                           length_base[sym]);

			dist = tinf_decode_symbol(d, dt);

			/* Check dist is within range */
			if (dist > dt->max_sym || dist > 29) {
				return -1;
			}

			/* Possibly get more bits from distance code */
			offs = tinf_getbits_base(d, dist_bits[dist],
			                         dist_base[dist]);

			if (offs > d->dest - d->dest_start) {
				return -1;
			}

			if (d->dest_end - d->dest < length) {
				return -2;
			}

			/* Copy match */
			for (i = 0; i < length; ++i) {
				d->dest[i] = d->dest[i - offs];
			}

			d->dest += length;
		}
	}
}


/* Given a data stream, decode dynamic trees from it */
static int tinf_decode_trees(struct tinf_data *d, struct tinf_tree *lt,
                             struct tinf_tree *dt)
{
	unsigned char lengths[288 + 32];

	/* Special ordering of code length codes */
	static const unsigned char clcidx[19] = {
		16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
		11,  4, 12, 3, 13, 2, 14, 1, 15
	};

	unsigned int hlit, hdist, hclen;
	unsigned int i, num, length;
	int res;

	/* Get 5 bits HLIT (257-286) */
	hlit = tinf_getbits_base(d, 5, 257);

	/* Get 5 bits HDIST (1-32) */
	hdist = tinf_getbits_base(d, 5, 1);

	/* Get 4 bits HCLEN (4-19) */
	hclen = tinf_getbits_base(d, 4, 4);

    printf("hlit %d, hdist %d, hclen %d\n", hlit, hdist, hclen);

	/*
	 * The RFC limits the range of HLIT to 286, but lists HDIST as range
	 * 1-32, even though distance codes 30 and 31 have no meaning. While
	 * we could allow the full range of HLIT and HDIST to make it possible
	 * to decode the fixed trees with this function, we consider it an
	 * error here.
	 *
	 * See also: https://github.com/madler/zlib/issues/82
	 */
	if (hlit > 286 || hdist > 30) {
		return -1;
	}

	for (i = 0; i < 19; ++i) {
		lengths[i] = 0;
	}

	/* Read code lengths for code length alphabet */
	for (i = 0; i < hclen; ++i) {
		/* Get 3 bits code length (0-7) */
		unsigned int clen = tinf_getbits(d, 3);

		lengths[clcidx[i]] = clen;
	}

	/* Build code length tree (in literal/length tree to save space) */
	res = tinf_build_tree(lt, lengths, 19);

	if (res != 0) {
		return res;
	}

	/* Check code length tree is not empty */
	if (lt->max_sym == -1) {
		return -1;
	}

	/* Decode code lengths for the dynamic trees */
	for (num = 0; num < hlit + hdist; ) {
		int sym = tinf_decode_symbol(d, lt);

		if (sym > lt->max_sym) {
			return -1;
		}

		switch (sym) {
		case 16:
			/* Copy previous code length 3-6 times (read 2 bits) */
			if (num == 0) {
				return -1;
			}
			sym = lengths[num - 1];
			length = tinf_getbits_base(d, 2, 3);
			break;
		case 17:
			/* Repeat code length 0 for 3-10 times (read 3 bits) */
			sym = 0;
			length = tinf_getbits_base(d, 3, 3);
			break;
		case 18:
			/* Repeat code length 0 for 11-138 times (read 7 bits) */
			sym = 0;
			length = tinf_getbits_base(d, 7, 11);
			break;
		default:
			/* Values 0-15 represent the actual code lengths */
			length = 1;
			break;
		}

		if (length > hlit + hdist - num) {
			return -1;
		}

		while (length--) {
			lengths[num++] = sym;
		}
	}

	/* Check EOB symbol is present */
	if (lengths[256] == 0) {
		return -1;
	}

	/* Build dynamic trees */
	res = tinf_build_tree(lt, lengths, hlit);

	if (res != 0) {
		return res;
	}

	res = tinf_build_tree(dt, lengths + hlit, hdist);

	if (res != 0) {
		return res;
	}

	return 0;
}

/* Inflate a block of data compressed with fixed Huffman trees */
static int tinf_inflate_fixed_block(struct tinf_data *d)
{
	/* Build fixed Huffman trees */
	tinf_build_fixed_trees(&d->ltree, &d->dtree);

	/* Decode block using fixed trees */
	return tinf_inflate_block_data(d, &d->ltree, &d->dtree);
}

/* Inflate a block of data compressed with dynamic Huffman trees */
static int tinf_inflate_dynamic_block(struct tinf_data *d)
{
	/* Decode trees from stream */
	int res = tinf_decode_trees(d, &d->ltree, &d->dtree);
	if (res != 0) {
		return res;
	}

	/* Decode block using decoded trees */
	return tinf_inflate_block_data(d, &d->ltree, &d->dtree);
}


void 
deflate_decode(const uint8_t* compressed, int compressed_length, uint8_t* decompressed, int* descompressed_len)
{
	struct tinf_data d;
	unsigned bfinal = 0;

    struct zlib_header h;
    uint16_t check;
    memcpy(&h, compressed, 2);
    if (h.compress_method != 8) {
        printf("not deflate, cm %d\n", h.compress_method);
    }
    memcpy(&check, &h, 2);
    if( ntohl(check) % 31) {
        printf("fcheck zlib error, fcheck %x\n", h.FCHECK);
    }
    if (h.compression_info > 7) {
        printf("too big window for lz77 not allowed %d\n", h.compression_info);
    }
    //PNG does not have a preset dict, so 
    if (h.preset_dict) {
        printf("for png preset dict should not be set\n");
    }
    // printf("conpression level %d\n", h.compression_level);

    uint32_t alder32 = *(uint32_t *)(compressed + compressed_length - 4);
    // printf("alder32 is %x\n", alder32);

	/* Initialise data */
	d.source = compressed + 2;
	d.source_end = d.source + compressed_length - 6;
	d.tag = 0;
	d.bitcount = 0;

	d.dest = decompressed;
	d.dest_start = d.dest;
	d.dest_end = d.dest + *descompressed_len;

	 while (!bfinal) 
     {
		unsigned int btype;
		int res;

		/* Read final block flag */
		bfinal = tinf_getbits(&d, 1);
		/* Read block type (2 bits) */
		btype = tinf_getbits(&d, 2);

        printf("btype %d\n", btype);
		/* Decompress block */
		switch (btype) {
		case BTYPE_NOCOMPRESSION:
            /* skip any remaining bits in byte */
			res = tinf_inflate_nocompression_block(&d);
			break;
		case BTYPE_COMPRESSED_WITH_FIXED_HUFFMAN:
			res = tinf_inflate_fixed_block(&d);
			break;
		case BTYPE_COMPRESSED_WITH_DYNAMIC_HUFFMAN:
			/* Decompress block with dynamic Huffman trees */
			res = tinf_inflate_dynamic_block(&d);
			break;
		default:
			res = -1;
			break;
		}

        if (res != 0) {
            printf("deflate error %d\n", res);
        }
	}

	*descompressed_len = d.dest - d.dest_start;

}