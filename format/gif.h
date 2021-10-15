#ifndef _GIF_H
#define _GIF_H

#include <stdbool.h>
#include <stdint.h>

#define DISPOSE_UNDEFINED 0
#define DISPOSE_NONE      1
#define DISPOSE_CLEAR     2
#define DISPOSE_RESTORE   3

#define _LITTLE_ENDIAN_

#pragma pack(push, 1)

typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} Color;

enum dispose_method {
	UNSPECIFIED = 0,
	DO_NOT_DISPOSE = 1,
	RESTORE_TO_BACKGROUND = 2,
	RESTORE_TO_PREVIOUS = 3,
};

struct graphic_control_extension {
	uint8_t block_size;
#ifdef _LITTLE_ENDIAN_
	uint8_t transparent_color_flag : 1;
	uint8_t user_input_flag : 1;
	uint8_t disposal_method : 3;
	uint8_t reserved : 3;
#endif
	uint16_t delay_time;         // in hundreths of a second (0.01)
	uint8_t transparent_color_index;
	uint8_t block_terminator;
};

struct image_descriptor {
	// uint8_t seprator; //0x2C
	uint16_t left;
	uint16_t top;
	uint16_t width;
	uint16_t height;
#ifdef _LITTLE_ENDIAN_
	uint8_t local_color_table_size : 3;
	uint8_t reserved : 2;
	uint8_t sort_flag : 1;
	uint8_t interlace_flag :1;
	uint8_t local_color_table_flag :1;
#endif
};

typedef struct {
	struct image_descriptor image_dsc;
	Color* local_ct;

	bool has_graphic_control;
	struct graphic_control_extension* control;

	uint8_t* data;
} Image;

struct plaintext_extension {
	uint8_t block_size;
	uint16_t text_left;
	uint16_t text_top;
	uint16_t text_width;
	uint16_t text_height;
	uint8_t char_width;
	uint8_t char_height;
	uint8_t foreground_color_index;
	uint8_t background_color_index;
};

typedef struct {
	struct plaintext_extension text_ext;
	char* text;
	int text_length;
} PlainText;

typedef struct {
	bool has_control;
	struct graphic_control_extension* control;

	uint8_t type;
	Image* image;
	PlainText* plaintext;	
} Graphic;

struct application_extension {
	uint8_t block_size;
	uint8_t identifier[8];
	uint8_t auth_code[3];
};
typedef struct {
	struct application_extension app_ext;
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

struct gif_file_header
{
	uint8_t signature[3];
	uint8_t version[3];
};

struct logical_screen_descriptor {
	uint16_t screen_witdh;    // unit in pixel
	uint16_t screen_height;
#ifdef _LITTLE_ENDIAN_
	uint8_t global_color_table_size : 3;
	uint8_t global_color_sort_flag : 1;
	uint8_t global_color_resolution : 3;
	uint8_t global_color_table_flag : 1;
#endif
	uint8_t background_color_index;
	uint8_t pixel_aspect_ratio; // width:height ratio
};

#pragma pack(pop)

typedef struct {
	struct gif_file_header file_h;
	struct logical_screen_descriptor ls_dsc;

	Color* global_ct;

	int graphic_count;
	Graphic* graphics;

	int extension_count;
	Extension* extensions;
} GIF;


void GIF_init(void);

#endif