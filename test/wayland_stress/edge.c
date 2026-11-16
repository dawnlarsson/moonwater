/*
        The edge cases of the stress bench (test/wayland_stress/run.py): one case a run, on the real libwayland-client, each a
        thing Weston, KWin and Mutter are expected to come through, and what this compositor made of it.

            wl-edge -c CASE [-F FIFO]
            wl-edge -l              the cases, one a line

        A case prints one line, "CASE name outcome=O ms=T detail=...". O is what this client saw the compositor do: ok (it went
        through), error:INTERFACE:CODE (a protocol error was raised on this client), closed (the connection ended with no error
        said), stall (a client sharing the compositor waited more than half a second for a frame), timeout, or skip (the case
        needs what this compositor or this run does not have: a seat, a FIFO to press keys through). T is the case's own time:
        the recovery (a fresh window's first frame) after a client was killed, the worst frame gap of a client sharing the
        compositor with a bad one, or the case's work. Whether the compositor lived is the runner's to judge. -F names the evdev
        FIFO a compositor reads (Moonwater headless's -i), for the cases that press keys or move the pointer.
        Exit 0, or 2 when it cannot connect.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client.h"
#include "relative-pointer-unstable-v1-client.h"
#include "viewporter-client.h"
#include "xdg-shell-client.h"

static uint64_t now_ns(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static double ms_since(uint64_t t)
{
        return (double)(now_ns() - t) / 1e6;
}

static int fifo = -1;
static char detail[256] = "-";

static void say(const char *name, const char *outcome, double ms)
{
        printf("CASE %s outcome=%s ms=%.3f detail=%s\n", name, outcome, ms, detail);
        fflush(stdout);
}

/* ---- evdev records to the compositor's FIFO ---- */
static void inject(int type, int code, int value)
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
        if (fifo >= 0 && write(fifo, e, sizeof(e)) < 0)
                return;
}

/* ---- one connection and what it binds ---- */
struct conn
{
        struct wl_display *d;
        struct wl_compositor *comp;
        struct wl_subcompositor *sub;
        struct wl_shm *shm;
        struct xdg_wm_base *wm;
        struct wl_seat *seat;
        struct wp_viewporter *viewporter;
        struct wl_data_device_manager *ddm;
        struct zwp_relative_pointer_manager_v1 *rel;
        struct zwp_pointer_constraints_v1 *cons;
};

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
        (void)d;
        xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static uint32_t vmin(uint32_t a, uint32_t b)
{
        return a < b ? a : b;
}

static void global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
        struct conn *c = data;
        if (!strcmp(iface, "wl_compositor"))
                c->comp = wl_registry_bind(r, name, &wl_compositor_interface, vmin(4, version));
        else if (!strcmp(iface, "wl_subcompositor"))
                c->sub = wl_registry_bind(r, name, &wl_subcompositor_interface, 1);
        else if (!strcmp(iface, "wl_shm"))
                c->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
        else if (!strcmp(iface, "xdg_wm_base"))
        {
                c->wm = wl_registry_bind(r, name, &xdg_wm_base_interface, vmin(5, version));
                xdg_wm_base_add_listener(c->wm, &wm_listener, NULL);
        }
        else if (!strcmp(iface, "wl_seat") && !c->seat)
                c->seat = wl_registry_bind(r, name, &wl_seat_interface, vmin(5, version));
        else if (!strcmp(iface, "wp_viewporter"))
                c->viewporter = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
        else if (!strcmp(iface, "wl_data_device_manager"))
                c->ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, vmin(3, version));
        else if (!strcmp(iface, "zwp_relative_pointer_manager_v1"))
                c->rel = wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1);
        else if (!strcmp(iface, "zwp_pointer_constraints_v1"))
                c->cons = wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t n)
{
        (void)d, (void)r, (void)n;
}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static int connect_to(struct conn *c)
{
        memset(c, 0, sizeof(*c));
        c->d = wl_display_connect(NULL);
        if (!c->d)
                return 0;
        wl_registry_add_listener(wl_display_get_registry(c->d), &registry_listener, c);
        wl_display_roundtrip(c->d);
        return c->comp && c->shm && c->wm;
}

// What the connection's state says: ok, error:INTERFACE:CODE or closed.
static const char *state_of(struct conn *c)
{
        static char out[96];
        int e = wl_display_get_error(c->d);
        if (!e)
                return "ok";
        if (e == EPROTO)
        {
                const struct wl_interface *iface = NULL;
                uint32_t id = 0, code = wl_display_get_protocol_error(c->d, &iface, &id);
                snprintf(out, sizeof(out), "error:%s:%u", iface ? iface->name : "?", code);
                return out;
        }
        return "closed";
}

// Dispatch until *flag is set, `ms` at most: 1 when it was, 0 on a timeout or when the connection failed.
static int wait_flag(struct conn *c, volatile int *flag, int ms)
{
        uint64_t end = now_ns() + (uint64_t)ms * 1000000ull;
        while (!*flag)
        {
                if (wl_display_get_error(c->d) || now_ns() >= end)
                        return 0;
                while (wl_display_prepare_read(c->d) != 0)
                        wl_display_dispatch_pending(c->d);
                wl_display_flush(c->d);
                struct pollfd p = {wl_display_get_fd(c->d), POLLIN, 0};
                int left = (int)((end - now_ns()) / 1000000ull);
                if (poll(&p, 1, left < 1 ? 1 : left) > 0)
                {
                        if (wl_display_read_events(c->d) < 0)
                                return 0;
                }
                else
                        wl_display_cancel_read(c->d);
                wl_display_dispatch_pending(c->d);
        }
        return 1;
}

static void sync_done(void *data, struct wl_callback *cb, uint32_t t)
{
        (void)t;
        *(int *)data = 1;
        wl_callback_destroy(cb);
}
static const struct wl_callback_listener sync_listener = {sync_done};

// A roundtrip that gives up after `ms`: 1 when it came back.
static int roundtrip_within(struct conn *c, int ms)
{
        static int done;
        done = 0;
        struct wl_callback *cb = wl_display_sync(c->d);
        wl_callback_add_listener(cb, &sync_listener, &done);
        return wait_flag(c, &done, ms);
}

/* ---- buffers ---- */
static struct wl_buffer *shm_buffer_at(struct conn *c, int w, int h, int stride, int offset, uint32_t format, int pool_bytes, uint32_t colour,
                                       int *fd_out)
{
        int fd = memfd_create("wl-edge", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, pool_bytes) < 0)
                return NULL;
        uint32_t *px = mmap(NULL, (size_t)pool_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (px != MAP_FAILED)
        {
                for (int i = 0; i < pool_bytes / 4; i++)
                        px[i] = colour;
                munmap(px, (size_t)pool_bytes);
        }
        struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, pool_bytes);
        struct wl_buffer *b = wl_shm_pool_create_buffer(pool, offset, w, h, stride, format);
        wl_shm_pool_destroy(pool);
        if (fd_out)
                *fd_out = fd;
        else
                close(fd);
        return b;
}

static struct wl_buffer *shm_buffer(struct conn *c, int w, int h, uint32_t colour)
{
        return shm_buffer_at(c, w, h, w * 4, 0, WL_SHM_FORMAT_XRGB8888, w * h * 4 > 0 ? w * h * 4 : 4, colour, NULL);
}

/* ---- windows ---- */
struct win
{
        struct conn *c;
        struct wl_surface *s;
        struct xdg_surface *xs;
        struct xdg_toplevel *top;
        int configured, ack, closed, configures;
        uint32_t serial;
        int32_t cw, ch;
};

static void xs_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
        struct win *w = data;
        w->serial = serial, w->configures++;
        if (w->ack)
                xdg_surface_ack_configure(xs, serial);
        w->configured = 1;
}
static const struct xdg_surface_listener xs_listener = {xs_configure};

static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s)
{
        (void)t, (void)s;
        ((struct win *)data)->cw = w, ((struct win *)data)->ch = h;
}
static void top_close(void *data, struct xdg_toplevel *t)
{
        (void)t;
        ((struct win *)data)->closed = 1;
}
static void top_bounds(void *d, struct xdg_toplevel *t, int32_t w, int32_t h)
{
        (void)d, (void)t, (void)w, (void)h;
}
static void top_caps(void *d, struct xdg_toplevel *t, struct wl_array *a)
{
        (void)d, (void)t, (void)a;
}
static const struct xdg_toplevel_listener top_listener = {top_configure, top_close, top_bounds, top_caps};

// A toplevel, configured (acknowledged when ack), not yet given a buffer. 0 when no configure came in 2 s.
static int open_win(struct conn *c, struct win *w, int ack)
{
        memset(w, 0, sizeof(*w));
        w->c = c, w->ack = ack;
        w->s = wl_compositor_create_surface(c->comp);
        w->xs = xdg_wm_base_get_xdg_surface(c->wm, w->s);
        xdg_surface_add_listener(w->xs, &xs_listener, w);
        w->top = xdg_surface_get_toplevel(w->xs);
        xdg_toplevel_add_listener(w->top, &top_listener, w);
        xdg_toplevel_set_title(w->top, "wl-edge");
        wl_surface_commit(w->s);
        return wait_flag(c, &w->configured, 2000);
}

// A frame of the surface: the buffer attached and damaged, and its frame callback waited for (`ms` at most). 1 when it came.
static int frame_of(struct conn *c, struct wl_surface *s, struct wl_buffer *b, int w, int h, int ms)
{
        static int done;
        done = 0;
        if (b)
                wl_surface_attach(s, b, 0, 0);
        wl_surface_damage_buffer(s, 0, 0, w, h);
        struct wl_callback *cb = wl_surface_frame(s);
        wl_callback_add_listener(cb, &sync_listener, &done);
        wl_surface_commit(s);
        return wait_flag(c, &done, ms);
}

/* A new client's window to its first frame, after whatever a case did: the recovery time, or -1. */
static double recovery(void)
{
        uint64_t t = now_ns();
        struct conn c;
        struct win w;
        if (!connect_to(&c))
                return -1;
        if (!open_win(&c, &w, 1))
        {
                wl_display_disconnect(c.d);
                return -1;
        }
        int ok = frame_of(&c, w.s, shm_buffer(&c, 200, 150, 0xff336699u), 200, 150, 2000);
        double ms = ms_since(t);
        wl_display_disconnect(c.d);
        return ok ? ms : -1;
}

/* A client that animates for `ms` beside whatever another does: the worst gap between its frames, in ms (-1: none came). */
static double animate_beside(int ms, int *frames_out)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1))
                return -1;
        struct wl_buffer *b[2] = {shm_buffer(&c, 320, 240, 0xff204060u), shm_buffer(&c, 320, 240, 0xff604020u)};
        uint64_t end = now_ns() + (uint64_t)ms * 1000000ull, last = now_ns();
        double worst = 0;
        int frames = 0;
        while (now_ns() < end)
        {
                if (!frame_of(&c, w.s, b[frames & 1], 320, 240, 1500))
                {
                        worst = ms_since(last);
                        break;
                }
                double gap = ms_since(last);
                if (frames > 2 && gap > worst)
                        worst = gap;
                last = now_ns();
                frames++;
        }
        if (frames_out)
                *frames_out = frames;
        wl_display_disconnect(c.d);
        return frames ? worst : -1;
}

/* ---- a client killed in the middle of something: the child does it and is killed, the parent judges the recovery ---- */
static void die(struct conn *c)
{
        wl_display_flush(c->d);
        kill(getpid(), SIGKILL);
}

static void child_commit(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1))
                _exit(1);
        // Attached and damaged, the commit half sent: the request's bytes cut in the middle by the kill.
        wl_surface_attach(w.s, shm_buffer(&c, 400, 300, 0xff00ff00u), 0, 0);
        wl_surface_damage_buffer(w.s, 0, 0, 400, 300);
        wl_display_flush(c.d);
        int fd = wl_display_get_fd(c.d);
        uint32_t half[1] = {wl_proxy_get_id((struct wl_proxy *)w.s)};
        if (write(fd, half, 4) < 0)
                _exit(1);
        kill(getpid(), SIGKILL);
}

static void child_resize(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1))
                _exit(1);
        for (int i = 0; i < 40; i++)
        {
                int ww = 300 + i * 13, hh = 200 + i * 7;
                wl_surface_attach(w.s, shm_buffer(&c, ww, hh, 0xff000000u | (uint32_t)i * 5000u), 0, 0);
                wl_surface_damage_buffer(w.s, 0, 0, ww, hh);
                xdg_surface_set_window_geometry(w.xs, 0, 0, ww, hh);
                wl_surface_commit(w.s);
        }
        die(&c);
}

static void child_popup(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1) || !frame_of(&c, w.s, shm_buffer(&c, 400, 300, 0xff808000u), 400, 300, 2000))
                _exit(1);
        struct xdg_positioner *p = xdg_wm_base_create_positioner(c.wm);
        xdg_positioner_set_size(p, 100, 80);
        xdg_positioner_set_anchor_rect(p, 10, 10, 1, 1);
        struct wl_surface *ps = wl_compositor_create_surface(c.comp);
        struct xdg_surface *pxs = xdg_wm_base_get_xdg_surface(c.wm, ps);
        struct win pw = {0};
        pw.ack = 1;
        xdg_surface_add_listener(pxs, &xs_listener, &pw);
        struct xdg_popup *pop = xdg_surface_get_popup(pxs, w.xs, p);
        (void)pop;
        wl_surface_commit(ps);
        wait_flag(&c, &pw.configured, 1000);
        frame_of(&c, ps, shm_buffer(&c, 100, 80, 0xffff0000u), 100, 80, 1000);
        die(&c);
}

static void child_lock(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1) || !c.seat || !c.cons)
                _exit(1);
        xdg_toplevel_set_fullscreen(w.top, NULL);
        wl_surface_commit(w.s);
        roundtrip_within(&c, 500);
        frame_of(&c, w.s, shm_buffer(&c, 800, 600, 0xff000080u), 800, 600, 1000);
        struct wl_pointer *pointer = wl_seat_get_pointer(c.seat);
        zwp_pointer_constraints_v1_lock_pointer(c.cons, w.s, pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        if (c.rel)
                zwp_relative_pointer_manager_v1_get_relative_pointer(c.rel, pointer);
        wl_surface_commit(w.s);
        for (int i = 0; i < 10; i++)
        {
                inject(2, 0, (i & 1) ? 2 : -2);
                roundtrip_within(&c, 50);
        }
        die(&c);
}

static void source_send(void *d, struct wl_data_source *s, const char *m, int32_t fd)
{
        (void)d, (void)s, (void)m;
        // Half the text, and the source killed with the pipe still open.
        static char text[2048];
        memset(text, 'x', sizeof(text));
        if (write(fd, text, sizeof(text)) < 0)
                _exit(1);
        kill(getpid(), SIGKILL);
}
static void source_nop(void *d, struct wl_data_source *s)
{
        (void)d, (void)s;
}
static void source_target(void *d, struct wl_data_source *s, const char *m)
{
        (void)d, (void)s, (void)m;
}
static void source_action(void *d, struct wl_data_source *s, uint32_t a)
{
        (void)d, (void)s, (void)a;
}
static const struct wl_data_source_listener source_listener = {source_target, source_send, source_nop, source_nop, source_nop, source_action};

static uint32_t key_serial;
static int key_focus;
static void k_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t size)
{
        (void)d, (void)k, (void)f, (void)size;
        close(fd);
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys)
{
        (void)d, (void)k, (void)s, (void)keys;
        key_serial = serial, key_focus = 1;
}
static void k_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
        (void)d, (void)k, (void)serial, (void)s;
        key_focus = 0;
}
static int keys_down;
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
        (void)d, (void)k, (void)t, (void)key;
        key_serial = serial;
        keys_down += state ? 1 : -1;
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

static void child_clipboard(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1) || !c.seat || !c.ddm)
                _exit(1);
        struct wl_keyboard *kb = wl_seat_get_keyboard(c.seat);
        wl_keyboard_add_listener(kb, &keyboard_listener, NULL);
        frame_of(&c, w.s, shm_buffer(&c, 400, 300, 0xff008080u), 400, 300, 1000);
        wait_flag(&c, &key_focus, 1000);
        inject(1, 30, 1);
        inject(1, 30, 0);
        roundtrip_within(&c, 200);
        struct wl_data_device *dd = wl_data_device_manager_get_data_device(c.ddm, c.seat);
        struct wl_data_source *src = wl_data_device_manager_create_data_source(c.ddm);
        wl_data_source_add_listener(src, &source_listener, NULL);
        wl_data_source_offer(src, "text/plain;charset=utf-8");
        wl_data_device_set_selection(dd, src, key_serial);
        // The compositor (Moonwater reads a copy itself) or the next paste asks for the text, and the source dies half way.
        static int never;
        wait_flag(&c, &never, 1500);
        die(&c);
}

static void child_drag(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1) || !c.seat || !c.ddm)
                _exit(1);
        frame_of(&c, w.s, shm_buffer(&c, 400, 300, 0xff800080u), 400, 300, 1000);
        struct wl_data_device *dd = wl_data_device_manager_get_data_device(c.ddm, c.seat);
        struct wl_data_source *src = wl_data_device_manager_create_data_source(c.ddm);
        wl_data_source_add_listener(src, &source_listener, NULL);
        wl_data_source_offer(src, "text/plain;charset=utf-8");
        wl_data_source_set_actions(src, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        inject(1, 272, 1);
        roundtrip_within(&c, 200);
        wl_data_device_start_drag(dd, src, w.s, NULL, key_serial);
        inject(2, 0, 5);
        roundtrip_within(&c, 200);
        die(&c);
}

static void case_killed(const char *name, void (*child)(void))
{
        pid_t pid = fork();
        if (pid == 0)
        {
                child();
                _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        snprintf(detail, sizeof(detail), "child_%s", WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL ? "killed" : "ended_first");
        // The pointer free again after a lock its client did not live to let go of: a motion reaches a new window.
        double ms = recovery();
        say(name, ms >= 0 ? "ok" : "timeout", ms);
}

/* ---- a bad client beside a good one: the good one's worst frame gap ---- */
static void child_frame_forever(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1))
                _exit(1);
        wl_surface_attach(w.s, shm_buffer(&c, 400, 300, 0xff111111u), 0, 0);
        wl_surface_damage_buffer(w.s, 0, 0, 400, 300);
        wl_surface_frame(w.s);
        wl_surface_commit(w.s);
        wl_display_flush(c.d);
        pause(); // never reads again: its socket fills with what the compositor sends
}

static void child_never_ack(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 0))
                _exit(1);
        for (;;)
        {
                xdg_toplevel_set_maximized(w.top);
                xdg_toplevel_unset_maximized(w.top);
                wl_surface_commit(w.s);
                wl_display_dispatch_pending(c.d);
                wl_display_flush(c.d);
                usleep(5000);
                if (wl_display_prepare_read(c.d) == 0)
                {
                        struct pollfd p = {wl_display_get_fd(c.d), POLLIN, 0};
                        if (poll(&p, 1, 0) > 0)
                                wl_display_read_events(c.d);
                        else
                                wl_display_cancel_read(c.d);
                }
        }
}

static void child_flood_sync(void)
{
        struct conn c;
        if (!connect_to(&c))
                _exit(1);
        for (;;)
        {
                for (int i = 0; i < 1000; i++)
                        wl_callback_destroy(wl_display_sync(c.d));
                if (wl_display_flush(c.d) < 0 && errno != EAGAIN)
                        _exit(3); // the compositor ended the connection
        }
}

static void child_flood_damage(void)
{
        struct conn c;
        struct win w;
        if (!connect_to(&c) || !open_win(&c, &w, 1))
                _exit(1);
        wl_surface_attach(w.s, shm_buffer(&c, 400, 300, 0xff222222u), 0, 0);
        for (;;)
        {
                for (int i = 0; i < 1000; i++)
                        wl_surface_damage_buffer(w.s, i % 400, i % 300, 3, 3);
                wl_surface_commit(w.s);
                if (wl_display_flush(c.d) < 0 && errno != EAGAIN)
                        _exit(3);
                wl_display_dispatch_pending(c.d);
        }
}

static void case_beside(const char *name, void (*child)(void))
{
        pid_t pid = fork();
        if (pid == 0)
        {
                child();
                _exit(0);
        }
        usleep(300000);
        int frames = 0;
        double worst = animate_beside(2000, &frames);
        int status = 0;
        int gone = waitpid(pid, &status, WNOHANG) == pid;
        if (!gone)
        {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
        }
        snprintf(detail, sizeof(detail), "frames=%d bad_client=%s", frames,
                 gone ? (WIFEXITED(status) && WEXITSTATUS(status) == 3 ? "disconnected" : "ended") : "still_connected");
        say(name, worst < 0 ? "timeout" : worst > 500 ? "stall" : "ok", worst);
}

/* ---- requests that are not well formed, on a socket of its own: what the compositor answers ---- */
static int raw_connect(void)
{
        const char *name = getenv("WAYLAND_DISPLAY"), *dir = getenv("XDG_RUNTIME_DIR");
        struct sockaddr_un a = {0};
        a.sun_family = AF_UNIX;
        if (!name)
                name = "wayland-0";
        if (name[0] == '/')
                snprintf(a.sun_path, sizeof(a.sun_path), "%s", name);
        else
                snprintf(a.sun_path, sizeof(a.sun_path), "%s/%s", dir ? dir : "/run", name);
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0)
                return -1;
        return fd;
}

// What came back for bytes sent: error (a wl_display.error and then the end), closed (the end alone) or open (nothing in 1 s).
static const char *raw_answer(const void *bytes, size_t n, int close_after)
{
        int fd = raw_connect();
        if (fd < 0)
                return "timeout";
        if (write(fd, bytes, n) < 0)
                return "closed";
        if (close_after)
                shutdown(fd, SHUT_WR);
        unsigned char in[4096];
        size_t got = 0;
        int error = 0;
        uint64_t end = now_ns() + 1000000000ull;
        for (;;)
        {
                int left = (int)(((int64_t)end - (int64_t)now_ns()) / 1000000);
                struct pollfd p = {fd, POLLIN, 0};
                if (left <= 0 || poll(&p, 1, left) <= 0)
                        break;
                ssize_t r = read(fd, in + got, sizeof(in) - got);
                if (r <= 0)
                {
                        close(fd);
                        return error ? "error" : "closed";
                }
                got += (size_t)r;
                // wl_display.error is object 1, opcode 0.
                for (size_t at = 0; at + 8 <= got;)
                {
                        uint32_t id, so;
                        memcpy(&id, in + at, 4), memcpy(&so, in + at + 4, 4);
                        if (id == 1 && (so & 0xffff) == 0)
                                error = 1;
                        if ((so >> 16) < 8)
                                break;
                        at += so >> 16;
                }
                if (got == sizeof(in))
                        got = 0;
        }
        close(fd);
        return error ? "error_kept_open" : "open";
}

static void case_raw(const char *name, int which)
{
        uint32_t m[64];
        size_t n = 0;
        uint64_t t = now_ns();
        memset(m, 0, sizeof(m));
        switch (which)
        {
        case 0: // bytes that are no message: a size of 0 in the header
                for (int i = 0; i < 16; i++)
                        m[i] = 0x9e3779b9u * (uint32_t)(i + 1);
                m[1] = 0x00000003u;
                n = 64;
                break;
        case 1: // a request on an object that does not exist
                m[0] = 4242, m[1] = 8u << 16 | 0;
                n = 8;
                break;
        case 2: // an opcode wl_display has not got
                m[0] = 1, m[1] = 8u << 16 | 77;
                n = 8;
                break;
        case 3: // a size smaller than the header
                m[0] = 1, m[1] = 4u << 16 | 0;
                n = 8;
                break;
        case 4: // a message cut short: its size says 64, 12 bytes come, and the socket's write side ends
                m[0] = 1, m[1] = 64u << 16 | 1, m[2] = 2;
                n = 12;
                break;
        case 5: // get_registry with a new id that is not the next (id 9 on a fresh connection)
                m[0] = 1, m[1] = 12u << 16 | 1, m[2] = 9;
                n = 12;
                break;
        case 6: // a string argument with no NUL: wl_registry.bind's interface name runs past its length
                m[0] = 1, m[1] = 12u << 16 | 1, m[2] = 2;                                       // get_registry -> 2
                m[3] = 2, m[4] = 32u << 16 | 0, m[5] = 1, m[6] = 8, memcpy(&m[7], "wl_shmXX", 8); // bind(name 1, "wl_shmXX" no NUL)
                m[9] = 1, m[10] = 3;
                n = 12 + 32;
                break;
        }
        const char *o = raw_answer(m, n, which == 4);
        snprintf(detail, sizeof(detail), "answer=%s", o);
        say(name, o, ms_since(t));
}

/* ---- protocol order: a buffer before the first configure is acknowledged ---- */
static void case_unconfigured_buffer(const char *name)
{
        struct conn c;
        uint64_t t = now_ns();
        if (!connect_to(&c))
                return say(name, "timeout", -1);
        struct wl_surface *s = wl_compositor_create_surface(c.comp);
        struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(c.wm, s);
        xdg_surface_get_toplevel(xs);
        wl_surface_attach(s, shm_buffer(&c, 100, 100, 0xffffffffu), 0, 0);
        wl_surface_commit(s);
        roundtrip_within(&c, 1000);
        say(name, state_of(&c), ms_since(t));
}

/* ---- limits: many pools (a descriptor each) and many buffers ---- */
static void case_many_pools(const char *name)
{
        struct conn c;
        uint64_t t = now_ns();
        if (!connect_to(&c))
                return say(name, "timeout", -1);
        int made = 0;
        for (; made < 4096; made++)
        {
                int fd = memfd_create("wl-edge-pool", MFD_CLOEXEC);
                if (fd < 0 || ftruncate(fd, 4096) < 0)
                        break;
                wl_shm_create_pool(c.shm, fd, 4096);
                close(fd);
                if ((made & 63) == 63 && !roundtrip_within(&c, 2000))
                        break;
        }
        roundtrip_within(&c, 2000);
        snprintf(detail, sizeof(detail), "pools_sent=%d", made);
        say(name, state_of(&c), ms_since(t));
}

static void case_many_buffers(const char *name)
{
        struct conn c;
        uint64_t t = now_ns();
        if (!connect_to(&c))
                return say(name, "timeout", -1);
        int fd = memfd_create("wl-edge-pool", MFD_CLOEXEC);
        if (ftruncate(fd, 64 * 64 * 4) < 0)
                return say(name, "timeout", -1);
        struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 64 * 64 * 4);
        int made = 0;
        for (; made < 50000; made++)
        {
                wl_shm_pool_create_buffer(pool, 0, 64, 64, 256, WL_SHM_FORMAT_XRGB8888);
                if ((made & 1023) == 1023 && !roundtrip_within(&c, 2000))
                        break;
        }
        roundtrip_within(&c, 2000);
        snprintf(detail, sizeof(detail), "buffers_sent=%d", made);
        say(name, state_of(&c), ms_since(t));
}

/* ---- buffers ---- */
static void case_buffer(const char *name, int which)
{
        struct conn c;
        struct win w;
        uint64_t t;
        if (!connect_to(&c) || !open_win(&c, &w, 1))
                return say(name, "timeout", -1);
        t = now_ns();
        int ok = 1, fd = -1;
        switch (which)
        {
        case 0: // attached, then destroyed before the commit
        {
                struct wl_buffer *b = shm_buffer(&c, 300, 200, 0xff0000ffu);
                wl_surface_attach(w.s, b, 0, 0);
                wl_buffer_destroy(b);
                ok = frame_of(&c, w.s, NULL, 300, 200, 1000) || 1; // a callback need not come for a buffer that is gone
                ok = frame_of(&c, w.s, shm_buffer(&c, 300, 200, 0xff00ff00u), 300, 200, 1000);
                break;
        }
        case 1: // one buffer attached again and again, written between, never waited for its release
        {
                int pool = 300 * 200 * 4;
                struct wl_buffer *b = shm_buffer_at(&c, 300, 200, 1200, 0, WL_SHM_FORMAT_XRGB8888, pool, 0xff101010u, &fd);
                uint32_t *px = mmap(NULL, (size_t)pool, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                for (int f = 0; f < 60 && ok; f++)
                {
                        for (int i = 0; i < 300 * 200; i++)
                                px[i] = 0xff000000u | (uint32_t)f * 4u;
                        ok = frame_of(&c, w.s, b, 300, 200, 1000);
                }
                break;
        }
        case 2: // a buffer of no size
                wl_surface_attach(w.s, shm_buffer_at(&c, 0, 0, 0, 0, WL_SHM_FORMAT_XRGB8888, 4096, 0, NULL), 0, 0);
                wl_surface_commit(w.s);
                break;
        case 3: // a stride past the row and an offset into the pool (both allowed)
                ok = frame_of(&c, w.s, shm_buffer_at(&c, 301, 199, 301 * 4 + 52, 4096, WL_SHM_FORMAT_XRGB8888, 4096 + (301 * 4 + 52) * 199, 0xff445566u, NULL),
                              301, 199, 1000);
                break;
        case 4: // a stride that is not a whole number of pixels
                ok = frame_of(&c, w.s, shm_buffer_at(&c, 300, 200, 300 * 4 + 3, 0, WL_SHM_FORMAT_XRGB8888, (300 * 4 + 3) * 200, 0xff445566u, NULL), 300, 200,
                              1000);
                break;
        case 5: // a buffer that runs past the end of its pool
                wl_surface_attach(w.s, shm_buffer_at(&c, 300, 200, 1200, 0, WL_SHM_FORMAT_XRGB8888, 1200 * 100, 0, NULL), 0, 0);
                wl_surface_commit(w.s);
                break;
        case 6: // a buffer larger than the window's geometry
                xdg_surface_set_window_geometry(w.xs, 50, 50, 200, 100);
                ok = frame_of(&c, w.s, shm_buffer(&c, 600, 400, 0xff998877u), 600, 400, 1000);
                break;
        case 7: // the buffer's scale and transform changed every frame
                for (int f = 0; f < 32 && ok; f++)
                {
                        int scale = 1 + (f & 1), tr = f % 8, bw = 320 * scale, bh = 240 * scale;
                        if (tr & 1)
                        {
                                int x = bw;
                                bw = bh, bh = x;
                        }
                        wl_surface_set_buffer_scale(w.s, scale);
                        wl_surface_set_buffer_transform(w.s, tr);
                        ok = frame_of(&c, w.s, shm_buffer(&c, bw, bh, 0xff000000u | (uint32_t)f * 8000u), bw, bh, 1000);
                }
                break;
        case 8: // a viewport's crop and size changed every frame
        {
                if (!c.viewporter)
                {
                        snprintf(detail, sizeof(detail), "no_wp_viewporter");
                        return say(name, "skip", -1);
                }
                struct wp_viewport *vp = wp_viewporter_get_viewport(c.viewporter, w.s);
                struct wl_buffer *b = shm_buffer(&c, 640, 480, 0xff336633u);
                for (int f = 0; f < 40 && ok; f++)
                {
                        wp_viewport_set_source(vp, wl_fixed_from_int(f * 4), wl_fixed_from_int(f * 3), wl_fixed_from_int(320 + f), wl_fixed_from_int(240));
                        wp_viewport_set_destination(vp, 200 + f * 7, 150 + f * 5);
                        ok = frame_of(&c, w.s, b, 640, 480, 1000);
                }
                break;
        }
        case 9: // a storm of damage: 4000 rectangles a commit, 30 commits
        {
                struct wl_buffer *b[2] = {shm_buffer(&c, 800, 600, 0xff123456u), shm_buffer(&c, 800, 600, 0xff654321u)};
                for (int f = 0; f < 30 && ok; f++)
                {
                        wl_surface_attach(w.s, b[f & 1], 0, 0);
                        for (int i = 0; i < 4000; i++)
                                wl_surface_damage_buffer(w.s, (i * 37) % 790, (i * 53) % 590, 7, 5);
                        ok = frame_of(&c, w.s, NULL, 0, 0, 2000);
                }
                snprintf(detail, sizeof(detail), "mean_frame_ms=%.3f", ms_since(t) / 30);
                break;
        }
        case 10: // the pool's file cut to nothing while its buffer is the window's
        {
                struct wl_buffer *b = shm_buffer_at(&c, 300, 200, 1200, 0, WL_SHM_FORMAT_XRGB8888, 1200 * 200, 0xff777777u, &fd);
                ok = frame_of(&c, w.s, b, 300, 200, 1000);
                if (ftruncate(fd, 0) < 0)
                        ok = 0;
                ok = frame_of(&c, w.s, b, 300, 200, 1000) && ok;
                break;
        }
        }
        roundtrip_within(&c, 1000);
        const char *s = state_of(&c);
        say(name, strcmp(s, "ok") ? s : ok ? "ok" : "timeout", ms_since(t));
}

/* ---- surfaces ---- */
struct popup
{
        struct wl_surface *s;
        struct xdg_surface *xs;
        struct xdg_popup *p;
        struct win w;
        int done;
};
static void popup_configure(void *d, struct xdg_popup *p, int32_t x, int32_t y, int32_t w, int32_t h)
{
        (void)d, (void)p, (void)x, (void)y, (void)w, (void)h;
}
static void popup_done(void *d, struct xdg_popup *p)
{
        (void)p;
        ((struct popup *)d)->done = 1;
}
static void popup_repositioned(void *d, struct xdg_popup *p, uint32_t t)
{
        (void)d, (void)p, (void)t;
}
static const struct xdg_popup_listener popup_listener = {popup_configure, popup_done, popup_repositioned};

static int open_popup(struct conn *c, struct popup *pp, struct xdg_surface *parent, int x)
{
        memset(pp, 0, sizeof(*pp));
        pp->w.ack = 1;
        struct xdg_positioner *pos = xdg_wm_base_create_positioner(c->wm);
        xdg_positioner_set_size(pos, 120, 90);
        xdg_positioner_set_anchor_rect(pos, x, 10, 1, 1);
        pp->s = wl_compositor_create_surface(c->comp);
        pp->xs = xdg_wm_base_get_xdg_surface(c->wm, pp->s);
        xdg_surface_add_listener(pp->xs, &xs_listener, &pp->w);
        pp->p = xdg_surface_get_popup(pp->xs, parent, pos);
        xdg_popup_add_listener(pp->p, &popup_listener, pp);
        xdg_positioner_destroy(pos);
        wl_surface_commit(pp->s);
        if (!wait_flag(c, &pp->w.configured, 1000))
                return 0;
        return frame_of(c, pp->s, shm_buffer(c, 120, 90, 0xffcc8800u), 120, 90, 1000);
}

static void case_surface(const char *name, int which)
{
        struct conn c;
        struct win w;
        uint64_t t;
        if (!connect_to(&c) || !open_win(&c, &w, 1) || !frame_of(&c, w.s, shm_buffer(&c, 500, 400, 0xff202020u), 500, 400, 1000))
                return say(name, "timeout", -1);
        t = now_ns();
        int ok = 1;
        switch (which)
        {
        case 0: // a tree of subsurfaces six deep, each over its parent
        case 1: // the same, each switching between synced and desynced every frame
        case 2: // the middle of the tree destroyed while its children live
        {
                struct wl_surface *s[7];
                struct wl_subsurface *ss[7];
                s[0] = w.s;
                for (int i = 1; i < 7; i++)
                {
                        s[i] = wl_compositor_create_surface(c.comp);
                        ss[i] = wl_subcompositor_get_subsurface(c.sub, s[i], s[i - 1]);
                        wl_subsurface_set_position(ss[i], 12, 10);
                        wl_surface_attach(s[i], shm_buffer(&c, 400 - 40 * i, 300 - 30 * i, 0xff000000u | (uint32_t)i * 0x202020u), 0, 0);
                        wl_surface_damage_buffer(s[i], 0, 0, 400, 300);
                        wl_surface_commit(s[i]);
                }
                for (int f = 0; f < (which == 1 ? 40 : 3) && ok; f++)
                {
                        for (int i = 1; i < 7 && which == 1; i++)
                        {
                                if ((f + i) & 1)
                                        wl_subsurface_set_desync(ss[i]);
                                else
                                        wl_subsurface_set_sync(ss[i]);
                                wl_subsurface_set_position(ss[i], 12 + (f & 3), 10);
                                wl_surface_commit(s[i]);
                        }
                        ok = frame_of(&c, w.s, NULL, 500, 400, 1000);
                }
                if (which == 2)
                {
                        wl_subsurface_destroy(ss[3]);
                        wl_surface_destroy(s[3]);
                        for (int i = 4; i < 7; i++)
                                wl_surface_commit(s[i]);
                        ok = frame_of(&c, w.s, NULL, 500, 400, 1000);
                }
                break;
        }
        case 3: // a chain of popups, menu, submenu, sub-submenu, and the middle one destroyed first
        case 4: // the same, closed from the top as the protocol asks
        {
                struct popup p[3];
                ok = open_popup(&c, &p[0], w.xs, 10) && open_popup(&c, &p[1], p[0].xs, 60) && open_popup(&c, &p[2], p[1].xs, 60);
                if (which == 3)
                {
                        xdg_popup_destroy(p[1].p);
                        xdg_surface_destroy(p[1].xs);
                }
                else
                        for (int i = 2; i >= 0; i--)
                        {
                                xdg_popup_destroy(p[i].p);
                                xdg_surface_destroy(p[i].xs);
                                wl_surface_destroy(p[i].s);
                        }
                ok = frame_of(&c, w.s, NULL, 500, 400, 1000) && ok;
                break;
        }
        case 5: // a popup whose window is unmapped under it
        {
                struct popup p;
                ok = open_popup(&c, &p, w.xs, 10);
                wl_surface_attach(w.s, NULL, 0, 0);
                wl_surface_commit(w.s);
                roundtrip_within(&c, 500);
                snprintf(detail, sizeof(detail), "popup_done=%d", p.done);
                break;
        }
        case 6: // two toplevels made each other's parent
        {
                struct win v;
                ok = open_win(&c, &v, 1) && frame_of(&c, v.s, shm_buffer(&c, 300, 200, 0xff404040u), 300, 200, 1000);
                xdg_toplevel_set_parent(v.top, w.top);
                xdg_toplevel_set_parent(w.top, v.top);
                ok = frame_of(&c, w.s, NULL, 500, 400, 1000) && ok;
                break;
        }
        case 7: // a parent changed 200 times between three toplevels, and its parent destroyed
        {
                struct win v, u;
                ok = open_win(&c, &v, 1) && open_win(&c, &u, 1) && frame_of(&c, v.s, shm_buffer(&c, 300, 200, 0xff404040u), 300, 200, 1000) &&
                     frame_of(&c, u.s, shm_buffer(&c, 200, 100, 0xff606060u), 200, 100, 1000);
                for (int i = 0; i < 200; i++)
                        xdg_toplevel_set_parent(u.top, (i & 1) ? v.top : (i % 3) ? w.top : NULL);
                xdg_toplevel_set_parent(u.top, v.top);
                xdg_toplevel_destroy(v.top);
                ok = frame_of(&c, u.s, NULL, 200, 100, 1000) && ok;
                break;
        }
        case 8: // the minimum and maximum sizes fought over, 200 commits, then a minimum above the maximum
                for (int i = 0; i < 200; i++)
                {
                        xdg_toplevel_set_min_size(w.top, 100 + i % 50, 100);
                        xdg_toplevel_set_max_size(w.top, (i & 1) ? 0 : 400 + i, 0);
                        wl_surface_commit(w.s);
                }
                ok = roundtrip_within(&c, 1000);
                xdg_toplevel_set_min_size(w.top, 500, 500);
                xdg_toplevel_set_max_size(w.top, 300, 300);
                wl_surface_commit(w.s);
                break;
        case 9: // fullscreen and maximized set and unset 300 times (minimized between), then a frame at the size the last configure said
        {
                for (int i = 0; i < 300; i++)
                {
                        switch (i % 5)
                        {
                        case 0: xdg_toplevel_set_fullscreen(w.top, NULL); break;
                        case 1: xdg_toplevel_unset_fullscreen(w.top); break;
                        case 2: xdg_toplevel_set_maximized(w.top); break;
                        case 3: xdg_toplevel_unset_maximized(w.top); break;
                        case 4: xdg_toplevel_set_minimized(w.top); break;
                        }
                        if (i % 10 == 9)
                                roundtrip_within(&c, 500);
                }
                // A minimized window may be given no frame (the protocol leaves it so): asked to be shown as it was before the last word.
                xdg_toplevel_unset_fullscreen(w.top);
                xdg_toplevel_unset_maximized(w.top);
                wl_surface_commit(w.s);
                int seen = w.configures;
                roundtrip_within(&c, 1000);
                int cw = w.cw > 0 ? w.cw : 500, ch = w.ch > 0 ? w.ch : 400;
                ok = frame_of(&c, w.s, shm_buffer(&c, cw, ch, 0xff505050u), cw, ch, 1500);
                snprintf(detail, sizeof(detail), "configures=%d last=%dx%d", seen, w.cw, w.ch);
                break;
        }
        }
        roundtrip_within(&c, 1000);
        const char *s = state_of(&c);
        say(name, strcmp(s, "ok") ? s : ok ? "ok" : "timeout", ms_since(t));
}

/* ---- input, pressed through the FIFO ---- */
struct pstate
{
        uint32_t enter_serial, button_serial;
        int enters, leaves, motions, buttons_down, leave_while_down;
        struct wl_surface *focus;
};
static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
        struct pstate *st = d;
        (void)p, (void)x, (void)y;
        st->enter_serial = serial, st->enters++, st->focus = s;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
        struct pstate *st = d;
        (void)p, (void)serial, (void)s;
        st->leaves++, st->focus = NULL;
        if (st->buttons_down)
                st->leave_while_down++;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
        (void)p, (void)t, (void)x, (void)y;
        ((struct pstate *)d)->motions++;
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t b, uint32_t state)
{
        struct pstate *st = d;
        (void)p, (void)t, (void)b;
        st->button_serial = serial;
        st->buttons_down += state ? 1 : -1;
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
        (void)d, (void)p, (void)t, (void)a, (void)v;
}
static void p_frame(void *d, struct wl_pointer *p)
{
        (void)d, (void)p;
}
static void p_u32(void *d, struct wl_pointer *p, uint32_t a)
{
        (void)d, (void)p, (void)a;
}
static void p_u32u32(void *d, struct wl_pointer *p, uint32_t a, uint32_t b)
{
        (void)d, (void)p, (void)a, (void)b;
}
static void p_u32i32(void *d, struct wl_pointer *p, uint32_t a, int32_t b)
{
        (void)d, (void)p, (void)a, (void)b;
}
static const struct wl_pointer_listener pointer_listener = {p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_u32, p_u32u32, p_u32i32, p_u32i32, p_u32u32};

static void pump(struct conn *c, int ms)
{
        static int never;
        never = 0;
        wait_flag(c, &never, ms);
}

static void case_input(const char *name, int which)
{
        if (fifo < 0)
        {
                snprintf(detail, sizeof(detail), "no_input_fifo");
                return say(name, "skip", -1);
        }
        if (which == 0)
        {
                // A client that locked the pointer killed: the next window has the pointer, moving.
                pid_t pid = fork();
                if (pid == 0)
                {
                        child_lock();
                        _exit(0);
                }
                waitpid(pid, NULL, 0);
        }
        struct conn c;
        struct win w;
        struct pstate st = {0};
        if (!connect_to(&c) || !c.seat || !open_win(&c, &w, 1))
                return say(name, "timeout", -1);
        uint64_t t = now_ns();
        struct wl_pointer *pointer = wl_seat_get_pointer(c.seat);
        wl_pointer_add_listener(pointer, &pointer_listener, &st);
        struct wl_keyboard *kb = wl_seat_get_keyboard(c.seat);
        wl_keyboard_add_listener(kb, &keyboard_listener, NULL);
        if (which != 2)
        {
                xdg_toplevel_set_fullscreen(w.top, NULL);
                wl_surface_commit(w.s);
                roundtrip_within(&c, 500);
        }
        int cw = w.cw > 0 ? w.cw : 400, ch = w.ch > 0 ? w.ch : 300;
        frame_of(&c, w.s, shm_buffer(&c, cw, ch, 0xff303030u), cw, ch, 1000);
        if (which == 2)
        {
                // A window of its own size somewhere: the pointer to the top left corner, then along the diagonal until it is over the window.
                for (int i = 0; i < 40; i++)
                        inject(2, 0, -100), inject(2, 1, -100);
                for (int i = 0; i < 300 && !st.focus; i++)
                {
                        inject(2, 0, 8), inject(2, 1, 6);
                        pump(&c, 3);
                }
                for (int i = 0; i < 6 && st.focus; i++)
                        inject(2, 0, 8), inject(2, 1, 6); // inside, away from the edge
                pump(&c, 30);
        }
        for (int i = 0; i < 20 && !st.focus; i++)
        {
                inject(2, 0, (i & 1) ? 3 : -3);
                pump(&c, 30);
        }
        int ok = st.focus != NULL;
        switch (which)
        {
        case 0: // the pointer moves for the new window
        {
                int before = st.motions;
                for (int i = 0; i < 10; i++)
                        inject(2, 0, (i & 1) ? 4 : -4), pump(&c, 20);
                ok = ok && st.motions > before;
                snprintf(detail, sizeof(detail), "motions=%d", st.motions - before);
                break;
        }
        case 1: // the cursor set to a surface that is then destroyed, and the pointer moved over it
        {
                struct wl_surface *cs = wl_compositor_create_surface(c.comp);
                wl_surface_attach(cs, shm_buffer(&c, 24, 24, 0xffff00ffu), 0, 0);
                wl_surface_damage_buffer(cs, 0, 0, 24, 24);
                wl_surface_commit(cs);
                wl_pointer_set_cursor(pointer, st.enter_serial, cs, 4, 4);
                roundtrip_within(&c, 200);
                wl_surface_destroy(cs);
                for (int i = 0; i < 10; i++)
                        inject(2, 0, (i & 1) ? 5 : -5), pump(&c, 20);
                ok = ok && roundtrip_within(&c, 500);
                break;
        }
        case 2: // a button held while the pointer leaves the window: the window keeps it until the release
        {
                inject(1, 272, 1);
                pump(&c, 50);
                for (int i = 0; i < 40; i++)
                        inject(2, 0, -60), inject(2, 1, -60), pump(&c, 5);
                int during = st.leave_while_down;
                inject(1, 272, 0);
                pump(&c, 100);
                snprintf(detail, sizeof(detail), "leave_while_held=%d leaves_after_release=%d", during, st.leaves);
                ok = ok && during == 0;
                for (int i = 0; i < 40; i++)
                        inject(2, 0, 60), inject(2, 1, 60);
                break;
        }
        case 3: // a key held while a new window takes the keys: the old one is left and is told no release after
        {
                wait_flag(&c, &key_focus, 1000);
                inject(1, 30, 1);
                pump(&c, 100);
                int held = keys_down;
                struct conn c2;
                struct win w2;
                if (!connect_to(&c2) || !open_win(&c2, &w2, 1))
                        return say(name, "timeout", -1);
                frame_of(&c2, w2.s, shm_buffer(&c2, 300, 200, 0xff505000u), 300, 200, 1000);
                pump(&c, 200);
                int left = !key_focus, before = keys_down;
                inject(1, 30, 0);
                pump(&c, 200);
                snprintf(detail, sizeof(detail), "held=%d first_window_left=%d events_after_leave=%d", held, left, keys_down != before);
                ok = ok && held == 1 && (!left || keys_down == before);
                wl_display_disconnect(c2.d);
                break;
        }
        }
        roundtrip_within(&c, 500);
        const char *s = state_of(&c);
        say(name, strcmp(s, "ok") ? s : ok ? "ok" : "fail", ms_since(t));
}

/* ---- the cases ---- */
static void c_kill_commit(const char *n) { case_killed(n, child_commit); }
static void c_kill_resize(const char *n) { case_killed(n, child_resize); }
static void c_kill_popup(const char *n) { case_killed(n, child_popup); }
static void c_kill_lock(const char *n) { case_killed(n, child_lock); }
static void c_kill_clip(const char *n) { case_killed(n, child_clipboard); }
static void c_kill_drag(const char *n) { case_killed(n, child_drag); }
static void c_frame_forever(const char *n) { case_beside(n, child_frame_forever); }
static void c_never_ack(const char *n) { case_beside(n, child_never_ack); }
static void c_flood_sync(const char *n) { case_beside(n, child_flood_sync); }
static void c_flood_damage(const char *n) { case_beside(n, child_flood_damage); }
#define RAW(k) static void c_raw##k(const char *n) { case_raw(n, k); }
RAW(0) RAW(1) RAW(2) RAW(3) RAW(4) RAW(5) RAW(6)
#define BUF(k) static void c_buf##k(const char *n) { case_buffer(n, k); }
BUF(0) BUF(1) BUF(2) BUF(3) BUF(4) BUF(5) BUF(6) BUF(7) BUF(8) BUF(9) BUF(10)
#define SURF(k) static void c_surf##k(const char *n) { case_surface(n, k); }
SURF(0) SURF(1) SURF(2) SURF(3) SURF(4) SURF(5) SURF(6) SURF(7) SURF(8) SURF(9)
#define INP(k) static void c_in##k(const char *n) { case_input(n, k); }
INP(0) INP(1) INP(2) INP(3)

static const struct
{
        const char *name;
        void (*run)(const char *);
} cases[] = {
    {"kill-mid-commit", c_kill_commit},       {"kill-mid-resize", c_kill_resize},
    {"kill-mid-popup", c_kill_popup},         {"kill-mid-lock", c_kill_lock},
    {"kill-mid-clipboard", c_kill_clip},      {"kill-mid-drag", c_kill_drag},
    {"frame-callback-forever", c_frame_forever}, {"never-ack-configure", c_never_ack},
    {"flood-sync", c_flood_sync},             {"flood-damage", c_flood_damage},
    {"raw-garbage", c_raw0},                  {"raw-no-object", c_raw1},
    {"raw-bad-opcode", c_raw2},               {"raw-short-size", c_raw3},
    {"raw-truncated", c_raw4},                {"raw-new-id-skip", c_raw5},
    {"raw-string-no-nul", c_raw6},            {"buffer-before-configure", case_unconfigured_buffer},
    {"many-pools", case_many_pools},          {"many-buffers", case_many_buffers},
    {"destroy-before-commit", c_buf0},        {"reuse-before-release", c_buf1},
    {"zero-size-buffer", c_buf2},             {"odd-stride-offset", c_buf3},
    {"stride-not-pixels", c_buf4},            {"buffer-past-pool", c_buf5},
    {"buffer-over-geometry", c_buf6},         {"scale-transform-stream", c_buf7},
    {"viewport-crop-scale", c_buf8},          {"damage-storm", c_buf9},
    {"pool-cut-while-shown", c_buf10},        {"subsurface-tree-6", c_surf0},
    {"subsurface-sync-toggle", c_surf1},      {"subsurface-middle-destroyed", c_surf2},
    {"popup-chain-middle-closed", c_surf3},   {"popup-chain-top-down", c_surf4},
    {"popup-parent-unmapped", c_surf5},       {"toplevel-parent-cycle", c_surf6},
    {"toplevel-parent-churn", c_surf7},       {"min-max-fight", c_surf8},
    {"state-toggle-storm", c_surf9},          {"lock-released-by-crash", c_in0},
    {"cursor-surface-destroyed", c_in1},      {"button-held-pointer-leaves", c_in2},
    {"key-held-focus-change", c_in3},
};

int main(int argc, char **argv)
{
        const char *which = NULL, *fifo_path = NULL;
        int c;
        signal(SIGPIPE, SIG_IGN);
        while ((c = getopt(argc, argv, "c:F:l")) != -1)
                switch (c)
                {
                case 'c': which = optarg; break;
                case 'F': fifo_path = optarg; break;
                case 'l':
                        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
                                printf("%s\n", cases[i].name);
                        return 0;
                default: fprintf(stderr, "usage: wl-edge -c CASE [-F FIFO] | -l\n"); return 2;
                }
        if (fifo_path)
                fifo = open(fifo_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        for (size_t i = 0; which && i < sizeof(cases) / sizeof(cases[0]); i++)
                if (!strcmp(cases[i].name, which))
                {
                        struct conn probe;
                        if (!connect_to(&probe))
                        {
                                fprintf(stderr, "cannot connect\n");
                                return 2;
                        }
                        wl_display_disconnect(probe.d);
                        cases[i].run(which);
                        return 0;
                }
        fprintf(stderr, "no case %s\n", which ? which : "(none)");
        return 2;
}
