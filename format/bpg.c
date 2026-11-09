/*
 * BPG image loader
 *
 * BPG 容器头 (BPG 0.9.5 spec):
 *   magic                4B  0x42 0x50 0x47 0xfb ("BPG\xfb")
 *   flags1               1B  [pixel_format:3][alpha1_flag:1][bit_depth_minus_8:4]
 *   flags2               1B  [color_space:4][extension_present_flag:1][alpha2_flag:1]
 *                            [limited_range_flag:1][animation_flag:1]
 *   (bit fields are MSB-first, matching libbpg bpg_decode_header())
 *   width                ue7(32)
 *   height               ue7(32)
 *   picture_data_length  ue7(32)  0 = to EOF
 *   [若 extension_present_flag=1]
 *     extension_data_len ue7(32)
 *     extension blocks   [extension_data_len bytes]
 *   hevc_header_length   ue7(32)
 *   hevc_header          [hevc_header_length bytes]
 *   hevc_data            [picture_data_length or EOF bytes]
 *
 * hevc_data 是完整 HEVC Annex-B 流**去掉第一个起始码** (BPG 规范 L337).
 * 后续每个 NAL 仍带原始起始码 (3 或 4 字节). 我们在加载时把 hevc_data 前补
 * 一个 4 字节起始码, 恢复完整 Annex-B, 再按起始码切出 NAL payload 喂给
 * 现有 parse_nalu.  这让我们完全复用 coding/hevc.c 的参数集解析、slice
 * 解码和 YUV→BGRA 路径, 不复制解码器, 也不改动 hevc.c 的既有语义.
 *
 * alpha 暂不支持 (alpha1_flag / alpha2_flag 任一置位即拒绝). alpha1 是
 * 同一 Annex-B 流里 nuh_layer_id==1 的辅助 HEVC 平面, 需要把两层码流分开
 * 解码并按 premultiplied 语义合并; alpha2 单独置位时是 W 平面. 两者都
 * 需要额外工程量和真实样本. 遇到 alpha 文件直接返回 NULL, 明确告知拒绝,
 * 避免输出不透明图丢 alpha 而静默失真.  (extension tag 5 是动画控制,
 * 不是 alpha.)
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basemedia.h"
#include "bitstream.h"
#include "byteorder.h"
#include "file.h"
#include "bpg.h"
#include "utils.h"
#include "vlog.h"
#include "colorspace.h"

#include "hevc.h"

VLOG_REGISTER(bpg, DEBUG)

/* BPG 容器头解析的临时信息结构 (内部使用, 不对外暴露) */
typedef struct {
    int width, height;
    int pixel_format;        /* 0=gray 1=4:2:0 2=4:2:2 3=4:4:4 */
    int bit_depth;           /* 8..14 */
    int color_space;         /* 0=YCbCr BT.601 ... (BPG spec) */
    int alpha1_flag;
    int alpha2_flag;
    int limited_range_flag;
    int animation_flag;
    int extension_present_flag;
    size_t hevc_data_off;    /* 在文件 buffer 内的偏移 */
    size_t hevc_data_len;
} bpg_header_info;

static int
BPG_probe(FILE *f)
{
    struct bpg_file header;
    if (f == NULL)
        return -ENOENT;
    FFREAD_IGNORE(&header, sizeof(header), 1, f);
    if (SWAP(header.file_magic) == 0x425047fb)
        return 0;
    return -EINVAL;
}

/* 从内存 buffer 按 ue7(32) 读无符号整数.
 * BPG 规范 L114-L124: 每字节 7 bit, MSB=1 表示后续还有字节, 仅用于 BPG
 * 容器头内的字段 (字节对齐).  出错或越界返回 -1. */
static int64_t
read_ue7_buf(const uint8_t *buf, size_t buf_len, size_t *idx)
{
    if (*idx >= buf_len)
        return -1;
    uint64_t ret = buf[*idx] & 0x7f;
    bool more = (buf[*idx] & 0x80) != 0;
    (*idx)++;
    while (more) {
        if (*idx >= buf_len)
            return -1;
        ret = (ret << 7) | (buf[*idx] & 0x7f);
        more = (buf[*idx] & 0x80) != 0;
        (*idx)++;
    }
    return (int64_t)ret;
}

/* BPG 容器头解析.
 * extension_present_flag 分支已改为有边界的遍历: 先读 extension_data_len,
 * 再在该范围内按 tag/tag_len 逐个跳过, 所有越界都返回 -errno, 不再死循环. */
static int
BPG_parse_header(const uint8_t *buf, size_t buf_len, bpg_header_info *info)
{
    size_t idx = 0;

    if (buf_len < 4) return -EINVAL;
    uint32_t magic = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                     ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
    if (magic != 0x425047fb) {
        VERR(bpg, "bad magic 0x%08" PRIx32, magic);
        return -EINVAL;
    }
    idx += 4;

    if (idx + 2 > buf_len) return -EINVAL;
    uint8_t flags1 = buf[idx++];
    uint8_t flags2 = buf[idx++];
    /* BPG spec / libbpg bit order: the header bytes are MSB-first.
     *   flags1: [pixel_format:3][alpha1_flag:1][bit_depth_minus_8:4]
     *   flags2: [color_space:4][ext:1][alpha2:1][limited_range:1][animation:1] */
    info->pixel_format       = (flags1 >> 5) & 0x07;
    info->alpha1_flag        = (flags1 >> 4) & 1;
    info->bit_depth          = 8 + (flags1 & 0x0f);
    info->color_space        = (flags2 >> 4) & 0x0f;
    int ext_present          = (flags2 >> 3) & 1;
    info->alpha2_flag        = (flags2 >> 2) & 1;
    info->limited_range_flag = (flags2 >> 1) & 1;
    info->animation_flag     = flags2 & 1;
    info->extension_present_flag = ext_present;

    if (info->pixel_format > 5 || info->bit_depth > 14) {
        VERR(bpg, "unsupported pixel_format %d bit_depth %d",
             info->pixel_format, info->bit_depth);
        return -EINVAL;
    }

    VDBG(bpg, "flags1=0x%02x flags2=0x%02x fmt=%d bd=%d cs=%d ext=%d a1=%d a2=%d lr=%d anim=%d",
         flags1, flags2, info->pixel_format, info->bit_depth, info->color_space,
         ext_present, info->alpha1_flag, info->alpha2_flag,
         info->limited_range_flag, info->animation_flag);

    int64_t v;
    v = read_ue7_buf(buf, buf_len, &idx);
    if (v < 0) return -EINVAL;
    info->width = (int)v;
    v = read_ue7_buf(buf, buf_len, &idx);
    if (v < 0) return -EINVAL;
    info->height = (int)v;
    v = read_ue7_buf(buf, buf_len, &idx);
    if (v < 0) return -EINVAL;
    uint64_t picture_data_len = (uint64_t)v;
    VDBG(bpg, "width=%d height=%d picture_data_len=%" PRIu64,
         info->width, info->height, picture_data_len);

    if (info->width <= 0 || info->height <= 0) {
        VERR(bpg, "invalid dimensions %dx%d", info->width, info->height);
        return -EINVAL;
    }

    /* extension blocks — 有边界, 不再死循环 */
    if (ext_present) {
        v = read_ue7_buf(buf, buf_len, &idx);
        if (v < 0) return -EINVAL;
        uint64_t ext_len = (uint64_t)v;
        size_t ext_end = idx + (size_t)ext_len;
        if (ext_end > buf_len) {
            VERR(bpg, "extension runs past EOF (%zu + %" PRIu64 " > %zu)",
                 idx, ext_len, buf_len);
            return -EINVAL;
        }
        VDBG(bpg, "extension len=%" PRIu64 " bytes", ext_len);
        while (idx < ext_end) {
            int64_t tag = read_ue7_buf(buf, buf_len, &idx);
            if (tag < 0 || idx > ext_end) return -EINVAL;
            int64_t tlen = read_ue7_buf(buf, buf_len, &idx);
            if (tlen < 0 || idx + (size_t)tlen > ext_end) return -EINVAL;
            VDBG(bpg, "  extension tag=%" PRId64 " tlen=%" PRId64, tag, tlen);
            idx += (size_t)tlen;
        }
        idx = ext_end;
    }

    v = read_ue7_buf(buf, buf_len, &idx);
    if (v < 0) return -EINVAL;
    uint64_t hevc_hdr_len = (uint64_t)v;
    VDBG(bpg, "hevc_header_length=%" PRIu64, hevc_hdr_len);
    if (idx + (size_t)hevc_hdr_len > buf_len) {
        VERR(bpg, "hevc_header runs past EOF");
        return -EINVAL;
    }
    idx += (size_t)hevc_hdr_len;

    info->hevc_data_off = idx;
    if (picture_data_len == 0) {
        info->hevc_data_len = buf_len - idx;
    } else {
        info->hevc_data_len = (size_t)picture_data_len;
    }
    if (idx + info->hevc_data_len > buf_len) {
        VERR(bpg, "hevc_data runs past EOF");
        return -EINVAL;
    }
    VDBG(bpg, "hevc_data off=%zu len=%zu", info->hevc_data_off, info->hevc_data_len);

    return 0;
}

struct pic *
BPG_load(const char *filename, int skip_flag UNUSED)
{
    struct pic *p = pic_alloc(sizeof(BPG));
    FILE *f = fopen(filename, "rb");
    if (!f) {
        VERR(bpg, "fopen %s failed: %s", filename, strerror(errno));
        pic_free(p);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 10 || fsize > 200 * 1024 * 1024) {
        VERR(bpg, "file size %ld out of range", fsize);
        fclose(f); pic_free(p); return NULL;
    }
    uint8_t *file_buf = malloc((size_t)fsize);
    if (!file_buf) { fclose(f); pic_free(p); return NULL; }
    size_t n = fread(file_buf, 1, (size_t)fsize, f);
    fclose(f);
    if (n != (size_t)fsize) {
        VERR(bpg, "short read: %zu != %ld", n, fsize);
        free(file_buf); pic_free(p); return NULL;
    }

    bpg_header_info info = {0};
    if (BPG_parse_header(file_buf, (size_t)fsize, &info) < 0) {
        free(file_buf); pic_free(p); return NULL;
    }

    if (info.alpha1_flag || info.alpha2_flag) {
        VERR(bpg, "alpha not supported (alpha1=%d alpha2=%d), refusing",
             info.alpha1_flag, info.alpha2_flag);
        free(file_buf); pic_free(p); return NULL;
    }

    /* hevc_data 前补 4 字节起始码 00 00 00 01 — 恢复完整 Annex-B. */
    if (info.hevc_data_len < 2) {
        VERR(bpg, "hevc_data too short");
        free(file_buf); pic_free(p); return NULL;
    }
    size_t annex_b_len = info.hevc_data_len + 4;
    uint8_t *annex_b = malloc(annex_b_len);
    if (!annex_b) { free(file_buf); pic_free(p); return NULL; }
    annex_b[0] = 0x00; annex_b[1] = 0x00; annex_b[2] = 0x00; annex_b[3] = 0x01;
    memcpy(annex_b + 4, file_buf + info.hevc_data_off, info.hevc_data_len);

    /* 按起始码切 NAL payload (起始码之间的内容, 不含起始码本身) */
    int cap = 16, n_nals = 0;
    uint8_t **nals = malloc((size_t)cap * sizeof(*nals));
    int *nal_lens  = malloc((size_t)cap * sizeof(*nal_lens));
    if (!nals || !nal_lens) {
        free(annex_b); free(file_buf); pic_free(p); return NULL;
    }

    size_t i = 0;
    while (i < annex_b_len) {
        size_t sc_len = 0;
        if (i + 4 <= annex_b_len &&
            annex_b[i] == 0 && annex_b[i+1] == 0 &&
            annex_b[i+2] == 0 && annex_b[i+3] == 1)
            sc_len = 4;
        else if (i + 3 <= annex_b_len &&
                 annex_b[i] == 0 && annex_b[i+1] == 0 && annex_b[i+2] == 1)
            sc_len = 3;
        else { i++; continue; }
        size_t payload_start = i + sc_len;
        if (payload_start >= annex_b_len) break;
        size_t j = payload_start;
        /* 找下一个起始码 (3 或 4 字节). 边界: j 能走到文件末尾. */
        while (j < annex_b_len) {
            if (j + 4 <= annex_b_len &&
                annex_b[j] == 0 && annex_b[j+1] == 0 &&
                annex_b[j+2] == 0 && annex_b[j+3] == 1) {
                break;  /* 4-byte start code */
            }
            if (j + 3 <= annex_b_len &&
                annex_b[j] == 0 && annex_b[j+1] == 0 && annex_b[j+2] == 1) {
                break;  /* 3-byte start code */
            }
            j++;
        }
        int nal_len = (int)(j - payload_start);
        if (nal_len >= 2) {
            if (n_nals == cap) {
                int new_cap = cap * 2;
                uint8_t **grown_nals =
                    realloc(nals, (size_t)new_cap * sizeof(*nals));
                if (!grown_nals) break;
                nals = grown_nals;
                int *grown_lens =
                    realloc(nal_lens, (size_t)new_cap * sizeof(*nal_lens));
                if (!grown_lens) break;
                nal_lens = grown_lens;
                cap = new_cap;
            }
            nals[n_nals] = malloc((size_t)nal_len);
            if (!nals[n_nals]) break;
            memcpy(nals[n_nals], annex_b + payload_start, (size_t)nal_len);
            nal_lens[n_nals] = nal_len;
            n_nals++;
        }
        i = j;
    }
    VDBG(bpg, "found %d NALs in hevc_data", n_nals);

    struct hevc_param_set hps = {0};

    /* 先分配 pixels (BPG 头已给出 width/height), 一遍喂所有 NAL
     * (与 test_hevc_tiles_real 同模式: 参数集 NAL 传 pixels=NULL, slice NAL 传 pixels) */
    int out_bits = (info.bit_depth > 8) ? 64 : 32;
    int display_w = info.width, display_h = info.height;
    int pitch = ((display_w + 15) >> 4) * 16 * (out_bits >> 3);
    if (pitch <= 0) pitch = display_w * (out_bits >> 3);
    uint8_t *pixels = malloc((size_t)pitch * (size_t)display_h);
    if (!pixels) {
        for (int k = 0; k < n_nals; k++) free(nals[k]);
        free(nals); free(nal_lens); free(annex_b); free(file_buf);
        free_hevc_param_set(&hps); pic_free(p); return NULL;
    }
    memset(pixels, 0, (size_t)pitch * display_h);

    /* BPG keeps the colour matrix and range in its own header; the HEVC
     * stream may carry no VUI at all.  BPG colour space values follow
     * libbpg: 0 = YCbCr/BT.601, 3 = BT.709, 4 = BT.2020 NCL.  RGB and
     * YCgCo need conversions this decoder does not have. */
    struct hevc_color_override color_override = {
        .full_range = info.limited_range_flag ? 0 : 1,
        .matrix_coeffs = 5,     /* BT.601 / SMPTE 170M */
    };
    int have_color_override = 1;
    switch (info.color_space) {
    case 0: color_override.matrix_coeffs = 5; break;
    case 3: color_override.matrix_coeffs = 1; break;
    case 4: color_override.matrix_coeffs = 9; break;
    default:
        VWARN(bpg, "colour space %d is not supported, using the HEVC VUI",
              info.color_space);
        have_color_override = 0;
        break;
    }

    if (info.animation_flag) {
        VWARN(bpg, "animated BPG: only the first frame is decoded (inter "
              "frames are not supported)");
    }

    int slice_count = 0;
    int seen_first_slice = 0;
    for (int k = 0; k < n_nals; k++) {
        uint8_t *nal = nals[k];
        int nut = (nal[0] & 0x7E) >> 1;
        int is_slice = (nut >= 1 && nut <= 9) || (nut >= 16 && nut <= 21) || nut == 38;
        /* A NAL shorter than its 2-byte header + 1 RBSP byte is not a valid
         * parameter set or slice; feeding it to parse_nalu can run the
         * unbounded VPS layer-set loops off the end of the RBSP. */
        if (nal_lens[k] < 3)
            continue;
        /* In an animation the following pictures carry their own
         * first_slice_segment_in_pic_flag; stop before overwriting the
         * frame we have already composited. */
        if (info.animation_flag && is_slice && seen_first_slice &&
            (nal[2] & 0x80))
            break;
        int ret = parse_nalu_ex(nal, nal_lens[k], is_slice ? pixels : NULL,
                                pitch, display_h, NULL, &hps, out_bits,
                                have_color_override ? &color_override : NULL);
        if (is_slice && ret > 0) {
            slice_count++;
            if (nal[2] & 0x80)
                seen_first_slice = 1;
        }
    }

    if (slice_count == 0) {
        VERR(bpg, "no slice decoded");
        free(pixels);
        for (int k = 0; k < n_nals; k++) free(nals[k]);
        free(nals); free(nal_lens); free(annex_b); free(file_buf);
        free_hevc_param_set(&hps); pic_free(p); return NULL;
    }

    /* 清理 */
    for (int k = 0; k < n_nals; k++) free(nals[k]);
    free(nals); free(nal_lens); free(annex_b); free(file_buf);
    free_hevc_param_set(&hps);

    p->width  = display_w;
    p->height = display_h;
    p->depth  = out_bits;
    p->pitch  = pitch;
    p->pixels = pixels;
    p->format = (out_bits > 32) ? CS_PIXELFORMAT_BGRA64
                             : CS_PIXELFORMAT_BGRA_BYTES;
    p->left = p->top = 0;

    BPG *h = (BPG *)p->pic;
    h->head.file_magic           = 0x425047fb;
    h->head.pixel_format         = (uint8_t)info.pixel_format;
    h->head.alpha1_flag          = (uint8_t)info.alpha1_flag;
    h->head.bit_depth_minus_8    = (uint8_t)(info.bit_depth - 8);
    h->head.color_space          = (uint8_t)info.color_space;
    h->head.extension_present_flag = (uint8_t)info.extension_present_flag;
    h->head.alpha2_flag          = (uint8_t)info.alpha2_flag;
    h->head.limited_range_flag   = (uint8_t)info.limited_range_flag;
    h->head.animation_flag       = (uint8_t)info.animation_flag;
    h->picture_width             = (uint32_t)display_w;
    h->picture_height            = (uint32_t)display_h;
    h->picture_data_length       = (uint32_t)info.hevc_data_len;

    VDBG(bpg, "loaded BPG %dx%d out_bits=%d", p->width, p->height, p->depth);
    return p;
}

static void
BPG_free(struct pic *p)
{
    pic_free(p);
}

static void
BPG_info(FILE *f, struct pic *p)
{
    BPG *h = (BPG *)p->pic;
    fprintf(f, "BPG file format:\n");
    fprintf(f, "pixel_format %d, alpha1 %d, bit_depth %d\n",
            h->head.pixel_format, h->head.alpha1_flag,
            h->head.bit_depth_minus_8 + 8);
    fprintf(f, "color_space %d, extension %d, alpha2 %d, limited_range %d\n",
            h->head.color_space, h->head.extension_present_flag,
            h->head.alpha2_flag, h->head.limited_range_flag);
    fprintf(f, "width %u, height %u, datalen %u\n",
            h->picture_width, h->picture_height, h->picture_data_length);
}

static struct file_ops bpg_ops = {
    .name = "BPG",
    .probe = BPG_probe,
    .load = BPG_load,
    .free = BPG_free,
    .info = BPG_info,
};

void BPG_init(void) {
    file_ops_register(&bpg_ops);
}
