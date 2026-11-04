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
    if (image->indices == NULL)
        goto out;

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

    /* de-interlace 4-pass when interlace_flag is set */
    if (image->image_dsc.interlace_flag) {
        static const int pass_start[4] = {0, 4, 2, 1};
        static const int pass_step [4] = {8, 8, 4, 2};
        uint8_t *reorder = (uint8_t*)malloc((size_t)image_data_length);
        if (reorder) {
            int src_row = 0;
            for (int pass = 0; pass < 4; pass++) {
                for (int dst_row = pass_start[pass];
                     dst_row < image->image_dsc.height;
                     dst_row += pass_step[pass]) {
                    if (src_row >= image->image_dsc.height) break;
                    memcpy(reorder + (size_t)dst_row * image->image_dsc.width,
                           image->indices + (size_t)src_row * image->image_dsc.width,
                           (size_t)image->image_dsc.width);
                    src_row++;
                }
            }
            memcpy(image->indices, reorder, (size_t)image_data_length);
            free(reorder);
        }
    }

    /* legacy colour-table lookup → data[] — kept so GIF_load_one path works.
     * The animation-aware loader skips this and uses indices directly. */
    {
        Color* ct = image->local_ct ? image->local_ct : gif->global_ct;
        uint32_t resolved_ct_size =
            image->local_ct ? ct_size : color_table_size(gif->ls_dsc.global_color_table_size);
        if (!gif->ls_dsc.global_color_table_flag && image->local_ct == NULL)
            resolved_ct_size = 0;
        if (ct && resolved_ct_size > 0) {
            image->data = (uint8_t*)calloc((size_t)image_data_length, 4);
            if (image->data) {
                for (int j = 0; j < image->image_dsc.height; j++) {
                    for (int i = 0; i < image->image_dsc.width; i++) {
                        uint8_t cindex = image->indices[i + j * image->image_dsc.width];
                        if (cindex >= resolved_ct_size)
                            continue;
                        Color c = ct[cindex];
                        uint8_t *dst = image->data + (size_t)j * 4 * image->image_dsc.width + 4 * i;
                        dst[0] = c.b; dst[1] = c.g; dst[2] = c.r; dst[3] = 0xFF;
                    }
                }
            }
        }
    }

    free(compressed);

    {
        Graphic* graphic = gif_next_dataless_graphic(gif);
        graphic->type = GRAPHIC_IMAGE;
        graphic->image = image;
    }
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

    /* Netscape 2.0 loop-count extension: 3-byte sub-block {sub_id=1, loop_le} */
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

/* ---- canvas helpers for animation composition ---- */

static void
gif_copy_canvas(uint8_t *dst, int dp, const uint8_t *src, int sp, const GIF *g)
{
    int w = g->ls_dsc.screen_witdh, h = g->ls_dsc.screen_height;
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * dp, src + (size_t)y * sp, (size_t)w * 4);
}

static void
gif_fill_background(uint8_t *canvas, int pitch, const GIF *g)
{
    int w = g->ls_dsc.screen_witdh, h = g->ls_dsc.screen_height;
    Color bg;
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

static bool
gif_lookup_color(const Image *img, const GIF *g, uint8_t idx, Color *out)
{
    Color *ct = img->local_ct ? img->local_ct : g->global_ct;
    uint32_t size = img->local_ct
        ? color_table_size(img->image_dsc.local_color_table_size)
        : (g->ls_dsc.global_color_table_flag
           ? color_table_size(g->ls_dsc.global_color_table_size) : 0);
    if (!ct || size == 0 || idx >= size) return false;
    *out = ct[idx]; return true;
}

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
            if (!gif_lookup_color(img, g, idx, &c)) {
                dr[cx*4+0] = dr[cx*4+1] = dr[cx*4+2] = 0; dr[cx*4+3] = 0xFF;
                continue;
            }
            dr[cx*4+0] = c.b; dr[cx*4+1] = c.g;
            dr[cx*4+2] = c.r; dr[cx*4+3] = 0xFF;
        }
    }
}

/* dispose=2: clear prev frame's *own rectangle* to background colour */
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
pic_new(int w, int h)
{
    struct pic *p = pic_alloc(sizeof(GIF *));
    free(p->pic); p->pic = NULL;
    p->width = w; p->height = h; p->depth = 32;
    p->pitch = ((w * 32 + 31) >> 5) << 2;
    p->pixels = calloc((size_t)p->pitch * h, 1);
    return p;
}

/* ---- GIF_load ---- */

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
        VERR(gif, "invalid canvas size"); GIF_free(holder); return NULL;
    }
    int pitch = cw * 4, bytes = pitch * ch;

    uint8_t *canvas     = (uint8_t*)calloc((size_t)bytes, 1);
    uint8_t *snap_a     = (uint8_t*)calloc((size_t)bytes, 1);
    uint8_t *snap_b     = (uint8_t*)calloc((size_t)bytes, 1);
    if (!canvas || !snap_a || !snap_b) {
        free(canvas); free(snap_a); free(snap_b);
        GIF_free(holder); return NULL;
    }

    gif_fill_background(canvas, pitch, g);

    int prev_dispose = 1;   /* previous frame's disposal */
    Graphic *prev_img = NULL;
    bool prev2_valid = false;

    struct pic *last = NULL;
    for (int i = 0; i < g->graphic_count; i++) {
        Graphic *gr = &g->graphics[i];
        if (!graphic_is_image(gr)) continue;

        /* 1. apply prev frame's disposal */
        if (prev_dispose == 3 && prev2_valid) {
            gif_copy_canvas(canvas, pitch, snap_b, pitch, g);  /* snap_b = prev-prev */
        } else if (prev_dispose == 2 && prev_img) {
            gif_dispose_clear_prev(canvas, pitch, prev_img, g);
        }

        /* 2. snapshot canvas *before* composing — rotate snap_a ↔ snap_b
         *    so snap_b always carries the "state before previous frame"
         *    needed for disposal=3 after next frame. */
        gif_copy_canvas(snap_a, pitch, canvas, pitch, g);
        { uint8_t *t = snap_b; snap_b = snap_a; snap_a = t; }
        prev2_valid = true;

        /* 3. compose this frame tile */
        gif_compose_frame(canvas, pitch, gr, g);

        /* 4. snapshot canvas → enqueue pic, set duration from GCE delay */
        struct pic *sn = pic_new(cw, ch);
        gif_copy_canvas((uint8_t*)sn->pixels, sn->pitch, canvas, pitch, g);
        if (gr->has_control && gr->control) {
            pic_set_duration(sn, (uint32_t)gr->control->delay_time * 10u);
        } else {
            pic_set_duration(sn, 100u);   /* default 10ms per frame = 100 */
        }
        if (!file_enqueue_pic(sn)) {
            VERR(gif, "queue full, dropping frame");
            free(sn->pixels); pic_free(sn); break;
        }

        /* 5. remember metadata for next iteration */
        prev_dispose = (gr->has_control && gr->control)
                        ? gr->control->disposal_method : 1;
        prev_img = gr; last = sn;
    }

    free(canvas); free(snap_a); free(snap_b);

    if (last == NULL) {
        VERR(gif, "no image in the file"); GIF_free(holder); return NULL;
    }
    /* last frame owns the GIF container */
    last->pic = g; holder->pic = NULL;
    pic_free(holder); return NULL;
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
