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
#define WAYLAND_POOLS 32
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

/*
        What a request answers: zero is done, negative is a client that is dead (the error is queued).
*/
#define WAYLAND_OK 0
#define WAYLAND_DEAD (-1)

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

#define WAYLAND_STATE_MAXIMIZED 1
#define WAYLAND_STATE_FULLSCREEN 2
#define WAYLAND_STATE_ACTIVATED 4
#define WAYLAND_STATE_RESIZING 8
#define WAYLAND_STATE_SUSPENDED 16
#define WAYLAND_STATE_TILED_LEFT 32
#define WAYLAND_STATE_TILED_RIGHT 64
#define WAYLAND_STATE_TILED_TOP 128
#define WAYLAND_STATE_TILED_BOTTOM 256

struct wayland_picture
{
        const unsigned char *pixels; // the buffer's first pixel
        long stride;                 // bytes a row
        long width, height;          // the buffer, in pixels
        long format;                 // WLN_wl_shm_format_*
        struct wayland_rect damage[WAYLAND_DAMAGE];
        long damages;                // how many, in buffer pixels; 0 with full set is the whole buffer
        long full;
};

struct wayland_facts
{
        long width, height; // what the compositor asks of the window, 0 for the client's own choice
        long states;        // WAYLAND_STATE_*
        long closing;       // the compositor asked the window to go
        long x, y;          // where the window is
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
        // What is shown.
        long scale, transform;
        long width, height;              // the shown buffer, in pixels
        // xdg.
        unsigned int xdg_id, role_id;
        unsigned int serial[WAYLAND_SERIALS];
        unsigned char serials;
        unsigned int last_serial;
        long geometry_x, geometry_y, geometry_width, geometry_height;
        unsigned char geometry_set;
        // A toplevel.
        char title[WAYLAND_TEXT];
        char app_id[WAYLAND_TEXT];
        long min_width, min_height, max_width, max_height;
        unsigned int parent;             // the parent toplevel's surface slot + 1
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
        long fd;
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

struct wayland_client
{
        long live, dead, slot, pid, uid;
        long in_length;
        unsigned char in[WAYLAND_MESSAGE];
        long out_length;
        unsigned char out[WAYLAND_OUT];
        long out_fds[WAYLAND_OUT_FDS], out_fd_at[WAYLAND_OUT_FDS], out_fd_count;
        long in_fds[WAYLAND_FDS], in_fd_count;
        unsigned long serial;
        long pool_bytes;
        long free_hint;
        unsigned short index[WAYLAND_IDS];
        unsigned short server_index[WAYLAND_SERVER_IDS];
        struct wayland_object objects[WAYLAND_OBJECTS];
        struct wayland_surface surfaces[WAYLAND_SURFACES];
        struct wayland_pool pools[WAYLAND_POOLS];
        struct wayland_buffer buffers[WAYLAND_BUFFERS];
        struct wayland_positioner positioners[WAYLAND_POSITIONERS];
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
        struct wayland_screen screen[WAYLAND_SCREENS];
        long screens;
        long clients, objects, surfaces, pools, buffers, fds, rows;
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
    {WLI_wl_seat, 9},
    {WLI_xdg_wm_base, 6},
    {WLI_zxdg_output_manager_v1, 3},
    {WLI_zxdg_decoration_manager_v1, 1},
};
#define WAYLAND_GLOBAL_COUNT ((long)(sizeof wayland_globals / sizeof wayland_globals[0]))

static int wayland_name_is(const unsigned char *text, long length, const char *name)
{
        long at = 0;

        for (; at < length; at++)
                if (!name[at] || text[at] != (unsigned char)name[at])
                        return 0;
        return name[at] == 0;
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
                wayland_word(&e, (unsigned long)wayland_max(1, (screen->scale + 99) / 100));
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

/* The xdg_output of an output: its place and size in the desktop's logical pixels, and done. */
static void wayland_xdg_output_send(struct wayland_client *c, long slot, long output)
{
        const struct wayland_screen *screen;
        struct wayland_event e;
        long scale;

        if (output >= wayland.screens)
                return;
        screen = &wayland.screen[output];
        scale = wayland_max(25, screen->scale);
        e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_logical_position);
        wayland_word(&e, (unsigned long)(screen->x * 100 / scale));
        wayland_word(&e, (unsigned long)(screen->y * 100 / scale));
        wayland_end(&e);
        e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_logical_size);
        wayland_word(&e, (unsigned long)(screen->width * 100 / scale));
        wayland_word(&e, (unsigned long)(screen->height * 100 / scale));
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
        e = wayland_begin(c, c->objects[slot].id, WLE_zxdg_output_v1_done);
        wayland_end(&e);
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

/*
        A surface's window, in the order a client expects it: the toplevel's size and states, the decoration's mode,
        then the xdg_surface's serial. The serial is the one the client must acknowledge; a client that has
        WAYLAND_SERIALS unacknowledged is sent no more until it does, so one that never answers cannot be sent
        without end.
*/
static void wayland_configure(struct wayland_client *c, struct wayland_surface *s, long initial)
{
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0};
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
                        bounds_w = wayland.screen[0].width * 100 / wayland_max(25, wayland.screen[0].scale);
                        bounds_h = wayland.screen[0].height * 100 / wayland_max(25, wayland.screen[0].scale);
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
                wayland_word(&e, (unsigned long)(facts.width > 0 ? facts.width : 0));
                wayland_word(&e, (unsigned long)(facts.height > 0 ? facts.height : 0));
                wayland_words(&e, states, count);
                wayland_end(&e);
                s->told_width = facts.width;
                s->told_height = facts.height;
                s->told_states = facts.states;
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
static void wayland_row_release(struct wayland_client *c, struct wayland_surface *s)
{
        if (s->row < 0)
                return;
        wayland.row_client[s->row] = wayland.row_surface[s->row] = -1;
        wayland.host->row_close(wayland.host->context, s->row);
        wayland.rows--;
        s->row = -1;
        s->flags &= ~(unsigned)WAYLAND_SURFACE_MAPPED;
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

/* A surface gone, in whatever order: its window, its callbacks, and the objects that named it inert. */
static void wayland_surface_drop(struct wayland_client *c, struct wayland_surface *s)
{
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
                wayland_global_send(c, c->objects[a[0].slot].id, name);
        for (long screen = 0; screen < wayland.screens; screen++)
                wayland_global_send(c, c->objects[a[0].slot].id, WAYLAND_OUTPUT_NAME + screen);
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

        if (name >= 1 && name <= WAYLAND_GLOBAL_COUNT)
        {
                iface = wayland_globals[name - 1].iface;
                version = wayland_globals[name - 1].version;
        }
        else if (name >= WAYLAND_OUTPUT_NAME && name < WAYLAND_OUTPUT_NAME + wayland.screens)
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
                c->objects[slot].aux = (unsigned short)(name - WAYLAND_OUTPUT_NAME);
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
        else if (iface == WLI_wl_seat)
        {
                struct wayland_event e = wayland_begin(c, id, WLE_wl_seat_capabilities);

                wayland_word(&e, WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard);
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
        c->objects[a[0].slot].link = (short)slot;
        c->objects[a[0].slot].aux = (unsigned short)s->gen;
        wayland.surfaces++;
        if (c->objects[a[0].slot].version >= 6)
        {
                struct wayland_event e = wayland_begin(c, s->id, WLE_wl_surface_preferred_buffer_scale);

                wayland_word(&e, (unsigned long)wayland_max(1, (screen_scale + 99) / 100));
                wayland_end(&e);
                e = wayland_begin(c, s->id, WLE_wl_surface_preferred_buffer_transform);
                wayland_word(&e, WLN_wl_output_transform_normal);
                wayland_end(&e);
        }
        return WAYLAND_OK;
}

static int wayland_region_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (op == WLQ_wl_region_destroy)
                return WAYLAND_OK;
        (void)a;
        (void)self;
        (void)c;
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
                        struct wayland_buffer *b, long scale)
{
        struct wayland_pool *p = &c->pools[b->pool];
        struct wayland_picture picture;
        int first = s->row < 0;

        if (first)
        {
                int parent = -1;
                struct wayland_facts facts = {0, 0, 0, 0, 0, 0};

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
        picture.full = first || s->width != b->width || s->height != b->height || s->damage_full || scale != s->scale || !s->damages;
        for (long at = 0; at < s->damages && !picture.full; at++)
        {
                struct wayland_rect r = s->damage[at];
                long x2, y2;

                if (!s->damage_kind[at])
                {
                        r.x *= scale;
                        r.y *= scale;
                        r.width *= scale;
                        r.height *= scale;
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
        if (wayland.host->row_picture(wayland.host->context, s->row, &picture) < 0)
                return wayland_fail(c, self->id, WAYLAND_IMPLEMENTATION, "the window refused the picture");
        s->flags |= WAYLAND_SURFACE_MAPPED;
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
        A commit. The pending state becomes the shown one: a buffer attached is copied (the damaged rows of it) into the
        surface's window and released at once, a null buffer unmaps the window, and the frame callbacks the client
        asked for are taken, to be answered when the frame is on the screen. A window's first commit has no buffer and
        is answered with the initial configure; a buffer before the client has acknowledged one is a protocol error.
*/
static int wayland_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        long buffer = -1, scale = s->pending_scale;
        int xdg = (s->flags & WAYLAND_SURFACE_XDG) != 0, initial = 0;

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

                if (b->width % scale || b->height % scale)
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_size, "the buffer is not a whole number of the scale");
                if (xdg && wayland_show(c, self, s, b, scale) < 0)
                        return WAYLAND_DEAD;
                s->width = b->width;
                s->height = b->height;
                s->scale = scale;
                s->transform = s->pending_transform;
                wayland_release_buffer(c, buffer);
        }
        else if (s->attach_made && !s->attached)
        {
                // A null buffer: the window goes, and what it was told is not true any more.
                if (xdg)
                {
                        wayland_row_release(c, s);
                        s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
                        s->serials = 0;
                        s->entered = 0;
                }
                s->width = s->height = 0;
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
        case WLQ_wl_surface_set_opaque_region:
        case WLQ_wl_surface_set_input_region:
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
                c->objects[a[0].slot].link = (short)slot;
                p->refs++;
                wayland.buffers++;
                return WAYLAND_OK;
        }
}

static int wayland_buffer_request(struct wayland_client *c, struct wayland_object *self)
{
        struct wayland_buffer *b = &c->buffers[self->link];

        // A surface that still names the buffer finds it gone at its commit.
        c->pools[b->pool].refs--;
        wayland_pool_unref(c, b->pool);
        b->live = 0;
        wayland.buffers--;
        return WAYLAND_OK;
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
        struct wayland_facts facts = {0, 0, 0, 0, 0, 0};
        long origin_x = 0, origin_y = 0, x, y, w, h, slot;
        struct wayland_event e;
        unsigned long serial;

        if (parent && parent->row >= 0 && wayland.host->row_facts(wayland.host->context, parent->row, &facts) >= 0)
        {
                origin_x = facts.x + parent->geometry_x;
                origin_y = facts.y + parent->geometry_y;
        }
        for (long at = 0; at < wayland.screens; at++)
                if (origin_x + p->anchor_x >= screens[at].x && origin_x + p->anchor_x < screens[at].x + screens[at].width &&
                    origin_y + p->anchor_y >= screens[at].y && origin_y + p->anchor_y < screens[at].y + screens[at].height)
                {
                        screen.x = screens[at].x;
                        screen.y = screens[at].y;
                        screen.width = screens[at].width;
                        screen.height = screens[at].height;
                }
        wayland_popup_place(p, origin_x, origin_y, &screen, &x, &y, &w, &h);
        s->popup_x = x + (parent ? parent->geometry_x : 0);
        s->popup_y = y + (parent ? parent->geometry_y : 0);
        s->popup_width = w;
        s->popup_height = h;
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
                if (row >= 0)
                        wayland.host->row_do(wayland.host->context, (int)row, WAYLAND_DO_MOVE, (long)a[1].u, 0);
                return WAYLAND_OK;
        case WLQ_xdg_toplevel_resize:
                if (a[2].u > WLN_xdg_toplevel_resize_edge_bottom_right || a[2].u == 3 || a[2].u == 7 || a[2].u == 11 || a[2].u == 12 ||
                    a[2].u == 13 || a[2].u == 14)
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_resize_edge, "no such edge");
                if (row >= 0 && a[2].u)
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

/* ---- seat ---- */

static int wayland_seat_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        switch (op)
        {
        case WLQ_wl_seat_get_pointer:
        case WLQ_wl_seat_get_keyboard:
                (void)a;
                return WAYLAND_OK;
        case WLQ_wl_seat_get_touch:
                return wayland_fail(c, self->id, WLN_wl_seat_error_missing_capability, "the seat has no touch");
        default:
                return WAYLAND_OK;
        }
}

/* ---- one request ---- */

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
                case WLI_wl_seat:
                        status = wayland_seat_request(c, o, op, a);
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
            !host->row_facts || !host->screens || !host->now)
                return -1;
        for (unsigned long at = 0; at < sizeof wayland; at++)
                clear[at] = 0;
        for (long row = 0; row < WAYLAND_ROWS; row++)
                wayland.row_client[row] = wayland.row_surface[row] = -1;
        wayland.host = host;
        wayland.screens = host->screens(host->context, wayland.screen, WAYLAND_SCREENS);
        if (wayland.screens < 0)
                wayland.screens = 0;
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
        A client gone, however it went: every window it had, every pool, every descriptor it sent and the shim did not
        use, and its record. The one place that gives back what a client held.
*/
WAYLAND_API void wayland_drop(long slot)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS ? wayland.client[slot] : 0;

        if (!c)
                return;
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
                        wayland.pools--;
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
WAYLAND_API int wayland_feed(long slot, const unsigned char *bytes, long length, const long *fds, long count)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;
        int status = WAYLAND_OK;

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
        while (length > 0 && !c->dead)
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
                bytes += size;
                length -= size;
        }
        return c->dead ? WAYLAND_DEAD : status;
}

/*
        What waits to be sent to a client: its bytes, and the descriptors that go with them (the first byte's ancillary
        data). wayland_sent takes what was written off, from the front; a send that wrote any bytes sent the descriptors.
*/
WAYLAND_API long wayland_out(long slot, const unsigned char **bytes, const long **fds, long *fd_count)
{
        struct wayland_client *c = slot >= 0 && slot < WAYLAND_CLIENTS && wayland.host ? wayland.client[slot] : 0;

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
        c->out_fd_count = 0;
}

/* The compositor's screens, changed: each output a client bound is told, and so is each xdg_output. */
WAYLAND_API void wayland_screens_changed(void)
{
        if (!wayland.host)
                return;
        wayland.screens = wayland.host->screens(wayland.host->context, wayland.screen, WAYLAND_SCREENS);
        if (wayland.screens < 0)
                wayland.screens = 0;
        for (long slot = 0; slot < WAYLAND_CLIENTS; slot++)
        {
                struct wayland_client *c = wayland.client[slot];

                for (long at = 0; c && at < WAYLAND_OBJECTS; at++)
                {
                        if (c->objects[at].iface == WLI_wl_output)
                                wayland_output_send(c, at);
                        else if (c->objects[at].iface == WLI_zxdg_output_v1)
                                wayland_xdg_output_send(c, at, c->objects[at].aux);
                }
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
        now = wayland.host->now(wayland.host->context);
        for (long row = 0; row < WAYLAND_ROWS; row++)
        {
                struct wayland_client *c = wayland.row_client[row] >= 0 ? wayland.client[wayland.row_client[row]] : 0;
                struct wayland_surface *s = c ? &c->surfaces[wayland.row_surface[row]] : 0;
                struct wayland_facts facts;

                if (!s || !s->live || !s->readies)
                        continue;
                if (wayland.host->row_facts(wayland.host->context, (int)row, &facts) >= 0 && (facts.states & WAYLAND_STATE_SUSPENDED))
                        continue;
                wayland_callbacks_done(c, s, now);
        }
}

/* Whether any callback waits for a frame, so the compositor knows to come back for one. */
WAYLAND_API long wayland_frame_wanted(void)
{
        for (long row = 0; wayland.host && row < WAYLAND_ROWS; row++)
        {
                struct wayland_client *c = wayland.row_client[row] >= 0 ? wayland.client[wayland.row_client[row]] : 0;

                if (c && c->surfaces[wayland.row_surface[row]].readies)
                        return 1;
        }
        return 0;
}

/* The client a row belongs to, or -1: the compositor asks before it treats a row as a program's. */
WAYLAND_API long wayland_row_owner(long row)
{
        return row >= 0 && row < WAYLAND_ROWS ? wayland.row_client[row] : -1;
}

#endif /* WAYLAND_INCLUDED */
