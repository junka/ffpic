#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>

#include "file.h"
#include "jpg.h"

#include "huffman.h"



struct decoder {
    int prev_dc;
    huffman_tree *dc;
    huffman_tree *ac;
    uint16_t *quant;
    uint16_t buf[64];
};


int JPG_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    uint16_t soi, eoi;
    int len = fread(&soi, 2, 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fseek(f, -2, SEEK_END);
    len = fread(&eoi, 2, 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (SOI == soi && eoi == EOI)
        return 0;

    return -EINVAL;
}

uint16_t 
read_marker_skip_null(FILE *f)
{
    uint8_t c = fgetc(f);
    if (c != 0xFF) {
        printf("marker 0x%x\n", c);
        return 0;
    }
    while (c == 0xFF) {
        c = fgetc(f);
    }
    return MARKER(c);
}

void 
idct(int input[][8], int output[][8])
{
    static double cosine[8][8] = {
        {0.707107,  0.980785,  0.923880,  0.831470,  0.707107,  0.555570,  0.382683,  0.195090 },
        {0.707107,  0.831470,  0.382683, -0.195090, -0.707107, -0.980785, -0.923880, -0.555570 },
        {0.707107,  0.555570, -0.382683, -0.980785, -0.707107,  0.195090,  0.923880,  0.831470 },
        {0.707107,  0.195090, -0.923880, -0.555570,  0.707107,  0.831470, -0.382683, -0.980785 },
        {0.707107, -0.195090, -0.923880,  0.555570,  0.707107, -0.831470, -0.382683,  0.980785 },
        {0.707107, -0.555570, -0.382683,  0.980785, -0.707107, -0.195090,  0.923880, -0.831470 },
        {0.707107, -0.831470,  0.382683,  0.195090, -0.707107,  0.980785, -0.923880,  0.555570 },
        {0.707107, -0.980785,  0.923880, -0.831470,  0.707107, -0.555570,  0.382683, -0.195090 }
    };
    for( int i  = 0; i < 8; i++ ) {
        for( int j = 0; j < 8; j++ ) {
            double tmp = 0.0;
            for( int u = 0; u < 8; u++ ) {
                for( int v = 0; v < 8; v++ ) {
                    tmp += (double)input[u][v] * cosine[i][u] * cosine[j][v];
                }
            }
            tmp /= 4;
            output[i][j] = (int)tmp;
        }
    }
}

void 
read_dqt(JPG *j, FILE *f)
{
    uint8_t c;
    uint16_t len;
    fread(&len, 2, 1, f);
    c = fgetc(f);
    uint8_t id = (c & 0xF);
    j->dqt[id].len = len;
    j->dqt[id].id = id;
    j->dqt[id].precision = (c >> 4);
    j->dqt[id].tdata = malloc((j->dqt[id].precision + 1) << 6);
    for (int i = 0; i < 64; i++) {
        fread(j->dqt[id].tdata + i , j->dqt[id].precision + 1, 1, f);
    }
}

void
read_dht(JPG* j, FILE *f)
{
    struct dht d;
    fread(&d, 3, 1, f);
    uint8_t ac = d.table_class;
    uint8_t id = d.huffman_id;
    j->dht[ac][id].len = ntohs(d.len);
    j->dht[ac][id].table_class = d.table_class;
    j->dht[ac][id].huffman_id = d.huffman_id;
    fread(j->dht[ac][id].num_codecs, 16, 1, f);
    int len = 0;
    for (int i =0; i < 16; i ++) {
        len += j->dht[ac][id].num_codecs[i];
    }
    j->dht[ac][id].data = malloc(len);
    fread(j->dht[ac][id].data, len, 1, f);
}

void
convert_YCbCr_to_RGB(uint8_t src[3], uint8_t dst[3])
{
    uint8_t Y, Cr, Cb, r, g, b;
    Y = src[0];
    Cb = src[1];
    Cr = src[2];
    Cr = Cr - 128 ;
    Cb = Cb - 128 ;
    r = Y + ((Cr >> 2) + (Cr >> 3) + (Cr >> 5)) ;
    g = Y - ((Cb >> 2) + (Cb >> 4) + (Cb >> 5)) - ((Cr >> 1) + (Cr >> 3) + (Cr >> 4) + (Cr >> 5));
    b = Y + (Cb + (Cb >> 1) + (Cb >> 2) + (Cb >> 6)) ;
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
}


static int 
get_vli(int code, int bitlen)
{
	if (!bitlen) return 0;

    // if msb is not 0, a negtiva value
	if ((code << 1) < (1 << bitlen))
		return code + 1 - (1 << bitlen);
    // else is code itself
	return code;
}


#define FAST_FLOAT float
#define DCTSIZE	   8
#define DCTSIZE2   (DCTSIZE*DCTSIZE)

#define DEQUANTIZE(coef,quantval)  (((FAST_FLOAT) (coef)) * (quantval))

static unsigned char clamp(int i)
{
	if (i < 0)
		return 0;
	else if (i > 255)
		return 255;
	else
		return i;
}

static inline unsigned char descale_and_clamp(int x, int shift)
{
  x += (1UL<<(shift-1));
  if (x<0)
    x = (x >> shift) | ((~(0UL)) << (32-(shift)));
  else
    x >>= shift;
  x += 128;
  if (x>255)
    return 255;
  else if (x<0)
    return 0;
  else
    return x;
}

/*
 * Perform dequantization and inverse DCT on one block of coefficients.
 */

void
idct_float(struct decoder *d, uint8_t *output_buf, int stride)
{
  FAST_FLOAT tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  FAST_FLOAT tmp10, tmp11, tmp12, tmp13;
  FAST_FLOAT z5, z10, z11, z12, z13;
  uint16_t *inptr;
  uint16_t *quantptr;
  FAST_FLOAT *wsptr;
  uint8_t *outptr;
  int ctr;
  FAST_FLOAT workspace[DCTSIZE2]; /* buffers data between passes */

  /* Pass 1: process columns from input, store into work array. */

  inptr = d->buf;
  quantptr = d->quant;
  wsptr = workspace;
  for (ctr = DCTSIZE; ctr > 0; ctr--) {
    /* Due to quantization, we will usually find that many of the input
     * coefficients are zero, especially the AC terms.  We can exploit this
     * by short-circuiting the IDCT calculation for any column in which all
     * the AC terms are zero.  In that case each output is equal to the
     * DC coefficient (with scale factor as needed).
     * With typical images and quantization tables, half or more of the
     * column DCT calculations can be simplified this way.
     */
    
    if (inptr[DCTSIZE*1] == 0 && inptr[DCTSIZE*2] == 0 &&
	inptr[DCTSIZE*3] == 0 && inptr[DCTSIZE*4] == 0 &&
	inptr[DCTSIZE*5] == 0 && inptr[DCTSIZE*6] == 0 &&
	inptr[DCTSIZE*7] == 0) {
      /* AC terms all zero */
      FAST_FLOAT dcval = DEQUANTIZE(inptr[DCTSIZE*0], quantptr[DCTSIZE*0]);
      
      wsptr[DCTSIZE*0] = dcval;
      wsptr[DCTSIZE*1] = dcval;
      wsptr[DCTSIZE*2] = dcval;
      wsptr[DCTSIZE*3] = dcval;
      wsptr[DCTSIZE*4] = dcval;
      wsptr[DCTSIZE*5] = dcval;
      wsptr[DCTSIZE*6] = dcval;
      wsptr[DCTSIZE*7] = dcval;
      
      inptr++;			/* advance pointers to next column */
      quantptr++;
      wsptr++;
      continue;
    }
    
    /* Even part */

    tmp0 = DEQUANTIZE(inptr[DCTSIZE*0], quantptr[DCTSIZE*0]);
    tmp1 = DEQUANTIZE(inptr[DCTSIZE*2], quantptr[DCTSIZE*2]);
    tmp2 = DEQUANTIZE(inptr[DCTSIZE*4], quantptr[DCTSIZE*4]);
    tmp3 = DEQUANTIZE(inptr[DCTSIZE*6], quantptr[DCTSIZE*6]);

    tmp10 = tmp0 + tmp2;	/* phase 3 */
    tmp11 = tmp0 - tmp2;

    tmp13 = tmp1 + tmp3;	/* phases 5-3 */
    tmp12 = (tmp1 - tmp3) * ((FAST_FLOAT) 1.414213562) - tmp13; /* 2*c4 */

    tmp0 = tmp10 + tmp13;	/* phase 2 */
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;
    
    /* Odd part */

    tmp4 = DEQUANTIZE(inptr[DCTSIZE*1], quantptr[DCTSIZE*1]);
    tmp5 = DEQUANTIZE(inptr[DCTSIZE*3], quantptr[DCTSIZE*3]);
    tmp6 = DEQUANTIZE(inptr[DCTSIZE*5], quantptr[DCTSIZE*5]);
    tmp7 = DEQUANTIZE(inptr[DCTSIZE*7], quantptr[DCTSIZE*7]);

    z13 = tmp6 + tmp5;		/* phase 6 */
    z10 = tmp6 - tmp5;
    z11 = tmp4 + tmp7;
    z12 = tmp4 - tmp7;

    tmp7 = z11 + z13;		/* phase 5 */
    tmp11 = (z11 - z13) * ((FAST_FLOAT) 1.414213562); /* 2*c4 */

    z5 = (z10 + z12) * ((FAST_FLOAT) 1.847759065); /* 2*c2 */
    tmp10 = ((FAST_FLOAT) 1.082392200) * z12 - z5; /* 2*(c2-c6) */
    tmp12 = ((FAST_FLOAT) -2.613125930) * z10 + z5; /* -2*(c2+c6) */

    tmp6 = tmp12 - tmp7;	/* phase 2 */
    tmp5 = tmp11 - tmp6;
    tmp4 = tmp10 + tmp5;

    wsptr[DCTSIZE*0] = tmp0 + tmp7;
    wsptr[DCTSIZE*7] = tmp0 - tmp7;
    wsptr[DCTSIZE*1] = tmp1 + tmp6;
    wsptr[DCTSIZE*6] = tmp1 - tmp6;
    wsptr[DCTSIZE*2] = tmp2 + tmp5;
    wsptr[DCTSIZE*5] = tmp2 - tmp5;
    wsptr[DCTSIZE*4] = tmp3 + tmp4;
    wsptr[DCTSIZE*3] = tmp3 - tmp4;

    inptr++;			/* advance pointers to next column */
    quantptr++;
    wsptr++;
  }
  
  /* Pass 2: process rows from work array, store into output array. */
  /* Note that we must descale the results by a factor of 8 == 2**3. */

  wsptr = workspace;
  outptr = output_buf;
  for (ctr = 0; ctr < DCTSIZE; ctr++) {
    /* Rows of zeroes can be exploited in the same way as we did with columns.
     * However, the column calculation has created many nonzero AC terms, so
     * the simplification applies less often (typically 5% to 10% of the time).
     * And testing floats for zero is relatively expensive, so we don't bother.
     */
    
    /* Even part */

    tmp10 = wsptr[0] + wsptr[4];
    tmp11 = wsptr[0] - wsptr[4];

    tmp13 = wsptr[2] + wsptr[6];
    tmp12 = (wsptr[2] - wsptr[6]) * ((FAST_FLOAT) 1.414213562) - tmp13;

    tmp0 = tmp10 + tmp13;
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;

    /* Odd part */

    z13 = wsptr[5] + wsptr[3];
    z10 = wsptr[5] - wsptr[3];
    z11 = wsptr[1] + wsptr[7];
    z12 = wsptr[1] - wsptr[7];

    tmp7 = z11 + z13;
    tmp11 = (z11 - z13) * ((FAST_FLOAT) 1.414213562);

    z5 = (z10 + z12) * ((FAST_FLOAT) 1.847759065); /* 2*c2 */
    tmp10 = ((FAST_FLOAT) 1.082392200) * z12 - z5; /* 2*(c2-c6) */
    tmp12 = ((FAST_FLOAT) -2.613125930) * z10 + z5; /* -2*(c2+c6) */

    tmp6 = tmp12 - tmp7;
    tmp5 = tmp11 - tmp6;
    tmp4 = tmp10 + tmp5;

    /* Final output stage: scale down by a factor of 8 and range-limit */

    outptr[0] = descale_and_clamp((int)(tmp0 + tmp7), 3);
    outptr[7] = descale_and_clamp((int)(tmp0 - tmp7), 3);
    outptr[1] = descale_and_clamp((int)(tmp1 + tmp6), 3);
    outptr[6] = descale_and_clamp((int)(tmp1 - tmp6), 3);
    outptr[2] = descale_and_clamp((int)(tmp2 + tmp5), 3);
    outptr[5] = descale_and_clamp((int)(tmp2 - tmp5), 3);
    outptr[4] = descale_and_clamp((int)(tmp3 + tmp4), 3);
    outptr[3] = descale_and_clamp((int)(tmp3 - tmp4), 3);

    
    wsptr += DCTSIZE;		/* advance pointer to next row */
    outptr += stride;
  }
}


int IDCT2(float* dst, uint16_t * block)
{
    float trans_matrix[8][8] = {
        {0.3536,    0.3536,    0.3536,    0.3536,    0.3536,    0.3536,    0.3536,    0.3536,},
        {0.4904,    0.4157,    0.2778,    0.0975,   -0.0975,   -0.2778,   -0.4157,   -0.4904,},
        {0.4619,    0.1913,   -0.1913,   -0.4619,   -0.4619,   -0.1913,    0.1913,    0.4619,},
        {0.4157,   -0.0975,   -0.4904,   -0.2778,    0.2778,    0.4904,    0.0975,   -0.4157,},
        {0.3536,   -0.3536,   -0.3536,    0.3536,    0.3536,   -0.3536,   -0.3536,    0.3536,},
        {0.2778,   -0.4904,    0.0975,    0.4157,   -0.4157,   -0.0975,    0.4904,   -0.2778,},
        {0.1913,   -0.4619,    0.4619,   -0.1913,   -0.1913,    0.4619,   -0.4619,    0.1913,},
        {0.0975,   -0.2778,    0.4157,   -0.4904,    0.4904,   -0.4157,    0.2778,   -0.0975,},
    };

    float tmp[8][8];

    float t=0;
    int i,j,k;
    for(i=0;i<8;i++)  //same as A'*I
	{
        for(j=0;j<8;j++)
		{
            t = 0;
            for(k=0; k<8; k++)
			{
                t += trans_matrix[k][i] * block[k*8 + j]; //trans_matrix's ith column * block's jth column
			}
            tmp[i][j] = t;
        }
    }

    for(i=0; i<8; i++)  //same as tmp*A
	{
        for(j=0; j<8; j++)
		{
            t=0;
            for(k=0; k<8; k++)
			{
                t += tmp[i][k] * trans_matrix[k][j];
			}
            dst[i*8 + j] = t + 128;
        }
    }

    return 0;
}

void 
decode_data_unit(struct decoder *d)
{
    static const uint8_t zigzag[64] = {
        0,  1,  8,  16, 9,  2,  3,  10,
        17, 24, 32, 25, 18, 11, 4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13, 6,  7,  14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };
    // static const uint8_t zigzag[64] = {
    //     0,  1,  5,  6, 14, 15, 27, 28,
    //     2,  4,  7, 13, 16, 26, 29, 42,
    //     3,  8, 12, 17, 25, 30, 41, 43,
    //     9, 11, 18, 24, 31, 40, 44, 53,
    //     10, 19, 23, 32, 39, 45, 52, 54,
    //     20, 22, 33, 38, 46, 51, 55, 60,
    //     21, 34, 37, 47, 50, 56, 59, 61,
    //     35, 36, 48, 49, 57, 58, 62, 63
    // };
    int dc, ac;
    uint16_t *p = d->buf;
    huffman_tree *dc_tree = d->dc;
    huffman_tree *ac_tree = d->ac;
    
    dc = huffman_decode_symbol(dc_tree);
    dc = get_vli(huffman_read_symbol(dc), dc);
    dc += d->prev_dc;
    p[0] = d->prev_dc = dc;

    // read AC coff
    for (int i = 1; i < 64;) {
        ac = huffman_decode_symbol(ac_tree);
        int lead_zero = ac >> 4;
        // printf("leading zero %d, %x\n", lead_zero, ac);
        ac = (ac & 0xF);
        if (ac == EOB)
        {
            if (lead_zero == 15)
                lead_zero ++;
            //fill all left ac as zero
            else if (lead_zero == 0) 
                lead_zero = 64 - i;
        }
        while (lead_zero>0) {
            p[zigzag[i++]] = 0;
            lead_zero--;
        }

        if (ac) {
            ac = get_vli(huffman_read_symbol(ac), ac);
            p[zigzag[i++]] = ac;
        }
    }

    for (int i = 0; i < 64; i++)
	{
		p[zigzag[i]] *= d->quant[i];
	}
    
}

int 
init_decoder(JPG* j, struct decoder *d, uint8_t comp_id)
{
    if (comp_id >= j->sof.components_num)
        return -1;
    huffman_tree * dc_tree= huffman_tree_init();
    huffman_tree * ac_tree= huffman_tree_init();
    int dc_ht_id = j->sos.comps[comp_id].DC_entropy;
    int ac_ht_id = j->sos.comps[comp_id].AC_entropy;
    huffman_build_lookup_table(dc_tree, 0, dc_ht_id, j->dht[0][dc_ht_id].num_codecs, j->dht[0][dc_ht_id].data);
    huffman_build_lookup_table(ac_tree, 1, ac_ht_id, j->dht[1][ac_ht_id].num_codecs, j->dht[1][ac_ht_id].data);

    d->prev_dc = 0;
    d->dc = dc_tree;
    d->ac = ac_tree;
    d->quant = j->dqt[j->sof.colors[comp_id].qt_id].tdata;

    return 0;
}

void
destroy_decoder(struct decoder *d)
{
    huffman_cleanup(d->dc);
    huffman_cleanup(d->ac);

}



//v = H*V (2*2)
static void 
YCrCB_to_RGB24(JPG *j, uint8_t* Y, uint8_t* Cr, uint8_t* Cb, int v)
{
	uint8_t *p, *p2;
	int offset_to_next_row;

#define SCALEBITS       10
#define ONE_HALF        (1UL << (SCALEBITS-1))
#define FIX(x)          ((int)((x) * (1UL<<SCALEBITS) + 0.5))
    int width = j->sof.width % 8 == 0 ? j->sof.width : j->sof.width + 8 - (j->sof.width % 8);
	p = j->data;
	p2 = j->data + width * 3;

	offset_to_next_row = (width * 3 * 2) - 16 * 3;
	for (int i = 0; i < 8; i++) {

		for (int k = 0; k < 8; k++) {

			int y, cb, cr;
			int add_r, add_g, add_b;
			int r, g, b;

			cb = *Cb++ - 128;
			cr = *Cr++ - 128;
			add_r = FIX(1.40200) * cr + ONE_HALF;
			add_g = -FIX(0.34414) * cb - FIX(0.71414) * cr + ONE_HALF;
			add_b = FIX(1.77200) * cb + ONE_HALF;

			y = (*Y++) << SCALEBITS;
			r = (y + add_r) >> SCALEBITS;
			*p++ = clamp(r);
			g = (y + add_g) >> SCALEBITS;
			*p++ = clamp(g);
			b = (y + add_b) >> SCALEBITS;
			*p++ = clamp(b);

			y = (*Y++) << SCALEBITS;
			r = (y + add_r) >> SCALEBITS;
			*p++ = clamp(r);
			g = (y + add_g) >> SCALEBITS;
			*p++ = clamp(g);
			b = (y + add_b) >> SCALEBITS;
			*p++ = clamp(b);

			y = (Y[16 - 2]) << SCALEBITS;
			r = (y + add_r) >> SCALEBITS;
			*p2++ = clamp(r);
			g = (y + add_g) >> SCALEBITS;
			*p2++ = clamp(g);
			b = (y + add_b) >> SCALEBITS;
			*p2++ = clamp(b);

			y = (Y[16 - 1]) << SCALEBITS;
			r = (y + add_r) >> SCALEBITS;
			*p2++ = clamp(r);
			g = (y + add_g) >> SCALEBITS;
			*p2++ = clamp(g);
			b = (y + add_b) >> SCALEBITS;
			*p2++ = clamp(b);
		}
		Y += 16;
		p += offset_to_next_row;
		p2 += offset_to_next_row;
	}

#undef SCALEBITS
#undef ONE_HALF
#undef FIX

}


#define cY	0
#define cCb	1
#define cCr	2

void
JPG_decode_image(JPG* j, uint8_t* data, int len) {

    struct decoder *d = malloc(sizeof(struct decoder) *j->sof.components_num);
    int yn = j->sof.colors[0].horizontal * j->sof.colors[0].vertical;
    //j->sof.components_num should be 3 least here
    for (int i = 0; i < j->sof.components_num; i ++) {
        init_decoder(j, d+i, i);
    }
    // huffman_dump_table(d[0].dc);
    int ystride = j->sof.colors[0].vertical * 8;
    int xstride = j->sof.colors[0].horizontal * 8;
    int pitch = j->sof.width * 3;
    // int pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    int bytes_block = pitch * ystride;
    int bytes_mcu = xstride * 3;
    uint8_t *ptr;
    int width = j->sof.width % 8 == 0 ? j->sof.width : j->sof.width + 8 - (j->sof.width % 8);
    int height = j->sof.height % 8 == 0 ? j->sof.height : j->sof.height + 8 - (j->sof.height % 8);
    j->data = malloc(width * height * 4);
    printf("bytes per block %d, bytes per mcu %d\n", bytes_block, bytes_mcu);
#if 0
    #include "utils.h"
    hexdump(stdout, "jpg raw data", data, 16);
#endif
    huffman_decode_start(data, len);
    uint8_t Y[64*4], Cr[64], Cb[64];

    for (int y = 0; y < height / ystride; y++) {
        ptr = j->data + y * bytes_block;
        for (int x = 0; x < width; x += xstride) {
            // Y
            for (int cy = 0; cy < yn; cy++) {
                decode_data_unit(d);
                // idct_float(d, Y + cy*64, 8);
                IDCT2(Y + cy*64, d->buf);
            }

            //Cr
            decode_data_unit(d+1);
            // idct_float(d, Cr, 8);
            IDCT2(Cr, d->buf);

            //Cb
            decode_data_unit(d+2);
            // idct_float(d, Cb, 8);
            IDCT2(Cb, d->buf);

            YCrCB_to_RGB24(j, Y, Cr, Cb, yn);

            j->data += bytes_mcu;
        }
    }
    #include "utils.h"
    hexdump(stdout, "jpg decode data", j->data, 160);

    for (int i = 0; i < j->sof.components_num; i ++) {
        destroy_decoder(d+i);
    }

    huffman_decode_end();

}

void 
read_compressed_image(JPG* j, FILE *f)
{
    int width = j->sof.width % 8 == 0 ? j->sof.width : j->sof.width + 8 - (j->sof.width % 8);
    int height = j->sof.height % 8 == 0 ? j->sof.height : j->sof.height + 8 - (j->sof.height % 8);
    uint8_t* compressed = malloc(width * height *j->sof.components_num);
    uint8_t prev , c = fgetc(f);
    int l = 0;
    do {
        prev = c;
        c = fgetc(f);
        if (prev != 0xFF)
            compressed[l++] = prev;
        else if (prev == 0xFF && c == 0) {
            compressed[l++] = 0xFF;
            prev = 0;
            c = fgetc(f);
        }
    } while( prev != 0xFF || (prev == 0xFF && c == 0));

    // JPG_decode_image(j, compressed, l);
    fseek(f, -2, SEEK_CUR);
    free(compressed);
}


void 
read_sos(JPG* j, FILE *f)
{
    fread(&j->sos, 3, 1, f);
    j->sos.comps = malloc(sizeof(struct comp_sel) * j->sos.nums);
    fread(j->sos.comps, sizeof(struct comp_sel), j->sos.nums, f);
    uint8_t c = fgetc(f);
    j->sos.predictor_start = c;
    c = fgetc(f);
    j->sos.predictor_end = c;
    c = fgetc(f);
    j->sos.approx_bits_h = c >> 4;
    j->sos.approx_bits_l = c & 0xF;
    read_compressed_image(j, f);
}

struct pic* 
JPG_load(const char *filename)
{
    struct pic *p = calloc(1, sizeof(struct pic));
    JPG *j = calloc(1, sizeof(JPG));
    j->data = NULL;
    p->pic = j;
    FILE *f = fopen(filename, "rb");
    uint16_t soi, m, len;
    fread(&soi, 2, 1, f);
    m = read_marker_skip_null(f);
    while (m != EOI && m != 0) {
        switch(m) {
            case SOF0:
                fread(&j->sof, 8, 1, f);
                j->sof.len = ntohs(j->sof.len);
                j->sof.height = ntohs(j->sof.height);
                j->sof.width = ntohs(j->sof.width);
                j->sof.colors = malloc(j->sof.components_num * sizeof(struct jpg_component));
                fread(j->sof.colors, sizeof(struct jpg_component), j->sof.components_num, f);
                break;
            case APP0:
                fread(&j->app0, 16, 1, f);
                if (j->app0.xthumbnail*j->app0.ythumbnail) {
                    j->app0.data = malloc(3*j->app0.xthumbnail*j->app0.ythumbnail);
                    fread(j->app0.data, 3, j->app0.xthumbnail*j->app0.ythumbnail, f);
                }
                break;
            case DHT:
                read_dht(j, f);
                break;
            case DQT:
                read_dqt(j, f);
                break;
            case SOS:
                read_sos(j, f);
                break;
            case COM:
                fread(&j->comment, 2, 1, f);
                j->comment.len = ntohs(j->comment.len);
                j->comment.data = malloc(j->comment.len-2);
                fread(j->comment.data, j->comment.len-2, 1, f);
                break;
            default:
                fread(&len, 2, 1, f);
                fseek(f, len-2, SEEK_CUR);
                break;
        }
        m = read_marker_skip_null(f);
    }
    fclose(f);

    p->width = j->sof.width;
    p->height = j->sof.height;
    p->depth = 24;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = j->data;

    return p;
}

void 
JPG_free(struct pic *p)
{
    JPG *j = (JPG *)p->pic;
    if (j->comment.data)
        free(j->comment.data);
    if (j->app0.data)
        free(j->app0.data);
    if (j->sof.colors)
        free(j->sof.colors);
    for (uint8_t ac = 0; ac < 2; ac ++) {
        for (uint8_t i = 0; i < 16; i++) {
            if (j->dht[ac][i].huffman_id == i && j->dht[ac][i].table_class == ac) {
                free(j->dht[ac][i].data);
            }
        }
    }
    for (uint8_t i = 0; i < 4; i++) {
        if (j->dqt[i].id == i) {
            free(j->dqt[i].tdata);
        }
    }
    if (j->data) {
        free(j->data);
    }
    free(j);
    free(p);
}

void 
JPG_info(FILE *f, struct pic* p)
{
    JPG *j = (JPG *)p->pic;
    fprintf(f, "JPEG file format\n");
    fprintf(f, "\twidth %d, height %d\n", j->sof.width, j->sof.height);
    fprintf(f, "\tprecision %d, components num %d\n", j->sof.precision, j->sof.components_num);
    for (int i = 0; i < j->sof.components_num; i ++) {
        fprintf(f, "\t cid %d vertical %d, horizon %d, quatation id %d\n", j->sof.colors[i].cid, j->sof.colors[i].vertical, 
            j->sof.colors[i].horizontal, j->sof.colors[i].qt_id);
    }
    fprintf(f, "\tAPP0: %s version %d.%d\n", j->app0.identifier, j->app0.major, j->app0.minor);
    fprintf(f, "\tAPP0: xdensity %d, ydensity %d %s\n", j->app0.xdensity, j->app0.ydensity,
        j->app0.unit == 0 ? "pixel aspect ratio" : 
        ( j->app0.unit == 1 ? "dots per inch": (j->app0.unit == 2 ? "dots per cm":"")));
    for (uint8_t i = 0; i < 4; i++) {
        if (j->dqt[i].id == i) {
            fprintf(f, "\tDQT %d: precision %d\n\t\t", i, j->dqt[i].precision);
            for (int k = 0; k < ((j->dqt[i].precision +1) << 6); k ++ ) {
                fprintf(f, "%x ", j->dqt[i].tdata[k]);
            }
            fprintf(f, "\n");
        }
    }
    fprintf(f, "-----------------------\n");
    for (uint8_t ac = 0; ac < 2; ac ++) {
        for (uint8_t i = 0; i < 16; i++) {
            if (j->dht[ac][i].huffman_id == i && j->dht[ac][i].table_class == ac) {
                fprintf(f, "\t%s DHT %d:", (j->dht[ac][i].table_class == 0 ? "DC" : "AC"), i);
                int len = 0;
                for(int k= 0; k < 16; k ++) {
                    fprintf(f, " %02d",  j->dht[ac][i].num_codecs[k]);
                    len += j->dht[ac][i].num_codecs[k];
                }
                fprintf(f, "\n");
    #if 1
                fprintf(f, "\t\t");
                for(int k= 0; k < len; k ++) {
                    fprintf(f, "%x ",  j->dht[ac][i].data[k]);
                }
                fprintf(f, "\n");
    #endif
            }
        }
    }
    fprintf(f, "-----------------------\n");
    fprintf(f, "\tsos: component num %d\n", j->sos.nums);
    for (int i = 0; i < j->sos.nums; i ++) {
        fprintf(f, "\t component id %d DC %d, AC %d\n", j->sos.comps[i].component_selector,
             j->sos.comps[i].DC_entropy, j->sos.comps[i].AC_entropy);
    }
    
    if (j->comment.data) {
        fprintf(f, "\tComment: %s\n", j->comment.data);
    }
}


static struct file_ops jpg_ops = {
    .name = "JPG",
    .probe = JPG_probe,
    .load = JPG_load,
    .free = JPG_free,
    .info = JPG_info,
};

void 
JPG_init(void)
{
    file_ops_register(&jpg_ops);
}
