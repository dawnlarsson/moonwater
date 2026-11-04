/*
        The X client of the window manager rows (test/wayland_xwm.sh): an Xlib program whose top-level window the compositor's
        window manager maps, focuses, configures and closes, and whose override-redirect popup it does not. It prints what the
        server sends it, one line each, and takes commands on its standard input, one line each, so the rows say what they
        ask and read what the client was told:

                window 0xID          its top-level window, once, when mapped
                expose               a drawn frame: the window is filled with its colour
                configure W H        its size, as the server now has it
                key K                a key press by its keysym (hex)
                motion X Y           pointer motion, in root coordinates
                button N X Y         a button press, in root coordinates
                delete               WM_DELETE_WINDOW arrived: the client goes (exit 0)
                popup 0xID           the override-redirect popup, mapped at 100,100 (commanded by "popup")
                fullscreen-request   _NET_WM_STATE_FULLSCREEN sent to the root (commanded by "fullscreen")

        Commands: "popup", "fullscreen", "unfullscreen", "resize W H", "quit". The client is started with -w W -h H (the
        window's size) and -t SECONDS (how long it waits for the server before it goes).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define CLIENT_COLOUR 0x3366ccUL
#define POPUP_COLOUR 0x22aa22UL

static Display *display;
static Window top, popup;
static GC gc;
static int width = 800, height = 600;
static Atom wm_protocols, wm_delete, net_wm_state, net_fullscreen;

static void fill(Window w, unsigned long colour, int w_px, int h_px)
{
        XSetForeground(display, gc, colour);
        XFillRectangle(display, w, gc, 0, 0, (unsigned int)w_px, (unsigned int)h_px);
        XFlush(display);
}

static void say_fullscreen_request(void)
{
        XEvent e;

        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = top;
        e.xclient.message_type = net_wm_state;
        e.xclient.format = 32;
        e.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
        e.xclient.data.l[1] = (long)net_fullscreen;
        XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XFlush(display);
        printf("fullscreen-request\n");
        fflush(stdout);
}

static void say_unfullscreen_request(void)
{
        XEvent e;

        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = top;
        e.xclient.message_type = net_wm_state;
        e.xclient.format = 32;
        e.xclient.data.l[0] = 0; // _NET_WM_STATE_REMOVE
        e.xclient.data.l[1] = (long)net_fullscreen;
        XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XFlush(display);
        printf("unfullscreen-request\n");
        fflush(stdout);
}

static void make_popup(void)
{
        XSetWindowAttributes a;

        if (popup)
                return;
        memset(&a, 0, sizeof a);
        a.override_redirect = True;
        a.event_mask = ExposureMask;
        popup = XCreateWindow(display, DefaultRootWindow(display), 100, 100, 120, 80, 0, CopyFromParent, InputOutput,
                              CopyFromParent, CWOverrideRedirect | CWEventMask, &a);
        XMapWindow(display, popup);
        XFlush(display);
        printf("popup 0x%lx\n", (unsigned long)popup);
        fflush(stdout);
}

static void command(const char *line)
{
        int w, h;

        if (strncmp(line, "popup", 5) == 0)
                make_popup();
        else if (strncmp(line, "fullscreen", 10) == 0)
                say_fullscreen_request();
        else if (strncmp(line, "unfullscreen", 12) == 0)
                say_unfullscreen_request();
        else if (sscanf(line, "resize %d %d", &w, &h) == 2 && w > 0 && h > 0)
        {
                XResizeWindow(display, top, (unsigned int)w, (unsigned int)h);
                XFlush(display);
        }
        else if (strncmp(line, "attrs", 5) == 0)
        {
                XWindowAttributes a;

                XGetWindowAttributes(display, top, &a);
                printf("attrs map_state %d override %d x %d y %d w %d h %d\n", a.map_state, a.override_redirect, a.x, a.y, a.width, a.height);
                XGetWindowAttributes(display, DefaultRootWindow(display), &a);
                printf("attrs root map_state %d w %d h %d\n", a.map_state, a.width, a.height);
                fflush(stdout);
        }
        else if (strncmp(line, "quit", 4) == 0)
                exit(0);
}

static void event(XEvent *e)
{
        switch (e->type)
        {
        case Expose:
                if (e->xexpose.count == 0 && e->xexpose.window == top)
                {
                        fill(top, CLIENT_COLOUR, width, height);
                        printf("expose\n");
                        fflush(stdout);
                }
                else if (e->xexpose.count == 0 && e->xexpose.window == popup)
                        fill(popup, POPUP_COLOUR, 120, 80);
                break;
        case ConfigureNotify:
                if (e->xconfigure.window == top && (e->xconfigure.width != width || e->xconfigure.height != height))
                {
                        width = e->xconfigure.width;
                        height = e->xconfigure.height;
                        fill(top, CLIENT_COLOUR, width, height);
                        printf("configure %d %d\n", width, height);
                        fflush(stdout);
                }
                break;
        case KeyPress:
        {
                KeySym sym = XLookupKeysym(&e->xkey, 0);

                printf("key %lx\n", (unsigned long)sym);
                fflush(stdout);
                break;
        }
        case MotionNotify:
                printf("motion %d %d\n", e->xmotion.x_root, e->xmotion.y_root);
                fflush(stdout);
                break;
        case ButtonPress:
                printf("button %u %d %d\n", e->xbutton.button, e->xbutton.x_root, e->xbutton.y_root);
                fflush(stdout);
                break;
        case ClientMessage:
                if ((Atom)e->xclient.message_type == wm_protocols && (Atom)e->xclient.data.l[0] == wm_delete)
                {
                        printf("delete\n");
                        fflush(stdout);
                        exit(0);
                }
                break;
        default:
                break;
        }
}

int main(int argc, char **argv)
{
        long seconds = 40;
        char line[128];
        time_t until;
        int stdin_open = 1;

        for (int at = 1; at + 1 < argc; at += 2)
        {
                if (strcmp(argv[at], "-w") == 0)
                        width = atoi(argv[at + 1]);
                else if (strcmp(argv[at], "-h") == 0)
                        height = atoi(argv[at + 1]);
                else if (strcmp(argv[at], "-t") == 0)
                        seconds = atol(argv[at + 1]);
        }
        display = XOpenDisplay(NULL);
        if (!display)
        {
                fprintf(stderr, "no display\n");
                return 1;
        }
        wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
        wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
        net_wm_state = XInternAtom(display, "_NET_WM_STATE", False);
        net_fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
        top = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, (unsigned int)width, (unsigned int)height, 0, 0, 0);
        XStoreName(display, top, "moonwater xwm client");
        XSetWMProtocols(display, top, &wm_delete, 1);
        XSelectInput(display, top, ExposureMask | KeyPressMask | PointerMotionMask | ButtonPressMask | StructureNotifyMask);
        gc = XCreateGC(display, top, 0, NULL);
        XMapWindow(display, top);
        XFlush(display);
        printf("window 0x%lx\n", (unsigned long)top);
        fflush(stdout);
        until = time(NULL) + seconds;
        for (;;)
        {
                fd_set set;
                struct timeval wait = {0, 50000};
                int top_fd = ConnectionNumber(display) > 0 ? ConnectionNumber(display) : 0;

                while (XPending(display))
                {
                        XEvent e;

                        XNextEvent(display, &e);
                        event(&e);
                }
                if (time(NULL) >= until)
                        break;
                FD_ZERO(&set);
                FD_SET(ConnectionNumber(display), &set);
                if (stdin_open)
                        FD_SET(0, &set);
                if (select(top_fd + 1, &set, NULL, NULL, &wait) > 0 && stdin_open && FD_ISSET(0, &set))
                {
                        if (fgets(line, sizeof line, stdin))
                                command(line);
                        else
                                stdin_open = 0;
                }
        }
        XCloseDisplay(display);
        return 0;
}
