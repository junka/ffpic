
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_render.h"
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

/* The pixel format constants in colorspace.h and SDL3 share their encoding,
 * so the container formats pass straight through to the surface.  A 64 bit
 * BGRA picture becomes a 16 bit per channel texture; the renderer converts
 * it to whatever the display surface accepts. */
int sdl_draw(void *pixels, int left, int top, int width, int height, int depth, int pitch, int format)
{
    (void)depth;

    SDL_Surface *s = SDL_CreateSurfaceFrom(width, height,
                                           (SDL_PixelFormat)format,
                                           pixels, pitch);
    if (!s) {
        return -1;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(scrn.r, s);
    SDL_DestroySurface(s);
    if (!texture) {
        return -1;
    }

    if (scrn.t) {
        SDL_DestroyTexture(scrn.t);
    }
    scrn.t = texture;
    scrn.rect.x = (float)left;
    scrn.rect.y = (float)top;
    scrn.rect.w = (float)width;
    scrn.rect.h = (float)height;

    SDL_SetRenderTarget(scrn.r, NULL);
    SDL_RenderClear(scrn.r);
    SDL_RenderTexture(scrn.r, scrn.t, NULL, &scrn.rect);
    SDL_RenderPresent(scrn.r);
    return 0;
}


void pic_poll_block(bool q)
{
    SDL_Event e;
    bool quit = false;
    int mx = 0, my = 0;
    SDL_FingerID fingerId = 0;
    while (!quit) {
        quit = q;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                quit = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_R) {
                    // do rotation
                }
                quit = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mx = (int)e.button.x;
                my = (int)e.button.y;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (e.wheel.y > 0) {
                    scrn.rect.h *= 1.01f;
                    scrn.rect.w *= 1.01f;
                } else {
                    scrn.rect.h /= 1.01f;
                    scrn.rect.w /= 1.01f;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    scrn.rect.x += e.motion.x - (float)mx;
                    scrn.rect.y += e.motion.y - (float)my;
                    mx = (int)e.motion.x;
                    my = (int)e.motion.y;
                }
                break;
            case SDL_EVENT_FINGER_DOWN:
                if (fingerId == 0) {
                    mx = (int)(e.tfinger.x * scrn.rect.w);
                    my = (int)(e.tfinger.y * scrn.rect.w);
                    fingerId = e.tfinger.fingerID;
                }
                break;
            case SDL_EVENT_FINGER_UP:
                if (e.tfinger.fingerID == fingerId) {
                    fingerId = 0;
                }
                break;
            case SDL_EVENT_FINGER_MOTION:
                scrn.rect.x += e.tfinger.x * scrn.rect.w - (float)mx;
                scrn.rect.y += e.tfinger.y * scrn.rect.w - (float)my;
                break;
            }
        }
        SDL_RenderClear(scrn.r);
        if (scrn.t) {
            SDL_RenderTexture(scrn.r, scrn.t, NULL, &scrn.rect);
        }
        SDL_RenderPresent(scrn.r);
    }
}

static int
sdl_screen_init(const char* title, int w, int h)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return -1;
    }

    scrn.w = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    if (scrn.w == NULL) {
        return -1;
    }

    scrn.r = SDL_CreateRenderer(scrn.w, NULL);
    if (scrn.r == NULL) {
        SDL_DestroyWindow(scrn.w);
        scrn.w = NULL;
        return -1;
    }

    SDL_SetRenderVSync(scrn.r, 1);
    SDL_SetRenderDrawBlendMode(scrn.r, SDL_BLENDMODE_BLEND);
    SDL_RenderClear(scrn.r);
    SDL_ShowWindow(scrn.w);

    return 0;
}

static int
sdl_screen_uninit(void)
{
    if (scrn.t) {
        SDL_DestroyTexture(scrn.t);
        scrn.t = NULL;
    }
    if (scrn.r) {
        SDL_DestroyRenderer(scrn.r);
        scrn.r = NULL;
    }
    if (scrn.w) {
        SDL_DestroyWindow(scrn.w);
        scrn.w = NULL;
    }
    SDL_Quit();
    return 0;
}


display_t sdl_display = {
    .name = "SDL3",
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
