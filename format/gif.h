#ifndef _GIF_H
#define _GIF_H

#include <stdbool.h>
#include <stdint.h>

#define DISPOSE_UNDEFINED 0
#define DISPOSE_NONE      1
#define DISPOSE_CLEAR     2
#define DISPOSE_RESTORE   3

#define GRAPHIC_IMAGE         0x2C
#define GRAPHIC_PLAINTEXT     0x01
#define EXTENSION_COMMENT     0xFE
#define EXTENSION_APPLICATION 0xFF

typedef struct {
	uint8_t r, g, b;
} Color;

typedef struct {
	uint8_t disposal_method;
	bool requires_user_input;
	bool uses_transparency;
	uint16_t delay; // in hundreths of a second (0.01)
	uint8_t transparent_color_index;
} GraphicControl;

typedef struct {
	uint16_t left;
	uint16_t top;
	uint16_t width;
	uint16_t height;
	bool interlaced;

	bool uses_local_ct;
	int local_ct_size;
	bool local_ct_sorted;
	Color* local_ct;

	bool has_graphic_control;
	GraphicControl* control;

	uint8_t* data;
} Image;

typedef struct {
	uint16_t text_left;
	uint16_t text_top;
	uint16_t text_width;
	uint16_t text_height;
	uint8_t char_width;
	uint8_t char_height;
	uint8_t foreground_color_index;
	uint8_t background_color_index;
	char* text;
	int text_length;
} PlainText;

typedef struct {
	bool has_control;
	GraphicControl* control;

	uint8_t type;
	Image* image;
	PlainText* plaintext;	
} Graphic;

typedef struct {
	uint8_t identifier[8];
	uint8_t auth_code[3];
	uint8_t* data;
	int data_length;
} Application;

typedef struct {
	char* text;
	int text_length;
} Comment;

typedef struct {
	uint8_t type;
	Application* application;
	Comment* comment;
} Extension;

#define GIF_FILE_HEADER_LEN (6)

#pragma pack(push, 1)
struct gif_file_header
{
	uint8_t signature[3];
	uint8_t version[3];
};

#pragma pack(pop)
typedef struct {
	struct gif_file_header file_h;
	uint16_t width;
	uint16_t height;
	uint8_t background_color_index;
	uint8_t aspect_ratio;

	bool uses_global_ct;
	uint8_t global_ct_resolution;
	int global_ct_size;
	bool global_ct_sorted;
	Color* global_ct;

	int graphic_count;
	Graphic* graphics;

	int extension_count;
	Extension* extensions;
} GIF;


void GIF_init(void);

#endif