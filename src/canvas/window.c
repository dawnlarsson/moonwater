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

        The pixels are the window's contents. The kernel desktop draws a frame
        and a titlebar around them, so x,y is the top left of the whole window
        and the contents land WINDOW_TITLE below it. The compositor that runs
        in userspace has a table of its own (struct windows, below).

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

// The numbers /dev/spark takes, which the compositor's ABI header holds.
#include "canvas_abi.h"

#define WINDOW_DEVICE "/dev/spark"

// O_RDWR | O_CLOEXEC, which are the same numbers on all three architectures.
// A window is this program's, not whatever it goes on to exec.
#define WINDOW_OPEN_FLAGS 0x80002u

// The contents start one page in, so the struct and the pixels are one mapping
// and the pixels stay page aligned.
#define WINDOW_PIXELS 4096

// The titlebar the kernel desktop draws above the contents.
#define WINDOW_TITLE 20

// The handle of a window of cells the program holds itself (window_grid_open):
// no file, so no call is made to it.
#define WINDOW_PRIVATE 0xffffffffu

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
        compositor that can be told to end any of them. The X in the kernel
        desktop's titlebar, or Alt-F4 or a middle press on the window's button
        in the compositor's panel, sets this and wakes the program; what
        happens next is the program's,
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
// A key held down and repeating, and the logo keys, which only the compositor sends.
#define WINDOW_KEY_REPEAT 128u
#define WINDOW_KEY_SUPER 256u

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

_Static_assert(((WINDOW_IOCTL_CREATE >> 16) & 0x3fffu) == sizeof(struct window_request),
               "window create ioctl size");

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

/*
        Contrast, for what is drawn over a ground it does not choose: a scroll
        bar on a terminal's paper, a tab's title over the windows under it.
        Not an invert -- the invert of a middle grey is a middle grey, and of
        a colour a garish other one -- but the other side of the ground's
        lightness: a ground darker than where black and white stand out from
        it equally (a relative luminance of 0.179) takes a light ink, and a
        lighter one a dark ink. The ink keeps a little of the ground's hue,
        which is what makes it sit on the ground rather than be stuck to it,
        and is still far from it in lightness. Luminance is worked out on the
        colour squared, near enough to sRGB's curve to pick a side.
*/
// from, moved amount 256ths of the way to to.
static inline unsigned int window_mix(unsigned int from, unsigned int to, unsigned int amount)
{
        unsigned int mixed = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                mixed |= (((from >> shift & 255) * (256 - amount) + (to >> shift & 255) * amount + 128) >> 8) << shift;
        return mixed;
}

static inline int window_light(unsigned int ground)
{
        unsigned int r = ground >> 16 & 255, g = ground >> 8 & 255, b = ground & 255;

        return 2126u * r * r + 7152u * g * g + 722u * b * b > 116394750u;
}

static inline unsigned int window_contrast(unsigned int ground)
{
        return window_mix(ground, window_light(ground) ? 0x000000u : 0xffffffu, 216);
}

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
        Windows on the compositor that runs in userspace (src/canvas/compositor.c).

        One table, which the compositor makes and every program maps: a
        column for each field and a row for each window, and the windows'
        pixels after it. Nothing is sent. Each side writes its own fields of
        a row and reads the other's, and knocks on a FIFO when it wrote
        something the other should look at:

                /run/moonwater/windows          the table
                /run/moonwater/window.N         row N's door: the program
                                                writes a byte when it has
                                                drawn or changed something,
                                                and when the last of its
                                                ends closes -- it exits or
                                                dies -- the row is free
                /run/moonwater/window.N.wake    the compositor writes a byte
                                                when there is an event, a
                                                size asked for, the keys
                                                gone or come, a close asked
                                                for or a picture taken

        A row is taken by writing a pid where there was none; only the
        compositor makes one none again, when its door has closed.

        The program writes x and y (where it wants to be, read once, when
        the window first shows), width and height (its picture's), buffer,
        frame, the damage, the title, state's WINDOW_MINIMIZED before the
        first picture, and event_tail. The compositor writes the rest: the
        output's numbers, focus, x and y from then on, state, want_width
        and want_height (the size it asks the window to be), shown and
        event_head. It reads every field a program writes once, into its
        own copy, and clamps it there.

        A picture is drawn into one of the row's two buffers, the buffer is
        named, the damage written and frame counted up, and the door is
        knocked on. The compositor takes it: it reads that buffer from then
        until the next picture and says so by writing the frame into shown,
        so the other buffer is the program's to draw into again once shown
        is the frame it last counted. A buffer is XR24, 0x00rrggbb a pixel,
        and every row of every buffer is stride pixels long, the output's
        width, so a window is any size up to the output's and nothing is
        made again when it changes.
*/
#define WINDOWS_TABLE "/run/moonwater/windows"
#define WINDOWS_DOOR "/run/moonwater/window."
#define WINDOWS 32
#define WINDOWS_EVENTS 64
#define WINDOWS_TITLE 32

// event_type, with event_flags WINDOW_KEY_*: a key (a its code, b its character), the pointer at a, b in
// the window, a button (a its code), the wheel (a in 120ths of a notch, b 1 when it is sideways).
#define WINDOW_EVENT_KEY 1u
#define WINDOW_EVENT_MOTION 2u
#define WINDOW_EVENT_BUTTON 3u
#define WINDOW_EVENT_WHEEL 4u

struct windows
{
        // The output: its pixels, how many of them one drawn pixel is, a frame in nanoseconds.
        unsigned int output_width, output_height, scale, period;
        unsigned int stride;   // pixels a row of every buffer
        unsigned int focus;    // the row with the keys, or WINDOWS
        unsigned long pixels;  // where row 0's first buffer is, in bytes from the table
        unsigned long buffer;  // the bytes of one buffer

        unsigned int pid[WINDOWS];
        unsigned int state[WINDOWS]; // WINDOW_MINIMIZED, WINDOW_CLOSING
        int x[WINDOWS], y[WINDOWS];
        unsigned int width[WINDOWS], height[WINDOWS];
        unsigned int want_width[WINDOWS], want_height[WINDOWS];
        unsigned int buffer_index[WINDOWS];
        unsigned int frame[WINDOWS];
        unsigned int shown[WINDOWS];
        // What the last picture changed: a width of 0 is all of it, and a height of 0 none.
        int damage_x[WINDOWS], damage_y[WINDOWS], damage_width[WINDOWS], damage_height[WINDOWS];
        char title[WINDOWS][WINDOWS_TITLE];

        unsigned int event_head[WINDOWS], event_tail[WINDOWS];
        unsigned short event_type[WINDOWS][WINDOWS_EVENTS], event_flags[WINDOWS][WINDOWS_EVENTS];
        int event_a[WINDOWS][WINDOWS_EVENTS], event_b[WINDOWS][WINDOWS_EVENTS];
};

// Row row's buffer which, where the table says it is: for the program, which made none of it.
#define windows_buffer(table, row, which)                                                              \
        ((unsigned int *)((char *)(table) + (table)->pixels +                                          \
                          ((unsigned long)(row) * 2 + ((which) & 1u)) * (table)->buffer))

// What a program has of its window: the table, its row, and its two ends.
struct window_row
{
        struct windows *table;
        unsigned int row;
        long door, wake;
        unsigned long bytes;
};

// One event, read out of a row's columns.
struct window_event
{
        unsigned int type, flags;
        int a, b;
};

_Static_assert(sizeof(struct window) == 1288, "window page ABI");
_Static_assert(sizeof(struct window_key) == 16, "window key ABI");
_Static_assert(sizeof(struct window_cell) == 8, "window cell ABI");
_Static_assert(sizeof(struct window_request) == 16, "window request ABI");

/* Every field of the page, where the kernel's desktop and its programs both
   expect it. */
#define WINDOW_AT(field, offset)                                               \
        _Static_assert(__builtin_offsetof(struct window, field) == (offset), "window page ABI " #field)
WINDOW_AT(x, 0);
WINDOW_AT(y, 4);
WINDOW_AT(z, 8);
WINDOW_AT(width, 12);
WINDOW_AT(height, 16);
WINDOW_AT(region, 20);
WINDOW_AT(display, 24);
WINDOW_AT(style, 28);
WINDOW_AT(edge, 32);
WINDOW_AT(sequence, 36);
WINDOW_AT(title, 40);
WINDOW_AT(state, 168);
WINDOW_AT(pitch, 172);
WINDOW_AT(max_width, 176);
WINDOW_AT(max_height, 180);
WINDOW_AT(display_width, 184);
WINDOW_AT(display_height, 188);
WINDOW_AT(awake, 192);
WINDOW_AT(handle, 196);
WINDOW_AT(stride, 200);
WINDOW_AT(history, 204);
WINDOW_AT(head, 208);
WINDOW_AT(lines, 212);
WINDOW_AT(key_head, 216);
WINDOW_AT(key_tail, 220);
WINDOW_AT(keys, 224);
WINDOW_AT(columns, 1248);
WINDOW_AT(rows, 1252);
WINDOW_AT(damage_row, 1256);
WINDOW_AT(damage_rows, 1260);
WINDOW_AT(grid_columns, 1264);
WINDOW_AT(grid_rows, 1268);
WINDOW_AT(max_columns, 1272);
WINDOW_AT(max_rows, 1276);
WINDOW_AT(mapping, 1280);
WINDOW_AT(want, 1284);
_Static_assert(__builtin_offsetof(struct window_key, character) == 4, "window key ABI");
_Static_assert(__builtin_offsetof(struct window_cell, ink) == 4, "window cell ABI");
_Static_assert(__builtin_offsetof(struct window_cell, paper) == 5, "window cell ABI");
_Static_assert(__builtin_offsetof(struct window_cell, flags) == 6, "window cell ABI");
_Static_assert(__builtin_offsetof(struct window_request, columns) == 8, "window request ABI");

#ifndef KERNEL_MODE

#if defined(__x86_64__)
#define WINDOW_SYS_OPENAT 257
#define WINDOW_SYS_MMAP 9
#define WINDOW_SYS_MUNMAP 11
#define WINDOW_SYS_IOCTL 16
#define WINDOW_SYS_CLOSE 3
#define WINDOW_SYS_PPOLL 271
#define WINDOW_SYS_READ 0
#define WINDOW_SYS_WRITE 1
#define WINDOW_SYS_LSEEK 8
#define WINDOW_SYS_GETPID 39
#define WINDOW_SYS_MADVISE 28
#else
// arm64 and riscv share the asm-generic numbering.
#define WINDOW_SYS_OPENAT 56
#define WINDOW_SYS_MMAP 222
#define WINDOW_SYS_MUNMAP 215
#define WINDOW_SYS_IOCTL 29
#define WINDOW_SYS_CLOSE 57
#define WINDOW_SYS_PPOLL 73
#define WINDOW_SYS_READ 63
#define WINDOW_SYS_WRITE 64
#define WINDOW_SYS_LSEEK 62
#define WINDOW_SYS_GETPID 172
#define WINDOW_SYS_MADVISE 233
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

        // A window of cells the program holds itself has no compositor to tell.
        if (window->handle == WINDOW_PRIVATE)
                return;

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
        if (window->handle != WINDOW_PRIVATE)
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
#define WINDOW_STRIDE_STEP 32u

/*
        The same ring, in the program's own memory.

        A terminal that draws its own pixels needs no compositor to hold its
        cells: the emulator addresses them through this struct whoever owns
        the page, so the page is an anonymous mapping, cut for lines
        max_columns wide and history of them, and only the pages written are
        ever resident. Its handle names no file, which is what makes
        window_widen move the lines here and not in a call.
*/
static struct window *window_grid_open(unsigned int columns, unsigned int rows,
                                       unsigned int max_columns, unsigned int history)
{
        // A ring with no lines has no line to name, and the offsets in the header and
        // the mapping's length are 32 bits: a grid that needs more is refused here, not
        // truncated into a mapping the window cannot find its lines in. A line is never
        // wider than the ring was cut for, and there are never more rows than lines.
        unsigned long cells, lines, bytes;
        unsigned int cut;
        // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
        long mapped;
        struct window *window;

        if (!history || !max_columns || !columns)
                return 0;
        columns = columns < max_columns ? columns : max_columns;
        rows = rows < history ? rows : history;

        cells = (unsigned long)history * max_columns;
        if (cells > 0xffffffffu / sizeof(struct window_cell))
                return 0;
        lines = WINDOW_PIXELS + cells * sizeof(struct window_cell);
        bytes = (lines + (unsigned long)history * sizeof(unsigned int) + 4095) & ~4095ul;
        if (bytes > 0xffffffffu)
                return 0;

        cut = (columns + WINDOW_STRIDE_STEP - 1) / WINDOW_STRIDE_STEP * WINDOW_STRIDE_STEP;
        mapped = window_call(WINDOW_SYS_MMAP, 0, (long)bytes, 3, 0x22, -1, 0);

        if (mapped < 0 && mapped > -4096)
                return 0;

        window = (struct window *)mapped;
        window->handle = WINDOW_PRIVATE;
        window->stride = cut < max_columns ? cut : max_columns;
        window->history = history;
        window->head = history + rows;
        window->lines = (unsigned int)lines;
        window->columns = window->grid_columns = columns;
        window->rows = window->grid_rows = rows;
        window->max_columns = max_columns;
        window->max_rows = history;
        window->mapping = (unsigned int)bytes;
        return window;
}

static void window_widen(struct window *window, unsigned int columns)
{
        unsigned int was = window->stride, stride;

        if (columns <= was)
                return;

        if (window->handle != WINDOW_PRIVATE)
        {
                window_call(WINDOW_SYS_IOCTL, window->handle, WINDOW_IOCTL_STRIDE,
                            columns, 0, 0, 0);
                return;
        }

        /*
                A step at a time, so a drag that widens the window recuts the
                lines a few times and not once a column, and never past what
                the ring was cut for. From the last line down: a line only
                ever moves further out, past the end of every line below it,
                so nothing lands on a line that has yet to move.
        */
        stride = (columns + WINDOW_STRIDE_STEP - 1) / WINDOW_STRIDE_STEP * WINDOW_STRIDE_STEP;
        stride = stride < window->max_columns ? stride : window->max_columns;

        if (stride <= was)
                return;

        for (unsigned int slot = window->history; slot--;)
        {
                unsigned int *length = window_lengths(window) + slot;
                unsigned int kept = *length < was ? *length : was;

                if (kept)
                        memory_copy(window_cells(window) + (unsigned long)slot * stride,
                                    window_cells(window) + (unsigned long)slot * was,
                                    (unsigned long)kept * sizeof(struct window_cell));
                *length = kept;
        }

        window->stride = stride;
}

static void window_close(struct window *window)
{
        long file = window->handle;

        window_call(WINDOW_SYS_MUNMAP, (long)window, window->mapping, 0, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
}

/*
        A window on the compositor that runs in userspace: a row of the table taken, drawn into and knocked
        for, and let go of by closing its door, which the compositor hears as the program having gone.
*/
#define WINDOWS_O_READ 0x80800L  // O_RDONLY | O_NONBLOCK | O_CLOEXEC
#define WINDOWS_O_WRITE 0x80801L // O_WRONLY | O_NONBLOCK | O_CLOEXEC
#define WINDOWS_O_BOTH 0x80802L  // O_RDWR | O_NONBLOCK | O_CLOEXEC

// "/run/moonwater/window.N" and, with wake, ".wake" after it.
static void windows_door_name(char *into, unsigned int row, int wake)
{
        const char *wake_name = ".wake";
        unsigned int at = 0;

        for (const char *from = WINDOWS_DOOR; *from; from++)
                into[at++] = *from;
        if (row >= 10)
                into[at++] = (char)('0' + row / 10);
        into[at++] = (char)('0' + row % 10);
        for (; wake && *wake_name; wake_name++)
                into[at++] = *wake_name;
        into[at] = 0;
}

static void windows_drain(long handle)
{
        char bytes[64];

        while (window_call(WINDOW_SYS_READ, handle, (long)bytes, sizeof(bytes), 0, 0, 0) > 0)
                ;
}

/*
        A row taken: the table mapped, the first row with no pid given this one, the program's columns
        cleared, and its door and its wake opened. The door is opened for writing first, which fails when
        no compositor holds its other end, and then for reading and writing, which is the end kept: a
        knock on a door whose compositor has gone is then a byte in a pipe and never a SIGPIPE. Answers 0
        or a negative errno.
*/
static long windows_take(struct window_row *own)
{
        long file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, WINDOW_OPEN_FLAGS, 0, 0, 0);
        long size, mapped, probe;
        unsigned int pid = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
        struct windows *table;
        char name[48];

        if (file < 0)
                return file;

        size = window_call(WINDOW_SYS_LSEEK, file, 0, 2, 0, 0, 0);
        // PROT_READ | PROT_WRITE, MAP_SHARED
        mapped = size >= (long)sizeof(struct windows) ? window_call(WINDOW_SYS_MMAP, 0, size, 3, 1, file, 0) : -22;
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        if (mapped < 0 && mapped > -4096)
                return mapped;

        table = (struct windows *)mapped;
        own->table = table;
        own->bytes = (unsigned long)size;

        for (own->row = 0; own->row < WINDOWS; own->row++)
        {
                unsigned int none = 0;

                if (__atomic_compare_exchange_n(&table->pid[own->row], &none, pid, 0, __ATOMIC_ACQ_REL,
                                                __ATOMIC_RELAXED))
                        break;
        }

        if (own->row == WINDOWS)
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0);
                return -28; // ENOSPC
        }

        windows_door_name(name, own->row, 0);
        probe = window_call(WINDOW_SYS_OPENAT, -100, (long)name, WINDOWS_O_WRITE, 0, 0, 0);
        own->door = probe >= 0 ? window_call(WINDOW_SYS_OPENAT, -100, (long)name, WINDOWS_O_BOTH, 0, 0, 0) : probe;
        if (probe >= 0)
                window_call(WINDOW_SYS_CLOSE, probe, 0, 0, 0, 0, 0);
        windows_door_name(name, own->row, 1);
        own->wake = own->door >= 0 ? window_call(WINDOW_SYS_OPENAT, -100, (long)name, WINDOWS_O_READ, 0, 0, 0) : -1;

        if (own->door < 0 || own->wake < 0)
        {
                long failed = own->door < 0 ? own->door : own->wake;

                // No compositor to empty the row: it is emptied here.
                if (own->door >= 0)
                        window_call(WINDOW_SYS_CLOSE, own->door, 0, 0, 0, 0, 0);
                __atomic_store_n(&table->pid[own->row], 0, __ATOMIC_RELEASE);
                window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0);
                return failed;
        }

        windows_drain(own->wake);
        table->state[own->row] = 0;
        table->x[own->row] = table->y[own->row] = 0;
        table->width[own->row] = table->height[own->row] = 0;
        table->buffer_index[own->row] = 0;
        table->damage_width[own->row] = 0;
        table->title[own->row][0] = 0;
        table->event_tail[own->row] = __atomic_load_n(&table->event_head[own->row], __ATOMIC_ACQUIRE);
        table->shown[own->row] = 0;
        __atomic_store_n(&table->frame[own->row], 0, __ATOMIC_RELEASE);
        return 0;
}

// Something for the compositor to look at.
static void windows_knock(const struct window_row *own)
{
        char byte = 0;

        window_call(WINDOW_SYS_WRITE, own->door, (long)&byte, 1, 0, 0, 0);
}

// A picture, drawn into buffer which: named, counted and knocked for. Width, height and the damage first.
static void windows_show(const struct window_row *own, unsigned int which)
{
        struct windows *table = own->table;

        table->buffer_index[own->row] = which & 1u;
        __atomic_store_n(&table->frame[own->row], table->frame[own->row] + 1, __ATOMIC_RELEASE);
        windows_knock(own);
}

// Whether the compositor has taken the last picture, so the other buffer is free.
static int windows_ready(const struct window_row *own)
{
        return __atomic_load_n(&own->table->shown[own->row], __ATOMIC_ACQUIRE) == own->table->frame[own->row];
}

// The next event, or false when there is none.
static int windows_event(const struct window_row *own, struct window_event *event)
{
        struct windows *table = own->table;
        unsigned int row = own->row, tail = table->event_tail[row], at = tail % WINDOWS_EVENTS;

        if (__atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE) == tail)
                return 0;

        event->type = table->event_type[row][at];
        event->flags = table->event_flags[row][at];
        event->a = table->event_a[row][at];
        event->b = table->event_b[row][at];
        __atomic_store_n(&table->event_tail[row], tail + 1, __ATOMIC_RELEASE);
        return 1;
}

/*
        Asleep until the compositor knocks, the other descriptor (a pty, for a terminal; -1 for none) is
        readable, or nanoseconds (negative: never). Answers what ppoll does, and -1 once the compositor has
        gone: its end of the wake is closed.
*/
static long windows_wait(const struct window_row *own, long other, long nanoseconds)
{
        struct window_poll wanted[2] = {{(int)own->wake, 1, 0}, {(int)other, 1, 0}};
        long timeout[2] = {nanoseconds / 1000000000, nanoseconds % 1000000000};
        long ready = window_call(WINDOW_SYS_PPOLL, (long)wanted, other >= 0 ? 2 : 1,
                                 nanoseconds < 0 ? 0 : (long)timeout, 0, 0, 0);

        if (ready > 0 && wanted[0].returned)
        {
                windows_drain(own->wake);
                if (wanted[0].returned & 16) // POLLHUP
                        return -1;
        }
        return ready;
}

// A buffer's pages given back, for a program that will draw it whole before it shows it again.
static void windows_discard(const struct window_row *own, unsigned int which)
{
        // MADV_REMOVE
        window_call(WINDOW_SYS_MADVISE, (long)windows_buffer(own->table, own->row, which), (long)own->table->buffer,
                    9, 0, 0, 0);
}

// The window gone: its door closed, which is all the compositor needs.
static void windows_leave(struct window_row *own)
{
        window_call(WINDOW_SYS_CLOSE, own->door, 0, 0, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, own->wake, 0, 0, 0, 0, 0);
        window_call(WINDOW_SYS_MUNMAP, (long)own->table, (long)own->bytes, 0, 0, 0, 0);
        own->table = 0;
}

#endif // KERNEL_MODE
#endif // WINDOW_INCLUDED
