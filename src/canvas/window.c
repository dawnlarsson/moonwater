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
// anywhere but on it asks it to go, as the close button would (the launcher has both).
#define WINDOW_NO_CLOSE 2u
#define WINDOW_DISMISS 4u

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
        // Why the top window was not shown straight at the last present: 0 it was; 1 no card; 2 the table is no memfd;
        // 3 the pointer is drawn in software; 4 no window, or one dragged or resized; 5 it is glass; 6 it is not at the
        // size it drew; 7 not wholly on the output; 8 too short past its corners and close button; 9 it meets the
        // tabs; 10 the card has no overlay for it; 11 the card refused its framebuffer (refused says why); 12 the card
        // refused the overlay's commit; 13 the desktop spans more than one screen, where every frame is composed.
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
        unsigned int ground[WINDOWS]; // the compositor's: what a glass window's tint came to, 0xrrggbb
        unsigned int lift[WINDOWS];   // anyone's: one more raises the window and gives it the keys
        unsigned int hover_off[WINDOWS]; // the program's: 1 when it does not want the pointer's motion over it
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

        unsigned int event_head[WINDOWS], event_tail[WINDOWS];
        unsigned short event_type[WINDOWS][WINDOWS_EVENTS], event_flags[WINDOWS][WINDOWS_EVENTS];
        int event_a[WINDOWS][WINDOWS_EVENTS], event_b[WINDOWS][WINDOWS_EVENTS];
};

// Where the pixels start: the compositor's header, a page-rounded struct windows from the table's start. It is a
// constant here and not the header's pixels field, which any mapper of the table may write.
#define WINDOWS_HEADER ((sizeof(struct windows) + 4095) & ~4095ul)

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
};

_Static_assert(sizeof(struct window) == 1288, "window page ABI");
_Static_assert(sizeof(struct window_key) == 16, "window key ABI");
_Static_assert(sizeof(struct window_cell) == 8, "window cell ABI");

/* Every field of the page, where the compositor and its programs both
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
static void window_flush(struct window *window)
{
        __atomic_store_n(&window->sequence, window->sequence + 1, __ATOMIC_RELEASE);
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
        table->ground[row] = 0;
        __atomic_store_n(&table->lift[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->hover_off[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->spoiled_height[row][0], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&table->spoiled_height[row][1], 0, __ATOMIC_RELEASE);
        table->event_tail[row] = __atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE);
        // A placement and a name are the program that asked's, and not the next one's to open in the row.
        table->place[row] = table->place_screen[row] = 0;
        table->place_x[row] = table->place_y[row] = 0;
        table->place_width[row] = table->place_height[row] = 0;
        for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                table->name[row][at] = 0;
        __atomic_store_n(&table->moved[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->shown[row], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&table->frame[row], 0, __ATOMIC_RELEASE);
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
        if (!windows_fits((unsigned long)size, pixels, buffer, stride, width, height))
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
        Whether the pointer's motion over the window is wanted. A program that
        draws no hover -- a terminal that is not in its any-motion mode -- says
        it does not, and the compositor sends it none, except what a button
        held or pressed over it needs; every report of the motion is a wakeup
        for the program otherwise. A program that never says is sent all of it.
*/
static void windows_hover(const struct window_row *own, unsigned int wanted)
{
        __atomic_store_n(&own->table->hover_off[own->row], wanted ? 0u : 1u, __ATOMIC_RELEASE);
}

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
