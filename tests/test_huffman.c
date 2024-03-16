#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "huffman.h"

int test_decoder(void)
{
    huffman_tree *dtree = huffman_tree_init();
    if (!dtree) {
        return -1;
    }
    /*
        This is not Canonical
        Symbol | Code | Bit Length
        -------+------+-----------
        A      | 00   | 2
        B      | 01   | 2
        C      | 100  | 3
        D      | 101  | 3

        DADBCD encoded to 1010010101100101
    */
    uint8_t num_codecs[16] = {0, 2, 2, 0};
    uint8_t syms[] = {'A', 'B', 'C', 'D'};
    uint8_t expect[] = "DADBCD";
    size_t len = sizeof(expect)-1;
    struct huffman_symbol *s = huffman_symbol_alloc(num_codecs, syms);

    huffman_build_lookup_table(dtree, 0, s);
    // huffman_dump_table(stdout, dtree);

    uint8_t *data = malloc(2);
    data[0] = 0xA5;
    data[1] = 0x65;

    // data must be a heap memory
    struct huffman_codec *hdec = huffman_codec_init(data, 2);
    if (!hdec) {
        huffman_cleanup(dtree);
        free(s->syms);
        free(s);
        return -1;
    }

    int ret = 0;
    size_t i = 0;
    do {
        ret = huffman_decode_symbol(hdec, dtree);
        // printf("get %c expect %c\n", ret, expect[i]);
        if (expect[i++] != ret) {
            goto err_decode;
        }
    } while (ret != -1 && i < len);

    free(s->syms);
    free(s);
    huffman_codec_free(hdec);
    huffman_cleanup(dtree);
    return 0;
err_decode:
    free(s->syms);
    free(s);
    huffman_codec_free(hdec);
    huffman_cleanup(dtree);
    return -1;
}


int test_encoder(void)
{

    /* Canonical encoder 
    A 1
    B 1
    C 2
    D 3
          6
         / \
        3   D
       / \
      2   C
     / \
    A   B 
    D 0   -> 0
    C 10  -> 10
    B 110 -> 110
    A 111 -> 111
    */
    unsigned char data[] = "DADBCDCB";
    uint8_t expect[] = {0x76, 0x96};
    int size = sizeof(data) / sizeof(data[0])-1;
    struct huffman_tree *tree = huffman_scan_buff(data, size, 0);
    if (!tree) {
        return -1;
    }
    //huffman_dump_table(stdout, tree);
    struct huffman_codec *codec = huffman_codec_init(NULL, 0);
    for (size_t i = 0; i < sizeof(data) / sizeof(data[0]) - 1; i++) {
        huffman_encode_symbol(codec, tree, data[i]);
    }
    uint8_t *got;
    int len = huffman_encode_done(codec, &got);
    if(len != 2) {
        printf("len %d\n", len);
        return -1;
    }
    if (got[0] != expect[0] || got[1] != expect[1]) {
        printf("%x , %x\n", got[0], got[1]);
        return -1;
    }

    return 0;
}

int main(void) {
    if (test_decoder()) {
        printf("test decoder fail\n");
        return -1;
    }
    printf("test decoder done\n");
    if (test_encoder()) {
        printf("test encoder fail\n");
        return -1;
    }
    printf("test encoder done\n");
    return 0;
}
