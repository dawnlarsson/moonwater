/*
        An Xlib client for the X rows of test/wayland_xwayland.sh: it maps one window of a fixed colour, draws it on every
        expose, and says on its standard output what the X server sends it, one line an event and flushed: "expose",
        "configure W H", "key KEYSYM" (in hex), "button N X Y ROOT_X ROOT_Y" and "motion ROOT_X ROOT_Y". It stops after the
        seconds it is given (-t) with its output complete, and uses no X extension but the core protocol.

            xclient [-w WIDTH] [-h HEIGHT] [-t SECONDS]

        It is built against the X11 headers and libX11 of the root Xwayland runs in (test/wayland_xwayland.sh says how).
*/
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define XCLIENT_COLOUR 0x3366cc

static int number_of(const char *text, int fallback)
{
        char *end = NULL;
        long value = strtol(text, &end, 10);

        if (end == text || *end || value < 1 || value > 4096)
                return fallback;
        return (int)value;
}

int main(int argc, char **argv)
{
        Display *display;
        Window window;
        XEvent event;
        GC gc;
        int width = 320, height = 200, seconds = 30;

        for (int at = 1; at + 1 < argc; at += 2)
        {
                if (!strcmp(argv[at], "-w"))
                        width = number_of(argv[at + 1], width);
                else if (!strcmp(argv[at], "-h"))
                        height = number_of(argv[at + 1], height);
                else if (!strcmp(argv[at], "-t"))
                        seconds = number_of(argv[at + 1], seconds);
                else
                        return 2;
        }
        setvbuf(stdout, NULL, _IOLBF, 0);
        display = XOpenDisplay(NULL);
        if (!display)
        {
                fprintf(stdout, "no display\n");
                return 1;
        }
        window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, (unsigned int)width,
                                     (unsigned int)height, 0, 0, XCLIENT_COLOUR);
        XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask |
                                      StructureNotifyMask);
        XStoreName(display, window, "xclient");
        XMapWindow(display, window);
        gc = XCreateGC(display, window, 0, NULL);
        XSetForeground(display, gc, XCLIENT_COLOUR);
        alarm((unsigned int)seconds);
        for (;;)
        {
                XNextEvent(display, &event);
                switch (event.type)
                {
                case Expose:
                        XFillRectangle(display, window, gc, 0, 0, (unsigned int)width, (unsigned int)height);
                        fprintf(stdout, "expose\n");
                        break;
                case ConfigureNotify:
                        fprintf(stdout, "configure %d %d\n", event.xconfigure.width, event.xconfigure.height);
                        break;
                case KeyPress:
                {
                        KeySym sym = XLookupKeysym(&event.xkey, 0);

                        fprintf(stdout, "key %lx\n", (unsigned long)sym);
                        break;
                }
                case ButtonPress:
                        fprintf(stdout, "button %u %d %d %d %d\n", event.xbutton.button, event.xbutton.x,
                                event.xbutton.y, event.xbutton.x_root, event.xbutton.y_root);
                        break;
                case MotionNotify:
                        fprintf(stdout, "motion %d %d\n", event.xmotion.x_root, event.xmotion.y_root);
                        break;
                default:
                        break;
                }
        }
}
