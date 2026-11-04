/*
        The compositor in userspace, and the smallest programs that draw on it.

        `compositor` takes the display from Canvas the way any program can:
        it opens the card, becomes its master, puts a buffer of its own on
        the screen at the mode Canvas left, and reads the keyboard and the
        pointer through evdev, the devices that are there and the ones that
        are plugged in later. While it holds the card Canvas draws nothing
        and keeps the console keyboard muted, and its own input handler
        still hears Control-Alt-Backspace, which ends this program's session
        if it ever stops answering. When it exits, the card's last file
        closes and Canvas's restore hook draws its ground again at once.

        What it draws is the desktop ink and each window's own pixels inside
        a black line of one pixel, round at the corners, and over them the
        panel: a row of tabs in the middle of the bottom edge -- one for each
        window, as wide as its title, and the tray with the time -- whose rest
        is off the output, with nothing between them.

        A window is a row of one table (struct windows, window.c) that the
        compositor makes and every program maps: the program writes its
        picture, its size and its title there and knocks on its row's door,
        and the compositor writes where the window is, the size it asks for,
        which window has the keys and the events, and knocks back. A window
        stacks, the press of a button raises it and gives it the keys, a press
        on its top, whatever is drawn there, or with Alt anywhere on it drags
        it, a press just outside its edges resizes it, its close button,
        Alt-F4 or a middle press on its tab asks it to go, and it is kept on
        the output, which a window filling it fills whole, the tabs lying over
        it. When its program ends, its door closes and it is gone.

        Damage is the union of what the programs say changed, composed
        straight onto the scanout, each pixel once but in the round corners.

        What the kernel desktop did for a hand and a keyboard it does here:
        keys are the characters of the layout `moonwater keyboard` set (every
        layout the kernel had, AltGr as the third level, dead accents and
        Caps Lock besides), Alt-Tab, Alt-F4 and Alt-F9 are the compositor's
        and the machine script's key_ functions are its chords (key_ctrl_shift_t, a
        terminal), the pointer is gained by the speed of the hand and
        wears a shape for the edge it is over, a window is arranged to fill
        the output or half of it by a double click on its top or with Alt
        held, or by a drag to an edge, Alt and the right button resize it
        from the corner nearest the hand, and a machine that slept has its
        screen drawn again.

        The desktop's ink and the pointer's size and colours are what
        `moonwater canvas background` and `moonwater canvas cursor` kept in
        /root, read when it starts and again whenever one is written.

        `surface` is a test client: a window of one colour, a line of text
        drawn with font.c when it is given a TrueType file, and a report on
        standard output of everything it is told.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_COMPOSITOR
#define MOONWATER_CANVAS_COMPOSITOR

// The Wayland shim is on unless the build's header says SHELL_NO_WAYLAND (CONFIG_MOONWATER_WAYLAND=n).
#if !defined(SHELL_NO_WAYLAND) && !defined(CONFIG_MOONWATER_WAYLAND)
#define CONFIG_MOONWATER_WAYLAND 1
#endif

/* ---- backend: the look on the CPU ---- */

// The desktop's ink: neutral grey, no hue, and not a kit role. The line round a window, its line with the keys, the
// chrome's shadow and the band's inks are the kit's roles (compositor_chrome, theme.h), which is what the windows
// draw with, so the chrome turns with the theme; the tabs drawn without a font are in taskbar.h.
#define COMPOSITOR_INK_DESKTOP 0x1e1e1eu
/*
        An edge or a corner of a window the hand holds to resize it, or all of its line while it is moved: the text's
        ink, well over the line with the keys, so a person sees which handle they have; and an edge the pointer is over,
        that a press would take, between the two. A finger's mark (`moonwater canvas touch on`) is in the held ink too.
*/
#define COMPOSITOR_INK_LINE_HELD 0xe4e4e4u
#define COMPOSITOR_INK_LINE_HOVER 0x9a9a9au
// The lit bits of a frame: its edges, as CP_EDGE_* number them (the pointer's, below), and every edge and corner, a
// window being moved.
#define COMPOSITOR_LIT_LEFT 1u
#define COMPOSITOR_LIT_RIGHT 2u
#define COMPOSITOR_LIT_TOP 4u
#define COMPOSITOR_LIT_BOTTOM 8u
#define COMPOSITOR_LIT_ALL 16u
// How far along each of its two edges a held corner is lit, in logical pixels: the corner's arc alone is too short to see.
#define COMPOSITOR_LIT_REACH 32

// The desktop's ink as `moonwater canvas background` set it, and the default when none is: what is drawn
// where no window is, and behind a window's round corners. The tabs' glass is made from what is on the screen.
static unsigned int compositor_ground = COMPOSITOR_INK_DESKTOP;

// The radius of a window's corners and the width of the line round it, in logical pixels; and the largest
// radius in device pixels, which is what the corners' scratch is made for. (The panel's height is in taskbar.h.)
#define COMPOSITOR_LINE 1
/*
        The snap's look, in one place. A snap preview is the selection's colour at COMPOSITOR_SNAP_PERCENT (more on a light
        wallpaper, up to COMPOSITOR_SNAP_PERCENT_MOST), its colour deepened till its edge reads COMPOSITOR_SNAP_READ
        (compositor_snap_ink; ui_contrast's hundredths, so 300 is 3 to 1). A thumbnail and a grid tile have the window's
        line and corners, and their fades run COMPOSITOR_SNAP_FADE_NS, the same time both ways. A window arranged, a tile or
        filling its screen, is square, as the preview of it is: a tile has the window's line and a preview of one a line of
        the tint at COMPOSITOR_SNAP_LINE percent of the tint's own strength, a window filling its screen has none.
*/
#define COMPOSITOR_SNAP_PERCENT 40
#define COMPOSITOR_SNAP_PERCENT_MOST 60
#define COMPOSITOR_SNAP_READ 300
#define COMPOSITOR_SNAP_LINE 200
#define COMPOSITOR_SNAP_FADE_NS (120 * UI_MS)
// The grid's tiles: the gap between them, which a thumbnail keeps from its cell's edges too; a tile's tint, white at a quarter;
// and the tint of a region a window fills, black at a half (it is shown taken, not hidden).
#define COMPOSITOR_GRID_GAP 4
#define COMPOSITOR_GRID_TINT 0x40ffffffu
#define COMPOSITOR_GRID_TAKEN 0x80000000u
// The top of a window that is a titlebar with nothing drawn -- a press there takes the window whatever is
// under it -- and its close button, a disc that far in from the window's top right, in logical pixels.
#define COMPOSITOR_GRAB 24
// The minimise button beside the close one: the same size, the same gap between them, and its dash this far in from each side.
#define COMPOSITOR_MINIMIZE_IN 3
#define COMPOSITOR_RADIUS_MOST 64

#include "ui/theme.h"
#include "ui/motion.h"
#include "ui/taskbar.h"
#include "ui/touch.h"
#include "../audio/state.c" // the sound server's page: the tray's volume cell and the volume keys
#include "../audio/control.c" // and its socket, which the keys and the wheel tell the volume to at once
#if !defined(SHELL_NO_GPU) && !defined(MOONWATER_BUILD_TOOL)
#include "gpu.c" // GPU rendering, on unless the build's header says SHELL_NO_GPU (CONFIG_MOONWATER_GPU=n); the build tool, which makes its kernels, is without it
#endif

static struct taskbar_view compositor_taskbar_view(void);
static unsigned int compositor_line_lit(void); // the lit line of the window with the keys, the theme's (compositor_theme_read)
static unsigned int compositor_ink_shadow(void); // the chrome's shadow ink: a line without the keys, the close button, a tab unlit
static unsigned int compositor_ink_selection(void); // the chrome's selection, 0xaarrggbb: the snap preview's tint
static unsigned int compositor_snap_ink(void); // the snap's tint, 0xaarrggbb: the selection at its percent, read on the wallpaper
static void compositor_preview(int row); // the snap preview of the drag of row (none clears it), from the pointer
static unsigned int compositor_tab_target(b32 lit); // what a tab's glass is tinted toward, the theme's

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch; // pixels a row
        int width, height;
        int x1, y1, x2, y2;  // the damage: nothing outside it is written
        int ox, oy;          // the point pixels[0] is, on the output (a corner's scratch); 0 for the output
        unsigned int *scratch; // where the scene draws a round corner's square; none is the one of its own
};

// A scale in percent as the whole device pixels of one logical pixel, never less than one.
static int compositor_px1(int percent)
{
        int px = (percent + 50) / 100;

        return px < 1 ? 1 : px;
}

// The line round a window at a scale in percent, in whole device pixels: COMPOSITOR_LINE logical pixels cut to whole ones and
// never less than one. At 50 and 75 percent the cut alone is none, and a window with no line was drawn with no corners
// either (compositor_frame_of), so at those scales a window was square with no outline.
static int compositor_line_px(int percent)
{
        int px = COMPOSITOR_LINE * percent / 100;

        return px < 1 ? 1 : px;
}

/*
        The solid runs of a window's picture, row by row: a stretch of 32 pixels or more that is one colour is a fill when the
        picture is composed, which writes what it writes and reads nothing, where a copy reads it all. memory_runs_u32 reads a
        picture for them, in chunks of 16 pixels (its comment says what a run is). A row keeps up to COMPOSITOR_RUN_ROW of them in
        the order they lie, x0 to x1 (not including it) of the row, and the colour; a stretch past the room is copied like any
        other. A row is 128 bytes, two lines.
*/
#define COMPOSITOR_RUN_ROW 15

// The shortest stretch of a run that is filled when the run is cut to a rectangle; less is copied with what is round it. A call
// for a fill and one for the copy after it cost as much as the 64 pixels they would save writing.
#define COMPOSITOR_RUNS_FILL 64
// The least rectangle composed from runs: its width in pixels and its area; a smaller one is the copy.
#define COMPOSITOR_RUNS_LEAST_WIDTH 64
#define COMPOSITOR_RUNS_LEAST_AREA 16384

struct compositor_run
{
        unsigned short x0, x1;
        unsigned int colour;
};

struct compositor_run_row
{
        unsigned int count, spare;
        struct compositor_run run[COMPOSITOR_RUN_ROW];
};

_Static_assert(sizeof(struct compositor_run_row) == 128, "a run row is two cache lines");

/*
        One thing to draw. A window is its buffer and nothing around it; the
        panel is a band with a mark a window, one byte each:
        COMPOSITOR_TAB_LIT and COMPOSITOR_TAB_AWAY, and its pixels are the
        tabs drawn with their titles, or with none it is the shapes alone.
*/
struct compositor_frame
{
        int x, y;            // the contents' top left
        int width, height;   // the contents
        int scale;
        const unsigned int *pixels;
        unsigned long pitch;
        int builtin;         // the panel: no pixels, the marks are its buttons
        const unsigned char *marks;
        unsigned int mark_count;
        int radius;          // the corners of the window and its line, in device pixels; 0 for square
        int line;            // the line round the contents, outside them, in device pixels; 0 for none
        unsigned int edge;   // the line's colour
        int square;          // corners kept square though there is a radius: 1 top left, 2 top right, 4, 8 below
        int slot;            // the compositor's panel: the tab this frame is (-2 tray, TASKBAR_LAUNCHER the launcher button, 0 up windows)
        unsigned int launcher_lit; // the panel: the launcher is up, so its button is drawn as the tab with the keys is
        unsigned int position; // the panel: where its band sits (TASKBAR_BOTTOM, TOP, LEFT or RIGHT)
        const unsigned char *lengths; // the panel: each window's title in characters, which its tab fits
        int close;           // a window with a close button over its top right
        unsigned int close_mean; // the mean colour under the close button, made once a present (compositor_close_mean)
        // What of its line is lit, and in what ink (COMPOSITOR_INK_LINE_HELD or _HOVER): one edge (CP_EDGE_*) lit whole,
        // two that meet a corner lit with COMPOSITOR_LIT_REACH of each, COMPOSITOR_LIT_ALL all of it; 0 none.
        unsigned int lit, lit_ink;
        // A window landing is drawn at less than its picture's size, source_w by source_h: its contents are that
        // picture scaled (compositor_scaled). 0 for a frame drawn at its picture's size.
        int source_w, source_h;
        // The snap preview of the window being dragged: its target's rectangle on the output (width 0 for none) and
        // the kit's selection, 0xaarrggbb, whose alpha is the tint's (compositor_scene draws it); shaped as the window will be
        // there, its corners' radius and its line in device pixels (0 for square, or none) and the line's ink, 0xaarrggbb.
        // The preview is over every window and the tabs, so compositor_present takes it off the window's frame into a frame
        // of its own after them (compositor_preview_lift); only the overlays (the grid, the assist) are over it.
        int preview[4];
        unsigned int preview_ink;
        int preview_radius, preview_line;
        unsigned int preview_edge;
        // The shared edge's indicator over this window (compositor_mark_set): the rectangle and the kit's selection.
        int mark[4];
        unsigned int mark_ink;
        // A window's picture composed from its runs (compositor_runs_note): each row of it, the stretches that are one colour
        // filled and the rest copied; none for a frame whose runs are not known, which is copied whole.
        const struct compositor_run_row *runs;
};

/*
        The close button: a disc at the window's top right, inset from both
        edges, and none on a window too small to hold it beside its corner.
        For drawing and for the press alike.
*/
#if MOONWATER_UI_ANY
static inline int language_rtl(void); // the language taken reads from the right (ui/language.c, which is built after this file)
#else
static inline int language_rtl(void)
{
        return 0; // a build with no window of the kit has no language to read from
}
#endif

static int compositor_close_box(const struct compositor_frame *frame, int *x, int *y, int *side)
{
        int box = UI_CLOSE * frame->scale / 100, in = UI_CLOSE_IN * frame->scale / 100;

        if (!frame->close || frame->builtin || frame->width < box + 2 * in + 4 * box || frame->height < box + 2 * in)
                return 0;
        *side = box;
        // At the window's start corner: the top right, or the top left where the language reads from the right.
        *x = language_rtl() ? frame->x + in : frame->x + frame->width - box - in;
        *y = frame->y + in;
        return 1;
}

static void compositor_fill(const struct compositor_target *t, int x, int y, int width,
                            int height, unsigned int colour)
{
        int x2 = x + width, y2 = y + height;

        x = x > t->x1 ? x : t->x1;
        y = y > t->y1 ? y : t->y1;
        x2 = x2 < t->x2 ? x2 : t->x2;
        y2 = y2 < t->y2 ? y2 : t->y2;

        if (x2 <= x || y2 <= y)
                return;

        memory_fill_u32_rect(t->pixels + (unsigned long)(y - t->oy) * t->pitch + (unsigned long)(x - t->ox),
                             t->pitch, (unsigned long)(x2 - x), (unsigned long)(y2 - y),
                             colour);
}

static void compositor_blit(const struct compositor_target *t, int x, int y, int width,
                            int height, const unsigned int *source, unsigned long pitch)
{
        int x1 = x > t->x1 ? x : t->x1;
        int y1 = y > t->y1 ? y : t->y1;
        int x2 = x + width < t->x2 ? x + width : t->x2;
        int y2 = y + height < t->y2 ? y + height : t->y2;

        if (x2 <= x1 || y2 <= y1)
                return;
        memory_copy_rect_u32(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                             source + (unsigned long)(y1 - y) * pitch + (unsigned long)(x1 - x), pitch,
                             (unsigned long)(x2 - x1), (unsigned long)(y2 - y1));
}

// Whether two run rows hold the same runs.
static int compositor_runs_same(const struct compositor_run_row *one, const struct compositor_run_row *two)
{
        return one->count == two->count &&
               !memory_compare(one->run, two->run,
                               (one->count < COMPOSITOR_RUN_ROW ? one->count : COMPOSITOR_RUN_ROW) * sizeof(one->run[0]));
}

/*
        A rectangle composed from its rows' runs: each run filled, what lies between them copied, a run cut to the rectangle
        (skip is the picture's column its left edge is) and one cut to less than COMPOSITOR_RUNS_FILL pixels left to the copy.
        Rows that hold the same runs, as the rows of a band of one colour or of a line's blank end do, are one block: a call to
        fill each run of it and one to copy each gap, for as many rows as the block has.
*/
static void compositor_runs_copy(unsigned int *into, unsigned long into_pitch, const unsigned int *from, unsigned long from_pitch,
                                 unsigned long width, unsigned long height, const struct compositor_run_row *rows, unsigned long skip)
{
        while (height)
        {
                unsigned long block = 1, at = 0, count = rows->count < COMPOSITOR_RUN_ROW ? rows->count : COMPOSITOR_RUN_ROW;

                while (block < height && compositor_runs_same(&rows[block], rows))
                        block++;
                for (unsigned long k = 0; k < count; k++)
                {
                        long a = (long)rows->run[k].x0 - (long)skip, b = (long)rows->run[k].x1 - (long)skip;

                        a = a < (long)at ? (long)at : a;
                        b = b > (long)width ? (long)width : b;
                        if (b - a < COMPOSITOR_RUNS_FILL)
                                continue;
                        if ((unsigned long)a > at)
                                memory_copy_rect_u32(into + at, into_pitch, from + at, from_pitch, (unsigned long)a - at, block);
                        memory_fill_u32_rect(into + a, into_pitch, (unsigned long)(b - a), block, rows->run[k].colour);
                        at = (unsigned long)b;
                }
                if (at < width)
                        memory_copy_rect_u32(into + at, into_pitch, from + at, from_pitch, width - at, block);
                into += block * into_pitch;
                from += block * from_pitch;
                rows += block;
                height -= block;
        }
}

// compositor_blit for a window whose runs are known: rows is the run row of the window's picture row 0.
static void compositor_blit_runs(const struct compositor_target *t, int x, int y, int width, int height,
                                 const unsigned int *source, unsigned long pitch, const struct compositor_run_row *rows)
{
        int x1 = x > t->x1 ? x : t->x1;
        int y1 = y > t->y1 ? y : t->y1;
        int x2 = x + width < t->x2 ? x + width : t->x2;
        int y2 = y + height < t->y2 ? y + height : t->y2;

        if (x2 <= x1 || y2 <= y1)
                return;
        // A caret's damage, a hover, a letter: what is copied in a few lines is not worth a row's runs.
        if (x2 - x1 < COMPOSITOR_RUNS_LEAST_WIDTH || (long)(x2 - x1) * (long)(y2 - y1) < COMPOSITOR_RUNS_LEAST_AREA)
        {
                compositor_blit(t, x, y, width, height, source, pitch);
                return;
        }
        compositor_runs_copy(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                             source + (unsigned long)(y1 - y) * pitch + (unsigned long)(x1 - x), pitch,
                             (unsigned long)(x2 - x1), (unsigned long)(y2 - y1), rows + (y1 - y), (unsigned long)(x1 - x));
}

/*
        A picture drawn at a size that is not its own, which only a window landing is (compositor_land): nearest
        neighbour, the source pixel under the centre of each target pixel. The walk is memory_resample_u32_rect in
        lib.c, the C it was measured against being resample_reference in test/checks.c; the source position steps in
        24-bit fixed point, and the step is rounded down, so the position never reaches the picture's last column or row.
*/
static void compositor_blit_scaled(const struct compositor_target *t, int x, int y, int width, int height,
                                   const unsigned int *source, unsigned long pitch, int source_w, int source_h)
{
        int x1 = x > t->x1 ? x : t->x1;
        int y1 = y > t->y1 ? y : t->y1;
        int x2 = x + width < t->x2 ? x + width : t->x2;
        int y2 = y + height < t->y2 ? y + height : t->y2;

        if (x2 <= x1 || y2 <= y1 || width <= 0 || height <= 0)
                return;
        memory_resample_u32_rect(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                                 (unsigned long)(x2 - x1), (unsigned long)(y2 - y1), source, pitch,
                                 (unsigned long)source_w, (unsigned long)source_h, (unsigned long)width,
                                 (unsigned long)height, (unsigned long)(x1 - x), (unsigned long)(y1 - y));
}

// A frame whose contents are its picture at another size (a window landing), so they are scaled from it.
static int compositor_scaled(const struct compositor_frame *frame)
{
        return frame->source_w && (frame->width != frame->source_w || frame->height != frame->source_h);
}

// The picture's pixel at dx, dy from a frame's contents' top left: the frame's own, or, a frame scaled, the one
// compositor_blit_scaled draws there (the source cell whose centre is the point's).
static unsigned int compositor_picture_at(const struct compositor_frame *frame, int dx, int dy)
{
        unsigned long sx = (unsigned long)dx, sy = (unsigned long)dy;

        if (compositor_scaled(frame))
        {
                sx = (2 * sx + 1) * (unsigned long)frame->source_w / (2 * (unsigned long)frame->width);
                sy = (2 * sy + 1) * (unsigned long)frame->source_h / (2 * (unsigned long)frame->height);
        }
        return frame->pixels[sy * frame->pitch + sx];
}

/*
        The minimise button: a disc the close button's size beside it, to its left, with the same gap between them. None
        where the close button is none, or where there is no room for both. A window with no close button (the
        launcher's, WINDOW_NO_CLOSE) has none either: it is put away by dismissing it. The close button's place does not
        change for any window, so a window that cannot minimise has its title bar as it was.
*/
static int compositor_minimize_box(const struct compositor_frame *frame, int *x, int *y, int *side)
{
        int bx, by, box, in = UI_CLOSE_IN * frame->scale / 100;

        if (!compositor_close_box(frame, &bx, &by, &box) || frame->width < 2 * box + 3 * in + 4 * box)
                return 0;
        *side = box;
        *x = language_rtl() ? bx + box + in : bx - in - box;
        *y = by;
        return 1;
}

/*
        The minimise button over a window's pixels: a dash across its disc, COMPOSITOR_MINIMIZE_IN in from each side and
        two device pixels thick, in the ink the close button's X takes for the same window (window_contrast of the mean
        under it), so the two stand out alike. Flat: no halo and no cover. Drawn after the window's pixels, through the
        target's clip, as the close button is.
*/
static void compositor_minimize_draw(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int mx, my, side, in = COMPOSITOR_MINIMIZE_IN * frame->scale / 100;

        if (!compositor_minimize_box(frame, &mx, &my, &side))
                return;
        compositor_fill(t, mx + in, my + side / 2 - compositor_px1(frame->scale), side - 2 * in, 2 * compositor_px1(frame->scale),
                        window_contrast(frame->close_mean));
}

// A title's length in characters, the bytes that begin one, to its end or the listing's.
static unsigned char compositor_title_length(const char *title, unsigned int most)
{
        unsigned int count = 0;

        for (unsigned int at = 0; at < most && title[at]; at++)
                count += ((unsigned char)title[at] & 0xc0) != 0x80;
        return (unsigned char)(count > 255 ? 255 : count);
}

// A panel frame's mark: its window's lit and put-away bits, the button's lit one, and none for the tray.
static unsigned int compositor_panel_mark(const struct compositor_frame *frame)
{
        if (frame->slot == TASKBAR_LAUNCHER)
                return frame->launcher_lit ? COMPOSITOR_TAB_LIT : 0;
        return frame->slot >= 0 && frame->slot < (int)frame->mark_count ? frame->marks[frame->slot] : 0;
}

/*
        One tab, its rectangle x, y, width, height: drawn with the taskbar's ink.
*/
static void compositor_panel_tab(const struct compositor_target *t, int slot, unsigned int mark, int x, int y,
                                 int width, int height)
{
        compositor_fill(t, x, y, width, height, taskbar_tab_ink(slot, mark));
}

/*
        A window's corners are quarters of a circle: the line's outside at
        the radius, the contents' inside it by the line's width. A mask is
        the top left quarter, each pixel's share of its sixteen sample
        points inside the circle, made once for a radius and kept, the
        outside's and the inside's alike; the other three corners read it
        mirrored.
*/
// Every radius's mask in one block, radius r at the sum of the squares below it: the masks of every radius up to
// COMPOSITOR_MASK_MOST, 299536 bytes, of which only the pages of the radii drawn are ever written (a window's radius
// is eight to its scale, four times at most, and a finger's mark is COMPOSITOR_SPOT to its scale). The mirrored masks of
// every radius are a second block of the same size, COMPOSITOR_MASKS_BYTES after the first (compositor_corner_mirror).
#define COMPOSITOR_MASK_MOST 96
#define COMPOSITOR_MASKS_BYTES (COMPOSITOR_MASK_MOST * (COMPOSITOR_MASK_MOST + 1) * (2 * COMPOSITOR_MASK_MOST + 1) / 6)
// Mapped when the first is wanted, not kept in the image: the address space of every program that links this is
// the image's, and a shell under a 12 MiB cap has none to spare.
static void *compositor_map_zeroed(unsigned long bytes);
static unsigned char *compositor_corner_masks;
static unsigned char compositor_corner_made[COMPOSITOR_MASK_MOST + 1];
static unsigned char compositor_corner_none[COMPOSITOR_MASK_MOST * COMPOSITOR_MASK_MOST];

static const unsigned char *compositor_corner_mask(int radius)
{
        unsigned char *mask;

        if (!compositor_corner_masks)
        {
                compositor_corner_masks = compositor_map_zeroed(2 * COMPOSITOR_MASKS_BYTES);
                if (!compositor_corner_masks)
                        return compositor_corner_none;
        }
        mask = compositor_corner_masks + (unsigned long)(radius - 1) * radius * (2 * radius - 1) / 6;

        if (!compositor_corner_made[radius])
        {
                for (int y = 0; y < radius; y++)
                        for (int x = 0; x < radius; x++)
                        {
                                int inside = 0;

                                // In eighths of a pixel, sample centres odd, the circle's centre at 8 radius.
                                for (int sy = 0; sy < 4; sy++)
                                        for (int sx = 0; sx < 4; sx++)
                                        {
                                                int dx = 8 * radius - (8 * x + 2 * sx + 1);
                                                int dy = 8 * radius - (8 * y + 2 * sy + 1);

                                                inside += dx * dx + dy * dy <= 64 * radius * radius;
                                        }
                                mask[y * radius + x] = (unsigned char)((inside * 255 + 8) / 16);
                        }
                // The same quarter read backwards along each row, where the corner is on the right: made with it,
                // so the parallel bands only ever read the masks.
                for (int y = 0; y < radius; y++)
                        for (int x = 0; x < radius; x++)
                                mask[COMPOSITOR_MASKS_BYTES + y * radius + x] = mask[y * radius + radius - 1 - x];
                compositor_corner_made[radius] = 1;
        }
        return mask;
}

// The mirrored mask of a radius: mirror[y * radius + x] is the mask's own [y * radius + radius - 1 - x], so a corner on
// the right reads its row forward as a corner on the left does.
static const unsigned char *compositor_corner_mirror(int radius)
{
        const unsigned char *mask = compositor_corner_mask(radius);

        return mask == compositor_corner_none ? compositor_corner_none : mask + COMPOSITOR_MASKS_BYTES;
}

// The rectangle a frame covers: its contents and the line round them.
static void compositor_outline(const struct compositor_frame *frame, int *x, int *y, int *width, int *height)
{
        int line = frame->builtin || frame->line < 0 ? 0 : frame->line;

        *x = frame->x - line;
        *y = frame->y - line;
        *width = frame->width + 2 * line;
        *height = frame->height + 2 * line;
}

/*
        A frame's radius as it is drawn, the line's outside: at least the
        line's width so the line has corners of its own,
        at most half the smaller side and the scratch's.
*/
static int compositor_radius(const struct compositor_frame *frame)
{
        int x, y, width, height, line = frame->builtin || frame->line < 0 ? 0 : frame->line;
        int radius = frame->radius <= 0 ? 0 : frame->radius < line ? line : frame->radius;

        compositor_outline(frame, &x, &y, &width, &height);
        radius = radius > width / 2 ? width / 2 : radius;
        radius = radius > height / 2 ? height / 2 : radius;
        return radius < 0 ? 0 : radius > COMPOSITOR_RADIUS_MOST ? COMPOSITOR_RADIUS_MOST : radius;
}

/*
        Each channel of ground and ink mixed by cover (0 the ground, 255 the ink): (ground * (255 - cover) + ink * cover + 127)
        / 255, rounded down, the top byte of either left out. The ends are the colours themselves: at a cover of none the sum
        is 255 * ground + 127, whose quotient is the ground, and at all of it the ink's. Between, the three
        channels are worked in one word, sixteen bits a lane: a lane's sum is at most 255 * 255 + 127, so none carries
        into the next, and the division is (x + 1 + (x >> 8)) >> 8 in every lane, exact for every sum up to 65534.
*/
static unsigned int compositor_mix(unsigned int ground, unsigned int ink, unsigned int cover)
{
        const unsigned long long low = 0xff00ff00ffull, one = 0x100010001ull;
        unsigned long long g, i, x;

        if (cover == 0)
                return ground & 0xffffffu;
        if (cover >= 255)
                return ink & 0xffffffu;
        g = (ground & 0xffu) | (unsigned long long)(ground & 0xff00u) << 8 | (unsigned long long)(ground & 0xff0000u) << 16;
        i = (ink & 0xffu) | (unsigned long long)(ink & 0xff00u) << 8 | (unsigned long long)(ink & 0xff0000u) << 16;
        x = g * (255 - cover) + i * cover + 127 * one;
        x += one + ((x >> 8) & low);
        x = (x >> 8) & low;
        return (unsigned int)((x & 0xff) | (x >> 8 & 0xff00) | (x >> 16 & 0xff0000));
}

// A target drawn on through a clip of its own: the rectangle, and what of it the target allows.
static struct compositor_target compositor_clipped(const struct compositor_target *t, int x, int y, int width,
                                                   int height)
{
        struct compositor_target part = *t;

        part.x1 = x > t->x1 ? x : t->x1;
        part.y1 = y > t->y1 ? y : t->y1;
        part.x2 = x + width < t->x2 ? x + width : t->x2;
        part.y2 = y + height < t->y2 ? y + height : t->y2;
        if (part.x2 < part.x1)
                part.x2 = part.x1;
        if (part.y2 < part.y1)
                part.y2 = part.y1;
        return part;
}

/*
        How much of each pixel of a close button the X's halo and its stroke
        cover, in sixteenths (four by four samples): the geometry of the
        button alone, its side and its scale, so made once for them
        (compositor_close_ready, before the scene's bands) and read for every
        window's button at every present, where the sixteen samples a pixel
        were most of a present's cost after the copy.
*/
#define COMPOSITOR_CLOSE_MOST (UI_CLOSE * 8)

static struct
{
        int side, half;
        unsigned char halo[COMPOSITOR_CLOSE_MOST * COMPOSITOR_CLOSE_MOST];
        unsigned char cross[COMPOSITOR_CLOSE_MOST * COMPOSITOR_CLOSE_MOST];
} compositor_close_cover;

static void compositor_close_sample(int side, int half, int x, int y, unsigned int *halo, unsigned int *cross)
{
        int arm = side * 2 / 7;

        *halo = *cross = 0;
        // In eighths of a pixel from the button's middle, sample centres odd.
        for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                        int dx = 8 * x + 2 * sx + 1 - 4 * side, dy = 8 * y + 2 * sy + 1 - 4 * side;
                        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                        int d1 = dx - dy, d2 = dx + dy;

                        d1 = d1 < 0 ? -d1 : d1;
                        d2 = d2 < 0 ? -d2 : d2;
                        *cross += ax <= 8 * arm && ay <= 8 * arm && (d1 <= 11 * half || d2 <= 11 * half);
                        *halo += ax <= 8 * arm + 12 * half && ay <= 8 * arm + 12 * half && (d1 <= 28 * half || d2 <= 28 * half);
                }
}

// The cover of a frame's button, made where it is not already for its side and scale.
static void compositor_close_ready(const struct compositor_frame *frame)
{
        int bx, by, side;

        if (!compositor_close_box(frame, &bx, &by, &side) || side > COMPOSITOR_CLOSE_MOST ||
            (compositor_close_cover.side == side && compositor_close_cover.half == compositor_px1(frame->scale)))
                return;
        for (int y = 0; y < side; y++)
                for (int x = 0; x < side; x++)
                {
                        unsigned int halo, cross;

                        compositor_close_sample(side, compositor_px1(frame->scale), x, y, &halo, &cross);
                        compositor_close_cover.halo[y * side + x] = (unsigned char)halo;
                        compositor_close_cover.cross[y * side + x] = (unsigned char)cross;
                }
        compositor_close_cover.side = side;
        compositor_close_cover.half = compositor_px1(frame->scale);
}

/*
        The close button over a window's pixels: an X and nothing round it.
        Its strokes are the ink that stands out on what is under the button
        (window_contrast of the mean of the window's pixels there, which
        compositor_close_mean made once for the present), and a halo a pixel
        and a half wider round them takes the window's own pixels further to
        the side of lightness they are on, as the tabs' glass does, so the X
        reads on a busy picture as on a plain ground; each pixel is the share
        of its sixteen sample points inside the strokes and inside the halo.
        What is under it is read from the window's pixels, which are known,
        and not from the target. Drawn last, after the round corners, through
        the target's clip, so the scene's pieces and its corners' scratch both
        have it.
*/
static void compositor_close_draw(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int bx, by, side;

        if (!compositor_close_box(frame, &bx, &by, &side))
                return;

        int x1 = bx > t->x1 ? bx : t->x1, y1 = by > t->y1 ? by : t->y1;
        int x2 = bx + side < t->x2 ? bx + side : t->x2, y2 = by + side < t->y2 ? by + side : t->y2;

        if (x2 <= x1 || y2 <= y1)
                return;

        unsigned int mean = frame->close_mean, ink = window_contrast(mean), tone = window_light(mean) ? 0xffffffu : compositor_ink_shadow();
        int half = compositor_px1(frame->scale); // half the X's stroke
        int made = compositor_close_cover.side == side && compositor_close_cover.half == half;
        const unsigned int *picture = compositor_scaled(frame) ? 0 : frame->pixels;

        for (int py = y1; py < y2; py++)
                for (int px = x1; px < x2; px++)
                {
                        unsigned int halo, cross;

                        if (made)
                                halo = compositor_close_cover.halo[(py - by) * side + (px - bx)],
                                cross = compositor_close_cover.cross[(py - by) * side + (px - bx)];
                        else
                                compositor_close_sample(side, half, px - bx, py - by, &halo, &cross);
                        if (!halo)
                                continue;

                        unsigned int under = picture ? picture[(unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(px - frame->x)]
                                                     : compositor_picture_at(frame, px - frame->x, py - frame->y);
                        unsigned int pixel = compositor_mix(under, window_mix(under, tone, 140), halo * 255 / 16);

                        if (cross)
                                pixel = compositor_mix(pixel, ink, cross * 255 / 16);
                        t->pixels[(unsigned long)(py - t->oy) * t->pitch + (unsigned long)(px - t->ox)] = pixel;
                }
}

// What is inside a frame's line, through the target's clip: a window's pixels, or one of the panel's tabs.
static void compositor_contents(const struct compositor_target *t, const struct compositor_frame *frame)
{
        // A frame with no picture is its line and corners only (a grid tile's border): it has no contents to draw.
        if (!frame->builtin && !frame->pixels)
                return;
        if (frame->builtin)
                compositor_panel_tab(t, frame->slot, compositor_panel_mark(frame), frame->x, frame->y, frame->width, frame->height);
        else if (compositor_scaled(frame))
                compositor_blit_scaled(t, frame->x, frame->y, frame->width, frame->height, frame->pixels, frame->pitch,
                                       frame->source_w, frame->source_h);
        else if (frame->runs)
                compositor_blit_runs(t, frame->x, frame->y, frame->width, frame->height, frame->pixels, frame->pitch, frame->runs);
        else
                compositor_blit(t, frame->x, frame->y, frame->width, frame->height, frame->pixels, frame->pitch);
}

/*
        A frame drawn over what the target holds: a window -- its line, then
        its pixels -- or a tab of the panel. Square, the line is four bands
        and the contents one rectangle. Round, the contents are a cross that
        leaves out the four corner squares of the line's radius, the line is
        four bands between them, and in each round corner's square a pixel
        is the target's with the line's outside mixed over it by the outer
        mask and the contents' mixed over that by the inner one: the one
        place anything here reads the target, which the scene makes sure is
        memory of its own and never the scanout. A corner the frame keeps
        square is its line and its contents as they are.
*/
/*
        What of a frame's line is lit (frame->lit), over the line between the corner squares: an edge held whole, a
        corner held as COMPOSITOR_LIT_REACH of each of its two edges from the corner's square (its arc is lit with the
        corners, compositor_corner_lit), or every edge.
*/
static void compositor_lit_line(const struct compositor_target *t, const struct compositor_frame *frame, int ox, int oy,
                                int ow, int oh, int r, int b)
{
        unsigned int lit = frame->lit & COMPOSITOR_LIT_ALL ? 15u : frame->lit;
        int across = lit & (COMPOSITOR_LIT_LEFT | COMPOSITOR_LIT_RIGHT), down = lit & (COMPOSITOR_LIT_TOP | COMPOSITOR_LIT_BOTTOM);
        int corner = across && down && !(frame->lit & COMPOSITOR_LIT_ALL), reach = COMPOSITOR_LIT_REACH * frame->scale / 100;
        int wide = ow - 2 * r, tall = oh - 2 * r;
        int seg_w = corner && reach < wide ? reach : wide, seg_h = corner && reach < tall ? reach : tall;
        int from_x = corner && (lit & COMPOSITOR_LIT_RIGHT) ? ox + ow - r - seg_w : ox + r;
        int from_y = corner && (lit & COMPOSITOR_LIT_BOTTOM) ? oy + oh - r - seg_h : oy + r;

        if (lit & COMPOSITOR_LIT_TOP)
                compositor_fill(t, from_x, oy, seg_w, b, frame->lit_ink);
        if (lit & COMPOSITOR_LIT_BOTTOM)
                compositor_fill(t, from_x, oy + oh - b, seg_w, b, frame->lit_ink);
        if (lit & COMPOSITOR_LIT_LEFT)
                compositor_fill(t, ox, from_y, b, seg_h, frame->lit_ink);
        if (lit & COMPOSITOR_LIT_RIGHT)
                compositor_fill(t, ox + ow - b, from_y, b, seg_h, frame->lit_ink);
}

// Whether corner (bit 0 the right, bit 1 the bottom) is lit: every edge is, or both of the edges that meet there.
static int compositor_corner_lit(const struct compositor_frame *frame, int corner)
{
        return (frame->lit & COMPOSITOR_LIT_ALL) ||
               ((frame->lit & (corner & 1 ? COMPOSITOR_LIT_RIGHT : COMPOSITOR_LIT_LEFT)) &&
                (frame->lit & (corner & 2 ? COMPOSITOR_LIT_BOTTOM : COMPOSITOR_LIT_TOP)));
}

/*
        One round corner's square, a row at a time, for a frame whose contents are its picture at its own size or a tab's
        ink (a scaled picture keeps the pixel loop in compositor_draw_with). Each row is two calls of memory_mix_u32_row,
        the same two mixes the pixel loop makes in the same order: the line's outside over the target by the outer
        mask, over the edge, then, where the row is inside the contents, the contents over that by the inner mask (or
        the outer where there is no line). A corner on the right reads the mirrored masks forward, so every call's
        covers and inks are plain runs. The ink rows are made here, on the stack, so bands drawn at once share nothing.
*/
static void compositor_corner_rows(const struct compositor_target *t, const struct compositor_frame *frame, int corner,
                                   int r, int b, unsigned int edge, unsigned int tab)
{
        int x = frame->x, y = frame->y, w = frame->width, h = frame->height, ox, oy, ow, oh;
        int ri = r > b ? r - b : 0, right = corner & 1, down = (corner & 2) != 0;
        const unsigned char *outer = right ? compositor_corner_mirror(r) : compositor_corner_mask(r);
        const unsigned char *inner = ri ? (right ? compositor_corner_mirror(ri) : compositor_corner_mask(ri)) : 0;
        unsigned int edge_row[COMPOSITOR_RADIUS_MOST], tab_row[COMPOSITOR_RADIUS_MOST];
        int cx, cy, x1, y1, x2, y2, sx1, sx2;

        compositor_outline(frame, &ox, &oy, &ow, &oh);
        cx = right ? ox + ow - r : ox;
        cy = down ? oy + oh - r : oy;
        x1 = cx > t->x1 ? cx : t->x1;
        y1 = cy > t->y1 ? cy : t->y1;
        x2 = cx + r < t->x2 ? cx + r : t->x2;
        y2 = cy + r < t->y2 ? cy + r : t->y2;
        // Every row has the same columns, so the inks that are not a picture are made once.
        sx1 = x1 > x ? x1 : x;
        sx2 = x2 < x + w ? x2 : x + w;
        if (b && x2 > x1)
                memory_fill_u32(edge_row, (positive)(x2 - x1), edge & 0xffffffu);
        if (frame->builtin && sx2 > sx1)
                memory_fill_u32(tab_row, (positive)(sx2 - sx1), tab);
        for (int py = y1; py < y2; py++)
        {
                int my = down ? cy + r - 1 - py : py - cy;
                unsigned int *at = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);

                if (b && !frame->builtin && !frame->pixels)
                {
                        // A border only (a grid tile's): the line's cover is the outer shape's less the inner's, so the square
                        // inside the line is not painted; a picture is what covers it for a frame that has one.
                        unsigned char ring[COMPOSITOR_RADIUS_MOST];

                        for (int k = 0; k < x2 - x1; k++)
                        {
                                int col = x1 - cx + k, iy = my - b, ix = right ? col : col - b;
                                unsigned int oc = outer[(unsigned long)my * r + col], ic = 0;

                                if (ri && iy >= 0 && iy < ri && ix >= 0 && ix < ri)
                                        ic = inner[(unsigned long)iy * ri + ix];
                                ring[k] = oc > ic ? (unsigned char)(oc - ic) : 0;
                        }
                        memory_mix_u32_row(at, edge_row, ring, (positive)(x2 - x1));
                }
                else if (b)
                        memory_mix_u32_row(at, edge_row, outer + (unsigned long)my * r + (x1 - cx), (positive)(x2 - x1));
                if (py >= y && py < y + h && ri && sx2 > sx1 && (frame->builtin || frame->pixels))
                {
                        int kx = sx1 - cx;
                        const unsigned char *cover;
                        const unsigned int *row;

                        if (!b)
                                cover = outer + (unsigned long)my * r + kx;
                        else if (right)
                                cover = inner + (unsigned long)(my - b) * ri + kx;
                        else
                                cover = inner + (unsigned long)(my - b) * ri + (kx - b);
                        row = frame->builtin ? tab_row
                                             : frame->pixels + (unsigned long)(py - y) * frame->pitch + (unsigned long)(sx1 - x);
                        memory_mix_u32_row(t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(sx1 - t->ox),
                                           row, cover, (positive)(sx2 - sx1));
                }
        }
}

/*
        A frame drawn over a target whose radius is r, which is compositor_radius of the frame: a scene makes those once
        for all its frames, and compositor_draw makes this one. Nothing is written outside the frame's outline, so a
        target the outline misses is left as it is, and that is the first thing asked.
*/
static void compositor_draw_with(const struct compositor_target *t, const struct compositor_frame *frame, int r)
{
        int x = frame->x, y = frame->y, w = frame->width, h = frame->height;
        int ox, oy, ow, oh, b, ri;
        struct compositor_target part;

        compositor_outline(frame, &ox, &oy, &ow, &oh);
        if (ox + ow <= t->x1 || oy + oh <= t->y1 || ox >= t->x2 || oy >= t->y2)
                return;
        b = x - ox;
        ri = r > b ? r - b : 0;

        // The line, between the corner squares, and what of it the hand holds over it.
        if (b)
        {
                compositor_fill(t, ox + r, oy, ow - 2 * r, b, frame->edge);
                compositor_fill(t, ox + r, y + h, ow - 2 * r, b, frame->edge);
                compositor_fill(t, ox, oy + r, b, oh - 2 * r, frame->edge);
                compositor_fill(t, x + w, oy + r, b, oh - 2 * r, frame->edge);
                if (frame->lit)
                        compositor_lit_line(t, frame, ox, oy, ow, oh, r, b);
        }

        // The contents, but for the corners' squares.
        if (!r)
        {
                compositor_contents(t, frame);
                compositor_close_draw(t, frame);
                compositor_minimize_draw(t, frame);
                return;
        }
        part = compositor_clipped(t, x + ri, y, w - 2 * ri, h);
        compositor_contents(&part, frame);
        part = compositor_clipped(t, x, y + ri, ri, h - 2 * ri);
        compositor_contents(&part, frame);
        part = compositor_clipped(t, x + w - ri, y + ri, ri, h - 2 * ri);
        compositor_contents(&part, frame);

        const unsigned char *outer = compositor_corner_mask(r), *inner = ri ? compositor_corner_mask(ri) : 0;
        // The corners the hand holds, a bit each, decided once: nothing at all is asked of them with nothing lit.
        unsigned int lit_corners = 0;

        if (frame->lit)
                for (int corner = 0; corner < 4; corner++)
                        lit_corners |= (unsigned int)compositor_corner_lit(frame, corner) << corner;
        unsigned int tab = frame->builtin ? taskbar_tab_ink(frame->slot, compositor_panel_mark(frame)) : 0;
        // A picture drawn at its own size is read straight from its pixels; a scaled one through its map.
        const unsigned int *picture = compositor_scaled(frame) ? 0 : frame->pixels;

        for (int corner = 0; corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;
                int x1 = cx > t->x1 ? cx : t->x1, y1 = cy > t->y1 ? cy : t->y1;
                int x2 = cx + r < t->x2 ? cx + r : t->x2, y2 = cy + r < t->y2 ? cy + r : t->y2;
                unsigned int edge = lit_corners >> corner & 1 ? frame->lit_ink : frame->edge;

                // A corner the target does not hold has nothing of its own to write, square or round.
                if (x2 <= x1 || y2 <= y1)
                        continue;
                if (frame->square & (1 << corner))
                {
                        if (b)
                        {
                                part = compositor_clipped(t, cx, cy, r, r);
                                compositor_fill(&part, cx, cy, r, r, edge);
                        }
                        part = compositor_clipped(t, cx, cy, r, r);
                        compositor_contents(&part, frame);
                        continue;
                }
                if (frame->builtin || !compositor_scaled(frame))
                {
                        compositor_corner_rows(t, frame, corner, r, b, edge, tab);
                        continue;
                }

                for (int py = y1; py < y2; py++)
                        for (int px = x1; px < x2; px++)
                        {
                                int mx = corner & 1 ? cx + r - 1 - px : px - cx, my = corner & 2 ? cy + r - 1 - py : py - cy;
                                unsigned int cover = outer[my * r + mx];
                                unsigned int *at = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(px - t->ox);
                                unsigned int pixel = *at;

                                // The line where the outer circle is; the contents where the inner one is, which is
                                // the part of the square inside the contents, b in from its outer sides.
                                if (cover)
                                        pixel = b ? compositor_mix(pixel, edge, cover) : pixel;
                                if (px >= x && px < x + w && py >= y && py < y + h)
                                {
                                        unsigned int in = inner ? inner[(my - b) * ri + (mx - b)] : 0;
                                        unsigned int ink = frame->builtin ? tab
                                                           : picture ? picture[(unsigned long)(py - y) * frame->pitch + (unsigned long)(px - x)]
                                                           : compositor_picture_at(frame, px - x, py - y);

                                        if (!b)
                                                in = cover;
                                        pixel = in == 255 ? ink : in ? compositor_mix(pixel, ink, in) : pixel;
                                }
                                *at = pixel;
                        }
        }
        compositor_close_draw(t, frame);
        compositor_minimize_draw(t, frame);
}

static void compositor_draw(const struct compositor_target *t, const struct compositor_frame *frame)
{
        compositor_draw_with(t, frame, compositor_radius(frame));
}

#ifdef MOONWATER_GPU_C
/*
        The GPU's compose (src/canvas/gpu.c): frames as the records of its scene, which draws them in order as compositor_draw does -- a frame's
        pictures copied, its line filled, and in each round corner's square the line's colour and then the picture mixed by a table of covers
        (compositor_corner_rows' own arithmetic, made once and kept in the GPU's buffer). A frame it cannot draw as the processor does
        (the panel, a picture scaled, a line lit, a close button, a preview) makes the whole answer none, and the frames are drawn by the processor.
*/
#define COMPOSITOR_GPU_TABLES 128
// A frame's picture's address for the GPU: in the map that holds all of it, 0 for none. A map that holds only its first byte (made when the window was
// smaller) would have the card read its rows below from the addresses after that map's end, which are another buffer's: another window's pixels.
#define compositor_gpu_va(f) gpu_va_bytes((f)->pixels, 4 * ((f)->pitch * (positive)((f)->height - 1) + (positive)(f)->width))
struct compositor_gscene
{
        unsigned int *ops;                  // the records, in the GPU's buffer
        unsigned int count, room, width, height; // and the picture they are for
        unsigned char *tables;              // the cover tables' pool, in the GPU's buffer, its size and its place from the buffer's start
        unsigned int tables_size, tables_at, tables_used, kept;
        struct
        {
                int r, b, corner, content;
                unsigned int at;
        } table[COMPOSITOR_GPU_TABLES];
};
static struct compositor_gscene compositor_gscene;

// The pool's table for a key (a size of bytes): its place from the buffer's start and, when it is new, where to fill it; none when the pool is full.
static unsigned int compositor_gpu_slot(int r, int b, int corner, int content, unsigned int size, unsigned char **fill)
{
        struct compositor_gscene *g = &compositor_gscene;

        *fill = 0;
        for (unsigned int k = 0; k < g->kept; k++)
                if (g->table[k].r == r && g->table[k].b == b && g->table[k].corner == corner && g->table[k].content == content)
                        return g->table[k].at;
        if (g->kept == COMPOSITOR_GPU_TABLES || g->tables_used + size > g->tables_size)
                return 0;
        *fill = g->tables + g->tables_used;
        g->table[g->kept].r = r, g->table[g->kept].b = b, g->table[g->kept].corner = corner, g->table[g->kept].content = content;
        g->table[g->kept++].at = g->tables_at + g->tables_used;
        g->tables_used += (size + 15) & ~15u;
        return g->table[g->kept - 1].at;
}

// The cover table of a corner's square (r by r bytes, rows from the square's top): the line's outside (content 0), or the picture's inside it (content 1), as
// compositor_corner_rows reads the masks, flipped and mirrored so that the square is read from its own top left. Its place from the buffer's start, 0 for none.
static unsigned int compositor_gpu_table(int r, int b, int corner, int content)
{
        int ri = r > b ? r - b : 0, right = corner & 1, down = (corner & 2) != 0;
        const unsigned char *outer = right ? compositor_corner_mirror(r) : compositor_corner_mask(r);
        const unsigned char *inner = ri ? (right ? compositor_corner_mirror(ri) : compositor_corner_mask(ri)) : 0;
        unsigned char *table;
        unsigned int at = compositor_gpu_slot(r, b, corner, content, (unsigned int)(r * r), &table);

        if (!table)
                return at;
        for (int row = 0; row < r; row++)
                for (int col = 0; col < r; col++)
                {
                        int my = down ? r - 1 - row : row;
                        unsigned char cover = outer[my * r + col];

                        if (content && b)
                        {
                                int iy = my - b, ix = right ? col : col - b;

                                cover = inner && iy >= 0 && iy < ri && ix >= 0 && ix < ri ? inner[iy * ri + ix] : 0;
                        }
                        table[row * r + col] = cover;
                }
        return at;
}

// A close button's table (side by side bytes): the halo's covers (which 0) or the strokes' (which 1), in 255ths as the processor mixes them (the samples of sixteen).
static unsigned int compositor_gpu_close_table(int side, int half, int which)
{
        unsigned char *table;
        unsigned int at = compositor_gpu_slot(side, half, 4, 2 + which, (unsigned int)(side * side), &table);

        for (int y = 0; table && y < side; y++)
                for (int x = 0; x < side; x++)
                {
                        unsigned int halo, cross;

                        compositor_close_sample(side, half, x, y, &halo, &cross);
                        table[y * side + x] = (unsigned char)((which ? cross : halo) * 255 / 16);
                }
        return at;
}

// One record: the rectangle cut to the picture, none when nothing is left. A fill writes the colour; a copy takes the picture's pixels from the frame, at the
// rectangle's own corner; a mix is memory_mix_u32_row by the cover table at cover (pitch r), the ink the colour or, with a frame, the picture.
static void compositor_gpu_rect(unsigned int kind, int x0, int y0, int x1, int y1, unsigned int colour, const struct compositor_frame *frame, unsigned int cover, int r)
{
        struct compositor_gscene *g = &compositor_gscene;
        int cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0, cx1 = x1 > (int)g->width ? (int)g->width : x1, cy1 = y1 > (int)g->height ? (int)g->height : y1;
        unsigned int *op = g->ops + 8 * g->count;

        if (cx1 <= cx0 || cy1 <= cy0)
                return;
        op[0] = kind, op[1] = (unsigned int)cx0, op[2] = (unsigned int)cy0, op[3] = (unsigned int)cx1, op[4] = (unsigned int)cy1;
        op[5] = colour, op[6] = op[7] = 0;
        if (frame)
        {
                positive at = compositor_gpu_va(frame) + 4ull * ((positive)(cy0 - frame->y) * frame->pitch + (positive)(cx0 - frame->x));

                if (kind == 4)
                        op[5] = (unsigned int)at, op[6] = (unsigned int)(at >> 32), op[7] = (unsigned int)frame->pitch;
                else
                        op[0] |= (unsigned int)frame->pitch << 16, op[6] = (unsigned int)at, op[7] = (unsigned int)(at >> 32);
        }
        if (kind == 5)
                op[0] |= (unsigned int)r << 8, op[5] = cover + (unsigned int)((cy0 - y0) * r + (cx0 - x0)), op[6] = frame ? op[6] : colour;
        g->count++;
}

// The close button and the minimise button over a window's pixels (compositor_close_draw, compositor_minimize_draw): the halo mixes the window's own pixels towards
// their mix with a tone and the strokes mix the ink over that; the dash is a fill. False when the button is not wholly in the picture (the processor draws it cut).
static b32 compositor_gpu_buttons(const struct compositor_frame *f)
{
        int bx, by, side, mx, my, in = COMPOSITOR_MINIMIZE_IN * f->scale / 100, half = compositor_px1(f->scale);
        unsigned int mean = f->close_mean, ink = window_contrast(mean), tone = window_light(mean) ? 0xffffffu : compositor_ink_shadow();
        unsigned int halo, cross;
        positive at;
        unsigned int *op = compositor_gscene.ops + 8 * compositor_gscene.count;

        if (!compositor_close_box(f, &bx, &by, &side))
                return true;
        if (bx < 0 || by < 0 || bx + side > (int)compositor_gscene.width || by + side > (int)compositor_gscene.height || side > 255 || f->pitch > 65535)
                return false;
        halo = compositor_gpu_close_table(side, half, 0), cross = compositor_gpu_close_table(side, half, 1);
        if (!halo || !cross)
                return false;
        at = compositor_gpu_va(f) + 4ull * ((positive)(by - f->y) * f->pitch + (positive)(bx - f->x));
        op[0] = 6u | (unsigned int)side << 8 | (unsigned int)f->pitch << 16, op[1] = (unsigned int)bx, op[2] = (unsigned int)by, op[3] = (unsigned int)(bx + side);
        op[4] = (unsigned int)(by + side), op[5] = halo, op[6] = (unsigned int)at, op[7] = (unsigned int)(at >> 32) | tone << 8;
        compositor_gscene.count++;
        compositor_gpu_rect(5, bx, by, bx + side, by + side, ink, 0, cross, side);
        if (compositor_minimize_box(f, &mx, &my, &side))
                compositor_gpu_rect(0, mx + in, my + side / 2 - half, mx + in + side - 2 * in, my + side / 2 + half, ink, 0, 0, 0);
        return true;
}

// What of the line is lit (compositor_lit_line), over the line between the corner squares: the same bands as fills in the lit ink.
static void compositor_gpu_lit(const struct compositor_frame *f, int ox, int oy, int ow, int oh, int r, int b)
{
        unsigned int lit = f->lit & COMPOSITOR_LIT_ALL ? 15u : f->lit;
        int across = lit & (COMPOSITOR_LIT_LEFT | COMPOSITOR_LIT_RIGHT), down = lit & (COMPOSITOR_LIT_TOP | COMPOSITOR_LIT_BOTTOM);
        int corner = across && down && !(f->lit & COMPOSITOR_LIT_ALL), reach = COMPOSITOR_LIT_REACH * f->scale / 100;
        int wide = ow - 2 * r, tall = oh - 2 * r;
        int seg_w = corner && reach < wide ? reach : wide, seg_h = corner && reach < tall ? reach : tall;
        int from_x = corner && (lit & COMPOSITOR_LIT_RIGHT) ? ox + ow - r - seg_w : ox + r;
        int from_y = corner && (lit & COMPOSITOR_LIT_BOTTOM) ? oy + oh - r - seg_h : oy + r;

        if (lit & COMPOSITOR_LIT_TOP)
                compositor_gpu_rect(0, from_x, oy, from_x + seg_w, oy + b, f->lit_ink, 0, 0, 0);
        if (lit & COMPOSITOR_LIT_BOTTOM)
                compositor_gpu_rect(0, from_x, oy + oh - b, from_x + seg_w, oy + oh, f->lit_ink, 0, 0, 0);
        if (lit & COMPOSITOR_LIT_LEFT)
                compositor_gpu_rect(0, ox, from_y, ox + b, from_y + seg_h, f->lit_ink, 0, 0, 0);
        if (lit & COMPOSITOR_LIT_RIGHT)
                compositor_gpu_rect(0, ox + ow - b, from_y, ox + ow, from_y + seg_h, f->lit_ink, 0, 0, 0);
}

// What is inside a frame's line, in a rectangle of it: the picture's pixels copied, or the tab's one ink.
static void compositor_gpu_contents(const struct compositor_frame *f, unsigned int tab, int x0, int y0, int x1, int y1)
{
        if (f->builtin)
                compositor_gpu_rect(0, x0, y0, x1, y1, tab, 0, 0, 0);
        else
                compositor_gpu_rect(4, x0, y0, x1, y1, 0, f, 0, 0);
}

// A frame as compositor_draw_with draws it (a window with a picture at its own size, or a tab of the panel, and nothing over it): false if it is not one, or the pools are full.
static b32 compositor_gpu_frame(const struct compositor_frame *f)
{
        struct compositor_gscene *g = &compositor_gscene;
        int x = f->x, y = f->y, w = f->width, h = f->height, ox, oy, ow, oh, r = compositor_radius(f), b, ri;

        unsigned int tab = f->builtin ? taskbar_tab_ink(f->slot, compositor_panel_mark(f)) : 0; // a tab of the panel is one ink

        if ((!f->builtin && (!f->pixels || w <= 0 || h <= 0 || !compositor_gpu_va(f))) || compositor_scaled(f) || f->preview[0] || f->preview[1] || f->preview[2] ||
            f->preview[3] || f->mark[0] || f->mark[1] || f->mark[2] || f->mark[3] || g->count + 40 > g->room - 400)
                return false;
        compositor_outline(f, &ox, &oy, &ow, &oh);
        b = x - ox, ri = r > b ? r - b : 0;
        if (b)
        {
                compositor_gpu_rect(0, ox + r, oy, ox + ow - r, oy + b, f->edge, 0, 0, 0);
                compositor_gpu_rect(0, ox + r, y + h, ox + ow - r, y + h + b, f->edge, 0, 0, 0);
                compositor_gpu_rect(0, ox, oy + r, ox + b, oy + oh - r, f->edge, 0, 0, 0);
                compositor_gpu_rect(0, x + w, oy + r, x + w + b, oy + oh - r, f->edge, 0, 0, 0);
                if (f->lit)
                        compositor_gpu_lit(f, ox, oy, ow, oh, r, b);
        }
        if (!r)
        {
                compositor_gpu_contents(f, tab, x, y, x + w, y + h);
                return compositor_gpu_buttons(f);
        }
        compositor_gpu_contents(f, tab, x + ri, y, x + w - ri, y + h);
        compositor_gpu_contents(f, tab, x, y + ri, x + ri, y + h - ri);
        compositor_gpu_contents(f, tab, x + w - ri, y + ri, x + w, y + h - ri);
        for (int corner = 0; corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;
                int sx1 = cx > x ? cx : x, sx2 = cx + r < x + w ? cx + r : x + w, sy1 = cy > y ? cy : y, sy2 = cy + r < y + h ? cy + r : y + h;
                unsigned int edge_table, content_table, edge = compositor_corner_lit(f, corner) && f->lit ? f->lit_ink : f->edge;

                if (f->square & (1 << corner))
                {
                        if (b)
                                compositor_gpu_rect(0, cx, cy, cx + r, cy + r, edge, 0, 0, 0);
                        compositor_gpu_contents(f, tab, sx1, sy1, sx2, sy2);
                        continue;
                }
                edge_table = b ? compositor_gpu_table(r, b, corner, 0) : 0;
                content_table = ri ? compositor_gpu_table(r, b, corner, 1) : 0;
                if ((b && !edge_table) || (ri && !content_table))
                        return false;
                if (b)
                        compositor_gpu_rect(5, cx, cy, cx + r, cy + r, edge & 0xffffffu, 0, edge_table, r);
                if (ri && sx2 > sx1 && sy2 > sy1)
                        compositor_gpu_rect(5, sx1, sy1, sx2, sy2, tab, f->builtin ? 0 : f, content_table + (unsigned int)((sy1 - cy) * r + (sx1 - cx)), r);
        }
        return compositor_gpu_buttons(f);
}

// The frames, in order, over a ground: the scene's records; false (and none) if any is not one the GPU draws.
static b32 compositor_gpu_frames(const struct compositor_frame *frames, unsigned int count, unsigned int ground)
{
        struct compositor_gscene *g = &compositor_gscene;

        g->count = 0;
        if (g->kept > COMPOSITOR_GPU_TABLES / 2) // the tables of the last scenes let go when the pool is half full: a scene is under that
                g->kept = 0, g->tables_used = 0;
        compositor_gpu_rect(0, 0, 0, (int)g->width, (int)g->height, ground, 0, 0, 0);
        for (unsigned int k = 0; k < count; k++)
                if (!compositor_gpu_frame(&frames[k]))
                {
                        g->count = 0;
                        return false;
                }
        return true;
}
#endif

/*
        The tabs of a panel as frames of their own, each its tab's rectangle
        and nothing of the band between them: shapes the scene draws when
        there is no font, and otherwise each the part of the band's pixels
        its tab is. Round on the side towards the middle of the output and
        square on the side that is off its edge (taskbar_square). Answers how many.
*/
static unsigned int compositor_panel_tabs(const struct compositor_frame *band, struct compositor_frame *tabs)
{
        struct taskbar_view view = compositor_taskbar_view();
        unsigned int count = 0;

        for (int slot = 0; slot <= (int)band->mark_count + 1; slot++)
        {
                int which = slot < (int)band->mark_count ? slot : slot == (int)band->mark_count ? -2 : TASKBAR_LAUNCHER;
                int x, y, width, height;

                if (!taskbar_slot(&view, which, &x, &y, &width, &height))
                        continue;
                tabs[count] = *band;
                tabs[count].x = x;
                tabs[count].y = y;
                tabs[count].width = width;
                tabs[count].height = height;
                tabs[count].slot = which;
                tabs[count].launcher_lit = band->launcher_lit;
                tabs[count].radius = UI_RADIUS * band->scale / 100;
                tabs[count].line = 0;
                tabs[count].square = taskbar_square(band->position);
                if (!band->builtin)
                        tabs[count].pixels = band->pixels + (unsigned long)(y - band->y) * band->pitch + (unsigned long)(x - band->x);
                count++;
        }
        return count;
}

// What a window fills: nothing, the output, half of it, or a quarter; or the room of its screen, the taskbar left out,
// which only a placement asks for (WINDOW_PLACE_MAXIMIZED) and which follows the taskbar where it is moved.
// The quarters are the four TOP_LEFT..BOTTOM_RIGHT in that order, and compositor_screen_part reads them as such.
#define COMPOSITOR_FLOATING 0
#define COMPOSITOR_MAXIMIZED 1
#define COMPOSITOR_LEFT 2
#define COMPOSITOR_RIGHT 3
#define COMPOSITOR_ROOM 4
#define COMPOSITOR_BOTTOM 5
#define COMPOSITOR_TOP_LEFT 6
#define COMPOSITOR_TOP_RIGHT 7
#define COMPOSITOR_BOTTOM_LEFT 8
#define COMPOSITOR_BOTTOM_RIGHT 9
// The windows, and a tab for each window and a tray and a launcher button for each band (`taskbar display all` is one on every screen).
#define COMPOSITOR_ASSIST_MAX 8
#define COMPOSITOR_GRID_TILES 7
#define COMPOSITOR_CELLS 15u // the four quarters of a screen, a bit each (compositor_tile_cells)
#define COMPOSITOR_OVERLAYS 40 // the snap assist's thumbnails and veils, and the grid's tiles, borders and regions (compositor_overlays)
#define COMPOSITOR_FRAMES (2 * WINDOWS + 2 + 2 * WINDOWS_OUTPUTS + COMPOSITOR_OVERLAYS) // the snap preview has a frame of its own

/*
        The scene: inside the target's damage, each window where nothing
        above it covers it and the desktop where nothing is, every pixel
        written once and none read. The frames are bottom first.

        The damage is cut into bands at the top and bottom edge of every
        piece of a window -- a square one is one piece, a round one three,
        its middle and its two sides between the corners -- so in one band
        each piece covers the same columns from the band's top to its
        bottom. The columns are given out from the top piece down: each
        takes what it covers of the spans still unclaimed and leaves the
        rest, which are never more than one more than there are pieces.
        What no piece claims is desktop. The target is drawn on through a
        clip of the cell, so the picture is the one the windows drawn bottom
        to top over a desktop would be, but in the corners of round windows,
        which no piece holds: each of those squares is drawn so, every frame
        bottom to top, into a scratch of its own, and then copied out. The
        target is written and never read.
*/
#define COMPOSITOR_PIECES (3 * COMPOSITOR_FRAMES)

static unsigned int compositor_corner_scratch[COMPOSITOR_RADIUS_MOST * COMPOSITOR_RADIUS_MOST];

// A run of the tint's colour and cover, as long as the row routine takes them at a time (compositor_mix_run).
#define COMPOSITOR_TINT_RUN 256

// The pixels the snap preview's tints were made over, all of them since the start: what a drag over a target costs.
unsigned long compositor_tint_pixels;

/*
        A run of a row mixed with one ink: memory_mix_u32_row over it with the colour and cover runs made once for the tint
        (compositor_tint), which is the same pixels as compositor_mix per pixel. A cover of 0 or 255 is the pixel rule
        itself (compositor_mix's clear and its whole ink), which the row routine does not take, so those are per pixel.
*/
static void compositor_mix_run(unsigned int *pixels, long count, unsigned int ink, const unsigned int *colour,
                               const unsigned char *cover)
{
        if ((ink >> 24) == 0 || (ink >> 24) >= 255)
        {
                for (long at = 0; at < count; at++)
                        pixels[at] = compositor_mix(pixels[at], ink & 0xffffffu, ink >> 24);
                return;
        }
        for (long done = 0; done < count; done += COMPOSITOR_TINT_RUN)
        {
                long n = count - done < COMPOSITOR_TINT_RUN ? count - done : COMPOSITOR_TINT_RUN;

                memory_mix_u32_row(pixels + done, colour, cover, (positive)n);
        }
}

/*
        A window's line and round corners drawn again over a tint: its outline less its contents' cross (the pixels
        compositor_tint leaves alone, which hold its contents already), as eight rectangles that meet at no pixel, so each
        pixel is drawn once and a corner is mixed once. The line is four bands, the corners' squares are the ri of the
        radius past the line; a square window, or one with no line, has none.
*/
static void compositor_draw_ring(const struct compositor_target *cell, const struct compositor_frame *f, int r)
{
        int ox, oy, ow, oh, line, ri, x = f->x, y = f->y, w = f->width, h = f->height;
        int rects[8][4];

        compositor_outline(f, &ox, &oy, &ow, &oh);
        line = x - ox;
        ri = r > line ? r - line : 0;
        int top[4] = {ox, oy, ow, y - oy}, bottom[4] = {ox, y + h, ow, oy + oh - (y + h)}, left[4] = {ox, y, x - ox, h},
            right[4] = {x + w, y, ox + ow - (x + w), h};
        memory_copy(rects[0], top, sizeof(top));
        memory_copy(rects[1], bottom, sizeof(bottom));
        memory_copy(rects[2], left, sizeof(left));
        memory_copy(rects[3], right, sizeof(right));
        int corners[4][4] = {{x, y, ri, ri}, {x + w - ri, y, ri, ri}, {x, y + h - ri, ri, ri}, {x + w - ri, y + h - ri, ri, ri}};
        memory_copy(rects[4], corners, sizeof(corners));
        for (int at = 0; at < 8; at++)
        {
                struct compositor_target part;

                if (rects[at][2] <= 0 || rects[at][3] <= 0)
                        continue;
                part = compositor_clipped(cell, rects[at][0], rects[at][1], rects[at][2], rects[at][3]);
                if (part.x2 > part.x1 && part.y2 > part.y1)
                        compositor_draw_with(&part, f, r);
        }
}

/*
        A tint over a rectangle of the scene, at the ink's own alpha (its top byte), cut to the band being drawn; then
        the frames from `from` up drawn again over it, so what is over the tint is as it is and the tint shows only
        where none of them is. Run after the scene is composed, so what the tint is over is final. Under is the snap
        preview of frames[from], the window it belongs to: its own contents are copied over the tint, so the tint is not
        made where they are (the cross compositor_draw_with copies), and the pixels are the same as with it made.
*/
/*
        A tint's shape, when it has one (a snap preview's: a tile is drawn as the window will be): its corners round at r and
        a line of `line` along its inside in the edge's ink, the rest in the fill's. Each row is a run at one cover between
        two ends drawn a pixel at a time: the corners' squares (the outer mask less the inner one is the line, the inner the
        fill, as a border frame's corner is drawn) on the rows of the corners, and the line's columns on the rest.
*/
struct compositor_shape
{
        int x, y, w, h, r, line;
        unsigned int fill, edge; // 0xaarrggbb
        const unsigned char *outer, *outer_right, *inner, *inner_right;
};

static unsigned int compositor_shape_cover(const struct compositor_shape *s, int px, int my, int corner)
{
        int ri = s->r - s->line, right = px >= s->x + s->w - s->r, col = right ? px - (s->x + s->w - s->r) : px - s->x;
        unsigned int outer, inner = 0;
        int iy = my - s->line, ix = right ? col : col - s->line;

        if (!corner)
                return s->edge >> 24;
        outer = (right ? s->outer_right : s->outer)[my * s->r + col];
        if (ri > 0 && iy >= 0 && iy < ri && ix >= 0 && ix < ri)
                inner = (right ? s->inner_right : s->inner)[iy * ri + ix];
        inner = inner > outer ? outer : inner;
        return ((outer - inner) * (s->edge >> 24) + inner * (s->fill >> 24) + 127) / 255;
}

// The columns [a, b) of row py of a shaped tint, at in the cell's pixels being column a.
static unsigned long compositor_shape_row(const struct compositor_shape *s, unsigned int *at, int a, int b, int py,
                                          const unsigned int *colour, const unsigned char *fill_cover, const unsigned char *edge_cover)
{
        int top = py - s->y, bottom = s->y + s->h - 1 - py, my = top < bottom ? top : bottom, corner = my < s->r;
        int zone = corner ? s->r : s->line, middle_to = s->x + s->w - zone;
        int edge_row = my < s->line;

        for (int px = a; px < b; px++)
        {
                if (px >= s->x + zone && px < middle_to)
                {
                        int to = middle_to < b ? middle_to : b;

                        compositor_mix_run(at + (px - a), to - px, edge_row ? s->edge : s->fill, colour,
                                           edge_row ? edge_cover : fill_cover);
                        px = to - 1;
                        continue;
                }
                at[px - a] = compositor_mix(at[px - a], s->fill & 0xffffffu, compositor_shape_cover(s, px, my, corner));
        }
        return (unsigned long)(b > a ? b - a : 0);
}

static void compositor_tint(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                            const int *radii, const int *rect, unsigned int ink, unsigned int from, int under, int radius,
                            int edge_line, unsigned int edge)
{
        const struct compositor_frame *owner = under && from < count && !frames[from].builtin ? &frames[from] : 0;
        struct compositor_target cell;
        unsigned int colour[COMPOSITOR_TINT_RUN];
        unsigned char cover[COMPOSITOR_TINT_RUN], edge_cover[COMPOSITOR_TINT_RUN];
        unsigned long made = 0;
        int line = 0, ri = 0;
        struct compositor_shape shape = {rect[0], rect[1], rect[2], rect[3], 0, 0, ink, edge, 0, 0, 0, 0};

        if (rect[2] <= 0 || rect[3] <= 0)
                return;
        cell = compositor_clipped(t, rect[0], rect[1], rect[2], rect[3]);
        if (cell.x2 <= cell.x1 || cell.y2 <= cell.y1)
                return;
        memory_fill_u32(colour, COMPOSITOR_TINT_RUN, ink & 0xffffffu);
        memory_fill(cover, (int)(ink >> 24), COMPOSITOR_TINT_RUN);
        // A shape: its radius kept to its half sides and the masks' largest, and its line inside the radius.
        radius = radius > rect[2] / 2 ? rect[2] / 2 : radius;
        radius = radius > rect[3] / 2 ? rect[3] / 2 : radius;
        radius = radius > COMPOSITOR_RADIUS_MOST ? COMPOSITOR_RADIUS_MOST : radius < 0 ? 0 : radius;
        edge_line = edge_line < 0 || !(edge >> 24) ? 0 : edge_line > radius && radius ? radius : edge_line;
        if (radius || edge_line)
        {
                shape.r = radius, shape.line = edge_line;
                memory_fill(edge_cover, (int)(edge >> 24), COMPOSITOR_TINT_RUN);
                if (radius)
                {
                        shape.outer = compositor_corner_mask(radius), shape.outer_right = compositor_corner_mirror(radius);
                        if (radius - edge_line > 0)
                        {
                                shape.inner = compositor_corner_mask(radius - edge_line);
                                shape.inner_right = compositor_corner_mirror(radius - edge_line);
                        }
                }
        }
        // The contents are the frame less its line, and less the round corners' squares of the radius past the line.
        if (owner)
        {
                int ox, oy, ow, oh;

                compositor_outline(owner, &ox, &oy, &ow, &oh);
                line = owner->x - ox;
                ri = radii[from] > line ? radii[from] - line : 0;
        }
        for (int py = cell.y1; py < cell.y2; py++)
        {
                // The columns [hide_from, hide_to) of this row are the owner's contents: the tint is made left and right of them.
                int hide_from = cell.x2, hide_to = cell.x2, lo, hi;
                unsigned int *at = cell.pixels + (unsigned long)(py - cell.oy) * cell.pitch + (unsigned long)(cell.x1 - cell.ox);

                if (owner && py >= owner->y && py < owner->y + owner->height)
                {
                        int inside = py >= owner->y + ri && py < owner->y + owner->height - ri;

                        hide_from = inside ? owner->x : owner->x + ri;
                        hide_to = inside ? owner->x + owner->width : owner->x + owner->width - ri;
                        if (hide_to <= hide_from)
                                hide_from = hide_to = cell.x2;
                }
                lo = hide_from < cell.x2 ? hide_from : cell.x2;
                hi = hide_to > cell.x1 ? hide_to : cell.x1;
                if (shape.r || shape.line)
                {
                        if (lo > cell.x1)
                                made += compositor_shape_row(&shape, at, cell.x1, lo, py, colour, cover, edge_cover);
                        if (hi < cell.x2)
                                made += compositor_shape_row(&shape, at + (hi - cell.x1), hi, cell.x2, py, colour, cover, edge_cover);
                        continue;
                }
                if (lo > cell.x1)
                {
                        compositor_mix_run(at, lo - cell.x1, ink, colour, cover);
                        made += (unsigned long)(lo - cell.x1);
                }
                if (hi < cell.x2)
                {
                        compositor_mix_run(at + (hi - cell.x1), cell.x2 - hi, ink, colour, cover);
                        made += (unsigned long)(cell.x2 - hi);
                }
        }
        // The bands of a scene are drawn on threads of their own: each adds its own, once.
        __atomic_fetch_add(&compositor_tint_pixels, made, __ATOMIC_RELAXED);
        for (unsigned int each = from; each < count; each++)
        {
                int x, y, width, height;

                if (owner && each == from)
                {
                        compositor_draw_ring(&cell, owner, radii[from]);
                        continue;
                }
                compositor_outline(&frames[each], &x, &y, &width, &height);
                if (x < cell.x2 && x + width > cell.x1 && y < cell.y2 && y + height > cell.y1)
                        compositor_draw_with(&cell, &frames[each], radii[each]);
        }
}

/*
        The tints of a scene, drawn after it is composed: a tint (frame->preview) over what lies under it, from its own
        frame up, so the frames after it are as they are over the tint (the snap preview is a frame of its own after every
        window and tab, so only the overlays are over it); and a shared edge's indicator (frame->mark) from the frame above
        its own, so the window it is drawn over is under it, not over it.
*/
static void compositor_preview_draw(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                                    const int *radii)
{
        for (unsigned int at = 0; at < count; at++)
                compositor_tint(t, frames, count, radii, frames[at].preview, frames[at].preview_ink, at, 1, frames[at].preview_radius,
                                frames[at].preview_line, frames[at].preview_edge);
        for (unsigned int at = 0; at < count; at++)
                compositor_tint(t, frames, count, radii, frames[at].mark, frames[at].mark_ink, at + 1, 0, 0, 0, 0);
}

/*
        The snap preview of the frames of the windows, taken off the window it belongs to into a frame of its own, none
        for no preview: the tint is over every window, the one dragged and the ones above it, a glass one and the tabs, so
        where the target is stays clear under whatever lies there. The frame goes into the list after the tabs and before
        the overlays, which lie over it: the grid's tints and the assist's thumbnails. It is not in the windows' frames,
        so the glass is made from the windows alone, and the tint is over the glass once and not also in what it shows.
*/
static int compositor_preview_lift(struct compositor_frame *frames, unsigned int windows, struct compositor_frame *tint)
{
        for (unsigned int at = 0; at < windows; at++)
        {
                if (frames[at].preview[2] <= 0 || frames[at].preview[3] <= 0)
                        continue;
                memory_zero(tint, sizeof(*tint));
                tint->scale = frames[at].scale;
                memory_copy(tint->preview, frames[at].preview, sizeof(tint->preview));
                tint->preview_ink = frames[at].preview_ink;
                tint->preview_radius = frames[at].preview_radius;
                tint->preview_line = frames[at].preview_line;
                tint->preview_edge = frames[at].preview_edge;
                memory_zero(frames[at].preview, sizeof(frames[at].preview));
                return 1;
        }
        return 0;
}

// Every frame's radius, as compositor_radius gives it: made once for the frames of a damage, and read by every band of it.
static void compositor_radii(const struct compositor_frame *frames, unsigned int count, int *radii)
{
        for (unsigned int at = 0; at < count; at++)
                radii[at] = compositor_radius(&frames[at]);
}

/*
        The pieces of a damage's frames and the edges they cut the damage at, made once for the damage (compositor_plan_make),
        with each frame's radius (compositor_radii). Every band of the damage draws what lies in its rows from the plan
        (compositor_scene_plan), so none of the bands builds the list again.
*/
struct compositor_plan
{
        int box[COMPOSITOR_PIECES][4];
        unsigned char owner[COMPOSITOR_PIECES];
        unsigned int pieces;
        int edge[2 * COMPOSITOR_PIECES + 2]; // the damage's top and bottom, and every piece's top or bottom inside them, sorted
        unsigned int edges;
};

static void compositor_plan_make(struct compositor_plan *plan, const struct compositor_target *t,
                                 const struct compositor_frame *frames, unsigned int count, const int *radii)
{
        int (*box)[4] = plan->box, *edge = plan->edge;
        unsigned char *owner = plan->owner;
        unsigned int edges = 2, pieces = 0;

        edge[0] = t->y1;
        edge[1] = t->y2;

        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_frame *f = &frames[at];
                int r = radii[at], fx, fy, fw, fh, x2, y2;
                unsigned int first = pieces;

                compositor_outline(f, &fx, &fy, &fw, &fh);
                x2 = fx + fw, y2 = fy + fh;
                if (!r)
                {
                        box[pieces][0] = fx, box[pieces][1] = fy, box[pieces][2] = x2, box[pieces][3] = y2;
                        owner[pieces++] = (unsigned char)at;
                }
                else
                {
                        box[pieces][0] = fx + r, box[pieces][1] = fy, box[pieces][2] = x2 - r, box[pieces][3] = y2;
                        owner[pieces++] = (unsigned char)at;
                        box[pieces][0] = fx, box[pieces][1] = fy + r, box[pieces][2] = fx + r, box[pieces][3] = y2 - r;
                        owner[pieces++] = (unsigned char)at;
                        box[pieces][0] = x2 - r, box[pieces][1] = fy + r, box[pieces][2] = x2, box[pieces][3] = y2 - r;
                        owner[pieces++] = (unsigned char)at;
                }

                for (unsigned int piece = first; piece < pieces; piece++)
                        for (unsigned int side = 1; side < 4; side += 2)
                                if (box[piece][side] > t->y1 && box[piece][side] < t->y2)
                                {
                                        unsigned int place = edges++;

                                        while (place && edge[place - 1] > box[piece][side])
                                                place--;
                                        memory_copy(edge + place + 1, edge + place, (edges - 1 - place) * sizeof(edge[0]));
                                        edge[place] = box[piece][side];
                                }
        }
        plan->pieces = pieces;
        plan->edges = edges;
}

// The scene of a band (or slice) of a damage, from its plan: the band's edges are its top and bottom and the plan's
// that lie strictly inside them, which are the ones the band would have cut at on its own.
static void compositor_scene_plan(const struct compositor_target *t, const struct compositor_frame *frames,
                                  unsigned int count, const int *radii, const struct compositor_plan *plan)
{
        unsigned int *scratch = t->scratch ? t->scratch : compositor_corner_scratch;
        int edge[2 * COMPOSITOR_PIECES + 2];
        const int (*box)[4] = plan->box;
        const unsigned char *owner = plan->owner;
        unsigned int edges = 0, pieces = plan->pieces;

        edge[edges++] = t->y1;
        for (unsigned int k = 0; k < plan->edges; k++)
                if (plan->edge[k] > t->y1 && plan->edge[k] < t->y2)
                        edge[edges++] = plan->edge[k];
        edge[edges++] = t->y2;

        for (unsigned int band = 0; band + 1 < edges; band++)
        {
                int top = edge[band], bottom = edge[band + 1];
                int span[COMPOSITOR_PIECES + 1][2];
                unsigned int spans = 1;

                if (bottom <= top)
                        continue;

                span[0][0] = t->x1;
                span[0][1] = t->x2;

                for (unsigned int at = pieces; at-- && spans;)
                {
                        if (box[at][1] >= bottom || box[at][3] <= top || box[at][2] <= box[at][0])
                                continue;

                        for (unsigned int s = 0; s < spans; s++)
                        {
                                int low = span[s][0], high = span[s][1];
                                int x1 = low > box[at][0] ? low : box[at][0];
                                int x2 = high < box[at][2] ? high : box[at][2];
                                struct compositor_target cell = *t;

                                if (x2 <= x1)
                                        continue;

                                cell.x1 = x1;
                                cell.y1 = top;
                                cell.x2 = x2;
                                cell.y2 = bottom;
                                compositor_draw_with(&cell, &frames[owner[at]], radii[owner[at]]);

                                if (low < x1 && x2 < high)
                                {
                                        span[s][1] = x1;
                                        span[spans][0] = x2;
                                        span[spans++][1] = high;
                                }
                                else if (low < x1)
                                        span[s][1] = x1;
                                else if (x2 < high)
                                        span[s][0] = x2;
                                else
                                {
                                        // Claimed whole; the last span takes its place.
                                        spans--;
                                        span[s][0] = span[spans][0];
                                        span[s][1] = span[spans][1];
                                        s--;
                                }
                        }
                }

                for (unsigned int s = 0; s < spans; s++)
                        compositor_fill(t, span[s][0], top, span[s][1] - span[s][0], bottom - top,
                                        compositor_ground);
        }

        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_frame *f = &frames[at];
                int r = radii[at], fx, fy, fw, fh;

                compositor_outline(f, &fx, &fy, &fw, &fh);
                for (int corner = 0; r && corner < 4; corner++)
                {
                        int cx = corner & 1 ? fx + fw - r : fx, cy = corner & 2 ? fy + fh - r : fy;
                        struct compositor_target square = {scratch, (unsigned long)r, t->width, t->height,
                                                           cx > t->x1 ? cx : t->x1, cy > t->y1 ? cy : t->y1,
                                                           cx + r < t->x2 ? cx + r : t->x2, cy + r < t->y2 ? cy + r : t->y2,
                                                           cx, cy};

                        if (square.x2 <= square.x1 || square.y2 <= square.y1)
                                continue;
                        compositor_fill(&square, cx, cy, r, r, compositor_ground);
                        for (unsigned int each = 0; each < count; each++)
                                compositor_draw_with(&square, &frames[each], radii[each]);
                        compositor_blit(t, cx, cy, r, r, scratch, (unsigned long)r);
                }
        }
        compositor_preview_draw(t, frames, count, radii);
}

// The scene of a damage with its frames' radii made here, for the callers that have no damage of their own.
static void compositor_scene(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count)
{
        int radii[COMPOSITOR_FRAMES];
        struct compositor_plan plan;

        compositor_radii(frames, count, radii);
        compositor_plan_make(&plan, t, frames, count, radii);
        compositor_scene_plan(t, frames, count, radii, &plan);
}

/*
        The scene of a large damage, in bands of rows on as many threads: it is
        copies, and one core copies only as fast as its own loads and stores go,
        where the rows of a damage are independent and any core's to write. Each
        band has a corner scratch of its own; the corner masks are made before
        (compositor_corner_masks_ready), so nothing the bands share is written.
        Small damage, under the pool's floor, is drawn here in one band.
*/
#define COMPOSITOR_BANDS 8

// Mapped when the first banded scene is drawn, not kept in the image: 128 KiB of the image's address space is
// what a shell under a 12 MiB cap cannot spare, in every program that links this.
static unsigned int *compositor_band_scratch;

/*
        A scene composed apart: each band a slice of COMPOSITOR_SLICE rows at
        a time, into its own rows of slices (pitch words a row), and each
        slice handed to flush while it is still in cache, which stores it to
        the card and says the rows it changed there, first to last, -1 none.
*/
#define COMPOSITOR_SLICE 16

struct compositor_apart
{
        void (*flush)(const struct compositor_target *slice, int *first, int *last);
        unsigned int *slices;
        unsigned long pitch;
};

struct compositor_banding
{
        const struct compositor_target *target;
        const struct compositor_frame *frames;
        unsigned int count, bands;
        const struct compositor_apart *apart;
        const int *radii;
        const struct compositor_plan *plan;
        int first[COMPOSITOR_BANDS], last[COMPOSITOR_BANDS];
};

static void compositor_scene_band(address_any context, positive index)
{
        struct compositor_banding *banding = (struct compositor_banding *)context;
        struct compositor_target band = *banding->target;
        long height = band.y2 - band.y1;

        band.y1 = banding->target->y1 + (int)(height * (long)index / banding->bands);
        band.y2 = banding->target->y1 + (int)(height * (long)(index + 1) / banding->bands);
        band.scratch = compositor_band_scratch
                           ? compositor_band_scratch + index * (COMPOSITOR_RADIUS_MOST * COMPOSITOR_RADIUS_MOST)
                           : 0;
        banding->first[index] = banding->last[index] = -1;
        if (band.y2 <= band.y1)
                return;
        if (!banding->apart)
        {
                compositor_scene_plan(&band, banding->frames, banding->count, banding->radii, banding->plan);
                return;
        }
        for (int top = band.y1; top < band.y2; top += COMPOSITOR_SLICE)
        {
                struct compositor_target slice = band;
                int first, last;

                slice.y1 = top;
                slice.y2 = top + COMPOSITOR_SLICE < band.y2 ? top + COMPOSITOR_SLICE : band.y2;
                slice.pixels = banding->apart->slices + index * COMPOSITOR_SLICE * banding->apart->pitch;
                slice.pitch = banding->apart->pitch;
                slice.ox = 0;
                slice.oy = top;
                compositor_scene_plan(&slice, banding->frames, banding->count, banding->radii, banding->plan);
                banding->apart->flush(&slice, &first, &last);
                if (first < 0)
                        continue;
                banding->first[index] = banding->first[index] < 0 ? first : banding->first[index];
                banding->last[index] = last;
        }
}

// The scene of a damage, in bands where it is large; composed apart (apart not none), the rows it changed on the card
// are rows[0] to rows[1], rows[0] -1 for none.
static void compositor_scene_banded(const struct compositor_target *t, const struct compositor_frame *frames,
                                    unsigned int count, const struct compositor_apart *apart, int rows[2])
{
        positive bytes = (positive)(t->x2 - t->x1) * (positive)(t->y2 - t->y1) * 4;
        positive bands = parallel_width();
        int radii[COMPOSITOR_FRAMES];
        struct compositor_plan plan;

        compositor_radii(frames, count, radii);
        compositor_plan_make(&plan, t, frames, count, radii);
        struct compositor_banding banding = {t, frames, count, 1, apart, radii, &plan, {0}, {0}};

        bands = bands < COMPOSITOR_BANDS ? bands : COMPOSITOR_BANDS;
        bands = bands < (positive)(t->y2 - t->y1) / 16 ? bands : (positive)(t->y2 - t->y1) / 16;
        if (bytes >= PARALLEL_MINIMUM_BYTES && bands >= 2 && !compositor_band_scratch)
                compositor_band_scratch = (unsigned int *)compositor_map_zeroed(
                    (unsigned long)COMPOSITOR_BANDS * COMPOSITOR_RADIUS_MOST * COMPOSITOR_RADIUS_MOST * sizeof(unsigned int));
        if (bytes < PARALLEL_MINIMUM_BYTES || bands < 2 || !compositor_band_scratch)
                compositor_scene_band(&banding, 0);
        else
        {
                banding.bands = (unsigned int)bands;
                parallel_for(compositor_scene_band, &banding, bands, bytes);
        }
        rows[0] = rows[1] = -1;
        for (unsigned int at = 0; at < banding.bands; at++)
                if (banding.first[at] >= 0)
                {
                        rows[0] = rows[0] < 0 ? banding.first[at] : rows[0];
                        rows[1] = banding.last[at];
                }
}

// Every corner mask the frames draw with, made now, so no band makes one while another reads it.
static void compositor_corner_masks_ready(const struct compositor_frame *frames, unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
        {
                int r = compositor_radius(&frames[at]), ox, oy, ow, oh;

                if (!r)
                        continue;
                compositor_outline(&frames[at], &ox, &oy, &ow, &oh);
                compositor_corner_mask(r);
                if (r > frames[at].x - ox)
                        compositor_corner_mask(r - (frames[at].x - ox));
        }
}

/*
        A finger's mark, `moonwater canvas touch on`: a faint disc of the held ink inside a ring of it a logical pixel
        wide, radius COMPOSITOR_SPOT to the scale round the point the finger is on, at alpha 255ths of itself as it fades
        once the finger is up. Over the scene and under the pointer, through the target's clip. The disc is the corner
        masks' quarter circles four times over, mirrored as a window's four corners are: the outer one is the ring's
        outside, and the one a ring's width smaller its inside and the disc. Each pixel is the target's with the disc's
        share mixed over it and the ring's over that, so the target is read here: the scene's slices and the scratch a
        mark is composed into, never the scanout.
*/
#define COMPOSITOR_SPOT 22
#define COMPOSITOR_SPOT_FILL 60  // the disc's alpha at its strongest, of 255
#define COMPOSITOR_SPOT_RING 220 // the ring's

// The radius of a mark at a scale, in device pixels: no more than the masks are made for.
static int compositor_spot_radius(int scale)
{
        int radius = COMPOSITOR_SPOT * compositor_px1(scale);

        return radius > COMPOSITOR_MASK_MOST ? COMPOSITOR_MASK_MOST : radius;
}

static void compositor_spot_draw(const struct compositor_target *t, int cx, int cy, int scale, unsigned int alpha)
{
        int radius = compositor_spot_radius(scale), ring = compositor_px1(scale), inner = radius - ring;
        int x1 = cx - radius > t->x1 ? cx - radius : t->x1, y1 = cy - radius > t->y1 ? cy - radius : t->y1;
        int x2 = cx + radius < t->x2 ? cx + radius : t->x2, y2 = cy + radius < t->y2 ? cy + radius : t->y2;
        unsigned int fill = COMPOSITOR_SPOT_FILL * alpha / 255, edge = COMPOSITOR_SPOT_RING * alpha / 255;
        const unsigned char *outer_mask, *inner_mask;

        if (!alpha || inner < 1 || x2 <= x1 || y2 <= y1)
                return;
        outer_mask = compositor_corner_mask(radius);
        inner_mask = compositor_corner_mask(inner);
        for (int py = y1; py < y2; py++)
        {
                // The pixel's place in the quarter it is mirrored into: 0 at the disc's outside, radius - 1 at its middle.
                int my = py < cy ? py - (cy - radius) : cy + radius - 1 - py;
                unsigned int *at = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);

                for (int px = x1; px < x2; px++, at++)
                {
                        int mx = px < cx ? px - (cx - radius) : cx + radius - 1 - px;
                        unsigned int out = outer_mask[my * radius + mx];
                        unsigned int in = mx >= ring && my >= ring ? inner_mask[(my - ring) * inner + (mx - ring)] : 0;
                        unsigned int pixel;

                        if (!out)
                                continue;
                        pixel = *at;
                        if (in)
                                pixel = compositor_mix(pixel, COMPOSITOR_INK_LINE_HELD, in * fill / 255);
                        if (out > in)
                                pixel = compositor_mix(pixel, COMPOSITOR_INK_LINE_HELD, (out - in) * edge / 255);
                        *at = pixel;
                }
        }
}

/* ---- end of the backend ---- */

/* ---- keymap: what a key means ---- */

/*
        What a keyboard means is the compositor's to decide, as what a
        string looks like is: a program is handed characters and not
        scancodes and a table of its own. These are the kernel desktop's
        tables, the US one and a patch over it for each of the other
        layouts, and the same rules over them -- AltGr is level three and
        Shift adds one, a level a layout leaves empty falls back the way it
        always did, and Control over a letter is the control code that
        letter names. Built on top of them, which the kernel desktop never
        had: dead keys, Caps Lock, and the characters a dead key and a
        letter make between them.

        A patch is a key and its four levels (plain, Shift, AltGr, AltGr and
        Shift); a character with CK_DEAD on is an accent that waits for the
        letter it goes on. Layouts are named as the CLI names them.

        A pure function of a state and a key: the checks drive it with no
        card and no compositor.
*/
#define CK_DEAD 0x10000u // above the Basic Multilingual Plane's last character: the presentation forms of Arabic (U+FExx) are characters too
#define CK_TABLE 99
#define CK_LEVELS 4
#define CK_102ND 86
#define CK_CAPSLOCK 58
#define CK_KEY_BACKSPACE 14
#define CK_KEY_TAB 15
#define CK_KEY_F4 62
#define CK_KEY_F9 67
#define CK_KEY_MUTE 113
#define CK_KEY_VOLUME_DOWN 114
#define CK_KEY_VOLUME_UP 115
#define CK_KEY_MICMUTE 248
#define CK_KEY_UP 103
#define CK_KEY_LEFT 105
#define CK_KEY_RIGHT 106
#define CK_KEY_DOWN 108
#define CK_KEY_ESC 1
#define CK_ACUTE 0xB4u
#define CK_GRAVE '`'
#define CK_DIAERESIS 0xA8u
#define CK_CIRCUMFLEX '^'
#define CK_TILDE '~'
#define D(c) (CK_DEAD | (c))

struct compositor_patch
{
        unsigned char code;
        unsigned int level[CK_LEVELS];
};

// Backspace is DEL and Return a line feed, as every window has read them.
static const unsigned char compositor_us[CK_TABLE][2] = {
    [1] = {27, 27},
    [2] = {'1', '!'},  [3] = {'2', '@'},  [4] = {'3', '#'},
    [5] = {'4', '$'},  [6] = {'5', '%'},  [7] = {'6', '^'},
    [8] = {'7', '&'},  [9] = {'8', '*'},  [10] = {'9', '('},
    [11] = {'0', ')'}, [12] = {'-', '_'}, [13] = {'=', '+'},
    [14] = {127, 127}, [15] = {'\t', '\t'},
    [16] = {'q', 'Q'}, [17] = {'w', 'W'}, [18] = {'e', 'E'},
    [19] = {'r', 'R'}, [20] = {'t', 'T'}, [21] = {'y', 'Y'},
    [22] = {'u', 'U'}, [23] = {'i', 'I'}, [24] = {'o', 'O'},
    [25] = {'p', 'P'}, [26] = {'[', '{'}, [27] = {']', '}'},
    [28] = {'\n', '\n'},
    [30] = {'a', 'A'}, [31] = {'s', 'S'}, [32] = {'d', 'D'},
    [33] = {'f', 'F'}, [34] = {'g', 'G'}, [35] = {'h', 'H'},
    [36] = {'j', 'J'}, [37] = {'k', 'K'}, [38] = {'l', 'L'},
    [39] = {';', ':'}, [40] = {'\'', '"'}, [41] = {'`', '~'},
    [43] = {'\\', '|'},
    [44] = {'z', 'Z'}, [45] = {'x', 'X'}, [46] = {'c', 'C'},
    [47] = {'v', 'V'}, [48] = {'b', 'B'}, [49] = {'n', 'N'},
    [50] = {'m', 'M'}, [51] = {',', '<'}, [52] = {'.', '>'},
    [53] = {'/', '?'}, [55] = {'*', '*'}, [57] = {' ', ' '},
    [71] = {'7', '7'}, [72] = {'8', '8'}, [73] = {'9', '9'},
    [74] = {'-', '-'}, [75] = {'4', '4'}, [76] = {'5', '5'},
    [77] = {'6', '6'}, [78] = {'+', '+'}, [79] = {'1', '1'},
    [80] = {'2', '2'}, [81] = {'3', '3'}, [82] = {'0', '0'},
    [83] = {'.', '.'}, [96] = {'\n', '\n'}, [98] = {'/', '/'},
};

static const struct compositor_patch compositor_uk[] = {
    {3, {'2', '"', '@'}},   {4, {'3', 0xA3, 0xA3}}, {40, {'\'', '@'}},
    {41, {'`', 0xAC, 0xA6}}, {43, {'#', '~', '\\'}}, {CK_102ND, {'\\', '|'}},
};

static const struct compositor_patch compositor_de[] = {
    {12, {0xDF, '?', '\\'}}, {13, {D(CK_ACUTE), D(CK_GRAVE)}}, {21, {'z', 'Z'}},
    {26, {0xFC, 0xDC}},      {27, {'+', '*', '~'}},            {39, {0xF6, 0xD6}},
    {40, {0xE4, 0xC4}},      {41, {D(CK_CIRCUMFLEX), 0xB0}},   {43, {'#', '\''}},
    {44, {'y', 'Y'}},        {CK_102ND, {'<', '>', '|'}},      {8, {'7', '/', '{'}},
    {9, {'8', '(', '['}},    {10, {'9', ')', ']'}},            {11, {'0', '=', '}'}},
    {16, {'q', 'Q', '@'}},   {18, {'e', 'E', 0x20AC}},
};

static const struct compositor_patch compositor_se[] = {
    {3, {'2', '"', '@'}},   {4, {'3', '#', 0xA3}},   {5, {'4', 0xA4, '$'}},
    {6, {'5', '%', 0x20AC}}, {7, {'6', '&'}},        {8, {'7', '/', '{'}},
    {9, {'8', '(', '['}},   {10, {'9', ')', ']'}},   {11, {'0', '=', '}'}},
    {12, {'+', '?', '\\'}}, {13, {D(CK_ACUTE), D(CK_GRAVE)}},
    {26, {0xE5, 0xC5}},     {27, {D(CK_DIAERESIS), D(CK_CIRCUMFLEX), D(CK_TILDE)}},
    {39, {0xF6, 0xD6}},     {40, {0xE4, 0xC4}},      {41, {0xA7, 0xBD}},
    {43, {'\'', '*'}},      {53, {'-', '_'}},        {CK_102ND, {'<', '>', '|'}},
};

// The Norwegian table has the ¨ ^ ~ key the kernel desktop's left as the US bracket.
static const struct compositor_patch compositor_no[] = {
    {3, {'2', '"', '@'}},   {4, {'3', '#', 0xA3}},   {5, {'4', 0xA4, '$'}},
    {6, {'5', '%', 0x20AC}}, {8, {'7', '/', '{'}},   {9, {'8', '(', '['}},
    {10, {'9', ')', ']'}},  {11, {'0', '=', '}'}},   {12, {'+', '?'}},
    {13, {'\\', D(CK_GRAVE), D(CK_ACUTE)}},          {26, {0xE5, 0xC5}},
    {27, {D(CK_DIAERESIS), D(CK_CIRCUMFLEX), D(CK_TILDE)}},
    {39, {0xF8, 0xD8}},     {40, {0xE6, 0xC6}},      {41, {'|', 0xA7}},
    {43, {'\'', '*'}},      {53, {'-', '_'}},        {CK_102ND, {'<', '>'}},
};

static const struct compositor_patch compositor_fr[] = {
    {2, {'&', '1'}},         {3, {0xE9, '2', '~'}},   {4, {'"', '3', '#'}},
    {5, {'\'', '4', '{'}},   {6, {'(', '5', '['}},    {7, {'-', '6', '|'}},
    {8, {0xE8, '7', '`'}},   {9, {'_', '8', '\\'}},   {10, {0xE7, '9', '^'}},
    {11, {0xE0, '0', '@'}},  {12, {')', 0xB0, ']'}},  {13, {'=', '+', '}'}},
    {16, {'a', 'A'}},        {17, {'z', 'Z'}},        {18, {'e', 'E', 0x20AC}},
    {26, {D(CK_CIRCUMFLEX), D(CK_DIAERESIS)}},        {27, {'$', 0xA3, 0xA4}},
    {30, {'q', 'Q'}},        {38, {'m', 'M'}},        {39, {0xF9, '%'}},
    {40, {'*', 0xB5}},       {44, {'w', 'W'}},        {50, {',', '?'}},
    {51, {';', '.'}},        {52, {':', '/'}},        {53, {'!', 0xA7}},
    {CK_102ND, {'<', '>'}},
};

static const struct compositor_patch compositor_es[] = {
    {3, {'2', '"', '@'}},    {4, {'3', 0xB7, '#'}},   {8, {'7', '/', '{'}},
    {11, {'0', '=', '}'}},   {12, {'\'', '?'}},       {13, {0xA1, 0xBF}},
    {26, {D(CK_GRAVE), D(CK_CIRCUMFLEX), '['}},       {27, {'+', '*', ']'}},
    {39, {0xF1, 0xD1}},      {40, {D(CK_ACUTE), D(CK_DIAERESIS), '{'}},
    {41, {0xBA, 0xAA, '\\'}}, {43, {0xE7, 0xC7, '}'}}, {CK_102ND, {'<', '>'}},
    {18, {'e', 'E', 0x20AC}},
};

static const struct compositor_patch compositor_it[] = {
    {3, {'2', '"'}},         {4, {'3', 0xA3}},        {8, {'7', '{'}},
    {9, {'8', '['}},         {10, {'9', ']'}},        {11, {'0', '}'}},
    {12, {'\'', '?'}},       {13, {0xEC, '^'}},       {26, {0xE8, 0xE9, '['}},
    {27, {'+', '*', ']'}},   {39, {0xF2, 0xE7, '@'}}, {40, {0xE0, 0xB0, '#'}},
    {41, {'\\', '|'}},       {43, {0xF9, 0xA7}},      {CK_102ND, {'<', '>'}},
    {18, {'e', 'E', 0x20AC}},
};

// Arabic: the letters of the standard 101-key layout (XKB's ara) on the keys of their place, the marks (the vowel signs, shadda, sukun) and
// the signs of the script at Shift, the digits left as they are: the person's numbers are the Western ones until they choose others.
static const struct compositor_patch compositor_ar[] = {
    {41, {0x630, 0x651}},   {16, {0x636, 0x64E}},   {17, {0x635, 0x64B}},   {18, {0x62B, 0x64F}},   {19, {0x642, 0x64C}},
    {20, {0x641, 0xFEF9}},  {21, {0x63A, 0x625}},   {22, {0x639, '`'}},     {23, {0x647, 0xF7}},    {24, {0x62E, 0xD7}},
    {25, {0x62D, 0x61B}},   {26, {0x62C, '<'}},     {27, {0x62F, '>'}},     {30, {0x634, 0x650}},   {31, {0x633, 0x64D}},
    {32, {0x64A, ']'}},     {33, {0x628, '['}},     {34, {0x644, 0xFEF7}},  {35, {0x627, 0x623}},   {36, {0x62A, 0x640}},
    {37, {0x646, 0x60C}},   {38, {0x645, '/'}},     {39, {0x643, ':'}},     {40, {0x637, '"'}},     {44, {0x626, '~'}},
    {45, {0x621, 0x652}},   {46, {0x624, '}'}},     {47, {0x631, '{'}},     {48, {0xFEFB, 0xFEF5}}, {49, {0x649, 0x622}},
    {50, {0x629, '\''}},    {51, {0x648, ','}},     {52, {0x632, '.'}},     {53, {0x638, 0x61F}},
};

// Hebrew: the standard layout (XKB's il, SI 1452); a letter has no other case, so Shift on one is the letter.
static const struct compositor_patch compositor_il[] = {
    {41, {';', '~'}},       {16, {'/', '/'}},       {17, {'\'', '\''}},     {18, {0x5E7}},          {19, {0x5E8}},
    {20, {0x5D0}},          {21, {0x5D8}},          {22, {0x5D5}},          {23, {0x5DF}},          {24, {0x5DD}},
    {25, {0x5E4}},          {26, {']', '}'}},       {27, {'[', '{'}},       {30, {0x5E9}},          {31, {0x5D3}},
    {32, {0x5D2}},          {33, {0x5DB}},          {34, {0x5E2}},          {35, {0x5D9}},          {36, {0x5D7}},
    {37, {0x5DC}},          {38, {0x5DA}},          {39, {0x5E3, ':'}},     {40, {',', '"'}},       {44, {0x5D6}},
    {45, {0x5E1}},          {46, {0x5D1}},          {47, {0x5D4}},          {48, {0x5E0}},          {49, {0x5DE}},
    {50, {0x5E6}},          {51, {0x5EA, '<'}},     {52, {0x5E5, '>'}},     {53, {'.', '?'}},
};

// Persian: the standard layout (XKB's ir): the Arabic letters of the 101-key layout, with the four Persian ones (pe, che, ke, ge) and ye.
static const struct compositor_patch compositor_ir[] = {
    {16, {0x636}},          {17, {0x635}},          {18, {0x62B}},          {19, {0x642}},          {20, {0x641}},
    {21, {0x63A}},          {22, {0x639}},          {23, {0x647}},          {24, {0x62E}},          {25, {0x62D}},
    {26, {0x62C}},          {27, {0x686}},          {30, {0x634}},          {31, {0x633}},          {32, {0x6CC}},
    {33, {0x628}},          {34, {0x644}},          {35, {0x627}},          {36, {0x62A}},          {37, {0x646}},
    {38, {0x645}},          {39, {0x6A9, ':'}},     {40, {0x6AF, '"'}},     {44, {0x638}},          {45, {0x637}},
    {46, {0x632}},          {47, {0x631}},          {48, {0x630}},          {49, {0x62F}},          {50, {0x67E}},
    {51, {0x648, '<'}},     {52, {'.', '>'}},       {53, {'/', '?'}},
};

#define CK_LAYOUT(name, table) {name, table, sizeof(table) / sizeof((table)[0])}
static const struct
{
        char name[3];
        const struct compositor_patch *patch;
        unsigned char count;
} compositor_layouts[] = {
    {"us", 0, 0},
    CK_LAYOUT("uk", compositor_uk), CK_LAYOUT("gb", compositor_uk), CK_LAYOUT("de", compositor_de),
    CK_LAYOUT("se", compositor_se), CK_LAYOUT("sv", compositor_se), CK_LAYOUT("fi", compositor_se),
    CK_LAYOUT("no", compositor_no), CK_LAYOUT("nb", compositor_no), CK_LAYOUT("dk", compositor_no),
    CK_LAYOUT("fr", compositor_fr), CK_LAYOUT("es", compositor_es), CK_LAYOUT("it", compositor_it),
    CK_LAYOUT("ar", compositor_ar), CK_LAYOUT("il", compositor_il), CK_LAYOUT("ir", compositor_ir),
};

// A dead accent and the letters it goes on, and what they make (Latin-1).
static const struct
{
        unsigned short accent;
        char bases[13];
        unsigned char made[12];
} compositor_composed[] = {
    {CK_ACUTE, "aeiouyAEIOUY", {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD}},
    {CK_GRAVE, "aeiouAEIOU", {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9}},
    {CK_DIAERESIS, "aeiouyAEIOU", {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC}},
    {CK_CIRCUMFLEX, "aeiouAEIOU", {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB}},
    {CK_TILDE, "anoANO", {0xE3, 0xF1, 0xF5, 0xC3, 0xD1, 0xD5}},
};
#undef D

// The keys that are modifiers, as the bit each is held in: the held word is
// per device, so that one keyboard letting go of Shift is not another's.
#define CK_HELD_LSHIFT 1u
#define CK_HELD_RSHIFT 2u
#define CK_HELD_LCONTROL 4u
#define CK_HELD_RCONTROL 8u
#define CK_HELD_ALT 16u
#define CK_HELD_ALTGR 32u
#define CK_HELD_LSUPER 64u
#define CK_HELD_RSUPER 128u

static unsigned int compositor_held_bit(unsigned int code)
{
        switch (code)
        {
        case 42: return CK_HELD_LSHIFT;
        case 54: return CK_HELD_RSHIFT;
        case 29: return CK_HELD_LCONTROL;
        case 97: return CK_HELD_RCONTROL;
        case 56: return CK_HELD_ALT;
        case 100: return CK_HELD_ALTGR; // the right Alt is AltGr, the right Control is Control
        case 125: return CK_HELD_LSUPER;
        case 126: return CK_HELD_RSUPER;
        }
        return 0;
}

// What is held, as the flags a key event carries.
static unsigned int compositor_held_flags(unsigned int held)
{
        return (held & (CK_HELD_LSHIFT | CK_HELD_RSHIFT) ? WINDOW_KEY_SHIFT : 0) |
               (held & (CK_HELD_LCONTROL | CK_HELD_RCONTROL) ? WINDOW_KEY_CONTROL : 0) |
               (held & CK_HELD_ALT ? WINDOW_KEY_ALT : 0) |
               (held & CK_HELD_ALTGR ? WINDOW_KEY_ALTGR : 0) |
               (held & (CK_HELD_LSUPER | CK_HELD_RSUPER) ? WINDOW_KEY_SUPER : 0);
}

struct compositor_keymap
{
        unsigned char layout;    // an index into compositor_layouts
        unsigned char caps;      // Caps Lock is on
        unsigned int dead;       // the accent that waits for a letter, or 0
        unsigned int hidden[4];  // keys whose press was taken and so whose release is
};

// A layout by name, or -1; the name it is kept under is what asked for it.
static int compositor_layout_index(const char *name)
{
        for (unsigned int at = 0; at < sizeof(compositor_layouts) / sizeof(compositor_layouts[0]); at++)
                if (name[0] == compositor_layouts[at].name[0] && name[1] == compositor_layouts[at].name[1] &&
                    !name[2])
                        return (int)at;
        return -1;
}

// The case of a letter turned over, which is what Caps Lock does to one.
static unsigned int compositor_letter_flip(unsigned int c)
{
        if ((c | 0x20) - 'a' < 26u)
                return c ^ 0x20;
        if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
                return c ^ 0x20;
        return c;
}

// What a key says at the levels the modifiers pick, with CK_DEAD on an accent.
static unsigned int compositor_keymap_character(const struct compositor_keymap *map, unsigned int code,
                                                unsigned int modifiers)
{
        const struct compositor_patch *patch = compositor_layouts[map->layout].patch;
        unsigned int count = compositor_layouts[map->layout].count;
        unsigned int level[CK_LEVELS] = {0, 0, 0, 0};
        unsigned int at, want = 0, c;

        if (code >= CK_TABLE)
                return 0;

        level[0] = compositor_us[code][0];
        level[1] = compositor_us[code][1];
        for (at = 0; at < count; at++)
                if (patch[at].code == code)
                        memory_copy(level, patch[at].level, sizeof(level));

        if (modifiers & WINDOW_KEY_ALTGR)
                want += 2;
        if (modifiers & WINDOW_KEY_SHIFT)
                want += 1;

        // A level the layout leaves empty is the one below it.
        c = level[want];
        if (!c && want)
                c = level[want & 2];
        if (!c && (modifiers & WINDOW_KEY_SHIFT))
                c = level[1];
        if (!c)
                c = level[0];
        if (!c)
                return 0;

        // A chord with Control is the key's Latin letter, whatever the layout types: Ctrl+C copies on an Arabic or a Hebrew keyboard.
        if ((modifiers & WINDOW_KEY_CONTROL) && c > 0x7f && !(c & CK_DEAD))
                c = compositor_us[code][(modifiers & WINDOW_KEY_SHIFT) != 0];

        if (map->caps && !(c & CK_DEAD))
                c = compositor_letter_flip(c);

        // Control turns a letter into the control code that letter names,
        // which is the whole of why a terminal wants a modifier at all.
        if ((modifiers & WINDOW_KEY_CONTROL) && !(c & CK_DEAD) && (c | 0x20) - 'a' < 26u)
                return c & 0x1f;

        return c;
}

// What a dead accent and a character make together, or 0 when they make nothing.
static unsigned int compositor_keymap_compose(unsigned int accent, unsigned int c)
{
        if (c == ' ' || c == accent)
                return accent;

        for (unsigned int row = 0; row < sizeof(compositor_composed) / sizeof(compositor_composed[0]); row++)
                if (compositor_composed[row].accent == accent)
                        for (unsigned int at = 0; compositor_composed[row].bases[at]; at++)
                                if (compositor_composed[row].bases[at] == (char)c)
                                        return compositor_composed[row].made[at];
        return 0;
}

/*
        A key went down (value 1), repeated (2) or came up (0), with these
        modifiers held. What comes back is how many characters it makes, which
        go out one event each: none for a key that means nothing, a release
        or a modifier; one, and with a dead accent waiting a letter that
        takes it is that letter made; two when the letter takes no accent, as
        the accent and then the letter, so that nothing typed is lost. A dead
        key makes none and is taken whole -- its release and its repeats too --
        and the answer is -1 for a key that is not to be delivered at all.
        A key that is not a character (an arrow, a function key, Escape,
        Return, Backspace) leaves a waiting accent alone or, for those that
        are control characters, ends it.
*/
static int compositor_keymap_event(struct compositor_keymap *map, unsigned int code, int value,
                                   unsigned int modifiers, unsigned int out[2])
{
        unsigned int bit = 1u << (code & 31), c, made;
        int taken = code < 128 && (map->hidden[code >> 5] & bit);

        if (code >= 128)
                return 0;

        if (!value)
        {
                map->hidden[code >> 5] &= ~bit;
                return taken ? -1 : 0;
        }

        if (taken)
                return -1;

        if (code == CK_CAPSLOCK)
        {
                if (value == 1)
                        map->caps ^= 1;
                return 0;
        }

        c = compositor_keymap_character(map, code, modifiers);
        if (!c)
                return 0;

        if (c & CK_DEAD)
        {
                unsigned int accent = c & ~CK_DEAD, was = map->dead;

                map->hidden[code >> 5] |= bit;
                map->dead = accent;
                if (!was)
                        return -1;

                // Typed, so it is a press and its release goes out with it.
                map->hidden[code >> 5] &= ~bit;
                if (was == accent)
                        map->dead = 0;
                out[0] = was; // twice the same is that accent; another types the first
                return 1;
        }

        if (!map->dead)
        {
                out[0] = c;
                return 1;
        }

        if (c < ' ' || c == 127)
        {
                map->dead = 0;
                out[0] = c;
                return 1;
        }

        made = compositor_keymap_compose(map->dead, c);
        if (made)
        {
                map->dead = 0;
                out[0] = made;
                return 1;
        }

        out[0] = map->dead;
        out[1] = c;
        map->dead = 0;
        return 2;
}

/* ---- end of the keymap ---- */

/* ---- pointer: what a hand means ---- */

/*
        Acceleration, the way a desktop does it: a mouse reports counts, not
        pixels, and the same count means aiming when the hand is slow and
        crossing the screen when it is fast, so the gain is a curve on speed.
        Below the floor it is exactly one to one, which is what makes careful
        movement land where it is aimed; above the ceiling it is flat. Fixed
        point, speed in counts a millisecond: the kernel desktop's curve.
*/
#define CP_ACCEL_ONE 1024
#define CP_ACCEL_FLOOR 1
#define CP_ACCEL_CEILING 8
#define CP_ACCEL_MAX (CP_ACCEL_ONE * 5 / 2)

static int compositor_gain(int speed)
{
        if (speed <= CP_ACCEL_FLOOR)
                return CP_ACCEL_ONE;
        if (speed >= CP_ACCEL_CEILING)
                return CP_ACCEL_MAX;
        return CP_ACCEL_ONE + (CP_ACCEL_MAX - CP_ACCEL_ONE) * (speed - CP_ACCEL_FLOOR) /
                                  (CP_ACCEL_CEILING - CP_ACCEL_FLOOR);
}

static unsigned long long compositor_isqrt(unsigned long long value)
{
        unsigned long long root = 0, bit = 1ull << 62;

        while (bit > value)
                bit >>= 2;
        for (; bit; bit >>= 2)
                if (value >= root + bit)
                {
                        value -= root + bit;
                        root = (root >> 1) + bit;
                }
                else
                        root >>= 1;
        return root;
}

// The gain of one movement of the hand, dx and dy together, in the time since the last.
static int compositor_gain_of(int dx, int dy, unsigned long long interval)
{
        unsigned long long distance, speed;

        if (interval < 1000000)
                interval = 1000000;
        distance = compositor_isqrt((unsigned long long)((long long)dx * dx) +
                                    (unsigned long long)((long long)dy * dy));
        speed = distance * 1000000 / interval;
        return compositor_gain((int)(speed < CP_ACCEL_CEILING ? speed : CP_ACCEL_CEILING));
}

// A count turned into pixels. The remainder is what stops a gain that is not
// whole from dropping the fraction of every movement: a slow drag would come
// up short of where it was aimed.
static int compositor_accel(int delta, int *remainder, int gain)
{
        long long scaled = (long long)delta * gain + *remainder;

        if (!delta)
                return 0;
        *remainder = (int)(scaled % CP_ACCEL_ONE);
        scaled /= CP_ACCEL_ONE;
        return (int)(scaled < -2147483647 ? -2147483647 : scaled > 2147483647 ? 2147483647 : scaled);
}

// A device's counts added up, held to WINDOW_COORD_MAX either way: an evdev value is any 32 bits a device (a USB one is
// anyone's) chose, and two of them, or one past a place, overflowed the int they were added into.
static int compositor_counts(int sum, long long more)
{
        long long total = (long long)sum + more;

        return (int)(total > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : total < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : total);
}

/*
        The gain the hand has chosen: the curve's at the speed, or one to one when
        `moonwater mouse acceleration off` says the curve is not wanted, times the
        sensitivity in percent (`moonwater mouse sensitivity`; none given, or 100,
        leaves the gain as it was, to the bit).
*/
static int compositor_gain_wanted(int gain, int flat, int percent)
{
        long long base = flat ? CP_ACCEL_ONE : gain;

        if (!percent || percent == 100)
                return (int)base;
        return (int)(base * percent / 100);
}

/*
        What a keyboard's repeat is made of for `moonwater keyboard repeat`: the delay
        before the first repeat in milliseconds and the period between repeats (0 stops them),
        each the setting's when it is kept (delay and rate are 0 when not, a rate of
        none being the setting's off, which is told by rate_kept) and else the
        device's own, as it was when it was opened.
*/
static int compositor_repeat_period(int rate)
{
        return rate > 0 ? (1000 + rate / 2) / rate : 0;
}

static void compositor_repeat_target(int kept, int delay, int rate, const int own[2], int into[2])
{
        into[0] = kept & 1 ? delay : own[0];
        into[1] = kept & 2 ? compositor_repeat_period(rate) : own[1];
}

// Where a tablet's report is on a size: the report held to its range, then
// the offset in it times the size over the range, rounded down.
static int compositor_tablet(int value, int minimum, int maximum, int size)
{
        long long span = (long long)maximum - minimum, offset;

        if (span <= 0)
                return 0;
        offset = (long long)(value < minimum ? minimum : value > maximum ? maximum : value) - minimum;
        return (int)(offset * size / span);
}

/*
        Shake to find it: reversing direction takes a real movement each
        time, so a slow wobble or a hand resting on the mouse is not a shake,
        and the reversals have to come inside one window or the count starts
        again.
*/
#define CP_SHAKE_WINDOW 700000000ull
#define CP_SHAKE_STEP 6
#define CP_SHAKE_REVERSALS 5

struct compositor_shake
{
        unsigned long long window;
        int direction, count;
};

static int compositor_shake(struct compositor_shake *shake, int delta, unsigned long long now)
{
        int direction;

        if (delta > -CP_SHAKE_STEP && delta < CP_SHAKE_STEP)
                return 0;

        direction = delta > 0 ? 1 : -1;
        if (now - shake->window > CP_SHAKE_WINDOW)
        {
                shake->window = now;
                shake->count = 0;
        }

        if (direction == shake->direction)
                return 0;

        shake->direction = direction;
        if (++shake->count >= CP_SHAKE_REVERSALS)
        {
                shake->count = 0;
                return 1;
        }
        return 0;
}


/*
        Cursors: one cell for every shape, so a shape change is a different
        bitmap and nothing else. Each word is a row of sixteen pixels, the
        first eight in its low byte, as the kernel desktop's are. The hotspot
        is the pixel the pointer is: the corner of the arrow and the centre of
        the resize shapes.
*/
#define CP_CURSOR_W 16
#define CP_CURSOR_H 20
#define CP_ARROW 0
#define CP_RESIZE_H 1
#define CP_RESIZE_V 2
#define CP_RESIZE_NWSE 3
#define CP_RESIZE_NESW 4
#define CP_SHAPES 5
// The cursor plane is as large as the smallest a driver makes: no shape is drawn larger than it takes.
#define CP_PLANE 64

static const unsigned short compositor_cursor_edge[CP_SHAPES][CP_CURSOR_H] = {
    {0x0080, 0x00c0, 0x00a0, 0x0090, 0x0088, 0x0084, 0x0082, 0x0081, 0x8080, 0x4080,
     0xe083, 0x0092, 0x00a9, 0x00c9, 0x8084, 0x8004, 0x8002, 0x8003, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x2008, 0x3018, 0x2828, 0xe44f, 0x0280, 0xe44f, 0x2828,
     0x3018, 0x2008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0001, 0x8002, 0x4004, 0x2008, 0xf01e, 0x8002, 0x8002, 0x8002, 0x8002, 0x8002,
     0xf01e, 0x2008, 0x4004, 0x8002, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x007f, 0x0042, 0x0044, 0x004c, 0x005a, 0x0065, 0x8442, 0x4c01, 0xb400,
     0x6400, 0x4400, 0x8400, 0xfc01, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0xfc01, 0x8400, 0x4400, 0x6400, 0xb400, 0x4c01, 0x8442, 0x0065, 0x005a,
     0x004c, 0x0044, 0x0042, 0x007f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
};

static const unsigned short compositor_cursor_fill[CP_SHAPES][CP_CURSOR_H] = {
    {0x0000, 0x0000, 0x0040, 0x0060, 0x0070, 0x0078, 0x007c, 0x007e, 0x007f, 0x807f,
     0x007c, 0x006c, 0x0046, 0x0006, 0x0003, 0x0003, 0x0001, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1010, 0x1830, 0xfc7f, 0x1830, 0x1010,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0001, 0x8003, 0xc007, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
     0x0001, 0xc007, 0x8003, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x003c, 0x0038, 0x0030, 0x0024, 0x0002, 0x0001, 0x8000, 0x4800,
     0x1800, 0x3800, 0x7800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x7800, 0x3800, 0x1800, 0x4800, 0x8000, 0x0001, 0x0002, 0x0024,
     0x0030, 0x0038, 0x003c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
};

static const signed char compositor_cursor_hot[CP_SHAPES] = {0, 7, 7, 7, 7};

// The edges of a window a hand has hold of, and the shape that says so.
#define CP_EDGE_LEFT 1u
#define CP_EDGE_RIGHT 2u
#define CP_EDGE_TOP 4u
#define CP_EDGE_BOTTOM 8u
static const unsigned char compositor_cursor_by_edges[16] = {
    [CP_EDGE_LEFT] = CP_RESIZE_H,
    [CP_EDGE_RIGHT] = CP_RESIZE_H,
    [CP_EDGE_TOP] = CP_RESIZE_V,
    [CP_EDGE_BOTTOM] = CP_RESIZE_V,
    [CP_EDGE_TOP | CP_EDGE_LEFT] = CP_RESIZE_NWSE,
    [CP_EDGE_BOTTOM | CP_EDGE_RIGHT] = CP_RESIZE_NWSE,
    [CP_EDGE_TOP | CP_EDGE_RIGHT] = CP_RESIZE_NESW,
    [CP_EDGE_BOTTOM | CP_EDGE_LEFT] = CP_RESIZE_NESW,
};

// The cell of a shape at a scale with the pointer at x, y: its top left.
static void compositor_cursor_cell(int *left, int *top, int x, int y, unsigned int shape, int scale)
{
        int hot = compositor_cursor_hot[shape < CP_SHAPES ? shape : 0] * scale;

        *left = x - hot;
        *top = y - hot;
}

// The largest whole scale of a shape the plane holds.
static int compositor_cursor_fit(int scale)
{
        int most = CP_PLANE / CP_CURSOR_H < CP_PLANE / CP_CURSOR_W ? CP_PLANE / CP_CURSOR_H : CP_PLANE / CP_CURSOR_W;

        return scale < most ? scale : most;
}

/*
        The cursor with its hotspot at x, y, in two colours that never share a
        pixel: runs of a row become rectangles, clipped to the target's damage
        like everything else it draws.
*/
static void compositor_cursor_draw(const struct compositor_target *t, int x, int y, unsigned int shape,
                                   int scale, unsigned int fill, unsigned int edge)
{
        int left, top;

        if (shape >= CP_SHAPES)
                shape = CP_ARROW;
        compositor_cursor_cell(&left, &top, x, y, shape, scale);

        for (int pass = 0; pass < 2; pass++)
                for (int row = 0; row < CP_CURSOR_H; row++)
                {
                        unsigned int bits = pass ? compositor_cursor_edge[shape][row] : compositor_cursor_fill[shape][row];

                        for (int column = 0; column < CP_CURSOR_W;)
                        {
                                int run = column;

                                while (run < CP_CURSOR_W && (bits & (run < 8 ? 0x80u >> run : 0x8000u >> (run - 8))))
                                        run++;
                                if (run == column)
                                {
                                        column++;
                                        continue;
                                }
                                compositor_fill(t, left + column * scale, top + row * scale, (run - column) * scale, scale,
                                                pass ? edge : fill);
                                column = run;
                        }
                }
}

#ifdef MOONWATER_GPU_C
// The pointer the compositor draws into the scene (compositor_cursor_draw): its runs of the shape's bitmap, fill then edge, as fills over everything.
static void compositor_gpu_cursor(int x, int y, unsigned int shape, int scale, unsigned int fill, unsigned int edge)
{
        int left, top;

        if (shape >= CP_SHAPES)
                shape = CP_ARROW;
        compositor_cursor_cell(&left, &top, x, y, shape, scale);
        for (int pass = 0; pass < 2; pass++)
                for (int row = 0; row < CP_CURSOR_H; row++)
                {
                        unsigned int bits = pass ? compositor_cursor_edge[shape][row] : compositor_cursor_fill[shape][row];

                        for (int column = 0; column < CP_CURSOR_W;)
                        {
                                int run = column;

                                while (run < CP_CURSOR_W && (bits & (run < 8 ? 0x80u >> run : 0x8000u >> (run - 8))))
                                        run++;
                                if (run == column)
                                {
                                        column++;
                                        continue;
                                }
                                compositor_gpu_rect(0, left + column * scale, top + row * scale, left + run * scale, top + (row + 1) * scale, pass ? edge : fill, 0, 0, 0);
                                column = run;
                        }
                }
}

#endif

_Static_assert(COMPOSITOR_LIT_LEFT == CP_EDGE_LEFT && COMPOSITOR_LIT_RIGHT == CP_EDGE_RIGHT && COMPOSITOR_LIT_TOP == CP_EDGE_TOP &&
                   COMPOSITOR_LIT_BOTTOM == CP_EDGE_BOTTOM,
               "a frame's lit edges are the hand's edges");

/*
        The edges of a window, x1, y1 to x2, y2 (its contents), a point is on: within grip outside them, and within in
        inside them (top_in at the top, where the window's titlebar starts), each edge by its own side; a corner where
        it is on two. With corner, a point on one edge within corner of the window's corner along it is on the corner
        too: a finger is not a pixel. 0 when it is on none, inside the window or outside the grip.
*/
static unsigned int compositor_edges_at(int px, int py, int x1, int y1, int x2, int y2, int grip, int in, int top_in,
                                        int corner)
{
        unsigned int edges = 0;

        if (px < x1 - grip || px >= x2 + grip || py < y1 - grip || py >= y2 + grip)
                return 0;
        edges |= px < x1 + in ? CP_EDGE_LEFT : px >= x2 - in ? CP_EDGE_RIGHT : 0;
        edges |= py < y1 + top_in ? CP_EDGE_TOP : py >= y2 - in ? CP_EDGE_BOTTOM : 0;
        if (corner && (edges & (CP_EDGE_LEFT | CP_EDGE_RIGHT)) && !(edges & (CP_EDGE_TOP | CP_EDGE_BOTTOM)))
                edges |= py < y1 + corner ? CP_EDGE_TOP : py >= y2 - corner ? CP_EDGE_BOTTOM : 0;
        else if (corner && (edges & (CP_EDGE_TOP | CP_EDGE_BOTTOM)) && !(edges & (CP_EDGE_LEFT | CP_EDGE_RIGHT)))
                edges |= px < x1 + corner ? CP_EDGE_LEFT : px >= x2 - corner ? CP_EDGE_RIGHT : 0;
        return edges;
}

/* ---- end of the pointer ---- */

/* ---- outputs: the screens one desktop spans ---- */

/*
        Every screen the card has connected is driven, each at its own mode,
        and the desktop is one space across them: a screen is a rectangle of
        it, x, y, width, height, and the space's top left is 0, 0. These are
        the rules of that space, each a function of the rectangles alone, so
        the checks hold them without a card:

        The arrangement. A screen the setting does not name is laid to the
        right of the ones before it, tops level, in the order the card lists
        its connectors; a named one comes first, in the setting's order, at
        the place it gives when it gives one, and dark when it says off. The
        first screen laid is the primary: the taskbar's, and where a window
        opens. The space is then moved so its top left is 0, 0.

        The pointer is on a screen and nowhere else: it crosses from one to
        the next only where their edges meet, and beside that part of an edge
        (a shorter screen's bottom, an offset one's corner, a gap) it stops,
        as at the desktop's own edge. A movement that ends off every screen
        is kept on the screen it started on; a point that came from nowhere
        (a tablet's, which spans the space) goes to the nearest point of the
        nearest screen.

        A window's screen is the one that holds the most of it, the lower
        index of two that hold as much, and the nearest when none holds any
        of it: that is the screen it is maximized and halved on, and the one
        whose taskbar lists it. A drag is aimed by the hand, so where one
        ends is read on the screen under the pointer, and only at an edge no
        other screen is against: a window let go just past a shared edge has
        crossed it, and is not arranged.
*/

// A connector's name as the kernel spells it: its type's name, a dash and its number among those of the type.
static void compositor_connector_name(unsigned int type, unsigned int number, char *into)
{
        static const char *const types[] = {"Unknown", "VGA", "DVI-I", "DVI-D", "DVI-A", "Composite", "SVIDEO",
                                            "LVDS", "Component", "DIN", "DP", "HDMI-A", "HDMI-B", "TV", "eDP",
                                            "Virtual", "DSI", "DPI", "Writeback", "SPI", "USB"};
        const char *name = type < sizeof(types) / sizeof(types[0]) ? types[type] : types[0];
        char digits[12];
        unsigned int length = 0, count = 0;

        while (name[length] && length < WINDOWS_OUTPUT_NAME - 2)
                into[length] = name[length], length++;
        into[length++] = '-';
        do
                digits[count++] = (char)('0' + number % 10), number /= 10;
        while (number && count < sizeof(digits));
        while (count && length < WINDOWS_OUTPUT_NAME - 1)
                into[length++] = digits[--count];
        into[length] = 0;
}

/*
        Whether a connector is a machine's own panel (eDP, LVDS or DSI: a laptop's, a tablet's, a handheld's), which a
        touch screen inside the machine is glued to. The card does not say which screen a touch screen is; a touch
        screen on no external bus is the first such panel's, and any other, or with no such panel, the primary's.
*/
static int compositor_builtin_panel(const char *name)
{
        return (name[0] == 'e' && name[1] == 'D' && name[2] == 'P' && name[3] == '-') ||
               (name[0] == 'L' && name[1] == 'V' && name[2] == 'D' && name[3] == 'S' && name[4] == '-') ||
               (name[0] == 'D' && name[1] == 'S' && name[2] == 'I' && name[3] == '-');
}

// A rectangle, x, y, width, height, and the point x, y: whether the one holds the other.
static int compositor_screen_holds(const int *screen, int x, int y)
{
        return x >= screen[0] && y >= screen[1] && x - screen[0] < screen[2] && y - screen[1] < screen[3];
}

// The screen a point is on, the first of the list that holds it; -1 for none.
static int compositor_screen_at(const int (*screens)[4], unsigned int count, int x, int y)
{
        for (unsigned int at = 0; at < count; at++)
                if (compositor_screen_holds(screens[at], x, y))
                        return (int)at;
        return -1;
}

// A value held to low to high, low winning where they cross: a window that does not fit keeps its top left.
static int compositor_fit(int value, int low, int high)
{
        value = value > high ? high : value;
        return value < low ? low : value;
}

// The point of a screen nearest x, y, and how far that is, squared.
static long long compositor_screen_nearest(const int *screen, int *x, int *y)
{
        int nx = compositor_fit(*x, screen[0], screen[0] + screen[2] - 1);
        int ny = compositor_fit(*y, screen[1], screen[1] + screen[3] - 1);
        long long dx = (long long)*x - nx, dy = (long long)*y - ny;

        *x = nx;
        *y = ny;
        return dx * dx + dy * dy;
}

/*
        The pointer moved from from_x, from_y to x, y: where it is, on a
        screen. A point on one is where it is; one off them all is kept on
        the screen the movement started on, at its edge; and one with no
        screen to start from goes to the nearest point of the nearest screen
        (the lower index of two as near).
*/
static void compositor_screen_keep_point(const int (*screens)[4], unsigned int count, int from_x, int from_y, int *x,
                                         int *y)
{
        int from = compositor_screen_at(screens, count, from_x, from_y), best_x = *x, best_y = *y;
        long long least = -1;

        if (!count || compositor_screen_at(screens, count, *x, *y) >= 0)
                return;
        if (from >= 0)
        {
                compositor_screen_nearest(screens[from], x, y);
                return;
        }
        for (unsigned int at = 0; at < count; at++)
        {
                int nx = *x, ny = *y;
                long long far = compositor_screen_nearest(screens[at], &nx, &ny);

                if (least < 0 || far < least)
                        least = far, best_x = nx, best_y = ny;
        }
        *x = best_x;
        *y = best_y;
}

// How much of a rectangle, x, y, width, height, lies on a screen, in pixels.
static long long compositor_screen_cover(const int *screen, int x, int y, int width, int height)
{
        long long x1 = x > screen[0] ? x : screen[0], y1 = y > screen[1] ? y : screen[1];
        long long x2 = (long long)x + width < (long long)screen[0] + screen[2] ? (long long)x + width
                                                                               : (long long)screen[0] + screen[2];
        long long y2 = (long long)y + height < (long long)screen[1] + screen[3] ? (long long)y + height
                                                                                : (long long)screen[1] + screen[3];

        return x2 > x1 && y2 > y1 ? (x2 - x1) * (y2 - y1) : 0;
}

// A window's screen: the one that holds the most of it, the lower index of two that hold as much, and with none
// holding any, the one nearest its middle.
static int compositor_screen_owner(const int (*screens)[4], unsigned int count, int x, int y, int width, int height)
{
        long long most = 0, least = -1;
        int owner = -1;

        for (unsigned int at = 0; at < count; at++)
        {
                long long cover = compositor_screen_cover(screens[at], x, y, width, height);

                if (cover > most)
                        most = cover, owner = (int)at;
        }
        if (owner >= 0 || !count)
                return owner < 0 ? 0 : owner;
        for (unsigned int at = 0; at < count; at++)
        {
                int mx = (int)(((long long)x * 2 + width) / 2), my = (int)(((long long)y * 2 + height) / 2);
                long long far = compositor_screen_nearest(screens[at], &mx, &my);

                if (least < 0 || far < least)
                        least = far, owner = (int)at;
        }
        return owner;
}

// The rectangle that holds every screen.
static void compositor_screens_box(const int (*screens)[4], unsigned int count, int *box)
{
        box[0] = box[1] = box[2] = box[3] = 0;
        for (unsigned int at = 0; at < count; at++)
        {
                int x2 = at ? box[0] + box[2] : screens[at][0] + screens[at][2];
                int y2 = at ? box[1] + box[3] : screens[at][1] + screens[at][3];

                x2 = x2 > screens[at][0] + screens[at][2] ? x2 : screens[at][0] + screens[at][2];
                y2 = y2 > screens[at][1] + screens[at][3] ? y2 : screens[at][1] + screens[at][3];
                box[0] = at && box[0] < screens[at][0] ? box[0] : screens[at][0];
                box[1] = at && box[1] < screens[at][1] ? box[1] : screens[at][1];
                box[2] = x2 - box[0];
                box[3] = y2 - box[1];
        }
}

// The line a window keeps off a span's edges: none where the window is exactly the span, its line having no room there
// (kept at its contents, the last column or row would be off the screen).
static int compositor_keep_line(int line, int size, int span)
{
        return size == span ? 0 : line;
}

/*
        A window, width by height with a line round it, kept where a person
        can reach it: on the space, all of it when it fits and its top left
        when it does not, as on one screen; and its top, the strip a press
        takes it by (grab rows of it, or all of a shorter window), wholly on
        a screen for as much of it as a press needs (grab pixels of it, or
        all of a narrower window). A window whose top is not is put on its
        screen, all of it when it fits and its top left when not.
*/
static void compositor_screen_keep_window(const int (*screens)[4], unsigned int count, int *x, int *y, int width,
                                          int height, int line, int grab)
{
        int box[4], strip = height < grab ? height : grab, need = width < grab ? width : grab, owner;
        int line_x, line_y;

        compositor_screens_box(screens, count, box);
        line_x = compositor_keep_line(line, width, box[2]);
        line_y = compositor_keep_line(line, height, box[3]);
        *x = compositor_fit(*x, box[0] + line_x, box[0] + box[2] - width - line_x);
        *y = compositor_fit(*y, box[1] + line_y, box[1] + box[3] - height - line_y);
        if (count < 2)
                return;

        for (unsigned int at = 0; at < count; at++)
        {
                const int *s = screens[at];
                long long x1 = *x > s[0] ? *x : s[0];
                long long x2 = (long long)*x + width < (long long)s[0] + s[2] ? (long long)*x + width : (long long)s[0] + s[2];

                if (*y >= s[1] && (long long)*y + strip <= (long long)s[1] + s[3] && x2 - x1 >= need)
                        return;
        }
        owner = compositor_screen_owner(screens, count, *x, *y, width, height);
        line_x = compositor_keep_line(line, width, screens[owner][2]);
        line_y = compositor_keep_line(line, height, screens[owner][3]);
        *x = compositor_fit(*x, screens[owner][0] + line_x, screens[owner][0] + screens[owner][2] - width - line_x);
        *y = compositor_fit(*y, screens[owner][1] + line_y, screens[owner][1] + screens[owner][3] - height - line_y);
}

// The screen's edges a snap may take, as bits: a window's contact with them (compositor_snap_rule) is one of these.
#define COMPOSITOR_SNAP_LEFT 1u
#define COMPOSITOR_SNAP_RIGHT 2u
#define COMPOSITOR_SNAP_TOP 4u
#define COMPOSITOR_SNAP_BOTTOM 8u

/*
        The edges of screen at that a window at box may snap to: all four, less each one another screen is against across
        the span the window and the screen share. The seam between two screens is never an edge, the other sides of a
        screen are.
*/
static unsigned int compositor_snap_free(const int (*screens)[4], unsigned int count, const int *box, int at)
{
        const int *s = screens[at];
        int top = box[1] > s[1] ? box[1] : s[1], bottom = box[1] + box[3] < s[1] + s[3] ? box[1] + box[3] : s[1] + s[3];
        int left = box[0] > s[0] ? box[0] : s[0], right = box[0] + box[2] < s[0] + s[2] ? box[0] + box[2] : s[0] + s[2];
        unsigned int free = COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT | COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM;

        for (unsigned int each = 0; each < count; each++)
        {
                const int *o = screens[each];

                if (each == (unsigned int)at)
                        continue;
                if (o[0] + o[2] == s[0] && o[1] < bottom && o[1] + o[3] > top)
                        free &= ~COMPOSITOR_SNAP_LEFT;
                if (o[0] == s[0] + s[2] && o[1] < bottom && o[1] + o[3] > top)
                        free &= ~COMPOSITOR_SNAP_RIGHT;
                if (o[1] + o[3] == s[1] && o[0] < right && o[0] + o[2] > left)
                        free &= ~COMPOSITOR_SNAP_TOP;
                if (o[1] == s[1] + s[3] && o[0] < right && o[0] + o[2] > left)
                        free &= ~COMPOSITOR_SNAP_BOTTOM;
        }
        return free;
}

// The side of one axis a snap takes, of the two that bound it (lo, hi): one touched, else the one alone near, else none.
static unsigned int compositor_snap_side(unsigned int touch, unsigned int near, unsigned int lo, unsigned int hi)
{
        if (touch & lo)
                return lo;
        if (touch & hi)
                return hi;
        if ((near & lo) && !(near & hi))
                return lo;
        if ((near & hi) && !(near & lo))
                return hi;
        return 0;
}

/*
        The snap target of a window at box (x, y, width, height), before any clamp, on screen, or COMPOSITOR_FLOATING for
        none. This reads the window's rectangle only. An edge is touched when the window's edge is at the screen's edge or
        past it (a gap of 0 or less): the first frame it is, and that frame's target is shown. A touched edge holds until it
        is edge pixels inside the screen's (the dead zone), which is held (the touched edges of the last frame). An axis the
        window spans, with both its edges touched, takes no snap. A corner is the quarter of its two edges when one of them
        is touched and the other is within corner pixels of the screen's corner: it wins over the edge that is touched. Else a
        touched top is the whole screen, and a left or right side that half. The bottom alone is no target: only its two
        corners are, so a tall window set low is not taken by the edge it rests on. free is the edges that may be snapped to
        (compositor_snap_free). touch is set to the edges touched now, for the next frame's held.
*/
static unsigned int compositor_snap_rule(const int *box, const int *screen, unsigned int free, unsigned int held, int edge, int corner,
                                         unsigned int *touch)
{
        int gap[4] = {box[0] - screen[0], screen[0] + screen[2] - (box[0] + box[2]), box[1] - screen[1],
                      screen[1] + screen[3] - (box[1] + box[3])};
        unsigned int contact = 0, near = 0, side_h, side_v;

        for (int each = 0; each < 4; each++)
        {
                unsigned int bit = 1u << each;

                if (!(free & bit))
                        continue;
                if (gap[each] <= 0 || ((held & bit) && gap[each] < edge))
                        contact |= bit;
                if (gap[each] < corner)
                        near |= bit;
        }
        if ((contact & (COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT)) == (COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT))
                contact &= ~(COMPOSITOR_SNAP_LEFT | COMPOSITOR_SNAP_RIGHT);
        if ((contact & (COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM)) == (COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM))
                contact &= ~(COMPOSITOR_SNAP_TOP | COMPOSITOR_SNAP_BOTTOM);
        *touch = contact;
        side_h = compositor_snap_side(contact, near, COMPOSITOR_SNAP_LEFT, COMPOSITOR_SNAP_RIGHT);
        side_v = compositor_snap_side(contact, near, COMPOSITOR_SNAP_TOP, COMPOSITOR_SNAP_BOTTOM);
        if (side_h && side_v && (contact & (side_h | side_v)))
        {
                if (side_v == COMPOSITOR_SNAP_TOP)
                        return side_h == COMPOSITOR_SNAP_LEFT ? COMPOSITOR_TOP_LEFT : COMPOSITOR_TOP_RIGHT;
                return side_h == COMPOSITOR_SNAP_LEFT ? COMPOSITOR_BOTTOM_LEFT : COMPOSITOR_BOTTOM_RIGHT;
        }
        if (contact & COMPOSITOR_SNAP_TOP)
                return COMPOSITOR_MAXIMIZED;
        if (contact & COMPOSITOR_SNAP_LEFT)
                return COMPOSITOR_LEFT;
        if (contact & COMPOSITOR_SNAP_RIGHT)
                return COMPOSITOR_RIGHT;
        return COMPOSITOR_FLOATING;
}

/*
        What a window arranged as how fills of a screen, as x, y, width,
        height. The halves and quarters are the screen's own halves: a right
        part starts at half the width, a bottom one at half the height, and
        the far half takes the odd pixel.
*/
static void compositor_screen_part(const int *screen, unsigned int how, int *part)
{
        int half_w = screen[2] / 2, half_h = screen[3] / 2;
        int wide = how == COMPOSITOR_LEFT || how == COMPOSITOR_RIGHT || how >= COMPOSITOR_TOP_LEFT;
        int tall = how == COMPOSITOR_BOTTOM || how >= COMPOSITOR_TOP_LEFT;
        int right = how == COMPOSITOR_RIGHT || how == COMPOSITOR_TOP_RIGHT || how == COMPOSITOR_BOTTOM_RIGHT;
        int low = how == COMPOSITOR_BOTTOM || how == COMPOSITOR_BOTTOM_LEFT || how == COMPOSITOR_BOTTOM_RIGHT;

        part[0] = screen[0] + (right ? half_w : 0);
        part[1] = screen[1] + (low ? half_h : 0);
        part[2] = !wide ? screen[2] : right ? screen[2] - half_w : half_w;
        part[3] = !tall ? screen[3] : low ? screen[3] - half_h : half_h;
}

/*
        A window width by height at x, y on the screen was, taken to the
        screen now: at the same share of the room it had to move in, across
        and down, so a screen that only moved takes its windows with it and
        one that went sends them to the same corner of another.
*/
static int compositor_carry_axis(int at, int was_at, int was_size, int now_at, int now_size, int size)
{
        long long room_was = (long long)was_size - size, room_now = (long long)now_size - size;
        long long offset = (long long)at - was_at;

        // A window as large as the room it had, or larger than the room it has, keeps its top left at the screen's.
        if (room_was <= 0 || room_now <= 0)
                return now_at;
        offset = offset < 0 ? 0 : offset > room_was ? room_was : offset;
        return now_at + (int)(offset * room_now / room_was);
}

static void compositor_screen_carry(const int *was, const int *now, int *x, int *y, int width, int height)
{
        *x = compositor_carry_axis(*x, was[0], was[2], now[0], now[2], width);
        *y = compositor_carry_axis(*y, was[1], was[3], now[1], now[3], height);
}

// The pointer's cell, size pixels square at left, top in the space, and a screen: where the cell is on that screen,
// and whether it meets it at all, which is whether that screen's cursor plane is to show it.
static int compositor_screen_cell(const int *screen, int left, int top, int size, int *x, int *y)
{
        *x = left - screen[0];
        *y = top - screen[1];
        return *x < screen[2] && *y < screen[3] && *x + size > 0 && *y + size > 0;
}

// What of a change (clips x1, y1, x2, y2 in the space) is on a screen: each clip cut to the screen, and those that miss
// it left out. Answers how many are left, into into.
static unsigned int compositor_screen_clips(const int *screen, const unsigned short (*clips)[4], unsigned int count,
                                            unsigned short (*into)[4])
{
        unsigned int kept = 0;

        for (unsigned int at = 0; at < count; at++)
        {
                int x1 = clips[at][0] > screen[0] ? clips[at][0] : screen[0];
                int y1 = clips[at][1] > screen[1] ? clips[at][1] : screen[1];
                int x2 = clips[at][2] < screen[0] + screen[2] ? clips[at][2] : screen[0] + screen[2];
                int y2 = clips[at][3] < screen[1] + screen[3] ? clips[at][3] : screen[1] + screen[3];

                if (x2 <= x1 || y2 <= y1)
                        continue;
                into[kept][0] = (unsigned short)x1, into[kept][1] = (unsigned short)y1;
                into[kept][2] = (unsigned short)x2, into[kept][3] = (unsigned short)y2;
                kept++;
        }
        return kept;
}

/* ---- end of the outputs ---- */

/*
        The DRM and evdev interface this needs, as the kernel's uapi lays it
        out. The ioctl numbers are _IOWR('d', n, size) with the size in bits
        16 to 29, and every struct is the same on the three machines.
*/
struct compositor_card_res
{
        unsigned long fbs, crtcs, connectors, encoders;
        unsigned int fb_count, crtc_count, connector_count, encoder_count;
        unsigned int min_width, max_width, min_height, max_height;
};

#define COMPOSITOR_MODE_USERDEF 32u // DRM_MODE_TYPE_USERDEF

struct compositor_mode
{
        unsigned int clock;
        unsigned short hdisplay, hsync_start, hsync_end, htotal, hskew;
        unsigned short vdisplay, vsync_start, vsync_end, vtotal, vscan;
        unsigned int vrefresh, flags, type;
        char name[32];
};

struct compositor_connector
{
        unsigned long encoders, modes, props, prop_values;
        unsigned int mode_count, prop_count, encoder_count;
        unsigned int encoder_id, connector_id, connector_type, connector_type_id;
        unsigned int connection, mm_width, mm_height, subpixel, pad;
};

struct compositor_encoder
{
        unsigned int encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
};

struct compositor_crtc
{
        unsigned long connectors;
        unsigned int connector_count, crtc_id, fb_id, x, y, gamma_size, mode_valid;
        struct compositor_mode mode;
};

struct compositor_dumb
{
        unsigned int height, width, bpp, flags, handle, pitch;
        unsigned long size;
};

struct compositor_map_dumb
{
        unsigned int handle, pad;
        unsigned long offset;
};

struct compositor_fb
{
        unsigned int fb_id, width, height, pitch, bpp, depth, handle;
};

struct compositor_dirty
{
        unsigned int fb_id, flags, colour, clip_count;
        unsigned long clips;
};

struct compositor_version
{
        int major, minor, patchlevel;
        unsigned long name_length;
        char *name;
        unsigned long date_length;
        char *date;
        unsigned long description_length;
        char *description;
};

_Static_assert(sizeof(struct compositor_version) == 64, "drm_version");

#define COMPOSITOR_VERSION COMPOSITOR_DRM(0x00u, 64u)

struct compositor_cursor
{
        unsigned int flags, crtc_id;
        int x, y;
        unsigned int width, height, handle;
};

_Static_assert(sizeof(struct compositor_card_res) == 64, "drm_mode_card_res");
_Static_assert(sizeof(struct compositor_mode) == 68, "drm_mode_modeinfo");
_Static_assert(sizeof(struct compositor_connector) == 80, "drm_mode_get_connector");
_Static_assert(sizeof(struct compositor_crtc) == 104, "drm_mode_crtc");
_Static_assert(sizeof(struct compositor_dumb) == 32, "drm_mode_create_dumb");
_Static_assert(sizeof(struct compositor_fb) == 28, "drm_mode_fb_cmd");
_Static_assert(sizeof(struct compositor_dirty) == 24, "drm_mode_fb_dirty_cmd");
_Static_assert(sizeof(struct compositor_cursor) == 28, "drm_mode_cursor");

#define COMPOSITOR_DRM(nr, size) (0xc0000000u | (size) << 16 | 0x6400u | (nr))
#define COMPOSITOR_SET_MASTER 0x641eu
#define COMPOSITOR_GETRESOURCES COMPOSITOR_DRM(0xa0u, 64u)
#define COMPOSITOR_GETCRTC COMPOSITOR_DRM(0xa1u, 104u)
#define COMPOSITOR_SETCRTC COMPOSITOR_DRM(0xa2u, 104u)
#define COMPOSITOR_CURSOR COMPOSITOR_DRM(0xa3u, 28u)
#define COMPOSITOR_GETENCODER COMPOSITOR_DRM(0xa6u, 20u)
#define COMPOSITOR_GETCONNECTOR COMPOSITOR_DRM(0xa7u, 80u)
#define COMPOSITOR_ADDFB COMPOSITOR_DRM(0xaeu, 28u)
#define COMPOSITOR_DIRTYFB COMPOSITOR_DRM(0xb1u, 24u)
#define COMPOSITOR_CREATE_DUMB COMPOSITOR_DRM(0xb2u, 32u)
#define COMPOSITOR_MAP_DUMB COMPOSITOR_DRM(0xb3u, 16u)
// A window shown straight from its buffer: its pages a dma-buf (udmabuf), imported, a framebuffer, flipped to.
#define COMPOSITOR_RMFB COMPOSITOR_DRM(0xafu, 4u)
#define COMPOSITOR_PAGE_FLIP COMPOSITOR_DRM(0xb0u, 24u)
#define COMPOSITOR_ADDFB2 COMPOSITOR_DRM(0xb8u, 104u)
#define COMPOSITOR_PRIME_FD_TO_HANDLE COMPOSITOR_DRM(0x2eu, 12u)
#define COMPOSITOR_GEM_CLOSE 0x40086409u    // DRM_IOW(0x09, struct drm_gem_close)
#define COMPOSITOR_UDMABUF_CREATE 0x40187542u // _IOW('u', 0x42, struct udmabuf_create)
#define COMPOSITOR_XRGB8888 0x34325258u
#define COMPOSITOR_FLIP_EVENT 1u
#define COMPOSITOR_EVENT_FLIP_COMPLETE 2u

struct compositor_input_event
{
        long seconds, microseconds;
        unsigned short type, code;
        int value;
};

struct compositor_absinfo
{
        int value, minimum, maximum, fuzz, flat, resolution;
};

#define COMPOSITOR_EVIOCGBIT(type, bytes) (0x80000000u | (bytes) << 16 | 0x4500u | (0x20u + (type)))
#define COMPOSITOR_EVIOCGABS(axis) (0x80184540u + (axis))
#define COMPOSITOR_EVIOCGID 0x80084502u // bus, vendor, product, version
#define COMPOSITOR_EVIOCGNAME(bytes) (0x80000000u | (bytes) << 16 | 0x4506u)
#define COMPOSITOR_EVIOCGPROP(bytes) (0x80000000u | (bytes) << 16 | 0x4509u)
#define COMPOSITOR_SYN_DROPPED 3u
#define COMPOSITOR_BTN_TOOL_FINGER 0x145u
#define COMPOSITOR_BTN_TOOL_QUINTTAP 0x148u // five fingers
#define COMPOSITOR_BTN_TOOL_DOUBLETAP 0x14du
#define COMPOSITOR_BTN_TOOL_TRIPLETAP 0x14eu
#define COMPOSITOR_BTN_TOOL_QUADTAP 0x14fu
#define COMPOSITOR_ABS_MT_POSITION_X 0x35u

/*
        A touchpad. A finger on it is a mouse, read from where the finger is against where it was at the last
        report, so the pointer goes on from where it was and not to the place on the screen the place on the
        pad would be, as a tablet's does. Two fingers on it are the wheel: up as they move up, right as they
        move right, as a mouse's notches go. A touch is no press, or every hand put on the pad would be a click:
        the pad's own button is (a clickpad's is the whole pad, pressed down), and pressed with two fingers on
        the pad it is the right button. The rates are of the hand, by the pad's resolution: a millimetre a
        finger moves is COMPOSITOR_PAD_COUNTS of a mouse's counts (half of what a 1000 dpi mouse says), which
        the pointer's curve and sensitivity then take as they take a mouse's, and a millimetre two fingers move
        is COMPOSITOR_PAD_WHEEL 120ths of a notch, a notch to six millimetres. A pad that does not say its
        resolution is taken as a hundred millimetres wide, its height in the same units.
*/
#define COMPOSITOR_PAD_COUNTS 20
#define COMPOSITOR_PAD_WHEEL 20
#define COMPOSITOR_PAD_WIDTH_MM 100

struct compositor_pad
{
        int next_x, next_y;     // where the pad says the finger is, in its units, kept from report to report
        int at_x, at_y;         // and where it was at the last report
        int moving;             // at_x and at_y are a place this touch moves from: not at its first report
        int fingers, touching;  // on the pad, as its tool keys say, and whether one touches it
        int per_mm_x, per_mm_y; // its units to a millimetre
        int left_x, left_y;     // what a move or a turn left under a whole count or 120th, times the units to a millimetre
        unsigned int clicked;   // the button the pad's press went down as
};

/*
        A touch screen: a screen the fingers are on (INPUT_PROP_DIRECT, BTN_TOUCH or slots, and no pen). It is read by
        its slots (ABS_MT_SLOT, ABS_MT_TRACKING_ID and ABS_MT_POSITION_X and Y), each a finger, and only once a report
        is over: the kernel says BTN_TOUCH before the place it made of the first finger (ABS_X and ABS_Y), so a press
        read where it is said went down where the last finger was lifted, and that place is the oldest finger's, which a
        second finger takes when the first is lifted. A screen of one finger and no slots is slot 0, BTN_TOUCH saying
        whether it is down. Each finger is its id (-1: none) and its place in the device's units; was is the id the
        last report ended with.
*/
#define COMPOSITOR_ABS_MT_SLOT 0x2fu
#define COMPOSITOR_ABS_MT_POSITION_Y 0x36u
#define COMPOSITOR_ABS_MT_TRACKING_ID 0x39u
#define COMPOSITOR_BTN_TOOL_PEN 0x140u
// The fingers a touch screen says at once that are followed.
#define UI_TOUCH_SLOTS 10

struct compositor_touch_screen
{
        int slot, slots;   // the slot the events are about, and whether it has slots at all
        int range[4];      // x from and to, y from and to, in its units
        int dropped;       // the kernel dropped events: what comes until the report's end is not to be trusted
        int id[UI_TOUCH_SLOTS], was[UI_TOUCH_SLOTS], x[UI_TOUCH_SLOTS], y[UI_TOUCH_SLOTS];
};

/*
        What the fingers on a touch screen are doing, one screen at a time (device, -1 for none). The finger that went
        down first is the pointer: its press is the left button, at once, where it went down; it moves nothing until it
        is past the slop (moved), and then it is the pointer's motion. A second finger before the first has moved makes
        it two (mode 2): the left press is taken back (a release that says it was no click) and the two are the wheel
        from their middle; lifted soon and neither moved, they are a click of the right button. Held still, the first is
        the right button (mode 3, nothing more until every finger is up). hidden: a finger was the last thing used, so
        the pointer is not shown under it.
*/
/*
        The fingers' gesture is the kit's (ui/touch.h: the modes, the slop, the hold, the wheel, the taps); here are the compositor's
        parts of it: the device it is on, the windows its events land on, and the close and hold rows.
*/
struct compositor_touch
{
        struct ui_touch gesture; // what the fingers mean (ui/touch.h)
        int device;              // the touch screen the gesture is on
        int hidden;              // a finger was the last thing used, so the pointer is not shown under it
        int closing;             // the window whose close button the finger went down on, which a tap closes
        int resumed;             // the press is being made again without the close button (compositor_touch_resume)
        int hold_row;            // the window a long press landed on, which its lift is told, or COMPOSITOR_NONE
        int holding;             // the long press's right button is being sent: its events carry WINDOW_KEY_HOLD
};

#define COMPOSITOR_EV_KEY 1u
#define COMPOSITOR_EV_REL 2u
#define COMPOSITOR_EV_ABS 3u
#define COMPOSITOR_EV_SW 5u
#define COMPOSITOR_REL_X 0u
#define COMPOSITOR_REL_Y 1u
#define COMPOSITOR_REL_WHEEL 8u
#define COMPOSITOR_REL_HWHEEL 6u
#define COMPOSITOR_REL_WHEEL_HI 11u
#define COMPOSITOR_REL_HWHEEL_HI 12u
#define COMPOSITOR_EV_SYN 0u
#define COMPOSITOR_BTN_TOUCH 0x14au
#define COMPOSITOR_EVIOCSCLOCKID 0x400445a0u
#define COMPOSITOR_EVIOCGREP 0x80084503u // the delay and the period of a keyboard's repeat, in milliseconds
#define COMPOSITOR_EVIOCSREP 0x40084503u
#define COMPOSITOR_BTN_LEFT 0x110u
#define COMPOSITOR_BTN_RIGHT 0x111u
#define COMPOSITOR_BTN_MIDDLE 0x112u
#define COMPOSITOR_BTN_TASK 0x117u
#define COMPOSITOR_KEY_A 30u
#define COMPOSITOR_KEY_POWER 116u

struct compositor_poll
{
        int handle;
        short events, returned;
};

#define COMPOSITOR_POLLIN 1
#define COMPOSITOR_POLLHUP 16

#define COMPOSITOR_INPUTS 32 // a lid, the audio jacks and a power button take one each, and a mouse plugged in later needs one
#define COMPOSITOR_WHEEL_JOIN 960 // eight notches, in 120ths: the most one wheel event joined from several holds
#define COMPOSITOR_DEVICES 64 // evdev numbers the hotplug watch can name
// How long the processor is kept out of its deep idle states after the last thing a person did.
#define COMPOSITOR_HOLD_NS 500000000ull
// A machine slept when the clock that runs through a sleep got this far ahead of the one that does
// not, and the screen is drawn again then, at half a second and at two and a half: a card's driver
// is still settling after a wake, and the later passes are the ones that land.
#define COMPOSITOR_SLEPT_NS 50000000ull
#define COMPOSITOR_WAKE_FIRST_NS 500000000ull
#define COMPOSITOR_WAKE_SECOND_NS 2000000000ull
// A drag that ends this near the edge of the output fills it or half of it. (Double presses are the kit's click count, ui_click_again.)
#define COMPOSITOR_TRAY TASKBAR_TRAY // taskbar_hit's answer for the tray

// The band at a window's edges a press resizes it by -- six pixels outside them and two inside, so a hand
// a pixel short of a line one pixel wide still has it -- and the step a new window is put down and across
// from one in its place, in logical pixels.
#define COMPOSITOR_GRIP 6
#define COMPOSITOR_GRIP_IN 2
/*
        A finger on a touch screen, in logical pixels and nanoseconds. It is wider than a pixel and shakes as it lands,
        so what it takes is wider than what a pointer takes: a window's edge is COMPOSITOR_TOUCH_GRIP outside it and
        COMPOSITOR_TOUCH_GRIP_IN inside (the top as a pointer's, where the titlebar is), a corner is UI_TARGET each way
        (compositor_corner_reach, the kit's one rule for a finger's target, ui_reach_hit) and COMPOSITOR_TOUCH_CORNER along
        either edge from it, a tab reaches the finger's UI_TARGET further toward the middle of its screen and to half the gap
        either side, and the close and the minimise button are the finger's within COMPOSITOR_TOUCH_BUTTON_SLOP of their disc
        (the window goes when the finger lifts from the X: one that moves past the slop was making the press the corner or the
        titlebar gives, and makes it). It moves nothing until it is UI_SLOP from where it went down, so a tap is a click
        where it landed and not a drag. Held UI_TOUCH_HOLD_NS without moving it is a long press: the right button,
        flagged WINDOW_KEY_HOLD, and then the finger may move on, the pointer following it, until it lifts, which ends the
        press with a release (CANCEL and HOLD) in the window it landed on. Two fingers lifted within UI_TOUCH_TAP_NS
        of the second landing, neither moved, are the right button too; two that move are the wheel, a notch to
        UI_TOUCH_NOTCH, what is under them following them.
*/
#define COMPOSITOR_TOUCH_GRIP 16
#define COMPOSITOR_TOUCH_GRIP_IN 8
#define COMPOSITOR_TOUCH_CORNER 24
// The close and the minimise button of a window, to a finger: the disc and this much more on every side, in logical pixels. The
// corner is a finger's UI_TARGET, and wins everywhere in it but the disc (compositor_corner_reach, compositor_on_close).
#define COMPOSITOR_TOUCH_BUTTON_SLOP 4
/*
        The prediction a frame ahead, for a touch screen's fingers and a tablet or pen. The mouse is not predicted: its lead is
        always zero and it is never sampled, so its pointer is its real place. The pointer is a finger's or a pen's real place
        (compositor.real_x, real_y) led by the class's lead over one frame, no further than the cap, and held on a screen. The pointer is where every consumer of motion reads it (the cursor, the windows told a motion, drags, hovers,
        snaps), so the lead lives at the root of input; a press or a release is handled at the real place (compositor.at_real),
        so a click lands where the hand was. A class with no new place for a frame is back on its real place, so a stop is not
        carried past the cap.

        The lead is a least-squares slope over the places of the last COMPOSITOR_PREDICT_WINDOW_NS, each in 1/1024 of a pixel
        (a mouse's place keeps the fraction its acceleration left) and at the time its device reported it. It is zero for a
        motion that is still (a hand that moved less than COMPOSITOR_PREDICT_STILL_LO to COMPOSITOR_PREDICT_STILL_HI
        1/1024 px a millisecond over the window, ramped), for a motion that is a wiggle (a net move under
        COMPOSITOR_PREDICT_STRAIGHT_LO thousandths of the path it took, ramped), and for an axis whose newest step turned back.
*/
#define COMPOSITOR_PREDICT_MOUSE 0
#define COMPOSITOR_PREDICT_TOUCH 1
#define COMPOSITOR_PREDICT_PEN 2
#define COMPOSITOR_PREDICTS 3
#define COMPOSITOR_PREDICT_CAP 4                  // logical pixels a lead may take a place past its newest
#define COMPOSITOR_PREDICT_FX 1024                // places and leads are in 1/1024 of a pixel
#define COMPOSITOR_PREDICT_HISTORY 16             // the newest places held per class
#define COMPOSITOR_PREDICT_WINDOW_NS 50000000ull  // the places a fit is taken over: the last 50 ms of their device times
#define COMPOSITOR_PREDICT_SPAN_NS 4000000ull     // a fit needs places spanning this much, or the batch of one read is no motion
#define COMPOSITOR_PREDICT_STILL_LO 40            // the speed, in 1/1024 px a ms over the window, below which there is no lead
#define COMPOSITOR_PREDICT_STILL_HI 80            // and the speed at which the whole lead is given
#define COMPOSITOR_PREDICT_STRAIGHT_LO 600        // the net move over the path, in thousandths, below which there is no lead
#define COMPOSITOR_PREDICT_STRAIGHT_SPAN 300      // and the net move over the path at which the whole lead is given

struct compositor_predict
{
        long long x[COMPOSITOR_PREDICT_HISTORY], y[COMPOSITOR_PREDICT_HISTORY]; // the places the class reported, in 1/1024 px
        positive when[COMPOSITOR_PREDICT_HISTORY];   // when each was reported, by its device (the event's time, or the arrival)
        positive arrived[COMPOSITOR_PREDICT_HISTORY]; // when each was read by the compositor
        unsigned int count; // how many of them are held, the newest last
        positive last;      // when the newest was read (a new place, not a repeat of one)
        long long lead_x, lead_y; // how far ahead of the newest place it is shown or delivered, in 1/1024 px; 0 for none
};

// A lead in 1/1024 px, as the whole pixel it is shown at (to the nearest).
static long long compositor_predict_px(long long fx)
{
        long long half = COMPOSITOR_PREDICT_FX / 2;

        return fx >= 0 ? (fx + half) / COMPOSITOR_PREDICT_FX : -((-fx + half) / COMPOSITOR_PREDICT_FX);
}
static void compositor_pointer_sync(void);
static void compositor_pointer_shown(void);

// How long a finger's mark fades once it is up.
#define COMPOSITOR_SPOT_FADE_NS 1000000000ull
#define COMPOSITOR_STEP 20
#define COMPOSITOR_EDGE_LEFT CP_EDGE_LEFT
#define COMPOSITOR_EDGE_RIGHT CP_EDGE_RIGHT
#define COMPOSITOR_EDGE_TOP CP_EDGE_TOP
#define COMPOSITOR_EDGE_BOTTOM CP_EDGE_BOTTOM
// The band at the top of an output that brings the grid up, and the Display map's snap band, in logical pixels; and how
// long a drag's pointer rests in it before the grid comes (a window thrown to the top is filled, and not caught by it).
#define COMPOSITOR_SNAP 8
#define COMPOSITOR_GRID_DWELL_NS (400 * UI_MS)
// The snap's dead zone, in logical pixels: a window's edge touched at its screen's edge holds until it is this far inside
// (compositor_snap_rule). A touch is at 0 px, with no band in front of it.
#define COMPOSITOR_SNAP_EDGE 8
// The snap's corner dead zone, in logical pixels: a corner whose two edges are both this near its screen's corner, with
// one of them touched, is the quarter, and wins over the edge (compositor_snap_rule).
#define COMPOSITOR_SNAP_CORNER 16
// The indicator of a shared edge: its half thickness across the edge, in logical pixels (compositor_shared_strip).
#define COMPOSITOR_SHARED_MARK 2
// How long a pointer that was shaken stays large.
#define COMPOSITOR_MAGNIFIED_NS 1200000000ull
#define COMPOSITOR_DAMAGE 8
#define COMPOSITOR_EAGAIN 11
#define COMPOSITOR_EBUSY 16
#define COMPOSITOR_EINVAL 22
#define COMPOSITOR_ENOSPC 28
#define COMPOSITOR_IN_CLOSE_WRITE 0x8u
#define COMPOSITOR_IN_CREATE 0x100u
#define COMPOSITOR_IN_MOVED_TO 0x80u
#define COMPOSITOR_NONBLOCK 0x800
#define COMPOSITOR_CLOEXEC 0x80000
// No row: no window has the keys, is under the pointer, is held.
#define COMPOSITOR_NONE (-1)
#define COMPOSITOR_OVER (-1) // damage_by: the pointer or the tabs, over every window and under none

/*
        A screen the compositor drives: its connector and the crtc that scans
        it, the mode that crtc is at, and its name. Its rectangle of the space
        is compositor.screen at the same index. The cursor plane is a crtc's,
        so what each screen's plane was given is its own: shows is the shape
        it wears (the paint it was given, 0 for none), at x, y on the screen.
*/
struct compositor_output
{
        unsigned int crtc, crtc_index, connector;
        struct compositor_mode mode;
        char name[WINDOWS_OUTPUT_NAME], model[WINDOWS_OUTPUT_MODEL];
        int scale;             // the percent this screen is drawn at: its displays line's, 100 when none
        unsigned int shows;
        int cursor_x, cursor_y;
        // The framebuffer its crtc shows: on one screen the scanout's own, and on more a framebuffer of the same buffer
        // for each, so a DIRTYFB of one screen's change commits that screen's crtc and waits on its vertical blank
        // and on no other's: one framebuffer every crtc showed held a 60 Hz screen to a 50 Hz one's pace.
        unsigned int fb;
        // The refresh rates it lists at its size (hz_count of them), and its turn in degrees with whether Canvas does
        // the turn (turn_software); the table says them to the Displays field (compositor_table_outputs).
        unsigned int hz[WINDOWS_OUTPUT_HZ], hz_count;
        unsigned int turn, turn_software;
        // The turn its displays line asks for: degrees, and turned 0 none, 1 by the card (its primary plane's property
        // turn_property, set to turn_value on turn_plane) or 2 by Canvas in software.
        unsigned int degrees, turned, turn_plane, turn_property;
        unsigned long turn_value;
};

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch; // pitch in bytes
        unsigned int fb, crtc;
        // Where a store to the card is dear: slices of the scene composed apart, and what the card holds (mirror, words a row
        // mirror_pitch; the scanout itself where the card's memory is the machine's). changed: 0 not yet asked, 1 on,
        // -1 off. mirror_stale: the card may hold anything, so the next present stores all it composes. changed_turned:
        // whether a screen was turned in software when changed was decided (a turn reads the scene whole, from memory that is
        // the machine's, so it is decided again when that changes).
        unsigned int *composing, *mirror;
        unsigned long mirror_pitch;
        int changed, mirror_stale, changed_turned;
        int width, height;   // the space every screen is a rectangle of, and the scanout that is as large
        /*
                The screens, how many there are (none listed is one screen, the whole space: a headless
                compositor, a check), and each one's rectangle of the space, x, y, width, height, in the order of
                the table's list: the primary first. layout counts the times the list changed.
        */
        struct compositor_output output[WINDOWS_OUTPUTS];
        int screen[WINDOWS_OUTPUTS][4];
        unsigned int outputs, layout;
        /*
                What every window's buffer is made for, for the session: stride the pixels a row of it (the
                widest screen connected when the compositor started) and most_w by most_h the largest a window
                may be, which is the largest screen. 0 is the space's own width and height: one screen.
        */
        int stride, most_w, most_h, room_h; // room_h: the rows a window's buffer holds
        unsigned int scanout_handle;        // the scanout's dumb buffer, given back when another takes its place
        unsigned long composing_bytes, mirror_bytes; // what compositor_changed_ready mapped, given back with it
        int space_wanted;   // the screens are to be taken again once the card has finished a flip
        int kept_x, kept_y; // where the pointer was last kept on a screen, which a movement starts from
        int absolute;       // the pointer was put by a tablet, whose point comes from no screen
        int damage[COMPOSITOR_DAMAGE][4]; // x1, y1, x2, y2: what changed, apart
        unsigned int damages;

        int signals;
        int inputs[COMPOSITOR_INPUTS];
        int absolute_x[COMPOSITOR_INPUTS][2], absolute_y[COMPOSITOR_INPUTS][2];
        unsigned int input_count;

        int watch;           // inotify on /dev/input, -1 when there is none
        int uevents;         // the kernel's uevents (compositor_uevents_read), -1 when there is no socket for them
        unsigned long devices; // the evdev numbers that are open
        int device_number[COMPOSITOR_INPUTS];

        int scale;           // device pixels a drawn one is

        /*
                The windows: the table every program maps (window.c), each row's door and wake, and the
                compositor's own columns beside it -- what it took from each row once, checked, and what
                it decided. A row is a window everywhere: the stack, the keys, the hand are rows.
        */
        struct windows *table;
        unsigned long pixels, buffer; // where the buffers are, one buffer: never the table's numbers
        /*
                A window shown straight from its buffer, with no copy: the table is then a memfd, given to
                /dev/spark by table_giver (a descriptor of the device, plus one) and linked at WINDOWS_TABLE (its
                descriptor plus one; 0 when the table is a plain file), /dev/udmabuf is open
                (plus one), and each buffer asked for is a framebuffer (scan_fb, its card handle scan_handle).
                on_screen is the framebuffer the card shows: fb, the compositor's own, or a window's, whose row
                is direct_row (-1 for none). A flip in flight (flip_pending, to
                flip_row's flip_buffer) holds back what a window is told was taken (held: a frame, plus one) until
                the card has let go of the buffer it showed before. The tabs written over a window's buffer are
                kept there with what they covered (under, a buffer's worth of the band each, patched when it is).
        */
        int table_memfd, table_giver, udmabuf, direct_row, direct_refused, flip_pending, flip_row;
        /*
                The tabs over a window shown straight, on an overlay plane where the card has one
                (compositor_overlay_ready: overlay_state 0 not yet asked, 1 found, -1 none or refused): the
                primary plane and the overlay with their property ids, two buffers of the card's the band is
                composed into (overlay_back the one drawn next, overlay_rows how many rows each holds), and
                overlay_on with overlay_rect, what the card shows of it. crtc_index is the crtc's place in the
                card's list, which a plane's possible crtcs are a mask of.
        */
        int overlay_state, overlay_on, overlay_rect[4];
        // A window on the overlay where it does not fill the output (plane_row, -1 for none): what of it the overlay
        // shows on the output (plane_rect), and the framebuffer it shows (plane_fb, 0 for none).
        int plane_row, plane_rect[4];
        unsigned int plane_fb;
        unsigned int crtc_index, primary, overlay, primary_props[11], overlay_props[11];
        unsigned int overlay_fb[2], overlay_handle[2], overlay_back, overlay_rows;
        unsigned int *overlay_pixels[2];
        unsigned long overlay_pitch, overlay_bytes; // pitch in words
        unsigned int scan_fb[WINDOWS][2], scan_handle[WINDOWS][2], on_screen, flip_buffer;
        unsigned int taken_held[WINDOWS], held_rows, dropping, patched[WINDOWS][2];
        int screen_stale; // the compositor's own screen holds nothing current: drawn whole before it is shown again
        unsigned int memfd_failed; // the step the table's memfd was refused at, said in the table's refused
        unsigned int *under[2];
        int under_rect[2][4];
        int door[WINDOWS], wake[WINDOWS];
        unsigned int woken;  // the rows to knock on at the end of the pass, a bit each
        unsigned int fit;    // the rows whose next picture of another size is asked to fit its screen (a scale was taken), a bit each
        positive fit_until;  // and until when (compositor_scale_fit)
        unsigned int seen[WINDOWS];     // the frame last taken
        // A window's solid runs (compositor_runs_note): its picture's, mapped when first read, the picture they are of (the tag of
        // its frame and buffer), how many presents composed that picture, and what became of the read: 0 not read, 1 read and used
        // by the compose, 2 read and not worth using.
        struct compositor_run_row *runs[WINDOWS];
        unsigned long runs_bytes[WINDOWS];
        unsigned int runs_tag[WINDOWS], runs_composed[WINDOWS];
        unsigned char runs_state[WINDOWS];
        unsigned int showing[WINDOWS];  // it has shown a picture: it is on the screen and has a tab
        unsigned char landing[WINDOWS]; // its first show is landing: drawn smaller, growing to its size (compositor_land)
        positive land_at[WINDOWS];      // when its landing began, at the first present that drew it; 0 until then
        // A window arranged or floated glides from the rectangle it had (compositor_glide_start): drawn between that and where
        // it is, from glide_at (0 until its first present), while glide[] is 1.
        unsigned char glide[WINDOWS];
        int glide_from[WINDOWS][4];
        positive glide_at[WINDOWS];
        // Where the window was put when its glide began: a window moved since (a hand, a placement) takes where it glides
        // from along with it. And the window the hand holds that glides all the same, plus one (0 for none): a tile torn
        // off, shrinking to its own size about the hand (compositor_float_for_drag).
        int glide_base[WINDOWS][2];
        int drag_glide;
        unsigned int made[WINDOWS];     // the order the windows first showed in, which their tabs are in
        int x[WINDOWS], y[WINDOWS];
        unsigned int w[WINDOWS], h[WINDOWS], front[WINDOWS], state[WINDOWS]; // w and h as it is shown
        unsigned int picture_w[WINDOWS], picture_h[WINDOWS]; // as its program drew it
        unsigned int asking[WINDOWS];   // shown at a size asked of it, until a picture of that size comes
        unsigned int arranged[WINDOWS]; // COMPOSITOR_FLOATING, or what it fills: all, the left half, the right
        unsigned int edges[WINDOWS];    // the edges the hand holds, until the window is the size asked for
        int far_x[WINDOWS], far_y[WINDOWS]; // where the edges it does not hold are
        int saved_x[WINDOWS], saved_y[WINDOWS]; // where it was floating, and how large
        unsigned int saved_w[WINDOWS], saved_h[WINDOWS];
        char title[WINDOWS][WINDOWS_TITLE];
        char name[WINDOWS][WINDOWS_NAME]; // what it is remembered by, taken with its first picture; none for nothing
        unsigned int moved[WINDOWS];      // the row's moved, as it was last taken
        unsigned char stack[WINDOWS];   // bottom first
        unsigned int stacked, next_made;

        // The tabs: the rows that have one in their order, what each shows, and the band drawn with the font.
        unsigned char listed[WINDOWS], marks[WINDOWS], lengths[WINDOWS];
        unsigned int listing;
        unsigned int *band;  // the band's pixels, or none: no font, and the tabs are shapes
        /*
                `taskbar display all`, a band on every screen listing that screen's windows: band_at is the band
                being drawn or asked of (screen band_at's; 0, the primary's, is band above), each other band's
                pixels are its own, and each band's marks are kept as they were listed for its frames.
        */
        int band_at;
        unsigned int *band_more[WINDOWS_OUTPUTS];
        unsigned long band_more_bytes[WINDOWS_OUTPUTS];
        unsigned char band_marks[WINDOWS_OUTPUTS][WINDOWS];
        unsigned char banded[WINDOWS]; // with a band on every screen, the screen each window's tab was last drawn on, + 1
        unsigned long band_bytes; // how much memory the band has
        unsigned int *glass; // what is under the tabs, blurred: as wide as the band and its margins, as high
        unsigned long glass_bytes;

        /*
                The windows that are glass (compositor_glass_pass): what is under each, blurred and not yet tinted,
                and its picture over that, each cut at the window's own size; the size and place the backdrop was
                made for (a width of 0: never); what of it is to be blurred again and what composed again,
                x1, y1, x2, y2 on the output and empty when x2 is not past x1; which side of lightness its tint
                goes to, and the ground it told its program. The scratch the blur is made in is shared.
        */
        unsigned int glassy[WINDOWS];
        unsigned int asks[WINDOWS]; // what the program asked of its window besides glass (WINDOW_NO_CLOSE, WINDOW_DISMISS, WINDOW_NO_TAB, WINDOW_NO_GRAB)
        unsigned int *backdrop[WINDOWS], *tinted[WINDOWS], *composed[WINDOWS];
        unsigned int *boosted[WINDOWS]; // compositor_vivid of each backdrop cell, as the cell last was
        unsigned long glass_sum[WINDOWS][3]; // per channel, the sums of boosted over the blocks: the side's mean
        int glass_blocks[WINDOWS][4]; // the first block kept of the small picture under it, across and down, and how many each way
        unsigned long glass_cut[WINDOWS];
        int glass_made[WINDOWS][4], stale[WINDOWS][4], dirty[WINDOWS][4];
        unsigned int glass_side[WINDOWS], glass_told[WINDOWS], lifted[WINDOWS];
        // The theme (`moonwater canvas theme`, its accent and motion): the word kept (0 light, 1 dark, 2 auto), and the
        // side every glass window is tinted to, 1 light and 0 dark, which the theme chooses and what is under it does not.
        unsigned int theme_word, theme_side;
        unsigned int *blur, *frost, *rise; // scratch: the tabs' small picture, a band of the scene halved, the levels risen
        unsigned long blur_bytes, frost_bytes, rise_bytes;
        positive blurred, recomposed; // pixels blurred again and composed again, all frames
        int damage_by; // what is being damaged: 0 the scene, a row and one its own picture, COMPOSITOR_OVER what lies over all
        font_face face;
        font_engine *engine;

        int focus, hover, grab, drag, resize, press; // rows, or COMPOSITOR_NONE
        struct ui_click press_click; // the presses on a window's top: a double press is the kit's click count
        struct taskbar_cells cells;  // the panel's cells: what a press on one does (taskbar_cell_press), and the tray's double press
        unsigned int settings_asked; // the times the script was asked for the settings (compositor_settings_open)
        unsigned int launcher_asked; // the times the script was asked for the launcher by its button (compositor_launcher_open)
        int drag_x, drag_y;
        int drag_moved; // the window being moved has moved: only then is where it is let go an arrangement
        // The floating rectangle the window being moved is remembered by, taken at the press: its own if it floated, else the
        // one it had before it was first arranged. A drop on a target keeps it, so a restore puts back where the window was
        // before the drag, not where it was let go against the edge, and snaps in a row keep the first one.
        int drag_keep[4];
        // What a drag or a resize took hold of, for Escape or a right press to put back (compositor_hold_cancel): the rows, a
        // bit each (a divider holds all of its windows), and each one's rectangle and what it filled at the press.
        unsigned int hold_rows;
        int hold_rect[WINDOWS][4];
        unsigned int hold_how[WINDOWS];
        // The snap assist (compositor_assist_refresh): up over a drag's target, or armed after its drop until a click or Escape;
        // the row snapped, the region it took and its screen; the thumbnails, each its row, its tile and its rectangle.
        int assist_on, assist_armed, assist_row, assist_screen, assist_count;
        unsigned int assist_how, assist_rows[COMPOSITOR_ASSIST_MAX], assist_zone[COMPOSITOR_ASSIST_MAX];
        int assist_rect[COMPOSITOR_ASSIST_MAX][4];
        // The top edge's grid (compositor_grid_at): up on a screen, and the tile the pointer is over; when a drag's pointer
        // came to rest in the top band of screen grid_since_screen, which brings it up after COMPOSITOR_GRID_DWELL_NS (0: not there).
        int grid_on, grid_screen, grid_since_screen;
        unsigned int grid_hover;
        positive grid_since;
        // The overlays (the assist and the grid) are up when overlays_up; they fade in and out (compositor_overlay_fade) from
        // overlay_from toward overlay_goal percent since overlay_at, and are drawn while they are up or fading out.
        unsigned int overlays_up, overlay_from, overlay_goal;
        positive overlay_at;
        // The window being resized, how large it was and where the hand took hold (press_x, press_y: a drag's too).
        int resize_w, resize_h, press_x, press_y;
        // A shared edge's resize (compositor_shared_begin): the window on its other side (COMPOSITOR_NONE for a resize of
        // one), the low side's and the high side's (left or top), whether the edge runs down, and the size each had.
        int shared_row, shared_lo, shared_hi, shared_vertical, shared_size[2];
        // All the windows of a divider's resize (compositor_divider), a bit each, the low side's and the high side's, and the
        // size each had across the line at the press.
        unsigned int shared_lo_set, shared_hi_set;
        int shared_sizes[WINDOWS];
        // The shared edge's indicator (compositor_mark_set): the rectangle, the window it is drawn over and the other, and all
        // the windows of its divider, a bit each.
        int mark_row, mark_with, mark[4];
        unsigned int mark_rows;
        int pointer_x, pointer_y;
        struct compositor_keymap keymap;
        // The keys whose press went to keys_to, one bit a code: their repeats and
        // their release follow the press, and a focus change ends them (see compositor_keys_release).
        unsigned char keys_down[16];
        int keys_to;
        struct
        {
                unsigned int handle[2]; // the plane's two buffers, 0 when it has none
                int back;               // the one that is not showing
                int plane_shape, plane_scale; // what the plane wears
                int soft;               // the pointer is drawn into the scanout
                int drawn, x, y;        // where the software pointer was last damaged,
                unsigned int drawn_shape;
                int drawn_scale;
                unsigned int shape;     // what the pointer wears now
                int magnified;          // it is shaken: found
                positive magnified_until;
                int magnify;            // the hand shook it since the last motion
                int shake_off;          // `moonwater canvas cursor shake off`: a shake finds nothing
                positive shake_ns;      // how long a shaken pointer stays large (0 is COMPOSITOR_MAGNIFIED_NS)
                int size, look;         // `moonwater canvas cursor`: its scale, 0 for the desktop's; 1 light, 2 dark, 0 for light too
                unsigned long paints, moves, refused; // on the plane: shapes given it, moves, and times it refused
        } cursor;
        struct compositor_shake shake;
        char driver[16];            // the card's driver, which says whether it is a real card
        struct compositor_mode mode; // the mode the crtc is on
        unsigned int connector;
        positive asleep;            // the clock that runs through a sleep less the one that does not, when last looked at
        positive wake_at;           // when the next of the redraws after a wake is due
        int wake_stage;             // 0, or the redraw after a wake that is next: 1 at half a second, 2 at two and a half
        positive motion_ts;         // when the hand's oldest movement not yet shown was reported
        int hold;                   // /dev/cpu_dma_latency, open with a zero in it while a person is at the machine, or -1
        positive hold_until;
        unsigned long holds;        // how many times it was taken
        struct { positive total, count, worst; } latency[3]; // pointer to plane, pointer to frame, key to window
        unsigned char wakeonly[COMPOSITOR_INPUTS]; // a power button or a lid: it wakes the loop and says nothing it uses
        int cycle;           // an Alt-Tab is under way: the place in the stack of the window it is on, from 1
        int raw_x, raw_y;    // what the mouse has said since its last report
        int accel_x, accel_y; // the fraction of a pixel the gain has not yet paid out
        int tablet_x, tablet_y, tablet_have; // what a tablet has said since its last report
        positive stamp;      // when the last movement was
        unsigned char hires[COMPOSITOR_INPUTS]; // the device reports its wheel in 120ths: bit 0 the vertical one, bit 1 the sideways one
        unsigned char touchpad[COMPOSITOR_INPUTS]; // the device is a touchpad, read as compositor_pad_report says
        struct compositor_pad pad[COMPOSITOR_INPUTS];
        // The device is a touch screen (compositor_touch_report), and its fingers; what they are doing; and whether the
        // press, release or motion being handled is a finger's, which hit wider and are told so (WINDOW_KEY_TOUCH).
        unsigned char touchscreen[COMPOSITOR_INPUTS];
        struct compositor_touch_screen screen_touch[COMPOSITOR_INPUTS];
        struct compositor_touch touch;
        // The prediction a frame ahead, per class (COMPOSITOR_PREDICT_*), and the classes `moonwater canvas predict` turned off.
        struct compositor_predict predict[COMPOSITOR_PREDICTS];
        unsigned char predict_off[COMPOSITOR_PREDICTS];
        // Where the hand is, before its lead (real_x, real_y), and the class that moved the pointer last (owner). The pointer
        // itself (pointer_x, pointer_y) is where that is led to, and every consumer of motion reads it; at_real is set while a
        // press is handled, so a press is at the real place.
        int real_x, real_y;
        int shown_x, shown_y; // the pointer as compositor_pointer_shown last set it: a pointer set anywhere else is the real place
        unsigned int owner;
        unsigned int at_real;
        int touching;
        int retracting; // a finger's or a pen's lead is being taken away (compositor_predict_clear): the windows are not told
        /*
                The marks of the fingers (`moonwater canvas touch on`, spots_on), one a slot of the touch screen being
                touched: where each is on the space, on 1 while its finger is down and 2 while it fades from lifted,
                its alpha, and what was damaged for it last (x, y, width; a width of 0 none).
        */
        int spots_on;
        unsigned int spots_shown, spots_fading; // a bit a slot: its mark is drawn, and it is fading
        struct
        {
                int x, y, on;
                positive lifted;
                unsigned int alpha;
                int drawn[3];
        } spot[UI_TOUCH_SLOTS];
        unsigned int *spot_scratch; // what a mark is composed over before it goes to a scanout that is only written
        // The window whose line is lit (lit_row, COMPOSITOR_NONE for none), what of it, and whether held (else hovered).
        int lit_row;
        unsigned int lit_edges, lit_held;
        // The snap preview a drag shows (compositor_preview): the row whose drag it is, and its target's rectangle, from the
        // frame the target is touched. The edges of the window's screen its rectangle touched last frame (compositor_snap_rule's
        // dead zone).
        int preview_row;
        int preview[4];
        unsigned int snap_held;
        /*
                The preview as it is drawn (compositor_preview_shown), under the window preview_draw - 1 (0 for none) and
                shaped as preview_how fills: it moves from preview_from, at preview_from_alpha percent, to the target over
                UI_WINDOW_NS once the target changes, and with no target (lost, or let go on) it fades out where it was
                drawn; preview_at is when that began, 0 until the first present draws it (compositor_preview_keep).
        */
        int preview_draw, preview_from[4], preview_fading;
        unsigned int preview_how, preview_from_alpha;
        positive preview_at;
        // What moonwater debug is told of each device, kept here for the table that is made after they are opened.
        unsigned int input_kinds[COMPOSITOR_INPUTS];
        unsigned short input_id[COMPOSITOR_INPUTS][4];
        char input_name[COMPOSITOR_INPUTS][WINDOWS_INPUT_NAME];
        int told_row, told_x, told_y; // where the pointer was last told to a window: its row and the place in its picture
        unsigned char button_down;    // the buttons (BTN_LEFT up, a bit each) a window was sent the press of
        int button_row[8];            // and the window each press went to, which its release goes to
        unsigned char held[COMPOSITOR_INPUTS]; // the modifiers each keyboard holds down
        // `moonwater keyboard repeat` and `moonwater mouse`: what the settings kept, and what each keyboard has been told.
        unsigned char repeats[COMPOSITOR_INPUTS]; // the keyboard's repeat can be set: it answered EVIOCGREP
        int repeat_own[COMPOSITOR_INPUTS][2], repeat_now[COMPOSITOR_INPUTS][2]; // delay, period: as it was, as it is
        int repeat_kept, repeat_delay, repeat_rate; // bit 1 delay, bit 2 rate kept; the delay in ms, the rate a second (0 off)
        int sensitivity, flat; // percent of the gain (0 is 100); one to one, with no curve
        int layout_given; // -l named one, and the settings are not looked at
        unsigned long painted; // pixels composed, all frames
        positive period, presented; // a frame in nanoseconds, and when the last one went out
} compositor;

// The fingers' marks (the touch section, below): whether any is drawn or fading, drawing them, and their masks.
static int compositor_spots_any(int fading);
static void compositor_spots_draw(const struct compositor_target *t);
static void compositor_spots_compose(const struct compositor_target *t, const struct compositor_frame *frames,
                                     unsigned int count);
static void compositor_spots_ready(void);

// A colour of the pointer: its fill is white and its outline black, or the other way for the dark one.
// The plane's pixels have an alpha, the scanout's none.
static unsigned int compositor_cursor_ink(int outline, unsigned int alpha)
{
        return ((compositor.cursor.look == 2) == outline ? 0xffffffu : 0u) | alpha;
}

// Whether the pointer is drawn into the scanout: when the card has no plane for it, or the plane is too
// small for the size it was asked to be.
static int compositor_turning(void);

static int compositor_cursor_in_scene(void)
{
        // A screen turned (by the card or by Canvas) has no plane that turns with it: its pointer is drawn into the scene.
        return compositor.cursor.soft || compositor_turning() ||
               (compositor.cursor.handle[0] && compositor.cursor.size > compositor_cursor_fit(compositor.cursor.size));
}

// The screens as rectangles of the space, and how many: one, the whole space, where none is listed.
static const int (*compositor_screens(unsigned int *count))[4]
{
        static int whole[1][4];

        if (compositor.outputs)
        {
                *count = compositor.outputs;
                return (const int (*)[4])compositor.screen;
        }
        whole[0][0] = whole[0][1] = 0;
        whole[0][2] = compositor.width;
        whole[0][3] = compositor.height;
        *count = 1;
        return (const int (*)[4])whole;
}

// The pixels a row of every window's buffer is, and the largest a window may be (compositor.stride, most_w and most_h).
static int compositor_stride(void)
{
        return compositor.stride ? compositor.stride : compositor.width;
}

static int compositor_most_width(void)
{
        return compositor.most_w ? compositor.most_w : compositor.width;
}

static int compositor_most_height(void)
{
        return compositor.most_h ? compositor.most_h : compositor.height;
}

/*
        The bytes of one window's buffer, for the session: rows stride pixels long, as many as the tallest screen
        connected at the start, lit or dark (room_h). A screen dark at the start (arranged off, or past what the card
        could scan out) is lit later by an arrangement, and a window may then be as tall as it is.
*/
static unsigned long compositor_buffer_room(void)
{
        int rows = compositor.room_h > compositor_most_height() ? compositor.room_h : compositor_most_height();

        return ((unsigned long)compositor_stride() * (unsigned long)rows * 4 + 4095) & ~4095ul;
}

// How many screens the desktop spans: at least one.
static int compositor_output_count(void)
{
        return compositor.outputs ? (int)compositor.outputs : 1;
}

// Screen index's rectangle of the space, as the table lists it (0 the primary); an index past the list is the primary's.
static void compositor_output_rect(int index, int *x, int *y, int *width, int *height)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        index = index >= 0 && (unsigned int)index < count ? index : 0;
        *x = screens[index][0];
        *y = screens[index][1];
        *width = screens[index][2];
        *height = screens[index][3];
}

// Screen index's connector name; "" for a screen that is not a connector (headless) or past the list.
static const char *compositor_output_name(int index)
{
        return compositor.outputs && index >= 0 && (unsigned int)index < compositor.outputs ? compositor.output[index].name
                                                                                            : "";
}

static long compositor_call(positive number, positive a, positive b, positive c,
                            positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

// Zeroed pages of their own, or null.
static void *compositor_map_zeroed(unsigned long bytes)
{
        long got = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

        return got < 0 && got > -4096 ? 0 : (void *)got;
}

/*
        What changed, as a few rectangles that do not overlap. A rectangle
        joins one it overlaps or lines up with, which costs nothing, and
        stays apart from the rest, so two windows at opposite corners are
        two small rectangles and not the screen between them. When the list
        is full the rectangle joins whichever grows least.
*/
static long compositor_area(const int *rectangle)
{
        return (long)(rectangle[2] - rectangle[0]) * (rectangle[3] - rectangle[1]);
}

static long compositor_joined(const int *a, const int *b, int *into)
{
        into[0] = a[0] < b[0] ? a[0] : b[0];
        into[1] = a[1] < b[1] ? a[1] : b[1];
        into[2] = a[2] > b[2] ? a[2] : b[2];
        into[3] = a[3] > b[3] ? a[3] : b[3];
        return compositor_area(into);
}

// A rectangle, x1, y1, x2, y2, grown to take in another; one with x2 not past x1 is none.
static void compositor_union(int *into, const int *other)
{
        if (other[2] <= other[0] || other[3] <= other[1])
                return;
        if (into[2] <= into[0] || into[3] <= into[1])
        {
                memory_copy(into, other, 4 * sizeof(int));
                return;
        }
        compositor_joined(into, other, into);
}

// A rectangle cut to another; none when they do not meet.
static void compositor_meet(int *into, const int *other)
{
        into[0] = into[0] > other[0] ? into[0] : other[0];
        into[1] = into[1] > other[1] ? into[1] : other[1];
        into[2] = into[2] < other[2] ? into[2] : other[2];
        into[3] = into[3] < other[3] ? into[3] : other[3];
        if (into[2] <= into[0] || into[3] <= into[1])
                into[0] = into[1] = into[2] = into[3] = 0;
}


// The glass's blur constants (the block, the spread and the reach) are in taskbar.h, where the band reads them too.

// The rectangle of a window's contents, x1, y1, x2, y2, on the output and in it.
static void compositor_contents_box(int row, int *box)
{
        int output[4] = {0, 0, compositor.width, compositor.height};

        box[0] = compositor.x[row];
        box[1] = compositor.y[row];
        box[2] = compositor.x[row] + (int)compositor.w[row];
        box[3] = compositor.y[row] + (int)compositor.h[row];
        compositor_meet(box, output);
}

// Where a row is in the stack, from 0 at the bottom; -1 when it is not in it.
static int compositor_stack_place(int row)
{
        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor.stack[at] == row)
                        return (int)at;
        return -1;
}

/*
        What a change does to the glass windows. A window's own picture is
        composed over its backdrop again where it changed; anything else in
        the scene -- the desktop, a window moved, raised or gone, or the
        picture of a window under it -- is blurred again under it as far as
        the blur reaches from the change, and only that. A window's picture
        changes nothing of the glass below it, and what lies over every
        window (the pointer drawn in software, the tabs) nothing of any.
*/
static void compositor_glass_touch(const int *rectangle)
{
        int reach = COMPOSITOR_GLASS_REACH;
        int by = compositor.damage_by > 0 ? compositor.damage_by - 1 : COMPOSITOR_NONE;
        int lowest = by >= 0 ? compositor_stack_place(by) : -1;

        if (compositor.damage_by == COMPOSITOR_OVER)
                return;

        for (int row = 0; row < WINDOWS; row++)
        {
                int box[4], change[4];

                if (!compositor.glassy[row] || !compositor.showing[row])
                        continue;
                compositor_contents_box(row, box);
                memory_copy(change, rectangle, sizeof(change));
                if (row == by)
                {
                        compositor_meet(change, box);
                        compositor_union(compositor.dirty[row], change);
                        continue;
                }
                if (by >= 0 && compositor_stack_place(row) < lowest)
                        continue;
                // The change itself, where it is near enough to reach the window.
                box[0] -= reach, box[1] -= reach, box[2] += reach, box[3] += reach;
                compositor_meet(change, box);
                compositor_union(compositor.stale[row], change);
        }
}

static int compositor_visible(int row);
static void compositor_frame_of(int row, struct compositor_frame *frame);
static unsigned int compositor_snap(const int *box, unsigned int *touch, int *screen);
static void compositor_assist_refresh(void);
static int compositor_overlays_up(void);
static void compositor_overlays_damage(void);
static void compositor_overlays_damage_all(void);
static int compositor_overlays_shown(void);
static void compositor_overlay_fade(unsigned int goal);
static unsigned int compositor_overlays(struct compositor_frame *frames, unsigned int at);

/*
        A change that touches a window's close button touches all of it: the X
        is the mean under the box, so a change to part of the box changes the
        ink of the rest, and a box drawn in part would be half old. The change
        grows to take the whole box in, as often as another box is touched by
        the growing.
*/
static void compositor_close_join(int *rectangle)
{
        int grown = 1;

        while (grown)
        {
                grown = 0;
                for (unsigned int at = 0; at < compositor.stacked; at++)
                {
                        struct compositor_frame frame;
                        int row = compositor.stack[at], box[4], bx, by, side, meet[4], mx, my, mside, right;

                        if (!compositor_visible(row))
                                continue;
                        compositor_frame_of(row, &frame);
                        if (!compositor_close_box(&frame, &bx, &by, &side))
                                continue;
                        // The minimise button is left of the close one: its damage is the pair's, the close button's right edge kept.
                        right = bx + side;
                        if (compositor_minimize_box(&frame, &mx, &my, &mside))
                                bx = mx;
                        box[0] = bx < 0 ? 0 : bx, box[1] = by < 0 ? 0 : by;
                        box[2] = right > compositor.width ? compositor.width : right;
                        box[3] = by + side > compositor.height ? compositor.height : by + side;
                        memory_copy(meet, rectangle, sizeof(meet));
                        compositor_meet(meet, box);
                        if (meet[2] > meet[0] && meet[3] > meet[1] && (rectangle[0] > box[0] || rectangle[1] > box[1] ||
                                                                      rectangle[2] < box[2] || rectangle[3] < box[3]))
                        {
                                compositor_union(rectangle, box);
                                grown = 1;
                        }
                }
        }
}

static void compositor_damage(int x, int y, int width, int height)
{
        int rectangle[4] = {x < 0 ? 0 : x, y < 0 ? 0 : y,
                            x + width > compositor.width ? compositor.width : x + width,
                            y + height > compositor.height ? compositor.height : y + height};
        int joined[4];
        unsigned int at = 0;

        if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
                return;

        compositor_close_join(rectangle);
        compositor_glass_touch(rectangle);

        while (at < compositor.damages)
        {
                int *other = compositor.damage[at];

                if (compositor_joined(rectangle, other, joined) <=
                    compositor_area(rectangle) + compositor_area(other))
                {
                        memory_copy(rectangle, joined, sizeof(joined));
                        memory_copy(other, compositor.damage[--compositor.damages], sizeof(joined));
                        at = 0;
                }
                else
                        at++;
        }

        if (compositor.damages == COMPOSITOR_DAMAGE)
        {
                unsigned int best = 0;
                long least = -1;

                for (at = 0; at < COMPOSITOR_DAMAGE; at++)
                {
                        long grown = compositor_joined(rectangle, compositor.damage[at], joined) -
                                     compositor_area(compositor.damage[at]);

                        if (least < 0 || grown < least)
                        {
                                least = grown;
                                best = at;
                        }
                }

                compositor_joined(rectangle, compositor.damage[best], compositor.damage[best]);
                return;
        }

        memory_copy(compositor.damage[compositor.damages++], rectangle, sizeof(rectangle));
}

// Whether a window is on the screen: it has shown a picture and is not put away.
static int compositor_visible(int row)
{
        return compositor.showing[row] && !(compositor.state[row] & WINDOW_MINIMIZED);
}

static int compositor_row_scale(int row);

// The line round a window while it floats, in device pixels, at the scale of the screen it is on; a window filling the
// output or half of it meets the output's edges with none, and square. Glass has the same line and corners: it is what is
// under it, inside them.
static int compositor_line(int row)
{
        return compositor.arranged[row] == COMPOSITOR_FLOATING ? compositor_line_px(compositor_row_scale(row)) : 0;
}

// A window filling the output, or its room: its edges are the screen's, so it has no line and square corners.
static int compositor_filling(int row)
{
        return compositor.arranged[row] == COMPOSITOR_MAXIMIZED || compositor.arranged[row] == COMPOSITOR_ROOM;
}

// Whether a window's corners are round: only a floating window's. A window arranged, filling the output or its room or
// a half or a quarter of its screen, is square (it meets the screen's edges and its neighbours', as a maximized one does).
static int compositor_rounded(int row)
{
        return compositor.arranged[row] == COMPOSITOR_FLOATING;
}

// The line a window is drawn with, at the scale of its screen: a floating window's, and a tile's (it reads as a window, and
// where it meets another tile the line is what tells them apart). A window filling the output, or its room, has none. The
// geometry keeps compositor_line, which a placement clamps to.
static int compositor_frame_line(int row)
{
        return compositor_filling(row) ? 0 : compositor_line_px(compositor_row_scale(row));
}

/*
        A window's first show lands: its picture starts at 96 percent of its size and grows to all of it over
        COMPOSITOR_LAND_NS, on the kit's --ease, about its centre. A scale only: no fade, no shadow, no outline.
        The clock starts at the first present that draws the window (compositor_land_keep), so that frame is the
        96 percent one. Reduced motion, the table's as the kit reads it, lands nothing: the window is drawn at its
        size at once. A window shown again, moved or resized does not land.
*/
#define COMPOSITOR_LAND_NS (170 * UI_MS)  // the landing, from the first present to its size
#define COMPOSITOR_LAND_SMALL UI_Q2(96)  // the size it starts at, as a part of its own (Q30)

// How much of a landing window's size is still to grow back, Q30: 4 percent before its first present, none once landed.
static b64 compositor_land_short(int row)
{
        b64 done;

        if (!compositor.landing[row])
                return 0;
        if (!compositor.land_at[row])
                return UI_ONE - COMPOSITOR_LAND_SMALL;
        done = ui_bezier_at(&ui_ease, ui_progress((b64)(clock_monotonic_nanoseconds() - compositor.land_at[row]), COMPOSITOR_LAND_NS));
        return ui_qmul(UI_ONE - COMPOSITOR_LAND_SMALL, UI_ONE - done);
}

// A landing window's frame: its rectangle less the part still to grow back, about its centre, with its picture's size.
static void compositor_land_frame(int row, struct compositor_frame *frame)
{
        b64 shrink = compositor_land_short(row);
        int dw, dh;

        if (!shrink || !compositor.w[row] || !compositor.h[row])
                return;
        dw = (int)(((b64)compositor.w[row] * shrink + UI_HALF) >> UI_Q);
        dh = (int)(((b64)compositor.h[row] * shrink + UI_HALF) >> UI_Q);
        frame->x += dw / 2;
        frame->y += dh / 2;
        frame->width -= dw;
        frame->height -= dh;
        frame->source_w = (int)compositor.w[row];
        frame->source_h = (int)compositor.h[row];
}

// The whole of a landing window's rectangle at its size: every frame of its landing lies in it.
static void compositor_land_damage(int row)
{
        int line = compositor_line(row);

        compositor_damage(compositor.x[row] - line, compositor.y[row] - line, (int)compositor.w[row] + 2 * line,
                          (int)compositor.h[row] + 2 * line);
}

// A window's first show: it lands from COMPOSITOR_LAND_SMALL, unless motion is reduced.
static void compositor_land(int row)
{
        if (!compositor.table || compositor.table->motion)
                return;
        compositor.landing[row] = 1;
        compositor.land_at[row] = 0;
        compositor_land_damage(row);
}

/*
        After a present: a window still landing is damaged again, so the next present draws it, and the one that
        lands it is drawn at its size once more. Nothing is damaged after that, and the loop goes idle.
*/
static void compositor_land_keep(void)
{
        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                positive now;

                if (!compositor.landing[row])
                        continue;
                now = clock_monotonic_nanoseconds();
                if (!compositor.land_at[row])
                        compositor.land_at[row] = now;
                if (!compositor.showing[row] || !compositor_visible(row) || now - compositor.land_at[row] >= COMPOSITOR_LAND_NS)
                        compositor.landing[row] = 0;
                compositor_land_damage(row);
        }
}

/*
        A window arranged or floated glides from the rectangle it had to the one it has, over COMPOSITOR_GLIDE_NS (the kit's
        window motion) on the kit's --ease, so the eye sees where it went. It is called once the change is made (from is what
        it was, taken before); a window still gliding starts from where it is drawn, not from where it was going, so two
        changes in a row never jump. Reduced motion, the table's as the kit reads it, glides nothing: the window is where it
        is at once. A window that is dragged does not glide, and one that stops being shown ends its glide (compositor_glide_keep).
*/
#define COMPOSITOR_GLIDE_NS UI_WINDOW_NS

static void compositor_damage_row(int row);

static int compositor_glides(void)
{
        return compositor.table && !compositor.table->motion;
}

// Where a window glides from now: where it began, moved by as much as the window was moved since (compositor.glide_base).
static void compositor_glide_origin(int row, int *from)
{
        memory_copy(from, compositor.glide_from[row], sizeof(compositor.glide_from[row]));
        from[0] += compositor.x[row] - compositor.glide_base[row][0];
        from[1] += compositor.y[row] - compositor.glide_base[row][1];
}

static void compositor_glide_damage(int row, const int *from)
{
        int line = compositor_frame_line(row), x1 = from[0] < compositor.x[row] ? from[0] : compositor.x[row],
            y1 = from[1] < compositor.y[row] ? from[1] : compositor.y[row];
        int x2 = from[0] + from[2] > compositor.x[row] + (int)compositor.w[row] ? from[0] + from[2] : compositor.x[row] + (int)compositor.w[row];
        int y2 = from[1] + from[3] > compositor.y[row] + (int)compositor.h[row] ? from[1] + from[3] : compositor.y[row] + (int)compositor.h[row];

        compositor_damage(x1 - line, y1 - line, x2 - x1 + 2 * line, y2 - y1 + 2 * line);
}

// A glide's whole span damaged: from where it glides from now to where the window is.
static void compositor_glide_damage_now(int row)
{
        int from[4];

        compositor_glide_origin(row, from);
        compositor_glide_damage(row, from);
}

// How far a glide that began at `at` is, Q30 on the kit's ease: none before its first present (at 0).
static b64 compositor_glide_done(positive at)
{
        return at ? ui_bezier_at(&ui_ease, ui_progress((b64)(clock_monotonic_nanoseconds() - at), COMPOSITOR_GLIDE_NS)) : 0;
}

// A glide from `from` to where the window is now, begun at its next present.
static void compositor_glide_begin(int row, const int *from)
{
        memory_copy(compositor.glide_from[row], from, sizeof(compositor.glide_from[row]));
        compositor.glide_base[row][0] = compositor.x[row], compositor.glide_base[row][1] = compositor.y[row];
        compositor.glide[row] = 1;
        compositor.glide_at[row] = 0;
        compositor_glide_damage(row, from);
}

static void compositor_glide_start(int row, const int *from)
{
        int drawn[4], origin[4];

        if (!compositor_glides() || row < 0 || !compositor.showing[row] || row == compositor.drag)
                return;
        // Still gliding toward from: it is drawn between where it glided from and there, and goes on from that. Where it glided
        // from moved with the window up to from, where it was put before this change (the window is already where it goes).
        if (compositor.glide[row])
        {
                b64 done = compositor_glide_done(compositor.glide_at[row]);

                memory_copy(origin, compositor.glide_from[row], sizeof(origin));
                origin[0] += from[0] - compositor.glide_base[row][0];
                origin[1] += from[1] - compositor.glide_base[row][1];
                for (int k = 0; k < 4; k++)
                        drawn[k] = origin[k] + (int)(((long long)(from[k] - origin[k]) * (long long)done) / (long long)UI_ONE);
                compositor_glide_damage(row, origin);
                from = drawn;
        }
        compositor.drag_glide = compositor.drag_glide == row + 1 ? 0 : compositor.drag_glide;
        compositor_glide_begin(row, from);
}

// A window's rectangle as it is in the frame: where it glides from, to where it is, by the ease of the time since its first present.
static void compositor_glide_frame(int row, struct compositor_frame *frame)
{
        int from[4];
        long long to[4] = {compositor.x[row], compositor.y[row], (long long)compositor.w[row], (long long)compositor.h[row]};
        long long at[4];
        b64 done = compositor_glide_done(compositor.glide_at[row]);

        compositor_glide_origin(row, from);
        for (int k = 0; k < 4; k++)
                at[k] = from[k] + ((long long)(to[k] - from[k]) * (long long)done) / (long long)UI_ONE;
        frame->x = (int)at[0], frame->y = (int)at[1], frame->width = (int)at[2], frame->height = (int)at[3];
        frame->source_w = (int)compositor.w[row], frame->source_h = (int)compositor.h[row];
}

/*
        After a present: a window still gliding is damaged again, so the next present draws it, and the one that ends it is
        drawn where it is. Nothing is damaged after that, and the loop goes idle.
*/
static void compositor_glide_keep(void)
{
        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                positive now;

                if (!compositor.glide[row])
                        continue;
                now = clock_monotonic_nanoseconds();
                if (!compositor.glide_at[row])
                        compositor.glide_at[row] = now;
                if (!compositor_visible((int)row) || (row == (unsigned int)compositor.drag && compositor.drag_glide != (int)row + 1) ||
                    now - compositor.glide_at[row] >= COMPOSITOR_GLIDE_NS)
                {
                        // Where it was is drawn again too: the last picture of it was at the glide's place, not its own.
                        compositor_glide_damage_now((int)row);
                        compositor.glide[row] = 0;
                        compositor.drag_glide = compositor.drag_glide == (int)row + 1 ? 0 : compositor.drag_glide;
                        compositor_damage_row((int)row);
                        continue;
                }
                compositor_glide_damage_now((int)row);
        }
}

/*
        The snap preview as it is drawn. It is there at full strength the frame its target is touched; a new target (an edge's
        corner) is reached from where the preview is drawn, over UI_WINDOW_NS on the kit's ease, so it moves and does not
        cut; and with no target -- lost, or let go on, the window gliding into it -- it fades out where it was drawn over the
        same time, so the window covers it as it lands. Reduced motion cuts. compositor.preview, the target, is the target
        from the first frame whatever is drawn, so a let go at any point lands on it.
*/
// The preview drawn now: its rectangle, and its strength in percent, 0 for none.
static unsigned int compositor_preview_shown(int *rect)
{
        b64 done;

        if (!compositor.preview_draw)
                return 0;
        done = compositor_glide_done(compositor.preview_at);
        if (compositor.preview_fading || compositor.preview[2] <= 0)
        {
                memory_copy(rect, compositor.preview_from, sizeof(compositor.preview_from));
                return compositor.preview_fading
                           ? (unsigned int)(compositor.preview_from_alpha -
                                            (long long)compositor.preview_from_alpha * (long long)done / (long long)UI_ONE)
                           : 0;
        }
        for (int k = 0; k < 4; k++)
                rect[k] = compositor.preview_from[k] +
                          (int)((long long)(compositor.preview[k] - compositor.preview_from[k]) * (long long)done / (long long)UI_ONE);
        return compositor.preview_from_alpha +
               (unsigned int)((long long)(100 - compositor.preview_from_alpha) * (long long)done / (long long)UI_ONE);
}

// A new target (rect, filling how) for row's preview, before compositor.preview is set to it: from where it is drawn when it
// is drawn for row, else there at once.
static void compositor_preview_enter(int row, const int *rect, unsigned int how)
{
        int drawn[4];
        unsigned int alpha = compositor_preview_shown(drawn);

        if (alpha)
                compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
        if (alpha && compositor_glides() && compositor.preview_draw == row + 1)
        {
                memory_copy(compositor.preview_from, drawn, sizeof(drawn));
                compositor.preview_from_alpha = alpha;
        }
        else
        {
                memory_copy(compositor.preview_from, rect, sizeof(compositor.preview_from));
                compositor.preview_from_alpha = 100;
        }
        compositor.preview_fading = 0;
        compositor.preview_at = 0;
        compositor.preview_draw = row + 1;
        compositor.preview_how = how;
}

// The target gone, before compositor.preview is cleared: the preview fades out where it is drawn.
static void compositor_preview_leave(void)
{
        int drawn[4];
        unsigned int alpha = compositor_preview_shown(drawn);

        if (alpha)
                compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
        if (!alpha || !compositor_glides())
        {
                compositor.preview_draw = 0;
                return;
        }
        memory_copy(compositor.preview_from, drawn, sizeof(drawn));
        compositor.preview_from_alpha = alpha;
        compositor.preview_fading = 1;
        compositor.preview_at = 0;
}

/*
        After a present: a preview still moving or fading is damaged again (where it moves from and to), its clock started at
        the present that first drew it; one that has arrived is still, and one that has faded is gone. A window that stops
        being shown takes its preview with it.
*/
static void compositor_preview_keep(void)
{
        int row = compositor.preview_draw - 1, drawn[4], *to = compositor.preview_fading || compositor.preview[2] <= 0
                                                                   ? compositor.preview_from : compositor.preview;
        int x1, y1, x2, y2;
        positive now;

        if (row < 0)
                return;
        if (!compositor_visible(row))
        {
                if (compositor_preview_shown(drawn))
                        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
                compositor.preview_draw = 0;
                return;
        }
        if (!compositor.preview_fading && compositor.preview_from_alpha == 100 &&
            !memory_compare(compositor.preview_from, compositor.preview, sizeof(compositor.preview)))
                return;
        now = clock_monotonic_nanoseconds();
        if (!compositor.preview_at)
                compositor.preview_at = now;
        x1 = to[0] < compositor.preview_from[0] ? to[0] : compositor.preview_from[0];
        y1 = to[1] < compositor.preview_from[1] ? to[1] : compositor.preview_from[1];
        x2 = to[0] + to[2] > compositor.preview_from[0] + compositor.preview_from[2] ? to[0] + to[2]
                                                                                      : compositor.preview_from[0] + compositor.preview_from[2];
        y2 = to[1] + to[3] > compositor.preview_from[1] + compositor.preview_from[3] ? to[1] + to[3]
                                                                                      : compositor.preview_from[1] + compositor.preview_from[3];
        compositor_damage(x1, y1, x2 - x1, y2 - y1);
        if (now - compositor.preview_at < UI_WINDOW_NS)
                return;
        if (compositor.preview_fading || compositor.preview[2] <= 0)
                compositor.preview_draw = 0;
        else
        {
                memory_copy(compositor.preview_from, compositor.preview, sizeof(compositor.preview));
                compositor.preview_from_alpha = 100;
                compositor.preview_at = 0;
        }
}

// The buffer a window's picture is in, where the compositor made it: every row of it the output's width.
static unsigned int *compositor_pixels(int row)
{
        return (unsigned int *)((char *)compositor.table + compositor.pixels +
                                ((unsigned long)row * 2 + compositor.front[row]) * compositor.buffer);
}

/*
        A window composed again and again is composed from its runs. What a window shows is a picture that holds until its program
        gives another (a frame), and a drag, a glide or an uncovering composes the same picture each present: the whole of it, at a
        new place. The third present that composes a picture reads its runs off it (after the compose, on the pool), and from the
        next the stretches of one colour are filled where they were copied. A picture composed once or twice, which is every
        picture of a window that scrolls, is only counted. A run row is the compositor's own: it is made from the pixels, not
        taken from the table, so it is never wrong about them, and it is dropped when the picture changes (the tag is the
        frame and the buffer), when the compositor writes into the buffer (compositor_stretch, the tabs of a direct window),
        and with the window.
*/
#define COMPOSITOR_RUNS_AFTER 3
#define COMPOSITOR_RUNS_WIDTH 4096
#define COMPOSITOR_RUNS_WORTH 30 // the percent of a picture its runs must cover for the compose to use them

static void compositor_runs_drop(int row)
{
        if (compositor.runs[row])
                compositor_call(syscall(munmap), (positive)compositor.runs[row], compositor.runs_bytes[row], 0, 0, 0, 0);
        compositor.runs[row] = 0;
        compositor.runs_bytes[row] = 0;
        compositor.runs_tag[row] = compositor.runs_composed[row] = compositor.runs_state[row] = 0;
}

// The picture is not what its runs were read from.
static void compositor_runs_forget(int row)
{
        if (row >= 0)
                compositor.runs_tag[row] = compositor.runs_composed[row] = compositor.runs_state[row] = 0;
}

// A window's run rows, mapped for the most rows a window's buffer holds; none when the map is refused.
static struct compositor_run_row *compositor_runs_of(int row)
{
        unsigned long rows = compositor_buffer_room() / ((unsigned long)compositor_stride() * 4);
        unsigned long bytes = (rows * sizeof(struct compositor_run_row) + 4095) & ~4095ul;
        long got;

        if (compositor.runs[row] && compositor.runs_bytes[row] >= bytes)
                return compositor.runs[row];
        compositor_runs_drop(row);
        got = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);
        if (got < 0 && got > -4096)
                return 0;
        compositor.runs[row] = (struct compositor_run_row *)got;
        compositor.runs_bytes[row] = bytes;
        return compositor.runs[row];
}

struct compositor_learning
{
        struct compositor_run_row *rows;
        const unsigned int *from;
        unsigned long pitch, width;
        unsigned int height, bands;
};

static void compositor_learn_band(address_any context, positive index)
{
        struct compositor_learning *learning = (struct compositor_learning *)context;
        unsigned long from = (unsigned long)learning->height * index / learning->bands;
        unsigned long to = (unsigned long)learning->height * (index + 1) / learning->bands;

        memory_runs_u32((unsigned char *)(learning->rows + from), learning->from + from * learning->pitch, learning->pitch, learning->width,
                        to - from);
}

// Whether a frame is a window's own picture at its own size, composed as it is in the table's buffer, and large enough to count.
static int compositor_runs_eligible(int row, const struct compositor_frame *frame)
{
        return !frame->builtin && frame->pixels && frame->pixels == compositor_pixels(row) && !compositor_scaled(frame) &&
               frame->width >= 64 && frame->width <= COMPOSITOR_RUNS_WIDTH && frame->height >= 16 &&
               (unsigned long)frame->width * (unsigned long)frame->height * 4 >= PARALLEL_MINIMUM_BYTES;
}

// Whether some of the damage is on the frame's rectangle: the present composes it.
static int compositor_runs_damaged(const struct compositor_frame *frame)
{
        for (unsigned int at = 0; at < compositor.damages; at++)
                if (compositor.damage[at][0] < frame->x + frame->width && compositor.damage[at][2] > frame->x &&
                    compositor.damage[at][1] < frame->y + frame->height && compositor.damage[at][3] > frame->y)
                        return 1;
        return 0;
}

// Before the compose: each window's frame takes its runs when it has them, and the presents that compose its picture are counted.
static void compositor_runs_note(struct compositor_frame *frames, const unsigned char *rows, unsigned int windows)
{
        for (unsigned int at = 0; at < windows; at++)
        {
                int row = rows[at];
                unsigned int tag;

                frames[at].runs = 0;
                if (!compositor_runs_eligible(row, &frames[at]))
                        continue;
                tag = compositor.seen[row] * 2 + (compositor.front[row] & 1u) + 1;
                if (compositor.runs_tag[row] != tag)
                {
                        compositor.runs_tag[row] = tag;
                        compositor.runs_composed[row] = compositor.runs_state[row] = 0;
                }
                if (compositor.runs_composed[row] < COMPOSITOR_RUNS_AFTER && compositor_runs_damaged(&frames[at]))
                        compositor.runs_composed[row]++;
                if (compositor.runs_state[row] == 1)
                        frames[at].runs = compositor.runs[row];
        }
}

// After the compose: the picture composed enough times is read for its runs, once, over its whole height.
static void compositor_runs_learn(const struct compositor_frame *frames, const unsigned char *rows, unsigned int windows)
{
        for (unsigned int at = 0; at < windows; at++)
        {
                int row = rows[at];
                struct compositor_run_row *runs;
                struct compositor_learning learning;
                positive bands = parallel_width();
                unsigned long covered = 0, height = (unsigned long)frames[at].height;

                if (compositor.runs_state[row] || compositor.runs_composed[row] < COMPOSITOR_RUNS_AFTER ||
                    !compositor_runs_eligible(row, &frames[at]) || !(runs = compositor_runs_of(row)))
                        continue;
                learning = (struct compositor_learning){runs, frames[at].pixels, frames[at].pitch, (unsigned long)frames[at].width,
                                                        (unsigned int)height, 1};
                bands = bands < COMPOSITOR_BANDS ? bands : COMPOSITOR_BANDS;
                bands = bands < (positive)height / 16 ? bands : (positive)height / 16;
                if (bands >= 2)
                {
                        learning.bands = (unsigned int)bands;
                        parallel_for(compositor_learn_band, &learning, bands, (positive)frames[at].width * (positive)height * 4);
                }
                else
                        compositor_learn_band(&learning, 0);
                // A picture of few runs (a photograph, a gradient) is composed as it always was: its runs there are the copy in
                // more calls.
                for (unsigned long y = 0; y < height; y++)
                        for (unsigned int k = 0; k < runs[y].count && k < COMPOSITOR_RUN_ROW; k++)
                                covered += runs[y].run[k].x1 - runs[y].run[k].x0;
                compositor.runs_state[row] = covered * 100 >= (unsigned long)frames[at].width * height * COMPOSITOR_RUNS_WORTH ? 1 : 2;
        }
}

/*
        A picture shown at a size it was not drawn at: past what its program
        drew, its last column again to the right and its last row again
        below, and short of it, cut. A size the compositor asked for is shown
        the moment it is asked -- the edge under the hand, a window filling
        the output -- and the program's picture at that size takes its place
        when it comes; for a terminal its last column and row are its paper,
        so what fills the gap is what will be there. Written into the buffer
        the window shows, which its program does not draw into: it draws into
        the other until this one is let go, and every buffer is the output's
        size, so there is room.
*/
static void compositor_stretch(int row)
{
        unsigned int *pixels = compositor_pixels(row);
        unsigned int drawn_w = compositor.picture_w[row], drawn_h = compositor.picture_h[row];
        unsigned int width = compositor.w[row], height = compositor.h[row];
        unsigned long stride = (unsigned long)compositor_stride();

        if (!drawn_w || !drawn_h)
                return;
        compositor_runs_forget(row);

        if (width > drawn_w)
                for (unsigned int line = 0; line < drawn_h && line < height; line++)
                        memory_fill_u32(pixels + line * stride + drawn_w, width - drawn_w, pixels[line * stride + drawn_w - 1]);
        for (unsigned int line = drawn_h; line < height; line++)
                memory_copy(pixels + line * stride, pixels + (drawn_h - 1) * stride, (positive)width * 4);
}

static int compositor_row_scale(int row);
static int compositor_output_scale(int at);

static void compositor_frame_of(int row, struct compositor_frame *frame)
{
        int line = compositor_frame_line(row), scale = compositor_row_scale(row);

        // The corners are the window's own: UI_RADIUS at its scale, whatever the line comes to (it is a pixel at the least).
        *frame = (struct compositor_frame){compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                           (int)compositor.h[row], scale, compositor_pixels(row),
                                           (unsigned long)compositor_stride(), 0, null, 0,
                                           compositor_rounded(row) ? UI_RADIUS * scale / 100 : 0, line,
                                           row == compositor.focus ? compositor_line_lit() : compositor_ink_shadow()};
        frame->close = !(compositor.asks[row] & WINDOW_NO_CLOSE);
        // A program is knocked when the scale its window is drawn at is not the one the table had: the kit lays its picture out again at it.
        if (compositor.table->pid[row] && compositor.table->window_scale[row] != (unsigned int)frame->scale)
                compositor.woken |= 1u << row;
        compositor.table->window_scale[row] = (unsigned int)frame->scale;
        compositor_land_frame(row, frame);
        if (compositor.glide[row] && !compositor.landing[row] && (row != compositor.drag || compositor.drag_glide == row + 1))
                compositor_glide_frame(row, frame);
        // The edge or corner the hand holds or is over, or all of the line of a window being moved.
        if (row == compositor.lit_row && line)
        {
                frame->lit = compositor.lit_edges;
                frame->lit_ink = compositor.lit_held ? COMPOSITOR_INK_LINE_HELD : COMPOSITOR_INK_LINE_HOVER;
        }
        // The snap preview is the dragged window's, carried here for compositor_preview_lift to take over every window: where
        // it is drawn now (compositor_preview_shown), shaped as
        // the window will be there -- square, a tile with the window's line, filling the screen with none -- at the scale of
        // the screen it is on, its line the tint's colour at COMPOSITOR_SNAP_LINE percent of its strength (compositor_scene).
        if (row == compositor.preview_draw - 1)
        {
                int rect[4];
                unsigned int alpha = compositor_preview_shown(rect), ink = compositor_snap_ink(), a, edge;

                if (alpha && rect[2] > 0 && rect[3] > 0)
                {
                        unsigned int count;
                        const int (*screens)[4] = compositor_screens(&count);
                        int on = compositor_screen_owner(screens, count, rect[0], rect[1], rect[2], rect[3]);
                        int scale = compositor_output_scale(on) > 0 ? compositor_output_scale(on) : compositor.scale;
                        int fills = compositor.preview_how == COMPOSITOR_MAXIMIZED || compositor.preview_how == COMPOSITOR_ROOM;

                        a = (ink >> 24) * alpha / 100;
                        edge = (ink >> 24) * COMPOSITOR_SNAP_LINE / 100 > 255 ? 255 : (ink >> 24) * COMPOSITOR_SNAP_LINE / 100;
                        memory_copy(frame->preview, rect, sizeof(rect));
                        frame->preview_ink = (ink & 0xffffffu) | a << 24;
                        frame->preview_radius = 0;
                        frame->preview_line = fills ? 0 : compositor_line_px(scale);
                        frame->preview_edge = fills ? 0 : (ink & 0xffffffu) | (edge * alpha / 100) << 24;
                }
        }
        if (row == compositor.mark_row && compositor.mark[2] > 0)
        {
                memory_copy(frame->mark, compositor.mark, sizeof(frame->mark));
                frame->mark_ink = compositor_ink_selection();
        }
        // Glass is shown as its picture over its backdrop, composed at its own width.
        if (compositor.glassy[row] && compositor.composed[row] && compositor.glass_made[row][2] == (int)compositor.w[row] &&
            compositor.glass_made[row][3] == (int)compositor.h[row])
        {
                frame->pixels = compositor.composed[row];
                frame->pitch = compositor.w[row];
        }
}

static void compositor_damage_row(int row)
{
        struct compositor_frame frame;
        int x, y, width, height;

        if (row < 0 || !compositor.showing[row])
                return;

        compositor_frame_of(row, &frame);
        compositor_outline(&frame, &x, &y, &width, &height);
        compositor_damage(x, y, width, height);
}

// A window's line and its corners' squares, and not what is inside them: what lighting an edge draws again.
static void compositor_damage_line(int row)
{
        struct compositor_frame frame;
        int x, y, width, height, band;

        if (row < 0 || !compositor.showing[row] || !compositor_line(row))
                return;
        compositor_frame_of(row, &frame);
        compositor_outline(&frame, &x, &y, &width, &height);
        band = compositor_radius(&frame) > frame.x - x ? compositor_radius(&frame) : frame.x - x;
        compositor_damage(x, y, width, band);
        compositor_damage(x, y + height - band, width, band);
        compositor_damage(x, y + band, band, height - 2 * band);
        compositor_damage(x + width - band, y + band, band, height - 2 * band);
}

/*
        What of a window's line is lit (compositor_frame_of): the edges the hand holds or is over, all of it for a
        window being moved, held or only hovered; none with no edges. Where it changes, both lines are drawn again.
*/
static void compositor_lit(int row, unsigned int edges, unsigned int held)
{
        row = edges ? row : COMPOSITOR_NONE;
        held = row >= 0 ? held : 0;
        if (row == compositor.lit_row && (row < 0 || (edges == compositor.lit_edges && held == compositor.lit_held)))
                return;
        if (compositor.lit_edges)
                compositor_damage_line(compositor.lit_row);
        compositor.lit_row = row;
        compositor.lit_edges = row >= 0 ? edges : 0;
        compositor.lit_held = held;
        compositor_damage_line(row);
}

static int compositor_screen_of(int row);

// How many bands there are: one on every screen with `taskbar display all`, and the primary's alone otherwise.
static int compositor_bands(void)
{
        return taskbar_display_all() ? compositor_output_count() : 1;
}

// The windows that have a tab, in the order they first showed, and what each tab shows: every window on the one band,
// and on each of several, the windows whose screen it is on (compositor_screen_of).
static int compositor_popup_parent[WINDOWS];
static int compositor_popup_top(int root);
static b32 compositor_popups_close(int root, int keep);
static b32 compositor_popup_first(int row);
static void compositor_popup_leave(int row);

/*
        The launcher that is up: the window that asked to be put away by a press anywhere but on it and has no close button
        (WINDOW_DISMISS and WINDOW_NO_CLOSE, which only the launcher asks for both of), unless it has been asked to go.
        The row, or COMPOSITOR_NONE.
*/
static int compositor_launcher_row(void)
{
        for (int row = 0; row < WINDOWS; row++)
                if (compositor.showing[row] && compositor_popup_parent[row] < 0 && !(compositor.state[row] & WINDOW_CLOSING) &&
                    (compositor.asks[row] & (WINDOW_DISMISS | WINDOW_NO_CLOSE)) == (WINDOW_DISMISS | WINDOW_NO_CLOSE))
                        return row;
        return COMPOSITOR_NONE;
}

static unsigned int compositor_windows(void)
{
        unsigned int count = 0;
        int bands = compositor_bands();

        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned int place = count;

                // A popup has no tab: it is its window's. Nor has a window that asked for none (WINDOW_NO_TAB).
                if (!compositor.showing[row] || compositor_popup_parent[row] >= 0 || (compositor.asks[row] & WINDOW_NO_TAB) ||
                    (bands > 1 && compositor_screen_of(row) != compositor.band_at))
                        continue;
                while (place && compositor.made[compositor.listed[place - 1]] > compositor.made[row])
                        place--;
                memory_copy(compositor.listed + place + 1, compositor.listed + place, count - place);
                compositor.listed[place] = (unsigned char)row;
                count++;
        }

        for (unsigned int at = 0; at < count; at++)
        {
                int row = compositor.listed[at];

                compositor.marks[at] = (unsigned char)((row == compositor.focus ? COMPOSITOR_TAB_LIT : 0) |
                                                       (compositor.state[row] & WINDOW_MINIMIZED ? COMPOSITOR_TAB_AWAY : 0));
                compositor.lengths[at] = compositor_title_length(compositor.title[row], WINDOWS_TITLE);
        }

        compositor.listing = count;
        return count;
}

// What the taskbar is given of the compositor, filled for each call: the output, the band's buffers and the windows'
// tabs as compositor_windows lists them (the caller lists them first when they may have changed).
static struct taskbar_view compositor_taskbar_view(void)
{
        struct taskbar_view view;

        memory_zero(&view, sizeof(view));
        // The primary screen's (`taskbar display primary`), or with a band on every screen the one asked of.
        compositor_output_rect(compositor.band_at, &view.x, &view.y, &view.width, &view.height);
        view.scale = compositor.scale;
        view.position = taskbar_position();
        view.pixels = compositor.band_at > 0 ? compositor.band_more[compositor.band_at] : compositor.band;
        view.band_bytes = compositor.band_at > 0 ? compositor.band_more_bytes[compositor.band_at] : compositor.band_bytes;
        view.glass = compositor.glass;
        view.face = &compositor.face;
        view.engine = compositor.engine;
        view.count = compositor.listing;
        view.launcher_lit = compositor_launcher_row() >= 0;
        view.marks = compositor.marks;
        view.lengths = compositor.lengths;
        for (unsigned int at = 0; at < compositor.listing; at++)
                view.titles[at] = compositor.title[compositor.listed[at]];
        return view;
}

static struct taskbar_view compositor_band_view(void)
{
        compositor_windows();
        return compositor_taskbar_view();
}

// The panel's band as a frame: the band's own pixels when there is a font to write the titles with, and shapes the
// scene draws when there is not.
// The band compositor.band_at is (0, the primary's, when there is one), as a frame; its marks are kept apart, so
// each band's frames keep their own however many bands are listed after it.
static void compositor_band_frame(struct compositor_frame *frame)
{
        struct taskbar_view view = compositor_band_view();
        unsigned char *marks = compositor.band_marks[compositor.band_at];
        int x, y, width, height;

        memory_copy(marks, compositor.marks, compositor.listing);
        taskbar_band(&view, &x, &y, &width, &height);
        *frame = (struct compositor_frame){x, y, width, height, compositor.scale, view.pixels,
                                           (unsigned long)width, !view.pixels, marks,
                                           compositor.listing};
        frame->position = view.position;
        frame->lengths = compositor.lengths;
        frame->launcher_lit = view.launcher_lit;
}

// What the tabs show changed: the windows, which has the keys, which are put away, a title, the minute, the battery.
static void compositor_tabs_changed(void)
{
        taskbar_changed();
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();
                int x, y, width, height;

                taskbar_band(&view, &x, &y, &width, &height);
                compositor.damage_by = COMPOSITOR_OVER;
                compositor_damage(x, y, width, height);
                compositor.damage_by = 0;
        }
        compositor.band_at = 0;
}

/*
        With a band on every screen, a window whose screen changed -- dragged, snapped, moved, carried -- has its tab
        on another band, and nothing of the move need reach either band: both are listed and drawn again, or one goes
        on showing the tab it lost, and a press there (which lists the band anew) raises or closes another window.
*/
static void compositor_bands_follow(void)
{
        int changed = 0;

        if (compositor_bands() < 2)
                return;
        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned char screen = (unsigned char)(compositor.showing[row] ? compositor_screen_of(row) + 1 : 0);

                changed |= compositor.banded[row] != screen;
                compositor.banded[row] = screen;
        }
        if (changed)
                compositor_tabs_changed();
}

// How far the blur reaches, at the scale: a power of two, which the passes' steps add up to less one.
static int compositor_glass_reach(int logical)
{
        int reach = 1;

        while (reach * 2 <= logical * compositor.scale / 100)
                reach *= 2;
        return reach;
}

/*
        Glass, frosted: one pipeline for the tabs and every glass window.

        The scene under the glass is composed (the output's last row and
        column again past its edges) and halved three times, each pixel of a
        level the rounded mean of the four under it (memory_halve_u32), so a
        pixel of the smallest is a block of eight by eight fixed to the
        output. That small picture is blurred twice by memory_average_ahead,
        a pass a pixel apart and one two apart across and down: a tent seven
        small pixels wide, so a reach of three blocks each way. It is then
        doubled back up a level at a time, each pixel of a level three
        quarters of the one over it and a quarter of its neighbour across
        and down (memory_double_u32), which is a bilinear rise and leaves no
        block in sight. Over it, where it is tinted, lies a fixed ordered
        grain of up to three levels (memory_glass_over_u32): the frost, and
        what keeps the smooth ramps from banding.

        The work below the top level is a sixty-fourth of the area; what is
        done at full size is a compose of the scene, a halving and the rise,
        each a pass of the library's vector bodies.
*/

static int compositor_floor_shift(int x, int shift)
{
        return x >> shift; // arithmetic: the floor for any sign
}

static int compositor_ceil_shift(int x, int shift)
{
        return -((-x) >> shift);
}

// A scratch mapping at least bytes long, grown when it is not -- to twice what it was at least, so a window
// grown a little each frame makes it again a few times and not every frame.
static unsigned int *compositor_scratch(unsigned int **at, unsigned long *had, unsigned long bytes)
{
        long mapped;

        if (bytes <= *had)
                return *at;
        bytes = bytes < *had * 2 ? *had * 2 : bytes;
        bytes = (bytes + 65535) & ~65535ul;
        if (*at)
                compositor_call(syscall(munmap), (positive)*at, *had, 0, 0, 0, 0);
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);
        *at = null;
        *had = 0;
        if (mapped < 0 && mapped > -4096)
                return null;
        *at = (unsigned int *)mapped;
        *had = bytes;
        return *at;
}

/*
        The grain: a four by four ordered pattern of 0 to 3 levels, as words
        to add (dark glass, made lighter) or to take away (light glass): a
        row of it sixty four words to add and sixty four to take away, so a
        call may start anywhere in its first sixteen and read sixteen words
        a turn past that (memory_glass_over_u32). [side][row of four][128].
*/
static unsigned int compositor_grains[2][4][128];
static int compositor_grains_made;

static const unsigned int *compositor_grain_row(unsigned int light, int x, int y)
{
        if (!compositor_grains_made)
        {
                static const unsigned char order[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

                for (int side = 0; side < 2; side++)
                        for (int row = 0; row < 4; row++)
                                for (int at = 0; at < 64; at++)
                                {
                                        unsigned int level = (unsigned int)order[row][at & 3] >> 2;

                                        compositor_grains[side][row][at] = side ? 0 : level * 0x010101u;
                                        compositor_grains[side][row][64 + at] = side ? level * 0x010101u : 0;
                                }
                compositor_grains_made = 1;
        }
        return compositor_grains[light ? 1 : 0][y & 3] + (x & 15);
}

// The per-pixel grain the rows are made with, kept as the reference the tests compare them against (memory_glass_over_u32
// is the same operation over a row); the glass itself no longer calls it.
// Whatever grain a pixel has: what memory_glass_over_u32 gives a clear pixel over ground at x, y.
static unsigned int compositor_grained(unsigned int ground, unsigned int light, int x, int y)
{
        unsigned int out = 0;
        const unsigned int *grain = compositor_grain_row(light, x, y);

        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                int c = (int)(ground >> shift & 255) + (int)(grain[0] >> shift & 255);

                c = c > 255 ? 255 : c;
                c -= (int)(grain[64] >> shift & 255);
                out |= (unsigned int)(c < 0 ? 0 : c) << shift;
        }
        return out;
}

/*
        The blurred small picture of the scene, the small pixels from sx, sy
        (in blocks of the output) sw by sh, into into (pitch words a row):
        each the tent round its block of the halved scene. The scene is
        composed and halved a band of COMPOSITOR_GLASS_BAND rows at a time,
        so what is made at full size is only ever that band, in the cache,
        and never all of it. False when there is no memory for it.
*/
#define COMPOSITOR_GLASS_BAND 64 // full-size rows composed and halved at once: a multiple of the block

static int compositor_frost(const struct compositor_frame *frames, unsigned int count, int sx, int sy, int sw, int sh,
                            unsigned int *into, unsigned long pitch)
{
        int block = COMPOSITOR_GLASS_BLOCK, spread = COMPOSITOR_GLASS_SPREAD;
        int tw = sw + 2 * spread, th = sh + 2 * spread, ax = (sx - spread) * block, ay = (sy - spread) * block;
        int aw = tw * block, ah = th * block;
        int cx0 = ax < 0 ? 0 : ax, cx1 = ax + aw > compositor.width ? compositor.width : ax + aw;
        int cy0 = 0, cy1 = compositor.height;
        unsigned int *strip = compositor_scratch(&compositor.frost, &compositor.frost_bytes,
                                                 ((unsigned long)aw * COMPOSITOR_GLASS_BAND + (unsigned long)tw * th) * 4);
        unsigned int *small;
        unsigned long bytes;

        if (!strip || sw <= 0 || sh <= 0)
                return 0;
        small = strip + (unsigned long)aw * COMPOSITOR_GLASS_BAND;

        // Wholly left or right of the output: its nearest column.
        if (cx1 <= cx0)
                cx0 = ax + aw <= 0 ? 0 : compositor.width - 1, cx1 = cx0 + 1;

        for (int top = 0; top < ah; top += COMPOSITOR_GLASS_BAND)
        {
                int rows = ah - top < COMPOSITOR_GLASS_BAND ? ah - top : COMPOSITOR_GLASS_BAND;
                int y0 = ay + top, vy0 = y0 > cy0 ? y0 : cy0, vy1 = y0 + rows < cy1 ? y0 + rows : cy1, first, last;
                struct compositor_target under;

                // Rows past the output's edges are its first or last row again: a band wholly past them makes that one row.
                if (vy1 <= vy0)
                        vy0 = y0 + rows <= cy0 ? cy0 : cy1 - 1, vy1 = vy0 + 1;
                under = (struct compositor_target){strip, (unsigned long)aw, compositor.width, compositor.height,
                                                   cx0, vy0, cx1, vy1, ax, vy0 < y0 || vy0 >= y0 + rows ? vy0 : y0};
                compositor_scene(&under, frames, count);
                first = under.oy == y0 ? vy0 - y0 : 0;
                last = under.oy == y0 ? vy1 - y0 : 1;

                for (int row = 0; row < rows; row++)
                {
                        unsigned int *line = strip + (long)row * aw;
                        int from = row < first ? first : row >= last ? last - 1 : row;

                        if (from != row)
                                continue;
                        if (cx0 > ax)
                                memory_fill_u32(line, (positive)(cx0 - ax), line[cx0 - ax]);
                        if (cx1 < ax + aw)
                                memory_fill_u32(line + (cx1 - ax), (positive)(ax + aw - cx1), line[cx1 - ax - 1]);
                }
                // Back to front, so a row is copied before it is written over.
                for (int row = rows; row-- > 0;)
                {
                        int from = row < first ? first : row >= last ? last - 1 : row;

                        if (from != row)
                                memory_copy(strip + (long)row * aw, strip + (long)from * aw, (positive)aw * 4);
                }

                // Halved a level at a time, in place, and the band's small rows taken out to their own.
                for (int level = 1; level <= COMPOSITOR_GLASS_LEVELS; level++)
                        memory_halve_u32(strip, (positive)aw, strip, (positive)aw, (positive)(aw >> level),
                                         (positive)(rows >> level));
                for (int row = 0; row < rows / block; row++)
                        memory_copy(small + (long)(top / block + row) * tw, strip + (long)row * aw, (positive)tw * 4);
        }

        // The tent: a box of four across and down twice, the second over the complements, which rounds down
        // where the first rounded up.
        bytes = (unsigned long)tw * (unsigned long)th * 4;
        for (int round = 0; round < 2; round++)
        {
                if (round)
                        memory_invert_u32(small, small, (positive)tw * (positive)th);
                for (int step = 1; step <= 2; step *= 2)
                        memory_average_ahead((p8 address_to)small, bytes - (unsigned long)step * 4, (positive)step * 4);
                for (int step = 1; step <= 2; step *= 2)
                        memory_average_ahead((p8 address_to)small, bytes - (unsigned long)step * tw * 4, (positive)step * tw * 4);
        }

        // The tent round small pixel s is the one spread before it.
        for (int row = 0; row < sh; row++)
        {
                unsigned int *line = into + (unsigned long)row * pitch;
                const unsigned int *from = small + (long)row * tw;

                // Rows of three or fewer blocks keep the loop: the routine's dispatch costs more than their words.
                if (sw >= 4)
                        memory_invert_u32(line, from, (positive)sw);
                else
                        for (int column = 0; column < sw; column++)
                                line[column] = ~from[column];
        }
        return 1;
}

/*
        The small picture risen to full size over the rectangle x0, y0 to x1,
        y1 of the output: top is the small picture, top_pitch words a row,
        whose first pixel is the block tx, ty. Each level is doubled from the
        one over it, only as much of it as the rectangle needs and a pixel
        round that. Answers where the rectangle's first pixel is in the
        level made, pitch words a row, or null when there is no memory for it.
*/
static const unsigned int *compositor_rise(const unsigned int *top, unsigned long top_pitch, int tx, int ty, int x0,
                                           int y0, int x1, int y1, unsigned long *pitch)
{
        int lo_x[COMPOSITOR_GLASS_LEVELS + 1], lo_y[COMPOSITOR_GLASS_LEVELS + 1];
        int hi_x[COMPOSITOR_GLASS_LEVELS + 1], hi_y[COMPOSITOR_GLASS_LEVELS + 1];
        unsigned long need = 0, at[COMPOSITOR_GLASS_LEVELS + 1];
        unsigned int *room;

        // What each level has to hold: the pixels the one under it doubles, and one round them.
        lo_x[0] = x0, lo_y[0] = y0, hi_x[0] = x1, hi_y[0] = y1;
        for (int level = 1; level <= COMPOSITOR_GLASS_LEVELS; level++)
        {
                lo_x[level] = compositor_floor_shift(lo_x[level - 1], 1) - 1;
                lo_y[level] = compositor_floor_shift(lo_y[level - 1], 1) - 1;
                hi_x[level] = compositor_ceil_shift(hi_x[level - 1], 1) + 1;
                hi_y[level] = compositor_ceil_shift(hi_y[level - 1], 1) + 1;
        }
        // Each level below the top doubled whole from the interior of the one over it.
        for (int level = COMPOSITOR_GLASS_LEVELS - 1; level >= 1; level--)
        {
                lo_x[level] = 2 * (lo_x[level + 1] + 1), lo_y[level] = 2 * (lo_y[level + 1] + 1);
                hi_x[level] = 2 * (hi_x[level + 1] - 1), hi_y[level] = 2 * (hi_y[level + 1] - 1);
                at[level] = need;
                need += (unsigned long)(hi_x[level] - lo_x[level]) * (unsigned long)(hi_y[level] - lo_y[level]);
        }
        at[0] = need;
        need += (unsigned long)(2 * (hi_x[1] - lo_x[1] - 2)) * (unsigned long)(2 * (hi_y[1] - lo_y[1] - 2));
        room = compositor_scratch(&compositor.rise, &compositor.rise_bytes, need * 4);
        if (!room)
                return null;

        for (int level = COMPOSITOR_GLASS_LEVELS; level >= 1; level--)
        {
                const unsigned int *from;
                unsigned long from_pitch, into_pitch;
                unsigned int *into;
                int w = hi_x[level] - lo_x[level] - 2, h = hi_y[level] - lo_y[level] - 2;

                if (level == COMPOSITOR_GLASS_LEVELS)
                {
                        from = top + (long)(lo_y[level] + 1 - ty) * (long)top_pitch + (lo_x[level] + 1 - tx);
                        from_pitch = top_pitch;
                }
                else
                {
                        from_pitch = (unsigned long)(hi_x[level] - lo_x[level]);
                        from = room + at[level] + from_pitch + 1;
                }
                into = room + at[level - 1];
                into_pitch = (unsigned long)(2 * w);
                memory_double_u32(into, into_pitch, from, from_pitch, (positive)w, (positive)h);
        }

        // Level 0 made from 2 (lo_x[1] + 1) on.
        *pitch = (unsigned long)(2 * (hi_x[1] - lo_x[1] - 2));
        return room + at[0] + (unsigned long)(y0 - 2 * (lo_y[1] + 1)) * *pitch + (unsigned long)(x0 - 2 * (lo_x[1] + 1));
}

/*
        The frost at full size, for the tabs: into (gw pixels a row) has the
        pixel at x, y of the output at x - across - gx, y - down - gy. The
        reach is the pipeline's whatever across and down say. False, and
        into untouched, when there is no memory for it.
*/
static int compositor_blur(unsigned int *into, const struct compositor_frame *frames, unsigned int count, int gx,
                           int gy, int gw, int gh, int across, int down)
{
        int x0 = gx + across, y0 = gy + down, x1 = x0 + gw - 2 * across, y1 = y0 + gh - 2 * down;
        int shift = COMPOSITOR_GLASS_LEVELS, margin = COMPOSITOR_GLASS_LEVELS;
        int sx = compositor_floor_shift(x0, shift) - margin, sy = compositor_floor_shift(y0, shift) - margin;
        int sw = compositor_ceil_shift(x1, shift) + margin - sx, sh = compositor_ceil_shift(y1, shift) + margin - sy;
        unsigned int *top = compositor_scratch(&compositor.blur, &compositor.blur_bytes, (unsigned long)sw * sh * 4);

        if (x1 <= x0 || y1 <= y0 || !top || !compositor_frost(frames, count, sx, sy, sw, sh, top, (unsigned long)sw))
                return 0;
        memory_zero(into, (positive)gh * (positive)gw * 4);
        {
                unsigned long pitch;
                const unsigned int *made = compositor_rise(top, (unsigned long)sw, sx, sy, x0, y0, x1, y1, &pitch);

                if (!made)
                        return 0;
                for (int row = 0; row < y1 - y0; row++)
                        memory_copy(into + (long)row * gw, made + (unsigned long)row * pitch, (positive)(x1 - x0) * 4);
        }
        return 1;
}

// The glass over a rectangle of the output: the scene under it composed and blurred into compositor.glass, with the
// blur's reach round it. Answers the row stride, or 0 when there is no memory for it.
static int compositor_glass_make(const struct compositor_frame *windows, unsigned int count, int x, int y, int width,
                                 int height)
{
        int across = compositor_glass_reach(COMPOSITOR_GLASS_ACROSS), down = compositor_glass_reach(COMPOSITOR_GLASS_DOWN);
        int gw = width + 2 * across, gh = height + 2 * down;

        if ((unsigned long)gw * (unsigned long)gh * 4 > compositor.glass_bytes ||
            !compositor_blur(compositor.glass, windows, count, x - across, y - down, gw, gh, across, down))
                return 0;
        return gw;
}

// The windows the taskbar's glass is made from, for its callback.
struct compositor_glass_scene
{
        const struct compositor_frame *windows;
        unsigned int count;
};

static int compositor_glass_scene_make(void *context, int x, int y, int width, int height)
{
        const struct compositor_glass_scene *scene = (const struct compositor_glass_scene *)context;

        return compositor_glass_make(scene->windows, scene->count, x, y, width, height);
}

// A colour half as far again from its grey, kept in a byte. One pixel stays here: a call into the library costs more
// than these few instructions, and spans of the glass go through memory_saturate_u32 (compositor_glass_pass).
static unsigned int compositor_vivid(unsigned int pixel)
{
        int r = (int)(pixel >> 16 & 255), g = (int)(pixel >> 8 & 255), b = (int)(pixel & 255);
        int grey = (r * 77 + g * 150 + b * 29) >> 8;

        r += (r - grey) >> 1;
        g += (g - grey) >> 1;
        b += (b - grey) >> 1;
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        return (unsigned int)(r << 16 | g << 8 | b);
}

// The output's band over x, width, from y down its height: what the tabs redone wrote, for the composition to show.
static void compositor_band_damage(int x, int y, int width, int height)
{
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(x, y, width, height);
        compositor.damage_by = 0;
}

#define COMPOSITOR_TABS_ALL (~0ul)

// Redoes the slots redo names on the taskbar: the glass is made over their span and the blur's reach, and the band is
// damaged over that span.
static void compositor_band_draw(const struct compositor_frame *windows, unsigned int count, unsigned long redo)
{
        struct taskbar_view view = compositor_band_view();
        struct compositor_glass_scene scene = {windows, count};
        int rect[4];

        if (taskbar_draw(&view, redo, compositor_glass_scene_make, &scene, rect))
                compositor_band_damage(rect[0], rect[1], rect[2], rect[3]);
}

/*
        Glass windows: a window whose program asked for it (WINDOW_GLASS) is
        what is under it, frosted as the tabs' glass is, made more vivid,
        and lifted from what is behind it toward mid grey (COMPOSITOR_GLASS_LIFT):
        lighter over a dark backdrop, darker over a bright one, so the glass
        always separates from it. Every pixel is then kept on the theme's side
        of lightness (the caps below), so an ink that stands out from the
        ground the program is told stands out everywhere. The program's picture,
        premultiplied ARGB, lies over that, and the grain under it.

        What is kept is the small picture: the blurred blocks under the
        window and three round it, as blurred and as tinted, a sixty-fourth
        of the window. A change in the scene under it makes again only the
        blocks it reaches; the window moved or another size makes them all
        again, which is a compose and a halving of what is under it and no
        more. The picture over them is risen and composed again only where
        the blocks or the program's picture changed. Bottom first, so a
        glass window over another sees it as it is now, and before the tabs'
        glass, which sees them both.
*/
#define COMPOSITOR_GLASS_LIFT 96        // how far toward mid grey a glass window lifts what is behind it, in 256ths: lighter over dark, darker over light
#define COMPOSITOR_GLASS_PIVOT 0x808080u // the grey the lift is toward, so a dark backdrop comes up and a bright one comes down
#define COMPOSITOR_GLASS_DARKEST 0x5c // dark glass: no colour of a pixel brighter
#define COMPOSITOR_GLASS_LIGHTEST 0xb4 // light glass: none dimmer
#define COMPOSITOR_GLASS_UNSIDED 2u
#define COMPOSITOR_GLASS_MARGIN 3 // the blocks kept round a window: what the rise reads past its edge

// The tint of a pixel already made more vivid (compositor_vivid), so a caller holding that value does not boost it again.
// The tint of count boosted pixels into into, for a window of the side given: memory_tint_u32 with the glass's lift
// toward mid grey, and the brightest capped (dark glass) or the dimmest raised (light glass).
static void compositor_glass_tone_span(unsigned int *into, const unsigned int *from, positive count, unsigned int side)
{
        memory_tint_u32(into, from, count, COMPOSITOR_GLASS_PIVOT, COMPOSITOR_GLASS_LIFT, side ? 255 : COMPOSITOR_GLASS_DARKEST,
                        side ? COMPOSITOR_GLASS_LIGHTEST : 0);
}

// One pixel stays in C, as compositor_vivid does: the library call is the loss at this length (see there).
static unsigned int compositor_glass_tone(unsigned int boosted, unsigned int side)
{
        unsigned int pixel = window_mix(boosted, COMPOSITOR_GLASS_PIVOT, COMPOSITOR_GLASS_LIFT);
        unsigned int r = pixel >> 16 & 255, g = pixel >> 8 & 255, b = pixel & 255;
        unsigned int most = r > g ? r : g, least = r < g ? r : g;

        most = most > b ? most : b;
        least = least < b ? least : b;
        if (!side && most > COMPOSITOR_GLASS_DARKEST)
        {
                r = r * COMPOSITOR_GLASS_DARKEST / most;
                g = g * COMPOSITOR_GLASS_DARKEST / most;
                b = b * COMPOSITOR_GLASS_DARKEST / most;
        }
        else if (side && least < COMPOSITOR_GLASS_LIGHTEST)
        {
                r = 255 - (255 - r) * (255 - COMPOSITOR_GLASS_LIGHTEST) / (255 - least);
                g = 255 - (255 - g) * (255 - COMPOSITOR_GLASS_LIGHTEST) / (255 - least);
                b = 255 - (255 - b) * (255 - COMPOSITOR_GLASS_LIGHTEST) / (255 - least);
        }
        return r << 16 | g << 8 | b;
}

static unsigned int compositor_glass_tint(unsigned int blurred, unsigned int side)
{
        return compositor_glass_tone(compositor_vivid(blurred), side);
}

// The picture over the risen, tinted glass and its grain, inside c (x1, y1, x2, y2 on the output, inside the window):
// risen and laid a band of rows at a time, so the risen glass is in the cache when it is laid.
static int compositor_glass_compose(int row, const int *c)
{
        const unsigned int *picture = compositor_pixels(row);
        int *blocks = compositor.glass_blocks[row], width = c[2] - c[0];

        for (int y0 = c[1]; y0 < c[3]; y0 += COMPOSITOR_GLASS_BAND)
        {
                int y1 = y0 + COMPOSITOR_GLASS_BAND < c[3] ? y0 + COMPOSITOR_GLASS_BAND : c[3];
                unsigned long pitch;
                const unsigned int *ground = compositor_rise(compositor.tinted[row], (unsigned long)blocks[2], blocks[0],
                                                             blocks[1], c[0], y0, c[2], y1, &pitch);

                if (!ground)
                        return 0;
                for (int y = y0; y < y1; y++)
                        memory_glass_over_u32(compositor.composed[row] + (unsigned long)(y - compositor.y[row]) * compositor.w[row] +
                                                  (unsigned long)(c[0] - compositor.x[row]),
                                              picture + (unsigned long)(y - compositor.y[row]) * (unsigned long)compositor_stride() +
                                                  (unsigned long)(c[0] - compositor.x[row]),
                                              ground + (unsigned long)(y - y0) * pitch, (positive)width,
                                              compositor_grain_row(compositor.glass_side[row], c[0], y));
        }
        compositor.recomposed += (positive)width * (positive)(c[3] - c[1]);
        return 1;
}

/*
        The side a glass window's tint goes to, from the mean of its blocks,
        and the ground that comes to: what its program's inks stand out from.
        The mean is the sums compositor_glass_pass keeps as the blocks change,
        so it is a division here and no block is boosted again. Answers whether
        the side changed.
*/
static int compositor_glass_side(int row)
{
        unsigned long *sum = compositor.glass_sum[row], count = (unsigned long)compositor.glass_blocks[row][2] * compositor.glass_blocks[row][3];
        unsigned int was = compositor.glass_side[row], mean, ground;

        if (!count)
                return 0;

        mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
        // The side is the theme's: a light theme's windows are light glass over whatever is under them, and a dark
        // one's dark, so the inks they draw in (the theme's) stand out from their ground everywhere.
        compositor.glass_side[row] = compositor.theme_side;
        ground = compositor_glass_tint(mean, compositor.glass_side[row]);

        // Told when the side changes, and when it has moved far enough for an ink to be worth drawing again.
        {
                unsigned int told = compositor.glass_told[row], moved = 0;

                for (unsigned int shift = 0; shift < 24; shift += 8)
                {
                        int d = (int)(ground >> shift & 255) - (int)(told >> shift & 255);

                        moved += (unsigned int)(d < 0 ? -d : d);
                }
                if (compositor.glass_side[row] != was || moved > 24)
                {
                        compositor.glass_told[row] = ground;
                        compositor.table->ground[row] = ground;
                        compositor.woken |= 1u << row;
                }
        }
        return compositor.glass_side[row] != was;
}

// A glass window's blocks and picture given back, and it is none; with no glass window left the scratch's pages
// go back too (MADV_DONTNEED), which the tabs' glass takes again a band's worth at a time.
static void compositor_glass_drop(int row)
{
        int left = 0;

        if (compositor.backdrop[row])
                compositor_call(syscall(munmap), (positive)compositor.backdrop[row], compositor.glass_cut[row], 0, 0, 0, 0);
        for (int other = 0; other < WINDOWS; other++)
                left |= other != row && compositor.glassy[other];
        if (!left && compositor.frost)
                compositor_call(syscall(madvise), (positive)compositor.frost, compositor.frost_bytes, 4, 0, 0, 0);
        if (!left && compositor.rise)
                compositor_call(syscall(madvise), (positive)compositor.rise, compositor.rise_bytes, 4, 0, 0, 0);
        compositor.backdrop[row] = compositor.tinted[row] = compositor.boosted[row] = compositor.composed[row] = null;
        compositor.glass_cut[row] = 0;
        compositor.glassy[row] = 0;
        memory_zero(compositor.glass_sum[row], sizeof(compositor.glass_sum[row]));
        memory_zero(compositor.glass_made[row], sizeof(compositor.glass_made[row]));
        memory_zero(compositor.glass_blocks[row], sizeof(compositor.glass_blocks[row]));
        memory_zero(compositor.stale[row], sizeof(compositor.stale[row]));
        memory_zero(compositor.dirty[row], sizeof(compositor.dirty[row]));
}

/*
        Room for the blocks, as blurred and as tinted, and the picture, at the
        window's size and place: one mapping as large as the output could
        make them, taken once, so a window resized every frame is never
        mapped again, and only the pages its size touches are ever there.
*/
static int compositor_glass_room(int row)
{
        int shift = COMPOSITOR_GLASS_LEVELS, margin = COMPOSITOR_GLASS_MARGIN;
        int x = compositor.x[row], y = compositor.y[row], w = (int)compositor.w[row], h = (int)compositor.h[row];
        int *blocks = compositor.glass_blocks[row];
        unsigned long pixels = (unsigned long)w * (unsigned long)h, cells, need, most;
        long mapped;

        blocks[0] = compositor_floor_shift(x, shift) - margin;
        blocks[1] = compositor_floor_shift(y, shift) - margin;
        blocks[2] = compositor_ceil_shift(x + w, shift) + margin - blocks[0];
        blocks[3] = compositor_ceil_shift(y + h, shift) + margin - blocks[1];
        cells = (unsigned long)blocks[2] * (unsigned long)blocks[3];
        need = ((cells * 3 + pixels) * 4 + 4095) & ~4095ul;
        most = ((unsigned long)((compositor.width >> shift) + 2 * margin + 2) * (unsigned long)((compositor.height >> shift) + 2 * margin + 2) * 3 +
                (unsigned long)compositor.width * (unsigned long)compositor.height) * 4;
        most = (most + 4095) & ~4095ul;
        need = need > most ? need : most;

        if (need > compositor.glass_cut[row])
        {
                if (compositor.backdrop[row])
                        compositor_call(syscall(munmap), (positive)compositor.backdrop[row], compositor.glass_cut[row], 0, 0, 0, 0);
                mapped = compositor_call(syscall(mmap), 0, need, 3, 0x22, (positive)-1, 0);
                compositor.backdrop[row] = compositor.tinted[row] = compositor.boosted[row] = compositor.composed[row] = null;
                compositor.glass_cut[row] = 0;
                if (mapped < 0 && mapped > -4096)
                        return 0;
                compositor.backdrop[row] = (unsigned int *)mapped;
                compositor.glass_cut[row] = need;
        }
        compositor.tinted[row] = compositor.backdrop[row] + cells;
        compositor.boosted[row] = compositor.tinted[row] + cells;
        compositor.composed[row] = compositor.boosted[row] + cells;
        return 1;
}

static void compositor_glass_pass(struct compositor_frame *frames, const unsigned char *rows, unsigned int count)
{
        int shift = COMPOSITOR_GLASS_LEVELS, spread = COMPOSITOR_GLASS_SPREAD, block = COMPOSITOR_GLASS_BLOCK;

        for (unsigned int at = 0; at < count; at++)
        {
                int row = rows[at], box[4], *made = compositor.glass_made[row], *blocks, *stale = compositor.stale[row];
                int place[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

                if (!compositor.glassy[row])
                        continue;

                compositor_contents_box(row, box);
                blocks = compositor.glass_blocks[row];
                if (memory_compare(made, place, sizeof(place)))
                {
                        if (!compositor_glass_room(row))
                        {
                                // No memory for it: the window is shown as it is drawn, and no longer glass.
                                compositor_glass_drop(row);
                                continue;
                        }
                        memory_copy(made, place, sizeof(place));
                        // The blocks are new, so none of the old ones count: their boosted values and sums are zero,
                        // and the change below (every block, which the clip takes in whole) adds each one in.
                        memory_zero(compositor.boosted[row], (positive)blocks[2] * (positive)blocks[3] * 4);
                        memory_zero(compositor.glass_sum[row], sizeof(compositor.glass_sum[row]));
                        // Every block.
                        stale[0] = blocks[0] * block, stale[1] = blocks[1] * block;
                        stale[2] = (blocks[0] + blocks[2]) * block, stale[3] = (blocks[1] + blocks[3]) * block;
                }

                if (stale[2] > stale[0])
                {
                        // The blocks the change reaches, of those kept.
                        int bx0 = compositor_floor_shift(stale[0], shift) - spread, by0 = compositor_floor_shift(stale[1], shift) - spread;
                        int bx1 = compositor_ceil_shift(stale[2], shift) + spread, by1 = compositor_ceil_shift(stale[3], shift) + spread;
                        int shown[4];

                        bx0 = bx0 < blocks[0] ? blocks[0] : bx0, by0 = by0 < blocks[1] ? blocks[1] : by0;
                        bx1 = bx1 > blocks[0] + blocks[2] ? blocks[0] + blocks[2] : bx1;
                        by1 = by1 > blocks[1] + blocks[3] ? blocks[1] + blocks[3] : by1;
                        if (bx1 > bx0 && by1 > by0)
                        {
                                unsigned int *into = compositor.backdrop[row] + (long)(by0 - blocks[1]) * blocks[2] + (bx0 - blocks[0]);

                                if (!compositor_frost(frames, at, bx0, by0, bx1 - bx0, by1 - by0, into, (unsigned long)blocks[2]))
                                {
                                        compositor_glass_drop(row);
                                        continue;
                                }
                                // Each row of the changed cells: boosted in one call, its sums moved cell by cell (the one boost of
                                // each cell), and tinted from the boost in another. The boosts wait in the tinted row until it is written.
                                for (int by = by0; by < by1; by++)
                                {
                                        long first = (long)(by - blocks[1]) * blocks[2] + (bx0 - blocks[0]);
                                        unsigned int *now = compositor.tinted[row] + first;

                                        memory_saturate_u32(now, compositor.backdrop[row] + first, (positive)(bx1 - bx0));
                                        memory_sum_swap_u32((p64 address_to)compositor.glass_sum[row], compositor.boosted[row] + first, now,
                                                            (positive)(bx1 - bx0));
                                        compositor_glass_tone_span(now, compositor.boosted[row] + first, (positive)(bx1 - bx0), compositor.glass_side[row]);
                                }
                                compositor.blurred += (positive)(bx1 - bx0) * (positive)(by1 - by0);

                                // The pixels those blocks rise into: a block each way past them.
                                shown[0] = (bx0 - 1) * block, shown[1] = (by0 - 1) * block;
                                shown[2] = (bx1 + 1) * block, shown[3] = (by1 + 1) * block;
                                compositor_meet(shown, box);
                                compositor_union(compositor.dirty[row], shown);
                                if (compositor_glass_side(row))
                                {
                                        // Every cell's boosted pixel is current (the sums were kept as the cells changed), so the flip boosts none.
                                        compositor_glass_tone_span(compositor.tinted[row], compositor.boosted[row], (positive)blocks[2] * (positive)blocks[3],
                                                                   compositor.glass_side[row]);
                                        memory_copy(compositor.dirty[row], box, sizeof(box));
                                }
                        }
                }

                compositor_meet(compositor.dirty[row], box);
                if (compositor.dirty[row][2] > compositor.dirty[row][0])
                {
                        int *c = compositor.dirty[row];

                        if (!compositor_glass_compose(row, c))
                        {
                                compositor_glass_drop(row);
                                continue;
                        }
                        // What changed of it is shown, and frosted again under any glass over it.
                        compositor.damage_by = row + 1;
                        compositor_damage(c[0], c[1], c[2] - c[0], c[3] - c[1]);
                        compositor.damage_by = 0;
                }
                memory_zero(compositor.stale[row], sizeof(compositor.stale[row]));
                memory_zero(compositor.dirty[row], sizeof(compositor.dirty[row]));
                frames[at].pixels = compositor.composed[row];
                frames[at].pitch = (unsigned long)place[2];
        }
}

// The mean colour under a window's close button, from the pixels the frame is drawn with: made once a present,
// after the glass, and read by compositor_close_draw for every piece of the window that is drawn.
static void compositor_close_mean(struct compositor_frame *frame)
{
        int bx, by, side;
        unsigned long sum[3] = {0, 0, 0};

        if (!compositor_close_box(frame, &bx, &by, &side))
                return;

        // Only where a change has reached the box: nothing is drawn in it otherwise.
        {
                int box[4] = {bx, by, bx + side, by + side}, touched = 0;

                for (unsigned int at = 0; at < compositor.damages && !touched; at++)
                {
                        int meet[4];

                        memory_copy(meet, compositor.damage[at], sizeof(meet));
                        compositor_meet(meet, box);
                        touched = meet[2] > meet[0] && meet[3] > meet[1];
                }
                if (!touched)
                        return;
        }

        // A picture drawn at its own size is summed a row of the box at a time; a scaled one is read through its map.
        const unsigned int *picture = compositor_scaled(frame) ? 0 : frame->pixels;

        for (int py = by; py < by + side; py++)
        {
                const unsigned int *row = picture ? picture + (unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(bx - frame->x) : 0;

                for (int px = bx; px < bx + side; px++)
                {
                        unsigned int under = row ? row[px - bx] : compositor_picture_at(frame, px - frame->x, py - frame->y);

                        sum[0] += under >> 16 & 255;
                        sum[1] += under >> 8 & 255;
                        sum[2] += under & 255;
                }
        }

        unsigned long count = (unsigned long)side * (unsigned long)side;
        frame->close_mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
}

/* ---- a window shown straight from its buffer ---- */

/*
        A window that fills the output is shown from the buffer its program
        drew, with no copy: the card scans that buffer out, and the compositor
        writes only what lies over it -- the tabs -- into the buffer itself,
        keeping what they covered, and tells the program the rectangle it wrote
        (spoiled) so the program draws it again before it shows that buffer
        next. A window is told a picture was taken when the card has let go of
        the buffer it showed before (the flip's event), never while the card
        still reads it. Anything the card or udmabuf will not do, and any
        window that does not fill the output alone, is composed and copied as
        before.
*/
struct compositor_fb2
{
        unsigned int fb_id, width, height, format, flags;
        unsigned int handles[4], pitches[4], offsets[4];
        unsigned long modifier[4];
};

// The framebuffer of a row's buffer, made the first time it is asked for; 0 when the table is not a memfd or udmabuf
// or the card refuses (a card that cannot show memory it did not make), and then none is asked for again.
static unsigned int compositor_window_fb(int row, unsigned int which)
{
        struct
        {
                unsigned int memfd, flags;
                unsigned long offset, size;
        } create;
        struct
        {
                unsigned int handle, flags;
                int fd;
        } prime = {0, 0, -1};
        struct compositor_fb2 fb = {0};
        long dmabuf;

        if (compositor.scan_fb[row][which] || !compositor.table_memfd || compositor.direct_refused)
                return compositor.scan_fb[row][which];
        if (!compositor.udmabuf)
        {
                long handle = (long)system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC);

                if (handle < 0)
                {
                        compositor.direct_refused = 1;
                        compositor.table->refused = 1;
                        return 0;
                }
                compositor.udmabuf = (int)handle + 1;
        }

        // Pinned first: a program gives back no pages of a buffer the card may read.
        __atomic_store_n(&compositor.table->pinned[row], 1u, __ATOMIC_RELEASE);
        create.memfd = (unsigned int)(compositor.table_memfd - 1);
        create.flags = 1; // UDMABUF_FLAGS_CLOEXEC
        create.offset = compositor.pixels + ((unsigned long)row * 2 + which) * compositor.buffer;
        create.size = compositor.buffer;
        dmabuf = system_control(compositor.udmabuf - 1, COMPOSITOR_UDMABUF_CREATE, address_of create);
        if (dmabuf < 0)
        {
                compositor.direct_refused = 1;
                compositor.table->refused = 2;
                return 0;
        }
        prime.fd = (int)dmabuf;
        if (system_control(compositor.card, COMPOSITOR_PRIME_FD_TO_HANDLE, address_of prime) != 0)
        {
                system_close(dmabuf);
                compositor.direct_refused = 1;
                compositor.table->refused = 3;
                return 0;
        }
        system_close(dmabuf);

        fb.width = (unsigned int)compositor.width;
        fb.height = (unsigned int)compositor.height;
        fb.format = COMPOSITOR_XRGB8888;
        fb.handles[0] = prime.handle;
        fb.pitches[0] = (unsigned int)compositor_stride() * 4;
        if (system_control(compositor.card, COMPOSITOR_ADDFB2, address_of fb) != 0)
        {
                unsigned int close[2] = {prime.handle, 0};

                system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                compositor.direct_refused = 1;
                compositor.table->refused = 4;
                return 0;
        }
        compositor.scan_fb[row][which] = fb.fb_id;
        compositor.scan_handle[row][which] = prime.handle;
        return fb.fb_id;
}

// A row's framebuffers gone, when the card is not showing one of them.
static void compositor_window_fb_drop(int row)
{
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.scan_fb[row][which])
                {
                        unsigned int close[2] = {compositor.scan_handle[row][which], 0};

                        system_control(compositor.card, COMPOSITOR_RMFB, address_of compositor.scan_fb[row][which]);
                        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                        compositor.scan_fb[row][which] = compositor.scan_handle[row][which] = 0;
                }
        compositor.patched[row][0] = compositor.patched[row][1] = 0;
        __atomic_store_n(&compositor.table->pinned[row], 0u, __ATOMIC_RELEASE);
}

static int compositor_shows_row(int row)
{
        return (compositor.on_screen &&
                (compositor.on_screen == compositor.scan_fb[row][0] || compositor.on_screen == compositor.scan_fb[row][1])) ||
               (compositor.plane_fb &&
                (compositor.plane_fb == compositor.scan_fb[row][0] || compositor.plane_fb == compositor.scan_fb[row][1]));
}

// A flip to fb, done at the next vertical blank, with its event; false when the card refuses it.
static int compositor_flip(unsigned int fb, int row, unsigned int which)
{
        struct
        {
                unsigned int crtc_id, fb_id, flags, reserved;
                unsigned long user_data;
        } flip = {compositor.crtc, fb, COMPOSITOR_FLIP_EVENT, 0, 0};

        if (system_control(compositor.card, COMPOSITOR_PAGE_FLIP, address_of flip) != 0)
                return 0;
        compositor.flip_pending = 1;
        compositor.flip_row = row;
        compositor.flip_buffer = which;
        compositor.on_screen = fb;
        return 1;
}

// The pictures held back are said taken: for the row the flip showed, once it shows that row's newest buffer.
static void compositor_flip_done(void)
{
        compositor.flip_pending = 0;
        for (int row = 0; row < WINDOWS; row++)
        {
                if (!(compositor.held_rows & (1u << row)))
                        continue;
                if (row == compositor.flip_row && compositor.front[row] != compositor.flip_buffer)
                        continue;
                __atomic_store_n(&compositor.table->shown[row], compositor.taken_held[row], __ATOMIC_RELEASE);
                compositor.held_rows &= ~(1u << row);
                compositor.woken |= 1u << row;
        }
        // A window that left while the card showed it: its framebuffers now, the card being off them.
        for (int row = 0; row < WINDOWS; row++)
                if ((compositor.dropping & (1u << row)) && !compositor_shows_row(row))
                {
                        compositor_window_fb_drop(row);
                        compositor.dropping &= ~(1u << row);
                }
}

// The card's events, read when its descriptor is readable: a flip's completion is the one asked for.
static void compositor_card_read(void)
{
        p8 events[512];
        long got = compositor_call(syscall(read), (positive)compositor.card, (positive)events, sizeof(events), 0, 0, 0);

        for (long at = 0; at + 8 <= got;)
        {
                unsigned int type, length;

                memory_copy(address_of type, events + at, 4);
                memory_copy(address_of length, events + at + 4, 4);
                if (length < 8)
                        break;
                if (type == COMPOSITOR_EVENT_FLIP_COMPLETE)
                        compositor_flip_done();
                at += length;
        }
}

// A row's buffer which, from the compositor's own copy of the layout: the table is shared, so the header's pixels,
// buffer and stride are a client's to write, and the compositor draws through what it set itself.
static unsigned int *compositor_buffer(int row, unsigned int which)
{
        return (unsigned int *)((char *)compositor.table + compositor.pixels +
                                ((unsigned long)row * 2 + (which & 1u)) * compositor.buffer);
}

// What the tabs covered of the buffer the card shows, put back, so what is made from it (the glass) is the window's.
static void compositor_direct_unpatch(void)
{
        int row = compositor.direct_row;
        unsigned int which = row >= 0 ? compositor.front[row] : 0;
        const int *rect = compositor.under_rect[which];
        unsigned int *pixels;

        if (row < 0 || !compositor.patched[row][which] || !compositor.under[which])
                return;
        compositor_runs_forget(row);
        pixels = compositor_buffer(row, which);
        for (int y = rect[1]; y < rect[3]; y++)
                memory_copy(pixels + (unsigned long)y * (unsigned long)compositor_stride() + (unsigned long)rect[0],
                            compositor.under[which] + (unsigned long)(y - rect[1]) * (unsigned long)(rect[2] - rect[0]),
                            (unsigned long)(rect[2] - rect[0]) * 4);
        compositor.patched[row][which] = 0;
}

// The tabs written over a row's buffer, what they covered kept, and the program told the rectangle.
static void compositor_direct_patch(int row, unsigned int which, const struct compositor_frame *tabs, unsigned int count)
{
        int rect[4] = {compositor.width, compositor.height, 0, 0};
        unsigned int *pixels = compositor_buffer(row, which);
        struct windows *table = compositor.table;

        compositor_runs_forget(row);
        for (unsigned int at = 0; at < count; at++)
        {
                int x, y, width, height;

                compositor_outline(&tabs[at], &x, &y, &width, &height);
                rect[0] = x < rect[0] ? x : rect[0];
                rect[1] = y < rect[1] ? y : rect[1];
                rect[2] = x + width > rect[2] ? x + width : rect[2];
                rect[3] = y + height > rect[3] ? y + height : rect[3];
        }
        rect[0] = rect[0] < 0 ? 0 : rect[0];
        rect[1] = rect[1] < 0 ? 0 : rect[1];
        rect[2] = rect[2] > compositor.width ? compositor.width : rect[2];
        rect[3] = rect[3] > compositor.height ? compositor.height : rect[3];
        if (rect[2] <= rect[0] || rect[3] <= rect[1])
                return;

        if (!compositor.under[which])
        {
                long mapped = compositor_call(syscall(mmap), 0, (positive)compositor.width * (positive)compositor.height * 4, 3,
                                              0x22, (positive)-1, 0);

                if (mapped < 0 && mapped > -4096)
                        return;
                compositor.under[which] = (unsigned int *)mapped;
        }
        for (int y = rect[1]; y < rect[3]; y++)
                memory_copy(compositor.under[which] + (unsigned long)(y - rect[1]) * (unsigned long)(rect[2] - rect[0]),
                            pixels + (unsigned long)y * (unsigned long)compositor_stride() + (unsigned long)rect[0],
                            (unsigned long)(rect[2] - rect[0]) * 4);
        memory_copy(compositor.under_rect[which], rect, sizeof(rect));

        struct compositor_target target = {pixels, (unsigned long)compositor_stride(), compositor.width, compositor.height,
                                           rect[0], rect[1], rect[2], rect[3]};

        for (unsigned int at = 0; at < count; at++)
                compositor_draw(&target, &tabs[at]);
        compositor.patched[row][which] = 1;
        table->spoiled_x[row][which] = rect[0];
        table->spoiled_y[row][which] = rect[1];
        table->spoiled_width[row][which] = rect[2] - rect[0];
        __atomic_store_n(&table->spoiled_height[row][which], rect[3] - rect[1], __ATOMIC_RELEASE);
}

// The window the card can show straight: the top one, opaque, square and filling the output at the size it drew, with
// nothing over it but the tabs, a pointer on its own plane, and a framebuffer of its buffer; -1 when there is none.
static int compositor_direct_candidate(const struct compositor_frame *frames, const unsigned char *rows, unsigned int windows)
{
        int row;

        // On more than one screen every frame is composed: a flip, the overlay and the window's framebuffer are one
        // crtc's, and the scanout is the whole space.
        if (compositor.card < 0 || compositor.direct_refused || !compositor.table_memfd || compositor_cursor_in_scene() || !windows ||
            compositor.drag >= 0 || compositor.resize >= 0 || compositor.preview_draw || compositor_output_count() > 1 ||
            compositor_spots_any(0))
                return -1;
        row = rows[windows - 1];
        if (compositor.table->glass[row] || compositor.landing[row] || compositor.glide[row] || compositor.x[row] != 0 || compositor.y[row] != 0 ||
            compositor.w[row] != (unsigned int)compositor.width || compositor.h[row] != (unsigned int)compositor.height ||
            compositor.picture_w[row] != compositor.w[row] || compositor.picture_h[row] != compositor.h[row] ||
            compositor.asking[row] || compositor_radius(&frames[windows - 1]) || frames[windows - 1].line)
                return -1;
        return compositor_window_fb(row, compositor.front[row]) ? row : -1;
}

// The frame of a window shown straight: the tabs over its buffer, and the card flipped to it. False when the card will not.
static b32 compositor_present_direct(int row, const struct compositor_frame *tabs, unsigned int count)
{
        unsigned int which = compositor.front[row], fb = compositor_window_fb(row, which);
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_dirty dirty = {fb, 0, 0, 0, (unsigned long)clip};

        if (!fb)
                return 0;
        compositor_direct_patch(row, which, tabs, count);
        if (compositor.on_screen != fb)
        {
                // The card is still on its way to the last one: this is shown once it is there.
                if (compositor.flip_pending)
                        return 1;
                if (!compositor_flip(fb, row, which))
                {
                        compositor_direct_unpatch();
                        compositor.direct_refused = 1;
                        compositor.table->refused = 5;
                        return 0;
                }
        }
        compositor.direct_row = row;
        compositor.table->scanned++;

        // A virtual card copies to its host only what it is told changed.
        for (unsigned int at = 0; at < compositor.damages; at++)
                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)compositor.damage[at][side];
        dirty.clip_count = compositor.damages;
        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        compositor.damages = 0;
        compositor.screen_stale = 1;
        return 1;
}

/*
        A slice of the scene composed apart, stored to the card: the software
        pointer over it, then each row by memory_copy_changed_u32 against
        the mirror, which stores only the 64-byte chunks that differ from
        what the card holds, so the card is written (and a virtual card's
        host told) only where the frame changed, however large the damage.
        A stale mirror is stored whole.
*/
static void compositor_slice_flush(const struct compositor_target *slice, int *first, int *last)
{
        unsigned long width = (unsigned long)(slice->x2 - slice->x1);
        unsigned long pitch = compositor.scanout_pitch / 4;

        *first = *last = -1;
        // The fingers' marks over the windows and under the pointer.
        compositor_spots_draw(slice);
        if (compositor.cursor.drawn)
                compositor_cursor_draw(slice, compositor.cursor.x, compositor.cursor.y, compositor.cursor.drawn_shape,
                                       compositor.cursor.drawn_scale, compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));
        for (int y = slice->y1; y < slice->y2; y++)
        {
                const unsigned int *from = slice->pixels + (unsigned long)(y - slice->oy) * slice->pitch +
                                           (unsigned long)(slice->x1 - slice->ox);
                unsigned int *into = compositor.scanout + (unsigned long)y * pitch + (unsigned long)slice->x1;
                unsigned int *mirror = compositor.mirror + (unsigned long)y * compositor.mirror_pitch + (unsigned long)slice->x1;

                if (compositor.mirror_stale)
                {
                        memory_copy(into, from, width * 4);
                        if (mirror != into)
                                memory_copy(mirror, from, width * 4);
                }
                else if (!memory_copy_changed_u32(into, mirror, from, width))
                        continue;
                *first = *first < 0 ? y : *first;
                *last = y;
        }
}

// Whether the card's driver is one of count names.
static int compositor_driver_in(const char *const *names, unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int same = 0;

                while (names[at][same] && names[at][same] == compositor.driver[same])
                        same++;
                if (!names[at][same] && !compositor.driver[same])
                        return 1;
        }
        return 0;
}

/*
        Whether the scene is composed apart and stored to the card only where
        it changed (compositor_slice_flush), decided once by the card's driver.
        On where a store to the card is dear: a card with memory of its own
        mapped across the bus (amdgpu, radeon, nouveau, and the emulated
        cards' VRAM), and a card whose framebuffer is the machine's memory
        but which copies what it is told changed to the device or its host
        (virtio_gpu, simpledrm and the like), where the mirror is the
        scanout itself, read at the machine's speed. Off on i915 and xe,
        whose dumb buffer is the machine's memory and shown as it is, where
        composing apart is only one more pass, and with no card. The cost
        is a slice of rows a band (COMPOSITOR_SLICE), and the mirror's memory
        where it is apart.
*/
static int compositor_turning_software(void);

// What compositor_changed_ready mapped, given back; it is decided again at the next present.
static void compositor_apart_drop(void)
{
        if (compositor.composing)
                compositor_call(syscall(munmap), (positive)compositor.composing, compositor.composing_bytes, 0, 0, 0, 0);
        if (compositor.mirror && compositor.mirror_bytes)
                compositor_call(syscall(munmap), (positive)compositor.mirror, compositor.mirror_bytes, 0, 0, 0, 0);
        compositor.composing = compositor.mirror = 0;
        compositor.composing_bytes = compositor.mirror_bytes = 0;
        compositor.changed = 0;
}

static int compositor_changed_ready(void)
{
        static const char *const apart[] = {"amdgpu", "radeon", "nouveau", "bochs-drm", "qxl", "vmwgfx"};
        static const char *const shared[] = {"virtio_gpu", "simpledrm", "cirrus-qemu", "hyperv_drm", "ast",
                                             "mgag200", "udl", "gud"};
        unsigned long bytes = (unsigned long)compositor.width * (unsigned long)compositor.height * 4;
        unsigned long slices = (unsigned long)compositor.width * COMPOSITOR_SLICE * COMPOSITOR_BANDS * 4;
        int own, listed, turned = compositor_turning_software();

        if (compositor.changed && compositor.changed_turned == turned)
                return compositor.changed > 0;
        compositor_apart_drop();
        compositor.changed = -1;
        compositor.changed_turned = turned;
        if (compositor.card < 0)
                return 0;
        own = compositor_driver_in(apart, sizeof(apart) / sizeof(apart[0]));
        listed = own || compositor_driver_in(shared, sizeof(shared) / sizeof(shared[0]));
        // A screen Canvas turns reads the scene whole, a frame at a time, and a read of the card's memory is more than a
        // frame can pay (a dumb buffer is mapped write-combined on most cards, and word loads from one came to about ten
        // megabytes a second on a real card): with a screen turned, the scene is kept in the machine's memory whatever the
        // card, as the apart cards' is.
        if (!listed && !turned)
                return 0;
        own = own || !listed;

        long composing = compositor_call(syscall(mmap), 0, slices, 3, 0x22, (positive)-1, 0);

        if (composing < 0 && composing > -4096)
                return 0;
        compositor.composing = (unsigned int *)composing;
        compositor.composing_bytes = slices;
        compositor.mirror = compositor.scanout;
        compositor.mirror_bytes = 0;
        compositor.mirror_pitch = compositor.scanout_pitch / 4;
        if (own)
        {
                long mirror = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

                if (mirror < 0 && mirror > -4096)
                {
                        compositor_call(syscall(munmap), (positive)composing, slices, 0, 0, 0, 0);
                        compositor.composing = 0;
                        return 0;
                }
                compositor.mirror = (unsigned int *)mirror;
                compositor.mirror_bytes = bytes;
                compositor.mirror_pitch = (unsigned long)compositor.width;
                compositor.mirror_stale = 1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        compositor.changed = 1;
        return 1;
}

/*
        The tabs over a window shown straight, on a plane of their own where
        the card has an overlay: the band's rectangle composed (the window
        under it, the glass, the tabs) into a buffer of the card's, and the
        window's buffer and that one given to the card in one atomic commit,
        which does not wait for the vertical blank and whose completion is the
        flip's event. The window's buffer is then never written: nothing it
        drew is spoiled and nothing is kept to put back. A card with no
        overlay that takes XRGB8888 on this crtc, or one that refuses the
        commit, has the tabs patched in as above.
*/
#define COMPOSITOR_SET_CLIENT_CAP 0x4010640du
#define COMPOSITOR_GETPLANERESOURCES COMPOSITOR_DRM(0xb5u, 16u)
#define COMPOSITOR_GETPLANE COMPOSITOR_DRM(0xb6u, 32u)
#define COMPOSITOR_SETPLANE COMPOSITOR_DRM(0xb7u, 48u)
#define COMPOSITOR_GETPROPERTY COMPOSITOR_DRM(0xaau, 64u)
#define COMPOSITOR_OBJ_GETPROPERTIES COMPOSITOR_DRM(0xb9u, 32u)
#define COMPOSITOR_ATOMIC COMPOSITOR_DRM(0xbcu, 56u)
#define COMPOSITOR_OBJECT_PLANE 0xeeeeeeeeu
#define COMPOSITOR_ATOMIC_NONBLOCK 0x0200u

enum
{
        COMPOSITOR_PLANE_FB,
        COMPOSITOR_PLANE_CRTC,
        COMPOSITOR_PLANE_SRC_X,
        COMPOSITOR_PLANE_SRC_Y,
        COMPOSITOR_PLANE_SRC_W,
        COMPOSITOR_PLANE_SRC_H,
        COMPOSITOR_PLANE_CRTC_X,
        COMPOSITOR_PLANE_CRTC_Y,
        COMPOSITOR_PLANE_CRTC_W,
        COMPOSITOR_PLANE_CRTC_H,
        COMPOSITOR_PLANE_TYPE,
        COMPOSITOR_PLANE_PROPS
};

// A plane's property ids by name and the value of its type (0 overlay, 1 primary, 2 cursor); false when one is missing.
static int compositor_plane_props(unsigned int plane, unsigned int props[COMPOSITOR_PLANE_PROPS], unsigned long *type)
{
        static const char *const names[COMPOSITOR_PLANE_PROPS] = {"FB_ID",  "CRTC_ID", "SRC_X",  "SRC_Y",
                                                                   "SRC_W",  "SRC_H",   "CRTC_X", "CRTC_Y",
                                                                   "CRTC_W", "CRTC_H",  "type"};
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, plane, COMPOSITOR_OBJECT_PLANE};

        if (system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return 0;
        memory_zero(props, sizeof(unsigned int) * COMPOSITOR_PLANE_PROPS);
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        continue;
                for (unsigned int name = 0; name < COMPOSITOR_PLANE_PROPS; name++)
                {
                        unsigned int same = 0;

                        while (names[name][same] && names[name][same] == property.name[same])
                                same++;
                        if (names[name][same] || property.name[same])
                                continue;
                        props[name] = ids[at];
                        if (name == COMPOSITOR_PLANE_TYPE)
                                *type = values[at];
                }
        }
        for (unsigned int name = 0; name < COMPOSITOR_PLANE_PROPS; name++)
                if (!props[name])
                        return 0;
        return 1;
}

// The primary plane of this crtc and an overlay that takes XRGB8888 on it, found once; false when the card has none.
static int compositor_overlay_ready(void)
{
        unsigned int planes[32];
        struct
        {
                unsigned long ids;
                unsigned int count, pad;
        } resources = {(unsigned long)planes, 32, 0};
        unsigned long cap[2] = {3, 1}; // DRM_CLIENT_CAP_ATOMIC, which brings every plane with it

        if (compositor.overlay_state)
                return compositor.overlay_state > 0;
        compositor.overlay_state = -1;
        if (system_control(compositor.card, COMPOSITOR_SET_CLIENT_CAP, address_of cap) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETPLANERESOURCES, address_of resources) != 0 || resources.count > 32)
                return 0;

        for (unsigned int at = 0; at < resources.count; at++)
        {
                unsigned int formats[64], props[COMPOSITOR_PLANE_PROPS], takes = 0;
                unsigned long type = 3;
                struct
                {
                        unsigned int id, crtc, fb, possible, gamma, count;
                        unsigned long formats;
                } plane = {planes[at], 0, 0, 0, 0, 64, (unsigned long)formats};

                if (system_control(compositor.card, COMPOSITOR_GETPLANE, address_of plane) != 0 || plane.count > 64 ||
                    !(plane.possible & 1u << compositor.crtc_index) || !compositor_plane_props(planes[at], props, &type))
                        continue;
                for (unsigned int format = 0; format < plane.count; format++)
                        takes |= formats[format] == COMPOSITOR_XRGB8888;
                if (type == 1 && (!compositor.primary || plane.crtc == compositor.crtc))
                {
                        compositor.primary = planes[at];
                        memory_copy(compositor.primary_props, props, sizeof(props));
                }
                else if (type == 0 && takes && !compositor.overlay)
                {
                        compositor.overlay = planes[at];
                        memory_copy(compositor.overlay_props, props, sizeof(props));
                }
        }
        if (!compositor.primary || !compositor.overlay)
                return 0;
        compositor.overlay_state = 1;
        return 1;
}

// The overlay's two buffers, rows of the output's width each, made again when the band wants more rows.
static int compositor_overlay_buffers(unsigned int rows)
{
        if (rows <= compositor.overlay_rows)
                return 1;
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.overlay_fb[which])
                {
                        unsigned int close[2] = {compositor.overlay_handle[which], 0};

                        compositor_call(syscall(munmap), (positive)compositor.overlay_pixels[which], compositor.overlay_bytes, 0, 0, 0, 0);
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of compositor.overlay_fb[which]);
                        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                        compositor.overlay_fb[which] = 0;
                }
        compositor.overlay_rows = 0;
        rows = (rows + 63) & ~63u;
        for (unsigned int which = 0; which < 2; which++)
        {
                struct compositor_dumb dumb = {rows, (unsigned int)compositor.width, 32, 0, 0, 0, 0};
                struct compositor_map_dumb map = {0};
                struct compositor_fb2 fb = {0};
                long mapped;

                if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                        return 0;
                fb.width = dumb.width;
                fb.height = dumb.height;
                fb.format = COMPOSITOR_XRGB8888;
                fb.handles[0] = map.handle = dumb.handle;
                fb.pitches[0] = dumb.pitch;
                if (system_control(compositor.card, COMPOSITOR_ADDFB2, address_of fb) != 0 ||
                    system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                        return 0;
                mapped = compositor_call(syscall(mmap), 0, dumb.size, 3, 1, (positive)compositor.card, map.offset);
                if (mapped < 0 && mapped > -4096)
                        return 0;
                compositor.overlay_fb[which] = fb.fb_id;
                compositor.overlay_handle[which] = dumb.handle;
                compositor.overlay_pixels[which] = (unsigned int *)mapped;
                compositor.overlay_pitch = dumb.pitch / 4;
                compositor.overlay_bytes = dumb.size;
        }
        compositor.overlay_rows = rows;
        return 1;
}

// One enum of a property: its value, a bit for a bitmask, and its name.
struct compositor_enum
{
        unsigned long value;
        char name[32];
};

/*
        The enums of the property called name on object (of type), in the card's order, as many as room: the count, or 0
        when the object has no property by that name. Its id comes back in id.
*/
static unsigned int compositor_enums_of(unsigned int object, unsigned int type, const char *name, unsigned int *id,
                                        struct compositor_enum *enums, unsigned int room)
{
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, object, type};

        if (system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return 0;
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};
                unsigned int same = 0;

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        continue;
                while (name[same] && name[same] == property.name[same])
                        same++;
                if (name[same] || property.name[same])
                        continue;
                *id = ids[at];
                property.enums = (unsigned long)enums;
                property.count_enums = room;
                // The values are not wanted: the first call left their count in, and the kernel puts that many through the
                // pointer, which is null, so the call failed (EFAULT) and no card's rotation was ever found.
                property.values = 0;
                property.count_values = 0;
                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        return 0;
                return property.count_enums < room ? property.count_enums : room;
        }
        return 0;
}

/*
        The turn the card does for a screen on crtc_index, in hardware: the primary plane of that crtc with a rotation
        property that lists rotate-DEGREES (windows_rotation_bit), the property's id and the value to set it to. False
        when no such plane is: the screen is then turned in software.
*/
static int compositor_rotation_hardware(unsigned int crtc_index, unsigned int degrees, unsigned int *plane,
                                        unsigned int *property, unsigned long *value)
{
        unsigned int planes[32];
        struct
        {
                unsigned long ids;
                unsigned int count, pad;
        } resources = {(unsigned long)planes, 32, 0};
        // DRM_CLIENT_CAP_ATOMIC, which the turn's commit needs and which brings every plane with it: without it the card lists
        // no primary plane, and the first take of the screens is before compositor_overlay_ready has ever asked for it.
        unsigned long cap[2] = {3, 1};

        if (system_control(compositor.card, COMPOSITOR_SET_CLIENT_CAP, address_of cap) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETPLANERESOURCES, address_of resources) != 0 || resources.count > 32)
                return 0;
        for (unsigned int at = 0; at < resources.count; at++)
        {
                unsigned int formats[64], props[COMPOSITOR_PLANE_PROPS], id = 0, count;
                unsigned long type = 3, bits[8] = {0};
                struct compositor_enum enums[8];
                const char *names[8] = {0};
                struct
                {
                        unsigned int id, crtc, fb, possible, gamma, count;
                        unsigned long formats;
                } plane_info = {planes[at], 0, 0, 0, 0, 64, (unsigned long)formats};

                if (system_control(compositor.card, COMPOSITOR_GETPLANE, address_of plane_info) != 0 || plane_info.count > 64 ||
                    !(plane_info.possible & 1u << crtc_index) || !compositor_plane_props(planes[at], props, &type) || type != 1)
                        continue;
                count = compositor_enums_of(planes[at], COMPOSITOR_OBJECT_PLANE, "rotation", &id, enums, 8);
                for (unsigned int each = 0; each < count; each++)
                        names[each] = enums[each].name, bits[each] = enums[each].value;
                *value = windows_rotation_bit(names, bits, count, degrees);
                if (!*value)
                        continue;
                *plane = planes[at];
                *property = id;
                return 1;
        }
        return 0;
}

/*
        A screen the card turns: its plane shows the screen's rectangle of the scanout (source, in the scanout's pixels,
        and width by height) at the panel's own size, with the rotation property set to value, in one commit. False
        when the card refuses it.
*/
static int compositor_rotate_commit(const struct compositor_output *output, const int *source, unsigned int width,
                                    unsigned int height, unsigned int fb)
{
        unsigned int props[COMPOSITOR_PLANE_PROPS], ids[11], plane = output->turn_plane, n = 0;
        unsigned long type = 0, values[11];
        unsigned int objects[1] = {plane}, counts[1];
        struct
        {
                unsigned int flags, count;
                unsigned long objects, counts, props, values, reserved, user_data;
        } commit;

        if (!compositor_plane_props(plane, props, &type))
                return 0;
        ids[n] = props[COMPOSITOR_PLANE_FB], values[n++] = fb;
        ids[n] = props[COMPOSITOR_PLANE_CRTC], values[n++] = output->crtc;
        ids[n] = props[COMPOSITOR_PLANE_SRC_X], values[n++] = (unsigned long)source[0] << 16;
        ids[n] = props[COMPOSITOR_PLANE_SRC_Y], values[n++] = (unsigned long)source[1] << 16;
        ids[n] = props[COMPOSITOR_PLANE_SRC_W], values[n++] = (unsigned long)width << 16;
        ids[n] = props[COMPOSITOR_PLANE_SRC_H], values[n++] = (unsigned long)height << 16;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_X], values[n++] = 0;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_Y], values[n++] = 0;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_W], values[n++] = output->mode.hdisplay;
        ids[n] = props[COMPOSITOR_PLANE_CRTC_H], values[n++] = output->mode.vdisplay;
        ids[n] = output->turn_property, values[n++] = output->turn_value;
        counts[0] = n;
        commit.flags = 0;
        commit.count = 1;
        commit.objects = (unsigned long)objects;
        commit.counts = (unsigned long)counts;
        commit.props = (unsigned long)ids;
        commit.values = (unsigned long)values;
        commit.reserved = commit.user_data = 0;
        return system_control(compositor.card, COMPOSITOR_ATOMIC, address_of commit) == 0;
}

/*
        One commit: fb on the primary plane and, where overlay says, the
        overlay's buffer which at rect, or the overlay off (rect none). It
        does not wait, and its event is the flip's; false when the card
        refuses it.
*/
static int compositor_atomic_to(unsigned int fb, int row, unsigned int which, int overlay, unsigned int shown,
                                const int *source, const int *rect)
{
        unsigned int objects[2] = {compositor.primary, compositor.overlay}, counts[2] = {1, 0}, props[12];
        unsigned long values[12];
        unsigned int n = 0;

        props[n] = compositor.primary_props[COMPOSITOR_PLANE_FB], values[n++] = fb;
        if (overlay)
        {
                const unsigned int *id = compositor.overlay_props;
                unsigned long width = rect ? (unsigned long)(rect[2] - rect[0]) : 0;
                unsigned long height = rect ? (unsigned long)(rect[3] - rect[1]) : 0;

                props[n] = id[COMPOSITOR_PLANE_FB], values[n++] = rect ? shown : 0;
                props[n] = id[COMPOSITOR_PLANE_CRTC], values[n++] = rect ? compositor.crtc : 0;
                if (rect)
                {
                        props[n] = id[COMPOSITOR_PLANE_SRC_X], values[n++] = (unsigned long)source[0] << 16;
                        props[n] = id[COMPOSITOR_PLANE_SRC_Y], values[n++] = (unsigned long)source[1] << 16;
                        props[n] = id[COMPOSITOR_PLANE_SRC_W], values[n++] = width << 16;
                        props[n] = id[COMPOSITOR_PLANE_SRC_H], values[n++] = height << 16;
                        props[n] = id[COMPOSITOR_PLANE_CRTC_X], values[n++] = (unsigned long)rect[0];
                        props[n] = id[COMPOSITOR_PLANE_CRTC_Y], values[n++] = (unsigned long)rect[1];
                        props[n] = id[COMPOSITOR_PLANE_CRTC_W], values[n++] = width;
                        props[n] = id[COMPOSITOR_PLANE_CRTC_H], values[n++] = height;
                }
                counts[1] = n - 1;
        }

        struct
        {
                unsigned int flags, count;
                unsigned long objects, counts, props, values, reserved, user_data;
        } commit = {COMPOSITOR_FLIP_EVENT | COMPOSITOR_ATOMIC_NONBLOCK, overlay ? 2u : 1u, (unsigned long)objects,
                    (unsigned long)counts, (unsigned long)props, (unsigned long)values, 0, 0};

        if (system_control(compositor.card, COMPOSITOR_ATOMIC, address_of commit) != 0)
                return 0;
        compositor.flip_pending = 1;
        compositor.flip_row = row;
        compositor.flip_buffer = which;
        compositor.on_screen = fb;
        if (overlay)
        {
                compositor.overlay_on = rect != 0;
                compositor.plane_fb = 0;
                if (rect)
                        memory_copy(compositor.overlay_rect, rect, sizeof(compositor.overlay_rect));
        }
        return 1;
}

// The tabs' buffer which on the overlay at rect, read from the same place in it.
static int compositor_atomic(unsigned int fb, int row, unsigned int which, int overlay, const int *rect, unsigned int buffer)
{
        int source[2] = {rect ? rect[0] : 0, 0};

        return compositor_atomic_to(fb, row, which, overlay, compositor.overlay_fb[buffer], source, rect);
}

// The overlay off where it is on and the commits that would take it off are refused: the plane set to nothing.
static void compositor_overlay_off(void)
{
        unsigned int off[12] = {compositor.overlay, compositor.crtc};

        if (!compositor.overlay_on)
                return;
        system_control(compositor.card, COMPOSITOR_SETPLANE, address_of off);
        compositor.overlay_on = 0;
        compositor.plane_fb = 0;
}

// The rectangle the tabs cover, on the output; false when they cover none of it.
static int compositor_tabs_rect(const struct compositor_frame *tabs, unsigned int count, int rect[4])
{
        rect[0] = compositor.width, rect[1] = compositor.height, rect[2] = rect[3] = 0;
        for (unsigned int at = 0; at < count; at++)
        {
                int x, y, width, height;

                compositor_outline(&tabs[at], &x, &y, &width, &height);
                rect[0] = x < rect[0] ? x : rect[0];
                rect[1] = y < rect[1] ? y : rect[1];
                rect[2] = x + width > rect[2] ? x + width : rect[2];
                rect[3] = y + height > rect[3] ? y + height : rect[3];
        }
        rect[0] = rect[0] < 0 ? 0 : rect[0];
        rect[1] = rect[1] < 0 ? 0 : rect[1];
        rect[2] = rect[2] > compositor.width ? compositor.width : rect[2];
        rect[3] = rect[3] > compositor.height ? compositor.height : rect[3];
        return rect[2] > rect[0] && rect[3] > rect[1];
}

// The frame of a window shown straight with the tabs on the overlay: false when the card will not, and the tabs are
// then patched in.
static b32 compositor_present_overlaid(int row, const struct compositor_frame *frames, unsigned int count, unsigned int windows)
{
        unsigned int which = compositor.front[row], fb = compositor_window_fb(row, which), buffer = compositor.overlay_back;
        int rect[4], shown = compositor_tabs_rect(frames + windows, count - windows, rect), drawn = 0;
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_dirty dirty = {fb, 0, 0, 0, (unsigned long)clip};

        if (!fb)
                return 0;
        // The card is still on its way to the last commit: this frame is made once it is there.
        if (compositor.flip_pending)
                return 1;

        if (shown)
        {
                drawn = !compositor.overlay_on || memory_compare(compositor.overlay_rect, rect, sizeof(rect)) != 0;
                for (unsigned int at = 0; at < compositor.damages && !drawn; at++)
                {
                        int meet[4];

                        memory_copy(meet, compositor.damage[at], sizeof(meet));
                        compositor_meet(meet, rect);
                        drawn = meet[2] > meet[0] && meet[3] > meet[1];
                }
                if (drawn)
                {
                        struct compositor_target target = {compositor.overlay_pixels[buffer], 0, compositor.width,
                                                           compositor.height, rect[0], rect[1], rect[2], rect[3], 0, rect[1], 0};
                        int rows[2];

                        if (!compositor_overlay_buffers((unsigned int)(rect[3] - rect[1])))
                        {
                                compositor.overlay_state = -1;
                                return 0;
                        }
                        target.pixels = compositor.overlay_pixels[buffer];
                        target.pitch = compositor.overlay_pitch;
                        compositor_scene_banded(&target, frames, count, 0, rows);
                }
        }
        if (fb != compositor.on_screen || drawn || (!shown && compositor.overlay_on))
        {
                if (!compositor_atomic(fb, row, which, drawn || !shown, shown ? rect : 0, buffer))
                {
                        compositor.overlay_state = -1;
                        compositor_overlay_off();
                        return 0;
                }
                if (drawn)
                        compositor.overlay_back ^= 1;
        }
        compositor.direct_row = row;
        compositor.table->scanned++;

        for (unsigned int at = 0; at < compositor.damages; at++)
                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)compositor.damage[at][side];
        dirty.clip_count = compositor.damages;
        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        compositor.damages = 0;
        compositor.screen_stale = 1;
        return 1;
}

// The card's first picture is drawn before the compositor has made its window table, so what the table is to say of that
// present has nowhere to go yet.
static unsigned int compositor_unscanned_spare;

static void compositor_unscanned(unsigned int why)
{
        if (compositor.table)
                compositor.table->unscanned = why;
}

/*
        A window that does not fill the output on the overlay: the card shows
        the top window's middle straight from its buffer, over the
        compositor's own screen, and the compositor composes everything else
        and nothing under it -- where it copied the whole window every frame.
        The window is the top one, so nothing of the scene is over it but the
        pointer, on its own plane; opaque, at the size it drew, wholly on the
        output and clear of the tabs. What the overlay shows is the window
        less its rows with corners, its close button and its line, which the
        compositor still draws round it. A card with no overlay, or one that
        refuses, has the window composed as any other.
*/
static int compositor_plane_candidate(const struct compositor_frame *frames, const unsigned char *rows,
                                      unsigned int windows, int rect[4])
{
        struct compositor_frame band;
        int row, bx, by, side, radius, top, bottom;

        unsigned int *why = compositor.table ? &compositor.table->unscanned : &compositor_unscanned_spare;

        if (compositor.card < 0 || compositor.direct_refused || !compositor.table_memfd)
                return *why = compositor.card < 0 ? 1 : compositor.direct_refused ? 11 : 2, -1;
        if (compositor_output_count() > 1)
                return *why = 13, -1;
        if (compositor.cursor.soft)
                return *why = 3, -1;
        if (!windows || compositor.drag >= 0 || compositor.resize >= 0 || compositor.preview_draw)
                return *why = 4, -1; // a preview is over the window, and the overlay is not composed under it
        if (compositor_spots_any(0))
                return *why = 14, -1;
        row = rows[windows - 1];
        const struct compositor_frame *frame = &frames[windows - 1];

        if (compositor.table->glass[row])
                return *why = 5, -1;
        if (compositor.landing[row])
                return *why = 4, -1; // a window landing is drawn smaller than its picture
        if (compositor.asking[row] || compositor.picture_w[row] != compositor.w[row] ||
            compositor.picture_h[row] != compositor.h[row])
                return *why = 6, -1;
        if (frame->x < 0 || frame->y < 0 || frame->x + frame->width > compositor.width ||
            frame->y + frame->height > compositor.height)
                return *why = 7, -1;
        radius = compositor_radius(frame);
        top = frame->y + radius;
        bottom = frame->y + frame->height - radius;
        if (compositor_close_box(frame, &bx, &by, &side))
                top = by + side > top ? by + side : top;
        if (bottom - top < 64 * compositor.scale / 100)
                return *why = 8, -1;
        rect[0] = frame->x, rect[1] = top, rect[2] = frame->x + frame->width, rect[3] = bottom;

        compositor_band_frame(&band);
        if (compositor.band && band.x < rect[2] && band.x + band.width > rect[0] && band.y < rect[3] &&
            band.y + band.height > rect[1])
                return *why = 9, -1;
        // The card asked last: whether it has an overlay, then the window's framebuffer.
        if (!compositor_overlay_ready())
                return *why = 10, -1;
        return compositor_window_fb(row, compositor.front[row]) ? row : (*why = 11, -1);
}

// The damage less the rectangle the overlay shows; false when the pieces do not fit the list, which is then left whole.
static int compositor_damage_cut(const int *rect)
{
        int pieces[COMPOSITOR_DAMAGE][4];
        unsigned int count = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *d = compositor.damage[at];
                int cut[4][4] = {{d[0], d[1], d[2], rect[1]},
                                 {d[0], rect[3], d[2], d[3]},
                                 {d[0], rect[1] > d[1] ? rect[1] : d[1], rect[0], rect[3] < d[3] ? rect[3] : d[3]},
                                 {rect[2], rect[1] > d[1] ? rect[1] : d[1], d[2], rect[3] < d[3] ? rect[3] : d[3]}};

                if (d[2] <= rect[0] || d[0] >= rect[2] || d[3] <= rect[1] || d[1] >= rect[3])
                {
                        if (count == COMPOSITOR_DAMAGE)
                                return 0;
                        memory_copy(pieces[count++], d, sizeof(pieces[0]));
                        continue;
                }
                for (unsigned int k = 0; k < 4; k++)
                {
                        int *c = cut[k];

                        c[0] = c[0] > d[0] ? c[0] : d[0], c[2] = c[2] < d[2] ? c[2] : d[2];
                        c[1] = c[1] > d[1] ? c[1] : d[1], c[3] = c[3] < d[3] ? c[3] : d[3];
                        if (c[2] <= c[0] || c[3] <= c[1])
                                continue;
                        if (count == COMPOSITOR_DAMAGE)
                                return 0;
                        memory_copy(pieces[count++], c, sizeof(pieces[0]));
                }
        }
        memory_copy(compositor.damage, pieces, sizeof(pieces[0]) * count);
        compositor.damages = count;
        return 1;
}

/*
        A change told to the card a framebuffer at a time, on more than one
        screen: each framebuffer is told what of the change is on the screens
        that show it, and none that shows nothing of it is told anything, so
        the commit a DIRTYFB is waits on the vertical blanks of the screens
        that changed and on no other's. A screen that has no framebuffer of
        its own shares the scanout's with the primary, and is told with it.
*/
static void compositor_dirty_screens(const unsigned short (*clip)[4], unsigned int clips)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
        {
                unsigned int fb = compositor.output[at].fb ? compositor.output[at].fb : compositor.fb, told = 0, users = 0;

                // A screen Canvas turns is told by compositor_turn_frame, on its own buffer.
                if (compositor.output[at].turned == 2)
                        continue;
                unsigned short mine[COMPOSITOR_DAMAGE][4];
                int seen = 0;

                for (unsigned int before = 0; before < at && !seen; before++)
                        seen = (compositor.output[before].fb ? compositor.output[before].fb : compositor.fb) == fb;
                if (seen)
                        continue;
                for (unsigned int screen = at; screen < compositor.outputs; screen++)
                {
                        unsigned short cut[COMPOSITOR_DAMAGE][4];
                        unsigned int count;

                        if ((compositor.output[screen].fb ? compositor.output[screen].fb : compositor.fb) != fb)
                                continue;
                        users++;
                        count = compositor_screen_clips(compositor.screen[screen], clip, clips, cut);
                        for (unsigned int each = 0; each < count && told < COMPOSITOR_DAMAGE; each++)
                                memory_copy(mine[told++], cut[each], sizeof(cut[each]));
                }
                // A framebuffer two screens share is told the change whole, as one framebuffer always was.
                if (users > 1)
                {
                        memory_copy(mine, clip, clips * sizeof(clip[0]));
                        told = clips;
                }
                if (told)
                {
                        struct compositor_dirty dirty = {fb, 0, 0, told, (unsigned long)mine};

                        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
                }
        }
}

// A screen Canvas turns in software: its framebuffer over the buffer its panel is drawn in, by screen (see
// compositor_turn_buffer).
struct compositor_turn
{
        unsigned int fb, handle;
        unsigned int *pixels;
        unsigned long bytes, pitch; // pitch in bytes
        unsigned int width, height;
};
static struct compositor_turn compositor_turns[WINDOWS_OUTPUTS];

// The framebuffer the primary plane shows when the overlay is taken off it: the scanout, or the buffer Canvas turns the
// first screen in.
static unsigned int compositor_primary_fb(void)
{
        return compositor.outputs && compositor.output[0].turned == 2 ? compositor.output[0].fb : compositor.fb;
}

// Whether any screen is turned, by the card or by Canvas. Then no window is shown straight and no overlay is put up.
static int compositor_turning(void)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor.output[at].turned)
                        return 1;
        return 0;
}

// Whether any screen is turned by Canvas itself: its picture is made from the scene, a frame at a time.
static int compositor_turning_software(void)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor.output[at].turned == 2)
                        return 1;
        return 0;
}

/*
        The part of the scene a turned screen shows, turned into its own buffer and told to its framebuffer: each rectangle
        of the damage (clip), cut to the screen, is turned for the panel (windows_rotate_u32) and the panel's rectangle of it
        is given to the card, all of a screen's in one DIRTYFB. A rectangle is turned alone: two far apart (the pointer's old
        place and its new, the clock) are two small ones and not the one box that holds them both, which was most of the
        screen. The scene is composed on the scanout first, so the pointer and the tabs are in it, and it is turned from the
        machine's memory: the mirror of what the card holds where Canvas keeps one (compositor_changed_ready, which it does
        for every card while a screen is turned), the scanout itself where that is the machine's memory. The card's own
        memory is only ever written: a read of it came to about ten megabytes a second on a real card, and a frame of
        2560 by 1080 a second.
*/
#ifdef MOONWATER_GPU_C
static int compositor_gpu_rotate(unsigned int at, const int *screen, unsigned int degrees, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
#endif
// gpu: the scene was composed by the GPU into the scanout (there is no mirror of it then), and the GPU turns it.
static void compositor_turn_frame(const unsigned short (*clip)[4], unsigned int clips, int gpu)
{
        const unsigned int *scene = !gpu && compositor.changed > 0 && compositor.mirror ? compositor.mirror : compositor.scanout;
        unsigned long pitch = !gpu && compositor.changed > 0 && compositor.mirror ? compositor.mirror_pitch : compositor.scanout_pitch / 4;

        for (unsigned int at = 0; at < compositor.outputs && clips; at++)
        {
                const int *screen = compositor.screen[at];
                unsigned int degrees = compositor.output[at].degrees, told = 0;
                unsigned short rect[COMPOSITOR_DAMAGE][4];

                if (compositor.output[at].turned != 2)
                        continue;
                for (unsigned int each = 0; each < clips && told < COMPOSITOR_DAMAGE; each++)
                {
                        unsigned int x0 = clip[each][0] > (unsigned int)screen[0] ? clip[each][0] : (unsigned int)screen[0];
                        unsigned int y0 = clip[each][1] > (unsigned int)screen[1] ? clip[each][1] : (unsigned int)screen[1];
                        unsigned int x1 = clip[each][2] < (unsigned int)(screen[0] + screen[2]) ? clip[each][2]
                                                                                              : (unsigned int)(screen[0] + screen[2]);
                        unsigned int y1 = clip[each][3] < (unsigned int)(screen[1] + screen[3]) ? clip[each][3]
                                                                                              : (unsigned int)(screen[1] + screen[3]);
                        unsigned int panel[4];

                        if (x0 >= x1 || y0 >= y1)
                                continue;
#ifdef MOONWATER_GPU_C
                        if (!gpu || !compositor_gpu_rotate(at, screen, degrees, x0 - screen[0], y0 - screen[1], x1 - screen[0], y1 - screen[1]))
#endif
                        windows_rotate_u32(compositor_turns[at].pixels, compositor_turns[at].pitch / 4,
                                           scene + (unsigned long)screen[1] * pitch + (unsigned long)screen[0], pitch,
                                           (unsigned int)screen[2], (unsigned int)screen[3], degrees, x0 - screen[0],
                                           y0 - screen[1], x1 - screen[0], y1 - screen[1]);
                        windows_rotate_panel((unsigned int)screen[2], (unsigned int)screen[3], degrees, x0 - screen[0],
                                             y0 - screen[1], x1 - screen[0], y1 - screen[1], panel);
                        for (unsigned int side = 0; side < 4; side++)
                                rect[told][side] = (unsigned short)panel[side];
                        told++;
                }
                if (told)
                {
                        struct compositor_dirty dirty = {compositor_turns[at].fb, 0, 0, told, (unsigned long)rect};

                        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
                }
        }
}

#ifdef MOONWATER_GPU_C
/*
        The present on the GPU (src/canvas/gpu.c): the frames of a present as the records of its scene (compositor_gpu_frames) and the pointer when the scene
        has it (software), drawn by the card straight into the scanout -- the display node's dumb buffer, which the render node is given by PRIME, or a headless file's pages as a userptr buffer -- over the union of
        the damage, and the card told what changed as it is when the processor drew it. It is for what it draws exactly as the processor does (every frame is a
        window with a picture at its own size, a tab, a line, a close button); anything else, a finger's mark, a scaled landing, a turned screen, a
        mode whose width is not a multiple of 4, a card of another family, a refusal or a hang, is the processor's, for that present, and three hangs in a row
        end the GPU's compose until the setting is turned off and on again (compositor_gpu_settings). Headless it is for -G.
*/
static int compositor_gpu; // GPU rendering as the setting has it (below)
static void compositor_say(string_address what, bipolar value);
#define COMPOSITOR_GPU_OPS_AT GPU_DATA
#define COMPOSITOR_GPU_TABLES_AT (GPU_DATA + (1 << 20))
#define COMPOSITOR_GPU_BINS_AT (GPU_DATA + (2 << 20))
static struct
{
        int up, dead, force, told; // started; given up; headless is to use it (-G); said once that it composes
        unsigned int failures;     // hangs in a row
        positive out, out_key;     // the scanout's address and what it was made from (a handle, or the file's pointer)
        positive turn_va[WINDOWS_OUTPUTS], turn_key[WINDOWS_OUTPUTS]; // the same for the buffers Canvas turns screens in
} compositor_gpu_state;

// The GPU started once, on a card it has kernels for, with the pools laid out in its buffer.
static int compositor_gpu_up(void)
{
        struct compositor_gscene *g = &compositor_gscene;

        if (compositor_gpu_state.up || compositor_gpu_state.dead)
                return compositor_gpu_state.up;
        if (!gpu_start((2 << 20) + (16 << 20)))
        {
                compositor_gpu_state.dead = 1;
                compositor_say(gpu.broken ? "the GPU is not trusted, its self-test drew wrong, family" : "no card the GPU's kernels are for, the processor composes", gpu.family);
                return 0;
        }
        g->ops = (unsigned int *)((unsigned char *)gpu.at + COMPOSITOR_GPU_OPS_AT), g->room = (1 << 20) / 32;
        g->tables = (unsigned char *)gpu.at + COMPOSITOR_GPU_TABLES_AT, g->tables_at = COMPOSITOR_GPU_TABLES_AT, g->tables_size = 1 << 20;
        compositor_gpu_state.up = 1;
        return 1;
}

// The scanout's address for the GPU, made when the scanout is new.
static positive compositor_gpu_output(void)
{
        positive key = compositor.card < 0 ? (positive)compositor.scanout : compositor.scanout_handle;

        if (compositor.card < 0)
        {
                positive at = gpu_va_bytes(compositor.scanout, compositor.scanout_bytes);

                if (!at && gpu_map_written(compositor.scanout, compositor.scanout_bytes))
                        at = gpu_va_bytes(compositor.scanout, compositor.scanout_bytes);
                compositor_gpu_state.out = at, compositor_gpu_state.out_key = key;
        }
        else if (compositor_gpu_state.out_key != key || !compositor_gpu_state.out)
        {
                gpu_unimport(compositor_gpu_state.out);
                compositor_gpu_state.out = gpu_import(compositor.card, compositor.scanout_handle, compositor.scanout_bytes), compositor_gpu_state.out_key = key;
        }
        return compositor_gpu_state.out;
}

// A frame's picture, wholly, reachable by the GPU: mapped the first time it is asked for.
static int compositor_gpu_picture(const struct compositor_frame *f)
{
        positive bytes;

        if (f->builtin)
                return 1;
        if (!f->pixels || f->width <= 0 || f->height <= 0 || f->pitch >= 1ull << 29)
                return 0;
        // The kernels reach a picture's pixels by a 32-bit signed offset from the first they are given (gpu.asm's image, mix, close and turn ops):
        // a picture of 2 GiB or more would wrap to bytes before it, so it is the processor's.
        bytes = 4 * (f->pitch * (positive)(f->height - 1) + (positive)f->width);
        if (bytes >= 1ull << 31)
                return 0;
        // A tab is a slice of its band's pixels (compositor_panel_tabs): the band is mapped whole, once, not a map a tab. The kernel looks at the pages
        // of every map at every job: on the box's 610M 34 tab slices cost each present 68 us more, the band as one map nothing to speak of.
        for (int at = 0; at < WINDOWS_OUTPUTS && !compositor_gpu_va(f); at++)
        {
                const unsigned int *band = at ? compositor.band_more[at] : compositor.band;
                unsigned long had = at ? compositor.band_more_bytes[at] : compositor.band_bytes;

                if (band && f->pixels >= band && f->pixels < band + had / 4)
                        gpu_map(band, had);
        }
        return compositor_gpu_va(f) || (gpu_map(f->pixels, bytes) && compositor_gpu_va(f));
}

// The present's damage drawn by the GPU: 1 when it is, 0 when the processor is to draw it.
static int compositor_gpu_present(const struct compositor_frame *frames, unsigned int count)
{
        struct compositor_gscene *g = &compositor_gscene;
        int x1 = compositor.width, y1 = compositor.height, x2 = 0, y2 = 0;
        positive out;

        if (!compositor_gpu || compositor_gpu_state.dead || (compositor.card < 0 && !compositor_gpu_state.force) ||
            compositor_spots_any(0) || (compositor.width & 3) || (compositor.scanout_pitch & 15))
                return 0;
        if (!compositor_gpu_up() || !gpu_idle()) // a job given up on may still read the buffer: no record is written into it until it is done
                return 0;
        // A present is a tick of the maps' (gpu_trim) whether the GPU draws it or not: a run of presents the processor draws (a window landing, scaled) made
        // maps of the pictures and let none go, and once all were held the GPU drew nothing again.
        gpu_trim();
        if (!(out = compositor_gpu_output()))
                return 0;
        g->width = (unsigned int)compositor.width, g->height = (unsigned int)compositor.height;
        for (unsigned int k = 0; k < count; k++)
                if (!compositor_gpu_picture(&frames[k]))
                        return 0;
        if (!compositor_gpu_frames(frames, count, compositor_ground))
                return 0;
        if (compositor.cursor.drawn)
                compositor_gpu_cursor(compositor.cursor.x, compositor.cursor.y, compositor.cursor.drawn_shape, compositor.cursor.drawn_scale, compositor_cursor_ink(0, 0),
                                      compositor_cursor_ink(1, 0));
        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *d = compositor.damage[at];

                x1 = d[0] < x1 ? d[0] : x1, y1 = d[1] < y1 ? d[1] : y1, x2 = d[2] > x2 ? d[2] : x2, y2 = d[3] > y2 ? d[3] : y2;
        }
        if (x2 <= x1 || y2 <= y1)
                return 0;
        if (!gpu_scene(COMPOSITOR_GPU_OPS_AT, g->count, COMPOSITOR_GPU_BINS_AT, out, (unsigned int)(compositor.scanout_pitch / 4), (unsigned int)compositor.width,
                       (unsigned int)compositor.height, (unsigned int)x1, (unsigned int)y1, (unsigned int)x2, (unsigned int)y2))
        {
                if (++compositor_gpu_state.failures >= 3)
                        compositor_gpu_state.dead = 1;
                return 0;
        }
        if (!compositor_gpu_state.failures && !compositor_gpu_state.told)
                compositor_gpu_state.told = 1, compositor_say("the GPU composes the desktop, frame records", g->count);
        compositor_gpu_state.failures = 0;
        return 1;
}

// A screen Canvas turns, the damage of it (x0, y0, x1, y1 of the screen) turned from the scanout into the screen's own buffer by the GPU (windows_rotate_u32's turn:
// one op over the whole panel with a step along its rows and one down them): 1 when it is, 0 when the processor is to.
static int compositor_gpu_rotate(unsigned int at, const int *screen, unsigned int degrees, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1)
{
        struct compositor_turn *turn = &compositor_turns[at];
        struct compositor_gscene *g = &compositor_gscene;
        unsigned int lw = (unsigned int)screen[2], lh = (unsigned int)screen[3], pw = degrees == 180 ? lw : lh, ph = degrees == 180 ? lh : lw, panel[4];
        long pitch = (long)(compositor.scanout_pitch / 4), origin = (long)screen[1] * pitch + screen[0], first, stepx, stepy;
        unsigned int *op;
        positive from, to;

        if (!compositor_gpu_state.up || !compositor_gpu_state.out || compositor.card < 0)
                return 0;
        if (compositor_gpu_state.turn_key[at] != turn->handle || !compositor_gpu_state.turn_va[at])
        {
                gpu_unimport(compositor_gpu_state.turn_va[at]);
                compositor_gpu_state.turn_va[at] = gpu_import(compositor.card, turn->handle, turn->bytes), compositor_gpu_state.turn_key[at] = turn->handle;
        }
        if (!compositor_gpu_state.turn_va[at] || (degrees != 90 && degrees != 180 && degrees != 270))
                return 0;
        if (degrees == 90)
                stepx = -pitch, stepy = 1, first = origin + ((long)lh - 1) * pitch;
        else if (degrees == 270)
                stepx = pitch, stepy = -1, first = origin + (long)lw - 1;
        else
                stepx = -1, stepy = -pitch, first = origin + ((long)lh - 1) * pitch + (long)lw - 1;
        from = compositor_gpu_state.out + 4ull * (positive)first;
        to = compositor_gpu_state.turn_va[at];
        op = g->ops;
        op[0] = 7u | (unsigned int)(stepx + 0x800000) << 8, op[1] = op[2] = 0, op[3] = pw, op[4] = ph;
        op[5] = (unsigned int)from, op[6] = (unsigned int)(from >> 32), op[7] = (unsigned int)stepy;
        windows_rotate_panel(lw, lh, degrees, x0, y0, x1, y1, panel);
        return gpu_scene(COMPOSITOR_GPU_OPS_AT, 1, COMPOSITOR_GPU_BINS_AT, to, (unsigned int)(turn->pitch / 4), pw, ph, panel[0], panel[1], panel[2], panel[3]);
}
#endif

/*
        The damage, composed straight onto the scanout and then announced.
        The scanout is a mapping of the card's memory, so it is only ever
        written: every pixel of the damage once, by the stores of
        memory_fill_u32_rect and memory_copy (full 64-byte lines where a row
        is long enough, a masked store or two overlapping ones at the ends),
        none non-temporal and none a read. The card is then told the
        rectangles that changed, in one call: a virtual card copies nothing
        to its host without that.

        The windows go bottom to top, and the tabs over all of them.
*/
static void compositor_present(void)
{
        struct compositor_frame frames[COMPOSITOR_FRAMES];
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_dirty dirty = {compositor.fb, 0, 0, 0, (unsigned long)clip};
        struct compositor_frame band;
        struct compositor_frame preview;
        unsigned char rows[WINDOWS];
        unsigned int count = 0, windows;
        int tinted;

        if (!compositor.damages)
                return;
        compositor_bands_follow();

        // What moonwater debug says of the card, once there is a table to say it in.
        if (compositor.table)
        {
                memory_copy(compositor.table->driver, compositor.driver, sizeof(compositor.table->driver));
                compositor.table->pointer_plane = !compositor.cursor.soft;
                compositor.table->overlay = compositor.overlay_state;
        }

        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor_visible(compositor.stack[at]))
                {
                        rows[count] = compositor.stack[at];
                        compositor_frame_of(compositor.stack[at], &frames[count++]);
                }
        windows = count;
        // The snap preview is over every window and the tabs: it leaves its window's frame for one of its own, after the tabs.
        tinted = compositor_preview_lift(frames, windows, &preview);
        // A window the card shows straight has what the tabs covered put back, so the glass is made from the window.
        compositor_direct_unpatch();
        compositor_glass_pass(frames, rows, windows);
        // After the glass, which puts a glass window's own composed picture in its frame: that one has no runs.
        compositor_runs_note(frames, rows, windows);

        // What is under the tabs changed, or what they say: the glass is made again over it, and shown. The glass
        // is made from the scene over the tabs' span and the blur's reach round it, so only a change there is one.
        // Each band in turn, with whether all are stale read once: the first one drawn says they are not.
        {
                int stale = taskbar_stale(), bands = compositor_bands();

                for (compositor.band_at = 0; compositor.band_at < bands; compositor.band_at++)
                {
                        struct taskbar_view view;
                        int span[4];
                        unsigned long redo = stale ? COMPOSITOR_TABS_ALL : 0;

                        compositor_band_frame(&band);
                        view = compositor_taskbar_view();
                        if (!redo && view.pixels && taskbar_span(&view, &span[0], &span[1], &span[2], &span[3]))
                                redo = taskbar_meets(&view, compositor.damage, compositor.damages);
                        if (redo)
                                compositor_band_draw(frames, windows, redo);
                }
                for (compositor.band_at = 0; compositor.band_at < bands; compositor.band_at++)
                {
                        compositor_band_frame(&band);
                        count += compositor_panel_tabs(&band, &frames[count]);
                }
                compositor.band_at = 0;
        }
        if (tinted)
                frames[count++] = preview;

        // The snap assist and the grid, over the windows, the tabs and the snap preview: a thumbnail's picture is drawn again each present. They
        // fade in when they come up and out when they go (compositor_overlay_fade), a thumbnail with them.
        if ((unsigned int)compositor_overlays_up() != compositor.overlays_up)
        {
                compositor.overlays_up = (unsigned int)compositor_overlays_up();
                // Up with no assist, the thumbnails of an earlier one are none; down, they fade out with the rest (or go now).
                if (compositor.overlays_up && !compositor.assist_on)
                        compositor.assist_count = 0;
                if (!compositor.overlays_up && !compositor_glides())
                        compositor.assist_count = 0;
                compositor_overlay_fade(compositor.overlays_up ? 100 : 0);
        }
        if (compositor_overlays_shown())
        {
                if (compositor.assist_armed)
                {
                        compositor_assist_refresh();
                        for (int thumb = 0; thumb < compositor.assist_count; thumb++)
                                compositor_damage(compositor.assist_rect[thumb][0], compositor.assist_rect[thumb][1],
                                                  compositor.assist_rect[thumb][2], compositor.assist_rect[thumb][3]);
                }
                count += compositor_overlays(frames, count);
                // The window held is over the grid: its frame is drawn again above the grid's tints, so the grid lies under it.
                // (The snap preview is under the grid and so under this copy too: with the grid up the grid is the aim, and the
                // window held lies over what it aims at.)
                if (compositor.grid_on && compositor.drag >= 0 && count < COMPOSITOR_FRAMES)
                        for (unsigned int at = 0; at < windows; at++)
                                if (rows[at] == compositor.drag)
                                {
                                        frames[count] = frames[at];
                                        // The copy is the window only: the indicator it may carry is drawn once.
                                        memory_zero(frames[count].preview, sizeof(frames[count].preview));
                                        memory_zero(frames[count].mark, sizeof(frames[count].mark));
                                        count++;
                                        break;
                                }
        }

        // The close buttons' means, now the glass and the band are made: where a change reaches a box.
        for (unsigned int at = 0; at < windows; at++)
        {
                compositor_close_mean(&frames[at]);
                compositor_close_ready(&frames[at]);
        }
        compositor_corner_masks_ready(frames, count);
        if (compositor_spots_any(0))
                compositor_spots_ready();

        // A window that fills the output alone is shown straight from its buffer, and nothing is composed.
        // A turned screen is composed and shown from its buffer, never straight from a window's: no direct scanout.
        int turning = compositor_turning();
        int direct = turning ? -1 : compositor_direct_candidate(frames, rows, windows);

        if (turning)
                compositor_unscanned(15);

        if (direct >= 0 &&
            ((compositor_overlay_ready() && compositor_present_overlaid(direct, frames, count, windows)) ||
             compositor_present_direct(direct, frames + windows, count - windows)))
        {
                // The overlay is the tabs' now, or nothing's.
                compositor_unscanned(0);
                compositor.plane_row = -1;
                compositor.plane_fb = 0;
                return;
        }

        // Not now: the compositor's own screen is shown again, drawn whole, once the card has finished a flip.
        int back = !turning && compositor.on_screen && compositor.on_screen != compositor.fb;

        if (back)
        {
                if (compositor.flip_pending)
                        return;
                compositor.direct_row = -1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        else if (compositor.screen_stale)
                compositor_damage(0, 0, compositor.width, compositor.height);
        compositor.screen_stale = 0;

        // The top window on the overlay where it can be: nothing of it composed under the plane.
        int planed[4], plane = !back && !turning ? compositor_plane_candidate(frames, rows, windows, planed) : -1;

        if (compositor.plane_row >= 0 && (plane != compositor.plane_row ||
                                          memory_compare(planed, compositor.plane_rect, sizeof(planed)) != 0))
        {
                // Off the plane, or moved on it: the card is to have it no more, and the compositor composes it.
                if (compositor.flip_pending)
                        return;
                compositor_damage(compositor.plane_rect[0], compositor.plane_rect[1],
                                  compositor.plane_rect[2] - compositor.plane_rect[0],
                                  compositor.plane_rect[3] - compositor.plane_rect[1]);
                if (plane < 0 && !compositor_atomic_to(compositor_primary_fb(), -1, 0, 1, 0, 0, 0))
                        compositor_overlay_off();
                compositor.plane_row = compositor.direct_row = -1;
                compositor.plane_fb = 0;
        }
        if (plane >= 0)
        {
                unsigned int which = compositor.front[plane], fb = compositor_window_fb(plane, which);
                int source[2] = {planed[0] - compositor.x[plane], planed[1] - compositor.y[plane]};

                if (compositor.flip_pending)
                        return;
                if (fb != compositor.plane_fb && !compositor_atomic_to(compositor.fb, plane, which, 1, fb, source, planed))
                {
                        // Refused: off the overlay for good, and the window composed whole from now.
                        compositor_unscanned(12);
                        compositor.overlay_state = -1;
                        compositor_overlay_off();
                        compositor.plane_row = compositor.direct_row = -1;
                        compositor_damage(planed[0], planed[1], planed[2] - planed[0], planed[3] - planed[1]);
                }
                else
                {
                        compositor.plane_fb = fb;
                        compositor_unscanned(0);
                        compositor.plane_row = compositor.direct_row = plane;
                        memory_copy(compositor.plane_rect, planed, sizeof(planed));
                        compositor.table->scanned++;
                        // Where the pieces do not fit the list, what is under the plane is composed too, unseen.
                        compositor_damage_cut(planed);
                }
        }

#ifdef MOONWATER_GPU_C
        int gpu = compositor_gpu_present(frames, count), gpu_composed = gpu;
        int apart = !gpu && compositor_changed_ready();
#else
        int apart = compositor_changed_ready(), gpu_composed = 0;
#endif
        struct compositor_apart slices = {compositor_slice_flush, compositor.composing, (unsigned long)compositor.width};
        unsigned int clips = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *rectangle = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rectangle[0], rectangle[1], rectangle[2], rectangle[3]};
                int rows[2];

#ifdef MOONWATER_GPU_C
                if (gpu)
                        rows[0] = rectangle[1], rows[1] = rectangle[3] - 1; // the GPU drew it all
                else
#endif
                compositor_scene_banded(&target, frames, count, apart ? &slices : 0, rows);

                // Composed apart, each slice had the fingers' marks; on a scanout only written, they are composed aside.
                if (!apart && compositor_spots_any(0))
                        compositor_spots_compose(&target, frames, count);
                if (!apart && compositor.cursor.drawn)
                        compositor_cursor_draw(&target, compositor.cursor.x, compositor.cursor.y,
                                               compositor.cursor.drawn_shape, compositor.cursor.drawn_scale,
                                               compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));

                compositor.painted += (unsigned long)compositor_area(rectangle);
                // Composed apart, the card is told only the rows that changed on it, and nothing where none did.
                if (apart && rows[0] < 0)
                        continue;
                for (unsigned int side = 0; side < 4; side++)
                        clip[clips][side] = (unsigned short)rectangle[side];
                if (apart)
                {
                        clip[clips][1] = (unsigned short)rows[0];
                        clip[clips][3] = (unsigned short)(rows[1] + 1);
                }
                clips++;
        }
        compositor.mirror_stale = 0;
        compositor_runs_learn(frames, rows, windows);

        // No clips at all would tell the card everything changed.
        dirty.clip_count = clips;
        if (clips && compositor_output_count() < 2 && !turning)
                system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        else if (clips)
                compositor_dirty_screens((const unsigned short (*)[4])clip, clips);
        // The screens Canvas turns: their part of the scene, turned into their own buffers and told to their cards.
        if (clips && turning)
                compositor_turn_frame((const unsigned short (*)[4])clip, clips, gpu_composed);
        compositor.damages = 0;

        // Back on the compositor's own screen: what was held for the window is said taken when the card is there.
        if (back && compositor.overlay_on && !compositor_atomic(compositor.fb, -1, 0, 1, 0, 0))
                compositor_overlay_off();
        if (back && compositor.on_screen != compositor.fb && !compositor_flip(compositor.fb, -1, 0))
        {
                struct compositor_crtc set = {(unsigned long)address_of compositor.connector, 1, compositor.crtc, compositor.fb,
                                              0, 0, 0, 1, compositor.mode};

                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set);
                compositor.on_screen = compositor.fb;
                compositor_flip_done();
        }
}

/*
        The card, and every screen it has connected, at the mode Canvas left
        on each.

        Taking Canvas's own modes is what keeps the switch a plane change and
        not a modeset, and what keeps every coordinate the same on both sides
        of it. A connector with no crtc driving it takes a free crtc it can
        have and its first mode (the kernel lists the preferred one first).

        One scanout is the whole space the screens make (compositor.width by
        compositor.height), and each screen's crtc shows its own rectangle of
        it: the scene is composed once across them, a window that straddles
        two is in both, and one DIRTYFB a frame is one commit that takes in
        every screen showing what changed, so no screen waits on another's
        vertical blank in turn. The rows of the space no screen shows (under a
        shorter screen) are composed with the rest and seen by none.
*/
static void compositor_kmsg(string_address text, positive value, string_address tail);
static void compositor_text(const char *path, char *text, unsigned int room);
static int compositor_turn_buffer(unsigned int at, unsigned int width, unsigned int height, struct compositor_turn *retired);
static void compositor_turn_retire(const struct compositor_turn *retired);
static void compositor_cursor_move(void);
static void compositor_arrange_on(int row, unsigned int how, int on);
static void compositor_place(int row, int x, int y);
static void compositor_room(const int *screen, int *room);
static void compositor_arrange_rect(int row, unsigned int how, const int *rect);
static void compositor_room_follow(void);

// The displays file as the compositor has it: its lines, read when the screens are taken (compositor_displays_read),
// and how many.
static struct windows_display compositor_display[WINDOWS_DISPLAYS];
static unsigned int compositor_displays;

// Appends text to line at at, bounded; answers where it ends.
static unsigned int compositor_said(char *line, unsigned int at, const char *text)
{
        for (unsigned int each = 0; text[each] && at < 127; each++)
                line[at++] = text[each];
        return at;
}

/*
        The mode a screen is driven at for the rate its displays line asks for: the first mode it lists at the size it
        has now and that rate (windows_mode_at), in mode. A rate no mode of the screen has keeps the mode it has, and
        the kernel log says so. A preferred rate, which a screen with no line has too, is the first mode it lists at that
        size (or the one the kernel's command line forced there, video=), which a screen at another rate than it goes
        back to: the mode it has is the one it was switched to, so keeping that was no switch back, and the settings'
        first rate, the recommended one, never took after another. The settings write that rate as no line at all once
        nothing else of the screen is asked (a line of the defaults is not kept), so a screen with none is not a screen
        whose mode is its own to keep.
*/
static void compositor_refresh_pick(const char *name, unsigned int count, const struct compositor_mode *modes,
                                    struct compositor_mode *mode)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));
        unsigned int rates[128][3], at, asked = display ? display->refresh : 0;
        char line[160];
        unsigned int length;

        for (at = 0; at < count && at < 128; at++)
                rates[at][0] = modes[at].hdisplay, rates[at][1] = modes[at].vdisplay, rates[at][2] = modes[at].vrefresh;
        at = windows_mode_at(rates, count < 128 ? count : 128, mode->hdisplay, mode->vdisplay, asked);
        // No rate asked: the mode the command line forced at this size, if one was, is the screen's own.
        for (unsigned int forced = 0; !asked && forced < count && forced < 128; forced++)
                if ((modes[forced].type & COMPOSITOR_MODE_USERDEF) && rates[forced][0] == mode->hdisplay &&
                    rates[forced][1] == mode->vdisplay)
                {
                        at = forced;
                        break;
                }
        if (at < count && at < 128)
        {
                *mode = modes[at];
                return;
        }
        if (!asked)
                return;
        length = compositor_said(line, 0, "compositor: canvas.displays: no mode of ");
        length = compositor_said(line, length, name);
        length = compositor_said(line, length, " at ");
        length += positive_into_string((p8 *)line + length, display->refresh);
        length = compositor_said(line, length, " Hz, kept the mode it has");
        line[length] = 0;
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

// Says on the kernel log that a screen is turned degrees, by the card's rotation (hardware) or by Canvas's (software).
static void compositor_turn_say(const char *name, unsigned int degrees, int hardware)
{
        char line[160];
        unsigned int length = compositor_said(line, 0, "compositor: canvas.displays: ");

        length = compositor_said(line, length, name);
        length = compositor_said(line, length, " turned ");
        length += positive_into_string((p8 *)line + length, degrees);
        length = compositor_said(line, length, hardware ? " degrees by hardware rotation" : " degrees by software rotation");
        line[length] = 0;
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

// The degrees a screen is turned by its displays line (0 when it has none): the screen is laid out at its turned size.
// The percent a screen's displays line draws it at: 100 when it has no line or none for it.
static int compositor_display_scale(const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));

        return display && display->scale ? (int)display->scale : WINDOWS_SCALE_DEFAULT;
}

// The percent screen at is drawn at (the desktop's when there is no such screen).
static int compositor_output_scale(int at)
{
        return at >= 0 && at < (int)compositor.outputs ? compositor.output[at].scale : compositor.scale;
}

// The percent a window is drawn at: the scale of the screen it is mostly on, which is what a window straddling a seam takes.
static int compositor_row_scale(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        return compositor_output_scale(compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row],
                                                              (int)compositor.w[row], (int)compositor.h[row]));
}

static unsigned int compositor_display_degrees(const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));

        return display ? display->rotation : 0;
}

/*
        What the card has connected: each connector that says it is connected
        and lists a mode, in the card's order, with its name, the crtc that
        drives it now and the mode that crtc is at (Canvas's: current), or the
        crtcs it could have and its first mode. GETCONNECTOR, asked by the
        card's master, probes the connector again, which is what a hotplug
        is to be answered by.
*/
#define COMPOSITOR_CONNECTORS 16

struct compositor_connected
{
        unsigned int count, crtc_count, max_width, max_height;
        unsigned int crtcs[COMPOSITOR_CONNECTORS];
        unsigned int connector[WINDOWS_OUTPUTS], crtc[WINDOWS_OUTPUTS], possible[WINDOWS_OUTPUTS];
        int current[WINDOWS_OUTPUTS];
        struct compositor_mode mode[WINDOWS_OUTPUTS];
        char name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME], model[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_MODEL];
        int size[WINDOWS_OUTPUTS][2];
        unsigned int hz[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_HZ], hz_count[WINDOWS_OUTPUTS];
};

// Adds a mode's refresh rate to a screen's list when the mode is the screen's size and the list has no such rate yet:
// the rates a screen lists at its size, which the Displays field offers.
static void compositor_rate_add(unsigned int *hz, unsigned int *count, const struct compositor_mode *mode, int width,
                                int height)
{
        if (mode->hdisplay != width || mode->vdisplay != height || !mode->vrefresh || *count >= WINDOWS_OUTPUT_HZ)
                return;
        for (unsigned int at = 0; at < *count; at++)
                if (hz[at] == mode->vrefresh)
                        return;
        hz[(*count)++] = mode->vrefresh;
}

#define COMPOSITOR_OBJECT_CONNECTOR 0xc0c0c0c0u // DRM_MODE_OBJECT_CONNECTOR
#define COMPOSITOR_GETPROPBLOB COMPOSITOR_DRM(0xacu, 16u)
// An EDID is a block of 128 bytes and its extensions, 128 each, 255 of them at the most.
#define COMPOSITOR_EDID_BLOCK 128u
#define COMPOSITOR_EDID_MOST (COMPOSITOR_EDID_BLOCK * 256u)

/*
        The monitor's name from an EDID block of length bytes: the descriptor (18 bytes, at 54, 72, 90 or 108) tagged 0xFC,
        its text of up to 13 bytes ended by a newline and its blanks at the end dropped. A byte that is not a printable
        character is a '?'. Answers the length written into name (room bytes, with a null after it); 0, and the empty name,
        when the block is not an EDID or has no such descriptor.
*/
static unsigned int compositor_edid_name(const unsigned char *edid, unsigned int length, char *name, unsigned int room)
{
        static const unsigned char header[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        unsigned int count = 0;

        if (room)
                name[0] = 0;
        if (length < 128 || !room || memory_compare(edid, header, sizeof(header)))
                return 0;
        for (unsigned int at = 54; at <= 108; at += 18)
        {
                if (edid[at] || edid[at + 1] || edid[at + 2] || edid[at + 3] != 0xfc)
                        continue;
                for (unsigned int each = 0; each < 13 && count + 1 < room; each++)
                {
                        unsigned char c = edid[at + 5 + each];

                        if (c == 0x0a)
                                break;
                        name[count++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
                }
                while (count && name[count - 1] == ' ')
                        count--;
                name[count] = 0;
                return count;
        }
        return 0;
}

// The monitor's name, from the EDID blob the connector's EDID property names: the connector's properties are listed and the one
// called EDID is read. Empty when the connector has no EDID (a virtual screen may have none), or the card will not say.
static void compositor_connector_model(unsigned int connector, char *name, unsigned int room)
{
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, connector, COMPOSITOR_OBJECT_CONNECTOR};
        static unsigned char edid[COMPOSITOR_EDID_MOST]; // 32 KiB: off the stack

        if (room)
                name[0] = 0;
        if (system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return;
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};
                struct
                {
                        unsigned int blob_id, length;
                        unsigned long data;
                } blob;

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0 ||
                    !string_equals((string_address)property.name, (string_address) "EDID"))
                        continue;
                if (!values[at])
                        return;
                /*
                        The kernel copies a blob only when the length asked is the blob's own (drm_mode_getblob_ioctl), so a
                        length of 256 read only a monitor whose EDID is one extension long: its length first, then all of
                        it, when it is whole blocks and no more than a base block and 255 extensions.
                */
                blob.blob_id = (unsigned int)values[at], blob.length = 0, blob.data = 0;
                if (system_control(compositor.card, COMPOSITOR_GETPROPBLOB, address_of blob) != 0 || !blob.length ||
                    blob.length % COMPOSITOR_EDID_BLOCK || blob.length > COMPOSITOR_EDID_MOST)
                        return;
                {
                        unsigned int length = blob.length;

                        memory_zero(edid, length);
                        blob.data = (unsigned long)edid;
                        if (system_control(compositor.card, COMPOSITOR_GETPROPBLOB, address_of blob) == 0 && blob.length == length)
                                compositor_edid_name(edid, length, name, room);
                }
                return;
        }
}

/*
        The rates screen n lists at the size its panel has: the size of the mode it is at, which is not the size it is laid
        out at when it is turned a quarter (its width and height the other way round: no mode has that), each rate once, in
        the order the screen lists them, which is the order the settings list them in.
*/
static void compositor_rates_take(struct compositor_connected *found, unsigned int n, const struct compositor_mode *modes,
                                  unsigned int count)
{
        for (unsigned int each = 0; each < count; each++)
                compositor_rate_add(found->hz[n], &found->hz_count[n], &modes[each], found->mode[n].hdisplay,
                                    found->mode[n].vdisplay);
}

static b32 compositor_connected_read(struct compositor_connected *found)
{
        struct compositor_card_res resources = {0};
        unsigned int connectors[COMPOSITOR_CONNECTORS], taken = 0, kept = 0;

        memory_zero(found, sizeof(*found));
        if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0 ||
            resources.connector_count > COMPOSITOR_CONNECTORS || resources.crtc_count > COMPOSITOR_CONNECTORS ||
            !resources.crtc_count)
                return -COMPOSITOR_EINVAL;
        resources.connectors = (unsigned long)connectors;
        resources.crtcs = (unsigned long)found->crtcs;
        resources.fb_count = resources.encoder_count = 0;
        {
                unsigned int room_connectors = resources.connector_count, room_crtcs = resources.crtc_count;

                if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                        return -COMPOSITOR_EINVAL;
                // The kernel writes no more than the room it was given and answers how many there are now, which a screen
                // that came between the two calls (a dock's MST port) makes more than were written: only those are read.
                resources.connector_count = resources.connector_count < room_connectors ? resources.connector_count : room_connectors;
                resources.crtc_count = resources.crtc_count < room_crtcs ? resources.crtc_count : room_crtcs;
        }
        found->crtc_count = resources.crtc_count;
        found->max_width = resources.max_width;
        found->max_height = resources.max_height;

        for (unsigned int at = 0; at < resources.connector_count && found->count < WINDOWS_OUTPUTS; at++)
        {
                struct compositor_connector probe = {.connector_id = connectors[at]};
                struct compositor_mode modes[128];
                unsigned int encoders[8], n = found->count, asked_modes, asked_encoders;
                int listed;

                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0 ||
                    probe.connection != 1 || !probe.mode_count)
                        continue;
                // The kernel copies a list only when it fits what was asked for, and says how long it is either way.
                asked_modes = probe.mode_count = probe.mode_count > 128 ? 128 : probe.mode_count;
                asked_encoders = probe.encoder_count = probe.encoder_count > 8 ? 8 : probe.encoder_count;
                probe.modes = (unsigned long)modes;
                probe.encoders = (unsigned long)encoders;
                probe.prop_count = 0;
                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0)
                        continue;
                listed = probe.mode_count && probe.mode_count <= asked_modes;
                probe.encoder_count = probe.encoder_count <= asked_encoders ? probe.encoder_count : 0;

                found->connector[n] = connectors[at];
                compositor_connector_name(probe.connector_type, probe.connector_type_id, found->name[n]);
                compositor_connector_model(connectors[at], found->model[n], WINDOWS_OUTPUT_MODEL);
                // The mode the kernel's command line names (video=, which the kernel marks the person's) before the
                // first listed, which is the one the screen prefers; Canvas takes them in that order too.
                for (unsigned int each = 0; listed && each < probe.mode_count && !found->mode[n].clock; each++)
                        if (modes[each].type & COMPOSITOR_MODE_USERDEF)
                                found->mode[n] = modes[each];
                if (listed && !found->mode[n].clock)
                        found->mode[n] = modes[0];
                if (probe.encoder_id)
                {
                        struct compositor_encoder encoder = {.encoder_id = probe.encoder_id};

                        if (system_control(compositor.card, COMPOSITOR_GETENCODER, address_of encoder) == 0 && encoder.crtc_id)
                        {
                                struct compositor_crtc current = {.crtc_id = encoder.crtc_id};

                                found->crtc[n] = encoder.crtc_id;
                                if (system_control(compositor.card, COMPOSITOR_GETCRTC, address_of current) == 0 &&
                                    current.mode_valid)
                                        found->mode[n] = current.mode, found->current[n] = 1;
                        }
                }
                for (unsigned int each = 0; each < probe.encoder_count; each++)
                {
                        struct compositor_encoder encoder = {.encoder_id = encoders[each]};

                        if (system_control(compositor.card, COMPOSITOR_GETENCODER, address_of encoder) == 0)
                                found->possible[n] |= encoder.possible_crtcs;
                }
                // The rate the displays file asks for: the mode of this size at it that the screen lists, or the mode it has.
                if (listed)
                        compositor_refresh_pick(found->name[n], probe.mode_count, modes, &found->mode[n]);
                if (!found->current[n] && !listed)
                        continue;
                found->size[n][0] = found->mode[n].hdisplay;
                found->size[n][1] = found->mode[n].vdisplay;
                // A screen turned a quarter is laid out at its turned size, its width and height the other way round.
                if (compositor_display_degrees(found->name[n]) % 180 == 90)
                {
                        int turned = found->size[n][0];

                        found->size[n][0] = found->size[n][1], found->size[n][1] = turned;
                }
                if (found->size[n][0] && found->size[n][1])
                {
                        if (listed)
                                compositor_rates_take(found, n, modes, probe.mode_count);
                        found->count++;
                }
        }

        // A crtc is one screen's: those a crtc drives now keep it, and the rest take the first free one they can have.
        for (unsigned int n = 0; n < found->count; n++)
                for (unsigned int at = 0; found->crtc[n] && at < found->crtc_count; at++)
                        if (found->crtcs[at] == found->crtc[n])
                        {
                                if (taken >> at & 1)
                                        found->crtc[n] = 0, found->current[n] = 0;
                                else
                                        taken |= 1u << at;
                                break;
                        }
        for (unsigned int n = 0; n < found->count; n++)
                for (unsigned int at = 0; !found->crtc[n] && at < found->crtc_count; at++)
                        if (!(taken >> at & 1) && (!found->possible[n] || found->possible[n] >> at & 1))
                        {
                                found->crtc[n] = found->crtcs[at];
                                taken |= 1u << at;
                        }
        // One no crtc is left for is no screen of this desktop.
        for (unsigned int n = 0; n < found->count; n++)
        {
                if (!found->crtc[n])
                        continue;
                if (kept != n)
                {
                        found->connector[kept] = found->connector[n], found->crtc[kept] = found->crtc[n];
                        found->possible[kept] = found->possible[n], found->current[kept] = found->current[n];
                        found->mode[kept] = found->mode[n];
                        memory_copy(found->name[kept], found->name[n], WINDOWS_OUTPUT_NAME);
                        memory_copy(found->model[kept], found->model[n], WINDOWS_OUTPUT_MODEL);
                        found->size[kept][0] = found->size[n][0], found->size[kept][1] = found->size[n][1];
                        memory_copy(found->hz[kept], found->hz[n], sizeof(found->hz[n]));
                        found->hz_count[kept] = found->hz_count[n];
                }
                kept++;
        }
        found->count = kept;
        return 0;
}

// The displays file, /root's, and a check's own.
static const char *compositor_displays_path = SETTING_CANVAS_DISPLAYS_PATH;

// Reads the displays file into compositor_display. What is bad in it is said on the kernel log (report), line by line:
// the field and the screen it is on, which is kept at its default.
static void compositor_displays_read(int report)
{
        static const char *const fields[] = {"name", "rotation", "refresh", "x", "y", "field"};
        char text[1024];
        unsigned int bits[] = {WINDOWS_DISPLAY_NAME, WINDOWS_DISPLAY_ROTATION, WINDOWS_DISPLAY_REFRESH, WINDOWS_DISPLAY_X,
                               WINDOWS_DISPLAY_Y, WINDOWS_DISPLAY_OTHER};

        compositor_text(compositor_displays_path, text, sizeof(text));
        compositor_displays = windows_displays_read(text, compositor_display, WINDOWS_DISPLAYS);
        for (unsigned int each = 0; report && each < compositor_displays; each++)
                for (unsigned int bit = 0; bit < sizeof(bits) / sizeof(bits[0]); bit++)
                {
                        char line[160];
                        unsigned int at = compositor_said(line, 0, "compositor: canvas.displays: bad ");

                        if (!(compositor_display[each].bad & bits[bit]))
                                continue;
                        at = compositor_said(line, at, fields[bit]);
                        at = compositor_said(line, at, " on ");
                        at = compositor_said(line, at, compositor_display[each].name[0] ? compositor_display[each].name : "a line");
                        at = compositor_said(line, at, ", kept at its default");
                        line[at] = 0;
                        compositor_kmsg((string_address)line, 0, (string_address) "");
                }
}

// The connected screens laid by the arrangement, but for those in dropped (a bit each), which are dark: what
// windows_arrange_lay answers, in the found list's order.
static unsigned int compositor_space_lay(const struct compositor_connected *found, unsigned int dropped,
                                         int (*rects)[4], unsigned char *order)
{
        char names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME], text[512], pinned[512];
        int sizes[WINDOWS_OUTPUTS][2], some[WINDOWS_OUTPUTS][4];
        unsigned char map[WINDOWS_OUTPUTS], laid[WINDOWS_OUTPUTS];
        unsigned int count = 0, lit, made;

        for (unsigned int n = 0; n < found->count; n++)
        {
                rects[n][0] = rects[n][1] = rects[n][2] = rects[n][3] = 0;
                if (dropped >> n & 1)
                        continue;
                memory_copy(names[count], found->name[n], WINDOWS_OUTPUT_NAME);
                sizes[count][0] = found->size[n][0];
                sizes[count][1] = found->size[n][1];
                map[count++] = (unsigned char)n;
        }
        compositor_text(SETTING_CANVAS_ARRANGE_PATH, text, sizeof(text));
        // A display's place in the file is its word in the arrangement: the arrangement laid is that text with the places put in.
        made = windows_arrange_pinned(text, (const char (*)[WINDOWS_OUTPUT_NAME])names, count, compositor_display,
                                      compositor_displays, pinned, sizeof(pinned));
        if (made)
                memory_copy(text, pinned, made);
        lit = windows_arrange_lay(text, (const char (*)[WINDOWS_OUTPUT_NAME])names, (const int (*)[2])sizes, count, some,
                                     laid);
        for (unsigned int at = 0; at < count; at++)
                memory_copy(rects[map[at]], some[at], sizeof(some[at]));
        for (unsigned int at = 0; at < lit; at++)
                order[at] = map[laid[at]];
        return lit;
}

// A scanout of width by height: a dumb buffer, its framebuffer and its mapping. Answers 0, or a negative errno.
static b32 compositor_scanout_make(int width, int height, unsigned int *fb_id, unsigned int *handle,
                                   unsigned int **pixels, unsigned long *bytes, unsigned long *pitch)
{
        struct compositor_dumb dumb = {(unsigned int)height, (unsigned int)width, 32, 0, 0, 0, 0};
        struct compositor_map_dumb map = {0};
        struct compositor_fb fb = {0};
        long mapped;

        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                return -COMPOSITOR_ENOSPC;
        fb.width = dumb.width;
        fb.height = dumb.height;
        fb.pitch = dumb.pitch;
        fb.bpp = 32;
        fb.depth = 24;
        fb.handle = dumb.handle;
        map.handle = dumb.handle;
        mapped = system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) != 0 ||
                         system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0
                     ? -COMPOSITOR_EINVAL
                     : compositor_call(syscall(mmap), 0, dumb.size, 3, 1, (positive)compositor.card, map.offset);
        if (mapped < 0 && mapped > -4096)
        {
                unsigned int close[2] = {dumb.handle, 0};

                if (fb.fb_id)
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of fb.fb_id);
                system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                return -COMPOSITOR_EINVAL;
        }
        *fb_id = fb.fb_id;
        *handle = dumb.handle;
        *pixels = (unsigned int *)mapped;
        *bytes = dumb.size;
        *pitch = dumb.pitch;
        return 0;
}

// A scanout no crtc shows any more, given back.
static void compositor_scanout_free(unsigned int fb, unsigned int handle, unsigned int *pixels, unsigned long bytes)
{
        unsigned int close[2] = {handle, 0};

#ifdef MOONWATER_GPU_C
        // The GPU's import of it goes with it: the card hands a buffer made later the lowest free handle, this one's, and the import (which keeps this
        // buffer alive) is known by the handle, so the GPU would go on drawing into this one while the screen shows that.
        if (compositor_gpu_state.out_key == handle && compositor_gpu_state.out)
                gpu_unimport(compositor_gpu_state.out), compositor_gpu_state.out = 0;
        for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                if (compositor_gpu_state.turn_key[at] == handle && compositor_gpu_state.turn_va[at])
                        gpu_unimport(compositor_gpu_state.turn_va[at]), compositor_gpu_state.turn_va[at] = 0;
#endif
        compositor_call(syscall(munmap), (positive)pixels, bytes, 0, 0, 0, 0);
        system_control(compositor.card, COMPOSITOR_RMFB, address_of fb);
        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
}

// Another framebuffer of the scanout, as large and over the same buffer, for a screen of its own; 0 when the card will
// not make one, and that screen shares the scanout's.
static unsigned int compositor_screen_fb(void)
{
        struct compositor_fb fb = {0, (unsigned int)compositor.width, (unsigned int)compositor.height,
                                   (unsigned int)compositor.scanout_pitch, 32, 24, compositor.scanout_handle};

        return system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) == 0 ? fb.fb_id : 0;
}

/*
        The framebuffer a screen Canvas turns in software is drawn into, at the panel's size: one for each screen, kept
        while the size is the same. A buffer whose size changes is handed back in retired, and given back once the
        crtcs show the new one (compositor_turn_retire). False when the card gives none.
*/
static int compositor_turn_buffer(unsigned int at, unsigned int width, unsigned int height, struct compositor_turn *retired)
{
        struct compositor_turn *turn = &compositor_turns[at];

        if (turn->fb && turn->width == width && turn->height == height)
                return 1;
        if (turn->fb)
                *retired = *turn;
        memory_zero(turn, sizeof(*turn));
        if (compositor_scanout_make((int)width, (int)height, &turn->fb, &turn->handle, &turn->pixels, &turn->bytes,
                                    &turn->pitch) < 0)
                return 0;
        turn->width = width;
        turn->height = height;
        return 1;
}

// A buffer given back, which no crtc shows any more.
static void compositor_turn_retire(const struct compositor_turn *retired)
{
        if (retired->fb)
                compositor_scanout_free(retired->fb, retired->handle, retired->pixels, retired->bytes);
}

// A crtc that shows nothing: off, its screen dark.
static void compositor_crtc_off(unsigned int crtc)
{
        struct compositor_crtc off = {0, 0, crtc, 0, 0, 0, 0, 0, {0}};

        system_control(compositor.card, COMPOSITOR_SETCRTC, address_of off);
}

// Whether two modes are the same timing: what a crtc scans at, whatever the name says.
static int compositor_mode_same(const struct compositor_mode *a, const struct compositor_mode *b)
{
        return a->clock == b->clock && a->hdisplay == b->hdisplay && a->vdisplay == b->vdisplay &&
               a->htotal == b->htotal && a->vtotal == b->vtotal && a->vrefresh == b->vrefresh && a->flags == b->flags;
}

/*
        The windows and the pointer, from the screens they were on (was, with
        their names, before the screens were taken again) to the screens now.
        A window on a screen that is still there, by its name, goes with it
        where it moved; one on a screen that went goes to the primary, at the
        same share of the room it had (compositor_screen_carry); one that
        filled a screen or half of one fills the same of the screen it is on
        now; and every one is then kept reachable. A drag or a resize under way
        ends. The pointer goes the same way and is kept on a screen.
*/
static void compositor_space_carry(const int (*was)[4], const char (*names)[WINDOWS_OUTPUT_NAME], unsigned int count)
{
        unsigned int now_count;
        const int (*now)[4] = compositor_screens(&now_count);
        int nowhere = -2147483647 - 1;

        compositor_pointer_sync();
        compositor_preview(COMPOSITOR_NONE);
        compositor.drag = compositor.resize = COMPOSITOR_NONE;
        for (int row = -1; row < WINDOWS; row++)
        {
                int x = row < 0 ? compositor.real_x : compositor.x[row], y = row < 0 ? compositor.real_y : compositor.y[row];
                int w = row < 0 ? 1 : (int)compositor.w[row], h = row < 0 ? 1 : (int)compositor.h[row], from, to = 0;

                if (row >= 0 && !compositor.showing[row])
                        continue;
                from = count ? compositor_screen_owner(was, count, x, y, w, h) : -1;
                for (unsigned int at = 0; from >= 0 && at < now_count && at < compositor.outputs; at++)
                        if (!string_compare((string_address)compositor.output[at].name, (string_address)names[from]))
                                to = (int)at;
                if (from < 0 || !memory_compare(was[from], now[to], sizeof(was[from])))
                        ;
                else if (row < 0)
                        compositor_screen_carry(was[from], now[to], &compositor.real_x, &compositor.real_y, 1, 1);
                else
                {
                        int sx = compositor.saved_x[row], sy = compositor.saved_y[row];

                        compositor_screen_carry(was[from], now[to], &x, &y, w, h);
                        compositor_screen_carry(was[from], now[to], &sx, &sy, (int)compositor.saved_w[row],
                                                (int)compositor.saved_h[row]);
                        compositor.saved_x[row] = sx, compositor.saved_y[row] = sy;
                        compositor.x[row] = compositor.table->x[row] = x;
                        compositor.y[row] = compositor.table->y[row] = y;
                }
                // One that filled a screen's room fills the room of the screen it is on now, the band left out.
                if (row >= 0 && compositor.arranged[row] == COMPOSITOR_ROOM)
                {
                        int room[4];

                        compositor_room(now[to], room);
                        compositor_arrange_rect(row, COMPOSITOR_ROOM, room);
                }
                else if (row >= 0 && compositor.arranged[row] != COMPOSITOR_FLOATING)
                        compositor_arrange_on(row, compositor.arranged[row], to);
                else if (row >= 0)
                        compositor_place(row, compositor.x[row], compositor.y[row]);
        }
        compositor_screen_keep_point(now, now_count, nowhere, nowhere, &compositor.real_x, &compositor.real_y);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor_pointer_shown();
        // A window on a screen that stayed whose band moved (the primary is another now) follows its room.
        compositor_room_follow();
}

static void compositor_table_outputs(void);
static void compositor_band_room(void);

/*
        The card is to show the compositor's own scanout on every crtc: no window straight, none on the overlay, and
        no flip whose row a picture held back waits on (compositor_flip_done lets go of every held picture once the
        crtcs are set; flip_row would have kept back the one a direct window's newest buffer was taken into, and with
        two screens nothing flips again).
*/
static void compositor_scanout_own(void)
{
        compositor_overlay_off();
        compositor.direct_row = compositor.plane_row = compositor.flip_row = -1;
        compositor.plane_fb = 0;
        compositor.on_screen = compositor.fb;
}

/*
        The whole space to be drawn again, and what waited to be drawn before it forgotten: those are rectangles of the space
        before, and a space that is another shape holds none of them. A turn makes 2560 by 1080 of 1080 by 2560, the band's
        rectangle along the old bottom (rows past the new space's end) is made just before the screens are taken again, and
        composed it was stored into memory past the scanout's end: a write the compositor died of on a booted guest at the
        third or fourth turn, when the memory there was not mapped.
*/
static void compositor_damage_space(void)
{
        compositor.damages = 0;
        compositor_damage(0, 0, compositor.width, compositor.height);
}

static void compositor_scale_fit(void);

/*
        The scale each screen's displays line asks, taken by the screens as they are driven. A scale is no mode and no
        place, so when it is all that changed the screens are not taken again (compositor_space_take): no scanout and
        no commit, and this follows it instead: each screen's scale, the desktop's (the primary's) and the table's, the
        pointer's plane painted again at the scale, the windows laid again in the room the bands leave at it, the
        bands' memory for the size they are drawn at, and every pixel drawn again. A window's own scale is its screen's,
        counted into the table as it is next drawn (compositor_frame_of), which also knocks it. True when one changed.
*/
static int compositor_scales_take(void)
{
        int was[WINDOWS_OUTPUTS][4], changed = 0;
        char names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];

        for (unsigned int at = 0; at < compositor.outputs && at < WINDOWS_OUTPUTS; at++)
        {
                int scale = compositor_display_scale(compositor.output[at].name);

                changed |= scale != compositor.output[at].scale;
                compositor.output[at].scale = scale;
                memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                memory_copy(names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
        }
        if (!changed)
                return 0;
        compositor.scale = compositor.output[0].scale;
        if (compositor.table)
                compositor.table->scale = (unsigned int)compositor.scale;
        compositor.cursor.plane_scale = 0;
        compositor_cursor_move();
        compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])names, compositor.outputs);
        compositor_band_room();
        compositor_scale_fit();
        compositor_tabs_changed();
        compositor_damage(0, 0, compositor.width, compositor.height);
        return 1;
}

/*
        The screens taken: what the card has connected, laid by the
        arrangement, one scanout as large as the space they make, and each
        crtc showing its screen's rectangle of it. A screen the card will not
        drive -- one that puts the space past the largest framebuffer the card
        makes, or whose mode a commit refuses -- is left dark and the rest laid
        again without it, so one screen that cannot be shown takes none of the
        others down with it. The first take starts from Canvas's state; a
        later one (a screen plugged in or out, the arrangement changed) is
        nothing when nothing changed, and otherwise carries the windows and
        the pointer to the space it makes. Answers 0, or a negative errno
        when not one screen could be shown.
*/
static b32 compositor_space_take(int first)
{
        struct compositor_connected found;
        int rects[WINDOWS_OUTPUTS][4], was[WINDOWS_OUTPUTS][4];
        char was_names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        unsigned int was_crtc[WINDOWS_OUTPUTS], was_count = compositor.outputs, dropped = 0, extra[WINDOWS_OUTPUTS], extras;
        unsigned char order[WINDOWS_OUTPUTS];
        b32 status;
        int carried = 0;

        compositor_displays_read(1);
        status = compositor_connected_read(&found);

        if (status < 0 || !found.count)
                return status < 0 ? status : -COMPOSITOR_EINVAL;
        // A flip in flight is the card showing a window's buffer on one crtc: the screens are taken once it has landed.
        if (!first && compositor.flip_pending)
        {
                compositor.space_wanted = 1;
                return 0;
        }
        compositor.space_wanted = 0;
        for (unsigned int at = 0; at < was_count; at++)
        {
                memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                memory_copy(was_names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                was_crtc[at] = compositor.output[at].crtc;
        }

        for (unsigned int attempt = 0; attempt <= found.count; attempt++)
        {
                unsigned int lit = compositor_space_lay(&found, dropped, rects, order), refused = 0, same;
                struct compositor_turn retired[WINDOWS_OUTPUTS] = {{0}};
                unsigned int old_fb = compositor.fb, old_handle = compositor.scanout_handle, *old_pixels = compositor.scanout;
                unsigned long old_bytes = compositor.scanout_bytes;
                unsigned int fb = compositor.fb, handle = compositor.scanout_handle, *pixels = compositor.scanout;
                unsigned long bytes = compositor.scanout_bytes, pitch = compositor.scanout_pitch;
                int box[4], screens[WINDOWS_OUTPUTS][4], most_w = 0, most_h = 0;
                positive period = 0;

                if (!lit)
                        return -COMPOSITOR_EINVAL;
                for (unsigned int at = 0; at < lit; at++)
                        memory_copy(screens[at], rects[order[at]], sizeof(screens[at]));
                compositor_screens_box((const int (*)[4])screens, lit, box);

                // Past the largest framebuffer the card makes: the last screen laid is left dark, and the rest laid again.
                if (lit > 1 && found.max_width && found.max_height &&
                    ((unsigned int)box[2] > found.max_width || (unsigned int)box[3] > found.max_height))
                {
                        compositor_kmsg((string_address) "compositor: the screens make a space larger than the card's largest framebuffer, dark: ",
                                        0, (string_address)found.name[order[lit - 1]]);
                        dropped |= 1u << order[lit - 1];
                        continue;
                }

                same = !first && lit == compositor.outputs && box[2] == compositor.width && box[3] == compositor.height;
                for (unsigned int at = 0; same && at < lit; at++)
                        same = compositor.output[at].crtc == found.crtc[order[at]] &&
                               compositor.output[at].connector == found.connector[order[at]] &&
                               !memory_compare(compositor.screen[at], screens[at], sizeof(screens[at])) &&
                               compositor_mode_same(&compositor.output[at].mode, &found.mode[order[at]]) &&
                               compositor.output[at].degrees == compositor_display_degrees(found.name[order[at]]);
                if (same)
                {
                        // The same screens at the same modes: only the scale a line asks can have changed, and it takes no commit.
                        // The table says what the lines ask now, beside what was done of it (the settings show both).
                        compositor_scales_take();
                        compositor_table_outputs();
                        return 0;
                }

                // A space of another size is another scanout; the one before is given back once no crtc shows it.
                if (first || box[2] != compositor.width || box[3] != compositor.height)
                {
                        status = compositor_scanout_make(box[2], box[3], &fb, &handle, &pixels, &bytes, &pitch);
                        if (status < 0 && lit > 1)
                        {
                                compositor_kmsg((string_address) "compositor: no scanout as large as the screens' space, dark: ",
                                                0, (string_address)found.name[order[lit - 1]]);
                                dropped |= 1u << order[lit - 1];
                                continue;
                        }
                        if (status < 0)
                                return status;
                }

                // The screens, the primary first, each at its own mode; the framebuffers the screens before had of their
                // own are given back once no crtc shows them.
                extras = 0;
                for (unsigned int at = 0; at < compositor.outputs; at++)
                        if (compositor.output[at].fb && compositor.output[at].fb != compositor.fb && compositor.output[at].turned != 2)
                                extra[extras++] = compositor.output[at].fb;
                memory_zero(compositor.output, sizeof(compositor.output));
                for (unsigned int at = 0; at < lit; at++)
                {
                        unsigned int n = order[at];
                        positive refresh = found.mode[n].vrefresh >= 24 && found.mode[n].vrefresh <= 1000 ? found.mode[n].vrefresh : 60;

                        compositor.output[at].crtc = found.crtc[n];
                        compositor.output[at].connector = found.connector[n];
                        compositor.output[at].mode = found.mode[n];
                        memory_copy(compositor.output[at].hz, found.hz[n], sizeof(found.hz[n]));
                        compositor.output[at].hz_count = found.hz_count[n];
                        memory_copy(compositor.output[at].name, found.name[n], WINDOWS_OUTPUT_NAME);
                        memory_copy(compositor.output[at].model, found.model[n], WINDOWS_OUTPUT_MODEL);
                        for (unsigned int index = 0; index < found.crtc_count; index++)
                                if (found.crtcs[index] == found.crtc[n])
                                        compositor.output[at].crtc_index = index;
                        // The turn its displays line asks for: the card's own rotation where its plane has one, else Canvas's.
                        compositor.output[at].degrees = compositor_display_degrees(found.name[n]);
                        compositor.output[at].scale = compositor_display_scale(found.name[n]);
                        compositor.output[at].turned = !compositor.output[at].degrees ? 0 :
                                compositor_rotation_hardware(compositor.output[at].crtc_index, compositor.output[at].degrees,
                                                             &compositor.output[at].turn_plane,
                                                             &compositor.output[at].turn_property,
                                                             &compositor.output[at].turn_value) ? 1 : 2;
                        // A screen the card turns has the buffer of one Canvas turns too: it is set up as that one is, and is
                        // that one if the card refuses the turn (below).
                        if (compositor.output[at].turned &&
                            !compositor_turn_buffer(at, (unsigned int)found.mode[n].hdisplay, (unsigned int)found.mode[n].vdisplay,
                                                    &retired[at]))
                        {
                                compositor.output[at].turned = 0;
                                compositor_kmsg((string_address) "compositor: canvas.displays: no buffer to turn a screen in software, left unturned",
                                                0, (string_address) "");
                        }
                        memory_copy(compositor.screen[at], screens[at], sizeof(screens[at]));
                        most_w = screens[at][2] > most_w ? screens[at][2] : most_w;
                        most_h = screens[at][3] > most_h ? screens[at][3] : most_h;
                        // Frames go out at the fastest screen's rate.
                        period = !period || 1000000000ull / refresh < period ? 1000000000ull / refresh : period;
                }
                compositor.outputs = lit;
                if (lit)
                        compositor.scale = compositor.output[0].scale;
                if (lit && compositor.table)
                        compositor.table->scale = (unsigned int)compositor.scale;
                compositor.period = period;
                compositor.crtc = compositor.output[0].crtc;
                compositor.crtc_index = compositor.output[0].crtc_index;
                compositor.connector = compositor.output[0].connector;
                compositor.mode = compositor.output[0].mode;
                // The room every window's buffer has: the widest and the tallest screen connected at the start, dark or
                // lit, for the session; what a window may be is the largest screen now, in that room.
                if (first)
                {
                        compositor.stride = compositor.room_h = 0;
                        for (unsigned int n = 0; n < found.count; n++)
                        {
                                compositor.stride = found.size[n][0] > compositor.stride ? found.size[n][0] : compositor.stride;
                                compositor.room_h = found.size[n][1] > compositor.room_h ? found.size[n][1] : compositor.room_h;
                        }
                }
                compositor.most_w = most_w < compositor.stride ? most_w : compositor.stride;
                compositor.most_h = most_h < compositor.room_h ? most_h : compositor.room_h;

                // The scanout the space is composed into; what was composed apart for the one before is let go.
                if (fb != compositor.fb)
                {
                        compositor_apart_drop();
                        compositor.scanout = pixels;
                        compositor.scanout_bytes = bytes;
                        compositor.scanout_pitch = pitch;
                        compositor.scanout_handle = handle;
                        compositor.fb = fb;
                        compositor.width = box[2];
                        compositor.height = box[3];
                }
                // The primary shows the scanout's framebuffer, and each other screen one of its own over it.
                for (unsigned int at = 0; at < lit; at++)
                        compositor.output[at].fb = compositor.output[at].turned == 2 ? compositor_turns[at].fb :
                                                   at ? compositor_screen_fb() : compositor.fb;
                if (!first)
                {
                        carried = 1;
                        compositor_scanout_own();
                        compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])was_names, was_count);
                        compositor_band_room();
                        compositor_tabs_changed();
                }
                compositor.mirror_stale = 1;
                compositor_damage_space();
                compositor_present();

                /*
                        A turned screen is set up on its own buffer, the panel's size, from the origin: a screen Canvas turns
                        is shown from it, whole, and one the card turns starts there too, since its mode set on the scanout
                        with the plane not turned yet is a viewport the space does not hold (a quarter turn's panel is the
                        space's height wide), which the kernel refuses. A plane still turned from before (a compositor
                        started again) refuses that buffer the same way, and then the scanout is the one that fits.
                */
                unsigned int on_scanout = 0;

                for (unsigned int at = 0; at < lit; at++)
                {
                        int turned = compositor.output[at].turned != 0;
                        unsigned int fb_of = compositor.output[at].fb ? compositor.output[at].fb : compositor.fb;
                        struct compositor_crtc set = {(unsigned long)address_of compositor.output[at].connector, 1,
                                                      compositor.output[at].crtc,
                                                      turned ? compositor_turns[at].fb : fb_of,
                                                      turned ? 0 : (unsigned int)screens[at][0],
                                                      turned ? 0 : (unsigned int)screens[at][1], 0, 1, compositor.output[at].mode};

                        if (system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) == 0)
                                continue;
                        set.fb_id = fb_of, set.x = (unsigned int)screens[at][0], set.y = (unsigned int)screens[at][1];
                        if (compositor.output[at].turned == 1 && system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) == 0)
                                on_scanout |= 1u << at;
                        else
                                refused |= 1u << order[at];
                }
                // The screens the card turns are turned by their plane once their crtcs are set: a refusal leaves one on its
                // own buffer turned in software, and one the plane already turned as the plane has it.
                for (unsigned int at = 0; at < lit; at++)
                {
                        unsigned int fb_of = compositor.output[at].fb ? compositor.output[at].fb : compositor.fb;
                        int source[2] = {screens[at][0], screens[at][1]};

                        if (compositor.output[at].turned == 1 && !(refused >> order[at] & 1) &&
                            !compositor_rotate_commit(&compositor.output[at], source, (unsigned int)screens[at][2],
                                                      (unsigned int)screens[at][3], fb_of) &&
                            !(on_scanout >> at & 1))
                        {
                                // Its buffer was not drawn by the present above, which the card was to turn: the next one draws it.
                                compositor.output[at].turned = 2;
                                compositor.output[at].fb = compositor_turns[at].fb;
                                compositor_damage_space();
                        }
                        if (compositor.output[at].turned && compositor.output[at].degrees)
                                compositor_turn_say(compositor.output[at].name, compositor.output[at].degrees,
                                                    compositor.output[at].turned == 1);
                }
                // The turn each screen has, for the table's output_turn and output_software (compositor_table_outputs).
                for (unsigned int at = 0; at < lit; at++)
                {
                        compositor.output[at].turn = compositor.output[at].turned ? compositor.output[at].degrees : 0;
                        compositor.output[at].turn_software = compositor.output[at].turned == 2;
                }
                // The buffers the screens had before, which the crtcs now show no more.
                for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                        compositor_turn_retire(&retired[at]);
                // The crtcs no screen of the space has: a screen that went, one arranged dark, one left dark above.
                for (unsigned int at = 0; at < was_count; at++)
                {
                        int used = 0;

                        for (unsigned int now = 0; now < lit; now++)
                                used |= compositor.output[now].crtc == was_crtc[at];
                        if (!used)
                                compositor_crtc_off(was_crtc[at]);
                }
                for (unsigned int n = 0; n < found.count; n++)
                        if (found.current[n] && !rects[n][2])
                                compositor_crtc_off(found.crtc[n]);
                for (unsigned int at = 0; at < extras; at++)
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of extra[at]);
                if (old_fb && old_fb != compositor.fb)
                        compositor_scanout_free(old_fb, old_handle, old_pixels, old_bytes);

                if (!refused)
                        break;
                // A commit refused a screen: it is left dark, and the rest are taken again without it, from what was taken.
                for (unsigned int n = 0; n < found.count; n++)
                        if (refused >> n & 1)
                                compositor_kmsg((string_address) "compositor: the card refused a screen, which is left dark: ", 0,
                                                (string_address)found.name[n]);
                if (lit == 1)
                        return -COMPOSITOR_EINVAL;
                dropped |= refused;
                was_count = compositor.outputs;
                for (unsigned int at = 0; at < was_count; at++)
                {
                        memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                        memory_copy(was_names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                        was_crtc[at] = compositor.output[at].crtc;
                }
                first = 0;
        }

        // The crtcs show the compositor's scanout now, and no window's buffer: what a flip would have let go of is.
        if (carried)
                compositor_flip_done();
        compositor.layout++;
        compositor_table_outputs();
        for (unsigned int at = 0; at < compositor.outputs; at++)
                compositor.output[at].shows = 0;
        compositor.cursor.plane_scale = 0;
        if (!first)
                compositor_cursor_move();
        return 0;
}

static b32 compositor_card_open(void)
{
        compositor.card = (int)system_open_at(AT_FDCWD, (string_address) "/dev/dri/card0",
                                              FILE_READ_WRITE | O_CLOEXEC);
        if (compositor.card < 0)
                return compositor.card;

        if (system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0)
                return -COMPOSITOR_EBUSY;

        {
                struct compositor_version version = {0};

                version.name_length = sizeof(compositor.driver) - 1;
                version.name = compositor.driver;
                if (system_control(compositor.card, COMPOSITOR_VERSION, address_of version) != 0)
                        compositor.driver[0] = 0;
        }

        return compositor_space_take(1);
}

/*
        The pointer: on the card's cursor plane where it has one, and drawn into
        the scanout where it has not, as the kernel desktop does. It has none
        where the driver's plane does not take a dumb buffer (i915 and xe
        stop the pipe), where Canvas was started without one (moonwater.cursor_plane=0,
        which the lane's pixel checks need, as a screendump has no plane in it),
        and where the card refuses what it is given; a card with no plane has
        a pointer that moves and presses all the same.

        The plane holds the shape the pointer wears, at the scale of the
        desktop but no larger than it takes, in one of two buffers: a shape
        change is painted into the one not showing and set, because a driver
        that keeps its framebuffer elsewhere sends the image only when the
        framebuffer changes. The software pointer is a rectangle of damage
        where it was and where it is, and is drawn over what is composed there.
*/

// The scale a pointer is drawn at: its own size when `moonwater canvas cursor size` gave one, and the desktop's
// when it did not, three times that while it is shaken large.
static int compositor_cursor_scale(void)
{
        int base = compositor.cursor.size ? compositor.cursor.size : compositor_px1(compositor.scale);

        return compositor.cursor.magnified ? 3 * base : base;
}

static void compositor_cursor_cell_damage(int x, int y, unsigned int shape, int scale)
{
        int left, top;

        compositor_cursor_cell(&left, &top, x, y, shape, scale);
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(left, top, CP_CURSOR_W * scale, CP_CURSOR_H * scale);
        compositor.damage_by = 0;
}

// Whether this card's cursor plane is worth using: the driver's name and the setting Canvas was started with.
static int compositor_cursor_plane_wanted(void)
{
        const char *name = compositor.driver;
        p8 setting[4] = {0};
        bipolar handle;

        if ((name[0] == 'i' && name[1] == '9' && name[2] == '1' && name[3] == '5' && !name[4]) ||
            (name[0] == 'x' && name[1] == 'e' && !name[2]))
                return 0;

        handle = system_open_at(AT_FDCWD, (string_address) "/sys/module/moonwater/parameters/cursor_plane",
                                FILE_READ | O_CLOEXEC);
        if (handle >= 0)
        {
                bipolar got = system_read_once(handle, setting, sizeof(setting) - 1);

                system_close(handle);
                if (got > 0 && (setting[0] == 'N' || setting[0] == 'n' || setting[0] == '0'))
                        return 0;
        }
        return 1;
}

// A shape painted into the buffer of the plane that is not showing, which is the one shown from here on: paints counts
// them, and a screen's plane given the newest wears paints.
static int compositor_cursor_paint(unsigned int shape, int fitted)
{
        struct compositor_map_dumb map = {.handle = compositor.cursor.handle[compositor.cursor.back]};
        long mapped;

        if (system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                return -1;

        mapped = compositor_call(syscall(mmap), 0, CP_PLANE * CP_PLANE * 4, 3, 1, (positive)compositor.card, map.offset);
        if (mapped < 0 && mapped > -4096)
                return -1;

        memory_zero((void *)mapped, CP_PLANE * CP_PLANE * 4);
        {
                struct compositor_target t = {(unsigned int *)mapped, CP_PLANE, CP_PLANE, CP_PLANE, 0, 0, CP_PLANE, CP_PLANE};

                compositor_cursor_draw(&t, compositor_cursor_hot[shape] * fitted, compositor_cursor_hot[shape] * fitted,
                                       shape, fitted, compositor_cursor_ink(0, 0xff000000u),
                                       compositor_cursor_ink(1, 0xff000000u));
        }
        compositor_call(syscall(munmap), (positive)mapped, CP_PLANE * CP_PLANE * 4, 0, 0, 0, 0);

        compositor.cursor.back ^= 1;
        compositor.cursor.paints++;
        compositor.cursor.plane_shape = (int)shape;
        compositor.cursor.plane_scale = fitted;
        return 0;
}

/*
        The plane is a crtc's, and the pointer is on one screen or, at an
        edge two screens share, on both: each screen's plane shows the cell
        at left, top where the cell meets its screen, at the cell's place on
        that screen, and nothing where it does not, so no screen keeps a
        pointer the hand has taken to another. A plane that wears an older
        shape is given the one painted last; the rest are moved. Answers -1
        when a plane refuses the shape.
*/
static int compositor_cursor_planes(int left, int top)
{
        unsigned int count, front = compositor.cursor.handle[compositor.cursor.back ^ 1];
        const int (*screens)[4] = compositor_screens(&count);

        for (unsigned int at = 0; at < count; at++)
        {
                struct compositor_output *output = &compositor.output[at];
                unsigned int crtc = compositor.outputs ? output->crtc : compositor.crtc;
                int x, y;

                if (compositor_screen_cell(screens[at], left, top, CP_PLANE, &x, &y))
                {
                        if (output->shows != (unsigned int)compositor.cursor.paints)
                        {
                                struct compositor_cursor give = {3, crtc, x, y, CP_PLANE, CP_PLANE, front};

                                if (system_control(compositor.card, COMPOSITOR_CURSOR, address_of give) != 0)
                                        return -1;
                                output->shows = (unsigned int)compositor.cursor.paints;
                        }
                        else
                        {
                                struct compositor_cursor move = {2, crtc, x, y, 0, 0, 0};

                                compositor.cursor.moves += system_control(compositor.card, COMPOSITOR_CURSOR, address_of move) == 0;
                        }
                }
                else if (output->shows)
                {
                        struct compositor_cursor gone = {1, crtc, 0, 0, CP_PLANE, CP_PLANE, 0};

                        system_control(compositor.card, COMPOSITOR_CURSOR, address_of gone);
                        output->shows = 0;
                }
        }
        return 0;
}

// Every screen's plane showing nothing; the next shape wanted is painted again.
static void compositor_cursor_planes_off(void)
{
        unsigned int count;

        compositor_screens(&count);
        for (unsigned int at = 0; at < count; at++)
        {
                struct compositor_cursor gone = {1, compositor.outputs ? compositor.output[at].crtc : compositor.crtc, 0, 0,
                                                 CP_PLANE, CP_PLANE, 0};

                system_control(compositor.card, COMPOSITOR_CURSOR, address_of gone);
                compositor.output[at].shows = 0;
        }
        compositor.cursor.plane_shape = -1;
}

static void compositor_cursor_start(void)
{
        struct compositor_dumb dumb[2] = {{CP_PLANE, CP_PLANE, 32, 0, 0, 0, 0}, {CP_PLANE, CP_PLANE, 32, 0, 0, 0, 0}};

        compositor.cursor.plane_shape = -1;
        if (compositor.card < 0 || !compositor_cursor_plane_wanted() ||
            system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb[0]) != 0 ||
            system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb[1]) != 0)
        {
                compositor.cursor.soft = compositor.card >= 0;
                return;
        }

        compositor.cursor.handle[0] = dumb[0].handle;
        compositor.cursor.handle[1] = dumb[1].handle;
}

// Where the pointer is drawn: the pointer, which is already where it will be (compositor_pointer_shown).
static int compositor_drawn_x(void)
{
        return compositor.pointer_x;
}

static int compositor_drawn_y(void)
{
        return compositor.pointer_y;
}

static void compositor_cursor_move(void)
{
        int scale = compositor_cursor_scale();
        unsigned int shape = compositor.cursor.shape;

        // A finger was the last thing used: no pointer under it, on a plane or drawn, until a mouse or a pad moves.
        if (compositor.touch.hidden)
        {
                if (compositor.cursor.handle[0] && compositor.cursor.plane_shape >= 0)
                        compositor_cursor_planes_off();
                if (compositor.cursor.drawn)
                {
                        compositor_cursor_cell_damage(compositor.cursor.x, compositor.cursor.y, compositor.cursor.drawn_shape,
                                                      compositor.cursor.drawn_scale);
                        compositor.cursor.drawn = 0;
                }
                return;
        }

        if (compositor_cursor_in_scene())
        {
                // A plane that was showing the pointer shows nothing now, and says so again when it is wanted.
                if (!compositor.cursor.soft && compositor.cursor.plane_shape >= 0)
                        compositor_cursor_planes_off();
                if (compositor.cursor.drawn)
                        compositor_cursor_cell_damage(compositor.cursor.x, compositor.cursor.y, compositor.cursor.drawn_shape,
                                                      compositor.cursor.drawn_scale);
                compositor.cursor.x = compositor_drawn_x();
                compositor.cursor.y = compositor_drawn_y();
                compositor.cursor.drawn_shape = shape;
                compositor.cursor.drawn_scale = scale;
                compositor.cursor.drawn = 1;
                compositor_cursor_cell_damage(compositor.cursor.x, compositor.cursor.y, shape, scale);
                return;
        }

        if (!compositor.cursor.handle[0])
                return;

        // The pointer drawn into the scanout, which a size the plane holds does not need: it is taken off.
        if (compositor.cursor.drawn)
        {
                compositor_cursor_cell_damage(compositor.cursor.x, compositor.cursor.y, compositor.cursor.drawn_shape,
                                              compositor.cursor.drawn_scale);
                compositor.cursor.drawn = 0;
        }

        {
                int fitted = compositor_cursor_fit(scale), left, top;

                compositor_cursor_cell(&left, &top, compositor_drawn_x(), compositor_drawn_y(), shape, fitted);

                if (((int)shape == compositor.cursor.plane_shape && fitted == compositor.cursor.plane_scale) ||
                    compositor_cursor_paint(shape, fitted) == 0)
                        if (compositor_cursor_planes(left, top) == 0)
                                return;

                // The card will not take it: the pointer is drawn from here on.
                compositor_cursor_planes_off();
                compositor.cursor.handle[0] = compositor.cursor.handle[1] = 0;
                compositor.cursor.refused++;
                compositor.cursor.soft = 1;
                compositor_cursor_move();
        }
}

/*
        What a sleep does to the screen. The card is left to the compositor and
        nothing tells it the machine slept, so it works it out from the clocks:
        CLOCK_BOOTTIME runs through a sleep and CLOCK_MONOTONIC does not, and
        the first thing that wakes the loop afterwards (the key that woke the
        machine, a power button, a lid) finds them further apart than they
        were. A real card's driver restores the state it saved, and a panel
        that state says is on and the hardware is not showing is answered by a
        plane update as if nothing had happened; only an active change makes it
        program the pipe again, which is turning the crtc off and on. That is
        done once, at half a second, where the driver has settled, and not for
        a virtual card, which has no such drift and does not always get its
        host window back from an off.
*/
static int compositor_virtual(void)
{
        static const char *const guests[] = {"bochs-drm", "virtio_gpu", "qxl", "vmwgfx", "cirrus-qemu", "hyperv_drm", "vkms"};

        return compositor_driver_in(guests, sizeof(guests) / sizeof(guests[0]));
}

// How far ahead of the clock that stops in a sleep the one that does not is, in nanoseconds.
static positive compositor_slept_clock(void)
{
        timespec boot = {0, 0}, mono = {0, 0};
        long long ahead;

        clock_gettime(CLOCK_BOOTTIME, address_of boot);
        clock_gettime(CLOCK_MONOTONIC, address_of mono);
        ahead = ((long long)boot.tv_sec - (long long)mono.tv_sec) * 1000000000ll +
                ((long long)boot.tv_nsec - (long long)mono.tv_nsec);
        return ahead < 0 ? 0 : (positive)ahead;
}

static void compositor_resume(int cycle)
{
        if (compositor.card < 0)
                return;

        // Every screen's pipe, each showing its rectangle of the space.
        for (unsigned int at = 0; cycle && !compositor_virtual() && at < compositor.outputs; at++)
        {
                struct compositor_output *output = &compositor.output[at];
                struct compositor_crtc off = {0, 0, output->crtc, 0, 0, 0, 0, 0, {0}};
                struct compositor_crtc on = {(unsigned long)address_of output->connector, 1, output->crtc,
                                             output->fb ? output->fb : compositor.fb,
                                             output->turned == 2 ? 0 : (unsigned int)compositor.screen[at][0],
                                             output->turned == 2 ? 0 : (unsigned int)compositor.screen[at][1],
                                             0, 1, output->mode};

                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of off);
                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of on);
        }

        // What the plane wears and where it was is whatever the driver put back: say it all again.
        compositor.cursor.plane_scale = 0;
        compositor.mirror_stale = 1;
        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_cursor_move();
}

// A line in the machine's log: the compositor's own output goes where its parent put it.
static void compositor_kmsg(string_address text, positive value, string_address tail)
{
        p8 line[160];
        positive length = 0;
        bipolar handle;

        while (text[length] && length < 100)
        {
                line[length] = text[length];
                length++;
        }
        if (value)
                length += positive_into_string(line + length, value);
        for (positive at = 0; tail[at] && length < sizeof(line) - 1; at++)
                line[length++] = tail[at];
        line[length++] = '\n';

        handle = system_open_at(AT_FDCWD, (string_address) "/dev/kmsg", O_WRONLY | O_CLOEXEC);
        if (handle < 0)
                return;
        system_write_once(handle, line, length);
        system_close(handle);
}

// Looked at whenever the loop wakes: whether the machine slept since it last did.
static void compositor_wake_check(positive now)
{
        positive asleep = compositor_slept_clock();

        if (asleep > compositor.asleep + COMPOSITOR_SLEPT_NS)
        {
                compositor_resume(0);
                compositor.wake_stage = 1;
                compositor.wake_at = now + COMPOSITOR_WAKE_FIRST_NS;
        }
        compositor.asleep = asleep;
}

// The redraws that follow a wake, when they are due.
static void compositor_wake_pass(positive now)
{
        if (!compositor.wake_stage || now < compositor.wake_at)
                return;

        compositor_resume(compositor.wake_stage == 1);
        if (++compositor.wake_stage == 3)
                compositor.wake_stage = 0;
        else
                compositor.wake_at = now + COMPOSITOR_WAKE_SECOND_NS;
}

/*
        How a file of /root (and the compositor's own of /run) is opened to be read: not through a link, which only root
        could plant there but which would have the root compositor read whatever it names, and without waiting, which a
        FIFO in a file's place would have it do for ever, the desktop with it. A file that is either is not there.
*/
#define COMPOSITOR_SETTING_OPEN (FILE_READ | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW)

// The layout the machine keeps, which `moonwater keyboard` writes; a machine that has not kept one has the US.
static void compositor_layout_read(void)
{
        char name[8] = {0};
        bipolar handle = system_open_at(AT_FDCWD, (string_address) "/root/keyboard", COMPOSITOR_SETTING_OPEN);
        int layout = -1;

        if (handle >= 0)
        {
                bipolar got = system_read_once(handle, name, sizeof(name) - 1);

                system_close(handle);
                for (bipolar at = 0; at < (got > 0 ? got : 0); at++)
                        if (name[at] == '\n' || name[at] == ' ')
                                name[at] = 0;
                layout = compositor_layout_index(name);
        }

        if (layout >= 0 && layout != compositor.keymap.layout)
        {
                compositor.keymap.layout = (unsigned char)layout;
                compositor.keymap.dead = 0;
        }
}

// A file's text, as far as room holds it, newlines and all: "" when the file is not there.
static void compositor_text(const char *path, char *text, unsigned int room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, COMPOSITOR_SETTING_OPEN);
        bipolar got = 0;

        if (handle >= 0)
        {
                got = system_read_once(handle, text, room - 1);
                system_close(handle);
        }
        text[got < 0 ? 0 : got] = 0;
}

// The word kept in a file of /root, cut at its first space or line end: empty when there is none.
static void compositor_word(const char *path, char *word, unsigned int room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, COMPOSITOR_SETTING_OPEN);
        bipolar got = 0, at = 0;

        if (handle >= 0)
        {
                got = system_read_once(handle, word, room - 1);
                system_close(handle);
        }
        while (at < got && word[at] != '\n' && word[at] != ' ' && word[at] != '\r')
                at++;
        word[at] = 0;
}

/*
        What `moonwater canvas background` and `moonwater canvas cursor` kept in /root: the desktop's ink,
        the pointer's size (1 to 4, or the desktop's own) and its colours (light or dark, or the light it
        always was). Read when the compositor starts and again when a file of them is written; a word it
        does not know is the default, as if nothing were kept. What changed is drawn at once.
*/
static void compositor_look_read(void)
{
        char word[16];
        long colour;
        int size = 0, look = 0;

        compositor_word(SETTING_BACKGROUND_PATH, word, sizeof(word));
        colour = word[0] ? window_colour_parse((string_address)word) : -1;
        if ((unsigned int)(colour < 0 ? COMPOSITOR_INK_DESKTOP : colour) != compositor_ground)
        {
                compositor_ground = (unsigned int)(colour < 0 ? COMPOSITOR_INK_DESKTOP : colour);
                compositor_damage(0, 0, compositor.width, compositor.height);
        }

        compositor_word(SETTING_CURSOR_SIZE_PATH, word, sizeof(word));
        if (word[0] >= '1' && word[0] <= '4' && !word[1])
                size = word[0] - '0';
        compositor_word(SETTING_CURSOR_LOOK_PATH, word, sizeof(word));
        if (!string_compare((string_address)word, (string_address) "light"))
                look = 1;
        else if (!string_compare((string_address)word, (string_address) "dark"))
                look = 2;

        if (size != compositor.cursor.size || look != compositor.cursor.look)
        {
                compositor.cursor.size = size;
                compositor.cursor.look = look;
                // The plane wears the old one, and is painted again (a scale it is not at); a plane that is
                // to be hidden still is told so.
                compositor.cursor.plane_scale = 0;
                compositor_cursor_move();
        }
}

/*
        The theme of Canvas's own windows, as `moonwater canvas theme`, `canvas theme accent` and `canvas
        motion` keep it, into the window table's header (theme, accent, motion), with themed counted up and
        every window knocked when any of it changes; auto is light from 7 to 19 by the local clock, and is
        worked out again each minute (compositor_theme_tick). Every glass window is tinted to the theme's
        side from then on, its blur made again.
*/
static void compositor_theme_set(unsigned int theme, unsigned int accent, unsigned int motion)
{
        struct windows *table = compositor.table;

        if (!table || (table->theme == theme && table->accent == accent && table->motion == motion && table->themed))
                return;
        table->theme = theme;
        table->accent = accent;
        table->motion = motion;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
        if (compositor.theme_side != !theme)
        {
                compositor.theme_side = !theme;
                for (int row = 0; row < WINDOWS; row++)
                        if (compositor.glassy[row])
                                compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        for (int row = 0; row < WINDOWS; row++)
                if (table->pid[row])
                        compositor.woken |= 1u << row;
}

// Whether the local clock says day (SETTING_THEME_DAY_FROM to SETTING_THEME_DAY_UNTIL).
static unsigned int compositor_theme_day(void)
{
        timespec real = {0, 0};
        tm broken;
        time_t seconds;

        clock_gettime(CLOCK_REALTIME, address_of real);
        seconds = real.tv_sec;
        if (!localtime_r(&seconds, &broken))
                return 1;
        return broken.tm_hour >= SETTING_THEME_DAY_FROM && broken.tm_hour < SETTING_THEME_DAY_UNTIL;
}

/*
        The theme's inks for the compositor's own chrome, the minimal look kept (frameless, 8 px corners, a 1 px line):
        the lit line of the window with the keys is the kit's primary at 30% over the shadow on the dark theme (white's
        is 4d4d4d, the line it always was), and the primary itself on the light one (the kit's ink where the accent is
        too light to show); a tab's glass is tinted toward the theme's surface, and the tab of the window with the keys
        toward its primary. A table the compositor has not themed yet has the standard, as the kit has.
*/
struct compositor_chrome
{
        struct ui_theme roles;
        unsigned int dark; // the theme the roles are made for: UI_THEME_DARK, else light
};

// The roles the chrome is drawn with, made by the kit (theme.h) for the window table's theme and accent, made again
// when either changes. A table not themed yet has the standard: dark, with white.
static const struct compositor_chrome *compositor_chrome_now(void)
{
        static struct compositor_chrome chrome;
        static unsigned int made, made_dark, made_accent;
        struct windows address_to table = compositor.table;
        unsigned int themed = table && table->themed, dark = themed ? table->theme == UI_THEME_DARK : 1;
        unsigned int accent = themed ? table->accent & 0xffffffu : UI_ACCENT_STANDARD;

        if (!made || made_dark != dark || made_accent != accent)
        {
                ui_theme_make(&chrome.roles, dark, accent);
                chrome.dark = dark;
                made = 1, made_dark = dark, made_accent = accent;
        }
        return &chrome;
}

// The lit line of the window with the keys: on the dark theme the primary at 30% over the shadow, which on the
// standard is white's 4d4d4d; on the light one the primary itself.
static unsigned int compositor_line_lit(void)
{
        const struct compositor_chrome *chrome = compositor_chrome_now();

        return (chrome->dark ? ui_mix(chrome->roles.role[UI_PRIMARY], 30, chrome->roles.role[UI_SHADOW])
                             : chrome->roles.role[UI_PRIMARY]) & 0xffffffu;
}

static unsigned int compositor_ink_shadow(void)
{
        return compositor_chrome_now()->roles.role[UI_SHADOW] & 0xffffffu;
}

// The kit's selection is the bright accent at 28%: its colour is the snap's, its alpha is not (compositor_snap_ink).
static unsigned int compositor_ink_selection(void)
{
        return compositor_chrome_now()->roles.role[UI_SELECTION];
}

/*
        The snap's tint, read on the wallpaper it is over. The selection's colour at COMPOSITOR_SNAP_PERCENT is kept while its
        edge reads COMPOSITOR_SNAP_READ against the wallpaper (the dark wallpaper: white at 40 percent is 3.8 to 1). Where it
        does not, the colour is mixed a step at a time toward black or white, whichever stands out more from the wallpaper,
        and if no colour reads at that percent the tint is made more opaque, 10 percent at a time to
        COMPOSITOR_SNAP_PERCENT_MOST (a light wallpaper: the light theme's grey at 40 percent is 2.0 to 1 at best; at 50 it
        is 3.0 in the colour 262628). Made once for each wallpaper and selection.
*/
static unsigned int compositor_snap_ink(void)
{
        static unsigned int made_ground, made_selection, made_ink;
        unsigned int ground = compositor_ground | 0xff000000u, selection = compositor_ink_selection() | 0xff000000u;
        unsigned int pole, ink = 0;

        if (made_ink && made_ground == ground && made_selection == selection)
                return made_ink;
        pole = ui_contrast(0xff000000u, ground) >= ui_contrast(0xffffffffu, ground) ? 0xff000000u : 0xffffffffu;
        for (unsigned int percent = COMPOSITOR_SNAP_PERCENT; percent <= COMPOSITOR_SNAP_PERCENT_MOST && !ink; percent += 10)
        {
                unsigned int at = selection;

                for (int step = 0; step <= 48 && !ink; step++)
                {
                        if (ui_contrast(ui_mix(at, percent, ground), ground) >= COMPOSITOR_SNAP_READ)
                                ink = (at & 0xffffffu) | (percent * 255 + 50) / 100 << 24;
                        else
                                at = ui_mix(at, 95, pole);
                }
        }
        if (!ink)
                ink = (selection & 0xffffffu) | COMPOSITOR_SNAP_PERCENT_MOST * 255 / 100 << 24;
        made_ground = ground, made_selection = selection, made_ink = ink;
        return ink;
}

// The colour a tab's glass is tinted toward: the theme's surface, or its primary for the tab of the window with the keys.
static unsigned int compositor_tab_target(b32 lit)
{
        return compositor_chrome_now()->roles.role[lit ? UI_PRIMARY : UI_SURFACE] & 0xffffffu;
}

static bool compositor_setting_kept(const char *path, const char *word, long *value);

// The wheel's glide (ui_scroll_wheel), as the table keeps it: a change knocks every window, as a theme change does.
static void compositor_wheel_publish(unsigned int off)
{
        struct windows *table = compositor.table;

        if (!table || table->wheel_off == off)
                return;
        table->wheel_off = off;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
}

// The caret's blink and width (ui.c's ui_caret_wave and ui_caret_thick), as the table keeps them, each one more than its place:
// a change knocks every window, as a theme change does.
static void compositor_caret_publish(unsigned int speed, unsigned int thick)
{
        struct windows *table = compositor.table;

        if (!table || (table->caret_speed == speed && table->caret_thick == thick))
                return;
        table->caret_speed = speed;
        table->caret_thick = thick;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
}

// The language the windows speak (ui/language.c), as the table keeps it: a change knocks every window, as a theme change does.
static void compositor_language_publish(unsigned int code)
{
        struct windows *table = compositor.table;

        if (!table || table->language == code)
                return;
        table->language = code;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
}

// The text's size in percent (ui.c's ui_em), as the table keeps it, 0 for 100: a change knocks every window, as a theme change does.
static void compositor_text_publish(unsigned int percent)
{
        struct windows *table = compositor.table;

        if (!table || table->text_scale == percent)
                return;
        table->text_scale = percent;
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
}

static void compositor_theme_read(void)
{
        char word[16];
        long accent = -1, smooth = 0, blink = 0, thick = 0, text = 100;
        unsigned int motion;

        compositor_word(SETTING_THEME_PATH, word, sizeof(word));
        // Dark is the theme when nothing is kept (and when what is kept is none of the words).
        compositor.theme_word = string_equals((string_address)word, (string_address) "light") ? 0
                                : string_equals((string_address)word, (string_address) "auto") ? 2 : 1;
        compositor_word(SETTING_ACCENT_PATH, word, sizeof(word));
        if (word[0] && !string_equals((string_address)word, (string_address) "reset") && !string_equals((string_address)word, (string_address) "default"))
                accent = window_colour_parse((string_address)word);
        compositor_word(SETTING_MOTION_PATH, word, sizeof(word));
        motion = string_equals((string_address)word, (string_address) "reduced");
        compositor_word(SETTING_WHEEL_SMOOTH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_WHEEL_SMOOTH_PATH, word, &smooth);
        compositor_wheel_publish(smooth != 0);
        compositor_word(SETTING_CARET_BLINK_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_CARET_BLINK_PATH, word, &blink);
        compositor_word(SETTING_CARET_THICKNESS_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_CARET_THICKNESS_PATH, word, &thick);
        compositor_caret_publish((unsigned int)blink + 1, (unsigned int)thick + 1);
        // English when nothing is kept, and when what is kept is not a code (default).
        compositor_word(SETTING_LANGUAGE_PATH, word, sizeof(word));
        compositor_language_publish(windows_language_code(word, string_length((string_address)word)));
        // 100 percent when nothing is kept (the table says 0 then, as every window takes it).
        compositor_word(SETTING_TEXT_SCALE_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_TEXT_SCALE_PATH, word, &text);
        compositor_text_publish(text == 100 ? 0 : (unsigned int)text);
        // White is the accent when none is kept: white fills with the dark ink on them, on the dark theme's surfaces.
        compositor_theme_set(compositor.theme_word == 2 ? !compositor_theme_day() : compositor.theme_word,
                             accent < 0 ? 0xffffffu : (unsigned int)accent, motion);
}

// Auto, again by the clock: a minute's wake is enough.
static void compositor_theme_tick(void)
{
        if (compositor.theme_word == 2 && compositor.table)
                compositor_theme_set(!compositor_theme_day(), compositor.table->accent, compositor.table->motion);
}

/*
        What the settings table keeps for the compositor (live is SETTING_LIVE_COMPOSITOR): the file of a
        row, as the row says it. A value is the row's standard when nothing is kept, and kept says that
        something is.
*/
static bool compositor_setting_kept(const char *path, const char *word, long *value)
{
        const struct settings_row *row = setting_by_path((string_address)path);

        *value = 0;
        return row && setting_kept(row, (string_address)word, value);
}

static void compositor_clip_settings(void);
#ifndef SHELL_NO_GPU
static void compositor_gpu_settings(void);
#endif

// Whether a name in /root is the file of a row the compositor takes at once.
static bool compositor_setting_file(const char *name)
{
        for (unsigned int at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].live == SETTING_LIVE_COMPOSITOR && settings_rows[at].path &&
                    string_equals((string_address)name, (string_address)settings_rows[at].path + sizeof("/root/") - 1))
                        return true;
        return false;
}

// A keyboard's repeat as the settings have it, when that is not what it has: delay, then period.
static void compositor_repeat_apply(unsigned int at)
{
        int want[2];

        if (!compositor.repeats[at] || compositor.inputs[at] < 0)
                return;
        compositor_repeat_target(compositor.repeat_kept, compositor.repeat_delay, compositor.repeat_rate,
                                 compositor.repeat_own[at], want);
        if (want[0] == compositor.repeat_now[at][0] && want[1] == compositor.repeat_now[at][1])
                return;
        if (system_control(compositor.inputs[at], COMPOSITOR_EVIOCSREP, want) == 0)
        {
                compositor.repeat_now[at][0] = want[0];
                compositor.repeat_now[at][1] = want[1];
        }
}

/*
        `moonwater canvas cursor shake` (and its delay), `moonwater keyboard repeat` (delay and rate) and
        `moonwater mouse` (sensitivity and acceleration), as the words their files hold (none is an empty
        word): taken at once. The defaults are what the compositor did before there were settings, to the
        bit; a repeat that is not kept leaves each keyboard the way it opened, and one that was kept and is
        put back is told to the keyboards as they were.
*/
static void compositor_input_settings_set(const char *shake, const char *shake_delay, const char *sensitivity,
                                          const char *acceleration, const char *repeat_delay, const char *repeat_rate)
{
        long value;
        int was_off = compositor.cursor.shake_off;

        compositor.cursor.shake_off = compositor_setting_kept(SETTING_SHAKE_PATH, shake, &value) && value == 1;
        compositor_setting_kept(SETTING_SHAKE_DELAY_PATH, shake_delay, &value);
        compositor.cursor.shake_ns = (positive)value * 1000000ull;
        compositor_setting_kept(SETTING_SENSITIVITY_PATH, sensitivity, &value);
        compositor.sensitivity = (int)value;
        compositor.flat = compositor_setting_kept(SETTING_ACCELERATION_PATH, acceleration, &value) && value == 1;

        compositor.repeat_kept = 0;
        if (compositor_setting_kept(SETTING_REPEAT_DELAY_PATH, repeat_delay, &value))
        {
                compositor.repeat_kept |= 1;
                compositor.repeat_delay = (int)value;
        }
        if (compositor_setting_kept(SETTING_REPEAT_RATE_PATH, repeat_rate, &value))
        {
                compositor.repeat_kept |= 2;
                compositor.repeat_rate = (int)value;
        }
        for (unsigned int at = 0; at < compositor.input_count; at++)
                compositor_repeat_apply(at);

        // A pointer that is large is no longer shaken for when shaking is turned off.
        if (compositor.cursor.shake_off && !was_off && (compositor.cursor.magnified || compositor.cursor.magnify))
        {
                compositor.cursor.magnified = compositor.cursor.magnify = 0;
                compositor_cursor_move();
        }
}

/*
        The taskbar's settings: its position, the screens it is on (primary, or all: a band on every screen), its
        battery and which way a side band's text faces (the place of the word kept among center and edge, the row's
        standard when none is), as the files in /root hold them. Read when the compositor starts and again when a file
        of them is written. Every band is damaged where it was as well as where it is now, a band on a screen that had
        none is given its memory, and when a band moved, came or went a window that fills a screen's room fills the
        room the bands leave now.
*/
static void compositor_room_follow(void);
static void compositor_band_room(void);

// Where each band is, a rectangle each and the rest none: what a change of the settings is held against.
static void compositor_bands_where(int (*rects)[4])
{
        memory_zero(rects, sizeof(int) * 4 * WINDOWS_OUTPUTS);
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();

                taskbar_band(&view, &rects[compositor.band_at][0], &rects[compositor.band_at][1], &rects[compositor.band_at][2],
                             &rects[compositor.band_at][3]);
        }
        compositor.band_at = 0;
}

static void compositor_taskbar_settings_read(void)
{
        char position[16], display[16], tune[512], text[16], tray[SETTING_TRAY_MOST + 8], launcher[16];
        int was[WINDOWS_OUTPUTS][4], now[WINDOWS_OUTPUTS][4];
        long edge;

        compositor_word(SETTING_TASKBAR_POSITION_PATH, position, sizeof(position));
        compositor_word(SETTING_TASKBAR_DISPLAY_PATH, display, sizeof(display));
        compositor_text(TASKBAR_TUNE_PATH, tune, sizeof(tune));
        compositor_text(SETTING_TASKBAR_TRAY_PATH, tray, sizeof(tray));
        // The standard tray in 12 hours, or with the date beside the clock, is a format of its own; a format of the person's own says
        // its hours and its date itself.
        if (!tray[0] || tray[0] == '\n' || string_equals((string_address)tray, (string_address) "default") ||
            string_equals((string_address)tray, (string_address)SETTING_TASKBAR_TRAY_STANDARD))
        {
                compositor_word(SETTING_CLOCK_FORMAT_PATH, text, sizeof(text));
                if (string_equals((string_address)text, (string_address) "12h"))
                        string_copy_bounded((p8 *)tray, (string_address)SETTING_TASKBAR_TRAY_STANDARD_12H, sizeof(tray));
                compositor_word(SETTING_DATE_FORMAT_PATH, text, sizeof(text));
                if (text[0] == 'y' || text[0] == 'd' || text[0] == 'm')
                {
                        if (!tray[0] || tray[0] == '\n' || string_equals((string_address)tray, (string_address) "default"))
                                string_copy_bounded((p8 *)tray, (string_address)SETTING_TASKBAR_TRAY_STANDARD, sizeof(tray));
                        string_append_bounded((string_address)tray,
                                              (string_address)(text[0] == 'y'   ? SETTING_TASKBAR_TRAY_DATE_YMD
                                                               : text[0] == 'd' ? SETTING_TASKBAR_TRAY_DATE_DMY
                                                                                : SETTING_TASKBAR_TRAY_DATE_MDY),
                                              sizeof(tray));
                }
        }
        compositor_word(SETTING_TASKBAR_TEXT_PATH, text, sizeof(text));
        compositor_setting_kept(SETTING_TASKBAR_TEXT_PATH, text, &edge);
        compositor_word(SETTING_TASKBAR_LAUNCHER_PATH, launcher, sizeof(launcher));
        compositor_bands_where(was);
        compositor_tabs_changed();
        taskbar_settings_set(position, display, tune);
        taskbar_text_set(edge == 1);
        taskbar_tray_set(tray);
        taskbar_launcher_set(string_equals((string_address)launcher, (string_address) "on"));
        compositor_band_room();
        compositor_tabs_changed();
        compositor_bands_where(now);
        if (memory_compare(was, now, sizeof(was)))
                compositor_room_follow();
}

static void compositor_touch_settings_read(void);
static void compositor_predict_read(void);

// Read when the compositor starts and again when a file of them is written.
static void compositor_input_settings_read(void)
{
        char words[6][16];
        static const char *const paths[6] = {SETTING_SHAKE_PATH, SETTING_SHAKE_DELAY_PATH, SETTING_SENSITIVITY_PATH,
                                             SETTING_ACCELERATION_PATH, SETTING_REPEAT_DELAY_PATH, SETTING_REPEAT_RATE_PATH};

        for (unsigned int at = 0; at < 6; at++)
                compositor_word(paths[at], words[at], sizeof(words[at]));
        compositor_input_settings_set(words[0], words[1], words[2], words[3], words[4], words[5]);
        compositor_taskbar_settings_read();
        compositor_touch_settings_read();
        compositor_predict_read();
}

_Static_assert(COMPOSITOR_INPUTS == WINDOWS_INPUTS, "an input slot is a row of the table's");

// What moonwater debug is shown of slot at: the device in it, or none, its counts started again. Once the table is made.
static void compositor_input_publish(unsigned int at)
{
        struct windows *table = compositor.table;

        if (!table)
                return;
        table->input_number[at] = at < compositor.input_count && compositor.inputs[at] >= 0 ? compositor.device_number[at] : -1;
        table->input_kinds[at] = compositor.input_kinds[at];
        memory_copy(table->input_id[at], compositor.input_id[at], sizeof(table->input_id[at]));
        memory_copy(table->input_name[at], compositor.input_name[at], sizeof(table->input_name[at]));
        table->input_wheel_read[at] = table->input_wheel_used[at] = table->input_dropped[at] = 0;
        table->input_wheel_last[at] = 0;
}

/*
        Every keyboard and pointer evdev has, now and as they come. Read
        beside Canvas's own handler, not grabbed from it: a grab would take
        Control-Alt-Backspace away from the kernel, and that chord is how a
        person gets out of a compositor that has stopped answering.

        The directory is watched before it is read, so a device that appears
        in between is in one or the other, and one that is in both is open
        once: the number in its name says which it is. A device that goes
        away is let go of where it is read (compositor_input_read) and its
        number is free again for the one that is plugged in next.
*/
static void compositor_space_retake(void);

// A touch screen as it is opened: no finger on it, and the range of its places, its slots' where it has slots.
static void compositor_touch_screen_open(unsigned int at, bipolar handle, int slotted)
{
        struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        struct compositor_absinfo x = {0}, y = {0};

        memory_zero(screen, sizeof(*screen));
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                screen->id[slot] = screen->was[slot] = -1;
        if (!compositor.touchscreen[at])
                return;
        screen->slots = slotted;
        system_control(handle, COMPOSITOR_EVIOCGABS(slotted ? COMPOSITOR_ABS_MT_POSITION_X : 0), address_of x);
        system_control(handle, COMPOSITOR_EVIOCGABS(slotted ? COMPOSITOR_ABS_MT_POSITION_Y : 1), address_of y);
        screen->range[0] = x.minimum, screen->range[1] = x.maximum;
        screen->range[2] = y.minimum, screen->range[3] = y.maximum;
        // The slot the kernel is on now, which its next events are about until it names another.
        if (slotted)
        {
                struct compositor_absinfo slot = {0};

                system_control(handle, COMPOSITOR_EVIOCGABS(COMPOSITOR_ABS_MT_SLOT), address_of slot);
                screen->slot = slot.value;
        }
}

static void compositor_input_add(unsigned int number)
{
        p8 path[32] = "/dev/input/event";
        unsigned long kinds = 0;
        p8 keys[96] = {0};
        unsigned long relative = 0, absolute = 0;
        positive length = string_length(path);
        bipolar handle;
        unsigned int at = 0;

        if (number >= COMPOSITOR_DEVICES || compositor.devices >> number & 1)
                return;

        while (at < compositor.input_count && compositor.inputs[at] >= 0)
                at++;
        if (at == COMPOSITOR_INPUTS)
                return;

        length += positive_into_string(path + length, number);
        path[length] = 0;

        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NONBLOCK);
        if (handle < 0)
                return;

        system_control(handle, COMPOSITOR_EVIOCGBIT(0, 8), address_of kinds);
        system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_KEY, 96), keys);
        system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_REL, 8), address_of relative);
        system_control(handle, COMPOSITOR_EVIOCGBIT(COMPOSITOR_EV_ABS, 8), address_of absolute);

        int keyboard = (kinds >> COMPOSITOR_EV_KEY & 1) &&
                       (keys[COMPOSITOR_KEY_A / 8] >> (COMPOSITOR_KEY_A % 8) & 1);
        int mouse = (kinds >> COMPOSITOR_EV_REL & 1) && (relative & 3) == 3;
        int tablet = (kinds >> COMPOSITOR_EV_ABS & 1) && (absolute & 3) == 3;
        // A power button and a lid are not typed on, and a machine may be woken by either.
        int wakes = ((kinds >> COMPOSITOR_EV_KEY & 1) && (keys[COMPOSITOR_KEY_POWER / 8] >> (COMPOSITOR_KEY_POWER % 8) & 1)) ||
                    (kinds >> COMPOSITOR_EV_SW & 1);
        unsigned int properties = 0;
        /*
                A screen fingers touch: INPUT_PROP_DIRECT, slots (ABS_MT_POSITION_X and Y) or a place and BTN_TOUCH, and
                no pen, whose screen is a tablet's as it always was. A virtual machine's (QEMU's virtio-multitouch) has
                slots and no ABS_X at all, and was not read.
        */
        int slotted = (kinds >> COMPOSITOR_EV_ABS & 1) && (absolute >> COMPOSITOR_ABS_MT_POSITION_X & 1) &&
                      (absolute >> COMPOSITOR_ABS_MT_POSITION_Y & 1);
        int touchscreen;

        system_control(handle, COMPOSITOR_EVIOCGPROP(4), address_of properties);
        touchscreen = (properties & 2) && !(keys[COMPOSITOR_BTN_TOOL_PEN / 8] >> (COMPOSITOR_BTN_TOOL_PEN % 8) & 1) &&
                      (slotted || (tablet && (keys[COMPOSITOR_BTN_TOUCH / 8] >> (COMPOSITOR_BTN_TOUCH % 8) & 1)));

        if (!keyboard && !mouse && !tablet && !wakes && !touchscreen)
        {
                system_close(handle);
                return;
        }

        compositor.absolute_x[at][0] = compositor.absolute_x[at][1] = 0;
        compositor.absolute_y[at][0] = compositor.absolute_y[at][1] = 0;
        compositor.held[at] = 0;
        compositor.wakeonly[at] = (unsigned char)(!keyboard && !mouse && !tablet && !touchscreen);
        // A wheel that reports in 120ths says so, and its detents are then not counted twice.
        compositor.hires[at] = (unsigned char)((relative >> COMPOSITOR_REL_WHEEL_HI & 1) |
                                               (relative >> COMPOSITOR_REL_HWHEEL_HI & 1) << 1);
        // What it is, for moonwater debug: a device that is not what this took it for says so there. A pad that
        // counts the fingers on it and is no screen (INPUT_PROP_DIRECT is a screen's mark) is a touchpad.
        {
                unsigned int kinds;

                compositor.touchpad[at] = (unsigned char)(tablet && !(properties & 2) &&
                                                          (keys[COMPOSITOR_BTN_TOOL_FINGER / 8] >> (COMPOSITOR_BTN_TOOL_FINGER % 8) & 1));
                compositor.pad[at] = (struct compositor_pad){0};
                compositor.touchscreen[at] = (unsigned char)touchscreen;
                compositor_touch_screen_open(at, handle, slotted);
                kinds = (keyboard ? WINDOWS_INPUT_KEYBOARD : 0) | (mouse ? WINDOWS_INPUT_MOUSE : 0) |
                        (tablet && !compositor.touchpad[at] && !touchscreen ? WINDOWS_INPUT_TABLET : 0) |
                        (compositor.wakeonly[at] ? WINDOWS_INPUT_WAKES : 0);
                kinds |=(relative >> COMPOSITOR_REL_WHEEL & 1 ? WINDOWS_INPUT_WHEEL : 0) |
                         (relative >> COMPOSITOR_REL_WHEEL_HI & 1 ? WINDOWS_INPUT_WHEEL_HI : 0) |
                         (relative >> COMPOSITOR_REL_HWHEEL & 1 ? WINDOWS_INPUT_HWHEEL : 0) |
                         (relative >> COMPOSITOR_REL_HWHEEL_HI & 1 ? WINDOWS_INPUT_HWHEEL_HI : 0) |
                         (keys[COMPOSITOR_BTN_LEFT / 8] >> (COMPOSITOR_BTN_LEFT % 8) & 1 ? WINDOWS_INPUT_BUTTONS : 0) |
                         (keys[COMPOSITOR_BTN_TOUCH / 8] >> (COMPOSITOR_BTN_TOUCH % 8) & 1 ? WINDOWS_INPUT_TOUCH : 0) |
                         (keys[COMPOSITOR_BTN_TOOL_FINGER / 8] >> (COMPOSITOR_BTN_TOOL_FINGER % 8) & 1 ? WINDOWS_INPUT_FINGERS : 0) |
                         (absolute >> COMPOSITOR_ABS_MT_POSITION_X & 1 ? WINDOWS_INPUT_SLOTS : 0) |
                         (properties & 1 ? WINDOWS_INPUT_POINTER : 0) | (properties & 2 ? WINDOWS_INPUT_DIRECT : 0);
                compositor.input_kinds[at] = kinds;
                memory_zero(compositor.input_id[at], sizeof(compositor.input_id[at]));
                memory_zero(compositor.input_name[at], sizeof(compositor.input_name[at]));
                system_control(handle, COMPOSITOR_EVIOCGID, compositor.input_id[at]);
                system_control(handle, COMPOSITOR_EVIOCGNAME(WINDOWS_INPUT_NAME - 1), compositor.input_name[at]);
        }
        // The events are stamped by the clock the speed of the hand is measured on.
        {
                int monotonic = 1;

                system_control(handle, COMPOSITOR_EVIOCSCLOCKID, address_of monotonic);
        }

        if (tablet)
        {
                struct compositor_absinfo x = {0}, y = {0};

                system_control(handle, COMPOSITOR_EVIOCGABS(0), address_of x);
                system_control(handle, COMPOSITOR_EVIOCGABS(1), address_of y);
                compositor.absolute_x[at][0] = x.minimum;
                compositor.absolute_x[at][1] = x.maximum;
                compositor.absolute_y[at][0] = y.minimum;
                compositor.absolute_y[at][1] = y.maximum;
                // A touchpad's units to a millimetre, which a finger's moves are measured in.
                compositor.pad[at].per_mm_x = x.resolution > 0 ? x.resolution
                                              : ((long)x.maximum - x.minimum) / COMPOSITOR_PAD_WIDTH_MM > 0
                                                  ? (int)(((long)x.maximum - x.minimum) / COMPOSITOR_PAD_WIDTH_MM)
                                                  : 1;
                compositor.pad[at].per_mm_y = y.resolution > 0 ? y.resolution : compositor.pad[at].per_mm_x;
                compositor.pad[at].next_x = compositor.pad[at].at_x = x.value;
                compositor.pad[at].next_y = compositor.pad[at].at_y = y.value;
        }

        compositor.inputs[at] = (int)handle;
        compositor.device_number[at] = (int)number;
        // A keyboard whose repeat can be asked is told what the settings keep, now and when they change.
        compositor.repeats[at] = 0;
        if (keyboard && system_control(handle, COMPOSITOR_EVIOCGREP, compositor.repeat_own[at]) == 0)
        {
                compositor.repeats[at] = 1;
                compositor.repeat_now[at][0] = compositor.repeat_own[at][0];
                compositor.repeat_now[at][1] = compositor.repeat_own[at][1];
                compositor_repeat_apply(at);
        }
        compositor.devices |= 1ul << number;
        compositor.input_count = at + 1 > compositor.input_count ? at + 1 : compositor.input_count;
        compositor_input_publish(at);
}

static void compositor_inputs_open(void)
{
        // IN_CREATE | IN_MOVED_TO on /dev/input, non-blocking and closed on exec.
        compositor.watch = (int)compositor_call(syscall(inotify_init1), COMPOSITOR_CLOEXEC | COMPOSITOR_NONBLOCK,
                                                0, 0, 0, 0, 0);
        if (compositor.watch >= 0 &&
            compositor_call(syscall(inotify_add_watch), (positive)compositor.watch,
                            (positive) "/dev/input", COMPOSITOR_IN_CREATE | COMPOSITOR_IN_MOVED_TO,
                            0, 0, 0) < 0)
        {
                system_close(compositor.watch);
                compositor.watch = -1;
        }

        // The keyboard setting is kept in /root/keyboard, written when it is changed.
        if (compositor.watch >= 0)
                compositor_call(syscall(inotify_add_watch), (positive)compositor.watch, (positive) "/root",
                                COMPOSITOR_IN_CLOSE_WRITE | COMPOSITOR_IN_MOVED_TO | COMPOSITOR_IN_CREATE,
                                0, 0, 0);

        for (unsigned int number = 0; number < COMPOSITOR_DEVICES; number++)
                compositor_input_add(number);
}

// What appeared in /dev/input since the last look: a name that is eventN is a device;
// and a keyboard written in /root is a layout that was changed.
static void compositor_inputs_watch(void)
{
        for (;;)
        {
                // struct inotify_event: wd, mask, cookie, len, and len bytes of name.
                unsigned int buffer[72]; // one event with the longest name: 16 bytes, and NAME_MAX + 1 after them
                long got = compositor_call(syscall(read), (positive)compositor.watch, (positive)buffer,
                                           sizeof(buffer), 0, 0, 0);

                for (long at = 0; at + 16 <= got;)
                {
                        const char *name = (const char *)(buffer + (at + 16) / 4);
                        unsigned int length = buffer[(at + 12) / 4], number = 0;

                        if (at + 16 + (long)length > got)
                                break;

                        if (compositor_setting_file(name) || string_equals((string_address)name, (string_address)TASKBAR_TUNE_NAME) ||
                            string_equals((string_address)name, (string_address)SETTING_CANVAS_DISPLAYS_PATH + sizeof("/root/") - 1))
                        {
                                compositor_clip_settings();
#ifndef SHELL_NO_GPU
                                compositor_gpu_settings();
#endif
                                compositor_look_read();
                                compositor_theme_read();
                                compositor_input_settings_read();
                                // The arrangement and the displays: the screens laid again, which probes them, so only when one
                                // of those files is the one written.
                                if (string_equals((string_address)name,
                                                  (string_address)SETTING_CANVAS_ARRANGE_PATH + sizeof("/root/") - 1) ||
                                    string_equals((string_address)name,
                                                  (string_address)SETTING_CANVAS_DISPLAYS_PATH + sizeof("/root/") - 1))
                                        compositor_space_retake();
                        }
                        else if (length > 8 && !string_compare_max((string_address)name, (string_address) "keyboard", 8) && !name[8] &&
                                 !compositor.layout_given)
                                compositor_layout_read();
                        else if (length > 5 && name[0] == 'e' && name[1] == 'v' && name[2] == 'e' &&
                            name[3] == 'n' && name[4] == 't')
                        {
                                for (unsigned int digit = 5; digit < length && name[digit] >= '0' &&
                                                             name[digit] <= '9' && number < 1000;
                                     digit++)
                                        number = number * 10 + (unsigned int)(name[digit] - '0');
                                compositor_input_add(number);
                        }

                        at += 16 + (long)length;
                }

                if (got <= 0)
                        return;
        }
}

/*
        An event into a row's columns. The compositor writes head and the
        program tail, and a full ring keeps what came first; a motion behind
        a motion the program has not read yet replaces it. tail is the
        program's, so it is read once and only believed as far as the ring
        is long. Whether it was put in the ring, or joined to the one before.

        A wheel behind a wheel the program has not read, turned the same way
        round its axis, is added to it once the program is half a ring
        behind: a swipe is sixty to a hundred and fifty events a second, a
        wheel that reports in 120ths sends eight to a notch, and a program
        that is busy for a moment had the rest of the swipe cut off where the
        ring filled. A program takes an event's number and leaves 0 where it
        was (windows_event), so a turn is added only to a number that is
        still there, in one compare and swap: one the program takes before
        the add is not added to, and the turn goes in an event of its own,
        and one it takes after has the turn in it. Below half a ring every
        turn is an event of its own, as it always was; and an event holds at
        most COMPOSITOR_WHEEL_JOIN, since a program bounds what one event may
        do (a terminal scrolls a screen at most).
*/
static void compositor_clip_gesture(int row);

static int compositor_post(int row, unsigned int type, unsigned int flags, int a, int b)
{
        struct windows *table = compositor.table;
        unsigned int head, used, last, at;

        if (row < 0)
                return 0;
        // A key pressed or a button pressed or let go is the person's: the clipboard lets the row copy and paste for a while.
        if ((type == WINDOW_EVENT_KEY && (flags & WINDOW_KEY_DOWN) && !(flags & WINDOW_KEY_REPEAT)) || type == WINDOW_EVENT_BUTTON)
                compositor_clip_gesture(row);

        // What a finger did says so: a program may scroll what a finger drags where a mouse would select.
        if (compositor.touching && type != WINDOW_EVENT_KEY)
                flags |= WINDOW_KEY_TOUCH;
        // A lead taken away is a place and no step of the hand's (WINDOW_KEY_SETTLE).
        if (compositor.retracting && type == WINDOW_EVENT_MOTION)
                flags |= WINDOW_KEY_SETTLE;
        // The right button of a long press says so, which a program may take as its own (WINDOW_KEY_HOLD).
        if (compositor.touch.holding && type == WINDOW_EVENT_BUTTON)
                flags |= WINDOW_KEY_HOLD;
        head = table->event_head[row];
        used = head - __atomic_load_n(&table->event_tail[row], __ATOMIC_ACQUIRE);
        last = (head - 1) % WINDOWS_EVENTS;
        compositor.woken |= 1u << row;

        if (type == WINDOW_EVENT_MOTION && used && used <= WINDOWS_EVENTS &&
            table->event_type[row][last] == WINDOW_EVENT_MOTION)
        {
                table->event_a[row][last] = a;
                table->event_b[row][last] = b;
                table->event_flags[row][last] = (unsigned short)flags;
                return 1;
        }

        if (type == WINDOW_EVENT_WHEEL && used >= WINDOWS_EVENTS / 2 && used <= WINDOWS_EVENTS &&
            table->event_type[row][last] == WINDOW_EVENT_WHEEL && table->event_b[row][last] == b &&
            table->event_flags[row][last] == flags)
        {
                int held = __atomic_load_n(&table->event_a[row][last], __ATOMIC_ACQUIRE);

                if (held && (held < 0) == (a < 0) && (long)held + a <= COMPOSITOR_WHEEL_JOIN &&
                    (long)held + a >= -COMPOSITOR_WHEEL_JOIN &&
                    __atomic_compare_exchange_n(&table->event_a[row][last], &held, held + a, 0, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE))
                        return 1;
        }

        if (used >= WINDOWS_EVENTS)
                return 0;

        at = head % WINDOWS_EVENTS;
        table->event_type[row][at] = (unsigned short)type;
        table->event_flags[row][at] = (unsigned short)flags;
        table->event_a[row][at] = a;
        table->event_b[row][at] = b;
        __atomic_store_n(&table->event_head[row], head + 1, __ATOMIC_RELEASE);
        return 1;
}

// Every row that was given something in this pass is knocked on, once.
static void compositor_knock(void)
{
        char byte = 0;

        for (int row = 0; row < WINDOWS; row++)
                if (compositor.woken & (1u << row))
                        system_write_once(compositor.wake[row], &byte, 1);
        compositor.woken = 0;
}

static void compositor_stack_remove(int row)
{
        unsigned int at = 0;

        while (at < compositor.stacked && compositor.stack[at] != row)
                at++;

        if (at >= compositor.stacked)
                return;

        memory_copy(compositor.stack + at, compositor.stack + at + 1, compositor.stacked - at - 1);
        compositor.stacked--;
}

// On top of the others.
static void compositor_raise(int row)
{
        if (compositor.stacked && compositor.stack[compositor.stacked - 1] == row)
                return;

        compositor_stack_remove(row);
        compositor.stack[compositor.stacked++] = (unsigned char)row;
        compositor_damage_row(row);
}

static unsigned int compositor_modifiers(void);

/*
        The keys that are down on the window that has the keyboard, by code.
        A key's repeats and its release follow its press, so a key held when
        the keyboard goes to another window is over for the one it was sent
        to: its release is sent there (its press was), and nothing further of
        it goes to the window that has the keys now, which never saw it go down.
*/
static void compositor_keys_release(void)
{
        int to = compositor.keys_to;

        compositor.keys_to = COMPOSITOR_NONE;
        if (to < 0)
                return;

        for (unsigned int code = 0; code < 128; code++)
                if (compositor.keys_down[code >> 3] & (1u << (code & 7)))
                        compositor_post(to, WINDOW_EVENT_KEY, compositor_modifiers(), (int)code, 0);
        memory_zero(compositor.keys_down, sizeof(compositor.keys_down));
}

// The keys to a window, or to none; both are knocked on, and read focus in the table.
static void compositor_focus(int row)
{
        int was = compositor.focus;

        // A popup never takes the keys from its window; the window that loses them loses its popups.
        if (was == row || (row >= 0 && compositor_popup_parent[row] >= 0))
                return;
        if (was >= 0)
                compositor_popups_close(was, -1);

        compositor.focus = row;
        compositor.table->focus = row < 0 ? WINDOWS : (unsigned int)row;
        if (compositor.keys_to != row)
                compositor_keys_release();
        // Both are knocked on, and both lines drawn again: the one with the keys is lit.
        if (was >= 0)
        {
                compositor.woken |= 1u << was;
                compositor_damage_row(was);
        }
        if (row >= 0)
        {
                compositor.woken |= 1u << row;
                compositor_damage_row(row);
        }
        compositor_tabs_changed();
}

// Whether a window is in the focus cycle: one that asked for no tab (WINDOW_NO_TAB, the identify overlays) is not, so
// neither Alt-Tab nor the refocus gives it the keys. Asked for them by its own program, it has them as any window does.
static b32 compositor_cycles(int row)
{
        return !(compositor.asks[row] & WINDOW_NO_TAB);
}

// The topmost window there is to give the keys to; none that is put away.
static void compositor_refocus(void)
{
        int next = COMPOSITOR_NONE;

        for (unsigned int at = compositor.stacked; at-- && next < 0;)
                if (compositor_visible(compositor.stack[at]) && compositor_popup_parent[compositor.stack[at]] < 0 &&
                    compositor_cycles(compositor.stack[at]))
                        next = compositor.stack[at];

        compositor_focus(next);
}

// A window's screen, by the rule of compositor_screen_owner: the one that holds the most of it.
static int compositor_screen_of(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        return count < 2 ? 0
                         : compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                                   (int)compositor.h[row]);
}

/*
        The corner of a window a finger is on, its two edges (CP_EDGE_*), or 0. A corner's own shape is the pointer's, grip
        outside the window and in inside it each way; a finger's reach is that shape grown to at least least each way
        (ui_reach_hit, UI_TARGET at the scale), about its middle, which is where the finger's whole hand lands on a corner. A
        window too small for two corners' targets apart gives the point to the corner nearest it.
*/
static unsigned int compositor_corner_reach(int px, int py, int x1, int y1, int x2, int y2, int grip, int in, int least)
{
        unsigned int found = 0;
        long long best = -1;

        for (unsigned int corner = 0; corner < 4; corner++)
        {
                int right = corner & 1, down = corner >> 1 & 1;
                int cx = right ? x2 : x1, cy = down ? y2 : y1;
                int x = right ? x2 - in : x1 - grip, y = down ? y2 - in : y1 - grip;
                long long dx = px - cx, dy = py - cy, far = dx * dx + dy * dy;

                if (!ui_reach_hit(1, least, px, py, x, y, grip + in, grip + in) || (best >= 0 && far >= best))
                        continue;
                best = far;
                found = (right ? CP_EDGE_RIGHT : CP_EDGE_LEFT) | (down ? CP_EDGE_BOTTOM : CP_EDGE_TOP);
        }
        return found;
}

// Whether a point is in the band at a window's edges that resizes it -- just inside its contents, its line,
// and past it -- and which edges.
static unsigned int compositor_edge_zone(int row, int x, int y)
{
        // A menu is not resized: its edges are no grip, for a pointer or a finger, and draw no indicator (none of
        // what is outside it is the menu's to take).
        if (compositor_popup_parent[row] >= 0)
                return 0;
        // A finger's are wider (COMPOSITOR_TOUCH_GRIP), its corners further along each edge, and its top a pointer's.
        int touch = compositor.touching;
        int grip = (touch ? COMPOSITOR_TOUCH_GRIP : COMPOSITOR_GRIP) * compositor.scale / 100 + compositor_line(row);
        int in = (touch ? COMPOSITOR_TOUCH_GRIP_IN : COMPOSITOR_GRIP_IN) * compositor.scale / 100;
        int x2 = compositor.x[row] + (int)compositor.w[row], y2 = compositor.y[row] + (int)compositor.h[row];
        unsigned int edges = compositor_edges_at(x, y, compositor.x[row], compositor.y[row], x2, y2, grip, in,
                                                 COMPOSITOR_GRIP_IN * compositor.scale / 100,
                                                 touch ? COMPOSITOR_TOUCH_CORNER * compositor.scale / 100 : 0);

        // A finger's corner is a target of UI_TARGET each way, and wins where it is: a corner is the window's own resize.
        if (touch)
        {
                unsigned int corner = compositor_corner_reach(x, y, compositor.x[row], compositor.y[row], x2, y2,
                                                              COMPOSITOR_GRIP * compositor.scale / 100 + compositor_line(row),
                                                              COMPOSITOR_GRIP_IN * compositor.scale / 100,
                                                              UI_TARGET * compositor.scale / 100);

                edges = corner ? corner : edges;
        }
        if (!edges)
                return 0;

        // A window that fills its screen or half of it has no edge on the screen's own: the pointer is held
        // there, and flung to the top to take the window it would start a resize instead.
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                unsigned int count;
                const int (*screens)[4] = compositor_screens(&count);
                const int *screen = screens[compositor_screen_of(row)];

                // The screen's own edges are no grip: the top where the window is at the top, the bottom where it is at the
                // bottom or fills a room (whose bottom is the taskbar's). A window's edge against another is a grip.
                edges &= compositor.y[row] <= screen[1] ? ~(unsigned int)COMPOSITOR_EDGE_TOP : ~0u;
                edges &= compositor.arranged[row] == COMPOSITOR_ROOM || y2 >= screen[1] + screen[3] ? ~(unsigned int)COMPOSITOR_EDGE_BOTTOM : ~0u;
                edges &= compositor.x[row] <= screen[0] ? ~(unsigned int)COMPOSITOR_EDGE_LEFT : ~0u;
                edges &= x2 >= screen[0] + screen[2] ? ~(unsigned int)COMPOSITOR_EDGE_RIGHT : ~0u;
        }
        return edges;
}

// What of the bands is at a point (taskbar_hit's answer), with the listing of the band it is on in compositor.listed,
// which is what a tab's number names.
static int compositor_band_hit(int x, int y)
{
        int hit = TASKBAR_HIT_NONE, bands = compositor_bands();
        int reach = compositor.touching ? UI_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; compositor.band_at < bands && hit == TASKBAR_HIT_NONE; compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();

                hit = taskbar_hit_reach(&view, x, y, reach);
        }
        // The listing of the band hit stays: the last listed.
        compositor.band_at = 0;
        return hit;
}

/*
        What is under a point: a tab, which tab is set to (TASKBAR_HIT_NONE
        when it is none), and otherwise the topmost window whose contents or
        grip, just outside its edges, hold it. Between the tabs is whatever is
        below them.
*/
static int compositor_band_hit(int x, int y);

static int compositor_under(int x, int y, int *tab)
{
        *tab = compositor_band_hit(x, y);
        if (*tab != TASKBAR_HIT_NONE)
                return COMPOSITOR_NONE;

        for (unsigned int at = compositor.stacked; at--;)
        {
                int row = compositor.stack[at];

                if (!compositor_visible(row))
                        continue;
                if ((x >= compositor.x[row] && x < compositor.x[row] + (int)compositor.w[row] && y >= compositor.y[row] &&
                     y < compositor.y[row] + (int)compositor.h[row]) ||
                    compositor_edge_zone(row, x, y))
                        return row;
        }

        return COMPOSITOR_NONE;
}

// A window where it is put, and the table told.
static void compositor_put(int row, int x, int y)
{
        compositor.x[row] = compositor.table->x[row] = x;
        compositor.y[row] = compositor.table->y[row] = y;
}

// Kept where a person can reach it (compositor_screen_keep_window): on one screen, all of it when it fits and its
// top left when it does not; across screens, its top on one of them.
static void compositor_place(int row, int x, int y)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        compositor_screen_keep_window(screens, count, &x, &y, (int)compositor.w[row], (int)compositor.h[row],
                                      compositor_line(row), COMPOSITOR_GRAB * compositor.scale / 100);
        compositor_put(row, x, y);
}

/*
        The size a window is asked to be, in the table, and a knock. It draws
        whatever size it will and says so with its next picture; the edges the
        hand does not hold stay where they were, whatever comes back.
*/
static void compositor_want(int row, int width, int height)
{
        int x = compositor.x[row], y = compositor.y[row];

        // The least first and the most last: at a scale whose least window is past the largest screen (96 pixels at 800
        // percent is 768), the least won and a window was made wider than its buffer's rows, which compositor_stretch
        // then filled into the rows after them -- the next window's pixels, and for the last row past the table.
        width = width < WINDOW_MIN_WIDTH * compositor.scale / 100 ? WINDOW_MIN_WIDTH * compositor.scale / 100 : width;
        width = width > compositor_most_width() ? compositor_most_width() : width;
        height = height < WINDOW_MIN_HEIGHT * compositor.scale / 100 ? WINDOW_MIN_HEIGHT * compositor.scale / 100 : height;
        height = height > compositor_most_height() ? compositor_most_height() : height;
        compositor.table->want_width[row] = (unsigned int)width;
        compositor.table->want_height[row] = (unsigned int)height;
        compositor.woken |= 1u << row;
        if (!compositor.showing[row])
                return;

        // Shown at it now, its picture stretched or cut to it, and the edges the hand does not hold where they were.
        compositor_damage_row(row);
        compositor.asking[row] = 1;
        compositor.w[row] = (unsigned int)width;
        compositor.h[row] = (unsigned int)height;
        if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                x = compositor.far_x[row] - width;
        if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                y = compositor.far_y[row] - height;
        compositor_place(row, x, y);
        compositor_stretch(row);
        compositor_damage_row(row);
}

/*
        A window whose size is more than the room of its screen is asked for the room (the band left out), as a window the
        hand stretched past the edge is.
*/
static void compositor_fit_row(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int room[4], on, width, height;

        if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_FLOATING)
                return;
        on = compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                     (int)compositor.h[row]);
        compositor_room(screens[on], room);
        width = (int)compositor.w[row] > room[2] ? room[2] : (int)compositor.w[row];
        height = (int)compositor.h[row] > room[3] ? room[3] : (int)compositor.h[row];
        if (width != (int)compositor.w[row] || height != (int)compositor.h[row])
                compositor_want(row, width, height);
}

/*
        A scale taken: a window that keeps its size in its own units is larger in pixels at a larger scale. One that is past
        the room of its screen now is asked to fit it; and every window that floats is asked to fit with its next picture of
        another size, which is the one it draws at the new scale (for the next three seconds, then it is what it draws).
        A window that fills a room was laid again with the rooms.
*/
static void compositor_scale_fit(void)
{
        compositor.fit = 0;
        for (int row = 0; row < WINDOWS; row++)
        {
                if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_FLOATING)
                        continue;
                compositor.fit |= 1u << row;
                compositor_fit_row(row);
        }
        compositor.fit_until = clock_monotonic_nanoseconds() + 3000000000ull;
}

/*
        A shared edge: windows that meet along one line, a divider (compositor_divider_of). A press anywhere along it, from
        one end of the line to the other (compositor_shared_at), takes all of them: compositor_shared_begin takes hold of
        each, and compositor_shared_move grows one side as the other shrinks, so they go on meeting. A corner of a window
        (an edge of it that is on two sides) keeps its own resize, as a single window's does. The indicator
        (compositor_mark_shared) is a tint across the whole line, COMPOSITOR_SHARED_MARK logical pixels either side of it, over
        the topmost of the windows; it is shown while the pointer is over the line (compositor_shared_hover) and while it is held.
*/

static void compositor_resize_begin(int row, unsigned int edges);
static void compositor_hold_take(int at, int row);
static unsigned int compositor_tile_cells(unsigned int how);

// The window on the far side of row's edge (one edge: a corner is the window's own) that meets it edge to edge with the
// same extent along it, or COMPOSITOR_NONE.
static int compositor_meets(int row, unsigned int edge)
{
        int x = compositor.x[row], y = compositor.y[row], w = (int)compositor.w[row], h = (int)compositor.h[row];
        int vertical = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_RIGHT;

        if (edge != COMPOSITOR_EDGE_LEFT && edge != COMPOSITOR_EDGE_RIGHT && edge != COMPOSITOR_EDGE_TOP &&
            edge != COMPOSITOR_EDGE_BOTTOM)
                return COMPOSITOR_NONE;
        for (int other = 0; other < WINDOWS; other++)
        {
                int ox = compositor.x[other], oy = compositor.y[other], ow = (int)compositor.w[other], oh = (int)compositor.h[other];

                // A menu is no window of the edge it lies against: nothing is shared with it.
                if (other == row || !compositor_visible(other) || compositor_popup_parent[other] >= 0)
                        continue;
                if (vertical ? oy != y || oh != h : ox != x || ow != w)
                        continue;
                if (edge == COMPOSITOR_EDGE_RIGHT ? ox == x + w : edge == COMPOSITOR_EDGE_LEFT ? ox + ow == x
                    : edge == COMPOSITOR_EDGE_BOTTOM ? oy == y + h : oy + oh == y)
                        return other;
        }
        return COMPOSITOR_NONE;
}

/*
        A divider: the line one window's edge lies on and every window whose edge lies on it too, reached along the line by
        overlapping -- the left half's right edge and both right quarters' left edges are one divider, which moves as one,
        so no gap or overlap opens between tiles and what a click finds follows them. lo is the windows on the left or top
        (their right or bottom on the line), hi those on the right or bottom, a bit each; lo_row and hi_row one of each, the
        window held and the one across that meets it where there is one. Windows that are no tile share a line only two at a
        time, edge to edge with the same extent (compositor_meets); tiles, every tile of the screen on the line.
*/
struct compositor_divider
{
        unsigned int lo, hi;
        int lo_row, hi_row, vertical, line, from, to; // the line, and the span of it the windows cover
};

// The line and the span of a divider's windows, from where they are now.
static void compositor_divider_measure(struct compositor_divider *d)
{
        int first = 1;

        for (int row = 0; row < WINDOWS; row++)
        {
                int a, b;

                if (!((d->lo | d->hi) >> row & 1))
                        continue;
                a = d->vertical ? compositor.y[row] : compositor.x[row];
                b = a + (int)(d->vertical ? compositor.h[row] : compositor.w[row]);
                if (first || a < d->from)
                        d->from = a;
                if (first || b > d->to)
                        d->to = b;
                if (d->hi >> row & 1)
                        d->line = d->vertical ? compositor.x[row] : compositor.y[row];
                first = 0;
        }
}

static int compositor_divider_of(int row, unsigned int edge, struct compositor_divider *d)
{
        int other = compositor_meets(row, edge), high = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_TOP;
        int vertical = edge == COMPOSITOR_EDGE_LEFT || edge == COMPOSITOR_EDGE_RIGHT, screen = compositor_screen_of(row), grew = 1;
        int line, from, to;

        if (edge != COMPOSITOR_EDGE_LEFT && edge != COMPOSITOR_EDGE_RIGHT && edge != COMPOSITOR_EDGE_TOP && edge != COMPOSITOR_EDGE_BOTTOM)
                return 0;
        line = vertical ? compositor.x[row] + (high ? 0 : (int)compositor.w[row]) : compositor.y[row] + (high ? 0 : (int)compositor.h[row]);
        from = vertical ? compositor.y[row] : compositor.x[row];
        to = from + (int)(vertical ? compositor.h[row] : compositor.w[row]);
        d->vertical = vertical;
        d->lo = high ? 0 : 1u << row, d->hi = high ? 1u << row : 0;
        d->lo_row = high ? other : row, d->hi_row = high ? row : other;
        while (grew && compositor_tile_cells(compositor.arranged[row]))
        {
                grew = 0;
                for (int each = 0; each < WINDOWS; each++)
                {
                        int near, far, a, b;

                        if (((d->lo | d->hi) >> each & 1) || !compositor_visible(each) || compositor_popup_parent[each] >= 0 ||
                            !compositor_tile_cells(compositor.arranged[each]) || compositor_screen_of(each) != screen)
                                continue;
                        near = vertical ? compositor.x[each] : compositor.y[each];
                        far = near + (int)(vertical ? compositor.w[each] : compositor.h[each]);
                        a = vertical ? compositor.y[each] : compositor.x[each];
                        b = a + (int)(vertical ? compositor.h[each] : compositor.w[each]);
                        if (a >= to || b <= from || (near != line && far != line))
                                continue;
                        if (far == line)
                                d->lo |= 1u << each, d->lo_row = d->lo_row >= 0 ? d->lo_row : each;
                        else
                                d->hi |= 1u << each, d->hi_row = d->hi_row >= 0 ? d->hi_row : each;
                        from = a < from ? a : from;
                        to = b > to ? b : to;
                        grew = 1;
                }
        }
        // No tile across it: a pair edge to edge with the same extent, whatever the two are, or nothing.
        if (!d->lo || !d->hi)
        {
                if (other == COMPOSITOR_NONE)
                        return 0;
                d->lo = 1u << (high ? other : row), d->hi = 1u << (high ? row : other);
        }
        compositor_divider_measure(d);
        return 1;
}

// The indicator's rectangle across a divider (vertical: one that runs down): the whole span its windows cover, from one
// end of the line to the other, and COMPOSITOR_SHARED_MARK logical pixels (at the scale of its windows' screen, a pixel at
// the least) either side of the line.
static void compositor_shared_strip(const struct compositor_divider *d, int *rect)
{
        int row = d->hi_row >= 0 ? d->hi_row : d->lo_row, scale = row >= 0 ? compositor_row_scale(row) : compositor.scale;
        int s = COMPOSITOR_SHARED_MARK * scale / 100, span = d->to - d->from;

        s = s < 1 ? 1 : s;
        if (d->vertical)
                rect[0] = d->line - s, rect[1] = d->from, rect[2] = 2 * s, rect[3] = span;
        else
                rect[0] = d->from, rect[1] = d->line - s, rect[2] = span, rect[3] = 2 * s;
}

// Whether row's edge is on a divider with the point along it (anywhere from one end to the other), and the divider.
static int compositor_shared_at(int row, unsigned int edge, int x, int y, struct compositor_divider *d)
{
        int rect[4];

        if (!edge || (edge & (edge - 1)) || !compositor_divider_of(row, edge, d))
                return 0;
        compositor_shared_strip(d, rect);
        return d->vertical ? y >= rect[1] && y < rect[1] + rect[3] : x >= rect[0] && x < rect[0] + rect[2];
}

// The indicator's rectangle over the window row, the window it meets being with (rect none for none), damaged where it
// was and where it is.
static void compositor_mark_set(int row, int with, const int *rect)
{
        if (row == compositor.mark_row && with == compositor.mark_with && !memory_compare(rect, compositor.mark, sizeof(compositor.mark)))
                return;
        if (compositor.mark[2] > 0)
                compositor_damage(compositor.mark[0], compositor.mark[1], compositor.mark[2], compositor.mark[3]);
        memory_copy(compositor.mark, rect, sizeof(compositor.mark));
        compositor.mark_row = row;
        compositor.mark_with = with;
        if (rect[2] > 0)
                compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

static void compositor_mark_clear(void)
{
        int none[4] = {0, 0, 0, 0};

        compositor_mark_set(COMPOSITOR_NONE, COMPOSITOR_NONE, none);
        compositor.mark_rows = 0;
}

// The indicator of a divider, over the topmost of its windows (so nothing above it covers it); the other is the one across.
static void compositor_mark_shared(const struct compositor_divider *d)
{
        int rect[4], top = d->hi_row, with = d->lo_row;

        compositor_shared_strip(d, rect);
        for (unsigned int at = 0; at < compositor.stacked; at++)
                if ((d->lo | d->hi) >> compositor.stack[at] & 1)
                        top = compositor.stack[at], with = d->lo >> top & 1 ? d->hi_row : d->lo_row;
        compositor_mark_set(top, with, rect);
        compositor.mark_rows = d->lo | d->hi;
}

// The indicator while no button is held: over a divider under the pointer, and none otherwise.
static void compositor_shared_hover(int under)
{
        unsigned int edge = under < 0 || compositor.grab >= 0 || compositor.touching ? 0
                            : compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y);
        struct compositor_divider d;

        if (compositor_shared_at(under, edge, compositor.pointer_x, compositor.pointer_y, &d))
                compositor_mark_shared(&d);
        else
                compositor_mark_clear();
}

// The divider being resized, from the windows it took at the press.
static void compositor_shared_divider(struct compositor_divider *d)
{
        d->lo = compositor.shared_lo_set, d->hi = compositor.shared_hi_set;
        d->lo_row = compositor.shared_lo, d->hi_row = compositor.shared_hi, d->vertical = compositor.shared_vertical;
        compositor_divider_measure(d);
}

/*
        A press along a divider takes all of its windows: row, pressed on its edge, is resized with the others,
        each holding its far edge (the low side's left or top, the high side's right or bottom) while the line moves. Each
        keeps what it fills -- a window that floats has an outline the screen keeps in view, which would take them off the
        line -- and the rectangle it had before. Answers 0 when there is no divider here, and the press is row's own.
*/
static int compositor_shared_begin(int row, unsigned int edge)
{
        struct compositor_divider d;
        unsigned int was = compositor.arranged[row], all;
        unsigned int lo_edge, hi_edge;

        if (!compositor_shared_at(row, edge, compositor.pointer_x, compositor.pointer_y, &d))
                return 0;
        all = d.lo | d.hi;
        lo_edge = d.vertical ? COMPOSITOR_EDGE_RIGHT : COMPOSITOR_EDGE_BOTTOM;
        hi_edge = d.vertical ? COMPOSITOR_EDGE_LEFT : COMPOSITOR_EDGE_TOP;
        compositor_resize_begin(row, edge);
        compositor.arranged[row] = was;
        compositor.shared_row = d.lo >> row & 1 ? d.hi_row : d.lo_row;
        compositor.shared_lo = d.lo_row, compositor.shared_hi = d.hi_row;
        compositor.shared_lo_set = d.lo, compositor.shared_hi_set = d.hi;
        compositor.shared_vertical = d.vertical;
        for (int each = 0; each < WINDOWS; each++)
        {
                if (!(all >> each & 1))
                        continue;
                compositor.shared_sizes[each] = d.vertical ? (int)compositor.w[each] : (int)compositor.h[each];
                if (each == row)
                        continue;
                compositor_hold_take(1, each);
                compositor_damage_row(each);
                compositor.edges[each] = d.lo >> each & 1 ? lo_edge : hi_edge;
                compositor.far_x[each] = compositor.x[each] + (int)compositor.w[each];
                compositor.far_y[each] = compositor.y[each] + (int)compositor.h[each];
                compositor_damage_row(each);
        }
        if (compositor.shared_lo >= 0 && compositor.shared_hi >= 0)
                compositor.shared_size[0] = compositor.shared_sizes[compositor.shared_lo],
                compositor.shared_size[1] = compositor.shared_sizes[compositor.shared_hi];
        compositor_mark_shared(&d);
        return 1;
}

/*
        The divider moved by the pointer's travel since the press, every window of it kept to the least and most a window
        is, so they go on meeting: the low side's far edges stay and the high side's near edges move with the line.
*/
static void compositor_shared_move(void)
{
        int vertical = compositor.shared_vertical, travel = vertical ? compositor.pointer_x - compositor.press_x
                                                                         : compositor.pointer_y - compositor.press_y;
        int least = (vertical ? WINDOW_MIN_WIDTH : WINDOW_MIN_HEIGHT) * compositor.scale / 100;
        int low = -2147483647, high = 2147483647;
        unsigned int all = compositor.shared_lo_set | compositor.shared_hi_set;
        struct compositor_divider d;

        for (int each = 0; each < WINDOWS; each++)
        {
                int size = compositor.shared_sizes[each];
                int most = (vertical ? compositor_most_width() : compositor_most_height()) - 2 * compositor_line(each);

                if (!(all >> each & 1))
                        continue;
                if (compositor.shared_lo_set >> each & 1)
                        low = least - size > low ? least - size : low, high = most - size < high ? most - size : high;
                else
                        low = size - most > low ? size - most : low, high = size - least < high ? size - least : high;
        }
        if (low > high)
                return;
        travel = travel < low ? low : travel > high ? high : travel;
        for (int each = 0; each < WINDOWS; each++)
        {
                int size;

                if (!(all >> each & 1))
                        continue;
                size = compositor.shared_sizes[each] + (compositor.shared_lo_set >> each & 1 ? travel : -travel);
                if (vertical)
                        compositor_want(each, size, (int)compositor.h[each]);
                else
                        compositor_want(each, (int)compositor.w[each], size);
        }
        compositor_shared_divider(&d);
        compositor_mark_shared(&d);
}

/*
        A resize takes hold of an edge (or a corner). A tile resized stays a tile: its screen's edges stay its own (no grip,
        no line kept off them, so nothing moves by a pixel when the hand takes it), and it keeps the rectangle it had before
        it was first arranged, which a drag away or a restore gives back, as a tile only moved between its neighbours does.
        A window filling its screen resized floats.
*/
static void compositor_resize_begin(int row, unsigned int edges)
{
        compositor_hold_take(0, row);
        compositor.resize = row;
        compositor.shared_row = COMPOSITOR_NONE;
        compositor_mark_clear();
        compositor.resize_w = (int)compositor.w[row];
        compositor.resize_h = (int)compositor.h[row];
        compositor.press_x = compositor.pointer_x;
        compositor.press_y = compositor.pointer_y;
        if (!compositor_tile_cells(compositor.arranged[row]))
                compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.edges[row] = edges;
        compositor.far_x[row] = compositor.x[row] + (int)compositor.w[row];
        compositor.far_y[row] = compositor.y[row] + (int)compositor.h[row];
}

static void compositor_resize_move(void)
{
        int row = compositor.resize;

        if (compositor.shared_row >= 0)
        {
                compositor_shared_move();
                return;
        }
        int dx = compositor.pointer_x - compositor.press_x;
        int dy = compositor.pointer_y - compositor.press_y;
        int w = compositor.resize_w, h = compositor.resize_h;
        int max_w = compositor_most_width() - 2 * compositor_line(row);
        int max_h = compositor_most_height() - 2 * compositor_line(row);

        if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                w -= dx;
        else if (compositor.edges[row] & COMPOSITOR_EDGE_RIGHT)
                w += dx;

        if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                h -= dy;
        else if (compositor.edges[row] & COMPOSITOR_EDGE_BOTTOM)
                h += dy;

        compositor_want(row, w > max_w ? max_w : w, h > max_h ? max_h : h);
}

/*
        Filling a rectangle as how says: all of a screen or half of it, or
        the room a placement fills (COMPOSITOR_ROOM). A window is asked for
        the size the rectangle gives it and put where the rectangle begins,
        and the rectangle it had is kept, from the time it left floating and
        not at every arrangement, so snapping left, then right, then dragging
        off puts it back where it was and not on the left half. The edge that
        is the screen's stays where it is whatever size comes back: the right
        half is held by its right.
*/
// The edges a window filling how holds whatever size its program comes back at: those that are not the screen's, so the
// screen's stay where they are (a window on the right is held by its right, one at the bottom by its bottom).
static unsigned int compositor_tile_edges(unsigned int how)
{
        return (how == COMPOSITOR_RIGHT || how == COMPOSITOR_TOP_RIGHT || how == COMPOSITOR_BOTTOM_RIGHT ? COMPOSITOR_EDGE_LEFT : 0) |
               (how == COMPOSITOR_BOTTOM || how == COMPOSITOR_BOTTOM_LEFT || how == COMPOSITOR_BOTTOM_RIGHT ? COMPOSITOR_EDGE_TOP : 0);
}

// A window set to rect, filling how, gliding there from where it was: what an arrangement and a put back share.
static void compositor_set_rect(int row, unsigned int how, const int *rect)
{
        int was[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

        // Its line goes with it: the outline it had is damaged before it is a filling one.
        compositor_damage_row(row);
        compositor.arranged[row] = how;
        compositor.edges[row] = compositor_tile_edges(how);
        compositor.far_x[row] = rect[0] + rect[2];
        compositor.far_y[row] = rect[1] + rect[3];
        compositor_put(row, rect[0], rect[1]);
        compositor_damage_row(row);
        compositor_want(row, rect[2], rect[3]);
        if (memory_compare(was, rect, sizeof(was)))
                compositor_glide_start(row, was);
}

static void compositor_arrange_rect(int row, unsigned int how, const int *rect)
{
        if (!compositor.showing[row])
                return;

        if (compositor.arranged[row] == COMPOSITOR_FLOATING)
        {
                compositor.saved_x[row] = compositor.x[row];
                compositor.saved_y[row] = compositor.y[row];
                compositor.saved_w[row] = compositor.w[row];
                compositor.saved_h[row] = compositor.h[row];
        }
        compositor_set_rect(row, how, rect);
}

/*
        Escape, or a right press, during a drag or a resize: what the hand took is put back as it was at the press -- its
        rectangle and what it filled, the remembered rectangle untouched -- gliding there from where it is, and the hand
        holds nothing. Its preview, the grid and its lit line go with it; the press is no half of a double click.
*/
// row taken hold of: the first of a hold (first, which forgets the last one's) or one more of it.
static void compositor_hold_take(int more, int row)
{
        int rect[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

        if (!more)
                compositor.hold_rows = 0;
        compositor.hold_rows |= 1u << row;
        memory_copy(compositor.hold_rect[row], rect, sizeof(rect));
        compositor.hold_how[row] = compositor.arranged[row];
}

static int compositor_hold_cancel(void)
{
        if (compositor.drag < 0 && compositor.resize < 0)
                return 0;
        compositor_preview(COMPOSITOR_NONE);
        compositor.drag = compositor.resize = compositor.shared_row = COMPOSITOR_NONE;
        compositor.press = COMPOSITOR_NONE;
        compositor_mark_clear();
        compositor_lit(COMPOSITOR_NONE, 0, 0);
        for (int row = 0; row < WINDOWS; row++)
                if ((compositor.hold_rows >> row & 1) && compositor.showing[row])
                        compositor_set_rect(row, compositor.hold_how[row], compositor.hold_rect[row]);
        compositor.hold_rows = 0;
        return 1;
}

static void compositor_float(int row);
static unsigned int compositor_region_cells(unsigned int how);

/*
        The cells a tile fills, one bit a quarter: top left 1, top right 2, bottom left 4, bottom right 8. A half is two of
        them. A window that is not a tile (floating, maximized, placed in its room) fills none.
*/
static unsigned int compositor_tile_cells(unsigned int how)
{
        switch (how)
        {
        case COMPOSITOR_LEFT: return 1u | 4u;
        case COMPOSITOR_RIGHT: return 2u | 8u;
        case COMPOSITOR_BOTTOM: return 4u | 8u;
        case COMPOSITOR_TOP_LEFT: case COMPOSITOR_TOP_RIGHT: case COMPOSITOR_BOTTOM_LEFT: case COMPOSITOR_BOTTOM_RIGHT:
                return 1u << (how - COMPOSITOR_TOP_LEFT);
        default: return 0;
        }
}

// The tile that fills exactly cells, or COMPOSITOR_FLOATING when no tile does.
static unsigned int compositor_cells_tile(unsigned int cells)
{
        static const unsigned int tiles[] = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_BOTTOM, COMPOSITOR_TOP_LEFT,
                                             COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};

        for (unsigned int at = 0; at < array_count(tiles); at++)
                if (compositor_tile_cells(tiles[at]) == cells)
                        return tiles[at];
        return COMPOSITOR_FLOATING;
}

/*
        A tile that row fills on screen leaves the tiles of the other windows on that screen the cells it does not fill.
        Each one it covers is cut to what is left, which is a tile again: a half that loses its top quarter is its bottom
        quarter, and a quarter that loses its half is the other quarter of it. One left nothing floats back to its own
        rectangle (compositor_float). A cut under a window's least is asked for the least at the cut's corner
        (compositor_want), so the rest of the cut is left free for the snap assist. A window that is not a tile is not
        touched, and neither is one on another screen.
*/
static void compositor_tile_others(int row, unsigned int how, int screen)
{
        unsigned int count, taken = compositor_region_cells(how);
        const int (*screens)[4] = compositor_screens(&count);

        for (int other = 0; other < WINDOWS; other++)
        {
                unsigned int was = compositor_tile_cells(compositor.arranged[other]), left;
                int part[4];

                if (other == row || !was || !compositor.showing[other] || compositor_screen_of(other) != screen)
                        continue;
                left = was & ~taken;
                if (left == was)
                        continue;
                if (!left)
                        compositor_float(other);
                else
                {
                        unsigned int rest = compositor_cells_tile(left);

                        compositor_screen_part(screens[screen], rest, part);
                        compositor_arrange_rect(other, rest, part);
                }
        }
}

/*
        The snap assist and the top edge's grid. A drag over a snap target shows the windows floating on its screen as
        thumbnails in the space the target leaves (compositor_assist_refresh). A drop there keeps them up, armed, until a
        click on one of them places that window in the tile it is over (compositor_assist_at), or a click elsewhere, or Escape,
        takes them away. A drag at the top edge of a screen shows the grid of regions along it (compositor_grid_at): a drop on a
        region takes it, and a region a window already fills is shown taken and is no target (compositor_target).
*/

// The grid's regions, in the order of its tiles, left to right.
static const unsigned int compositor_grid_regions[COMPOSITOR_GRID_TILES] = {COMPOSITOR_MAXIMIZED, COMPOSITOR_LEFT, COMPOSITOR_RIGHT,
                                                                            COMPOSITOR_TOP_LEFT, COMPOSITOR_TOP_RIGHT,
                                                                            COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};

// The cells a region fills: a tile's, or all four for the whole screen.
static unsigned int compositor_region_cells(unsigned int how)
{
        return how == COMPOSITOR_MAXIMIZED ? COMPOSITOR_CELLS : compositor_tile_cells(how);
}

// The cells the tiles of the other windows on a screen fill, row's own left out.
static unsigned int compositor_occupied(int row, int screen)
{
        unsigned int cells = 0;

        for (int other = 0; other < WINDOWS; other++)
                if (other != row && compositor.showing[other] && compositor_screen_of(other) == screen)
                        cells |= compositor_tile_cells(compositor.arranged[other]);
        return cells;
}

/*
        The grid's tiles on a screen: seven mini screens in a row along its top edge, each a tenth of the screen's width and
        as tall as the screen, with a gap between them, the row centred. Tile at is the region compositor_grid_regions gives it.
*/
static void compositor_grid_tile(const int *screen, unsigned int at, int *tile)
{
        int gap = COMPOSITOR_GRID_GAP * compositor.scale / 100, width = screen[2] / 10, row = COMPOSITOR_GRID_TILES * width + (COMPOSITOR_GRID_TILES - 1) * gap;

        tile[0] = screen[0] + (screen[2] - row) / 2 + (int)at * (width + gap);
        tile[1] = screen[1];
        tile[2] = width;
        tile[3] = width > 0 ? screen[3] * width / screen[2] : 0;
}

// Whether a pointer at x, y on screen is in the top band that brings the grid up.
static int compositor_grid_band(const int *screen, int x, int y)
{
        return x >= screen[0] && x < screen[0] + screen[2] && y >= screen[1] && y - screen[1] < COMPOSITOR_SNAP * compositor.scale / 100;
}

// Whether a pointer at x, y on screen is in the row of the grid's tiles, which stays up once it is (compositor_overlays_drag).
static int compositor_grid_panel(const int *screen, int x, int y)
{
        int first[4], last[4];

        compositor_grid_tile(screen, 0, first);
        compositor_grid_tile(screen, COMPOSITOR_GRID_TILES - 1, last);
        return x >= first[0] && x < last[0] + last[2] && y >= screen[1] && y - screen[1] < first[3];
}

// The tile of the grid under x, y on screen, or COMPOSITOR_GRID_TILES for none.
static unsigned int compositor_grid_under(const int *screen, int x, int y)
{
        int tile[4];

        for (unsigned int at = 0; at < COMPOSITOR_GRID_TILES; at++)
        {
                compositor_grid_tile(screen, at, tile);
                if (x >= tile[0] && x < tile[0] + tile[2] && y >= tile[1] && y < tile[1] + tile[3])
                        return at;
        }
        return COMPOSITOR_GRID_TILES;
}

// Where a drop of the window row at x, y arranges it: the grid's region under the pointer, which is no target when a window
// already fills it, or else the snap target of the window's rectangle box (compositor_snap). touch is the edges it touches.
static unsigned int compositor_target(int row, int x, int y, const int *box, int *screen, unsigned int *touch)
{
        unsigned int count, tile, snap = compositor_snap(box, touch, screen);
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_at(screens, count, x, y);

        if (at >= 0 && compositor.grid_on && compositor.grid_screen == at && compositor_grid_panel(screens[at], x, y))
        {
                tile = compositor_grid_under(screens[at], x, y);
                if (tile < COMPOSITOR_GRID_TILES)
                {
                        unsigned int how = compositor_grid_regions[tile];

                        *screen = at;
                        return compositor_region_cells(how) & compositor_occupied(row, at) ? COMPOSITOR_FLOATING : how;
                }
        }
        return snap;
}

// The tiles a set of free cells is cut into, the larger first: a half where it holds one, then quarters.
static unsigned int compositor_zones(unsigned int free, unsigned int *zones)
{
        static const unsigned int order[] = {COMPOSITOR_LEFT, COMPOSITOR_RIGHT, COMPOSITOR_BOTTOM, COMPOSITOR_TOP_LEFT,
                                             COMPOSITOR_TOP_RIGHT, COMPOSITOR_BOTTOM_LEFT, COMPOSITOR_BOTTOM_RIGHT};
        unsigned int count = 0;

        for (unsigned int at = 0; at < array_count(order); at++)
        {
                unsigned int cells = compositor_tile_cells(order[at]);

                if ((free & cells) == cells)
                {
                        zones[count++] = order[at];
                        free &= ~cells;
                }
        }
        return count;
}

// The rectangle of a thumbnail of a w by h window, at place of of in zone: a grid of the cells that many need, each the
// window's picture as large as its cell takes, centred in it.
static void compositor_assist_place(const int *zone, unsigned int at, unsigned int of, int w, int h, int *rect)
{
        int margin = COMPOSITOR_GRID_GAP * compositor.scale / 100, cols = 1, rows, cell_w, cell_h, fit_w, fit_h;

        while (cols * cols < (int)of)
                cols++;
        rows = ((int)of + cols - 1) / cols;
        cell_w = (zone[2] - 2 * margin) / cols;
        cell_h = (zone[3] - 2 * margin) / rows;
        if (cell_w < 1)
                cell_w = 1;
        if (cell_h < 1)
                cell_h = 1;
        if ((long)w * cell_h > (long)h * cell_w)
                fit_w = cell_w, fit_h = (int)((long)h * cell_w / w);
        else
                fit_h = cell_h, fit_w = (int)((long)w * cell_h / h);
        rect[0] = zone[0] + margin + (int)(at % (unsigned int)cols) * cell_w + (cell_w - fit_w) / 2;
        rect[1] = zone[1] + margin + (int)(at / (unsigned int)cols) * cell_h + (cell_h - fit_h) / 2;
        rect[2] = fit_w;
        rect[3] = fit_h;
}

// The snap assist's thumbnails, for the window assist_row arranged as assist_how on assist_screen: the visible windows
// floating there, most recent first, each in the tile the space the region leaves is cut into, round-robin over the tiles.
static void compositor_assist_refresh(void)
{
        unsigned int count, zones[4], zones_count, free, candidates[COMPOSITOR_ASSIST_MAX], taken[4] = {0, 0, 0, 0};
        unsigned int found = 0;
        const int (*screens)[4] = compositor_screens(&count);

        compositor.assist_count = 0;
        if (!compositor.assist_on || compositor.assist_screen < 0 || (unsigned int)compositor.assist_screen >= count)
                return;
        free = COMPOSITOR_CELLS & ~(compositor_region_cells(compositor.assist_how) |
                                    compositor_occupied(compositor.assist_row, compositor.assist_screen));
        zones_count = compositor_zones(free, zones);
        if (!zones_count)
                return;
        for (int at = compositor.stacked - 1; at >= 0 && found < COMPOSITOR_ASSIST_MAX; at--)
        {
                int row = compositor.stack[at];

                if (row == compositor.assist_row || !compositor_visible(row) || compositor.arranged[row] != COMPOSITOR_FLOATING ||
                    compositor_screen_of(row) != compositor.assist_screen || !compositor.w[row] || !compositor.h[row])
                        continue;
                candidates[found++] = (unsigned int)row;
        }
        for (unsigned int at = 0; at < found; at++)
                taken[at % zones_count]++;
        for (unsigned int at = 0; at < found; at++)
        {
                unsigned int zone = at % zones_count, place = at / zones_count;
                int part[4];
                int row = (int)candidates[at];

                compositor_screen_part(screens[compositor.assist_screen], zones[zone], part);
                compositor_assist_place(part, place, taken[zone], (int)compositor.w[row], (int)compositor.h[row],
                                        compositor.assist_rect[at]);
                compositor.assist_rows[at] = candidates[at];
                compositor.assist_zone[at] = zones[zone];
        }
        compositor.assist_count = (int)found;
}

// The thumbnail under x, y, or -1 for none.
static int compositor_assist_at(int x, int y)
{
        for (int at = 0; at < compositor.assist_count; at++)
        {
                const int *rect = compositor.assist_rect[at];

                if (x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3])
                        return at;
        }
        return -1;
}

/*
        The overlays' fade: the percent they are drawn at now, from overlay_from toward overlay_goal over COMPOSITOR_SNAP_FADE_NS
        on the kit's ease (reduced motion: the goal at once). They are shown while they are up or still fading out.
*/
static unsigned int compositor_overlay_alpha(void)
{
        int from = (int)compositor.overlay_from, goal = (int)compositor.overlay_goal;
        b64 done;

        if (!compositor.overlay_at || !compositor_glides())
                return compositor.overlay_goal;
        done = ui_bezier_at(&ui_ease, ui_progress((b64)(clock_monotonic_nanoseconds() - compositor.overlay_at), COMPOSITOR_SNAP_FADE_NS));
        return (unsigned int)(from + (int)((long long)(goal - from) * (long long)done / (long long)UI_ONE));
}

static void compositor_overlay_fade(unsigned int goal)
{
        compositor.overlay_from = compositor_overlay_alpha();
        compositor.overlay_goal = goal;
        compositor.overlay_at = compositor_glides() ? clock_monotonic_nanoseconds() : 0;
}

// Whether the overlays are drawn: up now, or still fading out. Up is read live (compositor_overlays_up), not as the last
// present saw it: a drag brings the snap assist up and damages its thumbnails before the next present, and those are
// the rectangles it must repaint.
static int compositor_overlays_shown(void)
{
        return compositor_overlays_up() || compositor_overlay_alpha() > 0;
}

/*
        After a present: overlays still fading are damaged again, so the next present draws them at their next percent. One
        that has faded out is gone, its thumbnails with it, and the loop goes idle.
*/
static void compositor_overlays_keep(void)
{
        if (!compositor.overlay_at)
                return;
        if (clock_monotonic_nanoseconds() - compositor.overlay_at < COMPOSITOR_SNAP_FADE_NS)
        {
                compositor_overlays_damage();
                return;
        }
        // Faded: where they were is drawn once more, or the faint last frame of the fade stays on the screen.
        compositor_overlays_damage_all();
        if (!compositor.overlays_up)
                compositor.assist_count = 0;
        compositor.overlay_at = 0;
}

// The snap's edge colour at a percent of itself over the wallpaper: a thumbnail's and a grid tile's line, which fades.
static unsigned int compositor_snap_edge(unsigned int percent)
{
        unsigned int ink = compositor_snap_ink() | 0xff000000u, ground = compositor_ground | 0xff000000u;

        return ui_mix(ink, percent > 100 ? 100 : percent, ground) & 0xffffffu;
}

// The percent the overlays of a screen are drawn at: the screen's own, or the desktop's where it has none.
static int compositor_overlay_scale(int screen)
{
        int scale = compositor_output_scale(screen);

        return scale > 0 ? scale : compositor.scale;
}

// The rectangles the overlays are drawn in now: each thumbnail's, and the grid's row while it is up or fading out (or, with
// all, wherever they were last, shown or not).
static int compositor_overlay_boxes_of(int boxes[COMPOSITOR_ASSIST_MAX + 1][4], int all)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int n = 0, first[4], last[4], shown = all || compositor_overlays_shown();

        for (int thumb = 0; shown && thumb < compositor.assist_count; thumb++)
        {
                const int *rect = compositor.assist_rect[thumb];
                int line = compositor_line_px(compositor_overlay_scale(compositor.assist_screen));

                boxes[n][0] = rect[0] - line, boxes[n][1] = rect[1] - line;
                boxes[n][2] = rect[2] + 2 * line, boxes[n][3] = rect[3] + 2 * line;
                n++;
        }
        if (shown && compositor.grid_on && compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
                const int *screen = screens[compositor.grid_screen];
                int line = compositor_line_px(compositor_overlay_scale(compositor.grid_screen));

                compositor_grid_tile(screen, 0, first);
                compositor_grid_tile(screen, COMPOSITOR_GRID_TILES - 1, last);
                boxes[n][0] = first[0] - line, boxes[n][1] = screen[1] - line;
                boxes[n][2] = last[0] + last[2] - first[0] + 2 * line;
                boxes[n][3] = first[3] + 2 * line;
                n++;
        }
        return n;
}

static void compositor_overlays_damage_of(int all)
{
        int boxes[COMPOSITOR_ASSIST_MAX + 1][4];
        int n = compositor_overlay_boxes_of(boxes, all);

        for (int at = 0; at < n; at++)
                compositor_damage(boxes[at][0], boxes[at][1], boxes[at][2], boxes[at][3]);
}

static void compositor_overlays_damage(void)
{
        compositor_overlays_damage_of(0);
}

// Where the overlays were last, shown or not: what a fade that has ended draws once more.
static void compositor_overlays_damage_all(void)
{
        compositor_overlays_damage_of(1);
}

// Whether the snap assist or the grid is up: what the frames of a present add (compositor_overlays).
static int compositor_overlays_up(void)
{
        return compositor.assist_on || compositor.grid_on;
}

/*
        The overlays of a present, from at in the frame list, all at the overlays' fade. Each thumbnail: its window's picture
        scaled into its rectangle, with the window's line and 8 px corners in the snap's colour, and a veil of the wallpaper
        over the pair that fades the picture in or out (a tint, drawn over what is composed below it). Then the grid: each
        tile a tint with its line and corners drawn over it (a tint frame, then a frame that is only its border), and each
        region in it tinted, the taken ones dark. Answers the frames added.
*/
static unsigned int compositor_overlays(struct compositor_frame *frames, unsigned int at)
{
        unsigned int count, added = 0, percent = compositor_overlay_alpha(), grid_ink, taken, ground;
        const int (*screens)[4] = compositor_screens(&count);
        int shown = compositor_overlays_shown();

        ground = compositor_ground;
        for (int thumb = 0; shown && thumb < compositor.assist_count; thumb++)
        {
                const int *rect = compositor.assist_rect[thumb];
                int row = (int)compositor.assist_rows[thumb], scale = compositor_overlay_scale(compositor.assist_screen);
                int line = compositor_line_px(scale);
                struct compositor_frame *picture = &frames[at + added], *veil = &frames[at + added + 1];

                compositor_frame_of(row, picture);
                picture->x = rect[0], picture->y = rect[1];
                picture->width = rect[2], picture->height = rect[3];
                picture->source_w = (int)compositor.w[row], picture->source_h = (int)compositor.h[row];
                picture->radius = UI_RADIUS * scale / 100, picture->line = line;
                picture->edge = compositor_snap_edge(percent);
                picture->close = 0;
                picture->lit = 0;
                memory_zero(picture->preview, sizeof(picture->preview));
                memory_zero(picture->mark, sizeof(picture->mark));
                memory_zero(veil, sizeof(*veil));
                veil->scale = compositor.scale;
                if (percent < 100)
                {
                        veil->mark[0] = rect[0] - line, veil->mark[1] = rect[1] - line;
                        veil->mark[2] = rect[2] + 2 * line, veil->mark[3] = rect[3] + 2 * line;
                        veil->mark_ink = (ground & 0xffffffu) | (100 - percent) * 255 / 100 << 24;
                }
                added += 2;
        }
        if (shown && compositor.grid_on && compositor.grid_screen >= 0 && (unsigned int)compositor.grid_screen < count)
        {
                const int *screen = screens[compositor.grid_screen];
                int scale = compositor_overlay_scale(compositor.grid_screen);
                unsigned int line = (unsigned int)compositor_line_px(scale);

                for (unsigned int tile = 0; tile < COMPOSITOR_GRID_TILES; tile++)
                {
                        int box[4];
                        struct compositor_frame *tint = &frames[at + added], *border = &frames[at + added + 1];
                        unsigned int ink = tile == compositor.grid_hover ? compositor_snap_ink() : COMPOSITOR_GRID_TINT;

                        compositor_grid_tile(screen, tile, box);
                        memory_zero(tint, sizeof(*tint));
                        tint->scale = compositor.scale;
                        memory_copy(tint->mark, box, sizeof(box));
                        tint->mark_ink = (ink & 0xffffffu) | ((ink >> 24) * percent / 100) << 24;
                        memory_zero(border, sizeof(*border));
                        border->scale = compositor.scale;
                        border->x = box[0], border->y = box[1];
                        border->width = box[2], border->height = box[3];
                        border->radius = UI_RADIUS * scale / 100, border->line = (int)line;
                        border->edge = compositor_snap_edge(percent);
                        added += 2;
                }
                for (unsigned int tile = 0; tile < COMPOSITOR_GRID_TILES; tile++)
                {
                        int part[4], tile_box[4], box[4];
                        unsigned int how = compositor_grid_regions[tile];

                        compositor_grid_tile(screen, tile, tile_box);
                        compositor_screen_part(screen, how, part);
                        box[0] = tile_box[0] + (part[0] - screen[0]) * tile_box[2] / screen[2];
                        box[1] = tile_box[1] + (part[1] - screen[1]) * tile_box[3] / screen[3];
                        box[2] = part[2] * tile_box[2] / screen[2];
                        box[3] = part[3] * tile_box[3] / screen[3];
                        taken = (compositor_region_cells(how) & compositor_occupied(compositor.drag, compositor.grid_screen)) != 0;
                        grid_ink = taken ? COMPOSITOR_GRID_TAKEN : tile == compositor.grid_hover ? compositor_snap_ink() : COMPOSITOR_GRID_TINT;
                        memory_zero(&frames[at + added], sizeof(frames[0]));
                        frames[at + added].scale = compositor.scale;
                        memory_copy(frames[at + added].preview, box, sizeof(box));
                        frames[at + added].preview_ink = (grid_ink & 0xffffffu) | ((grid_ink >> 24) * percent / 100) << 24;
                        added++;
                }
        }
        return added;
}

/*
        Whether a drag's pointer at x, y is resting in the top band of screen at where that band may bring the grid: the
        screen's top there is free (no screen above it: a drag on its way up to another screen crosses the band and is
        not caught), and Control, which is no snap, is not held.
*/
static int compositor_grid_rests(const int (*screens)[4], unsigned int count, int at, int x, int y)
{
        int point[4] = {x, y, 1, 1};

        return at >= 0 && compositor_grid_band(screens[at], x, y) && !(compositor_modifiers() & WINDOW_KEY_CONTROL) &&
               (compositor_snap_free(screens, count, point, at) & COMPOSITOR_SNAP_TOP);
}

/*
        The grid as a drag's pointer is at x, y. It comes up only once the pointer has rested in a free top band for
        COMPOSITOR_GRID_DWELL_NS (grid_since is when it came to it; the loop wakes for the rest, compositor_snap_due), so a
        window thrown up to the top is filled at once and the grid is for one held there. Up, it stays while the pointer is
        in its row of tiles, so a drop there can take a region. The snap assist is not shown during a drag, only the
        preview: the thumbnails are offered after the drop (compositor_release_as).
*/
static void compositor_overlays_drag(int row, int x, int y, unsigned int how, int screen)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_at(screens, count, x, y), rests = compositor_grid_rests(screens, count, at, x, y);
        positive now = clock_monotonic_nanoseconds();

        (void)row, (void)how, (void)screen;
        compositor_overlays_damage();
        if (!rests)
                compositor.grid_since = 0;
        else if (!compositor.grid_since || compositor.grid_since_screen != at)
                compositor.grid_since = now ? now : 1, compositor.grid_since_screen = at;

        if (at >= 0 && ((rests && now - compositor.grid_since >= COMPOSITOR_GRID_DWELL_NS) ||
                        (compositor.grid_on && compositor.grid_screen == at && compositor_grid_panel(screens[at], x, y) &&
                         !(compositor_modifiers() & WINDOW_KEY_CONTROL))))
        {
                compositor.grid_on = 1;
                compositor.grid_screen = at;
                compositor.grid_hover = compositor_grid_under(screens[at], x, y);
        }
        else
                compositor.grid_on = 0;
        compositor_overlays_damage();
}

// When the grid is due to come up under a pointer resting in the top band (compositor_overlays_drag), or 0 for never.
static positive compositor_snap_due(void)
{
        return compositor.drag >= 0 && compositor.grid_since && !compositor.grid_on ? compositor.grid_since + COMPOSITOR_GRID_DWELL_NS : 0;
}

// The loop woke for the grid: the drag's overlays and preview are read again where the pointer rests.
static void compositor_snap_tick(positive now)
{
        if (compositor_snap_due() && now >= compositor_snap_due())
                compositor_preview(compositor.drag);
}

// The snap assist taken away: a click elsewhere, Escape, or a window placed from it.
static void compositor_assist_close(void)
{
        compositor_overlays_damage();
        compositor.assist_on = compositor.assist_armed = 0;
        // The thumbnails fade out with the overlays (compositor_overlays_keep), unless the grid stays up: it has none of them.
        if (compositor.grid_on)
                compositor.assist_count = 0;
}

// All of a screen or half of it (compositor_screen_part): the screen named, or with none (-1) the window's own
// (compositor_screen_of). A tile leaves the others on the screen their share (compositor_tile_others).
static void compositor_arrange_on(int row, unsigned int how, int on)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int part[4], screen;

        if (!compositor.showing[row])
                return;

        screen = on >= 0 && (unsigned int)on < count ? on : compositor_screen_of(row);
        compositor_screen_part(screens[screen], how, part);
        compositor_arrange_rect(row, how, part);
        // A maximized window covers every tile on its screen, so they float back as a half that a half covers does.
        if (compositor_region_cells(how))
                compositor_tile_others(row, how, screen);
}

static void compositor_arrange(int row, unsigned int how)
{
        compositor_arrange_on(row, how, -1);
}

// Back to the rectangle it had, wherever it was put since.
static void compositor_float(int row)
{
        int was[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

        if (compositor.arranged[row] == COMPOSITOR_FLOATING)
                return;

        compositor_damage_row(row);
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.edges[row] = 0;
        compositor_put(row, compositor.saved_x[row], compositor.saved_y[row]);
        compositor_damage_row(row);
        compositor_want(row, (int)compositor.saved_w[row], (int)compositor.saved_h[row]);
        compositor_glide_start(row, was);
}

/*
        Taking an arranged window takes the window it was: back to the size it had, under the hand where the hand had it.
        Where the press took it (press_x, press_y) keeps its share of the width, so taking the right side does not make the
        window jump until its left is under the pointer; taken by its top, it keeps its distance from the top (the top is
        the same height whatever the window's size), and taken lower down (Alt) its share of the height. The pointer is
        always on the window it takes.
*/
static void compositor_float_for_drag(int row)
{
        int least_w = WINDOW_MIN_WIDTH * compositor.scale / 100, least_h = WINDOW_MIN_HEIGHT * compositor.scale / 100;
        int width = (int)compositor.saved_w[row] < least_w ? least_w : (int)compositor.saved_w[row];
        int height = (int)compositor.saved_h[row] < least_h ? least_h : (int)compositor.saved_h[row];
        int in_x = compositor.press_x - compositor.x[row], in_y = compositor.press_y - compositor.y[row];
        int grab = (int)((long)in_x * width / (compositor.w[row] ? (int)compositor.w[row] : 1));
        int at = in_y < COMPOSITOR_GRAB * compositor.scale / 100
                     ? in_y
                     : (int)((long)in_y * height / (compositor.h[row] ? (int)compositor.h[row] : 1));

        grab = compositor_fit(grab, 0, width - 1);
        at = compositor_fit(at, 0, height - 1);

        /*
                It shrinks to its own size about the hand: it glides from the tile as it is drawn, moved by as much as the hand
                has moved since the press (so the point pressed is under the pointer), to its own size, and the glide follows
                the hand (compositor_glide_origin). The point pressed keeps its share of both, so it stays under the pointer
                all the way. Reduced motion is its size at once.
        */
        struct compositor_frame drawn;

        compositor_frame_of(row, &drawn);
        if (compositor.glide[row])
                compositor_glide_damage_now(row);
        compositor.glide[row] = 0;
        compositor_damage_row(row);
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.edges[row] = 0;
        compositor_put(row, compositor.pointer_x - grab, compositor.pointer_y - at);
        compositor_damage_row(row);
        compositor_want(row, width, height);
        // The hand holds it by the grab, wherever the screen's edge keeps the window: read from the place the window was kept
        // at, a tile torn off by the edge of the screen would slide under the pointer, and its edge would never reach the
        // screen's again to snap.
        compositor.drag_x = grab;
        compositor.drag_y = at;
        if (compositor_glides())
        {
                int from[4] = {drawn.x + compositor.pointer_x - compositor.press_x, drawn.y + compositor.pointer_y - compositor.press_y,
                               drawn.width, drawn.height};

                compositor_glide_begin(row, from);
                compositor.drag_glide = row + 1;
        }
}

/*
        Where a window goes the first time it shows: where it asked to be,
        and a window asking for the place another holds is put down and
        across, twenty pixels a step, until it is on none that it can be
        seen from. Bounded: eight steps down is most of the way across a
        small screen, and past that the honest answer is to stop and let
        them overlap; a step that would put it off the bottom or the right
        stops the search too.
*/
static void compositor_first_place(int row, int *x, int *y)
{
        int step = COMPOSITOR_STEP * compositor.scale / 100;
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int on = compositor_screen_at(screens, count, *x, *y);
        // The screen it asked to be on, or the primary: the search stops at that screen's bottom and right.
        int right = screens[on < 0 ? 0 : on][0] + screens[on < 0 ? 0 : on][2];
        int bottom = screens[on < 0 ? 0 : on][1] + screens[on < 0 ? 0 : on][3];

        for (int at = 0; at < 8; at++)
        {
                int taken = 0, tx = *x + at * step, ty = *y + at * step;

                if (tx + (int)compositor.w[row] > right || ty + (int)compositor.h[row] > bottom)
                        break;

                for (int other = 0; other < WINDOWS; other++)
                        taken |= other != row && compositor.showing[other] && compositor.x[other] == tx &&
                                 compositor.y[other] == ty;

                if (!taken)
                {
                        *x += at * step;
                        *y += at * step;
                        return;
                }
        }
}

/*
        Placement: where a window that asked to be put somewhere goes (window.c's WINDOW_PLACE_*), worked out from
        rectangles alone -- x, y, width and height on the desktop -- so the checks can ask it everything. It runs
        when a window first shows and when the taskbar moves, never for a frame.
*/

// The room of a screen: the screen less the taskbar's band where the band lies along the edge position names.
// A band that is not on the screen, or that would leave nothing, takes nothing.
static void place_room(const int *screen, const int *band, unsigned int position, int *room)
{
        int cut = 0, size;

        memory_copy(room, screen, 4 * sizeof(int));
        if (band[2] <= 0 || band[3] <= 0 || band[0] >= screen[0] + screen[2] || band[0] + band[2] <= screen[0] ||
            band[1] >= screen[1] + screen[3] || band[1] + band[3] <= screen[1])
                return;
        if (position == TASKBAR_TOP || position == TASKBAR_BOTTOM)
        {
                cut = position == TASKBAR_TOP ? band[1] + band[3] - screen[1] : screen[1] + screen[3] - band[1];
                size = screen[3] - cut;
                if (size <= 0)
                        return;
                room[1] += position == TASKBAR_TOP ? cut : 0;
                room[3] = size;
                return;
        }
        cut = position == TASKBAR_LEFT ? band[0] + band[2] - screen[0] : screen[0] + screen[2] - band[0];
        size = screen[2] - cut;
        if (size <= 0)
                return;
        room[0] += position == TASKBAR_LEFT ? cut : 0;
        room[2] = size;
}

// Kept on a screen as compositor_place keeps it on the output: all of it, its line too, when it fits; its top left
// when it does not, so the top it is taken by is always there to take.
static void place_clamp(const int *screen, int width, int height, int line, int *x, int *y)
{
        int high_x = screen[0] + screen[2] - width - line, high_y = screen[1] + screen[3] - height - line;

        *x = *x > high_x ? high_x : *x;
        *y = *y > high_y ? high_y : *y;
        *x = *x < screen[0] + line ? screen[0] + line : *x;
        *y = *y < screen[1] + line ? screen[1] + line : *y;
}

// A size as a window may have it in an area: what was asked, up to the area's and never under the least.
static int place_size(unsigned int asked, int area, int least)
{
        int size = asked > (unsigned int)area ? area : (int)asked;

        return size < least ? (least < area ? least : area) : size;
}

/*
        Where a placement puts a window on its screen, whose room is room: what the window fills (COMPOSITOR_FLOATING,
        COMPOSITOR_ROOM, COMPOSITOR_MAXIMIZED for the whole screen), and the rectangle. A floating one is the size
        asked for, or the picture's, up to the room's when it is centred in it and the screen's when it is put at
        a place, and kept on the screen. x and y are where the program asked, which a placement with no mode
        keeps. line is the outline a floating window has round it, least the size no window is made smaller than.
*/
static unsigned int place_rect(const struct window_place *place, const int *screen, const int *room, int x, int y,
                               unsigned int picture_w, unsigned int picture_h, int line, int least_w, int least_h,
                               int *rect)
{
        unsigned int how = place->how & WINDOW_PLACE_HOW;
        unsigned int asked_w = place->how & WINDOW_PLACE_SIZE ? place->width : picture_w;
        unsigned int asked_h = place->how & WINDOW_PLACE_SIZE ? place->height : picture_h;
        const int *area = how == WINDOW_PLACE_CENTER ? room : screen;

        if (how == WINDOW_PLACE_MAXIMIZED || how == WINDOW_PLACE_FULLSCREEN)
        {
                memory_copy(rect, how == WINDOW_PLACE_MAXIMIZED ? room : screen, 4 * sizeof(int));
                return how == WINDOW_PLACE_MAXIMIZED ? COMPOSITOR_ROOM : COMPOSITOR_MAXIMIZED;
        }
        rect[2] = place_size(asked_w, area[2] - 2 * line, least_w);
        rect[3] = place_size(asked_h, area[3] - 2 * line, least_h);
        rect[0] = how == WINDOW_PLACE_CENTER ? room[0] + (room[2] - rect[2]) / 2 : how == WINDOW_PLACE_AT ? screen[0] + place->x : x;
        rect[1] = how == WINDOW_PLACE_CENTER ? room[1] + (room[3] - rect[3]) / 2 : how == WINDOW_PLACE_AT ? screen[1] + place->y : y;
        if (how)
                place_clamp(screen, rect[2], rect[3], line, &rect[0], &rect[1]);
        return COMPOSITOR_FLOATING;
}

// The distance, squared, from a point to a rectangle: 0 inside it.
static long place_distance(int x, int y, const int *rect)
{
        long dx = x < rect[0] ? rect[0] - x : x >= rect[0] + rect[2] ? x - (rect[0] + rect[2] - 1) : 0;
        long dy = y < rect[1] ? rect[1] - y : y >= rect[1] + rect[3] ? y - (rect[1] + rect[3] - 1) : 0;

        return dx * dx + dy * dy;
}

// The screen a point is on, or the nearest to it; the first of equals, and the first when there are none.
static unsigned int place_screen_at(int x, int y, const int (*screens)[4], unsigned int count)
{
        unsigned int found = 0;
        long best = -1;

        for (unsigned int at = 0; at < count; at++)
        {
                long distance = place_distance(x, y, screens[at]);

                if (best < 0 || distance < best)
                        best = distance, found = at;
        }
        return found;
}

// The screens' rectangles, and how many there are.
static unsigned int place_screens(int (*screens)[4])
{
        unsigned int count = compositor_output_count();

        count = count < WINDOW_PLACE_SCREENS ? count : WINDOW_PLACE_SCREENS;
        for (unsigned int at = 0; at < count; at++)
                compositor_output_rect((int)at, &screens[at][0], &screens[at][1], &screens[at][2], &screens[at][3]);
        return count;
}

// The room of a screen, the band on it where it is now taken out: the primary's, or with a band on every screen
// (`taskbar display all`) the screen's own.
static void compositor_room(const int *screen, int *room)
{
        memory_copy(room, screen, 4 * sizeof(int));
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();
                int band[4], cut[4];

                taskbar_band(&view, &band[0], &band[1], &band[2], &band[3]);
                place_room(screen, band, view.position, cut);
                if (memory_compare(cut, screen, sizeof(cut)))
                {
                        memory_copy(room, cut, sizeof(cut));
                        break;
                }
        }
        compositor.band_at = 0;
}

// The taskbar moved, or its screen changed: every window that fills the room of a screen fills it where it is now.
static void compositor_room_follow(void)
{
        int screens[WINDOW_PLACE_SCREENS][4], room[4];
        unsigned int count = place_screens(screens);

        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned int on;

                if (!compositor.showing[row] || compositor.arranged[row] != COMPOSITOR_ROOM)
                        continue;
                on = place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2,
                                     compositor.y[row] + (int)compositor.h[row] / 2, screens, count);
                compositor_room(screens[on], room);
                if (room[0] != compositor.x[row] || room[1] != compositor.y[row] || room[2] != (int)compositor.w[row] ||
                    room[3] != (int)compositor.h[row])
                        compositor_arrange_rect(row, COMPOSITOR_ROOM, room);
        }
}

/*
        A row's placement, read once into the compositor's own copy and clamped there: a mode it knows, a screen it
        can name, numbers inside WINDOW_PLACE_MOST and a size of at least a pixel. Anything else is the mode or the
        part left out, which is where the program asked, as a window always went. The name too, kept for what the
        compositor remembers, and none when it is not one.
*/
static void compositor_place_read(int row, struct window_place *place)
{
        struct windows *table = compositor.table;
        unsigned int how = __atomic_load_n(&table->place[row], __ATOMIC_ACQUIRE);
        int x = table->place_x[row], y = table->place_y[row];
        unsigned int width = table->place_width[row], height = table->place_height[row], screen = table->place_screen[row];

        memory_zero(place, sizeof(*place));
        if ((how & WINDOW_PLACE_HOW) <= WINDOW_PLACE_FULLSCREEN &&
            ((how & WINDOW_PLACE_HOW) != WINDOW_PLACE_AT ||
             (x >= -WINDOW_PLACE_MOST && x <= WINDOW_PLACE_MOST && y >= -WINDOW_PLACE_MOST && y <= WINDOW_PLACE_MOST)))
        {
                place->how = how & WINDOW_PLACE_HOW;
                place->x = x;
                place->y = y;
        }
        if ((how & WINDOW_PLACE_SCREEN) && screen < WINDOW_PLACE_SCREENS)
        {
                place->how |= WINDOW_PLACE_SCREEN;
                place->screen = screen;
        }
        if ((how & WINDOW_PLACE_SIZE) && width && height && width <= WINDOW_PLACE_MOST && height <= WINDOW_PLACE_MOST)
        {
                place->how |= WINDOW_PLACE_SIZE;
                place->width = width;
                place->height = height;
        }
        for (unsigned int at = 0; at < WINDOWS_NAME; at++)
                place->name[at] = ((volatile char *)table->name[row])[at];
        place->name[WINDOWS_NAME - 1] = 0;
        if (!window_name_valid(place->name))
                memory_zero(place->name, sizeof(place->name));
        memory_copy(compositor.name[row], place->name, WINDOWS_NAME);
}

/*
        Where windows were last put, kept (window.c's places): the files are /root's, and a checks run names others.
        The file is read when a window of a name opens and read, noted in and written whole when the hand lets one
        go, so nothing is held between the two and `canvas windows forget`, which takes the file away, needs to tell
        the compositor nothing.
*/
static const char *compositor_places_path = SETTING_WINDOWS_PLACES_PATH;
static const char *compositor_remember_path = SETTING_WINDOWS_REMEMBER_PATH;

// Whether windows are remembered: `canvas windows remember`, on unless it keeps off.
static int compositor_remembering(void)
{
        const struct settings_row *row = setting_by_path((string_address)SETTING_WINDOWS_REMEMBER_PATH);
        char word[16];
        long value = 0;

        compositor_word(compositor_remember_path, word, sizeof(word));
        return !(row && setting_kept(row, (string_address)word, &value) && value == 1);
}

static void compositor_places_read(struct places *places)
{
        static char text[PLACES_BYTES + 1];

        compositor_text(compositor_places_path, text, sizeof(text));
        places_parse(places, text, string_length((string_address)text));
}

/*
        The places written whole beside the file and renamed over it, so a reader finds the old one or the new and
        never half of either. Not synced: a drag's end is not to wait on a disk, and what a power cut takes is the
        last place a window was put, which the next drag puts back.
*/
static void compositor_places_write(const struct places *places)
{
        static char text[PLACES_BYTES];
        char stage[80];
        unsigned long length = places_say(places, text, sizeof(text)), path = string_length((string_address)compositor_places_path);
        bipolar handle, written;

        if (path + sizeof(".new") > sizeof(stage))
                return;
        memory_copy(stage, compositor_places_path, path);
        memory_copy(stage + path, ".new", sizeof(".new"));
        handle = system_open_at_mode(AT_FDCWD, (string_address)stage, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (handle < 0)
                return;
        written = length ? system_write_once(handle, text, length) : 0;
        system_close(handle);
        if (written != (bipolar)length ||
            system_rename_at(AT_FDCWD, (string_address)stage, AT_FDCWD, (string_address)compositor_places_path, 0) < 0)
                system_remove_at(AT_FDCWD, (string_address)stage, 0);
}

// The screen a kept place goes on: the one of its connector's name, else the one nearest where it was, else the first.
static unsigned int place_kept_screen(const struct place_kept *kept, const char (*names)[WINDOWS_NAME],
                                      const int (*screens)[4], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
                if (!memory_compare(names[at], kept->screen, WINDOWS_NAME))
                        return at;
        return place_screen_at(kept->origin_x + kept->x + kept->width / 2, kept->origin_y + kept->y + kept->height / 2,
                               screens, count);
}

// The screens' names, each the full WINDOWS_NAME bytes, nulls after.
static void compositor_screen_names(char (*names)[WINDOWS_NAME], unsigned int count)
{
        for (unsigned int at = 0; at < count; at++)
        {
                const char *name = compositor_output_name(at);
                unsigned int letter = 0;

                memory_zero(names[at], WINDOWS_NAME);
                for (; name[letter] && letter < WINDOWS_NAME - 1; letter++)
                        names[at][letter] = name[letter];
        }
}

/*
        Where the hand left a window, noted under its name: a window that has one and floats, let go of by the hand
        after a drag or a resize. Its place is kept from the top left of the screen its middle is on; a place off
        that screen is not kept, and nothing is written when the newest line already says it.
*/
static void compositor_remember(int row)
{
        int screens[WINDOW_PLACE_SCREENS][4];
        char names[WINDOW_PLACE_SCREENS][WINDOWS_NAME];
        unsigned int count, on;
        struct places places;
        struct place_kept kept;

        if (row < 0 || !compositor.showing[row] || !compositor.name[row][0] ||
            compositor.arranged[row] != COMPOSITOR_FLOATING || !compositor_remembering())
                return;
        count = place_screens(screens);
        on = place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2, compositor.y[row] + (int)compositor.h[row] / 2,
                             screens, count);
        compositor_screen_names(names, count);
        memory_zero(&kept, sizeof(kept));
        memory_copy(kept.name, compositor.name[row], WINDOWS_NAME);
        memory_copy(kept.screen, names[on], WINDOWS_NAME);
        kept.origin_x = screens[on][0];
        kept.origin_y = screens[on][1];
        kept.x = compositor.x[row] - screens[on][0];
        kept.y = compositor.y[row] - screens[on][1];
        kept.width = (int)compositor.w[row];
        kept.height = (int)compositor.h[row];
        if (kept.x < 0 || kept.y < 0 || kept.x >= screens[on][2] || kept.y >= screens[on][3] || kept.width < 1 ||
            kept.height < 1 || kept.width > WINDOW_PLACE_MOST || kept.height > WINDOW_PLACE_MOST ||
            !place_screen_name_valid(kept.screen))
                return;
        compositor_places_read(&places);
        if (places.count && !memory_compare(&places.kept[0], &kept, sizeof(kept)))
                return;
        places_note(&places, &kept);
        compositor_places_write(&places);
}

/*
        A window of a name put where the hand last left one: on the screen it was on, or the nearest to where that
        was, at the size it had up to the screen's, a step down and across from a window already there, and kept
        on the screen. False when no place is kept for it, and it goes where it would have.
*/
static int compositor_recall(int row, const int (*screens)[4], unsigned int count)
{
        char names[WINDOW_PLACE_SCREENS][WINDOWS_NAME];
        int least_w = WINDOW_MIN_WIDTH * compositor.scale / 100, least_h = WINDOW_MIN_HEIGHT * compositor.scale / 100, found, x, y;
        unsigned int on, width, height;
        struct places places;

        if (!compositor.name[row][0] || !compositor_remembering())
                return 0;
        compositor_places_read(&places);
        found = places_find(&places, compositor.name[row]);
        if (found < 0)
                return 0;
        compositor_screen_names(names, count);
        on = place_kept_screen(&places.kept[found], names, screens, count);
        x = screens[on][0] + places.kept[found].x;
        y = screens[on][1] + places.kept[found].y;
        width = (unsigned int)place_size((unsigned int)places.kept[found].width, screens[on][2], least_w);
        height = (unsigned int)place_size((unsigned int)places.kept[found].height, screens[on][3], least_h);
        compositor_put(row, x, y);
        if (width != compositor.w[row] || height != compositor.h[row])
                compositor_want(row, (int)width, (int)height);
        compositor_first_place(row, &x, &y);
        place_clamp(screens[on], (int)compositor.w[row], (int)compositor.h[row], compositor_line(row), &x, &y);
        compositor_put(row, x, y);
        return 1;
}

/*
        Where a window goes the first time it shows, from where its program asked (x, y) and the placement it was
        given: none is where the hand last put a window of its name (compositor_recall), and with nothing kept for it
        where it asked, a step down and across from any window in that place (compositor_first_place), as it always
        was; center and at are the rectangle place_rect works out, on the screen named or the first;
        maximized and fullscreen are where it would have gone with none, filling the room or the screen from there,
        so taking it out of that gives it back the place and size it would have had. A size is asked of the
        program as the hand asks one, and the window is shown at it until a picture of it comes.
*/
static void compositor_first_put(int row, int x, int y)
{
        int screens[WINDOW_PLACE_SCREENS][4], room[4], rect[4];
        int least_w = WINDOW_MIN_WIDTH * compositor.scale / 100, least_h = WINDOW_MIN_HEIGHT * compositor.scale / 100;
        unsigned int count = place_screens(screens), on, how, fills;
        struct window_place place;

        compositor_place_read(row, &place);
        // A placement stands over where the hand last put a window of the name; a name alone is no placement.
        if (!(place.how & (WINDOW_PLACE_HOW | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE)) && compositor_recall(row, screens, count))
                return;
        how = place.how & WINDOW_PLACE_HOW;
        on = (place.how & WINDOW_PLACE_SCREEN) && place.screen < count ? place.screen : 0;
        compositor_room(screens[on], room);

        // Where it asked is the first screen's, and the same place on the screen named.
        x += screens[on][0] - screens[0][0];
        y += screens[on][1] - screens[0][1];
        if (how == WINDOW_PLACE_AUTO || how == WINDOW_PLACE_MAXIMIZED || how == WINDOW_PLACE_FULLSCREEN)
        {
                if (place.how & WINDOW_PLACE_SIZE)
                {
                        compositor_put(row, x, y);
                        compositor_want(row, place_size(place.width, screens[on][2], least_w),
                                        place_size(place.height, screens[on][3], least_h));
                }
                compositor_first_place(row, &x, &y);
                compositor_place(row, x, y);
                if (how == WINDOW_PLACE_AUTO)
                        return;
        }
        fills = place_rect(&place, screens[on], room, x, y, compositor.w[row], compositor.h[row], compositor_line(row),
                           least_w, least_h, rect);
        if (fills != COMPOSITOR_FLOATING)
        {
                compositor_arrange_rect(row, fills, rect);
                return;
        }
        compositor_put(row, rect[0], rect[1]);
        if (rect[2] != (int)compositor.w[row] || rect[3] != (int)compositor.h[row])
                compositor_want(row, rect[2], rect[3]);
}

/*
        A window that shows, put again where its row's placement says now (`moonwater window move`): taken out of
        what it fills first, as a hand taking it would be, then put as compositor_first_put puts a new one, from
        where it is, on the screen named or the one it is on; with a screen and no mode it is at the same place on
        that screen. It is kept on the screen whatever it asked. One the hand holds is the hand's, and a placement
        of nothing leaves it where it is. The name it opened with is its name still.
*/
static void compositor_move(int row)
{
        int screens[WINDOW_PLACE_SCREENS][4], room[4], rect[4];
        int least_w = WINDOW_MIN_WIDTH * compositor.scale / 100, least_h = WINDOW_MIN_HEIGHT * compositor.scale / 100;
        unsigned int count = place_screens(screens), from, on, fills;
        char name[WINDOWS_NAME];
        struct window_place place;

        memory_copy(name, compositor.name[row], WINDOWS_NAME);
        compositor_place_read(row, &place);
        memory_copy(compositor.name[row], name, WINDOWS_NAME);
        if (compositor.drag == row || compositor.resize == row ||
            !(place.how & (WINDOW_PLACE_HOW | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE)))
                return;
        compositor_float(row);
        from = place_screen_at(compositor.x[row] + (int)compositor.w[row] / 2, compositor.y[row] + (int)compositor.h[row] / 2,
                               screens, count);
        on = (place.how & WINDOW_PLACE_SCREEN) && place.screen < count ? place.screen : from;
        compositor_room(screens[on], room);
        fills = place_rect(&place, screens[on], room, compositor.x[row] - screens[from][0] + screens[on][0],
                           compositor.y[row] - screens[from][1] + screens[on][1], compositor.w[row], compositor.h[row],
                           compositor_line(row), least_w, least_h, rect);
        if (fills != COMPOSITOR_FLOATING)
        {
                compositor_arrange_rect(row, fills, rect);
                return;
        }
        place_clamp(screens[on], rect[2], rect[3], compositor_line(row), &rect[0], &rect[1]);
        compositor_damage_row(row);
        compositor_put(row, rect[0], rect[1]);
        compositor_damage_row(row);
        if (rect[2] != (int)compositor.w[row] || rect[3] != (int)compositor.h[row])
                compositor_want(row, rect[2], rect[3]);
}

static void compositor_reveal(int row);

/*
        A knock: what the row says now, read once into the compositor's own
        columns and clamped there. A new title goes to its tab. A new picture
        is damaged and said to be taken; the first puts the window on the
        screen, where it asked to be or was placed (compositor_first_put) and
        on the output, over the others and with the keys unless it asked to
        be put away.
*/
static void compositor_take(int row)
{
        struct windows *table = compositor.table;
        unsigned int frame = __atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE);
        unsigned int width = table->width[row], height = table->height[row], front = table->buffer_index[row] & 1u;
        int damage[4] = {table->damage_x[row], table->damage_y[row], table->damage_width[row], table->damage_height[row]};
        char title[WINDOWS_TITLE];

        // The title, bounded and ended whatever the table holds.
        for (unsigned int at = 0; at < WINDOWS_TITLE; at++)
                title[at] = ((volatile char *)table->title[row])[at];
        title[WINDOWS_TITLE - 1] = 0;
        if (memory_compare(title, compositor.title[row], WINDOWS_TITLE))
        {
                memory_copy(compositor.title[row], title, WINDOWS_TITLE);
                if (compositor.showing[row])
                        compositor_tabs_changed();
        }

        // Lifted: brought back, raised and given the keys, as its tab would.
        if (compositor.showing[row] && table->lift[row] != compositor.lifted[row])
        {
                compositor.lifted[row] = table->lift[row];
                compositor_reveal(row);
        }
        // Moved: put where its placement says now.
        if (compositor.showing[row] && __atomic_load_n(&table->moved[row], __ATOMIC_ACQUIRE) != compositor.moved[row])
        {
                compositor.moved[row] = table->moved[row];
                compositor_move(row);
        }

        if (frame == compositor.seen[row])
                return;

        compositor.seen[row] = frame;
        width = width > (unsigned int)compositor_most_width() ? (unsigned int)compositor_most_width() : width;
        height = height > (unsigned int)compositor_most_height() ? (unsigned int)compositor_most_height() : height;
        // A window the card shows straight is told the picture was taken once the card has let go of the buffer
        // it showed before, at the flip's event; any other at once.
        if (row == compositor.direct_row)
        {
                compositor.taken_held[row] = frame;
                compositor.held_rows |= 1u << row;
        }
        else
                __atomic_store_n(&table->shown[row], frame, __ATOMIC_RELEASE);
        compositor.woken |= 1u << row;

        // A picture of nothing shows nothing.
        if (!width || !height)
                return;

        if (!compositor.showing[row])
        {
                int x = table->x[row], y = table->y[row];

                // Bounded to WINDOW_COORD_MAX, so the sums in compositor_first_place cannot wrap (a place of INT_MAX made
                // tx + w overflow). compositor_place keeps the window on the output either way.
                x = x > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : x < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : x;
                y = y > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : y < -WINDOW_COORD_MAX ? -WINDOW_COORD_MAX : y;
                compositor.w[row] = compositor.picture_w[row] = width;
                compositor.h[row] = compositor.picture_h[row] = height;
                compositor.asking[row] = 0;
                compositor.front[row] = front;
                compositor.state[row] = table->state[row] & WINDOW_MINIMIZED;
                table->state[row] = compositor.state[row];
                compositor.arranged[row] = COMPOSITOR_FLOATING;
                compositor.edges[row] = 0;
                compositor.lifted[row] = table->lift[row];
                compositor.moved[row] = table->moved[row];
                compositor.glassy[row] = table->glass[row] & WINDOW_GLASS;
                compositor.asks[row] = table->glass[row] & (WINDOW_NO_CLOSE | WINDOW_DISMISS | WINDOW_NO_TAB | WINDOW_NO_GRAB);
                compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
                compositor.showing[row] = 1;
                compositor.made[row] = ++compositor.next_made;
                if (compositor_popup_first(row))
                        return;
                compositor_first_put(row, x, y);
                compositor_raise(row);
                // Raised over the others, but given the keys only when the person asked for the program (its launch
                // carried WINDOW_ASKED_VARIABLE): a window a program opens on its own shows and leaves the keys alone.
                if (compositor_visible(row) && __atomic_load_n(&table->asked[row], __ATOMIC_ACQUIRE))
                        compositor_focus(row);
                compositor_land(row);
                compositor_tabs_changed();
                return;
        }

        compositor.picture_w[row] = width;
        compositor.picture_h[row] = height;
        // The buffer handed back is the program's again, whatever the tabs left on it: it draws that again.
        if (compositor.front[row] != front)
                compositor.patched[row][compositor.front[row] & 1u] = 0;
        compositor.front[row] = front;

        // The picture of the size asked for: shown as it is from here on, and, the hand let go, the edges are the
        // window's own again. One of another size while an ask is out is shown at the size asked, stretched. The
        // other window of a shared edge keeps the edge it shares until the hand lets go (compositor_shared_begin).
        if (compositor.asking[row] && width == table->want_width[row] && height == table->want_height[row])
        {
                compositor.asking[row] = 0;
                if (compositor.resize != row && compositor.shared_row != row)
                        compositor.edges[row] = 0;
        }
        if (!compositor.asking[row] && (width != compositor.w[row] || height != compositor.h[row]))
        {
                int x = compositor.x[row], y = compositor.y[row];

                compositor_damage_row(row);
                compositor.w[row] = width;
                compositor.h[row] = height;
                if (compositor.edges[row] & COMPOSITOR_EDGE_LEFT)
                        x = compositor.far_x[row] - (int)width;
                if (compositor.edges[row] & COMPOSITOR_EDGE_TOP)
                        y = compositor.far_y[row] - (int)height;
                compositor_place(row, x, y);
                damage[2] = 0;
                // The picture a scale taken made: if it is past the room it is asked to fit it.
                if (compositor.fit & (1u << row))
                {
                        compositor.fit &= ~(1u << row);
                        if (clock_monotonic_nanoseconds() < compositor.fit_until)
                                compositor_fit_row(row);
                }
        }
        if (width != compositor.w[row] || height != compositor.h[row])
        {
                compositor_stretch(row);
                damage[2] = 0;
        }

        if (!compositor_visible(row))
                return;

        // Clipped to the picture in a type the program's numbers cannot wrap; a width of 0 is all of it, and a
        // height of 0 none.
        {
                long x1 = damage[0] < 0 ? 0 : damage[0], y1 = damage[1] < 0 ? 0 : damage[1];
                long x2 = (long)damage[0] + damage[2], y2 = (long)damage[1] + damage[3];

                x2 = x2 > (long)width ? (long)width : x2;
                y2 = y2 > (long)height ? (long)height : y2;
                compositor.damage_by = row + 1;
                if (damage[2] <= 0)
                        compositor_damage_row(row);
                else if (x2 > x1 && y2 > y1)
                        compositor_damage(compositor.x[row] + (int)x1, compositor.y[row] + (int)y1, (int)(x2 - x1),
                                          (int)(y2 - y1));
                // A picture the card shows straight from its buffer is said taken by the present that puts it there,
                // and a present comes only for damage: one that changed no pixel still asks for one (a pixel of the
                // window on the output), or its program waits on it until something else is damaged.
                else if (compositor.held_rows & (1u << row))
                {
                        int x = compositor.x[row], y = compositor.y[row];

                        compositor_damage(x < 0 ? 0 : x >= compositor.width ? compositor.width - 1 : x,
                                          y < 0 ? 0 : y >= compositor.height ? compositor.height - 1 : y, 1, 1);
                }
                compositor.damage_by = 0;
        }
}

/*
        A row whose door has closed: its program is gone. The window goes
        from the screen, its pixels are given back, and the row is free.
*/
static void compositor_leave(int row)
{
        struct windows *table = compositor.table;

        if (compositor.showing[row])
        {
                compositor_damage_row(row);
                compositor_tabs_changed();
        }

        compositor_stack_remove(row);
        // An armed assist ends with its own window, and loses the thumbnail of one that goes (it is no window to place).
        if (compositor.assist_on && compositor.assist_row == row)
                compositor_assist_close();
        else if (compositor.assist_on)
                compositor_assist_refresh();
        if (compositor.keys_to == row)
        {
                compositor.keys_to = COMPOSITOR_NONE;
                memory_zero(compositor.keys_down, sizeof(compositor.keys_down));
        }
        compositor.hover = compositor.hover == row ? COMPOSITOR_NONE : compositor.hover;
        compositor.told_row = compositor.told_row == row ? COMPOSITOR_NONE : compositor.told_row;
        compositor.grab = compositor.grab == row ? COMPOSITOR_NONE : compositor.grab;
        if (compositor.preview_row == row)
                compositor_preview(COMPOSITOR_NONE);
        // What is drawn of its preview goes with it, and nothing of it is held to be put back.
        if (compositor.preview_draw == row + 1)
        {
                int drawn[4];

                if (compositor_preview_shown(drawn))
                        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[3]);
                compositor.preview_draw = 0;
        }
        compositor.hold_rows &= ~(1u << row);
        if (compositor.mark_row == row || compositor.mark_with == row || (compositor.mark_rows >> row & 1))
                compositor_mark_clear();
        // A window of the divider resized, going, ends the resize: the others are no longer taken with it.
        if (compositor.shared_row >= 0 && ((compositor.shared_lo_set | compositor.shared_hi_set) >> row & 1))
                compositor.shared_row = COMPOSITOR_NONE, compositor.resize = COMPOSITOR_NONE;
        compositor.drag = compositor.drag == row ? COMPOSITOR_NONE : compositor.drag;
        compositor.press = compositor.press == row ? COMPOSITOR_NONE : compositor.press;
        compositor.resize = compositor.resize == row ? COMPOSITOR_NONE : compositor.resize;
        // A window that comes into the row next has no edge of this one's lit, and a finger on its X closes nothing.
        if (compositor.lit_row == row)
                compositor.lit_row = COMPOSITOR_NONE, compositor.lit_edges = compositor.lit_held = 0;
        compositor.touch.closing = compositor.touch.closing == row ? COMPOSITOR_NONE : compositor.touch.closing;
        // The window a long press landed on (compositor_touch_tick) gone: the lift that ends the press goes nowhere, not to the
        // next program in the row, which never had the finger and would be given a press of the person's for the clipboard.
        compositor.touch.hold_row = compositor.touch.hold_row == row ? COMPOSITOR_NONE : compositor.touch.hold_row;
        // A button held over the window that went is let go nowhere: the row is the next program's, which never had
        // the press (compositor_release).
        for (unsigned int index = 0; index < 8; index++)
                if (compositor.button_row[index] == row)
                        compositor.button_down &= (unsigned char)~(1u << index);
        // The stack is one shorter, so an Alt-Tab under way ends here, on the window it stopped on, as letting go of
        // Alt would have ended it: that window is brought back if it was put away, and has the keys.
        if (compositor.cycle)
        {
                compositor.cycle = 0;
                if (compositor.focus >= 0 && compositor.focus != row)
                        compositor_reveal(compositor.focus);
        }

        compositor.showing[row] = compositor.seen[row] = compositor.state[row] = 0;
        compositor.landing[row] = 0;
        compositor.glide[row] = 0;
        compositor.w[row] = compositor.h[row] = compositor.edges[row] = 0;
        compositor.picture_w[row] = compositor.picture_h[row] = compositor.asking[row] = 0;
        compositor.asks[row] = 0;
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.title[row][0] = 0;
        compositor.name[row][0] = 0;
        compositor_glass_drop(row);
        compositor_runs_drop(row);
        compositor.glass_side[row] = compositor.glass_told[row] = 0;
        // Shown straight: the screen is the compositor's again at the next frame, and the window's framebuffers
        // go when the card is off them (now, if it is).
        compositor.held_rows &= ~(1u << row);
        if (compositor.direct_row == row)
        {
                compositor.direct_row = -1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        if (compositor.plane_row == row)
                compositor.plane_row = -1;
        if (compositor_shows_row(row) || (compositor.flip_pending && compositor.flip_row == row))
                compositor.dropping |= 1u << row;
        else if (compositor.scan_fb[row][0] || compositor.scan_fb[row][1])
                compositor_window_fb_drop(row);
        // Pinned for a framebuffer the card refused, which was never made: no drop above would let go of it.
        else
                __atomic_store_n(&table->pinned[row], 0u, __ATOMIC_RELEASE);
        table->spoiled_height[row][0] = table->spoiled_height[row][1] = 0;
        table->ground[row] = 0;
        table->hover_off[row] = 0;
        table->state[row] = 0;
        table->want_width[row] = table->want_height[row] = 0;
        table->shown[row] = 0;

        // MADV_REMOVE: the pages of its two buffers.
        compositor_call(syscall(madvise), (positive)compositor.table + compositor.pixels + row * 2 * compositor.buffer,
                        2 * compositor.buffer, 9, 0, 0, 0);

        if (compositor.focus == row)
                compositor_refocus();
        compositor.woken &= ~(1u << row);
        __atomic_store_n(&table->pid[row], 0, __ATOMIC_RELEASE);
}

/*
        What a tab does: its window comes back if it was put away, rises over
        the others and has the keys.
*/
static void compositor_reveal(int row)
{
        if (row < 0 || !compositor.showing[row])
                return;

        if (compositor.state[row] & WINDOW_MINIMIZED)
        {
                compositor.state[row] &= ~WINDOW_MINIMIZED;
                compositor.table->state[row] = compositor.state[row];
                compositor_damage_row(row);
                compositor_tabs_changed();
        }

        compositor_raise(row);
        compositor_focus(row);
}

/*
        Alt-Tab: one step gives the keys to the next window down, put away or
        not, and leaves the stack as it is, so a window put away can be looked
        at and passed; letting go of Alt brings the one it stopped on back if
        it was put away and raises it. Starting from the top, when the window
        with the keys is the one that is on top, it is the one behind; when it
        is not (it was put away and the keys went down) it is the one on top,
        which is the one that was put away, so one Alt-Tab restores it. A window that asked for no tab is passed over
        (compositor_cycles): it has no entry in the cycle.
*/
static void compositor_focus_step(void)
{
        int top = (int)compositor.stacked, focused = 0, below, next;

        for (int at = 0; at < (int)compositor.stacked; at++)
                if (compositor.stack[at] == compositor.focus)
                        focused = at + 1;

        below = compositor.cycle ? compositor.cycle : focused && focused == top ? focused : top + 1;
        next = below > 1 ? below - 1 : top;
        for (int skipped = 0; next && skipped < top && !compositor_cycles(compositor.stack[next - 1]); skipped++)
                next = next > 1 ? next - 1 : top;

        if (!next || next == focused || !compositor_cycles(compositor.stack[next - 1]))
                return;

        compositor.cycle = next;
        compositor_focus(compositor.stack[next - 1]);
}

static void compositor_focus_commit(void)
{
        compositor.cycle = 0;
        compositor_reveal(compositor.focus);
}

// A window put away, by its minimise button or Alt-F9; its tab brings it back. The keys go to the one under it if it had them.
static void compositor_minimize_row(int row)
{
        if (row < 0 || !compositor_visible(row))
                return;

        compositor_damage_row(row);
        compositor.state[row] |= WINDOW_MINIMIZED;
        compositor.table->state[row] = compositor.state[row];
        if (row == compositor.focus)
                compositor_refocus();
        compositor_tabs_changed();
}

// Alt-F9: the window with the keys is put away, and the keys go to the one under it.
static void compositor_minimize_focused(void)
{
        compositor_minimize_row(compositor.focus);
}

// A window asked to go: by its close button, Alt-F4, or a middle press on its tab. What happens next is the program's.
static void compositor_close(int row)
{
        if (row < 0 || !compositor.showing[row])
                return;

        compositor.state[row] |= WINDOW_CLOSING;
        compositor.table->state[row] = compositor.state[row];
        compositor.woken |= 1u << row;
}

// A door knocked on or closed: the row read, and a closed one let go of and opened again for the next program.
/* ---- the clipboard ---- */

/*
        The two selections are the compositor's, held as the memfds programs made them in: a copy is a sealed memfd a
        window names in its row, opened here through /proc/PID/fd (the compositor is root, as every window is), checked
        -- sealed against growing, shrinking and writing, at most the limit, UTF-8 with no NUL -- and kept at the
        descriptor WINDOWS_CLIP_FD + which, a number nothing else is given. A paste is that descriptor opened again by
        the window the compositor answers READY to, which is only the window with the keys (or a popup of it), and only
        within COMPOSITOR_CLIP_GESTURE_NS of a key or a button of the person's on it: a window in the background, or
        one that asks with nobody at the keys, is answered REFUSED and learns nothing. A copy is a gesture too: a
        window cannot fill the clipboard on its own. The command line (`moonwater clipboard`, root) offers and clears
        through the header and knocks on WINDOWS_CLIP_DOOR. Nothing is written to a disk: the history, when it is kept
        (`moonwater canvas clipboard history N`), is memfds in this process, and off empties it all.
*/
#define COMPOSITOR_CLIP_GESTURE_NS 1500000000ull
#define COMPOSITOR_CLIP_HISTORY 32
#define COMPOSITOR_F_GET_SEALS 1034
#define COMPOSITOR_F_DUPFD_CLOEXEC 1030
#define COMPOSITOR_CLIP_SEALS 14 // SHRINK, GROW and WRITE: the bytes and the size are what they were when it was taken

static struct
{
        int door;
        int fd[2];
        unsigned int offer_seen[WINDOWS], ask_seen[WINDOWS], cli_seen;
        positive gesture[WINDOWS];
        unsigned int history_most, history_count;
        int history[COMPOSITOR_CLIP_HISTORY];
} compositor_clip = {.door = -1, .fd = {-1, -1}};

// The window a row belongs to: itself, or the window at the root of its popups.
static int compositor_clip_root(int row)
{
        for (int hops = 0; hops < WINDOWS && row >= 0 && compositor.table->popup_parent[row]; hops++)
        {
                unsigned int parent = compositor.table->popup_parent[row];

                row = parent >= 1 && parent <= WINDOWS ? (int)parent - 1 : -1;
        }
        return row;
}

// A key or a button the person pressed on a row: the row and the window it belongs to may paste for a while.
static void compositor_clip_gesture(int row)
{
        positive now = clock_monotonic_nanoseconds();
        int root = compositor_clip_root(row);

        if (row >= 0)
                compositor_clip.gesture[row] = now;
        if (root >= 0)
                compositor_clip.gesture[root] = now;
}

static void compositor_clip_tell(int row, unsigned int which, int what)
{
        if (row >= 0 && row < WINDOWS && compositor.table->pid[row])
                compositor_post(row, WINDOW_EVENT_CLIP, 0, (int)which, what);
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_selection(unsigned int which);
#endif

// One selection let go, and its watchers told.
static void compositor_clip_clear(unsigned int which)
{
        if (compositor_clip.fd[which] >= 0)
                system_close(compositor_clip.fd[which]);
        compositor_clip.fd[which] = -1;
        compositor.table->clip_length[which] = 0;
        compositor.table->clip_owner[which] = -1;
        __atomic_add_fetch(&compositor.table->clip_serial[which], 1, __ATOMIC_RELEASE);
        for (int row = 0; row < WINDOWS; row++)
                if (compositor.table->clip_watch[row])
                        compositor_clip_tell(row, which, WINDOWS_CLIP_CHANGED);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_selection(which);
#endif
}

// Every selection and the history let go: the service turned off, or `moonwater clipboard clear`.
static void compositor_clip_wipe(void)
{
        for (unsigned int which = 0; which < 2; which++)
                compositor_clip_clear(which);
        for (unsigned int at = 0; at < compositor_clip.history_count; at++)
                system_close(compositor_clip.history[at]);
        compositor_clip.history_count = 0;
}

// The settings: on or off, how many copies the history keeps (none by default), the limit in MiB (16 by default).
#ifndef SHELL_NO_GPU
// GPU rendering as the setting has it (on unless the file says off): what the compose path asks before it uses src/canvas/gpu.c.
static int compositor_gpu = 1;

static void compositor_gpu_settings(void)
{
        char word[16];
#ifdef MOONWATER_GPU_C
        int was = compositor_gpu;
#endif

        compositor_word(SETTING_GPU_PATH, word, sizeof(word));
        compositor_gpu = !string_equals((string_address)word, (string_address) "off");
#ifdef MOONWATER_GPU_C
        // Turned on again: a GPU given up on after three hangs is tried again (one that never started, a card that is not one the kernels are for or that
        // drew its self-test wrong, stays given up).
        if (compositor_gpu && !was && compositor_gpu_state.up)
                compositor_gpu_state.dead = 0, compositor_gpu_state.failures = 0;
#endif
}
#endif

static void compositor_clip_settings(void)
{
        char word[16];
        long value;

        if (!compositor.table)
                return;
        compositor_word(SETTING_CLIPBOARD_PATH, word, sizeof(word));
        compositor.table->clip_on = !string_equals((string_address)word, "off");
        compositor_word(SETTING_CLIPBOARD_HISTORY_PATH, word, sizeof(word));
        compositor_clip.history_most = compositor_setting_kept(SETTING_CLIPBOARD_HISTORY_PATH, word, &value) && value >= 0 &&
                                               value <= COMPOSITOR_CLIP_HISTORY
                                           ? (unsigned int)value
                                           : 0;
        compositor_word(SETTING_CLIPBOARD_LIMIT_PATH, word, sizeof(word));
        compositor.table->clip_limit = compositor_setting_kept(SETTING_CLIPBOARD_LIMIT_PATH, word, &value) && value >= 1 && value <= 16
                                           ? (unsigned int)value << 20
                                           : WINDOWS_CLIP_LIMIT;
        if (!compositor.table->clip_on)
                compositor_clip_wipe();
        while (compositor_clip.history_count > compositor_clip.history_most)
                system_close(compositor_clip.history[--compositor_clip.history_count]);
}

/*
        A memfd another process holds, opened and checked: its descriptor here and its length, or -1. Sealed against
        every change of its bytes and size, no longer than the limit, and text a selection may hold.
*/
static int compositor_clip_open(unsigned int pid, int fd, unsigned int address_to length)
{
        char path[48];
        unsigned char info[256];
        long held, opened, size, seals, mapped;
        unsigned int mode;
        int text;

        if (!pid || fd < 0)
                return -1;
        /*
                The descriptor is whichever the row names, so it is anything its program holds: the read end of a pipe or a
                FIFO, whose open for reading waits for a writer for ever and the desktop with it; a terminal, which a session
                leader's open takes for its own; a device, whose open is its driver's to act on. Its file is taken first by
                O_PATH, which opens nothing, and only a regular file (a memfd is one) is opened, through that hold, so the
                file opened is the one looked at whatever the program does to its descriptor in between.
        */
        windows_proc_fd(path, pid, (unsigned int)fd);
        held = (long)system_open_at(AT_FDCWD, (string_address)path, O_PATH | O_CLOEXEC);
        if (held < 0)
                return -1;
        // struct statx: stx_mode at 28 (u16); STATX_TYPE of the descriptor itself (AT_EMPTY_PATH).
        memory_zero(info, sizeof(info));
        mode = compositor_call(syscall(statx), (positive)held, (positive) "", 0x1000, 1, (positive)info, 0) < 0
                   ? 0
                   : (unsigned int)info[28] | (unsigned int)info[29] << 8;
        if ((mode & 0170000) == 0100000)
        {
                windows_proc_fd(path, (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (unsigned int)held);
                opened = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        }
        else
                opened = -1;
        system_close((bipolar)held);
        if (opened < 0)
                return -1;
        seals = compositor_call(syscall(fcntl), (positive)opened, COMPOSITOR_F_GET_SEALS, 0, 0, 0, 0);
        size = compositor_call(syscall(lseek), (positive)opened, 0, 2, 0, 0, 0);
        if (seals < 0 || (seals & COMPOSITOR_CLIP_SEALS) != COMPOSITOR_CLIP_SEALS || size <= 0 ||
            (unsigned long)size > compositor.table->clip_limit)
        {
                system_close((bipolar)opened);
                return -1;
        }
        mapped = compositor_call(syscall(mmap), 0, (positive)size, 1, 2, (positive)opened, 0);
        text = !(mapped < 0 && mapped > -4096) && windows_clip_text((const unsigned char *)mapped, (unsigned long)size);
        if (!(mapped < 0 && mapped > -4096))
                compositor_call(syscall(munmap), (positive)mapped, (positive)size, 0, 0, 0, 0);
        if (!text)
        {
                system_close((bipolar)opened);
                return -1;
        }
        *length = (unsigned int)size;
        return (int)opened;
}

// A checked memfd made selection which, owned by owner (a row, or -2 for the command line); watchers are told.
static void compositor_clip_keep(unsigned int which, int opened, unsigned int length, int owner)
{
        // The copy before it goes to the history, when one is kept: a descriptor of its own, newest first.
        if (which == WINDOWS_CLIP_CLIPBOARD && compositor_clip.fd[which] >= 0 && compositor_clip.history_most)
        {
                long kept = compositor_call(syscall(fcntl), (positive)compositor_clip.fd[which], COMPOSITOR_F_DUPFD_CLOEXEC,
                                            WINDOWS_CLIP_FD + 8, 0, 0, 0);

                if (kept >= 0)
                {
                        if (compositor_clip.history_count == compositor_clip.history_most)
                                system_close(compositor_clip.history[--compositor_clip.history_count]);
                        memory_copy(compositor_clip.history + 1, compositor_clip.history,
                                    compositor_clip.history_count * sizeof(compositor_clip.history[0]));
                        compositor_clip.history[0] = (int)kept;
                        compositor_clip.history_count++;
                }
        }
        // dup3 puts it at the selection's number, letting go of what was there in the same call.
        compositor_call(syscall(dup3), (positive)opened, WINDOWS_CLIP_FD + which, O_CLOEXEC, 0, 0, 0);
        system_close(opened);
        compositor_clip.fd[which] = WINDOWS_CLIP_FD + (int)which;
        compositor.table->clip_length[which] = length;
        compositor.table->clip_owner[which] = owner;
        __atomic_add_fetch(&compositor.table->clip_serial[which], 1, __ATOMIC_RELEASE);
        for (int row = 0; row < WINDOWS; row++)
                if (compositor.table->clip_watch[row] && row != owner)
                        compositor_clip_tell(row, which, WINDOWS_CLIP_CHANGED);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_selection(which);
#endif
}

// What a row's knock may carry for the clipboard: an offer of a copy, a paste asked for.
static void compositor_clip_door(int row)
{
        struct windows address_to table = compositor.table;
        unsigned int offer = __atomic_load_n(&table->clip_offer_serial[row], __ATOMIC_ACQUIRE);
        unsigned int ask = __atomic_load_n(&table->clip_ask_serial[row], __ATOMIC_ACQUIRE);
        positive now;

        if (offer == compositor_clip.offer_seen[row] && ask == compositor_clip.ask_seen[row])
                return;
        now = clock_monotonic_nanoseconds();
        if (offer != compositor_clip.offer_seen[row])
        {
                unsigned int which = table->clip_offer_which[row] & 1u, length = 0;
                int opened = -1;

                compositor_clip.offer_seen[row] = offer;
                // A copy is the person's: a window copies after a key or a button of theirs on it, not on its own.
                if (table->clip_on && now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS)
                        opened = compositor_clip_open(table->pid[row], table->clip_offer_fd[row], &length);
                if (opened >= 0)
                        compositor_clip_keep(which, opened, length, row);
                __atomic_store_n(&table->clip_offer_answer[row], opened >= 0 ? offer : offer | WINDOWS_CLIP_NO, __ATOMIC_RELEASE);
                compositor_clip_tell(row, which, opened >= 0 ? WINDOWS_CLIP_TAKEN : WINDOWS_CLIP_DECLINED);
        }
        if (ask != compositor_clip.ask_seen[row])
        {
                unsigned int which = table->clip_ask_which[row] & 1u;
                int root = compositor_clip_root(row);
                b32 may = table->clip_on && compositor_clip.fd[which] >= 0 && root >= 0 && root == compositor.focus &&
                          now - compositor_clip.gesture[row] <= COMPOSITOR_CLIP_GESTURE_NS;

                compositor_clip.ask_seen[row] = ask;
                if (may)
                {
                        table->clip_given_length[row] = table->clip_length[which];
                        __atomic_store_n(&table->clip_given_serial[row], ask, __ATOMIC_RELEASE);
                }
                compositor_clip_tell(row, which, may ? WINDOWS_CLIP_READY : WINDOWS_CLIP_REFUSED);
        }
}

// A knock on the clipboard's own door: the command line offered, cleared, or changed the settings.
static void compositor_clip_cli(void)
{
        struct windows address_to table = compositor.table;
        char bytes[64];
        unsigned int serial;

        for (int reads = 0; reads < 64 && system_read_once(compositor_clip.door, bytes, sizeof(bytes)) > 0; reads++)
                ;
        if (table->cli_clear)
        {
                if (table->cli_clear == 2)
                        compositor_clip_settings();
                else
                        compositor_clip_wipe();
                __atomic_store_n(&table->cli_clear, 0u, __ATOMIC_RELEASE);
        }
        serial = __atomic_load_n(&table->cli_serial, __ATOMIC_ACQUIRE);
        if (serial != compositor_clip.cli_seen)
        {
                unsigned int length = 0;
                int opened = table->clip_on ? compositor_clip_open(table->cli_pid, (int)table->cli_fd, &length) : -1;

                compositor_clip.cli_seen = serial;
                if (opened >= 0)
                        compositor_clip_keep(table->cli_which & 1u, opened, length, -2);
                __atomic_store_n(&table->cli_answer, opened >= 0 ? serial : serial | WINDOWS_CLIP_NO, __ATOMIC_RELEASE);
        }
}

// The door made with the table's, and the service's numbers in the header.
static int compositor_clip_start(void)
{
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0, 0, 0, 0);
        compositor_call(syscall(mknodat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0010000 | WINDOWS_MODE, 0, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, WINDOWS_MODE, 0, 0, 0);
        compositor_clip.door = (int)system_open_at(AT_FDCWD, (string_address)WINDOWS_CLIP_DOOR, FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC);
        compositor.table->clip_pid = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        compositor.table->clip_owner[0] = compositor.table->clip_owner[1] = -1;
        compositor_clip_settings();
#ifndef SHELL_NO_GPU
        compositor_gpu_settings();
#endif
        return compositor_clip.door;
}

// A row gone: its copies stay (they are this process's now), and are owned by nobody.
static void compositor_clip_leave(int row)
{
        for (unsigned int which = 0; which < 2; which++)
                if (compositor.table->clip_owner[which] == row)
                        compositor.table->clip_owner[which] = -1;
        compositor_clip.gesture[row] = 0;
        compositor_clip.offer_seen[row] = compositor.table->clip_offer_serial[row];
        compositor_clip.ask_seen[row] = compositor.table->clip_ask_serial[row];
}

/* ---- popups: menus a window opens, placed and dismissed here ---- */

/*
        A popup is a row whose program named a parent row (popup_parent) and an anchor in the parent's pixels before
        its first picture. It is put against the anchor on the screen that holds the anchor -- a point's menu below
        and right of it, a rectangle's below it, a submenu's right of it -- and flipped to the other side where that
        screen ends, then held inside the screen. It has no tab, never takes the keys from its window (the window is
        still the one shown with them), but the keys go to the topmost popup of the window that has them while one is
        open. It is asked to go (WINDOW_CLOSING) by a press outside every popup, its window losing the keys, its
        window being taken hold of to move, or its window going.
*/
static int compositor_popup_parent[WINDOWS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

/*
        Where a popup width by height goes against an anchor (ax, ay, aw, ah in the desktop's space) on a screen (sx,
        sy, sw, sh): the pure rule, which the check holds to every edge. flags are WINDOW_POPUP_*.
*/
static void compositor_popup_place(int ax, int ay, int aw, int ah, int width, int height, unsigned int flags, const int *screen,
                                   int *x, int *y)
{
        // The window's flags say the side and the flips (ui_anchor_place does the placing, for the kit's panels too): EXACT is
        // its own place, its top left held on the screen; RIGHT is beside the rectangle's right edge, flipped left when it
        // would run off; a POINT is a rectangle of no size, below and flipped above, and left when it would run off the right;
        // else below the rectangle, flipped above when it would run off the bottom.
        int rect[4] = {ax, ay, aw, ah};
        unsigned int side = UI_BELOW, flip = UI_FLIP_SIDE;

        if (flags & WINDOW_POPUP_EXACT)
                side = UI_AT, flip = 0;
        else if (flags & WINDOW_POPUP_RIGHT)
                side = UI_RIGHT;
        else if (flags & WINDOW_POPUP_POINT)
                rect[2] = rect[3] = 0, flip |= UI_FLIP_CROSS;
        ui_anchor_place(side, flip, rect, screen, width, height, x, y);
}

// The window a popup belongs to at the root of its chain, or the row itself.
static int compositor_popup_root(int row)
{
        for (int hops = 0; hops < WINDOWS && row >= 0 && compositor_popup_parent[row] >= 0; hops++)
                row = compositor_popup_parent[row];
        return row;
}

// The topmost popup of a window, which the keys go to while it is open, or the window itself. A popup asked to go (a press
// outside it, its window losing the keys) has none: until it has gone it is no one's to type to, and a hung one would keep them.
static int compositor_popup_top(int root)
{
        if (root < 0)
                return root;
        for (unsigned int at = compositor.stacked; at--;)
        {
                int row = compositor.stack[at];

                if (compositor_popup_parent[row] >= 0 && compositor.showing[row] && !(compositor.state[row] & WINDOW_CLOSING) &&
                    compositor_popup_root(row) == root)
                        return row;
        }
        return root;
}

// Every popup asked to go: of one window (root), or of every window (-1); one in keep's chain stays. True when a popup
// that was still open is asked now: one already asked, on its way out, is not counted.
static b32 compositor_popups_close(int root, int keep)
{
        b32 asked = false;

        for (int row = 0; row < WINDOWS; row++)
        {
                b32 kept = false;

                if (compositor_popup_parent[row] < 0 || !compositor.showing[row])
                        continue;
                if (root >= 0 && compositor_popup_root(row) != root)
                        continue;
                for (int link = keep, hops = 0; link >= 0 && hops < WINDOWS; link = compositor_popup_parent[link], hops++)
                        kept |= link == row;
                if (!kept)
                {
                        asked |= !(compositor.state[row] & WINDOW_CLOSING);
                        compositor_close(row);
                }
        }
        return asked;
}

/*
        A first picture that is a popup's: its parent remembered, its place worked out against the anchor and the screen
        the anchor is on, and raised over everything without the keys. False for a window, which is placed as ever.
*/
static b32 compositor_popup_first(int row)
{
        struct windows *table = compositor.table;
        unsigned int named = table->popup_parent[row];
        int parent = named >= 1 && named <= WINDOWS ? (int)named - 1 : -1;
        unsigned int count;
        const int (*screens)[4];
        int ax, ay, aw, ah, x, y, at;

        if (parent < 0 || parent == row || !compositor.showing[parent])
                return false;
        compositor_popup_parent[row] = parent;
        ax = compositor.x[parent] + compositor_fit(table->popup_x[row], -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST);
        ay = compositor.y[parent] + compositor_fit(table->popup_y[row], -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST);
        aw = compositor_fit(table->popup_w[row], 0, WINDOW_PLACE_MOST);
        ah = compositor_fit(table->popup_h[row], 0, WINDOW_PLACE_MOST);
        screens = compositor_screens(&count);
        at = compositor_screen_at(screens, count, ax, ay);
        at = at < 0 ? compositor_screen_of(parent) : at;
        at = at < 0 ? 0 : at;
        compositor_popup_place(ax, ay, aw, ah, (int)compositor.w[row], (int)compositor.h[row], table->popup_flags[row], screens[at], &x, &y);
        compositor_put(row, x, y);
        compositor_raise(row);
        compositor_damage_row(row);
        return true;
}

// A row gone: it is no popup any more, and the popups of it go too.
static void compositor_popup_leave(int row)
{
        compositor_popup_parent[row] = -1;
        compositor_popups_close(row, -1);
        // Asked to go, they are no popups of whatever takes the row next: their link to it is cut.
        for (int child = 0; child < WINDOWS; child++)
                if (compositor_popup_parent[child] == row)
                        compositor_popup_parent[child] = -1;
}

static void compositor_door(int row, int returned)
{
        char bytes[64];
        p8 name[48];

        // Any program may open a door and write to it, so what is read in one turn is bounded: one that never stops
        // writing is not the whole of the compositor's time.
        for (int reads = 0; reads < 64 && system_read_once(compositor.door[row], bytes, sizeof(bytes)) > 0; reads++)
                ;

        compositor_take(row);
        compositor_clip_door(row);
        if (!(returned & COMPOSITOR_POLLHUP))
                return;

        windows_door_name((char *)name, (unsigned int)row, 0);
        system_close(compositor.door[row]);
        compositor.door[row] = (int)system_open_at(AT_FDCWD, name, FILE_READ | O_NONBLOCK | O_CLOEXEC);
        compositor_clip_leave(row);
        compositor_popup_leave(row);
        compositor_leave(row);
}

// How long a report took to be on its way to the screen or the window, which `moonwater latency`
// of the kernel desktop was for: a count, a total and the worst, in nanoseconds.
static void compositor_latency(unsigned int which, positive reported, positive now)
{
        positive took = reported && now > reported ? now - reported : 0;

        compositor.latency[which].count++;
        compositor.latency[which].total += took;
        if (took > compositor.latency[which].worst)
                compositor.latency[which].worst = took;
}

/*
        How long the processor is allowed to take waking up. The largest cost
        from a pointer moving to the program that draws it is the processor
        coming back from an idle state, and the deeper the state the longer
        that takes, so while a person is at the machine it is asked to stay
        out of them: a zero in /dev/cpu_dma_latency, which holds for as long
        as the file is open, taken after the first thing they do (the first
        pays the wakeup either way: the processor was idle when the interrupt
        came) and let go of half a second after the last. What counts is what
        a hand or a key did and not a program's commit, so a clock ticking in
        a window does not keep it. A request of zero is a processor polling,
        which to cpufreq is busy, so it is not held a moment longer than that.
*/
static void compositor_hold_touch(positive now)
{
        int zero = 0;

        compositor.hold_until = now + COMPOSITOR_HOLD_NS;
        if (compositor.hold >= 0 || compositor.holds == ~0ul)
                return;

        compositor.hold = (int)system_open_at(AT_FDCWD, (string_address) "/dev/cpu_dma_latency", O_WRONLY | O_CLOEXEC);
        if (compositor.hold < 0)
        {
                // Not there on this machine: it is not asked for again.
                compositor.hold = -1;
                compositor.holds = ~0ul;
                return;
        }

        system_write_once(compositor.hold, &zero, sizeof(zero));
        compositor.holds++;
}

static void compositor_hold_drop(positive now)
{
        if (compositor.hold < 0 || now < compositor.hold_until)
                return;

        system_close(compositor.hold);
        compositor.hold = -1;
}

/*
        The desktop's keys, which are the machine script's: a function there
        named key_ and a chord -- modifiers and a key joined by _, as
        key_ctrl_shift_t -- is run when the chord is pressed. The script
        writes its key_ functions' names to /run/moonwater/keys, a line each,
        before it starts this program; a chord pressed is no window's key, and
        its name is written, a line, to the FIFO /run/moonwater/keys.fire,
        which the script reads and runs. Everything on the desktop is started
        by the machine script and by nothing else; this process forks nothing.
        The keys are the physical ones, so a chord is the same key whatever
        the layout; the modifiers are ctrl (or control), shift, alt and super.
*/
#define COMPOSITOR_KEYS 32
#define COMPOSITOR_KEYS_FILE "/run/moonwater/keys"
#define COMPOSITOR_KEYS_FIRE "/run/moonwater/keys.fire"
static const char *compositor_keys_fire = COMPOSITOR_KEYS_FIRE; // and a check's own

static const struct
{
        const char *name;
        unsigned char code;
} compositor_key_names[] = {
    {"escape", 1}, {"1", 2}, {"2", 3}, {"3", 4}, {"4", 5}, {"5", 6}, {"6", 7}, {"7", 8}, {"8", 9}, {"9", 10},
    {"0", 11}, {"minus", 12}, {"equal", 13}, {"backspace", 14}, {"tab", 15}, {"q", 16}, {"w", 17}, {"e", 18},
    {"r", 19}, {"t", 20}, {"y", 21}, {"u", 22}, {"i", 23}, {"o", 24}, {"p", 25}, {"leftbrace", 26},
    {"rightbrace", 27}, {"enter", 28}, {"a", 30}, {"s", 31}, {"d", 32}, {"f", 33}, {"g", 34}, {"h", 35},
    {"j", 36}, {"k", 37}, {"l", 38}, {"semicolon", 39}, {"apostrophe", 40}, {"grave", 41}, {"backslash", 43},
    {"z", 44}, {"x", 45}, {"c", 46}, {"v", 47}, {"b", 48}, {"n", 49}, {"m", 50}, {"comma", 51}, {"dot", 52},
    {"slash", 53}, {"space", 57}, {"f1", 59}, {"f2", 60}, {"f3", 61}, {"f4", 62}, {"f5", 63}, {"f6", 64},
    {"f7", 65}, {"f8", 66}, {"f9", 67}, {"f10", 68}, {"f11", 87}, {"f12", 88}, {"home", 102}, {"up", 103},
    {"pageup", 104}, {"left", 105}, {"right", 106}, {"end", 107}, {"down", 108}, {"pagedown", 109},
    {"insert", 110}, {"delete", 111},
};

static struct
{
        unsigned int count;
        unsigned short modifiers[COMPOSITOR_KEYS];
        unsigned char code[COMPOSITOR_KEYS];
        char chord[COMPOSITOR_KEYS][32];
} compositor_keys;

// One word of a chord, as a modifier's flag (and *code untouched) or a key's code (and 0); ~0u for neither.
static unsigned int compositor_key_word(const char *word, unsigned int length, unsigned int *code)
{
        static const struct { const char *name; unsigned int flag; } modifiers[] = {
            {"control", WINDOW_KEY_CONTROL}, {"ctrl", WINDOW_KEY_CONTROL}, {"shift", WINDOW_KEY_SHIFT},
            {"alt", WINDOW_KEY_ALT}, {"super", WINDOW_KEY_SUPER}};

        for (unsigned int at = 0; at < sizeof(modifiers) / sizeof(modifiers[0]); at++)
                if (string_length((string_address)modifiers[at].name) == length &&
                    !memory_compare(modifiers[at].name, word, length))
                        return modifiers[at].flag;
        for (unsigned int at = 0; at < sizeof(compositor_key_names) / sizeof(compositor_key_names[0]); at++)
                if (string_length((string_address)compositor_key_names[at].name) == length &&
                    !memory_compare(compositor_key_names[at].name, word, length))
                {
                        *code = compositor_key_names[at].code;
                        return 0;
                }
        return ~0u;
}

// The chords the script bound, read once at the start: a line that is not one is left out, and said.
static void compositor_keys_read(void)
{
        static char text[4096];
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_KEYS_FILE, COMPOSITOR_SETTING_OPEN);
        bipolar got = handle < 0 ? 0 : system_read_once(handle, text, sizeof(text) - 1);
        unsigned int at = 0;

        if (handle >= 0)
                system_close(handle);
        text[got > 0 ? got : 0] = 0;

        while (text[at] && compositor_keys.count < COMPOSITOR_KEYS)
        {
                unsigned int start = at, close, modifiers = 0, code = 0, bad = 0;

                while (text[at] && text[at] != '\n')
                        at++;
                close = at;
                if (text[at])
                        at++;
                while (start < close && (text[start] == ' ' || text[start] == '\t'))
                        start++;
                while (close > start && (text[close - 1] == ' ' || text[close - 1] == '\t' || text[close - 1] == '\r'))
                        close--;
                if (close == start)
                        continue;

                // key_, then the words.
                bad = close - start < 5 || memory_compare(text + start, "key_", 4);
                for (unsigned int word = start + 4; word < close && !bad;)
                {
                        unsigned int stop = word, flag;

                        while (stop < close && text[stop] != '_')
                                stop++;
                        flag = compositor_key_word(text + word, stop - word, &code);
                        bad = flag == ~0u;
                        modifiers |= bad ? 0 : flag;
                        word = stop + 1;
                }

                // Named by the function, which is what the script's author looks for: there is no chord in it.
                if (bad || !code || close - start >= sizeof(compositor_keys.chord[0]))
                {
                        p8 name[48];
                        positive length = close - start < sizeof(name) - 1 ? close - start : sizeof(name) - 1;

                        memory_copy(name, text + start, length);
                        name[length] = 0;
                        compositor_kmsg((string_address) "compositor: no chord in the machine script's ", 0, (string_address)name);
                        continue;
                }

                memory_copy(compositor_keys.chord[compositor_keys.count], text + start, close - start);
                compositor_keys.chord[compositor_keys.count][close - start] = 0;
                compositor_keys.code[compositor_keys.count] = (unsigned char)code;
                compositor_keys.modifiers[compositor_keys.count++] = (unsigned short)modifiers;
        }
}

// A name said to the script on the FIFO, a line: a chord pressed, or a thing done to the desktop that the script
// starts a program for (tray_double, launcher_button). A script that does not listen hears nothing.
static void compositor_script_say(const char *name)
{
        // Not through a link: the FIFO is the machine script's, and a link in its place would have this write into what it names.
        bipolar handle = system_open_at(AT_FDCWD, (string_address)compositor_keys_fire, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        char line[34];
        positive length = string_length((string_address)name);

        if (handle < 0)
        {
                compositor_kmsg((string_address) "compositor: nothing reads /run/moonwater/keys.fire, error ", (positive)-handle,
                                (string_address) "");
                return;
        }
        if (length > sizeof(line) - 2)
                length = sizeof(line) - 2;
        memory_copy(line, name, length);
        line[length] = '\n';
        system_write_once(handle, line, length + 1);
        system_close(handle);
}

// A chord pressed: said to the script, which runs what it is bound to.
static void compositor_key_fire(unsigned int which)
{
        compositor_script_say(compositor_keys.chord[which]);
}

/*
        The sound server's master volume on the desktop: the keys (mute, volume down and volume up, five percent a press and a
        held key repeats), the wheel over the tray's volume cell and a middle press on it. The compositor reads the server's page
        (src/audio/state.c, mapped read-only) for the volume and for whether there is a server at all, which the tray's cell
        shows (and shows only then), and it changes the volume at once over the server's socket (control.c: SET_SINK_VOLUME and the
        mute, the sink by the name the page gives it) and as the settings do, /root/audio.volume and /root/audio.mute replaced whole,
        which keeps it across a restart and is all there is when the socket does not answer. What it shows of its own change it shows at once, as the
        number for a moment, and the page is not believed for a moment after, so the cell does not go back to the old value before
        the server has the new. A left press on the cell is the script's (volume_button): the mixer is a program, and everything on
        the desktop is started by the machine script.
*/
#define COMPOSITOR_VOLUME_STEP 5
#define COMPOSITOR_VOLUME_WAIT_MS 20           // the most the desktop waits for the server's answer: a key is not to hold the compositor
#define COMPOSITOR_VOLUME_AGAIN_NS 2000000000ull // a server that did not answer is tried again no sooner than this
#define COMPOSITOR_VOLUME_LOOK_NS 500000000ull // the page is looked at this often while there is a server,
#define COMPOSITOR_VOLUME_FIND_NS 2000000000ull // and this often for one to start
#define COMPOSITOR_VOLUME_FLASH_NS 1500000000ull
#define COMPOSITOR_VOLUME_HOLD_NS 800000000ull
#define COMPOSITOR_VOLUME_STALE_NS 4000000000ull // a page that has not moved for this long is looked for again by its name

// The page, the two files and the socket, which a check points at files of its own.
static const char *compositor_volume_path[5] = {AUDIO_STATE_PATH, SETTING_AUDIO_VOLUME_PATH, SETTING_AUDIO_MUTE_PATH, CTL_SOCKET, SETTING_AUDIO_SOURCE_MUTE_PATH};

// The connection to the server's socket (src/audio/control.c), made at the first change and kept; one that failed is tried again later.
static struct ctl compositor_ctl;
static positive compositor_ctl_again;

static struct
{
        const volatile struct audio_state *page;
        struct audio_state copy;
        positive look_at, flash_until, hold_until, seen_at, mic_flash_until;
        unsigned int seen_seq;
        int percent, muted, present;
        int recording, mic_muted; // programs that record from a microphone (not what plays), and whether the microphone is muted
} compositor_volume;

static void compositor_volume_unmap(void)
{
        if (compositor_volume.page)
                compositor_call(syscall(munmap), (positive)compositor_volume.page, 4096ul * ((sizeof(struct audio_state) + 4095) / 4096), 0, 0, 0, 0);
        compositor_volume.page = null;
}

static int compositor_volume_map(void)
{
        long fd, size, got;

        if (compositor_volume.page)
                return 1;
        fd = compositor_call(syscall(openat), (positive)AT_FDCWD, (positive)compositor_volume_path[0], COMPOSITOR_SETTING_OPEN, 0, 0, 0);
        if (fd < 0)
                return 0;
        size = compositor_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0);
        got = size < (long)sizeof(struct audio_state) ? -1
              : compositor_call(syscall(mmap), 0, 4096ul * ((sizeof(struct audio_state) + 4095) / 4096), 1, 1, (positive)fd, 0);
        compositor_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (got < 0 && got > -4096)
                return 0;
        compositor_volume.page = (const volatile struct audio_state *)got;
        return 1;
}

/*
        The volume cell pressed: the popup is started by the machine script (volume_button), which starts the mixer in its popup form
        (-q -a) beside the cell, as the launcher is beside its button; the table holds the cell's rectangle, the edge its band is along
        and its screen before the script is told. A popup that is up is put away by this very press (WINDOW_DISMISS), and the cell then
        asks for none, so the cell opens it and closes it.
*/
// The first window showing whose name is word (the table's name, whole), or COMPOSITOR_NONE.
static int compositor_row_named(const char *word)
{
        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                const volatile char *name = compositor.table->name[row];
                unsigned int at = 0;

                if (!compositor.showing[row])
                        continue;
                while (word[at] && name[at] == word[at])
                        at++;
                if (!word[at] && !name[at])
                        return (int)row;
        }
        return COMPOSITOR_NONE;
}

static int compositor_quick_row(void)
{
        return compositor_row_named("mixer-quick");
}

// The table holds the rectangle of the band's cell at a point (which is the hit taskbar_hit_reach names, anchored by anchor),
// its edge and its screen, for the script to start the popup beside it. Nothing when no cell of that kind is there.
static void compositor_button_rect(int x, int y, int which, int (*anchor)(const struct taskbar_view *, int *, int *, int *, int *))
{
        struct windows *table = compositor.table;
        int reach = compositor.touching ? UI_TARGET * compositor.scale / 100 : 0;

        for (compositor.band_at = 0; table && compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();
                int rect[4];

                if (taskbar_hit_reach(&view, x, y, reach) != which || !anchor(&view, &rect[0], &rect[1], &rect[2], &rect[3]))
                        continue;
                table->button_x = rect[0], table->button_y = rect[1];
                table->button_w = (unsigned int)rect[2], table->button_h = (unsigned int)rect[3];
                table->button_edge = view.position, table->button_screen = (unsigned int)compositor.band_at;
                break;
        }
        compositor.band_at = 0;
}

static void compositor_volume_open(int x, int y)
{
        if (compositor_quick_row() >= 0)
                return;
        compositor_button_rect(x, y, TASKBAR_VOLUME, taskbar_volume_anchor);
        compositor_script_say("volume_button");
}

// The microphone cell pressed: the same popup form of the mixer (mic_button, which adds -i) with the recording strips in it, beside the cell.
static void compositor_mic_open(int x, int y)
{
        if (compositor_quick_row() >= 0)
                return;
        compositor_button_rect(x, y, TASKBAR_MIC, taskbar_mic_anchor);
        compositor_script_say("mic_button");
}

// What the tray's cells show, told to the band; the band is drawn again when one changed. The microphone's is there while a program records
// from a microphone, and for a moment after its mute was thrown (to see it thrown).
static void compositor_volume_show(void)
{
        int changed = taskbar_volume_set(compositor_volume.present, compositor_volume.percent, compositor_volume.muted, compositor_volume.flash_until != 0);

        changed |= taskbar_mic_set(compositor_volume.present && (compositor_volume.recording || compositor_volume.mic_flash_until), compositor_volume.mic_muted);
        if (changed)
                compositor_tabs_changed();
}

// The page looked at: whether there is a server, and the volume and the mute it says (not for a moment after our own change).
static void compositor_volume_look(positive now)
{
        struct audio_state *copy = &compositor_volume.copy;

        if (compositor_volume.flash_until && now >= compositor_volume.flash_until)
                compositor_volume.flash_until = 0;
        if (compositor_volume.mic_flash_until && now >= compositor_volume.mic_flash_until)
                compositor_volume.mic_flash_until = 0;
        if (now >= compositor_volume.hold_until)
        {
                if (!compositor_volume_map() || !audio_state_read(compositor_volume.page, copy))
                        compositor_volume.present = 0, compositor_volume.recording = 0;
                else
                {
                        unsigned long percent = ((unsigned long)copy->volume * 100 + 32768) / 65536;

                        compositor_volume.present = 1;
                        compositor_volume.percent = percent > 100 ? 100 : (int)percent;
                        compositor_volume.muted = copy->muted != 0;
                        // A program that records from a microphone: a record stream that is not paused and does not record what plays, while recording is allowed.
                        compositor_volume.recording = 0;
                        for (unsigned int k = 0; k < AUDIO_STATE_RECORDS; k++)
                                compositor_volume.recording += copy->capture && copy->record[k].index != 0xffffffffu && !(copy->record[k].flags & (AUDIO_STATE_MONITOR | AUDIO_STATE_CORKED)) &&
                                                               (copy->record[k].flags & AUDIO_STATE_PLAYING);
                        compositor_volume.mic_muted = copy->input_muted != 0;
                        if (copy->seq != compositor_volume.seen_seq)
                                compositor_volume.seen_seq = copy->seq, compositor_volume.seen_at = now;
                        // A server that was started again has a page of its own: this one is let go when it stands still.
                        if (now - compositor_volume.seen_at > COMPOSITOR_VOLUME_STALE_NS)
                        {
                                compositor_volume_unmap();
                                compositor_volume.seen_at = now;
                        }
                }
        }
        compositor_volume.look_at = now + (compositor_volume.present ? COMPOSITOR_VOLUME_LOOK_NS : COMPOSITOR_VOLUME_FIND_NS);
        if (compositor_volume.flash_until && compositor_volume.flash_until < compositor_volume.look_at)
                compositor_volume.look_at = compositor_volume.flash_until;
        if (compositor_volume.mic_flash_until && compositor_volume.mic_flash_until < compositor_volume.look_at)
                compositor_volume.look_at = compositor_volume.mic_flash_until;
        compositor_volume_show();
}

// A small file of /root replaced whole: written beside it and renamed over it.
static void compositor_volume_write(const char *path, const char *text)
{
        char stage[48];
        positive length = string_length((string_address)path), size = string_length((string_address)text);
        bipolar handle, written;

        if (length + sizeof(".new") > sizeof(stage))
                return;
        memory_copy(stage, path, length);
        memory_copy(stage + length, ".new", sizeof(".new"));
        handle = system_open_at_mode(AT_FDCWD, (string_address)stage, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (handle < 0)
                return;
        written = system_write_once(handle, text, size);
        system_close(handle);
        if (written != (bipolar)size || system_rename_at(AT_FDCWD, (string_address)stage, AT_FDCWD, (string_address)path, 0) < 0)
                system_remove_at(AT_FDCWD, (string_address)stage, 0);
}

/*
        The volume and the mute told to the server over its socket (the sink by the name the page gives it), so a key is heard at once and
        not at the server's next look at the files. volume is a percent or -1 for none, mute is 0, 1 or -1 for none. The wait for the
        server's answer is short (COMPOSITOR_VOLUME_WAIT_MS), and a server that does not answer is let go and tried again in two seconds;
        the files are written all the same, so the person's volume is kept whatever the socket did.
*/
// The connection to the server's socket, made at the first change and kept: false when there is none (and none is tried again for two seconds).
static int compositor_ctl_ready(positive now)
{
        compositor_ctl.timeout_ms = COMPOSITOR_VOLUME_WAIT_MS;
        if (compositor_ctl.open)
                return 1;
        if (now < compositor_ctl_again)
                return 0;
        if (ctl_connect(&compositor_ctl, compositor_volume_path[3]) < 0)
        {
                compositor_ctl_again = now + COMPOSITOR_VOLUME_AGAIN_NS;
                return 0;
        }
        return 1;
}

// What the server answered: a connection that failed is let go and tried again later.
static void compositor_ctl_answered(long answer, positive now)
{
        if (answer == CTL_FAILED)
        {
                ctl_disconnect(&compositor_ctl);
                compositor_ctl_again = now + COMPOSITOR_VOLUME_AGAIN_NS;
        }
}

static void compositor_volume_tell(int volume, int mute)
{
        positive now = clock_monotonic_nanoseconds();
        const char *sink = compositor_volume.copy.name;
        long answer = 0;

        if (!compositor_volume.present || !sink[0])
                return;
        if (!compositor_ctl_ready(now))
                return;
        if (volume >= 0)
                answer = ctl_set_sink_volume(&compositor_ctl, sink, (unsigned long)volume * 65536 / 100);
        if (answer >= 0 && mute >= 0)
                answer = ctl_set_sink_mute(&compositor_ctl, sink, mute);
        compositor_ctl_answered(answer, now);
}

/*
        A change of the master volume: step percent more (or less) and unmuted, or with toggle the mute thrown and the volume as it
        is. Nothing without a server: the keys have nothing to turn. The number is shown for a moment in the tray.
*/
static void compositor_volume_step(int step, int toggle)
{
        positive now = clock_monotonic_nanoseconds();
        char text[8];
        int percent, muted;

        compositor_volume_look(now); // inside a moment of our own change it is our own number that is stepped, so a held key adds up
        if (!compositor_volume.present)
                return;
        percent = compositor_volume.percent, muted = compositor_volume.muted;
        if (toggle)
                muted = !muted;
        else
        {
                percent += step;
                percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
                muted = 0;
        }
        compositor_volume_tell(percent != compositor_volume.percent ? percent : -1, muted != compositor_volume.muted ? muted : -1);
        if (percent != compositor_volume.percent)
        {
                int at = 0;

                if (percent >= 100)
                        text[at++] = '1', text[at++] = '0', text[at++] = '0';
                else
                {
                        if (percent >= 10)
                                text[at++] = (char)('0' + percent / 10);
                        text[at++] = (char)('0' + percent % 10);
                }
                text[at++] = '\n', text[at] = 0;
                compositor_volume_write(compositor_volume_path[1], text);
        }
        if (muted != compositor_volume.muted)
                compositor_volume_write(compositor_volume_path[2], muted ? "on\n" : "off\n");
        compositor_volume.percent = percent, compositor_volume.muted = muted;
        compositor_volume.flash_until = now + COMPOSITOR_VOLUME_FLASH_NS;
        compositor_volume.hold_until = now + COMPOSITOR_VOLUME_HOLD_NS;
        compositor_volume.look_at = compositor_volume.flash_until;
        compositor_volume_show();
}

/*
        The microphone's mute thrown (the microphone key, a middle press on its cell): told to the server over its socket by the source's
        name as the volume's is, and kept in /root/audio.source.mute for the next boot. The cell is shown for a moment, struck or not,
        whether or not a program records, to see it thrown. Nothing without a server or a microphone.
*/
static void compositor_mic_step(void)
{
        positive now = clock_monotonic_nanoseconds();
        const char *source = compositor_volume.copy.input_name;
        int muted;

        compositor_volume_look(now);
        if (!compositor_volume.present || !source[0])
                return;
        muted = !compositor_volume.mic_muted;
        if (compositor_ctl_ready(now))
                compositor_ctl_answered(ctl_set_source_mute(&compositor_ctl, source, muted), now);
        compositor_volume_write(compositor_volume_path[4], muted ? "on\n" : "off\n");
        compositor_volume.mic_muted = muted;
        compositor_volume.mic_flash_until = now + COMPOSITOR_VOLUME_FLASH_NS;
        compositor_volume.hold_until = now + COMPOSITOR_VOLUME_HOLD_NS;
        compositor_volume.look_at = compositor_volume.mic_flash_until;
        compositor_volume_show();
}

// A volume key: five percent a press, the mute thrown by its own key; a held key repeats the steps and not the mute. The microphone's key is a mute alone.
static void compositor_volume_key(unsigned int code, int value)
{
        if (code == CK_KEY_MICMUTE)
        {
                if (value == 1)
                        compositor_mic_step();
        }
        else if (code == CK_KEY_MUTE)
        {
                if (value == 1)
                        compositor_volume_step(0, 1);
        }
        else
                compositor_volume_step(code == CK_KEY_VOLUME_UP ? COMPOSITOR_VOLUME_STEP : -COMPOSITOR_VOLUME_STEP, 0);
}

/*
        The tray, the clock and the battery, pressed twice quickly: the settings. A window of that name that is
        up is brought up and given the keys; with none, the script is told (tray_double) and starts it, since
        everything on the desktop is started by the machine script and by nothing else.
*/
static void compositor_settings_open(void)
{
        int row = compositor_row_named("settings");

        if (row >= 0)
        {
                compositor_reveal(row);
                return;
        }
        compositor.settings_asked++;
        compositor_script_say("tray_double");
}

/*
        The launcher button pressed with no launcher up: the script is told (launcher_button) and starts the launcher, since
        everything on the desktop is started by the machine script and by nothing else. Before it says so the table holds the
        rectangle of the button that was pressed, the edge its band is along and its screen, which the launcher opens beside
        (window.c's button_x and the rest); the band is the one whose button the point is on.
*/
static void compositor_launcher_open(int x, int y)
{
        compositor_button_rect(x, y, TASKBAR_LAUNCHER, taskbar_anchor);
        compositor.launcher_asked++;
        compositor_script_say("launcher_button");
}

// What is held on every keyboard, as the flags a key event carries.
static unsigned int compositor_modifiers(void)
{
        unsigned int held = 0;

        for (unsigned int at = 0; at < compositor.input_count; at++)
                held |= compositor.held[at];
        return compositor_held_flags(held);
}

/*
        A key from the keyboard on input number at.

        Some are the compositor's and go no further, as the kernel desktop's
        were: Alt and AltGr themselves (a window that was sent Alt going down
        and then, with the keys moved on by Alt-Tab, Alt going up would hold a
        modifier that never was), Alt-Tab and Alt-F9 with both edges of the
        key and whatever repeats, any other key typed while an Alt-Tab is under
        way, the chords of the script's key_ functions, and Control-Alt-Backspace and the same with Shift,
        which Canvas hears for itself and acts on. The rest make characters, one event each, for the window that has
        the keys; with none, a key gives them to the topmost.
*/
/*
        The tiling chords, built in: Super with an arrow walks the window with the keys through the arrangements a drag's
        snap makes (compositor_arrange, compositor_screen_part), so a window tiled by a key and one let go at an edge are the
        same rectangle. One step a press, and the same step from the same place every time:

                floating        Left, Right: that half      Up: fill            Down: nothing
                a half          Up: its top quarter         Down: its bottom quarter
                                toward the other side: back to the rectangle it had (the chord that put it there, undone)
                a top quarter   Down: its half              Up: fill            toward the other side: the other top quarter
                a low quarter   Up: its half                toward the other side: the other low quarter
                filled          Down: back to its rectangle                     Left, Right: that half

        and any other press is nothing: Down never puts a window away, and Left or Right never takes it off its screen
        (Super and Shift with Left or Right does that, compositor_screen_chord). A chord the machine script binds never gets
        here (compositor_key runs the script's first), so a user's binding of the same keys takes them.
*/
static unsigned int compositor_chord_next(unsigned int how, unsigned int code)
{
        int filled = how == COMPOSITOR_MAXIMIZED || how == COMPOSITOR_ROOM;

        if (code == CK_KEY_LEFT || code == CK_KEY_RIGHT)
        {
                int left = code == CK_KEY_LEFT;

                if (how == COMPOSITOR_FLOATING || filled)
                        return left ? COMPOSITOR_LEFT : COMPOSITOR_RIGHT;
                if (how == (left ? COMPOSITOR_RIGHT : COMPOSITOR_LEFT))
                        return COMPOSITOR_FLOATING;
                if (how == (left ? COMPOSITOR_TOP_RIGHT : COMPOSITOR_TOP_LEFT))
                        return left ? COMPOSITOR_TOP_LEFT : COMPOSITOR_TOP_RIGHT;
                if (how == COMPOSITOR_BOTTOM || how == (left ? COMPOSITOR_BOTTOM_RIGHT : COMPOSITOR_BOTTOM_LEFT))
                        return left ? COMPOSITOR_BOTTOM_LEFT : COMPOSITOR_BOTTOM_RIGHT;
                return how;
        }
        if (code == CK_KEY_UP)
                switch (how)
                {
                case COMPOSITOR_FLOATING: case COMPOSITOR_TOP_LEFT: case COMPOSITOR_TOP_RIGHT: case COMPOSITOR_BOTTOM:
                        return COMPOSITOR_MAXIMIZED;
                case COMPOSITOR_LEFT: return COMPOSITOR_TOP_LEFT;
                case COMPOSITOR_RIGHT: return COMPOSITOR_TOP_RIGHT;
                case COMPOSITOR_BOTTOM_LEFT: return COMPOSITOR_LEFT;
                case COMPOSITOR_BOTTOM_RIGHT: return COMPOSITOR_RIGHT;
                default: return how;
                }
        if (code == CK_KEY_DOWN)
                switch (how)
                {
                case COMPOSITOR_MAXIMIZED: case COMPOSITOR_ROOM: return COMPOSITOR_FLOATING;
                case COMPOSITOR_LEFT: return COMPOSITOR_BOTTOM_LEFT;
                case COMPOSITOR_RIGHT: return COMPOSITOR_BOTTOM_RIGHT;
                case COMPOSITOR_TOP_LEFT: return COMPOSITOR_LEFT;
                case COMPOSITOR_TOP_RIGHT: return COMPOSITOR_RIGHT;
                default: return how;
                }
        return how;
}

static void compositor_tile_chord(unsigned int code)
{
        int row = compositor.focus;
        unsigned int how;

        if (row < 0 || !compositor_visible(row) || compositor.cycle)
                return;
        how = compositor_chord_next(compositor.arranged[row], code);
        if (how == compositor.arranged[row])
                return;
        if (how == COMPOSITOR_FLOATING)
                compositor_float(row);
        else
                compositor_arrange(row, how);
}

/*
        Super and Shift with Left or Right: the window with the keys to the next screen that way -- the nearest whose left
        edge is at or past this one's right (or whose right is at or before its left), the one most level with it of two as
        near -- keeping what it fills there, a floating one its share of the room as a screen's carry does (and the
        rectangle a restore puts back goes with it). There is no wrap at the last screen. Its scale is the new screen's.
*/
static void compositor_screen_chord(unsigned int code)
{
        int row = compositor.focus, from, to = -1, right = code == CK_KEY_RIGHT;
        long long best = -1, level = 0;
        unsigned int count, how;
        const int (*screens)[4] = compositor_screens(&count);

        if (row < 0 || !compositor_visible(row) || compositor.cycle || count < 2)
                return;
        from = compositor_screen_of(row);
        for (unsigned int at = 0; at < count; at++)
        {
                const int *s = screens[at], *f = screens[from];
                long long gap = right ? (long long)s[0] - (f[0] + f[2]) : (long long)f[0] - (s[0] + s[2]);
                long long off = ((long long)s[1] * 2 + s[3]) - ((long long)f[1] * 2 + f[3]);

                off = off < 0 ? -off : off;
                if ((int)at == from || gap < 0)
                        continue;
                if (best < 0 || gap < best || (gap == best && off < level))
                        best = gap, level = off, to = (int)at;
        }
        if (to < 0)
                return;
        how = compositor.arranged[row];
        {
                int keep_x = compositor.saved_x[row], keep_y = compositor.saved_y[row];

                compositor_screen_carry(screens[from], screens[to], &keep_x, &keep_y, (int)compositor.saved_w[row], (int)compositor.saved_h[row]);
                compositor.saved_x[row] = keep_x, compositor.saved_y[row] = keep_y;
        }
        if (how == COMPOSITOR_FLOATING)
        {
                int rect[4] = {compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]};

                compositor_screen_carry(screens[from], screens[to], &rect[0], &rect[1], rect[2], rect[3]);
                compositor_set_rect(row, COMPOSITOR_FLOATING, rect);
                return;
        }
        compositor_arrange_on(row, how == COMPOSITOR_ROOM ? COMPOSITOR_MAXIMIZED : how, to);
}

#ifdef CONFIG_MOONWATER_WAYLAND
// A key a client's window reads as it is, from the keymap it was sent (wayland_host.c): true when the key was the client's.
static int compositor_wl_key(unsigned int code, int value);
static int compositor_wl_hold_counts(int dx, int dy, positive now);
static void compositor_wl_hold_keep(void);
#endif

static void compositor_key(unsigned int at, unsigned int code, int value, positive reported)
{
        unsigned int bit = compositor_held_bit(code), characters[2] = {0, 0}, flags, modifiers, chord;
        int count, target;

        // Escape while the hand moves or resizes a window puts it back as it was at the press, and is no window's key.
        if (code == CK_KEY_ESC && value && (compositor.drag >= 0 || compositor.resize >= 0))
        {
                if (value == 1)
                        compositor_hold_cancel();
                return;
        }

        // The snap assist offered after a drop goes at any key that is not a modifier: Escape is only that, and any other key
        // goes on to the window that has the keys, as it would have.
        if (compositor.assist_armed && value == 1 && !bit)
        {
                compositor_assist_close();
                if (code == CK_KEY_ESC)
                        return;
        }

        if (bit)
        {
                if (value)
                        compositor.held[at] |= (unsigned char)bit;
                else
                        compositor.held[at] &= (unsigned char)~bit;
                // A modifier is part of what a drag snaps to (Control is no snap): its target is read again now, not at the
                // next motion.
                if (compositor.drag >= 0)
                        compositor_preview(compositor.drag);
        }

        modifiers = compositor_modifiers();
        chord = modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT);

        if (bit & (CK_HELD_ALT | CK_HELD_ALTGR))
        {
                if (!(modifiers & WINDOW_KEY_ALT) && compositor.cycle)
                        compositor_focus_commit();
#ifdef CONFIG_MOONWATER_WAYLAND
                // A client's window reads Alt and AltGr itself: they are modifiers of its keymap.
                compositor_wl_key(code, value);
#endif
                return;
        }

        if (chord == WINDOW_KEY_ALT && code == CK_KEY_TAB)
        {
                if (value)
                        compositor_focus_step();
                return;
        }

        if (chord == WINDOW_KEY_ALT && code == CK_KEY_F4 && !compositor.cycle)
        {
                if (value == 1)
                        compositor_close(compositor.focus);
                return;
        }

        if (chord == WINDOW_KEY_ALT && code == CK_KEY_F9 && !compositor.cycle)
        {
                if (value == 1)
                        compositor_minimize_focused();
                return;
        }

        // The volume keys are the desktop's, whichever window has the keys.
        if (!(modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) &&
            (code == CK_KEY_MUTE || code == CK_KEY_VOLUME_DOWN || code == CK_KEY_VOLUME_UP || code == CK_KEY_MICMUTE))
        {
                if (value)
                        compositor_volume_key(code, value);
                return;
        }

        // A chord the script bound is its own and no window's; a press says so, and not a repeat, so a key held
        // down does not fill the desktop with what it starts.
        for (unsigned int at = 0; at < compositor_keys.count; at++)
                if (code == compositor_keys.code[at] &&
                    (modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) ==
                        compositor_keys.modifiers[at])
                {
                        if (value == 1)
                                compositor_key_fire(at);
                        return;
                }

        // Super alone with an arrow: the tiling chords, which the script's bindings above have had first say; and Super and
        // Shift with Left or Right, a window to the next screen.
        if ((modifiers & (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) == WINDOW_KEY_SUPER &&
            (code == CK_KEY_LEFT || code == CK_KEY_RIGHT || code == CK_KEY_UP || code == CK_KEY_DOWN))
        {
                if (value == 1)
                        compositor_tile_chord(code);
                return;
        }
        if ((modifiers & (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) == (WINDOW_KEY_SUPER | WINDOW_KEY_SHIFT) &&
            (code == CK_KEY_LEFT || code == CK_KEY_RIGHT))
        {
                if (value == 1)
                        compositor_screen_chord(code);
                return;
        }

        if (((chord & ~WINDOW_KEY_SHIFT) == (WINDOW_KEY_ALT | WINDOW_KEY_CONTROL) && code == CK_KEY_BACKSPACE) ||
            ((modifiers & WINDOW_KEY_ALT) && compositor.cycle))
                return;

#ifdef CONFIG_MOONWATER_WAYLAND
        // A client's window is given the key as it is, with no characters made of it here.
        if (compositor_wl_key(code, value))
                return;
#endif
        count = compositor_keymap_event(&compositor.keymap, code, value, modifiers, characters);
        if (count < 0)
                return;

        if (compositor.focus < 0)
                compositor_refocus();
        if (compositor.focus < 0)
                return;

        // A press goes to the window that has the keys and is held there; a repeat or a
        // release of a key whose press went elsewhere (or nowhere) is nobody's to hear.
        // While the window has a popup open, the topmost popup is where its keys go.
        target = compositor_popup_top(compositor.focus);
        if (code < 128)
        {
                unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[code >> 3];

                if (value == 1)
                {
                        if (compositor.keys_to != target)
                                compositor_keys_release();
                        *down |= mask;
                        compositor.keys_to = target;
                }
                else if (!(*down & mask))
                        return;
                else if (!value)
                        *down &= (unsigned char)~mask;
        }

        flags = modifiers | (value ? WINDOW_KEY_DOWN : 0) | (value == 2 ? WINDOW_KEY_REPEAT : 0);
        for (int each = 0; each < (count ? count : 1); each++)
                compositor_post(target, WINDOW_EVENT_KEY, flags, (int)code, (int)characters[each]);
        if (value == 1)
                compositor_latency(2, reported, clock_monotonic_nanoseconds());
}

/*
        The pointer stays on a screen, wherever the motions of a burst took it
        (compositor_screen_keep_point): where they end off every screen it is
        kept on the screen it was on, and a tablet's point, which comes from
        nowhere, goes to the nearest screen.
*/
static void compositor_pointer_clamp(void)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int nowhere = -2147483647 - 1;

        compositor_pointer_sync();
        compositor_screen_keep_point(screens, count, compositor.absolute ? nowhere : compositor.kept_x,
                                     compositor.absolute ? nowhere : compositor.kept_y, &compositor.real_x,
                                     &compositor.real_y);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor.absolute = 0;
        compositor_pointer_shown();
}

/*
        The pointer is the real place led to where it will be (the lead of the class that owns it, see predict), and held on a
        screen. A press is handled at the real place, so while it is (at_real) there is no lead.
*/
// A pointer set from outside the prediction (a warp, a screen's change, a caller that writes the pointer) is the real place,
// and the lead it had is over. Done before the hand moves again, so a report counts from where the pointer was put.
static void compositor_pointer_sync(void)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];

        if (compositor.pointer_x != compositor.shown_x || compositor.pointer_y != compositor.shown_y)
        {
                compositor.real_x = compositor.pointer_x, compositor.real_y = compositor.pointer_y;
                compositor.accel_x = compositor.accel_y = 0;
                track->lead_x = track->lead_y = 0;
        }
}

static void compositor_pointer_shown(void)
{
        struct compositor_predict *track = &compositor.predict[compositor.owner];
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        long long lead_x, lead_y;

        lead_x = compositor.at_real ? 0 : compositor_predict_px(track->lead_x);
        lead_y = compositor.at_real ? 0 : compositor_predict_px(track->lead_y);
        compositor.pointer_x = (int)(compositor.real_x + lead_x);
        compositor.pointer_y = (int)(compositor.real_y + lead_y);
        compositor_screen_keep_point(screens, count, compositor.real_x, compositor.real_y, &compositor.pointer_x,
                                     &compositor.pointer_y);
        compositor.shown_x = compositor.pointer_x, compositor.shown_y = compositor.pointer_y;
}

// Whether a movement of the hand is a shake that finds the pointer: `moonwater canvas cursor shake off` says none is.
static int compositor_shaken(int delta, unsigned long long now)
{
        return compositor_shake(&compositor.shake, delta, now) && !compositor.cursor.shake_off;
}

// The pointer was shaken: it is large until its time is up, which is COMPOSITOR_MAGNIFIED_NS or what `moonwater canvas cursor shake delay` says, from this last shake.
static void compositor_magnify(positive now)
{
        compositor.cursor.magnify = 0;
        compositor.cursor.magnified = 1;
        compositor.cursor.magnified_until = now + (compositor.cursor.shake_ns ? compositor.cursor.shake_ns : COMPOSITOR_MAGNIFIED_NS);
}

// The pointer, to a window: where it is in the window's picture.
// Told only when the place went in the ring: one a full ring refused is not taken as told, or the wheel after the
// program has caught up (compositor_wheel) would turn at the place it had before.
static void compositor_pointer_to(int row)
{
        int x = compositor.pointer_x - compositor.x[row], y = compositor.pointer_y - compositor.y[row];

        compositor.told_row = compositor_post(row, WINDOW_EVENT_MOTION, 0, x, y) ? row : COMPOSITOR_NONE;
        compositor.told_x = x;
        compositor.told_y = y;
}

static void compositor_motion(void)
{
        int under, tab;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);

        // The shape of what the hand has hold of or is over: the edges it could take.
        compositor.cursor.shape = compositor.resize >= 0 ? compositor_cursor_by_edges[compositor.edges[compositor.resize] & 15]
                                  : compositor.drag >= 0 || under < 0
                                      ? CP_ARROW
                                      : compositor_cursor_by_edges[compositor_edge_zone(under, compositor.pointer_x,
                                                                                         compositor.pointer_y) & 15];
        if (compositor.cursor.magnify)
                compositor_magnify(clock_monotonic_nanoseconds());
        compositor_cursor_move();

        // On the plane the pointer is on the screen when the call returns; drawn, when the frame has gone.
        if (compositor.motion_ts && !compositor_cursor_in_scene())
        {
                compositor_latency(0, compositor.motion_ts, clock_monotonic_nanoseconds());
                compositor.motion_ts = 0;
        }

        // The edge the hand holds, all of a window it moves, or for a pointer with no button down the edge it is over.
        compositor_lit(compositor.resize >= 0  ? compositor.resize
                       : compositor.drag >= 0 ? compositor.drag
                                              : under,
                       compositor.resize >= 0 ? compositor.edges[compositor.resize]
                       : compositor.drag >= 0 ? COMPOSITOR_LIT_ALL
                       : under < 0 || compositor.grab >= 0 || compositor.touching
                           ? 0
                           : compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y),
                       compositor.resize >= 0 || compositor.drag >= 0);

        if (compositor.resize >= 0)
        {
                compositor_resize_move();
                return;
        }

        if (compositor.drag >= 0)
        {
                int row = compositor.drag;

                // Taken by the hand and moved: a window that fills a place is the window it was, under the hand, once the
                // hand is UI_SLOP from where it pressed (in its own scale). Short of that it is still a press -- a click, or
                // half a double click with a shaking hand -- and nothing moves.
                if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                {
                        if (!ui_touch_past(compositor.press_x, compositor.press_y, compositor.pointer_x, compositor.pointer_y,
                                           ui_touch_slop(compositor_row_scale(row))))
                                return;
                        compositor_float_for_drag(row);
                }

                // Kept on the output as it is moved, as when it was placed.
                compositor.drag_moved = 1;
                compositor_damage_row(row);
                compositor_place(row, compositor.pointer_x - compositor.drag_x, compositor.pointer_y - compositor.drag_y);
                compositor_damage_row(row);
                compositor_preview(row);
                return;
        }

        // A held button keeps the pointer with the window it went down on. Otherwise a window is told of the motion
        // over it only when it wants the motion (windows_hover): each report of it wakes the program.
        compositor_shared_hover(under);
        compositor.hover = under;
        if (compositor.grab >= 0)
                compositor_pointer_to(compositor.grab);
        // A finger that was held for a long press has no button down (its press was taken back), and what it selects follows it as
        // a held button's drag does, to the window it landed on, whether or not that window asked for the pointer's motion.
        else if (compositor.touch.gesture.mode == UI_TOUCH_HELD && compositor.touch.hold_row >= 0)
                compositor_pointer_to(compositor.touch.hold_row);
        else if (under >= 0 && !__atomic_load_n(&compositor.table->hover_off[under], __ATOMIC_ACQUIRE))
                compositor_pointer_to(under);
}

/*
        Where a drag ended, read as an arrangement: the window's own rectangle, on the screen it is mostly on, and the edges of
        that screen it touches (compositor_snap_rule). The pointer is not read: the hand moves the window and the window is
        what snaps. The edges touched last frame are the dead zone's (compositor.snap_held). Control held is no snap at all, so a
        window can be set flush against an edge; let go of it and the edges count again.
*/
static unsigned int compositor_snap(const int *box, unsigned int *touch, int *screen)
{
        unsigned int count, free;
        const int (*screens)[4] = compositor_screens(&count);
        int at = compositor_screen_owner(screens, count, box[0], box[1], box[2], box[3]);

        *touch = 0;
        *screen = at;
        if (at < 0 || compositor_screen_cover(screens[at], box[0], box[1], box[2], box[3]) <= 0 ||
            (compositor_modifiers() & WINDOW_KEY_CONTROL))
                return COMPOSITOR_FLOATING;
        free = compositor_snap_free(screens, count, box, at);
        return compositor_snap_rule(box, screens[at], free, compositor.snap_held, COMPOSITOR_SNAP_EDGE * compositor_output_scale(at) / 100,
                                    COMPOSITOR_SNAP_CORNER * compositor_output_scale(at) / 100, touch);
}

// The rectangle a window dragged is where the hand has it, before placement keeps it on its screen (compositor_place
// clamps it to the screen, so a window held at an edge by the clamp is not one pushed there): a snap reads its edges
// from it (compositor_snap). A finger or a pen is led, so its pointer is where it is going, and a frame with no new
// place takes the lead away again (compositor_predict_clear). The snap reads the place the hand is at, which is the
// same between places as at them; the pointer's lead would flip a target between the two, every report.
static void compositor_box_of(int row, int *box)
{
        int x = compositor.pointer_x, y = compositor.pointer_y;

        if (compositor.owner != COMPOSITOR_PREDICT_MOUSE)
                x = compositor.real_x, y = compositor.real_y;
        box[0] = x - compositor.drag_x, box[1] = y - compositor.drag_y;
        box[2] = (int)compositor.w[row], box[3] = (int)compositor.h[row];
}

/*
        The snap preview: the target the window being moved touches (compositor_target), from the frame it is touched in, and
        damaged where it was and where it is. Its target is there while the contact is, and gone the moment it is lost or the
        window is let go; what is drawn of it is there at once, moves to a new target and fades out when there is none
        (compositor_preview_shown). Once the window has moved only.
*/
static void compositor_preview(int row)
{
        unsigned int count, how = COMPOSITOR_FLOATING, touch = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = 0, rect[4] = {0, 0, 0, 0}, box[4];

        if (row >= 0 && row == compositor.drag && compositor.drag_moved)
        {
                compositor_box_of(row, box);
                how = compositor_target(row, compositor.pointer_x, compositor.pointer_y, box, &screen, &touch);
                if (how != COMPOSITOR_FLOATING)
                        compositor_screen_part(screens[screen], how, rect);
                compositor_overlays_drag(row, compositor.pointer_x, compositor.pointer_y, how, screen);
        }
        else if (row < 0 && compositor.grid_on)
        {
                compositor_overlays_damage();
                compositor.grid_on = 0;
        }
        if (row < 0)
                compositor.grid_since = 0;
        compositor.snap_held = touch;
        if (how == COMPOSITOR_FLOATING)
        {
                if (compositor.preview[2] > 0)
                {
                        compositor_damage(compositor.preview[0], compositor.preview[1], compositor.preview[2], compositor.preview[3]);
                        compositor_preview_leave();
                }
                memory_zero(compositor.preview, sizeof(compositor.preview));
                compositor.preview_row = COMPOSITOR_NONE;
                return;
        }
        if (row == compositor.preview_row && !memory_compare(rect, compositor.preview, sizeof(rect)))
                return;
        if (compositor.preview[2] > 0)
                compositor_damage(compositor.preview[0], compositor.preview[1], compositor.preview[2], compositor.preview[3]);
        compositor_preview_enter(row, rect, how);
        memory_copy(compositor.preview, rect, sizeof(rect));
        compositor.preview_row = row;
        compositor_damage(rect[0], rect[1], rect[2], rect[3]);
}

// The button is let go. A window that was being moved is arranged by where the
// hand let go, unless the release is not the hand's (the device went away).
static void compositor_lit(int row, unsigned int edges, unsigned int held);

/*
        The window is taken hold of to be moved, wherever the pointer is on it now: its popups go, the hand's place on it
        is kept, and what a drop on a target would restore. The press on its top does this, and so does a client's own
        request to be moved while the person holds a button on it (the Wayland shim's xdg_toplevel.move).
*/
static void compositor_drag_begin(int under)
{
        compositor_popups_close(under, -1);
        compositor.drag = under;
        compositor.drag_moved = 0;
        compositor.drag_x = compositor.pointer_x - compositor.x[under];
        compositor.drag_y = compositor.pointer_y - compositor.y[under];
        compositor.press_x = compositor.pointer_x;
        compositor.press_y = compositor.pointer_y;
        compositor_hold_take(0, under);
        // What a drop on a target keeps for a restore: where it floats now, or where it floated before.
        if (compositor.arranged[under] == COMPOSITOR_FLOATING)
        {
                int keep[4] = {compositor.x[under], compositor.y[under], (int)compositor.w[under], (int)compositor.h[under]};

                memory_copy(compositor.drag_keep, keep, sizeof(keep));
        }
        else
        {
                int keep[4] = {compositor.saved_x[under], compositor.saved_y[under], (int)compositor.saved_w[under],
                               (int)compositor.saved_h[under]};

                memory_copy(compositor.drag_keep, keep, sizeof(keep));
        }
        // The window is in the hand: all of its line is lit until it is let go.
        compositor_lit(under, COMPOSITOR_LIT_ALL, 1);
}

// A release with flags: WINDOW_KEY_CANCEL is one that is no click (a finger that became two, or was held).
static void compositor_release_as(unsigned int code, int snap, unsigned int flags)
{
        int grab = compositor.grab, dragged = compositor.drag, to = grab, resized = compositor.resize;
        unsigned int index = code - COMPOSITOR_BTN_LEFT, drop_how = COMPOSITOR_FLOATING;
        int drop_screen = 0, drop_box[4];
        unsigned int drop_touch = 0;

        // Where the window lands is read before the preview and the grid are taken away: the grid's region, or the snap.
        if (dragged >= 0 && snap && compositor.drag_moved)
        {
                compositor_box_of(dragged, drop_box);
                drop_how = compositor_target(dragged, compositor.pointer_x, compositor.pointer_y, drop_box, &drop_screen, &drop_touch);
        }
        // The preview goes with the drag; where the window lands is the target read above, from its rectangle.
        compositor_preview(COMPOSITOR_NONE);
        compositor.drag = COMPOSITOR_NONE;
        compositor.grab = COMPOSITOR_NONE;
        compositor.resize = COMPOSITOR_NONE;
        // What the hand held is let go: its line is no longer lit.
        if (dragged >= 0 || resized >= 0)
                compositor_lit(COMPOSITOR_NONE, 0, 0);
        if (resized >= 0)
                compositor_mark_clear();
        // The two of a shared edge are let go where the hand left them, each still what it filled and remembering the rectangle
        // it had before (compositor_resize_begin).
        if (resized >= 0 && compositor.shared_row >= 0)
                compositor.shared_row = COMPOSITOR_NONE;
        // A button is let go in the window it was pressed in: a second button pressed over another window while the
        // first is held is that window's, and the first's release is the one that window held the grab for.
        if (index < 8 && (compositor.button_down >> index & 1))
        {
                to = compositor.button_row[index];
                compositor.button_down &= (unsigned char)~(1u << index);
        }
        compositor_post(to, WINDOW_EVENT_BUTTON, flags, (int)code, 0);
        // A press and a release with no motion between is a click on the window's top, which only raises it:
        // a window at the top of the output, where the snap margin is, would otherwise fill it when clicked.
        if (dragged >= 0 && snap && compositor.drag_moved)
        {
                unsigned int how = drop_how;
                int screen = drop_screen;

                if (how != COMPOSITOR_FLOATING)
                {
                        compositor_arrange_on(dragged, how, screen);
                        // The rectangle a restore puts back is the one from before the drag (compositor.drag_keep).
                        compositor.saved_x[dragged] = compositor.drag_keep[0], compositor.saved_y[dragged] = compositor.drag_keep[1];
                        compositor.saved_w[dragged] = (unsigned int)compositor.drag_keep[2];
                        compositor.saved_h[dragged] = (unsigned int)compositor.drag_keep[3];
                        // The others of the screen are offered the space the drop left, until a click or Escape.
                        compositor.assist_on = compositor.assist_armed = 1;
                        compositor.assist_row = dragged;
                        compositor.assist_how = (unsigned int)compositor.arranged[dragged];
                        compositor.assist_screen = screen;
                        compositor_assist_refresh();
                        if (!compositor.assist_count)
                                compositor_assist_close();
                        // They come up now, not during the drag: where they are is drawn for the next present.
                        compositor_overlays_damage();
                }
        }
        if (compositor.grid_on)
        {
                compositor_overlays_damage();
                compositor.grid_on = 0;
        }
        // Where the hand put a window is where a window of its name opens next (compositor_remember).
        if (snap && (resized >= 0 || (dragged >= 0 && compositor.drag_moved)))
                compositor_remember(resized >= 0 ? resized : dragged);
}

// A release is at the real place too (see compositor_press): a mouse's button let go where the hand is.
static void compositor_release(unsigned int code, int snap)
{
        int real;

        compositor_pointer_sync();
        real = !compositor.at_real && !compositor.touching && compositor.owner == COMPOSITOR_PREDICT_MOUSE;
        if (!real)
        {
                compositor_release_as(code, snap, 0);
                return;
        }
        {
                int led_x = compositor.pointer_x, led_y = compositor.pointer_y;

                compositor.at_real = 1;
                compositor_pointer_shown();
                if (compositor.pointer_x != led_x || compositor.pointer_y != led_y)
                        compositor_motion();
        }
        compositor_release_as(code, snap, 0);
        compositor.at_real = 0;
        compositor_pointer_shown();
        compositor_cursor_move();
}

/*
        Whether a point is on a window's close button: its box, or for a finger the box and COMPOSITOR_TOUCH_BUTTON_SLOP more
        on every side (ui_reach_hit), and no further: the corner of the window it sits in is the finger's own, a target of
        UI_TARGET (compositor_corner_reach), and wins everywhere in it but the disc. None for a press being made again
        without its buttons (compositor_touch_resume).
*/
static int compositor_on_close(int row, int x, int y)
{
        struct compositor_frame frame;
        int bx, by, side;

        compositor_frame_of(row, &frame);
        if (compositor.touch.resumed || !compositor_close_box(&frame, &bx, &by, &side))
                return 0;
        return ui_reach_hit(compositor.touching, side + 2 * (COMPOSITOR_TOUCH_BUTTON_SLOP * frame.scale / 100), x, y, bx, by, side, side);
}

/*
        The minimise button's press, and the release that is its: a press on the button puts its window away, and neither
        the press nor its release is given to any window or starts a drag (compositor_press). The code of the press is
        what its release must be, and a press of any button first lets go of what an earlier one held, so a release a
        finger's cancel lost is not held over.
*/
static unsigned int compositor_minimize_code;

static int compositor_on_minimize(int row, int x, int y)
{
        struct compositor_frame frame;
        int bx, by, side;

        compositor_frame_of(row, &frame);
        if (compositor.touch.resumed || !compositor_minimize_box(&frame, &bx, &by, &side))
                return 0;
        return ui_reach_hit(compositor.touching, side + 2 * (COMPOSITOR_TOUCH_BUTTON_SLOP * frame.scale / 100), x, y, bx, by, side, side);
}

static int compositor_minimize_press(unsigned int code, int down)
{
        int under, tab = COMPOSITOR_NONE;

        if (!down)
        {
                if (!code || code != compositor_minimize_code)
                        return 0;
                compositor_minimize_code = 0;
                return 1;
        }
        compositor_minimize_code = 0;
        if (code != COMPOSITOR_BTN_LEFT)
                return 0;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
        if (under < 0 || tab >= 0 || !compositor_visible(under) ||
            !compositor_on_minimize(under, compositor.pointer_x, compositor.pointer_y))
                return 0;

        compositor_popups_close(-1, -1);
        compositor_minimize_code = code;
        compositor_minimize_row(under);
        return 1;
}

// The windows that own the menus open now (the root of each one's chain), as a bit each.
static unsigned int compositor_menu_roots(void)
{
        unsigned int roots = 0;

        for (int row = 0; row < WINDOWS; row++)
                if (compositor_popup_parent[row] >= 0 && compositor.showing[row] && !(compositor.state[row] & WINDOW_CLOSING))
                        roots |= 1u << compositor_popup_root(row);
        return roots;
}

/*
        A press that dismisses a menu takes what the hand had in hand in the window that owns it, as a press that is no click
        does: a drag or a resize of that window is let go where it is, and a button held in it (a selection, a drag in its
        contents) is released with WINDOW_KEY_CANCEL, so the window does not finish it as a click.
*/
static void compositor_cancel_holds(unsigned int roots)
{
        for (unsigned int index = 0; index < 8; index++)
                if ((compositor.button_down >> index & 1) && compositor.button_row[index] >= 0 && roots >> compositor.button_row[index] & 1)
                        compositor_release_as(COMPOSITOR_BTN_LEFT + index, 0, WINDOW_KEY_CANCEL);
        if ((compositor.drag >= 0 && roots >> compositor.drag & 1) || (compositor.resize >= 0 && roots >> compositor.resize & 1))
                compositor_release_as(COMPOSITOR_BTN_LEFT, 0, WINDOW_KEY_CANCEL);
}

static void compositor_button(unsigned int code, int down)
{
        int under, tab, alt = (compositor_modifiers() & WINDOW_KEY_ALT) != 0, top, up;
        unsigned int roots;
        positive now;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);

        if (!down)
        {
                compositor_release(code, 1);
                return;
        }

        // A right press while the hand moves or resizes a window takes the move back, as Escape does (compositor_hold_cancel);
        // the press is nothing else. A resize the right button holds (Alt) has no second right press.
        if (code == COMPOSITOR_BTN_RIGHT && compositor_hold_cancel())
                return;

        if (under >= 0 && compositor_popup_parent[under] >= 0)
        {
                compositor_popups_close(-1, under);
                compositor.grab = compositor.grab >= 0 ? compositor.grab : under;
                compositor_pointer_to(under);
                compositor_post(under, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, (int)code, 0);
                if (code - COMPOSITOR_BTN_LEFT < 8)
                {
                        compositor.button_down |= (unsigned char)(1u << (code - COMPOSITOR_BTN_LEFT));
                        compositor.button_row[code - COMPOSITOR_BTN_LEFT] = under;
                }
                return;
        }
        // A press outside every popup that is open asks them to go, and is theirs: it goes no further, to the window
        // below, the desktop or a tab, with a finger the same as a mouse (compositor_touch_button is this path too).
        roots = compositor_menu_roots();
        if (compositor_popups_close(-1, -1))
        {
                compositor_cancel_holds(roots);
                return;
        }

        // The launcher that is up before this press, if there is one: the press below on the launcher's button puts it away
        // (it is asked to go here, like any press outside it) and asks for no other.
        up = compositor_launcher_row();

        // A window that asked to be put away by a press anywhere but on it (WINDOW_DISMISS) is asked to go by one on
        // the desktop, a tab, another window or the tray alike; the press then goes on as it would have.
        if (compositor.focus >= 0 && (compositor.asks[compositor.focus] & WINDOW_DISMISS) && compositor.showing[compositor.focus] &&
            !(compositor.pointer_x >= compositor.x[compositor.focus] &&
              compositor.pointer_x < compositor.x[compositor.focus] + (int)compositor.w[compositor.focus] &&
              compositor.pointer_y >= compositor.y[compositor.focus] &&
              compositor.pointer_y < compositor.y[compositor.focus] + (int)compositor.h[compositor.focus]))
                compositor_close(compositor.focus);

        // The panel's cells (the launcher's button, the microphone, the volume and the tray) take a press by the taskbar's rule
        // (taskbar_cell_press): what it does is the rule's action, and the compositor carries the action out. Each takes its
        // press alone: none of them passes it on to a window.
        if (tab == TASKBAR_LAUNCHER || tab == TASKBAR_MIC || tab == TASKBAR_VOLUME || tab == COMPOSITOR_TRAY)
        {
                switch (taskbar_cell_press(&compositor.cells, tab, code - COMPOSITOR_BTN_LEFT, clock_monotonic_nanoseconds(),
                                           compositor.pointer_x, compositor.pointer_y, compositor.scale))
                {
                case TASKBAR_ACT_LAUNCHER:
                        // with the launcher up the press puts it away, and with none it asks the script for one beside the button
                        if (up >= 0)
                        {
                                compositor_close(up);
                                compositor_tabs_changed();
                        }
                        else
                                compositor_launcher_open(compositor.pointer_x, compositor.pointer_y);
                        break;
                case TASKBAR_ACT_VOLUME_OPEN: compositor_volume_open(compositor.pointer_x, compositor.pointer_y); break;
                case TASKBAR_ACT_VOLUME_MUTE: compositor_volume_step(0, 1); break;
                case TASKBAR_ACT_MIC_OPEN: compositor_mic_open(compositor.pointer_x, compositor.pointer_y); break;
                case TASKBAR_ACT_MIC_MUTE: compositor_mic_step(); break;
                case TASKBAR_ACT_SETTINGS: compositor_settings_open(); break;
                default: break;
                }
                return;
        }

        // A press on a window's tab brings the window up, and a middle press asks it to go. The tray takes none.
        if (tab >= 0)
        {
                if (code == COMPOSITOR_BTN_MIDDLE)
                        compositor_close(compositor.listed[tab]);
                else if (code == COMPOSITOR_BTN_LEFT)
                        compositor_reveal(compositor.listed[tab]);
                return;
        }

        if (under < 0)
                return;

        if ((code == COMPOSITOR_BTN_LEFT || (code == COMPOSITOR_BTN_RIGHT && alt)) && compositor.grab < 0)
        {
                unsigned int edges = compositor_edge_zone(under, compositor.pointer_x, compositor.pointer_y);

                now = clock_monotonic_nanoseconds();
                compositor_raise(under);
                compositor_focus(under);

                /*
                        A finger on the close button: the window goes when the finger lifts from it without
                        moving (compositor_touch_report), and one that moves past the slop was making the press the
                        corner or the titlebar gives (compositor_touch_resume). The X is the finger's only within its
                        disc and a little more; the rest of the corner is the corner's.
                */
                if (compositor.touching && !alt && code == COMPOSITOR_BTN_LEFT &&
                    compositor_on_close(under, compositor.pointer_x, compositor.pointer_y))
                {
                        compositor.press = COMPOSITOR_NONE;
                        compositor.touch.closing = under;
                        ui_touch_pend(&compositor.touch.gesture, 1);
                        return;
                }

                // Just outside a window's edges is the edge, to resize by. An edge resize makes
                // this ordinary geometry: a double click after it fills the screen and does not
                // restore what was. The edge the hand has is lit at once.
                if (edges && code == COMPOSITOR_BTN_LEFT)
                {
                        if (!compositor_shared_begin(under, edges))
                                compositor_resize_begin(under, edges);
                        compositor_lit(under, edges, 1);
                        return;
                }

                // With Alt held, the right button resizes from the corner nearest the hand, which a window
                // filling the output has no other way to be.
                if (alt && code == COMPOSITOR_BTN_RIGHT)
                {
                        int centre_x = compositor.x[under] + (int)compositor.w[under] / 2;
                        int centre_y = compositor.y[under] + (int)compositor.h[under] / 2;

                        compositor_resize_begin(under, (compositor.pointer_x < centre_x ? COMPOSITOR_EDGE_LEFT
                                                                                        : COMPOSITOR_EDGE_RIGHT) |
                                                           (compositor.pointer_y < centre_y ? COMPOSITOR_EDGE_TOP
                                                                                            : COMPOSITOR_EDGE_BOTTOM));
                        compositor_lit(under, compositor.edges[under], 1);
                        return;
                }

                /*
                        The top of a window is its titlebar, drawn as nothing:
                        a press there is the window's whatever is under it --
                        on the close button it asks the window to go, twice
                        quickly it fills the output or gives it back, and
                        otherwise it takes hold of the window to move it.
                        With Alt, anywhere on the window is the same.
                */
                top = !alt && code == COMPOSITOR_BTN_LEFT && !(compositor.asks[under] & WINDOW_NO_GRAB) &&
                      compositor.pointer_x >= compositor.x[under] &&
                      compositor.pointer_x < compositor.x[under] + (int)compositor.w[under] &&
                      compositor.pointer_y >= compositor.y[under] &&
                      compositor.pointer_y < compositor.y[under] + COMPOSITOR_GRAB * compositor.scale / 100;

                if (top && compositor_on_close(under, compositor.pointer_x, compositor.pointer_y))
                {
                        // Not one of a pair, or a second click would fill the screen on its way out.
                        compositor.press = COMPOSITOR_NONE;
                        compositor_close(under);
                        return;
                }

                if (alt || top)
                {
                        // The count is kept for every press on a top (the second of a run of two, on the same window, is the pair).
                        int run = ui_click_again(&compositor.press_click, now, compositor.pointer_x, compositor.pointer_y, compositor.scale);
                        int again = run == 2 && under == compositor.press;

                        compositor.press = under;
                        if (again)
                        {
                                compositor.press = COMPOSITOR_NONE;
                                // One placed filling its screen's room is given back as a maximized one is.
                                if (compositor.arranged[under] == COMPOSITOR_MAXIMIZED ||
                                    compositor.arranged[under] == COMPOSITOR_ROOM)
                                        compositor_float(under);
                                else
                                        compositor_arrange(under, COMPOSITOR_MAXIMIZED);
                                return;
                        }
                        compositor_drag_begin(under);
                        return;
                }
        }

        // The window's own: a press inside its picture. The motion first, so it knows where.
        if (compositor.pointer_x < compositor.x[under] || compositor.pointer_y < compositor.y[under] ||
            compositor.pointer_x >= compositor.x[under] + (int)compositor.w[under] ||
            compositor.pointer_y >= compositor.y[under] + (int)compositor.h[under])
                return;

        compositor.grab = compositor.grab >= 0 ? compositor.grab : under;
        compositor_pointer_to(under);
        compositor_post(under, WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, (int)code, 0);
        if (code - COMPOSITOR_BTN_LEFT < 8)
        {
                compositor.button_down |= (unsigned char)(1u << (code - COMPOSITOR_BTN_LEFT));
                compositor.button_row[code - COMPOSITOR_BTN_LEFT] = under;
        }
}

/*
        Any press or release of a button, from the mouse or a finger: the minimise button's first, which takes its
        press and release alone, and then the window's (compositor_button).
*/
static void compositor_press_at(unsigned int code, int down);

/*
        A button of the mouse is handled at the hand's real place, presses and releases alike, whatever the pointer is led to:
        a click lands where the hand was. A finger's or a pen's press is at its led place, as it always was.
*/
static void compositor_press(unsigned int code, int down)
{
        int real;

        compositor_pointer_sync();
        real = !compositor.at_real && !compositor.touching && compositor.owner == COMPOSITOR_PREDICT_MOUSE;
        if (!real)
        {
                compositor_press_at(code, down);
                return;
        }
        // The window under the press is told the motion to the real place before the button, as each report was told before
        // the change: a button carries no place of its own, only the last motion's. A pointer that is not led is told already.
        {
                int led_x = compositor.pointer_x, led_y = compositor.pointer_y;

                compositor.at_real = 1;
                compositor_pointer_shown();
                if (compositor.pointer_x != led_x || compositor.pointer_y != led_y)
                        compositor_motion();
        }
        compositor_press_at(code, down);
        compositor.at_real = 0;
        compositor_pointer_shown();
        compositor_cursor_move();
}

static void compositor_press_at(unsigned int code, int down)
{
        if (down && code == COMPOSITOR_BTN_LEFT && compositor.assist_armed)
        {
                int at = compositor_assist_at(compositor.pointer_x, compositor.pointer_y);

                if (at >= 0)
                {
                        int row = (int)compositor.assist_rows[at];
                        unsigned int how = compositor.assist_zone[at];
                        int screen = compositor.assist_screen;

                        compositor_assist_close();
                        compositor_arrange_on(row, how, screen);
                        return;
                }
                compositor_assist_close();
        }
        if (!compositor_minimize_press(code, down))
                compositor_button(code, down);
}

// Whether a key of a touchpad is one of its touch and tool keys, which say where fingers are and press nothing.
static int compositor_pad_finger_key(unsigned int code)
{
        return code == COMPOSITOR_BTN_TOUCH || code == COMPOSITOR_BTN_TOOL_FINGER || code == COMPOSITOR_BTN_TOOL_DOUBLETAP ||
               code == COMPOSITOR_BTN_TOOL_TRIPLETAP || code == COMPOSITOR_BTN_TOOL_QUADTAP || code == COMPOSITOR_BTN_TOOL_QUINTTAP;
}

// A touchpad's touch and tool keys: whether a finger touches it, and how many are on it. A finger more or less, or
// none, starts the touch again: where one finger was is not where two are, and the pad says one place for them.
static void compositor_pad_key(struct compositor_pad *pad, unsigned int code, int value)
{
        int count = code == COMPOSITOR_BTN_TOOL_FINGER      ? 1
                    : code == COMPOSITOR_BTN_TOOL_DOUBLETAP ? 2
                    : code == COMPOSITOR_BTN_TOOL_TRIPLETAP ? 3
                    : code == COMPOSITOR_BTN_TOOL_QUADTAP   ? 4
                                                            : 5;

        if (code == COMPOSITOR_BTN_TOUCH)
                pad->touching = value != 0;
        else if (value)
                pad->fingers = count;
        else if (pad->fingers == count)
                pad->fingers = 0;
        pad->moving = 0;
        pad->left_x = pad->left_y = 0;
}

/*
        One report of a touchpad over: what its finger did since the last one, as a mouse's counts (x and y), or
        with two fingers or more on it as the wheel in 120ths (x sideways, y up). Which it was: 1 a move, 2 the
        wheel, and 0 neither, as the first report of a touch is, which says only where the finger is.
*/
static int compositor_pad_report(struct compositor_pad *pad, int *x, int *y)
{
        long dx = (long)pad->next_x - pad->at_x, dy = (long)pad->next_y - pad->at_y;
        int kind = pad->fingers >= 2 ? 2 : 1, per = kind == 2 ? COMPOSITOR_PAD_WHEEL : COMPOSITOR_PAD_COUNTS;
        long left_x, left_y;

        *x = *y = 0;
        pad->at_x = pad->next_x;
        pad->at_y = pad->next_y;
        if (!pad->touching || !pad->fingers || !pad->moving)
        {
                pad->moving = pad->touching && pad->fingers;
                return 0;
        }
        left_x = pad->left_x + dx * per;
        left_y = pad->left_y + (kind == 2 ? -dy : dy) * per;
        *x = (int)(left_x / pad->per_mm_x);
        *y = (int)(left_y / pad->per_mm_y);
        pad->left_x = (int)(left_x - (long)*x * pad->per_mm_x);
        pad->left_y = (int)(left_y - (long)*y * pad->per_mm_y);
        return *x || *y ? kind : 0;
}

/* ---- the prediction a frame ahead ---- */

// The frame a lead looks ahead by: the compositor's period, and 60 Hz before any screen has said one.
static positive compositor_predict_period(void)
{
        return compositor.period ? compositor.period : 1000000000ull / 60;
}

/*
        The lead of a class: the least-squares slope of its places over the window, times one frame (see the comment on the
        prediction), per axis; then the gates; then the cap on its length, rounded up before the scale so it is never past the
        cap, whole pixels or not.
*/
static void compositor_predict_lead(struct compositor_predict *track)
{
        const long long fx = COMPOSITOR_PREDICT_FX, period = (long long)compositor_predict_period() / 1000;
        const long long cap = (long long)COMPOSITOR_PREDICT_CAP * compositor.scale / 100 * fx;
        long long tn = (long long)track->when[track->count - 1], St = 0, Stt = 0, Sx = 0, Stx = 0, Sy = 0, Sty = 0, m, i0, i, n;
        long long denom, lead_x, lead_y, net, path, g, w, span, speed, ws, length2, length, limit, step;

        track->lead_x = track->lead_y = 0;
        n = track->count;
        i0 = n - 1;
        while (i0 > 0 && (long long)track->when[i0 - 1] >= tn - (long long)COMPOSITOR_PREDICT_WINDOW_NS)
                i0--;
        m = n - i0;
        if (m < 2 || tn - (long long)track->when[i0] < (long long)COMPOSITOR_PREDICT_SPAN_NS || cap <= 0)
                return;
        for (i = i0; i < n; i++)
        {
                long long t = ((long long)track->when[i] - tn) / 1000; /* microseconds, not after the newest */

                St += t, Stt += t * t;
                Sx += track->x[i], Stx += t * track->x[i];
                Sy += track->y[i], Sty += t * track->y[i];
        }
        denom = m * Stt - St * St;
        if (!denom)
                return;
        lead_x = (m * Stx - St * Sx) * period / denom;
        lead_y = (m * Sty - St * Sy) * period / denom;

        /* the straightness: the net move over the window against the path it took, both axes in Manhattan lengths */
        net = track->x[n - 1] - track->x[i0];
        net = (net < 0 ? -net : net);
        path = 0;
        for (i = i0 + 1; i < n; i++)
        {
                step = track->x[i] - track->x[i - 1];
                path += step < 0 ? -step : step;
                step = track->y[i] - track->y[i - 1];
                path += step < 0 ? -step : step;
        }
        step = track->y[n - 1] - track->y[i0];
        net += step < 0 ? -step : step;
        g = path > 0 ? net * 1000 / path : 0;
        w = (g - COMPOSITOR_PREDICT_STRAIGHT_LO) * 1000 / COMPOSITOR_PREDICT_STRAIGHT_SPAN;
        w = w < 0 ? 0 : w > 1000 ? 1000 : w;

        /* the speed: the net move over the window's span, in 1/1024 px a millisecond */
        span = tn - (long long)track->when[i0];
        speed = net * 1000000 / span;
        ws = (speed - COMPOSITOR_PREDICT_STILL_LO) * 1000 / (COMPOSITOR_PREDICT_STILL_HI - COMPOSITOR_PREDICT_STILL_LO);
        ws = ws < 0 ? 0 : ws > 1000 ? 1000 : ws;
        lead_x = lead_x * w / 1000 * ws / 1000;
        lead_y = lead_y * w / 1000 * ws / 1000;

        /* a turn: an axis whose newest step went back has no lead on it */
        step = track->x[n - 1] - track->x[n - 2];
        if ((step < 0 && lead_x > 0) || (step > 0 && lead_x < 0))
                lead_x = 0;
        step = track->y[n - 1] - track->y[n - 2];
        if ((step < 0 && lead_y > 0) || (step > 0 && lead_y < 0))
                lead_y = 0;

        /* the lead is shown in whole pixels: those are the cap's, rounded up before the scale, so the length shown is never past it */
        lead_x = compositor_predict_px(lead_x), lead_y = compositor_predict_px(lead_y);
        limit = cap / COMPOSITOR_PREDICT_FX;
        length2 = lead_x * lead_x + lead_y * lead_y;
        if (length2 > limit * limit)
        {
                length = (long long)compositor_isqrt((unsigned long long)length2);
                if (length * length < length2)
                        length++;
                lead_x = lead_x * limit / length;
                lead_y = lead_y * limit / length;
        }
        track->lead_x = lead_x * COMPOSITOR_PREDICT_FX, track->lead_y = lead_y * COMPOSITOR_PREDICT_FX;
}

// A class reported the place x, y (in 1/1024 px) on its device's time when, and was read at now. A place it repeats is no new
// one, and its lead stays as it was.
static void compositor_predict_sample(unsigned int cls, long long x, long long y, positive when, positive now)
{
        struct compositor_predict *track = &compositor.predict[cls];

        if (track->count && track->x[track->count - 1] == x && track->y[track->count - 1] == y)
                return;
        if (track->count == COMPOSITOR_PREDICT_HISTORY)
        {
                memmove(track->x, track->x + 1, sizeof(track->x[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                memmove(track->y, track->y + 1, sizeof(track->y[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                memmove(track->when, track->when + 1, sizeof(track->when[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                memmove(track->arrived, track->arrived + 1, sizeof(track->arrived[0]) * (COMPOSITOR_PREDICT_HISTORY - 1));
                track->count--;
        }
        track->x[track->count] = x, track->y[track->count] = y, track->when[track->count] = when, track->arrived[track->count] = now;
        track->count++;
        track->last = now;
        track->lead_x = track->lead_y = 0;
        if (!compositor.predict_off[cls] && track->count >= 2)
                compositor_predict_lead(track);
}

// The mouse moved. The mouse is not predicted (the owner's call): its real place is the pointer, with no lead and no sample, so
// the pointer is drawn and told where the hand is. Its times are not used.
static void compositor_predict_mouse(positive when, positive now)
{
        (void)when;
        (void)now;
        compositor.owner = COMPOSITOR_PREDICT_MOUSE;
        compositor_pointer_shown();
}

// A finger or a pen is at x, y: it is the hand's real place, and the pointer is where it will be, so that is where the windows
// it reaches are told. The mouse has no lead while the finger or the pen is the pointer.
static void compositor_predict_place(unsigned int cls, int x, int y, positive now)
{
        compositor_pointer_sync();
        compositor.owner = cls;
        compositor.real_x = x, compositor.real_y = y;
        compositor_predict_sample(cls, (long long)x * COMPOSITOR_PREDICT_FX, (long long)y * COMPOSITOR_PREDICT_FX, now, now);
        compositor.predict[COMPOSITOR_PREDICT_MOUSE].lead_x = compositor.predict[COMPOSITOR_PREDICT_MOUSE].lead_y = 0;
        compositor_pointer_shown();
}

// A class's lead is over (a touch lifted, a pen out of range, a stop, or its setting turned off). A mouse's pointer goes back to
// its real place, and the windows are told when it moved; a finger or a pen goes back to its newest place, and the windows are told
// where that is.
static void compositor_predict_clear(unsigned int cls)
{
        struct compositor_predict *track = &compositor.predict[cls];
        int moved_x, moved_y;

        compositor_pointer_sync();
        moved_x = compositor.pointer_x, moved_y = compositor.pointer_y;
        if (!track->lead_x && !track->lead_y)
                return;
        track->lead_x = track->lead_y = 0;
        if (cls == COMPOSITOR_PREDICT_MOUSE)
        {
                if (compositor.owner != COMPOSITOR_PREDICT_MOUSE)
                        return;
                compositor_pointer_shown();
                if (compositor.pointer_x != moved_x || compositor.pointer_y != moved_y)
                        compositor_motion();
                return;
        }
        compositor.owner = cls;
        compositor.real_x = (int)(track->x[track->count - 1] / COMPOSITOR_PREDICT_FX);
        compositor.real_y = (int)(track->y[track->count - 1] / COMPOSITOR_PREDICT_FX);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor_pointer_shown();
        compositor.touching = cls == COMPOSITOR_PREDICT_TOUCH;
        // The cursor, a window in the hand and the windows told go back to the real place; the motion the windows are told says it is
        // no step the hand made (WINDOW_KEY_SETTLE), since a window that reads a flick off its moves (the kit's throw) takes a step
        // back at the lift, the retraction of the lead, for the finger turning.
        compositor.retracting = 1;
        compositor_motion();
        compositor.retracting = 0;
        compositor.touching = 0;
}

// The time the next lead is due to be cleared (a frame after its newest place), or 0 when no class is led.
static positive compositor_predict_due(void)
{
        positive period = compositor_predict_period(), due = 0;

        for (unsigned int cls = 0; cls < COMPOSITOR_PREDICTS; cls++)
        {
                const struct compositor_predict *track = &compositor.predict[cls];

                if ((track->lead_x || track->lead_y) && (!due || track->last + period < due))
                        due = track->last + period;
        }
        return due;
}

// The leads whose frame has gone by with no new place are cleared: a hand that stopped is back on where it stopped.
static void compositor_predict_expire(positive now)
{
        positive period = compositor_predict_period();

        for (unsigned int cls = 0; cls < COMPOSITOR_PREDICTS; cls++)
                if ((compositor.predict[cls].lead_x || compositor.predict[cls].lead_y) && now >= compositor.predict[cls].last + period)
                        compositor_predict_clear(cls);
}

// A class's prediction turned on or off, read with the other settings the compositor takes at once. The mouse is always
// predicted, so only touch and pen have a setting (`moonwater canvas predict touch`): the one row, for both.
static void compositor_predict_set(unsigned int cls, int on)
{
        compositor.predict_off[cls] = (unsigned char)!on;
        if (!on)
                compositor_predict_clear(cls);
}

static void compositor_predict_read(void)
{
        char word[16];
        long value;
        int touch;

        compositor_word(SETTING_PREDICT_TOUCH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_PREDICT_TOUCH_PATH, word, &value);
        touch = value == 0;
        compositor_predict_set(COMPOSITOR_PREDICT_TOUCH, touch);
        compositor_predict_set(COMPOSITOR_PREDICT_PEN, touch);
}

// When an event was made by its device (its own stamp, in nanoseconds on the device's clock), or now when it has none.
static positive compositor_stamp(const struct compositor_input_event *event, positive now)
{
        positive stamp = (positive)event->seconds * 1000000000ull + (positive)event->microseconds * 1000ull;

        return event->seconds || event->microseconds ? stamp : now;
}

/*
        One report of a device is over: a mouse's counts since the last one are
        one movement of the hand, with the gain of its speed, and a tablet's
        place is where the report says on the output. Both axes of either are
        one movement: taken one at a time the cursor moved twice for it, the
        second time with the other axis a report out of date, and two axes
        timed each against the last gave left and right one speed and up and
        down another.
*/
static int compositor_report(positive now, positive stamp)
{
        int moved = 0;

        compositor_pointer_sync();
        if (compositor.tablet_have)
        {
                if (compositor.tablet_have & 1)
                        compositor.real_x = compositor.tablet_x;
                if (compositor.tablet_have & 2)
                        compositor.real_y = compositor.tablet_y;
                compositor.tablet_have = 0;
                compositor_predict_place(COMPOSITOR_PREDICT_PEN, compositor.real_x, compositor.real_y, now);
                compositor.absolute = 1;
                moved = 1;
        }

        if (compositor.raw_x || compositor.raw_y)
        {
                int dx = compositor.raw_x, dy = compositor.raw_y, gain;

                compositor.raw_x = compositor.raw_y = 0;
#ifdef CONFIG_MOONWATER_WAYLAND
                // A client's window holds the pointer locked: the hand's counts are its, and the pointer does not move.
                if (compositor_wl_hold_counts(dx, dy, now))
                        return moved;
#endif
                gain = compositor_gain_wanted(compositor_gain_of(dx, dy, now - compositor.stamp), compositor.flat,
                                              compositor.sensitivity);
                compositor.stamp = now;
                compositor.real_x = compositor_counts(compositor.real_x, compositor_accel(dx, &compositor.accel_x, gain));
                compositor.real_y = compositor_counts(compositor.real_y, compositor_accel(dy, &compositor.accel_y, gain));
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_hold_keep();
#endif
                compositor_predict_mouse(stamp, now);
                moved = 1;
        }

        // A mouse, a pad or a tablet moved: the pointer a finger hid is shown again where it is (compositor_motion).
        if (moved)
                compositor.touch.hidden = 0;
        return moved;
}

// What is under a point of its picture: the topmost window whose picture holds it, with the grip a window has just
// outside its edges (compositor_under's, for a hand that means to resize) not counted, and none over a tab.
static int compositor_picture_under(int x, int y)
{
        if (compositor_band_hit(x, y) != TASKBAR_HIT_NONE)
                return COMPOSITOR_NONE;
        for (unsigned int at = compositor.stacked; at--;)
        {
                int row = compositor.stack[at];

                if (compositor_visible(row) && x >= compositor.x[row] && x < compositor.x[row] + (int)compositor.w[row] &&
                    y >= compositor.y[row] && y < compositor.y[row] + (int)compositor.h[row])
                        return row;
        }
        return COMPOSITOR_NONE;
}

// The wheel goes to what holds the pointer, or to what is under it when the wheel turns. Not to what was under it when
// the pointer last moved (compositor.hover): a menu or a terminal that a key opened, a window closed or moved or
// raised by the keys, and the desktop before the first motion all lie under a pointer that has not moved, and a wheel
// sent to the window it last moved over scrolled that one, or nothing.
static void compositor_wheel_flags(int v120, int horizontal, unsigned int flags);

// A notch's wheel: 120ths of a notch, taken as notches by the region (a glide).
static void compositor_wheel(int v120, int horizontal)
{
        compositor_wheel_flags(v120, horizontal, 0);
}

// A trackpad's two fingers: the same 120ths, flagged precise, so the region moves one for one with them (WINDOW_WHEEL_PRECISE).
static void compositor_wheel_precise(int v120, int horizontal)
{
        compositor_wheel_flags(v120, horizontal, WINDOW_WHEEL_PRECISE);
}

static void compositor_wheel_flags(int v120, int horizontal, unsigned int flags)
{
        int row = compositor.grab;

        // A motion read with the wheel in one batch has moved the pointer and not yet kept it on a screen (that is
        // done when the batch is over): kept first, as before a button, or a flick past the edge sends the wheel to
        // no window, or to one in the room beside a shorter screen.
        compositor_pointer_clamp();
        if (row < 0 && !horizontal && taskbar_cell_of(compositor_band_hit(compositor.pointer_x, compositor.pointer_y), TASKBAR_BUTTON_WHEEL) == TASKBAR_ACT_VOLUME_STEP)
        {
                // The wheel over the tray's volume cell turns the volume, five percent a notch (120ths of one add up).
                static int notch;
                int turns = ui_notch_take(&notch, v120);

                if (turns)
                        compositor_volume_step(COMPOSITOR_VOLUME_STEP * turns, 0);
                return;
        }
        if (row < 0)
                row = compositor.hover = compositor_picture_under(compositor.pointer_x, compositor.pointer_y);
        // Where the pointer is goes first, as it does before a button: a window that turns a wheel by where the pointer
        // is (the settings window's two columns, a terminal's report to a program that asked for the mouse) has only
        // the last motion it was told of, which is none when it opened under a pointer that has not moved. Told once:
        // a wheel that reports in 120ths sends several events to a notch, and each would take two of the ring's places.
        if (row >= 0 && (row != compositor.told_row || compositor.pointer_x - compositor.x[row] != compositor.told_x ||
                         compositor.pointer_y - compositor.y[row] != compositor.told_y))
                compositor_pointer_to(row);
        // What became of it, for moonwater debug.
        if (row < 0)
                compositor.table->wheel_nowhere++;
        else if (compositor_post(row, WINDOW_EVENT_WHEEL, flags, v120, horizontal))
                compositor.table->wheel_sent++;
        else
                compositor.table->wheel_full++;
        compositor.table->wheel_row = row;
        compositor.table->wheel_x = row < 0 ? compositor.pointer_x : compositor.pointer_x - compositor.x[row];
        compositor.table->wheel_y = row < 0 ? compositor.pointer_y : compositor.pointer_y - compositor.y[row];
}

// A touchpad's report over: its finger's move is a mouse's, and two fingers' are the wheel, counted for moonwater
// debug as the device's wheel.
static void compositor_pad_apply(unsigned int at, positive now)
{
        int x, y, kind = compositor_pad_report(&compositor.pad[at], &x, &y);

        if (kind == 1)
        {
                compositor.raw_x = compositor_counts(compositor.raw_x, x);
                compositor.raw_y = compositor_counts(compositor.raw_y, y);
        }
        else if (kind == 2)
        {
                compositor.table->input_wheel_read[at]++;
                compositor.table->input_wheel_used[at]++;
                compositor.table->input_wheel_last[at] = now;
                if (y)
                        compositor_wheel_precise(y, 0);
                if (x)
                        compositor_wheel_precise(x, 1);
        }
}

/* ---- touch: fingers on a screen ---- */

/*
        The marks of the fingers (`moonwater canvas touch on`): each one damaged where it was drawn and where it is
        drawn now, as what lies over every window, so no glass is made again for it, and nothing at all is done for
        them while the setting is off or no finger is down or fading.
*/
static void compositor_spot_undraw(int slot)
{
        int *drawn = compositor.spot[slot].drawn;

        if (!drawn[2])
                return;
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[2]);
        compositor.damage_by = 0;
        drawn[2] = 0;
}

static void compositor_spot_redraw(int slot)
{
        int radius = compositor_spot_radius(compositor.scale), *drawn = compositor.spot[slot].drawn;

        compositor_spot_undraw(slot);
        drawn[0] = compositor.spot[slot].x - radius;
        drawn[1] = compositor.spot[slot].y - radius;
        drawn[2] = 2 * radius;
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(drawn[0], drawn[1], drawn[2], drawn[2]);
        compositor.damage_by = 0;
}

// A finger down at x, y: its mark follows it.
static void compositor_spot_set(int slot, int x, int y)
{
        if (!compositor.spots_on || slot < 0 || slot >= UI_TOUCH_SLOTS)
                return;
        if (compositor.spot[slot].on == 1 && compositor.spot[slot].x == x && compositor.spot[slot].y == y)
                return;
        compositor.spot[slot].x = x;
        compositor.spot[slot].y = y;
        compositor.spot[slot].on = 1;
        compositor.spot[slot].alpha = 255;
        compositor.spots_shown |= 1u << slot;
        compositor.spots_fading &= ~(1u << slot);
        compositor_spot_redraw(slot);
}

// Its finger lifted: the mark fades from now (compositor_spots_tick).
static void compositor_spot_lift(int slot, positive now)
{
        if (slot < 0 || slot >= UI_TOUCH_SLOTS || compositor.spot[slot].on != 1)
                return;
        compositor.spot[slot].on = 2;
        compositor.spot[slot].lifted = now;
        compositor.spots_fading |= 1u << slot;
}

// Whether any mark is drawn, and whether any is fading, which is a frame to draw each period until it is gone.
static int compositor_spots_any(int fading)
{
        return (fading ? compositor.spots_fading : compositor.spots_shown) != 0;
}

// The fading marks at now: each fainter by the time since its finger lifted, and gone after COMPOSITOR_SPOT_FADE_NS.
static void compositor_spots_tick(positive now)
{
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                positive since;
                unsigned int alpha;

                if (compositor.spot[slot].on != 2)
                        continue;
                since = now > compositor.spot[slot].lifted ? now - compositor.spot[slot].lifted : 0;
                if (since >= COMPOSITOR_SPOT_FADE_NS)
                {
                        compositor_spot_undraw((int)slot);
                        compositor.spot[slot].on = 0;
                        compositor.spot[slot].alpha = 0;
                        compositor.spots_shown &= ~(1u << slot);
                        compositor.spots_fading &= ~(1u << slot);
                        continue;
                }
                alpha = (unsigned int)((COMPOSITOR_SPOT_FADE_NS - since) * 255 / COMPOSITOR_SPOT_FADE_NS);
                if (alpha != compositor.spot[slot].alpha)
                {
                        compositor.spot[slot].alpha = alpha;
                        compositor_spot_redraw((int)slot);
                }
        }
}

// Every mark taken off at once: the setting was turned off.
static void compositor_spots_clear(void)
{
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                compositor_spot_undraw((int)slot);
                compositor.spot[slot].on = 0;
        }
        compositor.spots_shown = compositor.spots_fading = 0;
}

// The masks a mark is drawn with, made before the scene's bands, which draw marks into their slices at once.
static void compositor_spots_ready(void)
{
        int radius = compositor_spot_radius(compositor.scale);

        compositor_corner_mask(radius);
        compositor_corner_mask(radius - compositor_px1(compositor.scale));
}

// The marks over a target the scene was composed into and may be read (a slice of a scene composed apart).
static void compositor_spots_draw(const struct compositor_target *t)
{
        if (!compositor.spots_shown)
                return;
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (compositor.spot[slot].on)
                        compositor_spot_draw(t, compositor.spot[slot].x, compositor.spot[slot].y, compositor.scale,
                                             compositor.spot[slot].alpha);
}

/*
        The marks over a target that is only written (the scanout): what is under each mark is composed again into a
        scratch of its own, the marks are drawn over that, and it is copied out, as the round corners are.
*/
static void compositor_spots_compose(const struct compositor_target *t, const struct compositor_frame *frames,
                                     unsigned int count)
{
        int radius = compositor_spot_radius(compositor.scale);

        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                int box[4] = {compositor.spot[slot].x - radius, compositor.spot[slot].y - radius,
                              compositor.spot[slot].x + radius, compositor.spot[slot].y + radius};
                int clip[4] = {t->x1, t->y1, t->x2, t->y2};

                if (!compositor.spot[slot].on)
                        continue;
                compositor_meet(box, clip);
                if (box[2] <= box[0] || box[3] <= box[1])
                        continue;
                if (!compositor.spot_scratch)
                        compositor.spot_scratch = (unsigned int *)compositor_map_zeroed(
                            (unsigned long)4 * (2 * COMPOSITOR_MASK_MOST) * (2 * COMPOSITOR_MASK_MOST));
                if (!compositor.spot_scratch)
                        return;
                {
                        struct compositor_target square = {compositor.spot_scratch, (unsigned long)(box[2] - box[0]),
                                                           t->width, t->height, box[0], box[1], box[2], box[3],
                                                           box[0], box[1], 0};

                        compositor_scene(&square, frames, count);
                        compositor_spots_draw(&square);
                        compositor_blit(t, box[0], box[1], box[2] - box[0], box[3] - box[1], compositor.spot_scratch,
                                        (unsigned long)(box[2] - box[0]));
                }
        }
}

// `moonwater canvas touch`, read with the other settings the compositor takes at once: on (the row's standard, nothing
// kept or a file that says default) or off, which takes every mark away.
static void compositor_touch_settings_read(void)
{
        char word[16];
        long value;
        int on;

        compositor_word(SETTING_TOUCH_PATH, word, sizeof(word));
        compositor_setting_kept(SETTING_TOUCH_PATH, word, &value);
        on = value == 0;
        if (!on && compositor.spots_on)
                compositor_spots_clear();
        compositor.spots_on = on;
}

/*
        The screen a touch screen is: the machine's own panel when one is connected (compositor_builtin_panel), and
        else the primary. A finger's place in the device's units is a point of that screen, as a tablet's is of the
        space.
*/
static int compositor_touch_output(void)
{
        for (unsigned int at = 0; at < compositor.outputs; at++)
                if (compositor_builtin_panel(compositor.output[at].name))
                        return (int)at;
        return 0;
}

static void compositor_touch_point(unsigned int at, int slot, int *x, int *y)
{
        const struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        int sx, sy, width, height;

        compositor_output_rect(compositor_touch_output(), &sx, &sy, &width, &height);
        *x = sx + compositor_tablet(screen->x[slot], screen->range[0], screen->range[1], width - 1);
        *y = sy + compositor_tablet(screen->y[slot], screen->range[2], screen->range[3], height - 1);
}

// A touch screen's event, kept until its report is over: its slot, a finger's id and its place.
static void compositor_touch_event(unsigned int at, const struct compositor_input_event *event)
{
        struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        int slot = screen->slots ? screen->slot : 0;

        if (screen->dropped)
                return;
        if (event->type == COMPOSITOR_EV_ABS && event->code == COMPOSITOR_ABS_MT_SLOT)
                screen->slot = event->value;
        else if (slot < 0 || slot >= UI_TOUCH_SLOTS)
                return;
        else if (event->type == COMPOSITOR_EV_ABS && screen->slots && event->code == COMPOSITOR_ABS_MT_TRACKING_ID)
                screen->id[slot] = event->value < 0 ? -1 : event->value;
        else if (event->type == COMPOSITOR_EV_ABS && event->code == (screen->slots ? COMPOSITOR_ABS_MT_POSITION_X : 0u))
                screen->x[slot] = event->value;
        else if (event->type == COMPOSITOR_EV_ABS && event->code == (screen->slots ? COMPOSITOR_ABS_MT_POSITION_Y : 1u))
                screen->y[slot] = event->value;
        else if (event->type == COMPOSITOR_EV_KEY && event->code == COMPOSITOR_BTN_TOUCH && !screen->slots)
                screen->id[0] = event->value ? 0 : -1;
}

// What a finger does, done as a finger: hit wide, and told so (WINDOW_KEY_TOUCH).
static void compositor_touch_button(unsigned int code, int down)
{
        compositor.touching = 1;
        compositor_press(code, down);
        compositor.touching = 0;
}

static void compositor_touch_motion(int x, int y, positive now)
{
        compositor_predict_place(COMPOSITOR_PREDICT_TOUCH, x, y, now);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor.touching = 1;
        compositor_motion();
        compositor.touching = 0;
}

/*
        A finger that went down on the close button and moved past the slop was not pressing it: the press the window would
        have had there without the button is made now, from where the finger went down (the pointer is still there, a
        finger's motion waits for the slop), so the corner it is in is resized and the titlebar it is on is taken to move, and
        what the finger does next is that.
*/
static void compositor_touch_resume(void)
{
        compositor.touch.resumed = 1;
        compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
        compositor.touch.resumed = 0;
}

// The left press taken back: a release that says it was no click, for a finger that became two or was held.
static void compositor_touch_cancel(void)
{
        compositor.touching = 1;
        compositor_release_as(COMPOSITOR_BTN_LEFT, 0, WINDOW_KEY_CANCEL);
        compositor.touching = 0;
}

// A click of the right button where the first finger went down, which is where the pointer still is.
static void compositor_touch_right(void)
{
        compositor_touch_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_touch_button(COMPOSITOR_BTN_RIGHT, 0);
}

/*
        The events of the touch recogniser (ui/touch.h) applied to the windows, the pointer and the buttons: what the fingers mean is
        the kit's, what they do here is the compositor's.
*/
static void compositor_touch_apply(unsigned int at, const struct ui_touch_out *out, positive now)
{
        int wheel = 0;

        for (unsigned int index = 0; index < out->count; index++)
        {
                const struct ui_touch_event *e = &out->event[index];

                switch (e->kind)
                {
                case UI_TOUCH_START:
                        compositor.touch.device = (int)at;
                        compositor.touch.hidden = 1;
                        compositor.touch.closing = COMPOSITOR_NONE;
                        compositor.touch.hold_row = COMPOSITOR_NONE;
                        compositor_touch_motion(e->x, e->y, now);
                        break;
                case UI_TOUCH_MOVE:
                        compositor_touch_motion(e->x, e->y, now);
                        break;
                case UI_TOUCH_PRESS:
                        compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
                        break;
                case UI_TOUCH_RELEASE:
                        compositor.touch.closing = COMPOSITOR_NONE;
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);
                        break;
                case UI_TOUCH_CANCEL:
                        compositor_touch_cancel();
                        break;
                case UI_TOUCH_CLOSE:
                        // Lifted unmoved on the close button it went down on: the window goes.
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        if (compositor.touch.closing >= 0)
                        {
                                compositor.press = COMPOSITOR_NONE;
                                compositor_close(compositor.touch.closing);
                        }
                        compositor.touch.closing = COMPOSITOR_NONE;
                        break;
                case UI_TOUCH_RESUME:
                        // A press on the X that moves is no press of it: the press it was under the X is made.
                        compositor.touch.closing = COMPOSITOR_NONE;
                        compositor_touch_resume();
                        break;
                case UI_TOUCH_RIGHT:
                        compositor_touch_right();
                        break;
                case UI_TOUCH_WHEEL:
                        compositor.touching = 1;
                        compositor_wheel(e->v120, e->horizontal);
                        wheel = 1;
                        break;
                case UI_TOUCH_HOLD:
                        // The left press taken back and the right button sent are both the long press's: the cancel carries WINDOW_KEY_HOLD too.
                        {
                                int tab;

                                compositor.touch.holding = 1;
                                compositor_touch_cancel();
                                compositor.touch.hold_row = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
                                compositor_touch_right();
                                compositor.touch.holding = 0;
                        }
                        break;
                case UI_TOUCH_HOLD_END:
                        // Lifted after a long press: the release that ends it, in the window it landed on (a terminal copies its selection).
                        if (compositor.touch.hold_row >= 0)
                                compositor_post(compositor.touch.hold_row, WINDOW_EVENT_BUTTON,
                                                WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL | WINDOW_KEY_HOLD, COMPOSITOR_BTN_LEFT, 0);
                        compositor.touch.hold_row = COMPOSITOR_NONE;
                        break;
                case UI_TOUCH_IDLE:
                        compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
                        break;
                }
        }
        if (wheel)
                compositor.touching = 0;
}

/*
        A report of a touch screen over: the marks follow the fingers, and then the kit's gesture is told the fingers as they
        are, begun on the first finger down (ui_touch_begin), and each report's events are applied as they come. A report of
        another touch screen waits until this one's gesture is over.
*/
static void compositor_touch_report(unsigned int at, positive now)
{
        struct compositor_touch_screen *screen = &compositor.screen_touch[at];
        struct compositor_touch *touch = &compositor.touch;
        struct ui_touch_frame frame;
        struct ui_touch_out out;

        // Events were lost: no finger is trusted, and what they were doing ends as if they were lifted, no click made.
        if (screen->dropped)
        {
                screen->dropped = 0;
                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                {
                        if (screen->id[slot] >= 0)
                                compositor_spot_lift(slot, now);
                        screen->id[slot] = screen->was[slot] = -1;
                }
                if (touch->gesture.mode != UI_TOUCH_NONE && touch->device == (int)at)
                {
                        ui_touch_drop(&touch->gesture, &out);
                        compositor_touch_apply(at, &out, now);
                }
                return;
        }
        if (touch->gesture.mode != UI_TOUCH_NONE && touch->device != (int)at)
        {
                memory_copy(screen->was, screen->id, sizeof(screen->was));
                return;
        }

        // The marks follow the fingers, and fade from those lifted.
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                {
                        int x, y;

                        compositor_touch_point(at, slot, &x, &y);
                        compositor_spot_set(slot, x, y);
                }
                else if (screen->was[slot] >= 0)
                        compositor_spot_lift(slot, now);

        memory_zero(&frame, sizeof(frame));
        memory_copy(frame.id, screen->id, sizeof(frame.id));
        memory_copy(frame.was, screen->was, sizeof(frame.was));
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                if (screen->id[slot] >= 0)
                        compositor_touch_point(at, slot, &frame.x[slot], &frame.y[slot]);
        frame.scale = compositor.scale;
        frame.now = now;
        frame.pointer_x = compositor.pointer_x, frame.pointer_y = compositor.pointer_y;
        frame.busy = compositor.drag >= 0 || compositor.resize >= 0;

        // The first finger down: the pointer goes to it, hidden, and its left press is made there (the kit's START and PRESS).
        ui_touch_begin(&touch->gesture, &frame, &out);
        compositor_touch_apply(at, &out, now);

        // The report as it is then: a press may have begun a drag, or marked a close, or moved the pointer to the landing.
        frame.pointer_x = compositor.pointer_x, frame.pointer_y = compositor.pointer_y;
        frame.busy = compositor.drag >= 0 || compositor.resize >= 0;
        ui_touch_report(&touch->gesture, &frame, &out);
        compositor_touch_apply(at, &out, now);

        memory_copy(screen->was, screen->id, sizeof(screen->was));
}

// When the first finger is a long press, or 0: the kit's time for it (a press still for UI_TOUCH_HOLD_NS, the left button down there).
static positive compositor_touch_due(void)
{
        return ui_touch_due(&compositor.touch.gesture, compositor.button_down & 1);
}

// The long press's time has come: the kit's hold, applied (the left press taken back and the right button held at the pointer).
static void compositor_touch_tick(positive now)
{
        struct ui_touch_out out;

        ui_touch_tick(&compositor.touch.gesture, now, compositor.button_down & 1, &out);
        compositor_touch_apply((unsigned int)compositor.touch.device, &out, now);
}

/* ---- end of touch ---- */

static void compositor_input_read(unsigned int at)
{
        struct compositor_input_event events[64];
        int moved = 0;

        for (;;)
        {
                bipolar got = system_read_once(compositor.inputs[at], events, sizeof(events));

                // A device that went away answers every poll at once, so it
                // is let go of rather than read in a loop.
                if (got < 0 && got != -COMPOSITOR_EAGAIN && got != -4)
                {
                        system_close(compositor.inputs[at]);
                        compositor.inputs[at] = -1;
                        compositor.table->input_number[at] = -1;
                        compositor.devices &= ~(1ul << compositor.device_number[at]);

                        // What it held down will never be let go of: not a key, not a button, not a finger.
                        compositor.held[at] = 0;
                        compositor_release(COMPOSITOR_BTN_LEFT, 0);
                        if (compositor.touchscreen[at] && compositor.touch.gesture.mode != UI_TOUCH_NONE &&
                            compositor.touch.device == (int)at)
                                ui_touch_reset(&compositor.touch.gesture);
                        if (compositor.touchscreen[at])
                                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                                        compositor_spot_lift(slot, clock_monotonic_nanoseconds());
                        compositor.touchscreen[at] = 0;
                }

                if (got <= 0)
                        break;

                // It woke the loop, and that was all it was for.
                if (compositor.wakeonly[at])
                        continue;

                for (unsigned int i = 0; i < (unsigned int)got / sizeof(events[0]); i++)
                {
                        struct compositor_input_event *event = &events[i];
                        positive now = (positive)event->seconds * 1000000000ull + (positive)event->microseconds * 1000ull;

                        if ((event->type == COMPOSITOR_EV_REL && event->code < 2) || event->type == COMPOSITOR_EV_ABS)
                                compositor.motion_ts = compositor.motion_ts ? compositor.motion_ts : now;

                        // A touch screen's events are its fingers', taken when its report is over.
                        if (compositor.touchscreen[at])
                        {
                                if (event->type == COMPOSITOR_EV_SYN && event->code == 0)
                                        compositor_touch_report(at, now);
                                else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                                {
                                        compositor.table->input_dropped[at]++;
                                        compositor.screen_touch[at].dropped = 1;
                                }
                                else
                                        compositor_touch_event(at, event);
                                continue;
                        }

                        if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_X)
                        {
                                compositor.raw_x = compositor_counts(compositor.raw_x, event->value);
                                if (compositor_shaken(event->value, now))
                                        compositor.cursor.magnify = 1;
                        }
                        else if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_Y)
                                compositor.raw_y = compositor_counts(compositor.raw_y, event->value);
                        else if (event->type == COMPOSITOR_EV_REL &&
                                 (event->code == COMPOSITOR_REL_WHEEL || event->code == COMPOSITOR_REL_HWHEEL ||
                                  event->code == COMPOSITOR_REL_WHEEL_HI || event->code == COMPOSITOR_REL_HWHEEL_HI))
                        {
                                int fine = event->code == COMPOSITOR_REL_WHEEL_HI || event->code == COMPOSITOR_REL_HWHEEL_HI;
                                int sideways = event->code == COMPOSITOR_REL_HWHEEL || event->code == COMPOSITOR_REL_HWHEEL_HI;

                                compositor.table->input_wheel_read[at]++;
                                compositor.table->input_wheel_last[at] = now;
                                // A wheel that reports in 120ths is read from those, and its notches are not counted again.
                                if (fine || !(compositor.hires[at] >> sideways & 1))
                                {
                                        compositor.table->input_wheel_used[at]++;
                                        compositor_wheel(compositor_counts(0, fine ? (long long)event->value : (long long)event->value * 120),
                                                         sideways);
                                }
                        }
                        else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                        {
                                compositor.table->input_dropped[at]++;
                                // What the kernel dropped may be a lift and a touch somewhere else: the touchpad's
                                // next report says where the finger is, and is no move from where it was before.
                                if (compositor.touchpad[at])
                                {
                                        compositor.pad[at].moving = 0;
                                        compositor.pad[at].left_x = compositor.pad[at].left_y = 0;
                                }
                        }
                        else if (event->type == COMPOSITOR_EV_ABS && event->code < 2 && compositor.touchpad[at])
                        {
                                if (event->code)
                                        compositor.pad[at].next_y = event->value;
                                else
                                        compositor.pad[at].next_x = event->value;
                        }
                        else if (event->type == COMPOSITOR_EV_ABS && event->code < 2)
                        {
                                int *range = event->code ? compositor.absolute_y[at] : compositor.absolute_x[at];
                                int place = compositor_tablet(event->value, range[0], range[1],
                                                              event->code ? compositor.height : compositor.width);

                                if (event->code)
                                        compositor.tablet_y = place;
                                else
                                        compositor.tablet_x = place;
                                compositor.tablet_have |= event->code ? 2 : 1;
                        }
                        else if (event->type == COMPOSITOR_EV_SYN && event->code == 0)
                        {
                                if (compositor.touchpad[at])
                                        compositor_pad_apply(at, now);
                                moved |= compositor_report(now, compositor_stamp(event, now));
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && compositor.touchpad[at] &&
                                 compositor_pad_finger_key(event->code))
                                compositor_pad_key(&compositor.pad[at], event->code, event->value);
                        else if (event->type == COMPOSITOR_EV_KEY && event->code == COMPOSITOR_BTN_TOOL_PEN && !event->value &&
                                 !compositor.touchpad[at] && !compositor.touchscreen[at])
                                compositor_predict_clear(COMPOSITOR_PREDICT_PEN);
                        else if (event->type == COMPOSITOR_EV_KEY &&
                                 ((event->code >= COMPOSITOR_BTN_LEFT && event->code <= COMPOSITOR_BTN_TASK) ||
                                  event->code == COMPOSITOR_BTN_TOUCH))
                        {
                                // A touch is the left button (a touchpad's is no press, above); the others are where
                                // they are, but a touchpad's left pressed with two fingers on it, which is its right.
                                unsigned int code = event->code == COMPOSITOR_BTN_TOUCH ? COMPOSITOR_BTN_LEFT : event->code;

                                if (compositor.touchpad[at] && code == COMPOSITOR_BTN_LEFT)
                                {
                                        if (event->value)
                                                compositor.pad[at].clicked = compositor.pad[at].fingers >= 2 ? COMPOSITOR_BTN_RIGHT
                                                                                                             : COMPOSITOR_BTN_LEFT;
                                        code = compositor.pad[at].clicked ? compositor.pad[at].clicked : code;
                                }
                                moved |= compositor_report(now, compositor_stamp(event, now));
                                if (moved)
                                        compositor_motion(), moved = 0;
                                compositor_press(code, event->value != 0);
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code < 256)
                                compositor_key(at, event->code, event->value, now);
                }
        }

        if (moved)
                compositor_motion();

        if (!compositor.wakeonly[at])
                compositor_hold_touch(clock_monotonic_nanoseconds());
}

/*
        The table as a memfd, WINDOWS_TABLE a link to it in this process's
        /proc: what udmabuf makes a window's buffer a dma-buf of, so the card
        can show the window straight from where its program drew it. A program
        opens and maps it as it did the file, at the same path, which reopens
        the memfd (a memfd cannot be bound over a path: it lives on the
        kernel's own mount, which no namespace has). Opening it through /proc
        takes the right to look at this process, which root has; any program
        takes it from /dev/spark instead (WINDOWS_IOCTL_TABLE_GET), which is
        given it here, and the link is for where the device is not. It cannot
        shrink or grow (udmabuf's condition). The descriptor, or an error, and then the table is the
        plain file and every frame is copied (a headless compositor).
*/
static long compositor_table_memfd(unsigned long bytes)
{
        p8 path[48] = "/proc/";
        long handle = compositor_call(syscall(memfd_create), (positive) "moonwater-windows", 1 | 2, 0, 0, 0, 0);
        positive at = 6;

        if (handle < 0)
        {
                compositor.memfd_failed = 6;
                return handle;
        }
        // /proc/PID/fd/N, this process's own number and not self, which is whoever opens it.
        at += positive_into_string(path + at, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0));
        memory_copy(path + at, "/fd/", 4);
        at += 4;
        path[at + positive_into_string(path + at, (positive)handle)] = 0;
        // MFD_CLOEXEC | MFD_ALLOW_SEALING above; F_ADD_SEALS of F_SEAL_SEAL, F_SEAL_SHRINK and F_SEAL_GROW.
        compositor.memfd_failed = compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0 ? 8
                                  : compositor_call(syscall(fcntl), (positive)handle, 1033, 1 | 2 | 4, 0, 0, 0) < 0 ? 9
                                  : compositor_call(syscall(fchmod), (positive)handle, WINDOWS_MODE, 0, 0, 0, 0) < 0 ? 10
                                  : compositor_call(syscall(symlinkat), (positive)path, (positive)AT_FDCWD,
                                                    (positive)WINDOWS_TABLE, 0, 0, 0) < 0
                                      ? 11
                                      : 0;
        if (compositor.memfd_failed)
        {
                system_close(handle);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);
                return -COMPOSITOR_EINVAL;
        }
        return handle;
}

/*
        What the table says of the screens, at the start and whenever they change: the list, the primary first, each
        one's rectangle and connector name, and that it changed (layout).
*/
/*
        What a screen's displays line asks for, and what the compositor did not do of it, for the table (WINDOWS_REFUSED_*):
        the rate asked, when the mode it is at is not that rate; the turn asked, when it is not turned; and a place whose x or
        y the file does not read. A screen with no line asks for nothing and is refused nothing.
*/
static void compositor_table_refusals(struct windows *table, unsigned int at, const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name, windows_name_length(name));
        unsigned int refused = 0;

        table->output_asked_hz[at] = display ? display->refresh : 0;
        table->output_asked_turn[at] = display ? display->rotation : 0;
        table->output_asked_scale[at] = display ? display->scale : WINDOWS_SCALE_DEFAULT;
        if (display && display->refresh && compositor.output[at].mode.vrefresh != display->refresh)
                refused |= WINDOWS_REFUSED_RATE;
        if (display && display->rotation && !compositor.output[at].turned)
                refused |= WINDOWS_REFUSED_TURN;
        if (display && (display->bad & (WINDOWS_DISPLAY_X | WINDOWS_DISPLAY_Y)))
                refused |= WINDOWS_REFUSED_PLACE;
        table->output_refused[at] = refused;
}

static void compositor_table_outputs(void)
{
        struct windows *table = compositor.table;
        int count = compositor_output_count();

        if (!table)
                return;
        for (int at = 0; at < count && at < WINDOWS_OUTPUTS; at++)
        {
                int x, y, width, height;
                const char *name = compositor_output_name(at);
                unsigned int length = 0;

                compositor_output_rect(at, &x, &y, &width, &height);
                table->output_x[at] = x;
                table->output_y[at] = y;
                table->output_w[at] = (unsigned int)width;
                table->output_h[at] = (unsigned int)height;
                while (name[length] && length < WINDOWS_OUTPUT_NAME - 1)
                        table->output_name[at][length] = name[length], length++;
                memory_zero(table->output_name[at] + length, WINDOWS_OUTPUT_NAME - length);
                memory_copy(table->output_model[at], compositor.output[at].model, WINDOWS_OUTPUT_MODEL);
                table->output_now[at] = compositor.output[at].mode.vrefresh;
                table->output_rates[at] = compositor.output[at].hz_count;
                memory_copy(table->output_hz[at], compositor.output[at].hz, sizeof(table->output_hz[at]));
                table->output_turn[at] = compositor.output[at].turn;
                table->output_software[at] = compositor.output[at].turn_software;
                compositor_table_refusals(table, at, name);
        }
        // The largest a window may be follows the screens, inside the room its buffer has for the session.
        table->output_width = (unsigned int)compositor_most_width();
        table->output_height = (unsigned int)compositor_most_height();
        table->layout = compositor.layout;
        __atomic_store_n(&table->outputs, (unsigned int)count, __ATOMIC_RELEASE);
}

/*
        The table and its doors, made new: whatever is at the paths is from a
        compositor whose session is over. The table is sparse, so its size is
        the most every window could draw and what it costs is the pages that
        are drawn. The table is the compositor's memfd and the doors are
        root's (WINDOWS_DOOR_MODE): a window is a program a desktop session
        started, which is root, and a door the whole machine could write is a
        door the whole machine could hold open against the row it names.
*/
static int compositor_table(void)
{
        unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
        // Every window's buffer as large as the largest window may ever be, a row of it stride pixels: for the session,
        // as the table is sealed at its size and mapped by every window.
        unsigned long buffer = compositor_buffer_room();
        unsigned long bytes = header + buffer * 2 * WINDOWS;
        long handle, mapped;
        p8 name[48];

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run", 0755, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);

        handle = compositor.card >= 0 ? compositor_table_memfd(bytes) : -COMPOSITOR_EINVAL;
        compositor.table_memfd = handle >= 0 ? (int)handle + 1 : 0;
        if (handle >= 0)
        {
                long spark = (long)system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);

                if (spark >= 0 && system_control(spark, WINDOWS_IOCTL_TABLE_SET, handle) == 0)
                        compositor.table_giver = (int)spark + 1;
                else if (spark >= 0)
                        system_close(spark);
        }
        if (handle < 0)
        {
                handle = (long)system_open_at_mode(AT_FDCWD, (string_address)WINDOWS_TABLE,
                                                   FILE_READ_WRITE | O_CREAT | O_EXCL | O_CLOEXEC, WINDOWS_MODE);
                if (handle < 0)
                        return (int)handle;
                if (compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0)
                {
                        system_close(handle);
                        return -COMPOSITOR_ENOSPC;
                }
        }
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 1, (positive)handle, 0);
        // The memfd is kept: udmabuf is given it, buffer by buffer.
        if (!compositor.table_memfd)
                system_close(handle);
        if (mapped < 0 && mapped > -4096)
                return (int)mapped;
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, WINDOWS_MODE, 0, 0, 0);

        compositor.table = (struct windows *)mapped;
        compositor.pixels = header;
        compositor.buffer = buffer;
        compositor.table->output_width = (unsigned int)compositor_most_width();
        compositor.table->output_height = (unsigned int)compositor_most_height();
        compositor.table->stride = (unsigned int)compositor_stride();
        compositor.table->scale = (unsigned int)compositor.scale; // the kit draws in percent of the logical size
        compositor.table->period = (unsigned int)compositor.period;
        compositor.table->focus = WINDOWS;
        compositor.table->refused = compositor.memfd_failed;
        compositor.table->pixels = header;
        compositor.table->buffer = buffer;
        compositor_table_outputs();

        // The door is read here and the wake written, each end kept for the session; S_IFIFO.
        for (unsigned int row = 0; row < WINDOWS; row++)
                for (int wake = 0; wake < 2; wake++)
                {
                        windows_door_name((char *)name, row, wake);
                        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
                        compositor_call(syscall(mknodat), (positive)AT_FDCWD, (positive)name, 0010000 | WINDOWS_MODE, 0, 0, 0);
                        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)name, WINDOWS_MODE, 0, 0, 0);
                        handle = (long)system_open_at(AT_FDCWD, name,
                                                      (wake ? FILE_READ_WRITE : FILE_READ) | O_NONBLOCK | O_CLOEXEC);
                        if (handle < 0)
                                return (int)handle;
                        if (wake)
                                compositor.wake[row] = (int)handle;
                        else
                                compositor.door[row] = (int)handle;
                }
        if (compositor_clip_start() < 0)
                return -1;

        return 0;
}

// The table and the doors gone with the session: a program that looks finds no compositor.
static void compositor_table_drop(void)
{
        p8 name[48];

        if (compositor.table_giver)
        {
                system_control(compositor.table_giver - 1, WINDOWS_IOCTL_TABLE_SET, -1l);
                system_close(compositor.table_giver - 1);
                compositor.table_giver = 0;
        }
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0, 0, 0, 0);
        for (unsigned int row = 0; row < WINDOWS; row++)
                for (int wake = 0; wake < 2; wake++)
                {
                        windows_door_name((char *)name, row, wake);
                        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
                }
}

// SIGTERM, SIGINT and SIGHUP, as a descriptor the loop polls.
static int compositor_signals(void)
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1);

        compositor_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        return (int)compositor_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8,
                                    COMPOSITOR_NONBLOCK | COMPOSITOR_CLOEXEC, 0, 0);
}

static void compositor_say(string_address what, bipolar value)
{
        string_format(log, "compositor: %s %b\n", what, value);
        log_flush();
}

static void compositor_screens_say(void);

/*
        With no card the one screen is named Canvas (what the shim says it is called) and its displays line can ask for a
        scale, which is taken as a card's screen takes one: the desktop's scale, the table's, the bands and every pixel. A
        screen with no line is drawn at the scale the program was started with (-s).
*/
#define COMPOSITOR_HEADLESS_NAME "Canvas"

static void compositor_headless_scale(void)
{
        static int started;
        const struct windows_display *display;
        int scale;

        if (!started)
                started = compositor.scale;
        compositor_displays_read(0);
        display = windows_display_of(compositor_display, compositor_displays, COMPOSITOR_HEADLESS_NAME,
                                     windows_name_length(COMPOSITOR_HEADLESS_NAME));
        scale = display && display->scale ? (int)display->scale : started;
        if (scale == compositor.scale)
                return;
        compositor.scale = scale;
        if (compositor.table)
                compositor.table->scale = (unsigned int)scale;
        compositor.cursor.plane_scale = 0;
        compositor_cursor_move();
        compositor_band_room();
        compositor_scale_fit();
        compositor_tabs_changed();
        compositor_damage(0, 0, compositor.width, compositor.height);
}

// The screens taken again (a screen came or went, the arrangement changed), and said in the log when they changed.
static void compositor_space_retake(void)
{
        unsigned int layout = compositor.layout;
        b32 status = 0;

        if (compositor.card >= 0)
                status = compositor_space_take(0);
        else
                compositor_headless_scale();

        if (status < 0)
                compositor_say("the screens could not be taken again, error", status);
        else if (compositor.layout != layout)
                compositor_screens_say();
}

/*
        A screen plugged in or out. The kernel says so in a uevent of the drm
        subsystem -- HOTPLUG=1 on the card, a connector's change -- heard on a
        socket of NETLINK_KOBJECT_UEVENT, and the screens are taken again. A
        uevent is only a wake-up and is never believed further: any of the
        drm subsystem's is a reason to look, GETCONNECTOR asks every connector
        the card has, and taking the screens again when none changed is
        nothing (compositor_space_take). With no socket to be had the screens
        are the ones the desktop started with.
*/
#define COMPOSITOR_UEVENT 15 // NETLINK_KOBJECT_UEVENT

static int compositor_uevents_open(void)
{
        bipolar handle = socket_new(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, COMPOSITOR_UEVENT);
        socket_address_netlink self = {.family = AF_NETLINK, .groups = 1};

        if (handle < 0)
                return -1;
        if (socket_bind((b32)handle, address_of self, sizeof(self)) < 0)
        {
                system_close(handle);
                return -1;
        }
        return (int)handle;
}

// Whether a uevent, its lines KEY=VALUE each ended by a null, is the drm subsystem's.
static int compositor_uevent_drm(const char *message, long length)
{
        static const char want[] = "SUBSYSTEM=drm";

        for (long at = 0; at < length;)
        {
                long line = at;

                while (at < length && message[at])
                        at++;
                if (at - line == (long)sizeof(want) - 1 && !memory_compare(message + line, want, sizeof(want) - 1))
                        return 1;
                at++;
        }
        return 0;
}

// What the socket has, read to its end (a bounded number of messages a pass); the screens looked at once for a burst.
static void compositor_uevents_read(void)
{
        char message[2048];
        int drm = 0;

        for (int reads = 0; reads < 64; reads++)
        {
                bipolar got = socket_receive((b32)compositor.uevents, message, sizeof(message), MSG_DONTWAIT, 0, 0);

                if (got <= 0)
                        break;
                drm |= compositor_uevent_drm(message, got);
        }
        if (drm)
                compositor_space_retake();
}

/*
        The card back to Canvas.

        The scanout's mapping holds the card's file open as surely as the
        descriptor does, so both go before the last close -- which is what
        runs Canvas's restore hook. The kernel says in its log when the card
        is Canvas's again.
*/
static void compositor_hand_back(void)
{
        compositor_call(syscall(munmap), (positive)compositor.scanout, compositor.scanout_bytes, 0, 0, 0, 0);
        system_close(compositor.card);
}


/*
        No card: the scanout is a file the program maps, which is what a test
        reads the picture out of, and the card's calls fail on a descriptor
        that is not there and are not looked at. -H WIDTHxHEIGHT FILE.
*/
static b32 compositor_headless(string_address size, string_address path)
{
        long width = 0, height = 0, handle, bytes;

        while (*size >= '0' && *size <= '9')
                width = width * 10 + (*size++ - '0');
        if (*size++ != 'x')
                return -COMPOSITOR_EINVAL;
        while (*size >= '0' && *size <= '9')
                height = height * 10 + (*size++ - '0');
        if (width < 64 || height < 64 || width > 16384 || height > 16384)
                return -COMPOSITOR_EINVAL;

        bytes = width * height * 4;
        handle = (long)system_open_at_mode(AT_FDCWD, path, FILE_READ_WRITE | O_CREAT | O_CLOEXEC, 0644);
        if (handle < 0 || compositor_call(syscall(ftruncate), (positive)handle, (positive)bytes, 0, 0, 0, 0) < 0)
                return -COMPOSITOR_EINVAL;

        handle = compositor_call(syscall(mmap), 0, (positive)bytes, 3, 1, (positive)handle, 0);
        if (handle < 0 && handle > -4096)
                return -COMPOSITOR_ENOSPC;

        compositor.card = -1;
        compositor.scanout = (unsigned int *)handle;
        compositor.scanout_bytes = (unsigned long)bytes;
        compositor.scanout_pitch = (unsigned long)width * 4;
        compositor.width = (int)width;
        compositor.height = (int)height;
        compositor.period = 1000000000ull / 60;
        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_present();
        return 0;
}

static b32 compositor_option(string_address argument, char letter)
{
        return argument[0] == '-' && argument[1] == letter && !argument[2];
}

// A decimal number, or with hex a hexadecimal one, as far as it goes.
static bipolar compositor_number(string_address text, b32 hex)
{
        bipolar value = 0, sign = 1;

        if (*text == '-')
                sign = -1, text++;

        for (; *text; text++)
        {
                b32 digit = *text >= '0' && *text <= '9' ? *text - '0'
                            : hex && (*text | 32) >= 'a' && (*text | 32) <= 'f' ? (*text | 32) - 'a' + 10
                                                                                : -1;

                if (digit < 0)
                        break;
                value = value * (hex ? 16 : 10) + digit;
        }
        return value * sign;
}

// A file, mapped to be read for good: a font.
static positive compositor_map_file(string_address path, positive address_to size)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        long bytes, mapped;

        if (handle < 0)
                return 0;

        bytes = compositor_call(syscall(lseek), (positive)handle, 0, 2, 0, 0, 0);
        mapped = bytes > 0 ? compositor_call(syscall(mmap), 0, (positive)bytes, 1, 2, (positive)handle, 0) : -1;
        system_close(handle);

        if (mapped < 0 && mapped > -4096)
                return 0;

        address_to size = (positive)bytes;
        return (positive)mapped;
}

/*
        The faces behind the chain every engine draws with: Noto Sans Arabic and Noto Sans Hebrew, each regular and bold, which the
        font engine reaches for when the chain has no glyph for a code point (font_fallback). Mapped once for the program, by the
        first engine that asks, and put behind each engine after that; a file the image does not have leaves the chain as it was.
*/
static void compositor_fallbacks(font_engine address_to engine)
{
        static const string_address paths[4] = {"/usr/share/fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf",
                                                "/usr/share/fonts/noto-sans-arabic/NotoSansArabic-Bold.ttf",
                                                "/usr/share/fonts/noto-sans-hebrew/NotoSansHebrew-Regular.ttf",
                                                "/usr/share/fonts/noto-sans-hebrew/NotoSansHebrew-Bold.ttf"};
        static font_face faces[4];
        static b32 loaded[4], tried;

        if (!tried)
        {
                tried = 1;
                for (int at = 0; at < 4; at++)
                {
                        positive size = 0, bytes = compositor_map_file(paths[at], address_of size);

                        loaded[at] = bytes && font_load(&faces[at], (const p8 address_to)bytes, size, 0) == 0;
                }
        }
        for (int pair = 0; pair < 2; pair++)
                if (loaded[2 * pair])
                        font_fallback(engine, &faces[2 * pair], loaded[2 * pair + 1] ? &faces[2 * pair + 1] : null);
}

// The face the tabs are written in and the band they are drawn into; without one the tabs are shapes.
static void compositor_font(string_address path)
{
        static p8 atlas[128 * 128];
        positive size = 0, bytes = path && MOONWATER_UI_TASKBAR ? compositor_map_file(path, address_of size) : 0;
        unsigned long band_bytes = 0, glass_bytes = 0;
        long engine, band, glass;

        if (!bytes)
                return;

        // For the largest screen, which any screen the band is on fits in.
        taskbar_bytes(compositor_most_width(), compositor_most_height(), compositor.scale, &band_bytes, &glass_bytes);
        engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);
        band = compositor_call(syscall(mmap), 0, (positive)band_bytes, 3, 0x22, (positive)-1, 0);
        // The glass: the band and the blur's reach on every side, for a band on any edge.
        compositor.glass_bytes = glass_bytes;
        glass = compositor_call(syscall(mmap), 0, compositor.glass_bytes, 3, 0x22, (positive)-1, 0);
        if ((engine < 0 && engine > -4096) || (band < 0 && band > -4096) || (glass < 0 && glass > -4096) ||
            font_load(&compositor.face, (const p8 address_to)bytes, size, 0) != 0)
        {
                compositor_say("no font for the tabs, error", -COMPOSITOR_EINVAL);
                return;
        }

        compositor.engine = (font_engine address_to)engine;
        font_start(compositor.engine, atlas, 128, 128);
        compositor_fallbacks(compositor.engine);
        compositor.band = (unsigned int *)band;
        compositor.band_bytes = band_bytes;
        compositor.glass = (unsigned int *)glass;
        taskbar_changed();
        // The other screens' bands, where `taskbar display all` asks for them.
        compositor_band_room();
}

// The screens, a line each, the primary first: what the log says of the space whenever it is taken.
static void compositor_screens_say(void)
{
        for (int at = 0; at < compositor_output_count(); at++)
        {
                int x, y, width, height;

                compositor_output_rect(at, &x, &y, &width, &height);
                string_format(log, "compositor: screen %b %s %b by %b at %b,%b\n", (bipolar)at,
                              compositor_output_name(at)[0] ? (string_address)compositor_output_name(at) : (string_address) "-",
                              (bipolar)width, (bipolar)height, (bipolar)x, (bipolar)y);
        }
        log_flush();
}

/*
        The bands' memory for the screens they are on now: each band's pixels
        made again when its screen wants more than they have (a larger screen
        became the primary), a band on each other screen made with `taskbar
        display all` and given back without it, and the glass, which every
        band is drawn through in turn, as large as the largest wants. With no
        memory for the primary's band or the glass every tab is a shape, as
        with no font; with none for another band that band's are: a band
        larger than its memory would be read past its end.
*/
static unsigned int *compositor_remap(unsigned int *was, unsigned long had, unsigned long bytes)
{
        long mapped = compositor_call(syscall(mmap), 0, (positive)bytes, 3, 0x22, (positive)-1, 0);

        if (was)
                compositor_call(syscall(munmap), (positive)was, had, 0, 0, 0, 0);
        return mapped < 0 && mapped > -4096 ? 0 : (unsigned int *)mapped;
}

static void compositor_band_room(void)
{
        unsigned long glass_most = 0;
        int bands = compositor_bands();

        if (!compositor.band)
                return;
        for (int at = 0; at < WINDOWS_OUTPUTS; at++)
        {
                unsigned int **pixels = at ? &compositor.band_more[at] : &compositor.band;
                unsigned long *had = at ? &compositor.band_more_bytes[at] : &compositor.band_bytes;
                unsigned long band_bytes = 0, glass_bytes = 0;
                int x, y, width, height;

                if (at >= bands)
                {
                        if (*pixels)
                                compositor_call(syscall(munmap), (positive)*pixels, *had, 0, 0, 0, 0);
                        *pixels = 0;
                        *had = 0;
                        continue;
                }
                compositor_output_rect(at, &x, &y, &width, &height);
                taskbar_bytes(width, height, compositor.scale, &band_bytes, &glass_bytes);
                glass_most = glass_bytes > glass_most ? glass_bytes : glass_most;
                if (band_bytes <= *had)
                        continue;
                *pixels = compositor_remap(*pixels, *had, band_bytes);
                *had = *pixels ? band_bytes : 0;
                taskbar_changed();
                if (!at && !compositor.band)
                        break;
        }
        if (compositor.band && glass_most > compositor.glass_bytes)
        {
                compositor.glass = compositor_remap(compositor.glass, compositor.glass_bytes, glass_most);
                compositor.glass_bytes = compositor.glass ? glass_most : 0;
        }
        // No primary band, or no glass to draw any band through: every band's tabs are shapes.
        if (!compositor.band || !compositor.glass)
        {
                for (int at = 0; at < WINDOWS_OUTPUTS; at++)
                {
                        unsigned int **pixels = at ? &compositor.band_more[at] : &compositor.band;
                        unsigned long *had = at ? &compositor.band_more_bytes[at] : &compositor.band_bytes;

                        if (*pixels)
                                compositor_call(syscall(munmap), (positive)*pixels, *had, 0, 0, 0, 0);
                        *pixels = 0;
                        *had = 0;
                }
                if (compositor.glass)
                        compositor_call(syscall(munmap), (positive)compositor.glass, compositor.glass_bytes, 0, 0, 0, 0);
                compositor.glass = 0;
                compositor.glass_bytes = 0;
                taskbar_changed();
        }
}

/*
        compositor [-s SCALE] [-l LAYOUT] [-t] [-f FONT.ttf] [-H WIDTHxHEIGHT FILE] [-i FILE]

        Takes the card, or with -H maps FILE as the screen instead and takes
        none. -s is the desktop scale when Canvas is not there to say, -t puts
        the tabs on the top edge and not the bottom, -f is the face the tabs'
        titles and the time are written in (without one the tabs are only
        shapes), -l is the keyboard layout when Canvas is not there to say (it
        is otherwise the one `moonwater keyboard` set, and follows it when it
        is set again), and -i reads one more input device, a file of evdev
        records: what a test types and points with when there is no hardware
        to do it.
*/
#ifdef CONFIG_MOONWATER_WAYLAND
// The Wayland shim: the protocol (wayland.c) and the compositor's side of it, the socket and the host (wayland_host.c).
#include "wayland.c"
#include "wayland_host.c"
#endif

static b32 compositor_main()
{
        b32 status;
        int running = 1, extra = -1;
        string_address headless = null, file = null, font = null;

        claim_standard_descriptors();

        memory_zero(&compositor, sizeof(compositor));
        compositor.watch = compositor.uevents = -1;
        compositor.hold = -1;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = compositor.told_row = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.lit_row = compositor.preview_row = COMPOSITOR_NONE;
        compositor.mark_row = compositor.mark_with = compositor.shared_row = COMPOSITOR_NONE;
        compositor.touch.closing = COMPOSITOR_NONE;

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (compositor_option(option, 't'))
                        taskbar_position_default(TASKBAR_TOP);
                else if (at + 1 < program_argument_count() && compositor_option(option, 's'))
                        compositor.scale = (int)compositor_number(program_argument(++at), 0) * 100;
                else if (at + 1 < program_argument_count() && compositor_option(option, 'f'))
                        font = program_argument(++at);
#ifdef MOONWATER_GPU_C
                else if (compositor_option(option, 'G'))
                        compositor_gpu_state.force = 1;
#endif
                else if (at + 2 < program_argument_count() && compositor_option(option, 'H'))
                {
                        headless = program_argument(at + 1);
                        file = program_argument(at + 2);
                        at += 2;
                }
                else if (at + 1 < program_argument_count() && compositor_option(option, 'l'))
                {
                        int layout = compositor_layout_index((const char *)program_argument(++at));

                        if (layout >= 0)
                                compositor.keymap.layout = (unsigned char)layout, compositor.layout_given = 1;
                }
                else if (at + 1 < program_argument_count() && compositor_option(option, 'i'))
                        extra = (int)system_open_at(AT_FDCWD, program_argument(++at),
                                                    FILE_READ | O_CLOEXEC | O_NONBLOCK);
        }

        compositor_keys_read();
        if (compositor.scale < 25 || compositor.scale > 800)
                compositor.scale = WINDOWS_SCALE_DEFAULT;
        if (!headless && !compositor.layout_given)
                compositor_layout_read();

        status = headless ? compositor_headless(headless, file) : compositor_card_open();
        if (status < 0)
        {
                compositor_say("cannot take the display, error", status);
                return 1;
        }

        if (!headless)
        {
                compositor_cursor_start();
                compositor.uevents = compositor_uevents_open();
        }
        compositor_inputs_open();
        if (extra >= 0 && compositor.input_count < COMPOSITOR_INPUTS)
        {
                compositor.inputs[compositor.input_count] = extra;
                compositor.device_number[compositor.input_count++] = COMPOSITOR_DEVICES - 1;
        }
        // The middle of the primary screen.
        {
                int x, y, width, height;

                compositor_output_rect(0, &x, &y, &width, &height);
                compositor.real_x = compositor.kept_x = x + width / 2;
                compositor.real_y = compositor.kept_y = y + height / 2;
                compositor.owner = COMPOSITOR_PREDICT_MOUSE;
                compositor_pointer_shown();
        }
        compositor_look_read();
        compositor_input_settings_read();
        compositor_cursor_move();
        compositor_font(font);

        compositor.signals = compositor_signals();
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
        compositor.on_screen = compositor.fb;
        status = compositor_table();
        if (status < 0 || compositor.signals < 0)
        {
                compositor_say("cannot make the window table, error", status < 0 ? status : compositor.signals);
                if (!headless)
                        compositor_hand_back();
                return 1;
        }
        if (headless)
                compositor_headless_scale();
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_start();
#endif
        // The theme goes in the table it now has.
        compositor_theme_read();
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
                compositor_input_publish(at);

        string_format(log, "compositor: %b by %b (scale %b), %p input devices, pointer %s, windows in %s\n",
                      (bipolar)compositor.width, (bipolar)compositor.height,
                      (bipolar)compositor.scale, (positive)compositor.input_count,
                      compositor.cursor.soft ? "drawn" : compositor.cursor.handle[0] ? "on the plane" : "none",
                      WINDOWS_TABLE);
        compositor_screens_say();

        compositor.asleep = compositor_slept_clock();

        while (running)
        {
                // The Wayland shim's: its socket, its clients, its transfers' knock and the dma-bufs its stalled clients wait for.
                struct compositor_poll wanted[5 + COMPOSITOR_INPUTS + WINDOWS + 1 + 32 + 1 + 32];
                unsigned int count = 0, inputs, doors, card_at = 0, uevents_at = 0, clip_at = 0, wayland_at = 0;
                positive now = 0, until = 0;
                long awaited, wait[2];

                wanted[count++] = (struct compositor_poll){compositor.signals, COMPOSITOR_POLLIN, 0};
                wanted[count++] = (struct compositor_poll){compositor.watch, COMPOSITOR_POLLIN, 0};
                inputs = count;
                for (unsigned int at = 0; at < compositor.input_count; at++)
                        wanted[count++] = (struct compositor_poll){compositor.inputs[at], COMPOSITOR_POLLIN, 0};
                doors = count;
                for (unsigned int row = 0; row < WINDOWS; row++)
                        wanted[count++] = (struct compositor_poll){compositor.door[row], COMPOSITOR_POLLIN, 0};
                // A flip in flight: its event says the card has let go of the buffer it showed before.
                if (compositor.flip_pending)
                {
                        card_at = count;
                        wanted[count++] = (struct compositor_poll){compositor.card, COMPOSITOR_POLLIN, 0};
                }
                if (compositor.uevents >= 0)
                {
                        uevents_at = count;
                        wanted[count++] = (struct compositor_poll){compositor.uevents, COMPOSITOR_POLLIN, 0};
                }
                if (compositor_clip.door >= 0)
                {
                        clip_at = count;
                        wanted[count++] = (struct compositor_poll){compositor_clip.door, COMPOSITOR_POLLIN, 0};
                }
#ifdef CONFIG_MOONWATER_WAYLAND
                wayland_at = count;
                count = compositor_wl_poll(wanted, count);
#endif

                // With nothing to show or time the wait is for ever. With
                // something to show it is until a frame has gone by since the
                // last one went out, so a program that draws faster than the
                // card can scan costs one composition a frame and not one a
                // picture. The time in the tray is drawn at the next minute.
                if (compositor.wake_stage)
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor.wake_at < until ? compositor.wake_at : until;
                }

                if (compositor.hold >= 0)
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor.hold_until < until ? compositor.hold_until : until;
                }

                if (compositor.cursor.magnified)
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor.cursor.magnified_until < until ? compositor.cursor.magnified_until
                                                                                     : until;
                }

                // The sound server's volume, looked at for the tray's cell, while there is a band to show it on.
                if (compositor.band)
                {
                        positive due = compositor_volume.look_at ? compositor_volume.look_at : 1; // 1: not yet looked, so now

                        now = clock_monotonic_nanoseconds();
                        until = !until || due < until ? due : until;
                }

                // A lead is cleared a frame after its newest place, so the loop wakes for it.
                if (compositor_predict_due())
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor_predict_due() < until ? compositor_predict_due() : until;
                }

                // A finger held still is the right button when its time is up; a finger's mark fades a frame at a time.
                // Neither wakes the loop while no finger is down and no mark is fading.
                if (compositor_touch_due())
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor_touch_due() < until ? compositor_touch_due() : until;
                }
                // A drag's pointer resting in the top band brings the grid up when its time is up, with no motion.
                if (compositor_snap_due())
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor_snap_due() < until ? compositor_snap_due() : until;
                }
                if (compositor_spots_any(1))
                {
                        positive period = compositor.period ? compositor.period : 16666667ull;
                        positive frame = compositor.presented + period;

                        now = clock_monotonic_nanoseconds();
                        frame = frame > now ? frame : now + period;
                        until = !until || frame < until ? frame : until;
                }

                if (compositor.band || compositor.theme_word == 2)
                {
                        timespec real = {0, 0};
                        positive minute;

                        clock_gettime(CLOCK_REALTIME, address_of real);
                        now = clock_monotonic_nanoseconds();
                        minute = now + (positive)(59 - real.tv_sec % 60) * 1000000000ull +
                                 (positive)(1000000000l - real.tv_nsec) + 1000000ull;
                        until = !until || minute < until ? minute : until;
                }

#ifdef CONFIG_MOONWATER_WAYLAND
                // A client that asked for a frame is told of one a frame's time after the last, whether or not anything was drawn.
                if (compositor_wl.started && wayland_frame_wanted() && !compositor.damages)
                {
                        positive frame;

                        now = clock_monotonic_nanoseconds();
                        frame = now - compositor.presented < compositor.period ? compositor.presented + compositor.period : now;
                        until = !until || frame < until ? frame : until;
                }
                // A copy or a paste that stalls is ended when its time is up.
                if (compositor_wl.started && compositor_wl_due())
                {
                        now = clock_monotonic_nanoseconds();
                        until = !until || compositor_wl_due() < until ? compositor_wl_due() : until;
                }
#endif
                if (compositor.damages)
                {
                        positive frame;

                        now = clock_monotonic_nanoseconds();
                        frame = now - compositor.presented < compositor.period
                                    ? compositor.presented + compositor.period
                                    : now;
                        until = !until || frame < until ? frame : until;
                }

                // A timespec: a wait past a second in its nanoseconds is EINVAL, and the loop would spin on it.
                wait[0] = until > now ? (long)((until - now) / 1000000000ull) : 0;
                wait[1] = until > now ? (long)((until - now) % 1000000000ull) : 0;
                awaited = compositor_call(syscall(ppoll), (positive)wanted, count,
                                          until ? (positive)wait : 0, 0, 0, 0);

                // Whatever woke the loop, a sleep may be what it woke from; and auto's theme may have turned with the clock.
                compositor_theme_tick();
                compositor_wake_check(clock_monotonic_nanoseconds());
                compositor_wake_pass(clock_monotonic_nanoseconds());
                compositor_hold_drop(clock_monotonic_nanoseconds());

                if (awaited < 0)
                        continue;

                if (wanted[0].returned)
                        running = 0;

                if (wanted[1].returned)
                        compositor_inputs_watch();

                for (unsigned int at = 0; at < compositor.input_count; at++)
                        if (wanted[inputs + at].returned)
                                compositor_input_read(at);

                for (unsigned int row = 0; row < WINDOWS; row++)
                        if (wanted[doors + row].returned)
                                compositor_door((int)row, wanted[doors + row].returned);

#ifdef CONFIG_MOONWATER_WAYLAND
                if (wayland_at && compositor_wl.started)
                        compositor_wl_pass(wanted + wayland_at);
#endif
                if (card_at && wanted[card_at].returned)
                        compositor_card_read();
                if (clip_at && wanted[clip_at].returned)
                        compositor_clip_cli();

                // A screen came or went, or the screens are taken now that a flip that held them back has landed.
                if (uevents_at && wanted[uevents_at].returned)
                        compositor_uevents_read();
                if (compositor.space_wanted && !compositor.flip_pending)
                        compositor_space_retake();

                // A pointer that was shaken is its own size again when its time is up.
                if (compositor.cursor.magnified && clock_monotonic_nanoseconds() >= compositor.cursor.magnified_until)
                {
                        compositor.cursor.magnified = 0;
                        compositor_cursor_move();
                }

                if (compositor_predict_due())
                        compositor_predict_expire(clock_monotonic_nanoseconds());

                // A finger held long enough, and the marks that fade.
                if (compositor_touch_due())
                        compositor_touch_tick(clock_monotonic_nanoseconds());
                if (compositor_snap_due())
                        compositor_snap_tick(clock_monotonic_nanoseconds());
                if (compositor_spots_any(1))
                        compositor_spots_tick(clock_monotonic_nanoseconds());

                if (compositor.band && taskbar_tick())
                        compositor_tabs_changed();
                if (compositor.band && clock_monotonic_nanoseconds() >= compositor_volume.look_at)
                        compositor_volume_look(clock_monotonic_nanoseconds());

#ifdef CONFIG_MOONWATER_WAYLAND
                // The windows' clients are told of what changed, before the frame is drawn: no event waits for it.
                compositor_wl_wake();
#endif

                // Dated when it starts: a card that holds the call for the rest
                // of the frame has used the frame up.
                if (compositor.damages)
                {
                        positive stamp = clock_monotonic_nanoseconds();

                        if (stamp - compositor.presented >= compositor.period)
                        {
                                compositor.presented = stamp;
                                compositor_present();
                                compositor_land_keep();
                                compositor_glide_keep();
                                compositor_preview_keep();
                                compositor_overlays_keep();
                                if (compositor.motion_ts)
                                {
                                        compositor_latency(1, compositor.motion_ts, clock_monotonic_nanoseconds());
                                        compositor.motion_ts = 0;
                                }
#ifdef CONFIG_MOONWATER_WAYLAND
                                compositor_wl_frame();
#endif
                        }
                }
#ifdef CONFIG_MOONWATER_WAYLAND
                // Callbacks that waited for a frame and no frame was drawn for: the frame's time has gone by all the same.
                else if (compositor_wl.started && wayland_frame_wanted() &&
                         clock_monotonic_nanoseconds() - compositor.presented >= compositor.period)
                {
                        compositor.presented = clock_monotonic_nanoseconds();
                        compositor_wl_frame();
                }
#endif

                // One knock a window a pass, however much it was given.
                compositor_knock();
        }

#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_stop();
#endif
        compositor_table_drop();
        string_format(log, "compositor: %p pixels composed, the plane was given %p shapes and %p moves and refused %p times\n",
                      (positive)compositor.painted, (positive)compositor.cursor.paints, (positive)compositor.cursor.moves,
                      (positive)compositor.cursor.refused);
        string_format(log, "compositor: the processor was held %p times\n", (positive)(compositor.holds == ~0ul ? 0 : compositor.holds));
        string_format(log, "compositor: glass blurred again %p pixels and composed again %p\n", compositor.blurred,
                      compositor.recomposed);
        for (unsigned int which = 0; which < 3; which++)
                string_format(log, "compositor: %s: %p, on average %p us, worst %p us\n",
                              which == 0 ? "pointer to the plane" : which == 1 ? "pointer to the frame" : "key to the window",
                              (positive)compositor.latency[which].count,
                              (positive)(compositor.latency[which].count
                                             ? compositor.latency[which].total / compositor.latency[which].count / 1000
                                             : 0),
                              (positive)(compositor.latency[which].worst / 1000));
        log_flush();
        if (!headless)
                compositor_hand_back();
        return 0;
}

// surface ----------------------------------------------------------
/*
        A test client: surface [-x X] [-y Y] [-w WIDTH] [-h HEIGHT] [-c RRGGBB]
        [-T TITLE] [-N NAME] [-p] [-m] [-R] [-n] [-G] [-a ALPHA] [-F PICTURES]
        [-f FONT.ttf] [-t TEXT] [-s SECONDS].

        A window of one colour at X, Y, a line of TEXT drawn with font.c on it
        when FONT is given, and a line on standard output for everything it is
        told: its row and where it is, each key, button and wheel, the keys
        coming and going, a close asked for, and with -p the pointer's
        motions. -m puts it away as it opens, -R draws the size it is asked
        for, -n never shows a picture, and -F shows PICTURES more, as fast as
        it can. -G makes it glass, its colour laid over the glass at ALPHA
        (0, the default, is the glass alone, 255 the colour alone). -N is the
        name the compositor remembers where it was put by; without one it is
        not remembered. It lives until it is asked to close, the compositor
        goes, or SECONDS.
*/
static void surface_draw(const struct window_row *own, unsigned int which, unsigned int width, unsigned int height,
                         unsigned int colour, string_address font, string_address text)
{
        unsigned int *pixels = windows_pixels(own, which);

        for (unsigned int line = 0; line < height; line++)
                memory_fill_u32(pixels + (unsigned long)line * own->stride, width, colour);

        // Glass has no ground to draw text on.
        if (font && !own->table->glass[own->row])
        {
                positive size = 0;
                positive bytes = compositor_map_file(font, address_of size);
                positive atlas_at = (sizeof(font_engine) + 4095) & ~(positive)4095;
                long engine = compositor_call(syscall(mmap), 0, atlas_at + 256 * 256, 3, 0x22, (positive)-1, 0);
                font_face face;
                const font_face address_to faces[1] = {&face};
                font_target target = {pixels, own->stride, (b32)width, (b32)height};
                p32 em = 16 << 6;

                if (bytes && !(engine < 0 && engine > -4096) && font_load(&face, (const p8 address_to)bytes, size, 0) == 0)
                {
                        font_start((font_engine address_to)engine, (p8 address_to)(engine + atlas_at), 256, 256);
                        string_format(log, "surface: text drawn %b px wide\n",
                                      (bipolar)(font_draw((font_engine address_to)engine, faces, 1,
                                                          (const p8 address_to)text, string_length(text), em, 0,
                                                          &target, 8 << 16, 8 + font_metrics_at(&face, em).baseline,
                                                          0xffffff, colour) >>
                                                16));
                }
                else
                        log_direct(str("surface: no font\n"));
                if (!(engine < 0 && engine > -4096))
                        compositor_call(syscall(munmap), (positive)engine, atlas_at + 256 * 256, 0, 0, 0, 0);
                if (bytes)
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }

        own->table->width[own->row] = width;
        own->table->height[own->row] = height;
        own->table->damage_width[own->row] = 0;
}

static b32 surface_main()
{
        bipolar x = 0, y = 0, width = 320, height = 200, colour = 0x3366aa, seconds = -1, flood = 0, alpha = 0;
        b32 pointer = 0, never = 0, away = 0, sized = 0, focused = 0, glass = 0;
        string_address title = null, font = null, text = (string_address) "Moonwater", name = null;
        struct window_row own;
        struct windows *table;
        unsigned int which = 0;
        positive until;
        long taken;

        claim_standard_descriptors();

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2])
                        continue;
                if (option[1] == 'p' || option[1] == 'n' || option[1] == 'm' || option[1] == 'R' || option[1] == 'G')
                {
                        glass |= option[1] == 'G';
                        pointer |= option[1] == 'p';
                        sized |= option[1] == 'R';
                        never |= option[1] == 'n';
                        away |= option[1] == 'm';
                        continue;
                }
                if (!value)
                        break;
                at++;
                switch (option[1])
                {
                case 'x': x = compositor_number(value, 0); break;
                case 'y': y = compositor_number(value, 0); break;
                case 'w': width = compositor_number(value, 0); break;
                case 'h': height = compositor_number(value, 0); break;
                case 'c': colour = compositor_number(value, 1); break;
                case 's': seconds = compositor_number(value, 0); break;
                case 'F': flood = compositor_number(value, 0); break;
                case 'a': alpha = compositor_number(value, 0); break;
                case 'T': title = value; break;
                case 'N': name = value; break;
                case 'f': font = value; break;
                case 't': text = value; break;
                }
        }

        taken = windows_take(&own);
        if (taken < 0)
        {
                string_format(log, "surface: no window, error %b\n", (bipolar)taken);
                log_flush();
                return 1;
        }

        table = own.table;
        width = width < 1 ? 1 : width > (bipolar)table->output_width ? (bipolar)table->output_width : width;
        height = height < 1 ? 1 : height > (bipolar)table->output_height ? (bipolar)table->output_height : height;
        colour &= 0xffffff;
        if (glass)
        {
                // Premultiplied: each colour scaled by the alpha, which is the top byte.
                alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                colour = (bipolar)window_mix(0, (unsigned int)colour, (unsigned int)(alpha + (alpha >> 7))) |
                         (bipolar)((unsigned int)alpha << 24);
                table->glass[own.row] = WINDOW_GLASS;
        }
        table->x[own.row] = (int)x;
        table->y[own.row] = (int)y;
        table->state[own.row] = away ? WINDOW_MINIMIZED : 0;
        {
                positive at = 0;

                for (; title && title[at] && at < WINDOWS_TITLE - 1; at++)
                        table->title[own.row][at] = (char)title[at];
                table->title[own.row][at] = 0; // a row is reused: what it held past this title is not the title's
        }
        windows_name(&own, (const char *)name);
        surface_draw(&own, which, (unsigned int)width, (unsigned int)height, (unsigned int)colour, font, text);

        if (!never)
        {
                windows_show(&own, which);
                for (bipolar at = 0; at < flood; at++)
                        windows_show(&own, which);

                // Where it was put is the compositor's to say once it has taken the picture.
                until = clock_monotonic_nanoseconds() + 2000000000ull;
                while (!windows_ready(&own) && clock_monotonic_nanoseconds() < until)
                        windows_wait(&own, -1, 50000000);
        }

        string_format(log, "surface: %p at %b,%b\n", (positive)own.row, (bipolar)table->x[own.row],
                      (bipolar)table->y[own.row]);
        log_flush();

        until = seconds >= 0 ? clock_monotonic_nanoseconds() + (positive)seconds * 1000000000ull : 0;

        for (;;)
        {
                struct window_event event;
                long timeout = -1;
                b32 has = table->focus == own.row;

                if (has != focused)
                {
                        focused = has;
                        string_format(log, "surface: focus %b\n", (bipolar)focused);
                }

                // The size asked for, drawn once the last picture is taken, as a terminal does.
                if (sized && table->want_width[own.row] && table->want_height[own.row] &&
                    (table->want_width[own.row] != (unsigned int)width || table->want_height[own.row] != (unsigned int)height) &&
                    windows_ready(&own))
                {
                        width = table->want_width[own.row] > table->output_width ? table->output_width : table->want_width[own.row];
                        height = table->want_height[own.row] > table->output_height ? table->output_height
                                                                                    : table->want_height[own.row];
                        which ^= 1;
                        surface_draw(&own, which, (unsigned int)width, (unsigned int)height, (unsigned int)colour, null, text);
                        windows_show(&own, which);
                        string_format(log, "surface: size %b %b\n", width, height);
                }
                log_flush();

                if (table->state[own.row] & WINDOW_CLOSING)
                {
                        log_direct(str("surface: close\n"));
                        break;
                }

                if (until)
                {
                        positive now = clock_monotonic_nanoseconds();

                        if (now >= until)
                                break;
                        timeout = (long)(until - now);
                }

                if (windows_wait(&own, -1, timeout) < 0)
                {
                        log_direct(str("surface: the compositor has gone\n"));
                        break;
                }

                while (windows_event(&own, &event))
                        if (event.type == WINDOW_EVENT_KEY)
                                string_format(log, "surface: key %b %b %p\n", (bipolar)event.a, (bipolar)event.b,
                                              (positive)event.flags);
                        else if (event.type == WINDOW_EVENT_BUTTON)
                                string_format(log, "surface: button %b %p\n", (bipolar)event.a, (positive)event.flags);
                        else if (event.type == WINDOW_EVENT_WHEEL)
                                string_format(log, "surface: wheel %b %b\n", (bipolar)event.a, (bipolar)event.b);
                        else if (pointer)
                                string_format(log, "surface: motion %b %b\n", (bipolar)event.a, (bipolar)event.b);
        }

        log_flush();
        windows_leave(&own);
        return 0;
}

#endif // MOONWATER_CANVAS_COMPOSITOR
