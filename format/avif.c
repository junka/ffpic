#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "bitstream.h"
#include "vlog.h"
#include "file.h"
#include "avif.h"

VLOG_REGISTER(avif, DEBUG)

static int
AVIF_probe(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
    struct ftyp_box h;
    int len = fread(&h, sizeof(h), 1, f);
    if (len < 1) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (h.major_brand == TYPE2UINT("ftyp")) {
        if(h.minor_version == TYPE2UINT("avif"))
        return 0;
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
    int op = choose_operating_point();
    int OperatingPointIdc = obu->points[op].operating_point_idc;
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
    return obu;
}

int parse_tile_group_obu(struct bits_vec *v, int size)
{

    return 0;
}

int parse_frame_obu(struct bits_vec *v)
{
    return 0;
}

int parse_tile_list_obu(struct bits_vec *v)
{
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

int parse_obu(struct bits_vec *v, int sz, struct obu_header **obu, int idx)
{
    struct obu_header h;
    parse_obu_header(&h, v);
    static struct sequence_header_obu *seqobu = NULL;
    static int SeenFrameHeader = -1;
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
        SeenFrameHeader = 0;
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


static int
read_av1c_box(FILE *f, struct box **bn)
{
    struct av1C_box *b = malloc(sizeof(struct av1C_box));
    b->n_obu = 0;
    // b->configOBUs = (struct obu_header **)malloc(sizeof(struct obu_header *) * 32);
    // memset(b->configOBUs, 0, sizeof(struct obu_header *) * 32);
    assert (b->configOBUs);
    if (*bn == NULL) {
        *bn = (struct box *)b;
    }
    fread(b, 8, 1, f);
    b->size = SWAP(b->size);

    fread(&b->type + 4, 4, 1, f);

    VDBG(avif, "size %d", b->size);
    uint8_t *data = malloc(b->size - 12);
    fread(data, b->size - 12, 1, f);
    struct bits_vec *v = bits_vec_alloc(data, b->size - 12, BITS_LSB);
    
    // configOBUs field contains zero or more OBUs. Any OBU may be present provided that the following procedures produce compliant AV1 bitstreams:

    //the configOBUs field SHALL contain at most one Sequence Header OBU
    // and if present, it SHALL be the first OBU.

    int sz = b->size - 12;
    while (sz) {
        // if (b->n_obu >= 32) {
        //     b->configOBUs = realloc(b->configOBUs, sizeof(void *) *(b->n_obu + 1));
        // }
        assert(b->configOBUs);
        sz -= parse_obu(v, sz, b->configOBUs + b->n_obu, b->n_obu);
        b->n_obu ++;
    }
    bits_vec_free(v);
    VDBG(avif, "n_obu %d", b->n_obu);
    for (int i = 0; i < b->n_obu; i ++) {
        struct obu_header *h = b->configOBUs[i];
        if (h->obu_type == OBU_SEQUENCE_HEADER) {
            struct sequence_header_obu *obu = (struct sequence_header_obu *)b->configOBUs[i];
            VDBG(avif, "still_picture %d, reduced_still_picture_header %d", obu->still_picture, obu->reduced_still_picture_header);
        }
    }

    VDBG(avif, "av1C: version %d, marker 0x%x", b->version,
                b->marker);

    return b->size;
}

static void
read_meta_box(FILE *f, struct av1_meta_box *meta)
{
    struct box b;
    uint32_t type = read_full_box(f, meta, -1);
    if (type != TYPE2UINT("meta")) {
        VERR(avif, "error, it is not a meta after ftyp");
        return;
    }
    int size = meta->size -= 12;
    while (size) {
        type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        VDBG(avif, "type (%s), size %d", UINT2TYPE(type), b.size);
        switch (type) {
        case TYPE2UINT("hdlr"):
            read_hdlr_box(f, &meta->hdlr);
            break;
        case TYPE2UINT("pitm"):
            read_pitm_box(f, &meta->pitm);
            break;
        case TYPE2UINT("iloc"):
            read_iloc_box(f, &meta->iloc);
            break;
        case TYPE2UINT("iinf"):
            read_iinf_box(f, &meta->iinf);
            break;
        case TYPE2UINT("iref"):
            read_iref_box(f, &meta->iref);
            break;
        case TYPE2UINT("iprp"):
            read_iprp_box(f, &meta->iprp, &read_av1c_box);
            break;
        default:
            break;
        }
        size -= b.size;
    }
    
}

static struct pic* 
AVIF_load(const char *filename)
{
        struct pic *p = pic_alloc(sizeof(AVIF));
    AVIF *h = p->pic;
    FILE *f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size -= read_ftyp(f, &h->ftyp);
    h->mdat = malloc(sizeof(struct mdat_box));
    struct box b;
    while (size) {
        uint32_t type = read_box(f, &b, size);
        fseek(f, -8, SEEK_CUR);
        switch (type) {
        case TYPE2UINT("meta"):
            read_meta_box(f, &h->meta);
            break;
        case TYPE2UINT("mdat"):
            h->mdat_num ++;
            h->mdat = realloc(h->mdat, h->mdat_num * sizeof(struct mdat_box));
            read_mdat_box(f, h->mdat + h->mdat_num - 1);
            break;
        default:
            break;
        }
        size -= b.size;
        VDBG(avif, "%s, read %d, left %d", UINT2TYPE(type), b.size, size);
    }

    // extract some info from meta box
    for (int i = 0; i < 2; i ++) {
        if (h->meta.iprp.ipco.property[i]->type == TYPE2UINT("ispe")) {
            p->width = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_width;
            p->height = ((struct ispe_box *)(h->meta.iprp.ipco.property[i]))->image_height;
        }
    }
    // h->items = malloc(h->meta.iloc.item_count * sizeof(struct heif_item));
    // for (int i = 0; i < h->meta.iloc.item_count; i ++) {
    //     h->items[i].item = &h->meta.iloc.items[i];
    // }


    fclose(f);

    return p;
}

static void 
AVIF_free(struct pic *p)
{
    AVIF * a = (AVIF *)p->pic;
    if (a->mdat) {
        free(a->mdat);
    }
    pic_free(p);
}


static void
AVIF_info(FILE *f, struct pic* p)
{
    AVIF * h = (AVIF *)p->pic;
    fprintf(f, "AVIF file format:\n");
    fprintf(f, "-----------------------\n");
    char *s1 = UINT2TYPE(h->ftyp.minor_version);
    fprintf(f, "\tbrand: %s", s1);
    char *s2 = UINT2TYPE(h->ftyp.compatible_brands[1]);
    fprintf(f," compatible %s\n", s2);
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
    fprintf(f, " item_id=%d", h->meta.pitm.item_id);
    fprintf(f, "\n");

    fprintf(f, "\t");
    print_box(f, &h->meta.iloc);
    fprintf(f, "\n");
    for (int i = 0; i < h->meta.iloc.item_count; i ++) {
        fprintf(f, "\t\t");
        if (h->meta.iloc.version == 1) {
            fprintf(f, "construct_method=%d,", h->meta.iloc.items[i].construct_method);
        }
        fprintf(f,"item_id=%d,data_ref_id=%d,base_offset=%" PRIu64
                ",extent_count=%d\n",
                h->meta.iloc.items[i].item_id,
                h->meta.iloc.items[i].data_ref_id,
                h->meta.iloc.items[i].base_offset,
                h->meta.iloc.items[i].extent_count);
        for (int j = 0; j < h->meta.iloc.items[i].extent_count; j ++) {
            fprintf(f, "\t\t\t");
            if (h->meta.iloc.version == 1) {
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
        fprintf(f, " item_id=%d,item_protection_index=%d,item_type=%s",
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
    for (int i = 0; i < h->meta.iprp.ipco.n_prop; i ++) {
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
            fprintf(f, "\t\t\tessential %d, id %d \n", ipma->association[j] >> 15, ipma->association[j] & 0x7fff);
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
