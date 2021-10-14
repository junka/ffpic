#ifndef _CRC_H_
#define _CRC_H_

#ifdef __cplusplus
extern "C" {
#endif

uint32_t init_crc32(uint8_t *buf, int len);

uint32_t finish_crc32(uint32_t crc);

uint32_t update_crc(uint32_t crc, uint8_t *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /*_CRC_H_*/