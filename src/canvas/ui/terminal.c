/*
        The terminal window on the compositor: renders term.c's cell ring via font.c into double buffers or GPU draw lists,
        with scrollback, rubber-band scroll/touch, selection, clipboard, drag-and-drop, zoom, and klog mode.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_TERMINAL
#define MOONWATER_CANVAS_TERMINAL

#include "ui.c"

#define TERMINAL_HISTORY 512
#define TERMINAL_FACES 4
#define TERMINAL_FACE_SLOTS (TERMINAL_FACES + 1)
#define TERMINAL_WIDE 52
#define TERMINAL_HIGH 62
#define TERMINAL_LEFT 44
#define TERMINAL_TOP 14
#define TERMINAL_LOG_WIDE 52
#define TERMINAL_LOG_HIGH 62
#define TERMINAL_LOG_LEFT 3
#define TERMINAL_LOG_TOP 6
#define TERMINAL_LOG_HISTORY 1024
#define TERMINAL_STEPS 16
static const unsigned int terminal_steps[TERMINAL_STEPS] = {8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48, 56, 64};
#define TERMINAL_SIZE 14
#define TERMINAL_SIZE_DIR "/root/terminal"
#define TERMINAL_SIZE_FILE "/root/terminal/size"
#define TERMINAL_SIZE_TEMP "/root/terminal/size.tmp"
#define TERMINAL_ATLAS 2048
#define TERMINAL_CEILING 3840
#define TERMINAL_BUDGET (256 * 1024)
#define TERMINAL_WHEEL_LINES 3
#define TERMINAL_LIVE ((unsigned int)-1)
#define TERMINAL_SYNC_NANOSECONDS 200000000ll
#define TERMINAL_SETTLE_NANOSECONDS 3000000ll
#define TERMINAL_IDLE_NANOSECONDS 1000000000ll
#define TERMINAL_FRAME_NANOSECONDS 16666667ll
#define TERMINAL_STALL_NANOSECONDS 250000000ll
#define TERMINAL_DRAIN_HOLD_NANOSECONDS 2000000ll

typedef unsigned long long __attribute__((may_alias)) terminal_word;

#define TERMINAL_RUNNING 0
#define TERMINAL_STAYING 1
#define TERMINAL_DONE 2

static struct
{
        struct window_row own;
        struct ui ui;
        font_face face[TERMINAL_FACE_SLOTS];
        const font_face *plain[TERMINAL_FACES], *bold[TERMINAL_FACES];
        unsigned int plain_count, bold_count;
        font_metrics grid;
        unsigned int size, cell_w, cell_h, pad, pad_y, margin;
        unsigned int width, height, pitch, ground;

        struct window_cell *frame[3], *frames;
        unsigned long frame_bytes;
        unsigned int valid[2], cursor_at;
        int shown;
        b32 forced;
        unsigned int bar_first[2], bar_shown[2], bar_total[2], bar_drawn[2];
        struct ui_scroll bar;
        unsigned int view, view_skip;

        unsigned int modifiers, pressed_button;
        b32 pressed, focused, hovering;
        int wheel_remainder, wheel_notches, finger_left, group;
        struct ui_press finger;
        struct ui_body alt;
        char named[WINDOW_TITLE_MAX];
        unsigned int title_seen;
        positive modes_at, title_at;
        b32 modes_fresh, title_owed, margin_spoiled[2], stretched[2];
        int shift[2];
        unsigned int painted_ink, painted_paper;

        b32 dirty, changed, reading, gone, hung, keyed, sync_held;
        int phase;
        bipolar child;
        b32 master;
        positive oom_kills, sync_since, stalled_since, last_frame, settle_since, idle_since, drain_until;
        long hold_timeout;

        struct ui_select sel;
        p64 sel_anchor, sel_active;
        b32 select_box;
        int overlay_top[2], overlay_bottom[2];
        struct ui_caret caret;
        unsigned int caret_row, caret_column;
        b32 caret_seen;
        unsigned int caret_at[2], caret_cover[2], caret_marks[2];
        b32 caret_block[2];
        unsigned int address_to rec_whole, address_to rec_grid;
        b32 list_lost;
        unsigned long list_lost_key;
        unsigned int list_lost_frames;
} terminal = {.shown = -1, .view = TERMINAL_LIVE, .hold_timeout = -1, .caret_at = {~0u, ~0u}};

static unsigned int terminal_caret_index = ~0u, terminal_caret_cover = UI_CARET_FULL, terminal_caret_marks;
static b32 terminal_caret_block;
static unsigned int terminal_colour[256];

static unsigned int terminal_default_ink(void) { return ui_role(UI_ON_SURFACE) & 0xffffffu; }
static unsigned int terminal_default_paper(void) { return ui_role(UI_SURFACE) & 0xffffffu; }

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

static unsigned int terminal_palette(unsigned int index)
{
        if (index == TERM_INDEX_INK)
                return terminal_default_ink();
        if (index == TERM_INDEX_PAPER)
                return terminal_default_paper();
        return terminal_colour[index & 255];
}

/* ---- view, scrollback sums, and scrollbar ---- */

static unsigned int terminal_line_rows(unsigned int index)
{
        unsigned int length = min(*window_length(window, index), window->stride);

        return length ? (length + COLUMNS - 1) / COLUMNS : 1;
}

static unsigned int terminal_oldest(void)
{
        unsigned int filled = min(ROWS + (alternate ? 0 : history_lines), window->history);

        return window->head - filled;
}

static unsigned int terminal_top(unsigned int view, unsigned int address_to skip)
{
        unsigned int oldest = terminal_oldest(), at = window->head - 1, count = 0;

        address_to skip = 0;
        if (view != TERMINAL_LIVE)
        {
                at = view < oldest ? oldest : view > window->head - 1 ? window->head - 1 : view;
                address_to skip = min(terminal.view_skip, terminal_line_rows(at) - 1);
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

#define TERMINAL_SUMS 4096
static unsigned int terminal_sums[TERMINAL_SUMS], terminal_sums_base, terminal_sums_end, terminal_sums_grid[4];
static b32 terminal_sums_valid;

static b32 terminal_sums_sync(void)
{
        unsigned int oldest = terminal_oldest(), head = window->head, screen;
        unsigned int grid[4] = {COLUMNS, ROWS, window->stride, window->history};

        if (head == oldest || window->history + ROWS >= TERMINAL_SUMS)
                return false;
        screen = head - oldest > ROWS ? head - ROWS : oldest;
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
        return true;
}

static unsigned int terminal_sum_to(unsigned int at)
{
        unsigned int rows = 0;

        if (at <= terminal_sums_end)
                return terminal_sums[at % TERMINAL_SUMS];
        for (unsigned int line = terminal_sums_end; line != at; line++)
                rows += terminal_line_rows(line);
        return terminal_sums[terminal_sums_end % TERMINAL_SUMS] + rows;
}

static b32 terminal_extent(unsigned int address_to first, unsigned int address_to shown, unsigned int address_to total)
{
        unsigned int oldest = terminal_oldest(), head = window->head, skip, top;

        address_to shown = ROWS, address_to total = 0, address_to first = 0;
        if (head == oldest)
                return false;
        top = terminal_top(terminal.view, &skip);
        if (terminal_sums_sync())
        {
                unsigned int from = terminal_sums[oldest % TERMINAL_SUMS];
                address_to total = terminal_sum_to(head) - from;
                address_to first = terminal_sum_to(top) - from + skip;
                return address_to total > ROWS;
        }
        for (unsigned int at = oldest; at != head; at++)
        {
                if (at == top)
                        address_to first = address_to total + skip;
                address_to total += terminal_line_rows(at);
        }
        return address_to total > ROWS;
}

struct term_view_iter
{
        unsigned int line, fold, folds, row, length;
        struct window_cell address_to cells;
};

static void term_view_begin(struct term_view_iter address_to it, unsigned int view)
{
        unsigned int skip;

        it->line = terminal_top(view, &skip), it->fold = skip;
        it->folds = it->row = it->length = 0, it->cells = null;
}

static b32 term_view_next(struct term_view_iter address_to it, unsigned int max_rows)
{
        if (it->row >= max_rows)
                return false;
        while (it->line != window->head)
        {
                if (!it->folds)
                {
                        unsigned int slot = it->line % window->history;
                        it->length = min(window_lengths(window)[slot], window->stride);
                        it->cells = window_cells(window) + (unsigned long)slot * window->stride;
                        it->folds = it->length ? (it->length + COLUMNS - 1) / COLUMNS : 1;
                }
                if (it->fold < it->folds)
                        return true;
                it->fold = it->folds = 0, it->line++;
        }
        return false;
}

static void term_view_step(struct term_view_iter address_to it) { it->fold++, it->row++; }

static void terminal_bar_follow(void);

static b32 terminal_view_live(void)
{
        if (terminal.view == TERMINAL_LIVE)
                return false;
        terminal.view = TERMINAL_LIVE, terminal.view_skip = 0;
        terminal_bar_follow();
        return true;
}

static b32 terminal_view_put(unsigned int above)
{
        unsigned int was = terminal.view, was_skip = terminal.view_skip;
        unsigned int at = terminal_oldest(), run = 0, live_skip, skip = 0;
        unsigned int live = terminal_top(TERMINAL_LIVE, &live_skip);
        b32 found = false;

        if (terminal_sums_sync())
        {
                unsigned int base = terminal_sums[at % TERMINAL_SUMS], screen = terminal_sums_end;
                unsigned int cached = terminal_sums[screen % TERMINAL_SUMS] - base;

                if (above < cached)
                {
                        unsigned int lo = at, hi = screen;

                        while (lo + 1 < hi)
                        {
                                unsigned int mid = lo + (hi - lo) / 2;
                                if (terminal_sums[mid % TERMINAL_SUMS] - base <= above)
                                        lo = mid;
                                else
                                        hi = mid;
                        }
                        at = lo, skip = above - (terminal_sums[lo % TERMINAL_SUMS] - base), found = true;
                }
                else
                        at = screen, run = cached;
        }
        if (!found)
                for (; at != window->head; at++)
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
                terminal.view = TERMINAL_LIVE, terminal.view_skip = 0;
        else
                terminal.view = at, terminal.view_skip = skip;
        return terminal.view != was || terminal.view_skip != was_skip;
}

static b32 terminal_view_set(unsigned int above)
{
        b32 moved = terminal_view_put(above);

        if (moved)
                terminal_bar_follow();
        return moved;
}

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

static void terminal_view_clamp(void)
{
        unsigned int live_skip, live;

        if (terminal.view == TERMINAL_LIVE)
                return;
        live = terminal_top(TERMINAL_LIVE, &live_skip);
        if (terminal.view > live || (terminal.view == live && terminal.view_skip >= live_skip))
                terminal_view_live();
}

static int terminal_scale(void) { return terminal.ui.scale > 0 ? terminal.ui.scale : 100; }
static int terminal_at_x(void) { return ui_kit.pointer_x - (int)terminal.pad; }
static int terminal_at_y(void) { return ui_kit.pointer_y - (int)terminal.pad_y; }
static unsigned int terminal_wheel_lines(void) { return ui_kit.wheel_lines ? ui_kit.wheel_lines : TERMINAL_WHEEL_LINES; }
static int terminal_grid_height(void) { return (int)(ROWS * terminal.cell_h); }

static struct ui terminal_kit(unsigned int address_to whole)
{
        struct ui kit = terminal.ui;

        kit.pixels = whole, kit.pitch = terminal.pitch, kit.width = terminal.width, kit.height = terminal.height;
        kit.top = 0, kit.bottom = terminal.height, kit.band_top = kit.band_bottom = 0, kit.opaque = false;
        kit.scale = terminal_scale();
        return kit;
}

#define TERMINAL_BAR_KEY() ((terminal.bar.held ? 1u : 0u) | (terminal.bar.hover ? 2u : 0u))

static void terminal_bar_place(unsigned int first, unsigned int shown, unsigned int total)
{
        int cell = (int)terminal.cell_h;

        terminal.bar.x = (int)terminal.width - UI_SCROLL_WIDTH * terminal_scale() / 100, terminal.bar.y = 0;
        terminal.bar.height = (int)terminal.height, terminal.bar.inset = UI_CLOSE_BOTTOM;
        terminal.bar.content = (int)total * cell, terminal.bar.view = (int)shown * cell, terminal.bar.scale = terminal_scale();
        terminal.bar.wheel = (int)terminal_wheel_lines() * cell;
        ui_body_fit_end(&terminal.bar.body, (b64)shown * cell * UI_ONE, (b64)total * cell * UI_ONE, terminal.view == TERMINAL_LIVE);
        if (!ui_body_going(&terminal.bar.body) && ui_body_px(&terminal.bar.body) != (int)first * cell)
                ui_body_set(&terminal.bar.body, terminal.bar.body.view, terminal.bar.body.content, (b64)first * cell * UI_ONE, terminal.bar.body.at);
        terminal.bar.scroll = ui_body_px(&terminal.bar.body);
}

static b32 terminal_bar_sync(void)
{
        unsigned int first, shown, total;

        if (!terminal_extent(&first, &shown, &total) || total <= shown)
                return false;
        terminal_bar_place(first, shown, total);
        return true;
}

static void terminal_bar_follow(void)
{
        unsigned int first, shown, total;

        if (!terminal_extent(&first, &shown, &total) || total <= shown)
                return;
        terminal_bar_place(first, shown, total);
        ui_body_set(&terminal.bar.body, terminal.bar.body.view, terminal.bar.body.content, (b64)first * terminal.cell_h * UI_ONE, terminal.bar.body.at);
        terminal.bar.scroll = ui_body_px(&terminal.bar.body);
}

static int terminal_grid_delta(b32 pull)
{
        unsigned int first, shown, total;
        int drawn, rest, shift;

        if (alternate || mouse_mode || !terminal_extent(&first, &shown, &total) || total <= shown)
                return 0;
        drawn = ui_body_px(&terminal.bar.body);
        rest = ui_scroll_rest(&terminal.bar, drawn, terminal_grid_height());
        if (pull)
                return drawn - rest;
        shift = (int)(first * terminal.cell_h) - rest;
        return shift > (int)terminal.height ? (int)terminal.height : shift < -(int)terminal.height ? -(int)terminal.height : shift;
}

static int terminal_grid_shift(void) { return terminal_grid_delta(false); }
static int terminal_grid_pull(void) { return terminal_grid_delta(true); }

static int terminal_cell_y(void)
{
        int y = ui_kit.pointer_y;

        if (terminal_grid_pull())
        {
                struct ui kit = terminal_kit(null);
                y = ui_scroll_unpull(&kit, &terminal.bar, ui_body_px(&terminal.bar.body), (int)terminal.pad_y, terminal_grid_height(), y);
        }
        return y - (int)terminal.pad_y - terminal_grid_shift();
}

static b32 terminal_bar_out(void)
{
        int cell = (int)terminal.cell_h, at = ui_body_px(&terminal.bar.body);

        return terminal_view_put((unsigned int)(((at < 0 ? 0 : at) + cell / 2) / cell));
}

static void terminal_bar_turn(const struct window_event address_to event, b32 address_to dirty)
{
        struct ui kit = terminal_kit(null);

        if (terminal_bar_sync() && ui_wheel_turn(&kit, &terminal.bar, event))
                terminal_bar_out(), *dirty = true;
}

static void terminal_bar_step(void)
{
        struct ui kit = terminal_kit(null);
        int shift, before;

        terminal_bar_sync();
        shift = terminal_grid_shift(), before = terminal.bar.scroll;
        ui_kit.busy = 0;
        ui_scroll_frame(&kit, &terminal.bar);
        if (terminal_bar_out() || terminal.bar.scroll != before || terminal_grid_shift() != shift)
                terminal.dirty = true;
}

static b32 terminal_bar_event(int type, int x, int y, b32 down, b32 address_to moved)
{
        struct ui kit = terminal_kit(null);
        unsigned int lit;
        b32 taken;

        if (moved)
                *moved = false;
        if (!terminal_bar_sync())
        {
                if (type)
                        terminal.bar.hover = terminal.bar.held = 0;
                return false;
        }
        if (!type)
                return ui_scroll_reach(&kit, &terminal.bar, ui_kit.pointer_x, ui_kit.pointer_y);
        lit = TERMINAL_BAR_KEY();
        taken = ui_scroll_bar(&kit, &terminal.bar, type, x, y, down);
        *moved = TERMINAL_BAR_KEY() != lit;
        if (taken)
                *moved = terminal_bar_out() || ui_body_going(&terminal.bar.body) || *moved;
        return taken;
}

static void terminal_select_mark(struct window_cell address_to out, unsigned int line, unsigned int from);

/* ---- frame snapshot, caret blink, and diff spans ---- */

static void terminal_build(struct window_cell address_to into)
{
        struct term_view_iter it;
        const struct window_cell address_to block = shown && shown_block ? row_cells(shown_row) + shown_column : null;
        const struct window_cell address_to caret = shown ? row_cells(shown_row) + shown_column : null;

        terminal.cursor_at = terminal_caret_index = ~0u;
        for (term_view_begin(&it, terminal.view); term_view_next(&it, ROWS); term_view_step(&it))
        {
                unsigned int from = it.fold * COLUMNS, used = min(it.length > from ? it.length - from : 0, COLUMNS);
                struct window_cell address_to out = into + (unsigned long)it.row * COLUMNS;

                memory_copy(out, it.cells + from, (positive)used * sizeof(struct window_cell));
                if (block >= it.cells + from && block < it.cells + from + used)
                        terminal.cursor_at = it.row * COLUMNS + (unsigned int)(block - (it.cells + from));
                if (caret >= it.cells + from && caret < it.cells + from + used)
                        terminal_caret_index = it.row * COLUMNS + (unsigned int)(caret - (it.cells + from));
                memory_fill_u64_aligned(out + used, COLUMNS - used, BLANK_CELL_WORD);
                terminal_select_mark(out, it.line, from);
        }
        if (it.row < ROWS)
                memory_fill_u64_aligned(into + (unsigned long)it.row * COLUMNS, (ROWS - it.row) * COLUMNS, BLANK_CELL_WORD);
}

static unsigned int terminal_caret_cover_at(positive now)
{
        positive due = 0;

        (void)now;
        return !shown || !terminal.focused ? UI_CARET_FULL : ui_caret_cover(&terminal.caret, &due);
}

static void terminal_caret_frame(void)
{
        terminal_caret_cover = terminal_caret_index == ~0u ? UI_CARET_FULL : terminal_caret_cover_at((positive)ui_kit.now);
        terminal_caret_block = terminal_caret_index != ~0u && shown && shown_block;
        terminal_caret_marks = terminal_caret_index != ~0u && shown ? shown_marks : 0;
}

static void terminal_caret_store(unsigned int slot)
{
        terminal.caret_at[slot] = terminal_caret_index, terminal.caret_cover[slot] = terminal_caret_cover;
        terminal.caret_block[slot] = terminal_caret_block, terminal.caret_marks[slot] = terminal_caret_marks;
}

static b32 terminal_caret_held(unsigned int slot)
{
        return terminal.caret_at[slot] == terminal_caret_index && terminal.caret_cover[slot] == terminal_caret_cover &&
               terminal.caret_block[slot] == terminal_caret_block && terminal.caret_marks[slot] == terminal_caret_marks;
}

static void terminal_caret_restart(positive now) { (void)now; ui_caret_wake(&terminal.caret); }

static void terminal_caret_step(positive now, long address_to timeout)
{
        positive due = 0;

        if (shown != terminal.caret_seen || shown_row != terminal.caret_row || shown_column != terminal.caret_column)
                terminal.caret_seen = shown, terminal.caret_row = shown_row, terminal.caret_column = shown_column, terminal_caret_restart(now);
        if (!shown || !terminal.focused)
                return;
        if (terminal.shown >= 0 && (terminal_caret_index == ~0u ? UI_CARET_FULL : terminal_caret_cover_at(now)) != terminal.caret_cover[terminal.shown])
                terminal.dirty = true;
        ui_caret_cover(&terminal.caret, &due);
        if (due)
                *timeout = ui_deadline_wait(*timeout, (b64)now, (b64)due);
}

static unsigned int terminal_blend(unsigned int from, unsigned int to, unsigned int cover)
{
        unsigned int out = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                out |= ((((from >> shift) & 255) * (UI_CARET_FULL - cover) + ((to >> shift) & 255) * cover + UI_CARET_FULL / 2) /
                        UI_CARET_FULL) << shift;
        return out;
}

static void terminal_span_take(unsigned int address_to first, unsigned int address_to last, unsigned int column)
{
        if (*last > *first)
                *first = min(*first, column), *last = max(*last, column + 1);
        else
                *first = column, *last = column + 1;
}

static void terminal_span(const struct window_cell address_to now, const struct window_cell address_to old,
                          unsigned int address_to first, unsigned int address_to last)
{
        unsigned int start = (unsigned int)(memory_common_prefix((address_any)now, (address_any)old, (positive)COLUMNS * sizeof(*now)) / sizeof(*now));
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
        address_to first = start, address_to last = stop;
}

/* ---- pixel & scene-list rendering ---- */

static unsigned int terminal_mix(unsigned int ink, unsigned int paper) { return ((ink & 0xfefefe) >> 1) + ((paper & 0xfefefe) >> 1); }
static b32 terminal_recording(void) { return terminal.ui.scene != null; }
static unsigned int terminal_grid_w(void) { return terminal.width > terminal.pad ? terminal.width - terminal.pad : 0; }
static unsigned int terminal_grid_h(void) { return terminal.height > terminal.pad_y ? terminal.height - terminal.pad_y : 0; }

static void terminal_fill(unsigned int address_to pixels, int x, int y, unsigned int width, unsigned int height, unsigned int colour, b32 grid)
{
        unsigned int bound_w = grid ? terminal_grid_w() : terminal.width, bound_h = grid ? terminal_grid_h() : terminal.height;
        long x0 = x < 0 ? 0 : (long)x, y0 = y < 0 ? 0 : (long)y;
        long x2 = min((long)x + (long)width, (long)bound_w), y2 = min((long)y + (long)height, (long)bound_h);

        if (x0 >= x2 || y0 >= y2)
                return;
        if (terminal_recording())
        {
                int ox = pixels == terminal.rec_grid ? (int)terminal.pad : 0, oy = pixels == terminal.rec_grid ? (int)terminal.pad_y : 0;

                if (pixels != terminal.rec_grid && pixels != terminal.rec_whole)
                        return (void)(terminal.ui.scene->lost = UI_LOST_TERM_GRID);
                ui_scene_record(terminal.ui.scene, ox + (int)x0, oy + (int)y0, ox + (int)x2, oy + (int)y2, colour, true);
                return;
        }
        memory_fill_u32_rect(pixels + (positive)y0 * terminal.pitch + x0, terminal.pitch, x2 - x0, y2 - y0, colour);
}

static void terminal_grid_slide(unsigned int address_to whole, int shift)
{
        unsigned int height = terminal.height, pitch = terminal.pitch, width = terminal.width;
        unsigned int up = shift < 0 ? (unsigned int)-shift : (unsigned int)shift;

        if (!shift)
                return;
        if (up >= height)
                return terminal_fill(whole, 0, 0, width, height, whole[0], false);
        if (shift > 0)
        {
                unsigned int colour = whole[0];

                for (unsigned int y = height; y-- > up;)
                        memory_copy(whole + (positive)y * pitch, whole + (positive)(y - up) * pitch, (positive)width * sizeof(unsigned int));
                terminal_fill(whole, 0, 0, width, up, colour, false);
        }
        else
        {
                unsigned int colour = whole[(positive)(height - 1) * pitch];

                memory_copy_rect_u32(whole, pitch, whole + (positive)up * pitch, pitch, width, height - up);
                terminal_fill(whole, 0, (int)(height - up), width, up, colour, false);
        }
}

static void terminal_decorate(unsigned int address_to pixels, const struct window_cell address_to cell,
                              int x, int y, unsigned int cells, unsigned int ink, unsigned int caret)
{
        unsigned int width = terminal.cell_w * cells, thick = (unsigned int)((terminal.grid.thickness + 0x8000) >> 16);

        thick = caret ? caret : thick ? thick : terminal.cell_h / 16 ? terminal.cell_h / 16 : 1;
        if (cell->flags & WINDOW_CELL_UNDERLINE)
        {
                int rule = terminal.grid.underline ? terminal.grid.baseline - (int)((terminal.grid.underline + 0x8000) >> 16)
                                                   : terminal.grid.baseline + (int)thick;
                rule = rule + (int)thick > (int)terminal.cell_h ? (int)(terminal.cell_h - thick) : rule;
                terminal_fill(pixels, x, y + rule, width, thick, ink, true);
        }
        if (cell->flags & WINDOW_CELL_STRIKE)
                terminal_fill(pixels, x, y + terminal.grid.baseline - (int)((terminal.grid.x_height / 2 + 0x8000) >> 16), width, thick, ink, true);
        if (cell->flags & WINDOW_CELL_BAR)
                terminal_fill(pixels, x, y, caret ? caret : terminal.cell_w / 8 ? terminal.cell_w / 8 : 1, terminal.cell_h, ink, true);
}

#define TERMINAL_BANDS 8

struct terminal_painter
{
        struct ui ui;
        p32 codes[FONT_RUN];
};

static struct terminal_painter terminal_painters[TERMINAL_BANDS];

static void terminal_rec_glyphs(struct terminal_painter address_to painter, unsigned int address_to pixels, unsigned int from,
                                unsigned int count, b32 heavy, p32 flags, int y, unsigned int ink, unsigned int paper)
{
        font_target target = {pixels, terminal.pitch, (b32)terminal_grid_w(), (b32)terminal_grid_h()};

        if (pixels != terminal.rec_grid)
                return (void)(terminal.ui.scene->lost = UI_LOST_TERM_FONT);
        painter->ui.scene = terminal.ui.scene;
        ui_scene_cells(&painter->ui, heavy ? terminal.bold : terminal.plain, heavy ? terminal.bold_count : terminal.plain_count, flags,
                       painter->codes, count, &terminal.grid, terminal.size, &target, (b32)terminal.pad, (b32)terminal.pad_y,
                       (b32)(from * terminal.cell_w), y, ink, paper);
        painter->ui.scene = null;
}

/* A wide character's pair, recorded over two cells: the kit's wide record takes the face count and the style flags. */
static void terminal_rec_wide(struct terminal_painter address_to painter, unsigned int address_to pixels, unsigned int from, b32 heavy,
                              p32 flags, unsigned int code, int y, unsigned int ink, unsigned int paper)
{
        font_target target = {pixels, terminal.pitch, (b32)terminal_grid_w(), (b32)terminal_grid_h()};

        if (pixels != terminal.rec_grid)
                return (void)(terminal.ui.scene->lost = UI_LOST_TERM_FONT);
        painter->ui.scene = terminal.ui.scene;
        ui_scene_wide(&painter->ui, heavy ? terminal.bold : terminal.plain, heavy ? terminal.bold_count : terminal.plain_count, flags, code,
                      &terminal.grid, terminal.size, &target, (b32)terminal.pad, (b32)terminal.pad_y, (b32)(from * terminal.cell_w), y, ink, paper);
        painter->ui.scene = null;
}

static p32 terminal_style(const struct window_cell address_to cell)
{
        return (cell->flags & WINDOW_CELL_ITALIC ? FONT_OBLIQUE : 0) |
               ((cell->flags & WINDOW_CELL_BOLD) && !terminal.bold_count ? FONT_EMBOLDEN : 0);
}

static void terminal_blanks(unsigned int address_to pixels, unsigned int from, unsigned int count, int y, unsigned int colour)
{
        terminal_fill(pixels, (int)(from * terminal.cell_w), y, count * terminal.cell_w, terminal.cell_h, colour, true);
}

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

                if ((cell->flags & TERMINAL_PLAIN_OUT) ||
                    (old && *(const terminal_word address_to)cell == *(const terminal_word address_to)(old + at + n)) ||
                    terminal_paper_of(cell) != run->paper)
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

static void terminal_row(struct terminal_painter address_to painter, unsigned int address_to pixels, unsigned int row,
                         const struct window_cell address_to now, const struct window_cell address_to old,
                         unsigned int first, unsigned int last)
{
        font_target target = {pixels, terminal.pitch, (b32)terminal_grid_w(), (b32)terminal_grid_h()};
        int y = (int)(row * terminal.cell_h);
        unsigned int run = 0, run_from = 0, run_colour = 0;
        unsigned int glyphs = 0, glyphs_from = 0, glyphs_ink = 0, glyphs_paper = 0, glyphs_bits = 0;
        b32 glyphs_heavy = 0;
        p32 glyphs_flags = 0;
        unsigned int caret = terminal_caret_index != ~0u && terminal_caret_index / COLUMNS == row ? terminal_caret_index % COLUMNS : ~0u;
        unsigned int caret_cover = terminal_caret_cover;

#define TERMINAL_GLYPHS()                                                                                                \
        do                                                                                                               \
        {                                                                                                                \
                unsigned int trail = 0;                                                                                  \
                if (terminal_recording())                                                                                \
                {                                                                                                        \
                        if (glyphs)                                                                                      \
                                terminal_rec_glyphs(painter, pixels, glyphs_from, glyphs, glyphs_heavy, glyphs_flags, y, \
                                                    glyphs_ink, glyphs_paper);                                           \
                        glyphs = 0;                                                                                      \
                        break;                                                                                           \
                }                                                                                                        \
                while (trail < glyphs && painter->codes[glyphs - 1 - trail] == ' ')                                      \
                        trail++;                                                                                         \
                if (glyphs > trail)                                                                                      \
                        font_cells(painter->ui.engine, glyphs_heavy ? terminal.bold : terminal.plain,                    \
                                   glyphs_heavy ? terminal.bold_count : terminal.plain_count, painter->codes,            \
                                   glyphs - trail, &terminal.grid, terminal.size, glyphs_flags, &target,                 \
                                   (b32)(glyphs_from * terminal.cell_w), y, glyphs_ink, glyphs_paper);                   \
                if (trail)                                                                                               \
                        terminal_blanks(pixels, glyphs_from + glyphs - trail, trail, y, glyphs_paper);                   \
                glyphs = 0;                                                                                              \
        } while (0)

        for (unsigned int at = first; at < last; at++)
        {
                const struct window_cell address_to cell = now + at;
                unsigned int ink = terminal_ink_of(cell), paper = terminal_paper_of(cell);
                unsigned int stop = caret > at && caret < last ? caret : last;
                b32 here = at == caret, pair, glyph, decor, plain_glyph, plain_space;
                unsigned int cells;

                if (cell->flags & WINDOW_CELL_SELECTED)
                        paper = ui_over(ui_premultiplied(ui_role(UI_SELECTION), UI_FULL), paper) & 0xffffffu;
                if (here && terminal_caret_block)
                {
                        unsigned int on_ink = ink, on_paper = paper;
                        ink = terminal_blend(on_paper, on_ink, caret_cover);
                        paper = terminal_blend(on_ink, on_paper, caret_cover);
                }
                pair = (cell->flags & WINDOW_CELL_WIDE) && at + 1 < COLUMNS && (cell[1].flags & WINDOW_CELL_WIDE_RIGHT);
                cells = pair ? 2 : 1;
                glyph = cell->character > ' ' && !(cell->flags & WINDOW_CELL_HIDDEN) &&
                        !(cell->flags & (WINDOW_CELL_WIDE | WINDOW_CELL_WIDE_RIGHT) && !pair);
                decor = (cell->flags & (WINDOW_CELL_UNDERLINE | WINDOW_CELL_STRIKE | WINDOW_CELL_BAR)) != 0;

                if (old && !here && *(const terminal_word address_to)cell == *(const terminal_word address_to)(old + at) &&
                    (!(cell->flags & WINDOW_CELL_WIDE) || at + 1 >= COLUMNS ||
                     *(const terminal_word address_to)(cell + 1) == *(const terminal_word address_to)(old + at + 1)) &&
                    (!(cell->flags & WINDOW_CELL_WIDE_RIGHT) || !at ||
                     *(const terminal_word address_to)(cell - 1) == *(const terminal_word address_to)(old + at - 1)))
                        at += cells - 1, glyph = decor = false, cells = 0;

                plain_glyph = glyph && cells == 1 && !decor && !here;
                plain_space = !plain_glyph && glyphs && cells == 1 && !glyph && !decor && !here &&
                              paper == glyphs_paper && glyphs_from + glyphs == at && glyphs < FONT_RUN;
                if (plain_glyph || plain_space)
                {
                        if (plain_glyph)
                        {
                                b32 heavy = (cell->flags & WINDOW_CELL_BOLD) && terminal.bold_count;
                                p32 style = terminal_style(cell);

                                if (run)
                                        terminal_blanks(pixels, run_from, run, y, run_colour), run = 0;
                                if (cell->flags & WINDOW_CELL_DIM)
                                        ink = terminal_mix(ink, paper);
                                if (glyphs && (glyphs_from + glyphs != at || glyphs_ink != ink || glyphs_paper != paper ||
                                               glyphs_heavy != heavy || glyphs_flags != style || glyphs == FONT_RUN))
                                        TERMINAL_GLYPHS();
                                if (!glyphs)
                                        glyphs_from = at, glyphs_ink = ink, glyphs_paper = paper, glyphs_heavy = heavy,
                                        glyphs_flags = style, glyphs_bits = cell->flags & TERMINAL_STYLE_BITS;
                        }
                        painter->codes[glyphs++] = plain_glyph ? cell->character : ' ';
                        {
                                struct terminal_plain key = {glyphs_ink, glyphs_paper, glyphs_bits};
                                unsigned int limit = FONT_RUN - glyphs < stop - (at + 1) ? at + 1 + (FONT_RUN - glyphs) : stop;
                                unsigned int taken = terminal_plain_scan(now, old, at + 1, limit, painter->codes + glyphs, &key);
                                glyphs += taken, at += taken;
                        }
                        continue;
                }
                TERMINAL_GLYPHS();
                if (cells && !glyph && !decor && !here)
                {
                        if (run && run_colour == paper && run_from + run == at)
                                run++;
                        else
                        {
                                if (run)
                                        terminal_blanks(pixels, run_from, run, y, run_colour);
                                run = 1, run_from = at, run_colour = paper;
                        }
                        {
                                struct terminal_plain key = {0, run_colour, 0};
                                unsigned int taken = terminal_plain_scan(now, old, at + 1, stop, null, &key);
                                run += taken, at += taken;
                        }
                        continue;
                }
                if (run)
                        terminal_blanks(pixels, run_from, run, y, run_colour), run = 0;
                if (!cells)
                        continue;
                if (cell->flags & WINDOW_CELL_DIM)
                        ink = terminal_mix(ink, paper);
                if (glyph)
                {
                        b32 heavy = (cell->flags & WINDOW_CELL_BOLD) && terminal.bold_count;
                        if (terminal_recording())
                        {
                                if (pair)
                                        terminal_rec_wide(painter, pixels, at, heavy, terminal_style(cell), cell->character, y, ink, paper);
                                else
                                        painter->codes[0] = cell->character,
                                        terminal_rec_glyphs(painter, pixels, at, 1, heavy, terminal_style(cell), y, ink, paper);
                        }
                        else
                                font_cell(painter->ui.engine, heavy ? terminal.bold : terminal.plain,
                                          heavy ? terminal.bold_count : terminal.plain_count, cell->character,
                                          cells, &terminal.grid, terminal.size, terminal_style(cell), &target,
                                          (b32)(at * terminal.cell_w), y, ink, paper);
                }
                else
                        terminal_fill(pixels, (int)(at * terminal.cell_w), y, cells * terminal.cell_w, terminal.cell_h, paper, true);
                if (decor && here && !terminal_caret_block)
                {
                        struct window_cell rest = *cell, own = *cell;
                        int left = (int)(at * terminal.cell_w);

                        rest.flags = (unsigned short)(cell->flags & ~terminal_caret_marks);
                        terminal_decorate(pixels, &rest, left, y, cells, ink, 0);
                        if (caret_cover)
                                own.flags = (unsigned short)(cell->flags & terminal_caret_marks),
                                terminal_decorate(pixels, &own, left, y, cells, terminal_blend(paper, ink, caret_cover),
                                                  (unsigned int)(ui_caret_thick() * terminal_scale() / 100));
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

static void terminal_margin(unsigned int address_to whole, const struct window_cell address_to frame,
                            unsigned int row, unsigned int first, unsigned int last)
{
        unsigned int top, height, below = terminal.pad_y + ROWS * terminal.cell_h;

        terminal_band(row, &top, &height);
        if (!first)
                terminal_fill(whole, 0, (int)top, terminal.pad, height, terminal_ground(frame, row, 0), false);
        if (!row)
                for (unsigned int column = first; column < last; column++)
                        terminal_fill(whole, (int)(terminal.pad + column * terminal.cell_w), 0, terminal.cell_w,
                                      terminal.pad_y, terminal_ground(frame, 0, column), false);
        if (row + 1 == ROWS && terminal.height > below)
                for (unsigned int column = first; column < last; column++)
                        terminal_fill(whole, (int)(terminal.pad + column * terminal.cell_w), (int)below, terminal.cell_w,
                                      terminal.height - below, terminal_ground(frame, row, column), false);
}

static unsigned int terminal_bar_from(void)
{
        int left = (int)terminal.width - UI_SCROLL_WIDTH * terminal_scale() / 100 - (int)terminal.pad;
        unsigned int column = left > 0 ? (unsigned int)left / terminal.cell_w : 0;

        column = column ? column - 1 : 0;
        return column < COLUMNS ? column : COLUMNS - 1;
}

static void terminal_bar(unsigned int address_to whole, const struct window_cell address_to frame, unsigned int first,
                         unsigned int shown, unsigned int total)
{
        unsigned int x = terminal.pad + COLUMNS * terminal.cell_w, top, height;
        struct ui kit;

        for (unsigned int row = 0; row < ROWS; row++)
                terminal_band(row, &top, &height),
                terminal_fill(whole, (int)x, (int)top, terminal.width > x ? terminal.width - x : 0, height,
                              terminal_ground(frame, row, COLUMNS - 1), false);
        if (terminal.width < UI_SCROLL_WIDTH * (unsigned int)terminal_scale() / 100 || total <= shown)
                return;
        kit = terminal_kit(whole);
        terminal_bar_place(first, shown, total);
        ui_scroll_draw(&kit, &terminal.bar);
}

static unsigned int address_to terminal_grid(unsigned int address_to whole)
{
        return whole + (unsigned long)terminal.pad_y * terminal.pitch + terminal.pad;
}

/* ---- double buffering, parallel row bands, and draw-list frames ---- */

static int terminal_next(void)
{
        if (terminal.shown < 0)
                return 0;
        if (windows_ready(&terminal.own))
        {
                terminal.shown = (int)(__atomic_load_n(&terminal.own.table->buffer_index[terminal.own.row], __ATOMIC_ACQUIRE) & 1u);
                return 1 - terminal.shown;
        }
        return terminal.forced ? terminal.shown : -1;
}

static b32 terminal_frames(void)
{
        unsigned long bytes = ((unsigned long)ROWS * COLUMNS * sizeof(struct window_cell) + 4095) & ~4095ul;
        long mapped;

        if (terminal.frames)
                window_call(WINDOW_SYS_MUNMAP, (long)terminal.frames, 3 * (long)terminal.frame_bytes, 0, 0, 0, 0);
        terminal.frames = 0, terminal.frame[0] = terminal.frame[1] = terminal.frame[2] = null;
        terminal.valid[0] = terminal.valid[1] = 0;
        mapped = window_call(WINDOW_SYS_MMAP, 0, 3 * (long)bytes, 3, 0x22, -1, 0);
        if (mapped < 0 && mapped > -4096)
                return false;
        terminal.frames = (struct window_cell *)mapped, terminal.frame_bytes = bytes;
        for (unsigned int at = 0; at < 3; at++)
                terminal.frame[at] = (struct window_cell *)(mapped + (long)bytes * at);
        return true;
}

static void terminal_discard(void)
{
        int slot = terminal.shown < 0 ? -1 : 1 - terminal.shown;

        if (slot >= 0 && terminal.valid[slot] && windows_ready(&terminal.own))
                windows_discard(&terminal.own, (unsigned int)slot), terminal.valid[slot] = 0;
}

static b32 terminal_spoil_rows(struct window_cell address_to drawn, int top_px, int bot_px, b32 clamp_bottom)
{
        int first = top_px / (int)terminal.cell_h, last = (bot_px + (int)terminal.cell_h - 1) / (int)terminal.cell_h;

        first = first < 0 ? 0 : first, last = last > (int)ROWS ? (int)ROWS : last;
        if (clamp_bottom && bot_px > (int)(ROWS * terminal.cell_h))
                last = (int)ROWS, first = first < (int)ROWS - 1 ? first : (int)ROWS - 1;
        if (last <= first)
                return false;
        memory_fill_u64_aligned(drawn + (positive)first * COLUMNS, (positive)(last - first) * COLUMNS, ~0ull);
        return true;
}

static void terminal_spoiled(struct window_cell address_to drawn, int slot)
{
        int x, y, width, height;

        if (!windows_spoiled(&terminal.own, (unsigned int)slot, &x, &y, &width, &height))
                return;
        if (!terminal_spoil_rows(drawn, y - (int)terminal.pad_y, y + height - (int)terminal.pad_y, true) && y < (int)terminal.pad_y)
                terminal.margin_spoiled[slot] = true;
        terminal.bar_drawn[slot] = ~0u;
        (void)x, (void)width;
}

#define TERMINAL_SPAN_ROWS 4096
static unsigned short terminal_span_first[TERMINAL_SPAN_ROWS], terminal_span_last[TERMINAL_SPAN_ROWS];

struct terminal_banding
{
        unsigned int address_to pixels, address_to whole;
        const struct window_cell address_to now, address_to drawn;
        unsigned int rows, bands;
        b32 over;
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
        const struct terminal_banding address_to b = (const struct terminal_banding address_to)context;

        terminal_rows_draw(&terminal_painters[index], b, (unsigned int)(b->rows * index / b->bands),
                           (unsigned int)(b->rows * (index + 1) / b->bands));
}

static void terminal_cells_draw(struct terminal_banding address_to banding, positive changed)
{
        positive bytes = changed * terminal.cell_w * terminal.cell_h * 4;
        positive bands = min(min(parallel_width(), (positive)TERMINAL_BANDS), (positive)banding->rows);

        if (bytes >= PARALLEL_MINIMUM_BYTES && bands >= 2)
                for (unsigned int at = 1; at < (unsigned int)bands; at++)
                        if (!terminal_painters[at].ui.engine && !ui_engine_open_sized(&terminal_painters[at].ui, TERMINAL_ATLAS))
                        {
                                bands = at;
                                break;
                        }
        if (bytes < PARALLEL_MINIMUM_BYTES || bands < 2)
                return terminal_rows_draw(&terminal_painters[0], banding, 0, banding->rows);
        banding->bands = (unsigned int)bands;
        parallel_for(terminal_band_draw, banding, bands, bytes);
}

static b32 terminal_overlay_wanted(void);
static void terminal_overlay_spoil(struct window_cell address_to drawn, int slot);
static void terminal_overlay(unsigned int address_to whole, int slot);

static void terminal_list_draw(const struct window_cell address_to now, unsigned int first, unsigned int shown, unsigned int total)
{
        unsigned int address_to whole = windows_buffer(terminal.own.table, terminal.own.row, 0);

        terminal.ui.width = (int)terminal.width, terminal.ui.height = (int)terminal.height, terminal.ui.pitch = terminal.pitch;
        terminal.ui.top = 0, terminal.ui.bottom = (int)terminal.height;
        terminal.rec_whole = whole, terminal.rec_grid = terminal_grid(whole);
        for (unsigned int row = 0; row < ROWS; row++)
                terminal_row(&terminal_painters[0], terminal.rec_grid, row, now + (unsigned long)row * COLUMNS, null, 0, COLUMNS),
                terminal_margin(whole, now, row, 0, COLUMNS);
        terminal_bar(whole, now, first, shown, total);
        if (terminal_overlay_wanted() && terminal.ui.scene)
        {
                terminal.ui.scene->ox = (int)terminal.pad, terminal.ui.scene->oy = (int)terminal.pad_y;
                terminal_overlay(whole, 0);
                terminal.ui.scene->ox = terminal.ui.scene->oy = 0;
        }
        terminal.rec_whole = terminal.rec_grid = null;
}

#define TERMINAL_LIST_RETRY 60
static unsigned long terminal_list_key(const struct window_cell address_to now)
{
        const terminal_word address_to words = (const terminal_word address_to)now;
        unsigned long count = (unsigned long)ROWS * COLUMNS * sizeof(struct window_cell) / sizeof(terminal_word), hash = 1469598103934665603ul;
        unsigned long extra[6] = {terminal.size, terminal.width, terminal.height, (unsigned long)terminal_scale(),
                                  terminal_default_ink(), terminal_default_paper()};

        for (unsigned long at = 0; at < count; at++)
                hash = (hash ^ (unsigned long)words[at]) * 1099511628211ul;
        for (int k = 0; k < 6; k++)
                hash = (hash ^ extra[k]) * 1099511628211ul;
        return hash;
}

static b32 terminal_list_frame(const struct window_cell address_to now, unsigned int first, unsigned int shown, unsigned int total)
{
        if (!ui_list_frame_begin())
                return false;
        terminal_list_draw(now, first, shown, total);
        if (!ui_list_frame_end(&terminal.own, (int)terminal.width, (int)terminal.height, null))
                return false;
        terminal.valid[0] = terminal.valid[1] = 0, terminal.margin_spoiled[0] = terminal.margin_spoiled[1] = true;
        terminal.forced = false, terminal.shown = 0;
        terminal_caret_store(0);
        return true;
}

static b32 terminal_bar_diff(int slot, unsigned int first, unsigned int shown, unsigned int total, b32 turning)
{
        return terminal.bar_first[slot] != first || terminal.bar_shown[slot] != shown || terminal.bar_total[slot] != total ||
               terminal.bar_drawn[slot] != TERMINAL_BAR_KEY() || turning;
}

static void terminal_damage_add(int address_to x1, int address_to y1, int address_to x2, int address_to y2, int l, int t, int r, int b)
{
        if (l < *x1) *x1 = l;
        if (t < *y1) *y1 = t;
        if (r > *x2) *x2 = r;
        if (b > *y2) *y2 = b;
}

static b32 terminal_frame(void)
{
        int slot, front, x1 = 0x7fffffff, y1 = 0x7fffffff, x2 = 0, y2 = 0, shift, pull;
        unsigned int address_to pixels, address_to whole, first, shown, total, rows, from;
        struct windows address_to table = terminal.own.table;
        unsigned int own = terminal.own.row, ink = terminal_default_ink(), paper = terminal_default_paper();
        struct window_cell address_to now, address_to drawn, address_to screen;
        b32 bar, bar_told, any, right = false, turning, full, stretched = false, margin, visible;
        positive changed = 0;

        if (ink != terminal.painted_ink || paper != terminal.painted_paper)
                terminal.valid[0] = terminal.valid[1] = 0, terminal.painted_ink = ink, terminal.painted_paper = paper;
        if ((slot = terminal_next()) < 0)
                return false;
        if (!terminal.frames && !terminal_frames())
                return false;
        now = terminal.frame[2];
        front = windows_ready(&terminal.own) ? terminal.shown : terminal.shown < 0 ? -1 : 1 - terminal.shown;
        screen = front >= 0 && terminal.valid[front] ? terminal.frame[front] : null;
        terminal_view_clamp();
        terminal_build(now);
        terminal_caret_frame();
        terminal_extent(&first, &shown, &total);
        if (total > shown)
                terminal_bar_place(first, shown, total);
        shift = terminal_grid_shift(), pull = terminal_grid_pull();
        full = shift != 0 || pull != 0 || terminal.shift[slot] != 0 || terminal.stretched[slot];
        if (!shift && !pull && windows_ready(&terminal.own) && ui_list_wanted(&terminal.own))
        {
                /* The key of the frame only matters after a lost frame: a clean frame hashes nothing. */
                if (terminal.list_lost && terminal_list_key(now) == terminal.list_lost_key && terminal.list_lost_frames < TERMINAL_LIST_RETRY)
                        terminal.list_lost_frames++;
                else if (terminal_list_frame(now, first, shown, total))
                        return terminal.list_lost = 0, true;
                else
                        terminal.list_lost = 1, terminal.list_lost_key = terminal_list_key(now), terminal.list_lost_frames = 0;
        }
        whole = windows_buffer(table, own, slot), pixels = terminal_grid(whole);
        drawn = terminal.valid[slot] ? terminal.frame[slot] : null;
        if (drawn)
                terminal_spoiled(drawn, slot), terminal_overlay_spoil(drawn, slot);
        turning = total > shown && ui_scroll_moving(&terminal.bar);
        bar = !terminal.valid[slot] || terminal_bar_diff(slot, first, shown, total, turning) || full;
        bar_told = !screen || terminal_bar_diff(front, first, shown, total, turning) || full;
        margin = terminal.margin_spoiled[slot];
        any = bar || bar_told || margin || terminal_overlay_wanted() || terminal.overlay_bottom[slot] > terminal.overlay_top[slot];
        rows = min(ROWS, (unsigned int)TERMINAL_SPAN_ROWS), from = terminal_bar_from(), visible = total > shown;
        for (unsigned int row = 0; row < rows; row++)
        {
                const struct window_cell address_to line = now + (unsigned long)row * COLUMNS;
                unsigned int a0 = 0, a1 = COLUMNS, b0 = 0, b1 = COLUMNS;

                if (drawn)
                        terminal_span(line, drawn + (unsigned long)row * COLUMNS, &a0, &a1);
                if (screen)
                        terminal_span(line, screen + (unsigned long)row * COLUMNS, &b0, &b1);
                if (terminal_caret_index != ~0u && terminal_caret_index / COLUMNS == row)
                {
                        unsigned int col = terminal_caret_index % COLUMNS;
                        if (drawn && !terminal_caret_held(slot))
                                terminal_span_take(&a0, &a1, col);
                        if (screen && !terminal_caret_held(front))
                                terminal_span_take(&b0, &b1, col);
                }
                if (full)
                        a0 = 0, a1 = COLUMNS;
                if (bar || visible)
                {
                        unsigned int start = from && ((line[from].flags & WINDOW_CELL_WIDE_RIGHT) || (line[from - 1].flags & WINDOW_CELL_WIDE))
                                                 ? from - 1 : from;
                        a0 = a1 > a0 && a0 < start ? a0 : start, a1 = COLUMNS;
                }
                terminal_span_first[row] = (unsigned short)a0, terminal_span_last[row] = (unsigned short)(a1 > a0 ? a1 : a0);
                if (a1 > a0)
                        changed += a1 - a0, right |= a1 == COLUMNS;
                any |= a1 > a0 || b1 > b0;
                if (screen && b1 > b0)
                {
                        unsigned int top, height;
                        terminal_band(row, &top, &height);
                        terminal_damage_add(&x1, &y1, &x2, &y2, b0 ? (int)(terminal.pad + b0 * terminal.cell_w) : 0, (int)top,
                                            b1 < COLUMNS ? (int)(terminal.pad + b1 * terminal.cell_w) : (int)terminal.width, (int)(top + height));
                }
        }
        if (changed)
        {
                struct terminal_banding banding = {pixels, whole, now, drawn, rows, 1, bar || visible || full};
                terminal_cells_draw(&banding, changed);
        }
        if (margin)
                terminal_margin(whole, now, 0, 0, COLUMNS), terminal.margin_spoiled[slot] = false;
        terminal_grid_slide(whole, shift);
        if (pull)
        {
                struct ui kit = terminal_kit(whole);
                stretched = ui_scroll_pull(&kit, &terminal.bar, ui_body_px(&terminal.bar.body), (int)terminal.pad, (int)terminal.pad_y,
                                           (int)(COLUMNS * terminal.cell_w), terminal_grid_height());
        }
        if (!any)
                return terminal.forced = false, true;
        if (bar || right || visible)
                terminal_bar(whole, now, first, shown, total),
                terminal.bar_first[slot] = first, terminal.bar_shown[slot] = shown,
                terminal.bar_total[slot] = total, terminal.bar_drawn[slot] = TERMINAL_BAR_KEY();
        terminal_overlay(whole, slot);
        if (full)
                table->damage_x[own] = table->damage_y[own] = 0, table->damage_width[own] = terminal.width, table->damage_height[own] = terminal.height;
        else if (screen && ROWS <= TERMINAL_SPAN_ROWS)
        {
                if (margin)
                        terminal_damage_add(&x1, &y1, &x2, &y2, 0, 0, (int)terminal.width, (int)terminal.pad_y);
                if (bar_told && terminal.width >= UI_SCROLL_WIDTH * (unsigned int)terminal_scale() / 100)
                        terminal_damage_add(&x1, &y1, &x2, &y2, (int)terminal.width - UI_SCROLL_WIDTH * terminal_scale() / 100, 0,
                                            (int)terminal.width, (int)terminal.height);
                table->damage_x[own] = x2 > x1 ? x1 : 0, table->damage_y[own] = x2 > x1 ? y1 : 0;
                table->damage_width[own] = x2 > x1 ? x2 - x1 : 1, table->damage_height[own] = x2 > x1 ? y2 - y1 : 0;
        }
        else
                table->damage_width[own] = 0;
        terminal.frame[2] = terminal.frame[slot], terminal.frame[slot] = now;
        terminal_caret_store((unsigned int)slot);
        terminal.valid[slot] = 1, table->width[own] = terminal.width, table->height[own] = terminal.height;
        windows_show(&terminal.own, (unsigned int)slot);
        terminal.shift[slot] = shift, terminal.stretched[slot] = stretched, terminal.forced = false, terminal.shown = slot;
        return true;
}

/* ---- fonts, geometry, and zoom ---- */

static void terminal_margins(void)
{
        unsigned int margin = terminal.cell_h / 3 ? terminal.cell_h / 3 : 1;

        terminal.margin = terminal.pad = terminal.pad_y = margin;
}

static void terminal_said(b32 reading, string_address what);

static string_address terminal_ui_regular_path = (string_address)UI_FONT_REGULAR;
static string_address terminal_ui_bold_path = (string_address)UI_FONT_BOLD;

static b32 terminal_fonts(string_address address_to paths, unsigned int count, string_address bold, unsigned int size)
{
        unsigned int faces = 0;

        if (!ui_engine_open_sized(&terminal.ui, TERMINAL_ATLAS))
                return false;
        for (unsigned int at = 0; at <= count && faces < TERMINAL_FACE_SLOTS; at++)
        {
                string_address path = at < count ? paths[at] : bold;

                if (!path)
                        continue;
                if (!ui_face_load(&terminal.face[faces], path))
                {
                        p8 said[128];

                        string_copy_bounded(said, (string_address) " cannot use the font ", sizeof(said));
                        string_append_bounded(said, path, sizeof(said));
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
        if (terminal.bold_count)
                for (unsigned int at = 0; at < terminal.plain_count && terminal.bold_count < TERMINAL_FACES; at++)
                        terminal.bold[terminal.bold_count++] = terminal.plain[at];
        terminal_painters[0].ui.engine = terminal.ui.engine;
        terminal.ui.regular = ui_face_load(&terminal.ui.faces[0], terminal_ui_regular_path) ? &terminal.ui.faces[0] : terminal.plain[0];
        terminal.ui.bold = ui_face_load(&terminal.ui.faces[1], terminal_ui_bold_path) ? &terminal.ui.faces[1]
                           : terminal.bold_count                                      ? terminal.bold[0]
                                                                                      : terminal.plain[0];
        terminal.size = size << 6, terminal.grid = font_metrics_at(terminal.plain[0], terminal.size);
        terminal.cell_w = (unsigned int)terminal.grid.cell_width, terminal.cell_h = (unsigned int)terminal.grid.cell_height;
        terminal_margins();
        return true;
}

static unsigned int terminal_pixels(unsigned int columns, unsigned int rows, unsigned int address_to height)
{
        address_to height = rows * terminal.cell_h + 2 * terminal.margin;
        return columns * terminal.cell_w + terminal.pad;
}

static unsigned int terminal_columns(unsigned int width) { return width > terminal.pad ? (width - terminal.pad) / terminal.cell_w : 1; }
static unsigned int terminal_rows(unsigned int height) { return height > 2 * terminal.margin ? (height - 2 * terminal.margin) / terminal.cell_h : 1; }

static void terminal_open(unsigned int wide, unsigned int high, unsigned int left, unsigned int top,
                          unsigned int address_to columns, unsigned int address_to rows, const unsigned int address_to cells)
{
        struct windows address_to table = terminal.own.table;
        unsigned int display_w, display_h, pixels_w, pixels_h;
        int display_x, display_y, screen_w, screen_h;

        __atomic_store_n(&table->hover_off[terminal.own.row], 1u, __ATOMIC_RELEASE);
        terminal.hovering = false;
        __atomic_store_n(&table->pinch_wanted[terminal.own.row], 1u, __ATOMIC_RELEASE);
        windows_output(table, 0, &display_x, &display_y, &screen_w, &screen_h);
        display_w = min((unsigned int)screen_w, table->output_width), display_h = min((unsigned int)screen_h, table->output_height);
        terminal.pitch = table->stride;
        address_to columns = terminal_columns(display_w * wide / 100);
        address_to rows = terminal_rows(display_h * high / 100);
        address_to columns = address_to columns >= 20 ? address_to columns : min(20u, terminal_columns(display_w));
        address_to rows = address_to rows >= 4 ? address_to rows : min(4u, terminal_rows(display_h));
        if (cells[0] && cells[1])
                address_to columns = min(cells[0], terminal_columns(display_w)), address_to rows = min(cells[1], terminal_rows(display_h));
        pixels_w = terminal_pixels(address_to columns, address_to rows, &pixels_h);
        terminal.width = min(pixels_w, display_w), terminal.height = min(pixels_h, display_h);
        table->x[terminal.own.row] = display_x + (cells[0] && cells[1] ? ((int)display_w - (int)terminal.width) / 2 : (int)(display_w * left / 100));
        table->y[terminal.own.row] = display_y + (cells[0] && cells[1] ? ((int)display_h - (int)terminal.height) / 2 : (int)(display_h * top / 100));
        if (!terminal.reading)
                windows_place_free(table, terminal.own.row);
}

static void terminal_configure(b32 master, unsigned int width, unsigned int height)
{
        struct windows address_to table = terminal.own.table;
        unsigned int columns = max(terminal_columns(width), 1u), rows = min(max(terminal_rows(height), 1u), window->history);
        unsigned int pixels_h, pixels_w = terminal_pixels(columns, rows, &pixels_h);

        pixels_w = min(max(width, pixels_w), table->output_width), pixels_h = min(max(height, pixels_h), table->output_height);
        terminal.pad_y = terminal.margin + (pixels_h > rows * terminal.cell_h + 2 * terminal.margin
                                                ? (pixels_h - rows * terminal.cell_h - 2 * terminal.margin) / 2 : 0);
        if (pixels_w == terminal.width && pixels_h == terminal.height && columns == window->columns && rows == window->rows)
                return;
        window->columns = columns, window->rows = rows;
        regrid(-1);
        grid_tell(master);
        terminal_view_clamp();
        terminal_frames();
        terminal.width = pixels_w, terminal.height = pixels_h;
}

static unsigned int terminal_size_read(void)
{
        p8 text[8] = {0};
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address)TERMINAL_SIZE_FILE, text, sizeof(text));
        positive size = got > 0 ? (positive)got : 0, used = 0;
        unsigned int value = (unsigned int)string_digits_max((string_address)text, size, &used);

        if (got <= 0 || (used < size && (text[used] != '\n' || used + 1 != size)))
                return 0;
        for (unsigned int at = 0; at < TERMINAL_STEPS; at++)
                if (terminal_steps[at] == value)
                        return value;
        return 0;
}

static unsigned int terminal_step_index(unsigned int size)
{
        unsigned int at = 0;

        while (at + 1 < TERMINAL_STEPS && terminal_steps[at + 1] <= size)
                at++;
        return at;
}

static b32 terminal_size_write(unsigned int size)
{
        p8 text[12];
        positive length = positive_into_string(text, size);
        bipolar handle;
        b32 wrote;

        text[length++] = '\n';
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)TERMINAL_SIZE_DIR, 0755, 0, 0, 0);
        if ((handle = system_open_at_mode(AT_FDCWD, (string_address)TERMINAL_SIZE_TEMP, FILE_WRITE | O_CLOEXEC | O_NOFOLLOW, 0644)) < 0)
                return false;
        wrote = system_write_all((positive)handle, text, length) == length;
        system_close(handle);
        return wrote && compositor_call(syscall(renameat2), (positive)AT_FDCWD, (positive)TERMINAL_SIZE_TEMP,
                                        (positive)AT_FDCWD, (positive)TERMINAL_SIZE_FILE, 0, 0) >= 0;
}

static void terminal_text_set(b32 master, unsigned int size)
{
        terminal.size = size << 6, terminal.grid = font_metrics_at(terminal.plain[0], terminal.size);
        terminal.cell_w = term_cell_w = (unsigned int)terminal.grid.cell_width;
        terminal.cell_h = term_cell_h = (unsigned int)terminal.grid.cell_height;
        terminal_margins();
        terminal.valid[0] = terminal.valid[1] = 0;
        terminal_configure(master, terminal.width, terminal.height);
}

static void terminal_text_said(b32 reading)
{
        p8 text[64];
        positive length = 0;
        const char address_to piece[4] = {" text size ", " px, cell ", " by ", " px"};
        unsigned int values[3] = {terminal.size >> 6, terminal.cell_w, terminal.cell_h};

        for (unsigned int at = 0; at < 3; at++)
                length += string_copy_bounded(text + length, (string_address)piece[at], sizeof(text) - length),
                length += positive_into_string(text + length, values[at]);
        string_copy_bounded(text + length, (string_address)piece[3], sizeof(text) - length);
        terminal_said(reading, (string_address)text);
}

static void terminal_size_apply(b32 master, unsigned int step)
{
        if (terminal_steps[step] != terminal.size >> 6)
                terminal_size_write(terminal_steps[step]), terminal_text_set(master, terminal_steps[step]);
}

static void terminal_zoom(b32 master, int by)
{
        unsigned int at = terminal_step_index(terminal.size >> 6);

        terminal_size_apply(master, by == 2 ? terminal_step_index(TERMINAL_SIZE) : by > 0 ? min(at + 1, TERMINAL_STEPS - 1u) : at ? at - 1 : 0);
}

static int terminal_pinch_base;

static void terminal_pinch(const struct window_event address_to event)
{
        unsigned int now = terminal.size >> 6;

        if (event->flags & WINDOW_PINCH_BEGIN)
                terminal_pinch_base = (int)now;
        terminal_size_apply(terminal.master, ui_pinch_steps(terminal_steps, TERMINAL_STEPS, terminal_step_index(now),
                                                            ui_pinch_size(terminal_pinch_base, event->a)));
}

/* ---- input translation, touch scroll, selection, and clipboard ---- */

static void terminal_key(const struct window_event address_to event, struct window_key address_to key)
{
        unsigned int flags = event->flags, character = (unsigned int)event->b;

        key->code = (unsigned int)event->a;
        key->flags = flags & (WINDOW_KEY_DOWN | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_ALTGR);
        if ((flags & WINDOW_KEY_CONTROL) && (unsigned int)((character | 0x20) - 'a') < 26)
                character &= 0x1f;
        key->character = event->a == UI_KEY_BACKSPACE ? 127 : character;
        key->reserved = 0;
}

static void terminal_pointer_key(unsigned int button, unsigned int flags, struct window_key address_to key)
{
        unsigned int col = min((unsigned int)(terminal_at_x() < 0 ? 0 : terminal_at_x()) / terminal.cell_w, COLUMNS - 1);
        unsigned int row = min((unsigned int)(terminal_at_y() < 0 ? 0 : terminal_at_y()) / terminal.cell_h, ROWS - 1);

        button += (terminal.modifiers & WINDOW_KEY_SHIFT ? 4u : 0u) + (terminal.modifiers & WINDOW_KEY_ALT ? 8u : 0u) +
                  (terminal.modifiers & WINDOW_KEY_CONTROL ? 16u : 0u);
        key->code = 0, key->character = button, key->flags = WINDOW_KEY_POINTER | flags;
        key->reserved = ((col + 1) & 0xffff) | (((row + 1) & 0xffff) << 16);
}

static positive terminal_period(void) { return terminal.own.table->period ? terminal.own.table->period : TERMINAL_FRAME_NANOSECONDS; }

static void terminal_modes_read(b32 master, positive now)
{
        terminal.modes_at = now, terminal.modes_fresh = true;
        term_follow_modes(master);
}

static b32 terminal_alt_arrows(b32 master, int lines)
{
        unsigned int code = lines > 0 ? KEY_UP : KEY_DOWN;
        b32 editing = line_editing;

        line_editing = false;
        for (int at = lines < 0 ? -lines : lines; at > 0; at--)
                term_key_modified(0, code, 0);
        line_editing = editing;
        return term_send(master);
}

/* Whole units out of a running amount: the rest stays in *left for the next turn (a zero unit takes none). */
static int terminal_take(int address_to left, long amount, long unit)
{
        long total = (long)*left + amount, whole = unit ? total / unit : 0;

        *left = (int)(total - whole * unit);
        return (int)whole;
}

static b32 terminal_finger_scroll(b32 master, int pixels, b32 address_to dirty, b32 address_to moved)
{
        int lines = terminal_take(&terminal.finger_left, pixels, terminal.cell_h);

        *moved = true;
        lines = lines > (int)ROWS ? (int)ROWS : lines < -(int)ROWS ? -(int)ROWS : lines;
        if (!lines)
                return true;
        if (alternate)
                return terminal_alt_arrows(master, lines);
        if ((*moved = terminal_view_scroll(lines)))
                *dirty = true;
        return true;
}

static b32 terminal_finger(b32 master, const struct window_event address_to event, b32 address_to dirty, b32 address_to handled)
{
        struct ui kit;
        b32 moved, dragged;

        *handled = false;
        if (!ui_kit_touch || mouse_mode || terminal.bar.held)
                return true;
        if (event->type == WINDOW_EVENT_MOTION && terminal.finger.down)
        {
                int moved_px, shift = 0, drawn = 0;

                kit = terminal.ui, kit.scale = terminal_scale(), *handled = true;
                if (!alternate)
                        shift = terminal_grid_shift(), drawn = ui_body_px(&terminal.bar.body), terminal_bar_sync();
                moved_px = ui_press_follow(&kit, &terminal.finger, terminal_at_x(), terminal_at_y());
                if (!alternate)
                {
                        if (terminal.finger.dragged)
                                ui_body_track(&terminal.bar.body, -(b64)moved_px * UI_ONE, ui_kit_time());
                        if (terminal_bar_out() | (terminal_grid_shift() != shift) | (ui_body_px(&terminal.bar.body) != drawn))
                                *dirty = true;
                        return true;
                }
                return terminal.finger.dragged ? terminal_finger_scroll(master, moved_px, dirty, &moved) : true;
        }
        if (event->type != WINDOW_EVENT_BUTTON || event->a != UI_POINTER_LEFT ||
            ((event->flags & WINDOW_KEY_DOWN) && terminal_bar_event(0, 0, 0, false, null)))
                return true;
        *handled = true;
        if (event->flags & WINDOW_KEY_DOWN)
                return ui_press_down(&terminal.finger, terminal_at_x(), terminal_at_y(), 0, null),
                       ui_body_hold(alternate ? &terminal.alt : &terminal.bar.body, ui_kit_time()), terminal.finger_left = 0, true;
        if (!terminal.finger.down)
                return true;
        dragged = terminal.finger.dragged;
        ui_press_lift(&terminal.finger, (event->flags & WINDOW_KEY_CANCEL) != 0);
        if (dragged)
        {
                long speed = ui_press_velocity(&terminal.finger, ui_press_now());
                if (!alternate)
                        ui_body_let_go(&terminal.bar.body, -(b64)speed * UI_ONE, ui_kit_time(), terminal_scale(), true), terminal.dirty = true;
                else
                        ui_body_free(&terminal.alt, speed, ui_kit_time(), terminal_scale(), true);
        }
        return true;
}

#define TERMINAL_PLACE 65536
#define TERMINAL_LINE_OF(p) ((unsigned int)((p) / TERMINAL_PLACE))
#define TERMINAL_COLUMN_OF(p) ((unsigned int)((p) % TERMINAL_PLACE))
static b32 terminal_selecting(void);
static void terminal_select_set(p64 anchor, p64 active);
static struct ui_select address_to terminal_sel(void);

static b32 terminal_place_at(unsigned int row, unsigned int column, unsigned int address_to line_out, unsigned int address_to column_out)
{
        struct term_view_iter it;

        for (term_view_begin(&it, terminal.view); term_view_next(&it, row + 1); term_view_step(&it))
                if (it.row == row)
                        return *line_out = it.line, *column_out = it.fold * COLUMNS + column, true;
        *line_out = window->head - 1, *column_out = window_lengths(window)[(window->head - 1) % window->history];
        return false;
}

static void terminal_select_span(unsigned int address_to first_line, unsigned int address_to first_column,
                                 unsigned int address_to last_line, unsigned int address_to last_column)
{
        unsigned int al = TERMINAL_LINE_OF(terminal.sel_anchor), ac = TERMINAL_COLUMN_OF(terminal.sel_anchor);
        unsigned int bl = TERMINAL_LINE_OF(terminal.sel_active), bc = TERMINAL_COLUMN_OF(terminal.sel_active);
        b32 fwd = al < bl || (al == bl && ac <= bc);

        *first_line = fwd ? al : bl, *first_column = fwd ? ac : bc;
        *last_line = fwd ? bl : al, *last_column = fwd ? bc : ac;
}

static b32 terminal_line_kept(unsigned int line) { return window->head - line <= window->history && window->head - line > 0; }
static p64 terminal_place(unsigned int line, unsigned int column) { return (p64)line * TERMINAL_PLACE + column; }

#define TERMINAL_WORD_CELLS 256
static struct doc terminal_words;

static void terminal_word_span(unsigned int line, unsigned int column, p64 address_to start, p64 address_to finish)
{
        unsigned int slot = line % window->history, stride = window->stride, stored = min(window_lengths(window)[slot], stride);
        const struct window_cell address_to cells = window_cells(window) + (unsigned long)slot * stride;
        unsigned int lo, hi, cs, cf;
        positive offsets[TERMINAL_WORD_CELLS + 1], length = 0, first, last;
        p8 bytes[TERMINAL_WORD_CELLS * 4];

        *start = *finish = terminal_place(line, column);
        if (column >= stored)
                return;
        if (column && (cells[column].flags & WINDOW_CELL_WIDE_RIGHT))
                column--;
        lo = column > TERMINAL_WORD_CELLS / 2 ? column - TERMINAL_WORD_CELLS / 2 : 0;
        hi = min(lo + TERMINAL_WORD_CELLS, stored);
        for (unsigned int c = lo; c < hi; c++)
        {
                unsigned int code = cells[c].character;
                offsets[c - lo] = length;
                if (!(cells[c].flags & WINDOW_CELL_WIDE_RIGHT))
                        length += memory_utf8_encode(bytes + length, 4, code ? code : ' ');
        }
        offsets[hi - lo] = length;
        doc_word_in(&terminal_words, bytes, length, offsets[column - lo], &first, &last);
        for (cs = lo; cs < hi && ((cells[cs].flags & WINDOW_CELL_WIDE_RIGHT) || offsets[cs - lo] < first); cs++)
                ;
        for (cf = lo; cf < hi && ((cells[cf].flags & WINDOW_CELL_WIDE_RIGHT) || offsets[cf - lo] < last); cf++)
                ;
        *start = terminal_place(line, cs), *finish = terminal_place(line, cf > cs ? cf : min(cs + 1, hi));
}

static void terminal_sel_bounds(unsigned int line, unsigned int fl, unsigned int fc, unsigned int ll, unsigned int lc,
                                unsigned int full_stop, unsigned int address_to start, unsigned int address_to stop)
{
        if (terminal.select_box)
        {
                unsigned int a = TERMINAL_COLUMN_OF(terminal.sel_anchor), b = TERMINAL_COLUMN_OF(terminal.sel_active);
                *start = min(a, b), *stop = max(a, b) + 1;
        }
        else
                *start = line == fl ? fc : 0, *stop = line == ll ? lc : full_stop;
}

static void terminal_select_mark(struct window_cell address_to out, unsigned int line, unsigned int from)
{
        unsigned int fl, fc, ll, lc, start, stop;

        if (!terminal_selecting())
                return;
        terminal_select_span(&fl, &fc, &ll, &lc);
        if (line - fl > ll - fl)
                return;
        terminal_sel_bounds(line, fl, fc, ll, lc, ~0u, &start, &stop);
        for (unsigned int col = start > from ? start - from : 0; col < COLUMNS && from + col < stop; col++)
                out[col].flags |= WINDOW_CELL_SELECTED;
}

static positive terminal_select_text(p8 address_to address_to into, positive address_to room)
{
        unsigned int fl, fc, ll, lc;
        positive length = 0;

        *into = null, *room = 0;
        if (!terminal_selecting())
                return 0;
        terminal_select_span(&fl, &fc, &ll, &lc);
        if (window->head - ll == 0 || window->head - ll > window->history)
                return 0;
        if (window->head - fl > window->history)
        {
                fl = window->head - window->history;
                if (!terminal.select_box)
                        fc = 0;
        }
        for (unsigned int line = fl;; line++)
        {
                unsigned int slot = line % window->history, stored = min(window_lengths(window)[slot], window->stride), start, stop, finish;
                const struct window_cell address_to cells = window_cells(window) + (unsigned long)slot * window->stride;

                terminal_sel_bounds(line, fl, fc, ll, lc, stored, &start, &stop);
                finish = min(stop, stored);
                if (start && start < stored && (cells[start].flags & WINDOW_CELL_WIDE_RIGHT))
                        start--;
                while (finish > start && (cells[finish - 1].character == ' ' || !cells[finish - 1].character))
                        finish--;
                if (terminal_line_kept(line) && array_store_reserve(*into, *room, length, length + (positive)(finish > start ? finish - start : 0) * 4 + 2, 4096))
                        for (unsigned int col = start; col < finish; col++)
                                if (!(cells[col].flags & WINDOW_CELL_WIDE_RIGHT))
                                        length += memory_utf8_encode(*into + length, 4, cells[col].character ? cells[col].character : ' ');
                if (line == ll)
                        break;
                if (*room > length)
                        (*into)[length++] = '\n';
        }
        return length;
}

static void terminal_copy(unsigned int which)
{
        p8 address_to text;
        positive room, length = terminal_select_text(&text, &room);

        ui_clip_offer(&terminal.own, which, text, room, length);
}

#define TERMINAL_MENU_COPY 1
#define TERMINAL_MENU_PASTE 2
#define TERMINAL_MENU_ALL 3
#define TERMINAL_MENU_ZOOM_IN 4
#define TERMINAL_MENU_ZOOM_OUT 5
#define TERMINAL_MENU_ZOOM_RESET 6

static struct ui_popup terminal_menu;

static fn terminal_clip_act(int action)
{
        if (action == 1)
                terminal_copy(WINDOWS_CLIP_CLIPBOARD);
        else if (action == 2 && !terminal.reading)
                windows_clip_ask(&terminal.own, WINDOWS_CLIP_CLIPBOARD);
        else if (action == 3)
                terminal.select_box = false,
                terminal_select_set(terminal_place(terminal_oldest(), 0), terminal_place(window->head - 1, window->stride)),
                terminal.sel.handles = true;
        terminal.dirty = true;
}

static fn terminal_menu_open(int x, int y, b32 touch)
{
        struct ui_popmenu address_to m = &terminal_menu.menu;
        const struct ui_menu_spec specs[] = {
            {null, null, UI_MENU_SEPARATOR, 0, false},
            {WORDS(menu_zoom_in), "Ctrl+=", 0, TERMINAL_MENU_ZOOM_IN, false},
            {WORDS(menu_zoom_out), "Ctrl+-", 0, TERMINAL_MENU_ZOOM_OUT, false},
            {WORDS(menu_reset_zoom), "Ctrl+0", 0, TERMINAL_MENU_ZOOM_RESET, false},
        };

        m->count = 0;
        ui_menu_clip_rows(m, TERMINAL_MENU_COPY, "Ctrl+Shift+C", terminal_selecting() ? 0 : UI_MENU_DISABLED, TERMINAL_MENU_PASTE,
                          "Ctrl+Shift+V", terminal.reading ? UI_MENU_DISABLED : 0, TERMINAL_MENU_ALL, null, 0);
        ui_popmenu_build(m, specs, (int)(sizeof(specs) / sizeof(specs[0])));
        ui_popmenu_open(&terminal_menu, &terminal.own, &terminal.ui, x, y, 0, 0, WINDOW_POPUP_POINT, touch);
}

static fn terminal_chose(int id)
{
        if (id >= TERMINAL_MENU_COPY && id <= TERMINAL_MENU_ALL)
                terminal_clip_act(id);
        else if (id >= TERMINAL_MENU_ZOOM_IN && id <= TERMINAL_MENU_ZOOM_RESET)
                terminal_zoom(terminal.master, id == TERMINAL_MENU_ZOOM_IN ? 1 : id == TERMINAL_MENU_ZOOM_OUT ? -1 : 2), terminal.dirty = true;
}

/* ---- ui_select_ops client ---- */

static b32 terminal_selecting(void) { return terminal.sel_anchor != terminal.sel_active; }
static void terminal_select_set(p64 anchor, p64 active) { terminal.sel_anchor = anchor, terminal.sel_active = active; }
static void terminal_select_clear(void) { terminal_select_set(0, 0); terminal.sel.handles = terminal.sel.bar = false; }
static b32 terminal_overlay_wanted(void) { return (terminal.sel.handles && terminal_selecting()) || terminal.sel.bar; }

static struct ui terminal_sel_kit(void)
{
        struct ui kit = terminal.ui;
        kit.scale = terminal_scale();
        return kit;
}

static void terminal_overlay_spoil(struct window_cell address_to drawn, int slot)
{
        if (terminal.overlay_bottom[slot] > terminal.overlay_top[slot])
                terminal_spoil_rows(drawn, terminal.overlay_top[slot], terminal.overlay_bottom[slot], false);
}

static void terminal_overlay(unsigned int address_to whole, int slot)
{
        struct ui kit;
        struct ui_select address_to s = terminal_sel();

        terminal.ui.scale = terminal_scale();
        ui_scratch_fit(&terminal.ui, terminal.width);
        kit = terminal_kit(terminal_grid(whole));
        kit.width = (int)terminal.width - (int)terminal.pad, kit.height = kit.bottom = (int)terminal.height - (int)terminal.pad_y;
        s->over_top = s->over_bottom = 0;
        ui_select_overlay(&kit, s);
        terminal.overlay_top[slot] = s->over_top, terminal.overlay_bottom[slot] = s->over_bottom;
}

static int terminal_op_where(struct ui address_to ui, void address_to self, p64 at, int address_to x, int address_to top)
{
        unsigned int want_line = TERMINAL_LINE_OF(at), want_column = TERMINAL_COLUMN_OF(at), first_line;
        struct term_view_iter it;

        (void)ui, (void)self;
        term_view_begin(&it, terminal.view);
        first_line = it.line;
        for (; term_view_next(&it, ROWS); term_view_step(&it))
                if (it.line == want_line && it.fold == min(want_column / COLUMNS, it.folds - 1))
                        return *x = (int)((want_column - it.fold * COLUMNS) * terminal.cell_w),
                               *top = (int)(it.row * terminal.cell_h) + terminal_grid_shift(), 0;
        return want_line < first_line ? -1 : 1;
}

static int terminal_op_items(void address_to self, int address_to ids)
{
        int count = 0;

        (void)self;
        if (terminal_selecting())
                ids[count++] = UI_SELECT_COPY;
        if (!terminal.reading)
                ids[count++] = UI_SELECT_PASTE;
        if (terminal.sel_anchor != terminal_place(terminal_oldest(), 0) || terminal.sel_active != terminal_place(window->head - 1, window->stride))
                ids[count++] = UI_SELECT_ALL;
        return count;
}

static p32 terminal_op_item(struct ui address_to ui, void address_to self, int id)
{
        (void)ui, (void)self;
        terminal_clip_act(id == UI_SELECT_COPY ? 1 : id == UI_SELECT_PASTE ? 2 : id == UI_SELECT_ALL ? 3 : 0);
        return UI_TEXT_MOVED;
}

static void terminal_op_box(void address_to self, struct ui_select_box address_to b)
{
        (void)self;
        *b = (struct ui_select_box){0, 0, (int)(COLUMNS * terminal.cell_w), (int)(ROWS * terminal.cell_h), (int)terminal.cell_h, false};
}

static p64 terminal_op_place(struct ui address_to ui, void address_to self, int x, int y)
{
        unsigned int row = min((unsigned int)(y < 0 ? 0 : y) / terminal.cell_h, ROWS - 1);
        unsigned int col = min((unsigned int)(x < 0 ? 0 : x) / terminal.cell_w, COLUMNS), line, at;

        (void)ui, (void)self;
        terminal_place_at(row, col, &line, &at);
        return terminal_place(line, at);
}

static void terminal_op_span(void address_to self, int kind, p64 at, p64 address_to start, p64 address_to finish)
{
        unsigned int line = TERMINAL_LINE_OF(at), col = TERMINAL_COLUMN_OF(at);

        (void)self;
        if (kind == UI_SELECT_LINES)
                *start = terminal_place(line, 0), *finish = terminal_place(line, window->stride);
        else if (kind == UI_SELECT_WORDS)
                terminal_word_span(line, col, start, finish);
        else
                *start = *finish = at;
}

static void terminal_op_get(void address_to self, p64 address_to a, p64 address_to b) { (void)self; *a = terminal.sel_anchor, *b = terminal.sel_active; }
static void terminal_op_set(void address_to self, p64 a, p64 b) { (void)self; terminal_select_set(a, b); }
static b32 terminal_op_any(void address_to self) { (void)self; return terminal_selecting(); }
static b32 terminal_op_within(void address_to self, p64 at)
{
        (void)self;
        return at >= min(terminal.sel_anchor, terminal.sel_active) && at <= max(terminal.sel_anchor, terminal.sel_active);
}
static b32 terminal_op_inside(void address_to self, int x, int y)
{
        (void)self;
        return x >= 0 && y >= 0 && x < (int)(COLUMNS * terminal.cell_w) && y < (int)(ROWS * terminal.cell_h);
}
static void terminal_op_scroll(struct ui address_to ui, void address_to self, long lines, b32 edge)
{
        (void)ui, (void)self;
        if (edge && !alternate && lines && terminal_view_scroll(-(int)lines))
                terminal.dirty = true;
}
static void terminal_op_moved(struct ui address_to ui, void address_to self, b32 reveal) { (void)ui, (void)self, (void)reveal; }
static void terminal_op_copy(void address_to self, unsigned int which) { (void)self; terminal_copy(which); }

static const struct ui_select_ops terminal_select_ops = {
    .box = terminal_op_box,       .place = terminal_op_place,   .span = terminal_op_span,   .where = terminal_op_where,
    .get = terminal_op_get,       .set = terminal_op_set,       .extend = terminal_op_set,  .any = terminal_op_any,
    .shown = terminal_op_any,     .within = terminal_op_within, .inside = terminal_op_inside, .scroll = terminal_op_scroll,
    .moved = terminal_op_moved,   .copy = terminal_op_copy,     .items = terminal_op_items, .item = terminal_op_item,
};

static struct ui_select address_to terminal_sel(void)
{
        terminal.sel.ops = &terminal_select_ops, terminal.sel.self = &terminal;
        return &terminal.sel;
}

static b32 terminal_select_event(const struct window_event address_to event, b32 address_to dirty)
{
        b32 shift = (terminal.modifiers & WINDOW_KEY_SHIFT) != 0, alt = (terminal.modifiers & WINDOW_KEY_ALT) != 0;
        struct ui kit = terminal_sel_kit();
        struct ui_select address_to s = terminal_sel();

        if (mouse_mode && !shift && (event->type == WINDOW_EVENT_BUTTON ? (event->flags & WINDOW_KEY_DOWN) || !s->dragging : !s->dragging))
                return s->dragging = false;
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_MIDDLE && (event->flags & WINDOW_KEY_DOWN))
                return terminal.reading ? true : (windows_clip_ask(&terminal.own, WINDOWS_CLIP_PRIMARY), true);
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_RIGHT)
        {
                if (event->flags & WINDOW_KEY_DOWN)
                        terminal_menu_open(ui_kit.pointer_x, ui_kit.pointer_y, ui_kit_touch);
                return true;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_LEFT)
        {
                unsigned int row, col, line, at;
                p64 place;
                int clicks;

                if (!(event->flags & WINDOW_KEY_DOWN))
                {
                        if (!s->dragging)
                                return false;
                        s->dragging = false, ui_kit_shape = WINDOW_POINTER_ARROW;
                        if (terminal_selecting())
                                terminal_copy(WINDOWS_CLIP_PRIMARY);
                        return address_to dirty = true;
                }
                if (terminal_at_x() < 0 || terminal_cell_y() < 0)
                        return false;
                row = (unsigned int)terminal_cell_y() / terminal.cell_h, col = (unsigned int)terminal_at_x() / terminal.cell_w;
                if (row >= ROWS || col >= COLUMNS)
                        return false;
                terminal_place_at(row, col, &line, &at);
                place = terminal_place(line, at);
                clicks = ui_click_count(&kit, &s->click, terminal_at_x(), terminal_cell_y());
                if (shift && terminal_selecting() && clicks == 1)
                        ui_select_shift(&kit, s, place);
                else
                        terminal.select_box = alt && clicks == 1, ui_select_start(s, place, clicks, true);
                s->dragging = true, ui_kit_shape = WINDOW_POINTER_TEXT, s->edge.past = 0;
                terminal.sel.handles = terminal.sel.bar = false;
                return address_to dirty = true;
        }
        if (event->type == WINDOW_EVENT_MOTION && s->dragging)
        {
                p64 to = ui_select_track(&kit, s, terminal_at_x(), terminal_cell_y(), terminal_at_x(), terminal_cell_y(), 0, false);

                ui_kit_shape = WINDOW_POINTER_TEXT;
                if (to != terminal.sel_active)
                        ui_select_drag_to(&kit, s, to, false), address_to dirty = true;
                return true;
        }
        return false;
}

static b32 terminal_select_key(const struct window_event address_to event, b32 address_to dirty)
{
        int clip = ui_clip_chord(event);

        if (clip)
        {
                if (event->flags & WINDOW_KEY_DOWN)
                        clip == UI_CLIP_COPY ? terminal_copy(WINDOWS_CLIP_CLIPBOARD) : windows_clip_ask(&terminal.own, WINDOWS_CLIP_CLIPBOARD);
                return true;
        }
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && terminal_selecting() && !ui_key_modifier(event->a))
                terminal_select_clear(), address_to dirty = true;
        return false;
}

static struct ui_drop terminal_drop;

static b32 terminal_drop_event(b32 master, const struct window_event address_to event)
{
        static const char address_to const names[4] = {WINDOWS_CLIP_URIS, WINDOWS_CLIP_TEXT, "text/plain", "UTF8_STRING"};
        int what = ui_drop_event(&terminal_drop, event);
        p8 address_to bytes, address_to quoted;
        unsigned long length;
        positive room, typed;

        if (what == UI_DROP_OVER)
                ui_drop_pick(&terminal.own, &terminal_drop, names, 4, WINDOWS_DRAG_COPY, WINDOWS_DRAG_COPY);
        if (what != UI_DROP_READY)
                return true;
        if (!ui_drop_read(&terminal.own, &terminal_drop, &bytes, &length))
                return ui_drop_done(&terminal.own, &terminal_drop, false), true;
        cursor_hide();
        if (windows_clip_same(terminal_drop.name, string_length((string_address)terminal_drop.name), WINDOWS_CLIP_URIS))
        {
                if ((typed = ui_uris_quoted(bytes, (positive)length, &quoted, &room)))
                        term_paste(quoted, typed);
                array_store_release(quoted, room, typed);
        }
        else
                term_paste(bytes, length);
        windows_clip_free(bytes, length);
        ui_drop_done(&terminal.own, &terminal_drop, true);
        return term_send(master);
}

static b32 terminal_clip_event(b32 master, const struct window_event address_to event)
{
        p8 address_to bytes;
        unsigned long length;

        if (ui_clip_read(&terminal.own, event, true, &bytes, &length))
        {
                cursor_hide();
                term_paste(bytes, length);
                windows_clip_free(bytes, length);
                return term_send(master);
        }
        return true;
}

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
                if (((event->flags & WINDOW_KEY_DOWN) && (terminal.bar.held || terminal_bar_event(0, 0, 0, false, null))) ||
                    (!(event->flags & WINDOW_KEY_DOWN) && !s->finger && !(event->flags & WINDOW_KEY_HOLD)))
                        return false;
                if (ui_select_finger(&kit, s, event, terminal_at_x(), terminal_at_y()) & UI_TEXT_MOVED)
                        address_to dirty = true;
                return (event->flags & WINDOW_KEY_HOLD) != 0 && !(event->flags & WINDOW_KEY_DOWN);
        }
        if (event->a == UI_POINTER_RIGHT && (event->flags & WINDOW_KEY_HOLD))
        {
                if (ui_select_finger(&kit, s, event, terminal_at_x(), terminal_at_y()) & UI_TEXT_MOVED)
                        address_to dirty = true;
                return true;
        }
        return false;
}

static b32 terminal_receive(b32 master, const struct window_event address_to event, b32 address_to dirty)
{
        struct window_key key;

        if (terminal_touch_select(event, dirty))
                return true;
        if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
        {
                b32 handled, sent = terminal_finger(master, event, dirty, &handled);
                if (handled)
                        return sent;
        }
        if (event->type == WINDOW_EVENT_KEY || event->type == WINDOW_EVENT_WHEEL)
                ui_body_hold(&terminal.alt, ui_kit_time());

        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                terminal.modifiers = event->flags;
                if (terminal_select_key(event, dirty))
                        return true;
                if ((event->flags & WINDOW_KEY_DOWN) && (event->flags & WINDOW_KEY_SHIFT) &&
                    !(event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) && !alternate &&
                    (event->a == UI_KEY_PAGE_UP || event->a == UI_KEY_PAGE_DOWN))
                {
                        if (terminal_view_scroll(event->a == UI_KEY_PAGE_UP ? (int)ROWS - 1 : 1 - (int)ROWS))
                                address_to dirty = true;
                        return true;
                }
                if (!terminal.modes_fresh)
                        terminal_modes_read(master, (positive)ui_kit.now);
                terminal_key(event, &key);
                if ((key.flags & WINDOW_KEY_DOWN) && !ui_key_modifier(event->a))
                {
                        if (terminal_view_live())
                                address_to dirty = true;
                        cursor_hide();
                        terminal_caret_restart((positive)ui_kit.now);
                }
                return term_input(master, &key);

        case WINDOW_EVENT_MOTION:
        {
                b32 moved = false, lit = terminal.bar.hover;

                if (terminal.bar.held || terminal.bar.hover || terminal_bar_event(0, 0, 0, false, null))
                        terminal_bar_event(WINDOW_EVENT_MOTION, event->a, event->b, false, &moved);
                if (moved || lit != terminal.bar.hover)
                        address_to dirty = true;
                if (terminal.bar.held || terminal_select_event(event, dirty) || !mouse_mode)
                        return true;
                terminal_pointer_key(terminal.pressed ? terminal.pressed_button : 3,
                                     WINDOW_KEY_POINTER_MOVE | (terminal.pressed ? WINDOW_KEY_DOWN : 0), &key);
                return term_input(master, &key);
        }
        case WINDOW_EVENT_BUTTON:
        {
                unsigned int button = event->a == UI_POINTER_RIGHT ? 2 : event->a == UI_POINTER_MIDDLE ? 1 : 0;
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

                if (event->a != UI_POINTER_LEFT && event->a != UI_POINTER_RIGHT && event->a != UI_POINTER_MIDDLE)
                        return true;
                if (event->a == UI_POINTER_LEFT && (terminal.bar.held || (down && terminal_bar_event(0, 0, 0, false, null))))
                {
                        b32 moved = false;
                        terminal_bar_event(WINDOW_EVENT_BUTTON, ui_kit.pointer_x, ui_kit.pointer_y, down, &moved);
                        if (moved)
                                address_to dirty = true;
                        return true;
                }
                if ((event->a == UI_POINTER_LEFT && (down || !terminal.pressed) && terminal_at_x() >= (int)(COLUMNS * terminal.cell_w)) ||
                    terminal_select_event(event, dirty))
                        return true;
                terminal.pressed = down;
                if (down)
                        terminal.pressed_button = button;
                terminal_pointer_key(button, down ? WINDOW_KEY_DOWN : 0, &key);
                return term_input(master, &key);
        }
        case WINDOW_EVENT_CLIP: return terminal_clip_event(master, event);
        case WINDOW_EVENT_DRAG: return terminal_drop_event(master, event);
        case WINDOW_EVENT_WHEEL:
        {
                int steps = event->b ? 0 : event->a, cap = (int)ROWS * 120;

                steps = steps > cap ? cap : steps < -cap ? -cap : steps;
                if (mouse_mode && !(!alternate && (event->flags & WINDOW_KEY_SHIFT)))
                {
                        b32 sent = true;

                        terminal.wheel_notches += steps;
                        terminal.wheel_notches = terminal.wheel_notches > cap ? cap : terminal.wheel_notches < -cap ? -cap : terminal.wheel_notches;
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
                        return terminal_bar_turn(event, dirty), true;
                {
                        int lines = terminal_take(&terminal.wheel_remainder, (long)steps * (long)terminal_wheel_lines(), 120);

                        lines = lines > (int)ROWS ? (int)ROWS : lines < -(int)ROWS ? -(int)ROWS : lines;
                        return lines ? terminal_alt_arrows(master, lines) : true;
                }
        }
        }
        return true;
}

/* ---- window title, logging, and event loop hooks ---- */

static const char address_to terminal_label = "shell";

static void terminal_title_set(const char address_to title)
{
        struct windows address_to table = terminal.own.table;
        unsigned int at = 0;

        for (; title[at] && title[at] != '\n' && at < WINDOWS_TITLE - 1; at++)
                table->title[terminal.own.row][at] = title[at];
        if (at == WINDOWS_TITLE - 1 && ((unsigned char)title[at] & 0xc0) == 0x80)
                while (at > 0 && ((unsigned char)title[at] & 0xc0) == 0x80)
                        at--;
        table->title[terminal.own.row][at] = 0;
        windows_knock(&terminal.own);
}

static void terminal_title(b32 master, bipolar shell)
{
        int group = 0;

        if (compositor_call(syscall(ioctl), (positive)master, 0x540f, (positive)&group, 0, 0, 0) == 0 && group != terminal.group)
        {
                p8 path[32] = "/proc/", name[WINDOWS_TITLE] = {0};

                terminal.group = group;
                path[6 + positive_into_string(path + 6, (positive)group)] = 0;
                string_append_bounded(path, (string_address) "/comm", sizeof(path));
                if (group != (int)shell)
                        file_slurp_once_at(AT_FDCWD, (string_address)path, name, sizeof(name) - 1);
                terminal_title_set(name[0] ? (const char address_to)name : terminal_label);
        }
        if (title_said != terminal.title_seen)
        {
                terminal.title_seen = title_said;
                memory_copy(terminal.named, window->title, WINDOW_TITLE_MAX);
                if (terminal.named[0])
                        terminal_title_set(terminal.named);
                else
                        terminal.group = -1, terminal_title(master, shell);
        }
}

static void terminal_said(b32 reading, string_address what)
{
        p8 line[96];

        string_copy((string_address)line, (string_address)(reading ? "klog pid " : "terminal pid "));
        compositor_kmsg(line, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), what);
}

static void terminal_refused(b32 reading, string_address what)
{
        p8 line[128];
        positive at;

        terminal_said(reading, what);
        string_copy_bounded(line, (string_address)(reading ? "klog" : "terminal"), sizeof(line) - 1);
        string_append_bounded(line, what, sizeof(line) - 1);
        at = string_length(line), line[at++] = '\n';
        log_error((address_any)line, at);
}

static positive terminal_exit_line(p8 address_to line, positive room)
{
        positive at;

        string_copy(line, (string_address) "\x1b[0m\r\n[");
        at = string_length(line);
        at += language_format((char address_to)line + at, room - at,
                              (term_exit_status & 0x7f) ? WORDS(terminal_press_to_close) : WORDS(terminal_exited_press_to_close),
                              (long)((term_exit_status >> 8) & 0xff));
        line[at++] = ']', line[at] = 0;
        return at;
}

static b32 klog_drain(b32 far, b32 address_to gone, positive budget);
static b32 klog_receive(const struct window_event address_to event, b32 address_to dirty);
static void terminal_leave(b32 show);

static string_address terminal_paths[TERMINAL_FACES], terminal_bold_path, terminal_ready_path;
static unsigned int terminal_face_count, terminal_size_asked, terminal_cells[2];

static void terminal_on_ready(void)
{
        b32 drew = terminal.reading ? klog_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET)
                                    : term_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET);

        if (drew || touched_bottom > touched_top)
                terminal.changed = terminal.dirty = true;
        // A drain that read something holds the pty for a moment, so the next batch is larger. A wake that read nothing holds nothing,
        // and the first read after idle is never held: the echo of a key is drained at once.
        if (drew && !terminal.reading)
                terminal.drain_until = (positive)ui_kit.now + TERMINAL_DRAIN_HOLD_NANOSECONDS;
}

static void terminal_on_pass(void)
{
        if (terminal.phase != TERMINAL_RUNNING)
                return;
        if (!synchronized_output)
                touched_top = ROWS, touched_bottom = 0;
        terminal.modes_fresh = false;
        if (!terminal.reading && (positive)ui_kit.now - terminal.modes_at >= terminal_period())
                terminal_modes_read(terminal.master, (positive)ui_kit.now);
}

static b32 terminal_on_event(const struct window_event address_to event)
{
        int zoom = ui_zoom_key(event);

        if (event->type == WINDOW_EVENT_PINCH)
                return terminal_pinch(event), terminal.dirty = true;
        if (zoom)
                return terminal_zoom(terminal.master, zoom), terminal.dirty = true;
        if (terminal.phase == TERMINAL_STAYING)
        {
                if (terminal.keyed && event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) &&
                    !(event->flags & WINDOW_KEY_REPEAT) && !ui_key_modifier(event->a))
                        terminal.phase = TERMINAL_DONE;
                return true;
        }
        if (terminal.phase == TERMINAL_RUNNING &&
            !(terminal.reading ? klog_receive(event, &terminal.dirty) : terminal_receive(terminal.master, event, &terminal.dirty)))
                terminal.gone = true;
        return true;
}

static b32 terminal_on_focus(b32 focused)
{
        terminal.modifiers = 0;
        if (terminal.phase == TERMINAL_RUNNING && !terminal.reading)
        {
                term_focus(focused);
                if (!term_send(terminal.master))
                        terminal.gone = true;
        }
        return true;
}

static void terminal_on_size(unsigned int width, unsigned int height)
{
        terminal_configure(terminal.master, width, height);
        terminal.dirty = true;
}

static void terminal_on_closing(void)
{
        if (terminal.phase == TERMINAL_STAYING)
                terminal.phase = TERMINAL_DONE;
        else if (terminal.phase != TERMINAL_RUNNING)
                return;
        else if (terminal.reading)
                terminal.gone = true;
        else if (!terminal.gone)
                term_hang_up(terminal.child, &terminal.hung);
        else
                terminal.hung = true;
}

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
                        positive period = terminal_period(), since = now - terminal.last_frame;
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
                terminal.forced = true, terminal.stalled_since = 0;
        return false;
}

static void terminal_end(void)
{
        b32 drew = terminal.reading ? klog_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET)
                                    : term_drain(terminal.master, &terminal.gone, TERMINAL_BUDGET);

        utf8_flush();
        escape_discard();
        if (drew || touched_bottom > touched_top)
                cursor_show(), terminal_frame();
        terminal_leave(true);
}

static void terminal_leave(b32 show)
{
        b32 stays;

        if (terminal.reading)
                system_close(terminal.master);
        stays = !terminal.reading && term_finish(terminal.child, terminal.master, terminal.hung, terminal.oom_kills);
        terminal.master = (b32)-1;
        if (!terminal.reading && term_program_count && !terminal.hung)
        {
                p8 line[256];
                positive at = terminal_exit_line(line, sizeof(line) - 1);

                cursor_hide(), term_bytes(line, at), stays = terminal.keyed = true;
        }
        if (stays && show)
        {
                cursor_show();
                terminal.dirty = !terminal_frame(), terminal.phase = TERMINAL_STAYING;
                return;
        }
        terminal.phase = TERMINAL_DONE;
}

static positive terminal_select_autoscroll(positive now, b32 address_to dirty)
{
        struct ui kit = terminal_sel_kit();
        struct ui_select address_to s = terminal_sel();
        b32 redraw = false;
        positive due;

        (void)now;
        if (alternate)
                return s->edge.at = 0;
        due = ui_select_autoscroll(&kit, s, &redraw);
        if (redraw)
                address_to dirty = true;
        return due;
}

static long terminal_tick(long timeout, long address_to other)
{
        b32 was = terminal.dirty, again;
        long hold = terminal.hold_timeout, kit = timeout;
        positive now, due;

        terminal.hold_timeout = -1;
        if (terminal.phase == TERMINAL_RUNNING && terminal.gone)
                terminal_end();
        if (terminal.phase != TERMINAL_RUNNING)
                return *other = -1, terminal.phase == TERMINAL_STAYING && terminal.dirty ? TERMINAL_STALL_NANOSECONDS : -1;
        *other = terminal.master;
        if (!terminal.reading && (mouse_mode != 0) != terminal.hovering)
                terminal.hovering = mouse_mode != 0,
                __atomic_store_n(&terminal.own.table->hover_off[terminal.own.row], terminal.hovering ? 0u : 1u, __ATOMIC_RELEASE);
        if (!terminal.reading && !terminal.gone && (terminal.changed || terminal.title_owed))
        {
                positive title_now = (positive)ui_kit.now;
                if (title_now - terminal.title_at >= terminal_period() || title_said != terminal.title_seen)
                        terminal_title(terminal.master, terminal.child), terminal.title_at = title_now, terminal.title_owed = false;
                else
                        terminal.title_owed = true;
        }
        if (!terminal.reading && !terminal.gone && !term_send(terminal.master))
                terminal.gone = true;
        if (touched_bottom > touched_top)
                terminal.changed = true;
        timeout = kit;
        if (synchronized_output)
        {
                now = (positive)ui_kit.now;
                terminal.sync_since = terminal.sync_held ? terminal.sync_since : now, terminal.sync_held = true;
                if (terminal.gone || now - terminal.sync_since >= (positive)TERMINAL_SYNC_NANOSECONDS)
                        synchronized_output = false, terminal.changed = true;
                else
                        timeout = ui_deadline_wait(timeout, (b64)now, (b64)(terminal.sync_since + TERMINAL_SYNC_NANOSECONDS));
        }
        else
                terminal.sync_held = false;
        terminal.dirty |= terminal.changed;
        if (terminal.drain_until)
        {
                positive now_ = (positive)ui_kit.now;

                if (now_ < terminal.drain_until)
                        *other = -1, timeout = ui_timeout_min(timeout, (long)(terminal.drain_until - now_));
                else
                        terminal.drain_until = 0;
        }
        timeout = ui_timeout_min(timeout, hold);
        if (ui_body_going(&terminal.bar.body))
                terminal_bar_step(), timeout = ui_timeout_min(timeout, ui_kit_frame_ns());
        if (ui_body_going(&terminal.alt))
        {
                b32 moved = true;
                if (!terminal_finger_scroll(terminal.master, (int)ui_body_given(&terminal.alt, ui_kit_time()), &terminal.dirty, &moved))
                        terminal.gone = true;
                if (!moved)
                        ui_body_hold(&terminal.alt, ui_kit_time());
                if (ui_body_going(&terminal.alt))
                        timeout = ui_timeout_min(timeout, ui_kit_frame_ns());
        }
        if ((due = ui_select_bar_tick(terminal_sel(), &terminal.dirty)))
                timeout = ui_deadline_wait(timeout, (b64)ui_kit.now, (b64)due);
        if ((due = terminal_select_autoscroll((positive)ui_kit.now, &terminal.dirty)))
                timeout = ui_deadline_wait(timeout, (b64)ui_kit.now, (b64)due);
        if (terminal.title_owed)
                timeout = ui_deadline_wait(timeout, (b64)ui_kit.now, (b64)(terminal.title_at + terminal_period()));
        terminal_caret_step((positive)ui_kit.now, &timeout);
        {
                b32 spare = !terminal.dirty && !synchronized_output && terminal.shown >= 0 && terminal.valid[1 - terminal.shown] && windows_ready(&terminal.own);
                b32 stalled = terminal.dirty && !synchronized_output && terminal.shown >= 0 && !windows_ready(&terminal.own);

                if (spare && timeout < 0)
                {
                        now = (positive)ui_kit.now;
                        terminal.idle_since = terminal.idle_since ? terminal.idle_since : now;
                        if (now - terminal.idle_since >= (positive)TERMINAL_IDLE_NANOSECONDS)
                                terminal_discard(), terminal.idle_since = 0;
                        else
                                timeout = ui_deadline_wait(timeout, (b64)now, (b64)(terminal.idle_since + TERMINAL_IDLE_NANOSECONDS));
                }
                else
                {
                        terminal.idle_since = 0;
                        if (stalled)
                                timeout = ui_timeout_min(timeout, TERMINAL_STALL_NANOSECONDS);
                }
        }
        if (!terminal.dirty)
                terminal.stalled_since = terminal.settle_since = 0;
        again = !was && terminal.dirty && hold < 0 && !synchronized_output;
        if (!again)
                terminal.changed = false;
        return again ? 0 : timeout;
}

static b32 terminal_done(void) { return terminal.phase == TERMINAL_DONE; }

static void terminal_on_close(void)
{
        if (terminal.phase == TERMINAL_RUNNING)
        {
                if (!terminal.reading && !terminal.gone)
                        term_hang_up(terminal.child, &terminal.hung);
                terminal_leave(false);
        }
        terminal.phase = TERMINAL_DONE;
}

static int terminal_start(void)
{
        unsigned int size = terminal_size_asked, columns, rows, max_columns, tallest, history;
        struct windows address_to table = terminal.own.table;
        font_metrics least;

        size = size ? size : terminal_size_read();
        size = size ? size : TERMINAL_SIZE;
        size = max(terminal_steps[0], min(size, terminal_steps[TERMINAL_STEPS - 1]));
        if (!terminal_face_count && ui_faces()->face[0])
        {
                terminal_paths[terminal_face_count++] = (string_address)ui_faces()->face;
                if (!terminal_bold_path && ui_faces()->bold[0])
                        terminal_bold_path = (string_address)ui_faces()->bold;
        }
        if (!terminal_fonts(terminal_paths, terminal_face_count, terminal_bold_path, size))
                return terminal_refused(terminal.reading, " has no font to draw with (-f FONT.ttf)"), 1;
        for (unsigned int at = 0; at < 256; at++)
                terminal_colour[at] = window_cell_colour(at);
        term_palette = terminal_palette, term_cell_w = terminal.cell_w, term_cell_h = terminal.cell_h;
        terminal_open(terminal.reading ? TERMINAL_LOG_WIDE : TERMINAL_WIDE, terminal.reading ? TERMINAL_LOG_HIGH : TERMINAL_HIGH,
                      terminal.reading ? TERMINAL_LOG_LEFT : TERMINAL_LEFT, terminal.reading ? TERMINAL_LOG_TOP : TERMINAL_TOP,
                      &columns, &rows, terminal_cells);
        least = font_metrics_at(terminal.plain[0], terminal_steps[0] << 6);
        max_columns = max(table->output_width, (unsigned int)TERMINAL_CEILING) / (unsigned int)least.cell_width;
        tallest = max(table->output_height, (unsigned int)(TERMINAL_CEILING * 9 / 16)) / (unsigned int)least.cell_height;
        history = terminal.reading ? TERMINAL_LOG_HISTORY : TERMINAL_HISTORY;
        window = window_grid_open(columns, rows, max_columns, max(tallest * 2 + 1, history));
        if (window)
                grid_take();
        if (!window || !terminal_frames())
                return terminal_refused(terminal.reading, " has no memory for its cells"), 1;
        full_reset();
        if (terminal.reading)
                cursor_visible = false,
                terminal.master = (b32)system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW),
                terminal.child = (bipolar)terminal.master;
        else
                terminal.child = term_session(&terminal.master, &terminal.oom_kills);
        if (terminal.child < 0)
                return terminal_refused(terminal.reading, " cannot open its far end"), 1;
        cursor_show();
        terminal_frame();
        terminal.last_frame = (positive)ui_kit.now;
        terminal_text_said(terminal.reading);
        if (terminal_ready_path)
                system_close(system_open_at_mode(AT_FDCWD, terminal_ready_path, FILE_WRITE | O_CLOEXEC | O_NOFOLLOW, 0644));
        terminal.dirty = true, terminal.phase = TERMINAL_RUNNING;
        return -1;
}

static struct ui_app terminal_app = {
    .own = &terminal.own,     .ui = &terminal.ui,       .glass = 0,               .dirty = &terminal.dirty,
    .focused = &terminal.focused, .ground = &terminal.ground, .own_faces = true,  .open = terminal_start,
    .frame = terminal_pass_frame, .event = terminal_on_event, .focus = terminal_on_focus, .size = terminal_on_size,
    .tick = terminal_tick,    .ready = terminal_on_ready, .close = terminal_on_close, .pass = terminal_on_pass,
    .closing = terminal_on_closing, .done = terminal_done, .popup = &terminal_menu, .chose = terminal_chose,
};

static b32 terminal_run(b32 reading)
{
        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2] || !value)
                        continue;
                if (option[1] == 'x' && !reading)
                {
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
                        terminal_cells[0] = (unsigned int)compositor_number(value, 0);
                        for (positive at_x = 0; value[at_x]; at_x++)
                                if (value[at_x] == 'x')
                                        terminal_cells[1] = (unsigned int)compositor_number(value + at_x + 1, 0);
                }
        }
        if (term_program_count)
        {
                string_address slash = string_last_of(term_program_words[0], '/');
                terminal_label = term_program_count == 3 && string_equals(term_program_words[1], (string_address) "-c")
                                     ? term_program_words[2] : slash ? slash + 1 : term_program_words[0];
        }
        terminal.reading = reading;
        terminal_app.title = reading ? (string_address) "kernel log" : (string_address)terminal_label;
        terminal_app.own_title = !reading;
        terminal_app.name = reading ? (string_address) "klog" : (string_address) "terminal";
        terminal_app.missing = reading ? (string_address) "klog: no window: no compositor, or no row free in its table\n"
                                       : (string_address) "terminal: no window: no compositor, or no row free in its table\n";
        return ui_app_run(&terminal_app);
}

static b32 terminal_main() { return terminal_run(false); }

#endif // MOONWATER_CANVAS_TERMINAL
