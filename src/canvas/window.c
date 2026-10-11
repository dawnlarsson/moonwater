/*
        Moonwater window

        The whole interface a program needs to put something on screen. Include
        this one file and nothing else: it carries its own syscalls, so it
        works from a spark image with no runtime and from an ordinary ELF
        against libc.

                struct window *w = window_open(640, 480);

                unsigned int *pixels = window_pixels(w);
                pixels[y * w->pitch + x] = 0x00ff9900;

                w->x = 200;
                w->y = 120;
                window_commit(w);

        The struct is a page the program and the compositor both have mapped,
        so moving or resizing a window is a store, not a call. The fields above
        the line are the program's to write and the compositor reads them; the
        ones below are the compositor's and the program reads them.

        The pixels are the window's contents. The compositor draws the frame
        and the titlebar around them, so x,y is the top left of the whole
        window and the contents land WINDOW_TITLE below it.

        z is the order they stack in: higher is in front. Clicking a window
        puts it above every other, and the compositor writes the new value
        back, so a program can always read where it ended up.

        region is where a window wants to be rather than where it is. Zero is
        free floating and x,y are the program's; anything else is the
        compositor's placement and it writes x,y to say where that landed. It
        is a number and not a flag because there is more than one answer --
        centred is the one that exists so far.

        style is what the window looks like: whether it wears a frame, whether
        it covers its display, whether it is put away. A new window starts
        framed.

        edge is the corner radius, in pixels. Zero is square.

        awake says whether the compositor is currently watching the shared
        pages. window_commit only makes a call when it is not, so a program
        drawing every frame makes none at all. The first commit after opening a
        window is always a call, so whatever the compositor decides -- where a
        region put the window, how large its display is -- is there to read as
        soon as it returns.
*/

#ifndef WINDOW_INCLUDED
#define WINDOW_INCLUDED

// Version 2's numbers, which the compositor's ABI header holds.
#include "canvas_abi.h"

#define WINDOW_DEVICE "/dev/spark"

// O_RDWR | O_CLOEXEC, which are the same numbers on all three architectures.
// A window is this program's, not whatever it goes on to exec.
#define WINDOW_OPEN_FLAGS 0x80002u

// The contents start one page in, so the struct and the pixels are one mapping
// and the pixels stay page aligned.
#define WINDOW_PIXELS 4096

// The titlebar the compositor draws above the contents.
#define WINDOW_TITLE 20

// region
#define WINDOW_FREE 0
#define WINDOW_CENTRED 1

// style
#define WINDOW_FRAME 1u
#define WINDOW_FULLSCREEN 2u
#define WINDOW_MINIMIZED 4u

/*
        A window takes the pointer over the whole of itself, not only over the
        parts that do something with it: hovering its middle must not offer to
        resize whatever is buried underneath. WINDOW_PASSTHROUGH says not to,
        for a window that is meant to be looked through rather than used.
*/
#define WINDOW_PASSTHROUGH 8u

/* Keep pane_raise's top + 1 away from overflow on an untrusted shared page. */
#define WINDOW_Z_MAX (1 << 24)

/*
        The same rule for where a window says it is.

        A window is allowed off the side of the screen -- that is what dragging
        one half out of view is -- so this is not a placement policy and is far
        wider than any desktop. It is only far enough inside the type that the
        compositor can add a frame, a border and a corner radius to it without
        the arithmetic wrapping, because every rectangle it cuts, damages and
        clips is worked out from this number.
*/
#define WINDOW_COORD_MAX (1 << 24)

// state
#define WINDOW_FOCUSED 1u

/*
        The compositor is asking this window to go away.

        Asking, because it cannot do more: the pages are the program's and the
        task is the kernel's, and a compositor that could end either would be a
        compositor that can be told to end any of them. The X in the titlebar
        sets this and wakes the program; what happens next is the program's,
        and one that ignores it keeps its window.
*/
#define WINDOW_CLOSING 2u

/*
        Text layout. Alignment is one field: a horizontal one, a vertical one,
        and whether long text wraps at the width of its box or runs off the end
        of one line.
*/
#define TEXT_LEFT 0u
#define TEXT_CENTRE 1u
#define TEXT_RIGHT 2u
#define TEXT_TOP 0u
#define TEXT_MIDDLE 4u
#define TEXT_BOTTOM 8u
#define TEXT_WRAP 16u

/*
        Keys reach the window that has focus, through a ring in this page.

        The compositor writes at head and the program reads at tail, so a
        program that draws every frame reads its input the same way it reads
        everything else: no call. One side writes head and the other writes
        tail, and nothing writes both -- so a ring that fills drops what
        arrived last, because making room for it would mean moving the other
        side's index.
*/
#define WINDOW_KEYS 64

#define WINDOW_KEY_DOWN 1u
#define WINDOW_KEY_SHIFT 2u
#define WINDOW_KEY_CONTROL 4u
#define WINDOW_KEY_ALT 8u
#define WINDOW_KEY_POINTER 16u
#define WINDOW_KEY_POINTER_MOVE 32u
#define WINDOW_KEY_ALTGR 64u

struct window_key
{
        unsigned int code;      // the key itself, as Linux numbers them
        unsigned int character; // what it means, or zero for a key that means
                                // nothing on its own
        unsigned int flags;
        unsigned int reserved;
};

// A window is never resized below this, whatever is dragged.
#define WINDOW_MIN_WIDTH 96
#define WINDOW_MIN_HEIGHT 48

// How long a title can be, which is the one string that lives in this page
// rather than in a window's own content.
#define WINDOW_TITLE_MAX 128

struct window
{
        // The program writes these, and the compositor writes x, y and z back
        // when it is the one that moved them.
        int x, y, z; // higher is in front, clamped to +/-WINDOW_Z_MAX
        unsigned int width, height;
        unsigned int region;  // WINDOW_FREE, WINDOW_CENTRED
        unsigned int display; // which output a region is measured against
        unsigned int style;
        unsigned int edge;     // corner radius
        unsigned int sequence; // bump after drawing
        char title[WINDOW_TITLE_MAX];

        // The compositor writes these.
        unsigned int state;         // WINDOW_FOCUSED
        unsigned int pitch;         // pixels per row of the contents
        unsigned int max_width;     // what the mapping actually holds
        unsigned int max_height;
        unsigned int display_width; // the output this window is on
        unsigned int display_height;
        unsigned int awake;         // whether the compositor is watching

        // window_open's own bookkeeping. The compositor never reads it.
        unsigned int handle;

        /*
                A window of cells is a ring of lines rather than a grid.

                stride is how far apart two lines are, which starts as wide
                as the window opens and only grows, when the program asks
                with window_widen; the mapping has room for lines as wide as
                the desktop could ever make the window. history is how many
                lines the ring holds; head is one past the newest. The visible rows are the last of them, so scrolling is
                a store to head rather than a copy of every row, and what goes
                past the top is still there to scroll back to.

                lines is where the length of each one lives, as a byte offset
                into the mapping. A line is as long as it was written, not as
                wide as the window: the wrapping happens when it is drawn, so
                a window made wider re-wraps what it already had.

                The program writes head and the lengths; the rest is the
                compositor's.
        */
        unsigned int stride;
        unsigned int history;
        unsigned int head;
        unsigned int lines;

        // The compositor writes head, the program writes tail.
        unsigned int key_head;
        unsigned int key_tail;
        struct window_key keys[WINDOW_KEYS];

        // For cells, width includes the scrollbar gutter; columns excludes it.
        // The grid's shape, and which rows have changed since
        // the compositor last looked. Rows rather than a rectangle because
        // that is what text changes.
        unsigned int columns;
        unsigned int rows;
        unsigned int damage_row;
        unsigned int damage_rows;

        /*
                The shape the cells are actually laid out in, which the
                program writes and the compositor composes from.

                columns and rows above are a request: the compositor rounds a
                resize to whole cells and says so, and until the program has
                relaid its text the buffer is still in the old shape. Reading
                it at the new stride during a drag is a row of text taken from
                the middle of the row before it.
        */
        unsigned int grid_columns;
        unsigned int grid_rows;

        // What the compositor allocated, which is what has to be mapped: a
        // window of cells is given room for as many as the desktop could hold
        // so that resizing it has somewhere to grow into.
        unsigned int max_columns;
        unsigned int max_rows;
        unsigned int mapping;

        unsigned int want;
};

// Whether the compositor has asked this window to close. A program that draws
// in a loop reads this where it reads its keys.
static inline int window_closing(const struct window *window)
{
        return (int)(window->state & WINDOW_CLOSING);
}

static inline void window_damage(struct window *window, unsigned int row,
                                 unsigned int rows)
{
        unsigned int last = row + rows;
        unsigned int previous;

        if (!rows)
                return;

        // A count that runs off the end saturates: wrapping would turn the
        // widest possible damage into none at all.
        if (last < row)
                last = ~0u;

        if (!window->damage_rows)
        {
                window->damage_row = row;
                window->damage_rows = last - row;
                return;
        }

        previous = window->damage_row + window->damage_rows;

        if (row < window->damage_row)
                window->damage_row = row;

        if (previous > last)
                last = previous;

        window->damage_rows = last - window->damage_row;
}

// _IOW('s', 4, struct window_request)
#define WINDOW_IOCTL_CREATE 0x40107304u

// _IO('s', 5) -- redraw what changed
#define WINDOW_IOCTL_COMMIT 0x00007305u

// _IO('s', 16) -- spread the lines of a window of cells out to at least this
// many columns; answers the stride they are now
#define WINDOW_IOCTL_STRIDE 0x00007310u

struct window_request
{
        unsigned int width;
        unsigned int height;
        unsigned int columns; // non-zero asks for cells rather than pixels,
        unsigned int rows;    // and decides the size
};

/*
        A cell, for a window made of text.

        A program that draws with words writes eight bytes a character and the
        compositor draws the glyph, rather than the program writing the five
        hundred and twelve bytes of an eight by sixteen cell itself with a font
        of its own.

        The colours are indices into the two hundred and fifty six an xterm
        has: the first sixteen are the ones a terminal has always had, and
        the rest are the cube and the greys. A cell still carries an index
        rather than a colour, so the compositor can look the colour up once.

        flags is what SGR could not fold into those indices: underline, a
        faint ink, a hidden glyph. Reverse is applied when the cell is
        written, not stored.

        It is also what makes scrolling a store to one number: the cells are a
        ring of lines, and moving on is naming the next one.
*/
struct window_cell
{
        unsigned int character;
        unsigned char ink;
        unsigned char paper;
        unsigned short flags;
};

// The colour an index names, as 0xrrggbb.
static inline unsigned int window_cell_colour(unsigned int index)
{
        static const unsigned int sixteen[16] = {
            0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd,
            0xe5e5e5, 0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff,
            0x00ffff, 0xffffff,
        };
        static const unsigned char level[6] = {0, 95, 135, 175, 215, 255};

        if (index < 16)
                return sixteen[index];

        if (index < 232)
        {
                index -= 16;
                return (unsigned int)level[index / 36] << 16 |
                       (unsigned int)level[index / 6 % 6] << 8 | level[index % 6];
        }

        return (8 + (index - 232) % 24 * 10) * 0x010101u;
}

#define WINDOW_CELL_BOLD 1u
#define WINDOW_CELL_DIM 2u
#define WINDOW_CELL_ITALIC 4u
#define WINDOW_CELL_UNDERLINE 8u
#define WINDOW_CELL_BLINK 16u
#define WINDOW_CELL_HIDDEN 32u
#define WINDOW_CELL_STRIKE 64u

/* A character two columns wide takes two cells: itself with WINDOW_CELL_WIDE
   in the first, and again with WINDOW_CELL_WIDE_RIGHT in the second, so what
   follows it lands in the column the program counted. */
#define WINDOW_CELL_WIDE 128u
#define WINDOW_CELL_WIDE_RIGHT 256u

/* A bar down the left edge of the cell: the terminal's cursor when a program
   has asked for a bar rather than a block. */
#define WINDOW_CELL_BAR 512u

/* The program writes this; the compositor reads it. A terminal that has
   asked for mouse tracking sets WINDOW_WANT_POINTER so clicks, drags and
   the wheel reach its key ring instead of only moving the desktop. */
#define WINDOW_WANT_POINTER 1u

#define WINDOW_CELL_W 8
#define WINDOW_CELL_H 16

#define window_at(window, type, offset)                                       \
        ((type *)((char *)(window) + (offset)))
#define window_cells(window) window_at((window), struct window_cell, WINDOW_PIXELS)

// One line of the ring. Any index at all: it is taken modulo the history, so
// counting forever is what a caller is meant to do with head.
static inline struct window_cell *window_line(struct window *window,
                                              unsigned int index)
{
        return window_cells(window) + (index % window->history) * window->stride;
}

// How much of each line has been written. Nothing past this is drawn, so a
// line that shrinks does not leave the rest of what was there on the screen.
#define window_lengths(window) window_at((window), unsigned int, (window)->lines)

static inline unsigned int *window_length(struct window *window,
                                          unsigned int index)
{
        return window_lengths(window) + index % window->history;
}

/*
        A line has been finished and the next one begins.

        The one being moved to is cleared before head names it, so the
        compositor never draws the line the ring is about to hand back.
*/
static inline void window_scroll(struct window *window)
{
        *window_length(window, window->head) = 0;

        __atomic_store_n(&window->head, window->head + 1, __ATOMIC_RELEASE);
}

// Says the cells are now laid out this way. Until a program calls this the
// compositor keeps drawing the shape it last confirmed, so a window of cells
// that never calls it never reflows.
static inline void window_grid(struct window *window, unsigned int columns,
                               unsigned int rows)
{
        window->grid_columns = columns;
        window->grid_rows = rows;
}

// The window has been given a different shape than its cells are laid out in.
// Lay them out again at columns by rows, then say so with window_grid.
static inline int window_regrid(struct window *window)
{
        return window->columns != window->grid_columns ||
               window->rows != window->grid_rows;
}

// The next key, or false when there is none waiting.
static inline int window_key(struct window *window, struct window_key *key)
{
        // The entry was written before head was bumped, so head has to be
        // read before the entry for that order to hold on arm64 and riscv.
        unsigned int head = __atomic_load_n(&window->key_head, __ATOMIC_ACQUIRE);

        if (window->key_tail == head)
                return 0;

        *key = window->keys[window->key_tail % WINDOW_KEYS];

        // The slot is only free once it has been read out of.
        __atomic_store_n(&window->key_tail, window->key_tail + 1, __ATOMIC_RELEASE);

        return 1;
}

#define window_pixels(window) window_at((window), unsigned int, WINDOW_PIXELS)

/*
        A surface: protocol version 2's page.

        The version 1 struct first, every field where it was, then the rest of
        the header (canvas spec 01 section 4), and the buffers from
        WINDOW_PIXELS on. Of the version 1 half a surface uses the position,
        style, title, sequence, state and want; its contents are buffers, not
        the cells or the key ring.

        Who writes what: the program writes the requests, buffer_index, the
        damage list, acked_serial, the style requests and event_tail; the
        compositor writes everything it decides or applies and event_head.
        The compositor reads every field the program writes as hostile and
        keeps its own copy of the geometry it checked at create.

        A buffer is XR24: 0x00rrggbb a pixel, buffer_stride bytes a row. Two
        slots let a program draw the next frame while the last is still on
        the screen; release_serial is the last commit the compositor has
        finished reading.
*/
struct surface_damage
{
        int x, y, width, height;
};

struct surface_style
{
        unsigned int id;
        int value;
};

struct surface_event
{
        unsigned short type;
        unsigned short flags;
        unsigned int serial;
        int a, b;
};

#define SURFACE_DAMAGE_MAX 8
#define SURFACE_STYLES 16
#define SURFACE_EVENTS 64

struct surface
{
        struct window window;

        unsigned int magic;
        unsigned short protocol;
        unsigned short header_size;
        unsigned int role_requested;
        unsigned int role_state;
        int role_result;
        unsigned int layer;
        unsigned int anchor_requested;
        unsigned int anchor_applied;
        int exclusive_requested;
        unsigned int exclusive_applied;
        unsigned int lock_applied;
        unsigned int parent;
        unsigned int surface_id;
        int rel_x, rel_y;
        unsigned int configure_serial;
        unsigned int acked_serial;
        unsigned int applied_width;
        unsigned int applied_height;
        unsigned int buffer_slots;
        unsigned int buffer_index;
        unsigned int buffer_kind;
        unsigned int buffer_format;
        unsigned int buffer_stride;
        unsigned int buffer_size;
        unsigned int buffer_offset[2];
        unsigned int release_serial;
        unsigned int damage_count; // 0, or above SURFACE_DAMAGE_MAX, is all of it
        struct surface_damage damage[SURFACE_DAMAGE_MAX];
        unsigned int style_count;
        unsigned int style_serial;
        struct surface_style style_request[SURFACE_STYLES];
        struct surface_style style_applied[SURFACE_STYLES]; // {value, status}
        unsigned int event_head;
        unsigned int event_tail;
        unsigned int event_dropped;
        unsigned int reserved;
        struct surface_event event[SURFACE_EVENTS];
        unsigned int theme_generation;
        unsigned int theme_foreground;
        unsigned int theme_background;
        unsigned int theme_cursor;
        unsigned int theme_palette[16];
};

_Static_assert(sizeof(struct window) == 1288, "window page ABI");
_Static_assert(sizeof(struct surface_event) == 16, "surface event ABI");
_Static_assert(sizeof(struct surface_damage) == 16, "surface ABI");
_Static_assert(sizeof(struct surface_style) == 8, "surface ABI");

/* Every field of canvas spec 01 section 4.1, at its number: a field that
   moves is a version that is not 2. */
#define SURFACE_AT(field, offset)                                              \
        _Static_assert(__builtin_offsetof(struct surface, field) == (offset), "surface ABI " #field)
SURFACE_AT(magic, 1288);
SURFACE_AT(protocol, 1292);
SURFACE_AT(header_size, 1294);
SURFACE_AT(role_requested, 1296);
SURFACE_AT(role_state, 1300);
SURFACE_AT(role_result, 1304);
SURFACE_AT(layer, 1308);
SURFACE_AT(anchor_requested, 1312);
SURFACE_AT(anchor_applied, 1316);
SURFACE_AT(exclusive_requested, 1320);
SURFACE_AT(exclusive_applied, 1324);
SURFACE_AT(lock_applied, 1328);
SURFACE_AT(parent, 1332);
SURFACE_AT(surface_id, 1336);
SURFACE_AT(rel_x, 1340);
SURFACE_AT(rel_y, 1344);
SURFACE_AT(configure_serial, 1348);
SURFACE_AT(acked_serial, 1352);
SURFACE_AT(applied_width, 1356);
SURFACE_AT(applied_height, 1360);
SURFACE_AT(buffer_slots, 1364);
SURFACE_AT(buffer_index, 1368);
SURFACE_AT(buffer_kind, 1372);
SURFACE_AT(buffer_format, 1376);
SURFACE_AT(buffer_stride, 1380);
SURFACE_AT(buffer_size, 1384);
SURFACE_AT(buffer_offset, 1388);
SURFACE_AT(release_serial, 1396);
SURFACE_AT(damage_count, 1400);
SURFACE_AT(damage, 1404);
SURFACE_AT(style_count, 1532);
SURFACE_AT(style_serial, 1536);
SURFACE_AT(style_request, 1540);
SURFACE_AT(style_applied, 1668);
SURFACE_AT(event_head, 1796);
SURFACE_AT(event_tail, 1800);
SURFACE_AT(event_dropped, 1804);
SURFACE_AT(reserved, 1808);
SURFACE_AT(event, 1812);
SURFACE_AT(theme_generation, 2836);
SURFACE_AT(theme_foreground, 2840);
SURFACE_AT(theme_background, 2844);
SURFACE_AT(theme_cursor, 2848);
SURFACE_AT(theme_palette, 2852);
_Static_assert(sizeof(struct surface) == SURFACE_HEADER, "surface ABI");
_Static_assert(SURFACE_HEADER <= WINDOW_PIXELS, "the header ends before the first buffer");

/* Where a slot's pixels start, and how much a page of two slots holds.

   surface_buffer is the program's: it reads buffer_offset, which the
   compositor wrote in its answer to the create. The page is the program's
   to write as well, so the compositor never reads buffer_offset, buffer_size,
   buffer_stride or mapping back -- an offset the program changed after the
   create sends the blit anywhere in the compositor's address space, and
   0x7ffff000 at byte 1388 is a segfault. The compositor finds a slot from
   the size it checked itself: surface_slot. */
#define SURFACE_PAGE_ROUND(bytes) (((bytes) + 4095u) & ~4095u)
#define surface_buffer(surface, slot)                                          \
        window_at((surface), unsigned int, (surface)->buffer_offset[(slot) & 1])
#define surface_slot(surface, slot, width, height)                             \
        window_at((surface), unsigned int,                                     \
                  WINDOW_PIXELS + ((slot) & 1u) *                              \
                                      SURFACE_PAGE_ROUND((unsigned long)(width) * (height) * 4))

#ifndef KERNEL_MODE

#if defined(__x86_64__)
#define WINDOW_SYS_OPENAT 257
#define WINDOW_SYS_MMAP 9
#define WINDOW_SYS_MUNMAP 11
#define WINDOW_SYS_IOCTL 16
#define WINDOW_SYS_CLOSE 3
#define WINDOW_SYS_PPOLL 271
#define WINDOW_SYS_SOCKET 41
#define WINDOW_SYS_CONNECT 42
#define WINDOW_SYS_SENDMSG 46
#define WINDOW_SYS_RECVMSG 47
#define WINDOW_SYS_FCNTL 72
#define WINDOW_SYS_FTRUNCATE 77
#define WINDOW_SYS_MEMFD_CREATE 319
#define WINDOW_SYS_RECVMMSG 299
#else
// arm64 and riscv share the asm-generic numbering.
#define WINDOW_SYS_OPENAT 56
#define WINDOW_SYS_MMAP 222
#define WINDOW_SYS_MUNMAP 215
#define WINDOW_SYS_IOCTL 29
#define WINDOW_SYS_CLOSE 57
#define WINDOW_SYS_PPOLL 73
#define WINDOW_SYS_SOCKET 198
#define WINDOW_SYS_CONNECT 203
#define WINDOW_SYS_SENDMSG 211
#define WINDOW_SYS_RECVMSG 212
#define WINDOW_SYS_FCNTL 25
#define WINDOW_SYS_FTRUNCATE 46
#define WINDOW_SYS_MEMFD_CREATE 279
#define WINDOW_SYS_RECVMMSG 243
#endif

/*
        A shell image already carries lib.c's hardware-floor syscall
        entry on every supported architecture. Use it directly instead of
        emitting a second syscall body in every program that includes this
        client. window.c remains genuinely standalone: without the library's
        include guard it retains the small per-architecture fallback below.

        positive and long are both one register wide here. Casting a negative
        AT_FDCWD through positive preserves its bits, and the raw negative
        kernel answer becomes long again at the call site.
*/
#ifdef STANDARD_MODERN_C
#define window_call(number, a, b, c, d, e, f)                                \
        ((long)system_call_6((positive)(number), (positive)(a),              \
                             (positive)(b), (positive)(c), (positive)(d),    \
                             (positive)(e), (positive)(f)))
#elif defined(__x86_64__)

static long window_call(long number, long a, long b, long c, long d, long e, long f)
{
        register long r10 __asm__("r10") = d;
        register long r8 __asm__("r8") = e;
        register long r9 __asm__("r9") = f;
        long result;

        __asm__ volatile("syscall"
                         : "=a"(result)
                         : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
        return result;
}
#else
#if defined(__aarch64__)
static long window_call(long number, long a, long b, long c, long d, long e, long f)
{
        register long x8 __asm__("x8") = number;
        register long x0 __asm__("x0") = a;
        register long x1 __asm__("x1") = b;
        register long x2 __asm__("x2") = c;
        register long x3 __asm__("x3") = d;
        register long x4 __asm__("x4") = e;
        register long x5 __asm__("x5") = f;

        __asm__ volatile("svc #0"
                         : "+r"(x0)
                         : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                         : "memory");
        return x0;
}
#else
static long window_call(long number, long a, long b, long c, long d, long e, long f)
{
        register long a7 __asm__("a7") = number;
        register long a0 __asm__("a0") = a;
        register long a1 __asm__("a1") = b;
        register long a2 __asm__("a2") = c;
        register long a3 __asm__("a3") = d;
        register long a4 __asm__("a4") = e;
        register long a5 __asm__("a5") = f;

        __asm__ volatile("ecall"
                         : "+r"(a0)
                         : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
                         : "memory");
        return a0;
}
#endif
#endif

/*
        One file per window: the ioctl sizes it and the mapping is at offset
        zero, so there is no offset for either side to get wrong, and closing
        the file is what destroys the window.
*/
static struct window *window_request_open(struct window_request request)
{
        long bytes;
        struct window *window;
        long file, mapped;

        file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_DEVICE,
                           WINDOW_OPEN_FLAGS, 0, 0, 0);
        if (file < 0)
                return 0;

        // The compositor answers with how much it allocated, which is not
        // always what was asked for.
        bytes = window_call(WINDOW_SYS_IOCTL, file, WINDOW_IOCTL_CREATE,
                            (long)&request, 0, 0, 0);

        if (bytes <= 0)
        {
                window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                return 0;
        }

        // PROT_READ | PROT_WRITE, MAP_SHARED
        mapped = window_call(WINDOW_SYS_MMAP, 0, (unsigned long)bytes, 3, 1, file, 0);
        if (mapped < 0 && mapped > -4096)
        {
                window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                return 0;
        }

        window = (struct window *)mapped;
        window->handle = (unsigned int)file;

        return window;
}

static struct window *window_open(unsigned int width, unsigned int height)
{
        struct window_request request = {width, height, 0, 0};

        return window_request_open(request);
}

// A window of text. The compositor decides how large that is in pixels.
static struct window *window_open_text(unsigned int columns, unsigned int rows)
{
        struct window_request request = {0, 0, columns, rows};

        return window_request_open(request);
}

/*
        Sleeps until there is something for the program: a key in its ring,
        a grid the compositor changed, or the other descriptor -- a pty
        master, for a terminal -- having bytes. Napping and looking was how
        a terminal used to find a keystroke, and every keystroke paid half
        the nap on average; the compositor wakes this the moment it writes
        the key, so what is left is the wakeup itself.

        A negative timeout is no timeout. Answers what ppoll answers: how
        many descriptors are ready, zero on the timeout, negative on refusal.
*/
struct window_poll
{
        int handle;
        short events;
        short returned;
};

static long window_wait(struct window *window, long other, long nanoseconds)
{
        struct window_poll wanted[2] = {{(int)window->handle, 1, 0}, {(int)other, 1, 0}};
        long timeout[2] = {nanoseconds / 1000000000, nanoseconds % 1000000000};

        return window_call(WINDOW_SYS_PPOLL, (long)wanted, other >= 0 ? 2 : 1,
                           nanoseconds < 0 ? 0 : (long)timeout, 0, 0, 0);
}

/*
        Bumping sequence is what says a window changed. While the compositor is
        awake it is already reading that, so there is nothing else to do; the
        call below only happens on the first change after it went to sleep.
*/
static void window_commit(struct window *window)
{
        // Release, so the compositor cannot see the bump before the pixels,
        // cells or damage rows it is the announcement of.
        __atomic_store_n(&window->sequence, window->sequence + 1, __ATOMIC_RELEASE);

        /*
                The compositor clears awake and then looks at sequence one more
                time. This store has to be visible before that load for the
                pair to work, or the last frame of an animation is the one that
                never arrives.
        */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        if (!__atomic_load_n(&window->awake, __ATOMIC_ACQUIRE))
                window_call(WINDOW_SYS_IOCTL, window->handle, WINDOW_IOCTL_COMMIT, 0, 0, 0, 0);
}

/*
        The same, but now rather than within a frame.

        window_commit leaves it to the compositor's own timer while that is
        running, which is right for something animating and wrong for
        something echoing a keystroke: the letter would wait up to a frame for
        a clock it has no reason to be on. This one always makes the call.
*/
static void window_flush(struct window *window)
{
        __atomic_store_n(&window->sequence, window->sequence + 1, __ATOMIC_RELEASE);
        window_call(WINDOW_SYS_IOCTL, window->handle, WINDOW_IOCTL_COMMIT, 0, 0, 0, 0);
}

/*
        A window of cells given more columns than its lines are apart.

        A line is only as far from the next as the window was wide when it
        opened, because a line a page apart is a scrolled terminal holding
        two megabytes for text a quarter as wide. A program laying its text
        out at more columns than stride calls this first: the compositor
        moves every line out to a wider stride and stride says what it is.
        Lines already written keep their text; nothing narrower is ever
        asked for, so nothing is ever cut.
*/
static void window_widen(struct window *window, unsigned int columns)
{
        if (columns > window->stride)
                window_call(WINDOW_SYS_IOCTL, window->handle, WINDOW_IOCTL_STRIDE,
                            columns, 0, 0, 0);
}

static void window_close(struct window *window)
{
        long file = window->handle;

        window_call(WINDOW_SYS_MUNMAP, (long)window, window->mapping, 0, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
}

/*
        Version 2: a surface on the compositor that runs in userspace.

        The program makes the page -- a memfd sealed so it can never shrink
        under the compositor's mapping -- and sends it with the create
        message; the answer says whether the compositor took it. handle is
        the connection, and closing it is the surface going away.
*/
struct surface_iovec
{
        void *base;
        unsigned long length;
};

struct surface_msghdr
{
        void *name;
        unsigned int name_length;
        struct surface_iovec *iov;
        unsigned long iov_count;
        void *control;
        unsigned long control_length;
        int flags;
};

// One descriptor's worth of SCM_RIGHTS: cmsg_len, cmsg_level, cmsg_type.
struct surface_rights
{
        unsigned long length;
        int level, type;
        int handle, pad;
};

#define SURFACE_AF_UNIX 1
#define SURFACE_SOCK_SEQPACKET 5
#define SURFACE_SOCK_CLOEXEC 0x80000
#define SURFACE_MSG_DONTWAIT 0x40
#define SURFACE_MSG_NOSIGNAL 0x4000
#define SURFACE_MFD_CLOEXEC 1u
#define SURFACE_MFD_ALLOW_SEALING 2u
#define SURFACE_F_ADD_SEALS 1033
// F_SEAL_SEAL, F_SEAL_SHRINK and F_SEAL_GROW: the size is fixed for good.
#define SURFACE_SEAL_FIXED (1u | 2u | 4u)

// What the last surface_open was refused with, as a negative errno.
static long surface_refused;

static long surface_send(long handle, struct surface_message *message, int file)
{
        struct surface_iovec part = {message, sizeof(*message)};
        struct surface_rights rights = {16 + sizeof(int), 1, 1, file, 0};
        struct surface_msghdr header = {0, 0, &part, 1, 0, 0, 0};
        long sent;

        if (file >= 0)
        {
                header.control = &rights;
                header.control_length = sizeof(rights);
        }

        // A signal in a call that had to wait is not the call failing.
        do
                sent = window_call(WINDOW_SYS_SENDMSG, handle, (long)&header,
                                   SURFACE_MSG_NOSIGNAL, 0, 0, 0);
        while (sent == -4);

        return sent;
}

static long surface_receive(long handle, struct surface_message *message, int flags)
{
        struct surface_iovec part = {message, sizeof(*message)};
        struct surface_msghdr header = {0, 0, &part, 1, 0, 0, 0};

        long got;

        do
                got = window_call(WINDOW_SYS_RECVMSG, handle, (long)&header, flags, 0, 0, 0);
        while (got == -4);

        return got;
}

static struct surface *surface_open(unsigned int width, unsigned int height,
                                    unsigned int role)
{
        struct {
                unsigned short family;
                char path[sizeof(SURFACE_SOCKET)];
        } where = {SURFACE_AF_UNIX, SURFACE_SOCKET};
        struct surface_message message = {SURFACE_CREATE, 0, {0}};
        unsigned long slot, bytes;
        long handle, file, mapped;

        surface_refused = -22; // EINVAL
        if (!width || !height || width > 16384 || height > 16384)
                return 0;

        slot = SURFACE_PAGE_ROUND((unsigned long)width * height * 4);
        bytes = WINDOW_PIXELS + slot * 2;

        handle = window_call(WINDOW_SYS_SOCKET, SURFACE_AF_UNIX,
                             SURFACE_SOCK_SEQPACKET | SURFACE_SOCK_CLOEXEC, 0, 0, 0, 0);
        if (handle < 0)
                return surface_refused = handle, (struct surface *)0;

        surface_refused = window_call(WINDOW_SYS_CONNECT, handle, (long)&where,
                                      sizeof(where), 0, 0, 0);
        file = surface_refused < 0 ? surface_refused
                                   : window_call(WINDOW_SYS_MEMFD_CREATE, (long)"surface",
                                                 SURFACE_MFD_CLOEXEC | SURFACE_MFD_ALLOW_SEALING,
                                                 0, 0, 0, 0);
        mapped = file < 0 ? file : window_call(WINDOW_SYS_FTRUNCATE, file, (long)bytes, 0, 0, 0, 0);
        if (mapped >= 0)
                mapped = window_call(WINDOW_SYS_FCNTL, file, SURFACE_F_ADD_SEALS,
                                     SURFACE_SEAL_FIXED, 0, 0, 0);
        if (mapped >= 0)
                // PROT_READ | PROT_WRITE, MAP_SHARED
                mapped = window_call(WINDOW_SYS_MMAP, 0, (long)bytes, 3, 1, file, 0);

        if (mapped < 0 && mapped > -4096)
        {
                surface_refused = file < 0 ? file : mapped;
                if (file >= 0)
                        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
                return 0;
        }

        message.request.size = sizeof(message.request);
        message.request.protocol = SURFACE_PROTOCOL;
        message.request.role = role;
        message.request.width = width;
        message.request.height = height;
        message.request.buffer_slots = 2;

        surface_refused = surface_send(handle, &message, (int)file);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);

        if (surface_refused >= 0)
                surface_refused = surface_receive(handle, &message, 0);

        if (surface_refused >= 0)
                surface_refused = message.kind != SURFACE_CREATED ? -71 // EPROTO
                                  : message.status;

        if (surface_refused < 0)
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, (long)bytes, 0, 0, 0, 0);
                window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
                return 0;
        }

        ((struct surface *)mapped)->window.handle = (unsigned int)handle;
        return (struct surface *)mapped;
}

/*
        The buffer named by buffer_index, the damage list and the requests are
        ready. Damage is the program's: leave damage_count at zero for the
        whole buffer.
*/
static void surface_commit(struct surface *surface)
{
        struct surface_message message = {SURFACE_COMMIT, 0, {0}};

        __atomic_store_n(&surface->window.sequence, surface->window.sequence + 1,
                         __ATOMIC_RELEASE);
        surface_send(surface->window.handle, &message, -1);
}

// The next event, or false when there is none waiting.
static int surface_event(struct surface *surface, struct surface_event *event)
{
        unsigned int head = __atomic_load_n(&surface->event_head, __ATOMIC_ACQUIRE);

        if (surface->event_tail == head)
                return 0;

        *event = surface->event[surface->event_tail % SURFACE_EVENTS];
        __atomic_store_n(&surface->event_tail, surface->event_tail + 1, __ATOMIC_RELEASE);
        return 1;
}

// recvmmsg's header: a msghdr and the length it received.
struct surface_mmsghdr
{
        struct surface_msghdr header;
        unsigned int length;
};

#define SURFACE_MSG_WAITFORONE 0x10000
#define SURFACE_WAKES 2

/*
        The wakes waiting on the connection, taken in one call and thrown
        away: what they said is in the page's event ring, and they exist to
        end a sleep. Answers how many it took, or -1 when the compositor has
        gone (a zero-length message is the far end closed, and it is repeated,
        so it is seen in the first batch). flags is MSG_DONTWAIT for a look,
        or MSG_WAITFORONE for a sleep that ends with the first.
*/
static long surface_wakes(long handle, int flags)
{
        struct surface_message wake;
        struct surface_iovec part = {&wake, sizeof(wake)};
        struct surface_mmsghdr headers[SURFACE_WAKES];
        long got, taken = 0;

        do
        {
                for (int at = 0; at < SURFACE_WAKES; at++)
                        headers[at] = (struct surface_mmsghdr){{0, 0, &part, 1, 0, 0, 0}, 0};

                got = window_call(WINDOW_SYS_RECVMMSG, handle, (long)headers, SURFACE_WAKES,
                                  flags, 0, 0);

                for (int at = 0; at < got; at++)
                        if (!headers[at].length)
                                return -1;

                taken += got > 0 ? got : 0;
                flags = SURFACE_MSG_DONTWAIT;
        } while (got == SURFACE_WAKES);

        // EAGAIN is nothing waiting and EINTR a signal; neither is the far end gone.
        return got < 0 && got != -11 && got != -4 ? -1 : taken;
}

/*
        Sleeps until the compositor has written an event, the other descriptor
        has bytes, or the time is up, and empties the wakes that said so.
        Answers what ppoll does -- how many are ready, zero on the time being
        up or a signal -- or -1 when the compositor has gone.

        With nothing else to wait for it is one call: recvmmsg sleeps for the
        first wake and takes the rest with it. With a second descriptor or a
        time it is a ppoll, and the wakes are only looked for when the
        connection is what ended it.
*/
static long surface_wait(struct surface *surface, long other, long nanoseconds)
{
        struct window_poll wanted[2] = {{(int)surface->window.handle, 1, 0}, {(int)other, 1, 0}};
        long timeout[2] = {nanoseconds / 1000000000, nanoseconds % 1000000000};
        long ready;

        if (other < 0 && nanoseconds < 0)
        {
                ready = surface_wakes(surface->window.handle, SURFACE_MSG_WAITFORONE);

                return ready > 0 ? 1 : ready;
        }

        ready = window_call(WINDOW_SYS_PPOLL, (long)wanted, other >= 0 ? 2 : 1,
                            nanoseconds < 0 ? 0 : (long)timeout, 0, 0, 0);

        if (ready == -4) // EINTR
                return 0;

        if (ready > 0 && wanted[0].returned && surface_wakes(surface->window.handle, SURFACE_MSG_DONTWAIT) < 0)
                return -1;

        return ready;
}

static void surface_close(struct surface *surface)
{
        long handle = surface->window.handle;

        window_call(WINDOW_SYS_MUNMAP, (long)surface, surface->window.mapping, 0, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
}

#endif // KERNEL_MODE
#endif // WINDOW_INCLUDED
