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

/* ---- backend: the look on the CPU ---- */

// The palette, neutral greys and no hue: the desktop, and the black line round a window. The band's inks are in
// taskbar.h, with the tabs drawn without a font; with a font the tabs are glass, inked from what is under them.
#define COMPOSITOR_INK_DESKTOP 0x1e1e1eu
#define COMPOSITOR_INK_LINE 0x000000u
// The line of the window with the keys: white at 30%.
#define COMPOSITOR_INK_LINE_LIT 0x4d4d4du

// The desktop's ink as `moonwater canvas background` set it, and the default when none is: what is drawn
// where no window is, and behind a window's round corners. The tabs' glass is made from what is on the screen.
static unsigned int compositor_ground = COMPOSITOR_INK_DESKTOP;

// The radius of a window's corners and the width of the line round it, in logical pixels; and the largest
// radius in device pixels, which is what the corners' scratch is made for. (The panel's height is in taskbar.h.)
#define COMPOSITOR_RADIUS 8
#define COMPOSITOR_LINE 1
// The top of a window that is a titlebar with nothing drawn -- a press there takes the window whatever is
// under it -- and its close button, a disc that far in from the window's top right, in logical pixels.
#define COMPOSITOR_GRAB 24
#define COMPOSITOR_CLOSE 14
#define COMPOSITOR_CLOSE_IN 5
#define COMPOSITOR_RADIUS_MOST 64

#include "ui/taskbar.h"

static struct taskbar_view compositor_taskbar_view(void);

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch; // pixels a row
        int width, height;
        int x1, y1, x2, y2;  // the damage: nothing outside it is written
        int ox, oy;          // the point pixels[0] is, on the output (a corner's scratch); 0 for the output
        unsigned int *scratch; // where the scene draws a round corner's square; none is the one of its own
};

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
        int slot;            // the compositor's panel: the tab this frame is (-2 tray, 0 up windows)
        unsigned int position; // the panel: where its band sits (TASKBAR_BOTTOM, TOP, LEFT or RIGHT)
        const unsigned char *lengths; // the panel: each window's title in characters, which its tab fits
        int close;           // a window with a close button over its top right
        unsigned int close_mean; // the mean colour under the close button, made once a present (compositor_close_mean)
};

/*
        The close button: a disc at the window's top right, inset from both
        edges, and none on a window too small to hold it beside its corner.
        For drawing and for the press alike.
*/
static int compositor_close_box(const struct compositor_frame *frame, int *x, int *y, int *side)
{
        int box = COMPOSITOR_CLOSE * frame->scale, in = COMPOSITOR_CLOSE_IN * frame->scale;

        if (!frame->close || frame->builtin || frame->width < box + 2 * in + 4 * box || frame->height < box + 2 * in)
                return 0;
        *side = box;
        *x = frame->x + frame->width - box - in;
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

        if (x2 <= x1)
                return;

        for (int row = y1; row < y2; row++)
                memory_copy(t->pixels + (unsigned long)(row - t->oy) * t->pitch + (unsigned long)(x1 - t->ox),
                            source + (unsigned long)(row - y) * pitch + (unsigned long)(x1 - x),
                            (unsigned long)(x2 - x1) * 4);
}

// A title's length in characters, the bytes that begin one, to its end or the listing's.
static unsigned char compositor_title_length(const char *title, unsigned int most)
{
        unsigned int count = 0;

        for (unsigned int at = 0; at < most && title[at]; at++)
                count += ((unsigned char)title[at] & 0xc0) != 0x80;
        return (unsigned char)(count > 255 ? 255 : count);
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
// COMPOSITOR_RADIUS_MOST, 89440 bytes, of which only the first few pages are ever written (a window's radius is
// eight to its scale, four times at most).
#define COMPOSITOR_MASKS_BYTES 89440
// Mapped when the first is wanted, not kept in the image: the address space of every program that links this is
// the image's, and a shell under a 12 MiB cap has none to spare.
static void *compositor_map_zeroed(unsigned long bytes);
static unsigned char *compositor_corner_masks;
static unsigned char compositor_corner_made[COMPOSITOR_RADIUS_MOST + 1];
static unsigned char compositor_corner_none[COMPOSITOR_RADIUS_MOST * COMPOSITOR_RADIUS_MOST];

static const unsigned char *compositor_corner_mask(int radius)
{
        unsigned char *mask;

        if (!compositor_corner_masks)
        {
                compositor_corner_masks = compositor_map_zeroed(COMPOSITOR_MASKS_BYTES);
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
                compositor_corner_made[radius] = 1;
        }
        return mask;
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

static unsigned int compositor_mix(unsigned int ground, unsigned int ink, unsigned int cover)
{
        unsigned int out = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                out |= (((ground >> shift & 255) * (255 - cover) + (ink >> shift & 255) * cover + 127) / 255) << shift;
        return out;
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
#define COMPOSITOR_CLOSE_MOST (COMPOSITOR_CLOSE * 8)

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
            (compositor_close_cover.side == side && compositor_close_cover.half == frame->scale))
                return;
        for (int y = 0; y < side; y++)
                for (int x = 0; x < side; x++)
                {
                        unsigned int halo, cross;

                        compositor_close_sample(side, frame->scale, x, y, &halo, &cross);
                        compositor_close_cover.halo[y * side + x] = (unsigned char)halo;
                        compositor_close_cover.cross[y * side + x] = (unsigned char)cross;
                }
        compositor_close_cover.side = side;
        compositor_close_cover.half = frame->scale;
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

        unsigned int mean = frame->close_mean, ink = window_contrast(mean), tone = window_light(mean) ? 0xffffffu : 0x000000u;
        int half = frame->scale; // half the X's stroke
        int made = compositor_close_cover.side == side && compositor_close_cover.half == half;

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

                        unsigned int under = frame->pixels[(unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(px - frame->x)];
                        unsigned int pixel = compositor_mix(under, window_mix(under, tone, 140), halo * 255 / 16);

                        if (cross)
                                pixel = compositor_mix(pixel, ink, cross * 255 / 16);
                        t->pixels[(unsigned long)(py - t->oy) * t->pitch + (unsigned long)(px - t->ox)] = pixel;
                }
}

// What is inside a frame's line, through the target's clip: a window's pixels, or one of the panel's tabs.
static void compositor_contents(const struct compositor_target *t, const struct compositor_frame *frame)
{
        if (frame->builtin)
                compositor_panel_tab(t, frame->slot,
                                     frame->slot >= 0 && frame->slot < (int)frame->mark_count ? frame->marks[frame->slot] : 0,
                                     frame->x, frame->y, frame->width, frame->height);
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
static void compositor_draw(const struct compositor_target *t,
                            const struct compositor_frame *frame)
{
        int r = compositor_radius(frame), x = frame->x, y = frame->y, w = frame->width, h = frame->height;
        int ox, oy, ow, oh, b, ri;
        struct compositor_target part;

        compositor_outline(frame, &ox, &oy, &ow, &oh);
        b = x - ox;
        ri = r > b ? r - b : 0;

        // The line, between the corner squares.
        if (b)
        {
                compositor_fill(t, ox + r, oy, ow - 2 * r, b, frame->edge);
                compositor_fill(t, ox + r, y + h, ow - 2 * r, b, frame->edge);
                compositor_fill(t, ox, oy + r, b, oh - 2 * r, frame->edge);
                compositor_fill(t, x + w, oy + r, b, oh - 2 * r, frame->edge);
        }

        // The contents, but for the corners' squares.
        if (!r)
        {
                compositor_contents(t, frame);
                compositor_close_draw(t, frame);
                return;
        }
        part = compositor_clipped(t, x + ri, y, w - 2 * ri, h);
        compositor_contents(&part, frame);
        part = compositor_clipped(t, x, y + ri, ri, h - 2 * ri);
        compositor_contents(&part, frame);
        part = compositor_clipped(t, x + w - ri, y + ri, ri, h - 2 * ri);
        compositor_contents(&part, frame);

        const unsigned char *outer = compositor_corner_mask(r), *inner = ri ? compositor_corner_mask(ri) : 0;
        unsigned int tab = frame->builtin ? taskbar_tab_ink(frame->slot, frame->slot >= 0 &&
                                                                                    frame->slot < (int)frame->mark_count
                                                                                ? frame->marks[frame->slot]
                                                                                : 0)
                                          : 0;

        for (int corner = 0; corner < 4; corner++)
        {
                int cx = corner & 1 ? ox + ow - r : ox, cy = corner & 2 ? oy + oh - r : oy;
                int x1 = cx > t->x1 ? cx : t->x1, y1 = cy > t->y1 ? cy : t->y1;
                int x2 = cx + r < t->x2 ? cx + r : t->x2, y2 = cy + r < t->y2 ? cy + r : t->y2;

                if (frame->square & (1 << corner))
                {
                        if (b)
                        {
                                part = compositor_clipped(t, cx, cy, r, r);
                                compositor_fill(&part, cx, cy, r, r, frame->edge);
                        }
                        part = compositor_clipped(t, cx, cy, r, r);
                        compositor_contents(&part, frame);
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
                                        pixel = b ? compositor_mix(pixel, frame->edge, cover) : pixel;
                                if (px >= x && px < x + w && py >= y && py < y + h)
                                {
                                        unsigned int in = inner ? inner[(my - b) * ri + (mx - b)] : 0;
                                        unsigned int ink = frame->builtin ? tab
                                                           : frame->pixels[(unsigned long)(py - y) * frame->pitch +
                                                                           (unsigned long)(px - x)];

                                        if (!b)
                                                in = cover;
                                        pixel = in == 255 ? ink : in ? compositor_mix(pixel, ink, in) : pixel;
                                }
                                *at = pixel;
                        }
        }
        compositor_close_draw(t, frame);
}

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

        for (int slot = 0; slot <= (int)band->mark_count; slot++)
        {
                int which = slot == (int)band->mark_count ? -2 : slot;
                int x, y, width, height;

                if (!taskbar_slot(&view, which, &x, &y, &width, &height))
                        continue;
                tabs[count] = *band;
                tabs[count].x = x;
                tabs[count].y = y;
                tabs[count].width = width;
                tabs[count].height = height;
                tabs[count].slot = which;
                tabs[count].radius = COMPOSITOR_RADIUS * band->scale;
                tabs[count].line = 0;
                tabs[count].square = taskbar_square(band->position);
                if (!band->builtin)
                        tabs[count].pixels = band->pixels + (unsigned long)(y - band->y) * band->pitch + (unsigned long)(x - band->x);
                count++;
        }
        return count;
}

// What a window fills: nothing, the output, or half of it; or the room of its screen, the taskbar left out, which only
// a placement asks for (WINDOW_PLACE_MAXIMIZED) and which follows the taskbar where it is moved.
#define COMPOSITOR_FLOATING 0
#define COMPOSITOR_MAXIMIZED 1
#define COMPOSITOR_LEFT 2
#define COMPOSITOR_RIGHT 3
#define COMPOSITOR_ROOM 4
// The windows, and a tab for each window and a tray for each band (`taskbar display all` is one on every screen).
#define COMPOSITOR_FRAMES (2 * WINDOWS + 1 + WINDOWS_OUTPUTS)

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

static void compositor_scene(const struct compositor_target *t,
                             const struct compositor_frame *frames, unsigned int count)
{
        unsigned int *scratch = t->scratch ? t->scratch : compositor_corner_scratch;
        int edge[2 * COMPOSITOR_PIECES + 2];
        int box[COMPOSITOR_PIECES][4];
        unsigned char owner[COMPOSITOR_PIECES];
        unsigned int edges = 2, pieces = 0;

        edge[0] = t->y1;
        edge[1] = t->y2;

        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_frame *f = &frames[at];
                int r = compositor_radius(f), fx, fy, fw, fh, x2, y2;
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
                                compositor_draw(&cell, &frames[owner[at]]);

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
                int r = compositor_radius(f), fx, fy, fw, fh;

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
                                compositor_draw(&square, &frames[each]);
                        compositor_blit(t, cx, cy, r, r, scratch, (unsigned long)r);
                }
        }
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
                compositor_scene(&band, banding->frames, banding->count);
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
                compositor_scene(&slice, banding->frames, banding->count);
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
        struct compositor_banding banding = {t, frames, count, 1, apart, {0}, {0}};

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
#define CK_DEAD 0x8000u
#define CK_TABLE 99
#define CK_LEVELS 4
#define CK_102ND 86
#define CK_CAPSLOCK 58
#define CK_KEY_BACKSPACE 14
#define CK_KEY_TAB 15
#define CK_KEY_F4 62
#define CK_KEY_F9 67
#define CK_ACUTE 0xB4u
#define CK_GRAVE '`'
#define CK_DIAERESIS 0xA8u
#define CK_CIRCUMFLEX '^'
#define CK_TILDE '~'
#define D(c) (CK_DEAD | (c))

struct compositor_patch
{
        unsigned char code;
        unsigned short level[CK_LEVELS];
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
        unsigned short dead;     // the accent that waits for a letter, or 0
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
        unsigned short level[CK_LEVELS] = {0, 0, 0, 0};
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
                map->dead = (unsigned short)accent;
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

        compositor_screens_box(screens, count, box);
        *x = compositor_fit(*x, box[0] + line, box[0] + box[2] - width - line);
        *y = compositor_fit(*y, box[1] + line, box[1] + box[3] - height - line);
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
        *x = compositor_fit(*x, screens[owner][0] + line, screens[owner][0] + screens[owner][2] - width - line);
        *y = compositor_fit(*y, screens[owner][1] + line, screens[owner][1] + screens[owner][3] - height - line);
}

/*
        Where a drag let go at x, y arranges its window: the screen under the
        pointer and what of it the window fills (COMPOSITOR_MAXIMIZED at its
        top, the halves at its sides, the top before the sides so a corner
        fills it), or COMPOSITOR_FLOATING. An edge another screen is against
        is no edge: the pointer goes on across it.
*/
static unsigned int compositor_screen_snap(const int (*screens)[4], unsigned int count, int x, int y, int margin,
                                           int *screen)
{
        int at = compositor_screen_at(screens, count, x, y);
        const int *s;

        *screen = at;
        if (at < 0)
                return COMPOSITOR_FLOATING;
        s = screens[at];
        if (y - s[1] < margin && compositor_screen_at(screens, count, x, s[1] - 1) < 0)
                return COMPOSITOR_MAXIMIZED;
        if (x - s[0] < margin && compositor_screen_at(screens, count, s[0] - 1, y) < 0)
                return COMPOSITOR_LEFT;
        if (s[0] + s[2] - 1 - x < margin && compositor_screen_at(screens, count, s[0] + s[2], y) < 0)
                return COMPOSITOR_RIGHT;
        return COMPOSITOR_FLOATING;
}

// What a window arranged as how fills of a screen, x, y, width, height: all of it, or its left or right half.
static void compositor_screen_part(const int *screen, unsigned int how, int *part)
{
        int half = screen[2] / 2;

        part[0] = how == COMPOSITOR_RIGHT ? screen[0] + half : screen[0];
        part[1] = screen[1];
        part[2] = how == COMPOSITOR_LEFT ? half : how == COMPOSITOR_RIGHT ? screen[2] - half : screen[2];
        part[3] = screen[3];
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
#define COMPOSITOR_ABS_MT_POSITION_X 0x35u

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
#define COMPOSITOR_DEVICES 64 // evdev numbers the hotplug watch can name
// How long the processor is kept out of its deep idle states after the last thing a person did.
#define COMPOSITOR_HOLD_NS 500000000ull
// A machine slept when the clock that runs through a sleep got this far ahead of the one that does
// not, and the screen is drawn again then, at half a second and at two and a half: a card's driver
// is still settling after a wake, and the later passes are the ones that land.
#define COMPOSITOR_SLEPT_NS 50000000ull
#define COMPOSITOR_WAKE_FIRST_NS 500000000ull
#define COMPOSITOR_WAKE_SECOND_NS 2000000000ull
// Two presses on a window's top are a pair when the second comes this soon after the first,
// and a drag that ends this near the edge of the output fills it or half of it.
#define COMPOSITOR_AGAIN_NS 400000000ull

// The band at a window's edges a press resizes it by -- six pixels outside them and two inside, so a hand
// a pixel short of a line one pixel wide still has it -- and the step a new window is put down and across
// from one in its place, in logical pixels.
#define COMPOSITOR_GRIP 6
#define COMPOSITOR_GRIP_IN 2
#define COMPOSITOR_STEP 20
#define COMPOSITOR_EDGE_LEFT CP_EDGE_LEFT
#define COMPOSITOR_EDGE_RIGHT CP_EDGE_RIGHT
#define COMPOSITOR_EDGE_TOP CP_EDGE_TOP
#define COMPOSITOR_EDGE_BOTTOM CP_EDGE_BOTTOM
#define COMPOSITOR_SNAP 8
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
        char name[WINDOWS_OUTPUT_NAME];
        unsigned int shows;
        int cursor_x, cursor_y;
};

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch; // pitch in bytes
        unsigned int fb, crtc;
        // Where a store to the card is dear: slices of the scene composed apart, and what the card holds (mirror, words a row
        // mirror_pitch; the scanout itself where the card's memory is the machine's). changed: 0 not yet asked, 1 on,
        // -1 off. mirror_stale: the card may hold anything, so the next present stores all it composes.
        unsigned int *composing, *mirror;
        unsigned long mirror_pitch;
        int changed, mirror_stale;
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
        unsigned int seen[WINDOWS];     // the frame last taken
        unsigned int showing[WINDOWS];  // it has shown a picture: it is on the screen and has a tab
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
        unsigned int asks[WINDOWS]; // what the program asked of its window besides glass (WINDOW_NO_CLOSE, WINDOW_DISMISS)
        unsigned int *backdrop[WINDOWS], *tinted[WINDOWS], *composed[WINDOWS];
        unsigned int *boosted[WINDOWS]; // compositor_vivid of each backdrop cell, as the cell last was
        unsigned long glass_sum[WINDOWS][3]; // per channel, the sums of boosted over the blocks: the side's mean
        int glass_blocks[WINDOWS][4]; // the first block kept of the small picture under it, across and down, and how many each way
        unsigned long glass_cut[WINDOWS];
        int glass_made[WINDOWS][4], stale[WINDOWS][4], dirty[WINDOWS][4];
        unsigned int glass_side[WINDOWS], glass_told[WINDOWS], lifted[WINDOWS];
        unsigned int *blur, *frost, *rise; // scratch: the tabs' small picture, a band of the scene halved, the levels risen
        unsigned long blur_bytes, frost_bytes, rise_bytes;
        positive blurred, recomposed; // pixels blurred again and composed again, all frames
        int damage_by; // what is being damaged: 0 the scene, a row and one its own picture, COMPOSITOR_OVER what lies over all
        font_face face;
        font_engine *engine;

        int focus, hover, grab, drag, resize, press; // rows, or COMPOSITOR_NONE
        positive press_ns; // when the last press on a window's top was
        int drag_x, drag_y;
        int drag_moved; // the window being moved has moved: only then is where it is let go an arrangement
        // The window being resized, how large it was and where the hand took hold.
        int resize_w, resize_h, press_x, press_y;
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

// A colour of the pointer: its fill is white and its outline black, or the other way for the dark one.
// The plane's pixels have an alpha, the scanout's none.
static unsigned int compositor_cursor_ink(int outline, unsigned int alpha)
{
        return ((compositor.cursor.look == 2) == outline ? 0xffffffu : 0u) | alpha;
}

// Whether the pointer is drawn into the scanout: when the card has no plane for it, or the plane is too
// small for the size it was asked to be.
static int compositor_cursor_in_scene(void)
{
        return compositor.cursor.soft ||
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
                        int row = compositor.stack[at], box[4], bx, by, side, meet[4];

                        if (!compositor_visible(row))
                                continue;
                        compositor_frame_of(row, &frame);
                        if (!compositor_close_box(&frame, &bx, &by, &side))
                                continue;
                        box[0] = bx < 0 ? 0 : bx, box[1] = by < 0 ? 0 : by;
                        box[2] = bx + side > compositor.width ? compositor.width : bx + side;
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

// The line round a window while it floats, in device pixels; a window filling the output or half of it
// meets the output's edges with none, and square.
static int compositor_line(int row)
{
        return compositor.arranged[row] == COMPOSITOR_FLOATING ? COMPOSITOR_LINE * compositor.scale : 0;
}

// The buffer a window's picture is in, where the compositor made it: every row of it the output's width.
static unsigned int *compositor_pixels(int row)
{
        return (unsigned int *)((char *)compositor.table + compositor.pixels +
                                ((unsigned long)row * 2 + compositor.front[row]) * compositor.buffer);
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

        if (width > drawn_w)
                for (unsigned int line = 0; line < drawn_h && line < height; line++)
                        memory_fill_u32(pixels + line * stride + drawn_w, width - drawn_w, pixels[line * stride + drawn_w - 1]);
        for (unsigned int line = drawn_h; line < height; line++)
                memory_copy(pixels + line * stride, pixels + (drawn_h - 1) * stride, (positive)width * 4);
}

static void compositor_frame_of(int row, struct compositor_frame *frame)
{
        int line = compositor_line(row);

        *frame = (struct compositor_frame){compositor.x[row], compositor.y[row], (int)compositor.w[row],
                                           (int)compositor.h[row], compositor.scale, compositor_pixels(row),
                                           (unsigned long)compositor_stride(), 0, null, 0,
                                           line ? COMPOSITOR_RADIUS * compositor.scale : 0, line,
                                           row == compositor.focus ? COMPOSITOR_INK_LINE_LIT : COMPOSITOR_INK_LINE};
        frame->close = !(compositor.asks[row] & WINDOW_NO_CLOSE);
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

static int compositor_screen_of(int row);

// How many bands there are: one on every screen with `taskbar display all`, and the primary's alone otherwise.
static int compositor_bands(void)
{
        return taskbar_display_all() ? compositor_output_count() : 1;
}

// The windows that have a tab, in the order they first showed, and what each tab shows: every window on the one band,
// and on each of several, the windows whose screen it is on (compositor_screen_of).
static unsigned int compositor_windows(void)
{
        unsigned int count = 0;
        int bands = compositor_bands();

        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned int place = count;

                if (!compositor.showing[row] || (bands > 1 && compositor_screen_of(row) != compositor.band_at))
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

// How far the blur reaches, at the scale: a power of two, which the passes' steps add up to less one.
static int compositor_glass_reach(int logical)
{
        int reach = 1;

        while (reach * 2 <= logical * compositor.scale)
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
                        for (unsigned long at = 0; at < (unsigned long)tw * th; at++)
                                small[at] = ~small[at];
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

// A colour half as far again from its grey, kept in a byte.
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
        and tinted toward one side of lightness for the whole window -- the
        side its mean is on, held until the mean is well over on the other,
        so a backdrop near the middle does not flicker -- and every pixel
        kept on that side, so an ink that stands out from the ground the
        program is told stands out everywhere. The program's picture,
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
#define COMPOSITOR_GLASS_TINT 96      // how far toward its side a glass window's tint goes, in 256ths
#define COMPOSITOR_GLASS_DARKEST 0x5c // dark glass: no colour of a pixel brighter
#define COMPOSITOR_GLASS_LIGHTEST 0xb4 // light glass: none dimmer
#define COMPOSITOR_GLASS_SIDE 116394750u // window_light's line between the sides
#define COMPOSITOR_GLASS_UNSIDED 2u
#define COMPOSITOR_GLASS_MARGIN 3 // the blocks kept round a window: what the rise reads past its edge

// The tint of a pixel already made more vivid (compositor_vivid), so a caller holding that value does not boost it again.
static unsigned int compositor_glass_tone(unsigned int boosted, unsigned int side)
{
        unsigned int pixel = window_mix(boosted, side ? 0xffffffu : 0, COMPOSITOR_GLASS_TINT);
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
        A block's boosted pixel went from was to now: the channel sums take
        the new in and the old out. The old is in the sums already, so the
        subtraction never takes them below zero.
*/
static void compositor_glass_sum_change(unsigned long *sum, unsigned int was, unsigned int now)
{
        sum[0] += now >> 16 & 255;
        sum[0] -= was >> 16 & 255;
        sum[1] += now >> 8 & 255;
        sum[1] -= was >> 8 & 255;
        sum[2] += now & 255;
        sum[2] -= was & 255;
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
        unsigned int was = compositor.glass_side[row], mean, light, ground;

        if (!count)
                return 0;

        mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
        {
                unsigned int r = mean >> 16 & 255, g = mean >> 8 & 255, b = mean & 255;

                light = 2126u * r * r + 7152u * g * g + 722u * b * b;
        }
        compositor.glass_side[row] = was == COMPOSITOR_GLASS_UNSIDED ? light > COMPOSITOR_GLASS_SIDE
                                     : was                           ? light > COMPOSITOR_GLASS_SIDE / 3 * 2
                                                                     : light > COMPOSITOR_GLASS_SIDE / 2 * 3;
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
                        for (long cell = 0; cell < (long)blocks[2] * blocks[3]; cell++)
                                compositor.boosted[row][cell] = 0;
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
                                for (int by = by0; by < by1; by++)
                                        for (int bx = bx0; bx < bx1; bx++)
                                        {
                                                long cell = (long)(by - blocks[1]) * blocks[2] + (bx - blocks[0]);
                                                unsigned int now = compositor_vivid(compositor.backdrop[row][cell]);

                                                // The one boost of the cell: its sum moves, and its tint comes from it.
                                                compositor_glass_sum_change(compositor.glass_sum[row], compositor.boosted[row][cell], now);
                                                compositor.boosted[row][cell] = now;
                                                compositor.tinted[row][cell] = compositor_glass_tone(now, compositor.glass_side[row]);
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
                                        for (long cell = 0; cell < (long)blocks[2] * blocks[3]; cell++)
                                                compositor.tinted[row][cell] = compositor_glass_tone(compositor.boosted[row][cell], compositor.glass_side[row]);
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

        for (int py = by; py < by + side; py++)
                for (int px = bx; px < bx + side; px++)
                {
                        unsigned int under = frame->pixels[(unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(px - frame->x)];

                        sum[0] += under >> 16 & 255;
                        sum[1] += under >> 8 & 255;
                        sum[2] += under & 255;
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
            compositor.drag >= 0 || compositor.resize >= 0 || compositor_output_count() > 1)
                return -1;
        row = rows[windows - 1];
        if (compositor.table->glass[row] || compositor.x[row] != 0 || compositor.y[row] != 0 ||
            compositor.w[row] != (unsigned int)compositor.width || compositor.h[row] != (unsigned int)compositor.height ||
            compositor.picture_w[row] != compositor.w[row] || compositor.picture_h[row] != compositor.h[row] ||
            compositor.asking[row] || compositor_radius(&frames[windows - 1]) || compositor_line(row))
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
static int compositor_changed_ready(void)
{
        static const char *const apart[] = {"amdgpu", "radeon", "nouveau", "bochs-drm", "qxl", "vmwgfx"};
        static const char *const shared[] = {"virtio_gpu", "simpledrm", "cirrus-qemu", "hyperv_drm", "ast",
                                             "mgag200", "udl", "gud"};
        unsigned long bytes = (unsigned long)compositor.width * (unsigned long)compositor.height * 4;
        unsigned long slices = (unsigned long)compositor.width * COMPOSITOR_SLICE * COMPOSITOR_BANDS * 4;
        int own;

        if (compositor.changed)
                return compositor.changed > 0;
        compositor.changed = -1;
        if (compositor.card < 0)
                return 0;
        own = compositor_driver_in(apart, sizeof(apart) / sizeof(apart[0]));
        if (!own && !compositor_driver_in(shared, sizeof(shared) / sizeof(shared[0])))
                return 0;

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
        if (!windows || compositor.drag >= 0 || compositor.resize >= 0)
                return *why = 4, -1;
        row = rows[windows - 1];
        const struct compositor_frame *frame = &frames[windows - 1];

        if (compositor.table->glass[row])
                return *why = 5, -1;
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
        if (bottom - top < 64 * compositor.scale)
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
        unsigned char rows[WINDOWS];
        unsigned int count = 0, windows;

        if (!compositor.damages)
                return;

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
        // A window the card shows straight has what the tabs covered put back, so the glass is made from the window.
        compositor_direct_unpatch();
        compositor_glass_pass(frames, rows, windows);

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

        // The close buttons' means, now the glass and the band are made: where a change reaches a box.
        for (unsigned int at = 0; at < windows; at++)
        {
                compositor_close_mean(&frames[at]);
                compositor_close_ready(&frames[at]);
        }
        compositor_corner_masks_ready(frames, count);

        // A window that fills the output alone is shown straight from its buffer, and nothing is composed.
        int direct = compositor_direct_candidate(frames, rows, windows);

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
        int back = compositor.on_screen && compositor.on_screen != compositor.fb;

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
        int planed[4], plane = !back ? compositor_plane_candidate(frames, rows, windows, planed) : -1;

        if (compositor.plane_row >= 0 && (plane != compositor.plane_row ||
                                          memory_compare(planed, compositor.plane_rect, sizeof(planed)) != 0))
        {
                // Off the plane, or moved on it: the card is to have it no more, and the compositor composes it.
                if (compositor.flip_pending)
                        return;
                compositor_damage(compositor.plane_rect[0], compositor.plane_rect[1],
                                  compositor.plane_rect[2] - compositor.plane_rect[0],
                                  compositor.plane_rect[3] - compositor.plane_rect[1]);
                if (plane < 0 && !compositor_atomic_to(compositor.fb, -1, 0, 1, 0, 0, 0))
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

        int apart = compositor_changed_ready();
        struct compositor_apart slices = {compositor_slice_flush, compositor.composing, (unsigned long)compositor.width};
        unsigned int clips = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *rectangle = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rectangle[0], rectangle[1], rectangle[2], rectangle[3]};
                int rows[2];

                compositor_scene_banded(&target, frames, count, apart ? &slices : 0, rows);

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

        // No clips at all would tell the card everything changed.
        dirty.clip_count = clips;
        if (clips)
                system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
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
static void compositor_cursor_move(void);
static void compositor_arrange_on(int row, unsigned int how, int on);
static void compositor_place(int row, int x, int y);
static void compositor_room(const int *screen, int *room);
static void compositor_arrange_rect(int row, unsigned int how, const int *rect);
static void compositor_room_follow(void);

/*
        What the card has connected: each connector that says it is connected
        and lists a mode, in the card's order, with its name, the crtc that
        drives it now and the mode that crtc is at (Canvas's: current), or the
        crtcs it could have and its first mode. GETCONNECTOR, asked by the
        card's master, probes the connector again, which is what a hotplug
        is to be answered by.
*/
#define COMPOSITOR_CONNECTORS 16
#define COMPOSITOR_MODE_USERDEF 32u // DRM_MODE_TYPE_USERDEF

struct compositor_connected
{
        unsigned int count, crtc_count, max_width, max_height;
        unsigned int crtcs[COMPOSITOR_CONNECTORS];
        unsigned int connector[WINDOWS_OUTPUTS], crtc[WINDOWS_OUTPUTS], possible[WINDOWS_OUTPUTS];
        int current[WINDOWS_OUTPUTS];
        struct compositor_mode mode[WINDOWS_OUTPUTS];
        char name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        int size[WINDOWS_OUTPUTS][2];
};

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
        if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EINVAL;
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
                if (!found->current[n] && !listed)
                        continue;
                found->size[n][0] = found->mode[n].hdisplay;
                found->size[n][1] = found->mode[n].vdisplay;
                if (found->size[n][0] && found->size[n][1])
                        found->count++;
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
                        found->size[kept][0] = found->size[n][0], found->size[kept][1] = found->size[n][1];
                }
                kept++;
        }
        found->count = kept;
        return 0;
}

// The connected screens laid by the arrangement, but for those in dropped (a bit each), which are dark: what
// windows_arrange_lay answers, in the found list's order.
static unsigned int compositor_space_lay(const struct compositor_connected *found, unsigned int dropped,
                                         int (*rects)[4], unsigned char *order)
{
        char names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME], text[512];
        int sizes[WINDOWS_OUTPUTS][2], some[WINDOWS_OUTPUTS][4];
        unsigned char map[WINDOWS_OUTPUTS], laid[WINDOWS_OUTPUTS];
        unsigned int count = 0, lit;

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

        compositor_call(syscall(munmap), (positive)pixels, bytes, 0, 0, 0, 0);
        system_control(compositor.card, COMPOSITOR_RMFB, address_of fb);
        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
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

        compositor.drag = compositor.resize = COMPOSITOR_NONE;
        for (int row = -1; row < WINDOWS; row++)
        {
                int x = row < 0 ? compositor.pointer_x : compositor.x[row], y = row < 0 ? compositor.pointer_y : compositor.y[row];
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
                        compositor_screen_carry(was[from], now[to], &compositor.pointer_x, &compositor.pointer_y, 1, 1);
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
        compositor_screen_keep_point(now, now_count, nowhere, nowhere, &compositor.pointer_x, &compositor.pointer_y);
        compositor.kept_x = compositor.pointer_x;
        compositor.kept_y = compositor.pointer_y;
        // A window on a screen that stayed whose band moved (the primary is another now) follows its room.
        compositor_room_follow();
}

static void compositor_table_outputs(void);
static void compositor_band_room(void);

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
        unsigned int was_crtc[WINDOWS_OUTPUTS], was_count = compositor.outputs, dropped = 0;
        unsigned char order[WINDOWS_OUTPUTS];
        b32 status = compositor_connected_read(&found);

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
                               compositor_mode_same(&compositor.output[at].mode, &found.mode[order[at]]);
                if (same)
                        return 0;

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

                // The screens, the primary first, each at its own mode.
                memory_zero(compositor.output, sizeof(compositor.output));
                for (unsigned int at = 0; at < lit; at++)
                {
                        unsigned int n = order[at];
                        positive refresh = found.mode[n].vrefresh >= 24 && found.mode[n].vrefresh <= 1000 ? found.mode[n].vrefresh : 60;

                        compositor.output[at].crtc = found.crtc[n];
                        compositor.output[at].connector = found.connector[n];
                        compositor.output[at].mode = found.mode[n];
                        memory_copy(compositor.output[at].name, found.name[n], WINDOWS_OUTPUT_NAME);
                        for (unsigned int index = 0; index < found.crtc_count; index++)
                                if (found.crtcs[index] == found.crtc[n])
                                        compositor.output[at].crtc_index = index;
                        memory_copy(compositor.screen[at], screens[at], sizeof(screens[at]));
                        most_w = screens[at][2] > most_w ? screens[at][2] : most_w;
                        most_h = screens[at][3] > most_h ? screens[at][3] : most_h;
                        // Frames go out at the fastest screen's rate.
                        period = !period || 1000000000ull / refresh < period ? 1000000000ull / refresh : period;
                }
                compositor.outputs = lit;
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
                        if (compositor.composing)
                                compositor_call(syscall(munmap), (positive)compositor.composing, compositor.composing_bytes, 0, 0, 0, 0);
                        if (compositor.mirror && compositor.mirror_bytes)
                                compositor_call(syscall(munmap), (positive)compositor.mirror, compositor.mirror_bytes, 0, 0, 0, 0);
                        compositor.composing = compositor.mirror = 0;
                        compositor.composing_bytes = compositor.mirror_bytes = 0;
                        compositor.changed = 0;
                        compositor.scanout = pixels;
                        compositor.scanout_bytes = bytes;
                        compositor.scanout_pitch = pitch;
                        compositor.scanout_handle = handle;
                        compositor.fb = fb;
                        compositor.width = box[2];
                        compositor.height = box[3];
                }
                if (!first)
                {
                        // The card shows the compositor's own scanout from here on, on every crtc.
                        compositor_overlay_off();
                        compositor.direct_row = compositor.plane_row = -1;
                        compositor.plane_fb = 0;
                        compositor.on_screen = compositor.fb;
                        compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])was_names, was_count);
                        compositor_band_room();
                        compositor_tabs_changed();
                }
                compositor.mirror_stale = 1;
                compositor_damage(0, 0, compositor.width, compositor.height);
                compositor_present();

                for (unsigned int at = 0; at < lit; at++)
                {
                        struct compositor_crtc set = {(unsigned long)address_of compositor.output[at].connector, 1,
                                                      compositor.output[at].crtc, compositor.fb, (unsigned int)screens[at][0],
                                                      (unsigned int)screens[at][1], 0, 1, compositor.output[at].mode};

                        if (system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) != 0)
                                refused |= 1u << order[at];
                }
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
        int base = compositor.cursor.size ? compositor.cursor.size : compositor.scale;

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

static void compositor_cursor_move(void)
{
        int scale = compositor_cursor_scale();
        unsigned int shape = compositor.cursor.shape;

        if (compositor_cursor_in_scene())
        {
                // A plane that was showing the pointer shows nothing now, and says so again when it is wanted.
                if (!compositor.cursor.soft && compositor.cursor.plane_shape >= 0)
                        compositor_cursor_planes_off();
                if (compositor.cursor.drawn)
                        compositor_cursor_cell_damage(compositor.cursor.x, compositor.cursor.y, compositor.cursor.drawn_shape,
                                                      compositor.cursor.drawn_scale);
                compositor.cursor.x = compositor.pointer_x;
                compositor.cursor.y = compositor.pointer_y;
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

                compositor_cursor_cell(&left, &top, compositor.pointer_x, compositor.pointer_y, shape, fitted);

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
                struct compositor_crtc on = {(unsigned long)address_of output->connector, 1, output->crtc, compositor.fb,
                                             (unsigned int)compositor.screen[at][0], (unsigned int)compositor.screen[at][1],
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

// The layout the machine keeps, which `moonwater keyboard` writes; a machine that has not kept one has the US.
static void compositor_layout_read(void)
{
        char name[8] = {0};
        bipolar handle = system_open_at(AT_FDCWD, (string_address) "/root/keyboard", FILE_READ | O_CLOEXEC);
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
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
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
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
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
        char position[16], display[16], tune[512], text[16];
        int was[WINDOWS_OUTPUTS][4], now[WINDOWS_OUTPUTS][4];
        long edge;

        compositor_word(SETTING_TASKBAR_POSITION_PATH, position, sizeof(position));
        compositor_word(SETTING_TASKBAR_DISPLAY_PATH, display, sizeof(display));
        compositor_text(TASKBAR_TUNE_PATH, tune, sizeof(tune));
        compositor_word(SETTING_TASKBAR_TEXT_PATH, text, sizeof(text));
        compositor_setting_kept(SETTING_TASKBAR_TEXT_PATH, text, &edge);
        compositor_bands_where(was);
        compositor_tabs_changed();
        taskbar_settings_set(position, display, tune);
        taskbar_text_set(edge == 1);
        compositor_band_room();
        compositor_tabs_changed();
        compositor_bands_where(now);
        if (memory_compare(was, now, sizeof(was)))
                compositor_room_follow();
}

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

        if (!keyboard && !mouse && !tablet && !wakes)
        {
                system_close(handle);
                return;
        }

        compositor.absolute_x[at][0] = compositor.absolute_x[at][1] = 0;
        compositor.absolute_y[at][0] = compositor.absolute_y[at][1] = 0;
        compositor.held[at] = 0;
        compositor.wakeonly[at] = (unsigned char)(!keyboard && !mouse && !tablet);
        // A wheel that reports in 120ths says so, and its detents are then not counted twice.
        compositor.hires[at] = (unsigned char)((relative >> COMPOSITOR_REL_WHEEL_HI & 1) |
                                               (relative >> COMPOSITOR_REL_HWHEEL_HI & 1) << 1);
        // What it is, for moonwater debug: a device that is not what this took it for says so there.
        {
                unsigned int properties = 0, kinds = (keyboard ? WINDOWS_INPUT_KEYBOARD : 0) |
                                                     (mouse ? WINDOWS_INPUT_MOUSE : 0) |
                                                     (tablet ? WINDOWS_INPUT_TABLET : 0) |
                                                     (compositor.wakeonly[at] ? WINDOWS_INPUT_WAKES : 0);

                system_control(handle, COMPOSITOR_EVIOCGPROP(4), address_of properties);
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

                        if (compositor_setting_file(name) || string_equals((string_address)name, (string_address)TASKBAR_TUNE_NAME))
                        {
                                compositor_look_read();
                                compositor_input_settings_read();
                                // The arrangement: the screens laid again, which probes them, so only when it is the file written.
                                if (string_equals((string_address)name,
                                                  (string_address)SETTING_CANVAS_ARRANGE_PATH + sizeof("/root/") - 1))
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
*/
static int compositor_post(int row, unsigned int type, unsigned int flags, int a, int b)
{
        struct windows *table = compositor.table;
        unsigned int head, used, last, at;

        if (row < 0)
                return 0;

        head = table->event_head[row];
        used = head - __atomic_load_n(&table->event_tail[row], __ATOMIC_ACQUIRE);
        last = (head - 1) % WINDOWS_EVENTS;
        compositor.woken |= 1u << row;

        if (type == WINDOW_EVENT_MOTION && used && used <= WINDOWS_EVENTS &&
            table->event_type[row][last] == WINDOW_EVENT_MOTION)
        {
                table->event_a[row][last] = a;
                table->event_b[row][last] = b;
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

        if (was == row)
                return;

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

// The topmost window there is to give the keys to; none that is put away.
static void compositor_refocus(void)
{
        int next = COMPOSITOR_NONE;

        for (unsigned int at = compositor.stacked; at-- && next < 0;)
                if (compositor_visible(compositor.stack[at]))
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

// Whether a point is in the band at a window's edges that resizes it -- just inside its contents, its line,
// and past it -- and which edges.
static unsigned int compositor_edge_zone(int row, int x, int y)
{
        int grip = COMPOSITOR_GRIP * compositor.scale + compositor_line(row);
        int in = COMPOSITOR_GRIP_IN * compositor.scale;
        int x2 = compositor.x[row] + (int)compositor.w[row], y2 = compositor.y[row] + (int)compositor.h[row];
        unsigned int edges = 0;

        if (x < compositor.x[row] - grip || x >= x2 + grip || y < compositor.y[row] - grip || y >= y2 + grip)
                return 0;

        edges |= x < compositor.x[row] + in ? COMPOSITOR_EDGE_LEFT : x >= x2 - in ? COMPOSITOR_EDGE_RIGHT : 0;
        edges |= y < compositor.y[row] + in ? COMPOSITOR_EDGE_TOP : y >= y2 - in ? COMPOSITOR_EDGE_BOTTOM : 0;

        // A window that fills its screen or half of it has no edge on the screen's own: the pointer is held
        // there, and flung to the top to take the window it would start a resize instead.
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                unsigned int count;
                const int (*screens)[4] = compositor_screens(&count);
                const int *screen = screens[compositor_screen_of(row)];

                edges &= ~(unsigned int)(COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_BOTTOM);
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

        for (compositor.band_at = 0; compositor.band_at < bands && hit == TASKBAR_HIT_NONE; compositor.band_at++)
        {
                struct taskbar_view view = compositor_band_view();

                hit = taskbar_hit(&view, x, y);
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
                                      compositor_line(row), COMPOSITOR_GRAB * compositor.scale);
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

        width = width < WINDOW_MIN_WIDTH * compositor.scale ? WINDOW_MIN_WIDTH * compositor.scale
                : width > compositor_most_width()           ? compositor_most_width()
                                                            : width;
        height = height < WINDOW_MIN_HEIGHT * compositor.scale ? WINDOW_MIN_HEIGHT * compositor.scale
                 : height > compositor_most_height()           ? compositor_most_height()
                                                               : height;
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

static void compositor_resize_begin(int row, unsigned int edges)
{
        compositor.resize = row;
        compositor.resize_w = (int)compositor.w[row];
        compositor.resize_h = (int)compositor.h[row];
        compositor.press_x = compositor.pointer_x;
        compositor.press_y = compositor.pointer_y;
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.edges[row] = edges;
        compositor.far_x[row] = compositor.x[row] + (int)compositor.w[row];
        compositor.far_y[row] = compositor.y[row] + (int)compositor.h[row];
}

static void compositor_resize_move(void)
{
        int row = compositor.resize;
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
        Filling a screen, or half of it: a window is asked for the size the
        rectangle gives it and put where the rectangle begins, and the
        rectangle it had is kept, from the time it left floating and not at
        every arrangement, so snapping left, then right, then dragging off
        puts it back where it was and not on the left half. The edge that is
        the screen's stays where it is whatever size comes back: the right
        half is held by its right. The screen is the one named, or with none
        (-1) the window's own (compositor_screen_of).
*/
static void compositor_arrange_on(int row, unsigned int how, int on)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int part[4], left, right;

        if (!compositor.showing[row])
                return;

        compositor_screen_part(screens[on >= 0 && (unsigned int)on < count ? on : compositor_screen_of(row)], how, part);
        left = part[0];
        right = part[0] + part[2];

        if (compositor.arranged[row] == COMPOSITOR_FLOATING)
        {
                compositor.saved_x[row] = compositor.x[row];
                compositor.saved_y[row] = compositor.y[row];
                compositor.saved_w[row] = compositor.w[row];
                compositor.saved_h[row] = compositor.h[row];
        }
        // Its line goes with it: the outline it had is damaged before it is a filling one.
        compositor_damage_row(row);
        compositor.arranged[row] = how;
        compositor.edges[row] = how == COMPOSITOR_RIGHT ? COMPOSITOR_EDGE_LEFT : 0;
        compositor.far_x[row] = right;
        compositor_put(row, left, part[1]);
        compositor_damage_row(row);
        compositor_want(row, right - left, part[3]);
}

static void compositor_arrange(int row, unsigned int how)
{
        compositor_arrange_on(row, how, -1);
}

// Back to the rectangle it had, wherever it was put since.
static void compositor_float(int row)
{
        if (compositor.arranged[row] == COMPOSITOR_FLOATING)
                return;

        compositor_damage_row(row);
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.edges[row] = 0;
        compositor_put(row, compositor.saved_x[row], compositor.saved_y[row]);
        compositor_damage_row(row);
        compositor_want(row, (int)compositor.saved_w[row], (int)compositor.saved_h[row]);
}

/*
        Taking an arranged window takes the window it was: back to the size
        it had, under the hand where the hand had it, at the same share of
        its width and height, so taking the right side does not make the
        window jump until its left is under the pointer.
*/
static void compositor_float_for_drag(int row)
{
        int least_w = WINDOW_MIN_WIDTH * compositor.scale, least_h = WINDOW_MIN_HEIGHT * compositor.scale;
        int width = (int)compositor.saved_w[row] < least_w ? least_w : (int)compositor.saved_w[row];
        int height = (int)compositor.saved_h[row] < least_h ? least_h : (int)compositor.saved_h[row];
        int grab = (int)((long)(compositor.pointer_x - compositor.x[row]) * width /
                         (compositor.w[row] ? (int)compositor.w[row] : 1));
        int at = (int)((long)(compositor.pointer_y - compositor.y[row]) * height /
                       (compositor.h[row] ? (int)compositor.h[row] : 1));

        compositor_damage_row(row);
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.edges[row] = 0;
        compositor_put(row, compositor.pointer_x - grab, compositor.pointer_y - at);
        compositor_damage_row(row);
        compositor_want(row, width, height);
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
        int step = COMPOSITOR_STEP * compositor.scale;
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

/*
        Filling a rectangle, as compositor_arrange fills the output or half of it, for what a placement fills: the
        room of a screen, or all of one. The rectangle it had floating is kept, as there.
*/
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
        compositor_damage_row(row);
        compositor.arranged[row] = how;
        compositor.edges[row] = 0;
        compositor.far_x[row] = rect[0] + rect[2];
        compositor_put(row, rect[0], rect[1]);
        compositor_damage_row(row);
        compositor_want(row, rect[2], rect[3]);
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
        int least_w = WINDOW_MIN_WIDTH * compositor.scale, least_h = WINDOW_MIN_HEIGHT * compositor.scale, found, x, y;
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
        int least_w = WINDOW_MIN_WIDTH * compositor.scale, least_h = WINDOW_MIN_HEIGHT * compositor.scale;
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
        int least_w = WINDOW_MIN_WIDTH * compositor.scale, least_h = WINDOW_MIN_HEIGHT * compositor.scale;
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
                compositor.asks[row] = table->glass[row] & (WINDOW_NO_CLOSE | WINDOW_DISMISS);
                compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
                compositor.showing[row] = 1;
                compositor.made[row] = ++compositor.next_made;
                compositor_first_put(row, x, y);
                compositor_raise(row);
                if (compositor_visible(row))
                        compositor_focus(row);
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
        // window's own again. One of another size while an ask is out is shown at the size asked, stretched.
        if (compositor.asking[row] && width == table->want_width[row] && height == table->want_height[row])
        {
                compositor.asking[row] = 0;
                if (compositor.resize != row)
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
        if (compositor.keys_to == row)
        {
                compositor.keys_to = COMPOSITOR_NONE;
                memory_zero(compositor.keys_down, sizeof(compositor.keys_down));
        }
        compositor.hover = compositor.hover == row ? COMPOSITOR_NONE : compositor.hover;
        compositor.told_row = compositor.told_row == row ? COMPOSITOR_NONE : compositor.told_row;
        compositor.grab = compositor.grab == row ? COMPOSITOR_NONE : compositor.grab;
        compositor.drag = compositor.drag == row ? COMPOSITOR_NONE : compositor.drag;
        compositor.press = compositor.press == row ? COMPOSITOR_NONE : compositor.press;
        compositor.resize = compositor.resize == row ? COMPOSITOR_NONE : compositor.resize;
        // The stack is one shorter, so an Alt-Tab under way ends here, on the window it stopped on, as letting go of
        // Alt would have ended it: that window is brought back if it was put away, and has the keys.
        if (compositor.cycle)
        {
                compositor.cycle = 0;
                if (compositor.focus >= 0 && compositor.focus != row)
                        compositor_reveal(compositor.focus);
        }

        compositor.showing[row] = compositor.seen[row] = compositor.state[row] = 0;
        compositor.w[row] = compositor.h[row] = compositor.edges[row] = 0;
        compositor.picture_w[row] = compositor.picture_h[row] = compositor.asking[row] = 0;
        compositor.asks[row] = 0;
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.title[row][0] = 0;
        compositor.name[row][0] = 0;
        compositor_glass_drop(row);
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
        which is the one that was put away, so one Alt-Tab restores it.
*/
static void compositor_focus_step(void)
{
        int top = (int)compositor.stacked, focused = 0, below, next;

        for (int at = 0; at < (int)compositor.stacked; at++)
                if (compositor.stack[at] == compositor.focus)
                        focused = at + 1;

        below = compositor.cycle ? compositor.cycle : focused && focused == top ? focused : top + 1;
        next = below > 1 ? below - 1 : top;

        if (!next || next == focused)
                return;

        compositor.cycle = next;
        compositor_focus(compositor.stack[next - 1]);
}

static void compositor_focus_commit(void)
{
        compositor.cycle = 0;
        compositor_reveal(compositor.focus);
}

// Alt-F9: the window with the keys is put away, and the keys go to the one under it.
static void compositor_minimize_focused(void)
{
        int row = compositor.focus;

        if (row < 0 || !compositor_visible(row))
                return;

        compositor_damage_row(row);
        compositor.state[row] |= WINDOW_MINIMIZED;
        compositor.table->state[row] = compositor.state[row];
        compositor_refocus();
        compositor_tabs_changed();
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
static void compositor_door(int row, int returned)
{
        char bytes[64];
        p8 name[48];

        // Any program may open a door and write to it, so what is read in one turn is bounded: one that never stops
        // writing is not the whole of the compositor's time.
        for (int reads = 0; reads < 64 && system_read_once(compositor.door[row], bytes, sizeof(bytes)) > 0; reads++)
                ;

        compositor_take(row);
        if (!(returned & COMPOSITOR_POLLHUP))
                return;

        windows_door_name((char *)name, (unsigned int)row, 0);
        system_close(compositor.door[row]);
        compositor.door[row] = (int)system_open_at(AT_FDCWD, name, FILE_READ | O_NONBLOCK | O_CLOEXEC);
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
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_KEYS_FILE, FILE_READ | O_CLOEXEC);
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

// A chord pressed: said to the script, which runs what it is bound to; a script that does not listen hears nothing.
static void compositor_key_fire(unsigned int which)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_KEYS_FIRE, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        char line[34];
        positive length = string_length((string_address)compositor_keys.chord[which]);

        if (handle < 0)
        {
                compositor_kmsg((string_address) "compositor: nothing reads /run/moonwater/keys.fire, error ", (positive)-handle,
                                (string_address) "");
                return;
        }
        memory_copy(line, compositor_keys.chord[which], length);
        line[length] = '\n';
        system_write_once(handle, line, length + 1);
        system_close(handle);
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
static void compositor_key(unsigned int at, unsigned int code, int value, positive reported)
{
        unsigned int bit = compositor_held_bit(code), characters[2] = {0, 0}, flags, modifiers, chord;
        int count;

        if (bit)
        {
                if (value)
                        compositor.held[at] |= (unsigned char)bit;
                else
                        compositor.held[at] &= (unsigned char)~bit;
        }

        modifiers = compositor_modifiers();
        chord = modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT);

        if (bit & (CK_HELD_ALT | CK_HELD_ALTGR))
        {
                if (!(modifiers & WINDOW_KEY_ALT) && compositor.cycle)
                        compositor_focus_commit();
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

        if (((chord & ~WINDOW_KEY_SHIFT) == (WINDOW_KEY_ALT | WINDOW_KEY_CONTROL) && code == CK_KEY_BACKSPACE) ||
            ((modifiers & WINDOW_KEY_ALT) && compositor.cycle))
                return;

        count = compositor_keymap_event(&compositor.keymap, code, value, modifiers, characters);
        if (count < 0)
                return;

        if (compositor.focus < 0)
                compositor_refocus();
        if (compositor.focus < 0)
                return;

        // A press goes to the window that has the keys and is held there; a repeat or a
        // release of a key whose press went elsewhere (or nowhere) is nobody's to hear.
        if (code < 128)
        {
                unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[code >> 3];

                if (value == 1)
                {
                        if (compositor.keys_to != compositor.focus)
                                compositor_keys_release();
                        *down |= mask;
                        compositor.keys_to = compositor.focus;
                }
                else if (!(*down & mask))
                        return;
                else if (!value)
                        *down &= (unsigned char)~mask;
        }

        flags = modifiers | (value ? WINDOW_KEY_DOWN : 0) | (value == 2 ? WINDOW_KEY_REPEAT : 0);
        for (int each = 0; each < (count ? count : 1); each++)
                compositor_post(compositor.focus, WINDOW_EVENT_KEY, flags, (int)code, (int)characters[each]);
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

        compositor_screen_keep_point(screens, count, compositor.absolute ? nowhere : compositor.kept_x,
                                     compositor.absolute ? nowhere : compositor.kept_y, &compositor.pointer_x,
                                     &compositor.pointer_y);
        compositor.kept_x = compositor.pointer_x;
        compositor.kept_y = compositor.pointer_y;
        compositor.absolute = 0;
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
static void compositor_pointer_to(int row)
{
        compositor.told_row = row;
        compositor.told_x = compositor.pointer_x - compositor.x[row];
        compositor.told_y = compositor.pointer_y - compositor.y[row];
        compositor_post(row, WINDOW_EVENT_MOTION, 0, compositor.told_x, compositor.told_y);
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

        if (compositor.resize >= 0)
        {
                compositor_resize_move();
                return;
        }

        if (compositor.drag >= 0)
        {
                int row = compositor.drag;

                // Taken by the hand and moved: a window that fills a place is the window it was, under
                // the hand, from the first motion (a press that does not move it is half a double click).
                if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                {
                        compositor_float_for_drag(row);
                        compositor.drag_x = compositor.pointer_x - compositor.x[row];
                        compositor.drag_y = compositor.pointer_y - compositor.y[row];
                }

                // Kept on the output as it is moved, as when it was placed.
                compositor.drag_moved = 1;
                compositor_damage_row(row);
                compositor_place(row, compositor.pointer_x - compositor.drag_x, compositor.pointer_y - compositor.drag_y);
                compositor_damage_row(row);
                return;
        }

        // A held button keeps the pointer with the window it went down on. Otherwise a window is told of the motion
        // over it only when it wants the motion (windows_hover): each report of it wakes the program.
        compositor.hover = under;
        if (compositor.grab >= 0)
                compositor_pointer_to(compositor.grab);
        else if (under >= 0 && !__atomic_load_n(&compositor.table->hover_off[under], __ATOMIC_ACQUIRE))
                compositor_pointer_to(under);
}

// Where a drag ended, read as an arrangement: the pointer and not the window, because the pointer is what the
// hand aimed, on the screen under it and at its free edges (compositor_screen_snap), and which screen that is.
static unsigned int compositor_snap(int x, int y, int *screen)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        return compositor_screen_snap(screens, count, x, y, COMPOSITOR_SNAP * compositor.scale, screen);
}

// The button is let go. A window that was being moved is arranged by where the
// hand let go, unless the release is not the hand's (the device went away).
static void compositor_release(unsigned int code, int snap)
{
        int grab = compositor.grab, dragged = compositor.drag, to = grab, resized = compositor.resize;
        unsigned int index = code - COMPOSITOR_BTN_LEFT;

        compositor.drag = COMPOSITOR_NONE;
        compositor.grab = COMPOSITOR_NONE;
        compositor.resize = COMPOSITOR_NONE;
        // A button is let go in the window it was pressed in: a second button pressed over another window while the
        // first is held is that window's, and the first's release is the one that window held the grab for.
        if (index < 8 && (compositor.button_down >> index & 1))
        {
                to = compositor.button_row[index];
                compositor.button_down &= (unsigned char)~(1u << index);
        }
        compositor_post(to, WINDOW_EVENT_BUTTON, 0, (int)code, 0);
        // A press and a release with no motion between is a click on the window's top, which only raises it:
        // a window at the top of the output, where the snap margin is, would otherwise fill it when clicked.
        if (dragged >= 0 && snap && compositor.drag_moved)
        {
                int screen;
                unsigned int how = compositor_snap(compositor.pointer_x, compositor.pointer_y, &screen);

                if (how != COMPOSITOR_FLOATING)
                        compositor_arrange_on(dragged, how, screen);
        }
        // Where the hand put a window is where a window of its name opens next (compositor_remember).
        if (snap && (resized >= 0 || (dragged >= 0 && compositor.drag_moved)))
                compositor_remember(resized >= 0 ? resized : dragged);
}

static void compositor_button(unsigned int code, int down)
{
        int under, tab, alt = (compositor_modifiers() & WINDOW_KEY_ALT) != 0, top;
        positive now;

        compositor_pointer_clamp();
        under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);

        if (!down)
        {
                compositor_release(code, 1);
                return;
        }

        // A window that asked to be put away by a press anywhere but on it (WINDOW_DISMISS) is asked to go by one on
        // the desktop, a tab, another window or the tray alike; the press then goes on as it would have.
        if (compositor.focus >= 0 && (compositor.asks[compositor.focus] & WINDOW_DISMISS) && compositor.showing[compositor.focus] &&
            !(compositor.pointer_x >= compositor.x[compositor.focus] &&
              compositor.pointer_x < compositor.x[compositor.focus] + (int)compositor.w[compositor.focus] &&
              compositor.pointer_y >= compositor.y[compositor.focus] &&
              compositor.pointer_y < compositor.y[compositor.focus] + (int)compositor.h[compositor.focus]))
                compositor_close(compositor.focus);

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

                // Just outside a window's edges is the edge, to resize by. An edge resize makes
                // this ordinary geometry: a double click after it fills the screen and does not
                // restore what was.
                if (edges && code == COMPOSITOR_BTN_LEFT)
                {
                        compositor_resize_begin(under, edges);
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
                top = !alt && code == COMPOSITOR_BTN_LEFT && compositor.pointer_x >= compositor.x[under] &&
                      compositor.pointer_x < compositor.x[under] + (int)compositor.w[under] &&
                      compositor.pointer_y >= compositor.y[under] &&
                      compositor.pointer_y < compositor.y[under] + COMPOSITOR_GRAB * compositor.scale;

                if (top)
                {
                        struct compositor_frame frame;
                        int bx, by, side;

                        compositor_frame_of(under, &frame);
                        if (compositor_close_box(&frame, &bx, &by, &side) && compositor.pointer_x >= bx &&
                            compositor.pointer_x < bx + side && compositor.pointer_y >= by && compositor.pointer_y < by + side)
                        {
                                // Not one of a pair, or a second click would fill the screen on its way out.
                                compositor.press = COMPOSITOR_NONE;
                                compositor_close(under);
                                return;
                        }
                }

                if (alt || top)
                {
                        int again = under == compositor.press && now - compositor.press_ns <= COMPOSITOR_AGAIN_NS;

                        compositor.press = under;
                        compositor.press_ns = now;
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
                        compositor.drag = under;
                        compositor.drag_moved = 0;
                        compositor.drag_x = compositor.pointer_x - compositor.x[under];
                        compositor.drag_y = compositor.pointer_y - compositor.y[under];
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
        One report of a device is over: a mouse's counts since the last one are
        one movement of the hand, with the gain of its speed, and a tablet's
        place is where the report says on the output. Both axes of either are
        one movement: taken one at a time the cursor moved twice for it, the
        second time with the other axis a report out of date, and two axes
        timed each against the last gave left and right one speed and up and
        down another.
*/
static int compositor_report(positive now)
{
        int moved = 0;

        if (compositor.tablet_have)
        {
                if (compositor.tablet_have & 1)
                        compositor.pointer_x = compositor.tablet_x;
                if (compositor.tablet_have & 2)
                        compositor.pointer_y = compositor.tablet_y;
                compositor.tablet_have = 0;
                compositor.absolute = 1;
                moved = 1;
        }

        if (compositor.raw_x || compositor.raw_y)
        {
                int dx = compositor.raw_x, dy = compositor.raw_y, gain;

                compositor.raw_x = compositor.raw_y = 0;
                gain = compositor_gain_wanted(compositor_gain_of(dx, dy, now - compositor.stamp), compositor.flat,
                                              compositor.sensitivity);
                compositor.stamp = now;
                compositor.pointer_x += compositor_accel(dx, &compositor.accel_x, gain);
                compositor.pointer_y += compositor_accel(dy, &compositor.accel_y, gain);
                moved = 1;
        }

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
static void compositor_wheel(int v120, int horizontal)
{
        int row = compositor.grab;

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
        else if (compositor_post(row, WINDOW_EVENT_WHEEL, 0, v120, horizontal))
                compositor.table->wheel_sent++;
        else
                compositor.table->wheel_full++;
        compositor.table->wheel_row = row;
        compositor.table->wheel_x = row < 0 ? compositor.pointer_x : compositor.pointer_x - compositor.x[row];
        compositor.table->wheel_y = row < 0 ? compositor.pointer_y : compositor.pointer_y - compositor.y[row];
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
                        compositor.table->input_number[at] = -1;
                        compositor.devices &= ~(1ul << compositor.device_number[at]);

                        // What it held down will never be let go of: not a key, not a button.
                        compositor.held[at] = 0;
                        compositor_release(COMPOSITOR_BTN_LEFT, 0);
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

                        if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_X)
                        {
                                compositor.raw_x += event->value;
                                if (compositor_shaken(event->value, now))
                                        compositor.cursor.magnify = 1;
                        }
                        else if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_Y)
                                compositor.raw_y += event->value;
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
                                        compositor_wheel(fine ? event->value : event->value * 120, sideways);
                                }
                        }
                        else if (event->type == COMPOSITOR_EV_SYN && event->code == COMPOSITOR_SYN_DROPPED)
                                compositor.table->input_dropped[at]++;
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
                                moved |= compositor_report(now);
                        else if (event->type == COMPOSITOR_EV_KEY &&
                                 ((event->code >= COMPOSITOR_BTN_LEFT && event->code <= COMPOSITOR_BTN_TASK) ||
                                  event->code == COMPOSITOR_BTN_TOUCH))
                        {
                                // A touch is the left button; the others are where they are.
                                moved |= compositor_report(now);
                                if (moved)
                                        compositor_motion(), moved = 0;
                                compositor_button(event->code == COMPOSITOR_BTN_TOUCH ? COMPOSITOR_BTN_LEFT : event->code,
                                                  event->value != 0);
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
        // Every window's buffer as large as the largest window may be, a row of it stride pixels: for the session,
        // as the table is sealed at its size and mapped by every window.
        unsigned long buffer = ((unsigned long)compositor_stride() * (unsigned long)compositor_most_height() * 4 + 4095) &
                               ~4095ul;
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
        compositor.table->scale = (unsigned int)compositor.scale;
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

// The screens taken again (a screen came or went, the arrangement changed), and said in the log when they changed.
static void compositor_space_retake(void)
{
        unsigned int layout = compositor.layout;
        b32 status = compositor.card >= 0 ? compositor_space_take(0) : 0;

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
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (compositor_option(option, 't'))
                        taskbar_position_default(TASKBAR_TOP);
                else if (at + 1 < program_argument_count() && compositor_option(option, 's'))
                        compositor.scale = (int)compositor_number(program_argument(++at), 0);
                else if (at + 1 < program_argument_count() && compositor_option(option, 'f'))
                        font = program_argument(++at);
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
        if (compositor.scale < 1 || compositor.scale > SPARK_CANVAS_SCALE_MOST)
                compositor.scale = headless ? 1 : compositor_scale_read();
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
                compositor.pointer_x = compositor.kept_x = x + width / 2;
                compositor.pointer_y = compositor.kept_y = y + height / 2;
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
                struct compositor_poll wanted[4 + COMPOSITOR_INPUTS + WINDOWS];
                unsigned int count = 0, inputs, doors, card_at = 0, uevents_at = 0;
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

                if (compositor.band)
                {
                        timespec real = {0, 0};
                        positive minute;

                        clock_gettime(CLOCK_REALTIME, address_of real);
                        now = clock_monotonic_nanoseconds();
                        minute = now + (positive)(59 - real.tv_sec % 60) * 1000000000ull +
                                 (positive)(1000000000l - real.tv_nsec) + 1000000ull;
                        until = !until || minute < until ? minute : until;
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

                // A timespec: a wait past a second in its nanoseconds is EINVAL, and the loop would spin on it.
                wait[0] = until > now ? (long)((until - now) / 1000000000ull) : 0;
                wait[1] = until > now ? (long)((until - now) % 1000000000ull) : 0;
                awaited = compositor_call(syscall(ppoll), (positive)wanted, count,
                                          until ? (positive)wait : 0, 0, 0, 0);

                // Whatever woke the loop, a sleep may be what it woke from.
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

                if (card_at && wanted[card_at].returned)
                        compositor_card_read();

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

                if (compositor.band && taskbar_tick())
                        compositor_tabs_changed();

                // Dated when it starts: a card that holds the call for the rest
                // of the frame has used the frame up.
                if (compositor.damages)
                {
                        positive stamp = clock_monotonic_nanoseconds();

                        if (stamp - compositor.presented >= compositor.period)
                        {
                                compositor.presented = stamp;
                                compositor_present();
                                if (compositor.motion_ts)
                                {
                                        compositor_latency(1, compositor.motion_ts, clock_monotonic_nanoseconds());
                                        compositor.motion_ts = 0;
                                }
                        }
                }

                // One knock a window a pass, however much it was given.
                compositor_knock();
        }

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
