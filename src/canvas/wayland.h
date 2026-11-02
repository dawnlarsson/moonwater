/*
        Moonwater Canvas, the Wayland hooks as the rest of Canvas sees them.

        WAYLAND_HOOKS names every hook once, with its parameters and, in the
        fourth column, the same names cast to void. With CONFIG_MOONWATER_WAYLAND
        on, each one is declared here and defined in wayland.c. With it off,
        each one is a static inline that answers SHELL_EOPNOTSUPP: the void
        casts keep the parameters type-checked and used, and nothing else is
        emitted. Canvas includes this header either way and calls the same
        names. It never includes wayland.c, and wayland.c is not built when the
        switch is off.

        The meanings and the rules are in wayland.c's header.
*/
#ifndef CANVAS_WAYLAND_INCLUDED
#define CANVAS_WAYLAND_INCLUDED

#include "shell.h"

#define WAYLAND_HOOKS(H) \
        H(int, wayland_start, (const struct shell_sink *sink), ((void)sink)) \
        H(int, wayland_stop, (void), ((void)0)) \
        H(int, wayland_client_connect, (void), ((void)0)) \
        H(int, wayland_client_disconnect, (long client), ((void)client)) \
        H(int, wayland_role, (long client, long surface, long role), ((void)client, (void)surface, (void)role)) \
        H(int, wayland_buffer_offer, (long client, const struct shell_buffer *buffer), ((void)client, (void)buffer)) \
        H(int, wayland_damage, (long client, long surface, long buffer, const struct shell_rect *rect), ((void)client, (void)surface, (void)buffer, (void)rect)) \
        H(int, wayland_input_key, (long client, long code, long pressed), ((void)client, (void)code, (void)pressed)) \
        H(int, wayland_input_pointer, (long client, long x, long y, long buttons), ((void)client, (void)x, (void)y, (void)buttons)) \
        H(int, wayland_window_place, (long client, long surface, long x, long y, long screen), ((void)client, (void)surface, (void)x, (void)y, (void)screen)) \
        H(int, wayland_window_size, (long client, long surface, long width, long height), ((void)client, (void)surface, (void)width, (void)height)) \
        H(int, wayland_window_minimize, (long client, long surface, long on), ((void)client, (void)surface, (void)on)) \
        H(int, wayland_window_maximize, (long client, long surface, long on), ((void)client, (void)surface, (void)on)) \
        H(int, wayland_window_fullscreen, (long client, long surface, long on), ((void)client, (void)surface, (void)on)) \
        H(int, wayland_window_stack, (long client, long surface, long layer), ((void)client, (void)surface, (void)layer)) \
        H(int, wayland_window_activate, (long client, long surface), ((void)client, (void)surface)) \
        H(int, wayland_window_close, (long client, long surface), ((void)client, (void)surface)) \
        H(int, wayland_window_title, (long client, long surface, const char *text, long length), ((void)client, (void)surface, (void)text, (void)length)) \
        H(int, wayland_window_app_id, (long client, long surface, const char *text, long length), ((void)client, (void)surface, (void)text, (void)length)) \
        H(int, wayland_window_query, (long client, long surface, struct shell_facts *facts), ((void)client, (void)surface, (void)facts)) \
        H(int, wayland_layer_surface, (long client, long surface, const struct shell_layer *layer), ((void)client, (void)surface, (void)layer))

#ifdef CONFIG_MOONWATER_WAYLAND
#define WAYLAND_DECLARE(ret, name, params, unused) ret name params;
WAYLAND_HOOKS(WAYLAND_DECLARE)
#undef WAYLAND_DECLARE
#else
#define WAYLAND_OFF(ret, name, params, unused) \
        static inline ret name params { unused; return SHELL_EOPNOTSUPP; }
WAYLAND_HOOKS(WAYLAND_OFF)
#undef WAYLAND_OFF
#endif

#endif /* CANVAS_WAYLAND_INCLUDED */
