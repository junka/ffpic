#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "colorspace.h"
#include "gif.h"
#include "file.h"
#include "lzw.h"
#include "utils.h"
#include "vlog.h"

VLOG_REGISTER(gif, INFO)

void GIF_free(struct pic *p);

#define GRAPHIC_PLAINTEXT      0x01
#define GRAPHIC_IMAGE          0x2C
#define GRAPHIC_CONTROL        0xF9

#define EXTENSION_COMMENT      0xFE
#define EXTENSION_APPLICATION  0xFF

#define EXTENSION_INTRODUCER   0x21
#define TRAILER                0x3B

/* the gif spec puts the lzw minimum code size in 2..8 */
#define GIF_MIN_CODE_SIZE      2
#define GIF_MAX_CODE_SIZE      8

/* upper bound for a decoded image, keep it in sync with the callers of
 * image_data_length_of()
 */
#define GIF_MAX_IMAGE_BYTES    (512 * 1024 * 1024)

/* the gif spec stores the color table size as a 3 bit field, the table holds
 * 2 << n entries, n == 7 is the only valid value for 256 entries
 */
#define GIF_MAX_COLOR_TABLE    256

static uint32_t color_table_size(uint8_t table_size)
{
    if (table_size >= 7)
        return GIF_MAX_COLOR_TABLE;
    return 2u << table_size;
}

/* the number of bytes the lzw decoder has to produce for an image, the
 * decoder writes one byte per pixel hooking up to image_data_length(). zero
 * means the descriptor is not usable
 */
static uint64_t image_data_length_of(const struct image_descriptor* dsc)
{
    if (dsc->width == 0 || dsc->height == 0)
        return 0;

    uint64_t length = (uint64_t)dsc->width * dsc->height;
    if (length == 0 || length * 4 > GIF_MAX_IMAGE_BYTES)
        return 0;

    return length;
}

int read_blocks(uint8_t** data, FILE* file) 
{
    *data = NULL;
    int total_length = 0;
    for (;;) {
        int c = fgetc(file);
        if (c == EOF)
            break;
        uint8_t block_length = (uint8_t)c;
        if (block_length == 0)
            break;
        /* fgetc told us a sub block is coming but the file may end inside */
        if (total_length > INT_MAX - block_length - 1)
            break;
        uint8_t* block = (uint8_t*)realloc(*data, (size_t)total_length + block_length + 1);
        if (block == NULL) {
            free(*data);
            *data = NULL;
            return 0;
        }
        *data = block;
        size_t got = fread(*data + total_length, 1, block_length, file);
        total_length += (int)got;
        /* a comment or plaintext block is printed with %s by the info
         * callbacks, the block itself has no terminator
         */
        (*data)[total_length] = '\0';
        if (got < block_length)
            break;
    }
    return total_length;
}


Graphic* gif_increment_graphics(GIF* gif) {
    if (gif->graphic_count++ == 0) {
        gif->graphics = (Graphic*)malloc(sizeof(Graphic));
    } else {
        gif->graphics = (Graphic*)realloc(gif->graphics, gif->graphic_count * sizeof(Graphic));
    }
    Graphic* newest = gif->graphics + gif->graphic_count - 1;
    newest->has_control = false;
    newest->control = NULL;
    newest->type = 0;
    newest->image = NULL;
    newest->plaintext = NULL;
    return newest;
}

Graphic* gif_next_dataless_graphic(GIF* gif) {
    if (gif->graphic_count != 0) {
        Graphic* graphic = gif->graphics + gif->graphic_count - 1;
        if (graphic->type == 0) {
            return graphic;
        }
    }
    return gif_increment_graphics(gif);
}

/* 4-pass interlace reorder per GIF89a Image Descriptor interlace_flag:
 *   pass 0: start 0, step 8
 *   pass 1: start 4, step 8
 *   pass 2: start 2, step 4
 *   pass 3: start 1, step 2
 * LZW writes contiguous scanlines in pass order; we reorder to normal
 * top-to-bottom row order in-place. */
static void gif_deinterlace(uint8_t *data, int width, int height)
{
    static const int ps[4] = {0, 4, 2, 1};
    static const int pd[4] = {8, 8, 4, 2};
    uint8_t *tmp = (uint8_t*)malloc((size_t)width * height);
    if (!tmp) return;
    int src = 0;
    for (int pass = 0; pass < 4; pass++) {
        for (int dst_row = ps[pass]; dst_row < height; dst_row += pd[pass]) {
            if (src >= height) break;
            memcpy(tmp + (size_t)dst_row * width, data + (size_t)src * width, width);
            src++;
        }
    }
    memcpy(data, tmp, (size_t)width * height);
    free(tmp);
}

void read_image(GIF* gif, FILE* f) {
    Image* image = (Image*)calloc(1, sizeof(Image));
    if (image == NULL)
        return;
    if (fread(&image->image_dsc, sizeof(struct image_descriptor), 1, f) != 1)
        image->image_dsc.width = image->image_dsc.height = 0;

    uint32_t ct_size = 0;
    if (image->image_dsc.local_color_table_flag) {
        ct_size = color_table_size(image->image_dsc.local_color_table_size);
        size_t entries = ct_size;
        image->local_ct = (Color*)calloc(entries, sizeof(Color));
        if (image->local_ct == NULL)
            ct_size = 0;
        else
            FFREAD_IGNORE(image->local_ct, sizeof(Color), entries, f);
    } else {
        image->local_ct = NULL;
    }

    uint64_t image_data_length = image_data_length_of(&image->image_dsc);
    uint8_t* compressed = NULL;
    int compressed_length = 0;
    image->indices = NULL;
    image->data = NULL;

    if (image_data_length == 0) {
        VERR(gif, "invalid image size %dx%d",
             image->image_dsc.width, image->image_dsc.height);
        goto out;
    }

    image->indices = (uint8_t*)calloc((size_t)image_data_length, 1);
    image->data = (uint8_t*)calloc((size_t)image_data_length, 4);
    if (!image->indices || !image->data)
        goto out;

    /* LZW → indices buffer (1 byte per pixel) */
    {
        int lzw_code_size = fgetc(f);
        if (lzw_code_size < GIF_MIN_CODE_SIZE || lzw_code_size > GIF_MAX_CODE_SIZE) {
            VERR(gif, "invalid lzw minimum code size [%d]", lzw_code_size);
            goto out;
        }
        compressed_length = read_blocks(&compressed, f);
        if (compressed == NULL || compressed_length <= 0)
            goto out;

        if (lzw_decode_gif(1, lzw_code_size, compressed, compressed_length,
                           image->indices, (int)image_data_length) != (int)image_data_length) {
            VERR(gif, "lzw stream does not match the image size");
            goto out;
        }
    }

    /* de-interlace if the flag is set */
    if (image->image_dsc.interlace_flag)
        gif_deinterlace(image->indices, image->image_dsc.width, image->image_dsc.height);

    /* resolve color table; no table = can't render → bail */
    Color* ct = image->local_ct ? image->local_ct : gif->global_ct;
    uint32_t resolved_ct_size =
        image->local_ct ? ct_size : color_table_size(gif->ls_dsc.global_color_table_size);
    if (!gif->ls_dsc.global_color_table_flag && image->local_ct == NULL)
        resolved_ct_size = 0;
    if (ct == NULL || resolved_ct_size == 0) {
        VERR(gif, "no color table available for the image");
        goto out;
    }

    /* now we have indices + ct; find the previous Graphic (attached GCE) to
     * honour transparent_color_flag / transparent_color_index */
    Graphic* graphic = gif_next_dataless_graphic(gif);
    graphic->type = GRAPHIC_IMAGE;
    graphic->image = image;

    bool have_transp = false;
    uint8_t transp_idx = 0;
    if (graphic->has_control && graphic->control && graphic->control->transparent_color_flag) {
        have_transp = true;
        transp_idx = graphic->control->transparent_color_index;
    }

    /* look up colours from indices → BGRA; skip transparent pixels */
    {
        uint8_t* p = image->indices;
        uint8_t* q = image->data;
        int w = image->image_dsc.width, h = image->image_dsc.height;
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                uint8_t cindex = p[j * w + i];
                if (have_transp && cindex == transp_idx) {
                    *q++ = 0; *q++ = 0; *q++ = 0; *q++ = 0;
                    continue;
                }
                if (cindex >= resolved_ct_size) {
                    *q++ = 0; *q++ = 0; *q++ = 0; *q++ = 0xFF;
                    continue;
                }
                Color c = ct[cindex];
                *q++ = c.b; *q++ = c.g; *q++ = c.r; *q++ = 0xFF;
            }
        }
    }

    free(compressed);
    return;

out:
    free(compressed);
    image_free(image);
}

void 
read_plaintext_ext(GIF* gif, FILE* f) 
{
    // no one ever uses this, but hey. if you want it, it's here.
    PlainText* plaintext = (PlainText*)malloc(sizeof(PlainText));
    FFREAD_IGNORE(&plaintext->text_ext, sizeof(struct plaintext_extension), 1, f);
    plaintext->text_length = read_blocks((uint8_t**)(&plaintext->text), f);
    fgetc(f);

    Graphic* graphic = gif_next_dataless_graphic(gif);
    graphic->type = GRAPHIC_PLAINTEXT;
    graphic->plaintext = plaintext;
}

void 
read_graphic_control_ext(GIF* gif, FILE* file) 
{
    struct graphic_control_extension *ctrl = malloc(sizeof(struct graphic_control_extension));
    FFREAD_IGNORE(ctrl, sizeof(struct graphic_control_extension), 1, file);

    Graphic* graphic = gif_increment_graphics(gif);
    graphic->has_control = true;
    graphic->control = ctrl;
    graphic->type = 0;
}

Extension* gif_increment_extensions(GIF* gif) {
    if (gif->extension_count++ == 0) {
        gif->extensions = (Extension*)malloc(sizeof(Extension));
    } else {
        gif->extensions = (Extension*)realloc(gif->extensions, gif->extension_count * sizeof(Extension));
    }
    return gif->extensions + gif->extension_count - 1;
}

void read_comment_ext(GIF* gif, FILE* file) {
    Comment* comment = (Comment*)malloc(sizeof(Comment));
    comment->text_length = read_blocks((uint8_t**)&comment->text, file);

    Extension* extension = gif_increment_extensions(gif);
    extension->type = EXTENSION_COMMENT;
    extension->comment = comment;
}

void read_application_ext(GIF* gif, FILE* f) {
    Application* application = (Application*)malloc(sizeof(Application));
    
    FFREAD_IGNORE(&application->app_ext, sizeof(struct application_extension), 1, f);
    application->data_length = read_blocks(&application->data, f);

    /* Netscape 2.0 application extension: 3-byte sub-block
     *   [sub_id = 1][loop_count uint16 LE]
     * loop_count == 0 means infinite. */
    if (application->data_length == 3 && application->data) {
        if (!memcmp(application->app_ext.identifier, "NETSCAPE2.0", 11) &&
            application->data[0] == 1) {
            gif->loop_count = (int)application->data[1] |
                              ((int)application->data[2] << 8);
        }
    }

    Extension* extension = gif_increment_extensions(gif);
    extension->type = EXTENSION_APPLICATION;
    extension->application = application;
}

void read_extension(GIF* gif, FILE* file) {
    uint8_t extension_code = (uint8_t)fgetc(file);
    switch (extension_code) {
    case GRAPHIC_PLAINTEXT:
        read_plaintext_ext(gif, file);
        break;

    case EXTENSION_APPLICATION:
        read_application_ext(gif, file);
        break;

    case EXTENSION_COMMENT:
        read_comment_ext(gif, file);
        break;

    case GRAPHIC_CONTROL:
        read_graphic_control_ext(gif, file);
        break;
    }
}

void read_contents(GIF* gif, FILE* file) {
    while (!feof(file)) {
        uint8_t block_code = (uint8_t)fgetc(file);
        switch (block_code) {
        case GRAPHIC_IMAGE:
            read_image(gif, file);
            break;

        case EXTENSION_INTRODUCER:
            read_extension(gif, file);
            break;

        case 0:
            break;

        case TRAILER:
            return;

        default:
            VERR(gif, "invalid block code [%x]", block_code);
            return;
        }
    }
}

void read_gif(FILE* f, GIF *gif) {

    gif->graphic_count = 0;
    gif->extension_count = 0;
    gif->graphics = NULL;
    gif->extensions = NULL;
    gif->loop_count = -1;
    FFREAD_IGNORE(&gif->file_h, 1, GIF_FILE_HEADER_LEN, f);
    if (fread(&gif->ls_dsc, 1, sizeof(struct logical_screen_descriptor), f) !=
        sizeof(struct logical_screen_descriptor)) {
        gif->ls_dsc.global_color_table_flag = 0;
        gif->ls_dsc.global_color_table_size = 0;
    }

    if (gif->ls_dsc.global_color_table_flag) {
        gif->global_ct = (Color*)calloc(GIF_MAX_COLOR_TABLE, sizeof(Color));
        if (gif->global_ct)
            FFREAD_IGNORE(gif->global_ct, sizeof(Color),
                  color_table_size(gif->ls_dsc.global_color_table_size), f);
        else
            gif->ls_dsc.global_color_table_flag = 0;
    } else {
        gif->global_ct = NULL;
    }
    read_contents(gif, f);

}

static bool
graphic_is_image(const Graphic* g)
{
    return g->type == GRAPHIC_IMAGE && g->image && g->image->indices;
}

/* ---- canvas helpers (used by the animation-aware GIF_load) ---- */

static void
gif_copy_canvas(uint8_t *dst, int dst_pitch,
                const uint8_t *src, int src_pitch,
                const GIF *g)
{
    int w = g->ls_dsc.screen_witdh;
    int h = g->ls_dsc.screen_height;
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * dst_pitch,
               src + (size_t)y * src_pitch,
               (size_t)w * 4);
}

static void
gif_fill_background(uint8_t *canvas, int pitch, const GIF *g)
{
    int w = g->ls_dsc.screen_witdh, h = g->ls_dsc.screen_height;
    Color bg = {0, 0, 0};
    bg.r = 0xFF; bg.g = bg.b = 0;
    if (g->ls_dsc.global_color_table_flag && g->global_ct) {
        int idx = g->ls_dsc.background_color_index;
        if (idx < GIF_MAX_COLOR_TABLE) bg = g->global_ct[idx];
    }
    for (int y = 0; y < h; y++) {
        uint8_t *row = canvas + (size_t)y * pitch;
        for (int x = 0; x < w; x++) {
            row[x*4+0] = bg.b; row[x*4+1] = bg.g;
            row[x*4+2] = bg.r; row[x*4+3] = 0xFF;
        }
    }
}

/* Compose one decoded image tile onto the canvas. Pixels whose index equals
 * transparent_color_index (when enabled by the GCE) are skipped so the
 * canvas keeps whatever was underneath. */
static void
gif_compose_frame(uint8_t *canvas, int pitch,
                  const Graphic *gr, const GIF *g)
{
    const Image *img = gr->image;
    int L = img->image_dsc.left, T = img->image_dsc.top;
    int W = img->image_dsc.width, H = img->image_dsc.height;
    int cw = g->ls_dsc.screen_witdh, ch = g->ls_dsc.screen_height;

    bool have_transp = false;
    uint8_t transp_idx = 0;
    if (gr->has_control && gr->control && gr->control->transparent_color_flag) {
        have_transp = true;
        transp_idx = gr->control->transparent_color_index;
    }

    Color *ct = img->local_ct ? img->local_ct : g->global_ct;
    uint32_t ct_size = img->local_ct
        ? color_table_size(img->image_dsc.local_color_table_size)
        : (g->ls_dsc.global_color_table_flag
           ? color_table_size(g->ls_dsc.global_color_table_size) : 0);

    for (int y = 0; y < H; y++) {
        int cy = T + y;
        if (cy < 0 || cy >= ch) continue;
        const uint8_t *sr = img->indices + (size_t)y * W;
        uint8_t *dr = canvas + (size_t)cy * pitch;
        for (int x = 0; x < W; x++) {
            int cx = L + x;
            if (cx < 0 || cx >= cw) continue;
            uint8_t idx = sr[x];
            if (have_transp && idx == transp_idx) continue;
            Color c;
            if (!ct || ct_size == 0 || idx >= ct_size) {
                dr[cx*4+0] = dr[cx*4+1] = dr[cx*4+2] = 0;
                dr[cx*4+3] = 0xFF;
                continue;
            }
            c = ct[idx];
            dr[cx*4+0] = c.b; dr[cx*4+1] = c.g;
            dr[cx*4+2] = c.r; dr[cx*4+3] = 0xFF;
        }
    }
}

/* dispose=2: clear the *previous* frame's own rectangle to the background
 * colour (GIF89a spec — NOT the whole canvas). */
static void
gif_dispose_clear_prev(uint8_t *canvas, int pitch,
                       const Graphic *prev, const GIF *g)
{
    if (!prev || !prev->image) return;
    const Image *img = prev->image;
    int L = img->image_dsc.left, T = img->image_dsc.top;
    int W = img->image_dsc.width, H = img->image_dsc.height;
    int cw = g->ls_dsc.screen_witdh, ch = g->ls_dsc.screen_height;
    Color bg = {0, 0, 0};
    bg.r = 0xFF;
    if (g->ls_dsc.global_color_table_flag && g->global_ct) {
        int idx = g->ls_dsc.background_color_index;
        if (idx < GIF_MAX_COLOR_TABLE) bg = g->global_ct[idx];
    }
    for (int y = 0; y < H; y++) {
        int cy = T + y;
        if (cy < 0 || cy >= ch) continue;
        uint8_t *row = canvas + (size_t)cy * pitch;
        for (int x = 0; x < W; x++) {
            int cx = L + x;
            if (cx < 0 || cx >= cw) continue;
            row[cx*4+0] = bg.b; row[cx*4+1] = bg.g;
            row[cx*4+2] = bg.r; row[cx*4+3] = 0xFF;
        }
    }
}

static struct pic *
pic_new_fullcanvas(int w, int h)
{
    struct pic *p = pic_alloc(sizeof(void*));
    free(p->pic); p->pic = NULL;
    p->width = w; p->height = h; p->depth = 32;
    p->pitch = ((w * 32 + 31) >> 5) << 2;   /* 32-byte aligned */
    p->pixels = calloc((size_t)p->pitch * h, 1);
    p->format = CS_PIXELFORMAT_BGRA8888;
    p->left = 0; p->top = 0;
    return p;
}

static struct pic *
GIF_load(const char *filename, int skip_flag UNUSED)
{
    struct pic *holder = pic_alloc(sizeof(GIF));
    GIF* g = (GIF *)holder->pic;
    FILE* f = fopen(filename, "rb");
    if (f == NULL) { pic_free(holder); return NULL; }
    read_gif(f, g);
    fclose(f);

    int cw = g->ls_dsc.screen_witdh, ch = g->ls_dsc.screen_height;
    if (cw <= 0 || ch <= 0) {
        VERR(gif, "invalid canvas size");
        GIF_free(holder);
        return NULL;
    }

    int pitch = cw * 4, bytes = pitch * ch;

    /* Single-frame shortcut: graphic_count == 1 AND it's an image is returned
     * directly as file_load's return value (legacy contract). Multi-frame
     * animations (graphic_count > 1) always go to the queue — this also
     * covers files where a plaintext / app block precedes the single image. */
    if (g->graphic_count == 1 && graphic_is_image(&g->graphics[0])) {
        Graphic *gr = &g->graphics[0];

        uint8_t *canvas = (uint8_t*)calloc((size_t)bytes, 1);
        if (!canvas) {
            GIF_free(holder);
            return NULL;
        }
        gif_fill_background(canvas, pitch, g);
        gif_compose_frame(canvas, pitch, gr, g);

        struct pic *p = pic_new_fullcanvas(cw, ch);
        gif_copy_canvas((uint8_t*)p->pixels, p->pitch, canvas, pitch, g);
        if (gr->has_control && gr->control)
            pic_set_duration(p, (uint32_t)gr->control->delay_time * 10u);
        else
            pic_set_duration(p, 100u);

        free(canvas);
        /* single frame owns the GIF container — file_free → GIF_free releases it */
        p->pic = holder->pic;
        holder->pic = NULL;
        pic_free(holder);
        return p;
    }

    uint8_t *canvas = (uint8_t*)calloc((size_t)bytes, 1);
    uint8_t *prev_before = (uint8_t*)calloc((size_t)bytes, 1);
    if (!canvas || !prev_before) {
        free(canvas); free(prev_before);
        GIF_free(holder);
        return NULL;
    }

    /* prev_before 存"canvas 在 [dispose 应用后、compose 本帧前] 的状态"。
     * 下一帧若 prev_dispose==3 就恢复到这里。只需要一张：
     * 单帧路径（dispose=0/1/2） prev_before 始终被覆盖但未被使用；
     * dispose=3 只看这一张，因为它要恢复的是上一帧 compose 之前。 */
    gif_fill_background(canvas, pitch, g);
    gif_copy_canvas(prev_before, pitch, canvas, pitch, g);

    int prev_dispose = 1;   /* previous frame's disposal, default: do nothing */
    Graphic *prev_img = NULL;
    struct pic *last = NULL;

    for (int i = 0; i < g->graphic_count; i++) {
        Graphic *gr = &g->graphics[i];
        if (!graphic_is_image(gr)) continue;

        /* 1. apply previous frame's disposal
         *    dispose=3 恢复到"prev_before"（即上一帧 compose 前的状态） */
        if (prev_dispose == 3)
            gif_copy_canvas(canvas, pitch, prev_before, pitch, g);
        else if (prev_dispose == 2 && prev_img)
            gif_dispose_clear_prev(canvas, pitch, prev_img, g);
        /* dispose 0/1 = leave alone (default) */

        /* 2. snapshot canvas *after* dispose applied but *before* compose
         *    — this is what next frame's dispose=3 must restore to */
        gif_copy_canvas(prev_before, pitch, canvas, pitch, g);

        /* 3. compose this frame tile onto the persistent canvas */
        gif_compose_frame(canvas, pitch, gr, g);

        /* 4. snapshot the full canvas into a newly-malloc'd pic; its pitch
         *    may be wider than the canvas, so copy line-by-line */
        struct pic *sn = pic_new_fullcanvas(cw, ch);
        gif_copy_canvas((uint8_t*)sn->pixels, sn->pitch, canvas, pitch, g);
        if (gr->has_control && gr->control)
            pic_set_duration(sn, (uint32_t)gr->control->delay_time * 10u);
        else
            pic_set_duration(sn, 100u);

        /* 5. enqueue; ring_queue has a bounded buffer so file_enqueue_pic
         *    returns false when full — drop and bail out cleanly */
        if (!file_enqueue_pic(sn)) {
            /* fprintf instead of VERR: VLOG is #ifndef NDEBUG guarded,
             * so Release builds would silently bail out with zero
             * observability when the bounded queue fills up. */
            fprintf(stderr, "GIF(gif_load): ring queue full (cap=64), dropping frame at line %d\n",
                    __LINE__);
            free(sn->pixels); pic_free(sn); break;
        }

        /* 6. remember this frame's metadata for the next iteration */
        prev_dispose = (gr->has_control && gr->control)
                        ? gr->control->disposal_method : 1;
        prev_img = gr;
        last = sn;
    }

    free(canvas); free(prev_before);

    if (last == NULL) {
        /* no valid frame produced — free GIF container ourselves */
        GIF_free(holder);
        return NULL;
    }

    /* Contract: the *last* queued frame owns the GIF container so that
     * callers (picinfo.c, GIF_info) can reach the original GIF struct.
     * All other queued frames carry p->pic == NULL and only own pixels. */
    last->pic = holder->pic;
    holder->pic = NULL;
    pic_free(holder);
    return NULL;
}

static int 
GIF_probe(FILE *f)
{
    struct gif_file_header head;
    if (f == NULL) {
        return -ENOENT;
    }
    int len = fread(&head, 1, GIF_FILE_HEADER_LEN, f);
    if (len < 6) {
        return -EBADF;
    }
    if (!memcmp(&head, "GIF89a", GIF_FILE_HEADER_LEN) || 
        !memcmp(&head, "GIF87a", GIF_FILE_HEADER_LEN)) {
        return 0;
    }
    return -EINVAL;
}

static Graphic*
gif_graphic_at(GIF* g, int index)
{
    if (index < 0 || index >= g->graphic_count)
        return NULL;
    return &g->graphics[index];
}

static void 
GIF_info(FILE* f, struct pic* p)
{
    GIF *g = (GIF*)(p->pic);
    Graphic* first = gif_graphic_at(g, 0);
    fprintf(f, "GIF file format:\n");
    fprintf(f, "version %c%c%c\n", g->file_h.version[0], g->file_h.version[1], g->file_h.version[2]);
    fprintf(f, "\timage width %d height %d:\n", g->ls_dsc.screen_witdh, g->ls_dsc.screen_height);
    fprintf(f, "\tbackground_color_index: %d\n", g->ls_dsc.background_color_index);
    fprintf(f, "\taspect_ratio: %.2f\n", g->ls_dsc.pixel_aspect_ratio?((g->ls_dsc.pixel_aspect_ratio + 15)/64.0) : 0);
    if (g->ls_dsc.global_color_table_flag) {
        fprintf(f, "\tglobal_ct_resolution: %d\n", g->ls_dsc.global_color_resolution+1);
        fprintf(f, "\tglobal_ct_size: %d\n", 2 << g->ls_dsc.global_color_table_size);
        fprintf(f, "\tglobal_ct_sorted: %d\n", g->ls_dsc.global_color_sort_flag);
    }
    fprintf(f, "------------------------------\n");
    fprintf(f, "\tgraphic_count: %d\n", g->graphic_count);
    fprintf(f, "\textension_count: %d\n", g->extension_count);
    if (first == NULL) {
        fprintf(f, "\tno graphic block\n");
        return;
    }
    if (first->has_control) {
        fprintf(f, "\tfirst image: disposal_method %d\n", first->control->disposal_method);
        fprintf(f, "\tfirst image: delay_time %d\n", first->control->delay_time);
    }
    if (first->image == NULL) {
        fprintf(f, "\tfirst graphic: image type %d\n", first->type);
        return;
    }
    fprintf(f, "\tfirst image: width %d height %d\n", first->image->image_dsc.width,
                first->image->image_dsc.height);
    fprintf(f, "\tfirst image: left %d top %d\n", first->image->image_dsc.left,
                first->image->image_dsc.top);
    fprintf(f, "\tfirst image: interlace %d\n", first->image->image_dsc.interlace_flag);
    fprintf(f, "\tfirst image: local_color_flag %d\n", first->image->image_dsc.local_color_table_flag);

    if (g->graphic_count > 1) {
        Graphic* second = &g->graphics[1];
        if (second->has_control) {
            fprintf(f, "\tsecond image: disposal_method %d\n", second->control->disposal_method);
            fprintf(f, "\tsecond image: delay_time %d\n", second->control->delay_time);
        }
        fprintf(f, "\tsecond image: image type %d\n", second->type);
        if (second->image) {
            fprintf(f, "\tsecond image: width %d height %d\n", second->image->image_dsc.width,
                        second->image->image_dsc.height);
            fprintf(f, "\tsecond image: left %d top %d\n", second->image->image_dsc.left,
                        second->image->image_dsc.top);
            fprintf(f, "\tsecond image: interlace %d\n", second->image->image_dsc.interlace_flag);
            fprintf(f, "\tsecond image: local_color_flag %d\n", second->image->image_dsc.local_color_table_flag);
        }
        fprintf(f, "------------------------------\n");
        if (g->extension_count) {
            for(int i = 0; i < g->extension_count; i ++) {
                switch(g->extensions[i].type) {
                    case EXTENSION_COMMENT:
                        fprintf(f, "\tcomment extension:\n");
                        fprintf(f, "\t%s\n", g->extensions[i].comment->text ?
                                g->extensions[i].comment->text : "");
                        break;
                    case EXTENSION_APPLICATION:
                        fprintf(f, "\tapp extension:\n");
                        fprintf(f, "\tid %c%c%c%c%c%c%c%c\n", g->extensions[i].application->app_ext.identifier[0],
                        g->extensions[i].application->app_ext.identifier[1],g->extensions[i].application->app_ext.identifier[2],
                        g->extensions[i].application->app_ext.identifier[3],g->extensions[i].application->app_ext.identifier[4],
                        g->extensions[i].application->app_ext.identifier[5],g->extensions[i].application->app_ext.identifier[6],
                        g->extensions[i].application->app_ext.identifier[7]);
                        break;
                    default:
                        fprintf(f, "\tunknown extension\n");
                        break;
                }
            }
        }
    }
}

void image_free(Image* image) {
    if (image->image_dsc.local_color_table_flag)
        free(image->local_ct);

    free(image->indices);
    free(image->data);
    free(image);
}

void plaintext_free(PlainText* plaintext) {
    free(plaintext->text);
    free(plaintext);
}

void graphic_free(Graphic* graphic) {
    if (graphic->has_control)
        free(graphic->control);

    switch (graphic->type) {
    case GRAPHIC_IMAGE:
        image_free(graphic->image);
        break;
    case GRAPHIC_PLAINTEXT:
        plaintext_free(graphic->plaintext);
        break;
    }
}

void comment_free(Comment* comment) {
    free(comment->text);
    free(comment);
}

void application_free(Application* application) {
    free(application->data);
    free(application);
}

void extension_free(Extension* extension) {
    switch (extension->type) {
    case EXTENSION_COMMENT:
        comment_free(extension->comment);
        break;
    case EXTENSION_APPLICATION:
        application_free(extension->application);
        break;
    }
}

void GIF_free(struct pic *p) {
    GIF* gif = (GIF*)(p->pic);
    if (gif) {
        if (gif->graphic_count) {
            while (gif->graphic_count)
                graphic_free(gif->graphics + --gif->graphic_count);
            free(gif->graphics);
        }

        if (gif->extension_count) {
            while (gif->extension_count)
                extension_free(gif->extensions + --gif->extension_count);
            free(gif->extensions);
        }

        if (gif->ls_dsc.global_color_table_flag)
            free(gif->global_ct);
    }
    /* pic_new_fullcanvas owns its pixel buffer directly */
    free(p->pixels);
    p->pixels = NULL;
    pic_free(p);
}
static struct file_ops gif_ops = {
    .name = "GIF",
    .probe = GIF_probe,
    .load = GIF_load,
    .free = GIF_free,
    .info = GIF_info,
};

void GIF_init(void)
{
    file_ops_register(&gif_ops);
}
