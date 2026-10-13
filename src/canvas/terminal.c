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
        same folding of a stored line at the width it is drawn in, the same
        bar down the right that is the scrollback, and the wheel and a drag on
        the bar to move over it. It owes the shell what the kernel's window
        owed it -- keys, the pointer when a program has asked for it, a
        window size on the pty before SIGWINCH, the hangup when the X is
        pressed -- and those come through the same functions in screen.c.

        Two buffers, and the rule that comes with two. A buffer the compositor
        may still be reading is not drawn into: the cells go into the emulator
        the moment they arrive, and a frame is made only when the compositor
        has taken the last picture, so the buffer that is not on the screen
        is free. A burst of output is therefore
        one frame, not one a read, and a key's echo is on the screen in the
        first frame there is room for.

        Each buffer's picture is remembered as the cells that made it, so a
        frame draws only the cells that differ from what that buffer holds,
        and tells the compositor the rectangle it has to read again: the cells that
        differ from the buffer being drawn into, and those that differ from the
        one on the screen, which are not always the same.

        The font is a path: the image carries IBM Plex Mono under
        /usr/share/fonts, and the machine script names it.

            terminal [-s PIXELS] [-f FONT.ttf]... [-b BOLD.ttf] [-g COLUMNSxROWS]

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_TERMINAL
#define MOONWATER_CANVAS_TERMINAL

// Lines kept at least: what is on the screen and what has scrolled off it, as the kernel's window kept
// them, and never fewer than two screens of the tallest window the output could hold.
#define TERMINAL_HISTORY 512
#define TERMINAL_FACES 4
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
#define TERMINAL_ATLAS 512
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
// A frame that has waited this long for a buffer to be released draws into it regardless: the
// compositor not on the screen is not reading it, and a terminal that never draws again is worse.
#define TERMINAL_STALL_NANOSECONDS 250000000ll

typedef unsigned long long __attribute__((may_alias)) terminal_word;

static struct
{
        struct window_row own; // its row of the compositor's table
        font_face face[TERMINAL_FACES];
        const font_face *plain[TERMINAL_FACES];
        const font_face *bold[TERMINAL_FACES];
        unsigned int plain_count, bold_count;
        font_engine *engine;
        font_metrics grid;
        unsigned int size; // in 26.6, which is what font.c takes
        // The paper round the grid, the same on every side: at the left (pad), at the right (bar), where the
        // scroll bar is, and above and below (pad_y).
        unsigned int cell_w, cell_h, bar, pad, pad_y;
        unsigned int width, height; // the window, in pixels
        unsigned int pitch;         // pixels a row of a buffer, which is the output's width

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
        unsigned int bar_first[2], bar_shown[2], bar_total[2];

        // Where the view sits in the ring: a line and how many of its rows are above.
        unsigned int view, view_skip;

        // The pointer, in buffer pixels, and the button being held on the bar.
        int pointer_x, pointer_y;
        int bar_grab;
        unsigned int modifiers; // from the keys, which are the only events that say
        b32 pressed;
        int wheel_remainder;
        unsigned int asked_w, asked_h; // the newest size the compositor asked for
        b32 focused;
        int group;                      // the pty's foreground process group the title was taken from
        char named[WINDOW_TITLE_MAX];   // the last title a program set (OSC 0 or 2)
} terminal = {.shown = -1, .view = TERMINAL_LIVE};

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

// The oldest line still held, never fewer than a screenful before the end.
static unsigned int terminal_oldest(void)
{
        unsigned int filled = window->head - window->history;

        filled = filled < ROWS ? ROWS : filled > window->history ? window->history : filled;
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
*/
static b32 terminal_extent(unsigned int address_to first, unsigned int address_to shown,
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

// The bar put under the hand: where the pointer is in it, as a place in the rows.
static b32 terminal_bar_move(int y)
{
        unsigned int first, shown, total;
        unsigned int span, thumb, track = terminal_track();
        int top;

        if (!terminal_extent(&first, &shown, &total) || !track)
                return false;

        thumb = (unsigned int)((unsigned long)track * shown / total);
        thumb = thumb < terminal.cell_h ? terminal.cell_h : thumb;
        span = track > thumb ? track - thumb : 0;
        top = y - terminal.bar_grab;
        top = top < 0 ? 0 : (unsigned int)top > span ? (int)span : top;

        return terminal_view_set((unsigned int)((unsigned long)top * total / track));
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
        or through it, or a wide one, is its own font_cell. A run of blanks
        on one ground is one fill.
*/
static p32 terminal_codes[FONT_RUN];

//      Italic is a slant of the plain face, and bold without a bold face is
//      the plain face made heavier: neither has a face of its own here.
static p32 terminal_style(const struct window_cell address_to cell)
{
        return (cell->flags & WINDOW_CELL_ITALIC ? FONT_OBLIQUE : 0) |
               ((cell->flags & WINDOW_CELL_BOLD) && !terminal.bold_count ? FONT_EMBOLDEN : 0);
}
static void terminal_row(unsigned int address_to pixels, unsigned int row,
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

#define TERMINAL_GLYPHS()                                                                              \
        do                                                                                             \
        {                                                                                              \
                if (glyphs)                                                                            \
                        font_cells(terminal.engine, glyphs_heavy ? terminal.bold : terminal.plain,     \
                                   glyphs_heavy ? terminal.bold_count : terminal.plain_count,          \
                                   terminal_codes, glyphs, &terminal.grid, terminal.size, glyphs_flags,\
                                   &target,                                                            \
                                   (b32)(glyphs_from * terminal.cell_w), y, glyphs_ink, glyphs_paper); \
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
                                terminal_fill(pixels, (int)(run_from * terminal.cell_w), y, run * terminal.cell_w,
                                              terminal.cell_h, run_colour);
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
                        terminal_codes[glyphs++] = cell->character;
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
                                        terminal_fill(pixels, (int)(run_from * terminal.cell_w), y,
                                                      run * terminal.cell_w, terminal.cell_h, run_colour);
                                run = 1;
                                run_from = at;
                                run_colour = paper;
                        }
                        continue;
                }

                if (run)
                        terminal_fill(pixels, (int)(run_from * terminal.cell_w), y, run * terminal.cell_w,
                                      terminal.cell_h, run_colour);
                run = 0;

                if (!cells)
                        continue;

                if (cell->flags & WINDOW_CELL_DIM)
                        ink = terminal_mix(ink, paper);

                if (glyph)
                {
                        b32 heavy = (cell->flags & WINDOW_CELL_BOLD) && terminal.bold_count;

                        font_cell(terminal.engine, heavy ? terminal.bold : terminal.plain,
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
                terminal_fill(pixels, (int)(run_from * terminal.cell_w), y, run * terminal.cell_w,
                              terminal.cell_h, run_colour);
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
        The right of the grid: each row's paper, from its last cell, to the
        window's right edge, and the scroll bar's thumb a pill on it, as wide
        as half the gutter, round at both ends, a quarter of the gutter in
        from every side of the place the thumb has, in a colour between the
        paper and the ink that stands out on the row's paper (window_contrast),
        so it is seen on any ground a program paints. Where a pixel is part of
        a round end it is the share of its sixteen points inside the pill,
        mixed with that ground, which is known: nothing is read back. No
        track, and no pill when everything fits.
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

        if (!gutter || x + gutter > terminal.width || total <= shown)
                return;

        thumb = (unsigned int)((unsigned long)track * shown / total);
        thumb = thumb < terminal.cell_h ? terminal.cell_h : thumb;
        at = (unsigned int)((unsigned long)track * first / total);
        at = at + thumb > track ? track - thumb : at;

        inset = gutter / 4 ? gutter / 4 : 1;
        wide = gutter > 2 * inset ? gutter - 2 * inset : 1;
        if (thumb <= 2 * inset + wide)
                return;

        // In eighths of a pixel from the pill's top left (sample centres are odd): its ends are half
        // circles of radius wide / 2.
        int across = (int)wide * 4, along = (int)(thumb - 2 * inset) * 4, radius = across / 2;
        unsigned int left = x + inset, pill = terminal.pad_y + at + inset;

        for (int row = 0; row < along / 4; row++)
        {
                unsigned int y = pill + (unsigned int)row, cell_row = (y - terminal.pad_y) / terminal.cell_h;
                unsigned int address_to line = whole + (unsigned long)y * terminal.pitch + left;
                unsigned int ground = terminal_ground(frame, cell_row < ROWS ? cell_row : ROWS - 1, COLUMNS - 1);
                unsigned int ink = window_mix(ground, window_contrast(ground), 160);

                for (int column = 0; column < across / 4; column++)
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
        The buffer the next frame goes into: the one not on the screen, once
        the compositor has taken the last picture and so reads only the one
        that is; before the first picture, either.
*/
static int terminal_next(void)
{
        int slot = terminal.shown < 0 ? 0 : 1 - terminal.shown;

        return terminal.shown < 0 || terminal.forced || windows_ready(&terminal.own) ? slot : -1;
}

// What is held for the next frame's pictures, cut for the grid as it is now.
static b32 terminal_frames(void)
{
        unsigned long bytes = (unsigned long)ROWS * COLUMNS * sizeof(struct window_cell);
        long mapped;

        if (terminal.frames)
                window_call(WINDOW_SYS_MUNMAP, (long)terminal.frames, 3 * (long)terminal.frame_bytes, 0, 0, 0, 0);

        bytes = (bytes + 4095) & ~4095ul;
        // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
        mapped = window_call(WINDOW_SYS_MMAP, 0, 3 * (long)bytes, 3, 0x22, -1, 0);
        terminal.frames = 0;

        if (mapped < 0 && mapped > -4096)
                return false;

        terminal.frames = (struct window_cell *)mapped;
        terminal.frame_bytes = bytes;
        for (unsigned int at = 0; at < 3; at++)
                terminal.frame[at] = (struct window_cell *)(mapped + (long)bytes * at);
        terminal.valid[0] = terminal.valid[1] = 0;
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
        One frame: the cells that differ drawn into the free buffer, what
        the compositor has to read again named, and the picture shown.
        False when there is no buffer to draw into yet, and nothing was done.
*/
static b32 terminal_frame(void)
{
        int slot = terminal_next();
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

        terminal_view_clamp();
        terminal_build(now);
        terminal_extent(&first, &shown, &total);
        pixels = windows_buffer(table, own, slot);
        drawn = terminal.valid[slot] ? terminal.frame[slot] : null;

        // The cells are drawn through the grid's own top left, the paper round them through the buffer's.
        whole = pixels;
        pixels = terminal_grid(whole);
        screen = terminal.shown >= 0 && terminal.valid[terminal.shown] ? terminal.frame[terminal.shown] : null;

        // The bar is drawn where the buffer's own was not the same, and the compositor is
        // told where the one on the screen was not.
        bar = !terminal.valid[slot] || terminal.bar_first[slot] != first ||
              terminal.bar_shown[slot] != shown || terminal.bar_total[slot] != total;
        bar_told = !screen || terminal.bar_first[terminal.shown] != first ||
                   terminal.bar_shown[terminal.shown] != shown || terminal.bar_total[terminal.shown] != total;

        any = bar || bar_told;

        for (unsigned int row = 0; row < ROWS; row++)
        {
                const struct window_cell address_to line = now + (unsigned long)row * COLUMNS;
                unsigned int a0 = 0, a1 = COLUMNS, b0 = 0, b1 = COLUMNS;

                if (drawn)
                        terminal_span(line, drawn + (unsigned long)row * COLUMNS, &a0, &a1);
                if (screen)
                        terminal_span(line, screen + (unsigned long)row * COLUMNS, &b0, &b1);

                if (a1 > a0)
                {
                        terminal_row(pixels, row, line, drawn ? drawn + (unsigned long)row * COLUMNS : null, a0, a1);
                        terminal_margin(whole, now, row, a0, a1);
                        right |= a1 == COLUMNS;
                }
                any |= a1 > a0 || b1 > b0;

                // What the compositor reads again is what differs from what it shows: a cell
                // that is the same as on the screen is the same pixels in this buffer too,
                // however old the buffer was.
                row_x0[row & 1023] = b1 > b0 ? b0 : 0;
                row_x1[row & 1023] = b1 > b0 ? b1 : 0;
        }

        // Nothing differs from the buffer or from the screen: nothing is made or said.
        if (!any)
                return true;

        // A row's last cell is the paper right of it, which the bar is drawn on.
        if (bar || right)
        {
                terminal_bar(whole, now, first, shown, total);
                terminal.bar_first[slot] = first;
                terminal.bar_shown[slot] = shown;
                terminal.bar_total[slot] = total;
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

        terminal.pad = margin;
        terminal.bar = margin;
        terminal.pad_y = margin;
}

/*
        The faces, loaded from the files named. A font is the caller's bytes
        mapped for good; the engine and its atlas are mappings of their own,
        and only the pages a program draws with are ever resident.
*/
static b32 terminal_fonts(string_address address_to paths, unsigned int count, string_address bold,
                          unsigned int size)
{
        unsigned int faces = 0;
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);
        long atlas = compositor_call(syscall(mmap), 0, TERMINAL_ATLAS * TERMINAL_ATLAS, 3, 0x22, (positive)-1, 0);

        if ((engine < 0 && engine > -4096) || (atlas < 0 && atlas > -4096))
                return false;

        for (unsigned int at = 0; at <= count && faces < TERMINAL_FACES; at++)
        {
                string_address path = at < count ? paths[at] : bold;
                positive length = 0;
                positive bytes = path ? compositor_map_file(path, &length) : 0;

                if (!path)
                        continue;

                if (!bytes || font_load(&terminal.face[faces], (const p8 address_to)bytes, length, 0) != 0)
                {
                        string_format(log, "terminal: cannot use the font %s\n", path);
                        log_flush();
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

        terminal.engine = (font_engine address_to)engine;
        font_start(terminal.engine, (p8 address_to)atlas, TERMINAL_ATLAS, TERMINAL_ATLAS);
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
        address_to height = rows * terminal.cell_h + 2 * terminal.pad_y;
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
        return height > 2 * terminal.pad_y ? (height - 2 * terminal.pad_y) / terminal.cell_h : 1;
}

/*
        The window this program draws in, as large as a share of the output holds in whole cells, and where that
        share puts it: a row of the compositor's table taken, which says what the output is. The shares are
        percents, which is what lets one rule size a window on any screen; the compositor keeps it on the output.
        Never larger than the output, which is as large as a buffer is.
*/
static b32 terminal_open(unsigned int wide, unsigned int high, unsigned int left, unsigned int top,
                         unsigned int address_to columns, unsigned int address_to rows, const unsigned int address_to cells)
{
        struct windows address_to table;
        unsigned int display_w, display_h, pixels_w, pixels_h;

        if (windows_take(&terminal.own) < 0)
                return false;

        table = terminal.own.table;
        display_w = table->output_width;
        display_h = table->output_height;
        terminal.pitch = table->stride;
        address_to columns = terminal_columns(display_w * wide / 100);
        address_to rows = terminal_rows(display_h * high / 100);
        address_to columns = address_to columns < 20 ? COLUMNS_WANTED : address_to columns;
        address_to rows = address_to rows < 4 ? ROWS_WANTED : address_to rows;

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
        return true;
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
static void terminal_said(b32 reading, string_address what);

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
                for (const p8 address_to from = piece[at]; *from; from++)
                        text[length++] = *from;
                length += terminal_decimal(text + length, values[at]);
        }
        for (const p8 address_to from = piece[3]; *from; from++)
                text[length++] = *from;
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

static void terminal_zoom(b32 master, b32 reading, int by)
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
        terminal_text_said(reading);
}

/*
        windows_wait with the far end in it: the compositor's knock and the far end in one sleep, and nothing
        else, so an idle terminal wakes for neither. -1 once the compositor has gone.
*/
static long terminal_wait(long other, long nanoseconds)
{
        struct window_poll wanted[2] = {{(int)terminal.own.wake, 1, 0}, {(int)other, 1, 0}};
        long timeout[2] = {nanoseconds / 1000000000, nanoseconds % 1000000000};
        long ready = window_call(WINDOW_SYS_PPOLL, (long)wanted, 2, nanoseconds < 0 ? 0 : (long)timeout, 0, 0, 0);

        if (ready == -4) // EINTR
                return 0;

        if (ready > 0 && wanted[0].returned)
        {
                windows_drain(terminal.own.wake);
                if (wanted[0].returned & 16) // POLLHUP
                        return -1;
        }
        return ready;
}

// A key from the compositor, as the window's key ring would have had it.
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
                terminal_key(event, &key);
                if (key.flags & WINDOW_KEY_DOWN)
                {
                        // Typing is a reason to look at what is being typed.
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
                if (event->a == 0x110 && down && terminal.pointer_x >= (int)(COLUMNS * terminal.cell_w) &&
                    terminal_extent(&first, &shown, &total))
                {
                        unsigned int thumb = (unsigned int)((unsigned long)terminal_track() * shown / total);
                        unsigned int top = (unsigned int)((unsigned long)terminal_track() * first / total);

                        thumb = thumb < terminal.cell_h ? terminal.cell_h : thumb;
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
                int steps = event->b ? 0 : event->a;
                long scaled = (long)steps * TERMINAL_WHEEL_LINES + terminal.wheel_remainder;
                int lines = (int)(scaled / 120);

                terminal.wheel_remainder = (int)(scaled % 120);

                if (!lines)
                        return true;

                // A program that asked for the pointer is owed the wheel as buttons 64 and 65.
                if (mouse_mode)
                {
                        terminal_pointer_key(lines < 0 ? 65u : 64u, WINDOW_KEY_DOWN, &key);
                        return term_input(master, &key);
                }

                if (terminal_view_scroll(lines))
                        address_to dirty = true;
                return true;
        }
        }

        return true;
}

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
                terminal_title_set(name[0] ? (const char address_to)name : "shell");
        }

        if (window->title[0] && memory_compare(window->title, terminal.named, WINDOW_TITLE_MAX))
        {
                memory_copy(terminal.named, window->title, WINDOW_TITLE_MAX);
                terminal_title_set(terminal.named);
        }
}

// A line in the machine's log about this program, at its start and its end: its own standard output is nowhere.
static void terminal_said(b32 reading, string_address what)
{
        p8 line[96];
        positive length = 0;

        for (string_address name = (string_address)(reading ? "klog pid " : "terminal pid "); *name; name++)
                line[length++] = *name;
        line[length] = 0;
        compositor_kmsg(line, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), what);
}

static b32 klog_drain(b32 far, b32 address_to gone, positive budget);
static b32 klog_receive(const struct window_event address_to event, b32 address_to dirty);

/*
        The program, which is a shell on a pty, or with reading the kernel's log on /dev/kmsg (klog.c): the same
        cells, window, bar and wheel, a far end that is read and never written to, and keys that scroll.
*/
static b32 terminal_run(b32 reading)
{
        string_address paths[TERMINAL_FACES];
        string_address bold = null, ready_file = null;
        unsigned int cells[2] = {0, 0};
        unsigned int count = 0, size = 0, columns, rows;
        unsigned int max_columns, hung = reading;
        b32 master, gone = false, dirty = true, closing = false, held = false, paced;
        positive oom_kills = 0, sync_since = 0, stalled_since = 0, last_frame = 0, settle_since = 0;
        bipolar child = 0;
        b32 lost = false;
        struct windows address_to table;
        unsigned int own;

        claim_standard_descriptors();

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2] || !value)
                        continue;
                at++;
                if (option[1] == 'f' && count < TERMINAL_FACES)
                        paths[count++] = value;
                else if (option[1] == 'b')
                        bold = value;
                else if (option[1] == 's')
                        size = (unsigned int)compositor_number(value, 0);
                else if (option[1] == 'w')
                        ready_file = value;
                else if (option[1] == 'g')
                {
                        // COLUMNSxROWS
                        cells[0] = (unsigned int)compositor_number(value, 0);
                        for (positive at_x = 0; value[at_x]; at_x++)
                                if (value[at_x] == 'x')
                                        cells[1] = (unsigned int)compositor_number(value + at_x + 1, 0);
                }
        }

        // -s is the size asked for here; without it, the step the last zoom left, and the default.
        size = size ? size : terminal_size_read();
        size = size ? size : TERMINAL_SIZE;
        size = size < 6 ? 6 : size > 200 ? 200 : size;

        if (!terminal_fonts(paths, count, bold, size))
        {
                terminal_said(reading, " has no font to draw with (-f FONT.ttf)");
                return 1;
        }

        for (unsigned int at = 0; at < 256; at++)
                terminal_colour[at] = window_cell_colour(at);
        term_palette = terminal_palette;

        term_cell_w = terminal.cell_w;
        term_cell_h = terminal.cell_h;

        // Shares of the output, in percent: the log at the left and the terminal across from it, over the
        // log's right edge, so that the log's lines can be read and the terminal is the one in front.
        if (!(reading ? terminal_open(TERMINAL_LOG_WIDE, TERMINAL_LOG_HIGH, TERMINAL_LOG_LEFT, TERMINAL_LOG_TOP,
                                      &columns, &rows, cells)
                      : terminal_open(TERMINAL_WIDE, TERMINAL_HIGH, TERMINAL_LEFT, TERMINAL_TOP, &columns, &rows, cells)))
        {
                terminal_said(reading, " has no window: no compositor, or no row free in its table");
                return 1;
        }

        table = terminal.own.table;
        own = terminal.own.row;
        terminal.bar_grab = -1;

        // Named, because two untitled terminals are a guessing game.
        string_copy((string_address)table->title[own], (string_address)(reading ? "kernel log" : "shell"));

        // The ring is cut for the smallest text a zoom reaches, so that every size after this one has room in it.
        font_metrics least = font_metrics_at(terminal.plain[0], terminal_steps[0] << 6);

        max_columns = (table->output_width > TERMINAL_CEILING ? table->output_width : TERMINAL_CEILING) /
                      (unsigned int)least.cell_width;
        {
                unsigned int tallest = (table->output_height > TERMINAL_CEILING * 9 / 16 ? table->output_height
                                                                                       : TERMINAL_CEILING * 9 / 16) /
                                       (unsigned int)least.cell_height;

                unsigned int history = reading ? TERMINAL_LOG_HISTORY : TERMINAL_HISTORY;

                window = window_grid_open(columns, rows, max_columns, tallest * 2 + 1 > history ? tallest * 2 + 1 : history);
        }

        if (window)
                grid_take();

        if (!window || !terminal_frames())
        {
                terminal_said(reading, " has no memory for its cells");
                windows_leave(&terminal.own);
                return 1;
        }

        full_reset();

        if (reading)
        {
                // Not a terminal's cursor, and nothing to follow but the log: read without waiting, and waited for
                // with the compositor's events, which is what makes an idle log wake nothing.
                cursor_visible = false;
                master = (b32)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);
                child = (bipolar)master;
        }
        else
                child = term_session(&master, &oom_kills);

        if (child < 0)
        {
                terminal_said(reading, " cannot open its far end");
                windows_leave(&terminal.own);
                return 1;
        }

        cursor_show();
        terminal_frame();
        last_frame = clock_monotonic_nanoseconds();

        terminal_said(reading, " is on the screen");
        terminal_text_said(reading);

        // The first picture is committed: whoever is starting windows in an order may start the next.
        if (ready_file)
                system_close(system_open_at(AT_FDCWD, ready_file, FILE_WRITE | O_CLOEXEC));

        for (;;)
        {
                struct window_event event;
                long timeout = -1;
                positive now;
                b32 changed;

                // What the pty will say first; a held frame keeps its rows until it is let go.
                if (!synchronized_output)
                {
                        touched_top = ROWS;
                        touched_bottom = 0;
                }

                if (!reading)
                        term_follow_modes(master);

                // The zoom keys are the terminal's, whichever window it is: the kernel's log follows them too.
                while (windows_event(&terminal.own, &event))
                {
                        int zoom = terminal_zoom_key(&event);

                        if (zoom)
                        {
                                terminal_zoom(master, reading, zoom);
                                dirty = true;
                                continue;
                        }
                        if (!(reading ? klog_receive(&event, &dirty) : terminal_receive(master, &event, &dirty)))
                                gone = true;
                }

                // What the table says: whether this window has the keys, a size asked for, a close asked for.
                if (!reading && (table->focus == own) != terminal.focused)
                {
                        terminal.focused = table->focus == own;
                        term_focus(terminal.focused);
                        if (!term_send(master))
                                gone = true;
                }

                if (table->want_width[own] && table->want_height[own] &&
                    (table->want_width[own] != terminal.asked_w || table->want_height[own] != terminal.asked_h))
                {
                        terminal.asked_w = table->want_width[own];
                        terminal.asked_h = table->want_height[own];
                        terminal_configure(master, terminal.asked_w, terminal.asked_h);
                        dirty = true;
                }

                closing |= (table->state[own] & WINDOW_CLOSING) != 0;

                // The far end's own report.
                if (!gone && !term_send(master))
                        gone = true;

                changed = reading ? klog_drain(master, &gone, TERMINAL_BUDGET) : term_drain(master, &gone, TERMINAL_BUDGET);
                if (!reading && changed && !gone)
                        terminal_title(master, child);

                if (!gone && !term_send(master))
                        gone = true;

                // Being asked to go (Alt-F4, or the X of the kernel desktop's titlebar) is the line hanging up, once, to the group.
                if (!gone && !hung && closing)
                {
                        system_call_2(syscall(kill), (positive)(-child), SIGHUP);
                        hung = 1;
                }

                if (touched_bottom > touched_top)
                        changed = true;

                if (synchronized_output)
                {
                        now = clock_monotonic_nanoseconds();
                        sync_since = held ? sync_since : now;
                        held = true;

                        if (gone || now - sync_since >= (positive)TERMINAL_SYNC_NANOSECONDS)
                        {
                                synchronized_output = false;
                                changed = true;
                        }
                        else
                                timeout = (long)(TERMINAL_SYNC_NANOSECONDS - (now - sync_since));
                }
                else
                        held = false;

                dirty |= changed;

                /*
                        A frame is made at most once an output frame: more are made over
                        before they are shown, and a stream of output would be drawn six
                        hundred times a second where sixty are seen. The first after a quiet
                        time is made at once, so a key's echo does not wait; what comes
                        sooner than a frame after the last waits out the rest of it.
                */
                paced = false;

                /*
                        Output that has just ended a line -- the cursor at the
                        start of a row -- is most often the moment before the
                        shell's prompt, which comes a fraction of a millisecond
                        later in a write of its own: Enter's newline and then
                        the prompt. The frame waits a moment for it, so the
                        cursor is not seen at the left edge and then moved.
                        Typing never leaves the cursor there, so an echo is
                        not held.
                */
                if (dirty && !reading && changed && !column && !synchronized_output)
                {
                        now = clock_monotonic_nanoseconds();
                        settle_since = settle_since ? settle_since : now;
                        if (now - settle_since < (positive)TERMINAL_SETTLE_NANOSECONDS)
                        {
                                long left = (long)(TERMINAL_SETTLE_NANOSECONDS - (now - settle_since));

                                paced = true;
                                timeout = timeout < 0 || left < timeout ? left : timeout;
                        }
                }
                if (dirty && !synchronized_output && last_frame)
                {
                        positive period = table->period ? table->period : TERMINAL_FRAME_NANOSECONDS;
                        positive since = clock_monotonic_nanoseconds() - last_frame;

                        if (since < period)
                        {
                                paced = true;
                                timeout = (long)(period - since);
                        }
                }

                if (dirty && !synchronized_output && !paced)
                {
                        cursor_show();

                        if (terminal_frame())
                        {
                                dirty = false;
                                last_frame = clock_monotonic_nanoseconds();
                        }
                        else if (!stalled_since)
                                stalled_since = clock_monotonic_nanoseconds();
                        else if (clock_monotonic_nanoseconds() - stalled_since >= TERMINAL_STALL_NANOSECONDS)
                        {
                                // Waited for the compositor to take the last picture, and it has not: the buffer is taken.
                                terminal.forced = true;
                                stalled_since = 0;
                        }
                }

                if (!dirty)
                        stalled_since = settle_since = 0;

                // Being asked to go ends a log, which has no shell to hang up on.
                if (reading && closing)
                        gone = true;

                if (gone)
                        break;

                /*
                        Asleep until a key, a resize or the shell's output; and, with a frame
                        waiting for a buffer, until the compositor has let one go. With
                        nothing to do and a spare buffer held, once, for the time it takes
                        to be sure nothing is coming, after which the spare is given back and
                        the sleep is for ever: an idle terminal wakes no more.

                        The compositor having gone is the screen having gone: the shell is
                        hung up on, as it is when the window is closed, and what is left is
                        to see it out.
                */
                {
                        b32 spare = !dirty && !synchronized_output && terminal.shown >= 0 &&
                                    terminal.valid[1 - terminal.shown];
                        b32 stalled = dirty && !synchronized_output && terminal.shown >= 0 && !windows_ready(&terminal.own);
                        long ready = terminal_wait(master, spare && timeout < 0     ? TERMINAL_IDLE_NANOSECONDS
                                                          : stalled && timeout < 0 ? TERMINAL_STALL_NANOSECONDS
                                                                                   : timeout);

                        if (ready < 0)
                        {
                                if (!gone && !hung)
                                {
                                        system_call_2(syscall(kill), (positive)(-child), SIGHUP);
                                        hung = 1;
                                }
                                closing = true;
                                lost = true;
                                break;
                        }

                        if (!ready && spare && timeout < 0)
                                terminal_discard();


                }
        }

        // Said where the machine's log is, because nothing else is: this program's own output goes to
        // wherever the machine script put it, which is nowhere.
        terminal_said(reading, lost ? " ended: the compositor went away" : closing ? " ended: its window was closed"
                                                                         : " ended: its far end is over");

        if (reading)
                system_close(master);

        if (!reading && term_finish(child, master, hung, oom_kills))
        {
                // What is left of a window whose shell is gone: its cells, until the X.
                cursor_show();
                terminal_frame();

                while (!(table->state[own] & WINDOW_CLOSING))
                {
                        struct window_event event;

                        if (windows_wait(&terminal.own, -1, -1) < 0)
                                break;
                        while (windows_event(&terminal.own, &event))
                                ;
                }
        }

        windows_leave(&terminal.own);
        return 0;
}

static b32 terminal_main()
{
        return terminal_run(false);
}

#endif // MOONWATER_CANVAS_TERMINAL
