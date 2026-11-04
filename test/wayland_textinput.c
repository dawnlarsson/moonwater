/*
        A text field on libwayland, for the on-screen keyboard's rows (test/wayland_textinput.sh): a toplevel on a wl_shm buffer, with a
        zwp_text_input_v3 on its seat. Its standard input is the script's commands, one a line, and its standard output says what it saw,
        one line each:

            ready                          the window is drawn
            enter, leave                   the keyboard came to the window, or left it
            done N                         a commit was applied (its serial)
            key CODE STATE MODS            a key of the seat reached the window (STATE 1 down, 0 up; MODS the depressed modifiers)
            error                          the connection ended with a protocol error (exit 1)

        Commands: enable, disable, commit, purpose N (the content purpose, with no hint), caret X Y W H (the cursor rectangle),
        destroy (the text input object), new (a text input object again), raw ID OP [WORD...] (the words written to the connection
        as they are: a request to an object that is not there, or with an opcode it does not have), sync (a round trip, and sync is
        said), die (exit at once, without a word and without closing anything), quit (a clean exit).

        Built with the libwayland client and the generated headers of xdg-shell and text-input-unstable-v3:

            wayland-scanner client-header text-input-unstable-v3.xml text-input-client.h
            wayland-scanner private-code  text-input-unstable-v3.xml text-input-code.c
            (and the same for xdg-shell.xml)
            cc -std=gnu11 -Wall -Wextra -I. test/wayland_textinput.c xdg-shell-code.c text-input-code.c -lwayland-client -o wayland-textinput

        wayland-textinput [-t TITLE] [-W WIDTH] [-H HEIGHT] [-M]
        -M: a press on the window starts its move (xdg_toplevel.move), so a pointer drag moves it.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client.h"
#include "text-input-client.h"

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_keyboard *keyboard;
static struct wl_pointer *pointer;
static int move_on_press; // -M: a press on the window starts its move (xdg_toplevel.move), as a client that drags its own frame does
static struct xdg_wm_base *wm_base;
static struct zwp_text_input_manager_v3 *manager;
static struct zwp_text_input_v3 *text;
static struct wl_surface *surface;
static struct xdg_surface *xsurface;
static struct xdg_toplevel *toplevel;
static const char *title = "Field";
static int width = 320, height = 240, drawn, running = 1;
static uint32_t depressed;
static char input[4096];
static size_t input_length;

static void say(const char *line)
{
        printf("%s\n", line);
        fflush(stdout);
}

/* ---- the keyboard of the seat ---- */

static void key_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size)
{
        (void)data; (void)k; (void)format; (void)size;
        close(fd); // the keymap is the compositor's; this client takes the codes as they come
}

static void key_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys)
{
        (void)data; (void)k; (void)serial; (void)s; (void)keys;
}

static void key_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
        (void)data; (void)k; (void)serial; (void)s;
}

static void key_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t code, uint32_t state)
{
        char line[64];

        (void)data; (void)k; (void)serial; (void)time;
        snprintf(line, sizeof line, "key %u %u %u", (unsigned)code, (unsigned)(state ? 1 : 0), (unsigned)depressed);
        say(line);
}

static void key_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t mods_depressed, uint32_t latched, uint32_t locked,
                          uint32_t group)
{
        (void)data; (void)k; (void)serial; (void)latched; (void)locked; (void)group;
        depressed = mods_depressed;
}

static void key_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
        (void)data; (void)k; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {key_keymap, key_enter, key_leave, key_key, key_modifiers, key_repeat};

/* ---- the pointer: a press is said, and with -M a press starts a move of the window (the script drags it into the room above the keyboard) */

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
        (void)data; (void)p; (void)serial; (void)s; (void)x; (void)y;
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
        (void)data; (void)p; (void)serial; (void)s;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
        (void)data; (void)p; (void)time; (void)x; (void)y;
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
        (void)data; (void)p; (void)time; (void)button;
        if (state != WL_POINTER_BUTTON_STATE_PRESSED)
                return;
        say("press");
        if (move_on_press && toplevel)
                xdg_toplevel_move(toplevel, seat, serial);
}

static void pointer_frame(void *data, struct wl_pointer *p)
{
        (void)data; (void)p;
}

static const struct wl_pointer_listener pointer_listener = {
        .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion, .button = pointer_button, .frame = pointer_frame,
};

static void seat_capabilities(void *data, struct wl_seat *s, uint32_t caps)
{
        (void)data;
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard)
        {
                keyboard = wl_seat_get_keyboard(s);
                wl_keyboard_add_listener(keyboard, &keyboard_listener, 0);
        }
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer)
        {
                pointer = wl_seat_get_pointer(s);
                wl_pointer_add_listener(pointer, &pointer_listener, 0);
        }
}

static void seat_name(void *data, struct wl_seat *s, const char *name)
{
        (void)data; (void)s; (void)name;
}

static const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

/* ---- the text input ---- */

static void text_enter(void *data, struct zwp_text_input_v3 *t, struct wl_surface *s)
{
        (void)data; (void)t; (void)s;
        say("enter");
}

static void text_leave(void *data, struct zwp_text_input_v3 *t, struct wl_surface *s)
{
        (void)data; (void)t; (void)s;
        say("leave");
}

static void text_preedit(void *data, struct zwp_text_input_v3 *t, const char *s, int32_t begin, int32_t end)
{
        (void)data; (void)t; (void)s; (void)begin; (void)end;
}

static void text_commit_string(void *data, struct zwp_text_input_v3 *t, const char *s)
{
        (void)data; (void)t; (void)s;
}

static void text_delete(void *data, struct zwp_text_input_v3 *t, uint32_t before, uint32_t after)
{
        (void)data; (void)t; (void)before; (void)after;
}

static void text_done(void *data, struct zwp_text_input_v3 *t, uint32_t serial)
{
        char line[32];

        (void)data; (void)t;
        snprintf(line, sizeof line, "done %u", (unsigned)serial);
        say(line);
}

static const struct zwp_text_input_v3_listener text_listener = {
        .enter = text_enter, .leave = text_leave, .preedit_string = text_preedit, .commit_string = text_commit_string,
        .delete_surrounding_text = text_delete, .done = text_done,
};

/* ---- the window ---- */

static void wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
        (void)data;
        xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = {wm_ping};

static void xsurface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
        (void)data;
        xdg_surface_ack_configure(s, serial);
}

static const struct xdg_surface_listener xsurface_listener = {xsurface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
        (void)data; (void)t; (void)w; (void)h; (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
        (void)data; (void)t;
}

static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure, .close = toplevel_close};

static void registry_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
        (void)data;
        if (!strcmp(iface, "wl_compositor"))
                compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
        else if (!strcmp(iface, "wl_shm"))
                shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
        else if (!strcmp(iface, "wl_seat"))
                seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
        else if (!strcmp(iface, "xdg_wm_base"))
                wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        else if (!strcmp(iface, "zwp_text_input_manager_v3"))
                manager = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *r, uint32_t name)
{
        (void)data; (void)r; (void)name;
}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

/* A window's buffer: one solid colour, in a file of its own, and a line once it is drawn. */
static void draw(void)
{
        int stride = width * 4, size = stride * height, fd;
        uint32_t *pixels;
        struct wl_shm_pool *pool;
        struct wl_buffer *buffer;

        fd = memfd_create("wayland-textinput", 0);
        if (fd < 0 || ftruncate(fd, size) < 0)
                exit(2);
        pixels = mmap(0, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pixels == MAP_FAILED)
                exit(2);
        for (int at = 0; at < width * height; at++)
                pixels[at] = 0xff2a4f7aU;
        pool = wl_shm_create_pool(shm, fd, size);
        buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);
        if (!drawn)
                say("ready");
        drawn = 1;
}

static void make_window(void)
{
        surface = wl_compositor_create_surface(compositor);
        xsurface = xdg_wm_base_get_xdg_surface(wm_base, surface);
        xdg_surface_add_listener(xsurface, &xsurface_listener, 0);
        toplevel = xdg_surface_get_toplevel(xsurface);
        xdg_toplevel_add_listener(toplevel, &toplevel_listener, 0);
        xdg_toplevel_set_title(toplevel, title);
        // The app id is the title: a window is placed where the hand last put one of its name, so two of these must not share it
        xdg_toplevel_set_app_id(toplevel, title);
        wl_surface_commit(surface);
        wl_display_roundtrip(display);
        draw();
}

static void text_make(void)
{
        text = zwp_text_input_manager_v3_get_text_input(manager, seat);
        zwp_text_input_v3_add_listener(text, &text_listener, 0);
}

/* ---- the commands ---- */

static void command(char *line)
{
        char *word = strtok(line, " \t");

        if (!word)
                return;
        if (!strcmp(word, "enable") && text)
                zwp_text_input_v3_enable(text);
        else if (!strcmp(word, "disable") && text)
                zwp_text_input_v3_disable(text);
        else if (!strcmp(word, "commit") && text)
                zwp_text_input_v3_commit(text);
        else if (!strcmp(word, "purpose") && text)
        {
                char *arg = strtok(0, " \t");

                zwp_text_input_v3_set_content_type(text, 0, (uint32_t)strtoul(arg ? arg : "0", 0, 10));
        }
        else if (!strcmp(word, "caret") && text)
        {
                long v[4] = {0, 0, 0, 0};

                for (int at = 0; at < 4; at++)
                {
                        char *arg = strtok(0, " \t");

                        if (arg)
                                v[at] = strtol(arg, 0, 10);
                }
                zwp_text_input_v3_set_cursor_rectangle(text, (int32_t)v[0], (int32_t)v[1], (int32_t)v[2], (int32_t)v[3]);
        }
        else if (!strcmp(word, "destroy") && text)
        {
                zwp_text_input_v3_destroy(text);
                text = 0;
        }
        else if (!strcmp(word, "new") && !text)
                text_make();
        else if (!strcmp(word, "raw"))
        {
                uint32_t words[32];
                int count = 0;
                char *arg;
                int fd = wl_display_get_fd(display);

                wl_display_flush(display);
                while ((arg = strtok(0, " \t")) && count < 32)
                        words[count++] = (uint32_t)strtoul(arg, 0, 0);
                if (count >= 2)
                {
                        uint32_t header[2];

                        // the header: the object, then the size and the opcode; the words follow as they were given
                        header[0] = words[0];
                        header[1] = (uint32_t)((8 + 4 * (count - 2)) << 16) | (words[1] & 0xffff);
                        if (write(fd, header, sizeof header) < 0 || (count > 2 && write(fd, words + 2, (size_t)(count - 2) * 4) < 0))
                                running = 0;
                }
        }
        else if (!strcmp(word, "sync"))
        {
                wl_display_roundtrip(display);
                say("sync");
        }
        else if (!strcmp(word, "die"))
                _exit(3);
        else if (!strcmp(word, "quit"))
                running = 0;
}

static int commands_closed;

static void commands_read(void)
{
        ssize_t got = read(0, input + input_length, sizeof input - input_length - 1);

        if (got <= 0)
        {
                // The script closed the pipe: the window stays (it is a blocker or a field the script has done with), and no more
                // commands are read from it.
                commands_closed = 1;
                return;
        }
        input_length += (size_t)got;
        for (;;)
        {
                char *end = memchr(input, '\n', input_length);
                size_t length;

                if (!end)
                        break;
                length = (size_t)(end - input);
                *end = 0;
                command(input);
                length++;
                memmove(input, input + length, input_length - length);
                input_length -= length;
        }
}

int main(int argc, char **argv)
{
        for (int at = 1; at < argc; at++)
        {
                if (!strcmp(argv[at], "-M"))
                        move_on_press = 1;
                else if (at + 1 < argc && !strcmp(argv[at], "-t"))
                        title = argv[++at];
                else if (at + 1 < argc && !strcmp(argv[at], "-W"))
                        width = atoi(argv[++at]);
                else if (at + 1 < argc && !strcmp(argv[at], "-H"))
                        height = atoi(argv[++at]);
        }
        display = wl_display_connect(0);
        if (!display)
                return 2;
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, 0);
        wl_display_roundtrip(display);
        if (!compositor || !shm || !seat || !wm_base || !manager)
                return 2;
        wl_seat_add_listener(seat, &seat_listener, 0);
        xdg_wm_base_add_listener(wm_base, &wm_listener, 0);
        wl_display_roundtrip(display);
        make_window();
        text_make();
        wl_display_roundtrip(display);
        while (running)
        {
                struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0}, {0, POLLIN, 0}};

                if (commands_closed)
                        fds[1].fd = -1;

                while (wl_display_prepare_read(display) != 0)
                        if (wl_display_dispatch_pending(display) < 0)
                                goto failed;
                wl_display_flush(display);
                if (poll(fds, 2, -1) < 0 && errno != EINTR)
                {
                        wl_display_cancel_read(display);
                        return 2;
                }
                if (fds[0].revents & POLLIN)
                {
                        if (wl_display_read_events(display) < 0)
                                goto failed;
                }
                else
                        wl_display_cancel_read(display);
                if (wl_display_dispatch_pending(display) < 0)
                        goto failed;
                if (!commands_closed && (fds[1].revents & (POLLIN | POLLHUP)))
                        commands_read();
                wl_display_flush(display);
        }
        wl_display_disconnect(display);
        return 0;
failed:
        say("error");
        return 1;
}
