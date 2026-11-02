/*
        Moonwater Canvas, the Wayland and X11 hooks: the one file that is
        Canvas's whole hook surface for both desktop protocols.

        The file holds, in order: this design note, the caps, the sink Canvas
        hands in, the checks and the engine every call passes, the Wayland
        hooks, then the X11 and XWayland hooks. Each side keeps its own state
        struct and the two share nothing: a client of one is never looked up
        in the other.

        It is built when either switch is on (Kconfig, both off by default).
        CONFIG_MOONWATER_WAYLAND compiles the Wayland hooks and
        CONFIG_MOONWATER_X11 the X11 and XWayland hooks; a switch that is off
        leaves its hooks out entirely, and with both off the file refuses to
        build, so a Makefile that names it without a switch is caught here.
        Nothing in Canvas calls a hook yet, so no header declares them: a
        caller declares the names it needs at its own call site.

        DESIGN

        Data flows one way on the client side and one way on the Canvas side:

            client bytes -> protocol parser (not built) -> hooks (this file)
                         -> sink (Canvas's own functions, handed in at start)

        The parser calls a hook with numbers and strings it has already decoded
        from the wire. A hook validates every argument, records what the client
        is allowed to have recorded, and asks Canvas to act through the sink.
        Canvas never calls the parser, the parser never calls Canvas, and this
        file never calls anything outside its sink and the engine below.

        THREAT MODEL

        Every byte and every number that comes from a Wayland client is hostile.
        That includes the values a parser has already decoded: a length, an
        object or surface number, a size, a position, an enumeration, a file
        descriptor. A hook must not trust the parser to have bounded them. Each
        hook checks each argument against the caps below before it reads
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
        2. No global state outside the two static structs, one per side: wayland
           and x11. A third static variable in this file is a review failure.
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

        WHAT EACH WAYLAND REQUEST CALLS

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

        - The protocol parsers (Wayland and X11 wire formats): the byte-reading
          code that the threat model is about. Each lands as its own file, with
          its own fuzzer, once these hooks are proven. It is the only code that
          reads a byte a client sent.
        - The buffer imports (shared memory, dma-buf, DRI3): a later sink
          callback, behind the same checks.
        - The XWayland process: starting it, its socket and its lifetime. It is
          a process and belongs with the session code; here it is only recorded.
        - Clipboard, selections, drag and drop, cursor shapes, popups, pointer
          constraints and the window manager hints beyond the ones listed above:
          each is its own surface, not a window property.
*/
#if !defined(CONFIG_MOONWATER_WAYLAND) && !defined(CONFIG_MOONWATER_X11)
#error "wayland.c is built only with CONFIG_MOONWATER_WAYLAND or CONFIG_MOONWATER_X11; with both off the Makefile does not build it"
#endif

/*
        THE CAPS, THE SINK AND THE ENGINE

        Every function here is static inline, so a switch that compiles a side out
        leaves its unused helpers out too. Every number a client sent is a long here,
        and every one is compared with a range before it is used as an index, a size
        or a length. An engine call that returns negative changed nothing. A hook is
        one call into this engine with its side's state struct.
*/

/*
        What a hook answers. Zero or more is success; negative is a refusal,
        and a refusal changed nothing. SHELL_EOPNOTSUPP is what every hook
        answers when its switch is off.
*/
#define SHELL_OK 0
#define SHELL_ENOENT (-2)
#define SHELL_EBUSY (-16)
#define SHELL_EINVAL (-22)
#define SHELL_ENOSPC (-28)
#define SHELL_EOPNOTSUPP (-95)

/*
        The caps. Each table is a fixed array of this many records; nothing a
        client sends can make one larger. The window title and app id are
        capped at 127 bytes, which with the terminating NUL is 128, the size of
        WINDOW_TITLE_MAX in window.c; Canvas checks that the two agree when the
        sink lands.
*/
#define SHELL_CLIENTS 32
#define SHELL_SURFACES 64
#define SHELL_BUFFERS 64
#define SHELL_SCREENS 8
#define SHELL_DIMENSION_MAX 16384
#define SHELL_COORD_MAX (1L << 24)
#define SHELL_FD_MAX (1L << 20)
#define SHELL_STRIDE_MAX (SHELL_DIMENSION_MAX * 4L)
#define SHELL_BUFFER_BYTES_MAX (1L << 28)
#define SHELL_TITLE_MAX 127
#define SHELL_APP_ID_MAX 127
#define SHELL_KEY_MAX 0x2ffL
#define SHELL_BUTTONS_MASK 0x1fL

/*
        Roles: what a surface is, given once. A toplevel and a layer surface
        are a Canvas row, through the sink's open; the others are recorded and
        forwarded to nothing.
*/
#define SHELL_ROLE_TOPLEVEL 1L
#define SHELL_ROLE_POPUP 2L
#define SHELL_ROLE_SUBSURFACE 3L
#define SHELL_ROLE_CURSOR 4L
#define SHELL_ROLE_LAYER 5L

/*
        The state bits a window reports. ABOVE and BELOW are exclusive, and
        ACTIVE is held by one window at a time. CLOSING is set by a close
        request and never cleared: a closing window takes no more requests.
*/
#define SHELL_STATE_MINIMIZED 1L
#define SHELL_STATE_MAXIMIZED 2L
#define SHELL_STATE_FULLSCREEN 4L
#define SHELL_STATE_ACTIVE 8L
#define SHELL_STATE_CLOSING 16L
#define SHELL_STATE_ABOVE 32L
#define SHELL_STATE_BELOW 64L

/*
        Layer shell. The layers are background to overlay; anchor bits are the
        edges (top, bottom, left, right, in the order the edges are numbered
        below, so the strut edge n is the anchor bit 1 << n).
*/
#define SHELL_LAYER_BACKGROUND 0L
#define SHELL_LAYER_BOTTOM 1L
#define SHELL_LAYER_TOP 2L
#define SHELL_LAYER_OVERLAY 3L
#define SHELL_ANCHOR_TOP 1L
#define SHELL_ANCHOR_BOTTOM 2L
#define SHELL_ANCHOR_LEFT 4L
#define SHELL_ANCHOR_RIGHT 8L
#define SHELL_ANCHOR_MASK 15L
#define SHELL_EDGE_TOP 0L
#define SHELL_EDGE_BOTTOM 1L
#define SHELL_EDGE_LEFT 2L
#define SHELL_EDGE_RIGHT 3L
#define SHELL_EDGE_COUNT 4L
#define SHELL_KEYBOARD_NONE 0L
#define SHELL_KEYBOARD_EXCLUSIVE 1L
#define SHELL_KEYBOARD_ON_DEMAND 2L

/*
        Buffers. A shared-memory pool or a dma-buf, and the two 32-bit formats
        Canvas can show. Nothing else is accepted until its import is written.
*/
#define SHELL_BUFFER_SHM 1L
#define SHELL_BUFFER_DMABUF 2L
#define SHELL_FORMAT_ARGB8888 0x34325241L
#define SHELL_FORMAT_XRGB8888 0x34325258L

/*
        Keys: 0 is a release, 1 a press and 2 a repeat, the three a key can
        report. Pointer buttons are the five bits of SHELL_BUTTONS_MASK.
*/
#define SHELL_KEY_RELEASE 0L
#define SHELL_KEY_PRESS 1L
#define SHELL_KEY_REPEAT 2L

/*
        What Canvas gives the hooks. Every callback is required: shell_start
        refuses a sink with one missing, so an engine call never reaches a NULL.
        Each takes the context it was handed and the values the engine has
        checked; a row is a Canvas window row, and the sink returns zero or more
        for success and a negative status for a refusal, which the engine passes
        back to the client unchanged.

        open takes a window row for a toplevel or a layer surface and returns
        it; key and pointer receive a client's input for the sink to decide on;
        query fills the geometry of a window, which the engine then completes.
*/
struct shell_rect
{
        long x, y, width, height;
};

struct shell_buffer
{
        long kind, format, fd;
        long width, height, stride;
};

struct shell_layer
{
        long layer, anchor, keyboard;
        long exclusive;
        long width, height;
        long margin[4];
};

struct shell_facts
{
        long x, y;
        long width, height;
        long z;
        long state;
        long title_length;
        char title[SHELL_TITLE_MAX + 1];
        long app_id_length;
        char app_id[SHELL_APP_ID_MAX + 1];
};

struct shell_sink
{
        void *context;
        int (*open)(void *context, long client, long role);
        int (*key)(void *context, long client, long code, long pressed);
        int (*pointer)(void *context, long client, long x, long y, long buttons);
        int (*damage)(void *context, int row, const struct shell_rect *rect);
        int (*place)(void *context, int row, long x, long y, long screen);
        int (*size)(void *context, int row, long width, long height);
        int (*minimize)(void *context, int row, long on);
        int (*maximize)(void *context, int row, long on);
        int (*fullscreen)(void *context, int row, long on);
        int (*stack)(void *context, int row, long layer);
        int (*activate)(void *context, int row);
        int (*close)(void *context, int row);
        int (*title)(void *context, int row, const char *text, long length);
        int (*layer)(void *context, int row, const struct shell_layer *layer);
        int (*query)(void *context, int row, struct shell_facts *facts);
};

/*
        What the engine keeps, one record per surface, buffer and client, all
        of it fixed-size. A surface is claimed by the role event that names its
        slot; its title and app id are copies the engine made after checking
        them, never the client's bytes.
*/
struct shell_surface
{
        long live;
        long client;
        long role;
        long state;
        long row;
        long title_length;
        char title[SHELL_TITLE_MAX + 1];
        long app_id_length;
        char app_id[SHELL_APP_ID_MAX + 1];
};

struct shell_buffer_record
{
        long live;
        long client;
        long kind, format, fd;
        long width, height, stride, bytes;
};

struct shell_side
{
        const struct shell_sink *sink;
        long running;
        long xwayland;
        long client_live[SHELL_CLIENTS];
        struct shell_surface surfaces[SHELL_SURFACES];
        struct shell_buffer_record buffers[SHELL_BUFFERS];
};

/*
        The checks. Each one is a pure function of its arguments: a range, a
        length, a flag, or a sink that is complete. None reads state.
*/
static inline int shell_index_ok(long index, long count)
{
        return index >= 0 && index < count;
}

static inline int shell_coord_ok(long value)
{
        return value >= -SHELL_COORD_MAX && value <= SHELL_COORD_MAX;
}

static inline int shell_size_ok(long value)
{
        return value >= 1 && value <= SHELL_DIMENSION_MAX;
}

static inline int shell_span_ok(long value)
{
        return value >= 0 && value <= SHELL_DIMENSION_MAX;
}

static inline int shell_on_ok(long value)
{
        return value == 0 || value == 1;
}

static inline int shell_role_ok(long role)
{
        return role >= SHELL_ROLE_TOPLEVEL && role <= SHELL_ROLE_LAYER;
}

/*
        Text from a client: a length no larger than the cap, a pointer when
        there are bytes at all, and no byte below 0x20 or at 0x7f. A title is
        shown in a window, so a control byte is refused rather than drawn.
*/
static inline int shell_text_ok(const char *text, long length, long most)
{
        if (length < 0 || length > most)
                return 0;
        if (length && !text)
                return 0;
        for (long i = 0; i < length; i++)
        {
                unsigned char c = (unsigned char)text[i];

                if (c < 0x20 || c == 0x7f)
                        return 0;
        }
        return 1;
}

/*
        A buffer's geometry, its format and its descriptor. A stride is at least
        a row of pixels, a multiple of four, and no larger than the cap; the
        bytes are the stride times the height, and they must fit the cap, which
        is checked by division so that nothing here can overflow.
*/
static inline int shell_buffer_ok(const struct shell_buffer *buffer)
{
        if (buffer->kind != SHELL_BUFFER_SHM && buffer->kind != SHELL_BUFFER_DMABUF)
                return 0;
        if (buffer->format != SHELL_FORMAT_ARGB8888 && buffer->format != SHELL_FORMAT_XRGB8888)
                return 0;
        if (buffer->fd < 0 || buffer->fd >= SHELL_FD_MAX)
                return 0;
        if (!shell_size_ok(buffer->width) || !shell_size_ok(buffer->height))
                return 0;
        if (buffer->stride < buffer->width * 4 || buffer->stride > SHELL_STRIDE_MAX)
                return 0;
        if (buffer->stride % 4)
                return 0;
        if (buffer->height > SHELL_BUFFER_BYTES_MAX / buffer->stride)
                return 0;
        return 1;
}

/*
        A damage rectangle inside its buffer: it starts inside, is at least one
        pixel each way and ends no further than the buffer's edge.
*/
static inline int shell_rect_ok(const struct shell_rect *rect, long width, long height)
{
        if (rect->x < 0 || rect->y < 0 || rect->width < 1 || rect->height < 1)
                return 0;
        if (rect->x > SHELL_DIMENSION_MAX || rect->y > SHELL_DIMENSION_MAX)
                return 0;
        if (rect->width > SHELL_DIMENSION_MAX || rect->height > SHELL_DIMENSION_MAX)
                return 0;
        return rect->x + rect->width <= width && rect->y + rect->height <= height;
}

static inline long shell_edge_count(long anchor)
{
        long count = 0;

        for (long bit = 1; bit <= SHELL_ANCHOR_RIGHT; bit <<= 1)
                count += (anchor & bit) != 0;
        return count;
}

/*
        A layer surface's configuration. An exclusive zone other than zero or
        minus one reserves room along an edge, so it needs exactly one edge to
        reserve it on; a size of zero stretches along the axis, and is allowed.
*/
static inline int shell_layer_ok(const struct shell_layer *layer)
{
        if (layer->layer < SHELL_LAYER_BACKGROUND || layer->layer > SHELL_LAYER_OVERLAY)
                return 0;
        if (layer->anchor < 0 || layer->anchor > SHELL_ANCHOR_MASK)
                return 0;
        if (layer->keyboard < SHELL_KEYBOARD_NONE || layer->keyboard > SHELL_KEYBOARD_ON_DEMAND)
                return 0;
        if (!shell_span_ok(layer->width) || !shell_span_ok(layer->height))
                return 0;
        for (int i = 0; i < 4; i++)
                if (layer->margin[i] < -SHELL_DIMENSION_MAX || layer->margin[i] > SHELL_DIMENSION_MAX)
                        return 0;
        if (layer->exclusive < -1 || layer->exclusive > SHELL_DIMENSION_MAX)
                return 0;
        if (layer->exclusive > 0 && shell_edge_count(layer->anchor) != 1)
                return 0;
        return 1;
}

static inline int shell_sink_ok(const struct shell_sink *sink)
{
        return sink && sink->open && sink->key && sink->pointer && sink->damage &&
               sink->place && sink->size && sink->minimize && sink->maximize &&
               sink->fullscreen && sink->stack && sink->activate && sink->close &&
               sink->title && sink->layer && sink->query;
}

/*
        The tables. A client is live from its connect to its disconnect, and a
        surface or buffer belongs to the client that claimed it. Lookups check
        both the index and the owner, so a stale number from another client, or
        one from before a disconnect, finds nothing.
*/
static inline void shell_surface_clear(struct shell_surface *surface)
{
        surface->live = 0;
        surface->client = 0;
        surface->role = 0;
        surface->state = 0;
        surface->row = -1;
        surface->title_length = 0;
        surface->title[0] = 0;
        surface->app_id_length = 0;
        surface->app_id[0] = 0;
}

static inline void shell_clear(struct shell_side *side)
{
        for (long i = 0; i < SHELL_CLIENTS; i++)
                side->client_live[i] = 0;
        for (long i = 0; i < SHELL_SURFACES; i++)
                shell_surface_clear(&side->surfaces[i]);
        for (long i = 0; i < SHELL_BUFFERS; i++)
                side->buffers[i].live = 0;
}

static inline int shell_client_live(const struct shell_side *side, long client)
{
        return shell_index_ok(client, SHELL_CLIENTS) && side->client_live[client];
}

static inline struct shell_surface *shell_surface_of(struct shell_side *side, long client, long surface)
{
        struct shell_surface *record;

        if (!shell_client_live(side, client) || !shell_index_ok(surface, SHELL_SURFACES))
                return 0;
        record = &side->surfaces[surface];
        return record->live && record->client == client ? record : 0;
}

static inline struct shell_buffer_record *shell_buffer_of(struct shell_side *side, long client, long buffer)
{
        struct shell_buffer_record *record;

        if (!shell_client_live(side, client) || !shell_index_ok(buffer, SHELL_BUFFERS))
                return 0;
        record = &side->buffers[buffer];
        return record->live && record->client == client ? record : 0;
}

/*
        A window is a toplevel with a Canvas row, owned by this client, and the
        side is running. Every window request starts here.
*/
static inline struct shell_surface *shell_window(struct shell_side *side, long client, long surface)
{
        struct shell_surface *record = shell_surface_of(side, client, surface);

        return side->running && record && record->role == SHELL_ROLE_TOPLEVEL && record->row >= 0 ? record : 0;
}

static inline void shell_bit(long *state, long bit, long on)
{
        if (on)
                *state |= bit;
        else
                *state &= ~bit;
}

/*
        Lifecycle. start takes a complete sink and resets the tables; stop
        closes every window the clients still have, through the sink, and then
        clears everything. Neither can run twice in a row.
*/
static inline int shell_start(struct shell_side *side, const struct shell_sink *sink)
{
        if (side->running)
                return SHELL_EBUSY;
        if (!shell_sink_ok(sink))
                return SHELL_EINVAL;
        shell_clear(side);
        side->sink = sink;
        side->running = 1;
        return SHELL_OK;
}

static inline int shell_stop(struct shell_side *side)
{
        if (!side->running)
                return SHELL_EINVAL;
        for (long i = 0; i < SHELL_SURFACES; i++)
                if (side->surfaces[i].live && side->surfaces[i].row >= 0)
                        side->sink->close(side->sink->context, (int)side->surfaces[i].row);
        shell_clear(side);
        side->sink = 0;
        side->running = 0;
        side->xwayland = 0;
        return SHELL_OK;
}

/*
        A client connects and gets the lowest free number, or a refusal when all
        of them are taken. A disconnect closes what the client had: each window
        through the sink, then every surface and buffer of the client.
*/
static inline int shell_client_connect(struct shell_side *side)
{
        if (!side->running)
                return SHELL_EINVAL;
        for (long i = 0; i < SHELL_CLIENTS; i++)
                if (!side->client_live[i])
                {
                        side->client_live[i] = 1;
                        return (int)i;
                }
        return SHELL_ENOSPC;
}

static inline int shell_client_disconnect(struct shell_side *side, long client)
{
        if (!side->running || !shell_client_live(side, client))
                return SHELL_EINVAL;
        for (long i = 0; i < SHELL_SURFACES; i++)
        {
                struct shell_surface *record = &side->surfaces[i];

                if (!record->live || record->client != client)
                        continue;
                if (record->row >= 0)
                        side->sink->close(side->sink->context, (int)record->row);
                shell_surface_clear(record);
        }
        for (long i = 0; i < SHELL_BUFFERS; i++)
                if (side->buffers[i].live && side->buffers[i].client == client)
                        side->buffers[i].live = 0;
        side->client_live[client] = 0;
        return SHELL_OK;
}

/*
        A role event claims the surface slot the parser named. A toplevel or a
        layer surface asks the sink for a row first; if the sink refuses, the
        slot stays free. A slot that another client holds, or that this client
        has already given a role, is refused.
*/
static inline int shell_role(struct shell_side *side, long client, long surface, long role)
{
        struct shell_surface *record;
        int row = -1;

        if (!side->running || !shell_client_live(side, client))
                return SHELL_EINVAL;
        if (!shell_index_ok(surface, SHELL_SURFACES) || !shell_role_ok(role))
                return SHELL_EINVAL;
        record = &side->surfaces[surface];
        if (record->live)
                return record->client == client ? SHELL_EBUSY : SHELL_EINVAL;
        if (role == SHELL_ROLE_TOPLEVEL || role == SHELL_ROLE_LAYER)
        {
                int opened = side->sink->open(side->sink->context, client, role);

                if (opened < 0)
                        return opened;
                row = opened;
        }
        record->live = 1;
        record->client = client;
        record->role = role;
        record->state = 0;
        record->row = row;
        record->title_length = 0;
        record->title[0] = 0;
        record->app_id_length = 0;
        record->app_id[0] = 0;
        return SHELL_OK;
}

/*
        A buffer offer records a shared-memory or dma-buf buffer's geometry and
        its descriptor, and gives it the lowest free slot. The descriptor is
        recorded and never used here: nothing is imported, mapped or closed.
*/
static inline int shell_buffer_offer(struct shell_side *side, long client, const struct shell_buffer *buffer)
{
        struct shell_buffer_record *record;

        if (!side->running || !shell_client_live(side, client))
                return SHELL_EINVAL;
        if (!buffer || !shell_buffer_ok(buffer))
                return SHELL_EINVAL;
        for (long i = 0; i < SHELL_BUFFERS; i++)
        {
                if (side->buffers[i].live)
                        continue;
                record = &side->buffers[i];
                record->live = 1;
                record->client = client;
                record->kind = buffer->kind;
                record->format = buffer->format;
                record->fd = buffer->fd;
                record->width = buffer->width;
                record->height = buffer->height;
                record->stride = buffer->stride;
                record->bytes = buffer->stride * buffer->height;
                return (int)i;
        }
        return SHELL_ENOSPC;
}

/*
        Drawing: a rectangle of a buffer that a toplevel or a layer surface
        shows. The rectangle is checked against the buffer, then the sink is
        told, and the engine keeps no damage of its own.
*/
static inline int shell_damage(struct shell_side *side, long client, long surface, long buffer, const struct shell_rect *rect)
{
        struct shell_surface *record = shell_surface_of(side, client, surface);
        struct shell_buffer_record *pixels = shell_buffer_of(side, client, buffer);

        if (!side->running || !record || !pixels || !rect)
                return SHELL_EINVAL;
        if (record->role != SHELL_ROLE_TOPLEVEL && record->role != SHELL_ROLE_LAYER)
                return SHELL_EINVAL;
        if (record->row < 0 || !shell_rect_ok(rect, pixels->width, pixels->height))
                return SHELL_EINVAL;
        return side->sink->damage(side->sink->context, (int)record->row, rect);
}

/*
        Input from a client, forwarded with the client's number. The engine
        checks the numbers only: a key is a code in range and a state the three
        reports can take, a pointer is inside the coordinate cap with a button
        mask in range. Whether the client may inject at all is the sink's.
*/
static inline int shell_input_key(struct shell_side *side, long client, long code, long pressed)
{
        if (!side->running || !shell_client_live(side, client))
                return SHELL_EINVAL;
        if (code < 1 || code > SHELL_KEY_MAX)
                return SHELL_EINVAL;
        if (pressed < SHELL_KEY_RELEASE || pressed > SHELL_KEY_REPEAT)
                return SHELL_EINVAL;
        return side->sink->key(side->sink->context, client, code, pressed);
}

static inline int shell_input_pointer(struct shell_side *side, long client, long x, long y, long buttons)
{
        if (!side->running || !shell_client_live(side, client))
                return SHELL_EINVAL;
        if (!shell_coord_ok(x) || !shell_coord_ok(y))
                return SHELL_EINVAL;
        if (buttons < 0 || (buttons & ~SHELL_BUTTONS_MASK))
                return SHELL_EINVAL;
        return side->sink->pointer(side->sink->context, client, x, y, buttons);
}

/*
        Window requests. Each one is a window of this client, not closing where
        it changes anything, with its arguments checked, then the sink is asked;
        the state bit the request means is written only when the sink accepts.
*/
static inline int shell_window_place(struct shell_side *side, long client, long surface, long x, long y, long screen)
{
        struct shell_surface *window = shell_window(side, client, surface);

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (!shell_coord_ok(x) || !shell_coord_ok(y) || !shell_index_ok(screen, SHELL_SCREENS))
                return SHELL_EINVAL;
        return side->sink->place(side->sink->context, (int)window->row, x, y, screen);
}

static inline int shell_window_size(struct shell_side *side, long client, long surface, long width, long height)
{
        struct shell_surface *window = shell_window(side, client, surface);

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (!shell_size_ok(width) || !shell_size_ok(height))
                return SHELL_EINVAL;
        return side->sink->size(side->sink->context, (int)window->row, width, height);
}

static inline int shell_window_minimize(struct shell_side *side, long client, long surface, long on)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (!shell_on_ok(on))
                return SHELL_EINVAL;
        status = side->sink->minimize(side->sink->context, (int)window->row, on);
        if (status >= 0)
                shell_bit(&window->state, SHELL_STATE_MINIMIZED, on);
        return status;
}

static inline int shell_window_maximize(struct shell_side *side, long client, long surface, long on)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (!shell_on_ok(on))
                return SHELL_EINVAL;
        status = side->sink->maximize(side->sink->context, (int)window->row, on);
        if (status >= 0)
                shell_bit(&window->state, SHELL_STATE_MAXIMIZED, on);
        return status;
}

static inline int shell_window_fullscreen(struct shell_side *side, long client, long surface, long on)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (!shell_on_ok(on))
                return SHELL_EINVAL;
        status = side->sink->fullscreen(side->sink->context, (int)window->row, on);
        if (status >= 0)
                shell_bit(&window->state, SHELL_STATE_FULLSCREEN, on);
        return status;
}

/*
        Stacking: -1 keeps a window below, 0 puts it where it is, and 1 keeps it
        above. The two bits are exclusive, so the state is written from the
        layer, not toggled.
*/
static inline int shell_window_stack(struct shell_side *side, long client, long surface, long layer)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (layer < -1 || layer > 1)
                return SHELL_EINVAL;
        status = side->sink->stack(side->sink->context, (int)window->row, layer);
        if (status >= 0)
        {
                shell_bit(&window->state, SHELL_STATE_ABOVE, layer == 1);
                shell_bit(&window->state, SHELL_STATE_BELOW, layer == -1);
        }
        return status;
}

/*
        Activation makes this window the one with the keys. Only one window is
        ACTIVE at a time, so the bit is cleared on every other toplevel once the
        sink has accepted it.
*/
static inline int shell_window_activate(struct shell_side *side, long client, long surface)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        status = side->sink->activate(side->sink->context, (int)window->row);
        if (status >= 0)
        {
                for (long i = 0; i < SHELL_SURFACES; i++)
                        shell_bit(&side->surfaces[i].state, SHELL_STATE_ACTIVE, 0);
                shell_bit(&window->state, SHELL_STATE_ACTIVE, 1);
        }
        return status;
}

/*
        A close request sets CLOSING. A second one is refused rather than sent
        again, and nothing else is accepted from a closing window.
*/
static inline int shell_window_close(struct shell_side *side, long client, long surface)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        status = side->sink->close(side->sink->context, (int)window->row);
        if (status >= 0)
                window->state |= SHELL_STATE_CLOSING;
        return status;
}

static inline int shell_window_title(struct shell_side *side, long client, long surface, const char *text, long length)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window)
                return SHELL_EINVAL;
        if (window->state & SHELL_STATE_CLOSING)
                return SHELL_EBUSY;
        if (!shell_text_ok(text, length, SHELL_TITLE_MAX))
                return SHELL_EINVAL;
        status = side->sink->title(side->sink->context, (int)window->row, text, length);
        if (status >= 0)
        {
                for (long i = 0; i < length; i++)
                        window->title[i] = text[i];
                window->title[length] = 0;
                window->title_length = length;
        }
        return status;
}

/*
        The app id is kept here and read through the query: Canvas has no field
        for it, and the taskbar gets it from the hook's record.
*/
static inline int shell_window_app_id(struct shell_side *side, long client, long surface, const char *text, long length)
{
        struct shell_surface *window = shell_window(side, client, surface);

        if (!window)
                return SHELL_EINVAL;
        if (!shell_text_ok(text, length, SHELL_APP_ID_MAX))
                return SHELL_EINVAL;
        for (long i = 0; i < length; i++)
                window->app_id[i] = text[i];
        window->app_id[length] = 0;
        window->app_id_length = length;
        return SHELL_OK;
}

/*
        The read side. The sink fills the geometry (x, y, width, height and z);
        the engine adds the state bits, the title and the app id from its own
        record. A query changes no state.
*/
static inline int shell_window_query(struct shell_side *side, long client, long surface, struct shell_facts *facts)
{
        struct shell_surface *window = shell_window(side, client, surface);
        int status;

        if (!window || !facts)
                return SHELL_EINVAL;
        status = side->sink->query(side->sink->context, (int)window->row, facts);
        if (status < 0)
                return status;
        facts->state = window->state;
        facts->title_length = window->title_length;
        for (long i = 0; i <= window->title_length; i++)
                facts->title[i] = window->title[i];
        facts->app_id_length = window->app_id_length;
        for (long i = 0; i <= window->app_id_length; i++)
                facts->app_id[i] = window->app_id[i];
        return SHELL_OK;
}

/*
        A layer surface's configuration, forwarded to the sink, which turns an
        exclusive zone into a band of the taskbar's edge. A strut is the same
        thing for a dock window in the X protocol: an edge and a size.
*/
static inline int shell_layer_surface(struct shell_side *side, long client, long surface, const struct shell_layer *layer)
{
        struct shell_surface *record = shell_surface_of(side, client, surface);

        if (!side->running || !record || record->role != SHELL_ROLE_LAYER || record->row < 0)
                return SHELL_EINVAL;
        if (!layer || !shell_layer_ok(layer))
                return SHELL_EINVAL;
        return side->sink->layer(side->sink->context, (int)record->row, layer);
}

#ifdef CONFIG_MOONWATER_X11
static inline int shell_layer_strut(struct shell_side *side, long client, long surface, long edge, long size)
{
        struct shell_layer layer = {0};

        if (edge < SHELL_EDGE_TOP || edge >= SHELL_EDGE_COUNT)
                return SHELL_EINVAL;
        if (!shell_span_ok(size))
                return SHELL_EINVAL;
        layer.layer = SHELL_LAYER_TOP;
        layer.anchor = 1L << edge;
        layer.keyboard = SHELL_KEYBOARD_NONE;
        layer.exclusive = size;
        return shell_layer_surface(side, client, surface, &layer);
}
#endif /* CONFIG_MOONWATER_X11 */

#ifdef CONFIG_MOONWATER_X11
/*
        XWayland is an X server that is also a Wayland client. Its own process
        is started and stopped on the X side, and its clients come in through
        the connect and role hooks like any other client. What this records is
        that it is up, so that a second start is refused. The process is not
        started here.
*/
static inline int shell_xwayland_start(struct shell_side *side)
{
        if (!side->running)
                return SHELL_EINVAL;
        if (side->xwayland)
                return SHELL_EBUSY;
        side->xwayland = 1;
        return SHELL_OK;
}

static inline int shell_xwayland_stop(struct shell_side *side)
{
        if (!side->xwayland)
                return SHELL_EINVAL;
        side->xwayland = 0;
        return SHELL_OK;
}
#endif /* CONFIG_MOONWATER_X11 */

/*
        THE WAYLAND HOOKS
*/
#ifdef CONFIG_MOONWATER_WAYLAND

/*
        The one state the Wayland side has. Every Wayland hook is a call into the
        engine above with it. The X11 side has its own, below, and nothing is shared.
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

#endif /* CONFIG_MOONWATER_WAYLAND */

/*
        THE X11 AND XWAYLAND HOOKS
*/
#ifdef CONFIG_MOONWATER_X11

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

/*
        The one state the X11 side has, like the Wayland side's. XWayland's record
        lives in it too, as a flag the engine keeps.
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

#endif /* CONFIG_MOONWATER_X11 */
