/*
        Moonwater Canvas, the Wayland shim: the one file that is the whole of what a Wayland client can say to
        the compositor, and what the compositor says back.

        WHAT IT IS

        A Wayland client is a Canvas program whose bytes are protocol. The compositor listens on a socket,
        reads what a client sends, and this file turns it into the same things a program does to its row of
        the window table (struct windows, window.c): a picture, a size, a title, a request to be put away or to
        fill the screen; and turns what the compositor does to a row -- the keys, the pointer, a close, a size it
        asks for -- into events on the client's socket. The compositor never parses a byte a client sent, and
        this file never touches the display, an input device or a window directly: it asks the compositor
        through `struct wayland_host` (below), which is also the seam the checks put a fake compositor behind.

        It runs in the compositor's process, in ring 3, and is compiled into it when CONFIG_MOONWATER_WAYLAND is
        on (off by default). It is not kernel code: nothing here may be built into the module, and the file
        refuses to build without the switch so a Makefile cannot name it by mistake. It is freestanding C: no
        library, no allocator (a client's record is one fixed block the host maps when the client connects and
        gives back when it goes), only long, int and char.

        THE PROTOCOL TABLE

        wayland_protocol.inc is generated from the protocol XML (test/wayland_protocol.py). Every request's
        arguments are read by that table and not by hand, every object is checked against the interface the
        table names, and every event this file sends is checked in the tests against the table's event
        signatures, so an opcode or an argument cannot drift. What the table lists is what a client can bind;
        what is not listed is not offered, and a client that binds nothing it was not offered is never refused.

        THREAT MODEL

        Every byte and number a client sends is hostile: a length, an id, an object, a size, an offset, a
        string, a descriptor, and the order of calls. A client may be any program the user runs, including one
        in a bowl, and may die, stall or lie at any moment. Therefore:
        1. Fixed tables only. A client's numbers are compared with a cap and are never the size of anything.
        2. A request is checked whole before it changes anything; the first protocol error is fatal to that
           client (it is sent wl_display.error, flushed and dropped), as in every Wayland server, and the drop
           gives back everything the client held, in one place (wayland_drop): rows, descriptors, buffers.
        3. A descriptor is owned from the moment the shim takes it. The one that is not used is closed.
        4. A client that does not read its socket never blocks the compositor: its output queue is bounded, and
           one that fills it is dropped.
        5. A client never reads or writes another client's objects: an id is looked up in its own table only.
        6. Strings are copied into fixed arrays after they are checked; nothing a client sent is ever printed
           or shown unchecked.
*/
#ifndef WAYLAND_INCLUDED
#define WAYLAND_INCLUDED

#ifndef CONFIG_MOONWATER_WAYLAND
#error "wayland.c is built only with CONFIG_MOONWATER_WAYLAND"
#endif

#include "wayland_protocol.inc"

/* What the compositor calls: used in the compositor, and in the checks as much of it as they reach. */
#define WAYLAND_API static __attribute__((unused))

/*
        The caps. A client's record is one block of these, so a record is
        (about) WAYLAND_BLOCK bytes whatever the client does.
*/
#define WAYLAND_CLIENTS 32
#define WAYLAND_IDS 8192        // a client's ids run from 1 to this less one
#define WAYLAND_OBJECTS 2048    // live objects of one client
#define WAYLAND_SURFACES 64
#define WAYLAND_POOLS 96         // a dma-buf buffer is a pool of its own, so this is more than shm needs
#define WAYLAND_BUFFERS 256
#define WAYLAND_POSITIONERS 8
#define WAYLAND_MESSAGE 4096    // the largest message, header included, as in libwayland
#define WAYLAND_ARGS 8
#define WAYLAND_FDS 28          // descriptors a client may have sent and not yet had used
#define WAYLAND_OUT 65536       // a client's output queue
#define WAYLAND_OUT_FDS 8
#define WAYLAND_EVENT 1024      // the most one event can take
#define WAYLAND_DAMAGE 8
#define WAYLAND_CALLBACKS 32
#define WAYLAND_PRESENTATIONS 4 // presentation feedbacks a surface holds for its next commits (one more is discarded)
#define WAYLAND_SERIALS 8       // configures sent and not yet acknowledged
#define WAYLAND_TEXT 128        // a title or an app id, its NUL included
#define WAYLAND_POOL_BYTES (1L << 30)
#define WAYLAND_POOL_TOTAL (1L << 31) // what one client may have mapped
#define WAYLAND_DIMENSION 16384
#define WAYLAND_ROWS 32         // the compositor's table
#define WAYLAND_SERVER_IDS 64   // objects the server makes (ids from 0xff000000)
#define WAYLAND_SERVER_BASE 0xff000000UL
#define WAYLAND_SCREENS 8
#define WAYLAND_GLOBALS_MAX 32
#define WAYLAND_SEATS 8         // pointers, keyboards and touches, one client holds
#define WAYLAND_TOUCHES 10      // fingers down at once over every client (wl_touch's points)
#define WAYLAND_TOKENS 16       // xdg-activation tokens pending or given out, over every client
#define WAYLAND_TOKEN_MS 10000  // how long a given token may be used (its serial must still be a recent input of its client)
#define WAYLAND_KEYMAP_MAX (1L << 20)
#define WAYLAND_SOURCES 8       // data sources (and primary selection sources) one client holds
#define WAYLAND_OFFERS 16       // offers made to one client and not yet destroyed by it
#define WAYLAND_DEVICES 8       // data devices, of each kind, one client holds
#define WAYLAND_INPUT_SERIALS 8 // serials of input events a client may name to set a selection
#define WAYLAND_CLIP_CLIPBOARD 0
#define WAYLAND_CLIP_PRIMARY 1
#define WAYLAND_DMABUF_ENTRIES 32    // format and modifier pairs the compositor takes
#define WAYLAND_DMABUF_TRANCHES 4
#define WAYLAND_PARAMS 8             // zwp_linux_buffer_params_v1 one client holds
#define WAYLAND_FEEDBACKS 8          // zwp_linux_dmabuf_feedback_v1 one client holds
#define WAYLAND_DMABUFS 48           // buffers made of dma-bufs one client has (each holds a descriptor)
#define WAYLAND_DMABUFS_ALL 192      // and all clients together
#define WAYLAND_CONSTRAINTS 8        // pointer locks and confinements one client holds
#define WAYLAND_NOTIFIES 8           // ext-idle-notify notifications one client holds
#define WAYLAND_RELATIVES 8          // relative pointers one client holds
#define WAYLAND_FOURCC_ARGB8888 0x34325241UL
#define WAYLAND_FOURCC_XRGB8888 0x34325258UL
#define WAYLAND_MODIFIER_INVALID 0x00ffffffffffffffUL

/*
        What a request answers: zero is done, negative is a client that is dead (the error is queued).
*/
#define WAYLAND_OK 0
#define WAYLAND_DEAD (-1)
/*
        What a request answers when it cannot be done yet: a commit of a dma-buf the GPU is still drawing in. The commit and every
        byte the client sent after it wait in the client (WAYLAND_STASH of them) and the client is stalled, which the host sees
        (wayland_stalled) and stops reading its socket for, until wayland_resume finds the buffer ready. Nothing waits in the
        compositor's loop, and the client is held to what its GPU draws: it has no frame callback for a picture that is not shown.
*/
#define WAYLAND_STALL 1
#define WAYLAND_STASH 20480        // one read of the host (16384) and the message it was completing (4096)
#define WAYLAND_STALL_MS 10000     // a client whose buffer is not ready by then is ended: its GPU hung
/*
        A commit is also made to wait when its client has had more pictures copied than its share of a frame: what a picture costs
        the compositor is its pixels, copied at the commit, and a commit is 52 bytes, so a client that sent 64 KB of commits of a
        window the size of a 4K screen held the compositor (every window, the pointer, the keys) for 4.5 s a read. A client may have
        WAYLAND_BUDGET_SCREENS of the largest screen copied in WAYLAND_BUDGET_MS, which a window that draws every frame of a screen at
        twice 60 Hz keeps within; the commit past it waits, with the rest, until the next window of time. A client whose budget is
        untouched always has its commit made, so one picture larger than the budget is never refused.
*/
#define WAYLAND_BUDGET_MS 16
#define WAYLAND_BUDGET_SCREENS 2

struct wayland_rect
{
        long x, y, width, height;
};

/*
        What the shim asks of the compositor, and gives it. The shim calls nothing else outside itself.

        A pool: map says whether a descriptor is a file of at least size bytes and maps it read-only
        (zero for a refusal), remap grows the mapping when the client grew the pool, and unmap gives it back; close
        closes a descriptor. alloc gives a zeroed
        block for a client, release takes it back.

        A window: row_open takes a table row for the client's toplevel or popup (the parent's row for a popup,
        and the popup's place in the parent's picture) and answers it or a negative number, row_close lets it go, row_picture copies a buffer's changed rows
        into it and tells the compositor the picture is new, row_title gives the tab's text, and row_do asks it
        for something (WAYLAND_DO_*). row_facts reads what the compositor says of the window.
*/
#define WAYLAND_KIND_TOPLEVEL 1
#define WAYLAND_KIND_POPUP 2

#define WAYLAND_DO_MINIMIZE 1
#define WAYLAND_DO_MAXIMIZE 2
#define WAYLAND_DO_UNMAXIMIZE 3
#define WAYLAND_DO_FULLSCREEN 4
#define WAYLAND_DO_UNFULLSCREEN 5
#define WAYLAND_DO_MOVE 6
#define WAYLAND_DO_RESIZE 7
#define WAYLAND_DO_CLOSE 8
#define WAYLAND_DO_FOCUS 9      // raise and focus the window: an xdg-activation token that the person's input vouches for
#define WAYLAND_DO_PINCH_WANT 11 // the window takes the pinch gesture (1) rather than Ctrl and a wheel (0): a client asked for a pinch object
#define WAYLAND_DO_CURSOR 10    // the cursor shape the window's client asked for, over the pointer (a WAYLAND_CURSOR_* code, or -1)
#define WAYLAND_CURSOR_ARROW 0  // the shapes the compositor draws, the codes WAYLAND_DO_CURSOR takes
#define WAYLAND_CURSOR_TEXT 1
#define WAYLAND_CURSOR_CROSS 2
#define WAYLAND_CURSOR_MOVE 3
#define WAYLAND_CURSOR_RESIZE_H 4
#define WAYLAND_CURSOR_RESIZE_V 5
#define WAYLAND_CURSOR_RESIZE_NWSE 6
#define WAYLAND_CURSOR_RESIZE_NESW 7
#define WAYLAND_CURSOR_HAND 8
#define WAYLAND_CURSOR_WAIT 9
#define WAYLAND_CURSOR_NOTALLOWED 10
#define WAYLAND_CURSOR_HIDDEN 11 // no pointer at all over the window (wl_pointer.set_cursor with no surface)

#define WAYLAND_STATE_MAXIMIZED 1
#define WAYLAND_STATE_FULLSCREEN 2
#define WAYLAND_STATE_ACTIVATED 4
#define WAYLAND_STATE_RESIZING 8
#define WAYLAND_STATE_SUSPENDED 16
#define WAYLAND_STATE_TILED_LEFT 32
#define WAYLAND_STATE_TILED_RIGHT 64
#define WAYLAND_STATE_TILED_TOP 128
#define WAYLAND_STATE_TILED_BOTTOM 256

/*
        What the compositor takes as dma-bufs: the device the clients are to allocate on (a dev_t, as the kernel's stat
        gives it), the format and modifier pairs, and the tranches of them a client is told of in order: each is the pairs
        it names by index, the device they are for and the flags (WLN_zwp_linux_dmabuf_feedback_v1_tranche_flags_*).
*/
struct wayland_dmabuf_info
{
        unsigned long device;
        long count;
        struct
        {
                unsigned int format;
                unsigned long modifier;
        } entry[WAYLAND_DMABUF_ENTRIES];
        long tranches;
        struct
        {
                unsigned long device, flags;
                long count;
                unsigned char index[WAYLAND_DMABUF_ENTRIES];
        } tranche[WAYLAND_DMABUF_TRANCHES];
};

struct wayland_picture
{
        const unsigned char *pixels; // the buffer's first pixel
        long stride;                 // bytes a row
        long width, height;          // the buffer, in pixels
        long format;                 // WLN_wl_shm_format_*
        struct wayland_rect damage[WAYLAND_DAMAGE];
        long damages;                // how many, in buffer pixels; 0 with full set is the whole buffer
        long full;
        // Where it goes in the window's picture (a subsurface's place), the window's size, whether its alpha is laid over what is
        // there (premultiplied) and not copied, and whether the window's picture is complete and is taken now. A picture
        // with no pixels is only that taking.
        long dest_x, dest_y, row_width, row_height, blend, last;
        // A picture whose pixels are a dma-buf's: its descriptor, which the host brackets its reads with (and waits on the
        // buffer's writer through) and may skip, answering 1 where the buffer is not ready; -1 for pixels that are plain memory.
        long sync_fd;
};

struct wayland_facts
{
        long width, height; // what the compositor asks of the window, 0 for the client's own choice
        long states;        // WAYLAND_STATE_*
        long closing;       // the compositor asked the window to go
        long x, y;          // where the window is
        long scale;         // the percent the screen it is mostly on is drawn at (0 for the host that does not say: 100)
};

struct wayland_screen
{
        long x, y, width, height; // in the desktop's pixels
        long scale;               // percent
        long refresh;             // millihertz
        long physical_width, physical_height; // millimetres, 0 for unknown
        char name[16];
};

struct wayland_host
{
        void *context;
        void *(*alloc)(void *context, long bytes);
        void (*release)(void *context, void *block, long bytes);
        void *(*map)(void *context, long fd, long size);
        void *(*remap)(void *context, void *at, long old_size, long new_size);
        void (*unmap)(void *context, void *at, long size);
        void (*close)(void *context, long fd);
        int (*row_open)(void *context, long client, long kind, int parent_row, long x, long y);
        void (*row_close)(void *context, int row);
        int (*row_picture)(void *context, int row, const struct wayland_picture *picture);
        void (*row_title)(void *context, int row, const char *text, long length);
        void (*row_do)(void *context, int row, long what, long a, long b);
        int (*row_facts)(void *context, int row, struct wayland_facts *facts);
        long (*screens)(void *context, struct wayland_screen *screens, long room);
        long (*now)(void *context); // milliseconds on a clock that only goes forward
        // The seat: the keymap as a descriptor of xkb's text and its size (-1 for none), how fast a held key repeats, what the
        // machine has (WLN_wl_seat_capability_*), and which modifiers are locked, as the keymap's own masks.
        long (*keymap)(void *context, long *size);
        void (*repeat)(void *context, long *rate, long *delay);
        long (*seat)(void *context);
        long (*locks)(void *context);
        // The clipboard, which the compositor holds as text (which is WAYLAND_CLIP_*). clip_size is the bytes of the selection, 0
        // for none (and while the service is off). clip_pull is a descriptor for a client's source to write its text to, whose other
        // end the host reads on its own, makes the selection of and says so (wayland_selection_changed, then wayland_clip_pulled), or -1
        // for a copy it will not take; the descriptor is the shim's from then on, closed once it is sent (wayland_sent) or its client
        // is gone, so an abort never closes a number still waiting in the queue. clip_abort ends a pull. clip_push writes the selection to a descriptor a client gave for the
        // window row, and takes the descriptor whatever comes of it. clip_clear empties the selection when the client holds it.
        // dma-bufs. dmabuf_info is what is taken (0 for nothing, which keeps zwp_linux_dmabuf_v1 from being offered), dmabuf_table the
        // file of format and modifier pairs the feedback names (the host's: the shim never closes it, -1 for none), dmabuf_map a
        // descriptor that is a dma-buf of at least size bytes, mapped read-only (0 for a refusal; the descriptor stays the caller's).
        // The pointer held by a client's window: mode 1 is locked (the hand's counts are the client's, unaccelerated, through
        // wayland_pointer_relative, and the pointer is not drawn or moved), 2 is confined to the window, 0 is free. One at a time.
        void (*pointer_hold)(void *context, long row, long mode);
        long (*dmabuf_info)(void *context, struct wayland_dmabuf_info *info);
        long (*dmabuf_table)(void *context, long *size);
        void *(*dmabuf_map)(void *context, long fd, long size);
        long (*clip_size)(void *context, long which);
        long (*clip_pull)(void *context, long client, long which);
        void (*clip_abort)(void *context, long client, long which);
        void (*clip_push)(void *context, long row, long which, long fd);
        void (*clip_clear)(void *context, long client, long which);
        // Whether a dma-buf has no writer now (its GPU is done), asked and never waited for; a host with none says every buffer is ready.
        int (*dmabuf_ready)(void *context, long fd);
        int (*random)(void *context, unsigned char *into, long bytes); // fills bytes with random bytes: 1 when it did
};

/*
        Objects. An object is an id the client chose (or the server, from WAYLAND_SERVER_BASE), the interface it
        is (an index into the generated table) and the version it was made at. link and aux tie it to the typed
        record it is about: a surface's slot and generation, a pool, a buffer.
*/
struct wayland_object
{
        unsigned int id;
        unsigned char iface, version;
        unsigned short flags;
        short link;
        unsigned short aux;
};

#define WAYLAND_ROLE_NONE 0
#define WAYLAND_ROLE_TOPLEVEL 1
#define WAYLAND_ROLE_POPUP 2
#define WAYLAND_ROLE_SUBSURFACE 3
#define WAYLAND_ROLE_CURSOR 4

// A surface's flags.
#define WAYLAND_SURFACE_XDG 1u        // it has an xdg_surface
#define WAYLAND_SURFACE_CONFIGURED 2u // the client acknowledged a configure
#define WAYLAND_SURFACE_SENT 4u       // the initial configure was sent
#define WAYLAND_SURFACE_MAPPED 8u     // a buffer is shown
#define WAYLAND_SURFACE_CLOSED 16u    // the compositor asked the window to go, and said so

struct wayland_surface
{
        unsigned int gen;
        unsigned char live, role;
        unsigned short flags;
        unsigned int id;                 // the wl_surface's id
        // The pending state, which a commit applies.
        unsigned int attached;           // the buffer's id (0 for none) when attach was called
        unsigned char attach_made;
        unsigned char damage_full;
        unsigned char damages;
        unsigned char callbacks;
        struct wayland_rect damage[WAYLAND_DAMAGE];
        unsigned char damage_kind[WAYLAND_DAMAGE]; // 0 surface, 1 buffer coordinates
        unsigned int callback[WAYLAND_CALLBACKS];
        long pending_scale;
        long pending_transform;
        // The callbacks a commit took, waiting for the frame.
        unsigned int ready[WAYLAND_CALLBACKS];
        unsigned char readies;
        // Presentation feedback (wp_presentation): the objects requested since the last commit, and those a commit took, which
        // are answered when the frame that carries the commit is drawn (wayland_presented), not on a frame that was not drawn.
        unsigned int feedback[WAYLAND_PRESENTATIONS];
        unsigned char feedbacks;
        unsigned int presenting[WAYLAND_PRESENTATIONS];
        unsigned char presentings;
        // What is shown. scale is the buffer scale, and scale120 what a unit of the surface is in pixels, in 120ths (wp_fractional_scale's
        // unit): 120 times the buffer scale, or, where a viewport gives the surface a size, the buffer's pixels over that size.
        long scale, scale120, transform;
        long width, height;              // the shown buffer, in pixels
        // The viewport (wp_viewporter): its object, and the pending destination (0 for none) and source in 1/256ths of a pixel
        // (source_width 0 for none). The fractional scale object, and the scale it was last told, in 120ths, and the whole
        // number a client without one was told (preferred_buffer_scale).
        unsigned int viewport_id, fractional_id;
        long dest_width, dest_height, source_x, source_y, source_width, source_height;
        long told120, told_whole, told_unit;
        // The hints a client gives a surface: wp_content_type (set at once, the compositor reads it), wp_alpha_modifier (the
        // pending multiplier, 0xffffffff for opaque, which a commit shows as alpha), and an xdg_dialog's modal state.
        unsigned int content, alpha, pending_alpha;
        unsigned char modal;
        // xdg-toplevel-icon: whether an icon with pixels or a name was set on the toplevel, pending and as the commit showed it.
        unsigned char icon, pending_icon;
        // wp_tearing_control's presentation hint, asynchronous (tearing allowed) or not, pending and as the commit shows it.
        unsigned char pending_async, async_hint;
        unsigned char attention;     // an xdg-activation asked for the window that was not vouched for: it wants attention
        // wp_fifo (a commit waits for the barrier: the next frame latched) and wp_commit_timing (the time the commit is for, in
        // nanoseconds on the monotonic clock, 0 for none): pending, and as the commit took them.
        unsigned char pending_fifo, fifo_wait;
        // wp_fifo's set_barrier: pending, and as the commit set it, with the frame count it was set on (it clears at the next frame drawn).
        unsigned char pending_barrier, barrier;
        long barrier_frame;
        unsigned long pending_timestamp, timestamp;
        // xdg.
        unsigned int xdg_id, role_id;
        unsigned int serial[WAYLAND_SERIALS];
        unsigned char serials;
        unsigned int last_serial;
        long geometry_x, geometry_y, geometry_width, geometry_height;
        unsigned char geometry_set;
        long window_x, window_y, window_w, window_h; // the part of the picture last shown as the window, in pixels
        // A toplevel.
        char title[WAYLAND_TEXT];
        char app_id[WAYLAND_TEXT];
        long min_width, min_height, max_width, max_height;
        unsigned int parent;             // the parent toplevel's surface slot + 1
        // A subsurface: the surface it hangs from (its slot + 1), its object, its place and the place asked for, its order among its
        // siblings, whether it is synchronized, has a commit waiting for its parent's, and the buffer it holds as its picture.
        unsigned short sub_parent, held;
        unsigned int sub_id, cached;
        long sub_x, sub_y, pend_x, pend_y, sub_order;
        unsigned char tearing, shortcuts; // a tearing control and a shortcuts inhibitor it has
        // An X11 window's surface (xwayland_shell_v1): whether an xwayland_surface_v1 has it, and the serial its set_serial gave.
        // A zwp_xwayland_keyboard_grab_v1 holds the keys as a shortcuts inhibitor does (xgrab).
        unsigned char xsurface, xserial_set, xgrab;
        unsigned long long xserial;
        // The shortcuts inhibitor: its object, whether it is active (the surface has the keyboard and the inhibitor is not cancelled by
        // the escape chord), and the escape chord's Escape: held down, and since when (the host's ms).
        unsigned int shortcut_id;
        unsigned char shortcut_active, escape_down;
        long escape_at;
        unsigned char sub_sync, sub_dirty, pend_pos, need_full, input_none, shown_full, translucent;
        long translucent_x, translucent_y, translucent_w, translucent_h; // where a translucent picture was laid
        struct wayland_rect shown_rect;
        // What the window was last told.
        long told_width, told_height, told_states;
        unsigned char told;
        unsigned int decoration_id;      // its zxdg_toplevel_decoration_v1, or 0
        unsigned char want_maximized, want_fullscreen, grab;
        unsigned char entered;           // the screens it was told it is on, a bit each
        int row;
        // A popup: where its positioner put it, relative to its parent's picture.
        long popup_x, popup_y, popup_width, popup_height;
};

struct wayland_pool
{
        long live;
        long fd;     // a dma-buf's descriptor while the pool is its mapping, which the buffer holds
        long dmabuf; // 1 for such a pool
        long size;
        long refs;
        long gone;
        void *at;
};

struct wayland_buffer
{
        long live;
        long pool;
        long offset, width, height, stride, format;
        unsigned int id;
        long held, zombie; // surfaces that keep it as their picture; its object destroyed while they did
};

struct wayland_positioner
{
        long live;
        unsigned int id;
        long width, height;
        long anchor_x, anchor_y, anchor_width, anchor_height;
        long anchor, gravity, adjustment;
        long offset_x, offset_y;
        long reactive;
};

/*
        A data source, or a primary selection source, a client made: the types it offers (text only is taken: bit 0 the UTF-8
        type, 1 UTF8_STRING, 2 plain text), which selection it is for, and where it is: 0 made, 1 asked for and being read by the
        host, 2 the selection, 3 let go (cancelled, or used up).
*/
struct wayland_source
{
        unsigned int id;
        unsigned char live, which, mimes, state;
};

// An offer made to a client of the selection as it was when it was made.
struct wayland_offer
{
        unsigned int id;
        unsigned char live, which;
        unsigned long serial;
};

// A pointer lock or confinement: its kind (1 lock, 2 confine), whether it holds the pointer now, whether it ends with its first
// deactivation, and the surface it is of (an index and one).
struct wayland_constraint
{
        unsigned int id;
        unsigned char live, kind, active, oneshot;
        unsigned short surface;
};

// An ext_idle_notification_v1: its object, its timeout in ms, whether it counts input alone (get_input_idle_notification), whether it
// is idle and since when (the shim's ms: the last time it was idle, and the time it counts from otherwise: its creation or its resume).
struct wayland_notify
{
        unsigned int id;
        unsigned char live, input_only, idle;
        long timeout, since, idled;
};

// A zwp_linux_buffer_params_v1: the planes it was given, up to four, and whether it made its buffer.
struct wayland_params
{
        unsigned int id;
        unsigned char live, used;
        long fd[4];
        unsigned long offset[4], stride[4], modifier[4];
};

struct wayland_client
{
        long live, dead, slot, pid, uid;
        long in_length;
        unsigned char in[WAYLAND_MESSAGE];
        // A commit that waits for its GPU, and what the client sent from it on: the bytes, the descriptor to ask of, since when.
        unsigned char stash[WAYLAND_STASH];
        long stash_length, stalled, stalled_fd, stalled_at; // stalled_fd -1: a commit that waits for its budget, not for a GPU
        long budget_at, budget_used; // the window of time the client's copies are counted in (its start, ms), and its pixels so far
        long out_length;
        unsigned char out[WAYLAND_OUT];
        long out_fds[WAYLAND_OUT_FDS], out_fd_at[WAYLAND_OUT_FDS], out_fd_count;
        unsigned char out_fd_owned[WAYLAND_OUT_FDS]; // the shim's own (a copy's pipe), closed once sent; the others are the host's
        long in_fds[WAYLAND_FDS], in_fd_count;
        unsigned long serial;
        long pool_bytes;
        long free_hint;
        unsigned int pointer_id[WAYLAND_SEATS], keyboard_id[WAYLAND_SEATS], touch_id[WAYLAND_SEATS];
        unsigned char pointers, keyboards, touches;
        unsigned char finger_scroll; // a precise (finger) scroll was sent to this client and no axis_stop since
        unsigned int press_serial;  // the last button press this client was sent, which a move or a resize must name
        unsigned int device_id[2][WAYLAND_DEVICES]; // data devices (0) and primary selection devices (1)
        unsigned char devices[2];
        unsigned int input_serial[WAYLAND_INPUT_SERIALS]; // the serials of the last input this client was sent
        unsigned char input_next, server_hint;
        unsigned char pending[2];   // the source being read for each selection, as its index and one, or 0
        struct wayland_source sources[WAYLAND_SOURCES];
        struct wayland_offer offers[WAYLAND_OFFERS];
        struct wayland_params params[WAYLAND_PARAMS];
        unsigned int feedback_id[WAYLAND_FEEDBACKS];
        unsigned char feedbacks, dmabufs;
        struct wayland_constraint constraints[WAYLAND_CONSTRAINTS];
        struct wayland_notify notifies[WAYLAND_NOTIFIES];
        long inhibitors;                             // zwp_idle_inhibitor_v1 objects it holds (its objects are scanned only while one is)
        // A commit held for a fifo barrier (hold_at 0: woken by the frames) or for its time (hold_at, the host's ms), and the surface it is for.
        unsigned char hold;
        long hold_surface, hold_at;
        unsigned int relative_id[WAYLAND_RELATIVES];
        unsigned char relatives;
        long axis_left[2];          // the wheel's turns not yet a whole notch, for a client that has only notches
        unsigned short index[WAYLAND_IDS];
        unsigned short server_index[WAYLAND_SERVER_IDS];
        struct wayland_object objects[WAYLAND_OBJECTS];
        struct wayland_surface surfaces[WAYLAND_SURFACES];
        struct wayland_pool pools[WAYLAND_POOLS];
        struct wayland_buffer buffers[WAYLAND_BUFFERS];
        struct wayland_positioner positioners[WAYLAND_POSITIONERS];
};

/*
        A finger down on a window (wl_touch): its id (the tracking id the device gave it, unique while it is down), the row
        and the client and surface it went down on (the surface stays the one for the whole touch, as the protocol has it), and
        the place last sent in the row's picture, so a finger that did not move sends nothing.
*/
struct wayland_touch
{
        unsigned char live;
        int client, row, surface;
        long id, x, y;
};

/*
        An xdg-activation token: pending (made, not committed: the serial a client names and the surface), then given out (text,
        the time it was given, whether it may be used). Its client is the one that made it, and a token is used once.
*/
struct wayland_token
{
        unsigned char live, committed;
        int client;
        unsigned long serial;
        long when;
        char text[33];
};

/*
        The one state this file has: the host, the clients (a pointer each, to the block the host gave), and the
        row each window of a client is in. Nothing else is static.
*/
static struct
{
        const struct wayland_host *host;
        struct wayland_client *client[WAYLAND_CLIENTS];
        long row_client[WAYLAND_ROWS], row_surface[WAYLAND_ROWS];
        long row_percent[WAYLAND_ROWS];  // the percent the window of the row was last told its scale for (0 before the first)
        struct wayland_screen screen[WAYLAND_SCREENS];
        long screens;
        long output_age[WAYLAND_SCREENS]; // how many times the screen of the number has been removed: its global's name is not used twice
        long clients, objects, surfaces, pools, buffers, fds, rows;
        long pointer_row, pointer_surface, keyboard_row, pointer_x, pointer_y;
        unsigned long mods[4];
        unsigned char keys[16];
        // Which client's source is each selection (its slot and one, 0 for none: the text outlives its source), and a count of
        // how many times it changed, so an offer made of an older one is known.
        long selection_owner[2], selection_source[2];
        unsigned long selection_serial[2];
        struct wayland_dmabuf_info dmabuf; // what the compositor takes as dma-bufs, none when count is 0
        long dmabufs;                      // the buffers made of them, all clients
        long constraints, hold_row, hold_mode; // live pointer locks and confinements; the window that holds the pointer and how
        long input_at;                         // the shim's ms of the last input event the compositor gave it (wayland_input_seen): the idle clock
        long bells, icons;                     // xdg_system_bell rings and xdg-toplevel-icon sets with an icon, over every client (counted; nothing is drawn yet)
        long frames;                           // frames drawn (wayland_frame): a fifo barrier clears at the next one after its commit
        struct wayland_touch touch[WAYLAND_TOUCHES]; // the fingers down, over every client
        struct wayland_token token[WAYLAND_TOKENS];  // xdg-activation tokens, pending or given out
        long pinch_x, pinch_y;                       // the focal point the last pinch update was at
} wayland;

/* ---- the wire ---- */

static inline unsigned long wayland_get(const unsigned char *at)
{
        return (unsigned long)at[0] | (unsigned long)at[1] << 8 | (unsigned long)at[2] << 16 | (unsigned long)at[3] << 24;
}

static inline void wayland_put(unsigned char *at, unsigned long value)
{
        at[0] = (unsigned char)value;
        at[1] = (unsigned char)(value >> 8);
        at[2] = (unsigned char)(value >> 16);
        at[3] = (unsigned char)(value >> 24);
}

static inline void wayland_copy(unsigned char *to, const unsigned char *from, long count)
{
        for (long at = 0; at < count; at++)
                to[at] = from[at];
}

/*
        One argument of a request as the table reads it. u is the unsigned value (or an id), i the signed one, text
        and length a string or an array (not a copy: the message's own bytes), slot an object's slot or -1, fd a
        descriptor the shim now owns (or -1), and version the bind's.
*/
struct wayland_arg
{
        unsigned long u;
        long i;
        const unsigned char *text;
        long length;
        int slot;
        long fd;
        long version;
};

/*
        Events. An event is built in the output queue where it will be sent, with room checked once for the most
        it can take; a queue without that room drops the client, and the bytes built meanwhile go nowhere.
*/
struct wayland_event
{
        struct wayland_client *client;
        unsigned char *at;
        long start;
        int ok;
};

static unsigned char wayland_sink[WAYLAND_EVENT + 8];

static struct wayland_event wayland_begin(struct wayland_client *c, unsigned long object, long opcode)
{
        struct wayland_event e;

        e.client = c;
        e.start = c->out_length;
        e.ok = c->out_length + WAYLAND_EVENT <= WAYLAND_OUT && !c->dead;
        e.at = e.ok ? c->out + c->out_length : wayland_sink;
        if (!e.ok && !c->dead)
                c->dead = 2; // a queue that is full: a client that does not read
        wayland_put(e.at, object);
        wayland_put(e.at + 4, (unsigned long)opcode);
        e.at += 8;
        return e;
}

static inline void wayland_word(struct wayland_event *e, unsigned long value)
{
        wayland_put(e->at, value);
        e->at += 4;
}

static void wayland_string(struct wayland_event *e, const char *text)
{
        unsigned long count = 1, padded;

        while (text[count - 1] && count < 256)
                count++;
        padded = (count + 3) & ~3UL;
        wayland_word(e, count);
        for (unsigned long at = 0; at < padded; at++)
                e->at[at] = at + 1 < count ? (unsigned char)text[at] : 0;
        e->at += padded;
}

// The states of a toplevel, or an array of words, as the wire has it: a length, the words, no padding needed.
static void wayland_words(struct wayland_event *e, const unsigned long *words, long count)
{
        wayland_word(e, (unsigned long)count * 4);
        for (long at = 0; at < count; at++)
                wayland_word(e, words[at]);
}

static void wayland_end(struct wayland_event *e)
{
        unsigned char *base = e->ok ? e->client->out + e->start : wayland_sink;
        long size = (long)(e->at - base);

        wayland_put(base + 4, wayland_get(base + 4) | (unsigned long)size << 16);
        if (e->ok)
                e->client->out_length += size;
}

/* A protocol error: wl_display.error on the object, and the client is dead after it. */
static int wayland_fail(struct wayland_client *c, unsigned long object, unsigned long code, const char *text)
{
        if (!c->dead)
        {
                struct wayland_event e = wayland_begin(c, 1, WLE_wl_display_error);

                wayland_word(&e, object);
                wayland_word(&e, code);
                wayland_string(&e, text);
                wayland_end(&e);
                c->dead = 1;
        }
        return WAYLAND_DEAD;
}

/* ---- objects ---- */

static long wayland_slot_of(struct wayland_client *c, unsigned long id)
{
        if (id >= 1 && id < WAYLAND_IDS)
                return (long)c->index[id] - 1;
        if (id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < WAYLAND_SERVER_IDS)
                return (long)c->server_index[id - WAYLAND_SERVER_BASE] - 1;
        return -1;
}

static long wayland_object_new(struct wayland_client *c, unsigned long id, long iface, long version)
{
        long slot = -1;

        if (iface <= WLI_NONE || iface >= WLI_COUNT || wayland_slot_of(c, id) >= 0)
                return -1;
        if (!(id >= 1 && id < WAYLAND_IDS) && !(id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < WAYLAND_SERVER_IDS))
                return -1;
        for (long at = c->free_hint; at < WAYLAND_OBJECTS; at++)
                if (!c->objects[at].iface)
                {
                        slot = at;
                        break;
                }
        if (slot < 0)
                return -1;
        c->free_hint = slot + 1;
        c->objects[slot].id = (unsigned int)id;
        c->objects[slot].iface = (unsigned char)iface;
        c->objects[slot].version = (unsigned char)(version > wl_interfaces[iface].version ? wl_interfaces[iface].version : version);
        c->objects[slot].flags = 0;
        c->objects[slot].link = -1;
        c->objects[slot].aux = 0;
        if (id < WAYLAND_IDS)
                c->index[id] = (unsigned short)(slot + 1);
        else
                c->server_index[id - WAYLAND_SERVER_BASE] = (unsigned short)(slot + 1);
        wayland.objects++;
        return slot;
}

static void wayland_object_free(struct wayland_client *c, long slot)
{
        unsigned long id = c->objects[slot].id;

        if (c->objects[slot].iface == WLI_zwp_idle_inhibitor_v1 && c->inhibitors > 0)
                c->inhibitors--;

        if (id < WAYLAND_IDS)
                c->index[id] = 0;
        else
                c->server_index[id - WAYLAND_SERVER_BASE] = 0;
        c->objects[slot].iface = 0;
        c->objects[slot].id = 0;
        if (slot < c->free_hint)
                c->free_hint = slot;
        wayland.objects--;
}

/* The client's id is let go of: the server says so, and the number may be used again. A server's own id is not. */
static void wayland_object_delete(struct wayland_client *c, long slot)
{
        unsigned long id = c->objects[slot].id;

        wayland_object_free(c, slot);
        if (id < WAYLAND_IDS)
        {
                struct wayland_event e = wayland_begin(c, 1, WLE_wl_display_delete_id);

                wayland_word(&e, id);
                wayland_end(&e);
        }
}

/* The object with the id, if it is one of the interface (any, for 0). */
static long wayland_find(struct wayland_client *c, unsigned long id, long iface)
{
        long slot = wayland_slot_of(c, id);

        return slot >= 0 && (!iface || c->objects[slot].iface == iface) ? slot : -1;
}

static inline unsigned long wayland_serial(struct wayland_client *c)
{
        return ++c->serial;
}

// The serial of an input event sent to a client, kept among the last few: a selection is set by naming one.
static unsigned long wayland_input_serial(struct wayland_client *c)
{
        unsigned long serial = ++c->serial;

        c->input_serial[c->input_next++ % WAYLAND_INPUT_SERIALS] = (unsigned int)serial;
        return serial;
}

static int wayland_input_serial_ok(const struct wayland_client *c, unsigned long serial)
{
        for (long at = 0; serial && at < WAYLAND_INPUT_SERIALS; at++)
                if (c->input_serial[at] == (unsigned int)serial)
                        return 1;
        return 0;
}

/* ---- text, damage and geometry a client gave ---- */

/*
        A title or an app id, copied into into[WAYLAND_TEXT]: cut at the cap on a character's boundary, a byte that is
        not valid UTF-8 or is a control replaced by '?', so that what is shown is text and never anything a terminal
        or a tab would act on. The length is answered.
*/
static long wayland_text(char *into, const unsigned char *text, long length)
{
        long at = 0, used = 0;

        while (at < length && used < WAYLAND_TEXT - 1)
        {
                unsigned char c = text[at];
                long want = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
                int good = want && at + want <= length && used + want < WAYLAND_TEXT;

                for (long more = 1; good && more < want; more++)
                        good = (text[at + more] & 0xc0) == 0x80;
                // Overlong forms, surrogates and what is past U+10FFFF are not text either.
                if (good && want == 2 && c < 0xc2)
                        good = 0;
                if (good && want == 3 && ((c == 0xe0 && text[at + 1] < 0xa0) || (c == 0xed && text[at + 1] >= 0xa0)))
                        good = 0;
                if (good && want == 4 && ((c == 0xf0 && text[at + 1] < 0x90) || c > 0xf4 || (c == 0xf4 && text[at + 1] >= 0x90)))
                        good = 0;
                if (want == 1 && (c < 0x20 || c == 0x7f))
                        good = 0;
                if (!good)
                {
                        into[used++] = '?';
                        at++;
                        continue;
                }
                for (long more = 0; more < want; more++)
                        into[used++] = (char)text[at + more];
                at += want;
        }
        into[used] = 0;
        return used;
}

static inline long wayland_min(long a, long b)
{
        return a < b ? a : b;
}

static inline long wayland_max(long a, long b)
{
        return a > b ? a : b;
}

/*
        Scale. A screen is drawn at a percent (25 to 800); a surface's unit is that many pixels in 100ths. wp_fractional_scale
        says it in 120ths (150 percent is 180), which is the unit a surface's scale is kept in here: a whole buffer scale of
        n is 120n, so what a client that knows only whole scales does is the same arithmetic, exact. There is no scaler: a
        buffer is shown pixel for pixel, so a client that follows the scale it is told (a buffer of the size times the scale,
        and a viewport whose destination is the size) is shown at the size the compositor means, and one that does not is
        shown at the size it drew.
*/
#define WAYLAND_UNIT 120
#define WAYLAND_SCALE_LEAST 30   // 25 percent
#define WAYLAND_SCALE_MOST 960   // 800 percent

// A percent in 120ths, and the whole scale a client that knows no other is told (the percent rounded up, at least one).
static inline long wayland_percent120(long percent)
{
        return (wayland_max(25, wayland_min(percent, 800)) * WAYLAND_UNIT + 50) / 100;
}

static inline long wayland_percent_whole(long percent)
{
        return wayland_max(1, (wayland_min(percent, 800) + 99) / 100);
}

// Units to pixels at a scale in 120ths, rounded to the nearest (a half away from zero), exact at a whole scale.
static inline long wayland_pixels(long units, long scale120)
{
        long v = units * scale120;

        return v >= 0 ? (v + WAYLAND_UNIT / 2) / WAYLAND_UNIT : -((-v + WAYLAND_UNIT / 2) / WAYLAND_UNIT);
}

// Pixels to units, as the 24.8 fixed number an event carries.
static inline unsigned long wayland_units_fixed(long pixels, long scale120)
{
        return (unsigned long)(pixels * 256 * WAYLAND_UNIT / (scale120 > 0 ? scale120 : WAYLAND_UNIT));
}

// Pixels to whole units, rounded down: the size a window of so many pixels is, to the client.
static inline long wayland_units(long pixels, long scale120)
{
        return pixels * WAYLAND_UNIT / (scale120 > 0 ? scale120 : WAYLAND_UNIT);
}

/* ---- the host, and the records a client's block holds ---- */

static struct wayland_surface *wayland_surface_of(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        struct wayland_surface *s;

        if (o->link < 0 || o->link >= WAYLAND_SURFACES)
                return 0;
        s = &c->surfaces[o->link];
        return s->live && s->gen == o->aux ? s : 0;
}

static long wayland_surface_slot(struct wayland_client *c)
{
        for (long at = 0; at < WAYLAND_SURFACES; at++)
                if (!c->surfaces[at].live)
                        return at;
        return -1;
}

// The slot of a surface record, from the record.
static inline long wayland_surface_index(struct wayland_client *c, const struct wayland_surface *s)
{
        return (long)(s - c->surfaces);
}

static void wayland_release_buffer(struct wayland_client *c, long slot)
{
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_buffer_release);

        wayland_end(&e);
}

/* ---- the globals ---- */

static void wayland_dmabuf_formats(struct wayland_client *c, unsigned long id, long version);

/*
        What the registry advertises: name n is element n - 1, each at the version this file implements (not the
        table's: the table is what the XML says, this is what is built). A screen is a wl_output global of its own,
        named WAYLAND_OUTPUT_NAME + its number.
*/
#define WAYLAND_OUTPUT_NAME 64

struct wayland_global
{
        unsigned char iface, version;
};

static const struct wayland_global wayland_globals[] = {
    {WLI_wl_compositor, 6},
    {WLI_wl_shm, 1},
    {WLI_wl_subcompositor, 1},
    {WLI_wl_seat, 9},
    {WLI_xdg_wm_base, 6},
    {WLI_zxdg_output_manager_v1, 3},
    {WLI_zxdg_decoration_manager_v1, 1},
    {WLI_wl_data_device_manager, 3},
    {WLI_zwp_primary_selection_device_manager_v1, 1},
    {WLI_wp_cursor_shape_manager_v1, 2},
    {WLI_zwp_linux_dmabuf_v1, 4},
    {WLI_zwp_relative_pointer_manager_v1, 1},
    {WLI_zwp_pointer_constraints_v1, 1},
    {WLI_zwp_idle_inhibit_manager_v1, 1},
    {WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, 1},
    {WLI_wp_tearing_control_manager_v1, 1},
    {WLI_wp_viewporter, 1},
    {WLI_wp_fractional_scale_manager_v1, 1},
    {WLI_wp_presentation, 2},
    {WLI_xdg_wm_dialog_v1, 1},
    {WLI_xdg_system_bell_v1, 1},
    {WLI_wp_content_type_manager_v1, 1},
    {WLI_wp_alpha_modifier_v1, 1},
    {WLI_xdg_activation_v1, 1},
    {WLI_wp_fifo_manager_v1, 1},
    {WLI_wp_commit_timing_manager_v1, 1},
    {WLI_zwp_pointer_gestures_v1, 3},
    {WLI_ext_idle_notifier_v1, 2},
    {WLI_xdg_toplevel_icon_manager_v1, 1},
    {WLI_xwayland_shell_v1, 1},
    {WLI_zwp_xwayland_keyboard_grab_manager_v1, 1},
};
#define WAYLAND_GLOBAL_COUNT ((long)(sizeof wayland_globals / sizeof wayland_globals[0]))

// Whether a global is offered: all are, but the dma-buf one, which waits for a compositor that takes dma-bufs.
static int wayland_global_offered(long name)
{
        return name < 1 || name > WAYLAND_GLOBAL_COUNT || wayland_globals[name - 1].iface != WLI_zwp_linux_dmabuf_v1 || wayland.dmabuf.count > 0;
}

static int wayland_name_is(const unsigned char *text, long length, const char *name)
{
        long at = 0;

        for (; at < length; at++)
                if (!name[at] || text[at] != (unsigned char)name[at])
                        return 0;
        return name[at] == 0;
}

/*
        A screen's global name: 64 and its number, and one screen count further for each time a screen of that number was taken away
        (a name is never given to two globals, as the protocol has it); the screen a name stands for, or -1 for none that is there.
*/
static long wayland_output_name(long screen)
{
        return WAYLAND_OUTPUT_NAME + screen + WAYLAND_SCREENS * wayland.output_age[screen];
}

static long wayland_output_of(long name)
{
        long screen = name >= WAYLAND_OUTPUT_NAME ? (name - WAYLAND_OUTPUT_NAME) % WAYLAND_SCREENS : -1;

        return screen >= 0 && screen < wayland.screens && wayland_output_name(screen) == name ? screen : -1;
}

static void wayland_global_send(struct wayland_client *c, unsigned long registry, long name)
{
        struct wayland_event e = wayland_begin(c, registry, WLE_wl_registry_global);
        long iface = name >= WAYLAND_OUTPUT_NAME ? WLI_wl_output : wayland_globals[name - 1].iface;
        long version = name >= WAYLAND_OUTPUT_NAME ? 4 : wayland_globals[name - 1].version;

        wayland_word(&e, (unsigned long)name);
        wayland_string(&e, wl_interfaces[iface].name);
        wayland_word(&e, (unsigned long)version);
        wayland_end(&e);
}

/* What a screen says of itself, to one wl_output of a client: geometry, mode, scale, name, done. */
static void wayland_output_send(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        const struct wayland_screen *screen;
        struct wayland_event e;

        if (o->aux >= wayland.screens)
                return;
        screen = &wayland.screen[o->aux];
        e = wayland_begin(c, o->id, WLE_wl_output_geometry);
        wayland_word(&e, (unsigned long)screen->x);
        wayland_word(&e, (unsigned long)screen->y);
        wayland_word(&e, (unsigned long)screen->physical_width);
        wayland_word(&e, (unsigned long)screen->physical_height);
        wayland_word(&e, WLN_wl_output_subpixel_unknown);
        wayland_string(&e, "Moonwater");
        wayland_string(&e, screen->name);
        wayland_word(&e, WLN_wl_output_transform_normal);
        wayland_end(&e);
        e = wayland_begin(c, o->id, WLE_wl_output_mode);
        wayland_word(&e, WLN_wl_output_mode_current | WLN_wl_output_mode_preferred);
        wayland_word(&e, (unsigned long)screen->width);
        wayland_word(&e, (unsigned long)screen->height);
        wayland_word(&e, (unsigned long)screen->refresh);
        wayland_end(&e);
        if (o->version >= 2)
        {
                e = wayland_begin(c, o->id, WLE_wl_output_scale);
                wayland_word(&e, (unsigned long)wayland_percent_whole(screen->scale));
                wayland_end(&e);
        }
        if (o->version >= 4)
        {
                e = wayland_begin(c, o->id, WLE_wl_output_name);
                wayland_string(&e, screen->name);
                wayland_end(&e);
                e = wayland_begin(c, o->id, WLE_wl_output_description);
                wayland_string(&e, screen->name);
                wayland_end(&e);
        }
        if (o->version >= 2)
        {
                e = wayland_begin(c, o->id, WLE_wl_output_done);
                wayland_end(&e);
        }
}

/*
        The xdg_output of an output: its place and size in logical pixels, which are the screen's at its scale (a screen at 150
        percent is 2560 by 1440 of 3840 by 2160), and done, which from version 3 is the wl_output's (the caller sends it after
        the properties of the wl_output, so that the two are one atomic change).
*/
/*
        Where a screen is in the one logical space the clients are told of. The desktop is laid out in pixels, and a screen of another
        scale is another unit in it, so a screen that follows another is placed after what the other measures in units and not in
        pixels: 3840 pixels at 200 percent are 1920 units, and the screen that abuts it on the right starts at 1920 and not at 3840.
        Along an axis a screen is at the end of the screen it follows (the nearest to its start that lies wholly before it and
        shares some of its extent across), a gap between them in its own units; with none before it, at its pixel distance from the
        origin in its own units. The two axes are each worked out so, and a screen that is first on both keeps what it always had.
*/
static long wayland_logical_axis(long screen, int vertical, int depth)
{
        const struct wayland_screen *self = &wayland.screen[screen];
        long start = vertical ? self->y : self->x, across = vertical ? self->x : self->y;
        long across_size = vertical ? self->width : self->height, best = -1, best_end = 0;
        long scale120 = wayland_percent120(self->scale);

        for (long at = 0; at < wayland.screens && depth < WAYLAND_SCREENS; at++)
        {
                const struct wayland_screen *other = &wayland.screen[at];
                long other_start = vertical ? other->y : other->x, other_size = vertical ? other->height : other->width;
                long other_across = vertical ? other->x : other->y, other_across_size = vertical ? other->width : other->height;
                long stop = other_start + other_size;

                if (at == screen || stop > start || other_across >= across + across_size || across >= other_across + other_across_size)
                        continue;
                if (best < 0 || stop > best_end)
                        best = at, best_end = stop;
        }
        if (best < 0)
                return wayland_units(start, scale120);
        {
                const struct wayland_screen *before = &wayland.screen[best];

                return wayland_logical_axis(best, vertical, depth + 1) +
                       wayland_units(vertical ? before->height : before->width, wayland_percent120(before->scale)) +
                       wayland_units(start - best_end, scale120);
        }
}

static void wayland_xdg_output_send(struct wayland_client *c, long slot, long output)
{
        const struct wayland_screen *screen;
        struct wayland_event e;
        long scale120;

        if (output >= wayland.screens)
                return;
        screen = &wayland.screen[output];
        scale120 = wayland_percent120(screen->scale);
        e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_logical_position);
        wayland_word(&e, (unsigned long)wayland_logical_axis(output, 0, 0));
        wayland_word(&e, (unsigned long)wayland_logical_axis(output, 1, 0));
        wayland_end(&e);
        e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_logical_size);
        wayland_word(&e, (unsigned long)wayland_units(screen->width, scale120));
        wayland_word(&e, (unsigned long)wayland_units(screen->height, scale120));
        wayland_end(&e);
        if (c->objects[slot].version >= 2)
        {
                e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_name);
                wayland_string(&e, screen->name);
                wayland_end(&e);
                e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_description);
                wayland_string(&e, screen->name);
                wayland_end(&e);
        }
        if (c->objects[slot].version < 3)
        {
                e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_done);
                wayland_end(&e);
        }
}

/*
        The scale a surface's sizes are in, in 120ths: the one it was told when it follows the fractional scale, else the
        whole scale it was told, which is what a client that knows no other draws at. A surface told nothing yet is in
        what it shows.
*/
static long wayland_surface_unit(const struct wayland_surface *s)
{
        if (s->told120 <= 0)
                return s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT;
        return s->fractional_id ? s->told120 : WAYLAND_UNIT * s->told_whole;
}

/* ---- the windows ---- */

/*
        The decoration's mode. A window of this compositor has its line and its corners from the compositor and no
        titlebar: its top is the client's own, and a client moves it by asking (xdg_toplevel.move). So whatever mode a
        client asks, it is told the client's: a toolkit that can draws its own header, and one that cannot has none.
*/
static void wayland_decoration_send(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_event e;

        if (!s->decoration_id)
                return;
        e = wayland_begin(c, s->decoration_id, WLE_zxdg_toplevel_decoration_v1_configure);
        wayland_word(&e, WLN_zxdg_toplevel_decoration_v1_mode_client_side);
        wayland_end(&e);
}

static long wayland_screen_at(long x, long y);

/*
        A surface's window, in the order a client expects it: the toplevel's size and states, the decoration's mode,
        then the xdg_surface's serial. The serial is the one the client must acknowledge; a client that has
        WAYLAND_SERIALS unacknowledged is sent no more until it does, so one that never answers cannot be sent
        without end.
*/
static void wayland_configure(struct wayland_client *c, struct wayland_surface *s, long initial)
{
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};
        struct wayland_event e;
        unsigned long states[16];
        long count = 0, slot, bounds_w = 0, bounds_h = 0;
        unsigned long serial;

        if (s->serials >= WAYLAND_SERIALS || s->role == WAYLAND_ROLE_NONE)
                return;
        if (s->row >= 0)
                wayland.host->row_facts(wayland.host->context, s->row, &facts);
        if (s->role == WAYLAND_ROLE_TOPLEVEL)
        {
                long all = facts.states;

                if (all & WAYLAND_STATE_MAXIMIZED)
                        states[count++] = WLN_xdg_toplevel_state_maximized;
                if (all & WAYLAND_STATE_FULLSCREEN)
                        states[count++] = WLN_xdg_toplevel_state_fullscreen;
                if (all & WAYLAND_STATE_RESIZING)
                        states[count++] = WLN_xdg_toplevel_state_resizing;
                if (all & WAYLAND_STATE_ACTIVATED)
                        states[count++] = WLN_xdg_toplevel_state_activated;
                if (all & WAYLAND_STATE_TILED_LEFT)
                        states[count++] = WLN_xdg_toplevel_state_tiled_left;
                if (all & WAYLAND_STATE_TILED_RIGHT)
                        states[count++] = WLN_xdg_toplevel_state_tiled_right;
                if (all & WAYLAND_STATE_TILED_TOP)
                        states[count++] = WLN_xdg_toplevel_state_tiled_top;
                if (all & WAYLAND_STATE_TILED_BOTTOM)
                        states[count++] = WLN_xdg_toplevel_state_tiled_bottom;
                if (all & WAYLAND_STATE_SUSPENDED)
                        states[count++] = WLN_xdg_toplevel_state_suspended;
                slot = wayland_find(c, s->role_id, WLI_xdg_toplevel);
                if (slot < 0)
                        return;
                if (initial && wayland.screens)
                {
                        const struct wayland_screen *own = &wayland.screen[wayland_screen_at(facts.x, facts.y)];

                        bounds_w = wayland_units(own->width, wayland_surface_unit(s));
                        bounds_h = wayland_units(own->height, wayland_surface_unit(s));
                }
                if (initial && c->objects[slot].version >= 5)
                {
                        unsigned long caps[4] = {WLN_xdg_toplevel_wm_capabilities_window_menu,
                                                 WLN_xdg_toplevel_wm_capabilities_maximize,
                                                 WLN_xdg_toplevel_wm_capabilities_fullscreen, 4};
                        e = wayland_begin(c, c->objects[slot].id, WLE_xdg_toplevel_wm_capabilities);
                        wayland_words(&e, caps, 4);
                        wayland_end(&e);
                }
                if (initial && c->objects[slot].version >= 4 && bounds_w > 0)
                {
                        e = wayland_begin(c, c->objects[slot].id, WLE_xdg_toplevel_configure_bounds);
                        wayland_word(&e, (unsigned long)bounds_w);
                        wayland_word(&e, (unsigned long)bounds_h);
                        wayland_end(&e);
                }
                wayland_decoration_send(c, s);
                e = wayland_begin(c, c->objects[slot].id, WLE_xdg_toplevel_configure);
                wayland_word(&e, (unsigned long)(facts.width > 0 ? wayland_max(1, wayland_units(facts.width, wayland_surface_unit(s))) : 0));
                wayland_word(&e, (unsigned long)(facts.height > 0 ? wayland_max(1, wayland_units(facts.height, wayland_surface_unit(s))) : 0));
                wayland_words(&e, states, count);
                wayland_end(&e);
                s->told_width = facts.width;
                s->told_height = facts.height;
                s->told_states = facts.states;
                s->told_unit = wayland_surface_unit(s);
                s->told = 1;
        }
        serial = wayland_serial(c);
        slot = wayland_find(c, s->xdg_id, WLI_xdg_surface);
        if (slot < 0)
                return;
        e = wayland_begin(c, c->objects[slot].id, WLE_xdg_surface_configure);
        wayland_word(&e, serial);
        wayland_end(&e);
        s->serial[s->serials++] = (unsigned int)serial;
        s->flags |= WAYLAND_SURFACE_SENT;
}

/* A row given up, whatever its window was. */
static void wayland_constraints_update(void);
static void wayland_relative_send(struct wayland_client *c, long scale120, long dx, long dy, long udx, long udy, long micro);

static void wayland_row_release(struct wayland_client *c, struct wayland_surface *s)
{
        if (s->row < 0)
                return;
        wayland.row_client[s->row] = wayland.row_surface[s->row] = -1;
        wayland.row_percent[s->row] = 0;
        if (wayland.pointer_row == s->row)
                wayland.pointer_row = -1;
        if (wayland.keyboard_row == s->row)
                wayland.keyboard_row = -1;
        wayland.host->row_close(wayland.host->context, s->row);
        wayland.rows--;
        s->row = -1;
        s->flags &= ~(unsigned)WAYLAND_SURFACE_MAPPED;
        wayland_constraints_update();
        (void)c;
}

/* The callbacks of a surface that have waited for a frame: done, and their ids let go. */
static void wayland_callbacks_done(struct wayland_client *c, struct wayland_surface *s, long time)
{
        for (long at = 0; at < s->readies; at++)
        {
                long slot = wayland_find(c, s->ready[at], WLI_wl_callback);

                if (slot < 0)
                        continue;
                {
                        struct wayland_event e = wayland_begin(c, s->ready[at], WLE_wl_callback_done);

                        wayland_word(&e, (unsigned long)time);
                        wayland_end(&e);
                }
                wayland_object_delete(c, slot);
        }
        s->readies = 0;
}

/*
        Presentation feedback: a feedback that will not be answered with a frame is discarded, and its object let go. The
        object's id is looked up in the client's own table, so an id a client has since let go of is none.
*/
static void wayland_feedback_discard(struct wayland_client *c, unsigned int id)
{
        long slot = wayland_find(c, id, WLI_wp_presentation_feedback);

        if (slot < 0)
                return;
        {
                struct wayland_event e = wayland_begin(c, id, WLE_wp_presentation_feedback_discarded);

                wayland_end(&e);
        }
        wayland_object_delete(c, slot);
}

/*
        A commit takes the feedbacks requested since the last one: they are answered by the frame it is drawn in, or, when
        the surface has no window to draw it in (shown 0), discarded now. A surface past its feedbacks discards the one it
        cannot hold, which is the protocol's answer to a feedback that is not presented.
*/
static void wayland_feedback_take(struct wayland_client *c, struct wayland_surface *s, int shown)
{
        for (long at = 0; at < s->feedbacks; at++)
        {
                if (shown && s->presentings < WAYLAND_PRESENTATIONS)
                        s->presenting[s->presentings++] = s->feedback[at];
                else
                        wayland_feedback_discard(c, s->feedback[at]);
        }
        s->feedbacks = 0;
}

static void wayland_pool_unref(struct wayland_client *c, long pool);
static void wayland_release_buffer(struct wayland_client *c, long slot);

/* ---- subsurfaces ---- */

/*
        A subsurface is a surface that hangs from another at a place of its own, shown with it as one window: the window's
        picture is the root surface's with each subsurface's laid over it, in their order. The root's own buffer is copied and
        released at its commit as ever; a subsurface's current buffer is held, not released, until it is replaced, because
        the root's next commit overlaps it and it has to be laid over again from where it is. A subsurface that is
        synchronized (the way it is made, and while any surface above it is) commits into a cache, which the commit of the
        surface above it applies; one that is not is applied at once. What a subsurface may do to its window is bounded by
        the window: a picture past the root's edge is cut, and a subsurface never changes the window's size.
*/
static struct wayland_surface *wayland_root_of(struct wayland_client *c, struct wayland_surface *s)
{
        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < WAYLAND_SURFACES; hops++)
                s = &c->surfaces[s->sub_parent - 1];
        return s;
}

// A subsurface's place in its window's picture, in pixels: the sum of the places up the chain, at the root's scale.
static void wayland_sub_offset(struct wayland_client *c, struct wayland_surface *s, long *x, long *y)
{
        long dx = 0, dy = 0, scale = wayland_root_of(c, s)->scale120;

        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < WAYLAND_SURFACES; hops++)
        {
                dx += s->sub_x;
                dy += s->sub_y;
                s = &c->surfaces[s->sub_parent - 1];
        }
        *x = wayland_pixels(dx, scale > 0 ? scale : WAYLAND_UNIT);
        *y = wayland_pixels(dy, scale > 0 ? scale : WAYLAND_UNIT);
}

// Whether a subsurface is synchronized: itself, or any surface above it that is a subsurface.
static int wayland_sub_synced(struct wayland_client *c, struct wayland_surface *s)
{
        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < WAYLAND_SURFACES; hops++)
        {
                if (s->sub_sync)
                        return 1;
                s = &c->surfaces[s->sub_parent - 1];
        }
        return 0;
}

// A held buffer let go: its record is freed if its object was destroyed while it was held, else its client is told.
static void wayland_unhold(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_buffer *b;

        if (!s->held)
                return;
        b = &c->buffers[s->held - 1];
        s->held = 0;
        b->held--;
        if (b->zombie && b->held <= 0)
        {
                c->pools[b->pool].refs--;
                wayland_pool_unref(c, b->pool);
                b->live = 0;
                b->zombie = 0;
                wayland.buffers--;
        }
        else if (!b->zombie)
        {
                long slot = wayland_find(c, b->id, WLI_wl_buffer);

                if (slot >= 0)
                        wayland_release_buffer(c, slot);
        }
}

static int wayland_has_children(struct wayland_client *c, long parent)
{
        for (long at = 0; at < WAYLAND_SURFACES; at++)
                if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE && c->surfaces[at].sub_parent == parent + 1)
                        return 1;
        return 0;
}

// The next child of a surface above order, lowest first: its index, or -1.
static long wayland_next_child(struct wayland_client *c, long parent, long above, int *found)
{
        long best = -1;

        for (long at = 0; at < WAYLAND_SURFACES; at++)
        {
                struct wayland_surface *s = &c->surfaces[at];

                if (s->live && s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent == parent + 1 && (!*found || s->sub_order > above) &&
                    (best < 0 || s->sub_order < c->surfaces[best].sub_order))
                        best = at;
        }
        *found = 1;
        return best;
}

/*
        The part of a window's surfaces that is the window, in the root's pixels (which may be above and to the left of it, where
        a subsurface hangs past its edge): the geometry the toolkit set (in surface units, so at the scale), which is how it
        leaves out the shadow and the margins it draws round itself, taken from the bounding box of the root's picture
        (width by height pixels) and the subsurfaces; or all of the box when no geometry was set or none of it lies in the box.
        The row is this: what the person sees and points at begins at its corner, and what is outside it is not shown. A
        subsurface that is a window's title bar or border, outside the root, is in it when the geometry says so.
*/
static void wayland_window_rect(struct wayland_client *c, const struct wayland_surface *root, long width, long height, long scale120,
                                long *x, long *y, long *w, long *h)
{
        long x1 = 0, y1 = 0, x2 = width, y2 = height;

        for (long at = 0; at < WAYLAND_SURFACES; at++)
        {
                struct wayland_surface *child = &c->surfaces[at];
                long ox, oy;

                if (!child->live || child->role != WAYLAND_ROLE_SUBSURFACE || !child->held || wayland_root_of(c, child) != root)
                        continue;
                wayland_sub_offset(c, child, &ox, &oy);
                ox = wayland_max(-WAYLAND_DIMENSION, wayland_min(ox, WAYLAND_DIMENSION));
                oy = wayland_max(-WAYLAND_DIMENSION, wayland_min(oy, WAYLAND_DIMENSION));
                x1 = wayland_min(x1, ox);
                y1 = wayland_min(y1, oy);
                x2 = wayland_max(x2, ox + child->width);
                y2 = wayland_max(y2, oy + child->height);
        }
        if (root->geometry_set && scale120 >= WAYLAND_SCALE_LEAST)
        {
                long gx1 = wayland_max(wayland_pixels(root->geometry_x, scale120), x1);
                long gy1 = wayland_max(wayland_pixels(root->geometry_y, scale120), y1);
                long gx2 = wayland_min(wayland_pixels(root->geometry_x + root->geometry_width, scale120), x2);
                long gy2 = wayland_min(wayland_pixels(root->geometry_y + root->geometry_height, scale120), y2);

                if (gx2 > gx1 && gy2 > gy1)
                {
                        x1 = gx1;
                        y1 = gy1;
                        x2 = gx2;
                        y2 = gy2;
                }
        }
        *x = x1;
        *y = y1;
        *w = wayland_min(x2 - x1, WAYLAND_DIMENSION);
        *h = wayland_min(y2 - y1, WAYLAND_DIMENSION);
}

/* The row's picture taken: after the copies of a commit, the compositor is told the window has a new one. */
static void wayland_row_take(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_picture none;
        long wx, wy, ww, wh;

        if (root->row < 0)
                return;
        wayland_window_rect(c, root, root->width, root->height, root->scale120, &wx, &wy, &ww, &wh);
        none.pixels = 0;
        none.stride = 0;
        none.width = none.height = 0;
        none.format = 0;
        none.damages = 0;
        none.full = 0;
        none.dest_x = none.dest_y = 0;
        none.row_width = ww;
        none.row_height = wh;
        none.blend = 0;
        none.last = 1;
        none.sync_fd = -1;
        wayland.host->row_picture(wayland.host->context, root->row, &none);
}

/*
        The window of a root as wayland_window_rect makes it of what the root shows. A run of copies (a flatten, the copies of a
        commit) changes none of what it is made of, so the run's caller makes it once and hands it to each copy: made in every copy,
        it cost each copy a walk of every surface and its chain, and the copies of a window whose subsurfaces hang in one chain of
        sixty three were a quarter of a second of the compositor for one commit (ab981848).
*/
static struct wayland_rect wayland_window_of(struct wayland_client *c, const struct wayland_surface *root)
{
        struct wayland_rect window;

        wayland_window_rect(c, root, root->width, root->height, root->scale120, &window.x, &window.y, &window.width, &window.height);
        return window;
}

/*
        A client's budget (WAYLAND_BUDGET_*): its window of time begun again when it is over (or the clock went back), and whether
        the pixels copied for it in this one leave room. The largest screen is the unit, a screen of 1920 by 1080 with none.
*/
static int wayland_budget_left(struct wayland_client *c)
{
        long now = wayland.host->now(wayland.host->context), most = wayland.screens ? 0 : 1920L * 1080L;

        for (long at = 0; at < wayland.screens; at++)
                most = wayland_max(most, wayland.screen[at].width * wayland.screen[at].height);
        if (now - c->budget_at >= WAYLAND_BUDGET_MS || now < c->budget_at)
        {
                c->budget_at = now;
                c->budget_used = 0;
        }
        return c->budget_used < WAYLAND_BUDGET_SCREENS * most;
}

/* A subsurface's held buffer laid over the window's picture, all of it or where only (row pixels) says; window is its root's. */
static void wayland_child_copy(struct wayland_client *c, struct wayland_surface *child, const struct wayland_rect *only,
                               const struct wayland_rect *window)
{
        struct wayland_surface *root = wayland_root_of(c, child);
        struct wayland_picture picture;
        struct wayland_buffer *b;
        long ox, oy, wx = window->x, wy = window->y, ww = window->width, wh = window->height;

        if (root->row < 0 || !child->held || (child != root && !child->sub_parent))
                return;
        b = &c->buffers[child->held - 1];
        wayland_sub_offset(c, child, &ox, &oy);
        picture.pixels = (const unsigned char *)c->pools[b->pool].at + b->offset;
        picture.stride = b->stride;
        picture.width = b->width;
        picture.height = b->height;
        picture.format = b->format;
        picture.dest_x = ox - wx;
        picture.dest_y = oy - wy;
        picture.row_width = ww;
        picture.row_height = wh;
        picture.blend = child != root && b->format == WLN_wl_shm_format_argb8888;
        picture.last = 0;
        picture.sync_fd = c->pools[b->pool].dmabuf ? c->pools[b->pool].fd : -1;
        picture.damages = 0;
        picture.full = 1;
        if (only)
        {
                long x1 = wayland_max(only->x - ox, 0), y1 = wayland_max(only->y - oy, 0);
                long x2 = wayland_min(only->x + only->width - ox, b->width), y2 = wayland_min(only->y + only->height - oy, b->height);

                if (x2 <= x1 || y2 <= y1)
                        return;
                picture.full = 0;
                picture.damages = 1;
                picture.damage[0].x = x1;
                picture.damage[0].y = y1;
                picture.damage[0].width = x2 - x1;
                picture.damage[0].height = y2 - y1;
        }
        if (wayland.host->row_picture(wayland.host->context, root->row, &picture) > 0)
                root->need_full = 1;
        c->budget_used += picture.full ? wayland_min(b->width, ww) * wayland_min(b->height, wh) : picture.damage[0].width * picture.damage[0].height;
}

// The children of a surface laid over where it was just drawn (the whole picture for a null rectangle), in their order.
static void wayland_flatten(struct wayland_client *c, long parent, const struct wayland_rect *only, const struct wayland_rect *window)
{
        int found = 0;
        long order = 0;

        for (long child = wayland_next_child(c, parent, order, &found); child >= 0; child = wayland_next_child(c, parent, order, &found))
        {
                order = c->surfaces[child].sub_order;
                wayland_child_copy(c, &c->surfaces[child], only, window);
                wayland_flatten(c, child, only, window);
        }
}

/*
        The window's picture laid again, whole, where a subsurface that covered some of it went, moved or was put in another
        order: the root's held picture, when it kept one, and the subsurfaces over it. A root that kept none (it had no subsurface
        when it was last drawn) cannot show again what a subsurface that went had covered: only the order is laid again, and the
        rest waits for the root's next commit, which is drawn whole.
*/
static void wayland_relay(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_rect window;

        if (root->row < 0 || !root->need_full)
                return;
        if (!(root->flags & WAYLAND_SURFACE_MAPPED))
                return;
        // What is laid now is the window as it is now: the next picture of the root is drawn whole only if that changes.
        window = wayland_window_of(c, root);
        root->window_x = window.x;
        root->window_y = window.y;
        root->window_w = window.width;
        root->window_h = window.height;
        root->need_full = 0;
        if (root->held)
                wayland_child_copy(c, root, 0, &window);
        wayland_flatten(c, wayland_surface_index(c, root), 0, &window);
        wayland_row_take(c, root);
}

// Whether a surface has a viewport that says something: a destination or a source.
static inline int wayland_viewport_used(const struct wayland_surface *s)
{
        return s->dest_width > 0 || s->source_width > 0;
}

/*
        The scale a buffer is shown at, in 120ths. A surface with no viewport has its buffer scale, a whole number. With a
        destination, the unit is the buffer's pixels (the source's, when it has one) over the destination: and the scale the
        surface was told, where the buffer is the size that scale gives the destination to a pixel (a size that is not a whole
        number of pixels is rounded by the client, and a surface of a few units would otherwise be off by a good part).
*/
static long wayland_commit_scale(const struct wayland_surface *s, const struct wayland_buffer *b)
{
        long pixels = s->source_width > 0 ? (s->source_width * s->pending_scale + 255) / 256 : b->width, scale;
        long told = s->told120 > 0 ? wayland_pixels(s->dest_width, s->told120) : 0;

        if (s->dest_width <= 0)
                return WAYLAND_UNIT * s->pending_scale;
        if (told > 0 && wayland_max(pixels, told) - wayland_min(pixels, told) <= 1)
                return s->told120;
        scale = (pixels * WAYLAND_UNIT + s->dest_width / 2) / s->dest_width;
        return wayland_max(WAYLAND_SCALE_LEAST, wayland_min(scale, WAYLAND_SCALE_MOST));
}

/*
        A viewport's state against the buffer it is about to show: the source inside the buffer, and without a destination a
        source of whole units. 0 when it is right, else the viewport error.
*/
static long wayland_viewport_error(const struct wayland_surface *s, const struct wayland_buffer *b)
{
        if (s->source_width > 0 && ((s->source_x + s->source_width) * s->pending_scale > b->width * 256 ||
                                    (s->source_y + s->source_height) * s->pending_scale > b->height * 256))
                return WLN_wp_viewport_error_out_of_buffer;
        if (s->source_width > 0 && s->dest_width <= 0 && ((s->source_width & 255) || (s->source_height & 255)))
                return WLN_wp_viewport_error_bad_size;
        return 0;
}

/*
        A subsurface's pending state becomes what is shown: its place, and its buffer, which is held and laid over the window
        at once; the buffer it had is let go. A window that is not shown yet shows nothing of it, and the subsurface waits.
*/
static void wayland_sub_apply(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = wayland_root_of(c, s);
        long buffer = -1;

        if (root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED))
        {
                s->sub_dirty = 1;
                return;
        }
        s->sub_dirty = 0;
        if (s->pend_pos)
        {
                s->sub_x = s->pend_x;
                s->sub_y = s->pend_y;
                s->pend_pos = 0;
                root->need_full = 1;
        }
        if (s->attach_made && s->attached)
        {
                buffer = wayland_find(c, s->attached, WLI_wl_buffer);
                if (buffer >= 0 && ((!wayland_viewport_used(s) && (c->buffers[c->objects[buffer].link].width % s->pending_scale ||
                                                                  c->buffers[c->objects[buffer].link].height % s->pending_scale)) ||
                                    wayland_viewport_error(s, &c->buffers[c->objects[buffer].link])))
                        buffer = -1;
        }
        if (buffer >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                long ox, oy, scale120 = wayland_commit_scale(s, b);
                int full = s->held == 0 || s->width != b->width || s->height != b->height || s->damage_full || !s->damages ||
                           scale120 != s->scale120;
                struct wayland_rect window;

                wayland_unhold(c, s);
                b->held++;
                s->held = (unsigned short)(c->objects[buffer].link + 1);
                s->width = b->width;
                s->height = b->height;
                s->scale = s->pending_scale;
                s->scale120 = scale120;
                wayland_sub_offset(c, s, &ox, &oy);
                window = wayland_window_of(c, root);
                if (root->need_full)
                        wayland_relay(c, root);
                else if (b->format == WLN_wl_shm_format_argb8888 || s->translucent)
                {
                        // A translucent picture is laid over what is under it, which is not what an earlier picture left there:
                        // the root and what lies below it are laid again where it is, old place and new.
                        struct wayland_rect rect = {ox, oy, b->width, b->height};

                        if (s->translucent)
                        {
                                long x2 = wayland_max(rect.x + rect.width, s->translucent_x + s->translucent_w);
                                long y2 = wayland_max(rect.y + rect.height, s->translucent_y + s->translucent_h);

                                rect.x = wayland_min(rect.x, s->translucent_x);
                                rect.y = wayland_min(rect.y, s->translucent_y);
                                rect.width = x2 - rect.x;
                                rect.height = y2 - rect.y;
                        }
                        if (root->held)
                                wayland_child_copy(c, root, &rect, &window);
                        wayland_flatten(c, wayland_surface_index(c, root), &rect, &window);
                }
                else if (full)
                {
                        wayland_child_copy(c, s, 0, &window);
                        wayland_flatten(c, wayland_surface_index(c, s), 0, &window);
                }
                else
                        for (long at = 0; at < s->damages; at++)
                        {
                                struct wayland_rect r = s->damage[at];

                                if (!s->damage_kind[at])
                                {
                                        r.x = wayland_pixels(r.x, s->scale120);
                                        r.y = wayland_pixels(r.y, s->scale120);
                                        r.width = wayland_pixels(r.width, s->scale120);
                                        r.height = wayland_pixels(r.height, s->scale120);
                                }
                                r.x += ox;
                                r.y += oy;
                                wayland_child_copy(c, s, &r, &window);
                                wayland_flatten(c, wayland_surface_index(c, s), &r, &window);
                        }
        }
        else if (s->attach_made && !s->attached)
        {
                // A null buffer takes the subsurface off the window; what it covered is drawn again from the root's picture.
                wayland_unhold(c, s);
                s->width = s->height = 0;
                root->need_full = 1;
                wayland_relay(c, root);
        }
        else if (s->attach_made)
        {
                // An attach that could not be shown: the buffer is let go, the subsurface keeps what it had.
                long gone = wayland_find(c, s->attached, WLI_wl_buffer);

                if (gone >= 0)
                        wayland_release_buffer(c, gone);
        }
        if (root->need_full)
                wayland_relay(c, root);
        if (s->held)
        {
                struct wayland_buffer *shown = &c->buffers[s->held - 1];
                long px, py;

                wayland_sub_offset(c, s, &px, &py);
                s->translucent = shown->format == WLN_wl_shm_format_argb8888;
                s->translucent_x = px;
                s->translucent_y = py;
                s->translucent_w = shown->width;
                s->translucent_h = shown->height;
        }
        else
                s->translucent = 0;
        s->attach_made = 0;
        s->attached = 0;
        s->cached = 0;
        s->damages = 0;
        s->damage_full = 0;
        s->transform = s->pending_transform;
        s->alpha = s->pending_alpha;
        s->async_hint = s->pending_async;
        for (long at = 0; at < s->callbacks; at++)
                if (s->readies < WAYLAND_CALLBACKS)
                        s->ready[s->readies++] = s->callback[at];
                else
                {
                        long slot = wayland_find(c, s->callback[at], WLI_wl_callback);

                        if (slot >= 0)
                                wayland_object_free(c, slot);
                }
        s->callbacks = 0;
        wayland_feedback_take(c, s, 1);
}

// The subsurfaces under a surface that are waiting for its commit, applied now, and theirs under them.
static void wayland_sub_apply_children(struct wayland_client *c, long parent)
{
        for (long at = 0; at < WAYLAND_SURFACES; at++)
        {
                struct wayland_surface *s = &c->surfaces[at];

                if (!s->live || s->role != WAYLAND_ROLE_SUBSURFACE || s->sub_parent != parent + 1)
                        continue;
                if (s->sub_dirty || s->pend_pos)
                        wayland_sub_apply(c, s);
                wayland_sub_apply_children(c, at);
        }
}

// A subsurface's commit: cached while it is synchronized, applied at once when it is not.
static int wayland_sub_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        struct wayland_surface *parent = s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0;

        if (s->attach_made && s->attached && wayland_find(c, s->attached, WLI_wl_buffer) < 0)
                return wayland_fail(c, self->id, WLN_wl_display_error_invalid_object, "the attached buffer is gone");
        if (!parent || !parent->live)
        {
                // A subsurface with no surface above it shows nowhere: its buffer is let go, its callbacks answered now.
                long gone = s->attach_made && s->attached ? wayland_find(c, s->attached, WLI_wl_buffer) : -1;

                if (gone >= 0)
                        wayland_release_buffer(c, gone);
                s->attach_made = 0;
                s->attached = 0;
                s->damages = 0;
                for (long at = 0; at < s->callbacks; at++)
                        if (s->readies < WAYLAND_CALLBACKS)
                                s->ready[s->readies++] = s->callback[at];
                s->callbacks = 0;
                wayland_feedback_take(c, s, 0);
                wayland_callbacks_done(c, s, wayland.host->now(wayland.host->context));
                return WAYLAND_OK;
        }
        if (wayland_sub_synced(c, s))
        {
                // What an earlier commit cached and was not shown is let go, as a surface's replaced buffer is.
                if (s->attach_made && s->cached && s->cached != s->attached)
                {
                        long old = wayland_find(c, s->cached, WLI_wl_buffer);

                        if (old >= 0)
                                wayland_release_buffer(c, old);
                }
                if (s->attach_made)
                        s->cached = s->attached;
                s->sub_dirty = 1;
                return WAYLAND_OK;
        }
        wayland_sub_apply(c, s);
        wayland_sub_apply_children(c, wayland_surface_index(c, s));
        wayland_row_take(c, wayland_root_of(c, s));
        return WAYLAND_OK;
}

// A surface gone (or taken off its parent): what it held is let go, what hung from it hangs from nothing, and its window is drawn whole.
static void wayland_sub_drop(struct wayland_client *c, struct wayland_surface *s)
{
        long self = wayland_surface_index(c, s);
        struct wayland_surface *root = s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent ? wayland_root_of(c, s) : 0;

        wayland_unhold(c, s);
        for (long at = 0; at < WAYLAND_SURFACES; at++)
                if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE && c->surfaces[at].sub_parent == self + 1)
                {
                        wayland_unhold(c, &c->surfaces[at]);
                        c->surfaces[at].sub_parent = 0;
                }
        s->sub_parent = 0;
        if (root && root != s && root->live)
        {
                root->need_full = 1;
                wayland_relay(c, root);
        }
}

/* A surface gone, in whatever order: its window, its callbacks, and the objects that named it inert. */
static void wayland_constraints_drop(struct wayland_client *c, long surface);

static void wayland_surface_drop(struct wayland_client *c, struct wayland_surface *s)
{
        wayland_constraints_drop(c, wayland_surface_index(c, s));
        wayland_sub_drop(c, s);
        wayland_row_release(c, s);
        for (long at = 0; at < s->callbacks; at++)
        {
                long slot = wayland_find(c, s->callback[at], WLI_wl_callback);

                if (slot >= 0)
                        wayland_object_free(c, slot);
        }
        for (long at = 0; at < s->readies; at++)
        {
                long slot = wayland_find(c, s->ready[at], WLI_wl_callback);

                if (slot >= 0)
                        wayland_object_free(c, slot);
        }
        // The surface the pointer was over, or a window it was over, is nowhere now, and the next motion enters it again.
        if (wayland.pointer_row >= 0 && wayland.row_client[wayland.pointer_row] == c->slot &&
            wayland.pointer_surface == wayland_surface_index(c, s))
                wayland.pointer_row = -1;
        s->live = 0;
        s->gen++;
        wayland.surfaces--;
}

/* ---- requests ---- */

/* A new id that cannot be had: out of the client's range, in use, or no room left for another object. */
static int wayland_new_fail(struct wayland_client *c, unsigned long self, unsigned long id)
{
        if (id < 1 || id >= WAYLAND_IDS)
                return wayland_fail(c, self, WLN_wl_display_error_invalid_object, "the new id is out of range");
        if (wayland_slot_of(c, id) >= 0)
                return wayland_fail(c, self, WLN_wl_display_error_invalid_object, "the new id is in use");
        return wayland_fail(c, 1, WLN_wl_display_error_no_memory, "too many objects");
}

#define WAYLAND_INVALID_OBJECT WLN_wl_display_error_invalid_object
#define WAYLAND_INVALID_METHOD WLN_wl_display_error_invalid_method
#define WAYLAND_NO_MEMORY WLN_wl_display_error_no_memory
#define WAYLAND_IMPLEMENTATION WLN_wl_display_error_implementation

/* wl_display: sync answers at once, so its callback never lives; get_registry lists the globals. */
static int wayland_display_request(struct wayland_client *c, long op, struct wayland_arg *a)
{
        if (op == WLQ_wl_display_sync)
        {
                struct wayland_event e = wayland_begin(c, c->objects[a[0].slot].id, WLE_wl_callback_done);

                wayland_word(&e, (unsigned long)wayland_serial(c));
                wayland_end(&e);
                wayland_object_delete(c, a[0].slot);
                return WAYLAND_OK;
        }
        for (long name = 1; name <= WAYLAND_GLOBAL_COUNT; name++)
                if (wayland_global_offered(name))
                        wayland_global_send(c, c->objects[a[0].slot].id, name);
        for (long screen = 0; screen < wayland.screens; screen++)
                wayland_global_send(c, c->objects[a[0].slot].id, wayland_output_name(screen));
        return WAYLAND_OK;
}

/*
        wl_registry.bind: a global by its name, the interface named exactly, a version from one to the one built,
        and a new id. Checked whole before the object is made; the events a bound global sends follow it.
*/
static int wayland_registry_request(struct wayland_client *c, struct wayland_object *self, struct wayland_arg *a)
{
        long name = (long)a[0].u, iface, version, slot;
        unsigned long id = a[1].u;

        if (name >= 1 && name <= WAYLAND_GLOBAL_COUNT && wayland_global_offered(name))
        {
                iface = wayland_globals[name - 1].iface;
                version = wayland_globals[name - 1].version;
        }
        else if (wayland_output_of(name) >= 0)
        {
                iface = WLI_wl_output;
                version = 4;
        }
        else
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "no such global");
        if (!wayland_name_is(a[1].text, a[1].length, wl_interfaces[iface].name))
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the global is another interface");
        if (a[1].version < 1 || a[1].version > version)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "no such version");
        slot = id >= 1 && id < WAYLAND_IDS ? wayland_object_new(c, id, iface, a[1].version) : -1;
        if (slot < 0)
                return wayland_new_fail(c, self->id, id);
        if (iface == WLI_wl_output)
        {
                c->objects[slot].aux = (unsigned short)wayland_output_of(name);
                wayland_output_send(c, slot);
        }
        else if (iface == WLI_wl_shm)
        {
                static const unsigned long formats[] = {WLN_wl_shm_format_argb8888, WLN_wl_shm_format_xrgb8888};

                for (long at = 0; at < 2; at++)
                {
                        struct wayland_event e = wayland_begin(c, id, WLE_wl_shm_format);

                        wayland_word(&e, formats[at]);
                        wayland_end(&e);
                }
        }
        else if (iface == WLI_zwp_linux_dmabuf_v1 && a[1].version < 4)
                wayland_dmabuf_formats(c, id, a[1].version);
        else if (iface == WLI_xdg_toplevel_icon_manager_v1)
        {
                // No icon size is preferred, so the done event alone (the protocol sends it when there is no preference).
                struct wayland_event e = wayland_begin(c, id, WLE_xdg_toplevel_icon_manager_v1_done);

                wayland_end(&e);
        }
        else if (iface == WLI_wp_presentation)
        {
                // The clock every presented time is on: CLOCK_MONOTONIC (1), the clock the compositor's frames are timed by.
                struct wayland_event e = wayland_begin(c, id, WLE_wp_presentation_clock_id);

                wayland_word(&e, 1);
                wayland_end(&e);
        }
        else if (iface == WLI_wl_seat)
        {
                struct wayland_event e = wayland_begin(c, id, WLE_wl_seat_capabilities);

                wayland_word(&e, (unsigned long)wayland.host->seat(wayland.host->context));
                wayland_end(&e);
                if (a[1].version >= 2)
                {
                        e = wayland_begin(c, id, WLE_wl_seat_name);
                        wayland_string(&e, "seat0");
                        wayland_end(&e);
                }
        }
        return WAYLAND_OK;
}

/* wl_compositor: a surface with no role, and a region, which a surface's regions name and which has no effect here. */
/*
        A surface is told the scale of the screen it is on: its fractional scale, when it asked for one, and the whole scale
        (wl_surface.preferred_buffer_scale, from version 6) when that changed. Said again only when the percent is another
        one, so it can be called for every window each pass.
*/
static void wayland_scale_tell(struct wayland_client *c, struct wayland_surface *s, long percent)
{
        long scale120 = wayland_percent120(percent), whole = wayland_percent_whole(percent), slot;
        struct wayland_event e;

        if (s->told120 == scale120 && s->told_whole == whole)
                return;
        if (s->fractional_id && s->told120 != scale120)
        {
                e = wayland_begin(c, s->fractional_id, WLE_wp_fractional_scale_v1_preferred_scale);
                wayland_word(&e, (unsigned long)scale120);
                wayland_end(&e);
        }
        slot = wayland_find(c, s->id, WLI_wl_surface);
        if (slot >= 0 && c->objects[slot].version >= 6 && s->told_whole != whole)
        {
                e = wayland_begin(c, s->id, WLE_wl_surface_preferred_buffer_scale);
                wayland_word(&e, (unsigned long)whole);
                wayland_end(&e);
        }
        s->told120 = scale120;
        s->told_whole = whole;
}

static int wayland_compositor_request(struct wayland_client *c, long op, struct wayland_arg *a)
{
        long slot, screen_scale = wayland.screens ? wayland.screen[0].scale : 100;
        struct wayland_surface *s;

        if (op == WLQ_wl_compositor_create_region)
                return WAYLAND_OK;
        slot = wayland_surface_slot(c);
        if (slot < 0)
                return wayland_fail(c, c->objects[a[0].slot].id, WAYLAND_NO_MEMORY, "too many surfaces");
        s = &c->surfaces[slot];
        {
                unsigned int gen = s->gen;
                unsigned char *clear = (unsigned char *)s;

                for (unsigned long at = 0; at < sizeof *s; at++)
                        clear[at] = 0;
                s->gen = gen;
        }
        s->live = 1;
        s->id = c->objects[a[0].slot].id;
        s->row = -1;
        s->scale = s->pending_scale = 1;
        s->alpha = s->pending_alpha = 0xffffffffu;
        s->scale120 = WAYLAND_UNIT;
        c->objects[a[0].slot].link = (short)slot;
        c->objects[a[0].slot].aux = (unsigned short)s->gen;
        wayland.surfaces++;
        wayland_scale_tell(c, s, screen_scale);
        if (c->objects[a[0].slot].version >= 6)
        {
                struct wayland_event e = wayland_begin(c, s->id, WLE_wl_surface_preferred_buffer_transform);

                wayland_word(&e, WLN_wl_output_transform_normal);
                wayland_end(&e);
        }
        return WAYLAND_OK;
}

static int wayland_region_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        (void)c;
        // flags bit 0: a rectangle was added, so the region is not empty.
        if (op == WLQ_wl_region_add && a[2].i > 0 && a[3].i > 0)
                self->flags |= 1;
        return WAYLAND_OK;
}

/* A damage rectangle of a pending commit, kept in the order it came; past the room, one more is joined to the last. */
static void wayland_damage_add(struct wayland_surface *s, struct wayland_arg *a, int buffer)
{
        struct wayland_rect r = {a[0].i, a[1].i, a[2].i, a[3].i};
        long last;

        if (r.width <= 0 || r.height <= 0)
                return;
        r.x = wayland_max(-(1L << 30), wayland_min(r.x, 1L << 30));
        r.y = wayland_max(-(1L << 30), wayland_min(r.y, 1L << 30));
        r.width = wayland_min(r.width, 1L << 30);
        r.height = wayland_min(r.height, 1L << 30);
        if (s->damages < WAYLAND_DAMAGE)
        {
                s->damage[s->damages] = r;
                s->damage_kind[s->damages++] = (unsigned char)buffer;
                return;
        }
        // Joined: the last rectangle and this one, in their own coordinates, become one that holds both.
        last = s->damages - 1;
        if (s->damage_kind[last] != buffer)
        {
                s->damage_full = 1;
                return;
        }
        {
                long x2 = wayland_max(s->damage[last].x + s->damage[last].width, r.x + r.width);
                long y2 = wayland_max(s->damage[last].y + s->damage[last].height, r.y + r.height);

                s->damage[last].x = wayland_min(s->damage[last].x, r.x);
                s->damage[last].y = wayland_min(s->damage[last].y, r.y);
                s->damage[last].width = x2 - s->damage[last].x;
                s->damage[last].height = y2 - s->damage[last].y;
        }
}

static void wayland_pool_unref(struct wayland_client *c, long pool)
{
        struct wayland_pool *p = &c->pools[pool];

        if (!p->live || p->refs > 0 || !p->gone)
                return;
        wayland.host->unmap(wayland.host->context, p->at, p->size);
        if (p->dmabuf)
        {
                wayland.host->close(wayland.host->context, p->fd);
                wayland.fds--;
                p->dmabuf = 0;
                c->dmabufs--;
                wayland.dmabufs--;
        }
        c->pool_bytes -= p->size;
        p->live = 0;
        wayland.pools--;
}

/* The screen a point is on, by the table the compositor gave: the first that holds it, else the first. */
static long wayland_screen_at(long x, long y)
{
        for (long at = 0; at < wayland.screens; at++)
                if (x >= wayland.screen[at].x && x < wayland.screen[at].x + wayland.screen[at].width && y >= wayland.screen[at].y &&
                    y < wayland.screen[at].y + wayland.screen[at].height)
                        return at;
        return 0;
}

/* A surface shown on a screen is told so, by each wl_output of its client that is that screen: enter once, leave when it goes. */
static void wayland_surface_screens(struct wayland_client *c, struct wayland_surface *s, long screen)
{
        unsigned char now = screen >= 0 && screen < 8 ? (unsigned char)(1u << screen) : 0;

        if (now == s->entered)
                return;
        for (long at = 0; at < WAYLAND_OBJECTS; at++)
        {
                struct wayland_object *o = &c->objects[at];
                struct wayland_event e;

                if (o->iface != WLI_wl_output || o->aux >= 8)
                        continue;
                if ((s->entered & (1u << o->aux)) && !(now & (1u << o->aux)))
                {
                        e = wayland_begin(c, s->id, WLE_wl_surface_leave);
                        wayland_word(&e, o->id);
                        wayland_end(&e);
                }
                else if (!(s->entered & (1u << o->aux)) && (now & (1u << o->aux)))
                {
                        e = wayland_begin(c, s->id, WLE_wl_surface_enter);
                        wayland_word(&e, o->id);
                        wayland_end(&e);
                }
        }
        s->entered = now;
}

/*
        The window of a surface that has a buffer: its row, opened by the first picture, and the buffer's damaged rows
        copied into it. A popup whose parent is not shown has nothing to be shown over, and is not shown.
*/
static int wayland_show(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s,
                        struct wayland_buffer *b, long scale120)
{
        struct wayland_pool *p = &c->pools[b->pool];
        struct wayland_picture picture;
        int first = s->row < 0;
        long wx, wy, ww, wh, status;

        if (first)
        {
                int parent = -1;
                struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};

                if (s->role == WAYLAND_ROLE_POPUP)
                {
                        struct wayland_surface *above = s->parent ? &c->surfaces[s->parent - 1] : 0;

                        if (above && above->live && above->row >= 0)
                                parent = above->row;
                        if (parent < 0)
                                return WAYLAND_OK;
                }
                s->row = wayland.host->row_open(wayland.host->context, c->slot,
                                                s->role == WAYLAND_ROLE_POPUP ? WAYLAND_KIND_POPUP : WAYLAND_KIND_TOPLEVEL, parent,
                                                s->popup_x, s->popup_y);
                if (s->row < 0)
                {
                        s->row = -1;
                        return wayland_fail(c, s->role_id, WAYLAND_NO_MEMORY, "no window left");
                }
                wayland.row_client[s->row] = c->slot;
                wayland.row_surface[s->row] = wayland_surface_index(c, s);
                wayland.rows++;
                // The tab says the title, or what the program is called when it has set none.
                if (s->title[0] || s->app_id[0])
                        wayland.host->row_title(wayland.host->context, s->row, s->title[0] ? s->title : s->app_id, 0);
                if (wayland.host->row_facts(wayland.host->context, s->row, &facts) >= 0)
                        wayland_surface_screens(c, s, wayland_screen_at(facts.x, facts.y));
        }
        picture.pixels = (const unsigned char *)p->at + b->offset;
        picture.stride = b->stride;
        picture.width = b->width;
        picture.height = b->height;
        picture.format = b->format;
        picture.damages = 0;
        wayland_window_rect(c, s, b->width, b->height, scale120, &wx, &wy, &ww, &wh);
        picture.dest_x = -wx;
        picture.dest_y = -wy;
        picture.row_width = ww;
        picture.row_height = wh;
        picture.blend = 0;
        picture.sync_fd = p->dmabuf ? p->fd : -1;
        picture.last = !wayland_has_children(c, wayland_surface_index(c, s));
        picture.full = first || s->width != b->width || s->height != b->height || s->damage_full || scale120 != s->scale120 || !s->damages ||
                       s->need_full || wx != s->window_x || wy != s->window_y || ww != s->window_w || wh != s->window_h;
        s->window_x = wx;
        s->window_y = wy;
        s->window_w = ww;
        s->window_h = wh;
        s->need_full = 0;
        for (long at = 0; at < s->damages && !picture.full; at++)
        {
                struct wayland_rect r = s->damage[at];
                long x2, y2;

                if (!s->damage_kind[at])
                {
                        r.x = wayland_pixels(r.x, scale120);
                        r.y = wayland_pixels(r.y, scale120);
                        r.width = wayland_pixels(r.width, scale120);
                        r.height = wayland_pixels(r.height, scale120);
                }
                x2 = wayland_min(r.x + r.width, b->width);
                y2 = wayland_min(r.y + r.height, b->height);
                r.x = wayland_max(r.x, 0);
                r.y = wayland_max(r.y, 0);
                if (x2 <= r.x || y2 <= r.y)
                        continue;
                r.width = x2 - r.x;
                r.height = y2 - r.y;
                picture.damage[picture.damages++] = r;
        }
        status = wayland.host->row_picture(wayland.host->context, s->row, &picture);
        c->budget_used += picture.full ? wayland_min(b->width, ww) * wayland_min(b->height, wh) : 0;
        for (long at = 0; !picture.full && at < picture.damages; at++)
                c->budget_used += picture.damage[at].width * picture.damage[at].height;
        if (status < 0)
                return wayland_fail(c, self->id, WAYLAND_IMPLEMENTATION, "the window refused the picture");
        if (status > 0)
                s->need_full = 1; // the buffer was not read (a dma-buf whose writer was not done): not shown, and the next is shown whole
        s->flags |= WAYLAND_SURFACE_MAPPED;
        // What was copied, for the subsurfaces to be laid over again.
        s->shown_full = (unsigned char)picture.full;
        if (!picture.full)
        {
                long x2 = 0, y2 = 0;

                s->shown_rect = picture.damage[0];
                x2 = picture.damage[0].x + picture.damage[0].width;
                y2 = picture.damage[0].y + picture.damage[0].height;
                for (long at = 1; at < picture.damages; at++)
                {
                        s->shown_rect.x = wayland_min(s->shown_rect.x, picture.damage[at].x);
                        s->shown_rect.y = wayland_min(s->shown_rect.y, picture.damage[at].y);
                        x2 = wayland_max(x2, picture.damage[at].x + picture.damage[at].width);
                        y2 = wayland_max(y2, picture.damage[at].y + picture.damage[at].height);
                }
                s->shown_rect.width = x2 - s->shown_rect.x;
                s->shown_rect.height = y2 - s->shown_rect.y;
        }
        if (first)
        {
                // What was asked before the window was shown is done now that it is.
                if (s->want_maximized)
                        wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_MAXIMIZE, 0, 0);
                if (s->want_fullscreen)
                        wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_FULLSCREEN, 0, 0);
        }
        return WAYLAND_OK;
}

/*
        Whether a commit of s is held now, for wp_fifo or wp_commit_timing: its barrier is set by an earlier commit and no frame has been drawn
        since that commit (wayland_frame moves the count on), or its time is later than the host's now. A window that is not shown holds
        nothing, since no frame would come to clear the barrier, and a synchronized subsurface is held by its parent's commit, not its own.
*/
static int wayland_hold_wanted(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = wayland_root_of(c, s);

        if (!root || root->row < 0 || (s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_sync))
                return 0;
        if (s->pending_fifo && s->barrier && s->barrier_frame == wayland.frames)
                return 1;
        return s->pending_timestamp && (unsigned long)wayland.host->now(wayland.host->context) * 1000000ul < s->pending_timestamp;
}

/*
        A commit. The pending state becomes the shown one: a buffer attached is copied (the damaged rows of it) into the
        surface's window and released at once, a null buffer unmaps the window, and the frame callbacks the client
        asked for are taken, to be answered when the frame is on the screen. A window's first commit has no buffer and
        is answered with the initial configure; a buffer before the client has acknowledged one is a protocol error.
*/
static int wayland_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        long buffer = -1;
        int xdg = (s->flags & WAYLAND_SURFACE_XDG) != 0, initial = 0;

        // wp_fifo and wp_commit_timing: a commit that is held is held as one that waits for its budget is: the client's stream waits
        // here, and the commit is made again when wayland_resume finds it no longer held. A barrier this commit sets is set once it is
        // not held (the commit before it may have set one, and the frame that clears it is the next one drawn).
        if (wayland_hold_wanted(c, s))
        {
                c->hold = 1;
                c->hold_surface = wayland_surface_index(c, s);
                c->hold_at = s->pending_timestamp ? (long)(s->pending_timestamp / 1000000ul) : 0;
                c->stalled_fd = -1;
                return WAYLAND_STALL;
        }
        if (s->pending_barrier)
        {
                s->barrier = 1;
                s->barrier_frame = wayland.frames;
                s->pending_barrier = 0;
        }

        // The alpha multiplier and the tearing hint are the pending state at every commit, with or without a buffer (the
        // protocol's double buffering).
        s->alpha = s->pending_alpha;
        s->icon = s->pending_icon;
        s->async_hint = s->pending_async;
        s->fifo_wait = s->pending_fifo;
        s->pending_fifo = 0;
        s->timestamp = s->pending_timestamp;
        s->pending_timestamp = 0;

        // A client past its share of a frame's copies: this commit and what follows it wait for its next window of time.
        if (!wayland_budget_left(c))
        {
                c->stalled_fd = -1;
                return WAYLAND_STALL;
        }
        if (s->role == WAYLAND_ROLE_SUBSURFACE)
                return wayland_sub_commit(c, self, s);
        if (xdg)
        {
                if (!s->role_id)
                        return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_not_constructed, "the xdg_surface has no role");
                if (!(s->flags & WAYLAND_SURFACE_SENT))
                {
                        if (s->attach_made && s->attached)
                                return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_unconfigured_buffer,
                                                    "a buffer before the first configure");
                        initial = 1;
                }
                else if (s->attach_made && s->attached && !(s->flags & WAYLAND_SURFACE_CONFIGURED))
                        return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_unconfigured_buffer,
                                            "a buffer before the first configure was acknowledged");
        }
        if (s->attach_made && s->attached)
        {
                buffer = wayland_find(c, s->attached, WLI_wl_buffer);
                if (buffer < 0)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the attached buffer is gone");
        }
        // A dma-buf the GPU is still drawing in: nothing of this commit is done, and it is made again when the buffer is ready.
        if (buffer >= 0)
        {
                struct wayland_pool *held = &c->pools[c->buffers[c->objects[buffer].link].pool];

                if (held->dmabuf && wayland.host->dmabuf_ready && !wayland.host->dmabuf_ready(wayland.host->context, held->fd))
                {
                        c->stalled_fd = held->fd;
                        return WAYLAND_STALL;
                }
        }
        if ((s->min_width && s->max_width && s->min_width > s->max_width) ||
            (s->min_height && s->max_height && s->min_height > s->max_height))
                return wayland_fail(c, s->role_id, WLN_xdg_toplevel_error_invalid_size, "the minimum size is above the maximum");

        if (initial)
        {
                if (s->role == WAYLAND_ROLE_TOPLEVEL)
                        wayland_configure(c, s, 1);
        }
        else if (buffer >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                long scale120 = wayland_commit_scale(s, b), error = wayland_viewport_error(s, b);

                if (error)
                        return wayland_fail(c, s->viewport_id, error, error == WLN_wp_viewport_error_out_of_buffer
                                                                          ? "the source is not inside the buffer"
                                                                          : "the source is not whole units and there is no destination");
                if (!wayland_viewport_used(s) && (b->width % s->pending_scale || b->height % s->pending_scale))
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_size, "the buffer is not a whole number of the scale");
                if (xdg && wayland_show(c, self, s, b, scale120) < 0)
                        return WAYLAND_DEAD;
                s->width = b->width;
                s->height = b->height;
                s->scale = s->pending_scale;
                s->scale120 = scale120;
                s->transform = s->pending_transform;
                if (xdg && s->row >= 0 && wayland_has_children(c, wayland_surface_index(c, s)))
                {
                        // A window with subsurfaces keeps its picture, as they do, to lay them over again; they are laid on now.
                        struct wayland_rect window;

                        wayland_unhold(c, s);
                        b->held++;
                        s->held = (unsigned short)(c->objects[buffer].link + 1);
                        window = wayland_window_of(c, s);
                        wayland_flatten(c, wayland_surface_index(c, s), s->shown_full ? 0 : &s->shown_rect, &window);
                        wayland_sub_apply_children(c, wayland_surface_index(c, s));
                        wayland_row_take(c, s);
                }
                else
                {
                        wayland_unhold(c, s);
                        wayland_release_buffer(c, buffer);
                }
        }
        else if (s->attach_made && !s->attached)
        {
                // A null buffer: the window goes, and what it was told is not true any more.
                if (xdg)
                {
                        wayland_unhold(c, s);
                        wayland_row_release(c, s);
                        s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
                        s->serials = 0;
                        s->entered = 0;
                }
                s->width = s->height = 0;
        }

        // A commit with no buffer of its own still lays on the subsurfaces that were waiting for it.
        if (buffer < 0 && !initial && xdg && s->row >= 0 && (s->flags & WAYLAND_SURFACE_MAPPED) &&
            wayland_has_children(c, wayland_surface_index(c, s)))
        {
                if (s->need_full)
                        wayland_relay(c, s);
                wayland_sub_apply_children(c, wayland_surface_index(c, s));
                wayland_row_take(c, s);
        }
        s->attach_made = 0;
        s->attached = 0;
        s->damages = 0;
        s->damage_full = 0;
        // The frame callbacks the commit takes. A surface with no window has no frame: its callbacks are answered now.
        for (long at = 0; at < s->callbacks; at++)
        {
                if (s->readies < WAYLAND_CALLBACKS)
                        s->ready[s->readies++] = s->callback[at];
                else
                {
                        long slot = wayland_find(c, s->callback[at], WLI_wl_callback);

                        if (slot >= 0)
                                wayland_object_free(c, slot);
                }
        }
        s->callbacks = 0;
        wayland_feedback_take(c, s, s->row >= 0);
        if (s->row < 0 && s->readies)
                wayland_callbacks_done(c, s, wayland.host->now(wayland.host->context));
        return WAYLAND_OK;
}

static int wayland_surface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);

        if (!s)
                return op == WLQ_wl_surface_destroy ? WAYLAND_OK : WAYLAND_OK;
        switch (op)
        {
        case WLQ_wl_surface_destroy:
                // The feedbacks a surface still holds are discarded by it: a surface gone is a frame that will not come.
                for (long at = 0; at < s->feedbacks; at++)
                        wayland_feedback_discard(c, s->feedback[at]);
                for (long at = 0; at < s->presentings; at++)
                        wayland_feedback_discard(c, s->presenting[at]);
                s->feedbacks = s->presentings = 0;
                if (s->role_id)
                {
                        // A role object that outlives its surface is inert; the window goes with the surface.
                        wayland_surface_drop(c, s);
                        return WAYLAND_OK;
                }
                wayland_surface_drop(c, s);
                return WAYLAND_OK;
        case WLQ_wl_surface_attach:
                if (self->version >= 5 && (a[1].i || a[2].i))
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_offset, "attach with an offset");
                s->attached = a[0].slot >= 0 ? c->objects[a[0].slot].id : 0;
                s->attach_made = 1;
                return WAYLAND_OK;
        case WLQ_wl_surface_damage:
                wayland_damage_add(s, a, 0);
                return WAYLAND_OK;
        case WLQ_wl_surface_damage_buffer:
                wayland_damage_add(s, a, 1);
                return WAYLAND_OK;
        case WLQ_wl_surface_frame:
                if (s->callbacks < WAYLAND_CALLBACKS)
                        s->callback[s->callbacks++] = c->objects[a[0].slot].id;
                else
                {
                        struct wayland_event e = wayland_begin(c, c->objects[a[0].slot].id, WLE_wl_callback_done);

                        wayland_word(&e, (unsigned long)wayland.host->now(wayland.host->context));
                        wayland_end(&e);
                        wayland_object_delete(c, a[0].slot);
                }
                return WAYLAND_OK;
        case WLQ_wl_surface_set_input_region:
                // A region with nothing added is a surface the pointer passes through, which a subsurface over its window may ask
                // for; no region is all of it again. Taken at the commit in truth, here at once.
                s->input_none = a[0].slot >= 0 && !(c->objects[a[0].slot].flags & 1);
                return WAYLAND_OK;
        case WLQ_wl_surface_set_opaque_region:
        case WLQ_wl_surface_offset:
                return WAYLAND_OK;
        case WLQ_wl_surface_commit:
                return wayland_commit(c, self, s);
        case WLQ_wl_surface_set_buffer_transform:
                if (a[0].i < 0 || a[0].i > 7)
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_transform, "no such transform");
                s->pending_transform = a[0].i;
                return WAYLAND_OK;
        case WLQ_wl_surface_set_buffer_scale:
                if (a[0].i < 1 || a[0].i > 16)
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_scale, "no such scale");
                s->pending_scale = a[0].i;
                return WAYLAND_OK;
        default:
                return WAYLAND_OK;
        }
}

/* wl_shm and its pools. A pool is mapped once, read-only, and its descriptor is closed; a buffer is a rectangle of it. */
static int wayland_shm_request(struct wayland_client *c, struct wayland_object *self, struct wayland_arg *a)
{
        long size = a[2].i, pool = -1;
        void *at;

        if (size < 1 || size > WAYLAND_POOL_BYTES || c->pool_bytes + size > WAYLAND_POOL_TOTAL)
                return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_stride, "the pool's size is out of range");
        for (long i = 0; i < WAYLAND_POOLS && pool < 0; i++)
                if (!c->pools[i].live)
                        pool = i;
        if (pool < 0)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many pools");
        at = wayland.host->map(wayland.host->context, a[1].fd, size);
        wayland.host->close(wayland.host->context, a[1].fd);
        wayland.fds--;
        a[1].fd = -1;
        if (!at)
                return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "not a shared memory file of that size");
        c->pools[pool].live = 1;
        c->pools[pool].fd = -1;
        c->pools[pool].dmabuf = 0;
        c->pools[pool].size = size;
        c->pools[pool].refs = 0;
        c->pools[pool].gone = 0;
        c->pools[pool].at = at;
        c->pool_bytes += size;
        c->objects[a[0].slot].link = (short)pool;
        wayland.pools++;
        return WAYLAND_OK;
}

static int wayland_pool_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long pool = self->link;
        struct wayland_pool *p = &c->pools[pool];

        if (op == WLQ_wl_shm_pool_destroy)
        {
                p->gone = 1;
                wayland_pool_unref(c, pool);
                return WAYLAND_OK;
        }
        if (op == WLQ_wl_shm_pool_resize)
        {
                long size = a[0].i;
                void *at;

                if (size < p->size || size > WAYLAND_POOL_BYTES || c->pool_bytes - p->size + size > WAYLAND_POOL_TOTAL)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "the pool can only grow, within its cap");
                at = wayland.host->remap(wayland.host->context, p->at, p->size, size);
                if (!at)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "the pool cannot grow");
                c->pool_bytes += size - p->size;
                p->at = at;
                p->size = size;
                return WAYLAND_OK;
        }
        {
                long offset = a[1].i, width = a[2].i, height = a[3].i, stride = a[4].i, format = a[5].i, slot = -1;

                if (format != WLN_wl_shm_format_argb8888 && format != WLN_wl_shm_format_xrgb8888)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_format, "the format is not offered");
                if (offset < 0 || width < 1 || height < 1 || width > WAYLAND_DIMENSION || height > WAYLAND_DIMENSION ||
                    stride < width * 4 || stride > (1L << 20) || (stride & 3) || offset > p->size ||
                    stride * height > p->size - offset)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_stride, "the buffer does not fit the pool");
                for (long i = 0; i < WAYLAND_BUFFERS && slot < 0; i++)
                        if (!c->buffers[i].live)
                                slot = i;
                if (slot < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many buffers");
                c->buffers[slot].live = 1;
                c->buffers[slot].pool = pool;
                c->buffers[slot].offset = offset;
                c->buffers[slot].width = width;
                c->buffers[slot].height = height;
                c->buffers[slot].stride = stride;
                c->buffers[slot].format = format;
                c->buffers[slot].id = c->objects[a[0].slot].id;
                c->buffers[slot].held = c->buffers[slot].zombie = 0;
                c->objects[a[0].slot].link = (short)slot;
                p->refs++;
                wayland.buffers++;
                return WAYLAND_OK;
        }
}

static int wayland_buffer_request(struct wayland_client *c, struct wayland_object *self)
{
        struct wayland_buffer *b = &c->buffers[self->link];

        // A surface that still names the buffer finds it gone at its commit; one that holds it as its picture goes on showing it,
        // and the record is freed when it lets go.
        if (b->held > 0)
        {
                b->zombie = 1;
                return WAYLAND_OK;
        }
        c->pools[b->pool].refs--;
        wayland_pool_unref(c, b->pool);
        b->live = 0;
        wayland.buffers--;
        return WAYLAND_OK;
}

/* wl_subcompositor and wl_subsurface. */
static int wayland_subcompositor_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s, *parent;
        long order = 0;

        if (op == WLQ_wl_subcompositor_destroy)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[1].slot);
        parent = wayland_surface_of(c, a[2].slot);
        if (!s || !parent)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
        if (s == parent)
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_surface, "a surface cannot hang from itself");
        // A surface whose wl_subsurface was destroyed keeps that role, and may be given a new one; while the object is there it may not.
        if ((s->role != WAYLAND_ROLE_NONE && !(s->role == WAYLAND_ROLE_SUBSURFACE && !s->sub_id)) || (s->flags & WAYLAND_SURFACE_XDG))
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_surface, "the surface has a role");
        // The parent may not hang, however far up, from the surface that is to hang from it.
        for (struct wayland_surface *up = parent; up; up = up->role == WAYLAND_ROLE_SUBSURFACE && up->sub_parent ? &c->surfaces[up->sub_parent - 1] : 0)
                if (up == s)
                        return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_parent, "the parent hangs from the surface");
        for (long at = 0; at < WAYLAND_SURFACES; at++)
                if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE &&
                    c->surfaces[at].sub_parent == wayland_surface_index(c, parent) + 1)
                        order = wayland_max(order, c->surfaces[at].sub_order);
        s->role = WAYLAND_ROLE_SUBSURFACE;
        s->sub_parent = (unsigned short)(wayland_surface_index(c, parent) + 1);
        s->sub_id = c->objects[a[0].slot].id;
        s->sub_sync = 1;
        s->sub_dirty = s->pend_pos = 0;
        s->sub_x = s->sub_y = s->pend_x = s->pend_y = 0;
        s->sub_order = order + 1;
        c->objects[a[0].slot].link = (short)wayland_surface_index(c, s);
        c->objects[a[0].slot].aux = (unsigned short)s->gen;
        return WAYLAND_OK;
}

static int wayland_subsurface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects), *other;
        long key;

        if (!s)
                return WAYLAND_OK;
        switch (op)
        {
        case WLQ_wl_subsurface_destroy:
                // Taken off its parent: unmapped, and a surface that keeps the role it had.
                wayland_sub_drop(c, s);
                s->sub_id = 0;
                return WAYLAND_OK;
        case WLQ_wl_subsurface_set_position:
                s->pend_x = wayland_max(-(1L << 20), wayland_min(a[0].i, 1L << 20));
                s->pend_y = wayland_max(-(1L << 20), wayland_min(a[1].i, 1L << 20));
                s->pend_pos = 1;
                return WAYLAND_OK;
        case WLQ_wl_subsurface_place_above:
        case WLQ_wl_subsurface_place_below:
                other = wayland_surface_of(c, a[0].slot);
                if (!other || (other != (s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0) &&
                               !(other->role == WAYLAND_ROLE_SUBSURFACE && other->sub_parent == s->sub_parent && other != s)))
                        return wayland_fail(c, self->id, WLN_wl_subsurface_error_bad_surface, "the surface is not a sibling or the parent");
                key = other == (s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0) ? 0 : other->sub_order;
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                {
                        struct wayland_surface *sibling = &c->surfaces[at];

                        if (!sibling->live || sibling == s || sibling->role != WAYLAND_ROLE_SUBSURFACE || sibling->sub_parent != s->sub_parent)
                                continue;
                        if (op == WLQ_wl_subsurface_place_above && sibling->sub_order > key)
                                sibling->sub_order++;
                        else if (op == WLQ_wl_subsurface_place_below && sibling->sub_order < key)
                                sibling->sub_order--;
                }
                s->sub_order = op == WLQ_wl_subsurface_place_above ? key + 1 : key - 1;
                wayland_root_of(c, s)->need_full = 1;
                return WAYLAND_OK;
        case WLQ_wl_subsurface_set_sync:
                s->sub_sync = 1;
                return WAYLAND_OK;
        default: // set_desync: what it had cached is shown now, unless a surface above it is still synchronized
                s->sub_sync = 0;
                if (s->sub_parent && !wayland_sub_synced(c, s) && (s->sub_dirty || s->pend_pos))
                {
                        wayland_sub_apply(c, s);
                        wayland_sub_apply_children(c, wayland_surface_index(c, s));
                        wayland_row_take(c, wayland_root_of(c, s));
                }
                return WAYLAND_OK;
        }
}

/* ---- xdg_shell ---- */

/*
        Where a popup goes, by xdg_positioner's rules: the anchor point of the anchor rectangle, the popup's gravity
        away from it, the offset; and where that is past the screen, flipped, then slid, then made smaller, each only
        on an axis and only as far as the positioner allows. The rectangle answered is relative to the parent's window
        geometry; origin is where that is on the desktop, and the screen is what must hold it.
*/
static void wayland_anchor_point(const struct wayland_positioner *p, long flip_x, long flip_y, long *x, long *y)
{
        long anchor = p->anchor, left, right, top, bottom;

        left = anchor == WLN_xdg_positioner_anchor_left || anchor == WLN_xdg_positioner_anchor_top_left ||
               anchor == WLN_xdg_positioner_anchor_bottom_left;
        right = anchor == WLN_xdg_positioner_anchor_right || anchor == WLN_xdg_positioner_anchor_top_right ||
                anchor == WLN_xdg_positioner_anchor_bottom_right;
        top = anchor == WLN_xdg_positioner_anchor_top || anchor == WLN_xdg_positioner_anchor_top_left ||
              anchor == WLN_xdg_positioner_anchor_top_right;
        bottom = anchor == WLN_xdg_positioner_anchor_bottom || anchor == WLN_xdg_positioner_anchor_bottom_left ||
                 anchor == WLN_xdg_positioner_anchor_bottom_right;
        if (flip_x)
        {
                long swap = left;

                left = right;
                right = swap;
        }
        if (flip_y)
        {
                long swap = top;

                top = bottom;
                bottom = swap;
        }
        *x = p->anchor_x + (left ? 0 : right ? p->anchor_width : p->anchor_width / 2);
        *y = p->anchor_y + (top ? 0 : bottom ? p->anchor_height : p->anchor_height / 2);
}

static void wayland_gravity_place(const struct wayland_positioner *p, long flip_x, long flip_y, long width, long height,
                                  long *x, long *y)
{
        long gravity = p->gravity, left, right, top, bottom, px, py;

        wayland_anchor_point(p, flip_x, flip_y, &px, &py);
        left = gravity == WLN_xdg_positioner_gravity_left || gravity == WLN_xdg_positioner_gravity_top_left ||
               gravity == WLN_xdg_positioner_gravity_bottom_left;
        right = gravity == WLN_xdg_positioner_gravity_right || gravity == WLN_xdg_positioner_gravity_top_right ||
                gravity == WLN_xdg_positioner_gravity_bottom_right;
        top = gravity == WLN_xdg_positioner_gravity_top || gravity == WLN_xdg_positioner_gravity_top_left ||
              gravity == WLN_xdg_positioner_gravity_top_right;
        bottom = gravity == WLN_xdg_positioner_gravity_bottom || gravity == WLN_xdg_positioner_gravity_bottom_left ||
                 gravity == WLN_xdg_positioner_gravity_bottom_right;
        if (flip_x)
        {
                long swap = left;

                left = right;
                right = swap;
        }
        if (flip_y)
        {
                long swap = top;

                top = bottom;
                bottom = swap;
        }
        *x = px + p->offset_x - (left ? width : right ? 0 : width / 2);
        *y = py + p->offset_y - (top ? height : bottom ? 0 : height / 2);
        if (flip_x)
                *x -= 2 * p->offset_x;
        if (flip_y)
                *y -= 2 * p->offset_y;
}

static void wayland_popup_place(const struct wayland_positioner *p, long origin_x, long origin_y,
                                const struct wayland_rect *screen, long *x, long *y, long *width, long *height)
{
        long w = p->width, h = p->height, ax, ay, fx = 0, fy = 0;
        long out_x, out_y;

        wayland_gravity_place(p, 0, 0, w, h, &ax, &ay);
        out_x = origin_x + ax < screen->x || origin_x + ax + w > screen->x + screen->width;
        out_y = origin_y + ay < screen->y || origin_y + ay + h > screen->y + screen->height;
        if (out_x && (p->adjustment & WLN_xdg_positioner_constraint_adjustment_flip_x))
        {
                long bx, by;

                wayland_gravity_place(p, 1, 0, w, h, &bx, &by);
                if (!(origin_x + bx < screen->x || origin_x + bx + w > screen->x + screen->width))
                        ax = bx, fx = 1, out_x = 0;
        }
        if (out_y && (p->adjustment & WLN_xdg_positioner_constraint_adjustment_flip_y))
        {
                long bx, by;

                wayland_gravity_place(p, 0, 1, w, h, &bx, &by);
                if (!(origin_y + by < screen->y || origin_y + by + h > screen->y + screen->height))
                        ay = by, fy = 1, out_y = 0;
        }
        (void)fx;
        (void)fy;
        if (out_x && (p->adjustment & WLN_xdg_positioner_constraint_adjustment_slide_x))
        {
                long at = origin_x + ax;

                if (at + w > screen->x + screen->width)
                        at = screen->x + screen->width - w;
                if (at < screen->x)
                        at = screen->x;
                ax = at - origin_x;
                out_x = origin_x + ax < screen->x || origin_x + ax + w > screen->x + screen->width;
        }
        if (out_y && (p->adjustment & WLN_xdg_positioner_constraint_adjustment_slide_y))
        {
                long at = origin_y + ay;

                if (at + h > screen->y + screen->height)
                        at = screen->y + screen->height - h;
                if (at < screen->y)
                        at = screen->y;
                ay = at - origin_y;
                out_y = origin_y + ay < screen->y || origin_y + ay + h > screen->y + screen->height;
        }
        if (out_x && (p->adjustment & WLN_xdg_positioner_constraint_adjustment_resize_x))
        {
                long left = wayland_max(origin_x + ax, screen->x), right = wayland_min(origin_x + ax + w, screen->x + screen->width);

                if (right > left)
                        ax = left - origin_x, w = right - left;
        }
        if (out_y && (p->adjustment & WLN_xdg_positioner_constraint_adjustment_resize_y))
        {
                long top = wayland_max(origin_y + ay, screen->y), bottom = wayland_min(origin_y + ay + h, screen->y + screen->height);

                if (bottom > top)
                        ay = top - origin_y, h = bottom - top;
        }
        *x = ax;
        *y = ay;
        *width = w;
        *height = h;
}

/* The popup's place and size, sent as the configure its client waits for. */
static void wayland_popup_configure(struct wayland_client *c, struct wayland_surface *s, const struct wayland_positioner *p,
                                    struct wayland_surface *parent)
{
        struct wayland_screen *screens = wayland.screen;
        struct wayland_rect screen = {0, 0, 1 << 20, 1 << 20};
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};
        long origin_x = 0, origin_y = 0, x, y, w, h, slot, unit = parent ? wayland_surface_unit(parent) : WAYLAND_UNIT;
        struct wayland_event e;
        unsigned long serial;

        // The positioner is in the parent's units, and so is the place it gives: the window's place and the screens' are
        // taken to them, and the popup's place goes to the compositor in pixels.
        if (parent && parent->row >= 0 && wayland.host->row_facts(wayland.host->context, parent->row, &facts) >= 0)
        {
                origin_x = wayland_units(facts.x, unit);
                origin_y = wayland_units(facts.y, unit);
        }
        for (long at = 0; at < wayland.screens; at++)
        {
                struct wayland_rect each = {wayland_units(screens[at].x, unit), wayland_units(screens[at].y, unit),
                                            wayland_units(screens[at].width, unit), wayland_units(screens[at].height, unit)};

                if (origin_x + p->anchor_x >= each.x && origin_x + p->anchor_x < each.x + each.width &&
                    origin_y + p->anchor_y >= each.y && origin_y + p->anchor_y < each.y + each.height)
                        screen = each;
        }
        wayland_popup_place(p, origin_x, origin_y, &screen, &x, &y, &w, &h);
        s->popup_x = wayland_pixels(x, unit);
        s->popup_y = wayland_pixels(y, unit);
        s->popup_width = wayland_pixels(w, unit);
        s->popup_height = wayland_pixels(h, unit);
        slot = wayland_find(c, s->role_id, WLI_xdg_popup);
        if (slot < 0 || s->serials >= WAYLAND_SERIALS)
                return;
        e = wayland_begin(c, s->role_id, WLE_xdg_popup_configure);
        wayland_word(&e, (unsigned long)x);
        wayland_word(&e, (unsigned long)y);
        wayland_word(&e, (unsigned long)w);
        wayland_word(&e, (unsigned long)h);
        wayland_end(&e);
        serial = wayland_serial(c);
        slot = wayland_find(c, s->xdg_id, WLI_xdg_surface);
        if (slot < 0)
                return;
        e = wayland_begin(c, s->xdg_id, WLE_xdg_surface_configure);
        wayland_word(&e, serial);
        wayland_end(&e);
        s->serial[s->serials++] = (unsigned int)serial;
        s->flags |= WAYLAND_SURFACE_SENT;
}

static int wayland_positioner_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_positioner *p = &c->positioners[self->link];

        switch (op)
        {
        case WLQ_xdg_positioner_destroy:
                p->live = 0;
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_size:
                if (a[0].i < 1 || a[1].i < 1)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "the size must be positive");
                p->width = wayland_min(a[0].i, WAYLAND_DIMENSION);
                p->height = wayland_min(a[1].i, WAYLAND_DIMENSION);
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_anchor_rect:
                if (a[2].i < 1 || a[3].i < 1)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input,
                                            "the anchor rectangle must be positive");
                p->anchor_x = wayland_max(-(1L << 24), wayland_min(a[0].i, 1L << 24));
                p->anchor_y = wayland_max(-(1L << 24), wayland_min(a[1].i, 1L << 24));
                p->anchor_width = wayland_min(a[2].i, WAYLAND_DIMENSION);
                p->anchor_height = wayland_min(a[3].i, WAYLAND_DIMENSION);
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_anchor:
                if (a[0].u > WLN_xdg_positioner_anchor_bottom_right)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "no such anchor");
                p->anchor = (long)a[0].u;
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_gravity:
                if (a[0].u > WLN_xdg_positioner_gravity_bottom_right)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "no such gravity");
                p->gravity = (long)a[0].u;
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_constraint_adjustment:
                p->adjustment = (long)(a[0].u & 63);
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_offset:
                p->offset_x = wayland_max(-(1L << 24), wayland_min(a[0].i, 1L << 24));
                p->offset_y = wayland_max(-(1L << 24), wayland_min(a[1].i, 1L << 24));
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_reactive:
                p->reactive = 1;
                return WAYLAND_OK;
        default: // the parent's size and its configure, which only a reactive popup is moved by
                return WAYLAND_OK;
        }
}

static int wayland_wm_base_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;
        long slot = -1;

        switch (op)
        {
        case WLQ_xdg_wm_base_destroy:
                for (long at = 0; at < WAYLAND_OBJECTS; at++)
                        if (c->objects[at].iface == WLI_xdg_surface)
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_defunct_surfaces,
                                                    "xdg_surfaces still exist");
                return WAYLAND_OK;
        case WLQ_xdg_wm_base_create_positioner:
                for (long at = 0; at < WAYLAND_POSITIONERS && slot < 0; at++)
                        if (!c->positioners[at].live)
                                slot = at;
                if (slot < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many positioners");
                {
                        unsigned char *clear = (unsigned char *)&c->positioners[slot];

                        for (unsigned long at = 0; at < sizeof c->positioners[slot]; at++)
                                clear[at] = 0;
                }
                c->positioners[slot].live = 1;
                c->positioners[slot].id = c->objects[a[0].slot].id;
                c->objects[a[0].slot].link = (short)slot;
                return WAYLAND_OK;
        case WLQ_xdg_wm_base_get_xdg_surface:
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                if (s->flags & WAYLAND_SURFACE_XDG)
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has an xdg_surface");
                if (s->role != WAYLAND_ROLE_NONE && s->role != WAYLAND_ROLE_TOPLEVEL && s->role != WAYLAND_ROLE_POPUP)
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has another role");
                if (s->width || (s->attach_made && s->attached))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_surface_state,
                                            "the surface has a buffer");
                s->flags |= WAYLAND_SURFACE_XDG;
                s->xdg_id = c->objects[a[0].slot].id;
                c->objects[a[0].slot].link = (short)wayland_surface_index(c, s);
                c->objects[a[0].slot].aux = (unsigned short)s->gen;
                return WAYLAND_OK;
        default: // pong: the client is alive
                return WAYLAND_OK;
        }
}

static int wayland_xdg_surface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);
        struct wayland_surface *parent;
        long made;

        if (!s)
                return WAYLAND_OK; // its wl_surface is gone: the xdg_surface is inert, and only a destroy is left
        switch (op)
        {
        case WLQ_xdg_surface_destroy:
                if (s->role_id)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_defunct_role_object,
                                            "the role object still exists");
                s->flags &= ~(unsigned)(WAYLAND_SURFACE_XDG | WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT);
                s->xdg_id = 0;
                return WAYLAND_OK;
        case WLQ_xdg_surface_get_toplevel:
        case WLQ_xdg_surface_get_popup:
                if (s->role_id)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_already_constructed, "the xdg_surface has a role");
                if (s->role != WAYLAND_ROLE_NONE && s->role != (op == WLQ_xdg_surface_get_toplevel ? WAYLAND_ROLE_TOPLEVEL : WAYLAND_ROLE_POPUP))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has another role");
                if (s->width || (s->attach_made && s->attached))
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_unconfigured_buffer, "the surface has a buffer");
                made = a[0].slot;
                if (op == WLQ_xdg_surface_get_popup)
                {
                        struct wayland_positioner *p;

                        parent = a[1].slot >= 0 ? wayland_surface_of(c, a[1].slot) : 0;
                        if (a[1].slot < 0 || !parent || !(parent->flags & WAYLAND_SURFACE_XDG) || !parent->role_id ||
                            (parent->role != WAYLAND_ROLE_TOPLEVEL && parent->role != WAYLAND_ROLE_POPUP))
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_popup_parent,
                                                    "the popup's parent is not a window");
                        p = &c->positioners[c->objects[a[2].slot].link];
                        if (p->width < 1 || p->anchor_width < 1)
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_positioner,
                                                    "the positioner is not complete");
                        s->role = WAYLAND_ROLE_POPUP;
                        s->parent = (unsigned int)(wayland_surface_index(c, parent) + 1);
                        c->objects[made].link = (short)wayland_surface_index(c, s);
                        c->objects[made].aux = (unsigned short)s->gen;
                        s->role_id = c->objects[made].id;
                        wayland_popup_configure(c, s, p, parent);
                        return WAYLAND_OK;
                }
                s->role = WAYLAND_ROLE_TOPLEVEL;
                s->role_id = c->objects[made].id;
                s->title[0] = s->app_id[0] = 0;
                s->min_width = s->min_height = s->max_width = s->max_height = 0;
                c->objects[made].link = (short)wayland_surface_index(c, s);
                c->objects[made].aux = (unsigned short)s->gen;
                return WAYLAND_OK;
        case WLQ_xdg_surface_set_window_geometry:
                if (!s->role_id)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_not_constructed, "the xdg_surface has no role");
                if (a[2].i < 1 || a[3].i < 1)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_invalid_size, "the geometry must be positive");
                s->geometry_x = wayland_max(-(1L << 24), wayland_min(a[0].i, 1L << 24));
                s->geometry_y = wayland_max(-(1L << 24), wayland_min(a[1].i, 1L << 24));
                s->geometry_width = wayland_min(a[2].i, WAYLAND_DIMENSION);
                s->geometry_height = wayland_min(a[3].i, WAYLAND_DIMENSION);
                s->geometry_set = 1;
                return WAYLAND_OK;
        default: // ack_configure: one of the serials sent, and every one before it
                if (!s->role_id)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_not_constructed, "the xdg_surface has no role");
                for (long at = 0; at < s->serials; at++)
                        if (s->serial[at] == a[0].u)
                        {
                                for (long more = at + 1; more < s->serials; more++)
                                        s->serial[more - at - 1] = s->serial[more];
                                s->serials = (unsigned char)(s->serials - at - 1);
                                s->flags |= WAYLAND_SURFACE_CONFIGURED;
                                s->last_serial = (unsigned int)a[0].u;
                                return WAYLAND_OK;
                        }
                return wayland_fail(c, self->id, WLN_xdg_surface_error_invalid_serial, "the serial was not sent");
        }
}

/* Whether a toplevel is an ancestor of another, by its parent chain, which a loop of requests may not make. */
static int wayland_ancestor(struct wayland_client *c, long maybe, long of)
{
        for (long hops = 0; hops < WAYLAND_SURFACES && of; hops++)
        {
                if (of - 1 == maybe)
                        return 1;
                of = (long)c->surfaces[of - 1].parent;
        }
        return 0;
}

static int wayland_toplevel_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);
        long row;

        if (!s)
                return WAYLAND_OK;
        row = s->row;
        switch (op)
        {
        case WLQ_xdg_toplevel_destroy:
                if (s->decoration_id)
                        return wayland_fail(c, s->decoration_id, WLN_zxdg_toplevel_decoration_v1_error_orphaned,
                                            "the toplevel is destroyed before its decoration");
                wayland_unhold(c, s);
                wayland_row_release(c, s);
                s->role_id = 0;
                s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
                s->serials = 0;
                s->width = s->height = 0;
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_parent:
                if (a[0].slot < 0)
                {
                        s->parent = 0;
                        return WAYLAND_OK;
                }
                {
                        struct wayland_surface *p = wayland_surface_of(c, a[0].slot);

                        if (!p || p->role != WAYLAND_ROLE_TOPLEVEL || p == s ||
                            wayland_ancestor(c, wayland_surface_index(c, s), (long)wayland_surface_index(c, p) + 1))
                                return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_parent, "that parent makes a loop");
                        s->parent = (unsigned int)(wayland_surface_index(c, p) + 1);
                }
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_title:
                wayland_text(s->title, a[0].text, a[0].length);
                if (row >= 0)
                        wayland.host->row_title(wayland.host->context, row, s->title, 0);
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_app_id:
                wayland_text(s->app_id, a[0].text, a[0].length);
                if (row >= 0 && !s->title[0])
                        wayland.host->row_title(wayland.host->context, row, s->app_id, 0);
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_show_window_menu:
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_move:
                // Only for the press this client was last sent: a serial it was never given is no move (a compositor may ignore
                // it, and does), so a client cannot take a window by naming a number.
                if (row >= 0 && c->press_serial && a[1].u == c->press_serial)
                        wayland.host->row_do(wayland.host->context, (int)row, WAYLAND_DO_MOVE, (long)a[1].u, 0);
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_resize:
                if (a[2].u > WLN_xdg_toplevel_resize_edge_bottom_right || a[2].u == 3 || a[2].u == 7 || a[2].u == 11 || a[2].u == 12 ||
                    a[2].u == 13 || a[2].u == 14)
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_resize_edge, "no such edge");
                if (row >= 0 && a[2].u && c->press_serial && a[1].u == c->press_serial)
                        wayland.host->row_do(wayland.host->context, (int)row, WAYLAND_DO_RESIZE, (long)a[2].u, 0);
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_max_size:
                if (a[0].i < 0 || a[1].i < 0)
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_size, "a negative size");
                s->max_width = a[0].i;
                s->max_height = a[1].i;
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_min_size:
                if (a[0].i < 0 || a[1].i < 0)
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_size, "a negative size");
                s->min_width = a[0].i;
                s->min_height = a[1].i;
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_maximized:
        case WLQ_xdg_toplevel_unset_maximized:
        case WLQ_xdg_toplevel_set_fullscreen:
        case WLQ_xdg_toplevel_unset_fullscreen:
        case WLQ_xdg_toplevel_set_minimized:
        {
                long what = op == WLQ_xdg_toplevel_set_maximized     ? WAYLAND_DO_MAXIMIZE
                            : op == WLQ_xdg_toplevel_unset_maximized ? WAYLAND_DO_UNMAXIMIZE
                            : op == WLQ_xdg_toplevel_set_fullscreen  ? WAYLAND_DO_FULLSCREEN
                            : op == WLQ_xdg_toplevel_unset_fullscreen ? WAYLAND_DO_UNFULLSCREEN
                                                                       : WAYLAND_DO_MINIMIZE;

                // Asked before the window is shown, it is done when it is; asked after, at once.
                if (row >= 0)
                        wayland.host->row_do(wayland.host->context, (int)row, what, 0, 0);
                else if (what == WAYLAND_DO_MAXIMIZE)
                        s->want_maximized = 1;
                else if (what == WAYLAND_DO_UNMAXIMIZE)
                        s->want_maximized = 0;
                else if (what == WAYLAND_DO_FULLSCREEN)
                        s->want_fullscreen = 1;
                else if (what == WAYLAND_DO_UNFULLSCREEN)
                        s->want_fullscreen = 0;
                return WAYLAND_OK;
        }
        default:
                return WAYLAND_OK;
        }
}

static int wayland_popup_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);

        if (!s)
                return WAYLAND_OK;
        switch (op)
        {
        case WLQ_xdg_popup_destroy:
                // Popups close from the top: one that has a popup of its own open cannot go first.
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                        if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_POPUP && c->surfaces[at].role_id &&
                            c->surfaces[at].parent == (unsigned int)(wayland_surface_index(c, s) + 1))
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_not_the_topmost_popup,
                                                    "a popup is destroyed before the one over it");
                wayland_unhold(c, s);
                wayland_row_release(c, s);
                s->role_id = 0;
                s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
                s->serials = 0;
                s->width = s->height = 0;
                return WAYLAND_OK;
        case WLQ_xdg_popup_grab:
                if (s->flags & WAYLAND_SURFACE_MAPPED)
                        return wayland_fail(c, self->id, WLN_xdg_popup_error_invalid_grab, "the popup is already shown");
                s->grab = 1;
                return WAYLAND_OK;
        default: // reposition: a new place for the popup, and the token it was asked with
        {
                struct wayland_positioner *p = &c->positioners[c->objects[a[0].slot].link];
                struct wayland_surface *parent = s->parent ? &c->surfaces[s->parent - 1] : 0;
                struct wayland_event e = wayland_begin(c, self->id, WLE_xdg_popup_repositioned);

                wayland_word(&e, a[1].u);
                wayland_end(&e);
                wayland_popup_configure(c, s, p, parent);
                return WAYLAND_OK;
        }
        }
}

static int wayland_output_manager_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (op == WLQ_zxdg_output_manager_v1_get_xdg_output)
        {
                long slot = a[0].slot;

                c->objects[slot].aux = c->objects[a[1].slot].aux;
                wayland_xdg_output_send(c, slot, c->objects[slot].aux);
                if (c->objects[slot].version >= 3 && c->objects[a[1].slot].version >= 2)
                {
                        struct wayland_event e = wayland_begin(c, c->objects[a[1].slot].id, WLE_wl_output_done);

                        wayland_end(&e);
                }
        }
        (void)self;
        return WAYLAND_OK;
}

static int wayland_decoration_manager_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (op == WLQ_zxdg_decoration_manager_v1_destroy)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[1].slot);
        if (!s || s->role != WAYLAND_ROLE_TOPLEVEL)
                return wayland_fail(c, self->id, WLN_zxdg_toplevel_decoration_v1_error_orphaned, "the toplevel is gone");
        if (s->decoration_id)
                return wayland_fail(c, self->id, WLN_zxdg_toplevel_decoration_v1_error_already_constructed,
                                    "the toplevel has a decoration");
        if (s->width)
                return wayland_fail(c, self->id, WLN_zxdg_toplevel_decoration_v1_error_unconfigured_buffer,
                                    "the toplevel has a buffer");
        s->decoration_id = c->objects[a[0].slot].id;
        c->objects[a[0].slot].link = (short)wayland_surface_index(c, s);
        c->objects[a[0].slot].aux = (unsigned short)s->gen;
        if (s->flags & WAYLAND_SURFACE_SENT)
                wayland_configure(c, s, 0);
        return WAYLAND_OK;
}

static int wayland_decoration_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);

        if (op == WLQ_zxdg_toplevel_decoration_v1_destroy)
        {
                if (s)
                        s->decoration_id = 0;
                return WAYLAND_OK;
        }
        if (op == WLQ_zxdg_toplevel_decoration_v1_set_mode && a[0].u != WLN_zxdg_toplevel_decoration_v1_mode_client_side &&
            a[0].u != WLN_zxdg_toplevel_decoration_v1_mode_server_side)
                return wayland_fail(c, self->id, WLN_zxdg_toplevel_decoration_v1_error_invalid_mode, "no such mode");
        // Whatever mode is asked, the window is told the client's (wayland_decoration_send).
        if (s)
        {
                wayland_decoration_send(c, s);
                if (s->flags & WAYLAND_SURFACE_SENT)
                        wayland_configure(c, s, 0);
        }
        return WAYLAND_OK;
}

/* ---- the seat: the pointer, the keyboard and what they are told ---- */

/*
        Where the pointer and the keyboard are, as the clients are told of them: a row each, or none. The compositor
        decides both (the window under the pointer, the window with the keys) and says so; the shim sends the client
        that owns the row its enter and, to the one it left, its leave, to every pointer and keyboard that client has.
        A client's pointers, keyboards and touches are lists of ids kept as they are made and released, so an event
        finds them without a walk of the whole object table.
*/
static unsigned long wayland_fixed(long pixels, long scale120)
{
        return wayland_units_fixed(pixels, scale120);
}

/* The surface and client of a row, or 0. */
static struct wayland_surface *wayland_row_surface(long row, struct wayland_client **client)
{
        struct wayland_client *c;

        if (row < 0 || row >= WAYLAND_ROWS || wayland.row_client[row] < 0)
                return 0;
        c = wayland.client[wayland.row_client[row]];
        if (!c || !c->surfaces[wayland.row_surface[row]].live)
                return 0;
        *client = c;
        return &c->surfaces[wayland.row_surface[row]];
}

/* Whether a popup's window asked to grab the keys (a menu does; a tooltip does not). */
WAYLAND_API long wayland_row_grabs(long row)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);

        return s && s->grab;
}

/* A client's id list and count for one kind of seat object: pointer, keyboard or touch. */
static unsigned int *wayland_seat_ids(struct wayland_client *c, long iface, unsigned char **count)
{
        if (iface == WLI_wl_pointer)
        {
                *count = &c->pointers;
                return c->pointer_id;
        }
        if (iface == WLI_wl_touch)
        {
                *count = &c->touches;
                return c->touch_id;
        }
        *count = &c->keyboards;
        return c->keyboard_id;
}

/* The slots of a client's objects of a seat kind that are still there. */
static long wayland_seats(struct wayland_client *c, long iface, long *slots)
{
        unsigned char *count;
        const unsigned int *ids = wayland_seat_ids(c, iface, &count);
        long found = 0;

        for (long at = 0; at < *count; at++)
        {
                long slot = wayland_slot_of(c, ids[at]);

                if (slot >= 0 && c->objects[slot].iface == iface)
                        slots[found++] = slot;
        }
        return found;
}

/* A seat object made: its id is kept, up to the room; false for no room. */
static int wayland_seat_add(struct wayland_client *c, long iface, unsigned int id)
{
        unsigned char *count;
        unsigned int *ids = wayland_seat_ids(c, iface, &count);

        if (*count >= WAYLAND_SEATS)
                return 0;
        ids[(*count)++] = id;
        return 1;
}

static void wayland_seat_remove(struct wayland_client *c, long iface, unsigned int id)
{
        unsigned char *count;
        unsigned int *ids = wayland_seat_ids(c, iface, &count);

        for (long at = 0; at < *count; at++)
                if (ids[at] == id)
                {
                        ids[at] = ids[--(*count)];
                        return;
                }
}

static void wayland_pointer_frame(struct wayland_client *c, long slot)
{
        if (c->objects[slot].version >= 5)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_pointer_frame);

                wayland_end(&e);
        }
}

/*
        The surface of a window that holds a point of its picture, with the point in that surface's own pixels: the topmost
        subsurface that shows there and takes the pointer (an input region with nothing in it takes none), else the root. The
        later sibling is above the earlier and a child above what it hangs from.
*/
static struct wayland_surface *wayland_pick(struct wayland_client *c, struct wayland_surface *root, long x, long y, long *sx, long *sy)
{
        struct wayland_surface *best = root;
        long best_rank = -1, bx = 0, by = 0, wx, wy, ww, wh;

        wayland_window_rect(c, root, root->width, root->height, root->scale120, &wx, &wy, &ww, &wh);
        x += wx;
        y += wy;

        for (long at = 0; at < WAYLAND_SURFACES; at++)
        {
                struct wayland_surface *s = &c->surfaces[at];
                long ox, oy, depth = 0, rank;

                if (!s->live || s->role != WAYLAND_ROLE_SUBSURFACE || !s->held || s->input_none || wayland_root_of(c, s) != root)
                        continue;
                wayland_sub_offset(c, s, &ox, &oy);
                if (x < ox || y < oy || x >= ox + s->width || y >= oy + s->height)
                        continue;
                for (struct wayland_surface *up = s; up->role == WAYLAND_ROLE_SUBSURFACE && up->sub_parent && depth < 16;
                     up = &c->surfaces[up->sub_parent - 1])
                        depth++;
                rank = depth * 2000000 + s->sub_order + 1000000;
                if (rank > best_rank)
                {
                        best_rank = rank;
                        best = s;
                        bx = ox;
                        by = oy;
                }
        }
        *sx = x - bx;
        *sy = y - by;
        return best;
}

static void wayland_pointer_enter_one(struct wayland_client *c, long slot, struct wayland_surface *s, unsigned long serial, long sx, long sy)
{
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_pointer_enter);

        wayland_word(&e, serial);
        wayland_word(&e, s->id);
        wayland_word(&e, wayland_fixed(sx, s->scale120));
        wayland_word(&e, wayland_fixed(sy, s->scale120));
        wayland_end(&e);
        wayland_pointer_frame(c, slot);
}

static void wayland_pointer_leave_all(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned long serial = wayland_serial(c);
        long slots[WAYLAND_SEATS], count = wayland_seats(c, WLI_wl_pointer, slots);

        for (long at = 0; at < count; at++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[at]].id, WLE_wl_pointer_leave);

                wayland_word(&e, serial);
                wayland_word(&e, s->id);
                wayland_end(&e);
                wayland_pointer_frame(c, slots[at]);
        }
}

/*
        The pointer is over row (or none, -1), at (x, y) of its picture: the surface it leaves, a subsurface or a window's root,
        is sent its leave and the surface it enters its enter, each to every pointer its client has. The same surface is no event.
*/
WAYLAND_API void wayland_pointer_to(long row, long x, long y)
{
        struct wayland_client *old = 0, *now = 0;
        struct wayland_surface *root, *was = 0, *is = 0;
        long sx = 0, sy = 0;

        if (!wayland.host)
                return;
        root = wayland_row_surface(row, &now);
        if (root)
                is = wayland_pick(now, root, x, y, &sx, &sy);
        wayland.pointer_x = x;
        wayland.pointer_y = y;
        if (is && row == wayland.pointer_row && wayland_surface_index(now, is) == wayland.pointer_surface)
                return;
        if (wayland.pointer_row >= 0 && wayland_row_surface(wayland.pointer_row, &old) && wayland.pointer_surface >= 0 &&
            old->surfaces[wayland.pointer_surface].live)
                was = &old->surfaces[wayland.pointer_surface];
        if (was)
                wayland_pointer_leave_all(old, was);
        // The cursor shape a window's client asked for is its own while the pointer is on it: none on leave, none on enter
        // until the client asks again (the arrow is the compositor's).
        if (wayland.pointer_row >= 0 && wayland.pointer_row != (is ? row : -1))
                wayland.host->row_do(wayland.host->context, wayland.pointer_row, WAYLAND_DO_CURSOR, -1, 0);
        if (is)
                wayland.host->row_do(wayland.host->context, row, WAYLAND_DO_CURSOR, -1, 0);
        wayland.pointer_row = is ? row : -1;
        wayland.pointer_surface = is ? wayland_surface_index(now, is) : -1;
        wayland_constraints_update();
        if (is)
        {
                unsigned long serial = wayland_input_serial(now);
                long slots[WAYLAND_SEATS], count = wayland_seats(now, WLI_wl_pointer, slots);

                for (long at = 0; at < count; at++)
                        wayland_pointer_enter_one(now, slots[at], is, serial, sx, sy);
        }
}

WAYLAND_API void wayland_pointer_motion(long row, long x, long y)
{
        struct wayland_client *c;
        struct wayland_surface *root, *s;
        long time, slots[WAYLAND_SEATS], count, sx = 0, sy = 0, old_x = wayland.pointer_x, old_y = wayland.pointer_y;

        if (!wayland.host || row != wayland.pointer_row || !(root = wayland_row_surface(row, &c)))
                return;
        // A motion that crosses from one surface of the window to another is a leave and an enter first.
        wayland_pointer_to(row, x, y);
        if (wayland.pointer_row != row || wayland.pointer_surface < 0)
                return;
        s = wayland_pick(c, root, x, y, &sx, &sy);
        time = wayland.host->now(wayland.host->context);
        count = wayland_seats(c, WLI_wl_pointer, slots);
        for (long at = 0; at < count; at++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[at]].id, WLE_wl_pointer_motion);

                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, wayland_fixed(sx, s->scale120));
                wayland_word(&e, wayland_fixed(sy, s->scale120));
                wayland_end(&e);
                wayland_pointer_frame(c, slots[at]);
        }
        // The places the hand moved, to the client's relative pointers: as moved, the same accelerated or not.
        wayland_relative_send(c, s->scale120, x - old_x, y - old_y, x - old_x, y - old_y, time * 1000);
}

/*
        wl_touch. A finger that goes down on a window is sent to the surface under it, and its moves and its lift go to that
        surface whatever is under it then (the protocol's implicit grab). Every event is followed by a frame, which ends it.
        The window's coordinates come in as the window's picture (the row's), as the pointer's do, so the surface's own are
        the picture's less the subsurface's place, at the surface's scale.
*/
static int wayland_touch_find(long id)
{
        for (int at = 0; at < WAYLAND_TOUCHES; at++)
                if (wayland.touch[at].live && wayland.touch[at].id == id)
                        return at;
        return -1;
}

static void wayland_touch_frame_to(struct wayland_client *c, long slot)
{
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_touch_frame);

        wayland_end(&e);
}

/*
        Whether the window in row has a client that holds a wl_touch. A finger on such a window is the touch's: the compositor's
        pointer events for it (the kit's gesture turns a finger into a press and a motion) are not sent to that client, so it is
        told each finger once, as a touch, and not twice.
*/
WAYLAND_API long wayland_row_takes_touch(long row)
{
        struct wayland_client *c = 0;
        long slots[WAYLAND_SEATS];

        if (row < 0 || row >= WAYLAND_ROWS || !wayland_row_surface(row, &c))
                return 0;
        return wayland_seats(c, WLI_wl_touch, slots) > 0;
}

/* A finger went down on row at (x, y) of its picture; id is its tracking id, unique while it is down. */
WAYLAND_API void wayland_touch_down(long row, long id, long x, long y)
{
        struct wayland_client *c = 0;
        struct wayland_surface *root, *s;
        long sx = 0, sy = 0, time, slots[WAYLAND_SEATS], count, point = -1;
        unsigned long serial;

        if (!wayland.host || id < 0 || wayland_touch_find(id) >= 0 || !(root = wayland_row_surface(row, &c)))
                return;
        for (int at = 0; at < WAYLAND_TOUCHES && point < 0; at++)
                if (!wayland.touch[at].live)
                        point = at;
        count = wayland_seats(c, WLI_wl_touch, slots);
        if (point < 0 || count == 0 || !(s = wayland_pick(c, root, x, y, &sx, &sy)))
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        wayland.touch[point].live = 1;
        wayland.touch[point].client = c->slot;
        wayland.touch[point].row = (int)row;
        wayland.touch[point].surface = (int)wayland_surface_index(c, s);
        wayland.touch[point].id = id;
        wayland.touch[point].x = x;
        wayland.touch[point].y = y;
        for (long at = 0; at < count; at++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[at]].id, WLE_wl_touch_down);

                wayland_word(&e, serial);
                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, s->id);
                wayland_word(&e, (unsigned long)id);
                wayland_word(&e, wayland_fixed(sx, s->scale120));
                wayland_word(&e, wayland_fixed(sy, s->scale120));
                wayland_end(&e);
                wayland_touch_frame_to(c, slots[at]);
        }
}

/* The row a finger is down on, or -1 when it is not down: the shell gives a finger's place in that row's picture. */
WAYLAND_API long wayland_touch_row(long id)
{
        int at = wayland_touch_find(id);

        return at < 0 ? -1 : wayland.touch[at].row;
}

/* A finger that is down moved to (x, y) of its row's picture: a motion to its surface, unless it did not move. */
WAYLAND_API void wayland_touch_motion(long id, long x, long y)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s;
        long ox = 0, oy = 0, time, slots[WAYLAND_SEATS], count;
        int at = wayland_touch_find(id);

        if (!wayland.host || at < 0 || (wayland.touch[at].x == x && wayland.touch[at].y == y))
                return;
        if (!wayland_row_surface(wayland.touch[at].row, &c) || c->slot != wayland.touch[at].client)
                return;
        s = &c->surfaces[wayland.touch[at].surface];
        if (!s->live)
                return;
        wayland.touch[at].x = x;
        wayland.touch[at].y = y;
        if (s->role == WAYLAND_ROLE_SUBSURFACE)
                wayland_sub_offset(c, s, &ox, &oy);
        time = wayland.host->now(wayland.host->context);
        count = wayland_seats(c, WLI_wl_touch, slots);
        for (long n = 0; n < count; n++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[n]].id, WLE_wl_touch_motion);

                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, (unsigned long)id);
                wayland_word(&e, wayland_fixed(x - ox, s->scale120));
                wayland_word(&e, wayland_fixed(y - oy, s->scale120));
                wayland_end(&e);
                wayland_touch_frame_to(c, slots[n]);
        }
}

/* A finger that was down is lifted. */
WAYLAND_API void wayland_touch_up(long id)
{
        struct wayland_client *c;
        long time, slots[WAYLAND_SEATS], count;
        unsigned long serial;
        int at = wayland_touch_find(id);

        if (!wayland.host || at < 0)
                return;
        c = wayland.client[wayland.touch[at].client];
        wayland.touch[at].live = 0;
        if (!c)
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        count = wayland_seats(c, WLI_wl_touch, slots);
        for (long n = 0; n < count; n++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[n]].id, WLE_wl_touch_up);

                wayland_word(&e, serial);
                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, (unsigned long)id);
                wayland_end(&e);
                wayland_touch_frame_to(c, slots[n]);
        }
}

/*
        The fingers' sequence is taken away (the compositor's gesture took them, or the device lost its events): each client
        that had a finger down is sent a cancel, once, and none of its fingers is down any more.
*/
WAYLAND_API void wayland_touch_cancel(void)
{
        for (int at = 0; at < WAYLAND_TOUCHES; at++)
        {
                struct wayland_client *c;
                long slots[WAYLAND_SEATS], count;

                if (!wayland.touch[at].live)
                        continue;
                c = wayland.client[wayland.touch[at].client];
                for (int each = 0; each < WAYLAND_TOUCHES; each++)
                        if (wayland.touch[each].live && wayland.touch[each].client == wayland.touch[at].client)
                                wayland.touch[each].live = 0;
                if (!c)
                        continue;
                count = wayland_seats(c, WLI_wl_touch, slots);
                for (long n = 0; n < count; n++)
                {
                        struct wayland_event e = wayland_begin(c, c->objects[slots[n]].id, WLE_wl_touch_cancel);

                        wayland_end(&e);
                        wayland_touch_frame_to(c, slots[n]);
                }
        }
}

/* A client that goes has no finger down: its points are let go with it, and nothing is sent to it. */
static void wayland_touch_forget(long slot)
{
        for (int at = 0; at < WAYLAND_TOUCHES; at++)
                if (wayland.touch[at].live && wayland.touch[at].client == slot)
                        wayland.touch[at].live = 0;
}

/* A button of the pointer, by its Linux code, pressed or let go. A press is the serial a client may name to ask for a move. */
WAYLAND_API void wayland_pointer_button(long row, long code, long pressed)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        unsigned long serial;
        long time, slots[WAYLAND_SEATS], count;

        if (!wayland.host || row != wayland.pointer_row || !(s = wayland_row_surface(row, &c)) || code < 0 || code > 0x2ff)
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        if (pressed)
                c->press_serial = (unsigned int)serial;
        count = wayland_seats(c, WLI_wl_pointer, slots);
        for (long at = 0; at < count; at++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[at]].id, WLE_wl_pointer_button);

                wayland_word(&e, serial);
                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, (unsigned long)code);
                wayland_word(&e, pressed ? WLN_wl_pointer_button_state_pressed : WLN_wl_pointer_button_state_released);
                wayland_end(&e);
                wayland_pointer_frame(c, slots[at]);
        }
        (void)s;
}

/*
        The wheel or two fingers: v120 is in hundred-and-twentieths of a notch, a notch being 15 of the surface's units
        as every toolkit takes it, and horizontal says which axis. A wheel reports what version eight has for it
        (value120), what earlier ones had (discrete steps, the remainder kept for the next), and a finger's motion is a
        smooth value of the same size from a finger.
*/
WAYLAND_API void wayland_pointer_axis(long row, long horizontal, long v120, long precise)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        long time, slots[WAYLAND_SEATS], count, axis = horizontal ? WLN_wl_pointer_axis_horizontal_scroll : WLN_wl_pointer_axis_vertical_scroll;

        if (!wayland.host || row != wayland.pointer_row || !(s = wayland_row_surface(row, &c)) || !v120)
                return;
        if (precise)
                c->finger_scroll = 1;
        time = wayland.host->now(wayland.host->context);
        count = wayland_seats(c, WLI_wl_pointer, slots);
        for (long at = 0; at < count; at++)
        {
                struct wayland_object *o = &c->objects[slots[at]];
                struct wayland_event e;

                if (o->version >= 5)
                {
                        e = wayland_begin(c, o->id, WLE_wl_pointer_axis_source);
                        wayland_word(&e, precise ? WLN_wl_pointer_axis_source_finger : WLN_wl_pointer_axis_source_wheel);
                        wayland_end(&e);
                }
                if (o->version >= 9)
                {
                        // The direction of the axis against the fingers or the wheel: a scroll is as it was given, not inverted.
                        e = wayland_begin(c, o->id, WLE_wl_pointer_axis_relative_direction);
                        wayland_word(&e, (unsigned long)axis);
                        wayland_word(&e, WLN_wl_pointer_axis_relative_direction_identical);
                        wayland_end(&e);
                }
                if (!precise && o->version >= 8)
                {
                        e = wayland_begin(c, o->id, WLE_wl_pointer_axis_value120);
                        wayland_word(&e, (unsigned long)axis);
                        wayland_word(&e, (unsigned long)v120);
                        wayland_end(&e);
                }
                else if (!precise && o->version >= 5)
                {
                        long notches;

                        c->axis_left[horizontal ? 1 : 0] += v120;
                        notches = c->axis_left[horizontal ? 1 : 0] / 120;
                        c->axis_left[horizontal ? 1 : 0] -= notches * 120;
                        if (notches)
                        {
                                e = wayland_begin(c, o->id, WLE_wl_pointer_axis_discrete);
                                wayland_word(&e, (unsigned long)axis);
                                wayland_word(&e, (unsigned long)notches);
                                wayland_end(&e);
                        }
                }
                e = wayland_begin(c, o->id, WLE_wl_pointer_axis);
                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, (unsigned long)axis);
                wayland_word(&e, (unsigned long)(v120 * 15 * 256 / 120));
                wayland_end(&e);
                wayland_pointer_frame(c, slots[at]);
        }
        (void)s;
}

/*
        The fingers lifted off a pad that scrolled (the pad's last finger, WINDOW_WHEEL_STOP): each pointer of the client is sent
        axis_stop on both axes (from version 5), which is what a toolkit ends kinetic scrolling on. Only a client that had a finger
        scroll and no stop since is sent one; a wheel's scroll has none.
*/
WAYLAND_API void wayland_pointer_axis_stop(long row)
{
        struct wayland_client *c;
        long time, slots[WAYLAND_SEATS], count;

        if (!wayland.host || row != wayland.pointer_row || !wayland_row_surface(row, &c) || !c->finger_scroll)
                return;
        c->finger_scroll = 0;
        time = wayland.host->now(wayland.host->context);
        count = wayland_seats(c, WLI_wl_pointer, slots);
        for (long at = 0; at < count; at++)
                for (long axis = 0; axis < 2 && c->objects[slots[at]].version >= 5; axis++)
                {
                        struct wayland_event e = wayland_begin(c, c->objects[slots[at]].id, WLE_wl_pointer_axis_stop);

                        wayland_word(&e, (unsigned long)time);
                        wayland_word(&e, axis ? WLN_wl_pointer_axis_horizontal_scroll : WLN_wl_pointer_axis_vertical_scroll);
                        wayland_end(&e);
                        wayland_pointer_frame(c, slots[at]);
                }
}

/* How fast a held key repeats, to one keyboard (from version 4): the rate a second and the delay in milliseconds. */
static void wayland_keyboard_repeat(struct wayland_client *c, long slot)
{
        struct wayland_event e;
        long rate = 25, delay = 600;

        if (c->objects[slot].version < 4)
                return;
        wayland.host->repeat(wayland.host->context, &rate, &delay);
        e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_repeat_info);
        wayland_word(&e, (unsigned long)rate);
        wayland_word(&e, (unsigned long)delay);
        wayland_end(&e);
}

/* The keymap to one keyboard: a descriptor beside the event, and how fast a held key repeats. */
static void wayland_keyboard_keymap(struct wayland_client *c, long slot)
{
        struct wayland_event e;
        long size = 0, fd = wayland.host->keymap(wayland.host->context, &size);

        e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_keymap);
        if (fd >= 0 && size > 0 && size <= WAYLAND_KEYMAP_MAX && c->out_fd_count < WAYLAND_OUT_FDS)
        {
                wayland_word(&e, WLN_wl_keyboard_keymap_format_xkb_v1);
                wayland_word(&e, (unsigned long)size);
                c->out_fd_owned[c->out_fd_count] = 0;
                c->out_fds[c->out_fd_count] = fd;
                c->out_fd_at[c->out_fd_count++] = e.start;
        }
        else
        {
                wayland_word(&e, WLN_wl_keyboard_keymap_format_no_keymap);
                wayland_word(&e, 0);
        }
        wayland_end(&e);
        wayland_keyboard_repeat(c, slot);
}

/*
        The modifiers a keymap built by the compositor reads, from the keys held: Shift, Control, Alt on Mod1, the logo keys
        on Mod4 and AltGr on Mod5, the keymap's own masks (Shift 1, Lock 2, Control 4, Mod1 8, Mod2 16, Mod4 64, Mod5 128).
        The locked ones, Caps Lock and Num Lock, are the compositor's to say.
*/
static unsigned long wayland_depressed(void)
{
        unsigned long mods = 0;

#define WAYLAND_DOWN(code) ((wayland.keys[(code) >> 3] >> ((code) & 7)) & 1)
        if (WAYLAND_DOWN(42) || WAYLAND_DOWN(54))
                mods |= 1;
        if (WAYLAND_DOWN(29) || WAYLAND_DOWN(97))
                mods |= 4;
        if (WAYLAND_DOWN(56))
                mods |= 8;
        if (WAYLAND_DOWN(125) || WAYLAND_DOWN(126))
                mods |= 64;
        if (WAYLAND_DOWN(100))
                mods |= 128;
#undef WAYLAND_DOWN
        return mods;
}

static void wayland_modifiers_to(struct wayland_client *c, long slot)
{
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_modifiers);

        wayland_word(&e, wayland_serial(c));
        wayland_word(&e, (unsigned long)wayland.mods[0]);
        wayland_word(&e, (unsigned long)wayland.mods[1]);
        wayland_word(&e, (unsigned long)wayland.mods[2]);
        wayland_word(&e, (unsigned long)wayland.mods[3]);
        wayland_end(&e);
}

static void wayland_keyboard_enter_one(struct wayland_client *c, long slot, struct wayland_surface *s, unsigned long serial)
{
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_enter);
        long count = 0;

        wayland_word(&e, serial);
        wayland_word(&e, s->id);
        for (long key = 0; key < 128; key++)
                count += (wayland.keys[key >> 3] >> (key & 7)) & 1;
        wayland_word(&e, (unsigned long)count * 4);
        for (long key = 0; key < 128; key++)
                if ((wayland.keys[key >> 3] >> (key & 7)) & 1)
                        wayland_word(&e, (unsigned long)key);
        wayland_end(&e);
        wayland_modifiers_to(c, slot);
}

static void wayland_selections_to(struct wayland_client *c);

/* The keys go to row (or to none, -1): the client it leaves is sent its leave, the one it enters its enter with the keys held. */
static void wayland_shortcuts_send(struct wayland_client *c, struct wayland_surface *s, int active);

WAYLAND_API void wayland_keyboard_to(long row)
{
        struct wayland_client *old = 0, *now = 0;
        struct wayland_surface *was, *is;

        if (!wayland.host || row == wayland.keyboard_row)
                return;
        was = wayland_row_surface(wayland.keyboard_row, &old);
        is = wayland_row_surface(row, &now);
        if (was)
        {
                unsigned long serial = wayland_serial(old);
                long slots[WAYLAND_SEATS], count = wayland_seats(old, WLI_wl_keyboard, slots);

                for (long at = 0; at < count; at++)
                {
                        struct wayland_event e = wayland_begin(old, old->objects[slots[at]].id, WLE_wl_keyboard_leave);

                        wayland_word(&e, serial);
                        wayland_word(&e, was->id);
                        wayland_end(&e);
                }
        }
        wayland.keyboard_row = is ? row : -1;
        // A shortcuts inhibitor is active while its window has the keys: the one that lost them is sent inactive, the one that got them active.
        if (was && old && (was->shortcuts || was->xgrab) && was->shortcut_active)
                wayland_shortcuts_send(old, was, 0);
        if (is && now && (is->shortcuts || is->xgrab) && !is->shortcut_active)
                wayland_shortcuts_send(now, is, 1);
        wayland_constraints_update();
        if (is)
        {
                unsigned long serial = wayland_input_serial(now);
                long slots[WAYLAND_SEATS], count = wayland_seats(now, WLI_wl_keyboard, slots);

                // A window is entered with no key held and the locks the compositor says: whatever was held went with the window
                // that had the keys, whose release the compositor sent there.
                for (long key = 0; key < 16; key++)
                        wayland.keys[key] = 0;
                wayland.mods[0] = wayland.mods[1] = wayland.mods[3] = 0;
                wayland.mods[2] = (unsigned long)wayland.host->locks(wayland.host->context);
                for (long at = 0; at < count; at++)
                        wayland_keyboard_enter_one(now, slots[at], is, serial);
                wayland_selections_to(now);
        }
}

/*
        A key of the keyboard, by its Linux code, pressed or let go, to the window that has the keys. A repeat is not sent:
        the client repeats the key itself at the rate it was told, so a held key costs this nothing.
*/
WAYLAND_API void wayland_key(long row, long code, long pressed)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        unsigned long serial;
        long time, slots[WAYLAND_SEATS], count;

        if (!wayland.host || code < 1 || code > 0x2ff)
                return;
        if (code < 128)
        {
                if (pressed)
                        wayland.keys[code >> 3] |= (unsigned char)(1u << (code & 7));
                else
                        wayland.keys[code >> 3] &= (unsigned char)~(1u << (code & 7));
        }
        if (row != wayland.keyboard_row || !(s = wayland_row_surface(row, &c)))
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        count = wayland_seats(c, WLI_wl_keyboard, slots);
        for (long at = 0; at < count; at++)
        {
                struct wayland_event e = wayland_begin(c, c->objects[slots[at]].id, WLE_wl_keyboard_key);

                wayland_word(&e, serial);
                wayland_word(&e, (unsigned long)time);
                wayland_word(&e, (unsigned long)code);
                wayland_word(&e, pressed ? WLN_wl_keyboard_key_state_pressed : WLN_wl_keyboard_key_state_released);
                wayland_end(&e);
        }
        // The modifiers after the key that changed them, to each keyboard, when they did.
        {
                unsigned long depressed = wayland_depressed(), locked = (unsigned long)wayland.host->locks(wayland.host->context);

                if (depressed != wayland.mods[0] || locked != wayland.mods[2])
                {
                        wayland.mods[0] = depressed;
                        wayland.mods[2] = locked;
                        for (long at = 0; at < count; at++)
                                wayland_modifiers_to(c, slots[at]);
                }
        }
        (void)s;
}

/* The repeat rate or delay changed (the settings): each keyboard is told, and nothing else of it. */
WAYLAND_API void wayland_repeat_changed(void)
{
        if (!wayland.host)
                return;
        for (long client = 0; client < WAYLAND_CLIENTS; client++)
        {
                struct wayland_client *c = wayland.client[client];
                long slots[WAYLAND_SEATS], count = c ? wayland_seats(c, WLI_wl_keyboard, slots) : 0;

                for (long at = 0; at < count; at++)
                        wayland_keyboard_repeat(c, slots[at]);
        }
}

/* The keymap changed (a layout was set): each keyboard is sent the new one, and the modifiers. */
WAYLAND_API void wayland_keymap_changed(void)
{
        if (!wayland.host)
                return;
        for (long client = 0; client < WAYLAND_CLIENTS; client++)
        {
                struct wayland_client *c = wayland.client[client];
                long slots[WAYLAND_SEATS], count = c ? wayland_seats(c, WLI_wl_keyboard, slots) : 0;

                for (long at = 0; at < count; at++)
                {
                        wayland_keyboard_keymap(c, slots[at]);
                        wayland_modifiers_to(c, slots[at]);
                }
        }
}

/*
        wl_seat: a pointer or a keyboard is made, kept in the client's list, and told where things are now (the keyboard
        its keymap, and its enter if the keys are this client's already). Touch is not offered until the seat has it.
*/
static int wayland_seat_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long slot = a ? a[0].slot : -1;

        switch (op)
        {
        case WLQ_wl_seat_get_pointer:
                if (!wayland_seat_add(c, WLI_wl_pointer, c->objects[slot].id))
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many pointers");
                if (wayland.pointer_row >= 0 && wayland.row_client[wayland.pointer_row] == c->slot && wayland.pointer_surface >= 0)
                {
                        struct wayland_client *owner;
                        struct wayland_surface *root = wayland_row_surface(wayland.pointer_row, &owner), *s = 0;
                        long sx = 0, sy = 0;

                        if (root && c->surfaces[wayland.pointer_surface].live)
                        {
                                s = wayland_pick(c, root, wayland.pointer_x, wayland.pointer_y, &sx, &sy);
                                wayland_pointer_enter_one(c, slot, s, wayland_serial(c), sx, sy);
                        }
                }
                return WAYLAND_OK;
        case WLQ_wl_seat_get_keyboard:
                if (!wayland_seat_add(c, WLI_wl_keyboard, c->objects[slot].id))
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many keyboards");
                wayland_keyboard_keymap(c, slot);
                if (wayland.keyboard_row >= 0 && wayland.row_client[wayland.keyboard_row] == c->slot)
                {
                        struct wayland_client *owner;
                        struct wayland_surface *s = wayland_row_surface(wayland.keyboard_row, &owner);

                        if (s)
                                wayland_keyboard_enter_one(c, slot, s, wayland_serial(c));
                }
                return WAYLAND_OK;
        case WLQ_wl_seat_get_touch:
                // A seat with no touch screen has no touch to give: the client is refused, as the protocol says.
                if (!(wayland.host->seat(wayland.host->context) & WLN_wl_seat_capability_touch))
                        return wayland_fail(c, self->id, WLN_wl_seat_error_missing_capability, "the seat has no touch");
                if (!wayland_seat_add(c, WLI_wl_touch, c->objects[slot].id))
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many touches");
                return WAYLAND_OK;
        default:
                return WAYLAND_OK;
        }
}

/* A pointer or a keyboard let go: out of its client's list. A pointer's set_cursor is taken and its image not yet shown. */
static int wayland_seat_object_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        // wl_pointer.set_cursor (serial, surface, hotspot): the serial must be a recent input of the client's, and only the client
        // with the pointer may set its cursor. No surface hides the pointer over its window. A surface is not drawn yet: its window
        // gets the arrow, the compositor's own, rather than the client's picture (a gap in the protocol, not a fake of it).
        if (op == WLQ_wl_pointer_set_cursor && self->iface == WLI_wl_pointer && wayland.pointer_row >= 0 &&
            wayland.row_client[wayland.pointer_row] == c->slot && wayland_input_serial_ok(c, a[0].u))
                wayland.host->row_do(wayland.host->context, wayland.pointer_row, WAYLAND_DO_CURSOR,
                                     a[1].slot < 0 ? WAYLAND_CURSOR_HIDDEN : WAYLAND_CURSOR_ARROW, 0);
        if (op == WLQ_wl_pointer_release && self->iface == WLI_wl_pointer)
                wayland_seat_remove(c, WLI_wl_pointer, self->id);
        else if (op == WLQ_wl_keyboard_release && self->iface == WLI_wl_keyboard)
                wayland_seat_remove(c, WLI_wl_keyboard, self->id);
        else if (op == WLQ_wl_touch_release && self->iface == WLI_wl_touch)
                wayland_seat_remove(c, WLI_wl_touch, self->id);
        return WAYLAND_OK;
}

/* ---- the clipboard ---- */

/*
        A client's copy and paste are the compositor's: it holds one text for each selection (the clipboard and the primary
        selection), whoever made it, and what a client is shown of it is made from that text. A client's copy (set_selection
        with a source) is read once, into the compositor's: the host gives the shim a descriptor to hand the source in a send
        event and reads what comes, and the text is the selection when the source has ended it, so it outlives its source and
        its client, as a copy made in any window of the compositor does. A paste is an offer of text made to the client that
        has the keys, when it gets them and when the text changes, and a receive on an offer that is still the selection's is the
        host writing the text to the descriptor the client gave. Nothing but text is held, so a source that offers no text is
        cancelled; a copy is taken only for a client the person has just touched, and a paste is given only to the window that
        has the keys with the person's key or button just on it (the host's rule, as for a program of the compositor's).
        Dragging is not done: a drag that starts is cancelled.
*/
static const struct
{
        const char *name;
        unsigned char bit;
} wayland_mimes[] = {{"text/plain;charset=utf-8", 1}, {"UTF8_STRING", 2}, {"text/plain", 4}};
#define WAYLAND_MIME_COUNT 3

// The bit of a type that is text we take (either case), or 0.
static long wayland_mime_bit(const unsigned char *text, long length)
{
        for (long at = 0; at < WAYLAND_MIME_COUNT; at++)
        {
                long same = 1, n = 0;

                for (; wayland_mimes[at].name[n]; n++)
                {
                        unsigned char a = n < length ? text[n] : 0, b = (unsigned char)wayland_mimes[at].name[n];

                        a = a >= 'A' && a <= 'Z' ? (unsigned char)(a + 32) : a;
                        b = b >= 'A' && b <= 'Z' ? (unsigned char)(b + 32) : b;
                        if (a != b)
                        {
                                same = 0;
                                break;
                        }
                }
                if (same && n == length)
                        return wayland_mimes[at].bit;
        }
        return 0;
}

// The type to ask a source for, of those it offers: the UTF-8 one first.
static const char *wayland_mime_choice(unsigned int mimes)
{
        for (long at = 0; at < WAYLAND_MIME_COUNT; at++)
                if (mimes & wayland_mimes[at].bit)
                        return wayland_mimes[at].name;
        return 0;
}

/* The slots of a client's devices for a selection that are still there. */
static long wayland_devices(struct wayland_client *c, long which, long *slots)
{
        long found = 0, iface = which ? WLI_zwp_primary_selection_device_v1 : WLI_wl_data_device;

        for (long at = 0; at < c->devices[which]; at++)
        {
                long slot = wayland_slot_of(c, c->device_id[which][at]);

                if (slot >= 0 && c->objects[slot].iface == iface)
                        slots[found++] = slot;
        }
        return found;
}

static void wayland_device_remove(struct wayland_client *c, long which, unsigned int id)
{
        for (long at = 0; at < c->devices[which]; at++)
                if (c->device_id[which][at] == id)
                {
                        c->device_id[which][at] = c->device_id[which][--c->devices[which]];
                        return;
                }
}

// An id of the server's own that no object of the client has now, taken in turn so a number just let go is not the next.
static unsigned long wayland_server_id(struct wayland_client *c)
{
        for (long step = 0; step < WAYLAND_SERVER_IDS; step++)
        {
                long at = (c->server_hint + step) % WAYLAND_SERVER_IDS;

                if (!c->server_index[at])
                {
                        c->server_hint = (unsigned char)((at + 1) % WAYLAND_SERVER_IDS);
                        return WAYLAND_SERVER_BASE + (unsigned long)at;
                }
        }
        return 0;
}

/*
        The text the compositor holds for a selection, to one of a client's devices: a new offer of it with its types, then the
        selection naming the offer; or the selection with none when there is nothing, or no room for an offer (a client that
        keeps sixteen and lets none go is not offered more).
*/
static void wayland_selection_to(struct wayland_client *c, long slot, long which)
{
        unsigned long device = c->objects[slot].id, id = 0;
        struct wayland_event e;
        long index = -1, offer = -1;

        if (wayland.host->clip_size(wayland.host->context, which) > 0)
        {
                for (long at = 0; at < WAYLAND_OFFERS && index < 0; at++)
                        if (!c->offers[at].live)
                                index = at;
                id = index >= 0 ? wayland_server_id(c) : 0;
                if (id)
                        offer = wayland_object_new(c, id, which ? WLI_zwp_primary_selection_offer_v1 : WLI_wl_data_offer,
                                                   c->objects[slot].version);
        }
        if (offer >= 0)
        {
                c->offers[index].id = (unsigned int)id;
                c->offers[index].live = 1;
                c->offers[index].which = (unsigned char)which;
                c->offers[index].serial = wayland.selection_serial[which];
                c->objects[offer].link = (short)index;
                e = wayland_begin(c, device, which ? WLE_zwp_primary_selection_device_v1_data_offer : WLE_wl_data_device_data_offer);
                wayland_word(&e, id);
                wayland_end(&e);
                for (long at = 0; at < WAYLAND_MIME_COUNT; at++)
                {
                        e = wayland_begin(c, id, which ? WLE_zwp_primary_selection_offer_v1_offer : WLE_wl_data_offer_offer);
                        wayland_string(&e, wayland_mimes[at].name);
                        wayland_end(&e);
                }
        }
        else
                id = 0;
        e = wayland_begin(c, device, which ? WLE_zwp_primary_selection_device_v1_selection : WLE_wl_data_device_selection);
        wayland_word(&e, id);
        wayland_end(&e);
}

// Both selections to a client's devices: what it is shown when it gets the keys.
static void wayland_selections_to(struct wayland_client *c)
{
        for (long which = 0; which < 2; which++)
        {
                long slots[WAYLAND_DEVICES], count = wayland_devices(c, which, slots);

                for (long at = 0; at < count; at++)
                        wayland_selection_to(c, slots[at], which);
        }
}

// A source told it is not wanted: its copy is not taken, or the text is another's now. It is let go but for its object.
static void wayland_source_cancel(struct wayland_client *c, long index)
{
        struct wayland_source *source = &c->sources[index];
        struct wayland_event e;

        source->state = 3;
        if (wayland_slot_of(c, source->id) < 0)
                return;
        e = wayland_begin(c, source->id, source->which ? WLE_zwp_primary_selection_source_v1_cancelled : WLE_wl_data_source_cancelled);
        wayland_end(&e);
}

// A source object destroyed: a copy of it still being read is ended, and the selection stays what it was but is nobody's.
static void wayland_source_drop(struct wayland_client *c, long index)
{
        struct wayland_source *source = &c->sources[index];
        long which = source->which;

        if (source->state == 1 && c->pending[which] == index + 1)
        {
                c->pending[which] = 0;
                wayland.host->clip_abort(wayland.host->context, c->slot, which);
        }
        if (wayland.selection_owner[which] == c->slot + 1 && wayland.selection_source[which] == index)
                wayland.selection_owner[which] = 0;
        source->live = 0;
}

/*
        The compositor's text for a selection is new (or gone): the source of a client that was the selection is told it is not,
        the count that tells an old offer from a current one moves, and the client that has the keys is offered the new text.
*/
WAYLAND_API void wayland_selection_changed(long which)
{
        struct wayland_client *c;
        long slots[WAYLAND_DEVICES], count;

        if (!wayland.host || which < 0 || which > 1)
                return;
        wayland.selection_serial[which]++;
        if (wayland.selection_owner[which] > 0 && (c = wayland.client[wayland.selection_owner[which] - 1]) &&
            c->sources[wayland.selection_source[which]].live && c->sources[wayland.selection_source[which]].state == 2)
                wayland_source_cancel(c, wayland.selection_source[which]);
        wayland.selection_owner[which] = 0;
        if (wayland.keyboard_row < 0 || wayland.row_client[wayland.keyboard_row] < 0)
                return;
        c = wayland.client[wayland.row_client[wayland.keyboard_row]];
        count = c ? wayland_devices(c, which, slots) : 0;
        for (long at = 0; at < count; at++)
                wayland_selection_to(c, slots[at], which);
}

/*
        The host has read a client's source to its end (ok), or could not: the source is the selection, or is cancelled. The
        host says it after it has made the text the selection, so the text's change has been told by then.
*/
WAYLAND_API void wayland_clip_pulled(long client, long which, long ok)
{
        struct wayland_client *c = client >= 0 && client < WAYLAND_CLIENTS && wayland.host ? wayland.client[client] : 0;
        long index;

        if (!c || which < 0 || which > 1 || !c->pending[which])
                return;
        index = c->pending[which] - 1;
        c->pending[which] = 0;
        if (!c->sources[index].live || c->sources[index].state != 1)
                return;
        if (!ok)
        {
                wayland_source_cancel(c, index);
                return;
        }
        c->sources[index].state = 2;
        wayland.selection_owner[which] = c->slot + 1;
        wayland.selection_source[which] = index;
}

// A source made: a record, and the object tied to it.
static int wayland_source_new(struct wayland_client *c, struct wayland_object *self, long which, long slot)
{
        long index = -1;

        for (long at = 0; at < WAYLAND_SOURCES && index < 0; at++)
                if (!c->sources[at].live)
                        index = at;
        if (index < 0)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many data sources");
        c->sources[index].id = c->objects[slot].id;
        c->sources[index].live = 1;
        c->sources[index].which = (unsigned char)which;
        c->sources[index].mimes = 0;
        c->sources[index].state = 0;
        c->objects[slot].link = (short)index;
        return WAYLAND_OK;
}

// A device made: kept in the client's list, and shown the selection at once when the client has the keys already.
static int wayland_device_new(struct wayland_client *c, struct wayland_object *self, long which, long slot)
{
        if (c->devices[which] >= WAYLAND_DEVICES)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many data devices");
        c->device_id[which][c->devices[which]++] = c->objects[slot].id;
        if (wayland.keyboard_row >= 0 && wayland.row_client[wayland.keyboard_row] == c->slot)
                wayland_selection_to(c, slot, which);
        return WAYLAND_OK;
}

/*
        set_selection: a source (or none) and the serial of an input event of this client's. None clears the text when this
        client's source is the selection. A source is used once; one made from an event the client was not sent, one that offers
        no text, one the host will not take (no gesture of the person's, the service off, no room) is cancelled and nothing else
        happens. Otherwise the host is given the source to read, and it is the selection when it has been.
*/
static int wayland_set_selection(struct wayland_client *c, struct wayland_object *self, long which, const struct wayland_arg *a)
{
        long index = a[0].slot >= 0 ? c->objects[a[0].slot].link : -1, fd;
        struct wayland_source *source;
        struct wayland_event e;
        const char *mime;

        if (index < 0)
        {
                if (wayland.selection_owner[which] == c->slot + 1)
                        wayland.host->clip_clear(wayland.host->context, c->slot, which);
                return WAYLAND_OK;
        }
        source = &c->sources[index];
        // The source that is being read, or is the selection, set again (a toolkit does after adding a type) is no change.
        if (source->state == 1 || source->state == 2)
                return WAYLAND_OK;
        if (source->state != 0)
                return wayland_fail(c, self->id, WLN_wl_data_device_error_used_source, "the source was used before");
        mime = wayland_mime_choice(source->mimes);
        if (!wayland_input_serial_ok(c, a[1].u) || !mime || c->out_fd_count >= WAYLAND_OUT_FDS)
        {
                wayland_source_cancel(c, index);
                return WAYLAND_OK;
        }
        if (c->pending[which])
        {
                wayland.host->clip_abort(wayland.host->context, c->slot, which);
                wayland_source_cancel(c, c->pending[which] - 1);
                c->pending[which] = 0;
        }
        fd = wayland.host->clip_pull(wayland.host->context, c->slot, which);
        if (fd < 0)
        {
                wayland_source_cancel(c, index);
                return WAYLAND_OK;
        }
        source->state = 1;
        c->pending[which] = (unsigned char)(index + 1);
        e = wayland_begin(c, source->id, which ? WLE_zwp_primary_selection_source_v1_send : WLE_wl_data_source_send);
        wayland_string(&e, mime);
        // The shim's from here (0976821d): the host that ends the copy before this is sent has nothing to close.
        c->out_fd_owned[c->out_fd_count] = 1;
        c->out_fds[c->out_fd_count] = fd;
        c->out_fd_at[c->out_fd_count++] = e.start;
        wayland.fds++;
        wayland_end(&e);
        return WAYLAND_OK;
}

// receive: the text of an offer that is still the selection's, written to the descriptor, for the client that has the keys.
static int wayland_receive(struct wayland_client *c, struct wayland_object *self, struct wayland_arg *a)
{
        struct wayland_offer *offer = &c->offers[self->link];

        if (wayland_mime_bit(a[0].text, a[0].length) && offer->serial == wayland.selection_serial[offer->which] &&
            wayland.keyboard_row >= 0 && wayland.row_client[wayland.keyboard_row] == c->slot)
        {
                wayland.host->clip_push(wayland.host->context, wayland.keyboard_row, offer->which, a[1].fd);
                wayland.fds--;
                a[1].fd = -1;
        }
        return WAYLAND_OK;
}

static int wayland_data_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        switch (self->iface)
        {
        case WLI_wl_data_device_manager:
                if (op == WLQ_wl_data_device_manager_create_data_source)
                        return wayland_source_new(c, self, 0, a[0].slot);
                return op == WLQ_wl_data_device_manager_get_data_device ? wayland_device_new(c, self, 0, a[0].slot) : WAYLAND_OK;
        case WLI_zwp_primary_selection_device_manager_v1:
                if (op == WLQ_zwp_primary_selection_device_manager_v1_create_source)
                        return wayland_source_new(c, self, 1, a[0].slot);
                return op == WLQ_zwp_primary_selection_device_manager_v1_get_device ? wayland_device_new(c, self, 1, a[0].slot) : WAYLAND_OK;
        case WLI_wl_data_source:
        case WLI_zwp_primary_selection_source_v1:
                if (op == WLQ_wl_data_source_offer && c->sources[self->link].state == 0)
                        c->sources[self->link].mimes |= (unsigned char)wayland_mime_bit(a[0].text, a[0].length);
                else if (op == WLQ_wl_data_source_destroy)
                        wayland_source_drop(c, self->link);
                else if (op == WLQ_wl_data_source_set_actions && (a[0].u & ~7UL))
                        return wayland_fail(c, self->id, WLN_wl_data_source_error_invalid_action_mask, "the actions are not a mask of three");
                return WAYLAND_OK;
        case WLI_wl_data_device:
                if (op == WLQ_wl_data_device_set_selection)
                        return wayland_set_selection(c, self, 0, a);
                if (op == WLQ_wl_data_device_start_drag)
                {
                        // Not done: the source of a drag is told it is not going.
                        if (a[0].slot >= 0 && c->sources[c->objects[a[0].slot].link].state == 0)
                                wayland_source_cancel(c, c->objects[a[0].slot].link);
                        return WAYLAND_OK;
                }
                wayland_device_remove(c, 0, self->id);
                return WAYLAND_OK;
        case WLI_zwp_primary_selection_device_v1:
                if (op == WLQ_zwp_primary_selection_device_v1_set_selection)
                        return wayland_set_selection(c, self, 1, a);
                wayland_device_remove(c, 1, self->id);
                return WAYLAND_OK;
        case WLI_wl_data_offer:
                if (op == WLQ_wl_data_offer_receive)
                        return wayland_receive(c, self, a);
                if (op == WLQ_wl_data_offer_destroy)
                        c->offers[self->link].live = 0;
                else if (op == WLQ_wl_data_offer_finish)
                        return wayland_fail(c, self->id, WLN_wl_data_offer_error_invalid_finish, "finish is for a drag's offer");
                return WAYLAND_OK;
        case WLI_zwp_primary_selection_offer_v1:
                if (op == WLQ_zwp_primary_selection_offer_v1_receive)
                        return wayland_receive(c, self, a);
                c->offers[self->link].live = 0;
                return WAYLAND_OK;
        default:
                return WAYLAND_OK;
        }
}

/*
        wp_cursor_shape: a client names the shape it wants under the pointer (a cursor of the theme, by name) instead of drawing
        one. The compositor draws its own pointer whatever a window's client says, so the shape is checked (a shape that does
        not exist is the client's error) and no more; a toolkit that is told it is offered shapes does not load a theme and make
        a surface for each, which is what offering them is for. Tablets are not offered, so there is no tool to name.
*/
/*
        wp_viewporter: a viewport is a surface's destination size and source rectangle, pending state a commit applies. There is
        no scaler, so a destination that is not the buffer's size in the surface's scale shows the buffer as it is drawn; what
        the destination gives is the surface's size in its own units (the scale of a window that follows the fractional scale
        is the buffer's pixels over the destination). Every number is a client's: it is checked as the protocol says, and a
        size that is not a pixel count the shim could hold is a bad value.
*/
static int wayland_viewport_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (self->iface == WLI_wp_viewporter)
        {
                if (op == WLQ_wp_viewporter_destroy)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return WAYLAND_OK;
                if (s->viewport_id)
                        return wayland_fail(c, self->id, WLN_wp_viewporter_error_viewport_exists, "the surface has a viewport");
                s->viewport_id = c->objects[a[0].slot].id;
                c->objects[a[0].slot].link = (short)wayland_surface_index(c, s);
                c->objects[a[0].slot].aux = (unsigned short)s->gen;
                return WAYLAND_OK;
        }
        s = wayland_surface_of(c, self - c->objects);
        if (op == WLQ_wp_viewport_destroy)
        {
                if (s)
                {
                        s->viewport_id = 0;
                        s->dest_width = s->dest_height = s->source_width = s->source_height = 0;
                }
                return WAYLAND_OK;
        }
        if (!s)
                return wayland_fail(c, self->id, WLN_wp_viewport_error_no_surface, "the surface is gone");
        if (op == WLQ_wp_viewport_set_source)
        {
                if (a[0].i == -256 && a[1].i == -256 && a[2].i == -256 && a[3].i == -256)
                {
                        s->source_width = s->source_height = 0;
                        return WAYLAND_OK;
                }
                if (a[0].i < 0 || a[1].i < 0 || a[2].i <= 0 || a[3].i <= 0)
                        return wayland_fail(c, self->id, WLN_wp_viewport_error_bad_value, "the source is negative or empty");
                s->source_x = a[0].i;
                s->source_y = a[1].i;
                s->source_width = a[2].i;
                s->source_height = a[3].i;
                return WAYLAND_OK;
        }
        if (op == WLQ_wp_viewport_set_destination)
        {
                if (a[0].i == -1 && a[1].i == -1)
                {
                        s->dest_width = s->dest_height = 0;
                        return WAYLAND_OK;
                }
                if (a[0].i <= 0 || a[1].i <= 0)
                        return wayland_fail(c, self->id, WLN_wp_viewport_error_bad_value, "the destination is empty");
                s->dest_width = wayland_min(a[0].i, WAYLAND_DIMENSION);
                s->dest_height = wayland_min(a[1].i, WAYLAND_DIMENSION);
        }
        return WAYLAND_OK;
}

/* wp_fractional_scale: the scale of the screen the surface is on, in 120ths, said when it is made and whenever it changes. */
static int wayland_fractional_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (self->iface == WLI_wp_fractional_scale_v1)
        {
                s = wayland_surface_of(c, self - c->objects);
                if (s && op == WLQ_wp_fractional_scale_v1_destroy)
                        s->fractional_id = 0;
                return WAYLAND_OK;
        }
        if (op == WLQ_wp_fractional_scale_manager_v1_destroy)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[1].slot);
        if (!s)
                return WAYLAND_OK;
        if (s->fractional_id)
                return wayland_fail(c, self->id, WLN_wp_fractional_scale_manager_v1_error_fractional_scale_exists,
                                    "the surface has a fractional scale");
        s->fractional_id = c->objects[a[0].slot].id;
        c->objects[a[0].slot].link = (short)wayland_surface_index(c, s);
        c->objects[a[0].slot].aux = (unsigned short)s->gen;
        {
                // Said at once, in what it was told before it asked: the preferred scale is the first event of the object.
                struct wayland_event e = wayland_begin(c, s->fractional_id, WLE_wp_fractional_scale_v1_preferred_scale);

                wayland_word(&e, (unsigned long)(s->told120 > 0 ? s->told120 : WAYLAND_UNIT));
                wayland_end(&e);
        }
        return WAYLAND_OK;
}

/* The compositor's shape for a protocol shape: the ones it draws, and the arrow for the rest (a hand, a wait, a move it lacks). */
static long wayland_cursor_code(unsigned long shape)
{
        switch (shape)
        {
        case WLN_wp_cursor_shape_device_v1_shape_text:
        case WLN_wp_cursor_shape_device_v1_shape_vertical_text:
                return WAYLAND_CURSOR_TEXT;
        case WLN_wp_cursor_shape_device_v1_shape_crosshair:
                return WAYLAND_CURSOR_CROSS;
        case WLN_wp_cursor_shape_device_v1_shape_move:
        case WLN_wp_cursor_shape_device_v1_shape_all_scroll:
                return WAYLAND_CURSOR_MOVE;
        case WLN_wp_cursor_shape_device_v1_shape_e_resize:
        case WLN_wp_cursor_shape_device_v1_shape_w_resize:
        case WLN_wp_cursor_shape_device_v1_shape_ew_resize:
        case WLN_wp_cursor_shape_device_v1_shape_col_resize:
                return WAYLAND_CURSOR_RESIZE_H;
        case WLN_wp_cursor_shape_device_v1_shape_n_resize:
        case WLN_wp_cursor_shape_device_v1_shape_s_resize:
        case WLN_wp_cursor_shape_device_v1_shape_ns_resize:
        case WLN_wp_cursor_shape_device_v1_shape_row_resize:
                return WAYLAND_CURSOR_RESIZE_V;
        case WLN_wp_cursor_shape_device_v1_shape_nw_resize:
        case WLN_wp_cursor_shape_device_v1_shape_se_resize:
        case WLN_wp_cursor_shape_device_v1_shape_nwse_resize:
                return WAYLAND_CURSOR_RESIZE_NWSE;
        case WLN_wp_cursor_shape_device_v1_shape_ne_resize:
        case WLN_wp_cursor_shape_device_v1_shape_sw_resize:
        case WLN_wp_cursor_shape_device_v1_shape_nesw_resize:
                return WAYLAND_CURSOR_RESIZE_NESW;
        case WLN_wp_cursor_shape_device_v1_shape_pointer:
        case WLN_wp_cursor_shape_device_v1_shape_grab:
        case WLN_wp_cursor_shape_device_v1_shape_grabbing:
                return WAYLAND_CURSOR_HAND;
        case WLN_wp_cursor_shape_device_v1_shape_progress:
        case WLN_wp_cursor_shape_device_v1_shape_wait:
                return WAYLAND_CURSOR_WAIT;
        case WLN_wp_cursor_shape_device_v1_shape_not_allowed:
        case WLN_wp_cursor_shape_device_v1_shape_no_drop:
                return WAYLAND_CURSOR_NOTALLOWED;
        default:
                return WAYLAND_CURSOR_ARROW;
        }
}

/*
        wp_cursor_shape: a client names the shape it wants under the pointer, when its window has the pointer (the serial is one
        of its recent inputs, as the protocol asks). The compositor draws the pointer, so the shape is handed to the host for that
        window, which draws it inside the window and not on its edges; a shape the compositor lacks is its arrow. The shape is
        reset when the pointer enters a window and when it leaves one (wayland_pointer_to).
*/
static int wayland_cursor_shape_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (self->iface == WLI_wp_cursor_shape_manager_v1)
        {
                if (op == WLQ_wp_cursor_shape_manager_v1_get_tablet_tool_v2)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the seat has no tablet");
                return WAYLAND_OK;
        }
        if (op == WLQ_wp_cursor_shape_device_v1_set_shape &&
            (a[1].u < WLN_wp_cursor_shape_device_v1_shape_default || a[1].u > WLN_wp_cursor_shape_device_v1_shape_all_resize))
                return wayland_fail(c, self->id, WLN_wp_cursor_shape_device_v1_error_invalid_shape, "no such shape");
        if (op == WLQ_wp_cursor_shape_device_v1_set_shape && wayland.pointer_row >= 0 &&
            wayland.row_client[wayland.pointer_row] == c->slot && wayland_input_serial_ok(c, a[0].u))
                wayland.host->row_do(wayland.host->context, wayland.pointer_row, WAYLAND_DO_CURSOR, wayland_cursor_code(a[1].u), 0);
        return WAYLAND_OK;
}

/* ---- dma-bufs ---- */

/*
        zwp_linux_dmabuf_v1: a client that draws with the GPU hands over its buffer as a dma-buf, and the compositor reads it
        where it stands. What is taken is the host's to say (wayland_dmabuf_info): the device clients are to allocate on, and
        the format and modifier pairs with the tranches a client is told them in (version 4's feedback, which is also sent
        again when the host says they changed; a client of version 3 or less is sent each pair). Nothing is imported here: the
        host maps the descriptor read-only (it is a dma-buf, and as large as the plane needs), and the buffer is then a buffer
        of a pool that is that mapping, copied at commit and released as any is, except that the host brackets the read with
        the dma-buf's own synchronisation and may answer that the buffer's writer is not done, in which case that frame is
        skipped and the next one drawn whole. A client holds at most 48 such buffers, each with a descriptor, and all clients
        at most 192. One plane of ARGB8888 or XRGB8888 is what a buffer is.
*/

// An array of bytes as the wire has it: the length, the bytes, padded to a word.
static void wayland_array(struct wayland_event *e, const unsigned char *bytes, unsigned long length)
{
        unsigned long padded = (length + 3) & ~3UL;

        wayland_word(e, length);
        for (unsigned long at = 0; at < padded; at++)
                e->at[at] = at < length ? bytes[at] : 0;
        e->at += padded;
}

// A device number as a dev_t's eight bytes, the machine's order.
static void wayland_device_array(struct wayland_event *e, unsigned long device)
{
        unsigned char bytes[8];

        for (long at = 0; at < 8; at++)
                bytes[at] = (unsigned char)(device >> (8 * at));
        wayland_array(e, bytes, 8);
}

// The formats and, for version 3, the modifiers: what a client older than the feedback is told when it binds.
static void wayland_dmabuf_formats(struct wayland_client *c, unsigned long id, long version)
{
        const struct wayland_dmabuf_info *info = &wayland.dmabuf;

        for (long at = 0; at < info->count; at++)
        {
                int again = 0;

                for (long before = 0; before < at && !again; before++)
                        again = info->entry[before].format == info->entry[at].format;
                if (!again)
                {
                        struct wayland_event e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_v1_format);

                        wayland_word(&e, info->entry[at].format);
                        wayland_end(&e);
                }
                if (version >= 3)
                {
                        struct wayland_event e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_v1_modifier);

                        wayland_word(&e, info->entry[at].format);
                        wayland_word(&e, (unsigned long)(info->entry[at].modifier >> 32));
                        wayland_word(&e, (unsigned long)(info->entry[at].modifier & 0xffffffffUL));
                        wayland_end(&e);
                }
        }
}

// The feedback to one feedback object: the table, the main device, each tranche in order, and done.
static void wayland_feedback_send(struct wayland_client *c, long slot)
{
        const struct wayland_dmabuf_info *info = &wayland.dmabuf;
        unsigned long id = c->objects[slot].id;
        struct wayland_event e;
        long size = 0, fd = wayland.host->dmabuf_table(wayland.host->context, &size);

        if (fd < 0 || size <= 0 || c->out_fd_count >= WAYLAND_OUT_FDS)
                return;
        e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_format_table);
        wayland_word(&e, (unsigned long)size);
        c->out_fd_owned[c->out_fd_count] = 0;
        c->out_fds[c->out_fd_count] = fd;
        c->out_fd_at[c->out_fd_count++] = e.start;
        wayland_end(&e);
        e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_main_device);
        wayland_device_array(&e, info->device);
        wayland_end(&e);
        for (long at = 0; at < info->tranches; at++)
        {
                unsigned char indices[WAYLAND_DMABUF_ENTRIES * 2];

                e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_target_device);
                wayland_device_array(&e, info->tranche[at].device);
                wayland_end(&e);
                e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_flags);
                wayland_word(&e, info->tranche[at].flags);
                wayland_end(&e);
                for (long each = 0; each < info->tranche[at].count; each++)
                {
                        indices[each * 2] = info->tranche[at].index[each];
                        indices[each * 2 + 1] = 0;
                }
                e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_formats);
                wayland_array(&e, indices, (unsigned long)info->tranche[at].count * 2);
                wayland_end(&e);
                e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_done);
                wayland_end(&e);
        }
        e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_done);
        wayland_end(&e);
}

/*
        What the host takes, read and checked: at least one pair, only the formats a buffer can be (one plane of ARGB8888 or
        XRGB8888), tranches that name pairs there are, and a table to name them in. A host that fails any of it takes no
        dma-bufs, and the global is not offered.
*/
static void wayland_dmabuf_load(void)
{
        struct wayland_dmabuf_info *info = &wayland.dmabuf;
        long size = 0;

        for (unsigned long at = 0; at < sizeof *info; at++)
                ((unsigned char *)info)[at] = 0;
        if (!wayland.host->dmabuf_info(wayland.host->context, info) || info->count < 1 || info->count > WAYLAND_DMABUF_ENTRIES ||
            info->tranches < 1 || info->tranches > WAYLAND_DMABUF_TRANCHES || wayland.host->dmabuf_table(wayland.host->context, &size) < 0 ||
            size != info->count * 16)
                goto none;
        for (long at = 0; at < info->count; at++)
                if (info->entry[at].format != WAYLAND_FOURCC_ARGB8888 && info->entry[at].format != WAYLAND_FOURCC_XRGB8888)
                        goto none;
        for (long at = 0; at < info->tranches; at++)
        {
                if (info->tranche[at].count < 1 || info->tranche[at].count > info->count)
                        goto none;
                for (long each = 0; each < info->tranche[at].count; each++)
                        if (info->tranche[at].index[each] >= info->count)
                                goto none;
        }
        return;
none:
        info->count = 0;
}

/*
        The host's dma-bufs changed (a device came or went, a plane to scan out was found): what is taken is read again and
        every feedback object of every client is sent it again. A client that bound at version 3 keeps what it was told.
*/
WAYLAND_API void wayland_dmabuf_changed(void)
{
        if (!wayland.host)
                return;
        wayland_dmabuf_load();
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                for (long at = 0; c && wayland.dmabuf.count > 0 && at < c->feedbacks; at++)
                {
                        long object = wayland_slot_of(c, c->feedback_id[at]);

                        if (object >= 0 && c->objects[object].iface == WLI_zwp_linux_dmabuf_feedback_v1)
                                wayland_feedback_send(c, object);
                }
        }
}

static void wayland_params_free(struct wayland_client *c, struct wayland_params *p)
{
        for (long plane = 0; plane < 4; plane++)
                if (p->fd[plane] >= 0)
                {
                        wayland.host->close(wayland.host->context, p->fd[plane]);
                        wayland.fds--;
                        p->fd[plane] = -1;
                }
        (void)c;
        p->live = 0;
}

/*
        The buffer a params object describes, or why not: 0 and the buffer's slot, -1 for the protocol error already queued,
        -2 for a buffer that cannot be made (async: failed; immediate: invalid_wl_buffer). The descriptor goes to a pool of
        its own that is its mapping.
*/
static long wayland_dmabuf_make(struct wayland_client *c, struct wayland_object *self, struct wayland_params *p, const struct wayland_arg *a,
                                long id_slot)
{
        unsigned long width = a[0].u, height = a[1].u, format = a[2].u, flags = a[3].u;
        long offset, stride, need, pool = -1, buffer = -1, found = 0;
        void *at;

        p->used = 1; // a params object used (and checked unused before, by the request) makes one buffer, made or not
        if (p->fd[0] < 0 || p->fd[1] >= 0 || p->fd[2] >= 0 || p->fd[3] >= 0)
                return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_incomplete, "one plane is what the format has"), -1;
        if (width < 1 || height < 1 || width > WAYLAND_DIMENSION || height > WAYLAND_DIMENSION)
                return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_invalid_dimensions, "the size is out of range"), -1;
        if (format != WAYLAND_FOURCC_ARGB8888 && format != WAYLAND_FOURCC_XRGB8888)
                return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_invalid_format, "the format is not offered"), -1;
        offset = (long)p->offset[0];
        stride = (long)p->stride[0];
        if (p->offset[0] > 0x7fffffffUL || p->stride[0] > (1UL << 20) || stride < (long)width * 4 || (stride & 3))
                return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_out_of_bounds, "the plane does not fit its size"), -1;
        need = offset + stride * ((long)height - 1) + (long)width * 4;
        for (long entry = 0; entry < wayland.dmabuf.count; entry++)
                found |= wayland.dmabuf.entry[entry].format == format && wayland.dmabuf.entry[entry].modifier == p->modifier[0];
        if (!found || flags || need > WAYLAND_POOL_BYTES || c->pool_bytes + need > WAYLAND_POOL_TOTAL || c->dmabufs >= WAYLAND_DMABUFS ||
            wayland.dmabufs >= WAYLAND_DMABUFS_ALL)
                return -2;
        for (long i = 0; i < WAYLAND_POOLS && pool < 0; i++)
                if (!c->pools[i].live)
                        pool = i;
        for (long i = 0; i < WAYLAND_BUFFERS && buffer < 0; i++)
                if (!c->buffers[i].live)
                        buffer = i;
        if (pool < 0 || buffer < 0)
                return -2;
        at = wayland.host->dmabuf_map(wayland.host->context, p->fd[0], need);
        if (!at)
                return -2;
        c->pools[pool].live = 1;
        c->pools[pool].fd = p->fd[0];
        c->pools[pool].dmabuf = 1;
        c->pools[pool].size = need;
        c->pools[pool].refs = 1;
        c->pools[pool].gone = 1;
        c->pools[pool].at = at;
        c->pool_bytes += need;
        p->fd[0] = -1; // the pool holds it now, and wayland.fds still counts it
        c->buffers[buffer].live = 1;
        c->buffers[buffer].pool = pool;
        c->buffers[buffer].offset = offset;
        c->buffers[buffer].width = (long)width;
        c->buffers[buffer].height = (long)height;
        c->buffers[buffer].stride = stride;
        c->buffers[buffer].format = format == WAYLAND_FOURCC_ARGB8888 ? WLN_wl_shm_format_argb8888 : WLN_wl_shm_format_xrgb8888;
        c->buffers[buffer].id = c->objects[id_slot].id;
        c->buffers[buffer].held = c->buffers[buffer].zombie = 0;
        c->objects[id_slot].link = (short)buffer;
        c->dmabufs++;
        wayland.dmabufs++;
        wayland.pools++;
        wayland.buffers++;
        return buffer;
}

static int wayland_dmabuf_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_params *p;
        long slot;

        switch (self->iface)
        {
        case WLI_zwp_linux_dmabuf_v1:
                if (op == WLQ_zwp_linux_dmabuf_v1_create_params)
                {
                        long index = -1;

                        for (long at = 0; at < WAYLAND_PARAMS && index < 0; at++)
                                if (!c->params[at].live)
                                        index = at;
                        if (index < 0)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many buffer params");
                        c->params[index].id = c->objects[a[0].slot].id;
                        c->params[index].live = 1;
                        c->params[index].used = 0;
                        for (long plane = 0; plane < 4; plane++)
                        {
                                c->params[index].fd[plane] = -1;
                                c->params[index].offset[plane] = c->params[index].stride[plane] = c->params[index].modifier[plane] = 0;
                        }
                        c->objects[a[0].slot].link = (short)index;
                }
                else if (op == WLQ_zwp_linux_dmabuf_v1_get_default_feedback || op == WLQ_zwp_linux_dmabuf_v1_get_surface_feedback)
                {
                        if (c->feedbacks >= WAYLAND_FEEDBACKS)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many feedback objects");
                        c->feedback_id[c->feedbacks++] = c->objects[a[0].slot].id;
                        wayland_feedback_send(c, a[0].slot);
                }
                return WAYLAND_OK;
        case WLI_zwp_linux_dmabuf_feedback_v1:
                for (long at = 0; at < c->feedbacks; at++)
                        if (c->feedback_id[at] == self->id)
                        {
                                c->feedback_id[at] = c->feedback_id[--c->feedbacks];
                                break;
                        }
                return WAYLAND_OK;
        default:
                break;
        }
        p = &c->params[self->link];
        if (op == WLQ_zwp_linux_buffer_params_v1_destroy)
        {
                wayland_params_free(c, p);
                return WAYLAND_OK;
        }
        if (p->used)
                return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_already_used, "the params were used");
        if (op == WLQ_zwp_linux_buffer_params_v1_add)
        {
                unsigned long plane = a[1].u;

                if (plane > 3)
                        return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_plane_idx, "no such plane");
                if (p->fd[plane] >= 0)
                        return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_plane_set, "the plane is set");
                p->fd[plane] = a[0].fd; // held until the params are used or destroyed
                a[0].fd = -1;
                p->offset[plane] = a[2].u;
                p->stride[plane] = a[3].u;
                p->modifier[plane] = a[4].u << 32 | a[5].u;
                return WAYLAND_OK;
        }
        if (op == WLQ_zwp_linux_buffer_params_v1_set_sampling_device)
                return WAYLAND_OK;
        if (op == WLQ_zwp_linux_buffer_params_v1_create)
        {
                struct wayland_event e;
                unsigned long id = wayland_server_id(c);
                long object = id ? wayland_object_new(c, id, WLI_wl_buffer, 1) : -1, made;
                struct wayland_arg wa[4] = {a[0], a[1], a[2], a[3]};

                made = object >= 0 ? wayland_dmabuf_make(c, self, p, wa, object) : (p->used = 1, -2);
                if (made == -1)
                {
                        if (object >= 0)
                                wayland_object_free(c, object);
                        return WAYLAND_DEAD;
                }
                if (made < 0)
                {
                        if (object >= 0)
                                wayland_object_free(c, object);
                        e = wayland_begin(c, self->id, WLE_zwp_linux_buffer_params_v1_failed);
                        wayland_end(&e);
                        return WAYLAND_OK;
                }
                e = wayland_begin(c, self->id, WLE_zwp_linux_buffer_params_v1_created);
                wayland_word(&e, id);
                wayland_end(&e);
                return WAYLAND_OK;
        }
        // create_immed: the buffer is the id the client chose, and a buffer that cannot be made is the client's error.
        slot = a[0].slot;
        {
                struct wayland_arg wa[4] = {a[1], a[2], a[3], a[4]};
                long made = wayland_dmabuf_make(c, self, p, wa, slot);

                if (made == -1)
                        return WAYLAND_DEAD;
                if (made < 0)
                        return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_invalid_wl_buffer, "the buffer cannot be made");
        }
        return WAYLAND_OK;
}

/* ---- games: the pointer held, motion without a place, and what a game asks of the desktop ---- */

/*
        A game takes the pointer for its camera: zwp_pointer_constraints_v1 locks it (the pointer stays where it is, is not
        drawn, and the hand's counts go to the game, through zwp_relative_pointer_v1, as they are, unaccelerated) or confines
        it to the window. A lock or confinement holds only while its window is under the pointer and has the keys, so
        Alt-Tab, a click elsewhere, the window going or its client going end it at once, and the compositor's own chords are
        taken before any client sees them: no client can keep the pointer from the person. One holds at a time, the first of the
        clients in order that qualifies. A one-shot constraint ends the first time it is let go, a persistent one holds again
        when its window qualifies again. A confinement is to the whole window whatever region was named, which the protocol
        allows. Relative motion is also sent for every move of the pointer over a client's window, as the places differ.
        Idle inhibitors are taken and do nothing (the compositor has no idle to inhibit), a keyboard shortcuts inhibitor is
        taken and never made active (the compositor's chords are not a client's to take), and tearing control is taken and does
        nothing (frames are paced to the screen). Each of those is one a surface.
*/

// Relative motion to the relative pointers of a client, in the surface's units: the places of the hand, accelerated or not.
static void wayland_relative_send(struct wayland_client *c, long scale120, long dx, long dy, long udx, long udy, long micro)
{
        for (long at = 0; at < c->relatives; at++)
        {
                long slot = wayland_slot_of(c, c->relative_id[at]);
                struct wayland_event e;

                if (slot < 0 || c->objects[slot].iface != WLI_zwp_relative_pointer_v1)
                        continue;
                e = wayland_begin(c, c->relative_id[at], WLE_zwp_relative_pointer_v1_relative_motion);
                wayland_word(&e, (unsigned long)micro >> 32);
                wayland_word(&e, (unsigned long)micro & 0xffffffffUL);
                wayland_word(&e, wayland_fixed(dx, scale120));
                wayland_word(&e, wayland_fixed(dy, scale120));
                wayland_word(&e, wayland_fixed(udx, scale120));
                wayland_word(&e, wayland_fixed(udy, scale120));
                wayland_end(&e);
        }
}

// The hand's counts, to the client whose window holds the pointer locked.
WAYLAND_API void wayland_pointer_relative(long row, long dx, long dy, long micro)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);

        if (!wayland.host || !s || row != wayland.hold_row || wayland.hold_mode != 1)
                return;
        wayland_relative_send(c, s->scale120, dx, dy, dx, dy, micro);
}

static void wayland_constraint_event(struct wayland_client *c, struct wayland_constraint *con, int on)
{
        struct wayland_event e;

        if (wayland_slot_of(c, con->id) < 0)
                return;
        if (con->kind == 1)
                e = wayland_begin(c, con->id, on ? WLE_zwp_locked_pointer_v1_locked : WLE_zwp_locked_pointer_v1_unlocked);
        else
                e = wayland_begin(c, con->id, on ? WLE_zwp_confined_pointer_v1_confined : WLE_zwp_confined_pointer_v1_unconfined);
        wayland_end(&e);
}

// A constraint let go of: it says so (unless its object is going), and a one-shot one is over.
static void wayland_constraint_release(struct wayland_client *c, struct wayland_constraint *con, int say)
{
        if (con->active && say)
                wayland_constraint_event(c, con, 0);
        con->active = 0;
        if (con->oneshot || !say)
        {
                con->live = 0;
                wayland.constraints--;
        }
}

/*
        Which constraint holds the pointer: the first, of the clients in order, whose window is under the pointer and has the
        keys. The others that held it are let go, the one that qualifies is told it holds, and the host is told what is held.
*/
static void wayland_constraints_update(void)
{
        long client = -1, chosen = -1, row = -1, mode = 0;

        if (!wayland.host || (!wayland.constraints && !wayland.hold_mode))
                return;
        for (long slot = 0; slot < WAYLAND_CLIENTS && chosen < 0; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                for (long at = 0; c && at < WAYLAND_CONSTRAINTS && chosen < 0; at++)
                {
                        struct wayland_constraint *con = &c->constraints[at];
                        struct wayland_surface *surface = con->live && con->surface ? &c->surfaces[con->surface - 1] : 0;
                        struct wayland_surface *root = surface && surface->live ? wayland_root_of(c, surface) : 0;

                        if (root && root->row >= 0 && root->row == wayland.pointer_row && root->row == wayland.keyboard_row)
                        {
                                client = slot;
                                chosen = at;
                                row = root->row;
                                mode = con->kind;
                        }
                }
        }
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                for (long at = 0; c && at < WAYLAND_CONSTRAINTS; at++)
                        if (c->constraints[at].live && c->constraints[at].active && !(slot == client && at == chosen))
                                wayland_constraint_release(c, &c->constraints[at], 1);
        }
        if (chosen >= 0 && !wayland.client[client]->constraints[chosen].active)
        {
                wayland.client[client]->constraints[chosen].active = 1;
                wayland_constraint_event(wayland.client[client], &wayland.client[client]->constraints[chosen], 1);
        }
        if (row != wayland.hold_row || mode != wayland.hold_mode)
        {
                wayland.hold_row = row;
                wayland.hold_mode = mode;
                wayland.host->pointer_hold(wayland.host->context, row, mode);
        }
}

// A surface that is going: its constraints go, and what they held is let go.
static void wayland_constraints_drop(struct wayland_client *c, long surface)
{
        for (long at = 0; at < WAYLAND_CONSTRAINTS; at++)
                if (c->constraints[at].live && c->constraints[at].surface == surface + 1)
                        wayland_constraint_release(c, &c->constraints[at], 0);
        wayland_constraints_update();
}

/*
        wp_presentation.feedback (surface, new id): the feedback is answered by the commit that follows it, when its frame is
        drawn (wayland_presented). Arguments in the order the XML has them: the surface first, then the new object. A surface
        holding its limit of feedbacks discards the new one at once: the protocol's answer for a frame that is not timed.
*/
static int wayland_presentation_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (op != WLQ_wp_presentation_feedback)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[0].slot);
        if (!s)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
        if (s->feedbacks >= WAYLAND_PRESENTATIONS)
                wayland_feedback_discard(c, c->objects[a[1].slot].id);
        else
                s->feedback[s->feedbacks++] = c->objects[a[1].slot].id;
        return WAYLAND_OK;
}

/*
        An object made for a surface (a wp_content_type, a wp_alpha_modifier surface, an xdg_dialog) takes the surface's link and
        generation, so wayland_surface_of finds the surface it is for, and is none once that surface is gone.
*/
static void wayland_hint_bind(struct wayland_client *c, long hint, struct wayland_surface *s)
{
        long base = wayland_find(c, s->id, WLI_wl_surface);

        if (hint < 0 || base < 0)
                return;
        c->objects[hint].link = c->objects[base].link;
        c->objects[hint].aux = c->objects[base].aux;
}

/*
        xdg-toplevel-icon. An icon is made with pixels or a name (its flags say which: WAYLAND_ICON_CONTENT) and is immutable once it is set
        on a toplevel (WAYLAND_ICON_SET). set_icon is double-buffered: the toplevel's icon is the one set when its surface next commits,
        and an icon with neither pixels nor a name resets it to the default. The pixels are not kept: nothing draws an icon yet.
*/
#define WAYLAND_ICON_CONTENT 1
#define WAYLAND_ICON_SET 2

static void wayland_icon_set(struct wayland_client *c, long toplevel, long icon)
{
        unsigned int id = c->objects[toplevel].id;
        struct wayland_surface *s = 0;

        if (icon >= 0)
                c->objects[icon].flags |= WAYLAND_ICON_SET;
        for (long at = 0; at < WAYLAND_SURFACES && !s; at++)
                if (c->surfaces[at].live && c->surfaces[at].role_id == id)
                        s = &c->surfaces[at];
        if (!s)
                return;
        s->pending_icon = icon >= 0 && (c->objects[icon].flags & WAYLAND_ICON_CONTENT) ? 1 : 0;
        if (s->pending_icon)
                wayland.icons++;
}

/*
        The hints a client gives a window, none of which is drawn yet but all of which the compositor can read (wayland_row_*):
        xdg_system_bell (a ring is taken and sounds nothing yet), xdg_dialog (modal or not, for the toplevel it was made for),
        wp_content_type (the surface's kind of content, none by default and none on destroy), wp_alpha_modifier (the multiplier
        a commit shows, opaque by default and again on destroy). A request that does not name a live surface is checked, not
        taken: the protocol's refusals are the wire's, the rest is the hint.
*/
static int wayland_hints_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        switch (self->iface)
        {
        case WLI_xdg_wm_dialog_v1:
                if (op != WLQ_xdg_wm_dialog_v1_get_xdg_dialog)
                        return WAYLAND_OK;
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                        if (c->surfaces[at].live && c->surfaces[at].role_id == c->objects[a[1].slot].id)
                        {
                                wayland_hint_bind(c, a[0].slot, &c->surfaces[at]);
                                return WAYLAND_OK;
                        }
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "no such toplevel");
        case WLI_xdg_dialog_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_xdg_dialog_v1_set_modal && s)
                        s->modal = 1;
                else if ((op == WLQ_xdg_dialog_v1_unset_modal || op == WLQ_xdg_dialog_v1_destroy) && s)
                        s->modal = 0;
                return WAYLAND_OK;
        case WLI_wp_content_type_manager_v1:
                if (op != WLQ_wp_content_type_manager_v1_get_surface_content_type)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                wayland_hint_bind(c, a[0].slot, s);
                return WAYLAND_OK;
        case WLI_wp_content_type_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_content_type_v1_set_content_type && s)
                        s->content = a[0].u <= WLN_wp_content_type_v1_type_game ? (unsigned int)a[0].u : WLN_wp_content_type_v1_type_none;
                else if (op == WLQ_wp_content_type_v1_destroy && s)
                        s->content = WLN_wp_content_type_v1_type_none;
                return WAYLAND_OK;
        case WLI_wp_alpha_modifier_v1:
                if (op != WLQ_wp_alpha_modifier_v1_get_surface)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                wayland_hint_bind(c, a[0].slot, s);
                return WAYLAND_OK;
        case WLI_wp_alpha_modifier_surface_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_alpha_modifier_surface_v1_set_multiplier && s)
                        s->pending_alpha = (unsigned int)a[0].u;
                else if (op == WLQ_wp_alpha_modifier_surface_v1_destroy && s)
                        s->pending_alpha = 0xffffffffu;
                return WAYLAND_OK;
        case WLI_wp_fifo_manager_v1:
                if (op != WLQ_wp_fifo_manager_v1_get_fifo)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                wayland_hint_bind(c, a[0].slot, s);
                return WAYLAND_OK;
        case WLI_wp_fifo_v1:
                // The barrier is the next commit's (wait_barrier); set_barrier is the one the compositor's latch releases.
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_fifo_v1_wait_barrier && s)
                        s->pending_fifo = 1;
                else if (op == WLQ_wp_fifo_v1_set_barrier && s)
                        s->pending_barrier = 1;
                return WAYLAND_OK;
        case WLI_wp_commit_timing_manager_v1:
                if (op != WLQ_wp_commit_timing_manager_v1_get_timer)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                wayland_hint_bind(c, a[0].slot, s);
                return WAYLAND_OK;
        case WLI_wp_commit_timer_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op != WLQ_wp_commit_timer_v1_set_timestamp || !s)
                        return WAYLAND_OK;
                {
                        unsigned long seconds = ((unsigned long)a[0].u << 32) | (unsigned long)a[1].u;

                        if (s->pending_timestamp)
                                return wayland_fail(c, self->id, WLN_wp_commit_timer_v1_error_timestamp_exists, "the commit has a time already");
                        if (a[2].u >= 1000000000ul || seconds > 18000000000ul)
                                return wayland_fail(c, self->id, WLN_wp_commit_timer_v1_error_invalid_timestamp, "the timestamp is not a time");
                        s->pending_timestamp = seconds * 1000000000ul + (unsigned long)a[2].u;
                        if (!s->pending_timestamp)
                                s->pending_timestamp = 1;
                }
                return WAYLAND_OK;
        case WLI_xdg_toplevel_icon_manager_v1:
                if (op == WLQ_xdg_toplevel_icon_manager_v1_set_icon)
                        wayland_icon_set(c, a[0].slot, a[1].u ? a[1].slot : -1);
                return WAYLAND_OK;
        case WLI_xdg_toplevel_icon_v1:
                if (op == WLQ_xdg_toplevel_icon_v1_set_name || op == WLQ_xdg_toplevel_icon_v1_add_buffer)
                {
                        if (self->flags & WAYLAND_ICON_SET)
                                return wayland_fail(c, self->id, WLN_xdg_toplevel_icon_v1_error_immutable, "the icon is on a toplevel");
                }
                if (op == WLQ_xdg_toplevel_icon_v1_set_name)
                        self->flags |= WAYLAND_ICON_CONTENT;
                else if (op == WLQ_xdg_toplevel_icon_v1_add_buffer)
                {
                        struct wayland_buffer *b = &c->buffers[c->objects[a[0].slot].link];

                        // A square of shared memory, at a scale of one or more (a dma-buf is not one).
                        if (c->pools[b->pool].dmabuf || b->width != b->height || b->width <= 0 || a[1].i < 1)
                                return wayland_fail(c, self->id, WLN_xdg_toplevel_icon_v1_error_invalid_buffer,
                                                    "the buffer is not a square of shared memory");
                        self->flags |= WAYLAND_ICON_CONTENT;
                }
                return WAYLAND_OK;
        default: // xdg_system_bell: a ring is counted, and nothing sounds or flashes yet
                if (self->iface == WLI_xdg_system_bell_v1 && op == WLQ_xdg_system_bell_v1_ring)
                        wayland.bells++;
                return WAYLAND_OK;
        }
}

/*
        xdg-activation. A token is made by the client that asks for one, given a serial of its own input (the one the person's key
        or button carried to it) and a surface, and given out by its commit: a random 32 digit text, sent in done. A client
        activates a window with the text: when the token's serial is still one of its client's recent inputs and the token is
        under ten seconds old, the window is raised and focused (WAYLAND_DO_FOCUS); otherwise the window only wants attention.
        A token is used once; a token the shim does not know is nothing.
*/
static int wayland_token_free(void)
{
        long now = wayland.host->now(wayland.host->context);

        for (int at = 0; at < WAYLAND_TOKENS; at++)
                if (wayland.token[at].live && wayland.token[at].committed && now - wayland.token[at].when >= WAYLAND_TOKEN_MS)
                        wayland.token[at].live = 0;
        for (int at = 0; at < WAYLAND_TOKENS; at++)
                if (!wayland.token[at].live)
                        return at;
        return -1;
}

static int wayland_token_make(struct wayland_token *t)
{
        static const char digits[] = "0123456789abcdef";
        unsigned char bytes[16];

        if (!wayland.host->random(wayland.host->context, bytes, (long)sizeof bytes))
                return 0;
        for (int at = 0; at < 16; at++)
        {
                t->text[2 * at] = digits[bytes[at] >> 4];
                t->text[2 * at + 1] = digits[bytes[at] & 15];
        }
        t->text[32] = 0;
        return 1;
}

static void wayland_token_forget(long slot)
{
        for (int at = 0; at < WAYLAND_TOKENS; at++)
                if (wayland.token[at].live && wayland.token[at].client == slot)
                        wayland.token[at].live = 0;
}

static int wayland_activation_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        int at = self->link;
        struct wayland_token *t = at >= 0 && at < WAYLAND_TOKENS && wayland.token[at].live && wayland.token[at].client == c->slot
                                      ? &wayland.token[at] : 0;

        if (self->iface == WLI_xdg_activation_v1)
        {
                struct wayland_surface *s;
                long now;

                if (op == WLQ_xdg_activation_v1_get_activation_token)
                {
                        at = wayland_token_free();
                        if (at < 0)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many activation tokens");
                        wayland.token[at].live = 1;
                        wayland.token[at].committed = 0;
                        wayland.token[at].client = c->slot;
                        wayland.token[at].serial = 0;
                        wayland.token[at].text[0] = 0;
                        c->objects[a[0].slot].link = (short)at;
                        return WAYLAND_OK;
                }
                if (op != WLQ_xdg_activation_v1_activate)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                for (at = 0; at < WAYLAND_TOKENS; at++)
                        if (wayland.token[at].live && wayland.token[at].committed &&
                            wayland_name_is(a[0].text, a[0].length, wayland.token[at].text))
                                break;
                if (at == WAYLAND_TOKENS)
                        return WAYLAND_OK;
                {
                        struct wayland_token *given = &wayland.token[at];
                        struct wayland_client *issuer = wayland.client[given->client];
                        int recent;

                        now = wayland.host->now(wayland.host->context);
                        recent = now - given->when < WAYLAND_TOKEN_MS;
                        given->live = 0;
                        if (recent && issuer && s->row >= 0 && wayland_input_serial_ok(issuer, given->serial))
                        {
                                s->attention = 0;
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_FOCUS, 0, 0);
                        }
                        else
                                s->attention = 1;
                }
                return WAYLAND_OK;
        }
        // xdg_activation_token_v1: a serial (set_serial), the surface and app id are kept for nothing else, and a commit gives the token out.
        if (op == WLQ_xdg_activation_token_v1_set_serial)
        {
                if (t && !t->committed)
                        t->serial = (unsigned long)a[0].u;
        }
        else if (op == WLQ_xdg_activation_token_v1_commit)
        {
                if (t && !t->committed)
                {
                        struct wayland_event e;

                        if (!wayland_token_make(t))
                        {
                                t->live = 0;
                                return WAYLAND_OK;
                        }
                        t->committed = 1;
                        t->when = wayland.host->now(wayland.host->context);
                        e = wayland_begin(c, self->id, WLE_xdg_activation_token_v1_done);
                        wayland_string(&e, t->text);
                        wayland_end(&e);
                }
        }
        else if (op == WLQ_xdg_activation_token_v1_destroy)
        {
                if (t && !t->committed)
                        t->live = 0;
        }
        return WAYLAND_OK;
}

/*
        xwayland_shell_v1 and zwp_xwayland_keyboard_grab_manager_v1, the two protocols an X server (Xwayland) uses when its X
        windows are wl_surfaces of this compositor. get_xwayland_surface gives a surface its xwayland_surface_v1 (a surface with a
        role of its own is refused), and set_serial gives that surface the X11 serial of its window: the X window manager, which
        is this compositor too, names the same serial when it associates the window (wayland_serial_row). A grab of the keyboard
        holds the keys as a shortcuts inhibitor does, and is let go by its destroy; the chords that a shortcuts inhibitor
        suspends are the compositor's to suspend, and read through wayland_row_shortcuts.
*/
static int wayland_xwayland_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *surface;

        if (self->iface == WLI_xwayland_shell_v1)
        {
                if (op == WLQ_xwayland_shell_v1_get_xwayland_surface)
                {
                        surface = wayland_surface_of(c, a[1].slot);
                        if (!surface)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                        if (surface->xsurface || (surface->flags & WAYLAND_SURFACE_XDG) || surface->role != WAYLAND_ROLE_NONE)
                                return wayland_fail(c, self->id, WLN_xwayland_shell_v1_error_role, "the surface has another role");
                        surface->xsurface = 1;
                        c->objects[a[0].slot].link = (short)wayland_surface_index(c, surface);
                        c->objects[a[0].slot].aux = (unsigned short)surface->gen;
                }
                return WAYLAND_OK;
        }
        if (self->iface == WLI_xwayland_surface_v1)
        {
                if (op == WLQ_xwayland_surface_v1_set_serial)
                {
                        unsigned long long serial = ((unsigned long long)a[1].u << 32) | a[0].u;

                        surface = wayland_surface_of(c, self - c->objects);
                        if (!surface)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                        if (surface->xserial_set)
                                return wayland_fail(c, self->id, WLN_xwayland_surface_v1_error_already_associated,
                                                    "the surface has a serial");
                        if (!serial)
                                return wayland_fail(c, self->id, WLN_xwayland_surface_v1_error_invalid_serial, "the serial is zero");
                        surface->xserial = serial;
                        surface->xserial_set = 1;
                }
                return WAYLAND_OK;
        }
        if (self->iface == WLI_zwp_xwayland_keyboard_grab_manager_v1)
        {
                if (op == WLQ_zwp_xwayland_keyboard_grab_manager_v1_grab_keyboard)
                {
                        surface = wayland_surface_of(c, a[1].slot);
                        if (!surface)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                        surface->xgrab = 1;
                        c->objects[a[0].slot].link = (short)wayland_surface_index(c, surface);
                        c->objects[a[0].slot].aux = (unsigned short)surface->gen;
                }
                return WAYLAND_OK;
        }
        if (self->iface == WLI_zwp_xwayland_keyboard_grab_v1)
        {
                surface = wayland_surface_of(c, self - c->objects);
                if (surface && op == WLQ_zwp_xwayland_keyboard_grab_v1_destroy)
                        surface->xgrab = 0;
        }
        return WAYLAND_OK;
}

static int wayland_games_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *surface;

        switch (self->iface)
        {
        case WLI_zwp_relative_pointer_manager_v1:
                if (op == WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer)
                {
                        if (c->relatives >= WAYLAND_RELATIVES)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many relative pointers");
                        c->relative_id[c->relatives++] = c->objects[a[0].slot].id;
                }
                return WAYLAND_OK;
        case WLI_zwp_relative_pointer_v1:
                for (long at = 0; at < c->relatives; at++)
                        if (c->relative_id[at] == self->id)
                        {
                                c->relative_id[at] = c->relative_id[--c->relatives];
                                break;
                        }
                return WAYLAND_OK;
        case WLI_zwp_pointer_constraints_v1:
        {
                long index = -1;

                if (op == WLQ_zwp_pointer_constraints_v1_destroy)
                        return WAYLAND_OK;
                surface = wayland_surface_of(c, a[1].slot);
                if (!surface)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                if (a[4].u != WLN_zwp_pointer_constraints_v1_lifetime_oneshot && a[4].u != WLN_zwp_pointer_constraints_v1_lifetime_persistent)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the lifetime is oneshot or persistent");
                for (long at = 0; at < WAYLAND_CONSTRAINTS; at++)
                {
                        if (c->constraints[at].live && c->constraints[at].surface == wayland_surface_index(c, surface) + 1)
                                return wayland_fail(c, self->id, WLN_zwp_pointer_constraints_v1_error_already_constrained,
                                                    "the surface is constrained already");
                        if (!c->constraints[at].live && index < 0)
                                index = at;
                }
                if (index < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many pointer constraints");
                c->constraints[index].id = c->objects[a[0].slot].id;
                c->constraints[index].live = 1;
                c->constraints[index].active = 0;
                c->constraints[index].kind = op == WLQ_zwp_pointer_constraints_v1_lock_pointer ? 1 : 2;
                c->constraints[index].oneshot = a[4].u == WLN_zwp_pointer_constraints_v1_lifetime_oneshot;
                c->constraints[index].surface = (unsigned short)(wayland_surface_index(c, surface) + 1);
                c->objects[a[0].slot].link = (short)index;
                wayland.constraints++;
                wayland_constraints_update();
                return WAYLAND_OK;
        }
        case WLI_zwp_locked_pointer_v1:
        case WLI_zwp_confined_pointer_v1:
                // set_region and set_cursor_position_hint are taken: the whole window is what a hold is, and the pointer is
                // drawn by the compositor.
                if (op == WLQ_zwp_locked_pointer_v1_destroy)
                {
                        struct wayland_constraint *con = &c->constraints[self->link];

                        if (con->live)
                        {
                                wayland_constraint_release(c, con, 0);
                                wayland_constraints_update();
                        }
                }
                return WAYLAND_OK;
        case WLI_zwp_keyboard_shortcuts_inhibit_manager_v1:
                if (op == WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts)
                {
                        surface = wayland_surface_of(c, a[1].slot);
                        if (!surface)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                        if (surface->shortcuts)
                                return wayland_fail(c, self->id, WLN_zwp_keyboard_shortcuts_inhibit_manager_v1_error_already_inhibited,
                                                    "the surface has an inhibitor");
                        surface->shortcuts = 1;
                        surface->shortcut_id = c->objects[a[0].slot].id;
                        c->objects[a[0].slot].link = (short)wayland_surface_index(c, surface);
                        c->objects[a[0].slot].aux = (unsigned short)surface->gen;
                }
                return WAYLAND_OK;
        case WLI_zwp_keyboard_shortcuts_inhibitor_v1:
                surface = wayland_surface_of(c, self - c->objects);
                if (surface)
                {
                        surface->shortcuts = 0;
                        surface->shortcut_active = 0;
                        surface->escape_down = 0;
                }
                return WAYLAND_OK;
        case WLI_wp_tearing_control_manager_v1:
                if (op == WLQ_wp_tearing_control_manager_v1_get_tearing_control)
                {
                        surface = wayland_surface_of(c, a[1].slot);
                        if (!surface)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                        if (surface->tearing)
                                return wayland_fail(c, self->id, WLN_wp_tearing_control_manager_v1_error_tearing_control_exists,
                                                    "the surface has a tearing control");
                        surface->tearing = 1;
                        c->objects[a[0].slot].link = (short)wayland_surface_index(c, surface);
                        c->objects[a[0].slot].aux = (unsigned short)surface->gen;
                }
                return WAYLAND_OK;
        case WLI_wp_tearing_control_v1:
                surface = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_tearing_control_v1_destroy)
                {
                        if (surface)
                                surface->tearing = 0;
                        if (surface)
                                surface->pending_async = 0;
                }
                else if (a[0].u > WLN_wp_tearing_control_v1_presentation_hint_async)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "no such presentation hint");
                else if (surface)
                        surface->pending_async = a[0].u == WLN_wp_tearing_control_v1_presentation_hint_async;
                return WAYLAND_OK;
        case WLI_zwp_idle_inhibit_manager_v1:
                if (op != WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor)
                        return WAYLAND_OK;
                surface = wayland_surface_of(c, a[1].slot);
                if (!surface)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                // The inhibitor is linked to its surface, as a shortcuts inhibitor is: it holds while that surface is shown
                // (wayland_inhibitors_now), and ends with its surface or its own destroy, which is the object going.
                c->objects[a[0].slot].link = (short)wayland_surface_index(c, surface);
                c->objects[a[0].slot].aux = (unsigned short)surface->gen;
                c->inhibitors++;
                return WAYLAND_OK;
        case WLI_zwp_idle_inhibitor_v1:
                return WAYLAND_OK;
        default:
                return WAYLAND_OK;
        }
}

/*
        The idle clock (ext-idle-notify, and the screen's idle answer for the moonwater debug line). The clock is the last input the
        compositor gave the shim (wayland_input_seen). An idle inhibitor holds while its surface is shown on a screen: a window the host
        says is shown and is not minimized (the surface's root, for a subsurface). A surface with no window holds nothing, and neither does
        one whose window is gone; whether the window is covered is not known cheaply, so a covered window still holds.
*/
static int wayland_surface_shown(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_facts facts;

        if (!s)
                return 0;
        s = wayland_root_of(c, s);
        if (!s->live || s->row < 0 || wayland.host->row_facts(wayland.host->context, s->row, &facts) < 0)
                return 0;
        return !(facts.states & WAYLAND_STATE_SUSPENDED);
}

// The idle inhibitors that hold now: each zwp_idle_inhibitor_v1 of every client whose surface is shown.
static long wayland_inhibitors_now(void)
{
        long count = 0;

        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c || c->dead || !c->inhibitors)
                        continue;
                for (long at = 0; at < WAYLAND_OBJECTS; at++)
                        if (c->objects[at].iface == WLI_zwp_idle_inhibitor_v1 && c->objects[at].id &&
                            wayland_surface_shown(c, wayland_surface_of(c, at)))
                                count++;
        }
        return count;
}

static void wayland_notify_say(struct wayland_client *c, struct wayland_notify *n, long opcode)
{
        struct wayland_event e = wayland_begin(c, n->id, opcode);

        wayland_end(&e);
}

/*
        ext-idle-notify (ext_idle_notifier_v1, version 2). A notification counts from its creation, or from the input it was last resumed
        by, and goes idle when the shim has been given no input for its timeout and, unless it was made by get_input_idle_notification,
        no idle inhibitor holds. An idle notification sends resumed on the next input, and counts from that input again.
*/
static int wayland_idle_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long at;

        if (self->iface == WLI_ext_idle_notifier_v1)
        {
                struct wayland_notify *n;

                if (op != WLQ_ext_idle_notifier_v1_get_idle_notification && op != WLQ_ext_idle_notifier_v1_get_input_idle_notification)
                        return WAYLAND_OK;
                for (at = 0; at < WAYLAND_NOTIFIES && c->notifies[at].live; at++)
                        ;
                if (at == WAYLAND_NOTIFIES)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many idle notifications");
                n = &c->notifies[at];
                n->live = 1;
                n->idle = 0;
                n->input_only = op == WLQ_ext_idle_notifier_v1_get_input_idle_notification;
                n->id = c->objects[a[0].slot].id;
                n->timeout = (long)a[1].u;
                n->since = wayland.host->now(wayland.host->context);
                n->idled = 0;
                c->objects[a[0].slot].link = (short)at;
                return WAYLAND_OK;
        }
        if (op == WLQ_ext_idle_notification_v1_destroy && self->link >= 0 && self->link < WAYLAND_NOTIFIES)
                c->notifies[self->link].live = 0;
        return WAYLAND_OK;
}

/* ---- one request ---- */
static int wayland_gestures_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a);

/*
        Reads a request's arguments by the table. A new id becomes an object here, of the interface the table names
        and the version of the object it was sent to; an object argument must exist and be of its interface; a string
        must end in its NUL; a descriptor is the next one the client sent. The whole message is read before any
        handler runs, and nothing is left over.
*/
static int wayland_args(struct wayland_client *c, const struct wayland_object *self, const struct wl_message *msg,
                        const unsigned char *at, long left, struct wayland_arg *a)
{
        long n = 0;

        for (const unsigned char *kind = msg->args; kind[0]; kind += 2)
        {
                struct wayland_arg *arg;
                unsigned long word, count, padded;

                if (n >= WAYLAND_ARGS)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "too many arguments");
                arg = &a[n++];
                arg->u = 0;
                arg->i = 0;
                arg->text = 0;
                arg->length = 0;
                arg->slot = -1;
                arg->fd = -1;
                arg->version = 0;
                if (kind[0] == 'h')
                {
                        if (c->in_fd_count < 1)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a descriptor is missing");
                        arg->fd = c->in_fds[0];
                        for (long more = 1; more < c->in_fd_count; more++)
                                c->in_fds[more - 1] = c->in_fds[more];
                        c->in_fd_count--;
                        continue;
                }
                if (left < 4)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the arguments are short");
                word = wayland_get(at);
                at += 4;
                left -= 4;
                switch (kind[0])
                {
                case 'i':
                case 'f':
                        arg->i = (long)(int)word;
                        arg->u = word;
                        break;
                case 'u':
                        arg->u = word;
                        arg->i = (long)word;
                        break;
                case 'o':
                case 'O':
                        arg->u = word;
                        if (!word)
                        {
                                if (kind[0] == 'o')
                                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "an object is null");
                                break;
                        }
                        arg->slot = (int)wayland_slot_of(c, word);
                        if (arg->slot < 0)
                                return wayland_fail(c, word, WAYLAND_INVALID_OBJECT, "no such object");
                        if (kind[1] && c->objects[arg->slot].iface != kind[1])
                                return wayland_fail(c, word, WAYLAND_INVALID_METHOD, "the object is another interface");
                        break;
                case 'n':
                        arg->u = word;
                        arg->slot = word >= 1 && word < WAYLAND_IDS ? (int)wayland_object_new(c, word, kind[1], self->version) : -1;
                        if (arg->slot < 0)
                                return wayland_new_fail(c, self->id, word);
                        break;
                case 's':
                case 'S':
                case 'N':
                case 'a':
                        count = word;
                        if (count == 0 && kind[0] == 'S')
                                break;
                        if (count == 0 && kind[0] != 'a')
                                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a string has no length");
                        padded = (count + 3) & ~3UL;
                        if (count > (unsigned long)left || padded > (unsigned long)left)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a string is longer than its message");
                        if (kind[0] != 'a' && at[count - 1] != 0)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a string is not ended");
                        arg->text = at;
                        arg->length = kind[0] == 'a' ? (long)count : (long)count - 1;
                        at += padded;
                        left -= (long)padded;
                        if (kind[0] == 'N')
                        {
                                if (left < 8)
                                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the arguments are short");
                                arg->version = (long)wayland_get(at);
                                arg->u = wayland_get(at + 4);
                                at += 8;
                                left -= 8;
                        }
                        break;
                default:
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the table has an argument that cannot be read");
                }
        }
        if (left != 0)
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the arguments are longer than the request");
        return WAYLAND_OK;
}

static int wayland_dispatch(struct wayland_client *c, const unsigned char *message, long size)
{
        unsigned long id = wayland_get(message);
        long op = (long)(wayland_get(message + 4) & 0xffffUL), slot = wayland_slot_of(c, id), n;
        const struct wl_interface_info *info;
        const struct wl_message *msg;
        struct wayland_object *o;
        struct wayland_arg a[WAYLAND_ARGS];
        int status;

        if (slot < 0)
                return wayland_fail(c, id, WAYLAND_INVALID_OBJECT, "no such object");
        o = &c->objects[slot];
        info = &wl_interfaces[o->iface];
        if (op >= info->requests || info->request[op].since > o->version)
                return wayland_fail(c, id, WAYLAND_INVALID_METHOD, "no such request");
        msg = &info->request[op];
        for (n = 0; n < WAYLAND_ARGS; n++)
                a[n].fd = -1;
        status = wayland_args(c, o, msg, message + 8, size - 8, a);
        if (status >= 0)
        {
                switch (o->iface)
                {
                case WLI_wl_display:
                        status = wayland_display_request(c, op, a);
                        break;
                case WLI_wl_registry:
                        status = wayland_registry_request(c, o, a);
                        break;
                case WLI_wl_compositor:
                        status = wayland_compositor_request(c, op, a);
                        break;
                case WLI_wl_surface:
                        status = wayland_surface_request(c, o, op, a);
                        break;
                case WLI_wl_region:
                        status = wayland_region_request(c, o, op, a);
                        break;
                case WLI_wl_shm:
                        status = wayland_shm_request(c, o, a);
                        break;
                case WLI_wl_shm_pool:
                        status = wayland_pool_request(c, o, op, a);
                        break;
                case WLI_wl_buffer:
                        status = wayland_buffer_request(c, o);
                        break;
                case WLI_wl_subcompositor:
                        status = wayland_subcompositor_request(c, o, op, a);
                        break;
                case WLI_wl_subsurface:
                        status = wayland_subsurface_request(c, o, op, a);
                        break;
                case WLI_wl_seat:
                        status = wayland_seat_request(c, o, op, a);
                        break;
                case WLI_wl_pointer:
                case WLI_wl_keyboard:
                case WLI_wl_touch:
                        status = wayland_seat_object_request(c, o, op, a);
                        break;
                case WLI_xdg_wm_base:
                        status = wayland_wm_base_request(c, o, op, a);
                        break;
                case WLI_xdg_positioner:
                        status = wayland_positioner_request(c, o, op, a);
                        break;
                case WLI_xdg_surface:
                        status = wayland_xdg_surface_request(c, o, op, a);
                        break;
                case WLI_xdg_toplevel:
                        status = wayland_toplevel_request(c, o, op, a);
                        break;
                case WLI_xdg_popup:
                        status = wayland_popup_request(c, o, op, a);
                        break;
                case WLI_zxdg_output_manager_v1:
                        status = wayland_output_manager_request(c, o, op, a);
                        break;
                case WLI_zxdg_decoration_manager_v1:
                        status = wayland_decoration_manager_request(c, o, op, a);
                        break;
                case WLI_zxdg_toplevel_decoration_v1:
                        status = wayland_decoration_request(c, o, op, a);
                        break;
                case WLI_zwp_relative_pointer_manager_v1:
                case WLI_zwp_relative_pointer_v1:
                case WLI_zwp_pointer_constraints_v1:
                case WLI_zwp_locked_pointer_v1:
                case WLI_zwp_confined_pointer_v1:
                case WLI_zwp_idle_inhibit_manager_v1:
                case WLI_zwp_idle_inhibitor_v1:
                case WLI_zwp_keyboard_shortcuts_inhibit_manager_v1:
                case WLI_zwp_keyboard_shortcuts_inhibitor_v1:
                case WLI_wp_tearing_control_manager_v1:
                case WLI_wp_tearing_control_v1:
                        status = wayland_games_request(c, o, op, a);
                        break;
                case WLI_xwayland_shell_v1:
                case WLI_xwayland_surface_v1:
                case WLI_zwp_xwayland_keyboard_grab_manager_v1:
                case WLI_zwp_xwayland_keyboard_grab_v1:
                        status = wayland_xwayland_request(c, o, op, a);
                        break;
                case WLI_xdg_activation_v1:
                case WLI_xdg_activation_token_v1:
                        status = wayland_activation_request(c, o, op, a);
                        break;
                case WLI_zwp_pointer_gestures_v1:
                case WLI_zwp_pointer_gesture_swipe_v1:
                case WLI_zwp_pointer_gesture_pinch_v1:
                case WLI_zwp_pointer_gesture_hold_v1:
                        status = wayland_gestures_request(c, o, op, a);
                        break;
                case WLI_ext_idle_notifier_v1:
                case WLI_ext_idle_notification_v1:
                        status = wayland_idle_request(c, o, op, a);
                        break;
                case WLI_wp_fifo_manager_v1:
                case WLI_wp_fifo_v1:
                case WLI_wp_commit_timing_manager_v1:
                case WLI_wp_commit_timer_v1:
                        status = wayland_hints_request(c, o, op, a);
                        break;
                case WLI_xdg_wm_dialog_v1:
                case WLI_xdg_dialog_v1:
                case WLI_xdg_system_bell_v1:
                case WLI_xdg_toplevel_icon_manager_v1:
                case WLI_xdg_toplevel_icon_v1:
                case WLI_wp_content_type_manager_v1:
                case WLI_wp_content_type_v1:
                case WLI_wp_alpha_modifier_v1:
                case WLI_wp_alpha_modifier_surface_v1:
                        status = wayland_hints_request(c, o, op, a);
                        break;
                case WLI_zwp_linux_dmabuf_v1:
                case WLI_zwp_linux_buffer_params_v1:
                case WLI_zwp_linux_dmabuf_feedback_v1:
                        status = wayland_dmabuf_request(c, o, op, a);
                        break;
                case WLI_wp_cursor_shape_manager_v1:
                case WLI_wp_cursor_shape_device_v1:
                        status = wayland_cursor_shape_request(c, o, op, a);
                        break;
                case WLI_wp_presentation:
                        status = wayland_presentation_request(c, o, op, a);
                        break;
                case WLI_wp_viewporter:
                case WLI_wp_viewport:
                        status = wayland_viewport_request(c, o, op, a);
                        break;
                case WLI_wp_fractional_scale_manager_v1:
                case WLI_wp_fractional_scale_v1:
                        status = wayland_fractional_request(c, o, op, a);
                        break;
                case WLI_wl_data_device_manager:
                case WLI_wl_data_device:
                case WLI_wl_data_source:
                case WLI_wl_data_offer:
                case WLI_zwp_primary_selection_device_manager_v1:
                case WLI_zwp_primary_selection_device_v1:
                case WLI_zwp_primary_selection_source_v1:
                case WLI_zwp_primary_selection_offer_v1:
                        status = wayland_data_request(c, o, op, a);
                        break;
                default: // pointers, keyboards, outputs and xdg outputs have only a release or a destroy
                        status = WAYLAND_OK;
                        break;
                }
        }
        // A descriptor a handler did not take is closed here, whatever became of the request.
        for (n = 0; n < WAYLAND_ARGS; n++)
                if (a[n].fd >= 0)
                {
                        wayland.host->close(wayland.host->context, a[n].fd);
                        wayland.fds--;
                }
        if (status >= 0 && msg->destructor && o->iface && c->objects[slot].id == id)
                wayland_object_delete(c, slot);
        return status;
}

/* ---- the compositor's side of the shim ---- */

struct wayland_stats
{
        long clients, objects, surfaces, pools, buffers, fds, rows;
};

WAYLAND_API void wayland_stats_get(struct wayland_stats *into)
{
        into->clients = wayland.clients;
        into->objects = wayland.objects;
        into->surfaces = wayland.surfaces;
        into->pools = wayland.pools;
        into->buffers = wayland.buffers;
        into->fds = wayland.fds;
        into->rows = wayland.rows;
}

WAYLAND_API int wayland_start(const struct wayland_host *host)
{
        unsigned char *clear = (unsigned char *)&wayland;

        if (wayland.host || !host || !host->alloc || !host->release || !host->map || !host->remap || !host->unmap ||
            !host->close || !host->row_open || !host->row_close || !host->row_picture || !host->row_title || !host->row_do ||
            !host->row_facts || !host->screens || !host->now || !host->keymap || !host->repeat || !host->seat || !host->locks ||
            !host->clip_size || !host->clip_pull || !host->clip_abort || !host->clip_push || !host->clip_clear || !host->dmabuf_info ||
            !host->dmabuf_table || !host->dmabuf_map || !host->pointer_hold || !host->random)
                return -1;
        for (unsigned long at = 0; at < sizeof wayland; at++)
                clear[at] = 0;
        for (long row = 0; row < WAYLAND_ROWS; row++)
                wayland.row_client[row] = wayland.row_surface[row] = -1;
        wayland.pointer_row = wayland.pointer_surface = wayland.keyboard_row = -1;
        wayland.host = host;
        wayland.input_at = host->now(host->context);
        wayland.screens = host->screens(host->context, wayland.screen, WAYLAND_SCREENS);
        if (wayland.screens < 0)
                wayland.screens = 0;
        wayland_dmabuf_load();
        wayland.hold_row = -1;
        return 0;
}

/* A client's socket accepted: its record, with the display, or a refusal. */
WAYLAND_API long wayland_connect(long pid, long uid)
{
        struct wayland_client *c;
        long slot = -1;

        if (!wayland.host)
                return -1;
        for (long at = 0; at < WAYLAND_CLIENTS && slot < 0; at++)
                if (!wayland.client[at])
                        slot = at;
        if (slot < 0)
                return -1;
        c = (struct wayland_client *)wayland.host->alloc(wayland.host->context, (long)sizeof *c);
        if (!c)
                return -1;
        c->live = 1;
        c->slot = slot;
        c->pid = pid;
        c->uid = uid;
        wayland.client[slot] = c;
        wayland.clients++;
        wayland_object_new(c, 1, WLI_wl_display, 1);
        return slot;
}

/*
        The descriptors of the output queue let go: sent, or the client gone. The shim's own (a copy's pipe) are closed here and
        nowhere else, so the number the queue holds is open until it has gone out; the host's (a keymap, a format table) stay.
*/
static void wayland_out_fds_let_go(struct wayland_client *c)
{
        for (long at = 0; at < c->out_fd_count; at++)
                if (c->out_fd_owned[at])
                {
                        wayland.host->close(wayland.host->context, c->out_fds[at]);
                        wayland.fds--;
                }
        c->out_fd_count = 0;
}

/*
        A client gone, however it went: every window it had, every pool, every descriptor it sent and the shim did not
        use, and its record. The one place that gives back what a client held.
*/
WAYLAND_API void wayland_drop(long slot)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS ? wayland.client[slot] : 0;

        if (!c)
                return;
        wayland_touch_forget(slot);
        wayland_token_forget(slot);
        for (long at = 0; at < WAYLAND_SURFACES; at++)
                if (c->surfaces[at].live)
                {
                        wayland_row_release(c, &c->surfaces[at]);
                        wayland.surfaces--;
                }
        for (long at = 0; at < WAYLAND_POOLS; at++)
                if (c->pools[at].live)
                {
                        wayland.host->unmap(wayland.host->context, c->pools[at].at, c->pools[at].size);
                        if (c->pools[at].dmabuf)
                        {
                                wayland.host->close(wayland.host->context, c->pools[at].fd);
                                wayland.fds--;
                                wayland.dmabufs--;
                        }
                        wayland.pools--;
                }
        for (long at = 0; at < WAYLAND_PARAMS; at++)
                if (c->params[at].live)
                        wayland_params_free(c, &c->params[at]);
        for (long at = 0; at < WAYLAND_CONSTRAINTS; at++)
                if (c->constraints[at].live)
                {
                        c->constraints[at].live = 0;
                        wayland.constraints--;
                }
        for (long at = 0; at < WAYLAND_BUFFERS; at++)
                if (c->buffers[at].live)
                        wayland.buffers--;
        for (long at = 0; at < WAYLAND_OBJECTS; at++)
                if (c->objects[at].iface)
                        wayland.objects--;
        for (long at = 0; at < c->in_fd_count; at++)
        {
                wayland.host->close(wayland.host->context, c->in_fds[at]);
                wayland.fds--;
        }
        wayland_out_fds_let_go(c);
        for (long which = 0; which < 2; which++)
        {
                // A selection that was this client's stays, as text, and is nobody's; a copy it was in the middle of is ended.
                if (wayland.selection_owner[which] == c->slot + 1)
                        wayland.selection_owner[which] = 0;
                if (c->pending[which])
                        wayland.host->clip_abort(wayland.host->context, c->slot, which);
        }
        wayland.client[slot] = 0;
        wayland.clients--;
        wayland.host->release(wayland.host->context, c, (long)sizeof *c);
}

WAYLAND_API void wayland_stop(void)
{
        if (!wayland.host)
                return;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                wayland_drop(slot);
        wayland.host = 0;
}

/* What the client is: 0 alive, 1 sent a protocol error (flush, then drop), 2 cannot be sent more (drop). */
WAYLAND_API long wayland_dead(long slot)
{
        return slot >= 0 && slot < WAYLAND_CLIENTS && wayland.client[slot] ? wayland.client[slot]->dead : 2;
}

/*
        Bytes from a client, with the descriptors that came with them (which the shim owns from here, whatever
        happens). Any part of a message may arrive: the bytes of an unfinished one wait for the rest. A size below
        eight, above the largest or not a multiple of four leaves no boundary to find, so the client is dead; so is
        one whose request fails. Answers 0, or a negative number once the client is dead.
*/
/*
        A commit that answered WAYLAND_STALL and every byte from it on are kept in the client, from the start of the stash, and the
        client waits. head is a message that was completed in the client's own buffer, the rest what follows it; the rest may be the
        stash itself, being taken again (it is moved to the front, forward, so it does not run over what it still has to read).
*/
static void wayland_stall(struct wayland_client *c, const unsigned char *head, long head_size, const unsigned char *rest, long rest_length)
{
        if (head_size + rest_length > WAYLAND_STASH)
        {
                wayland_fail(c, 1, WAYLAND_NO_MEMORY, "more sent than can wait for a buffer");
                return;
        }
        for (long at = 0; at < head_size; at++)
                c->stash[at] = head[at];
        for (long at = 0; at < rest_length; at++)
                c->stash[head_size + at] = rest[at];
        c->stash_length = head_size + rest_length;
        c->stalled = 1;
        c->stalled_at = wayland.host->now(wayland.host->context);
}

/* The bytes of a client, a message at a time, until they are done or a commit has to wait. */
static int wayland_pump(struct wayland_client *c, const unsigned char *bytes, long length)
{
        int status = WAYLAND_OK;

        while (length > 0 && !c->dead && !c->stalled)
        {
                long size, take;

                if (c->in_length)
                {
                        // Completing a message begun before: its header first, then the rest of what it says.
                        take = c->in_length < 8 ? 8 - c->in_length : wayland_min(length, (long)(wayland_get(c->in + 4) >> 16) - c->in_length);
                        take = wayland_min(take, length);
                        wayland_copy(c->in + c->in_length, bytes, take);
                        c->in_length += take;
                        bytes += take;
                        length -= take;
                        if (c->in_length < 8)
                                continue;
                        size = (long)(wayland_get(c->in + 4) >> 16);
                        if (size < 8 || size > WAYLAND_MESSAGE || (size & 3))
                        {
                                c->in_length = 0;
                                status = wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "a message of an impossible size");
                                break;
                        }
                        if (c->in_length < size)
                                continue;
                        c->in_length = 0;
                        status = wayland_dispatch(c, c->in, size);
                        if (status == WAYLAND_STALL)
                        {
                                wayland_stall(c, c->in, size, bytes, length);
                                status = WAYLAND_OK;
                        }
                        continue;
                }
                if (length < 8)
                {
                        wayland_copy(c->in, bytes, length);
                        c->in_length = length;
                        break;
                }
                size = (long)(wayland_get(bytes + 4) >> 16);
                if (size < 8 || size > WAYLAND_MESSAGE || (size & 3))
                {
                        status = wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "a message of an impossible size");
                        break;
                }
                if (size > length)
                {
                        wayland_copy(c->in, bytes, length);
                        c->in_length = length;
                        break;
                }
                status = wayland_dispatch(c, bytes, size);
                if (status == WAYLAND_STALL)
                {
                        wayland_stall(c, bytes, 0, bytes, length);
                        status = WAYLAND_OK;
                        break;
                }
                bytes += size;
                length -= size;
        }
        return c->dead ? WAYLAND_DEAD : status;
}

WAYLAND_API int wayland_feed(long slot, const unsigned char *bytes, long length, const long *fds, long count)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;

        for (long at = 0; at < count; at++)
        {
                if (c && !c->dead && c->in_fd_count < WAYLAND_FDS)
                {
                        c->in_fds[c->in_fd_count++] = fds[at];
                        wayland.fds++;
                }
                else
                {
                        wayland.host->close(wayland.host->context, fds[at]);
                        if (c && !c->dead)
                                wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "too many descriptors");
                }
        }
        if (!c || c->dead)
                return WAYLAND_DEAD;
        if (c->stalled)
        {
                // A host that reads a client that waits for a buffer: what comes is kept behind what waits.
                if (c->stash_length + length > WAYLAND_STASH)
                        return wayland_fail(c, 1, WAYLAND_NO_MEMORY, "more sent than can wait for a buffer");
                wayland_copy(c->stash + c->stash_length, bytes, length);
                c->stash_length += length;
                return WAYLAND_OK;
        }
        return wayland_pump(c, bytes, length);
}

/* Whether a client waits for a buffer to be ready, which the host does not read it for, and asks wayland_resume of. */
WAYLAND_API long wayland_stalled(long slot)
{
        return slot >= 0 && slot < WAYLAND_CLIENTS && wayland.client[slot] && !wayland.client[slot]->dead && wayland.client[slot]->stalled;
}

/*
        When a stalled client is next worth asking of, on the host's clock (ms), and in fd the dma-buf it waits for, which the host
        may poll to be told the moment it is ready (-1 for none): a client that waits for its budget at the end of its window of time,
        one that waits for its GPU at the end of its time to finish. 0 for a client that does not wait.
*/
WAYLAND_API long wayland_stall_due(long slot, long *fd)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;

        *fd = -1;
        if (!c || c->dead || !c->stalled)
                return 0;
        if (c->hold)
                return c->hold_at; // a timed hold wakes the loop at its time; a barrier's is woken by the frames (wayland_frame_wanted)
        if (c->stalled_fd < 0)
                return c->budget_at + WAYLAND_BUDGET_MS;
        *fd = c->stalled_fd;
        return c->stalled_at + WAYLAND_STALL_MS + 1;
}

/*
        The commit a client waits for is made again if its buffer is ready now, and what the client sent after it is taken, which
        may be a commit that waits in its turn. Answers 1 while it is still waiting, else 0; a client whose buffer is not ready in
        WAYLAND_STALL_MS is ended, since its GPU is not going to finish.
*/
WAYLAND_API int wayland_resume(long slot)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;
        long length;

        if (!c || c->dead || !c->stalled)
                return 0;
        // A commit held for a barrier or a time is made again once wayland_hold_wanted says it is no longer held.
        if (c->hold)
        {
                struct wayland_surface *held = &c->surfaces[c->hold_surface];

                if (held->live && wayland_hold_wanted(c, held))
                        return 1;
                c->hold = 0;
        }
        // A commit that waits for its budget is made in the client's next window of time; one that waits for its GPU, when it is done.
        if (c->stalled_fd < 0 ? !wayland_budget_left(c)
                              : wayland.host->dmabuf_ready && !wayland.host->dmabuf_ready(wayland.host->context, c->stalled_fd))
        {
                if (c->stalled_fd >= 0 && wayland.host->now(wayland.host->context) - c->stalled_at > WAYLAND_STALL_MS)
                {
                        c->stalled = 0;
                        c->stash_length = 0;
                        wayland_fail(c, 1, WAYLAND_IMPLEMENTATION, "the buffer's writer never finished");
                        return 0;
                }
                return 1;
        }
        length = c->stash_length;
        c->stash_length = 0;
        c->stalled = 0;
        wayland_pump(c, c->stash, length);
        return c->stalled ? 1 : 0;
}

/*
        What waits to be sent to a client: its bytes, and the descriptors that go with them (the first byte's ancillary
        data). wayland_sent takes what was written off, from the front; a send that wrote any bytes sent the descriptors.
*/
WAYLAND_API long wayland_out(long slot, const unsigned char **bytes, const long **fds, long *fd_count)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;

        *bytes = 0;
        *fds = 0;
        *fd_count = 0;
        if (!c)
                return 0;
        *bytes = c->out;
        *fds = c->out_fds;
        *fd_count = c->out_fd_count;
        return c->out_length;
}

WAYLAND_API void wayland_sent(long slot, long count)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;

        if (!c || count <= 0 || count > c->out_length)
                return;
        for (long at = count; at < c->out_length; at++)
                c->out[at - count] = c->out[at];
        c->out_length -= count;
        wayland_out_fds_let_go(c);
}

/*
        The compositor's screens, changed (a scale, a mode, a place, a turn): every client is told the whole of each screen it
        bound, in the order a client waits for it. The xdg_output's logical position and size first, then the wl_output's
        geometry, mode, scale, name, description and done, which from version 3 applies the xdg_output's too; so a client
        sees the screen change once, all of it or none.
*/
WAYLAND_API void wayland_screens_changed(void)
{
        long was;

        if (!wayland.host)
                return;
        was = wayland.screens;
        wayland.screens = wayland.host->screens(wayland.host->context, wayland.screen, WAYLAND_SCREENS);
        if (wayland.screens < 0)
                wayland.screens = 0;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                //      A screen went: the surfaces on it leave it, and what the client bound of it is told nothing more (its number is
                //      nobody's: a screen that comes with it later is another global, and these are not it).
                for (long screen = wayland.screens; c && screen < was && screen < 8; screen++)
                {
                        for (long at = 0; at < WAYLAND_SURFACES; at++)
                        {
                                struct wayland_surface *surface = &c->surfaces[at];

                                if (!surface->live || !(surface->entered & (1u << screen)))
                                        continue;
                                for (long object = 0; object < WAYLAND_OBJECTS; object++)
                                        if (c->objects[object].iface == WLI_wl_output && c->objects[object].aux == screen)
                                        {
                                                struct wayland_event e = wayland_begin(c, surface->id, WLE_wl_surface_leave);

                                                wayland_word(&e, c->objects[object].id);
                                                wayland_end(&e);
                                        }
                                surface->entered &= (unsigned char)~(1u << screen);
                        }
                        for (long object = 0; object < WAYLAND_OBJECTS; object++)
                                if ((c->objects[object].iface == WLI_wl_output || c->objects[object].iface == WLI_zxdg_output_v1) &&
                                    c->objects[object].aux == screen)
                                        c->objects[object].aux = 0xffff;
                }
                //      Each registry a client holds says so, as the protocol has it: a screen that came is a global to bind, one that went
                //      is removed.
                for (long at = 0; c && at < WAYLAND_OBJECTS && wayland.screens != was; at++)
                        if (c->objects[at].iface == WLI_wl_registry)
                        {
                                for (long screen = was; screen < wayland.screens; screen++)
                                        wayland_global_send(c, c->objects[at].id, wayland_output_name(screen));
                                for (long screen = wayland.screens; screen < was; screen++)
                                {
                                        struct wayland_event e = wayland_begin(c, c->objects[at].id, WLE_wl_registry_global_remove);

                                        wayland_word(&e, (unsigned long)wayland_output_name(screen));
                                        wayland_end(&e);
                                }
                        }
                for (long at = 0; c && at < WAYLAND_OBJECTS; at++)
                        if (c->objects[at].iface == WLI_zxdg_output_v1)
                                wayland_xdg_output_send(c, at, c->objects[at].aux);
                for (long at = 0; c && at < WAYLAND_OBJECTS; at++)
                        if (c->objects[at].iface == WLI_wl_output)
                                wayland_output_send(c, at);
        }
        for (long screen = wayland.screens; screen < was && screen < WAYLAND_SCREENS; screen++)
                wayland.output_age[screen]++;
}

/*
        Every window is told the scale of the screen it is on, when that is another than it was told: the surfaces of the
        window (its subsurfaces too) their fractional scale and whole scale, and a toplevel the size the compositor asks of
        it in the new unit. A window that moved to another screen is told so (enter and leave). Once a pass, for each row a
        client holds; a row whose percent is what it was told costs a look at its facts.
*/
WAYLAND_API void wayland_scales_sweep(void)
{
        if (!wayland.host)
                return;
        for (long row = 0; row < WAYLAND_ROWS; row++)
        {
                struct wayland_client *c = wayland.row_client[row] >= 0 ? wayland.client[wayland.row_client[row]] : 0;
                struct wayland_surface *root = c ? &c->surfaces[wayland.row_surface[row]] : 0;
                struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};
                long percent;

                if (!root || !root->live || root->row != row || wayland.host->row_facts(wayland.host->context, (int)row, &facts) < 0)
                        continue;
                wayland_surface_screens(c, root, wayland_screen_at(facts.x, facts.y));
                percent = facts.scale > 0 ? facts.scale : 100;
                if (wayland.row_percent[row] == percent)
                        continue;
                wayland.row_percent[row] = percent;
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                        if (c->surfaces[at].live && wayland_root_of(c, &c->surfaces[at]) == root)
                                wayland_scale_tell(c, &c->surfaces[at], percent);
                if (root->role == WAYLAND_ROLE_TOPLEVEL && root->told && wayland_surface_unit(root) != root->told_unit &&
                    (facts.width > 0 || facts.height > 0))
                        wayland_configure(c, root, 0);
        }
}

/*
        A window's row knocked: what the compositor says of it now, against what its client was told. A close asked is
        said once (the toplevel's close, a popup's done); a size or a state that changed is a new configure, which a
        client with too many unacknowledged waits for.
*/
WAYLAND_API void wayland_row_wake(long row)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        struct wayland_facts facts;
        long slot;

        if (!wayland.host || row < 0 || row >= WAYLAND_ROWS || wayland.row_client[row] < 0)
                return;
        c = wayland.client[wayland.row_client[row]];
        s = c ? &c->surfaces[wayland.row_surface[row]] : 0;
        if (!s || !s->live || s->row != row || wayland.host->row_facts(wayland.host->context, (int)row, &facts) < 0)
                return;
        if (facts.closing && !(s->flags & WAYLAND_SURFACE_CLOSED))
        {
                struct wayland_event e;

                s->flags |= WAYLAND_SURFACE_CLOSED;
                slot = wayland_find(c, s->role_id, s->role == WAYLAND_ROLE_POPUP ? WLI_xdg_popup : WLI_xdg_toplevel);
                if (slot >= 0)
                {
                        e = wayland_begin(c, s->role_id, s->role == WAYLAND_ROLE_POPUP ? WLE_xdg_popup_popup_done : WLE_xdg_toplevel_close);
                        wayland_end(&e);
                }
                return;
        }
        if (s->role == WAYLAND_ROLE_TOPLEVEL && s->told &&
            (facts.width != s->told_width || facts.height != s->told_height || facts.states != s->told_states))
                wayland_configure(c, s, 0);
}

/*
        A frame is on the screen: the callbacks that waited for one are answered, for every window that is shown and
        not put away. A window put away is not told of frames, and is told of the next the moment it is shown.
*/
WAYLAND_API void wayland_frame(void)
{
        long now;

        if (!wayland.host)
                return;
        wayland.frames++;
        now = wayland.host->now(wayland.host->context);
        for (long row = 0; row < WAYLAND_ROWS; row++)
        {
                struct wayland_client *c = wayland.row_client[row] >= 0 ? wayland.client[wayland.row_client[row]] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row_surface[row]] : 0;
                struct wayland_facts facts;

                int any = s && s->live && s->readies;

                for (long at = 0; s && s->live && at < WAYLAND_SURFACES && !any; at++)
                        any = c->surfaces[at].live && c->surfaces[at].readies && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE &&
                              wayland_root_of(c, &c->surfaces[at]) == s;
                if (!any)
                        continue;
                if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                        continue;
                wayland_callbacks_done(c, s, now);
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                        if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE && wayland_root_of(c, &c->surfaces[at]) == s)
                                wayland_callbacks_done(c, &c->surfaces[at], now);
        }
}

/*
        The feedbacks one surface's commits took, answered: presented at when (nanoseconds on the monotonic clock, split as the
        protocol has it into seconds and nanoseconds), refreshing every refresh nanoseconds, the frame's count seq. Each object is
        destroyed by its event, so it is let go with it.
*/
static void wayland_presentings_done(struct wayland_client *c, struct wayland_surface *s, unsigned long when, unsigned long refresh,
                                     unsigned long seq, unsigned long flags)
{
        unsigned long seconds = when / 1000000000ul, nanoseconds = when % 1000000000ul;

        for (long at = 0; at < s->presentings; at++)
        {
                long slot = wayland_find(c, s->presenting[at], WLI_wp_presentation_feedback);

                if (slot < 0)
                        continue;
                {
                        unsigned int id = s->presenting[at];
                        struct wayland_event e = wayland_begin(c, id, WLE_wp_presentation_feedback_presented);

                        wayland_word(&e, (unsigned long)(seconds >> 32));
                        wayland_word(&e, (unsigned long)(seconds & 0xffffffffu));
                        wayland_word(&e, nanoseconds);
                        wayland_word(&e, refresh);
                        wayland_word(&e, (unsigned long)(seq >> 32));
                        wayland_word(&e, (unsigned long)(seq & 0xffffffffu));
                        wayland_word(&e, flags); // the presentation flags the compositor can vouch for (vsync, hw_clock, hw_completion)
                        wayland_end(&e);
                        wayland_object_delete(c, slot);
                }
        }
        s->presentings = 0;
}

/*
        A frame was drawn and flipped to the screen (wayland_presented's when, refresh and seq, as above): the feedbacks its
        commits took are answered, for every window that is shown and not put away. Called only for a frame that was drawn, so
        a timer's frame with nothing new answers none.
*/
WAYLAND_API void wayland_presented(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags)
{
        if (!wayland.host)
                return;
        for (long row = 0; row < WAYLAND_ROWS; row++)
        {
                struct wayland_client *c = wayland.row_client[row] >= 0 ? wayland.client[wayland.row_client[row]] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row_surface[row]] : 0;
                struct wayland_facts facts;

                if (!s || !s->live)
                        continue;
                if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                        continue;
                wayland_presentings_done(c, s, when, refresh, seq, flags);
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                        if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE && wayland_root_of(c, &c->surfaces[at]) == s)
                                wayland_presentings_done(c, &c->surfaces[at], when, refresh, seq, flags);
        }
}

/* Whether any callback waits for a frame, so the compositor knows to come back for one. */
WAYLAND_API long wayland_frame_wanted(void)
{
        // A commit held for a barrier is waited for a frame at a time: each frame drawn is the one that clears it.
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (wayland.client[slot] && wayland.client[slot]->stalled && wayland.client[slot]->hold && !wayland.client[slot]->hold_at)
                        return 1;
        for (long row = 0; wayland.host && row < WAYLAND_ROWS; row++)
        {
                struct wayland_client *c = wayland.row_client[row] >= 0 ? wayland.client[wayland.row_client[row]] : 0;

                if (!c)
                        continue;
                if (c->surfaces[wayland.row_surface[row]].readies)
                        return 1;
                for (long at = 0; at < WAYLAND_SURFACES; at++)
                        if (c->surfaces[at].live && c->surfaces[at].readies && c->surfaces[at].role == WAYLAND_ROLE_SUBSURFACE &&
                            wayland_root_of(c, &c->surfaces[at]) == &c->surfaces[wayland.row_surface[row]])
                                return 1;
        }
        return 0;
}

/*
        What the window in row was given as hints, for the compositor to read: the alpha multiplier its surface shows (opaque when
        none), its content type, and whether its dialog is modal. A row with no window is opaque, of no content type, not modal.
*/
WAYLAND_API unsigned long wayland_row_alpha(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->alpha : 0xffffffffu;
}

WAYLAND_API long wayland_row_content(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? (long)s->content : WLN_wp_content_type_v1_type_none;
}

/*
        Whether the window's commit waits for the barrier (wp_fifo), and the time its commit is for (wp_commit_timing, nanoseconds
        on the monotonic clock, 0 for none). The compositor shows a commit at its next frame whatever these say, so they are read
        for what they are and not yet for a hold: a frame is already the barrier, and a commit cannot be held to a future time.
*/
WAYLAND_API long wayland_row_fifo(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->fifo_wait : 0;
}

WAYLAND_API unsigned long wayland_row_timestamp(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->timestamp : 0;
}

/*
        zwp_pointer_gestures_v1 (pointer-gestures, version 3). A pinch object is asked for per pointer: the client's windows take the
        pinch from then on (the compositor sends a window that does not take it Ctrl and a wheel). Swipe and hold are accepted and
        never sent: the compositor has no swipe or hold gesture to send.
*/
static int wayland_gestures_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        (void)a;
        if (self->iface == WLI_zwp_pointer_gestures_v1 && op == WLQ_zwp_pointer_gestures_v1_get_pinch_gesture)
                for (long at = 0; at < WAYLAND_ROWS; at++)
                        if (wayland.row_client[at] == c->slot)
                                wayland.host->row_do(wayland.host->context, (int)at, WAYLAND_DO_PINCH_WANT, 1, 0);
        return WAYLAND_OK;
}

/*
        A pinch of the pointer's window (the host's WINDOW_EVENT_PINCH): phase 1 begins it, 0 updates it, 2 ends it. scale is in
        1024ths of the span it began at (1024 is 1.0), (fx, fy) the focal point in the row's pixels. Every pinch object of the
        pointer's client is sent it: begin with the fingers (two), an update with the focal point's move and the scale (fixed, 1.0
        is 256), no rotation, and an end that is not cancelled.
*/
WAYLAND_API void wayland_pointer_pinch(long row, long phase, long scale, long fx, long fy)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s;
        long time, dx = 0, dy = 0;
        unsigned long serial = 0;

        if (!wayland.host || row != wayland.pointer_row || !(s = wayland_row_surface(row, &c)))
                return;
        time = wayland.host->now(wayland.host->context);
        if (phase == 1)
        {
                wayland.pinch_x = fx;
                wayland.pinch_y = fy;
                serial = wayland_input_serial(c);
        }
        else if (phase == 2)
                serial = wayland_input_serial(c);
        else
        {
                dx = fx - wayland.pinch_x;
                dy = fy - wayland.pinch_y;
                wayland.pinch_x = fx;
                wayland.pinch_y = fy;
        }
        for (long at = 0; at < WAYLAND_OBJECTS; at++)
        {
                struct wayland_object *o = &c->objects[at];

                if (o->iface != WLI_zwp_pointer_gesture_pinch_v1 || o->id == 0)
                        continue;
                if (phase == 1)
                {
                        struct wayland_event e = wayland_begin(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_begin);

                        wayland_word(&e, serial);
                        wayland_word(&e, (unsigned long)time);
                        wayland_word(&e, s->id);
                        wayland_word(&e, 2);
                        wayland_end(&e);
                }
                else if (phase == 2)
                {
                        struct wayland_event e = wayland_begin(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_end);

                        wayland_word(&e, serial);
                        wayland_word(&e, (unsigned long)time);
                        wayland_word(&e, 0);
                        wayland_end(&e);
                }
                else
                {
                        struct wayland_event e = wayland_begin(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_update);

                        wayland_word(&e, (unsigned long)time);
                        wayland_word(&e, wayland_fixed(dx, s->scale120));
                        wayland_word(&e, wayland_fixed(dy, s->scale120));
                        wayland_word(&e, (unsigned long)(scale > 0 ? scale / 4 : 0)); // 1024ths to fixed (256 is 1.0)
                        wayland_word(&e, 0);
                        wayland_end(&e);
                }
        }
}

/* Whether a window was asked for by an xdg-activation it was not vouched for (it wants attention, until it is focused). */
WAYLAND_API long wayland_row_attention(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->attention : 0;
}

/* The window of the surface whose xwayland_surface_v1 gave this X11 serial: its row, -1 while it has none, -2 when no surface has the serial. */
WAYLAND_API long wayland_serial_row(unsigned long long serial)
{
        for (long at = 0; at < WAYLAND_CLIENTS; at++)
        {
                struct wayland_client *c = wayland.client[at];

                for (long slot = 0; c && slot < WAYLAND_SURFACES; slot++)
                {
                        struct wayland_surface *s = &c->surfaces[slot];

                        if (s->live && s->xserial_set && s->xserial == serial)
                                return s->row >= 0 && s->row < WAYLAND_ROWS ? s->row : -1;
                }
        }
        return -2;
}

/* Whether a window has a keyboard shortcuts inhibitor, and whether its tearing hint is asynchronous (allowed to tear). */
WAYLAND_API long wayland_row_shortcuts(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s && (s->shortcuts || s->xgrab) ? s->shortcut_active : 0;
}

/*
        The keyboard shortcuts inhibitor's active state follows the keyboard: it becomes active when its surface gets the keys (and says
        so, with active), and inactive when the keys go (and says so, with inactive). Nothing else sends them.
*/
static void wayland_shortcuts_send(struct wayland_client *c, struct wayland_surface *s, int active)
{
        long slot = wayland_find(c, s->shortcut_id, WLI_zwp_keyboard_shortcuts_inhibitor_v1);

        s->shortcut_active = (unsigned char)active;
        s->escape_down = 0;
        if (slot >= 0)
        {
                struct wayland_event e = wayland_begin(c, s->shortcut_id, active ? WLE_zwp_keyboard_shortcuts_inhibitor_v1_active
                                                                                 : WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive);

                wayland_end(&e);
        }
}

// Keys said to the compositor's chords, for the window that has them: whether a shortcuts inhibitor is active there. Where the
// keys are taken from a chord, these are the answers (wayland_shortcuts_key).
#define WAYLAND_KEY_ESC 1                 // the Linux code of Escape
#define WAYLAND_ESCAPE_MS 1000            // how long Super and Escape are held to give the keys back
#define WAYLAND_KEYS_CHORDS 0             // the compositor's chords run
#define WAYLAND_KEYS_CLIENT 1             // the window has the keys (a shortcut inhibitor): the compositor's chords do not run
#define WAYLAND_KEYS_ESCAPE 2             // Super and Escape, the one chord the inhibitor keeps: it is the compositor's, and nothing else gets it

/*
        What a key does to the compositor's chords, for the window with the keys in row. An active shortcuts inhibitor suspends them, so the
        key is the window's: WAYLAND_KEYS_CLIENT. The one chord kept is Super with Escape, held for WAYLAND_ESCAPE_MS: its press and its
        release are the compositor's (WAYLAND_KEYS_ESCAPE, and no client sees them), and a hold as long as that gives the keys back, the
        inhibitor is inactive until the keys next come to its window, and it is sent inactive. Escape with no Super is the window's.
        super is 1 while the logo key is held.
*/
WAYLAND_API long wayland_shortcuts_key(long row, long code, long value, int super)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;
        long now;

        if (!s || !(s->shortcuts || s->xgrab) || !s->shortcut_active)
                return WAYLAND_KEYS_CHORDS;
        if (code == WAYLAND_KEY_ESC && value == 1 && super && !s->escape_down)
        {
                s->escape_down = 1;
                s->escape_at = wayland.host->now(wayland.host->context);
                return WAYLAND_KEYS_ESCAPE;
        }
        if (code == WAYLAND_KEY_ESC && s->escape_down)
        {
                if (value == 0)
                {
                        now = wayland.host->now(wayland.host->context);
                        s->escape_down = 0;
                        if (now - s->escape_at >= WAYLAND_ESCAPE_MS)
                                wayland_shortcuts_send(c, s, 0);
                }
                return WAYLAND_KEYS_ESCAPE;
        }
        return WAYLAND_KEYS_CLIENT;
}

WAYLAND_API long wayland_row_async(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->async_hint : 0;
}

/*
        Whether any client holds an idle inhibitor. The compositor has no idle timer to hold back yet: when one exists it must not
        blank or suspend the screen while this is 1.
*/
WAYLAND_API long wayland_idle_inhibited(void)
{
        return wayland_inhibitors_now();
}

/* The compositor gave the shim an input event (a key, a button, a motion, a finger): the idle clock starts again from now. */
WAYLAND_API void wayland_input_seen(void)
{
        if (wayland.host)
                wayland.input_at = wayland.host->now(wayland.host->context);
}

/*
        Runs the idle notifications: each one that has been given no input for its timeout (and no inhibitor holds it) goes idle, and each
        one that is idle and has input since sends resumed. Called on each pass of the compositor, so it is current when a frame is.
*/
WAYLAND_API void wayland_idle_tick(void)
{
        long now;
        int inhibited = -1;

        if (!wayland.host)
                return;
        now = wayland.host->now(wayland.host->context);
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c || c->dead)
                        continue;
                for (long at = 0; at < WAYLAND_NOTIFIES; at++)
                {
                        struct wayland_notify *n = &c->notifies[at];
                        long base;

                        if (!n->live)
                                continue;
                        if (n->idle)
                        {
                                if (wayland.input_at > n->idled)
                                {
                                        n->idle = 0;
                                        n->since = wayland.input_at;
                                        wayland_notify_say(c, n, WLE_ext_idle_notification_v1_resumed);
                                }
                                continue;
                        }
                        if (!n->input_only && inhibited < 0)
                                inhibited = wayland_inhibitors_now() > 0;
                        base = wayland.input_at > n->since ? wayland.input_at : n->since;
                        if ((!n->input_only && inhibited) || now - base < n->timeout)
                                continue;
                        n->idle = 1;
                        n->idled = now;
                        wayland_notify_say(c, n, WLE_ext_idle_notification_v1_idled);
                }
        }
}

/*
        When the next idle notification may go idle, on the shim's clock (ms), or 0 for none: a notification that is not idle and is not
        held by an inhibitor. The compositor's loop wakes for it, so an idle desktop still goes idle on time.
*/
WAYLAND_API long wayland_idle_due(void)
{
        long due = 0;
        int inhibited = -1;

        if (!wayland.host)
                return 0;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c || c->dead)
                        continue;
                for (long at = 0; at < WAYLAND_NOTIFIES; at++)
                {
                        struct wayland_notify *n = &c->notifies[at];
                        long base, when;

                        if (!n->live || n->idle)
                                continue;
                        if (!n->input_only && inhibited < 0)
                                inhibited = wayland_inhibitors_now() > 0;
                        if (!n->input_only && inhibited)
                                continue;
                        base = wayland.input_at > n->since ? wayland.input_at : n->since;
                        when = base + n->timeout;
                        if (!due || when < due)
                                due = when;
                }
        }
        return due;
}

/* What the idle clock says, for the moonwater debug line: the shim's ms of the last input, the inhibitors that hold, the notifications idle. */
WAYLAND_API void wayland_idle_state(long *input_ms, long *inhibitors, long *idled)
{
        *input_ms = wayland.host ? wayland.input_at : 0;
        *inhibitors = wayland_inhibitors_now();
        *idled = 0;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
                if (wayland.client[slot] && !wayland.client[slot]->dead)
                        for (long at = 0; at < WAYLAND_NOTIFIES; at++)
                                *idled += wayland.client[slot]->notifies[at].live && wayland.client[slot]->notifies[at].idle;
}

/* Whether a window has an icon set on its toplevel (xdg-toplevel-icon), as its commit showed it. */
WAYLAND_API long wayland_row_icon(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->icon : 0;
}

/* How many xdg_system_bell rings and xdg-toplevel-icon sets there have been, over every client (for the moonwater debug line). */
WAYLAND_API void wayland_hint_counts(long *bells, long *icons)
{
        *bells = wayland.bells;
        *icons = wayland.icons;
}

WAYLAND_API long wayland_row_modal(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < WAYLAND_ROWS ? wayland_row_surface(row, &c) : 0;

        return s ? s->modal : 0;
}

/* The client a row belongs to, or -1: the compositor asks before it treats a row as a program's. */
WAYLAND_API long wayland_row_owner(long row)
{
        return row >= 0 && row < WAYLAND_ROWS ? wayland.row_client[row] : -1;
}

#endif /* WAYLAND_INCLUDED */
