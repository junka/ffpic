#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "bitstream.h"
#include "colorspace.h"
#include "vlog.h"
#include "file.h"
#include "avif.h"
#include "av1.h"
#include "utils.h"

extern int vlog_av1;
VLOG_REGISTER(avif, DEBUG)

static void AVIF_free(struct pic *p);
static int avif_item_protected(const AVIF *h, uint32_t item_id);
static void avif_apply_alpha16(const struct av1_image *alpha, uint8_t *pixels,
                               int pitch, int w, int h, int premultiplied);
static void avif_apply_alpha(const struct av1_image *alpha, uint8_t *pixels,
                             int pitch, int w, int h, int premultiplied);

static int
AVIF_probe(FILE *f)
{
    if (f == NULL) {
        return -ENOENT;
    }
    /* ftyp: size, 'ftyp', major_brand, minor_version, compatible_brands[] */
    uint32_t hdr[4];
    if (fread(hdr, sizeof(hdr[0]), 4, f) != 4) {
        return -EINVAL;
    }
    if (hdr[1] != TYPE2UINT("ftyp")) {
        return -EINVAL;
    }
    if (hdr[2] == TYPE2UINT("avif") || hdr[2] == TYPE2UINT("avis")) {
        return 0;
    }
    /* Some muxers use a generic HEIF major brand and only list avif among
     * the compatible brands. */
    if (hdr[2] != TYPE2UINT("mif1") && hdr[2] != TYPE2UINT("msf1")) {
        return -EINVAL;
    }
    uint32_t box_size = SWAP(hdr[0]);
    if (box_size < 20) {
        return -EINVAL;
    }
    uint32_t n = (box_size - 16) / 4;
    if (n > 64) {
        n = 64;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t brand;
        if (fread(&brand, sizeof(brand), 1, f) != 1) {
            return -EINVAL;
        }
        if (brand == TYPE2UINT("avif")) {
            return 0;
        }
    }
    return -EINVAL;
}

static uint64_t
read_leb128(struct bits_vec *v)
{
    uint64_t value = 0;
    uint64_t lbe = 0;
    for (int i = 0; i < 8; i++) {
        lbe = READ_BITS(v, 8);
        value |= ((lbe & 0x7F) << (i * 7));
        lbe += 1;
        if (!(lbe & 0x80)) {
            break;
        }
    }
    return value;
}

static int
parse_color_config(struct color_config *cc, struct sequence_header_obu *obu, struct bits_vec *v)
{
    cc->high_bitdepth = READ_BIT(v);
    if (obu->seq_profile == 2 && cc->high_bitdepth) {
        cc->twelve_bit = READ_BIT(v);
        obu->BitDepth = cc->twelve_bit ? 12: 10;
    } else if (obu->seq_profile <= 2) {
        obu->BitDepth = cc->high_bitdepth ? 10 : 8;
    }
    if (obu->seq_profile == 1) {
        cc->mono_chrome = 0;
    } else {
        cc->mono_chrome = READ_BIT(v);
    }
    obu->NumPlanes = cc->mono_chrome ? 1: 3;
    cc->color_description_present_flag = READ_BIT(v);
    if (cc->color_description_present_flag) {
        cc->color_primaries = READ_BITS(v, 8);
        cc->transfer_characteristics = READ_BITS(v, 8);
        cc->matrix_coefficients = READ_BITS(v, 8);
    } else {
        cc->color_primaries = CP_UNSPECIFIED;
        cc->transfer_characteristics = TC_UNSPECIFIED;
        cc->matrix_coefficients = MC_UNSPECIFIED;
    }
    if (cc->mono_chrome) {
        cc->color_range = READ_BIT(v);
        cc->subsampling_x = 1;
        cc->subsampling_y = 1;
        cc->chroma_sample_position = CSP_UNKNOWN;
        cc->separate_uv_delta_q = 0;
        return 0;
    } else if (cc->color_primaries == CP_BT_709 &&
        cc->transfer_characteristics == TC_SRGB &&
        cc->matrix_coefficients == MC_IDENTITY) {
        cc->color_range = 1;
        cc->subsampling_x = 0;
        cc->subsampling_y = 0;
    } else {
        cc->color_range = READ_BIT(v);
        if (obu->seq_profile == 0) {
            cc->subsampling_x = 1;
            cc->subsampling_y = 1;
        } else if (obu->seq_profile == 1) {
            cc->subsampling_x = 0;
            cc->subsampling_y = 0;
        } else {
            if (obu->BitDepth == 12) {
                cc->subsampling_x = READ_BIT(v);
                if (cc->subsampling_x) {
                    cc->subsampling_y = READ_BIT(v);
                } else {
                    cc->subsampling_y = 0;
                }
            } else {
                cc->subsampling_x = 1;
                cc->subsampling_y = 0;
            }
        }
        if (cc->subsampling_x && cc->subsampling_y) {
            cc->chroma_sample_position = READ_BITS(v, 2);
        }
    }
    cc->separate_uv_delta_q = READ_BIT(v);
    return 0;
}

int choose_operating_point(void)
{
    return 0;
}

static struct sequence_header_obu *
parse_sequence_header_obu(struct bits_vec *v)
{
    struct sequence_header_obu *obu = malloc(sizeof(*obu));
    obu->seq_profile = READ_BITS(v, 3);
    obu->still_picture = READ_BIT(v);
    obu->reduced_still_picture_header = READ_BIT(v);
    if (obu->reduced_still_picture_header) {
        obu->timing_info_present_flag = 0;
        obu->decoder_model_info_present_flag = 0;
        obu->initial_display_delay_present_flag = 0;
        obu->operating_points_cnt_minus_1 = 0;
        obu->points[0].operating_point_idc = 0;
        obu->points[0].seq_level_idx = READ_BITS(v, 5);
        obu->points[0].seq_tier = 0;
        obu->points[0].decoder_model_present_for_this_op = 0;
        obu->points[0].initial_display_delay_present_for_this_op = 0;
    } else {
        obu->timing_info_present_flag = READ_BIT(v);
        if (obu->timing_info_present_flag) {
            obu->decoder_model_info_present_flag = READ_BIT(v);
            if (obu->decoder_model_info_present_flag) {
                obu->minfo.buffer_delay_length_minus_1 = READ_BITS(v, 5);
                obu->minfo.num_units_in_decoding_tick = READ_BITS(v, 32);
                obu->minfo.buffer_removal_time_length_minus_1 = READ_BITS(v, 5);
                obu->minfo.frame_presentation_time_length_minus_1 = READ_BITS(v, 5);
            }
        } else {
            obu->decoder_model_info_present_flag = 0;
        }
        obu->initial_display_delay_present_flag = READ_BIT(v);
        obu->operating_points_cnt_minus_1 = READ_BITS(v, 5);
        obu->points = malloc(sizeof(struct operating_points) * (obu->operating_points_cnt_minus_1+1));
        for (int i = 0; i <= obu->operating_points_cnt_minus_1; i++) {
            obu->points[i].operating_point_idc = READ_BITS(v, 12);
            obu->points[i].seq_level_idx = READ_BITS(v, 5);
            if (obu->points[i].seq_level_idx > 7) {
                obu->points[i].seq_tier = READ_BIT(v);
            } else {
                obu->points[i].seq_tier = 0;
            }
            if (obu->decoder_model_info_present_flag) {
                obu->points[i].decoder_model_present_for_this_op = READ_BIT(v);
                if (obu->points[i].decoder_model_present_for_this_op) {
                    int n = obu->minfo.buffer_delay_length_minus_1 + 1;
                    obu->points[i].pinfo.decoder_buffer_delay = READ_BITS(v, n);
                    obu->points[i].pinfo.encoder_buffer_delay = READ_BITS(v, n);
                    obu->points[i].pinfo.low_delay_mode_flag = READ_BIT(v);
                }
            } else {
                obu->points[i].decoder_model_present_for_this_op = 0;
            }
            if (obu->initial_display_delay_present_flag) {
                obu->points[i].initial_display_delay_present_for_this_op = READ_BIT(v);
                if (obu->points[i].initial_display_delay_present_for_this_op) {
                    obu->points[i].initial_display_delay_minus_1 = READ_BITS(v, 4);
                }
            }
        }
    }
    // int op = choose_operating_point();
    // int OperatingPointIdc = obu->points[op].operating_point_idc;
    obu->frame_width_bits_minus_1 = READ_BITS(v, 4);
    obu->frame_height_bits_minus_1 = READ_BITS(v, 4);
    obu->max_frame_width_minus_1 = READ_BITS(v, obu->frame_width_bits_minus_1+1);
    obu->max_frame_height_minus_1 = READ_BITS(v, obu->frame_height_bits_minus_1+1);
    if (obu->reduced_still_picture_header) {
        obu->frame_id_numbers_present_flag = 0;
    } else {
        obu->frame_id_numbers_present_flag = READ_BIT(v);
    }
    if (obu->frame_id_numbers_present_flag) {
        obu->delta_frame_id_length_minus_2 = READ_BITS(v, 4);
        obu->additional_frame_id_length_minus_1 = READ_BITS(v, 3);
    }
    obu->use_128x128_superblock = READ_BIT(v);
    obu->enable_filter_intra = READ_BIT(v);
    obu->enable_intra_edge_filter = READ_BIT(v);
    if (obu->reduced_still_picture_header) {
        obu->enable_interintra_compound = 0;
        obu->enable_masked_compound = 0;
        obu->enable_warped_motion = 0;
        obu->enable_dual_filter = 0;
        obu->enable_order_hint = 0;
        obu->enable_jnt_comp = 0;
        obu->enable_ref_frame_mvs = 0;
        obu->seq_force_screen_content_tools = SELECT_SCREEN_CONTENT_TOOLS;
        obu->seq_force_integer_mv = SELECT_INTEGER_MV;
        obu->OrderHintBits = 0;
    } else {
        obu->enable_interintra_compound = READ_BIT(v);
        obu->enable_masked_compound = READ_BIT(v);
        obu->enable_warped_motion = READ_BIT(v);
        obu->enable_dual_filter = READ_BIT(v);
        obu->enable_order_hint = READ_BIT(v);
        if (obu->enable_order_hint) {
            obu->enable_jnt_comp = READ_BIT(v);
            obu->enable_ref_frame_mvs = READ_BIT(v);
        } else {
            obu->enable_jnt_comp = 0;
            obu->enable_ref_frame_mvs = 0;
        }
        obu->seq_choose_screen_content_tools = READ_BIT(v);
        if (obu->seq_choose_screen_content_tools) {
            obu->seq_force_screen_content_tools = SELECT_SCREEN_CONTENT_TOOLS;
        } else {
            obu->seq_force_screen_content_tools = READ_BIT(v);
        }
        if (obu->seq_force_screen_content_tools > 0) {
            obu->seq_choose_integer_mv = READ_BIT(v);
            if (obu->seq_choose_integer_mv) {
                obu->seq_force_integer_mv = SELECT_INTEGER_MV;
            } else {
                obu->seq_force_integer_mv = READ_BIT(v);
            }
        } else {
            obu->seq_force_integer_mv = SELECT_INTEGER_MV;
        }
        if (obu->enable_order_hint) {
            obu->order_hint_bits_minus_1 = READ_BITS(v, 3);
            obu->OrderHintBits = obu->order_hint_bits_minus_1 + 1;
        } else {
            obu->OrderHintBits = 0;
        }
    }

    obu->enable_superres = READ_BIT(v);
    obu->enable_cdef = READ_BIT(v);
    obu->enable_restoration = READ_BIT(v);
    parse_color_config(&obu->cc, obu, v);
    obu->film_grain_params_present = READ_BIT(v);

    return obu;
}

int parse_metadata_itut_t35(struct metadata_itut_t35 *mi, struct bits_vec *v)
{
    mi->itu_t_t35_country_code = READ_BITS(v, 8);
    if (mi->itu_t_t35_country_code == 0xFF) {
        mi->itu_t_t35_country_code_extension_byte = READ_BITS(v, 8);
    }
    // skip now for mi->itu_t_t35_payload_bytes
    while(READ_BITS(v, 8)) {

    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* parse_obu / parse_metadata_obu — 静态诊断与参考实现                */
/*                                                                    */
/* 这些函数不参与生产解码路径: avif_decode_item 把 AV1 原始 bitstream  */
/* 直接喂给 coding/av1.c 里的 av1_decode_image，而不是经过这里的        */
/* parse_obu 分派。它们仅用于调试/分析 av1C configOBUs 或 item payloads */
/* 时手动扫描 OBU 序列，或作为将来接入 container 层 OBU 解析的参考。    */
/*                                                                    */
/* 新增 type 分支时，优先改 coding/av1.c::av1_decode_image 里的        */
/* 生产分派循环 (type 1/3/4/5/6)，这里只要保持 metadata_type enum     */
/* 与 format/avif.h 对齐即可。                                         */
/* ------------------------------------------------------------------ */

int parse_metadata_hdr_cll(struct metadata_hdr_cll *mh, struct bits_vec *v)
{
    mh->max_cll = READ_BITS(v, 16);
    mh->max_fall = READ_BITS(v, 16);
    return 0;
}

int parse_metadata_hdr_mdcv(struct metadata_hdr_mdcv *mh, struct bits_vec *v)
{
    for (int i = 0; i < 3; i++ ) {
        mh->primary_chromaticity_x[i] = READ_BITS(v, 16);
        mh->primary_chromaticity_y[i] = READ_BITS(v, 16);
    }
    mh->white_point_chromaticity_x = READ_BITS(v, 16);
    mh->white_point_chromaticity_y = READ_BITS(v, 16);
    mh->luminance_max = READ_BITS(v, 32);
    mh->luminance_min = READ_BITS(v, 32);
    return 0;
}

int parse_metadata_scalability(struct metadata_scalability *ms, struct bits_vec *v)
{
    ms->scalability_mode_idc = READ_BITS(v, 8);
    if (ms->scalability_mode_idc == SCALABILITY_SS) {
        ms->spatial_layers_cnt_minus_1 =  READ_BITS(v, 2);
        ms->spatial_layer_dimensions_present_flag =  READ_BIT(v);
        ms->spatial_layer_description_present_flag =  READ_BIT(v);
        ms->temporal_group_description_present_flag = READ_BIT(v);
        ms->scalability_structure_reserved_3bits = READ_BITS(v, 3);
        if (ms->spatial_layer_dimensions_present_flag) {
            for (int i = 0; i <= ms->spatial_layers_cnt_minus_1 ; i++ ) {
                ms->spatial_layer_max_width[i] = READ_BITS(v, 16);
                ms->spatial_layer_max_height[i] = READ_BITS(v, 16);
            }
        }
        if (ms->spatial_layer_description_present_flag) {
            for (int i = 0; i <= ms->spatial_layers_cnt_minus_1; i++) {
                ms->spatial_layer_ref_id[i] = READ_BITS(v, 8);
            }
        }
        if (ms->temporal_group_description_present_flag) {
            ms->temporal_group_size = READ_BITS(v, 8);
            ms->groups = malloc(sizeof(struct temporal_group) * ms->temporal_group_size);
            for (int i = 0; i < ms->temporal_group_size; i++ ) {
                ms->groups[i].temporal_group_temporal_id = READ_BITS(v, 3);
                ms->groups[i].temporal_group_temporal_switching_up_point_flag = READ_BIT(v);
                ms->groups[i].temporal_group_spatial_switching_up_point_flag = READ_BIT(v);
                ms->groups[i].temporal_group_ref_cnt = READ_BITS(v, 3);
                for (int j = 0; j < ms->groups[i].temporal_group_ref_cnt; j++) {
                    ms->groups[i].temporal_group_ref_pic_diff[j] = READ_BITS(v, 8);
                }
            }
        }
    }
    return 0;
}

int parse_metadata_timecode(struct metadata_timecode *mt, struct bits_vec *v)
{
    mt->counting_type = READ_BITS(v, 5);
    mt->full_timestamp_flag = READ_BIT(v);
    mt->discontinuity_flag = READ_BIT(v);
    mt->cnt_dropped_flag = READ_BIT(v);
    mt->n_frames = READ_BITS(v, 9);
    if (mt->full_timestamp_flag) {
        mt->seconds_value = READ_BITS(v, 6);
        mt->minutes_value = READ_BITS(v, 6);
        mt->hours_value = READ_BITS(v, 5);
    } else {
        mt->seconds_flag =  READ_BIT(v);
        if (mt->seconds_flag) {
            mt->seconds_value = READ_BITS(v, 6);
            mt->minutes_flag = READ_BIT(v);
            if (mt->minutes_flag) {
                mt->minutes_value = READ_BITS(v, 6);
                mt->hours_flag = READ_BIT(v);
                if (mt->hours_flag) {
                    mt->hours_value = READ_BITS(v, 5);
                }
            }
        }
    }
    int time_offset_length = READ_BITS(v, 5);
    if (time_offset_length > 0) {
        mt->time_offset_value = READ_BITS(v, time_offset_length);
    }
    return 0;
}

struct metadata_obu*
parse_metadata_obu(struct bits_vec *v)
{
    struct metadata_obu *mo = malloc(sizeof(struct metadata_obu));
    mo->metadata_type = read_leb128(v);
    if (mo->metadata_type == METADATA_TYPE_ITUT_T35) {
        parse_metadata_itut_t35(&mo->itut_t35, v);
    } else if (mo->metadata_type == METADATA_TYPE_HDR_CLL) {
        parse_metadata_hdr_cll(&mo->hdr_cll, v);
    } else if (mo->metadata_type == METADATA_TYPE_HDR_MDCV) {
        parse_metadata_hdr_mdcv(&mo->hdr_mdcv, v);
    } else if (mo->metadata_type == METADATA_TYPE_SCALABILITY) {
        parse_metadata_scalability(&mo->scalability, v);
    } else if (mo->metadata_type == METADATA_TYPE_TIMECODE) {
        parse_metadata_timecode(&mo->timecode, v);
    } else {

    }
    return mo;
}

struct frame_header_obu * parse_frame_header_obu(struct bits_vec *v)
{
    struct frame_header_obu * obu = malloc(sizeof(*obu));
    (void)v;
    return obu;
}

int parse_tile_group_obu(struct bits_vec *v, int size UNUSED)
{
    (void)v;
    return 0;
}

int parse_frame_obu(struct bits_vec *v)
{
    (void)v;
    return 0;
}

int parse_tile_list_obu(struct bits_vec *v)
{
    (void)v;
    return 0;
}

static int
parse_obu_header(struct obu_header *h, struct bits_vec *v)
{
    h->obu_forbidden_bit = READ_BIT(v);
    h->obu_type = READ_BITS(v, 4);
    h->obu_extension_flag = READ_BIT(v);
    h->obu_has_size_field = READ_BIT(v);
    h->obu_reserved_1bit = READ_BIT(v);
    if (h->obu_extension_flag) {
        h->ext.temporal_id = READ_BITS(v, 3);
        h->ext.spatial_id = READ_BITS(v, 2);
        h->ext.extension_header_reserved_3bits = READ_BITS(v, 3);
    }
    return 0;
}

int parse_obu(struct bits_vec *v, int sz, struct obu_header **obu, int idx UNUSED)
{
    struct obu_header h;
    parse_obu_header(&h, v);
    static struct sequence_header_obu *seqobu = NULL;
    // static int SeenFrameHeader = -1;
    int obu_size = 0;
    /* The above code is declaring a variable `OperatingPointIdc` and assigning it a value based on a
    conditional statement. If `seqobu` is not null, it will access the `operating_point_idc`
    property of `seqobu->points[op]` and assign it to `OperatingPointIdc`. Otherwise, it will assign
    0 to `OperatingPointIdc`. */
    if (h.obu_has_size_field) {
        obu_size = read_leb128(v);
    } else {
        obu_size = sz - 1 - h.obu_extension_flag;
    }
    int op = choose_operating_point();
    int OperatingPointIdc = seqobu? seqobu->points[op].operating_point_idc : 0;
    int startPosition = bits_vec_position(v);
    if (h.obu_type != OBU_SEQUENCE_HEADER &&
        h.obu_type != OBU_TEMPORAL_DELIMITER &&
        OperatingPointIdc != 0 &&
        h.obu_extension_flag == 1) {
        int inTemporalLayer = (OperatingPointIdc >> h.ext.temporal_id ) & 1;
        int inSpatialLayer = (OperatingPointIdc >> ( h.ext.spatial_id + 8 ) ) & 1;
        if (!inTemporalLayer || !inSpatialLayer) {
            *obu = NULL;
            SKIP_BITS(v, obu_size * 8);
            return obu_size;
        }
    }
    if (h.obu_type == OBU_SEQUENCE_HEADER) {
        seqobu = parse_sequence_header_obu(v); // at most has one
        seqobu->h = h;
        *obu = (struct obu_header *)seqobu;
    } else if (h.obu_type == OBU_TEMPORAL_DELIMITER) {
        // SeenFrameHeader = 0;
        *obu = NULL;
    } else if (h.obu_type == OBU_FRAME_HEADER) {
        struct frame_header_obu * fh = parse_frame_header_obu(v);
        fh->h = h;
        *obu = (struct obu_header *)fh;
    } else if (h.obu_type == OBU_REDUNDANT_FRAME_HEADER) {
        struct frame_header_obu * fh = parse_frame_header_obu(v);
        fh->h = h;
        *obu = (struct obu_header *)fh;
    } else if (h.obu_type == OBU_TILE_GROUP) {
        parse_tile_group_obu(v, obu_size);
    } else if (h.obu_type == OBU_METADATA) {
        struct metadata_obu *mo = parse_metadata_obu(v);
        mo->h = h;
        *obu = (struct obu_header *)mo;
    } else if (h.obu_type == OBU_FRAME) {
        parse_frame_obu(v);
        *obu = NULL;
    } else if (h.obu_type == OBU_TILE_LIST) {
        parse_tile_list_obu(v);
        *obu = NULL;
    } else if (h.obu_type == OBU_PADDING) {
        *obu = NULL;
        SKIP_BITS(v, obu_size * 8);
        return obu_size + 1 + h.obu_extension_flag;
    } else {
        *obu = NULL;
        SKIP_BITS(v, obu_size * 8);
        return obu_size + 1 + h.obu_extension_flag;
    }
    int currentPosition = bits_vec_position(v);
    int payloadBits = currentPosition - startPosition;
    if (obu_size > 0 && h.obu_type != OBU_TILE_GROUP &&
        h.obu_type != OBU_TILE_LIST &&
        h.obu_type != OBU_FRAME) {
        SKIP_BITS(v, obu_size * 8 - payloadBits);
    }

    return obu_size + 1 + h.obu_extension_flag;
}


int
read_av1c_box(FILE *f, struct box **bn)
{
    if (*bn == NULL) {
        *bn = calloc(1, sizeof(struct av1C_box));
        if (*bn == NULL) {
            return -ENOMEM;
        }
    }
    struct av1C_box *b = (struct av1C_box *)*bn;
    FFREAD_BOX_ST(b, f, FOURCC2UINT('a', 'v', '1', 'C'));

    VDBG(avif, "av1C size %" PRIu64 "", b->size);
    if (b->size < 12) {
        return b->size;
    }

    /* AV1CodecConfigurationRecord: four fixed bytes followed by configOBUs,
     * which may carry the sequence header instead of the item payload. */
    uint8_t cfg[4];
    FFREAD(cfg, 1, 4, f);
    b->marker = cfg[0] >> 7;
    b->version = cfg[0] & 0x7F;
    b->seq_profile = cfg[1] >> 5;
    b->seq_level_idx_0 = cfg[1] & 0x1F;
    b->seq_tier_0 = cfg[2] >> 7;
    b->high_bitdepth = (cfg[2] >> 6) & 1;
    b->twelve_bit = (cfg[2] >> 5) & 1;
    b->monochrome = (cfg[2] >> 4) & 1;
    b->chroma_subsampling_x = (cfg[2] >> 3) & 1;
    b->chroma_subsampling_y = (cfg[2] >> 2) & 1;
    b->chroma_sample_position = cfg[2] & 3;
    b->reserved = cfg[3] >> 5;
    b->initial_presentation_delay_present = (cfg[3] >> 4) & 1;
    b->initial_presentation_delay_minus_one = cfg[3] & 0xF;

    if (b->size > 12) {
        size_t len = (size_t)(b->size - 12);
        b->config_obus = malloc(len);
        if (b->config_obus) {
            FFREAD(b->config_obus, 1, len, f);
            b->config_obus_len = (uint32_t)len;
        } else {
            fseek(f, (long)len, SEEK_CUR);
        }
    }

    VDBG(avif, "av1C: version %d, marker 0x%x, configOBUs %u", b->version,
         b->marker, b->config_obus_len);

    return b->size;
}

void
free_av1c_box(void *box)
{
    free(((struct av1C_box *)box)->config_obus);
}

/* Read AV1 Sample Entry (ISO/IEC 23008-12 6.6.2.3). */
int read_AV1SampleEntry(FILE *f, struct SampleEntry **b)
{
    if (*b == NULL) *b = calloc(1, sizeof(struct AV1SampleEntry));
    struct AV1SampleEntry *e = (struct AV1SampleEntry *)*b;
    long entry_start = ftell(f);
    read_VisualSampleEntry(f, &e->sample);
    assert(e->sample.entry.type == FOURCC2UINT('a', 'v', '0', '1'));
    read_av1c_box(f, (struct box **)(&e->config));
    fseek(f, entry_start + (long)e->sample.entry.size, SEEK_SET);
    return 0;
}

/* Decode one AV1 sample from an moov track. Joins configOBUs from av1C. */
int av1_decode_track_sample(const uint8_t *sample_data, size_t sample_len,
                            const struct av1C_box *av1c, struct av1_image *img)
{
    if (!av1c || !av1c->config_obus || av1c->config_obus_len == 0)
        return av1_decode_image(sample_data, sample_len, img);
    size_t total = (size_t)av1c->config_obus_len + sample_len;
    uint8_t *joined = malloc(total);
    if (!joined) return -ENOMEM;
    memcpy(joined, av1c->config_obus, av1c->config_obus_len);
    memcpy(joined + av1c->config_obus_len, sample_data, sample_len);
    int rc = av1_decode_image(joined, total, img);
    free(joined);
    return rc;
}

/* Track-based AVIF animation helpers. */
static int avif_is_avis(const AVIF *h) {
    /* read_ftyp() stores the box type in major_brand and the file's major
     * brand in minor_version; compatible_brands[0] is the file minor
     * version, the actual brands start at index 1. */
    if (h->ftyp.minor_version == FOURCC2UINT('a','v','i','s')) return 1;
    if (!h->ftyp.compatible_brands) return 0;
    int n = (int)((h->ftyp.size - 12) / 4);
    for (int i = 1; i < n; i++)
        if (h->ftyp.compatible_brands[i] == FOURCC2UINT('a','v','i','s')) return 1;
    return 0;
}
static uint32_t avif_stsc_spc(const struct stsc_box *stsc, uint32_t ci) {
    uint32_t one = ci + 1, r = 0;
    for (uint32_t i = 0; i < stsc->entry_count; i++)
        if (stsc->first_chunk[i] <= one) r = stsc->sample_per_chunk[i];
    return r;
}

struct avif_sample_ref {
    uint64_t offset;
    uint32_t size;
    uint32_t duration_ms;
};

/* Locate the first av01 track whose handler is 'auxv' (alpha) or a colour
 * video handler.  AVIF sequences signal the alpha plane as a second track. */
static int
avif_find_av01_track(const struct moov_box *moov, int want_aux,
                     int *stsd_idx_out)
{
    for (int t = 0; t < moov->trak_num; t++) {
        int is_aux = moov->trak[t].mdia.hdlr.handler_type ==
                     FOURCC2UINT('a','u','x','v');
        if (is_aux != want_aux)
            continue;
        struct stbl_box *stbl = &moov->trak[t].mdia.minf.stbl;
        for (uint32_t s = 0; s < stbl->stsd.entry_count; s++) {
            if (stbl->stsd.entries[s] &&
                stbl->stsd.entries[s]->type == FOURCC2UINT('a','v','0','1')) {
                *stsd_idx_out = (int)s;
                return t;
            }
        }
    }
    return -1;
}

/* Map every sample of a track to its file offset, size and presentation
 * duration.  Chunks are contiguous and stsc gives the run lengths. */
static int
avif_collect_samples(struct trak_box *trak, int stsd_idx UNUSED,
                     struct avif_sample_ref **out, int *count_out)
{
    struct stbl_box *stbl = &trak->mdia.minf.stbl;
    struct mdhd_box *mdhd = &trak->mdia.mdhd;
    int count = (int)stbl->stsz.sample_count;

    *out = NULL;
    *count_out = 0;
    if (count <= 0 || stbl->stco.entry_count == 0)
        return -EINVAL;
    struct avif_sample_ref *tab = calloc((size_t)count, sizeof(*tab));
    if (!tab)
        return -ENOMEM;
    uint32_t timescale = mdhd->timescale ? mdhd->timescale : 1;
    uint32_t ei = 0;
    uint32_t erem = stbl->stts.entry_count ? stbl->stts.sample_count[0] : 0;
    int si = 0;

    for (uint32_t ci = 0; ci < stbl->stco.entry_count && si < count; ci++) {
        uint64_t cum = stbl->stco.chunk_offset[ci];
        uint32_t spc = avif_stsc_spc(&stbl->stsc, ci);

        if (spc == 0)
            spc = (uint32_t)(count - si);
        for (uint32_t k = 0; k < spc && si < count; k++, si++) {
            uint32_t dt = 0;
            tab[si].offset = cum;
            tab[si].size = stbl->stsz.entry_size[si];
            if (ei < stbl->stts.entry_count) {
                dt = stbl->stts.sample_delta[ei];
                if (erem > 0 && --erem == 0 &&
                    ei + 1 < stbl->stts.entry_count) {
                    ei++;
                    erem = stbl->stts.sample_count[ei];
                }
            }
            tab[si].duration_ms =
                (uint32_t)(((uint64_t)dt * 1000 + timescale / 2) / timescale);
            cum += tab[si].size;
        }
    }
    if (si != count) {
        free(tab);
        return -EINVAL;
    }
    *out = tab;
    *count_out = count;
    return 0;
}

static uint8_t *
avif_read_sample(FILE *f, const struct avif_sample_ref *s)
{
    uint8_t *data = malloc(s->size ? s->size : 1);

    if (!data)
        return NULL;
    if (s->size > 0) {
        fseek(f, (long)s->offset, SEEK_SET);
        if (fread(data, 1, s->size, f) != s->size) {
            free(data);
            return NULL;
        }
    }
    return data;
}

static int
avif_decode_moov_track(FILE *f, struct moov_box *moov, struct pic **first_pic_out)
{
    if (first_pic_out)
        *first_pic_out = NULL;
    int cstsd = -1, astsd = -1;
    int ct = avif_find_av01_track(moov, 0, &cstsd);
    if (ct < 0)
        return 0;
    int at = avif_find_av01_track(moov, 1, &astsd);
    struct avif_sample_ref *ctab = NULL, *atab = NULL;
    int cn = 0, an = 0;

    if (avif_collect_samples(&moov->trak[ct], cstsd, &ctab, &cn) != 0)
        return 0;
    if (at >= 0 &&
        avif_collect_samples(&moov->trak[at], astsd, &atab, &an) != 0) {
        atab = NULL;
        an = 0;
    }
    struct AV1SampleEntry *ce = (struct AV1SampleEntry *)
        moov->trak[ct].mdia.minf.stbl.stsd.entries[cstsd];
    struct AV1SampleEntry *ae = at >= 0 ? (struct AV1SampleEntry *)
        moov->trak[at].mdia.minf.stbl.stsd.entries[astsd] : NULL;
    if (an > cn)
        an = cn;                /* the alpha track may be shorter */

    int decoded = 0;
    for (int i = 0; i < cn; i++) {
        uint8_t *cdata = avif_read_sample(f, &ctab[i]);
        struct av1_image img;
        memset(&img, 0, sizeof(img));
        if (!cdata ||
            av1_decode_track_sample(cdata, ctab[i].size, &ce->config,
                                    &img) != 0) {
            VERR(avif, "track sample %d failed; AVIF animation requires "
                 "all-intra samples", i);
            free(cdata);
            av1_image_free(&img);
            break;
        }
        free(cdata);

        struct av1_image alpha;
        int has_alpha = 0;
        memset(&alpha, 0, sizeof(alpha));
        if (ae && i < an) {
            uint8_t *adata = avif_read_sample(f, &atab[i]);
            if (adata &&
                av1_decode_track_sample(adata, atab[i].size, &ae->config,
                                        &alpha) == 0) {
                has_alpha = 1;
            } else {
                VWARN(avif, "alpha frame %d failed to decode", i);
                av1_image_free(&alpha);
            }
            free(adata);
        }

        int wide = img.bitdepth > 8;
        struct pic *p = pic_alloc(sizeof(AVIF));
        p->width = img.width;
        p->height = img.height;
        p->depth = wide ? 64 : 32;
        p->pitch = ((p->width + 15) >> 4) * 16 * (p->depth >> 3);
        p->format = wide ? CS_PIXELFORMAT_BGRA64 : CS_PIXELFORMAT_BGRA_BYTES;
        p->pixels = malloc((size_t)p->pitch * p->height);
        if (!p->pixels) {
            pic_free(p);
            av1_image_free(&img);
            if (has_alpha)
                av1_image_free(&alpha);
            break;
        }
        if (wide)
            av1_image_to_rgba16(&img, p->pixels, p->pitch);
        else
            av1_image_to_bgra(&img, p->pixels, p->pitch);
        if (has_alpha) {
            if (alpha.width == img.width && alpha.height == img.height) {
                if (wide)
                    avif_apply_alpha16(&alpha, p->pixels, p->pitch,
                                       img.width, img.height, 0);
                else
                    avif_apply_alpha(&alpha, p->pixels, p->pitch,
                                     img.width, img.height, 0);
            } else {
                VWARN(avif, "alpha frame is %dx%d, picture is %dx%d",
                      alpha.width, alpha.height, img.width, img.height);
            }
        }
        pic_set_duration(p, ctab[i].duration_ms);
        if (decoded == 0 && first_pic_out) {
            *first_pic_out = p;
        } else if (!file_enqueue_pic(p)) {
            VERR(avif, "animation queue is full at frame %d", i);
            pic_free(p);
        }
        decoded++;
        av1_image_free(&img);
        if (has_alpha)
            av1_image_free(&alpha);
    }
    free(ctab);
    free(atab);
    return decoded;
}


void decode_av01(uint8_t *data, uint64_t len, uint8_t **pixels UNUSED)
{
    // hexdump(stdout, "coded ", "", data, 256);
    uint8_t *p = data;
    while (len >= 4) {
        uint32_t sample_len = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                              (uint32_t)p[2] << 8 | p[3];
        /* the payloads reach this legacy path only when they could not be
         * decoded, so the sample framing cannot be trusted */
        if (sample_len > len - 4) {
            break;
        }
        len -= 4 + sample_len;
        p += 4 + sample_len;
    }
}

/* Read every extent of an item into one contiguous buffer: the extents are
 * pieces of the coded payload and only their concatenation is decodable. */
static int pre_read_item(AVIF *h, FILE *f, uint32_t idx, uint64_t file_size)
{
    const struct item_location *it = h->items[idx].item;
    uint64_t total = 0;

    for (int i = 0; i < it->extent_count; i++) {
        uint64_t off = it->base_offset + it->extents[i].extent_offset;
        uint64_t len = it->extents[i].extent_length;
        if (off > file_size || len > file_size - off) {
            VERR(avif, "item %d extent %d runs past the file", it->item_id, i);
            return -EINVAL;
        }
        if (total + len < total || total + len > SIZE_MAX) {
            return -EOVERFLOW;
        }
        total += len;
    }

    uint8_t *buf = malloc(total ? (size_t)total : 1);
    if (!buf) {
        return -ENOMEM;
    }
    size_t passed = 0;
    for (int i = 0; i < it->extent_count; i++) {
        uint64_t off = it->base_offset + it->extents[i].extent_offset;
        uint64_t len = it->extents[i].extent_length;
        if (len && fseek(f, (long)off, SEEK_SET) != 0) {
            free(buf);
            return -EBADF;
        }
        if (len && fread(buf + passed, 1, (size_t)len, f) != len) {
            free(buf);
            return -EBADF;
        }
        passed += (size_t)len;
    }

    free(h->items[idx].data);
    h->items[idx].data = buf;
    h->items[idx].length = total;
    return 0;
}

static void decode_items(AVIF *h, FILE *f, uint64_t file_size,
                         uint8_t **pixels)
{
    for (int i = 0; i < h->meta.iloc.item_count; i++) {
        if (pre_read_item(h, f, i, file_size) < 0) {
            continue;
        }
        if (avif_item_protected(h, h->items[i].item->item_id)) {
            VDBG(avif, "item %u is protected, skipping",
                 h->items[i].item->item_id);
            continue;
        }
        if (h->items[i].type == TYPE2UINT("mime")) {
            // exif mime, skip it
            // hexdump(stdout, "exif:", "", h->items[i].data,
            // h->items[i].length);
            VDBG(avif, "exif %" PRIu64, h->items[i].length);
        } else if (h->items[i].type == TYPE2UINT("av01")) {
            // take it as real coded data
            VINFO(avif, "decoding id 0x%p len %" PRIu64, (void *)h->items[i].data, h->items[i].length);
            decode_av01(h->items[i].data, h->items[i].length, pixels);
        }
    }
}
/* An auxiliary item (the alpha plane of a still image) is the source of an
 * auxl reference, never the picture to show. */
static int
avif_item_is_aux(const AVIF *h, uint32_t item_id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        const struct itemtype_ref_box *r = &h->meta.iref.refs[i];
        if (r->type == FOURCC2UINT('a', 'u', 'x', 'l') &&
            r->from_item_id == item_id) {
            return 1;
        }
    }
    return 0;
}

/* The av1C property associated with an item; its configOBUs may hold the
 * sequence header, which the item payload then omits. */
static const struct av1C_box *
avif_item_av1c(const AVIF *h, uint32_t item_id)
{
    const struct ipma_box *ipma = &h->meta.iprp.ipma;

    for (uint32_t i = 0; i < ipma->entry_count; i++) {
        if (ipma->entries[i].item_id != item_id) {
            continue;
        }
        for (int j = 0; j < ipma->entries[i].association_count; j++) {
            int idx = ipma->entries[i].property_index[j] & 0x7fff;
            struct box *prop;

            if (idx < 1 || idx > h->meta.iprp.ipco.n_property) {
                continue;
            }
            prop = h->meta.iprp.ipco.property[idx - 1];
            if (prop && prop->type == TYPE2UINT("av1C")) {
                return (const struct av1C_box *)prop;
            }
        }
    }
    return NULL;
}

/* Join the configOBUs in front of the item payload when present: the
 * decoder consumes one low overhead bitstream and needs the sequence
 * header from whichever place carries it. */
static int
avif_decode_item(const AVIF *h, uint32_t item_id, const uint8_t *data,
                 size_t len, struct av1_image *img)
{
    const struct av1C_box *av1c = avif_item_av1c(h, item_id);
    uint8_t *joined;
    int rc;

    if (!av1c || !av1c->config_obus || av1c->config_obus_len == 0) {
        return av1_decode_image(data, len, img);
    }
    joined = malloc((size_t)av1c->config_obus_len + len);
    if (!joined) {
        return -ENOMEM;
    }
    memcpy(joined, av1c->config_obus, av1c->config_obus_len);
    memcpy(joined + av1c->config_obus_len, data, len);
    rc = av1_decode_image(joined, (size_t)av1c->config_obus_len + len, img);
    free(joined);
    return rc;
}

/* The colr property associated with an item.  An embedded ICC profile is
 * preferred over the nclx code points when both are present. */
static const struct colr_box *
avif_item_colr(const AVIF *h, uint32_t item_id)
{
    const struct ipma_box *ipma = &h->meta.iprp.ipma;
    const struct colr_box *nclx = NULL;

    for (uint32_t i = 0; i < ipma->entry_count; i++) {
        if (ipma->entries[i].item_id != item_id) {
            continue;
        }
        for (int j = 0; j < ipma->entries[i].association_count; j++) {
            int idx = ipma->entries[i].property_index[j] & 0x7fff;
            struct box *prop;
            const struct colr_box *colr;

            if (idx < 1 || idx > h->meta.iprp.ipco.n_property) {
                continue;
            }
            prop = h->meta.iprp.ipco.property[idx - 1];
            if (!prop || prop->type != TYPE2UINT("colr")) {
                continue;
            }
            colr = (const struct colr_box *)prop;
            if (colr->color_type == FOURCC2UINT('p', 'r', 'o', 'f') ||
                colr->color_type == FOURCC2UINT('r', 'I', 'C', 'C')) {
                return colr;
            }
            nclx = colr;
        }
    }
    return nclx;
}

/* Container colour information wins over the CICP codes in the bitstream.
 * After the profile is built we fill in OBU_METADATA HDR fields from the
 * decoded image so the tone mapper stops assuming 10000 / 1000 cd/m2 when
 * the bitstream actually carried CLLI / MDCV. */
static struct cs_profile *
avif_color_profile(const AVIF *h, uint32_t item_id,
                   const struct av1_image *img)
{
    const struct colr_box *colr = avif_item_colr(h, item_id);
    struct cs_profile *cp = NULL;

    if (colr) {
        if (colr->icc_data && colr->icc_len) {
            cp = cs_profile_from_icc(colr->icc_data, colr->icc_len);
            if (!cp) {
                VWARN(avif, "the embedded ICC profile is not supported");
            }
        }
        if (!cp && colr->color_type == FOURCC2UINT('n', 'c', 'l', 'x')) {
            cp = cs_profile_from_cicp(colr->color_primaries,
                                      colr->transfer_characteristics);
        }
    }
    if (!cp) {
        cp = cs_profile_from_cicp(img->color_primaries,
                                  img->transfer_characteristics);
    }

    /* OBU_METADATA HDR is independent of ICC / nclx; always pass it through
     * when the bitstream carried it.  Zero fields mean "not signalled" and
     * the tone mapper keeps its legacy 10000 / 1000 cd/m2 defaults. */
    if (cp && img) {
        cs_profile_set_hdr(cp,
                           img->hdr_cll_present,
                           img->max_content_light_level,
                           img->max_frame_average_light_level,
                           img->hdr_mdcv_present,
                           img->luminance_max,
                           img->luminance_min);
    }
    return cp;
}

/* The alpha plane of a still image is a monochrome AV1 item that an auxl
 * reference points from; the reference targets the colour item. */
static int
avif_find_alpha_item(const AVIF *h, uint32_t color_id)
{
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        const struct itemtype_ref_box *r = &h->meta.iref.refs[i];
        if (r->type != FOURCC2UINT('a', 'u', 'x', 'l')) {
            continue;
        }
        for (int j = 0; j < r->ref_count; j++) {
            if (r->to_item_ids[j] != color_id) {
                continue;
            }
            for (int k = 0; k < h->meta.iloc.item_count; k++) {
                if (h->items[k].item->item_id == r->from_item_id &&
                    h->items[k].type == TYPE2UINT("av01")) {
                    return k;
                }
            }
        }
    }
    return -1;
}

/* Does this colour item carry premultiplied alpha?  Per AVIF spec the
 * container signals this by attaching a 'prem' property to the colour item
 * in the iprp/ipco/ipma property hierarchy.  basemedia.c's default ipco
 * slot keeps unknown boxes as a generic box with the correct .type, so we
 * can detect 'prem' here by walking ipma -> property -> type. */
static int
avif_color_is_premultiplied(const AVIF *h, uint32_t color_id)
{
    const struct ipma_box *ipma = &h->meta.iprp.ipma;
    const struct ipco_box *ipco = &h->meta.iprp.ipco;
    uint32_t prem_type = FOURCC2UINT('p', 'r', 'e', 'm');

    for (uint32_t i = 0; i < ipma->entry_count; i++) {
        const struct ipma_item *e = &ipma->entries[i];
        if (e->item_id != color_id) {
            continue;
        }
        for (int j = 0; j < e->association_count; j++) {
            int idx = e->property_index[j] & 0x7fff;
            if (idx < 1 || idx > ipco->n_property) {
                continue;
            }
            struct box *p = ipco->property[idx - 1];
            if (p && p->type == prem_type) {
                return 1;
            }
        }
    }
    return 0;
}

/* AVIF alpha is a separate monochrome image and is not premultiplied, so it
 * maps straight onto the alpha channel of the packed output.  When the
 * container signals premultiplied alpha (via the 'prem' property attached
 * to the colour item), we also divide the stored RGB values by the alpha
 * samples before writing them out. */
static void
avif_apply_alpha16(const struct av1_image *alpha, uint8_t *pixels, int pitch,
                   int w, int h, int premultiplied)
{
    int max = (1 << alpha->bitdepth) - 1;
    const uint16_t *src = alpha->plane[0];
    int stride = alpha->stride[0];

    for (int y = 0; y < h; y++) {
        uint16_t *row = (uint16_t *)(pixels + (size_t)y * pitch);
        for (int x = 0; x < w; x++) {
            int a = src[(size_t)y * stride + x];
            a = a < 0 ? 0 : a > max ? max : a;
            if (premultiplied) {
                /* inverse-premultiply R,G,B in place */
                if (a == 0) {
                    row[x * 4 + 0] = 0;
                    row[x * 4 + 1] = 0;
                    row[x * 4 + 2] = 0;
                } else {
                    for (int c = 0; c < 3; c++) {
                        int v = (int)row[x * 4 + c];
                        v = (int)(((int64_t)v * max + a / 2) / a);
                        if (v > max) {
                            v = max;
                        }
                        row[x * 4 + c] = (uint16_t)v;
                    }
                }
            }
            row[x * 4 + 3] =
                (uint16_t)(((int64_t)a * 65535 + max / 2) / max);
        }
    }
}

static void
avif_apply_alpha(const struct av1_image *alpha, uint8_t *pixels, int pitch,
                 int w, int h, int premultiplied)
{
    int shift = alpha->bitdepth - 8;
    const uint16_t *src = alpha->plane[0];
    int stride = alpha->stride[0];

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int a = src[(size_t)y * stride + x];
            if (shift > 0) {
                a = (a + (1 << (shift - 1))) >> shift;
            }
            a = a < 0 ? 0 : a > 255 ? 255 : a;
            if (premultiplied) {
                uint8_t *px = pixels + (size_t)y * pitch + x * 4;
                if (a == 0) {
                    px[0] = px[1] = px[2] = 0;
                } else {
                    int amax_clamped = 255;
                    for (int c = 0; c < 3; c++) {
                        int v = px[c];
                        v = (int)(((int64_t)v * amax_clamped + a / 2) / a);
                        if (v > 255) {
                            v = 255;
                        }
                        px[c] = (uint8_t)v;
                    }
                }
            }
            pixels[(size_t)y * pitch + x * 4 + 3] = (uint8_t)a;
        }
    }
}

/* Pick the coded image item: pitm names it, and an AVIF with alpha keeps the
 * colour image there while the alpha plane is a separate auxiliary item. */
static int
avif_find_image_item(const AVIF *h)
{
    int count = h->meta.iloc.item_count;
    uint32_t primary = h->meta.pitm.item_id;

    if (primary) {
        for (int i = 0; i < count; i++) {
            if (h->items[i].item->item_id == primary) {
                /* grid and other derived types have no single payload */
                return h->items[i].type == TYPE2UINT("av01") ? i : -1;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        if (h->items[i].type == TYPE2UINT("av01") &&
            !avif_item_is_aux(h, h->items[i].item->item_id)) {
            return i;
        }
    }
    for (int i = 0; i < count; i++) {
        if (h->items[i].type == TYPE2UINT("av01")) {
            return i;
        }
    }
    return -1;
}

static int
avif_parse_meta(AVIF *h, FILE *f, uint64_t size)
{
    if (size < 12) {
        return -EINVAL;
    }
    fseek(f, 0, SEEK_SET);
    int ftyp_size = read_ftyp(f, &h->ftyp);
    if (ftyp_size < 0 || (uint64_t)ftyp_size > size) {
        return -EINVAL;
    }

    uint64_t left = size - ftyp_size;
    while (left >= 8) {
        struct box b;
        uint32_t type = probe_box(f, &b);
        if (b.size < 8 || b.size > left) {
            VERR(avif, "box %s size %" PRIu64 " runs past the file",
                 UINT2TYPE(type), b.size);
            break;
        }
        switch (type) {
        case FOURCC2UINT('m', 'e', 't', 'a'):
            read_meta_box(f, &h->meta);
            break;
        case FOURCC2UINT('m', 'o', 'o', 'v'):
            h->moov = realloc(h->moov, (h->moov_num + 1) * sizeof(struct moov_box));
            read_moov_box(f, h->moov + h->moov_num);
            h->moov_num++;
            break;
        case FOURCC2UINT('m', 'd', 'a', 't'):
            h->mdat_num++;
            fseek(f, b.size, SEEK_CUR);
            break;
        default:
            fseek(f, b.size, SEEK_CUR);
            break;
        }
        left -= b.size;
        VDBG(avif, "%s, read %" PRIu64 ", left %" PRIu64 "", UINT2TYPE(type),
             b.size, left);
    }
    return 0;
}

static void
avif_build_items(AVIF *h)
{
    if (h->items || h->meta.iloc.item_count == 0) {
        return;
    }
    h->items = calloc(h->meta.iloc.item_count, sizeof(struct avif_item));
    if (!h->items) {
        return;
    }
    for (int i = 0; i < h->meta.iloc.item_count; i++) {
        h->items[i].item = &h->meta.iloc.items[i];
        for (int j = 0; j < (int)h->meta.iinf.entry_count; j++) {
            if (h->meta.iinf.item_infos[j].item_id ==
                h->items[i].item->item_id) {
                h->items[i].type = h->meta.iinf.item_infos[j].item_type;
            }
        }
    }
}

static int
avif_item_index(const AVIF *h, uint32_t item_id)
{
    for (int i = 0; i < h->meta.iloc.item_count; i++) {
        if (h->items[i].item->item_id == item_id) {
            return i;
        }
    }
    return -1;
}

static const struct infe_box *
avif_item_info(const AVIF *h, uint32_t item_id)
{
    for (int i = 0; i < (int)h->meta.iinf.entry_count; i++) {
        if (h->meta.iinf.item_infos[i].item_id == item_id) {
            return &h->meta.iinf.item_infos[i];
        }
    }
    return NULL;
}

static uint64_t
avif_item_size(const AVIF *h, uint32_t item_id)
{
    int idx;
    uint64_t total = 0;

    if (!h->items) {
        return 0;
    }
    idx = avif_item_index(h, item_id);
    if (idx < 0) {
        return 0;
    }
    for (int i = 0; i < h->items[idx].item->extent_count; i++) {
        total += h->items[idx].item->extents[i].extent_length;
    }
    return total;
}

static int
avif_item_protected(const AVIF *h, uint32_t item_id)
{
    const struct infe_box *infe = avif_item_info(h, item_id);

    return infe && infe->item_protection_index != 0;
}

static int
avif_ref_targets(const struct itemtype_ref_box *r, uint32_t item_id)
{
    for (int i = 0; i < r->ref_count; i++) {
        if (r->to_item_ids[i] == item_id) {
            return 1;
        }
    }
    return 0;
}

/* XMP is a mime item whose content type is the RDF/XML one; other mime
 * items describe the picture differently. */
static int
avif_item_is_xmp(const AVIF *h, uint32_t item_id)
{
    const struct infe_box *infe = avif_item_info(h, item_id);

    return infe && infe->content_type &&
           strncmp(infe->content_type, "application/rdf+xml", 19) == 0;
}

/* Exif and XMP describe the primary item through cdsc, a thumbnail points
 * at it through thmb. */
static void
avif_collect_metadata(AVIF *h, uint32_t primary, FILE *f, uint64_t file_size)
{
    if (!h->items) {
        return;
    }
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        const struct itemtype_ref_box *r = &h->meta.iref.refs[i];
        int idx;
        uint32_t type;

        if ((r->type != TYPE2UINT("cdsc") && r->type != TYPE2UINT("thmb")) ||
            !primary || !avif_ref_targets(r, primary)) {
            continue;
        }
        idx = avif_item_index(h, r->from_item_id);
        if (idx < 0) {
            continue;
        }
        type = h->items[idx].type;
        if (r->type == TYPE2UINT("thmb")) {
            if (!h->thumb_id) {
                h->thumb_id = r->from_item_id;
            }
        } else if (type == TYPE2UINT("Exif")) {
            if (!h->exif_id) {
                h->exif_id = r->from_item_id;
            }
        } else if (type == TYPE2UINT("mime")) {
            if (!h->xmp_id && avif_item_is_xmp(h, r->from_item_id)) {
                h->xmp_id = r->from_item_id;
            }
        }
    }
    /* tolerate writers that omit the cdsc reference */
    if (!h->exif_id) {
        for (int i = 0; i < h->meta.iloc.item_count; i++) {
            if (h->items[i].type == TYPE2UINT("Exif")) {
                h->exif_id = h->items[i].item->item_id;
                break;
            }
        }
    }
    if (!h->xmp_id) {
        for (int i = 0; i < h->meta.iloc.item_count; i++) {
            uint32_t id = h->items[i].item->item_id;

            if (h->items[i].type == TYPE2UINT("mime") &&
                avif_item_is_xmp(h, id)) {
                h->xmp_id = id;
                break;
            }
        }
    }
    if (h->exif_id) {
        int idx = avif_item_index(h, h->exif_id);

        if (idx >= 0 && pre_read_item(h, f, idx, file_size) == 0) {
            h->exif_orientation =
                media_exif_orientation(h->items[idx].data,
                                       h->items[idx].length);
            free(h->items[idx].data);
            h->items[idx].data = NULL;
        }
    }
}

/* Pack one decoded image into p (BGRA / 16 bit BGRA), then apply the
 * container colour profile, the alpha auxiliary plane and the iprp
 * transforms.  Consumes img and alpha. */
static struct pic *
avif_build_picture(AVIF *h, struct pic *p, uint32_t color_id,
                   struct av1_image *img, struct av1_image *alpha,
                   int has_alpha)
{
    int wide = img->bitdepth > 8;

    p->width = img->width;
    p->height = img->height;
    p->depth = wide ? 64 : 32;
    p->pitch = ((p->width + 15) >> 4) * 16 * (p->depth >> 3);
    p->format = wide ? CS_PIXELFORMAT_BGRA64 : CS_PIXELFORMAT_BGRA_BYTES;
    p->pixels = malloc((size_t)p->pitch * p->height);
    if (p->pixels) {
        struct cs_profile *cp;

        if (wide) {
            av1_image_to_rgba16(img, p->pixels, p->pitch);
        } else {
            av1_image_to_bgra(img, p->pixels, p->pitch);
        }
        cp = avif_color_profile(h, color_id, img);
        if (cp) {
            if (wide) {
                cs_profile_apply_bgra16(cp, p->pixels, img->width,
                                        img->height, p->pitch);
            } else {
                cs_profile_apply_bgra(cp, p->pixels, img->width,
                                      img->height, p->pitch);
            }
            cs_profile_free(cp);
        }
        if (has_alpha) {
            if (alpha->width == img->width &&
                alpha->height == img->height) {
                int prem = avif_color_is_premultiplied(h, color_id);
                VDBG(avif, "colour item %u has prem=%d alpha",
                     color_id, prem);
                if (wide) {
                    avif_apply_alpha16(alpha, p->pixels, p->pitch,
                                       img->width, img->height, prem);
                } else {
                    avif_apply_alpha(alpha, p->pixels, p->pitch,
                                     img->width, img->height, prem);
                }
            } else {
                VWARN(avif, "alpha item is %dx%d, picture is %dx%d",
                     alpha->width, alpha->height, img->width, img->height);
            }
        }
        struct packed_frame frame = {
            .pixels = p->pixels,
            .width = p->width,
            .height = p->height,
            .pitch = p->pitch,
            .bpp = p->depth >> 3,
        };

        packed_apply_transforms(&h->meta.iprp, color_id, &frame);
        p->pixels = frame.pixels;
        p->width = frame.width;
        p->height = frame.height;
        p->pitch = frame.pitch;
    }
    av1_image_free(img);
    if (has_alpha) {
        av1_image_free(alpha);
    }
    return p->pixels ? p : NULL;
}

/* Compose a 'grid' derived item from its dimg tile items.  The tiles are
 * listed in raster order and all have the same coded format; the canvas is
 * the grid's output size. */
static int
avif_decode_grid_item(AVIF *h, FILE *f, uint64_t file_size, uint32_t grid_id,
                      struct av1_image *out)
{
    int gi = avif_item_index(h, grid_id);
    if (gi < 0) {
        return -EINVAL;
    }
    if (pre_read_item(h, f, gi, file_size) < 0) {
        return -EINVAL;
    }
    const uint8_t *d = h->items[gi].data;
    size_t dl = (size_t)h->items[gi].length;
    if (dl < 4) {
        return -EINVAL;
    }
    int flags = d[1];
    int rows = d[2] + 1;
    int cols = d[3] + 1;
    uint32_t ow, oh;

    if ((flags & 1) == 0) {
        if (dl < 8) return -EINVAL;
        ow = ((uint32_t)d[4] << 8) | d[5];
        oh = ((uint32_t)d[6] << 8) | d[7];
    } else {
        if (dl < 12) return -EINVAL;
        ow = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) |
             ((uint32_t)d[6] << 8) | d[7];
        oh = ((uint32_t)d[8] << 24) | ((uint32_t)d[9] << 16) |
             ((uint32_t)d[10] << 8) | d[11];
    }
    if (ow == 0 || oh == 0 || rows <= 0 || cols <= 0 ||
        (int64_t)rows * cols > 1024) {
        return -EINVAL;
    }
    const struct itemtype_ref_box *dimg = NULL;
    for (int i = 0; i < h->meta.iref.refs_count; i++) {
        const struct itemtype_ref_box *r = &h->meta.iref.refs[i];
        if (r->type == FOURCC2UINT('d','i','m','g') &&
            r->from_item_id == grid_id &&
            r->ref_count >= (uint32_t)(rows * cols)) {
            dimg = r;
            break;
        }
    }
    if (!dimg) {
        VERR(avif, "grid item %u has no matching dimg reference", grid_id);
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));
    int tile_w = ((int)ow + cols - 1) / cols;
    int tile_h = ((int)oh + rows - 1) / rows;
    int got = 0;

    for (int j = 0; j < rows * cols; j++) {
        int ti = avif_item_index(h, dimg->to_item_ids[j]);
        struct av1_image tile;

        if (ti < 0 || pre_read_item(h, f, ti, file_size) < 0) {
            goto fail;
        }
        memset(&tile, 0, sizeof(tile));
        if (avif_decode_item(h, dimg->to_item_ids[j], h->items[ti].data,
                             (size_t)h->items[ti].length, &tile) != 0) {
            VERR(avif, "grid tile %d failed to decode", j);
            av1_image_free(&tile);
            goto fail;
        }
        if (!got) {
            out->width = (int)ow;
            out->height = (int)oh;
            out->bitdepth = tile.bitdepth;
            out->num_planes = tile.num_planes;
            out->mono_chrome = tile.mono_chrome;
            out->subsampling_x = tile.subsampling_x;
            out->subsampling_y = tile.subsampling_y;
            out->color_range = tile.color_range;
            out->matrix_coefficients = tile.matrix_coefficients;
            out->color_primaries = tile.color_primaries;
            out->transfer_characteristics = tile.transfer_characteristics;
            out->chroma_sample_position = tile.chroma_sample_position;
            for (int pl = 0; pl < tile.num_planes && pl < 3; pl++) {
                int sx = pl ? tile.subsampling_x : 0;
                int sy = pl ? tile.subsampling_y : 0;
                int pw = ((int)ow + (1 << sx) - 1) >> sx;
                int ph = ((int)oh + (1 << sy) - 1) >> sy;
                out->stride[pl] = pw;
                out->plane[pl] = calloc((size_t)pw * ph, sizeof(uint16_t));
                if (!out->plane[pl]) {
                    av1_image_free(&tile);
                    goto fail;
                }
            }
            got = 1;
        }
        if (tile.bitdepth != out->bitdepth ||
            tile.subsampling_x != out->subsampling_x ||
            tile.subsampling_y != out->subsampling_y ||
            tile.num_planes != out->num_planes) {
            VERR(avif, "grid tile %d has a different pixel format", j);
            av1_image_free(&tile);
            goto fail;
        }
        int x0 = (j % cols) * tile_w;
        int y0 = (j / cols) * tile_h;
        for (int pl = 0; pl < out->num_planes && pl < 3; pl++) {
            int sx = pl ? out->subsampling_x : 0;
            int sy = pl ? out->subsampling_y : 0;
            int x = x0 >> sx;
            int y = y0 >> sy;
            int tw = tile.width >> sx;
            int th = tile.height >> sy;
            int pw = ((int)ow + (1 << sx) - 1) >> sx;
            int ph = ((int)oh + (1 << sy) - 1) >> sy;
            if (x >= pw || y >= ph) {
                continue;
            }
            int cw = tw < pw - x ? tw : pw - x;
            int ch = th < ph - y ? th : ph - y;
            for (int r = 0; r < ch; r++) {
                memcpy(out->plane[pl] + (size_t)(y + r) * out->stride[pl] + x,
                       tile.plane[pl] + (size_t)r * tile.stride[pl],
                       (size_t)cw * sizeof(uint16_t));
            }
        }
        av1_image_free(&tile);
    }
    return got ? 0 : -EINVAL;

fail:
    for (int pl = 0; pl < 3; pl++) {
        free(out->plane[pl]);
        out->plane[pl] = NULL;
    }
    return -EINVAL;
}

static struct pic*
AVIF_load(const char *filename, int skip_flag UNUSED)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 12) {
        fclose(f);
        return NULL;
    }

    struct pic *p = pic_alloc(sizeof(AVIF));
    AVIF *h = p->pic;
    if (avif_parse_meta(h, f, (uint64_t)file_size) < 0) {
        fclose(f);
        AVIF_free(p);
        return NULL;
    }
    avif_build_items(h);

    /* Metadata items hang off the primary picture; also record who they
     * are so AVIF_info() can report them. */
    uint32_t primary_id = h->meta.pitm.item_id;
    int prim_idx = avif_find_image_item(h);
    if (!primary_id && prim_idx >= 0) {
        primary_id = h->items[prim_idx].item->item_id;
    }
    avif_collect_metadata(h, primary_id, f, (uint64_t)file_size);

    /* Track-based animation (avis brand + moov). */
    if (h->moov_num > 0 && avif_is_avis(h)) {
        struct pic *first = NULL;
        for (int m = 0; m < h->moov_num; m++) {
            avif_decode_moov_track(f, h->moov + m, m == 0 ? &first : NULL);
        }
        if (first) {
            fclose(f);
            /* p only carries the moov/meta box tree; the returned frame owns
             * its own empty AVIF container.  Keeping a pointer to p->pic
             * would dangle once p is freed. */
            AVIF_free(p);
            return first;
        }
    }

    /* Static grid items: the primary is a 'grid' derived image and the
     * pixels live in the dimg tile items. */
    int primary_idx = primary_id ? avif_item_index(h, primary_id) : -1;
    if (primary_idx >= 0 &&
        h->items[primary_idx].type == FOURCC2UINT('g','r','i','d') &&
        !avif_item_protected(h, primary_id)) {
        uint32_t color_id = primary_id;
        int alpha_idx = avif_find_alpha_item(h, color_id);
        struct av1_image alpha;
        int has_alpha = 0;

        if (alpha_idx >= 0 &&
            pre_read_item(h, f, alpha_idx, (uint64_t)file_size) == 0 &&
            h->items[alpha_idx].length > 0) {
            has_alpha = avif_decode_item(h, h->items[alpha_idx].item->item_id,
                                         h->items[alpha_idx].data,
                                         (size_t)h->items[alpha_idx].length,
                                         &alpha) == 0;
            free(h->items[alpha_idx].data);
            h->items[alpha_idx].data = NULL;
            if (!has_alpha) {
                VWARN(avif, "cannot decode the alpha item of the grid");
            }
        }
        struct av1_image gimg;
        if (avif_decode_grid_item(h, f, (uint64_t)file_size, color_id,
                                  &gimg) == 0) {
            fclose(f);
            struct pic *out = avif_build_picture(h, p, color_id, &gimg,
                                                 &alpha, has_alpha);
            if (out) {
                VINFO(avif, "decoded %dx%d grid with the built-in AV1 decoder",
                      p->width, p->height);
                return out;
            }
            AVIF_free(p);
            return NULL;
        }
        if (has_alpha) {
            av1_image_free(&alpha);
        }
        VWARN(avif, "grid item %u could not be composed", primary_id);
        /* fall through to the container-only path */
    }

    /* Decode the primary AV1 image item with the in-house spec-based
     * decoder.  The item payload is a low overhead bitstream. */
    int av01_idx = prim_idx;
    int protected_primary = av01_idx >= 0 &&
        avif_item_protected(h, h->items[av01_idx].item->item_id);
    if (protected_primary) {
        VWARN(avif, "item %u is protected, not decoding",
              h->items[av01_idx].item->item_id);
    }
    if (!protected_primary && av01_idx >= 0 &&
        pre_read_item(h, f, av01_idx, (uint64_t)file_size) == 0 &&
        h->items[av01_idx].length > 0) {
        uint32_t color_id = h->items[av01_idx].item->item_id;
        int alpha_idx = avif_find_alpha_item(h, color_id);
        if (alpha_idx == av01_idx ||
            (alpha_idx >= 0 &&
             (pre_read_item(h, f, alpha_idx, (uint64_t)file_size) < 0 ||
              h->items[alpha_idx].length == 0))) {
            alpha_idx = -1;
        }
        fclose(f);
        struct av1_image alpha;
        int has_alpha = 0;
        if (alpha_idx >= 0) {
            has_alpha = avif_decode_item(h, h->items[alpha_idx].item->item_id,
                                         h->items[alpha_idx].data,
                                         (size_t)h->items[alpha_idx].length,
                                         &alpha) == 0;
            free(h->items[alpha_idx].data);
            h->items[alpha_idx].data = NULL;
            if (!has_alpha) {
                VWARN(avif, "cannot decode the alpha item");
            }
        }
        struct av1_image img;
        int rc = avif_decode_item(h, color_id, h->items[av01_idx].data,
                                  (size_t)h->items[av01_idx].length, &img);
        free(h->items[av01_idx].data);
        h->items[av01_idx].data = NULL;
        if (rc == 0 && img.width > 0 && img.height > 0) {
            struct pic *out = avif_build_picture(h, p, color_id, &img,
                                                 &alpha, has_alpha);
            if (out) {
                VINFO(avif, "decoded %dx%d with the built-in AV1 decoder",
                      p->width, p->height);
                return out;
            }
            AVIF_free(p);
            return NULL;
        }
        if (rc == 0) {
            av1_image_free(&img);
        }
        if (has_alpha) {
            av1_image_free(&alpha);
        }
        VERR(av1, "in-house AV1 decode failed: %d", rc);
        /* fall through to the container-only path */
        /* The fallback re-parses the container from the start, so the first
         * attempt and its box tree have to go before p is reused. */
        AVIF_free(p);
        p = NULL;
        f = fopen(filename, "rb");
        if (f == NULL) {
            return NULL;
        }
        p = pic_alloc(sizeof(AVIF));
        h = p->pic;
        if (avif_parse_meta(h, f, (uint64_t)file_size) < 0) {
            fclose(f);
            AVIF_free(p);
            return NULL;
        }
    } else {
        fclose(f);
        f = fopen(filename, "rb");
        if (f == NULL) {
            return NULL;
        }
    }

    /* Container-only path.  Probe-only files (and the synthetic fixture in
     * the test suite) have no decodable AV1 item but still expose the ispe
     * size. */
    for (int i = 0; i < h->meta.iprp.ipco.n_property; i ++) {
        struct box *prop = h->meta.iprp.ipco.property[i];
        if (prop && prop->type == TYPE2UINT("ispe")) {
            p->width = ((struct ispe_box *)prop)->image_width;
            p->height = ((struct ispe_box *)prop)->image_height;
        }
    }
    avif_build_items(h);
    VINFO(avif, "no AV1 item decoded, using the ispe size %dx%d", p->width,
          p->height);
    p->width = ((p->width + 3) >> 2) << 2;
    p->depth = 32;
    p->pitch = ((((p->width + 15) >> 4) * 16 * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = calloc(1, (size_t)p->pitch * p->height);
    p->format = CS_PIXELFORMAT_BGRA_BYTES;
    if (h->items) {
        decode_items(h, f, (uint64_t)file_size, (uint8_t **)&p->pixels);
    }

    fclose(f);

    return p;
}

static void 
AVIF_free(struct pic *p)
{
    AVIF * a = (AVIF *)p->pic;
    if (a->items) {
        for (int i = 0; i < a->meta.iloc.item_count; i++) {
            free(a->items[i].data);
        }
        free(a->items);
    }
    if (a->moov) {
        for (int i = 0; i < a->moov_num; i++) free_moov_box(a->moov + i);
        free(a->moov);
    }
    if (a->ftyp.compatible_brands) {
        free(a->ftyp.compatible_brands);
    }
    free_meta_box(&a->meta);
    free(p->pixels);
    pic_free(p);
}


static void
AVIF_info(FILE *f, struct pic* p)
{
    AVIF * h = (AVIF *)p->pic;
    fprintf(f, "AVIF file format:\n");
    fprintf(f, "-----------------------\n");
    char *s1;
    /* read_ftyp() reads the box header straight into ftyp_box, so the
     * "major_brand" member holds the box type, "minor_version" holds the
     * ftyp major brand and compatible_brands[0] holds the minor version. */
    fprintf(f, "\tbrand: %s", UINT2TYPE(h->ftyp.minor_version));
    if (h->ftyp.compatible_brands && h->ftyp.size >= 20) {
        fprintf(f, " compatible %s",
                UINT2TYPE(h->ftyp.compatible_brands[1]));
    }
    fprintf(f, "\n");
    fprintf(f, "\theight: %d, width: %d\n", p->height, p->width);

    fprintf(f, "\tmeta box --------------\n");
    fprintf(f, "\t");
    print_box(f, &h->meta.hdlr);
    s1 = UINT2TYPE(h->meta.hdlr.handler_type);
    fprintf(f, " pre_define=%d,handle_type=\"%s\"", h->meta.hdlr.pre_defined, s1);
    if (h->meta.hdlr.name) {
        fprintf(f, ",name=%s", h->meta.hdlr.name);
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.pitm);
    fprintf(f, " item_id=%u", h->meta.pitm.item_id);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iloc);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        fprintf(f, "\t\t");
        if (h->meta.iloc.version == 1 || h->meta.iloc.version == 2) {
            fprintf(f, "construct_method=%d,", h->meta.iloc.items[i].construct_method);
        }
        fprintf(f,"item_id=%u,data_ref_id=%d,base_offset=%" PRIu64
                ",extent_count=%d\n",
                h->meta.iloc.items[i].item_id,
                h->meta.iloc.items[i].data_ref_id,
                h->meta.iloc.items[i].base_offset,
                h->meta.iloc.items[i].extent_count);
        for (int j = 0; j < h->meta.iloc.items[i].extent_count; j ++) {
            fprintf(f, "\t\t\t");
            if (h->meta.iloc.version == 1 || h->meta.iloc.version == 2) {
                fprintf(f, "extent_id=%" PRIu64 ",",
                        h->meta.iloc.items[i].extents[j].extent_index);
            }
            fprintf(f, "extent_offset=%" PRIu64 ",extent_length=%" PRIu64 "\n",
                    h->meta.iloc.items[i].extents[j].extent_offset,
                    h->meta.iloc.items[i].extents[j].extent_length);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iinf);
    fprintf(f, "\n");
    for (int i = 0; i < (int)h->meta.iinf.entry_count; i++) {
        fprintf(f, "\t\t");
        print_box(f, &h->meta.iinf.item_infos[i]);
        s1 = UINT2TYPE(h->meta.iinf.item_infos[i].item_type);
        fprintf(f, " item_id=%u,item_protection_index=%d,item_type=%s",
            h->meta.iinf.item_infos[i].item_id, h->meta.iinf.item_infos[i].item_protection_index,
            s1);
        fprintf(f, "\n");
    }

    fprintf(f, "\t");
    print_box(f, &h->meta.iprp);
    fprintf(f, "\n\t");
    fprintf(f, "\t");
    print_box(f, &h->meta.iprp.ipco);
    fprintf(f, "\n\t");
    for (int i = 0; i < h->meta.iprp.ipco.n_property; i ++) {
        fprintf(f, "\t\t");
        print_box(f, h->meta.iprp.ipco.property[i]);
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            fprintf(f, ", width %d, height %d", 
                ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width,
                ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("av1C")) {
            struct av1C_box * av1c = (struct av1C_box *)(h->meta.iprp.ipco.property[i]);

            fprintf(f, " config marker %d, version %d, seq_profile %d, seq_level_idx_0 %d\n", av1c->marker,
                    av1c->version, av1c->seq_profile, av1c->seq_level_idx_0);
            fprintf(f, "\t\t\t\tseq_tier_0 %d, high_bitdepth %d, twelve_bit %d, monochrome %d\n",
                av1c->seq_tier_0, av1c->high_bitdepth, av1c->twelve_bit, av1c->monochrome);
            fprintf(f, "\t\t\t\tchroma_subsampling_x %d, chroma_subsampling_y %d, chroma_sample_position %d\n",
                av1c->chroma_subsampling_x, av1c->chroma_subsampling_y, av1c->chroma_sample_position);
    
            fprintf(f, "\t\t\t\tinitial_presentation_delay_present %d", av1c->initial_presentation_delay_present);
            if (av1c->initial_presentation_delay_present)
                fprintf(f, " initial_presentation_delay_minus_one %d\n", av1c->initial_presentation_delay_minus_one);            
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("pixi")) {
            struct pixi_box * pixi = (struct pixi_box *)(h->meta.iprp.ipco.property[i]);
            fprintf(f, "\tnum_channels : %d\n", pixi->num_channels);
            for (int i = 0; i < pixi->num_channels; i ++) {
                fprintf(f, "\t\t\t\t bits_per_channel %d\n", pixi->bits_per_channel[i]);
            }
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("irot")) {
            fprintf(f, ", angle %d", ((struct irot_box *)
                    h->meta.iprp.ipco.property[i])->angle * 90);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("imir")) {
            fprintf(f, ", axis %d", ((struct imir_box *)
                    h->meta.iprp.ipco.property[i])->axis);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("clap")) {
            struct clap_box *clap = (struct clap_box *)
                h->meta.iprp.ipco.property[i];
            fprintf(f, ", width %d/%d, height %d/%d, offset %d/%d %d/%d",
                    clap->cleanApertureWidthN, clap->cleanApertureWidthD,
                    clap->cleanApertureHeightN, clap->cleanApertureHeightD,
                    clap->horizOffN, clap->horizOffD,
                    clap->vertOffN, clap->vertOffD);
        } else if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("pasp")) {
            struct pasp_box *pasp = (struct pasp_box *)
                h->meta.iprp.ipco.property[i];
            fprintf(f, ", hSpacing %d, vSpacing %d", pasp->hSpacing,
                    pasp->vSpacing);
        }
        fprintf(f, "\n\t");
    }
    fprintf(f, "\t");
    print_box(f, &h->meta.iprp.ipma);
    fprintf(f, "\n");
    for (int i = 0; i < (int)h->meta.iprp.ipma.entry_count; i++) {
        struct ipma_item *ipma = &h->meta.iprp.ipma.entries[i];
        fprintf(f, "\t\titem %d: association_count %d\n", ipma->item_id, ipma->association_count);
        for (int j = 0; j < ipma->association_count; j ++) {
            fprintf(f, "\t\t\tessential %d, id %d \n", ipma->property_index[j] >> 15, ipma->property_index[j] & 0x7fff);
        }
    }
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iref);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iref.refs_count; i ++) {
        fprintf(f, "\t\t");
        s1 = UINT2TYPE(h->meta.iref.refs[i].type);
        fprintf(f, "ref_type=%s,from_item_id=%d,ref_count=%d", s1,
            h->meta.iref.refs[i].from_item_id,
            h->meta.iref.refs[i].ref_count);
        for (int j = 0; j < h->meta.iref.refs[i].ref_count; j ++) {
            fprintf(f, ",to_item=%d",  h->meta.iref.refs[i].to_item_ids[j]);
        }
        fprintf(f, "\n");
    }

    if (h->exif_id || h->xmp_id || h->thumb_id ||
        h->meta.ipro.protection_count) {
        fprintf(f, "\tmetadata:\n");
        if (h->exif_id) {
            fprintf(f, "\t\texif item %u, %" PRIu64
                    " bytes, orientation %d\n", h->exif_id,
                    avif_item_size(h, h->exif_id), h->exif_orientation);
        }
        if (h->xmp_id) {
            const struct infe_box *infe = avif_item_info(h, h->xmp_id);
            fprintf(f, "\t\txmp item %u, %" PRIu64 " bytes, type %s\n",
                    h->xmp_id, avif_item_size(h, h->xmp_id),
                    infe && infe->content_type ? infe->content_type : "?");
        }
        if (h->thumb_id) {
            fprintf(f, "\t\tthumbnail item %u, %" PRIu64 " bytes\n",
                    h->thumb_id, avif_item_size(h, h->thumb_id));
        }
        if (h->meta.ipro.protection_count) {
            fprintf(f, "\t\tprotection entries %d\n",
                    h->meta.ipro.protection_count);
        }
        fprintf(f, "\n");
    }
}



static struct file_ops avif_ops = {
    .name = "AVIF",
    .probe = AVIF_probe,
    .load = AVIF_load,
    .free = AVIF_free,
    .info = AVIF_info,
};

void AVIF_init(void)
{
    file_ops_register(&avif_ops);
}
