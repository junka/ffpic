#include <stdint.h>

#define A32_BASE 65521
#define A32_NMAX 5552

uint32_t 
adler32(const void *data, unsigned int length)
{
	const uint8_t *buf = (const uint8_t *) data;

	unsigned int s1 = 1;
	unsigned int s2 = 0;

	while (length > 0) {
		int k = length < A32_NMAX ? length : A32_NMAX;
		int i;

		for (i = k / 16; i; --i, buf += 16) {
			s1 += buf[0];
			s2 += s1;
			s1 += buf[1];
			s2 += s1;
			s1 += buf[2];
			s2 += s1;
			s1 += buf[3];
			s2 += s1;
			s1 += buf[4];
			s2 += s1;
			s1 += buf[5];
			s2 += s1;
			s1 += buf[6];
			s2 += s1;
			s1 += buf[7];
			s2 += s1;

			s1 += buf[8];
			s2 += s1;
			s1 += buf[9];
			s2 += s1;
			s1 += buf[10];
			s2 += s1;
			s1 += buf[11];
			s2 += s1;
			s1 += buf[12];
			s2 += s1;
			s1 += buf[13];
			s2 += s1;
			s1 += buf[14];
			s2 += s1;
			s1 += buf[15];
			s2 += s1;
		}

		for (i = k % 16; i; --i) {
			s1 += *buf++;
			s2 += s1;
		}

		s1 %= A32_BASE;
		s2 %= A32_BASE;

		length -= k;
	}

	return (s2 << 16) | s1;
}