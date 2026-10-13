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
        and the machine script's key_ functions its chords (key_ctrl_shift_t, a
        terminal), the pointer is gained by the speed of the hand and
        wears a shape for the edge it is over, a window is arranged to fill
        the output or half of it by a double click on its top or with Alt
        held, or by a drag to an edge, Alt and the right button resize it
        from the corner nearest the hand, and a machine that slept has its
        screen drawn again.

        `surface` is a test client: a window of one colour, a line of text
        drawn with font.c when it is given a TrueType file, and a report on
        standard output of everything it is told.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_COMPOSITOR
#define MOONWATER_CANVAS_COMPOSITOR

/* ---- backend: the look on the CPU ---- */

// The palette, neutral greys and no hue: the desktop, the tabs drawn without a font (black, lighter while
// their window has the keys, a little off black while it is put away), and the black line round a window.
// With a font the tabs are glass, inked from what is under them.
#define COMPOSITOR_INK_DESKTOP 0x1e1e1eu
#define COMPOSITOR_INK_PANEL 0x141416u
#define COMPOSITOR_INK_BUTTON 0x000000u
#define COMPOSITOR_INK_BUTTON_LIT 0x38383cu
#define COMPOSITOR_INK_LINE 0x000000u
// The line of the window with the keys: white at 30%.
#define COMPOSITOR_INK_LINE_LIT 0x4d4d4du

// The panel's height, the radius of a window's corners and the width of the line round it, in logical
// pixels; and the largest radius in device pixels, which is what the corners' scratch is made for.
#define COMPOSITOR_PANEL 24
#define COMPOSITOR_RADIUS 8
#define COMPOSITOR_LINE 1
// The top of a window that is a titlebar with nothing drawn -- a press there takes the window whatever is
// under it -- and its close button, a disc that far in from the window's top right, in logical pixels.
#define COMPOSITOR_GRAB 24
#define COMPOSITOR_CLOSE 14
#define COMPOSITOR_CLOSE_IN 5
#define COMPOSITOR_RADIUS_MOST 64

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch; // pixels a row
        int width, height;
        int x1, y1, x2, y2;  // the damage: nothing outside it is written
        int ox, oy;          // the point pixels[0] is, on the output (a corner's scratch); 0 for the output
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
        int top;             // the panel is along the top of the output, its tabs hanging down
        const unsigned char *lengths; // the panel: each window's title in characters, which its tab fits
        int close;           // a window with a close button over its top right
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

/*
        The panel: tabs in a row in the middle of the band, with nothing
        between them -- a tab for each window and the tray -- each the head
        of a note whose rest is off the edge of the output: its title and its
        round corners show, three pixels in from the band's inner side, and
        it runs to the outer one. A window's tab is as wide as its title
        needs at a fixed width a character with an even margin all round,
        and never wider than the row has room for each nor eight bands, and
        the tray is as wide as the time's five characters, worked out from
        the title alone, without a font. The same slots are drawn and pressed
        on, so a window's tab is where it looks to be. Slot -2 is the tray
        and 0 up the windows; a window whose tab would be narrower than a
        pixel has none.
*/
#define COMPOSITOR_HIT_NONE (-3)
// A window's tab: lit while it has the keys, dim while it is put away.
#define COMPOSITOR_TAB_LIT 1u
#define COMPOSITOR_TAB_AWAY 2u

// A title's length in characters, the bytes that begin one, to its end or the listing's.
static unsigned char compositor_title_length(const char *title, unsigned int most)
{
        unsigned int count = 0;

        for (unsigned int at = 0; at < most && title[at]; at++)
                count += ((unsigned char)title[at] & 0xc0) != 0x80;
        return (unsigned char)(count > 255 ? 255 : count);
}

/*
        A tab's width for a run of characters: the run at the width a
        character of the panel's text has (its size is 11/20 of the tab's
        height and a character a little over half of that), and either side
        of it as much room as there is above and below a capital, so the
        title sits in the tab with the same margin all round. Never narrower
        than the tab is high.
*/
static int compositor_tab_text(const struct compositor_frame *frame, int characters)
{
        int high = frame->height - 3 * frame->scale;
        int wanted = (characters * high * 308 + high * 604) / 1000;

        return wanted < high ? high : wanted;
}

// A window's tab's width: its title's, up to the most it may be.
static int compositor_tab_width(const struct compositor_frame *frame, int which, int most)
{
        int wanted;

        if (!frame->lengths)
                return most;
        wanted = compositor_tab_text(frame, frame->lengths[which]);
        return wanted < most ? wanted : most;
}

static int compositor_panel_slot(const struct compositor_frame *frame, int which, int *x, int *y,
                                 int *width, int *height)
{
        int gap = 4 * frame->scale, inset = 3 * frame->scale, unit = frame->height;
        int tray = compositor_tab_text(frame, 5), count = (int)frame->mark_count;
        int room = frame->width - tray - (count + 2) * gap;
        int most = count && room > 0 ? room / count : 0, windows = 0, start;

        most = most > 8 * unit ? 8 * unit : most;
        for (int at = 0; most && at < count; at++)
                windows += compositor_tab_width(frame, at, most) + gap;
        start = frame->x + (frame->width - (windows + tray)) / 2;
        *y = frame->top ? frame->y : frame->y + inset;
        *height = unit - inset;

        if (which == -2)
        {
                *x = start + windows;
                *width = tray;
        }
        else
        {
                if (which < 0 || which >= count || most < 1)
                        return 0;

                *x = start;
                for (int at = 0; at < which; at++)
                        *x += compositor_tab_width(frame, at, most) + gap;
                *width = compositor_tab_width(frame, which, most);
        }

        // A band too narrow for a tab has none: nothing is drawn outside the band.
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
        One tab, its rectangle x, y, width, height: the window's lit while
        it has the keys and dim while it is put away, the tray for the time.
*/
static unsigned int compositor_tab_ink(int slot, unsigned int mark)
{
        return slot < 0                         ? COMPOSITOR_INK_BUTTON
               : mark & COMPOSITOR_TAB_AWAY  ? COMPOSITOR_INK_PANEL
               : mark & COMPOSITOR_TAB_LIT    ? COMPOSITOR_INK_BUTTON_LIT
                                                : COMPOSITOR_INK_BUTTON;
}

static void compositor_panel_tab(const struct compositor_target *t, int slot, unsigned int mark, int x, int y,
                                 int width, int height, int top)
{
        (void)top;
        compositor_fill(t, x, y, width, height, compositor_tab_ink(slot, mark));
}

// Every tab of a band, into the band's own pixels.
static void compositor_panel(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int x, y, width, height;

        for (int slot = -2; slot < (int)frame->mark_count; slot++)
                if (compositor_panel_slot(frame, slot, &x, &y, &width, &height))
                        compositor_panel_tab(t, slot, slot >= 0 ? frame->marks[slot] : 0, x, y, width, height, frame->top);
}


/*
        A window's corners are quarters of a circle: the line's outside at
        the radius, the contents' inside it by the line's width. A mask is
        the top left quarter, each pixel's share of its sixteen sample
        points inside the circle, made once for a radius and kept, one for
        the outside (which 0) and one for the inside (which 1); the other
        three corners read it mirrored.
*/
static unsigned char compositor_corner_masks[2][COMPOSITOR_RADIUS_MOST * COMPOSITOR_RADIUS_MOST];
static int compositor_corner_radii[2] = {-1, -1};

static const unsigned char *compositor_corner_mask(int radius, int which)
{
        if (compositor_corner_radii[which] != radius)
        {
                unsigned char *mask = compositor_corner_masks[which];

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
                compositor_corner_radii[which] = radius;
        }
        return compositor_corner_masks[which];
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
        The close button over a window's pixels: an X and nothing round it.
        Its strokes are the ink that stands out on what is under the button
        (window_contrast of the mean of the window's pixels there), and a
        halo a pixel and a half wider round them takes the window's own
        pixels further to the side of lightness they are on, as the tabs'
        glass does, so the X reads on a busy picture as on a plain ground;
        each pixel is the share of its sixteen sample points inside the
        strokes and inside the halo. What is under it is read from the
        window's buffer, which is known, and not from the target. Drawn last,
        after the round corners, through the target's clip, so the scene's
        pieces and its corners' scratch both have it.
*/
static void compositor_close_draw(const struct compositor_target *t, const struct compositor_frame *frame)
{
        int bx, by, side;
        unsigned long sum[3] = {0, 0, 0};

        if (!compositor_close_box(frame, &bx, &by, &side))
                return;

        for (int py = by; py < by + side; py++)
                for (int px = bx; px < bx + side; px++)
                {
                        unsigned int under = frame->pixels[(unsigned long)(py - frame->y) * frame->pitch + (unsigned long)(px - frame->x)];

                        sum[0] += under >> 16 & 255;
                        sum[1] += under >> 8 & 255;
                        sum[2] += under & 255;
                }

        unsigned long count = (unsigned long)side * (unsigned long)side;
        unsigned int mean = (unsigned int)(sum[0] / count << 16 | sum[1] / count << 8 | sum[2] / count);
        unsigned int ink = window_contrast(mean), tone = window_light(mean) ? 0xffffffu : 0x000000u;
        int x1 = bx > t->x1 ? bx : t->x1, y1 = by > t->y1 ? by : t->y1;
        int x2 = bx + side < t->x2 ? bx + side : t->x2, y2 = by + side < t->y2 ? by + side : t->y2;
        int arm = side * 2 / 7, half = frame->scale; // the X's reach from the middle, and half its stroke

        for (int py = y1; py < y2; py++)
                for (int px = x1; px < x2; px++)
                {
                        unsigned int halo = 0, cross = 0;

                        // In eighths of a pixel from the button's middle, sample centres odd.
                        for (int sy = 0; sy < 4; sy++)
                                for (int sx = 0; sx < 4; sx++)
                                {
                                        int dx = 8 * (px - bx) + 2 * sx + 1 - 4 * side, dy = 8 * (py - by) + 2 * sy + 1 - 4 * side;
                                        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                                        int d1 = dx - dy, d2 = dx + dy;

                                        d1 = d1 < 0 ? -d1 : d1;
                                        d2 = d2 < 0 ? -d2 : d2;
                                        cross += ax <= 8 * arm && ay <= 8 * arm && (d1 <= 11 * half || d2 <= 11 * half);
                                        halo += ax <= 8 * arm + 12 * half && ay <= 8 * arm + 12 * half &&
                                                (d1 <= 28 * half || d2 <= 28 * half);
                                }
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
                                     frame->x, frame->y, frame->width, frame->height, frame->top);
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

        const unsigned char *outer = compositor_corner_mask(r, 0), *inner = ri ? compositor_corner_mask(ri, 1) : 0;
        unsigned int tab = frame->builtin ? compositor_tab_ink(frame->slot, frame->slot >= 0 &&
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
        its tab is. Round on the side towards the middle of the output,
        square on the side that is off its edge. Answers how many.
*/
static unsigned int compositor_panel_tabs(const struct compositor_frame *band, struct compositor_frame *tabs)
{
        unsigned int count = 0;
        int x, y, width, height;

        for (int slot = 0; slot <= (int)band->mark_count; slot++)
        {
                int which = slot == (int)band->mark_count ? -2 : slot;

                if (!compositor_panel_slot(band, which, &x, &y, &width, &height))
                        continue;
                tabs[count] = *band;
                tabs[count].x = x;
                tabs[count].y = y;
                tabs[count].width = width;
                tabs[count].height = height;
                tabs[count].slot = which;
                tabs[count].radius = COMPOSITOR_RADIUS * band->scale;
                tabs[count].line = 0;
                tabs[count].square = band->top ? 1 | 2 : 4 | 8;
                if (!band->builtin)
                        tabs[count].pixels = band->pixels + (unsigned long)(y - band->y) * band->pitch + (unsigned long)(x - band->x);
                count++;
        }
        return count;
}

// The panel's band: along the bottom of the output or the top, as high as a tab and its inset. Its tabs lie
// over what fills the output, and take none of it.
static void compositor_band(int width, int height, int scale, int bottom, int *x, int *y,
                            int *band_width, int *band_height)
{
        *x = 0;
        *band_width = width;
        *band_height = COMPOSITOR_PANEL * scale;
        *y = bottom ? height - *band_height : 0;
}

// What a window fills: nothing, the output, or half of it.
#define COMPOSITOR_FLOATING 0
#define COMPOSITOR_MAXIMIZED 1
#define COMPOSITOR_LEFT 2
#define COMPOSITOR_RIGHT 3
#define COMPOSITOR_FRAMES (2 * WINDOWS + 2) // the windows, and a tab for each window and the tray

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

                                        for (; place && edge[place - 1] > box[piece][side]; place--)
                                                edge[place] = edge[place - 1];
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
                                        COMPOSITOR_INK_DESKTOP);
        }

        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_frame *f = &frames[at];
                int r = compositor_radius(f), fx, fy, fw, fh;

                compositor_outline(f, &fx, &fy, &fw, &fh);
                for (int corner = 0; r && corner < 4; corner++)
                {
                        int cx = corner & 1 ? fx + fw - r : fx, cy = corner & 2 ? fy + fh - r : fy;
                        struct compositor_target square = {compositor_corner_scratch, (unsigned long)r, t->width, t->height,
                                                           cx > t->x1 ? cx : t->x1, cy > t->y1 ? cy : t->y1,
                                                           cx + r < t->x2 ? cx + r : t->x2, cy + r < t->y2 ? cy + r : t->y2,
                                                           cx, cy};

                        if (square.x2 <= square.x1 || square.y2 <= square.y1)
                                continue;
                        compositor_fill(&square, cx, cy, r, r, COMPOSITOR_INK_DESKTOP);
                        for (unsigned int each = 0; each < count; each++)
                                compositor_draw(&square, &frames[each]);
                        compositor_blit(t, cx, cy, r, r, compositor_corner_scratch, (unsigned long)r);
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
                        for (unsigned int l = 0; l < CK_LEVELS; l++)
                                level[l] = patch[at].level[l];

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
#define COMPOSITOR_POLLERR 8
#define COMPOSITOR_POLLHUP 16

#define COMPOSITOR_INPUTS 16
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

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch; // pitch in bytes
        unsigned int fb, crtc;
        int width, height;
        int damage[COMPOSITOR_DAMAGE][4]; // x1, y1, x2, y2: what changed, apart
        unsigned int damages;

        int signals;
        int inputs[COMPOSITOR_INPUTS];
        int absolute_x[COMPOSITOR_INPUTS][2], absolute_y[COMPOSITOR_INPUTS][2];
        unsigned int input_count;

        int watch;           // inotify on /dev/input, -1 when there is none
        unsigned long devices; // the evdev numbers that are open
        int device_number[COMPOSITOR_INPUTS];

        int scale, bottom;   // device pixels a drawn one is; the panel's edge

        /*
                The windows: the table every program maps (window.c), each row's door and wake, and the
                compositor's own columns beside it -- what it took from each row once, checked, and what
                it decided. A row is a window everywhere: the stack, the keys, the hand are rows.
        */
        struct windows *table;
        unsigned long table_bytes, pixels, buffer; // its size, where the buffers are, one buffer: never the table's numbers
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
        unsigned char stack[WINDOWS];   // bottom first
        unsigned int stacked, next_made;

        // The tabs: the rows that have one in their order, what each shows, and the band drawn with the font.
        unsigned char listed[WINDOWS], marks[WINDOWS], lengths[WINDOWS];
        unsigned int listing;
        unsigned int *band;  // the band's pixels, or none: no font, and the tabs are shapes
        unsigned int *glass; // what is under the tabs, blurred: as wide as the band and its margins, as high
        unsigned long glass_bytes;
        int band_stale;

        /*
                The windows that are glass (compositor_glass_pass): what is under each, blurred and not yet tinted,
                and its picture over that, each cut at the window's own size; the size and place the backdrop was
                made for (a width of 0: never); what of it is to be blurred again and what composed again,
                x1, y1, x2, y2 on the output and empty when x2 is not past x1; which side of lightness its tint
                goes to, and the ground it told its program. The scratch the blur is made in is shared.
        */
        unsigned int glassy[WINDOWS];
        unsigned int *backdrop[WINDOWS], *tinted[WINDOWS], *composed[WINDOWS];
        int glass_blocks[WINDOWS][4]; // the first block kept of the small picture under it, across and down, and how many each way
        unsigned long glass_cut[WINDOWS];
        int glass_made[WINDOWS][4], stale[WINDOWS][4], dirty[WINDOWS][4];
        unsigned int glass_side[WINDOWS], glass_told[WINDOWS], lifted[WINDOWS];
        unsigned int *blur, *frost, *rise; // scratch: the tabs' small picture, a band of the scene halved, the levels risen
        unsigned long blur_bytes, frost_bytes, rise_bytes;
        positive blurred, recomposed; // pixels blurred again and composed again, all frames
        int damage_by; // what is being damaged: 0 the scene, a row and one its own picture, COMPOSITOR_OVER what lies over all
        unsigned int minute;
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
        unsigned char hires[COMPOSITOR_INPUTS]; // the device reports its wheel in 120ths
        unsigned char held[COMPOSITOR_INPUTS]; // the modifiers each keyboard holds down
        int layout_given; // -l named one, and the settings are not looked at
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


// Glass (compositor_frost): halved this many times, a block of the output a side, and the small blur's reach in blocks.
#define COMPOSITOR_GLASS_LEVELS 3
#define COMPOSITOR_GLASS_BLOCK (1 << COMPOSITOR_GLASS_LEVELS)
#define COMPOSITOR_GLASS_SPREAD 3 // the small blur's reach, in small pixels: two rounds of a box of four

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
        // How far a change reaches through the blur and the rise: the tent's blocks, and the rise's.
        int reach = (COMPOSITOR_GLASS_SPREAD + 3) * COMPOSITOR_GLASS_BLOCK;
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

static void compositor_damage(int x, int y, int width, int height)
{
        int rectangle[4] = {x < 0 ? 0 : x, y < 0 ? 0 : y,
                            x + width > compositor.width ? compositor.width : x + width,
                            y + height > compositor.height ? compositor.height : y + height};
        int joined[4];
        unsigned int at = 0;

        if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
                return;

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
        unsigned long stride = (unsigned long)compositor.width;

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
                                           (unsigned long)compositor.width, 0, null, 0,
                                           line ? COMPOSITOR_RADIUS * compositor.scale : 0, line,
                                           row == compositor.focus ? COMPOSITOR_INK_LINE_LIT : COMPOSITOR_INK_LINE};
        frame->close = 1;
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

// The windows that have a tab, in the order they first showed, and what each tab shows.
static unsigned int compositor_windows(void)
{
        unsigned int count = 0;

        for (int row = 0; row < WINDOWS; row++)
        {
                unsigned int place = count;

                if (!compositor.showing[row])
                        continue;
                for (; place && compositor.made[compositor.listed[place - 1]] > compositor.made[row]; place--)
                        compositor.listed[place] = compositor.listed[place - 1];
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

// The panel's band with its tabs: the band's own pixels when there is a font to write the titles with,
// and shapes the scene draws when there is not.
static void compositor_band_frame(struct compositor_frame *frame)
{
        int x, y, width, height;

        compositor_band(compositor.width, compositor.height, compositor.scale, compositor.bottom, &x, &y, &width,
                        &height);
        compositor_windows();
        *frame = (struct compositor_frame){x, y, width, height, compositor.scale, compositor.band,
                                           (unsigned long)width, !compositor.band, compositor.marks,
                                           compositor.listing};
        frame->top = !compositor.bottom;
        frame->lengths = compositor.lengths;
}

// What the tabs show changed: the windows, which has the keys, which are put away, a title, the minute.
static void compositor_tabs_changed(void)
{
        int x, y, width, height;

        compositor.band_stale = 1;
        compositor_band(compositor.width, compositor.height, compositor.scale, compositor.bottom, &x, &y, &width,
                        &height);
        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(x, y, width, height);
        compositor.damage_by = 0;
}

// One line of text in the middle of a tab, in the tab's own colour for ground, cut off at its edge.
static void compositor_band_text(int pitch, int x, int y, int width, int height, const char *text,
                                 unsigned int ink, unsigned int ground)
{
        const font_face address_to faces[1] = {&compositor.face};
        p32 em = (p32)(height * 11 / 20) << 6;
        font_metrics grid = font_metrics_at(&compositor.face, em);
        font_target here = {compositor.band + (long)y * pitch + x, (positive)pitch, width, height};
        unsigned int scratch[1] = {0};
        font_target nowhere = {scratch, 1, 1, 1};
        positive length = string_length((string_address)text);
        int pad;

        // How wide the run is, before it is put anywhere.
        pad = (width - (int)(font_draw(compositor.engine, faces, 1, (const p8 address_to)text, length, em, 0,
                                       &nowhere, 0, 0, ink, ground) >> 16)) / 2;
        font_draw(compositor.engine, faces, 1, (const p8 address_to)text, length, em, 0, &here,
                  (b32)(pad > 0 ? pad : 0) << 16, (height - grid.cell_height) / 2 + grid.baseline, ink, ground);
}

static unsigned int compositor_minute(void)
{
        timespec now = {0, 0};

        clock_gettime(CLOCK_REALTIME, address_of now);
        return (unsigned int)(now.tv_sec / 60);
}

/*
        The tabs are frosted glass. Under them is composed as the scene has
        it -- from the first tab to the last and the blur's reach each side,
        from the reach above the band to its far edge, the output's last row
        and column repeated past it -- and blurred by memory_average_ahead,
        a pass of the rounded mean of each byte and the byte ahead at a
        pixel, two, four and so to eight pixels a step across and four rows
        down (so many more at a larger scale): a box of sixteen by eight,
        which each tab reads from back where it reaches, eight pixels left
        and four up. Every pass is one call over the whole of it, nothing is
        read that a pass has written, and the work is the hardware's
        averaging instruction a register at a time. What it costs, besides
        that, is a little light: each pass rounds up.

        A tab is then its glass made more vivid (each colour half as far
        again from its grey) and taken further to the side of lightness it is
        on -- a dark ground darker, a light one lighter -- so the row of tabs
        is its ground's own and still stands off it, all of a piece: the tab
        of the window with the keys goes not quite as far, and is told by its
        title, the ink that stands out on what the tab came to
        (window_contrast) at full strength where the others' are two thirds
        of the way to it and a tab put away's under half. Over the desktop's
        grey that is a tab of 090909, and 0e0e0e with the keys.
*/
#define COMPOSITOR_GLASS_ACROSS 8 // half the blur's width, in logical pixels
#define COMPOSITOR_GLASS_DOWN 4   // and half its height

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

// Down to a multiple of the block, or by a power of two, for any sign.
static int compositor_floor_to(int x, int block)
{
        return x >= 0 ? x / block * block : -((-x + block - 1) / block * block);
}

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
        for (int row = 0; row < gh; row++)
                memory_zero(into + (long)row * gw, (positive)gw * 4);
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

// The glass: the scene under the span x0 to x1 of the band composed into it and blurred. Answers its width.
static int compositor_glass_make(const struct compositor_frame *windows, unsigned int count, int x0, int x1,
                                 const struct compositor_frame *band)
{
        int across = compositor_glass_reach(COMPOSITOR_GLASS_ACROSS), down = compositor_glass_reach(COMPOSITOR_GLASS_DOWN);
        int gw = x1 - x0 + 2 * across, gh = band->height + 2 * down;

        if ((unsigned long)gw * (unsigned long)gh * 4 > compositor.glass_bytes ||
            !compositor_blur(compositor.glass, windows, count, x0 - across, band->y - down, gw, gh, across, down))
                return 0;
        return gw;
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

/*
        One tab of glass, its rectangle x, y, width, height in the band, from
        the glass gw wide whose column 0 is the band's x0: made vivid and
        tinted toward its own side of lightness. Answers what it came to in its middle,
        which its title stands out from.
*/
static unsigned int compositor_glass_tab(int pitch, int gw, int x0, int x, int y, int width, int height, int top,
                                         int slot, unsigned int mark)
{
        unsigned int middle = compositor_vivid(compositor.glass[(long)(y + height / 2) * gw + x - x0 + width / 2]);
        unsigned int target = window_light(middle) ? 0xffffffu : 0x000000u;
        unsigned int amount = slot >= 0 && (mark & COMPOSITOR_TAB_LIT) ? 140 : 179;

        (void)top;
        for (int row = y; row < y + height; row++)
        {
                const unsigned int *from = compositor.glass + (long)row * gw + (x - x0);
                unsigned int *into = compositor.band + (long)row * pitch + x;

                for (int column = 0; column < width; column++)
                        into[column] = compositor_grained(window_mix(compositor_vivid(from[column]), target, amount),
                                                          target != 0, x + column, row);
        }
        return window_mix(middle, target, amount);
}

/*
        The band drawn over what is under it: the windows composed into it as
        the scene has them, blurred, each tab made glass, each window's title
        on its own and the time in the tray.
*/
static void compositor_band_draw(const struct compositor_frame *windows, unsigned int count)
{
        struct compositor_frame band;
        int x, y, width, height, glass_width = 0, glass_left = 0;
        timespec now = {0, 0};
        time_t seconds;
        tm broken;

        compositor.band_stale = 0;
        if (!compositor.band)
                return;

        compositor_band_frame(&band);
        band.x = band.y = 0;
        {
                int first, last, x, y, width, height;

                // The span of the tabs, which is all the glass is made under.
                if (!compositor_panel_slot(&band, -2, &x, &y, &width, &height))
                        return;
                last = x + width;
                first = band.mark_count && compositor_panel_slot(&band, 0, &x, &y, &width, &height) ? x : last - width;
                compositor_band_frame(&band);
                glass_width = compositor_glass_make(windows, count, first, last, &band);
                glass_left = first;
                band.x = band.y = 0;
        }
        if (!glass_width)
                return;

        for (unsigned int at = 0; at < band.mark_count; at++)
                if (compositor_panel_slot(&band, (int)at, &x, &y, &width, &height))
                {
                        unsigned int ground = compositor_glass_tab(band.width, glass_width, glass_left, x, y, width, height,
                                                                   band.top, (int)at, band.marks[at]);
                        unsigned int ink = window_contrast(ground);

                        compositor_band_text(band.width, x, y, width, height, compositor.title[compositor.listed[at]],
                                             band.marks[at] & COMPOSITOR_TAB_LIT    ? ink
                                             : band.marks[at] & COMPOSITOR_TAB_AWAY ? window_mix(ground, ink, 110)
                                                                                    : window_mix(ground, ink, 170),
                                             ground);
                }

        compositor.minute = compositor_minute();
        clock_gettime(CLOCK_REALTIME, address_of now);
        seconds = now.tv_sec;
        if (compositor_panel_slot(&band, -2, &x, &y, &width, &height) && localtime_r(&seconds, &broken))
        {
                char clock[6] = {(char)('0' + broken.tm_hour / 10), (char)('0' + broken.tm_hour % 10), ':',
                                 (char)('0' + broken.tm_min / 10), (char)('0' + broken.tm_min % 10), 0};
                unsigned int ground = compositor_glass_tab(band.width, glass_width, glass_left, x, y, width, height,
                                                           band.top, -2, 0);

                compositor_band_text(band.width, x, y, width, height, clock, window_contrast(ground), ground);
        }
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

static unsigned int compositor_glass_tint(unsigned int blurred, unsigned int side)
{
        unsigned int pixel = window_mix(compositor_vivid(blurred), side ? 0xffffffu : 0, COMPOSITOR_GLASS_TINT);
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
                                              picture + (unsigned long)(y - compositor.y[row]) * (unsigned long)compositor.width +
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
        Answers whether the side changed.
*/
static int compositor_glass_side(int row)
{
        unsigned long sum[3] = {0, 0, 0}, count = (unsigned long)compositor.glass_blocks[row][2] * compositor.glass_blocks[row][3];
        unsigned int was = compositor.glass_side[row], mean, light, ground;

        for (unsigned long at = 0; at < count; at++)
        {
                unsigned int pixel = compositor_vivid(compositor.backdrop[row][at]);

                sum[0] += pixel >> 16 & 255;
                sum[1] += pixel >> 8 & 255;
                sum[2] += pixel & 255;
        }
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
        compositor.backdrop[row] = compositor.tinted[row] = compositor.composed[row] = null;
        compositor.glass_cut[row] = 0;
        compositor.glassy[row] = 0;
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
        need = ((cells * 2 + pixels) * 4 + 4095) & ~4095ul;
        most = ((unsigned long)((compositor.width >> shift) + 2 * margin + 2) * (unsigned long)((compositor.height >> shift) + 2 * margin + 2) * 2 +
                (unsigned long)compositor.width * (unsigned long)compositor.height) * 4;
        most = (most + 4095) & ~4095ul;
        need = need > most ? need : most;

        if (need > compositor.glass_cut[row])
        {
                if (compositor.backdrop[row])
                        compositor_call(syscall(munmap), (positive)compositor.backdrop[row], compositor.glass_cut[row], 0, 0, 0, 0);
                mapped = compositor_call(syscall(mmap), 0, need, 3, 0x22, (positive)-1, 0);
                compositor.backdrop[row] = compositor.tinted[row] = compositor.composed[row] = null;
                compositor.glass_cut[row] = 0;
                if (mapped < 0 && mapped > -4096)
                        return 0;
                compositor.backdrop[row] = (unsigned int *)mapped;
                compositor.glass_cut[row] = need;
        }
        compositor.tinted[row] = compositor.backdrop[row] + cells;
        compositor.composed[row] = compositor.tinted[row] + cells;
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

                                                compositor.tinted[row][cell] =
                                                    compositor_glass_tint(compositor.backdrop[row][cell], compositor.glass_side[row]);
                                        }
                                compositor.blurred += (positive)(bx1 - bx0) * (positive)(by1 - by0);

                                // The pixels those blocks rise into: a block each way past them.
                                shown[0] = (bx0 - 1) * block, shown[1] = (by0 - 1) * block;
                                shown[2] = (bx1 + 1) * block, shown[3] = (by1 + 1) * block;
                                compositor_meet(shown, box);
                                compositor_union(compositor.dirty[row], shown);
                                if (compositor_glass_side(row))
                                {
                                        for (long cell = 0; cell < (long)blocks[2] * blocks[3]; cell++)
                                                compositor.tinted[row][cell] =
                                                    compositor_glass_tint(compositor.backdrop[row][cell], compositor.glass_side[row]);
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

        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor_visible(compositor.stack[at]))
                {
                        rows[count] = compositor.stack[at];
                        compositor_frame_of(compositor.stack[at], &frames[count++]);
                }
        windows = count;
        compositor_glass_pass(frames, rows, windows);

        // What is under the tabs changed, or what they say: the glass is made again over it, and shown.
        compositor_band_frame(&band);
        for (unsigned int at = 0; at < compositor.damages && compositor.band; at++)
                compositor.band_stale |= compositor.damage[at][1] < band.y + band.height && compositor.damage[at][3] > band.y;
        if (compositor.band_stale)
        {
                compositor_band_draw(frames, windows);
                compositor.damage_by = COMPOSITOR_OVER;
                compositor_damage(band.x, band.y, band.width, band.height);
                compositor.damage_by = 0;
        }

        compositor_band_frame(&band);
        count += compositor_panel_tabs(&band, &frames[count]);

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *rectangle = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rectangle[0], rectangle[1], rectangle[2], rectangle[3]};

                compositor_scene(&target, frames, count);

                if (compositor.cursor.soft && compositor.cursor.drawn)
                        compositor_cursor_draw(&target, compositor.cursor.x, compositor.cursor.y,
                                               compositor.cursor.drawn_shape, compositor.cursor.drawn_scale,
                                               0xffffffu, 0x000000u);

                for (unsigned int side = 0; side < 4; side++)
                        clip[at][side] = (unsigned short)rectangle[side];
                compositor.painted += (unsigned long)compositor_area(rectangle);
        }

        dirty.clip_count = compositor.damages;
        system_control(compositor.card, COMPOSITOR_DIRTYFB, address_of dirty);
        compositor.damages = 0;
}

/*
        The card, at the mode Canvas left on it.

        Taking Canvas's own mode is what keeps the switch a plane change and
        not a modeset, and what keeps every coordinate the same on both sides
        of it. A connector with no crtc driving it takes its first mode.
*/
static void compositor_kmsg(string_address text, positive value, string_address tail);

static b32 compositor_card_open(void)
{
        struct compositor_card_res resources = {0};
        unsigned int connectors[16], crtcs[16];
        struct compositor_mode mode;
        unsigned int connector = 0, crtc = 0, screens = 0;
        int have_mode = 0;

        compositor.card = (int)system_open_at(AT_FDCWD, (string_address) "/dev/dri/card0",
                                              FILE_READ_WRITE | O_CLOEXEC);
        if (compositor.card < 0)
                return compositor.card;

        if (system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0 ||
            system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EBUSY;

        {
                struct compositor_version version = {0};

                version.name_length = sizeof(compositor.driver) - 1;
                version.name = compositor.driver;
                if (system_control(compositor.card, COMPOSITOR_VERSION, address_of version) != 0)
                        compositor.driver[0] = 0;
        }

        if (resources.connector_count > 16 || resources.crtc_count > 16 || !resources.crtc_count)
                return -COMPOSITOR_EINVAL;

        resources.connectors = (unsigned long)connectors;
        resources.crtcs = (unsigned long)crtcs;
        resources.fb_count = resources.encoder_count = 0;
        if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                return -COMPOSITOR_EINVAL;

        for (unsigned int at = 0; at < resources.connector_count; at++)
        {
                struct compositor_connector probe = {.connector_id = connectors[at]};
                struct compositor_mode modes[64];
                struct compositor_encoder encoder = {0};
                struct compositor_crtc current = {0};

                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0 ||
                    probe.connection != 1 || !probe.mode_count)
                        continue;

                screens++;
                if (have_mode)
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

        // One screen is drawn on. The kernel desktop was one desktop across every screen of every
        // card; this is not yet, and says so where the machine's log has it.
        if (screens > 1)
                compositor_kmsg((string_address) "compositor: more than one screen is connected and this draws on one of them, screens: ",
                                screens, (string_address) "");

        compositor.width = mode.hdisplay;
        compositor.height = mode.vdisplay;
        compositor.crtc = crtc;
        compositor.mode = mode;
        compositor.connector = connector;
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

// A pointer's cell, in the scale and shape it is drawn: where it was last damaged and drawn.
static int compositor_cursor_scale(void)
{
        return compositor.cursor.magnified ? 3 * compositor.scale : compositor.scale;
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

// A shape painted into the buffer of the plane that is not showing, and the plane given it.
static int compositor_cursor_paint(unsigned int shape, int fitted, int left, int top)
{
        struct compositor_map_dumb map = {.handle = compositor.cursor.handle[compositor.cursor.back]};
        struct compositor_cursor cursor = {3, compositor.crtc, left, top, CP_PLANE, CP_PLANE, map.handle};
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
                                       shape, fitted, 0xffffffffu, 0xff000000u);
        }
        compositor_call(syscall(munmap), (positive)mapped, CP_PLANE * CP_PLANE * 4, 0, 0, 0, 0);

        if (system_control(compositor.card, COMPOSITOR_CURSOR, address_of cursor) != 0)
                return -1;

        compositor.cursor.back ^= 1;
        compositor.cursor.paints++;
        compositor.cursor.plane_shape = (int)shape;
        compositor.cursor.plane_scale = fitted;
        return 0;
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

        if (compositor.cursor.soft)
        {
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

        {
                int fitted = compositor_cursor_fit(scale), left, top;

                compositor_cursor_cell(&left, &top, compositor.pointer_x, compositor.pointer_y, shape, fitted);

                if ((int)shape != compositor.cursor.plane_shape || fitted != compositor.cursor.plane_scale)
                {
                        if (compositor_cursor_paint(shape, fitted, left, top) == 0)
                                return;

                        // The card will not take it: the pointer is drawn from here on.
                        {
                                struct compositor_cursor gone = {1, compositor.crtc, 0, 0, CP_PLANE, CP_PLANE, 0};

                                system_control(compositor.card, COMPOSITOR_CURSOR, address_of gone);
                        }
                        compositor.cursor.handle[0] = compositor.cursor.handle[1] = 0;
                        compositor.cursor.refused++;
                        compositor.cursor.soft = 1;
                        compositor_cursor_move();
                        return;
                }

                {
                        struct compositor_cursor move = {2, compositor.crtc, left, top, 0, 0, 0};

                        compositor.cursor.moves += system_control(compositor.card, COMPOSITOR_CURSOR, address_of move) == 0;
                }
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

        for (unsigned int at = 0; at < sizeof(guests) / sizeof(guests[0]); at++)
        {
                unsigned int same = 0;

                while (guests[at][same] && guests[at][same] == compositor.driver[same])
                        same++;
                if (!guests[at][same] && !compositor.driver[same])
                        return 1;
        }
        return 0;
}

// How far ahead of the clock that stops in a sleep the one that does not is, in nanoseconds.
static positive compositor_slept_clock(void)
{
        long boot[2] = {0, 0}, mono[2] = {0, 0};
        long long ahead;

        compositor_call(syscall(clock_gettime), 7, (positive)boot, 0, 0, 0, 0);
        compositor_call(syscall(clock_gettime), 1, (positive)mono, 0, 0, 0, 0);
        ahead = ((long long)boot[0] - mono[0]) * 1000000000ll + (boot[1] - mono[1]);
        return ahead < 0 ? 0 : (positive)ahead;
}

static void compositor_resume(int cycle)
{
        if (compositor.card < 0)
                return;

        if (cycle && !compositor_virtual())
        {
                struct compositor_crtc off = {0, 0, compositor.crtc, 0, 0, 0, 0, 0, {0}};
                struct compositor_crtc on = {(unsigned long)address_of compositor.connector, 1, compositor.crtc,
                                             compositor.fb, 0, 0, 0, 1, compositor.mode};

                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of off);
                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of on);
        }

        // What the plane wears and where it was is whatever the driver put back: say it all again.
        compositor.cursor.plane_shape = -1;
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

// The layout the machine keeps, which `moonwater keyboard` writes: the setting itself, and
// failing that the one Canvas was told, which a machine that has not kept one still has.
static void compositor_layout_read(void)
{
        struct canvas_control control = {0};
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

        if (layout < 0)
        {
                handle = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
                if (handle >= 0)
                {
                        control.request = SPARK_CANVAS_LAYOUT;
                        if (system_control(handle, SPARK_IOCTL_CANVAS, address_of control) >= 0)
                                layout = compositor_layout_index(control.master_command);
                        system_close(handle);
                }
        }

        if (layout >= 0 && layout != compositor.keymap.layout)
        {
                compositor.keymap.layout = (unsigned char)layout;
                compositor.keymap.dead = 0;
        }
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
        compositor.hires[at] = (unsigned char)(relative >> COMPOSITOR_REL_WHEEL_HI & 1);
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
        compositor.devices |= 1ul << number;
        compositor.input_count = at + 1 > compositor.input_count ? at + 1 : compositor.input_count;
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
                unsigned int buffer[64];
                long got = compositor_call(syscall(read), (positive)compositor.watch, (positive)buffer,
                                           sizeof(buffer), 0, 0, 0);

                for (long at = 0; at + 16 <= got;)
                {
                        const char *name = (const char *)(buffer + (at + 16) / 4);
                        unsigned int length = buffer[(at + 12) / 4], number = 0;

                        if (at + 16 + (long)length > got)
                                break;

                        if (length > 8 && !string_compare_max((string_address)name, (string_address) "keyboard", 8) && !name[8] &&
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
        is long.
*/
static void compositor_post(int row, unsigned int type, unsigned int flags, int a, int b)
{
        struct windows *table = compositor.table;
        unsigned int head, used, last, at;

        if (row < 0)
                return;

        head = table->event_head[row];
        used = head - __atomic_load_n(&table->event_tail[row], __ATOMIC_ACQUIRE);
        last = (head - 1) % WINDOWS_EVENTS;
        compositor.woken |= 1u << row;

        if (type == WINDOW_EVENT_MOTION && used && used <= WINDOWS_EVENTS &&
            table->event_type[row][last] == WINDOW_EVENT_MOTION)
        {
                table->event_a[row][last] = a;
                table->event_b[row][last] = b;
                return;
        }

        if (used >= WINDOWS_EVENTS)
                return;

        at = head % WINDOWS_EVENTS;
        table->event_type[row][at] = (unsigned short)type;
        table->event_flags[row][at] = (unsigned short)flags;
        table->event_a[row][at] = a;
        table->event_b[row][at] = b;
        __atomic_store_n(&table->event_head[row], head + 1, __ATOMIC_RELEASE);
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

        for (; at + 1 < compositor.stacked; at++)
                compositor.stack[at] = compositor.stack[at + 1];
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

        // A window that fills the output or half of it has no edge on the output's own: the pointer is held
        // there, and flung to the top to take the window it would start a resize instead.
        if (compositor.arranged[row] != COMPOSITOR_FLOATING)
        {
                edges &= ~(unsigned int)(COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_BOTTOM);
                edges &= compositor.x[row] <= 0 ? ~(unsigned int)COMPOSITOR_EDGE_LEFT : ~0u;
                edges &= x2 >= compositor.width ? ~(unsigned int)COMPOSITOR_EDGE_RIGHT : ~0u;
        }
        return edges;
}

/*
        What is under a point: a tab, which tab is set to (COMPOSITOR_HIT_NONE
        when it is none), and otherwise the topmost window whose contents or
        grip, just outside its edges, hold it. Between the tabs is whatever is
        below them.
*/
static int compositor_under(int x, int y, int *tab)
{
        struct compositor_frame band;

        compositor_band_frame(&band);
        *tab = compositor_panel_hit(&band, x, y);
        if (*tab != COMPOSITOR_HIT_NONE)
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

// Kept on the output: all of it when it fits, its top left when it does not, so a person can always reach it.
static void compositor_place(int row, int x, int y)
{
        int line = compositor_line(row);
        int high_x = compositor.width - (int)compositor.w[row] - line;
        int high_y = compositor.height - (int)compositor.h[row] - line;

        x = x > high_x ? high_x : x;
        y = y > high_y ? high_y : y;
        x = x < line ? line : x;
        y = y < line ? line : y;
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
                : width > compositor.width                  ? compositor.width
                                                            : width;
        height = height < WINDOW_MIN_HEIGHT * compositor.scale ? WINDOW_MIN_HEIGHT * compositor.scale
                 : height > compositor.height                  ? compositor.height
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
        int max_w = compositor.width - 2 * compositor_line(row);
        int max_h = compositor.height - 2 * compositor_line(row);

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
        Filling the output, or half of it: a window is asked for the size
        the rectangle gives it and put where the rectangle begins, and the
        rectangle it had is kept, from the time it left floating and not at
        every arrangement, so snapping left, then right, then dragging off
        puts it back where it was and not on the left half. The edge that is
        the output's stays where it is whatever size comes back: the right
        half is held by its right.
*/
static void compositor_arrange(int row, unsigned int how)
{
        int half = compositor.width / 2;
        int left = how == COMPOSITOR_RIGHT ? half : 0;
        int right = how == COMPOSITOR_LEFT ? half : compositor.width;

        if (!compositor.showing[row])
                return;

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
        compositor_put(row, left, 0);
        compositor_damage_row(row);
        compositor_want(row, right - left, compositor.height);
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

        for (int at = 0; at < 8; at++)
        {
                int taken = 0, tx = *x + at * step, ty = *y + at * step;

                if (tx + (int)compositor.w[row] > compositor.width || ty + (int)compositor.h[row] > compositor.height)
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

static void compositor_reveal(int row);

/*
        A knock: what the row says now, read once into the compositor's own
        columns and clamped there. A new title goes to its tab. A new picture
        is damaged and said to be taken; the first puts the window on the
        screen, where it asked to be and on the output, over the others and
        with the keys unless it asked to be put away.
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

        if (frame == compositor.seen[row])
                return;

        compositor.seen[row] = frame;
        width = width > (unsigned int)compositor.width ? (unsigned int)compositor.width : width;
        height = height > (unsigned int)compositor.height ? (unsigned int)compositor.height : height;
        __atomic_store_n(&table->shown[row], frame, __ATOMIC_RELEASE);
        compositor.woken |= 1u << row;

        // A picture of nothing shows nothing.
        if (!width || !height)
                return;

        if (!compositor.showing[row])
        {
                int x = table->x[row], y = table->y[row];

                compositor.w[row] = compositor.picture_w[row] = width;
                compositor.h[row] = compositor.picture_h[row] = height;
                compositor.asking[row] = 0;
                compositor.front[row] = front;
                compositor.state[row] = table->state[row] & WINDOW_MINIMIZED;
                table->state[row] = compositor.state[row];
                compositor.arranged[row] = COMPOSITOR_FLOATING;
                compositor.edges[row] = 0;
                compositor.lifted[row] = table->lift[row];
                compositor.glassy[row] = table->glass[row] & WINDOW_GLASS;
                compositor.glass_side[row] = COMPOSITOR_GLASS_UNSIDED;
                compositor.showing[row] = 1;
                compositor.made[row] = ++compositor.next_made;
                compositor_first_place(row, &x, &y);
                compositor_place(row, x, y);
                compositor_raise(row);
                if (compositor_visible(row))
                        compositor_focus(row);
                compositor_tabs_changed();
                return;
        }

        compositor.picture_w[row] = width;
        compositor.picture_h[row] = height;
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
        compositor.grab = compositor.grab == row ? COMPOSITOR_NONE : compositor.grab;
        compositor.drag = compositor.drag == row ? COMPOSITOR_NONE : compositor.drag;
        compositor.press = compositor.press == row ? COMPOSITOR_NONE : compositor.press;
        compositor.resize = compositor.resize == row ? COMPOSITOR_NONE : compositor.resize;
        // The stack is one shorter: an Alt-Tab under way would count past its end.
        compositor.cycle = 0;

        compositor.showing[row] = compositor.seen[row] = compositor.state[row] = 0;
        compositor.w[row] = compositor.h[row] = compositor.edges[row] = 0;
        compositor.picture_w[row] = compositor.picture_h[row] = compositor.asking[row] = 0;
        compositor.arranged[row] = COMPOSITOR_FLOATING;
        compositor.title[row][0] = 0;
        compositor_glass_drop(row);
        compositor.glass_side[row] = compositor.glass_told[row] = 0;
        table->ground[row] = 0;
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

        while (system_read_once(compositor.door[row], bytes, sizeof(bytes)) > 0)
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

// The pointer, to a window: where it is in the window's picture.
static void compositor_pointer_to(int row)
{
        compositor_post(row, WINDOW_EVENT_MOTION, 0, compositor.pointer_x - compositor.x[row],
                        compositor.pointer_y - compositor.y[row]);
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
        {
                compositor.cursor.magnify = 0;
                compositor.cursor.magnified = 1;
                compositor.cursor.magnified_until = clock_monotonic_nanoseconds() + COMPOSITOR_MAGNIFIED_NS;
        }
        compositor_cursor_move();

        // On the plane the pointer is on the screen when the call returns; drawn, when the frame has gone.
        if (compositor.motion_ts && !compositor.cursor.soft)
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

        // A held button keeps the pointer with the window it went down on.
        compositor.hover = under;
        compositor_pointer_to(compositor.grab >= 0 ? compositor.grab : under);
}

// Where a drag ended, read as an arrangement: the pointer and not the window,
// because the pointer is what the hand aimed. The top before the sides, so a
// corner fills the screen.
static unsigned int compositor_snap(int x, int y)
{
        int margin = COMPOSITOR_SNAP * compositor.scale;

        if (y < margin)
                return COMPOSITOR_MAXIMIZED;
        if (x < margin)
                return COMPOSITOR_LEFT;
        if (compositor.width - 1 - x < margin)
                return COMPOSITOR_RIGHT;
        return COMPOSITOR_FLOATING;
}

// The button is let go. A window that was being moved is arranged by where the
// hand let go, unless the release is not the hand's (the device went away).
static void compositor_release(unsigned int code, int snap)
{
        int grab = compositor.grab, dragged = compositor.drag;

        compositor.drag = COMPOSITOR_NONE;
        compositor.grab = COMPOSITOR_NONE;
        compositor.resize = COMPOSITOR_NONE;
        compositor_post(grab, WINDOW_EVENT_BUTTON, 0, (int)code, 0);
        // A press and a release with no motion between is a click on the window's top, which only raises it:
        // a window at the top of the output, where the snap margin is, would otherwise fill it when clicked.
        if (dragged >= 0 && snap && compositor.drag_moved &&
            compositor_snap(compositor.pointer_x, compositor.pointer_y) != COMPOSITOR_FLOATING)
                compositor_arrange(dragged, compositor_snap(compositor.pointer_x, compositor.pointer_y));
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
                                if (compositor.arranged[under] == COMPOSITOR_MAXIMIZED)
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
                moved = 1;
        }

        if (compositor.raw_x || compositor.raw_y)
        {
                int dx = compositor.raw_x, dy = compositor.raw_y, gain;

                compositor.raw_x = compositor.raw_y = 0;
                gain = compositor_gain_of(dx, dy, now - compositor.stamp);
                compositor.stamp = now;
                compositor.pointer_x += compositor_accel(dx, &compositor.accel_x, gain);
                compositor.pointer_y += compositor_accel(dy, &compositor.accel_y, gain);
                moved = 1;
        }

        return moved;
}

// The wheel goes to what is under the pointer, or what holds it.
static void compositor_wheel(int v120, int horizontal)
{
        compositor_post(compositor.grab >= 0 ? compositor.grab : compositor.hover, WINDOW_EVENT_WHEEL, 0, v120,
                        horizontal);
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
                                if (compositor_shake(&compositor.shake, event->value, now))
                                        compositor.cursor.magnify = 1;
                        }
                        else if (event->type == COMPOSITOR_EV_REL && event->code == COMPOSITOR_REL_Y)
                                compositor.raw_y += event->value;
                        else if (event->type == COMPOSITOR_EV_REL &&
                                 (event->code == COMPOSITOR_REL_WHEEL_HI || event->code == COMPOSITOR_REL_HWHEEL_HI))
                                compositor_wheel(event->value, event->code == COMPOSITOR_REL_HWHEEL_HI);
                        else if (event->type == COMPOSITOR_EV_REL && !compositor.hires[at] &&
                                 (event->code == COMPOSITOR_REL_WHEEL || event->code == COMPOSITOR_REL_HWHEEL))
                                compositor_wheel(event->value * 120, event->code == COMPOSITOR_REL_HWHEEL);
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
        The table and its doors, made new: whatever is at the paths is from a
        compositor whose session is over. The table is sparse, so its size is
        the most every window could draw and what it costs is the pages that
        are drawn. Any program may open a window.
*/
static int compositor_table(void)
{
        unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
        unsigned long buffer = ((unsigned long)compositor.width * (unsigned long)compositor.height * 4 + 4095) & ~4095ul;
        unsigned long bytes = header + buffer * 2 * WINDOWS;
        long handle, mapped;
        p8 name[48];

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run", 0755, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);

        handle = (long)system_open_at_mode(AT_FDCWD, (string_address)WINDOWS_TABLE,
                                           FILE_READ_WRITE | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (handle < 0)
                return (int)handle;
        if (compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0)
        {
                system_close(handle);
                return -COMPOSITOR_ENOSPC;
        }
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 1, (positive)handle, 0);
        system_close(handle);
        if (mapped < 0 && mapped > -4096)
                return (int)mapped;
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0666, 0, 0, 0);

        compositor.table = (struct windows *)mapped;
        compositor.table_bytes = bytes;
        compositor.pixels = header;
        compositor.buffer = buffer;
        compositor.table->output_width = (unsigned int)compositor.width;
        compositor.table->output_height = (unsigned int)compositor.height;
        compositor.table->stride = (unsigned int)compositor.width;
        compositor.table->scale = (unsigned int)compositor.scale;
        compositor.table->period = (unsigned int)compositor.period;
        compositor.table->focus = WINDOWS;
        compositor.table->pixels = header;
        compositor.table->buffer = buffer;

        // The door is read here and the wake written, each end kept for the session; S_IFIFO.
        for (unsigned int row = 0; row < WINDOWS; row++)
                for (int wake = 0; wake < 2; wake++)
                {
                        windows_door_name((char *)name, row, wake);
                        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
                        compositor_call(syscall(mknodat), (positive)AT_FDCWD, (positive)name, 0010666, 0, 0, 0);
                        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)name, 0666, 0, 0, 0);
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
        positive size = 0, bytes = path ? compositor_map_file(path, address_of size) : 0;
        int x, y, width, height;
        long engine, band, glass;

        if (!bytes)
                return;

        compositor_band(compositor.width, compositor.height, compositor.scale, compositor.bottom, &x, &y, &width,
                        &height);
        engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);
        band = compositor_call(syscall(mmap), 0, (positive)width * (positive)height * 4, 3, 0x22, (positive)-1, 0);
        // The glass: the band and the blur's reach on every side.
        compositor.glass_bytes = (unsigned long)(width + 2 * COMPOSITOR_GLASS_ACROSS * compositor.scale) *
                                 (unsigned long)(height + 2 * COMPOSITOR_GLASS_DOWN * compositor.scale) * 4;
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
        compositor.glass = (unsigned int *)glass;
        compositor.band_stale = 1;
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
        compositor.watch = -1;
        compositor.hold = -1;
        compositor.bottom = 1;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;

        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (compositor_option(option, 't'))
                        compositor.bottom = 0;
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
        compositor_font(font);

        compositor.signals = compositor_signals();
        status = compositor_table();
        if (status < 0 || compositor.signals < 0)
        {
                compositor_say("cannot make the window table, error", status < 0 ? status : compositor.signals);
                if (!headless)
                        compositor_hand_back();
                return 1;
        }

        string_format(log, "compositor: %b by %b (scale %b), %p input devices, pointer %s, windows in %s\n",
                      (bipolar)compositor.width, (bipolar)compositor.height,
                      (bipolar)compositor.scale, (positive)compositor.input_count,
                      compositor.cursor.soft ? "drawn" : compositor.cursor.handle[0] ? "on the plane" : "none",
                      WINDOWS_TABLE);
        log_flush();

        compositor.asleep = compositor_slept_clock();

        while (running)
        {
                struct compositor_poll wanted[2 + COMPOSITOR_INPUTS + WINDOWS];
                unsigned int count = 0, inputs, doors;
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

                // A pointer that was shaken is its own size again when its time is up.
                if (compositor.cursor.magnified && clock_monotonic_nanoseconds() >= compositor.cursor.magnified_until)
                {
                        compositor.cursor.magnified = 0;
                        compositor_cursor_move();
                }

                if (compositor.band && compositor_minute() != compositor.minute)
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
        [-T TITLE] [-p] [-m] [-R] [-n] [-G] [-a ALPHA] [-F PICTURES]
        [-f FONT.ttf] [-t TEXT] [-s SECONDS].

        A window of one colour at X, Y, a line of TEXT drawn with font.c on it
        when FONT is given, and a line on standard output for everything it is
        told: its row and where it is, each key, button and wheel, the keys
        coming and going, a close asked for, and with -p the pointer's
        motions. -m puts it away as it opens, -R draws the size it is asked
        for, -n never shows a picture, and -F shows PICTURES more, as fast as
        it can. -G makes it glass, its colour laid over the glass at ALPHA
        (0, the default, is the glass alone, 255 the colour alone). It lives
        until it is asked to close, the compositor goes, or SECONDS.
*/
static void surface_draw(const struct window_row *own, unsigned int which, unsigned int width, unsigned int height,
                         unsigned int colour, string_address font, string_address text)
{
        struct windows *table = own->table;
        unsigned int *pixels = windows_buffer(table, own->row, which);

        for (unsigned int line = 0; line < height; line++)
                memory_fill_u32(pixels + (unsigned long)line * table->stride, width, colour);

        // Glass has no ground to draw text on.
        if (font && !table->glass[own->row])
        {
                positive size = 0;
                positive bytes = compositor_map_file(font, address_of size);
                long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);
                static p8 atlas[256 * 256];
                font_face face;
                const font_face address_to faces[1] = {&face};
                font_target target = {pixels, table->stride, (b32)width, (b32)height};
                p32 em = 16 << 6;

                if (bytes && !(engine < 0 && engine > -4096) && font_load(&face, (const p8 address_to)bytes, size, 0) == 0)
                {
                        font_start((font_engine address_to)engine, atlas, 256, 256);
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
                        compositor_call(syscall(munmap), (positive)engine, sizeof(font_engine), 0, 0, 0, 0);
                if (bytes)
                        compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }

        table->width[own->row] = width;
        table->height[own->row] = height;
        table->damage_width[own->row] = 0;
}

static b32 surface_main()
{
        bipolar x = 0, y = 0, width = 320, height = 200, colour = 0x3366aa, seconds = -1, flood = 0, alpha = 0;
        b32 pointer = 0, never = 0, away = 0, sized = 0, focused = 0, glass = 0;
        string_address title = null, font = null, text = (string_address) "Moonwater";
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
        for (positive at = 0; title && title[at] && at < WINDOWS_TITLE - 1; at++)
                table->title[own.row][at] = (char)title[at];
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
