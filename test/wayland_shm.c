/*
        A Wayland client built on libwayland, as real programs are: the judge the shim is run against on a machine
        that has the library. It makes a toplevel on wl_shm buffers (two, as toolkits do), draws, asks for a frame and
        draws again, and says on standard output what it saw, one line each, so a lane or a person can read it.

            wayland-scanner client-header XDG.xml xdg-shell-client.h
            wayland-scanner private-code  XDG.xml xdg-shell-code.c
            cc -std=gnu11 -Wall -Wextra -I. test/wayland_shm.c xdg-shell-code.c -lwayland-client -o wayland-shm

        wayland-shm [-n FRAMES] [-w WIDTH] [-h HEIGHT] [-t TITLE] [-a APP_ID] [-s] [-m] [-f] [-d] [-q]
        -n frames to draw before it exits (default 60), -w and -h the size it asks to be, -s keep still (draw once, then
        only on a configure), -m ask to be maximized, -f fullscreen, -d damage only the rectangle that changes, -q quiet.
        Exit 0 once the frames are drawn, 1 on a protocol error or a connection that ends, 2 when it cannot start.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client.h"

struct buffer
{
        struct wl_buffer *buffer;
        void *data;
        int busy;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wl_seat *seat;
static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *toplevel;
static struct wl_callback *frame_callback;
static struct buffer buffers[2];

static int width = 320, height = 240, frames_wanted = 60, frames, configures, acked, closed, quiet, still, damage_only;
static int maximize, fullscreen, ready, pointer_events, key_events, enters, leaves, scale = 1;
static const char *title = "wayland-shm", *app_id = "wayland-shm";

static void say(const char *what, long value)
{
        if (!quiet)
                printf("wayland-shm: %s %ld\n", what, value);
}

static void release(void *data, struct wl_buffer *buffer)
{
        (void)buffer;
        ((struct buffer *)data)->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {release};

static int make_buffer(struct buffer *b, int w, int h)
{
        int stride = w * 4, size = stride * h, fd = memfd_create("wayland-shm", MFD_CLOEXEC);
        struct wl_shm_pool *pool;

        if (fd < 0 || ftruncate(fd, size) < 0)
                return -1;
        b->data = mmap(0, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (b->data == MAP_FAILED)
                return -1;
        pool = wl_shm_create_pool(shm, fd, size);
        b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(b->buffer, &buffer_listener, b);
        wl_shm_pool_destroy(pool);
        close(fd);
        b->busy = 0;
        return 0;
}

static void paint(struct buffer *b, int step)
{
        unsigned *pixels = b->data;

        for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                        unsigned r = (unsigned)(x * 255 / (width > 1 ? width - 1 : 1)), g = (unsigned)(y * 255 / (height > 1 ? height - 1 : 1));
                        unsigned bl = (unsigned)((step * 4) & 255);
                        int inside = x >= (step * 3) % (width - 20) && x < (step * 3) % (width - 20) + 20 && y >= 10 && y < 30;

                        pixels[y * width + x] = inside ? 0xffffffffu : 0xff000000u | r << 16 | g << 8 | bl;
                }
}

static void redraw(void);

static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
        (void)data;
        (void)time;
        wl_callback_destroy(callback);
        frame_callback = 0;
        frames++;
        if (frames >= frames_wanted)
                return;
        if (!still)
                redraw();
}

static const struct wl_callback_listener frame_listener = {frame_done};

static void redraw(void)
{
        struct buffer *b = !buffers[0].busy ? &buffers[0] : !buffers[1].busy ? &buffers[1] : 0;

        if (!b || !ready)
                return;
        paint(b, frames);
        wl_surface_attach(surface, b->buffer, 0, 0);
        if (damage_only)
                wl_surface_damage_buffer(surface, (frames * 3) % (width - 20) - 3, 10, 26, 20);
        else
                wl_surface_damage_buffer(surface, 0, 0, width, height);
        frame_callback = wl_surface_frame(surface);
        wl_callback_add_listener(frame_callback, &frame_listener, 0);
        b->busy = 1;
        wl_surface_commit(surface);
}

static void ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
        (void)data;
        xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {ping};

static void surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
        (void)data;
        xdg_surface_ack_configure(xs, serial);
        acked++;
        if (!ready)
        {
                ready = 1;
                redraw();
        }
}

static const struct xdg_surface_listener surface_listener = {surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
        (void)data;
        (void)t;
        (void)states;
        configures++;
        if (w > 0 && h > 0 && (w != width || h != height))
        {
                say("configured to", (long)w * 10000 + h);
        }
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
        (void)data;
        (void)t;
        closed = 1;
}

static void toplevel_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h)
{
        (void)data;
        (void)t;
        (void)w;
        (void)h;
}

static void toplevel_caps(void *data, struct xdg_toplevel *t, struct wl_array *caps)
{
        (void)data;
        (void)t;
        (void)caps;
}

static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close, toplevel_bounds, toplevel_caps};

static void surface_enter(void *data, struct wl_surface *s, struct wl_output *o)
{
        (void)data;
        (void)s;
        (void)o;
        enters++;
}

static void surface_leave(void *data, struct wl_surface *s, struct wl_output *o)
{
        (void)data;
        (void)s;
        (void)o;
        leaves++;
}

static void surface_scale(void *data, struct wl_surface *s, int32_t factor)
{
        (void)data;
        (void)s;
        scale = factor;
}

static void surface_transform(void *data, struct wl_surface *s, uint32_t transform)
{
        (void)data;
        (void)s;
        (void)transform;
}

static const struct wl_surface_listener wl_surface_listener = {surface_enter, surface_leave, surface_scale, surface_transform};

static void pointer_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *su, wl_fixed_t x, wl_fixed_t y)
{
        (void)d; (void)p; (void)s; (void)su; (void)x; (void)y;
        pointer_events++;
}

static void pointer_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *su)
{
        (void)d; (void)p; (void)s; (void)su;
        pointer_events++;
}

static void pointer_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
        (void)d; (void)p; (void)t; (void)x; (void)y;
        pointer_events++;
}

static void pointer_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
        (void)d; (void)p; (void)s; (void)t; (void)b; (void)st;
        pointer_events++;
}

static void pointer_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
        (void)d; (void)p; (void)t; (void)a; (void)v;
        pointer_events++;
}

static void pointer_frame(void *d, struct wl_pointer *p)
{
        (void)d; (void)p;
}

static void pointer_axis_source(void *d, struct wl_pointer *p, uint32_t s)
{
        (void)d; (void)p; (void)s;
}

static void pointer_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a)
{
        (void)d; (void)p; (void)t; (void)a;
}

static void pointer_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{
        (void)d; (void)p; (void)a; (void)v;
}

static void pointer_axis_value120(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{
        (void)d; (void)p; (void)a; (void)v;
}

static void pointer_axis_direction(void *d, struct wl_pointer *p, uint32_t a, uint32_t v)
{
        (void)d; (void)p; (void)a; (void)v;
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter, pointer_leave, pointer_motion, pointer_button, pointer_axis, pointer_frame,
    pointer_axis_source, pointer_axis_stop, pointer_axis_discrete, pointer_axis_value120, pointer_axis_direction};

static void keyboard_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t size)
{
        (void)d; (void)k; (void)f; (void)size;
        close(fd);
}

static void keyboard_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su, struct wl_array *keys)
{
        (void)d; (void)k; (void)s; (void)su; (void)keys;
        key_events++;
}

static void keyboard_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su)
{
        (void)d; (void)k; (void)s; (void)su;
        key_events++;
}

static void keyboard_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t state)
{
        (void)d; (void)k; (void)s; (void)t; (void)key; (void)state;
        key_events++;
}

static void keyboard_modifiers(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g)
{
        (void)d; (void)k; (void)s; (void)a; (void)b; (void)c; (void)g;
}

static void keyboard_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
        (void)d; (void)k; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key,
                                                              keyboard_modifiers, keyboard_repeat};

static void seat_capabilities(void *data, struct wl_seat *s, uint32_t caps)
{
        (void)data;
        if (caps & WL_SEAT_CAPABILITY_POINTER)
                wl_pointer_add_listener(wl_seat_get_pointer(s), &pointer_listener, 0);
        if (caps & WL_SEAT_CAPABILITY_KEYBOARD)
                wl_keyboard_add_listener(wl_seat_get_keyboard(s), &keyboard_listener, 0);
}

static void seat_name(void *data, struct wl_seat *s, const char *name)
{
        (void)data;
        (void)s;
        (void)name;
}

static const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
        (void)data;
        if (!strcmp(interface, wl_compositor_interface.name))
                compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 6 ? version : 6);
        else if (!strcmp(interface, wl_shm_interface.name))
                shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        else if (!strcmp(interface, xdg_wm_base_interface.name))
                wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < 6 ? version : 6);
        else if (!strcmp(interface, wl_seat_interface.name))
                seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
        (void)data;
        (void)registry;
        (void)name;
}

static const struct wl_registry_listener registry_listener = {global, global_remove};

int main(int argc, char **argv)
{
        struct wl_registry *registry;
        int option;

        while ((option = getopt(argc, argv, "n:w:h:t:a:smfdq")) != -1)
                switch (option)
                {
                case 'n': frames_wanted = atoi(optarg); break;
                case 'w': width = atoi(optarg); break;
                case 'h': height = atoi(optarg); break;
                case 't': title = optarg; break;
                case 'a': app_id = optarg; break;
                case 's': still = 1; break;
                case 'm': maximize = 1; break;
                case 'f': fullscreen = 1; break;
                case 'd': damage_only = 1; break;
                case 'q': quiet = 1; break;
                default: return 2;
                }
        display = wl_display_connect(0);
        if (!display)
        {
                fprintf(stderr, "wayland-shm: cannot connect\n");
                return 2;
        }
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        wl_display_roundtrip(display);
        if (!compositor || !shm || !wm_base)
        {
                fprintf(stderr, "wayland-shm: no compositor, shm or xdg_wm_base\n");
                return 2;
        }
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, 0);
        if (seat)
                wl_seat_add_listener(seat, &seat_listener, 0);
        if (make_buffer(&buffers[0], width, height) < 0 || make_buffer(&buffers[1], width, height) < 0)
        {
                fprintf(stderr, "wayland-shm: no buffers\n");
                return 2;
        }
        surface = wl_compositor_create_surface(compositor);
        wl_surface_add_listener(surface, &wl_surface_listener, 0);
        xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
        xdg_surface_add_listener(xdg_surface, &surface_listener, 0);
        toplevel = xdg_surface_get_toplevel(xdg_surface);
        xdg_toplevel_add_listener(toplevel, &toplevel_listener, 0);
        xdg_toplevel_set_title(toplevel, title);
        xdg_toplevel_set_app_id(toplevel, app_id);
        if (maximize)
                xdg_toplevel_set_maximized(toplevel);
        if (fullscreen)
                xdg_toplevel_set_fullscreen(toplevel, 0);
        wl_surface_commit(surface);
        while (!closed && frames < frames_wanted && wl_display_dispatch(display) != -1)
                ;
        if (wl_display_get_error(display))
        {
                fprintf(stderr, "wayland-shm: protocol error %d\n", wl_display_get_error(display));
                return 1;
        }
        say("frames", frames);
        say("configures", configures);
        say("acks", acked);
        say("enters", enters);
        say("pointer events", pointer_events);
        say("key events", key_events);
        say("closed", closed);
        wl_display_roundtrip(display);
        return frames >= frames_wanted || closed ? 0 : 1;
}
