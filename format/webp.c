#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include "webp.h"
#include "file.h"
#include "bitstream.h"



static int
WEBP_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct webp_header h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (!memcmp(&h.riff, "RIFF", 4) && !memcmp(&h.webp, "WEBP", 4)) {
        return 0;
    }

    return -EINVAL;
}

static void
read_vp8_ctl_part(WEBP *w, struct bits_vec *v)
{
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;

    w->k.cs_and_clamp.color_space = READ_BIT(v);
    w->k.cs_and_clamp.clamp = READ_BIT(v);
    w->k.segmentation.segmentation_enabled = READ_BIT(v);
    if (w->k.segmentation.segmentation_enabled) {
        /*READ segmentation */
        w->k.segmentation.update_mb_segmentation_map = READ_BIT(v);
        w->k.segmentation.update_segment_feature_data = READ_BIT(v);
        if (w->k.segmentation.update_segment_feature_data) {
            w->k.segmentation.segment_feature_mode = READ_BIT(v);
            for (int i = 0; i < 4; i ++) {
                w->k.segmentation.quant[i].quantizer_update = READ_BIT(v);
                if (w->k.segmentation.quant[i].quantizer_update) {
                    w->k.segmentation.quant[i].quantizer_update_value = READ_BITS(v, 7);
                    w->k.segmentation.quant[i].quantizer_update_sign = READ_BIT(v);
                }
            }
            for (int i = 0; i < 4; i ++) {
                w->k.segmentation.lf[i].loop_filter_update = READ_BIT(v);
                if (w->k.segmentation.lf[i].loop_filter_update) {
                    w->k.segmentation.lf[i].lf_update_value = READ_BITS(v, 6);
                    w->k.segmentation.lf[i].lf_update_sign = READ_BIT(v);
                }
            }
        }
        if (w->k.segmentation.update_mb_segmentation_map) {
            for (int i = 0; i < 3; i ++) {
                w->k.segmentation.segment_prob[i].segment_prob_update = READ_BIT(v);
                if (w->k.segmentation.segment_prob[i].segment_prob_update) {
                    w->k.segmentation.segment_prob[i].segment_prob = READ_BITS(v, 8);
                }
            }
        }
    }

    w->k.filter_type = READ_BIT(v);
    w->k.loop_filter_level = READ_BITS(v, 6);
    w->k.sharpness_level = READ_BITS(v, 3);

    /* READ adjustments */
    w->k.mb_lf_adjustments.loop_filter_adj_enable = READ_BIT(v);
    if (w->k.mb_lf_adjustments.loop_filter_adj_enable) {
        w->k.mb_lf_adjustments.mode_ref_lf_delta_update_flag = READ_BIT(v);
        if (w->k.mb_lf_adjustments.mode_ref_lf_delta_update_flag) {
            for (int i = 0; i < 4; i ++) {
                w->k.mb_lf_adjustments.mode_ref_lf_delta_update[i].ref_frame_delta_update_flag = READ_BIT(v);
                if (w->k.mb_lf_adjustments.mode_ref_lf_delta_update[i].ref_frame_delta_update_flag) {
                    w->k.mb_lf_adjustments.mode_ref_lf_delta_update[i].delta_magnitude = READ_BITS(v, 6);
                    w->k.mb_lf_adjustments.mode_ref_lf_delta_update[i].delta_sign = READ_BIT(v);
                }
            }
            for (int i = 0; i < 4; i ++) {
                w->k.mb_lf_adjustments.mb_mode_delta_update[i].mb_mode_delta_update_flag = READ_BIT(v);
                if (w->k.mb_lf_adjustments.mb_mode_delta_update[i].mb_mode_delta_update_flag) {
                    w->k.mb_lf_adjustments.mb_mode_delta_update[i].delta_magnitude = READ_BITS(v, 6);
                    w->k.mb_lf_adjustments.mb_mode_delta_update[i].delta_sign = READ_BIT(v);
                }
            }
        }
    }

    w->k.log2_nbr_of_dct_partitions = READ_BITS(v, 2);
    /* READ quant indices */
    w->k.quant_indice.y_ac_qi = READ_BITS(v, 7);
    w->k.quant_indice.y_dc_delta_present = READ_BIT(v);
    if (w->k.quant_indice.y_dc_delta_present) {
        w->k.quant_indice.y_dc_delta_magnitude = READ_BITS(v, 4);
        w->k.quant_indice.y_dc_delta_sign = READ_BIT(v);
    }
    w->k.quant_indice.y2_dc_delta_present = READ_BIT(v);
    if (w->k.quant_indice.y2_dc_delta_present) {
        w->k.quant_indice.y2_dc_delta_magnitude = READ_BITS(v, 4);
        w->k.quant_indice.y2_dc_delta_sign = READ_BIT(v);
    }
    w->k.quant_indice.y2_ac_delta_present = READ_BIT(v);
    if (w->k.quant_indice.y2_ac_delta_present) {
        w->k.quant_indice.y2_ac_delta_magnitude = READ_BITS(v, 4);
        w->k.quant_indice.y2_ac_delta_sign = READ_BIT(v);
    }
    w->k.quant_indice.uv_dc_delta_present = READ_BIT(v);
    if (w->k.quant_indice.uv_dc_delta_present) {
        w->k.quant_indice.uv_dc_delta_magnitude = READ_BITS(v, 4);
        w->k.quant_indice.uv_dc_delta_sign = READ_BIT(v);
    }
    w->k.quant_indice.uv_ac_delta_present = READ_BIT(v);
    if (w->k.quant_indice.uv_ac_delta_present) {
        w->k.quant_indice.uv_ac_delta_magnitude = READ_BITS(v, 4);
        w->k.quant_indice.uv_ac_delta_sign = READ_BIT(v);
    }
    w->k.refresh_entropy_probs = READ_BIT(v);
    /* READ prob */
    for (int i = 0; i < 4; i ++) {
        for (int j = 0; j < 8 ; j ++) {
            for (int k = 0; k < 3; k ++) {
                for (int l = 0; l < 11; l ++) {
                    uint8_t coeff_prob_update_flag = READ_BIT(v);
                    if (coeff_prob_update_flag) {
                        w->k.coeff_prob[i][j][k][l] = READ_BITS(v, 8);
                    }
                }
            }
        }
    }
    w->k.mb_no_skip_coeff = READ_BIT(v);
    if (w->k.mb_no_skip_coeff) {
        w->k.prob_skip_false = READ_BITS(v, 8);
    }
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
    if (chead == CHUNCK_HEADER("VP8X")) {
        fseek(f, -4, SEEK_CUR);
        fread(&w->vp8x, sizeof(struct webp_vp8x), 1, f);
    } else if (chead == CHUNCK_HEADER("VP8 ")) {
        //VP8 data chuck
        uint32_t size;
        fread(&size, 4, 1, f);
        fread(&w->fh, 3, 1, f);
        if (w->fh.frame_type == 0) {
            //key frame, more info
            fread(&w->fi, 7, 1, f);
        }
        uint8_t *buf = malloc(size - 10);
        fread(buf, size - 10, 1, f);
        struct bits_vec * v = init_bits_vec(buf, size - 10); 
        read_vp8_ctl_part(w, v);
    } else if (chead == CHUNCK_HEADER("VP8L")) {
        //VP8 lossless chuck
    }

    fclose(f);
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
    WEBP * t = (WEBP *)p->pic;
    fprintf(f, "WEBP file format:\n");
    fprintf(f, "\tfile size: %d\n", t->header.file_size);
    fprintf(f, "----------------------------------\n");
    if (t->vp8x.vp8x == CHUNCK_HEADER("VP8X")) {
        fprintf(f, "\tVP8X icc %d, alpha %d, exif %d, xmp %d, animation %d\n",
            t->vp8x.icc, t->vp8x.alpha, t->vp8x.exif_metadata, t->vp8x.xmp_metadata, t->vp8x.animation);
        fprintf(f, "\tVP8X canvas witdth %d, height %d\n", READ_UINT24(t->vp8x.canvas_width),
            READ_UINT24(t->vp8x.canvas_height));
    }
    int size = t->fh.size_h << 16 | t->fh.size;
    fprintf(f, "\t%s: version %d, size %d\n", t->fh.frame_type == 0? "I frame":"P frame", t->fh.version, size);
    if (t->fh.frame_type == 0) {
        fprintf(f, "\tscale horizon %d, vertical %d\n", t->fi.horizontal, t->fi.vertical);
        fprintf(f, "\theight %d, width %d\n", t->fi.height, t->fi.width);
    }
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