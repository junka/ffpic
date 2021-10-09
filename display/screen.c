
#include <stdbool.h>
#include <stdint.h>

#include "screen.h"

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480

static int pic_screen_init();
static int pic_screen_uninit();

Screen scrn = {
    .name = "SDL2",
    .w = NULL,
    .r = NULL,
    .t = NULL,
    .init = pic_screen_init,
    .uninit = pic_screen_uninit,
};

static int 
pic_screen_init(int w, int h)
{
    int ret;

    ret = SDL_Init(SDL_INIT_EVERYTHING);
    if (ret == -1) {
        return -1;
    }

    scrn.w = SDL_CreateWindow("Pic", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (scrn.w == NULL) {
        return -1;
    }

    // SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    scrn.r = SDL_CreateRenderer(scrn.w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (scrn.r == NULL) {
        SDL_DestroyWindow(scrn.w);
        return -1;
    }

    scrn.t = SDL_CreateTexture(scrn.r, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (scrn.t == NULL) {
        SDL_DestroyRenderer(scrn.r);
        SDL_DestroyWindow(scrn.w);
        return -1;
    }
    
    scrn.width = w;
    scrn.height = h;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_SetRenderDrawColor(scrn.r, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(scrn.r);
    SDL_RenderPresent(scrn.r);

    return 0;
}

static int 
pic_screen_uninit()
{
    SDL_Surface * ws = SDL_GetWindowSurface(scrn.w);
    SDL_FreeSurface(ws);
    SDL_DestroyRenderer(scrn.r);
    SDL_DestroyWindow(scrn.w);
    SDL_Quit();
    return 0;
}

int pic_draw(void *pixels, int width, int height, int depth, int pitch,
            uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask)
{
    SDL_Rect r;

    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(pixels, width, height, depth, pitch,
         rmask, gmask, bmask, amask);
    if (s == NULL) {
        return -1;
    }

    SDL_Texture * texture = SDL_CreateTextureFromSurface(scrn.r, s);

    SDL_Event e;

    bool quit = false;
    while (!quit) {
        SDL_RenderClear(scrn.r);
        SDL_RenderCopy(scrn.r, texture, NULL, NULL);
        SDL_RenderPresent(scrn.r);
        if (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT){
                quit = true;
            }
            if (e.type == SDL_KEYDOWN){
                quit = true;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                quit = true;
            }
        }
    }

    SDL_FreeSurface(s);
    return 0;
}

void
pic_register_screens() {
    // pic_register_screen(&scrn);
}