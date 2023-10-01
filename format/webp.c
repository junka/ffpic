#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include "bitstream.h"
#include "predict.h"
#include "booldec.h"
#include "file.h"
#include "utils.h"
#include "vlog.h"
#include "webp.h"

VLOG_REGISTER(webp, DEBUG)

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
    VDBG(webp, "segmentation_enabled :%d", s->segmentation_enabled);
    if (s->segmentation_enabled) {
        s->update_mb_segmentation_map = BOOL_BIT(br);
        s->update_segment_feature_data = BOOL_BIT(br);
        VDBG(webp, "update_mb_segmentation_map :%d, update_segment_feature_data %d",
             s->update_mb_segmentation_map, s->update_segment_feature_data);
        if (s->update_segment_feature_data) {
            s->segment_feature_mode = BOOL_BIT(br);
            for (int i = 0; i < NUM_MB_SEGMENTS; i ++) {
                s->quant[i].quantizer_update = BOOL_BIT(br);
                if (s->quant[i].quantizer_update) {
                  s->quant[i].quantizer_update_value = BOOL_SBITS(br, 7);
                } else {
                    s->quant[i].quantizer_update_value = 0;
                }
            }
            for (int i = 0; i < NUM_MB_SEGMENTS; i ++) {
                s->lf[i].loop_filter_update = BOOL_BIT(br);
                if (s->lf[i].loop_filter_update) {
                    s->lf[i].lf_update_value = BOOL_SBITS(br, 6);
                } else {
                    s->lf[i].lf_update_value = 0;
                }
            }
        }
        if (s->update_mb_segmentation_map) {
            for (int i = 0; i < 3; i ++) {
                if (BOOL_BIT(br)) {
                    s->segment_prob[i] = BOOL_BITS(br, 8);
                }
            }
        }
    } else {
        s->update_mb_segmentation_map = 1;
        s->update_segment_feature_data = 0;
    }
}

static void
read_mb_lf_adjustments(struct vp8_mb_lf_adjustments *lf, struct bool_dec *br)
{
    lf->loop_filter_adj_enable = BOOL_BIT(br);
    if (lf->loop_filter_adj_enable) {
        lf->mode_ref_lf_delta_update_flag = BOOL_BIT(br);
        if (lf->mode_ref_lf_delta_update_flag) {
            for (int i = 0; i < 4; i ++) {
                // d->mode_ref_lf_delta_update[i].ref_frame_delta_update_flag = BOOL_BIT(br);
                if (BOOL_BIT(br)) {
                    lf->mode_ref_lf_delta_update[i] = BOOL_SBITS(br, 6);
                } else {
                    lf->mode_ref_lf_delta_update[i] = 0;
                }
            }
            for (int i = 0; i < 4; i ++) {
                // d->mb_mode_delta_update[i].mb_mode_delta_update_flag = BOOL_BIT(br);
                if (BOOL_BIT(br)) {
                    lf->mb_mode_delta_update[i] = BOOL_SBITS(br, 6);
                } else {
                    lf->mb_mode_delta_update[i] = 0;
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
    /*  If the number of data partitions is
    greater than 1, the size of each partition (except the last) is
    written in 3 bytes (24 bits).  The size of the last partition is the
    remainder of the data not used by any of the previous partitions.
    */

    uint8_t size[3 * MAX_PARTI_NUM];
    if (num) {
        fread(size, 3, num, f);
    }
    uint32_t next_part = ftell(f);
    for (int i = 0; i < num; i ++)
    {
        int partsize = size[i*3] | size[i*3 + 1] << 8 | size[i*3 + 2] << 16;
        VDBG(webp, "partsize %d\n", partsize);
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

static void read_dequantization(WEBP *w, struct vp8_key_frame_header *kh,
                                struct bool_dec *br) {

    // 13.4 quant_indices
    kh->quant_indice.y_ac_qi = BOOL_BITS(br, 7);
    kh->quant_indice.y_dc_delta = (BOOL_BIT(br)) ? BOOL_SBITS(br, 4) : 0;

    kh->quant_indice.y2_dc_delta = (BOOL_BIT(br)) ? BOOL_SBITS(br, 4) : 0;
    kh->quant_indice.y2_ac_delta = (BOOL_BIT(br)) ? BOOL_SBITS(br, 4) : 0;
    kh->quant_indice.uv_dc_delta = (BOOL_BIT(br)) ? BOOL_SBITS(br, 4) : 0;
    kh->quant_indice.uv_ac_delta = (BOOL_BIT(br)) ? BOOL_SBITS(br, 4) : 0;

    /*
        All residue signals are specified via a quantized 4x4 DCT applied to
        the Y, U, V, or Y2 subblocks of a macroblock.  As detailed in
        Section 14, before inverting the transform, each decoded coefficient
        is multiplied by one of six dequantization factors, the choice of
        which depends on the plane (Y, chroma = U or V, Y2) and coefficient
        position (DC = coefficient 0, AC = coefficients 1-15).  The six
        values are specified using 7-bit indices into six corresponding fixed
        tables (the tables are given in Section 14).
   */

    // from section 14.1 Dequantization
    static const uint16_t dc_qlookup[128] = {
        4,   5,   6,   7,   8,   9,   10,  10,  11,  12,  13,  14,  15,
        16,  17,  17,  18,  19,  20,  20,  21,  21,  22,  22,  23,  23,
        24,  25,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,
        36,  37,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  46,
        47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
        60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,
        73,  74,  75,  76,  76,  77,  78,  79,  80,  81,  82,  83,  84,
        85,  86,  87,  88,  89,  91,  93,  95,  96,  98,  100, 101, 102,
        104, 106, 108, 110, 112, 114, 116, 118, 122, 124, 126, 128, 130,
        132, 134, 136, 138, 140, 143, 145, 148, 151, 154, 157,
    };

    static const uint16_t ac_qlookup[128] = {
        4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,
        17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
        30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,
        43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,
        56,  57,  58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  78,
        80,  82,  84,  86,  88,  90,  92,  94,  96,  98,  100, 102, 104,
        106, 108, 110, 112, 114, 116, 119, 122, 125, 128, 131, 134, 137,
        140, 143, 146, 149, 152, 155, 158, 161, 164, 167, 170, 173, 177,
        181, 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229,
        234, 239, 245, 249, 254, 259, 264, 269, 274, 279, 284,
    };
    /*
        Lookup values from the above two tables are directly used in the DC
        and AC coefficients in Y1, respectively.  For Y2 and chroma, values
        from the above tables undergo either scaling or clamping before the
        multiplies. 
    */
    // section 20.4 scaling and clamping processes
    // see dequant_init and ac_q and dc_q
    for (int i = 0; i < (w->k.segmentation.segmentation_enabled ? 4 : 1); i++) {
        uint16_t quant = kh->quant_indice.y_ac_qi;
        if (w->k.segmentation.segmentation_enabled) {
            if(!w->k.segmentation.update_mb_segmentation_map) {
                quant += w->k.segmentation.quant[i].quantizer_update_value;
            } else {
                quant = w->k.segmentation.quant[i].quantizer_update_value;
            }
        }
        w->d[i].y1_dc = dc_qlookup[clamp(quant + kh->quant_indice.y_dc_delta, 127)];
        w->d[i].y1_ac = ac_qlookup[clamp(quant, 127)];

        w->d[i].y2_dc =
            dc_qlookup[clamp(quant + kh->quant_indice.y2_dc_delta, 127)] * 2;
        w->d[i].y2_ac =
            ac_qlookup[clamp(quant + kh->quant_indice.y2_ac_delta, 127)] * 155 /
            100;

        w->d[i].uv_dc =
            dc_qlookup[clamp(quant + kh->quant_indice.uv_dc_delta, 127)];
        w->d[i].uv_ac =
            ac_qlookup[clamp(quant + kh->quant_indice.uv_ac_delta, 127)];

        if (w->d[i].y2_dc > 132) {
            w->d[i].y2_dc = 132;
        }
        if (w->d[i].y2_ac < 8) {
            w->d[i].y2_ac = 8;
        }
        w->d[i].quant = quant;
        // for dithering
        w->d[i].uv_quant = quant + kh->quant_indice.uv_ac_delta;
    }
}

static void
read_token_proba_update(struct vp8_key_frame_header *kh, struct bool_dec *br)
{
    // from Token Probality Updates 13.4
    static const uint8_t
        coeff_update_probs[NUM_TYPES][NUM_BANDS][NUM_CTX][NUM_PROBAS] = {
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

    /*   Default Token Probability Table 13.5*/
    static const uint8_t default_coeff_probs[NUM_TYPES][NUM_BANDS][NUM_CTX][NUM_PROBAS] = 
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
    /*if not keyframe 9.7, 9.8,
    * since we only process  static webp, ignore them
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

    /* DCT Coefficient Probability Update 9.9 , 13.4 */
    for (int i = 0; i < NUM_TYPES; i ++) {
        for (int j = 0; j < NUM_BANDS ; j ++) {
            for (int k = 0; k < NUM_CTX; k ++) {
                for (int l = 0; l < NUM_PROBAS; l ++) {
                    if (BOOL_DECODE(br, coeff_update_probs[i][j][k][l])) {
                        kh->coeff_prob[i][j].probas[k][l] = BOOL_BITS(br, 8);
                    } else {
                        kh->coeff_prob[i][j].probas[k][l] = default_coeff_probs[i][j][k][l];
                    }
                }
            }
        }
    }
}

static void
read_vp8_ctl_partition(WEBP *w, struct bool_dec *br, FILE *f)
{
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    // int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
    VDBG(webp, "width in MB block uint:%d", (width + 15) >> 4);
    VDBG(webp, "height in MB block uint:%d", (height + 15) >> 4);

    w->k.cs_and_clamp.color_space = BOOL_BIT(br);
    w->k.cs_and_clamp.clamp = BOOL_BIT(br);
    VDBG(webp, "cs %d, clamp %d", w->k.cs_and_clamp.color_space, w->k.cs_and_clamp.clamp);

    /* READ Segment-Based Adjustments 9.3 */
    read_vp8_segmentation_adjust(&w->k.segmentation, br);

    /* Loop Filter Type and Levels 9.4 */
    w->k.filter_type = BOOL_BIT(br);
    w->k.loop_filter_level = BOOL_BITS(br, 6);
    w->k.sharpness_level = BOOL_BITS(br, 3);

    /* READ adjustments */
    read_mb_lf_adjustments(&w->k.mb_lf_adjustments, br);

    /*Token Partition and Partition Data Offsets 9.5*/
    read_token_partition(w, br, f);

    /* READ Dequantization Indices 9.6 */
    read_dequantization(w, &w->k, br);

    read_token_proba_update(&w->k ,br);

    /* section 9.11 */
    w->k.mb_no_skip_coeff = BOOL_BIT(br);
    if (w->k.mb_no_skip_coeff) {
        w->k.prob_skip_false = BOOL_BITS(br, 8);
    } else {
        w->k.prob_skip_false = 0;
    }
}

/* Residual decoding (Paragraph 13.2 / 13.3) */
typedef enum {
    DCT_0,    /* value 0 */
    DCT_1,    /* 1 */
    DCT_2,    /* 2 */
    DCT_3,    /* 3 */
    DCT_4,    /* 4 */
    dct_cat1, /* range 5 - 6  (size 2) */
    dct_cat2, /* 7 - 10   (4) */
    dct_cat3, /* 11 - 18  (8) */
    dct_cat4, /* 19 - 34  (16) */
    dct_cat5, /* 35 - 66  (32) */
    dct_cat6, /* 67 - 2048  (1982) */
    dct_eob,  /* end of block */

    num_dct_tokens /* 12 */
} dct_token;

/**
 * The function DCTextra performs a bitwise operation on a series of boolean values and returns the
 * result as a uint8_t.
 * 
 * @param bt The parameter "bt" is a pointer to a boolean decoder object. It is used to decode boolean
 * values from a bitstream.
 * @param p The parameter `p` is a pointer to an array of `uint8_t` values.
 * 
 * @return a value of type `uint8_t`.
 */
uint8_t DCTextra(bool_dec *bt, const uint8_t *p) {
    uint8_t v = 0;
    do {
        v += v + BOOL_DECODE(bt, *p);
    } while (*++p);
    return v;
}

/**
 * The function `vp8_get_coefficients` decodes coefficients for a VP8 video frame.
 * 
 * @param bt A pointer to a struct bool_dec, which is a boolean decoder used for decoding coefficients
 * in the VP8 video codec.
 * @param out A pointer to an array of int16_t where the decoded coefficients will be stored.
 * @param bands An array of pointers to VP8BandProbas structures. Each VP8BandProbas structure contains
 * probability tables for each coefficient band.
 * @param first The parameter "first" is the index of the first coefficient to be decoded. It indicates
 * the starting point in the coefficient array where the decoding should begin.
 * @param ctx The parameter "ctx" represents the context for coefficient decoding. It is used to
 * determine the probability table to use for decoding the coefficient token.
 * @param quant_dc The parameter "quant_dc" represents the quantization factor for the DC coefficient.
 * It is used to scale the DC coefficient before encoding or decoding.
 * @param quant_ac The parameter "quant_ac" represents the quantization factor for the AC coefficients.
 * It is used to scale the absolute value of the coefficient before storing it in the "out" array.
 * 
 * @return the number of coefficients processed, which is always 16 in this case.
 */
static int vp8_get_coefficients(struct bool_dec *bt, int16_t *out,
                                const VP8BandProbas *const bands[], int first,
                                int ctx, uint16_t quant_dc, uint16_t quant_ac)
{
    bool prevCoeffWasZero = false;
    int token = 0;
    int absValue = 0;
    static int categoryBase[6] = {5, 7, 11, 19, 35, 67};
    /* pCatn specify ranges of unsigned values whose width is
     * 1, 2, 3, 4, 5, or 11 bits, respectively.
     */
    static const uint8_t pCat1[] = {159, 0};
    static const uint8_t pCat2[] = {165, 145, 0};
    static const uint8_t pCat3[] = {173, 148, 140, 0};
    static const uint8_t pCat4[] = {176, 155, 140, 135, 0};
    static const uint8_t pCat5[] = {180, 157, 141, 134, 130, 0};
    static const uint8_t pCat6[] = {254, 254, 243, 230, 196, 177,
                                    153, 140, 133, 130, 129, 0};

    static const uint8_t *pCat[] = {pCat1, pCat2, pCat3, pCat4, pCat5, pCat6};

    static const uint8_t kZigzag[16] = {0, 1,  4,  8,  5, 2,  3,  6,
                                        9, 12, 13, 10, 7, 11, 14, 15};

    static const int8_t coeff_tree[2 * (num_dct_tokens - 1)] = {
        -dct_eob,  2, /* eob = "0"   */
        -DCT_0,    4, /* 0   = "10"  */
        -DCT_1,    6, /* 1   = "110" */
        8,         12,       -DCT_2,
        10,                /* 2   = "11100" */
        -DCT_3,    -DCT_4, /* 3   = "111010", 4 = "111011" */
        14,        16,       -dct_cat1,
        -dct_cat2, /* cat1 =  "111100",
                      cat2 = "111101" */
        18,        20,       -dct_cat3,
        -dct_cat4,           /* cat3 = "1111100",
                                cat4 = "1111101" */
        -dct_cat5, -dct_cat6 /* cat4 = "1111110",
                                cat4 = "1111111" */
    };

    for (int n = first; n < 16; ++n) {
        const uint8_t *p = bands[n]->probas[ctx];

        token = bool_dec_tree(bt, coeff_tree, p, prevCoeffWasZero ? 2 : 0);
        // VDBG(webp, "token %d", token);
        if (token == dct_eob) {
            return n - first;
        } else if (token == DCT_0) {
            prevCoeffWasZero = true;
            absValue = 0;
        } else if (token > DCT_4) {
            int extraBits = DCTextra(bt, pCat[token - dct_cat1]);
            // VDBG(webp, "extra %d", token);
            absValue = categoryBase[token - dct_cat1] + extraBits;
            prevCoeffWasZero = false;
        } else {
            absValue = token;
            prevCoeffWasZero = false;
        }

        ctx = absValue == 0 ? 0 : (absValue == 1 ? 1 : 2);

        if (absValue != 0) {
            if (BOOL_DECODE(bt, 128)) {
                absValue = -absValue;
            }
        }

        out[kZigzag[n]] = absValue * (n > 0 ? quant_ac : quant_dc);
    }
    return 16;
}

//in out for 4*4
static void IWHT_C(const int16_t* in, int16_t* out)
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
    // pass two
    for (i = 0; i < 4; ++i) {
        const int dc = tmp[0 + i * 4] + 3;    // w/ rounder
        const int a0 = dc             + tmp[3 + i * 4];
        const int a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
        const int a2 = tmp[1 + i * 4] - tmp[2 + i * 4];
        const int a3 = dc             - tmp[3 + i * 4];
        out[4 * i + 0] = (a0 + a1) >> 3;
        out[4 * i + 1] = (a3 + a2) >> 3;
        out[4 * i + 2] = (a0 - a1) >> 3;
        out[4 * i + 3] = (a3 - a2) >> 3;
        // out += 64;
    }
}

#if 0
static void IDCT_C(const int16_t *in, int16_t *out)
{
    const int c1 = 20091;
    const int c2 = 35468;
    int tmp[16];
    int i;
    for (i = 0; i < 4; ++i) {
        const int a0 = in[0 + i] + in[8 + i];
        const int a1 = in[0 + i] - in[8 + i];
        const int a2 = ((in[4 + i] * c2) >> 16) - in[12 + i] - ((in[12 + i] * c1) >> 16);
        const int a3 = in[4 + i] + ((in[4 + i] * c1) >> 16) - ((in[12 + i] * c2) >> 16);
        tmp[0 + i] = a0 + a3;
        tmp[12 + i] = a1 - a3;
        tmp[4 + i] = a2 + a3;
        tmp[8 + i] = a2 - a3;
    }
    for (i = 0; i < 4; ++i) {
        const int dc = tmp[0 + i * 4] + 3; // w/ rounder
        const int a0 = tmp[0 + i * 4] + tmp[2 + i * 4];
        const int a1 = tmp[0 + i * 4] - tmp[2 + i * 4];
        const int a2 = (tmp[1 + i * 4] * c2 >> 16) - tmp[3 + i * 4] - (tmp[3 + i * 4] * c1 >> 16);
        const int a3 = tmp[1 + i * 4] + (tmp[1 + i * 4] * c1 >> 16) + (tmp[3 + i * 4] * c2 >> 16);
        out[4 * i] = (a0 + a3 + 4) >> 3;
        out[4 * i + 3] = (a0 - a3 + 4) >> 3;
        out[4 * i + 1] = (a1 + a2 + 4) >> 3;
        out[4 * i + 2] = (a1 - a2 + 4) >> 3;
        // out += 64;
    }
}
#endif

struct context {
    int ctx[9];
};

/**
 * The function `vp8_decode_residual_block` decodes the residual block of a VP8 video frame.
 * 
 * @param w A pointer to a structure of type WEBP, which contains information about the WebP image
 * being decoded.
 * @param block The "block" parameter is a pointer to a struct macro_block, which contains information
 * about a macro block in the video frame. It includes the block's position (x, y), context information
 * (ctx), intra prediction mode (intra_y_mode), and coefficients (coeffs).
 * @param bt The parameter "bt" is a pointer to a boolean decoder. It is used to decode the
 * coefficients of the residual block.
 * 
 * @return an integer value of 0.
 */
static int vp8_decode_residual_block(WEBP *w, struct macro_block *block,
                                     struct context *left, struct context *top,
                                     bool_dec *bt) 
{

    static const int coeff_bands[16] = {0, 1, 2, 3, 6, 4, 5, 6,
                                 6, 6, 6, 6, 6, 6, 6, 7};

    const VP8BandProbas *bands[NUM_TYPES][16];
    int16_t* dst = block->coeffs;
    memset(dst, 0, 384 * sizeof(*dst));
    // 384 = 16 * (16 + 4 + 4) which is Y U V

    struct WEBP_decoder *d = &w->d[block->segment_id];

    int firstCoeff;
    const VP8BandProbas* const * ac_proba;

    for (int t = 0; t < NUM_TYPES; ++t) {
        for (int b = 0; b < 16; ++b) {
            bands[t][b] = &w->k.coeff_prob[t][coeff_bands[b]];
        }
    }
    // Y2
    if (block->intra_y_mode != B_PRED) {
        int16_t dc[16] = {0};
        int ctx = top[block->x].ctx[0] + left->ctx[0];
        const int nz = vp8_get_coefficients(bt, dc, bands[1], 0, ctx, d->y2_dc, d->y2_ac);
        top[block->x].ctx[0] = left->ctx[0] = ((nz > 0) ? 1 : 0);
        if (nz > 1) {   // more than just the DC -> perform the full transform
            IWHT_C(dc, dst);
            // for (int i = 0; i < 16; i += 1) {
            //     dst[i * 16] = dst[i];
            // }
        } else {        // only DC is non-zero -> inlined simplified transform
            const int dc0 = (dc[0] + 3) >> 3;
            for (int i = 0; i < 16; i ++) {
                dst[i * 16] = dc0;
            }
        }
        firstCoeff = 1;
        ac_proba = bands[0];
    } else {
        firstCoeff = 0;
        ac_proba = bands[3];
    }

    // 16Y
    // int16_t dc[16] = {0};
    for (int y = 0; y < 4; ++y) {
        uint8_t l = left->ctx[y+1];
        for (int x = 0; x < 4; ++x) {
            int ctx = top[block->x].ctx[x + 1] + l;
            const int nz = vp8_get_coefficients(bt, dst, ac_proba, firstCoeff, ctx, d->y1_dc, d->y1_ac);
            // if (nz > 1) {
            //     IDCT_C(dc, dst);
            // }
            // for (int i = 0; i < 16; i ++) {
            //     dst[(x + y * 4) * 16 + i] = dc[i];
            // }
            dst += 16;
            l = top[block->x].ctx[x+1] = (nz > 0 ? 1 : 0);
        }
        left->ctx[y+1] = l;
    }

    //4U 4V
    // memset(dc, 0, 16 * 2);
    for (int ch = 5; ch <= 7; ch += 2) {
        for (int y = 0; y < 2; ++y) {
            uint8_t l = left->ctx[y + ch];
            for (int x = 0; x < 2; ++x) {
                int ctx = l + top[block->x].ctx[x + ch];
                const int nz = vp8_get_coefficients(bt, dst, bands[2], 0, ctx, d->uv_dc, d->uv_ac);
                // if (nz > 1) {
                //     IDCT_C(dc, dst);
                // }
                // for (int i = 0; i < 16; i++) {
                //     dst[(x + y * 2 + ((ch == 5) ? 16 : 20)) * 16 + i] = dc[i];
                // }
                dst += 16;
                l = top[block->x].ctx[x+ch] = (nz > 0) ? 1: 0;
            }
            left->ctx[y+ch] = l;
        }
    }

    return 0;
}

typedef struct {  // filter specs
  uint8_t f_limit;      // filter limit in [3..189], or 0 if no filtering
  uint8_t f_ilevel;     // inner limit in [1..63]
  uint8_t f_inner;      // do inner filtering?
  uint8_t hev_thresh;   // high edge variance threshold in [0..2]
} VP8FInfo;

static VP8FInfo fstrengths[NUM_MB_SEGMENTS][2];

static int vp8_decode_residual_data(WEBP *w, struct macro_block *block,
                                    bool_dec *bt, struct context *left,
                                    struct context *top, VP8FInfo *finfo) 
{
  // int filter_type = (w->k.loop_filter_level == 0) ? WEBP_FILTER_NONE :
  //     w->k.filter_type ? WEBP_FILTER_SIMPLE : WEBP_FILTER_NORMAL;
  // 0=off, 1=simple, 2=complex

  // see section 19.3
  if (!block->mb_skip_coeff) {
        vp8_decode_residual_block(w, block, left, top, bt);
    } else {
        // 1 DC
        if (block->intra_y_mode != B_PRED) {
            left->ctx[0] = 0;
            top[block->x].ctx[0] = 0;
        }
        //  4 luma and 4 chrome
        for (int i = 1; i < 9; i++) {
            left->ctx[i] = 0;
            top[block->x].ctx[i] = 0;
        }
        block->dither = 0;
        // memset(block->coeffs, 0, 384 * sizeof(int16_t));
    }
    //VDBG(webp, "%d %d %d %d %d %d %d", block->coeffs[0], block->coeffs[16], block->coeffs[64], block->coeffs[65], block->coeffs[381], block->coeffs[382], block->coeffs[383]);

    // if (filter_type) {
    //     *finfo = fstrengths[block->segment_id][block->intra_y_mode==B_PRED];
    //     finfo->f_inner |= !skip;
    // }
    return 0;
}

static int
above_block_mode(const struct macro_block *this,
                 const struct macro_block *above,
                 unsigned int b)
{
    if (b < 4) {
        switch (above->intra_y_mode) {
        case DC_PRED:
            return B_DC_PRED;
        case V_PRED:
            return B_VE_PRED;
        case H_PRED:
            return B_HE_PRED;
        case TM_PRED:
            return B_TM_PRED;
        case B_PRED:
            return above->imodes[b + 12];
        default:
            assert(0);
        }
    }

    return this->imodes[b - 4];
}

static int
left_block_mode(const struct macro_block *this,
                const struct macro_block *left, unsigned int b)
{
    if (!(b & 3)) {
        switch (left->intra_y_mode) {
        case DC_PRED:
            return B_DC_PRED;
        case V_PRED:
            return B_VE_PRED;
        case H_PRED:
            return B_HE_PRED;
        case TM_PRED:
            return B_TM_PRED;
        case B_PRED:
            return left->imodes[b + 3];
        default:
            assert(0);
        }
    }

    return this->imodes[b - 1];
}

static void
vp8_decode_mb_header(WEBP *w, bool_dec *bt, struct macro_block *mb, int y, int x)
{
    // prepare a fake left mb when ymode is B_PRED
    struct macro_block fake_left = {
        .intra_y_mode = 0,
    };
    int width = ((w->fi.width + 3) >> 2) << 2;
    int cols = (width + 15) >> 4;
    // int intra_size = (width + 15) >> 2;

    mb->x = x;

    // see 9.3.5, section 10, ref section 20.11
    if (w->k.segmentation.update_mb_segmentation_map) {
        mb->segment_id = !BOOL_DECODE(bt, w->k.segmentation.segment_prob[0]) ?
            BOOL_DECODE(bt, w->k.segmentation.segment_prob[1]) :
            BOOL_DECODE(bt, w->k.segmentation.segment_prob[2]) + 2;
    } else {
        mb->segment_id = 0;
    }
    // see section 11, section 9.11
    mb->mb_skip_coeff =
        (w->k.mb_no_skip_coeff) ? BOOL_DECODE(bt, w->k.prob_skip_false) : 0;

    // we have key frame only, is_inter_mb = 1
    // see section 11.2 or decode_kf_mb_mode
    static const int8_t kf_ymode_tree[8] = {
        -B_PRED, 2,         /* root: B_PRED = "0", "1" subtree */
        4, 6,               /* "1" subtree has 2 descendant subtrees */
        -DC_PRED, -V_PRED,  /* "10" subtree: DC_PRED = "100",
                                                    V_PRED = "101" */
        -H_PRED, -TM_PRED   /* "11" subtree: H_PRED = "110",
                                                   TM_PRED = "111" */
    };
    static const uint8_t kf_bmode_prob[NUM_BMODES][NUM_BMODES][NUM_BMODES - 1] =
        {{{231, 120, 48, 89, 115, 113, 120, 152, 112},
          {152, 179, 64, 126, 170, 118, 46, 70, 95},
          {175, 69, 143, 80, 85, 82, 72, 155, 103},
          {56, 58, 10, 171, 218, 189, 17, 13, 152},
          {114, 26, 17, 163, 44, 195, 21, 10, 173},
          {121, 24, 80, 195, 26, 62, 44, 64, 85},
          {144, 71, 10, 38, 171, 213, 144, 34, 26},
          {170, 46, 55, 19, 136, 160, 33, 206, 71},
          {63, 20, 8, 114, 114, 208, 12, 9, 226},
          {81, 40, 11, 96, 182, 84, 29, 16, 36}},
         {{134, 183, 89, 137, 98, 101, 106, 165, 148},
          {72, 187, 100, 130, 157, 111, 32, 75, 80},
          {66, 102, 167, 99, 74, 62, 40, 234, 128},
          {41, 53, 9, 178, 241, 141, 26, 8, 107},
          {74, 43, 26, 146, 73, 166, 49, 23, 157},
          {65, 38, 105, 160, 51, 52, 31, 115, 128},
          {104, 79, 12, 27, 217, 255, 87, 17, 7},
          {87, 68, 71, 44, 114, 51, 15, 186, 23},
          {47, 41, 14, 110, 182, 183, 21, 17, 194},
          {66, 45, 25, 102, 197, 189, 23, 18, 22}},
         {{88, 88, 147, 150, 42, 46, 45, 196, 205},
          {43, 97, 183, 117, 85, 38, 35, 179, 61},
          {39, 53, 200, 87, 26, 21, 43, 232, 171},
          {56, 34, 51, 104, 114, 102, 29, 93, 77},
          {39, 28, 85, 171, 58, 165, 90, 98, 64},
          {34, 22, 116, 206, 23, 34, 43, 166, 73},
          {107, 54, 32, 26, 51, 1, 81, 43, 31},
          {68, 25, 106, 22, 64, 171, 36, 225, 114},
          {34, 19, 21, 102, 132, 188, 16, 76, 124},
          {62, 18, 78, 95, 85, 57, 50, 48, 51}},
         {{193, 101, 35, 159, 215, 111, 89, 46, 111},
          {60, 148, 31, 172, 219, 228, 21, 18, 111},
          {112, 113, 77, 85, 179, 255, 38, 120, 114},
          {40, 42, 1, 196, 245, 209, 10, 25, 109},
          {88, 43, 29, 140, 166, 213, 37, 43, 154},
          {61, 63, 30, 155, 67, 45, 68, 1, 209},
          {100, 80, 8, 43, 154, 1, 51, 26, 71},
          {142, 78, 78, 16, 255, 128, 34, 197, 171},
          {41, 40, 5, 102, 211, 183, 4, 1, 221},
          {51, 50, 17, 168, 209, 192, 23, 25, 82}},
         {{138, 31, 36, 171, 27, 166, 38, 44, 229},
          {67, 87, 58, 169, 82, 115, 26, 59, 179},
          {63, 59, 90, 180, 59, 166, 93, 73, 154},
          {40, 40, 21, 116, 143, 209, 34, 39, 175},
          {47, 15, 16, 183, 34, 223, 49, 45, 183},
          {46, 17, 33, 183, 6, 98, 15, 32, 183},
          {57, 46, 22, 24, 128, 1, 54, 17, 37},
          {65, 32, 73, 115, 28, 128, 23, 128, 205},
          {40, 3, 9, 115, 51, 192, 18, 6, 223},
          {87, 37, 9, 115, 59, 77, 64, 21, 47}},
         {{104, 55, 44, 218, 9, 54, 53, 130, 226},
          {64, 90, 70, 205, 40, 41, 23, 26, 57},
          {54, 57, 112, 184, 5, 41, 38, 166, 213},
          {30, 34, 26, 133, 152, 116, 10, 32, 134},
          {39, 19, 53, 221, 26, 114, 32, 73, 255},
          {31, 9, 65, 234, 2, 15, 1, 118, 73},
          {75, 32, 12, 51, 192, 255, 160, 43, 51},
          {88, 31, 35, 67, 102, 85, 55, 186, 85},
          {56, 21, 23, 111, 59, 205, 45, 37, 192},
          {55, 38, 70, 124, 73, 102, 1, 34, 98}},
         {{125, 98, 42, 88, 104, 85, 117, 175, 82},
          {95, 84, 53, 89, 128, 100, 113, 101, 45},
          {75, 79, 123, 47, 51, 128, 81, 171, 1},
          {57, 17, 5, 71, 102, 57, 53, 41, 49},
          {38, 33, 13, 121, 57, 73, 26, 1, 85},
          {41, 10, 67, 138, 77, 110, 90, 47, 114},
          {115, 21, 2, 10, 102, 255, 166, 23, 6},
          {101, 29, 16, 10, 85, 128, 101, 196, 26},
          {57, 18, 10, 102, 102, 213, 34, 20, 43},
          {117, 20, 15, 36, 163, 128, 68, 1, 26}},
         {{102, 61, 71, 37, 34, 53, 31, 243, 192},
          {69, 60, 71, 38, 73, 119, 28, 222, 37},
          {68, 45, 128, 34, 1, 47, 11, 245, 171},
          {62, 17, 19, 70, 146, 85, 55, 62, 70},
          {37, 43, 37, 154, 100, 163, 85, 160, 1},
          {63, 9, 92, 136, 28, 64, 32, 201, 85},
          {75, 15, 9, 9, 64, 255, 184, 119, 16},
          {86, 6, 28, 5, 64, 255, 25, 248, 1},
          {56, 8, 17, 132, 137, 255, 55, 116, 128},
          {58, 15, 20, 82, 135, 57, 26, 121, 40}},
         {{164, 50, 31, 137, 154, 133, 25, 35, 218},
          {51, 103, 44, 131, 131, 123, 31, 6, 158},
          {86, 40, 64, 135, 148, 224, 45, 183, 128},
          {22, 26, 17, 131, 240, 154, 14, 1, 209},
          {45, 16, 21, 91, 64, 222, 7, 1, 197},
          {56, 21, 39, 155, 60, 138, 23, 102, 213},
          {83, 12, 13, 54, 192, 255, 68, 47, 28},
          {85, 26, 85, 85, 128, 128, 32, 146, 171},
          {18, 11, 7, 63, 144, 171, 4, 4, 246},
          {35, 27, 10, 146, 174, 171, 12, 26, 128}},
         {{190, 80, 35, 99, 180, 80, 126, 54, 45},
          {85, 126, 47, 87, 176, 51, 41, 20, 32},
          {101, 75, 128, 139, 118, 146, 116, 128, 85},
          {56, 41, 15, 176, 236, 85, 37, 9, 62},
          {71, 30, 17, 119, 118, 255, 17, 18, 138},
          {101, 38, 60, 138, 55, 70, 43, 26, 142},
          {146, 36, 19, 30, 171, 255, 97, 27, 20},
          {138, 45, 61, 62, 219, 1, 81, 188, 64},
          {32, 41, 20, 117, 151, 142, 20, 21, 163},
          {112, 19, 12, 61, 195, 128, 48, 4, 24}}};

    static const int8_t bmode_tree[18] = {
        -B_DC_PRED, 2,                          /* B_DC_PRED = "0" */
        -B_TM_PRED, 4,                          /* B_TM_PRED = "10" */
        -B_VE_PRED, 6,                          /* B_VE_PRED = "110" */
        8,          12,         -B_HE_PRED, 10, /* B_HE_PRED = "11100" */
        -B_RD_PRED, -B_VR_PRED,                 /* B_RD_PRED = "111010",
                                                             B_VR_PRED = "111011" */
        -B_LD_PRED, 14,                         /* B_LD_PRED = "111110" */
        -B_VL_PRED, 16,                         /* B_VL_PRED = "1111110" */
        -B_HD_PRED, -B_HU_PRED                  /* HD = "11111110",
                                                             HU = "11111111" */
    };
    static const uint8_t kf_ymode_prob[4] = {145, 156, 163, 128};
    int intra_y_mode = BOOL_TREE(bt, kf_ymode_tree, kf_ymode_prob);
    mb->intra_y_mode = intra_y_mode;
    assert(intra_y_mode <= NUM_PRED_MODES);
    mb->imodes[0] = intra_y_mode;
    if (intra_y_mode == B_PRED) {
        // Paragraph 11.5
        struct macro_block *above = mb - cols;
        for (int i = 0; i < 16; i++) {
            int a = above_block_mode(mb, above, i);
            int l = left_block_mode(mb, (x > 0) ? (mb - 1 ) : &fake_left, i);
            int intra_b_mode = BOOL_TREE(bt, bmode_tree, kf_bmode_prob[a][l]);
            mb->imodes[i] = intra_b_mode;
        }
    }

    const int8_t uv_mode_tree[6] = {
        -DC_PRED, 2,      /* root: DC_PRED = "0", "1" subtree */
        -V_PRED, 4,       /* "1" subtree:  V_PRED = "10", "11" subtree */
        -H_PRED, -TM_PRED /* "11" subtree: H_PRED = "110",
                                                   TM_PRED = "111" */
    };
    const uint8_t kf_uv_mode_prob[3] = {142, 114, 183};
    mb->intra_uv_mode = BOOL_TREE(bt, uv_mode_tree, kf_uv_mode_prob);
    // VDBG(webp, "y %d, x %d: ymode %d, uvmode %d", y, x, mb->intra_y_mode, mb->intra_uv_mode);
}

// typedef struct {
//     uint8_t y[16], u[8], v[8];
// } topsamples;

typedef struct {
    uint8_t *m;
    uint8_t *y;
    uint8_t *u;
    uint8_t *v;
    int y_stride;
    int uv_stride;
} cache;

// //see H264 table 7-8
// int intra16x16PredMode [] = {
//     0, 1, 2, 3,
//     0, 1, 2, 3,
//     0, 1, 2, 3,
//     0, 1, 2, 3,
//     0, 1, 2, 3,
//     0, 1, 2, 3,
// };

/* see h264 8.3 */
static void
vp8_prerdict_mb(WEBP *w, struct macro_block *block, int y,
                uint8_t* yout, uint8_t* uout, uint8_t* vout,
                int y_stride, int uv_stride)
{
    // int width = ((w->fi.width + 3) >> 2) << 2;
    // int height = w->fi.height;

    //predict luma
        // struct macro_block *mb = blocks + y * ((width + 15) >> 4) + x;
        // VP8FInfo *finfo = finfos + y *((width + 15) >> 4) + x;

        // topsamples* const top_yuv = &yuv_t;
        const int16_t* coeffs = block->coeffs;
        // uint32_t bits = mb->non_zero_y;
        // int n;

            // printf("x %d y %d\n",block->x, y);


        // VDBG(webp, "left %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
        // left[0], left[1], left[2], left[3], left[4], left[5], left[6], left[7],
        // left[8], left[9], left[10], left[11], left[12], left[13], left[14], left[15]);

        // VDBG(webp, "ymode %d, imodes0 %d", block->intra_y_mode, block->imodes[0]);

        pred_luma(block->intra_y_mode, block->imodes, yout, y_stride, block->x, y);
        // VDBG(webp, "y %d, x %d, pred%d %d:", y, block->x, block->intra_y_mode == B_PRED ? 4 : 16, block->imodes[0]);

        pred_chrome(block->intra_uv_mode, uout, vout, uv_stride, block->x, y);
        VDBG(webp, "y %d, x %d, mode %d:", y, block->x, block->intra_uv_mode);
        mb_dump(vlog_get_stream(), "", uout, 8, uv_stride);
        mb_dump(vlog_get_stream(), "", vout, 8, uv_stride);

        // Chroma
        // const uint32_t bits_uv = mb->non_zero_uv;
        // const int pred_func = CheckMode(x, y, mb->intra_uv_mode);
        // VP8PredChroma8[pred_func](u_dst);
        // VP8PredChroma8[pred_func](v_dst);
        // DoUVTransform(bits_uv >> 0, coeffs + 16 * 16, u_dst);
        // DoUVTransform(bits_uv >> 8, coeffs + 20 * 16, v_dst);

        // // stash away top samples for next block
        // if (y < ((height + 15) >> 4)- 1) {
        //     memcpy(top_yuv[0].y, y_dst + 15 * BPS, 16);
        //     memcpy(top_yuv[0].u, u_dst +  7 * BPS,  8);
        //     memcpy(top_yuv[0].v, v_dst +  7 * BPS,  8);
        // }
    
        // // Transfer reconstructed samples from yuv_b cache to final destination.
    
        // const int y_offset = cache_id * 16 * y_stride;
        // const int uv_offset = cache_id * 8 * uv_stride;
        // uint8_t* const y_out = c->y + block->x * 16 + y_offset;
        // uint8_t* const u_out = c->u + block->x * 8 + uv_offset;
        // uint8_t* const v_out = c->v + block->x * 8 + uv_offset;
        // // printf("Y: ");
        // for (int j = 0; j < 16; ++j) {
        //     memcpy(y_out + j * y_stride, y_dst + j * BPS, 16);
        //     // for (int ls = 0; ls < 16; ls ++) {
        //     // printf("%d ", *(y_out + j * y_stride + ls));
        //     // }
        //     // printf("\n");
        // }
        // for (int j = 0; j < 8; ++j) {
        //     memcpy(u_out + j * c->uv_stride, u_dst + j * BPS, 8);
        //     memcpy(v_out + j * c->uv_stride, v_dst + j * BPS, 8);
        // }
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
                        int thresh, int ithresh, int hev_thresh)
{
    FilterLoop26_C(p, stride, 1, 16, thresh, ithresh, hev_thresh);
}

static void HFilter16_C(uint8_t* p, int stride,
                        int thresh, int ithresh, int hev_thresh)
{
    FilterLoop26_C(p, 1, stride, 16, thresh, ithresh, hev_thresh);
}

// on three inner edges
static void VFilter16i_C(uint8_t* p, int stride,
                         int thresh, int ithresh, int hev_thresh)
{
    for (int k = 3; k > 0; --k) {
        p += 4 * stride;
        FilterLoop24_C(p, stride, 1, 16, thresh, ithresh, hev_thresh);
    }
}

static void HFilter16i_C(uint8_t* p, int stride,
                         int thresh, int ithresh, int hev_thresh)
{
    for (int k = 3; k > 0; --k) {
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
loopfilter(WEBP *w, int filter_type, int y, cache *c, VP8FInfo *finfos)
{
    int filter_now = (filter_type > 0);
    int height = w->fi.height;
    const int extra_y_rows = filter_type * 3 - 1;
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
    struct macro_block *block;
    
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int mbrows = (height + 15) >> 4;
    int mbcols = (width + 15) >> 4;
    int y_stride = mbcols * 16;       // 16 * 16 Y
    int uv_stride = y_stride >> 1;  // 8 * 8   U

    //reserve YUV data
    // w->data = malloc(mbrows * (y_stride * 16 + uv_stride * 2 * 8));
    uint8_t *Y = malloc(y_stride * 16 * mbrows);
    uint8_t *U = malloc(mbrows * 8 * uv_stride);
    uint8_t *V = malloc(mbrows * 8 * uv_stride);

    precompute_filter(w);

    struct macro_block *blocks = malloc(sizeof(struct macro_block)* (mbcols * mbrows));
    // one more struct for left of zero col
    struct context *top = calloc(mbcols, sizeof(struct context));

    VDBG(webp, "rows %d, cols %d\n", mbrows, mbcols);

    // Section 19.3: Macroblock header & Data
    for (int y = 0; y < mbrows; y++) {

        // parse intra mode here or in advance
        bool_dec *bt = btree[y & (w->k.nbr_partitions - 1)];

        // left part for each row is independent
        struct context left = { .ctx = {0,}};
        // from first partition
        for (int x = 0; x < mbcols; x++) {
            block = blocks + y * mbcols + x;
            vp8_decode_mb_header(w, br, block, y, x);
            vp8_decode_residual_data(w, block, bt, &left, top, NULL);
        }
    }

    for (int y = 0; y < mbrows; y++) {
        for (int x = 0; x < mbcols; x++) {
            block = blocks + y * mbcols + x;
            uint8_t *yout = Y + y_stride * y * 16 + x * 16;
            uint8_t *uout = U + 8 * uv_stride * y + x * 8;
            uint8_t *vout = V + 8 * uv_stride * y + x * 8;
            vp8_prerdict_mb(w, block, y, yout, uout, vout, y_stride, uv_stride);
        }
    }

    int filter_type = (w->k.loop_filter_level == 0) ? 0 :
            w->k.filter_type ? 1 : 2;
    uint8_t *yuv_b = malloc(32 * 17 + 32 * 9);
    int extra_rows = kFilterExtraRows[filter_type];
    for (int y = 0; y < mbrows; y++) {
        for (int x = 0; x < mbcols; x++) {
            // loopfilter(w, x, y, block);
        }
    }
    // cache c;
    // c.m = malloc((cols * sizeof(topsamples)) *
    //                 ((16 * 1 + extra_rows) * 3 / 2));

    // c.y = c.m + extra_rows * c.y_stride;
    // c.u = c.y + 16 * c.y_stride + (extra_rows / 2) * c.uv_stride;
    // c.v = c.u + 8 * c.uv_stride + (extra_rows / 2) * c.uv_stride;
    // printf("y %d, u %d, v %d, extra %d, cache size %ld\n", extra_rows
    // * c.y_stride, 16 * c.y_stride + (extra_rows / 2) * c.uv_stride, 8
    // * c.uv_stride + (extra_rows / 2) * c.uv_stride, extra_rows,
    //     (cols * sizeof(topsamples)) * ((16*1 + extra_rows) * 3 / 2));

    /* reconstruct the row with filter */
    // for (int y = 0; y < (height + 15) >> 4; y ++) {
    //     reconstruct_row(w, blocks, yuv_b, y, &c, NULL);
    //     finish_row(w, filter_type, y, &c, NULL);
    // }

    // free(yuv_b);
    // free(c.m);
}

int WEBP_read_frame(WEBP *w, FILE *f)
{
    unsigned char b[3]; // code for I frame 10byte， P frame 3byte.
    fread(&w->fh, sizeof(w->fh), 1, f);

    if (w->fh.frame_type != KEY_FRAME) {
        VERR(webp, "not a key frame for vp8\n");
        return -1;
    }

    /* key frame, more info */
    fread(&w->fi, 7, 1, f);
    if (w->fi.start1 != 0x9d || w->fi.start2 != 0x01 || w->fi.start3 != 0x2a) {
        VERR(webp, "not a valid start code for vp8\n");
        return -1;
    }
    int partition0_size = ((int)w->fh.size_h | w->fh.size << 3);

    uint8_t *buf = malloc(partition0_size);
    fread(buf, partition0_size, 1, f);

    struct bool_dec *first_bt = bool_dec_init(buf, partition0_size);

    read_vp8_ctl_partition(w, first_bt, f);

    /* Quato From 9.11:
       The remainder of the first data partition consists of macroblock-level
        prediction data. 
       After the frame header is processed, all probabilities needed to decode
        the prediction and residue data are known and will not change until the
        next frame.
    */
    bool_dec *bt[MAX_PARTI_NUM];
    for (int i = 0; i < w->k.nbr_partitions; i++) {
        uint8_t *parts = malloc(w->p[i].len);
        fseek(f, w->p[i].start, SEEK_SET);
        fread(parts, 1, w->p[i].len, f);
        VDBG(webp, "part %d: len %d, 0x%x", i, w->p[i].len, parts[0]);
        // hexdump(stdout, "partitions", parts, 120);
        bt[i] = bool_dec_init(parts, w->p[i].len);
    }
    // bits_vec_dump(first_bt->bits);

    vp8_decode(w, first_bt, bt);

    // free all decoders
    bool_dec_free(first_bt);
    for (int i = 0; i < w->k.nbr_partitions; i++) {
        bool_dec_free(bt[i]);
    }
    return 0;
}

static struct pic* 
WEBP_load(const char *filename)
{
    struct pic *p = pic_alloc(sizeof(WEBP));
    WEBP *w = p->pic;
    FILE *f = fopen(filename, "rb");
    // read riff 12 bytes header
    fread(&w->header, sizeof(w->header), 1, f);
    if (w->header.riff != CHUNCK_HEADER("RIFF") ||
        w->header.webp != CHUNCK_HEADER("WEBP")) {
        pic_free(p);
        fclose(f);
        return NULL;
    }

    uint32_t chead;
    uint32_t chunk_size;
    while (!feof(f)) {
        fread(&chead, 4, 1, f);
        if (chead == CHUNCK_HEADER("VP8X")) {
            fseek(f, -4, SEEK_CUR);
            fread(&w->vp8x, sizeof(struct webp_vp8x), 1, f);
            if (w->vp8x.size != sizeof(struct webp_vp8x) - 8) {
                pic_free(p);
                fclose(f);
                return NULL;
            }
            p->height = READ_UINT24(w->vp8x.canvas_height);
            p->width = READ_UINT24(w->vp8x.canvas_width);
        } else if (chead == CHUNCK_HEADER("ALPH")) {
            fseek(f, -4, SEEK_CUR);
            fread(&w->alpha, sizeof(struct webp_alpha), 1, f);
            if (w->alpha.size != sizeof(struct webp_alpha) - 8) {
                pic_free(p);
                fclose(f);
                return NULL;
            }
        } else if (chead == CHUNCK_HEADER("VP8 ")) {
            //VP8 data chuck
            fseek(f, -4, SEEK_CUR);
            fread(&w->vp8, sizeof(struct webp_vp8), 1, f);
            if (WEBP_read_frame(w, f) < 0) {
                pic_free(p);
                fclose(f);
                return NULL;
            }
            break;
        } else if (chead == CHUNCK_HEADER("VP8L")) {
            // VP8 lossless chuck
            fseek(f, -4, SEEK_CUR);
            fread(&w->vp8l, sizeof(struct webp_vp8l), 1, f);
            VINFO(webp, "VP8L\n");
            if (WEBP_read_frame(w, f) < 0) {
                pic_free(p);
                fclose(f);
                return NULL;
            }
            break;
        } else {
            // skip other chuck as optional
            fread(&chunk_size, 4, 1, f);
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    fclose(f);

    return p;
}

void 
WEBP_free(struct pic * p)
{
    pic_free(p);
}

void
WEBP_info(FILE *f, struct pic* p)
{
    WEBP * w = (WEBP *)p->pic;
    fprintf(f, "%s file format:\n",
            ((w->header.webp == CHUNCK_HEADER("WEBP")) ? "WEBP" : "unknown"));
    fprintf(f, "\tfile size: %d\n", w->header.file_size);
    fprintf(f, "----------------------------------\n");
    if (w->vp8x.vp8x == CHUNCK_HEADER("VP8X")) {
        fprintf(f, "Chunk VP8X length %d:\n", w->vp8x.size);
        fprintf(f, "\tVP8X icc %d, alpha %d, exif %d, xmp %d, animation %d\n",
            w->vp8x.icc, w->vp8x.alpha, w->vp8x.exif_metadata, w->vp8x.xmp_metadata, w->vp8x.animation);
        fprintf(f, "\tVP8X canvas witdth %d, height %d\n", READ_UINT24(w->vp8x.canvas_width),
            READ_UINT24(w->vp8x.canvas_height));
    }
    if (w->vp8.vp8 == CHUNCK_HEADER("VP8 ")) {
        fprintf(f, "Chunk VP8  length %d:\n", w->vp8.size);
    }
    if (w->vp8l.vp8l == CHUNCK_HEADER("VP8L")) {
        fprintf(f, "Chunk VP8L length %d:\n", w->vp8l.size);
    }
    int size = ((int)w->fh.size_h | w->fh.size << 3);
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
                fprintf(f, " %d", w->k.segmentation.segment_prob[i]);
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
