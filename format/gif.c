#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "gif.h"
#include "file.h"
#include "lzw.h"
#include "vlog.h"

VLOG_REGISTER(gif, INFO);

#define GRAPHIC_PLAINTEXT      0x01
#define GRAPHIC_IMAGE          0x2C
#define GRAPHIC_CONTROL        0xF9

#define EXTENSION_COMMENT      0xFE
#define EXTENSION_APPLICATION  0xFF

#define EXTENSION_INTRODUCER   0x21
#define TRAILER                0x3B

int read_blocks(uint8_t** data, FILE* file) 
{
    uint8_t block_length = (uint8_t)fgetc(file);
    int total_length = 0;
    while (block_length) {
        if (total_length == 0) {
            *data = (uint8_t*)malloc(block_length);
        } else {
            *data = (uint8_t*)realloc(*data, total_length + block_length);
        }
        fread(*data + total_length, 1, block_length, file);
        total_length += block_length;
        block_length = (uint8_t)fgetc(file);
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
    Image* image = (Image*)malloc(sizeof(Image));
    fread(&image->image_dsc, sizeof(struct image_descriptor), 1, f);

    if (image->image_dsc.local_color_table_flag) {
        image->local_ct = (Color*)malloc(sizeof(Color) * (2 << image->image_dsc.local_color_table_size));
        for (int i = 0; i < 2 << image->image_dsc.local_color_table_size; i ++) {
            fread(image->local_ct, 3, 2 << image->image_dsc.local_color_table_size, f);
        }
    } else {
        image->local_ct = NULL;
    }

    int width = ((image->image_dsc.width + 3) >> 2) << 2;
    int image_data_length = width * image->image_dsc.height;
    image->data = (uint8_t*)malloc(image_data_length * 4);
    
    uint8_t* compressed = NULL;
    uint8_t lzw_code_size = (uint8_t)fgetc(f);
    int compressed_length = read_blocks(&compressed, f);
    // lzw_decode
    // loadlzw
    lzw_decode_gif(1, lzw_code_size, compressed, compressed_length, image->data);
    uint8_t *p = image->data;
    Color *ct = NULL;
    if (image->local_ct == NULL) {
        ct = gif->global_ct;
    } else {
        ct = image->local_ct;
    }
    for (int j = image->image_dsc.height - 1; j >= 0; j --) {
        for(int i = width- 1; i >=0; i -- ) {
            uint8_t cindex = *(p+i+j*width);
            *(p +  (j * 4 * width)+ 4*i) = ct[cindex].b;
            *(p +  (j * 4 * width)+ 4*i + 1) = ct[cindex].g;
            *(p +  (j * 4 * width)+ 4*i + 2 ) = ct[cindex].r; 
            // *(p +  (j * 4 * width)+ 4*i + 3 ) = 0;
        }
    }

    free(compressed);

    Graphic* graphic = gif_next_dataless_graphic(gif);
    graphic->type = GRAPHIC_IMAGE;
    graphic->image = image;
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
    fread(&gif->ls_dsc, 1, sizeof(struct logical_screen_descriptor), f);

    if (gif->ls_dsc.global_color_table_flag) {
        gif->global_ct = (Color*)malloc(3 * (2<< gif->ls_dsc.global_color_table_size));
        fread(gif->global_ct, 3, 2 << gif->ls_dsc.global_color_table_size, f);
    } else {
        gif->global_ct = NULL;
    }
    read_contents(gif, f);

}

struct pic* GIF_load(const char* filename) {
    struct pic *p = pic_alloc(sizeof(GIF));
    FILE* file = fopen(filename, "rb");
    GIF* gif = p->pic;
    read_gif(file, gif);
    fclose(file);
    p->pic = gif;
    p->width = gif->ls_dsc.screen_witdh;
    p->height = gif->ls_dsc.screen_height;
    p->depth = 32;
    p->pitch = ((p->width * p->depth + p->depth - 1) >> 5) << 2;
    p->pixels = gif->graphics->image->data;
    // p->amask = 0;
    // p->rmask = 0;
    // p->gmask = 0;
    // p->bmask = 0;
    return p;
}

static int 
GIF_probe(const char* filename)
{
    struct gif_file_header head;
    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        VERR(gif, "fail to open %s", filename);
        return -ENOENT;
    }
    int len = fread(&head, 1, GIF_FILE_HEADER_LEN, f);
    if (len < 6) {
        fclose(f);
        return -EBADF;
    }
    fclose(f);
    if (!memcmp(&head, "GIF89a", GIF_FILE_HEADER_LEN) || 
            !memcmp(&head, "GIF87a", GIF_FILE_HEADER_LEN)) {
        return 0;
    }
    return -EINVAL;
}

static void 
GIF_info(FILE* f, struct pic* p)
{
    GIF *g = (GIF*)(p->pic);
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
    if (g->graphics->control) {
        fprintf(f, "\tfirst image: disposal_method %d\n", g->graphics->control->disposal_method);
        fprintf(f, "\tfirst image: delay_time %d\n", g->graphics->control->delay_time);
    }
    fprintf(f, "\tfirst image: width %d height %d\n", g->graphics->image->image_dsc.width,
                g->graphics->image->image_dsc.height);
    fprintf(f, "\tfirst image: left %d top %d\n", g->graphics->image->image_dsc.left,
                g->graphics->image->image_dsc.top);
    fprintf(f, "\tfirst image: interlace %d\n", g->graphics->image->image_dsc.interlace_flag);
    fprintf(f, "\tfirst image: local_color_flag %d\n", g->graphics->image->image_dsc.local_color_table_flag);

    if (g->graphic_count > 1) {
        if (g->graphics->control) {
            fprintf(f, "\tsecond image: disposal_method %d\n", g->graphics[1].control->disposal_method);
            fprintf(f, "\tsecond image: delay_time %d\n", g->graphics[1].control->delay_time);
        }
        fprintf(f, "\tsecond image: image type %d\n", g->graphics[1].type);
        fprintf(f, "\tsecond image: width %d height %d\n", g->graphics[1].image->image_dsc.width,
                    g->graphics[1].image->image_dsc.height);
        fprintf(f, "\tsecond image: left %d top %d\n", g->graphics[1].image->image_dsc.left,
                    g->graphics[1].image->image_dsc.top);
        fprintf(f, "\tsecond image: interlace %d\n", g->graphics[1].image->image_dsc.interlace_flag);
        fprintf(f, "\tsecond image: local_color_flag %d\n", g->graphics[1].image->image_dsc.local_color_table_flag);
        fprintf(f, "------------------------------\n");
        if (g->extension_count) {
            for(int i = 0; i < g->extension_count; i ++) {
                switch(g->extensions[i].type) {
                    case EXTENSION_COMMENT:
                        fprintf(f, "\tcomment extension:\n");
                        fprintf(f, "\t%s\n", g->extensions[i].comment->text);
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