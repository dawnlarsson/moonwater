/*
        The X side of the stress bench (test/wayland_stress/run.sh): an X client of an Xwayland, which is the path Steam and
        its overlay take on a Wayland desktop.

            x-stress -m anim [-t SECONDS] [-w W] [-h H]
            x-stress -m overlay [-t SECONDS] [-w W] [-h H]

        anim draws a frame into one of two pixmaps and shows it with the Present extension (PresentPixmap, what Mesa's
        DRI3 and Present loader does for a vsynced GL swap), the next frame at the CompleteNotify of the last, and says
        the frame times by Present's own clock (ust, CLOCK_MONOTONIC microseconds) and the commit to complete.
        overlay is the Steam overlay's case: a GL window (GLX, swap interval 1) of a solid colour, and over it an
        override-redirect window on a 32-bit ARGB visual whose left half is transparent (0x00000000) and right half white
        at half alpha, premultiplied (0x80808080). It prints "OVERLAY x y w h" and, once the GL window has drawn,
        "GROUND r g b"; it maps the overlay at a line on its standard input and keeps both on the screen for SECONDS, so the
        runner can read the screen before and after: under the left half the screen must not change, under the right half
        it must go halfway to white, and neither may go black.
        STRESS_T0 (ns of CLOCK_MONOTONIC, set before the exec) gives the launch to the first frame. One "RESULT key=value"
        line at the end. Exit 0, 2 when it cannot start, 3 when the mode cannot run here (said in a NOTE).
*/
#define _GNU_SOURCE
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <sys/wait.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/present.h>
#include <xcb/xcb.h>
#ifdef HAVE_GLX
#include <GL/gl.h>
#include <GL/glx.h>
#endif

static uint64_t now_ns(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int cmp(const void *a, const void *b)
{
        double x = *(const double *)a, y = *(const double *)b;
        return x < y ? -1 : x > y;
}

static double pct(double *v, int n, double p)
{
        if (!n)
                return -1;
        qsort(v, (size_t)n, sizeof(double), cmp);
        int i = (int)ceil(p / 100.0 * n) - 1;
        return v[i < 0 ? 0 : i >= n ? n - 1 : i];
}

static double intervals[100000], c2p[100000];

static int run_anim(Display *dpy, double seconds, int w, int h, uint64_t t_exec, uint64_t t_open)
{
        xcb_connection_t *c = XGetXCBConnection(dpy);
        const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_present_id);
        if (!ext || !ext->present)
        {
                printf("NOTE no Present extension: x anim skipped\n");
                return 3;
        }
        free(xcb_present_query_version_reply(c, xcb_present_query_version(c, 1, 2), NULL));
        int screen = DefaultScreen(dpy);
        Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, (unsigned)w, (unsigned)h, 0, 0, 0);
        XStoreName(dpy, win, "x-stress");
        XSelectInput(dpy, win, StructureNotifyMask);
        XMapWindow(dpy, win);
        for (;;)
        {
                XEvent e;
                XNextEvent(dpy, &e);
                if (e.type == MapNotify)
                        break;
        }
        uint64_t t_mapped = now_ns();
        xcb_present_event_t eid = xcb_generate_id(c);
        xcb_special_event_t *q = xcb_register_for_special_xge(c, &xcb_present_id, eid, NULL);
        xcb_present_select_input(c, eid, (xcb_window_t)win, XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);
        int depth = DefaultDepth(dpy, screen);
        Pixmap pix[3];
        int idle[3] = {1, 1, 1};
        XImage *img[3];
        GC gc = XCreateGC(dpy, win, 0, NULL);
        for (int i = 0; i < 3; i++)
        {
                pix[i] = XCreatePixmap(dpy, win, (unsigned)w, (unsigned)h, (unsigned)depth);
                char *data = malloc((size_t)w * (size_t)h * 4);
                img[i] = XCreateImage(dpy, DefaultVisual(dpy, screen), (unsigned)depth, ZPixmap, 0, data, (unsigned)w, (unsigned)h, 32, 0);
        }
        uint64_t t_start = now_ns(), t_end = t_start + (uint64_t)(seconds * 1e9), warm = t_start + 500000000ull;
        uint64_t last_ust = 0, first_complete = 0, sent_at[64] = {0}, last_msc = 0;
        uint32_t serial = 0;
        int n_int = 0, n_c2p = 0, inflight = 0;
        long frames = 0, msc_skips = 0, stalls = 0, completes = 0;
        unsigned frame = 0;
        double period = 1000.0 / 60;
        while (now_ns() < t_end)
        {
                if (!inflight)
                {
                        int which = idle[0] ? 0 : idle[1] ? 1 : idle[2] ? 2 : -1;
                        if (which >= 0)
                        {
                                uint32_t *px = (uint32_t *)img[which]->data;
                                uint32_t ground = ((frame * 3u) & 0xff) << 16 | 0x3060u;
                                int bar = (int)((frame * 8u) % (unsigned)w);
                                for (int y = 0; y < h; y++)
                                {
                                        for (int x = 0; x < w; x++)
                                                px[y * w + x] = ground;
                                        for (int x = bar; x < bar + 24 && x < w; x++)
                                                px[y * w + x] = 0xffffff;
                                }
                                XPutImage(dpy, pix[which], gc, img[which], 0, 0, 0, 0, (unsigned)w, (unsigned)h);
                                XFlush(dpy);
                                idle[which] = 0;
                                serial++;
                                sent_at[serial % 64] = now_ns();
                                xcb_present_pixmap(c, (xcb_window_t)win, (xcb_pixmap_t)pix[which], serial, 0, 0, 0, 0, 0, 0, 0,
                                                   XCB_PRESENT_OPTION_NONE, last_msc + 1, 0, 0, 0, NULL);
                                xcb_flush(c);
                                inflight = 1, frame++;
                        }
                        else
                                stalls++;
                }
                xcb_generic_event_t *ev = xcb_poll_for_special_event(c, q);
                if (!ev)
                {
                        struct timespec ts = {0, 200000};
                        nanosleep(&ts, NULL);
                        continue;
                }
                xcb_present_generic_event_t *ge = (xcb_present_generic_event_t *)ev;
                if (ge->evtype == XCB_PRESENT_COMPLETE_NOTIFY)
                {
                        xcb_present_complete_notify_event_t *ce = (xcb_present_complete_notify_event_t *)ev;
                        uint64_t t = now_ns();
                        uint64_t ust = ce->ust * 1000ull; // microseconds of CLOCK_MONOTONIC in Xwayland
                        completes++;
                        if (!first_complete)
                                first_complete = t;
                        if (ce->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP && ce->serial == serial)
                                inflight = 0;
                        if (t >= warm)
                        {
                                if (last_ust && n_int < 100000)
                                        intervals[n_int++] = (double)(ust - last_ust) / 1e6;
                                if (n_c2p < 100000 && sent_at[ce->serial % 64])
                                        c2p[n_c2p++] = (double)((int64_t)ust - (int64_t)sent_at[ce->serial % 64]) / 1e6;
                                if (last_msc && ce->msc > last_msc + 1)
                                        msc_skips += (long)(ce->msc - last_msc - 1);
                                frames++;
                        }
                        last_ust = ust, last_msc = ce->msc;
                }
                else if (ge->evtype == XCB_PRESENT_EVENT_IDLE_NOTIFY)
                {
                        xcb_present_idle_notify_event_t *ie = (xcb_present_idle_notify_event_t *)ev;
                        for (int i = 0; i < 3; i++)
                                if (pix[i] == ie->pixmap)
                                        idle[i] = 1;
                }
                free(ev);
        }
        double p50 = pct(intervals, n_int, 50), p99 = pct(intervals, n_int, 99);
        long dropped = 0;
        for (int i = 0; i < n_int; i++)
                if (intervals[i] > period * 1.5)
                        dropped += (long)floor(intervals[i] / period + 0.5) - 1;
        long late = 0;
        for (int i = 0; i < n_c2p; i++)
                if (c2p[i] > period + 1.0)
                        late++;
        const char *t0 = getenv("STRESS_T0");
        uint64_t launch = t0 ? strtoull(t0, NULL, 10) : 0;
        printf("RESULT mode=xanim size=%dx%d frames=%ld fps=%.2f frame_p50=%.3f frame_p99=%.3f dropped=%ld late=%ld msc_skips=%ld "
               "stalls=%ld c2p_p50=%.3f c2p_p99=%.3f open_ms=%.3f map_ms=%.3f first_complete_ms=%.3f launch_to_first_ms=%.3f\n",
               w, h, frames, p50 > 0 ? 1000.0 / p50 : 0, p50, p99, dropped, late, msc_skips, stalls, pct(c2p, n_c2p, 50), pct(c2p, n_c2p, 99),
               (double)(t_open - t_exec) / 1e6, (double)(t_mapped - t_exec) / 1e6, first_complete ? (double)(first_complete - t_exec) / 1e6 : -1.0,
               launch && first_complete ? (double)(first_complete - launch) / 1e6 : -1.0);
        return 0;
}

/* The Steam overlay's case: a GL window and an ARGB override-redirect window over it. */
static int run_overlay(Display *dpy, double seconds, int w, int h)
{
        int screen = DefaultScreen(dpy);
        Window root = RootWindow(dpy, screen);
        const unsigned gr = 0x20, gg = 0x60, gb = 0xc0;
        Window base;
        int gl = 0;
#ifdef HAVE_GLX
        int attrs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
        XVisualInfo *vi = glXChooseVisual(dpy, screen, attrs);
        GLXContext ctx = NULL;
        if (vi)
        {
                XSetWindowAttributes swa = {0};
                swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
                swa.event_mask = StructureNotifyMask;
                base = XCreateWindow(dpy, root, 0, 0, (unsigned)w, (unsigned)h, 0, vi->depth, InputOutput, vi->visual,
                                     CWColormap | CWEventMask, &swa);
                ctx = glXCreateContext(dpy, vi, NULL, True);
                gl = ctx != NULL;
        }
        if (!gl)
#endif
        {
                printf("NOTE no GLX here: the window under the overlay is a plain X window\n");
                base = XCreateSimpleWindow(dpy, root, 0, 0, (unsigned)w, (unsigned)h, 0, 0, gr << 16 | gg << 8 | gb);
                XSelectInput(dpy, base, StructureNotifyMask);
        }
        XStoreName(dpy, base, "x-stress-gl");
        XMapWindow(dpy, base);
        for (;;)
        {
                XEvent e;
                XNextEvent(dpy, &e);
                if (e.type == MapNotify)
                        break;
        }
        // Where the window manager put the GL window, in the root's coordinates: the overlay goes 100, 100 into it.
        int bx = 0, by = 0;
        Window child;
        XTranslateCoordinates(dpy, base, root, 0, 0, &bx, &by, &child);
        XVisualInfo tmpl = {0}, *argb = NULL;
        int nvi = 0;
        tmpl.screen = screen, tmpl.depth = 32, tmpl.class = TrueColor;
        XVisualInfo *list = XGetVisualInfo(dpy, VisualScreenMask | VisualDepthMask | VisualClassMask, &tmpl, &nvi);
        for (int i = 0; i < nvi; i++)
                if (list[i].red_mask == 0xff0000 && list[i].green_mask == 0xff00 && list[i].blue_mask == 0xff)
                        argb = &list[i];
        if (!argb)
        {
                printf("NOTE no 32-bit ARGB visual: overlay skipped\n");
                return 3;
        }
        const int ow = 200, oh = 120, ox = bx + 100, oy = by + 100;
        XSetWindowAttributes oa = {0};
        oa.override_redirect = True;
        oa.colormap = XCreateColormap(dpy, root, argb->visual, AllocNone);
        oa.border_pixel = 0, oa.background_pixel = 0;
        oa.event_mask = StructureNotifyMask | ExposureMask;
        Window over = XCreateWindow(dpy, root, ox, oy, ow, oh, 0, 32, InputOutput, argb->visual,
                                    CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &oa);
        printf("OVERLAY %d %d %d %d\n", ox, oy, ow, oh);
#ifdef HAVE_GLX
        // The GL window drawn a while before the overlay comes, so the screen can be read without it first.
        for (int i = 0; gl && i < 30; i++)
        {
                glXMakeCurrent(dpy, base, ctx);
                glClearColor(gr / 255.0f, gg / 255.0f, gb / 255.0f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glXSwapBuffers(dpy, base);
                usleep(16000);
        }
#endif
        printf("GROUND %u %u %u\n", gr, gg, gb);
        fflush(stdout);
        // The runner reads the screen, then says go (a line on standard input), and the overlay is mapped.
        char go[16];
        if (!fgets(go, sizeof(go), stdin))
                return 0;
        XMapRaised(dpy, over);
        GC ogc = XCreateGC(dpy, over, 0, NULL);
        uint32_t *px = malloc((size_t)ow * oh * 4);
        for (int y = 0; y < oh; y++)
                for (int x = 0; x < ow; x++)
                        px[y * ow + x] = x < ow / 2 ? 0x00000000u : 0x80808080u;
        XImage *img = XCreateImage(dpy, argb->visual, 32, ZPixmap, 0, (char *)px, ow, oh, 32, 0);
        fflush(stdout);
        uint64_t t_end = now_ns() + (uint64_t)(seconds * 1e9);
        long swaps = 0;
        while (now_ns() < t_end)
        {
                while (XPending(dpy))
                {
                        XEvent e;
                        XNextEvent(dpy, &e);
                }
                XPutImage(dpy, over, ogc, img, 0, 0, 0, 0, ow, oh);
#ifdef HAVE_GLX
                if (gl)
                {
                        glXMakeCurrent(dpy, base, ctx);
                        glClearColor(gr / 255.0f, gg / 255.0f, gb / 255.0f, 1);
                        glClear(GL_COLOR_BUFFER_BIT);
                        glXSwapBuffers(dpy, base);
                        swaps++;
                }
#endif
                XFlush(dpy);
                usleep(16000);
        }
        printf("RESULT mode=overlay gl=%d swaps=%ld\n", gl, swaps);
        return 0;
}

/* ---- the X edge cases: one CASE line each, as wl-edge's (test/wayland_stress/edge.c) ---- */
static const char *x_case = "?";
static uint64_t x_t0;

static int x_io_error(Display *d)
{
        (void)d;
        printf("CASE %s outcome=closed ms=%.3f detail=the_X_connection_was_lost\n", x_case, (double)(now_ns() - x_t0) / 1e6);
        fflush(stdout);
        _exit(0);
        return 0;
}

static int x_error(Display *d, XErrorEvent *e)
{
        (void)d, (void)e;
        return 0;
}

static Window x_window(Display *dpy, int x, int y, int w, int h, unsigned long colour, int override)
{
        XSetWindowAttributes a = {0};
        a.override_redirect = override ? True : False;
        a.background_pixel = colour;
        Window win = XCreateWindow(dpy, DefaultRootWindow(dpy), x, y, (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput, CopyFromParent,
                                   CWOverrideRedirect | CWBackPixel, &a);
        XMapWindow(dpy, win);
        return win;
}

static int run_xcase(Display *dpy, const char *name)
{
        char detail[160] = "-";
        x_case = name, x_t0 = now_ns();
        XSetIOErrorHandler(x_io_error);
        XSetErrorHandler(x_error);
        if (!strcmp(name, "x-many-windows"))
        {
                // 120 windows mapped at once: Xwayland makes a surface (and buffers) for each, all as one Wayland client.
                Window w[120];
                for (int i = 0; i < 120; i++)
                        w[i] = x_window(dpy, (i % 12) * 70, (i / 12) * 50, 64, 44, 0x100000u * (unsigned)(i % 16) + 0x3040u, 0);
                XSync(dpy, False);
                usleep(1500000);
                for (int i = 0; i < 120; i++)
                        XClearWindow(dpy, w[i]);
                XSync(dpy, False);
                usleep(500000);
                snprintf(detail, sizeof(detail), "windows=120");
        }
        else if (!strcmp(name, "x-windows-2000"))
        {
                // Two thousand windows mapped at once, past the window manager's old table of 64 and the desktop's 32 rows: the server and
                // its window manager keep going, and the last one mapped is answered.
                static Window w[2000];
                for (int i = 0; i < 2000; i++)
                {
                        w[i] = x_window(dpy, (i % 40) * 20, (i / 40) * 12, 18, 10, 0x010101u * (unsigned)(i % 200) + 0x2000u, 0);
                        if ((i & 255) == 255)
                                XSync(dpy, False);
                }
                XSync(dpy, False);
                usleep(2000000);
                XClearWindow(dpy, w[1999]);
                XSync(dpy, False);
                snprintf(detail, sizeof(detail), "windows=2000");
        }
        else if (!strcmp(name, "x-override-stacking"))
        {
                Window w[6];
                for (int i = 0; i < 6; i++)
                        w[i] = x_window(dpy, 100 + i * 30, 100 + i * 20, 200, 150, 0x203040u * (unsigned)(i + 1), 1);
                for (int i = 0; i < 300; i++)
                {
                        XRaiseWindow(dpy, w[i % 6]);
                        if (i % 7 == 0)
                                XLowerWindow(dpy, w[(i / 7) % 6]);
                        XFlush(dpy);
                        usleep(2000);
                }
                XSync(dpy, False);
        }
        else if (!strcmp(name, "x-grab-then-killed"))
        {
                pid_t pid = fork();
                if (pid == 0)
                {
                        Display *d2 = XOpenDisplay(NULL);
                        if (!d2)
                                _exit(1);
                        Window g = x_window(d2, 50, 50, 300, 200, 0x804020u, 0);
                        XSync(d2, False);
                        usleep(300000);
                        XGrabPointer(d2, g, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
                        XGrabKeyboard(d2, g, False, GrabModeAsync, GrabModeAsync, CurrentTime);
                        XSync(d2, False);
                        kill(getpid(), SIGKILL);
                }
                waitpid(pid, NULL, 0);
                // A window of this client after the grabber is gone: shown, and it can take the keyboard.
                Window w = x_window(dpy, 80, 80, 300, 200, 0x208040u, 0);
                XSync(dpy, False);
                usleep(300000);
                XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
                XSync(dpy, False);
                Window focus;
                int revert;
                XGetInputFocus(dpy, &focus, &revert);
                snprintf(detail, sizeof(detail), "focus_is_new_window=%d", focus == w);
        }
        else if (!strcmp(name, "x-property-flood"))
        {
                Window w = x_window(dpy, 60, 60, 300, 200, 0x404080u, 0);
                Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", False), utf8 = XInternAtom(dpy, "UTF8_STRING", False);
                Atom state = XInternAtom(dpy, "_NET_WM_STATE", False), above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
                XSync(dpy, False);
                char title[64];
                for (int i = 0; i < 20000; i++)
                {
                        int n = snprintf(title, sizeof(title), "title %d", i);
                        XStoreName(dpy, w, title);
                        XChangeProperty(dpy, w, net_name, utf8, 8, PropModeReplace, (unsigned char *)title, n);
                        XChangeProperty(dpy, w, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&above, i & 1);
                        XClassHint hint = {"x-stress", "XStress"};
                        XSetClassHint(dpy, w, &hint);
                        if ((i & 1023) == 1023)
                                XFlush(dpy);
                }
                uint64_t t = now_ns();
                XSync(dpy, False);
                snprintf(detail, sizeof(detail), "sync_after_ms=%.1f", (double)(now_ns() - t) / 1e6);
        }
        else if (!strcmp(name, "x-transient-chain"))
        {
                Window w[5];
                for (int i = 0; i < 5; i++)
                {
                        w[i] = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 40 + i * 40, 40 + i * 30, 300, 200, 0, 0, 0x205080u + (unsigned)i * 0x101010u);
                        if (i)
                                XSetTransientForHint(dpy, w[i], w[i - 1]);
                        XMapWindow(dpy, w[i]);
                }
                XSetTransientForHint(dpy, w[0], w[4]); // a cycle
                XSync(dpy, False);
                usleep(500000);
                XUnmapWindow(dpy, w[2]); // the middle of the chain goes
                XDestroyWindow(dpy, w[1]);
                XSync(dpy, False);
                usleep(300000);
        }
        else if (!strcmp(name, "x-selection-race"))
        {
                Window w = x_window(dpy, 60, 60, 200, 100, 0x808040u, 0);
                Atom clip = XInternAtom(dpy, "CLIPBOARD", False), target = XInternAtom(dpy, "UTF8_STRING", False), prop = XInternAtom(dpy, "XSTRESS", False);
                XSync(dpy, False);
                int owned = 0;
                for (int i = 0; i < 500; i++)
                {
                        XSetSelectionOwner(dpy, clip, (i & 1) ? w : None, CurrentTime);
                        XConvertSelection(dpy, clip, target, prop, w, CurrentTime);
                        owned += XGetSelectionOwner(dpy, clip) == w;
                        while (XPending(dpy))
                        {
                                XEvent e;
                                XNextEvent(dpy, &e);
                        }
                }
                XSync(dpy, False);
                snprintf(detail, sizeof(detail), "owned=%d_of_500", owned);
        }
        else if (!strcmp(name, "x-argb-overlay-toggle"))
        {
                XVisualInfo tmpl = {0}, *list;
                int n = 0;
                tmpl.screen = DefaultScreen(dpy), tmpl.depth = 32, tmpl.class = TrueColor;
                list = XGetVisualInfo(dpy, VisualScreenMask | VisualDepthMask | VisualClassMask, &tmpl, &n);
                if (!n)
                {
                        printf("CASE %s outcome=skip ms=-1 detail=no_argb_visual\n", name);
                        return 0;
                }
                XSetWindowAttributes a = {0};
                a.override_redirect = True;
                a.colormap = XCreateColormap(dpy, DefaultRootWindow(dpy), list[0].visual, AllocNone);
                Window o = XCreateWindow(dpy, DefaultRootWindow(dpy), 120, 120, 200, 120, 0, 32, InputOutput, list[0].visual,
                                         CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixel, &a);
                GC gc = XCreateGC(dpy, o, 0, NULL);
                uint32_t *px = malloc(200 * 120 * 4);
                for (int i = 0; i < 200 * 120; i++)
                        px[i] = (i % 200) < 100 ? 0 : 0x80808080u;
                XImage *img = XCreateImage(dpy, list[0].visual, 32, ZPixmap, 0, (char *)px, 200, 120, 32, 0);
                for (int i = 0; i < 200; i++)
                {
                        XMapRaised(dpy, o);
                        XPutImage(dpy, o, gc, img, 0, 0, 0, 0, 200, 120);
                        XFlush(dpy);
                        usleep(4000);
                        XUnmapWindow(dpy, o);
                        XFlush(dpy);
                        usleep(4000);
                }
                XSync(dpy, False);
        }
        else
        {
                fprintf(stderr, "no X case %s\n", name);
                return 2;
        }
        // The connection still answers.
        XSync(dpy, False);
        printf("CASE %s outcome=ok ms=%.3f detail=%s\n", name, (double)(now_ns() - x_t0) / 1e6, detail);
        return 0;
}

int main(int argc, char **argv)
{
        uint64_t t_exec = now_ns();
        const char *mode = "anim";
        double seconds = 3;
        int w = 1280, h = 720, c;
        while ((c = getopt(argc, argv, "m:t:w:h:")) != -1)
                switch (c)
                {
                case 'm': mode = optarg; break;
                case 't': seconds = atof(optarg); break;
                case 'w': w = atoi(optarg); break;
                case 'h': h = atoi(optarg); break;
                default: fprintf(stderr, "usage: x-stress -m anim|overlay [-t S] [-w W] [-h H]\n"); return 2;
                }
        Display *dpy = NULL;
        // An Xwayland started on demand comes up while this waits: the time to the first display that answers is the server's start.
        for (int i = 0; i < 1000 && !(dpy = XOpenDisplay(NULL)); i++)
                usleep(10000);
        if (!dpy)
        {
                fprintf(stderr, "cannot open the display\n");
                return 2;
        }
        uint64_t t_open = now_ns();
        if (!strcmp(mode, "anim"))
                return run_anim(dpy, seconds, w, h, t_exec, t_open);
        if (!strcmp(mode, "overlay"))
                return run_overlay(dpy, seconds, w, h);
        if (!strncmp(mode, "x-", 2))
                return run_xcase(dpy, mode);
        return 2;
}
