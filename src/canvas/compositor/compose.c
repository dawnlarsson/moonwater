/*
        The look: what a frame is drawn as, on the processor and as the card's records.

        A frame (struct compositor_frame) is a window's picture, its line and round corners, its close and minimise
        buttons and lit edges, a tab drawn without a font, or a finger's mark. This file draws frames into a target (struct
        compositor_target, compositor.c), copies a picture by the runs of one colour it was learned to have, cuts a present's
        scene in bands and plans it, composes aside the rows a scene reads of a target that is only written (a scanout:
        compositor_scene_reads), and makes the records a present on the card is drawn from (compositor_gpu_*, inside
        MOONWATER_GPU_C).

        It is the start of the backend: the core_state lane cuts compositor.c's text from its "backend" line to this file's
        end line, and keymap.c, pointer.c and outputs.c after it, and builds them for CHECK_canvas_cells with no card, no
        table and no compositor. So nothing here names the compositor's state (struct compositor, which compositor.c
        declares after these four files) or the kernel's interfaces: what it draws is handed to it, as a target and frames.
        Which window is where is manage.c's; what changed, damage.c's; the band's glass, glass.c's.

        It needs what compositor.c has above its includes: the inks and sizes, ui/ui.c's shared part, gpu.c or its stand-ins,
        struct compositor_target and compositor_reading; window.c before that. compositor_map_zeroed and
        compositor_taskbar_view are declared before their use and defined later (compositor.c, damage.c). It is called by
        nearly every part after it: damage.c (a frame's outline, its close box), glass.c, scanout.c and present.c (the scene),
        gpu_host.c (the records), manage.c and input.c (the arrangement's names, a blit, a corner's mask, a mark).
*/

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
        // 2 on the copy of one the scene draws a piece of through its plan (compositor_scene_plan): nothing is under it in
        // the damage but the ground (compositor_blend_alone), so it is laid over a row of the ground and the target is
        // written and not read.
        int blend;
        // Red and blue the other way round (1): a client's ABGR or XBGR dma-buf the card reads in place (compositor_lent_swap). The card's
        // records of it say so (0x80 on their kind, gpu.asm swaps the two as it loads them); the processor is never given one to read.
        int swap;
        // A layer of its row's window (a client's subsurface read where it keeps it, compositor_wl_layers_frames): a frame after the row's own,
        // with the row in rows[] as the window's own frame has, and nothing of a window but its picture (no line, corners, buttons or runs).
        int layer;
        // Solid opaque fill colour (0xaarrggbb) when !builtin && !pixels (e.g. black letterbox/pillarbox behind an aspect-fitted fullscreen window).
        unsigned int fill;
        // A client's tiled/DCC dma-buf resource (gpu_amd_image) sampled directly in the scene kernel (0x40 on op[0], zero detile pass).
        unsigned int tiled_desc[8];
        unsigned int tiled_kind;
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

// Clip (x, y, w, h) against target t; returns 1 if non-empty, writing [x1, x2) x [y1, y2).
static inline INLINE int target_clip(const struct compositor_target *restrict t, int x, int y, int w, int h,
                                     int *restrict x1, int *restrict y1, int *restrict x2, int *restrict y2)
{
        *x1 = x > t->x1 ? x : t->x1;
        *y1 = y > t->y1 ? y : t->y1;
        *x2 = x + w < t->x2 ? x + w : t->x2;
        *y2 = y + h < t->y2 ? y + h : t->y2;
        return *x2 > *x1 && *y2 > *y1;
}

HOT static void compositor_fill(const struct compositor_target *restrict t, int x, int y, int width,
                                int height, unsigned int colour)
{
        int x1, y1, x2, y2;

        if (!target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
                return;

        memory_fill_u32_rect(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox),
                             t->pitch, (unsigned long)(x2 - x1), (unsigned long)(y2 - y1),
                             colour);
}

HOT static void compositor_blit(const struct compositor_target *restrict t, int x, int y, int width,
                                int height, const unsigned int *restrict source, unsigned long pitch)
{
        int x1, y1, x2, y2;

        if (!target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
                return;
        (t->out ? memory_copy_rect_out_u32 : memory_copy_rect_u32)(t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox),
                                                                   t->pitch, source + (unsigned long)(y1 - y) * pitch + (unsigned long)(x1 - x), pitch,
                                                                   (unsigned long)(x2 - x1), (unsigned long)(y2 - y1));
}

/*
        The wallpaper (`canvas wallpaper moon`): the ground is the moon over its sky (ui_moon_night), each screen its own, made once
        into compositor_wall, as large as the space, and kept (compositor_wall_keep makes it again only when the space, the theme, the
        accent or a wallpaper setting changes); what the ground was filled with is copied from it. With none (plain, or no memory) the
        ground is the background colour, as it always was. The card's path (`canvas gpu on`, alpha) still draws the colour.
*/
static unsigned int *compositor_wall;
static int compositor_wall_w, compositor_wall_h;

static int compositor_space_all_fullscreen(void);

// The ground at a point of the space: solid black on Fullscreen Spaces, else the wallpaper's pixel or colour.
static inline unsigned int compositor_ground_at(int x, int y)
{
        if (compositor_space_all_fullscreen())
                return 0xff000000u;
        return compositor_wall && x >= 0 && y >= 0 && x < compositor_wall_w && y < compositor_wall_h
                       ? compositor_wall[(long)y * compositor_wall_w + x]
                       : compositor_ground;
}

// The ground over a rectangle of the target: solid black on Fullscreen Spaces, else the wallpaper or colour.
static void compositor_ground_fill(const struct compositor_target *t, int x, int y, int width, int height)
{
        int x1, y1, x2, y2;

        if (!target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
                return;
        if (compositor_space_all_fullscreen())
        {
                compositor_fill(t, x1, y1, x2 - x1, y2 - y1, 0xff000000u);
                return;
        }
        if (!compositor_wall || x1 < 0 || y1 < 0 || x2 > compositor_wall_w || y2 > compositor_wall_h)
        {
                compositor_fill(t, x1, y1, x2 - x1, y2 - y1, compositor_ground);
                return;
        }
        compositor_blit(t, x1, y1, x2 - x1, y2 - y1, compositor_wall + (long)y1 * compositor_wall_w + x1, (unsigned long)compositor_wall_w);
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

/*
        A blended picture over what the target holds, or, ground set, over the ground alone: the scene's plan draws a frame
        that nothing is under that way (compositor_blend_alone), and then the target is only written. The ground is a row
        of COMPOSITOR_GROUND_RUN words on the stack, read at a stride of none for every row, so a rectangle wider than it
        is laid a run of columns at a time; the answer is the ground filled and the picture laid over it, bit for bit. A
        target only written (out: a scanout, never one the next present reads back) takes memory_over_under_out_u32_rect,
        whose stores stream and so read no line they write, as memory_copy_rect_out_u32's do for an opaque window: laid
        with plain stores into the headless plate, which the processor caches, an opaque ARGB root at 3840x2160 cost 7.7 ms
        of compositor time a frame against 6.7 ms composed aside, every store reading its line first.
*/
#define COMPOSITOR_GROUND_RUN 1024

HOT static void compositor_over_ground(unsigned int *restrict into, unsigned long into_pitch, const unsigned int *restrict from, unsigned long from_pitch,
                                       unsigned long width, unsigned long height, int out, int gx, int gy)
{
        unsigned int ground[COMPOSITOR_GROUND_RUN];
        fn (*over)(p32 address_to, positive, const p32 address_to, positive, const p32 address_to, positive, positive, positive) =
            out ? memory_over_under_out_u32_rect : memory_over_under_u32_rect;

        // Over the wallpaper: its rows under the picture's, where the space has them.
        if (compositor_wall && gx >= 0 && gy >= 0 && gx + (long)width <= compositor_wall_w && gy + (long)height <= compositor_wall_h)
        {
                over(into, (positive)into_pitch, from, (positive)from_pitch, compositor_wall + (long)gy * compositor_wall_w + gx, (positive)compositor_wall_w,
                     (positive)width, (positive)height);
                return;
        }
        memory_fill_u32(ground, (positive)(width < COMPOSITOR_GROUND_RUN ? width : COMPOSITOR_GROUND_RUN), compositor_ground);
        for (unsigned long at = 0; at < width; at += COMPOSITOR_GROUND_RUN)
                over(into + at, (positive)into_pitch, from + at, (positive)from_pitch, ground, 0,
                     (positive)(width - at < COMPOSITOR_GROUND_RUN ? width - at : COMPOSITOR_GROUND_RUN), (positive)height);
}

HOT static void compositor_blit_blend(const struct compositor_target *restrict t, int x, int y, int width,
                                      int height, const unsigned int *restrict source, unsigned long pitch, int ground)
{
        int x1, y1, x2, y2;
        unsigned int *into;
        const unsigned int *from;

        if (!target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
                return;
        into = t->pixels + (unsigned long)(y1 - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);
        from = source + (unsigned long)(y1 - y) * pitch + (unsigned long)(x1 - x);
        if (ground)
        {
                compositor_over_ground(into, t->pitch, from, pitch, (unsigned long)(x2 - x1), (unsigned long)(y2 - y1), t->out, x1, y1);
                return;
        }
        compositor_reading(t);
        memory_over_u32_rect(into, t->pitch, from, pitch, (positive)(x2 - x1), (positive)(y2 - y1));
}

/*
        A frame whose picture has red and blue the other way round (frame->swap: a client's ABGR or XBGR buffer read in place, which the card
        swaps as it loads it), drawn by the processor as the card draws it: each row's run turned into a run of its own and laid from there, copied,
        over what the target holds, or over the ground (blend 2). The bytes are the card's: only red and blue trade places.
*/
static void compositor_blit_swapped(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int x1, y1, x2, y2;
        unsigned int run[1024];

        if (!target_clip(t, frame->x, frame->y, frame->width, frame->height, &x1, &y1, &x2, &y2))
                return;
        if (frame->blend == 1)
                compositor_reading(t);
        for (int py = y1; py < y2; py++)
                for (int px = x1; px < x2; px += 1024)
                {
                        unsigned long n = (unsigned long)(x2 - px < 1024 ? x2 - px : 1024);
                        unsigned int *into = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(px - t->ox);

                        memory_swizzle_rb_u32(run, frame->pixels + (unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(px - frame->x), (positive)n, 0);
                        if (frame->blend == 2)
                                compositor_over_ground(into, t->pitch, run, n, n, 1, t->out, px, py);
                        else if (frame->blend)
                                memory_over_u32_rect(into, t->pitch, run, n, n, 1);
                        else
                                (t->out ? memory_copy_rect_out_u32 : memory_copy_rect_u32)(into, t->pitch, run, n, n, 1);
                }
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
HOT static void compositor_runs_copy(unsigned int *restrict into, unsigned long into_pitch, const unsigned int *restrict from, unsigned long from_pitch,
                                     unsigned long width, unsigned long height, const struct compositor_run_row *restrict rows, unsigned long skip, int out)
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
HOT static void compositor_blit_runs(const struct compositor_target *restrict t, int x, int y, int width, int height,
                                     const unsigned int *restrict source, unsigned long pitch, const struct compositor_run_row *restrict rows)
{
        int x1, y1, x2, y2;

        if (!target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
                return;
        // A caret's damage, a hover, a letter: what is copied in a few lines is not worth a row's runs.
        if (unlikely(x2 - x1 < COMPOSITOR_RUNS_LEAST_WIDTH || (long)(x2 - x1) * (long)(y2 - y1) < COMPOSITOR_RUNS_LEAST_AREA))
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
        int x1, y1, x2, y2;

        if (width <= 0 || height <= 0 || !target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
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

                                // 16x16 supersampling (in 32nds of a pixel, sample centres odd, circle centre at 32*radius) for smooth 256-level AA coverage.
                                for (int sy = 0; sy < 16; sy++)
                                        for (int sx = 0; sx < 16; sx++)
                                        {
                                                int dx = 32 * radius - (32 * x + 2 * sx + 1);
                                                int dy = 32 * radius - (32 * y + 2 * sy + 1);

                                                inside += dx * dx + dy * dy <= 1024 * radius * radius;
                                        }
                                mask[y * radius + x] = (unsigned char)((inside * 255 + 128) >> 8);
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

        if (!target_clip(t, x, y, width, height, &part.x1, &part.y1, &part.x2, &part.y2))
        {
                if (part.x2 < part.x1)
                        part.x2 = part.x1;
                if (part.y2 < part.y1)
                        part.y2 = part.y1;
        }
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
        int bx, by, side, x1, y1, x2, y2;

        if (!compositor_close_box(frame, &bx, &by, &side) || !target_clip(t, bx, by, side, side, &x1, &y1, &x2, &y2))
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
        int x1, y1, x2, y2, w;

        if (width <= 0 || height <= 0 || !target_clip(t, x, y, width, height, &x1, &y1, &x2, &y2))
                return;
        w = x2 - x1;
        if (frame->blend != 2)
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
                        if (frame->blend == 2)
                                compositor_over_ground(into, t->pitch, scratch, (unsigned long)w, (unsigned long)w, (unsigned long)rows, t->out, x1, py);
                        else
                                memory_over_u32_rect(into, t->pitch, scratch, (unsigned long)w, (unsigned long)w, (unsigned long)rows);
                }
                return;
        }
        for (int py = y1; py < y2; py++)
        {
                unsigned int *into = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);

                for (int px = x1; px < x2; px++)
                        into[px - x1] = compositor_over_pixel(frame->blend == 2 ? compositor_ground_at(px, py) : into[px - x1],
                                                              compositor_picture_at(frame, px - x, py - y));
        }
}

// What is inside a frame's line, through the target's clip: a window's pixels, or one of the panel's tabs.
static void compositor_contents(const struct compositor_target *t, const struct compositor_frame *frame)
{
        // A frame with no picture is its line and corners only (a grid tile's border), unless it carries a solid backdrop fill.
        if (!frame->builtin && !frame->pixels)
        {
                if (frame->fill)
                        compositor_fill(t, frame->x, frame->y, frame->width, frame->height, frame->fill);
                return;
        }
        if (frame->builtin)
                compositor_fill(t, frame->x, frame->y, frame->width, frame->height, taskbar_tab_ink(((int)(frame->slot)), ((unsigned int)(compositor_panel_mark(frame)))));
        else if (frame->swap && !compositor_scaled(frame))
                compositor_blit_swapped(t, frame);
        else if (frame->blend)
        {
                if (compositor_scaled(frame))
                        compositor_blit_scaled_blend(t, frame);
                else
                        compositor_blit_blend(t, frame->x, frame->y, frame->width, frame->height, frame->pixels, frame->pitch,
                                              frame->blend == 2);
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
struct canvas_box
{
        int x, y, w, h;
};

/*
        Pure geometry of a frame's lit border segments between its corner squares: an edge held whole, a corner held as
        COMPOSITOR_LIT_REACH of each of its two edges from the corner's square (its arc is lit with the corners,
        compositor_corner_lit), or every edge. Shared by CPU and GPU emission.
*/
static unsigned int canvas_lit_segments(const struct compositor_frame *f, int ox, int oy, int ow, int oh,
                                        int r, int b, struct canvas_box out[4])
{
        unsigned int lit = f->lit & COMPOSITOR_LIT_ALL ? 15u : f->lit, n = 0;
        int across = lit & (COMPOSITOR_LIT_LEFT | COMPOSITOR_LIT_RIGHT), down = lit & (COMPOSITOR_LIT_TOP | COMPOSITOR_LIT_BOTTOM);
        int corner = across && down && !(f->lit & COMPOSITOR_LIT_ALL), reach = COMPOSITOR_LIT_REACH * f->scale / 100;
        int wide = ow - 2 * r, tall = oh - 2 * r;
        int seg_w = corner && reach < wide ? reach : wide, seg_h = corner && reach < tall ? reach : tall;
        int from_x = corner && (lit & COMPOSITOR_LIT_RIGHT) ? ox + ow - r - seg_w : ox + r;
        int from_y = corner && (lit & COMPOSITOR_LIT_BOTTOM) ? oy + oh - r - seg_h : oy + r;

        if (lit & COMPOSITOR_LIT_TOP)
                out[n++] = (struct canvas_box){from_x, oy, seg_w, b};
        if (lit & COMPOSITOR_LIT_BOTTOM)
                out[n++] = (struct canvas_box){from_x, oy + oh - b, seg_w, b};
        if (lit & COMPOSITOR_LIT_LEFT)
                out[n++] = (struct canvas_box){ox, from_y, b, seg_h};
        if (lit & COMPOSITOR_LIT_RIGHT)
                out[n++] = (struct canvas_box){ox + ow - b, from_y, b, seg_h};
        return n;
}

// Whether corner (bit 0 the right, bit 1 the bottom) is lit: every edge is, or both of the edges that meet there.
static int compositor_corner_lit(const struct compositor_frame *frame, int corner)
{
        return (frame->lit & COMPOSITOR_LIT_ALL) ||
               ((frame->lit & (corner & 1 ? COMPOSITOR_LIT_RIGHT : COMPOSITOR_LIT_LEFT)) &&
                (frame->lit & (corner & 2 ? COMPOSITOR_LIT_BOTTOM : COMPOSITOR_LIT_TOP)));
}

/*
        Unified frame emission sink: drives both CPU drawing (compositor_draw_with) and GPU scene record emission
        (compositor_gpu_frame) from the exact same pure-geometry decomposition of borders, lit edges, content cross,
        square/round corners, and window chrome buttons.
*/
struct canvas_sink
{
        void (*fill)(void *ctx, int x, int y, int w, int h, unsigned int colour);
        void (*contents)(void *ctx, const struct compositor_frame *f, unsigned int tab, int x, int y, int w, int h);
        int (*round_corner)(void *ctx, const struct compositor_frame *f, int corner, int r, int b, int ri,
                            int cx, int cy, int sx1, int sy1, int sx2, int sy2, unsigned int edge, unsigned int tab);
        int (*lit_corner)(void *ctx, const struct compositor_frame *f, int corner, int r, int thick,
                          int cx, int cy, unsigned int ink);
        int (*buttons)(void *ctx, const struct compositor_frame *f);
};

static int canvas_emit_lit_over(const struct canvas_sink *sink, void *ctx, const struct compositor_frame *f,
                                int ox, int oy, int ow, int oh, int r)
{
        struct canvas_box segs[4];
        int thick = f->lit_w;
        unsigned int n = canvas_lit_segments(f, ox, oy, ow, oh, r, thick, segs);

        for (unsigned int i = 0; i < n; i++)
                sink->fill(ctx, segs[i].x, segs[i].y, segs[i].w, segs[i].h, f->lit_ink);
        for (int corner = 0; r && corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;

                if (!compositor_corner_lit(f, corner))
                        continue;
                if (f->square & (1 << corner))
                {
                        int top = corner & 2 ? oy + oh - thick : oy, left = corner & 1 ? ox + ow - thick : ox;

                        sink->fill(ctx, cx, top, r, thick, f->lit_ink & 0xffffffu);
                        sink->fill(ctx, left, cy, thick, r, f->lit_ink & 0xffffffu);
                        continue;
                }
                if (!sink->lit_corner(ctx, f, corner, r, thick, cx, cy, f->lit_ink))
                        return 0;
        }
        return 1;
}

static int canvas_emit_frame(const struct canvas_sink *sink, void *ctx, const struct compositor_frame *f, int r)
{
        int x = f->x, y = f->y, w = f->width, h = f->height, ox, oy, ow, oh, b, ri;
        unsigned int tab = f->builtin ? taskbar_tab_ink(f->slot, compositor_panel_mark(f)) : 0;

        compositor_outline(f, &ox, &oy, &ow, &oh);
        b = x - ox;
        ri = r > b ? r - b : 0;
        if (b)
        {
                sink->fill(ctx, ox + r, oy, ow - 2 * r, b, f->edge);
                sink->fill(ctx, ox + r, y + h, ow - 2 * r, b, f->edge);
                sink->fill(ctx, ox, oy + r, b, oh - 2 * r, f->edge);
                sink->fill(ctx, x + w, oy + r, b, oh - 2 * r, f->edge);
                if (f->lit && f->lit_w <= b)
                {
                        struct canvas_box segs[4];
                        unsigned int n = canvas_lit_segments(f, ox, oy, ow, oh, r, b, segs);

                        for (unsigned int i = 0; i < n; i++)
                                sink->fill(ctx, segs[i].x, segs[i].y, segs[i].w, segs[i].h, f->lit_ink);
                }
        }
        if (!r)
        {
                sink->contents(ctx, f, tab, x, y, w, h);
                if (b && f->lit && f->lit_w > b && !canvas_emit_lit_over(sink, ctx, f, ox, oy, ow, oh, r))
                        return 0;
                return sink->buttons(ctx, f);
        }
        sink->contents(ctx, f, tab, x + ri, y, w - 2 * ri, h);
        sink->contents(ctx, f, tab, x, y + ri, ri, h - 2 * ri);
        sink->contents(ctx, f, tab, x + w - ri, y + ri, ri, h - 2 * ri);
        for (int corner = 0; corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;
                int sx1 = cx > x ? cx : x, sx2 = cx + r < x + w ? cx + r : x + w;
                int sy1 = cy > y ? cy : y, sy2 = cy + r < y + h ? cy + r : y + h;
                int picture = f->builtin || f->pixels != 0 || f->fill != 0;
                unsigned int edge = f->lit && compositor_corner_lit(f, corner) ? f->lit_ink : f->edge;

                if (f->square & (1 << corner))
                {
                        if (b && picture)
                                sink->fill(ctx, cx, cy, r, r, edge);
                        else if (b)
                        {
                                int top = corner & 2 ? oy + oh - b : oy, left = corner & 1 ? ox + ow - b : ox;

                                sink->fill(ctx, cx, top, r, b, edge);
                                sink->fill(ctx, left, cy, b, r, edge);
                        }
                        sink->contents(ctx, f, tab, sx1, sy1, sx2 - sx1, sy2 - sy1);
                        continue;
                }
                if (!sink->round_corner(ctx, f, corner, r, b, ri, cx, cy, sx1, sy1, sx2, sy2, edge, tab))
                        return 0;
        }
        if (b && f->lit && f->lit_w > b && !canvas_emit_lit_over(sink, ctx, f, ox, oy, ow, oh, r))
                return 0;
        return sink->buttons(ctx, f);
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
        if (!target_clip(t, cx, cy, r, r, &x1, &y1, &x2, &y2))
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
                canvas_mix_linear_row(at, ink_row, ring, (positive)(x2 - x1));
        }
}

/*
        One round corner's square, a row at a time, for a frame whose contents are its picture at its own size or a tab's
        ink (a scaled picture keeps the pixel loop in canvas_cpu_round_corner). Each row is two calls of memory_mix_u32_row,
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
        if (!target_clip(t, cx, cy, r, r, &x1, &y1, &x2, &y2))
                return;
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
                        canvas_mix_linear_row(at, edge_row, ring, (positive)(x2 - x1));
                }
                else if (b)
                        canvas_mix_linear_row(at, edge_row, outer + (unsigned long)my * r + (x1 - cx), (positive)(x2 - x1));
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
                        canvas_mix_linear_row(t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(sx1 - t->ox),
                                              row, cover, (positive)(sx2 - sx1));
                }
        }
}

static void canvas_cpu_fill(void *ctx, int x, int y, int w, int h, unsigned int colour)
{
        compositor_fill((const struct compositor_target *)ctx, x, y, w, h, colour);
}

static void canvas_cpu_contents(void *ctx, const struct compositor_frame *f, unsigned int tab, int x, int y, int w, int h)
{
        struct compositor_target part = compositor_clipped((const struct compositor_target *)ctx, x, y, w, h);

        (void)tab;
        if (part.x2 > part.x1 && part.y2 > part.y1)
                compositor_contents(&part, f);
}

static int canvas_cpu_round_corner(void *ctx, const struct compositor_frame *f, int corner, int r, int b, int ri,
                                   int cx, int cy, int sx1, int sy1, int sx2, int sy2, unsigned int edge, unsigned int tab)
{
        const struct compositor_target *t = (const struct compositor_target *)ctx;
        int x = f->x, y = f->y, w = f->width, h = f->height, x1, y1, x2, y2;

        (void)sx1, (void)sy1, (void)sx2, (void)sy2;
        if (!target_clip(t, cx, cy, r, r, &x1, &y1, &x2, &y2))
                return 1;
        if (f->builtin || (!compositor_scaled(f) && !f->blend))
        {
                compositor_corner_rows(t, f, corner, r, b, edge, tab);
                return 1;
        }
        compositor_reading(t);
        {
                const unsigned char *outer = compositor_corner_mask(r), *inner = ri ? compositor_corner_mask(ri) : 0;
                const unsigned int *picture = compositor_scaled(f) ? 0 : f->pixels;

                for (int py = y1; py < y2; py++)
                        for (int px = x1; px < x2; px++)
                        {
                                int mx = corner & 1 ? cx + r - 1 - px : px - cx, my = corner & 2 ? cy + r - 1 - py : py - cy;
                                unsigned int cover = outer[my * r + mx];
                                unsigned int *at = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(px - t->ox);
                                unsigned int pixel = *at;

                                if (cover)
                                        pixel = b ? canvas_mix_linear(pixel, edge & 0xffffffu, cover) : pixel;
                                if (px >= x && px < x + w && py >= y && py < y + h)
                                {
                                        unsigned int in = inner ? inner[(my - b) * ri + (mx - b)] : 0;
                                        unsigned int ink = f->builtin ? tab
                                                           : picture  ? picture[(unsigned long)(py - y) * f->pitch + (unsigned long)(px - x)]
                                                                      : compositor_picture_at(f, px - x, py - y);

                                        if (!b)
                                                in = cover;
                                        if (f->blend)
                                                pixel = compositor_over_weighted(pixel, ink, in);
                                        else
                                                pixel = in == 255 ? ink : in ? canvas_mix_linear(pixel, ink, in) : pixel;
                                }
                                *at = pixel;
                        }
        }
        return 1;
}

static int canvas_cpu_lit_corner(void *ctx, const struct compositor_frame *f, int corner, int r, int thick,
                                 int cx, int cy, unsigned int ink)
{
        (void)cx, (void)cy;
        compositor_lit_corner((const struct compositor_target *)ctx, f, corner, r, thick, ink);
        return 1;
}

static int canvas_cpu_buttons(void *ctx, const struct compositor_frame *f)
{
        const struct compositor_target *t = (const struct compositor_target *)ctx;

        compositor_close_draw(t, f);
        compositor_minimize_draw(t, f);
        return 1;
}

static const struct canvas_sink canvas_cpu_sink = {
    canvas_cpu_fill, canvas_cpu_contents, canvas_cpu_round_corner, canvas_cpu_lit_corner, canvas_cpu_buttons,
};

/*
        A frame drawn over a target whose radius is r, which is compositor_radius of the frame: a scene makes those once
        for all its frames, and compositor_draw makes this one. Nothing is written outside the frame's outline, so a
        target the outline misses is left as it is, and that is the first thing asked.
*/
static void compositor_draw_with(const struct compositor_target *t, const struct compositor_frame *frame, int r)
{
        int ox, oy, ow, oh;

        compositor_outline(frame, &ox, &oy, &ow, &oh);
        if (ox + ow <= t->x1 || oy + oh <= t->y1 || ox >= t->x2 || oy >= t->y2)
                return;
        canvas_emit_frame(&canvas_cpu_sink, (void *)t, frame, r);
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
static b32 compositor_list_busy; // the last COMPOSITOR_LIST_CARD was the card's being busy (a present's or a list's job on it), not its being off or gone
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
        COMPOSITOR_GPU_WHY_ROUND = COMPOSITOR_GPU_WHY_OTHER + 1, // a window with alpha whose picture reaches into a round corner, while gpu-round-off is there (else 12 lays it over)
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
        unsigned int frame_desc;            // byte offset from GPU_VA of the current tiled frame's 8-dword SQ_IMG_RSRC descriptor (0 for linear)
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

// The cover table of a corner's square (r by r bytes, rows from the square's top): the line's outside (content 0), or the picture's inside it (content 1), as
// compositor_corner_rows reads the masks, flipped and mirrored so that the square is read from its own top left. Its place from the buffer's start, 0 for none.
static unsigned int compositor_gpu_table(int r, int b, int corner, int content)
{
        int ri = r > b ? r - b : 0, right = corner & 1, down = (corner & 2) != 0;
        const unsigned char *outer = right ? compositor_corner_mirror(r) : compositor_corner_mask(r);
        const unsigned char *inner = ri ? (right ? compositor_corner_mirror(ri) : compositor_corner_mask(ri)) : 0;
        unsigned char *table;
        unsigned int at = compositor_gpu_slot_tag(r, b, corner, content, 0, (unsigned int)(r * r), &table);

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
        unsigned int at = compositor_gpu_slot_tag(side, half, 4, 3, 0, (unsigned int)(side * side), &table);

        for (int y = 0; table && y < side; y++)
                for (int x = 0; x < side; x++)
                        table[y * side + x] = (unsigned char)(compositor_close_sample(side, half, x, y) * 255 / 16);
        return at;
}

// An 8-dword SQ_IMG_RSRC descriptor placed 32-byte-aligned in the tables pool; returns its byte offset from GPU_VA, or 0 when full.
static unsigned int compositor_gpu_desc(const unsigned int desc[8])
{
        struct compositor_gscene *g = &compositor_gscene;
        unsigned int off = (g->tables_used + 31u) & ~31u;

        if (off > g->tables_size || 32u > g->tables_size - off)
                return 0;
        memory_copy(g->tables + off, desc, 32);
        g->tables_used = off + 32u;
        return g->tables_at + off;
}

// One record: the rectangle cut to the picture, none when nothing is left. A fill writes the colour; a copy takes the picture's pixels from the frame, at the
// rectangle's own corner; a mix (5) is memory_mix_u32_row by the cover table at cover (pitch r), the ink the colour or, with a frame, the picture; 12 is the
// same words with the premultiplied picture laid over by the cover (compositor_over_weighted), a blended picture's round corner.
static void compositor_gpu_rect(unsigned int kind, int x0, int y0, int x1, int y1, unsigned int colour, const struct compositor_frame *frame, unsigned int cover, int r)
{
        struct compositor_gscene *g = &compositor_gscene;
        int cx0 = x0 < g->clip[0] ? g->clip[0] : x0, cy0 = y0 < g->clip[1] ? g->clip[1] : y0, cx1 = x1 > g->clip[2] ? g->clip[2] : x1, cy1 = y1 > g->clip[3] ? g->clip[3] : y1;
        unsigned int *op = g->ops + 8 * g->count;

        if (cx1 <= cx0 || cy1 <= cy0)
                return;
        op[0] = kind, op[1] = (unsigned int)cx0, op[2] = (unsigned int)cy0, op[3] = (unsigned int)cx1, op[4] = (unsigned int)cy1;
        op[5] = colour, op[6] = op[7] = 0;
        if (frame && frame->tiled_kind)
        {
                unsigned int flags = 0x40u | ((frame->tiled_kind & GPU_IMAGE_RAW) ? 0u : 0x20u) | ((frame->tiled_kind & GPU_IMAGE_SWAP) ? 0x80u : 0u);
                unsigned int origin = (unsigned int)((cx0 - frame->x) & 0xffff) | ((unsigned int)((cy0 - frame->y) & 0xffff) << 16);

                op[0] |= flags;
                if (kind == 4 || kind == 11)
                        op[5] = g->frame_desc, op[6] = origin, op[7] = 0;
                else
                        op[6] = g->frame_desc, op[7] = origin;
        }
        else if (frame)
        {
                positive at = compositor_gpu_va(frame) + 4ull * ((positive)(cy0 - frame->y) * frame->pitch + (positive)(cx0 - frame->x));

                if (kind == 4 || kind == 11)
                        op[5] = (unsigned int)at, op[6] = (unsigned int)(at >> 32), op[7] = (unsigned int)frame->pitch;
                else
                        op[0] |= (unsigned int)frame->pitch << 16, op[6] = (unsigned int)at, op[7] = (unsigned int)(at >> 32);
                if (frame->swap)
                        op[0] |= 0x80u;
        }
        if (kind == 5 || kind == 12 || kind == 13)
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

// GPU sink callbacks driving canvas_emit_frame for scene record emission.
static void canvas_gpu_fill(void *ctx, int x, int y, int w, int h, unsigned int colour)
{
        (void)ctx;
        compositor_gpu_rect(0, x, y, x + w, y + h, colour, 0, 0, 0);
}

static void canvas_gpu_contents(void *ctx, const struct compositor_frame *f, unsigned int tab, int x, int y, int w, int h)
{
        (void)ctx;
        if (!f->builtin && !f->pixels)
        {
                if (f->fill)
                        compositor_gpu_rect(0, x, y, x + w, y + h, f->fill, 0, 0, 0);
                return;
        }
        if (f->builtin)
                compositor_gpu_rect(0, x, y, x + w, y + h, tab, 0, 0, 0);
        else
                compositor_gpu_rect(f->blend ? 11 : 4, x, y, x + w, y + h, 0, f, 0, 0);
}

static int canvas_gpu_round_corner(void *ctx, const struct compositor_frame *f, int corner, int r, int b, int ri,
                                   int cx, int cy, int sx1, int sy1, int sx2, int sy2, unsigned int edge, unsigned int tab)
{
        struct compositor_gscene *g = (struct compositor_gscene *)ctx;
        int picture = f->builtin || f->pixels != 0 || f->fill != 0;
        unsigned int edge_table = b ? compositor_gpu_table(r, b, corner, picture ? 0 : 2) : 0;
        unsigned int content_table = picture && ri ? compositor_gpu_table(r, b, corner, 1) : 0;

        if ((b && !edge_table) || (picture && ri && !content_table))
                return g->why = COMPOSITOR_GPU_WHY_POOL, 0;
        if (b)
                compositor_gpu_rect(13, cx, cy, cx + r, cy + r, edge & 0xffffffu, 0, edge_table, r);
        if (picture && ri && sx2 > sx1 && sy2 > sy1)
                compositor_gpu_rect(f->blend ? 12 : 13, sx1, sy1, sx2, sy2, tab, f->builtin ? 0 : f,
                                    content_table + (unsigned int)((sy1 - cy) * r + (sx1 - cx)), r);
        return 1;
}

static int canvas_gpu_lit_corner(void *ctx, const struct compositor_frame *f, int corner, int r, int thick,
                                 int cx, int cy, unsigned int ink)
{
        struct compositor_gscene *g = (struct compositor_gscene *)ctx;
        unsigned int ring = compositor_gpu_table(r, thick, corner, 2);

        (void)f;
        if (!ring)
                return g->why = COMPOSITOR_GPU_WHY_POOL, 0;
        compositor_gpu_rect(13, cx, cy, cx + r, cy + r, ink & 0xffffffu, 0, ring, r);
        return 1;
}

static int canvas_gpu_buttons(void *ctx, const struct compositor_frame *f)
{
        (void)ctx;
        return compositor_gpu_buttons(f);
}

static const struct canvas_sink canvas_gpu_sink = {
    canvas_gpu_fill, canvas_gpu_contents, canvas_gpu_round_corner, canvas_gpu_lit_corner, canvas_gpu_buttons,
};

// A frame as compositor_draw_with draws it (a window with a picture at its own size, or a tab of the panel, and nothing over it): false if it is not one, or the pools are full.
static b32 compositor_gpu_frame(const struct compositor_frame *f)
{
        struct compositor_gscene *g = &compositor_gscene;
        int w = f->width, h = f->height, ox, oy, ow, oh, r = compositor_radius(f), b, ri;

        // A frame of no area has no picture to read (a snap preview's lifted frame is one: its tint is in compositor_gpu_frames).
        if (!f->builtin && !f->tiled_kind && f->pixels && w > 0 && h > 0 && !compositor_gpu_va(f))
                return g->why = COMPOSITOR_GPU_WHY_PICTURE, false;
        if (compositor_scaled(f))
                return g->why = COMPOSITOR_GPU_WHY_SCALED, false;
        g->frame_desc = 0;
        if (!f->builtin && f->tiled_kind && w > 0 && h > 0)
        {
                if (!(g->frame_desc = compositor_gpu_desc(f->tiled_desc)))
                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
        }
        // The record's pitch is 16 bits (compositor_gpu_frame's op[0]): a wider picture is left to the processor.
        if (f->pitch > 65535)
                return g->why = COMPOSITOR_GPU_WHY_ALIGN, false;
        if (g->count + 40 > g->room - 400)
                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
        compositor_outline(f, &ox, &oy, &ow, &oh);
        b = f->x - ox, ri = r > b ? r - b : 0;
        // The owner's way back (/run/moonwater/gpu-round-off, gpu.c's gpu_round_off): a blended window's round corners on the processor, as before 12.
        if (gpu_round_off && f->blend && ri > 0 && (f->square & 15) != 15)
                return g->why = COMPOSITOR_GPU_WHY_ROUND, false;
        return canvas_emit_frame(&canvas_gpu_sink, g, f, r);
}

static b32 compositor_gpu_tint(const int *rect, unsigned int ink, int radius, int edge_line, unsigned int edge);

/*
        The kept wallpaper as a frame the card's records can copy from (op 4, the ground's whole picture): true when there is one as large as the
        card's columns and rows, so the card's ground is the processor's (compositor_ground_fill) and not the colour.
*/
static b32 compositor_gpu_wall_frame(struct compositor_frame *wall)
{
        memory_zero(wall, sizeof(*wall));
        if (compositor_space_all_fullscreen() || !compositor_wall ||
            compositor_wall_w < (int)compositor_gscene.width || compositor_wall_h < (int)compositor_gscene.height)
                return false;
        wall->pixels = compositor_wall, wall->pitch = (unsigned long)compositor_wall_w, wall->width = compositor_wall_w, wall->height = compositor_wall_h;
        return true;
}

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
        // The ground: the kept wallpaper copied (a record of its picture, mapped by compositor_gpu_wall), or the colour where there is none.
        {
                struct compositor_frame wall;

                if (compositor_gpu_wall_frame(&wall))
                {
                        if (!compositor_gpu_va(&wall))
                        {
                                g->why = COMPOSITOR_GPU_WHY_PICTURE;
                                goto refused;
                        }
                        compositor_gpu_rect(4, 0, 0, (int)g->width, (int)g->height, 0, &wall, 0, 0);
                }
                else
                        compositor_gpu_rect(0, 0, 0, (int)g->width, (int)g->height, ground, 0, 0, 0);
        }
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
        The plates of a panel as frames of their own, each a plate's rectangle and nothing of the band round them, every corner
        round at half its height, so each is a pill floating in the band: the launcher's button, the tabs' plate (TASKBAR_TABS) and
        the tray, each the part of the band's pixels it is. With no font they are shapes the scene draws in one ink each, and then
        each tab is a pill of its own, so the tab with the keys keeps its ink (taskbar_tab_ink). Answers how many.
*/
static unsigned int compositor_panel_tabs_with(const struct taskbar_view *view, const struct compositor_frame *band, struct compositor_frame *tabs)
{
        unsigned int count = 0, own = band->builtin ? band->mark_count : 0;

        // With a font: the band's plates, as many as it has (each task one when they are separate).
        for (int slot = 0; slot <= (int)own + (band->builtin ? 2 : 9 + (int)view->count); slot++)
        {
                int which = slot < (int)own ? slot : slot == (int)own ? TASKBAR_LAUNCHER : slot == (int)own + 1 ? TASKBAR_TABS : -2;
                int x, y, width, height;

                // With a font each plate is a frame (taskbar_plate: the band's plates in order); with none each slot is.
                if (band->builtin ? which == TASKBAR_TABS || !taskbar_slot(view, which, &x, &y, &width, &height)
                                  : !taskbar_plate(view, slot, &which, &x, &y, &width, &height))
                        continue;
                // A plate is the band's pixels, so it is no larger than the band: the moon's, grown, reached past the band's edge and
                // read rows the band never drew (black) from before or after its buffer.
                if (x < band->x)
                        width -= band->x - x, x = band->x;
                if (y < band->y)
                        height -= band->y - y, y = band->y;
                if (x + width > band->x + band->width)
                        width = band->x + band->width - x;
                if (y + height > band->y + band->height)
                        height = band->y + band->height - y;
                if (width <= 0 || height <= 0)
                        continue;
                tabs[count] = *band;
                tabs[count].x = x;
                tabs[count].y = y;
                tabs[count].width = width;
                tabs[count].height = height;
                tabs[count].slot = which;
                tabs[count].launcher_lit = band->launcher_lit;
                tabs[count].radius = (width < height ? width : height) / 2;
                tabs[count].line = 0;
                tabs[count].square = 0;
                if (!band->builtin)
                        tabs[count].pixels = band->pixels + (unsigned long)(y - band->y) * band->pitch + (unsigned long)(x - band->x);
                count++;
        }
        return count;
}

static unsigned int compositor_panel_tabs(const struct compositor_frame *band, struct compositor_frame *tabs)
{
        struct taskbar_view view = compositor_taskbar_view();

        return compositor_panel_tabs_with(&view, band, tabs);
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
// The frames a present of `rows` rows may have: two a window (its picture and its line or shadow), the snap preview and the moon's menu a frame
// each, the moon's glow one a band, the overlays and the layers. The arrays sized by it are the rows' (compositor_rows_make), as many as the rows in use.
#define COMPOSITOR_FRAMES_FOR(rows) (2 * (unsigned long)(rows) + 4 + 3 * WINDOWS_OUTPUTS + COMPOSITOR_OVERLAYS + COMPOSITOR_LAYERS)
#define COMPOSITOR_PIECES_FOR(rows) (3 * COMPOSITOR_FRAMES_FOR(rows))

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
HOT static void compositor_mix_run(unsigned int *restrict pixels, long count, unsigned int ink, const unsigned int *restrict colour,
                                   const unsigned char *restrict cover)
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

// Unified pure-geometry tint decomposition shared by CPU (compositor_tint) and GPU (compositor_gpu_tint).
struct canvas_tint_sink
{
        int (*corner)(void *ctx, int corner, int cx, int cy, int r, int line, unsigned int ink, unsigned int edge);
        int (*rect)(void *ctx, int x0, int y0, int x1, int y1, unsigned int argb, int is_edge);
};

static void canvas_tint_clamp(int w, int h, int radius, int edge_line, unsigned int edge, int *out_r, int *out_line)
{
        radius = radius > w / 2 ? w / 2 : radius;
        radius = radius > h / 2 ? h / 2 : radius;
        radius = radius > COMPOSITOR_RADIUS_MOST ? COMPOSITOR_RADIUS_MOST : radius < 0 ? 0 : radius;
        edge_line = edge_line < 0 || !(edge >> 24) ? 0 : edge_line > radius && radius ? radius : edge_line;
        *out_r = radius;
        *out_line = edge_line;
}

static int canvas_emit_tint(const struct canvas_tint_sink *sink, void *ctx, const int *rect, unsigned int ink,
                            int radius, int edge_line, unsigned int edge)
{
        int x = rect[0], y = rect[1], w = rect[2], h = rect[3], r, line;

        if (w <= 0 || h <= 0)
                return 1;
        canvas_tint_clamp(w, h, radius, edge_line, edge, &r, &line);
        if (r > 0)
                for (int corner = 0; corner < 4; corner++)
                {
                        int cx = corner & 1 ? x + w - r : x, cy = corner & 2 ? y + h - r : y;

                        if (!sink->corner(ctx, corner, cx, cy, r, line, ink, edge))
                                return 0;
                }
        for (int py = y; py < y + h;)
        {
                int top = py - y, bottom = y + h - 1 - py, my = top < bottom ? top : bottom, corner = my < r, edge_row = my < line, stop = py + 1;
                int from = corner ? x + r : x + line, to = corner ? x + w - r : x + w - line;

                for (; stop < y + h; stop++)
                {
                        int t = stop - y, b = y + h - 1 - stop, m = t < b ? t : b;

                        if ((m < r) != (corner != 0) || (m < line) != (edge_row != 0))
                                break;
                }
                if (to > from && !sink->rect(ctx, from, py, to, stop, edge_row ? edge : ink, edge_row))
                        return 0;
                if (!corner && line > 0)
                {
                        int left_to = x + line < x + w ? x + line : x + w;
                        int right_from = x + w - line > left_to ? x + w - line : left_to;

                        if ((left_to > x && !sink->rect(ctx, x, py, left_to, stop, edge, 1)) ||
                            (x + w > right_from && !sink->rect(ctx, right_from, py, x + w, stop, edge, 1)))
                                return 0;
                }
                py = stop;
        }
        return 1;
}

struct canvas_cpu_tint_ctx
{
        struct compositor_target cell;
        const struct compositor_frame *owner;
        int ri;
        unsigned int colour[COMPOSITOR_TINT_RUN], edge_colour[COMPOSITOR_TINT_RUN];
        unsigned char cover[COMPOSITOR_TINT_RUN], edge_cover[COMPOSITOR_TINT_RUN];
        struct compositor_shape shape;
        unsigned long made;
};

static void canvas_cpu_tint_cut(const struct canvas_cpu_tint_ctx *c, int py, int x2, int *hide_from, int *hide_to)
{
        *hide_from = *hide_to = x2;
        if (c->owner && py >= c->owner->y && py < c->owner->y + c->owner->height)
        {
                int inside = py >= c->owner->y + c->ri && py < c->owner->y + c->owner->height - c->ri;

                *hide_from = inside ? c->owner->x : c->owner->x + c->ri;
                *hide_to = inside ? c->owner->x + c->owner->width : c->owner->x + c->owner->width - c->ri;
                if (*hide_to <= *hide_from)
                        *hide_from = *hide_to = x2;
        }
}

static int canvas_cpu_tint_corner(void *raw, int corner, int cx, int cy, int r, int line, unsigned int ink, unsigned int edge)
{
        struct canvas_cpu_tint_ctx *c = (struct canvas_cpu_tint_ctx *)raw;
        int x1, y1, x2, y2;

        (void)line, (void)ink, (void)edge;
        if (!target_clip(&c->cell, cx, cy, r, r, &x1, &y1, &x2, &y2))
                return 1;
        for (int py = y1; py < y2; py++)
        {
                int my = corner & 2 ? cy + r - 1 - py : py - cy, hide_from, hide_to, w = x2 - x1;
                unsigned int *at = c->cell.pixels + (unsigned long)(py - c->cell.oy) * c->cell.pitch + (unsigned long)(x1 - c->cell.ox);
                unsigned char edge_cov[COMPOSITOR_RADIUS_MOST], fill_cov[COMPOSITOR_RADIUS_MOST];
                unsigned int any_edge = 0, any_fill = 0;

                canvas_cpu_tint_cut(c, py, x2, &hide_from, &hide_to);
                for (int k = 0; k < w; k++)
                {
                        int px = x1 + k;
                        unsigned int ce, cf;

                        if (px >= hide_from && px < hide_to)
                        {
                                edge_cov[k] = fill_cov[k] = 0;
                                continue;
                        }
                        compositor_shape_covers(&c->shape, px, my, &ce, &cf);
                        edge_cov[k] = (unsigned char)ce, any_edge |= ce;
                        fill_cov[k] = (unsigned char)cf, any_fill |= cf;
                        c->made++;
                }
                if (any_edge)
                        memory_mix_const_u32_rect(at, c->cell.pitch, edge_cov, (positive)w, (positive)w, 1, c->shape.edge & 0xffffffu, 255);
                if (any_fill)
                        memory_mix_const_u32_rect(at, c->cell.pitch, fill_cov, (positive)w, (positive)w, 1, c->shape.fill & 0xffffffu, 255);
        }
        return 1;
}

static int canvas_cpu_tint_rect(void *raw, int x0, int y0, int x1, int y1, unsigned int argb, int is_edge)
{
        struct canvas_cpu_tint_ctx *c = (struct canvas_cpu_tint_ctx *)raw;
        const unsigned int *col = is_edge ? c->edge_colour : c->colour;
        const unsigned char *cov = is_edge ? c->edge_cover : c->cover;
        int rx1, ry1, rx2, ry2;

        if (!target_clip(&c->cell, x0, y0, x1 - x0, y1 - y0, &rx1, &ry1, &rx2, &ry2))
                return 1;
        for (int py = ry1; py < ry2; py++)
        {
                int hide_from, hide_to, lo, hi;
                unsigned int *at = c->cell.pixels + (unsigned long)(py - c->cell.oy) * c->cell.pitch + (unsigned long)(rx1 - c->cell.ox);

                canvas_cpu_tint_cut(c, py, rx2, &hide_from, &hide_to);
                lo = hide_from < rx2 ? hide_from : rx2;
                hi = hide_to > rx1 ? hide_to : rx1;
                if (lo > rx1)
                {
                        compositor_mix_run(at, lo - rx1, argb, col, cov);
                        c->made += (unsigned long)(lo - rx1);
                }
                if (hi < rx2)
                {
                        compositor_mix_run(at + (hi - rx1), rx2 - hi, argb, col, cov);
                        c->made += (unsigned long)(rx2 - hi);
                }
        }
        return 1;
}

static const struct canvas_tint_sink canvas_cpu_tint_sink = {canvas_cpu_tint_corner, canvas_cpu_tint_rect};

static void compositor_tint(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                            const int *radii, const int *rect, unsigned int ink, unsigned int from, int under, int radius,
                            int edge_line, unsigned int edge)
{
        struct canvas_cpu_tint_ctx ctx;
        int r, line;

        if (rect[2] <= 0 || rect[3] <= 0)
                return;
        ctx.cell = compositor_clipped(t, rect[0], rect[1], rect[2], rect[3]);
        if (ctx.cell.x2 <= ctx.cell.x1 || ctx.cell.y2 <= ctx.cell.y1)
                return;
        compositor_reading(t);
        ctx.owner = under && from < count && !frames[from].builtin ? &frames[from] : 0;
        ctx.ri = 0;
        ctx.made = 0;
        memory_fill_u32(ctx.colour, COMPOSITOR_TINT_RUN, ink & 0xffffffu);
        memory_fill(ctx.cover, (int)(ink >> 24), COMPOSITOR_TINT_RUN);
        canvas_tint_clamp(rect[2], rect[3], radius, edge_line, edge, &r, &line);
        ctx.shape = (struct compositor_shape){rect[0], rect[1], rect[2], rect[3], r, line, ink, edge, 0, 0, 0, 0};
        if (r || line)
        {
                memory_fill_u32(ctx.edge_colour, COMPOSITOR_TINT_RUN, edge & 0xffffffu);
                memory_fill(ctx.edge_cover, (int)(edge >> 24), COMPOSITOR_TINT_RUN);
                if (r)
                {
                        ctx.shape.outer = compositor_corner_mask(r), ctx.shape.outer_right = compositor_corner_mirror(r);
                        if (r - line > 0)
                        {
                                ctx.shape.inner = compositor_corner_mask(r - line);
                                ctx.shape.inner_right = compositor_corner_mirror(r - line);
                        }
                }
        }
        if (ctx.owner)
        {
                int ox, oy, ow, oh, oline;

                compositor_outline(ctx.owner, &ox, &oy, &ow, &oh);
                oline = ctx.owner->x - ox;
                ctx.ri = radii[from] > oline ? radii[from] - oline : 0;
        }
        canvas_emit_tint(&canvas_cpu_tint_sink, &ctx, rect, ink, radius, edge_line, edge);
        // The bands of a scene are drawn on threads of their own: each adds its own, once.
        __atomic_fetch_add(&compositor_tint_pixels, ctx.made, __ATOMIC_RELAXED);
        for (unsigned int each = from; each < count; each++)
        {
                int x, y, width, height;

                if (ctx.owner && each == from)
                {
                        compositor_draw_ring(&ctx.cell, ctx.owner, radii[from]);
                        continue;
                }
                compositor_outline(&frames[each], &x, &y, &width, &height);
                if (x < ctx.cell.x2 && x + width > ctx.cell.x1 && y < ctx.cell.y2 && y + height > ctx.cell.y1)
                        compositor_draw_with(&ctx.cell, &frames[each], radii[each]);
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
        unsigned int at = compositor_gpu_slot_tag(-1, (int)alpha, 12, (int)g->width, 0, g->width, &table);

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
        unsigned int at = compositor_gpu_slot_tag(r, line, (part ? 24 : 8) + corner, alpha, 0, (unsigned int)(r * r), &table);
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

static int canvas_gpu_tint_corner(void *ctx, int corner, int cx, int cy, int r, int line, unsigned int ink, unsigned int edge)
{
        struct compositor_gscene *g = (struct compositor_gscene *)ctx;
        int fa = (int)(ink >> 24), ea = (int)(edge >> 24);

        if (line > 0 && ea > 0)
        {
                unsigned int at = compositor_gpu_shape(r, line, ea, corner, 0);

                if (!at)
                        return g->why = COMPOSITOR_GPU_WHY_POOL, 0;
                compositor_gpu_rect(5, cx, cy, cx + r, cy + r, edge & 0xffffffu, 0, at, r);
        }
        if (r - line > 0 && fa > 0)
        {
                unsigned int at = compositor_gpu_shape(r, line, fa, corner, 1);

                if (!at)
                        return g->why = COMPOSITOR_GPU_WHY_POOL, 0;
                compositor_gpu_rect(5, cx, cy, cx + r, cy + r, ink & 0xffffffu, 0, at, r);
        }
        return 1;
}

static int canvas_gpu_tint_rect(void *ctx, int x0, int y0, int x1, int y1, unsigned int argb, int is_edge)
{
        struct compositor_gscene *g = (struct compositor_gscene *)ctx;

        (void)is_edge;
        if (!compositor_gpu_const_rect(x0, y0, x1, y1, argb & 0xffffffu, argb >> 24))
                return g->why = COMPOSITOR_GPU_WHY_POOL, 0;
        return 1;
}

static const struct canvas_tint_sink canvas_gpu_tint_sink = {canvas_gpu_tint_corner, canvas_gpu_tint_rect};

/*
        A tint as compositor_tint draws it over the whole scene, with nothing of it left out (a snap preview lifted into a frame of its own, whose window is
        drawn again over it, or a shared edge's mark), as records via canvas_emit_tint.
*/
static b32 compositor_gpu_tint(const int *rect, unsigned int ink, int radius, int edge_line, unsigned int edge)
{
        struct compositor_gscene *g = &compositor_gscene;

        if (rect[2] <= 0 || rect[3] <= 0)
                return true;
        if (g->count + 64 > g->room - 400)
                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
        return canvas_emit_tint(&canvas_gpu_tint_sink, g, rect, ink, radius, edge_line, edge);
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
/*
        Whether the blended frame at has nothing under it in t but the ground: no frame before it whose outline meets its
        own inside t. The frames below are taken cut to t, the damage, the band or the slice being drawn and not the
        screen, so a band that a window under the blended one does not reach draws the blended one over the ground and the
        band that it reaches draws the two as they lie. A frame that does not meet t has nothing to draw there and answers
        yes. The rows where one does meet a frame under it are compositor_blend_under's.
*/
static int compositor_blend_under(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int at,
                                  int *top, int *bottom)
{
        int ax, ay, aw, ah, x1, y1, x2, y2, under = 0;

        compositor_outline(&frames[at], &ax, &ay, &aw, &ah);
        x1 = ax > t->x1 ? ax : t->x1, y1 = ay > t->y1 ? ay : t->y1;
        x2 = ax + aw < t->x2 ? ax + aw : t->x2, y2 = ay + ah < t->y2 ? ay + ah : t->y2;
        if (x2 <= x1 || y2 <= y1)
                return 0;
        for (unsigned int below = 0; below < at; below++)
        {
                int bx, by, bw, bh;

                compositor_outline(&frames[below], &bx, &by, &bw, &bh);
                if (bx >= x2 || bx + bw <= x1 || by >= y2 || by + bh <= y1)
                        continue;
                *top = (by > y1 ? by : y1) < *top ? (by > y1 ? by : y1) : *top;
                *bottom = (by + bh < y2 ? by + bh : y2) > *bottom ? (by + bh < y2 ? by + bh : y2) : *bottom;
                under = 1;
        }
        return under;
}

static int compositor_blend_alone(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int at)
{
        int top = t->y2, bottom = t->y1;

        return !compositor_blend_under(t, frames, at, &top, &bottom);
}

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
        int (*box)[4];      // COMPOSITOR_PIECES_FOR the rows in use of each, the rows' (compositor_plan_box and the rest)
        unsigned int *owner;
        unsigned int pieces;
        int *edge;          // the damage's top and bottom, and every piece's top or bottom inside them, sorted
        unsigned int edges;
};

/*
        The one plan's memory (a plan is made on the loop's thread, one damage at a time), and each band's edges and spans (the band
        threads' COMPOSITOR_BANDS, and the loop's own scene after them): arrays of the rows' reservation (compositor_rows_make), each with
        room for the pieces of every row, where they were arrays on the stack of the pieces of WINDOWS rows.
*/
static int (*compositor_plan_box)[4];
static unsigned int *compositor_plan_owner;
static int *compositor_plan_edge;
static int *compositor_lane_edge[9];
static int (*compositor_lane_span[9])[2];
static int *compositor_scene_radii;

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
static void compositor_plan_make(struct compositor_plan *plan, const struct compositor_target *t,
                                 const struct compositor_frame *frames, unsigned int count, const int *radii)
{
        int (*box)[4], *edge;
        unsigned int *owner;
        unsigned int edges = 2, pieces = 0;

        plan->box = compositor_plan_box, plan->owner = compositor_plan_owner, plan->edge = compositor_plan_edge;
        box = plan->box, edge = plan->edge, owner = plan->owner;

        edge[0] = t->y1;
        edge[1] = t->y2;

        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_frame *f = &frames[at];
                int r = radii[at], fx, fy, fw, fh, x2, y2;
                unsigned int first = pieces;

                compositor_outline(f, &fx, &fy, &fw, &fh);
                x2 = fx + fw, y2 = fy + fh;
                if (x2 <= t->x1 || fx >= t->x2 || y2 <= t->y1 || fy >= t->y2)
                        continue;
                if (!r)
                {
                        box[pieces][0] = fx, box[pieces][1] = fy, box[pieces][2] = x2, box[pieces][3] = y2;
                        owner[pieces++] = at;
                }
                else
                {
                        box[pieces][0] = fx + r, box[pieces][1] = fy, box[pieces][2] = x2 - r, box[pieces][3] = y2;
                        owner[pieces++] = at;
                        box[pieces][0] = fx, box[pieces][1] = fy + r, box[pieces][2] = fx + r, box[pieces][3] = y2 - r;
                        owner[pieces++] = at;
                        box[pieces][0] = x2 - r, box[pieces][1] = fy + r, box[pieces][2] = x2, box[pieces][3] = y2 - r;
                        owner[pieces++] = at;
                }

                for (unsigned int piece = first; piece < pieces; piece++)
                        for (unsigned int side = 1; side < 4; side += 2)
                        {
                                int ey = box[piece][side];

                                if (ey > t->y1 && ey < t->y2)
                                {
                                        unsigned int place = edges;

                                        while (place && edge[place - 1] > ey)
                                                place--;
                                        if (place && edge[place - 1] == ey)
                                                continue;
                                        memory_copy(edge + place + 1, edge + place, (edges - place) * sizeof(edge[0]));
                                        edge[place] = ey;
                                        edges++;
                                }
                        }
        }
        plan->pieces = pieces;
        plan->edges = edges;
}

// The scene of a band (or slice) of a damage, from its plan: the band's edges are its top and bottom and the plan's
// that lie strictly inside them, which are the ones the band would have cut at on its own.
static void compositor_scene_plan(const struct compositor_target *t, const struct compositor_frame *frames,
                                  unsigned int count, const int *radii, const struct compositor_plan *plan, unsigned int lane)
{
        unsigned int *scratch = t->scratch ? t->scratch : compositor_corner_scratch;
        int *edge = compositor_lane_edge[lane];
        const int (*box)[4] = plan->box;
        const unsigned int *owner = plan->owner;
        unsigned int edges = 0, pieces = plan->pieces;

        // A blended frame with a frame under it here: every frame drawn over the last, from the ground up. One with the ground
        // alone under it is a piece of the plan, as an opaque one is, laid over the ground (blend 2 on the copy drawn).
        for (unsigned int at = 0; at < count; at++)
                if (frames[at].blend && !compositor_blend_alone(t, frames, at))
                {
                        compositor_ground_fill(t, t->x1, t->y1, t->x2 - t->x1, t->y2 - t->y1);
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
                int (*span)[2] = compositor_lane_span[lane];
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
                                if (frames[owner[at]].blend)
                                {
                                        struct compositor_frame alone = frames[owner[at]];

                                        alone.blend = 2;
                                        compositor_draw_with(&cell, &alone, radii[owner[at]]);
                                }
                                else
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
                        compositor_ground_fill(t, span[s][0], top, span[s][1] - span[s][0], bottom - top);
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
                        compositor_ground_fill(&square, cx, cy, r, r);
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
        int *radii = compositor_scene_radii;
        struct compositor_plan plan;

        compositor_radii(frames, count, radii);
        compositor_plan_make(&plan, t, frames, count, radii);
        compositor_scene_plan(t, frames, count, radii, &plan, 8); // the loop's lane, after the bands' (COMPOSITOR_BANDS)
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
_Static_assert(sizeof(compositor_lane_edge) / sizeof(compositor_lane_edge[0]) == COMPOSITOR_BANDS + 1, "a lane a band and the loop's");

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
        A frame blended over another frame (frame->blend, compositor_blend_under) has every frame drawn over the last in the
        target, in each band or slice that holds a row of the two meeting, so those rows read it; one with the ground alone
        under it is laid over a row of the ground and reads nothing. A tint (a snap preview, a shared edge's mark) mixes over
        the target and has the frames over it drawn again over its rectangle, so its rows do. Answers whether any row reads,
        and the rows from *top to *bottom, inside the target's.
*/
static int compositor_scene_reads(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count,
                                  int *top, int *bottom)
{
        *top = t->y2, *bottom = t->y1;
        for (unsigned int at = 0; at < count; at++)
        {
                if (frames[at].blend)
                        compositor_blend_under(t, frames, at, top, bottom);
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

// The slices of the rows a scene reads, COMPOSITOR_SLICE rows of the damage's width for each band, mapped when a scene first reads
// and grown as the glass's scratch is (compositor_scratch, glass.c).
static unsigned int *compositor_aside;
static unsigned long compositor_aside_bytes;
static unsigned int *compositor_scratch(unsigned int **at, unsigned long *had, unsigned long bytes);

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
                        compositor_scene_plan(&band, banding->frames, banding->count, banding->radii, banding->plan, (unsigned int)index);
                        return;
                }
                part.out = 1, part.y2 = a;
                if (part.y2 > part.y1)
                        compositor_scene_plan(&part, banding->frames, banding->count, banding->radii, banding->plan, (unsigned int)index);
                for (int top = a; top < b; top += COMPOSITOR_SLICE)
                {
                        struct compositor_target slice = band;
                        unsigned long pitch = (unsigned long)(band.x2 - band.x1);

                        slice.y1 = top;
                        slice.y2 = top + COMPOSITOR_SLICE < b ? top + COMPOSITOR_SLICE : b;
                        slice.pixels = compositor_aside + index * COMPOSITOR_SLICE * pitch;
                        slice.pitch = pitch;
                        slice.ox = band.x1, slice.oy = top, slice.out = 0;
                        compositor_scene_plan(&slice, banding->frames, banding->count, banding->radii, banding->plan, (unsigned int)index);
                        compositor_blit(&band, band.x1, top, band.x2 - band.x1, slice.y2 - top, slice.pixels, pitch);
                }
                part.y1 = b, part.y2 = band.y2;
                if (part.y2 > part.y1)
                        compositor_scene_plan(&part, banding->frames, banding->count, banding->radii, banding->plan, (unsigned int)index);
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
                compositor_scene_plan(&slice, banding->frames, banding->count, banding->radii, banding->plan, (unsigned int)index);
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
        int *radii = compositor_scene_radii;
        struct compositor_plan plan;

        compositor_radii(frames, count, radii);
        compositor_plan_make(&plan, t, frames, count, radii);
        struct compositor_banding banding = {t, frames, count, 1, apart, radii, &plan, {0}, {0}, 0, 0};

        bands = bands < COMPOSITOR_BANDS ? bands : COMPOSITOR_BANDS;
        bands = bands < (positive)(t->y2 - t->y1) / 16 ? bands : (positive)(t->y2 - t->y1) / 16;
        // A target only written that the scene reads: room for each band's slice, as wide as the damage, made here before the bands.
        if (!apart && compositor_scene_reads(t, frames, count, &banding.read_top, &banding.read_bottom))
        {
                compositor_scratch(&compositor_aside, &compositor_aside_bytes,
                                   (unsigned long)COMPOSITOR_BANDS * COMPOSITOR_SLICE * (unsigned long)(t->x2 - t->x1) * 4);
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
        int x1, y1, x2, y2;
        unsigned int fill = COMPOSITOR_SPOT_FILL * alpha / 255, edge = COMPOSITOR_SPOT_RING * alpha / 255;
        const unsigned char *outer_mask, *inner_mask;

        if (!alpha || inner < 1 || !target_clip(t, cx - radius, cy - radius, 2 * radius, 2 * radius, &x1, &y1, &x2, &y2))
                return;
        compositor_reading(t);
        outer_mask = compositor_corner_mask(radius);
        inner_mask = compositor_corner_mask(inner);
        if (x2 - x1 <= 512)
        {
                int w = x2 - x1;
                unsigned char in_row[512], ring_row[512];

                for (int py = y1; py < y2; py++)
                {
                        int my = py < cy ? py - (cy - radius) : cy + radius - 1 - py;
                        unsigned int *at = t->pixels + (unsigned long)(py - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);
                        unsigned int any_in = 0, any_ring = 0;

                        for (int k = 0; k < w; k++)
                        {
                                int px = x1 + k, mx = px < cx ? px - (cx - radius) : cx + radius - 1 - px;
                                unsigned int out = outer_mask[my * radius + mx];
                                unsigned int in = mx >= ring && my >= ring ? inner_mask[(my - ring) * inner + (mx - ring)] : 0;
                                unsigned int diff = out > in ? out - in : 0;

                                in_row[k] = (unsigned char)in, any_in |= in;
                                ring_row[k] = (unsigned char)diff, any_ring |= diff;
                        }
                        if (any_in && fill)
                                memory_mix_const_u32_rect(at, t->pitch, in_row, (positive)w, (positive)w, 1, COMPOSITOR_INK_LINE_HELD, fill);
                        if (any_ring && edge)
                                memory_mix_const_u32_rect(at, t->pitch, ring_row, (positive)w, (positive)w, 1, COMPOSITOR_INK_LINE_HELD, edge);
                }
                return;
        }
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

/*
        Native Canvas external-surface pixel & orientation blitter primitives:
        BT.601 NV12->XRGB decode, premultiplied alpha-over, 8/10-bit FourCC row conversion,
        and unified 8-orientation bit-table crop/scale/y-invert blitter shared across protocols.
*/
#define CANVAS_FOURCC_ARGB8888    0x34325241u
#define CANVAS_FOURCC_XRGB8888    0x34325258u
#define CANVAS_FOURCC_ABGR8888    0x34324241u
#define CANVAS_FOURCC_XBGR8888    0x34324258u
#define CANVAS_FOURCC_ARGB2101010 0x30335241u
#define CANVAS_FOURCC_XRGB2101010 0x30335258u
#define CANVAS_FOURCC_ABGR2101010 0x30334241u
#define CANVAS_FOURCC_XBGR2101010 0x30334258u
#define CANVAS_FOURCC_NV12        0x3231564eu

static unsigned int canvas_ext_yuv_rgb(long yv, long u, long v)
{
        long c = yv < 16 ? 0 : yv - 16, d = u - 128, e = v - 128;
        long r = (298 * c + 409 * e + 128) >> 8;
        long g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        long b = (298 * c + 516 * d + 128) >> 8;

        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        return 0xff000000u | (unsigned int)r << 16 | (unsigned int)g << 8 | (unsigned int)b;
}

static unsigned int canvas_ext_over(unsigned int over, unsigned int under)
{
        unsigned int through = 255u - (over >> 24), out = 0xff000000u;

        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                unsigned int sum = ((over >> shift) & 255u) + ((((under >> shift) & 255u) * through + 127u) / 255u);

                out |= (sum > 255u ? 255u : sum) << shift;
        }
        return out;
}

static inline INLINE CONST unsigned int canvas_ext_pixel_argb(unsigned int v, unsigned int fourcc)
{
        unsigned int r, g, b, a;

        switch (fourcc)
        {
        case CANVAS_FOURCC_ABGR8888:
#if defined(__x86_64__)
                __asm__("bswap %0\n\tror $8, %0" : "+r"(v));
                return v;
#elif defined(__aarch64__)
                __asm__("rev %w0, %w0\n\tror %w0, %w0, #8" : "+r"(v));
                return v;
#else
                return (v & 0xff00ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu);
#endif
        case CANVAS_FOURCC_XBGR8888:
#if defined(__x86_64__)
                __asm__("bswap %0\n\tror $8, %0" : "+r"(v));
                return v | 0xff000000u;
#elif defined(__aarch64__)
                __asm__("rev %w0, %w0\n\tror %w0, %w0, #8" : "+r"(v));
                return v | 0xff000000u;
#else
                return 0xff000000u | (v & 0x0000ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu);
#endif
        case CANVAS_FOURCC_ARGB2101010:
        case CANVAS_FOURCC_XRGB2101010:
                r = (v >> 22) & 0xffu, g = (v >> 12) & 0xffu, b = (v >> 2) & 0xffu;
                a = fourcc == CANVAS_FOURCC_XRGB2101010 ? 0xffu : ((v >> 30) * 85u);
                return (a << 24) | (r << 16) | (g << 8) | b;
        case CANVAS_FOURCC_ABGR2101010:
        case CANVAS_FOURCC_XBGR2101010:
                b = (v >> 22) & 0xffu, g = (v >> 12) & 0xffu, r = (v >> 2) & 0xffu;
                a = fourcc == CANVAS_FOURCC_XBGR2101010 ? 0xffu : ((v >> 30) * 85u);
                return (a << 24) | (r << 16) | (g << 8) | b;
        case CANVAS_FOURCC_XRGB8888:
                return v | 0xff000000u;
        default:
                return v;
        }
}

static void canvas_ext_convert_row(unsigned int *dst, const unsigned int *src, long count, unsigned int fourcc, int blend)
{
        if (!blend || fourcc == CANVAS_FOURCC_XRGB8888 || fourcc == CANVAS_FOURCC_XBGR8888 ||
            fourcc == CANVAS_FOURCC_XRGB2101010 || fourcc == CANVAS_FOURCC_XBGR2101010)
        {
                switch (fourcc)
                {
                case CANVAS_FOURCC_ARGB8888:
                        memory_copy_apart(dst, src, (positive)count * 4);
                        return;
                case CANVAS_FOURCC_XRGB8888:
                        memory_or_u32(dst, src, (positive)count, 0xff000000u);
                        return;
                case CANVAS_FOURCC_ABGR8888:
                        memory_swizzle_rb_u32(dst, src, (positive)count, 0u);
                        return;
                case CANVAS_FOURCC_XBGR8888:
                        memory_swizzle_rb_u32(dst, src, (positive)count, 0xff000000u);
                        return;
                case CANVAS_FOURCC_ARGB2101010:
                        memory_pack_2101010_u32(dst, src, (positive)count, 0u);
                        return;
                case CANVAS_FOURCC_XRGB2101010:
                        memory_pack_2101010_u32(dst, src, (positive)count, 2u);
                        return;
                case CANVAS_FOURCC_ABGR2101010:
                        memory_pack_2101010_u32(dst, src, (positive)count, 1u);
                        return;
                case CANVAS_FOURCC_XBGR2101010:
                        memory_pack_2101010_u32(dst, src, (positive)count, 3u);
                        return;
                default:
                        for (long i = 0; i < count; i++)
                                dst[i] = canvas_ext_pixel_argb(src[i], fourcc);
                        return;
                }
        }
        if (fourcc == CANVAS_FOURCC_ARGB8888)
        {
                memory_over_u32_rect(dst, (positive)count, src, (positive)count, (positive)count, 1);
                return;
        }
        if (fourcc == CANVAS_FOURCC_ABGR8888 || fourcc == CANVAS_FOURCC_ARGB2101010 || fourcc == CANVAS_FOURCC_ABGR2101010)
        {
                unsigned int chunk[1024];

                for (long off = 0; off < count; off += 1024)
                {
                        long take = count - off < 1024 ? count - off : 1024;

                        if (fourcc == CANVAS_FOURCC_ABGR8888)
                                memory_swizzle_rb_u32(chunk, src + off, (positive)take, 0u);
                        else
                                memory_pack_2101010_u32(chunk, src + off, (positive)take, fourcc == CANVAS_FOURCC_ABGR2101010 ? 1u : 0u);
                        memory_over_u32_rect(dst + off, (positive)take, chunk, (positive)take, (positive)take, 1);
                }
                return;
        }
        for (long i = 0; i < count; i++)
                dst[i] = canvas_ext_over(canvas_ext_pixel_argb(src[i], fourcc), dst[i]);
}

struct canvas_ext_blit
{
        const unsigned char *pixels, *uv_base;
        unsigned long stride, uv_stride;
        long width, height;
        unsigned int fourcc;
        int transform, y_invert, blend, streaming;
        long src_x, src_y, src_w, src_h; // 24.8 fixed-point; src_w <= 0 means full transformed buffer
        long dst_w, dst_h;               // <= 0 means transformed buffer size
        long dest_x, dest_y;
        unsigned int *to;
        unsigned long to_pitch;
        long to_w, to_h;
        const int (*rects)[4];           // optional [x, y, w, h] damage rects (null/0 -> full)
        long rect_count;
};

static int canvas_ext_xform_blit(const struct canvas_ext_blit *b, int out_box[4])
{
        long bw, bh, bw_t, bh_t, tw, th, sx256, sy256, sw256, sh256, dx0, dy0, ox1, oy1, ox2, oy2;
        int tr;

        if (!b || !b->pixels || !b->to || b->width <= 0 || b->height <= 0)
                return 0;
        bw = b->width, bh = b->height, tr = b->transform & 7;
        bw_t = (tr & 1) ? bh : bw, bh_t = (tr & 1) ? bw : bh;
        tw = b->dst_w > 0 ? b->dst_w : bw_t, th = b->dst_h > 0 ? b->dst_h : bh_t;
        sx256 = b->src_w > 0 ? b->src_x : 0, sy256 = b->src_w > 0 ? b->src_y : 0;
        sw256 = b->src_w > 0 ? b->src_w : bw_t * 256, sh256 = b->src_w > 0 ? b->src_h : bh_t * 256;
        dx0 = b->dest_x, dy0 = b->dest_y;
        ox1 = dx0 < 0 ? -dx0 : 0, oy1 = dy0 < 0 ? -dy0 : 0;
        ox2 = dx0 + tw > b->to_w ? b->to_w - dx0 : tw;
        oy2 = dy0 + th > b->to_h ? b->to_h - dy0 : th;
        if (tw <= 0 || th <= 0 || ox2 <= ox1 || oy2 <= oy1)
                return 0;
        if (out_box)
                out_box[0] = (int)(dx0 + ox1), out_box[1] = (int)(dy0 + oy1), out_box[2] = (int)(dx0 + ox2), out_box[3] = (int)(dy0 + oy2);

        // Fast unscaled, untransformed 32-bit RGB path with optional damage-rect clipping and WC streaming row reads.
        if (b->fourcc != CANVAS_FOURCC_NV12 && !tr &&
            sx256 == 0 && sy256 == 0 && sw256 == tw * 256 && sh256 == th * 256 && tw == bw && th == bh)
        {
                long count = b->rects && b->rect_count > 0 ? b->rect_count : 1;
                unsigned int row_buf[4096];
                unsigned long src_pitch = b->stride / 4;
                const unsigned int *src_pixels = (const unsigned int *)b->pixels;
                int touched = 0;

                for (long at = 0; at < count; at++)
                {
                        long left = b->rects && b->rect_count > 0 ? b->rects[at][0] : 0;
                        long top = b->rects && b->rect_count > 0 ? b->rects[at][1] : 0;
                        long right = b->rects && b->rect_count > 0 ? left + b->rects[at][2] : bw;
                        long bottom = b->rects && b->rect_count > 0 ? top + b->rects[at][3] : bh;

                        left = left < 0 ? 0 : left, top = top < 0 ? 0 : top;
                        right = right > bw ? bw : right, bottom = bottom > bh ? bh : bottom;
                        if (dx0 + left < 0)
                                left = -dx0;
                        if (dy0 + top < 0)
                                top = -dy0;
                        if (dx0 + right > b->to_w)
                                right = b->to_w - dx0;
                        if (dy0 + bottom > b->to_h)
                                bottom = b->to_h - dy0;
                        if (right <= left || bottom <= top)
                                continue;
                        {
                                long w = right - left, h = bottom - top;
                                unsigned int *dst_base = b->to + (unsigned long)(dy0 + top) * b->to_pitch + (unsigned long)(dx0 + left);

                                for (long ry = 0; ry < h; ry++)
                                {
                                        long sy = b->y_invert ? (bh - 1 - (top + ry)) : (top + ry);
                                        const unsigned int *src_row = src_pixels + (unsigned long)sy * src_pitch + (unsigned long)left;

                                        if (b->streaming && w <= 4096)
                                        {
                                                memory_copy_rect_stream_u32(row_buf, (positive)w, src_row, src_pitch, (positive)w, 1);
                                                src_row = row_buf;
                                        }
                                        canvas_ext_convert_row(dst_base + (unsigned long)ry * b->to_pitch, src_row, w, b->fourcc, b->blend);
                                }
                        }
                        if (out_box)
                        {
                                int x1 = (int)(dx0 + left), y1 = (int)(dy0 + top), x2 = (int)(dx0 + right), y2 = (int)(dy0 + bottom);

                                if (!touched)
                                        out_box[0] = x1, out_box[1] = y1, out_box[2] = x2, out_box[3] = y2;
                                else
                                {
                                        out_box[0] = x1 < out_box[0] ? x1 : out_box[0], out_box[1] = y1 < out_box[1] ? y1 : out_box[1];
                                        out_box[2] = x2 > out_box[2] ? x2 : out_box[2], out_box[3] = y2 > out_box[3] ? y2 : out_box[3];
                                }
                        }
                        touched = 1;
                }
                return touched;
        }
        // Fast scaled 32-bit RGB without transform/y_invert/crop/blend/streaming.
        if ((b->fourcc == CANVAS_FOURCC_ARGB8888 || b->fourcc == CANVAS_FOURCC_XRGB8888 ||
             b->fourcc == CANVAS_FOURCC_ABGR8888 || b->fourcc == CANVAS_FOURCC_XBGR8888) &&
            !tr && !b->y_invert && !b->blend && !b->streaming &&
            sx256 == 0 && sy256 == 0 && sw256 == bw * 256 && sh256 == bh * 256)
        {
                unsigned int *dst_base = b->to + (unsigned long)(dy0 + oy1) * b->to_pitch + (unsigned long)(dx0 + ox1);
                positive rw = (positive)(ox2 - ox1), rh = (positive)(oy2 - oy1);

                memory_resample_u32_rect(dst_base, b->to_pitch, rw, rh,
                                         (const unsigned int *)b->pixels, (positive)(b->stride / 4),
                                         (positive)bw, (positive)bh, (positive)tw, (positive)th,
                                         (positive)ox1, (positive)oy1);
                if (b->fourcc == CANVAS_FOURCC_ABGR8888 || b->fourcc == CANVAS_FOURCC_XBGR8888)
                {
                        p32 mask = b->fourcc == CANVAS_FOURCC_XBGR8888 ? 0xff000000u : 0u;

                        for (positive ry = 0; ry < rh; ry++)
                                memory_swizzle_rb_u32(dst_base + ry * b->to_pitch, dst_base + ry * b->to_pitch, rw, mask);
                }
                else if (b->fourcc == CANVAS_FOURCC_XRGB8888)
                        for (positive ry = 0; ry < rh; ry++)
                                memory_or_u32(dst_base + ry * b->to_pitch, dst_base + ry * b->to_pitch, rw, 0xff000000u);
                return 1;
        }
        // Fast 1:1 unscaled NV12 -> XRGB row path via lib.c SIMD (AVX2/SSE2/NEON).
        if (b->fourcc == CANVAS_FOURCC_NV12 && !tr && !b->blend && b->uv_base && b->uv_stride > 0 &&
            sx256 == 0 && sy256 == 0 && sw256 == bw * 256 && sh256 == bh * 256 && tw == bw && th == bh && (ox1 & 1L) == 0)
        {
                for (long oy = oy1; oy < oy2; oy++)
                {
                        long by = b->y_invert ? bh - 1 - oy : oy;
                        unsigned int *dst = b->to + (unsigned long)(dy0 + oy) * b->to_pitch + (unsigned long)(dx0 + ox1);
                        const unsigned char *y_row = b->pixels + (unsigned long)by * b->stride + (unsigned long)ox1;
                        const unsigned char *uv_row = b->uv_base + (unsigned long)(by >> 1) * b->uv_stride + (unsigned long)ox1;

                        memory_nv12_to_xrgb_row(dst, y_row, uv_row, (positive)(ox2 - ox1));
                }
                return 1;
        }
        // Fast scaled/cropped opaque NV12 -> XRGB path: SIMD-convert each unique source row once, then gather.
        if (b->fourcc == CANVAS_FOURCC_NV12 && !tr && !b->blend && b->uv_base && b->uv_stride > 0 && bw > 0 && bw <= 4096 && ox2 <= 4096)
        {
                long tx_buf[4096], min_bx = bw, max_bx = 0, last_by = -1;
                unsigned int nv_row[4096];

                for (long ox = ox1; ox < ox2; ox++)
                {
                        long tx = (sx256 + ((ox * 2 + 1) * sw256) / (2 * tw)) >> 8;

                        tx = tx < 0 ? 0 : tx >= bw ? bw - 1 : tx;
                        tx_buf[ox] = tx;
                        if (tx < min_bx)
                                min_bx = tx;
                        if (tx > max_bx)
                                max_bx = tx;
                }
                if (min_bx <= max_bx)
                {
                        long x_even = min_bx & ~1L, x_len = max_bx - x_even + 1;

                        for (long oy = oy1; oy < oy2; oy++)
                        {
                                unsigned int *dst = b->to + (unsigned long)(dy0 + oy) * b->to_pitch + (unsigned long)dx0;
                                long ty = (sy256 + ((oy * 2 + 1) * sh256) / (2 * th)) >> 8;
                                long by;

                                ty = ty < 0 ? 0 : ty >= bh ? bh - 1 : ty;
                                by = b->y_invert ? bh - 1 - ty : ty;
                                if (by != last_by)
                                {
                                        memory_nv12_to_xrgb_row(nv_row + x_even,
                                                                b->pixels + (unsigned long)by * b->stride + (unsigned long)x_even,
                                                                b->uv_base + (unsigned long)(by >> 1) * b->uv_stride + (unsigned long)x_even,
                                                                (positive)x_len);
                                        last_by = by;
                                }
                                for (long ox = ox1; ox < ox2; ox++)
                                        dst[ox] = nv_row[tx_buf[ox]];
                        }
                }
                return 1;
        }
        // General 8-orientation bit-table blitter (0x36 X-flip / 0x6c Y-flip across Wayland/DRM transforms 0..7) + NV12/10-bit/crop/blend.
        {
                long tx_buf[4096];
                int have_tx = ox2 <= 4096;

                if (have_tx)
                        for (long ox = ox1; ox < ox2; ox++)
                        {
                                long tx = (sx256 + ((ox * 2 + 1) * sw256) / (2 * tw)) >> 8;

                                tx_buf[ox] = tx < 0 ? 0 : tx >= bw_t ? bw_t - 1 : tx;
                        }
                for (long oy = oy1; oy < oy2; oy++)
                {
                        unsigned int *dst = b->to + (unsigned long)(dy0 + oy) * b->to_pitch + (unsigned long)dx0;
                        long ty = (sy256 + ((oy * 2 + 1) * sh256) / (2 * th)) >> 8;

                        ty = ty < 0 ? 0 : ty >= bh_t ? bh_t - 1 : ty;
                        for (long ox = ox1; ox < ox2; ox++)
                        {
                                long tx = have_tx ? tx_buf[ox] : ((sx256 + ((ox * 2 + 1) * sw256) / (2 * tw)) >> 8);
                                long ax, ay, bx, by;
                                unsigned int px;

                                if (!have_tx)
                                        tx = tx < 0 ? 0 : tx >= bw_t ? bw_t - 1 : tx;
                                ax = (tr & 1) ? ty : tx, ay = (tr & 1) ? tx : ty;
                                bx = ((0x36 >> tr) & 1) ? bw - 1 - ax : ax;
                                by = ((0x6c >> tr) & 1) ? bh - 1 - ay : ay;
                                if (b->y_invert)
                                        by = bh - 1 - by;
                                if (b->fourcc == CANVAS_FOURCC_NV12)
                                {
                                        long yv = (long)b->pixels[(unsigned long)by * b->stride + (unsigned long)bx];
                                        long u = 128, v = 128;

                                        if (b->uv_base && b->uv_stride > 0)
                                        {
                                                const unsigned char *uv = b->uv_base + (unsigned long)(by >> 1) * b->uv_stride + (unsigned long)(bx & ~1L);

                                                u = (long)uv[0], v = (long)uv[1];
                                        }
                                        px = canvas_ext_yuv_rgb(yv, u, v);
                                }
                                else
                                        px = canvas_ext_pixel_argb(*(const unsigned int *)(b->pixels + (unsigned long)by * b->stride + (unsigned long)bx * 4), b->fourcc);
                                dst[ox] = b->blend ? canvas_ext_over(px, dst[ox]) : px;
                        }
                }
        }
        return 1;
}

/* ---- end of the backend ---- */

