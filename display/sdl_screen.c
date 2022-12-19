
#include <stdbool.h>
#include <stdint.h>

#include "display.h"
#include "sdl_screen.h"

#define SCREEN_WIDTH   640
#define SCREEN_HEIGHT  480


static sdl_screen scrn = {
    .w = NULL,
    .r = NULL,
    .t = NULL,
};

static int 
display_main(void *data)
{
    return 0;
}


int sdl_draw(void *pixels, int left, int top, int width, int height, int depth, int pitch,
            uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask)
{
    SDL_Rect rect;

    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(pixels, width, height, depth, pitch,
         rmask, gmask, bmask, amask);
    if (s == NULL) {
        return -1;
    }

    SDL_Texture * texture = SDL_CreateTextureFromSurface(scrn.r, s);

    rect.w = width;
    rect.h = height;
    rect.x = left;// + scrn.width/2 - width/2;
    rect.y = top; // + scrn.height/2 - height/2;

    if (scrn.t == NULL) {
        scrn.t = texture;
        SDL_SetRenderTarget(scrn.r, scrn.t);
    } else {
        SDL_RenderCopy(scrn.r, scrn.t, NULL, NULL);
    }
    SDL_RenderCopy(scrn.r, texture, NULL, &rect);
    SDL_RenderPresent(scrn.r);
    if (scrn.t != texture) {
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(s);
    return 0;
}

static int 
sdl_screen_init(const char* title, int w, int h)
{
    int ret;

    ret = SDL_Init(SDL_INIT_EVERYTHING);
    if (ret == -1) {
        return -1;
    }

    // scrn.tid = SDL_CreateThread(display_main, "Display Thread", NULL);
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

    scrn.t = SDL_CreateTexture(scrn.r, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (scrn.t == NULL) {
        SDL_DestroyRenderer(scrn.r);
        SDL_DestroyWindow(scrn.w);
        return -1;
    }
    
    // scrn.width = w;
    // scrn.height = h;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_SetRenderTarget(scrn.r, scrn.t);
    SDL_SetRenderDrawBlendMode(scrn.r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(scrn.r, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(scrn.r);
    SDL_RenderPresent(scrn.r);
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

void pic_poll_block(bool q)
{
    // int status;
    // SDL_WaitThread(scrn.tid, &status);
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
