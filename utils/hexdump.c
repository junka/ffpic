#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

#define LINE_LEN 128

void
hexdump(FILE *f, const char *title, const char *prefix, const void *buf, unsigned int len)
{
    unsigned int i, out, ofs;
    const unsigned char *data = buf;
    char line[LINE_LEN];    /* space needed 8+16*3+3+16 == 75 */

    fprintf(f, "%s at [%p], len=%u\n",
        title ? title : "  Dump data", (void *)data, len);
    ofs = 0;
    while (ofs < len) {
        /* format the line in the buffer */
        out = snprintf(line, LINE_LEN, "%s%08X:", prefix, ofs);
        for (i = 0; i < 16; i++) {
            if (ofs + i < len)
                snprintf(line + out, LINE_LEN - out,
                     " %02X", (data[ofs + i] & 0xff));
            else
                strcpy(line + out, "   ");
            out += 3;
        }


        for (; i <= 16; i++)
            out += snprintf(line + out, LINE_LEN - out, " | ");

        for (i = 0; ofs < len && i < 16; i++, ofs++) {
            unsigned char c = data[ofs];

            if (c < ' ' || c > '~')
                c = '.';
            out += snprintf(line + out, LINE_LEN - out, "%c", c);
        }
        fprintf(f, "%s\n", line);
    }
    fflush(f);
}


// for macro block size 8 * 8, 4 * 4, 16 * 16
void mb_dump(FILE *f, const char *title, const uint8_t *buf, int size, int stride)
{
    fprintf(f, "%s:\n", title);
    for (int i = 0; i < size; i ++) {
        for (int j = 0; j < size; j ++) {
            fprintf(f, "%02x ", *(buf+j));
        }
        buf += stride;
        fprintf(f, "\n");
    }
}

//for block int16_t
void block_dump(FILE *f, const char *title, const int16_t *buf, int size)
{
    fprintf(f, "%s:\n", title);
    for (int i = 0; i < size; i ++) {
        for (int j = 0; j < size; j ++) {
            fprintf(f, "%02d ", *(buf+j));
        }
        buf += size;
        fprintf(f, "\n");
    }
}
