#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include "webp.h"
#include "file.h"
#include "booldec.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(webp, DEBUG);

static int
WEBP_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        VCRIT(webp, "fail to open %s", filename);
        return -ENOENT;
    }
    struct webp_header h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        VCRIT(webp, "read %s file webp header error", filename);
        return -EBADF;
    }
    fclose(f);
    if (!memcmp(&h.riff, "RIFF", 4) && !memcmp(&h.webp, "WEBP", 4)) {
        return 0;
    }

    VDBG(webp, "%s is not a valid webp file", filename);
    return -EINVAL;
}


static const uint8_t abs0[255 + 255 + 1] = {
  0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4,
  0xf3, 0xf2, 0xf1, 0xf0, 0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8,
  0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0, 0xdf, 0xde, 0xdd, 0xdc,
  0xdb, 0xda, 0xd9, 0xd8, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0,
  0xcf, 0xce, 0xcd, 0xcc, 0xcb, 0xca, 0xc9, 0xc8, 0xc7, 0xc6, 0xc5, 0xc4,
  0xc3, 0xc2, 0xc1, 0xc0, 0xbf, 0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xb9, 0xb8,
  0xb7, 0xb6, 0xb5, 0xb4, 0xb3, 0xb2, 0xb1, 0xb0, 0xaf, 0xae, 0xad, 0xac,
  0xab, 0xaa, 0xa9, 0xa8, 0xa7, 0xa6, 0xa5, 0xa4, 0xa3, 0xa2, 0xa1, 0xa0,
  0x9f, 0x9e, 0x9d, 0x9c, 0x9b, 0x9a, 0x99, 0x98, 0x97, 0x96, 0x95, 0x94,
  0x93, 0x92, 0x91, 0x90, 0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88,
  0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80, 0x7f, 0x7e, 0x7d, 0x7c,
  0x7b, 0x7a, 0x79, 0x78, 0x77, 0x76, 0x75, 0x74, 0x73, 0x72, 0x71, 0x70,
  0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
  0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5c, 0x5b, 0x5a, 0x59, 0x58,
  0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4c,
  0x4b, 0x4a, 0x49, 0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41, 0x40,
  0x3f, 0x3e, 0x3d, 0x3c, 0x3b, 0x3a, 0x39, 0x38, 0x37, 0x36, 0x35, 0x34,
  0x33, 0x32, 0x31, 0x30, 0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28,
  0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c,
  0x1b, 0x1a, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10,
  0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
  0x03, 0x02, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
  0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
  0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c,
  0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
  0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44,
  0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
  0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c,
  0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74,
  0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80,
  0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c,
  0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
  0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
  0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
  0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc,
  0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
  0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0,
  0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec,
  0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static const uint8_t sclip1[1020 + 1020 + 1] = {
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
  0x80, 0x80, 0x80, 0x80, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93,
  0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab,
  0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
  0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3,
  0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
  0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
  0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
  0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
  0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
  0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
  0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
  0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
  0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f
};

static const uint8_t sclip2[112 + 112 + 1] = {
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
  0xfc, 0xfd, 0xfe, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
  0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
};

static const uint8_t clip1[255 + 511 + 1] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c,
    0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
    0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c,
    0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80,
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c,
    0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
    0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
    0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0,
    0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const int8_t* const VP8ksclip1 = (const int8_t*)&sclip1[1020];
const int8_t* const VP8ksclip2 = (const int8_t*)&sclip2[112];
const uint8_t* const VP8kclip1 = &clip1[255];
const uint8_t* const VP8kabs0 = &abs0[255];


static void
read_vp8_segmentation_adjust(struct vp8_update_segmentation *s, struct bool_dec *br)
{
    s->segmentation_enabled = BOOL_BIT(br);
    if (s->segmentation_enabled) {
        s->update_mb_segmentation_map = BOOL_BIT(br);
        s->update_segment_feature_data = BOOL_BIT(br);
        if (s->update_segment_feature_data) {
            s->segment_feature_mode = BOOL_BIT(br);
            for (int i = 0; i < NUM_MB_SEGMENTS; i ++) {
                s->quant[i].quantizer_update = BOOL_BIT(br);
                if (s->quant[i].quantizer_update) {
                    s->quant[i].quantizer_update_value = BOOL_BITS(br, 7);
                    if (BOOL_BIT(br)) {
                        s->quant[i].quantizer_update_value *= -1;
                    }
                }
            }
            for (int i = 0; i < NUM_MB_SEGMENTS; i ++) {
                s->lf[i].loop_filter_update = BOOL_BIT(br);
                if (s->lf[i].loop_filter_update) {
                    s->lf[i].lf_update_value = BOOL_BITS(br, 6);
                    if (BOOL_BIT(br)) {
                        s->lf[i].lf_update_value *= -1;
                    }
                } else {
                    s->lf[i].lf_update_value = 0;
                }
            }
        }
        if (s->update_mb_segmentation_map) {
            for (int i = 0; i < MB_FEATURE_TREE_PROBS; i ++) {
                if (BOOL_BIT(br)) {
                    s->segment_prob[i] = BOOL_BITS(br, 8);
                }
            }
        }
    } else {
        s->update_mb_segmentation_map = 0;
        s->update_segment_feature_data = 0;
    }
}

static void
read_mb_lf_adjustments(struct vp8_mb_lf_adjustments *d, struct bool_dec *br)
{
    d->loop_filter_adj_enable = BOOL_BIT(br);
    if (d->loop_filter_adj_enable) {
        d->mode_ref_lf_delta_update_flag = BOOL_BIT(br);
        if (d->mode_ref_lf_delta_update_flag) {
            for (int i = 0; i < NUM_REF_LF_DELTAS; i ++) {
                // d->mode_ref_lf_delta_update[i].ref_frame_delta_update_flag = BOOL_BIT(br);
                if (BOOL_BIT(br)) {
                    d->mode_ref_lf_delta_update[i] = BOOL_BITS(br, 6);
                    if (BOOL_BIT(br)) {
                        d->mode_ref_lf_delta_update[i] *= -1;
                    }
                } else {
                    d->mode_ref_lf_delta_update[i] = 0;
                }
            }
            for (int i = 0; i < NUM_MODE_LF_DELTAS; i ++) {
                // d->mb_mode_delta_update[i].mb_mode_delta_update_flag = BOOL_BIT(br);
                if (BOOL_BIT(br)) {
                    d->mb_mode_delta_update[i] = BOOL_BITS(br, 6);
                    if (BOOL_BIT(br)) {
                        d->mb_mode_delta_update[i] *= -1;
                    }
                } else {
                    d->mb_mode_delta_update[i] = 0;
                }
            }
        }
    }
}

static void
read_token_partition(WEBP *w, struct bool_dec *br, FILE *f)
{
    /* all partions info | partition 1| partition 2 */
    int log2_nbr_of_dct_partitions = BOOL_BITS(br, 2);
    int num = (1 << log2_nbr_of_dct_partitions) - 1;
    // bits_vec_dump(v);

    uint8_t size[3 * MAX_PARTI_NUM];
    if (num) {
        fread(size, 3, num, f);
    }
    uint32_t next_part = ftell(f);
    for (int i = 0; i < num; i ++)
    {
        int partsize = size[i*3] | size[i*3 + 1] << 8 | size[i*3 + 2] << 16;
        w->p[i].start = next_part;
        w->p[i].len = partsize;

        fseek(f, partsize, SEEK_CUR);
        next_part = ftell(f);
    }
    w->p[num].start = next_part;
    fseek(f, 0, SEEK_END);
    w->p[num].len = ftell(f) - next_part; 
    w->k.nbr_partitions = num + 1;
}

static void
read_dequant_indice(int8_t *indice, struct bool_dec *br)
{
    if (BOOL_BIT(br)) {
        *indice = BOOL_BITS(br, 4);
        if (BOOL_BIT(br)) {
            *indice *= -1;
        }
    }
}

static void
read_dequantization(struct vp8_key_frame_header *kh, struct bool_dec *br)
{

    kh->quant_indice.y_ac_qi = BOOL_BITS(br, 7);

    read_dequant_indice(&kh->quant_indice.y_dc_delta, br);
    read_dequant_indice(&kh->quant_indice.y2_dc_delta, br);
    read_dequant_indice(&kh->quant_indice.y2_ac_delta, br);
    read_dequant_indice(&kh->quant_indice.uv_dc_delta, br);
    read_dequant_indice(&kh->quant_indice.uv_ac_delta, br);

}

static void
read_proba(struct vp8_key_frame_header *kh, struct bool_dec *br)
{
    // Paragraph 13
    static const uint8_t
        CoeffsUpdateProba[NUM_TYPES][NUM_BANDS][NUM_CTX][NUM_PROBAS] = {
    { { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 176, 246, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 223, 241, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 249, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 244, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 234, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 246, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 239, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 251, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 251, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 254, 253, 255, 254, 255, 255, 255, 255, 255, 255 },
        { 250, 255, 254, 255, 254, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        }
    },
    { { { 217, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 225, 252, 241, 253, 255, 255, 254, 255, 255, 255, 255 },
        { 234, 250, 241, 250, 253, 255, 253, 254, 255, 255, 255 }
        },
        { { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 223, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 238, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 248, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 249, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 247, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 250, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        }
    },
    { { { 186, 251, 250, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 234, 251, 244, 254, 255, 255, 255, 255, 255, 255, 255 },
        { 251, 251, 243, 253, 254, 255, 254, 255, 255, 255, 255 }
        },
        { { 255, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 236, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 251, 253, 253, 254, 254, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        }
    },
    { { { 248, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 250, 254, 252, 254, 255, 255, 255, 255, 255, 255, 255 },
        { 248, 254, 249, 253, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 246, 253, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 252, 254, 251, 254, 254, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 254, 252, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 248, 254, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 253, 255, 254, 254, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 245, 251, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 253, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 251, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 252, 253, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 252, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 249, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 254, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 253, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 250, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        },
        { { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 },
        { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 }
        }
    }
    };

    /*13.5*/
    static const uint8_t def_coeffsProba0[NUM_TYPES][NUM_BANDS][NUM_CTX][NUM_PROBAS] = 
    {
        {
            {
                { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 }
            },
            { 
                { 253, 136, 254, 255, 228, 219, 128, 128, 128, 128, 128 },
                { 189, 129, 242, 255, 227, 213, 255, 219, 128, 128, 128 },
                { 106, 126, 227, 252, 214, 209, 255, 255, 128, 128, 128 }
            },
            {
                { 1, 98, 248, 255, 236, 226, 255, 255, 128, 128, 128 },
                { 181, 133, 238, 254, 221, 234, 255, 154, 128, 128, 128 },
                { 78, 134, 202, 247, 198, 180, 255, 219, 128, 128, 128 },
            },
            {
                { 1, 185, 249, 255, 243, 255, 128, 128, 128, 128, 128 },
                { 184, 150, 247, 255, 236, 224, 128, 128, 128, 128, 128 },
                { 77, 110, 216, 255, 236, 230, 128, 128, 128, 128, 128 },
            },
            {
                { 1, 101, 251, 255, 241, 255, 128, 128, 128, 128, 128 },
                { 170, 139, 241, 252, 236, 209, 255, 255, 128, 128, 128 },
                { 37, 116, 196, 243, 228, 255, 255, 255, 128, 128, 128 }
            },
            {
                { 1, 204, 254, 255, 245, 255, 128, 128, 128, 128, 128 },
                { 207, 160, 250, 255, 238, 128, 128, 128, 128, 128, 128 },
                { 102, 103, 231, 255, 211, 171, 128, 128, 128, 128, 128 }
            },
            {
                { 1, 152, 252, 255, 240, 255, 128, 128, 128, 128, 128 },
                { 177, 135, 243, 255, 234, 225, 128, 128, 128, 128, 128 },
                { 80, 129, 211, 255, 194, 224, 128, 128, 128, 128, 128 }
            },
            {
                { 1, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 246, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 255, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 }
            }
        },
        {
            {
                { 198, 35, 237, 223, 193, 187, 162, 160, 145, 155, 62 },
                { 131, 45, 198, 221, 172, 176, 220, 157, 252, 221, 1 },
                { 68, 47, 146, 208, 149, 167, 221, 162, 255, 223, 128 }
            },
            {
                { 1, 149, 241, 255, 221, 224, 255, 255, 128, 128, 128 },
                { 184, 141, 234, 253, 222, 220, 255, 199, 128, 128, 128 },
                { 81, 99, 181, 242, 176, 190, 249, 202, 255, 255, 128 }
            },
            {
                { 1, 129, 232, 253, 214, 197, 242, 196, 255, 255, 128 },
                { 99, 121, 210, 250, 201, 198, 255, 202, 128, 128, 128 },
                { 23, 91, 163, 242, 170, 187, 247, 210, 255, 255, 128 }
            },
            {
                { 1, 200, 246, 255, 234, 255, 128, 128, 128, 128, 128 },
                { 109, 178, 241, 255, 231, 245, 255, 255, 128, 128, 128 },
                { 44, 130, 201, 253, 205, 192, 255, 255, 128, 128, 128 }
            },
            {
                { 1, 132, 239, 251, 219, 209, 255, 165, 128, 128, 128 },
                { 94, 136, 225, 251, 218, 190, 255, 255, 128, 128, 128 },
                { 22, 100, 174, 245, 186, 161, 255, 199, 128, 128, 128 }
            },
            {
                { 1, 182, 249, 255, 232, 235, 128, 128, 128, 128, 128 },
                { 124, 143, 241, 255, 227, 234, 128, 128, 128, 128, 128 },
                { 35, 77, 181, 251, 193, 211, 255, 205, 128, 128, 128 }
            },
            {
                { 1, 157, 247, 255, 236, 231, 255, 255, 128, 128, 128 },
                { 121, 141, 235, 255, 225, 227, 255, 255, 128, 128, 128 },
                { 45, 99, 188, 251, 195, 217, 255, 224, 128, 128, 128 }
            },
            {
                { 1, 1, 251, 255, 213, 255, 128, 128, 128, 128, 128 },
                { 203, 1, 248, 255, 255, 128, 128, 128, 128, 128, 128 },
                { 137, 1, 177, 255, 224, 255, 128, 128, 128, 128, 128 }
            }
        },
        {
            {
                { 253, 9, 248, 251, 207, 208, 255, 192, 128, 128, 128 },
                { 175, 13, 224, 243, 193, 185, 249, 198, 255, 255, 128 },
                { 73, 17, 171, 221, 161, 179, 236, 167, 255, 234, 128 }
            },
            {
                { 1, 95, 247, 253, 212, 183, 255, 255, 128, 128, 128 },
                { 239, 90, 244, 250, 211, 209, 255, 255, 128, 128, 128 },
                { 155, 77, 195, 248, 188, 195, 255, 255, 128, 128, 128 }
            },
            {
                { 1, 24, 239, 251, 218, 219, 255, 205, 128, 128, 128 },
                { 201, 51, 219, 255, 196, 186, 128, 128, 128, 128, 128 },
                { 69, 46, 190, 239, 201, 218, 255, 228, 128, 128, 128 }
            },
            {
                { 1, 191, 251, 255, 255, 128, 128, 128, 128, 128, 128 },
                { 223, 165, 249, 255, 213, 255, 128, 128, 128, 128, 128 },
                { 141, 124, 248, 255, 255, 128, 128, 128, 128, 128, 128 }
            },
            {
                { 1, 16, 248, 255, 255, 128, 128, 128, 128, 128, 128 },
                { 190, 36, 230, 255, 236, 255, 128, 128, 128, 128, 128 },
                { 149, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
            },
            {
                { 1, 226, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 247, 192, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 240, 128, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
            },
            {
                { 1, 134, 252, 255, 255, 128, 128, 128, 128, 128, 128 },
                { 213, 62, 250, 255, 255, 128, 128, 128, 128, 128, 128 },
                { 55, 93, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
            },
            {
                { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 }
            }
        },
        {
            {
                { 202, 24, 213, 235, 186, 191, 220, 160, 240, 175, 255 },
                { 126, 38, 182, 232, 169, 184, 228, 174, 255, 187, 128 },
                { 61, 46, 138, 219, 151, 178, 240, 170, 255, 216, 128 }
            },
            {
                { 1, 112, 230, 250, 199, 191, 247, 159, 255, 255, 128 },
                { 166, 109, 228, 252, 211, 215, 255, 174, 128, 128, 128 },
                { 39, 77, 162, 232, 172, 180, 245, 178, 255, 255, 128 }
            },
            {
                { 1, 52, 220, 246, 198, 199, 249, 220, 255, 255, 128 },
                { 124, 74, 191, 243, 183, 193, 250, 221, 255, 255, 128 },
                { 24, 71, 130, 219, 154, 170, 243, 182, 255, 255, 128 }
            },
            {
                { 1, 182, 225, 249, 219, 240, 255, 224, 128, 128, 128 },
                { 149, 150, 226, 252, 216, 205, 255, 171, 128, 128, 128 },
                { 28, 108, 170, 242, 183, 194, 254, 223, 255, 255, 128 }
            },
            {
                { 1, 81, 230, 252, 204, 203, 255, 192, 128, 128, 128 },
                { 123, 102, 209, 247, 188, 196, 255, 233, 128, 128, 128 },
                { 20, 95, 153, 243, 164, 173, 255, 203, 128, 128, 128 }
            },
            {
                { 1, 222, 248, 255, 216, 213, 128, 128, 128, 128, 128 },
                { 168, 175, 246, 252, 235, 205, 255, 255, 128, 128, 128 },
                { 47, 116, 215, 255, 211, 212, 255, 255, 128, 128, 128 }
            },
            {
                { 1, 121, 236, 253, 212, 214, 255, 255, 128, 128, 128 },
                { 141, 84, 213, 252, 201, 202, 255, 219, 128, 128, 128 },
                { 42, 80, 160, 240, 162, 185, 255, 205, 128, 128, 128 }
            },
            {
                { 1, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 244, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 },
                { 238, 1, 255, 128, 128, 128, 128, 128, 128, 128, 128 }
            }
        }
    };

    kh->refresh_entropy_probs = BOOL_BIT(br);
    /*if not keyframe 9.7, 9.8
    {
    |   refresh_golden_frame                            | L(1)  |
    |   refresh_alternate_frame                         | L(1)  |
    |   if (!refresh_golden_frame)                      |       |
    |     copy_buffer_to_golden                         | L(2)  |
    |   if (!refresh_alternate_frame)                   |       |
    |     copy_buffer_to_alternate                      | L(2)  |
    |   sign_bias_golden                                | L(1)  |
    |   sign_bias_alternate                             | L(1)  |
    |   refresh_entropy_probs                           | L(1)  |
    |   refresh_last                                    | L(1)  |
    | }   
   */

    /* DCT Coefficient Probability Update 9.9 */
    for (int i = 0; i < NUM_TYPES; i ++) {
        for (int j = 0; j < NUM_BANDS ; j ++) {
            for (int k = 0; k < NUM_CTX; k ++) {
                for (int l = 0; l < NUM_PROBAS; l ++) {
                    if (BOOL_DECODE(br, CoeffsUpdateProba[i][j][k][l])) {
                        kh->coeff_prob[i][j].probas[k][l] = BOOL_BITS(br, 8);
                    } else {
                        kh->coeff_prob[i][j].probas[k][l] = def_coeffsProba0[i][j][k][l];
                    }
                }
            }
        }
    }

}

#include "bitstream.h"
static void
read_vp8_ctl_partition(WEBP *w, struct bool_dec *br, FILE *f)
{
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;

    w->k.cs_and_clamp.color_space = BOOL_BIT(br);
    w->k.cs_and_clamp.clamp = BOOL_BIT(br);
    VDBG(webp, "cs %d, clamp %d", w->k.cs_and_clamp.color_space, w->k.cs_and_clamp.clamp);
    
    /*READ Segment-Based Adjustments 9.3 */
    read_vp8_segmentation_adjust(&w->k.segmentation, br);

    /*Loop Filter Type and Levels 9.4*/
    w->k.filter_type = BOOL_BIT(br);
    w->k.loop_filter_level = BOOL_BITS(br, 6);
    w->k.sharpness_level = BOOL_BITS(br, 3);

    /* READ adjustments */
    read_mb_lf_adjustments(&w->k.mb_lf_adjustments, br);

    /*Token Partition and Partition Data Offsets 9.5*/

    read_token_partition(w, br, f);

    /* READ Dequantization Indices 9.6 */
    read_dequantization(&w->k, br);

    read_proba(&w->k ,br);

    /* 9.11 */
    w->k.mb_no_skip_coeff = BOOL_BIT(br);
    if (w->k.mb_no_skip_coeff) {
        w->k.prob_skip_false = BOOL_BITS(br, 8);
    } else {
        w->k.prob_skip_false = 0;
    }
}

/* Residual decoding (Paragraph 13.2 / 13.3) */
typedef enum
{
    DCT_0,      /* value 0 */
    DCT_1,      /* 1 */
    DCT_2,      /* 2 */
    DCT_3,      /* 3 */
    DCT_4,      /* 4 */
    dct_cat1,   /* range 5 - 6  (size 2) */
    dct_cat2,   /* 7 - 10   (4) */
    dct_cat3,   /* 11 - 18  (8) */
    dct_cat4,   /* 19 - 34  (16) */
    dct_cat5,   /* 35 - 66  (32) */
    dct_cat6,   /* 67 - 2048  (1982) */
    dct_eob,    /* end of block */

    num_dct_tokens   /* 12 */
} dct_token;
/* pCatn secifyranges of unsigned values whose width is 
1, 2, 3, 4, 5, or 11 bits, respectively.  */
static const uint8_t pCat1[] = { 159, 0};
static const uint8_t pCat2[] = { 165, 145, 0};
static const uint8_t pCat3[] = { 173, 148, 140, 0 };
static const uint8_t pCat4[] = { 176, 155, 140, 135, 0 };
static const uint8_t pCat5[] = { 180, 157, 141, 134, 130, 0 };
static const uint8_t pCat6[] =
  { 254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0 };

static const uint8_t* const pCat3456[] = { pCat3, pCat4, pCat5, pCat6 };

static const uint8_t kZigzag[16] = {
  0, 1, 4, 8,  5, 2, 3, 6,  9, 12, 13, 10,  7, 11, 14, 15
};

const int coeff_bands [16] = 
    {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7 };


// See section 13-2: https://datatracker.ietf.org/doc/html/rfc6386#section-13.2
static int
get_large_value(struct bool_dec *bt, const uint8_t* const p)
{
    int v;
    if (!BOOL_DECODE(bt, p[3])) {
        if (!BOOL_DECODE(bt, p[4])) {
            v = 2;
        } else {
            v = 3 + BOOL_DECODE(bt, p[5]);
        }
    } else {
    if (!BOOL_DECODE(bt, p[6])) {
        if (!BOOL_DECODE(bt, p[7])) {
            v = 5 + BOOL_DECODE(bt, 159);
        } else {
            v = 7 + 2 * BOOL_DECODE(bt, 165);
            v += BOOL_DECODE(bt, 145);
        }
    } else {
        const uint8_t* tab;
        const int bit1 = BOOL_DECODE(bt, p[8]);
        const int bit0 = BOOL_DECODE(bt, p[9 + bit1]);
        const int cat = 2 * bit1 + bit0;
        v = 0;
        for (tab = pCat3456[cat]; *tab; ++tab) {
            v += v + BOOL_DECODE(bt, *tab);
        }
            v += 3 + (8 << cat);
        }
    }
    return v;
}


/* Returns the position of the last non-zero coeff plus one */
static int
vp8_get_coeff_fast(struct bool_dec *bt, int16_t *out,
                    const VP8BandProbas * const bands[], int n, int ctx, uint8_t *dq)
{
    const uint8_t* p = bands[n]->probas[ctx];
    for (; n < 16; ++n) {
        if (!BOOL_DECODE(bt, p[0])) {
            return n;  // previous coeff was last non-zero coeff
        }
        while (!BOOL_DECODE(bt, p[1])) {       // sequence of zero coeffs
            p = bands[++n]->probas[0];
            if (n == 16)
                return 16;
        }
        // non zero coeff
        int v;
        if (!BOOL_DECODE(bt, p[2])) {
            v = 1;
            p = bands[n+1]->probas[1];
        } else {
            v = get_large_value(bt, p);
            p = bands[n+1]->probas[2];
        }
        out[kZigzag[n]] = BOOL_SIGNED(bt, v) * dq[n > 0];
    
    }
    return 16;
}

static int
vp8_get_coeff_alt(struct bool_dec *bt, uint8_t *out, 
                    uint8_t ***coeff_prob, int i, int ctx)
{
    const uint8_t* p = coeff_prob[i][ctx];
    for (; i < 16; i ++) {
        if (!BOOL_DECODE_ALT(bt, p[0])) {
            return i;
        }
        while (!BOOL_DECODE_ALT(bt, p[1])) {
            p = coeff_prob[++i][0];
            if (i == 16)
                return 16;
        }
        int v;
        uint8_t **p_ctx = coeff_prob[i+1];
        if (!BOOL_DECODE_ALT(bt, p[2])) {
            v = 1;
            p = p_ctx[1];
        } else {
            v = get_large_value(bt, p);
            p = p_ctx[2];

        }
        out[kZigzag[i]] = BOOL_DECODE_ALT(bt, 0x80);
    }
    return 0;
}

// Paragraph 11.5
static const uint8_t kf_bmode_prob[NUM_BMODES][NUM_BMODES][NUM_BMODES - 1] = {
    {
        { 231, 120, 48, 89, 115, 113, 120, 152, 112 },
        { 152, 179, 64, 126, 170, 118, 46, 70, 95 },
        { 175, 69, 143, 80, 85, 82, 72, 155, 103 },
        { 56, 58, 10, 171, 218, 189, 17, 13, 152 },
        { 114, 26, 17, 163, 44, 195, 21, 10, 173 },
        { 121, 24, 80, 195, 26, 62, 44, 64, 85 },
        { 144, 71, 10, 38, 171, 213, 144, 34, 26 },
        { 170, 46, 55, 19, 136, 160, 33, 206, 71 },
        { 63, 20, 8, 114, 114, 208, 12, 9, 226 },
        { 81, 40, 11, 96, 182, 84, 29, 16, 36 }
    },
    {
        { 134, 183, 89, 137, 98, 101, 106, 165, 148 },
        { 72, 187, 100, 130, 157, 111, 32, 75, 80 },
        { 66, 102, 167, 99, 74, 62, 40, 234, 128 },
        { 41, 53, 9, 178, 241, 141, 26, 8, 107 },
        { 74, 43, 26, 146, 73, 166, 49, 23, 157 },
        { 65, 38, 105, 160, 51, 52, 31, 115, 128 },
        { 104, 79, 12, 27, 217, 255, 87, 17, 7 },
        { 87, 68, 71, 44, 114, 51, 15, 186, 23 },
        { 47, 41, 14, 110, 182, 183, 21, 17, 194 },
        { 66, 45, 25, 102, 197, 189, 23, 18, 22 }
    },
    {
        { 88, 88, 147, 150, 42, 46, 45, 196, 205 },
        { 43, 97, 183, 117, 85, 38, 35, 179, 61 },
        { 39, 53, 200, 87, 26, 21, 43, 232, 171 },
        { 56, 34, 51, 104, 114, 102, 29, 93, 77 },
        { 39, 28, 85, 171, 58, 165, 90, 98, 64 },
        { 34, 22, 116, 206, 23, 34, 43, 166, 73 },
        { 107, 54, 32, 26, 51, 1, 81, 43, 31 },
        { 68, 25, 106, 22, 64, 171, 36, 225, 114 },
        { 34, 19, 21, 102, 132, 188, 16, 76, 124 },
        { 62, 18, 78, 95, 85, 57, 50, 48, 51 }
    },
    {
        { 193, 101, 35, 159, 215, 111, 89, 46, 111 },
        { 60, 148, 31, 172, 219, 228, 21, 18, 111 },
        { 112, 113, 77, 85, 179, 255, 38, 120, 114 },
        { 40, 42, 1, 196, 245, 209, 10, 25, 109 },
        { 88, 43, 29, 140, 166, 213, 37, 43, 154 },
        { 61, 63, 30, 155, 67, 45, 68, 1, 209 },
        { 100, 80, 8, 43, 154, 1, 51, 26, 71 },
        { 142, 78, 78, 16, 255, 128, 34, 197, 171 },
        { 41, 40, 5, 102, 211, 183, 4, 1, 221 },
        { 51, 50, 17, 168, 209, 192, 23, 25, 82 }
    },
    {
        { 138, 31, 36, 171, 27, 166, 38, 44, 229 },
        { 67, 87, 58, 169, 82, 115, 26, 59, 179 },
        { 63, 59, 90, 180, 59, 166, 93, 73, 154 },
        { 40, 40, 21, 116, 143, 209, 34, 39, 175 },
        { 47, 15, 16, 183, 34, 223, 49, 45, 183 },
        { 46, 17, 33, 183, 6, 98, 15, 32, 183 },
        { 57, 46, 22, 24, 128, 1, 54, 17, 37 },
        { 65, 32, 73, 115, 28, 128, 23, 128, 205 },
        { 40, 3, 9, 115, 51, 192, 18, 6, 223 },
        { 87, 37, 9, 115, 59, 77, 64, 21, 47 }
    },
    {
        { 104, 55, 44, 218, 9, 54, 53, 130, 226 },
        { 64, 90, 70, 205, 40, 41, 23, 26, 57 },
        { 54, 57, 112, 184, 5, 41, 38, 166, 213 },
        { 30, 34, 26, 133, 152, 116, 10, 32, 134 },
        { 39, 19, 53, 221, 26, 114, 32, 73, 255 },
        { 31, 9, 65, 234, 2, 15, 1, 118, 73 },
        { 75, 32, 12, 51, 192, 255, 160, 43, 51 },
        { 88, 31, 35, 67, 102, 85, 55, 186, 85 },
        { 56, 21, 23, 111, 59, 205, 45, 37, 192 },
        { 55, 38, 70, 124, 73, 102, 1, 34, 98 }
    },
    {
        { 125, 98, 42, 88, 104, 85, 117, 175, 82 },
        { 95, 84, 53, 89, 128, 100, 113, 101, 45 },
        { 75, 79, 123, 47, 51, 128, 81, 171, 1 },
        { 57, 17, 5, 71, 102, 57, 53, 41, 49 },
        { 38, 33, 13, 121, 57, 73, 26, 1, 85 },
        { 41, 10, 67, 138, 77, 110, 90, 47, 114 },
        { 115, 21, 2, 10, 102, 255, 166, 23, 6 },
        { 101, 29, 16, 10, 85, 128, 101, 196, 26 },
        { 57, 18, 10, 102, 102, 213, 34, 20, 43 },
        { 117, 20, 15, 36, 163, 128, 68, 1, 26 }
    },
    {
        { 102, 61, 71, 37, 34, 53, 31, 243, 192 },
        { 69, 60, 71, 38, 73, 119, 28, 222, 37 },
        { 68, 45, 128, 34, 1, 47, 11, 245, 171 },
        { 62, 17, 19, 70, 146, 85, 55, 62, 70 },
        { 37, 43, 37, 154, 100, 163, 85, 160, 1 },
        { 63, 9, 92, 136, 28, 64, 32, 201, 85 },
        { 75, 15, 9, 9, 64, 255, 184, 119, 16 },
        { 86, 6, 28, 5, 64, 255, 25, 248, 1 },
        { 56, 8, 17, 132, 137, 255, 55, 116, 128 },
        { 58, 15, 20, 82, 135, 57, 26, 121, 40 }
    },
    {
        { 164, 50, 31, 137, 154, 133, 25, 35, 218 },
        { 51, 103, 44, 131, 131, 123, 31, 6, 158 },
        { 86, 40, 64, 135, 148, 224, 45, 183, 128 },
        { 22, 26, 17, 131, 240, 154, 14, 1, 209 },
        { 45, 16, 21, 91, 64, 222, 7, 1, 197 },
        { 56, 21, 39, 155, 60, 138, 23, 102, 213 },
        { 83, 12, 13, 54, 192, 255, 68, 47, 28 },
        { 85, 26, 85, 85, 128, 128, 32, 146, 171 },
        { 18, 11, 7, 63, 144, 171, 4, 4, 246 },
        { 35, 27, 10, 146, 174, 171, 12, 26, 128 }
    },
    {
        { 190, 80, 35, 99, 180, 80, 126, 54, 45 },
        { 85, 126, 47, 87, 176, 51, 41, 20, 32 },
        { 101, 75, 128, 139, 118, 146, 116, 128, 85 },
        { 56, 41, 15, 176, 236, 85, 37, 9, 62 },
        { 71, 30, 17, 119, 118, 255, 17, 18, 138 },
        { 101, 38, 60, 138, 55, 70, 43, 26, 142 },
        { 146, 36, 19, 30, 171, 255, 97, 27, 20 },
        { 138, 45, 61, 62, 219, 1, 81, 188, 64 },
        { 32, 41, 20, 117, 151, 142, 20, 21, 163 },
        { 112, 19, 12, 61, 195, 128, 48, 4, 24 }
    }
};

static void
transformWHT_C(const int16_t* in, int16_t* out)
{
    int tmp[16];
    int i;
    for (i = 0; i < 4; ++i) {
        const int a0 = in[0 + i] + in[12 + i];
        const int a1 = in[4 + i] + in[ 8 + i];
        const int a2 = in[4 + i] - in[ 8 + i];
        const int a3 = in[0 + i] - in[12 + i];
        tmp[0  + i] = a0 + a1;
        tmp[8  + i] = a0 - a1;
        tmp[4  + i] = a3 + a2;
        tmp[12 + i] = a3 - a2;
    }
    for (i = 0; i < 4; ++i) {
        const int dc = tmp[0 + i * 4] + 3;    // w/ rounder
        const int a0 = dc             + tmp[3 + i * 4];
        const int a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
        const int a2 = tmp[1 + i * 4] - tmp[2 + i * 4];
        const int a3 = dc             - tmp[3 + i * 4];
        out[ 0] = (a0 + a1) >> 3;
        out[16] = (a3 + a2) >> 3;
        out[32] = (a0 - a1) >> 3;
        out[48] = (a3 - a2) >> 3;
        out += 64;
    }
}

static inline uint32_t
NzCodeBits(uint32_t nz_coeffs, int nz, int dc_nz)
{
    nz_coeffs <<= 2;
    nz_coeffs |= (nz > 3) ? 3 : (nz > 1) ? 2 : dc_nz;
    return nz_coeffs;
}


static int
vp8_residuals(WEBP *w, struct vp8mb *left_mb, struct vp8mb *mb, struct macro_block *block, bool_dec *bt, struct quant *qt)
{
    static const uint8_t kBands[16 + 1] = {
        0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7,
        0  // extra entry as sentinel
    };
    const VP8BandProbas *bands[NUM_TYPES][16 + 1];
    int16_t* dst = block->coeffs;
    memset(dst, 0, 384 * sizeof(*dst));
    // struct macro_block *left_mb = mb - 1;
    uint8_t tnz, lnz;
    uint32_t non_zero_y = 0;
    uint32_t non_zero_uv = 0;
    int x, y, ch;
    uint32_t out_t_nz, out_l_nz;
    int first;
    const VP8BandProbas* const * ac_proba;

    for (int t = 0; t < NUM_TYPES; ++t) {
        for (int b = 0; b < 16 + 1; ++b) {
            bands[t][b] = &w->k.coeff_prob[t][kBands[b]];
        }
    }
    if (!block->is_i4x4) {
        int16_t dc[16] = {0};
        const int ctx = mb->nz_dc + left_mb->nz_dc;
        const int nz = vp8_get_coeff_fast(bt, dc, bands[1], 0, ctx, qt->dqm_y2[block->segment]);
        mb->nz_dc = left_mb->nz_dc = (nz > 0);
        printf("nz %d\n", nz);
        if (nz > 1) {   // more than just the DC -> perform the full transform
            transformWHT_C(dc, dst);
        } else {        // only DC is non-zero -> inlined simplified transform
            const int dc0 = (dc[0] + 3) >> 3;
            for (int i = 0; i < 16 * 16; i += 16) {
                dst[i] = dc0;
            }
        }
        first = 1;
        ac_proba = bands[0];
    } else {
        first = 0;
        ac_proba = bands[3];
    }

    tnz = mb->nz & 0x0f;
    lnz = left_mb->nz & 0x0f;
    for (y = 0; y < 4; ++y) {
        int l = lnz & 1;
        uint32_t nz_coeffs = 0;
        for (x = 0; x < 4; ++x) {
            const int ctx = l + (tnz & 1);
            // printf("tnz %d, lnz %d, ctx %d\n", tnz, lnz, ctx);
            const int nz = vp8_get_coeff_fast(bt, dst, ac_proba, first, ctx, qt->dqm_y1[block->segment]);
            printf("1 %d nz %d\n", block->segment, nz);
            l = (nz > first);
            tnz = (tnz >> 1) | (l << 7);
            nz_coeffs = NzCodeBits(nz_coeffs, nz, dst[0] != 0);
            dst += 16;
        }
        tnz >>= 4;
        lnz = (lnz >> 1) | (l << 7);
        non_zero_y = (non_zero_y << 8) | nz_coeffs;
    }
    out_t_nz = tnz;
    out_l_nz = lnz >> 4;

    for (ch = 0; ch < 4; ch += 2) {
        uint32_t nz_coeffs = 0;
        tnz = mb->nz >> (4 + ch);
        lnz = left_mb->nz >> (4 + ch);
        for (y = 0; y < 2; ++y) {
            int l = lnz & 1;
            for (x = 0; x < 2; ++x) {
                const int ctx = l + (tnz & 1);
                const int nz = vp8_get_coeff_fast(bt, dst, bands[2], 0, ctx, qt->dqm_uv[block->segment]);
                printf("2 nz %d\n", nz);
                l = (nz > 0);
                tnz = (tnz >> 1) | (l << 3);
                nz_coeffs = NzCodeBits(nz_coeffs, nz, dst[0] != 0);
                dst += 16;
            }
            tnz >>= 2;
            lnz = (lnz >> 1) | (l << 5);
        }
        // Note: we don't really need the per-4x4 details for U/V blocks.
        non_zero_uv |= nz_coeffs << (4 * ch);
        out_t_nz |= (tnz << 4) << ch;
        out_l_nz |= (lnz & 0xf0) << ch;
    }
    mb->nz = out_t_nz;
    left_mb->nz = out_l_nz;

    block->non_zero_y = non_zero_y;
    block->non_zero_uv = non_zero_uv;

    return 0;
}


typedef struct {  // filter specs
  uint8_t f_limit;      // filter limit in [3..189], or 0 if no filtering
  uint8_t f_ilevel;     // inner limit in [1..63]
  uint8_t f_inner;      // do inner filtering?
  uint8_t hev_thresh;   // high edge variance threshold in [0..2]
} VP8FInfo;

static VP8FInfo fstrengths[NUM_MB_SEGMENTS][2];

static int 
vp8_decode_MB(WEBP *w, struct vp8mb *left, struct vp8mb *mb, struct macro_block *block, bool_dec *bt,
             struct quant *qt, VP8FInfo *finfo)
{
    int filter_type = (w->k.loop_filter_level == 0) ? WEBP_FILTER_NONE :
        w->k.filter_type ? WEBP_FILTER_SIMPLE : WEBP_FILTER_NORMAL;
    // 0=off, 1=simple, 2=complex
    // struct macro_block *left_mb = mb - 1;

    int skip = w->k.mb_no_skip_coeff ? block->skip : 0;
    if (!skip) {
        skip = vp8_residuals(w, left, mb, block, bt, qt);
    } else {
        left->nz = mb->nz = 0;
        if (!block->is_i4x4) {
            left->nz_dc = mb->nz_dc = 0;
        }
        block->non_zero_y = 0;
        block->non_zero_uv = 0;
        block->dither = 0;
    }
    if (filter_type) {
        *finfo = fstrengths[block->segment][block->is_i4x4];
        finfo->f_inner |= !skip;
    }
    return 0;
}
static const int8_t kYModesIntra4[18] = {
    -B_DC_PRED, 1,
        -B_TM_PRED, 2,
            -B_VE_PRED, 3,
                4, 6,
                    -B_HE_PRED, 5,
                        -B_RD_PRED, -B_VR_PRED,
                -B_LD_PRED, 7,
                    -B_VL_PRED, 8,
                        -B_HD_PRED, -B_HU_PRED
};


void
vp8_intramode(WEBP *w, bool_dec *bt, struct macro_block *mbs)
{
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int intra_size = (width + 15) >> 2;
    uint8_t left[4], *tops, *top;
    tops = malloc(intra_size);
    struct macro_block *mb;

    for (int y = 0; y < (height + 15) >> 4; y ++) {
        memset(left, 0, 4 * sizeof(uint8_t));
        for (int x = 0; x < (width + 15) >> 4; x ++) {
            mb = mbs + y *((width + 15) >> 4) + x;
            top = tops + 4 * x;
            if (w->k.segmentation.update_mb_segmentation_map) {
                mb->segment = !BOOL_DECODE(bt, w->k.segmentation.segment_prob[0]) ?
                    BOOL_DECODE(bt, w->k.segmentation.segment_prob[1]) :
                    BOOL_DECODE(bt, w->k.segmentation.segment_prob[2]) + 2;
            } else {
                mb->segment = 0;
            }
            // printf("%x, %ld\n", mb->segment, bt->bits->len - (bt->bits->ptr - bt->bits->start));
            if (w->k.mb_no_skip_coeff) {
                mb->skip = BOOL_DECODE(bt, w->k.prob_skip_false);
            }
            mb->is_i4x4 = !BOOL_DECODE(bt, 145);
            // printf("y %d, x %d, is 4x4 %d\n", y, x, mb->is_i4x4);
            if (!mb->is_i4x4) {
                const int ymode = BOOL_DECODE(bt, 156) ?
                    (BOOL_DECODE(bt, 128) ? TM_PRED : H_PRED) :
                    (BOOL_DECODE(bt, 163) ? V_PRED : DC_PRED);
                mb->imodes[0] = ymode;
                memset(top, ymode, 4 * sizeof(*top));
                memset(left, ymode, 4 * sizeof(*left));
            } else {
                uint8_t* modes = mb->imodes;
                for (int i = 0; i < 4; i++) {
                    int ymode = left[i];
                    for (int j = 0; j < 4; j ++) {
                        const uint8_t* const prob = kf_bmode_prob[top[j]][ymode];
                        // printf("top %d, ymode %d, prob %d\n", top[j], ymode, prob[0]);
                        int a = kYModesIntra4[BOOL_DECODE(bt, prob[0])];
                        while (a > 0) {
                            a = kYModesIntra4[2 * a + BOOL_DECODE(bt, prob[a])];
                        }
                        ymode = -a;
                        top[j] = ymode;
                    }
                    memcpy(modes, top, 4 *sizeof(*top));
                    modes += 4;
                    left[i] = ymode;
                }
            }
            mb->uvmode = !BOOL_DECODE(bt, 142) ? DC_PRED
                        : !BOOL_DECODE(bt, 114) ? V_PRED
                        : BOOL_DECODE(bt, 183) ? TM_PRED : H_PRED;
        }
    }
    free(tops);
}



// Paragraph 14.1
static const uint8_t kDcTable[128] = {
    4,     5,   6,   7,   8,   9,  10,  10,
    11,   12,  13,  14,  15,  16,  17,  17,
    18,   19,  20,  20,  21,  21,  22,  22,
    23,   23,  24,  25,  25,  26,  27,  28,
    29,   30,  31,  32,  33,  34,  35,  36,
    37,   37,  38,  39,  40,  41,  42,  43,
    44,   45,  46,  46,  47,  48,  49,  50,
    51,   52,  53,  54,  55,  56,  57,  58,
    59,   60,  61,  62,  63,  64,  65,  66,
    67,   68,  69,  70,  71,  72,  73,  74,
    75,   76,  76,  77,  78,  79,  80,  81,
    82,   83,  84,  85,  86,  87,  88,  89,
    91,   93,  95,  96,  98, 100, 101, 102,
    104, 106, 108, 110, 112, 114, 116, 118,
    122, 124, 126, 128, 130, 132, 134, 136,
    138, 140, 143, 145, 148, 151, 154, 157
};

static const uint16_t kAcTable[128] = {
    4,     5,   6,   7,   8,   9,  10,  11,
    12,   13,  14,  15,  16,  17,  18,  19,
    20,   21,  22,  23,  24,  25,  26,  27,
    28,   29,  30,  31,  32,  33,  34,  35,
    36,   37,  38,  39,  40,  41,  42,  43,
    44,   45,  46,  47,  48,  49,  50,  51,
    52,   53,  54,  55,  56,  57,  58,  60,
    62,   64,  66,  68,  70,  72,  74,  76,
    78,   80,  82,  84,  86,  88,  90,  92,
    94,   96,  98, 100, 102, 104, 106, 108,
    110, 112, 114, 116, 119, 122, 125, 128,
    131, 134, 137, 140, 143, 146, 149, 152,
    155, 158, 161, 164, 167, 170, 173, 177,
    181, 185, 189, 193, 197, 201, 205, 209,
    213, 217, 221, 225, 229, 234, 239, 245,
    249, 254, 259, 264, 269, 274, 279, 284
};



#define BPS      (32)
#define YUV_SIZE (BPS * 17 + BPS * 9)
#define Y_OFF    (BPS * 1 + 8)
#define U_OFF    (Y_OFF + BPS * 16 + BPS)
#define V_OFF    (U_OFF + 16)

//------------------------------------------------------------------------------
// Transforms (Paragraph 14.4)
static inline uint8_t clip_8b(int v) {
    return (!(v & ~0xff)) ? v : (v < 0) ? 0 : 255;
}

#define STORE(x, y, v) \
    dst[(x) + (y) * BPS] = clip_8b(dst[(x) + (y) * BPS] + ((v) >> 3))

#define STORE2(y, dc, d, c) do {      \
    const int DC = (dc);              \
    STORE(0, y, DC + (d));            \
    STORE(1, y, DC + (c));            \
    STORE(2, y, DC - (c));            \
    STORE(3, y, DC - (d));            \
} while (0)

#define MUL1(a) ((((a) * 20091) >> 16) + (a))
#define MUL2(a) (((a) * 35468) >> 16)

static inline void
TrueMotion(uint8_t* dst, int size)
{
    const uint8_t* top = dst - BPS;
    const uint8_t* const clip0 = VP8kclip1 - top[-1];

    for (int y = 0; y < size; ++y) {
        const uint8_t* const clip = clip0 + dst[-1];
        int x;
        for (x = 0; x < size; ++x) {
            dst[x] = clip[top[x]];
        }
        dst += BPS;
    }
}

static void TransformOne_C(const int16_t* in, uint8_t* dst) {
    int C[4 * 4], *tmp;
    int i;
    tmp = C;
    for (i = 0; i < 4; ++i) {    // vertical pass
        const int a = in[0] + in[8];    // [-4096, 4094]
        const int b = in[0] - in[8];    // [-4095, 4095]
        const int c = MUL2(in[4]) - MUL1(in[12]);   // [-3783, 3783]
        const int d = MUL1(in[4]) + MUL2(in[12]);   // [-3785, 3781]
        tmp[0] = a + d;   // [-7881, 7875]
        tmp[1] = b + c;   // [-7878, 7878]
        tmp[2] = b - c;   // [-7878, 7878]
        tmp[3] = a - d;   // [-7877, 7879]
        tmp += 4;
        in++;
    }
    // Each pass is expanding the dynamic range by ~3.85 (upper bound).
    // The exact value is (2. + (20091 + 35468) / 65536).
    // After the second pass, maximum interval is [-3794, 3794], assuming
    // an input in [-2048, 2047] interval. We then need to add a dst value
    // in the [0, 255] range.
    // In the worst case scenario, the input to clip_8b() can be as large as
    // [-60713, 60968].
    tmp = C;
    for (i = 0; i < 4; ++i) {    // horizontal pass
        const int dc = tmp[0] + 4;
        const int a =  dc +  tmp[8];
        const int b =  dc -  tmp[8];
        const int c = MUL2(tmp[4]) - MUL1(tmp[12]);
        const int d = MUL1(tmp[4]) + MUL2(tmp[12]);
        STORE(0, 0, a + d);
        STORE(1, 0, b + c);
        STORE(2, 0, b - c);
        STORE(3, 0, a - d);
        tmp++;
        dst += BPS;
    }
}

// Simplified transform when only in[0], in[1] and in[4] are non-zero
static void
TransformAC3_C(const int16_t* in, uint8_t* dst)
{
    const int a = in[0] + 4;
    const int c4 = MUL2(in[4]);
    const int d4 = MUL1(in[4]);
    const int c1 = MUL2(in[1]);
    const int d1 = MUL1(in[1]);
    STORE2(0, a + d4, d1, c1);
    STORE2(1, a + c4, d1, c1);
    STORE2(2, a - c4, d1, c1);
    STORE2(3, a - d4, d1, c1);
}

#undef MUL1
#undef MUL2
#undef STORE2

static void 
TransformTwo_C(const int16_t* in, uint8_t* dst, int do_two)
{
    TransformOne_C(in, dst);
    if (do_two) {
        TransformOne_C(in + 16, dst + 4);
    }
}

static void
TransformUV_C(const int16_t* in, uint8_t* dst)
{
    TransformTwo_C(in + 0 * 16, dst, 1);
    TransformTwo_C(in + 2 * 16, dst + 4 * BPS, 1);
}

static void
TransformDC_C(const int16_t* in, uint8_t* dst)
{
    const int DC = in[0] + 4;
    int i, j;
    for (j = 0; j < 4; ++j) {
        for (i = 0; i < 4; ++i) {
            STORE(i, j, DC);
        }
    }
}

static void
TransformDCUV_C(const int16_t* in, uint8_t* dst)
{
    if (in[0 * 16]) TransformDC_C(in + 0 * 16, dst);
    if (in[1 * 16]) TransformDC_C(in + 1 * 16, dst + 4);
    if (in[2 * 16]) TransformDC_C(in + 2 * 16, dst + 4 * BPS);
    if (in[3 * 16]) TransformDC_C(in + 3 * 16, dst + 4 * BPS + 4);
}


//------------------------------------------------------------------------------
// Main reconstruction function.

static const uint16_t kScan[16] = {
  0 +  0 * BPS,  4 +  0 * BPS, 8 +  0 * BPS, 12 +  0 * BPS,
  0 +  4 * BPS,  4 +  4 * BPS, 8 +  4 * BPS, 12 +  4 * BPS,
  0 +  8 * BPS,  4 +  8 * BPS, 8 +  8 * BPS, 12 +  8 * BPS,
  0 + 12 * BPS,  4 + 12 * BPS, 8 + 12 * BPS, 12 + 12 * BPS
};

static int
CheckMode(int mb_x, int mb_y, int mode)
{
    if (mode == B_DC_PRED) {
        if (mb_x == 0) {
            return (mb_y == 0) ? B_DC_PRED_NOTOPLEFT : B_DC_PRED_NOLEFT;
        } else {
            return (mb_y == 0) ? B_DC_PRED_NOTOP : B_DC_PRED;
        }
    }
    return mode;
}

static inline void
DoTransform(uint32_t bits, const int16_t* const src,
                                uint8_t* const dst)
{
    switch (bits >> 30) {
    case 3:
        TransformTwo_C(src, dst, 0);
        break;
    case 2:
        TransformAC3_C(src, dst);
        break;
    case 1:
        TransformDC_C(src, dst);
        break;
    default:
        break;
    }
}

static void
DoUVTransform(uint32_t bits, const int16_t* const src,
                            uint8_t* const dst)
{
    if (bits & 0xff) {    // any non-zero coeff at all?
        if (bits & 0xaa) {  // any non-zero AC coefficient?
            TransformUV_C(src, dst);   // note we don't use the AC3 variant for U/V
        } else {
            TransformDCUV_C(src, dst);
        }
    }
}

#define DST(x, y) dst[(x) + (y) * BPS]
#define AVG3(a, b, c) ((uint8_t)(((a) + 2 * (b) + (c) + 2) >> 2))
#define AVG2(a, b) (((a) + (b) + 1) >> 1)

static void VE4_C(uint8_t* dst)
{    // vertical
    const uint8_t* top = dst - BPS;
    const uint8_t vals[4] = {
        AVG3(top[-1], top[0], top[1]),
        AVG3(top[ 0], top[1], top[2]),
        AVG3(top[ 1], top[2], top[3]),
        AVG3(top[ 2], top[3], top[4])
    };
    int i;
    for (i = 0; i < 4; ++i) {
        memcpy(dst + i * BPS, vals, sizeof(vals));
    }
}

static void
HE4_C(uint8_t* dst)
{    // horizontal
    const int A = dst[-1 - BPS];
    const int B = dst[-1];
    const int C = dst[-1 + BPS];
    const int D = dst[-1 + 2 * BPS];
    const int E = dst[-1 + 3 * BPS];
    uint32_t sr = (uint32_t)0x01010101U * AVG3(A, B, C);
    memcpy(dst + 0 * BPS, &sr, 4);
    sr = (uint32_t)0x01010101U * AVG3(B, C, D);
    memcpy(dst + 1 * BPS, &sr, 4);
    sr = (uint32_t)0x01010101U * AVG3(C, D, E);
    memcpy(dst + 2 * BPS, &sr, 4);
    sr = (uint32_t)0x01010101U * AVG3(D, E, E);
    memcpy(dst + 3 * BPS, &sr, 4);
}

static void
DC4_C(uint8_t* dst)
{   // DC
    uint32_t dc = 4;
    int i;
    for (i = 0; i < 4; ++i) dc += dst[i - BPS] + dst[-1 + i * BPS];
    dc >>= 3;
    for (i = 0; i < 4; ++i) memset(dst + i * BPS, dc, 4);
}

static void
RD4_C(uint8_t* dst)
{   // Down-right
    const int I = dst[-1 + 0 * BPS];
    const int J = dst[-1 + 1 * BPS];
    const int K = dst[-1 + 2 * BPS];
    const int L = dst[-1 + 3 * BPS];
    const int X = dst[-1 - BPS];
    const int A = dst[0 - BPS];
    const int B = dst[1 - BPS];
    const int C = dst[2 - BPS];
    const int D = dst[3 - BPS];
    DST(0, 3)                                     = AVG3(J, K, L);
    DST(1, 3) = DST(0, 2)                         = AVG3(I, J, K);
    DST(2, 3) = DST(1, 2) = DST(0, 1)             = AVG3(X, I, J);
    DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) = AVG3(A, X, I);
                DST(3, 2) = DST(2, 1) = DST(1, 0) = AVG3(B, A, X);
                            DST(3, 1) = DST(2, 0) = AVG3(C, B, A);
                                        DST(3, 0) = AVG3(D, C, B);
}

static void
LD4_C(uint8_t* dst)
{   // Down-Left
    const int A = dst[0 - BPS];
    const int B = dst[1 - BPS];
    const int C = dst[2 - BPS];
    const int D = dst[3 - BPS];
    const int E = dst[4 - BPS];
    const int F = dst[5 - BPS];
    const int G = dst[6 - BPS];
    const int H = dst[7 - BPS];
    DST(0, 0)                                     = AVG3(A, B, C);
    DST(1, 0) = DST(0, 1)                         = AVG3(B, C, D);
    DST(2, 0) = DST(1, 1) = DST(0, 2)             = AVG3(C, D, E);
    DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3(D, E, F);
                DST(3, 1) = DST(2, 2) = DST(1, 3) = AVG3(E, F, G);
                            DST(3, 2) = DST(2, 3) = AVG3(F, G, H);
                                        DST(3, 3) = AVG3(G, H, H);
}


static void
VR4_C(uint8_t* dst)
{   // Vertical-Right
    const int I = dst[-1 + 0 * BPS];
    const int J = dst[-1 + 1 * BPS];
    const int K = dst[-1 + 2 * BPS];
    const int X = dst[-1 - BPS];
    const int A = dst[0 - BPS];
    const int B = dst[1 - BPS];
    const int C = dst[2 - BPS];
    const int D = dst[3 - BPS];
    DST(0, 0) = DST(1, 2) = AVG2(X, A);
    DST(1, 0) = DST(2, 2) = AVG2(A, B);
    DST(2, 0) = DST(3, 2) = AVG2(B, C);
    DST(3, 0)             = AVG2(C, D);

    DST(0, 3) =             AVG3(K, J, I);
    DST(0, 2) =             AVG3(J, I, X);
    DST(0, 1) = DST(1, 3) = AVG3(I, X, A);
    DST(1, 1) = DST(2, 3) = AVG3(X, A, B);
    DST(2, 1) = DST(3, 3) = AVG3(A, B, C);
    DST(3, 1) =             AVG3(B, C, D);
}

static void
VL4_C(uint8_t* dst) {   // Vertical-Left
    const int A = dst[0 - BPS];
    const int B = dst[1 - BPS];
    const int C = dst[2 - BPS];
    const int D = dst[3 - BPS];
    const int E = dst[4 - BPS];
    const int F = dst[5 - BPS];
    const int G = dst[6 - BPS];
    const int H = dst[7 - BPS];
    DST(0, 0) =             AVG2(A, B);
    DST(1, 0) = DST(0, 2) = AVG2(B, C);
    DST(2, 0) = DST(1, 2) = AVG2(C, D);
    DST(3, 0) = DST(2, 2) = AVG2(D, E);

    DST(0, 1) =             AVG3(A, B, C);
    DST(1, 1) = DST(0, 3) = AVG3(B, C, D);
    DST(2, 1) = DST(1, 3) = AVG3(C, D, E);
    DST(3, 1) = DST(2, 3) = AVG3(D, E, F);
                DST(3, 2) = AVG3(E, F, G);
                DST(3, 3) = AVG3(F, G, H);
}

static void
HU4_C(uint8_t* dst)
{   // Horizontal-Up
    const int I = dst[-1 + 0 * BPS];
    const int J = dst[-1 + 1 * BPS];
    const int K = dst[-1 + 2 * BPS];
    const int L = dst[-1 + 3 * BPS];
    DST(0, 0) =             AVG2(I, J);
    DST(2, 0) = DST(0, 1) = AVG2(J, K);
    DST(2, 1) = DST(0, 2) = AVG2(K, L);
    DST(1, 0) =             AVG3(I, J, K);
    DST(3, 0) = DST(1, 1) = AVG3(J, K, L);
    DST(3, 1) = DST(1, 2) = AVG3(K, L, L);
    DST(3, 2) = DST(2, 2) =
    DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = L;
}

static void
HD4_C(uint8_t* dst)
{  // Horizontal-Down
    const int I = dst[-1 + 0 * BPS];
    const int J = dst[-1 + 1 * BPS];
    const int K = dst[-1 + 2 * BPS];
    const int L = dst[-1 + 3 * BPS];
    const int X = dst[-1 - BPS];
    const int A = dst[0 - BPS];
    const int B = dst[1 - BPS];
    const int C = dst[2 - BPS];

    DST(0, 0) = DST(2, 1) = AVG2(I, X);
    DST(0, 1) = DST(2, 2) = AVG2(J, I);
    DST(0, 2) = DST(2, 3) = AVG2(K, J);
    DST(0, 3)             = AVG2(L, K);

    DST(3, 0)             = AVG3(A, B, C);
    DST(2, 0)             = AVG3(X, A, B);
    DST(1, 0) = DST(3, 1) = AVG3(I, X, A);
    DST(1, 1) = DST(3, 2) = AVG3(J, I, X);
    DST(1, 2) = DST(3, 3) = AVG3(K, J, I);
    DST(1, 3)             = AVG3(L, K, J);
}

static void TM4_C(uint8_t* dst)   { TrueMotion(dst, 4); }
static void TM8uv_C(uint8_t* dst) { TrueMotion(dst, 8); }
static void TM16_C(uint8_t* dst)  { TrueMotion(dst, 16); }


// helper for chroma-DC predictions
static inline void
Put8x8uv(uint8_t value, uint8_t* dst)
{
    for (int j = 0; j < 8; ++j) {
        memset(dst + j * BPS, value, 8);
    }
}

static void DC8uv_C(uint8_t* dst) {     // DC
    int dc0 = 8;
    for (int i = 0; i < 8; ++i) {
        dc0 += dst[i - BPS] + dst[-1 + i * BPS];
    }
    Put8x8uv(dc0 >> 4, dst);
}

static void VE8uv_C(uint8_t* dst) {    // vertical
  int j;
  for (j = 0; j < 8; ++j) {
    memcpy(dst + j * BPS, dst - BPS, 8);
  }
}

static void HE8uv_C(uint8_t* dst) {    // horizontal
  int j;
  for (j = 0; j < 8; ++j) {
    memset(dst, dst[-1], 8);
    dst += BPS;
  }
}

static void DC8uvNoTop_C(uint8_t* dst) {  // DC with no top samples
  int dc0 = 4;
  int i;
  for (i = 0; i < 8; ++i) {
    dc0 += dst[-1 + i * BPS];
  }
  Put8x8uv(dc0 >> 3, dst);
}

static void DC8uvNoLeft_C(uint8_t* dst) {   // DC with no left samples
    int dc0 = 4;
    for (int i = 0; i < 8; ++i) {
        dc0 += dst[i - BPS];
    }
    Put8x8uv(dc0 >> 3, dst);
}

static void DC8uvNoTopLeft_C(uint8_t* dst) {    // DC with nothing
    Put8x8uv(0x80, dst);
}


//------------------------------------------------------------------------------
// 16x16
static void VE16_C(uint8_t* dst) {     // vertical
    for (int j = 0; j < 16; ++j) {
        memcpy(dst + j * BPS, dst - BPS, 16);
    }
}

static void HE16_C(uint8_t* dst) {     // horizontal
    for (int j = 16; j > 0; --j) {
        memset(dst, dst[-1], 16);
        dst += BPS;
    }
}

static inline void Put16(int v, uint8_t* dst) {
    for (int j = 0; j < 16; ++j) {
        memset(dst + j * BPS, v, 16);
    }
}

static void DC16_C(uint8_t* dst) {    // DC
    int DC = 16;
    for (int j = 0; j < 16; ++j) {
        DC += dst[-1 + j * BPS] + dst[j - BPS];
    }
    Put16(DC >> 5, dst);
}

static void DC16NoTop_C(uint8_t* dst) {   // DC with top samples not available
    int DC = 8;
    for (int j = 0; j < 16; ++j) {
        DC += dst[-1 + j * BPS];
    }
    Put16(DC >> 4, dst);
}

static void DC16NoLeft_C(uint8_t* dst)
{  // DC with left samples not available
    int DC = 8;
    for (int i = 0; i < 16; ++i) {
        DC += dst[i - BPS];
    }
    Put16(DC >> 4, dst);
}

static void DC16NoTopLeft_C(uint8_t* dst) {  // DC with no top and left samples
    Put16(0x80, dst);
}




typedef void (*VP8PredFunc)(uint8_t* dst);

VP8PredFunc VP8PredLuma4[NUM_BMODES] = {
    DC4_C,
    TM4_C,
    VE4_C,
    HE4_C,
    RD4_C,
    VR4_C,
    LD4_C,
    VL4_C,
    HD4_C,
    HU4_C
};

VP8PredFunc VP8PredChroma8[NUM_B_DC_MODES] = {
    DC8uv_C,
    TM8uv_C,
    VE8uv_C,
    HE8uv_C,
    DC8uvNoTop_C,
    DC8uvNoLeft_C,
    DC8uvNoTopLeft_C,
};

VP8PredFunc VP8PredLuma16[NUM_B_DC_MODES] = {
    DC16_C,
    TM16_C,
    VE16_C,
    HE16_C,
    DC16NoTop_C,
    DC16NoLeft_C,
    DC16NoTopLeft_C
};

typedef struct {
    uint8_t y[16], u[8], v[8];
} topsamples;

typedef struct {
    uint8_t *m;
    uint8_t *y;
    uint8_t *u;
    uint8_t *v;
    int y_stride;
    int uv_stride;
} cache;

void
reconstruct_row(WEBP *w, struct macro_block *blocks, 
                uint8_t* yuv_b, int y, cache *c, VP8FInfo *finfos)
{

    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int cache_id = 0;

    // int filter_now = (filter_type > 0) && ();
    topsamples yuv_t;

    uint8_t* const y_dst = yuv_b + Y_OFF;
    uint8_t* const u_dst = yuv_b + U_OFF;
    uint8_t* const v_dst = yuv_b + V_OFF;

    // Initialize left-most block.
    for (int j = 0; j < 16; ++j) {
        y_dst[j * BPS - 1] = 129;
    }
    for (int j = 0; j < 8; ++j) {
        u_dst[j * BPS - 1] = 129;
        v_dst[j * BPS - 1] = 129;
    }

    // Init top-left sample on left column too.
    if (y > 0) {
        y_dst[-1 - BPS] = u_dst[-1 - BPS] = v_dst[-1 - BPS] = 129;
    } else {
        // we only need to do this init once at block (0,0).
        // Afterward, it remains valid for the whole topmost row.
        memset(y_dst - BPS - 1, 127, 16 + 4 + 1);
        memset(u_dst - BPS - 1, 127, 8 + 1);
        memset(v_dst - BPS - 1, 127, 8 + 1);
    }

    // Reconstruct one row.
    for (int x = 0; x < ((width + 15) >> 4); ++x) {
        struct macro_block *mb = blocks + y * ((width + 15) >> 4) + x;
        VP8FInfo *finfo = finfos + y *((width + 15) >> 4) + x;

        // Rotate in the left samples from previously decoded block. We move four
        // pixels at a time for alignment reason, and because of in-loop filter.
        if (x > 0) {
            for (int j = -1; j < 16; ++j) {
                memcpy(&y_dst[j * BPS - 4], &y_dst[j * BPS + 12], 4);
            }
            for (int j = -1; j < 8; ++j) {
                memcpy(&u_dst[j * BPS - 4], &u_dst[j * BPS + 4], 4);
                memcpy(&v_dst[j * BPS - 4], &v_dst[j * BPS + 4], 4);
            }
        }

        // bring top samples into the cache
        topsamples* const top_yuv = &yuv_t;
        const int16_t* const coeffs = mb->coeffs;
        uint32_t bits = mb->non_zero_y;
        int n;

        if (y > 0) {
            memcpy(y_dst - BPS, top_yuv[0].y, 16);
            memcpy(u_dst - BPS, top_yuv[0].u, 8);
            memcpy(v_dst - BPS, top_yuv[0].v, 8);
        }

        // predict and add residuals
        if (mb->is_i4x4) {   // 4x4
            uint32_t* const top_right = (uint32_t*)(y_dst - BPS + 16);

            if (y > 0) {
                if (x >= ((width + 15) >> 4) - 1) {    // on rightmost border
                memset(top_right, top_yuv[0].y[15], sizeof(*top_right));
                } else {
                memcpy(top_right, top_yuv[1].y, sizeof(*top_right));
                }
            }
            // replicate the top-right pixels below
            top_right[BPS] = top_right[2 * BPS] = top_right[3 * BPS] = top_right[0];

            // predict and add residuals for all 4x4 blocks in turn.
            for (int n = 0; n < 16; ++n, bits <<= 2) {
                uint8_t* const dst = y_dst + kScan[n];
                VP8PredLuma4[mb->imodes[n]](dst);
                DoTransform(bits, coeffs + n * 16, dst);
            }
        } else {    // 16x16
            const int pred_func = CheckMode(x, y, mb->imodes[0]);
            VP8PredLuma16[pred_func](y_dst);
            if (bits != 0) {
                for (int n = 0; n < 16; ++n, bits <<= 2) {
                    DoTransform(bits, coeffs + n * 16, y_dst + kScan[n]);
                }
            }
        }
        
        // Chroma
        const uint32_t bits_uv = mb->non_zero_uv;
        const int pred_func = CheckMode(x, y, mb->uvmode);
        VP8PredChroma8[pred_func](u_dst);
        VP8PredChroma8[pred_func](v_dst);
        DoUVTransform(bits_uv >> 0, coeffs + 16 * 16, u_dst);
        DoUVTransform(bits_uv >> 8, coeffs + 20 * 16, v_dst);

        // stash away top samples for next block
        if (y < ((height + 15) >> 4)- 1) {
            memcpy(top_yuv[0].y, y_dst + 15 * BPS, 16);
            memcpy(top_yuv[0].u, u_dst +  7 * BPS,  8);
            memcpy(top_yuv[0].v, v_dst +  7 * BPS,  8);
        }
    
        // Transfer reconstructed samples from yuv_b cache to final destination.
    
        const int y_offset = cache_id * 16 * c->y_stride;
        const int uv_offset = cache_id * 8 * c->uv_stride;
        uint8_t* const y_out = c->y + x * 16 + y_offset;
        uint8_t* const u_out = c->u + x * 8 + uv_offset;
        uint8_t* const v_out = c->v + x * 8 + uv_offset;
        // printf("Y: ");
        for (int j = 0; j < 16; ++j) {
            memcpy(y_out + j * c->y_stride, y_dst + j * BPS, 16);
            // for (int ls = 0; ls < 16; ls ++) {
            // printf("%d ", *(y_out + j * c->y_stride + ls));
            // }
            // printf("\n");
        }
        for (int j = 0; j < 8; ++j) {
            memcpy(u_out + j * c->uv_stride, u_dst + j * BPS, 8);
            memcpy(v_out + j * c->uv_stride, v_dst + j * BPS, 8);
        }
    }
}


static void DitherRow() {

}


//-------------------------------------------------------------------------
// Filtering

// kFilterExtraRows[] = How many extra lines are needed on the MB boundary
// for caching, given a filtering level.
// Simple filter:  up to 2 luma samples are read and 1 is written.
// Complex filter: up to 4 luma samples are read and 3 are written. Same for
//                 U/V, so it's 8 samples total (because of the 2x upsampling).
static const uint8_t kFilterExtraRows[3] = { 0, 2, 8 };

typedef void (*VP8SimpleFilterFunc)(uint8_t* p, int stride, int thresh);


// 4 pixels in, 2 pixels out
static inline void DoFilter2_C(uint8_t* p, int step) {
  const int p1 = p[-2*step], p0 = p[-step], q0 = p[0], q1 = p[step];
  const int a = 3 * (q0 - p0) + VP8ksclip1[p1 - q1];  // in [-893,892]
  const int a1 = VP8ksclip2[(a + 4) >> 3];            // in [-16,15]
  const int a2 = VP8ksclip2[(a + 3) >> 3];
  p[-step] = VP8kclip1[p0 + a2];
  p[    0] = VP8kclip1[q0 - a1];
}

// 4 pixels in, 4 pixels out
static inline void DoFilter4_C(uint8_t* p, int step) {
  const int p1 = p[-2*step], p0 = p[-step], q0 = p[0], q1 = p[step];
  const int a = 3 * (q0 - p0);
  const int a1 = VP8ksclip2[(a + 4) >> 3];
  const int a2 = VP8ksclip2[(a + 3) >> 3];
  const int a3 = (a1 + 1) >> 1;
  p[-2*step] = VP8kclip1[p1 + a3];
  p[-  step] = VP8kclip1[p0 + a2];
  p[      0] = VP8kclip1[q0 - a1];
  p[   step] = VP8kclip1[q1 - a3];
}

// 6 pixels in, 6 pixels out
static inline void DoFilter6_C(uint8_t* p, int step)
{
    const int p2 = p[-3*step], p1 = p[-2*step], p0 = p[-step];
    const int q0 = p[0], q1 = p[step], q2 = p[2 * step];
    const int a = VP8ksclip1[3 * (q0 - p0) + VP8ksclip1[p1 - q1]];
    // a is in [-128,127], a1 in [-27,27], a2 in [-18,18] and a3 in [-9,9]
    const int a1 = (27 * a + 63) >> 7;  // eq. to ((3 * a + 7) * 9) >> 7
    const int a2 = (18 * a + 63) >> 7;  // eq. to ((2 * a + 7) * 9) >> 7
    const int a3 = (9  * a + 63) >> 7;  // eq. to ((1 * a + 7) * 9) >> 7
    p[-3*step] = VP8kclip1[p2 + a3];
    p[-2*step] = VP8kclip1[p1 + a2];
    p[-  step] = VP8kclip1[p0 + a1];
    p[      0] = VP8kclip1[q0 - a1];
    p[   step] = VP8kclip1[q1 - a2];
    p[ 2*step] = VP8kclip1[q2 - a3];
}

static inline int
Hev(const uint8_t* p, int step, int thresh)
{
    const int p1 = p[-2*step], p0 = p[-step], q0 = p[0], q1 = p[step];
    return (VP8kabs0[p1 - p0] > thresh) || (VP8kabs0[q1 - q0] > thresh);
}


static inline int
NeedsFilter_C(const uint8_t* p, int step, int t)
{
    const int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
    return ((4 * VP8kabs0[p0 - q0] + VP8kabs0[p1 - q1]) <= t);
}

static inline int
NeedsFilter2_C(const uint8_t* p,
               int step, int t, int it)
{
    const int p3 = p[-4 * step], p2 = p[-3 * step], p1 = p[-2 * step];
    const int p0 = p[-step], q0 = p[0];
    const int q1 = p[step], q2 = p[2 * step], q3 = p[3 * step];
    if ((4 * VP8kabs0[p0 - q0] + VP8kabs0[p1 - q1]) > t) return 0;
    return VP8kabs0[p3 - p2] <= it && VP8kabs0[p2 - p1] <= it &&
            VP8kabs0[p1 - p0] <= it && VP8kabs0[q3 - q2] <= it &&
            VP8kabs0[q2 - q1] <= it && VP8kabs0[q1 - q0] <= it;
}

static void
SimpleVFilter16_C(uint8_t* p, int stride, int thresh)
{
    const int thresh2 = 2 * thresh + 1;
    for (int i = 0; i < 16; ++i) {
        if (NeedsFilter_C(p + i, stride, thresh2)) {
            DoFilter2_C(p + i, stride);
        }
    }
}

static void
SimpleHFilter16_C(uint8_t* p, int stride, int thresh)
{
    const int thresh2 = 2 * thresh + 1;
    for (int i = 0; i < 16; ++i) {
        if (NeedsFilter_C(p + i * stride, 1, thresh2)) {
            DoFilter2_C(p + i * stride, 1);
        }
    }
}

static void
SimpleVFilter16i_C(uint8_t* p, int stride, int thresh)
{
    for (int k = 3; k > 0; --k) {
        p += 4 * stride;
        SimpleVFilter16_C(p, stride, thresh);
    }
}

static void
SimpleHFilter16i_C(uint8_t* p, int stride, int thresh)
{
    for (int k = 3; k > 0; --k) {
        p += 4;
        SimpleHFilter16_C(p, stride, thresh);
    }
}

static inline void
FilterLoop26_C(uint8_t* p,
                int hstride, int vstride, int size,
                int thresh, int ithresh,
                int hev_thresh)
{
    const int thresh2 = 2 * thresh + 1;
    while (size-- > 0) {
        if (NeedsFilter2_C(p, hstride, thresh2, ithresh)) {
            if (Hev(p, hstride, hev_thresh)) {
                DoFilter2_C(p, hstride);
            } else {
                DoFilter6_C(p, hstride);
            }
        }
        p += vstride;
    }
}

static inline
void FilterLoop24_C(uint8_t* p,
                    int hstride, int vstride, int size,
                    int thresh, int ithresh,
                    int hev_thresh)
{
    const int thresh2 = 2 * thresh + 1;
    while (size-- > 0) {
        if (NeedsFilter2_C(p, hstride, thresh2, ithresh)) {
            if (Hev(p, hstride, hev_thresh)) {
                DoFilter2_C(p, hstride);
            } else {
                DoFilter4_C(p, hstride);
            }
        }
        p += vstride;
    }
}


// on macroblock edges
static void VFilter16_C(uint8_t* p, int stride,
                        int thresh, int ithresh, int hev_thresh) {
    FilterLoop26_C(p, stride, 1, 16, thresh, ithresh, hev_thresh);
}

static void HFilter16_C(uint8_t* p, int stride,
                        int thresh, int ithresh, int hev_thresh) {
    FilterLoop26_C(p, 1, stride, 16, thresh, ithresh, hev_thresh);
}

// on three inner edges
static void VFilter16i_C(uint8_t* p, int stride,
                         int thresh, int ithresh, int hev_thresh) {
    int k;
    for (k = 3; k > 0; --k) {
        p += 4 * stride;
        FilterLoop24_C(p, stride, 1, 16, thresh, ithresh, hev_thresh);
    }
}
static void HFilter16i_C(uint8_t* p, int stride,
                         int thresh, int ithresh, int hev_thresh) {
  int k;
  for (k = 3; k > 0; --k) {
    p += 4;
    FilterLoop24_C(p, 1, stride, 16, thresh, ithresh, hev_thresh);
  }
}
// 8-pixels wide variant, for chroma filtering
static void VFilter8_C(uint8_t* u, uint8_t* v, int stride,
                       int thresh, int ithresh, int hev_thresh) {
    FilterLoop26_C(u, stride, 1, 8, thresh, ithresh, hev_thresh);
    FilterLoop26_C(v, stride, 1, 8, thresh, ithresh, hev_thresh);
}
static void HFilter8_C(uint8_t* u, uint8_t* v, int stride,
                       int thresh, int ithresh, int hev_thresh) {
    FilterLoop26_C(u, 1, stride, 8, thresh, ithresh, hev_thresh);
    FilterLoop26_C(v, 1, stride, 8, thresh, ithresh, hev_thresh);
}
static void VFilter8i_C(uint8_t* u, uint8_t* v, int stride,
                        int thresh, int ithresh, int hev_thresh) {
    FilterLoop24_C(u + 4 * stride, stride, 1, 8, thresh, ithresh, hev_thresh);
    FilterLoop24_C(v + 4 * stride, stride, 1, 8, thresh, ithresh, hev_thresh);
}
static void HFilter8i_C(uint8_t* u, uint8_t* v, int stride,
                        int thresh, int ithresh, int hev_thresh) {
    FilterLoop24_C(u + 4, 1, stride, 8, thresh, ithresh, hev_thresh);
    FilterLoop24_C(v + 4, 1, stride, 8, thresh, ithresh, hev_thresh);
}
static void
DoFilter(WEBP *w, int filter_type, int x, int y, cache *c, VP8FInfo *finfo)
{
    int cache_id = 0;
    int y_bps = c->y_stride;
    uint8_t* const y_dst = c->y + cache_id * 16 * y_bps + x * 16;
    const int ilevel = finfo->f_ilevel;
    const int limit = finfo->f_limit;

    if (filter_type == 1) {
        if (x > 0) {
            SimpleHFilter16_C(y_dst, y_bps, limit + 4);
        }
        if (finfo->f_inner) {
            SimpleHFilter16i_C(y_dst, y_bps, limit);
        }
        if (y > 0) {
            SimpleVFilter16_C(y_dst, y_bps, limit + 4);
        }
        if (finfo->f_inner) {
            SimpleVFilter16i_C(y_dst, y_bps, limit);
        }
    } else { //complex
        int uv_bps = c->uv_stride;
        uint8_t * u_dst = c->u + x * 8 + cache_id * uv_bps * 8;
        uint8_t * v_dst = c->v + x * 8 + cache_id * uv_bps * 8;
        const int hev_thresh = finfo->hev_thresh;
        if (x > 0) {
            HFilter16_C(y_dst, y_bps, limit + 4, ilevel, hev_thresh);
            HFilter8_C(u_dst, v_dst, uv_bps, limit + 4, ilevel, hev_thresh);
        }
        if (finfo->f_inner) {
            HFilter16i_C(y_dst, y_bps, limit, ilevel, hev_thresh);
            HFilter8i_C(u_dst, v_dst, uv_bps, limit, ilevel, hev_thresh);
        }
        if (y > 0) {
            VFilter16_C(y_dst, y_bps, limit + 4, ilevel, hev_thresh);
            VFilter8_C(u_dst, v_dst, uv_bps, limit + 4, ilevel, hev_thresh);
        }
        if (finfo->f_inner) {
            VFilter16i_C(y_dst, y_bps, limit, ilevel, hev_thresh);
            VFilter8i_C(u_dst, v_dst, uv_bps, limit, ilevel, hev_thresh);
        }
    }
}

// Filter the decoded macroblock row (if needed)
static void
FilterRow(WEBP *w, int filter_type, int y, cache *c, VP8FInfo *finfos)
{
    int width = ((w->fi.width + 3) >> 2) << 2;
    for (int x = 0; x < (width + 15) >> 4; ++ x) {
        VP8FInfo *finfo = finfos + y *((width + 15) >> 4) + x;
        DoFilter(w, filter_type, x, y, c, finfo);
    }
}

static int
finish_row(WEBP *w, int filter_type, int y, cache *c, VP8FInfo *finfos)
{
    int filter_now = (filter_type > 0);
    int height = w->fi.height;
    int dither = 0;
    const int extra_y_rows = kFilterExtraRows[filter_type];
    const int cache_id = 0;
    const int ysize = extra_y_rows * c->y_stride;
    const int uvsize = (extra_y_rows / 2) * c->uv_stride;
    const int y_offset = cache_id * 16 * c->y_stride;
    const int uv_offset = cache_id * 8 * c->uv_stride;
    uint8_t* const ydst = c->y - ysize + y_offset;
    uint8_t* const udst = c->u - uvsize + uv_offset;
    uint8_t* const vdst = c->v - uvsize + uv_offset;
    const int is_first_row = (y == 0);
    const int is_last_row = (y >= ((height + 15) >> 4) - 1);

    if (filter_now) {
        FilterRow(w, filter_type, y, c, finfos);
    }

    if (dither) {
        DitherRow();
    }

    if (!is_last_row) {
        memcpy(c->y - ysize, ydst + 16 * c->y_stride, ysize);
        memcpy(c->u - uvsize, ydst + 8 * c->uv_stride, uvsize);
        memcpy(c->v - uvsize, ydst + 8 * c->uv_stride, uvsize);
    }

    return 0;
}


/* see 15.1 */
static void
precompute_filter(WEBP *w)
{
    int filter_type = (w->k.loop_filter_level == 0) ? 0 : w->k.filter_type ? 1 : 2;

    // precompute the filtering strength for each segment and each i4x4/i16x16 mode
    if (filter_type) {
        for (int s = 0; s < NUM_MB_SEGMENTS; s++) {
            int base_level;
            if (w->k.segmentation.segmentation_enabled) {
                base_level = w->k.segmentation.lf[s].lf_update_value;
                if (!w->k.segmentation.segment_feature_mode) {
                    base_level += w->k.loop_filter_level;
                }
            } else {
                base_level = w->k.loop_filter_level;
            }
            for (int i4x4 = 0; i4x4 <=1; i4x4 ++) {
                VP8FInfo* const info = &fstrengths[s][i4x4];
                int level = base_level;
                if (w->k.mb_lf_adjustments.loop_filter_adj_enable) {
                    level += w->k.mb_lf_adjustments.mode_ref_lf_delta_update[0];
                    if (i4x4) {
                        level += w->k.mb_lf_adjustments.mb_mode_delta_update[0];
                    }
                }
                level = clamp(level, 63);
                if (level > 0) {
                    int ilevel = level;
                    if (w->k.sharpness_level > 0) {
                        if (w->k.sharpness_level > 4) {
                            ilevel >>= 2;
                        } else {
                            ilevel >>= 1;
                        }
                        if (ilevel > 9 - w->k.sharpness_level) {
                            ilevel = 9 - w->k.sharpness_level;
                        }
                    }
                    if (ilevel < 1) ilevel = 1;
                    info->f_ilevel = ilevel;
                    info->f_limit = 2 * level + ilevel;
                    info->hev_thresh = (level >= 40) ? 2 : (level >= 15) ? 1 : 0;
                } else {
                    info->f_limit = 0;  // no filtering
                }
                info->f_inner = i4x4;
            }
        }
    }
}

static void
vp8_decode(WEBP *w, bool_dec *br, bool_dec *btree[4])
{
    uint32_t *Y, *U, *V;
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;

    struct quant qt;

    for (int i = 0; i < NUM_MB_SEGMENTS; ++i) {
        int q;
        if (w->k.segmentation.segmentation_enabled) {
            q = w->k.segmentation.quant[i].quantizer_update_value;
            if (!w->k.segmentation.segment_feature_mode) {
                q += w->k.quant_indice.y_ac_qi;
            }
        } else {
            if (i > 0) {
                for (int c = 0; c < 2; c ++) {
                    qt.dqm_y1[i][c] = qt.dqm_y1[0][c];
                    qt.dqm_y2[i][c] = qt.dqm_y2[0][c];
                    qt.dqm_uv[i][c] = qt.dqm_uv[0][c];
                    qt.uv_quant[i] = qt.uv_quant[0];
                }
                continue;
            } else {
                q = w->k.quant_indice.y_ac_qi;
            }
        }

        qt.dqm_y1[i][0] = kDcTable[clamp(q + w->k.quant_indice.y_dc_delta, 127)];
        qt.dqm_y1[i][1] = kAcTable[clamp(q + 0, 127)];

        qt.dqm_y2[i][0] = kDcTable[clamp(q + w->k.quant_indice.y2_dc_delta, 127)] * 2;
        // For all x in [0..284], x*155/100 is bitwise equal to (x*101581) >> 16.
        // The smallest precision for that is '(x*6349) >> 12' but 16 is a good
        // word size.
        qt.dqm_y2[i][1] = (kAcTable[clamp(q + w->k.quant_indice.y2_ac_delta, 127)] * 101581) >> 16;
        if (qt.dqm_y2[i][1] < 8) qt.dqm_y2[i][1] = 8;

        qt.dqm_uv[i][0] = kDcTable[clamp(q + w->k.quant_indice.uv_dc_delta, 117)];
        qt.dqm_uv[i][1] = kAcTable[clamp(q + w->k.quant_indice.uv_ac_delta, 127)];

        qt.uv_quant[i] = q + w->k.quant_indice.uv_ac_delta;   // for dithering strength evaluation
    }

    precompute_filter(w);
#if 0
    for (int s = 0; s < NUM_MB_SEGMENTS; ++s) {
        for (int i4x4 = 0; i4x4 <= 1; ++i4x4) {
        VP8FInfo* const fi = &fstrengths[s][i4x4];
        printf("ilevel %d, inner %d,  limit %d, hev %d \n",
            fi->f_ilevel, fi->f_inner, fi->f_limit, fi->hev_thresh);
        }
    }
#endif
    struct macro_block *blocks = malloc(sizeof(struct macro_block)* (((width + 15) >> 4) * ((height + 15) >> 4)));
    
    struct vp8mb *mbs = malloc(sizeof(struct vp8mb)* (((width + 15) >> 4) + 1));
    
    vp8_intramode(w, br, blocks);

    VP8FInfo *finfos = malloc(sizeof(VP8FInfo) * ((width + 15) >> 4)* ((height + 15) >> 4));

    for (int y = 0; y < (height + 15) >> 4; y ++) {
        // parse intra mode here or in advance
        bool_dec *bt = btree[y & (w->k.nbr_partitions - 1)];

        //reset left part
        struct vp8mb *left = mbs;
        left->nz = 0;
        left->nz_dc = 0;

        for (int x = 0; x < (width + 15) >> 4; x ++) {
            struct macro_block *block = blocks + y *((width + 15) >> 4) + x;
            struct vp8mb *mb = mbs + 1 + x;
            VP8FInfo *fi = finfos + y *((width + 15) >> 4) + x;
            // printf("2 y %d, x %d, is 4x4 %d, tnz %d, lnz %d\n", y, x, block->is_i4x4, mb->nz, left->nz);
            vp8_decode_MB(w, left, mb, block, bt, &qt, fi);
        }
    }

    int filter_type = (w->k.loop_filter_level == 0) ? 0 : w->k.filter_type ? 1 : 2;
    uint8_t* yuv_b = malloc(YUV_SIZE);
    int extra_rows = kFilterExtraRows[filter_type];

    cache c;
    c.m = malloc((((width + 15) >> 4) * sizeof(topsamples)) * ((16*1 + extra_rows) * 3 / 2));
    c.y_stride = ((width + 15) >> 4) * 16;
    c.uv_stride = ((width + 15) >> 4) * 8;
    c.y = c.m + extra_rows * c.y_stride;
    c.u = c.y + 16 * c.y_stride + (extra_rows / 2) * c.uv_stride;
    c.v = c.u + 8 * c.uv_stride + (extra_rows / 2) * c.uv_stride;
    // printf("y %d, u %d, v %d, extra %d, cache size %ld\n",  extra_rows * c.y_stride, 16 * c.y_stride + (extra_rows / 2) * c.uv_stride,
    // 8 * c.uv_stride + (extra_rows / 2) * c.uv_stride, extra_rows,
    //     (((width + 15) >> 4) * sizeof(topsamples)) * ((16*1 + extra_rows) * 3 / 2));

    for (int j = 0; j < 24; j ++) {
        for (int i = 0; i < 16; i ++) {
            printf("%03d ", blocks->coeffs[j * 16 + i]);
        }
        printf("\n");
    }
    /* reconstruct the row with filter */
    for (int y = 0; y < (height + 15) >> 4; y ++) {
        reconstruct_row(w, blocks, yuv_b, y, &c, finfos);
        finish_row(w, filter_type, y, &c, finfos);
    }
    
    

    // for (int i = 0; i < ((width + 15) >> 4); i ++) {
    //     VP8FInfo *fi = finfos + i;
    //     VDBG(webp, "ilevel %d, inner %d,  limit %d, hev %d", 
    //         fi->f_ilevel, fi->f_inner, fi->f_limit, fi->hev_thresh);
    // }

    free(yuv_b);
    free(c.m);
}

static struct pic* 
WEBP_load(const char *filename)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    WEBP *w = calloc(1, sizeof(WEBP));
    p->pic = w;
    FILE *f = fopen(filename, "rb");
    fread(&w->header, sizeof(w->header), 1, f);
    uint32_t chead;
    fread(&chead, 4, 1, f);
    uint32_t chunk_size;
    if (chead == CHUNCK_HEADER("VP8X")) {
        fseek(f, -4, SEEK_CUR);
        fread(&w->vp8x, sizeof(struct webp_vp8x), 1, f);
    } else if (chead == CHUNCK_HEADER("VP8 ")) {
        //VP8 data chuck
        
    } else if (chead == CHUNCK_HEADER("VP8L")) {
        //VP8 lossless chuck
        VINFO(webp, "VP8L\n");
        return NULL;
    }
    fread(&chunk_size, 4, 1, f);
    unsigned char b[3];
    fread(b, 3, 1, f);
    w->fh.frame_type = b[0] & 0x1;
    w->fh.version = (b[0] >> 1) & 0x7;
    w->fh.show_frame = (b[0] >> 4) & 0x1;
    w->fh.size_h = (b[0] >> 5) & 0x7;
    w->fh.size = b[2] << 8 | b[1];
    if (w->fh.frame_type != KEY_FRAME) {
        VERR(webp, "not a key frame for vp8\n");
        free(w);
        free(p);
        fclose(f);
        return NULL;
    }

    /* key frame, more info */
    uint8_t keyfb[7];
    fread(keyfb, 7, 1, f);
    if (keyfb[0] != 0x9d || keyfb[1] != 0x01 || keyfb[2] != 0x2a) {
        VERR(webp, "not a valid start code for vp8\n");
    }
    w->fi.start1 = keyfb[0];
    w->fi.start2 = keyfb[1];
    w->fi.start3 = keyfb[2];
    w->fi.horizontal = keyfb[4] >> 6;
    w->fi.width = (keyfb[4] << 8 | keyfb[3]) & 0x3fff;
    w->fi.vertical = keyfb[6] >> 6;
    w->fi.height = (keyfb[6] << 8 | keyfb[5]) & 0x3fff;

    int partition0_size = (((int)w->fh.size << 3) | w->fh.size_h);
    uint8_t *buf = malloc(partition0_size);
    fread(buf, partition0_size, 1, f);
    VDBG(webp, "partion0_size %d", partition0_size);
    struct bool_dec *first_bt = bool_dec_init(buf, partition0_size);

    read_vp8_ctl_partition(w, first_bt, f);

    bool_dec *bt[MAX_PARTI_NUM];

    for (int i = 0; i < w->k.nbr_partitions; i ++) {
        uint8_t *parts = malloc(w->p[i].len);
        fseek(f, w->p[i].start, SEEK_SET);
        fread(parts, 1, w->p[i].len, f);
        VDBG(webp, "part %d: len %d", i, w->p[i].len);
        // hexdump(stdout, "partitions", parts, 120);
        bt[i] = bool_dec_init(parts, w->p[i].len);
    }
    fclose(f);
    
    vp8_decode(w, first_bt, bt);

    bool_dec_free(first_bt);
    for (int i = 0; i < w->k.nbr_partitions; i ++) { 
        bool_dec_free(bt[i]);
    }
    return p;
}

void 
WEBP_free(struct pic * p)
{
    WEBP * w = (WEBP *)p->pic;

    free(w);
    free(p);
}

void 
WEBP_info(FILE *f, struct pic* p)
{
    WEBP * w = (WEBP *)p->pic;
    fprintf(f, "WEBP file format:\n");
    fprintf(f, "\tfile size: %d\n", w->header.file_size);
    fprintf(f, "----------------------------------\n");
    if (w->vp8x.vp8x == CHUNCK_HEADER("VP8X")) {
        fprintf(f, "\tVP8X icc %d, alpha %d, exif %d, xmp %d, animation %d\n",
            w->vp8x.icc, w->vp8x.alpha, w->vp8x.exif_metadata, w->vp8x.xmp_metadata, w->vp8x.animation);
        fprintf(f, "\tVP8X canvas witdth %d, height %d\n", READ_UINT24(w->vp8x.canvas_width),
            READ_UINT24(w->vp8x.canvas_height));
    }
    int size = w->fh.size_h | (w->fh.size << 3);
    fprintf(f, "\t%s: version %d, partition0_size %d\n", w->fh.frame_type == KEY_FRAME? "I frame":"P frame", w->fh.version, size);
    if (w->fh.frame_type == KEY_FRAME) {
        fprintf(f, "\tscale horizon %d, vertical %d\n", w->fi.horizontal, w->fi.vertical);
        fprintf(f, "\theight %d, width %d\n", w->fi.height, w->fi.width);
        fprintf(f, "\tsegment_enable %d, update_map %d, update_data %d\n", w->k.segmentation.segmentation_enabled,
            w->k.segmentation.update_mb_segmentation_map, w->k.segmentation.update_segment_feature_data);
        if (w->k.segmentation.update_segment_feature_data) {
            fprintf(f, "\tquantizer: ");
            for (int i = 0; i < 4; i ++) {
                if (w->k.segmentation.quant[i].quantizer_update) {
                    fprintf(f, "%d ", w->k.segmentation.quant[i].quantizer_update_value);
                }
            }
            fprintf(f, "\n");
            fprintf(f, "\tfilter strength: ");
            for (int i = 0; i < 4; i ++) {
                if (w->k.segmentation.lf[i].loop_filter_update) {
                    fprintf(f, "%d ", w->k.segmentation.lf[i].lf_update_value);
                }
            }
            fprintf(f, "\n");
        }
        if (w->k.segmentation.update_mb_segmentation_map) {
            fprintf(f, "\tsegment prob:");
            for (int i = 0; i < 3; i ++) {
                fprintf(f, "%d ", w->k.segmentation.segment_prob[i]);
            }
            fprintf(f, "\n");
        }
        fprintf(f, "\tfilter_type %d, loop_filter_level %d, sharpness_level %d \n",
                w->k.filter_type, w->k.loop_filter_level, w->k.sharpness_level);
        fprintf(f, "\tpartitions %d\n", w->k.nbr_partitions);
        fprintf(f, "\ty_ac_qi: %d\n", w->k.quant_indice.y_ac_qi);
        fprintf(f, "\ty_dc_delta: %d\n", w->k.quant_indice.y_dc_delta);
        fprintf(f, "\ty2_dc_delta: %d\n", w->k.quant_indice.y2_dc_delta);
        fprintf(f, "\ty2_ac_delta: %d\n", w->k.quant_indice.y2_ac_delta);
        fprintf(f, "\tuv_dc_delta: %d\n", w->k.quant_indice.uv_dc_delta);
        fprintf(f, "\tuv_ac_delta: %d\n", w->k.quant_indice.uv_ac_delta);
    }
#if 0
    for (int i = 0; i < 4; i ++) {
        for (int j = 0; j < 8 ; j ++) {
            for (int k = 0; k < 3; k ++) {
                fprintf(f, "\t");
                for (int l = 0; l < 11; l ++) {
                    fprintf(f, "%03d ", w->k.coeff_prob[i][j][k][l]);
                }
                fprintf(f, "\n");
            }
        }
    }
#endif
    fprintf(f, "\tprob_skip_false %d\n", w->k.prob_skip_false);

}

static struct file_ops webp_ops = {
    .name = "WEBP",
    .probe = WEBP_probe,
    .load = WEBP_load,
    .free = WEBP_free,
    .info = WEBP_info,
};

void WEBP_init(void)
{
    file_ops_register(&webp_ops);
}