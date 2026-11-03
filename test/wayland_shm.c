/*
        A Wayland client built on libwayland, as real programs are: the judge the shim is run against on a machine
        that has the library. It makes a toplevel on wl_shm buffers (two, as toolkits do), draws, asks for a frame and
        draws again, and says on standard output what it saw, one line each, so a lane or a person can read it.

            wayland-scanner client-header XDG.xml xdg-shell-client.h
            wayland-scanner private-code  XDG.xml xdg-shell-code.c
            cc -std=gnu11 -Wall -Wextra -I. test/wayland_shm.c xdg-shell-code.c -lwayland-client -o wayland-shm

        wayland-shm [-n FRAMES] [-w WIDTH] [-h HEIGHT] [-t TITLE] [-a APP_ID] [-s] [-m] [-f] [-d] [-u] [-C TEXT] [-P] [-q] [-v]
        -n frames to draw before it exits (default 60), -w and -h the size it asks to be, -s keep still (draw once, then
        only on a configure), -m ask to be maximized, -f fullscreen, -d damage only the rectangle that changes, -u a subsurface of 64 by 48 in 0xff10ff20 at (10, 20),
        -C TEXT copy it to the clipboard at the first key, -P paste the clipboard at the first key (and say what came),
        -q quiet,
        -v a line on standard output for each pointer, key and keymap event as it arrives (with -DHAVE_XKB, the keymap is
        compiled by libxkbcommon and each key says its keysym and its text).
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
#ifdef HAVE_XKB
#include <xkbcommon/xkbcommon.h>
#endif

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
static struct wl_subcompositor *subcompositor;
static struct wl_data_device_manager *data_manager;
static struct wl_data_device *data_device;
static struct wl_data_offer *selection_offer;
static struct wl_data_source *copy_source;
static const char *copy_text;
static int paste_wanted, copy_done, paste_done;
static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *toplevel;
static struct wl_callback *frame_callback;
static struct buffer buffers[2];

static int width = 320, height = 240, frames_wanted = 60, frames, configures, acked, closed, quiet, still, damage_only;
static int child_wanted, maximize, fullscreen, ready, pointer_events, key_events, enters, leaves, scale = 1, verbose;
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
        (void)d; (void)p; (void)s; (void)su;
        pointer_events++;
        if (verbose)
                printf("pointer enter %d %d\n", wl_fixed_to_int(x), wl_fixed_to_int(y)), fflush(stdout);
}

static void pointer_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *su)
{
        (void)d; (void)p; (void)s; (void)su;
        pointer_events++;
        if (verbose)
                printf("pointer leave\n"), fflush(stdout);
}

static void pointer_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
        (void)d; (void)p; (void)t;
        pointer_events++;
        if (verbose)
                printf("pointer motion %d %d\n", wl_fixed_to_int(x), wl_fixed_to_int(y)), fflush(stdout);
}

static void pointer_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
        (void)d; (void)p; (void)s; (void)t;
        pointer_events++;
        if (verbose)
                printf("pointer button %u %u\n", b, st), fflush(stdout);
}

static void pointer_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
        (void)d; (void)p; (void)t;
        pointer_events++;
        if (verbose)
                printf("pointer axis %u %d\n", a, wl_fixed_to_int(v)), fflush(stdout);
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
        (void)d; (void)p;
        if (verbose)
                printf("pointer value120 %u %d\n", a, v), fflush(stdout);
}

static void pointer_axis_direction(void *d, struct wl_pointer *p, uint32_t a, uint32_t v)
{
        (void)d; (void)p; (void)a; (void)v;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion, .button = pointer_button, .axis = pointer_axis,
    .frame = pointer_frame, .axis_source = pointer_axis_source, .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete, .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_direction};

#ifdef HAVE_XKB
static struct xkb_context *xkb_ctx;
static struct xkb_keymap *xkb_map;
static struct xkb_state *xkb_st;
#endif

static void keyboard_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t size)
{
        (void)d; (void)k;
        if (verbose)
                printf("keymap %u %u\n", f, size), fflush(stdout);
#ifdef HAVE_XKB
        {
                char *text = f == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 ? mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;

                if (text != MAP_FAILED)
                {
                        if (!xkb_ctx)
                                xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);
                        if (xkb_st)
                                xkb_state_unref(xkb_st);
                        if (xkb_map)
                                xkb_keymap_unref(xkb_map);
                        xkb_map = xkb_keymap_new_from_buffer(xkb_ctx, text, size - 1 + (text[size - 1] ? 1 : 0), XKB_KEYMAP_FORMAT_TEXT_V1, 0);
                        xkb_st = xkb_map ? xkb_state_new(xkb_map) : 0;
                        if (verbose)
                                printf("xkb %s\n", xkb_map ? "ok" : "refused"), fflush(stdout);
                        munmap(text, size);
                }
        }
#endif
        close(fd);
}

static void keyboard_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su, struct wl_array *keys)
{
        (void)d; (void)k; (void)s; (void)su;
        key_events++;
        if (verbose)
                printf("keyboard enter %zu\n", keys->size / 4), fflush(stdout);
}

static void keyboard_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su)
{
        (void)d; (void)k; (void)s; (void)su;
        key_events++;
        if (verbose)
                printf("keyboard leave\n"), fflush(stdout);
}

static void source_target(void *d, struct wl_data_source *source, const char *mime)
{
        (void)d; (void)source; (void)mime;
}

static void source_send(void *d, struct wl_data_source *source, const char *mime, int32_t fd)
{
        (void)d; (void)source;
        if (copy_text && write(fd, copy_text, strlen(copy_text)) < 0)
                perror("wayland-shm: write");
        close(fd);
        printf("sent %s\n", mime);
        fflush(stdout);
}

static void source_cancelled(void *d, struct wl_data_source *source)
{
        (void)d;
        printf("cancelled\n");
        fflush(stdout);
        wl_data_source_destroy(source);
        copy_source = 0;
}

static void source_drop(void *d, struct wl_data_source *source)
{
        (void)d; (void)source;
}

static void source_finished(void *d, struct wl_data_source *source)
{
        (void)d; (void)source;
}

static void source_action(void *d, struct wl_data_source *source, uint32_t action)
{
        (void)d; (void)source; (void)action;
}

static const struct wl_data_source_listener source_listener = {
    .target = source_target, .send = source_send, .cancelled = source_cancelled, .dnd_drop_performed = source_drop,
    .dnd_finished = source_finished, .action = source_action};

static void offer_offer(void *d, struct wl_data_offer *offer, const char *mime)
{
        (void)d; (void)offer;
        if (verbose)
                printf("offer %s\n", mime), fflush(stdout);
}

static void offer_source_actions(void *d, struct wl_data_offer *offer, uint32_t actions)
{
        (void)d; (void)offer; (void)actions;
}

static void offer_action(void *d, struct wl_data_offer *offer, uint32_t action)
{
        (void)d; (void)offer; (void)action;
}

static const struct wl_data_offer_listener offer_listener = {
    .offer = offer_offer, .source_actions = offer_source_actions, .action = offer_action};

static void device_data_offer(void *d, struct wl_data_device *device, struct wl_data_offer *offer)
{
        (void)d; (void)device;
        wl_data_offer_add_listener(offer, &offer_listener, 0);
}

static void device_enter(void *d, struct wl_data_device *device, uint32_t serial, struct wl_surface *su, wl_fixed_t x, wl_fixed_t y,
                         struct wl_data_offer *offer)
{
        (void)d; (void)device; (void)serial; (void)su; (void)x; (void)y; (void)offer;
}

static void device_leave(void *d, struct wl_data_device *device)
{
        (void)d; (void)device;
}

static void device_motion(void *d, struct wl_data_device *device, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
        (void)d; (void)device; (void)t; (void)x; (void)y;
}

static void device_drop(void *d, struct wl_data_device *device)
{
        (void)d; (void)device;
}

static void device_selection(void *d, struct wl_data_device *device, struct wl_data_offer *offer)
{
        (void)d; (void)device;
        if (selection_offer)
                wl_data_offer_destroy(selection_offer);
        selection_offer = offer;
        printf("selection %s\n", offer ? "offered" : "none");
        fflush(stdout);
}

static const struct wl_data_device_listener device_listener = {
    .data_offer = device_data_offer, .enter = device_enter, .leave = device_leave, .motion = device_motion, .drop = device_drop,
    .selection = device_selection};

// A key of the person's: the copy this client was asked to make, or the paste it was asked to do, with that key's serial.
static void clipboard_key(uint32_t serial)
{
        if (copy_text && !copy_done && data_device)
        {
                copy_done = 1;
                copy_source = wl_data_device_manager_create_data_source(data_manager);
                wl_data_source_add_listener(copy_source, &source_listener, 0);
                wl_data_source_offer(copy_source, "text/plain;charset=utf-8");
                wl_data_device_set_selection(data_device, copy_source, serial);
                printf("copy set\n");
                fflush(stdout);
        }
        else if (paste_wanted && !paste_done && data_device)
        {
                int fds[2];
                char text[256];
                ssize_t got = 0, more;
                time_t end = time(0) + 3;

                paste_done = 1;
                if (!selection_offer || pipe(fds) < 0)
                {
                        printf("paste: no selection\n");
                        fflush(stdout);
                        return;
                }
                wl_data_offer_receive(selection_offer, "text/plain;charset=utf-8", fds[1]);
                close(fds[1]);
                wl_display_flush(display);
                fcntl(fds[0], F_SETFL, O_NONBLOCK);
                while (time(0) < end && got < (ssize_t)sizeof text - 1)
                {
                        more = read(fds[0], text + got, sizeof text - 1 - (size_t)got);
                        if (more == 0)
                                break;
                        if (more > 0)
                                got += more;
                        else if (errno == EAGAIN)
                                usleep(10000);
                        else
                                break;
                }
                close(fds[0]);
                text[got] = 0;
                printf("pasted %zd %s\n", got, text);
                fflush(stdout);
        }
}

static void keyboard_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t state)
{
        (void)d; (void)k; (void)t;
        if (state == 1)
                clipboard_key(s);
        key_events++;
        if (verbose)
        {
#ifdef HAVE_XKB
                if (xkb_st)
                {
                        char name[64] = "?", utf8[16] = "";
                        xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_st, key + 8);

                        xkb_keysym_get_name(sym, name, sizeof name);
                        xkb_state_key_get_utf8(xkb_st, key + 8, utf8, sizeof utf8);
                        printf("key %u %u %s %s\n", key, state, name, utf8);
                }
                else
#endif
                        printf("key %u %u\n", key, state);
                fflush(stdout);
        }
}

static void keyboard_modifiers(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g)
{
        (void)d; (void)k; (void)s; (void)g;
#ifdef HAVE_XKB
        if (xkb_st)
                xkb_state_update_mask(xkb_st, a, b, c, 0, 0, g);
#endif
        if (verbose)
                printf("modifiers %u %u %u\n", a, b, c), fflush(stdout);
}

static void keyboard_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
        (void)d; (void)k;
        if (verbose)
                printf("repeat %d %d\n", rate, delay), fflush(stdout);
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
        else if (!strcmp(interface, wl_subcompositor_interface.name))
                subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
        else if (!strcmp(interface, wl_data_device_manager_interface.name))
                data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, version < 3 ? version : 3);
        else if (!strcmp(interface, wl_seat_interface.name))
                seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 9 ? version : 9);
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

        while ((option = getopt(argc, argv, "n:w:h:t:a:smfduC:Pqv")) != -1)
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
                case 'u': child_wanted = 1; break;
                case 'C': copy_text = optarg; break;
                case 'P': paste_wanted = 1; break;
                case 'q': quiet = 1; break;
                case 'v': verbose = 1; break;
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
        if (seat && data_manager)
        {
                data_device = wl_data_device_manager_get_data_device(data_manager, seat);
                wl_data_device_add_listener(data_device, &device_listener, 0);
        }
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
        if (child_wanted)
        {
                // A subsurface the window shows at once, not held for the window's commit.
                struct buffer picture;
                struct wl_surface *child;
                struct wl_subsurface *sub;

                if (!subcompositor || make_buffer(&picture, 64, 48) < 0)
                {
                        fprintf(stderr, "wayland-shm: no subcompositor\n");
                        return 2;
                }
                for (int at = 0; at < 64 * 48; at++)
                        ((unsigned *)picture.data)[at] = 0xff10ff20u;
                child = wl_compositor_create_surface(compositor);
                sub = wl_subcompositor_get_subsurface(subcompositor, child, surface);
                wl_subsurface_set_position(sub, 10, 20);
                wl_subsurface_set_desync(sub);
                wl_surface_attach(child, picture.buffer, 0, 0);
                wl_surface_damage_buffer(child, 0, 0, 64, 48);
                wl_surface_commit(child);
        }
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
