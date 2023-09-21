#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "bitstream.h"
#include "deflate.h"
#include "vlog.h"

VLOG_REGISTER(deflate, INFO)

struct deflate_tree {
    uint16_t counts[16]; /* Number of codes with a given length */
    uint16_t symbols[288]; /* Symbols sorted by code */
    int max_sym;
};

struct deflate_decoder {
    struct bits_vec *v;

    uint8_t *dest_start;
    uint8_t *dest;
    uint8_t *dest_end;

    struct deflate_tree ltree; /* Literal/length tree */
    struct deflate_tree dtree; /* Distance tree */
};


enum {
    BTYPE_NOCOMPRESSION = 0,
    BTYPE_COMPRESSED_WITH_FIXED_HUFFMAN = 1,
    BTYPE_COMPRESSED_WITH_DYNAMIC_HUFFMAN = 2,
};


/* Build fixed Huffman trees */
static void
build_fixed_trees(struct deflate_tree *lt, struct deflate_tree *dt)
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
static int
deflate_build_tree(struct deflate_tree *t, const uint8_t *lengths,
                           uint32_t num)
{
    uint16_t offs[16];
    uint32_t num_codes, available, i;

    assert(num <= 288);

    for (int i = 0; i < 16; ++i) {
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
        uint32_t used = t->counts[i];

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
deflate_nocompression_block(struct deflate_decoder *d)
{
    int length, invlength;

    /* Get length */
    length = READ_BITS(d->v, 16);

    /* Get one's complement of length */
    invlength = READ_BITS(d->v, 16);

    VDBG(deflate, "no compression %d, %d", length, invlength);
    /* Check length */
    if (length != (~invlength & 0x0000FFFF)) {
        return -2;
    }

    if (EOF_BITS(d->v, 0) < length) {
        return -3;
    }

    if (d->dest_end - d->dest < length) {
        return -4;
    }

    /* Copy block */
    while (length--) {
        *d->dest++ = READ_BITS(d->v, 8);
    }

    /* Make sure we start next block on a byte boundary */
    RESET_BORDER(d->v);

    return 0;
}

/* Given a data stream and a tree, decode a symbol */
static int
deflate_decode_symbol(struct bits_vec *v, const struct deflate_tree *t)
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
        offs = 2 * offs + READ_BIT(v);

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
static int
deflate_block_data(struct deflate_decoder *d, struct deflate_tree *lt,
                    struct deflate_tree *dt)
{
    /* Extra bits and base tables for length codes */
    static const uint8_t length_bits[30] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 0, 127
    };

    static const uint16_t length_base[30] = {
         3,  4,  5,   6,   7,   8,   9,  10,  11,  13,
        15, 17, 19,  23,  27,  31,  35,  43,  51,  59,
        67, 83, 99, 115, 131, 163, 195, 227, 258,   0
    };

    /* Extra bits and base tables for distance codes */
    static const uint8_t dist_bits[30] = {
        0, 0,  0,  0,  1,  1,  2,  2,  3,  3,
        4, 4,  5,  5,  6,  6,  7,  7,  8,  8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };

    static const uint16_t dist_base[30] = {
           1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
          33,   49,   65,   97,  129,  193,  257,   385,   513,   769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };

    for (;;) {
        int sym = deflate_decode_symbol(d->v, lt);

        if (sym < 256) {
            if (d->dest == d->dest_end) {
                return -2;
            }
            *d->dest++ = sym;
        }
        else {
            int length, dist, offs;

            /* Check for end of block */
            if (sym == 256) {
                return 0;
            }

            /* Check sym is within range and distance tree is not empty */
            if (sym > lt->max_sym || sym - 257 > 28 || dt->max_sym == -1) {
                VERR(deflate, "error for sym");
                return -1;
            }

            sym -= 257;

            /* Possibly get more bits from length code */
            length = READ_BITS_BASE(d->v, length_bits[sym], length_base[sym]);

            dist = deflate_decode_symbol(d->v, dt);

            /* Check dist is within range */
            if (dist > dt->max_sym || dist > 29) {
                VERR(deflate, "error for dist");
                return -1;
            }

            /* Possibly get more bits from distance code */
            offs = READ_BITS_BASE(d->v, dist_bits[dist],
                                     dist_base[dist]);

            if (offs > d->dest - d->dest_start) {
                return -1;
            }

            if (d->dest_end - d->dest < length) {
                return -2;
            }

            /* Copy match */
            for (int i = 0; i < length; ++i) {
                d->dest[i] = d->dest[i - offs];
            }
            d->dest += length;
        }
    }
}

/* Given a data stream, decode dynamic trees from it */
static int
deflate_decode_trees(struct deflate_decoder *d, struct deflate_tree *lt,
                     struct deflate_tree *dt)
{
    uint8_t lengths[288 + 32];

    /* Special ordering of code length codes */
    static const uint8_t clcidx[19] = {
        16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
        11,  4, 12, 3, 13, 2, 14, 1, 15
    };

    uint32_t hlit, hdist, hclen;
    uint32_t num, length;
    int res;

    /* Get 5 bits HLIT (257-286) */
    hlit = READ_BITS_BASE(d->v, 5, 257);

    /* Get 5 bits HDIST (1-32) */
    hdist = READ_BITS_BASE(d->v, 5, 1);

    /* Get 4 bits HCLEN (4-19) */
    hclen = READ_BITS_BASE(d->v, 4, 4);

    VDBG(deflate, "hlit %d, hdist %d, hclen %d", hlit, hdist, hclen);

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

    for (int i = 0; i < 19; ++i) {
        lengths[i] = 0;
    }

    /* Read code lengths for code length alphabet */
    for (uint32_t i = 0; i < hclen; ++i) {
        /* Get 3 bits code length (0-7) */
        lengths[clcidx[i]] = READ_BITS(d->v, 3);
    }

    /* Build code length tree (in literal/length tree to save space) */
    res = deflate_build_tree(lt, lengths, 19);

    if (res != 0) {
        return res;
    }

    /* Check code length tree is not empty */
    if (lt->max_sym == -1) {
        return -1;
    }

    /* Decode code lengths for the dynamic trees */
    for (num = 0; num < hlit + hdist; ) {
        int sym = deflate_decode_symbol(d->v, lt);

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
            length = READ_BITS_BASE(d->v, 2, 3);
            break;
        case 17:
            /* Repeat code length 0 for 3-10 times (read 3 bits) */
            sym = 0;
            length = READ_BITS_BASE(d->v, 3, 3);
            break;
        case 18:
            /* Repeat code length 0 for 11-138 times (read 7 bits) */
            sym = 0;
            length = READ_BITS_BASE(d->v, 7, 11);
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
    res = deflate_build_tree(lt, lengths, hlit);

    if (res != 0) {
        return res;
    }

    res = deflate_build_tree(dt, lengths + hlit, hdist);

    if (res != 0) {
        return res;
    }

    return 0;
}

/* Inflate a block of data compressed with fixed Huffman trees */
static int
deflate_fixed_block(struct deflate_decoder *d)
{
    /* Build fixed Huffman trees */
    build_fixed_trees(&d->ltree, &d->dtree);

    /* Decode block using fixed trees */
    return deflate_block_data(d, &d->ltree, &d->dtree);
}

/* Inflate a block of data compressed with dynamic Huffman trees */
static int
deflate_dynamic_block(struct deflate_decoder *d)
{
    /* Decode trees from stream */
    int res = deflate_decode_trees(d, &d->ltree, &d->dtree);
    if (res != 0) {
        VERR(deflate, "decode trees error");
        return res;
    }

    /* Decode block using decoded trees */
    return deflate_block_data(d, &d->ltree, &d->dtree);
}


int 
deflate_decode(uint8_t* compressed, int compressed_length, uint8_t* decompressed, int* decomp_len)
{
    unsigned bfinal = 0;

    struct zlib_header h;
    uint16_t check;
    // uint32_t alder32 = *(uint32_t *)(compressed + compressed_length - 4);

    memcpy(&h, compressed, 2);
    if (h.compress_method != 8) {
        VERR(deflate, "not deflate, cm %d", h.compress_method);
    }
    memcpy(&check, &h, 2);
    if( ntohl(check) % 31) {
        VERR(deflate, "fcheck zlib error, fcheck %x", h.FCHECK);
    }
    if (h.compression_info > 7) {
        VERR(deflate, "too big window for lz77 not allowed %d", h.compression_info);
    }
    //PNG does not have a preset dict, so 
    if (h.preset_dict) {
        VERR(deflate, "for png preset dict should not be set");
    }
    compressed += 2;

    // /* Initialise data */
    struct deflate_decoder d = {
        .dest = decompressed,
        .dest_start = decompressed,
        .dest_end = decompressed + *decomp_len,
    };

    /* first two bytes zlib header and last four bytes alder32 */
    d.v = bits_vec_alloc(compressed, compressed_length - 6, BITS_LSB);

     while (!bfinal)
     {
        uint32_t btype;
        int res;

        /* Read final block flag */
        bfinal = READ_BIT(d.v);
        /* Read block type (2 bits) */
        btype = READ_BITS(d.v, 2);

        VDBG(deflate, "btype %d", btype);
        /* Decompress block */
        switch (btype) {
        case BTYPE_NOCOMPRESSION:
            /* skip any remaining bits in byte */
            RESET_BORDER(d.v);
            res = deflate_nocompression_block(&d);
            break;
        case BTYPE_COMPRESSED_WITH_FIXED_HUFFMAN:
            res = deflate_fixed_block(&d);
            break;
        case BTYPE_COMPRESSED_WITH_DYNAMIC_HUFFMAN:
            /* Decompress block with dynamic Huffman trees */
            res = deflate_dynamic_block(&d);
            break;
        default:
            res = -1;
            break;
        }

        if (res != 0) {
            VERR(deflate, "deflate error %d", res);
        }
    }
    *decomp_len = d.dest - d.dest_start;

    free(d.v);

    return 0;
}
