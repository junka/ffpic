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
 * alpha 支持:
 *   alpha1_flag=1: alpha 是同一 Annex-B 流里 nuh_layer_id==1 的辅助单色
 *     HEVC 平面. 我们把 layer_id==1 的 NAL 路由到第二个 hevc_param_set 和
 *     第二个像素缓冲, 解码该平面 (单色), 再把它的 luma 写进彩色缓冲的
 *     alpha 通道. 与 libbpg 一样, 送进 decoder 之前先清掉 layer 位
 *     (libbpg 的 nal_buf[4] &= 0x7), 因为 hevc.c 只处理 base-layer NAL.
 *     alpha1 && alpha2 表示 premultiplied alpha: 合并时按 libbpg 的
 *     alpha_divide8/16 语义把彩色分量除回直通 alpha.
 *   alpha1_flag=0, alpha2_flag=1: 这是独立的 "W 平面" (libbpg has_w_plane),
 *     不是 alpha, 而是给 CMYK 用的第四个分量. ffpic 的 BGRA/BGRA64 没有
 *     多余的平面来承载它, 所以直接返回 NULL 并给出明确的 VERR, 而不是把它
 *     当成 alpha 静默误用.  (extension tag 5 是动画控制, 不是 alpha.)
 *   若 alpha1 但 alpha 平面解码失败, 也返回 NULL: 静默输出不透明会丢 alpha.
 *
 * 注意: 真正 bpgenc 产出的 BPG 把 SPS/VPS 放在 hevc_header (modified SPS)
 * 里、hevc_data 里没有参数集; 本加载器不解 modified SPS, 因此这类文件
 * (无论有无 alpha) 都仍无法解码, 这与已有行为一致.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basemedia.h"
#include "mfile.h"
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

    /* BPG spec hevc_header_and_data():
     *   if (alpha1_flag || alpha2_flag) hevc_header();
     *   hevc_header();
     *   hevc_data();
     * An alpha file therefore carries TWO length-prefixed hevc_header blocks
     * (the alpha one first).  We do not decode the modified SPS in them -- the
     * streams we accept keep their parameter sets inline -- but we must skip
     * both to land on hevc_data. */
    int n_headers = (info->alpha1_flag || info->alpha2_flag) ? 2 : 1;
    for (int h = 0; h < n_headers; h++) {
        v = read_ue7_buf(buf, buf_len, &idx);
        if (v < 0) return -EINVAL;
        uint64_t hevc_hdr_len = (uint64_t)v;
        VDBG(bpg, "hevc_header[%d]_length=%" PRIu64, h, hevc_hdr_len);
        if (idx + (size_t)hevc_hdr_len > buf_len) {
            VERR(bpg, "hevc_header runs past EOF");
            return -EINVAL;
        }
        idx += (size_t)hevc_hdr_len;
    }

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

/* ---- alpha merge -------------------------------------------------------- *
 *
 * The alpha1 plane is a monochrome HEVC picture decoded into its own BGRA
 * buffer: coding/hevc.c's YUV400 path replicates the luma into B/G/R and sets
 * alpha to opaque, exactly like libbpg's gray_to_gray path.  We therefore read
 * the plane's luma back out of channel 0 (B) and store it in the colour
 * buffer's channel 3 (A).
 *
 * libbpg's premultiplied_alpha semantics (alpha1_flag && alpha2_flag): the
 * colour samples are stored multiplied by alpha, so an output that carries
 * straight alpha has to divide them back.  comp_divide8/16 and the reciprocal
 * tables are copied from libbpg (alpha_divide8/alpha_divide16), including the
 * "alpha == 0 -> white" rule, so the numbers match the reference decoder.
 */
static inline int
bpg_comp_divide8(unsigned val, unsigned alpha, unsigned alpha_inv)
{
    if (val >= alpha)
        return 255;
    return (int)((val * alpha_inv + (1u << 15)) >> 16);
}

static inline int
bpg_comp_divide16(unsigned val, unsigned alpha, unsigned alpha_inv)
{
    if (val >= alpha)
        return 65535;
    return (int)((val * alpha_inv + (1u << 14)) >> 15);
}

static void
bpg_merge_alpha(uint8_t *dst, const uint8_t *alpha, int pitch,
                int w, int h, int out_bits, int premultiplied)
{
    if (out_bits > 32) {
        for (int y = 0; y < h; y++) {
            uint16_t *d = (uint16_t *)(dst + (size_t)y * pitch);
            const uint16_t *a = (const uint16_t *)(alpha + (size_t)y * pitch);
            for (int x = 0; x < w; x++) {
                uint16_t av = a[x * 4 + 0];
                d[x * 4 + 3] = av;
                if (premultiplied) {
                    if (av == 0) {
                        d[x * 4 + 0] = d[x * 4 + 1] = d[x * 4 + 2] = 65535;
                    } else {
                        unsigned inv = ((65535u << 15) + av / 2) / av;
                        d[x * 4 + 0] = (uint16_t)bpg_comp_divide16(d[x * 4 + 0], av, inv);
                        d[x * 4 + 1] = (uint16_t)bpg_comp_divide16(d[x * 4 + 1], av, inv);
                        d[x * 4 + 2] = (uint16_t)bpg_comp_divide16(d[x * 4 + 2], av, inv);
                    }
                }
            }
        }
    } else {
        for (int y = 0; y < h; y++) {
            uint8_t *d = dst + (size_t)y * pitch;
            const uint8_t *a = alpha + (size_t)y * pitch;
            for (int x = 0; x < w; x++) {
                uint8_t av = a[x * 4 + 0];
                d[x * 4 + 3] = av;
                if (premultiplied) {
                    if (av == 0) {
                        d[x * 4 + 0] = d[x * 4 + 1] = d[x * 4 + 2] = 255;
                    } else {
                        unsigned inv = ((255u << 16) + av / 2 + 128) / av;
                        d[x * 4 + 0] = (uint8_t)bpg_comp_divide8(d[x * 4 + 0], av, inv);
                        d[x * 4 + 1] = (uint8_t)bpg_comp_divide8(d[x * 4 + 1], av, inv);
                        d[x * 4 + 2] = (uint8_t)bpg_comp_divide8(d[x * 4 + 2], av, inv);
                    }
                }
            }
        }
    }
}

struct pic *
BPG_load(const char *filename, int skip_flag UNUSED)
{
    struct pic *p = pic_alloc(sizeof(BPG));
    struct mfile_buf fb = {0};

    /* the whole container has to stay addressable: the header gives the
     * offsets the HEVC payload is read from, so parse it in place instead of
     * copying the file out of a stream */
    if (mfile_map_file(filename, &fb) != 0) {
        VERR(bpg, "open %s failed: %s", filename, strerror(errno));
        pic_free(p);
        return NULL;
    }
    if (fb.size < 10 || fb.size > 200 * 1024 * 1024) {
        VERR(bpg, "file size %zu out of range", fb.size);
        mfile_unmap(&fb); pic_free(p); return NULL;
    }

    bpg_header_info info = {0};
    if (BPG_parse_header(fb.data, fb.size, &info) < 0) {
        mfile_unmap(&fb); pic_free(p); return NULL;
    }

    /* alpha2 without alpha1 is the "W plane" (libbpg has_w_plane): it is not
     * alpha, it carries a fourth colour/opacity component for CMYK.  This
     * loader has no W-plane output contract (ffpic's packed BGRA/BGRA64 has
     * no spare plane), so refuse it explicitly rather than folding it into
     * the alpha channel and losing its meaning.  See the file header comment. */
    if (!info.alpha1_flag && info.alpha2_flag) {
        VERR(bpg, "BPG W plane (alpha2 without alpha1) is not supported");
        mfile_unmap(&fb); pic_free(p); return NULL;
    }
    int has_alpha = info.alpha1_flag;
    int premultiplied = info.alpha1_flag && info.alpha2_flag;

    /* hevc_data 前补 4 字节起始码 00 00 00 01 — 恢复完整 Annex-B. */
    if (info.hevc_data_len < 2) {
        VERR(bpg, "hevc_data too short");
        mfile_unmap(&fb); pic_free(p); return NULL;
    }
    size_t annex_b_len = info.hevc_data_len + 4;
    uint8_t *annex_b = malloc(annex_b_len);
    if (!annex_b) { mfile_unmap(&fb); pic_free(p); return NULL; }
    annex_b[0] = 0x00; annex_b[1] = 0x00; annex_b[2] = 0x00; annex_b[3] = 0x01;
    memcpy(annex_b + 4, fb.data + info.hevc_data_off, info.hevc_data_len);

    /* 按起始码切 NAL payload (起始码之间的内容, 不含起始码本身) */
    int cap = 16, n_nals = 0;
    uint8_t **nals = malloc((size_t)cap * sizeof(*nals));
    int *nal_lens  = malloc((size_t)cap * sizeof(*nal_lens));
    if (!nals || !nal_lens) {
        free(annex_b); mfile_unmap(&fb); pic_free(p); return NULL;
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

    struct hevc_param_set hps = {0};   /* colour plane (nuh_layer_id 0) */
    struct hevc_param_set ahps = {0};  /* alpha plane  (nuh_layer_id 1) */

    /* 先分配 pixels (BPG 头已给出 width/height), 一遍喂所有 NAL
     * (与 test_hevc_tiles_real 同模式: 参数集 NAL 传 pixels=NULL, slice NAL 传 pixels) */
    int out_bits = (info.bit_depth > 8) ? 64 : 32;
    int display_w = info.width, display_h = info.height;
    int pitch = ((display_w + 15) >> 4) * 16 * (out_bits >> 3);
    if (pitch <= 0) pitch = display_w * (out_bits >> 3);
    uint8_t *pixels = malloc((size_t)pitch * (size_t)display_h);
    /* the auxiliary alpha plane is monochrome but has the same display size,
     * so it gets its own buffer at the same pitch (channel 0 holds its luma) */
    uint8_t *alpha_pixels = NULL;
    if (pixels && has_alpha) {
        alpha_pixels = malloc((size_t)pitch * (size_t)display_h);
    }
    if (!pixels || (has_alpha && !alpha_pixels)) {
        free(pixels); free(alpha_pixels);
        for (int k = 0; k < n_nals; k++) free(nals[k]);
        free(nals); free(nal_lens); free(annex_b); mfile_unmap(&fb);
        free_hevc_param_set(&hps); pic_free(p); return NULL;
    }
    memset(pixels, 0, (size_t)pitch * display_h);
    if (alpha_pixels)
        memset(alpha_pixels, 0, (size_t)pitch * display_h);

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
    int alpha_slice_count = 0;
    int seen_first_slice[2] = {0, 0};
    for (int k = 0; k < n_nals; k++) {
        uint8_t *nal = nals[k];
        int nut = (nal[0] & 0x7E) >> 1;
        int is_slice = (nut >= 1 && nut <= 9) || (nut >= 16 && nut <= 21) || nut == 38;
        /* A NAL shorter than its 2-byte header + 1 RBSP byte is not a valid
         * parameter set or slice; feeding it to parse_nalu can run the
         * unbounded VPS layer-set loops off the end of the RBSP. */
        if (nal_lens[k] < 3)
            continue;
        /* alpha1: the auxiliary plane lives in the same Annex-B stream with
         * nuh_layer_id == 1.  Route those NALs to the second decoder. */
        int lid = ((nal[0] & 1) << 5) | (nal[1] >> 3);
        int plane = (has_alpha && lid == 1) ? 1 : 0;
        /* In an animation the following pictures carry their own
         * first_slice_segment_in_pic_flag; stop before overwriting the
         * frame we have already composited.  Tracked per plane so the alpha
         * and colour pictures do not cut each other short. */
        if (info.animation_flag && is_slice && seen_first_slice[plane] &&
            (nal[2] & 0x80))
            break;
        if (plane == 1) {
            /* Strip the layer id the way libbpg does (nal_buf[4] &= 0x7):
             * hevc.c only decodes base-layer NALs, and a non-zero
             * nuh_layer_id would send the slice header down the multilayer
             * path. */
            nal[0] &= (uint8_t)~0x01;
            nal[1] &= (uint8_t)~0xF8;
        }
        int ret = parse_nalu_ex(nal, nal_lens[k],
                                is_slice ? (plane ? alpha_pixels : pixels) : NULL,
                                pitch, display_h, NULL, plane ? &ahps : &hps,
                                out_bits,
                                (plane == 0 && have_color_override)
                                    ? &color_override : NULL);
        if (is_slice && ret > 0) {
            if (plane)
                alpha_slice_count++;
            else
                slice_count++;
            if (nal[2] & 0x80)
                seen_first_slice[plane] = 1;
        }
    }

    if (slice_count == 0) {
        VERR(bpg, "no slice decoded");
        free(pixels); free(alpha_pixels);
        for (int k = 0; k < n_nals; k++) free(nals[k]);
        free(nals); free(nal_lens); free(annex_b); mfile_unmap(&fb);
        free_hevc_param_set(&hps); free_hevc_param_set(&ahps);
        pic_free(p); return NULL;
    }
    /* alpha1 was signalled, so silently dropping the alpha plane would be a
     * lossy surprise: require that it decoded too. */
    if (has_alpha && alpha_slice_count == 0) {
        VERR(bpg, "alpha1 signalled but the alpha plane did not decode");
        free(pixels); free(alpha_pixels);
        for (int k = 0; k < n_nals; k++) free(nals[k]);
        free(nals); free(nal_lens); free(annex_b); mfile_unmap(&fb);
        free_hevc_param_set(&hps); free_hevc_param_set(&ahps);
        pic_free(p); return NULL;
    }
    if (has_alpha) {
        bpg_merge_alpha(pixels, alpha_pixels, pitch, display_w, display_h,
                        out_bits, premultiplied);
    }

    /* 清理 */
    for (int k = 0; k < n_nals; k++) free(nals[k]);
    free(nals); free(nal_lens); free(annex_b); mfile_unmap(&fb);
    free_hevc_param_set(&hps); free_hevc_param_set(&ahps);
    free(alpha_pixels);

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
    /* the pixels are the malloc'd plane from BPG_load, not a buffer the BPG
     * container owns, so pic_free alone would drop them */
    free(p->pixels);
    p->pixels = NULL;
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
