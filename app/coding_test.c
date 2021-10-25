#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coding.h"
#include "huffman.h"


void strreverse(char* begin, char* end) {
	char aux;
	while(end>begin)
		aux=*end, *end--=*begin, *begin++=aux;
}
	
void itoa(int value, char* str, int base) {
	static char num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	char* wstr=str;
	int sign;	
	div_t res;

	// Validate base
	if (base<2 || base>35){ *wstr='\0'; return; }

	// Take care of sign
	if ((sign=value) < 0) value = -value;

	// Conversion. Number is reversed.
	do {
		res = div(value,base);
		*wstr++ = num[res.rem];
        value=res.quot;
	}while(value);

	if(sign<0) *wstr++='-';
	*wstr='\0';
	// Reverse string	
	strreverse(str,wstr-1);
	
}


int main()
{
    uint8_t dc[16]={0, 2, 2, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t syms[] = {3, 4, 2, 5, 1, 6, 7, 0, 8, 9};
    huffman_tree *tree = huffman_tree_init();
    huffman_build_lookup_table(tree, 0, 0, dc, syms);
    uint8_t data[] = {0xFC , 0xFF, 0xE2 , 0xAF , 0xEF , 0xF3 , 0x15, 0x7F};
    huffman_decode_start(data, 8);
    int a = huffman_decode_symbol(tree);
    huffman_decode_end();
    printf("maxbitlen %d, numofcodes %d\n", tree->maxbitlen, tree->numcodes);
    huffman_cleanup(tree);
    printf("decode first dc %d\n", a);
    
    return 0;
}
