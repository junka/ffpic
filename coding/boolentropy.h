#ifndef _BOOL_ENTROPY_H_
#define _BOOL_ENTROPY_H_

#ifdef __cplusplus
extern "C"{
#endif

#define BOOL_VALUE_SIZE ((int)sizeof(size_t) * CHAR_BIT)

/*This is meant to be a large, positive constant that can still be efficiently
   loaded as an immediate (on platforms like ARM, for example).
  Even relatively modest values like 100 would work fine.*/
#define VP8_LOTS_OF_BITS (0x40000000)

typedef struct bool_tree {
    uint64_t value;
    uint32_t range;     //[127, 254]
    int count;
    struct bits_vec *bits;
} bool_tree;

bool_tree *bool_tree_init(uint8_t* start, int len);

void bool_tree_free(bool_tree *bt);

int bool_decode(bool_tree *br, int probability);

int bool_decode_alt(bool_tree *br, int probability);

#ifdef __cplusplus
}
#endif

#endif /*_BOOL_ENTROPY_H_*/