#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

#define LINE_LEN 128

void
hexdump(FILE *f, const char *title, const void *buf, unsigned int len)
{
	unsigned int i, out, ofs;
	const unsigned char *data = buf;
	char line[LINE_LEN];	/* space needed 8+16*3+3+16 == 75 */

	fprintf(f, "%s at [%p], len=%u\n",
		title ? : "  Dump data", data, len);
	ofs = 0;
	while (ofs < len) {
		/* format the line in the buffer */
		out = snprintf(line, LINE_LEN, "%08X:", ofs);
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