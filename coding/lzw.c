#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "lzw.h"

typedef struct {
    int length;
    int prev;
    uint8_t data;
} Entry;

/* order 0 tiff usually use,
 * while 1 means compatible mode, gif use it
 */
/* the maximum code size of a gif lzw stream, the spec allows 2..8 */
#define LZW_GIF_MIN_CODE_SIZE    2
#define LZW_GIF_MAX_CODE_SIZE    8
#define LZW_GIF_MAX_OUTPUT_CODE  4096
#define LZW_GIF_MAX_DICT_ENTRY   (1 << 12)

int 
lzw_decode_gif(int order, int codesize, const uint8_t* compressed, 
            int compressed_length, uint8_t* decompressed, int out_capacity)
{
    if (!compressed || !decompressed || compressed_length < 0 || out_capacity < 0)
        return -1;

    /* the code size decides how many dictionary entries, code_clear and the
     * rest of the codes the loops below touch, so it must be in range
     */
    if (codesize < LZW_GIF_MIN_CODE_SIZE || codesize > LZW_GIF_MAX_CODE_SIZE)
        return -1;

    int code_clear = 1 << codesize;
    int pos = 0;
    int code_length = codesize + 1;
    int code_eoi = code_clear + 1;
    int reset_code_length = code_length;
    int prev = code_clear;
    int code;

    int buffer = 0;
    int buffer_size = 0;

    int dict_index = 0;
    int dict_capacity = 1 << code_length;

    Entry* dict = (Entry*)malloc(LZW_GIF_MAX_DICT_ENTRY * sizeof(Entry));
    if (!dict)
        return -1;
    for (int i = 0; i < code_clear; i++) {
        dict[i].data = i;
        dict[i].prev = 0;
        dict[i].length = 1;
    }
    dict_index = (code_eoi + 1);

    for (int i = 0; i < compressed_length; i++) {
        if (order == 1) {
            buffer |= (compressed[i] << buffer_size);
        } else {
            buffer = ((buffer<<8)|compressed[i]);
        }
        buffer_size += 8;

        while (buffer_size >= code_length) {
            if (order == 1) {
                code = buffer & ((1 << code_length) - 1);
                buffer >>= code_length;
            } else {
                // buffer 
                code = (buffer >> (buffer_size - code_length)) & ((1 << code_length) -1);
                buffer = buffer & ((1 << (buffer_size - code_length)) - 1);
            }
            buffer_size -= code_length;

            if (code == code_clear) {
                code_length = reset_code_length;
                dict_capacity = 1 << code_length;

                // for (int i = 0; i < code_clear; i++) {
                //     dict[i].data = i;
                //     dict[i].prev = -1;
                //     dict[i].length = 1;
                // }
                dict_index = code_clear + 2;
                prev = code;
                continue;
            }

            if (code == code_eoi) {
                free(dict);
                return pos;
            }
            if (prev == code_clear) {
                /* the code right after a clear has to be a literal, a
                 * dictionary code can not exist yet
                 */
                if (code >= code_clear) {
                    free(dict);
                    return -1;
                }
                if (pos >= out_capacity) {
                    free(dict);
                    return -1;
                }
                decompressed[pos++] = code;
                prev = code;
                continue;
            }

            if (code > dict_index) {
                /* an entry that was never built, the standard kwkwk case is
                 * code == dict_index which is handled below
                 */
                free(dict);
                return -1;
            }

            if ((dict_index < LZW_GIF_MAX_DICT_ENTRY)) {
                /* prev is a code decoded before and it must already be in the
                 * dictionary, otherwise its entry is uninitialized garbage
                 */
                if (prev < 0 || prev >= dict_index) {
                    free(dict);
                    return -1;
                }
                int ptr = (code == dict_index ? prev : code);
                // int ptr = code;
                int len = dict[ptr].length;
                /* any expansion is at least one output byte, a string can
                 * never be longer than the number of codes left plus the
                 * bytes already decoded
                 */
                if (len > LZW_GIF_MAX_OUTPUT_CODE + (out_capacity - pos)) {
                    free(dict);
                    return -1;
                }
                for (int i = 0; i <= len; i++) {
                    if (dict[ptr].prev!=0)
                        ptr = dict[ptr].prev;
                }
                dict[dict_index].prev = prev;
                dict[dict_index].data = dict[ptr].data;
                dict[dict_index].length = dict[prev].length + 1;

                dict_index ++;
                if ((dict_index == dict_capacity) && (code_length < 12)) {
                    code_length ++;
                    dict_capacity <<= 1;
                }
            }
            
            if (code <= dict_index) {
                int match_length = dict[code].length;
                int tempcode = code;
                if (match_length <= 0 || match_length > LZW_GIF_MAX_OUTPUT_CODE ||
                    match_length > (out_capacity - pos)) {
                    free(dict);
                    return -1;
                }
                for (int ii = 0; ii < match_length; ii++) {
                    decompressed[pos + match_length - ii - 1] = dict[tempcode].data;
                    tempcode = dict[tempcode].prev;
                }
                pos += match_length;
            }
            prev = code;
        }
    }

    free(dict);
    return pos;
}

static bool
big_endian(int codesize, const uint8_t* raw)
{
    int code_clear = 1 << codesize;
    int code_len = codesize + 1;
    uint16_t big = (raw[0] << 8 | raw[1]);
    if (((big >> (16 - code_len)) & ((1 << code_len)-1)) == code_clear) {
        return true;
    }
    return false;
}

int 
lzw_decode_tiff(int legacy, int codesize, const uint8_t *raw,
             int count, uint8_t *dec)
{
    int code_clear = 1 << codesize;
    int code_eoi = code_clear + 1;
    int code_len = codesize + 1;;
    int capacity = 1 << code_len;
    int dict_index = code_eoi + 1;
    bool endian = big_endian(codesize, raw);

    if (!raw || !dec)
        return -1;

    int pos = 0;
    int ii, tsize;
    int len;
    int second;
    // the stream starts as if just after a clear code, so the first code word
    // is emitted on its own instead of extending a previous entry
    int buf = code_clear;
    int tempcode;
    int ch = 0;
    int buffer = 0;
    int buffer_size = 0;

    Entry *table = malloc(sizeof(Entry) * (1 << 12));
    if (legacy == 1)
        tsize = code_clear;
    else 
        tsize = dict_index;

    for (ii = 0; ii < tsize; ii++) {
        table[ii].prev = 0;
        table[ii].length = 1;
        table[ii].data = ii;
    }

    for (int i = 0; i < count; i++)
    {
        if (legacy == 1) {
            buffer |= (raw[i] << buffer_size);
        } else {
            buffer = ((buffer << 8) | raw[i]);
        }
        buffer_size += 8;

        while (buffer_size >= code_len) {
            if (legacy == 1) {
                second = buffer & ((1 << code_len) - 1);
                buffer >>= code_len;
            } else {
                second = (buffer >> (buffer_size - code_len)) & ((1 << code_len) -1);
                buffer = buffer & ((1 << (buffer_size - code_len)) - 1);
            }
            buffer_size -= code_len;

            if (second == code_eoi) {
                break;
            }

            if (second == code_clear) {
                //reset 
                dict_index = code_eoi + 1;
                code_len = codesize + 1;
                capacity = (1 << code_len);

                buf = second;
                continue;
            }

            if (buf == code_clear) {
                ch = second;
                dec[pos++] = ch;
                buf = second;
                continue;
            }

            if (second >= (dict_index + (legacy?1:0))) {
                len = table[buf].length;

                tempcode = buf;
                for (ii = 0; ii < len; ii++) {
                    if (legacy == 1)
                        dec[pos + len - ii - 1] = (uint8_t)table[tempcode].data;
                    else
                        dec[pos + len - ii - 1] = (uint8_t)table[tempcode].data;
                    tempcode = table[tempcode].prev;
                }
                
                dec[pos + len] = (uint8_t)ch;
                pos += (len + 1);
            } else {
                len = table[second].length;
                tempcode = second;

                for (ii = 0; ii < len; ii++) {
                    ch = table[tempcode].data;
                    dec[pos + len - ii - 1] = (uint8_t)ch;
                    tempcode = table[tempcode].prev;
                }
                pos += len;
            }

            if (dict_index < (1 << 12)) {
                table[dict_index].prev = buf;
                table[dict_index].length = table[buf].length + 1;
                table[dict_index].data = ch;

                dict_index ++;
                if ((dict_index == capacity - ((endian == 1) ? 1 : 0))&&
                    code_len < 12) {
                    code_len ++;
                    capacity = (1 << code_len);
                }
            }

            buf = second;
        }
    }

    free(table);
    return pos;
}
