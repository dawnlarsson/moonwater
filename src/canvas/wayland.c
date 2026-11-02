/*
        Moonwater Canvas, the Wayland hooks.

        This file is the whole of Canvas's Wayland surface: the functions the
        rest of Canvas calls to start and stop Wayland clients, to hear about
        them, and to hand them what they may do to a window. It contains no
        protocol. The wire format, the object table and the parser that turns
        bytes into these calls are a separate file that does not exist yet,
        and when it does it is the only code that reads a byte a client sent.

        It is built only with CONFIG_MOONWATER_WAYLAND (Kconfig, off by
        default). With the switch off the Makefile does not build this file at
        all, and the declarations in wayland.h become inline functions that do
        nothing and compile to nothing. Nothing about an X client is in here:
        that is src/canvas/x11.c, with its own switch and its own state.

        DESIGN

        Data flows one way on the client side and one way on the Canvas side:

            client bytes -> protocol parser (not built) -> hooks (this file)
                         -> sink (Canvas's own functions, handed in at start)

        The parser calls a hook with numbers and strings it has already decoded
        from the wire. A hook validates every argument, records what the client
        is allowed to have recorded, and asks Canvas to act through the sink.
        Canvas never calls the parser, the parser never calls Canvas, and this
        file never calls anything outside its sink and the shared header
        shell.h.

        THREAT MODEL

        Every byte and every number that comes from a Wayland client is hostile.
        That includes the values a parser has already decoded: a length, an
        object or surface number, a size, a position, an enumeration, a file
        descriptor. A hook must not trust the parser to have bounded them. Each
        hook checks each argument against the caps in shell.h before it reads
        or writes its own state, and a rejected call leaves that state exactly
        as it was (CHECK_shell compares the whole struct before and after).

        A client may be any program the user runs, including one in a bowl.
        This file does not tell them apart and does not try: it grants a client
        nothing that the sink would refuse. Policy about which client may do
        what is Canvas's, behind the sink.

        RULES FOR WHAT THIS FILE MAY TOUCH

        1. No DRM and no device. No ioctl, no open, no mmap, no drm header, no
           dma-buf. A buffer is recorded by its descriptor and its geometry and
           is never imported here; the import is a later file behind a sink
           callback.
        2. No global state outside the one static struct wayland_state. A
           second static variable in this file is a review failure.
        3. Bounded buffers only. Every table is a fixed array sized by a SHELL_
           cap. A client's count, size or length is compared with a cap and is
           never the size of an allocation. This file calls no allocator at all.
        4. A file descriptor is recorded, never closed, duplicated, read or
           passed on. Its owner is the parser, which closes it when the client
           is gone.
        5. Strings a client gives are copied into fixed arrays after they are
           checked for length and for control bytes. They are never trusted as
           NUL-terminated and are never printed here.
        6. A hook that is not running (start has not succeeded, or stop has
           run) answers SHELL_EINVAL without touching anything.

        WHAT EACH PROTOCOL'S REQUESTS CALL

        The hook names are the meanings a request has, not the request names.
        Each protocol's exact XML names are fixed when its parser lands, against
        the protocol files then on the box; this list is what those requests
        will be routed to.

        Client lifecycle and surfaces:
          wayland_start, wayland_stop         Canvas starting and stopping the
                                              Wayland side (the compositor's
                                              start path; stop closes every
                                              window the clients had)
          wayland_client_connect              a client's socket accepted
          wayland_client_disconnect           the socket gone; its windows close
          wayland_role                        xdg_toplevel -> TOPLEVEL (sink->open
                                              takes a window row, as windows_take
                                              does for a Spark program);
                                              layer_surface -> LAYER; xdg_popup,
                                              subsurface and cursor are recorded
                                              and forwarded to nothing yet

        Buffers and drawing:
          wayland_buffer_offer                wl_shm pool buffer, or a linux-dmabuf
                                              buffer: geometry and format checked,
                                              bytes capped; recorded only
          wayland_damage                      wl_surface.damage_buffer: a rectangle
                                              inside the buffer, forwarded as
                                              window_damage(window, row, rows)

        Input (forwarded to Canvas's own handlers; the sink decides what a
        client may inject, this file only checks the numbers):
          wayland_input_key                   wl_keyboard key, and a virtual
                                              keyboard -> the window's key ring
          wayland_input_pointer               wl_pointer motion and buttons, and
                                              a virtual pointer -> compositor_key
                                              and compositor_pointer_to

        Window management, the same meaning in three protocols: the KDE
        plasma-window-management protocol (org_kde_plasma_window_management and
        its plasma_window), the wlr foreign-toplevel-management protocol
        (zwlr_foreign_toplevel_handle_v1), and the wlr layer shell for panels
        and taskbars (zwlr_layer_shell_v1). Where a request exists in only one
        of them, the other still reaches the same hook through the same state
        check.
          wayland_window_place                position request, with an output
                                              number: compositor_place(row, x, y)
                                              and the placement's screen
                                              (windows_place_set)
          wayland_window_size                 size request: the window's width and
                                              height with the placement's SIZE bit
                                              (window_place, windows_place_set); the
                                              interactive resize path is not used
          wayland_window_minimize             set and unset minimized:
                                              compositor_minimize_row(row) and
                                              WINDOW_MINIMIZED
          wayland_window_maximize             set and unset maximized: the placement
                                              WINDOW_PLACE_MAXIMIZED through
                                              windows_place_set and
                                              compositor_places_write
          wayland_window_fullscreen           set and unset fullscreen: the placement
                                              WINDOW_PLACE_FULLSCREEN, same path
          wayland_window_stack                keep above (+1), normal (0), keep below
                                              (-1): the window's z, clamped to
                                              WINDOW_Z_MAX, through compositor_raise
                                              and compositor_stack_place
          wayland_window_activate             request activate, the foreign-toplevel
                                              activate with a seat: compositor_focus(row)
                                              and compositor_raise(row)
          wayland_window_close                request close: compositor_close(row)
                                              (WINDOW_CLOSING)
          wayland_window_title                title event: the window's title field
                                              (WINDOW_TITLE_MAX), compositor_title_length
          wayland_window_app_id               app_id event: recorded here only, for
                                              the taskbar to read through
                                              wayland_window_query
          wayland_window_query                the read side: position, size, z, the
                                              state bits, title and app_id, for the
                                              state, geometry and done events
          wayland_layer_surface               layer, anchor, exclusive zone, size and
                                              margins of a panel: compositor_bands and
                                              compositor_band_room, with the
                                              taskbar's edge (TASKBAR_BOTTOM, TOP,
                                              LEFT, RIGHT)

        The sink is a struct of callbacks that Canvas fills in and hands to
        wayland_start. Every callback is required; start refuses a sink with
        a missing one, so the hooks never call a NULL.

        WHAT IS NOT IN THIS FILE, AND WHY

        - The protocol parser: it is the byte-reading code that the threat model
          is about, and it lands as its own file, with its own fuzzer, after
          these hooks are proven.
        - The buffer import (memory mapping of a shared pool, dma-buf import):
          it needs the DRM side and is a sink callback in a later step.
        - Clipboard, drag and drop, cursor shapes, popups and pointer constraints:
          they are not window properties, and each one is its own protocol
          surface.
*/
#ifndef CONFIG_MOONWATER_WAYLAND
#error "wayland.c is built only with CONFIG_MOONWATER_WAYLAND; with the switch off the Makefile does not build it"
#endif

#include "wayland.h"

/*
        The one state this file has. Every hook below is a call into shell.h's
        engine with it, so a second static variable here would be a review
        failure: the engine's rules hold for this struct and no other.
*/
static struct shell_side wayland;

int wayland_start(const struct shell_sink *sink)
{
        return shell_start(&wayland, sink);
}

int wayland_stop(void)
{
        return shell_stop(&wayland);
}

int wayland_client_connect(void)
{
        return shell_client_connect(&wayland);
}

int wayland_client_disconnect(long client)
{
        return shell_client_disconnect(&wayland, client);
}

int wayland_role(long client, long surface, long role)
{
        return shell_role(&wayland, client, surface, role);
}

int wayland_buffer_offer(long client, const struct shell_buffer *buffer)
{
        return shell_buffer_offer(&wayland, client, buffer);
}

int wayland_damage(long client, long surface, long buffer, const struct shell_rect *rect)
{
        return shell_damage(&wayland, client, surface, buffer, rect);
}

int wayland_input_key(long client, long code, long pressed)
{
        return shell_input_key(&wayland, client, code, pressed);
}

int wayland_input_pointer(long client, long x, long y, long buttons)
{
        return shell_input_pointer(&wayland, client, x, y, buttons);
}

int wayland_window_place(long client, long surface, long x, long y, long screen)
{
        return shell_window_place(&wayland, client, surface, x, y, screen);
}

int wayland_window_size(long client, long surface, long width, long height)
{
        return shell_window_size(&wayland, client, surface, width, height);
}

int wayland_window_minimize(long client, long surface, long on)
{
        return shell_window_minimize(&wayland, client, surface, on);
}

int wayland_window_maximize(long client, long surface, long on)
{
        return shell_window_maximize(&wayland, client, surface, on);
}

int wayland_window_fullscreen(long client, long surface, long on)
{
        return shell_window_fullscreen(&wayland, client, surface, on);
}

int wayland_window_stack(long client, long surface, long layer)
{
        return shell_window_stack(&wayland, client, surface, layer);
}

int wayland_window_activate(long client, long surface)
{
        return shell_window_activate(&wayland, client, surface);
}

int wayland_window_close(long client, long surface)
{
        return shell_window_close(&wayland, client, surface);
}

int wayland_window_title(long client, long surface, const char *text, long length)
{
        return shell_window_title(&wayland, client, surface, text, length);
}

int wayland_window_app_id(long client, long surface, const char *text, long length)
{
        return shell_window_app_id(&wayland, client, surface, text, length);
}

int wayland_window_query(long client, long surface, struct shell_facts *facts)
{
        return shell_window_query(&wayland, client, surface, facts);
}

int wayland_layer_surface(long client, long surface, const struct shell_layer *layer)
{
        return shell_layer_surface(&wayland, client, surface, layer);
}
