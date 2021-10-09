#ifndef _SCREEN_H_
#define _SCREEN_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <SDL.h>

typedef struct {
    const char* name;
    int width;
    int height;
    SDL_Window *w;
    SDL_Renderer *r;
    SDL_Texture *t;

    int (*init)(int w, int h);
    int (*uninit)(void);
} Screen;

int pic_draw(void *pixels, int width, int height, int depth, int pitch,
            uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask);

int pic_refresh();

int pic_clear();

void pic_register_screens();

#ifdef __cplusplus
}
#endif

#endif /*_SCREEN_H_*/