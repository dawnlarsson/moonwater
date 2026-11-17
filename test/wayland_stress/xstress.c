/*
        The X side of the stress bench (test/wayland_stress/run.sh): an X client of an Xwayland, which is the path Steam and
        its overlay take on a Wayland desktop.

            x-stress -m anim [-t SECONDS] [-w W] [-h H]
            x-stress -m overlay [-t SECONDS] [-w W] [-h H]
            x-stress -m x-CASE (the X edge cases; x-dnd needs STRESS_FIFO, the compositor's -i device)
            x-stress -m x-clip-own | x-clip-paste | x-dnd-to-wl | x-dnd-from-wl (the X side of the bridge's rows, run.py's
                     x_bridge: STRESS_FIFO, STRESS_BYTES the value's size, STRESS_DIE_AFTER a piece to die at, STRESS_HOLD seconds)

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
#include <fcntl.h>
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

/*
        x-dnd: XDND (version 5) between two X windows of this client, through the compositor's pointer: the hand is put on the source (A) by
        the motions STRESS_FIFO is written (an evdev pair of the compositor's -i device, as wl-edge writes), the button pressed, moved
        right over the target (B, XdndAware) and let go. The source owns XdndSelection and sends XdndEnter, XdndPosition and XdndDrop to
        whatever X window with XdndAware is under the pointer as the X server has it; the target answers XdndStatus, converts the
        selection at the drop and says XdndFinished. The detail says how far it went: the hand on A, the X window under it at the end of
        the motion (B, root, other), entered, accepted, dropped, the text read, finished.
*/
static int x_fifo = -1;
static void x_inject(int type, int code, int value)
{
        struct
        {
                int64_t sec, usec;
                uint16_t type, code;
                int32_t value;
        } e[2];
        uint64_t t = now_ns();
        memset(e, 0, sizeof(e));
        e[0].sec = (int64_t)(t / 1000000000ull), e[0].usec = (int64_t)(t % 1000000000ull / 1000);
        e[0].type = (uint16_t)type, e[0].code = (uint16_t)code, e[0].value = value;
        e[1].sec = e[0].sec, e[1].usec = e[0].usec;
        if (x_fifo >= 0 && write(x_fifo, e, sizeof(e)) < 0)
                return;
}

static void x_client_message(Display *dpy, Window to, Atom type, long l0, long l1, long l2, long l3, long l4)
{
        XEvent e = {0};
        e.xclient.type = ClientMessage, e.xclient.window = to, e.xclient.message_type = type, e.xclient.format = 32;
        e.xclient.data.l[0] = l0, e.xclient.data.l[1] = l1, e.xclient.data.l[2] = l2, e.xclient.data.l[3] = l3, e.xclient.data.l[4] = l4;
        XSendEvent(dpy, to, False, NoEventMask, &e);
}

static int x_dnd(Display *dpy, char *detail, size_t room)
{
        const char *fifo = getenv("STRESS_FIFO");
        Atom aware = XInternAtom(dpy, "XdndAware", False), enter = XInternAtom(dpy, "XdndEnter", False), position = XInternAtom(dpy, "XdndPosition", False);
        Atom status = XInternAtom(dpy, "XdndStatus", False), leave = XInternAtom(dpy, "XdndLeave", False), drop = XInternAtom(dpy, "XdndDrop", False);
        Atom finished = XInternAtom(dpy, "XdndFinished", False), selection = XInternAtom(dpy, "XdndSelection", False);
        Atom copy = XInternAtom(dpy, "XdndActionCopy", False), utf8 = XInternAtom(dpy, "UTF8_STRING", False), prop = XInternAtom(dpy, "XSTRESS_DND", False);
        Window root = DefaultRootWindow(dpy), a, b, over = None, rr, child;
        long version = 5;
        int ax, ay, bx, by, rx = 0, ry = 0, wx, wy, on_a = 0, entered = 0, accepted = 0, dropped = 0, done = 0, text_ok = 0;
        unsigned int mask;

        if (!fifo || (x_fifo = open(fifo, O_WRONLY | O_NONBLOCK)) < 0)
        {
                snprintf(detail, room, "no_STRESS_FIFO");
                return 3;
        }
        // Override-redirect, so each is where it was made: the window manager places a normal window as it likes (both at one place here).
        a = x_window(dpy, 100, 200, 300, 200, 0x406080u, 1);
        b = x_window(dpy, 520, 200, 300, 200, 0x608040u, 1);
        XSelectInput(dpy, a, ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
        XSelectInput(dpy, b, PointerMotionMask);
        XChangeProperty(dpy, b, aware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);
        XSync(dpy, False);
        usleep(600000);
        XTranslateCoordinates(dpy, a, root, 0, 0, &ax, &ay, &child);
        XTranslateCoordinates(dpy, b, root, 0, 0, &bx, &by, &child);
        // The hand to the top left, then along the diagonal until the X server has it over A.
        for (int i = 0; i < 40; i++)
                x_inject(2, 0, -100), x_inject(2, 1, -100);
        for (int i = 0; i < 400 && !on_a; i++)
        {
                x_inject(2, 0, 6), x_inject(2, 1, 6);
                usleep(4000);
                XQueryPointer(dpy, root, &rr, &child, &rx, &ry, &wx, &wy, &mask);
                on_a = child == a && rx > ax + 20 && ry > ay + 20;
        }
        if (!on_a)
        {
                snprintf(detail, room, "the_hand_never_reached_the_source_(a_at_%d,%d_b_at_%d,%d_pointer_%d,%d_under_%s)", ax, ay, bx, by, rx, ry,
                         child == b ? "B" : child == None ? "none" : "other");
                return 0;
        }
        x_inject(1, 272, 1);
        usleep(60000);
        XSetSelectionOwner(dpy, selection, a, CurrentTime);
        for (int i = 0; i < 200 && !(accepted && over == b && rx > bx + 40); i++)
        {
                x_inject(2, 0, 6);
                usleep(5000);
                XQueryPointer(dpy, root, &rr, &child, &rx, &ry, &wx, &wy, &mask);
                if (child != over)
                {
                        if (over != None && entered)
                                x_client_message(dpy, over, leave, (long)a, 0, 0, 0, 0), entered = 0, accepted = 0;
                        over = child;
                        Atom type;
                        int format;
                        unsigned long count, after;
                        unsigned char *data = NULL;
                        if (over != None && XGetWindowProperty(dpy, over, aware, 0, 1, False, XA_ATOM, &type, &format, &count, &after, &data) == Success &&
                            count == 1)
                                x_client_message(dpy, over, enter, (long)a, 5L << 24, (long)utf8, 0, 0), entered = 1;
                        if (data)
                                XFree(data);
                }
                if (entered)
                        x_client_message(dpy, over, position, (long)a, 0, (long)rx << 16 | ry, CurrentTime, (long)copy);
                XFlush(dpy);
                while (XPending(dpy))
                {
                        XEvent e;
                        XNextEvent(dpy, &e);
                        if (e.type == ClientMessage && e.xclient.window == b && (Atom)e.xclient.message_type == position)
                                x_client_message(dpy, (Window)e.xclient.data.l[0], status, (long)b, 1, 0, 0, (long)copy);
                        else if (e.type == ClientMessage && e.xclient.window == a && (Atom)e.xclient.message_type == status)
                                accepted = e.xclient.data.l[1] & 1;
                }
        }
        x_inject(1, 272, 0);
        if (entered && accepted)
                x_client_message(dpy, over, drop, (long)a, 0, CurrentTime, 0, 0), dropped = 1;
        else if (entered)
                x_client_message(dpy, over, leave, (long)a, 0, 0, 0, 0);
        XFlush(dpy);
        for (uint64_t end = now_ns() + 2000000000ull; dropped && !done && now_ns() < end;)
        {
                if (!XPending(dpy))
                {
                        usleep(2000);
                        continue;
                }
                XEvent e;
                XNextEvent(dpy, &e);
                if (e.type == ClientMessage && e.xclient.window == b && (Atom)e.xclient.message_type == drop)
                        XConvertSelection(dpy, selection, utf8, prop, b, CurrentTime);
                else if (e.type == SelectionRequest)
                {
                        XSelectionEvent n = {0};
                        static const char text[] = "dragged text";
                        XChangeProperty(dpy, e.xselectionrequest.requestor, e.xselectionrequest.property, utf8, 8, PropModeReplace, (const unsigned char *)text,
                                        (int)strlen(text));
                        n.type = SelectionNotify, n.requestor = e.xselectionrequest.requestor, n.selection = e.xselectionrequest.selection;
                        n.target = e.xselectionrequest.target, n.property = e.xselectionrequest.property, n.time = e.xselectionrequest.time;
                        XSendEvent(dpy, n.requestor, False, NoEventMask, (XEvent *)&n);
                }
                else if (e.type == SelectionNotify && e.xselection.requestor == b)
                {
                        Atom type;
                        int format;
                        unsigned long count, after;
                        unsigned char *data = NULL;
                        if (e.xselection.property != None &&
                            XGetWindowProperty(dpy, b, prop, 0, 64, True, AnyPropertyType, &type, &format, &count, &after, &data) == Success && data)
                                text_ok = count == 12 && !memcmp(data, "dragged text", 12);
                        if (data)
                                XFree(data);
                        x_client_message(dpy, (Window)a, finished, (long)b, 1, (long)copy, 0, 0);
                }
                else if (e.type == ClientMessage && e.xclient.window == a && (Atom)e.xclient.message_type == finished)
                        done = 1;
                else if (e.type == ClientMessage && e.xclient.window == b && (Atom)e.xclient.message_type == position)
                        x_client_message(dpy, (Window)e.xclient.data.l[0], status, (long)b, 1, 0, 0, (long)copy);
                XFlush(dpy);
        }
        snprintf(detail, room, "a_at=%d,%d_b_at=%d,%d_pointer=%d,%d_under=%s_entered=%d_accepted=%d_dropped=%d_text=%d_finished=%d", ax, ay, bx, by, rx, ry,
                 over == b ? "B" : over == a ? "A" : over == None ? "none" : "other", entered, accepted, dropped, text_ok, done);
        return text_ok && done ? 1 : 0;
}

/*
        ---- the X side of the bridge between X and Wayland (run.py's x_bridge) ----

        Each value is STRESS_BYTES bytes of 'a' + i % 26, as wl-stress's are, so either side checks every byte. A value longer than a
        piece (64 KiB here) goes by INCR (ICCCM 2.7.2) whichever side sends it, and STRESS_DIE_AFTER kills this client (SIGKILL) once it
        has sent or read that many pieces: the other side then has a transfer whose peer is gone in the middle.
            x-clip-own     a window of this client is given the person's key (the compositor's -i device), then owns CLIPBOARD and answers
                           TARGETS, UTF8_STRING and STRING; "SERVED n" on each whole answer; it holds for STRESS_HOLD seconds
            x-clip-paste   the key, then CLIPBOARD converted as UTF8_STRING, INCR read, every byte checked
            x-dnd-to-wl    an XDND 5 source: the hand on its window, pressed, moved right until a window with XdndAware that is not its
                           own is under the pointer (a Wayland window's, through the window manager's proxy), entered, positioned,
                           accepted, dropped, its XdndSelection converted (INCR), finished
            x-dnd-from-wl  an XDND 5 target over the right half of the screen: "READY x y w h", then whatever comes (entered,
                           positions, a drop converted as UTF8_STRING and checked, or a leave), for STRESS_HOLD seconds
*/
static long xb_bytes(void)
{
        const char *b = getenv("STRESS_BYTES");
        return b ? atol(b) : 4096;
}

static int xb_die_after(void)
{
        const char *d = getenv("STRESS_DIE_AFTER");
        return d ? atoi(d) : 0;
}

static double xb_hold(void)
{
        const char *h = getenv("STRESS_HOLD");
        return h ? atof(h) : 8.0;
}

static void xb_fill(unsigned char *p, long at, long n)
{
        for (long i = 0; i < n; i++)
                p[i] = (unsigned char)('a' + (at + i) % 26);
}

// How many of n bytes at offset at are not the pattern.
static long xb_wrong(const unsigned char *p, long at, long n)
{
        long bad = 0;
        for (long i = 0; i < n; i++)
                bad += p[i] != (unsigned char)('a' + (at + i) % 26);
        return bad;
}

static int xb_next(Display *dpy, XEvent *e, uint64_t until)
{
        while (!XPending(dpy))
        {
                if (now_ns() >= until)
                        return 0;
                usleep(1000);
        }
        XNextEvent(dpy, e);
        return 1;
}

// The window has the keys and has had a key of the person's (the compositor's -i device): what a copy or a paste under the hand asks.
static int xb_gesture(Display *dpy, Window w)
{
        uint64_t until = now_ns() + 3000000000ull;
        Window focus;
        int revert, pressed = 0;
        XEvent e;

        if (x_fifo < 0)
        {
                const char *fifo = getenv("STRESS_FIFO");
                if (!fifo || (x_fifo = open(fifo, O_WRONLY | O_NONBLOCK)) < 0)
                        return 0;
        }
        // A window an X client opens after its first does not take the keys (the compositor gives them to a window the person asked
        // for): the hand is put on it, along the diagonal from the top left until the X server has it there, and clicks.
        XGetInputFocus(dpy, &focus, &revert);
        if (focus != w)
        {
                Window rr, child = None;
                int rx, ry, wx, wy, over = 0;
                unsigned int mask;

                for (int i = 0; i < 40; i++)
                        x_inject(2, 0, -100), x_inject(2, 1, -100);
                for (int i = 0; i < 400 && !over; i++)
                {
                        x_inject(2, 0, 6), x_inject(2, 1, 6);
                        usleep(4000);
                        XQueryPointer(dpy, DefaultRootWindow(dpy), &rr, &child, &rx, &ry, &wx, &wy, &mask);
                        over = child == w;
                }
                for (int i = 0; i < 8; i++)
                        x_inject(2, 0, 6), x_inject(2, 1, 6);
                usleep(20000);
                x_inject(1, 272, 1);
                x_inject(1, 272, 0);
        }
        for (;;)
        {
                XGetInputFocus(dpy, &focus, &revert);
                if (focus == w || now_ns() >= until)
                        break;
                usleep(10000);
        }
        x_inject(1, 30, 1);
        x_inject(1, 30, 0);
        while (!pressed && xb_next(dpy, &e, until))
                pressed = e.type == KeyPress && e.xkey.window == w;
        return pressed;
}

// The target a bridge row's copy or paste is of (STRESS_TARGET: image/png alone, say), UTF8_STRING by default.
static Atom xb_target(Display *dpy)
{
        const char *t = getenv("STRESS_TARGET");
        return XInternAtom(dpy, t && *t ? t : "UTF8_STRING", False);
}

static int x_clip_own(Display *dpy, char *detail, size_t room)
{
        Atom clip = XInternAtom(dpy, "CLIPBOARD", False), targets = XInternAtom(dpy, "TARGETS", False), utf8 = xb_target(dpy);
        Atom incr = XInternAtom(dpy, "INCR", False), manager = XInternAtom(dpy, "CLIPBOARD_MANAGER", False);
        Atom save = XInternAtom(dpy, "SAVE_TARGETS", False), saved_into = XInternAtom(dpy, "XSTRESS_SAVED", False);
        int saving = getenv("STRESS_SAVE") != NULL, text = utf8 == XInternAtom(dpy, "UTF8_STRING", False);
        long total = xb_bytes(), chunk = 65536, served = 0, pieces = 0;
        int die = xb_die_after(), owned;
        Window w = x_window(dpy, 80, 80, 1600, 900, 0x305070u, 0), requestor = None;
        Atom property = None, type = None;
        long at = -1;
        unsigned char *data = malloc((size_t)chunk);
        uint64_t until;
        XEvent e;

        XSelectInput(dpy, w, KeyPressMask | FocusChangeMask | StructureNotifyMask);
        XSync(dpy, False);
        usleep(300000);
        if (!xb_gesture(dpy, w))
        {
                snprintf(detail, room, "no_key_reached_the_window");
                return 0;
        }
        XSetSelectionOwner(dpy, clip, w, CurrentTime);
        owned = XGetSelectionOwner(dpy, clip) == w;
        printf("OWNED %d\n", owned);
        fflush(stdout);
        until = now_ns() + (uint64_t)(xb_hold() * 1e9);
        // The owner about to quit asks the clipboard manager to keep its copy (freedesktop's SAVE_TARGETS), and quits once it is answered.
        if (saving)
        {
                usleep(500000);
                XConvertSelection(dpy, manager, save, saved_into, w, CurrentTime);
                XFlush(dpy);
        }
        while (xb_next(dpy, &e, until))
        {
                if (saving && e.type == SelectionNotify && e.xselection.selection == manager)
                {
                        printf("SAVED %d\n", e.xselection.property != None);
                        fflush(stdout);
                        snprintf(detail, room, "owned=%d_saved=%d", owned, e.xselection.property != None);
                        return e.xselection.property != None;
                }
                if (e.type == SelectionRequest)
                {
                        XSelectionRequestEvent *r = &e.xselectionrequest;
                        XSelectionEvent n = {0};
                        Atom prop = r->property ? r->property : r->target;

                        n.type = SelectionNotify, n.requestor = r->requestor, n.selection = r->selection, n.target = r->target, n.time = r->time;
                        n.property = None;
                        if (r->target == targets)
                        {
                                Atom list[3] = {targets, utf8, XA_STRING};
                                XChangeProperty(dpy, r->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)list, text ? 3 : 2);
                                n.property = prop;
                        }
                        else if ((r->target == utf8 || (text && r->target == XA_STRING)) && total <= chunk)
                        {
                                xb_fill(data, 0, total);
                                XChangeProperty(dpy, r->requestor, prop, r->target, 8, PropModeReplace, data, (int)total);
                                n.property = prop;
                                printf("SERVED %ld\n", total);
                                fflush(stdout);
                                served++;
                        }
                        else if ((r->target == utf8 || (text && r->target == XA_STRING)) && requestor == None)
                        {
                                long size = total;
                                XSelectInput(dpy, r->requestor, PropertyChangeMask);
                                XChangeProperty(dpy, r->requestor, prop, incr, 32, PropModeReplace, (unsigned char *)&size, 1);
                                n.property = prop;
                                requestor = r->requestor, property = prop, type = r->target, at = 0;
                        }
                        XSendEvent(dpy, r->requestor, False, NoEventMask, (XEvent *)&n);
                        XFlush(dpy);
                }
                else if (e.type == PropertyNotify && requestor != None && e.xproperty.window == requestor && e.xproperty.atom == property &&
                         e.xproperty.state == PropertyDelete)
                {
                        long n = total - at > chunk ? chunk : total - at;

                        if (die && pieces >= die)
                        {
                                printf("DYING after %ld pieces\n", pieces);
                                fflush(stdout);
                                kill(getpid(), SIGKILL);
                        }
                        xb_fill(data, at, n);
                        XChangeProperty(dpy, requestor, property, type, 8, PropModeReplace, data, (int)n);
                        XFlush(dpy);
                        pieces++;
                        at += n;
                        if (n == 0)
                        {
                                printf("SERVED %ld\n", total);
                                fflush(stdout);
                                served++;
                                requestor = None;
                        }
                }
                else if (e.type == SelectionClear)
                        break;
        }
        snprintf(detail, room, "owned=%d_served=%ld_bytes=%ld", owned, served, total);
        return served > 0;
}

/* CLIPBOARD (or the XdndSelection of a drop) converted as target into this window's property, read whole or by INCR, each byte checked:
   the bytes read, -1 for a refusal or a timeout; *wrong the bytes that were not the pattern. */
static long xb_convert(Display *dpy, Window w, Atom selection, Atom target, Time time, int die, double secs, long *wrong, double *ms)
{
        Atom prop = XInternAtom(dpy, "XSTRESS_BRIDGE", False), incr = XInternAtom(dpy, "INCR", False);
        uint64_t t0 = now_ns(), until = t0 + (uint64_t)(secs * 1e9);
        long total = 0, pieces = 0;
        int incremental = 0, notified = 0;
        XEvent e;

        *wrong = 0;
        XSelectInput(dpy, w, KeyPressMask | FocusChangeMask | StructureNotifyMask | PropertyChangeMask);
        XConvertSelection(dpy, selection, target, prop, w, time);
        XFlush(dpy);
        while (xb_next(dpy, &e, until))
        {
                Atom type;
                int format;
                unsigned long count, after;
                unsigned char *data = NULL;

                if (e.type == SelectionNotify && e.xselection.requestor == w && !notified)
                {
                        notified = 1;
                        if (e.xselection.property == None)
                                return -1;
                        if (XGetWindowProperty(dpy, w, prop, 0, 0x1fffffff, True, AnyPropertyType, &type, &format, &count, &after, &data) != Success)
                                return -1;
                        if (type == incr)
                                incremental = 1;
                        else
                        {
                                *wrong += xb_wrong(data, 0, (long)count);
                                total = (long)count;
                                XFree(data);
                                *ms = (double)(now_ns() - t0) / 1e6;
                                return total;
                        }
                        if (data)
                                XFree(data);
                }
                else if (e.type == PropertyNotify && incremental && e.xproperty.window == w && e.xproperty.atom == prop && e.xproperty.state == PropertyNewValue)
                {
                        if (XGetWindowProperty(dpy, w, prop, 0, 0x1fffffff, True, AnyPropertyType, &type, &format, &count, &after, &data) != Success)
                                return -1;
                        if (!count)
                        {
                                if (data)
                                        XFree(data);
                                *ms = (double)(now_ns() - t0) / 1e6;
                                return total;
                        }
                        *wrong += xb_wrong(data, total, (long)count);
                        total += (long)count;
                        XFree(data);
                        if (die && ++pieces >= die)
                        {
                                printf("DYING after %ld pieces\n", pieces);
                                fflush(stdout);
                                kill(getpid(), SIGKILL);
                        }
                }
        }
        *ms = (double)(now_ns() - t0) / 1e6;
        return incremental ? total : -1;
}

/* MULTIPLE (ICCCM): the target and TARGETS asked in one conversion, each into a property of its own: the target's bytes read (whole, as
   this asks for a value no larger than a property holds), and whether TARGETS named it. -1 for a refusal. */
static long xb_multiple(Display *dpy, Window w, Atom clip, Atom target, long *wrong, int *listed)
{
        Atom multiple = XInternAtom(dpy, "MULTIPLE", False), pair = XInternAtom(dpy, "ATOM_PAIR", False), targets = XInternAtom(dpy, "TARGETS", False);
        Atom into = XInternAtom(dpy, "XSTRESS_MULTIPLE", False), p1 = XInternAtom(dpy, "XSTRESS_ONE", False), p2 = XInternAtom(dpy, "XSTRESS_TWO", False);
        Atom pairs[4] = {target, p1, targets, p2}, type;
        uint64_t until = now_ns() + 10000000000ull;
        unsigned long count, after;
        unsigned char *data = NULL;
        int format;
        XEvent e;

        *wrong = 0, *listed = 0;
        XChangeProperty(dpy, w, into, pair, 32, PropModeReplace, (unsigned char *)pairs, 4);
        XConvertSelection(dpy, clip, multiple, into, w, CurrentTime);
        XFlush(dpy);
        while (xb_next(dpy, &e, until))
                if (e.type == SelectionNotify && e.xselection.requestor == w)
                {
                        long got = -1;

                        if (e.xselection.property == None)
                                return -1;
                        if (XGetWindowProperty(dpy, w, p2, 0, 1024, True, XA_ATOM, &type, &format, &count, &after, &data) == Success && data)
                        {
                                for (unsigned long k = 0; k < count; k++)
                                        *listed |= ((Atom *)data)[k] == target;
                                XFree(data);
                                data = NULL;
                        }
                        if (XGetWindowProperty(dpy, w, p1, 0, 0x1fffffff, True, AnyPropertyType, &type, &format, &count, &after, &data) == Success && data)
                        {
                                got = (long)count;
                                *wrong = xb_wrong(data, 0, got);
                                XFree(data);
                        }
                        return got;
                }
        return -1;
}

static int x_clip_paste(Display *dpy, char *detail, size_t room)
{
        Atom clip = XInternAtom(dpy, "CLIPBOARD", False), utf8 = xb_target(dpy);
        Window w = x_window(dpy, 80, 80, 1600, 900, 0x507030u, 0);
        long want = xb_bytes(), wrong = 0, got;
        double ms = -1;

        XSelectInput(dpy, w, KeyPressMask | FocusChangeMask | StructureNotifyMask | PropertyChangeMask);
        XSync(dpy, False);
        usleep(300000);
        if (!xb_gesture(dpy, w))
        {
                snprintf(detail, room, "no_key_reached_the_window");
                return 0;
        }
        if (getenv("STRESS_MULTIPLE"))
        {
                int listed = 0;

                got = xb_multiple(dpy, w, clip, utf8, &wrong, &listed);
                snprintf(detail, room, "multiple_bytes=%ld_of_%ld_wrong=%ld_targets_named_it=%d", got, want, wrong, listed);
                return got == want && !wrong && listed;
        }
        got = xb_convert(dpy, w, clip, utf8, CurrentTime, xb_die_after(), 10.0, &wrong, &ms);
        snprintf(detail, room, "bytes=%ld_of_%ld_wrong=%ld_paste_ms=%.1f", got, want, wrong, ms);
        return got == want && !wrong;
}

static int x_dnd_to_wl(Display *dpy, char *detail, size_t room)
{
        Atom aware = XInternAtom(dpy, "XdndAware", False), enter = XInternAtom(dpy, "XdndEnter", False), position = XInternAtom(dpy, "XdndPosition", False);
        Atom status = XInternAtom(dpy, "XdndStatus", False), leave = XInternAtom(dpy, "XdndLeave", False), drop = XInternAtom(dpy, "XdndDrop", False);
        Atom finished = XInternAtom(dpy, "XdndFinished", False), selection = XInternAtom(dpy, "XdndSelection", False);
        Atom copy = XInternAtom(dpy, "XdndActionCopy", False), utf8 = XInternAtom(dpy, "UTF8_STRING", False), incr = XInternAtom(dpy, "INCR", False);
        Window root = DefaultRootWindow(dpy), a, over = None, rr, child, requestor = None;
        Atom property = None, type = None;
        long total = xb_bytes(), chunk = 65536, at = 0;
        int ax, ay, rx = 0, ry = 0, wx, wy, on_a = 0, entered = 0, accepted = 0, dropped = 0, done = 0, ok = 0, served = 0, escaped_status = 0;
        unsigned int mask;
        unsigned char *data = malloc((size_t)chunk);
        uint64_t t0;
        Atom asked = None;

        if (!getenv("STRESS_FIFO") || (x_fifo = open(getenv("STRESS_FIFO"), O_WRONLY | O_NONBLOCK)) < 0)
        {
                snprintf(detail, room, "no_STRESS_FIFO");
                return 3;
        }
        a = x_window(dpy, 100, 200, 300, 200, 0x406080u, 1);
        XSelectInput(dpy, a, ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
        XSync(dpy, False);
        usleep(600000);
        XTranslateCoordinates(dpy, a, root, 0, 0, &ax, &ay, &child);
        for (int i = 0; i < 40; i++)
                x_inject(2, 0, -100), x_inject(2, 1, -100);
        for (int i = 0; i < 400 && !on_a; i++)
        {
                x_inject(2, 0, 6), x_inject(2, 1, 6);
                usleep(4000);
                XQueryPointer(dpy, root, &rr, &child, &rx, &ry, &wx, &wy, &mask);
                on_a = child == a && rx > ax + 20 && ry > ay + 20;
        }
        if (!on_a)
        {
                snprintf(detail, room, "the_hand_never_reached_the_source");
                return 0;
        }
        x_inject(1, 272, 1);
        usleep(60000);
        XSetSelectionOwner(dpy, selection, a, CurrentTime);
        // STRESS_ASK: the person is asked (XdndActionAsk), the source offering copy and move (XdndActionList, XdndActionDescription).
        if (getenv("STRESS_ASK"))
        {
                Atom list[2] = {copy, XInternAtom(dpy, "XdndActionMove", False)};
                static const char words[] = "Copy\0Move";

                XChangeProperty(dpy, a, XInternAtom(dpy, "XdndActionList", False), XA_ATOM, 32, PropModeReplace, (unsigned char *)list, 2);
                XChangeProperty(dpy, a, XInternAtom(dpy, "XdndActionDescription", False), XA_STRING, 8, PropModeReplace, (const unsigned char *)words,
                                sizeof(words));
                asked = XInternAtom(dpy, "XdndActionAsk", False);
        }
        t0 = now_ns();
        // Right, a step at a time, until a window with XdndAware that is not this client's own accepts.
        for (int i = 0; i < 300 && !(accepted && over != a && over != None); i++)
        {
                x_inject(2, 0, 6);
                usleep(6000);
                XQueryPointer(dpy, root, &rr, &child, &rx, &ry, &wx, &wy, &mask);
                if (child != over)
                {
                        if (over != None && entered)
                                x_client_message(dpy, over, leave, (long)a, 0, 0, 0, 0), entered = 0, accepted = 0;
                        over = child;
                        Atom t;
                        int f;
                        unsigned long count, after;
                        unsigned char *p = NULL;
                        if (over != None && over != a && XGetWindowProperty(dpy, over, aware, 0, 1, False, XA_ATOM, &t, &f, &count, &after, &p) == Success &&
                            count == 1)
                                x_client_message(dpy, over, enter, (long)a, 5L << 24, (long)utf8, 0, 0), entered = 1;
                        if (p)
                                XFree(p);
                }
                if (entered)
                        x_client_message(dpy, over, position, (long)a, 0, (long)rx << 16 | ry, CurrentTime, (long)(asked ? asked : copy));
                XFlush(dpy);
                for (uint64_t wait_status = now_ns() + 30000000ull; entered && now_ns() < wait_status;)
                {
                        XEvent e;
                        if (!xb_next(dpy, &e, wait_status))
                                break;
                        if (e.type == ClientMessage && e.xclient.window == a && (Atom)e.xclient.message_type == status)
                        {
                                accepted = e.xclient.data.l[1] & 1;
                                break;
                        }
                }
        }
        // STRESS_ESCAPE: Escape on the compositor's keyboard while the drag is over the Wayland window: the compositor ends the drag on
        // both sides (the window left, the proxy's status no more accepted), and this source drops all the same, which is refused.
        if (getenv("STRESS_ESCAPE") && entered)
        {
                x_inject(1, 1, 1);
                x_inject(1, 1, 0);
                usleep(200000);
                x_client_message(dpy, over, position, (long)a, 0, (long)rx << 16 | ry, CurrentTime, (long)copy);
                XFlush(dpy);
                for (uint64_t wait_status = now_ns() + 500000000ull; now_ns() < wait_status;)
                {
                        XEvent e;
                        if (!xb_next(dpy, &e, wait_status))
                                break;
                        if (e.type == ClientMessage && e.xclient.window == a && (Atom)e.xclient.message_type == status)
                                escaped_status = 1, accepted = e.xclient.data.l[1] & 1;
                }
        }
        x_inject(1, 272, 0);
        if (getenv("STRESS_ESCAPE") && entered)
        {
                x_client_message(dpy, over, drop, (long)a, 0, CurrentTime, 0, 0);
                XFlush(dpy);
                for (uint64_t end = now_ns() + 3000000000ull; !done && now_ns() < end;)
                {
                        XEvent e;
                        if (!xb_next(dpy, &e, end))
                                break;
                        if (e.type == ClientMessage && e.xclient.window == a && (Atom)e.xclient.message_type == finished)
                                done = 1, ok = (e.xclient.data.l[1] & 1) != 0;
                }
                snprintf(detail, room, "escaped_status=%d_accepted_after=%d_finished=%d_ok=%d", escaped_status, accepted, done, ok);
                return escaped_status && !accepted && done && !ok;
        }
        if (entered && accepted)
                x_client_message(dpy, over, drop, (long)a, 0, CurrentTime, 0, 0), dropped = 1;
        else if (entered)
                x_client_message(dpy, over, leave, (long)a, 0, 0, 0, 0);
        XFlush(dpy);
        for (uint64_t end = now_ns() + 10000000000ull; dropped && !done && now_ns() < end;)
        {
                XEvent e;
                if (!xb_next(dpy, &e, end))
                        break;
                if (e.type == SelectionRequest)
                {
                        XSelectionRequestEvent *r = &e.xselectionrequest;
                        XSelectionEvent n = {0};
                        Atom prop = r->property ? r->property : r->target;

                        n.type = SelectionNotify, n.requestor = r->requestor, n.selection = r->selection, n.target = r->target, n.time = r->time;
                        n.property = None;
                        if (r->target == utf8 && total <= chunk)
                        {
                                xb_fill(data, 0, total);
                                XChangeProperty(dpy, r->requestor, prop, utf8, 8, PropModeReplace, data, (int)total);
                                n.property = prop, served = 1;
                        }
                        else if (r->target == utf8)
                        {
                                long size = total;
                                XSelectInput(dpy, r->requestor, PropertyChangeMask);
                                XChangeProperty(dpy, r->requestor, prop, incr, 32, PropModeReplace, (unsigned char *)&size, 1);
                                n.property = prop, requestor = r->requestor, property = prop, type = utf8, at = 0;
                        }
                        XSendEvent(dpy, r->requestor, False, NoEventMask, (XEvent *)&n);
                        XFlush(dpy);
                }
                else if (e.type == PropertyNotify && requestor != None && e.xproperty.window == requestor && e.xproperty.atom == property &&
                         e.xproperty.state == PropertyDelete)
                {
                        long n = total - at > chunk ? chunk : total - at;
                        xb_fill(data, at, n);
                        XChangeProperty(dpy, requestor, property, type, 8, PropModeReplace, data, (int)n);
                        XFlush(dpy);
                        at += n;
                        if (!n)
                                requestor = None, served = 1;
                }
                else if (e.type == ClientMessage && e.xclient.window == a && (Atom)e.xclient.message_type == finished)
                        done = 1, ok = (e.xclient.data.l[1] & 1) != 0;
        }
        snprintf(detail, room, "entered=%d_accepted=%d_dropped=%d_served=%d_finished=%d_ok=%d_bytes=%ld_drag_ms=%.1f", entered, accepted, dropped, served, done, ok,
                 total, (double)(now_ns() - t0) / 1e6);
        return done && ok && served;
}

static Window xb_proxy, xb_proxied;
static Display *xb_proxy_dpy;
static long proxied;

static int x_dnd_from_wl(Display *dpy, char *detail, size_t room)
{
        Atom aware = XInternAtom(dpy, "XdndAware", False), enter = XInternAtom(dpy, "XdndEnter", False), position = XInternAtom(dpy, "XdndPosition", False);
        Atom status = XInternAtom(dpy, "XdndStatus", False), leave = XInternAtom(dpy, "XdndLeave", False), drop = XInternAtom(dpy, "XdndDrop", False);
        Atom finished = XInternAtom(dpy, "XdndFinished", False), selection = XInternAtom(dpy, "XdndSelection", False);
        Atom copy = XInternAtom(dpy, "XdndActionCopy", False), utf8 = XInternAtom(dpy, "UTF8_STRING", False);
        int sw = DisplayWidth(dpy, DefaultScreen(dpy)), sh = DisplayHeight(dpy, DefaultScreen(dpy));
        // Override-redirect, so it is where it says (over the right half), and mapped while the Wayland drag is held, over the window it began in.
        Window b = x_window(dpy, sw / 2, 0, sw - sw / 2, sh, 0x608040u, 1), child;
        int bx = 0, by = 0;
        long version = 5, want = xb_bytes(), got = -2, wrong = 0, positions = 0;
        int entered = 0, dropped = 0, left = 0, types_utf8 = 0;
        double ms = -1;
        uint64_t until = now_ns() + (uint64_t)(xb_hold() * 1e9);
        XEvent e;

        XSelectInput(dpy, b, PropertyChangeMask);
        XChangeProperty(dpy, b, aware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);
        // STRESS_PROXY: the target is a proxy's (XdndProxy on it and on the proxy, which names itself): the drag's messages go to the proxy
        // window, naming this one, and are read there.
        if (getenv("STRESS_PROXY"))
        {
                // The proxy is another connection's window, so a message that comes there came to the proxy and to nothing else.
                Display *d2 = XOpenDisplay(NULL);
                Atom proxy_atom = XInternAtom(dpy, "XdndProxy", False);
                Window p = d2 ? XCreateSimpleWindow(d2, DefaultRootWindow(d2), -10, -10, 1, 1, 0, 0, 0) : None;

                if (p)
                {
                        XChangeProperty(dpy, b, proxy_atom, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&p, 1);
                        XChangeProperty(d2, p, proxy_atom, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&p, 1);
                        XChangeProperty(d2, p, aware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);
                        XSync(d2, False);
                        xb_proxy = p, xb_proxied = b, xb_proxy_dpy = d2;
                }
        }
        XSync(dpy, False);
        usleep(400000);
        XTranslateCoordinates(dpy, b, DefaultRootWindow(dpy), 0, 0, &bx, &by, &child);
        printf("READY %d %d %d %d\n", bx, by, sw - sw / 2, sh);
        fflush(stdout);
        while (!dropped && !left && xb_next(xb_proxy_dpy ? xb_proxy_dpy : dpy, &e, until))
        {
                // Through a proxy a message comes to the proxy's connection and names the target: counted, and read as the target's.
                if (xb_proxy_dpy && e.type == ClientMessage && e.xclient.window == xb_proxied)
                        proxied++;
                if (e.type != ClientMessage || e.xclient.window != b)
                        continue;
                Atom m = (Atom)e.xclient.message_type;
                Window source = (Window)e.xclient.data.l[0];
                if (m == enter)
                {
                        entered++;
                        for (int k = 2; k < 5; k++)
                                types_utf8 |= (Atom)e.xclient.data.l[k] == utf8;
                        if (e.xclient.data.l[1] & 1)
                        {
                                Atom t, *list = NULL;
                                int f;
                                unsigned long count, after;
                                Atom typelist = XInternAtom(dpy, "XdndTypeList", False);
                                if (XGetWindowProperty(dpy, source, typelist, 0, 64, False, XA_ATOM, &t, &f, &count, &after, (unsigned char **)&list) == Success && list)
                                {
                                        for (unsigned long k = 0; k < count; k++)
                                                types_utf8 |= list[k] == utf8;
                                        XFree(list);
                                }
                        }
                }
                else if (m == position)
                {
                        positions++;
                        x_client_message(dpy, source, status, (long)b, types_utf8 ? 1 : 0, 0, 0, types_utf8 ? (long)copy : 0);
                        XFlush(dpy);
                }
                else if (m == leave)
                        left = 1;
                else if (m == drop)
                {
                        dropped = 1;
                        got = xb_convert(dpy, b, selection, utf8, (Time)e.xclient.data.l[2], xb_die_after(), 10.0, &wrong, &ms);
                        x_client_message(dpy, source, finished, (long)b, got == want && !wrong ? 1 : 0, got == want && !wrong ? (long)copy : 0, 0, 0);
                        XFlush(dpy);
                }
        }
        snprintf(detail, room, "entered=%d_positions=%ld_text=%d_dropped=%d_left=%d_bytes=%ld_of_%ld_wrong=%ld_read_ms=%.1f_proxied=%ld", entered, positions,
                 types_utf8, dropped, left, got, want, wrong, ms, proxied);
        if (xb_proxy && !proxied)
                return 0;
        if (getenv("STRESS_EXPECT_LEAVE"))
                return entered && left && !dropped;
        return dropped && got == want && !wrong;
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
        else if (!strcmp(name, "x-clip-own") || !strcmp(name, "x-clip-paste") || !strcmp(name, "x-dnd-to-wl") || !strcmp(name, "x-dnd-from-wl"))
        {
                int got = !strcmp(name, "x-clip-own")     ? x_clip_own(dpy, detail, sizeof(detail))
                          : !strcmp(name, "x-clip-paste") ? x_clip_paste(dpy, detail, sizeof(detail))
                          : !strcmp(name, "x-dnd-to-wl")  ? x_dnd_to_wl(dpy, detail, sizeof(detail))
                                                          : x_dnd_from_wl(dpy, detail, sizeof(detail));

                if (got == 3)
                {
                        printf("CASE %s outcome=skip ms=-1 detail=%s\n", name, detail);
                        return 0;
                }
                printf("CASE %s outcome=%s ms=%.3f detail=%s\n", name, got ? "ok" : "fail", (double)(now_ns() - x_t0) / 1e6, detail);
                return 0;
        }
        else if (!strcmp(name, "x-dnd"))
        {
                int got = x_dnd(dpy, detail, sizeof(detail));

                if (got == 3)
                {
                        printf("CASE %s outcome=skip ms=-1 detail=%s\n", name, detail);
                        return 0;
                }
                if (!got)
                {
                        printf("CASE %s outcome=fail ms=%.3f detail=%s\n", name, (double)(now_ns() - x_t0) / 1e6, detail);
                        return 0;
                }
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
