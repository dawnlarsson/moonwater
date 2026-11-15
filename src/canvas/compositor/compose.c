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
        int minimize;        // a window with a minimise button: it has a tab, which brings it back (none for WINDOW_NO_TAB)
        unsigned int close_mean; // the mean colour under the close button, made once a present (compositor_close_mean)
        // What of its line is lit, and in what ink (COMPOSITOR_INK_LINE_HELD or _HOVER): one edge (CP_EDGE_*) lit whole,
        // two that meet a corner lit with COMPOSITOR_LIT_REACH of each, COMPOSITOR_LIT_ALL all of it; 0 none.
        unsigned int lit, lit_ink;
        int lit_w;           // the lit edges' width in device pixels (compositor_lit_px); the line's, or more
        // A window landing is drawn at less than its picture's size, source_w by source_h: its contents are that
        // picture scaled (compositor_scaled). 0 for a frame drawn at its picture's size.
        int source_w, source_h;
        // The snap preview of the window being dragged: its target's rectangle on the output (width 0 for none) and
        // the kit's selection, 0xaarrggbb, whose alpha is the tint's (compositor_scene draws it); its corners' radius, the
        // window's, and its line in device pixels (0 for none) and the line's ink, 0xaarrggbb. compositor_present takes it
        // off the window's frame into a frame of its own after the tabs (compositor_preview_lift), which names the window's
        // frame in preview_under, one more than its place in the list (0: the preview lies under its own frame). It is drawn
        // under that window and what is over it, and over the windows under it; the overlays (the grid, the assist) are over it.
        int preview[4];
        unsigned int preview_ink;
        int preview_radius, preview_line;
        unsigned int preview_edge;
        int preview_under;
        // The shared edge's indicator over this window (compositor_mark_set): the rectangle and the kit's selection.
        int mark[4];
        unsigned int mark_ink;
        // A window's picture composed from its runs (compositor_runs_note): each row of it, the stretches that are one colour
        // filled and the rest copied; none for a frame whose runs are not known, which is copied whole.
        const struct compositor_run_row *runs;
        // The card's address of the first pixel, for a picture the card reads by an import of a client's dma-buf (0 for one its pages map).
        positive gpu_base;
        // A client's premultiplied ARGB8888 picture that is not opaque: blended over what lies under it (op 11 on the card).
        int blend;
};

/*
        The close button: a disc at the window's top right, inset from both
        edges, and none on a window too small to hold it beside its corner.
        For drawing and for the press alike.
*/
#if MOONWATER_UI_ANY
static inline int language_rtl(void); // the language taken reads from the right (ui/ui.c, which is built after this file)
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
        (t->out ? memory_copy_rect_out_u32 : memory_copy_rect_u32)(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox),
                                                                   t->pitch, source + (unsigned long)(y1 - y) * pitch + (unsigned long)(x1 - x), pitch,
                                                                   (unsigned long)(x2 - x1), (unsigned long)(y2 - y1));
}

// Premultiplied ARGB8888 source-over (matching the GPU kernel's op 11: src + dst * (255 - src_a) / 255, saturating each channel at 255).
static inline unsigned int compositor_over_pixel(unsigned int dst, unsigned int src)
{
        unsigned int a = src >> 24, inv;
        unsigned int trb, tga, drb, dga, rb, ga, b, g, r, oa;

        if (a == 255u)
                return src;
        if (!a)
                return dst;
        inv = 255u - a;
        trb = src & 0x00ff00ffu, tga = (src >> 8) & 0x00ff00ffu;
        drb = (dst & 0x00ff00ffu) * inv + 0x007f007fu;
        dga = ((dst >> 8) & 0x00ff00ffu) * inv + 0x007f007fu;
        rb = trb + (((drb + 0x00010001u + ((drb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu);
        ga = tga + (((dga + 0x00010001u + ((dga >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu);
        b = rb & 0x1ffu, r = (rb >> 16) & 0x1ffu, g = ga & 0x1ffu, oa = (ga >> 16) & 0x1ffu;
        b = b > 255u ? 255u : b, g = g > 255u ? 255u : g, r = r > 255u ? 255u : r, oa = oa > 255u ? 255u : oa;
        return b | (g << 8) | (r << 16) | (oa << 24);
}

static inline unsigned int compositor_over_weighted(unsigned int dst, unsigned int src, unsigned int weight)
{
        unsigned int rb, ga;

        if (!weight || !(src >> 24))
                return dst;
        if (weight >= 255u)
                return compositor_over_pixel(dst, src);
        rb = (src & 0x00ff00ffu) * weight + 0x007f007fu;
        ga = ((src >> 8) & 0x00ff00ffu) * weight + 0x007f007fu;
        rb = ((rb + 0x00010001u + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
        ga = (ga + 0x00010001u + ((ga >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
        return compositor_over_pixel(dst, rb | ga);
}

static void compositor_blit_blend(const struct compositor_target *t, int x, int y, int width,
                                  int height, const unsigned int *source, unsigned long pitch)
{
        int x1 = x > t->x1 ? x : t->x1;
        int y1 = y > t->y1 ? y : t->y1;
        int x2 = x + width < t->x2 ? x + width : t->x2;
        int y2 = y + height < t->y2 ? y + height : t->y2;

        if (x2 <= x1 || y2 <= y1)
                return;
        compositor_reading(t);
        memory_over_u32_rect(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox), t->pitch,
                             source + (unsigned long)(y1 - y) * pitch + (unsigned long)(x1 - x), pitch,
                             (positive)(x2 - x1), (positive)(y2 - y1));
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
                                 unsigned long width, unsigned long height, const struct compositor_run_row *rows, unsigned long skip, int out)
{
        fn (*copy)(p32 address_to, positive, const p32 address_to, positive, positive, positive) = out ? memory_copy_rect_out_u32 : memory_copy_rect_u32;

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
                                copy(into + at, into_pitch, from + at, from_pitch, (unsigned long)a - at, block);
                        memory_fill_u32_rect(into + a, into_pitch, (unsigned long)(b - a), block, rows->run[k].colour);
                        at = (unsigned long)b;
                }
                if (at < width)
                        copy(into + at, into_pitch, from + at, from_pitch, width - at, block);
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
                             (unsigned long)(x2 - x1), (unsigned long)(y2 - y1), rows + (y1 - y), (unsigned long)(x1 - x), t->out);
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
// compositor_blit_scaled draws there: the source cell memory_resample_u32_rect's walk gives a frame drawn whole (the first position half a cell in, stepping by the
// floored step in 24 bit fractions), so that a corner's or a button's pixel is the contents' own and the card, which makes the whole frame's picture once, agrees.
static unsigned int compositor_picture_at(const struct compositor_frame *frame, int dx, int dy)
{
        unsigned long sx = (unsigned long)dx, sy = (unsigned long)dy;

        if (compositor_scaled(frame))
        {
                unsigned long step_x = ((unsigned long)frame->source_w << 24) / (unsigned long)frame->width, step_y = ((unsigned long)frame->source_h << 24) / (unsigned long)frame->height;

                sx = ((((unsigned long)frame->source_w << 23) / (unsigned long)frame->width) + sx * step_x) >> 24;
                sy = ((((unsigned long)frame->source_h << 23) / (unsigned long)frame->height) + sy * step_y) >> 24;
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

        if (!frame->minimize || !compositor_close_box(frame, &bx, &by, &box) || frame->width < 2 * box + 3 * in + 4 * box)
                return 0;
        *side = box;
        *x = language_rtl() ? bx + box + in : bx - in - box;
        *y = by;
        return 1;
}

/*
        The ink of the close and minimise glyphs over the mean of a window's pixels under its close button
        (compositor_close_mean): the text ink of the theme whose paper is on the mean's side of lightness, the light theme's
        18181b on a light mean and the dark theme's ededef on a dark one, so over a window of the kit the glyphs are its own
        text's ink in either theme, and over any other picture they stand out 3.3 to 1 or more from the mean.
*/
static unsigned int compositor_glyph_ink(unsigned int mean)
{
        return (window_light(mean) ? ui_theme_dawning.role[UI_ON_SURFACE] : ui_theme_night.role[UI_ON_SURFACE]) & 0xffffffu;
}

/*
        The minimise button over a window's pixels: a dash across its disc, COMPOSITOR_MINIMIZE_IN in from each side and two
        logical pixels thick, in the close button's ink (compositor_glyph_ink), and, as the X, with nothing round it, so the
        two are one look. Drawn after the window's pixels, through the target's clip, as the close button is.
*/
static void compositor_minimize_draw(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int mx, my, side, in = COMPOSITOR_MINIMIZE_IN * frame->scale / 100;

        if (!compositor_minimize_box(frame, &mx, &my, &side))
                return;
        compositor_fill(t, mx + in, my + side / 2 - compositor_px1(frame->scale), side - 2 * in, 2 * compositor_px1(frame->scale),
                        compositor_glyph_ink(frame->close_mean));
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
        How much of each pixel of a close button the X's strokes cover, in
        sixteenths (four by four samples): the geometry of the button alone,
        its side and its scale, so made once for them (compositor_close_ready,
        before the scene's bands) and read for every window's button at every
        present, where the sixteen samples a pixel were most of a present's
        cost after the copy.
*/
#define COMPOSITOR_CLOSE_MOST (UI_CLOSE * 8)

static struct
{
        int side, half;
        unsigned char cross[COMPOSITOR_CLOSE_MOST * COMPOSITOR_CLOSE_MOST];
} compositor_close_cover;

// The share of the sixteen sample points of pixel x, y of a close button side wide that are inside the X's strokes, half each stroke's half width.
static unsigned int compositor_close_sample(int side, int half, int x, int y)
{
        int arm = side * 2 / 7;
        unsigned int cross = 0;

        // In eighths of a pixel from the button's middle, sample centres odd.
        for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                        int dx = 8 * x + 2 * sx + 1 - 4 * side, dy = 8 * y + 2 * sy + 1 - 4 * side;
                        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                        int d1 = dx - dy, d2 = dx + dy;

                        d1 = d1 < 0 ? -d1 : d1;
                        d2 = d2 < 0 ? -d2 : d2;
                        cross += ax <= 8 * arm && ay <= 8 * arm && (d1 <= 11 * half || d2 <= 11 * half);
                }
        return cross;
}

// The cover of a frame's button, made where it is not already for its side and scale.
static void compositor_close_ready(const struct compositor_frame *frame)
{
        int bx, by, side, half = compositor_px1(frame->scale);

        if (!compositor_close_box(frame, &bx, &by, &side) || side > COMPOSITOR_CLOSE_MOST ||
            (compositor_close_cover.side == side && compositor_close_cover.half == half))
                return;
        for (int y = 0; y < side; y++)
                for (int x = 0; x < side; x++)
                        compositor_close_cover.cross[y * side + x] = (unsigned char)compositor_close_sample(side, half, x, y);
        compositor_close_cover.side = side;
        compositor_close_cover.half = half;
}

/*
        The close button over a window's pixels: an X in the glyph ink (compositor_glyph_ink) and nothing round it, as the
        minimise button's dash has nothing round it, so the two are one look. Each pixel is the window's own mixed with the
        ink by the share of its sixteen sample points inside the strokes. What is under it is read from the window's pixels,
        which are known, and not from the target. Drawn last, after the round corners, through the target's clip, so the
        scene's pieces and its corners' scratch both have it.
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

        unsigned int ink = compositor_glyph_ink(frame->close_mean);
        int half = compositor_px1(frame->scale); // half the X's stroke
        int made = compositor_close_cover.side == side && compositor_close_cover.half == half;
        const unsigned int *picture = compositor_scaled(frame) ? 0 : frame->pixels;

        for (int py = y1; py < y2; py++)
                for (int px = x1; px < x2; px++)
                {
                        unsigned int cross = made ? compositor_close_cover.cross[(py - by) * side + (px - bx)]
                                                  : compositor_close_sample(side, half, px - bx, py - by);

                        if (!cross)
                                continue;

                        unsigned int under = picture ? picture[(unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(px - frame->x)]
                                                     : compositor_picture_at(frame, px - frame->x, py - frame->y);

                        t->pixels[(unsigned long)(py - t->oy) * t->pitch + (unsigned long)(px - t->ox)] = compositor_mix(under, ink, cross * 255 / 16);
                }
}

static void compositor_blit_scaled_blend(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int x = frame->x, y = frame->y, width = frame->width, height = frame->height;
        int x1 = x > t->x1 ? x : t->x1;
        int y1 = y > t->y1 ? y : t->y1;
        int x2 = x + width < t->x2 ? x + width : t->x2;
        int y2 = y + height < t->y2 ? y + height : t->y2;
        int w = x2 - x1;

        if (w <= 0 || y2 <= y1 || width <= 0 || height <= 0)
                return;
        compositor_reading(t);
        if (w <= 4096)
        {
                unsigned int scratch[4096];
                int chunk = 4096 / w;

                for (int py = y1; py < y2; py += chunk)
                {
                        int rows = y2 - py < chunk ? y2 - py : chunk;
                        unsigned int *into = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);

                        memory_resample_u32_rect(scratch, (unsigned long)w, (unsigned long)w, (unsigned long)rows,
                                                 frame->pixels, frame->pitch,
                                                 (unsigned long)frame->source_w, (unsigned long)frame->source_h,
                                                 (unsigned long)width, (unsigned long)height,
                                                 (unsigned long)(x1 - x), (unsigned long)(py - y));
                        memory_over_u32_rect(into, t->pitch, scratch, (unsigned long)w, (unsigned long)w, (unsigned long)rows);
                }
                return;
        }
        for (int py = y1; py < y2; py++)
        {
                unsigned int *into = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);

                for (int px = x1; px < x2; px++)
                        into[px - x1] = compositor_over_pixel(into[px - x1], compositor_picture_at(frame, px - x, py - y));
        }
}

// What is inside a frame's line, through the target's clip: a window's pixels, or one of the panel's tabs.
static void compositor_contents(const struct compositor_target *t, const struct compositor_frame *frame)
{
        // A frame with no picture is its line and corners only (a grid tile's border): it has no contents to draw.
        if (!frame->builtin && !frame->pixels)
                return;
        if (frame->builtin)
                compositor_fill(t, frame->x, frame->y, frame->width, frame->height, taskbar_tab_ink(((int)(frame->slot)), ((unsigned int)(compositor_panel_mark(frame)))));
        else if (frame->blend)
        {
                if (compositor_scaled(frame))
                        compositor_blit_scaled_blend(t, frame);
                else
                        compositor_blit_blend(t, frame->x, frame->y, frame->width, frame->height, frame->pixels, frame->pitch);
        }
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
        A lit round corner drawn over the contents, where the lit width (frame->lit_w) is more than the line: the ring
        between the corner's outline and the outline cut lit_w in, in the lit ink, a row at a time as compositor_corner_rows
        makes its line (the same masks, the outer less the inner). Nothing else of the frame is drawn here.
*/
static void compositor_lit_corner(const struct compositor_target *t, const struct compositor_frame *frame, int corner, int r,
                                  int thick, unsigned int ink)
{
        int ox, oy, ow, oh, ri = r > thick ? r - thick : 0, right = corner & 1, down = (corner & 2) != 0;
        const unsigned char *outer = right ? compositor_corner_mirror(r) : compositor_corner_mask(r);
        const unsigned char *inner = ri ? (right ? compositor_corner_mirror(ri) : compositor_corner_mask(ri)) : 0;
        unsigned int ink_row[COMPOSITOR_RADIUS_MOST];
        unsigned char ring[COMPOSITOR_RADIUS_MOST];
        int cx, cy, x1, y1, x2, y2;

        compositor_outline(frame, &ox, &oy, &ow, &oh);
        cx = right ? ox + ow - r : ox;
        cy = down ? oy + oh - r : oy;
        x1 = cx > t->x1 ? cx : t->x1;
        y1 = cy > t->y1 ? cy : t->y1;
        x2 = cx + r < t->x2 ? cx + r : t->x2;
        y2 = cy + r < t->y2 ? cy + r : t->y2;
        if (x2 <= x1 || y2 <= y1)
                return;
        compositor_reading(t);
        memory_fill_u32(ink_row, (positive)(x2 - x1), ink & 0xffffffu);
        for (int py = y1; py < y2; py++)
        {
                int my = down ? cy + r - 1 - py : py - cy;
                unsigned int *at = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);

                for (int k = 0; k < x2 - x1; k++)
                {
                        int col = x1 - cx + k, iy = my - thick, ix = right ? col : col - thick;
                        unsigned int oc = outer[(unsigned long)my * r + col], ic = 0;

                        if (ri && iy >= 0 && iy < ri && ix >= 0 && ix < ri)
                                ic = inner[(unsigned long)iy * ri + ix];
                        ring[k] = oc > ic ? (unsigned char)(oc - ic) : 0;
                }
                memory_mix_u32_row(at, ink_row, ring, (positive)(x2 - x1));
        }
}

// The lit edges and corners at their width over a frame's contents (frame->lit_w more than the line): the straight bands, and
// the lit corners' rings, or for a square corner its two bands of that width. Drawn after the contents, so they cover them.
static void compositor_lit_over(const struct compositor_target *t, const struct compositor_frame *frame, int ox, int oy, int ow, int oh,
                                int r)
{
        int thick = frame->lit_w;

        compositor_lit_line(t, frame, ox, oy, ow, oh, r, thick);
        for (int corner = 0; r && corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;

                if (!compositor_corner_lit(frame, corner))
                        continue;
                if (!(frame->square & (1 << corner)))
                {
                        compositor_lit_corner(t, frame, corner, r, thick, frame->lit_ink);
                        continue;
                }
                compositor_fill(t, cx, corner & 2 ? oy + oh - thick : oy, r, thick, frame->lit_ink);
                compositor_fill(t, corner & 1 ? ox + ow - thick : ox, cy, thick, r, frame->lit_ink);
        }
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
        if (x2 > x1 && y2 > y1)
                compositor_reading(t);
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
                if (frame->lit && frame->lit_w <= b)
                        compositor_lit_line(t, frame, ox, oy, ow, oh, r, b);
        }

        // The contents, but for the corners' squares.
        if (!r)
        {
                compositor_contents(t, frame);
                if (b && frame->lit && frame->lit_w > b)
                        compositor_lit_over(t, frame, ox, oy, ow, oh, r);
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
                if (frame->builtin || (!compositor_scaled(frame) && !frame->blend))
                {
                        compositor_corner_rows(t, frame, corner, r, b, edge, tab);
                        continue;
                }
                compositor_reading(t);

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
                                        if (frame->blend)
                                                pixel = compositor_over_weighted(pixel, ink, in);
                                        else
                                                pixel = in == 255 ? ink : in ? compositor_mix(pixel, ink, in) : pixel;
                                }
                                *at = pixel;
                        }
        }
        if (b && frame->lit && frame->lit_w > b)
                compositor_lit_over(t, frame, ox, oy, ow, oh, r);
        compositor_close_draw(t, frame);
        compositor_minimize_draw(t, frame);
}

static void compositor_draw(const struct compositor_target *t, const struct compositor_frame *frame)
{
        compositor_draw_with(t, frame, compositor_radius(frame));
}

// Why a window's list was not drawn by the card (counted, and shown by moonwater debug): the program then paints that frame in pixels. The card sets the
// reason (compositor_list_why) before it answers 0.
enum compositor_list_why
{
        COMPOSITOR_LIST_DRAWN,
        COMPOSITOR_LIST_INVALID, // window_list_valid refused the header (magic, version, size, torn)
        COMPOSITOR_LIST_PARTIAL, // a list that is not the whole picture after a frame the program painted in pixels
        COMPOSITOR_LIST_CARD,    // no card to draw it: the setting off, the card away or given up, not started, or a job still running
        COMPOSITOR_LIST_OPEN,    // the list could not be opened as the picture's (its size is not the window's, a publish was writing it)
        COMPOSITOR_LIST_CUT,     // an op of the list is not one a window is made of, or reaches outside what it may
        COMPOSITOR_LIST_TORN,    // the program published a new generation while the ops were read
        COMPOSITOR_LIST_MAPS,    // the cover bytes or the picture could not be mapped for the card
        COMPOSITOR_LIST_JOB,     // the card did not finish the scene
        COMPOSITOR_LIST_PIXELS,  // not a refusal: a frame the program painted in pixels after one the card drew from its list (a switch between the two)
        COMPOSITOR_LIST_COUNT
};
static enum compositor_list_why compositor_list_why;
static unsigned int compositor_list_counts[COMPOSITOR_LIST_COUNT];

#ifdef MOONWATER_GPU_C
/*
        The GPU's compose (src/canvas/gpu.c): frames as the records of its scene, which draws them in order as compositor_draw does -- a frame's
        pictures copied, its line filled, and in each round corner's square the line's colour and then the picture mixed by a table of covers
        (compositor_corner_rows' own arithmetic, made once and kept in the GPU's buffer). A frame it cannot draw as the processor does
        (the panel, a picture scaled, a line lit, a close button, a preview) makes the whole answer none, and the frames are drawn by the processor.
*/
#define COMPOSITOR_GPU_TABLES 128

// Why the GPU did not compose a present (compositor_gpu_present), one code each: the presents the card drew and the presents it was refused are
// counted (compositor_gpu_counts, moonwater canvas's gpu lines), and each reason is said in the log. The names are host.c's (the same order).
enum compositor_gpu_why
{
        COMPOSITOR_GPU_WHY_DRAWN,     // not a refusal: the card drew the present
        COMPOSITOR_GPU_WHY_OFF,       // canvas gpu off
        COMPOSITOR_GPU_WHY_HEADLESS,  // no card, and the headless compositor not asked for the GPU (-G)
        COMPOSITOR_GPU_WHY_NO_CARD,   // no card the kernels are for, or the GPU did not start
        COMPOSITOR_GPU_WHY_UNTRUSTED, // a card whose self-test drew wrong
        COMPOSITOR_GPU_WHY_DEAD,      // cooling down after hangs (the card is tried again after it; canvas gpu off and on ends it at once)
        COMPOSITOR_GPU_WHY_ALIGN,     // a width that is not a multiple of 4, or a pitch that is not 16 bytes
        COMPOSITOR_GPU_WHY_BUSY,      // a job given up on may still be running
        COMPOSITOR_GPU_WHY_OUTPUT,    // the scanout is not one the card can draw into
        COMPOSITOR_GPU_WHY_PICTURE,   // a window's picture is not in a map the card can read
        COMPOSITOR_GPU_WHY_SCALED,    // a window landing, drawn scaled
        COMPOSITOR_GPU_WHY_PREVIEW,   // a snap preview or a shared edge's mark is drawn
        COMPOSITOR_GPU_WHY_POOL,      // the records or the cover tables are full
        COMPOSITOR_GPU_WHY_BUTTON,    // a close button that is not wholly in its window's picture
        COMPOSITOR_GPU_WHY_HANG,      // the scene did not finish
        COMPOSITOR_GPU_WHY_TURN,      // a turned screen's rotation did not finish on the card (the processor turned it)
        COMPOSITOR_GPU_WHY_OTHER,     // a frame of another kind, or the damage is empty
        COMPOSITOR_GPU_WHY_ROUND = COMPOSITOR_GPU_WHY_OTHER + 1, // a window with alpha whose picture reaches into a round corner (the card does not draw that yet): no map in it
        // the pointer drawn into the scene wears a window's client's own picture (compositor_picture); slot 17, after OTHER, is the
        // round corners' that window.c names already
        COMPOSITOR_GPU_WHY_POINTER = COMPOSITOR_GPU_WHY_OTHER + 2,
        COMPOSITOR_GPU_WHY_COUNT
};
typedef char compositor_gpu_why_fits[COMPOSITOR_GPU_WHY_COUNT <= WINDOWS_GPU_REASONS ? 1 : -1];

// A frame's picture's address for the GPU: in the map that holds all of it, 0 for none. A map that holds only its first byte (made when the window was
// smaller) would have the card read its rows below from the addresses after that map's end, which are another buffer's: another window's pixels.
#define compositor_gpu_va(f) ((f)->gpu_base ? (f)->gpu_base : gpu_va_bytes((f)->pixels, 4 * ((f)->pitch * (positive)((f)->height - 1) + (positive)(f)->width)))
struct compositor_gscene
{
        unsigned int *ops;                  // the records, in the GPU's buffer
        unsigned int count, room, width, height; // and the picture they are for
        int clip[4];                        // the rectangle the records are cut to: the picture, or a tint's while its frames are drawn over it (x0 y0 x1 y1)
        enum compositor_gpu_why why;        // why the frames were refused, when they were
        unsigned char *tables;              // the cover tables' pool, in the GPU's buffer, its size and its place from the buffer's start
        unsigned int tables_size, tables_at, tables_used, kept;
        struct
        {
                int r, b, corner, content;
                unsigned long tag; // what else the bytes depend on (a chip's kind and cover), 0 for the shapes the key says all of
                unsigned int at;
        } table[COMPOSITOR_GPU_TABLES];
};
static struct compositor_gscene compositor_gscene;

// The pool's table for a key and a tag (a size of bytes): its place from the buffer's start and, when it is new, where to fill it; none when the pool is full.
static unsigned int compositor_gpu_slot_tag(int r, int b, int corner, int content, unsigned long tag, unsigned int size, unsigned char **fill)
{
        struct compositor_gscene *g = &compositor_gscene;
        unsigned int rounded = (size + 15) & ~15u;

        *fill = 0;
        for (unsigned int k = 0; k < g->kept; k++)
                if (g->table[k].r == r && g->table[k].b == b && g->table[k].corner == corner && g->table[k].content == content && g->table[k].tag == tag)
                        return g->table[k].at;
        if (g->kept == COMPOSITOR_GPU_TABLES || rounded < size || g->tables_used > g->tables_size || rounded > g->tables_size - g->tables_used)
                return 0;
        *fill = g->tables + g->tables_used;
        g->table[g->kept].r = r, g->table[g->kept].b = b, g->table[g->kept].corner = corner, g->table[g->kept].content = content;
        g->table[g->kept].tag = tag;
        g->table[g->kept++].at = g->tables_at + g->tables_used;
        g->tables_used += rounded;
        return g->table[g->kept - 1].at;
}

// The pool's table for a key whose bytes the key says all of.
static unsigned int compositor_gpu_slot(int r, int b, int corner, int content, unsigned int size, unsigned char **fill)
{
        return compositor_gpu_slot_tag(r, b, corner, content, 0, size, fill);
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

                        // Content 2 is the ring of the line at b (the outside less the inside), a lit corner drawn over the contents.
                        if (content == 2)
                        {
                                int iy = my - b, ix = right ? col : col - b;
                                unsigned int in = inner && iy >= 0 && iy < ri && ix >= 0 && ix < ri ? inner[iy * ri + ix] : 0;

                                cover = cover > in ? (unsigned char)(cover - in) : 0;
                        }
                        else if (content && b)
                        {
                                int iy = my - b, ix = right ? col : col - b;

                                cover = inner && iy >= 0 && iy < ri && ix >= 0 && ix < ri ? inner[iy * ri + ix] : 0;
                        }
                        table[row * r + col] = cover;
                }
        return at;
}

// A close button's table (side by side bytes): the X's strokes' covers, in 255ths as the processor mixes them (the samples of sixteen).
static unsigned int compositor_gpu_close_table(int side, int half)
{
        unsigned char *table;
        unsigned int at = compositor_gpu_slot(side, half, 4, 3, (unsigned int)(side * side), &table);

        for (int y = 0; table && y < side; y++)
                for (int x = 0; x < side; x++)
                        table[y * side + x] = (unsigned char)(compositor_close_sample(side, half, x, y) * 255 / 16);
        return at;
}

// One record: the rectangle cut to the picture, none when nothing is left. A fill writes the colour; a copy takes the picture's pixels from the frame, at the
// rectangle's own corner; a mix is memory_mix_u32_row by the cover table at cover (pitch r), the ink the colour or, with a frame, the picture.
static void compositor_gpu_rect(unsigned int kind, int x0, int y0, int x1, int y1, unsigned int colour, const struct compositor_frame *frame, unsigned int cover, int r)
{
        struct compositor_gscene *g = &compositor_gscene;
        int cx0 = x0 < g->clip[0] ? g->clip[0] : x0, cy0 = y0 < g->clip[1] ? g->clip[1] : y0, cx1 = x1 > g->clip[2] ? g->clip[2] : x1, cy1 = y1 > g->clip[3] ? g->clip[3] : y1;
        unsigned int *op = g->ops + 8 * g->count;

        if (cx1 <= cx0 || cy1 <= cy0)
                return;
        op[0] = kind, op[1] = (unsigned int)cx0, op[2] = (unsigned int)cy0, op[3] = (unsigned int)cx1, op[4] = (unsigned int)cy1;
        op[5] = colour, op[6] = op[7] = 0;
        if (frame)
        {
                positive at = compositor_gpu_va(frame) + 4ull * ((positive)(cy0 - frame->y) * frame->pitch + (positive)(cx0 - frame->x));

                if (kind == 4 || kind == 11)
                        op[5] = (unsigned int)at, op[6] = (unsigned int)(at >> 32), op[7] = (unsigned int)frame->pitch;
                else
                        op[0] |= (unsigned int)frame->pitch << 16, op[6] = (unsigned int)at, op[7] = (unsigned int)(at >> 32);
        }
        if (kind == 5)
                op[0] |= (unsigned int)r << 8, op[5] = cover + (unsigned int)((cy0 - y0) * r + (cx0 - x0)), op[6] = frame ? op[6] : colour;
        g->count++;
}

// The close button and the minimise button over a window's pixels (compositor_close_draw, compositor_minimize_draw): the X's strokes mix the glyph ink over
// the window's pixels and the dash is a fill of it, nothing round either. A button not wholly in the picture is cut to it, as the processor draws it.
static b32 compositor_gpu_buttons(const struct compositor_frame *f)
{
        int bx, by, side, mx, my, in = COMPOSITOR_MINIMIZE_IN * f->scale / 100, half = compositor_px1(f->scale);
        unsigned int ink = compositor_glyph_ink(f->close_mean), cross;

        if (!compositor_close_box(f, &bx, &by, &side))
                return true;
        if (!f->builtin && !f->pixels) // a close button is drawn over a picture, and a frame with none has no pixels under it
        {
                compositor_gscene.why = COMPOSITOR_GPU_WHY_BUTTON;
                return false;
        }
        // A button past the picture's edge is cut by compositor_gpu_rect, which moves the cover table's start with the cut, as the processor draws it cut.
        if (side <= 0 || side > 255 || f->pitch > 65535)
        {
                compositor_gscene.why = COMPOSITOR_GPU_WHY_BUTTON;
                return false;
        }
        cross = compositor_gpu_close_table(side, half);
        if (!cross)
        {
                compositor_gscene.why = COMPOSITOR_GPU_WHY_POOL;
                return false;
        }
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

// compositor_lit_over on the GPU: the lit edges at frame->lit_w over the contents, and each lit round corner's ring (its cover table
// is content 2 of compositor_gpu_table), or a square corner's two bands. False when a table is not made (the pools are full).
static b32 compositor_gpu_lit_over(const struct compositor_frame *f, int ox, int oy, int ow, int oh, int r)
{
        struct compositor_gscene *g = &compositor_gscene;
        int thick = f->lit_w;
        unsigned int ink = f->lit_ink & 0xffffffu;

        compositor_gpu_lit(f, ox, oy, ow, oh, r, thick);
        for (int corner = 0; r && corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;
                unsigned int ring;

                if (!compositor_corner_lit(f, corner))
                        continue;
                if (f->square & (1 << corner))
                {
                        int top = corner & 2 ? oy + oh - thick : oy, left = corner & 1 ? ox + ow - thick : ox;

                        compositor_gpu_rect(0, cx, top, cx + r, top + thick, ink, 0, 0, 0);
                        compositor_gpu_rect(0, left, cy, left + thick, cy + r, ink, 0, 0, 0);
                        continue;
                }
                ring = compositor_gpu_table(r, thick, corner, 2);
                if (!ring)
                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                compositor_gpu_rect(5, cx, cy, cx + r, cy + r, ink, 0, ring, r);
        }
        return true;
}

// What is inside a frame's line, in a rectangle of it: the picture's pixels copied (or blended when non-opaque), or the tab's one ink.
static void compositor_gpu_contents(const struct compositor_frame *f, unsigned int tab, int x0, int y0, int x1, int y1)
{
        if (!f->builtin && !f->pixels) // no picture: nothing inside the line (compositor_contents)
                return;
        if (f->builtin)
                compositor_gpu_rect(0, x0, y0, x1, y1, tab, 0, 0, 0);
        else
                compositor_gpu_rect(f->blend ? 11 : 4, x0, y0, x1, y1, 0, f, 0, 0);
}

// A frame as compositor_draw_with draws it (a window with a picture at its own size, or a tab of the panel, and nothing over it): false if it is not one, or the pools are full.
static b32 compositor_gpu_frame(const struct compositor_frame *f)
{
        struct compositor_gscene *g = &compositor_gscene;
        int x = f->x, y = f->y, w = f->width, h = f->height, ox, oy, ow, oh, r = compositor_radius(f), b, ri;

        unsigned int tab = f->builtin ? taskbar_tab_ink(f->slot, compositor_panel_mark(f)) : 0; // a tab of the panel is one ink

        // A frame of no area has no picture to read (a snap preview's lifted frame is one: its tint is in compositor_gpu_frames).
        if (!f->builtin && f->pixels && w > 0 && h > 0 && !compositor_gpu_va(f))
                return g->why = COMPOSITOR_GPU_WHY_PICTURE, false;
        if (compositor_scaled(f))
                return g->why = COMPOSITOR_GPU_WHY_SCALED, false;
        // The record's pitch is 16 bits (compositor_gpu_frame's op[0]): a wider picture is left to the processor.
        if (f->pitch > 65535)
                return g->why = COMPOSITOR_GPU_WHY_ALIGN, false;
        if (g->count + 40 > g->room - 400)
                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
        compositor_outline(f, &ox, &oy, &ow, &oh);
        b = x - ox, ri = r > b ? r - b : 0;
        if (f->blend && ri > 0 && (f->square & 15) != 15)
                return g->why = COMPOSITOR_GPU_WHY_ROUND, false;
        if (b)
        {
                compositor_gpu_rect(0, ox + r, oy, ox + ow - r, oy + b, f->edge, 0, 0, 0);
                compositor_gpu_rect(0, ox + r, y + h, ox + ow - r, y + h + b, f->edge, 0, 0, 0);
                compositor_gpu_rect(0, ox, oy + r, ox + b, oy + oh - r, f->edge, 0, 0, 0);
                compositor_gpu_rect(0, x + w, oy + r, x + w + b, oy + oh - r, f->edge, 0, 0, 0);
                if (f->lit && f->lit_w <= b)
                        compositor_gpu_lit(f, ox, oy, ow, oh, r, b);
        }
        if (!r)
        {
                compositor_gpu_contents(f, tab, x, y, x + w, y + h);
                if (b && f->lit && f->lit_w > b && !compositor_gpu_lit_over(f, ox, oy, ow, oh, r))
                        return false;
                return compositor_gpu_buttons(f);
        }
        compositor_gpu_contents(f, tab, x + ri, y, x + w - ri, y + h);
        compositor_gpu_contents(f, tab, x, y + ri, x + ri, y + h - ri);
        compositor_gpu_contents(f, tab, x + w - ri, y + ri, x + w, y + h - ri);
        for (int corner = 0; corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;
                int sx1 = cx > x ? cx : x, sx2 = cx + r < x + w ? cx + r : x + w, sy1 = cy > y ? cy : y, sy2 = cy + r < y + h ? cy + r : y + h;
                int picture = f->builtin || f->pixels != 0;
                unsigned int edge_table, content_table, edge = compositor_corner_lit(f, corner) && f->lit ? f->lit_ink : f->edge;

                if (f->square & (1 << corner))
                {
                        if (b && picture)
                                compositor_gpu_rect(0, cx, cy, cx + r, cy + r, edge, 0, 0, 0);
                        else if (b)
                        {
                                int top = corner & 2 ? oy + oh - b : oy, left = corner & 1 ? ox + ow - b : ox;

                                compositor_gpu_rect(0, cx, top, cx + r, top + b, edge, 0, 0, 0);
                                compositor_gpu_rect(0, left, cy, left + b, cy + r, edge, 0, 0, 0);
                        }
                        compositor_gpu_contents(f, tab, sx1, sy1, sx2, sy2);
                        continue;
                }
                // A frame with no picture draws only the ring of its line (content 2, the outside less the inside); one with a picture fills the outer
                // square with the line (content 0) and mixes the picture over the inner corner (content 1).
                edge_table = b ? compositor_gpu_table(r, b, corner, picture ? 0 : 2) : 0;
                content_table = picture && ri ? compositor_gpu_table(r, b, corner, 1) : 0;
                if ((b && !edge_table) || (picture && ri && !content_table))
                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                if (b)
                        compositor_gpu_rect(5, cx, cy, cx + r, cy + r, edge & 0xffffffu, 0, edge_table, r);
                if (picture && ri && sx2 > sx1 && sy2 > sy1)
                        compositor_gpu_rect(5, sx1, sy1, sx2, sy2, tab, f->builtin ? 0 : f, content_table + (unsigned int)((sy1 - cy) * r + (sx1 - cx)), r);
        }
        if (b && f->lit && f->lit_w > b && !compositor_gpu_lit_over(f, ox, oy, ow, oh, r))
                return false;
        return compositor_gpu_buttons(f);
}

static b32 compositor_gpu_tint(const int *rect, unsigned int ink, int radius, int edge_line, unsigned int edge);

// The frames, in order, over a ground, then the tints as compositor_preview_draw draws them: every frame's snap preview, then every frame's mark, each
// over the scene and the frames it lies under drawn again over it (for the lifted preview, which has no area and draws nothing, from the window it names,
// drawn again whole over the tint, which leaves the pixels compositor_tint leaves by not tinting its contents; from a frame with no area itself, a grid
// region's; from the frame above it for a mark). The scene's records; false (and none) if any is not one the GPU draws.
static b32 compositor_gpu_frames(const struct compositor_frame *frames, unsigned int count, unsigned int ground)
{
        struct compositor_gscene *g = &compositor_gscene;

        g->count = 0, g->why = COMPOSITOR_GPU_WHY_DRAWN;
        // The tables of the last scenes let go when the pool is half full, by count or by bytes: a scene is under that, so it never finds the pool shut.
        if (g->kept > COMPOSITOR_GPU_TABLES / 2 || g->tables_used > g->tables_size / 2)
                g->kept = 0, g->tables_used = 0;
        g->clip[0] = 0, g->clip[1] = 0, g->clip[2] = (int)g->width, g->clip[3] = (int)g->height;
        compositor_gpu_rect(0, 0, 0, (int)g->width, (int)g->height, ground, 0, 0, 0);
        for (unsigned int k = 0; k < count; k++)
                if (!compositor_gpu_frame(&frames[k]))
                        goto refused;
        for (unsigned int pass = 0; pass < 2; pass++)
                for (unsigned int k = 0; k < count; k++)
                {
                        const struct compositor_frame *f = &frames[k];
                        const int *rect = pass ? f->mark : f->preview;
                        unsigned int from = pass ? k + 1 : f->preview_under > 0 ? (unsigned int)f->preview_under - 1 : k;

                        if (rect[2] <= 0 || rect[3] <= 0)
                                continue;
                        // A window carrying its own snap preview is the owner of its ring and hide (compositor_tint), which these records do not make:
                        // a preview lifted to a frame of no area and no line has its window drawn again whole over the tint instead.
                        if (!pass && !f->builtin && ((f->width > 0 && f->height > 0) || f->line > 0 || compositor_radius(f) > 0))
                        {
                                g->why = COMPOSITOR_GPU_WHY_PREVIEW;
                                goto refused;
                        }
                        if (!compositor_gpu_tint(rect, pass ? f->mark_ink : f->preview_ink, pass ? 0 : f->preview_radius, pass ? 0 : f->preview_line,
                                                 pass ? 0 : f->preview_edge))
                                goto refused;
                        // the frames it lies over are drawn again inside its rectangle alone (compositor_tint's cell), then the scene is whole again
                        g->clip[0] = rect[0] > 0 ? rect[0] : 0, g->clip[1] = rect[1] > 0 ? rect[1] : 0;
                        g->clip[2] = rect[0] + rect[2] < (int)g->width ? rect[0] + rect[2] : (int)g->width;
                        g->clip[3] = rect[1] + rect[3] < (int)g->height ? rect[1] + rect[3] : (int)g->height;
                        for (unsigned int j = from; j < count; j++)
                                if (!compositor_gpu_frame(&frames[j]))
                                        goto refused;
                        g->clip[0] = 0, g->clip[1] = 0, g->clip[2] = (int)g->width, g->clip[3] = (int)g->height;
                }
        return true;
refused:
        g->count = 0, g->clip[0] = 0, g->clip[1] = 0, g->clip[2] = (int)g->width, g->clip[3] = (int)g->height;
        return false;
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
// A client's surfaces shown as layers of its window, each a frame with no chrome after its window's (wayland_host.c appends them): room for this many in all.
#define COMPOSITOR_LAYERS 64
#define COMPOSITOR_FRAMES (2 * WINDOWS + 2 + 2 * WINDOWS_OUTPUTS + COMPOSITOR_OVERLAYS + COMPOSITOR_LAYERS) // the snap preview has a frame of its own

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
        (compositor_tint), the rule the card's mix records follow too. Its ends are its own: an ink of alpha 255 is its colour
        filled (the top byte none), and one of alpha 0 leaves every pixel as it is, its top byte with it, so neither is mixed.
*/
static void compositor_mix_run(unsigned int *pixels, long count, unsigned int ink, const unsigned int *colour,
                               const unsigned char *cover)
{
        if ((ink >> 24) >= 255)
        {
                memory_fill_u32(pixels, (positive)count, ink & 0xffffffu);
                return;
        }
        if ((ink >> 24) == 0)
                return;
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

static void compositor_shape_covers(const struct compositor_shape *s, int px, int my, unsigned int *ce, unsigned int *cf)
{
        int ri = s->r - s->line, right = px >= s->x + s->w - s->r, col = right ? px - (s->x + s->w - s->r) : px - s->x;
        unsigned int outer = (right ? s->outer_right : s->outer)[my * s->r + col], inner = 0;
        int iy = my - s->line, ix = right ? col : col - s->line;

        if (ri > 0 && iy >= 0 && iy < ri && ix >= 0 && ix < ri)
                inner = (right ? s->inner_right : s->inner)[iy * ri + ix];
        inner = inner > outer ? outer : inner;
        *ce = ((outer - inner) * (s->edge >> 24) + 127) / 255;
        *cf = (inner * (s->fill >> 24) + 127) / 255;
}

// The columns [a, b) of row py of a shaped tint, at in the cell's pixels being column a.
static unsigned long compositor_shape_row(const struct compositor_shape *s, unsigned int *at, int a, int b, int py,
                                          const unsigned int *colour, const unsigned char *fill_cover,
                                          const unsigned int *edge_colour, const unsigned char *edge_cover)
{
        int top = py - s->y, bottom = s->y + s->h - 1 - py, my = top < bottom ? top : bottom, corner = my < s->r;
        int zone = corner ? s->r : s->line, middle_to = s->x + s->w - zone;
        int edge_row = my < s->line;

        for (int px = a; px < b; px++)
        {
                if (px >= s->x + zone && px < middle_to)
                {
                        int to = middle_to < b ? middle_to : b;

                        compositor_mix_run(at + (px - a), to - px, edge_row ? s->edge : s->fill, edge_row ? edge_colour : colour,
                                           edge_row ? edge_cover : fill_cover);
                        px = to - 1;
                        continue;
                }
                if (!corner)
                {
                        at[px - a] = compositor_mix(at[px - a], s->edge & 0xffffffu, s->edge >> 24);
                        continue;
                }
                {
                        unsigned int ce, cf;

                        compositor_shape_covers(s, px, my, &ce, &cf);
                        if (ce)
                                at[px - a] = compositor_mix(at[px - a], s->edge & 0xffffffu, ce);
                        if (cf)
                                at[px - a] = compositor_mix(at[px - a], s->fill & 0xffffffu, cf);
                }
        }
        return (unsigned long)(b > a ? b - a : 0);
}

static void compositor_tint(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                            const int *radii, const int *rect, unsigned int ink, unsigned int from, int under, int radius,
                            int edge_line, unsigned int edge)
{
        const struct compositor_frame *owner = under && from < count && !frames[from].builtin ? &frames[from] : 0;
        struct compositor_target cell;
        unsigned int colour[COMPOSITOR_TINT_RUN], edge_colour[COMPOSITOR_TINT_RUN];
        unsigned char cover[COMPOSITOR_TINT_RUN], edge_cover[COMPOSITOR_TINT_RUN];
        unsigned long made = 0;
        int line = 0, ri = 0;
        struct compositor_shape shape = {rect[0], rect[1], rect[2], rect[3], 0, 0, ink, edge, 0, 0, 0, 0};

        if (rect[2] <= 0 || rect[3] <= 0)
                return;
        cell = compositor_clipped(t, rect[0], rect[1], rect[2], rect[3]);
        if (cell.x2 <= cell.x1 || cell.y2 <= cell.y1)
                return;
        compositor_reading(t);
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
                memory_fill_u32(edge_colour, COMPOSITOR_TINT_RUN, edge & 0xffffffu);
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
                                made += compositor_shape_row(&shape, at, cell.x1, lo, py, colour, cover, edge_colour, edge_cover);
                        if (hi < cell.x2)
                                made += compositor_shape_row(&shape, at + (hi - cell.x1), hi, cell.x2, py, colour, cover, edge_colour, edge_cover);
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

#ifdef MOONWATER_GPU_C
/*
        A constant cover of the scene's width at one alpha (a mix record of pitch 0 reads its cover from the left edge of its rectangle, so a row of this table
        covers any rectangle of the scene at that alpha). Keyed by the alpha and the width; its place from the buffer's start, 0 for none.
*/
static unsigned int compositor_gpu_const(unsigned int alpha)
{
        struct compositor_gscene *g = &compositor_gscene;
        unsigned char *table;
        unsigned int at = compositor_gpu_slot(-1, (int)alpha, 12, (int)g->width, g->width, &table);

        if (table)
                memory_fill(table, (int)alpha, (positive)g->width);
        return at;
}

// A rectangle of the scene mixed with one colour at one alpha, as compositor_mix_run does it (the colour's own top byte is not the cover: the alpha is).
static b32 compositor_gpu_const_rect(int x0, int y0, int x1, int y1, unsigned int colour, unsigned int alpha)
{
        struct compositor_gscene *g = &compositor_gscene;
        unsigned int at;

        if (x0 < 0)
                x0 = 0;
        if (x1 > (int)g->width)
                x1 = (int)g->width;
        if (x1 <= x0 || y1 <= y0)
                return true;
        at = compositor_gpu_const(alpha);
        if (!at)
                return false;
        compositor_gpu_rect(5, x0, y0, x1, y1, colour, 0, at, 0);
        return true;
}

// The cover of one corner square of a round shape (compositor_shape_cover, read the way the processor reads it in the square's own corner): r by r bytes,
// rows from the square's top, the corner 0 top left, 1 top right, 2 bottom left, 3 bottom right; part 0 = outer edge ring, part 1 = inner fill; its place from the buffer's start, 0 for none.
static unsigned int compositor_gpu_shape(int r, int line, int alpha, int corner, int part)
{
        unsigned char *table;
        unsigned int at = compositor_gpu_slot(r, line, (part ? 24 : 8) + corner, alpha, (unsigned int)(r * r), &table);
        struct compositor_shape s;
        int right = corner & 1, down = (corner & 2) != 0;

        if (!table)
                return at;
        // A square of its own, as the shape's corner: its outer mask (mirrored on the right) and its inner one where the line leaves one.
        memory_zero(&s, sizeof(s));
        s.x = 0, s.y = 0, s.w = 2 * r, s.h = 2 * r, s.r = r, s.line = line;
        s.fill = part ? (unsigned int)alpha << 24 : 0, s.edge = part ? 0 : (unsigned int)alpha << 24;
        s.outer = compositor_corner_mask(r), s.outer_right = compositor_corner_mirror(r);
        if (r - line > 0)
                s.inner = compositor_corner_mask(r - line), s.inner_right = compositor_corner_mirror(r - line);
        for (int row = 0; row < r; row++)
                for (int col = 0; col < r; col++)
                {
                        int my = down ? r - 1 - row : row, px = right ? r + col : col;
                        unsigned int ce, cf;

                        compositor_shape_covers(&s, px, my, &ce, &cf);
                        table[row * r + col] = (unsigned char)(part ? cf : ce);
                }
        return at;
}

/*
        A tint as compositor_tint draws it over the whole scene, with nothing of it left out (a snap preview lifted into a frame of its own, whose window is
        drawn again over it, or a shared edge's mark), as records. The clamps are compositor_tint's. A round tint's four corner squares are records with their covers' tables (edge ring then inner fill). Each run of rows that are alike
        (round corner rows, the edge's rows, the rest) is one record for the middle of the run, mixed with the ink (the edge's on the edge rows), and, on the
        rows past the corners, the line's two sides mixed with the edge's ink, as compositor_shape_row does it. A flat tint is the middle alone.
*/
static b32 compositor_gpu_tint(const int *rect, unsigned int ink, int radius, int edge_line, unsigned int edge)
{
        struct compositor_gscene *g = &compositor_gscene;
        int x = rect[0], y = rect[1], w = rect[2], h = rect[3], r, line, fa, ea;
        unsigned int fill = ink & 0xffffffu, ring = edge & 0xffffffu;

        if (w <= 0 || h <= 0)
                return true;
        if (g->count + 64 > g->room - 400)
                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
        radius = radius > rect[2] / 2 ? rect[2] / 2 : radius;
        radius = radius > rect[3] / 2 ? rect[3] / 2 : radius;
        radius = radius > COMPOSITOR_RADIUS_MOST ? COMPOSITOR_RADIUS_MOST : radius < 0 ? 0 : radius;
        edge_line = edge_line < 0 || !(edge >> 24) ? 0 : edge_line > radius && radius ? radius : edge_line;
        r = radius, line = edge_line, fa = (int)(ink >> 24), ea = (int)(edge >> 24);
        if (r > 0)
                for (int corner = 0; corner < 4; corner++)
                {
                        int cx = corner & 1 ? x + w - r : x, cy = corner & 2 ? y + h - r : y;

                        if (line > 0 && ea > 0)
                        {
                                unsigned int at = compositor_gpu_shape(r, line, ea, corner, 0);

                                if (!at)
                                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                                compositor_gpu_rect(5, cx, cy, cx + r, cy + r, ring, 0, at, r);
                        }
                        if (r - line > 0 && fa > 0)
                        {
                                unsigned int at = compositor_gpu_shape(r, line, fa, corner, 1);

                                if (!at)
                                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                                compositor_gpu_rect(5, cx, cy, cx + r, cy + r, fill, 0, at, r);
                        }
                }
        for (int py = y; py < y + h;)
        {
                int top = py - y, bottom = y + h - 1 - py, my = top < bottom ? top : bottom, corner = my < r, edge_row = my < line, stop = py + 1;
                unsigned int colour = edge_row ? ring : fill;
                int alpha = edge_row ? ea : fa, from = corner ? x + r : x + line, to = corner ? x + w - r : x + w - line;

                // the run of rows alike: the same corner and the same edge row
                for (; stop < y + h; stop++)
                {
                        int t = stop - y, b = y + h - 1 - stop, m = t < b ? t : b;

                        if ((m < r) != (corner != 0) || (m < line) != (edge_row != 0))
                                break;
                }
                if (!compositor_gpu_const_rect(from, py, to, stop, colour, (unsigned int)alpha))
                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                // The sides are the pixels of the rectangle that are not the middle (compositor_shape_row classifies each pixel once): a line wider than the
                // rectangle makes the two sides meet, so the left side is cut to the rectangle and the right one starts past it, and no pixel is mixed twice
                // or outside the rectangle.
                if (!corner && line > 0)
                {
                        int left_to = x + line < x + w ? x + line : x + w, right_from = x + w - line > left_to ? x + w - line : left_to;

                        if (!compositor_gpu_const_rect(x, py, left_to, stop, ring, (unsigned int)ea) ||
                            !compositor_gpu_const_rect(right_from, py, x + w, stop, ring, (unsigned int)ea))
                                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                }
                py = stop;
        }
        return true;
}
#endif

/*
        The tints of a scene, drawn after it is composed: a tint (frame->preview) over what lies under it, from the frame it
        lies under up, so the frames from there on are as they are over the tint (the snap preview is a frame of its own after
        every window and tab that names the window being dragged, so that window, what is over it and the overlays are over
        it, and the windows under it are under it); and a shared edge's indicator (frame->mark) from the frame above its own,
        so the window it is drawn over is under it, not over it.
*/
static void compositor_preview_draw(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                                    const int *radii)
{
        for (unsigned int at = 0; at < count; at++)
                compositor_tint(t, frames, count, radii, frames[at].preview, frames[at].preview_ink,
                                frames[at].preview_under > 0 ? (unsigned int)frames[at].preview_under - 1 : at, 1, frames[at].preview_radius,
                                frames[at].preview_line, frames[at].preview_edge);
        for (unsigned int at = 0; at < count; at++)
                compositor_tint(t, frames, count, radii, frames[at].mark, frames[at].mark_ink, at + 1, 0, 0, 0, 0);
}

/*
        The snap preview of the frames of the windows, taken off the window it belongs to into a frame of its own, none
        for no preview. The frame goes into the list after the tabs and before the overlays (the grid's tints and the
        assist's thumbnails), and names the window's frame (preview_under), so the tint is drawn under the window being
        dragged, which a press raised over the rest, and under what is over it, the tabs and the overlays; and over every
        window under it, a glass one too. It is not in the windows' frames, so the glass is made from the windows alone,
        and the tint is over a glass window under it once and not also in what it shows.
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
                tint->preview_under = (int)at + 1;
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

        for (unsigned int at = 0; at < count; at++)
                if (frames[at].blend)
                {
                        compositor_fill(t, t->x1, t->y1, t->x2 - t->x1, t->y2 - t->y1, compositor_ground);
                        for (unsigned int each = 0; each < count; each++)
                                compositor_draw_with(t, &frames[each], radii[each]);
                        compositor_preview_draw(t, frames, count, radii);
                        return;
                }

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
                        {
                                int occluded = 0;

                                for (unsigned int p = 0; p < pieces; p++)
                                        if (owner[p] > at && box[p][0] <= square.x1 && box[p][1] <= square.y1 &&
                                            box[p][2] >= square.x2 && box[p][3] >= square.y2)
                                        {
                                                occluded = 1;
                                                break;
                                        }
                                if (occluded)
                                        continue;
                        }
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
        int read_top, read_bottom; // the rows of a target only written that the scene reads (compositor_scene_reads): composed aside
};

/*
        What of a scene reads the target it is composed into, which a scanout cannot pay for: it is write-combined memory on
        most cards, where a word loaded is a trip across the bus (about ten megabytes a second, compositor_changed_ready).
        A frame blended over what is under it (frame->blend) has every frame of the damage drawn over the last in the target,
        so the whole damage reads it; a tint (a snap preview, a shared edge's mark) mixes over the target and has the frames
        over it drawn again over its rectangle, so its rows do. Answers whether any row reads, and the rows from *top to
        *bottom, inside the target's.
*/
static int compositor_scene_reads(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                                  int *top, int *bottom)
{
        *top = t->y2, *bottom = t->y1;
        for (unsigned int at = 0; at < count; at++)
        {
                if (frames[at].blend)
                {
                        *top = t->y1, *bottom = t->y2;
                        break;
                }
                for (int k = 0; k < 2; k++)
                {
                        const int *r = k ? frames[at].mark : frames[at].preview;

                        if (r[2] <= 0 || r[3] <= 0)
                                continue;
                        *top = r[1] < *top ? r[1] : *top;
                        *bottom = r[1] + r[3] > *bottom ? r[1] + r[3] : *bottom;
                }
        }
        *top = *top < t->y1 ? t->y1 : *top;
        *bottom = *bottom > t->y2 ? t->y2 : *bottom;
        return *bottom > *top;
}

// The slices of the rows a scene reads, COMPOSITOR_SLICE rows of the damage's width for each band, mapped when a scene first reads.
static unsigned int *compositor_aside;
static unsigned long compositor_aside_bytes;

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
        // Not composed apart, the band is written straight into the caller's target, which is a scanout (the screen's, a strip of
        // it the card left, an overlay plane's): only written, so its copies stream. The rows of it the scene reads are composed
        // aside, a slice at a time into memory of the band's own, and each slice copied out, so nothing reads the target.
        if (!banding->apart)
        {
                int a = banding->read_top > band.y1 ? banding->read_top : band.y1;
                int b = banding->read_bottom < band.y2 ? banding->read_bottom : band.y2;
                struct compositor_target part = band;

                band.out = 1;
                if (b <= a || !compositor_aside)
                {
                        compositor_scene_plan(&band, banding->frames, banding->count, banding->radii, banding->plan);
                        return;
                }
                part.out = 1, part.y2 = a;
                if (part.y2 > part.y1)
                        compositor_scene_plan(&part, banding->frames, banding->count, banding->radii, banding->plan);
                for (int top = a; top < b; top += COMPOSITOR_SLICE)
                {
                        struct compositor_target slice = band;
                        unsigned long pitch = (unsigned long)(band.x2 - band.x1);

                        slice.y1 = top;
                        slice.y2 = top + COMPOSITOR_SLICE < b ? top + COMPOSITOR_SLICE : b;
                        slice.pixels = compositor_aside + index * COMPOSITOR_SLICE * pitch;
                        slice.pitch = pitch;
                        slice.ox = band.x1, slice.oy = top, slice.out = 0;
                        compositor_scene_plan(&slice, banding->frames, banding->count, banding->radii, banding->plan);
                        compositor_blit(&band, band.x1, top, band.x2 - band.x1, slice.y2 - top, slice.pixels, pitch);
                }
                part.y1 = b, part.y2 = band.y2;
                if (part.y2 > part.y1)
                        compositor_scene_plan(&part, banding->frames, banding->count, banding->radii, banding->plan);
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
        struct compositor_banding banding = {t, frames, count, 1, apart, radii, &plan, {0}, {0}, 0, 0};

        bands = bands < COMPOSITOR_BANDS ? bands : COMPOSITOR_BANDS;
        bands = bands < (positive)(t->y2 - t->y1) / 16 ? bands : (positive)(t->y2 - t->y1) / 16;
        // A target only written that the scene reads: room for each band's slice, as wide as the damage, made here before the bands.
        if (!apart && compositor_scene_reads(t, frames, count, &banding.read_top, &banding.read_bottom))
        {
                unsigned long need = (unsigned long)COMPOSITOR_BANDS * COMPOSITOR_SLICE * (unsigned long)(t->x2 - t->x1) * 4;

                if (need > compositor_aside_bytes)
                {
                        if (compositor_aside)
                                system_call_2(syscall(munmap), (positive)compositor_aside, compositor_aside_bytes);
                        compositor_aside = (unsigned int *)compositor_map_zeroed(need);
                        compositor_aside_bytes = compositor_aside ? need : 0;
                }
        }
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
        compositor_reading(t);
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

