
#include <stdbool.h>
#include <stdint.h>

#include "display.h"
#include "exr.h"
#include "sdl_screen.h"

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480


static sdl_screen scrn = {
    .w = NULL,
    .r = NULL,
    .t = NULL,
};


int sdl_draw(void *pixels, int left, int top, int width, int height, int depth, int pitch, int format)
{
    SDL_Rect rect;
    rect.w = width;
    rect.h = height;
    rect.x = left;
    rect.y = top;

    //SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, width, height, depth, format);
    SDL_Surface * s = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, depth, pitch, format);
    if (!s) {
        return -1;
    }
    // SDL_memcpy(s->pixels, pixels, height * pitch);

    SDL_Texture * texture = SDL_CreateTextureFromSurface(scrn.r, s);
    // printf("width %d, height %d, left %d, top %d, pitch %d, depth %d\n", width, height, left, top, pitch, depth);
    rect.w = width;
    rect.h = height;
    rect.x = left;
    rect.y = top;

    if (!scrn.t) {
        scrn.t = texture;
        SDL_SetRenderTarget(scrn.r, scrn.t);
    } else {
        SDL_RenderCopy(scrn.r, scrn.t, NULL, &rect);
        // SDL_RenderClear(scrn.r);
        // SDL_RenderPresent(scrn.r);
    }
    SDL_RenderCopy(scrn.r, texture, NULL, &rect);
    SDL_RenderPresent(scrn.r);
    if (scrn.t != texture) {
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(s);
    return 0;
}


void pic_poll_block(bool q)
{
    SDL_RenderClear(scrn.r);
    SDL_Event e;
    bool quit = false;
    while (!quit) {
        quit = q;
        if (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT){
                quit = true;
            }
            // if (e.type == SDL_KEYDOWN){
            //     quit = true;
            // }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                quit = true;
            }
        }
    }
}

static int 
sdl_screen_init(const char* title, int w, int h)
{
    int ret;

    ret = SDL_Init(SDL_INIT_EVERYTHING);
    if (ret == -1) {
        return -1;
    }

    scrn.w = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                w, h, SDL_WINDOW_SHOWN);
    if (scrn.w == NULL) {
        return -1;
    }

    scrn.r = SDL_CreateRenderer(scrn.w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (scrn.r == NULL) {
        SDL_DestroyWindow(scrn.w);
        return -1;
    }

    scrn.t = SDL_CreateTexture(scrn.r, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STATIC, w, h);
    if (scrn.t == NULL) {
        SDL_DestroyRenderer(scrn.r);
        SDL_DestroyWindow(scrn.w);
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_SetRenderTarget(scrn.r, scrn.t);
    SDL_SetRenderDrawBlendMode(scrn.r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(scrn.r, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(scrn.r);
    SDL_RenderPresent(scrn.r);
    pic_poll_block(true);

    return 0;
}

static int 
sdl_screen_uninit()
{
    SDL_Surface * ws = SDL_GetWindowSurface(scrn.w);
    SDL_FreeSurface(ws);
    SDL_DestroyTexture(scrn.t);
    SDL_DestroyRenderer(scrn.r);
    SDL_DestroyWindow(scrn.w);
    SDL_Quit();
    return 0;
}


display_t sdl_display = {
    .name = "SDL2",
    .private = &scrn,
    .width = 0,
    .height = 0,
    .init = sdl_screen_init,
    .uninit = sdl_screen_uninit,
    .draw_pixels = sdl_draw,
};

void
sdl_screen_register(void) {
    display_register(&sdl_display);
}
