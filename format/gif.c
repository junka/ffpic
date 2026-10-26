#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "colorspace.h"
#include "gif.h"
#include "file.h"
#include "lzw.h"
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
            fread(image->local_ct, sizeof(Color), entries, f);
    } else {
        image->local_ct = NULL;
    }

    uint64_t image_data_length = image_data_length_of(&image->image_dsc);
    uint8_t* compressed = NULL;
    int compressed_length = 0;
    image->data = NULL;

    if (image_data_length == 0) {
        VERR(gif, "invalid image size %dx%d",
             image->image_dsc.width, image->image_dsc.height);
        goto out;
    }

    image->data = (uint8_t*)calloc((size_t)image_data_length, 4);
    if (image->data == NULL)
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

        /* lzw_decode
         * loadlzw
         * the decoder writes one byte per pixel, so a valid stream can never
         * need more room than the frame holds
         */
        if (lzw_decode_gif(1, lzw_code_size, compressed, compressed_length,
                           image->data, (int)image_data_length) != (int)image_data_length) {
            VERR(gif, "lzw stream does not match the image size");
            goto out;
        }
    }

    {
        /* resolve the color table the way the decoder output has to be
         * mapped, a frame without any color table can not be rendered
         */
        Color* ct = image->local_ct ? image->local_ct : gif->global_ct;
        uint32_t resolved_ct_size =
            image->local_ct ? ct_size : color_table_size(gif->ls_dsc.global_color_table_size);
        if (!gif->ls_dsc.global_color_table_flag && image->local_ct == NULL)
            resolved_ct_size = 0;
        if (ct == NULL || resolved_ct_size == 0) {
            VERR(gif, "no color table available for the image");
            goto out;
        }

        uint8_t* p = image->data;
        for (int j = image->image_dsc.height - 1; j >= 0; j --) {
            for (int i = image->image_dsc.width - 1; i >= 0; i --) {
                uint8_t cindex = *(p + i + j * image->image_dsc.width);
                /* a byte written by the decoder is not guaranteed to index
                 * the color table, the lzw code size says nothing about how
                 * many entries the table has
                 */
                if (cindex >= resolved_ct_size) {
                    VERR(gif, "color index [%d] out of the %d entry color table",
                         cindex, resolved_ct_size);
                    goto out;
                }
                *(p +  (j * 4 * image->image_dsc.width) + 4*i) = ct[cindex].b;
                *(p +  (j * 4 * image->image_dsc.width) + 4*i + 1) = ct[cindex].g;
                *(p +  (j * 4 * image->image_dsc.width) + 4*i + 2 ) = ct[cindex].r;
                *(p +  (j * 4 * image->image_dsc.width) + 4*i + 3 ) = 0xFF;
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
    fread(&plaintext->text_ext, sizeof(struct plaintext_extension), 1, f);
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
    fread(ctrl, sizeof(struct graphic_control_extension), 1, file);

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
    
    fread(&application->app_ext, sizeof(struct application_extension), 1, f);
    application->data_length = read_blocks(&application->data, f);

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
    fread(&gif->file_h, 1, GIF_FILE_HEADER_LEN, f);
    if (fread(&gif->ls_dsc, 1, sizeof(struct logical_screen_descriptor), f) !=
        sizeof(struct logical_screen_descriptor)) {
        gif->ls_dsc.global_color_table_flag = 0;
        gif->ls_dsc.global_color_table_size = 0;
    }

    if (gif->ls_dsc.global_color_table_flag) {
        gif->global_ct = (Color*)calloc(GIF_MAX_COLOR_TABLE, sizeof(Color));
        if (gif->global_ct)
            fread(gif->global_ct, sizeof(Color),
                  color_table_size(gif->ls_dsc.global_color_table_size), f);
        else
            gif->ls_dsc.global_color_table_flag = 0;
    } else {
        gif->global_ct = NULL;
    }
    read_contents(gif, f);

}

static struct pic *
GIF_load_one(Graphic *g)
{
    struct pic *p = pic_alloc(sizeof(GIF *));
    p->pic = NULL;
    p->left = g->image->image_dsc.left;
    p->top = g->image->image_dsc.top;
    p->width = g->image->image_dsc.width;
    p->height = g->image->image_dsc.height;
    p->depth = 32;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = g->image->data;
    p->format = CS_PIXELFORMAT_RGB888;
    return p;
}

static bool
graphic_is_image(const Graphic* g)
{
    return g->type == GRAPHIC_IMAGE && g->image && g->image->data;
}

static struct pic *
GIF_load(const char *filename, int skip_flag)
{
    struct pic *p = pic_alloc(sizeof(GIF));
    GIF* g = (GIF *)p->pic;
    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        pic_free(p);
        return NULL;
    }
    read_gif(f, g);
    fclose(f);
    if (!skip_flag) {
        if (g->graphic_count > 1) {
            struct pic *last = NULL;
            for (int i = 0; i < g->graphic_count; i++) {
                if (!graphic_is_image(&g->graphics[i]))
                    continue;
                last = GIF_load_one(&g->graphics[i]);
                file_enqueue_pic(last);
            }
            if (last == NULL) {
                VERR(gif, "no image in the file");
                GIF_free(p);
                return NULL;
            }
            //for GIF, the info will be stored in the last frame
            last->pic = g;
            return NULL;
        } else if (g->graphic_count == 1 && graphic_is_image(&g->graphics[0])) {
            p = GIF_load_one(g->graphics);
            p->pic = g;
            return p;
        }
        VERR(gif, "no image in the file");
        GIF_free(p);
        return NULL;
    }
    return p;
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
