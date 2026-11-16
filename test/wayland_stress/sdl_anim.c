/*
        The stress bench's animation through SDL2 (SDL_VIDEODRIVER=wayland): a window drawn by SDL's renderer with vsync,
        a field and a bar that moves, for SECONDS. Its frame times are the returns of SDL_RenderPresent (toolkit_common.h).

            sdl-anim [SECONDS] [W] [H]
*/
#include <SDL.h>

#include "toolkit_common.h"

int main(int argc, char **argv)
{
        tk_exec = tk_now();
        double seconds = argc > 1 ? atof(argv[1]) : 3;
        int w = argc > 2 ? atoi(argv[2]) : 1280, h = argc > 3 ? atoi(argv[3]) : 720;
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
                printf("NOTE SDL cannot start its video here: %s\n", SDL_GetError());
                return 3;
        }
        SDL_Window *win = SDL_CreateWindow("sdl-anim", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, 0);
        if (!win)
        {
                printf("NOTE SDL made no window: %s\n", SDL_GetError());
                return 3;
        }
        SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!r)
        {
                printf("NOTE SDL made no renderer: %s\n", SDL_GetError());
                return 3;
        }
        SDL_RendererInfo info;
        SDL_GetRendererInfo(r, &info);
        uint64_t end = tk_now() + (uint64_t)(seconds * 1e9);
        tk_warm = tk_now() + 500000000ull;
        unsigned frame = 0;
        while (tk_now() < end)
        {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                        if (e.type == SDL_QUIT)
                                end = 0;
                SDL_SetRenderDrawColor(r, (Uint8)(frame * 3), 0x30, 0x60, 255);
                SDL_RenderClear(r);
                SDL_Rect bar = {(int)((frame * 8u) % (unsigned)w), 0, 24, h};
                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
                SDL_RenderFillRect(r, &bar);
                SDL_RenderPresent(r);
                tk_frame();
                frame++;
        }
        tk_report("sdl", info.name);
        SDL_Quit();
        return 0;
}
