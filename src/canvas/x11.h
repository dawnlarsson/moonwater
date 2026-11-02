/*
        Moonwater Canvas, the X11 and XWayland hooks as the rest of Canvas sees them.

        X11_HOOKS names every hook once, with its parameters and, in the fourth
        column, the same names cast to void. With CONFIG_MOONWATER_X11 on, each
        one is declared here and defined in x11.c. With it off, each one is a
        static inline that answers SHELL_EOPNOTSUPP, with the parameters kept
        type-checked and nothing else emitted. Canvas includes this header
        either way, and x11.c is not built when the switch is off.

        The window types are the numbers an X client's _NET_WM_WINDOW_TYPE is
        mapped to before it reaches x11_window_role. The X wire protocol is not
        parsed here; that is a parser that lands later, as wayland.c's header
        says for the Wayland one.
*/
#ifndef CANVAS_X11_INCLUDED
#define CANVAS_X11_INCLUDED

#include "shell.h"

/*
        What an X window says it is. A window that sets no type, or sets
        override-redirect, is OVERRIDE, which is a popup: nothing a window
        manager would manage.
*/
#define X11_TYPE_NORMAL 0L
#define X11_TYPE_DIALOG 1L
#define X11_TYPE_UTILITY 2L
#define X11_TYPE_SPLASH 3L
#define X11_TYPE_DOCK 4L
#define X11_TYPE_DESKTOP 5L
#define X11_TYPE_MENU 6L
#define X11_TYPE_POPUP_MENU 7L
#define X11_TYPE_TOOLTIP 8L
#define X11_TYPE_NOTIFICATION 9L
#define X11_TYPE_OVERRIDE 10L
#define X11_TYPE_COUNT 11L

#define X11_HOOKS(H) \
        H(int, x11_start, (const struct shell_sink *sink), ((void)sink)) \
        H(int, x11_stop, (void), ((void)0)) \
        H(int, x11_client_connect, (void), ((void)0)) \
        H(int, x11_client_disconnect, (long client), ((void)client)) \
        H(int, x11_window_role, (long client, long window, long type), ((void)client, (void)window, (void)type)) \
        H(int, x11_buffer_offer, (long client, const struct shell_buffer *buffer), ((void)client, (void)buffer)) \
        H(int, x11_damage, (long client, long window, long buffer, const struct shell_rect *rect), ((void)client, (void)window, (void)buffer, (void)rect)) \
        H(int, x11_input_key, (long client, long code, long pressed), ((void)client, (void)code, (void)pressed)) \
        H(int, x11_input_pointer, (long client, long x, long y, long buttons), ((void)client, (void)x, (void)y, (void)buttons)) \
        H(int, x11_window_place, (long client, long window, long x, long y, long screen), ((void)client, (void)window, (void)x, (void)y, (void)screen)) \
        H(int, x11_window_size, (long client, long window, long width, long height), ((void)client, (void)window, (void)width, (void)height)) \
        H(int, x11_window_minimize, (long client, long window, long on), ((void)client, (void)window, (void)on)) \
        H(int, x11_window_maximize, (long client, long window, long on), ((void)client, (void)window, (void)on)) \
        H(int, x11_window_fullscreen, (long client, long window, long on), ((void)client, (void)window, (void)on)) \
        H(int, x11_window_stack, (long client, long window, long layer), ((void)client, (void)window, (void)layer)) \
        H(int, x11_window_activate, (long client, long window), ((void)client, (void)window)) \
        H(int, x11_window_close, (long client, long window), ((void)client, (void)window)) \
        H(int, x11_window_title, (long client, long window, const char *text, long length), ((void)client, (void)window, (void)text, (void)length)) \
        H(int, x11_window_app_id, (long client, long window, const char *text, long length), ((void)client, (void)window, (void)text, (void)length)) \
        H(int, x11_window_query, (long client, long window, struct shell_facts *facts), ((void)client, (void)window, (void)facts)) \
        H(int, x11_window_strut, (long client, long window, long edge, long size), ((void)client, (void)window, (void)edge, (void)size)) \
        H(int, xwayland_start, (void), ((void)0)) \
        H(int, xwayland_stop, (void), ((void)0))

#ifdef CONFIG_MOONWATER_X11
#define X11_DECLARE(ret, name, params, unused) ret name params;
X11_HOOKS(X11_DECLARE)
#undef X11_DECLARE
#else
#define X11_OFF(ret, name, params, unused) \
        static inline ret name params { unused; return SHELL_EOPNOTSUPP; }
X11_HOOKS(X11_OFF)
#undef X11_OFF
#endif

#endif /* CANVAS_X11_INCLUDED */
