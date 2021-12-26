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

static void
read_vp8_segmentation_adjust(struct vp8_update_segmentation *s, struct bool_dec *br)
{
    s->segmentation_enabled = BOOL_BIT(br);
    if (s->segmentation_enabled) {
        s->update_mb_segmentation_map = BOOL_BIT(br);
        s->update_segment_feature_data = BOOL_BIT(br);
        if (s->update_segment_feature_data) {
            s->segment_feature_mode = BOOL_BIT(br);
            for (int i = 0; i < 4; i ++) {
                s->quant[i].quantizer_update = BOOL_BIT(br);
                if (s->quant[i].quantizer_update) {
                    s->quant[i].quantizer_update_value = BOOL_BITS(br, 7);
                    if (BOOL_BIT(br)) {
                        s->quant[i].quantizer_update_value *= -1;
                    }
                }
            }
            for (int i = 0; i < 4; i ++) {
                s->lf[i].loop_filter_update = BOOL_BIT(br);
                if (s->lf[i].loop_filter_update) {
                    s->lf[i].lf_update_value = BOOL_BITS(br, 6);
                    if (BOOL_BIT(br)) {
                        s->lf[i].lf_update_value *= -1;
                    }
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
            for (int i = 0; i < 4; i ++) {
                // d->mode_ref_lf_delta_update[i].ref_frame_delta_update_flag = BOOL_BIT(br);
                if (BOOL_BIT(br)) {
                    d->mode_ref_lf_delta_update[i] = BOOL_BITS(br, 6);
                    if (BOOL_BIT(br)) {
                        d->mode_ref_lf_delta_update[i] *= -1;
                    }
                }
            }
            for (int i = 0; i < 4; i ++) {
                // d->mb_mode_delta_update[i].mb_mode_delta_update_flag = BOOL_BIT(br);
                if (BOOL_BIT(br)) {
                    d->mb_mode_delta_update[i] = BOOL_BITS(br, 6);
                    if (BOOL_BIT(br)) {
                        d->mb_mode_delta_update[i] *= -1;
                    }
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
        CoeffsUpdateProba[4][8][3][11] = {
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
    static const uint8_t def_coeffsProba0[4][8][3][11] = 
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
    for (int i = 0; i < 4; i ++) {
        for (int j = 0; j < 8 ; j ++) {
            for (int k = 0; k < 3; k ++) {
                for (int l = 0; l < 11; l ++) {
                    if (BOOL_DECODE(br, CoeffsUpdateProba[i][j][k][l])) {
                        kh->coeff_prob[i][j][k][l] = BOOL_BITS(br, 8);
                    } else {
                        kh->coeff_prob[i][j][k][l] = def_coeffsProba0[i][j][k][l];
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
vp8_get_coeff(struct bool_dec *bt, uint8_t *out, uint8_t ***coeff_prob, int i, int ctx)
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
static const uint8_t kBModesProba[NUM_BMODES][NUM_BMODES][NUM_BMODES - 1] = {
  { { 231, 120, 48, 89, 115, 113, 120, 152, 112 },
    { 152, 179, 64, 126, 170, 118, 46, 70, 95 },
    { 175, 69, 143, 80, 85, 82, 72, 155, 103 },
    { 56, 58, 10, 171, 218, 189, 17, 13, 152 },
    { 114, 26, 17, 163, 44, 195, 21, 10, 173 },
    { 121, 24, 80, 195, 26, 62, 44, 64, 85 },
    { 144, 71, 10, 38, 171, 213, 144, 34, 26 },
    { 170, 46, 55, 19, 136, 160, 33, 206, 71 },
    { 63, 20, 8, 114, 114, 208, 12, 9, 226 },
    { 81, 40, 11, 96, 182, 84, 29, 16, 36 } },
  { { 134, 183, 89, 137, 98, 101, 106, 165, 148 },
    { 72, 187, 100, 130, 157, 111, 32, 75, 80 },
    { 66, 102, 167, 99, 74, 62, 40, 234, 128 },
    { 41, 53, 9, 178, 241, 141, 26, 8, 107 },
    { 74, 43, 26, 146, 73, 166, 49, 23, 157 },
    { 65, 38, 105, 160, 51, 52, 31, 115, 128 },
    { 104, 79, 12, 27, 217, 255, 87, 17, 7 },
    { 87, 68, 71, 44, 114, 51, 15, 186, 23 },
    { 47, 41, 14, 110, 182, 183, 21, 17, 194 },
    { 66, 45, 25, 102, 197, 189, 23, 18, 22 } },
  { { 88, 88, 147, 150, 42, 46, 45, 196, 205 },
    { 43, 97, 183, 117, 85, 38, 35, 179, 61 },
    { 39, 53, 200, 87, 26, 21, 43, 232, 171 },
    { 56, 34, 51, 104, 114, 102, 29, 93, 77 },
    { 39, 28, 85, 171, 58, 165, 90, 98, 64 },
    { 34, 22, 116, 206, 23, 34, 43, 166, 73 },
    { 107, 54, 32, 26, 51, 1, 81, 43, 31 },
    { 68, 25, 106, 22, 64, 171, 36, 225, 114 },
    { 34, 19, 21, 102, 132, 188, 16, 76, 124 },
    { 62, 18, 78, 95, 85, 57, 50, 48, 51 } },
  { { 193, 101, 35, 159, 215, 111, 89, 46, 111 },
    { 60, 148, 31, 172, 219, 228, 21, 18, 111 },
    { 112, 113, 77, 85, 179, 255, 38, 120, 114 },
    { 40, 42, 1, 196, 245, 209, 10, 25, 109 },
    { 88, 43, 29, 140, 166, 213, 37, 43, 154 },
    { 61, 63, 30, 155, 67, 45, 68, 1, 209 },
    { 100, 80, 8, 43, 154, 1, 51, 26, 71 },
    { 142, 78, 78, 16, 255, 128, 34, 197, 171 },
    { 41, 40, 5, 102, 211, 183, 4, 1, 221 },
    { 51, 50, 17, 168, 209, 192, 23, 25, 82 } },
  { { 138, 31, 36, 171, 27, 166, 38, 44, 229 },
    { 67, 87, 58, 169, 82, 115, 26, 59, 179 },
    { 63, 59, 90, 180, 59, 166, 93, 73, 154 },
    { 40, 40, 21, 116, 143, 209, 34, 39, 175 },
    { 47, 15, 16, 183, 34, 223, 49, 45, 183 },
    { 46, 17, 33, 183, 6, 98, 15, 32, 183 },
    { 57, 46, 22, 24, 128, 1, 54, 17, 37 },
    { 65, 32, 73, 115, 28, 128, 23, 128, 205 },
    { 40, 3, 9, 115, 51, 192, 18, 6, 223 },
    { 87, 37, 9, 115, 59, 77, 64, 21, 47 } },
  { { 104, 55, 44, 218, 9, 54, 53, 130, 226 },
    { 64, 90, 70, 205, 40, 41, 23, 26, 57 },
    { 54, 57, 112, 184, 5, 41, 38, 166, 213 },
    { 30, 34, 26, 133, 152, 116, 10, 32, 134 },
    { 39, 19, 53, 221, 26, 114, 32, 73, 255 },
    { 31, 9, 65, 234, 2, 15, 1, 118, 73 },
    { 75, 32, 12, 51, 192, 255, 160, 43, 51 },
    { 88, 31, 35, 67, 102, 85, 55, 186, 85 },
    { 56, 21, 23, 111, 59, 205, 45, 37, 192 },
    { 55, 38, 70, 124, 73, 102, 1, 34, 98 } },
  { { 125, 98, 42, 88, 104, 85, 117, 175, 82 },
    { 95, 84, 53, 89, 128, 100, 113, 101, 45 },
    { 75, 79, 123, 47, 51, 128, 81, 171, 1 },
    { 57, 17, 5, 71, 102, 57, 53, 41, 49 },
    { 38, 33, 13, 121, 57, 73, 26, 1, 85 },
    { 41, 10, 67, 138, 77, 110, 90, 47, 114 },
    { 115, 21, 2, 10, 102, 255, 166, 23, 6 },
    { 101, 29, 16, 10, 85, 128, 101, 196, 26 },
    { 57, 18, 10, 102, 102, 213, 34, 20, 43 },
    { 117, 20, 15, 36, 163, 128, 68, 1, 26 } },
  { { 102, 61, 71, 37, 34, 53, 31, 243, 192 },
    { 69, 60, 71, 38, 73, 119, 28, 222, 37 },
    { 68, 45, 128, 34, 1, 47, 11, 245, 171 },
    { 62, 17, 19, 70, 146, 85, 55, 62, 70 },
    { 37, 43, 37, 154, 100, 163, 85, 160, 1 },
    { 63, 9, 92, 136, 28, 64, 32, 201, 85 },
    { 75, 15, 9, 9, 64, 255, 184, 119, 16 },
    { 86, 6, 28, 5, 64, 255, 25, 248, 1 },
    { 56, 8, 17, 132, 137, 255, 55, 116, 128 },
    { 58, 15, 20, 82, 135, 57, 26, 121, 40 } },
  { { 164, 50, 31, 137, 154, 133, 25, 35, 218 },
    { 51, 103, 44, 131, 131, 123, 31, 6, 158 },
    { 86, 40, 64, 135, 148, 224, 45, 183, 128 },
    { 22, 26, 17, 131, 240, 154, 14, 1, 209 },
    { 45, 16, 21, 91, 64, 222, 7, 1, 197 },
    { 56, 21, 39, 155, 60, 138, 23, 102, 213 },
    { 83, 12, 13, 54, 192, 255, 68, 47, 28 },
    { 85, 26, 85, 85, 128, 128, 32, 146, 171 },
    { 18, 11, 7, 63, 144, 171, 4, 4, 246 },
    { 35, 27, 10, 146, 174, 171, 12, 26, 128 } },
  { { 190, 80, 35, 99, 180, 80, 126, 54, 45 },
    { 85, 126, 47, 87, 176, 51, 41, 20, 32 },
    { 101, 75, 128, 139, 118, 146, 116, 128, 85 },
    { 56, 41, 15, 176, 236, 85, 37, 9, 62 },
    { 71, 30, 17, 119, 118, 255, 17, 18, 138 },
    { 101, 38, 60, 138, 55, 70, 43, 26, 142 },
    { 146, 36, 19, 30, 171, 255, 97, 27, 20 },
    { 138, 45, 61, 62, 219, 1, 81, 188, 64 },
    { 32, 41, 20, 117, 151, 142, 20, 21, 163 },
    { 112, 19, 12, 61, 195, 128, 48, 4, 24 } }
};

static inline uint32_t 
NzCodeBits(uint32_t nz_coeffs, int nz, int dc_nz)
{
    nz_coeffs <<= 2;
    nz_coeffs |= (nz > 3) ? 3 : (nz > 1) ? 2 : dc_nz;
    return nz_coeffs;
}

static int 
vp8_residuals(WEBP *w, struct macro_block *mb, bool_dec *bt, struct partition *p)
{
    int16_t* dst = mb->coeffs;
    memset(dst, 0, 384 * sizeof(*dst));

    if (mb->is_i4x4) {
        //dc
        int16_t dc[16] = {0};


    }
    return 0;
}

void
vp8_intramode(WEBP *w, bool_dec *bt)
{
    uint8_t left[4], top[4];
    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
    struct macro_block *mb = malloc(sizeof(*mb));

    for (int y = 0; y < (height + 15) >> 4; y ++) {
        for (int x = 0; x < (width + 15) >> 4; x ++) {
            if (w->k.segmentation.update_mb_segmentation_map) {
                // VDBG(webp, "0x%llx\tbuf[0] 0x%x, count %d: ", bt->value, bt->bits->ptr[0], bt->count);
                mb->segment = !BOOL_DECODE(bt, w->k.segmentation.segment_prob[0]) ?
                    BOOL_DECODE(bt, w->k.segmentation.segment_prob[1]) :
                    BOOL_DECODE(bt, w->k.segmentation.segment_prob[2]) + 2;
                VDBG(webp, "%x", mb->segment);
            } else {
                mb->segment = 0;
            }
            if (w->k.mb_no_skip_coeff) {
                mb->skip = BOOL_DECODE(bt, w->k.prob_skip_false);
            }
            uint8_t is_4x4 = !BOOL_DECODE(bt, 145);
            if (!is_4x4) {
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
                        const uint8_t* const prob = kBModesProba[top[x]][ymode];
                        int ymode = !BOOL_DECODE(bt, prob[0]) ? B_DC_PRED :
                            !BOOL_DECODE(bt, prob[1]) ? B_TM_PRED :
                            !BOOL_DECODE(bt, prob[2]) ? B_VE_PRED :
                            !BOOL_DECODE(bt, prob[3]) ?
                            (!BOOL_DECODE(bt, prob[4]) ? B_HE_PRED :
                            (!BOOL_DECODE(bt, prob[5]) ? B_RD_PRED : B_VR_PRED)) :
                            (!BOOL_DECODE(bt, prob[6]) ? B_LD_PRED :
                            (!BOOL_DECODE(bt, prob[7]) ? B_VL_PRED :
                            (!BOOL_DECODE(bt, prob[8]) ? B_HD_PRED : B_HU_PRED))
                        );
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
}

static void
vp8_decode(WEBP *w, bool_dec *br, bool_dec **btree)
{

    struct macro_block *mb = malloc(sizeof(*mb));
    uint32_t *Y, *U, *V;

    vp8_intramode(w, br);

    int width = ((w->fi.width + 3) >> 2) << 2;
    int height = w->fi.height;
    int pitch = ((width * 32 + 32 - 1) >> 5) << 2;
    for (int y = 0; y < (height + 15) >> 4; y ++) {
        // parse intra mode
        struct partition *p = &w->p[y & (w->k.nbr_partitions-1)];
        bool_dec *bt = btree[y & (w->k.nbr_partitions-1)];
        for (int x = 0; x < (width + 15) >> 4; x ++) {
            // from libwebp we don't save the segment map
            
        }
        //end of intra mode


        for (int x = 0; x < width >> 4; x ++) {
            int skip = w->k.mb_no_skip_coeff? mb->skip : 0;
            if (skip) {
                skip = vp8_residuals(w, mb, bt, p);
            } else {
                mb->non_zero_y = 0;
                mb->non_zero_uv = 0;
                mb->dither = 0;
            }
        }
    }

    free(mb);
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