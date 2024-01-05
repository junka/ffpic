
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "SDL_events.h"
#include "SDL_render.h"
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
    scrn.rect.w = width;
    scrn.rect.h = height;
    scrn.rect.x = left;
    scrn.rect.y = top;

    SDL_Surface * s = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, depth, pitch, format);
    if (!s) {
        return -1;
    }

    SDL_Texture * texture = SDL_CreateTextureFromSurface(scrn.r, s);
    // printf("width %d, height %d, left %d, top %d, pitch %d, depth %d\n", width, height, left, top, pitch, depth);

    if (scrn.t) {
        SDL_DestroyTexture(scrn.t);
    }
    scrn.t = texture;
    SDL_SetRenderTarget(scrn.r, scrn.t);
    SDL_RenderCopy(scrn.r, scrn.t, NULL, &scrn.rect);
    SDL_RenderPresent(scrn.r);
    SDL_FreeSurface(s);
    return 0;
}


void pic_poll_block(bool q)
{
    SDL_Event e;
    bool quit = false;
    int mx, my;
    SDL_FingerID fingerId = 0;
    while (!quit) {
        quit = q;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
            case SDL_KEYDOWN:
                quit = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                mx = e.button.x;
                my = e.button.y;
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y > 0) {
                    scrn.rect.h *= 1.01;
                    scrn.rect.w *= 1.01;
                } else {
                    scrn.rect.h /= 1.01;
                    scrn.rect.w /= 1.01;
                }
                break;
            case SDL_MOUSEMOTION:
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    scrn.rect.x += e.motion.x - mx;
                    scrn.rect.y += e.motion.y - my;
                    mx = e.motion.x;
                    my = e.motion.y;
                }
                break;
            case SDL_MULTIGESTURE:
                break;
            case SDL_FINGERDOWN:
                if (fingerId == 0) {
                    mx = e.tfinger.x * scrn.rect.w;
                    my = e.tfinger.y * scrn.rect.w;
                    fingerId = e.tfinger.fingerId;
                }
                break;
            case SDL_FINGERUP:
                if (e.tfinger.fingerId == fingerId) {
                    fingerId = 0;
                }
                break;
            case SDL_FINGERMOTION:
                if (e.tfinger.fingerId == fingerId) {
                    scrn.rect.x += e.tfinger.x * scrn.rect.w - mx;
                    scrn.rect.y += e.tfinger.y * scrn.rect.w - my;
                    mx = e.tfinger.x * scrn.rect.w;
                    my = e.tfinger.y * scrn.rect.w;
                }

                break;
            }
        }
        SDL_RenderClear(scrn.r);
        SDL_RenderCopy(scrn.r, scrn.t, NULL, &scrn.rect);
        SDL_RenderPresent(scrn.r);
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
                w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (scrn.w == NULL) {
        return -1;
    }

    scrn.r = SDL_CreateRenderer(scrn.w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (scrn.r == NULL) {
        SDL_DestroyWindow(scrn.w);
        return -1;
    }

    SDL_SetRenderDrawBlendMode(scrn.r, SDL_BLENDMODE_BLEND);
    SDL_RenderClear(scrn.r);

    return 0;
}

static int 
sdl_screen_uninit(void)
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
    setenv("SDL_MOUSE_TOUCH_EVENTS", "1", 1);
    display_register(&sdl_display);
}
