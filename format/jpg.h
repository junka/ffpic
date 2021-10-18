#ifndef _JPG_H_
#define _JPG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define _LITTLE_ENDIAN_

#ifdef _LITTLE_ENDIAN_
#define _MARKER(x, v) (v<<8|x)
#else
#defien _MARKER(x, v) (x<<8|v)
#endif

#define MARKER(v) _MARKER(0xFF, v)

#define SOI MARKER(0xD8)
#define EOI MARKER(0xD9)
#define SOS MARKER(0xDA)
#define DQT MARKER(0xDB)
#define DRI MARKER(0xDD)
#define COM MARKER(0xFE)
 

typedef struct {

}JPG;


void JPG_init(void);

#ifdef __cplusplus
}
#endif

#endif /*_JPG_H_*/