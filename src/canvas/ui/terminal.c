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
        same folding of a stored line at the width it is drawn in, the kit's
        scroll bar down the right where there is a scrollback, and the wheel
        and a drag on the bar to move over it. It owes
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
// A held synchronized frame is let go of after this long.
#define TERMINAL_SYNC_NANOSECONDS 200000000ll
// How long a frame waits when output has just ended a line, for what comes next on it: a prompt.
#define TERMINAL_SETTLE_NANOSECONDS 3000000ll
// A buffer that is not on the screen is given back after the terminal has been left alone this long.
#define TERMINAL_IDLE_NANOSECONDS 1000000000ll
// The longest a frame of the output lasts, when the compositor does not say.
#define TERMINAL_FRAME_NANOSECONDS 16666667ll
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
        // The paper round the grid: margin at least on every side, at the left (pad), where the grid ends at the right
        // (the cells fill the window's width, what is left over is the ground of the edge cells), and above and below
        // (pad_y), which also takes half of what is left of the window's height under its whole rows, so the grid sits in
        // the middle of it. The kit's scroll bar is drawn over the cells at the right edge, and takes no column.
        unsigned int cell_w, cell_h, pad, pad_y, margin;
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
        // The kit's scroll bar: its track and thumb as the kit draws them, and its hover and grab as the kit keeps them. Its place is
        // the body's (ui_body): the wheel's notches, a finger's drag and throw, and the grid's stretch all move the body.
        struct ui_scroll bar;
        struct ui_body body;

        // Where the view sits in the ring: a line and how many of its rows are above.
        unsigned int view, view_skip;

        unsigned int modifiers; // from the keys, which are the only events that say
        b32 pressed;
        int wheel_remainder;
        int wheel_notches; // the wheel in 120ths of a notch, while a program has the pointer
        /*
                A finger on the paper (a touch) with no program that asked for the pointer: the kit's finger (ui_finger) for its press,
                slop and speed. Where the grid scrolls its drag moves the body (ui_body_track, and ui_body_let_go at the lift). On the
                alternate screen its drag is the arrows', line by line as its pixels make lines (finger_left, what is under a line).
        */
        struct ui_finger finger;
        int finger_left;
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
        int shift[2];              // the pixels the grid was slid by in each buffer when it was last drawn (terminal_grid_shift): a slid buffer is drawn whole
        // The theme's default ink and paper the pictures were drawn in: a turn of the theme draws them whole again.
        unsigned int painted_ink, painted_paper;

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

        // The selection (terminal_select_*): the kit's controller (sel: the drag, its click count, the finger, the handles and the
        // bar), and its two ends as ring places (TERMINAL_PLACE per line): where the press was, and where the drag is. select_box is
        // an Alt press's rectangle, which the two ends make. overlay_top and overlay_bottom are the grid rows (pixels) the handles
        // and the bar covered in each buffer when it was last drawn: those rows are drawn again before the next frame's diff.
        struct ui_select sel;
        p64 sel_anchor, sel_active;
        b32 select_box;
        int overlay_top[2], overlay_bottom[2];
        // The caret's blink (ui_caret_cover, as a field's): its phase and the last key or move (ui_caret_wake); the caret's place
        // and whether it was shown when last seen; and the frame cell and cover each buffer holds, so a frame draws a caret whose
        // cover changed, as one whose cells changed.
        struct ui_caret caret;
        unsigned int caret_row, caret_column;
        b32 caret_seen;
        unsigned int caret_at[2], caret_cover[2], caret_marks[2];
        b32 caret_block[2];
        // While a frame is recorded into the window's list (terminal_list_frame): the picture's top left and the grid's, which the
        // recorder reads a fill's origin from (a fill of the grid is offset by the paper round it; one of the picture is not).
        unsigned int address_to rec_whole, address_to rec_grid;
        // A frame the recorder lost: what it was made of (terminal_list_key), and the frames since it was refused (TERMINAL_LIST_RETRY).
        b32 list_lost;
        unsigned long list_lost_key;
        unsigned int list_lost_frames;
} terminal = {.shown = -1, .view = TERMINAL_LIVE, .hold_timeout = -1, .caret_at = {~0u, ~0u}};

// The caret as the frame being made draws it: its index in the frame (~0 for none), its cover, and whether it is a block
// (its cell is turned round, so the two ways round are blended) and the marks a bar or an underline put on its cell.
static unsigned int terminal_caret_index = ~0u, terminal_caret_cover = UI_CARET_FULL;
static b32 terminal_caret_block;
static unsigned int terminal_caret_marks;

static unsigned int terminal_colour[256];

// The theme's default ink and paper, as 0xrrggbb: the roles the kit sets text and surfaces in, light or dark. A cell the program
// did not colour is drawn in them, and in reverse video the two trade places.
static unsigned int terminal_default_ink(void)
{
        return ui_role(UI_ON_SURFACE) & 0xffffffu;
}

static unsigned int terminal_default_paper(void)
{
        return ui_role(UI_SURFACE) & 0xffffffu;
}

// The ink and the paper a cell is drawn in: the palette's where the program set the colour, the theme's where it did not.
static unsigned int terminal_ink_of(const struct window_cell address_to cell)
{
        if (cell->flags & WINDOW_CELL_INK_SET)
                return terminal_colour[cell->ink];
        return cell->flags & WINDOW_CELL_SWAP ? terminal_default_paper() : terminal_default_ink();
}

static unsigned int terminal_paper_of(const struct window_cell address_to cell)
{
        if (cell->flags & WINDOW_CELL_PAPER_SET)
                return terminal_colour[cell->paper];
        return cell->flags & WINDOW_CELL_SWAP ? terminal_default_ink() : terminal_default_paper();
}

// What a program is answered for a colour index: the palette's, or for the default ink and paper (OSC 10 and 11), the theme's.
static unsigned int terminal_palette(unsigned int index)
{
        if (index == TERM_INDEX_INK)
                return terminal_default_ink();
        if (index == TERM_INDEX_PAPER)
                return terminal_default_paper();
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

static void terminal_bar_follow(void);

static b32 terminal_view_live(void)
{
        if (terminal.view == TERMINAL_LIVE)
                return false;

        terminal.view = TERMINAL_LIVE;
        terminal.view_skip = 0;
        terminal_bar_follow();
        return true;
}

// The view put where a count of rows above it says, and nothing else; false when it did not move. The kit's bar is the one that
// moves it while the wheel or a finger does (terminal_bar_out), so this is the only setter those paths use.
static b32 terminal_view_put(unsigned int above)
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

// The view put where a count of rows above it says, and the kit's bar put where the view is; false when it did not move.
static b32 terminal_view_set(unsigned int above)
{
        b32 moved = terminal_view_put(above);

        if (moved)
                terminal_bar_follow();
        return moved;
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

// The scale the kit draws at: the runner's, or 1 where there is no runner (the checks).
static int terminal_scale(void)
{
        return terminal.ui.scale > 0 ? terminal.ui.scale : 100;
}

// The pointer in the grid's pixels: the kit's pointer (window pixels, ui_kit.pointer_x and _y) less the paper round the grid. A
// button's event carries no place, so a press is at the last motion's place, which the kit keeps for every app.
static int terminal_at_x(void)
{
        return ui_kit.pointer_x - (int)terminal.pad;
}

static int terminal_at_y(void)
{
        return ui_kit.pointer_y - (int)terminal.pad_y;
}

// The kit's ui for the window's picture: the terminal's own engine and faces, at the scale, over the buffer whole.
static struct ui terminal_kit(unsigned int address_to whole)
{
        struct ui kit = terminal.ui;

        kit.pixels = whole;
        kit.pitch = terminal.pitch;
        kit.width = terminal.width;
        kit.height = terminal.height;
        kit.top = 0;
        kit.bottom = terminal.height;
        kit.band_top = kit.band_bottom = 0;
        kit.opaque = false;
        kit.scale = terminal_scale();
        return kit;
}

// The body's place in whole device pixels, unclamped (a stretch past an end is outside the content): where the grid is drawn, and the
// bar's thumb is the same place clamped to the track (ui_scroll_set).
static int terminal_body_px(void)
{
        return (int)((ui_body_drawn(&terminal.body) + UI_ONE / 2) >> UI_Q);
}

// The kit's scroll bar where the view is: its track down the window's right edge, flush with it, from below the close
// button to the window's bottom; the view's rows over the scrollback's, and first, the rows above the view. Its hover
// and grab stay as the kit keeps them, from one frame to the next.
static void terminal_bar_place(unsigned int first, unsigned int shown, unsigned int total)
{
        int cell = (int)terminal.cell_h;

        ui_scroll_place(&terminal.bar, (int)terminal.width - UI_SCROLL_WIDTH * terminal_scale() / 100, 0, (int)terminal.height,
                        UI_CLOSE_BOTTOM, (int)total * cell, (int)shown * cell, terminal_scale());
        ui_body_fit(&terminal.body, (b64)shown * cell * UI_ONE, (b64)total * cell * UI_ONE);
        // Where the view is, unless the body is moving (a notch's glide, a finger's hold or throw): then its place stands.
        if (!ui_body_going(&terminal.body) && terminal_body_px() != (int)first * cell)
                ui_body_set(&terminal.body, terminal.body.view, terminal.body.content, (b64)first * cell * UI_ONE, terminal.body.at);
        ui_scroll_set(&terminal.bar, terminal_body_px());
}

// The kit's bar follows the view: placed where the view's first row is, in pixels, and the body with it (not moving any more).
static void terminal_bar_follow(void)
{
        unsigned int first, shown, total;

        if (!terminal_extent(&first, &shown, &total) || total <= shown)
                return;
        terminal_bar_place(first, shown, total);
        ui_body_set(&terminal.body, terminal.body.view, terminal.body.content, (b64)first * terminal.cell_h * UI_ONE, terminal.body.at);
        ui_scroll_set(&terminal.bar, terminal_body_px());
}

// The grid's shift in device pixels, down positive: the body's place less the view's whole row, which is the overscroll past an end
// (the body's rubber band) and the part of a notch's glide between rows the view cannot draw. Nothing on the alternate screen, and
// nothing while a program has the pointer: it has no scrollback to stretch, and the wheel is its.
static int terminal_grid_shift(void)
{
        unsigned int first, shown, total;
        int shift;

        if (alternate || mouse_mode || !terminal_extent(&first, &shown, &total) || total <= shown)
                return 0;
        shift = (int)(first * terminal.cell_h) - terminal_body_px();
        return shift > (int)terminal.height ? (int)terminal.height : shift < -(int)terminal.height ? -(int)terminal.height : shift;
}

// The view is where the body is, to the nearest row (a row is a cell's height): the grid's only rounding of the body's place.
static b32 terminal_bar_out(void)
{
        int cell = (int)terminal.cell_h, at = terminal_body_px() < 0 ? 0 : terminal_body_px();

        return terminal_view_put((unsigned int)((at + cell / 2) / cell));
}

// A wheel's turn (a notch, or a trackpad's pixels) is a notch of the body: the grid glides to where the turns take it.
static void terminal_bar_turn(const struct window_event address_to event, b32 address_to dirty)
{
        unsigned int first, shown, total;

        if (!terminal_extent(&first, &shown, &total) || total <= shown)
                return;
        terminal_bar_place(first, shown, total);
        if (ui_body_wheel(terminal_scale(), &terminal.body, event, ui_kit_time()))
                *dirty = true;
}

// The body's motion, a frame: a notch's glide, a finger's throw, a pull's spring, and the view and the bar with it. A change of the
// grid's shift is a frame too, the last one of a spring included.
static void terminal_bar_step(void)
{
        int shift = terminal_grid_shift(), before = terminal.bar.scroll;

        ui_kit.busy = 0;
        ui_body_step(&terminal.body, ui_kit_time());
        ui_scroll_set(&terminal.bar, terminal_body_px());
        if (terminal_bar_out() || terminal.bar.scroll != before || terminal_grid_shift() != shift)
                terminal.dirty = true;
}

// What the bar is drawn in beyond its place: held and hovered, the two states the kit draws it in.
static unsigned int terminal_bar_key(void)
{
        return (terminal.bar.held ? 1u : 0u) | (terminal.bar.hover ? 2u : 0u);
}

// Whether a press at the pointer is the bar's: on its track where the bar is, or a finger's within its reach (ui_scroll_reach,
// which is wider than the bar drawn). The pointer is the grid's, so the window's place is the pointer's plus the grid's.
static b32 terminal_bar_takes(void)
{
        unsigned int first, shown, total;
        struct ui kit = terminal_kit(null);

        if (!terminal_extent(&first, &shown, &total) || total <= shown)
                return false;
        terminal_bar_place(first, shown, total);
        return ui_scroll_reach(&kit, &terminal.bar, ui_kit.pointer_x, ui_kit.pointer_y);
}

// The bar's pointer, through the kit's own (ui_scroll_pointer): a press, a release or a motion at x, y in the window.
// A button's event carries its code and no place, so a press passes where the pointer is. Answers whether the bar took
// it, and sets moved when the view or the bar's lit state went somewhere (the bar is drawn again).
static b32 terminal_bar_pointer(int type, int x, int y, b32 down, b32 address_to moved)
{
        unsigned int first, shown, total;
        struct ui kit = terminal_kit(null);
        unsigned int lit;
        int scroll, most;

        *moved = false;
        if (!terminal_extent(&first, &shown, &total) || total <= shown)
        {
                terminal.bar.hover = terminal.bar.held = 0;
                return false;
        }
        terminal_bar_place(first, shown, total);
        lit = terminal_bar_key();
        scroll = ui_scroll_pointer(&kit, &terminal.bar, type, x, y, down);
        *moved = terminal_bar_key() != lit;
        if (scroll < 0)
                return false;
        most = (int)(total - shown) * (int)terminal.cell_h;
        scroll = scroll > most ? most : scroll < 0 ? 0 : scroll;
        *moved = terminal_view_put((unsigned int)(scroll / (int)terminal.cell_h)) || *moved;
        terminal_bar_follow();
        return true;
}

static void terminal_select_mark(struct window_cell address_to out, unsigned int line, unsigned int from);

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
        const struct window_cell address_to caret = shown ? row_cells(shown_row) + shown_column : null;

        terminal.cursor_at = ~0u;
        terminal_caret_index = ~0u;

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
                        if (caret >= cells + from && caret < cells + from + used)
                                terminal_caret_index = row * COLUMNS + (unsigned int)(caret - (cells + from));
                        memory_fill_u64_aligned(out + used, COLUMNS - used, BLANK_CELL_WORD);
                        terminal_select_mark(out, line, from);
                }

                skip = 0;
                line++;
        }

        if (row < ROWS)
                memory_fill_u64_aligned(into + (unsigned long)row * COLUMNS, (ROWS - row) * COLUMNS,
                                        BLANK_CELL_WORD);
}

/*
        The caret's cover at now, from the field's own blink (ui.c's ui_caret_at): solid unless the window has the keys and a
        caret is shown. A key or a move restarts its phase (terminal_caret_restart, terminal_caret_step).
*/
static unsigned int terminal_caret_cover_at(positive now)
{
        positive due = 0;

        if (!shown || !terminal.focused)
                return UI_CARET_FULL;
        return ui_caret_cover(&terminal.caret, &due);
}

// The caret of the frame being made: read here, in its own function, since terminal_frame has a local named shown. With no
// caret in the frame there is no cover, shape or mark to draw, so none is kept.
static void terminal_caret_frame(void)
{
        terminal_caret_cover = terminal_caret_index == ~0u ? UI_CARET_FULL : terminal_caret_cover_at((positive)ui_kit.now);
        terminal_caret_block = terminal_caret_index != ~0u && shown && shown_block;
        terminal_caret_marks = terminal_caret_index != ~0u && shown ? shown_marks : 0;
}

// Whether a buffer holds the caret as this frame draws it: its cell, its cover, its shape and its marks.
static b32 terminal_caret_held(unsigned int slot)
{
        return terminal.caret_at[slot] == terminal_caret_index && terminal.caret_cover[slot] == terminal_caret_cover &&
               terminal.caret_block[slot] == terminal_caret_block && terminal.caret_marks[slot] == terminal_caret_marks;
}

static void terminal_caret_restart(positive now)
{
        ui_caret_wake(&terminal.caret);
}

// The caret's phase restarts when it moves; the loop's wait ends when its cover next changes.
static void terminal_caret_step(positive now, long address_to timeout)
{
        positive due = 0;

        if (shown != terminal.caret_seen || shown_row != terminal.caret_row || shown_column != terminal.caret_column)
        {
                terminal.caret_seen = shown, terminal.caret_row = shown_row, terminal.caret_column = shown_column;
                terminal_caret_restart(now);
        }
        if (!shown || !terminal.focused)
                return;
        // The wait ends where the cover changes, and an idle terminal has nothing else to draw it: the caret is repainted then,
        // as the frame would make it, or it stays at the cover the last frame drew (gone, in the off phase).
        if (terminal.shown >= 0 && (terminal_caret_index == ~0u ? UI_CARET_FULL : terminal_caret_cover_at(now)) != terminal.caret_cover[terminal.shown])
                terminal.dirty = true;
        ui_caret_cover(&terminal.caret, &due);
        if (due && (*timeout < 0 || (long)(due - now) < *timeout))
                *timeout = (long)(due - now);
}

// A colour the cover of the way from one to the other, channel by channel: from at cover 0, to at UI_CARET_FULL.
static unsigned int terminal_blend(unsigned int from, unsigned int to, unsigned int cover)
{
        unsigned int out = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                out |= ((((from >> shift) & 255) * (UI_CARET_FULL - cover) + ((to >> shift) & 255) * cover + UI_CARET_FULL / 2) /
                        UI_CARET_FULL) << shift;
        return out;
}

// A row's span of changed columns taken to cover one column as well: the column alone where none changed (a span of none
// is first and last both zero).
static void terminal_span_take(unsigned int address_to first, unsigned int address_to last, unsigned int column)
{
        if (*last > *first)
                *first = *first < column ? *first : column, *last = *last > column + 1 ? *last : column + 1;
        else
                *first = column, *last = column + 1;
}

/*
        The columns of a row that differ: the first and one past the last, or
        both zero when they are the same. A wide character is two cells that
        change together, so a span never starts on the second; and a half of
        one whose other half changed is drawn again too, since a half alone is
        drawn blank (a delete of characters from the right half of a pair left
        the left half's ink on the screen, where a full draw has none).
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

        if (start && ((now[start].flags & WINDOW_CELL_WIDE_RIGHT) || (now[start - 1].flags & WINDOW_CELL_WIDE)))
                start--;

        if (stop < COLUMNS && ((now[stop - 1].flags & WINDOW_CELL_WIDE) || (now[stop].flags & WINDOW_CELL_WIDE_RIGHT)))
                stop++;

        address_to first = start;
        address_to last = stop;
}

/* ---- the pixels ---- */

// SGR dim, the program's own attribute on its text: content, which the kit has no role for, so it stays.
static unsigned int terminal_mix(unsigned int ink, unsigned int paper)
{
        return ((ink & 0xfefefe) >> 1) + ((paper & 0xfefefe) >> 1);
}

/*
        A rectangle of the window in one colour, clipped to it: the part of
        a cell a style paints over its glyph, or the ground of a run of blanks.
*/
// Whether a frame is being recorded into the window's list (terminal_list_frame): the kit's scene is set then, and its fills and
// glyph runs are ops, and no pixel is written.
static b32 terminal_recording(void)
{
        return terminal.ui.scene != null;
}

static void terminal_fill(unsigned int address_to pixels, int x, int y, unsigned int width,
                          unsigned int height, unsigned int colour)
{
        unsigned int x2 = x + width > terminal.width ? terminal.width : x + width;
        unsigned int y2 = y + height > terminal.height ? terminal.height : y + height;

        if ((unsigned int)x >= x2 || (unsigned int)y >= y2)
                return;
        if (terminal_recording())
        {
                // The grid's fills are offset by the paper round it; the picture's (the margins and the bar) are not. Any other is lost.
                int ox = pixels == terminal.rec_grid ? (int)terminal.pad : 0, oy = pixels == terminal.rec_grid ? (int)terminal.pad_y : 0;

                if (pixels != terminal.rec_grid && pixels != terminal.rec_whole)
                {
                        terminal.ui.scene->lost = UI_LOST_TERM_GRID;
                        return;
                }
                ui_scene_record(terminal.ui.scene, ox + x, oy + y, ox + (int)x2, oy + (int)y2, colour, true);
                return;
        }

        memory_fill_u32_rect(pixels + (positive)y * terminal.pitch + x, terminal.pitch,
                             x2 - x, y2 - y, colour);
}

// A frame drawn whole (every row, its margins, the bar) slid by shift device pixels, down positive: the rows move, and the strip
// they leave is the paper of the edge they were pulled from (the window's first pixel at the top, its last at the bottom). The
// rows that move past the window's edge are cut, as a window cuts what it draws.
static void terminal_grid_slide(unsigned int address_to whole, int shift)
{
        unsigned int height = terminal.height, pitch = terminal.pitch, width = terminal.width;
        unsigned int up = shift < 0 ? (unsigned int)-shift : (unsigned int)shift;

        if (!shift)
                return;
        if (up >= height)
        {
                terminal_fill(whole, 0, 0, width, height, whole[0]);
                return;
        }
        if (shift > 0)
        {
                unsigned int colour = whole[0];

                for (unsigned int y = height; y-- > up;)
                        memory_copy(whole + (positive)y * pitch, whole + (positive)(y - up) * pitch, (positive)width * sizeof(unsigned int));
                terminal_fill(whole, 0, 0, width, up, colour);
        }
        else
        {
                unsigned int colour = whole[(positive)(height - 1) * pitch];

                for (unsigned int y = 0; y + up < height; y++)
                        memory_copy(whole + (positive)y * pitch, whole + (positive)(y + up) * pitch, (positive)width * sizeof(unsigned int));
                terminal_fill(whole, 0, (int)(height - up), width, up, colour);
        }
}

// What a style paints over the cell: an underline, a strike and the bar.
static void terminal_decorate(unsigned int address_to pixels, const struct window_cell address_to cell,
                              int x, int y, unsigned int cells, unsigned int ink, unsigned int caret)
{
        unsigned int width = terminal.cell_w * cells;
        unsigned int thick = (unsigned int)((terminal.grid.thickness + 0x8000) >> 16);
        int rule;

        thick = thick ? thick : terminal.cell_h / 16 ? terminal.cell_h / 16 : 1;
        // A caret's own bar or underline is as wide as the caret setting says (caret is that width in device pixels, 0 for none).
        thick = caret ? caret : thick;

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
                terminal_fill(pixels, x, y, caret ? caret : terminal.cell_w / 8 ? terminal.cell_w / 8 : 1,
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

// A glyph run recorded (terminal_recording): the codes of the run, on one ground and one face, as font_cells draws them; the cells are the
// grid's, so its origin is the paper's pad. The painter's engine writes the ops into the scene (ui_scene_cells).
static void terminal_rec_glyphs(struct terminal_painter address_to painter, unsigned int address_to pixels, unsigned int from,
                                unsigned int count, b32 heavy, p32 flags, int y, unsigned int ink, unsigned int paper)
{
        font_target target = {pixels, terminal.pitch, (b32)terminal.width, (b32)terminal.height};

        if (pixels != terminal.rec_grid)
        {
                terminal.ui.scene->lost = UI_LOST_TERM_FONT;
                return;
        }
        painter->ui.scene = terminal.ui.scene;
        ui_scene_cells(&painter->ui, heavy ? terminal.bold : terminal.plain, heavy ? terminal.bold_count : terminal.plain_count, flags,
                       painter->codes, count, &terminal.grid, terminal.size, &target, (b32)terminal.pad, (b32)terminal.pad_y,
                       (b32)(from * terminal.cell_w), y, ink, paper);
        painter->ui.scene = null;
}

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

//      The cells terminal_row takes one at a time on the plainest path, in one loop. A plain cell has none of the
//      flags that change how it is drawn (a wide half, a hidden or selected or dimmed one, a line through it, a handle),
//      and it differs from the buffer it is drawn over. From at to end, while the cell's paper is the run's paper:
//      a glyph whose ink and style (bold and italic bits) are the run's is appended to codes, and a blank is appended
//      as a space when codes is set; with codes null (a run of blanks) a glyph ends the scan and a blank is counted.
//      The count is what terminal_row adds to its run, so each cell it takes is the one its own path would have taken.
#define TERMINAL_PLAIN_OUT (WINDOW_CELL_WIDE | WINDOW_CELL_WIDE_RIGHT | WINDOW_CELL_HIDDEN | WINDOW_CELL_SELECTED | \
                            WINDOW_CELL_DIM | WINDOW_CELL_UNDERLINE | WINDOW_CELL_STRIKE | WINDOW_CELL_BAR)
#define TERMINAL_STYLE_BITS (WINDOW_CELL_BOLD | WINDOW_CELL_ITALIC)

struct terminal_plain
{
        unsigned int ink, paper, bits;
};

static unsigned int terminal_plain_scan(const struct window_cell address_to now, const struct window_cell address_to old,
                                        unsigned int at, unsigned int limit, p32 address_to codes,
                                        const struct terminal_plain address_to run)
{
        unsigned int n = 0;

        for (; at + n < limit; n++)
        {
                const struct window_cell address_to cell = now + at + n;

                if (cell->flags & TERMINAL_PLAIN_OUT)
                        break;
                if (old && *(const terminal_word address_to)cell == *(const terminal_word address_to)(old + at + n))
                        break;
                if (terminal_paper_of(cell) != run->paper)
                        break;
                if (cell->character > ' ')
                {
                        if (!codes || terminal_ink_of(cell) != run->ink || (cell->flags & TERMINAL_STYLE_BITS) != run->bits)
                                break;
                        codes[n] = cell->character;
                }
                else if (codes)
                        codes[n] = ' ';
        }
        return n;
}

// The scan for a glyph run that holds glyphs codes already: as many more cells as the run has room for.
static unsigned int terminal_plain_glyphs(const struct window_cell address_to now, const struct window_cell address_to old,
                                          unsigned int at, unsigned int last, p32 address_to codes, unsigned int glyphs,
                                          const struct terminal_plain address_to run)
{
        unsigned int limit = FONT_RUN - glyphs < last - at ? at + (FONT_RUN - glyphs) : last;

        return terminal_plain_scan(now, old, at, limit, codes + glyphs, run);
}

static void terminal_row(struct terminal_painter address_to painter, unsigned int address_to pixels, unsigned int row,
                         const struct window_cell address_to now,
                         const struct window_cell address_to old, unsigned int first,
                         unsigned int last)
{
        font_target target = {pixels, terminal.pitch, (b32)terminal.width, (b32)terminal.height};
        int y = (int)(row * terminal.cell_h);
        unsigned int run = 0, run_from = 0, run_colour = 0;
        unsigned int glyphs = 0, glyphs_from = 0, glyphs_ink = 0, glyphs_paper = 0, glyphs_bits = 0;
        b32 glyphs_heavy = 0;
        p32 glyphs_flags = 0;
        // The caret's cell in this row, if it is in the row (~0 for none): it is never a plain cell, so it is drawn at its
        // cover whatever the buffer holds.
        unsigned int caret = terminal_caret_index != ~0u && terminal_caret_index / COLUMNS == row ? terminal_caret_index % COLUMNS : ~0u;
        unsigned int caret_cover = terminal_caret_cover;

// The run, less the blanks it ends with, which are a fill: a line's trailing blanks would otherwise be
// made pixels a coverage byte at a time.
#define TERMINAL_GLYPHS()                                                                              \
        do                                                                                             \
        {                                                                                              \
                unsigned int trail = 0;                                                                \
                if (terminal_recording())                                                              \
                {                                                                                      \
                        if (glyphs)                                                                    \
                                terminal_rec_glyphs(painter, pixels, glyphs_from, glyphs, glyphs_heavy, glyphs_flags, y, glyphs_ink, glyphs_paper); \
                        glyphs = 0;                                                                    \
                        break;                                                                         \
                }                                                                                      \
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
                unsigned int ink = terminal_ink_of(cell);
                unsigned int paper = terminal_paper_of(cell);
                // The plain scans stop at the caret, which is drawn by itself.
                unsigned int stop = caret > at && caret < last ? caret : last;
                b32 here = at == caret;

                // A selected cell lies on the theme's selection colour, which is a tint over its own paper, blended as the kit
                // blends its fills (the paper is the program's, the tint the kit's; the alpha stays as the buffer's, 0).
                if (cell->flags & WINDOW_CELL_SELECTED)
                        paper = ui_over(ui_premultiplied(ui_role(UI_SELECTION), UI_FULL), paper) & 0xffffffu;
                // A block caret is its cell turned round in the frame, so its two ways round are blended by the cover: at the
                // full cover the cell as it is, at none the cell as the program wrote it.
                if (here && terminal_caret_block)
                {
                        unsigned int on_ink = ink, on_paper = paper;

                        ink = terminal_blend(on_paper, on_ink, caret_cover);
                        paper = terminal_blend(on_ink, on_paper, caret_cover);
                }
                // A wide character is both its cells; one cut by the edge of a row, or whose
                // other half is on another row of a folded line, is blank here.
                b32 pair = (cell->flags & WINDOW_CELL_WIDE) && at + 1 < COLUMNS &&
                           (cell[1].flags & WINDOW_CELL_WIDE_RIGHT);
                unsigned int cells = pair ? 2 : 1;
                b32 glyph = cell->character > ' ' && !(cell->flags & WINDOW_CELL_HIDDEN) &&
                            !(cell->flags & (WINDOW_CELL_WIDE | WINDOW_CELL_WIDE_RIGHT) && !pair);
                b32 decor = (cell->flags & (WINDOW_CELL_UNDERLINE | WINDOW_CELL_STRIKE | WINDOW_CELL_BAR)) != 0;

                // The same as the buffer has, and so are the halves it pairs with: a half whose other half changed
                // is drawn again, whole or blank as it now is.
                if (old && !here && *(const terminal_word address_to)cell == *(const terminal_word address_to)(old + at) &&
                    (!(cell->flags & WINDOW_CELL_WIDE) || at + 1 >= COLUMNS ||
                     *(const terminal_word address_to)(cell + 1) == *(const terminal_word address_to)(old + at + 1)) &&
                    (!(cell->flags & WINDOW_CELL_WIDE_RIGHT) || !at ||
                     *(const terminal_word address_to)(cell - 1) == *(const terminal_word address_to)(old + at - 1)))
                {
                        at += cells - 1;
                        glyph = decor = false;
                        cells = 0;
                }

                if (glyph && cells == 1 && !decor && !here)
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
                                glyphs_bits = cell->flags & TERMINAL_STYLE_BITS;
                        }
                        painter->codes[glyphs++] = cell->character;
                        {
                                struct terminal_plain key = {glyphs_ink, glyphs_paper, glyphs_bits};
                                unsigned int taken = terminal_plain_glyphs(now, old, at + 1, stop, painter->codes, glyphs, &key);

                                glyphs += taken;
                                at += taken;
                        }
                        continue;
                }
                // A blank between glyphs on the run's ground is a cell of the run: a space no
                // longer ends it, so a line of words in one colour is one font_cells and not one
                // a word, each with its own setup and its own narrow rows to make pixels.
                if (glyphs && cells == 1 && !glyph && !decor && !here && paper == glyphs_paper && glyphs_from + glyphs == at &&
                    glyphs < FONT_RUN)
                {
                        painter->codes[glyphs++] = ' ';
                        {
                                struct terminal_plain key = {glyphs_ink, glyphs_paper, glyphs_bits};
                                unsigned int taken = terminal_plain_glyphs(now, old, at + 1, stop, painter->codes, glyphs, &key);

                                glyphs += taken;
                                at += taken;
                        }
                        continue;
                }
                TERMINAL_GLYPHS();

                if (cells && !glyph && !decor && !here)
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
                        {
                                struct terminal_plain key = {0, run_colour, 0};
                                unsigned int taken = terminal_plain_scan(now, old, at + 1, stop, null, &key);

                                run += taken;
                                at += taken;
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

                if (pair && terminal_recording())
                        terminal.ui.scene->lost = UI_LOST_TERM_WIDE; // a wide character is two cells: the recorder does not draw one yet

                if (glyph && terminal_recording())
                {
                        b32 heavy = (cell->flags & WINDOW_CELL_BOLD) && terminal.bold_count;

                        painter->codes[0] = cell->character;
                        painter->ui.scene = terminal.ui.scene;
                        ui_scene_cells(&painter->ui, heavy ? terminal.bold : terminal.plain, heavy ? terminal.bold_count : terminal.plain_count,
                                       terminal_style(cell), painter->codes, 1, &terminal.grid, terminal.size, &target, (b32)terminal.pad,
                                       (b32)terminal.pad_y, (b32)(at * terminal.cell_w), y, ink, paper);
                        painter->ui.scene = null;
                }
                else if (glyph)
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

                // A bar or an underline caret: its own marks are the cover's blend of ink and paper (none at cover 0, so the
                // glyph under it is not painted over), and the cell's own marks are as they were.
                if (decor && here && !terminal_caret_block)
                {
                        struct window_cell rest = *cell, own = *cell;
                        int left = (int)(at * terminal.cell_w);

                        rest.flags = (unsigned short)(cell->flags & ~terminal_caret_marks);
                        terminal_decorate(pixels, &rest, left, y, cells, ink, 0);
                        if (caret_cover)
                        {
                                own.flags = (unsigned short)(cell->flags & terminal_caret_marks);
                                terminal_decorate(pixels, &own, left, y, cells, terminal_blend(paper, ink, caret_cover),
                                                  (unsigned int)(ui_caret_thick() * terminal_scale() / 100));
                        }
                }
                else if (decor)
                        terminal_decorate(pixels, cell, (int)(at * terminal.cell_w), y, cells, ink, 0);

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

        return at == terminal.cursor_at ? terminal_ink_of(frame + at) : terminal_paper_of(frame + at);
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

        // The top and the bottom paper are one column loop each: a middle row has neither, and walking the columns for
        // nothing was most of the margin's cost (every row walks every column). The rectangles do not overlap.
        if (!row)
                for (unsigned int column = first; column < last; column++)
                        terminal_fill(whole, (int)(terminal.pad + column * terminal.cell_w), 0, terminal.cell_w,
                                      terminal.pad_y, terminal_ground(frame, 0, column));
        if (row + 1 == ROWS && terminal.height > below)
                for (unsigned int column = first; column < last; column++)
                        terminal_fill(whole, (int)(terminal.pad + column * terminal.cell_w), (int)below, terminal.cell_w,
                                      terminal.height - below, terminal_ground(frame, row, column));
}

// The first column the bar's track covers, one column earlier for a wide character's first cell: from there to the last
// column is what a bar drawn over the grid's right edge can cover, so those cells are drawn again with it.
static unsigned int terminal_bar_from(void)
{
        int left = (int)terminal.width - UI_SCROLL_WIDTH * terminal_scale() / 100 - (int)terminal.pad;
        unsigned int column = left > 0 ? (unsigned int)left / terminal.cell_w : 0;

        column = column ? column - 1 : 0;
        return column < COLUMNS ? column : COLUMNS - 1;
}

/*
        The right of the grid: each row's paper, from its last cell, to the
        window's right edge, and the kit's scroll bar over the cells at that
        edge where the view has a scrollback. The kit draws the bar
        (ui_scroll_draw) in its own colours and at its own width, and takes the
        pointer on the same track (terminal_bar_pointer), so what is drawn and
        what is pressed agree. The bar takes no column: the last column's cells
        are drawn again whenever the bar is, so the thumb's old place comes back.
*/
static void terminal_bar(unsigned int address_to whole, const struct window_cell address_to frame, unsigned int first,
                         unsigned int shown, unsigned int total)
{
        unsigned int x = terminal.pad + COLUMNS * terminal.cell_w, top, height;
        struct ui kit;

        for (unsigned int row = 0; row < ROWS; row++)
        {
                terminal_band(row, &top, &height);
                terminal_fill(whole, (int)x, (int)top, terminal.width > x ? terminal.width - x : 0, height,
                              terminal_ground(frame, row, COLUMNS - 1));
        }

        if (terminal.width < UI_SCROLL_WIDTH * (unsigned int)terminal_scale() / 100 || total <= shown)
                return;

        kit = terminal_kit(whole);
        terminal_bar_place(first, shown, total);
        ui_scroll_draw(&kit, &terminal.bar);
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
        b32 over; // the bar is drawn over these cells: each cell is painted, none is taken as the buffer's already (the thumb is in it)
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
                             banding->drawn && !banding->over ? banding->drawn + (unsigned long)row * COLUMNS : null, a0, a1);
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
        The theme's default ink and paper are what a picture's default cells are drawn in, so a picture drawn in the other pair
        is not a picture of these cells. When the pair has changed (a theme turned, which the kit does a frame at a time) both
        buffers are taken back, and the next frames draw every cell whole. The cells are the same, so the text, the cursor and
        the selection stay where they were.
*/
static void terminal_paint_theme(void)
{
        unsigned int ink = terminal_default_ink(), paper = terminal_default_paper();

        if (ink == terminal.painted_ink && paper == terminal.painted_paper)
                return;
        terminal.valid[0] = terminal.valid[1] = 0;
        terminal.painted_ink = ink;
        terminal.painted_paper = paper;
}

/*
        One frame: the cells that differ drawn into the free buffer, what
        the compositor has to read again named, and the picture shown.
        False when there is no buffer to draw into yet, and nothing was done.
*/
static b32 terminal_overlay_wanted(void);
static void terminal_overlay_spoil(struct window_cell address_to drawn, int slot);
static void terminal_overlay(unsigned int address_to whole, int slot);

/*
        The whole frame recorded into the window's list (ui_list_wanted: the compositor draws lists and has not refused this window's
        last): every row of cells, the paper round the grid and the bar, drawn through the painter as the recorder (ui_scene_cells
        and the fills route to the scene). Nothing is written to a picture; ui_list_frame_end publishes the list, and the compositor
        draws it into buffer 0 and shows it. A frame that did not record whole (a wide character, a room that is full) is discarded,
        and the frame is painted on the processor as it always was. After a list frame neither picture matches what the processor
        last drew, so both are drawn whole by the next processor frame.
*/
// The frame's ops into the scene the kit's ui records into (ui_scene_begin set it): the whole grid, its margins and the bar, each
// row through the painter. The ui's size is the picture's, which the list's header carries (ui_draw_list_publish), and its band is
// the whole picture (the kit's dots and rings are cut to it). Nothing is written to the picture.
static void terminal_list_draw(const struct window_cell address_to now, unsigned int first, unsigned int shown, unsigned int total)
{
        struct windows address_to table = terminal.own.table;
        unsigned int address_to whole = windows_buffer(table, terminal.own.row, 0);

        terminal.ui.width = (int)terminal.width, terminal.ui.height = (int)terminal.height, terminal.ui.pitch = terminal.pitch;
        terminal.ui.top = 0, terminal.ui.bottom = (int)terminal.height;
        terminal.rec_whole = whole;
        terminal.rec_grid = terminal_grid(whole);
        for (unsigned int row = 0; row < ROWS; row++)
        {
                terminal_row(&terminal_painters[0], terminal.rec_grid, row, now + (unsigned long)row * COLUMNS, null, 0, COLUMNS);
                terminal_margin(whole, now, row, 0, COLUMNS);
        }
        terminal_bar(whole, now, first, shown, total);
        // The handles and the bar: the kit draws them from the grid's origin, so the scene's ops are placed at the grid (its origin in the
        // picture: pad right, pad_y down), as the processor draws them there.
        if (terminal_overlay_wanted() && terminal.ui.scene)
        {
                terminal.ui.scene->ox = (int)terminal.pad, terminal.ui.scene->oy = (int)terminal.pad_y;
                terminal_overlay(whole, 0);
                terminal.ui.scene->ox = terminal.ui.scene->oy = 0;
        }
        terminal.rec_whole = terminal.rec_grid = null;
}

// What a list is made of, as a hash: the cells, the grid's size and scale, the paper and ink of the theme. A list the recorder lost
// is lost again for the same key (a wide character, the room, the pool's tables), so it is not recorded again while the key holds.
#define TERMINAL_LIST_RETRY 60
static unsigned long terminal_list_key(const struct window_cell address_to now)
{
        const terminal_word address_to words = (const terminal_word address_to)now;
        unsigned long count = (unsigned long)ROWS * COLUMNS * sizeof(struct window_cell) / sizeof(terminal_word), hash = 1469598103934665603ul;

        for (unsigned long at = 0; at < count; at++)
                hash = (hash ^ (unsigned long)words[at]) * 1099511628211ul;
        hash = (hash ^ terminal.size) * 1099511628211ul;
        hash = (hash ^ terminal.width) * 1099511628211ul;
        hash = (hash ^ terminal.height) * 1099511628211ul;
        hash = (hash ^ (unsigned long)terminal_scale()) * 1099511628211ul;
        hash = (hash ^ terminal_default_ink()) * 1099511628211ul;
        hash = (hash ^ terminal_default_paper()) * 1099511628211ul;
        return hash;
}

static b32 terminal_list_frame(const struct window_cell address_to now, unsigned int first, unsigned int shown, unsigned int total)
{
        if (!ui_list_frame_begin())
                return false;
        terminal_list_draw(now, first, shown, total);
        if (!ui_list_frame_end(&terminal.own, (int)terminal.width, (int)terminal.height, null))
                return false;
        terminal.valid[0] = terminal.valid[1] = 0;
        terminal.margin_spoiled[0] = terminal.margin_spoiled[1] = true;
        terminal.forced = false;
        terminal.shown = 0;
        return true;
}

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
        b32 bar, bar_told, any, right = false, turning, full;
        int shift;

        terminal_paint_theme();
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
        terminal_caret_frame();
        terminal_extent(&first, &shown, &total);
        // A grid shifted (a stretch past an end, a glide between rows) is drawn whole, every row and the bar; so is a buffer that
        // still holds a shifted grid, which the incremental draw would otherwise diff against cells it does not show.
        // The bar is put where the view is first (unless it is moving), so the shift reads the bar's place of this frame.
        if (total > shown)
                terminal_bar_place(first, shown, total);
        shift = terminal_grid_shift();
        full = shift != 0 || terminal.shift[slot] != 0;
        // A frame the compositor draws from the window's list: no shift (the list has no copy within a picture), and the last picture taken
        // (windows_ready), so nothing it reads is written. The selection's handles and bar are recorded with the grid (terminal_list_draw).
        // A list the recorder lost is not recorded again while what it was made of holds (terminal_list_key): the frames after it are
        // the processor's until the cells, the size or the theme change, or TERMINAL_LIST_RETRY frames have gone by.
        if (!shift && windows_ready(&terminal.own) && ui_list_wanted(&terminal.own))
        {
                unsigned long key = terminal_list_key(now);

                if (terminal.list_lost && key == terminal.list_lost_key && terminal.list_lost_frames < TERMINAL_LIST_RETRY)
                        terminal.list_lost_frames++;
                else if (terminal_list_frame(now, first, shown, total))
                {
                        terminal.list_lost = 0;
                        return true;
                }
                else
                        terminal.list_lost = 1, terminal.list_lost_key = key, terminal.list_lost_frames = 0;
        }
        pixels = windows_buffer(table, own, slot);
        drawn = terminal.valid[slot] ? terminal.frame[slot] : null;
        if (drawn)
        {
                terminal_spoiled(drawn, slot);
                terminal_overlay_spoil(drawn, slot);
        }

        // The cells are drawn through the grid's own top left, the paper round them through the buffer's.
        whole = pixels;
        pixels = terminal_grid(whole);

        // The bar is drawn where the buffer's own was not the same, and the compositor is
        // told where the one on the screen was not. A thumb growing or shrinking across its track is drawn again each frame.
        turning = total > shown && ui_scroll_moving(&terminal.bar);
        bar = !terminal.valid[slot] || terminal.bar_first[slot] != first || terminal.bar_shown[slot] != shown ||
              terminal.bar_total[slot] != total || terminal.bar_drawn[slot] != terminal_bar_key() || turning;
        bar_told = !screen || terminal.bar_first[front] != first || terminal.bar_shown[front] != shown ||
                   terminal.bar_total[front] != total || terminal.bar_drawn[front] != terminal_bar_key() || turning;

        // A top margin a spoil reached is drawn again, which makes the frame whether or not a cell changed.
        b32 margin = terminal.margin_spoiled[slot];

        bar = bar || full;
        bar_told = bar_told || full;
        any = bar || bar_told || margin || terminal_overlay_wanted() || terminal.overlay_bottom[slot] > terminal.overlay_top[slot];

        positive changed = 0;
        unsigned int rows = ROWS < TERMINAL_SPAN_ROWS ? ROWS : TERMINAL_SPAN_ROWS, from = terminal_bar_from();
        // The thumb is drawn over the last column's cells, translucent at its edges, so it is drawn over fresh cells every
        // frame it is there: a thumb drawn again over its own last frame would blend twice.
        b32 visible = total > shown;

        for (unsigned int row = 0; row < rows; row++)
        {
                const struct window_cell address_to line = now + (unsigned long)row * COLUMNS;
                unsigned int a0 = 0, a1 = COLUMNS, b0 = 0, b1 = COLUMNS;

                if (drawn)
                        terminal_span(line, drawn + (unsigned long)row * COLUMNS, &a0, &a1);
                if (screen)
                        terminal_span(line, screen + (unsigned long)row * COLUMNS, &b0, &b1);

                // A caret the buffer, or the screen, does not hold as this frame draws it is drawn again, though its cells are the same.
                if (terminal_caret_index != ~0u && terminal_caret_index / COLUMNS == row)
                {
                        unsigned int column = terminal_caret_index % COLUMNS;

                        if (drawn && !terminal_caret_held(slot))
                                terminal_span_take(&a0, &a1, column);
                        if (screen && !terminal_caret_held(front))
                                terminal_span_take(&b0, &b1, column);
                }

                // A whole frame draws every row: the cells the buffer holds are not diffed against, and the spans are the row.
                if (full)
                        a0 = 0, a1 = COLUMNS;

                // The bar is drawn over the cells at the window's right edge, so when it is drawn again (it moved, grew,
                // or went, or there is a thumb to draw) those cells are painted again too: the cells under its old thumb
                // come back, and it is drawn on them.
                if (bar || visible)
                {
                        // A row that did not change has the span 0 to 0, which is not a first column of 0: the cells the bar
                        // covers are drawn again, and not the whole row. They start where a span starts (terminal_span): never
                        // on the second half of a wide character, which drawn alone is a blank.
                        unsigned int start = from && ((line[from].flags & WINDOW_CELL_WIDE_RIGHT) || (line[from - 1].flags & WINDOW_CELL_WIDE))
                                                 ? from - 1
                                                 : from;

                        a0 = a1 > a0 && a0 < start ? a0 : start;
                        a1 = COLUMNS;
                }

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
                struct terminal_banding banding = {pixels, whole, now, drawn, rows, 1, bar || visible || full};

                terminal_cells_draw(&banding, changed);
        }

        if (margin)
        {
                terminal_margin(whole, now, 0, 0, COLUMNS);
                terminal.margin_spoiled[slot] = false;
        }

        // The grid slid past its place: the rows drawn above are moved, and the paper they leave is filled (before the bar,
        // which is the window's own and not the grid's).
        terminal_grid_slide(whole, shift);

        // Nothing differs from the buffer or from the screen: nothing is made or said.
        if (!any)
        {
                terminal.forced = false;
                return true;
        }

        // A row's last cell is the paper right of it, which the bar is drawn on.
        if (bar || right || visible)
        {
                terminal_bar(whole, now, first, shown, total);
                terminal.bar_first[slot] = first;
                terminal.bar_shown[slot] = shown;
                terminal.bar_total[slot] = total;
                terminal.bar_drawn[slot] = terminal_bar_key();
        }

        // The handles and the bar go over the cells and the scrollbar, last in the buffer.
        terminal_overlay(whole, slot);

        // What the compositor reads again: one rectangle round the rows and the bar that differ from what
        // it shows. With nothing on the screen to differ from, all of it (a width of 0); with nothing
        // differing, none (a height of 0).
        if (full)
        {
                // A whole frame is the window's picture: the compositor takes all of it again.
                table->damage_x[own] = 0;
                table->damage_y[own] = 0;
                table->damage_width[own] = terminal.width;
                table->damage_height[own] = terminal.height;
        }
        else if (screen && ROWS <= 1024)
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

                // The bar's track: down the window's right edge, the whole height.
                if (bar_told && terminal.width >= UI_SCROLL_WIDTH * (unsigned int)terminal_scale() / 100)
                {
                        int left = (int)terminal.width - UI_SCROLL_WIDTH * terminal_scale() / 100;

                        x1 = left < x1 ? left : x1;
                        x2 = (int)terminal.width > x2 ? (int)terminal.width : x2;
                        y1 = y1 < 0 ? y1 : 0;
                        y2 = (int)terminal.height > y2 ? (int)terminal.height : y2;
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
        terminal.caret_at[slot] = terminal_caret_index;
        terminal.caret_cover[slot] = terminal_caret_cover;
        terminal.caret_block[slot] = terminal_caret_block;
        terminal.caret_marks[slot] = terminal_caret_marks;
        terminal.valid[slot] = 1;

        table->width[own] = terminal.width;
        table->height[own] = terminal.height;
        windows_show(&terminal.own, (unsigned int)slot);
        terminal.shift[slot] = shift;
        terminal.forced = false;
        terminal.shown = slot;
        return true;
}

/* ---- the program ---- */

// The paper round the grid, for the cell the font gives: a third of a cell's height on every side. A function of the
// font's cell, not a kit metric: the kit's pad is for its own rows and sections, and the grid is the font's.
static void terminal_margins(void)
{
        unsigned int margin = terminal.cell_h / 3 ? terminal.cell_h / 3 : 1;

        terminal.margin = margin;
        terminal.pad = margin;
        terminal.pad_y = margin;
}

static void terminal_said(b32 reading, string_address what);

/*
        The faces, loaded from the files named. A font is the caller's bytes
        mapped for good; the engine and its atlas are mappings of their own,
        and only the pages a program draws with are ever resident.
*/
// The kit's UI faces, the files every app's own text is set in (ui_app_run loads them); a check names its own.
static string_address terminal_ui_regular_path = (string_address)UI_FONT_REGULAR;
static string_address terminal_ui_bold_path = (string_address)UI_FONT_BOLD;

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
        // The kit's own text (the actions bar's labels, the menu) is set in the UI faces, as every app's is (ui_app_run loads them),
        // and the grid keeps its mono faces. A UI font that will not load falls back to the grid's face: a kit with no face faults in
        // the first label it draws.
        terminal.ui.regular = ui_face_load(&terminal.ui.faces[0], terminal_ui_regular_path) ? &terminal.ui.faces[0] : terminal.plain[0];
        terminal.ui.bold = ui_face_load(&terminal.ui.faces[1], terminal_ui_bold_path) ? &terminal.ui.faces[1]
                           : terminal.bold_count ? terminal.bold[0] : terminal.plain[0];
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
        return columns * terminal.cell_w + terminal.pad;
}

// The cells a window's width or height has room for, the paper at the left taken off: the grid runs to the right edge,
// whether or not the bar is there. The grid is the app's own geometry (a terminal's cells, not a kit metric), so it stays as it is.
static unsigned int terminal_columns(unsigned int width)
{
        unsigned int edge = terminal.pad;

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
        int display_x, display_y, screen_w, screen_h;

        // A shell at its prompt has no use for the pointer's motion, and each report of it is a wakeup: asked for
        // only while a program has a mouse mode (the loop says when that changes).
        windows_hover(&terminal.own, false);
        terminal.hovering = false;

        // A pinch zooms the text (terminal_pinch), and is not sent to the program as a wheel with Ctrl.
        windows_pinch(&terminal.own, true);

        // The primary screen is where it opens, its shares of that screen, and no larger than a window may be.
        table = terminal.own.table;
        windows_output(table, 0, &display_x, &display_y, &screen_w, &screen_h);
        display_w = (unsigned int)screen_w < table->output_width ? (unsigned int)screen_w : table->output_width;
        display_h = (unsigned int)screen_h < table->output_height ? (unsigned int)screen_h : table->output_height;
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
        table->x[terminal.own.row] = display_x + (cells[0] && cells[1] ? ((int)display_w - (int)terminal.width) / 2
                                                                       : (int)(display_w * left / 100));
        table->y[terminal.own.row] = display_y + (cells[0] && cells[1] ? ((int)display_h - (int)terminal.height) / 2
                                                                       : (int)(display_h * top / 100));
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

        // The digits, and nothing after them but one newline at the end.
        {
                positive size = got > 0 ? (positive)got : 0, used = 0;

                value = (unsigned int)string_digits_max((string_address)text, size, &used);
                if (used < size && (text[used] != '\n' || used + 1 != size))
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

        // renameat2 with no flags, as riscv64 has no renameat.
        return wrote && compositor_call(syscall(renameat2), (positive)AT_FDCWD, (positive)TERMINAL_SIZE_TEMP,
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

// A pinch zooms this terminal by steps: the size it had when the pinch began, scaled, is the step nearest it, with the edges kept
// (ui_pinch_steps). It is written the way a zoom key writes it.
static int terminal_pinch_base;

static void terminal_pinch(const struct window_event address_to event)
{
        unsigned int now = terminal.size >> 6, next;

        if (event->flags & WINDOW_PINCH_BEGIN)
                terminal_pinch_base = (int)now;
        next = ui_pinch_steps(terminal_steps, TERMINAL_STEPS, terminal_step_index(now), ui_pinch_size(terminal_pinch_base, event->a));
        if (terminal_steps[next] == now)
                return;
        terminal_size_write(terminal_steps[next]);
        terminal_text_set(terminal.master, terminal_steps[next]);
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
        key->character = event->a == UI_KEY_BACKSPACE ? 127 : character;
        key->reserved = 0;
}

// The pointer as a report in cells, the way the window's key ring carried it.
static void terminal_pointer_key(unsigned int button, unsigned int flags, struct window_key address_to key)
{
        unsigned int column = (unsigned int)(terminal_at_x() < 0 ? 0 : terminal_at_x()) / terminal.cell_w;
        unsigned int row = (unsigned int)(terminal_at_y() < 0 ? 0 : terminal_at_y()) / terminal.cell_h;

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
        A finger's pixels as the view's lines: what is under a line is kept for the next move. Down is back in the
        scrollback (what is under the finger follows it), as the wheel turned up is; on the alternate screen, where a
        pager or an editor has no scrollback here, they are its arrows, as the wheel's are. Answers whether the pty still
        takes what it is owed; false in moved when the view went nowhere (an end of the scrollback).
*/
static b32 terminal_finger_scroll(b32 master, int pixels, b32 address_to dirty, b32 address_to moved)
{
        long total = (long)terminal.finger_left + pixels;
        int lines = terminal.cell_h ? (int)(total / (long)terminal.cell_h) : 0;

        *moved = true;
        terminal.finger_left = (int)(total - (long)lines * (long)terminal.cell_h);
        lines = lines > (int)ROWS ? (int)ROWS : lines < -(int)ROWS ? -(int)ROWS : lines;
        if (!lines)
                return true;
        if (alternate)
        {
                string_address arrow = lines > 0 ? (application_keys ? "\033OA" : "\033[A") : (application_keys ? "\033OB" : "\033[B");

                for (int at = lines < 0 ? -lines : lines; at > 0; at--)
                        emit_bytes((address_any)arrow, string_length(arrow));
                return term_send(master);
        }
        *moved = terminal_view_scroll(lines);
        if (*moved)
                *dirty = true;
        return true;
}

/*
        A finger with no program that asked for the pointer: down, nothing yet (no selection starts under a finger); past
        the slop it drags the view; up unmoved it is a click, the press and the release where it went down; up moving it
        throws the view (terminal_tick steps it); taken back (WINDOW_KEY_CANCEL) it is nothing. Answers as
        terminal_receive does; handled is false for what is not a finger's to take here.
*/
static b32 terminal_finger(b32 master, const struct window_event address_to event, b32 address_to dirty, b32 address_to handled)
{
        struct window_key key;
        struct ui kit;
        b32 moved, sent, dragged;

        *handled = false;
        if (!ui_kit_touch || mouse_mode || terminal.bar.held)
                return true;
        if (event->type == WINDOW_EVENT_MOTION && ui_finger_held(&terminal.finger))
        {
                // The slop is the kit's, at the scale the terminal draws at (its own ui may not have one yet).
                kit = terminal.ui;
                kit.scale = terminal_scale();
                *handled = true;
                if (!alternate)
                {
                        unsigned int first, shown, total;
                        int shift = terminal_grid_shift(), moved_px;

                        if (terminal_extent(&first, &shown, &total) && total > shown)
                                terminal_bar_place(first, shown, total);
                        ui_finger_move(&kit, &terminal.finger, terminal_at_x(), terminal_at_y());
                        // The grid moves one for one with the finger, up for a finger that goes down (the tracker's reports are kept
                        // where the grid fits, for a drag that finds it scrolling later). Past an end the body's rubber band draws it.
                        if (ui_finger_dragged(&terminal.finger))
                        {
                                moved_px = ui_drag_follow(&terminal.finger.drag, terminal_at_y());
                                ui_body_track(&terminal.body, -(b64)moved_px * UI_ONE, ui_kit_time());
                        }
                        if (terminal_bar_out() | (terminal_grid_shift() != shift))
                                *dirty = true;
                        return true;
                }
                ui_finger_move(&kit, &terminal.finger, terminal_at_x(), terminal_at_y());
                if (!ui_finger_dragged(&terminal.finger))
                        return true;
                return terminal_finger_scroll(master, ui_drag_follow(&terminal.finger.drag, terminal_at_y()),
                                              dirty, &moved);
        }
        if (event->type != WINDOW_EVENT_BUTTON || event->a != UI_POINTER_LEFT)
                return true;
        // The bar's track is the bar's, for a finger as for the mouse.
        if ((event->flags & WINDOW_KEY_DOWN) && terminal_bar_takes())
                return true;
        *handled = true;
        if (event->flags & WINDOW_KEY_DOWN)
        {
                // Whatever was moving the grid stops where it is drawn, and the press is where the finger is.
                ui_finger_down(&terminal.finger, terminal_at_x(), terminal_at_y(), 0, null);
                if (!alternate)
                        ui_body_hold(&terminal.body, ui_kit_time());
                terminal.finger_left = 0;
                return true;
        }
        if (!ui_finger_held(&terminal.finger))
                return true;
        dragged = ui_finger_dragged(&terminal.finger);
        ui_finger_up(&terminal.finger, (event->flags & WINDOW_KEY_CANCEL) != 0);
        if (!dragged)
                return true;
        if (!alternate)
        {
                // The lift throws the grid at the finger's fitted speed (the tracker's, down the screen positive), and a speed under the
                // throw's floor, at the scale, throws nothing.
                long speed = ui_drag_velocity(&terminal.finger.drag, ui_drag_now());

                ui_body_let_go(&terminal.body, ui_throws(speed, terminal_scale()) ? -(b64)speed * UI_ONE : 0, ui_kit_time());
                terminal.dirty = true;
                return true;
        }
        ui_drag_throw_let_go(&terminal.finger.drag, terminal_scale()); // the event's own time (ui_event_context)
        return true;
}

/* ---- the selection: what the hand marked, and the clipboard ---- */

// A place in the ring as one number: its line times TERMINAL_PLACE, and a column in the line. The controller compares places as it
// compares text offsets (ui_select_extend), so the terminal's selection is two of them.
#define TERMINAL_PLACE 65536
#define TERMINAL_LINE_OF(p) ((unsigned int)((p) / TERMINAL_PLACE))
#define TERMINAL_COLUMN_OF(p) ((unsigned int)((p) % TERMINAL_PLACE))
static b32 terminal_selecting(void);
static void terminal_select_set(p64 anchor, p64 active);
static struct ui_select address_to terminal_sel(void);

/*
        A selection is two places in the ring, each a line (as the ring counts them, which only grows) and a column in
        that line as it is stored: a folded line is one line, so a selection keeps its place when the window is made
        narrower or wider, and when the view scrolls. A drag marks characters, a double click words and a triple click
        lines, extending by its kind as it goes; Alt makes it a rectangle of columns; Shift with a click extends what
        there is. While a program has asked for the pointer, Shift is what selects (as in xterm). Lines that scroll off
        the ring's end take the selection with them. The marked cells are drawn on the theme's selection colour; what
        a release leaves marked is the PRIMARY selection, Ctrl+Shift+C (or Ctrl+Insert) copies it, and Ctrl+Shift+V,
        Shift+Insert and the middle button paste, through term_paste.
*/

// The line and column of the ring under a cell of the grid (row and column on the screen), as terminal_build lays them.
static b32 terminal_place_at(unsigned int row, unsigned int column, unsigned int address_to line_out, unsigned int address_to column_out)
{
        unsigned int skip, line = terminal_top(terminal.view, &skip), at = 0;

        while (line != window->head)
        {
                unsigned int slot = line % window->history, length = window_lengths(window)[slot];
                unsigned int folds = length ? ((length < window->stride ? length : window->stride) + COLUMNS - 1) / COLUMNS : 1;

                for (unsigned int fold = skip; fold < folds; fold++, at++)
                        if (at == row)
                        {
                                *line_out = line;
                                *column_out = fold * COLUMNS + column;
                                return true;
                        }
                skip = 0;
                line++;
        }
        // Below the newest line: the end of it.
        *line_out = window->head - 1;
        *column_out = window_lengths(window)[(window->head - 1) % window->history];
        return false;
}

// Where the selection starts and stops in reading order: line, column; the stop is past the last cell.
static void terminal_select_span(unsigned int address_to first_line, unsigned int address_to first_column,
                                 unsigned int address_to last_line, unsigned int address_to last_column)
{
        unsigned int anchor_line = TERMINAL_LINE_OF(terminal.sel_anchor), anchor_column = TERMINAL_COLUMN_OF(terminal.sel_anchor);
        unsigned int active_line = TERMINAL_LINE_OF(terminal.sel_active), active_column = TERMINAL_COLUMN_OF(terminal.sel_active);
        b32 forward = anchor_line < active_line || (anchor_line == active_line && anchor_column <= active_column);

        *first_line = forward ? anchor_line : active_line;
        *first_column = forward ? anchor_column : active_column;
        *last_line = forward ? active_line : anchor_line;
        *last_column = forward ? active_column : anchor_column;
}

// Whether a ring line is still in the ring.
static b32 terminal_line_kept(unsigned int line)
{
        return window->head - line <= window->history && window->head - line > 0;
}


// A place in the grid's ring as one number in reading order (the line, then the column in it: a column is under the window's
// stride, which is under TERMINAL_PLACE), so the kit's selection rule compares places as it compares text offsets.
static p64 terminal_place(unsigned int line, unsigned int column)
{
        return (p64)line * TERMINAL_PLACE + column;
}

/*
        A word of a line, by the rule every selection takes (doc_word_in, the text widget's): the cells of the line round column are
        written as the UTF-8 the document would hold (a wide cell's right half takes no bytes of its own), the word is read there, and
        its bytes are mapped back to cells. A right half under the press is its left cell's word. A place past the line's text is no
        word: the place alone, as an empty line gives the text widget.
*/
#define TERMINAL_WORD_CELLS 256

static struct doc terminal_words; // zeroed: the default separators

static void terminal_word_span(unsigned int line, unsigned int column, p64 address_to start, p64 address_to finish)
{
        unsigned int slot = line % window->history, stride = window->stride, stored = window_lengths(window)[slot];
        const struct window_cell address_to cells = window_cells(window) + (unsigned long)slot * stride;
        unsigned int lo, hi, cs, cf;
        positive offsets[TERMINAL_WORD_CELLS + 1], length = 0, at, first, last;
        p8 bytes[TERMINAL_WORD_CELLS * 4];

        stored = stored < stride ? stored : stride;
        *start = *finish = terminal_place(line, column);
        if (column >= stored)
                return;
        if (column && (cells[column].flags & WINDOW_CELL_WIDE_RIGHT))
                column--;
        lo = column > TERMINAL_WORD_CELLS / 2 ? column - TERMINAL_WORD_CELLS / 2 : 0;
        hi = lo + TERMINAL_WORD_CELLS < stored ? lo + TERMINAL_WORD_CELLS : stored;
        for (unsigned int c = lo; c < hi; c++)
        {
                unsigned int code = cells[c].character;

                offsets[c - lo] = length;
                if (cells[c].flags & WINDOW_CELL_WIDE_RIGHT)
                        continue;
                length += memory_utf8_encode(bytes + length, 4, code ? code : ' ');
        }
        offsets[hi - lo] = length;
        at = offsets[column - lo];
        doc_word_in(&terminal_words, bytes, length, at, &first, &last);
        for (cs = lo; cs < hi; cs++)
                if (!(cells[cs].flags & WINDOW_CELL_WIDE_RIGHT) && offsets[cs - lo] >= first)
                        break;
        for (cf = lo; cf < hi; cf++)
                if (!(cells[cf].flags & WINDOW_CELL_WIDE_RIGHT) && offsets[cf - lo] >= last)
                        break;
        *start = terminal_place(line, cs), *finish = terminal_place(line, cf > cs ? cf : cs + 1 > hi ? hi : cs + 1);
}

// The span a kind takes at a place: its characters (the place alone), its word (the cells of the word it is in, or the one cell
// where there is no word), or its line (the whole line). The kit's kinds (ui_select_extend) decide the selection from these.
static void terminal_select_span_of(int kind, unsigned int line, unsigned int column, p64 address_to start, p64 address_to finish)
{
        if (kind == UI_SELECT_LINES)
        {
                *start = terminal_place(line, 0), *finish = terminal_place(line, window->stride);
                return;
        }
        if (kind == UI_SELECT_WORDS)
        {
                terminal_word_span(line, column, start, finish);
                return;
        }
        *start = *finish = terminal_place(line, column);
}

// The marked cells of a row of the frame: the part of line from column from (used of it written) the selection covers.
static void terminal_select_mark(struct window_cell address_to out, unsigned int line, unsigned int from)
{
        unsigned int first_line, first_column, last_line, last_column, start, stop;

        if (!terminal_selecting())
                return;
        terminal_select_span(&first_line, &first_column, &last_line, &last_column);
        if (line - first_line > last_line - first_line)
                return;
        if (terminal.select_box)
        {
                unsigned int left = TERMINAL_COLUMN_OF(terminal.sel_anchor) < TERMINAL_COLUMN_OF(terminal.sel_active) ? TERMINAL_COLUMN_OF(terminal.sel_anchor)
                                                                                                 : TERMINAL_COLUMN_OF(terminal.sel_active);
                unsigned int right = TERMINAL_COLUMN_OF(terminal.sel_anchor) < TERMINAL_COLUMN_OF(terminal.sel_active) ? TERMINAL_COLUMN_OF(terminal.sel_active)
                                                                                                  : TERMINAL_COLUMN_OF(terminal.sel_anchor);

                start = left, stop = right + 1;
        }
        else
        {
                start = line == first_line ? first_column : 0;
                stop = line == last_line ? last_column : ~0u;
        }
        for (unsigned int column = start > from ? start - from : 0; column < COLUMNS && from + column < stop; column++)
                out[column].flags |= WINDOW_CELL_SELECTED;
}

// The selected text, a line end between lines and each line's trailing blanks left off, into a buffer the caller frees.
static positive terminal_select_text(p8 address_to address_to into, positive address_to room)
{
        unsigned int first_line, first_column, last_line, last_column;
        positive length = 0;

        *into = null, *room = 0;
        if (!terminal_selecting())
                return 0;
        terminal_select_span(&first_line, &first_column, &last_line, &last_column);
        for (unsigned int line = first_line;; line++)
        {
                unsigned int slot = line % window->history, stored = window_lengths(window)[slot], start, stop, finish;
                const struct window_cell address_to cells = window_cells(window) + (unsigned long)slot * window->stride;

                stored = stored < window->stride ? stored : window->stride;
                if (terminal.select_box)
                {
                        start = TERMINAL_COLUMN_OF(terminal.sel_anchor) < TERMINAL_COLUMN_OF(terminal.sel_active) ? TERMINAL_COLUMN_OF(terminal.sel_anchor) : TERMINAL_COLUMN_OF(terminal.sel_active);
                        stop = (TERMINAL_COLUMN_OF(terminal.sel_anchor) < TERMINAL_COLUMN_OF(terminal.sel_active) ? TERMINAL_COLUMN_OF(terminal.sel_active) : TERMINAL_COLUMN_OF(terminal.sel_anchor)) + 1;
                }
                else
                {
                        start = line == first_line ? first_column : 0;
                        stop = line == last_line ? last_column : stored;
                }
                finish = stop < stored ? stop : stored;
                while (finish > start && (cells[finish - 1].character == ' ' || !cells[finish - 1].character))
                        finish--;
                if (terminal_line_kept(line) && array_store_reserve(*into, *room, length, length + (positive)(finish > start ? finish - start : 0) * 4 + 2, 4096))
                        for (unsigned int column = start; column < finish; column++)
                        {
                                unsigned int c = cells[column].character;

                                if (cells[column].flags & WINDOW_CELL_WIDE_RIGHT)
                                        continue;
                                length += memory_utf8_encode(*into + length, 4, c ? c : ' ');
                        }
                if (line == last_line)
                        break;
                if (*room > length)
                        (*into)[length++] = '\n';
        }
        return length;
}

// What is selected copied to a selection, the compositor told.
static void terminal_copy(unsigned int which)
{
        p8 address_to text;
        positive room, length = terminal_select_text(&text, &room);

        ui_clip_offer(&terminal.own, which, text, room, length);
}

/*
        The terminal's context menu, at the pointer on the right button (with Shift while a program has the pointer):
        copy what is selected, paste, select everything the ring holds, and the zoom.
*/
#define TERMINAL_MENU_COPY 1
#define TERMINAL_MENU_PASTE 2
#define TERMINAL_MENU_ALL 3
#define TERMINAL_MENU_ZOOM_IN 4
#define TERMINAL_MENU_ZOOM_OUT 5
#define TERMINAL_MENU_ZOOM_RESET 6

static struct ui_popup terminal_menu;

static fn terminal_menu_open(int x, int y, b32 touch)
{
        struct ui_popmenu address_to m = &terminal_menu.menu;

        m->count = 0;
        ui_menu_clip_rows(m, TERMINAL_MENU_COPY, "Ctrl+Shift+C", terminal_selecting() ? 0 : UI_MENU_DISABLED, TERMINAL_MENU_PASTE,
                          "Ctrl+Shift+V", 0, TERMINAL_MENU_ALL, null, 0);
        ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(m, WORDS(menu_zoom_in), "Ctrl+=", 0, TERMINAL_MENU_ZOOM_IN);
        ui_popmenu_add(m, WORDS(menu_zoom_out), "Ctrl+-", 0, TERMINAL_MENU_ZOOM_OUT);
        ui_popmenu_add(m, WORDS(menu_reset_zoom), "Ctrl+0", 0, TERMINAL_MENU_ZOOM_RESET);
        ui_popmenu_open(&terminal_menu, &terminal.own, &terminal.ui, x, y, 0, 0, WINDOW_POPUP_POINT, touch);
}

static fn terminal_chose(int id)
{
        if (id == TERMINAL_MENU_COPY)
                terminal_copy(WINDOWS_CLIP_CLIPBOARD);
        else if (id == TERMINAL_MENU_PASTE)
                windows_clip_ask(&terminal.own, WINDOWS_CLIP_CLIPBOARD);
        else if (id == TERMINAL_MENU_ALL)
        {
                terminal.select_box = false;
                terminal_select_set(terminal_place(terminal_oldest(), 0), terminal_place(window->head - 1, window->stride));
                terminal.sel.handles = true;
        }
        else if (id >= TERMINAL_MENU_ZOOM_IN && id <= TERMINAL_MENU_ZOOM_RESET)
                terminal_zoom(terminal.master, id == TERMINAL_MENU_ZOOM_IN ? 1 : id == TERMINAL_MENU_ZOOM_OUT ? -1 : 2);
        terminal.dirty = true;
}

/*
        The terminal as the kit's selection controller's client (ui_select_ops): its places are ring places (terminal_place), its box is
        the grid, its scroll is the view (a drag held past an edge, which goes back through the scrollback or forward to the live end),
        and its selection is the two ends the ring keeps (sel_anchor, sel_active). Its touch and bar answers are in the touch part.
*/
// The grid row a ring place is drawn on and its x in the row (grid pixels), as the controller's handles and bar need it: 0 when it is on
// the grid, -1 above the view and 1 below it.
static int terminal_op_where(struct ui address_to ui, void address_to self, p64 at, int address_to x, int address_to top)
{
        unsigned int want_line = TERMINAL_LINE_OF(at), want_column = TERMINAL_COLUMN_OF(at), skip, line, row = 0;

        (void)ui, (void)self;
        line = terminal_top(terminal.view, &skip);
        while (line != window->head)
        {
                unsigned int slot = line % window->history, length = window_lengths(window)[slot];
                unsigned int folds = length ? ((length < window->stride ? length : window->stride) + COLUMNS - 1) / COLUMNS : 1;
                unsigned int at_fold = want_column / COLUMNS < folds ? want_column / COLUMNS : folds - 1;

                for (unsigned int fold = skip; fold < folds; fold++, row++)
                        if (line == want_line && fold == at_fold)
                        {
                                if (row >= ROWS)
                                        return 1;
                                *x = (int)((want_column - fold * COLUMNS) * terminal.cell_w);
                                *top = (int)(row * terminal.cell_h) + terminal_grid_shift(); // the grid's slide, which the cells move by too
                                return 0;
                        }
                skip = 0;
                line++;
        }
        return want_line < terminal_top(terminal.view, &skip) ? -1 : 1;
}

static b32 terminal_op_shown(void address_to self)
{
        (void)self;
        return terminal_selecting();
}

// The bar's items the terminal can do: Copy when something is selected, Paste always, and Select All unless the whole ring is.
static int terminal_op_items(void address_to self, int address_to ids)
{
        int count = 0;

        (void)self;
        if (terminal_selecting())
                ids[count++] = UI_SELECT_COPY;
        ids[count++] = UI_SELECT_PASTE;
        if (!(terminal.sel_anchor == terminal_place(terminal_oldest(), 0) && terminal.sel_active == terminal_place(window->head - 1, window->stride)))
                ids[count++] = UI_SELECT_ALL;
        return count;
}

// What an item of the bar does: Copy and Paste are done and the bar goes; Select All selects the ring, and the bar stays over it.
static p32 terminal_op_item(struct ui address_to ui, void address_to self, int id)
{
        (void)ui, (void)self;
        if (id == UI_SELECT_COPY)
                terminal_copy(WINDOWS_CLIP_CLIPBOARD);
        else if (id == UI_SELECT_PASTE)
                windows_clip_ask(&terminal.own, WINDOWS_CLIP_CLIPBOARD);
        else if (id == UI_SELECT_ALL)
        {
                terminal.select_box = false;
                terminal_select_set(terminal_place(terminal_oldest(), 0), terminal_place(window->head - 1, window->stride));
                terminal.sel.handles = true;
        }
        terminal.dirty = true;
        return UI_TEXT_MOVED;
}

// Whether the handles or the bar are to be drawn this frame.
static b32 terminal_overlay_wanted(void)
{
        return (terminal.sel.handles && terminal_selecting()) || terminal.sel.bar;
}

// The rows the handles and the bar covered when this buffer was last drawn are drawn again, as a spoil does: the cells in them are
// not what the buffer holds any more.
static void terminal_overlay_spoil(struct window_cell address_to drawn, int slot)
{
        int top = terminal.overlay_top[slot], bottom = terminal.overlay_bottom[slot], first, last;

        if (bottom <= top)
                return;
        first = top / (int)terminal.cell_h, last = (bottom + (int)terminal.cell_h - 1) / (int)terminal.cell_h;
        first = first < 0 ? 0 : first;
        last = last > (int)ROWS ? (int)ROWS : last;
        if (last > first)
                memory_fill_u64_aligned(drawn + (positive)first * COLUMNS, (positive)(last - first) * COLUMNS, ~0ull);
}

// The handles and the bar, drawn over the grid's cells and the scrollbar in this buffer; the rows they cover are kept for the next frame.
static void terminal_overlay(unsigned int address_to whole, int slot)
{
        struct ui kit;

        // The kit's text (the bar's labels) is drawn through a scratch picture, which only ui_scratch_fit makes: a kit with none
        // draws no text at all, so the labels were blank. The scratch is the terminal kit's own, so every copy of it has it.
        terminal.ui.scale = terminal_scale();
        ui_scratch_fit(&terminal.ui, terminal.width);
        kit = terminal_kit(terminal_grid(whole));
        struct ui_select address_to s = terminal_sel();

        // The kit's origin is the grid's, pad right and pad_y down from the window's corner, so the picture it may write is the
        // window less that much: a knob hung below the last line is clipped there, not at the window's bottom past the picture.
        kit.width = (int)terminal.width - (int)terminal.pad;
        kit.height = kit.bottom = (int)terminal.height - (int)terminal.pad_y;
        s->over_top = s->over_bottom = 0;
        ui_select_overlay(&kit, s);
        terminal.overlay_top[slot] = s->over_top, terminal.overlay_bottom[slot] = s->over_bottom;
}

static b32 terminal_selecting(void)
{
        return terminal.sel_anchor != terminal.sel_active;
}

static void terminal_select_set(p64 anchor, p64 active)
{
        terminal.sel_anchor = anchor, terminal.sel_active = active;
}

static void terminal_select_clear(void)
{
        terminal_select_set(0, 0);
        terminal.sel.handles = terminal.sel.bar = false;
}

static struct ui terminal_sel_kit(void)
{
        struct ui kit = terminal.ui;

        kit.scale = terminal_scale();
        return kit;
}

static void terminal_op_box(void address_to self, struct ui_select_box address_to b)
{
        (void)self;
        b->left = 0, b->top = 0;
        b->right = (int)(COLUMNS * terminal.cell_w), b->bottom = (int)(ROWS * terminal.cell_h);
        b->line_height = (int)terminal.cell_h;
        b->field = false;
}

static p64 terminal_op_place(struct ui address_to ui, void address_to self, int x, int y)
{
        unsigned int row = (unsigned int)(y < 0 ? 0 : y) / terminal.cell_h, column = (unsigned int)(x < 0 ? 0 : x) / terminal.cell_w, line, at;

        (void)ui, (void)self;
        row = row < ROWS ? row : ROWS - 1;
        column = column < COLUMNS ? column : COLUMNS;
        terminal_place_at(row, column, &line, &at);
        return terminal_place(line, at);
}

static void terminal_op_span(void address_to self, int kind, p64 at, p64 address_to start, p64 address_to finish)
{
        (void)self;
        terminal_select_span_of(kind, TERMINAL_LINE_OF(at), TERMINAL_COLUMN_OF(at), start, finish);
}

static void terminal_op_get(void address_to self, p64 address_to anchor, p64 address_to active)
{
        (void)self;
        *anchor = terminal.sel_anchor, *active = terminal.sel_active;
}

static void terminal_op_set(void address_to self, p64 anchor, p64 active)
{
        (void)self;
        terminal_select_set(anchor, active);
}

static b32 terminal_op_any(void address_to self)
{
        (void)self;
        return terminal_selecting();
}

static b32 terminal_op_within(void address_to self, p64 at)
{
        p64 low = terminal.sel_anchor < terminal.sel_active ? terminal.sel_anchor : terminal.sel_active;
        p64 high = terminal.sel_anchor < terminal.sel_active ? terminal.sel_active : terminal.sel_anchor;

        (void)self;
        return at >= low && at <= high;
}

static b32 terminal_op_inside(void address_to self, int x, int y)
{
        (void)self;
        return x >= 0 && y >= 0 && x < (int)(COLUMNS * terminal.cell_w) && y < (int)(ROWS * terminal.cell_h);
}

// A drag held past the grid's top (lines < 0) scrolls the view back into the scrollback; past its bottom, forward to the live end.
static void terminal_op_scroll(struct ui address_to ui, void address_to self, long lines, b32 edge)
{
        (void)ui, (void)self;
        if (!edge || alternate || !lines)
                return;
        if (terminal_view_scroll(lines < 0 ? (int)-lines : -(int)lines))
                terminal.dirty = true;
}

static void terminal_op_moved(struct ui address_to ui, void address_to self, b32 reveal)
{
        (void)ui, (void)self, (void)reveal;
}

static void terminal_op_copy(void address_to self, unsigned int which)
{
        (void)self;
        terminal_copy(which);
}

static struct ui_select address_to terminal_sel(void);

static const struct ui_select_ops terminal_select_ops = {
        .box = terminal_op_box,       .place = terminal_op_place,   .span = terminal_op_span,   .where = terminal_op_where,
        .get = terminal_op_get,       .set = terminal_op_set,       .extend = terminal_op_set,  .any = terminal_op_any,
        .shown = terminal_op_shown,   .within = terminal_op_within, .inside = terminal_op_inside, .scroll = terminal_op_scroll,
        .moved = terminal_op_moved,   .copy = terminal_op_copy,     .items = terminal_op_items, .item = terminal_op_item,
};

static struct ui_select address_to terminal_sel(void)
{
        terminal.sel.ops = &terminal_select_ops;
        terminal.sel.self = &terminal;
        return &terminal.sel;
}

/*
        A button or a motion the selection takes: true when it was the selection's (the program does not get it).
        A program that asked for the pointer has it unless Shift is held.
*/
static b32 terminal_select_event(const struct window_event address_to event, b32 address_to dirty)
{
        b32 shift = (terminal.modifiers & WINDOW_KEY_SHIFT) != 0, alt = (terminal.modifiers & WINDOW_KEY_ALT) != 0;
        struct ui kit = terminal_sel_kit();
        struct ui_select address_to s = terminal_sel();
        unsigned int row, column, line, at;
        p64 place;
        int clicks;

        // A program that asked for the pointer has its presses; one comes in while a selection is dragged, the drag ends.
        if (mouse_mode && !shift && (event->type == WINDOW_EVENT_BUTTON ? (event->flags & WINDOW_KEY_DOWN) || !s->dragging : !s->dragging))
        {
                s->dragging = false;
                return false;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_MIDDLE && (event->flags & WINDOW_KEY_DOWN))
        {
                windows_clip_ask(&terminal.own, WINDOWS_CLIP_PRIMARY);
                return true;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_RIGHT)
        {
                if (event->flags & WINDOW_KEY_DOWN)
                        terminal_menu_open(ui_kit.pointer_x, ui_kit.pointer_y, ui_kit_touch);
                return true;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_LEFT)
        {
                if (!(event->flags & WINDOW_KEY_DOWN))
                {
                        if (!s->dragging)
                                return false;
                        s->dragging = false;
                        if (terminal_selecting())
                                terminal_copy(WINDOWS_CLIP_PRIMARY);
                        address_to dirty = true;
                        return true;
                }
                if (terminal_at_x() < 0 || terminal_at_y() < 0)
                        return false;
                row = (unsigned int)terminal_at_y() / terminal.cell_h;
                column = (unsigned int)terminal_at_x() / terminal.cell_w;
                if (row >= ROWS || column >= COLUMNS)
                        return false;
                terminal_place_at(row, column, &line, &at);
                place = terminal_place(line, at);
                clicks = ui_click_count(&kit, &s->click, terminal_at_x(), terminal_at_y());
                if (shift && terminal_selecting() && clicks == 1)
                        ui_select_shift(&kit, s, place);
                else
                {
                        terminal.select_box = alt && clicks == 1;
                        ui_select_start(s, place, clicks, true);
                }
                s->dragging = true;
                s->edge.past = 0; // the press is on the grid: the edge a drag before it held past is not this drag's
                terminal.sel.handles = terminal.sel.bar = false;
                address_to dirty = true;
                return true;
        }
        if (event->type == WINDOW_EVENT_MOTION && s->dragging)
        {
                p64 to = ui_select_track(&kit, s, terminal_at_x(), terminal_at_y(), terminal_at_x(), terminal_at_y(), 0, false);

                if (to != terminal.sel_active)
                {
                        ui_select_drag_to(&kit, s, to, false);
                        address_to dirty = true;
                }
                return true;
        }
        return false;
}

/*
        The clipboard's keys: Ctrl+Shift+C and Ctrl+Insert copy, Ctrl+Shift+V and Shift+Insert paste. True when the key
        was one of them (the program does not get it); typing anything else lets go of the selection.
*/
static b32 terminal_select_key(const struct window_event address_to event, b32 address_to dirty)
{
        int clip = ui_clip_chord(event);

        if (clip)
        {
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        if (clip == UI_CLIP_COPY)
                                terminal_copy(WINDOWS_CLIP_CLIPBOARD);
                        else
                                windows_clip_ask(&terminal.own, WINDOWS_CLIP_CLIPBOARD);
                }
                return true;
        }
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && terminal_selecting() && !ui_key_modifier(event->a))
        {
                terminal_select_clear();
                address_to dirty = true;
        }
        return false;
}

// The clipboard's answer: a paste that may be read is pasted, the terminal's own (a block turned, a cursor hidden as a key's paste is).
static b32 terminal_clip_event(b32 master, const struct window_event address_to event)
{
        p8 address_to bytes;
        unsigned long length;

        if (ui_clip_read(&terminal.own, event, true, &bytes, &length))
        {
                // As a key does: the block is a cell turned round in the ring, and the line the paste writes
                // over it was turned back by the next hide, a block left on the cell where the paste began.
                cursor_hide();
                term_paste(bytes, length);
                windows_clip_free(bytes, length);
                return term_send(master);
        }
        return true;
}

/*
        A finger on a terminal no program asked the pointer of: the controller's (ui_select_finger) for its tap, its long press (the
        word under the finger, held until the lift), its handles and its bar; the finger's scroll is terminal_finger's, which goes on
        after it. A right button's long press is the selection's, not a menu, and a press on the scrollbar is the bar's. Answers
        whether the event was the selection's and is taken here.
*/
static b32 terminal_touch_select(const struct window_event address_to event, b32 address_to dirty)
{
        struct ui kit = terminal_sel_kit();
        struct ui_select address_to s = terminal_sel();
        p32 asks;

        if (!ui_kit_touch || mouse_mode)
                return false;
        if (event->type == WINDOW_EVENT_MOTION)
        {
                if (s->finger)
                {
                        asks = ui_select_finger(&kit, s, event, terminal_at_x(), terminal_at_y());
                        terminal.sel.quiet_at = (positive)ui_kit.now + UI_SELECT_BAR_AFTER_NS;
                        if (asks & UI_TEXT_MOVED)
                                address_to dirty = true;
                }
                return false;
        }
        if (event->type != WINDOW_EVENT_BUTTON)
                return false;
        if (event->a == UI_POINTER_LEFT)
        {
                // A press on the scrollbar is the bar's, and a press that is not held is the scroll's as well as the selection's.
                if ((event->flags & WINDOW_KEY_DOWN) && (terminal.bar.held || terminal_bar_takes()))
                        return false;
                if (!(event->flags & WINDOW_KEY_DOWN) && !s->finger && !(event->flags & WINDOW_KEY_HOLD))
                        return false;
                asks = ui_select_finger(&kit, s, event, terminal_at_x(), terminal_at_y());
                if (asks & UI_TEXT_MOVED)
                        address_to dirty = true;
                return (event->flags & WINDOW_KEY_HOLD) != 0 && !(event->flags & WINDOW_KEY_DOWN);
        }
        if (event->a == UI_POINTER_RIGHT && (event->flags & WINDOW_KEY_HOLD))
        {
                // A long press: the word under the finger, selected and held until the lift (the controller's).
                asks = ui_select_finger(&kit, s, event, terminal_at_x(), terminal_at_y());
                if (asks & UI_TEXT_MOVED)
                        address_to dirty = true;
                return true;
        }
        return false;
}

/*
        One event from the compositor. Answers whether the pty will still take
        what the shell is owed; dirty is set when what is drawn has to be made
        again, whether or not the cells changed.
*/
static b32 terminal_receive(b32 master, const struct window_event address_to event, b32 address_to dirty)
{
        struct window_key key;

        // A finger drags the view where the mouse would select; a program that asked for the pointer has it as a mouse. The
        // pointer is the kit's (ui_event_context records a motion's place before this runs).
        if (terminal_touch_select(event, dirty))
                return true;
        if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
        {
                b32 handled, sent = terminal_finger(master, event, dirty, &handled);

                if (handled)
                        return sent;
        }
        if (event->type == WINDOW_EVENT_KEY || event->type == WINDOW_EVENT_WHEEL)
                ui_drag_stop(&terminal.finger.drag);

        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                terminal.modifiers = event->flags;
                if (terminal_select_key(event, dirty))
                        return true;
                if (!terminal.modes_fresh)
                        terminal_modes_read(master, (positive)ui_kit.now);
                terminal_key(event, &key);
                // Typing is a reason to look at what is being typed; a modifier alone is not typing, and
                // Control held for a zoom or a copy keeps the place the view was scrolled back to.
                if ((key.flags & WINDOW_KEY_DOWN) && !ui_key_modifier(event->a))
                {
                        if (terminal_view_live())
                                address_to dirty = true;
                        cursor_hide();
                        // A key restarts the caret's blink: solid as it is typed at.
                        terminal_caret_restart((positive)ui_kit.now);
                }
                return term_input(master, &key);

        case WINDOW_EVENT_MOTION:
                {
                        // The paper round the grid is no cell's, and terminal_at_x and terminal_at_y say where the pointer is in the grid.
                        b32 moved = false, lit = terminal.bar.hover;

                        // The kit's bar takes the motion that is over it or held; what it lights or moves is drawn again.
                        if (terminal.bar.held || terminal.bar.hover || terminal_bar_takes())
                                terminal_bar_pointer(WINDOW_EVENT_MOTION, event->a, event->b, false, &moved);
                        if (moved || lit != terminal.bar.hover)
                                address_to dirty = true;
                        if (terminal.bar.held)
                                return true;
                }

                if (terminal_select_event(event, dirty))
                        return true;
                if (!mouse_mode)
                        return true;

                terminal_pointer_key(0, WINDOW_KEY_POINTER_MOVE | (terminal.pressed ? WINDOW_KEY_DOWN : 0), &key);
                return term_input(master, &key);

        case WINDOW_EVENT_BUTTON:
        {
                unsigned int button = event->a == UI_POINTER_RIGHT ? 2 : event->a == UI_POINTER_MIDDLE ? 1 : 0;
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

                if (event->a != UI_POINTER_LEFT && event->a != UI_POINTER_RIGHT && event->a != UI_POINTER_MIDDLE)
                        return true;

                // The bar's track is the bar's, not the program's, where there is a bar: its press, and the release of a held
                // thumb, go to the kit's pointer.
                if (event->a == UI_POINTER_LEFT && (terminal.bar.held || (down && terminal_bar_takes())))
                {
                        b32 moved = false;

                        terminal_bar_pointer(WINDOW_EVENT_BUTTON, ui_kit.pointer_x,
                                             ui_kit.pointer_y, down, &moved);
                        if (moved)
                                address_to dirty = true;
                        return true;
                }

                // A left press on the paper right of the grid, and its release when the press was not the program's, is
                // not the program's whether or not there is anything to scroll: a report there would be clamped onto the grid's last column.
                if (event->a == UI_POINTER_LEFT && (down || !terminal.pressed) && terminal_at_x() >= (int)(COLUMNS * terminal.cell_w))
                        return true;
                if (terminal_select_event(event, dirty))
                        return true;

                terminal.pressed = down;
                terminal_pointer_key(button, down ? WINDOW_KEY_DOWN : 0, &key);
                return term_input(master, &key);
        }

        case WINDOW_EVENT_CLIP:
                return terminal_clip_event(master, event);

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
                if (!alternate)
                {
                        terminal_bar_turn(event, dirty);
                        return true;
                }

                long scaled = (long)steps * TERMINAL_WHEEL_LINES + terminal.wheel_remainder;
                int lines = (int)(scaled / 120);

                lines = lines > (int)ROWS ? (int)ROWS : lines < -(int)ROWS ? -(int)ROWS : lines;

                terminal.wheel_remainder = (int)(scaled % 120);

                if (!lines)
                        return true;

                // A pager or an editor on the alternate screen has no scrollback here: the wheel is its
                // arrows, a line a step, as the cursor keys' mode spells them.
                string_address arrow = lines > 0 ? (application_keys ? "\033OA" : "\033[A")
                                                 : (application_keys ? "\033OB" : "\033[B");

                for (int at = lines < 0 ? -lines : lines; at > 0; at--)
                        emit_bytes((address_any)arrow, string_length(arrow));
                return term_send(master);
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

// A start that fails (no font, no memory, no far end): the same line in the machine's log, and one line on standard error, where a
// program started with no font (not the launcher's words) would otherwise say nothing.
static void terminal_refused(b32 reading, string_address what)
{
        string_address name = reading ? (string_address) "klog" : (string_address) "terminal";

        p8 line[128];
        positive at = 0;

        terminal_said(reading, what);
        for (positive k = 0; name[k] && at < sizeof(line) - 2; k++)
                line[at++] = (p8)name[k];
        for (positive k = 0; what[k] && at < sizeof(line) - 2; k++)
                line[at++] = (p8)what[k];
        line[at++] = '\n';
        log_error((address_any)line, at);
}

// What is said after a program this terminal was asked to run has ended: how it did, and what closes the window. The line is
// the table's words in [ ] after a break from what was on the screen; here and not in terminal_exit_said so a check can ask it.
static positive terminal_exit_line(p8 address_to line, positive room)
{
        positive at;

        string_copy(line, (string_address) "\x1b[0m\r\n[");
        at = string_length(line);
        at += language_format((char address_to)line + at, room - at,
                              (term_exit_status & 0x7f) ? WORDS(terminal_press_to_close) : WORDS(terminal_exited_press_to_close),
                              (long)((term_exit_status >> 8) & 0xff));
        line[at++] = ']';
        line[at] = 0;
        return at;
}

static fn terminal_exit_said(void)
{
        p8 line[256];
        positive at = terminal_exit_line(line, sizeof(line) - 1);

        cursor_hide();
        term_bytes(line, at);
}

// Whether an event is a key that was pressed and means something: not a repeat, and not a modifier on its own.
static b32 terminal_key_pressed(const struct window_event address_to event)
{
        return event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && !(event->flags & WINDOW_KEY_REPEAT) &&
               !ui_key_modifier(event->a);
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
        closing, the frame and its holds, then the far end's report, the title, the synchronized frame's hold, and the
        timers that say how long the wait may be.
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
                positive read_now = (positive)ui_kit.now;

                if (read_now - terminal.modes_at >= terminal_period())
                        terminal_modes_read(terminal.master, read_now);
        }
}

// One event from the compositor: zoom keys, then the keys and pointer of the shell (or the log's keys). A staying window
// leaves on a key, when the shell was a program that was asked for.
static b32 terminal_on_event(const struct window_event address_to event)
{
        int zoom = ui_zoom_key(event);

        if (event->type == WINDOW_EVENT_PINCH)
        {
                terminal_pinch(event);
                terminal.dirty = true;
                return true;
        }
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
        // The keys are the only events that say which modifiers are down, and a key let go while another window
        // had them is never told to this one: left as they were, a Control or Shift that was down when the keys
        // moved is added to every pointer report after (a wheel notch becomes button 84), until a key is typed here.
        terminal.modifiers = 0;
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

                now = (positive)ui_kit.now;
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
                        terminal.last_frame = (positive)ui_kit.now;
                return true;
        }

        now = (positive)ui_kit.now;
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
        hold, and the timers. The answer is the wait's timeout: -1 for ever, else the longest the wait may
        last; 0 when a frame is due now and not held, so the loop runs again before it waits. The pty the wait is on is set
        too: none while the window stays.
*/
/*
        A selection drag held past the grid's top or bottom scrolls the scrollback, a line at a time as the time passes: 120
        logical pixels a second at the edge, 6 more for each logical pixel the pointer is past it, at most 2400. Above the grid
        the view goes back through what has been said, below it forward to the live end, and the selection's end is taken again
        at the edge row each step, so it grows as the text scrolls. The alternate screen has no scrollback here, and a drag that
        is back on the grid, or lifted, stops it. Answers when the next step is due, 0 for none.
*/
static positive terminal_select_autoscroll(positive now, b32 address_to dirty)
{
        struct ui kit = terminal_sel_kit();
        struct ui_select address_to s = terminal_sel();
        b32 redraw = false;
        positive due;

        (void)now;
        if (alternate)
        {
                s->edge.at = 0;
                return 0;
        }
        due = ui_select_autoscroll(&kit, s, &redraw);
        if (redraw)
                address_to dirty = true;
        return due;
}

static long terminal_tick(long timeout, long address_to other)
{
        b32 was = terminal.dirty, again = false;
        long hold = terminal.hold_timeout;
        long kit = timeout;
        positive now;

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
                positive title_now = (positive)ui_kit.now;

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

        // The kit's own wait comes first: a theme turn in flight asks for its next frame a period on, and a frame the kit could
        // not draw is asked again. The terminal's waits below are taken as the least of these, never in place of them.
        timeout = kit;
        if (synchronized_output)
        {
                long sync;

                now = (positive)ui_kit.now;
                terminal.sync_since = terminal.sync_held ? terminal.sync_since : now;
                terminal.sync_held = true;

                if (terminal.gone || now - terminal.sync_since >= (positive)TERMINAL_SYNC_NANOSECONDS)
                {
                        synchronized_output = false;
                        terminal.changed = true;
                }
                else
                {
                        sync = (long)(TERMINAL_SYNC_NANOSECONDS - (now - terminal.sync_since));
                        timeout = timeout < 0 || sync < timeout ? sync : timeout;
                }
        }
        else
                terminal.sync_held = false;

        terminal.dirty |= terminal.changed;

        if (hold >= 0 && (timeout < 0 || hold < timeout))
                timeout = hold;

        // The body's motion of the scrollback (a wheel's glide, a finger's throw, a pull's spring) goes on a frame at a time.
        if (ui_body_going(&terminal.body))
        {
                terminal_bar_step();
                timeout = timeout < 0 || timeout > UI_DRAG_FRAME_NS ? UI_DRAG_FRAME_NS : timeout;
        }

        // The view a finger threw on the alternate screen goes on a frame at a time, slowing, until it is still.
        if (terminal.finger.drag.flying)
        {
                b32 moved = true;

                if (!terminal_finger_scroll(terminal.master, ui_drag_step(&terminal.finger.drag),
                                            &terminal.dirty, &moved))
                        terminal.gone = true;
                if (!moved)
                        ui_drag_stop(&terminal.finger.drag);
                if (terminal.finger.drag.flying)
                        timeout = timeout < 0 || timeout > UI_DRAG_FRAME_NS ? UI_DRAG_FRAME_NS : timeout;
        }

        // The actions bar comes back when the view has been still for a while (ui_select_bar_tick).
        {
                positive bar_due = ui_select_bar_tick(terminal_sel(), &terminal.dirty), bar_now = (positive)ui_kit.now;

                if (bar_due)
                {
                        long left = bar_due > bar_now ? (long)(bar_due - bar_now) : 0;

                        timeout = timeout < 0 || left < timeout ? left : timeout;
                }
        }
        // A selection drag held past the grid scrolls the scrollback a step at a time (terminal_select_autoscroll).
        {
                positive scroll_due = terminal_select_autoscroll((positive)ui_kit.now, &terminal.dirty), scroll_now;

                if (scroll_due)
                {
                        long left;

                        scroll_now = (positive)ui_kit.now;
                        left = scroll_due > scroll_now ? (long)(scroll_due - scroll_now) : 0;
                        timeout = timeout < 0 || left < timeout ? left : timeout;
                }
        }

        if (terminal.title_owed)
        {
                positive due = terminal.title_at + terminal_period(), owed_now = (positive)ui_kit.now;
                long left = due > owed_now ? (long)(due - owed_now) : 0;

                timeout = timeout < 0 || left < timeout ? left : timeout;
        }

        // The caret's blink: a move restarts it, and the wait ends when its cover next changes.
        if (terminal.phase == TERMINAL_RUNNING)
                terminal_caret_step((positive)ui_kit.now, &timeout);

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
                        now = (positive)ui_kit.now;
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
                terminal_refused(terminal.reading, " has no font to draw with (-f FONT.ttf)");
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
                terminal_refused(terminal.reading, " has no memory for its cells");
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
                terminal_refused(terminal.reading, " cannot open its far end");
                return 1;
        }

        cursor_show();
        terminal_frame();
        terminal.last_frame = (positive)ui_kit.now;

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
        .popup = &terminal_menu,
        .chose = terminal_chose,
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
        // The log's tab is the kit's to name (and to say again in a new language); a terminal's is its program's, as it runs.
        terminal_app.own_title = !reading;
        terminal_app.name = reading ? (string_address) "klog" : (string_address) "terminal";
        terminal_app.missing = reading ? (string_address) "klog: no window: no compositor, or no row free in its table\n"
                                       : (string_address) "terminal: no window: no compositor, or no row free in its table\n";
        return ui_app_run(&terminal_app);
}

static b32 terminal_main()
{
        return terminal_run(false);
}

#endif // MOONWATER_CANVAS_TERMINAL
