#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "lzw.h"

typedef struct {
	int length;
	int prev;
	uint8_t data;
} Entry;

void 
lzw_decode(int lzw_code_size, const uint8_t* compressed, 
            int compressed_length, uint8_t* decompressed)
{
	int code_length = lzw_code_size + 1;
	int clear_code = 1 << lzw_code_size;
	int end_of_information = clear_code + 1;
	int reset_code_length = code_length;
	int prev = -1;

	int buffer = 0;
	int buffer_size = 0;

	int dict_index;
	int dict_capacity = 1 << code_length;
	Entry* dict = (Entry*)malloc(dict_capacity * sizeof(Entry));
	for (dict_index = 0; dict_index < clear_code; dict_index++) {
		dict[dict_index].data = dict_index;
		dict[dict_index].prev = -1;
		dict[dict_index].length = 1;
	}
	dict_index += 2;

	for (int i = 0; i < compressed_length; i++) {
		buffer |= compressed[i] << buffer_size;
		buffer_size += 8;

		while (buffer_size >= code_length) {
			buffer_size -= code_length;
			int code = buffer & ((1 << code_length) - 1);
			buffer >>= code_length;

			if (code == clear_code) {
				code_length = reset_code_length;
				dict_capacity = 1 << code_length;

				for (dict_index = 0; dict_index < clear_code; dict_index++) {
					dict[dict_index].data = dict_index;
					dict[dict_index].prev = -1;
					dict[dict_index].length = 1;
				}
				dict_index += 2;
				prev = -1;
				continue;
			}

			if (code == end_of_information) {
				free(dict);
				return;
			}

			if ((prev > -1) && (dict_index != dict_capacity)) {
				if (code > dict_index) {
					printf("lzw code error, got %d, but dict %d\n", code, dict_index);
					free(dict);
					return;
				}

				int ptr = code == dict_index ? prev : code;
				while (dict[ptr].prev != -1) {
					ptr = dict[ptr].prev;
				}
				dict[dict_index].data = dict[ptr].data;
				dict[dict_index].prev = prev;
				dict[dict_index].length = dict[prev].length + 1;
				dict_index++;

				if ((dict_index == dict_capacity) && (code_length < 12)) {
					code_length++;
					dict_capacity <<= 1;
					dict = (Entry*)realloc(dict, dict_capacity * sizeof(Entry));
				}
			}

			prev = code;
			int match_length = dict[code].length;
			while (code != -1) {
				decompressed[dict[code].length - 1] = dict[code].data;
				if (dict[code].prev == code) {
					printf("self reference error\n");
					free(dict);
					return;
				}
				code = dict[code].prev;
			}
			decompressed += match_length;
		}
	}

	free(dict);
}