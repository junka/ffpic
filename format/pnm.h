#ifndef _PNM_H_
#define _PNM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#pragma pack(push, 1)

struct file_header {
    uint8_t magic;
    uint8_t version;
};

// struct ppm_color {
//     uint8_t red;
//     uint8_t green;
//     uint8_t blue;
// };

enum tuple_type {
    BLACKANDWHITE = 1,
    GRAYSCALE = 2,
    RGB = 3,
    BLACKANDWHITE_ALPHA = 4,
    GRAYSCALE_ALPHA = 5, 
    RGB_ALPHA = 6
};

typedef struct {
    struct file_header pn;
    int width;
    int height;
    int color_size;

    int depth;
    int tupe_type;

    uint8_t *data;
}PNM;


#pragma pack(pop)


void PNM_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_PNM_H_*/
