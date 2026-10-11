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
        closes and Canvas's restore hook redraws the kernel desktop at once.

        What it draws is the classic look of the kernel's compose path, pixel
        for pixel -- the desktop ink, and around each surface the border, the
        titlebar and the close box -- so the golden scene G-S1 hashes the
        same from either at scale one (test/checks.c). Surfaces are protocol
        version 2 (canvas_abi.h, window.c): a program connects to
        SURFACE_SOCKET, hands over a sealed page, and commits buffers into
        it. A surface has a role (canvas spec 02). An application window
        stacks, the press of a button raises and focuses it, its titlebar
        drags it, its close box asks it to go, keys reach it while it has
        focus and the pointer reaches it when it asks for the pointer, and it
        is kept on the work area, which is the output less the panel's zone.
        Any other role is the compositor's to give: a layer, the edges it is
        held to, a size and a place, and what it may not do (be moved, be
        resized, take the keys). It is given to a program that has a grant,
        which is a root program's word that one process may hold it, and
        that goes when the process does; it is held by one program at a time,
        and a program that may replace the holder asks it to go and takes the
        role when it has, or when its time is up and it has not. The roles
        that have a built-in are always on the screen: the desktop ink, and
        the panel with a button for each window and the tray; when no program
        holds the panel role this draws it, and a program that is killed
        leaves it there in the next frame.

        Damage is the union of what the programs say changed, composed
        straight onto the scanout, each pixel once.

        `surface` is a test client: a window of one colour, a line of text
        drawn with font.c when it is given a TrueType file, and a report on
        standard output of every event it is sent. `panel` is the panel as a
        program: it holds the panel role and is sent the window list.
        `compositor grant ROLE COMMAND` starts a program that holds a role.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_COMPOSITOR
#define MOONWATER_CANVAS_COMPOSITOR

/* ---- backend: the classic look on the CPU ---- */

// The classic palette (canvas spec 03 section 4), today's kernel inks.
#define COMPOSITOR_INK_DESKTOP 0x1b2733u
#define COMPOSITOR_INK_FRAME 0x2f3f52u
#define COMPOSITOR_INK_TITLE 0x2b3a4cu
#define COMPOSITOR_INK_TITLE_LIT 0x4c6785u
#define COMPOSITOR_INK_TEXT 0xdfe7efu
#define COMPOSITOR_INK_PANEL COMPOSITOR_INK_FRAME

// The metrics the kernel draws with, in logical pixels, and the panel's.
#define COMPOSITOR_TITLE 20
#define COMPOSITOR_BORDER 2
#define COMPOSITOR_CELL 8
#define COMPOSITOR_PANEL 24

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch; // pixels a row
        int width, height;
        int x1, y1, x2, y2;  // the damage: nothing outside it is written
};

/*
        One thing to draw. A surface is its buffer inside the frame the role
        gives it (framed or not); the compositor's own panel is a frame with
        no buffer and a mark a window, one byte each: SURFACE_LIST_FOCUSED
        and SURFACE_LIST_MINIMIZED. A panel program draws the same shapes
        into its own buffer, from the window list it is sent.
*/
struct compositor_frame
{
        int x, y;            // the titlebar's top left, as version 1 has it
        int width, height;   // the contents
        int scale;
        int framed, focused, closable;
        const unsigned int *pixels;
        unsigned long pitch;
        int builtin;         // the panel: no pixels, the marks are its buttons
        const unsigned char *marks;
        unsigned int mark_count;
};

// The surface's whole rectangle: border, titlebar, contents, border.
static void compositor_outline(const struct compositor_frame *frame, int *x, int *y,
                               int *width, int *height)
{
        int border = frame->framed ? COMPOSITOR_BORDER * frame->scale : 0;
        int title = frame->framed ? COMPOSITOR_TITLE * frame->scale : 0;

        *x = frame->x - border;
        *y = frame->y - border;
        *width = frame->width + border * 2;
        *height = frame->height + title + border * 3;
}

/*
        The close box, for drawing and for the press alike: one square in the
        titlebar's right end, inset by the border, and none on a window too
        narrow to hold it and two cells of titlebar beside it.
*/
static int compositor_close_box(const struct compositor_frame *frame, int *x, int *y,
                                int *side)
{
        int border = COMPOSITOR_BORDER * frame->scale;
        int box = COMPOSITOR_TITLE * frame->scale - border * 2;

        if (!frame->framed || !frame->closable || box <= 0 ||
            frame->width < box + COMPOSITOR_CELL * frame->scale * 2)
                return 0;

        *side = box;
        *x = frame->x + frame->width - box - border;
        *y = frame->y + border;
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

        memory_fill_u32_rect(t->pixels + (unsigned long)y * t->pitch + (unsigned long)x,
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

        if (x2 <= x1)
                return;

        for (int row = y1; row < y2; row++)
                memory_copy(t->pixels + (unsigned long)row * t->pitch + (unsigned long)x1,
                            source + (unsigned long)(row - y) * pitch + (unsigned long)(x1 - x),
                            (unsigned long)(x2 - x1) * 4);
}

/*
        The close box's X, drawn at any scale from the same two strokes.

        Each stroke is a band along a diagonal of the glyph's square, 3
        logical pixels wide across a row and cut off by the square, so at
        scale one it is the classic X of the kernel's bitmap bit for bit
        (the bitmap, below, is that scale's coverage) and at every other
        scale it is the same shape at the device's size. A pixel's coverage
        is the exact area of the band in it, in 1/256: the share of the unit
        square where the two coordinates differ by less than the half width,
        which is the triangular distribution's integral, in integers, so
        every machine draws the same bytes. Where the strokes cross a pixel
        holds parts of both and the union is counted by 256 samples. The
        mask is made once for a scale and the glyph is blended over the
        titlebar's own ink, which the draw knows, so nothing is read back.
*/
static const unsigned char compositor_close_bits[8] = {
    0xc3, 0xe7, 0x7e, 0x3c, 0x3c, 0x7e, 0xe7, 0xc3,
};

#define COMPOSITOR_CROSS_SCALES 8
static unsigned char compositor_cross_mask[COMPOSITOR_CELL * COMPOSITOR_CROSS_SCALES *
                                           COMPOSITOR_CELL * COMPOSITOR_CROSS_SCALES];
static int compositor_cross_scale;

// How much of the unit square has x - y below z (Q8): the triangle's integral.
static int compositor_tri(int z)
{
        if (z <= -256)
                return 0;
        if (z >= 256)
                return 256;
        return z <= 0 ? ((256 + z) * (256 + z)) >> 9 : 256 - (((256 - z) * (256 - z)) >> 9);
}

static void compositor_cross_make(int scale)
{
        int n = COMPOSITOR_CELL * scale;
        int half = 384 * scale; // 1.5 logical pixels, Q8

        if (scale == compositor_cross_scale)
                return;

        for (int y = 0; y < n; y++)
                for (int x = 0; x < n; x++)
                {
                        int d1 = (x - y) * 256, d2 = (x - (n - 1 - y)) * 256;
                        int a = compositor_tri(half - d1) - compositor_tri(-half - d1);
                        int b = compositor_tri(half - d2) - compositor_tri(-half - d2);
                        int cover = a > b ? a : b;

                        if (scale == 1)
                                cover = compositor_close_bits[y] & (0x80 >> x) ? 256 : 0;
                        else if (a && b && a < 256 && b < 256)
                        {
                                cover = 0;
                                for (int j = 0; j < 16; j++)
                                        for (int i = 0; i < 16; i++)
                                        {
                                                int u = (2 * i + 1) * 8, v = (2 * j + 1) * 8;
                                                int s1 = d1 + u - v, s2 = d2 + u + v - 256;

                                                cover += (s1 <= half && s1 >= -half) ||
                                                         (s2 <= half && s2 >= -half);
                                        }
                        }

                        compositor_cross_mask[y * n + x] = (unsigned char)((cover * 255 + 128) >> 8);
                }

        compositor_cross_scale = scale;
}

static unsigned int compositor_mix(unsigned int ground, unsigned int ink, unsigned int cover)
{
        unsigned int out = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                out |= ((((ground >> shift & 255) * (255 - cover) + (ink >> shift & 255) * cover + 127) / 255)
                        & 255) << shift;

        return out;
}

static void compositor_cross(const struct compositor_target *t, int x, int y, int scale,
                             unsigned int ground, unsigned int colour)
{
        int n = COMPOSITOR_CELL * scale;

        // A glyph the damage does not touch is not made.
        if (scale < 1 || scale > COMPOSITOR_CROSS_SCALES || x >= t->x2 || x + n <= t->x1 ||
            y >= t->y2 || y + n <= t->y1)
                return;

        compositor_cross_make(scale);

        for (int row = y > t->y1 ? 0 : t->y1 - y; row < n && y + row < t->y2; row++)
                for (int column = x > t->x1 ? 0 : t->x1 - x; column < n && x + column < t->x2; column++)
                {
                        unsigned int cover = compositor_cross_mask[row * n + column];

                        if (cover)
                                t->pixels[(unsigned long)(y + row) * t->pitch + (unsigned long)(x + column)] =
                                    cover == 255 ? colour : compositor_mix(ground, colour, cover);
                }
}

/*
        The panel, laid out in one band from the left: the launcher button, a
        button for each window, and the tray well at the right end. The same
        slots are drawn by the compositor's own panel and by a panel program,
        and pressed on in both, so a window's button is where it looks to be.
        Slot -1 is the launcher, -2 the tray, and 0 up the windows; a window
        whose button would be narrower than a pixel has none.
*/
#define COMPOSITOR_HIT_NONE (-3)

static int compositor_panel_slot(const struct compositor_frame *frame, int which, int *x, int *y,
                                 int *width, int *height)
{
        int margin = 2 * frame->scale, unit = frame->height;
        int first = frame->x + margin + 2 * unit + margin * 2;
        int last = frame->x + frame->width - margin - 3 * unit - margin * 2;
        int count = (int)frame->mark_count;
        int each = count ? (last - first - (count - 1) * margin) / count : 0;

        *y = frame->y + margin;
        *height = unit - margin * 2;

        if (which == -1)
        {
                *x = frame->x + margin;
                *width = 2 * unit;
        }
        else if (which == -2)
        {
                *x = frame->x + frame->width - margin - 3 * unit;
                *width = 3 * unit;
        }
        else
        {
                each = each > 8 * unit ? 8 * unit : each;
                if (which < 0 || which >= count || each < 1)
                        return 0;

                *x = first + which * (each + margin);
                *width = each;
        }

        // A band too narrow for a slot has none: nothing is drawn outside the band.
        return *height > 0 && *x >= frame->x && *x + *width <= frame->x + frame->width;
}

// What is at a point of the band: a slot's number, or COMPOSITOR_HIT_NONE.
static int compositor_panel_hit(const struct compositor_frame *frame, int px, int py)
{
        for (int which = -2; which < (int)frame->mark_count; which++)
        {
                int x, y, width, height;

                if (compositor_panel_slot(frame, which, &x, &y, &width, &height) &&
                    px >= x && px < x + width && py >= y && py < y + height)
                        return which;
        }

        return COMPOSITOR_HIT_NONE;
}

/*
        The panel: the band in its ink, the launcher button, the windows'
        buttons (the focused one lit, a put-away one only an outline) and the
        tray well. Every pixel of the band is written once: the margins above
        and below, then along the row of slots the gaps between them and the
        slots themselves, left to right, which is the order they lie in.
*/
static void compositor_panel(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int margin = 2 * frame->scale, line = frame->scale, edge = frame->x;
        int top = frame->y + margin, bottom = frame->y + frame->height - margin;
        int x, y, width, height;

        if (frame->height <= 2 * margin)
        {
                compositor_fill(t, frame->x, frame->y, frame->width, frame->height, COMPOSITOR_INK_PANEL);
                return;
        }

        compositor_fill(t, frame->x, frame->y, frame->width, margin, COMPOSITOR_INK_PANEL);
        compositor_fill(t, frame->x, bottom, frame->width, margin, COMPOSITOR_INK_PANEL);

        for (int which = -1; which <= (int)frame->mark_count; which++)
        {
                // The launcher, the windows, then the tray at the far end.
                int slot = which == (int)frame->mark_count ? -2 : which;
                unsigned int mark = slot >= 0 ? frame->marks[slot] : 0;

                if (!compositor_panel_slot(frame, slot, &x, &y, &width, &height))
                        continue;

                compositor_fill(t, edge, top, x - edge, bottom - top, COMPOSITOR_INK_PANEL);
                edge = x + width;

                if (slot == -1)
                {
                        int mark_side = height / 2;

                        compositor_fill(t, x, y, width, (height - mark_side) / 2, COMPOSITOR_INK_TITLE_LIT);
                        compositor_fill(t, x, y + (height - mark_side) / 2, (width - mark_side) / 2, mark_side,
                                        COMPOSITOR_INK_TITLE_LIT);
                        compositor_fill(t, x + (width - mark_side) / 2, y + (height - mark_side) / 2, mark_side,
                                        mark_side, COMPOSITOR_INK_TEXT);
                        compositor_fill(t, x + (width - mark_side) / 2 + mark_side,
                                        y + (height - mark_side) / 2, width - (width - mark_side) / 2 - mark_side,
                                        mark_side, COMPOSITOR_INK_TITLE_LIT);
                        compositor_fill(t, x, y + (height - mark_side) / 2 + mark_side, width,
                                        height - (height - mark_side) / 2 - mark_side, COMPOSITOR_INK_TITLE_LIT);
                }
                else if (slot == -2)
                        compositor_fill(t, x, y, width, height, COMPOSITOR_INK_DESKTOP);
                else if ((mark & SURFACE_LIST_MINIMIZED) && width > 2 * line && height > 2 * line)
                {
                        // Put away: the button is only its outline.
                        compositor_fill(t, x, y, width, line, COMPOSITOR_INK_TITLE);
                        compositor_fill(t, x, y + line, line, height - 2 * line, COMPOSITOR_INK_TITLE);
                        compositor_fill(t, x + line, y + line, width - 2 * line, height - 2 * line,
                                        COMPOSITOR_INK_PANEL);
                        compositor_fill(t, x + width - line, y + line, line, height - 2 * line,
                                        COMPOSITOR_INK_TITLE);
                        compositor_fill(t, x, y + height - line, width, line, COMPOSITOR_INK_TITLE);
                }
                else
                        compositor_fill(t, x, y, width, height,
                                        mark & SURFACE_LIST_FOCUSED ? COMPOSITOR_INK_TITLE_LIT
                                                                    : COMPOSITOR_INK_TITLE);
        }

        compositor_fill(t, edge, top, frame->x + frame->width - edge, bottom - top, COMPOSITOR_INK_PANEL);
}

static void compositor_draw(const struct compositor_target *t,
                            const struct compositor_frame *frame)
{
        int title = frame->framed ? COMPOSITOR_TITLE * frame->scale : 0;

        if (frame->builtin)
        {
                compositor_panel(t, frame);
                return;
        }

        if (frame->framed)
        {
                int fx, fy, fw, fh, cx, cy, side;
                int bottom = frame->y + title + frame->height;
                int span = title + frame->height;
                unsigned int ground = frame->focused ? COMPOSITOR_INK_TITLE_LIT : COMPOSITOR_INK_TITLE;

                compositor_outline(frame, &fx, &fy, &fw, &fh);
                compositor_fill(t, fx, fy, fw, frame->y - fy, COMPOSITOR_INK_FRAME);
                compositor_fill(t, fx, bottom, fw, fy + fh - bottom, COMPOSITOR_INK_FRAME);
                compositor_fill(t, fx, frame->y, frame->x - fx, span, COMPOSITOR_INK_FRAME);
                compositor_fill(t, frame->x + frame->width, frame->y,
                                fx + fw - (frame->x + frame->width), span, COMPOSITOR_INK_FRAME);
                compositor_fill(t, frame->x, frame->y, frame->width, title, ground);

                if (compositor_close_box(frame, &cx, &cy, &side))
                        compositor_cross(t, cx + (side - COMPOSITOR_CELL * frame->scale) / 2,
                                         cy + (side - COMPOSITOR_CELL * frame->scale) / 2,
                                         frame->scale, ground, COMPOSITOR_INK_TEXT);
        }

        compositor_blit(t, frame->x, frame->y + title, frame->width, frame->height,
                        frame->pixels, frame->pitch);
}

/*
        Where things go, which is the roles' (canvas spec 02 section 2). The
        panel is one band along the bottom or the top of the output, as high
        as its zone; the work area is what is left, and a window is kept
        inside it. A role's layer, lock and anchor are its own: a program
        can ask for others and is told the ones it has.
*/
struct compositor_area
{
        int x1, y1, x2, y2;
};

// The output less the panel's zone, at the scale and the edge in force.
static void compositor_work(int width, int height, int scale, int bottom,
                            struct compositor_area *area)
{
        int zone = COMPOSITOR_PANEL * scale;

        area->x1 = 0;
        area->x2 = width;
        area->y1 = bottom ? 0 : zone;
        area->y2 = bottom ? height - zone : height;
}

// The panel's own rectangle.
static void compositor_band(int width, int height, int scale, int bottom, int *x, int *y,
                            int *band_width, int *band_height)
{
        *x = 0;
        *band_width = width;
        *band_height = COMPOSITOR_PANEL * scale;
        *y = bottom ? height - *band_height : 0;
}

// How a role's surface is held: by anyone, by one program with a grant, not
// yet, or not at all.
#define COMPOSITOR_HELD_ANY 0
#define COMPOSITOR_HELD_ONE 1
#define COMPOSITOR_HELD_NOT_YET 2
#define COMPOSITOR_HELD_NEVER 3

// How a role's size is decided: the program's, the output's, the panel's,
// the work area's.
#define COMPOSITOR_FIT_FREE 0
#define COMPOSITOR_FIT_OUTPUT 1
#define COMPOSITOR_FIT_BAND 2
#define COMPOSITOR_FIT_WORK 3

struct compositor_role
{
        unsigned char layer, lock, framed, fit, held, anchor;
};

#define COMPOSITOR_ANCHOR_ALL \
        (SURFACE_ANCHOR_TOP | SURFACE_ANCHOR_BOTTOM | SURFACE_ANCHOR_LEFT | SURFACE_ANCHOR_RIGHT)
#define COMPOSITOR_LOCK_FIXED (SURFACE_LOCK_NO_FOCUS_CLICK | SURFACE_LOCK_NO_KEYBOARD)
#define COMPOSITOR_LOCK_WINDOW \
        (SURFACE_LOCK_MOVABLE | SURFACE_LOCK_RESIZABLE | SURFACE_LOCK_DECORATED)

static const struct compositor_role compositor_roles[SURFACE_ROLE_LEGACY + 1] = {
    [SURFACE_ROLE_APP] = {SURFACE_LAYER_NORMAL, COMPOSITOR_LOCK_WINDOW, 1, COMPOSITOR_FIT_FREE,
                          COMPOSITOR_HELD_ANY, 0},
    [SURFACE_ROLE_BACKGROUND] = {SURFACE_LAYER_BACKGROUND, COMPOSITOR_LOCK_FIXED, 0,
                                 COMPOSITOR_FIT_OUTPUT, COMPOSITOR_HELD_ONE, COMPOSITOR_ANCHOR_ALL},
    // The anchor of a panel is its two sides; the edge is added where it is made.
    [SURFACE_ROLE_PANEL] = {SURFACE_LAYER_TOP, COMPOSITOR_LOCK_FIXED, 0, COMPOSITOR_FIT_BAND,
                            COMPOSITOR_HELD_ONE, SURFACE_ANCHOR_LEFT | SURFACE_ANCHOR_RIGHT},
    [SURFACE_ROLE_LAUNCHER] = {SURFACE_LAYER_OVERLAY, 0, 0, COMPOSITOR_FIT_FREE, COMPOSITOR_HELD_ONE, 0},
    [SURFACE_ROLE_WINDOW_LIST] = {0, 0, 0, 0, COMPOSITOR_HELD_NOT_YET, 0},
    [SURFACE_ROLE_TRAY] = {0, 0, 0, 0, COMPOSITOR_HELD_NOT_YET, 0},
    [SURFACE_ROLE_ICONS] = {SURFACE_LAYER_BOTTOM, COMPOSITOR_LOCK_FIXED, 0, COMPOSITOR_FIT_WORK,
                            COMPOSITOR_HELD_ONE, COMPOSITOR_ANCHOR_ALL},
    [SURFACE_ROLE_CONTROL] = {SURFACE_LAYER_NORMAL, COMPOSITOR_LOCK_WINDOW, 1, COMPOSITOR_FIT_FREE,
                              COMPOSITOR_HELD_ONE, 0},
    [SURFACE_ROLE_LOG] = {0, 0, 0, 0, COMPOSITOR_HELD_NEVER, 0},
    [SURFACE_ROLE_LEGACY] = {0, 0, 0, 0, COMPOSITOR_HELD_NEVER, 0},
};

#define COMPOSITOR_CLIENTS 32
#define COMPOSITOR_FRAMES (COMPOSITOR_CLIENTS + 1) // and the compositor's own panel

/*
        The scene: inside the target's damage, each surface where nothing
        above it covers it and the desktop where nothing is, every pixel
        written once and none read. The frames are bottom first.

        The damage is cut into bands at the top and bottom edge of every
        surface, so in one band each surface covers the same columns from
        the band's top to its bottom. The columns are given out from the top
        surface down: each takes what it covers of the spans still unclaimed
        and leaves the rest, which are never more than one more than there
        are surfaces. What no surface claims is desktop. The target is
        drawn on through a clip of the cell, so the picture is the one the
        surfaces drawn bottom to top over a desktop would be.
*/
static void compositor_scene(const struct compositor_target *t,
                             const struct compositor_frame *frames, unsigned int count)
{
        int edge[2 * COMPOSITOR_FRAMES + 2];
        int box[COMPOSITOR_FRAMES][4];
        unsigned int edges = 2;

        edge[0] = t->y1;
        edge[1] = t->y2;

        for (unsigned int at = 0; at < count; at++)
        {
                int width, height;

                compositor_outline(&frames[at], &box[at][0], &box[at][1], &width, &height);
                box[at][2] = box[at][0] + width;
                box[at][3] = box[at][1] + height;

                for (unsigned int side = 1; side < 4; side += 2)
                        if (box[at][side] > t->y1 && box[at][side] < t->y2)
                        {
                                unsigned int place = edges++;

                                for (; place && edge[place - 1] > box[at][side]; place--)
                                        edge[place] = edge[place - 1];
                                edge[place] = box[at][side];
                        }
        }

        for (unsigned int band = 0; band + 1 < edges; band++)
        {
                int top = edge[band], bottom = edge[band + 1];
                int span[COMPOSITOR_FRAMES + 1][2];
                unsigned int spans = 1;

                if (bottom <= top)
                        continue;

                span[0][0] = t->x1;
                span[0][1] = t->x2;

                for (unsigned int at = count; at-- && spans;)
                {
                        if (box[at][1] >= bottom || box[at][3] <= top)
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
                                compositor_draw(&cell, &frames[at]);

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
                                        COMPOSITOR_INK_DESKTOP);
        }
}

/* ---- end of the backend ---- */

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

#define COMPOSITOR_EV_KEY 1u
#define COMPOSITOR_EV_REL 2u
#define COMPOSITOR_EV_ABS 3u
#define COMPOSITOR_REL_X 0u
#define COMPOSITOR_REL_Y 1u
#define COMPOSITOR_REL_WHEEL 8u
#define COMPOSITOR_REL_HWHEEL 6u
#define COMPOSITOR_BTN_LEFT 0x110u
#define COMPOSITOR_BTN_TASK 0x117u
#define COMPOSITOR_KEY_A 30u

struct compositor_poll
{
        int handle;
        short events, returned;
};

#define COMPOSITOR_POLLIN 1
#define COMPOSITOR_POLLERR 8
#define COMPOSITOR_POLLHUP 16

#define COMPOSITOR_INPUTS 16
#define COMPOSITOR_DEVICES 64 // evdev numbers the hotplug watch can name
#define COMPOSITOR_GRANTS 16
#define COMPOSITOR_DAMAGE 8
#define COMPOSITOR_BURST 16
// How many surfaces a program may hold that have never shown anything.
#define COMPOSITOR_UNSHOWN 4
// How long a holder that was asked to go has before its role is taken.
#define COMPOSITOR_REPLACE_NS 500000000ull
#define COMPOSITOR_EPERM 1
#define COMPOSITOR_ESRCH 3
#define COMPOSITOR_EAGAIN 11
#define COMPOSITOR_EBUSY 16
#define COMPOSITOR_EINVAL 22
#define COMPOSITOR_ENOSPC 28
#define COMPOSITOR_ETIMEDOUT 110
#define COMPOSITOR_EOPNOTSUPP 95
#define COMPOSITOR_SOL_SOCKET 1
#define COMPOSITOR_SO_PEERCRED 17
#define COMPOSITOR_IN_CREATE 0x100u
#define COMPOSITOR_IN_MOVED_TO 0x80u

#define COMPOSITOR_F_GET_SEALS 1034
#define COMPOSITOR_SEAL_SHRINK 2u
#define COMPOSITOR_SOCK_NONBLOCK 0x800

// A client, and once it has created one, its surface.
struct compositor_client
{
        int handle;          // the connection; -1 for a free slot
        struct surface *page;
        unsigned long bytes;
        unsigned int id;
        unsigned int width, height, slots;
        int x, y;            // where it is, which the page's x, y request
        int asked_x, asked_y; // the request this was last placed from
        unsigned int style;
        unsigned int sequence;
        unsigned int slot;   // the slot on the screen
        unsigned int shown;  // committed once, so there is something to show
        unsigned int committed; // the sequence that put slot there
        unsigned int wake;   // events written since the last wake
        unsigned int arrived; // the count of connections when it came
        unsigned int role, layer, lock;
        unsigned int active; // it holds what its role gives: composed, and heard
        unsigned int pid, uid; // who is on the other end, as the kernel says
        char title[SURFACE_LISTING_TITLE]; // what the window list was last told
};

// A role a program may claim, and the process it was given to. The pidfd is
// the process itself and not its number, so a pid that is reused is not it.
struct compositor_grant
{
        int pidfd;
        unsigned int pid, role, flags;
};

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch; // pitch in bytes
        unsigned int fb, crtc, cursor_handle;
        int width, height;
        int damage[COMPOSITOR_DAMAGE][4]; // x1, y1, x2, y2: what changed, apart
        unsigned int damages;

        int listener, signals;
        int inputs[COMPOSITOR_INPUTS];
        int absolute_x[COMPOSITOR_INPUTS][2], absolute_y[COMPOSITOR_INPUTS][2];
        unsigned int input_count;

        int watch;           // inotify on /dev/input, -1 when there is none
        unsigned long devices; // the evdev numbers that are open
        int device_number[COMPOSITOR_INPUTS];

        int scale, bottom;   // device pixels a drawn one is; the panel's edge
        struct compositor_grant grants[COMPOSITOR_GRANTS];
        // Who holds each role, who is waiting to, and when the holder's time is up.
        struct compositor_client *holder[SURFACE_ROLE_LEGACY + 1];
        struct compositor_client *waiting[SURFACE_ROLE_LEGACY + 1];
        positive deadline[SURFACE_ROLE_LEGACY + 1];
        unsigned int list_changed; // the windows or what shows of them changed
        unsigned int list_generation; // of the listing the panel program was last sent
        unsigned char marks[COMPOSITOR_CLIENTS];
        struct compositor_client *listed[COMPOSITOR_CLIENTS];
        unsigned int listing;

        struct compositor_client clients[COMPOSITOR_CLIENTS];
        struct compositor_client *stack[COMPOSITOR_CLIENTS]; // bottom first, by layer
        unsigned int stacked;
        struct compositor_client *focus, *hover, *grab, *drag;
        int drag_x, drag_y;
        int pointer_x, pointer_y;
        unsigned int modifiers;
        unsigned int next_id, placed, connections;
        unsigned long painted; // pixels composed, all frames
        positive period, presented; // a frame in nanoseconds, and when the last one went out
} compositor;

static long compositor_call(positive number, positive a, positive b, positive c,
                            positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
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

static void compositor_damage(int x, int y, int width, int height)
{
        int rectangle[4] = {x < 0 ? 0 : x, y < 0 ? 0 : y,
                            x + width > compositor.width ? compositor.width : x + width,
                            y + height > compositor.height ? compositor.height : y + height};
        int joined[4];
        unsigned int at = 0;

        if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
                return;

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

// Whether a surface is on the screen: it has shown something, it holds what
// its role gives, and it is not put away.
static int compositor_visible(const struct compositor_client *client)
{
        return client->shown && client->active && !(client->style & WINDOW_MINIMIZED);
}

static int compositor_framed(const struct compositor_client *client)
{
        return compositor_roles[client->role].framed && (client->style & WINDOW_FRAME);
}

static int compositor_title_height(const struct compositor_client *client)
{
        return compositor_framed(client) ? COMPOSITOR_TITLE * compositor.scale : 0;
}

static void compositor_frame_of(const struct compositor_client *client,
                                struct compositor_frame *frame)
{
        *frame = (struct compositor_frame){client->x, client->y, (int)client->width,
                                           (int)client->height, compositor.scale,
                                           compositor_framed(client), client == compositor.focus, 1,
                                           // Not surface_buffer: the page is the program's, and
                                           // its buffer_offset is whatever it last wrote.
                                           surface_slot(client->page, client->slot, client->width,
                                                        client->height),
                                           client->width, 0, null, 0};
}

// The windows a panel lists, in the order they were made, and what shows of each.
static unsigned int compositor_windows(void)
{
        unsigned int count = 0;

        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
        {
                struct compositor_client *client = &compositor.clients[at];
                unsigned int place = count;

                if (client->handle < 0 || !client->page || !client->active || !client->shown ||
                    !compositor_roles[client->role].framed)
                        continue;

                for (; place && compositor.listed[place - 1]->id > client->id; place--)
                        compositor.listed[place] = compositor.listed[place - 1];
                compositor.listed[place] = client;
                count++;
        }

        for (unsigned int at = 0; at < count; at++)
                compositor.marks[at] =
                    (unsigned char)((compositor.listed[at] == compositor.focus ? SURFACE_LIST_FOCUSED : 0) |
                                    (compositor.listed[at]->style & WINDOW_MINIMIZED ? SURFACE_LIST_MINIMIZED : 0));

        return count;
}

// The compositor's panel, for as long as no program holds the panel role.
static int compositor_panel_frame(struct compositor_frame *frame)
{
        int x, y, width, height;

        if (compositor.holder[SURFACE_ROLE_PANEL] && compositor.holder[SURFACE_ROLE_PANEL]->active)
                return 0;

        compositor_band(compositor.width, compositor.height, compositor.scale, compositor.bottom,
                        &x, &y, &width, &height);
        compositor.listing = compositor_windows();
        *frame = (struct compositor_frame){x, y, width, height, compositor.scale, 0, 0, 0, null, 0,
                                           1, compositor.marks, compositor.listing};
        return 1;
}

static void compositor_damage_client(const struct compositor_client *client)
{
        struct compositor_frame frame;
        int x, y, width, height;

        if (!client->shown || !client->active)
                return;

        compositor_frame_of(client, &frame);
        compositor_outline(&frame, &x, &y, &width, &height);
        compositor_damage(x, y, width, height);
}

// What a panel shows changed: the windows, which one has the keys, which are put away.
static void compositor_list_changed(void)
{
        struct compositor_frame panel;

        compositor.list_changed = 1;
        if (compositor_panel_frame(&panel))
                compositor_damage(panel.x, panel.y, panel.width, panel.height);
}

/*
        The damage, composed straight onto the scanout and then announced.
        The scanout is a mapping of the card's memory, so it is only ever
        written: every pixel of the damage once, by the stores of
        memory_fill_u32_rect and memory_copy (full 64-byte lines where a row
        is long enough, a masked store or two overlapping ones at the ends),
        none non-temporal and none a read. The card is then told the
        rectangles that changed, in one call: a virtual card copies nothing
        to its host without that.

        The frames go bottom to top, which is by layer: the panel is in the
        top layer, above every window and below what overlays it, whether a
        program holds the panel role or the compositor draws one.
*/
static void compositor_present(void)
{
        struct compositor_frame frames[COMPOSITOR_FRAMES];
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_dirty dirty = {compositor.fb, 0, 0, compositor.damages, (unsigned long)clip};
        unsigned int count = 0, at = 0;
        struct compositor_frame panel;
        int have_panel;

        if (!compositor.damages)
                return;

        have_panel = compositor_panel_frame(&panel);

        for (; at < compositor.stacked; at++)
        {
                struct compositor_client *client = compositor.stack[at];

                if (have_panel && client->layer > SURFACE_LAYER_TOP)
                {
                        frames[count++] = panel;
                        have_panel = 0;
                }

                if (compositor_visible(client))
                        compositor_frame_of(client, &frames[count++]);
        }

        if (have_panel)
                frames[count++] = panel;

        for (at = 0; at < compositor.damages; at++)
        {
                const int *rectangle = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rectangle[0], rectangle[1], rectangle[2], rectangle[3]};

                compositor_scene(&target, frames, count);

                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)rectangle[side];
                compositor.painted += (unsigned long)compositor_area(rectangle);
        }

        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        compositor.damages = 0;
}

/*
        The card, at the mode Canvas left on it.

        Taking Canvas's own mode is what keeps the switch a plane change and
        not a modeset, and what keeps every coordinate the same on both sides
        of it. A connector with no crtc driving it takes its first mode.
*/
static b32 compositor_card_open(void)
{
        struct compositor_card_res resources = {0};
        unsigned int connectors[16], crtcs[16];
        struct compositor_mode mode;
        unsigned int connector = 0, crtc = 0;
        int have_mode = 0;

        compositor.card = (int)system_open_at(AT_FDCWD, (string_address) "/dev/dri/card0",
                                              FILE_READ_WRITE | O_CLOEXEC);
        if (compositor.card < 0)
                return compositor.card;

        if (system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EBUSY;

        if (resources.connector_count > 16 || resources.crtc_count > 16 || !resources.crtc_count)
                return -COMPOSITOR_EINVAL;

        resources.connectors = (unsigned long)connectors;
        resources.crtcs = (unsigned long)crtcs;
        resources.fb_count = resources.encoder_count = 0;
        if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EINVAL;

        for (unsigned int at = 0; at < resources.connector_count && !have_mode; at++)
        {
                struct compositor_connector probe = {.connector_id = connectors[at]};
                struct compositor_mode modes[64];
                struct compositor_encoder encoder = {0};
                struct compositor_crtc current = {0};

                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0 ||
                    probe.connection != 1 || !probe.mode_count)
                        continue;

                probe.mode_count = probe.mode_count > 64 ? 64 : probe.mode_count;
                probe.modes = (unsigned long)modes;
                probe.prop_count = probe.encoder_count = 0;
                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0)
                        continue;

                connector = connectors[at];
                crtc = crtcs[0];
                mode = modes[0];
                have_mode = 1;

                encoder.encoder_id = probe.encoder_id;
                if (probe.encoder_id &&
                    system_control(compositor.card, COMPOSITOR_GETENCODER, address_of encoder) == 0 &&
                    encoder.crtc_id)
                {
                        current.crtc_id = encoder.crtc_id;
                        crtc = encoder.crtc_id;
                        if (system_control(compositor.card, COMPOSITOR_GETCRTC, address_of current) == 0 &&
                            current.mode_valid)
                                mode = current.mode;
                }
        }

        if (!have_mode)
                return -COMPOSITOR_EINVAL;

        compositor.width = mode.hdisplay;
        compositor.height = mode.vdisplay;
        compositor.crtc = crtc;
        compositor.period = 1000000000ull / (mode.vrefresh >= 24 && mode.vrefresh <= 1000 ? mode.vrefresh : 60);

        struct compositor_dumb dumb = {(unsigned int)compositor.height,
                                       (unsigned int)compositor.width, 32, 0, 0, 0, 0};
        struct compositor_map_dumb map = {0};
        struct compositor_fb fb = {0};

        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                return -COMPOSITOR_ENOSPC;

        fb.width = dumb.width;
        fb.height = dumb.height;
        fb.pitch = dumb.pitch;
        fb.bpp = 32;
        fb.depth = 24;
        fb.handle = dumb.handle;
        map.handle = dumb.handle;
        if (system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) != 0 ||
            system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                return -COMPOSITOR_EINVAL;

        long mapped = compositor_call(syscall(mmap), 0, dumb.size, 3, 1,
                                      (positive)compositor.card, map.offset);

        if (mapped < 0 && mapped > -4096)
                return -COMPOSITOR_ENOSPC;

        compositor.scanout = (unsigned int *)mapped;
        compositor.scanout_bytes = dumb.size;
        compositor.scanout_pitch = dumb.pitch;
        compositor.fb = fb.fb_id;

        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_present();

        struct compositor_crtc set = {(unsigned long)address_of connector, 1, crtc, fb.fb_id,
                                      0, 0, 0, 1, mode};

        if (system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) != 0)
                return -COMPOSITOR_EINVAL;

        return 0;
}

/*
        The pointer on the card's cursor plane: the kernel's arrow, white
        edged in black. A card with no plane has no pointer drawn, and still
        has one that moves and presses.
*/
static const unsigned short compositor_arrow_edge[20] = {
    0x0080, 0x00c0, 0x00a0, 0x0090, 0x0088, 0x0084, 0x0082, 0x0081, 0x8080, 0x4080,
    0xe083, 0x0092, 0x00a9, 0x00c9, 0x8084, 0x8004, 0x8002, 0x8003, 0x0000, 0x0000};
static const unsigned short compositor_arrow_fill[20] = {
    0x0000, 0x0000, 0x0040, 0x0060, 0x0070, 0x0078, 0x007c, 0x007e, 0x007f, 0x807f,
    0x007c, 0x006c, 0x0046, 0x0006, 0x0003, 0x0003, 0x0001, 0x0000, 0x0000, 0x0000};

static void compositor_cursor_start(void)
{
        struct compositor_dumb dumb = {64, 64, 32, 0, 0, 0, 0};
        struct compositor_map_dumb map = {0};
        struct compositor_cursor cursor = {1, compositor.crtc, 0, 0, 64, 64, 0};
        long mapped;

        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                return;

        map.handle = dumb.handle;
        if (system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                return;

        mapped = compositor_call(syscall(mmap), 0, dumb.size, 3, 1,
                                 (positive)compositor.card, map.offset);
        if (mapped < 0 && mapped > -4096)
                return;

        for (unsigned int y = 0; y < 64; y++)
        {
                unsigned int *row = (unsigned int *)((p8 address_to)mapped + (positive)y * dumb.pitch);

                for (unsigned int x = 0; x < 64; x++)
                {
                        unsigned int bit = x < 16 && y < 20
                                               ? (x < 8 ? 0x80u >> x : 0x8000u >> (x - 8))
                                               : 0;
                        unsigned int edge = bit && (compositor_arrow_edge[y] & bit);
                        unsigned int fill = bit && (compositor_arrow_fill[y] & bit);

                        row[x] = edge ? 0xff000000u : fill ? 0xffffffffu : 0;
                }
        }

        compositor_call(syscall(munmap), (positive)mapped, dumb.size, 0, 0, 0, 0);

        cursor.handle = dumb.handle;
        if (system_control(compositor.card, COMPOSITOR_CURSOR, address_of cursor) == 0)
                compositor.cursor_handle = dumb.handle;
}

static void compositor_cursor_move(void)
{
        struct compositor_cursor cursor = {2, compositor.crtc, compositor.pointer_x,
                                           compositor.pointer_y, 0, 0, 0};

        if (compositor.cursor_handle)
                system_control(compositor.card, COMPOSITOR_CURSOR, address_of cursor);
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

        if (!keyboard && !mouse && !tablet)
        {
                system_close(handle);
                return;
        }

        compositor.absolute_x[at][0] = compositor.absolute_x[at][1] = 0;
        compositor.absolute_y[at][0] = compositor.absolute_y[at][1] = 0;

        if (tablet)
        {
                struct compositor_absinfo x = {0}, y = {0};

                system_control(handle, COMPOSITOR_EVIOCGABS(0), address_of x);
                system_control(handle, COMPOSITOR_EVIOCGABS(1), address_of y);
                compositor.absolute_x[at][0] = x.minimum;
                compositor.absolute_x[at][1] = x.maximum;
                compositor.absolute_y[at][0] = y.minimum;
                compositor.absolute_y[at][1] = y.maximum;
        }

        compositor.inputs[at] = (int)handle;
        compositor.device_number[at] = (int)number;
        compositor.devices |= 1ul << number;
        compositor.input_count = at + 1 > compositor.input_count ? at + 1 : compositor.input_count;
}

static void compositor_inputs_open(void)
{
        // IN_CREATE | IN_MOVED_TO on /dev/input, non-blocking and closed on exec.
        compositor.watch = (int)compositor_call(syscall(inotify_init1), SURFACE_SOCK_CLOEXEC | COMPOSITOR_SOCK_NONBLOCK,
                                                0, 0, 0, 0, 0);
        if (compositor.watch >= 0 &&
            compositor_call(syscall(inotify_add_watch), (positive)compositor.watch,
                            (positive) "/dev/input", COMPOSITOR_IN_CREATE | COMPOSITOR_IN_MOVED_TO,
                            0, 0, 0) < 0)
        {
                system_close(compositor.watch);
                compositor.watch = -1;
        }

        for (unsigned int number = 0; number < COMPOSITOR_DEVICES; number++)
                compositor_input_add(number);
}

// What appeared in /dev/input since the last look: a name that is eventN is a device.
static void compositor_inputs_watch(void)
{
        for (;;)
        {
                // struct inotify_event: wd, mask, cookie, len, and len bytes of name.
                unsigned int buffer[64];
                long got = compositor_call(syscall(read), (positive)compositor.watch, (positive)buffer,
                                           sizeof(buffer), 0, 0, 0);

                for (long at = 0; at + 16 <= got;)
                {
                        const char *name = (const char *)(buffer + (at + 16) / 4);
                        unsigned int length = buffer[(at + 12) / 4], number = 0;

                        if (at + 16 + (long)length > got)
                                break;

                        if (length > 5 && name[0] == 'e' && name[1] == 'v' && name[2] == 'e' &&
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
        An event into a surface's ring. One side writes head and the other
        tail, and a full ring keeps what came first; a pointer motion behind
        a motion the program has not read yet replaces it, as version 1 does.
        tail is the program's, so it is read once and only believed as far as
        the ring is long.
*/
static void compositor_post(struct compositor_client *client, unsigned int type,
                            unsigned int flags, unsigned int serial, int a, int b)
{
        struct surface *page = client->page;
        unsigned int head = page->event_head;
        unsigned int used = head - __atomic_load_n(&page->event_tail, __ATOMIC_ACQUIRE);
        struct surface_event *last = &page->event[(head - 1) % SURFACE_EVENTS];

        if (type == SURFACE_EVENT_POINTER_MOTION && used && used <= SURFACE_EVENTS &&
            last->type == SURFACE_EVENT_POINTER_MOTION)
        {
                last->a = a;
                last->b = b;
                client->wake = 1;
                return;
        }

        if (used >= SURFACE_EVENTS)
        {
                page->event_dropped++;
                return;
        }

        struct surface_event *event = &page->event[head % SURFACE_EVENTS];

        event->type = (unsigned short)type;
        event->flags = (unsigned short)flags;
        event->serial = serial;
        event->a = a;
        event->b = b;
        __atomic_store_n(&page->event_head, head + 1, __ATOMIC_RELEASE);
        client->wake = 1;
}

static int compositor_wants_pointer(const struct compositor_client *client)
{
        return client && (__atomic_load_n(&client->page->window.want, __ATOMIC_RELAXED) &
                          WINDOW_WANT_POINTER);
}

// Where the surfaces are in the stack is what their z says, bottom up.
static void compositor_stack_number(void)
{
        for (unsigned int at = 0; at < compositor.stacked; at++)
                compositor.stack[at]->page->window.z = (int)at;
}

// Onto the top of its layer.
static void compositor_stack_add(struct compositor_client *client)
{
        unsigned int at = compositor.stacked++;

        for (; at && compositor.stack[at - 1]->layer > client->layer; at--)
                compositor.stack[at] = compositor.stack[at - 1];

        compositor.stack[at] = client;
        compositor_stack_number();
}

static void compositor_stack_remove(const struct compositor_client *client)
{
        unsigned int at = 0;

        while (at < compositor.stacked && compositor.stack[at] != client)
                at++;

        if (at >= compositor.stacked)
                return;

        for (; at + 1 < compositor.stacked; at++)
                compositor.stack[at] = compositor.stack[at + 1];

        compositor.stacked--;
        compositor_stack_number();
}

static void compositor_focus(struct compositor_client *client)
{
        struct compositor_client *was = compositor.focus;

        // A surface that takes no keys is never given them.
        if (was == client || (client && client->lock & SURFACE_LOCK_NO_KEYBOARD))
                return;

        compositor.focus = client;

        if (was)
        {
                was->page->window.state &= ~WINDOW_FOCUSED;
                compositor_post(was, SURFACE_EVENT_FOCUS, 0, 0, 0, 0);
                compositor_damage_client(was);
        }

        if (client)
        {
                client->page->window.state |= WINDOW_FOCUSED;
                compositor_post(client, SURFACE_EVENT_FOCUS, 0, 0, 1, 0);
                compositor_damage_client(client);
        }

        compositor_list_changed();
}

// Above the rest of its layer, and no further: the panel stays over windows.
static void compositor_raise(struct compositor_client *client)
{
        unsigned int at = 0;

        while (at < compositor.stacked && compositor.stack[at] != client)
                at++;

        if (at >= compositor.stacked ||
            (at + 1 == compositor.stacked || compositor.stack[at + 1]->layer > client->layer))
                return;

        compositor_stack_remove(client);
        compositor_stack_add(client);
        compositor_damage_client(client);
}

/*
        The topmost surface whose rectangle holds the point, frame and all,
        and whether the compositor's own panel is what is there instead: the
        panel is in the top layer, over the windows and under an overlay.
        A surface that lets the pointer through is not there.
*/
static struct compositor_client *compositor_under(int x, int y, int *panel)
{
        int bar_x, bar_y, bar_width, bar_height;
        int bar = !(compositor.holder[SURFACE_ROLE_PANEL] && compositor.holder[SURFACE_ROLE_PANEL]->active);
        unsigned int at = compositor.stacked;

        compositor_band(compositor.width, compositor.height, compositor.scale, compositor.bottom,
                        &bar_x, &bar_y, &bar_width, &bar_height);
        *panel = 0;

        for (;;)
        {
                struct compositor_client *client = at ? compositor.stack[at - 1] : null;
                struct compositor_frame frame;
                int fx, fy, fw, fh;

                if (bar && (!client || client->layer <= SURFACE_LAYER_TOP))
                {
                        bar = 0;
                        if (x >= bar_x && x < bar_x + bar_width && y >= bar_y && y < bar_y + bar_height)
                        {
                                *panel = 1;
                                return null;
                        }
                }

                if (!client)
                        return null;

                at--;
                if (!compositor_visible(client) || client->lock & SURFACE_LOCK_PASS_POINTER)
                        continue;

                compositor_frame_of(client, &frame);
                compositor_outline(&frame, &fx, &fy, &fw, &fh);
                if (x >= fx && x < fx + fw && y >= fy && y < fy + fh)
                        return client;
        }
}

/*
        Where a surface goes. A role that fixes its size fixes its place
        too: the whole output, the panel's band, the work area, beside the
        panel for a menu. Any other is put where it asked, all of it on the
        work area when it fits and its top left when it does not, so a
        person can always reach its titlebar. A position asked for outside
        that is moved in, not refused (canvas spec 02 section 7), and the
        program is told where it is.
*/
static void compositor_place(struct compositor_client *client, int x, int y)
{
        int border = compositor_framed(client) ? COMPOSITOR_BORDER * compositor.scale : 0;
        struct compositor_area work;
        int band_width, band_height;

        compositor_work(compositor.width, compositor.height, compositor.scale, compositor.bottom,
                        &work);

        switch (compositor_roles[client->role].fit)
        {
        case COMPOSITOR_FIT_OUTPUT:
                x = y = 0;
                break;
        case COMPOSITOR_FIT_BAND:
                compositor_band(compositor.width, compositor.height, compositor.scale,
                                compositor.bottom, &x, &y, &band_width, &band_height);
                break;
        case COMPOSITOR_FIT_WORK:
                x = work.x1;
                y = work.y1;
                break;
        default:
        {
                int high_x = work.x2 - (int)client->width - border;
                int high_y = work.y2 - (int)client->height - compositor_title_height(client) - border * 2;

                if (client->role == SURFACE_ROLE_LAUNCHER)
                {
                        // A menu rises from the panel and has no other place.
                        x = work.x1;
                        y = compositor.bottom ? work.y2 - (int)client->height : work.y1;
                        break;
                }

                x = x > high_x ? high_x : x;
                y = y > high_y ? high_y : y;
                x = x < work.x1 + border ? work.x1 + border : x;
                y = y < work.y1 + border ? work.y1 + border : y;
        }
        }

        client->x = x;
        client->y = y;
        client->page->window.x = client->asked_x = client->x;
        client->page->window.y = client->asked_y = client->y;
}

// The topmost surface there is to give the keys to; never one that has shown
// nothing, is put away, or takes none.
static void compositor_refocus(void)
{
        compositor.focus = null;

        for (unsigned int at = compositor.stacked; at--;)
        {
                struct compositor_client *client = compositor.stack[at];

                if (compositor_visible(client) && !(client->lock & SURFACE_LOCK_NO_KEYBOARD))
                {
                        compositor_focus(client);
                        break;
                }
        }
}

/*
        A surface takes the role it asked for: it is placed, stacked, drawn
        and heard from here on. A role that has one holder is told so by an
        event, as a surface that waited for it must be.
*/
static void compositor_activate(struct compositor_client *client)
{
        struct surface *page = client->page;

        client->active = 1;
        if (compositor_roles[client->role].held != COMPOSITOR_HELD_ANY)
        {
                compositor.holder[client->role] = client;
                compositor.waiting[client->role] = null;
                compositor.deadline[client->role] = 0;
        }

        page->role_state = SURFACE_ROLE_ACTIVE;
        page->role_result = 0;
        compositor_place(client, client->x, client->y);
        compositor_stack_add(client);
        compositor_damage_client(client);

        // A new panel has to be told the windows; a window is listed once it has shown something.
        if (client->role == SURFACE_ROLE_PANEL || (compositor_roles[client->role].framed && client->shown))
                compositor_list_changed();

        if (client->role != SURFACE_ROLE_APP)
                compositor_post(client, SURFACE_EVENT_ROLE, 0, 0, SURFACE_ROLE_ACTIVE, 0);
}

/*
        A surface that is no longer what its role says: its time as the
        holder ran out, or the grant that let it was taken back. Nothing is
        killed and nothing is freed; it is not drawn, not heard, and told in
        the page and by an event, and its connection stays until it closes.
*/
static void compositor_detach(struct compositor_client *client, unsigned int state, int result)
{
        struct compositor_client *next = compositor.waiting[client->role];

        if (!client->active)
                return;

        compositor_damage_client(client);
        client->active = 0;
        compositor_stack_remove(client);

        if (compositor.holder[client->role] == client)
                compositor.holder[client->role] = null;
        if (compositor.focus == client)
                compositor_refocus();
        if (compositor.hover == client)
                compositor.hover = null;
        if (compositor.grab == client)
                compositor.grab = null;
        if (compositor.drag == client)
                compositor.drag = null;

        client->page->role_state = state;
        client->page->role_result = result;
        compositor_post(client, SURFACE_EVENT_ROLE, 0, 0, (int)state, result);
        if (compositor_roles[client->role].framed && client->shown)
                compositor_list_changed();

        if (next)
                compositor_activate(next);
}

static void compositor_destroy(struct compositor_client *client)
{
        unsigned int role = client->role;
        struct compositor_client *next;
        int listed = 0;

        if (client->page)
        {
                if (compositor.waiting[role] == client)
                {
                        // The replacement went away before it took the role:
                        // the holder it asked to go is let stay, and told.
                        struct compositor_client *holder = compositor.holder[role];

                        compositor.waiting[role] = null;
                        compositor.deadline[role] = 0;
                        if (holder)
                        {
                                holder->page->window.state &= ~WINDOW_CLOSING;
                                compositor_post(holder, SURFACE_EVENT_ROLE, 0, 0, SURFACE_ROLE_ACTIVE, 0);
                        }
                }

                next = compositor.holder[role] == client ? compositor.waiting[role] : null;
                listed = client->active && client->shown && compositor_roles[role].framed;
                compositor_damage_client(client);
                client->active = 0;
                compositor_stack_remove(client);
                if (compositor.holder[role] == client)
                        compositor.holder[role] = null;

                if (compositor.hover == client)
                        compositor.hover = null;
                if (compositor.grab == client)
                        compositor.grab = null;
                if (compositor.drag == client)
                        compositor.drag = null;
                if (compositor.focus == client)
                        compositor_refocus();

                if (next)
                        compositor_activate(next);
                if (listed)
                        compositor_list_changed();

                compositor_call(syscall(munmap), (positive)client->page, client->bytes, 0, 0, 0, 0);
        }

        system_close(client->handle);
        memory_zero(client, sizeof(*client));
        client->handle = -1;
}

static void compositor_reply(struct compositor_client *client, int status,
                             const struct surface_request *request)
{
        struct surface_message message = {SURFACE_CREATED, status, *request};

        surface_send(client->handle, &message, -1);
}

/*
        What a program has been given: the flags of the grant a live process
        holds for a role, or -1 for none. The grant is the process, which the
        pidfd keeps apart from any other that comes to have its number, and a
        process that has gone has no grant.
*/
static int compositor_granted(unsigned int pid, unsigned int role)
{
        for (unsigned int at = 0; at < COMPOSITOR_GRANTS; at++)
        {
                struct compositor_grant *grant = &compositor.grants[at];
                struct compositor_poll gone = {grant->pidfd, COMPOSITOR_POLLIN, 0};
                long zero[2] = {0, 0};

                if (grant->pidfd <= 0 || grant->pid != pid || grant->role != role)
                        continue;

                if (compositor_call(syscall(ppoll), (positive)&gone, 1, (positive)zero, 0, 0, 0) > 0)
                        continue;

                return (int)grant->flags;
        }

        return -1;
}

static void compositor_grant_drop(struct compositor_grant *grant)
{
        system_close(grant->pidfd);
        memory_zero(grant, sizeof(*grant));
}

/*
        A grant, from a program that is root: the process request.parent may
        claim the role request.role, and with SURFACE_GRANT_REPLACE may take
        it from whoever holds it. SURFACE_GRANT_REVOKE takes it back, and the
        role from the process if it holds it, at once. Anybody else is
        refused, and so is a process that is not there to be given anything.
*/
static int compositor_grant(const struct compositor_client *from, const struct surface_request *request)
{
        struct compositor_grant *free_slot = null, *same = null;
        unsigned int role = request->role, pid = request->parent;
        long watch;

        if (from->uid != 0)
                return -COMPOSITOR_EPERM;

        if (request->size != sizeof(*request) || role > SURFACE_ROLE_LEGACY || !pid ||
            compositor_roles[role].held != COMPOSITOR_HELD_ONE ||
            request->flags & ~(SURFACE_GRANT_REPLACE | SURFACE_GRANT_REVOKE))
                return -COMPOSITOR_EINVAL;

        for (unsigned int at = 0; at < COMPOSITOR_GRANTS; at++)
                if (!compositor.grants[at].pidfd)
                        free_slot = free_slot ? free_slot : &compositor.grants[at];
                else if (compositor.grants[at].pid == pid && compositor.grants[at].role == role)
                        same = &compositor.grants[at];

        if (request->flags & SURFACE_GRANT_REVOKE)
        {
                struct compositor_client *holder = compositor.holder[role];

                if (!same)
                        return -COMPOSITOR_EINVAL;
                compositor_grant_drop(same);
                if (holder && holder->pid == pid)
                        compositor_detach(holder, SURFACE_ROLE_REVOKED, -COMPOSITOR_EPERM);
                return 0;
        }

        if (same)
        {
                same->flags = request->flags;
                return 0;
        }

        if (!free_slot)
                return -COMPOSITOR_ENOSPC;

        watch = compositor_call(syscall(pidfd_open), pid, 0, 0, 0, 0, 0);
        if (watch < 0)
                return -COMPOSITOR_ESRCH;

        *free_slot = (struct compositor_grant){(int)watch, pid, role, request->flags};
        return 0;
}

// The size a role gives its surface, or what it was asked for.
static void compositor_required(unsigned int role, int *width, int *height)
{
        struct compositor_area work;
        int x, y;

        compositor_work(compositor.width, compositor.height, compositor.scale, compositor.bottom,
                        &work);

        switch (compositor_roles[role].fit)
        {
        case COMPOSITOR_FIT_OUTPUT:
                *width = compositor.width;
                *height = compositor.height;
                break;
        case COMPOSITOR_FIT_BAND:
                compositor_band(compositor.width, compositor.height, compositor.scale,
                                compositor.bottom, &x, &y, width, height);
                break;
        case COMPOSITOR_FIT_WORK:
                *width = work.x2 - work.x1;
                *height = work.y2 - work.y1;
                break;
        }
}

/*
        A create: the request checked, then the page, then what the role
        says. An application window is anybody's. Every other role needs a
        grant for the process (canvas spec 02 section 4), and then it is
        taken if it is free, waited for if it is held and the grant may
        replace, and refused if it is held and does not: the surface exists
        and is told EBUSY and to close, which is how a program learns it.
*/
static int compositor_create(struct compositor_client *client, struct surface_request *request,
                             int file)
{
        unsigned long slot, bytes;
        long size, seals, mapped;
        struct surface *page;
        const struct compositor_role *role;
        struct compositor_client *holder;
        int granted = 0, width = 0, height = 0, shown_nothing = 0;

        if (client->page)
                return -COMPOSITOR_EBUSY;

        if (request->size != sizeof(*request) ||
            (request->protocol != 0 && request->protocol != SURFACE_PROTOCOL) ||
            request->flags & ~(SURFACE_REQUEST_FOCUS | SURFACE_REQUEST_TRANSIENT) ||
            (request->buffer_slots != 1 && request->buffer_slots != 2) ||
            request->parent || request->rel_x || request->rel_y ||
            request->surface_id || request->bytes ||
            !request->width || !request->height ||
            request->width > (unsigned int)compositor.width ||
            request->height > (unsigned int)compositor.height)
                return -COMPOSITOR_EINVAL;

        if (request->role > SURFACE_ROLE_LEGACY ||
            compositor_roles[request->role].held == COMPOSITOR_HELD_NEVER)
                return -COMPOSITOR_EINVAL;

        role = &compositor_roles[request->role];

        // An application has no edge and no zone to ask for; the others' are the role's.
        if (request->role == SURFACE_ROLE_APP && (request->anchor || request->exclusive))
                return -COMPOSITOR_EINVAL;

        if (role->held == COMPOSITOR_HELD_NOT_YET)
                return -COMPOSITOR_EOPNOTSUPP;

        if (request->role != SURFACE_ROLE_APP)
        {
                granted = compositor_granted(client->pid, request->role);
                if (granted < 0)
                        return -COMPOSITOR_EPERM;
        }

        if (role->fit != COMPOSITOR_FIT_FREE)
        {
                compositor_required(request->role, &width, &height);
                if (request->width != (unsigned int)width || request->height != (unsigned int)height)
                {
                        request->width = (unsigned int)width;
                        request->height = (unsigned int)height;
                        return SURFACE_ESIZE;
                }
        }

        // A program that makes surfaces and shows nothing on them does not
        // get to hold the rest of the slots.
        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                shown_nothing += compositor.clients[at].page && !compositor.clients[at].shown &&
                                 compositor.clients[at].pid == client->pid;
        if (shown_nothing >= COMPOSITOR_UNSHOWN)
                return -COMPOSITOR_EAGAIN;

        if (file < 0)
                return -COMPOSITOR_EINVAL;

        slot = SURFACE_PAGE_ROUND((unsigned long)request->width * request->height * 4);
        bytes = WINDOW_PIXELS + slot * request->buffer_slots;

        size = compositor_call(syscall(lseek), (positive)file, 0, 2, 0, 0, 0);
        seals = compositor_call(syscall(fcntl), (positive)file, COMPOSITOR_F_GET_SEALS, 0, 0, 0, 0);
        if (size < (long)bytes || seals < 0 || !(seals & COMPOSITOR_SEAL_SHRINK))
                return -COMPOSITOR_EINVAL;

        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 1, (positive)file, 0);
        if (mapped < 0 && mapped > -4096)
                return -COMPOSITOR_ENOSPC;

        page = (struct surface *)mapped;
        client->page = page;
        client->bytes = bytes;
        client->id = ++compositor.next_id;
        client->width = request->width;
        client->height = request->height;
        client->slots = request->buffer_slots;
        client->role = request->role;
        client->layer = role->layer;
        client->lock = role->lock;
        client->style = role->framed ? WINDOW_FRAME : 0;

        // A window is put in a cascade from where the kernel puts a new one.
        if (role->fit == COMPOSITOR_FIT_FREE)
        {
                client->x = client->y = 80 + (int)(compositor.placed % 8) * 32;
                compositor.placed++;
        }

        page->window.width = request->width;
        page->window.height = request->height;
        page->window.style = client->style;
        page->window.pitch = request->width;
        page->window.max_width = request->width;
        page->window.max_height = request->height;
        page->window.display_width = (unsigned int)compositor.width;
        page->window.display_height = (unsigned int)compositor.height;
        page->window.awake = 1;
        page->window.mapping = (unsigned int)bytes;
        page->window.state = 0;

        page->protocol = SURFACE_PROTOCOL;
        page->header_size = SURFACE_HEADER;
        page->role_requested = request->role;
        page->role_state = SURFACE_ROLE_PENDING;
        page->role_result = 0;
        page->layer = role->layer;
        page->anchor_applied = role->anchor | (request->role == SURFACE_ROLE_PANEL
                                                   ? (compositor.bottom ? SURFACE_ANCHOR_BOTTOM
                                                                        : SURFACE_ANCHOR_TOP)
                                                   : 0);
        page->exclusive_applied = request->role == SURFACE_ROLE_PANEL
                                      ? (unsigned int)COMPOSITOR_PANEL * (unsigned int)compositor.scale
                                      : 0;
        page->lock_applied = role->lock;
        page->surface_id = client->id;
        page->applied_width = request->width;
        page->applied_height = request->height;
        page->buffer_slots = request->buffer_slots;
        page->buffer_format = SURFACE_FORMAT_XR24;
        page->buffer_stride = request->width * 4;
        page->buffer_size = request->width * request->height * 4;
        page->buffer_offset[0] = WINDOW_PIXELS;
        page->buffer_offset[1] = (unsigned int)(WINDOW_PIXELS + slot);
        __atomic_store_n(&page->magic, SURFACE_MAGIC, __ATOMIC_RELEASE);

        client->sequence = page->window.sequence;

        request->protocol = SURFACE_PROTOCOL;
        request->surface_id = client->id;
        request->bytes = (unsigned int)bytes;

        holder = role->held == COMPOSITOR_HELD_ANY ? null : compositor.holder[request->role];

        if (!holder || !holder->active)
                compositor_activate(client);
        else if ((granted & SURFACE_GRANT_REPLACE) && !compositor.waiting[request->role])
        {
                // The holder is asked to go and is let finish; this one waits.
                compositor.waiting[request->role] = client;
                compositor.deadline[request->role] = clock_monotonic_nanoseconds() + COMPOSITOR_REPLACE_NS;
                holder->page->window.state |= WINDOW_CLOSING;
                compositor_post(holder, SURFACE_EVENT_CLOSE, 0, 0, SURFACE_CLOSE_REPLACED, 0);
        }
        else
        {
                page->role_state = SURFACE_ROLE_REFUSED;
                page->role_result = -COMPOSITOR_EBUSY;
                compositor_post(client, SURFACE_EVENT_ROLE, 0, 0, SURFACE_ROLE_REFUSED, -COMPOSITOR_EBUSY);
                compositor_post(client, SURFACE_EVENT_CLOSE, 0, 0, SURFACE_CLOSE_REFUSED, 0);
        }

        return 0;
}

// A holder's time is up when it was asked to go and has not: it loses the
// role, and what waited for it has it.
static void compositor_expire(positive now)
{
        for (unsigned int role = 0; role <= SURFACE_ROLE_LEGACY; role++)
                if (compositor.waiting[role] && compositor.holder[role] &&
                    now >= compositor.deadline[role])
                        compositor_detach(compositor.holder[role], SURFACE_ROLE_REVOKED,
                                          -COMPOSITOR_ETIMEDOUT);
}

/*
        A commit: what the page says now, read once into the compositor's own
        copy and clamped there, then damaged. The geometry stays what create
        checked; the program moves, frames or minimizes its window, and names
        the slot to show. What its role does not let it move or resize stays
        where it is, the page is written back with where that is, and the
        program is sent SURFACE_EVENT_REFUSED for each, so it is told and not
        only left to find out.
*/
static void compositor_commit(struct compositor_client *client)
{
        struct surface *page = client->page;
        unsigned int sequence = __atomic_load_n(&page->window.sequence, __ATOMIC_ACQUIRE);
        unsigned int slot = __atomic_load_n(&page->buffer_index, __ATOMIC_RELAXED);
        unsigned int style = __atomic_load_n(&page->window.style, __ATOMIC_RELAXED);
        int x = __atomic_load_n(&page->window.x, __ATOMIC_RELAXED);
        int y = __atomic_load_n(&page->window.y, __ATOMIC_RELAXED);
        unsigned int count = __atomic_load_n(&page->damage_count, __ATOMIC_RELAXED);
        int whole = !client->shown;
        char title[SURFACE_LISTING_TITLE];

        if (sequence == client->sequence && client->shown)
                return;

        client->sequence = sequence;
        slot = slot < client->slots ? slot : 0;

        if (!(client->lock & SURFACE_LOCK_MOVABLE))
        {
                if (x != client->asked_x || y != client->asked_y)
                        compositor_post(client, SURFACE_EVENT_REFUSED, 0, 0, SURFACE_REFUSED_MOVE,
                                        -COMPOSITOR_EPERM);
                x = client->asked_x;
                y = client->asked_y;
                page->window.x = x;
                page->window.y = y;
        }

        if (!(client->lock & SURFACE_LOCK_RESIZABLE) &&
            (page->window.width != client->width || page->window.height != client->height))
        {
                compositor_post(client, SURFACE_EVENT_REFUSED, 0, 0, SURFACE_REFUSED_RESIZE,
                                -COMPOSITOR_EPERM);
                page->window.width = client->width;
                page->window.height = client->height;
        }

        // The title the window list shows: bounded, and ended, whatever the page holds.
        for (unsigned int at = 0; at < sizeof(title); at++)
                title[at] = ((volatile char *)page->window.title)[at];
        title[sizeof(title) - 1] = 0;
        if (memory_compare(title, client->title, sizeof(title)))
        {
                memory_copy(client->title, title, sizeof(title));
                if (compositor_roles[client->role].framed)
                        compositor_list_changed();
        }

        // Only a window has a frame or can be put away; a role that has none keeps it so.
        style &= compositor_roles[client->role].framed ? WINDOW_FRAME | WINDOW_MINIMIZED : 0;
        if (style != client->style)
        {
                compositor_damage_client(client);
                client->style = style;
                whole = 1;
                compositor_list_changed();
        }

        // A move the program asked for since the last one it was given, or
        // a frame it put on or took off, which moves the edges.
        if (x != client->asked_x || y != client->asked_y || whole)
        {
                compositor_damage_client(client);
                compositor_place(client, x, y);
                whole = 1;
        }

        // The slot that was showing is the program's again.
        if (client->shown && slot != client->slot)
        {
                page->release_serial = client->committed;
                compositor_post(client, SURFACE_EVENT_RELEASE, 0, client->committed,
                                (int)client->slot, 0);
                whole = 1;
        }

        client->slot = slot;
        client->committed = sequence;
        if (!client->shown)
        {
                client->shown = 1;
                if (compositor_roles[client->role].framed)
                        compositor_list_changed();
        }

        if (!client->active)
                return;

        if (whole || !count || count > SURFACE_DAMAGE_MAX)
        {
                compositor_damage_client(client);
                return;
        }

        for (unsigned int at = 0; at < count; at++)
        {
                // Clipped to the buffer in a type the program's numbers
                // cannot wrap; an empty or inverted one is no damage.
                struct surface_damage rectangle = page->damage[at];
                long x1 = rectangle.x < 0 ? 0 : rectangle.x;
                long y1 = rectangle.y < 0 ? 0 : rectangle.y;
                long x2 = (long)rectangle.x + rectangle.width;
                long y2 = (long)rectangle.y + rectangle.height;

                x2 = x2 > (long)client->width ? (long)client->width : x2;
                y2 = y2 > (long)client->height ? (long)client->height : y2;
                if (rectangle.width <= 0 || rectangle.height <= 0 || x2 <= x1 || y2 <= y1)
                        continue;

                compositor_damage(client->x + (int)x1,
                                  client->y + compositor_title_height(client) + (int)y1,
                                  (int)(x2 - x1), (int)(y2 - y1));
        }
}

/*
        What a window's button in the panel does: the window comes back if it
        was put away, rises over the others of its layer and has the keys.
        Only a window that shows has one.
*/
static void compositor_reveal(unsigned int id)
{
        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
        {
                struct compositor_client *client = &compositor.clients[at];

                if (client->handle < 0 || !client->page || client->id != id || !client->active ||
                    !client->shown || !compositor_roles[client->role].framed)
                        continue;

                if (client->style & WINDOW_MINIMIZED)
                {
                        client->style &= ~WINDOW_MINIMIZED;
                        client->page->window.style &= ~WINDOW_MINIMIZED;
                        compositor_damage_client(client);
                        compositor_list_changed();
                }

                compositor_raise(client);
                compositor_focus(client);
                return;
        }
}

// What a connection has said, until it would block or has said a burst's worth:
// a program that never stops talking waits its turn behind the rest, and what
// is left is still there for the next poll.
static void compositor_client_read(struct compositor_client *client)
{
        for (unsigned int burst = 0; burst < COMPOSITOR_BURST; burst++)
        {
                struct surface_message message;
                struct surface_iovec part = {&message, sizeof(message)};
                unsigned long control[4] = {0};
                struct surface_msghdr header = {0, 0, &part, 1, control, sizeof(control), 0};
                struct surface_rights *rights = (struct surface_rights *)control;
                int files[4], file_count = 0;
                long got = compositor_call(syscall(recvmsg), (positive)client->handle,
                                           (positive)address_of header,
                                           SURFACE_MSG_DONTWAIT | 0x40000000u, 0, 0, 0);

                if (got == -COMPOSITOR_EAGAIN)
                        return;

                if (header.control_length >= 16 && rights->level == 1 && rights->type == 1 &&
                    rights->length >= 16 && rights->length <= sizeof(control))
                {
                        int *handles = (int *)((p8 address_to)control + 16);

                        file_count = (int)((rights->length - 16) / 4);
                        for (int at = 0; at < file_count; at++)
                                files[at] = handles[at];
                }

                if (got == (long)sizeof(message) && message.kind == SURFACE_CREATE)
                {
                        int status = file_count == 1 ? compositor_create(client, &message.request, files[0])
                                                     : -COMPOSITOR_EINVAL;

                        compositor_reply(client, status, &message.request);
                        // A second create is answered EBUSY and the surface it came on stays.
                        if (status < 0 && status != -COMPOSITOR_EBUSY)
                                got = 0;
                }
                else if (got == (long)sizeof(message) && message.kind == SURFACE_COMMIT &&
                         client->page && !file_count)
                        compositor_commit(client);
                else if (got == (long)sizeof(message) && message.kind == SURFACE_ACTIVATE &&
                         client->page && !file_count)
                {
                        // Only the surface that holds the panel role is heard.
                        if (client == compositor.holder[SURFACE_ROLE_PANEL] && client->active)
                                compositor_reveal(message.request.surface_id);
                }
                else if (got == (long)sizeof(message) && message.kind == SURFACE_GRANT &&
                         !client->page && !file_count)
                {
                        // Answered whatever it was; a refused grant is not a broken connection.
                        message.status = compositor_grant(client, &message.request);
                        message.kind = SURFACE_GRANTED;
                        surface_send(client->handle, &message, -1);
                }
                else
                        got = 0;

                for (int at = 0; at < file_count; at++)
                        system_close(files[at]);

                // Closed, refused, or said something that is not the protocol.
                if (got <= 0)
                {
                        compositor_destroy(client);
                        return;
                }
        }
}

static void compositor_accept(void)
{
        for (;;)
        {
                long handle = compositor_call(syscall(accept4), (positive)compositor.listener, 0, 0,
                                              COMPOSITOR_SOCK_NONBLOCK | SURFACE_SOCK_CLOEXEC, 0, 0);
                struct compositor_client *free_slot = null;

                if (handle < 0)
                        return;

                for (unsigned int at = 0; at < COMPOSITOR_CLIENTS && !free_slot; at++)
                        if (compositor.clients[at].handle < 0)
                                free_slot = &compositor.clients[at];

                // With every slot taken, the connection that has waited longest
                // without showing anything gives its slot up: a program that
                // only connects, or makes surfaces and never commits to one,
                // cannot keep the rest from making theirs.
                if (!free_slot)
                {
                        struct compositor_client *idle = null;

                        for (unsigned int other = 0; other < COMPOSITOR_CLIENTS; other++)
                                if (!compositor.clients[other].shown &&
                                    (!idle || compositor.clients[other].arrived - idle->arrived > 0x80000000u))
                                        idle = &compositor.clients[other];

                        if (idle)
                        {
                                compositor_destroy(idle);
                                free_slot = idle;
                        }
                }

                if (!free_slot)
                {
                        system_close(handle);
                        continue;
                }

                memory_zero(free_slot, sizeof(*free_slot));
                free_slot->handle = (int)handle;
                free_slot->arrived = ++compositor.connections;

                // Who is there, as the kernel says and not as the program does:
                // struct ucred, a pid and then a uid and a gid.
                {
                        unsigned int cred[3] = {0, 0, 0};
                        unsigned int length = sizeof(cred);

                        // Nobody is root until the kernel says so.
                        free_slot->uid = 1;
                        if (compositor_call(syscall(getsockopt), (positive)handle, COMPOSITOR_SOL_SOCKET,
                                            COMPOSITOR_SO_PEERCRED, (positive)cred, (positive)&length, 0) == 0)
                        {
                                free_slot->pid = cred[0];
                                free_slot->uid = cred[1];
                        }
                }
        }
}

// The US layout, enough to name what a test types; anything else is 0.
static const char compositor_plain[58] =
    "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\r\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char compositor_shifted[58] =
    "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\r\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

static unsigned int compositor_modifier(unsigned int code)
{
        switch (code)
        {
        case 42: case 54: return SURFACE_KEY_SHIFT;
        case 29: case 97: return SURFACE_KEY_CONTROL;
        case 56: return SURFACE_KEY_ALT;
        case 100: return SURFACE_KEY_ALTGR;
        case 125: case 126: return SURFACE_KEY_SUPER;
        }
        return 0;
}

static void compositor_key(unsigned int code, int value)
{
        unsigned int modifier = compositor_modifier(code);
        unsigned int character = 0;
        unsigned int flags;

        if (modifier)
        {
                if (value)
                        compositor.modifiers |= modifier;
                else
                        compositor.modifiers &= ~modifier;
        }

        if (code < sizeof(compositor_plain))
                character = (unsigned char)(compositor.modifiers & SURFACE_KEY_SHIFT
                                                ? compositor_shifted[code]
                                                : compositor_plain[code]);

        flags = compositor.modifiers | (value ? SURFACE_KEY_DOWN : 0) | (value == 2 ? SURFACE_KEY_REPEAT : 0);

        if (compositor.focus)
                compositor_post(compositor.focus, SURFACE_EVENT_KEY, flags, 0, (int)code, (int)character);
}

// Where the pointer is in a surface's buffer.
static void compositor_local(const struct compositor_client *client, int *x, int *y)
{
        *x = compositor.pointer_x - client->x;
        *y = compositor.pointer_y - client->y - compositor_title_height(client);
}

// The pointer stays on the output, wherever the motions of a burst took it.
static void compositor_pointer_clamp(void)
{
        compositor.pointer_x = compositor.pointer_x < 0 ? 0
                               : compositor.pointer_x >= compositor.width ? compositor.width - 1
                                                                        : compositor.pointer_x;
        compositor.pointer_y = compositor.pointer_y < 0 ? 0
                               : compositor.pointer_y >= compositor.height ? compositor.height - 1
                                                                         : compositor.pointer_y;
}

static void compositor_motion(void)
{
        struct compositor_client *under;
        int x, y, panel;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &panel);
        compositor_cursor_move();

        if (compositor.drag)
        {
                struct compositor_client *client = compositor.drag;

                // Kept on the work area as it is moved, as when it was placed.
                compositor_damage_client(client);
                compositor_place(client, compositor.pointer_x - compositor.drag_x,
                                 compositor.pointer_y - compositor.drag_y);
                compositor_damage_client(client);
                return;
        }

        if (under != compositor.hover)
        {
                if (compositor_wants_pointer(compositor.hover))
                        compositor_post(compositor.hover, SURFACE_EVENT_POINTER_LEAVE, 0, 0, 0, 0);
                compositor.hover = under;
                if (compositor_wants_pointer(under))
                {
                        compositor_local(under, &x, &y);
                        compositor_post(under, SURFACE_EVENT_POINTER_ENTER, 0, 0, x, y);
                }
        }

        // A held button keeps the pointer with the surface it went down on.
        under = compositor.grab ? compositor.grab : under;
        if (compositor_wants_pointer(under))
        {
                compositor_local(under, &x, &y);
                compositor_post(under, SURFACE_EVENT_POINTER_MOTION, 0, 0, x, y);
        }
}

static void compositor_button(unsigned int code, int down)
{
        struct compositor_client *under;
        int x, y, panel;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &panel);

        if (!down)
        {
                struct compositor_client *grab = compositor.grab;

                compositor.drag = null;
                compositor.grab = null;
                if (compositor_wants_pointer(grab))
                        compositor_post(grab, SURFACE_EVENT_POINTER_BUTTON, 0, 0, (int)code, 0);
                return;
        }

        // The compositor's own panel: a press on a window's button brings the
        // window up, as a program's panel asks for it to be. Nothing else of
        // the panel takes a press, and none of it takes the keys.
        if (panel)
        {
                struct compositor_frame bar;
                int hit;

                if (code == COMPOSITOR_BTN_LEFT && compositor_panel_frame(&bar) &&
                    (hit = compositor_panel_hit(&bar, compositor.pointer_x, compositor.pointer_y)) >= 0)
                        compositor_reveal(compositor.listed[hit]->id);
                return;
        }

        if (!under)
                return;

        if (code == COMPOSITOR_BTN_LEFT && !compositor.grab)
        {
                struct compositor_frame frame;
                int cx, cy, side;

                // A raise stays in the surface's layer; a surface that takes
                // no focus on a press (a panel) does not take it.
                compositor_raise(under);
                if (!(under->lock & SURFACE_LOCK_NO_FOCUS_CLICK))
                        compositor_focus(under);

                compositor_frame_of(under, &frame);
                if (compositor_close_box(&frame, &cx, &cy, &side) &&
                    compositor.pointer_x >= cx && compositor.pointer_x < cx + side &&
                    compositor.pointer_y >= cy && compositor.pointer_y < cy + side)
                {
                        under->page->window.state |= WINDOW_CLOSING;
                        compositor_post(under, SURFACE_EVENT_CLOSE, 0, 0, SURFACE_CLOSE_REQUESTED, 0);
                        return;
                }

                if ((under->lock & SURFACE_LOCK_MOVABLE) &&
                    compositor.pointer_y < under->y + compositor_title_height(under))
                {
                        compositor.drag = under;
                        compositor.drag_x = compositor.pointer_x - under->x;
                        compositor.drag_y = compositor.pointer_y - under->y;
                        return;
                }
        }

        compositor_local(under, &x, &y);
        if (y < 0 || x < 0 || x >= (int)under->width)
                return;

        compositor.grab = compositor.grab ? compositor.grab : under;
        if (compositor_wants_pointer(under))
        {
                // The button event carries its code; the position is the
                // motion's, sent just before it.
                compositor_post(under, SURFACE_EVENT_POINTER_MOTION, 0, 0, x, y);
                compositor_post(under, SURFACE_EVENT_POINTER_BUTTON, SURFACE_KEY_DOWN, 0, (int)code, 0);
        }
}

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
                        compositor.devices &= ~(1ul << compositor.device_number[at]);

                        // A press that was held on it will never be released.
                        compositor_button(COMPOSITOR_BTN_LEFT, 0);
                }

                if (got <= 0)
                        break;

                for (unsigned int i = 0; i < (unsigned int)got / sizeof(events[0]); i++)
                {
                        struct compositor_input_event *event = &events[i];

                        if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_X)
                                compositor.pointer_x += event->value, moved = 1;
                        else if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_Y)
                                compositor.pointer_y += event->value, moved = 1;
                        else if (event->type == COMPOSITOR_EV_REL &&
                                 (event->code == COMPOSITOR_REL_WHEEL || event->code == COMPOSITOR_REL_HWHEEL))
                        {
                                struct compositor_client *under = compositor.grab ? compositor.grab : compositor.hover;

                                if (compositor_wants_pointer(under))
                                        compositor_post(under, SURFACE_EVENT_POINTER_WHEEL, 0, 0,
                                                        event->value * 120,
                                                        event->code == COMPOSITOR_REL_HWHEEL);
                        }
                        else if (event->type == COMPOSITOR_EV_ABS && event->code < 2)
                        {
                                int *range = event->code ? compositor.absolute_y[at] : compositor.absolute_x[at];
                                long span = (long)range[1] - range[0];
                                long size = event->code ? compositor.height : compositor.width;
                                // As Canvas maps a tablet: offset times size over span.
                                long place = span > 0 ? ((long)event->value - range[0]) * size / span : 0;

                                if (event->code)
                                        compositor.pointer_y = (int)place;
                                else
                                        compositor.pointer_x = (int)place;
                                moved = 1;
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code >= COMPOSITOR_BTN_LEFT &&
                                 event->code <= COMPOSITOR_BTN_TASK)
                        {
                                if (moved)
                                        compositor_motion(), moved = 0;
                                compositor_button(event->code, event->value != 0);
                        }
                        else if (event->type == COMPOSITOR_EV_KEY && event->code < 256)
                                compositor_key(event->code, event->value);
                }
        }

        if (moved)
                compositor_motion();
}

static int compositor_listen(void)
{
        struct {
                unsigned short family;
                char path[sizeof(SURFACE_SOCKET)];
        } where = {SURFACE_AF_UNIX, SURFACE_SOCKET};
        long handle;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run", 0755, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);

        // The card is this program's, so a socket left at the path is from a
        // compositor that is no longer running.
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)SURFACE_SOCKET, 0, 0, 0, 0);

        handle = compositor_call(syscall(socket), SURFACE_AF_UNIX,
                                 SURFACE_SOCK_SEQPACKET | SURFACE_SOCK_CLOEXEC | COMPOSITOR_SOCK_NONBLOCK,
                                 0, 0, 0, 0);
        if (handle < 0)
                return (int)handle;

        if (compositor_call(syscall(bind), (positive)handle, (positive)address_of where,
                            sizeof(where), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)handle, 16, 0, 0, 0, 0) < 0)
        {
                system_close(handle);
                return -COMPOSITOR_EINVAL;
        }

        // Any program may open an application window, as any may open
        // /dev/spark for a version 1 one.
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)SURFACE_SOCKET, 0666, 0, 0, 0);
        compositor.listener = (int)handle;
        return 0;
}

// SIGTERM, SIGINT and SIGHUP, as a descriptor the loop polls.
static int compositor_signals(void)
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1);

        compositor_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        return (int)compositor_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8,
                                    COMPOSITOR_SOCK_NONBLOCK | SURFACE_SOCK_CLOEXEC, 0, 0);
}

static void compositor_say(string_address what, bipolar value)
{
        string_format(log, "compositor: %s %b\n", what, value);
        log_flush();
}

/*
        The card back to Canvas, and how long Canvas took to draw again.

        The scanout's mapping holds the card's file open as surely as the
        descriptor does, so both go before the last close -- which is what
        runs Canvas's restore hook. The kernel's own count of composes says
        when the desktop was drawn again; it needs CAP_SYS_ADMIN, which a
        program that could take the card has.
*/
static void compositor_hand_back(void)
{
        struct input_stats before = {0}, after = {0};
        b32 spark = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
        b32 counted = spark >= 0 && system_control(spark, SPARK_IOCTL_INPUT_STATS, address_of before) == 0;
        positive start, now;

        compositor_call(syscall(munmap), (positive)compositor.scanout, compositor.scanout_bytes, 0, 0, 0, 0);
        start = clock_monotonic_nanoseconds();
        system_close(compositor.card);
        now = start;

        while (counted && now - start < 2000000000ull)
        {
                long nap[2] = {0, 200000};

                if (system_control(spark, SPARK_IOCTL_INPUT_STATS, address_of after) != 0)
                        break;
                now = clock_monotonic_nanoseconds();
                if (after.composes != before.composes)
                {
                        compositor_say("the kernel desktop was drawn again, microseconds after the card was let go:",
                                       (bipolar)((now - start) / 1000));
                        break;
                }
                system_call_2(syscall(nanosleep), (positive)nap, 0);
        }

        if (spark >= 0)
                system_close(spark);
}

/*
        What the panel is told: the windows it has a button for, in the page
        past the header, and an event that says they changed. Written only
        for the surface that holds the panel role; the compositor's own
        panel is drawn from the same list and needs no telling.
*/
static void compositor_list_send(void)
{
        struct compositor_client *panel = compositor.holder[SURFACE_ROLE_PANEL];
        struct surface_listing *listing;
        unsigned int count;

        compositor.list_changed = 0;
        if (!panel || !panel->active)
                return;

        count = compositor_windows();
        listing = window_at(panel->page, struct surface_listing, SURFACE_HEADER);

        // Odd while it is being written, and one more when it is whole.
        __atomic_store_n(&listing->generation, ++compositor.list_generation, __ATOMIC_RELEASE);
        listing->count = count;
        for (unsigned int at = 0; at < count; at++)
        {
                listing->entry[at].id = compositor.listed[at]->id;
                listing->entry[at].state = compositor.marks[at];
                memory_copy(listing->entry[at].title, compositor.listed[at]->title,
                            SURFACE_LISTING_TITLE);
        }
        __atomic_store_n(&listing->generation, ++compositor.list_generation, __ATOMIC_RELEASE);
        compositor_post(panel, SURFACE_EVENT_LIST, 0, compositor.list_generation, 0, 0);
}

// The desktop scale Canvas is drawing at, which is what a panel's 24 pixels are multiplied by.
static int compositor_scale_read(void)
{
        struct canvas_control control = {0};
        b32 spark = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
        int scale = 1;

        if (spark < 0)
                return scale;

        if (system_control(spark, SPARK_IOCTL_CANVAS, address_of control) == 0 &&
            control.scale >= 1 && control.scale <= SPARK_CANVAS_SCALE_MOST)
                scale = (int)control.scale;

        system_close(spark);
        return scale;
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

static bipolar surface_number(string_address text, b32 hex);

static b32 compositor_option(string_address argument, char letter)
{
        return argument[0] == '-' && argument[1] == letter && !argument[2];
}

/*
        compositor grant ROLE[:replace] COMMAND [ARGUMENT...]

        What a root script runs to start a program that is to hold a role:
        it asks the compositor to grant this process the role and then
        becomes COMMAND, which is the same process and so holds it. The
        grant is the process and goes with it, which is why there is
        nothing to hand over and no window between the grant and the
        program in which another could take it.
*/
static b32 compositor_grant_main(void)
{
        static const char *const names[] = {"app", "background", "panel", "launcher", "window-list",
                                            "tray", "icons", "control"};
        struct {
                unsigned short family;
                char path[sizeof(SURFACE_SOCKET)];
        } where = {SURFACE_AF_UNIX, SURFACE_SOCKET};
        struct surface_message message = {SURFACE_GRANT, 0, {0}};
        string_address arguments[64], role = program_argument(2);
        unsigned int count = 0, which = 0;
        long handle;

        if (program_argument_count() < 4 || program_argument_count() > 60)
        {
                log_direct(str("usage: compositor grant ROLE[:replace] COMMAND [ARGUMENT...]\n"));
                return 2;
        }

        for (; which < sizeof(names) / sizeof(names[0]); which++)
        {
                positive length = string_length((string_address)names[which]);

                if (!string_compare_max(role, (string_address)names[which], length) &&
                    (!role[length] || (role[length] == ':' && !string_compare(role + length + 1, "replace"))))
                {
                        message.request.flags = role[length] ? SURFACE_GRANT_REPLACE : 0;
                        break;
                }
        }

        if (which == sizeof(names) / sizeof(names[0]))
        {
                log_direct(str("compositor grant: no such role\n"));
                return 2;
        }

        message.request.size = sizeof(message.request);
        message.request.role = which;
        message.request.parent = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);

        handle = compositor_call(syscall(socket), SURFACE_AF_UNIX,
                                 SURFACE_SOCK_SEQPACKET | SURFACE_SOCK_CLOEXEC, 0, 0, 0, 0);
        if (handle < 0 || compositor_call(syscall(connect), (positive)handle, (positive)address_of where,
                                          sizeof(where), 0, 0, 0) < 0 ||
            surface_send(handle, &message, -1) < 0 || surface_receive(handle, &message, 0) < 0 ||
            message.kind != SURFACE_GRANTED || message.status < 0)
        {
                compositor_say("the compositor would not grant it, error", handle < 0 ? (bipolar)handle : message.status);
                return 1;
        }

        for (positive at = 3; at < program_argument_count(); at++)
                arguments[count++] = program_argument(at);
        arguments[count] = null;

        system_execute(arguments[0], arguments, program_environment_list());
        compositor_say("cannot run it, error", -COMPOSITOR_EINVAL);
        return 127;
}

/*
        compositor [-s SCALE] [-t] [-H WIDTHxHEIGHT FILE] [-i FILE]

        Takes the card, or with -H maps FILE as the screen instead and takes
        none. -s is the desktop scale when Canvas is not there to say, -t puts
        the panel on the top edge and not the bottom, and -i reads one more
        input device, a file of evdev records: what a test types and points
        with when there is no hardware to do it.
*/
static b32 compositor_main()
{
        b32 status;
        int running = 1, extra = -1;
        string_address headless = null, file = null;

        claim_standard_descriptors();

        if (program_argument_count() > 1 && program_argument(1)[0] == 'g')
                return compositor_grant_main();

        memory_zero(&compositor, sizeof(compositor));
        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                compositor.clients[at].handle = -1;
        compositor.watch = -1;
        compositor.bottom = 1;
        compositor.scale = 0;

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (compositor_option(option, 't'))
                        compositor.bottom = 0;
                else if (at + 1 < program_argument_count() && compositor_option(option, 's'))
                        compositor.scale = (int)surface_number(program_argument(++at), 0);
                else if (at + 2 < program_argument_count() && compositor_option(option, 'H'))
                {
                        headless = program_argument(at + 1);
                        file = program_argument(at + 2);
                        at += 2;
                }
                else if (at + 1 < program_argument_count() && compositor_option(option, 'i'))
                        extra = (int)system_open_at(AT_FDCWD, program_argument(++at),
                                                    FILE_READ | O_CLOEXEC | O_NONBLOCK);
        }

        if (compositor.scale < 1 || compositor.scale > SPARK_CANVAS_SCALE_MOST)
                compositor.scale = headless ? 1 : compositor_scale_read();

        status = headless ? compositor_headless(headless, file) : compositor_card_open();
        if (status < 0)
        {
                compositor_say("cannot take the display, error", status);
                return 1;
        }

        if (!headless)
                compositor_cursor_start();
        compositor_inputs_open();
        if (extra >= 0)
        {
                compositor.inputs[compositor.input_count] = extra;
                compositor.device_number[compositor.input_count++] = COMPOSITOR_DEVICES - 1;
        }
        compositor.pointer_x = compositor.width / 2;
        compositor.pointer_y = compositor.height / 2;
        compositor_cursor_move();

        compositor.signals = compositor_signals();
        status = compositor_listen();
        if (status < 0 || compositor.signals < 0)
        {
                compositor_say("cannot listen, error", status < 0 ? status : compositor.signals);
                if (!headless)
                        compositor_hand_back();
                return 1;
        }

        string_format(log, "compositor: %b by %b (scale %b), %p input devices, listening on %s\n",
                      (bipolar)compositor.width, (bipolar)compositor.height,
                      (bipolar)compositor.scale, (positive)compositor.input_count, SURFACE_SOCKET);
        log_flush();

        while (running)
        {
                enum { WATCHED = 3 };
                struct compositor_poll wanted[WATCHED + COMPOSITOR_INPUTS + COMPOSITOR_GRANTS +
                                              COMPOSITOR_CLIENTS];
                struct compositor_client *polled[COMPOSITOR_CLIENTS];
                unsigned int count = 0, clients = 0, inputs, grants;
                positive now = 0, until = 0;
                long awaited, wait[2];

                wanted[count++] = (struct compositor_poll){compositor.signals, COMPOSITOR_POLLIN, 0};
                wanted[count++] = (struct compositor_poll){compositor.listener, COMPOSITOR_POLLIN, 0};
                wanted[count++] = (struct compositor_poll){compositor.watch, COMPOSITOR_POLLIN, 0};
                inputs = count;
                for (unsigned int at = 0; at < compositor.input_count; at++)
                        wanted[count++] = (struct compositor_poll){compositor.inputs[at], COMPOSITOR_POLLIN, 0};
                grants = count;
                // A grant is a process: it is over when the process is.
                for (unsigned int at = 0; at < COMPOSITOR_GRANTS; at++)
                        wanted[count++] = (struct compositor_poll){
                            compositor.grants[at].pidfd > 0 ? compositor.grants[at].pidfd : -1,
                            COMPOSITOR_POLLIN, 0};
                for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                        if (compositor.clients[at].handle >= 0)
                        {
                                polled[clients++] = &compositor.clients[at];
                                wanted[count++] = (struct compositor_poll){compositor.clients[at].handle,
                                                                          COMPOSITOR_POLLIN, 0};
                        }

                // With nothing to show or time the wait is for ever. With
                // something to show it is until a frame has gone by since the
                // last one went out, so a program that commits faster than the
                // card can scan costs one composition a frame and not one a
                // commit. A holder that was asked to go has until its time is up.
                for (unsigned int role = 0; role <= SURFACE_ROLE_LEGACY; role++)
                        if (compositor.waiting[role] && compositor.holder[role])
                        {
                                now = clock_monotonic_nanoseconds();
                                until = !until || compositor.deadline[role] < until ? compositor.deadline[role]
                                                                                    : until;
                        }

                if (compositor.damages)
                {
                        positive frame;

                        now = clock_monotonic_nanoseconds();
                        frame = now - compositor.presented < compositor.period
                                    ? compositor.presented + compositor.period
                                    : now;
                        until = !until || frame < until ? frame : until;
                }

                wait[0] = 0;
                wait[1] = until > now ? (long)(until - now) : 0;
                awaited = compositor_call(syscall(ppoll), (positive)wanted, count,
                                          until ? (positive)wait : 0, 0, 0, 0);

                if (awaited < 0)
                        continue;

                if (wanted[0].returned)
                        running = 0;

                if (wanted[2].returned)
                        compositor_inputs_watch();

                for (unsigned int at = 0; at < compositor.input_count; at++)
                        if (wanted[inputs + at].returned)
                                compositor_input_read(at);

                for (unsigned int at = 0; at < COMPOSITOR_GRANTS; at++)
                        if (wanted[grants + at].returned && compositor.grants[at].pidfd > 0)
                                compositor_grant_drop(&compositor.grants[at]);

                for (unsigned int at = 0; at < clients; at++)
                        if (wanted[grants + COMPOSITOR_GRANTS + at].returned && polled[at]->handle >= 0)
                                compositor_client_read(polled[at]);

                if (wanted[1].returned)
                        compositor_accept();

                if (until)
                        compositor_expire(clock_monotonic_nanoseconds());

                if (compositor.list_changed)
                        compositor_list_send();

                // Dated when it starts: a card that holds the call for the rest
                // of the frame has used the frame up.
                if (compositor.damages)
                {
                        positive stamp = clock_monotonic_nanoseconds();

                        if (stamp - compositor.presented >= compositor.period)
                        {
                                compositor.presented = stamp;
                                compositor_present();
                        }
                }

                // One wake a program per pass, however many events it got.
                for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                {
                        struct compositor_client *client = &compositor.clients[at];
                        struct surface_message wake = {SURFACE_WAKE, 0, {0}};

                        if (client->handle < 0 || !client->wake)
                                continue;

                        client->wake = 0;
                        surface_send(client->handle, &wake, -1);
                }
        }

        for (unsigned int at = 0; at < COMPOSITOR_CLIENTS; at++)
                if (compositor.clients[at].handle >= 0)
                        compositor_destroy(&compositor.clients[at]);

        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)SURFACE_SOCKET, 0, 0, 0, 0);
        string_format(log, "compositor: %p pixels composed\n", (positive)compositor.painted);
        log_flush();
        if (!headless)
                compositor_hand_back();
        return 0;
}

// surface ----------------------------------------------------------
/*
        A test client: surface [-x X] [-y Y] [-w WIDTH] [-h HEIGHT]
        [-c RRGGBB] [-r ROLE] [-p] [-n] [-m] [-2] [-F COMMITS] [-W WRITERS]
        [-O OFFSET] [-z WIDTH] [-T TITLE] [-N COUNT] [-X COUNT] [-f FONT.ttf] [-t TEXT] [-s SECONDS].

        A window of one colour at X, Y, a line of TEXT drawn with font.c on
        it when FONT is given, pointer events asked for with -p, and every
        event it is sent written to standard output, one line each. It lives
        until its close box is pressed, the compositor goes, or SECONDS. -n
        never commits, and -F commits COMMITS times in a row, whole window
        each, as fast as it can, from WRITERS processes sharing the
        connection when -W is given. -m puts the window away as it opens,
        -z asks for another width than it was made with, -T names the
        window, and a role that fixes its size is asked again for the size
        it answers with. -N makes COUNT surfaces and shows nothing on any
        of them, and says how many it was given. -X draws its line COUNT
        more times and says how much address space that took. -O writes OFFSET over its first buffer's
        offset in the page and commits, and -2 sends a second create on
        its connection and reports the answer: what a program that means
        harm does with the page and the socket. -X draws the line COUNT more
        times and says how much address space that took, which is none.
*/
static bipolar surface_number(string_address text, b32 hex)
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

static positive surface_map_file(string_address path, positive address_to size)
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

// The pages mapped, from /proc/self/statm.
static long surface_pages(void)
{
        char text[64] = {0};
        bipolar handle = system_open_at(AT_FDCWD, "/proc/self/statm", FILE_READ | O_CLOEXEC);
        long pages = 0;

        if (handle < 0)
                return -1;
        system_read_once(handle, text, sizeof(text) - 1);
        system_close(handle);
        for (unsigned int at = 0; text[at] >= '0' && text[at] <= '9'; at++)
                pages = pages * 10 + (text[at] - '0');
        return pages;
}

/*
        One engine for the life of the program, mapped when the first line is
        drawn and let go of at the end: it is 766 KB of address space of which
        a start touches 4 KB and a line of text a few tens, so what a draw
        costs is the pages it writes, and a second draw costs none. The font
        is read in place from its file for the length of the call.
*/
static long surface_engine;

static void surface_text_free(void)
{
        if (surface_engine)
                compositor_call(syscall(munmap), (positive)surface_engine, sizeof(font_engine), 0, 0, 0, 0);
        surface_engine = 0;
}

static void surface_text(struct surface *surface, unsigned int slot, string_address path,
                         string_address text, unsigned int colour)
{
        positive size = 0;
        positive bytes = surface_map_file(path, address_of size);
        font_face face;
        const font_face address_to faces[1] = {&face};
        static p8 atlas[256 * 256];
        font_target target = {surface_buffer(surface, slot), surface->window.width,
                              (b32)surface->window.width, (b32)surface->window.height};
        p32 em = 16 << 6;
        b32 got;

        if (!surface_engine)
        {
                long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);

                if (!(engine < 0 && engine > -4096))
                        surface_engine = engine;
        }

        if (!bytes || !surface_engine || font_load(&face, (const p8 address_to)bytes, size, 0) != 0)
        {
                log_direct(str("surface: no font\n"));
                if (bytes)
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
                return;
        }

        font_start((font_engine address_to)surface_engine, atlas, 256, 256);
        got = font_draw((font_engine address_to)surface_engine, faces, 1, (const p8 address_to)text,
                        string_length(text), em, 0, &target, 8 << 16,
                        8 + font_metrics_at(&face, em).baseline,
                        0xffffff, colour);
        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        string_format(log, "surface: text drawn %b px wide\n", (bipolar)(got >> 16));
        log_flush();
}

static b32 surface_main()
{
        bipolar x = -1, y = -1, width = 320, height = 200, colour = 0x3366aa, role = SURFACE_ROLE_APP;
        bipolar seconds = -1;
        b32 pointer = 0, never = 0, twice = 0, away = 0;
        bipolar flood = 0, offset = -1, writers = 1, wider = -1, many = 0, again = 0;
        string_address title = null;
        b32 child = 0;
        string_address font = null, text = (string_address) "Moonwater";
        struct surface *surface;
        positive until;

        claim_standard_descriptors();

        for (positive at = 1; at + 1 <= program_argument_count(); at++)
        {
                string_address option = program_argument(at);
                string_address value = at + 1 < program_argument_count() ? program_argument(at + 1) : null;

                if (option[0] != '-' || !option[1] || option[2])
                        continue;
                if (option[1] == 'p' || option[1] == 'n' || option[1] == '2' || option[1] == 'm')
                {
                        pointer |= option[1] == 'p';
                        never |= option[1] == 'n';
                        twice |= option[1] == '2';
                        away |= option[1] == 'm';
                        continue;
                }
                if (!value)
                        break;
                at++;
                switch (option[1])
                {
                case 'x': x = surface_number(value, 0); break;
                case 'y': y = surface_number(value, 0); break;
                case 'w': width = surface_number(value, 0); break;
                case 'h': height = surface_number(value, 0); break;
                case 'c': colour = surface_number(value, 1); break;
                case 'r': role = surface_number(value, 0); break;
                case 's': seconds = surface_number(value, 0); break;
                case 'F': flood = surface_number(value, 0); break;
                case 'O': offset = surface_number(value, 0); break;
                case 'W': writers = surface_number(value, 0); break;
                case 'z': wider = surface_number(value, 0); break;
                case 'T': title = value; break;
                case 'N': many = surface_number(value, 0); break;
                case 'X': again = surface_number(value, 0); break;
                case 'f': font = value; break;
                case 't': text = value; break;
                }
        }

        // Many surfaces, none committed: how many a program is given.
        if (many > 0)
        {
                struct surface *made[COMPOSITOR_CLIENTS];
                bipolar got = 0;

                for (; got < many && got < COMPOSITOR_CLIENTS; got++)
                        if (!(made[got] = surface_open((unsigned int)width, (unsigned int)height,
                                                       (unsigned int)role)))
                                break;

                string_format(log, "surface: made %b\n", got);
                log_flush();
                if (got)
                        surface_wait(made[0], -1, seconds >= 0 ? seconds * 1000000000 : -1);
                return 0;
        }

        surface = width > 0 && height > 0 && role >= 0
                      ? surface_open((unsigned int)width, (unsigned int)height, (unsigned int)role)
                      : null;

        // A role that fixes the size says what it is.
        if (!surface && surface_refused == SURFACE_ESIZE)
        {
                width = surface_wanted_width;
                height = surface_wanted_height;
                surface = surface_open((unsigned int)width, (unsigned int)height, (unsigned int)role);
        }

        if (!surface)
        {
                string_format(log, "surface: refused %b\n", (bipolar)surface_refused);
                log_flush();
                return 1;
        }

        memory_fill_u32(surface_buffer(surface, 0), (positive)(width * height), (p32)colour & 0xffffff);

        if (font)
                surface_text(surface, 0, font, text, (unsigned int)colour & 0xffffff);

        // The size of the address space before and after COUNT more draws, in pages.
        if (font && again > 0)
        {
                long before = surface_pages(), size;

                for (bipolar draw = 0; draw < again; draw++)
                        surface_text(surface, 0, font, text, (unsigned int)colour & 0xffffff);
                size = surface_pages();
                string_format(log, "surface: %b draws changed the mapped pages by %b\n", again,
                              (bipolar)(size - before));
                log_flush();
        }

        if (x >= 0)
                surface->window.x = (int)x;
        if (y >= 0)
                surface->window.y = (int)y;
        if (pointer)
                surface->window.want = WINDOW_WANT_POINTER;
        if (away)
                surface->window.style |= WINDOW_MINIMIZED;
        if (wider >= 0)
                surface->window.width = (unsigned int)wider;
        for (positive at = 0; title && title[at] && at < sizeof(surface->window.title) - 1; at++)
                surface->window.title[at] = title[at];
        surface->buffer_index = 0;
        if (!never)
                surface_commit(surface);

        // WRITERS processes on the one connection, each committing COMMITS times.
        for (bipolar at = 1; at < writers && flood && !child; at++)
                child = system_fork() == 0;

        for (bipolar at = 0; at < flood; at++)
        {
                surface->damage_count = 1;
                surface->damage[0] = (struct surface_damage){0, 0, (int)width, (int)height};
                surface_commit(surface);
        }

        if (child)
                return 0;

        // What a hostile program does: writes where the compositor wrote
        // its offsets, and commits.
        if (offset >= 0)
        {
                surface->buffer_offset[0] = (unsigned int)offset;
                surface_commit(surface);
        }

        // A second create on the connection it already made a surface on.
        if (twice)
        {
                struct surface_message message = {SURFACE_CREATE, 0, {0}};
                long file = window_call(WINDOW_SYS_MEMFD_CREATE, (long)"surface",
                                        SURFACE_MFD_CLOEXEC | SURFACE_MFD_ALLOW_SEALING, 0, 0, 0, 0);

                window_call(WINDOW_SYS_FTRUNCATE, file, surface->window.mapping, 0, 0, 0, 0);
                window_call(WINDOW_SYS_FCNTL, file, SURFACE_F_ADD_SEALS, SURFACE_SEAL_FIXED, 0, 0, 0);
                message.request.size = sizeof(message.request);
                message.request.protocol = SURFACE_PROTOCOL;
                message.request.role = SURFACE_ROLE_APP;
                message.request.width = (unsigned int)width;
                message.request.height = (unsigned int)height;
                message.request.buffer_slots = 2;
                surface_send(surface->window.handle, &message, (int)file);
                window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                while (surface_receive(surface->window.handle, &message, 0) > 0 &&
                       message.kind != SURFACE_CREATED)
                        ;
                string_format(log, "surface: second create answered %b\n", (bipolar)message.status);
                log_flush();
        }

        string_format(log, "surface: %p at %b,%b\n", (positive)surface->surface_id,
                      (bipolar)surface->window.x, (bipolar)surface->window.y);
        string_format(log, "surface: role %p state %p layer %p anchor %p zone %p lock %p\n",
                      (positive)surface->role_requested, (positive)surface->role_state,
                      (positive)surface->layer, (positive)surface->anchor_applied,
                      (positive)surface->exclusive_applied, (positive)surface->lock_applied);
        log_flush();

        until = seconds >= 0 ? clock_monotonic_nanoseconds() + (positive)seconds * 1000000000ull : 0;

        for (;;)
        {
                struct surface_event event;
                long timeout = -1;
                b32 closing = 0;

                if (until)
                {
                        positive now = clock_monotonic_nanoseconds();

                        if (now >= until)
                                break;
                        timeout = (long)(until - now);
                }

                if (surface_wait(surface, -1, timeout) < 0)
                {
                        log_direct(str("surface: the compositor has gone\n"));
                        break;
                }

                while (surface_event(surface, &event))
                {
                        string_format(log, "surface: event %p %p %b %b\n", (positive)event.type,
                                      (positive)event.flags, (bipolar)event.a, (bipolar)event.b);
                        closing |= event.type == SURFACE_EVENT_CLOSE;
                }
                log_flush();

                if (closing)
                        break;
        }

        surface_text_free();
        surface_close(surface);
        return 0;
}

// panel -----------------------------------------------------------
/*
        panel [-f FONT.ttf]

        The panel, as a program that holds the panel role: a band as wide as
        the output along its edge, with the launcher button, a button for
        each window, the focused one lit, and at the right end the tray with
        the time. It draws the shapes the compositor's own panel does, from
        the same code, and the same buttons are pressed: a press on one
        asks the compositor to bring that window up, which restores it if
        it was put away. Given a font it writes each window's title on its
        button and the time in the tray; without one the shapes are all
        there is, and nothing wakes it but the compositor.

        It has one buffer and draws into it where it is: it is the pixels of
        a band that is redrawn only when the windows change or the minute
        does, and a second slot would be as much memory again for nothing.
        The window list is read from the page the compositor writes it in.
        Run as `compositor grant panel /panel`, so that it is given the role.
*/
static struct
{
        struct surface *surface;
        struct surface_listing listing;
        font_face face;
        font_engine address_to engine;
        int width, height, scale, left_x, left_y;
        unsigned int minute;
} panel;

static p8 panel_atlas[128 * 128];

// The window buttons' marks and the frame they are laid out in, which are what is drawn and pressed.
static void panel_frame(struct compositor_frame *frame, unsigned char *marks)
{
        for (unsigned int at = 0; at < panel.listing.count; at++)
                marks[at] = (unsigned char)panel.listing.entry[at].state;

        *frame = (struct compositor_frame){0, 0, panel.width, panel.height, panel.scale, 0, 0, 0, null, 0,
                                           1, marks, panel.listing.count};
}

// One line of text at the left of a slot, in the slot's own colour for ground, cut off at its edge.
static positive panel_text(int x, int y, int width, int height, string_address text,
                           unsigned int ground, int centred)
{
        font_metrics grid = font_metrics_at(&panel.face, (p32)(height * 11 / 20) << 6);
        const font_face address_to faces[1] = {&panel.face};
        unsigned int *pixels = surface_buffer(panel.surface, 0);
        font_target here = {pixels + (long)y * panel.width + x, (positive)panel.width, width, height};
        p32 em = (p32)(height * 11 / 20) << 6;
        positive length = string_length(text);
        b32 advance = 0;
        int pad = 4 * panel.scale;

        if (centred)
        {
                // How wide the run is, before it is put anywhere.
                unsigned int scratch[1] = {0};
                font_target nowhere = {scratch, 1, 1, 1};

                advance = font_draw(panel.engine, faces, 1, (const p8 address_to)text, length, em, 0,
                                    &nowhere, 0, 0, COMPOSITOR_INK_TEXT, ground) >> 16;
                pad = (width - (int)advance) / 2;
        }

        return font_draw(panel.engine, faces, 1, (const p8 address_to)text, length, em, 0, &here,
                         (b32)(pad > 0 ? pad : 0) << 16, (height - grid.cell_height) / 2 + grid.baseline,
                         COMPOSITOR_INK_TEXT, ground);
}

static unsigned int panel_minute(void)
{
        timespec now = {0, 0};

        clock_gettime(CLOCK_REALTIME, address_of now);
        return (unsigned int)(now.tv_sec / 60);
}

static void panel_draw(void)
{
        unsigned char marks[SURFACE_LISTING_MAX];
        struct compositor_frame frame;
        struct compositor_target target = {surface_buffer(panel.surface, 0), (unsigned long)panel.width,
                                           panel.width, panel.height, 0, 0, panel.width, panel.height};
        int x, y, width, height;

        panel_frame(&frame, marks);
        compositor_panel(&target, &frame);

        if (!panel.engine)
                return;

        for (unsigned int at = 0; at < panel.listing.count; at++)
                if (compositor_panel_slot(&frame, (int)at, &x, &y, &width, &height))
                        panel_text(x, y, width, height, panel.listing.entry[at].title,
                                   marks[at] & SURFACE_LIST_MINIMIZED ? COMPOSITOR_INK_PANEL
                                   : marks[at] & SURFACE_LIST_FOCUSED ? COMPOSITOR_INK_TITLE_LIT
                                                                     : COMPOSITOR_INK_TITLE,
                                   0);

        if (compositor_panel_slot(&frame, -2, &x, &y, &width, &height))
        {
                timespec now = {0, 0};
                time_t seconds;
                tm broken;
                char clock[6];

                clock_gettime(CLOCK_REALTIME, address_of now);
                seconds = now.tv_sec;
                if (localtime_r(&seconds, &broken))
                {
                        clock[0] = (char)('0' + broken.tm_hour / 10);
                        clock[1] = (char)('0' + broken.tm_hour % 10);
                        clock[2] = ':';
                        clock[3] = (char)('0' + broken.tm_min / 10);
                        clock[4] = (char)('0' + broken.tm_min % 10);
                        clock[5] = 0;
                        panel_text(x, y, width, height, clock, COMPOSITOR_INK_DESKTOP, 1);
                }
        }
}

static void panel_show(void)
{
        panel_draw();
        panel.surface->damage_count = 0;
        panel.surface->buffer_index = 0;
        surface_commit(panel.surface);
}

static b32 panel_main()
{
        string_address path = null;
        unsigned int generation = 0;
        int pointer_x = 0, pointer_y = 0, running = 1;

        claim_standard_descriptors();
        memory_zero(&panel, sizeof(panel));

        for (positive at = 1; at + 1 < program_argument_count(); at++)
                if (compositor_option(program_argument(at), 'f'))
                        path = program_argument(++at);

        // The compositor says how big the band is; the page is made that size.
        panel.surface = surface_open_slots(1, 1, SURFACE_ROLE_PANEL, 1);
        if (!panel.surface && surface_refused == SURFACE_ESIZE)
                panel.surface = surface_open_slots(surface_wanted_width, surface_wanted_height,
                                                   SURFACE_ROLE_PANEL, 1);
        if (!panel.surface)
        {
                string_format(log, "panel: refused %b\n", (bipolar)surface_refused);
                log_flush();
                return 1;
        }

        panel.width = (int)panel.surface->window.width;
        panel.height = (int)panel.surface->window.height;
        panel.scale = panel.height / COMPOSITOR_PANEL;
        panel.surface->window.want = WINDOW_WANT_POINTER;

        if (path)
        {
                positive size = 0;
                positive bytes = surface_map_file(path, address_of size);
                long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);

                if (bytes && !(engine < 0 && engine > -4096) &&
                    font_load(&panel.face, (const p8 address_to)bytes, size, 0) == 0)
                {
                        panel.engine = (font_engine address_to)engine;
                        font_start(panel.engine, panel_atlas, 128, 128);
                }
                else
                        log_direct(str("panel: no font\n"));
        }

        panel.minute = panel_minute();
        generation = surface_listing_read(panel.surface, &panel.listing);
        panel_show();

        while (running)
        {
                struct surface_event event;
                int redraw = 0;
                long timeout = -1;

                // A clock is the one thing that wakes it by itself: at the next minute.
                if (panel.engine)
                {
                        timespec now = {0, 0};

                        clock_gettime(CLOCK_REALTIME, address_of now);
                        timeout = (59 - now.tv_sec % 60) * 1000000000l + (1000000000l - now.tv_nsec) + 1000000l;
                }

                if (surface_wait(panel.surface, -1, timeout) < 0)
                        break;

                while (surface_event(panel.surface, &event))
                {
                        if (event.type == SURFACE_EVENT_CLOSE || (event.type == SURFACE_EVENT_ROLE &&
                                                                 event.a != (int)SURFACE_ROLE_ACTIVE))
                                running = 0;
                        else if (event.type == SURFACE_EVENT_POINTER_MOTION)
                        {
                                pointer_x = event.a;
                                pointer_y = event.b;
                        }
                        else if (event.type == SURFACE_EVENT_POINTER_BUTTON && (event.flags & SURFACE_KEY_DOWN) &&
                                 event.a == (int)COMPOSITOR_BTN_LEFT)
                        {
                                unsigned char marks[SURFACE_LISTING_MAX];
                                struct compositor_frame frame;
                                int hit;

                                panel_frame(&frame, marks);
                                hit = compositor_panel_hit(&frame, pointer_x, pointer_y);
                                if (hit >= 0)
                                        surface_activate(panel.surface, panel.listing.entry[hit].id);
                        }
                }

                if (surface_listing_read(panel.surface, &panel.listing) != generation)
                {
                        generation = surface_listing_read(panel.surface, &panel.listing);
                        redraw = 1;
                }

                if (panel.engine && panel_minute() != panel.minute)
                {
                        panel.minute = panel_minute();
                        redraw = 1;
                }

                if (redraw && running)
                        panel_show();
        }

        surface_close(panel.surface);
        return 0;
}

#endif // MOONWATER_CANVAS_COMPOSITOR
