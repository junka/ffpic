#ifndef _SDL_SCREEN_H_
#define _SDL_SCREEN_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdbool.h>
#include <SDL.h>

typedef struct {
    SDL_Window *w;
    SDL_Renderer *r;
    SDL_Texture *t;
    SDL_Rect rect;
    // SDL_Thread *tid;
} sdl_screen;

void pic_poll_block(bool q);

void sdl_screen_register(void);

#ifdef __cplusplus
}
#endif

#endif /*_SDL_SCREEN_H_*/