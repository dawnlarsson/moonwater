/*
        The Wayland stress client: one program on the real libwayland-client that drives a compositor through the paths a
        browser, a game and a launcher use, and says what each cost, by wp_presentation's feedback where the compositor has it
        and by its own CLOCK_MONOTONIC always. test/wayland_stress/run.sh builds it and runs it against Moonwater and the
        reference compositors; it is test tooling, never part of src/.

            wl-stress -m MODE [-t SECONDS] [-w W] [-h H] [-N WINDOWS] [-B 2|3] [-R] [-F FIFO] [-S BYTES] [-n COUNT] [-A]

        MODE is one of
            anim     N toplevels (-N, default 1) on wl_shm animating on frame callbacks, -B buffers each (2: draw at the
                     callback; 3: draw the next frame ahead, as a browser does), full damage or, with -R, a band of 64 rows
            dmabuf   the same on linux-dmabuf buffers made by GBM on the render node (exit 3 with a NOTE where none is)
            resize   one toplevel whose buffer changes size every frame (a resize storm), new wl_buffers from one pool
            sub      a subsurface destroyed and made again every frame over an animating parent
            popup    an xdg_popup destroyed and made again every frame over an animating parent
            lock     a fullscreen toplevel that locks the pointer; with -F (the compositor's evdev FIFO, Moonwater headless)
                     it writes relative motion there itself and measures input to the frame that shows it
            clip     -n clipboard round trips of -S bytes through the compositor (copy, then paste back to itself), and with
                     -F as many drag and drop round trips
            first    connect, one frame, and the time to its presentation (STRESS_T0, ns of CLOCK_MONOTONIC set by the runner
                     before the exec, gives the launch to first frame as well)
        -A draws ARGB with alpha (a translucent window). Output: NOTE lines, and one line "RESULT key=value ..." at the end.
        Exit 0, 1 on a protocol error, 2 when it cannot start, 3 when the mode cannot run here (said in a NOTE).
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client.h"
#include "presentation-time-client.h"
#include "relative-pointer-unstable-v1-client.h"
#include "xdg-shell-client.h"
#ifdef HAVE_DMABUF
#include <gbm.h>
#include "linux-dmabuf-v1-client.h"
#endif

#define MAXWIN 64
#define RING 256

static uint64_t now_ns(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

/* ---- a series of samples, in milliseconds ---- */
struct series
{
        double *v;
        int n, cap;
};

static void push(struct series *s, double x)
{
        if (s->n == s->cap)
        {
                s->cap = s->cap ? s->cap * 2 : 1024;
                s->v = realloc(s->v, sizeof(double) * (size_t)s->cap);
        }
        s->v[s->n++] = x;
}

static int cmp(const void *a, const void *b)
{
        double x = *(const double *)a, y = *(const double *)b;
        return x < y ? -1 : x > y;
}

static double pct(struct series *s, double p)
{
        if (!s->n)
                return -1;
        qsort(s->v, (size_t)s->n, sizeof(double), cmp);
        int i = (int)ceil(p / 100.0 * s->n) - 1;
        if (i < 0)
                i = 0;
        if (i >= s->n)
                i = s->n - 1;
        return s->v[i];
}

/* ---- globals ---- */
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_subcompositor *subcompositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wp_presentation *presentation;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct wl_keyboard *keyboard;
static struct zwp_relative_pointer_manager_v1 *relative_manager;
static struct zwp_pointer_constraints_v1 *constraints;
static struct wl_data_device_manager *ddm;
static struct wl_data_device *data_device;
static int ddm_version;
#ifdef HAVE_DMABUF
static struct zwp_linux_dmabuf_v1 *dmabuf;
static struct gbm_device *gbm;
#endif
static clockid_t presentation_clock = CLOCK_MONOTONIC;
static int presentation_clock_known;

/* ---- options ---- */
static const char *mode = "anim";
static double seconds = 3;
static int want_w = 1280, want_h = 720, windows = 1, nbuffers = 2, band = 0, alpha = 0, count = 50;
static long payload = 65536;
static const char *fifo_path;
static int fifo = -1;

/* ---- what was measured ---- */
static struct series s_interval, s_c2p, s_cb, s_cb_after, s_input, s_event, s_op, s_op2;
static long dropped, late, discarded, presented_n, commits, stalls;
static uint64_t refresh_ns, flags_or, t_start, t_end, warm_until;
static int use_dmabuf;

/* ---- buffers ---- */
struct buf
{
        struct wl_buffer *b;
        uint32_t *data;
        int w, h, stride, busy;
        size_t size;
#ifdef HAVE_DMABUF
        struct gbm_bo *bo;
        void *map_data;
#endif
};

static void buffer_release(void *data, struct wl_buffer *b)
{
        (void)b;
        ((struct buf *)data)->busy = 0;
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};

static int shm_file(size_t size)
{
        int fd = memfd_create("wl-stress", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd < 0 || ftruncate(fd, (off_t)size) < 0)
                return -1;
        return fd;
}

static int make_shm(struct buf *b, int w, int h)
{
        b->w = w, b->h = h, b->stride = w * 4, b->size = (size_t)b->stride * (size_t)h;
        int fd = shm_file(b->size);
        if (fd < 0)
                return 0;
        b->data = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (b->data == MAP_FAILED)
                return 0;
        struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
        b->b = wl_shm_pool_create_buffer(pool, 0, w, h, b->stride, alpha ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        wl_buffer_add_listener(b->b, &buffer_listener, b);
        b->busy = 0;
        return 1;
}

#ifdef HAVE_DMABUF
static int make_dmabuf(struct buf *b, int w, int h)
{
        b->w = w, b->h = h;
        b->bo = gbm_bo_create(gbm, (uint32_t)w, (uint32_t)h, alpha ? GBM_FORMAT_ARGB8888 : GBM_FORMAT_XRGB8888,
                              GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
        if (!b->bo)
                return 0;
        struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(dmabuf);
        int fd = gbm_bo_get_fd(b->bo);
        uint64_t modifier = gbm_bo_get_modifier(b->bo);
        zwp_linux_buffer_params_v1_add(params, fd, 0, gbm_bo_get_offset(b->bo, 0), gbm_bo_get_stride(b->bo), (uint32_t)(modifier >> 32),
                                       (uint32_t)modifier);
        b->b = zwp_linux_buffer_params_v1_create_immed(params, w, h, alpha ? GBM_FORMAT_ARGB8888 : GBM_FORMAT_XRGB8888, 0);
        zwp_linux_buffer_params_v1_destroy(params);
        close(fd);
        uint32_t stride = 0;
        b->data = gbm_bo_map(b->bo, 0, 0, (uint32_t)w, (uint32_t)h, GBM_BO_TRANSFER_WRITE, &stride, &b->map_data);
        if (!b->data)
                return 0;
        b->stride = (int)stride;
        wl_buffer_add_listener(b->b, &buffer_listener, b);
        b->busy = 0;
        return 1;
}
#endif

static int make_buffer(struct buf *b, int w, int h)
{
#ifdef HAVE_DMABUF
        if (use_dmabuf)
                return make_dmabuf(b, w, h);
#endif
        return make_shm(b, w, h);
}

/* What every frame draws: a field whose shade moves with the frame and a bar that sweeps across it. */
static void draw(struct buf *b, int w, unsigned frame, int y0, int y1)
{
        uint32_t a = alpha ? 0xc0000000u : 0xff000000u;
        uint32_t ground = a | ((frame * 3u) & 0xff) << 16 | 0x3000u | 0x60u;
        int bar = (int)((frame * 8u) % (unsigned)w);
        for (int y = y0; y < y1; y++)
        {
                uint32_t *row = (uint32_t *)((char *)b->data + (size_t)y * (size_t)b->stride);
                for (int x = 0; x < w; x++)
                        row[x] = ground;
                for (int x = bar; x < bar + 24 && x < w; x++)
                        row[x] = 0xffffffffu;
        }
}

/* ---- windows ---- */
struct frame_rec
{
        struct win *win;
        uint64_t t_commit, t_present, t_cb, t_input;
        int seq, used;
};

struct win
{
        struct wl_surface *s;
        struct xdg_surface *xs;
        struct xdg_toplevel *top;
        struct buf bufs[3];
        struct buf *ahead; // a frame drawn before its callback (-B 3)
        int w, h, configured, closed, mapped, stalled;
        struct wl_callback *cb;
        unsigned frame;
        uint64_t last_present;
        struct frame_rec ring[RING];
        int seq;
};

static struct win win[MAXWIN];
static int running = 1;
static uint64_t first_present_ns, t_connect;

static void frame_presented(struct win *w, struct frame_rec *r)
{
        if (!r->t_present)
                return;
        if (!first_present_ns)
                first_present_ns = r->t_present;
        if (r->t_present < warm_until)
                return;
        presented_n++;
        double c2p = (double)(r->t_present - r->t_commit) / 1e6;
        push(&s_c2p, c2p);
        double period = refresh_ns ? (double)refresh_ns / 1e6 : 1000.0 / 60;
        if (c2p > period + 1.0)
                late++;
        if (w->last_present)
        {
                double interval = (double)(r->t_present - w->last_present) / 1e6;
                push(&s_interval, interval);
                if (interval > period * 1.5)
                        dropped += (long)floor(interval / period + 0.5) - 1;
        }
        w->last_present = r->t_present;
        if (r->t_input)
        {
                push(&s_input, (double)(r->t_present - r->t_input) / 1e6);
                r->t_input = 0;
        }
}

static void feedback_sync_output(void *d, struct wp_presentation_feedback *f, struct wl_output *o)
{
        (void)d, (void)f, (void)o;
}

static void feedback_presented(void *d, struct wp_presentation_feedback *f, uint32_t hi, uint32_t lo, uint32_t nsec, uint32_t refresh,
                               uint32_t seq_hi, uint32_t seq_lo, uint32_t flags)
{
        (void)seq_hi, (void)seq_lo;
        struct frame_rec *r = d;
        uint64_t t = ((uint64_t)hi << 32 | lo) * 1000000000ull + nsec;
        if (presentation_clock != CLOCK_MONOTONIC)
        {
                struct timespec a, m;
                clock_gettime(presentation_clock, &a);
                clock_gettime(CLOCK_MONOTONIC, &m);
                t = t - ((uint64_t)a.tv_sec * 1000000000ull + (uint64_t)a.tv_nsec) + ((uint64_t)m.tv_sec * 1000000000ull + (uint64_t)m.tv_nsec);
        }
        if (refresh)
                refresh_ns = refresh;
        flags_or |= flags;
        r->t_present = t;
        frame_presented(r->win, r);
        if (r->t_cb && r->t_cb >= r->t_present && r->t_present >= warm_until)
                push(&s_cb_after, (double)(r->t_cb - r->t_present) / 1e6);
        r->used = 0;
        wp_presentation_feedback_destroy(f);
}

static void feedback_discarded(void *d, struct wp_presentation_feedback *f)
{
        struct frame_rec *r = d;
        if (now_ns() >= warm_until)
                discarded++;
        r->used = 0;
        wp_presentation_feedback_destroy(f);
}

static const struct wp_presentation_feedback_listener feedback_listener = {feedback_sync_output, feedback_presented, feedback_discarded};

static struct buf *free_buffer(struct win *w)
{
        for (int i = 0; i < nbuffers; i++)
                if (!w->bufs[i].busy && &w->bufs[i] != w->ahead)
                        return &w->bufs[i];
        return NULL;
}

static void win_commit(struct win *w);
static void (*next_frame)(struct win *w) = win_commit;
static int resize_mode;

static void frame_done(void *data, struct wl_callback *cb, uint32_t ms)
{
        (void)ms;
        struct frame_rec *r = data;
        struct win *w = r->win;
        uint64_t t = now_ns();
        wl_callback_destroy(cb);
        w->cb = NULL;
        r->t_cb = t;
        if (t >= warm_until)
                push(&s_cb, (double)(t - r->t_commit) / 1e6);
        if (!presentation)
        {
                r->t_present = t; // no wp_presentation: the callback is the nearest thing to a present the client can see
                frame_presented(w, r);
                r->used = 0;
        }
        else if (r->t_present && r->t_present >= warm_until)
                push(&s_cb_after, (double)(t - r->t_present) / 1e6);
        if (running)
                next_frame(w);
}
static const struct wl_callback_listener frame_listener = {frame_done};

static uint64_t pending_input; // the time the newest input not yet in a frame was written
static void (*before_commit)(struct win *w);

static void win_commit(struct win *w)
{
        struct buf *b = w->ahead;
        w->ahead = NULL;
        if (!b)
        {
                b = free_buffer(w);
                if (!b)
                {
                        // Every buffer is held: wait for a release, then draw (the loop calls back here when one is).
                        if (!w->stalled)
                                stalls++, w->stalled = 1;
                        return;
                }
                draw(b, w->w, w->frame, band ? (int)((w->frame * 4u) % (unsigned)(w->h - 64)) : 0,
                     band ? (int)((w->frame * 4u) % (unsigned)(w->h - 64)) + 64 : w->h);
        }
        w->frame++, w->stalled = 0;
        struct frame_rec *r = &w->ring[w->seq++ % RING];
        memset(r, 0, sizeof(*r));
        r->win = w, r->used = 1;
        if (before_commit)
                before_commit(w);
        wl_surface_attach(w->s, b->b, 0, 0);
        if (band)
        {
                int y = (int)(((w->frame - 1) * 4u) % (unsigned)(w->h - 64));
                wl_surface_damage_buffer(w->s, 0, y > 4 ? y - 4 : 0, w->w, 72);
        }
        else
                wl_surface_damage_buffer(w->s, 0, 0, w->w, w->h);
        b->busy = 1;
        w->cb = wl_surface_frame(w->s);
        wl_callback_add_listener(w->cb, &frame_listener, r);
        if (presentation)
        {
                struct wp_presentation_feedback *f = wp_presentation_feedback(presentation, w->s);
                wp_presentation_feedback_add_listener(f, &feedback_listener, r);
        }
        r->t_input = pending_input;
        pending_input = 0;
        r->t_commit = now_ns();
        wl_surface_commit(w->s);
        commits++;
        if (nbuffers == 3)
        {
                struct buf *n = free_buffer(w);
                if (n)
                {
                        draw(n, w->w, w->frame, 0, w->h);
                        w->ahead = n;
                }
        }
}

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
        struct win *w = data;
        xdg_surface_ack_configure(xs, serial);
        if (!w->configured)
        {
                w->configured = 1;
                for (int i = 0; i < nbuffers && !resize_mode; i++)
                        if (!make_buffer(&w->bufs[i], w->w, w->h))
                        {
                                fprintf(stderr, "cannot make buffers\n");
                                exit(2);
                        }
                next_frame(w);
        }
}
static const struct xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
        (void)data, (void)t, (void)w, (void)h, (void)states;
}
static void toplevel_close(void *data, struct xdg_toplevel *t)
{
        (void)t;
        ((struct win *)data)->closed = 1;
        running = 0;
}
static void toplevel_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h)
{
        (void)d, (void)t, (void)w, (void)h;
}
static void toplevel_caps(void *d, struct xdg_toplevel *t, struct wl_array *a)
{
        (void)d, (void)t, (void)a;
}
static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close, toplevel_bounds, toplevel_caps};

static void win_open(struct win *w, int width, int height, int fullscreen)
{
        w->w = width, w->h = height;
        w->s = wl_compositor_create_surface(compositor);
        w->xs = xdg_wm_base_get_xdg_surface(wm_base, w->s);
        xdg_surface_add_listener(w->xs, &xdg_surface_listener, w);
        w->top = xdg_surface_get_toplevel(w->xs);
        xdg_toplevel_add_listener(w->top, &toplevel_listener, w);
        xdg_toplevel_set_title(w->top, "wl-stress");
        xdg_toplevel_set_app_id(w->top, "wl-stress");
        if (fullscreen)
                xdg_toplevel_set_fullscreen(w->top, NULL);
        wl_surface_commit(w->s);
}

/* ---- the seat ---- */
static uint32_t serial_key, serial_button, serial_enter;
static struct wl_surface *pointer_focus;
static int keyboard_focus, button_down, pointer_in;

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
        (void)d, (void)p, (void)x, (void)y;
        pointer_focus = s, serial_enter = serial, pointer_in = 1;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
        (void)d, (void)p, (void)serial, (void)s;
        pointer_focus = NULL, pointer_in = 0;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
        (void)d, (void)p, (void)t, (void)x, (void)y;
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
        (void)d, (void)p, (void)t, (void)button;
        serial_button = serial;
        button_down = state == WL_POINTER_BUTTON_STATE_PRESSED;
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
        (void)d, (void)p, (void)t, (void)a, (void)v;
}
static void p_frame(void *d, struct wl_pointer *p)
{
        (void)d, (void)p;
}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s)
{
        (void)d, (void)p, (void)s;
}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a)
{
        (void)d, (void)p, (void)t, (void)a;
}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{
        (void)d, (void)p, (void)a, (void)v;
}
static void p_axis_v120(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{
        (void)d, (void)p, (void)a, (void)v;
}
static void p_axis_dir(void *d, struct wl_pointer *p, uint32_t a, uint32_t v)
{
        (void)d, (void)p, (void)a, (void)v;
}
static const struct wl_pointer_listener pointer_listener = {p_enter, p_leave, p_motion, p_button, p_axis, p_frame,
                                                            p_axis_source, p_axis_stop, p_axis_discrete, p_axis_v120, p_axis_dir};

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t size)
{
        (void)d, (void)k, (void)f, (void)size;
        close(fd);
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys)
{
        (void)d, (void)k, (void)s, (void)keys;
        keyboard_focus = 1, serial_key = serial;
}
static void k_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
        (void)d, (void)k, (void)serial, (void)s;
        keyboard_focus = 0;
}
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
        (void)d, (void)k, (void)t, (void)key, (void)state;
        serial_key = serial;
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t a, uint32_t b, uint32_t c, uint32_t e)
{
        (void)d, (void)k, (void)serial, (void)a, (void)b, (void)c, (void)e;
}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t delay)
{
        (void)d, (void)k, (void)r, (void)delay;
}
static const struct wl_keyboard_listener keyboard_listener = {k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
        (void)d;
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer)
        {
                pointer = wl_seat_get_pointer(s);
                wl_pointer_add_listener(pointer, &pointer_listener, NULL);
        }
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard)
        {
                keyboard = wl_seat_get_keyboard(s);
                wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
        }
}
static void seat_name(void *d, struct wl_seat *s, const char *n)
{
        (void)d, (void)s, (void)n;
}
static const struct wl_seat_listener seat_listener = {seat_caps, seat_name};

/* ---- the registry ---- */
static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
        (void)d;
        xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static void presentation_clock_id(void *d, struct wp_presentation *p, uint32_t id)
{
        (void)d, (void)p;
        presentation_clock = (clockid_t)id, presentation_clock_known = 1;
}
static const struct wp_presentation_listener presentation_listener = {presentation_clock_id};

static uint32_t min_u(uint32_t a, uint32_t b)
{
        return a < b ? a : b;
}

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
        (void)d;
        if (!strcmp(iface, wl_compositor_interface.name))
                compositor = wl_registry_bind(r, name, &wl_compositor_interface, min_u(version, 4));
        else if (!strcmp(iface, wl_subcompositor_interface.name))
                subcompositor = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
        else if (!strcmp(iface, wl_shm_interface.name))
                shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
        else if (!strcmp(iface, xdg_wm_base_interface.name))
        {
                wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, min_u(version, 4));
                xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
        }
        else if (!strcmp(iface, wp_presentation_interface.name) && !getenv("STRESS_NO_PRESENTATION"))
        {
                presentation = wl_registry_bind(r, name, &wp_presentation_interface, 1);
                wp_presentation_add_listener(presentation, &presentation_listener, NULL);
        }
        else if (!strcmp(iface, wl_seat_interface.name) && !seat)
        {
                seat = wl_registry_bind(r, name, &wl_seat_interface, min_u(version, 5));
                wl_seat_add_listener(seat, &seat_listener, NULL);
        }
        else if (!strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
                relative_manager = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
        else if (!strcmp(iface, zwp_pointer_constraints_v1_interface.name))
                constraints = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
        else if (!strcmp(iface, wl_data_device_manager_interface.name))
                ddm_version = (int)min_u(version, 3), ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, (uint32_t)ddm_version);
#ifdef HAVE_DMABUF
        else if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
                dmabuf = wl_registry_bind(r, name, &zwp_linux_dmabuf_v1_interface, min_u(version, 3));
#endif
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name)
{
        (void)d, (void)r, (void)name;
}
static const struct wl_registry_listener registry_listener = {global, global_remove};

/* ---- the loop: the display, a timer, and a check that a held buffer came back ---- */
static int timer = -1;
static void (*on_timer)(void);
static int (*until)(void); // the mode's own end; 0 to go on

static int loop(void)
{
        while (running)
        {
                if (!until && now_ns() >= t_end)
                        break;
                if (until && until())
                        break;
                while (wl_display_prepare_read(display) != 0)
                        wl_display_dispatch_pending(display);
                if (wl_display_flush(display) < 0 && errno != EAGAIN)
                {
                        wl_display_cancel_read(display);
                        return 1;
                }
                struct pollfd p[2] = {{wl_display_get_fd(display), POLLIN, 0}, {timer, POLLIN, 0}};
                int ms = 20;
                int r = poll(p, timer >= 0 ? 2 : 1, ms);
                if (r > 0 && (p[0].revents & POLLIN))
                {
                        if (wl_display_read_events(display) < 0)
                                return 1;
                }
                else
                        wl_display_cancel_read(display);
                if (wl_display_dispatch_pending(display) < 0)
                        return 1;
                if (timer >= 0 && r > 0 && (p[1].revents & POLLIN))
                {
                        uint64_t n;
                        if (read(timer, &n, sizeof(n)) == sizeof(n) && on_timer)
                                on_timer();
                }
                // A window that stalled for a buffer draws when one is released.
                for (int i = 0; i < windows; i++)
                        if (win[i].configured && !win[i].cb && running)
                                next_frame(&win[i]);
        }
        return wl_display_get_error(display) ? 1 : 0;
}

static void timer_every(uint64_t ns)
{
        timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        struct itimerspec it = {{(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)}, {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)}};
        timerfd_settime(timer, 0, &it, NULL);
}

/* ---- evdev records written to the compositor's FIFO (Moonwater headless's -i) ---- */
struct evdev
{
        int64_t sec, usec;
        uint16_t type, code;
        int32_t value;
};

static uint64_t inject(int type, int code, int value)
{
        struct evdev e[2];
        uint64_t t = now_ns();
        memset(e, 0, sizeof(e));
        e[0].sec = (int64_t)(t / 1000000000ull), e[0].usec = (int64_t)(t % 1000000000ull / 1000);
        e[0].type = (uint16_t)type, e[0].code = (uint16_t)code, e[0].value = value;
        e[1] = e[0];
        e[1].type = 0, e[1].code = 0, e[1].value = 0;
        if (fifo >= 0 && write(fifo, e, sizeof(e)) != (ssize_t)sizeof(e))
                return 0;
        return t;
}

/* ---- lock: fullscreen, the pointer locked, relative motion from the FIFO to the frame that shows it ---- */
static struct zwp_locked_pointer_v1 *locked;
static int is_locked;
static uint64_t injected_at;
static long injected, received;

static void rel_motion(void *d, struct zwp_relative_pointer_v1 *r, uint32_t hi, uint32_t lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t ux,
                       wl_fixed_t uy)
{
        (void)d, (void)r, (void)hi, (void)lo, (void)dx, (void)dy, (void)ux, (void)uy;
        uint64_t t = now_ns();
        received++;
        if (injected_at)
        {
                if (t >= warm_until)
                        push(&s_event, (double)(t - injected_at) / 1e6);
                if (t >= warm_until)
                        pending_input = injected_at; // the next frame drawn shows it
                injected_at = 0;
        }
}
static const struct zwp_relative_pointer_v1_listener rel_listener = {rel_motion};

static void lock_locked(void *d, struct zwp_locked_pointer_v1 *l)
{
        (void)d, (void)l;
        is_locked = 1;
}
static void lock_unlocked(void *d, struct zwp_locked_pointer_v1 *l)
{
        (void)d, (void)l;
        is_locked = 0;
}
static const struct zwp_locked_pointer_v1_listener lock_listener = {lock_locked, lock_unlocked};

static long lost;
static void lock_tick(void)
{
        if (!is_locked)
                return;
        if (injected_at && now_ns() - injected_at < 100000000ull)
                return; // the last one has not come yet
        if (injected_at)
                lost++; // 100 ms and no relative motion for it
        injected_at = inject(2, 0, (injected & 1) ? 3 : -3); // EV_REL REL_X
        if (injected_at)
                injected++;
}

/* ---- sub and popup churn ---- */
static struct wl_surface *child_s;
static struct wl_subsurface *child_sub;
static struct buf child_buf[2];
static unsigned churn;
static long churned;

static void sub_churn(struct win *w)
{
        if (child_sub)
        {
                wl_subsurface_destroy(child_sub);
                wl_surface_destroy(child_s);
        }
        uint64_t t = now_ns();
        child_s = wl_compositor_create_surface(compositor);
        child_sub = wl_subcompositor_get_subsurface(subcompositor, child_s, w->s);
        wl_subsurface_set_position(child_sub, (int)(churn * 7 % (unsigned)(w->w - 64)), (int)(churn * 5 % (unsigned)(w->h - 64)));
        struct buf *b = &child_buf[churn & 1];
        wl_surface_attach(child_s, b->b, 0, 0);
        wl_surface_damage_buffer(child_s, 0, 0, 64, 64);
        wl_surface_commit(child_s);
        churn++, churned++;
        if (t >= warm_until)
                push(&s_op, (double)(now_ns() - t) / 1e6);
}

struct popup
{
        struct wl_surface *s;
        struct xdg_surface *xs;
        struct xdg_popup *p;
        uint64_t t_made;
        int configured;
};
static struct popup pop;
static long popups_shown;

static void popup_done(void *data, struct wl_callback *cb, uint32_t ms)
{
        (void)ms;
        uint64_t made = (uint64_t)(uintptr_t)data;
        wl_callback_destroy(cb);
        if (made >= warm_until)
                push(&s_op2, (double)(now_ns() - made) / 1e6);
        popups_shown++;
}
static const struct wl_callback_listener popup_frame_listener = {popup_done};

static void popup_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
        struct popup *p = data;
        xdg_surface_ack_configure(xs, serial);
        if (p->configured)
                return;
        p->configured = 1;
        if (p->t_made >= warm_until)
                push(&s_op, (double)(now_ns() - p->t_made) / 1e6);
        wl_surface_attach(p->s, child_buf[churn & 1].b, 0, 0);
        wl_surface_damage_buffer(p->s, 0, 0, 64, 64);
        struct wl_callback *cb = wl_surface_frame(p->s);
        wl_callback_add_listener(cb, &popup_frame_listener, (void *)(uintptr_t)p->t_made);
        wl_surface_commit(p->s);
}
static const struct xdg_surface_listener popup_surface_listener = {popup_surface_configure};
static void popup_configure(void *d, struct xdg_popup *p, int32_t x, int32_t y, int32_t w, int32_t h)
{
        (void)d, (void)p, (void)x, (void)y, (void)w, (void)h;
}
static void popup_dismissed(void *d, struct xdg_popup *p)
{
        (void)d, (void)p;
}
static void popup_repositioned(void *d, struct xdg_popup *p, uint32_t t)
{
        (void)d, (void)p, (void)t;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_dismissed, popup_repositioned};

static void popup_churn(struct win *w)
{
        if (pop.p && !pop.configured)
                return; // the last one is not configured yet: one at a time
        if (pop.p)
        {
                xdg_popup_destroy(pop.p);
                xdg_surface_destroy(pop.xs);
                wl_surface_destroy(pop.s);
        }
        memset(&pop, 0, sizeof(pop));
        pop.t_made = now_ns();
        struct xdg_positioner *pos = xdg_wm_base_create_positioner(wm_base);
        xdg_positioner_set_size(pos, 64, 64);
        xdg_positioner_set_anchor_rect(pos, (int)(churn * 13 % (unsigned)(w->w - 80)), (int)(churn * 11 % (unsigned)(w->h - 80)), 8, 8);
        pop.s = wl_compositor_create_surface(compositor);
        pop.xs = xdg_wm_base_get_xdg_surface(wm_base, pop.s);
        xdg_surface_add_listener(pop.xs, &popup_surface_listener, &pop);
        pop.p = xdg_surface_get_popup(pop.xs, w->xs, pos);
        xdg_popup_add_listener(pop.p, &popup_listener, &pop);
        xdg_positioner_destroy(pos);
        wl_surface_commit(pop.s);
        churn++, churned++;
}

/* ---- resize storm ---- */
static int pool_fd = -1;
static uint32_t *pool_data;
static struct wl_shm_pool *pool;
static size_t pool_slot;
static struct buf rbufs[3];
static int rmax_w, rmax_h;

static void resize_release(void *data, struct wl_buffer *b)
{
        struct buf *rb = data;
        wl_buffer_destroy(b);
        rb->b = NULL, rb->busy = 0;
}
static const struct wl_buffer_listener resize_buffer_listener = {resize_release};


static void resize_commit(struct win *w)
{
        struct buf *b = NULL;
        int slot = 0;
        for (int i = 0; i < 3; i++)
                if (!rbufs[i].busy)
                {
                        b = &rbufs[i], slot = i;
                        break;
                }
        if (!b)
        {
                if (!w->stalled)
                        stalls++, w->stalled = 1;
                return;
        }
        w->stalled = 0;
        unsigned f = w->frame;
        unsigned span = (unsigned)(rmax_w - 400) / 8;
        unsigned phase = f % (2 * span);
        int nw = 400 + 8 * (int)(phase < span ? phase : 2 * span - phase);
        int nh = 300 + (nw - 400) * (rmax_h - 300) / (rmax_w - 400);
        b->w = nw, b->h = nh, b->stride = nw * 4;
        b->data = (uint32_t *)((char *)pool_data + pool_slot * (size_t)slot);
        draw(b, nw, f, 0, nh);
        if (b->b)
                wl_buffer_destroy(b->b);
        b->b = wl_shm_pool_create_buffer(pool, (int32_t)(pool_slot * (size_t)slot), nw, nh, b->stride, WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(b->b, &resize_buffer_listener, b);
        b->busy = 1;
        w->w = nw, w->h = nh, w->frame++;
        struct frame_rec *r = &w->ring[w->seq++ % RING];
        memset(r, 0, sizeof(*r));
        r->win = w, r->used = 1;
        wl_surface_attach(w->s, b->b, 0, 0);
        wl_surface_damage_buffer(w->s, 0, 0, nw, nh);
        xdg_surface_set_window_geometry(w->xs, 0, 0, nw, nh);
        w->cb = wl_surface_frame(w->s);
        wl_callback_add_listener(w->cb, &frame_listener, r);
        if (presentation)
        {
                struct wp_presentation_feedback *fb = wp_presentation_feedback(presentation, w->s);
                wp_presentation_feedback_add_listener(fb, &feedback_listener, r);
        }
        r->t_commit = now_ns();
        wl_surface_commit(w->s);
        commits++;
}

/* ---- clipboard and drag and drop round trips ---- */
struct sender
{
        int fd;
        long bytes;
};
static char *payload_data;
static struct wl_data_offer *offer_now;
static int offer_is_selection, offer_entered, offer_dropped;
static uint64_t t_op;
static long ops_done, op_bytes_bad;

static void *send_thread(void *data)
{
        struct sender *s = data;
        long at = 0;
        while (at < s->bytes)
        {
                ssize_t n = write(s->fd, payload_data + at, (size_t)(s->bytes - at));
                if (n <= 0)
                        break;
                at += n;
        }
        close(s->fd);
        free(s);
        return NULL;
}

static void source_target(void *d, struct wl_data_source *s, const char *m)
{
        (void)d, (void)s, (void)m;
}
static void source_send(void *d, struct wl_data_source *s, const char *m, int32_t fd)
{
        (void)d, (void)s, (void)m;
        struct sender *x = malloc(sizeof(*x));
        x->fd = fd, x->bytes = payload;
        pthread_t th;
        pthread_create(&th, NULL, send_thread, x);
        pthread_detach(th);
}
static void source_cancelled(void *d, struct wl_data_source *s)
{
        (void)d;
        wl_data_source_destroy(s);
}
static void source_dnd_drop(void *d, struct wl_data_source *s)
{
        (void)d, (void)s;
}
static void source_dnd_finished(void *d, struct wl_data_source *s)
{
        (void)d;
        wl_data_source_destroy(s);
}
static void source_action(void *d, struct wl_data_source *s, uint32_t a)
{
        (void)d, (void)s, (void)a;
}
static const struct wl_data_source_listener source_listener = {source_target, source_send, source_cancelled, source_dnd_drop,
                                                               source_dnd_finished, source_action};

static void offer_offer(void *d, struct wl_data_offer *o, const char *m)
{
        (void)d, (void)o, (void)m;
}
static void offer_source_actions(void *d, struct wl_data_offer *o, uint32_t a)
{
        (void)d, (void)o, (void)a;
}
static void offer_action(void *d, struct wl_data_offer *o, uint32_t a)
{
        (void)d, (void)o, (void)a;
}
static const struct wl_data_offer_listener offer_listener = {offer_offer, offer_source_actions, offer_action};

static void dd_data_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
        (void)d, (void)dd;
        wl_data_offer_add_listener(o, &offer_listener, NULL);
}
static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{
        (void)d, (void)dd, (void)s, (void)x, (void)y;
        if (!o)
                return;
        wl_data_offer_accept(o, serial, "text/plain;charset=utf-8");
        if (ddm_version >= 3)
                wl_data_offer_set_actions(o, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        offer_now = o, offer_entered = 1;
}
static void dd_leave(void *d, struct wl_data_device *dd)
{
        (void)d, (void)dd;
}
static void dd_motion(void *d, struct wl_data_device *dd, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
        (void)d, (void)dd, (void)t, (void)x, (void)y;
}
static void dd_drop(void *d, struct wl_data_device *dd)
{
        (void)d, (void)dd;
        offer_dropped = 1;
}
static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
        (void)d, (void)dd;
        if (o && o != offer_now)
        {
                if (offer_now && offer_is_selection)
                        wl_data_offer_destroy(offer_now);
                offer_now = o, offer_is_selection = 1;
        }
}
static const struct wl_data_device_listener dd_listener = {dd_data_offer, dd_enter, dd_leave, dd_motion, dd_drop, dd_selection};

// Read the offer's text through a pipe to the end, dispatching meanwhile (the source is this client). The bytes read, -1 on failure.
static long receive_all(struct wl_data_offer *o)
{
        int p[2];
        if (pipe2(p, O_CLOEXEC) < 0)
                return -1;
        wl_data_offer_receive(o, "text/plain;charset=utf-8", p[1]);
        close(p[1]);
        wl_display_flush(display);
        long total = 0;
        static char chunk[65536];
        uint64_t give_up = now_ns() + 5000000000ull;
        for (;;)
        {
                struct pollfd f[2] = {{p[0], POLLIN, 0}, {wl_display_get_fd(display), POLLIN, 0}};
                if (now_ns() > give_up)
                        break;
                wl_display_dispatch_pending(display);
                wl_display_flush(display);
                if (poll(f, 2, 100) <= 0)
                        continue;
                if (f[1].revents & POLLIN)
                        wl_display_dispatch(display);
                if (f[0].revents & (POLLIN | POLLHUP))
                {
                        ssize_t n = read(p[0], chunk, sizeof(chunk));
                        if (n <= 0)
                                break;
                        total += n;
                }
        }
        close(p[0]);
        return total;
}

static int wait_for(int *flag, double secs)
{
        uint64_t end = now_ns() + (uint64_t)(secs * 1e9);
        while (!*flag && now_ns() < end)
        {
                wl_display_flush(display);
                struct pollfd f = {wl_display_get_fd(display), POLLIN, 0};
                if (poll(&f, 1, 20) > 0 && wl_display_dispatch(display) < 0)
                        return 0;
                wl_display_dispatch_pending(display);
        }
        return *flag;
}

static int run_clip(void)
{
        if (!seat || !ddm)
        {
                printf("NOTE no wl_seat or wl_data_device_manager: clipboard skipped\n");
                return 3;
        }
        data_device = wl_data_device_manager_get_data_device(ddm, seat);
        wl_data_device_add_listener(data_device, &dd_listener, NULL);
        payload_data = malloc((size_t)payload);
        for (long i = 0; i < payload; i++)
                payload_data[i] = (char)('a' + i % 26);
        if (fifo >= 0 && !keyboard_focus)
        {
                inject(1, 30, 1); // KEY_A, for a serial
                inject(1, 30, 0);
        }
        wait_for(&keyboard_focus, 2.0);
        wl_display_roundtrip(display);
        if (!keyboard_focus)
        {
                printf("NOTE the window has no keyboard focus here: clipboard skipped\n");
                return 3;
        }
        warm_until = 0;
        s_cb.n = 0;
        long bad = 0;
        for (int i = 0; i < count; i++)
        {
                struct wl_data_source *src = wl_data_device_manager_create_data_source(ddm);
                wl_data_source_add_listener(src, &source_listener, NULL);
                wl_data_source_offer(src, "text/plain;charset=utf-8");
                struct wl_data_offer *before = offer_now;
                if (fifo >= 0)
                {
                        // The person's key just on the window: what a compositor that takes a copy only from a gesture asks.
                        uint32_t was = serial_key;
                        inject(1, 30, 1);
                        inject(1, 30, 0);
                        uint64_t until_key = now_ns() + 500000000ull;
                        while (serial_key == was && now_ns() < until_key)
                        {
                                wl_display_flush(display);
                                struct pollfd f = {wl_display_get_fd(display), POLLIN, 0};
                                if (poll(&f, 1, 10) > 0)
                                        wl_display_dispatch(display);
                        }
                }
                t_op = now_ns();
                wl_data_device_set_selection(data_device, src, serial_key);
                uint64_t end = now_ns() + 2000000000ull;
                while (offer_now == before && now_ns() < end)
                {
                        wl_display_flush(display);
                        struct pollfd f = {wl_display_get_fd(display), POLLIN, 0};
                        if (poll(&f, 1, 20) > 0)
                                wl_display_dispatch(display);
                }
                if (offer_now == before)
                {
                        printf("NOTE the selection was not offered back within 2 s: clipboard stopped at %d\n", i);
                        break;
                }
                uint64_t t_offer = now_ns();
                long n = receive_all(offer_now);
                if (n != payload)
                        bad++;
                push(&s_op, (double)(now_ns() - t_op) / 1e6);
                push(&s_cb, (double)(t_offer - t_op) / 1e6);
                ops_done++;
        }
        op_bytes_bad = bad;
        // Drag and drop: only where the client can press the button itself (the FIFO).
        long dnd = 0, dnd_bad = 0;
        if (fifo >= 0 && pointer)
        {
                for (int i = 0; i < 20 && !pointer_focus; i++)
                {
                        inject(2, 0, (i & 1) ? 6 : -6);
                        wait_for(&pointer_in, 0.05);
                }
                if (!pointer_focus)
                        printf("NOTE the pointer never entered the window: drag and drop not run\n");
                for (int i = 0; i < count && pointer_focus; i++)
                {
                        button_down = 0;
                        inject(1, 272, 1); // BTN_LEFT
                        if (!wait_for(&button_down, 1.0))
                                break;
                        struct wl_data_source *src = wl_data_device_manager_create_data_source(ddm);
                        wl_data_source_add_listener(src, &source_listener, NULL);
                        wl_data_source_offer(src, "text/plain;charset=utf-8");
                        if (ddm_version >= 3)
                                wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
                        offer_entered = 0, offer_dropped = 0, offer_is_selection = 0;
                        uint64_t t0 = now_ns();
                        wl_data_device_start_drag(data_device, src, win[0].s, NULL, serial_button);
                        wl_display_flush(display);
                        inject(2, 0, (i & 1) ? 2 : -2);
                        if (!wait_for(&offer_entered, 1.0))
                        {
                                inject(1, 272, 0);
                                printf("NOTE a drag did not enter the window it began in: drag and drop stopped at %d\n", i);
                                break;
                        }
                        push(&s_op2, (double)(now_ns() - t0) / 1e6);
                        inject(1, 272, 0);
                        if (!wait_for(&offer_dropped, 1.0))
                        {
                                printf("NOTE a drop did not come: drag and drop stopped at %d\n", i);
                                break;
                        }
                        long n = receive_all(offer_now);
                        if (ddm_version >= 3)
                                wl_data_offer_finish(offer_now);
                        wl_data_offer_destroy(offer_now);
                        offer_now = NULL;
                        if (n != payload)
                                dnd_bad++;
                        push(&s_input, (double)(now_ns() - t0) / 1e6);
                        dnd++;
                }
        }
        else
                printf("NOTE no input FIFO: drag and drop not run (it needs a button the client can press)\n");
        printf("RESULT mode=clip copies=%ld copy_p50=%.3f copy_p99=%.3f offer_p50=%.3f bad=%ld bytes=%ld dnd=%ld dnd_enter_p50=%.3f "
               "dnd_p50=%.3f dnd_p99=%.3f dnd_bad=%ld\n",
               ops_done, pct(&s_op, 50), pct(&s_op, 99), pct(&s_cb, 50), op_bytes_bad, payload, dnd, pct(&s_op2, 50), pct(&s_input, 50),
               pct(&s_input, 99), dnd_bad);
        return 0;
}

static void report(void)
{
        double span = (double)(now_ns() - t_start) / 1e9;
        printf("RESULT mode=%s windows=%d buffers=%d size=%dx%d presentation=%d refresh_ms=%.3f frames=%ld fps=%.2f "
               "frame_p50=%.3f frame_p99=%.3f dropped=%ld late=%ld discarded=%ld stalls=%ld c2p_p50=%.3f c2p_p99=%.3f "
               "cb_p50=%.3f cb_p99=%.3f cb_after_present_p50=%.3f commits_per_s=%.1f flags=0x%lx",
               mode, windows, nbuffers, want_w, want_h, presentation != NULL, refresh_ns / 1e6, presented_n,
               s_interval.n ? 1000.0 / pct(&s_interval, 50) : 0, pct(&s_interval, 50), pct(&s_interval, 99), dropped, late,
               discarded, stalls, pct(&s_c2p, 50), pct(&s_c2p, 99), pct(&s_cb, 50), pct(&s_cb, 99), pct(&s_cb_after, 50),
               (double)commits / span, (unsigned long)flags_or);
        if (!strcmp(mode, "lock"))
                printf(" locked=%d injected=%ld received=%ld lost=%ld event_p50=%.3f event_p99=%.3f input_p50=%.3f input_p99=%.3f", is_locked,
                       injected, received, lost, pct(&s_event, 50), pct(&s_event, 99), pct(&s_input, 50), pct(&s_input, 99));
        if (!strcmp(mode, "sub") || !strcmp(mode, "popup"))
                printf(" churned=%ld churn_per_s=%.1f op_p50=%.3f op_p99=%.3f shown=%ld shown_p50=%.3f shown_p99=%.3f", churned,
                       (double)churned / span, pct(&s_op, 50), pct(&s_op, 99), popups_shown, pct(&s_op2, 50), pct(&s_op2, 99));
        printf("\n");
}

static void usage(void)
{
        fprintf(stderr, "usage: wl-stress -m anim|dmabuf|resize|sub|popup|lock|clip|first [-t S] [-w W] [-h H] [-N N] [-B 2|3] [-R] [-A] [-F FIFO] [-S BYTES] [-n COUNT]\n");
        exit(2);
}

int main(int argc, char **argv)
{
        int c;
        uint64_t t_exec = now_ns();
        while ((c = getopt(argc, argv, "m:t:w:h:N:B:RAF:S:n:")) != -1)
                switch (c)
                {
                case 'm': mode = optarg; break;
                case 't': seconds = atof(optarg); break;
                case 'w': want_w = atoi(optarg); break;
                case 'h': want_h = atoi(optarg); break;
                case 'N': windows = atoi(optarg); break;
                case 'B': nbuffers = atoi(optarg); break;
                case 'R': band = 1; break;
                case 'A': alpha = 1; break;
                case 'F': fifo_path = optarg; break;
                case 'S': payload = atol(optarg); break;
                case 'n': count = atoi(optarg); break;
                default: usage();
                }
        if (windows < 1 || windows > MAXWIN || nbuffers < 2 || nbuffers > 3)
                usage();
        signal(SIGPIPE, SIG_IGN); // a paste's reader that goes is a write that fails, not the end of the bench
        if (fifo_path)
                fifo = open(fifo_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        t_connect = now_ns();
        display = wl_display_connect(NULL);
        if (!display)
        {
                fprintf(stderr, "cannot connect\n");
                return 2;
        }
        struct wl_registry *registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, NULL);
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);
        if (!compositor || !shm || !wm_base)
        {
                fprintf(stderr, "missing globals\n");
                return 2;
        }
        if (!presentation)
                printf("NOTE no wp_presentation: frame times are the frame callbacks' arrival by the client's clock\n");
        t_start = now_ns();
        warm_until = t_start + 500000000ull; // the first half second is not counted
        t_end = t_start + (uint64_t)(seconds * 1e9);

        if (!strcmp(mode, "dmabuf"))
        {
#ifdef HAVE_DMABUF
                int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
                if (fd < 0 || !dmabuf || !(gbm = gbm_create_device(fd)))
                {
                        printf("NOTE no render node or no linux-dmabuf here: dmabuf skipped\n");
                        return 3;
                }
                use_dmabuf = 1;
#else
                printf("NOTE built without gbm: dmabuf skipped\n");
                return 3;
#endif
        }
        if (!strcmp(mode, "first"))
        {
                windows = 1;
                win_open(&win[0], want_w, want_h, 0);
                warm_until = 0;
                while (!first_present_ns && wl_display_dispatch(display) >= 0)
                        ;
                const char *t0 = getenv("STRESS_T0");
                uint64_t launch = t0 ? strtoull(t0, NULL, 10) : 0;
                printf("RESULT mode=first presentation=%d connect_to_present_ms=%.3f exec_to_present_ms=%.3f launch_to_present_ms=%.3f\n",
                       presentation != NULL, (double)(first_present_ns - t_connect) / 1e6, (double)(first_present_ns - t_exec) / 1e6,
                       launch ? (double)(first_present_ns - launch) / 1e6 : -1.0);
                return 0;
        }
        if (!strcmp(mode, "clip"))
        {
                windows = 1;
                win_open(&win[0], want_w, want_h, fifo >= 0);
                for (int i = 0; i < 20 && !win[0].configured; i++)
                        wl_display_roundtrip(display);
                running = 0; // no animation
                return run_clip();
        }
        if (!strcmp(mode, "resize"))
        {
                windows = 1;
                rmax_w = want_w, rmax_h = want_h;
                pool_slot = (size_t)rmax_w * (size_t)rmax_h * 4;
                pool_fd = shm_file(pool_slot * 3);
                pool_data = mmap(NULL, pool_slot * 3, PROT_READ | PROT_WRITE, MAP_SHARED, pool_fd, 0);
                pool = wl_shm_create_pool(shm, pool_fd, (int32_t)(pool_slot * 3));
                resize_mode = 1, next_frame = resize_commit;
                win_open(&win[0], 400, 300, 0);
                int st = loop();
                report();
                return st;
        }
        if (!strcmp(mode, "sub") || !strcmp(mode, "popup"))
        {
                windows = 1;
                if (!strcmp(mode, "sub") && !subcompositor)
                {
                        printf("NOTE no wl_subcompositor: sub skipped\n");
                        return 3;
                }
                for (int i = 0; i < 2; i++)
                {
                        int save = alpha;
                        alpha = 0;
                        make_shm(&child_buf[i], 64, 64);
                        alpha = save;
                        draw(&child_buf[i], 64, (unsigned)i * 40, 0, 64);
                        child_buf[i].busy = 0;
                        // These are attached to many surfaces; the release of one says nothing, so they are never held back.
                }
                before_commit = !strcmp(mode, "sub") ? sub_churn : popup_churn;
                win_open(&win[0], want_w, want_h, 0);
                int st = loop();
                report();
                return st;
        }
        if (!strcmp(mode, "lock"))
        {
                windows = 1;
                win_open(&win[0], want_w, want_h, 1);
                for (int i = 0; i < 20 && !win[0].configured; i++)
                        wl_display_roundtrip(display);
                // A seat's pointer may come a moment after the seat (a compositor whose devices are made as it runs says so then).
                for (int i = 0; i < 20 && seat && !pointer; i++)
                {
                        if (fifo >= 0)
                                inject(2, 0, 1);
                        wl_display_roundtrip(display);
                        usleep(20000);
                }
                if (!pointer || !relative_manager || !constraints)
                        printf("NOTE no pointer, relative pointer or pointer constraints: the lock is not taken\n");
                else
                {
                        if (fifo >= 0)
                        {
                                inject(2, 0, 1); // the pointer moves onto the fullscreen window
                                wait_for(&pointer_in, 1.0);
                        }
                        struct zwp_relative_pointer_v1 *rel = zwp_relative_pointer_manager_v1_get_relative_pointer(relative_manager, pointer);
                        zwp_relative_pointer_v1_add_listener(rel, &rel_listener, NULL);
                        locked = zwp_pointer_constraints_v1_lock_pointer(constraints, win[0].s, pointer, NULL,
                                                                         ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
                        zwp_locked_pointer_v1_add_listener(locked, &lock_listener, NULL);
                        wl_surface_commit(win[0].s);
                        // A lock holds once the pointer is over the window: nudged until it is, a second at most.
                        for (int i = 0; i < 50 && !is_locked; i++)
                        {
                                if (fifo >= 0)
                                        inject(2, 0, (i & 1) ? 4 : -4);
                                uint64_t until_ms = now_ns() + 20000000ull;
                                while (!is_locked && now_ns() < until_ms)
                                {
                                        wl_display_flush(display);
                                        struct pollfd f = {wl_display_get_fd(display), POLLIN, 0};
                                        if (poll(&f, 1, 5) > 0)
                                                wl_display_dispatch(display);
                                        wl_display_dispatch_pending(display);
                                }
                        }
                        if (!is_locked)
                                printf("NOTE the pointer lock did not hold within a second\n");
                }
                if (fifo >= 0)
                {
                        on_timer = lock_tick;
                        timer_every(7300000); // 7.3 ms: input at every phase of the frame
                }
                else
                        printf("NOTE no input FIFO: input to frame is not measured (no way to inject input here)\n");
                int st = loop();
                report();
                return st;
        }
        // anim and dmabuf
        int cols = (int)ceil(sqrt((double)windows));
        int ww = windows > 1 ? want_w / cols : want_w, wh = windows > 1 ? want_h / cols : want_h;
        for (int i = 0; i < windows; i++)
                win_open(&win[i], ww, wh, 0);
        want_w = ww, want_h = wh;
        int st = loop();
        report();
        return st;
}
