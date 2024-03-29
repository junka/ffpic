
#include <arpa/inet.h>

#include "byteorder.h"

uint32_t
net_order_32(uint32_t a)
{
    return htonl(a);
}


uint64_t
host_order_64(uint64_t a)
{
#if __APPLE__
    return ntohll(a);
#else
    return be64toh(a);
#endif
}

uint32_t
host_order_32(uint32_t a)
{
    return ntohl(a);
}

uint16_t
host_order_16(uint16_t a)
{
    return ntohs(a);
}

