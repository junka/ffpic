#ifndef _GIF_H
#define _GIF_H

#include <stdbool.h>

#define DISPOSE_UNDEFINED 0
#define DISPOSE_NONE      1
#define DISPOSE_CLEAR     2
#define DISPOSE_RESTORE   3

#define GRAPHIC_IMAGE         0x2C
#define GRAPHIC_PLAINTEXT     0x01
#define EXTENSION_COMMENT     0xFE
#define EXTENSION_APPLICATION 0xFF

typedef unsigned char byte;
typedef unsigned short ushort;

typedef struct {
	byte r, g, b;
} Color;

typedef struct {
	byte disposal_method;
	bool requires_user_input;
	bool uses_transparency;
	ushort delay; // in hundreths of a second (0.01)
	byte transparent_color_index;
} GraphicControl;

typedef struct {
	ushort left;
	ushort top;
	ushort width;
	ushort height;
	bool interlaced;

	bool uses_local_ct;
	int local_ct_size;
	bool local_ct_sorted;
	Color* local_ct;

	bool has_graphic_control;
	GraphicControl* control;

	byte* data;
} Image;

typedef struct {
	ushort text_left;
	ushort text_top;
	ushort text_width;
	ushort text_height;
	byte char_width;
	byte char_height;
	byte foreground_color_index;
	byte background_color_index;
	char* text;
	int text_length;
} PlainText;

typedef struct {
	bool has_control;
	GraphicControl* control;

	byte type;
	Image* image;
	PlainText* plaintext;	
} Graphic;

typedef struct {
	byte identifier[8];
	byte auth_code[3];
	byte* data;
	int data_length;
} Application;

typedef struct {
	char* text;
	int text_length;
} Comment;

typedef struct {
	byte type;
	Application* application;
	Comment* comment;
} Extension;

typedef struct {
	ushort width;
	ushort height;
	byte background_color_index;
	byte aspect_ratio;

	bool uses_global_ct;
	byte global_ct_resolution;
	int global_ct_size;
	bool global_ct_sorted;
	Color* global_ct;

	int graphic_count;
	Graphic* graphics;

	int extension_count;
	Extension* extensions;
} GIF;

GIF* GIF_Load(const char*);
void GIF_Free(GIF*);

#endif