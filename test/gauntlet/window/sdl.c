/*
        The window gauntlet's SDL client, the SDL 2 API: built against sdl2-compat (SDL 3 underneath) and run against any real
        SDL 2 by LD_LIBRARY_PATH (Steam's runtime's, a game's bundled old one). SDL_VIDEODRIVER picks wayland or x11.

            sdl

        Steps, each held to a deadline: a window shown and presenting (an SDL_Renderer with vsync, and SDL_GL with swaps); a size the
        program asks is the size it gets; desktop fullscreen fills the screen, and back gives the size it had; real fullscreen
        likewise; minimized and restored; relative mouse mode on and off; the clipboard round trip; a second window; SDL_Quit.
*/
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *step = "start";

static void watchdog(int sig)
{
        (void)sig;
        char m[200];
        int n = snprintf(m, sizeof m, "FAIL hung at: %s\n", step);
        write(2, m, (size_t)n);
        _exit(1);
}

static void fail(const char *why)
{
        fprintf(stderr, "FAIL %s (at %s): %s\n", why, step, SDL_GetError());
        exit(1);
}

static int frames(SDL_Renderer *r, Uint32 ms)
{
        int n = 0;
        Uint32 end = SDL_GetTicks() + ms;
        SDL_Event e;
        while (SDL_GetTicks() < end)
        {
                while (SDL_PollEvent(&e))
                        ;
                SDL_SetRenderDrawColor(r, (Uint8)(n * 7), 60, 120, 255);
                SDL_RenderClear(r);
                SDL_RenderPresent(r);
                n++;
        }
        return n;
}

static int size_is(SDL_Window *win, SDL_Renderer *r, int w, int h, Uint32 ms)
{
        Uint32 end = SDL_GetTicks() + ms;
        int cw = 0, ch = 0;
        while (SDL_GetTicks() < end)
        {
                frames(r, 50);
                SDL_GetWindowSize(win, &cw, &ch);
                if (cw == w && ch == h)
                        return 1;
        }
        fprintf(stderr, "  size %dx%d, wanted %dx%d\n", cw, ch, w, h);
        return 0;
}

int main(void)
{
        SDL_version v;
        SDL_DisplayMode dm;

        signal(SIGALRM, watchdog);
        alarm(60);
        SDL_GetVersion(&v);
        step = "SDL_Init";
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
                fail("SDL_Init");
        printf("SDL %d.%d.%d, video %s\n", v.major, v.minor, v.patch, SDL_GetCurrentVideoDriver());
        if (SDL_GetDesktopDisplayMode(0, &dm) < 0)
                fail("no display mode");
        step = "create";
        SDL_Window *w = SDL_CreateWindow("gauntlet sdl", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
        if (!w)
                fail("SDL_CreateWindow");
        SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!r)
                fail("SDL_CreateRenderer");
        step = "frames";
        int n = frames(r, 1000);
        if (n < 10)
        {
                fprintf(stderr, "FAIL %d frames presented in a second\n", n);
                return 1;
        }
        step = "resize by the program";
        SDL_SetWindowSize(w, 500, 400);
        if (!size_is(w, r, 500, 400, 3000))
                fail("the size asked is not the size had");
        step = "desktop fullscreen";
        SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (!size_is(w, r, dm.w, dm.h, 3000))
                fail("desktop fullscreen is not the screen's size");
        step = "back from desktop fullscreen";
        SDL_SetWindowFullscreen(w, 0);
        if (!size_is(w, r, 500, 400, 3000))
                fail("back from fullscreen, the size is not the one it had");
        step = "real fullscreen";
        SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN);
        if (!size_is(w, r, dm.w, dm.h, 3000))
                fail("fullscreen is not the screen's size");
        SDL_SetWindowFullscreen(w, 0);
        if (!size_is(w, r, 500, 400, 3000))
                fail("back from real fullscreen, the size is not the one it had");
        step = "minimize";
        SDL_MinimizeWindow(w);
        frames(r, 300);
        SDL_RestoreWindow(w);
        if (frames(r, 500) < 5)
                fail("restored, it presents no frames");
        step = "relative mouse";
        if (SDL_SetRelativeMouseMode(SDL_TRUE) < 0)
                fprintf(stderr, "  note: relative mouse refused: %s\n", SDL_GetError());
        frames(r, 300);
        SDL_SetRelativeMouseMode(SDL_FALSE);
        frames(r, 100);
        step = "clipboard";
        SDL_SetClipboardText("gauntlet-clip-5678");
        frames(r, 200);
        char *clip = SDL_GetClipboardText();
        if (!clip || strcmp(clip, "gauntlet-clip-5678"))
                fprintf(stderr, "  note: the clipboard read back %s\n", clip ? clip : "(nothing)");
        SDL_free(clip);
        step = "second window";
        SDL_Window *w2 = SDL_CreateWindow("gauntlet sdl 2", 10, 10, 320, 240, SDL_WINDOW_SHOWN);
        SDL_Renderer *r2 = w2 ? SDL_CreateRenderer(w2, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
        if (!r2)
                fail("second window");
        if (frames(r2, 500) < 5 || frames(r, 300) < 3)
                fail("two windows: frames stall");
        step = "destroy";
        SDL_DestroyRenderer(r2), SDL_DestroyWindow(w2);
        SDL_DestroyRenderer(r), SDL_DestroyWindow(w);
        alarm(10);
        step = "SDL_Quit";
        SDL_Quit();
        printf("PASS sdl %s\n", getenv("SDL_VIDEODRIVER") ? getenv("SDL_VIDEODRIVER") : "default");
        return 0;
}
