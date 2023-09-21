#ifndef _BMPWRITEER_H_
#define _BMPWRITEER_H_

#ifdef __cplusplus
extern "C"{
#endif

#pragma pack(push,2)

struct bmp_header {
    //bmp file header
    uint16_t file_type;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;

    //bmp info head
    uint32_t biSize;
    uint32_t biWidth;
    uint32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    uint32_t biXPelsPerMeter;
    uint32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};

#pragma pack(pop)


void bmp_writer_register(void);


#ifdef __cplusplus
}
#endif

#endif /*_BMPWRITEER_H_*/
