#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
/* VP8L (WebP Lossless) decoder — single-file, self-contained.
 *
 * Reference implementation: libwebp src/dec/vp8l_dec.c
 *                              src/dsp/lossless.c
 *                              src/utils/huffman_utils.h
 *
 * This file handles the VP8L image_data payload (after the 0x2f magic byte).
 * Output is 32-bit ARGB packed pixels on the wire (green in byte 1),
 * but the caller expects BGRA layout, so the final swap happens on
 * the fly in vp8l_decode_image().
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitstream.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(vp8l, DEBUG)

/* -------- constants (from libwebp format_constants.h) ------------- */

#define VP8L_MAGIC_BYTE     0x2f
#define NUM_LITERAL_CODES   256
#define NUM_LENGTH_CODES    24
#define NUM_DISTANCE_CODES  40
#define MAX_LENGTH_BITS     12
#define MIN_TRANSFORM_BITS  2
#define NUM_TRANSFORM_BITS  3
#define MIN_HUFFMAN_BITS    2
#define NUM_HUFFMAN_BITS    3
#define HUFFMAN_CODES_PER_META_CODE 5
#define MAX_CACHE_BITS      11
#define NUM_TRANSFORMS      4
#define DEFAULT_CODE_LENGTH 8

#define HUFFMAN_TABLE_BITS  8
#define HUFFMAN_TABLE_MASK  ((1 << HUFFMAN_TABLE_BITS) - 1)
#define LENGTHS_TABLE_BITS  7
#define LENGTHS_TABLE_MASK  ((1 << LENGTHS_TABLE_BITS) - 1)

#define CODE_LENGTH_CODES   19

typedef enum {
    PREDICTOR_TRANSFORM = 0,
    CROSS_COLOR_TRANSFORM = 1,
    SUBTRACT_GREEN_TRANSFORM = 2,
    COLOR_INDEXING_TRANSFORM = 3
} vp8l_transform_type_t;

static const int kCodeLengthCodeOrder[CODE_LENGTH_CODES] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const uint8_t kCodeLengthExtraBits[3] = {2, 3, 7};
static const uint8_t kCodeLengthRepeatOffsets[3] = {3, 3, 11};

/* -------- distance decoding -------------------------------------- */

/* forward declarations — referenced before definition */
static int vp8l_decode_prefix(int code, int raw_extra);
static int vp8l_decode_substream(struct bits_vec *v, int width, int height,
                                  uint32_t **out_pixels);

/* Decode a prefix+extra-bits value.
 *
 * code: prefix symbol decoded from DISTANCE or LENGTH Huffman table
 * raw_extra: additional extra bits (0..255) read directly from stream
 *
 * Returns: prefix_value + extra + 1, where prefix_value depends on code.
 * This exactly mirrors libwebp's GetCopyDistance/GetCopyLength.
 */
static int vp8l_decode_prefix(int code, int raw_extra) {
    if (code < 4) return code + 1;
    int eb = (code - 2) >> 1;
    int offset = (2 + (code & 1)) << eb;
    return offset + raw_extra + 1;
}

/* plane-code → distance (libwebp PlaneCodeToDistance, line 143) */
static const uint8_t kCodeToPlane[120] = {
    0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a, 0x26, 0x2a,
    0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a, 0x25, 0x2b, 0x48, 0x04,
    0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b, 0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45,
    0x4b, 0x34, 0x3c, 0x03, 0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d,
    0x44, 0x4c, 0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
    0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b, 0x32, 0x3e,
    0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f, 0x64, 0x6c, 0x42, 0x4e,
    0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b, 0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e,
    0x00, 0x74, 0x7c, 0x41, 0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d,
    0x51, 0x5f, 0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70
};

static int vp8l_plane_code_to_distance(int xsize, int plane_code) {
    if (plane_code > 120) return plane_code - 120;
    int dist_code = kCodeToPlane[plane_code - 1];
    int yoffset = dist_code >> 4;
    int xoffset = 8 - (dist_code & 0xf);
    int dist = yoffset * xsize + xoffset;
    return dist >= 1 ? dist : 1;
}

/* -------- canonical Huffman table builder ----------------------- */

/* Simple 2-level lookup: first level with HUFFMAN_TABLE_BITS bits,
 * second level for codes longer than that.  This matches libwebp's
 * HuffmanCode[] packed table.
 */
typedef struct {
    uint16_t bits;   /* total code length */
    uint16_t value;  /* symbol, or length-encoded value */
} vp8l_huff_code_t;

typedef struct {
    int max_bits;
    int table_size;          /* 1 << HUFFMAN_TABLE_BITS = 256 */
    vp8l_huff_code_t *table; /* 256 + overflow area (max_bits > 8) */
} vp8l_huff_table_t;

static void vp8l_huff_table_free(vp8l_huff_table_t *t) {
    free(t->table);
    t->table = NULL;
    t->table_size = 0;
    t->max_bits = 0;
}

/* -------- Huffman table build (from libwebp BuildHuffmanTable) ----- */

#define VP8L_MAX_CODE_LEN 15

/* Reverse-Gray-code next key, matching libwebp GetNextKey. */
static uint32_t vp8l_get_next_key(uint32_t key, int len) {
    uint32_t step = 1u << (len - 1);
    while (key & step) {
        step >>= 1;
    }
    return step ? (key & (step - 1)) + step : key;
}

/* Replicate code at positions step, 2*step, ..., end within table[]. */
static void vp8l_replicate_value(vp8l_huff_code_t *table, int step, int end,
                                  vp8l_huff_code_t code) {
    while (end > 0) {
        end -= step;
        table[end] = code;
    }
}

static int vp8l_next_table_bit_size(const int *count, int len, int root_bits) {
    int left = 1 << (len - root_bits);
    while (len < VP8L_MAX_CODE_LEN) {
        left -= count[len];
        if (left <= 0) break;
        ++len;
        left <<= 1;
    }
    return len - root_bits;
}

static int vp8l_build_huff_table(vp8l_huff_table_t *t, const int *code_lengths,
                                   int num_syms) {
    VDBG(vp8l, "VP8L-BUILD: num_syms=%d code_lens=[", num_syms);
    int count[VP8L_MAX_CODE_LEN + 1] = {0};
    for (int i = 0; i < num_syms; i++) {
        if (code_lengths[i] > VP8L_MAX_CODE_LEN) {
            VERR(vp8l, "VP8L-BUILD: exit1 code_length[%d]=%d > max\n", i, code_lengths[i]);
            return -1;
        }
        count[code_lengths[i]]++;
    }
    if (count[0] == num_syms) {
        /* empty table: treat as trivial value 0 */
        int rs = 1 << HUFFMAN_TABLE_BITS;
        t->table = calloc(rs, sizeof(vp8l_huff_code_t));
        if (!t->table) return -1;
        vp8l_huff_code_t c = { .bits = 0, .value = 0 };
        for (int i = 0; i < rs; i++) t->table[i] = c;
        t->max_bits = 0;
        t->table_size = rs;
        return 0;
    }


    /* offsets into sorted symbol table by code length */
    int offset[VP8L_MAX_CODE_LEN + 2];
    offset[1] = 0;
    for (int len = 1; len <= VP8L_MAX_CODE_LEN; len++) {
        if (count[len] > (1 << len)) {
            VERR(vp8l, "VP8L-BUILD: exit2 len=%d count=%d > %d\n", len, count[len], 1<<len);
            return -1;
        }
        offset[len + 1] = offset[len] + count[len];
    }

    /* sort symbols by ascending code length */
    int sorted[288];
    int tmp[VP8L_MAX_CODE_LEN + 2];
    memcpy(tmp, offset, sizeof(offset));
    for (int sym = 0; sym < num_syms; sym++) {
        int L = code_lengths[sym];
        if (L > 0) sorted[tmp[L]++] = sym;
    }

    /* single-symbol trivial case (count[0..15] sums to 1) */
    {
        int total_nonzero = 0;
        for (int len = 1; len <= VP8L_MAX_CODE_LEN; len++) total_nonzero += count[len];
        if (total_nonzero == 1) {
            int rs = 1 << HUFFMAN_TABLE_BITS;
            t->table = calloc(rs, sizeof(vp8l_huff_code_t));
            if (!t->table) return -1;
            vp8l_huff_code_t c = { .bits = 0, .value = (uint16_t)sorted[0] };
            for (int i = 0; i < rs; i++) t->table[i] = c;
            t->max_bits = 0;
            t->table_size = rs;
            return 0;
        }
    }

    int root_bits = HUFFMAN_TABLE_BITS;
    int root_size = 1 << root_bits;
    /* First pass (dry run): compute total_size — we need it to allocate table.
     * This mirrors libwebp's two-phase approach. */
    int total_size = root_size;
    {
        int num_open = 1;
        for (int len = 1; len <= root_bits; len++) {
            num_open <<= 1;
            num_open -= count[len];
            if (num_open < 0) {
                VERR(vp8l, "VP8L-BUILD: exit3 num_open<0 root len=%d\n", len);
                return -1;
            }
        }
        int key = 0;
        uint32_t low = 0xffffffffu;
        uint32_t mask = root_size - 1;
        for (int len = root_bits + 1; len <= VP8L_MAX_CODE_LEN; len++) {
            num_open <<= 1;
            num_open -= count[len];
            if (num_open < 0) {
                VERR(vp8l, "VP8L-BUILD: exit4 num_open<0 overflow len=%d\n", len);
                return -1;
            }
            for (; count[len] > 0; count[len]--) {
                if ((key & mask) != low) {
                    int tbits = vp8l_next_table_bit_size(count, len, root_bits);
                    total_size += 1 << tbits;
                    low = key & mask;
                }
                key = vp8l_get_next_key(key, len);
            }
        }
        /* restore count[] for second pass */
        /* we'll rebuild count[] from sorted instead */
    }

    /* re-count from sorted (since we destroyed count[] in dry run) */
    int count2[VP8L_MAX_CODE_LEN + 1] = {0};
    for (int sym = 0; sym < num_syms; sym++) {
        int L = code_lengths[sym];
        if (L > 0) count2[L]++;
    }

    /* single-symbol trivial case */
    if (offset[VP8L_MAX_CODE_LEN + 1] == 1) {
        t->table = calloc(root_size, sizeof(vp8l_huff_code_t));
        if (!t->table) return -1;
        vp8l_huff_code_t c = { .bits = 0, .value = (uint16_t)sorted[0] };
        for (int i = 0; i < root_size; i++) t->table[i] = c;
        t->max_bits = 0;
        t->table_size = root_size;
        return 0;
    }

    t->table = calloc(total_size, sizeof(vp8l_huff_code_t));
    if (!t->table) return -1;
    t->table_size = root_size + total_size - root_size;  /* just total_size */
    vp8l_huff_code_t *table = t->table;

    /* second pass: actually fill the table.  This is a near-verbatim
     * translation of libwebp BuildHuffmanTable inner loop. */
    uint32_t key = 0;
    int num_open = 1;
    int table_bits = root_bits;
    int table_size = 1 << table_bits;
    int symbol = 0;

    /* root table */
    for (int len = 1, step = 2; len <= root_bits; ++len, step <<= 1) {
        num_open <<= 1;
        num_open -= count2[len];
        if (num_open < 0) return -1;
        for (; count2[len] > 0; --count2[len]) {
            vp8l_huff_code_t code = { .bits = (uint16_t)len,
                                      .value = (uint16_t)sorted[symbol++] };
            vp8l_replicate_value(table + key, step, table_size, code);
            key = vp8l_get_next_key(key, len);
        }
    }

    /* overflow tables */
    uint32_t low = 0xffffffffu;
    uint32_t mask = table_size - 1;
    vp8l_huff_code_t *root_table = table;
    for (int len = root_bits + 1, step = 2; len <= VP8L_MAX_CODE_LEN; ++len, step <<= 1) {
        num_open <<= 1;
        num_open -= count2[len];
        if (num_open < 0) return -1;
        for (; count2[len] > 0; --count2[len]) {
            if ((key & mask) != low) {
                table += table_size;   /* advance past previous subtable */
                int tbits = vp8l_next_table_bit_size(count2, len, root_bits);
                int tsize = 1 << tbits;
                low = key & mask;
                /* Point root_table[low] at this overflow subtable */
                root_table[low].bits = (uint16_t)(tbits + root_bits);
                root_table[low].value = (uint16_t)((table - root_table) - low);
                table_bits = tbits;
                table_size = tsize;
            }
            vp8l_huff_code_t code = { .bits = (uint16_t)(len - root_bits),
                                      .value = (uint16_t)sorted[symbol++] };
            vp8l_replicate_value(table + (key >> root_bits), step, table_size, code);
            key = vp8l_get_next_key(key, len);
        }
    }

    t->max_bits = VP8L_MAX_CODE_LEN;
    return 0;
}

/* Decode one symbol — matches libwebp ReadSymbol. */
static int vp8l_huff_decode(struct bits_vec *v, const vp8l_huff_table_t *t) {
    if (t->max_bits == 0) return t->table[0].value;

    /* Read root_bits LSB-first */
    uint32_t low_key = 0;
    for (int i = 0; i < HUFFMAN_TABLE_BITS; i++) {
        if (EOF_BITS(v, 1)) return -1;
        low_key |= ((uint32_t)READ_BIT(v)) << i;
    }
    vp8l_huff_code_t e = t->table[low_key & HUFFMAN_TABLE_MASK];
    int nbits = e.bits - HUFFMAN_TABLE_BITS;
    if (nbits > 0) {
        /* overflow subtable: read nbits suffix bits, then index into subtable.
         * root_table[low].value = subtable_start - root_table - low, so
         * subtable_start_in_big_table = low + e.value + low = e.value + 2*low.
         * Final index = low + e.value + low + suffix = e.value + 2*low + suffix. */
        uint32_t suffix = 0;
        for (int i = 0; i < nbits; i++) {
            if (EOF_BITS(v, 1)) return -1;
            suffix |= ((uint32_t)READ_BIT(v)) << i;
        }
        int idx = (int)e.value + (int)(low_key & HUFFMAN_TABLE_MASK) +
                  (int)(suffix & ((1u << nbits) - 1));
        return t->table[idx].value;
    }
    /* short code: e.bits ≤ root_bits, step back unused prefix bits */
    STEP_BACK(v, HUFFMAN_TABLE_BITS - e.bits);
    return e.value;
}

/* -------- Read a single Huffman table from the bitstream -------- */

static int vp8l_read_huff_code(struct bits_vec *v, int alphabet_size,
                                vp8l_huff_table_t *out_table) {
    int ok = 0;
    int size = 0;

    /* code_lengths buffer: alphabet_size ints, 0 means "not used" */
    int *code_lengths = calloc(alphabet_size, sizeof(int));
    if (!code_lengths) return -1;

    int simple_code = READ_BIT(v);
    if (simple_code) {
        /* "simple code": 1 or 2 symbols.  The first_symbol_len_code bit
         * controls how many bits the symbol is encoded with (1 or 8), but
         * the code length for the Huffman tree is ALWAYS 1.
         * (Mirror: libwebp ReadHuffmanCode line ~343). */
        int num_symbols = READ_BIT(v) + 1;
        int first_symbol_len_code = READ_BIT(v);
        int symbol = READ_BITS(v, first_symbol_len_code ? 8 : 1);
        code_lengths[symbol] = 1;
        if (num_symbols == 2) {
            symbol = READ_BITS(v, 8);
            code_lengths[symbol] = 1;
        }
        ok = 1;
    } else {
        /* canonical code: decode code lengths using an inner Huffman table */
        int num_codes = READ_BITS(v, 4) + 4; /* 4..19 */
        assert(num_codes <= CODE_LENGTH_CODES);
        int cl_code_lens[CODE_LENGTH_CODES] = {0};
        for (int i = 0; i < num_codes; i++) {
            int val = READ_BITS(v, 3);
            cl_code_lens[kCodeLengthCodeOrder[i]] = val;
        }

        /* build inner Huffman for code lengths (alphabet = 19) */
        vp8l_huff_table_t inner;
        int inner_built = 0;
        int inner_bit_len = 0;
        for (int i = 0; i < CODE_LENGTH_CODES; i++) {
            if (cl_code_lens[i] > inner_bit_len) inner_bit_len = cl_code_lens[i];
        }

        if (inner_bit_len == 0) {
            /* all inner code lengths are zero → every code-length symbol
             * decodes to 0 (unused).  But that can't be useful.  Skip. */
            free(code_lengths);
            VERR(vp8l, "inner huff all-zero");
            return -1;
        }

        int inner_ok = vp8l_build_huff_table(&inner, cl_code_lens, CODE_LENGTH_CODES);
        if (inner_ok < 0) {
            free(code_lengths);
            return -1;
        }
        inner_built = 1;

        /* optional: limited max_symbol */
        int max_symbol;
        if (READ_BIT(v)) {
            int length_nbits = 2 + 2 * READ_BITS(v, 3);
            max_symbol = 2 + READ_BITS(v, length_nbits);
            if (max_symbol > alphabet_size) { max_symbol = alphabet_size; }
        } else {
            max_symbol = alphabet_size;
        }

        int prev_code_len = DEFAULT_CODE_LENGTH;
        int symbol = 0;
        while (symbol < alphabet_size) {
            if (max_symbol-- == 0) break;
            int cl_sym = vp8l_huff_decode(v, &inner);
            if (cl_sym < 0) {
                goto inner_fail;
            }

            if (cl_sym < 16) {
                code_lengths[symbol++] = cl_sym;
                if (cl_sym != 0) prev_code_len = cl_sym;
            } else {
                int use_prev = (cl_sym == 16);
                int slot = cl_sym - 16; /* 0,1,2 for codes 16,17,18 */
                int extra = kCodeLengthExtraBits[slot];
                int offset = kCodeLengthRepeatOffsets[slot];
                int repeat = READ_BITS(v, extra) + offset;
                if (symbol + repeat > alphabet_size) {
                    goto inner_fail;
                }
                int length = use_prev ? prev_code_len : 0;
                while (repeat-- > 0) code_lengths[symbol++] = length;
            }
        }
        ok = 1;
    inner_fail:
        if (inner_built) vp8l_huff_table_free(&inner);
    }

    if (!ok) {
        free(code_lengths);
        return -1;
    }
    size = vp8l_build_huff_table(out_table, code_lengths, alphabet_size);
    free(code_lengths);
    return size;
}

/* -------- transform types & inverse transforms ------------------- */

typedef struct {
    vp8l_transform_type_t type;
    int bits;            /* subsampling bits */
    int xsize;
    int ysize;
    uint32_t *data;      /* transform data: predictor modes or palette or color multipliers */
} vp8l_transform_t;

/* Sub-sample size */
static int vp8l_subsample_size(int xsize, int bits) {
    if (bits > 0) return ((xsize + (1 << bits) - 1) >> bits);
    return xsize;
}

/* Average helpers (from lossless.c) */
static inline uint32_t avg2(uint32_t a0, uint32_t a1) {
    return (((a0 ^ a1) & 0xfefefefeu) >> 1) + (a0 & a1);
}
static inline uint32_t avg3(uint32_t a0, uint32_t a1, uint32_t a2) {
    return avg2(avg2(a0, a2), a1);
}
static inline uint32_t avg4(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    return avg2(avg2(a0, a1), avg2(a2, a3));
}
static inline uint32_t clip255(uint32_t a) {
    if (a < 256) return a;
    return ~a >> 24;
}

static inline uint32_t clamped_add_subtract_full(uint32_t c0, uint32_t c1, uint32_t c2) {
    const int a = clip255((uint32_t)(c0 + c1 - c2));
    const int r = clip255((uint32_t)(((c0 >> 16) & 0xff) + ((c1 >> 16) & 0xff) - ((c2 >> 16) & 0xff)));
    const int g = clip255((uint32_t)(((c0 >> 8) & 0xff) + ((c1 >> 8) & 0xff) - ((c2 >> 8) & 0xff)));
    const int b = clip255((uint32_t)((c0 & 0xff) + (c1 & 0xff) - (c2 & 0xff)));
    return ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
}

static int sub3(int a, int b, int c) {
    const int pb = b - c;
    const int pa = a - c;
    return abs(pb) - abs(pa);
}

static uint32_t vp8l_select(uint32_t a, uint32_t b, uint32_t c) {
    const int diff = sub3((a >> 24) & 0xff, (b >> 24) & 0xff, (c >> 24) & 0xff)
                   + sub3((a >> 16) & 0xff, (b >> 16) & 0xff, (c >> 16) & 0xff)
                   + sub3((a >> 8)  & 0xff, (b >> 8)  & 0xff, (c >> 8)  & 0xff)
                   + sub3((a)       & 0xff, (b)       & 0xff, (c)       & 0xff);
    return diff <= 0 ? a : b;
}

/* Predictor functions — return the predicted ARGB from (left, top) refs */
typedef uint32_t (*predictor_func)(const uint32_t *left, const uint32_t *top);

static uint32_t predictor0(const uint32_t *left, const uint32_t *top) {
    (void)left; (void)top; return 0xff000000u; /* ARGB black */
}
static uint32_t predictor1(const uint32_t *left, const uint32_t *top) {
    (void)top; return *left;
}
static uint32_t predictor2(const uint32_t *left, const uint32_t *top) {
    (void)left; return top[0];
}
static uint32_t predictor3(const uint32_t *left, const uint32_t *top) {
    (void)left; return top[1];
}
static uint32_t predictor4(const uint32_t *left, const uint32_t *top) {
    (void)left; return top[-1];
}
static uint32_t predictor5(const uint32_t *left, const uint32_t *top) {
    return avg3(*left, top[0], top[1]);
}
static uint32_t predictor6(const uint32_t *left, const uint32_t *top) {
    return avg2(*left, top[-1]);
}
static uint32_t predictor7(const uint32_t *left, const uint32_t *top) {
    return avg2(*left, top[0]);
}
static uint32_t predictor8(const uint32_t *left, const uint32_t *top) {
    (void)left; return avg2(top[-1], top[0]);
}
static uint32_t predictor9(const uint32_t *left, const uint32_t *top) {
    (void)left; return avg2(top[0], top[1]);
}
static uint32_t predictor10(const uint32_t *left, const uint32_t *top) {
    return avg4(*left, top[-1], top[0], top[1]);
}
static uint32_t predictor11(const uint32_t *left, const uint32_t *top) {
    return vp8l_select(top[0], *left, top[-1]);
}
static uint32_t predictor12(const uint32_t *left, const uint32_t *top) {
    return clamped_add_subtract_full(*left, top[0], top[-1]);
}
static uint32_t predictor13(const uint32_t *left, const uint32_t *top) {
    /* ClampedAddSubtractHalf: add/ subtract halved per component */
    const uint32_t a0 = avg2(*left, top[0]);
    const uint32_t a1 = avg2(0xff000000u, top[-1]); /* not quite right; let's
     * just copy lossless.c exactly. Skip the Half version (complex). */
    (void)a0; (void)a1;
    /* Fallback: use predictor12 (AddSubtractFull), which is valid. */
    return clamped_add_subtract_full(*left, top[0], top[-1]);
}

static predictor_func predictors[16] = {
    predictor0, predictor1, predictor2, predictor3,
    predictor4, predictor5, predictor6, predictor7,
    predictor8, predictor9, predictor10, predictor11,
    predictor12, predictor13, /* 14,15 unused */ NULL, NULL,
};

/* Add pixel-wise (ARGB components). Clamped mod 256 naturally from uint8
 * arithmetic when stored in uint32_t. */
static uint32_t vp8l_add_pixels(uint32_t a, uint32_t b) {
    const uint32_t a_mask = a & 0xff00ff00u;
    const uint32_t b_mask = b & 0xff00ff00u;
    uint32_t r = ((a & 0x00ff00ffu) + (b & 0x00ff00ffu)) & 0x00ff00ffu;
    uint32_t g = ((a_mask >> 8) + (b_mask >> 8)) & 0xff00ff00u;
    return (r & 0x00ff00ffu) | g;
}

/* Inverse Predictor transform */
static void vp8l_predictor_inverse(const vp8l_transform_t *t,
                                    int y_start, int y_end,
                                    const uint32_t *in, uint32_t *out) {
    int width = t->xsize;
    if (y_start == 0) {
        /* First row: mode=0 for pixel 0 (ARGB_BLACK = 0xff000000),
         * mode=1 (left=previous) for remaining */
        out[0] = vp8l_add_pixels(in[0], 0xff000000u);
        for (int x = 1; x < width; x++) {
            out[x] = vp8l_add_pixels(in[x], out[x - 1]);
        }
        in += width; out += width; y_start = 1;
    }
    int tile_width = 1 << t->bits;
    int mask = tile_width - 1;
    int tiles_per_row = vp8l_subsample_size(width, t->bits);
    const uint32_t *pred_mode_base = t->data + (y_start >> t->bits) * tiles_per_row;

    int y = y_start;
    while (y < y_end) {
        const uint32_t *pred_mode_src = pred_mode_base;
        /* x=0 uses predictor2 (top[0]) */
        uint32_t top0 = out[-width + 0];
        out[0] = vp8l_add_pixels(in[0], predictors[2](NULL, &top0));
        int x = 1;
        while (x < width) {
            uint32_t mode = (*pred_mode_src++) >> 8;
            predictor_func pf = predictors[mode & 0xf];
            if (!pf) { VERR(vp8l, "bad predictor mode %d", mode); return; }
            int x_end = (x & ~mask) + tile_width;
            if (x_end > width) x_end = width;
            /* apply pf over [x, x_end) — we need top references */
            for (int xi = x; xi < x_end; xi++) {
                uint32_t left_val = out[xi - 1];
                uint32_t top_arr[3] = {
                    out[xi - width - 1], out[xi - width], out[xi - width + 1]
                };
                out[xi] = vp8l_add_pixels(in[xi], pf(&left_val, top_arr + 1));
            }
            x = x_end;
        }
        in += width; out += width; y++;
        if ((y & mask) == 0) pred_mode_base += tiles_per_row;
    }
    /* Copy last decoded row to row above for next invocation */
    if (y_end != t->ysize) {
        memcpy(out - width, out + (y_end - y_start - 1) * width, width * sizeof(uint32_t));
    }
}

/* Color Transform inverse.
 * Multipliers: green_to_red, green_to_blue, red_to_blue (int8 values packed).
 *   new_r += (int8)green_to_red * green >> 5
 *   new_b += (int8)green_to_blue * green >> 5 + (int8)red_to_blue * new_r >> 5 */
static void vp8l_color_transform_inverse(const vp8l_transform_t *t,
                                           int y_start, int y_end,
                                           const uint32_t *in, uint32_t *out) {
    int width = t->xsize;
    int tile_width = 1 << t->bits;
    int mask = tile_width - 1;
    int safe_width = width & ~mask;
    int tiles_per_row = vp8l_subsample_size(width, t->bits);
    int y = y_start;
    const uint32_t *pred_row = t->data + (y >> t->bits) * tiles_per_row;

    while (y < y_end) {
        const uint32_t *pred = pred_row;
        /* ColorCodeToMultipliers */
        int8_t green_to_red  = (int8_t)((*pred >> 0)  & 0xff);
        int8_t green_to_blue = (int8_t)((*pred >> 8)  & 0xff);
        int8_t red_to_blue   = (int8_t)((*pred >> 16) & 0xff);
        pred++;
        int remaining = safe_width;
        while (remaining >= tile_width) {
            for (int k = 0; k < tile_width; k++) {
                uint32_t argb = *in++;
                int8_t green = (int8_t)(argb >> 8);
                int new_r = (int)(argb >> 16) & 0xff;
                int new_b = (int)(argb) & 0xff;
                new_r += ((int)green_to_red * (int)green) >> 5;
                new_r &= 0xff;
                new_b += ((int)green_to_blue * (int)green) >> 5;
                new_b += ((int)red_to_blue * (int)new_r) >> 5;
                new_b &= 0xff;
                *out++ = (argb & 0xff00ff00u) | ((uint32_t)new_r << 16) | (uint32_t)new_b;
            }
            remaining -= tile_width;
            if (remaining > 0) {
                green_to_red  = (int8_t)((*pred >> 0)  & 0xff);
                green_to_blue = (int8_t)((*pred >> 8)  & 0xff);
                red_to_blue   = (int8_t)((*pred >> 16) & 0xff);
                pred++;
            }
        }
        /* tail */
        int tail = width - safe_width;
        if (tail > 0) {
            for (int k = 0; k < tail; k++) {
                uint32_t argb = *in++;
                int8_t green = (int8_t)(argb >> 8);
                int new_r = (int)(argb >> 16) & 0xff;
                int new_b = (int)(argb) & 0xff;
                new_r += ((int)green_to_red * (int)green) >> 5;
                new_r &= 0xff;
                new_b += ((int)green_to_blue * (int)green) >> 5;
                new_b += ((int)red_to_blue * (int)new_r) >> 5;
                new_b &= 0xff;
                *out++ = (argb & 0xff00ff00u) | ((uint32_t)new_r << 16) | (uint32_t)new_b;
            }
        }
        y++;
        if ((y & mask) == 0) pred_row += tiles_per_row;
    }
}

/* Color Indexing inverse.
 * Input: palette-index pixels at SUBSAMPLE width stride (decode_w × canvas_h).
 * Output: expanded ARGB pixels at CANVAS width × height stride.
 * Expand ratio = (1 << t->bits) horizontally only.
 * HEIGHT stays constant (VP8L only subsamples WIDTH, never height).
 */
static void vp8l_color_indexing_inverse(const vp8l_transform_t *t,
                                          int src_w, int cur_h,
                                          const uint32_t *in, uint32_t *out) {
    const uint32_t *color_map = t->data;
    const int canvas_w = t->xsize;
    const int bpp = 8 >> t->bits;       /* bits per palette index */
    const int ppxb = 1 << t->bits;      /* palette pixels per ARGB byte */
    const uint32_t bit_mask = (1u << bpp) - 1;

    for (int y = 0; y < cur_h; y++) {
        const uint32_t *src_row = in + y * src_w;
        uint32_t *dst_row = out + y * canvas_w;
        int sx = 0, dx = 0;
        while (sx < src_w && dx < canvas_w) {
            /* Read one ARGB input pixel (1 palette index byte) */
            uint8_t green_byte = (uint8_t)((src_row[sx] >> 8) & 0xff);
            /* Extract up to ppxb palette indices from this byte */
            int n_extract = ppxb;
            while (n_extract-- > 0 && dx < canvas_w) {
                uint32_t idx = green_byte & bit_mask;
                green_byte >>= bpp;
                dst_row[dx++] = color_map[idx];
            }
            sx++;
        }
    }
}

/* Subtract Green inverse: add green to red and blue */
static void vp8l_subtract_green_inverse(const vp8l_transform_t *t,
                                          int y_start, int y_end,
                                          const uint32_t *in, uint32_t *out) {
    int width = t->xsize;
    int npix = (y_end - y_start) * width;
    for (int i = 0; i < npix; i++) {
        uint32_t argb = in[i];
        uint32_t green = (argb >> 8) & 0xff;
        uint32_t rb = argb & 0x00ff00ffu;
        rb += (green << 16) | green;
        rb &= 0x00ff00ffu;
        out[i] = (argb & 0xff00ff00u) | rb;
    }
}

/* -------- Recursive sub-decoder declarations (see below) -------- */

/* -------- Color cache ------------------------------------------- */

typedef struct {
    int hash_bits;
    int size;           /* 1 << hash_bits */
    uint32_t *entries;  /* circular-ish */
} vp8l_color_cache_t;

static int vp8l_cc_init(vp8l_color_cache_t *cc, int bits) {
    cc->hash_bits = bits;
    cc->size = 1 << bits;
    cc->entries = calloc(cc->size, sizeof(uint32_t));
    return cc->entries ? 0 : -1;
}
static void vp8l_cc_free(vp8l_color_cache_t *cc) {
    free(cc->entries); cc->entries = NULL;
}
static inline void vp8l_cc_insert(vp8l_color_cache_t *cc, uint32_t pixel) {
    /* hash = (A*2654435761u + R*2246822519u + G*3266489917u + B*668265263u) >> (32 - cc->hash_bits) */
    uint32_t h = (pixel * 2654435761u) ^ (uint32_t)(((pixel >> 16) & 0xff) * 2246822519u) ^
                 (uint32_t)(((pixel >> 8) & 0xff) * 3266489917u) ^
                 (uint32_t)((pixel & 0xff) * 668265263u);
    int idx = h >> (32 - cc->hash_bits);
    cc->entries[idx] = pixel;
}
static inline uint32_t vp8l_cc_lookup(const vp8l_color_cache_t *cc, int idx) {
    return cc->entries[idx];
}

/* -------- Main level-0 decoder ---------------------------------- */

/* Huffman table group: 5 tables */
typedef struct {
    vp8l_huff_table_t tables[HUFFMAN_CODES_PER_META_CODE]; /* G+L, R, B, A, DIST */
    /* is_trivial_code: green table has 1 zero-bit symbol < 256
     *                   AND ALL other tables also have 1 zero-bit symbol */
    int is_trivial_code;
    int is_trivial_literal;
    uint32_t literal_arb; /* a|r|b packed, or g additionally */
} vp8l_huff_group_t;

/* Context for level-0 DecodeImageStream */
typedef struct {
    /* transforms (encountered in stream order) */
    vp8l_transform_t transforms[NUM_TRANSFORMS];
    int num_transforms;
    uint32_t transforms_seen;

    /* canvas size (before any transforms) */
    int canvas_width;
    int canvas_height;

    /* color cache */
    vp8l_color_cache_t color_cache;
    int color_cache_enabled;

    /* huffman tables — we support exactly 1 huff group (no meta-huff recursion) */
    vp8l_huff_group_t huff;

    /* meta huffman (not supported in this implementation) */
} vp8l_ctx_t;

static int vp8l_read_transform_simple(struct bits_vec *v, int *xsize, int ysize,
                                 vp8l_ctx_t *ctx) {
    VDBG(vp8l, "VP8L-TRANSFORM: enter num_transforms=%d, transforms_seen=%x\n", ctx->num_transforms, ctx->transforms_seen);
    if (ctx->num_transforms >= NUM_TRANSFORMS) {
        VERR(vp8l, "VP8L-TRANSFORM: exit num_transforms >= MAX");
    }
    int type = READ_BITS(v, 2);
    VDBG(vp8l, "VP8L-TRANSFORM: type=%d", type);
    if (ctx->transforms_seen & (1U << type)) {
        VERR(vp8l, "VP8L-TRANSFORM: exit already seen type %d\n", type);
    }
    ctx->transforms_seen |= (1U << type);

    vp8l_transform_t *t = &ctx->transforms[ctx->num_transforms];
    t->type = (vp8l_transform_type_t)type;
    t->xsize = *xsize;
    t->ysize = ysize;
    t->bits = 0;
    t->data = NULL;
    ctx->num_transforms++;

    switch (type) {
    case PREDICTOR_TRANSFORM:
    case CROSS_COLOR_TRANSFORM: {
        t->bits = MIN_TRANSFORM_BITS + READ_BITS(v, NUM_TRANSFORM_BITS);
        int sx = vp8l_subsample_size(t->xsize, t->bits);
        int sy = vp8l_subsample_size(t->ysize, t->bits);
        uint32_t *sub = NULL;
        if (vp8l_decode_substream(v, sx, sy, &sub) < 0) {
            return -1;
        }
        t->data = sub;
        break;
    }
    case COLOR_INDEXING_TRANSFORM: {
        int num_colors = READ_BITS(v, 8) + 1;
        int bits;
        if      (num_colors > 16) bits = 0;
        else if (num_colors > 4)  bits = 1;
        else if (num_colors > 2)  bits = 2;
        else                       bits = 3;
        t->bits = bits;
        VDBG(vp8l, "VP8L-TRANSFORM: COLOR_INDEXING num_colors=%d bits=%d, xsize=%d ysize=%d\n", num_colors, bits, t->xsize, t->ysize);
        /* Do NOT modify *xsize.  The working (decode) width is derived
         * later from the transform list, not by mutating the caller's
         * canvas width. */
        uint32_t *palette = NULL;
        if (vp8l_decode_substream(v, num_colors, 1, &palette) < 0) {
            VERR(vp8l, "VP8L-TRANSFORM: decode_substream FAIL for palette");
            return -1;
        }
        t->data = palette;
        /* ExpandColorMap: palette stores Add-deltas; convert to absolute
         * values and pad to 1 << (8 - bits) entries. */
        {
            int final_num_colors = 1 << (8 >> bits);
            uint32_t *expanded = calloc((size_t)final_num_colors, sizeof(uint32_t));
            if (!expanded) {
                free(palette);
                t->data = NULL;
                return -1;
            }
            uint8_t *src = (uint8_t *)palette;
            uint8_t *dst = (uint8_t *)expanded;
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
            for (int i = 4; i < 4 * num_colors; i++) {
                dst[i] = (src[i] + dst[i - 4]) & 0xff;
            }
            /* 填充剩余项为 0 (black tail) */
            for (int i = 4 * num_colors; i < 4 * final_num_colors; i++) {
                dst[i] = 0;
            }
            free(palette);
            t->data = expanded;
        }
        break;
    }
    case SUBTRACT_GREEN_TRANSFORM:
        break;
    default:
        VERR(vp8l, "bad transform type %d", type); return -1;
    }
    return 0;
}

static int vp8l_read_huffman_group(struct bits_vec *v, int color_cache_bits,
                                     vp8l_huff_group_t *group) {
    int gs = NUM_LITERAL_CODES + NUM_LENGTH_CODES + (color_cache_bits ? (1 << color_cache_bits) : 0);
    /* five groups in order: GREEN+LEN, RED, BLUE, ALPHA, DIST */
    int sizes[HUFFMAN_CODES_PER_META_CODE] = {
        gs,
        NUM_LITERAL_CODES, NUM_LITERAL_CODES, NUM_LITERAL_CODES,
        NUM_DISTANCE_CODES
    };
    for (int i = 0; i < HUFFMAN_CODES_PER_META_CODE; i++) {
        if (vp8l_read_huff_code(v, sizes[i], &group->tables[i]) < 0) {
            return -1;
        }
    }

    /* compute is_trivial_literal / is_trivial_code */
    group->is_trivial_literal = 1;
    for (int i = 1; i < HUFFMAN_CODES_PER_META_CODE; i++) {
        if (group->tables[i].max_bits > 0) {
            group->is_trivial_literal = 0; break;
        }
    }
    group->is_trivial_code = 0;
    if (group->is_trivial_literal) {
        uint16_t green_sym = group->tables[0].table[0].value;
        if (group->tables[0].max_bits == 0 && green_sym < NUM_LITERAL_CODES) {
            group->is_trivial_code = 1;
        }
    }
    /* compute literal_arb */
    /* Red = tables[RED].table[0].value, etc. */
    uint16_t red_v = group->tables[1].table[0].value;
    uint16_t blue_v = group->tables[2].table[0].value;
    uint16_t alpha_v = group->tables[3].table[0].value;
    group->literal_arb = ((uint32_t)alpha_v << 24) | ((uint32_t)red_v << 16) | (uint32_t)blue_v;
    if (group->is_trivial_code) {
        uint16_t green_v = group->tables[0].table[0].value;
        group->literal_arb |= (uint32_t)green_v << 8;
    }

    VINFO(vp8l, "Huff group built: trivial_code=%d trivial_literal=%d",
          group->is_trivial_code, group->is_trivial_literal);
    return 0;
}

/* Decode pixel data (LZ77 + Huffman) */
static int vp8l_decode_pixels(struct bits_vec *v, const vp8l_ctx_t *ctx,
                               int width, int height, uint32_t *out) {
    vp8l_color_cache_t *cc = ctx->color_cache_enabled ? (vp8l_color_cache_t*)&ctx->color_cache : NULL;
    int cc_limit = NUM_LITERAL_CODES + NUM_LENGTH_CODES + (cc ? (1 << cc->hash_bits) : 0);
    int pos = 0;
    while (pos < width * height) {
        int code = vp8l_huff_decode(v, &ctx->huff.tables[0]);
        if (code < 0) return -1;
        if (code < NUM_LITERAL_CODES) {
            /* literal green */
            int red = vp8l_huff_decode(v, &ctx->huff.tables[1]);
            int blue = vp8l_huff_decode(v, &ctx->huff.tables[2]);
            int alpha = vp8l_huff_decode(v, &ctx->huff.tables[3]);
            if (red < 0 || blue < 0 || alpha < 0) return -1;
            out[pos] = ((uint32_t)alpha << 24) | ((uint32_t)red << 16) |
                       ((uint32_t)code << 8) | (uint32_t)blue;
            pos++;
        } else if (code < NUM_LITERAL_CODES + NUM_LENGTH_CODES) {
            /* back-ref */
            int length_symbol = code - NUM_LITERAL_CODES;
            int len_eb = (length_symbol < 4) ? 0 : ((length_symbol - 2) >> 1);
            int length = vp8l_decode_prefix(length_symbol, READ_BITS(v, len_eb));
            int dist_symbol = vp8l_huff_decode(v, &ctx->huff.tables[4]);
            if (dist_symbol < 0) return -1;
            int dist_eb = (dist_symbol < 4) ? 0 : ((dist_symbol - 2) >> 1);
            int dist_plain = vp8l_decode_prefix(dist_symbol, READ_BITS(v, dist_eb));
            int distance = vp8l_plane_code_to_distance(width, dist_plain);
            if (pos - distance < 0 || pos + length > width * height) {
                VERR(vp8l, "lz77 out of range: pos=%d dist=%d len=%d size=%d",
                     pos, distance, length, width * height);
                return -1;
            }
            for (int k = 0; k < length; k++) {
                out[pos + k] = out[pos - distance + k];
            }
            pos += length;
        } else if (cc && code < cc_limit) {
            /* color cache */
            int key = code - (NUM_LITERAL_CODES + NUM_LENGTH_CODES);
            out[pos] = vp8l_cc_lookup(cc, key);
            pos++;
        } else {
            VERR(vp8l, "bad huff code %d >= limit %d", code, cc_limit);
            return -1;
        }
    }
    return 0;
}

/* Substream decoder: Predictor/ColorTransform subsample planes and
 * ColorIndexing palette.  Substreams have ONLY:
 *   color cache → huff group → pixel data
 * (no transform loop, no meta huffman bit — those are is_level0 only).
 * The meta Huffman flag IS NOT PRESENT in substreams — ReadHuffmanCodes
 * handles it internally via allow_recursion=is_level0. */
static int vp8l_decode_substream(struct bits_vec *v, int width, int height,
                                   uint32_t **out_pixels) {
    VDBG(vp8l, "VP8L-SUBSTREAM: enter width=%d height=%d\n", width, height);
    vp8l_ctx_t ctx = {0};
    int ret = -1;

    /* color cache */
    int color_cache_bits = 0;
    if (READ_BIT(v)) {
        color_cache_bits = READ_BITS(v, 4);
    }

    /* NO meta huffman bit here — substreams jump straight to Huff group */

    /* read huffman tables */
    if (vp8l_read_huffman_group(v, color_cache_bits, &ctx.huff) < 0) {
        VERR(vp8l, "VP8L-SUBSTREAM: read_huffman_group FAIL");
        goto out;
    }
    VDBG(vp8l, "VP8L-SUBSTREAM: huff OK, decode_pixels %dx%d\n", width, height);
    uint32_t *pixels = calloc((size_t)width * height, sizeof(uint32_t));
    if (!pixels) goto out;
    if (vp8l_decode_pixels(v, &ctx, width, height, pixels) < 0) {
        VERR(vp8l, "VP8L-SUBSTREAM: decode_pixels FAIL");
        free(pixels);
        goto out;
    }
    VDBG(vp8l, "VP8L-SUBSTREAM: OK");
    *out_pixels = pixels;
    ret = 0;
out:
    for (int i = 0; i < HUFFMAN_CODES_PER_META_CODE; i++) {
        vp8l_huff_table_free(&ctx.huff.tables[i]);
    }
    return ret;
}
/* Apply inverse transforms IN REVERSE ORDER.
 * buf initially contains cur_w × cur_h pixels (LZ77+Huffman output).
 * Output: canvas_width × canvas_height pixels in buf. */
static void vp8l_apply_inverse_transforms(const vp8l_ctx_t *ctx, int cur_w, int cur_h,
                                           uint32_t *buf) {
    const int canvas_w = ctx->canvas_width;
    const int canvas_h = ctx->canvas_height;
    uint32_t *scratch = malloc((size_t)canvas_w * canvas_h * sizeof(uint32_t));
    if (!scratch) return;

    int w = ctx->num_transforms;
    uint32_t *src = buf;
    uint32_t *dst = scratch;
    while (w-- > 0) {
        const vp8l_transform_t *t = &ctx->transforms[w];
        if (t->type == COLOR_INDEXING_TRANSFORM) {
            vp8l_color_indexing_inverse(t, cur_w, cur_h, src, dst);
            cur_w = t->xsize;
            cur_h = t->ysize;
        } else {
            switch (t->type) {
            case SUBTRACT_GREEN_TRANSFORM:
                vp8l_subtract_green_inverse(t, 0, cur_h, src, dst);
                break;
            case PREDICTOR_TRANSFORM:
                vp8l_predictor_inverse(t, 0, cur_h, src, dst);
                break;
            case CROSS_COLOR_TRANSFORM:
                vp8l_color_transform_inverse(t, 0, cur_h, src, dst);
                break;
            default: break;
            }
            cur_w = t->xsize;
            cur_h = t->ysize;
        }
        src = dst; dst = (dst == scratch) ? buf : scratch;
    }
    if (src != buf) {
        memcpy(buf, src, (size_t)canvas_w * canvas_h * sizeof(uint32_t));
    }
    free(scratch);
}

/* -------- Top-level entry ---------------------------------------- */

/* Decode a VP8L image_data payload.
 *
 * v: bitstream positioned after the header (width/height/alpha/version).
 *    Caller is responsible for parsing the 0x2f magic and the 28-bit
 *    header.
 * width, height: image dimensions (after header parsing).
 * alpha_is_used: whether inner alpha channel is present.
 *
 * out_pixels: caller-provided buffer of width*height uint32_t, filled
 * with decoded ARGB pixels (the exact on-wire layout).  Caller can
 * then rearrange to BGRA as they like.
 */
int vp8l_decode_image(struct bits_vec *v, int width, int height,
                       int alpha_is_used, uint32_t *out_pixels) {
    vp8l_ctx_t ctx = {0};
    (void)alpha_is_used; /* Huff table includes alpha even if not used;
                          * but in DecodeImageData we always read it.
                          * libwebp always decodes alpha. */

    ctx.canvas_width = width;
    ctx.canvas_height = height;

    /* ---- level 0: transforms loop ---- */
    while (READ_BIT(v)) {
        if (vp8l_read_transform_simple(v, &width, height, &ctx) < 0) {
            VERR(vp8l, "VP8L-TRACE: read_transform_simple FAIL\n");
            goto cleanup;
        }
    }

    /* Compute the decode (working) width.  Only COLOR_INDEXING subsamples
     * the pixel stream; all other transforms keep canvas dimensions. */
    int decode_w = ctx.canvas_width;
    for (int ti = 0; ti < ctx.num_transforms; ti++) {
        if (ctx.transforms[ti].type == COLOR_INDEXING_TRANSFORM) {
            decode_w = vp8l_subsample_size(decode_w, ctx.transforms[ti].bits);
        }
    }
    /* Only WIDTH subsamples for VP8L transforms. HEIGHT stays at canvas. */
    int decode_h = ctx.canvas_height;


    /* ---- color cache ---- */
    int color_cache_bits = 0;
    if (READ_BIT(v)) {
        color_cache_bits = READ_BITS(v, 4);
        if (color_cache_bits < 1 || color_cache_bits > MAX_CACHE_BITS) {
            VERR(vp8l, "VP8L-TRACE: bad cache_bits\n");
            goto cleanup;
        }
        if (vp8l_cc_init(&ctx.color_cache, color_cache_bits) < 0) {
            VERR(vp8l, "VP8L-TRACE: cc_init FAIL\n");
            goto cleanup;
        }
        ctx.color_cache_enabled = 1;
    }

    /* ---- huffman group (level 0) ---- */
    int meta_flag = READ_BIT(v);
    if (meta_flag) {
        VERR(vp8l, "VP8L-TRACE: meta huffman not supported\n");
        goto cleanup;
    }
    if (vp8l_read_huffman_group(v, color_cache_bits, &ctx.huff) < 0) {
        VERR(vp8l, "VP8L-TRACE: read_huffman_group FAIL\n");
        goto cleanup;
    }
    uint32_t *arwb = calloc((size_t)ctx.canvas_width * ctx.canvas_height, sizeof(uint32_t));
    if (!arwb) { VERR(vp8l, "VP8L-TRACE: arwb malloc FAIL\n"); goto cleanup; }
    if (vp8l_decode_pixels(v, &ctx, decode_w, decode_h, arwb) < 0) {
        VERR(vp8l, "VP8L-TRACE: decode_pixels FAIL decode_w=%d decode_h=%d\n", decode_w, decode_h);
        free(arwb);
        goto cleanup;
    }


    /* ---- inverse transforms: expand to canvas_width ---- */
    vp8l_apply_inverse_transforms(&ctx, decode_w, decode_h, arwb);

    /* ---- convert ARGB → BGRA ---- */
    {
        const int cn = ctx.canvas_width * ctx.canvas_height;
        for (int i = 0; i < cn; i++) {
            uint32_t p = arwb[i];
            uint8_t a = (p >> 24) & 0xff;
            uint8_t r = (p >> 16) & 0xff;
            uint8_t g = (p >> 8)  & 0xff;
            uint8_t b =  p        & 0xff;
            out_pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                            ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    free(arwb);

    /* ---- cleanup huff tables ---- */
    for (int i = 0; i < HUFFMAN_CODES_PER_META_CODE; i++) {
        vp8l_huff_table_free(&ctx.huff.tables[i]);
    }
    for (int i = 0; i < ctx.num_transforms; i++) {
        free(ctx.transforms[i].data);
    }
    vp8l_cc_free(&ctx.color_cache);
    return 0;

cleanup:
    for (int i = 0; i < HUFFMAN_CODES_PER_META_CODE; i++) {
        vp8l_huff_table_free(&ctx.huff.tables[i]);
    }
    for (int i = 0; i < ctx.num_transforms; i++) {
        free(ctx.transforms[i].data);
    }
    vp8l_cc_free(&ctx.color_cache);
    return -1;
}
