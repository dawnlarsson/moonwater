/*
        Moonwater Canvas, the X11 and XWayland hooks.

        The X side of Canvas's desktop, with the same rules as wayland.c: the
        functions the rest of Canvas calls, a checked engine in shell.h, no
        protocol here. The X wire protocol and its parser are not built. When
        the parser lands it is the only code that reads a byte an X client sent,
        and it calls these hooks with numbers and strings it has decoded.

        Built only with CONFIG_MOONWATER_X11 (Kconfig, off by default). With the
        switch off the Makefile does not build this file, and x11.h's hooks are
        inline no-ops. X11 and Wayland share no state: each file has its own
        struct, and a client of one is never looked up in the other.

        THREAT MODEL, RULES AND ENGINE

        These are wayland.c's, word for word where they apply. Every number and
        byte from an X client is hostile, including what a parser has decoded.
        Each hook checks its arguments before it touches state, and a refused
        call changes nothing. The file may not touch DRM or a device, may hold
        one static struct and nothing else, keeps every table fixed-size, does
        not allocate from a client's size, and never closes, duplicates or reads
        a descriptor. The engine is shell.h's.

        WHAT EACH X REQUEST CALLS

        The EWMH names below are the ones a window manager gets from a client.
        The X request's bytes are the parser's; these are the meanings.

        Clients and surfaces:
          x11_start, x11_stop                 Canvas starting and stopping the X
                                              side; stop closes every window
          x11_client_connect, _disconnect     an X client's connection, and its end
          x11_window_role                     _NET_WM_WINDOW_TYPE, mapped to a type
                                              number (X11_TYPE_*): NORMAL, DIALOG,
                                              UTILITY and SPLASH are toplevels;
                                              DOCK and DESKTOP are layer surfaces;
                                              MENU, POPUP_MENU, TOOLTIP,
                                              NOTIFICATION and OVERRIDE (no type, or
                                              override-redirect) are popups
          xwayland_start, xwayland_stop       XWayland's server, which is also a
                                              Wayland client; its process is not
                                              started here, only recorded

        Buffers and drawing:
          x11_buffer_offer                    a MIT-SHM segment or a DRI3 dma-buf:
                                              geometry and format checked, bytes
                                              capped; recorded only
          x11_damage                          a rectangle of a buffer, forwarded as
                                              window_damage()

        Input (the numbers are checked here; the sink decides what a client may
        inject):
          x11_input_key                       XTest or XSendEvent key events, to
                                              compositor_key
          x11_input_pointer                   pointer motion and buttons, to
                                              compositor_pointer_to

        Window management (EWMH):
          x11_window_place                    _NET_MOVERESIZE_WINDOW, position:
                                              compositor_place(row, x, y) and the
                                              placement's screen (windows_place_set)
          x11_window_size                     _NET_MOVERESIZE_WINDOW, size: the window's
                                              width and height with the placement's
                                              SIZE bit
          x11_window_minimize                 _NET_WM_STATE_HIDDEN, and a request to
                                              iconify: compositor_minimize_row(row)
          x11_window_maximize                 _NET_WM_STATE_MAXIMIZED_VERT and _HORZ:
                                              WINDOW_PLACE_MAXIMIZED through
                                              windows_place_set and
                                              compositor_places_write
          x11_window_fullscreen               _NET_WM_STATE_FULLSCREEN:
                                              WINDOW_PLACE_FULLSCREEN, same path
          x11_window_stack                    _NET_WM_STATE_ABOVE and _BELOW: the z
                                              clamped to WINDOW_Z_MAX, through
                                              compositor_raise and compositor_stack_place
          x11_window_activate                 _NET_ACTIVE_WINDOW: compositor_focus(row)
                                              and compositor_raise(row)
          x11_window_close                    _NET_CLOSE_WINDOW: compositor_close(row)
          x11_window_title                    _NET_WM_NAME and WM_NAME: the title field
                                              (WINDOW_TITLE_MAX)
          x11_window_app_id                   WM_CLASS: recorded here for the taskbar
          x11_window_query                    the read side, for the state and geometry
                                              a client asks for
          x11_window_strut                    _NET_WM_STRUT_PARTIAL, and _NET_WM_STRUT:
                                              an edge and a size, reserved as a band
                                              of the taskbar's edge (compositor_bands,
                                              compositor_band_room)

        WHAT IS NOT IN THIS FILE, AND WHY

        - The X wire protocol and its parser: the byte-reading code, which gets
          its own fuzzer once these hooks are proven.
        - The XWayland process: starting it, its socket and its lifetime. It is
          a process, and it belongs with the session code, not the hook file.
        - The buffer import (shared memory, dma-buf): a later sink callback, as
          for Wayland.
        - Selections, drag and drop, the window manager hints beyond these: each
          is its own surface, not a window property.
*/
#ifndef CONFIG_MOONWATER_X11
#error "x11.c is built only with CONFIG_MOONWATER_X11; with the switch off the Makefile does not build it"
#endif

#include "x11.h"

/*
        The one state this file has, like wayland.c's. XWayland's record lives in
        it too, as a flag the engine keeps.
*/
static struct shell_side x11;

/*
        The X window type a client gave, as the role a Canvas window takes. An
        unknown type has no role, and the caller refuses it before this runs.
*/
static long x11_role_of(long type)
{
        switch (type)
        {
        case X11_TYPE_NORMAL:
        case X11_TYPE_DIALOG:
        case X11_TYPE_UTILITY:
        case X11_TYPE_SPLASH:
                return SHELL_ROLE_TOPLEVEL;
        case X11_TYPE_DOCK:
        case X11_TYPE_DESKTOP:
                return SHELL_ROLE_LAYER;
        case X11_TYPE_MENU:
        case X11_TYPE_POPUP_MENU:
        case X11_TYPE_TOOLTIP:
        case X11_TYPE_NOTIFICATION:
        case X11_TYPE_OVERRIDE:
                return SHELL_ROLE_POPUP;
        default:
                return 0;
        }
}

int x11_start(const struct shell_sink *sink)
{
        return shell_start(&x11, sink);
}

int x11_stop(void)
{
        return shell_stop(&x11);
}

int x11_client_connect(void)
{
        return shell_client_connect(&x11);
}

int x11_client_disconnect(long client)
{
        return shell_client_disconnect(&x11, client);
}

int x11_window_role(long client, long window, long type)
{
        long role;

        if (type < 0 || type >= X11_TYPE_COUNT)
                return SHELL_EINVAL;
        role = x11_role_of(type);
        return shell_role(&x11, client, window, role);
}

int x11_buffer_offer(long client, const struct shell_buffer *buffer)
{
        return shell_buffer_offer(&x11, client, buffer);
}

int x11_damage(long client, long window, long buffer, const struct shell_rect *rect)
{
        return shell_damage(&x11, client, window, buffer, rect);
}

int x11_input_key(long client, long code, long pressed)
{
        return shell_input_key(&x11, client, code, pressed);
}

int x11_input_pointer(long client, long x, long y, long buttons)
{
        return shell_input_pointer(&x11, client, x, y, buttons);
}

int x11_window_place(long client, long window, long x, long y, long screen)
{
        return shell_window_place(&x11, client, window, x, y, screen);
}

int x11_window_size(long client, long window, long width, long height)
{
        return shell_window_size(&x11, client, window, width, height);
}

int x11_window_minimize(long client, long window, long on)
{
        return shell_window_minimize(&x11, client, window, on);
}

int x11_window_maximize(long client, long window, long on)
{
        return shell_window_maximize(&x11, client, window, on);
}

int x11_window_fullscreen(long client, long window, long on)
{
        return shell_window_fullscreen(&x11, client, window, on);
}

int x11_window_stack(long client, long window, long layer)
{
        return shell_window_stack(&x11, client, window, layer);
}

int x11_window_activate(long client, long window)
{
        return shell_window_activate(&x11, client, window);
}

int x11_window_close(long client, long window)
{
        return shell_window_close(&x11, client, window);
}

int x11_window_title(long client, long window, const char *text, long length)
{
        return shell_window_title(&x11, client, window, text, length);
}

int x11_window_app_id(long client, long window, const char *text, long length)
{
        return shell_window_app_id(&x11, client, window, text, length);
}

int x11_window_query(long client, long window, struct shell_facts *facts)
{
        return shell_window_query(&x11, client, window, facts);
}

int x11_window_strut(long client, long window, long edge, long size)
{
        return shell_layer_strut(&x11, client, window, edge, size);
}

int xwayland_start(void)
{
        return shell_xwayland_start(&x11);
}

int xwayland_stop(void)
{
        return shell_xwayland_stop(&x11);
}
