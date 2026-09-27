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

#ifndef KERNEL_MODE

#if defined(__x86_64__)
#define WINDOW_SYS_OPENAT 257
#define WINDOW_SYS_MMAP 9
#define WINDOW_SYS_MUNMAP 11
#define WINDOW_SYS_IOCTL 16
#define WINDOW_SYS_CLOSE 3
#define WINDOW_SYS_PPOLL 271
#else
// arm64 and riscv share the asm-generic numbering.
#define WINDOW_SYS_OPENAT 56
#define WINDOW_SYS_MMAP 222
#define WINDOW_SYS_MUNMAP 215
#define WINDOW_SYS_IOCTL 29
#define WINDOW_SYS_CLOSE 57
#define WINDOW_SYS_PPOLL 73
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

#endif // KERNEL_MODE
#endif // WINDOW_INCLUDED
