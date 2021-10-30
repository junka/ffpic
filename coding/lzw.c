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
int 
lzw_decode_gif(int order, int codesize, const uint8_t* compressed, 
            int compressed_length, uint8_t* decompressed)
{
	int code_clear = 1 << codesize;
	int pos = 0;
	int code_length = codesize + 1;
	int code_eoi = code_clear + 1;
	int reset_code_length = code_length;
	int prev = -1;
	int code;

	int buffer = 0;
	int buffer_size = 0;

	int ch;
	int dict_index = 0;
	int dict_capacity = 1 << code_length;

	Entry* dict = (Entry*)malloc((1<<12) * sizeof(Entry));
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
				// 	dict[i].data = i;
				// 	dict[i].prev = -1;
				// 	dict[i].length = 1;
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
				ch = code;
				decompressed[pos++] = code;
				prev = code;
				continue;
			}

			
			if (code > dict_index) {
				int len = dict[code].length;

				int tempcode = code;
				for (int i = 0; i < len; i++) {
					decompressed[pos + len - i - 1] = (uint8_t)dict[tempcode].data;
					tempcode = dict[tempcode].prev;
				}
				
				decompressed[pos + len] = (uint8_t)ch;
				pos += (len + 1);
			} else {
				// int match_length = dict[code].length;
				// int tempcode = code;
				// for (int ii = 0; ii < match_length; ii++) {
				// 	ch = dict[tempcode].data;
				// 	decompressed[pos + match_length - ii - 1] = dict[tempcode].data;
				// 	tempcode = dict[tempcode].prev;
				// }
				// pos += match_length;
			}

			if ((dict_index < (1<<12))) {
				int ptr = (code == dict_index ? prev : code);
				// int ptr = code;
				int len = dict[ptr].length;
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
				for (int ii = 0; ii < match_length; ii++) {
					ch = dict[tempcode].data;
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
	int buf;
	int tempcode;
	int ch;
	int buffer;
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