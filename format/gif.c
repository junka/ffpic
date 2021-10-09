#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "gif.h"
#include "file.h"

#define EXTENSION_INTRODUCER   0x21
#define TRAILER                0x3B
#define GRAPHIC_CONTROL        0xF9

typedef struct {
	int length;
	int prev;
	byte data;
} Entry;

ushort fget_ushort(FILE* file) {
	ushort value;
	fread(&value, 2, 1, file);
	return value;
}

byte fget_byte(FILE* file) {
	return (byte)fgetc(file);
}

void read_logical_screen_descriptor(GIF* gif, FILE* file) {
	gif->width = fget_ushort(file);
	gif->height = fget_ushort(file);

	byte packet = fget_byte(file);
	gif->uses_global_ct = (packet & 0x80) != 0;
	gif->global_ct_resolution = ((packet & 0x70) >> 4) + 1;
	gif->global_ct_sorted = (packet & 0x08) != 0;
	gif->global_ct_size = 2 << (packet & 0x07);

	gif->background_color_index = fget_byte(file);
	gif->aspect_ratio = fget_byte(file);

	if (gif->uses_global_ct) {
		gif->global_ct = (Color*)malloc(3 * gif->global_ct_size);
		fread(gif->global_ct, 3, gif->global_ct_size, file);
	} else {
		gif->global_ct = NULL;
	}
}

int read_blocks(byte** data, FILE* file) {
	byte block_length = fget_byte(file);
	int total_length = 0;
	while (block_length) {
		if (total_length == 0) {
			*data = (byte*)malloc(block_length);
		} else {
			*data = (byte*)realloc(*data, total_length + block_length);
		}
		fread(*data + total_length, 1, block_length, file);
		total_length += block_length;
		block_length = fget_byte(file);
	}
	return total_length;
}

void read_image_data(int lzw_code_size, const byte* compressed, int compressed_length, byte* decompressed) {
	int code_length = lzw_code_size + 1;
	int clear_code = 1 << lzw_code_size;
	int end_of_information = clear_code + 1;
	int reset_code_length = code_length;
	int prev = -1;

	int buffer = 0;
	int buffer_size = 0;

	int dictionary_index;
	int dictionary_capacity = 1 << code_length;
	Entry* dictionary = (Entry*)malloc(dictionary_capacity * sizeof(Entry));
	for (dictionary_index = 0; dictionary_index < clear_code; dictionary_index++) {
		dictionary[dictionary_index].data = dictionary_index;
		dictionary[dictionary_index].prev = -1;
		dictionary[dictionary_index].length = 1;
	}
	dictionary_index += 2;

	for (int i = 0; i<compressed_length; i++) {
		buffer |= compressed[i] << buffer_size;
		buffer_size += 8;

		while (buffer_size >= code_length) {
			buffer_size -= code_length;
			int code = buffer & ((1 << code_length) - 1);
			buffer >>= code_length;

			if (code == clear_code) {
				code_length = reset_code_length;
				dictionary_capacity = 1 << code_length;

				for (dictionary_index = 0; dictionary_index < clear_code; dictionary_index++) {
					dictionary[dictionary_index].data = dictionary_index;
					dictionary[dictionary_index].prev = -1;
					dictionary[dictionary_index].length = 1;
				}
				dictionary_index += 2;
				prev = -1;
				continue;
			}

			if (code == end_of_information) {
				free(dictionary);
				return;
			}

			if ((prev > -1) && (dictionary_index != dictionary_capacity)) {
				if (code > dictionary_index) {
					printf("lzw code error, got %d, but dict %d\n", code, dictionary_index);
					free(dictionary);
					return;
				}

				int ptr = code == dictionary_index ? prev : code;
				while (dictionary[ptr].prev != -1) {
					ptr = dictionary[ptr].prev;
				}
				dictionary[dictionary_index].data = dictionary[ptr].data;
				dictionary[dictionary_index].prev = prev;
				dictionary[dictionary_index].length 	= dictionary[prev].length + 1;
				dictionary_index++;

				if ((dictionary_index == dictionary_capacity) && (code_length < 12)) {
					code_length++;
					dictionary_capacity <<= 1;
					dictionary = (Entry*)realloc(dictionary, dictionary_capacity * sizeof(Entry));
				}
			}

			prev = code;
			int match_length = dictionary[code].length;
			while (code != -1) {
				decompressed[dictionary[code].length - 1] = dictionary[code].data;
				if (dictionary[code].prev == code) {
					printf("self reference error\n");
					free(dictionary);
					return;
				}
				code = dictionary[code].prev;
			}
			decompressed += match_length;
		}
	}

	free(dictionary);
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

void read_image(GIF* gif, FILE* file) {
	Image* image = (Image*)malloc(sizeof(Image));
	image->left = fget_ushort(file);
	image->top = fget_ushort(file);
	image->width = fget_ushort(file);
	image->height = fget_ushort(file);

	byte packet = fget_byte(file);
	image->interlaced = (packet & 0x40) != 0;
	image->uses_local_ct = (packet & 0x80) != 0;
	image->local_ct_sorted = (packet & 0x20) != 0;
	image->local_ct_size = 2 << (packet & 0x07);

	if (image->uses_local_ct) {
		image->local_ct = (Color*)malloc(3 * image->local_ct_size);
		fread(image->local_ct, 3, image->local_ct_size, file);
	} else {
		image->local_ct = NULL;
	}

	int image_data_length = image->width * image->height;
	image->data = (byte*)malloc(image_data_length);
	
	byte* compressed = NULL;;
	byte lzw_code_size = fget_byte(file);
	int compressed_length = read_blocks(&compressed, file);
	read_image_data(lzw_code_size, compressed, compressed_length, image->data);
	free(compressed);

	Graphic* graphic = gif_next_dataless_graphic(gif);
	graphic->type = GRAPHIC_IMAGE;
	graphic->image = image;
}

void read_plaintext_ext(GIF* gif, FILE* file) {
	// no one ever uses this, but hey. if you want it, it's here.
	PlainText* plaintext = (PlainText*)malloc(sizeof(PlainText));
	fget_byte(file);
	plaintext->text_left = fget_ushort(file);
	plaintext->text_top = fget_ushort(file);
	plaintext->text_width = fget_ushort(file);
	plaintext->text_height = fget_ushort(file);
	plaintext->char_width = fget_byte(file);
	plaintext->char_height = fget_byte(file);
	plaintext->foreground_color_index = fget_byte(file);
	plaintext->background_color_index = fget_byte(file);
	plaintext->text_length = read_blocks((byte**)(&plaintext->text), file);
	fget_byte(file);

	Graphic* graphic = gif_next_dataless_graphic(gif);
	graphic->type = GRAPHIC_PLAINTEXT;
	graphic->plaintext = plaintext;
}

void read_graphic_control_ext(GIF* gif, FILE* file) {
	GraphicControl* control = (GraphicControl*)malloc(sizeof(GraphicControl));

	fget_byte(file);
	byte packet = fget_byte(file);
	control->disposal_method = (packet >> 2) & 7;
	control->requires_user_input = (packet & 2) != 0;
	control->uses_transparency = (packet & 1) != 0;
	control->delay = fget_ushort(file);
	control->transparent_color_index = fget_byte(file);
	fget_byte(file);

	Graphic* graphic = gif_increment_graphics(gif);
	graphic->has_control = true;
	graphic->control = control;
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
	comment->text_length = read_blocks((byte**)&comment->text, file);

	Extension* extension = gif_increment_extensions(gif);
	extension->type = EXTENSION_COMMENT;
	extension->comment = comment;
}

void read_application_ext(GIF* gif, FILE* file) {
	Application* application = (Application*)malloc(sizeof(Application));
	fget_byte(file);
	fread(application->identifier, 1, 8, file);
	fread(application->auth_code, 1, 3, file);
	application->data_length = read_blocks(&application->data, file);

	Extension* extension = gif_increment_extensions(gif);
	extension->type = EXTENSION_APPLICATION;
	extension->application = application;
}

void read_extension(GIF* gif, FILE* file) {
	byte extension_code = fget_byte(file);
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
		byte block_code = fget_byte(file);
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
			printf("invalid block code [%x]\n", block_code);
			return;
		}
	}
}

GIF* read_gif(FILE* file) {

	GIF* gif = (GIF*)malloc(sizeof(GIF));
	gif->graphic_count = 0;
	gif->extension_count = 0;
	gif->graphics = NULL;
	gif->extensions = NULL;

	read_logical_screen_descriptor(gif, file);
	read_contents(gif, file);

	return gif;
}

struct pic* GIF_load(const char* filename) {
	struct pic *p = malloc(sizeof(struct pic));
	FILE* file = fopen(filename, "rb");
	GIF* gif = read_gif(file);
	fclose(file);
	p->pic = gif;
	return p;
}

int GIF_probe(const char* filename)
{
	char head[6];
	FILE* f = fopen(filename, "rb");
	if (f == NULL) {
        printf("fail to open %s\n", filename);
        return -ENOENT;
    }
	int len = fread(head, 1, 6, f);
	if (len < 6) {
        fclose(f);
        return -EBADF;
    }
	fclose(f);
	if (!memcmp(head, "GIF89a", 6) || !memcmp(head, "GIF87a", 6)) {
		return 0;
	}
	return -EINVAL;
}

void image_free(Image* image) {
	if (image->uses_local_ct)
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

	if (gif->uses_global_ct)
		free(gif->global_ct);

	free(gif);
	free(p);
}
static struct file_ops gif_ops = {
    .name = "GIF",
    .probe = GIF_probe,
    .load = GIF_load,
    .free = GIF_free,
};

void GIF_init(void)
{
    file_ops_register(&gif_ops);
}