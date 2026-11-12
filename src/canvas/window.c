/*
        Moonwater window

        What a program shares with the compositor, and the cells a terminal's
        text lives in. It includes canvas.c's ABI itself, and it needs the
        library before it (lib.c and lib.util.c, as the shell and the tests
        include them): the file uses the library's types and memory routines.
        Its own syscall entry is used only where the library does not supply
        one.

        A program's window is a row of the compositor's table (struct windows,
        below): the program writes its picture, its size and its title there
        and knocks on its row's door, and the compositor writes where the
        window is and which window has the keys. struct window is the page of
        cells a window of text is made of, which a terminal holds in its own
        memory (window_grid_open): the program and the emulator address the
        same ring of lines whoever owns the page. The fields above the line
        are the program's to write; the ones below are the compositor's,
        which a program that holds the page itself fills in for itself.

        The fields z, region, display, style, edge, pitch, awake, the output
        and maximum sizes are the older per-window protocol. No code outside
        this file names them now, since struct windows (below) carries the
        live window state. They stay because the page's layout is asserted
        further down. The title is still read: the terminal takes its title
        from the page.
*/

#ifndef WINDOW_INCLUDED
#define WINDOW_INCLUDED

// The numbers /dev/spark takes, which the compositor's ABI header holds.
#define CANVAS_ABI_ONLY
#include "canvas.c"
#undef CANVAS_ABI_ONLY

// The window list: its format and reader (ui/ui.c's first part, UI_LIST_ONLY), which the compositor checks a list with (window_list_valid).
#define UI_LIST_ONLY
#include "ui/ui.c"
#undef UI_LIST_ONLY

// O_RDWR | O_CLOEXEC, which are the same numbers on all three architectures.
// A window is this program's, not whatever it goes on to exec.
#define WINDOW_OPEN_FLAGS 0x80002u

// The contents start one page in, so the struct and the pixels are one mapping
// and the pixels stay page aligned.
#define WINDOW_PIXELS 4096

// The handle of a window of cells the program holds itself (window_grid_open):
// no file, so no call is made to it.
#define WINDOW_PRIVATE 0xffffffffu

// style
#define WINDOW_MINIMIZED 4u

/* Keep a raise's top + 1 away from overflow on an untrusted shared page. */
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

/*
        The compositor is asking this window to go away.

        Asking, because it cannot do more: the pages are the program's and the
        task is the kernel's, and a compositor that could end either would be a
        compositor that can be told to end any of them. The compositor's close
        button, Alt-F4 or a middle press on the window's tab sets this and
        wakes the program; what happens next is the program's, and one that
        ignores it keeps its window.
*/
#define WINDOW_CLOSING 2u

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
/*
        A button, a motion or a wheel that a finger on a touch screen made, which only the compositor sends: a program
        may scroll what a finger drags where a mouse would select. And a release that is no click: the finger that
        pressed became two fingers or was held for the right button, and what the press would do is not done.
*/
#define WINDOW_KEY_TOUCH 512u
#define WINDOW_KEY_CANCEL 1024u
/*
        A finger held still for the right button (a long press): the right button's press and release, and the left
        release that ends such a press (CANCEL, TOUCH and HOLD together, once the finger lifts). A program may take the
        long press as its own, as a terminal selects the word under it, where a click would open a menu.
*/
#define WINDOW_KEY_HOLD 2048u
/*
        A motion that is no step the hand made: a finger's or a pen's lead (the place it was told at, a frame ahead of where it is)
        taken away at its lift or in a frame with no report, so the window knows where the hand is. It is a place and not a speed: a
        program that reads the hand's velocity off its moves leaves it out (the kit does, ui_press_track), as the step back it is
        would read as the hand turning.
*/
#define WINDOW_KEY_SETTLE 4096u

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
        unsigned int region;  // placement request; no code names it yet
        unsigned int display; // which output a region is measured against
        unsigned int style;
        unsigned int edge;     // corner radius
        unsigned int sequence; // bump after drawing
        char title[WINDOW_TITLE_MAX];

        // The compositor writes these.
        unsigned int state;         // WINDOW_CLOSING and the rest, as the compositor writes them
        unsigned int pitch;         // pixels per row of the contents
        unsigned int max_width;     // what the mapping actually holds
        unsigned int max_height;
        unsigned int display_width; // the output this window is on
        unsigned int display_height;
        unsigned int awake;         // whether the compositor is watching

        // The window's own bookkeeping: WINDOW_PRIVATE for a page the program holds itself.
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

/* A cell the hand has selected: drawn on the theme's selection colour. Only a frame's copy of the cells has it;
   the ring never does. */
#define WINDOW_CELL_SELECTED 1024u

/* The first and the last cell of a touch selection, a frame's copy only: each is drawn with a flat handle on its outer edge. */

/* Which colours a program set. Without INK_SET the ink is the theme's default ink, and without PAPER_SET the paper is the
   theme's default paper: the index is then not looked at. SWAP is a cell written in reverse video, where the defaults trade
   places, so an unset ink is the theme's paper. The index means nothing while its bit is clear. */
#define WINDOW_CELL_INK_SET 8192u
#define WINDOW_CELL_PAPER_SET 16384u
#define WINDOW_CELL_SWAP 32768u

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

/*
        The window table of the compositor that runs in userspace (src/canvas/compositor.c).

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
        the window first shows), place to name (where it asks to be put and
        what it is remembered by, read then too), width and height (its picture's), buffer,
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

        A window that writes WINDOW_GLASS into its glass column before its
        first picture is glass: what is under it shows through, blurred, and
        its buffer is ARGB, 0xaarrggbb premultiplied, so a pixel of 0 is the
        glass alone and one of alpha 255 the program's colour alone. The
        glass is tinted toward its own side of lightness, and the compositor
        writes the colour it came to, on average, into ground and knocks
        when that changes enough to matter: the colour an ink has to stand
        out from (window_contrast).

        lift is a count anyone holding the table may add one to and knock
        for: the window is brought back if it was put away, raised over the
        others and given the keys, as its tab would. It is how a second
        start of a program that keeps one window finds the first. moved is
        the same for a window's place: the placement columns written again
        and one more, and the window is put where they say now.
*/
#define WINDOWS_TABLE "/run/moonwater/windows"
// The reasons the GPU's compose refused a present (struct windows gpu_refused, compositor.c's compositor_gpu_why): one slot each, none of them
// the card drawing it. The names are host.c's, in the same order.
#define WINDOWS_GPU_REASONS 24
// The names, in the order of compositor.c's enum compositor_gpu_why (slot 0 is the card drawing it: no refusal, and never said).
static const string_address windows_gpu_reasons[WINDOWS_GPU_REASONS] = {
    "drawn", "canvas gpu is off", "headless, and not asked for the GPU (-G)", "no card the GPU's kernels are for", "the self-test drew wrong",
    "cooling down after hangs", "a scanout pitch not 16 bytes", "a job given up on may still run",
    "the scanout is not one the card can draw into", "a window's picture is not mapped for the card", "a landing (scaled)", "a snap preview or a shared edge",
    "the records or the cover tables are full", "a close button not wholly in its picture", "the scene did not finish", "a turned screen (the processor turned it)",
    "other frames, or empty damage"};
// The GPU's prerequisites, in the order the path needs them (gpu.c's enum gpu_req, the same order): their names, and what to do about each one, in words.
// moonwater debug shows them, and the issue board's detail for a failed one.
#define WINDOWS_GPU_REQS 10
static const string_address windows_gpu_req_names[WINDOWS_GPU_REQS] = {"none", "render node", "driver", "family", "userptr", "context", "ring", "self-test",
                                                                     "udmabuf", "PRIME import"};
static const string_address windows_gpu_req_fixes[WINDOWS_GPU_REQS] = {
    "", "no render node of the card opened: check its mode and owner, and that the driver is loaded (the render group, or mode 0666, lets a user open it)",
    "the card's driver is not amdgpu: there is no GPU path for it yet (i915, virtio_gpu and nouveau stay on the processor)",
    "this AMD family has no kernels (RDNA2 only: 144, 146, 149, 151); the card stays on the processor",
    "the kernel would not map a page for the card: rebuild it with CONFIG_DRM_AMDGPU_USERPTR=y (error 19 is that kernel: it selects HMM_MIRROR and MMU_NOTIFIER)",
    "the card would not give a buffer, its VM map or a compute context: the amdgpu compute ring or its firmware is not up (the kernel lines say which)",
    "the compute ring did not finish the self-test job: firmware, clocks or a ring reset (the kernel lines say which)",
    "the card drew the self-test picture wrong: its kernels do not match this family's ISA; the card stays on the processor",
    "/dev/udmabuf would not make a dma-buf of a memfd page: CONFIG_UDMABUF=y, and /dev/udmabuf must open (the card still composes; a refused userptr then has no fallback)",
    "a dma-buf would not import into the render node: the card cannot read a window's memfd (CONFIG_DRM_AMDGPU, with PRIME; the card still composes)"};

/*
        The issue board: the problems a Settings option has, which any program that holds the table may raise and clear, and which Settings and
        moonwater debug read. An issue is its key (the words of the option's row, "canvas gpu", NUL-ended), a severity (WINDOWS_ISSUE_WARNING or
        WINDOWS_ISSUE_ERROR), a code (the message: WINDOWS_ISSUE_* below, which Settings words in the reader's language), two numbers the
        message formats (a, b) and a detail string, the kernel's own words say, bounded and NUL-ended. Each slot holds its own detail, so the
        board's memory is fixed and no write allocates. An issue is one key with one code: setting it again updates it in place.
*/
#define WINDOWS_ISSUES 32         // the board's slots; a set that finds none free is refused and counted (issue_refused)
#define WINDOWS_ISSUE_KEY 24      // a key's bytes, its null with them
#define WINDOWS_ISSUE_DETAIL 192  // a detail's bytes kept, its null with them
#define WINDOWS_ISSUE_WARNING 1u
#define WINDOWS_ISSUE_ERROR 2u
#define WINDOWS_ISSUE_ANY 0u      // windows_issue_clear's code: every issue of the key
#define WINDOWS_KEY_GPU "canvas gpu"            // the keys, as Settings' rows for the GPU option and the system notices have them
#define WINDOWS_KEY_NOTICES "system notices"
// The kernel's notice classes, each once: its name in WINDOWS_ISSUE_KERNEL_*, the key its issue is filed under and its name for moonwater debug, in the
// class's index order (its bit in the dismiss mask, its place on the board). The codes, keys, names and Settings' cases are expanded from it; the
// words that raise a class are kernel_notice_rules'.
#define WINDOWS_KERNEL_CLASSES(X)                                          \
        X(GPU, WINDOWS_KEY_GPU, "a kernel notice about the GPU")           \
        X(USB, "usb", "a kernel notice about USB")                         \
        X(WIFI, "wifi", "a kernel notice about Wi-Fi")                     \
        X(BLUETOOTH, "bluetooth", "a kernel notice about Bluetooth")       \
        X(AUDIO, "audio device", "a kernel notice about audio")            \
        X(STORAGE, "storage", "a kernel notice about storage")             \
        X(SYSTEM, WINDOWS_KEY_NOTICES, "a kernel notice about the system") \
        X(PROGRAM, WINDOWS_KEY_NOTICES, "a program crashed")
// The messages (Settings words each in the reader's language, the language table's WINDOWS_ISSUE_* ids in ui/ui.c). a and b are the numbers the message
// formats, and detail is shown after it, copyable. The numbers are per code: see the comment on each.
enum windows_issue_code
{
        WINDOWS_ISSUE_NONE,
        WINDOWS_ISSUE_GPU_MAP,       // the kernel refused to map window pictures for the card: a the step, b the kernel's error number
        WINDOWS_ISSUE_GPU_REFUSED,   // the card refused presents: a the count, b the reason (WINDOWS_GPU_REASONS' index)
        WINDOWS_ISSUE_GPU_LISTS,     // the lists are off: the card is not composing in earnest (a and b 0)
        WINDOWS_ISSUE_GPU_COOLING,   // the card is cooling down after hangs (a the cool-downs in a row)
        WINDOWS_ISSUE_GPU_UNTRUSTED, // the card's self-test drew wrong (a the family)
        WINDOWS_ISSUE_GPU_FAMILY,    // this GPU family is not supported (a the family)
        WINDOWS_ISSUE_GPU_LOST,      // apps lost frames to the processor: a the lost frames, b the frames tried
        WINDOWS_ISSUE_GPU_SCANOUT,   // the card refused a window's framebuffer for direct scan-out: a the kernel's error number
        WINDOWS_ISSUE_GPU_USERPTR,   // the kernel was built without userptr for the GPU (CONFIG_DRM_AMDGPU_USERPTR): a the step, b the error
        WINDOWS_ISSUE_AUDIO_OPEN,    // the audio service could not open the sink: a the error number
        WINDOWS_ISSUE_AUDIO_RATE,    // the sink refused the sample rate: a the rate
        WINDOWS_ISSUE_GPU_REQUIRES,  // a prerequisite of the GPU path failed: a the step (gpu.c's enum gpu_req), b the kernel's error; the detail is the fix
        WINDOWS_ISSUE_GPU_FAULT,     // the card faulted reading GPU memory: a the jobs that found a fault; the detail is the fault's text (what the address was in)
        WINDOWS_ISSUE_GPU_HUNG,      // the card hung twice within a minute: the processor draws the desktop for the rest of the session (no numbers)
        // The kernel's own notices (the feeder below): a the records of the class seen, b the seconds since the last, the detail the last line.
#define WINDOWS_KERNEL_CODE(name, key, text) WINDOWS_ISSUE_KERNEL_##name,
        WINDOWS_KERNEL_CLASSES(WINDOWS_KERNEL_CODE)
#undef WINDOWS_KERNEL_CODE
        WINDOWS_ISSUE_RADIO,            // a radio is blocked by rfkill (under wifi or bluetooth): the detail says which switch
        WINDOWS_ISSUE_COUNT
};
typedef char windows_issue_codes_fit[WINDOWS_ISSUE_COUNT <= 256 ? 1 : -1];
// The classes are the codes before the radio's; the dismiss mask has a bit for each, and a code is a kernel notice's when it is one of them.
#define KERNEL_NOTICE_CLASSES (WINDOWS_ISSUE_RADIO - WINDOWS_ISSUE_KERNEL_GPU)
#define KERNEL_NOTICE_ALL ((1u << KERNEL_NOTICE_CLASSES) - 1u)
#define windows_issue_kernel(code) ((unsigned int)(code) - WINDOWS_ISSUE_KERNEL_GPU < KERNEL_NOTICE_CLASSES)
// The codes' names, for moonwater debug (the Settings words are the reader's own).
static const string_address windows_issue_names[WINDOWS_ISSUE_COUNT] = {
        "none", "the kernel refused to map window pictures", "the card refused presents", "the lists are off", "the card is cooling down",
        "the card's self-test drew wrong", "the GPU family is not supported", "apps lost frames to the processor", "the card refused direct scan-out",
        "the kernel has no userptr for the GPU (CONFIG_DRM_AMDGPU_USERPTR)", "the audio sink did not open", "the audio sink refused the rate",
        "a prerequisite of the GPU path failed", "the card faulted reading GPU memory", "the card hung twice within a minute",
#define WINDOWS_KERNEL_NAME(name, key, text) [WINDOWS_ISSUE_KERNEL_##name] = text,
        WINDOWS_KERNEL_CLASSES(WINDOWS_KERNEL_NAME)
#undef WINDOWS_KERNEL_NAME
        [WINDOWS_ISSUE_RADIO] = "a radio is blocked (rfkill)"};

struct windows_issue
{
        char key[WINDOWS_ISSUE_KEY];
        unsigned int severity, code, a, b;
        unsigned int used;
        char detail[WINDOWS_ISSUE_DETAIL];
};

#define WINDOWS_DOOR "/run/moonwater/window."
/*      A door is read by the compositor, which runs as root, and the row it
        names is freed when every writer of the door has closed (the program
        ended). A door anyone may write is a door anyone may hold open: a
        process that is not the window's keeps the door's writer count above
        zero, so the compositor never hears the keeper leave and never frees
        the row, and thirty-two held rows leave no room for a new window --
        the desktop denied to the machine by whoever can open the FIFO. The
        table itself is the compositor's memfd, reached only through
        /proc/PID/fd of a root process, so no program that is not root (or the
        compositor's own user) can map it and be a window at all; a door no
        such program can use is a door it has no business opening. So the door
        is root's, 0600, as the wake beside it and the keys FIFO (0600) are:
        the compositor and the programs a desktop session starts open it as
        root. A design that lets an unprivileged program be a window must hand
        it the door (and the table) as a descriptor, not leave the FIFO open
        to the whole machine. */
#define WINDOWS 32
#define WINDOWS_EVENTS 64
#define WINDOWS_TITLE 32
// The screens one desktop spans, at most, and a screen's connector name with its null.
#define WINDOWS_OUTPUTS 8
#define WINDOWS_OUTPUT_NAME 16
#define WINDOWS_OUTPUT_MODEL 16 // the monitor's name from its EDID, the descriptor's 13 bytes and a null
#define WINDOWS_OUTPUT_HZ 8 // the refresh rates one screen lists at its size, at most
// The input devices the compositor reads, the room of the name it keeps of each, and what it found each to be.
#define WINDOWS_INPUTS 32
// The wheel turns the readout keeps for each input: the last WINDOW_WHEEL_RING that were used, on the device's clock, and whether each
// was a stream's notch (window_wheel_note). `moonwater debug` reads the ones within WINDOW_WHEEL_SPAN_NS (window_wheel_report).
#define WINDOW_WHEEL_RING 1024
#define WINDOW_WHEEL_SPAN_NS 5000000000ull
#define WINDOWS_INPUT_NAME 48
#define WINDOWS_INPUT_KEYBOARD 1u    // taken as a keyboard: it has the letter A
#define WINDOWS_INPUT_MOUSE 2u       // as a mouse: relative X and Y
#define WINDOWS_INPUT_TABLET 4u      // as a tablet: absolute X and Y
#define WINDOWS_INPUT_WAKES 8u       // a power button or a lid, read only to wake
#define WINDOWS_INPUT_WHEEL 16u      // it has REL_WHEEL
#define WINDOWS_INPUT_WHEEL_HI 32u   // REL_WHEEL_HI_RES, the wheel in 120ths of a notch
#define WINDOWS_INPUT_HWHEEL 64u     // REL_HWHEEL
#define WINDOWS_INPUT_HWHEEL_HI 128u // REL_HWHEEL_HI_RES
#define WINDOWS_INPUT_BUTTONS 256u   // BTN_LEFT
#define WINDOWS_INPUT_TOUCH 512u     // BTN_TOUCH
#define WINDOWS_INPUT_FINGERS 1024u  // BTN_TOOL_FINGER: a touchpad, or a touch screen that counts fingers
#define WINDOWS_INPUT_SLOTS 2048u    // ABS_MT_POSITION_X: more than one finger at once
#define WINDOWS_INPUT_POINTER 4096u  // INPUT_PROP_POINTER: it moves a pointer, a touchpad's mark
#define WINDOWS_INPUT_DIRECT 8192u   // INPUT_PROP_DIRECT: a touch screen or a pen on one

/*
        The mode of the table, its memfd and the doors: owner only, 0600.

        The table is one mapping of every window's buffers and input rings:
        whoever maps it reads any window's picture (the root terminal's among
        them) and writes any window's event ring, focus and lift. It is a
        trust boundary, not a public interface, and every program that uses it
        legitimately -- the compositor and the windows the machine script
        starts (the terminal, the log, the launcher, the settings) -- is root.
        A program that is not root (an unprivileged local process, or a bowl
        program once it runs as a real unprivileged uid) must not be able to
        open it: it would be a confused deputy over a root shell. The door
        FIFOs are the same owner, so a knock, too, is the owner's. The one
        other way in, /dev/spark's WINDOWS_IOCTL_TABLE_GET, is held to
        CAP_SYS_ADMIN beside this (canvas.c windows_table_get).
*/
#define WINDOWS_MODE 0600
// The doors and their wakes are the table's owner's too.
#define WINDOWS_DOOR_MODE WINDOWS_MODE

// event_type, with event_flags WINDOW_KEY_*: a key (a is its code, b its character), the pointer at a, b in
// the window, a button (a its code), the wheel (a in 120ths of a notch, b 1 when it is sideways).
#define WINDOW_EVENT_KEY 1u
#define WINDOW_EVENT_MOTION 2u
#define WINDOW_EVENT_BUTTON 3u
#define WINDOW_EVENT_WHEEL 4u
// A wheel event's flag: its a is a trackpad's motion in 120ths of a notch, as a notch's is, but not taken as notches: the region
// moves one for one with it, at its own scale (ui_wheel_turn), and no glide is put on it.
#define WINDOW_WHEEL_PRECISE 4096u
// A precise gesture is over: a touchpad's fingers have all lifted (a is 0). A region in hand lets go now, with the pause rule
// (ui_scroll_release: a finger still for UI_HAND_GAP_NS before its lift throws nothing), and not after the gap the reports left.
#define WINDOW_WHEEL_STOP 8192u
// A precise turn that is a wheel's stream (a notch to every report, within UI_HAND_GAP_NS of the last): the kit takes it as a
// trackpad's motion (WINDOW_WHEEL_PRECISE), and a Wayland client takes it as the wheel it is (value120), not as a pad's fingers.
#define WINDOW_WHEEL_STREAM 16384u // shares the bit of WINDOW_PINCH_BEGIN: the event's type says which meaning a flag has
// The scroll body's switches, as the table's scroll_off holds them (0 is the standard: natural direction off, momentum and the rubber
// band on). FLIP: natural direction is on, so a wheel's notch and a pad's motion move the content the other way from the OS's sign, the way
// the fingers go (ui_wheel_turn, ui_body_track). STOPS: momentum is off, so a release carries no coast (ui_body_let_go). STILL: the rubber
// band is off, so past an end the content stops there (ui_body_pin, ui_body_drawn).
#define WINDOW_SCROLL_FLIP 1u
#define WINDOW_SCROLL_STOPS 2u
#define WINDOW_SCROLL_STILL 4u
/*
        A pinch: two fingers that moved apart or together on a touch screen, or a trackpad's two (the kit's recogniser, ui/ui.c).
        a is the scale in 1024ths of the span the pinch began from (WINDOW_PINCH_ONE is 1.0), b the focal point, the middle of the
        two, in the window's own pixels (window_pinch_pack). The flags are WINDOW_PINCH_BEGIN, WINDOW_PINCH_END, or neither for an
        update. A program that has said it takes pinches (the window pinch_wanted flag) is sent these; any other is sent Ctrl with a wheel of a notch
        each time the scale has moved a quarter from the last notch sent (the zoom a browser or GTK program takes from a trackpad).
*/
#define WINDOW_EVENT_PINCH 6u
#define WINDOW_PINCH_BEGIN 16384u
#define WINDOW_PINCH_END 32768u
#define WINDOW_PINCH_ONE 1024
static inline int window_pinch_pack(int x, int y)
{
        return (int)((unsigned int)x << 16 | ((unsigned int)y & 0xffffu));
}
static inline int window_pinch_x(int b)
{
        return (int)(((unsigned int)b >> 16) & 0xffffu);
}
static inline int window_pinch_y(int b)
{
        return (int)((unsigned int)b & 0xffffu);
}
/*
        The clipboard's news, a the selection (WINDOWS_CLIP_CLIPBOARD or WINDOWS_CLIP_PRIMARY) and b what happened: another
        window copied (CHANGED: a window that asked to watch hears it, and is told no text), the paste this window asked
        for may be read now (READY: windows_clip_read) or may not (REFUSED: it has not the keys, the person did nothing
        that asks for a paste, there is nothing, or the service is off), the copy it offered was taken (TAKEN: its
        memfd may be closed) or not (DECLINED: too large, not UTF-8, a NUL in it, not sealed, or the service is off).
*/
#define WINDOW_EVENT_CLIP 5u

/*
        Where a program asks its window to be put, and the name the window is known by.

        The program writes these into its row before its first picture, and the compositor reads them once with
        that picture, into its own copy, and clamps them there as it does x and y. place is one of the
        WINDOW_PLACE_ modes, with WINDOW_PLACE_SCREEN when a screen was named and WINDOW_PLACE_SIZE when a size
        was; place_x and place_y are where WINDOW_PLACE_AT puts the top left, counted from the top left of the
        screen; place_screen counts the screens from 0, in the order `moonwater canvas` lists them. A screen
        that is not there is the first. The room of a screen is the screen less the taskbar along its edge.

        name is what the compositor remembers where the window was put by: lower-case letters, digits and
        hyphens, a letter or a digit first, and none for a window that is not remembered. Not the title, which
        a program changes as it runs (a terminal's is its shell's).

        `moonwater window` hands a program its placement in its environment, as MOONWATER_PLACE: the words of
        the verb, which window_place_parse reads. windows_take reads it into the row it takes and empties it in
        the program's environment, so the first window a program opens is the one put there and the programs
        it starts after that are not. A program that is no window of this compositor's (a bowl's, one of
        Wayland or X) never reads it.
*/
#define WINDOW_PLACE_AUTO 0u       // where the program asked, x and y, as it always was
#define WINDOW_PLACE_CENTER 1u     // in the middle of the screen's room
#define WINDOW_PLACE_AT 2u         // its top left at place_x, place_y of the screen
#define WINDOW_PLACE_MAXIMIZED 3u  // filling the screen's room, the taskbar not covered
#define WINDOW_PLACE_FULLSCREEN 4u // filling the screen, under the taskbar too
#define WINDOW_PLACE_HOW 7u        // the mode's bits
#define WINDOW_PLACE_SCREEN 8u
#define WINDOW_PLACE_SIZE 16u
// The most a number of a placement can be either way: wider than any desktop, and far inside WINDOW_COORD_MAX.
#define WINDOW_PLACE_MOST 16384
// The screens a placement can name, from 1 as it is typed.
#define WINDOW_PLACE_SCREENS 8u
// A name, its null included.
#define WINDOWS_NAME 16
// The variable, the words a placement has at most, and the room of its text.
#define WINDOW_PLACE_VARIABLE "MOONWATER_PLACE"
#define WINDOW_PLACE_WORDS 14u
#define WINDOW_PLACE_TEXT 112u
// A launch the user asked for (the launcher, a typed command, a terminal the keys opened) carries this variable as 1,
// and the first window its program opens takes the keys when it first shows. Any other window shows without them.
#define WINDOW_ASKED_VARIABLE "MOONWATER_ASKED"
#define WINDOW_ASKED_ENVIRONMENT 256

// A placement as it is parsed, and as a row's columns hold it.
struct window_place
{
        unsigned int how; // WINDOW_PLACE_*
        int x, y;
        unsigned int width, height, screen;
        char name[WINDOWS_NAME];
};

// glass: the window is frosted glass, its buffer premultiplied ARGB over it.
#define WINDOW_GLASS 1u
// The glass column's other bits: the window has no close button (none is drawn over it, none is hit), and a press
// anywhere but on it asks it to go, as the close button would (the launcher has both). WINDOW_NO_TAB: the window has
// no tab on the taskbar, as a popup has none (the identify overlays, which are on the screen for a few seconds only).
#define WINDOW_NO_CLOSE 2u
#define WINDOW_DISMISS 4u
#define WINDOW_NO_TAB 8u
// A window that draws its own frame, as a Wayland client does: the top of its picture is its own (a press there is not the
// compositor's to take for moving it), which a client moves by asking (xdg_toplevel.move).
#define WINDOW_NO_GRAB 16u
// The on-screen keyboard's two (src/canvas/ui/osk.c): WINDOW_NO_KEYS, the window never has the keys, not when it is made nor when a
// press lands on it (a tap on a key never moves the keys from the field); WINDOW_KEYBOARD, the window's key requests (the table's
// osk_*) are sent to the window with the keys, but only while a press of the person's is on this window (compositor_osk_read).
#define WINDOW_NO_KEYS 32u
#define WINDOW_KEYBOARD 64u
// A key request the keyboard makes, one ring of WINDOWS_OSK entries a row: the evdev code, the character (0 for none) and the
// WINDOW_KEY_* flags (DOWN, the modifiers, REPEAT). The program writes at osk_head and knocks; the compositor reads from osk_tail.
#define WINDOWS_OSK 16

// The pointer's shape over a program's controls, the compositor's CP_ codes (compositor.c checks they agree): the arrow, the
// default; the I-beam over a text field or a text well; the hand over a button, a tab, a switch or any other control that is pressed.
#define WINDOW_POINTER_ARROW 0u
#define WINDOW_POINTER_TEXT 5u
#define WINDOW_POINTER_HAND 8u

// What the compositor did not do of a screen's displays line (struct windows' output_refused): a refresh rate the screen
// does not offer at its size, which it keeps its mode for; a turn it cannot do, which it is left unturned for; and a place
// the file does not read (a bad x or y), which it is put at its default for.
#define WINDOWS_REFUSED_RATE 1u
#define WINDOWS_REFUSED_TURN 2u
#define WINDOWS_REFUSED_PLACE 4u

#define WINDOWS_LOOP_SOURCES 10 // the compositor loop's wake sources (metric_loop_by)
struct windows
{
        // The output: its pixels, how many of them one drawn pixel is, a frame in nanoseconds.
        unsigned int output_width, output_height, scale, period;
        unsigned int stride;   // pixels a row of every buffer
        unsigned int focus;    // the row with the keys, or WINDOWS
        // The compositor's: frames the card showed a window straight from its buffer, and why it would not when it
        // would not (1 no /dev/udmabuf, 2 udmabuf refused the buffer, 3 the card its import, 4 its framebuffer, 5 a flip;
        // the table is no memfd at 6 memfd_create, 8 its size, 9 its seals, 10 its mode, 11 the link to it).
        unsigned int scanned, refused;
        unsigned int refused_error; // the kernel's error (negated) that refused it, 0 when none
        // Why the top window was not shown straight at the last present: 0 it was; 1 no card; 2 the table is no memfd;
        // 3 the pointer is drawn in software; 4 no window, or one dragged or resized; 5 it is glass; 6 it is not at the
        // size it drew; 7 not wholly on the output; 8 too short past its corners and close button; 9 it meets the
        // tabs; 10 the card has no overlay for it; 11 the card refused its framebuffer (refused says why); 12 the card
        // refused the overlay's commit; 13 the desktop spans more than one screen, where every frame is composed; 14 a
        // finger's mark is drawn over it (`moonwater canvas touch on`); 15 a screen is turned (its displays line), so no window is shown straight; 16 the windows'
        // lists are drawn (list_mode): the card writes the pictures in place, so they are composed and none is shown straight.
        unsigned int unscanned;
        // The compositor's card for moonwater debug: its driver's name, whether the pointer is on a plane of its own,
        // and the overlay: 1 found, -1 none or refused, 0 not yet asked.
        char driver[16];
        unsigned int pointer_plane;
        int overlay;
        unsigned long pixels;  // where row 0's first buffer is, in bytes from the table
        unsigned long buffer;  // the bytes of one buffer

        /*
                The screens the desktop spans, the compositor's. Every x and y of
                the table is in one space across them, its top left 0, 0 (which
                the primary screen is not always at). A screen is named by its
                place in this list, and index 0 is the primary: the taskbar's
                and where a window opens. The rest follow in the arrangement's
                order. A screen's connector name (Virtual-1, HDMI-A-1, eDP-1)
                is what outlives a list that changes, so what is remembered of a
                screen is remembered by name. layout counts up each time the
                list changes. output_width and output_height above are the
                largest a window may be: the largest screen.
        */
        unsigned int outputs; // how many below are filled: 1 for one screen
        unsigned int layout;
        int output_x[WINDOWS_OUTPUTS], output_y[WINDOWS_OUTPUTS];
        unsigned int output_w[WINDOWS_OUTPUTS], output_h[WINDOWS_OUTPUTS];
        char output_name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        char output_model[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_MODEL]; // the monitor's name from its EDID, "" when it has none

        /*
                What each screen can be driven at, for the Displays field: output_rates of its refresh rates in Hz,
                each once, in the order the screen lists them at its size (output_hz); output_now, the rate it is
                at; and its turn in degrees (0, 90, 180 or 270), with output_software 1 when the turn is done by
                Canvas and 0 when the card does it, or none is.
        */
        unsigned int output_rates[WINDOWS_OUTPUTS], output_now[WINDOWS_OUTPUTS];
        unsigned int output_hz[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_HZ];
        unsigned int output_turn[WINDOWS_OUTPUTS], output_software[WINDOWS_OUTPUTS];

        /*
                What the displays file asks of each screen, and what the compositor did not do of it: output_asked_hz the
                refresh rate it asks (0 for the screen's own), output_asked_turn the turn it asks in degrees, and
                output_refused the WINDOWS_REFUSED_* bits. The settings window says the refusals on the screen's row.
        */
        unsigned int output_asked_hz[WINDOWS_OUTPUTS], output_asked_turn[WINDOWS_OUTPUTS], output_refused[WINDOWS_OUTPUTS];
        unsigned int output_asked_scale[WINDOWS_OUTPUTS]; // the scale its displays line asks, in percent (100 when none)

        /*
                The compositor's, for moonwater debug: each input device it reads, by its slot, and what became
                of the wheel. input_number is the device's eventN, or -1 for a slot that holds none; input_name
                is what the device calls itself, which is whatever bytes it chose, and is shown escaped;
                input_id its bus, vendor, product and version; input_kinds the WINDOWS_INPUT_* bits. Counted
                as the events are read: wheel_read the events of its four wheel codes, wheel_used those that
                turned a wheel (a wheel that reports in 120ths sends its notches twice, and one of the two is
                not used), dropped the times the kernel said events were lost because they were not read in
                time; wheel_last when its last wheel event was made, on the monotonic clock. wheel_sent,
                wheel_nowhere and wheel_full are what became of each turn: sent to a window, turned with no
                window under the pointer, or refused by a window's ring it had not read; wheel_row, wheel_x
                and wheel_y are the window the last was sent to and the place in it.
        */
        int input_number[WINDOWS_INPUTS];
        unsigned short input_id[WINDOWS_INPUTS][4];
        unsigned int input_kinds[WINDOWS_INPUTS];
        unsigned int input_wheel_read[WINDOWS_INPUTS], input_wheel_used[WINDOWS_INPUTS], input_dropped[WINDOWS_INPUTS];
        unsigned long input_wheel_last[WINDOWS_INPUTS];
        unsigned long long wheel_when[WINDOWS_INPUTS][WINDOW_WHEEL_RING]; // the used turns' times, a ring (wheel_turns is the count ever)
        unsigned char wheel_stream_flag[WINDOWS_INPUTS][WINDOW_WHEEL_RING]; // 1 when that turn was a stream's notch
        unsigned int wheel_turns[WINDOWS_INPUTS], wheel_streams[WINDOWS_INPUTS]; // the used turns ever, and how many were a stream's
        char input_name[WINDOWS_INPUTS][WINDOWS_INPUT_NAME];
        unsigned int wheel_sent, wheel_nowhere, wheel_full;
        int wheel_row, wheel_x, wheel_y;

        /*
                The compositor's: the theme Canvas's own windows draw in, as `moonwater canvas theme`, `canvas
                theme accent` and `canvas motion` keep it -- theme 0 light or 1 dark (auto is worked out to one of
                them by the clock), accent 0xrrggbb, motion 0 full or 1 reduced. themed counts up each time any
                of them changes, and every window is knocked, so a window takes a new theme without a restart.
                wheel_off is 1 when `moonwater canvas wheel smooth off` keeps it: a notch of the wheel then moves a whole row
                at once, with no glide (ui_wheel_turn), and themed counts it too.
                caret_speed and caret_thick are the caret's blink and width, `canvas caret blink` and `canvas caret thickness`,
                each as its place plus one: 0 is not kept, and the standard (normal, and 2 px) is what a window draws then.
        */
        unsigned int theme, accent, motion, themed;
        unsigned int wheel_off;
        // The compositor's: the scroll body's switches, `moonwater canvas scroll` and `canvas trackpad speed` as they are kept. scroll_off
        // holds WINDOW_SCROLL_FLIP, WINDOW_SCROLL_STOPS and WINDOW_SCROLL_STILL (0 is the standard); scroll_speed is the trackpad's speed
        // in percent, 25 to 400 (0 is 100). A change counts themed up, as a theme's does.
        unsigned int scroll_off, scroll_speed;
        unsigned int caret_speed, caret_thick;
        // The compositor's: the language the windows Canvas draws itself speak, `moonwater language` as /root/language keeps it,
        // packed by windows_language_code (0 for English, which "en" is too). A change counts themed up, as a theme's does.
        unsigned int language;
        // The compositor's: the text's size in percent, `moonwater canvas text scale` as /root/canvas.text.scale keeps it (50 to 300); 0
        // when none is kept, which is 100. A change counts themed up, as a theme's does.
        unsigned int text_scale;

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
        unsigned int glass[WINDOWS];  // the program's: WINDOW_GLASS, WINDOW_NO_CLOSE and WINDOW_DISMISS, read with the first picture
        // The program's: the shape its pointer wears over its controls (WINDOW_POINTER_*, the arrow when none). The compositor shows it,
        // and a press on the titlebar at the top of the window is the program's while the pointer is on one of its controls there.
        unsigned int pointer_shape[WINDOWS];
        unsigned int asked[WINDOWS];  // the launch's: 1 when the user asked for the program (MOONWATER_ASKED=1 at its start), so its first window takes the keys
        unsigned int ground[WINDOWS]; // the compositor's: what a glass window's tint came to, 0xrrggbb
        unsigned int window_scale[WINDOWS]; // the compositor's: the percent the window is drawn at, its screen's (0 until known)
        unsigned int lift[WINDOWS];   // anyone's: one more raises the window and gives it the keys
        // The program's, read by the compositor: how many of the window's text fields have the keys (ui.c's ui_field_focus moves
        // it with each field's focus; the program zeroes it when it starts). The on-screen keyboard shows while the focus's is not 0.
        unsigned int field[WINDOWS];
        // The program's, for moonwater debug: the scroll body that moved last in the window (ui.c's ui_body_note), its state (ui.c's
        // BODY_*), its position and the place it is drawn, in device pixels, and its speed in pixels a second.
        unsigned int body_state[WINDOWS];
        int body_raw[WINDOWS], body_drawn[WINDOWS], body_speed[WINDOWS];
        // The compositor's, for moonwater debug: the on-screen keyboard's setting (0 auto, 1 off, 2 on), whether it is shown, the
        // last input (0 the mouse, 1 a touch or a pen, 2 a key of a physical keyboard) and whether a physical keyboard is plugged in.
        // osk_start is the decision the desktop starts the keyboard by (`moonwater keyboard start`): 1 when the setting is on, or
        // auto with a touch screen plugged in or no physical keyboard.
        unsigned int osk_setting, osk_shown, osk_input, osk_physical, osk_start;
        // The compositor's: the keyboard layout's name, its two letters packed (us is 'u' | 's' << 8), which the keyboard lays its letters out by.
        unsigned int osk_layout;
        // The compositor's: the content purpose of the focus's text field (the protocol's content_purpose values, 0 normal, and 0 for a
        // window with no field from a Wayland client), and the bottom of its text cursor in the window's pixels (0 for none).
        unsigned int osk_purpose, osk_caret;
        // The keyboard's key requests (WINDOWS_OSK entries a row, the program's head and the compositor's tail, see WINDOWS_KEYBOARD).
        unsigned int osk_head[WINDOWS], osk_tail[WINDOWS];
        int osk_code[WINDOWS][WINDOWS_OSK], osk_character[WINDOWS][WINDOWS_OSK];
        unsigned int osk_flags[WINDOWS][WINDOWS_OSK];
        unsigned int hover_off[WINDOWS]; // the program's: 1 when it does not want the pointer's motion over it
        unsigned int pinch_wanted[WINDOWS]; // the program's: 1 when it takes WINDOW_EVENT_PINCH (the window pinch_wanted flag); 0 sends a pinch as Ctrl and a wheel
        /*
                The compositor's, for a window it shows straight from its buffers (scanned out, with no
                copy): pinned is 1 while it does, and a program then never gives a buffer's pages back,
                which the card is reading; spoiled is the rectangle of a buffer the compositor wrote the
                tabs over while it showed it, which the program draws again the next time it draws into
                that buffer, and clears. A height of 0 is nothing spoiled.
        */
        unsigned int pinned[WINDOWS];
        int spoiled_x[WINDOWS][2], spoiled_y[WINDOWS][2], spoiled_width[WINDOWS][2], spoiled_height[WINDOWS][2];

        // The program's, read with its first picture: where it asks to be put, and its name (WINDOW_PLACE_*).
        unsigned int place[WINDOWS];
        int place_x[WINDOWS], place_y[WINDOWS];
        unsigned int place_width[WINDOWS], place_height[WINDOWS], place_screen[WINDOWS];
        char name[WINDOWS][WINDOWS_NAME];
        // Anyone's, as lift is: one more puts a window that shows where its placement says now (`window move`).
        unsigned int moved[WINDOWS];
        // Anyone's, with a lift: the page a second start asks the window it lifts to show, its number and one (0: none), which the window takes once.
        unsigned int page[WINDOWS];

        /*
                The compositor's, for the launcher: where the panel's launcher button was last pressed, so the launcher it
                asks for opens beside it. button_x, button_y, button_w and button_h are the rectangle it opens beside (the
                button along the panel, the panel's whole thickness across it) in the table's space, button_edge the panel's
                edge of its screen (TASKBAR_BOTTOM 0, TOP 1, LEFT 2, RIGHT 3) and button_screen the screen's number. A width
                of 0 is no press: the launcher is then in the middle of the primary screen, as its key puts it.
        */
        int button_x, button_y;
        unsigned int button_w, button_h, button_edge, button_screen;

        unsigned int event_head[WINDOWS], event_tail[WINDOWS];
        unsigned short event_type[WINDOWS][WINDOWS_EVENTS], event_flags[WINDOWS][WINDOWS_EVENTS];
        int event_a[WINDOWS][WINDOWS_EVENTS], event_b[WINDOWS][WINDOWS_EVENTS];
        unsigned long long event_when[WINDOWS][WINDOWS_EVENTS]; // the device's stamp of each event (window_event.when), 0 for none

        /*
                The clipboard, an OS service the compositor holds (.github/SECURITY.md says what it guards). Two
                selections, CLIPBOARD (copy and paste) and PRIMARY (what was last selected, pasted with the middle
                button), each text/plain;charset=utf-8: valid UTF-8, no NUL, at most clip_limit bytes. A copy is a sealed memfd
                the copying program makes and names in its row (clip_offer_*), which the compositor opens through
                /proc/PID/fd and keeps -- no byte is copied, the copy outlives the window, and nothing the program
                does after can change it -- and a paste is that memfd opened again by the program the compositor
                lets read it: the window with the keys, right after a key or a button press of its own, and nothing
                else. The compositor's are clip_*: its pid, whether the service is on, each selection's count of
                changes, length and the row that copied it (-1 for none, or the command line), and the limit.
        */
        unsigned int clip_pid, clip_on, clip_limit;
        unsigned int clip_serial[2], clip_length[2];
        int clip_owner[2];
        // `moonwater clipboard set` (root): its pid and memfd, the selection, a count up, and the compositor's answer.
        unsigned int cli_pid, cli_fd, cli_which, cli_serial, cli_answer, cli_clear;
        // The program's: it wants to hear of copies; it offers a memfd for a selection; it asks to paste one.
        unsigned int clip_watch[WINDOWS];
        int clip_offer_fd[WINDOWS];
        unsigned int clip_offer_which[WINDOWS], clip_offer_serial[WINDOWS];
        unsigned int clip_ask_which[WINDOWS], clip_ask_serial[WINDOWS];
        // The compositor's answers: the offer it took (or refused, the serial with WINDOWS_CLIP_NO), the paste it gave.
        unsigned int clip_offer_answer[WINDOWS], clip_given_serial[WINDOWS], clip_given_length[WINDOWS];

        /*
                A popup: a menu or a list a window opens, which is a row of its own (so it may reach past its window's
                edges) placed by the compositor against an anchor in its parent and drawn over every window. The
                program writes popup_parent (the parent's row + 1; 0 is a window), the anchor (a rectangle in the
                parent's pixels, width and height 0 for a point) and popup_flags before the first picture; the
                compositor puts it below or right of the anchor, flipped to the other side where the screen holding
                the anchor ends, gives it the keys while it is open and still shows its parent as the one with them,
                and asks it to go (WINDOW_CLOSING) at a press outside every popup of the chain, its parent losing the
                keys or being moved, or its parent going.
        */
        unsigned int popup_parent[WINDOWS];
        int popup_x[WINDOWS], popup_y[WINDOWS], popup_w[WINDOWS], popup_h[WINDOWS];
        unsigned int popup_flags[WINDOWS];

        /*
                The GPU's compose, for moonwater canvas: the presents the compositor composed on the card (gpu_drawn) and on the
                processor (gpu_processor), and for each reason in WINDOWS_GPU_REASON how many presents the card was refused them
                (gpu_refused). A present is counted once, as drawn or as the processor's, and a reason counts the presents it
                refused (a turn the card refused is counted as the processor's turn, the present still the card's).
        */
        unsigned int gpu_drawn, gpu_processor;
        unsigned int gpu_refused[WINDOWS_GPU_REASONS];
        unsigned int gpu_map_why; // why the last map of a picture for the card was refused (gpu.c's gpu_map_why), 0 for none yet
        unsigned int gpu_family, gpu_device, gpu_map_probe; // the card's family and device id, and what the probes after a refused map found (gpu.c)
        char gpu_said[256];                                 // the last lines the kernel's log had about the card when a map was refused, joined with ' | '
        unsigned int gpu_faults; char gpu_fault[320]; // the jobs that found a page fault in the kernel's log, and the last one's text (gpu.c's gpu_fault, GPU_FAULT_BYTES): "" for none yet
        unsigned int gpu_map_user, gpu_map_dma; // the maps the card has, by userptr and by the dma-buf route
        unsigned int gpu_recovered; // the times the card was started again after it lost its state (a reset, a resume)
        unsigned int gpu_node_pick; // why the render node is the one it is: 1 the display's own device, 2 another device than the display's, 0 no display's bus known
        char gpu_node_bus[32], gpu_display_bus[32]; // the chosen node's bus and the display's ("pci:..."), "" when not known
        unsigned int gpu_req_checked, gpu_req_first, gpu_req_error, gpu_req_mask, gpu_req_mode, gpu_req_owner, gpu_req_route, gpu_req_route_error; // the prerequisites (gpu.c's gpu_req): whether they were checked, the first that failed (0 none), its error, the bits that passed, the node's mode and owner
        char gpu_req_driver[8], gpu_req_node[20];                                                           // the first node's driver name and path

        /*
                The window lists (window_list_of): WINDOWS lists of list_bytes each, after the pictures, row r's at lists + r * list_bytes from
                the table's start (lists 0: a table with none). list_mode is 1 when the compositor renders the lists its windows publish, 0 when
                every window paints its own frames; compositor_lists_set is the only writer. Per row, list_state is the compositor's: 0 the
                program's frames, 1 rendered from the list, 2 refused (the program paints its next frame, and the compositor clears 2 at that
                frame). list_seen is the compositor's: the generation of the last list it took. list_refused counts the lists it refused.
        */
        unsigned long lists, list_bytes;
        unsigned int list_mode;
        unsigned int list_state[WINDOWS];
        unsigned int list_seen[WINDOWS];
        unsigned int list_refused;
        unsigned int list_why[16]; // the compositor's: the lists drawn (0) and the lists refused by reason (compositor_list_why), for moonwater debug
        // The app's: the frames it recorded into its list (list_tried) and, of them, the ones that did not record whole, by cause (enum ui_list_lost).
        unsigned int list_tried[WINDOWS];
        unsigned int list_lost[WINDOWS][UI_LOST_COUNT];
        // The Wayland shim's state, for moonwater debug, written by the compositor on each wake: the shim's ms of the last input it was
        // given (0 when the shim is off), the idle inhibitors that hold (their surface is shown), the idle notifications that are idle,
        // the xdg_system_bell rings and toplevel icons set, and per window its content type (wp_content_type), whether its dialog is
        // modal (xdg-dialog) and whether it has an icon (xdg-toplevel-icon).
        unsigned long wl_input_ms;
        unsigned int wl_inhibitors, wl_idled, wl_bells, wl_icons, wl_shortcuts;
        unsigned char wl_content[WINDOWS], wl_modal[WINDOWS], wl_icon[WINDOWS];
        /*
                The issue board (windows_issue_set, windows_issues_read): issue_seq is a seqlock, odd while a writer is in the board; issue_lock is
                the writers' lock (a compare and swap, held for one write); issue_count the slots in use; issue_refused the sets that found no slot
                free, counted until a slot is freed. A reader takes a copy only when issue_seq was even and the same at both ends of it.
        */
        unsigned int issue_seq, issue_lock, issue_count, issue_refused;
        struct windows_issue issues[WINDOWS_ISSUES];
        /*
                The GPU's state for the board (windows_gpu_wanted, windows_gpu_judge), the compositor's: gpu_on is the canvas gpu setting, gpu_able
                is whether the lists go to the card (compositor_gpu_able), gpu_state is 0 up, 1 untrusted, 2 no card, 3 cooling down after hangs,
                and gpu_steps the cool-downs in a row.
        */
        unsigned int gpu_on, gpu_able, gpu_state, gpu_steps;
        // A client's (Wayland) picture: the committed pictures the compositor read where the client's buffer is (client_in_place) and the
        // ones it copied into the table (client_copied), and the presents the card drew with such a picture in place (client_card).
        unsigned int client_in_place, client_copied, client_card;
        /*
                The metrics (moonwater debug, and Settings' line under the GPU row), the compositor's: metric_card_* and metric_processor_* are the mean
                and the worst, in microseconds, of the last WINDOWS_METRIC_RUN presents the card drew and the processor drew (a whole present each);
                metric_maps the maps the card holds now, metric_map_hits and metric_map_misses the lookups that found a map and that did not;
                metric_wakes the clock-ramp wakes of the card, metric_jobs its jobs and metric_job_mean the mean of their wait in microseconds;
                metric_presents and metric_lists are the presents and the list frames drawn in the last second.
        */
        unsigned int metric_card_mean, metric_card_worst, metric_processor_mean, metric_processor_worst;
        unsigned int metric_maps, metric_map_hits, metric_map_misses, metric_wakes, metric_jobs, metric_job_mean;
        unsigned int metric_presents, metric_lists;
        // metric_loops: the compositor's loop turns in the last second, and metric_loop_idle the turns that waited for nothing (a timer already due).
        unsigned int metric_loops, metric_loop_idle;
        // metric_loop_by: the same turns a second by what returned events: signals, the watch, inputs, doors, done, card, uevents, clip,
        // wayland (xwm too), other (WINDOWS_LOOP_SOURCES of them).
        unsigned int metric_loop_by[WINDOWS_LOOP_SOURCES];
        // The kernel notices dismissed (windows_kernel_dismiss): bit i is class i (WINDOWS_ISSUE_KERNEL_GPU + i). Settings or moonwater sets a bit and
        // clears the class's issue; the feeder, the one that counts, takes the bit at its next publish and starts the class again from nothing.
        unsigned int kernel_dismiss;
        unsigned int client_rects_card, client_rects_cpu; // the damage rectangles copied by the card, and by the processor
};

#define WINDOWS_CLIP_CLIPBOARD 0u
#define WINDOWS_CLIP_PRIMARY 1u
#define WINDOWS_CLIP_CHANGED 1
#define WINDOWS_CLIP_READY 2
#define WINDOWS_CLIP_REFUSED 3
#define WINDOWS_CLIP_TAKEN 4
#define WINDOWS_CLIP_DECLINED 5
#define WINDOWS_CLIP_NO 0x80000000u
// The compositor keeps each selection's memfd at this descriptor and the next: numbers nothing else is given.
#define WINDOWS_CLIP_FD 900
#define WINDOWS_CLIP_LIMIT (16u << 20)
#define WINDOWS_CLIP_DOOR "/run/moonwater/clip"
// popup_flags: the anchor is a point a context menu opens at (else a rectangle a menu drops from: below it, or to its
// right for a submenu).
#define WINDOW_POPUP_POINT 1u
#define WINDOW_POPUP_RIGHT 2u
// The popup is put with its top left at the anchor point, as asked, with no flipping: its program has worked out the place.
#define WINDOW_POPUP_EXACT 4u

// Where the pixels start: the compositor's header, a page-rounded struct windows from the table's start. It is a
// constant here and not the header's pixels field, which any mapper of the table may write.
/*
        A wheel turn the compositor used, from input at, on its device's clock, and whether it was a stream's notch (the
        compositor's stream test, compositor_wheel_stream). The ring keeps the last WINDOW_WHEEL_RING.
*/
static void window_wheel_note(struct windows address_to table, unsigned int at, unsigned long long when, int stream)
{
        unsigned int slot = table->wheel_turns[at] % WINDOW_WHEEL_RING;

        table->wheel_when[at][slot] = when;
        table->wheel_stream_flag[at][slot] = stream != 0;
        table->wheel_turns[at]++;
        table->wheel_streams[at] += stream != 0;
}

struct window_wheel_report
{
        unsigned int notches, streams, peak, gaps; // within the span; the stream's among them; the most in any one second; the gaps between them
        unsigned long long least, median;          // the shortest gap and the median one, in ns (0 with fewer than two)
};

/*
        The readout for one input at now: the turns within WINDOW_WHEEL_SPAN_NS of now (a turn that is later than now is none),
        how many were a stream's, the most in one second of the span and the gaps between consecutive ones in it. A ring that
        has wrapped holds the last WINDOW_WHEEL_RING turns only, so a span with more than that is counted over those.
*/
static void window_wheel_report(const struct windows address_to table, unsigned int at, unsigned long long now, struct window_wheel_report address_to r)
{
        static unsigned long long kept[WINDOW_WHEEL_RING], gap[WINDOW_WHEEL_RING];
        unsigned int turns = table->wheel_turns[at], first = turns > WINDOW_WHEEL_RING ? turns - WINDOW_WHEEL_RING : 0, n = 0, second[5] = {0, 0, 0, 0, 0};

        memory_zero(r, sizeof(*r));
        for (unsigned int k = first; k < turns; k++)
        {
                unsigned int slot = k % WINDOW_WHEEL_RING;
                unsigned long long when = table->wheel_when[at][slot];

                if (when > now || now - when >= WINDOW_WHEEL_SPAN_NS)
                        continue;
                kept[n++] = when;
                r->notches++;
                r->streams += table->wheel_stream_flag[at][slot] != 0;
                second[(now - when) / 1000000000ull]++;
        }
        for (unsigned int k = 0; k < 5; k++)
                r->peak = second[k] > r->peak ? second[k] : r->peak;
        for (unsigned int k = 1; k < n; k++)
                if (kept[k] >= kept[k - 1])
                        gap[r->gaps++] = kept[k] - kept[k - 1];
        // The gaps are sorted by insertion: a span's gaps are a few hundred at most.
        for (unsigned int k = 1; k < r->gaps; k++)
        {
                unsigned long long v = gap[k];
                unsigned int j = k;

                while (j > 0 && gap[j - 1] > v)
                        gap[j] = gap[j - 1], j--;
                gap[j] = v;
        }
        if (r->gaps)
                r->least = gap[0], r->median = gap[r->gaps / 2];
}

#define WINDOWS_HEADER ((sizeof(struct windows) + 4095) & ~4095ul)

/*
        A window's list (ui/ui.c's format, the GPU's draw list): each row has one, WINDOW_LIST_BYTES after the pictures, memory the
        pages of which exist only where the program wrote them. WINDOW_LIST_ROOM is the ops a half holds: the kit's recorder and the
        compositor's reader take the room from the list's header, and both are made with these numbers.
*/
#define WINDOW_LIST_BYTES (4ul << 20)
#define WINDOW_LIST_ROOM 16384u

// Row row's list in the table, as the program and the compositor both reach it, or 0 when the table has none (lists is 0, a list
// of another size, or a row that is not one).
static inline unsigned char *window_list_of(const struct windows *table, int row)
{
        if (!table || !table->lists || table->list_bytes != WINDOW_LIST_BYTES || row < 0 || row >= WINDOWS)
                return 0;
        return (unsigned char *)table + table->lists + (unsigned long)row * table->list_bytes;
}

/*
        Whether a list (window_list_of's) may be rendered at width by height: the header is the kit's list, read as ui/ui.c reads it
        (magic, version, a generation even and the same after the read, the halves, the room, the cover bytes inside the list, and no
        lost flag), its room is WINDOW_LIST_ROOM, its picture is the row's size and its pitch is at least the width, and the damage it
        names is inside the picture. A list a program wrote is not trusted: each word is read once, by the reader. The ops are read
        after this, and the reader's ui_draw_list_still says whether they are the generation this answered for.
*/
static inline int window_list_valid(unsigned char *list, int width, int height)
{
        struct ui_draw_view view;

        if (!list || width <= 0 || height <= 0)
                return 0;
        if (ui_draw_list_open(list, WINDOW_LIST_BYTES, &view) != 1 || !ui_draw_list_still(list, &view))
                return 0;
        if (view.room != WINDOW_LIST_ROOM || view.width != (unsigned int)width || view.height != (unsigned int)height)
                return 0;
        if (view.pitch < (unsigned long)width)
                return 0;
        if (view.x1 > view.x0 && view.y1 > view.y0 && (view.x0 < 0 || view.y0 < 0 || view.x1 > width || view.y1 > height))
                return 0;
        return 1;
}

/*
        The issue board's functions (the struct's comment says what the board is). Writers are any holders of the table: a write takes
        issue_lock for its one change (a bounded wait, so a writer that died in the lock refuses writes and never hangs the others) and
        brackets it in the seqlock. A reader copies the board, and takes the copy only when the sequence was even and unchanged across the
        copy; whatever it copied is then checked on its own terms: a count is not trusted (the used slots are counted), each string is ended,
        a severity that is neither warning nor error is no issue, and a code past the list is kept as a code the reader does not name.
*/
#define WINDOWS_ISSUE_SPINS 100000u
#define WINDOWS_ISSUE_TRIES 16

struct windows_issues
{
        unsigned int count, refused; // the used slots, and the sets refused for want of a slot (until one is freed)
        struct windows_issue issue[WINDOWS_ISSUES];
};

static int windows_issue_same(const char *a, const char *b)
{
        for (unsigned int i = 0; i < WINDOWS_ISSUE_KEY; i++)
        {
                if (a[i] != b[i])
                        return 0;
                if (!a[i])
                        return 1;
        }
        return 1;
}

static int windows_issue_lock(struct windows *table)
{
        for (unsigned int spin = 0; spin < WINDOWS_ISSUE_SPINS; spin++)
        {
                unsigned int empty = 0;

                if (__atomic_compare_exchange_n(&table->issue_lock, &empty, 1u, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                        return 1;
        }
        return 0;
}

static unsigned int windows_issue_begin(struct windows *table)
{
        unsigned int seq = __atomic_load_n(&table->issue_seq, __ATOMIC_RELAXED) | 1u;

        __atomic_store_n(&table->issue_seq, seq, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        return seq;
}

static void windows_issue_end(struct windows *table, unsigned int seq)
{
        __atomic_store_n(&table->issue_seq, seq + 1, __ATOMIC_RELEASE);
        __atomic_store_n(&table->issue_lock, 0u, __ATOMIC_RELEASE);
}

// Raises an issue, or updates the one with its key and code in place. 1 when it is on the board; 0 when it is not: a key or a severity or a
// code that is none of ours, a board with no slot free (counted in issue_refused), or a lock a dead writer left held.
static int windows_issue_set(struct windows *table, const char *key, unsigned int severity, unsigned int code, unsigned int a, unsigned int b,
                             const char *detail)
{
        int at = -1, empty = -1;
        struct windows_issue *issue;
        unsigned int seq;

        if (!table || !key || !key[0] || (severity != WINDOWS_ISSUE_WARNING && severity != WINDOWS_ISSUE_ERROR) || code == WINDOWS_ISSUE_NONE ||
            code >= WINDOWS_ISSUE_COUNT)
                return 0;
        if (!windows_issue_lock(table))
                return 0;
        for (int i = 0; i < WINDOWS_ISSUES; i++)
        {
                if (table->issues[i].used && table->issues[i].code == code && windows_issue_same(table->issues[i].key, key))
                {
                        at = i;
                        break;
                }
                if (!table->issues[i].used && empty < 0)
                        empty = i;
        }
        if (at < 0 && empty < 0)
        {
                seq = windows_issue_begin(table);
                table->issue_refused++;
                windows_issue_end(table, seq);
                return 0;
        }
        seq = windows_issue_begin(table);
        issue = &table->issues[at >= 0 ? at : empty];
        if (at < 0)
                issue->used = 1, table->issue_count++;
        string_copy_bounded((string_address)issue->key, (string_address)key, WINDOWS_ISSUE_KEY);
        issue->severity = severity, issue->code = code, issue->a = a, issue->b = b;
        string_copy_bounded((string_address)issue->detail, (string_address)(detail ? detail : ""), WINDOWS_ISSUE_DETAIL);
        windows_issue_end(table, seq);
        return 1;
}

// Clears the issues of a key: the one with code, or every one of the key when code is WINDOWS_ISSUE_ANY. The number cleared.
static unsigned int windows_issue_clear(struct windows *table, const char *key, unsigned int code)
{
        unsigned int cleared = 0, seq;

        if (!table || !key || !key[0] || !windows_issue_lock(table))
                return 0;
        seq = windows_issue_begin(table);
        for (int i = 0; i < WINDOWS_ISSUES; i++)
                if (table->issues[i].used && (code == WINDOWS_ISSUE_ANY || table->issues[i].code == code) && windows_issue_same(table->issues[i].key, key))
                {
                        memory_zero(&table->issues[i], sizeof(table->issues[i]));
                        table->issue_count--, cleared++;
                }
        if (table->issue_count < WINDOWS_ISSUES)
                table->issue_refused = 0;
        windows_issue_end(table, seq);
        return cleared;
}

// Copies the board: 1 when the copy is one the writers left whole, 0 when they kept writing through every try (the caller says the board is
// busy and reads it again later). The copy is sanitized here, whatever the table says of itself.
static int windows_issues_read(const struct windows *table, struct windows_issues *into)
{
        for (int tries = 0; table && into && tries < WINDOWS_ISSUE_TRIES; tries++)
        {
                unsigned int before = __atomic_load_n(&table->issue_seq, __ATOMIC_ACQUIRE), after;
                unsigned int count = 0;

                if (before & 1u)
                        continue;
                memory_copy(into->issue, table->issues, sizeof(table->issues));
                into->refused = table->issue_refused;
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                after = __atomic_load_n(&table->issue_seq, __ATOMIC_RELAXED);
                if (before != after)
                        continue;
                for (int i = 0; i < WINDOWS_ISSUES; i++)
                {
                        struct windows_issue *issue = &into->issue[i];

                        issue->key[WINDOWS_ISSUE_KEY - 1] = 0, issue->detail[WINDOWS_ISSUE_DETAIL - 1] = 0;
                        if (!issue->used || !issue->key[0] || (issue->severity != WINDOWS_ISSUE_WARNING && issue->severity != WINDOWS_ISSUE_ERROR))
                        {
                                memory_zero(issue, sizeof(*issue));
                                continue;
                        }
                        count++;
                }
                into->count = count;
                return 1;
        }
        if (into)
                memory_zero(into, sizeof(*into));
        return 0;
}

// The lines an expander shows under a key: when open, each issue's message and, when it has a detail that is not the GPU's prerequisite's fix
// (which the message says), the detail too; none when shut.
static unsigned int windows_issue_lines(const struct windows_issues *all, const char *key, unsigned int open)
{
        unsigned int lines = 0;

        for (int i = 0; open && all && key && i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && windows_issue_same(all->issue[i].key, key))
                        lines += 1 + (all->issue[i].detail[0] && all->issue[i].code != WINDOWS_ISSUE_GPU_REQUIRES);
        return lines;
}

// The worst severity of a key's issues on a copy (0 none, WINDOWS_ISSUE_WARNING or WINDOWS_ISSUE_ERROR), and how many there are.
static unsigned int windows_issues_worst(const struct windows_issues *all, const char *key, unsigned int *count)
{
        unsigned int worst = 0;

        if (count)
                *count = 0;
        for (int i = 0; all && key && i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && windows_issue_same(all->issue[i].key, key))
                {
                        worst = all->issue[i].severity > worst ? all->issue[i].severity : worst;
                        if (count)
                                (*count)++;
                }
        return worst;
}

// The worst severity of every issue on a copy (0 none, WINDOWS_ISSUE_WARNING or WINDOWS_ISSUE_ERROR), and how many there are: what the tray's
// notices dot says. Kernel notices and the options' issues alike; a dismissed kernel notice is none (windows_kernel_dismiss clears it).
static unsigned int windows_issues_summary(const struct windows_issues *all, unsigned int *count)
{
        unsigned int worst = 0;

        if (count)
                *count = 0;
        for (int i = 0; all && i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && all->issue[i].key[0])
                {
                        worst = all->issue[i].severity > worst ? all->issue[i].severity : worst;
                        if (count)
                                (*count)++;
                }
        return worst;
}

// The metrics' rings: the last samples of a path's presents (nanoseconds), and the mean and the worst of them in microseconds. A rate is a count over
// the nanoseconds it took, per second.
#define WINDOWS_METRIC_RUN 128
static void windows_metric_stats(const unsigned long long *ring, unsigned int count, unsigned int *mean_us, unsigned int *worst_us)
{
        unsigned long long sum = 0, worst = 0;

        if (count > WINDOWS_METRIC_RUN)
                count = WINDOWS_METRIC_RUN;
        for (unsigned int i = 0; i < count; i++)
        {
                sum += ring[i];
                worst = ring[i] > worst ? ring[i] : worst;
        }
        *mean_us = count ? (unsigned int)(sum / count / 1000) : 0;
        *worst_us = (unsigned int)(worst / 1000);
}

static unsigned int windows_metric_rate(unsigned long long count, unsigned long long nanoseconds)
{
        return nanoseconds ? (unsigned int)(count * 1000000000ull / nanoseconds) : 0;
}

// The codes the GPU raises under its key, in the order the board shows them (windows_gpu_wanted says which hold now).
static const unsigned int windows_gpu_codes[] = {
        WINDOWS_ISSUE_GPU_USERPTR, WINDOWS_ISSUE_GPU_MAP, WINDOWS_ISSUE_GPU_UNTRUSTED, WINDOWS_ISSUE_GPU_FAMILY, WINDOWS_ISSUE_GPU_COOLING,
        WINDOWS_ISSUE_GPU_REFUSED, WINDOWS_ISSUE_GPU_LISTS, WINDOWS_ISSUE_GPU_LOST, WINDOWS_ISSUE_GPU_SCANOUT,
        WINDOWS_ISSUE_GPU_HUNG, WINDOWS_ISSUE_GPU_FAULT};
#define WINDOWS_GPU_CODES (sizeof(windows_gpu_codes) / sizeof(windows_gpu_codes[0]))
// The GPU states (the table's gpu_state, and what Settings' status line says): the card is up, untrusted, not there, or cooling down.
#define WINDOWS_GPU_UP 0u
#define WINDOWS_GPU_UNTRUSTED 1u
#define WINDOWS_GPU_NO_CARD 2u
#define WINDOWS_GPU_COOLING 3u
#define WINDOWS_GPU_HUNG 4u // given up for the session after two hangs within a minute (compositor_gpu_job_failed)
// What Settings says the GPU does: off (the setting), no supported GPU, in use, partly in use, not in use (windows_gpu_judge).
#define WINDOWS_GPU_STATUS_OFF 0u
#define WINDOWS_GPU_STATUS_NONE 1u
#define WINDOWS_GPU_STATUS_IN_USE 2u
#define WINDOWS_GPU_STATUS_PARTLY 3u
#define WINDOWS_GPU_STATUS_NOT 4u
// A refusal reason is shown when it refused at least this many presents and at least one in this many of the presents the card was given.
#define WINDOWS_GPU_REFUSAL_MIN 8u
#define WINDOWS_GPU_REFUSAL_SHARE 20u
// The lost frames worth an issue: this many, and one in ten of the frames tried.
#define WINDOWS_GPU_LOST_MIN 8u

// Whether text has the bytes of word (the reader's own strings are ended, so the search stops at the null).
static int windows_has_word(const char *text, const char *word)
{
        for (unsigned int at = 0; text[at]; at++)
        {
                unsigned int k = 0;

                while (word[k] && text[at + k] == word[k])
                        k++;
                if (!word[k])
                        return 1;
        }
        return 0;
}

// The issues the GPU has now, from the table (the compositor's counters, which are read here and never written): each is set in want[] with its
// severity, code, numbers and detail, and the number of them is returned (at most room). A code not in the answer is not raised: the caller clears
// it. What the GPU is not doing is the answer, when the setting is off: none.
static unsigned int windows_gpu_wanted(const struct windows *table, struct windows_issue *want, unsigned int room)
{
        unsigned int count = 0, step, error, total, refused_top = 0, refused_why = 0, tried = 0, lost = 0;

        if (!table || !want || !table->gpu_on)
                return 0;
        // A new issue is added at the end of the answer, from the board's point of view in the order of the codes it knows.
        #define WANT(sev, cod, x, y, text) do { if (count < room) { struct windows_issue *w = &want[count++]; memory_zero(w, sizeof(*w)); \
                w->severity = (sev), w->code = (cod), w->a = (x), w->b = (y); string_copy_bounded((string_address)w->detail, (string_address)(text), WINDOWS_ISSUE_DETAIL); } } while (0)
        step = table->gpu_map_why & 255u, error = table->gpu_map_why >> 8;
        // A map refused for the card's own sake (9: a picture it reads in this present is held, the processor draws it) is not the kernel's refusal.
        if (step && step != 9u && (windows_has_word(table->gpu_said, "HMM_MIRROR") || (step == 3 && error == 19)))
                WANT(WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_USERPTR, step, error, table->gpu_said);
        else if (step && step != 9u)
                WANT(WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_MAP, step, error, table->gpu_said);
        if (table->gpu_state == WINDOWS_GPU_UNTRUSTED)
                WANT(WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_UNTRUSTED, table->gpu_family, 0, "");
        if (table->gpu_state == WINDOWS_GPU_NO_CARD && table->gpu_family)
                WANT(WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_FAMILY, table->gpu_family, 0, "");
        if (table->gpu_state == WINDOWS_GPU_COOLING)
                WANT(WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_COOLING, table->gpu_steps, 0, "");
        if (table->gpu_state == WINDOWS_GPU_HUNG)
                WANT(WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_HUNG, 0, 0, "");
        if (table->gpu_fault[0])
                WANT(WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_FAULT, table->gpu_faults, 0, table->gpu_fault);
        for (unsigned int why = 1; why < WINDOWS_GPU_REASONS; why++)
                if (table->gpu_refused[why] > refused_top)
                        refused_top = table->gpu_refused[why], refused_why = why;
        total = table->gpu_drawn + table->gpu_processor;
        if (refused_top >= WINDOWS_GPU_REFUSAL_MIN && refused_top * WINDOWS_GPU_REFUSAL_SHARE >= total && refused_why)
                WANT(WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_REFUSED, refused_top, refused_why, windows_gpu_reasons[refused_why]);
        if (!table->gpu_able && table->gpu_state == WINDOWS_GPU_UP && table->gpu_drawn + table->gpu_processor > 0)
                WANT(WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, 0, 0, "");
        for (unsigned int row = 0; row < WINDOWS; row++)
                if (table->pid[row] && table->list_tried[row])
                {
                        tried += table->list_tried[row];
                        for (unsigned int why = 0; why < UI_LOST_COUNT; why++)
                                lost += table->list_lost[row][why];
                }
        if (lost >= WINDOWS_GPU_LOST_MIN && lost * 10 >= tried)
                WANT(WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LOST, lost, tried, "");
        if (table->refused && table->refused_error)
                WANT(WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_SCANOUT, table->refused_error, 0, "");
        #undef WANT
        return count;
}

// What Settings' status line says the GPU does, from the table's counters since the last judgement (drawn and processor presents of the
// last sample): previous is the answer last time, so a share that moves about a threshold does not flap. Fewer than 16 presents keep it.
static unsigned int windows_gpu_judge(const struct windows *table, unsigned int previous, unsigned int drawn, unsigned int processor)
{
        unsigned int total = drawn + processor, share;

        if (!table->gpu_on)
                return WINDOWS_GPU_STATUS_OFF;
        if (table->gpu_state == WINDOWS_GPU_NO_CARD && !table->gpu_family)
                return WINDOWS_GPU_STATUS_NONE;
        if (total < 16)
                return previous == WINDOWS_GPU_STATUS_OFF || previous == WINDOWS_GPU_STATUS_NONE ? WINDOWS_GPU_STATUS_NOT : previous;
        share = drawn * 100 / total;
        if (previous == WINDOWS_GPU_STATUS_IN_USE)
                return share >= 65 ? WINDOWS_GPU_STATUS_IN_USE : share > 15 ? WINDOWS_GPU_STATUS_PARTLY : WINDOWS_GPU_STATUS_NOT;
        if (previous == WINDOWS_GPU_STATUS_NOT)
                return share <= 15 ? WINDOWS_GPU_STATUS_NOT : share >= 85 ? WINDOWS_GPU_STATUS_IN_USE : WINDOWS_GPU_STATUS_PARTLY;
        return share >= 85 ? WINDOWS_GPU_STATUS_IN_USE : share <= 15 ? WINDOWS_GPU_STATUS_NOT : WINDOWS_GPU_STATUS_PARTLY;
}

/*
        The kernel's own notices (moonwater's feeder): /dev/kmsg, the kernel's log, read without waiting, each record classified and the classes put on
        the board. A record is raised when its level is emergency to error (0 to 3), or when its words are one of the classes' that matter whatever the
        level (a warning from amdgpu about a ring that timed out, a program that crashed, an out of memory). Level 4 is raised only by those words;
        levels 5 to 7 are never raised. Lines the kernel prints on every boot and that are not faults are quiet (kernel_notice_quiet). A class is one
        issue on the board with the records of it counted, its worst severity, the seconds since its last record and its last line, sanitized: the
        text is copied in as plain bytes, a control byte or an escape becomes a blank, and the line is cut to the detail's room. A flood of one
        class is still one issue, and a warning not seen again for KERNEL_NOTICE_AGE_NS is cleared; an error is kept until it is dismissed.
*/
#define KMSG_FILE "/dev/kmsg"
#define KERNEL_NOTICE_MOST 256
#define KERNEL_NOTICE_LINE 1024
#define KERNEL_NOTICE_AGE_NS (600ull * 1000000000ull)
static const char *const kernel_notice_key[KERNEL_NOTICE_CLASSES] = {
#define WINDOWS_KERNEL_KEY(name, key, text) key,
    WINDOWS_KERNEL_CLASSES(WINDOWS_KERNEL_KEY)
#undef WINDOWS_KERNEL_KEY
};
// The lines the kernel prints on normal boots that are not faults (every one is here for a reason the box's log showed).
static const char *const kernel_notice_quiet[] = {
    "LTTPR count is nonzero", // the amdgpu driver's DisplayPort repeater message, every mode set on a machine with no repeater
    "ACPI BIOS Error",        // firmware's own tables, on most boards
    "Direct firmware load",   // an optional blob that is not there
    "RDSEED32 is broken",     // a CPU bug the kernel works around
    "kauditd_printk_skb",     // audit's count of the messages it suppressed
    "Moonwater starting",     // the module's own banner, printed at the alert level so a quiet boot still shows it
    "Spark format unregistered", // its farewell at the same level
    0};

struct kernel_notices
{
        int fd;                                                                  // /dev/kmsg: -1 until it is first read, -2 when it cannot be
        unsigned int count[KERNEL_NOTICE_CLASSES], severity[KERNEL_NOTICE_CLASSES]; // the records of each class, and its worst severity
        unsigned long long seen[KERNEL_NOTICE_CLASSES];                         // when its last record was read (monotonic ns), 0 for none
        char last[KERNEL_NOTICE_CLASSES][WINDOWS_ISSUE_DETAIL];                  // its last line, sanitized
};
static struct kernel_notices kernel_notice_state = {.fd = -1};

// Whether text (length bytes, not ended) has any of words (ended): the alternatives, split at a bar.
static int kernel_notice_has(const char *text, unsigned int length, const char *words)
{
        for (unsigned int from = 0, to = 0, k; words[from]; from = to + (words[to] == '|'))
        {
                for (to = from; words[to] && words[to] != '|'; to++)
                        ;
                for (unsigned int at = 0; at + to - from <= length; at++)
                {
                        for (k = 0; k < to - from && text[at + k] == words[from + k]; k++)
                                ;
                        if (k == to - from)
                                return 1;
                }
        }
        return 0;
}

// What makes a line a class's, asked in this order, the first that holds wins (the system's crashes and a program's segfault come before the drivers').
// words: one of these alternatives is in the line; needs, when there are some: so is one of these (a line of level 3 or less needs none). severity is
// the one it is raised at, or 0 for the level's: an error up to level 3, else a warning. A line none holds is the system's error at level 3 or less.
static const struct { unsigned int code, severity; const char *words, *needs; } kernel_notice_rules[] = {
    {WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR, "Out of memory|oom_kill_process|Killed process|BUG:|Oops|Kernel panic|WARNING: CPU", null},
    {WINDOWS_ISSUE_KERNEL_PROGRAM, WINDOWS_ISSUE_WARNING, "segfault at", null},
    {WINDOWS_ISSUE_KERNEL_GPU, 0, "amdgpu|drm|i915", "timeout|reset|fault|failed"},
    {WINDOWS_ISSUE_KERNEL_USB, 0, "xhci|usb", "overrun|disconnect|failed"},
    {WINDOWS_ISSUE_KERNEL_WIFI, 0, "iwlwifi|ath1|ath9|mt76|brcm|cfg80211", "firmware|timeout|error"},
    {WINDOWS_ISSUE_KERNEL_BLUETOOTH, 0, "Bluetooth:", "failed|error"},
    {WINDOWS_ISSUE_KERNEL_AUDIO, 0, "snd_|hda", "no codecs|error|failed"},
    {WINDOWS_ISSUE_KERNEL_STORAGE, 0, "nvme|ext4|btrfs|xfs|mmc|FAT-fs|I/O error|Buffer I/O", "I/O error|Buffer I/O|not properly unmounted|error|timeout"},
    {WINDOWS_ISSUE_KERNEL_SYSTEM, 0, "thermal|throttl|Hardware Error|mce:|EDAC", null}};

// The class of a line of the kernel's log at level, and the severity it is raised at (WINDOWS_ISSUE_*), or 0 when it is not raised: quiet lines,
// and a level of 4 or more without the words that make a class of its own. Written as the table of the box's real lines it is checked against.
static unsigned int kernel_notice_class(const char *text, unsigned int length, unsigned int level, unsigned int *severity)
{
        *severity = 0;
        for (int q = 0; kernel_notice_quiet[q]; q++)
                if (kernel_notice_has(text, length, kernel_notice_quiet[q]))
                        return 0;
        for (unsigned int r = 0; r < sizeof(kernel_notice_rules) / sizeof(kernel_notice_rules[0]); r++)
                if (kernel_notice_has(text, length, kernel_notice_rules[r].words) &&
                    (!kernel_notice_rules[r].needs || level <= 3 || kernel_notice_has(text, length, kernel_notice_rules[r].needs)))
                {
                        *severity = kernel_notice_rules[r].severity ? kernel_notice_rules[r].severity : level <= 3 ? WINDOWS_ISSUE_ERROR : WINDOWS_ISSUE_WARNING;
                        return kernel_notice_rules[r].code;
                }
        return level <= 3 ? (*severity = WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_KERNEL_SYSTEM) : 0;
}

// One record of /dev/kmsg: "priority,sequence,time,flags;message", the message ended by a newline (the lines after it, which begin with a blank,
// are not read). Its level is the priority's low three bits. 0 when the record is not one.
static int kernel_notice_parse(const char *record, unsigned int length, unsigned int *level, const char **message, unsigned int *message_length)
{
        unsigned int at = 0, priority = 0, start, stop;

        while (at < length && record[at] >= '0' && record[at] <= '9')
        {
                priority = priority * 10 + (unsigned int)(record[at] - '0');
                if (priority > 100000u)
                        return 0;
                at++;
        }
        if (!at || at >= length || record[at] != ',')
                return 0;
        while (at < length && record[at] != ';' && record[at] != '\n')
                at++;
        if (at >= length || record[at] != ';')
                return 0;
        start = ++at;
        for (stop = start; stop < length && record[stop] != '\n'; stop++)
                ;
        *level = priority & 7u, *message = record + start, *message_length = stop - start;
        return 1;
}

// The kernel's words, copied in as plain text: a control byte or an escape is a blank, and the copy stops at the room (ended).
static void kernel_notice_sanitize(char *into, unsigned int room, const char *text, unsigned int length)
{
        unsigned int at = 0;

        for (unsigned int i = 0; i < length && at + 1 < room; i++)
        {
                unsigned char c = (unsigned char)text[i];

                into[at++] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
        }
        into[at] = 0;
}

// Counts one record of the kernel's log under its class, when it is raised: 1 when it was, 0 when it is none (a bad record, quiet, or a level
// nothing raises).
static int kernel_notices_record(struct kernel_notices *k, const char *record, unsigned int length, unsigned long long now)
{
        unsigned int level = 0, severity = 0, message_length = 0, code;
        const char *message = null;
        unsigned int idx;

        if (!kernel_notice_parse(record, length, &level, &message, &message_length))
                return 0;
        code = kernel_notice_class(message, message_length, level, &severity);
        if (!code)
                return 0;
        idx = code - WINDOWS_ISSUE_KERNEL_GPU;
        if (k->count[idx] < 0xffffffffu)
                k->count[idx]++;
        if (severity > k->severity[idx])
                k->severity[idx] = severity;
        k->seen[idx] = now;
        kernel_notice_sanitize(k->last[idx], sizeof(k->last[idx]), message, message_length);
        return 1;
}

// Puts the classes on the board: a class with records is set (its count, its worst severity, the seconds since its last record, its last line),
// and one whose warning has not come again for KERNEL_NOTICE_AGE_NS is cleared and its count started again. An error stays until it is dismissed.
static void kernel_notices_publish(struct windows *table, struct kernel_notices *k, unsigned long long now)
{
        for (unsigned int idx = 0; idx < KERNEL_NOTICE_CLASSES; idx++)
        {
                unsigned int code = WINDOWS_ISSUE_KERNEL_GPU + idx;

                // A class dismissed by its reader starts again from nothing, and its bit is taken.
                if (table && (__atomic_load_n(&table->kernel_dismiss, __ATOMIC_ACQUIRE) >> idx & 1u))
                {
                        k->count[idx] = 0, k->severity[idx] = 0;
                        __atomic_and_fetch(&table->kernel_dismiss, ~(1u << idx), __ATOMIC_RELEASE);
                }
                if (!k->count[idx])
                        continue;
                if (k->severity[idx] == WINDOWS_ISSUE_WARNING && k->seen[idx] && now - k->seen[idx] > KERNEL_NOTICE_AGE_NS)
                {
                        if (table)
                                windows_issue_clear(table, kernel_notice_key[idx], code);
                        k->count[idx] = 0, k->severity[idx] = 0;
                        continue;
                }
                if (table)
                        windows_issue_set(table, kernel_notice_key[idx], k->severity[idx], code, k->count[idx] > 65535u ? 65535u : k->count[idx],
                                          (unsigned int)((now - k->seen[idx]) / 1000000000ull), k->last[idx]);
        }
}


/*
        A language's code as the table carries it: its two or three lower case letters (an ISO 639 code), the first in
        the low byte, or 0 when the word is none of that. Here because the command, the compositor and every window
        read the same word.
*/
static unsigned int windows_language_code(const char *word, unsigned int length)
{
        unsigned int code = 0;

        if (length < 2 || length > 3)
                return 0;
        for (unsigned int at = 0; at < length; at++)
        {
                if (word[at] < 'a' || word[at] > 'z')
                        return 0;
                code |= (unsigned int)word[at] << 8 * at;
        }
        return code;
}

/*
        The arrangement of the screens, which the compositor lays them by and
        `moonwater canvas arrange` keeps (/root/canvas.arrange): words, each
        a screen's connector name alone (laid to the right of the ones
        before it, tops level), NAME@X,Y (at that place) or NAME@off (dark),
        in the order they are laid, the first the primary; screens no word
        names follow in the card's order; default alone is the card's order.
        Here because the command and the compositor both judge the words.
*/
// A word of the arrangement, as long as length: whether it is one, NAME, NAME@X,Y or NAME@off, with the name's length
// and what it says. A name is letters, digits and dashes, as a connector's is.
#define WINDOWS_PLACE_FLOW 0
#define WINDOWS_PLACE_AT 1
#define WINDOWS_PLACE_OFF 2
#define WINDOWS_PLACE_MOST 32768

static int windows_arrange_word(const char *word, unsigned int length, unsigned int *name, int *how, int *x, int *y)
{
        unsigned int at = 0;

        while (at < length && ((unsigned int)((word[at] | 32) - 'a') < 26u || (word[at] >= '0' && word[at] <= '9') ||
                               word[at] == '-'))
                at++;
        *name = at;
        *how = WINDOWS_PLACE_FLOW;
        *x = *y = 0;
        if (!at || at >= WINDOWS_OUTPUT_NAME)
                return 0;
        if (at == length)
                return 1;
        if (word[at++] != '@')
                return 0;
        if (length - at == 3 && word[at] == 'o' && word[at + 1] == 'f' && word[at + 2] == 'f')
        {
                *how = WINDOWS_PLACE_OFF;
                return 1;
        }
        for (int axis = 0; axis < 2; axis++)
        {
                int sign = 1, value = 0, digits = 0, *into = axis ? y : x;

                if (at < length && word[at] == '-')
                        sign = -1, at++;
                while (at < length && word[at] >= '0' && word[at] <= '9' && digits < 6)
                        value = value * 10 + (word[at++] - '0'), digits++;
                if (!digits || value > WINDOWS_PLACE_MOST)
                        return 0;
                *into = sign * value;
                if (!axis && (at >= length || word[at++] != ','))
                        return 0;
        }
        *how = WINDOWS_PLACE_AT;
        return at == length;
}

// Whether count bytes at a and at b are the same.
static int windows_same(const char *a, const char *b, unsigned int count)
{
        while (count && *a == *b)
                a++, b++, count--;
        return !count;
}

// Whether a whole arrangement is one: default or nothing, or words each of which is one, a name once.
static int windows_arrange_valid(const char *text)
{
        unsigned int at = 0, words = 0, starts[WINDOWS_OUTPUTS * 2], names[WINDOWS_OUTPUTS * 2];

        while (text[at])
        {
                unsigned int start, name;
                int how, x, y;

                while (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')
                        at++;
                if (!text[at])
                        break;
                start = at;
                while (text[at] && text[at] != ' ' && text[at] != '\t' && text[at] != '\n' && text[at] != '\r')
                        at++;
                // default is a word alone, never a screen's name among others.
                if (at - start == 7 && windows_same(text + start, "default", 7))
                {
                        while (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')
                                at++;
                        return !words && !text[at];
                }
                if (words == WINDOWS_OUTPUTS * 2 || !windows_arrange_word(text + start, at - start, &name, &how, &x, &y))
                        return 0;
                for (unsigned int other = 0; other < words; other++)
                        if (names[other] == name && windows_same(text + starts[other], text + start, name))
                                return 0;
                starts[words] = start;
                names[words++] = name;
        }
        return 1;
}

/*
        The arrangement: count screens in the card's order, each its name and
        its width and height, laid by the words of the setting (default or
        none for the card's own order). Each screen's rectangle comes back in
        rects, x, y, width, height, in the card's order, with a width of 0 for
        one that is dark; order is the screens lit, the primary first and the
        rest as they were laid. Answers how many are lit, at least one when
        any is connected: a setting that would leave every screen dark lights
        the first.
*/
static unsigned int windows_arrange_lay(const char *text, const char (*names)[WINDOWS_OUTPUT_NAME], const int (*sizes)[2],
                                           unsigned int count, int (*rects)[4], unsigned char *order)
{
        unsigned char laid[WINDOWS_OUTPUTS] = {0}, placed[WINDOWS_OUTPUTS] = {0}, dark[WINDOWS_OUTPUTS] = {0};
        int place[WINDOWS_OUTPUTS][2] = {{0}}, valid = windows_arrange_valid(text);
        unsigned int lit = 0, at = 0, used = 0;
        int right = 0, low_x = 0, low_y = 0;

        count = count < WINDOWS_OUTPUTS ? count : WINDOWS_OUTPUTS;

        // The named screens first, in the setting's order; a name no screen has is a screen not plugged in now.
        while (valid && text[at])
        {
                unsigned int start, name;
                int how, x, y;

                while (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')
                        at++;
                start = at;
                while (text[at] && text[at] != ' ' && text[at] != '\t' && text[at] != '\n' && text[at] != '\r')
                        at++;
                if (at == start || !windows_arrange_word(text + start, at - start, &name, &how, &x, &y))
                        continue;
                for (unsigned int screen = 0; screen < count; screen++)
                {
                        unsigned int same = 0;

                        while (same < name && names[screen][same] == text[start + same])
                                same++;
                        if (same != name || names[screen][name] || laid[screen])
                                continue;
                        laid[screen] = 1;
                        dark[screen] = how == WINDOWS_PLACE_OFF;
                        placed[screen] = how == WINDOWS_PLACE_AT;
                        place[screen][0] = x, place[screen][1] = y;
                        order[used++] = (unsigned char)screen;
                }
        }
        for (unsigned int screen = 0; screen < count; screen++)
                if (!laid[screen])
                        order[used++] = (unsigned char)screen;

        for (unsigned int screen = 0; screen < count; screen++)
                lit += !dark[screen];
        if (!lit && count)
                dark[0] = 0;

        // Those given a place have it; the rest go to the right of everything laid before them, tops level.
        lit = 0;
        for (unsigned int each = 0; each < used; each++)
        {
                unsigned int screen = order[each];

                if (dark[screen])
                        continue;
                rects[screen][0] = placed[screen] ? place[screen][0] : right;
                rects[screen][1] = placed[screen] ? place[screen][1] : 0;
                rects[screen][2] = sizes[screen][0];
                rects[screen][3] = sizes[screen][1];
                right = rects[screen][0] + rects[screen][2] > right || !lit ? rects[screen][0] + rects[screen][2] : right;
                low_x = !lit || rects[screen][0] < low_x ? rects[screen][0] : low_x;
                low_y = !lit || rects[screen][1] < low_y ? rects[screen][1] : low_y;
                order[lit++] = (unsigned char)screen;
        }
        for (unsigned int screen = 0; screen < count; screen++)
                if (dark[screen])
                        rects[screen][0] = rects[screen][1] = rects[screen][2] = rects[screen][3] = 0;
                else
                        rects[screen][0] -= low_x, rects[screen][1] -= low_y;
        return lit;
}

/*
        The displays (/root/canvas.displays): one line a screen that has any
        setting, NAME rotation=0|90|180|270 refresh=preferred|HZ x=X y=Y, the
        fields in any order. rotation is 0 and refresh preferred unless given.
        x and y give the screen's place in the arrangement (NAME@X,Y there) and
        count only when both are given. A malformed field takes its default and
        its bit is set in bad, so the compositor can name it; a line with no
        valid name is kept with an empty name and bad NAME. A word that is no
        field, a field given twice and a name given twice are bad too (the
        first one holds). windows_displays_write gives back the lines a read
        takes, so a write and a read agree.
*/
#define WINDOWS_DISPLAYS 32
#define WINDOWS_DISPLAY_NAME 1u
#define WINDOWS_DISPLAY_ROTATION 2u
#define WINDOWS_DISPLAY_REFRESH 4u
#define WINDOWS_DISPLAY_X 8u
#define WINDOWS_DISPLAY_Y 16u
#define WINDOWS_DISPLAY_OTHER 32u
#define WINDOWS_DISPLAY_SCALE 64u
#define WINDOWS_DISPLAY_HZ 1000u
// The scale a screen is drawn at, in percent: the six presets the settings offer, 100 the default.
#define WINDOWS_SCALE_DEFAULT 100u
static const unsigned int windows_scale_presets[] = {50, 75, 100, 125, 150, 200};

// Whether a percent is one of the scale presets.
static int windows_scale_ok(int percent)
{
        for (unsigned int at = 0; at < sizeof(windows_scale_presets) / sizeof(windows_scale_presets[0]); at++)
                if (percent == (int)windows_scale_presets[at])
                        return 1;
        return 0;
}

struct windows_display
{
        char name[WINDOWS_OUTPUT_NAME];
        unsigned int rotation; // degrees, 0 when not given
        unsigned int refresh;  // a rate in Hz, or 0 for preferred
        unsigned int scale;    // the scale in percent, WINDOWS_SCALE_DEFAULT when not given
        int x, y;              // the place, when placed
        unsigned int placed;
        unsigned int bad;      // WINDOWS_DISPLAY_* bits of what was malformed or missing
};

// Whether length bytes at word are a connector name, and copies them into name: letters, digits and dashes.
static int windows_display_name(const char *word, unsigned int length, char *name)
{
        if (!length || length >= WINDOWS_OUTPUT_NAME)
                return 0;
        for (unsigned int at = 0; at < length; at++)
                if (!((unsigned int)((word[at] | 32) - 'a') < 26u || (word[at] >= '0' && word[at] <= '9') ||
                      word[at] == '-'))
                        return 0;
        for (unsigned int at = 0; at < length; at++)
                name[at] = word[at];
        name[length] = 0;
        return 1;
}

// Whether name (NUL-ended) is the length bytes at word.
static int windows_name_is(const char *name, const char *word, unsigned int length)
{
        if (length >= WINDOWS_OUTPUT_NAME || name[length])
                return 0;
        for (unsigned int at = 0; at < length; at++)
                if (name[at] != word[at])
                        return 0;
        return 1;
}

// Whether two NUL-ended connector names are the same.
static int windows_display_same_name(const char *a, const char *b)
{
        unsigned int at = 0;

        while (at < WINDOWS_OUTPUT_NAME - 1 && a[at] && a[at] == b[at])
                at++;
        return a[at] == b[at];
}

// Whether length bytes at word are the decimal number value, as at most digits digits (a minus first when signed).
static int windows_display_number(const char *word, unsigned int length, int signed_number, unsigned int digits, int *into)
{
        unsigned int at = 0, count = 0;
        int negative = 0;
        long value = 0;

        if (signed_number && at < length && word[at] == '-')
                negative = 1, at++;
        for (; at < length; at++, count++)
        {
                if (word[at] < '0' || word[at] > '9' || count == digits)
                        return 0;
                value = value * 10 + (word[at] - '0');
        }
        if (!count)
                return 0;
        *into = (int)(negative ? -value : value);
        return 1;
}

// Reads one line of length bytes at line into display, and says in bad which of its fields were not right.
static void windows_display_line(const char *line, unsigned int length, struct windows_display *display)
{
        unsigned int at = 0, start, seen = 0, have_x = 0, have_y = 0;
        int x = 0, y = 0;

        for (unsigned int each = 0; each < WINDOWS_OUTPUT_NAME; each++)
                display->name[each] = 0;
        display->rotation = display->refresh = display->placed = display->bad = 0;
        display->scale = WINDOWS_SCALE_DEFAULT;
        display->x = display->y = 0;
        while (at < length && (line[at] == ' ' || line[at] == '\t'))
                at++;
        start = at;
        while (at < length && line[at] != ' ' && line[at] != '\t')
                at++;
        if (!windows_display_name(line + start, at - start, display->name))
        {
                display->bad = WINDOWS_DISPLAY_NAME;
                return;
        }
        for (;;)
        {
                unsigned int word, equals, key, keys;
                int value;

                while (at < length && (line[at] == ' ' || line[at] == '\t'))
                        at++;
                if (at == length)
                        break;
                word = at;
                while (at < length && line[at] != ' ' && line[at] != '\t')
                        at++;
                for (equals = word; equals < at && line[equals] != '='; equals++)
                        ;
                keys = equals - word;
                if (equals == at || equals == word)
                {
                        display->bad |= WINDOWS_DISPLAY_OTHER;
                        continue;
                }
                // Each field is the key's name and the value after its '=': the rest is the value.
                key = 0;
                if (keys == 8 && windows_name_is("rotation", line + word, 8))
                        key = WINDOWS_DISPLAY_ROTATION;
                else if (keys == 7 && windows_name_is("refresh", line + word, 7))
                        key = WINDOWS_DISPLAY_REFRESH;
                else if (keys == 5 && windows_name_is("scale", line + word, 5))
                        key = WINDOWS_DISPLAY_SCALE;
                else if (keys == 1 && line[word] == 'x')
                        key = WINDOWS_DISPLAY_X;
                else if (keys == 1 && line[word] == 'y')
                        key = WINDOWS_DISPLAY_Y;
                if (!key || (seen & key))
                {
                        display->bad |= WINDOWS_DISPLAY_OTHER;
                        continue;
                }
                seen |= key;
                if (key == WINDOWS_DISPLAY_ROTATION)
                {
                        if (windows_display_number(line + equals + 1, at - equals - 1, 0, 3, &value) &&
                            (value == 0 || value == 90 || value == 180 || value == 270))
                                display->rotation = (unsigned int)value;
                        else
                                display->bad |= key;
                }
                else if (key == WINDOWS_DISPLAY_SCALE)
                {
                        if (windows_display_number(line + equals + 1, at - equals - 1, 0, 3, &value) && windows_scale_ok(value))
                                display->scale = (unsigned int)value;
                        else
                                display->bad |= key;
                }
                else if (key == WINDOWS_DISPLAY_REFRESH)
                {
                        if (at - equals - 1 == 9 && windows_name_is("preferred", line + equals + 1, 9))
                                display->refresh = 0;
                        else if (windows_display_number(line + equals + 1, at - equals - 1, 0, 4, &value) &&
                                 value >= 1 && value <= (int)WINDOWS_DISPLAY_HZ)
                                display->refresh = (unsigned int)value;
                        else
                                display->bad |= key;
                }
                else if (windows_display_number(line + equals + 1, at - equals - 1, 1, 6, &value) &&
                         value <= WINDOWS_PLACE_MOST && value >= -WINDOWS_PLACE_MOST)
                {
                        if (key == WINDOWS_DISPLAY_X)
                                x = value, have_x = 1;
                        else
                                y = value, have_y = 1;
                }
                else
                        display->bad |= key;
        }
        // x and y go together: one alone is malformed for the field it lacks.
        if (have_x && have_y)
                display->placed = 1, display->x = x, display->y = y;
        else if (have_x)
                display->bad |= WINDOWS_DISPLAY_Y;
        else if (have_y)
                display->bad |= WINDOWS_DISPLAY_X;
}

// Reads the lines of text into list, at most room of them, and answers how many were read; a blank line is none.
static unsigned int windows_displays_read(const char *text, struct windows_display *list, unsigned int room)
{
        unsigned int count = 0, at = 0;

        while (text[at] && count < room)
        {
                unsigned int start = at, blank = 1;

                while (text[at] && text[at] != '\n')
                        at++;
                for (unsigned int each = start; each < at; each++)
                        blank &= text[each] == ' ' || text[each] == '\t' || text[each] == '\r';
                if (!blank)
                {
                        windows_display_line(text + start, at - start, &list[count]);
                        for (unsigned int earlier = 0; earlier < count && list[count].name[0]; earlier++)
                                if (list[earlier].name[0] && windows_display_same_name(list[earlier].name, list[count].name))
                                        list[count].bad |= WINDOWS_DISPLAY_OTHER;
                        count++;
                }
                if (text[at] == '\n')
                        at++;
        }
        return count;
}

// The first display of the list named as the length bytes at word are: the one a line gives its place or setting.
static const struct windows_display *windows_display_of(const struct windows_display *list, unsigned int count,
                                                        const char *word, unsigned int length)
{
        for (unsigned int at = 0; at < count; at++)
                if (list[at].name[0] && windows_name_is(list[at].name, word, length))
                        return &list[at];
        return 0;
}

/*
        The mode a screen is driven at for a rate the displays file asks for: the first of its modes (count of them, each
        its width, height and rate in modes[i][0..2], in the order the screen lists them) that is width by height at hz;
        no hz (0) is the preferred rate, the first it lists at that size, which is the rate the settings recommend.
        Answers its index, or count when the screen lists none, and the screen keeps the mode it has.
*/
static unsigned int windows_mode_at(const unsigned int (*modes)[3], unsigned int count, unsigned int width,
                                    unsigned int height, unsigned int hz)
{
        for (unsigned int at = 0; at < count; at++)
                if (modes[at][0] == width && modes[at][1] == height && (!hz || modes[at][2] == hz))
                        return at;
        return count;
}

/*
        The value a plane's rotation property is set to for degrees, when the card lists that rotation: its enum
        "rotate-DEGREES", whose value is a bit (names[i] with values[i], the enums of the property), so the value is the
        one-bit mask. 0 when the card lists none, and the screen is then rotated in software.
*/
static unsigned long windows_rotation_bit(const char *const *names, const unsigned long *values, unsigned int count,
                                          unsigned int degrees)
{
        static const char *const wanted[] = {"rotate-0", "rotate-90", "rotate-180", "rotate-270"};

        if (degrees % 90 || degrees > 270)
                return 0;
        for (unsigned int at = 0; at < count; at++)
                if (windows_display_same_name(names[at], wanted[degrees / 90]) && values[at] < 32)
                        return 1ul << values[at];
        return 0;
}

/*
        A picture turned for the panel it is shown on, in software. The picture is lw wide and lh tall, and for 90 and 270
        the panel is lh wide and lw tall (for 0 and 180 the same size). The panel's pixel for a picture pixel lx, ly is
        where the turn sends it: 90 clockwise puts the picture's top left at the panel's top right, 270 anti-clockwise
        puts it at the bottom left, and 180 turns it over both ways. windows_rotate_u32 copies the picture's rectangle
        x0, y0 to x1, y1 from from (rows from_pitch words apart) into into (rows into_pitch words apart).
*/
static void windows_rotate_at(unsigned int lw, unsigned int lh, unsigned int degrees, unsigned int lx, unsigned int ly,
                              unsigned int *px, unsigned int *py)
{
        if (degrees == 90)
                *px = lh - 1 - ly, *py = lx;
        else if (degrees == 180)
                *px = lw - 1 - lx, *py = lh - 1 - ly;
        else if (degrees == 270)
                *px = ly, *py = lw - 1 - lx;
        else
                *px = lx, *py = ly;
}

/*
        The panel is the card's memory, mapped to be written: a store to it that does not fill a whole line goes over the bus
        as a partial write, and a quarter turn stored a pixel at a time steps a panel row between stores, so every store of
        it is one. A quarter turn is therefore done in tiles WINDOWS_TURN_TILE pixels square, 64 bytes a row, each panel
        row of a tile stored whole and in order from the tile's 16 picture rows, which the tile's rows read again and
        again from the cache; a half turn and no turn store each panel row in order as they read.
*/
#define WINDOWS_TURN_TILE 16u

static void windows_rotate_u32(unsigned int *into, unsigned long into_pitch, const unsigned int *from, unsigned long from_pitch,
                               unsigned int lw, unsigned int lh, unsigned int degrees, unsigned int x0, unsigned int y0,
                               unsigned int x1, unsigned int y1)
{
        if (degrees == 90 || degrees == 270)
        {
                // The panel columns and rows the rectangle turns into.
                unsigned int pa = degrees == 90 ? lh - y1 : y0, pb = degrees == 90 ? lh - y0 : y1;
                unsigned int qa = degrees == 90 ? x0 : lw - x1, qb = degrees == 90 ? x1 : lw - x0;

                for (unsigned int p = pa, pe; p < pb; p = pe)
                {
                        // Tiles start at multiples of the tile, so a tile's panel row is a line of the card's memory.
                        pe = (p / WINDOWS_TURN_TILE + 1) * WINDOWS_TURN_TILE;
                        pe = pe < pb ? pe : pb;
                        for (unsigned int q = qa; q < qb; q += WINDOWS_TURN_TILE)
                        {
                                unsigned int qe = q + WINDOWS_TURN_TILE < qb ? q + WINDOWS_TURN_TILE : qb;

                                // A full-width tile (sixteen columns, at least four rows) is one call. The panel row r of the tile is the
                                // picture's column q + r, and panel column c is the picture's row lh - 1 - p - c for a quarter turn one way,
                                // or p + c for the other: memory_transpose_u32 with the picture's column step, and the row step that turns
                                // the tile's rows the same way. Any other edge is the loop below: measured, the call wins only at sixteen
                                // columns on both machines, and the edges of a picture stay with the loop.
                                if (qe - q >= 4 && pe - p == WINDOWS_TURN_TILE)
                                {
                                        unsigned int *to = into + (unsigned long)q * into_pitch + p;

                                        if (degrees == 90)
                                                memory_transpose_u32(to, into_pitch, from + (unsigned long)(lh - 1 - p) * from_pitch + q,
                                                                     -(long)from_pitch, 1, qe - q, pe - p);
                                        else
                                                memory_transpose_u32(to, into_pitch, from + (unsigned long)p * from_pitch + (lw - 1 - q),
                                                                     (long)from_pitch, -1, qe - q, pe - p);
                                        continue;
                                }

                                for (unsigned int row = q; row < qe; row++)
                                {
                                        unsigned int *to = into + (unsigned long)row * into_pitch;

                                        if (degrees == 90)
                                        {
                                                // Panel column px of this row is the picture's column row, from the row lh - 1 - px.
                                                const unsigned int *source = from + (unsigned long)(lh - 1 - p) * from_pitch + row;

                                                for (unsigned int px = p; px < pe; px++, source -= from_pitch)
                                                        to[px] = *source;
                                        }
                                        else
                                        {
                                                // Panel column px of this row is the picture's row px, from the column lw - 1 - row.
                                                const unsigned int *source = from + (unsigned long)p * from_pitch + (lw - 1 - row);

                                                for (unsigned int px = p; px < pe; px++, source += from_pitch)
                                                        to[px] = *source;
                                        }
                                }
                        }
                }
                return;
        }
        for (unsigned int ly = y0; ly < y1; ly++)
        {
                if (degrees == 180)
                {
                        unsigned int *to = into + (unsigned long)(lh - 1 - ly) * into_pitch + (lw - x1);
                        const unsigned int *row = from + (unsigned long)ly * from_pitch + (x1 - 1);

                        for (unsigned int each = x1 - x0; each; each--)
                                *to++ = *row--;
                }
                else
                {
                        unsigned int *to = into + (unsigned long)ly * into_pitch + x0;
                        const unsigned int *row = from + (unsigned long)ly * from_pitch + x0;

                        for (unsigned int each = x1 - x0; each; each--)
                                *to++ = *row++;
                }
        }
}

// The panel rectangle (left, top, right, bottom) the picture rectangle x0, y0 to x1, y1 turns into; the corners are
// where a turn puts the extremes, since each axis of the turn is one of the picture's.
static void windows_rotate_panel(unsigned int lw, unsigned int lh, unsigned int degrees, unsigned int x0, unsigned int y0,
                                 unsigned int x1, unsigned int y1, unsigned int *panel)
{
        unsigned int corner[4][2] = {{x0, y0}, {x1 - 1, y0}, {x0, y1 - 1}, {x1 - 1, y1 - 1}};

        panel[0] = panel[1] = ~0u, panel[2] = panel[3] = 0;
        for (unsigned int each = 0; each < 4; each++)
        {
                unsigned int px, py;

                windows_rotate_at(lw, lh, degrees, corner[each][0], corner[each][1], &px, &py);
                panel[0] = px < panel[0] ? px : panel[0];
                panel[1] = py < panel[1] ? py : panel[1];
                panel[2] = px + 1 > panel[2] ? px + 1 : panel[2];
                panel[3] = py + 1 > panel[3] ? py + 1 : panel[3];
        }
}

// Appends text (length bytes) to into at *at, bounded by room with its NUL; 0 when it does not fit.
static int windows_put(char *into, unsigned int room, unsigned int *at, const char *text, unsigned int length)
{
        if (room < *at || room - *at <= length)
                return 0;
        for (unsigned int each = 0; each < length; each++)
                into[*at + each] = text[each];
        *at += length;
        into[*at] = 0;
        return 1;
}

// Appends value in decimal (a minus first when negative) to into at *at.
static int windows_put_number(char *into, unsigned int room, unsigned int *at, int value)
{
        char digits[12];
        unsigned int count = 0;
        unsigned long magnitude = value < 0 ? 0ul - (unsigned long)(long)value : (unsigned long)value;

        do
                digits[count++] = (char)('0' + magnitude % 10), magnitude /= 10;
        while (magnitude);
        if (value < 0 && !windows_put(into, room, at, "-", 1))
                return 0;
        while (count)
                if (!windows_put(into, room, at, digits + --count, 1))
                        return 0;
        return 1;
}

// The displays a screen has a setting for, one line each, as windows_displays_read reads them; 0 when they do not fit.
static unsigned int windows_displays_write(char *into, unsigned int room, const struct windows_display *list, unsigned int count)
{
        unsigned int at = 0;

        if (!room)
                return 0;
        into[0] = 0;
        for (unsigned int each = 0; each < count; each++)
        {
                const struct windows_display *display = &list[each];
                unsigned int length = 0;

                if (!display->name[0] || (!display->rotation && !display->refresh && !display->placed &&
                                          display->scale == WINDOWS_SCALE_DEFAULT))
                        continue;
                while (display->name[length])
                        length++;
                if (!windows_put(into, room, &at, display->name, length) ||
                    !windows_put(into, room, &at, " rotation=", 10) ||
                    !windows_put_number(into, room, &at, (int)display->rotation) ||
                    !windows_put(into, room, &at, " refresh=", 9))
                        return 0;
                if (!display->refresh ? !windows_put(into, room, &at, "preferred", 9) :
                    !windows_put_number(into, room, &at, (int)display->refresh))
                        return 0;
                if (display->scale != WINDOWS_SCALE_DEFAULT &&
                    (!windows_put(into, room, &at, " scale=", 7) || !windows_put_number(into, room, &at, (int)display->scale)))
                        return 0;
                if (display->placed && (!windows_put(into, room, &at, " x=", 3) ||
                                        !windows_put_number(into, room, &at, display->x) ||
                                        !windows_put(into, room, &at, " y=", 3) ||
                                        !windows_put_number(into, room, &at, display->y)))
                        return 0;
                if (!windows_put(into, room, &at, "\n", 1))
                        return 0;
        }
        return at;
}

/*
        A word a `canvas display` gives one field of a screen, judged as windows_displays_read judges that field of the
        file: rotation is 0, 90, 180 or 270; refresh is preferred (into is 0) or a whole number of Hz from 1 to
        WINDOWS_DISPLAY_HZ; a place's X or Y is a whole number within WINDOWS_PLACE_MOST. Answers whether it is one.
*/
static int windows_display_value(unsigned int field, const char *word, unsigned int length, int *into)
{
        int value = 0;

        if (field == WINDOWS_DISPLAY_REFRESH && length == 9 && windows_name_is("preferred", word, 9))
        {
                *into = 0;
                return 1;
        }
        if (field == WINDOWS_DISPLAY_ROTATION)
        {
                if (!windows_display_number(word, length, 0, 3, &value) ||
                    (value != 0 && value != 90 && value != 180 && value != 270))
                        return 0;
        }
        else if (field == WINDOWS_DISPLAY_SCALE)
        {
                if (!windows_display_number(word, length, 0, 3, &value) || !windows_scale_ok(value))
                        return 0;
        }
        else if (field == WINDOWS_DISPLAY_REFRESH)
        {
                if (!windows_display_number(word, length, 0, 4, &value) || value < 1 || value > (int)WINDOWS_DISPLAY_HZ)
                        return 0;
        }
        else if (!windows_display_number(word, length, 1, 6, &value) || value > WINDOWS_PLACE_MOST ||
                 value < -WINDOWS_PLACE_MOST)
                return 0;
        *into = value;
        return 1;
}

/*
        The displays file (text, NUL-ended) with one screen's setting changed. field is the bit it changes:
        WINDOWS_DISPLAY_ROTATION, WINDOWS_DISPLAY_REFRESH, or X and Y together for the place, and change holds the new
        value in its rotation, refresh, or x and y. The screen keeps its other fields: its line is the one
        windows_displays_read takes for it, changed and written by windows_displays_write, so a screen the file did not
        have gets a line of that field alone. Every other line is copied as it is, with a newline where the file had
        none, and a later line of the same screen is dropped, as the reader takes only the first. A screen left with
        nothing set has no line. Answers the bytes written into (NUL-ended), or -1 when name is not a connector's, the
        file names more screens than the reader holds without this one, or the bytes do not fit.
*/
static int windows_displays_set(char *into, unsigned int room, const char *text, const char *name, unsigned int field,
                                const struct windows_display *change)
{
        struct windows_display list[WINDOWS_DISPLAYS], screen;
        const struct windows_display *found;
        unsigned int name_length = 0, count, made, at = 0, start = 0, done = 0;
        char line[128];

        if (!room)
                return -1;
        into[0] = 0;
        screen = (struct windows_display){0};
        screen.scale = WINDOWS_SCALE_DEFAULT;
        while (name_length < WINDOWS_OUTPUT_NAME && name[name_length])
                name_length++;
        if (!windows_display_name(name, name_length, screen.name))
                return -1;
        count = windows_displays_read(text, list, WINDOWS_DISPLAYS);
        found = windows_display_of(list, count, name, name_length);
        if (!found && count == WINDOWS_DISPLAYS)
                return -1;
        if (found)
                screen = *found;
        screen.bad = 0;
        if (field & WINDOWS_DISPLAY_ROTATION)
                screen.rotation = change->rotation;
        if (field & WINDOWS_DISPLAY_REFRESH)
                screen.refresh = change->refresh;
        if (field & WINDOWS_DISPLAY_SCALE)
                screen.scale = change->scale;
        if (field & (WINDOWS_DISPLAY_X | WINDOWS_DISPLAY_Y))
                screen.placed = 1, screen.x = change->x, screen.y = change->y;
        made = windows_displays_write(line, sizeof(line), &screen, 1);

        while (text[start])
        {
                unsigned int stop = start, word, word_end;

                while (text[stop] && text[stop] != '\n')
                        stop++;
                word = start;
                while (word < stop && (text[word] == ' ' || text[word] == '\t'))
                        word++;
                word_end = word;
                while (word_end < stop && text[word_end] != ' ' && text[word_end] != '\t')
                        word_end++;
                if (word_end - word == name_length && windows_name_is(name, text + word, name_length))
                {
                        if (!done && made && !windows_put(into, room, &at, line, made))
                                return -1;
                        done = 1;
                }
                else if (!windows_put(into, room, &at, text + start, stop - start) ||
                         !windows_put(into, room, &at, "\n", 1))
                        return -1;
                start = stop + (text[stop] == '\n');
        }
        if (!done && made && !windows_put(into, room, &at, line, made))
                return -1;
        return (int)at;
}

/*
        The arrangement the compositor lays, with each display's place put in
        it. The arrangement's words stay as they are, except that a placed
        display's word (NAME@X,Y) takes the display's place where the
        arrangement names it (a screen the arrangement holds dark stays dark).
        Then the card screens the arrangement does not name follow in the
        card's order, and last the displays it does not name but places, each
        as NAME@X,Y: so a placed display that is not in the arrangement does not
        become the primary. An arrangement that is not one is taken as none.
        Answers the bytes written with the NUL, or 0 when they do not fit or
        are not an arrangement after all; the compositor then lays the
        arrangement it has.
*/
// The next word of text from *start, whose bounds come back in start and end; 0 when there is none.
static int windows_word_next(const char *text, unsigned int *start, unsigned int *stop)
{
        unsigned int at = *start;

        while (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')
                at++;
        if (!text[at])
                return 0;
        *start = at;
        while (text[at] && text[at] != ' ' && text[at] != '\t' && text[at] != '\n' && text[at] != '\r')
                at++;
        *stop = at;
        return 1;
}

// The length of a NUL-ended connector name.
static unsigned int windows_name_length(const char *name)
{
        unsigned int at = 0;

        while (at < WINDOWS_OUTPUT_NAME - 1 && name[at])
                at++;
        return at;
}

// Whether the card screen names are a screen the length bytes at word name: which one, plus one, or 0.
static unsigned int windows_screen_of(const char (*names)[WINDOWS_OUTPUT_NAME], unsigned int count, const char *word,
                                      unsigned int length)
{
        for (unsigned int screen = 0; screen < count; screen++)
                if (windows_name_is(names[screen], word, length))
                        return screen + 1;
        return 0;
}

static unsigned int windows_arrange_pinned(const char *arrangement, const char (*names)[WINDOWS_OUTPUT_NAME],
                                           unsigned int count, const struct windows_display *list, unsigned int listed,
                                           char *into, unsigned int room)
{
        unsigned int at = 0, named[WINDOWS_OUTPUTS] = {0}, used[WINDOWS_DISPLAYS] = {0};
        const char *base = windows_arrange_valid(arrangement) ? arrangement : "";
        unsigned int start, stop, name, screen;
        int how, x, y;

        if (!room || count > WINDOWS_OUTPUTS || listed > WINDOWS_DISPLAYS)
                return 0;
        into[0] = 0;
        // What the arrangement names: its screens, and the first display of each of its names.
        for (start = 0; windows_word_next(base, &start, &stop); start = stop)
        {
                const struct windows_display *display;

                if (stop - start == 7 && windows_name_is("default", base + start, 7))
                        continue;
                windows_arrange_word(base + start, stop - start, &name, &how, &x, &y);
                screen = windows_screen_of(names, count, base + start, name);
                if (screen)
                        named[screen - 1] = 1;
                display = windows_display_of(list, listed, base + start, name);
                if (display)
                        used[display - list] = 1;
        }
        // The displays the arrangement does not name but places: the screens they are name the screens they are on.
        for (unsigned int each = 0; each < listed; each++)
        {
                const struct windows_display *display = &list[each];
                unsigned int length = windows_name_length(display->name);
                unsigned int screen;

                if (!display->placed || used[each] || !length || windows_display_of(list, listed, display->name, length) != display)
                        continue;
                screen = windows_screen_of(names, count, display->name, length);
                if (screen)
                        named[screen - 1] = 1;
        }
        // The arrangement's words, a placed display's own word in place of its word where it has one.
        for (start = 0; windows_word_next(base, &start, &stop); start = stop)
        {
                const struct windows_display *display;

                if (stop - start == 7 && windows_name_is("default", base + start, 7))
                        continue;
                windows_arrange_word(base + start, stop - start, &name, &how, &x, &y);
                display = how == WINDOWS_PLACE_OFF ? 0 : windows_display_of(list, listed, base + start, name);
                if (display && display->placed)
                {
                        if (!windows_put(into, room, &at, display->name, name) || !windows_put(into, room, &at, "@", 1) ||
                            !windows_put_number(into, room, &at, display->x) || !windows_put(into, room, &at, ",", 1) ||
                            !windows_put_number(into, room, &at, display->y) || !windows_put(into, room, &at, " ", 1))
                                return 0;
                }
                else if (!windows_put(into, room, &at, base + start, stop - start) || !windows_put(into, room, &at, " ", 1))
                        return 0;
        }
        // The card screens the arrangement does not name, in the card's order.
        for (unsigned int screen = 0; screen < count; screen++)
        {
                unsigned int length = windows_name_length(names[screen]);

                if (named[screen])
                        continue;
                if (!windows_put(into, room, &at, names[screen], length) || !windows_put(into, room, &at, " ", 1))
                        return 0;
                named[screen] = 1;
        }
        // The displays it does not name but places, each on a screen that is there.
        for (unsigned int each = 0; each < listed; each++)
        {
                const struct windows_display *display = &list[each];
                unsigned int length = windows_name_length(display->name);

                if (!display->placed || used[each] || !length || windows_display_of(list, listed, display->name, length) != display ||
                    !windows_screen_of(names, count, display->name, length))
                        continue;
                if (!windows_put(into, room, &at, display->name, length) || !windows_put(into, room, &at, "@", 1) ||
                    !windows_put_number(into, room, &at, display->x) || !windows_put(into, room, &at, ",", 1) ||
                    !windows_put_number(into, room, &at, display->y) || !windows_put(into, room, &at, " ", 1))
                        return 0;
        }
        // The space each word left at its end is dropped; an empty arrangement (no screens) is the card's order.
        while (at && into[at - 1] == ' ')
                into[--at] = 0;
        return windows_arrange_valid(into) ? at + 1 : 0;
}

// Row row's buffer which, for a caller holding the table: the buffer's size is the header's, which the compositor
// wrote and any mapper may write too, so the program's own copy (windows_pixels) is the one to draw through.
#define windows_buffer(table, row, which)                                                              \
        ((unsigned int *)((char *)(table) + WINDOWS_HEADER +                                           \
                          ((unsigned long)(row) * 2 + ((which) & 1u)) * (table)->buffer))

// What a program has of its window: the table, its row, and its two ends. The rest is the program's copy of the
// header, taken once by windows_take and checked there: the table is shared, so a peer may write its header.
struct window_row
{
        struct windows *table;
        unsigned int row;
        long door, wake;
        unsigned long bytes;
        unsigned long buffer;
        unsigned int stride, output_width, output_height;
};

// Row row's buffer which, through the program's copy of the header: the one it draws into and gives back.
static inline unsigned int *windows_pixels(const struct window_row *own, unsigned int which)
{
        return (unsigned int *)((char *)own->table + WINDOWS_HEADER +
                                ((unsigned long)own->row * 2 + (which & 1u)) * own->buffer);
}

// One event, read out of a row's columns.
struct window_event
{
        unsigned int type, flags;
        int a, b;
        // When the device made it (its evdev stamp, CLOCK_MONOTONIC nanoseconds), or 0 when none was given: a drag, its velocity and
        // its late lift read this, not the time the program dispatched the event (ui_press_now), so a batch of reports is its own speed.
        unsigned long long when;
};

_Static_assert(sizeof(struct window) == 1288, "window page ABI");
_Static_assert(sizeof(struct window_key) == 16, "window key ABI");
_Static_assert(sizeof(struct window_cell) == 8, "window cell ABI");

/* Every field of the page, where the compositor and its programs both
   expect it. */
#define WINDOW_AT(field, offset)                                               \
        _Static_assert(offset_of(struct window, field) == (offset), "window page ABI " #field)
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
_Static_assert(offset_of(struct window_key, character) == 4, "window key ABI");
_Static_assert(offset_of(struct window_cell, ink) == 4, "window cell ABI");
_Static_assert(offset_of(struct window_cell, paper) == 5, "window cell ABI");
_Static_assert(offset_of(struct window_cell, flags) == 6, "window cell ABI");

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
#define WINDOW_SYS_MEMFD 319
#define WINDOW_SYS_FCNTL 72
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
#define WINDOW_SYS_MEMFD 279
#define WINDOW_SYS_FCNTL 25
#endif

/*
        A shell image already carries lib.c's hardware-floor syscall
        entry on every supported architecture. Use it directly instead of
        emitting a second syscall body in every program that includes this
        client. Without the library's include guard the small per-architecture
        fallback below is used instead; the types and memory routines come from
        the library either way.

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
        The touch trace, a file switch: while /run/moonwater/touch-trace exists, the compositor and each program that takes touch append
        lines to /run/moonwater/touch-trace.log, up to WINDOW_TRACE_CAP bytes (a line past it is not written). The switch is looked for
        at a touch-down (window_trace_touch: one open that fails when the file is not there), and a log is made fresh when the switch
        goes on. Off, a trace line costs one branch (WINDOW_TRACE); the log is not truncated when the switch goes, so it can be sent.
        window_trace_out is the sink a line goes to (window_trace_write, the file; a check puts its own there).
*/
#define WINDOW_TRACE_SWITCH "/run/moonwater/touch-trace"
#define WINDOW_TRACE_LOG "/run/moonwater/touch-trace.log"
#define WINDOW_TRACE_CAP 1048576l
#define WINDOW_TRACE_TEXT 320
#define WINDOW_TRACE_APPEND (0x1 | 0x400 | 0x40 | 0x80000)  // O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC
#define WINDOW_TRACE_NEW (0x1 | 0x200 | 0x40 | 0x80000)     // O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC
#define WINDOW_TRACE_READ 0x80000                          // O_RDONLY | O_CLOEXEC

static b32 window_trace_live;                    // this program's trace is on: the switch was there at the last touch-down
static char window_trace_text[WINDOW_TRACE_TEXT];
static positive window_trace_length;

// The log holds room for a line of length bytes when it is at size now: the size is the log's own end, so the lines of every program count.
static b32 window_trace_room(long size, long length)
{
        return size >= 0 && length > 0 && size + length <= WINDOW_TRACE_CAP;
}

// Appends a line (length bytes) to the log if it fits under the cap. Answers whether it was written.
static b32 window_trace_write(const char address_to line, positive length)
{
        long handle = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_TRACE_LOG, WINDOW_TRACE_APPEND, 0644, 0, 0);
        long size;
        b32 wrote = false;

        if (handle < 0)
                return false;
        size = window_call(WINDOW_SYS_LSEEK, handle, 0, 2, 0, 0, 0); // SEEK_END
        if (window_trace_room(size, (long)length))
                wrote = window_call(WINDOW_SYS_WRITE, handle, (long)line, (long)length, 0, 0, 0) == (long)length;
        window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
        return wrote;
}

static b32 (*window_trace_out)(const char address_to line, positive length) = window_trace_write;

// The switch at a touch-down: on when the file is there, and the log made fresh when it was not on before.
static void window_trace_touch(void)
{
        long handle = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_TRACE_SWITCH, WINDOW_TRACE_READ, 0, 0, 0);
        b32 was = window_trace_live;

        window_trace_live = handle >= 0;
        if (handle >= 0)
                window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
        if (window_trace_live && !was)
        {
                long fresh = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_TRACE_LOG, WINDOW_TRACE_NEW, 0644, 0, 0);

                if (fresh >= 0)
                        window_call(WINDOW_SYS_CLOSE, fresh, 0, 0, 0, 0, 0);
        }
}

// The text of a line is formatted through string_format into window_trace_text, and sent with a newline by window_trace_flush.
static void window_trace_take(address_any data, positive length)
{
        const char address_to bytes = (const char address_to)data;

        for (positive at = 0; at < length && window_trace_length < WINDOW_TRACE_TEXT - 1; at++)
                window_trace_text[window_trace_length++] = bytes[at];
}

static void window_trace_flush(void)
{
        // A line ends in one newline: the format's own, or the flush's where the format gave none.
        if (window_trace_length < WINDOW_TRACE_TEXT - 1 && (window_trace_length == 0 || window_trace_text[window_trace_length - 1] != '\n'))
                window_trace_text[window_trace_length++] = '\n';
        if (window_trace_out)
                window_trace_out(window_trace_text, window_trace_length);
        window_trace_length = 0;
}

// A word of text (a reason), appended as it is. string_format's %s does not come out through this writer, so a line with a word in it
// is made of parts: WINDOW_TRACE_START, WINDOW_TRACE_PART (the formatter's numbers), window_trace_word, WINDOW_TRACE_END.
static void window_trace_word(const char address_to word)
{
        if (!window_trace_live || !word)
                return;
        for (positive at = 0; word[at] && window_trace_length < WINDOW_TRACE_TEXT - 1; at++)
                window_trace_text[window_trace_length++] = word[at];
}

// A trace line when the trace is on: one branch when it is not. The format and its arguments are string_format's (numbers only).
#define WINDOW_TRACE_START()                                 \
        do                                                   \
        {                                                    \
                if (window_trace_live)                       \
                        window_trace_length = 0;             \
        } while (0)
#define WINDOW_TRACE_PART(...)                               \
        do                                                   \
        {                                                    \
                if (window_trace_live)                       \
                        string_format(window_trace_take, __VA_ARGS__); \
        } while (0)
#define WINDOW_TRACE_END()                                   \
        do                                                   \
        {                                                    \
                if (window_trace_live)                       \
                        window_trace_flush();                \
        } while (0)
#define WINDOW_TRACE(...)                                    \
        do                                                   \
        {                                                    \
                if (window_trace_live)                       \
                {                                            \
                        window_trace_length = 0;             \
                        string_format(window_trace_take, __VA_ARGS__); \
                        window_trace_flush();                \
                }                                            \
        } while (0)

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

/*
        Bumping sequence is what says a window changed, and the program that
        holds the page itself has no compositor to tell: the bump is the whole
        of it, and the release keeps it from being seen before the cells,
        damage rows or pixels it announces.
*/

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

        /*
                Line n is slot n modulo history, and head counts lines in 32 bits, so it wraps after four billion
                of them. A history that does not divide 2^32 puts the lines either side of the wrap in slots that
                are not next to each other: a screen that spans the wrap showed lines written long ago in place of
                the newest, and the scrollback was overwritten early. A power of two divides 2^32, so the ring is
                that size or the next one up.
        */
        if (history > 0x80000000u)
                return 0;
        // The ring's own name for the size: `power` outside the loop was lib.util.c's function of that name, and its address cut to 32 bits
        // was the history, so no terminal or kernel log ever got its cells.
        unsigned long ring = 1;

        while (ring < history)
                ring <<= 1;
        history = (unsigned int)ring;

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

/*
        The wake read until it has no more: a read that comes back short had the pipe empty as it read, and a knock
        after that is still in the pipe for the next wait (a ppoll that is readable), so the read that would answer
        EAGAIN is not made. A knock is one byte, so a wakeup is one read.
*/
static void windows_drain(long handle)
{
        char bytes[64];
        long got;

        do
                got = window_call(WINDOW_SYS_READ, handle, (long)bytes, sizeof(bytes), 0, 0, 0);
        while (got == (long)sizeof(bytes));
}

/*
        Whether the table's lists (if it has any) are where the layout puts them and whole inside the mapping: the lists follow the
        pictures, each list is WINDOW_LIST_BYTES, and WINDOWS of them fit in size. A table with no lists is fine as it is.
*/
static int windows_lists_fit(const struct windows *table, unsigned long size, unsigned long buffer)
{
        unsigned long lists = table->lists;

        if (!lists)
                return 1;
        if (table->list_bytes != WINDOW_LIST_BYTES || lists != WINDOWS_HEADER + buffer * 2 * WINDOWS || lists > size)
                return 0;
        return (size - lists) / WINDOW_LIST_BYTES >= WINDOWS;
}

/*
        Whether a header describes a table of size bytes that can be drawn through: the pixels where the layout puts
        them, every buffer a whole number of pages and all 2 * WINDOWS of them inside the mapping, and each row of a
        buffer stride pixels long with stride at least the output's width, the rows used fitting in the buffer. The
        compositor writes exactly that; a table that does not is refused, not drawn through.
*/
static int windows_fits(unsigned long size, unsigned long pixels, unsigned long buffer, unsigned int stride,
                        unsigned int width, unsigned int height)
{
        if (pixels != WINDOWS_HEADER || size < WINDOWS_HEADER || !buffer || buffer % 4096)
                return 0;
        if (buffer > (size - WINDOWS_HEADER) / (2 * WINDOWS))
                return 0;
        if (!width || !height || stride < width)
                return 0;
        return buffer / 4 / stride >= height;
}

/*
        A row's program fields put back as a row never taken has them. It runs after the row is claimed and before
        its pid is stored as the program's, so a peer that finds the pid finds a row with no picture, no hover and no
        damage. pinned is not among them: it is the compositor's, and is 1 while the card may still read the row's
        buffers after the row has left (compositor_window_fb_drop runs once the card is off them), so clearing it
        here would let windows_discard give back pages the card reads.
*/
static void windows_reset_row(struct windows *table, unsigned int row)
{
        table->state[row] = 0;
        table->x[row] = table->y[row] = 0;
        table->width[row] = table->height[row] = 0;
        table->want_width[row] = table->want_height[row] = 0;
        table->buffer_index[row] = 0;
        table->damage_x[row] = table->damage_y[row] = table->damage_width[row] = table->damage_height[row] = 0;
        // The whole title: compositor_take reads all of it, so what the row held past a shorter title must go too.
        for (unsigned int at = 0; at < WINDOWS_TITLE; at++)
                table->title[row][at] = 0;
        table->glass[row] = 0;
        // The scale the window is drawn at and its text fields with the keys are the compositor's and the program's for the window
        // that had the row: the program reads the scale at its start, and the keyboard shows for any field count above 0.
        __atomic_store_n(&table->window_scale[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->field[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->pointer_shape[row], WINDOW_POINTER_ARROW, __ATOMIC_RELEASE);
        table->asked[row] = 0;
        table->ground[row] = 0;
        __atomic_store_n(&table->lift[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->page[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->hover_off[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->pinch_wanted[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->spoiled_height[row][0], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&table->spoiled_height[row][1], 0, __ATOMIC_RELEASE);
        table->event_tail[row] = __atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE);
        // The key requests the last keyboard left unread are not the next one's to send on: its ring starts empty, as the events' does.
        __atomic_store_n(&table->osk_tail[row], __atomic_load_n(&table->osk_head[row], __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        table->body_state[row] = 0;
        table->body_raw[row] = table->body_drawn[row] = table->body_speed[row] = 0;
        // A placement and a name are the program that asked's, and not the next one's to open in the row.
        table->place[row] = table->place_screen[row] = 0;
        table->place_x[row] = table->place_y[row] = 0;
        table->place_width[row] = table->place_height[row] = 0;
        for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                table->name[row][at] = 0;
        __atomic_store_n(&table->moved[row], 0u, __ATOMIC_RELEASE);
        // A popup's parent and anchor were the menu's that last had the row: kept, the next window there was put at
        // the menu's anchor as a popup of its window, with no tab and never the keys. A watch of the clipboard too
        // is its program's; the serials only count up, and the compositor reads them against what it last saw.
        table->popup_parent[row] = table->popup_flags[row] = 0;
        table->popup_x[row] = table->popup_y[row] = table->popup_w[row] = table->popup_h[row] = 0;
        __atomic_store_n(&table->clip_watch[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->shown[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->frame[row], 0, __ATOMIC_RELEASE);
        // A row's list is its new program's from the first frame: the generation the compositor took last is the old program's.
        __atomic_store_n(&table->list_state[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->list_seen[row], 0u, __ATOMIC_RELEASE);
}

/*
        A name a window may be remembered by: one to WINDOWS_NAME - 1 lower-case letters, digits and hyphens, a
        letter or a digit first. It is written into a file of /root and read back, so nothing else is one.
*/
static int window_name_valid(const char *name)
{
        unsigned int at = 0;

        if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
                return 0;
        for (; name[at] && at < WINDOWS_NAME; at++)
                if (!((name[at] >= 'a' && name[at] <= 'z') || (name[at] >= '0' && name[at] <= '9') || name[at] == '-'))
                        return 0;
        return at < WINDOWS_NAME;
}

/*
        A number of a placement, read from text up to stop (0: the end of the word): digits, a minus before them
        only where low is below zero, no zero in front of another digit and no minus zero, from low to high.
        Where the number ends, or null for anything else.
*/
static const char *window_place_number(const char *text, char stop, int low, int high, int *value)
{
        int negative = text[0] == '-' && low < 0, digits = 0;
        long number = 0;

        text += negative;
        if (text[0] == '0' && text[1] >= '0' && text[1] <= '9')
                return 0;
        // Six digits is past WINDOW_PLACE_MOST, and is refused at the stop.
        for (; text[0] >= '0' && text[0] <= '9' && digits < 6; text++, digits++)
                number = number * 10 + (text[0] - '0');
        if (!digits || text[0] != stop || (negative && !number))
                return 0;
        number = negative ? -number : number;
        if (number < low || number > high)
                return 0;
        *value = (int)number;
        return text;
}

static int window_place_word(const char *word, const char *name)
{
        while (*name && *word == *name)
                word++, name++;
        return !*name && !*word;
}

/*
        The words of a placement, as `moonwater window` takes them and MOONWATER_PLACE holds them: center, at X,Y,
        maximized, fullscreen, screen N, size WxH and name NAME, each at most once and one of the first four. X
        and Y are -16384 to 16384, N 1 to 8, W and H 1 to 16384. Answers how many of words were the placement's
        (the first that is none of them ends it), or -1 when one of them is wrong: a number out of its range or
        with anything after its digits, a name that is none, a word with nothing after it, a word given twice,
        two of the first four, or a size with maximized or fullscreen, which are the screen's size.
*/
static int window_place_parse(struct window_place *place, const char *const *words, unsigned int count)
{
        unsigned int at = 0, seen = 0;

        for (unsigned int byte = 0; byte < sizeof(*place); byte++)
                ((char *)place)[byte] = 0;
        while (at < count)
        {
                const char *word = words[at], *value = at + 1 < count ? words[at + 1] : 0, *after;
                unsigned int bit, how = 0;
                int number;

                if (window_place_word(word, "center"))
                        how = WINDOW_PLACE_CENTER;
                else if (window_place_word(word, "maximized"))
                        how = WINDOW_PLACE_MAXIMIZED;
                else if (window_place_word(word, "fullscreen"))
                        how = WINDOW_PLACE_FULLSCREEN;
                else if (window_place_word(word, "at"))
                        how = WINDOW_PLACE_AT;
                bit = how                                 ? WINDOW_PLACE_HOW
                      : window_place_word(word, "screen") ? WINDOW_PLACE_SCREEN
                      : window_place_word(word, "size")   ? WINDOW_PLACE_SIZE
                      : window_place_word(word, "name")   ? 32u
                                                          : 0;
                if (!bit)
                        break;
                if (seen & bit)
                        return -1;
                seen |= bit;
                at++;
                if (how == WINDOW_PLACE_CENTER || how == WINDOW_PLACE_MAXIMIZED || how == WINDOW_PLACE_FULLSCREEN)
                {
                        place->how |= how;
                        continue;
                }
                if (!value)
                        return -1;
                at++;
                if (how == WINDOW_PLACE_AT)
                {
                        after = window_place_number(value, ',', -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST, &place->x);
                        if (!after || !window_place_number(after + 1, 0, -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST, &place->y))
                                return -1;
                        place->how |= how;
                }
                else if (bit == WINDOW_PLACE_SCREEN)
                {
                        if (!window_place_number(value, 0, 1, (int)WINDOW_PLACE_SCREENS, &number))
                                return -1;
                        place->screen = (unsigned int)number - 1;
                        place->how |= WINDOW_PLACE_SCREEN;
                }
                else if (bit == WINDOW_PLACE_SIZE)
                {
                        after = window_place_number(value, 'x', 1, WINDOW_PLACE_MOST, &number);
                        place->width = (unsigned int)number;
                        if (!after || !window_place_number(after + 1, 0, 1, WINDOW_PLACE_MOST, &number))
                                return -1;
                        place->height = (unsigned int)number;
                        place->how |= WINDOW_PLACE_SIZE;
                }
                else
                {
                        if (!window_name_valid(value))
                                return -1;
                        for (unsigned int letter = 0; value[letter]; letter++)
                                place->name[letter] = value[letter];
                }
        }
        if ((place->how & WINDOW_PLACE_SIZE) && ((place->how & WINDOW_PLACE_HOW) == WINDOW_PLACE_MAXIMIZED ||
                                                 (place->how & WINDOW_PLACE_HOW) == WINDOW_PLACE_FULLSCREEN))
                return -1;
        return (int)at;
}

// A whole number in decimal, a minus before it when it is below zero: where it ends.
static char *window_place_digits(char *into, int value)
{
        char digits[12];
        unsigned int count = 0, magnitude = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;

        if (value < 0)
                *into++ = '-';
        do
                digits[count++] = (char)('0' + magnitude % 10);
        while ((magnitude /= 10));
        while (count)
                *into++ = digits[--count];
        return into;
}

static char *window_place_text(char *into, const char *text)
{
        while (*text)
                *into++ = *text++;
        return into;
}

/*
        A placement as its words, the way window_place_parse reads them back, into room bytes (WINDOW_PLACE_TEXT
        holds the longest): one blank between two words and none at either end. Its length.
*/
static unsigned int window_place_say(const struct window_place *place, char *into, unsigned int room)
{
        static const char *const modes[] = {"", "center", "at", "maximized", "fullscreen"};
        char text[WINDOW_PLACE_TEXT], *at = text;
        unsigned int how = place->how & WINDOW_PLACE_HOW, length;

        if (how && how < sizeof(modes) / sizeof(modes[0]))
                at = window_place_text(at, modes[how]);
        if (how == WINDOW_PLACE_AT)
        {
                at = window_place_digits(window_place_text(at, " "), place->x);
                at = window_place_digits(window_place_text(at, ","), place->y);
        }
        if (place->how & WINDOW_PLACE_SCREEN)
                at = window_place_digits(window_place_text(at, at > text ? " screen " : "screen "), (int)place->screen + 1);
        if (place->how & WINDOW_PLACE_SIZE)
        {
                at = window_place_digits(window_place_text(at, at > text ? " size " : "size "), (int)place->width);
                at = window_place_digits(window_place_text(at, "x"), (int)place->height);
        }
        if (place->name[0])
                at = window_place_text(window_place_text(at, at > text ? " name " : "name "), place->name);
        length = (unsigned int)(at - text);
        length = length < room ? length : room ? room - 1 : 0;
        for (unsigned int byte = 0; byte < length; byte++)
                into[byte] = text[byte];
        if (room)
                into[length] = 0;
        return length;
}

// A placement into a row's columns, the name too when it has one: what the compositor reads with the first picture.
static void windows_place_set(struct windows *table, unsigned int row, const struct window_place *place)
{
        table->place_x[row] = place->x;
        table->place_y[row] = place->y;
        table->place_width[row] = place->width;
        table->place_height[row] = place->height;
        table->place_screen[row] = place->screen;
        if (place->name[0])
                for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                        table->name[row][at] = place->name[at];
        __atomic_store_n(&table->place[row], place->how, __ATOMIC_RELEASE);
}

/*
        The text of MOONWATER_PLACE as a placement: words a single blank apart, every one of them the
        placement's. False for anything else, which is no placement at all.
*/
static int window_place_from_text(struct window_place *place, const char *text)
{
        char words[WINDOW_PLACE_TEXT];
        const char *word[WINDOW_PLACE_WORDS];
        unsigned int length = 0, count = 0;

        while (text[length] && length < WINDOW_PLACE_TEXT)
                length++;
        if (!length || length == WINDOW_PLACE_TEXT)
                return 0;
        for (unsigned int at = 0; at <= length; at++)
        {
                words[at] = text[at] == ' ' ? 0 : text[at];
                if (at < length && text[at] != ' ' && (!at || text[at - 1] == ' '))
                {
                        if (count == WINDOW_PLACE_WORDS)
                                return 0;
                        word[count++] = words + at;
                }
                // A blank at either end, or two together, is a word that is empty.
                if (text[at] == ' ' && (!at || at + 1 == length || text[at + 1] == ' '))
                        return 0;
        }
        return window_place_parse(place, word, count) == (int)count;
}

/*
        MOONWATER_PLACE, into the row just taken, and emptied in the environment the program was started with, so
        a program that starts another with that environment hands on no placement. Emptied where it is, the
        string cut after its '=', and not taken out of the vector: the vector ends in the null the auxiliary
        vector is found after (the vDSO's clock is looked for there the first time the clock is read), and a
        vector made shorter in place would put that null where the walk stops too soon. A placement whose words
        are wrong is not used, and is emptied all the same.
*/
static void windows_place_take(struct windows *table, unsigned int row)
{
        string_address address_to environment = program_environment_list();
        const unsigned int prefix = sizeof(WINDOW_PLACE_VARIABLE);
        int placed = 0;
        struct window_place place;

        for (unsigned int at = 0; environment && environment[at]; at++)
        {
                char *entry = (char *)environment[at];
                unsigned int same = 0;

                while (same + 1 < prefix && entry[same] == WINDOW_PLACE_VARIABLE[same])
                        same++;
                if (same + 1 != prefix || entry[same] != '=')
                        continue;
                if (!placed && entry[prefix] && window_place_from_text(&place, entry + prefix))
                {
                        windows_place_set(table, row, &place);
                        placed = 1;
                }
                entry[prefix] = 0;
        }
}

/*
        MOONWATER_ASKED=1 in the environment the program was started with makes its row asked, which is what gives the
        keys to its first window when it shows (compositor_take). The variable is emptied the same way a placement is,
        so the programs this one starts after its first window are not asked. Any other value is not asked.
*/
static void windows_asked_take(struct windows *table, unsigned int row, string_address address_to environment)
{
        const unsigned int prefix = sizeof(WINDOW_ASKED_VARIABLE);

        for (unsigned int at = 0; environment && environment[at]; at++)
        {
                char *entry = (char *)environment[at];
                unsigned int same = 0;

                while (same + 1 < prefix && entry[same] == WINDOW_ASKED_VARIABLE[same])
                        same++;
                if (same + 1 != prefix || entry[same] != '=')
                        continue;
                if (entry[prefix] == '1' && !entry[prefix + 1])
                        __atomic_store_n(&table->asked[row], 1u, __ATOMIC_RELEASE);
                entry[prefix] = 0;
        }
}

/*
        The environment of a program the user asked for: the program's own, with MOONWATER_ASKED=1 first and any
        MOONWATER_ASKED it had taken out. Built once in a static array; a program with more variables than that is
        started as it is, without the mark, which is what it was before the mark.
*/
static string_address address_to window_asked_environment(void)
{
        static string_address environment[WINDOW_ASKED_ENVIRONMENT];
        static char asked[] = WINDOW_ASKED_VARIABLE "=1";
        string_address address_to inherited = program_environment_list();
        unsigned int used = 0;

        for (unsigned int at = 0; inherited && inherited[at]; at++)
                if (!string_has_prefix(inherited[at], WINDOW_ASKED_VARIABLE "="))
                        used++;
        if (used + 2 > WINDOW_ASKED_ENVIRONMENT)
                return inherited;

        used = 0;
        environment[used++] = asked;
        for (unsigned int at = 0; inherited && inherited[at]; at++)
                if (!string_has_prefix(inherited[at], WINDOW_ASKED_VARIABLE "="))
                        environment[used++] = inherited[at];
        environment[used] = null;
        return environment;
}

// The name a program's windows are remembered by, unless its placement named one.
static void windows_name(const struct window_row *own, const char *name)
{
        if (!name || own->table->name[own->row][0] || !window_name_valid(name))
                return;
        for (unsigned int at = 0; name[at] && at < WINDOWS_NAME - 1; at++)
                own->table->name[own->row][at] = name[at];
}

/*
        Screen index of the table's list as x, y, width and height in the desktop's space: what a window that opens
        on it centres on. A table that lists none (a headless compositor, or one from before the list) is one screen,
        the output's size at 0, 0, and an index past the list is the primary's, 0. The header is shared and any
        mapper may write it, so what is read is held to what the compositor's arithmetic takes (WINDOW_COORD_MAX).
        Answers the index it read.
*/
static unsigned int windows_output(const struct windows *table, unsigned int index, int *x, int *y, int *width,
                                   int *height)
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);
        long values[4];

        if (!count || count > WINDOWS_OUTPUTS)
        {
                index = 0;
                values[0] = values[1] = 0;
                values[2] = (long)table->output_width;
                values[3] = (long)table->output_height;
        }
        else
        {
                index = index < count ? index : 0;
                values[0] = table->output_x[index];
                values[1] = table->output_y[index];
                values[2] = (long)table->output_w[index];
                values[3] = (long)table->output_h[index];
        }
        for (unsigned int at = 0; at < 4; at++)
        {
                long low = at < 2 ? -WINDOW_COORD_MAX : 1;

                values[at] = values[at] < low ? low : values[at] > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : values[at];
        }
        *x = (int)values[0];
        *y = (int)values[1];
        *width = (int)values[2];
        *height = (int)values[3];
        return index;
}

/*
        The screen a point of the desktop is on, as windows_output reads each: the first whose rectangle holds it, and the primary's
        (index 0) when none does. Answers the index it read; x, y, width and height are that screen's.
*/
static unsigned int windows_output_at(const struct windows *table, int px, int py, int *x, int *y, int *width, int *height)
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);

        for (unsigned int at = 0; count <= WINDOWS_OUTPUTS && at < count; at++)
                if (windows_output(table, at, x, y, width, height) == at && px >= *x && px - *x < *width && py >= *y && py - *y < *height)
                        return at;
        return windows_output(table, 0, x, y, width, height);
}

/*
        The identify numbers of the screens the table lists: number[i] is screen i's, from 1, in reading order: the screens
        are read as lines of text, the top edge first and then the left edge (the top row left to right, then the row
        below it), and a screen level with another in both is numbered by its place in the list. Answers the count, which
        is windows_output's: one screen numbered 1 where the table lists none or more than WINDOWS_OUTPUTS. The edges are
        copied once, so the numbers are a permutation of 1 to the count whatever the compositor writes meanwhile. The
        identify overlay and the settings' Display map and dropdowns all take their numbers from here.
*/
static unsigned int windows_screen_numbers(const struct windows *table, unsigned int number[WINDOWS_OUTPUTS])
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);
        int left[WINDOWS_OUTPUTS], top[WINDOWS_OUTPUTS];

        if (!count || count > WINDOWS_OUTPUTS)
        {
                number[0] = 1;
                return 1;
        }
        for (unsigned int at = 0; at < count; at++)
        {
                left[at] = table->output_x[at];
                top[at] = table->output_y[at];
        }
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int rank = 1;

                for (unsigned int other = 0; other < count; other++)
                {
                        b32 first = top[other] < top[at] || (top[other] == top[at] && left[other] < left[at]) ||
                                    (top[other] == top[at] && left[other] == left[at] && other < at);

                        rank += first;
                }
                number[at] = rank;
        }
        return count;
}

/*
        Where windows were last put by the hand, by their names: what the compositor keeps across boots in a file
        of /root (registry.c's SETTING_WINDOWS_PLACES_PATH), written when a drag or a resize ends and read when a
        window of a name opens, and what `moonwater canvas windows` reads and its forget empties. One line a
        window, the newest first, PLACES_MOST of them at most:

                NAME SCREEN ORIGIN_X ORIGIN_Y X Y WIDTH HEIGHT

        SCREEN is the name of the connector the window was on (Virtual-1, HDMI-A-1), ORIGIN that screen's top
        left on the desktop when it was, and X and Y the window's top left from there. Every number is inside
        WINDOW_PLACE_MOST and a size is at least 1. A line that is not one -- a word too many or too few, a blank
        at an end or two together, a number past its bound or with anything after its digits, a name that is
        none, no line end -- is left out, and so is a second line of a name: the file is the compositor's, but
        it is a file, and a file can be anything.
*/
#define PLACES_MOST 32
#define PLACES_LINE 96
#define PLACES_BYTES (PLACES_MOST * PLACES_LINE)

struct place_kept
{
        char name[WINDOWS_NAME], screen[WINDOWS_NAME];
        int origin_x, origin_y, x, y, width, height;
};

struct places
{
        unsigned int count;
        struct place_kept kept[PLACES_MOST];
};

// A connector's name as a place is kept with it: letters, digits and hyphens, one to fifteen of them.
static int place_screen_name_valid(const char *name)
{
        unsigned int at = 0;

        for (; name[at] && at < WINDOWS_NAME; at++)
                if (!((name[at] >= 'a' && name[at] <= 'z') || (name[at] >= 'A' && name[at] <= 'Z') ||
                      (name[at] >= '0' && name[at] <= '9') || name[at] == '-'))
                        return 0;
        return at && at < WINDOWS_NAME;
}

// The line of a name, or -1.
static int places_find(const struct places *places, const char *name)
{
        for (unsigned int at = 0; at < places->count; at++)
        {
                unsigned int letter = 0;

                while (letter < WINDOWS_NAME && places->kept[at].name[letter] == name[letter] && name[letter])
                        letter++;
                if (letter < WINDOWS_NAME && places->kept[at].name[letter] == name[letter])
                        return (int)at;
        }
        return -1;
}

// One line, ended at its line end, as a place: false when it is none.
static int places_line(char *line, struct place_kept *kept)
{
        char *field[8];
        unsigned int fields = 0;
        int value[6];

        for (unsigned int at = 0;; at++)
        {
                if (line[at] && line[at] != ' ')
                {
                        if (!at || !line[at - 1])
                        {
                                if (fields == 8)
                                        return 0;
                                field[fields++] = line + at;
                        }
                        continue;
                }
                // A blank first, last or after another is a field that is empty.
                if (line[at] == ' ' && (!at || !line[at - 1] || !line[at + 1]))
                        return 0;
                if (!line[at])
                        break;
                line[at] = 0;
        }
        if (fields != 8 || !window_name_valid(field[0]) || !place_screen_name_valid(field[1]))
                return 0;
        for (unsigned int at = 0; at < 6; at++)
                if (!window_place_number(field[2 + at], 0, at < 4 ? -WINDOW_PLACE_MOST : 1, WINDOW_PLACE_MOST, &value[at]))
                        return 0;
        for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                kept->name[at] = kept->screen[at] = 0;
        for (unsigned int at = 0; field[0][at]; at++)
                kept->name[at] = field[0][at];
        for (unsigned int at = 0; field[1][at]; at++)
                kept->screen[at] = field[1][at];
        kept->origin_x = value[0];
        kept->origin_y = value[1];
        kept->x = value[2];
        kept->y = value[3];
        kept->width = value[4];
        kept->height = value[5];
        return 1;
}

// The places a file's text holds, of length bytes.
static void places_parse(struct places *places, const char *text, unsigned long length)
{
        unsigned long at = 0;

        places->count = 0;
        while (at < length && places->count < PLACES_MOST)
        {
                char line[PLACES_LINE];
                unsigned long size = 0;
                struct place_kept kept;

                for (; at + size < length && text[at + size] != '\n'; size++)
                        if (size < PLACES_LINE)
                                line[size] = text[at + size];
                // A line with no end is a line cut short, and one longer than any the compositor writes is none.
                if (at + size >= length || size >= PLACES_LINE)
                {
                        at += size + 1;
                        continue;
                }
                line[size] = 0;
                at += size + 1;
                if (places_line(line, &kept) && places_find(places, kept.name) < 0)
                        places->kept[places->count++] = kept;
        }
}

// The places as the file keeps them, into room bytes (PLACES_BYTES holds all of them): its length.
static unsigned long places_say(const struct places *places, char *into, unsigned long room)
{
        unsigned long length = 0;

        for (unsigned int at = 0; at < places->count; at++)
        {
                const struct place_kept *kept = &places->kept[at];
                const int numbers[6] = {kept->origin_x, kept->origin_y, kept->x, kept->y, kept->width, kept->height};
                char line[PLACES_LINE * 2], *tail = window_place_text(line, kept->name);

                tail = window_place_text(window_place_text(tail, " "), kept->screen);
                for (unsigned int number = 0; number < 6; number++)
                        tail = window_place_digits(window_place_text(tail, " "), numbers[number]);
                *tail++ = '\n';
                if (length + (unsigned long)(tail - line) > room)
                        break;
                for (char *byte = line; byte < tail; byte++)
                        into[length++] = *byte;
        }
        return length;
}

// A place noted: the newest line, the line of its name it had taken out, and the oldest left off when they are all used.
static void places_note(struct places *places, const struct place_kept *kept)
{
        int found = places_find(places, kept->name);
        unsigned int last = found >= 0 ? (unsigned int)found : places->count < PLACES_MOST ? places->count : PLACES_MOST - 1;

        for (unsigned int at = last; at > 0; at--)
                places->kept[at] = places->kept[at - 1];
        places->kept[0] = *kept;
        if (found < 0 && places->count < PLACES_MOST)
                places->count++;
}

/*
        A row taken: the table mapped and its header copied and checked (-22 when it does not fit), the first row
        with no pid given this one, the row's program fields reset and its placement taken from MOONWATER_PLACE
        (windows_place_take), and its door and its wake opened. The row is
        claimed with WINDOWS_TAKING, a pid no peer takes for a program, and the program's pid is stored last, so a
        peer never finds a row half reset. The door is opened for writing first, which fails when no compositor
        holds its other end, and then for reading and writing, which is the end kept: a knock on a door whose
        compositor has gone is then a byte in a pipe and never a SIGPIPE. Answers 0 or a negative errno.
*/
#define WINDOWS_TAKING 0x80000000u


// The radio a type of rfkill switch is for, as the Settings key its issue is under (window.c's WINDOWS_KEY_GPU style), or null for a type that is none
// of the radios Settings has a row for. A radio blocked by a switch, in software or in hardware, is the fault; the detail says which.
static const char *radio_key_of_type(const char *type)
{
        if (type && string_equals((string_address)type, (string_address)"wlan"))
                return "wifi";
        if (type && string_equals((string_address)type, (string_address)"bluetooth"))
                return "bluetooth";
        return null;
}

static const char *radio_blocked_detail(const char *type, int soft, int hard)
{
        if (!radio_key_of_type(type) || (!soft && !hard))
                return null;
        if (hard)
                return "blocked by its hardware switch: turn the radio on at the switch";
        return "blocked in software (rfkill): run rfkill unblock wifi (or bluetooth)";
}

// Reads a small file of sysfs, whole, into buf (ended): its length, or -1.
static long radio_read_file(const char *path, char *buf, unsigned int room)
{
        long file = window_call(WINDOW_SYS_OPENAT, -100, (long)path, 0x80000, 0, 0, 0), got = -1;

        if (file < 0)
                return -1;
        got = window_call(WINDOW_SYS_READ, file, (long)buf, room - 1, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        if (got < 0)
                return -1;
        buf[got] = 0;
        for (long i = 0; i < got; i++)
                if (buf[i] == '\n')
                        buf[i] = 0, got = i;
        return got;
}

// The path of one of an rfkill switch's files (/sys/class/rfkill/rfkillN/leaf), for N of one digit.
static void radio_path(char *path, unsigned int room, unsigned int n, const char *leaf)
{
        string_copy_bounded((string_address)path, (string_address)"/sys/class/rfkill/rfkill", room);
        path[24] = (char)('0' + (n % 10)), path[25] = 0;
        string_append_bounded((string_address)path, (string_address)"/", room);
        string_append_bounded((string_address)path, (string_address)leaf, room);
}

// The rfkill switches (/sys/class/rfkill, eight at most): a radio that is blocked is on the board under its key, and one that is not is cleared.
// Read once a second with the kernel's records (kernel_notices_tick).
static void radio_state_tick(struct windows *table)
{
        if (!table)
                return;
        for (unsigned int n = 0; n < 8; n++)
        {
                char type[16], soft[4], hard[4], path[64];
                const char *key, *detail;

                radio_path(path, sizeof(path), n, "type");
                if (radio_read_file(path, type, sizeof(type)) < 0 || !(key = radio_key_of_type(type)))
                        continue;
                radio_path(path, sizeof(path), n, "soft");
                if (radio_read_file(path, soft, sizeof(soft)) < 0)
                        soft[0] = 0;
                radio_path(path, sizeof(path), n, "hard");
                if (radio_read_file(path, hard, sizeof(hard)) < 0)
                        hard[0] = 0;
                detail = radio_blocked_detail(type, soft[0] == '1', hard[0] == '1');
                if (detail)
                        windows_issue_set(table, key, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_RADIO, 0, 0, detail);
                else
                        windows_issue_clear(table, key, WINDOWS_ISSUE_RADIO);
        }
}

// Dismisses the kernel notices of the classes in mask (bit i is class i): their issues leave the board now, and the feeder starts each class again
// from nothing at its next publish (kernel_notices_publish takes the bit). An error stays on the board until it is dismissed, so this is how it goes.
// The number of issues cleared.
static unsigned int windows_kernel_dismiss(struct windows *table, unsigned int mask)
{
        unsigned int cleared = 0;

        if (!table)
                return 0;
        for (unsigned int idx = 0; idx < KERNEL_NOTICE_CLASSES; idx++)
                if (mask >> idx & 1u)
                {
                        cleared += windows_issue_clear(table, kernel_notice_key[idx], WINDOWS_ISSUE_KERNEL_GPU + idx);
                        __atomic_or_fetch(&table->kernel_dismiss, 1u << idx, __ATOMIC_RELEASE);
                }
        return cleared;
}

// One tick of the feeder (once a second from the compositor): reads up to KERNEL_NOTICE_MOST records from /dev/kmsg without waiting, counts
// them, and publishes. /dev/kmsg is opened on the first tick (the compositor is root); a kernel that does not give it is not asked again.
static void kernel_notices_tick(struct windows *table, unsigned long long now)
{
        struct kernel_notices *k = &kernel_notice_state;
        char record[KERNEL_NOTICE_LINE];

        if (k->fd == -1)
        {
                long file = window_call(WINDOW_SYS_OPENAT, -100, (long)KMSG_FILE, 0x800 | 0x80000, 0, 0, 0); // O_NONBLOCK | O_CLOEXEC, O_RDONLY

                k->fd = file >= 0 ? (int)file : -2;
        }
        for (unsigned int n = 0; k->fd >= 0 && n < KERNEL_NOTICE_MOST; n++)
        {
                long got = window_call(WINDOW_SYS_READ, k->fd, (long)record, sizeof(record), 0, 0, 0);

                if (got <= 0)
                        break;
                kernel_notices_record(k, record, (unsigned int)got, now);
        }
        kernel_notices_publish(table, k, now);
        radio_state_tick(table);
}

/*
        The table for a program that is not a window and only writes the board (the audio service): mapped read-write from the table's file, once,
        and kept. The table is root's (WINDOWS_MODE), so only a root process raises an issue; a process that cannot map it gets none (null), and its
        issues are simply not shown.
*/
static struct windows *windows_board_cache;

static struct windows *windows_board_map(void)
{
        long file, size, mapped;

        if (windows_board_cache)
                return windows_board_cache;
        file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, WINDOW_OPEN_FLAGS, 0, 0, 0);
        if (file < 0)
                return null;
        size = window_call(WINDOW_SYS_LSEEK, file, 0, 2, 0, 0, 0);
        // PROT_READ | PROT_WRITE, MAP_SHARED
        mapped = size >= (long)sizeof(struct windows) ? window_call(WINDOW_SYS_MMAP, 0, size, 3, 1, file, 0) : -22;
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        if (mapped < 0 && mapped > -4096)
                return null;
        windows_board_cache = (struct windows *)mapped;
        return windows_board_cache;
}

static long windows_take(struct window_row *own)
{
        long spark = window_call(WINDOW_SYS_OPENAT, -100, (long)"/dev/spark", WINDOW_OPEN_FLAGS, 0, 0, 0);
        long file = spark >= 0 ? window_call(WINDOW_SYS_IOCTL, spark, WINDOWS_IOCTL_TABLE_GET, 0, 0, 0, 0) : -2;
        long size, mapped, probe;
        unsigned int pid = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
        struct windows *table;
        unsigned long pixels, buffer;
        unsigned int stride, width, height;
        char name[48];

        // The table from the device, which hands any program the compositor's; the path where it gave none
        // (a headless compositor's file).
        if (spark >= 0)
                window_call(WINDOW_SYS_CLOSE, spark, 0, 0, 0, 0, 0);
        if (file < 0)
                file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, WINDOW_OPEN_FLAGS, 0, 0, 0);
        if (file < 0)
                return file;

        size = window_call(WINDOW_SYS_LSEEK, file, 0, 2, 0, 0, 0);
        // PROT_READ | PROT_WRITE, MAP_SHARED
        mapped = size >= (long)sizeof(struct windows) ? window_call(WINDOW_SYS_MMAP, 0, size, 3, 1, file, 0) : -22;
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        if (mapped < 0 && mapped > -4096)
                return mapped;

        table = (struct windows *)mapped;
        // The header read once, each field into the program's copy, and checked there: a peer may write any of it.
        pixels = __atomic_load_n(&table->pixels, __ATOMIC_ACQUIRE);
        buffer = __atomic_load_n(&table->buffer, __ATOMIC_ACQUIRE);
        stride = __atomic_load_n(&table->stride, __ATOMIC_ACQUIRE);
        width = __atomic_load_n(&table->output_width, __ATOMIC_ACQUIRE);
        height = __atomic_load_n(&table->output_height, __ATOMIC_ACQUIRE);
        if (!windows_fits((unsigned long)size, pixels, buffer, stride, width, height) || !windows_lists_fit(table, (unsigned long)size, buffer))
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0);
                return -22; // EINVAL
        }
        own->table = table;
        own->bytes = (unsigned long)size;
        own->buffer = buffer;
        own->stride = stride;
        own->output_width = width;
        own->output_height = height;

        for (own->row = 0; own->row < WINDOWS; own->row++)
        {
                unsigned int none = 0;

                if (__atomic_compare_exchange_n(&table->pid[own->row], &none, WINDOWS_TAKING, 0, __ATOMIC_ACQ_REL,
                                                __ATOMIC_RELAXED))
                        break;
        }

        if (own->row == WINDOWS)
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0);
                return -28; // ENOSPC
        }
        windows_reset_row(table, own->row);
        windows_place_take(table, own->row);
        windows_asked_take(table, own->row, program_environment_list());

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
        __atomic_store_n(&table->pid[own->row], pid, __ATOMIC_RELEASE);
        return 0;
}

// Something for the compositor to look at.
static void windows_knock(const struct window_row *own)
{
        char byte = 0;

        window_call(WINDOW_SYS_WRITE, own->door, (long)&byte, 1, 0, 0, 0);
}

/*
        A key the on-screen keyboard sends the window with the keys (WINDOW_KEYBOARD): one entry of the row's ring, knocked on. A ring of
        WINDOWS_OSK entries the compositor has not read yet refuses the key (0): a keyboard that taps faster than the compositor reads is
        not a case a person reaches, and a refused key is none rather than a late one.
*/
static int windows_keyboard_send(const struct window_row *own, int code, int character, unsigned int flags)
{
        struct windows *table = own->table;
        unsigned int head = table->osk_head[own->row], at;

        if (head - __atomic_load_n(&table->osk_tail[own->row], __ATOMIC_ACQUIRE) >= WINDOWS_OSK)
                return 0;
        at = head % WINDOWS_OSK;
        table->osk_code[own->row][at] = code;
        table->osk_character[own->row][at] = character;
        table->osk_flags[own->row][at] = flags;
        __atomic_store_n(&table->osk_head[own->row], head + 1, __ATOMIC_RELEASE);
        windows_knock(own);
        return 1;
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

// The next event, or false when there is none. Its number is taken and 0 left in its place, which tells the compositor
// that joins a wheel's turns to the last unread one (compositor_post) that this one is read and no longer to be added to.
static int windows_event(const struct window_row *own, struct window_event *event)
{
        struct windows *table = own->table;
        unsigned int row = own->row, tail = table->event_tail[row], at = tail % WINDOWS_EVENTS;

        if (__atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE) == tail)
                return 0;

        event->type = table->event_type[row][at];
        event->flags = table->event_flags[row][at];
        event->a = __atomic_exchange_n(&table->event_a[row][at], 0, __ATOMIC_ACQ_REL);
        event->b = table->event_b[row][at];
        event->when = table->event_when[row][at];
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

/*
        windows_wait with one more wake: a popup's row of the same program (-1 for none), which a menu's events knock on.
*/
static long windows_wait_also(const struct window_row *own, long also, long other, long nanoseconds)
{
        struct window_poll wanted[3];
        long timeout[2] = {nanoseconds / 1000000000, nanoseconds % 1000000000}, ready;
        unsigned int count = 0, also_at = 3;

        wanted[count++] = (struct window_poll){(int)own->wake, 1, 0};
        if (also >= 0)
                also_at = count, wanted[count++] = (struct window_poll){(int)also, 1, 0};
        if (other >= 0)
                wanted[count++] = (struct window_poll){(int)other, 1, 0};
        ready = window_call(WINDOW_SYS_PPOLL, (long)wanted, count, nanoseconds < 0 ? 0 : (long)timeout, 0, 0, 0);
        if (ready > 0 && wanted[0].returned)
        {
                windows_drain(own->wake);
                if (wanted[0].returned & 16) // POLLHUP
                        return -1;
        }
        if (ready > 0 && also_at < 3 && wanted[also_at].returned)
                windows_drain(also);
        return ready;
}

/*
        What of buffer which the compositor wrote over since the program last drew into it: false when nothing,
        and the rectangle cleared once read. The program draws it again, as it would cells that changed.
*/
static int windows_spoiled(const struct window_row *own, unsigned int which, int *x, int *y, int *width,
                           int *height)
{
        struct windows *table = own->table;
        unsigned int row = own->row;

        which &= 1u;
        if (__atomic_load_n(&table->spoiled_height[row][which], __ATOMIC_ACQUIRE) <= 0)
                return 0;
        *x = table->spoiled_x[row][which];
        *y = table->spoiled_y[row][which];
        *width = table->spoiled_width[row][which];
        *height = table->spoiled_height[row][which];
        __atomic_store_n(&table->spoiled_height[row][which], 0, __ATOMIC_RELEASE);
        return *width > 0 && *height > 0;
}

// A buffer's pages given back, for a program that will draw it whole before it shows it again; never while the
// compositor shows the window straight from its buffers, whose pages the card reads.
static void windows_discard(const struct window_row *own, unsigned int which)
{
        if (__atomic_load_n(&own->table->pinned[own->row], __ATOMIC_ACQUIRE))
                return;
        // MADV_REMOVE
        window_call(WINDOW_SYS_MADVISE, (long)windows_pixels(own, which), (long)own->buffer, 9, 0, 0, 0);
}

// The window gone: its door closed, which is all the compositor needs.
static void windows_leave(struct window_row *own)
{
        window_call(WINDOW_SYS_CLOSE, own->door, 0, 0, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, own->wake, 0, 0, 0, 0, 0);
        window_call(WINDOW_SYS_MUNMAP, (long)own->table, (long)own->bytes, 0, 0, 0, 0);
        own->table = 0;
}

/*
        The clipboard from a program's side. A copy: the text into a memfd sealed against any change, named in the row,
        and a knock; the memfd is held until the compositor says it took it (windows_clip_done) or the next copy of the
        selection. A paste: asked for with a knock, and read once the compositor says READY, from its own memfd through
        /proc/PID/fd -- the length it said, at most the limit, and refused unless it is UTF-8 with no NUL, whatever the
        compositor checked before.
*/
#define WINDOWS_MFD_CLOEXEC 1u
#define WINDOWS_MFD_SEALING 2u
#define WINDOWS_F_ADD_SEALS 1033
#define WINDOWS_F_GET_SEALS 1034
#define WINDOWS_SEALS 15 // SEAL, SHRINK, GROW and WRITE

static long windows_clip_held[2] = {-1, -1};

// A memfd holding bytes, sealed so neither its size nor its bytes can change again: the descriptor, or a negative errno.
static long windows_clip_memfd(const void *bytes, unsigned long length)
{
        long fd = window_call(WINDOW_SYS_MEMFD, (long)"moonwater-copy", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0);
        unsigned long done = 0;

        if (fd < 0)
                return fd;
        while (done < length)
        {
                long wrote = window_call(WINDOW_SYS_WRITE, fd, (long)((const char *)bytes + done), (long)(length - done), 0, 0, 0);

                if (wrote <= 0)
                {
                        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                        return wrote < 0 ? wrote : -5;
                }
                done += (unsigned long)wrote;
        }
        if (window_call(WINDOW_SYS_FCNTL, fd, WINDOWS_F_ADD_SEALS, WINDOWS_SEALS, 0, 0, 0) < 0)
        {
                window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                return -1;
        }
        return fd;
}

static long windows_clip_offer(const struct window_row *own, unsigned int which, const void *bytes, unsigned long length)
{
        struct windows *table = own->table;
        long fd;

        which &= 1u;
        if (!length || length > WINDOWS_CLIP_LIMIT)
                return -22;
        fd = windows_clip_memfd(bytes, length);
        if (fd < 0)
                return fd;
        if (windows_clip_held[which] >= 0)
                window_call(WINDOW_SYS_CLOSE, windows_clip_held[which], 0, 0, 0, 0, 0);
        windows_clip_held[which] = fd;
        table->clip_offer_fd[own->row] = (int)fd;
        table->clip_offer_which[own->row] = which;
        __atomic_add_fetch(&table->clip_offer_serial[own->row], 1, __ATOMIC_RELEASE);
        windows_knock(own);
        return 0;
}

// The copy of which the compositor answered for: the memfd it opened for itself is let go here.
static void windows_clip_done(unsigned int which)
{
        which &= 1u;
        if (windows_clip_held[which] >= 0)
                window_call(WINDOW_SYS_CLOSE, windows_clip_held[which], 0, 0, 0, 0, 0);
        windows_clip_held[which] = -1;
}

static void windows_clip_ask(const struct window_row *own, unsigned int which)
{
        own->table->clip_ask_which[own->row] = which & 1u;
        __atomic_add_fetch(&own->table->clip_ask_serial[own->row], 1, __ATOMIC_RELEASE);
        windows_knock(own);
}

// Whether bytes are text a selection may hold: UTF-8, every sequence whole and shortest, no surrogate, no NUL.
static int windows_clip_text(const unsigned char *bytes, unsigned long length)
{
        for (unsigned long at = 0; at < length;)
        {
                unsigned int lead = bytes[at], size, value;

                if (!lead)
                        return 0;
                if (lead < 0x80)
                {
                        at++;
                        continue;
                }
                if (lead < 0xc2 || lead > 0xf4)
                        return 0;
                size = lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                if (length - at < size)
                        return 0;
                value = lead & (0x7fu >> size);
                for (unsigned int k = 1; k < size; k++)
                {
                        if ((bytes[at + k] & 0xc0) != 0x80)
                                return 0;
                        value = value << 6 | (bytes[at + k] & 0x3f);
                }
                if ((size == 3 && (value < 0x800 || (value >= 0xd800 && value <= 0xdfff))) ||
                    (size == 4 && (value < 0x10000 || value > 0x10ffff)))
                        return 0;
                at += size;
        }
        return 1;
}

// "/proc/PID/fd/NUMBER" into path (48 bytes).
static void windows_proc_fd(char *path, unsigned int pid, unsigned int number)
{
        unsigned int at = 0, values[2] = {pid, number};
        const char *parts[3] = {"/proc/", "/fd/", ""};

        for (unsigned int part = 0; part < 3; part++)
        {
                char digits[12];
                unsigned int count = 0;

                for (const char *from = parts[part]; *from; from++)
                        path[at++] = *from;
                if (part == 2)
                        break;
                do
                        digits[count++] = (char)('0' + values[part] % 10);
                while ((values[part] /= 10));
                while (count)
                        path[at++] = digits[--count];
        }
        path[at] = 0;
}

/*
        A selection's text read from the compositor's memfd of it: into a mapping of its own (unmapped with munmap of
        *length + 1), the length it says checked against the file's, at most the limit, UTF-8 with no NUL. The length,
        or a negative errno. want is what the compositor said the length is.
*/
static long windows_clip_fetch(unsigned int pid, unsigned int which, unsigned long want, unsigned char **bytes,
                               unsigned long *length)
{
        char path[48];
        unsigned long got = 0;
        long fd, mapped, size;

        *bytes = 0;
        *length = 0;
        if (!pid || !want || want > WINDOWS_CLIP_LIMIT)
                return -2;
        windows_proc_fd(path, pid, WINDOWS_CLIP_FD + (which & 1u));
        fd = window_call(WINDOW_SYS_OPENAT, -100, (long)path, 0x80000, 0, 0, 0); // O_RDONLY | O_CLOEXEC
        if (fd < 0)
                return fd;
        size = window_call(WINDOW_SYS_LSEEK, fd, 0, 2, 0, 0, 0);
        window_call(WINDOW_SYS_LSEEK, fd, 0, 0, 0, 0, 0);
        mapped = size == (long)want ? window_call(WINDOW_SYS_MMAP, 0, (long)want + 1, 3, 0x22, -1, 0) : -22;
        if (mapped < 0 && mapped > -4096)
        {
                window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                return mapped;
        }
        while (got < want)
        {
                long read = window_call(WINDOW_SYS_READ, fd, mapped + (long)got, (long)(want - got), 0, 0, 0);

                if (read <= 0)
                        break;
                got += (unsigned long)read;
        }
        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        if (got != want || !windows_clip_text((const unsigned char *)mapped, got))
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, (long)want + 1, 0, 0, 0, 0);
                return -74; // EBADMSG
        }
        *bytes = (unsigned char *)mapped;
        *length = got;
        return (long)got;
}

// The paste the compositor said is READY for this window.
static long windows_clip_read(const struct window_row *own, unsigned int which, unsigned char **bytes, unsigned long *length)
{
        return windows_clip_fetch(__atomic_load_n(&own->table->clip_pid, __ATOMIC_ACQUIRE), which,
                                  __atomic_load_n(&own->table->clip_given_length[own->row], __ATOMIC_ACQUIRE), bytes, length);
}

static void windows_clip_free(unsigned char *bytes, unsigned long length)
{
        if (bytes)
                window_call(WINDOW_SYS_MUNMAP, (long)bytes, (long)length + 1, 0, 0, 0, 0);
}

/*
        Whether the pointer's motion over the window is wanted. A program that
        draws no hover -- a terminal that is not in its any-motion mode -- says
        it does not, and the compositor sends it none, except what a button
        held or pressed over it needs; every report of the motion is a wakeup
        for the program otherwise. A program that never says is sent all of it.
*/

/*
        Whether the window takes WINDOW_EVENT_PINCH. One that does not is sent a pinch as Ctrl and a wheel (compositor_pinch_legacy),
        so a program that handles a wheel with Ctrl zooms with a pinch without knowing one.
*/

/*
        A colour as the settings keep it: #rrggbb or rrggbb, or the three digits of either that stand
        for each one twice. Negative for anything else.
*/
static long window_colour_parse(string_address text)
{
        p8 digits[6], bytes[3];

        if (*text == '#')
                text++;
        if (string_length(text) == 3)
        {
                for (positive at = 0; at < 3; at++)
                        digits[at * 2] = digits[at * 2 + 1] = text[at];
                text = digits;
        }
        else if (string_length(text) != 6)
                return -1;
        return memory_from_hex(bytes, text, 3) ? (long)(bytes[0] << 16 | bytes[1] << 8 | bytes[2]) : -1;
}

#endif // WINDOW_INCLUDED
