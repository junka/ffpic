#include <stdint.h>
#include <stdio.h>

#include "coding.h"
#include "huffman.h"

int main()
{
    uint8_t dc[16]={00, 02, 02, 03, 01, 01, 01, 00, 00, 00, 00, 00, 00, 00, 00, 00};
    uint8_t codes[] = {3, 4, 2, 5, 1, 6, 7, 0, 8, 9};
    huffman_tree tree;
    huffman_tree_init(&tree, NULL, 10, 16);
    printf("help\n");
    return 0;
}