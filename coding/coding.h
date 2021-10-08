#ifndef _CODING_H_
#define _CODING_H_

#ifdef __cplusplus
extern "C"{
#endif

typedef int (* decoding)(const uint8_t *buf, unsigned len);

typedef int (* encoding)(const uint8_t *buf, unsigned len);


#ifdef __cplusplus
}
#endif

#endif /*_CODING_H_*/