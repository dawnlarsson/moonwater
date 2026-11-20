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
        on (on by default). It is not kernel code: nothing here may be built into the module, and the file
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
// A client's ids run from 1 up to as many as it has made, as libwayland's do: a new id is the next or one it let go of.
#define WAYLAND_IDS 0xff000000UL
/*
        A client's objects, surfaces, pools, buffers, positioners and regions, and the index of its ids, are tables that grow
        (struct wayland_table): no count of them is a cap, and what bounds them is the machine's memory. Xwayland is one client for
        every X window of the desktop, and the fixed tables that were here (2048 objects, 64 then 256 surfaces, 96 then 512 pools,
        256 then 1024 buffers, 8 positioners, 64 regions) ended it, and every X program with it, at the count a desktop reached.
*/
#define WAYLAND_MESSAGE 4096    // the largest message, header included, as in libwayland
#define WAYLAND_ARGS 12
#define WAYLAND_FDS 28          // descriptors a client may have sent and not yet had used
#define WAYLAND_OUT 65536       // a client's output queue as it is made; it grows (wayland_out_grow) to WAYLAND_OUT_MOST
#define WAYLAND_OUT_MOST (4L << 20) // the most a queue grows to: a client that has read none of 4 MiB of its events is not reading
#define WAYLAND_OUT_HIGH (1L << 20) // a queue past this is the host's word to read no more of the client until it has drained some
#define WAYLAND_OUT_FDS 8       // descriptors one send carries (the host's control message takes that many); the queue holds any number, sent in turn
#define WAYLAND_EVENT 1024      // the most one event can take
#define WAYLAND_DAMAGE 8
#define WAYLAND_CALLBACKS 32     // frame callbacks a surface holds in itself; past them a list that grows (struct wayland_idlist)
#define WAYLAND_PRESENTATIONS 4  // presentation feedbacks a surface holds in itself for its next commits; past them likewise
#define WAYLAND_SERIALS 8       // configures sent and not yet acknowledged
#define WAYLAND_TEXT 128        // a title or an app id, its NUL included
// A pool's size is the protocol's int, so 2 GiB less a byte at most; what a client has mapped is the address space's and the file's,
// whose memory is the client's: a map the kernel refuses is the one refusal. Where a pool was held to 1 GiB and a client to 2 GiB.
#define WAYLAND_POOL_BYTES 0x7fffffffL
#define WAYLAND_DIMENSION 16384
#define WAYLAND_SERVER_IDS 0x01000000UL // the server's own ids, from 0xff000000 (the protocol's range): its index grows with what it makes
#define WAYLAND_ID_GAP 4096     // how far past the next id a client's new one may be (libwayland's servers allow none; a gap costs index, not more)
#define WAYLAND_SERVER_BASE 0xff000000UL
#define WAYLAND_SCREENS 32 // the screens a card drives at once (its CRTCs: the kernel's 32-bit masks), as window.c's WINDOWS_OUTPUTS
#define WAYLAND_GLOBALS_MAX 40
#define WAYLAND_TOUCHES 10      // fingers down at once over every client (wl_touch's points)
#define WAYLAND_TOKEN_MS 10000  // how long a given token may be used (its serial must still be a recent input of its client)
#define WAYLAND_KEYMAP_MAX (1L << 20)
#define WAYLAND_INPUT_SERIALS 8 // serials of input events a client may name to set a selection
#define WAYLAND_CLIP_CLIPBOARD 0
#define WAYLAND_CLIP_PRIMARY 1
#define WAYLAND_PICTURE_LENT 2                 // row_picture's answer: the host reads the buffer in place and keeps it (see struct wayland_host)
#define WAYLAND_DMABUF_ENTRIES 64    // format and modifier pairs the compositor takes
#define WAYLAND_DMABUF_TRANCHES 4
#define WAYLAND_REGION_RECTS 8       // exact disjoint rectangles tracked per region before bounding fallback
#define WAYLAND_FOURCC_ARGB8888 0x34325241UL
#define WAYLAND_FOURCC_XRGB8888 0x34325258UL
#define WAYLAND_FOURCC_ABGR8888 0x34324241UL
#define WAYLAND_FOURCC_XBGR8888 0x34324258UL
#define WAYLAND_FOURCC_ARGB2101010 0x30335241UL
#define WAYLAND_FOURCC_XRGB2101010 0x30335258UL
#define WAYLAND_FOURCC_ABGR2101010 0x30334241UL
#define WAYLAND_FOURCC_XBGR2101010 0x30334258UL
#define WAYLAND_FOURCC_NV12 0x3231564eUL
#define WAYLAND_MODIFIER_INVALID 0x00ffffffffffffffUL
#define WAYLAND_DMABUF_UNMAPPED ((void *)1ul)
#define WAYLAND_SYNCOBJ_TIMELINE_OK 1L
#define WAYLAND_SYNCOBJ_NODE_OK 2L

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
        A commit is also made to wait when its window has had more pictures copied than its share of a frame: what a picture costs
        the compositor is its pixels, copied at the commit, and a commit is 52 bytes, so a client that sent 64 KB of commits of a
        window the size of a 4K screen held the compositor (every window, the pointer, the keys) for 4.5 s a read. A window may have
        WAYLAND_BUDGET_SCREENS of the largest screen copied in WAYLAND_BUDGET_MS, which a window that draws every frame of a screen at
        twice 60 Hz keeps within; the commit past it waits, with the rest of what its client sent, until the window's next window of
        time. A window whose budget is untouched always has its commit made, so one picture larger than the budget is never refused.
        The budget is the window's (its root surface's), not the client's: the compositor's rows are its windows whoever's they
        are, so what all of them may cost it is the same, and a client's one busy window does not use up what its others may draw
        (a browser is one client and many windows). Its commit that waits still holds the client's stream behind it: the requests
        of one connection are made in the order they came.
*/
#define WAYLAND_BUDGET_MS 16
#define WAYLAND_BUDGET_SCREENS 8

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
        and the popup's place in the parent's picture; for a toplevel the row of the window it is for, -1 for none, and for
        WAYLAND_KIND_PLACED its place on the desktop) and answers it or a negative number, row_close lets it go, row_picture copies a buffer's changed rows
        into it and tells the compositor the picture is new, row_title gives the tab's text, and row_do asks it
        for something (WAYLAND_DO_*). row_facts reads what the compositor says of the window.
*/
#define WAYLAND_XWAIT_MS 30       // a commit of an X window waits this long for its window manager to say what the window is
#define WAYLAND_KIND_TOPLEVEL 1
#define WAYLAND_KIND_POPUP 2
#define WAYLAND_KIND_PLACED 3 // a toplevel at a place of its own (x, y): an X window that hard-set it, or an override-redirect one alone
// wayland_xwindow_adopt's popup word: an override-redirect window, an X window that hard-set its place, and the row (+ 1) of the
// window it is transient for, in the bits above.
#define WAYLAND_XWINDOW_POPUP 1L
#define WAYLAND_XWINDOW_PLACED 2L
#define WAYLAND_XWINDOW_OWNER_SHIFT 8
#define WAYLAND_XWINDOW_OWNER(row) ((row) >= 0 ? ((long)(row) + 1) << WAYLAND_XWINDOW_OWNER_SHIFT : 0L)

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
#define WAYLAND_DO_TEXT 12       // a text field of the window has the keys (a = 1), gives them back (a = -1), or says its content purpose (a = 0): b is the purpose
#define WAYLAND_DO_CARET 13      // the bottom of the text cursor, in the window's pixels (a; 0 for none): the on-screen keyboard keeps it above its top
#define WAYLAND_DO_PLACE 14      // move an already-mapped popup (a, b are its x and y relative to its parent)
#define WAYLAND_DO_LAYER 15      // set window stacking layer a (-1..2) on output b (zwlr_layer_shell_v1)
#define WAYLAND_DO_MIN_SIZE 16   // xdg_toplevel.set_min_size (a, b in logical pixels)
#define WAYLAND_DO_MAX_SIZE 17   // xdg_toplevel.set_max_size (a, b in logical pixels)
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
        // The mapping the pixels are in (a pool's, or a dma-buf's CPU mapping) and its size: a picture the host may read where it is
        // (WAYLAND_PICTURE_LENT) names the whole of it, and is 0 and 0 for a picture of pixels that are not a client's buffer.
        const unsigned char *map;
        long map_bytes;
        // Whether the buffer may not hold the window's whole picture outside the damage (1): the host then reads it in place only when
        // all of it is damaged. 0 for a client held to the protocol, whose buffer is the surface's whole content at every commit and
        // whose damage says only where it differs from the last, so a part damaged is read in place too and only that part is drawn again.
        long fragile;
        // Surface alpha/format transparency hint for the whole window (1 when window has alpha not covered by opaque_region).
        long transparent;
        // Where the window is opaque all the same (transparent 1): the rectangle of its opaque region it is sure of, in the window's pixels
        // (cut to the window, rounded inward), 0 wide where none is known. A toolkit's window with round corners is opaque but for them.
        long opaque_x, opaque_y, opaque_w, opaque_h;
        // Buffer transform (wl_output_transform 0..7) and y_invert flag (zwp_linux_buffer_params_v1 flags & 1).
        long transform, y_invert;
        // Viewport source rectangle in 1/256 buffer-local surface pixels (src_width 0 for whole buffer) and scaled destination pixels (0 for none).
        long src_x, src_y, src_width, src_height;
        long dst_width, dst_height;
        // DRM fourcc and modifier when dma-buf (or 0 to infer from format / linear), and monotonic pool identity.
        unsigned int fourcc;
        unsigned long modifier;
        unsigned long pool_id;
        // A toplevel's size is its client's own choice (1): every configure it was sent is acknowledged and the last one said what the
        // compositor asks of it now, so a size other than that ask is one the client chose (a browser that resizes itself), not one it is
        // still on its way from. The host lets a floating window take it (compositor_wl_row_picture).
        long own_size;
        // The commit was synchronized explicitly (wp_linux_drm_syncobj: an acquire point and a release point): the shim held it until its
        // acquire point was reached, so the client's writes to the buffer are done, and the buffer's implicit fences are no word on it.
        long explicit_sync;
        // Extra planes for multi-plane or planar dma-bufs (plane 0 duplicates sync_fd when >= 0).
        long plane_count;
        long plane_fd[4];
        unsigned long plane_offset[4], plane_stride[4];
        // A subsurface's picture handed as a layer of its window (row_layers): its surface's index and one, and whether it is new since
        // the layers were last handed (1: damage and full say where, as above; 0: the picture the layer had, at the place it says now).
        long layer, changed;
};

/*
        Optional host callbacks for explicit DRM syncobj timelines and wl_drm render-node advertisement. Kept separate from
        struct wayland_host so existing positional initializers remain untouched.
*/
struct wayland_syncobj_host
{
        void *context;
        long (*available)(void *context, char *node, long node_cap);
        long (*import)(void *context, long fd);
        void (*drop)(void *context, long handle);
        int (*ready)(void *context, long handle, unsigned long long point);
        void (*signal)(void *context, long handle, unsigned long long point);
        long (*eventfd)(void *context, long handle, unsigned long long point);
};

struct wayland_facts
{
        long width, height; // what the compositor asks of the window, 0 for the client's own choice
        long states;        // WAYLAND_STATE_*
        long closing;       // how many times the compositor has asked the window to go (0: never; never 0 again once asked): each ask is said once
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
        // row_picture: 0 for a picture copied into the row (the buffer is released by the commit), WAYLAND_PICTURE_LENT when the host reads
        // the buffer where it is and keeps it: the shim holds the buffer until the picture is replaced and the frame that shows the
        // replacement is drawn (wayland_presented), 1 for a buffer not read yet, -1 for a refusal.
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
        // The clipboard, which the compositor holds as a copy of any types (which is WAYLAND_CLIP_*). clip_type is the nth type a
        // selection is offered as (0 past the last, or for none and while the service is off). clip_pull begins the read of a client's
        // source (1, or 0 for a copy it will not take): the host asks for each type it wants (wayland_clip_type, wayland_clip_send with
        // a descriptor whose other end it reads on its own), makes the selection of what came and says so (wayland_selection_changed,
        // then wayland_clip_pulled); a descriptor sent is the shim's, closed once it is sent (wayland_sent) or its client is gone, so an
        // abort never closes a number still waiting in the queue. clip_abort ends a pull. clip_push writes a type of the selection to a
        // descriptor a client gave for the window row, and takes the descriptor whatever comes of it. clip_clear empties the selection
        // when the client holds it.
        // dma-bufs. dmabuf_info is what is taken (0 for nothing, which keeps zwp_linux_dmabuf_v1 from being offered), dmabuf_table the
        // file of format and modifier pairs the feedback names (the host's: the shim never closes it, -1 for none), dmabuf_map a
        // descriptor that is a dma-buf of at least size bytes, mapped read-only (0 for a refusal; the descriptor stays the caller's), or
        // for a size below zero (a buffer of tiles, -size bytes) only found to be one the host's card reads: WAYLAND_DMABUF_UNMAPPED or 0.
        // The pointer held by a client's window: mode 1 is locked (the hand's counts are the client's, unaccelerated, through
        // wayland_pointer_relative, and the pointer is not drawn or moved), 2 is confined to the window, 0 is free. One at a time.
        void (*pointer_hold)(void *context, long row, long mode);
        long (*dmabuf_info)(void *context, struct wayland_dmabuf_info *info);
        long (*dmabuf_table)(void *context, long *size);
        void *(*dmabuf_map)(void *context, long fd, long size);
        const char *(*clip_type)(void *context, long which, long nth);
        long (*clip_pull)(void *context, long client, long which);
        void (*clip_abort)(void *context, long client, long which);
        void (*clip_push)(void *context, long row, long which, const char *mime, long fd);
        void (*clip_clear)(void *context, long client, long which);
        // Whether a dma-buf has no writer now (its GPU is done), asked and never waited for; a host with none says every buffer is ready.
        int (*dmabuf_ready)(void *context, long fd);
        int (*random)(void *context, unsigned char *into, long bytes); // fills bytes with random bytes: 1 when it did
        // row_layers: the subsurfaces of a window as layers of their own over its picture, bottom first, count pictures each at its place in
        // the window (with layer and changed), read where their client keeps them: WAYLAND_LAYERS_KEPT when the host keeps them, and the
        // shim holds each one's buffer as a lent picture's until layers that leave it out are kept or refused and the frame after is drawn;
        // 0 when it does not, having laid any it kept into the window's picture (the shim then lays them there as it did); WAYLAND_LAYERS_ROOT
        // when it keeps them only with the window's own picture again (the picture it has holds older ones laid in it). root 1: the window's
        // own picture follows at once (row_picture with last), and the layers are over it. A count of -1 is no layers: any kept are laid into
        // the picture. Optional, and last, so a host's positional initializer without it is one that lays every subsurface in its window.
        int (*row_layers)(void *context, int row, const struct wayland_picture *layers, long count, long root);
        // row_busy: 1 while a job of the card that reads the row's pictures where their clients keep them may still run (the present does not wait
        // for its jobs); a buffer such a picture was read from is then not released at a commit or a close but at the next frame drawn or presented
        // (wayland_drawn, wayland_presented), which the host calls once its jobs are done. The client is read all along. Optional: none is never busy.
        int (*row_busy)(void *context, int row);
        // The tables' memory (struct wayland_table): reserve is address space of *bytes, which the host may make less (to the machine's
        // memory), of no memory until committed, or 0; commit makes bytes of it from at usable, zeroed, 1 when it did and 0 when the
        // memory is not there; unreserve gives a reservation back. Optional, together: a host with none has its tables made of alloc,
        // a block of WAYLAND_TABLE_FALLBACK bytes each.
        void *(*reserve)(void *context, long *bytes);
        int (*commit)(void *context, void *at, long bytes);
        void (*unreserve)(void *context, void *at, long bytes);
        // The bridge to X (the window manager of Xwayland, xwm.c), optional. xreceive: a receive the X side answers, of a type of a drag
        // an X window makes (which 2), for the client of that slot; the descriptor is the host's from then on.
        void (*xreceive)(void *context, long client, long which, const char *mime, long fd);
        // xdrag: a Wayland drag over a row whose client has no data device (an X window) is offered to the X side: WAYLAND_XDRAG_OVER at
        // (x, y) of the row, answered 1 when the row is a target the X side takes (and it says what it takes with wayland_drag_x_status),
        // WAYLAND_XDRAG_LEAVE, WAYLAND_XDRAG_DROP; and WAYLAND_XDRAG_END (x: 1 finished, 0 not) when a drag of the X side's ends, or a
        // Wayland one that was over an X window.
        long (*xdrag)(void *context, long what, long row, long x, long y);
        // row_app: the app_id a toplevel says, for its row (what the panel matches a window to its app by), as it sets it and as its
        // row opens with one; none (a host that does not ask) is no call.
        void (*row_app)(void *context, int row, const char *app_id);
};
#define WAYLAND_XDRAG_OVER 1
#define WAYLAND_XDRAG_LEAVE 2
#define WAYLAND_XDRAG_DROP 3
#define WAYLAND_XDRAG_END 4

#define WAYLAND_LAYERS 8        // the most subsurfaces a window is handed to the host with as layers (row_layers)
#define WAYLAND_LAYERS_KEPT 1
#define WAYLAND_LAYERS_ROOT 2

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
        int link;
        // A surface's generation for the objects that name a surface (all of it: a slot made again 65536 times had every new object
        // of it taken for a stale one when this was 16 bits), else the object's own small word (a wl_output's screen).
        unsigned int aux;
};

/*
        A table that grows, and whose elements never move: one reservation of address space (the host's reserve, untouched and of no
        memory), of which as much is committed (the host's commit) as the elements made need, a step at a time. An element's address
        is the reservation's start and its index, so a pointer to an element taken before the table grew is good after it, and no
        lookup is a scan. The elements a client let go of are on a stack in the same reservation, taken again first. What a table may
        hold is what the reservation has room for, which is as much as the machine's memory (the compositor's host makes it so): a
        commit that fails is the one request refused, with the protocol's no_memory, and nothing else.
*/
struct wayland_table
{
        unsigned char *at;     // the reservation (0 before the first element)
        long unit;             // an element's bytes
        long count;            // the elements ever made: every index below this has been one
        long usable;           // elements whose memory is committed
        long reserved;         // elements the reservation has room for
        long bytes;            // the reservation's bytes
        long frees;            // indices on the free stack, which starts at stack
        long committed, stack_committed, stack;
};

/*
        Flat per-client pool of frame callbacks and presentation feedbacks (WLT_CB): each surface keeps 4 O(1) FIFO
        queues (1-based indices into c->cbs; 0 means empty so zero-init is empty), saving >580 bytes per surface and
        making commit-time callback/feedback transfers O(1) splices with zero allocation.
*/
enum wayland_cb_list
{
        WLCB_CALLBACK = 0,
        WLCB_READY,
        WLCB_FEEDBACK,
        WLCB_PRESENTING,
        WLCB_LISTS
};

struct wayland_cb
{
        unsigned int id;
        unsigned int next; // 1-based index into c->cbs, 0 for tail
};

struct wayland_cbq
{
        unsigned int head, tail; // 1-based indices into c->cbs, 0 for empty
        long count;
};

struct wayland_ids
{
        unsigned int *id;
        long count;
        struct wayland_table table;
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

/* Double-buffered surface state: pending (accumulated until commit), cur (committed/shown), and cached (synced subsurface). */
struct wayland_surf_state
{
        unsigned int attached;       // buffer id (0 for none) when attach was called
        unsigned char attach_made;
        unsigned char damage_full;
        unsigned char damages;
        unsigned char damage_kind[WAYLAND_DAMAGE]; // 0 surface, 1 buffer coordinates
        struct wayland_rect damage[WAYLAND_DAMAGE];
        long scale, transform;
        // Buffer offset in surface units (attach x/y before version 5, offset after).
        long dx, dy;
        // Viewport source in 1/256 px (src_w 0 for none) and destination in surface units (dst_w 0 for none).
        long src_x, src_y, src_w, src_h;
        long dst_w, dst_h;
        struct wayland_rect opaque;
        unsigned char opaque_set;
        // wp_alpha_modifier (0xffffffff = opaque), xdg-toplevel-icon, wp_tearing_control, wp_fifo, wp_commit_timing.
        unsigned int alpha;
        unsigned char icon, async_hint, fifo_wait, barrier;
        unsigned long timestamp;
        // wp_linux_drm_syncobj explicit acquire/release points.
        unsigned char sync_acq_set, sync_rel_set;
        unsigned short sync_acq_slot, sync_rel_slot;
        unsigned long long sync_acq_point, sync_rel_point;
};

struct wayland_surface
{
        unsigned int gen;
        unsigned char live, role;
        unsigned short flags;
        unsigned int id;                 // the wl_surface's id
        struct wayland_surf_state pending, cur, cached;
        // Per-surface queues into the client's flat WLT_CB pool: pending/ready frame callbacks and pending/presenting feedbacks.
        struct wayland_cbq cbq[WLCB_LISTS];
        // Of the feedbacks taken, the first so many are the frame in flight's (wayland_latch).
        long presentings_framed;
        long scale120;                   // current surface unit in 120ths of a pixel
        long width, height;              // the shown buffer, in pixels
        unsigned int viewport_id, fractional_id;
        long told120, told_whole, told_unit;
        unsigned int content;            // wp_content_type (immediate)
        unsigned char modal, attention;  // xdg_dialog modal state, unvouched xdg-activation attention
        long barrier_frame;              // latch count when cur.barrier was set
        // xdg common state
        unsigned int xdg_id, role_id;
        unsigned int serial[WAYLAND_SERIALS];
        unsigned char serials;
        unsigned int last_serial;
        long geometry_x, geometry_y, geometry_width, geometry_height;
        unsigned char geometry_set;
        long window_x, window_y, window_w, window_h; // the part of the picture last shown as the window, in pixels
        // Lent buffers (4-state hold/release) and held buffer (subsurface / root-with-children / cursor).
        unsigned int lent, lent_gone, lent_latched, lent_shown, held;
        unsigned char layered, laying;
        char title[WAYLAND_TEXT];
        char app_id[WAYLAND_TEXT];
        unsigned int parent;             // parent toplevel/popup surface slot + 1
        // Intrusive doubly-linked subsurface tree (1-based surface slot indices; 0 = none).
        // first_child[0]/last_child[0] = children below self (bottom-to-top); [1] = children above self (bottom-to-top).
        unsigned int sub_parent;
        unsigned int first_child[2], last_child[2];
        unsigned int next_sib, prev_sib;
        unsigned char sub_above;
        unsigned char tearing, shortcuts, xsurface, xgrab;
        long closes;
        unsigned int shortcut_id;
        unsigned char shortcut_active, escape_down;
        long escape_at;
        unsigned char need_full, input_none, input_bounded, input_count, shown_full;
        struct wayland_rect input_rects[WAYLAND_REGION_RECTS];
        struct wayland_rect shown_rect;
        long budget_at, budget_used;
        unsigned int entered;            // the screens it was told it is on, a bit each (WAYLAND_SCREENS of them)
        unsigned int syncobj_id;
        int row;
        // Tagged role-specific state: top (WAYLAND_ROLE_TOPLEVEL), popup (WAYLAND_ROLE_POPUP),
        // sub (WAYLAND_ROLE_SUBSURFACE), xwin (WAYLAND_ROLE_NONE && xsurface).
        union
        {
                struct
                {
                        long min_width, min_height, max_width, max_height;
                        long configure_frame;
                        long told_width, told_height, told_states;
                        long layer_level, layer_output, layer_w, layer_h;
                        unsigned char layer_shell, layer_configured, configure_late, told;
                        unsigned char want_maximized, want_fullscreen;
                        short want_screen;
                } top;
                struct
                {
                        long x, y, width, height;
                        unsigned char grab;
                } popup;
                struct
                {
                        unsigned int id;
                        long x, y, pend_x, pend_y;
                        unsigned char sync, dirty, pend_pos, translucent;
                        long translucent_x, translucent_y, translucent_w, translucent_h;
                } sub;
                struct
                {
                        unsigned long long serial;
                        long owner, x, y, wait;
                        unsigned char serial_set, adopted, popup, dead, alone, placed;
                } xwin;
        } u;
};

struct wayland_pool
{
        long live;
        long fd;     // a dma-buf's descriptor while the pool is its mapping, which the buffer holds
        long extra_fd[3];
        long dmabuf; // 1 for such a pool
        long unmapped; // 1 when dma-buf cannot be CPU-mmap'd (VRAM/protected)
        long size;
        long refs;
        long gone;
        unsigned long id;
        void *at;
};

struct wayland_buffer
{
        long live;
        long pool;
        long offset, width, height, stride, format;
        unsigned int fourcc;
        unsigned long modifier;
        long plane_count;
        unsigned long plane_offset[4], plane_stride[4];
        unsigned char y_invert;
        unsigned char rel_pending;
        unsigned char explicit_sync; // its last commit had an acquire point, waited for before the commit was taken (wayland_syncobj_check)
        unsigned short rel_timeline;
        unsigned long long rel_point;
        unsigned int id;
        long held, zombie; // surfaces that keep it as their picture; its object destroyed while they did
};

struct wayland_sync_timeline
{
        unsigned char live, gone;
        int refs;
        long handle;
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

struct wayland_region
{
        unsigned char live, exact, count, overflow;
        struct wayland_rect rect, bounds;
        struct wayland_rect rects[WAYLAND_REGION_RECTS];
};

/*
        A data source, or a primary selection source, a client made: which selection it is for, and where it is: 0 made, 1 asked for
        and being read by the host, 2 the selection, 3 let go (cancelled, or used up), 4 a drag's.
*/
struct wayland_source
{
        unsigned int id;
        unsigned char live, which, state;
        unsigned int actions;            // what a drag of it may be (wl_data_source.set_actions, version 3)
        struct wayland_table mime_table; // every type it offered, each with its null, as a drag's offer carries them
        long mime_bytes;
};

// An offer made to a client of the selection as it was when it was made.
struct wayland_offer
{
        unsigned int id;
        unsigned char live, which;
        unsigned long serial;
        unsigned char drag, accepted, finished; // a drag's offer: the target accepted a type of it, and said it is done with it
        unsigned int actions, preferred;        // what the target takes of a drag (wl_data_offer.set_actions)
};

// A pointer lock or confinement: its kind (1 lock, 2 confine), whether it holds the pointer now, whether it ends with its first
// deactivation, and the surface it is of (an index and one).
struct wayland_constraint
{
        unsigned int id;
        unsigned char live, kind, active, oneshot, hinted;
        unsigned int surface;
        int hint_x, hint_y; // a lock's set_cursor_position_hint, 24.8 in the surface's units
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

/*
        A zwp_text_input_v3: its object's id, and its state as the last commit applied it (enabled, the content purpose, the cursor
        rectangle's bottom in surface units, -1 for none) with the state the next commit applies (pending_*). row is the window that
        has the seat's keyboard through its surface, -1 for none; counted is the row whose field count it holds (-1 for none). serial
        counts the commits, which done carries.
*/
struct wayland_text
{
        unsigned int id;
        unsigned char live, enabled, pending_enabled, purpose, pending_purpose;
        long caret, pending_caret;
        int row, counted;
        unsigned long serial;
};

struct wayland_feedback
{
        unsigned int id;
        unsigned char need;
};

// A descriptor the output queue holds: its number, the offset of the event it goes with, and whether it is the shim's own (closed once sent).
struct wayland_outfd
{
        long fd, at, owned;
};

// Declarative catalog of a client's growable tables (struct wayland_table). WLT_POOL is index 0 so the anonymous
// union aliases c->tbl[WLT_POOL] for tests that inspect pool_table.count directly.
enum wayland_tbl_kind
{
        WLT_POOL = 0,
        WLT_INDEX,
        WLT_SERVER,
        WLT_OBJECT,
        WLT_SURFACE,
        WLT_BUFFER,
        WLT_POSITIONER,
        WLT_REGION,
        WLT_FEEDBACK,
        WLT_SOURCE,
        WLT_OFFER,
        WLT_PARAMS,
        WLT_TEXT,
        WLT_TIMELINE,
        WLT_CONSTRAINT,
        WLT_NOTIFY,
        WLT_CB,
        WLT_OUT,
        WLT_OUT_FD,
        WLT_COUNT
};

struct wayland_client
{
        long live, dead, slot, pid, uid;
        unsigned char xspace;        // 1 when the client is an X server (wayland_xwayland_client): its outputs are in X's space; one of several
        long in_length;
        unsigned char in[WAYLAND_MESSAGE];
        // A commit that waits for its GPU, and what the client sent from it on: the bytes, the descriptor to ask of, since when.
        unsigned char stash[WAYLAND_STASH];
        long stash_length, stalled, stalled_fd, stalled_at; // stalled_fd -1: a commit that waits for its budget, not for a GPU
        long stalled_sync_handle;
        unsigned long long stalled_sync_point;
        long budget_surface;         // the root surface whose budget the stalled commit waits for
        long waiting;                // surfaces of the client whose frame callbacks are ready (readies above zero), exact: a client with none is passed over by a frame
        long presenting_surfaces;    // surfaces of the client holding presentation feedbacks for a frame (presentings above zero), exact: a client with none is passed over
        long subsurfaces;            // live surfaces whose role is a subsurface's, exact (counted where the role is given, and where the surface goes): none, and no child scan is made
        long out_length, out_room;      // the bytes queued from out's start, and the bytes of out made (whole pages of WLT_OUT)
        unsigned char *out;             // the output queue: WLT_OUT's memory, which never moves as it grows
        struct wayland_outfd *out_fds;  // WLT_OUT_FD's memory
        long out_fd_count;
        long in_fds[WAYLAND_FDS], in_fd_count;
        unsigned long serial;
        struct wayland_ids seat_ids[3]; // its pointers, keyboards and touches (wayland_seat_ids)
        unsigned char finger_scroll; // a precise (finger) scroll was sent to this client and no axis_stop since
        unsigned int press_serial;  // the last button press this client was sent, which a move or a resize must name
        unsigned int pointer_enter; // the serial of the last wl_pointer.enter this client was sent, which its cursor requests name (0 before the first)
        struct wayland_ids device_ids[2]; // data devices (0) and primary selection devices (1)
        unsigned int input_serial[WAYLAND_INPUT_SERIALS]; // the serials of the last input this client was sent
        unsigned char input_next;
        long server_hint, server_live; // where the next of the server's own ids is looked for, and how many are in use
        long pending[2];            // the source being read for each selection, as its index and one, or 0
        long feedbacks;
        unsigned char feedback_pending;
        long dmabufs;
        long inhibitors;                             // zwp_idle_inhibitor_v1 objects it holds (its objects are scanned only while one is)
        long pinches;                                // zwp_pointer_gesture_pinch_v1 objects held (pinch delivery skips clients with none)
        // A commit held for a fifo barrier (hold_at 0: woken by the frames) or for its time (hold_at, the host's ms), and the surface it is for.
        unsigned char hold;
        long hold_surface, hold_at;
        struct wayland_ids relative_ids; // its relative pointers
        long axis_left[2];          // the wheel's turns not yet a whole notch, for a client that has only notches
        // The buffers subsurfaces that went (destroyed, or taken off their parent) and rows a card job may still read were read in place
        // from, buffer index and one: a list that grows. The host has been told they went, and a frame drawn since lets them go
        // (wayland_gone_drawn); a buffer is kept until then, however many there are.
        struct wayland_ids gone;
        // Declarative table array (struct wayland_table), and each one's elements as a typed pointer at its reservation: they never move.
        union
        {
                struct wayland_table tbl[WLT_COUNT];
                struct
                {
                        struct wayland_table pool_table;
                };
        };
        unsigned int *index;                  // id -> object slot and one, for ids below tbl[WLT_INDEX].count
        unsigned int *server_index;           // server id -> object slot and one, for ids below tbl[WLT_SERVER].count
        struct wayland_object *objects;
        struct wayland_surface *surfaces;
        struct wayland_pool *pools;
        struct wayland_buffer *buffers;
        struct wayland_positioner *positioners;
        struct wayland_region *regions;
        struct wayland_feedback *feedback;
        struct wayland_source *sources;
        struct wayland_offer *offers;
        struct wayland_params *params;
        struct wayland_text *texts;
        struct wayland_sync_timeline *timelines;
        struct wayland_constraint *constraints;
        struct wayland_notify *notifies;
        struct wayland_cb *cbs;
};

struct wayland_row
{
        long client;
        long surface;
        long percent;
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
        unsigned int gen; // bumped each time the slot is given to a new token: the object that made one holds the gen it was given (its aux)
        char text[33];
};

/*
        The one state this file has: the host, the clients (a pointer each, to the block the host gave), and the
        row each window of a client is in. Nothing else is static.
*/
static struct
{
        const struct wayland_host *host;
        // The clients, a slot each (client_slots of them, grown as a client finds none free, never moving), and a bit for each that
        // has bytes queued or is dead and waiting to be flushed (out_dirty, a word a 64): tables (struct wayland_table), as a client's
        // own are, given back with the last client.
        struct wayland_client **client;
        unsigned long long *out_dirty;
        struct wayland_table client_table, out_table;
        long client_slots;
        // Each row of the compositor's table the shim has been given (row_made of them, grown as a row past them is opened, never
        // moving): the client and the surface whose window it is (-1 for none), and the percent the window was last told its scale
        // for (0 before the first).
        struct wayland_row *row;
        struct wayland_table row_table;
        long row_made;
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
        long buttons; // the pointer's buttons held, as the clients were told them
        /*
                The drag (wl_data_device.start_drag), while there is one: its client, its source (an index, -1 for a drag in its own
                client with none) and the serial it began with; the client and surface it is over (-1 none) with the offer made it
                (an index of that client's offers) and the action it came to; dropped once the button let go of it over a target
                that took a type, until the target is done with it; released while the button let go before the target answered its enter
        (its accept and actions may be on their way), decided when they come or WAYLAND_DRAG_ANSWER_MS after.
        */
        struct
        {
                int live, dropped, released;
                long released_at; // the host's ms of a release the target had not answered yet (its accept and actions in flight)
                long client, source, origin;
                long target, surface, offer;
                unsigned int action;
                // The X side (the window manager of Xwayland's): the row of the X window a Wayland source's drag is over (-1 none) and
                // whether it takes a type there; or, xsource, a drag an X window makes, with its types (each with its null) and actions.
                long xtarget;
                // held: a drag of the host's whose pointer the compositor drives (a program of the compositor's, wayland_drag_host_begin),
                // where an X source's is Xwayland's.
                int x_accepted, xsource, x_seen, held; // x_seen: the drag has been over an X window (its end is told the X side)
                unsigned int x_actions;
                struct wayland_table x_types;
                long x_bytes;
        } drag;
        unsigned long selection_serial[2];
        struct wayland_dmabuf_info dmabuf; // what the compositor takes as dma-bufs, none when count is 0
        long dmabufs;                      // the buffers made of them, all clients
        struct wayland_syncobj_host syncobj;
        long syncobj_ok;
        char drm_node[64];
        long constraints, hold_row, hold_mode; // live pointer locks and confinements; the window that holds the pointer and how
        long unheld_row;                       // a window whose constraints the host holds off (Steam's overlay open over it), -1 for none
        long hint_row, hint_x, hint_y;         // where a lock let go of hinted the pointer is: the row and the place in its picture
        // Requests dispatched, and of them the quiet ones (a lock's hint, a commit with nothing pending: wayland_quiet), over every
        // client: a read whose requests were all quiet changed nothing the host shows (compositor_wl_quiet_turn).
        long heard, quiet;
        long counts_row, counts[4], counts_micro; // the hand's counts for the frame of the pointer's next motion: its row (-1: none),
                                                  // dx, dy, the unaccelerated two, the last report's microsecond
        long input_at;                         // the shim's ms of the last input event the compositor gave it (wayland_input_seen): the idle clock
        long bells, icons;                     // xdg_system_bell rings and xdg-toplevel-icon sets with an icon, over every client (counted; nothing is drawn yet)
        long frames;                           // frames drawn (wayland_frame)
        // Latches: a present began (wayland_latch) or a frame's time went by with nothing drawn (wayland_frame): a fifo barrier clears at
        // the next one after its commit. framing: the feedbacks of a latched frame wait for its flip (wayland_presented); calling: its
        // callbacks wait for its end (wayland_frame).
        long latches;
        int framing, calling;
        // The vblank the next latch is shown at and the refresh period, nanoseconds on the monotonic clock (wayland_target), 0 unknown.
        unsigned long target, target_period;
        struct wayland_touch touch[WAYLAND_TOUCHES]; // the fingers down, over every client
        // xdg-activation tokens, pending or given out: a table that grows (tokens is its length); a given one lasts WAYLAND_TOKEN_MS, a
        // pending one until it is committed or destroyed, or its client goes.
        struct wayland_token *token;
        long tokens;
        struct wayland_table token_table;
        long pinch_x, pinch_y;                       // the focal point the last pinch update was at
        unsigned long synth_code;                    // the key whose press held a Shift the shim sent for it (wayland_key_shift), 0 for none
        long xwayland_uid;                           // the one user whose clients are offered the X server's protocols (wayland_xwayland_user), -1 for none
        long idle_notifies;                          // live ext_idle_notification_v1 count across all clients
        unsigned long pool_id;                       // monotonic pool identity counter
        // The pointer's picture a client drew (wl_pointer.set_cursor with a surface, wayland_cursor_picture): the client (its slot, -1 for
        // none) and the surface (its index and generation) named for the window under the pointer, the hotspot in the surface's units, and
        // the count that moves whenever any of it, or what the surface shows, changes.
        long cursor_client, cursor_surface, cursor_hot_x, cursor_hot_y;
        unsigned int cursor_gen;
        unsigned long cursor_serial;
} wayland;

#define WAYLAND_TABLE_WANT (1L << 36)       // the address space a table asks for (64 GiB), which the compositor's host makes the machine's memory
#define WAYLAND_TABLE_FALLBACK (1L << 26)   // a table's block where the host reserves nothing
#define WAYLAND_TABLE_PAGE 4096L
#define WAYLAND_TABLE_FIRST 64L             // elements committed first

/*
        The host a table's memory is made by: the shim's, once it is started, and before that (or with none) the one the compositor
        named for tables of its own (wayland_tables_host: the mappings it tracks, the window manager of Xwayland's records), which
        are made before a client connects and in a compositor with no shim at all. A table with neither has no memory: its take is
        refused, where it read through a host that was not there.
*/
static const struct wayland_host *wayland_tables_by;

WAYLAND_API void wayland_tables_host(const struct wayland_host *host)
{
        wayland_tables_by = host;
}

/* Multiplicative Fibonacci hash for power-of-two open-addressing tables (shared by xwm.c and wayland_host.c). */
WAYLAND_API unsigned long wayland_u32_hash(unsigned long key, long buckets)
{
        return (key * 0x9e3779b97f4a7c15ul >> 24) & (unsigned long)(buckets - 1);
}

static const struct wayland_host *wayland_table_host(void)
{
        return wayland.host ? wayland.host : wayland_tables_by;
}

static long wayland_round_page(long bytes)
{
        return (bytes + WAYLAND_TABLE_PAGE - 1) & ~(WAYLAND_TABLE_PAGE - 1);
}

static void wayland_table_free_mem(const struct wayland_host *h, void *at, long bytes)
{
        if (h->reserve && h->commit && h->unreserve)
                h->unreserve(h->context, at, bytes);
        else
                h->release(h->context, at, bytes);
}

// A table's memory committed for `want` elements, its reservation made first: 1 when it is, 0 when the memory is not there.
static int wayland_table_room(struct wayland_table *t, long want)
{
        const struct wayland_host *h = wayland_table_host();
        long usable, need, stack_need;
        int has_res;

        if (want <= t->usable)
                return 1;
        if (!h)
                return 0;
        has_res = h->reserve && h->commit && h->unreserve;
        if (!t->at)
        {
                long bytes = has_res ? WAYLAND_TABLE_WANT : WAYLAND_TABLE_FALLBACK;
                void *at = has_res ? h->reserve(h->context, &bytes) : h->alloc(h->context, bytes);

                if (!at || bytes < 2 * WAYLAND_TABLE_PAGE + t->unit + 4)
                {
                        if (at)
                                wayland_table_free_mem(h, at, bytes);
                        return 0;
                }
                t->at = (unsigned char *)at;
                t->bytes = bytes;
                t->reserved = (bytes - 2 * WAYLAND_TABLE_PAGE) / (t->unit + 4);
                t->stack = wayland_round_page(t->reserved * t->unit);
        }
        if (want > t->reserved)
                return 0;
        usable = t->usable * 2 > want ? t->usable * 2 : want;
        usable = usable < WAYLAND_TABLE_FIRST ? WAYLAND_TABLE_FIRST : usable > t->reserved ? t->reserved : usable;
        need = wayland_round_page(usable * t->unit);
        stack_need = wayland_round_page(usable * 4);
        if (has_res)
        {
                if (need > t->committed && !h->commit(h->context, t->at + t->committed, need - t->committed))
                        return 0;
                if (need > t->committed)
                        t->committed = need;
                if (stack_need > t->stack_committed &&
                    !h->commit(h->context, t->at + t->stack + t->stack_committed, stack_need - t->stack_committed))
                        return 0;
        }
        if (need > t->committed)
                t->committed = need;
        if (stack_need > t->stack_committed)
                t->stack_committed = stack_need;
        t->usable = usable;
        return 1;
}

// An element of the table to use: one let go of, or a new one. Its index, or -1 when the memory for it is not there.
static long wayland_table_take(struct wayland_table *t)
{
        if (t->frees > 0)
                return (long)((unsigned int *)(t->at + t->stack))[--t->frees];
        if (!wayland_table_room(t, t->count + 1))
                return -1;
        return t->count++;
}

// An element let go of, to be taken again. The stack has room: it holds fewer than count, and usable of it is committed.
static void wayland_table_give(struct wayland_table *t, long index)
{
        if (index >= 0 && index < t->count && t->frees < t->count)
                ((unsigned int *)(t->at + t->stack))[t->frees++] = (unsigned int)index;
}

// The table's elements below `count` usable (an index of ids, a bitmap), the count raised to it. 1 when they are.
static int wayland_table_reach(struct wayland_table *t, long count)
{
        if (!wayland_table_room(t, count))
                return 0;
        if (count > t->count)
                t->count = count;
        return 1;
}

static void wayland_table_drop(struct wayland_table *t)
{
        const struct wayland_host *h = wayland_table_host();

        if (t->at && h)
                wayland_table_free_mem(h, t->at, t->bytes);
        t->at = 0;
        t->count = t->usable = t->reserved = t->bytes = t->frees = t->committed = t->stack_committed = t->stack = 0;
}

/*
        What this file asks of the library (src/lib.c), which is the machine's own instructions on each of the three machines and not
        C: the head of a message read and checked (wire_message), its arguments walked by the list of their kinds (wire_arguments), an
        event of words written (wire_event), an id looked up in an index (wire_slot), and a commit's damage cut to its buffer
        (rects_clip). Their contracts are at their declarations there, and the verify lane holds each to the C this file had in
        its place. The compositor is built with the library in front of this file; built alone (the engine's own row,
        and the checks, which link the library beside it), the routines are declared here, in the plain types theirs are.
*/
#ifndef STANDARD_MODERN_C
unsigned long long wire_message(const unsigned char *bytes, unsigned long long size, unsigned long long most, unsigned int *head);
unsigned long long rects_clip(long long *rects, unsigned long long count, unsigned long long width, unsigned long long height);
unsigned long long wire_arguments(const unsigned char *kinds, const unsigned char *classes, const unsigned char *bytes, unsigned long long left,
                                  unsigned int *out, unsigned long long room);
unsigned long long wire_event(unsigned char *into, unsigned long long object, unsigned long long opcode, const unsigned int *words,
                              unsigned long long count);
int bits_trailing_zeros(unsigned long long value);
void memory_zero(void *destination, unsigned long size);
unsigned long memory_into_hex(void *destination, const void *source, unsigned long size);
#define WIRE_NONE 1
#define WIRE_WORD 2
#define WIRE_TEXT 4
#define WIRE_EMPTY 8
#define WIRE_ENDED 16
#define WIRE_PAIR 32
#define WIRE_END_DONE 0
#define WIRE_END_SHORT 1
#define WIRE_END_NOTHING 2
#define WIRE_END_LONG 3
#define WIRE_END_OPEN 4
#define WIRE_END_PAIR 5
#define WIRE_END_KIND 6
#define WIRE_END_ROOM 7
#define WIRE_END_OVER 8
#endif

#ifdef STANDARD_MODERN_C_LIB_UTIL
#define wayland_first_bit(mask) ((long)bottom_bit_known((positive)(mask)))
#else
#define wayland_first_bit(mask) ((long)bits_trailing_zeros(mask))
#endif

struct wayland_tbl_desc
{
        long unit;
        long ptr_off; // byte offset of typed pointer in struct wayland_client, or -1
};

#define WAYLAND_OFF(field) ((long)(unsigned long)&((struct wayland_client *)0)->field)

static const struct wayland_tbl_desc wayland_tbl_meta[WLT_COUNT] = {
    [WLT_POOL]       = {sizeof(struct wayland_pool), WAYLAND_OFF(pools)},
    [WLT_INDEX]      = {sizeof(unsigned int), WAYLAND_OFF(index)},
    [WLT_SERVER]     = {sizeof(unsigned int), WAYLAND_OFF(server_index)},
    [WLT_OBJECT]     = {sizeof(struct wayland_object), WAYLAND_OFF(objects)},
    [WLT_SURFACE]    = {sizeof(struct wayland_surface), WAYLAND_OFF(surfaces)},
    [WLT_BUFFER]     = {sizeof(struct wayland_buffer), WAYLAND_OFF(buffers)},
    [WLT_POSITIONER] = {sizeof(struct wayland_positioner), WAYLAND_OFF(positioners)},
    [WLT_REGION]     = {sizeof(struct wayland_region), WAYLAND_OFF(regions)},
    [WLT_FEEDBACK]   = {sizeof(struct wayland_feedback), WAYLAND_OFF(feedback)},
    [WLT_SOURCE]     = {sizeof(struct wayland_source), WAYLAND_OFF(sources)},
    [WLT_OFFER]      = {sizeof(struct wayland_offer), WAYLAND_OFF(offers)},
    [WLT_PARAMS]     = {sizeof(struct wayland_params), WAYLAND_OFF(params)},
    [WLT_TEXT]       = {sizeof(struct wayland_text), WAYLAND_OFF(texts)},
    [WLT_TIMELINE]   = {sizeof(struct wayland_sync_timeline), WAYLAND_OFF(timelines)},
    [WLT_CONSTRAINT] = {sizeof(struct wayland_constraint), WAYLAND_OFF(constraints)},
    [WLT_NOTIFY]     = {sizeof(struct wayland_notify), WAYLAND_OFF(notifies)},
    [WLT_CB]         = {sizeof(struct wayland_cb), WAYLAND_OFF(cbs)},
    [WLT_OUT]        = {WAYLAND_TABLE_PAGE, WAYLAND_OFF(out)},
    [WLT_OUT_FD]     = {sizeof(struct wayland_outfd), WAYLAND_OFF(out_fds)},
};

static inline void wayland_tbl_point(struct wayland_client *c, enum wayland_tbl_kind k)
{
        if (wayland_tbl_meta[k].ptr_off >= 0)
                *(void **)((unsigned char *)c + wayland_tbl_meta[k].ptr_off) = c->tbl[k].at;
}

static void wayland_tables_init(struct wayland_client *c)
{
        for (long k = 0; k < WLT_COUNT; k++)
                c->tbl[k].unit = wayland_tbl_meta[k].unit;
}

static int wayland_tbl_reach(struct wayland_client *c, enum wayland_tbl_kind k, long count)
{
        if (!wayland_table_reach(&c->tbl[k], count))
                return 0;
        wayland_tbl_point(c, k);
        return 1;
}

static long wayland_tbl_alloc(struct wayland_client *c, enum wayland_tbl_kind k)
{
        long at = wayland_table_take(&c->tbl[k]);

        if (at < 0)
                return -1;
        wayland_tbl_point(c, k);
        if (k == WLT_SURFACE)
        {
                unsigned int gen = c->surfaces[at].gen;

                memory_zero(&c->surfaces[at], sizeof c->surfaces[at]);
                c->surfaces[at].gen = gen;
        }
        else
                memory_zero(c->tbl[k].at + at * c->tbl[k].unit, (unsigned long)c->tbl[k].unit);
        return at;
}

static void wayland_tbl_free(struct wayland_client *c, enum wayland_tbl_kind k, long idx)
{
        wayland_table_give(&c->tbl[k], idx);
}

/*
        The output queue grown to hold one more event (WAYLAND_EVENT) past what it has: its table's pages made, up to
        WAYLAND_OUT_MOST. 1 when there is the room, 0 when the queue is at its most or the memory is not there, which ends the client
        as a full queue did. Where the queue was 64 KiB in the client's record and a client was ended, with no error, once one pass
        of the host's reads had made more events than that: an unpaced client that sends many frames for one read of its socket,
        or one that read nothing for a moment while the hand moved a pointer at 8 kHz (40 bytes a report).
*/
static __attribute__((noinline)) int wayland_out_grow(struct wayland_client *c)
{
        long want = (c->out_length + WAYLAND_EVENT + WAYLAND_TABLE_PAGE - 1) / WAYLAND_TABLE_PAGE;

        if (want * WAYLAND_TABLE_PAGE > WAYLAND_OUT_MOST || !wayland_tbl_reach(c, WLT_OUT, want))
                return 0;
        c->out_room = c->tbl[WLT_OUT].usable * WAYLAND_TABLE_PAGE < WAYLAND_OUT_MOST ? c->tbl[WLT_OUT].usable * WAYLAND_TABLE_PAGE : WAYLAND_OUT_MOST;
        return c->out_length + WAYLAND_EVENT <= c->out_room;
}

// Whether the queue has the room for one more event, grown for it when it has not.
static inline int wayland_out_has(struct wayland_client *c)
{
        return c->out_length + WAYLAND_EVENT <= c->out_room || wayland_out_grow(c);
}

// The output queue's room for one more descriptor (1, or 0 when the memory for it is not there), and the one put there.
static int wayland_out_fd_room(struct wayland_client *c)
{
        return wayland_tbl_reach(c, WLT_OUT_FD, c->out_fd_count + 1);
}

static void wayland_out_fd_put(struct wayland_client *c, long fd, long owned, long at)
{
        struct wayland_outfd *f = &c->out_fds[c->out_fd_count++];

        f->fd = fd;
        f->owned = owned;
        f->at = at;
}

// The descriptors one send carries: the first WAYLAND_OUT_FDS of the queue (the host's control message takes that many).
static long wayland_out_fd_batch(const struct wayland_client *c)
{
        return c->out_fd_count < WAYLAND_OUT_FDS ? c->out_fd_count : WAYLAND_OUT_FDS;
}

/* Flat per-client callback/feedback queue operations (WLT_CB). */
static int wayland_cb_push(struct wayland_client *c, struct wayland_cbq *q, unsigned int id)
{
        long idx = wayland_tbl_alloc(c, WLT_CB);

        if (idx < 0)
                return 0;
        c->cbs[idx].id = id;
        c->cbs[idx].next = 0;
        if (q->tail)
                c->cbs[q->tail - 1].next = (unsigned int)(idx + 1);
        else
                q->head = (unsigned int)(idx + 1);
        q->tail = (unsigned int)(idx + 1);
        q->count++;
        return 1;
}

static unsigned int wayland_cb_pop(struct wayland_client *c, struct wayland_cbq *q)
{
        unsigned int h = q->head, id;

        if (!h)
                return 0;
        id = c->cbs[h - 1].id;
        q->head = c->cbs[h - 1].next;
        if (!q->head)
                q->tail = 0;
        q->count--;
        wayland_tbl_free(c, WLT_CB, (long)(h - 1));
        return id;
}

static void wayland_cb_splice(struct wayland_client *c, struct wayland_cbq *dst, struct wayland_cbq *src)
{
        if (!src->head)
                return;
        if (dst->tail)
                c->cbs[dst->tail - 1].next = src->head;
        else
                dst->head = src->head;
        dst->tail = src->tail;
        dst->count += src->count;
        src->head = src->tail = 0;
        src->count = 0;
}

/*
        The rows below `row` + 1 made the shim's (their client and surface none, their percent none):
        1 when they are, 0 when the memory is not there (the window is not shown, as when the compositor has no row for it).
*/
static int wayland_rows_reach(long row)
{
        long was = wayland.row_made;

        if (row < was)
                return 1;
        wayland.row_table.unit = sizeof(struct wayland_row);
        if (!wayland_table_reach(&wayland.row_table, row + 1))
                return 0;
        wayland.row = (struct wayland_row *)wayland.row_table.at;
        for (long at = was; at <= row; at++)
                wayland.row[at] = (struct wayland_row){-1, -1, 0};
        wayland.row_made = row + 1;
        return 1;
}

// A client's slot marked as having bytes queued (or dead state) to flush, and cleared once it has none.
static void wayland_out_mark(long slot)
{
        if (slot >= 0 && slot < wayland.client_slots)
                wayland.out_dirty[slot / 64] |= 1ull << (slot % 64);
}

static void wayland_out_clear(long slot)
{
        if (slot >= 0 && slot < wayland.client_slots)
                wayland.out_dirty[slot / 64] &= ~(1ull << (slot % 64));
}

/*
        One more client slot, past those made: the clients' table and the dirty bits grown (their elements never move). The new slot,
        empty, or -1 when the memory is not there (the connection is refused, as with any other no_memory).
*/
static long wayland_client_slot_grow(void)
{
        long slot = wayland.client_slots;

        wayland.client_table.unit = sizeof(struct wayland_client *);
        wayland.out_table.unit = sizeof(unsigned long long);
        if (!wayland_table_reach(&wayland.client_table, slot + 1) || !wayland_table_reach(&wayland.out_table, slot / 64 + 1))
                return -1;
        wayland.client = (struct wayland_client **)wayland.client_table.at;
        wayland.out_dirty = (unsigned long long *)wayland.out_table.at;
        wayland.client[slot] = 0;
        if (slot % 64 == 0)
                wayland.out_dirty[slot / 64] = 0;
        wayland.client_slots = slot + 1;
        return slot;
}

// The slots given back with the last client: the next connection makes them again.
static void wayland_client_slots_drop(void)
{
        wayland_table_drop(&wayland.client_table);
        wayland_table_drop(&wayland.out_table);
        wayland.client = 0, wayland.out_dirty = 0, wayland.client_slots = 0;
}

// An id added to a list (its table grown when it is full): 1, or 0 when the memory is not there.
static int wayland_ids_add(struct wayland_ids *ids, unsigned int id)
{
        ids->table.unit = sizeof(unsigned int);
        if (!wayland_table_reach(&ids->table, ids->count + 1))
                return 0;
        ids->id = (unsigned int *)ids->table.at;
        ids->id[ids->count++] = id;
        return 1;
}

// An id taken out of a list, the last put in its place.
static void wayland_ids_remove(struct wayland_ids *ids, unsigned int id)
{
        for (long at = 0; at < ids->count; at++)
                if (ids->id[at] == id)
                {
                        ids->id[at] = ids->id[--ids->count];
                        return;
                }
}

static void wayland_ids_drop(struct wayland_ids *ids)
{
        wayland_table_drop(&ids->table);
        ids->id = 0, ids->count = 0;
}

static void wayland_tables_drop(struct wayland_client *c)
{
        for (long at = 0; at < c->tbl[WLT_SOURCE].count; at++)
                wayland_table_drop(&c->sources[at].mime_table);
        for (long k = 0; k < 3; k++)
                wayland_ids_drop(&c->seat_ids[k]);
        wayland_ids_drop(&c->device_ids[0]), wayland_ids_drop(&c->device_ids[1]), wayland_ids_drop(&c->relative_ids);
        wayland_ids_drop(&c->gone);
        for (long k = 0; k < WLT_COUNT; k++)
        {
                wayland_table_drop(&c->tbl[k]);
                wayland_tbl_point(c, (enum wayland_tbl_kind)k);
        }
        c->out_room = c->out_length = 0;
}

/* Preorder traversal of `root` and all its subsurface descendants (zero recursion, zero allocation). */
static struct wayland_surface *wayland_tree_next(struct wayland_client *c, const struct wayland_surface *root,
                                                 struct wayland_surface *cur)
{
        unsigned int ch = cur->first_child[0] ? cur->first_child[0] : cur->first_child[1];

        if (ch)
                return &c->surfaces[ch - 1];
        while (cur != root && cur->sub_parent)
        {
                struct wayland_surface *p = &c->surfaces[cur->sub_parent - 1];

                if (cur->next_sib)
                        return &c->surfaces[cur->next_sib - 1];
                if (!cur->sub_above && p->first_child[1])
                        return &c->surfaces[p->first_child[1] - 1];
                cur = p;
        }
        return 0;
}

/* ---- the wire ---- */


/*
        A word of the wire read or written where it is, in one access at any alignment: the wire's words are the machine's
        order, which is little-endian on all three the shim runs on. Spelled a byte at a time, gcc built an event's head of
        two words in a register a byte at a time, shifted and or-ed together (45 instructions where two stores do).
*/
typedef unsigned int __attribute__((may_alias, aligned(1))) wayland_wire_word;
typedef unsigned short __attribute__((may_alias, aligned(1))) wayland_wire_half;
typedef unsigned long long __attribute__((may_alias, aligned(1))) wayland_wire_long;

static inline unsigned long wayland_get(const unsigned char *at)
{
        return *(const wayland_wire_word *)at;
}

static inline void wayland_put(unsigned char *at, unsigned long value)
{
        *(wayland_wire_word *)at = (unsigned int)value;
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
        e.ok = !c->dead && wayland_out_has(c);
        e.at = e.ok ? c->out + c->out_length : wayland_sink;
        if (!e.ok && !c->dead)
        {
                c->dead = 2; // a queue that is full: a client that does not read
                wayland_out_mark(c->slot);
        }
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

/*
        An event that is words alone, which most are: its room asked once, as wayland_begin asks it, and its head and words written by
        the library (wire_event). The frame's own events go this way: a callback's done, a buffer's release, an id let go.
*/
static void wayland_send(struct wayland_client *c, unsigned long object, long opcode, const unsigned int *words, long count)
{
        int ok = !c->dead && wayland_out_has(c);
        long size;

        if (!ok && !c->dead)
                c->dead = 2; // a queue that is full: a client that does not read
        size = (long)wire_event(ok ? c->out + c->out_length : wayland_sink, object, (unsigned long long)opcode, words, (unsigned long long)count);
        if (ok)
                c->out_length += size;
        if (c->out_length || c->dead)
                wayland_out_mark(c->slot);
}

/*
        An event of a few words that the input path sends at every report of the hand (a relative motion, a frame, a motion, a key,
        a button), written in place: the room asked once, the head and the words stored, the slot marked. A queue without the room,
        or a dead client, goes the way wayland_send goes (the client dropped, nothing written). Where each was wayland_begin and
        wayland_end, two calls that handed the event's place to each other through memory and asked again what the first had asked.
*/
static inline __attribute__((always_inline)) void wayland_emit(struct wayland_client *c, unsigned long object, long opcode,
                                                               const unsigned int *words, long count)
{
        unsigned char *at;

        if (c->dead || !wayland_out_has(c))
        {
                wayland_send(c, object, opcode, words, count);
                return;
        }
        at = c->out + c->out_length;
        wayland_put(at, object);
        wayland_put(at + 4, (unsigned long)opcode | (unsigned long)(8 + 4 * count) << 16);
#pragma GCC unroll 8
        for (long word = 0; word < count; word++)
                wayland_put(at + 8 + 4 * word, words[word]);
        c->out_length += 8 + 4 * count;
        wayland_out_mark(c->slot);
}

static void wayland_end(struct wayland_event *e)
{
        unsigned char *base = e->ok ? e->client->out + e->start : wayland_sink;
        long size = (long)(e->at - base);

        // The size is the head's second word's high half, which wayland_begin left 0 (an opcode is below 65536, an event below
        // WAYLAND_EVENT).
        *(wayland_wire_half *)(base + 6) = (unsigned short)size;
        if (e->ok)
        {
                e->client->out_length += size;
                wayland_out_mark(e->client->slot);
        }
}

/* Compact event emitters: 0-word, N-word (wire_event), single/nullable string, and string+fd. */
static inline void wayland_ev0(struct wayland_client *c, unsigned long object, long opcode)
{
        wayland_send(c, object, opcode, 0, 0);
}

#define wayland_ev(c, obj, op, ...) do { \
        const unsigned int _wl_w[] = {__VA_ARGS__}; \
        wayland_send((c), (obj), (op), _wl_w, (long)(sizeof _wl_w / sizeof _wl_w[0])); \
} while (0)

static void wayland_ev_str(struct wayland_client *c, unsigned long object, long opcode, const char *text)
{
        struct wayland_event e = wayland_begin(c, object, opcode);

        if (text)
                wayland_string(&e, text);
        else
                wayland_word(&e, 0);
        wayland_end(&e);
}

static void wayland_ev_fd(struct wayland_client *c, unsigned long object, long opcode, const char *text, long fd, long owned)
{
        struct wayland_event e = wayland_begin(c, object, opcode);

        if (text)
                wayland_string(&e, text);
        wayland_out_fd_put(c, fd, owned, e.start);
        wayland_end(&e);
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
                wayland_out_mark(c->slot);
        }
        return WAYLAND_DEAD;
}

/* ---- objects ---- */

static long wayland_slot_of(struct wayland_client *c, unsigned long id)
{
        // A client's ids from 1, in its index; the server's from WAYLAND_SERVER_BASE, in theirs; anything else is in neither (an id
        // below the server's base is, less the base, far past the index's end).
        if (id < WAYLAND_IDS)
                return id && id < (unsigned long)c->tbl[WLT_INDEX].count ? (long)c->index[id] - 1 : -1;
        return id - WAYLAND_SERVER_BASE < (unsigned long)c->tbl[WLT_SERVER].count ? (long)c->server_index[id - WAYLAND_SERVER_BASE] - 1 : -1;
}

static long wayland_object_new(struct wayland_client *c, unsigned long id, long iface, long version)
{
        long slot = -1;

        if (iface <= WLI_NONE || iface >= WLI_COUNT || wayland_slot_of(c, id) >= 0)
                return -1;
        if (!(id >= 1 && id < WAYLAND_IDS) && !(id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < WAYLAND_SERVER_IDS))
                return -1;
        // A client's new id is one below the most it has made, let go of, or the next one (libwayland's rule, and its servers'): an id
        // far past them would have the index committed up to it for nothing. The index's memory first, then the object's.
        if (id < WAYLAND_IDS)
        {
                long next = c->tbl[WLT_INDEX].count > 1 ? c->tbl[WLT_INDEX].count : 1;

                if ((long)id > next + WAYLAND_ID_GAP || !wayland_tbl_reach(c, WLT_INDEX, (long)id + 1))
                        return -1;
        }
        else if (id - WAYLAND_SERVER_BASE >= (unsigned long)c->tbl[WLT_SERVER].count)
                return -1;
        slot = wayland_tbl_alloc(c, WLT_OBJECT);
        if (slot < 0)
                return -1;
        c->objects[slot].id = (unsigned int)id;
        c->objects[slot].iface = (unsigned char)iface;
        c->objects[slot].version = (unsigned char)(version > wl_interfaces[iface].version ? wl_interfaces[iface].version : version);
        c->objects[slot].flags = 0;
        c->objects[slot].link = -1;
        c->objects[slot].aux = 0;
        if (id < WAYLAND_IDS)
                c->index[id] = (unsigned int)(slot + 1);
        else
                c->server_index[id - WAYLAND_SERVER_BASE] = (unsigned int)(slot + 1), c->server_live++;
        wayland.objects++;
        return slot;
}

static void wayland_notify_free(struct wayland_client *c, long idx)
{
        if (idx < 0 || idx >= c->tbl[WLT_NOTIFY].count || !c->notifies[idx].live)
                return;
        c->notifies[idx].live = 0;
        wayland_tbl_free(c, WLT_NOTIFY, idx);
        if (wayland.idle_notifies > 0)
                wayland.idle_notifies--;
}

static void wayland_region_free(struct wayland_client *c, long idx)
{
        if (idx < 0 || idx >= c->tbl[WLT_REGION].count || !c->regions[idx].live)
                return;
        c->regions[idx].live = 0;
        wayland_tbl_free(c, WLT_REGION, idx);
}

static void wayland_object_free(struct wayland_client *c, long slot)
{
        unsigned long id = c->objects[slot].id;

        if (c->objects[slot].iface == WLI_zwp_idle_inhibitor_v1 && c->inhibitors > 0)
                c->inhibitors--;
        else if (c->objects[slot].iface == WLI_zwp_pointer_gesture_pinch_v1 && c->pinches > 0)
                c->pinches--;
        else if (c->objects[slot].iface == WLI_ext_idle_notification_v1)
                wayland_notify_free(c, c->objects[slot].link), c->objects[slot].link = -1;
        else if (c->objects[slot].iface == WLI_wl_region)
                wayland_region_free(c, c->objects[slot].link), c->objects[slot].link = -1;

        if (id < WAYLAND_IDS)
                c->index[id] = 0;
        else
                c->server_index[id - WAYLAND_SERVER_BASE] = 0, c->server_live--;
        c->objects[slot].iface = 0;
        c->objects[slot].id = 0;
        wayland_tbl_free(c, WLT_OBJECT, slot);
        wayland.objects--;
}

/* The client's id is let go of: the server says so, and the number may be used again. A server's own id is not. */
static void wayland_object_delete(struct wayland_client *c, long slot)
{
        unsigned long id = c->objects[slot].id;

        wayland_object_free(c, slot);
        if (id < WAYLAND_IDS)
                wayland_ev(c, 1, WLE_wl_display_delete_id, (unsigned int)id);
}

/* The object with the id, if it is one of the interface (any, for 0). */
static long wayland_find(struct wayland_client *c, unsigned long id, long iface)
{
        long slot = wayland_slot_of(c, id);

        return slot >= 0 && (!iface || c->objects[slot].iface == iface) ? slot : -1;
}

static inline unsigned long wayland_serial(struct wayland_client *c)
{
        return __atomic_add_fetch(&c->serial, 1, __ATOMIC_RELAXED); // the input thread takes a key's serial too (compositor_wl_direct_key)
}

// The serial of an input event sent to a client, kept among the last few: a selection is set by naming one.
static unsigned long wayland_input_serial(struct wayland_client *c)
{
        unsigned long serial = __atomic_add_fetch(&c->serial, 1, __ATOMIC_RELAXED);

        c->input_serial[c->input_next++ % WAYLAND_INPUT_SERIALS] = (unsigned int)serial;
        return serial;
}

/*
        Whether a serial names the pointer's being on the client's surface now, which is what a cursor request must say
        (wl_pointer.set_cursor and wp_cursor_shape's set_shape take the serial of the enter): the last enter's own, or any serial the
        client was sent since it. One from before that enter is stale (the pointer has left and come back since) and is not taken.
        The enter's serial was looked for among the last eight input serials, which a few keys and clicks push it out of: a browser
        that hides the pointer over a video three seconds after the hand stops names the enter it had minutes ago, and was ignored
        from the ninth key or click on, so the pointer never hid and its shape stopped following the page.
*/
static int wayland_pointer_serial_ok(const struct wayland_client *c, unsigned long serial)
{
        return c->pointer_enter && (unsigned int)serial - c->pointer_enter <= (unsigned int)c->serial - c->pointer_enter;
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

static inline long wayland_min(long a, long b) { return a < b ? a : b; }
static inline long wayland_max(long a, long b) { return a > b ? a : b; }
static inline long wayland_clamp(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }

#define WAYLAND_UNIT 120
#define WAYLAND_NOTCH 15 // a wheel's notch, in the surface's units, as every toolkit takes it (wl_pointer.axis)
#define WAYLAND_SCALE_LEAST 30   // 25 percent
#define WAYLAND_SCALE_MOST 960   // 800 percent

// A percent in 120ths, and the whole scale a client that knows no other is told (the percent rounded up, at least one).
static inline long wayland_percent120(long percent) { return (wayland_clamp(percent, 25, 800) * WAYLAND_UNIT + 50) / 100; }
static inline long wayland_whole_scale(long percent) { return wayland_max(1, (wayland_min(percent, 800) + 99) / 100); }

// Units to pixels at a scale in 120ths: nearest (half away from zero), floor, and ceil.
static inline long wayland_pixels(long units, long scale120)
{
        long v = units * scale120;
        return v >= 0 ? (v + WAYLAND_UNIT / 2) / WAYLAND_UNIT : -((-v + WAYLAND_UNIT / 2) / WAYLAND_UNIT);
}

static inline long wayland_pixels_floor(long units, long scale120)
{
        long v = units * scale120;
        return v >= 0 ? v / WAYLAND_UNIT : -((-v + WAYLAND_UNIT - 1) / WAYLAND_UNIT);
}

static inline long wayland_pixels_ceil(long units, long scale120)
{
        long v = units * scale120;
        return v >= 0 ? (v + WAYLAND_UNIT - 1) / WAYLAND_UNIT : -(-v / WAYLAND_UNIT);
}

static inline struct wayland_rect wayland_cover(struct wayland_rect r, long scale120)
{
        long x2 = wayland_pixels_ceil(r.x + r.width, scale120), y2 = wayland_pixels_ceil(r.y + r.height, scale120);

        r.x = wayland_pixels_floor(r.x, scale120);
        r.y = wayland_pixels_floor(r.y, scale120);
        r.width = x2 - r.x;
        r.height = y2 - r.y;
        return r;
}

static inline unsigned long wayland_units_fixed(long pixels, long scale120)
{
        return (unsigned long)(scale120 == WAYLAND_UNIT || scale120 <= 0 ? pixels * 256 : pixels * 256 * WAYLAND_UNIT / scale120);
}

static inline long wayland_units(long pixels, long scale120)
{
        return scale120 == WAYLAND_UNIT || scale120 <= 0 ? pixels : pixels * WAYLAND_UNIT / scale120;
}

/* ---- the host, and the records a client's block holds ---- */

static struct wayland_surface *wayland_surface_of(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        struct wayland_surface *s;

        if (o->link < 0 || o->link >= c->tbl[WLT_SURFACE].count)
                return 0;
        s = &c->surfaces[o->link];
        return s->live && s->gen == o->aux ? s : 0;
}

static long wayland_surface_slot(struct wayland_client *c)
{
        return wayland_tbl_alloc(c, WLT_SURFACE);
}

// The slot of a surface record, from the record.
static inline long wayland_surface_index(struct wayland_client *c, const struct wayland_surface *s)
{
        return (long)(s - c->surfaces);
}

static inline void wayland_zero(void *dst, unsigned long bytes)
{
        memory_zero(dst, bytes);
}

static void wayland_timeline_drop(struct wayland_client *c, long idx)
{
        struct wayland_sync_timeline *t = &c->timelines[idx];

        if (wayland.syncobj.drop && t->handle >= 0)
                wayland.syncobj.drop(wayland.syncobj.context, t->handle);
        t->handle = -1;
        t->live = 0;
        wayland_tbl_free(c, WLT_TIMELINE, idx);
}

static void wayland_timeline_unref(struct wayland_client *c, long idx)
{
        struct wayland_sync_timeline *t;

        if (idx < 0 || idx >= c->tbl[WLT_TIMELINE].count)
                return;
        t = &c->timelines[idx];
        if (!t->live || !t->gone || t->refs > 0)
                return;
        wayland_timeline_drop(c, idx);
}

static void wayland_buffer_signal_release(struct wayland_client *c, struct wayland_buffer *b)
{
        long idx;

        if (!b->rel_pending)
                return;
        idx = b->rel_timeline;
        b->rel_pending = 0;
        if (idx >= 0 && idx < c->tbl[WLT_TIMELINE].count && c->timelines[idx].live)
        {
                if (wayland.syncobj.signal && c->timelines[idx].handle >= 0)
                        wayland.syncobj.signal(wayland.syncobj.context, c->timelines[idx].handle, b->rel_point);
                c->timelines[idx].refs--;
                wayland_timeline_unref(c, idx);
        }
}

static void wayland_release_buffer(struct wayland_client *c, long slot)
{
        long link = c->objects[slot].link;

        if (link >= 0 && link < c->tbl[WLT_BUFFER].count && c->buffers[link].live)
                wayland_buffer_signal_release(c, &c->buffers[link]);
        wayland_send(c, c->objects[slot].id, WLE_wl_buffer_release, 0, 0);
}

/* ---- the globals ---- */

static void wayland_dmabuf_formats(struct wayland_client *c, unsigned long id, long version);

/*
        What the registry advertises: name n is element n - 1, each at the version this file implements (not the
        table's: the table is what the XML says, this is what is built). A screen is a wl_output global of its own,
        named WAYLAND_OUTPUT_NAME + its number.

        xdg-decoration (zxdg_decoration_manager_v1) is not among them. A window of this compositor has its line and its
        corners from the compositor and no titlebar: its top is the client's own, and a client moves it by asking
        (xdg_toplevel.move). That is client-side decoration, which is what a client assumes when the manager is absent, so
        every toolkit draws the same header without it (GTK never binds it; Qt, Chromium, GLFW, winit, kitty and mpv fall to
        their own frame). Offered and answered client_side to a client that asked for none, it made SDL (2 and 3) tear its
        first toplevel down in the decoration's configure and build another under libdecor; SDL 2's
        Wayland_ShowWindow runs that from inside its wait for the first toplevel's configure, and the flag that wait
        reads (shell_surface.xdg.initial_configure_seen) lies in a union the libdecor frame was written over, so the
        wait never ended: Half-Life 2 and every SDL 2 game with libdecor present sat in wl_display_dispatch forever with
        no window. Without the manager SDL takes libdecor from the start, as on GNOME.
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
    {WLI_zwp_text_input_manager_v3, 1},
    {WLI_wp_linux_drm_syncobj_manager_v1, 1},
    {WLI_wl_drm, 2},
    {WLI_zwlr_layer_shell_v1, 4},
};
#define WAYLAND_GLOBAL_COUNT ((long)(sizeof wayland_globals / sizeof wayland_globals[0]))

/*
        Whether a global is offered to a client: all are, but the dma-buf one, which waits for a compositor that takes dma-bufs, and
        the two an X server uses (xwayland_shell_v1, the keyboard grab), which are offered only to the user the X window manager's
        socket takes (wayland_xwayland_user, or uid 1000 when running as root). Any other client that could bind them
        would name an X window's serial before its X server did and have its surface shown in that window's place, with that
        window's keys.
*/
static int wayland_global_offered(const struct wayland_client *c, long name)
{
        long iface;

        if (name < 1 || name > WAYLAND_GLOBAL_COUNT)
                return 1;
        iface = wayland_globals[name - 1].iface;
        if (iface == WLI_zwp_linux_dmabuf_v1)
                return wayland.dmabuf.count > 0;
        if (iface == WLI_wl_drm)
                return wayland.dmabuf.count > 0 && wayland.drm_node[0] != 0;
        if (iface == WLI_wp_linux_drm_syncobj_manager_v1)
                return wayland.syncobj_ok != 0;
        if (iface == WLI_xwayland_shell_v1 || iface == WLI_zwp_xwayland_keyboard_grab_manager_v1)
                return wayland.xwayland_uid >= 0 && (c->uid == wayland.xwayland_uid || (wayland.xwayland_uid == 0 && c->uid == 1000));
        return 1;
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

/* What a screen says of itself, to one wl_output of a client: geometry, mode, scale, name, done (to the X server, in X's space and at
   scale 1: wayland_x_screen). */
static void wayland_x_screen(long output, long *x, long *y);

// An output's name and description, both its screen's name (two adjacent events in wl_output and zxdg_output_v1).
static void wayland_ev_names(struct wayland_client *c, unsigned long id, long op, const char *name)
{
        wayland_ev_str(c, id, op, name);
        wayland_ev_str(c, id, op + 1, name);
}

static void wayland_output_send(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        const struct wayland_screen *sc;
        struct wayland_event e;
        long x, y;

        if (o->aux >= wayland.screens)
                return;
        sc = &wayland.screen[o->aux];
        x = sc->x, y = sc->y;
        if (c->xspace)
                wayland_x_screen(o->aux, &x, &y);
        // Standard 96 DPI physical dimensions when EDID physical mm is 0 so XWayland/Wine/Proton compute 1.0x DPI cleanly.
        e = wayland_begin(c, o->id, WLE_wl_output_geometry);
        wayland_word(&e, (unsigned long)x);
        wayland_word(&e, (unsigned long)y);
        wayland_word(&e, (unsigned long)(sc->physical_width > 0 ? sc->physical_width : (sc->width * 254 + 480) / 960));
        wayland_word(&e, (unsigned long)(sc->physical_height > 0 ? sc->physical_height : (sc->height * 254 + 480) / 960));
        wayland_word(&e, WLN_wl_output_subpixel_unknown);
        wayland_string(&e, "Moonwater");
        wayland_string(&e, sc->name);
        wayland_word(&e, WLN_wl_output_transform_normal);
        wayland_end(&e);
        wayland_ev(c, o->id, WLE_wl_output_mode, WLN_wl_output_mode_current | WLN_wl_output_mode_preferred,
                   (unsigned int)sc->width, (unsigned int)sc->height, (unsigned int)sc->refresh);
        if (o->version >= 2)
                wayland_ev(c, o->id, WLE_wl_output_scale, c->xspace ? 1u : (unsigned int)wayland_whole_scale(sc->scale));
        if (o->version >= 4)
                wayland_ev_names(c, o->id, WLE_wl_output_name, sc->name);
        if (o->version >= 2)
                wayland_ev0(c, o->id, WLE_wl_output_done);
}

// Logical position of `screen` along X (vertical=0) or Y (vertical=1) in mixed-DPI layout.
static long wayland_logical_axis(long screen, int vertical, int depth)
{
        const struct wayland_screen *self = &wayland.screen[screen];
        long start = vertical ? self->y : self->x, across = vertical ? self->x : self->y;
        long across_size = vertical ? self->width : self->height, best = -1, best_end = 0;
        long scale120 = wayland_percent120(self->scale);

        for (long at = 0; at < wayland.screens && depth < WAYLAND_SCREENS; at++)
        {
                const struct wayland_screen *o = &wayland.screen[at];
                long o_start = vertical ? o->y : o->x, o_size = vertical ? o->height : o->width;
                long o_across = vertical ? o->x : o->y, o_across_size = vertical ? o->width : o->height;
                long stop = o_start + o_size;

                if (at != screen && stop <= start && o_across < across + across_size && across < o_across + o_across_size &&
                    (best < 0 || stop > best_end))
                        best = at, best_end = stop;
        }
        if (best < 0)
                return wayland_units(start, scale120);
        return wayland_logical_axis(best, vertical, depth + 1) +
               wayland_units(vertical ? wayland.screen[best].height : wayland.screen[best].width,
                             wayland_percent120(wayland.screen[best].scale)) +
               wayland_units(start - best_end, scale120);
}

static void wayland_x_screen(long output, long *x, long *y)
{
        long ox = 0, oy = 0;

        for (long at = 0; at < wayland.screens; at++)
                ox = !at || wayland.screen[at].x < ox ? wayland.screen[at].x : ox,
                oy = !at || wayland.screen[at].y < oy ? wayland.screen[at].y : oy;
        *x = wayland.screen[output].x - ox;
        *y = wayland.screen[output].y - oy;
}

static void wayland_xdg_output_send(struct wayland_client *c, long slot, long output)
{
        const struct wayland_screen *sc;
        unsigned long id = c->objects[slot].id;
        long scale120, x, y;

        if (output >= wayland.screens)
                return;
        sc = &wayland.screen[output];
        scale120 = c->xspace ? WAYLAND_UNIT : wayland_percent120(sc->scale);
        if (c->xspace)
                wayland_x_screen(output, &x, &y);
        else
                x = wayland_logical_axis(output, 0, 0), y = wayland_logical_axis(output, 1, 0);
        wayland_ev(c, id, WLE_zxdg_output_v1_logical_position, (unsigned int)x, (unsigned int)y);
        wayland_ev(c, id, WLE_zxdg_output_v1_logical_size,
                   (unsigned int)wayland_units(sc->width, scale120), (unsigned int)wayland_units(sc->height, scale120));
        if (c->objects[slot].version >= 2)
                wayland_ev_names(c, id, WLE_zxdg_output_v1_name, sc->name);
        if (c->objects[slot].version < 3)
                wayland_ev0(c, id, WLE_zxdg_output_v1_done);
}

static long wayland_surface_unit(const struct wayland_surface *s)
{
        return s->told120 <= 0 ? (s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT)
                               : (s->fractional_id ? s->told120 : WAYLAND_UNIT * s->told_whole);
}

/* ---- the windows ---- */

static long wayland_screen_at(long x, long y);

static int wayland_xdg_send_serial(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned long serial = wayland_serial(c);

        if (wayland_find(c, s->xdg_id, WLI_xdg_surface) < 0)
                return 0;
        wayland_ev(c, s->xdg_id, WLE_xdg_surface_configure, (unsigned int)serial);
        s->serial[s->serials++] = (unsigned int)serial;
        s->flags |= WAYLAND_SURFACE_SENT;
        return 1;
}

static void wayland_configure(struct wayland_client *c, struct wayland_surface *s, long initial)
{
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0}, ask;
        struct wayland_event e;
        unsigned long states[16];
        long count = 0, slot;

        if (s->serials >= WAYLAND_SERIALS || s->role == WAYLAND_ROLE_NONE)
                return;
        if (s->row >= 0)
                wayland.host->row_facts(wayland.host->context, s->row, &facts);
        ask = facts;
        if (s->role == WAYLAND_ROLE_TOPLEVEL)
        {
                static const struct { long mask; unsigned char min_ver; unsigned int wln; } state_map[] = {
                        {WAYLAND_STATE_MAXIMIZED, 1, WLN_xdg_toplevel_state_maximized},
                        {WAYLAND_STATE_FULLSCREEN, 1, WLN_xdg_toplevel_state_fullscreen},
                        {WAYLAND_STATE_RESIZING, 1, WLN_xdg_toplevel_state_resizing},
                        {WAYLAND_STATE_ACTIVATED, 1, WLN_xdg_toplevel_state_activated},
                        {WAYLAND_STATE_TILED_LEFT, 2, WLN_xdg_toplevel_state_tiled_left},
                        {WAYLAND_STATE_TILED_RIGHT, 2, WLN_xdg_toplevel_state_tiled_right},
                        {WAYLAND_STATE_TILED_TOP, 2, WLN_xdg_toplevel_state_tiled_top},
                        {WAYLAND_STATE_TILED_BOTTOM, 2, WLN_xdg_toplevel_state_tiled_bottom},
                        {WAYLAND_STATE_SUSPENDED, 6, WLN_xdg_toplevel_state_suspended},
                };
                long unit = wayland_surface_unit(s), version, cw, ch;

                // Start-fullscreen toplevels have no row yet; seed ask from the target screen while keeping told_* on facts.
                if (s->row < 0 && s->u.top.want_fullscreen)
                {
                        if (wayland.screens)
                        {
                                const struct wayland_screen *own =
                                        &wayland.screen[s->u.top.want_screen >= 0 && s->u.top.want_screen < wayland.screens
                                                                ? s->u.top.want_screen : wayland_screen_at(ask.x, ask.y)];
                                ask.x = own->x, ask.y = own->y, ask.width = own->width, ask.height = own->height;
                        }
                        ask.states |= WAYLAND_STATE_FULLSCREEN | WAYLAND_STATE_ACTIVATED;
                }
                else if (s->row < 0 && s->u.top.want_maximized)
                        ask.states |= WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_ACTIVATED;
                slot = wayland_find(c, s->role_id, WLI_xdg_toplevel);
                if (slot < 0)
                        return;
                version = c->objects[slot].version;
                for (unsigned long i = 0; i < sizeof state_map / sizeof state_map[0]; i++)
                        if (version >= state_map[i].min_ver && (ask.states & state_map[i].mask))
                                states[count++] = state_map[i].wln;
                if (initial && version >= 5)
                {
                        static const unsigned long caps[4] = {WLN_xdg_toplevel_wm_capabilities_window_menu,
                                                              WLN_xdg_toplevel_wm_capabilities_maximize,
                                                              WLN_xdg_toplevel_wm_capabilities_fullscreen, 4};
                        e = wayland_begin(c, s->role_id, WLE_xdg_toplevel_wm_capabilities);
                        wayland_words(&e, caps, 4);
                        wayland_end(&e);
                }
                if (initial && version >= 4 && wayland.screens)
                {
                        const struct wayland_screen *own = &wayland.screen[wayland_screen_at(ask.x, ask.y)];
                        long bw = wayland_units(own->width, unit), bh = wayland_units(own->height, unit);

                        if (bw > 0)
                                wayland_ev(c, s->role_id, WLE_xdg_toplevel_configure_bounds, (unsigned int)bw, (unsigned int)bh);
                }
                cw = ask.width > 0 ? wayland_max(1, wayland_units(ask.width, unit)) : 0;
                ch = ask.height > 0 ? wayland_max(1, wayland_units(ask.height, unit)) : 0;
                if (cw > 0 && ch > 0 && !(ask.states & WAYLAND_STATE_FULLSCREEN))
                {
                        if (s->u.top.max_width > 0) cw = wayland_min(cw, s->u.top.max_width);
                        if (s->u.top.max_height > 0) ch = wayland_min(ch, s->u.top.max_height);
                        if (s->u.top.min_width > 0) cw = wayland_max(cw, s->u.top.min_width);
                        if (s->u.top.min_height > 0) ch = wayland_max(ch, s->u.top.min_height);
                }
                e = wayland_begin(c, s->role_id, WLE_xdg_toplevel_configure);
                wayland_word(&e, (unsigned long)cw);
                wayland_word(&e, (unsigned long)ch);
                wayland_words(&e, states, count);
                wayland_end(&e);
                s->u.top.told_width = facts.width, s->u.top.told_height = facts.height;
                s->u.top.told_states = facts.states, s->told_unit = unit, s->u.top.told = 1;
        }
        if (!wayland_xdg_send_serial(c, s))
                return;
        if (s->role == WAYLAND_ROLE_TOPLEVEL)
                s->u.top.configure_frame = wayland.frames + 1, s->u.top.configure_late = 0;
}

static void wayland_constraints_update(void);
static void wayland_counts_send(int frame);
static void wayland_let_go(struct wayland_client *c, long index);
static void wayland_lent_let(struct wayland_client *c, long row, unsigned int index1);
static struct wayland_surface *wayland_root_of(struct wayland_client *c, struct wayland_surface *s);
static void wayland_presentings_discard(struct wayland_client *c, struct wayland_surface *s);
static void wayland_texts_row_gone(struct wayland_client *c, long row);

static void wayland_row_release(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned int held[4] = {s->lent, s->lent_gone, s->lent_latched, s->lent_shown};

        if (s->row < 0)
                return;
        wayland_texts_row_gone(c, s->row);
        wayland.row[s->row] = (struct wayland_row){-1, -1, 0};
        if (wayland.pointer_row == s->row)
                wayland.pointer_row = -1;
        if (wayland.keyboard_row == s->row)
                wayland.keyboard_row = -1;
        wayland.host->row_close(wayland.host->context, s->row);
        s->lent = s->lent_gone = s->lent_latched = s->lent_shown = 0;
        s->layered = s->laying = 0;
        for (long at = 0; at < 4; at++)
                wayland_lent_let(c, s->row, held[at]);
        for (struct wayland_surface *ch = wayland_tree_next(c, s, s); ch; ch = wayland_tree_next(c, s, ch))
        {
                unsigned int ch_held[2] = {ch->lent, ch->lent_gone};

                ch->lent = ch->lent_gone = 0;
                wayland_lent_let(c, s->row, ch_held[0]);
                wayland_lent_let(c, s->row, ch_held[1]);
        }
        wayland.rows--;
        wayland_presentings_discard(c, s);
        s->row = -1;
        if (!wayland.rows)
                wayland_table_drop(&wayland.row_table), wayland.row = 0, wayland.row_made = 0;
        s->flags &= ~(unsigned)WAYLAND_SURFACE_MAPPED;
        s->closes = 0;
        wayland_constraints_update();
}

static void wayland_callback_answer(struct wayland_client *c, unsigned int id, long time)
{
        long slot = wayland_find(c, id, WLI_wl_callback);

        if (slot < 0)
                return;
        wayland_ev(c, id, WLE_wl_callback_done, (unsigned int)time);
        wayland_object_delete(c, slot);
}

static void wayland_callbacks_done(struct wayland_client *c, struct wayland_surface *s, long time)
{
        if (s->cbq[WLCB_READY].count)
                c->waiting--;
        while (s->cbq[WLCB_READY].count)
                wayland_callback_answer(c, wayland_cb_pop(c, &s->cbq[WLCB_READY]), time);
}

static void wayland_callbacks_take(struct wayland_client *c, struct wayland_surface *s, long time)
{
        (void)time;
        if (!s->cbq[WLCB_CALLBACK].count)
                return;
        if (!s->cbq[WLCB_READY].count)
                c->waiting++;
        wayland_cb_splice(c, &s->cbq[WLCB_READY], &s->cbq[WLCB_CALLBACK]);
}

static void wayland_feedback_discard(struct wayland_client *c, unsigned int id)
{
        long slot = wayland_find(c, id, WLI_wp_presentation_feedback);

        if (slot < 0)
                return;
        wayland_ev0(c, id, WLE_wp_presentation_feedback_discarded);
        wayland_object_delete(c, slot);
}

static void wayland_presenting_drop(struct wayland_client *c, struct wayland_surface *o)
{
        if (o->cbq[WLCB_PRESENTING].count)
                c->presenting_surfaces--;
        while (o->cbq[WLCB_PRESENTING].count)
                wayland_feedback_discard(c, wayland_cb_pop(c, &o->cbq[WLCB_PRESENTING]));
        o->presentings_framed = 0;
}

static void wayland_presentings_discard(struct wayland_client *c, struct wayland_surface *s)
{
        for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                wayland_presenting_drop(c, o);
}

static void wayland_feedback_take(struct wayland_client *c, struct wayland_surface *s, int shown)
{
        struct wayland_cbq *pq = &s->cbq[WLCB_PRESENTING], *fq = &s->cbq[WLCB_FEEDBACK];
        long keep = !shown ? pq->count : !wayland.framing ? 0 : wayland_min(s->presentings_framed, pq->count);

        if (shown && keep < pq->count)
        {
                if (!keep)
                {
                        c->presenting_surfaces--;
                        while (pq->count)
                                wayland_feedback_discard(c, wayland_cb_pop(c, pq));
                }
                else
                {
                        unsigned int cur = pq->head, drop;

                        for (long i = 1; i < keep; i++)
                                cur = c->cbs[cur - 1].next;
                        drop = c->cbs[cur - 1].next, c->cbs[cur - 1].next = 0;
                        pq->tail = cur, pq->count = keep;
                        for (unsigned int next; drop; drop = next)
                        {
                                unsigned int id = c->cbs[drop - 1].id;

                                next = c->cbs[drop - 1].next;
                                wayland_tbl_free(c, WLT_CB, (long)(drop - 1));
                                wayland_feedback_discard(c, id);
                        }
                }
        }
        if (shown && fq->count)
        {
                if (!pq->count)
                        c->presenting_surfaces++;
                wayland_cb_splice(c, pq, fq);
        }
        else if (!shown)
                while (fq->count)
                        wayland_feedback_discard(c, wayland_cb_pop(c, fq));
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
        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
                s = &c->surfaces[s->sub_parent - 1];
        return s;
}

static void wayland_sub_unlink(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *p;
        int side;

        if (!s->sub_parent)
                return;
        p = &c->surfaces[s->sub_parent - 1];
        side = s->sub_above ? 1 : 0;
        if (s->prev_sib)
                c->surfaces[s->prev_sib - 1].next_sib = s->next_sib;
        else
                p->first_child[side] = s->next_sib;
        if (s->next_sib)
                c->surfaces[s->next_sib - 1].prev_sib = s->prev_sib;
        else
                p->last_child[side] = s->prev_sib;
        s->prev_sib = s->next_sib = 0;
}

static void wayland_sub_link_after(struct wayland_client *c, struct wayland_surface *p, struct wayland_surface *s, int side,
                                   unsigned int after)
{
        unsigned int si = (unsigned int)(wayland_surface_index(c, s) + 1);
        unsigned int next = after ? c->surfaces[after - 1].next_sib : p->first_child[side];

        s->sub_above = (unsigned char)side;
        s->prev_sib = after;
        s->next_sib = next;
        if (after)
                c->surfaces[after - 1].next_sib = si;
        else
                p->first_child[side] = si;
        if (next)
                c->surfaces[next - 1].prev_sib = si;
        else
                p->last_child[side] = si;
}

// A subsurface's place in its window's picture, in pixels: the sum of the places up the chain, at the root's scale.
static void wayland_sub_offset(struct wayland_client *c, struct wayland_surface *s, long *x, long *y)
{
        long dx = 0, dy = 0, scale = wayland_root_of(c, s)->scale120;

        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
        {
                dx += s->u.sub.x;
                dy += s->u.sub.y;
                s = &c->surfaces[s->sub_parent - 1];
        }
        *x = wayland_pixels(dx, scale > 0 ? scale : WAYLAND_UNIT);
        *y = wayland_pixels(dy, scale > 0 ? scale : WAYLAND_UNIT);
}

// Whether a subsurface is synchronized: itself, or any surface above it that is a subsurface.
static int wayland_sub_synced(struct wayland_client *c, struct wayland_surface *s)
{
        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
        {
                if (s->u.sub.sync)
                        return 1;
                s = &c->surfaces[s->sub_parent - 1];
        }
        return 0;
}

// A buffer held by a surface let go of (buffer index): its record is freed if its object was destroyed while it was held, else its client is told.
// once the last hold of it goes. A buffer is held more than once while it is read in place (a window's picture and its lent one, a layer's), and
// told at the first let go, its client drew into it while the compositor still read it.
static void wayland_let_go(struct wayland_client *c, long index)
{
        struct wayland_buffer *b = &c->buffers[index];

        b->held--;
        if (b->held <= 0)
                wayland_buffer_signal_release(c, b);
        if (b->zombie && b->held <= 0)
        {
                c->pools[b->pool].refs--;
                wayland_pool_unref(c, b->pool);
                b->live = 0;
                b->zombie = 0;
                wayland_tbl_free(c, WLT_BUFFER, b - c->buffers);
                wayland.buffers--;
        }
        else if (!b->zombie && b->held <= 0)
        {
                long slot = wayland_find(c, b->id, WLI_wl_buffer);

                if (slot >= 0)
                        wayland_release_buffer(c, slot);
        }
}

// Whether a card's job may still read the pictures of a row in place (wayland_host.row_busy).
static int wayland_row_busy(long row)
{
        return row >= 0 && wayland.host->row_busy && wayland.host->row_busy(wayland.host->context, (int)row);
}

// A buffer read in place let go of (index and one), or, while a job of the card may still read its row's pictures, kept until a frame is drawn
// (the client's gone list, wayland_gone_drawn); let go of at once only when the memory for the list is not there.
static void wayland_lent_let(struct wayland_client *c, long row, unsigned int index1)
{
        if (!index1)
                return;
        if (!wayland_row_busy(row) || !wayland_ids_add(&c->gone, index1))
                wayland_let_go(c, index1 - 1);
}

// The buffer a surface replaced while it was lent, let go of: unless it is the one the commit attaches (now), whose release the commit makes
// itself, so it is only given up here (its hold is the commit's to end). While a job of the card may still read it, it is kept (wayland_lent_let).
static void wayland_gone_let(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *now)
{
        long index = s->lent_gone - 1;

        s->lent_gone = 0;
        if (&c->buffers[index] == now)
                c->buffers[index].held--;
        else
                wayland_lent_let(c, wayland_root_of(c, s)->row, (unsigned int)(index + 1));
}

// A held buffer let go: its record is freed if its object was destroyed while it was held, else its client is told.
static void wayland_unhold(struct wayland_client *c, struct wayland_surface *s)
{
        long index;

        if (!s->held)
                return;
        index = s->held - 1;
        s->held = 0;
        wayland_let_go(c, index);
}

static inline int wayland_has_children(const struct wayland_surface *s)
{
        return s->first_child[0] != 0 || s->first_child[1] != 0;
}

static struct wayland_rect wayland_rect_union(struct wayland_rect a, struct wayland_rect b)
{
        long x1 = wayland_min(a.x, b.x), y1 = wayland_min(a.y, b.y);
        return (struct wayland_rect){x1, y1, wayland_max(a.x + a.width, b.x + b.width) - x1,
                                     wayland_max(a.y + a.height, b.y + b.height) - y1};
}

static inline int wayland_rect_contains(struct wayland_rect a, struct wayland_rect b)
{
        return a.x <= b.x && a.y <= b.y && a.x + a.width >= b.x + b.width && a.y + a.height >= b.y + b.height;
}

static struct wayland_rect wayland_rect_bounds(const struct wayland_rect *rects, long count)
{
        struct wayland_rect box = rects[0];

        for (long i = 1; i < count; i++)
                box = wayland_rect_union(box, rects[i]);
        return box;
}

static long wayland_rect_sub(struct wayland_rect b, struct wayland_rect sub, struct wayland_rect out[4])
{
        long bx2 = b.x + b.width, by2 = b.y + b.height, sx2 = sub.x + sub.width, sy2 = sub.y + sub.height, n = 0;

        if (b.width <= 0 || b.height <= 0)
                return 0;
        if (sub.x >= bx2 || sx2 <= b.x || sub.y >= by2 || sy2 <= b.y)
        {
                out[0] = b;
                return 1;
        }
        {
                long iy1 = wayland_max(b.y, sub.y), iy2 = wayland_min(by2, sy2);
                struct wayland_rect p[4] = {
                        {b.x, b.y, b.width, sub.y - b.y},
                        {b.x, sy2, b.width, by2 - sy2},
                        {b.x, iy1, sub.x - b.x, iy2 - iy1},
                        {sx2, iy1, bx2 - sx2, iy2 - iy1},
                };
                for (long k = 0; k < 4; k++)
                        if (p[k].width > 0 && p[k].height > 0)
                                out[n++] = p[k];
        }
        return n;
}

static void wayland_window_rect(struct wayland_client *c, struct wayland_surface *root, long width, long height, long scale120,
                                long *x, long *y, long *w, long *h)
{
        struct wayland_rect box = {0, 0, width, height};

        if (!wayland_has_children(root) && !root->geometry_set)
        {
                *x = 0, *y = 0, *w = wayland_min(width, WAYLAND_DIMENSION), *h = wayland_min(height, WAYLAND_DIMENSION);
                return;
        }
        for (struct wayland_surface *ch = wayland_tree_next(c, root, root); ch; ch = wayland_tree_next(c, root, ch))
        {
                long ox, oy;

                if (!ch->held)
                        continue;
                wayland_sub_offset(c, ch, &ox, &oy);
                ox = wayland_clamp(ox, -WAYLAND_DIMENSION, WAYLAND_DIMENSION);
                oy = wayland_clamp(oy, -WAYLAND_DIMENSION, WAYLAND_DIMENSION);
                box = wayland_rect_union(box, (struct wayland_rect){ox, oy, ch->width, ch->height});
        }
        if (root->geometry_set && scale120 >= WAYLAND_SCALE_LEAST)
        {
                long gx1 = wayland_max(wayland_pixels(root->geometry_x, scale120), box.x);
                long gy1 = wayland_max(wayland_pixels(root->geometry_y, scale120), box.y);
                long gx2 = wayland_min(wayland_pixels(root->geometry_x + root->geometry_width, scale120), box.x + box.width);
                long gy2 = wayland_min(wayland_pixels(root->geometry_y + root->geometry_height, scale120), box.y + box.height);

                if (gx2 > gx1 && gy2 > gy1)
                        box = (struct wayland_rect){gx1, gy1, gx2 - gx1, gy2 - gy1};
        }
        *x = box.x, *y = box.y;
        *w = wayland_min(box.width, WAYLAND_DIMENSION);
        *h = wayland_min(box.height, WAYLAND_DIMENSION);
}

static void wayland_row_take(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_picture none;
        long wx, wy, ww, wh;

        if (root->row < 0 || !root->laying)
                return;
        root->laying = 0;
        wayland_zero(&none, sizeof none);
        wayland_window_rect(c, root, root->width, root->height, root->scale120, &wx, &wy, &ww, &wh);
        none.row_width = ww, none.row_height = wh, none.last = 1, none.sync_fd = -1;
        none.plane_fd[0] = none.plane_fd[1] = none.plane_fd[2] = none.plane_fd[3] = -1;
        wayland.host->row_picture(wayland.host->context, root->row, &none);
}

static struct wayland_rect wayland_window_of(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_rect window;

        wayland_window_rect(c, root, root->width, root->height, root->scale120, &window.x, &window.y, &window.width, &window.height);
        return window;
}

static int wayland_budget_left(struct wayland_surface *root)
{
        long now = wayland.host->now(wayland.host->context), most = wayland.screens ? 0 : 1920L * 1080L;

        for (long at = 0; at < wayland.screens; at++)
                most = wayland_max(most, wayland.screen[at].width * wayland.screen[at].height);
        if (now - root->budget_at >= WAYLAND_BUDGET_MS || now < root->budget_at)
                root->budget_at = now, root->budget_used = 0;
        return root->budget_used < WAYLAND_BUDGET_SCREENS * most;
}

static inline int wayland_format_has_alpha(long format)
{
        return format == WLN_wl_shm_format_argb8888 || format == WLN_wl_shm_format_abgr8888 ||
               format == (long)WAYLAND_FOURCC_ARGB2101010 || format == (long)WAYLAND_FOURCC_ABGR2101010;
}

static inline void wayland_transformed_size(long w, long h, long transform, long *out_w, long *out_h)
{
        *out_w = (transform & 1) ? h : w;
        *out_h = (transform & 1) ? w : h;
}

static void wayland_surface_target_pixels(const struct wayland_surface *s, const struct wayland_surf_state *st,
                                          const struct wayland_buffer *b, long *out_w, long *out_h)
{
        long bw_t, bh_t, disp = s->told120 > 0 ? s->told120 : WAYLAND_UNIT;

        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        *out_w = wayland_min(st->dst_w > 0 ? wayland_pixels(st->dst_w, disp)
                                           : st->src_w > 0 ? (st->src_w * st->scale + 255) / 256 : bw_t, WAYLAND_DIMENSION);
        *out_h = wayland_min(st->dst_w > 0 ? wayland_pixels(st->dst_h, disp)
                                           : st->src_w > 0 ? (st->src_h * st->scale + 255) / 256 : bh_t, WAYLAND_DIMENSION);
}

static inline int wayland_surface_is_opaque_at(const struct wayland_surface *s, const struct wayland_surf_state *st,
                                               const struct wayland_buffer *b)
{
        long sw, sh, bw_t, bh_t, scale = st->scale > 0 ? st->scale : 1;

        if (!wayland_format_has_alpha(b->format))
                return 1;
        if (s->cur.opaque.width <= 0 || s->cur.opaque.height <= 0 || s->cur.opaque.x > 0 || s->cur.opaque.y > 0)
                return 0;
        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        sw = st->dst_w > 0 ? st->dst_w : st->src_w > 0 ? (st->src_w + 255) / 256 : (bw_t + scale - 1) / scale;
        sh = st->dst_w > 0 ? st->dst_h : st->src_w > 0 ? (st->src_h + 255) / 256 : (bh_t + scale - 1) / scale;
        return (long)s->cur.opaque.x + s->cur.opaque.width >= sw && (long)s->cur.opaque.y + s->cur.opaque.height >= sh;
}

static inline int wayland_surface_is_opaque(const struct wayland_surface *s, const struct wayland_buffer *b)
{
        return wayland_surface_is_opaque_at(s, &s->cur, b);
}

// Chromium-safe inward-rounded opaque subrect for transparent surfaces (no viewport/transform).
static void wayland_picture_opaque(struct wayland_picture *picture, const struct wayland_surface *s, const struct wayland_surf_state *st,
                                   long scale120, long wx, long wy, long ww, long wh)
{
        long x1, y1, x2, y2;

        picture->opaque_x = picture->opaque_y = picture->opaque_w = picture->opaque_h = 0;
        if (!picture->transparent || s->cur.opaque.width <= 0 || s->cur.opaque.height <= 0 || st->transform || st->src_w > 0 || st->dst_w > 0)
                return;
        x1 = wayland_max(wayland_pixels_ceil(s->cur.opaque.x, scale120) - wx, 0);
        y1 = wayland_max(wayland_pixels_ceil(s->cur.opaque.y, scale120) - wy, 0);
        x2 = wayland_min(wayland_pixels_floor(s->cur.opaque.x + s->cur.opaque.width, scale120) - wx, ww);
        y2 = wayland_min(wayland_pixels_floor(s->cur.opaque.y + s->cur.opaque.height, scale120) - wy, wh);
        if (x2 > x1 && y2 > y1)
                picture->opaque_x = x1, picture->opaque_y = y1, picture->opaque_w = x2 - x1, picture->opaque_h = y2 - y1;
}

// Plain top-left integer crop (e.g. Chromium omnibox dropdown): treat as cropped buffer without viewport scaling.
static int wayland_crop_plain(const struct wayland_surf_state *st, const struct wayland_buffer *b, long tw, long th)
{
        return st->src_w > 0 && !st->transform && !b->y_invert && !st->src_x && !st->src_y && st->scale == 1 &&
               !(st->src_w & 255) && !(st->src_h & 255) && st->src_w / 256 <= b->width &&
               st->src_h / 256 <= b->height && tw == st->src_w / 256 && th == st->src_h / 256;
}

static void wayland_fill_picture_meta(struct wayland_picture *picture, const struct wayland_surf_state *st, const struct wayland_buffer *b,
                                      const struct wayland_pool *p, long tw, long th)
{
        long bw_t, bh_t;

        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        picture->pixels = p->unmapped ? 0 : (const unsigned char *)p->at + b->offset;
        picture->map = p->unmapped ? 0 : (const unsigned char *)p->at;
        picture->map_bytes = p->unmapped ? 0 : p->size;
        picture->stride = b->stride, picture->width = b->width, picture->height = b->height;
        picture->format = b->format, picture->sync_fd = p->dmabuf ? p->fd : -1;
        picture->explicit_sync = b->explicit_sync, picture->pool_id = p->id;
        picture->transform = st->transform, picture->y_invert = b->y_invert;
        picture->fourcc = b->fourcc ? b->fourcc : (unsigned int)(b->format == WLN_wl_shm_format_argb8888 ? WAYLAND_FOURCC_ARGB8888
                                                                : b->format == WLN_wl_shm_format_xrgb8888 ? WAYLAND_FOURCC_XRGB8888
                                                                                                          : (unsigned long)b->format);
        picture->modifier = b->modifier;
        picture->plane_count = b->plane_count > 0 ? b->plane_count : 1;
        for (long k = 0; k < 4; k++)
        {
                picture->plane_fd[k] = !p->dmabuf || k >= picture->plane_count ? -1 : k == 0 ? p->fd : p->extra_fd[k - 1];
                picture->plane_offset[k] = k < picture->plane_count ? b->plane_offset[k] : 0;
                picture->plane_stride[k] = k < picture->plane_count ? b->plane_stride[k] : 0;
        }
        if (st->src_w > 0)
                picture->src_x = st->src_x * st->scale, picture->src_y = st->src_y * st->scale,
                picture->src_width = st->src_w * st->scale, picture->src_height = st->src_h * st->scale;
        if (tw != bw_t || th != bh_t || st->src_w > 0)
                picture->dst_width = tw, picture->dst_height = th;
        if (wayland_crop_plain(st, b, tw, th))
                picture->width = tw, picture->height = th,
                picture->src_x = picture->src_y = picture->src_width = picture->src_height = 0,
                picture->dst_width = picture->dst_height = 0;
}

static void wayland_picture_init(struct wayland_picture *picture, const struct wayland_surf_state *st, const struct wayland_buffer *b,
                                 const struct wayland_pool *p, long tw, long th, long dest_x, long dest_y, long row_w, long row_h)
{
        wayland_zero(picture, sizeof *picture);
        wayland_fill_picture_meta(picture, st, b, p, tw, th);
        picture->dest_x = dest_x, picture->dest_y = dest_y;
        picture->row_width = row_w, picture->row_height = row_h;
        picture->full = 1;
}

static void wayland_picture_clip_damage(struct wayland_picture *picture, const struct wayland_surf_state *st,
                                        long scale120, long bw, long bh)
{
        unsigned long long kept;

        for (long at = 0; at < st->damages; at++)
                picture->damage[at] = st->damage_kind[at] ? st->damage[at] : wayland_cover(st->damage[at], scale120);
        kept = rects_clip((long long *)picture->damage, (unsigned long long)st->damages, (unsigned long long)bw, (unsigned long long)bh);
        picture->damages = (long)(kept << 1 >> 1);
        picture->full = (kept >> 63) || !picture->damages;
}

static void wayland_child_copy(struct wayland_client *c, struct wayland_surface *child, const struct wayland_rect *only,
                               const struct wayland_rect *window)
{
        struct wayland_surface *root = wayland_root_of(c, child);
        struct wayland_picture picture;
        struct wayland_buffer *b;
        long ox, oy, wx = window->x, wy = window->y, ww = window->width, wh = window->height;
        long tw, th;
        unsigned int shown = child->held ? child->held : child == root ? child->lent : 0;

        if (root->row < 0 || !shown || (child != root && !child->sub_parent))
                return;
        b = &c->buffers[shown - 1];
        wayland_sub_offset(c, child, &ox, &oy);
        wayland_surface_target_pixels(child, &child->cur, b, &tw, &th);
        wayland_picture_init(&picture, &child->cur, b, &c->pools[b->pool], tw, th, ox - wx, oy - wy, ww, wh);
        picture.transparent = !wayland_surface_is_opaque(child, b);
        picture.blend = child != root && picture.transparent;
        if (only && !picture.src_width && !picture.dst_width && !picture.transform && !picture.y_invert)
        {
                long x1 = wayland_max(only->x - ox, 0), y1 = wayland_max(only->y - oy, 0);
                long x2 = wayland_min(only->x + only->width - ox, tw), y2 = wayland_min(only->y + only->height - oy, th);

                if (x2 <= x1 || y2 <= y1)
                        return;
                picture.full = 0, picture.damages = 1;
                picture.damage[0] = (struct wayland_rect){x1, y1, x2 - x1, y2 - y1};
        }
        root->laying = 1;
        if (wayland.host->row_picture(wayland.host->context, root->row, &picture) > 0)
                root->need_full = 1;
        root->budget_used += picture.full ? wayland_min(tw, ww) * wayland_min(th, wh) : picture.damage[0].width * picture.damage[0].height;
}

static struct wayland_rect wayland_flatten_reach(struct wayland_client *c, struct wayland_surface *root, struct wayland_rect only)
{
        if (!wayland_has_children(root))
                return only;
        for (struct wayland_surface *ch = wayland_tree_next(c, root, root); ch; ch = wayland_tree_next(c, root, ch))
        {
                long ox, oy, tw, th;
                struct wayland_buffer *b;

                if (!ch->held || !(ch->cur.src_w > 0 || ch->cur.dst_w > 0 || ch->cur.transform || c->buffers[ch->held - 1].y_invert))
                        continue;
                b = &c->buffers[ch->held - 1];
                wayland_surface_target_pixels(ch, &ch->cur, b, &tw, &th);
                if (wayland_crop_plain(&ch->cur, b, tw, th))
                        continue;
                wayland_sub_offset(c, ch, &ox, &oy);
                if (ox < only.x + only.width && oy < only.y + only.height && ox + ch->width > only.x && oy + ch->height > only.y)
                        only = wayland_rect_union(only, (struct wayland_rect){ox, oy, ch->width, ch->height});
        }
        return only;
}

static void wayland_flatten(struct wayland_client *c, long parent, const struct wayland_rect *only, const struct wayland_rect *window)
{
        struct wayland_surface *s = &c->surfaces[parent < 0 ? ~parent : parent];

        for (int side = 0; side < 2; side++)
        {
                if (side && parent < 0 && s->held)
                        wayland_child_copy(c, s, only, window);
                for (unsigned int ch = s->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                        wayland_flatten(c, ~(long)(ch - 1), only, window);
        }
}

/*
        ---- a window's subsurfaces as layers (row_layers) ----

        A window's subsurfaces are handed to the host as layers of their own over the root's picture, each read where its client keeps
        it, rather than laid into one picture (wayland_flatten): what a subsurface's commit costs the compositor is the composing of its
        damage from its buffer, nothing copied, and the window is drawn again only where that damage is. The list is the subsurfaces that
        show a buffer, in the order wayland_flatten lays them. One under the root is a layer only where the root, opaque, hides it whole
        (it is left out); one the root shows through, or more than WAYLAND_LAYERS, and the window is laid as one picture as before, as it
        is whenever the host refuses (a picture that needs turning or scaling, a corner, no room). A layer's buffer is held as a lent
        picture's (lent, lent_gone), and so is one of a subsurface that goes (gone), until the host has been told and a frame is drawn.
*/

// A buffer read in place as a surface's picture (index and one): held once more as the surface's lent buffer, and the one it replaced is the
// one that goes when the frame that shows the new one is drawn, as a window's lent picture (wayland_show).
static void wayland_lend_hold(struct wayland_client *c, struct wayland_surface *s, unsigned int now, struct wayland_buffer *now_buf)
{
        if (!now || s->lent == now)
                return;
        if (s->lent_gone == now)
                s->lent_gone = 0; // its hold is the picture's again
        else
        {
                c->buffers[now - 1].held++;
                if (s->lent_gone)
                        wayland_gone_let(c, s, now_buf);
        }
        if (s->lent)
                s->lent_gone = s->lent;
        s->lent = now;
}

// A surface's picture read in place no more: its lent buffer is the one that goes when the next frame is drawn.
static void wayland_lend_end(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *now_buf)
{
        if (!s->lent)
                return;
        if (s->lent_gone)
                wayland_gone_let(c, s, now_buf);
        s->lent_gone = s->lent;
        s->lent = 0;
}

// Submit a window picture to the host and apply the 4-state lent buffer transition (unified across wayland_show & wayland_root_again).
static long wayland_picture_submit(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *b,
                                   const struct wayland_picture *picture, int promote_held, int *lent_out)
{
        unsigned int now = (unsigned int)((b - c->buffers) + 1);
        long status = wayland.host->row_picture(wayland.host->context, s->row, picture);

        if (lent_out)
                *lent_out = 0;
        if (status == WAYLAND_PICTURE_LENT)
        {
                wayland_lend_hold(c, s, now, promote_held ? 0 : b);
                if (lent_out)
                        *lent_out = 1;
                return 0;
        }
        if (status == 0)
        {
                if (promote_held && !s->held && s->lent == now)
                        s->held = s->lent, s->lent = 0;
                else if (s->lent)
                        wayland_lend_end(c, s, promote_held ? 0 : b);
        }
        else if (status > 0)
                s->need_full = 1;
        return status;
}

// The buffers a surface that goes was read in place from (it hangs from no window, or is gone): kept by its client until a frame is drawn
// without them (wayland_gone_drawn), let go of at once only when the memory for the list is not there.
static void wayland_lend_leave(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned int kept[4] = {s->lent, s->lent_gone, s->lent_latched, s->lent_shown};

        s->lent = s->lent_gone = s->lent_latched = s->lent_shown = 0;
        for (long at = 0; at < 4; at++)
                if (kept[at] && !wayland_ids_add(&c->gone, kept[at]))
                        wayland_let_go(c, kept[at] - 1);
}

// A frame drawn without the layers of subsurfaces that went: their buffers are their client's again (a dma-buf's once it is presented, as
// the card may have shown it until the flip; dmabufs 1).
static void wayland_gone_drawn(struct wayland_client *c, int dmabufs)
{
        long kept = 0;

        for (long at = 0; at < c->gone.count; at++)
        {
                long index = c->gone.id[at] - 1;

                if (!dmabufs && c->pools[c->buffers[index].pool].dmabuf)
                        c->gone.id[kept++] = c->gone.id[at];
                else
                        wayland_let_go(c, index);
        }
        c->gone.count = kept;
}

// The list being made: the pictures, the window, and the root's picture in the window's pixels and whether it is opaque.
struct wayland_layering
{
        struct wayland_picture *list;
        long count;
        int refused;
        struct wayland_surface *root;
        struct wayland_rect window, root_box;
        int root_opaque;
};

static void wayland_layer_add(struct wayland_client *c, struct wayland_surface *s, struct wayland_layering *l, int below)
{
        struct wayland_buffer *b = &c->buffers[s->held - 1];
        struct wayland_picture *p;
        long ox, oy;

        if (l->count >= WAYLAND_LAYERS)
        {
                l->refused = 1;
                return;
        }
        p = &l->list[l->count];
        wayland_sub_offset(c, s, &ox, &oy);
        wayland_picture_init(p, &s->cur, b, &c->pools[b->pool], s->width, s->height,
                             ox - l->window.x, oy - l->window.y, l->window.width, l->window.height);
        p->blend = p->transparent = !wayland_surface_is_opaque(s, b);
        p->layer = wayland_surface_index(c, s) + 1;
        if (below)
        {
                if (!l->root_opaque || !wayland_rect_contains(l->root_box, (struct wayland_rect){p->dest_x, p->dest_y, s->width, s->height}))
                        l->refused = 1;
                return;
        }
        l->count++;
}

static void wayland_layers_walk(struct wayland_client *c, long self, struct wayland_layering *l, int below, long depth)
{
        struct wayland_surface *s = &c->surfaces[self];

        if (depth > c->tbl[WLT_SURFACE].count)
        {
                l->refused = 1;
                return;
        }
        for (int side = 0; side < 2 && !l->refused; side++)
        {
                if (side && s != l->root && s->held)
                        wayland_layer_add(c, s, l, below);
                for (unsigned int ch = s->first_child[side]; ch && !l->refused; ch = c->surfaces[ch - 1].next_sib)
                        wayland_layers_walk(c, (long)(ch - 1), l, below || (!side && s == l->root), depth + 1);
        }
}

static long wayland_layers_make(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *changed, int full,
                                const struct wayland_rect *window, const struct wayland_rect *root_box, int root_opaque,
                                struct wayland_picture *list)
{
        struct wayland_layering l = {list, 0, 0, root, *window, *root_box, root_opaque};

        wayland_layers_walk(c, wayland_surface_index(c, root), &l, 0, 0);
        if (l.refused)
                return -1;
        for (long at = 0; changed && at < l.count; at++)
                if (list[at].layer == wayland_surface_index(c, changed) + 1)
                {
                        struct wayland_picture *p = &list[at];

                        p->changed = p->full = 1;
                        if (!full && changed->pending.damages && !p->transform && !p->src_width && !p->dst_width)
                                wayland_picture_clip_damage(p, &changed->pending, changed->scale120, p->width, p->height);
                }
        return l.count;
}

static void wayland_layers_lent(struct wayland_client *c, struct wayland_surface *root, const struct wayland_picture *list, long count)
{
        for (struct wayland_surface *s = wayland_tree_next(c, root, root); s; s = wayland_tree_next(c, root, s))
        {
                long at = wayland_surface_index(c, s);
                int kept = 0;

                for (long k = 0; list && k < count; k++)
                        kept |= list[k].layer == at + 1;
                if (kept)
                        wayland_lend_hold(c, s, s->held, 0);
                else
                        wayland_lend_end(c, s, 0);
        }
        root->layered = list != 0;
}

static long wayland_root_again(struct wayland_client *c, struct wayland_surface *root, const struct wayland_rect *window,
                               const struct wayland_picture *list, long count)
{
        unsigned int shown = root->held ? root->held : root->lent;
        struct wayland_picture picture;
        struct wayland_buffer *b;

        if (!shown || wayland.host->row_layers(wayland.host->context, root->row, list, count, 1) != WAYLAND_LAYERS_KEPT)
                return 0;
        b = &c->buffers[shown - 1];
        wayland_picture_init(&picture, &root->cur, b, &c->pools[b->pool], root->width, root->height,
                             -window->x, -window->y, window->width, window->height);
        picture.transparent = !wayland_surface_is_opaque(root, b);
        wayland_picture_opaque(&picture, root, &root->cur, root->scale120 > 0 ? root->scale120 : WAYLAND_UNIT,
                               window->x, window->y, window->width, window->height);
        picture.fragile = root->xsurface != 0 && !c->pools[b->pool].dmabuf;
        picture.last = 1;
        wayland_picture_submit(c, root, b, &picture, 1, 0);
        return WAYLAND_LAYERS_KEPT;
}

static int wayland_layers(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *changed, int full)
{
        struct wayland_picture list[WAYLAND_LAYERS];
        unsigned int shown = root->held ? root->held : root->lent;
        struct wayland_rect window, box;
        long count, status = 0;

        if (!wayland.host->row_layers || root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED))
                return 0;
        window = wayland_window_of(c, root);
        box = (struct wayland_rect){-window.x, -window.y, root->width, root->height};
        count = wayland_layers_make(c, root, changed, full, &window, &box, shown && wayland_surface_is_opaque(root, &c->buffers[shown - 1]), list);
        if (count >= 0 && (window.x != root->window_x || window.y != root->window_y || window.width != root->window_w ||
                           window.height != root->window_h))
                status = WAYLAND_LAYERS_ROOT;
        else if (count >= 0)
                status = wayland.host->row_layers(wayland.host->context, root->row, list, count, 0);
        else if (root->layered)
                wayland.host->row_layers(wayland.host->context, root->row, 0, -1, 0);
        if (status == WAYLAND_LAYERS_ROOT)
                status = wayland_root_again(c, root, &window, list, count);
        wayland_layers_lent(c, root, status == WAYLAND_LAYERS_KEPT ? list : 0, count);
        if (status == WAYLAND_LAYERS_KEPT)
                root->window_x = window.x, root->window_y = window.y, root->window_w = window.width, root->window_h = window.height;
        return status == WAYLAND_LAYERS_KEPT;
}

static void wayland_relay(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_rect window;

        if (root->row < 0 || !root->need_full || !(root->flags & WAYLAND_SURFACE_MAPPED))
                return;
        root->need_full = 0;
        if (wayland_layers(c, root, 0, 0))
                return;
        window = wayland_window_of(c, root);
        root->window_x = window.x, root->window_y = window.y, root->window_w = window.width, root->window_h = window.height;
        wayland_flatten(c, ~wayland_surface_index(c, root), 0, &window);
        wayland_row_take(c, root);
}

static inline int wayland_viewport_used(const struct wayland_surf_state *st) { return st->dst_w > 0 || st->src_w > 0; }

static inline int wayland_state_xform_changed(const struct wayland_surf_state *a, const struct wayland_surf_state *b)
{
        return a->transform != b->transform || a->src_x != b->src_x || a->src_y != b->src_y ||
               a->src_w != b->src_w || a->src_h != b->src_h || a->dst_w != b->dst_w || a->dst_h != b->dst_h;
}

static void wayland_state_apply(struct wayland_surface *s, long tw, long th, long scale120)
{
        s->width = tw, s->height = th, s->scale120 = scale120;
        s->cur.scale = s->pending.scale, s->cur.transform = s->pending.transform;
        s->cur.src_x = s->pending.src_x, s->cur.src_y = s->pending.src_y;
        s->cur.src_w = s->pending.src_w, s->cur.src_h = s->pending.src_h;
        s->cur.dst_w = s->pending.dst_w, s->cur.dst_h = s->pending.dst_h;
}

static long wayland_commit_scale(const struct wayland_surface *s, const struct wayland_buffer *b)
{
        (void)b;
        return s->pending.dst_w > 0 ? (s->told120 > 0 ? s->told120 : WAYLAND_UNIT) : WAYLAND_UNIT * s->pending.scale;
}

static long wayland_viewport_error(const struct wayland_surf_state *st, const struct wayland_buffer *b)
{
        long bw_t, bh_t;

        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        if (st->src_w > 0 && ((st->src_x + st->src_w) * st->scale > bw_t * 256 || (st->src_y + st->src_h) * st->scale > bh_t * 256))
                return WLN_wp_viewport_error_out_of_buffer;
        if (st->src_w > 0 && st->dst_w <= 0 && ((st->src_w & 255) || (st->src_h & 255)))
                return WLN_wp_viewport_error_bad_size;
        return 0;
}

static void wayland_sub_apply(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = wayland_root_of(c, s);
        long buffer = -1;

        if (root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED))
        {
                if (root->row < 0)
                {
                        long now = wayland.host->now(wayland.host->context);
                        wayland_callbacks_take(c, s, now);
                        wayland_callbacks_done(c, s, now);
                }
                s->u.sub.dirty = 1;
                return;
        }
        s->u.sub.dirty = 0;
        if (s->cached.opaque_set)
                s->cur.opaque = s->cached.opaque, s->cached.opaque_set = 0;
        if (s->u.sub.pend_pos)
                s->u.sub.x = s->u.sub.pend_x, s->u.sub.y = s->u.sub.pend_y, s->u.sub.pend_pos = 0, root->need_full = 1;
        if (s->pending.attach_made && s->pending.attached)
        {
                buffer = wayland_find(c, s->pending.attached, WLI_wl_buffer);
                if (buffer >= 0)
                {
                        struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                        if ((!wayland_viewport_used(&s->pending) && (b->width % s->pending.scale || b->height % s->pending.scale)) ||
                            wayland_viewport_error(&s->pending, b))
                                buffer = -1;
                }
        }
        if (buffer >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                long ox, oy, tw, th, scale120 = wayland_commit_scale(s, b);
                int full, is_opaque;
                struct wayland_rect window;

                wayland_surface_target_pixels(s, &s->pending, b, &tw, &th);
                full = !s->held || s->width != tw || s->height != th || s->pending.damage_full || !s->pending.damages ||
                       scale120 != s->scale120 || wayland_state_xform_changed(&s->cur, &s->pending);
                wayland_unhold(c, s);
                b->held++;
                s->held = (unsigned int)(c->objects[buffer].link + 1);
                wayland_state_apply(s, tw, th, scale120);
                is_opaque = wayland_surface_is_opaque(s, b);
                wayland_sub_offset(c, s, &ox, &oy);
                window = wayland_window_of(c, root);
                if (wayland_layers(c, root, s, full))
                        root->need_full = 0;
                else if (root->need_full)
                        wayland_relay(c, root);
                else if (!is_opaque || s->u.sub.translucent)
                {
                        struct wayland_rect rect = {ox, oy, tw, th};

                        if (s->u.sub.translucent)
                                rect = wayland_rect_union(rect, (struct wayland_rect){s->u.sub.translucent_x, s->u.sub.translucent_y,
                                                                                      s->u.sub.translucent_w, s->u.sub.translucent_h});
                        if (root->held || root->lent)
                                wayland_child_copy(c, root, &rect, &window);
                        wayland_flatten(c, wayland_surface_index(c, root), &rect, &window);
                }
                else if (full)
                {
                        wayland_child_copy(c, s, 0, &window);
                        wayland_flatten(c, wayland_surface_index(c, s), 0, &window);
                }
                else
                        for (long at = 0; at < s->pending.damages; at++)
                        {
                                struct wayland_rect r = s->pending.damage_kind[at] ? s->pending.damage[at]
                                                                                   : wayland_cover(s->pending.damage[at], s->scale120);
                                r.x += ox, r.y += oy;
                                wayland_child_copy(c, s, &r, &window);
                                wayland_flatten(c, wayland_surface_index(c, s), &r, &window);
                        }
        }
        else if (s->pending.attach_made && !s->pending.attached)
                wayland_unhold(c, s), s->width = s->height = 0, root->need_full = 1;
        else if (s->pending.attach_made)
        {
                long gone = wayland_find(c, s->pending.attached, WLI_wl_buffer);
                if (gone >= 0)
                        wayland_release_buffer(c, gone);
        }
        if (root->need_full)
                wayland_relay(c, root);
        if (s->held)
        {
                struct wayland_buffer *shown = &c->buffers[s->held - 1];
                long px, py, tw, th;

                wayland_sub_offset(c, s, &px, &py);
                wayland_surface_target_pixels(s, &s->cur, shown, &tw, &th);
                s->u.sub.translucent = !wayland_surface_is_opaque(s, shown);
                s->u.sub.translucent_x = px, s->u.sub.translucent_y = py;
                s->u.sub.translucent_w = tw, s->u.sub.translucent_h = th;
        }
        else
                s->u.sub.translucent = 0;
        s->pending.attach_made = s->pending.attached = s->cached.attached = 0;
        s->pending.damages = s->pending.damage_full = 0;
        s->cur.transform = s->pending.transform, s->cur.alpha = s->pending.alpha, s->cur.async_hint = s->pending.async_hint;
        wayland_callbacks_take(c, s, wayland.host->now(wayland.host->context));
        wayland_feedback_take(c, s, 1);
}

static void wayland_sub_apply_children(struct wayland_client *c, long parent)
{
        struct wayland_surface *p = &c->surfaces[parent];

        for (int side = 0; side < 2; side++)
                for (unsigned int ch = p->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                {
                        struct wayland_surface *s = &c->surfaces[ch - 1];

                        if (s->u.sub.dirty || s->u.sub.pend_pos)
                                wayland_sub_apply(c, s);
                        wayland_sub_apply_children(c, (long)(ch - 1));
                }
}

static int wayland_sub_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        struct wayland_surface *parent = s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0;

        if (s->pending.attach_made && s->pending.attached && wayland_find(c, s->pending.attached, WLI_wl_buffer) < 0)
                return wayland_fail(c, self->id, WLN_wl_display_error_invalid_object, "the attached buffer is gone");
        if (!parent || !parent->live)
        {
                long gone = s->pending.attach_made && s->pending.attached ? wayland_find(c, s->pending.attached, WLI_wl_buffer) : -1;
                long now = wayland.host->now(wayland.host->context);

                if (gone >= 0)
                        wayland_release_buffer(c, gone);
                s->pending.attach_made = s->pending.attached = s->pending.damages = 0;
                wayland_callbacks_take(c, s, now);
                wayland_feedback_take(c, s, 0);
                wayland_callbacks_done(c, s, now);
                return WAYLAND_OK;
        }
        if (wayland_sub_synced(c, s))
        {
                if (s->pending.attach_made && s->cached.attached && s->cached.attached != s->pending.attached)
                {
                        long old = wayland_find(c, s->cached.attached, WLI_wl_buffer);
                        if (old >= 0)
                                wayland_release_buffer(c, old);
                }
                if (s->pending.attach_made)
                        s->cached.attached = s->pending.attached;
                s->u.sub.dirty = 1;
                return WAYLAND_OK;
        }
        wayland_sub_apply(c, s);
        wayland_sub_apply_children(c, wayland_surface_index(c, s));
        wayland_row_take(c, wayland_root_of(c, s));
        return WAYLAND_OK;
}

static void wayland_sub_drop(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent ? wayland_root_of(c, s) : 0;

        wayland_unhold(c, s);
        wayland_sub_unlink(c, s);
        s->sub_parent = 0;
        for (int side = 0; side < 2; side++)
                for (unsigned int ch = s->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                        wayland_unhold(c, &c->surfaces[ch - 1]);
        if (root && root != s && root->live)
                root->need_full = 1, wayland_relay(c, root);
        for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                if ((o != s || s->row < 0) && (o->lent || o->lent_gone))
                        wayland_lend_leave(c, o);
        for (int side = 0; side < 2; side++)
        {
                for (unsigned int ch = s->first_child[side], next; ch; ch = next)
                {
                        struct wayland_surface *cs = &c->surfaces[ch - 1];
                        next = cs->next_sib;
                        cs->sub_parent = cs->prev_sib = cs->next_sib = cs->sub_above = 0;
                }
                s->first_child[side] = s->last_child[side] = 0;
        }
}

static void wayland_constraints_drop(struct wayland_client *c, long surface);

static void wayland_surface_drop(struct wayland_client *c, struct wayland_surface *s)
{
        long idx = wayland_surface_index(c, s);

        wayland_constraints_drop(c, idx);
        wayland_sub_drop(c, s);
        wayland_row_release(c, s);
        for (int q = 0; q < 4; q++)
        {
                if (q == WLCB_READY && s->cbq[q].count)
                        c->waiting--;
                if (q == WLCB_PRESENTING && s->cbq[q].count)
                        c->presenting_surfaces--;
                while (s->cbq[q].count)
                {
                        unsigned int id = wayland_cb_pop(c, &s->cbq[q]);
                        long slot = q <= WLCB_READY ? wayland_find(c, id, WLI_wl_callback) : -1;
                        if (slot >= 0)
                                wayland_object_free(c, slot);
                }
        }
        s->presentings_framed = 0;
        if (s->live && s->role == WAYLAND_ROLE_SUBSURFACE)
                c->subsurfaces--;
        if (wayland.pointer_row >= 0 && wayland.row[wayland.pointer_row].client == c->slot && wayland.pointer_surface == idx)
                wayland.pointer_row = -1;
        if (wayland.cursor_client == c->slot && wayland.cursor_surface == idx)
                wayland.cursor_client = wayland.cursor_surface = -1, wayland.cursor_serial++;
        s->pending.sync_acq_set = s->pending.sync_rel_set = 0;
        s->pending.dx = s->pending.dy = 0;
        s->syncobj_id = 0, s->live = 0, s->gen++;
        wayland_tbl_free(c, WLT_SURFACE, idx);
        wayland.surfaces--;
}

/* ---- requests ---- */

/* A new id that cannot be had: out of the client's range, in use, or no room left for another object. */
static int wayland_new_fail(struct wayland_client *c, unsigned long self, unsigned long id)
{
        if (id < 1 || id >= WAYLAND_IDS || (long)id > (c->tbl[WLT_INDEX].count > 1 ? c->tbl[WLT_INDEX].count : 1) + WAYLAND_ID_GAP)
                return wayland_fail(c, self, WLN_wl_display_error_invalid_object, "the new id is out of range");
        if (wayland_slot_of(c, id) >= 0)
                return wayland_fail(c, self, WLN_wl_display_error_invalid_object, "the new id is in use");
        return wayland_fail(c, 1, WLN_wl_display_error_no_memory, "no memory for the object");
}

#define WAYLAND_INVALID_OBJECT WLN_wl_display_error_invalid_object
#define WAYLAND_INVALID_METHOD WLN_wl_display_error_invalid_method
#define WAYLAND_NO_MEMORY WLN_wl_display_error_no_memory
#define WAYLAND_IMPLEMENTATION WLN_wl_display_error_implementation

static int wayland_display_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        (void)self;
        if (op == WLQ_wl_display_sync)
        {
                wayland_ev(c, c->objects[a[0].slot].id, WLE_wl_callback_done, (unsigned int)wayland_serial(c));
                wayland_object_delete(c, a[0].slot);
                return WAYLAND_OK;
        }
        for (long name = 1; name <= WAYLAND_GLOBAL_COUNT; name++)
                if (wayland_global_offered(c, name))
                        wayland_global_send(c, c->objects[a[0].slot].id, name);
        for (long screen = 0; screen < wayland.screens; screen++)
                wayland_global_send(c, c->objects[a[0].slot].id, wayland_output_name(screen));
        return WAYLAND_OK;
}

WAYLAND_API void wayland_xwayland_client(long slot);

static int wayland_registry_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long name = (long)a[0].u, iface, version, slot;
        unsigned long id = a[1].u;

        (void)op;
        if (name >= 1 && name <= WAYLAND_GLOBAL_COUNT && wayland_global_offered(c, name))
                iface = wayland_globals[name - 1].iface, version = wayland_globals[name - 1].version;
        else if (wayland_output_of(name) >= 0)
                iface = WLI_wl_output, version = 4;
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
                c->objects[slot].aux = (unsigned short)wayland_output_of(name), wayland_output_send(c, slot);
        else if (iface == WLI_wl_shm)
        {
                static const unsigned int fmts[4] = {WLN_wl_shm_format_argb8888, WLN_wl_shm_format_xrgb8888,
                                                     WLN_wl_shm_format_abgr8888, WLN_wl_shm_format_xbgr8888};
                for (long at = 0; at < 4; at++)
                        wayland_ev(c, id, WLE_wl_shm_format, fmts[at]);
        }
        else if (iface == WLI_zwp_linux_dmabuf_v1 && a[1].version < 4)
                wayland_dmabuf_formats(c, id, a[1].version);
        else if (iface == WLI_wl_drm)
        {
                if (wayland.drm_node[0])
                        wayland_ev_str(c, id, WLE_wl_drm_device, wayland.drm_node);
                if (a[1].version >= 2)
                        wayland_ev(c, id, WLE_wl_drm_capabilities, WLN_wl_drm_capability_prime);
                for (long at = 0; at < wayland.dmabuf.count; at++)
                {
                        int again = 0;
                        for (long before = 0; before < at && !again; before++)
                                again = wayland.dmabuf.entry[before].format == wayland.dmabuf.entry[at].format;
                        if (!again)
                                wayland_ev(c, id, WLE_wl_drm_format, wayland.dmabuf.entry[at].format);
                }
        }
        else if (iface == WLI_xdg_toplevel_icon_manager_v1)
                wayland_ev0(c, id, WLE_xdg_toplevel_icon_manager_v1_done);
        else if (iface == WLI_wp_presentation)
                wayland_ev(c, id, WLE_wp_presentation_clock_id, 1u);
        else if (iface == WLI_wl_seat)
        {
                wayland_ev(c, id, WLE_wl_seat_capabilities, (unsigned int)wayland.host->seat(wayland.host->context));
                if (a[1].version >= 2)
                        wayland_ev_str(c, id, WLE_wl_seat_name, "seat0");
        }
        return WAYLAND_OK;
}

static void wayland_scale_tell(struct wayland_client *c, struct wayland_surface *s, long percent)
{
        if (c->xspace)
                percent = 100;
        long scale120 = wayland_percent120(percent), whole = wayland_whole_scale(percent), slot;

        if (s->told120 == scale120 && s->told_whole == whole)
                return;
        if (s->fractional_id && s->told120 != scale120)
                wayland_ev(c, s->fractional_id, WLE_wp_fractional_scale_v1_preferred_scale, (unsigned int)scale120);
        slot = wayland_find(c, s->id, WLI_wl_surface);
        if (slot >= 0 && c->objects[slot].version >= 6 && s->told_whole != whole)
                wayland_ev(c, s->id, WLE_wl_surface_preferred_buffer_scale, (unsigned int)whole);
        s->told120 = scale120, s->told_whole = whole;
}

static int wayland_compositor_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long slot, screen_scale = wayland.screens ? wayland.screen[0].scale : 100;
        struct wayland_surface *s;

        (void)self;
        if (op == WLQ_wl_compositor_create_region)
        {
                long rslot = wayland_tbl_alloc(c, WLT_REGION);

                if (rslot < 0)
                        return wayland_fail(c, c->objects[a[0].slot].id, WAYLAND_NO_MEMORY, "no memory for the region");
                c->regions[rslot].live = 1, c->objects[a[0].slot].link = (int)rslot;
                return WAYLAND_OK;
        }
        slot = wayland_surface_slot(c);
        if (slot < 0)
                return wayland_fail(c, c->objects[a[0].slot].id, WAYLAND_NO_MEMORY, "too many surfaces");
        s = &c->surfaces[slot];
        s->live = 1, s->id = c->objects[a[0].slot].id, s->row = -1;
        s->cur.scale = s->pending.scale = 1, s->cur.alpha = s->pending.alpha = 0xffffffffu, s->scale120 = WAYLAND_UNIT;
        c->objects[a[0].slot].link = (int)slot, c->objects[a[0].slot].aux = s->gen;
        wayland.surfaces++;
        wayland_scale_tell(c, s, screen_scale);
        if (c->objects[a[0].slot].version >= 6)
                wayland_ev(c, s->id, WLE_wl_surface_preferred_buffer_transform, WLN_wl_output_transform_normal);
        return WAYLAND_OK;
}

// Track largest guaranteed-interior rect (exact) for Chromium rounded-corner opaque regions alongside exact rect set.
static void wayland_region_add_exact(struct wayland_region *r, struct wayland_rect add)
{
        struct wayland_rect a = r->rect, joined = a;
        long ax2 = a.x + a.width, ay2 = a.y + a.height, nx2 = add.x + add.width, ny2 = add.y + add.height;

        if (!r->exact || wayland_rect_contains(add, a))
        {
                r->exact = 1, r->rect = add;
                return;
        }
        if (wayland_rect_contains(a, add))
                return;
        if (add.y <= a.y && ny2 >= ay2 && add.x <= ax2 && nx2 >= a.x)
                joined.x = wayland_min(a.x, add.x), joined.width = wayland_max(ax2, nx2) - joined.x;
        else if (add.x <= a.x && nx2 >= ax2 && add.y <= ay2 && ny2 >= a.y)
                joined.y = wayland_min(a.y, add.y), joined.height = wayland_max(ay2, ny2) - joined.y;
        r->rect = add.width * add.height > joined.width * joined.height ? add : joined;
}

static void wayland_region_add_rect(struct wayland_region *r, struct wayland_rect add)
{
        wayland_region_add_exact(r, add);
        r->bounds = !r->count && !r->overflow ? add : wayland_rect_union(r->bounds, add);
        if (r->overflow)
                return;
        for (long i = 0; i < r->count;)
        {
                struct wayland_rect b = r->rects[i];

                if (wayland_rect_contains(b, add))
                        return;
                if (wayland_rect_contains(add, b))
                        r->rects[i] = r->rects[--r->count];
                else if ((b.y == add.y && b.height == add.height && add.x <= b.x + b.width && add.x + add.width >= b.x) ||
                         (b.x == add.x && b.width == add.width && add.y <= b.y + b.height && add.y + add.height >= b.y))
                        add = wayland_rect_union(b, add), r->rects[i] = r->rects[--r->count];
                else
                        i++;
        }
        if (r->count < WAYLAND_REGION_RECTS)
                r->rects[r->count++] = add;
        else
                r->overflow = 1;
}

static void wayland_region_sub_overflow(struct wayland_region *r, struct wayland_rect sub)
{
        struct wayland_rect p[4];
        long n = wayland_rect_sub(r->bounds, sub, p);

        if (n == 0)
                r->count = r->overflow = 0, r->bounds = (struct wayland_rect){0, 0, 0, 0};
        else if (n == 1)
                r->bounds = p[0];
}

static void wayland_region_sub_rect(struct wayland_region *r, struct wayland_rect sub)
{
        struct wayland_rect out[WAYLAND_REGION_RECTS], p[4];
        long out_count = 0;
        int over = 0;

        r->exact = 0, r->rect.width = r->rect.height = 0;
        if (r->overflow)
        {
                wayland_region_sub_overflow(r, sub);
                return;
        }
        for (long i = 0; i < r->count; i++)
        {
                long n = wayland_rect_sub(r->rects[i], sub, p);
                for (long k = 0; k < n; k++)
                        if (out_count < WAYLAND_REGION_RECTS)
                                out[out_count++] = p[k];
                        else
                                over = 1;
        }
        if (over)
        {
                r->bounds = wayland_rect_bounds(r->rects, r->count);
                r->count = 0, r->overflow = 1;
                wayland_region_sub_overflow(r, sub);
                return;
        }
        r->count = (unsigned char)out_count;
        for (long i = 0; i < out_count; i++)
                r->rects[i] = out[i];
        r->bounds = out_count ? wayland_rect_bounds(out, out_count) : (struct wayland_rect){0, 0, 0, 0};
        if (out_count == 1)
                r->exact = 1, r->rect = out[0];
}

static int wayland_region_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_region *r = self->link >= 0 && self->link < c->tbl[WLT_REGION].count ? &c->regions[self->link] : 0;

        if (op == WLQ_wl_region_destroy)
                return wayland_region_free(c, self->link), self->link = -1, WAYLAND_OK;
        if (a[2].i <= 0 || a[3].i <= 0)
                return WAYLAND_OK;
        if (op == WLQ_wl_region_add)
        {
                self->flags |= 1;
                if (r)
                        wayland_region_add_rect(r, (struct wayland_rect){a[0].i, a[1].i, a[2].i, a[3].i});
        }
        else if (op == WLQ_wl_region_subtract && r && (r->count || r->overflow))
        {
                wayland_region_sub_rect(r, (struct wayland_rect){a[0].i, a[1].i, a[2].i, a[3].i});
                if (!r->count && !r->overflow)
                        self->flags &= ~1u;
        }
        return WAYLAND_OK;
}

static void wayland_damage_add(struct wayland_surface *s, struct wayland_arg *a, int buffer)
{
        struct wayland_rect r = {a[0].i, a[1].i, a[2].i, a[3].i};
        long last;

        if (r.width <= 0 || r.height <= 0)
                return;
        r.x = wayland_clamp(r.x, -(1L << 30), 1L << 30), r.y = wayland_clamp(r.y, -(1L << 30), 1L << 30);
        r.width = wayland_min(r.width, 1L << 30), r.height = wayland_min(r.height, 1L << 30);
        if (s->pending.damages < WAYLAND_DAMAGE)
        {
                s->pending.damage[s->pending.damages] = r;
                s->pending.damage_kind[s->pending.damages++] = (unsigned char)buffer;
                return;
        }
        last = s->pending.damages - 1;
        if (s->pending.damage_kind[last] != buffer)
                s->pending.damage_full = 1;
        else
                s->pending.damage[last] = wayland_rect_union(s->pending.damage[last], r);
}

static void wayland_pool_drop(struct wayland_client *c, long pool)
{
        struct wayland_pool *p = &c->pools[pool];

        if (!p->unmapped && p->at)
                wayland.host->unmap(wayland.host->context, p->at, p->size);
        if (p->dmabuf)
        {
                wayland.host->close(wayland.host->context, p->fd);
                wayland.fds--;
                for (long k = 0; k < 3; k++)
                        if (p->extra_fd[k] >= 0)
                        {
                                wayland.host->close(wayland.host->context, p->extra_fd[k]);
                                wayland.fds--;
                                p->extra_fd[k] = -1;
                        }
                p->dmabuf = 0;
                p->unmapped = 0;
                c->dmabufs--;
                wayland.dmabufs--;
        }
        p->live = 0;
        wayland_tbl_free(c, WLT_POOL, pool);
        wayland.pools--;
}

static void wayland_pool_unref(struct wayland_client *c, long pool)
{
        struct wayland_pool *p = &c->pools[pool];

        if (!p->live || p->refs > 0 || !p->gone)
                return;
        wayland_pool_drop(c, pool);
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
        unsigned int now = screen >= 0 && screen < WAYLAND_SCREENS ? 1u << screen : 0;

        if (now == s->entered)
                return;
        for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
        {
                struct wayland_object *o = &c->objects[at];

                if (o->iface != WLI_wl_output || o->aux >= WAYLAND_SCREENS)
                        continue;
                if ((s->entered & (1u << o->aux)) && !(now & (1u << o->aux)))
                        wayland_ev(c, s->id, WLE_wl_surface_leave, o->id);
                else if (!(s->entered & (1u << o->aux)) && (now & (1u << o->aux)))
                        wayland_ev(c, s->id, WLE_wl_surface_enter, o->id);
        }
        s->entered = now;
}

/*
        The row an X popup hangs from: the first shown X window of the same Xwayland that is not a popup itself, and where the popup
        is in relation to it (the popup's place in the output less the window's), -1 when there is none.
*/
static long wayland_xparent(struct wayland_client *c, const struct wayland_surface *s, long *px, long *py)
{
        long best_row = -1, best_x = 0, best_y = 0, best_score = 0;

        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
        {
                const struct wayland_surface *o = &c->surfaces[at];
                struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};
                long score = 1;

                if (o == s || !o->live || !o->xsurface || !o->u.xwin.adopted || o->u.xwin.popup || o->row < 0)
                        continue;
                if (wayland.host->row_facts(wayland.host->context, o->row, &facts) < 0)
                        continue;
                if (o->row == wayland.pointer_row)
                        score = 4;
                else if (o->row == wayland.keyboard_row || (facts.states & WAYLAND_STATE_ACTIVATED))
                        score = 3;
                else if (facts.width > 0 && facts.height > 0 && s->u.xwin.x >= facts.x && s->u.xwin.x < facts.x + facts.width &&
                         s->u.xwin.y >= facts.y && s->u.xwin.y < facts.y + facts.height)
                        score = 2;
                if (score > best_score)
                {
                        best_score = score;
                        best_row = o->row;
                        best_x = facts.x;
                        best_y = facts.y;
                }
        }
        if (best_row >= 0)
        {
                *px = s->u.xwin.x - best_x;
                *py = s->u.xwin.y - best_y;
        }
        return best_row;
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
        int first = s->row < 0, lent = 0;
        long wx, wy, ww, wh, tw, th, status;

        wayland_zero(&picture, sizeof picture);
        wayland_surface_target_pixels(s, &s->pending, b, &tw, &th);

        if (first)
        {
                int parent = -1, popup = s->role == WAYLAND_ROLE_POPUP, placed;
                long open_x = popup ? s->u.popup.x : 0, open_y = popup ? s->u.popup.y : 0;
                struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};

                if (popup)
                {
                        struct wayland_surface *above = s->parent ? &c->surfaces[s->parent - 1] : 0;

                        if (above && above->live && above->row >= 0)
                                parent = above->row;
                        if (parent < 0)
                                return WAYLAND_OK;
                }
                else if (s->xsurface && s->u.xwin.popup)
                {
                        // An override-redirect X window is a popup of the window it is over, at its place in the output; with no
                        // window to hang from it is shown as a window.
                        long px = 0, py = 0;

                        parent = (int)wayland_xparent(c, s, &px, &py);
                        if (parent >= 0)
                        {
                                popup = 1;
                                open_x = px;
                                open_y = py;
                        }
                        s->u.xwin.alone = parent < 0;
                }
                else
                {
                        // A toplevel's parent (xdg_toplevel.set_parent, an X transient's window) is the window it is centred on.
                        struct wayland_surface *above = !s->xsurface && s->parent ? &c->surfaces[s->parent - 1] : 0;

                        parent = above && above->live && above->row >= 0 ? (int)above->row : s->xsurface && s->u.xwin.owner > 0 ? (int)s->u.xwin.owner - 1 : -1;
                }
                // An X window that placed itself, or an override-redirect one with nothing to hang from, is where X says.
                placed = !popup && s->xsurface && (s->u.xwin.alone || s->u.xwin.placed);
                s->row = wayland.host->row_open(wayland.host->context, c->slot,
                                                popup ? WAYLAND_KIND_POPUP : placed ? WAYLAND_KIND_PLACED : WAYLAND_KIND_TOPLEVEL,
                                                parent, placed ? s->u.xwin.x : open_x, placed ? s->u.xwin.y : open_y);
                // No row of the table is free: the window is not shown yet, and each commit asks again, so it is shown when one is let go
                // of. Its client goes on, as a popup with nothing to hang from does: ending it ended Xwayland, every X program with it,
                // at the desktop's 33rd window.
                if (s->row >= 0 && !wayland_rows_reach(s->row))
                {
                        wayland.host->row_close(wayland.host->context, s->row);
                        s->row = -1;
                }
                if (s->row < 0)
                {
                        s->row = -1;
                        return WAYLAND_OK;
                }
                wayland.row[s->row].client = c->slot;
                wayland.row[s->row].surface = wayland_surface_index(c, s);
                wayland.rows++;
                // The tab says the title, or what the program is called when it has set none.
                if (s->title[0] || s->app_id[0])
                        wayland.host->row_title(wayland.host->context, s->row, s->title[0] ? s->title : s->app_id, 0);
                if (s->app_id[0] && wayland.host->row_app)
                        wayland.host->row_app(wayland.host->context, s->row, s->app_id);
                if (wayland.host->row_facts(wayland.host->context, s->row, &facts) >= 0)
                        wayland_surface_screens(c, s, wayland_screen_at(facts.x, facts.y));
        }
        wayland_window_rect(c, s, tw, th, scale120, &wx, &wy, &ww, &wh);
        wayland_picture_init(&picture, &s->pending, b, p, tw, th, -wx, -wy, ww, wh);
        picture.transparent = !wayland_surface_is_opaque_at(s, &s->pending, b);
        wayland_picture_opaque(&picture, s, &s->pending, scale120, wx, wy, ww, wh);
        picture.fragile = s->xsurface != 0 && !p->dmabuf;
        picture.last = !wayland_has_children(s);
        picture.full = first || s->width != tw || s->height != th || s->pending.damage_full || scale120 != s->scale120 || !s->pending.damages ||
                       s->need_full || wx != s->window_x || wy != s->window_y || ww != s->window_w || wh != s->window_h ||
                       wayland_state_xform_changed(&s->cur, &s->pending) ||
                       picture.src_width > 0 || picture.dst_width > 0 || picture.transform != 0 || picture.y_invert != 0;
        s->window_x = wx, s->window_y = wy, s->window_w = ww, s->window_h = wh, s->need_full = 0;
        if (!picture.full)
                wayland_picture_clip_damage(&picture, &s->pending, scale120, b->width, b->height);
        if (!picture.last)
        {
                struct wayland_picture list[WAYLAND_LAYERS];
                struct wayland_rect window = {wx, wy, ww, wh}, box = {-wx, -wy, tw, th};
                long count = -1;

                if (wayland.host->row_layers && scale120 == (s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT))
                        count = wayland_layers_make(c, s, 0, 0, &window, &box, !picture.transparent, list);
                if (count >= 0)
                        picture.last = wayland.host->row_layers(wayland.host->context, s->row, list, count, 1) == WAYLAND_LAYERS_KEPT;
                else if (s->layered)
                        wayland.host->row_layers(wayland.host->context, s->row, 0, -1, 0);
                wayland_layers_lent(c, s, picture.last ? list : 0, count);
                s->laying = !picture.last;
        }
        if (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.told && !s->serials && s->row >= 0)
        {
                struct wayland_facts now;

                picture.own_size = wayland.host->row_facts(wayland.host->context, (int)s->row, &now) >= 0 && now.width == s->u.top.told_width &&
                                   now.height == s->u.top.told_height;
        }
        status = wayland_picture_submit(c, s, b, &picture, 0, &lent);
        if (!lent)
                s->budget_used += picture.full ? wayland_min(tw, ww) * wayland_min(th, wh) : 0;
        for (long at = 0; !lent && !picture.full && at < picture.damages; at++)
                s->budget_used += picture.damage[at].width * picture.damage[at].height;
        if (status < 0)
                return wayland_fail(c, self->id, WAYLAND_IMPLEMENTATION, "the window refused the picture");
        s->flags |= WAYLAND_SURFACE_MAPPED;
        s->shown_full = (unsigned char)picture.full;
        if (!picture.full)
                s->shown_rect = wayland_rect_bounds(picture.damage, picture.damages);
        if (first)
        {
                if (s->role == WAYLAND_ROLE_TOPLEVEL)
                {
                        if (s->u.top.layer_shell)
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_LAYER,
                                                     s->u.top.layer_level <= 1 ? -1 : 2, s->u.top.layer_output);
                        if (s->u.top.min_width || s->u.top.min_height)
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_MIN_SIZE, s->u.top.min_width, s->u.top.min_height);
                        if (s->u.top.max_width || s->u.top.max_height)
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_MAX_SIZE, s->u.top.max_width, s->u.top.max_height);
                        if (s->u.top.want_maximized)
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_MAXIMIZE, 0, 0);
                        if (s->u.top.want_fullscreen)
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_FULLSCREEN, s->u.top.want_screen, 0);
                }
                if (s->xsurface && s->u.xwin.alone)
                        wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_PLACE, s->u.xwin.x, s->u.xwin.y);
        }
        return WAYLAND_OK;
}

/*
        wp_commit_timing: whether a commit for the time stamp (ns) waits. The next latch shows what it takes at the vblank the host
        said (wayland_target), and a commit for a time no later than that vblank (an eighth of a refresh to spare, for the clocks'
        rounding and a crystal's drift) is made now, to be shown then: "as close as possible to, but not before". Held until the
        host's clock reached the time, the commit was latched by the first present after it and shown a refresh later: a Vulkan
        client that asked for the vblank its last presented time and the refresh predict was shown at the one after, every frame.
        With no vblank known (a headless compositor, a card that stamps none) it is the host's clock as before.
*/
static int wayland_timing_held(unsigned long stamp)
{
        if (!stamp)
                return 0;
        if (wayland.target && wayland.target_period)
                return stamp > wayland.target + wayland.target_period / 8;
        return (unsigned long)wayland.host->now(wayland.host->context) * 1000000ul < stamp;
}

// When a commit timing holds is next worth asking of, on the host's ms: a refresh before the first vblank it may be shown at.
static long wayland_timing_wake(unsigned long stamp)
{
        if (wayland.target && wayland.target_period && stamp > wayland.target + wayland.target_period / 8)
        {
                unsigned long p = wayland.target_period, ahead = stamp - wayland.target_period / 8 - wayland.target;
                unsigned long shown = wayland.target + (ahead + p - 1) / p * p;

                return (long)((shown - p) / 1000000ul);
        }
        return (long)(stamp / 1000000ul);
}

/*
        The host says the vblank the next latch will be shown at (when, nanoseconds on the monotonic clock) and the refresh period,
        before it hands the clients' requests in: what a commit for a time is held against (wayland_timing_held). 0 for none known.
*/
WAYLAND_API void wayland_target(unsigned long when, unsigned long period)
{
        wayland.target = when;
        wayland.target_period = period;
}

/*
        Whether a commit of s is held now, for wp_fifo or wp_commit_timing: its barrier is set by an earlier commit and no frame has latched
        since that commit (wayland_latch, or wayland_frame for a frame's time with nothing drawn, moves the count on), or it is for a time
        after the vblank the next latch is shown at. A window that is not shown holds nothing, since no frame would come to clear the
        barrier, and a synchronized subsurface is held by its parent's commit, not its own.
*/
static int wayland_hold_wanted(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = wayland_root_of(c, s);

        // An X window's first picture waits for its window manager to say what the window is (a window or a popup, and its title),
        // a moment at most: past it the window is shown as a window, as a client whose window manager never answered would be.
        if (s->xsurface && !s->u.xwin.adopted && !s->u.xwin.dead && s->row < 0 && s->pending.attach_made && s->pending.attached)
        {
                long now = wayland.host->now(wayland.host->context);

                if (!s->u.xwin.wait)
                        s->u.xwin.wait = now + WAYLAND_XWAIT_MS;
                if (now < s->u.xwin.wait)
                        return 1;
                s->u.xwin.adopted = 1;
        }
        if (!root || root->row < 0 || (s->role == WAYLAND_ROLE_SUBSURFACE && s->u.sub.sync))
                return 0;
        if (s->pending.fifo_wait && s->cur.barrier && s->barrier_frame == wayland.latches)
                return 1;
        return wayland_timing_held(s->pending.timestamp);
}

/*
        The pending state a commit takes, once it is sure to be made: its barrier (wp_fifo's set_barrier, up from this frame), its
        wait, its time, and the alpha, icon and tearing hint, which are the pending state at every commit, with or without a
        buffer (the protocol's double buffering). Taken once only: a commit that stalls before it is made again with all of it.
*/
static void wayland_commit_take(struct wayland_client *c, struct wayland_surface *s)
{
        if (s->pending.barrier)
                s->cur.barrier = 1, s->barrier_frame = wayland.latches, s->pending.barrier = 0;
        if (s->pending.opaque_set)
        {
                if (s->role == WAYLAND_ROLE_SUBSURFACE && wayland_sub_synced(c, s))
                        s->cached.opaque = s->pending.opaque, s->cached.opaque_set = 1;
                else
                        s->cur.opaque = s->pending.opaque, s->cached.opaque_set = 0;
                s->pending.opaque_set = 0;
        }
        s->cur.alpha = s->pending.alpha, s->cur.icon = s->pending.icon, s->cur.async_hint = s->pending.async_hint;
        s->cur.fifo_wait = s->pending.fifo_wait, s->pending.fifo_wait = 0;
        s->cur.timestamp = s->pending.timestamp, s->pending.timestamp = 0;
}

static int wayland_syncobj_check(struct wayland_client *c, struct wayland_surface *s, long buffer)
{
        long tl;

        if (!s->pending.sync_acq_set && !s->pending.sync_rel_set)
                return 0;
        if (buffer < 0)
                return wayland_fail(c, s->syncobj_id, WLN_wp_linux_drm_syncobj_surface_v1_error_no_buffer, "no buffer attached for syncobj"), -1;
        if (!c->pools[c->buffers[c->objects[buffer].link].pool].dmabuf)
                return wayland_fail(c, s->syncobj_id, WLN_wp_linux_drm_syncobj_surface_v1_error_unsupported_buffer, "syncobj requires a dma-buf"), -1;
        if (s->pending.sync_acq_set != s->pending.sync_rel_set)
                return wayland_fail(c, s->syncobj_id, s->pending.sync_acq_set ? WLN_wp_linux_drm_syncobj_surface_v1_error_no_release_point
                                                                              : WLN_wp_linux_drm_syncobj_surface_v1_error_no_acquire_point,
                                    s->pending.sync_acq_set ? "missing release point" : "missing acquire point"), -1;
        if (s->pending.sync_acq_slot == s->pending.sync_rel_slot && s->pending.sync_acq_point >= s->pending.sync_rel_point)
                return wayland_fail(c, s->syncobj_id, WLN_wp_linux_drm_syncobj_surface_v1_error_conflicting_points, "acquire and release points conflict"), -1;
        tl = s->pending.sync_acq_slot;
        if (tl >= 0 && tl < c->tbl[WLT_TIMELINE].count && c->timelines[tl].live && c->timelines[tl].handle >= 0 && wayland.syncobj.ready &&
            !wayland.syncobj.ready(wayland.syncobj.context, c->timelines[tl].handle, s->pending.sync_acq_point))
        {
                c->stalled_fd = -1, c->stalled_sync_handle = c->timelines[tl].handle;
                c->stalled_sync_point = s->pending.sync_acq_point, c->budget_surface = -1;
                return 1;
        }
        return 0;
}

static void wayland_syncobj_take(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *b)
{
        long tl = s->pending.sync_rel_slot;

        if (!s->pending.sync_acq_set && !s->pending.sync_rel_set)
        {
                if (b)
                        b->explicit_sync = 0;
                return;
        }
        if (b)
                b->explicit_sync = (unsigned char)(s->pending.sync_acq_set != 0);
        if (s->pending.sync_rel_set && b && tl >= 0 && tl < c->tbl[WLT_TIMELINE].count && c->timelines[tl].live)
        {
                wayland_buffer_signal_release(c, b);
                b->rel_pending = 1, b->rel_timeline = (unsigned short)tl, b->rel_point = s->pending.sync_rel_point;
                c->timelines[tl].refs++;
        }
        s->pending.sync_acq_set = s->pending.sync_rel_set = 0;
}

// Dual readiness check: waits on BOTH explicit DRM syncobj acquire point AND dma-buf implicit kernel fence.
static int wayland_commit_ready(struct wayland_client *c, struct wayland_surface *s, long buffer)
{
        int sync_st = wayland_syncobj_check(c, s, buffer);

        if (sync_st)
                return sync_st < 0 ? WAYLAND_DEAD : WAYLAND_STALL;
        if (buffer >= 0)
        {
                struct wayland_pool *held = &c->pools[c->buffers[c->objects[buffer].link].pool];

                if (held->dmabuf && wayland.host->dmabuf_ready && !wayland.host->dmabuf_ready(wayland.host->context, held->fd))
                {
                        c->stalled_fd = held->fd, c->stalled_sync_handle = -1, c->budget_surface = -1;
                        return WAYLAND_STALL;
                }
        }
        return WAYLAND_OK;
}

static int wayland_commit_quiet(const struct wayland_surface *s, int xdg, int shown)
{
        return !s->pending.attach_made && !s->pending.damages && !s->pending.damage_full && !s->cbq[WLCB_CALLBACK].count &&
               !s->cbq[WLCB_FEEDBACK].count && !s->pending.barrier && !s->pending.opaque_set && s->pending.alpha == s->cur.alpha &&
               s->pending.icon == s->cur.icon && s->pending.async_hint == s->cur.async_hint && !s->pending.fifo_wait &&
               !s->pending.timestamp && !s->pending.sync_acq_set && !s->pending.sync_rel_set && !s->pending.dx &&
               !s->pending.dy && s->role != WAYLAND_ROLE_CURSOR && s->role != WAYLAND_ROLE_SUBSURFACE &&
               (!xdg || (s->flags & WAYLAND_SURFACE_SENT)) && !(s->row < 0 && s->cbq[WLCB_READY].count) &&
               !(shown && s->row >= 0 && (s->flags & WAYLAND_SURFACE_MAPPED) && wayland_has_children(s));
}

static void wayland_hold_buf(struct wayland_client *c, struct wayland_surface *s, long buf_idx)
{
        unsigned int now = (unsigned int)(buf_idx + 1);

        if (s->held == now)
                return;
        wayland_unhold(c, s);
        c->buffers[buf_idx].held++;
        s->held = now;
}

static int wayland_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        long buffer = -1;
        int xdg = (s->flags & WAYLAND_SURFACE_XDG) != 0, initial = 0, ready;
        int shown = xdg || (s->xsurface && s->u.xwin.adopted && !s->u.xwin.dead);

        if (wayland_commit_quiet(s, xdg, shown) && (!xdg || s->role_id) &&
            !(s->role == WAYLAND_ROLE_TOPLEVEL &&
              ((s->u.top.min_width && s->u.top.max_width && s->u.top.min_width > s->u.top.max_width) ||
               (s->u.top.min_height && s->u.top.max_height && s->u.top.min_height > s->u.top.max_height))))
        {
                wayland_commit_take(c, s);
                if (s->cbq[WLCB_PRESENTING].count)
                        wayland_feedback_take(c, s, s->row >= 0);
                wayland.quiet++;
                return WAYLAND_OK;
        }
        if (wayland_hold_wanted(c, s))
        {
                int fifo_hold = s->pending.fifo_wait && s->cur.barrier && s->barrier_frame == wayland.latches;

                c->hold = 1, c->hold_surface = wayland_surface_index(c, s);
                c->hold_at = s->xsurface && s->u.xwin.wait && !s->u.xwin.adopted ? s->u.xwin.wait
                             : (!fifo_hold && s->pending.timestamp) ? wayland_timing_wake(s->pending.timestamp) : 0;
                c->stalled_fd = -1, c->stalled_sync_handle = -1, c->budget_surface = -1;
                return WAYLAND_STALL;
        }
        if (!wayland_budget_left(wayland_root_of(c, s)))
        {
                c->stalled_fd = -1;
                c->stalled_sync_handle = -1;
                c->budget_surface = wayland_surface_index(c, wayland_root_of(c, s));
                return WAYLAND_STALL;
        }
        shown = xdg || (s->xsurface && s->u.xwin.adopted && !s->u.xwin.dead);
        if (s->role == WAYLAND_ROLE_SUBSURFACE)
        {
                long sub_buf = s->pending.attach_made && s->pending.attached ? wayland_find(c, s->pending.attached, WLI_wl_buffer) : -1;

                if ((ready = wayland_commit_ready(c, s, sub_buf)) != WAYLAND_OK)
                        return ready;
                if (sub_buf >= 0)
                        wayland_syncobj_take(c, s, &c->buffers[c->objects[sub_buf].link]);
                wayland_commit_take(c, s);
                return wayland_sub_commit(c, self, s);
        }
        if (xdg)
        {
                if (!s->role_id)
                        return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_not_constructed, "the xdg_surface has no role");
                if (!(s->flags & WAYLAND_SURFACE_SENT))
                {
                        if (s->pending.attach_made && s->pending.attached)
                                return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_unconfigured_buffer, "a buffer before the first configure");
                        initial = 1;
                }
                else if (s->pending.attach_made && s->pending.attached && !(s->flags & WAYLAND_SURFACE_CONFIGURED))
                        return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_unconfigured_buffer,
                                            "a buffer before the first configure was acknowledged");
        }
        if (s->pending.attach_made && s->pending.attached && (buffer = wayland_find(c, s->pending.attached, WLI_wl_buffer)) < 0)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the attached buffer is gone");
        if ((ready = wayland_commit_ready(c, s, buffer)) != WAYLAND_OK)
                return ready;
        wayland_commit_take(c, s);
        if (s->role == WAYLAND_ROLE_TOPLEVEL &&
            ((s->u.top.min_width && s->u.top.max_width && s->u.top.min_width > s->u.top.max_width) ||
             (s->u.top.min_height && s->u.top.max_height && s->u.top.min_height > s->u.top.max_height)))
                return wayland_fail(c, s->role_id, WLN_xdg_toplevel_error_invalid_size, "the minimum size is above the maximum");
        if (initial)
        {
                if (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.layer_shell)
                {
                        long out = s->u.top.layer_output >= 0 && s->u.top.layer_output < wayland.screens ? s->u.top.layer_output : 0;
                        long cw = s->u.top.layer_w > 0 ? s->u.top.layer_w : (wayland.screens ? wayland.screen[out].width : 1920);
                        long ch = s->u.top.layer_h > 0 ? s->u.top.layer_h : (wayland.screens ? wayland.screen[out].height : 1080);

                        s->u.top.layer_configured = 1, s->flags |= WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_CONFIGURED;
                        wayland_ev(c, s->role_id, WLE_zwlr_layer_surface_v1_configure,
                                   (unsigned int)wayland_serial(c), (unsigned int)cw, (unsigned int)ch);
                }
                else if (s->role == WAYLAND_ROLE_TOPLEVEL)
                        wayland_configure(c, s, 1);
        }
        else if (buffer >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                long tw, th, bw_t, bh_t, scale120 = wayland_commit_scale(s, b), error = wayland_viewport_error(&s->pending, b);

                wayland_transformed_size(b->width, b->height, s->pending.transform, &bw_t, &bh_t);
                if (error)
                        return wayland_fail(c, s->viewport_id, error, error == WLN_wp_viewport_error_out_of_buffer
                                                                          ? "the source is not inside the buffer"
                                                                          : "the source is not whole units and there is no destination");
                if (!wayland_viewport_used(&s->pending) && (bw_t % s->pending.scale || bh_t % s->pending.scale))
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_size, "the buffer is not a whole number of the scale");
                wayland_syncobj_take(c, s, b);
                if (shown && wayland_show(c, self, s, b, scale120) < 0)
                        return WAYLAND_DEAD;
                wayland_surface_target_pixels(s, &s->pending, b, &tw, &th);
                wayland_state_apply(s, tw, th, scale120);
                if (shown && s->row >= 0 && wayland_has_children(s))
                {
                        struct wayland_rect window = wayland_window_of(c, s);

                        wayland_hold_buf(c, s, c->objects[buffer].link);
                        if (!s->layered)
                        {
                                struct wayland_rect reach;
                                const struct wayland_rect *only = s->shown_full ? 0 : &s->shown_rect;

                                if (only)
                                {
                                        reach = wayland_flatten_reach(c, s, *only);
                                        if (reach.x != only->x || reach.y != only->y || reach.width != only->width || reach.height != only->height)
                                                wayland_child_copy(c, s, &reach, &window), only = &reach;
                                }
                                wayland_flatten(c, wayland_surface_index(c, s), only, &window);
                        }
                        wayland_sub_apply_children(c, wayland_surface_index(c, s));
                        wayland_row_take(c, s);
                }
                else if (s->role == WAYLAND_ROLE_CURSOR)
                        wayland_hold_buf(c, s, c->objects[buffer].link);
                else
                {
                        unsigned int now = (unsigned int)(c->objects[buffer].link + 1);

                        if (s->held == now)
                                s->held = 0, b->held--;
                        else
                                wayland_unhold(c, s);
                        if (b->held <= 0 && s->lent != now && s->lent_gone != now && s->lent_latched != now && s->lent_shown != now)
                                wayland_release_buffer(c, buffer);
                }
        }
        else if (s->pending.attach_made && !s->pending.attached)
        {
                if (shown)
                        wayland_unhold(c, s), wayland_row_release(c, s),
                        s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED),
                        s->serials = 0, s->entered = 0;
                if (s->role == WAYLAND_ROLE_CURSOR)
                        wayland_unhold(c, s);
                s->width = s->height = 0;
        }
        if (s->role == WAYLAND_ROLE_CURSOR && wayland.cursor_client == c->slot && wayland.cursor_surface == wayland_surface_index(c, s))
        {
                if (wayland.cursor_gen == s->gen)
                        wayland.cursor_hot_x = wayland_clamp(wayland.cursor_hot_x - s->pending.dx, -WAYLAND_DIMENSION, WAYLAND_DIMENSION),
                        wayland.cursor_hot_y = wayland_clamp(wayland.cursor_hot_y - s->pending.dy, -WAYLAND_DIMENSION, WAYLAND_DIMENSION);
                wayland.cursor_serial++;
        }
        s->pending.dx = s->pending.dy = 0;
        if (buffer < 0 && !initial && shown && s->row >= 0 && (s->flags & WAYLAND_SURFACE_MAPPED) && wayland_has_children(s))
        {
                if (s->need_full)
                        wayland_relay(c, s);
                wayland_sub_apply_children(c, wayland_surface_index(c, s));
                wayland_row_take(c, s);
        }
        s->pending.attach_made = s->pending.attached = s->pending.damages = s->pending.damage_full = 0;
        if (s->cbq[WLCB_CALLBACK].count)
                wayland_callbacks_take(c, s, wayland.host->now(wayland.host->context));
        wayland_feedback_take(c, s, s->row >= 0);
        if (s->row < 0 && s->cbq[WLCB_READY].count)
                wayland_callbacks_done(c, s, wayland.host->now(wayland.host->context));
        return WAYLAND_OK;
}

static int wayland_surface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);

        if (!s)
                return WAYLAND_OK;
        switch (op)
        {
        case WLQ_wl_surface_destroy:
                while (s->cbq[WLCB_FEEDBACK].count)
                        wayland_feedback_discard(c, wayland_cb_pop(c, &s->cbq[WLCB_FEEDBACK]));
                wayland_presenting_drop(c, s);
                wayland_surface_drop(c, s);
                return WAYLAND_OK;
        case WLQ_wl_surface_attach:
                if (self->version >= 5 && (a[1].i || a[2].i))
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_offset, "attach with an offset");
                s->pending.attached = a[0].slot >= 0 ? c->objects[a[0].slot].id : 0, s->pending.attach_made = 1;
                if (self->version < 5)
                        s->pending.dx = a[1].i, s->pending.dy = a[2].i;
                return WAYLAND_OK;
        case WLQ_wl_surface_damage:
        case WLQ_wl_surface_damage_buffer:
                return wayland_damage_add(s, a, op == WLQ_wl_surface_damage_buffer), WAYLAND_OK;
        case WLQ_wl_surface_frame:
                if (!wayland_cb_push(c, &s->cbq[WLCB_CALLBACK], c->objects[a[0].slot].id))
                        wayland_callback_answer(c, c->objects[a[0].slot].id, wayland.host->now(wayland.host->context));
                return WAYLAND_OK;
        case WLQ_wl_surface_set_input_region:
                s->input_none = s->input_bounded = s->input_count = 0;
                if (a[0].slot >= 0)
                {
                        struct wayland_object *ro = &c->objects[a[0].slot];
                        struct wayland_region *r = ro->link >= 0 && ro->link < c->tbl[WLT_REGION].count ? &c->regions[ro->link] : 0;

                        if (!(ro->flags & 1))
                                s->input_none = s->input_bounded = 1;
                        else if (r && !r->overflow && r->count > 0 && r->count <= WAYLAND_REGION_RECTS)
                        {
                                s->input_bounded = 1, s->input_count = r->count;
                                for (long i = 0; i < r->count; i++)
                                        s->input_rects[i] = r->rects[i];
                        }
                        else if (r && r->bounds.width > 0 && r->bounds.height > 0)
                                s->input_bounded = 1, s->input_count = 1, s->input_rects[0] = r->bounds;
                }
                return WAYLAND_OK;
        case WLQ_wl_surface_set_opaque_region:
                s->pending.opaque_set = 1;
                s->pending.opaque = a[0].slot >= 0 && c->objects[a[0].slot].link >= 0 &&
                                    c->objects[a[0].slot].link < c->tbl[WLT_REGION].count &&
                                    c->regions[c->objects[a[0].slot].link].exact
                                        ? c->regions[c->objects[a[0].slot].link].rect : (struct wayland_rect){0, 0, 0, 0};
                return WAYLAND_OK;
        case WLQ_wl_surface_offset:
                return s->pending.dx = a[0].i, s->pending.dy = a[1].i, WAYLAND_OK;
        case WLQ_wl_surface_commit:
                return wayland_commit(c, self, s);
        case WLQ_wl_surface_set_buffer_transform:
                if (a[0].i < 0 || a[0].i > 7)
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_transform, "no such transform");
                return s->pending.transform = a[0].i, WAYLAND_OK;
        case WLQ_wl_surface_set_buffer_scale:
                if (a[0].i < 1 || a[0].i > 16)
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_scale, "no such scale");
                return s->pending.scale = a[0].i, WAYLAND_OK;
        default:
                return WAYLAND_OK;
        }
}

static int wayland_shm_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long size = a[2].i, pool;
        void *at;

        (void)op;
        if (size < 1 || size > WAYLAND_POOL_BYTES)
                return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_stride, "the pool's size is out of range");
        pool = wayland_tbl_alloc(c, WLT_POOL);
        if (pool < 0)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the pool");
        at = wayland.host->map(wayland.host->context, a[1].fd, size);
        wayland.host->close(wayland.host->context, a[1].fd);
        wayland.fds--, a[1].fd = -1;
        if (!at)
                return wayland_tbl_free(c, WLT_POOL, pool), wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "not a shared memory file of that size");
        c->pools[pool].live = 1, c->pools[pool].fd = -1;
        c->pools[pool].extra_fd[0] = c->pools[pool].extra_fd[1] = c->pools[pool].extra_fd[2] = -1;
        c->pools[pool].size = size, c->pools[pool].at = at;
        c->objects[a[0].slot].link = (int)pool;
        wayland.pools++;
        return WAYLAND_OK;
}

static int wayland_pool_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long pool = self->link;
        struct wayland_pool *p = &c->pools[pool];

        if (op == WLQ_wl_shm_pool_destroy)
                return p->gone = 1, wayland_pool_unref(c, pool), WAYLAND_OK;
        if (op == WLQ_wl_shm_pool_resize)
        {
                long size = a[0].i;
                void *at;

                if (size < p->size || size > WAYLAND_POOL_BYTES)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "the pool can only grow");
                at = wayland.host->remap(wayland.host->context, p->at, p->size, size);
                if (!at)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "the pool cannot grow");
                p->at = at, p->size = size;
                return WAYLAND_OK;
        }
        {
                long offset = a[1].i, width = a[2].i, height = a[3].i, stride = a[4].i, format = a[5].i, slot;
                struct wayland_buffer *b;

                if (format != WLN_wl_shm_format_argb8888 && format != WLN_wl_shm_format_xrgb8888 &&
                    format != WLN_wl_shm_format_abgr8888 && format != WLN_wl_shm_format_xbgr8888)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_format, "the format is not offered");
                if (offset < 0 || width < 1 || height < 1 || width > WAYLAND_DIMENSION || height > WAYLAND_DIMENSION ||
                    stride < width * 4 || stride > (1L << 20) || (stride & 3) || offset > p->size ||
                    stride * height > p->size - offset)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_stride, "the buffer does not fit the pool");
                slot = wayland_tbl_alloc(c, WLT_BUFFER);
                if (slot < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the buffer");
                b = &c->buffers[slot];
                b->live = 1, b->pool = pool, b->offset = offset, b->width = width, b->height = height, b->stride = stride;
                b->format = format, b->plane_count = 1, b->id = c->objects[a[0].slot].id;
                b->fourcc = format == WLN_wl_shm_format_argb8888 ? WAYLAND_FOURCC_ARGB8888
                            : format == WLN_wl_shm_format_xrgb8888 ? WAYLAND_FOURCC_XRGB8888 : (unsigned int)format;
                c->objects[a[0].slot].link = (int)slot;
                p->refs++, wayland.buffers++;
                return WAYLAND_OK;
        }
}

static int wayland_buffer_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_buffer *b = &c->buffers[self->link];

        (void)op, (void)a;
        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
        {
                struct wayland_surface *s = &c->surfaces[at];

                if (!s->live)
                        continue;
                if (s->pending.attach_made && s->pending.attached == self->id)
                        s->pending.attach_made = 0, s->pending.attached = 0;
                if (s->cached.attached == self->id)
                        s->cached.attached = 0;
        }
        if (b->held > 0)
                return b->zombie = 1, WAYLAND_OK;
        wayland_buffer_signal_release(c, b);
        c->pools[b->pool].refs--;
        wayland_pool_unref(c, b->pool);
        b->live = 0;
        wayland_tbl_free(c, WLT_BUFFER, b - c->buffers);
        wayland.buffers--;
        return WAYLAND_OK;
}

static int wayland_subcompositor_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s, *parent;

        if (op == WLQ_wl_subcompositor_destroy)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[1].slot);
        parent = wayland_surface_of(c, a[2].slot);
        if (!s || !parent)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
        if (s == parent)
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_surface, "a surface cannot hang from itself");
        if (s->xsurface || (s->role != WAYLAND_ROLE_NONE && !(s->role == WAYLAND_ROLE_SUBSURFACE && !s->u.sub.id)) ||
            (s->flags & WAYLAND_SURFACE_XDG))
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_surface, "the surface has a role");
        long hops = 0;
        for (struct wayland_surface *up = parent; up && hops < c->tbl[WLT_SURFACE].count;
             hops++, up = up->role == WAYLAND_ROLE_SUBSURFACE && up->sub_parent ? &c->surfaces[up->sub_parent - 1] : 0)
                if (up == s)
                        return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_parent, "the parent hangs from the surface");
        if (s->role != WAYLAND_ROLE_SUBSURFACE)
                c->subsurfaces++;
        wayland_sub_unlink(c, s);
        s->role = WAYLAND_ROLE_SUBSURFACE, s->sub_parent = (unsigned int)(wayland_surface_index(c, parent) + 1);
        s->u.sub.id = c->objects[a[0].slot].id, s->u.sub.sync = 1;
        s->u.sub.dirty = s->u.sub.pend_pos = 0;
        s->u.sub.x = s->u.sub.y = s->u.sub.pend_x = s->u.sub.pend_y = 0;
        wayland_sub_link_after(c, parent, s, 1, parent->last_child[1]);
        c->objects[a[0].slot].link = (int)wayland_surface_index(c, s), c->objects[a[0].slot].aux = s->gen;
        return WAYLAND_OK;
}

static int wayland_subsurface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects), *other;

        if (!s)
                return WAYLAND_OK;
        switch (op)
        {
        case WLQ_wl_subsurface_destroy:
                wayland_sub_drop(c, s), s->u.sub.id = 0;
                return WAYLAND_OK;
        case WLQ_wl_subsurface_set_position:
                s->u.sub.pend_x = wayland_clamp(a[0].i, -(1L << 20), 1L << 20);
                s->u.sub.pend_y = wayland_clamp(a[1].i, -(1L << 20), 1L << 20);
                s->u.sub.pend_pos = s->u.sub.pend_x != s->u.sub.x || s->u.sub.pend_y != s->u.sub.y;
                return WAYLAND_OK;
        case WLQ_wl_subsurface_place_above:
        case WLQ_wl_subsurface_place_below:
        {
                struct wayland_surface *parent = s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0;
                int above = op == WLQ_wl_subsurface_place_above;

                other = wayland_surface_of(c, a[0].slot);
                if (!other || !parent || (other != parent && !(other->role == WAYLAND_ROLE_SUBSURFACE && other->sub_parent == s->sub_parent && other != s)))
                        return wayland_fail(c, self->id, WLN_wl_subsurface_error_bad_surface, "the surface is not a sibling or the parent");
                wayland_sub_unlink(c, s);
                wayland_sub_link_after(c, parent, s, other == parent ? above : other->sub_above,
                                       other == parent ? (above ? 0 : parent->last_child[0])
                                                       : (above ? (unsigned int)(wayland_surface_index(c, other) + 1) : other->prev_sib));
                wayland_root_of(c, s)->need_full = 1;
                return WAYLAND_OK;
        }
        case WLQ_wl_subsurface_set_sync:
                return s->u.sub.sync = 1, WAYLAND_OK;
        default:
                s->u.sub.sync = 0;
                if (s->sub_parent && !wayland_sub_synced(c, s) && (s->u.sub.dirty || s->u.sub.pend_pos))
                        wayland_sub_apply(c, s), wayland_sub_apply_children(c, wayland_surface_index(c, s)), wayland_row_take(c, wayland_root_of(c, s));
                return WAYLAND_OK;
        }
}

/* ---- xdg_shell ---- */

// Axis-symmetric 1D xdg_positioner solver: place + flip -> slide -> resize.
static long wayland_pos_place1d(long anc_pos, long anc_len, long anc_d, long grav_d, long off, long len, int flip)
{
        long ad = flip ? -anc_d : anc_d, gd = flip ? -grav_d : grav_d;
        long pt = anc_pos + (ad < 0 ? 0 : ad > 0 ? anc_len : anc_len / 2);

        return pt + (flip ? -off : off) - (gd < 0 ? len : gd > 0 ? 0 : len / 2);
}

static void wayland_pos_axis(long anc_pos, long anc_len, long anc_d, long grav_d, long off, long origin,
                             long sc_pos, long sc_len, long flip_ok, long slide_ok, long resize_ok,
                             long *out_pos, long *out_len)
{
        long len = *out_len, sc_end = sc_pos + sc_len;
        long a = wayland_pos_place1d(anc_pos, anc_len, anc_d, grav_d, off, len, 0);
        int out = origin + a < sc_pos || origin + a + len > sc_end;

        if (out && flip_ok)
        {
                long b = wayland_pos_place1d(anc_pos, anc_len, anc_d, grav_d, off, len, 1);
                if (origin + b >= sc_pos && origin + b + len <= sc_end)
                        a = b, out = 0;
        }
        if (out && slide_ok)
        {
                long at = origin + a;
                if (at + len > sc_end) at = sc_end - len;
                if (at < sc_pos) at = sc_pos;
                a = at - origin;
                out = origin + a < sc_pos || origin + a + len > sc_end;
        }
        if (out && resize_ok)
        {
                long lo = wayland_max(origin + a, sc_pos), hi = wayland_min(origin + a + len, sc_end);
                if (hi > lo)
                        a = lo - origin, len = hi - lo;
        }
        *out_pos = a, *out_len = len;
}

static void wayland_popup_place(const struct wayland_positioner *p, long origin_x, long origin_y,
                                const struct wayland_rect *screen, long *x, long *y, long *width, long *height)
{
        static const signed char dir[9][2] = {
                [WLN_xdg_positioner_anchor_none] = {0, 0}, [WLN_xdg_positioner_anchor_top] = {0, -1},
                [WLN_xdg_positioner_anchor_bottom] = {0, 1}, [WLN_xdg_positioner_anchor_left] = {-1, 0},
                [WLN_xdg_positioner_anchor_right] = {1, 0}, [WLN_xdg_positioner_anchor_top_left] = {-1, -1},
                [WLN_xdg_positioner_anchor_bottom_left] = {-1, 1}, [WLN_xdg_positioner_anchor_top_right] = {1, -1},
                [WLN_xdg_positioner_anchor_bottom_right] = {1, 1},
        };
        long adx = p->anchor >= 0 && p->anchor < 9 ? dir[p->anchor][0] : 0;
        long ady = p->anchor >= 0 && p->anchor < 9 ? dir[p->anchor][1] : 0;
        long gdx = p->gravity >= 0 && p->gravity < 9 ? dir[p->gravity][0] : 0;
        long gdy = p->gravity >= 0 && p->gravity < 9 ? dir[p->gravity][1] : 0;

        *width = p->width, *height = p->height;
        wayland_pos_axis(p->anchor_x, p->anchor_width, adx, gdx, p->offset_x, origin_x, screen->x, screen->width,
                         p->adjustment & WLN_xdg_positioner_constraint_adjustment_flip_x,
                         p->adjustment & WLN_xdg_positioner_constraint_adjustment_slide_x,
                         p->adjustment & WLN_xdg_positioner_constraint_adjustment_resize_x, x, width);
        wayland_pos_axis(p->anchor_y, p->anchor_height, ady, gdy, p->offset_y, origin_y, screen->y, screen->height,
                         p->adjustment & WLN_xdg_positioner_constraint_adjustment_flip_y,
                         p->adjustment & WLN_xdg_positioner_constraint_adjustment_slide_y,
                         p->adjustment & WLN_xdg_positioner_constraint_adjustment_resize_y, y, height);
}

static void wayland_popup_configure(struct wayland_client *c, struct wayland_surface *s, const struct wayland_positioner *p,
                                    struct wayland_surface *parent)
{
        struct wayland_rect screen = {0, 0, 1 << 20, 1 << 20};
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};
        long origin_x = 0, origin_y = 0, x, y, w, h, unit = parent ? wayland_surface_unit(parent) : WAYLAND_UNIT;

        if (parent && parent->row >= 0 && wayland.host->row_facts(wayland.host->context, parent->row, &facts) >= 0)
                origin_x = wayland_units(facts.x, unit), origin_y = wayland_units(facts.y, unit);
        for (long at = 0; at < wayland.screens; at++)
        {
                struct wayland_rect each = {wayland_units(wayland.screen[at].x, unit), wayland_units(wayland.screen[at].y, unit),
                                            wayland_units(wayland.screen[at].width, unit), wayland_units(wayland.screen[at].height, unit)};
                if (origin_x + p->anchor_x >= each.x && origin_x + p->anchor_x < each.x + each.width &&
                    origin_y + p->anchor_y >= each.y && origin_y + p->anchor_y < each.y + each.height)
                        screen = each;
        }
        wayland_popup_place(p, origin_x, origin_y, &screen, &x, &y, &w, &h);
        s->u.popup.x = wayland_pixels(x, unit), s->u.popup.y = wayland_pixels(y, unit);
        s->u.popup.width = wayland_pixels(w, unit), s->u.popup.height = wayland_pixels(h, unit);
        if (wayland_find(c, s->role_id, WLI_xdg_popup) < 0 || s->serials >= WAYLAND_SERIALS)
                return;
        wayland_ev(c, s->role_id, WLE_xdg_popup_configure, (unsigned int)x, (unsigned int)y, (unsigned int)w, (unsigned int)h);
        wayland_xdg_send_serial(c, s);
}

static int wayland_positioner_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_positioner *p = &c->positioners[self->link];

        switch (op)
        {
        case WLQ_xdg_positioner_destroy:
                return p->live = 0, wayland_tbl_free(c, WLT_POSITIONER, self->link), WAYLAND_OK;
        case WLQ_xdg_positioner_set_size:
                if (a[0].i < 1 || a[1].i < 1)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "the size must be positive");
                p->width = wayland_min(a[0].i, WAYLAND_DIMENSION), p->height = wayland_min(a[1].i, WAYLAND_DIMENSION);
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_anchor_rect:
                if (a[2].i < 1 || a[3].i < 1)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "the anchor rectangle must be positive");
                p->anchor_x = wayland_clamp(a[0].i, -(1L << 24), 1L << 24);
                p->anchor_y = wayland_clamp(a[1].i, -(1L << 24), 1L << 24);
                p->anchor_width = wayland_min(a[2].i, WAYLAND_DIMENSION), p->anchor_height = wayland_min(a[3].i, WAYLAND_DIMENSION);
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_anchor:
                if (a[0].u > WLN_xdg_positioner_anchor_bottom_right)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "no such anchor");
                return p->anchor = (long)a[0].u, WAYLAND_OK;
        case WLQ_xdg_positioner_set_gravity:
                if (a[0].u > WLN_xdg_positioner_gravity_bottom_right)
                        return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "no such gravity");
                return p->gravity = (long)a[0].u, WAYLAND_OK;
        case WLQ_xdg_positioner_set_constraint_adjustment:
                return p->adjustment = (long)(a[0].u & 63), WAYLAND_OK;
        case WLQ_xdg_positioner_set_offset:
                p->offset_x = wayland_clamp(a[0].i, -(1L << 24), 1L << 24);
                p->offset_y = wayland_clamp(a[1].i, -(1L << 24), 1L << 24);
                return WAYLAND_OK;
        case WLQ_xdg_positioner_set_reactive:
                return p->reactive = 1, WAYLAND_OK;
        default:
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
                for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
                        if (c->objects[at].iface == WLI_xdg_surface)
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_defunct_surfaces,
                                                    "xdg_surfaces still exist");
                return WAYLAND_OK;
        case WLQ_xdg_wm_base_create_positioner:
                slot = wayland_tbl_alloc(c, WLT_POSITIONER);
                if (slot < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the positioner");
                c->positioners[slot].live = 1;
                c->positioners[slot].id = c->objects[a[0].slot].id;
                c->objects[a[0].slot].link = (int)slot;
                return WAYLAND_OK;
        case WLQ_xdg_wm_base_get_xdg_surface:
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                if (s->flags & WAYLAND_SURFACE_XDG)
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has an xdg_surface");
                if (s->xsurface || (s->role != WAYLAND_ROLE_NONE && s->role != WAYLAND_ROLE_TOPLEVEL && s->role != WAYLAND_ROLE_POPUP))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has another role");
                if (s->width || (s->pending.attach_made && s->pending.attached))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_surface_state,
                                            "the surface has a buffer");
                s->flags |= WAYLAND_SURFACE_XDG;
                s->xdg_id = c->objects[a[0].slot].id;
                c->objects[a[0].slot].link = (int)wayland_surface_index(c, s);
                c->objects[a[0].slot].aux = s->gen;
                return WAYLAND_OK;
        default: // pong: the client is alive
                return WAYLAND_OK;
        }
}

/*
        A toplevel that changed while its client had WAYLAND_SERIALS configures unacknowledged was not told (wayland_configure sends none
        then, and keeps what it last told). Once the client has acknowledged some, what the window is now is sent, as the next change
        would have sent it; nothing else would, if the window stays as it is.
*/
/*
        Whether what the compositor says of a toplevel changed only by its ask being answered: a floating window's size goes back to its
        own (0 by 0) once its client drew the size asked, the states the same. Nothing is told for it (the client is at that size, and is
        free to keep it); the shim only takes the 0 by 0 as told, so the next ask of any size is sent. Told, a configure of 0 by 0 a few
        milliseconds after the one it answered had Chromium choose its size again in the middle of its resize, and its page stayed at the
        size before, in a window of the new one, until something else made it draw (seconds).
*/
static int wayland_facts_freed(struct wayland_surface *s, const struct wayland_facts *facts)
{
        if (facts->width || facts->height || facts->states != s->u.top.told_states || !s->u.top.told_width || !s->u.top.told_height)
                return 0;
        s->u.top.told_width = s->u.top.told_height = 0;
        return 1;
}

static void wayland_configure_owed(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};

        if (s->role != WAYLAND_ROLE_TOPLEVEL || !s->u.top.told || s->row < 0 || s->serials >= WAYLAND_SERIALS)
                return;
        if (wayland.host->row_facts(wayland.host->context, s->row, &facts) < 0)
                return;
        if ((facts.width != s->u.top.told_width || facts.height != s->u.top.told_height || facts.states != s->u.top.told_states) &&
            !wayland_facts_freed(s, &facts))
                wayland_configure(c, s, 0);
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
                if (s->width || (s->pending.attach_made && s->pending.attached))
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
                        c->objects[made].link = (int)wayland_surface_index(c, s);
                        c->objects[made].aux = s->gen;
                        s->role_id = c->objects[made].id;
                        wayland_popup_configure(c, s, p, parent);
                        return WAYLAND_OK;
                }
                s->role = WAYLAND_ROLE_TOPLEVEL;
                s->role_id = c->objects[made].id;
                s->title[0] = s->app_id[0] = 0;
                s->u.top.min_width = s->u.top.min_height = s->u.top.max_width = s->u.top.max_height = 0;
                c->objects[made].link = (int)wayland_surface_index(c, s);
                c->objects[made].aux = s->gen;
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
                                wayland_configure_owed(c, s);
                                return WAYLAND_OK;
                        }
                return wayland_fail(c, self->id, WLN_xdg_surface_error_invalid_serial, "the serial was not sent");
        }
}

/* Whether a toplevel is an ancestor of another, by its parent chain, which a loop of requests may not make. */
static int wayland_ancestor(struct wayland_client *c, long maybe, long of)
{
        for (long hops = 0; hops < c->tbl[WLT_SURFACE].count && of; hops++)
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
                wayland_unhold(c, s);
                wayland_row_release(c, s);
                s->role_id = 0;
                s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
                s->serials = 0;
                s->width = s->height = 0;
                s->u.top.want_maximized = s->u.top.want_fullscreen = 0; // a toplevel made after this one starts with no state asked
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
                if (row >= 0 && wayland.host->row_app)
                        wayland.host->row_app(wayland.host->context, (int)row, s->app_id);
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
                s->u.top.max_width = a[0].i;
                s->u.top.max_height = a[1].i;
                if (row >= 0)
                        wayland.host->row_do(wayland.host->context, (int)row, WAYLAND_DO_MAX_SIZE, s->u.top.max_width, s->u.top.max_height);
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_set_min_size:
                if (a[0].i < 0 || a[1].i < 0)
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_size, "a negative size");
                s->u.top.min_width = a[0].i;
                s->u.top.min_height = a[1].i;
                if (row >= 0)
                        wayland.host->row_do(wayland.host->context, (int)row, WAYLAND_DO_MIN_SIZE, s->u.top.min_width, s->u.top.min_height);
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

                // The client's last word on maximized and fullscreen is kept whatever window it was asked of: a window shown again (after a
                // null buffer took it away) is in the state last asked (wayland_show does it at the first show). Asked of a shown window,
                // it is done now.
                if (what == WAYLAND_DO_MAXIMIZE)
                        s->u.top.want_maximized = 1;
                else if (what == WAYLAND_DO_UNMAXIMIZE)
                        s->u.top.want_maximized = 0;
                else if (what == WAYLAND_DO_FULLSCREEN)
                        s->u.top.want_fullscreen = 1;
                else if (what == WAYLAND_DO_UNFULLSCREEN)
                        s->u.top.want_fullscreen = 0;
                // set_fullscreen's output, when it names one: the screen it is (its wl_output's), or -1 for the one the window is mostly on.
                if (what == WAYLAND_DO_FULLSCREEN)
                        s->u.top.want_screen = op == WLQ_xdg_toplevel_set_fullscreen && a[0].slot >= 0 && c->objects[a[0].slot].iface == WLI_wl_output &&
                                                         c->objects[a[0].slot].aux < WAYLAND_SCREENS
                                                 ? (short)c->objects[a[0].slot].aux
                                                 : -1;
                if (row >= 0)
                        wayland.host->row_do(wayland.host->context, (int)row, what, what == WAYLAND_DO_FULLSCREEN ? s->u.top.want_screen : 0, 0);
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
                for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
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
                s->u.popup.grab = 1;
                return WAYLAND_OK;
        default: // reposition: a new place for the popup, and the token it was asked with
        {
                struct wayland_positioner *p = &c->positioners[c->objects[a[0].slot].link];
                struct wayland_surface *parent = s->parent ? &c->surfaces[s->parent - 1] : 0;

                wayland_ev(c, self->id, WLE_xdg_popup_repositioned, (unsigned int)a[1].u);
                wayland_popup_configure(c, s, p, parent);
                if (s->row >= 0)
                        wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_PLACE, s->u.popup.x, s->u.popup.y);
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
                        wayland_ev0(c, c->objects[a[1].slot].id, WLE_wl_output_done);
        }
        (void)self;
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

        if (row < 0 || row >= wayland.row_made || wayland.row[row].client < 0)
                return 0;
        c = wayland.client[wayland.row[row].client];
        if (!c || !c->surfaces[wayland.row[row].surface].live)
                return 0;
        *client = c;
        return &c->surfaces[wayland.row[row].surface];
}

/* Whether a popup's window asked to grab the keys (a menu does; a tooltip does not). */
WAYLAND_API long wayland_row_grabs(long row)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);

        return s && s->role == WAYLAND_ROLE_POPUP && s->u.popup.grab;
}

/* A client's id list for one kind of seat object: pointer, keyboard or touch. */
static struct wayland_ids *wayland_seat_ids(struct wayland_client *c, long iface)
{
        return &c->seat_ids[iface == WLI_wl_pointer ? 0 : iface == WLI_wl_touch ? 2 : 1];
}

/* Zero-alloc iterator over live object slots of `iface` in `ids`. */
static long wayland_ids_next(struct wayland_client *c, const struct wayland_ids *ids, long iface, long *cursor)
{
        while (*cursor < ids->count)
        {
                long slot = wayland_slot_of(c, ids->id[(*cursor)++]);

                if (slot >= 0 && c->objects[slot].iface == iface)
                        return slot;
        }
        return -1;
}

static long wayland_seat_next(struct wayland_client *c, long iface, long *cursor)
{
        return wayland_ids_next(c, wayland_seat_ids(c, iface), iface, cursor);
}

static int wayland_seat_any(struct wayland_client *c, long iface)
{
        long at = 0;

        return wayland_seat_next(c, iface, &at) >= 0;
}

/* A seat object made: its id is kept; false when the memory for it is not there. */
static int wayland_seat_add(struct wayland_client *c, long iface, unsigned int id)
{
        return wayland_ids_add(wayland_seat_ids(c, iface), id);
}

static void wayland_seat_remove(struct wayland_client *c, long iface, unsigned int id)
{
        wayland_ids_remove(wayland_seat_ids(c, iface), id);
}

static void wayland_pointer_frame(struct wayland_client *c, long slot)
{
        if (c->objects[slot].version >= 5)
                wayland_emit(c, c->objects[slot].id, WLE_wl_pointer_frame, 0, 0);
}

static void wayland_pointer_frames(struct wayland_client *c)
{
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
                wayland_pointer_frame(c, slot);
}

static int wayland_surface_hits(const struct wayland_surface *s, int is_root, long px, long py)
{
        if (s->input_none || (!is_root && !s->held))
                return 0;
        if (s->width > 0 && s->height > 0 && (px < 0 || py < 0 || px >= s->width || py >= s->height))
                return 0;
        if (!s->input_bounded)
                return 1;
        {
                long scale = s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT;
                long lx = wayland_units(px, scale), ly = wayland_units(py, scale);

                for (long i = 0; i < s->input_count; i++)
                {
                        const struct wayland_rect *r = &s->input_rects[i];

                        if (lx >= r->x && lx < r->x + r->width && ly >= r->y && ly < r->y + r->height)
                                return 1;
                }
        }
        return 0;
}

static struct wayland_surface *wayland_pick_node(struct wayland_client *c, struct wayland_surface *node,
                                                 const struct wayland_surface *root, long ax, long ay, long root_scale,
                                                 long x, long y, long depth, long *sx, long *sy)
{
        long ox, oy;

        if (depth >= c->tbl[WLT_SURFACE].count)
                return 0;
        for (unsigned int ch = node->last_child[1]; ch; ch = c->surfaces[ch - 1].prev_sib)
        {
                struct wayland_surface *cs = &c->surfaces[ch - 1];
                struct wayland_surface *hit = wayland_pick_node(c, cs, root, ax + cs->u.sub.x, ay + cs->u.sub.y, root_scale, x, y, depth + 1, sx, sy);

                if (hit)
                        return hit;
        }
        ox = wayland_pixels(ax, root_scale);
        oy = wayland_pixels(ay, root_scale);
        if (wayland_surface_hits(node, node == root, x - ox, y - oy))
        {
                *sx = x - ox;
                *sy = y - oy;
                return node;
        }
        for (unsigned int ch = node->last_child[0]; ch; ch = c->surfaces[ch - 1].prev_sib)
        {
                struct wayland_surface *cs = &c->surfaces[ch - 1];
                struct wayland_surface *hit = wayland_pick_node(c, cs, root, ax + cs->u.sub.x, ay + cs->u.sub.y, root_scale, x, y, depth + 1, sx, sy);

                if (hit)
                        return hit;
        }
        return 0;
}

/*
        The surface of a window that holds a point of its picture, with the point in that surface's own pixels: the topmost
        subsurface that shows there and takes the pointer (an input region with nothing in it takes none), else the root. The
        later sibling is above the earlier and children placed above lie over what they hang from while children placed below
        lie behind it.
*/
static struct wayland_surface *wayland_pick(struct wayland_client *c, struct wayland_surface *root, long x, long y, long *sx, long *sy)
{
        struct wayland_surface *hit = 0;
        long wx, wy, ww, wh;

        wayland_window_rect(c, root, root->width, root->height, root->scale120, &wx, &wy, &ww, &wh);
        x += wx;
        y += wy;

        if (!wayland_has_children(root) && !root->input_bounded)
        {
                *sx = x;
                *sy = y;
                return root;
        }
        hit = wayland_pick_node(c, root, root, 0, 0, root->scale120 > 0 ? root->scale120 : WAYLAND_UNIT, x, y, 0, sx, sy);
        if (!hit)
        {
                *sx = x;
                *sy = y;
                return root;
        }
        return hit;
}

static void wayland_pointer_enter_one(struct wayland_client *c, long slot, struct wayland_surface *s, unsigned long serial, long sx, long sy)
{
        wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_enter, (unsigned int)serial, s->id,
                   (unsigned int)wayland_fixed(sx, s->scale120), (unsigned int)wayland_fixed(sy, s->scale120));
        wayland_pointer_frame(c, slot);
}

static void wayland_pointer_leave_all(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned long serial = wayland_serial(c);

        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
        {
                wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_leave, (unsigned int)serial, s->id);
                wayland_pointer_frame(c, slot);
        }
}

/*
        The pointer is over row (or none, -1), at (x, y) of its picture: the surface it leaves, a subsurface or a window's root,
        is sent its leave and the surface it enters its enter, each to every pointer its client has. The same surface is no event.
*/
WAYLAND_API int wayland_dragging(void);
WAYLAND_API void wayland_drag_motion(long row, long x, long y);
WAYLAND_API void wayland_drag_cancel(void);
static void wayland_drag_release(void);
static void wayland_drag_end(int how);

static void wayland_pointer_to_at(long row, long x, long y, struct wayland_surface **picked, long *sx_out, long *sy_out)
{
        struct wayland_client *old = 0, *now = 0;
        struct wayland_surface *root, *was = 0, *is = 0;
        long sx = 0, sy = 0;

        if (!wayland.host)
        {
                if (picked) *picked = 0;
                return;
        }
        // A drag holds the pointer: where it is goes to the drag, and no client is told of the pointer.
        if (wayland_dragging())
        {
                if (picked) *picked = 0;
                wayland_drag_motion(row, x, y);
                return;
        }
        root = wayland_row_surface(row, &now);
        if (root)
                is = wayland_pick(now, root, x, y, &sx, &sy);
        if (picked) *picked = is;
        if (sx_out) *sx_out = sx;
        if (sy_out) *sy_out = sy;
        wayland.pointer_x = x;
        wayland.pointer_y = y;
        if (is && row == wayland.pointer_row && wayland_surface_index(now, is) == wayland.pointer_surface)
                return;
        // The hand's counts kept for the surface the pointer leaves are its, sent before the leave.
        wayland_counts_send(1);
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
        // And so is the picture a client drew for it (wayland_cursor_picture): the client names one again for the surface it enters.
        if (wayland.cursor_client >= 0)
        {
                wayland.cursor_client = wayland.cursor_surface = -1;
                wayland.cursor_serial++;
        }
        wayland.pointer_row = is ? row : -1;
        wayland.pointer_surface = is ? wayland_surface_index(now, is) : -1;
        if (is)
        {
                unsigned long serial = wayland_input_serial(now);

                now->pointer_enter = (unsigned int)serial;
                for (long at = 0, slot; (slot = wayland_seat_next(now, WLI_wl_pointer, &at)) >= 0;)
                        wayland_pointer_enter_one(now, slot, is, serial, sx, sy);
        }
        wayland_constraints_update();
}

WAYLAND_API void wayland_pointer_to(long row, long x, long y)
{
        wayland_pointer_to_at(row, x, y, 0, 0, 0);
}

WAYLAND_API void wayland_pointer_motion(long row, long x, long y)
{
        struct wayland_client *c;
        struct wayland_surface *root, *s = 0;
        long time, sx = 0, sy = 0;

        if (wayland.host && wayland_dragging())
        {
                wayland_drag_motion(row, x, y);
                return;
        }
        if (!wayland.host || row != wayland.pointer_row || (wayland.hold_mode == 1 && row == wayland.hold_row) ||
            !(root = wayland_row_surface(row, &c)))
                return;
        // A motion that crosses from one surface of the window to another is a leave and an enter first.
        wayland_pointer_to_at(row, x, y, &s, &sx, &sy);
        if (wayland.pointer_row != row || wayland.pointer_surface < 0 || !s)
                return;
        time = wayland.host->now(wayland.host->context);
        {
                const unsigned int words[3] = {(unsigned int)time, (unsigned int)wayland_fixed(sx, s->scale120),
                                               (unsigned int)wayland_fixed(sy, s->scale120)};

                for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
                        wayland_emit(c, c->objects[slot].id, WLE_wl_pointer_motion, words, 3);
        }
        // The hand's counts since the last motion, in this frame (wayland_pointer_relative): the reports' own, not the places' difference,
        // which stops at the screen's edge and is a pixel the gain rounded.
        if (wayland.counts_row == row)
                wayland_counts_send(0);
        wayland_pointer_frames(c);
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

/*
        One wl_touch event to each touch seat of a client, with its words (count of them, as the wire has them), and a frame
        after each: the frame is what ends a touch event for the client.
*/
static void wayland_touch_send(struct wayland_client *c, long event, const unsigned long *words, long count)
{
        unsigned int w[6];

        for (long i = 0; i < count; i++)
                w[i] = (unsigned int)words[i];
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_touch, &at)) >= 0;)
        {
                wayland_send(c, c->objects[slot].id, event, w, count);
                wayland_ev0(c, c->objects[slot].id, WLE_wl_touch_frame);
        }
}

/*
        Whether the window in row has a client that holds a wl_touch. A finger on such a window is the touch's: the compositor's
        pointer events for it (the kit's gesture turns a finger into a press and a motion) are not sent to that client, so it is
        told each finger once, as a touch, and not twice.
*/
WAYLAND_API long wayland_row_takes_touch(long row)
{
        struct wayland_client *c = 0;

        if (row < 0 || row >= wayland.row_made || !wayland_row_surface(row, &c))
                return 0;
        return wayland_seat_any(c, WLI_wl_touch);
}

/* A finger went down on row at (x, y) of its picture; id is its tracking id, unique while it is down. */
WAYLAND_API void wayland_touch_down(long row, long id, long x, long y)
{
        struct wayland_client *c = 0;
        struct wayland_surface *root, *s;
        long sx = 0, sy = 0, time, point = -1;
        unsigned long serial, words[6];

        if (!wayland.host || id < 0 || wayland_touch_find(id) >= 0 || !(root = wayland_row_surface(row, &c)))
                return;
        for (int at = 0; at < WAYLAND_TOUCHES && point < 0; at++)
                if (!wayland.touch[at].live)
                        point = at;
        if (point < 0 || !wayland_seat_any(c, WLI_wl_touch) || !(s = wayland_pick(c, root, x, y, &sx, &sy)))
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        c->press_serial = (unsigned int)serial;
        wayland.touch[point].live = 1;
        wayland.touch[point].client = c->slot;
        wayland.touch[point].row = (int)row;
        wayland.touch[point].surface = (int)wayland_surface_index(c, s);
        wayland.touch[point].id = id;
        wayland.touch[point].x = x;
        wayland.touch[point].y = y;
        words[0] = serial, words[1] = (unsigned long)time, words[2] = s->id, words[3] = (unsigned long)id;
        words[4] = wayland_fixed(sx, s->scale120), words[5] = wayland_fixed(sy, s->scale120);
        wayland_touch_send(c, WLE_wl_touch_down, words, 6);
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
        long ox = 0, oy = 0, time;
        unsigned long words[4];
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
        words[0] = (unsigned long)time, words[1] = (unsigned long)id;
        words[2] = wayland_fixed(x - ox, s->scale120), words[3] = wayland_fixed(y - oy, s->scale120);
        wayland_touch_send(c, WLE_wl_touch_motion, words, 4);
}

/* A finger that was down is lifted. */
WAYLAND_API void wayland_touch_up(long id)
{
        struct wayland_client *c;
        long time;
        unsigned long serial, words[3];
        int at = wayland_touch_find(id);

        if (!wayland.host || at < 0)
                return;
        c = wayland.client[wayland.touch[at].client];
        wayland.touch[at].live = 0;
        if (!c)
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        words[0] = serial, words[1] = (unsigned long)time, words[2] = (unsigned long)id;
        wayland_touch_send(c, WLE_wl_touch_up, words, 3);
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

                if (!wayland.touch[at].live)
                        continue;
                c = wayland.client[wayland.touch[at].client];
                for (int each = 0; each < WAYLAND_TOUCHES; each++)
                        if (wayland.touch[each].live && wayland.touch[each].client == wayland.touch[at].client)
                                wayland.touch[each].live = 0;
                if (c)
                        wayland_touch_send(c, WLE_wl_touch_cancel, 0, 0);
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
        long time;

        if (!wayland.host || code < 0 || code > 0x2ff)
                return;
        wayland.buttons += pressed ? 1 : wayland.buttons > 0 ? -1 : 0;
        // A drag holds the pointer: a button let go is its drop (or its cancel), and no client is told of it.
        if (wayland_dragging())
        {
                if (!pressed)
                        wayland_drag_release();
                return;
        }
        if (row != wayland.pointer_row || !(s = wayland_row_surface(row, &c)))
                return;
        time = wayland.host->now(wayland.host->context);
        serial = wayland_input_serial(c);
        if (pressed)
                c->press_serial = (unsigned int)serial;
        {
                const unsigned int words[4] = {(unsigned int)serial, (unsigned int)time, (unsigned int)code,
                                               pressed ? WLN_wl_pointer_button_state_pressed : WLN_wl_pointer_button_state_released};

                for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
                {
                        wayland_emit(c, c->objects[slot].id, WLE_wl_pointer_button, words, 4);
                        wayland_pointer_frame(c, slot);
                }
        }
        (void)s;
}

/*
        The wheel or two fingers: v120 is in hundred-and-twentieths of a notch, and horizontal says which axis. A wheel (notch_px
        0) reports what version eight has for it (value120), what earlier ones had (discrete steps, the remainder kept for the
        next), a notch being WAYLAND_NOTCH of the surface's units. A finger's turn (notch_px, the window's UI_NOTCH) is a smooth
        value of the same size from a finger: a notch of it is notch_px pixels of the surface, so the client moves as the
        fingers do.
*/
WAYLAND_API void wayland_pointer_axis(long row, long horizontal, long v120, long notch_px)
{
        struct wayland_client *c;
        long time, axis = horizontal ? WLN_wl_pointer_axis_horizontal_scroll : WLN_wl_pointer_axis_vertical_scroll;
        int precise = notch_px > 0;

        if (!wayland.host || row != wayland.pointer_row || !wayland_row_surface(row, &c) || !v120)
                return;
        if (precise)
                c->finger_scroll = 1;
        time = wayland.host->now(wayland.host->context);
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
        {
                struct wayland_object *o = &c->objects[slot];

                if (o->version >= 5)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_source,
                                   precise ? WLN_wl_pointer_axis_source_finger : WLN_wl_pointer_axis_source_wheel);
                if (o->version >= 9)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_relative_direction,
                                   (unsigned int)axis, WLN_wl_pointer_axis_relative_direction_identical);
                if (!precise && o->version >= 8)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_value120, (unsigned int)axis, (unsigned int)v120);
                else if (!precise && o->version >= 5)
                {
                        long notches;

                        c->axis_left[horizontal ? 1 : 0] += v120;
                        notches = c->axis_left[horizontal ? 1 : 0] / 120;
                        c->axis_left[horizontal ? 1 : 0] -= notches * 120;
                        if (notches)
                                wayland_ev(c, o->id, WLE_wl_pointer_axis_discrete, (unsigned int)axis, (unsigned int)notches);
                }
                wayland_ev(c, o->id, WLE_wl_pointer_axis, (unsigned int)time, (unsigned int)axis,
                           (unsigned int)(v120 * (precise ? notch_px : WAYLAND_NOTCH) * 256 / 120));
                wayland_pointer_frame(c, slot);
        }
}

/*
        The fingers lifted off a pad that scrolled (the pad's last finger, WINDOW_WHEEL_STOP): each pointer of the client is sent
        axis_stop on both axes (from version 5), which is what a toolkit ends kinetic scrolling on. Only a client that had a finger
        scroll and no stop since is sent one; a wheel's scroll has none.
*/
WAYLAND_API void wayland_pointer_axis_stop(long row)
{
        struct wayland_client *c;
        long time;

        if (!wayland.host || row != wayland.pointer_row || !wayland_row_surface(row, &c) || !c->finger_scroll)
                return;
        c->finger_scroll = 0;
        time = wayland.host->now(wayland.host->context);
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
                for (long axis = 0; axis < 2 && c->objects[slot].version >= 5; axis++)
                {
                        wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_axis_stop, (unsigned int)time,
                                   axis ? WLN_wl_pointer_axis_horizontal_scroll : WLN_wl_pointer_axis_vertical_scroll);
                        wayland_pointer_frame(c, slot);
                }
}

/*
        zwp_text_input_v3: a text field of a client's window, which the desktop's on-screen keyboard shows by. Its enable, content type
        and cursor rectangle are applied at the commit, as the protocol has them, and done carries the commit's serial. While it is
        enabled and its window has the keyboard, the window's row holds one field count in the window table (WAYLAND_DO_TEXT, the same
        count ui_field_focus keeps for a kit field), so the keyboard shows; the count goes when either stops being so, when the window
        goes, and when the object is destroyed or its client dies. The cursor rectangle's bottom is passed with it (WAYLAND_DO_CARET),
        so the window is moved up for the cursor and not for its own bottom edge. Nothing else of the protocol is done: no preedit, no
        surrounding text, no change cause; a commit_string is never sent.
*/
static void wayland_text_sync(struct wayland_client *c, struct wayland_text *t)
{
        int want = t->enabled && t->row >= 0 ? t->row : -1;
        struct wayland_surface *s;
        struct wayland_client *owner = 0;
        long caret = -1;

        if (t->counted >= 0 && t->counted != want)
        {
                wayland.host->row_do(wayland.host->context, t->counted, WAYLAND_DO_TEXT, -1, 0);
                t->counted = -1;
        }
        if (want >= 0 && t->counted < 0)
        {
                wayland.host->row_do(wayland.host->context, want, WAYLAND_DO_TEXT, 1, (long)t->purpose);
                t->counted = want;
        }
        if (t->counted < 0)
                return;
        s = wayland_row_surface(t->counted, &owner);
        if (s && owner == c && t->caret >= 0)
                caret = wayland_pixels(t->caret, s->scale120);
        wayland.host->row_do(wayland.host->context, t->counted, WAYLAND_DO_TEXT, 0, (long)t->purpose);
        wayland.host->row_do(wayland.host->context, t->counted, WAYLAND_DO_CARET, caret, 0);
}

// The window of a row goes: its text inputs lose the keys with it, and give back their counts (called before the row is closed).
static void wayland_texts_row_gone(struct wayland_client *c, long row)
{
        for (long at = 0; at < c->tbl[WLT_TEXT].count; at++)
                if (c->texts[at].live && c->texts[at].row == row)
                {
                        c->texts[at].row = -1;
                        wayland_text_sync(c, &c->texts[at]);
                }
}

// The keyboard went to a window of the client (its row and surface id) or left it: each text input is told, and holds or gives back its
// count with it. A leave is only for the window the keys were in.
static void wayland_texts_focus(struct wayland_client *c, long row, unsigned int surface, int entered)
{
        for (long at = 0; at < c->tbl[WLT_TEXT].count; at++)
        {
                struct wayland_text *t = &c->texts[at];

                if (!t->live || (!entered && t->row != row))
                        continue;
                t->row = entered ? (int)row : -1;
                wayland_ev(c, t->id, entered ? WLE_zwp_text_input_v3_enter : WLE_zwp_text_input_v3_leave, surface);
                wayland_text_sync(c, t);
        }
}

// The client's text input objects are given up: each one's count goes back (the client died, or the window's keys went with it).
static void wayland_texts_drop(struct wayland_client *c)
{
        for (long at = 0; at < c->tbl[WLT_TEXT].count; at++)
                if (c->texts[at].live)
                {
                        c->texts[at].enabled = 0;
                        c->texts[at].row = -1;
                        wayland_text_sync(c, &c->texts[at]);
                        c->texts[at].live = 0;
                }
}

static int wayland_text_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_text *t;

        if (self->iface == WLI_zwp_text_input_manager_v3)
        {
                struct wayland_surface *focus;
                struct wayland_client *owner = 0;
                long index;

                if (op != WLQ_zwp_text_input_manager_v3_get_text_input)
                        return WAYLAND_OK;
                index = wayland_tbl_alloc(c, WLT_TEXT);
                if (index < 0)
                        return wayland_new_fail(c, self->id, a[0].u);
                t = &c->texts[index];
                t->live = 1;
                t->id = (unsigned int)a[0].u;
                t->caret = t->pending_caret = -1;
                t->row = -1;
                t->counted = -1;
                c->objects[a[0].slot].link = (int)index;
                // A client that gets its text input once the keys are in its window is told so at once.
                focus = wayland_row_surface(wayland.keyboard_row, &owner);
                if (focus && owner == c)
                        wayland_texts_focus(c, wayland.keyboard_row, focus->id, 1);
                return WAYLAND_OK;
        }
        if (self->link < 0 || self->link >= c->tbl[WLT_TEXT].count || !c->texts[self->link].live)
                return WAYLAND_OK;
        t = &c->texts[self->link];
        switch (op)
        {
        case WLQ_zwp_text_input_v3_destroy:
                t->enabled = 0;
                t->row = -1;
                wayland_text_sync(c, t);
                t->live = 0;
                wayland_tbl_free(c, WLT_TEXT, self->link);
                self->link = -1;
                break;
        case WLQ_zwp_text_input_v3_enable:
                t->pending_enabled = 1;
                break;
        case WLQ_zwp_text_input_v3_disable:
                t->pending_enabled = 0;
                break;
        case WLQ_zwp_text_input_v3_set_content_type:
                // A purpose the protocol does not have is a normal one, so no field is ever given a purpose it cannot show.
                t->pending_purpose = a[1].u <= WLN_zwp_text_input_v3_content_purpose_terminal ? (unsigned char)a[1].u
                                                                                             : (unsigned char)WLN_zwp_text_input_v3_content_purpose_normal;
                break;
        case WLQ_zwp_text_input_v3_set_cursor_rectangle:
                // The bottom edge is y + height, in surface units; a rectangle with a negative size, or one above the surface, is none.
                t->pending_caret = a[2].i < 0 || a[3].i < 0 || (long)a[1].i + (long)a[3].i < 0 ? -1 : (long)a[1].i + (long)a[3].i;
                break;
        case WLQ_zwp_text_input_v3_commit:
                t->enabled = t->pending_enabled;
                t->purpose = t->pending_purpose;
                t->caret = t->pending_caret;
                wayland_text_sync(c, t);
                t->serial++;
                wayland_ev(c, t->id, WLE_zwp_text_input_v3_done, (unsigned int)t->serial);
                break;
        default: // set_surrounding_text and set_text_change_cause are taken and not used
                break;
        }
        return WAYLAND_OK;
}

/* How fast a held key repeats, to one keyboard (from version 4): the rate a second and the delay in milliseconds. */
static void wayland_keyboard_repeat(struct wayland_client *c, long slot)
{
        long rate = 25, delay = 600;

        if (c->objects[slot].version < 4)
                return;
        wayland.host->repeat(wayland.host->context, &rate, &delay);
        wayland_ev(c, c->objects[slot].id, WLE_wl_keyboard_repeat_info, (unsigned int)rate, (unsigned int)delay);
}

/* The keymap to one keyboard: a descriptor beside the event, and how fast a held key repeats. */
static void wayland_keyboard_keymap(struct wayland_client *c, long slot)
{
        struct wayland_event e;
        long size = 0, fd = wayland.host->keymap(wayland.host->context, &size);

        e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_keymap);
        if (fd >= 0 && size > 0 && size <= WAYLAND_KEYMAP_MAX && wayland_out_fd_room(c))
        {
                wayland_word(&e, WLN_wl_keyboard_keymap_format_xkb_v1);
                wayland_word(&e, (unsigned long)size);
                wayland_out_fd_put(c, fd, 0, e.start);
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
        wayland_ev(c, c->objects[slot].id, WLE_wl_keyboard_modifiers, (unsigned int)wayland_serial(c),
                   (unsigned int)wayland.mods[0], (unsigned int)wayland.mods[1], (unsigned int)wayland.mods[2], (unsigned int)wayland.mods[3]);
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

                for (long at = 0, slot; (slot = wayland_seat_next(old, WLI_wl_keyboard, &at)) >= 0;)
                        wayland_ev(old, old->objects[slot].id, WLE_wl_keyboard_leave, (unsigned int)serial, was->id);
                wayland_texts_focus(old, wayland.keyboard_row, was->id, 0);
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

                // A window is entered with no key held and the locks the compositor says: whatever was held went with the window
                // that had the keys, whose release the compositor sent there.
                memory_zero(wayland.keys, sizeof wayland.keys);
                wayland.mods[0] = wayland.mods[1] = wayland.mods[3] = 0;
                wayland.mods[2] = (unsigned long)wayland.host->locks(wayland.host->context);
                for (long at = 0, slot; (slot = wayland_seat_next(now, WLI_wl_keyboard, &at)) >= 0;)
                        wayland_keyboard_enter_one(now, slot, is, serial);
                wayland_selections_to(now);
                wayland_texts_focus(now, row, is->id, 1);
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
        long time;

        if (!wayland.host || code < 1 || code > 0x2ff)
                return;
        // Escape during a drag: the drag cancelled, and the key the drag's.
        if (code == 1 && wayland_dragging())
        {
                if (pressed)
                        wayland_drag_cancel();
                return;
        }
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
        {
                const unsigned int words[4] = {(unsigned int)serial, (unsigned int)time, (unsigned int)code,
                                               pressed ? WLN_wl_keyboard_key_state_pressed : WLN_wl_keyboard_key_state_released};

                for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_keyboard, &at)) >= 0;)
                        wayland_emit(c, c->objects[slot].id, WLE_wl_keyboard_key, words, 4);
        }
        // The modifiers after the key that changed them, to each keyboard, when they did.
        {
                unsigned long depressed = wayland_depressed(), locked = (unsigned long)wayland.host->locks(wayland.host->context);

                if (depressed != wayland.mods[0] || locked != wayland.mods[2])
                {
                        wayland.mods[0] = depressed;
                        wayland.mods[2] = locked;
                        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_keyboard, &at)) >= 0;)
                                wayland_modifiers_to(c, slot);
                }
        }
        (void)s;
}

/*
        A key the desktop's keyboard sends for a window: the same as wayland_key, with Shift held for it when the key makes a capital or
        a shifted symbol (shift). The client's keymap makes the character from the code and the modifiers, so the shim holds Shift for
        the press and lets it go with the key's release, as a person's Shift would be (wayland.synth_code remembers the key).
*/
WAYLAND_API void wayland_key_shift(long row, long code, long pressed, long shift)
{
        int inject = pressed && shift && code != 42 && code != 54 && code >= 1 && code < 128 && !wayland.synth_code &&
                     !((wayland.keys[42 >> 3] >> (42 & 7)) & 1) && !((wayland.keys[54 >> 3] >> (54 & 7)) & 1);

        if (!wayland.host)
                return;
        if (inject)
        {
                wayland_key(row, 42, 1);
                wayland.synth_code = (unsigned long)code;
        }
        wayland_key(row, code, pressed);
        if (!pressed && wayland.synth_code == (unsigned long)code)
        {
                wayland_key(row, 42, 0);
                wayland.synth_code = 0;
        }
}

/* The repeat rate or delay changed (the settings): each keyboard is told, and nothing else of it. */
WAYLAND_API void wayland_repeat_changed(void)
{
        if (!wayland.host)
                return;
        for (long client = 0; client < wayland.client_slots; client++)
        {
                struct wayland_client *c = wayland.client[client];

                if (!c)
                        continue;
                for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_keyboard, &at)) >= 0;)
                        wayland_keyboard_repeat(c, slot);
        }
}

/* The keymap changed (a layout was set): each keyboard is sent the new one, and the modifiers. */
WAYLAND_API void wayland_keymap_changed(void)
{
        if (!wayland.host)
                return;
        for (long client = 0; client < wayland.client_slots; client++)
        {
                struct wayland_client *c = wayland.client[client];

                if (!c)
                        continue;
                for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_keyboard, &at)) >= 0;)
                {
                        wayland_keyboard_keymap(c, slot);
                        wayland_modifiers_to(c, slot);
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
                if (wayland.pointer_row >= 0 && wayland.row[wayland.pointer_row].client == c->slot && wayland.pointer_surface >= 0)
                {
                        struct wayland_client *owner;
                        struct wayland_surface *root = wayland_row_surface(wayland.pointer_row, &owner), *s = 0;
                        long sx = 0, sy = 0;

                        if (root && c->surfaces[wayland.pointer_surface].live)
                        {
                                unsigned long serial = wayland_input_serial(c);

                                c->pointer_enter = (unsigned int)serial;
                                s = wayland_pick(c, root, wayland.pointer_x, wayland.pointer_y, &sx, &sy);
                                wayland_pointer_enter_one(c, slot, s, serial, sx, sy);
                        }
                }
                return WAYLAND_OK;
        case WLQ_wl_seat_get_keyboard:
                if (!wayland_seat_add(c, WLI_wl_keyboard, c->objects[slot].id))
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many keyboards");
                wayland_keyboard_keymap(c, slot);
                if (wayland.keyboard_row >= 0 && wayland.row[wayland.keyboard_row].client == c->slot)
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
        // wl_pointer.set_cursor (serial, surface, hotspot): the surface takes the cursor role (one with another role is the role error,
        // whatever the serial), the serial is the enter's (wayland_pointer_serial_ok), and only the client with the pointer may set its
        // cursor. No surface hides the pointer over its window. A surface is the client's picture of the pointer, which the compositor
        // reads with wayland_cursor_picture; it is told the arrow (WAYLAND_DO_CURSOR) for as long as it draws no such picture.
        if (op == WLQ_wl_pointer_set_cursor && self->iface == WLI_wl_pointer && a[1].slot >= 0)
        {
                struct wayland_surface *named = wayland_surface_of(c, a[1].slot);

                if (!named || (named->role != WAYLAND_ROLE_NONE && named->role != WAYLAND_ROLE_CURSOR) || (named->flags & WAYLAND_SURFACE_XDG) ||
                    named->xsurface)
                        return wayland_fail(c, self->id, WLN_wl_pointer_error_role, "the cursor's surface has another role");
                named->role = WAYLAND_ROLE_CURSOR;
        }
        if (op == WLQ_wl_pointer_set_cursor && self->iface == WLI_wl_pointer && wayland.pointer_row >= 0 &&
            wayland.row[wayland.pointer_row].client == c->slot && wayland_pointer_serial_ok(c, a[0].u))
        {
                struct wayland_surface *named = a[1].slot >= 0 ? wayland_surface_of(c, a[1].slot) : 0;

                wayland.host->row_do(wayland.host->context, wayland.pointer_row, WAYLAND_DO_CURSOR,
                                     a[1].slot < 0 ? WAYLAND_CURSOR_HIDDEN : WAYLAND_CURSOR_ARROW, 0);
                wayland.cursor_client = named ? c->slot : -1;
                wayland.cursor_surface = named ? wayland_surface_index(c, named) : -1;
                wayland.cursor_gen = named ? named->gen : 0;
                wayland.cursor_hot_x = wayland_max(-WAYLAND_DIMENSION, wayland_min((long)a[2].i, WAYLAND_DIMENSION));
                wayland.cursor_hot_y = wayland_max(-WAYLAND_DIMENSION, wayland_min((long)a[3].i, WAYLAND_DIMENSION));
                wayland.cursor_serial++;
        }
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
        A client's copy and paste are the compositor's: it holds one copy for each selection (the clipboard and the primary
        selection), of every type its maker offered, whoever made it (a Wayland client, an X client, a program of the compositor's),
        and what a client is shown of it is the types the host names (clip_type). A client's copy (set_selection with a source) is
        read once, into the compositor's: the host asks for each type the source offers in turn (wayland_clip_type, then
        wayland_clip_send with a descriptor of its own for the source's send event) and reads what comes, and the copy is the
        selection when the source has given every one, so it outlives its source and its client. A paste is an offer of the copy's
        types made to the client that has the keys, when it gets them and when the copy changes, and a receive on an offer that is
        still the selection's is the host writing that type to the descriptor the client gave. A copy is taken only for a client the
        person has just touched, and a paste is given only to the window that has the keys with the person's key or button just on
        it (the host's rule, as for a program of the compositor's).
*/

/* The next live device slot of a client for a selection, or -1. */
static long wayland_device_next(struct wayland_client *c, long which, long *cursor)
{
        return wayland_ids_next(c, &c->device_ids[which], which ? WLI_zwp_primary_selection_device_v1 : WLI_wl_data_device, cursor);
}

static void wayland_device_remove(struct wayland_client *c, long which, unsigned int id)
{
        wayland_ids_remove(&c->device_ids[which], id);
}

// An id of the server's own that no object of the client has now, taken in turn so a number just let go is not the next.
static unsigned long wayland_server_id(struct wayland_client *c)
{
        // Half full or more, the index doubles (64 first), so the walk from the hint meets a free id in a step or two.
        if (2 * (c->server_live + 1) > c->tbl[WLT_SERVER].count)
        {
                long want = c->tbl[WLT_SERVER].count ? 2 * c->tbl[WLT_SERVER].count : 64;

                if ((unsigned long)want > WAYLAND_SERVER_IDS || !wayland_tbl_reach(c, WLT_SERVER, want))
                        want = 0;
                if (!want && c->server_live >= c->tbl[WLT_SERVER].count)
                        return 0;
        }
        for (long step = 0; step < c->tbl[WLT_SERVER].count; step++)
        {
                long at = (c->server_hint + step) % c->tbl[WLT_SERVER].count;

                if (!c->server_index[at])
                {
                        c->server_hint = (at + 1) % c->tbl[WLT_SERVER].count;
                        return WAYLAND_SERVER_BASE + (unsigned long)at;
                }
        }
        return 0;
}

/* Packed NUL-terminated MIME buffer helpers shared by clipboard/primary sources and XDND/host drags. */
static int wayland_mime_buf_append(struct wayland_table *tbl, long *bytes, const unsigned char *text, long len)
{
        if (!text || len < 1)
                return 0;
        tbl->unit = 1;
        if (!wayland_table_reach(tbl, *bytes + len + 1))
                return 0;
        for (long at = 0; at < len; at++)
                tbl->at[*bytes + at] = text[at];
        tbl->at[*bytes + len] = 0;
        *bytes += len + 1;
        return 1;
}

static const char *wayland_mime_buf_nth(const unsigned char *buf, long bytes, long nth)
{
        for (long at = 0; buf && at < bytes;)
        {
                const char *type = (const char *)buf + at;
                long length = 0;

                while (at + length < bytes && type[length])
                        length++;
                if (!nth--)
                        return type;
                at += length + 1;
        }
        return 0;
}

/* Unified offer creation + data_offer/offer(/source_actions) emission for selection (is_drag=0) and DND (is_drag=1). */
static unsigned long wayland_offer_create_and_send(struct wayland_client *c, long device_slot, long which, int is_drag,
                                                   const unsigned char *types, long type_bytes, unsigned int src_actions, long *out_index)
{
        long index = wayland_tbl_alloc(c, WLT_OFFER), offer = -1;
        unsigned long id = index >= 0 ? wayland_server_id(c) : 0;

        if (out_index)
                *out_index = -1;
        if (id)
                offer = wayland_object_new(c, id, which ? WLI_zwp_primary_selection_offer_v1 : WLI_wl_data_offer,
                                           c->objects[device_slot].version);
        if (offer < 0)
        {
                if (index >= 0)
                        wayland_tbl_free(c, WLT_OFFER, index);
                return 0;
        }
        c->offers[index].id = (unsigned int)id;
        c->offers[index].live = 1;
        c->offers[index].which = (unsigned char)which;
        if (is_drag)
                c->offers[index].drag = 1;
        else
                c->offers[index].serial = wayland.selection_serial[which];
        c->objects[offer].link = (int)index;
        if (out_index)
                *out_index = index;
        wayland_ev(c, c->objects[device_slot].id,
                   which ? WLE_zwp_primary_selection_device_v1_data_offer : WLE_wl_data_device_data_offer, (unsigned int)id);
        for (long nth = 0;; nth++)
        {
                const char *type = is_drag ? wayland_mime_buf_nth(types, type_bytes, nth)
                                           : wayland.host->clip_type(wayland.host->context, which, nth);
                if (!type)
                        break;
                wayland_ev_str(c, id, which ? WLE_zwp_primary_selection_offer_v1_offer : WLE_wl_data_offer_offer, type);
        }
        if (is_drag && c->objects[offer].version >= 3)
                wayland_ev(c, id, WLE_wl_data_offer_source_actions, src_actions);
        return id;
}

/* Unified data_source/primary_selection_source send+fd emitter (from_host=1 increments wayland.fds). */
static int wayland_source_send_fd(struct wayland_client *c, const struct wayland_source *source, const char *mime, long fd, int from_host)
{
        if (!c || !source || !mime || fd < 0 || wayland_slot_of(c, source->id) < 0 || !wayland_out_fd_room(c))
                return 0;
        wayland_ev_fd(c, source->id, source->which ? WLE_zwp_primary_selection_source_v1_send : WLE_wl_data_source_send, mime, fd, 1);
        if (from_host)
                wayland.fds++;
        return 1;
}

/*
        The copy the compositor holds for a selection, to one of a client's devices: a new offer of it with its types, then the
        selection naming the offer; or the selection with none when there is nothing, or no room for an offer.
*/
static void wayland_selection_to(struct wayland_client *c, long slot, long which)
{
        unsigned long id = 0;

        if (wayland.host->clip_type(wayland.host->context, which, 0))
                id = wayland_offer_create_and_send(c, slot, which, 0, 0, 0, 0, 0);
        wayland_ev(c, c->objects[slot].id, which ? WLE_zwp_primary_selection_device_v1_selection : WLE_wl_data_device_selection, (unsigned int)id);
}

// Both selections to a client's devices: what it is shown when it gets the keys.
static void wayland_selections_to(struct wayland_client *c)
{
        for (long which = 0; which < 2; which++)
                for (long at = 0, slot; (slot = wayland_device_next(c, which, &at)) >= 0;)
                        wayland_selection_to(c, slot, which);
}

// A source told it is not wanted: its copy is not taken, or the copy is another's now. It is let go but for its object.
static void wayland_source_cancel(struct wayland_client *c, long index)
{
        struct wayland_source *source = &c->sources[index];

        source->state = 3;
        if (wayland_slot_of(c, source->id) < 0)
                return;
        wayland_ev0(c, source->id, source->which ? WLE_zwp_primary_selection_source_v1_cancelled : WLE_wl_data_source_cancelled);
}

static void wayland_drag_end(int how);

// A source object destroyed: a copy of it still being read is ended, and the selection stays what it was but is nobody's.
static void wayland_source_drop(struct wayland_client *c, long index)
{
        struct wayland_source *source = &c->sources[index];
        long which = source->which;

        if (!source->live)
                return;
        if (source->state == 1 && c->pending[which] == index + 1)
        {
                c->pending[which] = 0;
                wayland.host->clip_abort(wayland.host->context, c->slot, which);
        }
        if (wayland.selection_owner[which] == c->slot + 1 && wayland.selection_source[which] == index)
                wayland.selection_owner[which] = 0;
        // The source of the drag gone: the drag is cancelled, its target told it left.
        if (wayland.drag.live && wayland.drag.client == c->slot && wayland.drag.source == index)
                wayland_drag_end(0);
        wayland_table_drop(&source->mime_table);
        source->mime_bytes = 0;
        source->live = 0;
        wayland_tbl_free(c, WLT_SOURCE, index);
}

/*
        The compositor's copy for a selection is new (or gone): the source of a client that was the selection is told it is not,
        the count that tells an old offer from a current one moves, and the client that has the keys is offered the new copy.
*/
WAYLAND_API void wayland_selection_changed(long which)
{
        struct wayland_client *c;

        if (!wayland.host || which < 0 || which > 1)
                return;
        wayland.selection_serial[which]++;
        if (wayland.selection_owner[which] > 0 && (c = wayland.client[wayland.selection_owner[which] - 1]) &&
            c->sources[wayland.selection_source[which]].live && c->sources[wayland.selection_source[which]].state == 2)
                wayland_source_cancel(c, wayland.selection_source[which]);
        wayland.selection_owner[which] = 0;
        if (wayland.keyboard_row < 0 || wayland.row[wayland.keyboard_row].client < 0)
                return;
        c = wayland.client[wayland.row[wayland.keyboard_row].client];
        if (!c)
                return;
        for (long at = 0, slot; (slot = wayland_device_next(c, which, &at)) >= 0;)
                wayland_selection_to(c, slot, which);
}

// The source being read for a selection of a client, or 0.
static struct wayland_source *wayland_clip_pending(long client, long which, struct wayland_client **owner)
{
        struct wayland_client *c = client >= 0 && client < wayland.client_slots && wayland.host ? wayland.client[client] : 0;
        long index = c && which >= 0 && which < 2 ? c->pending[which] - 1 : -1;

        if (index < 0 || index >= c->tbl[WLT_SOURCE].count || !c->sources[index].live || c->sources[index].state != 1)
                return 0;
        *owner = c;
        return &c->sources[index];
}

/*
        The host's reading of a client's copy: wayland_clip_type is the nth type the source being read offered (0 past the last, or
        when no source is being read), each NUL ended; wayland_clip_send asks the source for it into the descriptor fd, which is the
        shim's from then on (1 when the send is queued; 0, and fd closed, when the source is gone or the client's queue is full).
*/
WAYLAND_API const char *wayland_clip_type(long client, long which, long nth)
{
        struct wayland_client *c = 0;
        struct wayland_source *source = wayland_clip_pending(client, which, &c);

        return source ? wayland_mime_buf_nth(source->mime_table.at, source->mime_bytes, nth) : 0;
}

WAYLAND_API int wayland_clip_send(long client, long which, long nth, long fd)
{
        struct wayland_client *c = 0;
        struct wayland_source *source = wayland_clip_pending(client, which, &c);
        const char *type = source ? wayland_mime_buf_nth(source->mime_table.at, source->mime_bytes, nth) : 0;

        if (!wayland_source_send_fd(c, source, type, fd, 1))
        {
                if (fd >= 0 && wayland.host->close)
                        wayland.host->close(wayland.host->context, fd);
                return 0;
        }
        return 1;
}

/*
        The host has read a client's source to its end (ok), or could not: the source is the selection, or is cancelled. The
        host says it after it has made the copy the selection, so the copy's change has been told by then.
*/
WAYLAND_API void wayland_clip_pulled(long client, long which, long ok)
{
        struct wayland_client *c = 0;
        struct wayland_source *source = wayland_clip_pending(client, which, &c);
        long index;

        if (!source)
                return;
        index = c->pending[which] - 1;
        c->pending[which] = 0;
        if (!ok)
        {
                wayland_source_cancel(c, index);
                return;
        }
        source->state = 2;
        wayland.selection_owner[which] = c->slot + 1;
        wayland.selection_source[which] = index;
}

// A source made: a record, and the object tied to it.
static int wayland_source_new(struct wayland_client *c, struct wayland_object *self, long which, long slot)
{
        long index = wayland_tbl_alloc(c, WLT_SOURCE);

        if (index < 0)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a data source");
        c->sources[index].id = c->objects[slot].id;
        c->sources[index].live = 1;
        c->sources[index].which = (unsigned char)which;
        c->sources[index].state = 0;
        c->objects[slot].link = (int)index;
        return WAYLAND_OK;
}

// A device made: kept in the client's list, and shown the selection at once when the client has the keys already.
static int wayland_device_new(struct wayland_client *c, struct wayland_object *self, long which, long slot)
{
        if (!wayland_ids_add(&c->device_ids[which], c->objects[slot].id))
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a data device");
        if (wayland.keyboard_row >= 0 && wayland.row[wayland.keyboard_row].client == c->slot)
                wayland_selection_to(c, slot, which);
        return WAYLAND_OK;
}

/*
        set_selection: a source (or none) and the serial of an input event of this client's. None clears the copy when this
        client's source is the selection. A source is used once; one made from an event the client was not sent, one that offers
        no type, one the host will not take (no gesture of the person's, the service off) is cancelled and nothing else happens.
        Otherwise the host is given the source to read (clip_pull, which asks for its types with wayland_clip_send), and it is the
        selection when it has been.
*/
static int wayland_set_selection(struct wayland_client *c, struct wayland_object *self, long which, const struct wayland_arg *a)
{
        long index = a[0].slot >= 0 ? c->objects[a[0].slot].link : -1;
        struct wayland_source *source;

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
        if (!wayland_input_serial_ok(c, a[1].u) || !source->mime_bytes)
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
        source->state = 1;
        c->pending[which] = index + 1;
        if (!wayland.host->clip_pull(wayland.host->context, c->slot, which) && c->pending[which] == index + 1)
        {
                c->pending[which] = 0;
                wayland_source_cancel(c, index);
        }
        return WAYLAND_OK;
}

// receive: a type of an offer that is still the selection's, written to the descriptor by the host, for the client that has the keys.
static int wayland_receive(struct wayland_client *c, struct wayland_object *self, struct wayland_arg *a)
{
        struct wayland_offer *offer = &c->offers[self->link];

        int current = offer->serial == wayland.selection_serial[offer->which] && wayland.keyboard_row >= 0 &&
                      wayland.row[wayland.keyboard_row].client == c->slot;

        if (current && a[0].text && a[0].length > 0)
        {
                wayland.host->clip_push(wayland.host->context, wayland.keyboard_row, offer->which, (const char *)a[0].text, a[1].fd);
                wayland.fds--;
                a[1].fd = -1;
        }
        return WAYLAND_OK;
}


/*
        Drag and drop (wl_data_device.start_drag). A client starts a drag with the serial of the press its button holds; the pointer is
        then the drag's until the button lets go: what is under it is entered (wl_data_device.enter with an offer of its own carrying
        the source's types and actions), moved over (motion) and left (leave), and no pointer event goes to any client. The target
        accepts a type (wl_data_offer.accept, told to the source as target) and says what it takes (set_actions); the action is
        what both take, the target's preferred one first, told to both. Let go over a target that took a type (and an action, at
        version 3): the target is told drop and the source dnd_drop_performed, and the target's receive hands its descriptor to the
        source's send as it is (no byte passes through the compositor), and its finish is the source's dnd_finished. Let go
        anywhere else, Escape, the source or either client gone: the source is told cancelled and the target left. A drag with no
        source is the client's own: only its own surfaces are entered, and nothing is carried. XDND between X windows is the X server's
        own (the stress bench's x-dnd). Xwayland has no data device (it binds no wl_data_device_manager), so between an X window and a
        Wayland one the window manager of Xwayland is the bridge (xwm.c), as Mutter's and wlroots' are: a Wayland drag over an X window
        is offered to it (host->xdrag), which is the XDND source to that window and says what it takes (wayland_drag_x_status) and when
        it is done (wayland_drag_x_finished), the X window's conversion the source's send (wayland_drag_x_send); and an X drag over a
        Wayland window is a drag of the X side's (wayland_drag_x_begin, xsource): its types and actions offered to the Wayland target,
        its motion the X source's positions, its drop the X source's, and the target's receive handed to the X side (host->xreceive).
        The pointer stays the X client's then (its button and motion are Xwayland's: the X source drives the drag), so Escape is the
        X source's to take.
*/
#define WAYLAND_DRAG_ACTIONS 7u
#define WAYLAND_DRAG_ANSWER_MS 1000
static void wayland_drag_decide(void);

// A type a source offered, kept for a drag's offer (its null with it). A type the memory has no room for is not offered.
static void wayland_source_type(struct wayland_client *c, long index, const unsigned char *text, long length)
{
        struct wayland_source *source = &c->sources[index];

        wayland_mime_buf_append(&source->mime_table, &source->mime_bytes, text, length);
}

static struct wayland_client *wayland_drag_client(void)
{
        return wayland.drag.live && wayland.drag.client >= 0 && wayland.drag.client < wayland.client_slots ? wayland.client[wayland.drag.client] : 0;
}

static struct wayland_source *wayland_drag_source(void)
{
        struct wayland_client *c = wayland_drag_client();

        return c && wayland.drag.source >= 0 && wayland.drag.source < c->tbl[WLT_SOURCE].count && c->sources[wayland.drag.source].live
                   ? &c->sources[wayland.drag.source]
                   : 0;
}

static struct wayland_client *wayland_drag_target(void)
{
        return wayland.drag.live && wayland.drag.target >= 0 && wayland.drag.target < wayland.client_slots ? wayland.client[wayland.drag.target] : 0;
}

// The target's first data device, or -1.
static long wayland_drag_device(struct wayland_client *c)
{
        long at = 0;

        return wayland_device_next(c, 0, &at);
}

// An event of the drag's source with a word (or none), when the source is there and its version has it.
static void wayland_drag_tell_source(long op, long version, long word, int has_word)
{
        struct wayland_client *c = wayland_drag_client();
        struct wayland_source *source = wayland_drag_source();
        long slot = source ? wayland_slot_of(c, source->id) : -1;

        if (slot < 0 || c->objects[slot].version < version)
                return;
        if (has_word)
                wayland_ev(c, source->id, op, (unsigned int)word);
        else
                wayland_ev0(c, source->id, op);
}

static long wayland_xdrag(long what, long row, long x, long y)
{
        return wayland.host && wayland.host->xdrag ? wayland.host->xdrag(wayland.host->context, what, row, x, y) : 0;
}

// The X window a Wayland drag was over left: the X side told, and the source that it takes no type there.
static void wayland_drag_xleave(void)
{
        if (wayland.drag.xtarget < 0)
                return;
        wayland_xdrag(WAYLAND_XDRAG_LEAVE, wayland.drag.xtarget, 0, 0);
        wayland.drag.xtarget = -1;
        if (wayland.drag.x_accepted)
                wayland_drag_tell_source(WLE_wl_data_source_target, 1, 0, 1);
        wayland.drag.x_accepted = 0;
        wayland.drag.action = 0;
}

// The action a drag comes to: what the source and the target both take, the target's preferred one if it is among them, else the
// first of copy, move and ask; 0 for none. Told to both when it changes.
static void wayland_drag_actions(void)
{
        struct wayland_client *t = wayland_drag_target();
        struct wayland_source *source = wayland_drag_source();
        struct wayland_offer *o = t && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count ? &t->offers[wayland.drag.offer] : 0;
        unsigned int both, action = 0;
        long slot;

        if (!o)
                return;
        both = (source ? source->actions : wayland.drag.xsource ? wayland.drag.x_actions : WAYLAND_DRAG_ACTIONS) & o->actions;
        action = (both & o->preferred) ? o->preferred : (both & 1u) ? 1u : (both & 2u) ? 2u : (both & 4u) ? 4u : 0;
        if (action == wayland.drag.action)
                return;
        wayland.drag.action = action;
        wayland_drag_tell_source(WLE_wl_data_source_action, 3, action, 1);
        slot = wayland_slot_of(t, o->id);
        if (slot >= 0 && t->objects[slot].version >= 3)
                wayland_ev(t, o->id, WLE_wl_data_offer_action, action);
        if (wayland.drag.released && o->accepted && action)
                wayland_drag_decide();
}

// The target told the drag left it (its offer stays the client's until it destroys it).
static void wayland_drag_leave(void)
{
        struct wayland_client *t = wayland_drag_target();
        long device = t ? wayland_drag_device(t) : -1;

        if (device >= 0)
                wayland_ev0(t, t->objects[device].id, WLE_wl_data_device_leave);
        if (t && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count)
                t->offers[wayland.drag.offer].drag = 0;
        if (t && wayland.drag.target != wayland.drag.client)
                wayland_drag_tell_source(WLE_wl_data_source_target, 1, 0, 1); // no type accepted any more (a null type)
        wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1;
        wayland.drag.action = 0;
}

/*
        The drag ended: 0 cancelled (the target left, the source told cancelled), 1 the source's own end (nothing told it),
        2 finished (the source told dnd_finished). The drag is none after.
*/
static void wayland_drag_end(int how)
{
        int x_end = wayland.drag.xsource || wayland.drag.xtarget >= 0 || wayland.drag.x_seen;
        unsigned int action = wayland.drag.action;

        if (!wayland.drag.live)
                return;
        if (!wayland.drag.dropped && wayland_drag_target())
                wayland_drag_leave();
        if (!wayland.drag.dropped)
                wayland_drag_xleave();
        if (how == 0)
                wayland_drag_tell_source(WLE_wl_data_source_cancelled, 1, 0, 0);
        else if (how == 2)
                wayland_drag_tell_source(WLE_wl_data_source_dnd_finished, 3, 0, 0);
        wayland.drag.live = wayland.drag.dropped = wayland.drag.released = 0;
        wayland.drag.client = wayland.drag.source = wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1;
        if (wayland.drag.xsource)
                wayland_table_drop(&wayland.drag.x_types);
        wayland.drag.xtarget = -1, wayland.drag.x_accepted = 0, wayland.drag.xsource = 0, wayland.drag.x_seen = 0, wayland.drag.x_bytes = 0;
        wayland.drag.held = 0;
        // The X side is told last, the drag none by then: it may start another from what it is told.
        if (x_end)
                wayland_xdrag(WAYLAND_XDRAG_END, -1, how == 2, (long)action);
}

// The drag over (x, y) of row (-1: no window of a client): the surface there entered with an offer of its own, or moved over.
static void wayland_drag_over(long row, long x, long y)
{
        struct wayland_client *t = 0, *c = wayland_drag_client();
        struct wayland_surface *root = row >= 0 ? wayland_row_surface(row, &t) : 0, *is = 0;
        struct wayland_source *source = wayland_drag_source();
        long sx = 0, sy = 0, device;

        if ((!c && !wayland.drag.xsource) || wayland.drag.dropped)
                return;
        if (root)
                is = wayland_pick(t, root, x, y, &sx, &sy);
        // A drag with no source is its own client's alone.
        if (is && wayland.drag.source < 0 && !wayland.drag.xsource && t != c)
                is = 0;
        /*
                A window the host takes a drag over (xdrag): one whose client has no data device (an X window, whose drag the window manager
                of Xwayland takes) or a row no client has (a program of the compositor's, which takes drops through the window table). It is
                the target when the host says so, whoever the source is: a Wayland client's, an X client's or a program's.
        */
        if ((is && (device = wayland_drag_device(t)) < 0) || (row >= 0 && !root))
        {
                if ((source || wayland.drag.xsource) && wayland.host->xdrag)
                {
                        if (wayland.drag.xtarget == row)
                        {
                                wayland_xdrag(WAYLAND_XDRAG_OVER, row, x, y);
                                return;
                        }
                        if (wayland_drag_target())
                                wayland_drag_leave();
                        wayland_drag_xleave();
                        if (wayland_xdrag(WAYLAND_XDRAG_OVER, row, x, y) == 1)
                                wayland.drag.xtarget = row, wayland.drag.x_accepted = 0, wayland.drag.action = 0, wayland.drag.x_seen = 1;
                        return;
                }
                is = 0;
        }
        if (!is || wayland.drag.xtarget != row)
                wayland_drag_xleave();
        if (is && wayland.drag.target == t->slot && wayland.drag.surface == wayland_surface_index(t, is))
        {
                wayland_ev(t, t->objects[wayland_drag_device(t)].id, WLE_wl_data_device_motion,
                           (unsigned int)wayland.host->now(wayland.host->context),
                           (unsigned int)wayland_fixed(sx, is->scale120), (unsigned int)wayland_fixed(sy, is->scale120));
                return;
        }
        if (wayland_drag_target())
                wayland_drag_leave();
        if (!is)
                return;
        device = wayland_drag_device(t);
        {
                long index = -1;
                unsigned long id = 0;

                if (source || wayland.drag.xsource)
                {
                        id = wayland_offer_create_and_send(t, device, 0, 1,
                                                           source ? source->mime_table.at : wayland.drag.x_types.at,
                                                           source ? source->mime_bytes : wayland.drag.x_bytes,
                                                           source ? source->actions : wayland.drag.x_actions, &index);
                        if (!id)
                                return;
                }
                wayland.drag.target = t->slot, wayland.drag.surface = wayland_surface_index(t, is), wayland.drag.offer = index;
                wayland_ev(t, t->objects[device].id, WLE_wl_data_device_enter,
                           (unsigned int)wayland_input_serial(t), is->id,
                           (unsigned int)wayland_fixed(sx, is->scale120), (unsigned int)wayland_fixed(sy, is->scale120), (unsigned int)id);
        }
}

// start_drag: the source (or none), the origin surface, an icon (not drawn) and the serial of the press that holds the button.
static int wayland_drag_start(struct wayland_client *c, struct wayland_arg *a)
{
        long source = -1;

        if (a[0].slot >= 0 && c->objects[a[0].slot].iface == WLI_wl_data_source)
        {
                source = c->objects[a[0].slot].link;
                if (source < 0 || source >= c->tbl[WLT_SOURCE].count || c->sources[source].state != 0)
                        return wayland_fail(c, c->objects[a[0].slot].id, WLN_wl_data_device_error_used_source, "the source was used before");
        }
        // Only with the button its press holds: the serial is that press's, and a button is down. Otherwise the source is cancelled.
        if (!wayland.buttons || (unsigned int)a[3].u != c->press_serial || !wayland_surface_of(c, a[1].slot) || wayland.drag.live)
        {
                if (source >= 0)
                        wayland_source_cancel(c, source);
                return WAYLAND_OK;
        }
        if (source >= 0)
                c->sources[source].state = 4; // a drag's, never a selection's
        wayland.drag.live = 1, wayland.drag.dropped = wayland.drag.released = 0;
        wayland.drag.client = c->slot, wayland.drag.source = source, wayland.drag.origin = a[1].slot;
        wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1;
        wayland.drag.action = 0;
        wayland_drag_over(wayland.pointer_row, wayland.pointer_x, wayland.pointer_y);
        return WAYLAND_OK;
}

// The target accepted a type (none: it takes nothing here), told to the source.
static void wayland_drag_accept(struct wayland_client *c, long offer, const char *type)
{
        struct wayland_client *source_client = wayland_drag_client();
        struct wayland_source *source = wayland_drag_source();
        long slot;

        if (!wayland.drag.live || wayland.drag.target != c->slot || wayland.drag.offer != offer || wayland.drag.dropped)
                return;
        c->offers[offer].accepted = type != 0;
        slot = source ? wayland_slot_of(source_client, source->id) : -1;
        if (slot >= 0)
                wayland_ev_str(source_client, source->id, WLE_wl_data_source_target, type);
        // Released before this answer: a type and an action taken (or a version with no actions) is the drop; no type is the cancel.
        if (wayland.drag.released)
        {
                long offer_slot = wayland_slot_of(c, c->offers[offer].id);

                if (!type || wayland.drag.action || (offer_slot >= 0 && c->objects[offer_slot].version < 3))
                        wayland_drag_decide();
        }
}

/*
        The button let go: a drop where the target took a type (and an action at version 3), else the drag is cancelled. A target that
        was entered and has not yet said what it takes (a client answers an enter with its accept, which may still be in its socket as
        the release is read) is waited for: the drag is released, and decided when the answer comes (wayland_drag_decide).
*/
static void wayland_drag_release(void)
{
        struct wayland_client *t = wayland_drag_target();

        if (((t && (wayland.drag.source >= 0 || wayland.drag.xsource) && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count &&
              !t->offers[wayland.drag.offer].accepted) ||
             (wayland.drag.xtarget >= 0 && !wayland.drag.x_accepted)) &&
            !wayland.drag.released)
        {
                wayland.drag.released = 1;
                wayland.drag.released_at = wayland.host->now(wayland.host->context);
                return;
        }
        wayland_drag_decide();
}

static void wayland_drag_decide(void)
{
        struct wayland_client *t = wayland_drag_target();
        struct wayland_offer *o = t && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count ? &t->offers[wayland.drag.offer] : 0;
        long device = t ? wayland_drag_device(t) : -1, slot = o ? wayland_slot_of(t, o->id) : -1;
        int v3 = slot >= 0 && t->objects[slot].version >= 3;

        // Over a window the host takes it over: dropped when it takes a type (and an action, for a source of version 3 or the host's), the
        // host told to drop.
        if (wayland.drag.xtarget >= 0)
        {
                struct wayland_client *sc = wayland_drag_client();
                struct wayland_source *source = wayland_drag_source();
                long source_slot = source ? wayland_slot_of(sc, source->id) : -1;

                if (!wayland.drag.x_accepted || (source_slot >= 0 && sc->objects[source_slot].version >= 3 && !wayland.drag.action) ||
                    (wayland.drag.xsource && !wayland.drag.action))
                {
                        wayland_drag_end(0);
                        return;
                }
                wayland.drag.dropped = 1;
                wayland_xdrag(WAYLAND_XDRAG_DROP, wayland.drag.xtarget, 0, 0);
                wayland_drag_tell_source(WLE_wl_data_source_dnd_drop_performed, 3, 0, 0);
                return;
        }
        if (wayland.drag.source < 0 && !wayland.drag.xsource && t && device >= 0)
        {
                // A drag of the client's own, with nothing carried: dropped, and over.
                wayland_ev0(t, t->objects[device].id, WLE_wl_data_device_drop);
                wayland.drag.dropped = 1;
                wayland_drag_end(1);
                return;
        }
        if (!o || device < 0 || !o->accepted || (v3 && !wayland.drag.action))
        {
                wayland_drag_end(0);
                return;
        }
        wayland_ev0(t, t->objects[device].id, WLE_wl_data_device_drop);
        wayland.drag.dropped = 1;
        wayland_drag_tell_source(WLE_wl_data_source_dnd_drop_performed, 3, 0, 0);
}

// receive on a drag's offer: the target's descriptor handed to the source's send, as it is (none of its bytes pass the compositor).
static int wayland_drag_receive(struct wayland_client *c, struct wayland_object *self, struct wayland_arg *a)
{
        struct wayland_client *source_client = wayland_drag_client();
        struct wayland_source *source = wayland_drag_source();

        // An X drag's: the X side converts its selection into the descriptor.
        if (wayland.drag.live && wayland.drag.xsource && wayland.drag.target == c->slot && wayland.drag.offer == self->link && a[0].text &&
            a[0].length > 0 && wayland.host->xreceive)
        {
                wayland.host->xreceive(wayland.host->context, c->slot, 2, (const char *)a[0].text, a[1].fd);
                wayland.fds--;
                a[1].fd = -1;
                return WAYLAND_OK;
        }
        if (wayland.drag.live && wayland.drag.target == c->slot && wayland.drag.offer == self->link && a[0].text && a[0].length > 1 &&
            wayland_source_send_fd(source_client, source, (const char *)a[0].text, a[1].fd, 0))
                a[1].fd = -1; // the source's now, already counted in wayland.fds until sent
        return WAYLAND_OK;
}

// A drag's offer destroyed by its target: a dropped drag ends (finished at version 2, where there is no finish; cancelled at 3).
static void wayland_drag_offer_gone(struct wayland_client *c, long offer)
{
        if (!wayland.drag.live || wayland.drag.target != c->slot || wayland.drag.offer != offer)
                return;
        if (wayland.drag.dropped)
        {
                long slot = wayland_slot_of(c, c->offers[offer].id);

                wayland_drag_end(c->offers[offer].finished || (slot >= 0 && c->objects[slot].version < 3) ? 2 : 0);
                return;
        }
        wayland.drag.offer = -1;
}

// Whether a drag holds the pointer (the host sends its motion and its release here, and Escape cancels it): a Wayland client's, or a
// program's of the compositor's (an X client's pointer is Xwayland's).
WAYLAND_API int wayland_dragging(void)
{
        return wayland.drag.live && !wayland.drag.dropped && !wayland.drag.released && (!wayland.drag.xsource || wayland.drag.held);
}

/*
        The X side of drag and drop (the window manager of Xwayland's, DRAG AND DROP in xwm.c).

        A Wayland drag over an X window: wayland_drag_x_types is the source's types (each with its null) and actions; wayland_drag_x_status
        what the X window takes there (a type, and the action as the drag's bits), told to the source; wayland_drag_x_send the source's
        send of a type into the descriptor (the shim's from then on: 1 when sent); wayland_drag_x_finished the X window done with the drop.

        A drag an X window makes, over the Wayland windows: wayland_drag_x_begin its types and actions (none while another drag is), then
        wayland_drag_x_motion, wayland_drag_x_leave and wayland_drag_x_drop as the X source says them, and wayland_drag_x_answer what
        the Wayland target takes (1 with the action, for the X source's status).
*/
WAYLAND_API long wayland_drag_x_types(const char **types, unsigned int *actions)
{
        struct wayland_source *source = wayland_drag_source();

        // A drag of the host's (a program's over an X window): its own types and actions.
        if (wayland.drag.live && wayland.drag.xsource && wayland.drag.x_bytes)
        {
                *types = (const char *)wayland.drag.x_types.at;
                if (actions)
                        *actions = wayland.drag.x_actions;
                return wayland.drag.x_bytes;
        }
        if (!wayland.drag.live || !source || !source->mime_bytes)
                return 0;
        *types = (const char *)source->mime_table.at;
        if (actions)
                *actions = source->actions ? source->actions : 1u;
        return source->mime_bytes;
}

WAYLAND_API void wayland_drag_x_status(long accepted, unsigned long action)
{
        struct wayland_client *sc = wayland_drag_client();
        struct wayland_source *source = wayland_drag_source();

        if (!wayland.drag.live || wayland.drag.xtarget < 0 || wayland.drag.dropped || (!source && !wayland.drag.xsource))
                return;
        action = accepted ? action & (source ? (source->actions ? source->actions : WAYLAND_DRAG_ACTIONS) : wayland.drag.x_actions) : 0;
        action = action & 1u ? 1u : action & 2u ? 2u : action & 4u ? 4u : 0;
        if (!!accepted != wayland.drag.x_accepted)
        {
                long slot = source ? wayland_slot_of(sc, source->id) : -1;

                wayland.drag.x_accepted = !!accepted;
                if (slot >= 0)
                        wayland_ev_str(sc, source->id, WLE_wl_data_source_target,
                                       accepted && source->mime_bytes ? (const char *)source->mime_table.at : 0);
        }
        if (action != wayland.drag.action)
        {
                wayland.drag.action = (unsigned int)action;
                wayland_drag_tell_source(WLE_wl_data_source_action, 3, (long)action, 1);
        }
        if (wayland.drag.released)
                wayland_drag_decide();
}

WAYLAND_API int wayland_drag_x_send(const char *mime, long fd)
{
        if (!wayland.drag.live || (!wayland.drag.dropped && wayland.drag.xtarget < 0))
                return 0;
        return wayland_source_send_fd(wayland_drag_client(), wayland_drag_source(), mime, fd, 1);
}

// The host's target (an X window, a program's) is done with the drop: the drag ends, finished or not.
WAYLAND_API void wayland_drag_x_finished(long ok)
{
        if (wayland.drag.live && wayland.drag.dropped && wayland.drag.xtarget >= 0)
                wayland_drag_end(ok ? 2 : 0);
}

WAYLAND_API int wayland_drag_x_begin(const char *types, long bytes, unsigned long actions)
{
        if (!wayland.host || wayland.drag.live || !types || bytes <= 0)
                return 0;
        wayland.drag.x_bytes = 0;
        if (!wayland_mime_buf_append(&wayland.drag.x_types, &wayland.drag.x_bytes, (const unsigned char *)types, bytes))
                return 0;
        wayland.drag.x_bytes = bytes;
        wayland.drag.live = 1, wayland.drag.dropped = wayland.drag.released = 0, wayland.drag.xsource = 1;
        wayland.drag.x_actions = (unsigned int)(actions & WAYLAND_DRAG_ACTIONS ? actions & WAYLAND_DRAG_ACTIONS : 1u);
        wayland.drag.client = wayland.drag.source = wayland.drag.origin = -1;
        wayland.drag.target = wayland.drag.surface = wayland.drag.offer = wayland.drag.xtarget = -1;
        wayland.drag.action = 0;
        return 1;
}

/* The actions an X drag offers changed under it (the person held a key, or asked): its offer is told them, and the action worked out again. */
WAYLAND_API void wayland_drag_x_actions(unsigned long actions)
{
        struct wayland_client *t = wayland_drag_target();
        struct wayland_offer *o = t && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count ? &t->offers[wayland.drag.offer] : 0;
        long slot = o ? wayland_slot_of(t, o->id) : -1;

        actions &= WAYLAND_DRAG_ACTIONS;
        if (!wayland.drag.live || !wayland.drag.xsource || wayland.drag.dropped || !actions || actions == wayland.drag.x_actions)
                return;
        wayland.drag.x_actions = (unsigned int)actions;
        if (slot >= 0 && t->objects[slot].version >= 3)
                wayland_ev(t, o->id, WLE_wl_data_offer_source_actions, (unsigned int)actions);
        wayland_drag_actions();
}

// Whether an X client's drag is over the Wayland side and not dropped (the host's Escape is its cancel too).
WAYLAND_API int wayland_x_dragging(void)
{
        return wayland.drag.live && wayland.drag.xsource && !wayland.drag.held && !wayland.drag.dropped;
}

/*
        A drag a program of the compositor's makes (the host holds its types and bytes, its window table's copy): an X drag's but that the
        compositor drives its pointer, as a Wayland client's (wayland_dragging: its motion and release come here, Escape cancels it), first
        over the window row at (x, y). 0 while another drag is.
*/
WAYLAND_API int wayland_drag_host_begin(const char *types, long bytes, unsigned long actions, long row, long x, long y)
{
        if (!wayland_drag_x_begin(types, bytes, actions))
                return 0;
        wayland.drag.held = 1;
        wayland.pointer_x = x, wayland.pointer_y = y;
        wayland_drag_over(row, x, y);
        return 1;
}

// The button of a drag the host makes let go (or its finger lifted): its drop, or its cancel.
WAYLAND_API void wayland_drag_host_release(void)
{
        if (wayland.drag.live && wayland.drag.held && !wayland.drag.dropped && !wayland.drag.released)
                wayland_drag_release();
}

WAYLAND_API void wayland_drag_x_motion(long row, long x, long y)
{
        if (wayland.drag.live && wayland.drag.xsource && !wayland.drag.held && !wayland.drag.dropped && !wayland.drag.released)
        {
                wayland.pointer_x = x, wayland.pointer_y = y;
                wayland_drag_over(row, x, y);
        }
}

WAYLAND_API void wayland_drag_x_leave(void)
{
        if (wayland.drag.live && wayland.drag.xsource && !wayland.drag.held && !wayland.drag.dropped)
                wayland_drag_end(0);
}

WAYLAND_API void wayland_drag_x_drop(void)
{
        if (wayland.drag.live && wayland.drag.xsource && !wayland.drag.held && !wayland.drag.dropped && !wayland.drag.released)
                wayland_drag_release();
}

WAYLAND_API int wayland_drag_x_answer(unsigned long *action)
{
        struct wayland_client *t = wayland_drag_target();
        struct wayland_offer *o = t && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count ? &t->offers[wayland.drag.offer] : 0;
        long slot = o ? wayland_slot_of(t, o->id) : -1;

        *action = 0;
        if (!wayland.drag.live || !wayland.drag.xsource || !o || !o->accepted)
                return 0;
        *action = wayland.drag.action ? wayland.drag.action : (slot >= 0 && t->objects[slot].version < 3 ? 1u : 0);
        return *action != 0;
}

WAYLAND_API void wayland_drag_cancel(void)
{
        if (wayland_dragging())
                wayland_drag_end(0);
}

// The drag over the row under the pointer (-1 for none), from the host that knows which window that is.
WAYLAND_API void wayland_drag_motion(long row, long x, long y)
{
        if (wayland_dragging())
        {
                wayland.pointer_x = x, wayland.pointer_y = y;
                wayland_drag_over(row, x, y);
        }
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
                        wayland_source_type(c, self->link, a[0].text, a[0].length);
                else if (op == WLQ_wl_data_source_set_actions && !(a[0].u & ~7UL))
                        c->sources[self->link].actions = (unsigned int)a[0].u;
                else if (op == WLQ_wl_data_source_destroy)
                        wayland_source_drop(c, self->link);
                else if (op == WLQ_wl_data_source_set_actions && (a[0].u & ~7UL))
                        return wayland_fail(c, self->id, WLN_wl_data_source_error_invalid_action_mask, "the actions are not a mask of three");
                return WAYLAND_OK;
        case WLI_wl_data_device:
                if (op == WLQ_wl_data_device_set_selection)
                        return wayland_set_selection(c, self, 0, a);
                if (op == WLQ_wl_data_device_start_drag)
                        return wayland_drag_start(c, a);
                wayland_device_remove(c, 0, self->id);
                return WAYLAND_OK;
        case WLI_zwp_primary_selection_device_v1:
                if (op == WLQ_zwp_primary_selection_device_v1_set_selection)
                        return wayland_set_selection(c, self, 1, a);
                wayland_device_remove(c, 1, self->id);
                return WAYLAND_OK;
        case WLI_wl_data_offer:
                if (op == WLQ_wl_data_offer_receive)
                        return c->offers[self->link].drag ? wayland_drag_receive(c, self, a) : wayland_receive(c, self, a);
                if (op == WLQ_wl_data_offer_destroy)
                {
                        if (c->offers[self->link].live)
                        {
                                c->offers[self->link].live = 0;
                                wayland_drag_offer_gone(c, self->link);
                                wayland_tbl_free(c, WLT_OFFER, self->link);
                                self->link = -1;
                        }
                }
                else if (op == WLQ_wl_data_offer_finish)
                {
                        if (!c->offers[self->link].drag || !wayland.drag.live || !wayland.drag.dropped || wayland.drag.target != c->slot ||
                            wayland.drag.offer != self->link)
                                return wayland_fail(c, self->id, WLN_wl_data_offer_error_invalid_finish, "finish is for a dropped drag's offer");
                        c->offers[self->link].finished = 1;
                        wayland_drag_end(2);
                }
                else if (op == WLQ_wl_data_offer_accept && c->offers[self->link].drag)
                        wayland_drag_accept(c, self->link, a[1].length > 0 ? (const char *)a[1].text : 0);
                else if (op == WLQ_wl_data_offer_set_actions && c->offers[self->link].drag)
                {
                        if ((a[0].u & ~7UL) || (a[1].u & (a[1].u - 1)) || (a[1].u & ~7UL))
                                return wayland_fail(c, self->id, WLN_wl_data_offer_error_invalid_action_mask, "the actions are not a mask of three");
                        c->offers[self->link].actions = (unsigned int)a[0].u, c->offers[self->link].preferred = (unsigned int)a[1].u;
                        wayland_drag_actions();
                }
                return WAYLAND_OK;
        case WLI_zwp_primary_selection_offer_v1:
                if (op == WLQ_zwp_primary_selection_offer_v1_receive)
                        return wayland_receive(c, self, a);
                if (c->offers[self->link].live)
                {
                        c->offers[self->link].live = 0;
                        wayland_tbl_free(c, WLT_OFFER, self->link);
                        self->link = -1;
                }
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
/*
        An object made for a surface takes the surface's slot and generation, so wayland_surface_of finds it in O(1) and returns 0 once gone.
        Declarative table for all 12 surface-extension manager bind requests (uniqueness check, dead-surface policy, role check).
*/
static void wayland_surface_link(struct wayland_client *c, long slot, struct wayland_surface *s)
{
        if (slot < 0 || !s)
                return;
        c->objects[slot].link = (int)wayland_surface_index(c, s);
        c->objects[slot].aux = s->gen;
}

#define WAYLAND_SOFF(field) ((long)(unsigned long)&((struct wayland_surface *)0)->field)

struct wayland_ext_spec
{
        unsigned char iface, bind_op, fail_dead, check_role, uniq_size;
        long uniq_off;
        unsigned int uniq_err;
        const char *uniq_msg;
};

static const struct wayland_ext_spec wayland_ext_specs[] = {
        {WLI_wp_viewporter, WLQ_wp_viewporter_get_viewport, 0, 0, 4, WAYLAND_SOFF(viewport_id),
         WLN_wp_viewporter_error_viewport_exists, "the surface has a viewport"},
        {WLI_wp_fractional_scale_manager_v1, WLQ_wp_fractional_scale_manager_v1_get_fractional_scale, 0, 0, 4, WAYLAND_SOFF(fractional_id),
         WLN_wp_fractional_scale_manager_v1_error_fractional_scale_exists, "the surface has a fractional scale"},
        {WLI_wp_linux_drm_syncobj_manager_v1, WLQ_wp_linux_drm_syncobj_manager_v1_get_surface, 0, 0, 4, WAYLAND_SOFF(syncobj_id),
         WLN_wp_linux_drm_syncobj_manager_v1_error_surface_exists, "the surface already has a syncobj surface"},
        {WLI_xdg_wm_dialog_v1, WLQ_xdg_wm_dialog_v1_get_xdg_dialog, 1, 1, 0, -1, 0, 0},
        {WLI_wp_content_type_manager_v1, WLQ_wp_content_type_manager_v1_get_surface_content_type, 1, 0, 0, -1, 0, 0},
        {WLI_wp_alpha_modifier_v1, WLQ_wp_alpha_modifier_v1_get_surface, 1, 0, 0, -1, 0, 0},
        {WLI_wp_fifo_manager_v1, WLQ_wp_fifo_manager_v1_get_fifo, 1, 0, 0, -1, 0, 0},
        {WLI_wp_commit_timing_manager_v1, WLQ_wp_commit_timing_manager_v1_get_timer, 1, 0, 0, -1, 0, 0},
        {WLI_zwp_xwayland_keyboard_grab_manager_v1, WLQ_zwp_xwayland_keyboard_grab_manager_v1_grab_keyboard, 1, 0, 0, -1, 0, 0},
        {WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts, 1, 0, 1, WAYLAND_SOFF(shortcuts),
         WLN_zwp_keyboard_shortcuts_inhibit_manager_v1_error_already_inhibited, "the surface has an inhibitor"},
        {WLI_wp_tearing_control_manager_v1, WLQ_wp_tearing_control_manager_v1_get_tearing_control, 1, 0, 1, WAYLAND_SOFF(tearing),
         WLN_wp_tearing_control_manager_v1_error_tearing_control_exists, "the surface has a tearing control"},
        {WLI_zwp_idle_inhibit_manager_v1, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, 1, 0, 0, -1, 0, 0},
};

static int wayland_ext_bind(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a, struct wayland_surface **out_s)
{
        const struct wayland_ext_spec *sp = 0;
        struct wayland_surface *s;

        if (out_s)
                *out_s = 0;
        for (unsigned long i = 0; i < sizeof wayland_ext_specs / sizeof wayland_ext_specs[0]; i++)
                if (wayland_ext_specs[i].iface == self->iface)
                {
                        sp = &wayland_ext_specs[i];
                        break;
                }
        if (!sp || op != sp->bind_op)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[1].slot);
        if (s && sp->check_role && s->role_id != c->objects[a[1].slot].id)
                s = 0;
        if (!s)
                return sp->fail_dead ? wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, sp->check_role ? "no such toplevel" : "a surface is gone")
                                     : WAYLAND_OK;
        if (sp->uniq_size == 4)
        {
                unsigned int *field = (unsigned int *)((unsigned char *)s + sp->uniq_off);

                if (*field)
                        return wayland_fail(c, self->id, sp->uniq_err, sp->uniq_msg);
                *field = c->objects[a[0].slot].id;
        }
        else if (sp->uniq_size == 1)
        {
                unsigned char *field = (unsigned char *)s + sp->uniq_off;

                if (*field)
                        return wayland_fail(c, self->id, sp->uniq_err, sp->uniq_msg);
                *field = 1;
        }
        wayland_surface_link(c, a[0].slot, s);
        if (out_s)
                *out_s = s;
        return WAYLAND_OK;
}

static int wayland_viewport_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (self->iface == WLI_wp_viewporter)
                return wayland_ext_bind(c, self, op, a, 0);
        s = wayland_surface_of(c, self - c->objects);
        if (op == WLQ_wp_viewport_destroy)
        {
                if (s)
                {
                        s->viewport_id = 0;
                        s->pending.dst_w = s->pending.dst_h = s->pending.src_w = s->pending.src_h = 0;
                }
                return WAYLAND_OK;
        }
        if (!s)
                return wayland_fail(c, self->id, WLN_wp_viewport_error_no_surface, "the surface is gone");
        if (op == WLQ_wp_viewport_set_source)
        {
                if (a[0].i == -256 && a[1].i == -256 && a[2].i == -256 && a[3].i == -256)
                {
                        s->pending.src_w = s->pending.src_h = 0;
                        return WAYLAND_OK;
                }
                if (a[0].i < 0 || a[1].i < 0 || a[2].i <= 0 || a[3].i <= 0)
                        return wayland_fail(c, self->id, WLN_wp_viewport_error_bad_value, "the source is negative or empty");
                s->pending.src_x = a[0].i;
                s->pending.src_y = a[1].i;
                s->pending.src_w = a[2].i;
                s->pending.src_h = a[3].i;
                return WAYLAND_OK;
        }
        if (op == WLQ_wp_viewport_set_destination)
        {
                if (a[0].i == -1 && a[1].i == -1)
                {
                        s->pending.dst_w = s->pending.dst_h = 0;
                        return WAYLAND_OK;
                }
                if (a[0].i <= 0 || a[1].i <= 0)
                        return wayland_fail(c, self->id, WLN_wp_viewport_error_bad_value, "the destination is empty");
                s->pending.dst_w = wayland_min(a[0].i, WAYLAND_DIMENSION);
                s->pending.dst_h = wayland_min(a[1].i, WAYLAND_DIMENSION);
        }
        return WAYLAND_OK;
}

/* wp_fractional_scale: the scale of the screen the surface is on, in 120ths, said when it is made and whenever it changes. */
static int wayland_fractional_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;
        int status;

        if (self->iface == WLI_wp_fractional_scale_v1)
        {
                s = wayland_surface_of(c, self - c->objects);
                if (s && op == WLQ_wp_fractional_scale_v1_destroy)
                        s->fractional_id = 0;
                return WAYLAND_OK;
        }
        status = wayland_ext_bind(c, self, op, a, &s);
        if (status == WAYLAND_OK && s)
                wayland_ev(c, s->fractional_id, WLE_wp_fractional_scale_v1_preferred_scale,
                           (unsigned int)(s->told120 > 0 ? s->told120 : WAYLAND_UNIT));
        return status;
}

/* The compositor's shape for a protocol shape: the ones it draws, and the arrow (0) for the rest. */
static long wayland_cursor_code(unsigned long shape)
{
        static const signed char map[WLN_wp_cursor_shape_device_v1_shape_all_resize + 1] = {
                [WLN_wp_cursor_shape_device_v1_shape_text] = WAYLAND_CURSOR_TEXT,
                [WLN_wp_cursor_shape_device_v1_shape_vertical_text] = WAYLAND_CURSOR_TEXT,
                [WLN_wp_cursor_shape_device_v1_shape_crosshair] = WAYLAND_CURSOR_CROSS,
                [WLN_wp_cursor_shape_device_v1_shape_move] = WAYLAND_CURSOR_MOVE,
                [WLN_wp_cursor_shape_device_v1_shape_all_scroll] = WAYLAND_CURSOR_MOVE,
                [WLN_wp_cursor_shape_device_v1_shape_e_resize] = WAYLAND_CURSOR_RESIZE_H,
                [WLN_wp_cursor_shape_device_v1_shape_w_resize] = WAYLAND_CURSOR_RESIZE_H,
                [WLN_wp_cursor_shape_device_v1_shape_ew_resize] = WAYLAND_CURSOR_RESIZE_H,
                [WLN_wp_cursor_shape_device_v1_shape_col_resize] = WAYLAND_CURSOR_RESIZE_H,
                [WLN_wp_cursor_shape_device_v1_shape_n_resize] = WAYLAND_CURSOR_RESIZE_V,
                [WLN_wp_cursor_shape_device_v1_shape_s_resize] = WAYLAND_CURSOR_RESIZE_V,
                [WLN_wp_cursor_shape_device_v1_shape_ns_resize] = WAYLAND_CURSOR_RESIZE_V,
                [WLN_wp_cursor_shape_device_v1_shape_row_resize] = WAYLAND_CURSOR_RESIZE_V,
                [WLN_wp_cursor_shape_device_v1_shape_nw_resize] = WAYLAND_CURSOR_RESIZE_NWSE,
                [WLN_wp_cursor_shape_device_v1_shape_se_resize] = WAYLAND_CURSOR_RESIZE_NWSE,
                [WLN_wp_cursor_shape_device_v1_shape_nwse_resize] = WAYLAND_CURSOR_RESIZE_NWSE,
                [WLN_wp_cursor_shape_device_v1_shape_ne_resize] = WAYLAND_CURSOR_RESIZE_NESW,
                [WLN_wp_cursor_shape_device_v1_shape_sw_resize] = WAYLAND_CURSOR_RESIZE_NESW,
                [WLN_wp_cursor_shape_device_v1_shape_nesw_resize] = WAYLAND_CURSOR_RESIZE_NESW,
                [WLN_wp_cursor_shape_device_v1_shape_pointer] = WAYLAND_CURSOR_HAND,
                [WLN_wp_cursor_shape_device_v1_shape_grab] = WAYLAND_CURSOR_HAND,
                [WLN_wp_cursor_shape_device_v1_shape_grabbing] = WAYLAND_CURSOR_HAND,
                [WLN_wp_cursor_shape_device_v1_shape_progress] = WAYLAND_CURSOR_WAIT,
                [WLN_wp_cursor_shape_device_v1_shape_wait] = WAYLAND_CURSOR_WAIT,
                [WLN_wp_cursor_shape_device_v1_shape_not_allowed] = WAYLAND_CURSOR_NOTALLOWED,
                [WLN_wp_cursor_shape_device_v1_shape_no_drop] = WAYLAND_CURSOR_NOTALLOWED,
        };

        return shape < sizeof map ? map[shape] : WAYLAND_CURSOR_ARROW;
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
            wayland.row[wayland.pointer_row].client == c->slot && wayland_pointer_serial_ok(c, a[0].u))
        {
                wayland.host->row_do(wayland.host->context, wayland.pointer_row, WAYLAND_DO_CURSOR, wayland_cursor_code(a[1].u), 0);
                // A shape named takes the place of a picture the client drew (set_cursor's surface).
                if (wayland.cursor_client >= 0)
                {
                        wayland.cursor_client = wayland.cursor_surface = -1;
                        wayland.cursor_serial++;
                }
        }
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
                        wayland_ev(c, id, WLE_zwp_linux_dmabuf_v1_format, info->entry[at].format);
                if (version >= 3)
                        wayland_ev(c, id, WLE_zwp_linux_dmabuf_v1_modifier, info->entry[at].format,
                                   (unsigned int)(info->entry[at].modifier >> 32), (unsigned int)(info->entry[at].modifier & 0xffffffffUL));
        }
}

// Supported DRM fourcc codes and opaque/shm format mapping.
static int wayland_fourcc_supported(unsigned int fourcc)
{
        switch (fourcc)
        {
        case WAYLAND_FOURCC_ARGB8888:
        case WAYLAND_FOURCC_XRGB8888:
        case WAYLAND_FOURCC_ABGR8888:
        case WAYLAND_FOURCC_XBGR8888:
        case WAYLAND_FOURCC_ARGB2101010:
        case WAYLAND_FOURCC_XRGB2101010:
        case WAYLAND_FOURCC_ABGR2101010:
        case WAYLAND_FOURCC_XBGR2101010:
        case WAYLAND_FOURCC_NV12:
                return 1;
        default:
                return 0;
        }
}

static long wayland_shm_format_of(unsigned int fourcc)
{
        if (fourcc == WAYLAND_FOURCC_ARGB8888)
                return WLN_wl_shm_format_argb8888;
        if (fourcc == WAYLAND_FOURCC_XRGB8888)
                return WLN_wl_shm_format_xrgb8888;
        return (long)fourcc;
}

static void wayland_feedback_queue(struct wayland_client *c, unsigned long id)
{
        for (long at = 0; at < c->feedbacks; at++)
                if (c->feedback[at].id == id)
                {
                        c->feedback[at].need = 1;
                        c->feedback_pending = 1;
                        wayland_out_mark(c->slot);
                        return;
                }
}

// The feedback to one feedback object: the table, the main device, each tranche in order, and done.
static void wayland_feedback_send(struct wayland_client *c, long slot)
{
        const struct wayland_dmabuf_info *info = &wayland.dmabuf;
        unsigned long id = c->objects[slot].id;
        struct wayland_event e;
        long size = 0, fd = wayland.host->dmabuf_table(wayland.host->context, &size);

        if (fd < 0 || size <= 0)
                return;
        if (!wayland_out_fd_room(c))
        {
                wayland_feedback_queue(c, id);
                return;
        }
        e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_format_table);
        wayland_word(&e, (unsigned long)size);
        wayland_out_fd_put(c, fd, 0, e.start);
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
                wayland_ev(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_flags, (unsigned int)info->tranche[at].flags);
                for (long each = 0; each < info->tranche[at].count; each++)
                {
                        indices[each * 2] = info->tranche[at].index[each];
                        indices[each * 2 + 1] = 0;
                }
                e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_formats);
                wayland_array(&e, indices, (unsigned long)info->tranche[at].count * 2);
                wayland_end(&e);
                wayland_ev0(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_done);
        }
        wayland_ev0(c, id, WLE_zwp_linux_dmabuf_feedback_v1_done);
}

static void wayland_feedback_flush(struct wayland_client *c)
{
        int more = 0;

        if (!c->feedback_pending || c->dead)
                return;
        for (long at = 0; at < c->feedbacks; at++)
        {
                long object;

                if (!c->feedback[at].need)
                        continue;
                if (!wayland_out_fd_room(c))
                {
                        more = 1;
                        break;
                }
                c->feedback[at].need = 0;
                object = wayland_slot_of(c, c->feedback[at].id);
                if (object >= 0 && c->objects[object].iface == WLI_zwp_linux_dmabuf_feedback_v1)
                        wayland_feedback_send(c, object);
        }
        for (long at = 0; at < c->feedbacks && !more; at++)
                if (c->feedback[at].need)
                        more = 1;
        c->feedback_pending = (unsigned char)more;
        if (c->feedback_pending)
                wayland_out_mark(c->slot);
}

/*
        What the host takes, read and checked: at least one pair, supported formats, tranches that name pairs there are,
        and a table to name them in. A host that fails any of it takes no dma-bufs, and the global is not offered.
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
                if (!wayland_fourcc_supported(info->entry[at].format))
                        goto none;
        for (long at = 0; at < info->tranches; at++)
        {
                if (info->tranche[at].count < 1 || info->tranche[at].count > info->count)
                        goto none;
                for (long each = 0; each < info->tranche[at].count; each++)
                        if (info->tranche[at].index[each] >= info->count)
                                goto none;
        }
        if (wayland.syncobj.available)
        {
                long caps = wayland.syncobj.available(wayland.syncobj.context, wayland.drm_node, (long)sizeof wayland.drm_node);

                wayland.syncobj_ok = (caps & WAYLAND_SYNCOBJ_TIMELINE_OK) != 0;
                if (!(caps & WAYLAND_SYNCOBJ_NODE_OK))
                        wayland.drm_node[0] = 0;
        }
        return;
none:
        info->count = 0;
        wayland.drm_node[0] = 0;
}

static void wayland_global_remove_send(struct wayland_client *c, unsigned long registry, long name)
{
        wayland_ev(c, registry, WLE_wl_registry_global_remove, (unsigned int)name);
}

static long wayland_global_name(long iface)
{
        for (long at = 0; at < WAYLAND_GLOBAL_COUNT; at++)
                if (wayland_globals[at].iface == iface)
                        return at + 1;
        return 0;
}

static void wayland_global_flip_notify(long iface, int was_on, int now_on)
{
        long name = was_on != now_on ? wayland_global_name(iface) : 0;

        if (!name)
                return;
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c)
                        continue;
                for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
                        if (c->objects[at].iface == WLI_wl_registry)
                        {
                                if (now_on)
                                        wayland_global_send(c, c->objects[at].id, name);
                                else
                                        wayland_global_remove_send(c, c->objects[at].id, name);
                        }
        }
}

/*
        The host's dma-bufs changed (a device came or went, a plane to scan out was found): what is taken is read again,
        zwp_linux_dmabuf_v1 / wl_drm / wp_linux_drm_syncobj_manager_v1 are advertised or withdrawn on open registries if their
        availability flipped, and every feedback object of every client is sent it again.
*/
WAYLAND_API void wayland_dmabuf_changed(void)
{
        int was_dmabuf, was_drm, was_syncobj;
        int now_dmabuf, now_drm, now_syncobj;

        if (!wayland.host)
                return;
        was_dmabuf = wayland.dmabuf.count > 0;
        was_drm = was_dmabuf && wayland.drm_node[0] != 0;
        was_syncobj = wayland.syncobj_ok != 0;
        wayland_dmabuf_load();
        now_dmabuf = wayland.dmabuf.count > 0;
        now_drm = now_dmabuf && wayland.drm_node[0] != 0;
        now_syncobj = wayland.syncobj_ok != 0;
        wayland_global_flip_notify(WLI_zwp_linux_dmabuf_v1, was_dmabuf, now_dmabuf);
        wayland_global_flip_notify(WLI_wl_drm, was_drm, now_drm);
        wayland_global_flip_notify(WLI_wp_linux_drm_syncobj_manager_v1, was_syncobj, now_syncobj);
        if (!now_dmabuf)
                return;
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c)
                        continue;
                for (long at = 0; at < c->feedbacks; at++)
                {
                        long object = wayland_slot_of(c, c->feedback[at].id);

                        if (object >= 0 && c->objects[object].iface == WLI_zwp_linux_dmabuf_feedback_v1)
                                wayland_feedback_send(c, object);
                }
        }
}

static void wayland_params_free(struct wayland_client *c, struct wayland_params *p)
{
        if (!p->live)
                return;
        for (long plane = 0; plane < 4; plane++)
                if (p->fd[plane] >= 0)
                {
                        wayland.host->close(wayland.host->context, p->fd[plane]);
                        wayland.fds--;
                        p->fd[plane] = -1;
                }
        p->live = 0;
        wayland_tbl_free(c, WLT_PARAMS, p - c->params);
}

static long wayland_dmabuf_alloc_buffer(struct wayland_client *c, unsigned long width, unsigned long height, unsigned long format,
                                        unsigned long flags, long plane_count, long fds[4], const unsigned long offsets[4],
                                        const unsigned long strides[4], unsigned long modifier, long id_slot)
{
        long offset = (long)offsets[0], stride = (long)strides[0], need, pool = -1, buffer = -1, found = 0;
        void *at;

        if (format == WAYLAND_FOURCC_NV12)
                need = offset + stride * (long)height;
        else if (modifier == 0 || modifier == WAYLAND_MODIFIER_INVALID)
                need = offset + stride * ((long)height - 1) + (long)width * 4;
        else
                need = offset + stride * (long)height;
        for (long entry = 0; entry < wayland.dmabuf.count; entry++)
                found |= wayland.dmabuf.entry[entry].format == format &&
                         (modifier == WAYLAND_MODIFIER_INVALID || wayland.dmabuf.entry[entry].modifier == modifier);
        if (!found || (flags & ~1UL) || need < 1)
                return -2;
        pool = wayland_tbl_alloc(c, WLT_POOL);
        buffer = pool >= 0 ? wayland_tbl_alloc(c, WLT_BUFFER) : -1;
        // A buffer of a modifier that is a layout of tiles (not linear, nor the implicit one) has nothing the processor can read: the host is
        // asked to take it unmapped (a size below zero), and its pictures are the card's to read (no pixels).
        at = buffer >= 0 ? wayland.host->dmabuf_map(wayland.host->context, fds[0], modifier && modifier != WAYLAND_MODIFIER_INVALID ? -need : need) : 0;
        if (!at)
        {
                if (buffer >= 0)
                        wayland_tbl_free(c, WLT_BUFFER, buffer);
                if (pool >= 0)
                        wayland_tbl_free(c, WLT_POOL, pool);
                return -2;
        }
        c->pools[pool].live = 1;
        c->pools[pool].fd = fds[0];
        fds[0] = -1; // the pool holds it now, and wayland.fds still counts it
        for (long k = 0; k < 3; k++)
        {
                c->pools[pool].extra_fd[k] = (k + 1 < plane_count) ? fds[k + 1] : -1;
                if (k + 1 < plane_count)
                        fds[k + 1] = -1;
        }
        c->pools[pool].dmabuf = 1;
        c->pools[pool].unmapped = (at == WAYLAND_DMABUF_UNMAPPED);
        c->pools[pool].size = need;
        c->pools[pool].refs = 1;
        c->pools[pool].gone = 1;
        c->pools[pool].id = ++wayland.pool_id;
        c->pools[pool].at = c->pools[pool].unmapped ? 0 : at;
        c->buffers[buffer].live = 1;
        c->buffers[buffer].pool = pool;
        c->buffers[buffer].offset = offset;
        c->buffers[buffer].width = (long)width;
        c->buffers[buffer].height = (long)height;
        c->buffers[buffer].stride = stride;
        c->buffers[buffer].format = wayland_shm_format_of((unsigned int)format);
        c->buffers[buffer].fourcc = (unsigned int)format;
        c->buffers[buffer].modifier = modifier;
        c->buffers[buffer].plane_count = plane_count;
        for (long k = 0; k < 4; k++)
        {
                c->buffers[buffer].plane_offset[k] = k < plane_count ? offsets[k] : 0;
                c->buffers[buffer].plane_stride[k] = k < plane_count ? strides[k] : 0;
        }
        c->buffers[buffer].y_invert = (flags & 1) ? 1 : 0;
        c->buffers[buffer].rel_pending = 0;
        c->buffers[buffer].explicit_sync = 0;
        c->buffers[buffer].id = c->objects[id_slot].id;
        c->buffers[buffer].held = c->buffers[buffer].zombie = 0;
        c->objects[id_slot].link = (int)buffer;
        c->dmabufs++;
        wayland.dmabufs++;
        wayland.pools++;
        wayland.buffers++;
        return buffer;
}

/*
        The buffer a params object describes, or why not: buffer index >= 0, -1 for the protocol error already queued,
        -2 for a buffer that cannot be made (async: failed; immediate: invalid_wl_buffer). The descriptors go to a pool of
        their own.
*/
/* Shared dma-buf/prime plane geometry validation: returns 0 OK, 1 incomplete, 2 dims, 3 format, 4 bounds. */
static int wayland_dmabuf_validate(unsigned long width, unsigned long height, unsigned long format,
                                   long plane_count, int linear, const unsigned long *offsets, const unsigned long *strides)
{
        if (plane_count < 1)
                return 1;
        if (width < 1 || height < 1 || width > WAYLAND_DIMENSION || height > WAYLAND_DIMENSION)
                return 2;
        if (!wayland_fourcc_supported((unsigned int)format))
                return 3;
        if (format == WAYLAND_FOURCC_NV12)
        {
                if (plane_count != 2 || (width & 1) || (height & 1))
                        return 1;
                if (offsets[0] > 0x7fffffffUL || offsets[1] > 0x7fffffffUL || strides[0] > (1UL << 20) || strides[1] > (1UL << 20) ||
                    strides[0] < width || strides[1] < width)
                        return 4;
                return 0;
        }
        if (linear && plane_count != 1)
                return 1;
        if (offsets[0] > 0x7fffffffUL || strides[0] == 0 || strides[0] > (1UL << 20) ||
            (linear && (strides[0] < width * 4 || (strides[0] & 3))))
                return 4;
        for (long k = 1; k < plane_count; k++)
                if (offsets[k] > 0x7fffffffUL || strides[k] > (1UL << 20))
                        return 4;
        return 0;
}

static long wayland_dmabuf_make(struct wayland_client *c, struct wayland_object *self, struct wayland_params *p, const struct wayland_arg *a,
                                long id_slot)
{
        unsigned long width = a[0].u, height = a[1].u, format = a[2].u, flags = a[3].u;
        long plane_count = 0;
        int linear, err;

        p->used = 1; // a params object used (and checked unused before, by the request) makes one buffer, made or not
        for (long k = 0; k < 4; k++)
        {
                if (p->fd[k] < 0)
                        continue;
                if (k != plane_count)
                        return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_incomplete, "planes must be contiguous from 0"), -1;
                plane_count++;
        }
        linear = p->modifier[0] == 0 || p->modifier[0] == WAYLAND_MODIFIER_INVALID;
        err = wayland_dmabuf_validate(width, height, format, plane_count, linear, p->offset, p->stride);
        if (err)
        {
                static const unsigned int codes[5] = {0, WLN_zwp_linux_buffer_params_v1_error_incomplete,
                                                      WLN_zwp_linux_buffer_params_v1_error_invalid_dimensions,
                                                      WLN_zwp_linux_buffer_params_v1_error_invalid_format,
                                                      WLN_zwp_linux_buffer_params_v1_error_out_of_bounds};
                static const char *const msgs[5] = {0, "incomplete planes", "the size is out of range",
                                                    "the format is not offered", "the plane does not fit its size"};

                return wayland_fail(c, self->id, codes[err], msgs[err]), -1;
        }
        for (long k = 1; k < plane_count; k++)
                if (p->modifier[k] != p->modifier[0])
                        return -2;
        return wayland_dmabuf_alloc_buffer(c, width, height, format, flags, plane_count, p->fd, p->offset, p->stride, p->modifier[0], id_slot);
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
                        long index = wayland_tbl_alloc(c, WLT_PARAMS);

                        if (index < 0)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for buffer params");
                        c->params[index].id = c->objects[a[0].slot].id;
                        c->params[index].live = 1;
                        for (long plane = 0; plane < 4; plane++)
                                c->params[index].fd[plane] = -1;
                        c->objects[a[0].slot].link = (int)index;
                }
                else if (op == WLQ_zwp_linux_dmabuf_v1_get_default_feedback || op == WLQ_zwp_linux_dmabuf_v1_get_surface_feedback)
                {
                        if (!wayland_tbl_reach(c, WLT_FEEDBACK, c->feedbacks + 1))
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the feedback");
                        c->feedback[c->feedbacks].need = 0;
                        c->feedback[c->feedbacks++].id = c->objects[a[0].slot].id;
                        wayland_feedback_send(c, a[0].slot);
                }
                return WAYLAND_OK;
        case WLI_zwp_linux_dmabuf_feedback_v1:
                for (long at = 0; at < c->feedbacks; at++)
                        if (c->feedback[at].id == self->id)
                        {
                                c->feedbacks--;
                                c->feedback[at].id = c->feedback[c->feedbacks].id;
                                c->feedback[at].need = c->feedback[c->feedbacks].need;
                                c->feedback[c->feedbacks].need = 0;
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
                        wayland_ev0(c, self->id, WLE_zwp_linux_buffer_params_v1_failed);
                        return WAYLAND_OK;
                }
                wayland_ev(c, self->id, WLE_zwp_linux_buffer_params_v1_created, (unsigned int)id);
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

static int wayland_drm_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (op == WLQ_wl_drm_authenticate)
        {
                wayland_ev0(c, self->id, WLE_wl_drm_authenticated);
                return WAYLAND_OK;
        }
        if (op == WLQ_wl_drm_create_prime_buffer)
        {
                long width = a[2].i, height = a[3].i;
                unsigned long format = a[4].u;
                long fds[4] = {a[1].fd, -1, -1, -1};
                unsigned long offsets[4] = {(unsigned long)a[5].i, (unsigned long)a[7].i, (unsigned long)a[9].i, 0};
                unsigned long strides[4] = {(unsigned long)a[6].i, (unsigned long)a[8].i, (unsigned long)a[10].i, 0};
                long plane_count = format == WAYLAND_FOURCC_NV12 ? 2 : 1;
                long made;

                if (wayland_dmabuf_validate((unsigned long)width, (unsigned long)height, format, plane_count, 1, offsets, strides))
                        return wayland_fail(c, self->id, WLN_wl_drm_error_invalid_format, "invalid prime buffer parameters");
                made = wayland_dmabuf_alloc_buffer(c, (unsigned long)width, (unsigned long)height, format, 0, plane_count, fds, offsets,
                                                   strides, WAYLAND_MODIFIER_INVALID, a[0].slot);
                a[1].fd = fds[0]; // -1 if taken by pool
                if (made < 0)
                        return wayland_fail(c, self->id, WLN_wl_drm_error_invalid_name, "cannot import prime buffer");
                return WAYLAND_OK;
        }
        return wayland_fail(c, self->id, WLN_wl_drm_error_invalid_name, "flink buffer names are not supported");
}

static int wayland_syncobj_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (self->iface == WLI_wp_linux_drm_syncobj_manager_v1)
        {
                if (op == WLQ_wp_linux_drm_syncobj_manager_v1_get_surface)
                        return wayland_ext_bind(c, self, op, a, 0);
                if (op == WLQ_wp_linux_drm_syncobj_manager_v1_import_timeline)
                {
                        long idx = wayland_tbl_alloc(c, WLT_TIMELINE), handle;

                        if (idx < 0)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a syncobj timeline");
                        handle = wayland.syncobj.import ? wayland.syncobj.import(wayland.syncobj.context, a[1].fd) : -1;
                        wayland.host->close(wayland.host->context, a[1].fd);
                        wayland.fds--;
                        a[1].fd = -1;
                        if (handle < 0)
                        {
                                wayland_tbl_free(c, WLT_TIMELINE, idx);
                                return wayland_fail(c, self->id, WLN_wp_linux_drm_syncobj_manager_v1_error_invalid_timeline,
                                                    "invalid drm syncobj timeline fd");
                        }
                        c->timelines[idx].live = 1;
                        c->timelines[idx].handle = handle;
                        c->objects[a[0].slot].link = (int)idx;
                        return WAYLAND_OK;
                }
                return WAYLAND_OK;
        }
        if (self->iface == WLI_wp_linux_drm_syncobj_timeline_v1)
        {
                long idx = self->link;

                if (idx >= 0 && idx < c->tbl[WLT_TIMELINE].count && c->timelines[idx].live)
                {
                        c->timelines[idx].gone = 1;
                        wayland_timeline_unref(c, idx);
                }
                return WAYLAND_OK;
        }
        s = wayland_surface_of(c, self - c->objects);
        if (op == WLQ_wp_linux_drm_syncobj_surface_v1_destroy)
        {
                if (s)
                {
                        s->syncobj_id = 0;
                        s->pending.sync_acq_set = s->pending.sync_rel_set = 0;
                }
                return WAYLAND_OK;
        }
        if (!s)
                return wayland_fail(c, self->id, WLN_wp_linux_drm_syncobj_surface_v1_error_no_surface, "the surface is gone");
        if (op == WLQ_wp_linux_drm_syncobj_surface_v1_set_acquire_point)
        {
                long tl = a[0].slot >= 0 ? c->objects[a[0].slot].link : -1;

                if (tl < 0 || tl >= c->tbl[WLT_TIMELINE].count || !c->timelines[tl].live)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "invalid syncobj timeline");
                s->pending.sync_acq_set = 1;
                s->pending.sync_acq_slot = (unsigned short)tl;
                s->pending.sync_acq_point = ((unsigned long long)a[1].u << 32) | a[2].u;
                return WAYLAND_OK;
        }
        if (op == WLQ_wp_linux_drm_syncobj_surface_v1_set_release_point)
        {
                long tl = a[0].slot >= 0 ? c->objects[a[0].slot].link : -1;

                if (tl < 0 || tl >= c->tbl[WLT_TIMELINE].count || !c->timelines[tl].live)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "invalid syncobj timeline");
                s->pending.sync_rel_set = 1;
                s->pending.sync_rel_slot = (unsigned short)tl;
                s->pending.sync_rel_point = ((unsigned long long)a[1].u << 32) | a[2].u;
                return WAYLAND_OK;
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

// Relative motion to the relative pointers of a client, in the surface's units: the motion with the gain and the hand's counts.
// How many it went to.
static long wayland_relative_send(struct wayland_client *c, long scale120, long dx, long dy, long udx, long udy, long micro)
{
        long sent = 0;
        const unsigned int words[6] = {(unsigned int)((unsigned long)micro >> 32), (unsigned int)micro, (unsigned int)wayland_fixed(dx, scale120),
                                       (unsigned int)wayland_fixed(dy, scale120), (unsigned int)wayland_fixed(udx, scale120),
                                       (unsigned int)wayland_fixed(udy, scale120)};

        for (long at = 0; at < c->relative_ids.count; at++)
        {
                long slot = wayland_slot_of(c, c->relative_ids.id[at]);

                if (slot < 0 || c->objects[slot].iface != WLI_zwp_relative_pointer_v1)
                        continue;
                wayland_emit(c, c->relative_ids.id[at], WLE_zwp_relative_pointer_v1_relative_motion, words, 6);
                sent++;
        }
        return sent;
}

// The counts kept for the frame of the pointer's next motion (wayland_pointer_relative), sent now: in that frame (frame 0, the
// motion's own closes it) or in one of their own. A client with no relative pointer is sent nothing, no empty frame either.
static void wayland_counts_send(int frame)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        long row = wayland.counts_row;

        wayland.counts_row = -1;
        if (row < 0 || !(s = wayland_row_surface(row, &c)) ||
            !wayland_relative_send(c, s->scale120, wayland.counts[0], wayland.counts[1], wayland.counts[2], wayland.counts[3],
                                   wayland.counts_micro) ||
            !frame)
                return;
        wayland_pointer_frames(c);
}

/*
        One report of the hand (dx, dy with the gain, udx, udy as the hand gave them, micro the device's microsecond), to the client
        whose window holds the pointer locked or confined, or with none held to the one the pointer is over. Locked, it is sent at
        once in a wl_pointer frame of its own: no place moves. Free or confined the pointer moves too, and the reports' counts are
        kept and go in the frame of the place's motion (wayland_pointer_motion), the reports read together summed as their places
        are; what no motion took (the pointer stopped at an edge) is sent in a frame of its own by wayland_pointer_counts_flush, as
        the host's pass ends. In one frame because Xwayland reads an X game's raw input from the relative motion of a frame and
        makes a raw event of the place itself when a frame has a place and no relative motion: an X client was sent the pointer's
        screen position as raw motion, once a report. A drag holds the pointer for itself.
*/
WAYLAND_API void wayland_pointer_relative(long row, long dx, long dy, long udx, long udy, long micro)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);

        if (!wayland.host || !s || row != (wayland.hold_mode ? wayland.hold_row : wayland.pointer_row) || wayland_dragging() ||
            !c->relative_ids.count)
                return;
        if (wayland.hold_mode != 1)
        {
                if (wayland.counts_row != row)
                {
                        wayland_counts_send(1);
                        wayland.counts[0] = wayland.counts[1] = wayland.counts[2] = wayland.counts[3] = 0;
                        wayland.counts_row = row;
                }
                wayland.counts[0] += dx, wayland.counts[1] += dy, wayland.counts[2] += udx, wayland.counts[3] += udy;
                wayland.counts_micro = micro;
                return;
        }
        if (wayland_relative_send(c, s->scale120, dx, dy, udx, udy, micro))
                wayland_pointer_frames(c);
}

// The counts no motion took, in a frame of their own (the host's pass ends with it).
WAYLAND_API void wayland_pointer_counts_flush(void)
{
        if (wayland.host)
                wayland_counts_send(1);
}

/*
        What the compositor's input thread needs to send a locked pointer's report itself, as wayland_pointer_relative would send it: the
        ids of row's client's relative pointers (each a relative_motion) and of its pointers that take a frame (version 5 on, a frame each
        after them), and its surface's scale; the client's slot and one, or 0 when the report would not be sent so (no host, no surface, the
        pointer not held locked on row, a drag, no relative pointer, more ids than room). Asked by the loop, whose state this reads.
*/
WAYLAND_API long wayland_pointer_direct(long row, unsigned int *relative, long *relatives, unsigned int *frames, long *framed, long room,
                                        long *scale120)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        const struct wayland_ids *ids;

        *relatives = *framed = 0;
        if (!wayland.host || !s || wayland.hold_mode != 1 || row != wayland.hold_row || wayland_dragging() || !c->relative_ids.count)
                return 0;
        for (long at = 0; at < c->relative_ids.count; at++)
        {
                long slot = wayland_slot_of(c, c->relative_ids.id[at]);

                if (slot < 0 || c->objects[slot].iface != WLI_zwp_relative_pointer_v1)
                        continue;
                if (*relatives == room)
                        return 0;
                relative[(*relatives)++] = (unsigned int)c->relative_ids.id[at];
        }
        ids = &c->seat_ids[0];
        for (long at = 0; at < ids->count; at++)
        {
                long slot = wayland_slot_of(c, ids->id[at]);

                if (slot < 0 || c->objects[slot].iface != WLI_wl_pointer || c->objects[slot].version < 5)
                        continue;
                if (*framed == room)
                        return 0;
                frames[(*framed)++] = (unsigned int)ids->id[at];
        }
        *scale120 = s->scale120;
        return *relatives ? c->slot + 1 : 0;
}

/*
        What the input thread needs to send a key to the window that has the keys itself, as wayland_key would send it: the ids of row's
        client's keyboards and where its serials are counted (taken there atomically, as wayland_serial takes them); the client's slot and
        one, or 0 when a key would not be sent so (no host, row not the keyboard's, no surface, a drag, no keyboard, more than room).
*/
WAYLAND_API long wayland_keyboard_direct(long row, unsigned int *ids, long *count, long room, unsigned long **serial)
{
        struct wayland_client *c;
        long made = 0;

        *count = 0;
        if (!wayland.host || row != wayland.keyboard_row || !wayland_row_surface(row, &c) || wayland_dragging())
                return 0;
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_keyboard, &at)) >= 0;)
        {
                if (made >= room)
                        return 0;
                ids[made++] = (unsigned int)c->objects[slot].id;
        }
        if (!made)
                return 0;
        *count = made;
        *serial = &c->serial;
        return c->slot + 1;
}

// A key the input thread sent itself (wayland_keyboard_direct), kept as wayland_key keeps one: the keys down, and its serial among the
// client's input serials (a selection or a popup names one).
WAYLAND_API void wayland_key_noted(long row, long code, long pressed, unsigned long serial)
{
        struct wayland_client *c;

        if (!wayland.host || code < 1 || code > 0x2ff)
                return;
        if (code < 128)
        {
                if (pressed)
                        wayland.keys[code >> 3] |= (unsigned char)(1u << (code & 7));
                else
                        wayland.keys[code >> 3] &= (unsigned char)~(1u << (code & 7));
        }
        if (row != wayland.keyboard_row || !wayland_row_surface(row, &c))
                return;
        c->input_serial[c->input_next++ % WAYLAND_INPUT_SERIALS] = (unsigned int)serial;
}

// A count of the hand's in the wire's fixed point at a surface's scale (wayland_fixed's), for a sender that is not the loop.
WAYLAND_API unsigned long wayland_fixed_of(long pixels, long scale120)
{
        return wayland_units_fixed(pixels, scale120);
}

static void wayland_constraint_event(struct wayland_client *c, struct wayland_constraint *con, int on)
{
        if (wayland_slot_of(c, con->id) < 0)
                return;
        wayland_ev0(c, con->id, con->kind == 1
                                    ? (on ? WLE_zwp_locked_pointer_v1_locked : WLE_zwp_locked_pointer_v1_unlocked)
                                    : (on ? WLE_zwp_confined_pointer_v1_confined : WLE_zwp_confined_pointer_v1_unconfined));
}

// A constraint let go of: it says so (unless its object is going), and a one-shot one is over.
static void wayland_constraint_release(struct wayland_client *c, struct wayland_constraint *con, int say)
{
        struct wayland_surface *s = con->surface ? &c->surfaces[con->surface - 1] : 0;

        // A lock that held, let go of with a hint on its window's own surface: where in the window's picture the client last drew
        // the pointer, for the host to show it there (wayland_constraints_update tells it as the hold ends).
        if (con->active && con->kind == 1 && con->hinted && s && s->live && wayland_root_of(c, s) == s && s->row >= 0)
        {
                long wx, wy, ww, wh;

                wayland_window_rect(c, s, s->width, s->height, s->scale120, &wx, &wy, &ww, &wh);
                wayland.hint_row = s->row;
                wayland.hint_x = wayland_pixels_floor(con->hint_x / 256, s->scale120) - wx;
                wayland.hint_y = wayland_pixels_floor(con->hint_y / 256, s->scale120) - wy;
        }
        if (con->active && say)
                wayland_constraint_event(c, con, 0);
        con->active = 0;
        if (con->oneshot || !say)
        {
                if (con->live)
                {
                        con->live = 0;
                        wayland.constraints--;
                        wayland_tbl_free(c, WLT_CONSTRAINT, con - c->constraints);
                }
        }
}

/*
        Which constraint holds the pointer: the first, of the clients in order, whose window is under the pointer and has the
        keys, and is not the window the host holds constraints off (wayland_unhold_row). The others that held it are let go, the
        one that qualifies is told it holds, and the host is told what is held.
*/
static void wayland_constraints_update(void)
{
        long client = -1, chosen = -1, row = -1, mode = 0;

        if (!wayland.host || (!wayland.constraints && !wayland.hold_mode))
                return;
        for (long slot = 0; slot < wayland.client_slots && chosen < 0; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                for (long at = 0; c && at < c->tbl[WLT_CONSTRAINT].count && chosen < 0; at++)
                {
                        struct wayland_constraint *con = &c->constraints[at];
                        struct wayland_surface *surface = con->live && con->surface ? &c->surfaces[con->surface - 1] : 0;
                        struct wayland_surface *root = surface && surface->live ? wayland_root_of(c, surface) : 0;

                        if (root && root->row >= 0 && root->row == wayland.pointer_row &&
                            (root->row == wayland.keyboard_row ||
                             ((root->role == WAYLAND_ROLE_POPUP || (root->xsurface && root->u.xwin.popup)) &&
                              wayland.keyboard_row >= 0 && wayland.row[wayland.keyboard_row].client == slot)) &&
                            root->row != wayland.unheld_row)
                        {
                                client = slot;
                                chosen = at;
                                row = root->row;
                                mode = con->kind;
                        }
                }
        }
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                for (long at = 0; c && at < c->tbl[WLT_CONSTRAINT].count; at++)
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

// The host holds the pointer's constraints off a window (row), or none (-1): a lock or a confinement there is let go of now and
// taken again, as the protocol's persistent ones are, once the window is not held off.
WAYLAND_API void wayland_unhold_row(long row)
{
        if (row == wayland.unheld_row)
                return;
        wayland.unheld_row = row;
        wayland_constraints_update();
}

// A surface that is going: its constraints go, and what they held is let go.
static void wayland_constraints_drop(struct wayland_client *c, long surface)
{
        for (long at = 0; at < c->tbl[WLT_CONSTRAINT].count; at++)
                if (c->constraints[at].live && c->constraints[at].surface == surface + 1)
                        wayland_constraint_release(c, &c->constraints[at], 0);
        wayland_constraints_update();
}

/*
        wp_presentation.feedback (surface, new id): the feedback is answered by the commit that follows it, when its frame is
        drawn (wayland_presented). Arguments in the order the XML has them: the surface first, then the new object. A feedback
        the memory has no room for is discarded at once: the protocol's answer for a frame that is not timed.
*/
static int wayland_presentation_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (op != WLQ_wp_presentation_feedback)
                return WAYLAND_OK;
        s = wayland_surface_of(c, a[0].slot);
        if (!s)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
        if (!wayland_cb_push(c, &s->cbq[WLCB_FEEDBACK], c->objects[a[1].slot].id))
                wayland_feedback_discard(c, c->objects[a[1].slot].id);
        return WAYLAND_OK;
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
        struct wayland_surface *s = wayland_surface_of(c, toplevel);

        if (icon >= 0)
                c->objects[icon].flags |= WAYLAND_ICON_SET;
        if (!s || s->role_id != c->objects[toplevel].id)
                return;
        s->pending.icon = icon >= 0 && (c->objects[icon].flags & WAYLAND_ICON_CONTENT) ? 1 : 0;
        if (s->pending.icon)
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
        case WLI_wp_content_type_manager_v1:
        case WLI_wp_alpha_modifier_v1:
        case WLI_wp_fifo_manager_v1:
        case WLI_wp_commit_timing_manager_v1:
                return wayland_ext_bind(c, self, op, a, 0);
        case WLI_xdg_dialog_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_xdg_dialog_v1_set_modal && s)
                        s->modal = 1;
                else if ((op == WLQ_xdg_dialog_v1_unset_modal || op == WLQ_xdg_dialog_v1_destroy) && s)
                        s->modal = 0;
                return WAYLAND_OK;
        case WLI_wp_content_type_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_content_type_v1_set_content_type && s)
                        s->content = a[0].u <= WLN_wp_content_type_v1_type_game ? (unsigned int)a[0].u : WLN_wp_content_type_v1_type_none;
                else if (op == WLQ_wp_content_type_v1_destroy && s)
                        s->content = WLN_wp_content_type_v1_type_none;
                return WAYLAND_OK;
        case WLI_wp_alpha_modifier_surface_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_alpha_modifier_surface_v1_set_multiplier && s)
                        s->pending.alpha = (unsigned int)a[0].u;
                else if (op == WLQ_wp_alpha_modifier_surface_v1_destroy && s)
                        s->pending.alpha = 0xffffffffu;
                return WAYLAND_OK;
        case WLI_wp_fifo_v1:
                // The barrier is the next commit's (wait_barrier); set_barrier is the one the compositor's latch releases.
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_fifo_v1_wait_barrier && s)
                        s->pending.fifo_wait = 1;
                else if (op == WLQ_wp_fifo_v1_set_barrier && s)
                        s->pending.barrier = 1;
                return WAYLAND_OK;
        case WLI_wp_commit_timer_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op != WLQ_wp_commit_timer_v1_set_timestamp || !s)
                        return WAYLAND_OK;
                {
                        unsigned long seconds = ((unsigned long)a[0].u << 32) | (unsigned long)a[1].u;

                        if (s->pending.timestamp)
                                return wayland_fail(c, self->id, WLN_wp_commit_timer_v1_error_timestamp_exists, "the commit has a time already");
                        if (a[2].u >= 1000000000ul || seconds > 18000000000ul)
                                return wayland_fail(c, self->id, WLN_wp_commit_timer_v1_error_invalid_timestamp, "the timestamp is not a time");
                        s->pending.timestamp = seconds * 1000000000ul + (unsigned long)a[2].u;
                        if (!s->pending.timestamp)
                                s->pending.timestamp = 1;
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

        for (long at = 0; at < wayland.tokens; at++)
                if (wayland.token[at].live && wayland.token[at].committed && now - wayland.token[at].when >= WAYLAND_TOKEN_MS)
                        wayland.token[at].live = 0;
        for (long at = 0; at < wayland.tokens; at++)
                if (!wayland.token[at].live)
                {
                        wayland.token[at].gen++;
                        return (int)at;
                }
        // None free: the table grown by one (-1 when the memory is not there).
        wayland.token_table.unit = sizeof(struct wayland_token);
        if (wayland.tokens >= 0x7fffffffL || !wayland_table_reach(&wayland.token_table, wayland.tokens + 1))
                return -1;
        wayland.token = (struct wayland_token *)wayland.token_table.at;
        wayland.token[wayland.tokens].live = 0;
        wayland.token[wayland.tokens].gen = 0;
        return (int)wayland.tokens++;
}

static int wayland_token_make(struct wayland_token *t)
{
        unsigned char bytes[16];

        if (!wayland.host->random(wayland.host->context, bytes, (long)sizeof bytes))
                return 0;
        memory_into_hex(t->text, bytes, 16);
        t->text[32] = 0;
        return 1;
}

static void wayland_token_forget(long slot)
{
        long live = 0;

        for (long at = 0; at < wayland.tokens; at++)
        {
                if (wayland.token[at].live && wayland.token[at].client == slot)
                        wayland.token[at].live = 0;
                live += wayland.token[at].live;
        }
        // The last token gone with its client: the table let go too, as the rows' tables go with the last window.
        if (!live && wayland.tokens)
        {
                wayland_table_drop(&wayland.token_table);
                wayland.token = 0, wayland.tokens = 0;
        }
}

static int wayland_activation_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        int at = self->link;
        // The object's token is the one it was given: the slot's live token is another's once the first is used and the slot given again.
        struct wayland_token *t = at >= 0 && at < wayland.tokens && wayland.token[at].live && wayland.token[at].client == c->slot &&
                                          wayland.token[at].gen == self->aux
                                      ? &wayland.token[at] : 0;

        if (self->iface == WLI_xdg_activation_v1)
        {
                struct wayland_surface *s;
                long now;

                if (op == WLQ_xdg_activation_v1_get_activation_token)
                {
                        at = wayland_token_free();
                        if (at < 0)
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for an activation token");
                        wayland.token[at].live = 1;
                        wayland.token[at].committed = 0;
                        wayland.token[at].client = c->slot;
                        wayland.token[at].serial = 0;
                        wayland.token[at].text[0] = 0;
                        c->objects[a[0].slot].link = (int)at;
                        c->objects[a[0].slot].aux = wayland.token[at].gen;
                        return WAYLAND_OK;
                }
                if (op != WLQ_xdg_activation_v1_activate)
                        return WAYLAND_OK;
                s = wayland_surface_of(c, a[1].slot);
                if (!s)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                for (at = 0; at < wayland.tokens; at++)
                        if (wayland.token[at].live && wayland.token[at].committed &&
                            wayland_name_is(a[0].text, a[0].length, wayland.token[at].text))
                                break;
                if (at == wayland.tokens)
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
                        if (!wayland_token_make(t))
                        {
                                t->live = 0;
                                return WAYLAND_OK;
                        }
                        t->committed = 1;
                        t->when = wayland.host->now(wayland.host->context);
                        wayland_ev_str(c, self->id, WLE_xdg_activation_token_v1_done, t->text);
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
        int status;

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
                        wayland_surface_link(c, a[0].slot, surface);
                        // The client whose surfaces are X windows is the X server: its outputs are told again in X's space.
                        wayland_xwayland_client(c->slot);
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
                        if (surface->u.xwin.serial_set)
                                return wayland_fail(c, self->id, WLN_xwayland_surface_v1_error_already_associated,
                                                    "the surface has a serial");
                        if (!serial)
                                return wayland_fail(c, self->id, WLN_xwayland_surface_v1_error_invalid_serial, "the serial is zero");
                        surface->u.xwin.serial = serial;
                        surface->u.xwin.serial_set = 1;
                }
                return WAYLAND_OK;
        }
        if (self->iface == WLI_zwp_xwayland_keyboard_grab_manager_v1)
        {
                status = wayland_ext_bind(c, self, op, a, &surface);
                if (status == WAYLAND_OK && surface)
                {
                        surface->xgrab = 1;
                        if (surface->row >= 0 && surface->row == wayland.keyboard_row && !surface->shortcut_active)
                        {
                                surface->shortcut_active = 1;
                                surface->escape_down = 0;
                        }
                }
                return status;
        }
        if (self->iface == WLI_zwp_xwayland_keyboard_grab_v1)
        {
                surface = wayland_surface_of(c, self - c->objects);
                if (surface && op == WLQ_zwp_xwayland_keyboard_grab_v1_destroy)
                {
                        surface->xgrab = 0;
                        if (!surface->shortcuts)
                        {
                                surface->shortcut_active = 0;
                                surface->escape_down = 0;
                        }
                }
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
                        if (!wayland_ids_add(&c->relative_ids, c->objects[a[0].slot].id))
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a relative pointer");
                }
                return WAYLAND_OK;
        case WLI_zwp_relative_pointer_v1:
                wayland_ids_remove(&c->relative_ids, self->id);
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
                for (long at = 0; at < c->tbl[WLT_CONSTRAINT].count; at++)
                        if (c->constraints[at].live && c->constraints[at].surface == wayland_surface_index(c, surface) + 1)
                                return wayland_fail(c, self->id, WLN_zwp_pointer_constraints_v1_error_already_constrained,
                                                    "the surface is constrained already");
                index = wayland_tbl_alloc(c, WLT_CONSTRAINT);
                if (index < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a pointer constraint");
                c->constraints[index].id = c->objects[a[0].slot].id;
                c->constraints[index].live = 1;
                c->constraints[index].kind = op == WLQ_zwp_pointer_constraints_v1_lock_pointer ? 1 : 2;
                c->constraints[index].oneshot = a[4].u == WLN_zwp_pointer_constraints_v1_lifetime_oneshot;
                c->constraints[index].surface = (unsigned int)(wayland_surface_index(c, surface) + 1);
                c->objects[a[0].slot].link = (int)index;
                wayland.constraints++;
                wayland_constraints_update();
                return WAYLAND_OK;
        }
        case WLI_zwp_locked_pointer_v1:
        case WLI_zwp_confined_pointer_v1:
                // set_region is taken: the whole window is what a hold is. A lock's set_cursor_position_hint is kept (at once, not at
                // the surface's next commit, which the protocol allows the hint to wait for): where the client has drawn the pointer
                // it holds, and where the pointer is shown when the lock lets it go (wayland_constraint_release).
                // The hint is quiet (wayland.quiet): it is read only when the lock lets go, which is not a quiet request.
                if (self->iface == WLI_zwp_locked_pointer_v1 && op == WLQ_zwp_locked_pointer_v1_set_cursor_position_hint)
                        wayland.quiet++;
                if (self->iface == WLI_zwp_locked_pointer_v1 && op == WLQ_zwp_locked_pointer_v1_set_cursor_position_hint &&
                    self->link >= 0 && self->link < c->tbl[WLT_CONSTRAINT].count && c->constraints[self->link].live &&
                    c->constraints[self->link].id == self->id)
                {
                        c->constraints[self->link].hinted = 1;
                        c->constraints[self->link].hint_x = (int)a[0].i;
                        c->constraints[self->link].hint_y = (int)a[1].i;
                }
                if (op == WLQ_zwp_locked_pointer_v1_destroy)
                {
                        struct wayland_constraint *con = self->link >= 0 && self->link < c->tbl[WLT_CONSTRAINT].count ? &c->constraints[self->link] : 0;

                        if (con && con->live && con->id == self->id)
                        {
                                wayland_constraint_release(c, con, 0);
                                wayland_constraints_update();
                        }
                        self->link = -1;
                }
                return WAYLAND_OK;
        case WLI_zwp_keyboard_shortcuts_inhibit_manager_v1:
        {
                int status = wayland_ext_bind(c, self, op, a, &surface);

                if (status == WAYLAND_OK && surface)
                {
                        surface->shortcut_id = c->objects[a[0].slot].id;
                        if (surface->row >= 0 && surface->row == wayland.keyboard_row)
                        {
                                surface->shortcut_active = 1;
                                wayland_ev0(c, surface->shortcut_id, WLE_zwp_keyboard_shortcuts_inhibitor_v1_active);
                        }
                }
                return status;
        }
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
                return wayland_ext_bind(c, self, op, a, 0);
        case WLI_wp_tearing_control_v1:
                surface = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_tearing_control_v1_destroy)
                {
                        if (surface)
                                surface->tearing = 0;
                        if (surface)
                                surface->pending.async_hint = 0;
                }
                else if (a[0].u > WLN_wp_tearing_control_v1_presentation_hint_async)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "no such presentation hint");
                else if (surface)
                        surface->pending.async_hint = a[0].u == WLN_wp_tearing_control_v1_presentation_hint_async;
                return WAYLAND_OK;
        case WLI_zwp_idle_inhibit_manager_v1:
        {
                int status = wayland_ext_bind(c, self, op, a, &surface);

                if (status == WAYLAND_OK && surface)
                        c->inhibitors++;
                return status;
        }
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

        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];
                long seen = 0;

                if (!c || c->dead || !c->inhibitors)
                        continue;
                // c->inhibitors counts the zwp_idle_inhibitor_v1 objects the client holds (wayland_object_free takes one back when it
                // goes), so the scan ends at the last of them: a turn of the loop asks this on every pass while a video plays.
                for (long at = 0; at < c->tbl[WLT_OBJECT].count && seen < c->inhibitors; at++)
                        if (c->objects[at].iface == WLI_zwp_idle_inhibitor_v1 && c->objects[at].id)
                        {
                                seen++;
                                if (wayland_surface_shown(c, wayland_surface_of(c, at)))
                                        count++;
                        }
        }
        return count;
}

static void wayland_notify_say(struct wayland_client *c, struct wayland_notify *n, long opcode)
{
        wayland_ev0(c, n->id, opcode);
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
                at = wayland_tbl_alloc(c, WLT_NOTIFY);
                if (at < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for an idle notification");
                n = &c->notifies[at];
                n->live = 1;
                n->input_only = op == WLQ_ext_idle_notifier_v1_get_input_idle_notification;
                n->id = c->objects[a[0].slot].id;
                n->timeout = (long)a[1].u;
                n->since = wayland.host->now(wayland.host->context);
                c->objects[a[0].slot].link = (int)at;
                wayland.idle_notifies++;
                return WAYLAND_OK;
        }
        if (op == WLQ_ext_idle_notification_v1_destroy)
                wayland_notify_free(c, self->link), self->link = -1;
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
// How each kind of argument in the generated table is read off the wire (wire_arguments' classes): a descriptor takes no bytes, a number,
// a fixed, an object and a new id are a word, and a string, a string that may be none, a global's name with its version and id, and an
// array are a count and its bytes.
static const unsigned char wayland_kinds[256] = {
    ['h'] = WIRE_NONE,
    ['i'] = WIRE_WORD,
    ['f'] = WIRE_WORD,
    ['u'] = WIRE_WORD,
    ['o'] = WIRE_WORD,
    ['O'] = WIRE_WORD,
    ['n'] = WIRE_WORD,
    ['s'] = WIRE_TEXT | WIRE_ENDED,
    ['S'] = WIRE_TEXT | WIRE_ENDED | WIRE_EMPTY,
    ['N'] = WIRE_TEXT | WIRE_ENDED | WIRE_PAIR,
    ['a'] = WIRE_TEXT | WIRE_EMPTY,
};

/*
        A request's arguments. The library walks the bytes by the table's list of kinds (wire_arguments): how many arguments are
        there whole, a record of each, and why the walk stopped. What an argument means is then taken here in the order they came
        (a descriptor from the ones sent, an object looked up and its interface asked, a new id made), each failing as it did when
        the bytes were read here an argument at a time; and only when every argument before it has been taken is the request
        refused for what the walk found wrong with its bytes, so the first thing wrong is still the thing said.
*/
static int wayland_args(struct wayland_client *c, const struct wayland_object *self, const struct wl_message *msg,
                        const unsigned char *at, long left, struct wayland_arg *a, long *begun, int *took_fd)
{
        unsigned int records[WAYLAND_ARGS][4];
        unsigned long long walked = wire_arguments(msg->args, wayland_kinds, at, (unsigned long long)left, &records[0][0], WAYLAND_ARGS);
        long whole = (long)(walked & 255), why = (long)(walked >> 8);
        // The argument the walk stopped in is begun as the ones before it are (initialized, and counted for the caller), unless it
        // stopped before one: with no room for it, or past the last.
        long count = whole + (why != WIRE_END_DONE && why != WIRE_END_ROOM && why != WIRE_END_OVER);

        *begun = 0;
        for (long n = 0; n < count; n++)
        {
                const unsigned char *kind = msg->args + 2 * n;
                struct wayland_arg *arg = &a[n];
                unsigned long word = records[n][0], length;

                *begun = n + 1; // every argument begun is initialized below, so the caller reads no further than this
                arg->u = 0;
                arg->i = 0;
                arg->text = 0;
                arg->length = 0;
                arg->slot = -1;
                arg->fd = -1;
                arg->version = 0;
                if (n == whole)
                        break;
                switch (kind[0])
                {
                case 'h':
                        if (c->in_fd_count < 1)
                                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a descriptor is missing");
                        arg->fd = c->in_fds[0];
                        *took_fd = 1;
                        for (long more = 1; more < c->in_fd_count; more++)
                                c->in_fds[more - 1] = c->in_fds[more];
                        c->in_fd_count--;
                        break;
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
                default: // a string, a string that may be none, a global's name, an array: the count, and where its bytes are
                        length = word;
                        if (!length && kind[0] == 'S')
                                break;
                        arg->text = at + records[n][1];
                        arg->length = kind[0] == 'a' ? (long)length : (long)length - 1;
                        if (kind[0] == 'N')
                        {
                                arg->version = (long)records[n][2];
                                arg->u = records[n][3];
                        }
                        break;
                }
        }
        switch (why)
        {
        case WIRE_END_DONE:
                return WAYLAND_OK;
        case WIRE_END_ROOM:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "too many arguments");
        case WIRE_END_SHORT:
        case WIRE_END_PAIR:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the arguments are short");
        case WIRE_END_NOTHING:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a string has no length");
        case WIRE_END_LONG:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a string is longer than its message");
        case WIRE_END_OPEN:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a string is not ended");
        case WIRE_END_OVER:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the arguments are longer than the request");
        default:
                return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the table has an argument that cannot be read");
        }
}

static int wayland_layer_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (self->iface == WLI_zwlr_layer_shell_v1)
        {
                struct wayland_surface *s;
                long out = 0;

                if (op == WLQ_zwlr_layer_shell_v1_destroy)
                        return WAYLAND_OK;
                s = &c->surfaces[c->objects[a[1].slot].link];
                if (s->role != WAYLAND_ROLE_NONE || s->xsurface)
                        return wayland_fail(c, self->id, WLN_zwlr_layer_shell_v1_error_already_constructed, "the surface has a role");
                if (a[3].u > 3)
                        return wayland_fail(c, self->id, WLN_zwlr_layer_shell_v1_error_invalid_layer, "unknown layer");
                if (a[2].slot >= 0 && c->objects[a[2].slot].iface == WLI_wl_output && c->objects[a[2].slot].aux < WAYLAND_SCREENS)
                        out = (long)c->objects[a[2].slot].aux;
                s->role = WAYLAND_ROLE_TOPLEVEL;
                s->role_id = c->objects[a[0].slot].id;
                s->u.top.layer_shell = 1;
                s->u.top.layer_level = (long)a[3].u;
                s->u.top.layer_output = out >= 0 ? out : 0;
                s->u.top.layer_w = s->u.top.layer_h = 0;
                s->u.top.layer_configured = 0;
                c->objects[a[0].slot].link = c->objects[a[1].slot].link;
                return WAYLAND_OK;
        }
        if (op == WLQ_zwlr_layer_surface_v1_destroy)
        {
                struct wayland_surface *s = &c->surfaces[self->link];

                if (s->live && s->role_id == self->id)
                {
                        wayland_unhold(c, s);
                        wayland_row_release(c, s);
                        s->role = WAYLAND_ROLE_NONE;
                        s->role_id = 0;
                        s->u.top.layer_shell = 0;
                        s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
                }
                return WAYLAND_OK;
        }
        {
                struct wayland_surface *s = &c->surfaces[self->link];

                if (!s->live || s->role_id != self->id)
                        return WAYLAND_OK;
                if (op == WLQ_zwlr_layer_surface_v1_set_size)
                        s->u.top.layer_w = (long)a[0].u, s->u.top.layer_h = (long)a[1].u;
                else if (op == WLQ_zwlr_layer_surface_v1_set_layer)
                {
                        if (a[0].u > 3)
                                return wayland_fail(c, self->id, WLN_zwlr_layer_shell_v1_error_invalid_layer, "unknown layer");
                        s->u.top.layer_level = (long)a[0].u;
                        if (s->row >= 0)
                                wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_LAYER,
                                                     s->u.top.layer_level <= 1 ? -1 : 2, s->u.top.layer_output);
                }
                else if (op == WLQ_zwlr_layer_surface_v1_ack_configure)
                        s->flags |= WAYLAND_SURFACE_CONFIGURED;
        }
        return WAYLAND_OK;
}

typedef int (*wayland_request_fn)(struct wayland_client *, struct wayland_object *, long, struct wayland_arg *);

// Unlisted interfaces (outputs, xdg outputs, etc.) have only release/destroy and return WAYLAND_OK.
static const wayland_request_fn wayland_handlers[WLI_COUNT] = {
        [WLI_wl_display] = wayland_display_request,
        [WLI_wl_registry] = wayland_registry_request,
        [WLI_wl_compositor] = wayland_compositor_request,
        [WLI_wl_surface] = wayland_surface_request,
        [WLI_wl_region] = wayland_region_request,
        [WLI_wl_shm] = wayland_shm_request,
        [WLI_wl_shm_pool] = wayland_pool_request,
        [WLI_wl_buffer] = wayland_buffer_request,
        [WLI_wl_subcompositor] = wayland_subcompositor_request,
        [WLI_wl_subsurface] = wayland_subsurface_request,
        [WLI_wl_seat] = wayland_seat_request,
        [WLI_wl_pointer] = wayland_seat_object_request,
        [WLI_wl_keyboard] = wayland_seat_object_request,
        [WLI_wl_touch] = wayland_seat_object_request,
        [WLI_xdg_wm_base] = wayland_wm_base_request,
        [WLI_xdg_positioner] = wayland_positioner_request,
        [WLI_xdg_surface] = wayland_xdg_surface_request,
        [WLI_xdg_toplevel] = wayland_toplevel_request,
        [WLI_xdg_popup] = wayland_popup_request,
        [WLI_zxdg_output_manager_v1] = wayland_output_manager_request,
        [WLI_zwp_relative_pointer_manager_v1] = wayland_games_request,
        [WLI_zwp_relative_pointer_v1] = wayland_games_request,
        [WLI_zwp_pointer_constraints_v1] = wayland_games_request,
        [WLI_zwp_locked_pointer_v1] = wayland_games_request,
        [WLI_zwp_confined_pointer_v1] = wayland_games_request,
        [WLI_zwp_idle_inhibit_manager_v1] = wayland_games_request,
        [WLI_zwp_idle_inhibitor_v1] = wayland_games_request,
        [WLI_zwp_keyboard_shortcuts_inhibit_manager_v1] = wayland_games_request,
        [WLI_zwp_keyboard_shortcuts_inhibitor_v1] = wayland_games_request,
        [WLI_wp_tearing_control_manager_v1] = wayland_games_request,
        [WLI_wp_tearing_control_v1] = wayland_games_request,
        [WLI_xwayland_shell_v1] = wayland_xwayland_request,
        [WLI_xwayland_surface_v1] = wayland_xwayland_request,
        [WLI_zwp_xwayland_keyboard_grab_manager_v1] = wayland_xwayland_request,
        [WLI_zwp_xwayland_keyboard_grab_v1] = wayland_xwayland_request,
        [WLI_xdg_activation_v1] = wayland_activation_request,
        [WLI_xdg_activation_token_v1] = wayland_activation_request,
        [WLI_zwp_pointer_gestures_v1] = wayland_gestures_request,
        [WLI_zwp_pointer_gesture_swipe_v1] = wayland_gestures_request,
        [WLI_zwp_pointer_gesture_pinch_v1] = wayland_gestures_request,
        [WLI_zwp_pointer_gesture_hold_v1] = wayland_gestures_request,
        [WLI_ext_idle_notifier_v1] = wayland_idle_request,
        [WLI_ext_idle_notification_v1] = wayland_idle_request,
        [WLI_wp_fifo_manager_v1] = wayland_hints_request,
        [WLI_wp_fifo_v1] = wayland_hints_request,
        [WLI_wp_commit_timing_manager_v1] = wayland_hints_request,
        [WLI_wp_commit_timer_v1] = wayland_hints_request,
        [WLI_xdg_wm_dialog_v1] = wayland_hints_request,
        [WLI_xdg_dialog_v1] = wayland_hints_request,
        [WLI_xdg_system_bell_v1] = wayland_hints_request,
        [WLI_xdg_toplevel_icon_manager_v1] = wayland_hints_request,
        [WLI_xdg_toplevel_icon_v1] = wayland_hints_request,
        [WLI_wp_content_type_manager_v1] = wayland_hints_request,
        [WLI_wp_content_type_v1] = wayland_hints_request,
        [WLI_wp_alpha_modifier_v1] = wayland_hints_request,
        [WLI_wp_alpha_modifier_surface_v1] = wayland_hints_request,
        [WLI_zwp_linux_dmabuf_v1] = wayland_dmabuf_request,
        [WLI_zwp_linux_buffer_params_v1] = wayland_dmabuf_request,
        [WLI_zwp_linux_dmabuf_feedback_v1] = wayland_dmabuf_request,
        [WLI_wl_drm] = wayland_drm_request,
        [WLI_wp_linux_drm_syncobj_manager_v1] = wayland_syncobj_request,
        [WLI_wp_linux_drm_syncobj_timeline_v1] = wayland_syncobj_request,
        [WLI_wp_linux_drm_syncobj_surface_v1] = wayland_syncobj_request,
        [WLI_wp_cursor_shape_manager_v1] = wayland_cursor_shape_request,
        [WLI_wp_cursor_shape_device_v1] = wayland_cursor_shape_request,
        [WLI_zwp_text_input_manager_v3] = wayland_text_request,
        [WLI_zwp_text_input_v3] = wayland_text_request,
        [WLI_wp_presentation] = wayland_presentation_request,
        [WLI_wp_viewporter] = wayland_viewport_request,
        [WLI_wp_viewport] = wayland_viewport_request,
        [WLI_wp_fractional_scale_manager_v1] = wayland_fractional_request,
        [WLI_wp_fractional_scale_v1] = wayland_fractional_request,
        [WLI_zwlr_layer_shell_v1] = wayland_layer_request,
        [WLI_zwlr_layer_surface_v1] = wayland_layer_request,
        [WLI_wl_data_device_manager] = wayland_data_request,
        [WLI_wl_data_device] = wayland_data_request,
        [WLI_wl_data_source] = wayland_data_request,
        [WLI_wl_data_offer] = wayland_data_request,
        [WLI_zwp_primary_selection_device_manager_v1] = wayland_data_request,
        [WLI_zwp_primary_selection_device_v1] = wayland_data_request,
        [WLI_zwp_primary_selection_source_v1] = wayland_data_request,
        [WLI_zwp_primary_selection_offer_v1] = wayland_data_request,
};

static int wayland_dispatch(struct wayland_client *c, const unsigned char *message, long size, unsigned long id, long op)
{
        long slot = wayland_slot_of(c, id), n;
        const struct wl_interface_info *info;
        const struct wl_message *msg;
        struct wayland_object *o;
        struct wayland_arg a[WAYLAND_ARGS];
        long begun = 0;
        int status, took_fd = 0;

        wayland.heard++;
        if (slot < 0)
                return wayland_fail(c, id, WAYLAND_INVALID_OBJECT, "no such object");
        o = &c->objects[slot];
        info = &wl_interfaces[o->iface];
        if (op >= info->requests || info->request[op].since > o->version)
                return wayland_fail(c, id, WAYLAND_INVALID_METHOD, "no such request");
        msg = &info->request[op];
        status = wayland_args(c, o, msg, message + 8, size - 8, a, &begun, &took_fd);
        if (status >= 0)
        {
                wayland_request_fn handler = o->iface < WLI_COUNT ? wayland_handlers[o->iface] : 0;

                status = handler ? handler(c, o, op, a) : WAYLAND_OK;
        }
        // A descriptor a handler did not take is closed here, whatever became of the request (a request that took none has none to look
        // for: every request walked its arguments for one).
        for (n = 0; took_fd && n < begun; n++)
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
        if (wayland.host || !host || !host->alloc || !host->release || !host->map || !host->remap || !host->unmap ||
            !host->close || !host->row_open || !host->row_close || !host->row_picture || !host->row_title || !host->row_do ||
            !host->row_facts || !host->screens || !host->now || !host->keymap || !host->repeat || !host->seat || !host->locks ||
            !host->clip_type || !host->clip_pull || !host->clip_abort || !host->clip_push || !host->clip_clear || !host->dmabuf_info ||
            !host->dmabuf_table || !host->dmabuf_map || !host->pointer_hold || !host->random)
                return -1;
        memory_zero(&wayland, sizeof wayland);
        wayland.pointer_row = wayland.pointer_surface = wayland.keyboard_row = -1;
        wayland.drag.client = wayland.drag.source = wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1;
        wayland.drag.xtarget = -1;
        wayland.cursor_client = wayland.cursor_surface = -1;
        wayland.xwayland_uid = -1;
        wayland.host = host;
        wayland.input_at = host->now(host->context);
        wayland.screens = host->screens(host->context, wayland.screen, WAYLAND_SCREENS);
        if (wayland.screens < 0)
                wayland.screens = 0;
        wayland_dmabuf_load();
        wayland.hold_row = wayland.hint_row = wayland.counts_row = wayland.unheld_row = -1;
        return 0;
}

/* Register optional explicit syncobj / wl_drm host callbacks before or right after wayland_start. */
WAYLAND_API void wayland_syncobj_host(const struct wayland_syncobj_host *syncobj)
{
        if (syncobj)
                wayland.syncobj = *syncobj;
        else
                wayland_zero(&wayland.syncobj, sizeof wayland.syncobj);
        wayland.syncobj_ok = 0;
        wayland.drm_node[0] = 0;
        if (wayland.host && wayland.syncobj.available)
        {
                long caps = wayland.syncobj.available(wayland.syncobj.context, wayland.drm_node, (long)sizeof wayland.drm_node);

                wayland.syncobj_ok = (caps & WAYLAND_SYNCOBJ_TIMELINE_OK) != 0;
                if (!(caps & WAYLAND_SYNCOBJ_NODE_OK))
                        wayland.drm_node[0] = 0;
        }
}

/* The user whose clients are offered the X server's protocols: the one the X window manager's socket takes. None until the host says. */
WAYLAND_API void wayland_xwayland_user(long uid)
{
        wayland.xwayland_uid = uid;
}

/* The client that is the X server (its window manager found it by its pid): its outputs are told again, in X's space (wayland_x_screen). */
WAYLAND_API void wayland_xwayland_client(long slot)
{
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots ? wayland.client[slot] : 0;

        // Each X server is a client of its own (one Xwayland per program run): its own space, marked on it, until it is dropped.
        if (!wayland.host || !c || c->xspace)
                return;
        c->xspace = 1;
        for (long at = 0; c && at < c->tbl[WLT_OBJECT].count; at++)
                if (c->objects[at].iface == WLI_zxdg_output_v1)
                        wayland_xdg_output_send(c, at, c->objects[at].aux);
        for (long at = 0; c && at < c->tbl[WLT_OBJECT].count; at++)
                if (c->objects[at].iface == WLI_wl_output)
                        wayland_output_send(c, at);
}

// Whether a row is an override-redirect X window's (a menu, a tooltip, a drag's window, one that places itself): its client keeps its
// own grabs, and the compositor neither puts it away for a press elsewhere nor lets a window it does not hang from over it.
WAYLAND_API int wayland_row_xoverride(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 ? wayland_row_surface(row, &c) : 0;

        return s && s->xsurface && s->u.xwin.popup;
}

/* A client's socket accepted: its record, with the display, or a refusal. */
WAYLAND_API long wayland_connect(long pid, long uid)
{
        struct wayland_client *c;
        long slot = -1;

        if (!wayland.host)
                return -1;
        for (long at = 0; at < wayland.client_slots && slot < 0; at++)
                if (!wayland.client[at])
                        slot = at;
        // Every slot taken: one more is made (no count of clients is a cap; the memory for them is).
        if (slot < 0 && (slot = wayland_client_slot_grow()) < 0)
                return -1;
        c = (struct wayland_client *)wayland.host->alloc(wayland.host->context, (long)sizeof *c);
        if (!c)
        {
                if (!wayland.clients)
                        wayland_client_slots_drop();
                return -1;
        }
        c->live = 1;
        c->slot = slot;
        c->pid = pid;
        c->uid = uid;
        c->xspace = 0;
        c->stalled_sync_handle = -1;
        wayland_tables_init(c);
        wayland.client[slot] = c;
        wayland.clients++;
        // The queue's first WAYLAND_OUT, which most clients never pass.
        c->out_length = WAYLAND_OUT - WAYLAND_EVENT;
        if (!wayland_out_grow(c))
                c->out = 0;
        c->out_length = 0;
        if (!c->out || wayland_object_new(c, 1, WLI_wl_display, 1) < 0)
        {
                // No memory for the display's object: the connection is refused, and its tables given back.
                wayland_tables_drop(c);
                wayland.client[slot] = 0;
                wayland.clients--;
                wayland.host->release(wayland.host->context, c, (long)sizeof *c);
                if (!wayland.clients)
                        wayland_client_slots_drop();
                return -1;
        }
        return slot;
}

/*
        The descriptors of the output queue let go: sent, or the client gone. The shim's own (a copy's pipe) are closed here and
        nowhere else, so the number the queue holds is open until it has gone out; the host's (a keymap, a format table) stay.
*/
static void wayland_out_fds_let_go(struct wayland_client *c)
{
        struct wayland_outfd *f = c->out_fds;

        for (long at = 0; at < c->out_fd_count; at++)
                if (f[at].owned)
                {
                        wayland.host->close(wayland.host->context, f[at].fd);
                        wayland.fds--;
                }
        c->out_fd_count = 0;
}

// The descriptors a send of count bytes carried (the batch wayland_out gave, when any byte went): the shim's own are closed, and the
// rest move to the front, their offsets less the bytes gone.
static void wayland_out_fds_sent(struct wayland_client *c, long count)
{
        struct wayland_outfd *f = c->out_fds;
        long sent = wayland_out_fd_batch(c), kept = 0;

        for (long at = 0; at < sent; at++)
                if (f[at].owned)
                {
                        wayland.host->close(wayland.host->context, f[at].fd);
                        wayland.fds--;
                }
        for (long at = sent; at < c->out_fd_count; at++, kept++)
        {
                f[kept] = f[at];
                f[kept].at -= count;
        }
        c->out_fd_count = kept;
}

/*
        A client gone, however it went: every window it had, every pool, every descriptor it sent and the shim did not
        use, and its record. The one place that gives back what a client held.
*/
WAYLAND_API void wayland_drop(long slot)
{
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots ? wayland.client[slot] : 0;

        if (!c)
                return;
        wayland_touch_forget(slot);
        wayland_token_forget(slot);
        // The drag's client gone: cancelled. The client a drag is over gone: left (a dropped drag is cancelled).
        if (wayland.drag.live && wayland.drag.client == slot)
        {
                wayland.drag.source = -1; // its source goes with it, and is told nothing
                wayland_drag_end(1);
        }
        else if (wayland.drag.live && wayland.drag.target == slot)
        {
                if (wayland.drag.dropped)
                {
                        wayland.drag.target = -1;
                        wayland_drag_end(0);
                }
                else
                        wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1;
        }
        // Its picture of the pointer is none: the pools it was in are let go of below.
        if (wayland.cursor_client == slot)
        {
                wayland.cursor_client = wayland.cursor_surface = -1;
                wayland.cursor_serial++;
        }
        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
                if (c->surfaces[at].live)
                {
                        wayland_row_release(c, &c->surfaces[at]);
                        wayland.surfaces--;
                }
        wayland_texts_drop(c);
        for (long at = 0; at < c->tbl[WLT_POOL].count; at++)
                if (c->pools[at].live)
                        wayland_pool_drop(c, at);
        for (long at = 0; at < c->tbl[WLT_TIMELINE].count; at++)
                if (c->timelines[at].live)
                        wayland_timeline_drop(c, at);
        for (long at = 0; at < c->tbl[WLT_PARAMS].count; at++)
                if (c->params[at].live)
                        wayland_params_free(c, &c->params[at]);
        for (long at = 0; at < c->tbl[WLT_CONSTRAINT].count; at++)
                if (c->constraints[at].live)
                        c->constraints[at].live = 0, wayland.constraints--;
        for (long at = 0; at < c->tbl[WLT_BUFFER].count; at++)
                if (c->buffers[at].live)
                        wayland.buffers--;
        for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
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
        for (long at = 0; at < c->tbl[WLT_NOTIFY].count; at++)
                wayland_notify_free(c, at);
        wayland_out_clear(slot);
        wayland.client[slot] = 0;
        wayland.clients--;
        wayland_tables_drop(c);
        wayland.host->release(wayland.host->context, c, (long)sizeof *c);
        // The last client gone: the slots are given back, made again by the next connection.
        if (!wayland.clients)
                wayland_client_slots_drop();
}

WAYLAND_API void wayland_stop(void)
{
        if (!wayland.host)
                return;
        for (long slot = 0; slot < wayland.client_slots; slot++)
                wayland_drop(slot);
        wayland_table_drop(&wayland.token_table);
        wayland.token = 0, wayland.tokens = 0;
        wayland_table_drop(&wayland.drag.x_types);
        wayland.drag.x_bytes = 0;
        wayland_zero(&wayland.syncobj, sizeof wayland.syncobj);
        wayland.syncobj_ok = 0;
        wayland.drm_node[0] = 0;
        wayland.host = 0;
}

/* What the client is: 0 alive, 1 sent a protocol error (flush, then drop), 2 cannot be sent more (drop). */
WAYLAND_API long wayland_dead(long slot)
{
        return slot >= 0 && slot < wayland.client_slots && wayland.client[slot] ? wayland.client[slot]->dead : 2;
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
                unsigned int head[2];

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
                        size = (long)wire_message(c->in, (unsigned long long)c->in_length, WAYLAND_MESSAGE, head);
                        if (size < 0)
                        {
                                c->in_length = 0;
                                status = wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "a message of an impossible size");
                                break;
                        }
                        if (!size)
                                continue;
                        c->in_length = 0;
                        status = wayland_dispatch(c, c->in, size, head[0], (long)head[1]);
                        if (status == WAYLAND_STALL)
                        {
                                wayland_stall(c, c->in, size, bytes, length);
                                status = WAYLAND_OK;
                        }
                        continue;
                }
                // The head read and checked by the library (wire_message): a size no message has is the client's end, and no size yet
                // (fewer than eight bytes, or a message that has not all come) is kept for the rest.
                size = (long)wire_message(bytes, (unsigned long long)length, WAYLAND_MESSAGE, head);
                if (size < 0)
                {
                        status = wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "a message of an impossible size");
                        break;
                }
                if (!size)
                {
                        wayland_copy(c->in, bytes, length);
                        c->in_length = length;
                        break;
                }
                status = wayland_dispatch(c, bytes, size, head[0], (long)head[1]);
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
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots && wayland.host ? wayland.client[slot] : 0;

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
        return slot >= 0 && slot < wayland.client_slots && wayland.client[slot] && !wayland.client[slot]->dead && wayland.client[slot]->stalled;
}

/*
        When a stalled client is next worth asking of, on the host's clock (ms), and in fd the dma-buf it waits for, which the host
        may poll to be told the moment it is ready (-1 for none): a client that waits for its budget at the end of its window of time,
        one that waits for its GPU at the end of its time to finish. 0 for a client that does not wait.
*/
WAYLAND_API long wayland_stall_due(long slot, long *fd)
{
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots && wayland.host ? wayland.client[slot] : 0;

        *fd = -1;
        if (!c || c->dead || !c->stalled)
                return 0;
        if (c->hold)
                return c->hold_at; // a timed hold wakes the loop at its time; a barrier's is woken by the frames (wayland_frame_wanted)
        if (c->stalled_sync_handle >= 0)
        {
                long efd = wayland.syncobj.eventfd ? wayland.syncobj.eventfd(wayland.syncobj.context, c->stalled_sync_handle, c->stalled_sync_point) : -1;
                if (efd >= 0)
                {
                        *fd = efd;
                        return c->stalled_at + WAYLAND_STALL_MS + 1;
                }
                long next = wayland.host->now(wayland.host->context) + 1;
                long cap = c->stalled_at + WAYLAND_STALL_MS + 1;

                return next < cap ? next : cap;
        }
        if (c->stalled_fd < 0)
                return c->budget_surface >= 0 ? c->surfaces[c->budget_surface].budget_at + WAYLAND_BUDGET_MS : 0;
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
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots && wayland.host ? wayland.client[slot] : 0;
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
        if (c->stalled_sync_handle >= 0
                ? (wayland.syncobj.ready && !wayland.syncobj.ready(wayland.syncobj.context, c->stalled_sync_handle, c->stalled_sync_point))
            : c->stalled_fd < 0
                ? c->budget_surface >= 0 && c->surfaces[c->budget_surface].live && !wayland_budget_left(&c->surfaces[c->budget_surface])
                : wayland.host->dmabuf_ready && !wayland.host->dmabuf_ready(wayland.host->context, c->stalled_fd))
        {
                if ((c->stalled_fd >= 0 || c->stalled_sync_handle >= 0) &&
                    wayland.host->now(wayland.host->context) - c->stalled_at > WAYLAND_STALL_MS)
                {
                        c->stalled = 0;
                        c->stalled_sync_handle = -1;
                        c->stash_length = 0;
                        wayland_fail(c, 1, WAYLAND_IMPLEMENTATION, "the buffer's writer never finished");
                        return 0;
                }
                return 1;
        }
        c->stalled_sync_handle = -1;
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
// What wayland_out hands the host: the batch's descriptors, copied in order.
static long wayland_out_fd_view[WAYLAND_OUT_FDS];

WAYLAND_API long wayland_out(long slot, const unsigned char **bytes, const long **fds, long *fd_count)
{
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots && wayland.host ? wayland.client[slot] : 0;

        *bytes = 0;
        *fds = 0;
        *fd_count = 0;
        if (!c)
                return 0;
        *bytes = c->out;
        *fd_count = wayland_out_fd_batch(c);
        for (long at = 0; at < *fd_count; at++)
                wayland_out_fd_view[at] = c->out_fds[at].fd;
        *fds = wayland_out_fd_view;
        // The descriptors past the batch wait for the next send: the bytes of this one end where the first of them begins.
        return *fd_count < c->out_fd_count ? c->out_fds[*fd_count].at : c->out_length;
}

// A send that took part of the queue, or carried descriptors, or left feedback waiting for room: out of the line of wayland_sent,
// whose every send of all of a report's events is then a leaf that saves no registers (it saved six, and gave them back, each time).
static __attribute__((noinline)) void wayland_sent_part(struct wayland_client *c, long slot, long count);

WAYLAND_API void wayland_sent(long slot, long count)
{
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots && wayland.host ? wayland.client[slot] : 0;

        if (!c || count <= 0 || count > c->out_length)
                return;
        // All of it sent with no descriptor and no feedback waiting for room, as nearly every send is: the queue is empty, nothing
        // to move up or close.
        if (count == c->out_length && !c->out_fd_count && !c->feedback_pending)
        {
                c->out_length = 0;
                if (!c->dead)
                        wayland_out_clear(slot);
                return;
        }
        wayland_sent_part(c, slot, count);
}

static __attribute__((noinline)) void wayland_sent_part(struct wayland_client *c, long slot, long count)
{
        // What is left moved to the front, a word at a time forward (the place a word goes is below where it is read, and every word
        // is read before one is written over it), then its last few bytes.
        {
                long at = count;

                for (; at + 8 <= c->out_length; at += 8)
                        *(wayland_wire_long *)(c->out + at - count) = *(const wayland_wire_long *)(c->out + at);
                for (; at < c->out_length; at++)
                        c->out[at - count] = c->out[at];
        }
        c->out_length -= count;
        wayland_out_fds_sent(c, count);
        wayland_feedback_flush(c);
        if (!c->out_length && !c->dead && !c->feedback_pending)
                wayland_out_clear(slot);
}

// The requests dispatched so far, over every client, and in quiet those of them that changed nothing the host shows (wayland.quiet).
WAYLAND_API long wayland_heard(long *quiet)
{
        *quiet = wayland.quiet;
        return wayland.heard;
}

// Whether a client has bytes queued or dead state waiting to flush; and how many slots there are to ask of.
WAYLAND_API int wayland_out_dirty(long slot)
{
        return slot >= 0 && slot < wayland.client_slots && (wayland.out_dirty[slot / 64] >> (slot % 64) & 1u);
}

/*
        Whether a client's queue is past WAYLAND_OUT_HIGH: the host reads no more of it, and polls it for room and not for its
        requests, until it has taken enough of its events (a reply to every request is queued, so reading on would only grow it).
*/
WAYLAND_API int wayland_out_backlog(long slot)
{
        return slot >= 0 && slot < wayland.client_slots && wayland.host && wayland.client[slot] &&
               wayland.client[slot]->out_length > WAYLAND_OUT_HIGH;
}

WAYLAND_API long wayland_client_slots(void)
{
        return wayland.client_slots;
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
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                //      A screen went: the surfaces on it leave it, and what the client bound of it is told nothing more (its number is
                //      nobody's: a screen that comes with it later is another global, and these are not it).
                for (long screen = wayland.screens; c && screen < was && screen < WAYLAND_SCREENS; screen++)
                {
                        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
                        {
                                struct wayland_surface *surface = &c->surfaces[at];

                                if (!surface->live || !(surface->entered & (1u << screen)))
                                        continue;
                                for (long object = 0; object < c->tbl[WLT_OBJECT].count; object++)
                                        if (c->objects[object].iface == WLI_wl_output && c->objects[object].aux == screen)
                                                wayland_ev(c, surface->id, WLE_wl_surface_leave, c->objects[object].id);
                                surface->entered &= ~(1u << screen);
                        }
                        for (long object = 0; object < c->tbl[WLT_OBJECT].count; object++)
                                if ((c->objects[object].iface == WLI_wl_output || c->objects[object].iface == WLI_zxdg_output_v1) &&
                                    c->objects[object].aux == screen)
                                        c->objects[object].aux = 0xffff;
                }
                //      Each registry a client holds says so, as the protocol has it: a screen that came is a global to bind, one that went
                //      is removed.
                for (long at = 0; c && at < c->tbl[WLT_OBJECT].count && wayland.screens != was; at++)
                        if (c->objects[at].iface == WLI_wl_registry)
                        {
                                for (long screen = was; screen < wayland.screens; screen++)
                                        wayland_global_send(c, c->objects[at].id, wayland_output_name(screen));
                                for (long screen = wayland.screens; screen < was; screen++)
                                        wayland_global_remove_send(c, c->objects[at].id, wayland_output_name(screen));
                        }
                for (long at = 0; c && at < c->tbl[WLT_OBJECT].count; at++)
                        if (c->objects[at].iface == WLI_zxdg_output_v1)
                                wayland_xdg_output_send(c, at, c->objects[at].aux);
                for (long at = 0; c && at < c->tbl[WLT_OBJECT].count; at++)
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
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *root = c ? &c->surfaces[wayland.row[row].surface] : 0;
                struct wayland_facts facts = {0, 0, 0, 0, 0, 0, 0};
                long percent;

                if (!root || !root->live || root->row != row || wayland.host->row_facts(wayland.host->context, (int)row, &facts) < 0)
                        continue;
                wayland_surface_screens(c, root, wayland_screen_at(facts.x, facts.y));
                percent = facts.scale > 0 ? facts.scale : 100;
                if (wayland.row[row].percent == percent)
                        continue;
                wayland.row[row].percent = percent;
                for (struct wayland_surface *o = root; o; o = wayland_tree_next(c, root, o))
                        wayland_scale_tell(c, o, percent);
                if (root->role == WAYLAND_ROLE_TOPLEVEL && root->u.top.told && wayland_surface_unit(root) != root->told_unit &&
                    (facts.width > 0 || facts.height > 0))
                        wayland_configure(c, root, 0);
        }
}

/*
        A window's row knocked: what the compositor says of it now, against what its client was told. The host counts the asks
        to go of a window (facts.closing) and the surface keeps the count its client was told of: an ask it has not been told of
        is said (xdg_toplevel.close) and is then told, so what the client does with it is the client's (it may ask the person
        and keep the window), and the next press of the close button is one more in the host's count, said again. The count is
        the host's alone to change: the shim took the ask off the row itself once (a call that cleared the host's closing), which
        was a second way of saying an ask was over beside the count. A popup is asked to go once by the host, and is said so
        once (popup_done: it is dismissed, and stays so until its window is gone). A size or a state that changed is a new
        configure, which a client with too many unacknowledged waits for.
*/
WAYLAND_API void wayland_row_wake(long row)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        struct wayland_facts facts;
        long slot;

        if (!wayland.host || row < 0 || row >= wayland.row_made || wayland.row[row].client < 0)
                return;
        c = wayland.client[wayland.row[row].client];
        s = c ? &c->surfaces[wayland.row[row].surface] : 0;
        if (!s || !s->live || s->row != row || wayland.host->row_facts(wayland.host->context, (int)row, &facts) < 0)
                return;
        if (facts.closing && facts.closing != s->closes)
        {
                int popup = s->role == WAYLAND_ROLE_POPUP;

                slot = wayland_find(c, s->role_id, popup ? WLI_xdg_popup : WLI_xdg_toplevel);
                if (slot >= 0)
                        wayland_ev0(c, s->role_id, popup ? WLE_xdg_popup_popup_done : WLE_xdg_toplevel_close);
                // This ask is told, or has nobody here to tell (an X window, which its window manager asks): it is not looked at again.
                s->closes = facts.closing;
        }
        // The compositor's own word on the state is the last word too: a window it took out of fullscreen (or out of maximized) is not
        // put back in it when it is shown again, for a request the compositor has since let go of.
        if (s->role == WAYLAND_ROLE_TOPLEVEL)
        {
                if (!(facts.states & WAYLAND_STATE_FULLSCREEN))
                        s->u.top.want_fullscreen = 0;
                if (!(facts.states & (WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_FULLSCREEN)))
                        s->u.top.want_maximized = 0;
        }
        if (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.told &&
            (facts.width != s->u.top.told_width || facts.height != s->u.top.told_height || facts.states != s->u.top.told_states) && !wayland_facts_freed(s, &facts))
        {
                // A window resized by hand is told a size a frame: the hand moves many times in one (a mouse reports a thousand times a
                // second), and each size told is a buffer the client allocates and a picture it draws to be shown for none of them. A
                // size that changed again in the frame the last one was told in is owed, and the frame's end says what the size is
                // then (wayland_frame). A state that changed (the resize begun or ended, the keys taken) is said at once.
                if (facts.states == s->u.top.told_states && (facts.states & WAYLAND_STATE_RESIZING) && s->u.top.configure_frame == wayland.frames + 1)
                        s->u.top.configure_late = 1;
                else
                        wayland_configure(c, s, 0);
        }
}

/*
        A frame is on the screen: the callbacks that waited for one are answered, for every window that is shown and
        not put away. A window put away is not told of frames, and is told of the next the moment it is shown.
*/
static void wayland_callbacks_due(long now);

WAYLAND_API void wayland_frame(void)
{
        long now;

        if (!wayland.host)
                return;
        wayland.frames++;
        now = wayland.host->now(wayland.host->context);
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row[row].surface] : 0;

                // The size a hand's resize came to in the frame that ended, said now (wayland_row_wake kept it back).
                if (s && s->live && s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.configure_late)
                {
                        s->u.top.configure_late = 0;
                        wayland_configure_owed(c, s);
                }
        }
        // The callbacks were answered when the frame latched (wayland_latch), and those taken since are the next frame's. A frame's time
        // with nothing latched (no present began) answers them all, and is a latch of its own for the fifo barriers.
        if (!wayland.calling)
        {
                wayland.latches++;
                wayland_callbacks_due(now);
        }
        wayland.calling = 0;
}

/* The callbacks every window shown has waiting, answered at the host's ms now: a window put away is told of no frame. */
static void wayland_callbacks_due(long now)
{
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *s = c && c->waiting ? &c->surfaces[wayland.row[row].surface] : 0;
                struct wayland_facts facts;
                int any = 0;

                if (s && s->live)
                        for (struct wayland_surface *o = s; o && !any; o = wayland_tree_next(c, s, o))
                                any = o->cbq[WLCB_READY].count != 0;
                if (!any)
                        continue;
                if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                        continue;
                for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                        wayland_callbacks_done(c, o, now);
        }
}

/*
        A present begins: what the clients have committed is what it latches. The frame callbacks waiting are answered now, as the
        content they were asked with is taken: the client draws its next picture while this one is composed and flipped, which is the
        most time it can be given (the present's whole time on the card, 3 ms of box's 6.9 ms refresh, more than it had). The feedbacks
        taken so far are this frame's (presentings_framed), answered at its flip; what is committed from now until then is the next
        frame's, its callbacks answered at the next latch, and a fifo barrier set before now is cleared (the content that set it is
        latched). Before this, the frame's end (wayland_frame) and its flip (wayland_presented) answered whatever was there when they
        came: a commit made while the card composed or the flip was on its way was told presented at that flip, a refresh before it was
        shown, its frame callback came before any frame had it, and a barrier it set was cleared by a frame that never latched it, so
        the next FIFO commit replaced it unshown.
*/
WAYLAND_API void wayland_latch(void)
{
        if (!wayland.host)
                return;
        wayland.latches++;
        wayland.framing = wayland.calling = 1;
        // Pin the exact lent buffer handed to this present (flip or GPU compose) so commits arriving before the vblank
        // cannot release the buffer that is currently being flipped onto or scanned out by the display.
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row[row].surface] : 0;

                if (!s || !s->live)
                        continue;
                if (s->lent_latched)
                {
                        wayland_let_go(c, s->lent_latched - 1);
                        s->lent_latched = 0;
                }
                if (s->lent)
                {
                        s->lent_latched = s->lent;
                        c->buffers[s->lent - 1].held++;
                }
        }
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                // A client with no feedback taken has none framed either (each count is kept at or under its list's).
                if (!c || !c->presenting_surfaces)
                        continue;
                for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
                        c->surfaces[at].presentings_framed = c->surfaces[at].cbq[WLCB_PRESENTING].count;
        }
        wayland_callbacks_due(wayland.host->now(wayland.host->context));
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
        struct wayland_cbq *pq = &s->cbq[WLCB_PRESENTING];
        // A frame flipped answers what it latched (wayland_latch); a frame's time that latched nothing answers all.
        long k = wayland.framing && s->presentings_framed < pq->count ? s->presentings_framed : pq->count;

        if (k && k == pq->count)
                c->presenting_surfaces--;
        for (long at = 0; at < k; at++)
        {
                unsigned int id = wayland_cb_pop(c, pq);
                long slot = wayland_find(c, id, WLI_wp_presentation_feedback);

                if (slot < 0)
                        continue;
                {
                        // The last word is the presentation flags the compositor can vouch for (vsync, hw_clock, hw_completion).
                        unsigned int words[7] = {(unsigned int)(seconds >> 32), (unsigned int)seconds, (unsigned int)nanoseconds, (unsigned int)refresh,
                                                 (unsigned int)(seq >> 32), (unsigned int)seq, (unsigned int)flags};

                        wayland_send(c, id, WLE_wp_presentation_feedback_presented, words, 7);
                        wayland_object_delete(c, slot);
                }
        }
        s->presentings_framed = 0;
}

/*
        The buffers a window's lent pictures replaced, its own and its subsurfaces' as layers, let go of: those in pools, and those of dma-bufs
        too when dmabufs (one may be the buffer the card shows until the flip lands).
*/
static void wayland_window_gone(struct wayland_client *c, struct wayland_surface *root, int dmabufs)
{
        for (struct wayland_surface *s = root; s; s = wayland_tree_next(c, root, s))
                if (s->lent_gone && (dmabufs || !c->pools[c->buffers[s->lent_gone - 1].pool].dmabuf))
                {
                        wayland_let_go(c, s->lent_gone - 1);
                        s->lent_gone = 0;
                }
}

/*
        The feedbacks of every window shown, answered with the frame at when. A drawn frame (drawn 1) also lets go of the buffer a
        lent picture was replaced by, since the frame that shows the new picture is the one drawn; an idle frame does not. Either lets go of
        the buffers of subsurfaces that went (wayland_gone_drawn): the host was told when they went, and a frame has gone by since.
*/
static void wayland_presented_rows(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags, int drawn, long zero_row)
{
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row[row].surface] : 0;
                struct wayland_facts facts;

                if (!s || !s->live)
                        continue;
                if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                {
                        // A window put away is in no frame, so none will show the picture that replaced its lent buffer: the buffer is the
                        // client's again at the first frame's time after the commit, drawn or not (what read it before the commit is done
                        // by then). Kept for a frame that never comes, a client with two buffers that draws while it is put away had
                        // neither: one is the picture, and the other was never released.
                        if (s->lent_latched)
                        {
                                wayland_let_go(c, s->lent_latched - 1);
                                s->lent_latched = 0;
                        }
                        if (s->lent_shown)
                        {
                                wayland_let_go(c, s->lent_shown - 1);
                                s->lent_shown = 0;
                        }
                        wayland_window_gone(c, s, 1);
                        continue;
                }
                // The frame that shows the picture a lent buffer was replaced by is drawn: update the pinned on-screen scanout
                // buffer first so a buffer that just flipped onto the display (zero_row) stays held until the NEXT flip away from it.
                if (drawn)
                {
                        if (row == zero_row)
                        {
                                unsigned int old_shown = s->lent_shown;

                                s->lent_shown = s->lent_latched;
                                s->lent_latched = 0;
                                if (old_shown)
                                        wayland_let_go(c, old_shown - 1);
                        }
                        else
                        {
                                if (s->lent_latched)
                                {
                                        wayland_let_go(c, s->lent_latched - 1);
                                        s->lent_latched = 0;
                                }
                                if (s->lent_shown)
                                {
                                        wayland_let_go(c, s->lent_shown - 1);
                                        s->lent_shown = 0;
                                }
                        }
                        wayland_window_gone(c, s, 1);
                }
                // The window the card scanned out of its own buffer is presented with no copy (zero_copy), its subsurfaces were not shown so.
                for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                        wayland_presentings_done(c, o, when, refresh, seq,
                                                 o == s && row == zero_row ? flags | WLN_wp_presentation_feedback_kind_zero_copy : flags);
        }
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && wayland.client[slot]->gone.count)
                        wayland_gone_drawn(wayland.client[slot], 1);
        wayland.framing = 0;
}

/*
        A frame was drawn (composed; its flip may still be on its way): every window shown is in it with the picture it has now, so the
        buffer a lent picture replaced is read by nothing any more and is the client's again, here, before the frame callbacks the same
        frame answers (the host calls this, then wayland_frame). A buffer in a pool, that is: one of a dma-buf may be the very buffer the
        card shows until the flip lands, and waits for wayland_presented. Released at the flip instead, a client with two buffers was
        told the frame was done while both were still the compositor's, and one that draws only when a buffer is free drew no more.
*/
static void wayland_drawn_rows(int dmabuf_busy_row)
{
        for (long row = 0; wayland.host && row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row[row].surface] : 0;
                struct wayland_facts facts;

                if (!s || !s->live)
                        continue;
                // A window put away is in no frame: its buffer goes at a frame's time all the same (wayland_presented_rows).
                if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                        continue;
                wayland_window_gone(c, s, !(dmabuf_busy_row < -1 || (int)row == dmabuf_busy_row));
        }
        for (long slot = 0; wayland.host && slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && wayland.client[slot]->gone.count)
                        wayland_gone_drawn(wayland.client[slot], 0);
}

WAYLAND_API void wayland_drawn(void)
{
        wayland_drawn_rows(-2);
}

WAYLAND_API void wayland_drawn_composed(int busy_row)
{
        wayland_drawn_rows(busy_row);
}

/*
        A frame was drawn and flipped to the screen (wayland_presented's when, refresh and seq, as above): the feedbacks its
        commits took are answered, for every window that is shown and not put away. Called only for a frame that was drawn, so
        a timer's frame with nothing new answers none. zero_row is the row the card showed straight from its client's buffer, -1 for none.
*/
WAYLAND_API void wayland_presented(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags, long zero_row)
{
        if (!wayland.host)
                return;
        wayland_presented_rows(when, refresh, seq, flags, 1, zero_row);
}

/*
        A frame time went by and nothing was drawn: a commit whose content was the screen's (no damage, or damage of nothing) changes
        nothing, so the picture is on the screen at this frame, and the feedbacks it took are answered. Without this a commit that
        draws nothing waits for a frame that is never drawn, and a client that waits for its feedback (Chromium's video, which submits
        its next frame when the last one is presented) stops.
*/
WAYLAND_API void wayland_presented_idle(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags)
{
        if (!wayland.host)
                return;
        wayland.framing = 0;
        wayland_presented_rows(when, refresh, seq, flags, 0, -1);
}

/* Whether a window that is shown has a feedback waiting for the next frame (wayland_presented_idle answers it). */
static int wayland_presentings_wanted(struct wayland_client *c, long row)
{
        struct wayland_surface *root = &c->surfaces[wayland.row[row].surface];
        struct wayland_facts facts;

        if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                return 0;
        for (struct wayland_surface *o = root; o; o = wayland_tree_next(c, root, o))
                if (o->cbq[WLCB_PRESENTING].count)
                        return 1;
        return 0;
}

/*
        Whether any callback waits for a frame, or a feedback for the next frame, so the compositor knows to come back for one.
*/
WAYLAND_API long wayland_frame_wanted(void)
{
        // A commit held for a barrier is waited for a frame at a time: each frame drawn is the one that clears it.
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && wayland.client[slot]->stalled && wayland.client[slot]->hold && !wayland.client[slot]->hold_at)
                        return 1;
        for (long row = 0; wayland.host && row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland.row[row].client >= 0 ? wayland.client[wayland.row[row].client] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row[row].surface] : 0;
                struct wayland_facts facts;
                int waits = 0;

                // A size owed to a window resized by hand is said at the frame's end: a frame is wanted for it.
                if (s && s->live && s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.configure_late)
                        return 1;
                // A window put away that holds the buffer its picture replaced is given a frame's time to let it go in (wayland_presented_rows),
                // and so is a client that holds those of subsurfaces that went.
                if (c && c->gone.count)
                        return 1;
                if (s && s->live && s->lent_gone &&
                    wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                        return 1;
                if (!c || (!c->waiting && !c->presenting_surfaces))
                        continue;
                if (c->waiting && s && s->live)
                        for (struct wayland_surface *o = s; o && !waits; o = wayland_tree_next(c, s, o))
                                waits = o->cbq[WLCB_READY].count != 0;
                // A window put away is told of no frame (wayland_frame), so no frame is wanted for its callbacks: they are answered by the
                // first frame drawn once it is shown, which the compositor draws for any window it shows.
                if (waits && !(wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED)))
                        return 1;
                // A feedback waits for the frame an idle tick answers (wayland_presented_idle): a client that holds none costs nothing here.
                if (c->presenting_surfaces && wayland_presentings_wanted(c, row))
                        return 1;
        }
        return 0;
}

/*
        A window's hints as its commits set them, one WAYLAND_HINT_ name each, read from the surface of its row. A row with no window
        has the fallback of each: opaque (0xffffffff) for ALPHA, the multiplier its surface shows; no content type for CONTENT; 0 for
        the rest. CONTENT is the content type; FIFO whether its commit waits for the barrier (wp_fifo); TIMESTAMP the time its commit is
        for (wp_commit_timing, nanoseconds on the monotonic clock, 0 for none); ATTENTION whether an xdg-activation it was not vouched for
        wants attention until it is focused; ASYNC the tearing hint; ICON whether its toplevel has an icon (xdg-toplevel-icon); MODAL
        whether its dialog is modal. The compositor shows a commit at its next frame whatever FIFO and TIMESTAMP say, so they are read
        for what they are and not yet for a hold: a frame is already the barrier, and a commit cannot be held to a future time.
*/
#define WAYLAND_ROW_HINTS(X) \
        X(ALPHA, s->cur.alpha, 0xffffffffu) \
        X(CONTENT, s->content, WLN_wp_content_type_v1_type_none) \
        X(FIFO, s->cur.fifo_wait, 0) \
        X(TIMESTAMP, s->cur.timestamp, 0) \
        X(ATTENTION, s->attention, 0) \
        X(ASYNC, s->cur.async_hint, 0) \
        X(ICON, s->cur.icon, 0) \
        X(MODAL, s->modal, 0)

enum wayland_row_hint
{
#define X(name, field, fallback) WAYLAND_HINT_##name,
        WAYLAND_ROW_HINTS(X)
#undef X
};

WAYLAND_API unsigned long wayland_row_hint(long row, enum wayland_row_hint hint)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < wayland.row_made ? wayland_row_surface(row, &c) : 0;

        switch (hint)
        {
#define X(name, field, fallback) case WAYLAND_HINT_##name: return s ? (unsigned long)(field) : (unsigned long)(fallback);
                WAYLAND_ROW_HINTS(X)
#undef X
        }
        return 0;
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
        {
                c->pinches++;
                for (long at = 0; at < wayland.row_made; at++)
                        if (wayland.row[at].client == c->slot)
                                wayland.host->row_do(wayland.host->context, (int)at, WAYLAND_DO_PINCH_WANT, 1, 0);
        }
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

        if (!wayland.host || row != wayland.pointer_row || !(s = wayland_row_surface(row, &c)) || !c->pinches)
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
        for (long at = 0, left = c->pinches; at < c->tbl[WLT_OBJECT].count && left > 0; at++)
        {
                struct wayland_object *o = &c->objects[at];

                if (o->iface != WLI_zwp_pointer_gesture_pinch_v1 || o->id == 0)
                        continue;
                left--;
                if (phase == 1)
                        wayland_ev(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_begin,
                                   (unsigned int)serial, (unsigned int)time, s->id, 2u);
                else if (phase == 2)
                        wayland_ev(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_end,
                                   (unsigned int)serial, (unsigned int)time, 0u);
                else
                        wayland_ev(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_update,
                                   (unsigned int)time, (unsigned int)wayland_fixed(dx, s->scale120),
                                   (unsigned int)wayland_fixed(dy, s->scale120), (unsigned int)(scale > 0 ? scale / 4 : 0), 0u);
        }
}

/* The window of the surface whose xwayland_surface_v1 gave this X11 serial: its row, -1 while it has none, -2 when no surface has the serial. */
/* The serials of one X server are its own: each server numbers its windows from its own counter, so two servers name the same serial for
   two windows. A serial is looked for in the client that is the server (client: its slot, the window manager's, -1 for none). */
WAYLAND_API long wayland_client_count(void)
{
        return wayland.client_slots;
}

WAYLAND_API long wayland_client_pid(long slot)
{
        struct wayland_client *c = slot >= 0 && slot < wayland.client_slots ? wayland.client[slot] : 0;

        return c && c->live ? c->pid : -1;
}

static struct wayland_surface *wayland_xserial_surface(long client, unsigned long long serial, struct wayland_client **out_c)
{
        struct wayland_client *c = client >= 0 && client < wayland.client_slots ? wayland.client[client] : 0;

        if (out_c)
                *out_c = c;
        for (long slot = 0; c && slot < c->tbl[WLT_SURFACE].count; slot++)
        {
                struct wayland_surface *s = &c->surfaces[slot];

                if (s->live && s->xsurface && s->u.xwin.serial_set && s->u.xwin.serial == serial)
                        return s;
        }
        return 0;
}

WAYLAND_API long wayland_serial_row(long client, unsigned long long serial)
{
        struct wayland_surface *s = wayland_xserial_surface(client, serial, 0);

        if (!s)
                return -2;
        return s->row >= 0 && s->row < wayland.row_made ? s->row : -1;
}

/*
        The window manager's word on the X window whose surface set this serial: it is a window, or (popup, WAYLAND_XWINDOW_POPUP) an
        override-redirect window at (x, y) of the output; a window at (x, y) it placed itself (WAYLAND_XWINDOW_PLACED); one transient for
        the window of a row (WAYLAND_XWINDOW_OWNER: centred on it), with this title (plain text, cut to what a title takes). A surface that waited for the word
        is shown at the commit that follows. Answers the surface's row (-1 while it has none), or -2 when no surface has the serial
        yet, which the window manager asks of again. Said again, it sets the title anew.
*/
WAYLAND_API long wayland_xwindow_adopt(long client, unsigned long long serial, long popup, long x, long y, const char *title)
{
        long how = popup;
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_xserial_surface(client, serial, &c);
        char text[WAYLAND_TEXT];
        long at = 0;

        if (!s || !s->xsurface || s->u.xwin.dead)
                return -2;
        s->u.xwin.adopted = 1;
        popup = how & WAYLAND_XWINDOW_POPUP;
        if (s->row < 0)
        {
                s->u.xwin.popup = popup ? 1 : 0;
                s->u.xwin.placed = (how & WAYLAND_XWINDOW_PLACED) ? 1 : 0;
                s->u.xwin.owner = how >> WAYLAND_XWINDOW_OWNER_SHIFT;
                s->u.xwin.x = x;
                s->u.xwin.y = y;
        }
        else if (s->u.xwin.popup && wayland.host && (s->u.xwin.x != x || s->u.xwin.y != y))
        {
                // A popup its window manager moved while it is shown goes to its new place in the host, relative to its parent.
                long px = 0, py = 0;

                s->u.xwin.x = x;
                s->u.xwin.y = y;
                if (c && !s->u.xwin.alone && wayland_xparent(c, s, &px, &py) >= 0)
                        wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_PLACE, px, py);
                else if (s->u.xwin.alone)
                        wayland.host->row_do(wayland.host->context, s->row, WAYLAND_DO_PLACE, x, y);
        }
        for (; title && title[at] && at < WAYLAND_TEXT - 1; at++)
                text[at] = title[at];
        text[at] = 0;
        for (long i = 0; i <= at; i++)
                if (text[i] != s->title[i])
                {
                        for (long j = 0; j <= at; j++)
                                s->title[j] = text[j];
                        if (s->row >= 0 && wayland.host)
                                wayland.host->row_title(wayland.host->context, s->row, s->title[0] ? s->title : s->app_id, 0);
                        break;
                }
        return s->row >= 0 ? s->row : -1;
}

/* The window manager of an X window is gone: its window is taken away, and its surface is shown no more. */
WAYLAND_API void wayland_xwindow_gone(long client, unsigned long long serial)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_xserial_surface(client, serial, &c);

        if (!s || !s->xsurface)
                return;
        s->u.xwin.dead = 1;
        s->u.xwin.adopted = 0;
        if (c)
                wayland_row_release(c, s);
}

/* Whether a window has a keyboard shortcuts inhibitor, and whether its tearing hint is asynchronous (allowed to tear). */
WAYLAND_API long wayland_row_shortcuts(long row)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < wayland.row_made ? wayland_row_surface(row, &c) : 0;

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
                wayland_ev0(c, s->shortcut_id, active ? WLE_zwp_keyboard_shortcuts_inhibitor_v1_active
                                                      : WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive);
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
        struct wayland_surface *s = row >= 0 && row < wayland.row_made ? wayland_row_surface(row, &c) : 0;
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

        // A released drag whose target did not answer in WAYLAND_DRAG_ANSWER_MS is decided as it stands (cancelled, as nothing was taken).
        if (wayland.host && wayland.drag.live && wayland.drag.released &&
            wayland.host->now(wayland.host->context) - wayland.drag.released_at >= WAYLAND_DRAG_ANSWER_MS)
                wayland_drag_decide();
        if (!wayland.host || !wayland.idle_notifies)
                return;
        now = wayland.host->now(wayland.host->context);
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c || c->dead)
                        continue;
                for (long at = 0; at < c->tbl[WLT_NOTIFY].count; at++)
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
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                if (!c || c->dead)
                        continue;
                for (long at = 0; at < c->tbl[WLT_NOTIFY].count; at++)
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
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && !wayland.client[slot]->dead)
                        for (long at = 0; at < wayland.client[slot]->tbl[WLT_NOTIFY].count; at++)
                                *idled += wayland.client[slot]->notifies[at].live && wayland.client[slot]->notifies[at].idle;
}

/* How many xdg_system_bell rings and xdg-toplevel-icon sets there have been, over every client (for the moonwater debug line). */
WAYLAND_API void wayland_hint_counts(long *bells, long *icons)
{
        *bells = wayland.bells;
        *icons = wayland.icons;
}

/*
        The four hints of a row in one lookup, as wayland_row_hint gives them one by one (CONTENT, ICON, MODAL and the shortcut hint):
        compositor_wl_table asks every row of them each turn, so a row's surface is found once, not four times.
*/
WAYLAND_API void wayland_row_hints(long row, long *content, long *icon, long *modal, long *shortcuts)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = row >= 0 && row < wayland.row_made ? wayland_row_surface(row, &c) : 0;

        *content = s ? (long)s->content : WLN_wp_content_type_v1_type_none;
        *icon = s ? s->cur.icon : 0;
        *modal = s ? s->modal : 0;
        *shortcuts = s && (s->shortcuts || s->xgrab) ? s->shortcut_active : 0;
}

/* The client a row belongs to, or -1: the compositor asks before it treats a row as a program's. */
WAYLAND_API long wayland_row_owner(long row)
{
        return row >= 0 && row < wayland.row_made ? wayland.row[row].client : -1;
}

/*
        The pointer's picture a client drew for its window under the pointer (wl_pointer.set_cursor with a surface), for the compositor
        to draw in place of its own shape: the first pixel of the surface's buffer (format WLN_wl_shm_format_argb8888, premultiplied, or
        _xrgb8888; stride bytes a row, width by height pixels), the hotspot in those pixels (the point of it that is the pointer's place,
        which may lie outside it), the buffer's scale (its pixels a unit of the surface), and serial, which moves whenever any of that
        does: a commit of the surface, another surface or hotspot named, a shape named in its place, the pointer on another surface, the
        surface or its client gone. 1 for row the window under the pointer and a picture that is a buffer of one of those two formats the
        compositor can read; 0 otherwise, with out's pixels 0 and its serial the one now, and the compositor draws the shape it was told
        (WAYLAND_DO_CURSOR: the arrow while a surface is named, which has drawn nothing yet). The pixels are the client's pool, read-only
        and held (the buffer is not released while it is the picture) until the shim is next fed or a client is dropped: the compositor
        copies them then, as it copies any client picture. A pool its client cut is read as zeros (the SIGBUS handler's pages).
*/
struct wayland_cursor
{
        const unsigned char *pixels;
        long stride, width, height, format;
        long hot_x, hot_y, scale;
        unsigned long serial;
};

WAYLAND_API int wayland_cursor_picture(long row, struct wayland_cursor *out)
{
        struct wayland_client *c = wayland.cursor_client >= 0 && wayland.cursor_client < wayland.client_slots ? wayland.client[wayland.cursor_client] : 0;
        struct wayland_surface *s = c && wayland.cursor_surface >= 0 && wayland.cursor_surface < c->tbl[WLT_SURFACE].count ? &c->surfaces[wayland.cursor_surface] : 0;
        const struct wayland_buffer *b;
        const struct wayland_pool *p;
        long scale;

        out->pixels = 0;
        out->stride = out->width = out->height = out->format = out->hot_x = out->hot_y = 0;
        out->scale = 1;
        out->serial = wayland.cursor_serial;
        if (!s || !s->live || s->gen != wayland.cursor_gen || s->role != WAYLAND_ROLE_CURSOR || !s->held || row < 0 || row >= wayland.row_made ||
            row != wayland.pointer_row || wayland.row[row].client != wayland.cursor_client)
                return 0;
        b = &c->buffers[s->held - 1];
        p = &c->pools[b->pool];
        if (!b->live || !p->live || p->unmapped || !p->at || (b->format != WLN_wl_shm_format_argb8888 && b->format != WLN_wl_shm_format_xrgb8888) ||
            b->width <= 0 || b->height <= 0)
                return 0;
        scale = s->cur.scale > 0 ? s->cur.scale : 1;
        out->pixels = (const unsigned char *)p->at + b->offset;
        out->stride = b->stride;
        out->width = b->width;
        out->height = b->height;
        out->format = b->format;
        out->hot_x = wayland.cursor_hot_x * scale;
        out->hot_y = wayland.cursor_hot_y * scale;
        out->scale = scale;
        return 1;
}

#endif /* WAYLAND_INCLUDED */
