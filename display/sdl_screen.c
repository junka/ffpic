
#include <stdbool.h>
#include <stdint.h>

#include "SDL_events.h"
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

    //SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, width, height, depth, format);
    SDL_Surface * s = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, depth, pitch, format);
    if (!s) {
        return -1;
    }
    // SDL_memcpy(s->pixels, pixels, height * pitch);

    SDL_Texture * texture = SDL_CreateTextureFromSurface(scrn.r, s);
    // printf("width %d, height %d, left %d, top %d, pitch %d, depth %d\n", width, height, left, top, pitch, depth);

    if (!scrn.t) {
        scrn.t = texture;
        SDL_SetRenderTarget(scrn.r, scrn.t);
    } else {
        SDL_RenderCopy(scrn.r, scrn.t, NULL, &scrn.rect);
        // SDL_RenderClear(scrn.r);
        // SDL_RenderPresent(scrn.r);
    }
    SDL_RenderCopy(scrn.r, texture, NULL, &scrn.rect);
    SDL_RenderPresent(scrn.r);
    if (scrn.t != texture) {
        SDL_DestroyTexture(scrn.t);
        scrn.t = texture;
    }
    SDL_FreeSurface(s);
    return 0;
}


void pic_poll_block(bool q)
{
    SDL_RenderClear(scrn.r);
    SDL_Event e;
    bool quit = false;
    int mx, my;
    // int fx, fy;
    // int sx, sy;
    // int delta = 0;
    while (!quit) {
        quit = q;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
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
            // case SDL_FINGERMOTION:
            //     if (e.tfinger.fingerId == 0) {
            //         fx = e.tfinger.x;
            //         fy = e.tfinger.y;
            //     } else if (e.tfinger.fingerId == 1) {
            //         sx = e.tfinger.x;
            //         sy = e.tfinger.y;
            //     }
            //     if (delta == 0) {
            //         delta = (fx - sx) * (fx - sx) + (fy - sy) * (fy - sy);
            //     } else if (delta > (fx - sx) * (fx - sx) + (fy - sy) * (fy - sy)) {
            //         scrn.rect.h *= 1.01;
            //         scrn.rect.w *= 1.01;
            //     } else if (delta < (fx - sx) * (fx - sx) + (fy - sy) * (fy - sy)) {
            //         scrn.rect.h *= 1.01;
            //         scrn.rect.w *= 1.01;
            //     }
            //     break;
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
