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
        void *p = *at;

        *at = (unsigned int *)mem_buf_grow(&p, had, bytes, MEM_BUF_GPU_FORGET | MEM_BUF_GROW_2X | MEM_BUF_ALIGN_64K);
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
#define COMPOSITOR_GLASS_BG_NS 33000000ull // 30 Hz coalesce cap for continuous GPU animations behind stationary glass

#ifdef MOONWATER_GPU_C
static const unsigned int *compositor_gpu_frost_scene(const struct compositor_frame *frames, unsigned int count,
                                                      int x0, int y0, int x1, int y1, unsigned long *pitch);
#else
static const unsigned int *compositor_gpu_frost_scene(const struct compositor_frame *frames, unsigned int count,
                                                      int x0, int y0, int x1, int y1, unsigned long *pitch)
{
        (void)frames, (void)count, (void)x0, (void)y0, (void)x1, (void)y1, (void)pitch;
        return null;
}
#endif

static positive compositor_glass_bg_at[WINDOWS];
static positive compositor_glass_bg_next;

static positive compositor_glass_bg_due(void)
{
        return compositor_glass_bg_next;
}

static void compositor_glass_bg_flush(positive now)
{
        if (!compositor_glass_bg_next || now < compositor_glass_bg_next)
                return;
        compositor_glass_bg_next = 0;
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (compositor.glassy[row] && compositor.showing[row] && compositor.stale[row][2] > compositor.stale[row][0])
                {
                        int box[4];

                        compositor_contents_box(row, box);
                        if (box[2] > box[0] && box[3] > box[1])
                                compositor_damage_over(box[0], box[1], 1, 1);
                }
}

static int compositor_frost_has_gpu(const struct compositor_frame *frames, unsigned int count, int x0, int y0, int x1, int y1, int *has_tiled)
{
        int gpu_any = 0, tiled_any = 0;

        for (unsigned int k = 0; k < count; k++)
        {
                const struct compositor_frame *f = &frames[k];

                if (f->x < x1 && x0 < f->x + f->width && f->y < y1 && y0 < f->y + f->height)
                {
                        if (f->tiled_kind)
                                tiled_any = 1, gpu_any = 1;
                        else if (f->gpu_base)
                                gpu_any = 1;
                }
        }
        if (has_tiled)
                *has_tiled = tiled_any;
        return gpu_any;
}

static int compositor_frost(const struct compositor_frame *frames, unsigned int count, int sx, int sy, int sw, int sh,
                            unsigned int *into, unsigned long pitch)
{
        int block = COMPOSITOR_GLASS_BLOCK, spread = COMPOSITOR_GLASS_SPREAD;
        int tw = sw + 2 * spread, th = sh + 2 * spread, ax = (sx - spread) * block, ay = (sy - spread) * block;
        int aw = tw * block, ah = th * block;
        int cx0 = ax < 0 ? 0 : ax, cx1 = ax + aw > compositor.width ? compositor.width : ax + aw;
        int cy0 = ay < 0 ? 0 : ay, cy1 = ay + ah > compositor.height ? compositor.height : ay + ah;
        const unsigned int *gpu_pic = null;
        unsigned long gpu_pitch = 0;
        int has_tiled = 0, has_gpu;
        unsigned int *strip, *small;
        unsigned long bytes;

        if (sw <= 0 || sh <= 0)
                return 0;
        strip = compositor_scratch(&compositor.frost, &compositor.frost_bytes,
                                   ((unsigned long)aw * COMPOSITOR_GLASS_BAND + (unsigned long)tw * th) * 4);
        if (!strip)
                return 0;
        small = strip + (unsigned long)aw * COMPOSITOR_GLASS_BAND;
        if (cx1 <= cx0)
                cx0 = ax + aw <= 0 ? 0 : compositor.width - 1, cx1 = cx0 + 1;
        if (cy1 <= cy0)
                cy0 = ay + ah <= 0 ? 0 : compositor.height - 1, cy1 = cy0 + 1;
        has_gpu = compositor_frost_has_gpu(frames, count, cx0, cy0, cx1, cy1, &has_tiled);
        if (has_gpu)
        {
                gpu_pic = compositor_gpu_frost_scene(frames, count, cx0, cy0, cx1, cy1, &gpu_pitch);
                if (!gpu_pic)
                {
                        if (has_tiled)
                                compositor_wl_straight_pays(-1);
                        compositor_wl_tiled_settle();
                        compositor_wl_lent_sync(1);
                }
        }
        else
                compositor_wl_tiled_settle();
        {
                int ox = cx0 < ax || cx0 >= ax + aw ? cx0 : ax;
                int xfirst = ox == ax ? cx0 - ax : 0, xlast = ox == ax ? cx1 - ax : 1;
                struct compositor_plan frost_plan;
                int *frost_radii = 0;
                int frost_planned = 0;

                if (!gpu_pic)
                {
                        int fy0 = ay > 0 ? ay : 0, fy1 = ay + ah < compositor.height ? ay + ah : compositor.height;
                        if (fy1 <= fy0)
                                fy0 = ay + ah <= 0 ? 0 : compositor.height - 1, fy1 = fy0 + 1;
                        struct compositor_target full_under = {strip, (unsigned long)aw, compositor.width, compositor.height,
                                                               cx0, fy0, cx1, fy1, ox, fy0};
                        frost_radii = compositor_scene_radii;
                        compositor_radii(frames, count, frost_radii);
                        compositor_plan_make(&frost_plan, &full_under, frames, count, frost_radii);
                        frost_planned = 1;
                }
                for (int top = 0; top < ah; top += COMPOSITOR_GLASS_BAND)
                {
                        int rows = ah - top < COMPOSITOR_GLASS_BAND ? ah - top : COMPOSITOR_GLASS_BAND;
                        int y0 = ay + top, vy0 = y0 > 0 ? y0 : 0, vy1 = y0 + rows < compositor.height ? y0 + rows : compositor.height, first, last;
                        struct compositor_target under;

                        if (vy1 <= vy0)
                                vy0 = y0 + rows <= 0 ? 0 : compositor.height - 1, vy1 = vy0 + 1;
                        under = (struct compositor_target){strip, (unsigned long)aw, compositor.width, compositor.height,
                                                           cx0, vy0, cx1, vy1, ox, vy0 < y0 || vy0 >= y0 + rows ? vy0 : y0};
                        first = under.oy == y0 ? vy0 - y0 : 0, last = under.oy == y0 ? vy1 - y0 : 1;
                        if (gpu_pic)
                                memory_copy_rect_u32(strip + (long)first * aw + xfirst, (unsigned long)aw,
                                                     gpu_pic + (unsigned long)vy0 * gpu_pitch + (unsigned long)cx0, gpu_pitch,
                                                     (unsigned long)(cx1 - cx0), (unsigned long)(vy1 - vy0));
                        else if (frost_planned)
                                compositor_scene_plan(&under, frames, count, frost_radii, &frost_plan, 8);
                        else
                                compositor_scene(&under, frames, count);
                        for (int row = first; row < last; row++)
                        {
                                unsigned int *line = strip + (long)row * aw;

                                if (xfirst > 0)
                                        memory_fill_u32(line, (positive)xfirst, line[xfirst]);
                                if (xlast < aw)
                                        memory_fill_u32(line + xlast, (positive)(aw - xlast), line[xlast - 1]);
                        }
                        for (int row = rows; row-- > 0;)
                        {
                                int from = row < first ? first : row >= last ? last - 1 : row;

                                if (from != row)
                                        memory_copy(strip + (long)row * aw, strip + (long)from * aw, (positive)aw * 4);
                        }
                        for (int level = 1; level <= COMPOSITOR_GLASS_LEVELS; level++)
                                memory_halve_u32(strip, (positive)aw, strip, (positive)aw, (positive)(aw >> level), (positive)(rows >> level));
                        for (int row = 0; row < rows / block; row++)
                                memory_copy(small + (long)(top / block + row) * tw, strip + (long)row * aw, (positive)tw * 4);
                }
        }
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
        for (int row = 0; row < sh; row++)
        {
                unsigned int *line = into + (unsigned long)row * pitch;
                const unsigned int *from = small + (long)row * tw;

                if (sw >= 4)
                        memory_invert_u32(line, from, (positive)sw);
                else
                        for (int col = 0; col < sw; col++)
                                line[col] = ~from[col];
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
        unsigned int *top;

        if (x1 <= x0 || y1 <= y0)
                return 0;
        top = compositor_scratch(&compositor.blur, &compositor.blur_bytes, (unsigned long)sw * sh * 4);
        if (!top || !compositor_frost(frames, count, sx, sy, sw, sh, top, (unsigned long)sw))
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

/*
        The band over a window is solid, not glass (compositor_band_solid, a band each): while any window lies under the band or its blur's
        reach (a window maximized or tiled up to it, the common case, or one moved over it), its plates are the desktop's ground tinted as glass is, with
        nothing composed or blurred under them. The blur of the window under the band was made again at every frame the window drew there
        (a browser scrolling under a maximized window's band), and its pixels were the processor's to read; a solid band reads nothing of
        it, and the window under it is free of the band's needs (a plane can show it but for the band's strip). Over the bare desktop the
        band is glass as it always was.
*/
static unsigned char compositor_band_solid[WINDOWS_OUTPUTS];

// Whether a window of the scene meets the band's rectangle or the reach of its blur round it: a window maximized stops at the band's
// edge, and the blur read its bottom rows (from its dma-buf, over the bus) at every frame it drew there.
static int compositor_band_covers(const struct compositor_frame *windows, unsigned int count, const struct compositor_frame *band)
{
        int across = compositor_glass_reach(COMPOSITOR_GLASS_ACROSS), down = compositor_glass_reach(COMPOSITOR_GLASS_DOWN);
        int x1 = band->x - across, y1 = band->y - down, x2 = band->x + band->width + across, y2 = band->y + band->height + down;

        for (unsigned int at = 0; at < count; at++)
                if (windows[at].x < x2 && x1 < windows[at].x + windows[at].width && windows[at].y < y2 && y1 < windows[at].y + windows[at].height)
                        return 1;
        return 0;
}

// The glass over a rectangle of the output: the scene under it composed and blurred into compositor.glass, with the
// blur's reach round it; a solid band's (compositor_band_solid) is the desktop's ground, nothing composed. Answers the row stride, or 0
// when there is no memory for it.
static int compositor_glass_make_for(int band, const struct compositor_frame *windows, unsigned int count, int x, int y,
                                     int width, int height)
{
        int across = compositor_glass_reach(COMPOSITOR_GLASS_ACROSS), down = compositor_glass_reach(COMPOSITOR_GLASS_DOWN);
        int gw = width + 2 * across, gh = height + 2 * down;

        if ((unsigned long)gw * (unsigned long)gh * 4 > compositor.glass_bytes)
                return 0;
        if (compositor_band_solid[(unsigned int)band < WINDOWS_OUTPUTS ? band : 0])
        {
                memory_fill_u32_rect(compositor.glass, (positive)gw, (positive)gw, (positive)gh, compositor_ground | 0xff000000u);
                return gw;
        }
        if (!compositor_blur(compositor.glass, windows, count, x - across, y - down, gw, gh, across, down))
                return 0;
        return gw;
}

// The windows the taskbar's glass is made from, for its callback.
struct compositor_glass_scene
{
        const struct compositor_frame *windows;
        unsigned int count;
        int band;
};

static int compositor_glass_scene_make(void *context, int x, int y, int width, int height)
{
        const struct compositor_glass_scene *scene = (const struct compositor_glass_scene *)context;

        return compositor_glass_make_for(scene->band, scene->windows, scene->count, x, y, width, height);
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
        compositor_damage_over(x, y, width, height);
}

// The damage of the band's move: where it was (before) and where it is now, on every band. A slide damages nothing else.
static void compositor_taskbar_damage(b64 before)
{
        for (int band = 0; band < compositor_bands(); band++)
        {
                struct taskbar_view view = compositor_taskbar_view_for(band);
                int x, y, width, height;

                taskbar_slide_rect(&view, before, &x, &y, &width, &height);
                compositor_band_damage(x, y, width, height);
        }
}

/*
        Every screen's top fullscreen window (one a screen, most of them): the first window of each screen from the top of the stack,
        passing over popups, X override-redirect menus and no-tab overlays. compositor_fullscreen_top returns the first (-1 for none).
*/
static unsigned int compositor_fullscreen_tops(int *rows, unsigned int most)
{
        unsigned int seen = 0, count = 0;

        if (compositor.spaces.ready)
        {
                unsigned int scount = 0;

                compositor_screens(&scount);
                for (unsigned int sc = 0; sc < scount && sc < WINDOWS_OUTPUTS && count < most; sc++)
                {
                        unsigned int sid = compositor.spaces.active[sc];

                        for (unsigned int i = 0; i < compositor.spaces.count; i++)
                        {
                                const struct compositor_space *sp = &compositor.spaces.list[i];

                                if (sp->id == sid)
                                {
                                        if (sp->kind == COMPOSITOR_SPACE_FULLSCREEN && sp->fs_row >= 0 &&
                                            compositor_visible(sp->fs_row))
                                                rows[count++] = sp->fs_row;
                                        break;
                                }
                        }
                }
                return count;
        }
        for (unsigned int at = compositor.stacked; at-- > 0 && count < most;)
        {
                int row = (int)compositor.stack[at], screen;

                if (!compositor_visible(row) || compositor_popup_parent[row] >= 0 || compositor_row_xoverride(row) ||
                    (compositor.asks[row] & WINDOW_NO_TAB))
                        continue;
                screen = compositor_screen_of(row);
                if (screen < 0 || screen >= 32 || (seen & (1u << screen)))
                        continue;
                seen |= 1u << screen;
                if (compositor.fullscreen[row])
                        rows[count++] = row;
        }
        return count;
}

static int compositor_fullscreen_top(void)
{
        int row = -1;

        return compositor_fullscreen_tops(&row, 1) ? row : -1;
}

static int compositor_fullscreen_up(void)
{
        return compositor_fullscreen_top() >= 0;
}

// The taskbar's place, asked on every pass of the loop before a frame is drawn, with the clock: away while `taskbar fullscreen on`
// and a window is fullscreen (unless the Scenes overview is open). A move damages the band where it was and where it goes.
static void compositor_scenes_tick(positive now); // scenes.c
static int compositor_scenes_shown(void);

static void compositor_taskbar_slide(positive now)
{
        b64 before = 0;

        compositor_glass_bg_flush(now);
        compositor_scenes_tick(now);
        if (taskbar_slide_now(compositor_fullscreen_up() && !compositor_scenes_shown(), now, compositor_glides(), &before))
                compositor_taskbar_damage(before);
}

// The apps being started, asked on every pass of the loop before a frame: the band made again when what is shown of them changed, and
// each TASKBAR_STARTING_FRAME while one turns.
static void compositor_starting_keep(positive now)
{
        if (compositor.band && compositor.table && taskbar_starting_keep(now))
                compositor_tabs_changed();
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
        for (int band = 0; band < compositor_bands(); band++)
        {
                struct taskbar_view view = compositor_taskbar_view_for(band);
                int x, y, w, h, most, rect[4];

                if (!taskbar_slot(&view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || w <= 0 || h <= 0)
                        continue;
                most = taskbar_moon_most(w < h ? w : h);
                {
                        int square[4] = {x - most, y - most, w + 2 * most, h + 2 * most}, glow[4], shown = taskbar_moon_glow_level() > 0;

                        // While the glow is shown, or was in the last frame, its square is the moon's damage: past the band, and nothing else.
                        if (taskbar_moon_glow_was(band, shown) | shown && taskbar_moon_glow_square(&view, glow))
                                memory_copy(square, glow, sizeof(square));
                        compositor_band_damage(square[0], square[1], square[2], square[3]);
                        rect[0] = square[0] < 0 ? 0 : square[0], rect[1] = square[1] < 0 ? 0 : square[1];
                        rect[2] = square[0] + square[2] > compositor.width ? compositor.width : square[0] + square[2];
                        rect[3] = square[1] + square[3] > compositor.height ? compositor.height : square[1] + square[3];
                }
                taskbar_moon_damaged(band, rect);
        }
}

/*
        The moon's glow of the band being drawn as a frame of its own (taskbar_moon_glow): its square, premultiplied and blended over what is
        under it, put under the band's plates so the moon is over it. Its pixels are a buffer a band, made as large as the square when it
        is first wanted larger (the card forgets the one before). Answers how many frames: 1, or 0 with the moon at rest.
*/
static struct
{
        unsigned int *pixels;
        unsigned long bytes;
} compositor_glow[WINDOWS_OUTPUTS];

static unsigned int compositor_moon_glow_frame_for(int band, struct compositor_frame *frame)
{
        struct taskbar_view view = compositor_taskbar_view_for(band);
        unsigned int at = (unsigned int)band < WINDOWS_OUTPUTS ? (unsigned int)band : 0;
        int square[4];
        unsigned long bytes;
        void *p;

        if (taskbar_moon_glow_level() <= 0 || !taskbar_moon_glow_square(&view, square))
                return 0;
        bytes = (unsigned long)square[2] * (unsigned long)square[3] * 4;
        p = compositor_glow[at].pixels;
        if (!mem_buf_grow(&p, &compositor_glow[at].bytes, bytes, MEM_BUF_GPU_FORGET))
                return compositor_glow[at].pixels = 0, 0;
        compositor_glow[at].pixels = (unsigned int *)p;
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
        unsigned long bytes;
        int band;  // the band whose moon it is from
        int drawn; // the buffer holds the menu's picture
} compositor_menu;

static unsigned int compositor_moon_menu_frame(struct compositor_frame *frame)
{
        struct taskbar_view view;
        int rect[4];
        unsigned long bytes;

        if (!taskbar_menu_rect(rect))
                return 0;
        bytes = (unsigned long)rect[2] * (unsigned long)rect[3] * 4;
        if (compositor_menu.bytes < bytes)
        {
                void *p = compositor_menu.pixels;

                if (!mem_buf_grow(&p, &compositor_menu.bytes, bytes, MEM_BUF_GPU_FORGET))
                        return compositor_menu.pixels = 0, compositor_menu.drawn = 0, 0;
                compositor_menu.pixels = (unsigned int *)p, compositor_menu.drawn = 0;
        }
        if (taskbar_menu_redraw() || !compositor_menu.drawn)
        {
                view = compositor_band_view_for(compositor_menu.band);
                taskbar_menu_draw(&view, compositor_menu.pixels);
                compositor_menu.drawn = 1;
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
static void compositor_band_draw_for(int band, const struct compositor_frame *windows, unsigned int count, unsigned long redo)
{
        struct taskbar_view view = compositor_band_view_for(band);
        struct compositor_glass_scene scene = {windows, count, band};
        int rect[4];

        if (taskbar_draw(&view, redo, compositor_glass_scene_make, &scene, rect))
                compositor_band_damage(rect[0], rect[1], rect[2], rect[3]);
}

static void compositor_band_draw(const struct compositor_frame *windows, unsigned int count, unsigned long redo)
{
        compositor_band_draw_for(compositor.band_at, windows, count, redo);
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

static unsigned int compositor_glass_tone(unsigned int boosted, unsigned int side)
{
        unsigned int pixel = window_mix(boosted, side ? COMPOSITOR_GLASS_PIVOT_LIGHT : COMPOSITOR_GLASS_PIVOT, COMPOSITOR_GLASS_LIFT);
        unsigned int r = pixel >> 16 & 255, g = pixel >> 8 & 255, b = pixel & 255;
        unsigned int most = r > g ? (r > b ? r : b) : (g > b ? g : b);
        unsigned int least = r < g ? (r < b ? r : b) : (g < b ? g : b);

        if (!side && most > COMPOSITOR_GLASS_DARKEST)
                r = r * COMPOSITOR_GLASS_DARKEST / most, g = g * COMPOSITOR_GLASS_DARKEST / most, b = b * COMPOSITOR_GLASS_DARKEST / most;
        else if (side && least < COMPOSITOR_GLASS_LIGHTEST)
        {
                unsigned int num = 255 - COMPOSITOR_GLASS_LIGHTEST, den = 255 - least;

                r = 255 - (255 - r) * num / den, g = 255 - (255 - g) * num / den, b = 255 - (255 - b) * num / den;
        }
        return r << 16 | g << 8 | b;
}

static unsigned int compositor_glass_tint(unsigned int blurred, unsigned int side)
{
        return compositor_glass_tone(compositor_vivid(blurred), side);
}

#ifdef MOONWATER_GPU_C
/*
        A glass row's ground (compositor_rise's output, bytes long) as the card reads it (C2-glass-op3, step 2): a read map of its pages, and the
        card address of its first byte. Anonymous memory maps as a read userptr (gpu_map), so the scratch needs no memfd. 0 when the card cannot
        have it (no card, or a family whose read map is refused: Intel outside Gen8 and Gen9); the processor then composes the row, as it does now.
        Unused until step 4 calls it: the card's op 3 is gpu_glass.
*/
static positive __attribute__((unused)) compositor_glass_ground_va(const unsigned int *ground, unsigned long bytes)
{
        if (!ground || !bytes || !gpu_map(ground, (positive)bytes))
                return 0;
        return gpu_va_bytes(ground, (positive)bytes);
}
#endif

static int compositor_glass_compose(int row, const int *c)
{
        // A lent buffer is a client's, with the client's pitch; the table's own picture is the compositor's stride (wl-host: no lent row
        // carries WINDOW_GLASS today, so this is the guard, not a fix of a reachable read).
        unsigned long picture_pitch = (unsigned long)compositor_stride();
        const unsigned int *picture = compositor_lent_pixels(row, &picture_pitch);
        int *blocks = compositor.glass_blocks[row], width = c[2] - c[0];

        if (!picture)
                picture = compositor_pixels(row), picture_pitch = (unsigned long)compositor_stride();

        for (int y0 = c[1]; y0 < c[3]; y0 += COMPOSITOR_GLASS_BAND)
        {
                int y1 = y0 + COMPOSITOR_GLASS_BAND < c[3] ? y0 + COMPOSITOR_GLASS_BAND : c[3];
                unsigned long pitch;
                const unsigned int *ground = compositor_rise(compositor.tinted[row], (unsigned long)blocks[2], blocks[0],
                                                             blocks[1], c[0], y0, c[2], y1, &pitch);

                if (!ground)
                        return 0;
                for (int y = y0; y < y1; y++)
                        memory_glass_over_u32(compositor.composed[row] + (unsigned long)(y - compositor.y[row]) * compositor.w[row] + (unsigned long)(c[0] - compositor.x[row]),
                                              picture + (unsigned long)(y - compositor.y[row]) * picture_pitch + (unsigned long)(c[0] - compositor.x[row]),
                                              ground + (unsigned long)(y - y0) * pitch, (positive)width,
                                              compositor_grain_row(compositor.glass_side[row], c[0], y));
        }
        compositor.recomposed += (positive)width * (positive)(c[3] - c[1]);
        return 1;
}

static int compositor_glass_side(int row)
{
        unsigned long *sum = compositor.glass_sum[row], count = (unsigned long)compositor.glass_blocks[row][2] * compositor.glass_blocks[row][3];
        unsigned int was = compositor.glass_side[row], mean, ground, told = compositor.glass_told[row], moved = 0;

        if (!count)
                return 0;
        mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
        compositor.glass_side[row] = compositor.theme_side;
        ground = compositor_glass_tint(mean, compositor.glass_side[row]);
        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                int d = (int)(ground >> shift & 255) - (int)(told >> shift & 255);

                moved += (unsigned int)(d < 0 ? -d : d);
        }
        if (compositor.glass_side[row] != was || moved > 24)
                compositor.glass_told[row] = compositor.table->ground[row] = ground, rows_add(&compositor.woken, row);
        return compositor.glass_side[row] != was;
}

static void compositor_glass_drop(int row)
{
        int left = 0;
        void *p = compositor.backdrop[row];

        mem_buf_free(&p, &compositor.glass_cut[row], MEM_BUF_GPU_FORGET);
        for (int other = 0; other < (int)compositor_rows_in_use; other++)
                left |= other != row && compositor.glassy[other];
        if (!left && compositor.frost)
                compositor_call(syscall(madvise), (positive)compositor.frost, compositor.frost_bytes, 4, 0, 0, 0);
        if (!left && compositor.rise)
                compositor_call(syscall(madvise), (positive)compositor.rise, compositor.rise_bytes, 4, 0, 0, 0);
        compositor.backdrop[row] = compositor.tinted[row] = compositor.boosted[row] = compositor.composed[row] = null;
        compositor.glassy[row] = 0;
        memory_zero(compositor.glass_sum[row], sizeof(compositor.glass_sum[row]));
        memory_zero(compositor.glass_made[row], sizeof(compositor.glass_made[row]));
        memory_zero(compositor.glass_blocks[row], sizeof(compositor.glass_blocks[row]));
        memory_zero(compositor.stale[row], sizeof(compositor.stale[row]));
        memory_zero(compositor.dirty[row], sizeof(compositor.dirty[row]));
}

static int compositor_glass_room(int row)
{
        int shift = COMPOSITOR_GLASS_LEVELS, margin = COMPOSITOR_GLASS_MARGIN;
        int x = compositor.x[row], y = compositor.y[row], w = (int)compositor.w[row], h = (int)compositor.h[row];
        int *blocks = compositor.glass_blocks[row];
        unsigned long pixels = (unsigned long)w * (unsigned long)h, cells, need, most;

        blocks[0] = compositor_floor_shift(x, shift) - margin, blocks[1] = compositor_floor_shift(y, shift) - margin;
        blocks[2] = compositor_ceil_shift(x + w, shift) + margin - blocks[0];
        blocks[3] = compositor_ceil_shift(y + h, shift) + margin - blocks[1];
        cells = (unsigned long)blocks[2] * (unsigned long)blocks[3];
        need = ((cells * 3 + pixels) * 4 + 4095) & ~4095ul;
        most = (((unsigned long)((compositor.width >> shift) + 2 * margin + 2) * (unsigned long)((compositor.height >> shift) + 2 * margin + 2) * 3 +
                 (unsigned long)compositor.width * (unsigned long)compositor.height) * 4 + 4095) & ~4095ul;
        need = need > most ? need : most;
        if (need > compositor.glass_cut[row])
        {
                void *p = compositor.backdrop[row];

                if (!mem_buf_grow(&p, &compositor.glass_cut[row], need, MEM_BUF_GPU_FORGET))
                        return compositor.backdrop[row] = compositor.tinted[row] = compositor.boosted[row] = compositor.composed[row] = null, 0;
                compositor.backdrop[row] = (unsigned int *)p;
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
                int moved = 0, deferred = 0;

                if (!compositor.glassy[row] || frames[at].layer || (!frames[at].builtin && !frames[at].pixels))
                        continue;
                compositor_contents_box(row, box);
                blocks = compositor.glass_blocks[row];
                if (memory_compare(made, place, sizeof(place)))
                {
                        moved = 1;
                        if (!compositor_glass_room(row))
                        {
                                compositor_glass_drop(row);
                                continue;
                        }
                        memory_copy(made, place, sizeof(place));
                        memory_zero(compositor.boosted[row], (positive)blocks[2] * (positive)blocks[3] * 4);
                        memory_zero(compositor.glass_sum[row], sizeof(compositor.glass_sum[row]));
                        stale[0] = blocks[0] * block, stale[1] = blocks[1] * block;
                        stale[2] = (blocks[0] + blocks[2]) * block, stale[3] = (blocks[1] + blocks[3]) * block;
                }
                if (stale[2] > stale[0])
                {
                        int bx0 = compositor_floor_shift(stale[0], shift) - spread, by0 = compositor_floor_shift(stale[1], shift) - spread;
                        int bx1 = compositor_ceil_shift(stale[2], shift) + spread, by1 = compositor_ceil_shift(stale[3], shift) + spread;

                        bx0 = bx0 < blocks[0] ? blocks[0] : bx0, by0 = by0 < blocks[1] ? blocks[1] : by0;
                        bx1 = bx1 > blocks[0] + blocks[2] ? blocks[0] + blocks[2] : bx1;
                        by1 = by1 > blocks[1] + blocks[3] ? blocks[1] + blocks[3] : by1;
                        if (bx1 > bx0 && by1 > by0)
                        {
                                unsigned int *into = compositor.backdrop[row] + (long)(by0 - blocks[1]) * blocks[2] + (bx0 - blocks[0]);
                                int shown[4] = {(bx0 - 1) * block, (by0 - 1) * block, (bx1 + 1) * block, (by1 + 1) * block};
                                positive now = 0;

                                // Coalesce continuous GPU-client animation updates behind stationary glass at 30 Hz;
                                // moving/resizing glass or single changes after idle still update immediately (0 ms).
                                if (!moved && compositor.card >= 0 && row >= 0 && row < WINDOWS &&
                                    compositor_frost_has_gpu(frames, at, (bx0 - spread) * block, (by0 - spread) * block,
                                                             (bx1 + spread) * block, (by1 + spread) * block, null))
                                {
                                        now = clock_monotonic_nanoseconds();
                                        if (compositor_glass_bg_at[row] && now < compositor_glass_bg_at[row] + COMPOSITOR_GLASS_BG_NS)
                                        {
                                                positive due = compositor_glass_bg_at[row] + COMPOSITOR_GLASS_BG_NS;

                                                if (!compositor_glass_bg_next || due < compositor_glass_bg_next)
                                                        compositor_glass_bg_next = due;
                                                deferred = 1;
                                        }
                                }
                                if (!deferred)
                                {
                                        if (row >= 0 && row < WINDOWS)
                                                compositor_glass_bg_at[row] = now ? now : clock_monotonic_nanoseconds();
                                        if (!compositor_frost(frames, at, bx0, by0, bx1 - bx0, by1 - by0, into, (unsigned long)blocks[2]))
                                        {
                                                compositor_glass_drop(row);
                                                continue;
                                        }
                                        for (int by = by0; by < by1; by++)
                                        {
                                                long first = (long)(by - blocks[1]) * blocks[2] + (bx0 - blocks[0]);
                                                unsigned int *now_t = compositor.tinted[row] + first;

                                                memory_saturate_u32(now_t, compositor.backdrop[row] + first, (positive)(bx1 - bx0));
                                                memory_sum_swap_u32((p64 address_to)compositor.glass_sum[row], compositor.boosted[row] + first, now_t, (positive)(bx1 - bx0));
                                                compositor_glass_tone_span(now_t, compositor.boosted[row] + first, (positive)(bx1 - bx0), compositor.glass_side[row]);
                                        }
                                        compositor.blurred += (positive)(bx1 - bx0) * (positive)(by1 - by0);
                                        compositor_meet(shown, box);
                                        compositor_union(compositor.dirty[row], shown);
                                        if (compositor_glass_side(row))
                                        {
                                                compositor_glass_tone_span(compositor.tinted[row], compositor.boosted[row],
                                                                           (positive)blocks[2] * (positive)blocks[3], compositor.glass_side[row]);
                                                memory_copy(compositor.dirty[row], box, sizeof(box));
                                        }
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
                        compositor_damage_from(c[0], c[1], c[2] - c[0], c[3] - c[1], row + 1);
                }
                if (!deferred)
                        memory_zero(compositor.stale[row], sizeof(compositor.stale[row]));
                memory_zero(compositor.dirty[row], sizeof(compositor.dirty[row]));
                frames[at].pixels = compositor.composed[row], frames[at].pitch = (unsigned long)place[2];
        }
}

static int compositor_close_mean_read(struct compositor_frame *frame)
{
        int bx, by, side, touched = 0, step;
        unsigned long sum[3] = {0, 0, 0}, count = 0;
        const unsigned int *picture;

        if (!compositor_close_box(frame, &bx, &by, &side))
                return 0;
        for (unsigned int at = 0; at < compositor.damages && !touched; at++)
        {
                int box[4] = {bx, by, bx + side, by + side};

                compositor_meet(box, compositor.damage[at]);
                touched = box[2] > box[0] && box[3] > box[1];
        }
        if (!touched)
                return 1;
        picture = compositor_scaled(frame) ? 0 : frame->pixels;
        // Stride by 4 when reading write-combined client dma-bufs to cut uncached PCIe reads 16x with identical lightness mean.
        step = frame->gpu_base ? 4 : 1;
        for (int py = by; py < by + side; py += step)
        {
                const unsigned int *row = picture ? picture + (unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(bx - frame->x) : 0;

                for (int px = bx; px < bx + side; px += step)
                {
                        unsigned int under = row ? row[px - bx] : compositor_picture_at(frame, px - frame->x, py - frame->y);

                        sum[0] += under >> 16 & 255, sum[1] += under >> 8 & 255, sum[2] += under & 255;
                        count++;
                }
        }
        if (!count)
                return 1;
        if (frame->swap)
        {
                unsigned long red = sum[2];

                sum[2] = sum[0], sum[0] = red;
        }
        frame->close_mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
        return 1;
}

/*
        The close button's ink side with hysteresis. The glyphs flickered on a backdrop whose mean sat at the light/dark threshold of the plain
        test (window_light): each present decided the side from its own mean, so a mean that crossed the threshold back and forth switched the
        X each time. Each window row keeps its side (1 dark, 2 light). A side changes only when the mean's weighted lightness (window_light's own
        value) is past the threshold by COMPOSITOR_INK_MARGIN percent, so a backdrop at the threshold keeps the side it has. The side is decided by
        the mean made this present; a frame that made no mean keeps its row's side and reads no mean (zero, as before). A row with no close button
        forgets its side.
*/
#define COMPOSITOR_INK_LIGHT 116394750ull // window_light's threshold (window.c): a weighted lightness above it is light paper
#define COMPOSITOR_INK_MARGIN 8           // percent past the threshold before a side changes
static unsigned char compositor_close_side[WINDOWS]; // per row: 0 none, 1 dark, 2 light

static int compositor_ink_side_next(int previous, unsigned int mean, unsigned int margin)
{
        unsigned long long r = mean >> 16 & 255, g = mean >> 8 & 255, b = mean & 255;
        unsigned long long value = 2126ull * r * r + 7152ull * g * g + 722ull * b * b;

        if (!previous)
                return value > COMPOSITOR_INK_LIGHT ? 2 : 1;
        if (previous == 1 && value * 100 > COMPOSITOR_INK_LIGHT * (100 + margin))
                return 2;
        if (previous == 2 && value * 100 < COMPOSITOR_INK_LIGHT * (100 - margin))
                return 1;
        return previous;
}

static void compositor_close_mean(struct compositor_frame *frame)
{
        int row = frame->row_plus1 - 1, has_box;

        frame->close_mean = ~0u; // the sentinel: the read sets the mean only when it makes one this present
        has_box = compositor_close_mean_read(frame);
        if (row >= 0 && row < (int)WINDOWS)
        {
                if (!has_box)
                        compositor_close_side[row] = 0;
                else if (frame->close_mean != ~0u)
                        compositor_close_side[row] = (unsigned char)compositor_ink_side_next(compositor_close_side[row], frame->close_mean, COMPOSITOR_INK_MARGIN);
                frame->close_side = compositor_close_side[row];
        }
        if (frame->close_mean == ~0u)
                frame->close_mean = 0;
}

