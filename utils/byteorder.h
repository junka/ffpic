#ifndef _BYTE_ORDER_H_
#define _BYTE_ORDER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint16_t host_order_16(uint16_t a);
uint32_t host_order_32(uint32_t a);
uint64_t host_order_64(uint64_t a);

#define SWAP(a) _Generic((a) , \
                    uint32_t : host_order_32, \
                    int32_t : host_order_32, \
                    uint64_t : host_order_64, \
                    int64_t : host_order_64, \
                    uint16_t : host_order_16, \
                    int16_t : host_order_16, \
                    default : host_order_32)(a)


#ifdef __cplusplus
}
#endif

#endif /* _BYTE_ORDER_H_ */