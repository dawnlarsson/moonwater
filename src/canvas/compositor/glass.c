/*
        The glass under the panel and the tabs, and under a glass window: the grain, the frost, the blur and the scene
        it is made from, the band drawn through it and slid, and a close button's mean.

        A pipeline over pixels already composed: it does not decide what changed (damage.c), what the band holds or where
        it is (the taskbar, ui/taskbar.c, through compositor_taskbar_view), or how large the band's and the glass's
        memory is (compositor_band_room, compositor.c). It reads the compositor's state, compose.c's scene and damage.c's
        rectangles and views, so it comes after them. Called by present.c (the glass pass, the band, the close button's
        mean), session.c and manage.c (a window's side of the glass, compositor_glass_drop) and compositor.c's loop (the
        band's slide).
*/

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
                gpu_forget((const void *)(*at), (positive)(*had)), compositor_call(syscall(munmap), (positive)*at, *had, 0, 0, 0, 0);
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
        {
                unsigned long pitch;
                const unsigned int *made = compositor_rise(top, (unsigned long)sw, sx, sy, x0, y0, x1, y1, &pitch);

                if (!made)
                        return 0;
                memory_copy_rect_u32(into, (unsigned long)gw, made, pitch, (unsigned long)(x1 - x0), (unsigned long)(y1 - y0));
                if (x1 - x0 < gw)
                        memory_fill_u32_rect(into + (x1 - x0), (positive)gw, (positive)(gw - (x1 - x0)), (positive)(y1 - y0), 0);
                if (y1 - y0 < gh)
                        memory_zero(into + (unsigned long)(y1 - y0) * gw, (positive)(gh - (y1 - y0)) * (positive)gw * 4);
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

// The damage of the band's move: where it was (before) and where it is now, on every band. A slide damages nothing else.
static void compositor_taskbar_damage(b64 before)
{
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();
                int x, y, width, height;

                taskbar_slide_rect(&view, before, &x, &y, &width, &height);
                compositor_band_damage(x, y, width, height);
        }
        compositor.band_at = 0;
}

// Whether a window is fullscreen and on the screen: a fullscreen window that is put away is not, and the taskbar slides away for one
// that is (taskbar_slide_now).
static int compositor_fullscreen_up(void)
{
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if (compositor.fullscreen[row] && compositor_visible((int)row))
                        return 1;
        return 0;
}

// The taskbar's place, asked on every pass of the loop before a frame is drawn, with the clock: away while `taskbar fullscreen on`
// and a window is fullscreen. A move damages the band where it was and where it goes.
static void compositor_taskbar_slide(positive now)
{
        b64 before = 0;

        if (taskbar_slide_now(compositor_fullscreen_up(), now, compositor_glides(), &before))
                compositor_taskbar_damage(before);
}

/*
        The moon's motion, asked on every pass of the loop before a frame: a step of its turn, or its growth toward the pointer over it and
        the launcher, damages the moon's square on each band as far as it grows, and that alone; the band's other plates are not made
        again for it (taskbar_moon_damaged, which taskbar_meets tells apart from a change under them). At rest, nothing.
*/
static void compositor_moon_keep(positive now)
{
        if (!compositor.band || !compositor.table || !taskbar_moon_tick(now, compositor_launcher_row() >= 0))
                return;
        for (compositor.band_at = 0; compositor.band_at < compositor_bands(); compositor.band_at++)
        {
                struct taskbar_view view = compositor_taskbar_view();
                int x, y, w, h, most, rect[4];

                if (!taskbar_slot(&view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || w <= 0 || h <= 0)
                        continue;
                most = taskbar_moon_most(w < h ? w : h);
                {
                        int square[4] = {x - most, y - most, w + 2 * most, h + 2 * most}, glow[4], shown = taskbar_moon_share() > 0;

                        // While the glow is shown, or was in the last frame, its square is the moon's damage: past the band, and nothing else.
                        if (taskbar_moon_glow_was(compositor.band_at, shown) | shown && taskbar_moon_glow_square(&view, glow))
                                memory_copy(square, glow, sizeof(square));
                        compositor_band_damage(square[0], square[1], square[2], square[3]);
                        rect[0] = square[0] < 0 ? 0 : square[0], rect[1] = square[1] < 0 ? 0 : square[1];
                        rect[2] = square[0] + square[2] > compositor.width ? compositor.width : square[0] + square[2];
                        rect[3] = square[1] + square[3] > compositor.height ? compositor.height : square[1] + square[3];
                }
                taskbar_moon_damaged(compositor.band_at, rect);
        }
        compositor.band_at = 0;
}

/*
        The moon's glow of the band being drawn as a frame of its own (taskbar_moon_glow): its square, premultiplied and blended over what is
        under it, put under the band's plates so the moon is over it. Its pixels are a buffer a band, made as large as the square when it
        is first wanted larger (the card forgets the one before). Answers how many frames: 1, or 0 with the moon at rest.
*/
static struct
{
        unsigned int *pixels;
        positive bytes;
} compositor_glow[WINDOWS_OUTPUTS];

static unsigned int compositor_moon_glow_frame(struct compositor_frame *frame)
{
        struct taskbar_view view = compositor_taskbar_view();
        unsigned int at = (unsigned int)compositor.band_at < WINDOWS_OUTPUTS ? (unsigned int)compositor.band_at : 0;
        int square[4];
        positive bytes;

        if (taskbar_moon_share() <= 0 || !taskbar_moon_glow_square(&view, square))
                return 0;
        bytes = (positive)square[2] * (positive)square[3] * 4;
        if (compositor_glow[at].bytes < bytes)
        {
                long got = (long)compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

                if (got < 0 && got > -4096)
                        return 0;
                if (compositor_glow[at].pixels)
                        gpu_forget(compositor_glow[at].pixels, compositor_glow[at].bytes),
                            compositor_call(syscall(munmap), (positive)compositor_glow[at].pixels, compositor_glow[at].bytes, 0, 0, 0, 0);
                compositor_glow[at].pixels = (unsigned int *)got, compositor_glow[at].bytes = bytes;
        }
        if (!taskbar_moon_glow(&view, compositor_glow[at].pixels, square))
                return 0;
        memory_zero(frame, sizeof(*frame));
        frame->x = square[0], frame->y = square[1], frame->width = square[2], frame->height = square[3], frame->scale = view.scale;
        frame->pixels = compositor_glow[at].pixels, frame->pitch = (unsigned long)square[2], frame->blend = 1, frame->slot = -5, frame->square = 15;
        return 1;
}

/*
        The moon's menu (taskbar_menu) as a frame of its own over everything, while it is up: its picture drawn again only when the menu
        says it changed (a hover, a press's ripple), into a buffer made as large as it when it is first wanted larger. Answers how many.
*/
static struct
{
        unsigned int *pixels;
        positive bytes;
        int band;  // the band whose moon it is from
        int drawn; // the buffer holds the menu's picture
} compositor_menu;

static unsigned int compositor_moon_menu_frame(struct compositor_frame *frame)
{
        struct taskbar_view view;
        int rect[4], was = compositor.band_at;
        positive bytes;

        if (!taskbar_menu_rect(rect))
                return 0;
        bytes = (positive)rect[2] * (positive)rect[3] * 4;
        if (compositor_menu.bytes < bytes)
        {
                long got = (long)compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

                if (got < 0 && got > -4096)
                        return 0;
                if (compositor_menu.pixels)
                        gpu_forget(compositor_menu.pixels, compositor_menu.bytes),
                            compositor_call(syscall(munmap), (positive)compositor_menu.pixels, compositor_menu.bytes, 0, 0, 0, 0);
                compositor_menu.pixels = (unsigned int *)got, compositor_menu.bytes = bytes, compositor_menu.drawn = 0;
        }
        if (taskbar_menu_redraw() || !compositor_menu.drawn)
        {
                compositor.band_at = compositor_menu.band;
                view = compositor_band_view();
                taskbar_menu_draw(&view, compositor_menu.pixels);
                compositor.band_at = was, compositor_menu.drawn = 1;
                if (taskbar_menu_redraw()) // a ripple still going: the next frame draws it on
                        compositor_damage(rect[0], rect[1], rect[2], rect[3]);
        }
        memory_zero(frame, sizeof(*frame));
        frame->x = rect[0], frame->y = rect[1], frame->width = rect[2], frame->height = rect[3];
        frame->scale = compositor.scale, frame->pixels = compositor_menu.pixels, frame->pitch = (unsigned long)rect[2];
        frame->radius = 8 * compositor.scale / 100, frame->line = compositor.scale / 100 > 0 ? compositor.scale / 100 : 1;
        frame->edge = window_mix(compositor_tab_target(0) & 0xffffffu, window_contrast(compositor_tab_target(0)) & 0xffffffu, 48), frame->slot = -5;
        return 1;
}

// After a present: a slide still going, or the highlight of the tab with the keys still moving from one tab to the next, is damaged
// again, so the next frame draws the band where it has got to.
static void compositor_taskbar_keep(positive now)
{
        if (taskbar_sliding(now) || taskbar_moving(now))
                compositor_taskbar_damage(taskbar_shift());
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
#define COMPOSITOR_GLASS_PIVOT 0x808080u // the grey the lift is toward, so a dark backdrop comes up and a bright one comes down (dark glass)
#define COMPOSITOR_GLASS_PIVOT_LIGHT 0xf0f0f0u // light glass lifts toward a near white: it stays light over a light window or wallpaper (a menu over a light window was a mid grey)
#define COMPOSITOR_GLASS_DARKEST 0x5c // dark glass: no colour of a pixel brighter
#define COMPOSITOR_GLASS_LIGHTEST 0xe0 // light glass: none dimmer (0xb4 was a mid grey over the dark desktop: a light theme's menus read too grey)
#define COMPOSITOR_GLASS_UNSIDED 2u
#define COMPOSITOR_GLASS_MARGIN 3 // the blocks kept round a window: what the rise reads past its edge

// The tint of a pixel already made more vivid (compositor_vivid), so a caller holding that value does not boost it again.
// The tint of count boosted pixels into into, for a window of the side given: memory_tint_u32 with the glass's lift
// toward mid grey, and the brightest capped (dark glass) or the dimmest raised (light glass).
static void compositor_glass_tone_span(unsigned int *into, const unsigned int *from, positive count, unsigned int side)
{
        memory_tint_u32(into, from, count, side ? COMPOSITOR_GLASS_PIVOT_LIGHT : COMPOSITOR_GLASS_PIVOT, COMPOSITOR_GLASS_LIFT, side ? 255 : COMPOSITOR_GLASS_DARKEST,
                        side ? COMPOSITOR_GLASS_LIGHTEST : 0);
}

// One pixel stays in C, as compositor_vivid does: the library call is the loss at this length (see there).
static unsigned int compositor_glass_tone(unsigned int boosted, unsigned int side)
{
        unsigned int pixel = window_mix(boosted, side ? COMPOSITOR_GLASS_PIVOT_LIGHT : COMPOSITOR_GLASS_PIVOT, COMPOSITOR_GLASS_LIFT);
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
                        rows_add(&compositor.woken, row);
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
                gpu_forget((const void *)(compositor.backdrop[row]), (positive)(compositor.glass_cut[row])), compositor_call(syscall(munmap), (positive)compositor.backdrop[row], compositor.glass_cut[row], 0, 0, 0, 0);
        for (int other = 0; other < (int)compositor_rows_in_use; other++)
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
                        gpu_forget((const void *)(compositor.backdrop[row]), (positive)(compositor.glass_cut[row])), compositor_call(syscall(munmap), (positive)compositor.backdrop[row], compositor.glass_cut[row], 0, 0, 0, 0);
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

static void compositor_glass_pass(struct compositor_frame *frames, const unsigned int *rows, unsigned int count)
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

#ifdef CONFIG_MOONWATER_WAYLAND
                                compositor_wl_lent_sync(1);
#endif
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
        // A picture with red and blue the other way round (frame->swap) summed them in each other's place.
        if (frame->swap)
        {
                unsigned long red = sum[2];

                sum[2] = sum[0], sum[0] = red;
        }
        frame->close_mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
}

