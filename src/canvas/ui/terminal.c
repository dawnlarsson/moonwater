/*
        The terminal that draws its own pixels.

        `terminal` is a program on the compositor in userspace
        (src/canvas/compositor.c): it takes a window, runs a shell on a pty,
        keeps the cells of the screen in its own memory -- the emulator in
        term.c, on a ring that is an anonymous mapping and not a page the
        compositor holds -- and turns the cells that changed into pixels with
        the text engine (src/canvas/font.c) into the window's buffers. The
        compositor sees pixels, a size and events; what a terminal is stays
        here, so the size of the font is this program's to choose, any pixel
        size is crisp, and nothing in the kernel knows what a cell is.

        What it draws is what the kernel's cells draw: the same palette, the
        same folding of a stored line at the width it is drawn in, a bar down
        the right that is the scrollback, seen while the view is scrolled
        back, and the wheel and a drag on the bar to move over it. It owes
        the shell what the kernel's window owed it -- keys, the pointer when
        a program has asked for it, a window size on the pty before SIGWINCH,
        the hangup when the X is pressed -- and those come through the same
        functions in screen.c.

        Two buffers, and the rule that comes with two. A buffer the compositor
        may still be reading is not drawn into: the cells go into the emulator
        the moment they arrive, and a frame is made only when the compositor
        has taken the last picture, so the buffer that is not on the screen
        is free (a frame that has waited out the compositor is the one
        exception, and draws into the picture it has not taken). A burst of
        output is therefore one frame, not one a read, and a key's echo is on
        the screen in the first frame there is room for.

        Each buffer's picture is remembered as the cells that made it, so a
        frame draws only the cells that differ from what that buffer holds,
        and tells the compositor the rectangle it has to read again: the cells that
        differ from the buffer being drawn into, and those that differ from the
        one on the screen, which are not always the same.

        The font is a path: the image carries IBM Plex Mono under
        /usr/share/fonts, and the machine script names it.

            terminal [-s PIXELS] [-f FONT.ttf]... [-b BOLD.ttf] [-g COLUMNSxROWS] [-w READY] [-x PROGRAM [WORD]...]

        -x runs a program on the pty in place of the shell (everything after it
        is the program's words), which is how the launcher shows a command's
        output: the window says how the program ended and stays, its text where
        it was, until a key is pressed or its X.

        -w READY creates the file READY once the first picture is shown, which
        is how whoever starts windows in an order knows the next one may go.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_TERMINAL
#define MOONWATER_CANVAS_TERMINAL

// The runner and the drawing primitives the window's engines are made with. ui.c has its own guard, and the shell
// includes it under the features' switches, which the terminal does not have: it is always built with this.
#include "ui.c"

// Lines kept at least: what is on the screen and what has scrolled off it, as the kernel's window kept
// them, and never fewer than two screens of the tallest window the output could hold.
#define TERMINAL_HISTORY 512
#define TERMINAL_FACES 4
// Every face the options can name: each -f font, and the -b face after them.
#define TERMINAL_FACE_SLOTS (TERMINAL_FACES + 1)
// The window of each on a plain boot, as percents of the output: the width and height it takes and where its top left
// corner goes. The log is behind and the terminal in front, over the log's right edge; the compositor keeps both on the
// work area, which the shares leave room for at any scale a panel takes.
#define TERMINAL_WIDE 52
#define TERMINAL_HIGH 62
#define TERMINAL_LEFT 44
#define TERMINAL_TOP 14
#define TERMINAL_LOG_WIDE 52
#define TERMINAL_LOG_HIGH 62
#define TERMINAL_LOG_LEFT 3
#define TERMINAL_LOG_TOP 6
// Lines the log keeps: the kernel's own ring is 128 KiB of text and is what this follows from the start.
#define TERMINAL_LOG_HISTORY 1024
// The text sizes a terminal zooms through, in pixels: one step a press, each terminal its own. The default
// is one step below the 16 this program drew at before there was a ladder, and four steps below it the
// floor is the reader's eyes.
#define TERMINAL_STEPS 16
static const unsigned int terminal_steps[TERMINAL_STEPS] = {8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48, 56, 64};
#define TERMINAL_SIZE 14
// The step the last zoom left, in one file: written to a temporary name and renamed over it, and read by a
// terminal when it starts, which is where a new one begins. A missing or corrupt file is the default.
#define TERMINAL_SIZE_DIR "/root/terminal"
#define TERMINAL_SIZE_FILE "/root/terminal/size"
#define TERMINAL_SIZE_TEMP "/root/terminal/size.tmp"
// The glyph atlas's side: a screen of code at 64 px, bold with it, is a few hundred cells of 39 by 84, more
// than 512 square holds without emptying mid-frame. Only the pages drawn into are ever resident.
#define TERMINAL_ATLAS 2048
// A ring is cut for the widest display there is, or this, as the kernel's was.
#define TERMINAL_CEILING 3840
// Bytes taken from the pty in one go before a frame is looked at.
#define TERMINAL_BUDGET (256 * 1024)
// Three lines a wheel step, as the kernel's window scrolled.
#define TERMINAL_WHEEL_LINES 3
#define TERMINAL_LIVE ((unsigned int)-1)
#define TERMINAL_KEY_BACKSPACE 14
// A held synchronized frame is let go of after this long.
#define TERMINAL_SYNC_NANOSECONDS 200000000ll
// How long a frame waits when output has just ended a line, for what comes next on it: a prompt.
#define TERMINAL_SETTLE_NANOSECONDS 3000000ll
// A buffer that is not on the screen is given back after the terminal has been left alone this long.
#define TERMINAL_IDLE_NANOSECONDS 1000000000ll
// The longest a frame of the output lasts, when the compositor does not say.
#define TERMINAL_FRAME_NANOSECONDS 16666667ll
// How long the scroll bar takes to fade once the view is back at the live end.
#define TERMINAL_FADE_NANOSECONDS 250000000ll
// A frame that has waited this long for the compositor to take the last picture draws into the one it has
// not taken regardless: the compositor not on the screen is not reading it, and a terminal that never draws
// again is worse.
#define TERMINAL_STALL_NANOSECONDS 250000000ll

typedef unsigned long long __attribute__((may_alias)) terminal_word;

// The loop's phases: the shell's output and keys (running); the shell gone, with what it said left on the screen until a
// key or the close (staying); and over (done), which is when the runner leaves.
#define TERMINAL_RUNNING 0
#define TERMINAL_STAYING 1
#define TERMINAL_DONE 2

static struct
{
        struct window_row own; // its row of the compositor's table
        struct ui ui;          // the main engine's: the atlas and the picture's inks, as every window's
        font_face face[TERMINAL_FACE_SLOTS];
        const font_face *plain[TERMINAL_FACES];
        const font_face *bold[TERMINAL_FACES];
        unsigned int plain_count, bold_count;
        font_metrics grid;
        unsigned int size; // in 26.6, which is what font.c takes
        // The paper round the grid: margin at least on every side, at the left (pad), at the right (bar),
        // where the scroll bar is, and above and below (pad_y), which also takes half of what is left of the
        // window's height under its whole rows, so the grid sits in the middle of it.
        unsigned int cell_w, cell_h, bar, pad, pad_y, margin;
        unsigned int width, height; // the window, in pixels
        unsigned int pitch;         // pixels a row of a buffer, which is the output's width
        unsigned int ground;        // the window's ground as the runner last saw it (the terminal draws its own paper)

        /*
                What each buffer shows, as the cells that made it, and the
                frame being made. Rows by COLUMNS cells each, so a resize
                makes them again.
        */
        struct window_cell *frame[3];
        struct window_cell *frames;
        unsigned long frame_bytes;
        unsigned int valid[2];
        int shown;                 // the buffer on the screen, -1 before the first picture
        unsigned int cursor_at;    // the frame's cell a block cursor turned round, or ~0
        b32 forced;                // a frame waited too long for the compositor: the buffer is taken
        unsigned int bar_first[2], bar_shown[2], bar_total[2], bar_drawn[2];
        // How much of the bar is seen, in 256ths: all of it while the view is off the live end or the bar is held,
        // and fading to none from when the view is back at the end.
        unsigned int bar_level;
        unsigned long bar_fading;

        // Where the view sits in the ring: a line and how many of its rows are above.
        unsigned int view, view_skip;

        // The pointer, in buffer pixels, and the button being held on the bar.
        int pointer_x, pointer_y;
        int bar_grab;
        unsigned int modifiers; // from the keys, which are the only events that say
        b32 pressed;
        int wheel_remainder;
        int wheel_notches; // the wheel in 120ths of a notch, while a program has the pointer
        b32 focused;
        int group;                      // the pty's foreground process group the title was taken from
        char named[WINDOW_TITLE_MAX];   // the last title a program set (OSC 0 or 2)
        unsigned int title_seen;        // how many titles programs had set when it was taken
        b32 hovering;                   // whether the compositor is told of the pointer's motion over it
        // When the far end's modes were last read, and whether they were read in this pass of the loop (before a key
        // is taken); when the title was last read, and whether a reading is owed at the frame's end.
        positive modes_at, title_at;
        b32 modes_fresh, title_owed;
        // Per buffer: the top margin was drawn over by what the compositor wrote, and is drawn again.
        b32 margin_spoiled[2];

        /*
                The loop, as the runner's hooks see it. dirty: a picture is due. changed: the cells changed since the
                frame last looked (a drain or the cells the pass touched); the frame's settle and the title read it.
                hold_timeout: how long a due frame is held, as the frame found it. The rest is what the loop was
                keeping in its locals: the pty and the far end's state, the timers, and the staying phase.
        */
        b32 dirty, changed, reading, gone, hung, keyed;
        int phase;
        bipolar child;
        b32 master;
        positive oom_kills, sync_since, stalled_since, last_frame, settle_since, idle_since;
        b32 sync_held;
        long hold_timeout;
} terminal = {.shown = -1, .view = TERMINAL_LIVE, .hold_timeout = -1};

static unsigned int terminal_colour[256];

static unsigned int terminal_palette(unsigned int index)
{
        return terminal_colour[index & 255];
}

/* ---- the view: which cells are on the screen ---- */

/*
        A stored line is as long as it was written and is folded when it is
        drawn, so a window made wider re-wraps everything in it. Everything
        below counts rows at the width the grid is laid out in.
*/
static unsigned int terminal_line_rows(unsigned int index)
{
        unsigned int length = *window_length(window, index);

        length = length < window->stride ? length : window->stride;
        return length ? (length + COLUMNS - 1) / COLUMNS : 1;
}

/*
        The oldest line the view may go back to: the screen and the scrollback
        the emulator counts behind it, and none behind the alternate screen,
        whose lines behind it are the primary screen's. Counting every line in
        the ring let the wheel scroll into the shell behind less or nano, and
        through the blank lines a clear had dropped.
*/
static unsigned int terminal_oldest(void)
{
        unsigned int filled = ROWS + (alternate ? 0 : history_lines);

        filled = filled > window->history ? window->history : filled;
        return window->head - filled;
}

/*
        The line the top row is on and how many rows of it are above the
        window. Following the end counts rows back from the newest line until
        the window is full, which can stop in the middle of a line too long to
        fit.
*/
static unsigned int terminal_top(unsigned int view, unsigned int address_to skip)
{
        unsigned int oldest = terminal_oldest();
        unsigned int at = window->head - 1;
        unsigned int count = 0;

        address_to skip = 0;

        if (view != TERMINAL_LIVE)
        {
                at = view < oldest ? oldest : view > window->head - 1 ? window->head - 1 : view;
                address_to skip = terminal.view_skip < terminal_line_rows(at) - 1
                                      ? terminal.view_skip
                                      : terminal_line_rows(at) - 1;
                return at;
        }

        while (count < ROWS && at > oldest)
        {
                count += terminal_line_rows(at);

                if (count >= ROWS)
                        break;

                at--;
        }

        if (count < ROWS)
                count += terminal_line_rows(at);

        address_to skip = count > ROWS ? count - ROWS : 0;
        return at;
}

/*
        Where the view sits in what there is, in rows rather than lines: what
        the bar is a picture of. False when there is nothing to scroll.

        The rows of the lines above the screen do not change, so they are kept as
        running sums: terminal_sums[i % TERMINAL_SUMS] is the rows of the lines
        from the base up to i, for i up to the end of the sums, and only the lines
        of the screen are counted each time. Walking every line from the oldest
        (terminal_extent_walk, which is also the fallback for a history too long
        for the sums) cost about ten instructions a line, every frame: about 9000
        a frame with a 900-line scrollback, from 3% to 14% of a frame while the
        view was back from the end. The sums are made again when the grid
        changes, or the oldest line or the screen moves back past them: a return
        from the alternate screen moves head back, and the lines it writes over
        were above the screen while the alternate one was up.
*/
#define TERMINAL_SUMS 4096

static unsigned int terminal_sums[TERMINAL_SUMS];
static unsigned int terminal_sums_base, terminal_sums_end;
static unsigned int terminal_sums_grid[4];
static b32 terminal_sums_valid;

static b32 terminal_extent_walk(unsigned int address_to first, unsigned int address_to shown,
                                unsigned int address_to total)
{
        unsigned int skip, top = terminal_top(terminal.view, &skip), at;

        address_to shown = ROWS;
        address_to total = 0;
        address_to first = 0;

        for (at = terminal_oldest(); at != window->head; at++)
        {
                if (at == top)
                        address_to first = address_to total + skip;

                address_to total += terminal_line_rows(at);
        }

        return address_to total > ROWS;
}

// The rows from the base to the line at, which is one the sums hold, or else one past their end counted directly.
static unsigned int terminal_sum_to(unsigned int at)
{
        unsigned int rows = 0;

        if (at <= terminal_sums_end)
                return terminal_sums[at % TERMINAL_SUMS];

        for (unsigned int line = terminal_sums_end; line != at; line++)
                rows += terminal_line_rows(line);
        return terminal_sums[terminal_sums_end % TERMINAL_SUMS] + rows;
}

static b32 terminal_extent(unsigned int address_to first, unsigned int address_to shown,
                           unsigned int address_to total)
{
        unsigned int oldest = terminal_oldest(), head = window->head, screen, skip, top, grid[4];
        unsigned int from;

        address_to shown = ROWS;
        address_to total = 0;
        address_to first = 0;

        if (head == oldest)
                return false;
        if (window->history + ROWS >= TERMINAL_SUMS)
                return terminal_extent_walk(first, shown, total);

        top = terminal_top(terminal.view, &skip);
        screen = head - oldest > ROWS ? head - ROWS : oldest;
        grid[0] = COLUMNS, grid[1] = ROWS, grid[2] = window->stride, grid[3] = window->history;

        if (!terminal_sums_valid || oldest < terminal_sums_base || oldest > terminal_sums_end || screen < terminal_sums_end ||
            memory_compare(grid, terminal_sums_grid, sizeof(grid)))
        {
                terminal_sums_base = terminal_sums_end = oldest;
                terminal_sums[oldest % TERMINAL_SUMS] = 0;
                memory_copy(terminal_sums_grid, grid, sizeof(grid));
                terminal_sums_valid = true;
        }

        for (; terminal_sums_end < screen; terminal_sums_end++)
                terminal_sums[(terminal_sums_end + 1) % TERMINAL_SUMS] =
                    terminal_sums[terminal_sums_end % TERMINAL_SUMS] + terminal_line_rows(terminal_sums_end);

        from = terminal_sums[oldest % TERMINAL_SUMS];
        address_to total = terminal_sum_to(head) - from;
        address_to first = terminal_sum_to(top) - from + skip;

        return address_to total > ROWS;
}

static b32 terminal_view_live(void)
{
        if (terminal.view == TERMINAL_LIVE)
                return false;

        terminal.view = TERMINAL_LIVE;
        terminal.view_skip = 0;
        return true;
}

// The view put where a count of rows above it says; false when it did not move.
static b32 terminal_view_set(unsigned int above)
{
        unsigned int was = terminal.view, was_skip = terminal.view_skip;
        unsigned int at, run = 0, live_skip, skip = 0;
        unsigned int live = terminal_top(TERMINAL_LIVE, &live_skip);

        for (at = terminal_oldest(); at != window->head; at++)
        {
                unsigned int past = run + terminal_line_rows(at);

                if (above < past)
                {
                        skip = above - run;
                        break;
                }

                run = past;
        }

        if (at > live || (at == live && skip >= live_skip))
        {
                terminal.view = TERMINAL_LIVE;
                terminal.view_skip = 0;
        }
        else
        {
                terminal.view = at;
                terminal.view_skip = skip;
        }

        return terminal.view != was || terminal.view_skip != was_skip;
}

// The wheel, in lines: positive is back through what has been said.
static b32 terminal_view_scroll(int lines)
{
        unsigned int above, shown, total, live;
        long target;

        terminal_extent(&above, &shown, &total);
        live = total > shown ? total - shown : 0;
        target = (long)above - lines;
        target = target < 0 ? 0 : (unsigned long)target > live ? (long)live : target;

        return terminal_view_set((unsigned int)target);
}

/*
        The view is never later in the ring than following the end would be.
        The end moves, and a window made taller or wider pulls the top of the
        last screenful earlier, so a view left where it was can be past it.
*/
static void terminal_view_clamp(void)
{
        unsigned int live_skip, live;

        if (terminal.view == TERMINAL_LIVE)
                return;

        live = terminal_top(TERMINAL_LIVE, &live_skip);

        if (terminal.view > live || (terminal.view == live && terminal.view_skip >= live_skip))
                terminal_view_live();
}

// The bar's track: the grid's height, beside it in the gutter.
static unsigned int terminal_track(void)
{
        return ROWS * terminal.cell_h;
}

// The thumb's height, never under a cell, and where its top is: the rows above the view over the rows it
// can start at, across the span the thumb travels.
static unsigned int terminal_thumb(unsigned int shown, unsigned int total)
{
        unsigned int thumb = (unsigned int)((unsigned long)terminal_track() * shown / total);

        return thumb < terminal.cell_h ? terminal.cell_h : thumb > terminal_track() ? terminal_track() : thumb;
}

static unsigned int terminal_thumb_top(unsigned int first, unsigned int shown, unsigned int total)
{
        unsigned int span = terminal_track() - terminal_thumb(shown, total);

        return total > shown ? (unsigned int)((unsigned long)span * first / (total - shown)) : 0;
}

// The bar put under the hand: where the pointer is in it, as a place in the rows.
static b32 terminal_bar_move(int y)
{
        unsigned int first, shown, total;
        unsigned int span, thumb, track = terminal_track();
        int top;

        if (!terminal_extent(&first, &shown, &total) || !track)
                return false;

        thumb = terminal_thumb(shown, total);
        span = track > thumb ? track - thumb : 0;
        top = y - terminal.bar_grab;
        top = top < 0 ? 0 : (unsigned int)top > span ? (int)span : top;

        // The thumb's top over the span it travels is the view over the rows it can start at, so the
        // bottom of the track is the live end even when the thumb was made taller than its share.
        return terminal_view_set(span ? (unsigned int)((unsigned long)top * (total - shown) / span) : 0);
}

/* ---- the frame: what is on the screen, as cells ---- */

/*
        The cells that are on the screen, row by row at COLUMNS wide, as the
        view has them: a folded line is as many rows as it needs, and what is
        past the end of a row, and below the newest line, is a blank.
*/
static void terminal_build(struct window_cell address_to into)
{
        unsigned int skip, line = terminal_top(terminal.view, &skip);
        unsigned int row = 0;
        const struct window_cell address_to block = shown && shown_block ? row_cells(shown_row) + shown_column : null;

        terminal.cursor_at = ~0u;

        while (row < ROWS && line != window->head)
        {
                unsigned int slot = line % window->history;
                unsigned int length = window_lengths(window)[slot];
                struct window_cell address_to cells = window_cells(window) + (unsigned long)slot * window->stride;
                unsigned int folds;

                length = length < window->stride ? length : window->stride;
                folds = length ? (length + COLUMNS - 1) / COLUMNS : 1;

                for (unsigned int fold = skip; fold < folds && row < ROWS; fold++, row++)
                {
                        unsigned int from = fold * COLUMNS;
                        unsigned int used = length > from ? length - from : 0;
                        struct window_cell address_to out = into + (unsigned long)row * COLUMNS;

                        used = used < COLUMNS ? used : COLUMNS;
                        memory_copy(out, cells + from, (positive)used * sizeof(struct window_cell));
                        if (block >= cells + from && block < cells + from + used)
                                terminal.cursor_at = row * COLUMNS + (unsigned int)(block - (cells + from));
                        memory_fill_u64_aligned(out + used, COLUMNS - used, BLANK_CELL_WORD);
                }

                skip = 0;
                line++;
        }

        if (row < ROWS)
                memory_fill_u64_aligned(into + (unsigned long)row * COLUMNS, (ROWS - row) * COLUMNS,
                                        BLANK_CELL_WORD);
}

/*
        The columns of a row that differ: the first and one past the last, or
        both zero when they are the same. A wide character is two cells that
        change together, so a span never starts on the second.
*/
static void terminal_span(const struct window_cell address_to now, const struct window_cell address_to old,
                          unsigned int address_to first, unsigned int address_to last)
{
        unsigned int start = (unsigned int)(memory_common_prefix((address_any)now, (address_any)old,
                                                                (positive)COLUMNS * sizeof(*now)) /
                                            sizeof(*now));
        unsigned int stop = COLUMNS;

        if (start == COLUMNS)
        {
                address_to first = address_to last = 0;
                return;
        }

        while (*(const terminal_word address_to)(now + stop - 1) == *(const terminal_word address_to)(old + stop - 1))
                stop--;

        if (start && (now[start].flags & WINDOW_CELL_WIDE_RIGHT))
                start--;

        if (stop < COLUMNS && (now[stop - 1].flags & WINDOW_CELL_WIDE))
                stop++;

        address_to first = start;
        address_to last = stop;
}

/* ---- the pixels ---- */

static unsigned int terminal_mix(unsigned int ink, unsigned int paper)
{
        return ((ink & 0xfefefe) >> 1) + ((paper & 0xfefefe) >> 1);
}

/*
        A rectangle of the window in one colour, clipped to it: the part of
        a cell a style paints over its glyph, or the ground of a run of blanks.
*/
static void terminal_fill(unsigned int address_to pixels, int x, int y, unsigned int width,
                          unsigned int height, unsigned int colour)
{
        unsigned int x2 = x + width > terminal.width ? terminal.width : x + width;
        unsigned int y2 = y + height > terminal.height ? terminal.height : y + height;

        if ((unsigned int)x >= x2 || (unsigned int)y >= y2)
                return;

        memory_fill_u32_rect(pixels + (positive)y * terminal.pitch + x, terminal.pitch,
                             x2 - x, y2 - y, colour);
}

// What a style paints over the cell: an underline, a strike and the bar.
static void terminal_decorate(unsigned int address_to pixels, const struct window_cell address_to cell,
                              int x, int y, unsigned int cells, unsigned int ink)
{
        unsigned int width = terminal.cell_w * cells;
        unsigned int thick = (unsigned int)((terminal.grid.thickness + 0x8000) >> 16);
        int rule;

        thick = thick ? thick : terminal.cell_h / 16 ? terminal.cell_h / 16 : 1;

        if (cell->flags & WINDOW_CELL_UNDERLINE)
        {
                rule = terminal.grid.underline
                           ? terminal.grid.baseline - (int)((terminal.grid.underline + 0x8000) >> 16)
                           : terminal.grid.baseline + (int)thick;
                rule = rule + (int)thick > (int)terminal.cell_h ? (int)(terminal.cell_h - thick) : rule;
                terminal_fill(pixels, x, y + rule, width, thick, ink);
        }

        if (cell->flags & WINDOW_CELL_STRIKE)
        {
                rule = terminal.grid.baseline - (int)((terminal.grid.x_height / 2 + 0x8000) >> 16);
                terminal_fill(pixels, x, y + rule, width, thick, ink);
        }

        if (cell->flags & WINDOW_CELL_BAR)
                terminal_fill(pixels, x, y, terminal.cell_w / 8 ? terminal.cell_w / 8 : 1,
                              terminal.cell_h, ink);
}

/*
        The cells of one row from first to last that differ from what the
        buffer holds, or all of them where it holds nothing yet. Neighbouring
        cells with glyphs on one pair of colours and one face are one
        font_cells, which lays their coverage side by side and makes it
        pixels in one pass, rows as wide as the run; a cell with a line under
        or through it, or a wide one, is its own font_cell. A blank between
        two glyphs of a run, on its ground, is a cell of the run; a run of
        blanks anywhere else on one ground is one fill.
*/
/*
        What draws a band of rows: a font engine and the run of codes it lays.
        A frame whose cells changed over many rows is drawn in bands on as many
        threads, and an engine's caches and atlas are written as it draws, so
        each band has its own; the faces are the terminal's, read and never
        written. The first is the terminal's own engine, the rest made the first
        time a frame asks for them.
*/
#define TERMINAL_BANDS 8

struct terminal_painter
{
        struct ui ui;     // only its engine is used: the painter's own atlas, or the terminal's for the first band
        p32 codes[FONT_RUN];
};

static struct terminal_painter terminal_painters[TERMINAL_BANDS];

//      Italic is a slant of the plain face, and bold without a bold face is
//      the plain face made heavier: neither has a face of its own here.
static p32 terminal_style(const struct window_cell address_to cell)
{
        return (cell->flags & WINDOW_CELL_ITALIC ? FONT_OBLIQUE : 0) |
               ((cell->flags & WINDOW_CELL_BOLD) && !terminal.bold_count ? FONT_EMBOLDEN : 0);
}
// A run of blank cells on one ground, from the first cell of the run for its length.
static void terminal_blanks(unsigned int address_to pixels, unsigned int from, unsigned int count, int y,
                            unsigned int colour)
{
        terminal_fill(pixels, (int)(from * terminal.cell_w), y, count * terminal.cell_w, terminal.cell_h, colour);
}

static void terminal_row(struct terminal_painter address_to painter, unsigned int address_to pixels, unsigned int row,
                         const struct window_cell address_to now,
                         const struct window_cell address_to old, unsigned int first,
                         unsigned int last)
{
        font_target target = {pixels, terminal.pitch, (b32)terminal.width, (b32)terminal.height};
        int y = (int)(row * terminal.cell_h);
        unsigned int run = 0, run_from = 0, run_colour = 0;
        unsigned int glyphs = 0, glyphs_from = 0, glyphs_ink = 0, glyphs_paper = 0;
        b32 glyphs_heavy = 0;
        p32 glyphs_flags = 0;

// The run, less the blanks it ends with, which are a fill: a line's trailing blanks would otherwise be
// made pixels a coverage byte at a time.
#define TERMINAL_GLYPHS()                                                                              \
        do                                                                                             \
        {                                                                                              \
                unsigned int trail = 0;                                                                \
                while (trail < glyphs && painter->codes[glyphs - 1 - trail] == ' ')                    \
                        trail++;                                                                       \
                if (glyphs > trail)                                                                    \
                        font_cells(painter->ui.engine, glyphs_heavy ? terminal.bold : terminal.plain,     \
                                   glyphs_heavy ? terminal.bold_count : terminal.plain_count,          \
                                   painter->codes, glyphs - trail, &terminal.grid, terminal.size,      \
                                   glyphs_flags, &target, (b32)(glyphs_from * terminal.cell_w), y,     \
                                   glyphs_ink, glyphs_paper);                                          \
                if (trail)                                                                             \
                        terminal_blanks(pixels, glyphs_from + glyphs - trail, trail, y, glyphs_paper); \
                glyphs = 0;                                                                            \
        } while (0)

        for (unsigned int at = first; at < last; at++)
        {
                const struct window_cell address_to cell = now + at;
                unsigned int ink = terminal_colour[cell->ink];
                unsigned int paper = terminal_colour[cell->paper];
                // A wide character is both its cells; one cut by the edge of a row, or whose
                // other half is on another row of a folded line, is blank here.
                b32 pair = (cell->flags & WINDOW_CELL_WIDE) && at + 1 < COLUMNS &&
                           (cell[1].flags & WINDOW_CELL_WIDE_RIGHT);
                unsigned int cells = pair ? 2 : 1;
                b32 glyph = cell->character > ' ' && !(cell->flags & WINDOW_CELL_HIDDEN) &&
                            !(cell->flags & (WINDOW_CELL_WIDE | WINDOW_CELL_WIDE_RIGHT) && !pair);
                b32 decor = (cell->flags & (WINDOW_CELL_UNDERLINE | WINDOW_CELL_STRIKE | WINDOW_CELL_BAR)) != 0;

                if (old && *(const terminal_word address_to)cell == *(const terminal_word address_to)(old + at) &&
                    (cells == 1 ||
                     *(const terminal_word address_to)(cell + 1) == *(const terminal_word address_to)(old + at + 1)))
                {
                        at += cells - 1;
                        glyph = decor = false;
                        cells = 0;
                }

                if (glyph && cells == 1 && !decor)
                {
                        b32 heavy = (cell->flags & WINDOW_CELL_BOLD) && terminal.bold_count;
                        p32 style = terminal_style(cell);

                        if (run)
                                terminal_blanks(pixels, run_from, run, y, run_colour);
                        run = 0;
                        if (cell->flags & WINDOW_CELL_DIM)
                                ink = terminal_mix(ink, paper);
                        if (glyphs && (glyphs_from + glyphs != at || glyphs_ink != ink || glyphs_paper != paper ||
                                       glyphs_heavy != heavy || glyphs_flags != style || glyphs == FONT_RUN))
                                TERMINAL_GLYPHS();
                        if (!glyphs)
                        {
                                glyphs_from = at;
                                glyphs_ink = ink;
                                glyphs_paper = paper;
                                glyphs_heavy = heavy;
                                glyphs_flags = style;
                        }
                        painter->codes[glyphs++] = cell->character;
                        continue;
                }
                // A blank between glyphs on the run's ground is a cell of the run: a space no
                // longer ends it, so a line of words in one colour is one font_cells and not one
                // a word, each with its own setup and its own narrow rows to make pixels.
                if (glyphs && cells == 1 && !glyph && !decor && paper == glyphs_paper && glyphs_from + glyphs == at &&
                    glyphs < FONT_RUN)
                {
                        painter->codes[glyphs++] = ' ';
                        continue;
                }
                TERMINAL_GLYPHS();

                if (cells && !glyph && !decor)
                {
                        // Blanks on the same ground are one rectangle.
                        if (run && run_colour == paper && run_from + run == at)
                                run++;
                        else
                        {
                                if (run)
                                        terminal_blanks(pixels, run_from, run, y, run_colour);
                                run = 1;
                                run_from = at;
                                run_colour = paper;
                        }
                        continue;
                }

                if (run)
                        terminal_blanks(pixels, run_from, run, y, run_colour);
                run = 0;

                if (!cells)
                        continue;

                if (cell->flags & WINDOW_CELL_DIM)
                        ink = terminal_mix(ink, paper);

                if (glyph)
                {
                        b32 heavy = (cell->flags & WINDOW_CELL_BOLD) && terminal.bold_count;

                        font_cell(painter->ui.engine, heavy ? terminal.bold : terminal.plain,
                                  heavy ? terminal.bold_count : terminal.plain_count, cell->character,
                                  cells, &terminal.grid, terminal.size, terminal_style(cell), &target,
                                  (b32)(at * terminal.cell_w), y, ink, paper);
                }
                else
                        terminal_fill(pixels, (int)(at * terminal.cell_w), y, cells * terminal.cell_w,
                                      terminal.cell_h, paper);

                if (decor)
                        terminal_decorate(pixels, cell, (int)(at * terminal.cell_w), y, cells, ink);

                at += cells - 1;
        }

        TERMINAL_GLYPHS();
        if (run)
                terminal_blanks(pixels, run_from, run, y, run_colour);
#undef TERMINAL_GLYPHS
}

/*
        The paper round the grid is the ground of the cells at its edge, as if
        they were stretched to the window's edges: a program that paints its
        screen one colour fills the window with it. A block cursor's cell
        stands on its own paper, not the cursor's. A row's band runs from the
        window's top for the first row and to its bottom for the last.
*/
static unsigned int terminal_ground(const struct window_cell address_to frame, unsigned int row, unsigned int column)
{
        unsigned int at = row * COLUMNS + column;

        return terminal_colour[at == terminal.cursor_at ? frame[at].ink : frame[at].paper];
}

static void terminal_band(unsigned int row, unsigned int address_to top, unsigned int address_to height)
{
        unsigned int bottom = row + 1 < ROWS ? terminal.pad_y + (row + 1) * terminal.cell_h : terminal.height;

        address_to top = row ? terminal.pad_y + row * terminal.cell_h : 0;
        address_to height = bottom > address_to top ? bottom - address_to top : 0;
}

// The paper at the left of a row, from its first cell, and above or below the columns first to last of the
// first and last rows; the right is the bar's.
static void terminal_margin(unsigned int address_to whole, const struct window_cell address_to frame,
                            unsigned int row, unsigned int first, unsigned int last)
{
        unsigned int top, height, below = terminal.pad_y + ROWS * terminal.cell_h;

        terminal_band(row, &top, &height);
        if (!first)
                terminal_fill(whole, 0, (int)top, terminal.pad, height, terminal_ground(frame, row, 0));

        for (unsigned int column = first; column < last; column++)
        {
                int x = (int)(terminal.pad + column * terminal.cell_w);

                if (!row)
                        terminal_fill(whole, x, 0, terminal.cell_w, terminal.pad_y, terminal_ground(frame, 0, column));
                if (row + 1 == ROWS && terminal.height > below)
                        terminal_fill(whole, x, (int)below, terminal.cell_w, terminal.height - below,
                                      terminal_ground(frame, row, column));
        }
}

/*
        The coverage of the pill's pixels: how many of each pixel's sixteen sample points are inside its round ends. It
        depends on the pill's size and nothing else, and the thumb moves a line at a time, so its size changes about
        once in every few lines; the table is worked out again only when the size changes. Sampling every pixel of the
        pill at each redraw was most of the bar's cost (about 75000 instructions a frame at 680 pixels).
*/
#define TERMINAL_BAR_COVER 32768
static unsigned char terminal_bar_cover[TERMINAL_BAR_COVER];
static int terminal_bar_across = -1, terminal_bar_along = -1;

// How many of the sixteen sample points of one pixel of the scroll bar's pill are inside its round ends (see terminal_bar).
static unsigned int terminal_bar_inside(int column, int row, int across, int along, int radius)
{
        unsigned int inside = 0;

        for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                        int px = 2 * (column * 4 + sx) + 1, py = 2 * (row * 4 + sy) + 1;
                        int dx = px - across;
                        int dy = py < 2 * radius ? py - 2 * radius
                                 : py > 2 * (along - radius) ? py - 2 * (along - radius) : 0;

                        inside += dx * dx + dy * dy <= across * across;
                }
        return inside;
}

/*
        The right of the grid: each row's paper, from its last cell, to the
        window's right edge, and the scroll bar's thumb a pill on it, as wide
        as half the gutter, round at both ends, a quarter of the gutter in
        from every side of the place the thumb has, in a colour between the
        paper and the ink that stands out on the row's paper (window_contrast),
        so it is seen on any ground a program paints. Where a pixel is part of
        a round end it is the share of its sixteen points inside the pill,
        mixed with that ground, which is known: nothing is read back. No
        track, and no pill when everything fits or the view is at the live end
        and the bar has faded: it is seen only while the view is scrolled back.
*/
static void terminal_bar(unsigned int address_to whole, const struct window_cell address_to frame, unsigned int first,
                         unsigned int shown, unsigned int total)
{
        unsigned int x = terminal.pad + COLUMNS * terminal.cell_w, track = terminal_track();
        unsigned int thumb, at, gutter = terminal.bar, inset, wide, top, height;

        for (unsigned int row = 0; row < ROWS; row++)
        {
                terminal_band(row, &top, &height);
                terminal_fill(whole, (int)x, (int)top, terminal.width > x ? terminal.width - x : 0, height,
                              terminal_ground(frame, row, COLUMNS - 1));
        }

        if (!gutter || x + gutter > terminal.width || total <= shown || !terminal.bar_level)
                return;

        // The thumb and its top are the ones the pointer is tested against (terminal_thumb, terminal_thumb_top): the
        // two must agree when the thumb is made taller than its share, or a press on the thumb is not on it.
        thumb = terminal_thumb(shown, total);
        at = terminal_thumb_top(first, shown, total);
        at = at + thumb > track ? track - thumb : at;

        inset = gutter / 4 ? gutter / 4 : 1;
        wide = gutter > 2 * inset ? gutter - 2 * inset : 1;
        if (thumb <= 2 * inset + wide)
                return;

        // In eighths of a pixel from the pill's top left (sample centres are odd): its ends are half
        // circles of radius wide / 2.
        int across = (int)wide * 4, along = (int)(thumb - 2 * inset) * 4, radius = across / 2;
        unsigned int left = x + inset, pill = terminal.pad_y + at + inset;
        int columns = across / 4, rows = along / 4;
        b32 kept = (unsigned long)columns * (unsigned long)rows <= TERMINAL_BAR_COVER;

        if (kept && (across != terminal_bar_across || along != terminal_bar_along))
        {
                for (int row = 0; row < rows; row++)
                        for (int column = 0; column < columns; column++)
                                terminal_bar_cover[row * columns + column] =
                                    (unsigned char)terminal_bar_inside(column, row, across, along, radius);
                terminal_bar_across = across;
                terminal_bar_along = along;
        }

        unsigned int ink = 0, inked_ground = 0;
        b32 inked = false;

        for (int row = 0; row < rows; row++)
        {
                unsigned int y = pill + (unsigned int)row, cell_row = (y - terminal.pad_y) / terminal.cell_h;
                unsigned int address_to line = whole + (unsigned long)y * terminal.pitch + left;
                unsigned int ground = terminal_ground(frame, cell_row < ROWS ? cell_row : ROWS - 1, COLUMNS - 1);

                // The ink is a function of the ground and the level alone, and most rows have the same ground: it is
                // worked out again only when the ground changes.
                if (!inked || ground != inked_ground)
                {
                        ink = window_mix(ground, window_contrast(ground), 160 * terminal.bar_level / 256);
                        inked_ground = ground;
                        inked = true;
                }

                for (int column = 0; column < columns; column++)
                {
                        unsigned int inside = kept ? terminal_bar_cover[row * columns + column]
                                                   : terminal_bar_inside(column, row, across, along, radius);

                        if (inside == 16)
                                line[column] = ink;
                        else if (inside)
                        {
                                unsigned int mixed = 0;

                                for (unsigned int shift = 0; shift < 24; shift += 8)
                                        mixed |= (((ground >> shift & 255) * (16 - inside) + (ink >> shift & 255) * inside + 8) / 16)
                                                 << shift;
                                line[column] = mixed;
                        }
                }
        }
}

// The grid's top left in a buffer, which the cells and the bar are drawn from.
static unsigned int address_to terminal_grid(unsigned int address_to whole)
{
        return whole + (unsigned long)terminal.pad_y * terminal.pitch + terminal.pad;
}

/* ---- the buffers ---- */

/*
        The buffer the next frame goes into. The compositor shows the one it
        took last, and reads only that one: once it has taken the last picture
        that is the one drawn last, and the frame goes into the other. While
        it has not, the one drawn last is the one it has not taken, so a frame
        forced past the wait goes into that one and never into the picture the
        compositor shows. Before the first picture, either.
*/
static int terminal_next(void)
{
        if (terminal.shown < 0)
                return 0;
        if (windows_ready(&terminal.own))
                return 1 - terminal.shown;
        return terminal.forced ? terminal.shown : -1;
}

// What is held for the next frame's pictures, cut for the grid as it is now.
static b32 terminal_frames(void)
{
        unsigned long bytes = (unsigned long)ROWS * COLUMNS * sizeof(struct window_cell);
        long mapped;

        if (terminal.frames)
                window_call(WINDOW_SYS_MUNMAP, (long)terminal.frames, 3 * (long)terminal.frame_bytes, 0, 0, 0, 0);

        // Until a mapping is made there are no pictures: the pointers are none, so nothing is drawn through them.
        terminal.frames = 0;
        terminal.frame[0] = terminal.frame[1] = terminal.frame[2] = null;
        terminal.valid[0] = terminal.valid[1] = 0;

        bytes = (bytes + 4095) & ~4095ul;
        // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
        mapped = window_call(WINDOW_SYS_MMAP, 0, 3 * (long)bytes, 3, 0x22, -1, 0);

        if (mapped < 0 && mapped > -4096)
                return false;

        terminal.frames = (struct window_cell *)mapped;
        terminal.frame_bytes = bytes;
        for (unsigned int at = 0; at < 3; at++)
                terminal.frame[at] = (struct window_cell *)(mapped + (long)bytes * at);
        return true;
}

/*
        The buffer that is not on the screen, given back to the system: it
        costs a screenful of pixels and is only wanted for the next frame,
        which draws it whole again. The page is punched out of the shared
        file, so the compositor, which maps the same file and is not reading
        this slot, loses nothing.
*/
static void terminal_discard(void)
{
        int slot = terminal.shown < 0 ? -1 : 1 - terminal.shown;

        if (slot < 0 || !terminal.valid[slot] || !windows_ready(&terminal.own))
                return;

        windows_discard(&terminal.own, (unsigned int)slot);
        terminal.valid[slot] = 0;
}

/*
        What the compositor wrote over a buffer it showed straight from where
        it is (the tabs, over a window that fills the output): every row of
        cells the rectangle reaches is no longer what the buffer holds, so the
        frame draws it again, its margins and the bar with it.
*/
static void terminal_spoiled(struct window_cell address_to drawn, int slot)
{
        int x, y, width, height;

        if (!windows_spoiled(&terminal.own, (unsigned int)slot, &x, &y, &width, &height))
                return;

        int first = (y - (int)terminal.pad_y) / (int)terminal.cell_h;
        int last = (y + height - (int)terminal.pad_y + (int)terminal.cell_h - 1) / (int)terminal.cell_h;

        first = first < 0 ? 0 : first;
        last = last > (int)ROWS ? (int)ROWS : last;
        // Below the grid is the last row's paper, which that row draws.
        if (y + height > (int)(terminal.pad_y + ROWS * terminal.cell_h))
                last = (int)ROWS, first = first < (int)ROWS - 1 ? first : (int)ROWS - 1;
        if (last > first)
                memory_fill_u64_aligned(drawn + (positive)first * COLUMNS, (positive)(last - first) * COLUMNS, ~0ull);
        // A rectangle that marked no row lies in the paper above the grid, which the top row would have drawn with its
        // own cells. Nothing draws that paper, so the frame draws it again. A rectangle that does mark a row has the
        // top row drawn over its whole width, and the paper with it.
        else if (y < (int)terminal.pad_y)
                terminal.margin_spoiled[slot] = true;
        terminal.bar_drawn[slot] = ~0u;
        (void)x, (void)width;
}

/*
        The cells of a frame that differ from what the buffer holds, by row:
        found first, in one pass, and drawn after -- here when they are few, and
        when they are many (a screen that scrolled is all of them) in bands of
        rows on the pool's threads, each band with a painter of its own.
*/
#define TERMINAL_SPAN_ROWS 4096

static unsigned short terminal_span_first[TERMINAL_SPAN_ROWS], terminal_span_last[TERMINAL_SPAN_ROWS];

struct terminal_banding
{
        unsigned int address_to pixels, address_to whole;
        const struct window_cell address_to now, address_to drawn;
        unsigned int rows, bands;
};

static void terminal_rows_draw(struct terminal_painter address_to painter, const struct terminal_banding address_to banding,
                               unsigned int from, unsigned int to)
{
        for (unsigned int row = from; row < to; row++)
        {
                unsigned int a0 = terminal_span_first[row], a1 = terminal_span_last[row];

                if (a1 <= a0)
                        continue;
                terminal_row(painter, banding->pixels, row, banding->now + (unsigned long)row * COLUMNS,
                             banding->drawn ? banding->drawn + (unsigned long)row * COLUMNS : null, a0, a1);
                terminal_margin(banding->whole, banding->now, row, a0, a1);
        }
}

static void terminal_band_draw(address_any context, positive index)
{
        const struct terminal_banding address_to banding = (const struct terminal_banding address_to)context;

        terminal_rows_draw(&terminal_painters[index], banding, (unsigned int)(banding->rows * index / banding->bands),
                           (unsigned int)(banding->rows * (index + 1) / banding->bands));
}

// The painters a frame of so many bands draws with, each an engine of its own with its own atlas; false when
// one cannot be made, and the frame is drawn in fewer.
static unsigned int terminal_painters_ready(unsigned int bands)
{
        for (unsigned int at = 1; at < bands; at++)
        {
                if (terminal_painters[at].ui.engine)
                        continue;
                if (!ui_engine_open_sized(&terminal_painters[at].ui, TERMINAL_ATLAS))
                        return at;
        }
        return bands;
}

static void terminal_cells_draw(struct terminal_banding address_to banding, positive changed)
{
        positive bytes = changed * terminal.cell_w * terminal.cell_h * 4, bands = parallel_width();

        bands = bands < TERMINAL_BANDS ? bands : TERMINAL_BANDS;
        bands = bands < banding->rows ? bands : banding->rows;
        if (bytes >= PARALLEL_MINIMUM_BYTES && bands >= 2)
                bands = terminal_painters_ready((unsigned int)bands);
        if (bytes < PARALLEL_MINIMUM_BYTES || bands < 2)
        {
                terminal_rows_draw(&terminal_painters[0], banding, 0, banding->rows);
                return;
        }
        banding->bands = (unsigned int)bands;
        parallel_for(terminal_band_draw, banding, bands, bytes);
}

/*
        One frame: the cells that differ drawn into the free buffer, what
        the compositor has to read again named, and the picture shown.
        False when there is no buffer to draw into yet, and nothing was done.
*/
static b32 terminal_frame(void)
{
        int slot = terminal_next();
        int front;
        unsigned int address_to pixels, address_to whole;
        struct windows address_to table = terminal.own.table;
        unsigned int own = terminal.own.row;
        struct window_cell address_to now = terminal.frame[2];
        struct window_cell address_to drawn;
        struct window_cell address_to screen;
        unsigned int first, shown, total;
        unsigned int row_x0[1024], row_x1[1024];
        int x1 = 0x7fffffff, y1 = 0x7fffffff, x2 = 0, y2 = 0;
        b32 bar, bar_told, any, right = false;

        if (slot < 0)
                return false;

        // A mapping the last grid's pictures were cut for is made again first, and a frame that cannot be
        // drawn into its pictures is refused.
        if (!terminal.frames && !terminal_frames())
                return false;

        // The picture the compositor shows is the one it took last: the one drawn last once it has taken
        // it, the other while it has not. Nothing is measured against a picture it never took.
        front = windows_ready(&terminal.own) ? terminal.shown : terminal.shown < 0 ? -1 : 1 - terminal.shown;
        screen = front >= 0 && terminal.valid[front] ? terminal.frame[front] : null;

        terminal_view_clamp();
        terminal_build(now);
        terminal_extent(&first, &shown, &total);
        pixels = windows_buffer(table, own, slot);
        drawn = terminal.valid[slot] ? terminal.frame[slot] : null;
        if (drawn)
                terminal_spoiled(drawn, slot);

        // The cells are drawn through the grid's own top left, the paper round them through the buffer's.
        whole = pixels;
        pixels = terminal_grid(whole);

        // The bar is drawn where the buffer's own was not the same, and the compositor is
        // told where the one on the screen was not.
        bar = !terminal.valid[slot] || terminal.bar_first[slot] != first || terminal.bar_shown[slot] != shown ||
              terminal.bar_total[slot] != total || terminal.bar_drawn[slot] != terminal.bar_level;
        bar_told = !screen || terminal.bar_first[front] != first || terminal.bar_shown[front] != shown ||
                   terminal.bar_total[front] != total || terminal.bar_drawn[front] != terminal.bar_level;

        // A top margin a spoil reached is drawn again, which makes the frame whether or not a cell changed.
        b32 margin = terminal.margin_spoiled[slot];

        any = bar || bar_told || margin;

        positive changed = 0;
        unsigned int rows = ROWS < TERMINAL_SPAN_ROWS ? ROWS : TERMINAL_SPAN_ROWS;

        for (unsigned int row = 0; row < rows; row++)
        {
                const struct window_cell address_to line = now + (unsigned long)row * COLUMNS;
                unsigned int a0 = 0, a1 = COLUMNS, b0 = 0, b1 = COLUMNS;

                if (drawn)
                        terminal_span(line, drawn + (unsigned long)row * COLUMNS, &a0, &a1);
                if (screen)
                        terminal_span(line, screen + (unsigned long)row * COLUMNS, &b0, &b1);

                terminal_span_first[row] = (unsigned short)a0;
                terminal_span_last[row] = (unsigned short)(a1 > a0 ? a1 : a0);
                if (a1 > a0)
                {
                        changed += a1 - a0;
                        right |= a1 == COLUMNS;
                }
                any |= a1 > a0 || b1 > b0;

                // What the compositor reads again is what differs from what it shows: a cell
                // that is the same as on the screen is the same pixels in this buffer too,
                // however old the buffer was.
                row_x0[row & 1023] = b1 > b0 ? b0 : 0;
                row_x1[row & 1023] = b1 > b0 ? b1 : 0;
        }

        if (changed)
        {
                struct terminal_banding banding = {pixels, whole, now, drawn, rows, 1};

                terminal_cells_draw(&banding, changed);
        }

        if (margin)
        {
                terminal_margin(whole, now, 0, 0, COLUMNS);
                terminal.margin_spoiled[slot] = false;
        }

        // Nothing differs from the buffer or from the screen: nothing is made or said.
        if (!any)
        {
                terminal.forced = false;
                return true;
        }

        // A row's last cell is the paper right of it, which the bar is drawn on.
        if (bar || right)
        {
                terminal_bar(whole, now, first, shown, total);
                terminal.bar_first[slot] = first;
                terminal.bar_shown[slot] = shown;
                terminal.bar_total[slot] = total;
                terminal.bar_drawn[slot] = terminal.bar_level;
        }

        // What the compositor reads again: one rectangle round the rows and the bar that differ from what
        // it shows. With nothing on the screen to differ from, all of it (a width of 0); with nothing
        // differing, none (a height of 0).
        if (screen && ROWS <= 1024)
        {
                for (unsigned int row = 0; row < ROWS; row++)
                        if (row_x1[row] > row_x0[row])
                        {
                                // A row's edge cells are the paper beyond them too.
                                unsigned int top, height;
                                int left = row_x0[row] ? (int)(terminal.pad + row_x0[row] * terminal.cell_w) : 0;
                                int edge = row_x1[row] < COLUMNS ? (int)(terminal.pad + row_x1[row] * terminal.cell_w)
                                                                 : (int)terminal.width;

                                terminal_band(row, &top, &height);
                                x1 = left < x1 ? left : x1;
                                x2 = edge > x2 ? edge : x2;
                                y1 = (int)top < y1 ? (int)top : y1;
                                y2 = (int)(top + height) > y2 ? (int)(top + height) : y2;
                        }

                // The top margin drawn again is the window's top edge across, to the grid's first row.
                if (margin)
                {
                        x1 = x1 < 0 ? x1 : 0;
                        y1 = y1 < 0 ? y1 : 0;
                        x2 = x2 > (int)terminal.width ? x2 : (int)terminal.width;
                        y2 = y2 > (int)terminal.pad_y ? y2 : (int)terminal.pad_y;
                }

                if (bar_told && terminal.bar)
                {
                        int left = (int)(terminal.pad + COLUMNS * terminal.cell_w), top = (int)terminal.pad_y;

                        x1 = left < x1 ? left : x1;
                        x2 = left + (int)terminal.bar > x2 ? left + (int)terminal.bar : x2;
                        y1 = top < y1 ? top : y1;
                        y2 = top + (int)terminal_track() > y2 ? top + (int)terminal_track() : y2;
                }

                table->damage_x[own] = x2 > x1 ? x1 : 0;
                table->damage_y[own] = x2 > x1 ? y1 : 0;
                table->damage_width[own] = x2 > x1 ? x2 - x1 : 1;
                table->damage_height[own] = x2 > x1 ? y2 - y1 : 0;
        }
        else
                table->damage_width[own] = 0;

        // The picture just made is what this buffer holds; the frame it was made from is
        // kept as that picture, and the one it replaces is the next to be made.
        terminal.frame[2] = terminal.frame[slot];
        terminal.frame[slot] = now;
        terminal.valid[slot] = 1;

        table->width[own] = terminal.width;
        table->height[own] = terminal.height;
        windows_show(&terminal.own, (unsigned int)slot);
        terminal.forced = false;
        terminal.shown = slot;
        return true;
}

/* ---- the program ---- */

// The paper round the grid, for the cell the font gives: a third of a cell's height on every side.
static void terminal_margins(void)
{
        unsigned int margin = terminal.cell_h / 3 ? terminal.cell_h / 3 : 1;

        terminal.margin = margin;
        terminal.pad = margin;
        terminal.bar = margin;
        terminal.pad_y = margin;
}

static void terminal_said(b32 reading, string_address what);

/*
        The faces, loaded from the files named. A font is the caller's bytes
        mapped for good; the engine and its atlas are mappings of their own,
        and only the pages a program draws with are ever resident.
*/
static b32 terminal_fonts(string_address address_to paths, unsigned int count, string_address bold,
                          unsigned int size)
{
        unsigned int faces = 0;

        if (!ui_engine_open_sized(&terminal.ui, TERMINAL_ATLAS))
                return false;

        for (unsigned int at = 0; at <= count && faces < TERMINAL_FACE_SLOTS; at++)
        {
                string_address path = at < count ? paths[at] : bold;

                if (!path)
                        continue;

                // Said where the machine's log is, with the file: this program's own output goes nowhere.
                if (!ui_face_load(&terminal.face[faces], path))
                {
                        static p8 said[128] = " cannot use the font ";
                        positive at = 21;

                        for (positive from = 0; path[from] && at < sizeof(said) - 1; from++)
                                said[at++] = (p8)path[from];
                        said[at] = 0;
                        terminal_said(false, (string_address)said);
                        return false;
                }

                if (at < count)
                        terminal.plain[terminal.plain_count++] = &terminal.face[faces];
                else
                        terminal.bold[terminal.bold_count++] = &terminal.face[faces];
                faces++;
        }

        if (!terminal.plain_count)
                return false;

        // Bold looks in its own face first and in the plain ones after, for what it lacks.
        if (terminal.bold_count)
                for (unsigned int at = 0; at < terminal.plain_count && terminal.bold_count < TERMINAL_FACES; at++)
                        terminal.bold[terminal.bold_count++] = terminal.plain[at];

        terminal_painters[0].ui.engine = terminal.ui.engine;
        terminal.size = size << 6;
        terminal.grid = font_metrics_at(terminal.plain[0], terminal.size);
        terminal.cell_w = (unsigned int)terminal.grid.cell_width;
        terminal.cell_h = (unsigned int)terminal.grid.cell_height;
        terminal_margins();
        return true;
}

// The window size a grid needs, the paper round it included.
static unsigned int terminal_pixels(unsigned int columns, unsigned int rows, unsigned int address_to height)
{
        address_to height = rows * terminal.cell_h + 2 * terminal.margin;
        return columns * terminal.cell_w + terminal.pad + terminal.bar;
}

// The cells a window's width or height has room for, the paper and the gutter taken off.
static unsigned int terminal_columns(unsigned int width)
{
        unsigned int edge = terminal.pad + terminal.bar;

        return width > edge ? (width - edge) / terminal.cell_w : 1;
}

static unsigned int terminal_rows(unsigned int height)
{
        return height > 2 * terminal.margin ? (height - 2 * terminal.margin) / terminal.cell_h : 1;
}

/*
        The window this program draws in, as large as a share of the output holds in whole cells, and where that
        share puts it: the row the runner took, which says what the output is. The shares are percents, which is
        what lets one rule size a window on any screen; the compositor keeps it on the output. Never larger than
        the output, which is as large as a buffer is.
*/
static void terminal_open(unsigned int wide, unsigned int high, unsigned int left, unsigned int top,
                          unsigned int address_to columns, unsigned int address_to rows, const unsigned int address_to cells)
{
        struct windows address_to table;
        unsigned int display_w, display_h, pixels_w, pixels_h;

        // A shell at its prompt has no use for the pointer's motion, and each report of it is a wakeup: asked for
        // only while a program has a mouse mode (the loop says when that changes).
        windows_hover(&terminal.own, false);
        terminal.hovering = false;

        table = terminal.own.table;
        display_w = table->output_width;
        display_h = table->output_height;
        terminal.pitch = table->stride;
        address_to columns = terminal_columns(display_w * wide / 100);
        address_to rows = terminal_rows(display_h * high / 100);
        // A share too small for a usable grid is twenty columns and four rows, or what the output holds: at a
        // large text size the 80 by 24 this once fell back to was the whole output.
        address_to columns = address_to columns >= 20 ? address_to columns : min(20u, terminal_columns(display_w));
        address_to rows = address_to rows >= 4 ? address_to rows : min(4u, terminal_rows(display_h));

        // A grid asked for (-g) is that many cells in the middle of the output, and not a share of it.
        if (cells[0] && cells[1])
        {
                address_to columns = cells[0];
                address_to rows = cells[1];
        }

        pixels_w = terminal_pixels(address_to columns, address_to rows, &pixels_h);
        if (pixels_w > display_w)
                address_to columns = terminal_columns(display_w);
        if (pixels_h > display_h)
                address_to rows = terminal_rows(display_h);
        pixels_w = terminal_pixels(address_to columns, address_to rows, &pixels_h);
        terminal.width = pixels_w < display_w ? pixels_w : display_w;
        terminal.height = pixels_h < display_h ? pixels_h : display_h;
        table->x[terminal.own.row] = cells[0] && cells[1] ? ((int)display_w - (int)terminal.width) / 2
                                                          : (int)(display_w * left / 100);
        table->y[terminal.own.row] = cells[0] && cells[1] ? ((int)display_h - (int)terminal.height) / 2
                                                          : (int)(display_h * top / 100);
}

/*
        The window is another size: the grid is cut again, the pty told
        before the shell is signalled, and everything drawn at once at it.

        What the compositor asked for is a number of pixels, and that is the
        window: the grid is the whole cells it has room for and the paper
        round them takes the rest, so a window filling the output fills it to
        the last pixel and a drag on an edge follows the hand. Never less
        than one cell and its margins, nor more than the output.
*/
static void terminal_configure(b32 master, unsigned int width, unsigned int height)
{
        struct windows address_to table = terminal.own.table;
        unsigned int columns = terminal_columns(width);
        unsigned int rows = terminal_rows(height);
        unsigned int pixels_h, pixels_w;

        columns = columns ? columns : 1;
        rows = rows ? rows : 1;
        pixels_w = terminal_pixels(columns, rows, &pixels_h);
        pixels_w = width > pixels_w ? width : pixels_w;
        pixels_h = height > pixels_h ? height : pixels_h;
        pixels_w = pixels_w < table->output_width ? pixels_w : table->output_width;
        pixels_h = pixels_h < table->output_height ? pixels_h : table->output_height;
        terminal.pad_y = terminal.margin + (pixels_h > rows * terminal.cell_h + 2 * terminal.margin
                                                ? (pixels_h - rows * terminal.cell_h - 2 * terminal.margin) / 2
                                                : 0);

        // The same size: nothing to draw. The cells have to be the same too: a new text size can keep the
        // pixels and cut the grid another way.
        if (pixels_w == terminal.width && pixels_h == terminal.height && columns == window->columns &&
            rows == window->rows)
                return;

        // The emulator first: it lays the ring out at the new shape and the pty is told
        // before anything is drawn, so a program that redraws on SIGWINCH has the size.
        window->columns = columns;
        window->rows = rows;
        regrid(-1);
        grid_tell(master);
        terminal_view_clamp();
        terminal_frames();
        terminal.width = pixels_w;
        terminal.height = pixels_h;
}

/*
        The text size. Each terminal zooms on its own, and the step the last zoom left is kept in one file that a
        terminal starts at: a decimal number of pixels and a newline, nothing else. A missing file, or one that
        does not say a step of the ladder, is the default; and the file is replaced by a rename, so a reader sees
        the old size or the new one, never a part.
*/
static unsigned int terminal_size_read(void)
{
        p8 text[8] = {0};
        bipolar handle = system_open_at(AT_FDCWD, (string_address)TERMINAL_SIZE_FILE, FILE_READ | O_CLOEXEC);
        bipolar got;
        unsigned int value = 0;

        if (handle < 0)
                return 0;

        got = system_read_once(handle, text, sizeof(text));
        system_close(handle);

        for (bipolar at = 0; at < got; at++)
        {
                if (text[at] >= '0' && text[at] <= '9')
                        value = value * 10 + (unsigned int)(text[at] - '0');
                else if (text[at] != '\n' || at + 1 != got)
                        return 0;
        }

        for (unsigned int at = 0; at < TERMINAL_STEPS; at++)
                if (terminal_steps[at] == value)
                        return value;
        return 0;
}

static unsigned int terminal_step_index(unsigned int size)
{
        unsigned int at = 0;

        // The largest step that is not above size, which is the step a size of the desktop's sits on.
        while (at + 1 < TERMINAL_STEPS && terminal_steps[at + 1] <= size)
                at++;
        return at;
}

static b32 terminal_size_write(unsigned int size)
{
        p8 text[4];
        unsigned int digits[3], count = 0, length = 0;
        bipolar handle;
        b32 wrote;

        for (unsigned int value = size; value || !count; value /= 10)
                digits[count++] = value % 10;
        while (count)
                text[length++] = (p8)('0' + digits[--count]);
        text[length++] = '\n';

        // The directory is made if it is not there; a directory that is there is the answer it should be.
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)TERMINAL_SIZE_DIR, 0755, 0, 0, 0);
        handle = system_open_at(AT_FDCWD, (string_address)TERMINAL_SIZE_TEMP, FILE_WRITE | O_CLOEXEC);
        if (handle < 0)
                return false;

        wrote = system_write_all((positive)handle, text, length) == length;
        system_close(handle);

        return wrote && compositor_call(syscall(renameat), (positive)AT_FDCWD, (positive)TERMINAL_SIZE_TEMP,
                                        (positive)AT_FDCWD, (positive)TERMINAL_SIZE_FILE, 0, 0) >= 0;
}

/*
        The text is drawn at a new size: the metrics are taken again, and the cells are cut again to the window
        they are in, so the window keeps its pixels and holds fewer cells as the text grows (and more as it
        shrinks). The pty is told the new size, as for any resize.
*/
static void terminal_text_set(b32 master, unsigned int size)
{
        terminal.size = size << 6;
        terminal.grid = font_metrics_at(terminal.plain[0], terminal.size);
        terminal.cell_w = (unsigned int)terminal.grid.cell_width;
        terminal.cell_h = (unsigned int)terminal.grid.cell_height;
        terminal_margins();
        term_cell_w = terminal.cell_w;
        term_cell_h = terminal.cell_h;
        // Every glyph is another size: neither buffer holds a cell that can be kept, even where the grid is.
        terminal.valid[0] = terminal.valid[1] = 0;
        terminal_configure(master, terminal.width, terminal.height);
}

static positive terminal_decimal(p8 address_to text, unsigned int value)
{
        p8 digits[10];
        positive count = 0, length = 0;

        do
                digits[count++] = (p8)('0' + value % 10);
        while ((value /= 10) && count < sizeof(digits));
        while (count)
                text[length++] = digits[--count];
        return length;
}

static void terminal_text_said(b32 reading)
{
        p8 text[64];
        positive length = 0;
        const p8 address_to piece[4] = {" text size ", " px, cell ", " by ", " px"};
        unsigned int values[3] = {terminal.size >> 6, terminal.cell_w, terminal.cell_h};

        for (unsigned int at = 0; at < 3; at++)
        {
                memory_copy(text + length, piece[at], string_length((string_address)piece[at]));
                length += string_length((string_address)piece[at]);
                length += terminal_decimal(text + length, values[at]);
        }
        memory_copy(text + length, piece[3], string_length((string_address)piece[3]));
        length += string_length((string_address)piece[3]);
        text[length] = 0;
        terminal_said(reading, (string_address)text);
}

/*
        Ctrl with the character a layout gives one of the zoom keys: = and + grow the text, - and _ shrink it,
        0 goes back to the default. Shift is not looked at, because it is what makes + and _ on the keys that
        carry them, and a layout that puts the sign on other keys sends its own characters.
*/
static int terminal_zoom_key(const struct window_event address_to event)
{
        if (event->type != WINDOW_EVENT_KEY || !(event->flags & WINDOW_KEY_DOWN) ||
            !(event->flags & WINDOW_KEY_CONTROL))
                return 0;

        switch ((unsigned int)event->b)
        {
        case '=':
        case '+':
                return 1;
        case '-':
        case '_':
                return -1;
        case '0':
                return 2;
        }
        return 0;
}

static void terminal_zoom(b32 master, int by)
{
        unsigned int at = terminal_step_index(terminal.size >> 6), next = at;

        if (by == 2)
                next = terminal_step_index(TERMINAL_SIZE);
        else if (by > 0)
                next = at + 1 < TERMINAL_STEPS ? at + 1 : at;
        else
                next = at > 0 ? at - 1 : 0;

        if (terminal_steps[next] == terminal.size >> 6)
                return;

        // This terminal only; the file is where the next one starts.
        terminal_size_write(terminal_steps[next]);
        terminal_text_set(master, terminal_steps[next]);
}

// A key from the compositor, as the window's key ring would have had it.
// Control, Shift, Alt, AltGr, the Super keys and Caps Lock, by their evdev codes: keys that type nothing.
static b32 terminal_modifier_key(int code)
{
        return code == 29 || code == 97 || code == 42 || code == 54 || code == 56 || code == 100 || code == 125 ||
               code == 126 || code == 58;
}

static void terminal_key(const struct window_event address_to event, struct window_key address_to key)
{
        unsigned int flags = event->flags;
        unsigned int character = (unsigned int)event->b;

        key->code = (unsigned int)event->a;
        key->flags = flags & (WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_ALTGR);

        // Control turns a letter into the control code that letter names, which is the
        // whole of why a terminal wants a modifier at all.
        if ((flags & WINDOW_KEY_CONTROL) && (unsigned int)((character | 0x20) - 'a') < 26)
                character &= 0x1f;

        // The key labelled Backspace sends DEL, which is what the kernel's window sent for it
        // and what a shell's line discipline erases on; the compositor's name for it is a
        // backspace character.
        key->character = event->a == TERMINAL_KEY_BACKSPACE ? 127 : character;
        key->reserved = 0;
}

// The pointer as a report in cells, the way the window's key ring carried it.
static void terminal_pointer_key(unsigned int button, unsigned int flags, struct window_key address_to key)
{
        unsigned int column = (unsigned int)(terminal.pointer_x < 0 ? 0 : terminal.pointer_x) / terminal.cell_w;
        unsigned int row = (unsigned int)(terminal.pointer_y < 0 ? 0 : terminal.pointer_y) / terminal.cell_h;

        column = column < COLUMNS ? column : COLUMNS - 1;
        row = row < ROWS ? row : ROWS - 1;

        if (terminal.modifiers & WINDOW_KEY_SHIFT)
                button += 4;
        if (terminal.modifiers & WINDOW_KEY_ALT)
                button += 8;
        if (terminal.modifiers & WINDOW_KEY_CONTROL)
                button += 16;

        key->code = 0;
        key->character = button;
        key->flags = WINDOW_KEY_POINTER | flags;
        key->reserved = ((column + 1) & 0xffff) | (((row + 1) & 0xffff) << 16);
}

// The frame period: the far end's modes and the title are read at most once in one.
static positive terminal_period(void)
{
        struct windows address_to table = terminal.own.table;

        return table->period ? table->period : TERMINAL_FRAME_NANOSECONDS;
}

/*
        The far end's line discipline, read. ICANON and ECHO say whether the emulator edits the line being typed, and then
        the kernel's echo has to be off before a key's bytes are queued, or the key is echoed twice. So they are read
        before any key is taken in a pass of the loop, and otherwise once a frame, where a program's change shows: its
        output comes with it, as a rule.
*/
static void terminal_modes_read(b32 master, positive now)
{
        terminal.modes_at = now;
        terminal.modes_fresh = true;
        term_follow_modes(master);
}

/*
        One event from the compositor. Answers whether the pty will still take
        what the shell is owed; dirty is set when what is drawn has to be made
        again, whether or not the cells changed.
*/
static b32 terminal_receive(b32 master, const struct window_event address_to event, b32 address_to dirty)
{
        struct window_key key;

        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                terminal.modifiers = event->flags;
                if (!terminal.modes_fresh)
                        terminal_modes_read(master, clock_monotonic_nanoseconds());
                terminal_key(event, &key);
                // Typing is a reason to look at what is being typed; a modifier alone is not typing, and
                // Control held for a zoom or a copy keeps the place the view was scrolled back to.
                if ((key.flags & WINDOW_KEY_DOWN) && !terminal_modifier_key(event->a))
                {
                        if (terminal_view_live())
                                address_to dirty = true;
                        cursor_hide();
                }
                return term_input(master, &key);

        case WINDOW_EVENT_MOTION:
                // Where the pointer is from the grid's top left: the paper round it is no cell's.
                terminal.pointer_x = event->a - (int)terminal.pad;
                terminal.pointer_y = event->b - (int)terminal.pad_y;

                if (!mouse_mode && terminal.bar_grab < 0)
                        return true;

                if (terminal.bar_grab >= 0)
                {
                        if (terminal_bar_move(terminal.pointer_y))
                                address_to dirty = true;
                        return true;
                }

                terminal_pointer_key(0, WINDOW_KEY_POINTER_MOVE | (terminal.pressed ? WINDOW_KEY_DOWN : 0), &key);
                return term_input(master, &key);

        case WINDOW_EVENT_BUTTON:
        {
                unsigned int button = event->a == 0x111 ? 2 : event->a == 0x112 ? 1 : 0;
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;
                unsigned int first, shown, total;

                if (event->a != 0x110 && event->a != 0x111 && event->a != 0x112)
                        return true;

                // The gutter is the bar's, not the program's.
                if (event->a == 0x110 && down && terminal.bar_level &&
                    terminal.pointer_x >= (int)(COLUMNS * terminal.cell_w) && terminal_extent(&first, &shown, &total))
                {
                        unsigned int thumb = terminal_thumb(shown, total), top = terminal_thumb_top(first, shown, total);

                        terminal.bar_grab = (unsigned int)terminal.pointer_y >= top &&
                                                    (unsigned int)terminal.pointer_y < top + thumb
                                                ? terminal.pointer_y - (int)top
                                                : (int)thumb / 2;
                        if (terminal_bar_move(terminal.pointer_y))
                                address_to dirty = true;
                        return true;
                }

                if (!down && terminal.bar_grab >= 0)
                {
                        terminal.bar_grab = -1;
                        return true;
                }

                terminal.pressed = down;
                terminal_pointer_key(button, down ? WINDOW_KEY_DOWN : 0, &key);
                return term_input(master, &key);
        }

        case WINDOW_EVENT_WHEEL:
        {
                //      A wheel event is at most a screen: a delta of 2^30 notches is one from a hostile client
                //      or a bad driver, and it used to be a write per notch (17.9M of them) or 160 MB of
                //      arrows queued on the alternate screen.
                int steps = event->b ? 0 : event->a;

                //      ROWS is unsigned, so its negation is not: the bounds are signed here.
                steps = steps > (int)ROWS * 120 ? (int)ROWS * 120 : steps < -(int)ROWS * 120 ? -(int)ROWS * 120 : steps;

                // A program that asked for the pointer is owed the wheel as buttons 64 and 65, one a notch:
                // a wheel that reports in fifteenths of a notch is the same notches as one that reports whole.
                if (mouse_mode)
                {
                        b32 sent = true;

                        terminal.wheel_notches += steps;
                        terminal.wheel_notches = terminal.wheel_notches > (int)ROWS * 120 ? (int)ROWS * 120
                                                 : terminal.wheel_notches < -(int)ROWS * 120 ? -(int)ROWS * 120
                                                                                              : terminal.wheel_notches;
                        terminal.wheel_remainder = 0;
                        while (sent && (terminal.wheel_notches >= 120 || terminal.wheel_notches <= -120))
                        {
                                terminal_pointer_key(terminal.wheel_notches < 0 ? 65u : 64u, WINDOW_KEY_DOWN, &key);
                                terminal.wheel_notches += terminal.wheel_notches < 0 ? 120 : -120;
                                sent = term_input(master, &key);
                        }
                        return sent;
                }
                terminal.wheel_notches = 0;

                long scaled = (long)steps * TERMINAL_WHEEL_LINES + terminal.wheel_remainder;
                int lines = (int)(scaled / 120);

                lines = lines > (int)ROWS ? (int)ROWS : lines < -(int)ROWS ? -(int)ROWS : lines;

                terminal.wheel_remainder = (int)(scaled % 120);

                if (!lines)
                        return true;

                // A pager or an editor on the alternate screen has no scrollback here: the wheel is its
                // arrows, a line a step, as the cursor keys' mode spells them.
                if (alternate)
                {
                        string_address arrow = lines > 0 ? (application_keys ? "\033OA" : "\033[A")
                                                         : (application_keys ? "\033OB" : "\033[B");

                        for (int at = lines < 0 ? -lines : lines; at > 0; at--)
                                emit_bytes((address_any)arrow, string_length(arrow));
                        return term_send(master);
                }

                if (terminal_view_scroll(lines))
                        address_to dirty = true;
                return true;
        }
        }

        return true;
}

// The tab's name at the prompt: "shell", or the program a terminal was asked to run (-x).
static const char address_to terminal_label = "shell";

/*
        The window's title, which its tab shows: the program in front on the
        pty -- btop while btop runs -- and "shell" back at the prompt, and a
        title a program sets (OSC 0 or 2) when it sets one. Looked at as the
        far end is read, which is when either can change, and said to the
        compositor only when it has.
*/
static void terminal_title_set(const char address_to title)
{
        struct windows address_to table = terminal.own.table;
        unsigned int at = 0;

        for (; title[at] && title[at] != '\n' && at < WINDOWS_TITLE - 1; at++)
                table->title[terminal.own.row][at] = title[at];
        // Cut on a character, not inside one: back over the continuation bytes of the one that did not fit.
        if (at == WINDOWS_TITLE - 1 && ((unsigned char)title[at] & 0xc0) == 0x80)
                while (at > 0 && ((unsigned char)title[at] & 0xc0) == 0x80)
                        at--;
        table->title[terminal.own.row][at] = 0;
        windows_knock(&terminal.own);
}

static void terminal_title(b32 master, bipolar shell)
{
        int group = 0;

        if (compositor_call(syscall(ioctl), (positive)master, 0x540f, (positive)&group, 0, 0, 0) == 0 &&
            group != terminal.group)
        {
                p8 path[32] = "/proc/", name[WINDOWS_TITLE] = {0};
                bipolar handle;

                terminal.group = group;
                path[6 + positive_into_string(path + 6, (positive)group)] = 0;
                string_copy((string_address)path + string_length((string_address)path), (string_address) "/comm");
                handle = group == (int)shell ? -1 : system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
                if (handle >= 0)
                {
                        system_read_once((b32)handle, name, sizeof(name) - 1);
                        system_close(handle);
                }
                terminal_title_set(name[0] ? (const char address_to)name : terminal_label);
        }

        // A title a program set, each time it sets one: the same again after the program's name took the tab
        // is the program's again, and an empty one gives the tab back to the program's name.
        if (title_said != terminal.title_seen)
        {
                terminal.title_seen = title_said;
                memory_copy(terminal.named, window->title, WINDOW_TITLE_MAX);
                if (terminal.named[0])
                        terminal_title_set(terminal.named);
                else
                {
                        terminal.group = -1;
                        terminal_title(master, shell);
                }
        }
}

// A line in the machine's log about this program, at its start and its end: its own standard output is nowhere.
static void terminal_said(b32 reading, string_address what)
{
        p8 line[96];

        string_copy((string_address)line, (string_address)(reading ? "klog pid " : "terminal pid "));
        compositor_kmsg(line, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), what);
}

// What is said after a program this terminal was asked to run has ended: how it did, and what closes the window.
static fn terminal_exit_said(void)
{
        p8 line[96] = "\x1b[0m\r\n[";
        positive at = string_length(line);

        if (!(term_exit_status & 0x7f))
        {
                string_copy(line + at, (string_address) "exited with status ");
                at += string_length(line + at);
                at += positive_into_string(line + at, (term_exit_status >> 8) & 0xff);
                string_copy(line + at, (string_address) ", ");
                at += 2;
        }
        string_copy(line + at, (string_address) "press a key to close]");
        at += string_length(line + at);
        cursor_hide();
        term_bytes(line, at);
}

// Whether an event is a key that was pressed and means something: not a repeat, and not a modifier on its own.
static b32 terminal_key_pressed(const struct window_event address_to event)
{
        return event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && !(event->flags & WINDOW_KEY_REPEAT) &&
               !terminal_modifier_key(event->a);
}

static b32 klog_drain(b32 far, b32 address_to gone, positive budget);
static b32 klog_receive(const struct window_event address_to event, b32 address_to dirty);
static void terminal_leave(b32 show);

// The program's arguments, read before the runner takes the window and used when it opens.
static string_address terminal_paths[TERMINAL_FACES];
static string_address terminal_bold_path, terminal_ready_path;
static unsigned int terminal_face_count, terminal_size_asked;
static unsigned int terminal_cells[2];

/*
        The loop, as the runner drives it. The runner waits on the compositor and on the pty, and each pass takes the
        compositor's events, the table's asks, and a picture when one is due; the hooks below are that body in its
        order: the pass's start (the cells touched and the far end's modes), the events, the focus, the size, the
        closing, the frame and its holds, then the far end's report, the title, the synchronized frame's hold, the bar's
        fade, and the timers that say how long the wait may be.
*/

// The far end's output, read: what is read is a change, and the frame is due. Waited on after every pass, as the loop read it.
static void terminal_on_ready(void)
{
        b32 drew = terminal.reading ? klog_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET)
                                    : term_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET);

        if (touched_bottom > touched_top)
                drew = true;
        if (drew)
        {
                terminal.changed = true;
                terminal.dirty = true;
        }
}

// The top of a pass: what the pty will say first, and the far end's modes read at most once a frame period, before a key is taken.
static void terminal_on_pass(void)
{
        if (terminal.phase != TERMINAL_RUNNING)
                return;

        // A held frame keeps the rows it has touched until it is let go.
        if (!synchronized_output)
        {
                touched_top = ROWS;
                touched_bottom = 0;
        }

        terminal.modes_fresh = false;
        if (!terminal.reading)
        {
                positive read_now = clock_monotonic_nanoseconds();

                if (read_now - terminal.modes_at >= terminal_period())
                        terminal_modes_read(terminal.master, read_now);
        }
}

// One event from the compositor: zoom keys, then the keys and pointer of the shell (or the log's keys). A staying window
// leaves on a key, when the shell was a program that was asked for.
static b32 terminal_on_event(const struct window_event address_to event)
{
        int zoom = terminal_zoom_key(event);

        if (zoom)
        {
                terminal_zoom(terminal.master, zoom);
                terminal.dirty = true;
                return true;
        }
        if (terminal.phase == TERMINAL_STAYING)
        {
                if (terminal.keyed && terminal_key_pressed(event))
                        terminal.phase = TERMINAL_DONE;
                return true;
        }
        if (terminal.phase == TERMINAL_RUNNING &&
            !(terminal.reading ? klog_receive(event, &terminal.dirty) : terminal_receive(terminal.master, event, &terminal.dirty)))
                terminal.gone = true;
        return true;
}

// The keys came or went: the far end is told, where the shell asked to hear of it.
static b32 terminal_on_focus(b32 focused)
{
        if (terminal.phase == TERMINAL_RUNNING && !terminal.reading)
        {
                term_focus(focused);
                if (!term_send(terminal.master))
                        terminal.gone = true;
        }
        return true;
}

// The table asked for a size: the grid is cut to it, the pty told, and the picture due again.
static void terminal_on_size(unsigned int width, unsigned int height)
{
        terminal_configure(terminal.master, width, height);
        terminal.dirty = true;
}

// The window is closing: the shell is hung up on, once, and the loop goes on until the shell has gone. A log has no
// shell, so it is over. A staying window is over as well.
static void terminal_on_closing(void)
{
        if (terminal.phase == TERMINAL_STAYING)
        {
                terminal.phase = TERMINAL_DONE;
                return;
        }
        if (terminal.phase != TERMINAL_RUNNING)
                return;
        if (terminal.reading)
                terminal.gone = true;
        else if (!terminal.gone)
                term_hang_up(terminal.child, &terminal.hung);
}

/*
        The far end's output is drawn when it is due and not held: a frame is held while the output that has just ended a
        line settles, for the rest of a period after the last frame, and while the far end holds a synchronized frame open.
        A picture the compositor has not let go of waits; after TERMINAL_STALL_NANOSECONDS it is drawn regardless.
        Answers whether the picture is made; a held one leaves its wait in terminal.hold_timeout.
*/
static b32 terminal_pass_frame(void)
{
        positive now;

        terminal.hold_timeout = -1;
        if (terminal.phase == TERMINAL_RUNNING)
        {
                if (synchronized_output)
                        return false;

                now = clock_monotonic_nanoseconds();
                if (terminal.changed && !terminal.reading && !column)
                {
                        terminal.settle_since = terminal.settle_since ? terminal.settle_since : now;
                        if (now - terminal.settle_since < (positive)TERMINAL_SETTLE_NANOSECONDS)
                                terminal.hold_timeout = (long)(TERMINAL_SETTLE_NANOSECONDS - (now - terminal.settle_since));
                }
                if (terminal.last_frame)
                {
                        positive period = terminal_period();
                        positive since = now - terminal.last_frame;

                        if (since < period)
                                terminal.hold_timeout = (long)(period - since);
                }
                if (terminal.hold_timeout >= 0)
                        return false;
                cursor_show();
        }

        if (terminal_frame())
        {
                terminal.dirty = false;
                if (terminal.phase == TERMINAL_RUNNING)
                        terminal.last_frame = clock_monotonic_nanoseconds();
                return true;
        }

        now = clock_monotonic_nanoseconds();
        if (!terminal.stalled_since)
                terminal.stalled_since = now;
        else if (now - terminal.stalled_since >= (positive)TERMINAL_STALL_NANOSECONDS)
        {
                // Waited for the compositor to take the last picture, and it has not: the buffer is taken.
                terminal.forced = true;
                terminal.stalled_since = 0;
        }
        return false;
}

/*
        The far end is gone: what the pty still holds is read once more, and drawn if it changed anything, and the window
        stays for what the shell said or leaves (terminal_leave).
*/
static void terminal_end(void)
{
        b32 drew = terminal.reading ? klog_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET)
                                    : term_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET);

        if (drew || touched_bottom > touched_top)
        {
                cursor_show();
                terminal_frame();
        }
        terminal_leave(true);
}

/*
        The end of the shell, as the loop used to leave it: the pty is closed for a log, and the shell is finished with. A
        program that was asked for, or a shell that asks to stay, keeps the window until a key is pressed or its X; otherwise
        the loop is over.
*/
static void terminal_leave(b32 show)
{
        b32 stays;

        if (terminal.reading)
                system_close(terminal.master);

        stays = !terminal.reading && term_finish(terminal.child, terminal.master, terminal.hung, terminal.oom_kills);

        // A program that was asked for ends in a window that waits for a key, unless the X sent the end.
        if (!terminal.reading && term_program_count && !terminal.hung)
        {
                terminal_exit_said();
                stays = terminal.keyed = true;
        }

        if (stays && show)
        {
                // What is left of a window whose shell is gone: its cells, and what said how it ended, drawn until the
                // compositor has taken them.
                cursor_show();
                terminal.dirty = !terminal_frame();
                terminal.phase = TERMINAL_STAYING;
                return;
        }
        terminal.phase = TERMINAL_DONE;
}

/*
        The rest of a pass, after the frame: the modes and the title, the report the far end is owed, the synchronized frame's
        hold, the bar's fade, and the timers. The answer is the wait's timeout: -1 for ever, else the longest the wait may
        last; 0 when a frame is due now and not held, so the loop runs again before it waits. The pty the wait is on is set
        too: none while the window stays.
*/
static long terminal_tick(long timeout, long address_to other)
{
        b32 was = terminal.dirty, again = false;
        long hold = terminal.hold_timeout;
        positive now;

        (void)timeout;
        terminal.hold_timeout = -1;

        if (terminal.phase == TERMINAL_RUNNING && terminal.gone)
                terminal_end();
        if (terminal.phase == TERMINAL_DONE)
        {
                *other = -1;
                return -1;
        }
        if (terminal.phase == TERMINAL_STAYING)
        {
                *other = -1;
                return terminal.dirty ? TERMINAL_STALL_NANOSECONDS : -1;
        }
        *other = terminal.master;

        // The mouse modes say whether the pointer's motion is wanted: a press, a held button and a wheel all say where it is.
        if (!terminal.reading && (mouse_mode != 0) != terminal.hovering)
        {
                terminal.hovering = mouse_mode != 0;
                windows_hover(&terminal.own, terminal.hovering);
        }

        // The title is read when the far end has said something, at most once a frame period; a title a program sets is read as it is set.
        if (!terminal.reading && !terminal.gone && (terminal.changed || terminal.title_owed))
        {
                positive title_now = clock_monotonic_nanoseconds();

                if (title_now - terminal.title_at >= terminal_period() || title_said != terminal.title_seen)
                {
                        terminal_title(terminal.master, terminal.child);
                        terminal.title_at = title_now;
                        terminal.title_owed = false;
                }
                else
                        terminal.title_owed = true;
        }

        if (!terminal.gone && !term_send(terminal.master))
                terminal.gone = true;

        if (touched_bottom > touched_top)
                terminal.changed = true;

        timeout = -1;
        if (synchronized_output)
        {
                now = clock_monotonic_nanoseconds();
                terminal.sync_since = terminal.sync_held ? terminal.sync_since : now;
                terminal.sync_held = true;

                if (terminal.gone || now - terminal.sync_since >= (positive)TERMINAL_SYNC_NANOSECONDS)
                {
                        synchronized_output = false;
                        terminal.changed = true;
                }
                else
                        timeout = (long)(TERMINAL_SYNC_NANOSECONDS - (now - terminal.sync_since));
        }
        else
                terminal.sync_held = false;

        terminal.dirty |= terminal.changed;

        // The bar: seen while the view is scrolled back or the bar is held, and faded out over a quarter of a second once the
        // view is back at the live end, a frame a step.
        {
                unsigned int level = 256;

                if (terminal.view != TERMINAL_LIVE || terminal.bar_grab >= 0)
                        terminal.bar_fading = 0;
                else if (!terminal.bar_level)
                        level = 0;
                else
                {
                        now = clock_monotonic_nanoseconds();
                        terminal.bar_fading = terminal.bar_fading ? terminal.bar_fading : now;
                        level = now - terminal.bar_fading >= (positive)TERMINAL_FADE_NANOSECONDS
                                    ? 0
                                    : (unsigned int)(256 - 256 * (now - terminal.bar_fading) / TERMINAL_FADE_NANOSECONDS);
                        terminal.bar_fading = level ? terminal.bar_fading : 0;
                }
                if (level != terminal.bar_level)
                {
                        terminal.bar_level = level;
                        terminal.dirty = true;
                }
                if (terminal.bar_fading)
                        timeout = timeout < 0 || timeout > (long)TERMINAL_FRAME_NANOSECONDS ? (long)TERMINAL_FRAME_NANOSECONDS : timeout;
        }

        if (hold >= 0 && (timeout < 0 || hold < timeout))
                timeout = hold;

        if (terminal.title_owed)
        {
                positive due = terminal.title_at + terminal_period(), owed_now = clock_monotonic_nanoseconds();
                long left = due > owed_now ? (long)(due - owed_now) : 0;

                timeout = timeout < 0 || left < timeout ? left : timeout;
        }

        /*
                Asleep until a key, a resize or the shell's output; and, with a picture waiting for a buffer, until the compositor has
                let one go. With nothing to do and a spare buffer held, the spare is given back after the time it takes to be sure nothing
                is coming, and then the sleep is for ever: an idle terminal wakes no more.
        */
        {
                b32 spare = !terminal.dirty && !synchronized_output && terminal.shown >= 0 && terminal.valid[1 - terminal.shown] &&
                            windows_ready(&terminal.own);
                b32 stalled = terminal.dirty && !synchronized_output && terminal.shown >= 0 && !windows_ready(&terminal.own);

                if (spare && timeout < 0)
                {
                        // A spare is one the compositor has taken the last picture from; until it has, nothing is to be discarded.
                        now = clock_monotonic_nanoseconds();
                        terminal.idle_since = terminal.idle_since ? terminal.idle_since : now;
                        if (now - terminal.idle_since >= (positive)TERMINAL_IDLE_NANOSECONDS)
                        {
                                terminal_discard();
                                terminal.idle_since = 0;
                        }
                        else
                                timeout = (long)(TERMINAL_IDLE_NANOSECONDS - (now - terminal.idle_since));
                }
                else
                {
                        terminal.idle_since = 0;
                        if (stalled && timeout < 0)
                                timeout = TERMINAL_STALL_NANOSECONDS;
                }
        }

        if (!terminal.dirty)
                terminal.stalled_since = terminal.settle_since = 0;

        // A frame that became due in this pass, and is not held, is drawn by the next pass before any wait.
        again = !was && terminal.dirty && hold < 0 && !synchronized_output;
        if (!again)
                terminal.changed = false;
        return again ? 0 : timeout;
}

// The runner ends its loop on this, after a tick: the far end is gone and what it said is seen to, or it is over.
static b32 terminal_done(void)
{
        return terminal.phase == TERMINAL_DONE;
}

// The runner stopped its loop on the compositor's going: the shell is hung up on, and what is left is seen out.
static void terminal_on_close(void)
{
        if (terminal.phase == TERMINAL_RUNNING)
        {
                if (!terminal.gone)
                        term_hang_up(terminal.child, &terminal.hung);
                terminal_leave(false);
        }
        terminal.phase = TERMINAL_DONE;
}

/*
        The window's start, after the runner took it: the faces, the grid the window has room for, the ring, the pty or the
        log, and the first picture. Answers the runner's status when it fails, or -1 to run the loop.
*/
static int terminal_start(void)
{
        unsigned int size = terminal_size_asked, columns, rows, max_columns;
        struct windows address_to table = terminal.own.table;

        // -s is the size asked for here; without it, the step the last zoom left, and the default.
        size = size ? size : terminal_size_read();
        size = size ? size : TERMINAL_SIZE;
        size = size < terminal_steps[0] ? terminal_steps[0] : size > terminal_steps[TERMINAL_STEPS - 1] ? terminal_steps[TERMINAL_STEPS - 1] : size;

        if (!terminal_fonts(terminal_paths, terminal_face_count, terminal_bold_path, size))
        {
                terminal_said(terminal.reading, " has no font to draw with (-f FONT.ttf)");
                return 1;
        }

        for (unsigned int at = 0; at < 256; at++)
                terminal_colour[at] = window_cell_colour(at);
        term_palette = terminal_palette;

        term_cell_w = terminal.cell_w;
        term_cell_h = terminal.cell_h;

        // Shares of the output, in percent: the log at the left and the terminal across from it, over the log's right edge.
        terminal_open(terminal.reading ? TERMINAL_LOG_WIDE : TERMINAL_WIDE, terminal.reading ? TERMINAL_LOG_HIGH : TERMINAL_HIGH,
                      terminal.reading ? TERMINAL_LOG_LEFT : TERMINAL_LEFT, terminal.reading ? TERMINAL_LOG_TOP : TERMINAL_TOP,
                      &columns, &rows, terminal_cells);
        terminal.bar_grab = -1;

        // The ring is cut for the smallest text a zoom reaches, so that every size after this one has room in it.
        font_metrics least = font_metrics_at(terminal.plain[0], terminal_steps[0] << 6);

        max_columns = (table->output_width > TERMINAL_CEILING ? table->output_width : TERMINAL_CEILING) /
                      (unsigned int)least.cell_width;
        {
                unsigned int tallest = (table->output_height > TERMINAL_CEILING * 9 / 16 ? table->output_height
                                                                                       : TERMINAL_CEILING * 9 / 16) /
                                       (unsigned int)least.cell_height;

                unsigned int history = terminal.reading ? TERMINAL_LOG_HISTORY : TERMINAL_HISTORY;

                window = window_grid_open(columns, rows, max_columns, tallest * 2 + 1 > history ? tallest * 2 + 1 : history);
        }

        if (window)
                grid_take();

        if (!window || !terminal_frames())
        {
                terminal_said(terminal.reading, " has no memory for its cells");
                return 1;
        }

        full_reset();

        if (terminal.reading)
        {
                // Not a terminal's cursor, and nothing to follow but the log: read without waiting, and waited for
                // with the compositor's events, which is what makes an idle log wake nothing.
                cursor_visible = false;
                terminal.master = (b32)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);
                terminal.child = (bipolar)terminal.master;
        }
        else
                terminal.child = term_session(&terminal.master, &terminal.oom_kills);

        if (terminal.child < 0)
        {
                terminal_said(terminal.reading, " cannot open its far end");
                return 1;
        }

        cursor_show();
        terminal_frame();
        terminal.last_frame = clock_monotonic_nanoseconds();

        terminal_text_said(terminal.reading);

        // The first picture is committed: whoever is starting windows in an order may start the next.
        if (terminal_ready_path)
                system_close(system_open_at(AT_FDCWD, terminal_ready_path, FILE_WRITE | O_CLOEXEC));

        terminal.dirty = true;
        terminal.phase = TERMINAL_RUNNING;
        return -1;
}

static struct ui_app terminal_app = {
        .own = &terminal.own,
        .ui = &terminal.ui,
        .glass = 0,
        .dirty = &terminal.dirty,
        .focused = &terminal.focused,
        .ground = &terminal.ground,
        .own_faces = true,
        .open = terminal_start,
        .frame = terminal_pass_frame,
        .event = terminal_on_event,
        .focus = terminal_on_focus,
        .size = terminal_on_size,
        .tick = terminal_tick,
        .ready = terminal_on_ready,
        .close = terminal_on_close,
        .pass = terminal_on_pass,
        .closing = terminal_on_closing,
        .done = terminal_done,
};

/*
        The program, which is a shell on a pty, or with reading the kernel's log on /dev/kmsg (klog.c): the same
        cells, window, bar and wheel, a far end that is read and never written to, and keys that scroll.
*/
static b32 terminal_run(b32 reading)
{
        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2] || !value)
                        continue;
                // The rest is the program's own words, whatever they look like.
                if (option[1] == 'x' && !reading)
                {
                        // The words are the kernel's argv from here on, every one of them, ended by its null as always.
                        term_program_words = program_argument_list() + at + 1;
                        term_program_count = (positive)program_argument_count() - (at + 1);
                        break;
                }
                at++;
                if (option[1] == 'f' && terminal_face_count < TERMINAL_FACES)
                        terminal_paths[terminal_face_count++] = value;
                else if (option[1] == 'b')
                        terminal_bold_path = value;
                else if (option[1] == 's')
                        terminal_size_asked = (unsigned int)compositor_number(value, 0);
                else if (option[1] == 'w')
                        terminal_ready_path = value;
                else if (option[1] == 'g')
                {
                        // COLUMNSxROWS
                        terminal_cells[0] = (unsigned int)compositor_number(value, 0);
                        for (positive at_x = 0; value[at_x]; at_x++)
                                if (value[at_x] == 'x')
                                        terminal_cells[1] = (unsigned int)compositor_number(value + at_x + 1, 0);
                }
        }

        // Named, because two untitled terminals are a guessing game.
        if (term_program_count)
        {
                // A command given to the shell as it was typed, else the program's name and not its path.
                string_address slash = string_last_of(term_program_words[0], '/');

                terminal_label = term_program_count == 3 && string_equals(term_program_words[1], (string_address) "-c")
                                     ? term_program_words[2]
                                     : slash ? slash + 1 : term_program_words[0];
        }

        terminal.reading = reading;
        terminal_app.title = reading ? (string_address) "kernel log" : (string_address)terminal_label;
        terminal_app.missing = reading ? (string_address) "klog: no window: no compositor, or no row free in its table\n"
                                       : (string_address) "terminal: no window: no compositor, or no row free in its table\n";
        return ui_app_run(&terminal_app);
}

static b32 terminal_main()
{
        return terminal_run(false);
}

#endif // MOONWATER_CANVAS_TERMINAL
