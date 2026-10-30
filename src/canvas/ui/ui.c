/*
        The system's own look: what a window Moonwater draws for itself is
        made of, so the settings, the launcher and every window like them
        look like one system. It is the kit (Dawning Native, ported): its
        theme's roles, its type roles, its shapes drawn with their edges
        where they fall, its controls with their motion, and the composites
        a window is made of -- a page (a title, a search field, a column of
        groups, a body and a status line), a list of rows in sections, the
        menu a choice opens, a page of text, a scrollbar on whatever scrolls
        and a finger's press, drag and throw. An app declares what it shows;
        every size, colour, state and motion is the kit's, with nothing to
        set. Sizes are logical pixels, times the scale the compositor draws
        at. Text is font.c's, Inter; a run is drawn as coverage into a
        scratch line and laid over the picture, premultiplied like everything
        else, at whole pixels and in grey, never coloured by the subpixels.

        A window of the kit is opaque, the theme's surface (ui->opaque); a
        glass window (WINDOW_GLASS, window.c) is premultiplied over what is
        under it, and its ground's inks (ui_inks: ink, quiet, lit, ring) are
        what stands out from the tint the compositor gives it.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_UI
#define MOONWATER_CANVAS_UI

// What text is below the look: documents, history, selections, segmentation (text.c, which says why it is its own file).
#include "text.c"

#define UI_TEXT 14     // a label, a value
#define UI_SMALL 12    // a section's header
#define UI_TITLE 20    // the window's title
#define UI_ROW 40      // a row
#define UI_PAD 16      // the window's margin
#define UI_INSET 14    // the inset of a panel's content from its edge
#define UI_RADIUS 8    // a focused row's and a field's corners, as the windows'
#define UI_SWITCH_W 36 // a switch
#define UI_SWITCH_H 20
#define UI_RING 2      // the focus ring's width
#define UI_SCRATCH_ROWS 96

struct ui
{
        unsigned int address_to pixels; // premultiplied ARGB, pitch pixels a row
        unsigned long pitch;
        int width, height;
        int top, bottom;                // the rows that may be drawn: a list scrolled under a title is cut
        int scale;
        font_engine address_to engine;
        const font_face address_to regular;
        const font_face address_to bold;
        unsigned int ground, ink, quiet, lit, ring;
        unsigned int address_to scratch; // a line of text, as coverage
        int scratch_width;
        // A repaint's rows, when band_bottom > band_top: nothing else is written. Last, so the positional initialisers of a ui stay.
        int band_top, band_bottom;
        font_face faces[2];             // the regular and the bold, loaded by the runner
        const font_face address_to mono; // the kit's mono (a tag, a code well, a key), or none: then the regular
        font_face face_mono;
        b32 opaque;                     // a window of the kit's surface, not glass: cleared to the theme's surface
};

/*
        x / 255 for x below 65535, exact: x plus its high byte, plus one, shifted. The blends
        below never make a product past 255 * 255 + 127, which is well under that.
*/
static inline unsigned int ui_div255(unsigned int x)
{
        return (x + 1 + (x >> 8)) >> 8;
}

// Whether row y may be written: a repaint's band, or every row when there is none.
static inline b32 ui_in_band(const struct ui address_to ui, int y)
{
        return ui->band_bottom <= ui->band_top || (y >= ui->band_top && y < ui->band_bottom);
}

// A colour at alpha (0 to 255), premultiplied: what every piece is drawn in.
static unsigned int ui_tone(unsigned int colour, unsigned int alpha)
{
        alpha = alpha > 255 ? 255 : alpha;
        return alpha << 24 | window_mix(0, colour & 0xffffffu, alpha + (alpha >> 7));
}

// The inks for a ground: what the compositor says the glass came to.
static fn ui_inks(struct ui address_to ui, unsigned int ground)
{
        ui->ground = ground & 0xffffffu;
        ui->ink = window_contrast(ui->ground);
        ui->quiet = window_mix(ui->ground, ui->ink, 176);
        ui->lit = ui_tone(ui->ink, 36);
        ui->ring = ui_tone(ui->ink, 190);
}

// One premultiplied pixel over another.
static inline unsigned int ui_over(unsigned int pixel, unsigned int under)
{
        unsigned int rest = 255 - (pixel >> 24), out = 0;

        if (!(pixel >> 24))
                return under;
        if (!rest)
                return pixel;
        // Over nothing (a picture cleared to the glass), the pixel is the pixel: the blend adds nothing.
        if (!under)
                return pixel;
        /*
                The four channels in two words of 16-bit lanes (red and blue, green and alpha): under times rest plus 127
                in each lane is at most 65152, so no lane carries into the next, and ui_div255 is one shift and an add.
                A sum past 255 in a lane (a tone that is not premultiplied) is clamped the long way.
        */
        {
                unsigned int v_rb = (under & 0x00ff00ffu) * rest + 0x007f007fu;
                unsigned int v_ag = ((under >> 8) & 0x00ff00ffu) * rest + 0x007f007fu;
                unsigned int sum_rb = (pixel & 0x00ff00ffu) + ((v_rb + 0x00010001u + ((v_rb >> 8) & 0x00ff00ffu)) >> 8 & 0x00ff00ffu);
                unsigned int sum_ag = ((pixel >> 8) & 0x00ff00ffu) + ((v_ag + 0x00010001u + ((v_ag >> 8) & 0x00ff00ffu)) >> 8 & 0x00ff00ffu);

                if (!((sum_rb | sum_ag) & 0x01000100u))
                        return (sum_rb & 0x00ff00ffu) | (sum_ag & 0x00ff00ffu) << 8;
        }
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                unsigned int channel = (pixel >> shift & 255) + ui_div255((under >> shift & 255) * rest + 127);

                out |= (channel > 255 ? 255 : channel) << shift;
        }
        return out;
}

// A tone at cover 255ths of itself.
static unsigned int ui_cover(unsigned int tone, unsigned int cover)
{
        unsigned int out = 0;

        if (cover >= 255)
                return tone;
        for (unsigned int shift = 0; shift < 32; shift += 8)
                out |= ui_div255((tone >> shift & 255) * cover + 127) << shift;
        return out;
}

static fn ui_dot(struct ui address_to ui, int x, int y, unsigned int tone)
{
        unsigned int address_to at;

        if (x < 0 || x >= ui->width || y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y))
                return;
        at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x;
        address_to at = ui_over(tone, address_to at);
}

static unsigned int ui_role_surface(void);

// The whole picture clear: the glass alone (the kit's surface for an opaque window); within a repaint's band, its rows.
static fn ui_clear(struct ui address_to ui)
{
        int top = 0, bottom = ui->height;

        if (ui->band_bottom > ui->band_top)
        {
                top = ui->band_top < 0 ? 0 : ui->band_top;
                bottom = ui->band_bottom > ui->height ? ui->height : ui->band_bottom;
        }
        if (bottom > top)
                memory_fill_u32_rect(ui->pixels + (unsigned long)top * ui->pitch, (positive)ui->pitch, (positive)ui->width,
                                     (positive)(bottom - top), ui->opaque ? ui_role_surface() : 0);
}

// The picture of a whole window, width by height, its rows pitch pixels apart, cleared to the glass.
static fn ui_begin(struct ui address_to ui, unsigned int address_to pixels, unsigned long pitch, int width, int height)
{
        ui->pixels = pixels;
        ui->pitch = pitch;
        ui->width = width;
        ui->height = height;
        ui->top = 0;
        ui->bottom = height;
        ui_clear(ui);
}

static fn ui_fill(struct ui address_to ui, int x, int y, int width, int height, unsigned int tone)
{
        int x2 = x + width, y2 = y + height;

        x = x < 0 ? 0 : x;
        y = y < ui->top ? ui->top : y;
        x2 = x2 > ui->width ? ui->width : x2;
        y2 = y2 > ui->bottom ? ui->bottom : y2;
        if (ui->band_bottom > ui->band_top)
        {
                y = y < ui->band_top ? ui->band_top : y;
                y2 = y2 > ui->band_bottom ? ui->band_bottom : y2;
        }
        // An opaque tone over a pixel is that tone: the rectangle is one fill.
        if (tone >> 24 == 255)
        {
                if (x2 > x && y2 > y)
                        memory_fill_u32_rect(ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x, (positive)ui->pitch,
                                             (positive)(x2 - x), (positive)(y2 - y), tone);
                return;
        }
        for (int row = y; row < y2; row++)
                for (int column = x; column < x2; column++)
                {
                        unsigned int address_to at = ui->pixels + (unsigned long)row * ui->pitch + (unsigned long)column;

                        address_to at = ui_over(tone, address_to at);
                }
}

/*
        How much of a pixel a rounded rectangle covers, in 255ths: sixteen
        sample points, those in a corner's square counted inside its quarter
        circle. x and y are the pixel's place from the rectangle's top left.
*/
static unsigned int ui_inside(int x, int y, int width, int height, int radius)
{
        unsigned int inside = 0;

        if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
        if ((x >= radius && x < width - radius) || (y >= radius && y < height - radius))
                return 255;

        for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                        // In eighths of a pixel, from the nearest corner's circle's centre.
                        int px = 8 * x + 2 * sx + 1, py = 8 * y + 2 * sy + 1;
                        int cx = x < radius ? 8 * radius : 8 * (width - radius), cy = y < radius ? 8 * radius : 8 * (height - radius);
                        int dx = px - cx, dy = py - cy;

                        inside += dx * dx + dy * dy <= 64 * radius * radius;
                }
        return (inside * 255 + 8) / 16;
}

/*
        A corner's cover depends on its radius alone: the radius is already in device pixels, and ui_inside reads no
        other input. Its sample points are symmetric about the middle of the corner's square, so a pixel of any corner
        samples the same circle as the pixel mirrored across that middle into the top left quarter. That quarter is made
        once for each radius, the first time the radius is drawn, and kept: radius r at the sum of the squares below it,
        so the masks of every radius up to UI_CORNER_MOST take UI_CORNER_BYTES (22140), and a larger radius is not kept
        (ui_inside answers it, as before). A radius is one slot, so no two radii share one.
*/
#define UI_CORNER_MOST 40
#define UI_CORNER_BYTES (UI_CORNER_MOST * (UI_CORNER_MOST + 1) * (2 * UI_CORNER_MOST + 1) / 6)
static unsigned char ui_corner_masks[UI_CORNER_BYTES];
static unsigned char ui_corner_made[UI_CORNER_MOST + 1];

static const unsigned char address_to ui_corner_mask(int radius)
{
        unsigned char address_to mask = ui_corner_masks + (unsigned long)(radius - 1) * radius * (2 * radius - 1) / 6;

        if (!ui_corner_made[radius])
        {
                // The quarter of a corner of radius, read from ui_inside itself: its square is 2 radius across.
                for (int y = 0; y < radius; y++)
                        for (int x = 0; x < radius; x++)
                                mask[y * radius + x] = (unsigned char)ui_inside(x, y, 2 * radius, 2 * radius, radius);
                ui_corner_made[radius] = 1;
        }
        return mask;
}

// ui_inside, read from a corner's mask where the pixel is in a corner's square. A pixel left of the middle or above it
// is its own place in the top left quarter; one to the right or below is mirrored across the middle.
static unsigned int ui_inside_cached(int x, int y, int width, int height, int radius)
{
        int mx, my;

        if (radius < 1 || radius > UI_CORNER_MOST)
                return ui_inside(x, y, width, height, radius);
        if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
        if ((x >= radius && x < width - radius) || (y >= radius && y < height - radius))
                return 255;
        mx = x < radius ? x : width - 1 - x;
        my = y < radius ? y : height - 1 - y;
        return ui_corner_mask(radius)[my * radius + mx];
}

// A rounded rectangle, filled.
static fn ui_round(struct ui address_to ui, int x, int y, int width, int height, int radius, unsigned int tone)
{
        radius = radius * 2 > width ? width / 2 : radius;
        radius = radius * 2 > height ? height / 2 : radius;
        for (int row = 0; row < height; row++)
        {
                // The straight middle rows are one fill.
                if (row >= radius && row < height - radius)
                {
                        int last = height - radius;

                        ui_fill(ui, x, y + row, width, last - row, tone);
                        row = last - 1;
                        continue;
                }
                // A row outside the band is not walked; a corner row's straight middle is one fill, as its cover is all of it.
                if (!ui_in_band(ui, y + row))
                        continue;
                for (int column = 0; column < width; column++)
                {
                        unsigned int cover;

                        if (column == radius && width - radius > radius)
                        {
                                ui_fill(ui, x + radius, y + row, width - 2 * radius, 1, tone);
                                column = width - radius - 1;
                                continue;
                        }
                        cover = ui_inside_cached(column, row, width, height, radius);

                        if (cover)
                                ui_dot(ui, x + column, y + row, ui_cover(tone, cover));
                }
        }
}

// A rounded rectangle's outline, thickness wide, inside its edge.
static fn ui_outline(struct ui address_to ui, int x, int y, int width, int height, int radius, int thickness,
                     unsigned int tone)
{
        int inner = radius > thickness ? radius - thickness : 0;

        for (int row = 0; row < height; row++)
                for (int column = 0; column < width; column++)
                {
                        unsigned int outer_cover, inner_cover;

                        if (!ui_in_band(ui, y + row))
                        {
                                column = width - 1;
                                continue;
                        }
                        // The middle is not the outline.
                        if (column >= thickness + inner && column < width - thickness - inner && row >= thickness &&
                            row < height - thickness)
                        {
                                column = width - thickness - inner - 1;
                                continue;
                        }
                        outer_cover = ui_inside_cached(column, row, width, height, radius);
                        inner_cover = ui_inside_cached(column - thickness, row - thickness, width - 2 * thickness,
                                                height - 2 * thickness, inner);
                        if (outer_cover > inner_cover)
                                ui_dot(ui, x + column, y + row, ui_cover(tone, outer_cover - inner_cover));
                }
}

/*
        A run laid a code point at a time: the pen after the last glyph, which
        is the advance font_draw answers for the run so far, kerning and all.
        The same steps font_draw takes, without the coverage.
*/
struct ui_pen
{
        b64 reach;        // the pen after the last glyph, in 16.16
        b64 cell;         // the advance of a cell the font does not know
        p32 glyph, face;  // the last glyph, and the face it is from (FONT_DRAWN for none)
};

static void ui_pen_start(const font_face address_to const address_to faces, p32 em, struct ui_pen address_to pen)
{
        pen->reach = 0;
        pen->cell = (b64)font_metrics_at(faces[0], em).cell_width << 16;
        pen->glyph = 0;
        pen->face = FONT_DRAWN;
}

static void ui_pen_step(struct ui address_to ui, const font_face address_to const address_to faces, p32 em, p32 code,
                        struct ui_pen address_to pen)
{
        p32 face, glyph;
        b64 advance;

        font_find(ui->engine, faces, 2, code, &face, &glyph);
        if (face != FONT_DRAWN && pen->face == face)
                pen->reach += font_kern(ui->engine, faces[face], pen->glyph, glyph, em);
        advance = face == FONT_DRAWN ? pen->cell : font_advance(faces[face], glyph, em);
        pen->reach += advance;
        pen->glyph = glyph;
        pen->face = face;
}

static int ui_pen_width(const struct ui_pen address_to pen)
{
        return (int)(pen->reach >> 16);
}

// A run's advance in whole pixels, laid out and not drawn.
static int ui_advance(struct ui address_to ui, const font_face address_to const address_to faces, string_address text,
                      positive length, p32 em)
{
        struct ui_pen pen;
        positive at = 0;

        ui_pen_start(faces, em, &pen);
        while (at < length)
        {
                p32 code;

                at = font_decode((const p8 address_to)text, length, at, &code);
                ui_pen_step(ui, faces, em, code, &pen);
        }
        return ui_pen_width(&pen);
}

/*
        A run of text drawn with its pen at x on the baseline in colour (alpha
        255 is opaque ink, less a fainter one), and its width answered. Only
        the columns from low to high are drawn: a field's text is cut at its
        well, and the rest of the run is not. A run wholly outside the band
        of a repaint is measured and not drawn.
*/
/*
        The run in a face of the caller's (faces is it and the regular, for what
        it has not), with a stroke: the kit's -webkit-text-stroke .5px is 64, a
        pixel taking half its strongest neighbour's cover (measured against
        Chrome's stroke by eye on the filmstrips), which never moves a glyph off
        its whole pixel. 0 is no stroke.
*/
static int ui_run_in(struct ui address_to ui, const font_face address_to const address_to faces, int x, int baseline,
                     string_address text, int size, unsigned int colour, unsigned int alpha, int low, int high,
                     unsigned int stroke);

static int ui_run(struct ui address_to ui, int x, int baseline, string_address text, int size, b32 bold,
                  unsigned int colour, unsigned int alpha, int low, int high)
{
        const font_face address_to faces[2] = {bold && ui->bold ? ui->bold : ui->regular, ui->regular};

        return ui_run_in(ui, faces, x, baseline, text, size, colour, alpha, low, high, 0);
}

static int ui_run_in(struct ui address_to ui, const font_face address_to const address_to faces, int x, int baseline,
                     string_address text, int size, unsigned int colour, unsigned int alpha, int low, int high,
                     unsigned int stroke)
{
        p32 em = (p32)(size * ui->scale) << 6;
        positive length = string_length(text);
        int width = ui_advance(ui, faces, text, length, em);
        font_metrics metrics = font_metrics_at(faces[0], em);
        int above = metrics.baseline + 2 * ui->scale, rows = metrics.cell_height + 4 * ui->scale;
        int left = 2 * ui->scale, wide = width + 2 * left, top = baseline - above;
        b32 light = window_light(colour); // light ink: drawn white on black, so the coverage is the light text's own
        unsigned int paper = light ? 0 : 0xffffffu;

        if (!alpha || !ui->scratch)
                return width;
        wide = wide > ui->scratch_width ? ui->scratch_width : wide;
        rows = rows > UI_SCRATCH_ROWS * ui->scale ? UI_SCRATCH_ROWS * ui->scale : rows;
        if (ui->band_bottom > ui->band_top && (top + rows <= ui->band_top || top >= ui->band_bottom))
                return width;

        memory_fill_u32_rect(ui->scratch, (positive)wide, (positive)wide, (positive)rows, paper);
        {
                font_target line = {ui->scratch, (positive)wide, wide, rows};

                font_draw(ui->engine, faces, 2, (const p8 address_to)text, length, em, 0, &line, (b32)left << 16, above,
                          light ? 0xffffffu : 0, paper);
        }

        // The coverage, as the ink at that alpha, over the picture: one tone for each coverage, made once. The columns
        // drawn are those in low to high and in the picture, worked out once, so the walk over a row has no test in it.
        {
                unsigned int tone[256];
                unsigned long long known[4] = {0, 0, 0, 0};
                int pen_x = x - left, first = 0, last = wide;

                if (low - pen_x > first)
                        first = low - pen_x;
                if (high - pen_x < last)
                        last = high - pen_x;
                if (-pen_x > first)
                        first = -pen_x;
                if (ui->width - pen_x < last)
                        last = ui->width - pen_x;
                for (int row = 0; row < rows && first < last; row++)
                {
                        int y = top + row;
                        const unsigned int address_to coverage = ui->scratch + (unsigned long)row * (unsigned long)wide;
                        unsigned int address_to picture;

                        if (y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y))
                                continue;
                        picture = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)(pen_x + first);
                        for (int column = first; column < last; column++, picture++)
                        {
                                unsigned int value = coverage[column] >> 8 & 255;
                                unsigned int cover = light ? value : 255 - value, bit;

                                if (stroke && cover < 255)
                                {
                                        // The stroke: a pixel takes a share of its strongest neighbour's cover.
                                        unsigned int most = 0;

                                        for (int n = 0; n < 4; n++)
                                        {
                                                int nx = column + (n == 0 ? -1 : n == 1 ? 1 : 0), ny = row + (n == 2 ? -1 : n == 3 ? 1 : 0);
                                                unsigned int v, c;

                                                if (nx < 0 || nx >= wide || ny < 0 || ny >= rows)
                                                        continue;
                                                v = ui->scratch[(unsigned long)ny * (unsigned long)wide + (unsigned long)nx] >> 8 & 255;
                                                c = light ? v : 255 - v;
                                                most = c > most ? c : most;
                                        }
                                        cover += most * stroke / 128;
                                        cover = cover > 255 ? 255 : cover;
                                }
                                bit = cover & 63;

                                if (!cover)
                                        continue;
                                if (!(known[cover >> 6] >> bit & 1))
                                {
                                        tone[cover] = ui_tone(colour, cover * alpha / 255);
                                        known[cover >> 6] |= 1ull << bit;
                                }
                                *picture = ui_over(tone[cover], *picture);
                        }
                }
        }
        return width;
}

// A run of text, whole, drawn as ui_run draws it: its width is answered, and a run of alpha 0 draws nothing.
static int ui_text(struct ui address_to ui, int x, int baseline, string_address text, int size, b32 bold,
                   unsigned int colour, unsigned int alpha)
{
        return ui_run(ui, x, baseline, text, size, bold, colour, alpha, 0, ui->width);
}

// A run's width, measured.
static int ui_measure(struct ui address_to ui, string_address text, int size, b32 bold)
{
        return ui_text(ui, 0, 0, text, size, bold, 0, 0);
}

// A run's capital height at size, in whole pixels: font.c's metrics are 16.16.
static int ui_capital(struct ui address_to ui, int size)
{
        font_metrics metrics = font_metrics_at(ui->regular, (p32)(size * ui->scale) << 6);

        return (int)((metrics.cap_height + 32768) >> 16);
}

// Where a run of size sits in a band top to top + height: its capitals in the band's middle.
static int ui_baseline(struct ui address_to ui, int top, int height, int size)
{
        int capital = ui_capital(ui, size);

        return top + (height - capital) / 2 + capital;
}

// The row with the focus: lit, with a ring round it.
static fn ui_focus(struct ui address_to ui, int x, int y, int width, int height, b32 ring)
{
        int radius = (UI_RADIUS - 2) * ui->scale;

        ui_round(ui, x, y, width, height, radius, ui->lit);
        if (ring)
                ui_outline(ui, x, y, width, height, radius, UI_RING * ui->scale, ui->ring);
}

// A logical size in pixels at the window's scale.
static int ui_px(const struct ui address_to ui, int logical)
{
        return logical * ui->scale;
}

/*
        A finger, and a press. A finger on a touch screen (WINDOW_KEY_TOUCH on what the compositor sends) is wider than
        a pointer and shakes as it lands, so what it can press is at least UI_TARGET logical pixels each way however
        small it is drawn, the drawn shape in the middle of that (ui_hit); a press held moves nothing until it is
        UI_SLOP from where it went down, and past that it is a drag (the list follows the finger, ui_drag) and no
        click; and a press the compositor takes back (WINDOW_KEY_CANCEL: a second finger, or one held for the right
        button) is no click either. What is under a press is shown pressed the moment it goes down, not when it comes
        up, for a mouse's press as for a finger's; the app numbers what can be pressed as it likes (a row, a button):

            ui_press_begin(press, x, y, target, touch)  a button or a finger went down on target (-1 for nothing)
            ui_press_move(ui, press, x, y)              true once the press is a drag: target is pressed no more
            ui_press_end(press, cancelled)              what was clicked as the press comes up, or -1
            ui_pressed(press, target)                   whether target is shown pressed now
            ui_press_fill(ui, x, y, width, height)      how pressed looks: a faint fill of the ink, over what is drawn
*/
#define UI_TARGET 44 // a finger's least target in logical pixels: about 9 mm at the 96 to 120 dpi of most screens
#define UI_SLOP 8    // how far a press moves before it is a drag, as the compositor's finger (COMPOSITOR_TOUCH_SLOP)

struct ui_press
{
        int x, y, target;        // where it went down and on what (-1 nothing)
        b32 down, touch, dragged;
};

static fn ui_press_begin(struct ui_press address_to press, int x, int y, int target, b32 touch)
{
        press->x = x, press->y = y, press->target = target;
        press->down = true, press->touch = touch, press->dragged = false;
}

static b32 ui_press_move(const struct ui address_to ui, struct ui_press address_to press, int x, int y)
{
        long dx = (long)x - press->x, dy = (long)y - press->y, slop = UI_SLOP * (long)ui->scale;

        if (press->down && !press->dragged && dx * dx + dy * dy >= slop * slop)
                press->dragged = true;
        return press->down && press->dragged;
}

static int ui_press_end(struct ui_press address_to press, b32 cancelled)
{
        int clicked = press->down && !press->dragged && !cancelled ? press->target : -1;

        press->down = false;
        press->target = -1;
        return clicked;
}

static b32 ui_pressed(const struct ui_press address_to press, int target)
{
        return press->down && !press->dragged && target >= 0 && press->target == target;
}

// The pressed look is the kit's state layer (defined with the kit's controls, below).
static fn ui_press_fill(struct ui address_to ui, int x, int y, int width, int height);

static b32 ui_kit_touch; // the event is a finger's (WINDOW_KEY_TOUCH): what it can press is at least UI_TARGET each way

// Whether px, py hits a shape x, y, width by height: a finger's reach is the shape grown to UI_TARGET each way where it is smaller.
static b32 ui_hit(const struct ui address_to ui, b32 touch, int px, int py, int x, int y, int width, int height)
{
        int least = touch ? UI_TARGET * ui->scale : 0;

        if (width < least)
                x -= (least - width) / 2, width = least;
        if (height < least)
                y -= (least - height) / 2, height = least;
        return px >= x && px < x + width && py >= y && py < y + height;
}

/*
        A list dragged by a finger follows it, and let go while moving it goes on, slowing as a thing thrown does,
        until it stops. Speeds are pixels a second, times are the monotonic clock's nanoseconds, all whole numbers.

            ui_drag_start(drag, at, now)   the finger took the list at at (a y, or an x)
            ui_drag_follow(drag, at, now)  the finger is at at: how far the list moves, the finger's move since the last
            ui_drag_let_go(drag, now)      the finger lifted: whether the list is thrown (ui_drag_step moves it then)
            ui_drag_step(drag, now)        a throw's move since the last step, and 0 once it stopped (flying is false);
                                           the app steps it again UI_DRAG_FRAME_NS later while it flies
            ui_drag_stop(drag)             it stops at once: a press, a key, the end of the list
*/
#define UI_DRAG_FRAME_NS 16000000l
#define UI_DRAG_THROW 300   // the least speed a finger lets go at that throws the list
#define UI_DRAG_STILL 20    // the speed a throw stops at
#define UI_DRAG_SLOW_NS 325000000l // a throw's speed falls by e in this long, near enough: a fifth of it every 65 ms

struct ui_drag
{
        int last;          // where the finger was at the last move
        long speed;        // pixels a second, signed
        long owed;         // a throw's move not yet given, in pixel-nanoseconds
        positive at;       // when the last move or step was
        b32 flying;
};

static fn ui_drag_start(struct ui_drag address_to drag, int at, positive now)
{
        drag->last = at, drag->speed = 0, drag->owed = 0, drag->at = now, drag->flying = false;
}

static int ui_drag_follow(struct ui_drag address_to drag, int at, positive now)
{
        int moved = at - drag->last;
        long gap = now > drag->at ? (long)(now - drag->at) : 0;

        // The speed is the moves of the last few reports, the newest weighed most.
        if (gap > 0)
        {
                long seen = (long)moved * 1000000000l / (gap < 4000000l ? 4000000l : gap);

                drag->speed = gap > 100000000l ? seen : (drag->speed * 2 + seen * 3) / 5;
        }
        drag->last = at;
        drag->at = now;
        return moved;
}

static b32 ui_drag_let_go(struct ui_drag address_to drag, positive now)
{
        // A finger that stopped before it lifted throws nothing.
        if (now > drag->at && now - drag->at > 80000000ul)
                drag->speed = 0;
        drag->flying = drag->speed >= UI_DRAG_THROW || drag->speed <= -UI_DRAG_THROW;
        drag->owed = 0;
        drag->at = now;
        return drag->flying;
}

static int ui_drag_step(struct ui_drag address_to drag, positive now)
{
        long gap = now > drag->at ? (long)(now - drag->at) : 0, whole;

        if (!drag->flying)
                return 0;
        gap = gap > 100000000l ? 100000000l : gap;
        drag->owed += drag->speed * gap;
        whole = drag->owed / 1000000000l;
        drag->owed -= whole * 1000000000l;
        // Slower by its share of the time, and by a pixel a second at least, so a slow throw ends too.
        {
                long slow = drag->speed * gap / UI_DRAG_SLOW_NS;

                drag->speed -= slow ? slow : drag->speed > 0 ? 1 : -1;
        }
        drag->at = now;
        if (drag->speed < UI_DRAG_STILL && drag->speed > -UI_DRAG_STILL)
                drag->flying = false;
        return (int)whole;
}

static fn ui_drag_stop(struct ui_drag address_to drag)
{
        drag->flying = false;
        drag->speed = 0;
        drag->owed = 0;
}


/* ---- kit: motion ---- */

/*
        The kit's motion, Dawning Native's Rubber Band standard (its ui.js and
        ui.css), to the number. A value is Q30 in a b64 (UI_ONE is one), a time
        is nanoseconds on the monotonic clock, and nothing here is floating
        point, so the same frame times make the same values, and the same
        pictures, on x86-64, arm64 and riscv64. The host check (test/checks.c,
        the kit rows) holds every curve below to numbers the kit's own
        JavaScript produced at the same frame times, at 60 Hz, at 120 Hz and at
        an uneven pace.

        The pieces, each the one in ui.js or ui.css it is named for:

            spring      P(): the release, a damped ring from --rb-spring-freq
                        4.905 rad/s and --rb-spring-decay 24.164 /s; a channel
                        rests below 0.0001 (a scale) or 0.05 px (a nudge), and
                        its clock starts at the first frame drawn after it is let go
            squash      C() and BB(): the pull of a drag, 400 px to the unit
            stretch     the domino: up 80 ms (ease out), held 15, back 210 (ease in)
            ripple      NB(): grows in 225 ms and travels to the middle in 425
                        on cubic-bezier(.4,0,.2,1) solved as ui.js solves it, is
                        in for 75 ms, held to 225 ms after the press and out in 150
            tween       a CSS transition: a cubic-bezier solved as Chrome
                        solves it, starting at the first frame after the change,
                        and turned back the way the CSS Transitions spec turns
                        a transition back (the reversing shortening factor)
            keyframes   a CSS animation, its timing function within each interval
            slide       the slider's click: 120 ms plus 180 for the whole track,
                        ease-out cubic, snapped to the step, then the landing
                        squish (60 ms up, 220 back)
*/
#define UI_Q 30
#define UI_ONE (1ll << UI_Q)
#define UI_HALF (1ll << (UI_Q - 1))
#define UI_LN2 744261118ll                 // ln 2
#define UI_INV_LN2 1549082005ll            // 1 / ln 2
#define UI_PI_2_Q60 1811004864519280711ll  // pi / 2, in Q60
#define UI_TWO_OVER_PI 683565276ll         // 2 / pi
#define UI_MS 1000000ll                    // a millisecond in nanoseconds

static inline b64 ui_qmul(b64 a, b64 b)
{
        return (b64)(((b128)a * b + UI_HALF) >> UI_Q);
}

// a / b, both Q30, rounded to the nearest. A large a shifts less and b loses as many of its last places.
static inline b64 ui_qdiv(b64 a, b64 b)
{
        p64 n = (p64)(a < 0 ? -a : a), d = (p64)(b < 0 ? -b : b);
        int shift = UI_Q;

        while (shift > 0 && n >> (62 - shift))
                shift--;
        d >>= UI_Q - shift;
        if (!d)
                return 0;
        n = ((n << shift) + d / 2) / d;
        return (a < 0) != (b < 0) ? -(b64)n : (b64)n;
}

// A whole number of logical pixels, or a count, as Q30.
static inline b64 ui_q(b64 whole)
{
        return whole * UI_ONE;
}

// A span of nanoseconds as milliseconds in Q30; past a minute it is a minute.
static inline b64 ui_ms(b64 ns)
{
        ns = ns < 0 ? 0 : ns > 60000 * UI_MS ? 60000 * UI_MS : ns;
        return (ns << 24) / 15625; // 2^30 / 10^6 is 2^24 / 15625
}

// How far through a span of duration a time elapsed is, 0 to UI_ONE.
static inline b64 ui_progress(b64 elapsed, b64 duration)
{
        if (elapsed <= 0 || duration <= 0)
                return duration <= 0 ? UI_ONE : 0;
        if (elapsed >= duration)
                return UI_ONE;
        // Both under 2^33 ns (8 s), so the shifted time stays in 64 bits; longer spans lose their last places.
        while (duration >= 1ll << 33)
                duration >>= 1, elapsed >>= 1;
        return (elapsed << UI_Q) / duration;
}

// e to the -x, x 0 or more: x taken down by ln 2 till under it, the rest's series, and the halvings.
static b64 ui_exp_neg(b64 x)
{
        b64 n, r, sum = UI_ONE;

        if (x <= 0)
                return UI_ONE;
        if (x >= 22 * UI_ONE)
                return 0; // under half the last place
        n = (b64)(((b128)x * UI_INV_LN2) >> (2 * UI_Q));
        r = x - n * UI_LN2;
        while (r >= UI_LN2)
                r -= UI_LN2, n++;
        while (r < 0)
                r += UI_LN2, n--;
        for (int k = 14; k >= 1; k--)
                sum = UI_ONE - ui_qmul(r, sum) / k;
        return n ? (sum + (1ll << (n - 1))) >> n : sum;
}

// The sine and cosine of x radians, x 0 or more: x less the nearest quarter turn, and the two series.
static void ui_sincos(b64 x, b64 address_to sine, b64 address_to cosine)
{
        b64 k = (b64)(((b128)x * UI_TWO_OVER_PI + ((b128)1 << (2 * UI_Q - 1))) >> (2 * UI_Q));
        b64 r = (b64)((((b128)x << UI_Q) - (b128)k * UI_PI_2_Q60 + ((b128)1 << (UI_Q - 1))) >> UI_Q);
        b64 r2 = ui_qmul(r, r), s = UI_ONE, c = UI_ONE;

        for (int n = 16; n >= 2; n -= 2)
        {
                s = UI_ONE - ui_qmul(r2, s) / ((n + 1) * n);
                c = UI_ONE - ui_qmul(r2, c) / (n * (n - 1));
        }
        s = ui_qmul(r, s);
        switch (k & 3)
        {
        case 0: *sine = s, *cosine = c; break;
        case 1: *sine = c, *cosine = -s; break;
        case 2: *sine = -s, *cosine = -c; break;
        default: *sine = -c, *cosine = s; break;
        }
}

// The whole square root of n.
static p64 ui_isqrt(p64 n)
{
        p64 root = 0, bit = 1ull << 62;

        while (bit > n)
                bit >>= 2;
        while (bit)
        {
                if (n >= root + bit)
                        n -= root + bit, root = (root >> 1) + bit;
                else
                        root >>= 1;
                bit >>= 2;
        }
        return root;
}

// The square root of a Q30 value 0 or more, Q30: of x shifted by as much of 30 as it holds, and the rest after.
static inline b64 ui_qsqrt(b64 x)
{
        int shift = UI_Q;

        if (x <= 0)
                return 0;
        while (shift > 0 && (p64)x >> (63 - shift))
                shift -= 2;
        return (b64)(ui_isqrt((p64)x << shift) << ((UI_Q - shift) / 2));
}

/*
        A cubic-bezier timing function: x(t) and y(t) as polynomials, Q30. CSS's
        are solved for t as Chrome solves them (gfx::CubicBezier: Newton's steps
        from the straight guess, then halving the bracket), to under 1e-7 of x.
*/
struct ui_bezier
{
        b64 ax, bx, cx, ay, by, cy;
};

#define UI_BEZIER(x1, y1, x2, y2)                                                                                     \
        {(UI_ONE - 3 * (x1) - (3 * ((x2) - (x1)) - 3 * (x1))), (3 * ((x2) - (x1)) - 3 * (x1)), 3 * (x1),                \
         (UI_ONE - 3 * (y1) - (3 * ((y2) - (y1)) - 3 * (y1))), (3 * ((y2) - (y1)) - 3 * (y1)), 3 * (y1)}
#define UI_Q2(hundredths) ((b64)(hundredths) * UI_ONE / 100)

static const struct ui_bezier ui_ease = UI_BEZIER(UI_Q2(20), 0, 0, UI_ONE);                  // --ease, --ease-emphasized
static const struct ui_bezier ui_ease_rise = UI_BEZIER(UI_Q2(40), UI_Q2(65), UI_Q2(30), UI_ONE); // --rb-ease-rise
static const struct ui_bezier ui_ease_out = UI_BEZIER(0, 0, UI_Q2(58), UI_ONE);              // CSS ease-out
static const struct ui_bezier ui_ease_ripple = UI_BEZIER(UI_Q2(40), 0, UI_Q2(20), UI_ONE);   // ui.js i()

static inline b64 ui_bezier_x(const struct ui_bezier address_to e, b64 t)
{
        return ui_qmul(ui_qmul(ui_qmul(e->ax, t) + e->bx, t) + e->cx, t);
}

static inline b64 ui_bezier_y(const struct ui_bezier address_to e, b64 t)
{
        return ui_qmul(ui_qmul(ui_qmul(e->ay, t) + e->by, t) + e->cy, t);
}

static inline b64 ui_bezier_dx(const struct ui_bezier address_to e, b64 t)
{
        return ui_qmul(ui_qmul(3 * e->ax, t) + 2 * e->bx, t) + e->cx;
}

static b64 ui_bezier_at(const struct ui_bezier address_to e, b64 x)
{
        b64 t = x, low = 0, high = UI_ONE;

        if (x <= 0)
                return 0;
        if (x >= UI_ONE)
                return UI_ONE;
        for (int step = 0; step < 8; step++)
        {
                b64 off = ui_bezier_x(e, t) - x, slope = ui_bezier_dx(e, t);

                if (off >= -1 && off <= 1)
                        return ui_bezier_y(e, t);
                if (slope < 108) // 1e-7
                        break;
                t -= ui_qdiv(off, slope);
                t = t < 0 ? 0 : t > UI_ONE ? UI_ONE : t;
        }
        t = x;
        for (int step = 0; step < 48 && low < high; step++)
        {
                b64 at = ui_bezier_x(e, t);

                if (at - x >= -1 && at - x <= 1)
                        break;
                if (x > at)
                        low = t;
                else
                        high = t;
                t = (low + high) / 2;
        }
        return ui_bezier_y(e, t);
}

// ui.js's i(): the ripple's cubic-bezier(.4,0,.2,1) by five of Newton's steps from t = x, no bracket.
static b64 ui_ripple_ease(b64 x)
{
        const struct ui_bezier address_to e = &ui_ease_ripple;
        b64 t = x;

        if (x <= 0)
                return 0;
        if (x >= UI_ONE)
                return UI_ONE;
        for (int step = 0; step < 5; step++)
        {
                b64 off = ui_bezier_x(e, t) - x, slope = ui_bezier_dx(e, t);

                if (slope > -11 && slope < 11) // 1e-8
                        break;
                t -= ui_qdiv(off, slope);
        }
        return ui_bezier_y(e, t);
}

// ui.js's C(): how far a pull of k units squashes, 0.064 of it and 0.064 more that comes on fast.
static b64 ui_squash_curve(b64 k)
{
        b64 z = k < 0 ? -k : k;
        b64 out = ui_qmul(68719477, z) + ui_qmul(68719477, UI_ONE - ui_exp_neg(ui_qmul(z, 8844645117ll)));

        return k < 0 ? -out : out;
}

// BB(): a drag of dx, dy logical pixels (Q30) as the scale it squashes by along each axis and the nudge with it.
struct ui_squash
{
        b64 sx, sy, tx, ty; // scales less one, and nudges in logical pixels, Q30
};

static struct ui_squash ui_squash_of(b64 dx, b64 dy)
{
        b64 z = ui_squash_curve((dx < 0 ? -dx : dx) / 400), w = ui_squash_curve((dy < 0 ? -dy : dy) / 400);
        struct ui_squash out;

        out.sx = z - ui_qmul(w, UI_Q2(35));
        out.sy = w - ui_qmul(z, UI_Q2(35));
        out.tx = dx > 0 ? out.sx * 8 : dx < 0 ? -out.sx * 8 : 0;
        out.ty = dy > 0 ? out.sy * 8 : dy < 0 ? -out.sy * 8 : 0;
        return out;
}

/*
        P(): channels let go at amplitudes, the first a scale (at rest under
        0.0001) and the second a nudge (at rest under 0.05 px). The clock starts
        at the first frame drawn after the release, as ui.js's rAF does; a
        channel at rest is 0, and the spring is live while either is not.
*/
#define UI_SPRING_DECAY 25945897435ll // --rb-spring-decay 24.164
#define UI_SPRING_FREQ 5266703647ll   // --rb-spring-freq 4.905
#define UI_SPRING_UB 5289683473ll     // decay / freq, ui.js's UB
#define UI_REST_SCALE 107374ll        // 0.0001
#define UI_REST_PX 53687091ll         // 0.05

struct ui_spring
{
        b64 amplitude[2];
        b64 value[2];
        b64 start; // the first frame's time, 0 before it
        b32 live;
};

static fn ui_spring_go(struct ui_spring address_to s, b64 scale, b64 nudge)
{
        s->amplitude[0] = s->value[0] = scale;
        s->amplitude[1] = s->value[1] = nudge;
        s->start = 0;
        s->live = 1; // the first frame says whether any of it is past rest
}

static fn ui_spring_stop(struct ui_spring address_to s)
{
        s->live = 0;
        s->value[0] = s->value[1] = 0;
}

static b32 ui_spring_at(struct ui_spring address_to s, b64 now)
{
        b64 h, j, sine, cosine;
        b32 live = 0;

        if (!s->live)
                return 0;
        if (!s->start)
                s->start = now;
        h = ui_ms(now - s->start) / 1000;
        j = ui_exp_neg(ui_qmul(UI_SPRING_DECAY, h));
        ui_sincos(ui_qmul(UI_SPRING_FREQ, h), &sine, &cosine);
        for (int k = 0; k < 2; k++)
        {
                b64 a = s->amplitude[k], f = ui_qmul(j, ui_qmul(a, cosine) + ui_qmul(ui_qmul(UI_SPRING_UB, a), sine));
                b64 rest = k ? UI_REST_PX : UI_REST_SCALE;

                s->value[k] = f < rest && f > -rest ? 0 : f;
                live |= s->value[k] != 0;
        }
        s->live = live;
        return live;
}

/*
        The domino stretch(): a scale along one axis to most and back, called at
        a time (a cascade calls it late, by its place along the travel) and drawn
        from the first frame at or after it. 1 before and after.
*/
struct ui_stretch
{
        b64 at;   // when it was called, 0 for none
        b64 most; // the peak scale, Q30
};

static fn ui_stretch_go(struct ui_stretch address_to s, b64 at, b64 most)
{
        s->at = at ? at : 1;
        s->most = most;
}

static b64 ui_stretch_at(struct ui_stretch address_to s, b64 now, b32 address_to live)
{
        b64 e, p, u;

        if (!s->at)
                return UI_ONE;
        if (now < s->at)
        {
                *live = 1;
                return UI_ONE;
        }
        e = ui_ms(now - s->at);
        if (e < ui_q(80))
        {
                u = UI_ONE - e / 80;
                p = UI_ONE - ui_qmul(u, u);
        }
        else if (e < ui_q(95))
                p = UI_ONE;
        else if (e < ui_q(305))
        {
                u = UI_ONE - (e - ui_q(95)) / 210;
                p = ui_qmul(u, u);
        }
        else
        {
                s->at = 0;
                return UI_ONE;
        }
        *live = 1;
        return UI_ONE + ui_qmul(s->most - UI_ONE, p);
}

/*
        A CSS transition of one value. A change is taken at the first frame
        after it (the style change event), from the value the transition has
        then; a change back to where a running one came from is shortened by
        the spec's reversing shortening factor. kind says how a value moves:
        a number, or a colour (four 16-bit channels of 8.8, premultiplied).
*/
#define UI_TWEEN_NUMBER 0
#define UI_TWEEN_COLOUR 1

struct ui_tween
{
        b64 from, to, adjusted;   // the start, the end, and the reversing-adjusted start
        b64 start, duration;      // ns; start 0 is done (or never begun)
        b64 factor;               // the reversing shortening factor, Q30
        b64 want;                 // a change not yet taken
        b64 want_duration;
        const struct ui_bezier address_to ease;
        b32 pending, kind;
        b32 placed; // it has a value: CSS does not transition an element's first style, so the first value is put, not set
};

// A colour 0xaarrggbb as a tween's value: alpha and each channel premultiplied by it, 8.8 each, a 16-bit lane apiece.
static b64 ui_tween_colour(unsigned int argb)
{
        p64 a = argb >> 24, out = a << 56;

        for (int shift = 0; shift < 24; shift += 8)
                out |= (((p64)(argb >> shift & 255) * a * 256 + 127) / 255) << (shift * 2);
        return (b64)out;
}

// And back: 0xaarrggbb, the alpha divided out again, rounded.
static unsigned int ui_tween_argb(b64 value)
{
        p64 v = (p64)value, a = v >> 48 & 0xffff, alpha = (a + 128) >> 8;
        unsigned int out;

        if (!a)
                return 0;
        out = (unsigned int)(alpha > 255 ? 255 : alpha) << 24;
        for (int shift = 0; shift < 24; shift += 8)
        {
                p64 c = ((v >> (shift * 2) & 0xffff) * 255 + a / 2) / a;

                out |= (unsigned int)(c > 255 ? 255 : c) << shift;
        }
        return out;
}

static b64 ui_tween_mix(b32 kind, b64 from, b64 to, b64 eased)
{
        p64 out = 0;

        if (kind == UI_TWEEN_NUMBER)
                return from + ui_qmul(to - from, eased);
        for (int shift = 0; shift < 64; shift += 16)
        {
                b64 a = (b64)((p64)from >> shift & 0xffff), b = (b64)((p64)to >> shift & 0xffff);

                out |= (p64)(a + ui_qmul(b - a, eased)) << shift;
        }
        return (b64)out;
}

// Start where it is: a value with no transition running.
static fn ui_tween_put(struct ui_tween address_to t, b64 value, b32 kind)
{
        memory_zero(t, sizeof(*t));
        t->from = t->to = t->adjusted = t->want = value;
        t->kind = kind;
        t->factor = UI_ONE;
        t->placed = 1;
}

// The value wanted from now on, over duration ns on ease (0: at once).
static fn ui_tween_set(struct ui_tween address_to t, b64 value, b64 duration, const struct ui_bezier address_to ease)
{
        if (value == (t->pending ? t->want : t->to))
                return;
        t->want = value;
        t->want_duration = duration;
        t->ease = ease ? ease : &ui_ease;
        t->pending = 1;
}

// The value at a frame, taking a change first; live is set while it moves.
static b64 ui_tween_at(struct ui_tween address_to t, b64 now, b32 address_to live)
{
        b64 eased = UI_ONE, current;

        if (t->start)
                eased = ui_bezier_at(t->ease, ui_progress(now - t->start, t->duration));
        current = t->start ? ui_tween_mix(t->kind, t->from, t->to, eased) : t->to;
        if (t->pending)
        {
                b64 want = t->want, duration = t->want_duration;

                t->pending = 0;
                if (current == want || duration <= 0)
                {
                        t->from = t->to = t->adjusted = want;
                        t->start = 0;
                        t->factor = UI_ONE;
                        return want;
                }
                if (t->start && t->adjusted == want)
                {
                        // Back the way it came: the new one runs for the share the old had covered.
                        b64 factor = ui_qmul(eased, t->factor) + UI_ONE - t->factor;

                        factor = factor < 0 ? -factor : factor;
                        t->factor = factor > UI_ONE ? UI_ONE : factor;
                        t->adjusted = t->to;
                        t->duration = ui_qmul(duration, t->factor);
                }
                else
                {
                        t->factor = UI_ONE;
                        t->adjusted = current;
                        t->duration = duration;
                }
                t->from = current;
                t->to = want;
                t->start = now;
                eased = 0;
                current = t->from;
                if (t->duration <= 0)
                {
                        t->start = 0;
                        return t->to;
                }
        }
        if (t->start && now - t->start >= t->duration)
        {
                t->start = 0;
                return t->to;
        }
        if (t->start)
                *live = 1;
        return current;
}

// A tween that is done or never ran: its end, at once.
static inline b32 ui_tween_still(const struct ui_tween address_to t)
{
        return !t->start && !t->pending;
}

/*
        A CSS animation over stops (offset and value, Q30), its timing function
        within each interval as CSS applies it, from start (the first frame) for
        duration. The value at the end is the last stop's.
*/
struct ui_stop
{
        b64 at, value;
};

static b64 ui_keyframes_at(const struct ui_stop address_to stops, int count, const struct ui_bezier address_to ease, b64 start,
                           b64 duration, b64 now, b32 address_to live)
{
        b64 p = ui_progress(now - start, duration);

        if (!start)
                return stops[count - 1].value;
        if (p < UI_ONE)
                *live = 1;
        for (int at = 1; at < count; at++)
                if (p <= stops[at].at)
                {
                        b64 within = ui_qdiv(p - stops[at - 1].at, stops[at].at - stops[at - 1].at);

                        return stops[at - 1].value + ui_qmul(stops[at].value - stops[at - 1].value,
                                                             ease ? ui_bezier_at(ease, within) : within);
                }
        return stops[count - 1].value;
}

/*
        NB()'s ripple, for a press at x, y (Q30 logical pixels) in a box of w by
        h whose clip reaches margin past it on every side (8 for a button). Its
        size reaches the clip's farthest corner from the box's middle. The clock
        starts at the first frame; exit is when the pointer went up or away.
*/
struct ui_ripple
{
        b64 x, y, w, h, margin;   // the press and the box, Q30 logical pixels
        b64 reach;                // the radius at full size
        b64 start, exit, hold;    // ns: the first frame, the exit (0 before it), what the exit waits
        b32 live;
};

static fn ui_ripple_go(struct ui_ripple address_to r, b64 x, b64 y, b64 w, b64 h, b64 margin)
{
        b64 f = margin + w / 2, v = margin + h / 2, across = w + 2 * margin, down = h + 2 * margin;
        b64 rx = f > across - f ? f : across - f, ry = v > down - v ? v : down - v;

        r->x = x, r->y = y, r->w = w, r->h = h, r->margin = margin;
        r->reach = ui_qsqrt(ui_qmul(rx, rx) + ui_qmul(ry, ry));
        r->start = r->exit = r->hold = 0;
        r->live = 1;
}

static fn ui_ripple_exit(struct ui_ripple address_to r, b64 now)
{
        b64 shown;

        if (!r->live || r->exit)
                return;
        r->exit = now;
        shown = r->start ? now - r->start : 0;
        r->hold = shown > 0 && shown < 225 * UI_MS ? 225 * UI_MS - shown : 0;
}

// The ripple at a frame: its middle in the clip's own place (x, y from the clip's top left), its radius and its
// opacity (Q30, the 0.2 of the kit included). False when it is gone.
static b32 ui_ripple_at(struct ui_ripple address_to r, b64 now, b64 address_to cx, b64 address_to cy, b64 address_to radius,
                        b64 address_to opacity)
{
        b64 h, grow, travel, f, v, a;

        if (!r->live)
                return 0;
        if (!r->start)
                r->start = now;
        h = now - r->start;
        grow = ui_ripple_ease(ui_progress(h, 225 * UI_MS));
        travel = ui_ripple_ease(ui_progress(h, 425 * UI_MS));
        f = r->margin + r->w / 2;
        v = r->margin + r->h / 2;
        *cx = f + ui_qmul(r->x + r->margin - f, UI_ONE - travel);
        *cy = v + ui_qmul(r->y + r->margin - v, UI_ONE - travel);
        *radius = ui_qmul(r->reach, grow);
        if (!r->exit)
                a = ui_progress(h, 75 * UI_MS);
        else
        {
                b64 n = now - r->exit - r->hold;

                a = n < 0 ? UI_ONE : UI_ONE - ui_progress(n, 150 * UI_MS);
        }
        *opacity = ui_qmul(a, UI_Q2(20));
        if (r->exit && a <= 0)
        {
                r->live = 0;
                return 0;
        }
        return 1;
}

/*
        The slider's click (its pointerdown handler): the value slides from
        where it was to the value clicked over 120 ms and 180 more for the whole
        track, ease-out cubic, and on the release the bar stretches toward the
        throw by a peak of 0.015 and 0.04 of the share of the track (at most
        0.045): 60 ms up, ease out, then 220 back, cubic.
*/
struct ui_slide
{
        b64 from, to;      // values, Q30
        b64 at, duration;  // ns: the press, and how long
        b64 peak;          // the landing's peak, Q30
        b64 landed;        // ns: the release, 0 for none
};

static fn ui_slide_go(struct ui_slide address_to s, b64 from, b64 to, b64 span, b64 at)
{
        b64 jump = to > from ? to - from : from - to, ratio = ui_qdiv(jump, span ? span : UI_ONE);
        b64 peak = UI_Q2(1) + UI_Q2(1) / 2 + ui_qmul(ratio, UI_Q2(4));

        s->from = from, s->to = to, s->at = at ? at : 1, s->landed = 0;
        s->duration = 120 * UI_MS + ui_qmul(ratio, 180 * UI_MS);
        s->peak = peak > UI_Q2(4) + UI_Q2(1) / 2 ? UI_Q2(4) + UI_Q2(1) / 2 : peak;
}

// The value the slide has reached at a frame: ease-out cubic of the time.
static b64 ui_slide_value(const struct ui_slide address_to s, b64 now, b32 address_to live)
{
        b64 p = ui_progress(now - s->at, s->duration), u = UI_ONE - p;

        if (p < UI_ONE)
                *live = 1;
        return s->from + ui_qmul(s->to - s->from, UI_ONE - ui_qmul(ui_qmul(u, u), u));
}

// The landing squish at a frame, a scale (UI_ONE at rest).
static b64 ui_slide_land(struct ui_slide address_to s, b64 now, b32 address_to live)
{
        b64 e, u;

        if (!s->landed)
                return UI_ONE;
        e = ui_ms(now - s->landed);
        if (e < ui_q(60))
        {
                u = UI_ONE - e / 60;
                *live = 1;
                return UI_ONE + ui_qmul(s->peak, UI_ONE - ui_qmul(u, u));
        }
        if (e < ui_q(280))
        {
                u = UI_ONE - (e - ui_q(60)) / 220;
                *live = 1;
                return UI_ONE + ui_qmul(s->peak, ui_qmul(ui_qmul(u, u), u));
        }
        s->landed = 0;
        return UI_ONE;
}

/* ---- end of the kit's motion ---- */

/* ---- kit: theme ---- */

/*
        The kit's colours, by role (the Material role names its ui.css maps
        onto), made from two choices: light or dark, and an accent. Light with
        the accent ff8800 is Dawning's palette as styles-core.css and ui.css
        have it, every value as written there; another accent derives the
        roles that follow it (its container, the ink on it, the bright tint
        the focus glow and the controls' off colour are mixed from) with CSS
        color-mix in srgb, and dark is a palette of its own on the same neutral
        ramp. The ink on a fill is whichever of the kit's ink and white stands
        out more by WCAG's contrast ratio; where neither reaches 4.5:1 (an
        accent of middle lightness) it is black or white, which always do. A
        colour is 0xaarrggbb, not premultiplied.

        `moonwater canvas theme` and `canvas theme accent` keep the choices;
        the compositor puts them in the window table's header and knocks, and a
        window turns to the new colours over the kit's .2s --ease, as the kit's
        own background and colour transitions take them.
*/
#define UI_PRIMARY 0
#define UI_ON_PRIMARY 1
#define UI_PRIMARY_CONTAINER 2
#define UI_ON_PRIMARY_CONTAINER 3
#define UI_SECONDARY_CONTAINER 4
#define UI_ON_SECONDARY_CONTAINER 5
#define UI_SURFACE 6
#define UI_SURFACE_DIM 7
#define UI_SURFACE_LOWEST 8
#define UI_SURFACE_LOW 9
#define UI_SURFACE_CONTAINER 10
#define UI_SURFACE_HIGH 11
#define UI_SURFACE_HIGHEST 12
#define UI_ON_SURFACE 13
#define UI_ON_SURFACE_VARIANT 14
#define UI_OUTLINE 15
#define UI_OUTLINE_VARIANT 16
#define UI_ERROR 17
#define UI_ON_ERROR 18
#define UI_ERROR_CONTAINER 19
#define UI_ON_ERROR_CONTAINER 20
#define UI_SUCCESS 21
#define UI_SUCCESS_CONTAINER 22
#define UI_ON_SUCCESS_CONTAINER 23
#define UI_WARNING 24
#define UI_WARNING_CONTAINER 25
#define UI_ON_WARNING_CONTAINER 26
#define UI_INFO 27
#define UI_INFO_CONTAINER 28
#define UI_ON_INFO_CONTAINER 29
#define UI_CONTROL_OFF 30   // the rest of a switch, a checkbox, a radio and a slider: the bright accent at 14% on paper
#define UI_ACCENT_BRIGHT 31 // --dawn-400: the focus glow, the selection and the controls' off colour are made from it
#define UI_SELECTION 32     // the bright accent at 28%
#define UI_SHADOW 33        // the ink of shadows, at the alphas the kit gives them
#define UI_HIGHLIGHT 34     // the light a raised field catches on its top edge
#define UI_ROLES 35

#define UI_THEME_LIGHT 0u
#define UI_THEME_DARK 1u
#define UI_THEME_AUTO 2u
#define UI_ACCENT_DAWN 0xff8800u // --dawn-500, the mark: one accent of the choices
#define UI_ACCENT_STANDARD 0xffffffu // the accent when none is chosen: white, on the dark theme (the standard)
#define UI_INK 0xff18181bu       // --ink

struct ui_theme
{
        unsigned int role[UI_ROLES];
};

// Dawning light, as styles-core.css and ui.css write it.
static const struct ui_theme ui_theme_dawning = {{
    0xffff8800, 0xff18181b, 0xffffe8cc, 0xff4a2600, 0xffededef, 0xff1c1c1f, 0xfff7f7f8, 0xffededef, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffededef, 0xff18181b, 0xff52525b, 0xffa1a1aa, 0xffe4e4e7, 0xffdc2e26, 0xffffffff, 0xfffce4e2,
    0xff5c1512, 0xff0d7f49, 0xffe3f3ea, 0xff0b3d23, 0xff9c6b00, 0xfff7ecd4, 0xff4a330a, 0xff1f5fd0, 0xffe2ecfb, 0xff143a7a,
    0xfffff2e2, 0xffff9f33, 0x47ff9f33, 0xff101014, 0xffffffff,
}};

/*
        Dark, a design and not the light one turned over: paper is the ramp's
        900, a card 850, a field's face 800 and a well or a track 700, text
        the ramp's 100 and its quiet the 300; the status colours are lifted to
        read on the dark and their containers are their deep colour at a
        quarter on the field. The accent's roles are mixed into it when made.
*/
static const struct ui_theme ui_theme_night = {{
    0xffff8800, 0xff18181b, 0xff47290c, 0xffffd199, 0xff2a2a30, 0xffededef, 0xff111113, 0xff0a0a0b, 0xff0a0a0b, 0xff161618,
    0xff1c1c1f, 0xff1c1c1f, 0xff2a2a30, 0xffededef, 0xffa1a1aa, 0xff52525b, 0xff2a2a30, 0xffff6b63, 0xff18181b, 0xff4a1d1c,
    0xffffb4ae, 0xff4cc38a, 0xff123826, 0xffa6e9c5, 0xffe5b54a, 0xff3d3012, 0xfff5d99a, 0xff7aa7ff, 0xff1b2c4f, 0xffbcd3ff,
    0xff3a2f27, 0xffff9f33, 0x47ff9f33, 0xff000000, 0x0fffffff,
}};

// sRGB to linear light, 0 to 65536, for WCAG's relative luminance.
static const unsigned int ui_linear[256] = {
        0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219, 241, 264, 288, 313,
        340, 367, 396, 427, 458, 491, 526, 562, 599, 637, 677, 718, 761, 805, 851, 898,
        947, 997, 1048, 1101, 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1791, 1863,
        1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258,
        3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129, 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124,
        5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246, 6395, 6547, 6701, 6856, 7014, 7174, 7336, 7500,
        7666, 7834, 8004, 8177, 8352, 8529, 8708, 8889, 9072, 9258, 9446, 9636, 9828, 10022, 10219, 10418,
        10619, 10822, 11028, 11236, 11446, 11658, 11873, 12090, 12309, 12531, 12754, 12981, 13209, 13440, 13673, 13909,
        14147, 14387, 14629, 14874, 15122, 15372, 15624, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
        18278, 18556, 18838, 19121, 19408, 19696, 19988, 20281, 20578, 20876, 21178, 21481, 21788, 22096, 22408, 22722,
        23038, 23357, 23679, 24003, 24329, 24659, 24991, 25325, 25662, 26002, 26344, 26689, 27036, 27387, 27739, 28095,
        28453, 28813, 29177, 29543, 29911, 30283, 30657, 31033, 31413, 31795, 32180, 32567, 32957, 33350, 33746, 34144,
        34545, 34949, 35355, 35765, 36177, 36591, 37009, 37429, 37852, 38278, 38707, 39138, 39572, 40009, 40449, 40892,
        41337, 41786, 42237, 42691, 43147, 43607, 44069, 44534, 45003, 45474, 45947, 46424, 46904, 47386, 47871, 48360,
        48851, 49345, 49842, 50342, 50844, 51350, 51859, 52370, 52884, 53402, 53922, 54445, 54972, 55501, 56033, 56568,
        57106, 57647, 58191, 58738, 59288, 59841, 60397, 60956, 61518, 62083, 62651, 63222, 63796, 64373, 64953, 65536,
};

// WCAG's relative luminance of an opaque colour, 0 to 65536.
static unsigned int ui_luminance(unsigned int rgb)
{
        return (2126u * ui_linear[rgb >> 16 & 255] + 7152u * ui_linear[rgb >> 8 & 255] + 722u * ui_linear[rgb & 255] + 5000) / 10000;
}

// The contrast ratio of two opaque colours, in hundredths (700 is 7:1).
static unsigned int ui_contrast(unsigned int one, unsigned int two)
{
        unsigned int a = ui_luminance(one) + 3277, b = ui_luminance(two) + 3277; // + 0.05

        return a > b ? a * 100 / b : b * 100 / a;
}

/*
        color-mix(in srgb, one percent%, two): each colour premultiplied by its
        alpha, the two mixed channel by channel, and the alpha divided out
        again, rounded to the nearest; mixing with transparent keeps one's
        colour at percent of its alpha.
*/
static unsigned int ui_mix(unsigned int one, unsigned int percent, unsigned int two)
{
        unsigned int a1 = one >> 24, a2 = two >> 24, rest = 100 - percent;
        unsigned int alpha = a1 * percent + a2 * rest; // hundredths of an 8-bit alpha
        unsigned int out;

        if (!alpha)
                return 0;
        out = (alpha + 50) / 100 << 24;
        for (int shift = 0; shift < 24; shift += 8)
        {
                unsigned int c = ((one >> shift & 255) * a1 * percent + (two >> shift & 255) * a2 * rest + alpha / 2) / alpha;

                out |= (c > 255 ? 255 : c) << shift;
        }
        return out;
}

// The ink to write on a fill: the kit's ink or white, whichever stands out more; black or white where neither reaches 4.5:1.
static unsigned int ui_ink_on(unsigned int fill)
{
        unsigned int ink = ui_contrast(UI_INK, fill), white = ui_contrast(0xffffffffu, fill);

        if (ink >= 450 || white >= 450)
                return ink >= white ? UI_INK : 0xffffffffu;
        return ui_contrast(0xff000000u, fill) >= white ? 0xff000000u : 0xffffffffu;
}

// An ink for text on a container, from a start mixed toward black (or white on dark) till it stands out 4.5:1.
static unsigned int ui_ink_deepened(unsigned int start, unsigned int container, unsigned int toward)
{
        unsigned int ink = start;

        for (int step = 0; step < 24 && ui_contrast(ink, container) < 450; step++)
                ink = ui_mix(ink, 85, toward);
        return ink;
}

// The roles for light or dark and an accent (0xrrggbb).
static fn ui_theme_make(struct ui_theme address_to theme, unsigned int dark, unsigned int accent)
{
        unsigned int fill = 0xff000000u | (accent & 0xffffffu), bright;

        *theme = dark ? ui_theme_night : ui_theme_dawning;
        if (!dark && (accent & 0xffffffu) == UI_ACCENT_DAWN)
                return; // the palette as written, to the value
        // An accent that does not stand out from the surfaces (white on the light theme) fills with the theme's ink.
        if (ui_contrast(fill, theme->role[UI_SURFACE_HIGH]) < 150)
                fill = theme->role[UI_ON_SURFACE];
        bright = (accent & 0xffffffu) == UI_ACCENT_DAWN ? 0xffff9f33u : ui_mix(fill, 80, 0xffffffffu);
        theme->role[UI_PRIMARY] = fill;
        theme->role[UI_ON_PRIMARY] = ui_ink_on(fill);
        theme->role[UI_ACCENT_BRIGHT] = bright;
        theme->role[UI_SELECTION] = ui_mix(bright, 28, 0);
        if (dark)
        {
                theme->role[UI_PRIMARY_CONTAINER] = ui_mix(fill, 22, theme->role[UI_SURFACE_HIGH]);
                theme->role[UI_ON_PRIMARY_CONTAINER] = ui_ink_deepened(ui_mix(fill, 40, 0xffffffffu), theme->role[UI_PRIMARY_CONTAINER], 0xffffffffu);
                theme->role[UI_CONTROL_OFF] = ui_mix(bright, 14, theme->role[UI_SURFACE_HIGHEST]);
        }
        else
        {
                theme->role[UI_PRIMARY_CONTAINER] = ui_mix(fill, 20, 0xffffffffu);
                theme->role[UI_ON_PRIMARY_CONTAINER] = ui_ink_deepened(ui_mix(fill, 29, 0xff000000u), theme->role[UI_PRIMARY_CONTAINER], 0xff000000u);
                theme->role[UI_CONTROL_OFF] = ui_mix(bright, 14, 0xffffffffu);
        }
}

/*
        What one process draws with: the theme as the table last said it and
        the colours shown this frame, and the frame's clock. A frame sets
        busy when anything in it is still moving, and the loop asks for the
        next frame a display period on; nothing moving, it sleeps.
*/
static struct ui_kit
{
        struct ui_theme shown, from, to;
        struct ui_tween turn;              // 0 to UI_ONE while the theme turns
        unsigned int mode, accent, motion; // UI_THEME_LIGHT or _DARK, 0xrrggbb, 0 full or 1 reduced
        unsigned int themed;               // the table's theme count as last taken
        b64 now;                           // the frame's time, ns
        b64 drawn;                         // the last frame's time: the next is a display period after it
        b32 busy;                          // something moved: another frame is due
        b32 made;
        int damage_top, damage_bottom;     // the rows (device pixels) the moving controls drew this frame
        b64 epoch;                         // counted up when every control is to be placed again (ui_kit_settle)
} ui_kit;

/*
        Everything is placed again, not moved: the window was resized, the
        theme or the scale changed. A control drawn after this is drawn at its
        state, its transitions and springs put at their ends (ui_control_place).
*/
static fn ui_kit_settle(void)
{
        ui_kit.epoch++;
}

// The colour of a role this frame.
static inline unsigned int ui_role(int role)
{
        return ui_kit.shown.role[role];
}

// The window's surface, opaque: what a kit window is cleared to.
static unsigned int ui_role_surface(void)
{
        return ui_kit.shown.role[UI_SURFACE] | 0xff000000u;
}

// A kit window's inks: those that stand out from its surface (the rows' text, a header, the focus).
static fn ui_kit_inks(struct ui address_to ui)
{
        if (ui->opaque)
                ui_inks(ui, ui_kit.to.role[UI_SURFACE] & 0xffffffu);
}

// The theme to turn to: at once the first time, and over .2s --ease after (at once with reduced motion).
static fn ui_kit_theme(unsigned int mode, unsigned int accent, unsigned int motion)
{
        ui_kit.motion = motion;
        if (ui_kit.made && mode == ui_kit.mode && (accent & 0xffffffu) == ui_kit.accent)
                return;
        ui_kit.mode = mode;
        ui_kit.accent = accent & 0xffffffu;
        ui_kit.epoch += 1 + !ui_kit.epoch; // the controls are placed in the new colours; only the theme's own turn moves
        ui_kit.from = ui_kit.shown;
        ui_theme_make(&ui_kit.to, mode == UI_THEME_DARK, ui_kit.accent);
        if (!ui_kit.made || motion)
        {
                ui_kit.shown = ui_kit.from = ui_kit.to;
                ui_tween_put(&ui_kit.turn, UI_ONE, UI_TWEEN_NUMBER);
                ui_kit.made = 1;
                return;
        }
        ui_tween_put(&ui_kit.turn, 0, UI_TWEEN_NUMBER);
        ui_tween_set(&ui_kit.turn, UI_ONE, 200 * UI_MS, &ui_ease);
}

// The theme's colours at this frame: a turn under way mixes every role from the old to the new.
static fn ui_kit_turn(void)
{
        b32 live = 0;
        b64 p;

        if (ui_tween_still(&ui_kit.turn))
                return;
        p = ui_tween_at(&ui_kit.turn, ui_kit.now, &live);
        for (int role = 0; role < UI_ROLES; role++)
                ui_kit.shown.role[role] = ui_tween_argb(ui_tween_mix(UI_TWEEN_COLOUR, ui_tween_colour(ui_kit.from.role[role]),
                                                                     ui_tween_colour(ui_kit.to.role[role]), p));
        if (live)
                ui_kit.busy = 1;
        else
                ui_kit.shown = ui_kit.to;
}

/* ---- end of the kit's theme ---- */

/* ---- kit: shapes ---- */

/*
        The kit's shapes, drawn with their edges where they fall: a rounded
        rectangle whose corners are ellipses (a squashed button's are), a
        circle, a pill, a ring as one less another. Places are 256ths of a
        device pixel and are never rounded to a pixel while a shape moves.
        Across a row the cover is exact; down it a row is sixteen lines of
        it, so an edge that is near flat is in sixteen steps. Text is not
        drawn here: it is ui_run's, at whole pixels, and a stretched control's
        label is moved with it (rounded) and never resampled.
*/
#define UI_SUB 16                 // lines a row is taken in
#define UI_FULL (256 * UI_SUB)    // the cover of a pixel wholly inside
#define UI_KIT_LINE 8192          // the widest a shape is drawn, in device pixels

struct ui_box
{
        b32 x0, y0, x1, y1; // the edges, 256ths of a device pixel
        b32 rx, ry;         // each corner's radii
};

static unsigned short ui_kit_line[UI_KIT_LINE], ui_kit_clip[UI_KIT_LINE];

// A box from a logical rectangle (Q30 logical pixels) at the window's scale, with a corner radius (Q30, logical).
static struct ui_box ui_box_of(const struct ui address_to ui, b64 x, b64 y, b64 w, b64 h, b64 radius)
{
        struct ui_box box;
        b64 s = ui->scale;

        box.x0 = (b32)((x * s) >> 22);
        box.y0 = (b32)((y * s) >> 22);
        box.x1 = (b32)(((x + w) * s) >> 22);
        box.y1 = (b32)(((y + h) * s) >> 22);
        box.rx = box.ry = (b32)((radius * s) >> 22);
        return box;
}

// A box scaled by sx, sy (Q30) about the point ox, oy (256ths), then moved by tx, ty (256ths): a CSS transform
// translate() scaleX() scaleY() with its origin. The corners scale with it, into ellipses.
static struct ui_box ui_box_transform(struct ui_box box, b64 sx, b64 sy, b32 ox, b32 oy, b32 tx, b32 ty)
{
        struct ui_box out;

        out.x0 = ox + tx + (b32)ui_qmul(box.x0 - ox, sx);
        out.x1 = ox + tx + (b32)ui_qmul(box.x1 - ox, sx);
        out.y0 = oy + ty + (b32)ui_qmul(box.y0 - oy, sy);
        out.y1 = oy + ty + (b32)ui_qmul(box.y1 - oy, sy);
        out.rx = (b32)ui_qmul(box.rx, sx);
        out.ry = (b32)ui_qmul(box.ry, sy);
        return out;
}

// A box's radii as CSS draws them: where two corners' radii come to more than the side, every radius is scaled by
// the one factor that fits them all, both of each corner's.
static fn ui_box_radii(const struct ui_box address_to box, b32 address_to rx, b32 address_to ry)
{
        p64 w = (p64)(box->x1 - box->x0), h = (p64)(box->y1 - box->y0), f = 65536;

        if (*rx > 0 && 2 * (p64)*rx > w)
                f = (w << 16) / (2 * (p64)*rx);
        if (*ry > 0 && 2 * (p64)*ry > h && (h << 16) / (2 * (p64)*ry) < f)
                f = (h << 16) / (2 * (p64)*ry);
        if (f < 65536)
        {
                *rx = (b32)(((p64)*rx * f) >> 16);
                *ry = (b32)(((p64)*ry * f) >> 16);
        }
}

// One line's span of a box, at y (256ths): false when the line misses it.
static b32 ui_box_span(const struct ui_box address_to box, b32 y, b32 address_to left, b32 address_to right)
{
        b32 dy = 0, rx = box->rx, ry = box->ry;

        if (y < box->y0 || y >= box->y1 || box->x1 <= box->x0)
                return 0;
        ui_box_radii(box, &rx, &ry);
        if (y < box->y0 + ry)
                dy = box->y0 + ry - y;
        else if (y >= box->y1 - ry)
                dy = y - (box->y1 - ry) + 1;
        if (dy > 0 && ry > 0 && rx > 0)
        {
                p64 u = ((p64)dy << 16) / (p64)ry;
                b32 dx = u >= 65536 ? 0 : (b32)(((p64)rx * ui_isqrt((1ull << 32) - u * u)) >> 16);

                *left = box->x0 + rx - dx;
                *right = box->x1 - rx + dx;
                return *right > *left;
        }
        *left = box->x0;
        *right = box->x1;
        return 1;
}

// A span's cover added into a line, from pixel first (the line's 0), weight lines of it.
static fn ui_line_add(unsigned short address_to line, b32 first, b32 width, b32 left, b32 right, b32 weight)
{
        b32 p0, p1;

        left -= first * 256;
        right -= first * 256;
        left = left < 0 ? 0 : left;
        right = right > width * 256 ? width * 256 : right;
        if (right <= left)
                return;
        p0 = left >> 8;
        p1 = (right - 1) >> 8;
        if (p0 == p1)
        {
                line[p0] += (unsigned short)((right - left) * weight);
                return;
        }
        line[p0] += (unsigned short)((256 - (left & 255)) * weight);
        for (b32 p = p0 + 1; p < p1; p++)
                line[p] += (unsigned short)(256 * weight);
        line[p1] += (unsigned short)((right - p1 * 256) * weight);
}

// The cover of a box over the row y of pixels, into line from pixel first, width wide: false when none.
static b32 ui_box_row(const struct ui_box address_to box, b32 y, unsigned short address_to line, b32 first, b32 width)
{
        b32 top = y * 256, bottom = top + 256, ry = box->ry, any = 0;

        if (bottom <= box->y0 || top >= box->y1)
                return 0;
        {
                b32 rx = box->rx;

                ui_box_radii(box, &rx, &ry);
        }
        // A row in the straight middle, all of it inside: one span, every line.
        if (top >= box->y0 + ry && bottom <= box->y1 - ry && top >= box->y0 && bottom <= box->y1)
        {
                ui_line_add(line, first, width, box->x0, box->x1, UI_SUB);
                return 1;
        }
        for (b32 sub = 0; sub < UI_SUB; sub++)
        {
                b32 left, right;

                if (ui_box_span(box, top + sub * (256 / UI_SUB) + 256 / UI_SUB / 2, &left, &right))
                {
                        ui_line_add(line, first, width, left, right, 1);
                        any = 1;
                }
        }
        return any;
}

// A straight colour (0xaarrggbb) at a cover of UI_FULL, as the premultiplied pixel ui_over takes.
static inline unsigned int ui_premultiplied(unsigned int argb, unsigned int cover)
{
        unsigned int a = (argb >> 24) * cover / UI_FULL;

        return a << 24 | window_mix(0, argb & 0xffffffu, a + (a >> 7));
}

// Whether the row y is in the straight middle of a box (its radii as drawn, ry): every line of it is the one span.
static inline b32 ui_box_straight(const struct ui_box address_to box, b32 ry, b32 y)
{
        b32 top = y * 256, bottom = top + 256;

        return top >= box->y0 + ry && bottom <= box->y1 - ry && top >= box->y0 && bottom <= box->y1;
}

static inline b32 ui_box_ry(const struct ui_box address_to box)
{
        b32 rx = box->rx, ry = box->ry;

        ui_box_radii(box, &rx, &ry);
        return ry;
}

/*
        A span of a straight row, left to right (256ths, from pixel first), clamped to width pixels, as the line its
        cover would make: the pixels it starts and ends in by their share, those between it wholly. Answers its first and
        last pixel and their covers (UI_FULL a pixel wholly in it), or false when it covers none.
*/
static inline b32 ui_span_ends(b32 first, b32 width, b32 left, b32 right, b32 address_to p0, b32 address_to p1, unsigned int address_to c0,
                               unsigned int address_to c1)
{
        left -= first * 256, right -= first * 256;
        left = left < 0 ? 0 : left;
        right = right > width * 256 ? width * 256 : right;
        if (right <= left)
                return false;
        *p0 = left >> 8, *p1 = (right - 1) >> 8;
        if (*p0 == *p1)
                *c0 = *c1 = (unsigned int)(right - left) * UI_SUB;
        else
                *c0 = (unsigned int)(256 - (left & 255)) * UI_SUB, *c1 = (unsigned int)(right - *p1 * 256) * UI_SUB;
        return true;
}

/*
        A box filled with a colour, cut to clip when there is one (a ripple to its
        button): over the picture, in the rows the ui may write. A row in the box's
        straight middle is its one span, laid on without the cover line: the same
        pixels, without a pass over them for each.
*/
static fn ui_kit_fill(struct ui address_to ui, const struct ui_box address_to box, unsigned int argb, const struct ui_box address_to clip)
{
        b32 first = box->x0 >> 8, last = (box->x1 + 255) >> 8, top = box->y0 >> 8, bottom = (box->y1 + 255) >> 8, ry = ui_box_ry(box);
        unsigned int whole = argb >> 24 == 255 ? argb | 0xff000000u : ui_premultiplied(argb, UI_FULL);

        if (!(argb >> 24))
                return;
        first = first < 0 ? 0 : first;
        last = last > ui->width ? ui->width : last;
        if (clip)
        {
                first = first < clip->x0 >> 8 ? clip->x0 >> 8 : first;
                last = last > (clip->x1 + 255) >> 8 ? (clip->x1 + 255) >> 8 : last;
        }
        if (last - first > UI_KIT_LINE)
                last = first + UI_KIT_LINE;
        if (last <= first)
                return;
        for (b32 y = top; y < bottom; y++)
        {
                unsigned int address_to row;
                b32 width = last - first;

                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                if (!clip && ui_box_straight(box, ry, y))
                {
                        b32 p0, p1;
                        unsigned int c0, c1;

                        if (!ui_span_ends(first, width, box->x0, box->x1, &p0, &p1, &c0, &c1))
                                continue;
                        row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                        for (b32 x = p0; x <= p1; x++)
                        {
                                unsigned int cover = x == p0 ? c0 : x == p1 ? c1 : UI_FULL;

                                if (cover >= UI_FULL && argb >> 24 == 255)
                                        row[x] = whole;
                                else if (cover >= UI_FULL)
                                        row[x] = ui_over(whole, row[x]);
                                else if (cover)
                                        row[x] = ui_over(ui_premultiplied(argb, cover), row[x]);
                        }
                        continue;
                }
                if (!ui_box_row(box, y, ui_kit_line, first, width))
                        continue;
                if (clip && !ui_box_row(clip, y, ui_kit_clip, first, width))
                {
                        memory_zero(ui_kit_line, (positive)width * sizeof(ui_kit_line[0]));
                        continue;
                }
                row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                for (b32 x = 0; x < width; x++)
                {
                        unsigned int cover = ui_kit_line[x];

                        if (clip)
                        {
                                cover = cover * ui_kit_clip[x] / UI_FULL;
                                ui_kit_clip[x] = 0;
                        }
                        ui_kit_line[x] = 0;
                        if (!cover)
                                continue;
                        if (cover >= UI_FULL && argb >> 24 == 255)
                                row[x] = argb | 0xff000000u;
                        else
                                row[x] = ui_over(ui_premultiplied(argb, cover > UI_FULL ? UI_FULL : cover), row[x]);
                }
        }
}

// A ring: the box less the box inset by thickness (256ths), in one colour (an outline, a hairline).
static fn ui_kit_ring(struct ui address_to ui, const struct ui_box address_to box, b32 thickness, unsigned int argb)
{
        struct ui_box inner = *box;
        b32 first = box->x0 >> 8, last = (box->x1 + 255) >> 8, top = box->y0 >> 8, bottom = (box->y1 + 255) >> 8;

        b32 ry, inner_ry;

        inner.x0 += thickness, inner.y0 += thickness, inner.x1 -= thickness, inner.y1 -= thickness;
        inner.rx = inner.rx > thickness ? inner.rx - thickness : 0;
        inner.ry = inner.ry > thickness ? inner.ry - thickness : 0;
        ry = ui_box_ry(box), inner_ry = ui_box_ry(&inner);
        first = first < 0 ? 0 : first;
        last = last > ui->width ? ui->width : last;
        if (last - first > UI_KIT_LINE)
                last = first + UI_KIT_LINE;
        for (b32 y = top; y < bottom && last > first; y++)
        {
                unsigned int address_to row;
                b32 width = last - first;

                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                // A row straight in the box, past the inner box or straight in it too, its two bands apart: their spans alone.
                if (ui_box_straight(box, ry, y))
                {
                        b32 above = (y + 1) * 256 <= inner.y0 || y * 256 >= inner.y1 || inner.x1 <= inner.x0;
                        b32 spans[2][2] = {{box->x0, inner.x0}, {inner.x1, box->x1}};
                        int count = above ? 1 : 2;

                        if (above)
                                spans[0][1] = box->x1;
                        if (above || (ui_box_straight(&inner, inner_ry, y) && (inner.x0 - 1) >> 8 < inner.x1 >> 8))
                        {
                                row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                                for (int k = 0; k < count; k++)
                                {
                                        b32 p0, p1;
                                        unsigned int c0, c1;

                                        if (!ui_span_ends(first, width, spans[k][0], spans[k][1], &p0, &p1, &c0, &c1))
                                                continue;
                                        for (b32 x = p0; x <= p1; x++)
                                                row[x] = ui_over(ui_premultiplied(argb, x == p0 ? c0 : x == p1 ? c1 : UI_FULL), row[x]);
                                }
                                continue;
                        }
                }
                if (!ui_box_row(box, y, ui_kit_line, first, width))
                        continue;
                ui_box_row(&inner, y, ui_kit_clip, first, width);
                row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                for (b32 x = 0; x < width; x++)
                {
                        int cover = (int)ui_kit_line[x] - (int)ui_kit_clip[x];

                        ui_kit_line[x] = ui_kit_clip[x] = 0;
                        if (cover > 0)
                                row[x] = ui_over(ui_premultiplied(argb, (unsigned int)cover > UI_FULL ? UI_FULL : (unsigned int)cover), row[x]);
                }
        }
}

// A Gaussian's share left of x sigmas, x from -4 to 4 in sixteenths, 0 to 65535: a blurred edge.
static const unsigned short ui_edge[129] = {
        2, 3, 3, 5, 6, 7, 9, 12, 15, 19, 24, 30, 38, 47, 58, 72, 88, 108, 132, 161, 195, 236, 284, 341, 407, 485, 575, 680,
        801, 941, 1101, 1283, 1491, 1726, 1992, 2291, 2625, 2998, 3413, 3872, 4378, 4934, 5542, 6205, 6924, 7701, 8539, 9437,
        10397, 11420, 12503, 13648, 14852, 16114, 17431, 18801, 20220, 21684, 23188, 24728, 26299, 27894, 29508, 31135,
        32768, 34400, 36027, 37641, 39236, 40807, 42347, 43851, 45315, 46734, 48104, 49421, 50683, 51887, 53032, 54115,
        55138, 56098, 56996, 57834, 58611, 59330, 59993, 60601, 61157, 61663, 62122, 62537, 62910, 63244, 63543, 63809,
        64044, 64252, 64434, 64594, 64734, 64855, 64960, 65050, 65128, 65194, 65251, 65299, 65340, 65374, 65403, 65427,
        65447, 65463, 65477, 65488, 65497, 65505, 65511, 65516, 65520, 65523, 65526, 65528, 65529, 65530, 65532, 65532,
        65533,
};

// The share of a blurred span from low to high (256ths) at x, for a blur of sigma (256ths), 0 to 65535.
static unsigned int ui_blurred(b32 x, b32 low, b32 high, b32 sigma)
{
        b64 a = ((b64)(x - low) * 16) / sigma, b = ((b64)(x - high) * 16) / sigma;
        unsigned int in = a <= -64 ? 0 : a >= 64 ? 65535 : ui_edge[a + 64];
        unsigned int out = b <= -64 ? 0 : b >= 64 ? 65535 : ui_edge[b + 64];

        return in > out ? in - out : 0;
}

/*
        A box-shadow: the box moved by dx, dy, grown by spread (all 256ths of a
        device pixel) and blurred by blur (CSS's blur radius, twice the
        Gaussian's sigma), in a colour at its own alpha. The blur is the box's
        two axes blurred apart and multiplied, from a ramp made once, so a shadow
        whose numbers move each frame (a field's, lifting on hover) is drawn
        afresh at no more than a multiply a pixel. Drawn outside the box only,
        as CSS draws an outer shadow: the box's own pixels are its face's.
*/
static fn ui_kit_shadow(struct ui address_to ui, const struct ui_box address_to box, b32 dx, b32 dy, b32 blur, b32 spread,
                        unsigned int argb)
{
        struct ui_box s = *box;
        b32 sigma = blur / 2, reach = blur + 256;
        b32 first, last, top, bottom;

        if (!(argb >> 24))
                return;
        s.x0 += dx - spread, s.x1 += dx + spread, s.y0 += dy - spread, s.y1 += dy + spread;
        s.rx = s.rx + spread > 0 ? s.rx + spread : 0;
        s.ry = s.ry + spread > 0 ? s.ry + spread : 0;
        first = (s.x0 - reach) >> 8, last = (s.x1 + reach + 255) >> 8, top = (s.y0 - reach) >> 8, bottom = (s.y1 + reach + 255) >> 8;
        first = first < 0 ? 0 : first;
        last = last > ui->width ? ui->width : last;
        if (last - first > UI_KIT_LINE)
                last = first + UI_KIT_LINE;
        // The horizontal blur is the same on every row: worked out once, into the clip line, and given back zeroed.
        for (b32 x = 0; x < last - first; x++)
        {
                b32 cx = (first + x) * 256 + 128;

                ui_kit_clip[x] = (unsigned short)(sigma > 0 ? ui_blurred(cx, s.x0, s.x1, sigma) : (cx >= s.x0 && cx < s.x1 ? 65535 : 0));
        }
        for (b32 y = top; y < bottom && last > first; y++)
        {
                unsigned int address_to row;
                b32 width = last - first, cy = y * 256 + 128;
                unsigned int vertical;

                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                vertical = sigma > 0 ? ui_blurred(cy, s.y0, s.y1, sigma) : (cy >= s.y0 && cy < s.y1 ? 65535 : 0);
                if (!vertical)
                        continue;
                // The box itself is not shadowed: its cover is taken out of the shadow's.
                ui_box_row(box, y, ui_kit_line, first, width);
                row = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)first;
                for (b32 x = 0; x < width; x++)
                {
                        unsigned int cover = (unsigned int)(((p64)ui_kit_clip[x] * vertical) >> 20); // to UI_FULL (4096)
                        unsigned int inside = ui_kit_line[x];

                        ui_kit_line[x] = 0;
                        cover = inside >= UI_FULL ? 0 : cover * (UI_FULL - inside) / UI_FULL;
                        if (cover)
                                row[x] = ui_over(ui_premultiplied(argb, cover), row[x]);
                }
        }
        memory_zero(ui_kit_clip, (positive)(last > first ? last - first : 0) * sizeof(ui_kit_clip[0]));
}

/* ---- end of the kit's shapes ---- */

/* ---- kit: controls ---- */

/*
        The kit's type scale, its role names kept: Geist in the kit, here the
        faces Canvas ships, a weight of 600 and up the bold, 500 and under the
        regular, and the mono IBM Plex Mono. Sizes and line heights are the
        kit's, in logical pixels.
*/
#define UI_TITLE_LARGE 0  // 600 22/28
#define UI_TITLE_MEDIUM 1 // 600 16/24
#define UI_TITLE_SMALL 2  // 600 14/20
#define UI_BODY_LARGE 3   // 400 16/24
#define UI_BODY_MEDIUM 4  // 400 14/20
#define UI_LABEL_LARGE 5  // 600 14/20
#define UI_LABEL_MEDIUM 6 // 500 12/16
#define UI_MONO 7         // label-medium in the mono: a tag, a key
#define UI_CODE 8         // the mono at 13/1.6: a code well
#define UI_FIELD 9        // a field's text, 15px

static const struct ui_type
{
        unsigned char size, line, bold, mono;
} ui_types[] = {{22, 28, 1, 0}, {16, 24, 1, 0}, {14, 20, 1, 0}, {16, 24, 0, 0}, {14, 20, 0, 0},
                {14, 20, 1, 0}, {12, 16, 0, 0}, {12, 16, 0, 1}, {13, 21, 0, 1}, {15, 20, 0, 0}};

// Text of a role at x on the baseline (device pixels, whole), in a colour at its own alpha, with a stroke; its width.
static int ui_kit_text(struct ui address_to ui, int x, int baseline, string_address text, int type, unsigned int argb,
                       unsigned int stroke)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to faces[2] = {t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular,
                                               ui->regular};

        return ui_run_in(ui, faces, x, baseline, text, t->size, argb & 0xffffffu, argb >> 24, 0, ui->width, stroke);
}

static int ui_kit_measure(struct ui address_to ui, string_address text, int type)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to faces[2] = {t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular,
                                               ui->regular};

        return ui_run_in(ui, faces, 0, 0, text, t->size, 0, 0, 0, ui->width, 0);
}

// The baseline of a role's line box whose top is top (device pixels): the line's half-leading above the face's ascent.
static int ui_kit_baseline(struct ui address_to ui, int top, int type)
{
        const struct ui_type address_to t = &ui_types[type];
        const font_face address_to face = t->mono && ui->mono ? ui->mono : t->bold && ui->bold ? ui->bold : ui->regular;
        font_metrics metrics = font_metrics_at(face, (p32)(t->size * ui->scale) << 6);

        return top + (t->line * ui->scale - metrics.cell_height) / 2 + metrics.baseline;
}

/*
        The press channel, --press-x/-y and --press-tx/-ty: a drag holds the
        pull (KB), a release springs it home (pressSpring), and a new press
        freezes a spring where it is (cancelSprings leaves the last values
        written) until the pull is moved or let go of.
*/
struct ui_squish
{
        struct ui_spring x, y;
        struct ui_squash shown, pull;
        b32 holding, pulled;
};

static fn ui_squish_press(struct ui_squish address_to q)
{
        ui_spring_stop(&q->x);
        ui_spring_stop(&q->y);
        q->holding = 1;
        q->pulled = 0;
        memory_zero(&q->pull, sizeof(q->pull));
}

static fn ui_squish_move(struct ui_squish address_to q, struct ui_squash pull)
{
        // Reduced motion: no squash, and so nothing to spring home.
        if (ui_kit.motion)
                memory_zero(&pull, sizeof(pull));
        q->pull = q->shown = pull;
        q->pulled = 1;
}

static fn ui_squish_release(struct ui_squish address_to q)
{
        struct ui_squash s = q->pull;

        q->holding = 0;
        if (q->pulled && (s.sx > UI_REST_SCALE || s.sx < -UI_REST_SCALE || s.sy > UI_REST_SCALE || s.sy < -UI_REST_SCALE))
        {
                ui_spring_go(&q->x, s.sx, s.tx);
                ui_spring_go(&q->y, s.sy, s.ty);
                q->shown = s;
        }
        else
                memory_zero(&q->shown, sizeof(q->shown));
}

static struct ui_squash ui_squish_at(struct ui_squish address_to q, b64 now)
{
        if (q->holding)
                return q->shown;
        if (q->x.live | q->y.live)
        {
                ui_kit.busy |= ui_spring_at(&q->x, now) | ui_spring_at(&q->y, now);
                q->shown.sx = q->x.value[0], q->shown.tx = q->x.value[1];
                q->shown.sy = q->y.value[0], q->shown.ty = q->y.value[1];
        }
        return q->shown;
}

/*
        A button's drawn transform: ui.css gives a button `transition: transform
        .1s var(--ease)`, so each frame the values ui.js writes start a 100 ms
        transition from where the drawn transform is, and what is drawn at a
        frame is that transition's value there. Four channels as one value, as
        CSS turns one transform property.
*/
struct ui_chase
{
        b64 from[4], to[4], start;
};

static fn ui_chase_at(struct ui_chase address_to c, const b64 address_to want, b64 now, b64 address_to out)
{
        b64 p = c->start ? ui_bezier_at(&ui_ease, ui_progress(now - c->start, 100 * UI_MS)) : UI_ONE;
        b32 changed = 0;

        for (int k = 0; k < 4; k++)
        {
                out[k] = c->from[k] + ui_qmul(c->to[k] - c->from[k], p);
                changed |= want[k] != c->to[k];
        }
        if (changed && !ui_kit.motion)
        {
                for (int k = 0; k < 4; k++)
                        c->from[k] = out[k], c->to[k] = want[k];
                c->start = now;
        }
        else if (changed)
                for (int k = 0; k < 4; k++)
                        c->from[k] = c->to[k] = out[k] = want[k];
        if (c->start && now - c->start >= 100 * UI_MS && !changed)
                c->start = 0;
        if (c->start)
                ui_kit.busy = 1;
}

/*
        One control: its kind, its box in logical pixels (a row for a switch, a
        checkbox and each radio), what it holds and the motion it is in. A group
        of radios and a strip of tabs are each one control with items. The
        drawing reads the motion at ui_kit.now and marks busy while any of it
        moves; the pointer and the keys come in through ui_kit_pointer and
        ui_kit_key, which answer whether the control changed.
*/
#define UI_BUTTON 1
#define UI_SWITCH 2
#define UI_CHECKBOX 3
#define UI_RADIOS 4
#define UI_SLIDER 5
#define UI_TABS 6
#define UI_INPUT 7
#define UI_PROGRESS 8
#define UI_BADGE 9
#define UI_TAG 10
#define UI_AVATAR 11
#define UI_CALLOUT 12
#define UI_CODE_WELL 13
#define UI_KBD 14
#define UI_SPINNER 15
#define UI_CARD 16
#define UI_WINDOW 17
#define UI_HEADING 18 // a line of text in a role (a heading, a status line)
#define UI_RULE 19   // the hairline <hr>
#define UI_LIST_ROW 20 // a row of a list with no control of the kit's: its hover layer and its press ripple, edge to edge
#define UI_SWATCHES 21 // a colour each (items, rrggbb), 20 round and 8 apart, its right edge the box's, the chosen (at) ringed

// A button's variant; a slider's; tabs'; a badge's and a callout's (as roles: UI_SUCCESS...).
#define UI_BUTTON_DEFAULT 0
#define UI_BUTTON_PRIMARY 1
#define UI_BUTTON_OUTLINED 2
#define UI_BUTTON_TEXT 3
#define UI_SLIDER_STEPS 1
#define UI_SLIDER_LINE 2
#define UI_TABS_UNDERLINE 0
#define UI_TABS_SEGMENTED 1
#define UI_TABS_VERTICAL 2 // the segmented strip stood on its end: a column of items h tall, the pip travelling along y
#define UI_ITEMS 16
#define UI_RADIO_PITCH 64 // a radio row and the gap under it (48 + 16, the card's gap)

struct ui_control
{
        unsigned char kind, variant;
        b32 on, disabled;                  // checked; a radio group's or tabs' chosen item is at
        int at;
        int x, y, w, h;                    // logical pixels
        const char address_to label;
        const char address_to items[UI_ITEMS];
        int count;
        b64 value, low, high, step;        // a slider's (Q30); progress 0 to UI_ONE, below 0 indeterminate
        char text[64];                     // a field's
        int role;                          // a badge's, a callout's, a text's colour role; a text's type in variant
        // The pointer.
        b32 hover, down, moved, focused;
        int item;                          // the item under the pointer or pressed
        int press_x, press_y, pointer_x, pointer_y; // device pixels in the window
        b64 pressed_at;
        // Motion.
        struct ui_squish squish;
        struct ui_chase drawn;
        struct ui_ripple ripple[3];
        struct ui_tween layer, fill, ink, label_ink, stroke, scale, extent, shadow, pip_x, pip_w, pip_y;
        struct ui_tween item_ink[UI_ITEMS], item_scale[UI_ITEMS];
        struct ui_stretch dom, item_dom[UI_ITEMS];
        int dom_origin;                    // -1 the far edge, 1 the near (left, top), 0 the middle
        struct ui_spring held_squish;      // --sw-squish or --squish let go
        b64 bend, bend_target;             // --sw-squish / --squish held, Q30 over one; the slider's jello target
        int field_state;                   // a field's: rest, hover or focus, as its shadows were last sent to
        b64 field_start;                   // and when they left for it
        int squish_origin;                 // 1 left, -1 right
        b64 pip_squash, pip_press;         // the radio pip's keyframes: their first frames (0 none, 1 pending)
        b64 shadow_from[4][5], shadow_to[4][5]; // a field's four box-shadows: x, y, blur, spread, colour
        // The slider.
        struct ui_slide slide;
        b32 sliding, bounce, dragged;
        b64 slide_from;
        int hover_x;                       // where the pointer hovers over it, device pixels, or -1
        struct ui_tween spot;              // the spot's opacity
        int spot_x;
        b64 spot_gone;
        b64 out_at;                        // the pointer left: hovering ends 200 ms after
        b64 last_frame;
        struct ui_text address_to edit;    // a field's text widget (the text agent's engine), or none: then text[] as it was
        b64 placed;                        // the kit's epoch it was last placed in; 0 before its first frame
        // The row the control is in, logical pixels: what its press ripples across and its hover lights, edge to edge.
        // None (row_w 0) is the kit's own: the box and the card's padding round it (24 across, 8 down, ui.css's ripple-clip).
        int row_x, row_y, row_w, row_h;
        // A field's text when it is longer than text holds (what is typed in a list's row): drawn in place of text; and
        // drawn as a dot for each character when masked (a passphrase), never as it is.
        const char address_to source;
        b32 masked;
};

// The row a control's ripple and hover cover, in logical pixels.
static fn ui_control_row(const struct ui_control address_to c, int address_to x, int address_to y, int address_to w, int address_to h)
{
        if (c->row_w > 0)
        {
                *x = c->row_x, *y = c->row_y, *w = c->row_w, *h = c->row_h;
                return;
        }
        *x = c->x - 24, *y = c->y - 8, *w = c->w + 48, *h = c->h + 16;
}

// A control to be placed at its next frame, as if it had just appeared: a page shown again, a list rebuilt, a row scrolled in.
static inline fn ui_control_forget(struct ui_control address_to c)
{
        c->placed = 0;
}

// The pieces a control needs in device pixels.
static inline b32 ui_dev(const struct ui address_to ui, int logical) { return logical * ui->scale; }
static inline b32 ui_q8(const struct ui address_to ui, int logical) { return logical * ui->scale * 256; }

// A control before its first frame: its transitions have no value yet, so the first frame puts each at its state's.
static fn ui_control_start(struct ui_control address_to c)
{
        ui_tween_put(&c->extent, c->on ? UI_ONE : 0, UI_TWEEN_NUMBER);
        ui_tween_put(&c->spot, 0, UI_TWEEN_NUMBER);
        for (int k = 0; k < UI_ITEMS; k++)
                ui_tween_put(&c->item_scale[k], k == c->at ? 0 : UI_ONE, UI_TWEEN_NUMBER);
        if (c->kind == UI_PROGRESS && c->value >= 0)
                ui_tween_put(&c->extent, c->value, UI_TWEEN_NUMBER);
        c->hover_x = -1;
        c->item = -1;
        c->pip_squash = c->pip_press = 0;
        for (int k = 0; k < 4; k++)
                c->drawn.from[k] = c->drawn.to[k] = 0;
}

// A tween's value now, marking the frame busy while it moves.
static inline b64 ui_kit_tween(struct ui_tween address_to t)
{
        b32 live = 0;
        b64 v = ui_tween_at(t, ui_kit.now, &live);

        ui_kit.busy |= live;
        return v;
}

static inline unsigned int ui_kit_colour(struct ui_tween address_to t)
{
        return ui_tween_argb(ui_kit_tween(t));
}

// A colour to transition to over the kit's .2s (or at once with reduced motion); its first is where it starts.
static inline fn ui_kit_colour_to(struct ui_tween address_to t, unsigned int argb, int ms, const struct ui_bezier address_to ease)
{
        if (!t->placed)
                ui_tween_put(t, ui_tween_colour(argb), UI_TWEEN_COLOUR);
        else
                ui_tween_set(t, ui_tween_colour(argb), ui_kit.motion ? 0 : ms * UI_MS, ease);
}

static inline fn ui_kit_number_to(struct ui_tween address_to t, b64 value, int ms, const struct ui_bezier address_to ease)
{
        if (!t->placed)
                ui_tween_put(t, value, UI_TWEEN_NUMBER);
        else
                ui_tween_set(t, value, ui_kit.motion ? 0 : ms * UI_MS, ease);
}

/*
        The ripples of a control in a colour: measured from the top left of
        their clip box (the control's box and the margin round it, x0, y0 in
        device 256ths, as laid out), drawn through the control's transform
        (sx, sy about ox, oy, then tx, ty) and cut to clip.
*/
struct ui_place
{
        b64 sx, sy;
        b32 ox, oy, tx, ty;
};

static fn ui_kit_ripples(struct ui address_to ui, struct ui_control address_to c, b32 x0, b32 y0, const struct ui_place address_to place,
                         const struct ui_box address_to clip, unsigned int argb)
{
        for (int k = 0; k < 3; k++)
        {
                b64 cx, cy, radius, opacity;
                struct ui_box dot;

                if (!ui_ripple_at(&c->ripple[k], ui_kit.now, &cx, &cy, &radius, &opacity))
                        continue;
                ui_kit.busy = 1;
                dot.x0 = x0 + (b32)(((cx - radius) * ui->scale) >> 22);
                dot.x1 = x0 + (b32)(((cx + radius) * ui->scale) >> 22);
                dot.y0 = y0 + (b32)(((cy - radius) * ui->scale) >> 22);
                dot.y1 = y0 + (b32)(((cy + radius) * ui->scale) >> 22);
                dot.rx = dot.ry = (b32)((radius * ui->scale) >> 22);
                dot = ui_box_transform(dot, place->sx, place->sy, place->ox, place->oy, place->tx, place->ty);
                ui_kit_fill(ui, &dot, ui_mix(argb, (unsigned int)((opacity * 100 + UI_HALF) >> UI_Q), 0), clip);
        }
}

// A ripple from a press at device x, y in a box (device px) whose clip reaches margin (logical) past it.
static fn ui_kit_ripple_start(const struct ui address_to ui, struct ui_control address_to c, int x, int y, int bx, int by, int bw, int bh, int margin)
{
        struct ui_ripple address_to r = &c->ripple[0];

        if (ui_kit.motion)
                return;
        for (int k = 0; k < 3; k++)
                if (!c->ripple[k].live)
                {
                        r = &c->ripple[k];
                        break;
                }
        ui_ripple_go(r, ui_qdiv(ui_q(x - bx), ui_q(ui->scale)), ui_qdiv(ui_q(y - by), ui_q(ui->scale)), ui_qdiv(ui_q(bw), ui_q(ui->scale)),
                     ui_qdiv(ui_q(bh), ui_q(ui->scale)), ui_q(margin));
}

static fn ui_kit_ripples_exit(struct ui_control address_to c)
{
        for (int k = 0; k < 3; k++)
                ui_ripple_exit(&c->ripple[k], ui_kit.now);
}

/*
        A button: a pill 36 tall, 14 in at each side (10 for a text button),
        the label-large text in its middle. Its fill and ink go over .2s
        --ease; its state layer (the ink at .06 hovered, .10 pressed, .2s) is
        over the fill; a press ripples in the ink at .2, cut to the pill; a drag
        squashes it (BB) and the release springs it home, drawn through the
        .1s transform transition. Disabled: the ink at 38%, the fill at 8%.
*/
static int ui_button_width(struct ui address_to ui, const char address_to label, int variant)
{
        return ui_kit_measure(ui, (string_address)label, UI_LABEL_LARGE) / ui->scale + 2 * (variant == UI_BUTTON_TEXT ? 10 : 14);
}

static fn ui_button_targets(struct ui_control address_to c)
{
        unsigned int fill = 0, ink;

        switch (c->variant)
        {
        case UI_BUTTON_PRIMARY:
                fill = c->down ? ui_mix(ui_role(UI_ON_SURFACE), 16, ui_role(UI_PRIMARY)) : ui_role(UI_PRIMARY);
                ink = ui_role(UI_ON_PRIMARY);
                break;
        case UI_BUTTON_OUTLINED: ink = ui_role(UI_ON_SURFACE); break;
        case UI_BUTTON_TEXT: ink = ui_role(UI_PRIMARY); break;
        default:
                fill = ui_role(UI_SECONDARY_CONTAINER);
                ink = ui_role(UI_ON_SECONDARY_CONTAINER);
                break;
        }
        if (c->disabled)
        {
                fill = ui_mix(ui_role(UI_ON_SURFACE), 8, 0);
                ink = ui_mix(ui_role(UI_ON_SURFACE), 38, 0);
        }
        ui_kit_colour_to(&c->fill, fill, 200, &ui_ease);
        ui_kit_colour_to(&c->ink, ink, 200, &ui_ease);
        ui_kit_number_to(&c->layer, c->disabled ? 0 : c->down ? UI_Q2(10) : c->hover ? UI_Q2(6) : 0, 200, &ui_ease);
}

// The transform a control is drawn with this frame: scale less one and nudge (device 256ths), about its middle.
static fn ui_kit_press_transform(const struct ui address_to ui, struct ui_control address_to c, b32 chased, b64 address_to sx,
                                 b64 address_to sy, b32 address_to tx, b32 address_to ty)
{
        struct ui_squash s = ui_squish_at(&c->squish, ui_kit.now);
        b64 want[4] = {s.sx, s.sy, s.tx, s.ty}, drawn[4];

        if (chased)
                ui_chase_at(&c->drawn, want, ui_kit.now, drawn);
        else
                for (int k = 0; k < 4; k++)
                        drawn[k] = want[k];
        *sx = UI_ONE + drawn[0];
        *sy = UI_ONE + drawn[1];
        *tx = (b32)((drawn[2] * ui->scale) >> 22);
        *ty = (b32)((drawn[3] * ui->scale) >> 22);
}

static fn ui_button_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b64 sx, sy;
        b32 tx, ty, ox = ui_q8(ui, c->x) + ui_q8(ui, c->w) / 2, oy = ui_q8(ui, c->y) + ui_q8(ui, c->h) / 2;
        struct ui_box face = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(c->h) / 2);
        unsigned int fill, ink, layer;
        int width, baseline, x;

        ui_button_targets(c);
        fill = ui_kit_colour(&c->fill);
        ink = ui_kit_colour(&c->ink);
        layer = (unsigned int)((ui_kit_tween(&c->layer) * 100 + UI_HALF) >> UI_Q);
        ui_kit_press_transform(ui, c, 1, &sx, &sy, &tx, &ty);
        face = ui_box_transform(face, sx, sy, ox, oy, tx, ty);
        ui_kit_fill(ui, &face, fill, null);
        if (c->variant == UI_BUTTON_OUTLINED && !c->disabled)
                ui_kit_ring(ui, &face, ui_q8(ui, 1), ui_role(c->focused ? UI_PRIMARY : c->hover || c->down ? UI_OUTLINE : UI_OUTLINE_VARIANT));
        if (layer)
                ui_kit_fill(ui, &face, ui_mix(ink, layer, 0), null);
        {
                struct ui_place place = {sx, sy, ox, oy, tx, ty};

                ui_kit_ripples(ui, c, ui_q8(ui, c->x - 8), ui_q8(ui, c->y - 8), &place, &face, ink);
        }
        width = ui_kit_measure(ui, (string_address)c->label, UI_LABEL_LARGE);
        // The label at whole pixels where the transform takes its middle: never scaled, never resampled.
        x = (ox + tx) / 256 - width / 2;
        baseline = ui_kit_baseline(ui, (oy + ty) / 256 - ui_dev(ui, ui_types[UI_LABEL_LARGE].line) / 2, UI_LABEL_LARGE);
        ui_kit_text(ui, x, baseline, (string_address)c->label, UI_LABEL_LARGE, ink, 0);
}

/*
        A switch or a checkbox (the kit draws them alike): the row's label at the
        left in body-large, half the ink, the accent hovered and full ink with a
        stroke when on (.2s); at the right a 52 by 28 pill, the rest in the
        control-off colour and the fill in the accent, which grow and shrink
        across it over .2s (dragged, they part by 8 and follow the hand), hovered
        1.05 and pressed 1.1 (.15s), squashed toward a drag (--sw-squish) and
        sprung home. A toggle stretches the label 1.11 toward where the knob went
        (the domino, at 0.6 of it on the text).
*/
static fn ui_toggle_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b64 sx, sy, extent, scale, squish, dom;
        b32 tx, ty, live = 0;
        b32 rx = ui_q8(ui, c->x), ry = ui_q8(ui, c->y), rw = ui_q8(ui, c->w), rh = ui_q8(ui, c->h);
        b32 ox = rx + rw / 2, oy = ry + rh / 2;
        b32 bx = rx + rw - ui_q8(ui, 52), by = oy - ui_q8(ui, 14), bw = ui_q8(ui, 52), bh = ui_q8(ui, 28);
        b32 gap = 0, fill_w, rest_w, origin_x;
        struct ui_box fill, rest, clip;
        unsigned int label;
        int width, baseline, x;

        ui_kit_colour_to(&c->label_ink, c->on ? ui_role(UI_ON_SURFACE) : c->hover && !c->disabled ? ui_role(UI_PRIMARY)
                                                                                      : ui_mix(ui_role(UI_ON_SURFACE), 50, 0), 200, &ui_ease);
        ui_kit_number_to(&c->stroke, c->on || (c->hover && !c->disabled) ? 64 * UI_ONE : 0, 200, &ui_ease);
        ui_kit_number_to(&c->scale, c->down ? UI_Q2(110) : c->hover ? UI_Q2(105) : UI_ONE, 150, &ui_ease);
        if (!c->moved)
                ui_kit_number_to(&c->extent, c->on ? UI_ONE : 0, 200, &ui_ease);
        label = ui_kit_colour(&c->label_ink);
        extent = ui_kit_tween(&c->extent);
        scale = ui_kit_tween(&c->scale);
        // --sw-squish: held while dragged, else the spring that let it go.
        squish = c->moved ? (ui_kit.motion ? 0 : c->bend) : (ui_kit.busy |= ui_spring_at(&c->held_squish, ui_kit.now), c->held_squish.value[0]);
        dom = ui_stretch_at(&c->dom, ui_kit.now, &live);
        ui_kit.busy |= live;
        ui_kit_press_transform(ui, c, 0, &sx, &sy, &tx, &ty);

        // The two bars across the pill: dragged, each 4 short of the hand.
        if (c->moved)
                gap = ui_q8(ui, 4);
        fill_w = (b32)ui_qmul(extent, bw) - gap;
        rest_w = bw - (b32)ui_qmul(extent, bw) - gap;
        fill = (struct ui_box){bx, by, bx + (fill_w > 0 ? fill_w : 0), by + bh, ui_q8(ui, 14), ui_q8(ui, 14)};
        rest = (struct ui_box){bx + bw - (rest_w > 0 ? rest_w : 0), by, bx + bw, by + bh, ui_q8(ui, 14), ui_q8(ui, 14)};
        // The bars' own transform: scaleX(--sw-squish) then the hover scale, about the squish's origin (the middle at rest).
        origin_x = c->squish_origin > 0 ? bx : c->squish_origin < 0 ? bx + bw : bx + bw / 2;
        fill = ui_box_transform(fill, ui_qmul(UI_ONE + squish, scale), scale, origin_x, oy, 0, 0);
        rest = ui_box_transform(rest, ui_qmul(UI_ONE + squish, scale), scale, origin_x, oy, 0, 0);
        fill = ui_box_transform(fill, sx, sy, ox, oy, tx, ty);
        rest = ui_box_transform(rest, sx, sy, ox, oy, tx, ty);
        // The row's ripple, in the accent: its clip is the whole row, edge to edge (by default to the card's edges).
        {
                int cx, cy, cw, ch;

                ui_control_row(c, &cx, &cy, &cw, &ch);
                clip = ui_box_transform((struct ui_box){ui_q8(ui, cx), ui_q8(ui, cy), ui_q8(ui, cx + cw), ui_q8(ui, cy + ch), 0, 0},
                                        sx, sy, ox, oy, tx, ty);
        }
        if (rest_w > 0)
                ui_kit_fill(ui, &rest, ui_role(UI_CONTROL_OFF), null);
        if (fill_w > 0)
                ui_kit_fill(ui, &fill, ui_role(UI_PRIMARY), null);

        // The label: its box shrink-wraps the text, stretched 0.6 of the domino about its trailing edge, and the text
        // laid at whole pixels where the stretch takes the box's middle.
        width = ui_kit_measure(ui, (string_address)c->label, UI_BODY_LARGE);
        {
                b32 left = rx, middle = left + width * 128, anchor = c->dom_origin > 0 ? left : left + width * 256;
                b64 stretch = UI_ONE + ui_qmul(dom - UI_ONE, UI_Q2(60));

                middle = anchor + (b32)ui_qmul(middle - anchor, stretch);
                middle = ox + tx + (b32)ui_qmul(middle - ox, sx);
                x = middle / 256 - width / 2;
        }
        baseline = ui_kit_baseline(ui, (oy + ty) / 256 - ui_dev(ui, 12), UI_BODY_LARGE);
        ui_kit_text(ui, x, baseline, (string_address)c->label, UI_BODY_LARGE, c->disabled ? ui_mix(label, 38, 0) : label,
                    (unsigned int)(ui_kit_tween(&c->stroke) >> UI_Q));
        // The ripple, in the accent, over the row: its clip is the label's last child, so it lies over the bars and the text.
        {
                struct ui_place place = {sx, sy, ox, oy, tx, ty};
                int cx, cy, cw, ch;

                ui_control_row(c, &cx, &cy, &cw, &ch);
                ui_kit_ripples(ui, c, ui_q8(ui, cx), ui_q8(ui, cy), &place, &clip, ui_role(UI_PRIMARY));
        }
}

/*
        A group of radios, one row each UI_RADIO_PITCH apart: each a 28 circle in
        the middle of the switch's column, the rest a control-off circle that
        shrinks away (.2s linear) when it is the one chosen, and over them the
        one accent pip that flies to the chosen (.4s --ease-emphasized) with
        the pip-squash keyframes (.45s), the rows it rolls over stretched along
        the travel (1.34, as the pip reaches each over 240 ms) and their labels
        at 0.55 of it. Pressing the chosen one again squishes it (0.84) and the
        pip (pip-press, .3s).
*/
static b32 ui_radio_row_y(const struct ui_control address_to c, int item)
{
        return c->y + item * UI_RADIO_PITCH;
}

static fn ui_radios_draw(struct ui address_to ui, struct ui_control address_to c)
{
        static const struct ui_stop squash_x[] = {{0, UI_ONE}, {UI_Q2(20), UI_Q2(80)}, {UI_Q2(60), UI_Q2(110)}, {UI_ONE, UI_ONE}};
        static const struct ui_stop squash_y[] = {{0, UI_ONE}, {UI_Q2(20), UI_Q2(127)}, {UI_Q2(60), UI_Q2(91)}, {UI_ONE, UI_ONE}};
        static const struct ui_stop press[] = {{0, UI_ONE}, {UI_Q2(42), UI_Q2(82)}, {UI_ONE, UI_ONE}};
        b64 sx, sy, pip_y, psx = UI_ONE, psy = UI_ONE;
        b32 tx, ty, live = 0;
        b32 cx = ui_q8(ui, c->x + c->w - 12 - 14); // the circles' middle: 12 in from the row's right, 28 wide

        ui_kit_press_transform(ui, c, 0, &sx, &sy, &tx, &ty);
        for (int item = 0; item < c->count; item++)
        {
                b32 ry = ui_q8(ui, ui_radio_row_y(c, item)), rh = ui_q8(ui, c->h), oy = ry + rh / 2, rx = ui_q8(ui, c->x), rw = ui_q8(ui, c->w);
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live), hover, rest;
                b32 origin = c->dom_origin > 0 ? oy - ui_q8(ui, 14) : c->dom_origin < 0 ? oy + ui_q8(ui, 14) : oy;
                b32 lrow = c->item == item;
                struct ui_box dot = {cx - ui_q8(ui, 14), oy - ui_q8(ui, 14), cx + ui_q8(ui, 14), oy + ui_q8(ui, 14), ui_q8(ui, 14), ui_q8(ui, 14)};
                unsigned int label;
                int baseline, x;
                b64 row_sx = lrow ? sx : UI_ONE, row_sy = lrow ? sy : UI_ONE;
                b32 row_tx = lrow ? tx : 0, row_ty = lrow ? ty : 0;
                struct ui_box clip;

                ui_kit_number_to(&c->item_scale[item], item == c->at ? 0 : UI_ONE, 200, null);
                ui_kit_colour_to(&c->item_ink[item], item == c->at ? ui_role(UI_ON_SURFACE) : lrow && c->hover ? ui_role(UI_PRIMARY)
                                                                                                        : ui_mix(ui_role(UI_ON_SURFACE), 50, 0), 200, &ui_ease);
                hover = lrow ? (c->down ? UI_Q2(110) : c->hover ? UI_Q2(105) : UI_ONE) : UI_ONE;
                {
                        // The rest's scale goes linearly (.2s linear): a straight bezier.
                        static const struct ui_bezier linear = UI_BEZIER(0, 0, UI_ONE, UI_ONE);

                        if (c->item_scale[item].pending)
                                c->item_scale[item].ease = &linear;
                }
                rest = ui_kit_tween(&c->item_scale[item]);
                clip = ui_box_transform((struct ui_box){rx - ui_q8(ui, 24), ry - ui_q8(ui, 8), rx + rw + ui_q8(ui, 24), ry + rh + ui_q8(ui, 8), 0, 0},
                                        row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty);
                // The rest circle: scale(rest) about its middle, inside the bars' scaleY(dom) scale(hover) about the origin.
                if (rest > 0)
                {
                        struct ui_box r = ui_box_transform(dot, rest, rest, cx, oy, 0, 0);

                        r = ui_box_transform(r, hover, ui_qmul(dom, hover), cx, origin, 0, 0);
                        r = ui_box_transform(r, row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty);
                        ui_kit_fill(ui, &r, ui_role(UI_CONTROL_OFF), null);
                }
                label = ui_kit_colour(&c->item_ink[item]);
                x = (rx + row_tx) / 256;
                {
                        // The label stretches 0.55 of the row's along y about the same origin: its line moves with it.
                        b64 stretch = UI_ONE + ui_qmul(dom - UI_ONE, UI_Q2(55));
                        b32 middle = origin + (b32)ui_qmul(oy - origin, stretch);

                        baseline = ui_kit_baseline(ui, (middle + row_ty) / 256 - ui_dev(ui, 12), UI_BODY_LARGE);
                }
                ui_kit_text(ui, x, baseline, (string_address)c->items[item], UI_BODY_LARGE, label,
                            item == c->at || (lrow && c->hover) ? 64 : 0);
                if (lrow)
                {
                        struct ui_place place = {row_sx, row_sy, rx + rw / 2, oy, row_tx, row_ty};

                        ui_kit_ripples(ui, c, rx - ui_q8(ui, 24), ry - ui_q8(ui, 8), &place, &clip, ui_role(UI_PRIMARY));
                }
        }
        ui_kit.busy |= live;
        // The pip.
        // The pip's place is the group's own (from its top), so a group the layout moves takes its pip with it at once.
        ui_kit_number_to(&c->pip_y, ui_q(ui_radio_row_y(c, c->at) - c->y + c->h / 2), 400, &ui_ease);
        pip_y = ui_q(c->y) + ui_kit_tween(&c->pip_y);
        if (c->pip_squash == 1)
                c->pip_squash = ui_kit.now;
        if (c->pip_press == 1)
                c->pip_press = ui_kit.now;
        if (c->pip_squash)
        {
                b32 l = 0;

                psx = ui_keyframes_at(squash_x, 4, &ui_ease, c->pip_squash, 450 * UI_MS, ui_kit.now, &l);
                psy = ui_keyframes_at(squash_y, 4, &ui_ease, c->pip_squash, 450 * UI_MS, ui_kit.now, &l);
                if (!l)
                        c->pip_squash = 0;
                ui_kit.busy |= l;
        }
        else if (c->pip_press)
        {
                b32 l = 0;

                psx = psy = ui_keyframes_at(press, 3, &ui_ease, c->pip_press, 300 * UI_MS, ui_kit.now, &l);
                if (!l)
                        c->pip_press = 0;
                ui_kit.busy |= l;
        }
        {
                b32 py = (b32)((pip_y * ui->scale) >> 22);
                struct ui_box pip = {cx - ui_q8(ui, 14), py - ui_q8(ui, 14), cx + ui_q8(ui, 14), py + ui_q8(ui, 14), ui_q8(ui, 14), ui_q8(ui, 14)};

                pip = ui_box_transform(pip, psx, psy, cx, py, 0, 0);
                ui_kit_fill(ui, &pip, ui_role(UI_PRIMARY), null);
        }
}

// The radio domino: the rows between the old and the new, stretched as the pip passes each (240 ms of flight).
static fn ui_radios_choose(struct ui_control address_to c, int item)
{
        int from = c->at, distance = item > from ? item - from : from - item;

        if (item == from)
                return;
        c->dom_origin = item > from ? 1 : -1;
        if (!ui_kit.motion)
                for (int k = 0; k < c->count; k++)
                {
                        int along = (item > from ? k - from : from - k);

                        if (along < 0 || along > distance)
                                continue;
                        ui_stretch_go(&c->item_dom[k], ui_kit.now + (b64)along * 240 * UI_MS / distance, UI_Q2(134));
                }
        c->at = item;
        c->pip_squash = ui_kit.motion ? 0 : 1;
}

/*
        A slider: its label and value on one line (body-large; both the accent
        and stroked while hovered), under them a 48 tall track with the fill to
        the value (less 2) and the rest after it (plus 2): pills --bar-h tall
        (16, hovered 20, pressed 12; a line slider 6, 8, 4), which snap, as the
        kit's unregistered properties do. Hovered, a ghost previews where a
        press would land (the accent at 40% on the control-off); a press slides
        there and lands with a squish; a drag follows the hand and stretches
        past an end like a band; the spot under the hand fades in .3s.
*/
static b64 ui_slider_ratio(const struct ui_control address_to c, b64 value)
{
        return c->high > c->low ? ui_qdiv(value - c->low, c->high - c->low) : 0;
}

// A value as the range input keeps it: snapped to its step from its low end, inside its ends.
static b64 ui_slider_snap(const struct ui_control address_to c, b64 value)
{
        b64 step = c->step > 0 ? c->step : UI_ONE, n = ui_qdiv(value - c->low, step);

        n = (n + UI_HALF) >> UI_Q << UI_Q;
        value = c->low + ui_qmul(n, step);
        return value < c->low ? c->low : value > c->high ? c->high : value;
}

static fn ui_slider_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b64 sx, sy, value, ratio, squish = 0;
        b32 tx, ty, live = 0;
        b32 rx = ui_q8(ui, c->x), ry = ui_q8(ui, c->y), rw = ui_q8(ui, c->w), rh = ui_q8(ui, c->h);
        b32 ox = rx + rw / 2, oy = ry + rh / 2;
        b32 line = c->variant & UI_SLIDER_LINE;
        int thumb = line ? 20 : c->down ? 12 : c->hover ? 20 : 16;
        int bar = line ? (c->down ? 4 : c->hover ? 8 : 6) : thumb;
        b32 track_y = ry + rh - ui_q8(ui, 8) - ui_q8(ui, 24); // the bars' middle: 48 tall, 8 above the row's bottom
        b32 stop, gap = ui_q8(ui, 2), origin;
        struct ui_box fill, rest;
        unsigned int accent = ui_role(UI_PRIMARY), off = ui_role(UI_CONTROL_OFF), ink;
        char shown[24];
        int baseline;

        // The value: sliding after a click, else where it is; snapped to the step as the input keeps it.
        if (c->sliding)
        {
                value = ui_slider_snap(c, ui_slide_value(&c->slide, ui_kit.now, &live));
                c->value = value;
                if (!live)
                        c->sliding = 0;
        }
        value = c->value;
        ratio = ui_slider_ratio(c, value);
        stop = (b32)ui_qmul(ratio, rw - ui_q8(ui, thumb)) + ui_q8(ui, thumb) / 2;
        // The bars' scaleX: the jello while dragged past an end, the spring that lets it go, or the landing.
        if (c->moved && c->down)
        {
                // ui.js eases the stretch toward its target by 0.3 a frame at 60 Hz: the same per 1/60 s at any rate.
                b64 dt = c->last_frame ? ui_kit.now - c->last_frame : 16666667, keep = UI_Q2(70), k = UI_ONE;

                for (b64 left = dt; left >= 16666667; left -= 16666667)
                        k = ui_qmul(k, keep);
                if (dt % 16666667)
                        k = ui_qmul(k, UI_ONE - ui_qmul(UI_Q2(30), ui_progress(dt % 16666667, 16666667)));
                c->bend += ui_qmul(c->bend_target - c->bend, UI_ONE - k);
                squish = c->bend > 3221225 && !ui_kit.motion ? c->bend : 0; // > 0.003
                ui_kit.busy = 1;
        }
        else if (c->held_squish.live)
        {
                ui_kit.busy |= ui_spring_at(&c->held_squish, ui_kit.now);
                squish = c->held_squish.value[0];
        }
        else if (c->slide.landed)
                squish = ui_slide_land(&c->slide, ui_kit.now, &live) - UI_ONE;
        c->last_frame = ui_kit.now;
        ui_kit.busy |= live;
        ui_kit_press_transform(ui, c, 0, &sx, &sy, &tx, &ty);
        origin = c->squish_origin > 0 ? rx : c->squish_origin < 0 ? rx + rw : rx + rw / 2;

        // The spot under the hand.
        {
                b64 spot = ui_kit_tween(&c->spot);

                if (spot > 0)
                {
                        b32 spx = ui_q8(ui, c->x) + c->spot_x * 256, spy = ry + rh - ui_q8(ui, 24);

                        // A radial ellipse 180 by 80, the accent at its middle to nothing at 70%: rings of it, outermost first.
                        for (int ring = 7; ring >= 0; ring--)
                        {
                                b32 hw = ui_q8(ui, 90) * (ring + 1) / 8 * 7 / 10, hh = ui_q8(ui, 40) * (ring + 1) / 8 * 7 / 10;
                                struct ui_box e = {spx - hw, spy - hh, spx + hw, spy + hh, hw, hh};
                                unsigned int share = (unsigned int)((spot * 100 + UI_HALF) >> UI_Q) * 100 / 8 / 100;

                                e = ui_box_transform(e, sx, sy, ox, oy, tx, ty);
                                ui_kit_fill(ui, &e, ui_mix(accent, share ? share : 1, 0), null);
                        }
                }
        }

        fill = (struct ui_box){rx, track_y - ui_q8(ui, bar) / 2, rx + stop - gap, track_y + ui_q8(ui, bar) / 2, ui_q8(ui, bar) / 2, ui_q8(ui, bar) / 2};
        rest = (struct ui_box){rx + stop + gap, fill.y0, rx + rw, fill.y1, fill.rx, fill.ry};
        if (ratio <= 0)
                rest.x0 = rx;
        if (ratio >= UI_ONE)
                fill.x1 = rx + rw;
        fill = ui_box_transform(ui_box_transform(fill, UI_ONE + squish, UI_ONE, origin, track_y, 0, 0), sx, sy, ox, oy, tx, ty);
        rest = ui_box_transform(ui_box_transform(rest, UI_ONE + squish, UI_ONE, origin, track_y, 0, 0), sx, sy, ox, oy, tx, ty);
        if (ratio < UI_ONE)
                ui_kit_fill(ui, &rest, off, null);
        if (ratio > 0)
                ui_kit_fill(ui, &fill, accent, null);

        // The thumb's halo: the pointer on the thumb (its box, 20 hovered), a ring of 10 at 8% round it; pressed, 14 at 12%.
        {
                b32 thumb_x = rx + stop, hx = c->pointer_x * 256;
                b32 on_thumb = (c->hover || c->down) && hx >= thumb_x - ui_q8(ui, thumb) / 2 && hx < thumb_x + ui_q8(ui, thumb) / 2;
                b64 halo;

                ui_kit_number_to(&c->shadow, on_thumb ? (c->down ? UI_Q2(12) : UI_Q2(8)) : 0, 200, &ui_ease);
                ui_kit_number_to(&c->extent, on_thumb ? ui_q(c->down ? 14 : 10) : 0, 200, &ui_ease);
                halo = ui_kit_tween(&c->shadow);
                if (halo > 0)
                {
                        b32 r = ui_q8(ui, thumb) / 2 + (b32)((ui_kit_tween(&c->extent) * ui->scale) >> 22);
                        struct ui_box ring = {thumb_x - r, track_y - r, thumb_x + r, track_y + r, r, r};

                        ring = ui_box_transform(ring, sx, sy, ox, oy, tx, ty);
                        // The ring and the thumb's own background, both the accent at the halo's share.
                        ui_kit_fill(ui, &ring, ui_mix(accent, (unsigned int)((halo * 100 + UI_HALF) >> UI_Q), 0), null);
                }
        }

        // The ghost: hovered and not dragged, from the value to where a press would land, in the accent at 40%.
        if (c->hover && !c->down && c->hover_x >= 0)
        {
                b32 hx = c->hover_x * 256, left = stop < hx - rx ? rx + stop : hx, right = stop < hx - rx ? hx : rx + stop;
                b32 under = hx - rx < stop;
                unsigned int ghost = ui_mix(accent, 40, off);
                b32 thumb_x = rx + stop;

                if (c->variant & UI_SLIDER_STEPS)
                {
                        // Snapped to the steps, as the hover ratio is.
                        b64 steps = c->step > 0 ? ui_qdiv(c->high - c->low, c->step) : UI_ONE;
                        b64 z = ui_qdiv(ui_q(hx - rx - ui_q8(ui, thumb) / 2), ui_q(rw - ui_q8(ui, thumb)));

                        z = z < 0 ? 0 : z > UI_ONE ? UI_ONE : z;
                        z = ui_qdiv(((ui_qmul(z, steps) + UI_HALF) >> UI_Q) << UI_Q, steps);
                        hx = rx + (b32)ui_qmul(z, rw - ui_q8(ui, thumb)) + ui_q8(ui, thumb) / 2;
                        left = stop < hx - rx ? rx + stop : hx;
                        right = stop < hx - rx ? hx : rx + stop;
                }
                // Over the thumb itself the ghost is the fill's colour (under) or none (past).
                if (hx > thumb_x - ui_q8(ui, 11) && hx < thumb_x + ui_q8(ui, 11))
                        ghost = under ? accent : 0;
                if (ghost && right > left)
                {
                        struct ui_box g = {left, fill.y0, right, fill.y1, 0, 0};
                        struct ui_box bar_box = under ? fill : rest;

                        g = ui_box_transform(g, sx, sy, ox, oy, tx, ty);
                        g.y0 = bar_box.y0, g.y1 = bar_box.y1;
                        ui_kit_fill(ui, &g, ghost, &bar_box);
                }
        }

        // A stepped slider's dots: a faint ink one on the rest, a paper knockout on the fill, none where the value is.
        if (c->variant & UI_SLIDER_STEPS && c->step > 0)
        {
                int steps = (int)((ui_qdiv(c->high - c->low, c->step) + UI_HALF) >> UI_Q), here = (int)((ui_qmul(ratio, ui_q(steps)) + UI_HALF) >> UI_Q);

                for (int k = 0; k <= steps && steps > 0; k++)
                {
                        b32 dx = rx + (b32)((b64)(rw - ui_q8(ui, thumb)) * k / steps) + ui_q8(ui, thumb) / 2;
                        b32 filled = (b64)k * UI_ONE / steps <= ratio + 107374;
                        struct ui_box dot;

                        if (line)
                                dot = (struct ui_box){dx - ui_q8(ui, 1), track_y + ui_q8(ui, 6) - ui_q8(ui, k == 0 || k == steps || 2 * k == steps ? 6 : 4),
                                                      dx + ui_q8(ui, 1), track_y + ui_q8(ui, 6) + ui_q8(ui, k == 0 || k == steps || 2 * k == steps ? 6 : 4),
                                                      ui_q8(ui, 1), ui_q8(ui, 1)};
                        else
                                dot = (struct ui_box){dx - ui_q8(ui, 3), track_y - ui_q8(ui, 3), dx + ui_q8(ui, 3), track_y + ui_q8(ui, 3), ui_q8(ui, 3), ui_q8(ui, 3)};
                        if (!line && k == here && k != 0 && k != steps)
                                continue;
                        dot = ui_box_transform(dot, sx, sy, ox, oy, tx, ty);
                        ui_kit_fill(ui, &dot, filled && !line ? ui_mix(0xffffffffu, 90, 0) : ui_mix(ui_role(UI_ON_SURFACE), 22, 0), null);
                }
        }

        // The label and the value, which ride the squish too.
        ink = c->hover || c->down ? accent : ui_role(UI_ON_SURFACE);
        baseline = ui_kit_baseline(ui, (ry + ui_q8(ui, 8) + ty) / 256, UI_BODY_LARGE);
        {
                b32 lx = origin + (b32)ui_qmul(rx - origin, UI_ONE + squish);

                ui_kit_text(ui, (ox + tx + (b32)ui_qmul(lx - ox, sx)) / 256, baseline, (string_address)c->label, UI_BODY_LARGE, ink,
                            c->hover || c->down ? 64 : 0);
        }
        {
                positive length = positive_into_string((p8 address_to)shown, (positive)((value + UI_HALF) >> UI_Q));
                int width;
                b32 vx;

                shown[length] = 0;
                width = ui_kit_measure(ui, (string_address)shown, UI_BODY_LARGE);
                vx = origin + (b32)ui_qmul(rx + rw - width * 256 - origin, UI_ONE + squish);
                ui_kit_text(ui, (ox + tx + (b32)ui_qmul(vx - ox, sx)) / 256, baseline, (string_address)shown, UI_BODY_LARGE,
                            c->hover || c->down ? accent : ui_role(UI_ON_SURFACE_VARIANT), c->hover || c->down ? 64 : 0);
        }
}

/*
        Tabs, a strip of buttons: segmented, pills 32 tall 4 apart with the
        accent pip behind the chosen (on-primary on it); underlined, 40 tall 2
        apart over a hairline with a 2 tall pip under the chosen, 8 in from its
        sides. The pip slides over .35s --ease-emphasized, and the tabs it rolls
        over stretch 1.13 along the travel (130 ms of flight); colours go in .2s.
*/
static int ui_tab_width(struct ui address_to ui, const struct ui_control address_to c, int item)
{
        return ui_kit_measure(ui, (string_address)c->items[item], UI_LABEL_LARGE) / ui->scale + 28;
}

static int ui_tab_left(struct ui address_to ui, const struct ui_control address_to c, int item)
{
        int x = c->x;

        for (int k = 0; k < item; k++)
                x += ui_tab_width(ui, c, k) + (c->variant == UI_TABS_SEGMENTED ? 4 : 2);
        return x;
}

static fn ui_vtabs_draw(struct ui address_to ui, struct ui_control address_to c);

static fn ui_tabs_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 seg = c->variant == UI_TABS_SEGMENTED, live = 0;

        if (c->variant == UI_TABS_VERTICAL)
        {
                ui_vtabs_draw(ui, c);
                return;
        }
        int height = seg ? 32 : 40, chosen = c->at;
        b64 pip_x, pip_w;

        if (!seg)
        {
                struct ui_box hair = {ui_q8(ui, c->x), ui_q8(ui, c->y + height), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + height + 1), 0, 0};

                ui_kit_fill(ui, &hair, ui_role(UI_OUTLINE_VARIANT), null);
        }
        // The pip's place is the strip's own (from its left), so a strip the layout moves takes its pip with it at once.
        ui_kit_number_to(&c->pip_x, ui_q(ui_tab_left(ui, c, chosen) - c->x + (seg ? 0 : 8)), 350, &ui_ease);
        ui_kit_number_to(&c->pip_w, ui_q(ui_tab_width(ui, c, chosen) - (seg ? 0 : 16)), 350, &ui_ease);
        pip_x = ui_q(c->x) + ui_kit_tween(&c->pip_x);
        pip_w = ui_kit_tween(&c->pip_w);
        {
                struct ui_box pip = seg ? ui_box_of(ui, pip_x, ui_q(c->y), pip_w, ui_q(height), ui_q(height) / 2)
                                        : ui_box_of(ui, pip_x, ui_q(c->y + height - 1), pip_w, ui_q(2), ui_q(2));

                ui_kit_fill(ui, &pip, ui_role(UI_PRIMARY), null);
        }
        for (int item = 0; item < c->count; item++)
        {
                int x = ui_tab_left(ui, c, item), w = ui_tab_width(ui, c, item), width, baseline;
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live);
                b32 bx = ui_q8(ui, x), bw = ui_q8(ui, w), anchor = c->dom_origin > 0 ? bx : bx + bw;
                struct ui_box face = ui_box_of(ui, ui_q(x), ui_q(c->y), ui_q(w), ui_q(height), ui_q(height) / 2);
                unsigned int ink, layer;

                ui_kit_colour_to(&c->item_ink[item], item == chosen ? ui_role(seg ? UI_ON_PRIMARY : UI_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT),
                                 200, &ui_ease);
                ink = ui_kit_colour(&c->item_ink[item]);
                face = ui_box_transform(face, dom, UI_ONE, anchor, 0, 0, 0);
                // The state layer and the ripple: a tab is a button.
                if (c->item == item)
                {
                        ui_kit_number_to(&c->layer, c->down ? UI_Q2(10) : c->hover ? UI_Q2(6) : 0, 200, &ui_ease);
                        layer = (unsigned int)((ui_kit_tween(&c->layer) * 100 + UI_HALF) >> UI_Q);
                        if (layer)
                                ui_kit_fill(ui, &face, ui_mix(ink, layer, 0), null);
                        {
                                struct ui_place place = {dom, UI_ONE, anchor, 0, 0, 0};

                                ui_kit_ripples(ui, c, bx - ui_q8(ui, 8), ui_q8(ui, c->y - 8), &place, &face, ink);
                        }
                }
                width = ui_kit_measure(ui, (string_address)c->items[item], UI_LABEL_LARGE);
                {
                        b32 middle = bx + bw / 2;

                        middle = anchor + (b32)ui_qmul(middle - anchor, dom);
                        baseline = ui_kit_baseline(ui, ui_dev(ui, c->y) + (ui_dev(ui, height) - ui_dev(ui, 20)) / 2, UI_LABEL_LARGE);
                        ui_kit_text(ui, middle / 256 - width / 2, baseline, (string_address)c->items[item], UI_LABEL_LARGE, ink, 0);
                }
        }
        ui_kit.busy |= live;
}

/*
        Tabs stood on end (UI_TABS_VERTICAL): a column of items h tall and w wide,
        their text at the left 12 in, the accent pip behind the chosen one sliding
        up and down to it over .35s --ease-emphasized, the items it rolls over
        stretched 1.13 along y as it passes (130 ms of flight, anchored at their
        trailing edge), colours .2s, the hovered one's state layer and a press's
        ripple: the horizontal strip's every number, on the other axis.
*/
static fn ui_vtabs_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 live = 0;
        b64 pip_y;

        ui_kit_number_to(&c->pip_y, ui_q(c->at * c->h), 350, &ui_ease);
        pip_y = ui_q(c->y) + ui_kit_tween(&c->pip_y);
        {
                struct ui_box pip = ui_box_of(ui, ui_q(c->x), pip_y, ui_q(c->w), ui_q(c->h), ui_q(c->h) / 2 < ui_q(8) ? ui_q(c->h) / 2 : ui_q(8));

                if (!c->disabled)
                        ui_kit_fill(ui, &pip, ui_role(UI_PRIMARY), null);
                // The keys in the column: the kit's focus ring, 2 out from the chosen item.
                if (c->focused && !c->disabled)
                {
                        struct ui_box ring = pip;

                        ring.x0 -= ui_q8(ui, 2), ring.y0 -= ui_q8(ui, 2), ring.x1 += ui_q8(ui, 2), ring.y1 += ui_q8(ui, 2);
                        ring.rx += ui_q8(ui, 2), ring.ry += ui_q8(ui, 2);
                        ui_kit_ring(ui, &ring, ui_q8(ui, 2), ui_role(UI_PRIMARY));
                }
        }
        for (int item = 0; item < c->count; item++)
        {
                int y = c->y + item * c->h;
                b64 dom = ui_stretch_at(&c->item_dom[item], ui_kit.now, &live);
                b32 by = ui_q8(ui, y), bh = ui_q8(ui, c->h), anchor = c->dom_origin > 0 ? by : by + bh;
                struct ui_box face = ui_box_of(ui, ui_q(c->x), ui_q(y), ui_q(c->w), ui_q(c->h), ui_q(c->h) / 2 < ui_q(8) ? ui_q(c->h) / 2 : ui_q(8));
                unsigned int ink;
                b32 middle;

                if (y + c->h + 8 < ui->top / ui->scale || y - 8 > ui->bottom / ui->scale)
                        continue;
                ui_kit_colour_to(&c->item_ink[item], item == c->at && !c->disabled ? ui_role(UI_ON_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT), 200, &ui_ease);
                ink = ui_kit_colour(&c->item_ink[item]);
                face = ui_box_transform(face, UI_ONE, dom, 0, anchor, 0, 0);
                if (c->item == item)
                {
                        struct ui_place place = {UI_ONE, dom, 0, anchor, 0, 0};
                        unsigned int layer;

                        ui_kit_number_to(&c->layer, c->down ? UI_Q2(10) : c->hover ? UI_Q2(6) : 0, 200, &ui_ease);
                        layer = (unsigned int)((ui_kit_tween(&c->layer) * 100 + UI_HALF) >> UI_Q);
                        if (layer)
                                ui_kit_fill(ui, &face, ui_mix(ink, layer, 0), null);
                        ui_kit_ripples(ui, c, ui_q8(ui, c->x - 8), by - ui_q8(ui, 8), &place, &face, ink);
                }
                // The text at whole pixels where the stretch takes the item's middle.
                middle = anchor + (b32)ui_qmul(by + bh / 2 - anchor, dom);
                ui_kit_text(ui, ui_dev(ui, c->x + 12), ui_kit_baseline(ui, middle / 256 - ui_dev(ui, 10), UI_LABEL_LARGE),
                            (string_address)c->items[item], item == c->at ? UI_LABEL_LARGE : UI_BODY_MEDIUM, ink, 0);
        }
        ui_kit.busy |= live;
}

// The vertical cascade: each item whose middle the pip passes stretches as it does (130 ms of flight).
static fn ui_vtabs_choose(struct ui_control address_to c, int item)
{
        int from = c->at, distance = item > from ? item - from : from - item;

        if (item == from || item < 0 || item >= c->count)
                return;
        c->dom_origin = item > from ? 1 : -1;
        if (!ui_kit.motion)
                for (int k = 0; k < c->count; k++)
                {
                        int along = item > from ? k - from : from - k;

                        if (along < 0 || along > distance)
                                continue;
                        ui_stretch_go(&c->item_dom[k], ui_kit.now + (b64)along * 130 * UI_MS / distance, UI_Q2(113));
                }
        c->at = item;
}

// The tabs' cascade: each tab whose middle the pip rolls over stretches as it passes (130 ms of flight).
static fn ui_tabs_choose(struct ui address_to ui, struct ui_control address_to c, int item)
{
        if (c->variant == UI_TABS_VERTICAL)
        {
                (void)ui;
                ui_vtabs_choose(c, item);
                return;
        }
        b64 from, to, length;

        if (item == c->at)
                return;
        from = ui_q(ui_tab_left(ui, c, c->at)) + ui_q(ui_tab_width(ui, c, c->at)) / 2;
        to = ui_q(ui_tab_left(ui, c, item)) + ui_q(ui_tab_width(ui, c, item)) / 2;
        length = to > from ? to - from : from - to;
        c->dom_origin = to >= from ? 1 : -1;
        if (!ui_kit.motion && length >= UI_ONE)
                for (int k = 0; k < c->count; k++)
                {
                        b64 middle = ui_q(ui_tab_left(ui, c, k)) + ui_q(ui_tab_width(ui, c, k)) / 2;
                        b64 along = to >= from ? middle - from : from - middle;

                        if (along < -ui_q(6) || along > length + ui_q(6))
                                continue;
                        along = along < 0 ? 0 : along > length ? length : along;
                        ui_stretch_go(&c->item_dom[k], ui_kit.now + ui_qmul(ui_qdiv(along, length), 130 * UI_MS), UI_Q2(113));
                }
        c->at = item;
}

/*
        A field, raised: the label above it (body-large, the ink), 6 under it
        a well 48 tall of the surface's high face, 6 round, 14 in, its text 15
        px, with four shadows -- the light caught on its top edge, a hairline,
        and two soft ones under it -- that lift on hover and turn to the bright
        accent's glow on focus, with a 4% wash of it in the face (.18s --ease).
*/
#define UI_FIELD_BARE 1 // a field's variant: no label above it, the well at the control's top and its own height
#define UI_FIELD_DOTS 96 // the most dots a masked field shows
#define UI_FIELD_REST 0
#define UI_FIELD_HOVER 1
#define UI_FIELD_FOCUS 2

static fn ui_field_shadows(int state, b64 address_to into)
{
        // x, y, blur, spread (logical pixels, Q30) and the colour, for the three after the inset light.
        static const int numbers[3][3][4] = {
            {{0, 0, 0, 1}, {0, 1, 1, 0}, {0, 4, 10, -4}},
            {{0, 0, 0, 1}, {0, 2, 4, -1}, {0, 8, 18, -5}},
            {{0, 0, 11, 0}, {0, 0, 4, 0}, {0, 4, 10, -5}},
        };
        unsigned int shadow = ui_role(UI_SHADOW), bright = ui_role(UI_ACCENT_BRIGHT);
        unsigned int colours[3][3] = {
            {ui_mix(shadow, 5, 0), ui_mix(shadow, 4, 0), ui_mix(shadow, 11, 0)},
            {ui_mix(shadow, 7, 0), ui_mix(shadow, 6, 0), ui_mix(shadow, 13, 0)},
            {ui_mix(bright, 26, 0), ui_mix(bright, 18, 0), ui_mix(shadow, 7, 0)},
        };

        for (int k = 0; k < 3; k++)
        {
                for (int n = 0; n < 4; n++)
                        into[k * 5 + n] = ui_q(numbers[state][k][n]);
                into[k * 5 + 4] = ui_tween_colour(colours[state][k]);
        }
}

struct ui_text;
static fn ui_text_field_draw(struct ui address_to ui, struct ui_control address_to c, int left, int right, int top, int bottom);

static fn ui_input_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int state = c->focused ? UI_FIELD_FOCUS : c->hover ? UI_FIELD_HOVER : UI_FIELD_REST;
        b64 want[15], now[15], p;
        b32 bare = c->variant == UI_FIELD_BARE;
        int above = bare ? 0 : 24 + 6, tall = bare && c->h ? c->h : 48;
        b32 fy = ui_dev(ui, c->y) + ui_dev(ui, above);
        struct ui_box face = ui_box_of(ui, ui_q(c->x), ui_q(c->y + above), ui_q(c->w), ui_q(tall), ui_q(6));
        unsigned int fill;
        int baseline;
        char dots[UI_FIELD_DOTS * 3 + 1];
        const char address_to text = c->source ? c->source : c->text;

        // A masked field shows a dot for each character it holds.
        if (c->masked)
        {
                positive count = 0;

                for (positive at = 0; text[at] && count < UI_FIELD_DOTS; at++)
                        if (((unsigned char)text[at] & 0xc0) != 0x80)
                                memory_copy(dots + 3 * count++, "\xe2\x80\xa2", 3);
                dots[3 * count] = 0;
                text = dots;
        }

        // The label above (a bare field has none).
        if (!bare && c->label)
                ui_kit_text(ui, ui_dev(ui, c->x), ui_kit_baseline(ui, ui_dev(ui, c->y), UI_BODY_LARGE), (string_address)c->label, UI_BODY_LARGE,
                            ui_role(UI_ON_SURFACE), 0);
        // The shadows move as one transition (.18s --ease), from wherever they are when the state changes.
        ui_field_shadows(state, want);
        p = c->field_start ? ui_bezier_at(&ui_ease, ui_progress(ui_kit.now - c->field_start, 180 * UI_MS)) : UI_ONE;
        for (int k = 0; k < 15; k++)
                now[k] = ui_tween_mix(k % 5 == 4, ((b64 address_to)c->shadow_from)[k], ((b64 address_to)c->shadow_to)[k], p);
        if (!c->shadow_to[0][4] || state != c->field_state)
        {
                for (int k = 0; k < 15; k++)
                {
                        ((b64 address_to)c->shadow_from)[k] = c->shadow_to[0][4] ? now[k] : want[k];
                        ((b64 address_to)c->shadow_to)[k] = want[k];
                }
                c->field_start = c->shadow_to[0][4] && !ui_kit.motion ? ui_kit.now : 0;
                c->field_state = state;
                for (int k = 0; k < 15; k++)
                        now[k] = ((b64 address_to)c->shadow_from)[k];
        }
        else if (c->field_start && ui_kit.now - c->field_start >= 180 * UI_MS)
                c->field_start = 0;
        if (c->field_start)
                ui_kit.busy = 1;
        for (int k = 2; k >= 0; k--)
        {
                b32 x = (b32)((now[k * 5] * ui->scale) >> 22), y = (b32)((now[k * 5 + 1] * ui->scale) >> 22);
                b32 blur = (b32)((now[k * 5 + 2] * ui->scale) >> 22), spread = (b32)((now[k * 5 + 3] * ui->scale) >> 22);
                unsigned int colour = ui_tween_argb(now[k * 5 + 4]);

                if (blur < 32 && spread > 0)
                {
                        // A hard ring: the face grown by spread, less the face.
                        struct ui_box ring = {face.x0 - spread + x, face.y0 - spread + y, face.x1 + spread + x, face.y1 + spread + y,
                                              face.rx + spread, face.ry + spread};

                        ui_kit_ring(ui, &ring, spread, colour);
                }
                else
                        ui_kit_shadow(ui, &face, x, y, blur, spread, colour);
        }
        ui_kit_colour_to(&c->fill, c->focused ? ui_mix(ui_role(UI_ACCENT_BRIGHT), 4, ui_role(UI_SURFACE_HIGH)) : ui_role(UI_SURFACE_HIGH), 180, &ui_ease);
        fill = ui_kit_colour(&c->fill);
        ui_kit_fill(ui, &face, fill, null);
        // The light on its top edge: the face less itself a pixel down, inside it.
        {
                struct ui_box low = face;

                low.y0 += ui_q8(ui, 1);
                low.y1 += ui_q8(ui, 1);
                for (b32 y = face.y0 >> 8; y < (face.y0 >> 8) + ui_dev(ui, 6); y++)
                {
                        b32 width = ui_dev(ui, c->w), first = face.x0 >> 8;

                        if (y < ui->top || y >= ui->bottom || y >= ui->height || !ui_in_band(ui, y) || width > UI_KIT_LINE)
                                continue;
                        ui_box_row(&face, y, ui_kit_line, first, width);
                        ui_box_row(&low, y, ui_kit_clip, first, width);
                        for (b32 x = 0; x < width; x++)
                        {
                                int cover = (int)ui_kit_line[x] - (int)ui_kit_clip[x];

                                ui_kit_line[x] = ui_kit_clip[x] = 0;
                                if (cover > 0 && first + x >= 0 && first + x < ui->width)
                                {
                                        unsigned int address_to at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)(first + x);

                                        *at = ui_over(ui_premultiplied(ui_mix(ui_role(UI_HIGHLIGHT), 90, 0), (unsigned int)cover), *at);
                                }
                        }
                }
        }
        baseline = ui_kit_baseline(ui, fy + (ui_dev(ui, tall) - ui_dev(ui, 20)) / 2, UI_FIELD);
        // A field with a text widget behind it: the widget draws the text, its selection and its carets in the well.
        if (c->edit)
        {
                ui_text_field_draw(ui, c, ui_dev(ui, c->x + 14), ui_dev(ui, c->x + c->w - 14), fy, fy + ui_dev(ui, tall));
                return;
        }
        if (text[0])
        {
                // Typed in, a line wider than the well shows its end, the caret after it.
                const font_face address_to const faces[2] = {ui->regular, ui->regular};
                int room = ui_dev(ui, c->w - 28), wide = ui_run_in(ui, faces, 0, 0, (string_address)text, 15, 0, 0, 0, ui->width, 0) + ui_dev(ui, 2);
                int shift = c->focused && wide > room ? wide - room : 0;
                int drawn = ui_run_in(ui, faces, ui_dev(ui, c->x + 14) - shift, baseline, (string_address)text, 15,
                                      ui_role(UI_ON_SURFACE) & 0xffffffu, 255, ui_dev(ui, c->x + 14), ui_dev(ui, c->x + c->w - 14), 0);

                if (c->focused)
                        ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x + 14) + (drawn - shift) * 256 + 256, (baseline - ui_dev(ui, 14)) * 256,
                                                       ui_q8(ui, c->x + 14) + (drawn - shift) * 256 + 256 + ui_q8(ui, 1) + ui_q8(ui, 1) / 2,
                                                       (baseline + ui_dev(ui, 4)) * 256, 0, 0}, ui_role(UI_PRIMARY), null);
        }
        else
        {
                // The placeholder, the ink at 36%; the caret, in the accent, at its start while focused.
                if (c->items[0])
                        ui_run_in(ui, (const font_face address_to const[]){ui->regular, ui->regular}, ui_dev(ui, c->x + 14), baseline,
                                  (string_address)c->items[0], 15, ui_role(UI_ON_SURFACE) & 0xffffffu, 92, 0, ui->width, 0);
                if (c->focused)
                        ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x + 14), (baseline - ui_dev(ui, 14)) * 256,
                                                       ui_q8(ui, c->x + 14) + ui_q8(ui, 1) + ui_q8(ui, 1) / 2, (baseline + ui_dev(ui, 4)) * 256, 0, 0},
                                    ui_role(UI_PRIMARY), null);
        }
}

/*
        Progress: a 6 tall pill track in the surface's highest, the accent fill
        to its value (width over .45s --ease-emphasized), or, indeterminate, a
        34% pill sweeping from -34% to 100% every 1.5 s on the same ease, cut to
        the track.
*/
static fn ui_progress_draw(struct ui address_to ui, struct ui_control address_to c)
{
        struct ui_box track = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(6), ui_q(3)), fill = track;

        ui_kit_fill(ui, &track, ui_role(UI_SURFACE_HIGHEST), null);
        if (c->value < 0)
        {
                b64 period = 1500 * UI_MS, at = ui_kit.motion ? 0 : ui_kit.now % period;
                b64 left = -UI_Q2(34) + ui_qmul(UI_Q2(134), ui_bezier_at(&ui_ease, ui_progress(at, period)));

                fill = ui_box_of(ui, ui_q(c->x) + ui_qmul(left, ui_q(c->w)), ui_q(c->y), ui_qmul(UI_Q2(34), ui_q(c->w)), ui_q(6), ui_q(3));
                ui_kit_fill(ui, &fill, ui_role(UI_PRIMARY), &track);
                if (!ui_kit.motion)
                        ui_kit.busy = 1;
                return;
        }
        ui_kit_number_to(&c->extent, c->value, 450, &ui_ease);
        fill.x1 = fill.x0 + (b32)ui_qmul(ui_kit_tween(&c->extent), track.x1 - track.x0);
        if (fill.x1 > fill.x0)
                ui_kit_fill(ui, &fill, ui_role(UI_PRIMARY), null);
}

/*
        The kit's still pieces: a badge (22 tall, 9 in, 6 round, label-medium,
        a role's container and ink, a dot of the ink), a tag (26 tall, mono, a
        hairline), an avatar (32 round, initials in the bold, a status dot),
        a callout (14 round, a tinted box, a strong first line), a code well
        (14 round, a header strip, mono 13 lines), a key (20 tall, 2 at its
        foot), a spinner (a ring and a quarter of it in the accent, a turn in
        .7 s), a card and a window (hairline, 14 and 20 round).
*/
static fn ui_badge_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int role = c->role, width = ui_kit_measure(ui, (string_address)c->label, UI_LABEL_MEDIUM), dot = c->on ? 12 : 0;
        unsigned int fill = role == UI_PRIMARY ? ui_role(UI_PRIMARY_CONTAINER) : role < 0 ? 0 : ui_role(role + 1);
        unsigned int ink = role == UI_PRIMARY ? ui_role(UI_ON_PRIMARY_CONTAINER) : role < 0 ? ui_role(UI_ON_SURFACE_VARIANT) : ui_role(role);
        struct ui_box box;

        if (role == UI_SURFACE_HIGHEST)
                fill = ui_role(UI_SURFACE_HIGHEST), ink = ui_role(UI_ON_SURFACE_VARIANT);
        c->w = width / ui->scale + 18 + dot;
        c->h = 22;
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(22), ui_q(6));
        if (fill)
                ui_kit_fill(ui, &box, fill, null);
        if (role < 0)
                ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
        if (dot)
                ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x + 9), ui_q8(ui, c->y + 8), ui_q8(ui, c->x + 15), ui_q8(ui, c->y + 14), ui_q8(ui, 3), ui_q8(ui, 3)},
                            ink, null);
        ui_kit_text(ui, ui_dev(ui, c->x + 9 + dot), ui_kit_baseline(ui, ui_dev(ui, c->y + 3), UI_LABEL_MEDIUM), (string_address)c->label,
                    UI_LABEL_MEDIUM, ink, 0);
}

static fn ui_tag_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int width = ui_kit_measure(ui, (string_address)c->label, UI_MONO);
        unsigned int ink = c->on ? ui_role(UI_PRIMARY) : ui_role(UI_ON_SURFACE_VARIANT);
        struct ui_box box;

        c->w = width / ui->scale + 20;
        c->h = 26;
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(26), ui_q(6));
        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_LOW), null);
        ui_kit_ring(ui, &box, ui_q8(ui, 1), c->on ? ui_mix(ui_role(UI_PRIMARY), 35, ui_role(UI_OUTLINE_VARIANT)) : ui_role(UI_OUTLINE_VARIANT));
        ui_kit_text(ui, ui_dev(ui, c->x + 10), ui_kit_baseline(ui, ui_dev(ui, c->y + 5), UI_MONO), (string_address)c->label, UI_MONO, ink, 0);
}

static fn ui_avatar_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = c->w ? c->w : 32, width = ui_kit_measure(ui, (string_address)c->label, UI_TITLE_SMALL);
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(size), ui_q(size), ui_q(c->variant ? 6 : size / 2));
        const struct ui_type address_to t = &ui_types[UI_LABEL_MEDIUM];
        const font_face address_to faces[2] = {ui->bold ? ui->bold : ui->regular, ui->regular};

        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_HIGHEST), null);
        width = ui_run_in(ui, faces, 0, 0, (string_address)c->label, t->size, 0, 0, 0, ui->width, 0);
        ui_run_in(ui, faces, ui_dev(ui, c->x) + (ui_dev(ui, size) - width) / 2, ui_kit_baseline(ui, ui_dev(ui, c->y) + (ui_dev(ui, size) - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM),
                  (string_address)c->label, t->size, ui_role(UI_ON_SURFACE) & 0xffffffu, 255, 0, ui->width, 0);
        if (c->on)
        {
                struct ui_box ring = ui_box_of(ui, ui_q(c->x + size - 8 - 2), ui_q(c->y + size - 8 - 2), ui_q(13), ui_q(13), ui_q(7));
                struct ui_box dot = ui_box_of(ui, ui_q(c->x + size - 8), ui_q(c->y + size - 8), ui_q(9), ui_q(9), ui_q(5));

                ui_kit_fill(ui, &ring, ui_role(UI_SURFACE_LOW), null);
                ui_kit_fill(ui, &dot, ui_role(UI_SUCCESS), null);
        }
}

static fn ui_spinner_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = c->w ? c->w : 20;
        b64 ring = size == 14 ? ui_q(2) : size == 28 ? ui_q(3) : ui_q(5) / 2;
        b32 cx = ui_q8(ui, c->x) + ui_q8(ui, size) / 2, cy = ui_q8(ui, c->y) + ui_q8(ui, size) / 2;
        b32 outer = ui_q8(ui, size) / 2, inner = outer - (b32)((ring * ui->scale) >> 22);
        b64 turn = ui_kit.motion ? 0 : ui_progress(ui_kit.now % (700 * UI_MS), 700 * UI_MS); // linear, a turn in .7 s
        b64 sine, cosine;
        b32 first = (cx - outer) >> 8, last = (cx + outer + 255) >> 8;

        ui_sincos(ui_qmul(turn, 6746518852ll), &sine, &cosine); // 2 pi
        // The ring, and the top quarter of it (45 to 135 degrees, mitred as a border's corners are) turned.
        ui_kit_ring(ui, &(struct ui_box){cx - outer, cy - outer, cx + outer, cy + outer, outer, outer}, outer - inner, ui_role(UI_SURFACE_HIGHEST));
        for (b32 y = (cy - outer) >> 8; y < (cy + outer + 255) >> 8; y++)
        {
                if (y < ui->top || y >= ui->bottom || y >= ui->height || y < 0 || !ui_in_band(ui, y))
                        continue;
                for (b32 x = first < 0 ? 0 : first; x < last && x < ui->width; x++)
                {
                        unsigned int hits = 0;

                        for (int s = 0; s < 16; s++)
                        {
                                b64 px = ((b64)x * 256 + (s & 3) * 64 + 32 - cx), py = ((b64)y * 256 + (s >> 2) * 64 + 32 - cy);
                                // Turned back by the spin: is the point in the top quarter of the ring?
                                b64 ux = (ui_qmul(px << 22, cosine) + ui_qmul(py << 22, sine)) >> 22;
                                b64 uy = (ui_qmul(py << 22, cosine) - ui_qmul(px << 22, sine)) >> 22;
                                b64 r2 = ux * ux + uy * uy;

                                hits += r2 <= (b64)outer * outer && r2 >= (b64)inner * inner && uy < 0 && (ux < 0 ? -ux : ux) <= -uy;
                        }
                        if (hits)
                        {
                                unsigned int address_to at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x;

                                *at = ui_over(ui_premultiplied(ui_role(UI_PRIMARY), hits * UI_FULL / 16), *at);
                        }
                }
        }
        if (!ui_kit.motion)
                ui_kit.busy = 1;
}

// A surface: a card (14 round, the low face, a hairline) or a window (20 round, the high face, a hairline and a lift).
static fn ui_surface_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 window = c->kind == UI_WINDOW;
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(window ? 20 : 14));

        if (window)
        {
                ui_kit_shadow(ui, &box, 0, ui_q8(ui, 10), ui_q8(ui, 30), -ui_q8(ui, 12), ui_mix(ui_role(UI_SHADOW), 14, 0));
                ui_kit_shadow(ui, &box, 0, ui_q8(ui, 1), ui_q8(ui, 2), 0, ui_mix(ui_role(UI_SHADOW), 4, 0));
        }
        ui_kit_fill(ui, &box, ui_role(window ? UI_SURFACE_HIGH : UI_SURFACE_LOW), null);
        if (window)
        {
                struct ui_box outer = box;

                outer.x0 -= ui_q8(ui, 1), outer.y0 -= ui_q8(ui, 1), outer.x1 += ui_q8(ui, 1), outer.y1 += ui_q8(ui, 1);
                outer.rx += ui_q8(ui, 1), outer.ry += ui_q8(ui, 1);
                ui_kit_ring(ui, &outer, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
                if (c->label)
                {
                        ui_kit_text(ui, ui_dev(ui, c->x + 24), ui_kit_baseline(ui, ui_dev(ui, c->y + 24), UI_TITLE_LARGE), (string_address)c->label,
                                    UI_TITLE_LARGE, ui_role(UI_ON_SURFACE), 0);
                        ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x), ui_q8(ui, c->y + 68), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + 69), 0, 0},
                                    ui_role(UI_OUTLINE_VARIANT), null);
                }
        }
        else
                ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
}

static fn ui_callout_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int role = c->role;
        unsigned int fill = role ? ui_role(role == UI_PRIMARY ? UI_PRIMARY_CONTAINER : role + 1) : ui_role(UI_SURFACE_LOW);
        unsigned int ink = role ? ui_role(role == UI_PRIMARY ? UI_ON_PRIMARY_CONTAINER : role + 2) : ui_role(UI_ON_SURFACE_VARIANT);
        unsigned int line = role ? ui_mix(ui_role(role), 28, ui_role(UI_OUTLINE_VARIANT)) : ui_role(UI_OUTLINE_VARIANT);
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(14));

        ui_kit_fill(ui, &box, fill, null);
        ui_kit_ring(ui, &box, ui_q8(ui, 1), line);
        ui_kit_text(ui, ui_dev(ui, c->x + 16), ui_kit_baseline(ui, ui_dev(ui, c->y + 14), UI_LABEL_LARGE), (string_address)c->label,
                    UI_LABEL_LARGE, role ? ink : ui_role(UI_ON_SURFACE), 0);
        if (c->text[0])
                ui_kit_text(ui, ui_dev(ui, c->x + 16), ui_kit_baseline(ui, ui_dev(ui, c->y + 14 + 24), UI_BODY_MEDIUM), (string_address)c->text,
                            UI_BODY_MEDIUM, ink, 0);
}

static fn ui_code_draw(struct ui address_to ui, struct ui_control address_to c)
{
        struct ui_box box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(c->h), ui_q(14));
        struct ui_box head = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(37), 0);

        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_HIGHEST), null);
        ui_kit_fill(ui, &head, ui_role(UI_SURFACE_LOW), &box);
        ui_kit_fill(ui, &(struct ui_box){head.x0, head.y1 - ui_q8(ui, 1), head.x1, head.y1, 0, 0}, ui_role(UI_OUTLINE_VARIANT), null);
        ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
        ui_kit_text(ui, ui_dev(ui, c->x + 14), ui_kit_baseline(ui, ui_dev(ui, c->y + 10), UI_MONO), (string_address)c->label, UI_MONO,
                    ui_role(UI_ON_SURFACE_VARIANT), 0);
        for (int k = 0; k < c->count; k++)
                ui_kit_text(ui, ui_dev(ui, c->x + 14), ui_kit_baseline(ui, ui_dev(ui, c->y + 37 + 14 + k * 21), UI_CODE), (string_address)c->items[k],
                            UI_CODE, ui_role(UI_ON_SURFACE), 0);
}

static fn ui_kbd_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int width = ui_kit_measure(ui, (string_address)c->label, UI_MONO);
        struct ui_box box, foot;

        c->w = width / ui->scale + 10 < 20 ? 20 : width / ui->scale + 10;
        box = ui_box_of(ui, ui_q(c->x), ui_q(c->y), ui_q(c->w), ui_q(20), ui_q(3));
        foot = box;
        foot.y1 -= ui_q8(ui, 1);
        ui_kit_fill(ui, &box, ui_role(UI_OUTLINE_VARIANT), null);
        foot.x0 += ui_q8(ui, 1), foot.y0 += ui_q8(ui, 1), foot.x1 -= ui_q8(ui, 1), foot.y1 -= ui_q8(ui, 1);
        foot.rx = foot.ry = ui_q8(ui, 2);
        ui_kit_fill(ui, &foot, ui_role(UI_SURFACE_LOW), null);
        ui_kit_text(ui, ui_dev(ui, c->x) + (ui_dev(ui, c->w) - width) / 2, ui_kit_baseline(ui, ui_dev(ui, c->y + 1), UI_MONO), (string_address)c->label,
                    UI_MONO, ui_role(UI_ON_SURFACE_VARIANT), 0);
}

/*
        RISE, the entrance: a control (or a group, staggered by index times
        --rb-step 48 ms) shows from opacity 0 and scaleY 1.45, anchored at its
        top (from below) or its bottom (from above), to 1 over --rb-rise 440 ms
        on --rb-ease-rise; with reduced motion it is shown at once.
*/
struct ui_rise
{
        b64 start;   // the first frame, 1 pending, 0 shown
        int index;
        b32 from_top;
};

// The rise at a frame: its opacity and scale (Q30). Shown, both are one.
static fn ui_rise_at(struct ui_rise address_to r, b64 now, b64 address_to opacity, b64 address_to scale)
{
        b64 p, delay;

        *opacity = *scale = UI_ONE;
        if (!r->start || ui_kit.motion)
        {
                r->start = 0;
                return;
        }
        if (r->start == 1)
                r->start = now;
        delay = (b64)r->index * 48 * UI_MS;
        p = ui_bezier_at(&ui_ease_rise, ui_progress(now - r->start - delay, 440 * UI_MS));
        *opacity = p;
        *scale = UI_Q2(145) + ui_qmul(UI_ONE - UI_Q2(145), p);
        if (now - r->start - delay >= 440 * UI_MS)
                r->start = 0;
        else
                ui_kit.busy = 1;
}

// The rows a control that moved this frame reaches, with room for its ripple, its shadow and its stretch.
static fn ui_kit_mark(const struct ui address_to ui, const struct ui_control address_to c)
{
        int top = c->y - 40, bottom = c->y + (c->kind == UI_RADIOS ? c->count * UI_RADIO_PITCH : c->h) + 40;

        top = ui_dev(ui, top < 0 ? 0 : top);
        bottom = ui_dev(ui, bottom);
        if (ui_kit.damage_bottom <= ui_kit.damage_top)
                ui_kit.damage_top = top, ui_kit.damage_bottom = bottom;
        else
        {
                ui_kit.damage_top = top < ui_kit.damage_top ? top : ui_kit.damage_top;
                ui_kit.damage_bottom = bottom > ui_kit.damage_bottom ? bottom : ui_kit.damage_bottom;
        }
}

static fn ui_control_draw_one(struct ui address_to ui, struct ui_control address_to c);

// One control drawn, whatever it is; the rows it reaches marked when anything in it is moving.
static fn ui_control_draw(struct ui address_to ui, struct ui_control address_to c)
{
        b32 was = ui_kit.busy;

        ui_kit.busy = 0;
        ui_control_draw_one(ui, c);
        if (ui_kit.busy)
                ui_kit_mark(ui, c);
        ui_kit.busy |= was;
}

/*
        A control placed: what it shows is its state now, with nothing moving.
        A control is placed at its first frame, so a value set when it is made,
        or any time before it is first drawn (the settings' values come from
        the verbs after the row is made), is where it starts and not a change;
        and again whenever the kit settles (a resize, a theme, a scale) or its
        app forgets it. Only a change after that is drawn moving.
*/
static fn ui_control_place(struct ui_control address_to c)
{
        struct ui_tween address_to unplaced[] = {&c->layer, &c->fill, &c->ink, &c->label_ink, &c->stroke, &c->scale, &c->pip_x, &c->pip_w, &c->pip_y};

        for (positive k = 0; k < sizeof(unplaced) / sizeof(unplaced[0]); k++)
                unplaced[k]->placed = 0;
        for (int k = 0; k < UI_ITEMS; k++)
        {
                c->item_ink[k].placed = 0;
                ui_tween_put(&c->item_scale[k], k == c->at ? 0 : UI_ONE, UI_TWEEN_NUMBER);
                c->item_dom[k].at = 0;
        }
        ui_tween_put(&c->extent, c->kind == UI_PROGRESS ? (c->value < 0 ? 0 : c->value) : c->kind == UI_SLIDER ? 0 : c->on ? UI_ONE : 0,
                     UI_TWEEN_NUMBER);
        ui_tween_put(&c->spot, 0, UI_TWEEN_NUMBER);
        for (int k = 0; k < 3; k++)
                c->ripple[k].live = 0;
        ui_spring_stop(&c->squish.x);
        ui_spring_stop(&c->squish.y);
        memory_zero(&c->squish.shown, sizeof(c->squish.shown));
        c->squish.holding = c->squish.pulled = 0;
        ui_spring_stop(&c->held_squish);
        memory_zero(&c->drawn, sizeof(c->drawn));
        c->dom.at = 0;
        c->pip_squash = c->pip_press = 0;
        c->sliding = 0;
        c->slide.landed = 0;
        c->bend = c->bend_target = 0;
        c->moved = 0;
        c->shadow_to[0][4] = 0; // a field's shadows are put at its state
        c->field_start = 0;
        c->placed = ui_kit.epoch;
}

/*
        A list's row with no control of the kit's in it (a choice, a field's row, a command): the kit's hover state layer
        (the ink at .06, .2s) and its press ripple in the accent, across the whole row, edge to edge.
*/
static fn ui_list_row_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int x, y, w, h;
        struct ui_box row;
        struct ui_place place = {UI_ONE, UI_ONE, 0, 0, 0, 0};
        unsigned int layer;

        ui_control_row(c, &x, &y, &w, &h);
        row = (struct ui_box){ui_q8(ui, x), ui_q8(ui, y), ui_q8(ui, x + w), ui_q8(ui, y + h), 0, 0};
        ui_kit_number_to(&c->layer, c->disabled ? 0 : c->down ? UI_Q2(10) : c->hover ? UI_Q2(6) : 0, 200, &ui_ease);
        layer = (unsigned int)((ui_kit_tween(&c->layer) * 100 + UI_HALF) >> UI_Q);
        if (layer)
                ui_kit_fill(ui, &row, ui_mix(ui_role(UI_ON_SURFACE), layer, 0), null);
        ui_kit_ripples(ui, c, row.x0, row.y0, &place, &row, ui_role(UI_PRIMARY));
}

// A press on a list row: its ripple from the press, across the row (device pixels).
static fn ui_list_row_press(const struct ui address_to ui, struct ui_control address_to c, int px, int py)
{
        int x, y, w, h;

        ui_control_row(c, &x, &y, &w, &h);
        ui_kit_ripple_start(ui, c, px, py, ui_dev(ui, x), ui_dev(ui, y), ui_dev(ui, w), ui_dev(ui, h), 0);
}

/*
        Swatches: a colour each (the items, as rrggbb), 20 round and 8 apart, the last at the box's right edge and all of
        them on its middle; the chosen one (at) ringed in the ink 3 out, and the one under the pointer ringed faintly.
*/
#define UI_SWATCH 20
#define UI_SWATCH_GAP 8

static int ui_swatches_left(const struct ui address_to ui, const struct ui_control address_to c)
{
        return ui_dev(ui, c->x + c->w) - c->count * ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP) + ui_dev(ui, UI_SWATCH_GAP);
}

// The swatch under a device point, or -1: each owns its colour and half the gap either side.
static int ui_swatch_at(const struct ui address_to ui, const struct ui_control address_to c, int x, int y)
{
        int left = ui_swatches_left(ui, c) - ui_dev(ui, UI_SWATCH_GAP) / 2, step = ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP);

        if (!c->count || x < left || x >= left + c->count * step ||
            !ui_hit(ui, ui_kit_touch, x, y, 0, ui_dev(ui, c->y + c->h / 2 - UI_SWATCH / 2), ui->width, ui_dev(ui, UI_SWATCH)))
                return -1;
        return (x - left) / step;
}

static fn ui_swatches_draw(struct ui address_to ui, struct ui_control address_to c)
{
        int size = ui_dev(ui, UI_SWATCH), step = ui_dev(ui, UI_SWATCH + UI_SWATCH_GAP), left = ui_swatches_left(ui, c);
        b32 out = ui_q8(ui, 3), s = size * 256, y = (ui_dev(ui, c->y) + (ui_dev(ui, c->h) - size) / 2) * 256;

        for (int k = 0; k < c->count; k++)
        {
                long colour = window_colour_parse((string_address)c->items[k]);
                b32 x = (left + k * step) * 256;
                struct ui_box dot = {x, y, x + s, y + s, s / 2, s / 2}, ring = {x - out, y - out, x + s + out, y + s + out, s / 2 + out, s / 2 + out};

                ui_kit_fill(ui, &dot, 0xff000000u | (unsigned int)(colour < 0 ? 0 : colour), null);
                if (k == c->at)
                        ui_kit_ring(ui, &ring, ui_q8(ui, 2), ui_role(UI_ON_SURFACE));
                else if (c->hover && k == c->item && !c->disabled)
                        ui_kit_ring(ui, &ring, ui_q8(ui, 2), ui_mix(ui_role(UI_ON_SURFACE), 30, 0));
        }
}

static fn ui_control_draw_one(struct ui address_to ui, struct ui_control address_to c)
{
        if (c->placed != ui_kit.epoch)
                ui_control_place(c);
        switch (c->kind)
        {
        case UI_LIST_ROW: ui_list_row_draw(ui, c); break;
        case UI_SWATCHES: ui_swatches_draw(ui, c); break;
        case UI_BUTTON: ui_button_draw(ui, c); break;
        case UI_SWITCH:
        case UI_CHECKBOX: ui_toggle_draw(ui, c); break;
        case UI_RADIOS: ui_radios_draw(ui, c); break;
        case UI_SLIDER: ui_slider_draw(ui, c); break;
        case UI_TABS: ui_tabs_draw(ui, c); break;
        case UI_INPUT: ui_input_draw(ui, c); break;
        case UI_PROGRESS: ui_progress_draw(ui, c); break;
        case UI_BADGE: ui_badge_draw(ui, c); break;
        case UI_TAG: ui_tag_draw(ui, c); break;
        case UI_AVATAR: ui_avatar_draw(ui, c); break;
        case UI_CALLOUT: ui_callout_draw(ui, c); break;
        case UI_CODE_WELL: ui_code_draw(ui, c); break;
        case UI_KBD: ui_kbd_draw(ui, c); break;
        case UI_SPINNER: ui_spinner_draw(ui, c); break;
        case UI_CARD:
        case UI_WINDOW: ui_surface_draw(ui, c); break;
        case UI_HEADING:
                ui_kit_text(ui, ui_dev(ui, c->x), ui_kit_baseline(ui, ui_dev(ui, c->y), c->variant), (string_address)c->label, c->variant,
                            ui_role(c->role), 0);
                break;
        case UI_RULE:
                ui_kit_fill(ui, &(struct ui_box){ui_q8(ui, c->x), ui_q8(ui, c->y), ui_q8(ui, c->x + c->w), ui_q8(ui, c->y + 1), 0, 0},
                            ui_role(UI_OUTLINE_VARIANT), null);
                break;
        }
}

/* ---- the pointer ---- */

// Whether a device point is in a control's box (a radio group: which row; tabs: which tab), -1 when not.
static int ui_control_hit(struct ui address_to ui, const struct ui_control address_to c, int x, int y)
{
        int lx = x / ui->scale, ly = y / ui->scale;

        if (c->kind == UI_RADIOS)
        {
                for (int k = 0; k < c->count; k++)
                        if (lx >= c->x && lx < c->x + c->w && ly >= ui_radio_row_y(c, k) && ly < ui_radio_row_y(c, k) + c->h)
                                return k;
                return -1;
        }
        if (c->kind == UI_TABS && c->variant == UI_TABS_VERTICAL)
                return lx >= c->x && lx < c->x + c->w && ly >= c->y && ly < c->y + c->count * c->h ? (ly - c->y) / c->h : -1;
        if (c->kind == UI_TABS)
        {
                for (int k = 0; k < c->count; k++)
                {
                        int left = ui_tab_left(ui, c, k);

                        if (lx >= left && lx < left + ui_tab_width(ui, c, k) && ly >= c->y && ly < c->y + (c->variant ? 32 : 40))
                                return k;
                }
                return -1;
        }
        if (c->kind == UI_INPUT && c->variant == UI_FIELD_BARE)
                return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h ? c->h : 48)) ? 0 : -1;
        if (c->kind == UI_INPUT)
                return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + 30), ui_dev(ui, c->w), ui_dev(ui, 48)) ? 0 : -1;
        if (c->kind == UI_SWATCHES)
                return ui_swatch_at(ui, c, x, y);
        (void)lx, (void)ly;
        return ui_hit(ui, ui_kit_touch, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h)) ? 0 : -1;
}

static b32 ui_control_live(const struct ui_control address_to c)
{
        return !c->disabled && (c->kind == UI_BUTTON || c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS ||
                                c->kind == UI_SLIDER || c->kind == UI_TABS || c->kind == UI_INPUT);
}

// The slider's value under a device x: ui.js's a(), the track's whole width, snapped to the step.
static b64 ui_slider_at(struct ui address_to ui, const struct ui_control address_to c, int x)
{
        b64 ratio = ui_qdiv(ui_q(x - ui_dev(ui, c->x)), ui_q(ui_dev(ui, c->w)));

        ratio = ratio < 0 ? 0 : ratio > UI_ONE ? UI_ONE : ratio;
        return ui_slider_snap(c, c->low + ui_qmul(ratio, c->high - c->low));
}

/*
        The pointer's events for the controls: motion, a press and a release
        (the left button), each as ui.js's handlers take them. Answers the
        control whose value changed (a toggle, a choice, a slider's value, a
        button's click), or null. A control holding the pointer takes every
        motion till the release (setPointerCapture).
*/
static struct ui_control address_to ui_kit_held;

static struct ui_control address_to ui_kit_pointer(struct ui address_to ui, struct ui_control address_to controls, int count, int type, int x, int y,
                                                   b32 down, unsigned int flags)
{
        struct ui_control address_to changed = null;

        ui_kit_touch = (flags & WINDOW_KEY_TOUCH) != 0;
        // A press the compositor takes back (a second finger, a finger held for the right button) clicks nothing:
        // what it held springs home as a release would, and no value changes.
        if (type == WINDOW_EVENT_BUTTON && !down && (flags & WINDOW_KEY_CANCEL) && ui_kit_held)
        {
                struct ui_control address_to c = ui_kit_held;

                ui_kit_held = null;
                c->down = c->moved = 0;
                c->sliding = c->sliding && c->kind == UI_SLIDER;
                ui_kit_ripples_exit(c);
                ui_squish_release(&c->squish);
                if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX)
                        ui_kit_number_to(&c->extent, c->on ? UI_ONE : 0, 200, &ui_ease);
                c->bend = c->bend_target = 0;
                return null;
        }

        if (type == WINDOW_EVENT_MOTION)
        {
                struct ui_control address_to c = ui_kit_held;

                if (c)
                {
                        int dx = x - c->press_x, dy = y - c->press_y;
                        b64 ldx = ui_qdiv(ui_q(dx), ui_q(ui->scale)), ldy = ui_qdiv(ui_q(dy), ui_q(ui->scale));

                        c->pointer_x = x, c->pointer_y = y;
                        if (c->kind == UI_BUTTON)
                        {
                                ui_squish_move(&c->squish, ui_squash_of(ldx, ldy));
                                // pointerleave ends the ripple.
                                if (ui_control_hit(ui, c, x, y) < 0)
                                        ui_kit_ripples_exit(c);
                        }
                        else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS)
                        {
                                b32 right = dx > 0, toward = right != (c->kind == UI_RADIOS ? c->item == c->at : c->on);
                                b64 fill;

                                if (!c->moved && dx < 3 * ui->scale && dx > -3 * ui->scale && dy < 3 * ui->scale && dy > -3 * ui->scale)
                                        return null;
                                c->moved = 1;
                                if (c->kind != UI_RADIOS)
                                {
                                        fill = (c->on ? UI_ONE : 0) + ldx / 100;
                                        fill = fill < 0 ? 0 : fill > UI_ONE ? UI_ONE : fill;
                                        ui_tween_put(&c->extent, fill, UI_TWEEN_NUMBER);
                                }
                                c->bend = ui_qmul(ui_squash_curve((ldx < 0 ? -ldx : ldx) / 52), toward ? UI_ONE : UI_Q2(30));
                                c->squish_origin = right ? 1 : -1;
                                ui_squish_move(&c->squish, ui_squash_of(ldx, ldy));
                        }
                        else if (c->kind == UI_SLIDER)
                        {
                                b64 over = 0, f = ldy;
                                int left = ui_dev(ui, c->x), right = left + ui_dev(ui, c->w);

                                if (!c->moved && dx < 4 * ui->scale && dx > -4 * ui->scale)
                                        return null;
                                c->moved = 1;
                                c->sliding = 0;
                                c->value = ui_slider_at(ui, c, x);
                                c->spot_x = x / ui->scale - c->x;
                                changed = c;
                                over = x < left ? ui_q(left - x) : x > right ? ui_q(x - right) : 0;
                                c->bend_target = over > 0 ? ui_qmul(ui_squash_curve(ui_qdiv(over, ui_q(right - left)) * 2), UI_Q2(130)) : 0;
                                if (over > 0)
                                        c->squish_origin = x < left ? -1 : 1;
                                {
                                        struct ui_squash s = {0, ui_squash_curve((f < 0 ? -f : f) / 400), 0, 0};

                                        s.ty = f > 0 ? s.sy * 8 : f < 0 ? -s.sy * 8 : 0;
                                        ui_squish_move(&c->squish, s);
                                }
                        }
                        return changed;
                }
                for (int k = 0; k < count; k++)
                {
                        struct ui_control address_to c = &controls[k];
                        int hit = ui_control_live(c) ? ui_control_hit(ui, c, x, y) : -1;

                        if ((hit >= 0) != c->hover || (hit >= 0 && hit != c->item))
                        {
                                if (hit < 0 && c->hover)
                                        ui_kit_ripples_exit(c);
                                c->hover = hit >= 0;
                                c->item = hit;
                        }
                        if (c->kind == UI_SLIDER)
                                c->hover_x = hit >= 0 ? x : -1;
                }
                return null;
        }
        if (type != WINDOW_EVENT_BUTTON)
                return null;
        if (down)
        {
                // A press takes the focus from wherever it was: a field's, and a button's (a click focuses a button in Chrome).
                for (int k = 0; k < count; k++)
                        controls[k].focused = 0;
                for (int k = 0; k < count; k++)
                {
                        struct ui_control address_to c = &controls[k];
                        int hit = ui_control_live(c) ? ui_control_hit(ui, c, x, y) : -1;

                        if (hit < 0)
                                continue;
                        if (c->kind == UI_BUTTON)
                                c->focused = 1; // button[outlined]:focus: its border is the accent
                        ui_kit_held = c;
                        c->down = 1;
                        c->moved = 0;
                        c->item = hit;
                        c->hover = 1;
                        c->press_x = c->pointer_x = x, c->press_y = c->pointer_y = y;
                        c->pressed_at = ui_kit.now;
                        ui_squish_press(&c->squish);
                        if (c->kind == UI_BUTTON)
                                ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y), ui_dev(ui, c->w), ui_dev(ui, c->h), 8);
                        else if (c->kind == UI_TABS)
                                ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, ui_tab_left(ui, c, hit)), ui_dev(ui, c->y), ui_dev(ui, ui_tab_width(ui, c, hit)),
                                                    ui_dev(ui, c->variant ? 32 : 40), 8);
                        else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_LIST_ROW)
                        {
                                ui_spring_stop(&c->held_squish);
                                ui_list_row_press(ui, c, x, y);
                        }
                        else if (c->kind == UI_RADIOS)
                                ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x - 24), ui_dev(ui, ui_radio_row_y(c, hit) - 8), ui_dev(ui, c->w + 48),
                                                    ui_dev(ui, c->h + 16), 0);
                        else if (c->kind == UI_INPUT)
                                c->focused = 1;
                        else if (c->kind == UI_SLIDER)
                        {
                                b64 from = c->value, to = ui_slider_at(ui, c, x);

                                ui_spring_stop(&c->held_squish);
                                c->slide.landed = 0;
                                c->bend = c->bend_target = 0;
                                c->squish_origin = 0;
                                c->last_frame = 0;
                                c->bounce = 0;
                                c->slide_from = from;
                                if ((to > from ? to - from : from - to) > 1073741 && !ui_kit.motion) // 0.001
                                {
                                        ui_slide_go(&c->slide, from, to, c->high - c->low, ui_kit.now);
                                        c->sliding = c->bounce = 1;
                                        c->squish_origin = to >= from ? 1 : -1;
                                }
                                else
                                        c->value = to;
                                if (c->value != from || c->sliding)
                                        changed = c;
                                c->spot_x = x / ui->scale - c->x;
                                ui_tween_put(&c->spot, UI_Q2(10), UI_TWEEN_NUMBER);
                                c->hover_x = -1; // v(B): the hover preview is over till the pointer moves again
                        }
                        break;
                }
                return changed;
        }
        // The release.
        {
                struct ui_control address_to c = ui_kit_held;

                ui_kit_held = null;
                if (!c)
                        return null;
                c->down = 0;
                ui_kit_ripples_exit(c);
                if (c->kind == UI_BUTTON)
                {
                        ui_squish_release(&c->squish);
                        if (ui_control_hit(ui, c, x, y) >= 0)
                                changed = c;
                }
                else if (c->kind == UI_TABS)
                {
                        ui_squish_release(&c->squish);
                        if (ui_control_hit(ui, c, x, y) == c->item)
                        {
                                ui_tabs_choose(ui, c, c->item);
                                changed = c;
                        }
                }
                else if (c->kind == UI_SWITCH || c->kind == UI_CHECKBOX || c->kind == UI_RADIOS)
                {
                        int dx = x - c->press_x;
                        b64 ldx = ui_qdiv(ui_q(dx), ui_q(ui->scale));
                        b32 right = dx > 0;

                        b32 was = c->kind == UI_RADIOS ? c->item == c->at : c->on;

                        if ((c->moved && right != was && (ldx > ui_q(100) || ldx < -ui_q(100))) || !c->moved)
                        {
                                if (c->kind == UI_RADIOS)
                                {
                                        if (c->item != c->at)
                                        {
                                                ui_radios_choose(c, c->item);
                                                changed = c;
                                        }
                                        else if (!ui_kit.motion)
                                        {
                                                // Pressed again on the chosen circle: a squish in place, and the pip's own.
                                                int lx = x / ui->scale;

                                                if (lx >= c->x + c->w - 40 && lx < c->x + c->w - 12)
                                                {
                                                        c->dom_origin = 0;
                                                        ui_stretch_go(&c->item_dom[c->at], ui_kit.now, UI_Q2(84));
                                                        c->pip_press = 1;
                                                }
                                        }
                                }
                                else
                                {
                                        c->on = !c->on;
                                        changed = c;
                                        // The change: the row stretches toward where the knob went.
                                        c->dom_origin = c->on ? 1 : -1;
                                        if (!ui_kit.motion)
                                                ui_stretch_go(&c->dom, ui_kit.now, UI_Q2(111));
                                }
                        }
                        if (c->kind != UI_RADIOS)
                        {
                                // The bars go back to the width their state gives, from where the hand left them.
                                c->moved = 0;
                                ui_kit_number_to(&c->extent, c->on ? UI_ONE : 0, 200, &ui_ease);
                        }
                        c->moved = 0;
                        if (c->bend > UI_REST_SCALE && !ui_kit.motion)
                                ui_spring_go(&c->held_squish, c->bend, 0);
                        c->bend = 0;
                        ui_squish_release(&c->squish);
                }
                else if (c->kind == UI_SLIDER)
                {
                        b64 cur = c->bend > 3221225 ? c->bend : 0;

                        if (c->moved && cur > 0 && !ui_kit.motion)
                                ui_spring_go(&c->held_squish, cur, 0);
                        else if (c->bounce && !c->moved && !ui_kit.motion)
                                c->slide.landed = ui_kit.now;
                        else
                                c->squish_origin = 0;
                        c->bend = c->bend_target = 0;
                        c->moved = 0;
                        ui_kit_number_to(&c->spot, 0, 300, &ui_ease);
                        ui_squish_release(&c->squish); // pressSpring(B, 0, vy, 0, vyT): the drag's vertical press only
                }
                return changed;
        }
}

/*
        How pressed looks, for what is not one of the kit's controls (a row, a list's item): the kit's pressed state
        layer, the ink at .10 over what is drawn (button:active::after), at the rows' radius, with its edges where
        they fall.
*/
static fn ui_press_fill(struct ui address_to ui, int x, int y, int width, int height)
{
        b32 radius = (UI_RADIUS - 2) * ui->scale * 256;
        struct ui_box box = {x * 256, y * 256, (x + width) * 256, (y + height) * 256, radius, radius};

        ui_kit_fill(ui, &box, ui_mix(0xff000000u | ui->ink, 10, 0), null);
}

/*
        The scrollbar, every scrolling region's (ui.css's ::-webkit-scrollbar): a
        track 10 wide at the region's right edge, a clear 3 round a pill thumb (4
        wide), the ink at .18 and .30 while hovered or held, shown whenever what
        scrolls is taller than its view and never hidden. The thumb is the view's
        share of the track (24 at least) where the scroll's share of what is left
        puts it; it is dragged, a press on the track pages a view toward the
        press, and the wheel and a finger go on scrolling the region itself. An
        app gives the region (x its track's left, y and height its view's) and the
        three numbers, all device pixels; the kit draws it and takes its pointer.
        The terminal's scrollback and an editor's view take it the same way.
*/
#define UI_SCROLL_WIDTH 10
#define UI_SCROLL_BORDER 3
#define UI_SCROLL_LEAST 24

struct ui_scroll
{
        int x, y, height;            // the track: its left, its top and its height
        int content, view, scroll;   // what scrolls, what shows of it, how far it is scrolled
        b32 hover, held;
        int grab;                    // where in the thumb the hand took it
        int unit;                    // it scrolls in whole units of this (a menu's choices), or pixels (0)
        int wheel, left;             // a wheel's turn under a unit, and a finger's move under one, kept for the next
};

// The thumb's top and length (device pixels); false when everything shows and there is no bar.
static b32 ui_scroll_thumb(const struct ui address_to ui, const struct ui_scroll address_to s, int address_to top, int address_to length)
{
        int track = s->height, least = UI_SCROLL_LEAST * ui->scale, rest = s->content - s->view, scroll;

        if (s->content <= s->view || s->view <= 0 || track <= 0)
                return 0;
        *length = (int)((b64)track * s->view / s->content);
        *length = *length < least ? least : *length > track ? track : *length;
        scroll = s->scroll < 0 ? 0 : s->scroll > rest ? rest : s->scroll;
        *top = s->y + (int)((b64)(track - *length) * scroll / rest);
        return 1;
}

static fn ui_scroll_draw(struct ui address_to ui, const struct ui_scroll address_to s)
{
        int top, length, border = UI_SCROLL_BORDER * ui->scale, width = UI_SCROLL_WIDTH * ui->scale;
        struct ui_box thumb;

        if (!ui_scroll_thumb(ui, s, &top, &length))
                return;
        thumb = (struct ui_box){(s->x + border) * 256, (top + border) * 256, (s->x + width - border) * 256, (top + length - border) * 256,
                                (width / 2 - border) * 256, (width / 2 - border) * 256};
        ui_kit_fill(ui, &thumb, ui_mix(ui_role(UI_ON_SURFACE), s->hover || s->held ? 30 : 18, 0), null);
}

/*
        The pointer on a scrollbar: motion (a held thumb follows the hand; hover lights it), a press (on the thumb it is
        held, on the track it pages) and a release. Answers the scroll it asks for, or -1 when the event was not its own
        (a press off it, a motion with nothing held); a press on it that pages answers the new scroll.
*/
static int ui_scroll_pointer(const struct ui address_to ui, struct ui_scroll address_to s, int type, int x, int y, b32 down)
{
        int top, length, rest = s->content - s->view;
        b32 bar = ui_scroll_thumb(ui, s, &top, &length);
        b32 on_track = bar && x >= s->x && x < s->x + UI_SCROLL_WIDTH * ui->scale && y >= s->y && y < s->y + s->height;

        if (type == WINDOW_EVENT_MOTION)
        {
                s->hover = on_track && y >= top && y < top + length;
                if (!s->held || !bar)
                        return -1;
                if (s->height - length <= 0)
                        return s->scroll;
                return (int)((b64)(y - s->grab - s->y) * rest / (s->height - length));
        }
        if (type != WINDOW_EVENT_BUTTON)
                return -1;
        if (!down)
        {
                b32 was = s->held;

                s->held = 0;
                return was ? s->scroll : -1;
        }
        if (!on_track)
                return -1;
        if (y >= top && y < top + length)
        {
                s->held = 1;
                s->grab = y - top;
                return s->scroll;
        }
        return y < top ? s->scroll - s->view : s->scroll + s->view;
}

/*
        How a region scrolls, whatever moves it: it is held between its top and its end, in whole units where it has
        them; a band of what scrolls is brought into view; the wheel turns it a row a notch (a unit, where it has
        them), a finger moves it pixel for pixel; and the keys move a text a line, a view, or to an end. Each answers
        whether it moved, which is what the app draws again.
*/
static int ui_scroll_most(const struct ui_scroll address_to s)
{
        int most = s->content - s->view;

        return most > 0 ? most : 0;
}

static b32 ui_scroll_set(struct ui_scroll address_to s, int scroll)
{
        int most = ui_scroll_most(s);

        scroll = scroll > most ? most : scroll < 0 ? 0 : scroll;
        if (s->unit > 0)
                scroll -= scroll % s->unit;
        if (scroll == s->scroll)
                return false;
        s->scroll = scroll;
        return true;
}

static b32 ui_scroll_reveal(struct ui_scroll address_to s, int top, int height)
{
        if (top < s->scroll)
                return ui_scroll_set(s, top);
        if (top + height > s->scroll + s->view)
                return ui_scroll_set(s, top + height - s->view + (s->unit > 0 ? s->unit - 1 : 0));
        return ui_scroll_set(s, s->scroll);
}

// A finger's move of the region by pixels (whole units at a time where it has them): false only at an end it could not pass.
static b32 ui_scroll_by(struct ui_scroll address_to s, int pixels)
{
        if (s->unit > 0)
        {
                long total = (long)s->left + pixels;
                int steps = (int)(total / s->unit);

                s->left = (int)(total - (long)steps * s->unit);
                return !steps || ui_scroll_set(s, s->scroll + steps * s->unit);
        }
        return !pixels || ui_scroll_set(s, s->scroll + pixels);
}

// A turn of the wheel, in the 120ths of a notch the compositor sends: what is under a step is kept for the next turn.
static b32 ui_scroll_wheel(const struct ui address_to ui, struct ui_scroll address_to s, int delta)
{
        int per = s->unit > 0 ? 1 : UI_ROW * ui->scale;
        long total = (long)s->wheel + (long)delta * per;
        int steps = (int)(total / 120);

        s->wheel = (int)(total - (long)steps * 120);
        return steps && ui_scroll_set(s, s->scroll - steps * (s->unit > 0 ? s->unit : 1));
}

/* ---- end of the kit's controls ---- */

// Anonymous memory for an app's own state, or a negative errno.
static long ui_map(positive size)
{
        return compositor_call(syscall(mmap), 0, size, 3, 0x22, (positive)-1, 0);
}

// A font engine and its atlas, atlas pixels a side, as one mapping. The default, 512, is 256 KiB of the image's address space
// in every program that links this, a shell under a 12 MiB cap among them. False when the memory is not there.
static b32 ui_engine_open_sized(struct ui address_to ui, positive atlas)
{
        positive atlas_at = (sizeof(font_engine) + 4095) & ~(positive)4095;
        long engine = ui_map(atlas_at + atlas * atlas);

        if (engine < 0 && engine > -4096)
                return false;
        ui->engine = (font_engine address_to)engine;
        font_start(ui->engine, (p8 address_to)(engine + atlas_at), (p32)atlas, (p32)atlas);
        return true;
}

static b32 ui_engine_open(struct ui address_to ui)
{
        return ui_engine_open_sized(ui, 512);
}

// A face from a font file, loaded into the engine's face. False when there is no path, the file, or a font in it.
static b32 ui_face_load(font_face address_to face, string_address path)
{
        positive size = 0, bytes = path ? compositor_map_file(path, &size) : 0;

        return bytes && font_load(face, (const p8 address_to)bytes, size, 0) == 0;
}

// The fonts an app draws in when it names none.
#define UI_FONT_REGULAR "/usr/share/fonts/inter/Inter-Regular.ttf"
#define UI_FONT_BOLD "/usr/share/fonts/inter/Inter-Bold.ttf"
#define UI_FONT_MONO "/usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf"

// The scratch line the text is drawn through is at least this wide: it is mapped again only when it must grow.
static void ui_scratch_fit(struct ui address_to ui, unsigned int width)
{
        unsigned int scratch = width + 8u * (unsigned int)ui->scale;
        long mapped;

        if (scratch <= (unsigned int)ui->scratch_width)
                return;
        mapped = ui_map((positive)scratch * UI_SCRATCH_ROWS * (positive)ui->scale * 4);
        if (mapped < 0 && mapped > -4096)
                return;
        if (ui->scratch)
                compositor_call(syscall(munmap), (positive)ui->scratch,
                                (positive)ui->scratch_width * UI_SCRATCH_ROWS * (positive)ui->scale * 4, 0, 0, 0, 0);
        ui->scratch = (unsigned int address_to)mapped;
        ui->scratch_width = (int)scratch;
}

/* ---- menus: a popup surface and the list in it ---- */

/*
        A menu is a popup row of its own (window.c's popup columns), so it reaches past its window's edges and the
        compositor places it, flipping it where the screen ends. Its items are labels with an optional accelerator's
        text, a mnemonic marked with & (the letter typed chooses it), check and radio marks, separators, section
        headers and disabled items; a submenu item is answered UI_MENU_OPEN, for the app to open another popup against
        the item with WINDOW_POPUP_RIGHT (the hover delay and the pointer's safe triangle toward it are not built yet).
        The keys while it is open: Up and Down (Home and End) move over what can be chosen, Enter and Space choose,
        Right answers a submenu's item and Left closes, Escape closes, letters jump to the first item they
        start (a mnemonic chooses). Rows are 28 logical pixels, 44 when a finger opened it; a menu taller than its
        screen scrolls with the wheel and the keys. Colours are the kit's: the surface container, the selection's
        layer for the item under the hand, the outline between groups.
*/
#define UI_MENU_ITEMS 40
#define UI_MENU_SEPARATOR 1u
#define UI_MENU_DISABLED 2u
#define UI_MENU_CHECK 4u   // a check mark when UI_MENU_ON
#define UI_MENU_RADIO 8u   // a dot when UI_MENU_ON
#define UI_MENU_ON 16u
#define UI_MENU_HEADER 32u
#define UI_MENU_SUBMENU 64u // id names the submenu the app opens for it
#define UI_MENU_ROW 28
#define UI_MENU_TOUCH_ROW 44
#define UI_MENU_PAD 6
#define UI_MENU_HOVER_NS 300000000ull

struct ui_menu_item
{
        const char address_to label, address_to keys;
        p32 flags;
        int id;
};

struct ui_popmenu
{
        struct ui_menu_item items[UI_MENU_ITEMS];
        int count, hot, top, rows_shown;
        b32 touch, pressed;
        int width, height;
        char typed[16];
        int typed_length;
        positive typed_at;
};

static fn ui_popmenu_add(struct ui_popmenu address_to m, const char address_to label, const char address_to keys, p32 flags, int id)
{
        if (m->count < UI_MENU_ITEMS)
                m->items[m->count++] = (struct ui_menu_item){label, keys, flags, id};
}

static b32 ui_popmenu_choosable(const struct ui_popmenu address_to m, int at)
{
        return at >= 0 && at < m->count && !(m->items[at].flags & (UI_MENU_SEPARATOR | UI_MENU_DISABLED | UI_MENU_HEADER));
}

static int ui_popmenu_row(const struct ui address_to ui, const struct ui_popmenu address_to m)
{
        return (m->touch ? UI_MENU_TOUCH_ROW : UI_MENU_ROW) * ui->scale;
}

// A label as drawn: its & left out (and && one &).
static positive ui_popmenu_shown(const char address_to label, p8 address_to into, positive room, p32 address_to mnemonic)
{
        positive out = 0;

        *mnemonic = 0;
        for (positive at = 0; label && label[at] && out + 1 < room; at++)
        {
                if (label[at] == '&' && label[at + 1] && label[at + 1] != '&')
                {
                        *mnemonic = (p32)byte_to_lower((b8)label[at + 1]);
                        continue;
                }
                if (label[at] == '&')
                        at++;
                into[out++] = (p8)label[at];
        }
        into[out] = 0;
        return out;
}

// How big the menu is drawn (device pixels), and how many rows show in at most limit pixels of height.
static fn ui_popmenu_size(struct ui address_to ui, struct ui_popmenu address_to m, int limit)
{
        int widest = 0, height = 2 * UI_MENU_PAD * ui->scale;

        for (int at = 0; at < m->count; at++)
        {
                p8 shown[96];
                p32 mnemonic;
                int wide;

                if (m->items[at].flags & UI_MENU_SEPARATOR)
                {
                        height += (2 * UI_MENU_PAD + 1) * ui->scale;
                        continue;
                }
                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                wide = ui_kit_measure(ui, shown, UI_BODY_MEDIUM) + (m->items[at].keys ? ui_kit_measure(ui, (string_address)m->items[at].keys, UI_BODY_MEDIUM) + 32 * ui->scale : 0);
                widest = wide > widest ? wide : widest;
                height += ui_popmenu_row(ui, m);
        }
        m->width = widest + 2 * (UI_MENU_PAD + 30) * ui->scale;
        m->height = limit > 0 && height > limit ? limit : height;
        m->rows_shown = (m->height - 2 * UI_MENU_PAD * ui->scale) / ui_popmenu_row(ui, m);
}

// The item at y (device pixels from the menu's top), or -1.
static int ui_popmenu_at(const struct ui address_to ui, const struct ui_popmenu address_to m, int y)
{
        int top = UI_MENU_PAD * ui->scale;

        for (int at = m->top; at < m->count; at++)
        {
                int tall = m->items[at].flags & UI_MENU_SEPARATOR ? (2 * UI_MENU_PAD + 1) * ui->scale : ui_popmenu_row(ui, m);

                if (y >= top && y < top + tall)
                        return m->items[at].flags & UI_MENU_SEPARATOR ? -1 : at;
                top += tall;
        }
        return -1;
}

static fn ui_popmenu_draw(struct ui address_to ui, struct ui_popmenu address_to m)
{
        struct ui_box face = ui_box_of(ui, 0, 0, ui_q(m->width / ui->scale), ui_q(m->height / ui->scale), ui_q(8));
        int y = UI_MENU_PAD * ui->scale, row = ui_popmenu_row(ui, m);

        ui_kit_fill(ui, &face, ui_role(UI_SURFACE_CONTAINER), null);
        ui_kit_ring(ui, &face, ui->scale, ui_role(UI_OUTLINE_VARIANT));
        for (int at = m->top; at < m->count && y < m->height - UI_MENU_PAD * ui->scale; at++)
        {
                const struct ui_menu_item address_to item = &m->items[at];
                unsigned int ink = ui_role(UI_ON_SURFACE);
                p8 shown[96];
                p32 mnemonic;
                int baseline = y + (row - ui_capital(ui, 14)) / 2 + ui_capital(ui, 14);
                unsigned int alpha = item->flags & UI_MENU_DISABLED ? 97 : 255;

                if (item->flags & UI_MENU_SEPARATOR)
                {
                        ui_fill(ui, UI_MENU_PAD * ui->scale, y + UI_MENU_PAD * ui->scale, m->width - 2 * UI_MENU_PAD * ui->scale, ui->scale,
                                ui_tone(ui_role(UI_OUTLINE_VARIANT) & 0xffffffu, 255));
                        y += (2 * UI_MENU_PAD + 1) * ui->scale;
                        continue;
                }
                if (at == m->hot && ui_popmenu_choosable(m, at))
                {
                        struct ui_box lit = ui_box_of(ui, ui_q(UI_MENU_PAD), ui_q(y / ui->scale), ui_q((m->width - 2 * UI_MENU_PAD * ui->scale) / ui->scale),
                                                      ui_q(row / ui->scale), ui_q(6));

                        ui_kit_fill(ui, &lit, ui_role(UI_SELECTION), null);
                }
                ui_popmenu_shown(item->label, shown, sizeof(shown), &mnemonic);
                if (item->flags & UI_MENU_HEADER)
                        ui_kit_text(ui, (UI_MENU_PAD + 12) * ui->scale, baseline, shown, UI_LABEL_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0);
                else
                        ui_kit_text(ui, (UI_MENU_PAD + 30) * ui->scale, baseline, shown, UI_BODY_MEDIUM, (alpha << 24) | (ink & 0xffffffu), 0);
                if ((item->flags & (UI_MENU_CHECK | UI_MENU_RADIO)) && (item->flags & UI_MENU_ON))
                {
                        int cx = (UI_MENU_PAD + 15) * ui->scale, cy = y + row / 2;

                        if (item->flags & UI_MENU_RADIO)
                                ui_round(ui, cx - 3 * ui->scale, cy - 3 * ui->scale, 6 * ui->scale, 6 * ui->scale, 3 * ui->scale, ui_tone(ink & 0xffffffu, alpha));
                        else
                                ui_kit_text(ui, cx - 5 * ui->scale, baseline, (string_address) "\xe2\x9c\x93", UI_BODY_MEDIUM, (alpha << 24) | (ink & 0xffffffu), 0);
                }
                if (item->keys)
                        ui_kit_text(ui, m->width - (UI_MENU_PAD + 14) * ui->scale - ui_kit_measure(ui, (string_address)item->keys, UI_BODY_MEDIUM), baseline,
                                    (string_address)item->keys, UI_BODY_MEDIUM, (alpha * 6 / 10) << 24 | (ink & 0xffffffu), 0);
                if (item->flags & UI_MENU_SUBMENU)
                        ui_kit_text(ui, m->width - (UI_MENU_PAD + 14) * ui->scale, baseline, (string_address) "\xe2\x80\xba", UI_BODY_MEDIUM,
                                    (alpha << 24) | (ink & 0xffffffu), 0);
                y += row;
        }
}

// The hot item moved by step over what can be chosen (wrapping), the view following it.
static fn ui_popmenu_step(struct ui_popmenu address_to m, int step)
{
        for (int tries = 0, at = m->hot; tries < m->count; tries++)
        {
                at = (at + step + m->count) % m->count;
                if (ui_popmenu_choosable(m, at))
                {
                        m->hot = at;
                        break;
                }
        }
        if (m->hot < m->top)
                m->top = m->hot;
        if (m->rows_shown > 0 && m->hot >= m->top + m->rows_shown)
                m->top = m->hot - m->rows_shown + 1;
}

/*
        An event of the menu's row: the id of the item chosen, UI_MENU_NOTHING, UI_MENU_GONE (Escape, Left at the top,
        the compositor asking it to go), or UI_MENU_OPEN for a submenu's item (its id in hot's item). x and y are the
        menu's own device pixels.
*/
#define UI_MENU_NOTHING (-1)
#define UI_MENU_GONE (-2)
#define UI_MENU_OPEN (-3)

static int ui_popmenu_event(struct ui address_to ui, struct ui_popmenu address_to m, const struct window_event address_to event, int x, int y)
{
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN))
        {
                p32 c = (p32)event->b;

                switch (event->a)
                {
                case 103: ui_popmenu_step(m, -1); return UI_MENU_NOTHING;
                case 108: ui_popmenu_step(m, 1); return UI_MENU_NOTHING;
                case 102: m->hot = m->count - 1; ui_popmenu_step(m, 1); return UI_MENU_NOTHING;
                case 107: m->hot = 0; ui_popmenu_step(m, -1); return UI_MENU_NOTHING;
                case 1: case 105: return UI_MENU_GONE;
                case 106:
                        return ui_popmenu_choosable(m, m->hot) && (m->items[m->hot].flags & UI_MENU_SUBMENU) ? UI_MENU_OPEN : UI_MENU_NOTHING;
                case 28: case 96: case 57:
                        if (!ui_popmenu_choosable(m, m->hot))
                                return UI_MENU_NOTHING;
                        return m->items[m->hot].flags & UI_MENU_SUBMENU ? UI_MENU_OPEN : m->items[m->hot].id;
                }
                if (c > ' ' && c < 127)
                {
                        positive now = clock_monotonic_nanoseconds();
                        p8 shown[96];
                        p32 mnemonic;

                        if (now - m->typed_at > 1000000000ull || m->typed_length >= (int)sizeof(m->typed) - 1)
                                m->typed_length = 0;
                        m->typed[m->typed_length++] = (char)byte_to_lower((b8)c);
                        m->typed_at = now;
                        // A mnemonic chooses at once; else the first item the letters typed start.
                        for (int at = 0; at < m->count; at++)
                        {
                                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                                if (ui_popmenu_choosable(m, at) && mnemonic == byte_to_lower((b8)c) && m->typed_length == 1)
                                {
                                        m->hot = at;
                                        return m->items[at].flags & UI_MENU_SUBMENU ? UI_MENU_OPEN : m->items[at].id;
                                }
                        }
                        for (int at = 0; at < m->count; at++)
                        {
                                positive k = 0;

                                ui_popmenu_shown(m->items[at].label, shown, sizeof(shown), &mnemonic);
                                while (k < (positive)m->typed_length && shown[k] && byte_to_lower((b8)shown[k]) == m->typed[k])
                                        k++;
                                if (ui_popmenu_choosable(m, at) && k == (positive)m->typed_length)
                                {
                                        m->hot = at;
                                        break;
                                }
                        }
                }
                return UI_MENU_NOTHING;
        }
        if (event->type == WINDOW_EVENT_MOTION)
        {
                int at = x >= 0 && x < m->width ? ui_popmenu_at(ui, m, y) : -1;

                m->hot = at;
                return UI_MENU_NOTHING;
        }
        if (event->type == WINDOW_EVENT_WHEEL && !event->b)
        {
                m->top += event->a < 0 ? 1 : -1;
                m->top = m->top < 0 ? 0 : m->top > m->count - m->rows_shown ? (m->count - m->rows_shown > 0 ? m->count - m->rows_shown : 0) : m->top;
                return UI_MENU_NOTHING;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a >= 0x110 && event->a <= 0x112)
        {
                int at = x >= 0 && x < m->width ? ui_popmenu_at(ui, m, y) : -1;

                m->touch |= (event->flags & WINDOW_KEY_TOUCH) != 0;
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        m->pressed = true;
                        m->hot = at;
                        return UI_MENU_NOTHING;
                }
                // A release on an item chooses it: after a press on it, or at the end of a drag from the button that
                // opened the menu (press, drag, release).
                if (event->flags & WINDOW_KEY_CANCEL)
                        return UI_MENU_NOTHING;
                if (ui_popmenu_choosable(m, at))
                        return m->items[at].flags & UI_MENU_SUBMENU ? UI_MENU_OPEN : m->items[at].id;
        }
        return UI_MENU_NOTHING;
}

/*
        A popup an app opens: its own row of the table, a picture of its own drawn with the app's engine and faces, and
        its menu. ui_popup_open takes the row (as a popup of the app's window, at an anchor in the window's pixels),
        ui_popup_pump reads its events and answers what was chosen, and ui_popup_close leaves the row. The app's runner
        waits on its wake too while it is open (ui_app.popup).
*/
struct ui_popup
{
        struct window_row own;
        struct ui ui;
        struct ui_popmenu menu;
        b32 open;
        int shown, x, y;
};

static fn ui_popup_present(struct ui_popup address_to p)
{
        struct windows address_to table = p->own.table;
        unsigned int row = p->own.row;
        int slot = p->shown < 0 ? 0 : 1 - p->shown;

        if (p->shown >= 0 && !windows_ready(&p->own))
                return;
        ui_begin(&p->ui, windows_pixels(&p->own, (unsigned int)slot), p->own.stride, p->menu.width, p->menu.height);
        ui_popmenu_draw(&p->ui, &p->menu);
        table->width[row] = (unsigned int)p->menu.width;
        table->height[row] = (unsigned int)p->menu.height;
        table->damage_width[row] = 0;
        windows_show(&p->own, (unsigned int)slot);
        p->shown = slot;
}

static b32 ui_popup_open(struct ui_popup address_to p, const struct window_row address_to parent, const struct ui address_to parent_ui,
                         int x, int y, int width, int height, unsigned int flags, b32 touch)
{
        struct windows address_to table;
        int screen_x, screen_y, screen_w, screen_h;

        if (p->open)
                windows_leave(&p->own);
        p->open = false;
        if (windows_take(&p->own) < 0)
                return false;
        table = p->own.table;
        p->ui = *parent_ui;
        p->shown = -1;
        p->menu.touch = touch;
        p->menu.hot = -1;
        p->menu.top = 0;
        windows_output(table, 0, &screen_x, &screen_y, &screen_w, &screen_h);
        ui_popmenu_size(&p->ui, &p->menu, screen_h);
        table->popup_parent[p->own.row] = parent->row + 1;
        table->popup_x[p->own.row] = x, table->popup_y[p->own.row] = y;
        table->popup_w[p->own.row] = width, table->popup_h[p->own.row] = height;
        table->popup_flags[p->own.row] = flags;
        table->glass[p->own.row] = WINDOW_NO_CLOSE;
        string_copy_bounded((string_address)table->title[p->own.row], (string_address) "menu", WINDOWS_TITLE);
        p->open = true;
        ui_popup_present(p);
        return true;
}

static fn ui_popup_close(struct ui_popup address_to p)
{
        if (p->open)
                windows_leave(&p->own);
        p->open = false;
}

// The popup's events: the id chosen (the popup closed), UI_MENU_GONE when it went, UI_MENU_NOTHING otherwise.
static int ui_popup_pump(struct ui_popup address_to p)
{
        struct window_event event;
        int answer = UI_MENU_NOTHING;

        if (!p->open)
                return UI_MENU_NOTHING;
        while (windows_event(&p->own, &event) && answer == UI_MENU_NOTHING)
        {
                int hot = p->menu.hot;

                if (event.type == WINDOW_EVENT_MOTION)
                        p->x = event.a, p->y = event.b;
                answer = ui_popmenu_event(&p->ui, &p->menu, &event, p->x, p->y);
                if (answer == UI_MENU_OPEN)
                        answer = UI_MENU_NOTHING;
                if (p->menu.hot != hot)
                        ui_popup_present(p);
        }
        if (p->own.table->state[p->own.row] & WINDOW_CLOSING)
                answer = UI_MENU_GONE;
        if (answer != UI_MENU_NOTHING)
                ui_popup_close(p);
        return answer;
}

/* ---- the kit's composites: what a window is made of ---- */

/*
        A window of the kit is a page: its title at the top left, a search
        field at the title's right when it has one, a column of groups at
        the left when it has one (the kit's tabs stood on end), the body (a
        list of rows in sections, or a page of text), and the status line at
        its foot; and over all of it, when a row opened one, a menu of a
        choice's words. An app says what it shows -- the rows, each one's
        label and value and what kind of value it is, the groups, the words
        of a choice, the status -- and the kit does the rest: every size, the
        type, the theme's colours, the hover and the press and its ripple
        edge to edge, the row the keys are on and their ring, a finger's
        reach, a scrollbar on whatever scrolls, the wheel, a finger's drag
        and throw, the keys that move in a list, a column or a menu, and a
        value placed at its first frame and moved only when it changes.

        A list's row is one of:

            plain       its value quiet at the right
            choice      its value in the ink and a chevron: it opens a menu
            step        its value between Left and Right marks
            switch      the kit's switch
            tabs        the kit's segmented tabs, one for each of its words
            swatches    a colour for each of its words, the chosen ringed
            field       the kit's field, typed in (as dots when masked)

        and any row may have a quiet note before its value, the bars of a
        signal at its edge and a mark before its label (the one in use).
        Sizes are logical pixels, times the window's scale.
*/
#define UI_PAGE_TOP 60       // a page's title band
#define UI_PAGE_FOOT 40      // its status line's band
#define UI_PAGE_INSET 6      // the title and the status line in from the margin
#define UI_SEARCH_W 190      // a page's search field, at the title's right
#define UI_SEARCH_H 28
#define UI_SEARCH_Y 24
#define UI_COLUMN_SHARE 30   // a page's column: this share of the window,
#define UI_COLUMN_LEAST 88   // no narrower than this, no wider than the most, and never more than half of the window
#define UI_COLUMN_MOST 170
#define UI_COLUMN_GUTTER 14  // between the column and the body
#define UI_COLUMN_PAGE 3     // Page Up and Page Down in a column move three groups
#define UI_SECTION 34        // a section's header over its first row
#define UI_SECTION_GAP 10    // under its last row
#define UI_ROW_FIELD_W 220   // a field in a row: at most this wide, and at least the least
#define UI_ROW_FIELD_LEAST 100
#define UI_MENU_LEAST 140    // the narrowest a choice's menu is (its rows and pad are the menus', UI_MENU_ROW and UI_MENU_PAD)
#define UI_LINE 20           // a line of a page of text: body-medium's line
#define UI_LINES_PAD 10

// The keys the kit moves by (the evdev codes the compositor sends in a key's a).
#define UI_KEY_ESCAPE 1
#define UI_KEY_ENTER 28
#define UI_KEY_SPACE 57
#define UI_KEY_HOME 102
#define UI_KEY_UP 103
#define UI_KEY_PAGE_UP 104
#define UI_KEY_END 107
#define UI_KEY_DOWN 108
#define UI_KEY_PAGE_DOWN 109
#define UI_KEY_ENTER_PAD 96

/*
        A text cut to room at a role: the longest start, ending before a whole
        character, that fits with three dots after it, or the text itself when
        it fits. A cut is kept with what it was made for, so a text drawn again
        is not measured again; the memo is mapped at the first cut.
*/
#define UI_CUTS 128
#define UI_CUT_TEXT 128

struct ui_cut
{
        char text[UI_CUT_TEXT];
        positive length, cut, capacity;
        int type, room, scale;
        unsigned char kind; // 1 whole, 2 the first cut bytes and the dots, 3 the first cut bytes alone
};

static struct ui_cut address_to ui_cuts;

static unsigned char ui_cut_find(struct ui address_to ui, string_address text, positive length, int type, int room, char address_to fitted,
                                 positive capacity, positive address_to cut)
{
        positive n = length < capacity ? length : capacity - 1, low = 1, high, best = 0;

        if (ui_kit_measure(ui, text, type) <= room)
                return *cut = length, 1;
        if (n <= 1)
                return *cut = n, 3;
        high = n - 1;
        while (low <= high)
        {
                positive mid = (low + high) / 2, at = mid;

                while (at > 1 && (((unsigned char)text[at]) & 0xc0) == 0x80)
                        at--;
                memory_copy(fitted, text, at);
                fitted[at] = 0;
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
                if (ui_kit_measure(ui, (string_address)fitted, type) <= room)
                        best = at, low = mid + 1;
                else
                        high = mid - 1;
        }
        if (best)
                return *cut = best, 2;
        return *cut = 1, 3;
}

static string_address ui_fit(struct ui address_to ui, string_address text, int type, int room, char address_to fitted, positive capacity)
{
        positive length = string_length(text), cut = 0;
        unsigned char kind = 0;
        struct ui_cut address_to memo = null;

        if (!ui_cuts)
        {
                long mapped = ui_map(sizeof(struct ui_cut) * UI_CUTS);

                ui_cuts = mapped < 0 && mapped > -4096 ? null : (struct ui_cut address_to)mapped;
        }
        if (ui_cuts && length < UI_CUT_TEXT)
        {
                unsigned int hash = 2166136261u;

                for (positive at = 0; at < length; at++)
                        hash = (hash ^ (unsigned char)text[at]) * 16777619u;
                hash = (((hash ^ (unsigned int)type) * 16777619u ^ (unsigned int)room) * 16777619u ^ (unsigned int)ui->scale) * 16777619u;
                memo = &ui_cuts[(hash ^ hash >> 15) & (UI_CUTS - 1)];
                if (memo->kind && memo->length == length && memo->type == type && memo->room == room && memo->scale == ui->scale &&
                    memo->capacity == capacity && !memory_compare(memo->text, text, length))
                        kind = memo->kind, cut = memo->cut;
        }
        if (!kind)
        {
                kind = ui_cut_find(ui, text, length, type, room, fitted, capacity, &cut);
                if (memo)
                {
                        memory_copy(memo->text, text, length);
                        memo->length = length, memo->type = type, memo->room = room, memo->scale = ui->scale;
                        memo->capacity = capacity, memo->cut = cut, memo->kind = kind;
                }
        }
        if (kind == 1)
                return text;
        memory_copy(fitted, text, cut);
        fitted[cut] = 0;
        if (kind == 2)
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
        return (string_address)fitted;
}

// A control's items, the words of words a blank apart, kept in its text: as many as fit, UI_ITEMS at most.
static fn ui_items_words(struct ui_control address_to c, const char address_to words)
{
        char address_to into = c->text;
        positive left = sizeof(c->text);

        c->count = 0;
        while (words && *words && c->count < UI_ITEMS)
        {
                positive length = 0;

                while (*words == ' ')
                        words++;
                while (words[length] && words[length] != ' ')
                        length++;
                if (!length || length + 1 > left)
                        break;
                memory_copy(into, words, length);
                into[length] = 0;
                c->items[c->count++] = into;
                into += length + 1, left -= length + 1;
                words += length;
        }
}

/*
        The bars of a signal, its right edge at right and their middle on middle: four, as many in the ink as bars says
        and the rest in the outline.
*/
static fn ui_signal(struct ui address_to ui, int right, int middle, int bars)
{
        int width = ui_dev(ui, 3), gap = ui_dev(ui, 2), bottom = middle + ui_dev(ui, 7);

        for (int bar = 0; bar < 4; bar++)
        {
                int height = ui_dev(ui, 4 + 3 * bar), x = right - 4 * width - 3 * gap + bar * (width + gap);
                struct ui_box box = {x * 256, (bottom - height) * 256, (x + width) * 256, bottom * 256, ui_q8(ui, 1), ui_q8(ui, 1)};

                ui_kit_fill(ui, &box, bar < bars ? ui_role(UI_ON_SURFACE) : ui_role(UI_OUTLINE_VARIANT), null);
        }
}

/*
        A chevron pointing down, centred on cx, cy, size to a side: two strokes of the scale's width, each pixel's cover
        exact, in the colour.
*/
static fn ui_chevron(struct ui address_to ui, int cx, int cy, int size, unsigned int argb)
{
        b32 thick = ui_q8(ui, 1) + 128;

        for (int step = 0; step <= size; step++)
        {
                b32 y = (cy - size / 2 + step) * 256;

                ui_kit_fill(ui, &(struct ui_box){(cx - size + step) * 256, y, (cx - size + step) * 256 + thick, y + ui_q8(ui, 1), 0, 0}, argb, null);
                ui_kit_fill(ui, &(struct ui_box){(cx + size - step + 1) * 256 - thick, y, (cx + size - step + 1) * 256, y + ui_q8(ui, 1), 0, 0}, argb, null);
        }
}

/* ---- a list of rows in sections ---- */

#define UI_LIST_MOST 256 // the rows a list shows at once
#define UI_LIST_STARTS 1 // a row is its section's first
#define UI_LIST_HEADED 2 // and its section has a header over it

#define UI_ROW_PLAIN 0
#define UI_ROW_CHOICE 1
#define UI_ROW_STEP 2
#define UI_ROW_SWITCH 3
#define UI_ROW_TABS 4
#define UI_ROW_SWATCHES 5
#define UI_ROW_FIELD 6

// What a row shows, said by the app whenever the kit asks (ui_list.describe); what is not said is none.
struct ui_row
{
        const char address_to label;       // at the left
        const char address_to value;       // at the right
        const char address_to note;        // a quiet word before the value: what the row is, where it is from
        const char address_to words;       // a tabs row's choices, a swatches row's colours (rrggbb): words a blank apart
        const char address_to header;      // the header of its section, over the section's first row
        const char address_to typed;       // a field's text, and what it says while empty
        const char address_to placeholder;
        int section;                       // the section it is in: a header where it changes
        int kind;                          // UI_ROW_*
        int at;                            // a tabs or a swatches row's choice now, -1 none
        int bars;                          // the bars of a signal at its edge, 1 to 4 (0 none)
        b32 on;                            // a switch's
        b32 dim;                           // the label and the value quiet: a note, a value that is not there
        b32 mark;                          // a dot before the label: the one in use
        b32 masked;                        // a field shown as dots
        b32 head;                          // a field right after the label, to the row's end (the words after a verb)
};

struct ui_list
{
        // The app's: the row (its own number) each place shows, in order, how it is described, and where a change must
        // be drawn again (rows of the window, device pixels).
        unsigned short id[UI_LIST_MOST];
        int count;
        int ids;                           // how many rows the app numbers: each has its controls while the window is open
        void (*describe)(int id, struct ui_row address_to row);
        fn (*damaged)(int top, int bottom);
        const char address_to empty;       // what it says when it shows nothing
        b32 lit;                           // the row the keys are on is shown (the keys are in it, or a search lists it)
        b32 keys;                          // and ringed: the keys are in it and the window has them
        int focus;                         // the row the keys are on, -1 none
        // The kit's.
        int top[UI_LIST_MOST];             // each place's top, from the list's
        unsigned char starts[UI_LIST_MOST]; // each place's first of a section, and with a header (UI_LIST_STARTS, _HEADED)
        int content;
        int x, y, w, h;                    // where it is in the window, device pixels
        struct ui_scroll scroll;
        int hover, down;                   // the row under the pointer and the row pressed, -1 none
        int part, way;                     // a press's: the item of a tabs or swatches row it was on (-1 none), a step's way
        int pointer_x, pointer_y;
        struct ui_control address_to controls; // two to a row: the row's own (or its switch) and its trailing control
};

static fn ui_list_describe(const struct ui_list address_to l, int id, struct ui_row address_to row)
{
        memory_zero(row, sizeof(*row));
        row->at = -1;
        l->describe(id, row);
}

// The rows' controls, mapped at the first draw: what is never drawn is never touched.
static struct ui_control address_to ui_list_control(struct ui_list address_to l, int id, int which)
{
        if (!l->controls && l->ids > 0)
        {
                long mapped = ui_map((positive)l->ids * 2 * sizeof(struct ui_control));

                l->controls = mapped < 0 && mapped > -4096 ? null : (struct ui_control address_to)mapped;
        }
        return l->controls && id >= 0 && id < l->ids ? &l->controls[2 * id + which] : null;
}

// Where each row is: a header over the first of a section that has one, a gap under the last of each.
static fn ui_list_layout(const struct ui address_to ui, struct ui_list address_to l)
{
        int y = 0, section = 0;

        for (int at = 0; at < l->count; at++)
        {
                struct ui_row row;

                ui_list_describe(l, l->id[at], &row);
                l->starts[at] = !at || row.section != section ? UI_LIST_STARTS | (row.header ? UI_LIST_HEADED : 0) : 0;
                if (at && l->starts[at])
                        y += ui_dev(ui, UI_SECTION_GAP);
                if (l->starts[at] & UI_LIST_HEADED)
                        y += ui_dev(ui, UI_SECTION);
                section = row.section;
                l->top[at] = y;
                y += ui_dev(ui, UI_ROW);
        }
        y += l->count ? ui_dev(ui, UI_SECTION_GAP) : 0;
        l->content = y;
        l->scroll.content = y;
        ui_scroll_set(&l->scroll, l->scroll.scroll);
}

// The list at x, y, w by h (device pixels); its scrollbar in the margin at its right.
static fn ui_list_place(const struct ui address_to ui, struct ui_list address_to l, int x, int y, int w, int h)
{
        l->x = x, l->y = y, l->w = w, l->h = h;
        l->scroll.x = x + w + (ui_dev(ui, UI_PAD) - ui_dev(ui, UI_SCROLL_WIDTH)) / 2;
        l->scroll.y = y, l->scroll.height = h, l->scroll.view = h;
        l->scroll.content = l->content;
        ui_scroll_set(&l->scroll, l->scroll.scroll);
}

// The place a row is at in the list, or -1.
static int ui_list_at(const struct ui_list address_to l, int id)
{
        for (int at = 0; at < l->count; at++)
                if (l->id[at] == id)
                        return at;
        return -1;
}

// A row's top in the window (device pixels).
static int ui_list_row_y(const struct ui_list address_to l, int at)
{
        return l->y + l->top[at] - l->scroll.scroll;
}

static fn ui_list_damage(const struct ui address_to ui, const struct ui_list address_to l, int id)
{
        int at = id >= 0 ? ui_list_at(l, id) : -1;

        if (at >= 0 && l->damaged)
                l->damaged(ui_list_row_y(l, at), ui_list_row_y(l, at) + ui_dev(ui, UI_ROW));
}

static fn ui_list_damage_all(const struct ui_list address_to l)
{
        if (l->damaged)
                l->damaged(l->y, l->y + l->h);
}

// The row under a point of the window, or -1.
static int ui_list_hit(const struct ui address_to ui, const struct ui_list address_to l, int x, int y)
{
        int inside = y - l->y + l->scroll.scroll;

        if (x < l->x || x >= l->x + l->w || y < l->y || y >= l->y + l->h)
                return -1;
        for (int at = 0; at < l->count; at++)
                if (inside >= l->top[at] && inside < l->top[at] + ui_dev(ui, UI_ROW))
                        return l->id[at];
        return -1;
}

// The row the keys are on kept in sight, the header with it when it is a section's first: true when the list moved.
static b32 ui_list_reveal(const struct ui address_to ui, struct ui_list address_to l)
{
        int at = ui_list_at(l, l->focus), top, height = ui_dev(ui, UI_ROW);

        if (at < 0)
                return ui_scroll_set(&l->scroll, l->scroll.scroll);
        top = l->top[at];
        if (l->starts[at] & UI_LIST_HEADED)
                top -= ui_dev(ui, UI_SECTION), height += ui_dev(ui, UI_SECTION);
        if (!ui_scroll_reveal(&l->scroll, top, height))
                return false;
        ui_list_damage_all(l);
        return true;
}

// The keys moved by whole rows; answers whether they went to another row.
static b32 ui_list_move(const struct ui address_to ui, struct ui_list address_to l, int by)
{
        int at = ui_list_at(l, l->focus), last = l->count - 1, before = l->focus;

        if (last < 0)
                return false;
        at = (at < 0 ? 0 : at) + by;
        at = at < 0 ? 0 : at > last ? last : at;
        l->focus = l->id[at];
        ui_list_damage(ui, l, before);
        ui_list_damage(ui, l, l->focus);
        ui_list_reveal(ui, l);
        return l->focus != before;
}

// Up, Down, Home, End, Page Up and Page Down in a list (a page is a view of rows less one): true when the key was one.
static b32 ui_list_key(const struct ui address_to ui, struct ui_list address_to l, int key, b32 address_to moved)
{
        int page = l->h / ui_dev(ui, UI_ROW) - 1, by;

        page = page < 1 ? 1 : page;
        switch (key)
        {
        case UI_KEY_UP: by = -1; break;
        case UI_KEY_DOWN: by = 1; break;
        case UI_KEY_HOME: by = -l->count; break;
        case UI_KEY_END: by = l->count; break;
        case UI_KEY_PAGE_UP: by = -page; break;
        case UI_KEY_PAGE_DOWN: by = page; break;
        default: return false;
        }
        *moved = ui_list_move(ui, l, by);
        return true;
}

// A row's trailing control where it has one, made and put in its place at the right edge, in logical pixels.
static struct ui_control address_to ui_list_trailing(struct ui address_to ui, struct ui_list address_to l, int id, const struct ui_row address_to row,
                                                     int right, int y, int room)
{
        struct ui_control address_to c = ui_list_control(l, id, 1);
        unsigned char kind = row->kind == UI_ROW_TABS ? UI_TABS : row->kind == UI_ROW_SWATCHES ? UI_SWATCHES : UI_INPUT;
        int height = ui_dev(ui, UI_ROW);

        if (!c)
                return null;
        if (c->kind != kind)
        {
                memory_zero(c, sizeof(*c));
                c->kind = kind;
                c->variant = kind == UI_TABS ? UI_TABS_SEGMENTED : kind == UI_INPUT ? UI_FIELD_BARE : 0;
                ui_control_start(c);
        }
        if (kind == UI_INPUT)
        {
                c->source = row->typed ? row->typed : "", c->masked = row->masked, c->focused = 1;
                c->items[0] = row->placeholder;
                c->y = (y + ui_dev(ui, 6)) / ui->scale, c->h = (height - ui_dev(ui, 12)) / ui->scale;
                c->w = (room > ui_dev(ui, UI_ROW_FIELD_W) && !row->head ? ui_dev(ui, UI_ROW_FIELD_W) : room < ui_dev(ui, UI_ROW_FIELD_LEAST) ? ui_dev(ui, UI_ROW_FIELD_LEAST) : room) / ui->scale;
                c->x = right / ui->scale - c->w;
                return c;
        }
        ui_items_words(c, row->words);
        c->y = (y + height / 2) / ui->scale - 16, c->h = 32;
        if (kind == UI_TABS)
        {
                // The chosen segment: placed at the first frame, the pip moving to another after.
                if (row->at >= 0 && row->at != c->at)
                {
                        if (c->pip_x.placed)
                                ui_tabs_choose(ui, c, row->at);
                        else
                                c->at = row->at;
                }
                c->x = 0;
                c->w = c->count ? ui_tab_left(ui, c, c->count - 1) + ui_tab_width(ui, c, c->count - 1) : 0;
        }
        else
        {
                c->at = row->at;
                c->w = c->count * (UI_SWATCH + UI_SWATCH_GAP) - UI_SWATCH_GAP;
        }
        c->x = right / ui->scale - c->w;
        c->item = ui_control_hit(ui, c, l->pointer_x, l->pointer_y);
        c->hover = c->item >= 0 && l->hover == id;
        return c;
}

/*
        A row at its place, y its top: the row the keys are on in the secondary container (ringed in the accent while
        the keys are in the list), the row's own control (its hover layer and its ripple, edge to edge, or its switch),
        the label at the left cut to leave its value a third of the row, and the value as its kind says at the right.
*/
static fn ui_list_row(struct ui address_to ui, struct ui_list address_to l, int id, int y)
{
        struct ui_row row;
        int x = l->x, w = l->w, height = ui_dev(ui, UI_ROW), inset = ui_dev(ui, UI_INSET), gap = ui_dev(ui, 10);
        int right = x + w - inset, text_left = x + inset, baseline, wide = 0, label_room;
        unsigned int ink = ui_role(UI_ON_SURFACE), quiet = ui_role(UI_ON_SURFACE_VARIANT);
        struct ui_control address_to own = ui_list_control(l, id, 0), address_to trailing = null;
        char cut[UI_CUT_TEXT];

        ui_list_describe(l, id, &row);
        baseline = ui_kit_baseline(ui, y + (height - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM);
        if (row.dim)
                ink = quiet;
        if (l->lit && id == l->focus)
        {
                struct ui_box box = {(x + ui_dev(ui, 3)) * 256, (y + ui_dev(ui, 2)) * 256, (x + w - ui_dev(ui, 3)) * 256,
                                     (y + height - ui_dev(ui, 2)) * 256, ui_q8(ui, 6), ui_q8(ui, 6)};

                ui_kit_fill(ui, &box, ui_role(UI_SECONDARY_CONTAINER), null);
                if (l->keys)
                        ui_kit_ring(ui, &box, ui_q8(ui, 2), ui_role(UI_PRIMARY));
        }
        // The row's own control: a switch is its row's (the kit's switch is a row), any other row the list's row.
        if (own)
        {
                unsigned char kind = row.kind == UI_ROW_SWITCH ? UI_SWITCH : UI_LIST_ROW;

                if (own->kind != kind)
                {
                        memory_zero(own, sizeof(*own));
                        own->kind = kind;
                        own->label = "";
                        ui_control_start(own);
                }
                own->row_x = x / ui->scale, own->row_y = y / ui->scale, own->row_w = w / ui->scale, own->row_h = height / ui->scale;
                own->x = own->row_x, own->y = own->row_y, own->w = own->row_w, own->h = own->row_h;
                own->hover = l->hover == id, own->down = l->down == id;
                own->disabled = row.kind == UI_ROW_FIELD;
                if (kind == UI_SWITCH)
                {
                        own->on = row.on;
                        own->x = right / ui->scale - 52, own->w = 52;
                        own->y = (y + height / 2) / ui->scale - 24, own->h = 48;
                }
                ui_control_draw(ui, own);
        }
        if (row.mark)
        {
                int dot = ui_dev(ui, 7);
                struct ui_box box = {text_left * 256, (y + (height - dot) / 2) * 256, (text_left + dot) * 256, (y + (height + dot) / 2) * 256, dot * 128, dot * 128};

                ui_kit_fill(ui, &box, ui_role(UI_PRIMARY), null);
                text_left += ui_dev(ui, 17);
        }
        // The trailing pieces from the right: the signal, then the control or the value, then the note.
        if (row.bars > 0)
        {
                ui_signal(ui, right, y + height / 2, row.bars);
                right -= ui_dev(ui, 18) + gap;
        }
        if (row.kind == UI_ROW_TABS || row.kind == UI_ROW_SWATCHES)
                trailing = ui_list_trailing(ui, l, id, &row, right, y, 0);
        label_room = right - text_left - (row.kind == UI_ROW_SWITCH ? ui_dev(ui, 52) + gap : trailing ? ui_dev(ui, trailing->w) + gap
                                         : row.kind == UI_ROW_FIELD && row.head ? (w - 2 * inset) * 2 / 3 : (w - 2 * inset) / 3);
        if (row.label)
                wide = ui_kit_text(ui, text_left, baseline, ui_fit(ui, (string_address)row.label, UI_BODY_MEDIUM,
                                                                  label_room < ui_dev(ui, 40) ? ui_dev(ui, 40) : label_room, cut, sizeof(cut)),
                                   UI_BODY_MEDIUM, ink, 0);
        if (trailing)
        {
                ui_control_draw(ui, trailing);
                return;
        }
        if (row.kind == UI_ROW_FIELD)
        {
                int room = right - (text_left + wide + gap);

                trailing = ui_list_trailing(ui, l, id, &row, right, y, room);
                if (trailing)
                        ui_control_draw(ui, trailing);
                return;
        }
        if (row.kind == UI_ROW_SWITCH)
                return;
        if (row.value)
        {
                int room = right - (text_left + wide + ui_dev(ui, 16)), marks = 0, width;
                string_address shown;

                if (row.kind == UI_ROW_CHOICE)
                {
                        int size = ui_dev(ui, 4);

                        ui_chevron(ui, right - size, baseline - ui_dev(ui, 5), size, quiet);
                        right -= 2 * size + ui_dev(ui, 8);
                }
                else if (row.kind == UI_ROW_STEP)
                {
                        marks = ui_kit_measure(ui, (string_address) "\xe2\x80\xba", UI_LABEL_LARGE);
                        ui_kit_text(ui, right - marks, baseline, (string_address) "\xe2\x80\xba", UI_LABEL_LARGE, quiet, 0);
                        right -= marks + ui_dev(ui, 8);
                        room -= 2 * (marks + ui_dev(ui, 8));
                }
                room = row.kind == UI_ROW_CHOICE ? room - ui_dev(ui, 16) : room;
                shown = ui_fit(ui, (string_address)row.value, UI_BODY_MEDIUM, room < ui_dev(ui, 24) ? ui_dev(ui, 24) : room, cut, sizeof(cut));
                width = ui_kit_measure(ui, shown, UI_BODY_MEDIUM);
                ui_kit_text(ui, right - width, baseline, shown, UI_BODY_MEDIUM, row.kind != UI_ROW_PLAIN && !row.dim ? ui_role(UI_ON_SURFACE) : quiet, 0);
                if (row.kind == UI_ROW_STEP)
                        ui_kit_text(ui, right - width - ui_dev(ui, 8) - marks, baseline, (string_address) "\xe2\x80\xb9", UI_LABEL_LARGE, quiet, 0);
                right -= width + (marks ? marks + ui_dev(ui, 8) : 0) + gap;
        }
        if (row.note)
        {
                int width = ui_kit_measure(ui, (string_address)row.note, UI_LABEL_MEDIUM);

                if (right - width > text_left + wide + gap)
                        ui_kit_text(ui, right - width, ui_kit_baseline(ui, y + (height - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM), (string_address)row.note,
                                    UI_LABEL_MEDIUM, row.mark ? ui_role(UI_ON_SURFACE) : quiet, 0);
        }
}

// The list drawn: the headers of the sections in view and their rows, cut to the list's band, and its scrollbar.
static fn ui_list_draw(struct ui address_to ui, struct ui_list address_to l)
{
        int row_height = ui_dev(ui, UI_ROW);

        ui->top = l->y;
        ui->bottom = l->y + l->h;
        if (!l->count && l->empty)
                ui_kit_text(ui, l->x + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, l->y + (row_height - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM),
                            (string_address)l->empty, UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0);
        for (int at = 0; at < l->count; at++)
        {
                int y = ui_list_row_y(l, at);

                if ((l->starts[at] & UI_LIST_HEADED) && y > l->y && y - ui_dev(ui, UI_SECTION) < l->y + l->h)
                {
                        struct ui_row row;

                        ui_list_describe(l, l->id[at], &row);
                        ui_kit_text(ui, l->x + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, y - ui_dev(ui, UI_SECTION) + ui_dev(ui, 8), UI_TITLE_SMALL),
                                    (string_address)row.header, UI_TITLE_SMALL, ui_role(UI_ON_SURFACE_VARIANT), 0);
                }
                if (y + row_height > l->y && y < l->y + l->h)
                        ui_list_row(ui, l, l->id[at], y);
        }
        ui_scroll_draw(ui, &l->scroll);
}

/*
        The pointer on a list. Motion lights the row under it; a press is on a row (the keys go to it, it shows pressed
        and ripples from the press, edge to edge), and the release lets it go. A press answers its row, with part the
        item of a tabs or swatches row it was on (-1 none) and way -1 when it was on the left half of a step's value;
        everything else answers -1. A finger's press shows pressed the same way (touch), and is done when the app says.
*/
static fn ui_list_let_go(const struct ui address_to ui, struct ui_list address_to l)
{
        struct ui_control address_to own = ui_list_control(l, l->down, 0), address_to trailing = ui_list_control(l, l->down, 1);

        if (l->down < 0)
                return;
        if (own)
                ui_kit_ripples_exit(own);
        if (trailing)
                ui_kit_ripples_exit(trailing);
        ui_list_damage(ui, l, l->down);
        l->down = -1;
}

static int ui_list_pointer(struct ui address_to ui, struct ui_list address_to l, int type, int x, int y, b32 down)
{
        int id = ui_list_hit(ui, l, x, y);
        struct ui_control address_to own, address_to trailing;

        l->pointer_x = x, l->pointer_y = y;
        if (type == WINDOW_EVENT_MOTION)
        {
                if (id != l->hover)
                {
                        ui_list_damage(ui, l, l->hover);
                        ui_list_damage(ui, l, id);
                        l->hover = id;
                }
                else if (id >= 0 && (trailing = ui_list_control(l, id, 1)) && (trailing->kind == UI_TABS || trailing->kind == UI_SWATCHES))
                        ui_list_damage(ui, l, id); // a segment or a swatch under it
                return -1;
        }
        if (type != WINDOW_EVENT_BUTTON)
                return -1;
        if (!down)
        {
                ui_list_let_go(ui, l);
                return -1;
        }
        ui_list_let_go(ui, l);
        if (id < 0)
                return -1;
        l->part = -1, l->way = 1;
        trailing = ui_list_control(l, id, 1);
        if (trailing && (trailing->kind == UI_TABS || trailing->kind == UI_SWATCHES))
                l->part = ui_control_hit(ui, trailing, x, y);
        {
                struct ui_row row;

                ui_list_describe(l, id, &row);
                if (row.kind == UI_ROW_STEP && x < l->x + l->w - ui_dev(ui, UI_INSET + 40))
                        l->way = -1;
        }
        own = ui_list_control(l, id, 0);
        if (own && l->part < 0)
        {
                ui_spring_stop(&own->held_squish);
                ui_list_row_press(ui, own, x, y);
        }
        ui_list_damage(ui, l, l->focus);
        l->focus = id, l->down = id, l->hover = id;
        ui_list_damage(ui, l, id);
        return id;
}

/* ---- a menu of a choice's words ---- */

#define UI_MENU_MOST 32
#define UI_MENU_WORD 64

#define UI_MENU_KEPT 0   // a key moved in it, or did nothing
#define UI_MENU_CHOSEN 1 // the choice at is the one
#define UI_MENU_CLOSED 2 // closed with nothing chosen

struct ui_menu
{
        int owner;                         // the row it opened from (the app's number), -1 when closed
        char word[UI_MENU_MOST][UI_MENU_WORD];
        int count, at, current;            // its words, the keys' one, the one the row has now (-1 none)
        b32 touch;                         // a finger opened it: its choices are a finger's height
        int widest;                        // the widest word, device pixels
        int x, y, w, h, shown;             // where it is, device pixels, and how many choices show
        int pressed;                       // the choice a finger is on, -1 none
        struct ui_scroll scroll;           // in whole choices
};

static int ui_menu_item_height(const struct ui address_to ui, const struct ui_menu address_to m)
{
        return ui_dev(ui, m->touch ? UI_MENU_TOUCH_ROW : UI_MENU_ROW);
}

static int ui_menu_top(const struct ui address_to ui, const struct ui_menu address_to m)
{
        return m->scroll.scroll / ui_menu_item_height(ui, m);
}

// A menu opened from a row, empty: the app adds its words, the one the row has now marked current.
static fn ui_menu_open(struct ui_menu address_to m, int owner, b32 touch)
{
        m->owner = owner, m->touch = touch;
        m->count = 0, m->at = 0, m->current = -1, m->widest = 0, m->pressed = -1;
        m->scroll.scroll = m->scroll.wheel = m->scroll.left = 0;
}

static fn ui_menu_add(struct ui address_to ui, struct ui_menu address_to m, const char address_to word, b32 current)
{
        int wide;

        if (m->count >= UI_MENU_MOST)
                return;
        string_copy_bounded((string_address)m->word[m->count], (string_address)word, UI_MENU_WORD);
        wide = ui_kit_measure(ui, (string_address)m->word[m->count], UI_BODY_MEDIUM);
        m->widest = wide > m->widest ? wide : m->widest;
        if (current)
                m->current = m->at = m->count;
        m->count++;
}

/*
        Where the menu is, from its row's band in the window (top to bottom), the edge its right side is at, and the
        window's size: under the row, or over it when there is more room there; with room on neither side for all of
        it, slid up over the row where it fits the window so, and else cut to the larger side, and it scrolls.
*/
static fn ui_menu_place(const struct ui address_to ui, struct ui_menu address_to m, int row_top, int row_bottom, int right, int width,
                        int height)
{
        int item = ui_menu_item_height(ui, m), pad = ui_dev(ui, UI_MENU_PAD), margin = ui_dev(ui, 8);
        int below = height - margin - (row_bottom - ui_dev(ui, 4)), above = row_top + ui_dev(ui, 4) - margin, most;

        most = (below >= above ? below : above) - 2 * pad;
        most = most / item < 1 ? 1 : most / item;
        if (m->count > most && m->count * item + 2 * pad <= height - 2 * margin)
                most = m->count;
        m->shown = m->count < most ? m->count : most;
        m->h = m->shown * item + 2 * pad;
        m->w = m->widest + ui_dev(ui, 12 + 6 + 10 + 18);
        m->w = m->w < ui_dev(ui, UI_MENU_LEAST) ? ui_dev(ui, UI_MENU_LEAST) : m->w;
        m->w = m->w > width - 2 * margin ? width - 2 * margin : m->w;
        m->x = right - m->w;
        m->x = m->x < margin ? margin : m->x;
        if (m->h <= below)
                m->y = row_bottom - ui_dev(ui, 4);
        else if (m->h <= above)
                m->y = row_top + ui_dev(ui, 4) - m->h;
        else
        {
                m->y = height - margin - m->h;
                m->y = m->y < margin ? margin : m->y;
        }
        m->scroll.unit = item;
        m->scroll.content = m->count * item;
        m->scroll.view = m->shown * item;
        m->scroll.x = m->x + m->w - ui_dev(ui, UI_SCROLL_WIDTH);
        m->scroll.y = m->y + pad, m->scroll.height = m->h - 2 * pad;
        ui_scroll_set(&m->scroll, m->scroll.scroll);
        // The keys' choice stays in view.
        if (m->at < ui_menu_top(ui, m))
                m->at = ui_menu_top(ui, m);
        else if (m->at >= ui_menu_top(ui, m) + m->shown)
                m->at = ui_menu_top(ui, m) + m->shown - 1;
}

// The keys on a choice, kept in view.
static fn ui_menu_go(const struct ui address_to ui, struct ui_menu address_to m, int at)
{
        int item = ui_menu_item_height(ui, m);

        m->at = at < 0 ? 0 : at >= m->count ? m->count - 1 : at;
        ui_scroll_reveal(&m->scroll, m->at * item, item);
}

// The choice under a point of the window, or -1.
static int ui_menu_item(const struct ui address_to ui, const struct ui_menu address_to m, int x, int y)
{
        int pad = ui_dev(ui, UI_MENU_PAD), at;

        if (m->owner < 0 || x < m->x || x >= m->x + m->w || y < m->y + pad || y >= m->y + m->h - pad)
                return -1;
        at = ui_menu_top(ui, m) + (y - m->y - pad) / ui_menu_item_height(ui, m);
        return at < m->count ? at : -1;
}

/*
        A key with the menu open (it has every key): Up, Down, Home, End, Page Up and Page Down move in it, a letter or a
        digit goes to the next word that starts with it, Enter or Space chooses, Escape closes.
*/
static int ui_menu_key(const struct ui address_to ui, struct ui_menu address_to m, int key, unsigned int c, unsigned int flags)
{
        switch (key)
        {
        case UI_KEY_ESCAPE: return UI_MENU_CLOSED;
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
        case UI_KEY_SPACE: return UI_MENU_CHOSEN;
        case UI_KEY_UP: ui_menu_go(ui, m, m->at - 1); return UI_MENU_KEPT;
        case UI_KEY_DOWN: ui_menu_go(ui, m, m->at + 1); return UI_MENU_KEPT;
        case UI_KEY_HOME: ui_menu_go(ui, m, 0); return UI_MENU_KEPT;
        case UI_KEY_END: ui_menu_go(ui, m, m->count - 1); return UI_MENU_KEPT;
        case UI_KEY_PAGE_UP: ui_menu_go(ui, m, m->at - (m->shown > 0 ? m->shown : 1)); return UI_MENU_KEPT;
        case UI_KEY_PAGE_DOWN: ui_menu_go(ui, m, m->at + (m->shown > 0 ? m->shown : 1)); return UI_MENU_KEPT;
        }
        if (c > ' ' && c < 0x7f && !(flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)))
        {
                c = c >= 'A' && c <= 'Z' ? c + 32 : c;
                for (int step = 1; step <= m->count; step++)
                {
                        int at = (m->at + step) % m->count;
                        unsigned char first = (unsigned char)m->word[at][0];

                        if ((first >= 'A' && first <= 'Z' ? first + 32u : first) == c)
                        {
                                ui_menu_go(ui, m, at);
                                break;
                        }
                }
        }
        return UI_MENU_KEPT;
}

/*
        The menu over everything: the high surface, lifted, 8 round with a hairline; the keys' choice in the secondary
        container, the row's own one dotted in the accent, a finger's choice pressed, and the scrollbar when not all
        of it shows.
*/
static fn ui_menu_draw(struct ui address_to ui, struct ui_menu address_to m)
{
        int item = ui_menu_item_height(ui, m), pad = ui_dev(ui, UI_MENU_PAD), top = ui_menu_top(ui, m);
        struct ui_box box = {m->x * 256, m->y * 256, (m->x + m->w) * 256, (m->y + m->h) * 256, ui_q8(ui, 8), ui_q8(ui, 8)};

        if (m->owner < 0)
                return;
        ui->top = 0;
        ui->bottom = ui->height;
        ui_kit_shadow(ui, &box, 0, ui_q8(ui, 8), ui_q8(ui, 24), -ui_q8(ui, 6), ui_mix(ui_role(UI_SHADOW), 18, 0));
        ui_kit_fill(ui, &box, ui_role(UI_SURFACE_HIGH), null);
        ui_kit_ring(ui, &box, ui_q8(ui, 1), ui_role(UI_OUTLINE_VARIANT));
        for (int k = 0; k < m->shown && top + k < m->count; k++)
        {
                int at = top + k, y = m->y + pad + k * item, x = m->x + ui_dev(ui, 4), w = m->w - ui_dev(ui, 8);
                int dot = ui_dev(ui, 6), inset = ui_dev(ui, 12);
                struct ui_box face = {x * 256, y * 256, (x + w) * 256, (y + item) * 256, ui_q8(ui, 6), ui_q8(ui, 6)};

                if (at == m->at)
                        ui_kit_fill(ui, &face, ui_role(UI_SECONDARY_CONTAINER), null);
                if (at == m->pressed)
                        ui_kit_fill(ui, &face, ui_mix(ui_role(UI_ON_SURFACE), 10, 0), null);
                if (at == m->current)
                        ui_kit_fill(ui, &(struct ui_box){(x + inset) * 256, (y + (item - dot) / 2) * 256, (x + inset + dot) * 256,
                                                         (y + (item + dot) / 2) * 256, dot * 128, dot * 128}, ui_role(UI_PRIMARY), null);
                ui_run_in(ui, (const font_face address_to const[]){ui->regular, ui->regular}, x + inset + dot + ui_dev(ui, 10),
                          ui_kit_baseline(ui, y + (item - ui_dev(ui, 20)) / 2, UI_BODY_MEDIUM), (string_address)m->word[at], 14,
                          ui_role(UI_ON_SURFACE) & 0xffffffu, 255, x, x + w - ui_dev(ui, 6), 0);
        }
        if (m->count > m->shown)
                ui_scroll_draw(ui, &m->scroll);
}

/* ---- a finger on a page ---- */

/*
        A finger: down, what it is on shows pressed and nothing is done yet; past the slop it drags the region it went
        down on, which follows it; up, the press is done where it went down if it did not move off it, and the region
        is thrown if it was moving; a press taken back does nothing. The region's moves are answered, to be drawn.
*/
struct ui_finger
{
        struct ui_press press;
        struct ui_drag drag;
        struct ui_scroll address_to region;
};

static fn ui_finger_down(struct ui_finger address_to f, int x, int y, int target, struct ui_scroll address_to region)
{
        ui_drag_stop(&f->drag);
        f->region = region;
        if (region)
                region->left = 0;
        ui_press_begin(&f->press, x, y, target, true);
}

// The finger moved: true when what it shows changed (its press became a drag, or the region moved).
static b32 ui_finger_move(const struct ui address_to ui, struct ui_finger address_to f, int x, int y)
{
        b32 changed = false;

        if (!f->press.down)
                return false;
        if (!f->press.dragged && ui_press_move(ui, &f->press, x, y))
        {
                ui_drag_start(&f->drag, f->press.y, clock_monotonic_nanoseconds());
                changed = true;
        }
        if (f->press.dragged && f->region)
        {
                int before = f->region->scroll;

                ui_scroll_by(f->region, -ui_drag_follow(&f->drag, y, clock_monotonic_nanoseconds()));
                changed |= f->region->scroll != before;
        }
        return changed;
}

// The finger lifted: what it pressed, done, or -1.
static int ui_finger_up(struct ui_finger address_to f, b32 cancelled)
{
        if (f->press.dragged)
                ui_drag_let_go(&f->drag, clock_monotonic_nanoseconds());
        return f->press.down ? ui_press_end(&f->press, cancelled) : -1;
}

// A thrown region's next move, from the app's tick: the wait for the one after, or -1 once it is still; moved says it moved.
static long ui_finger_throw(struct ui_finger address_to f, b32 address_to moved)
{
        int before;

        *moved = false;
        if (!f->drag.flying || !f->region)
                return -1;
        before = f->region->scroll;
        if (!ui_scroll_by(f->region, -ui_drag_step(&f->drag, clock_monotonic_nanoseconds())))
                ui_drag_stop(&f->drag);
        *moved = f->region->scroll != before;
        return f->drag.flying ? UI_DRAG_FRAME_NS : -1;
}

/* ---- a page ---- */

struct ui_page
{
        const char address_to title;       // at the top left
        const char address_to status;      // at the foot: a line, or two where it does not fit one
        b32 column_on;                     // it has a column of groups at the left
        int width, height;                 // the window, device pixels
        // Laid out by the kit, device pixels.
        int top, bottom;                   // the body's band
        int left, right;                   // and its sides
        int column_w;
        struct ui_control search;          // a bare field at the title's right, when the app gave it a placeholder
        struct ui_control column;          // the column: the kit's tabs on end
        struct ui_scroll column_scroll;
        char names[UI_ITEMS][UI_MENU_WORD];
        int pointer_x, pointer_y;
};

static fn ui_page_layout(const struct ui address_to ui, struct ui_page address_to p, int width, int height)
{
        int pad = ui_dev(ui, UI_PAD), side = width * UI_COLUMN_SHARE / 100;

        p->width = width, p->height = height;
        p->top = ui_dev(ui, UI_PAGE_TOP);
        p->bottom = height - ui_dev(ui, UI_PAGE_FOOT);
        side = side < ui_dev(ui, UI_COLUMN_LEAST) ? ui_dev(ui, UI_COLUMN_LEAST) : side;
        side = side > ui_dev(ui, UI_COLUMN_MOST) ? ui_dev(ui, UI_COLUMN_MOST) : side;
        p->column_w = p->column_on ? (side > width / 2 ? width / 2 : side) : 0;
        p->left = pad + (p->column_on ? p->column_w + ui_dev(ui, UI_COLUMN_GUTTER) : 0);
        p->right = width - pad;
        p->column_scroll.x = pad + p->column_w + (ui_dev(ui, UI_COLUMN_GUTTER) - ui_dev(ui, UI_SCROLL_WIDTH)) / 2;
        p->column_scroll.y = p->top, p->column_scroll.height = p->bottom - p->top, p->column_scroll.view = p->bottom - p->top;
        p->column_scroll.content = p->column.count * ui_dev(ui, UI_ROW);
        ui_scroll_set(&p->column_scroll, p->column_scroll.scroll);
}

// The group under a point of the window, or -1.
static int ui_page_column_hit(const struct ui address_to ui, const struct ui_page address_to p, int x, int y)
{
        int group = (y - p->top + p->column_scroll.scroll) / ui_dev(ui, UI_ROW);

        if (!p->column_on || y < p->top || y >= p->bottom || x < ui_dev(ui, UI_PAD) || x >= ui_dev(ui, UI_PAD) + p->column_w ||
            group >= p->column.count)
                return -1;
        return group;
}

// The column's groups: their names, cut to the column, and how many.
static fn ui_page_groups(struct ui address_to ui, struct ui_page address_to p, const char address_to (*name)(int group), int count)
{
        p->column.count = count < UI_ITEMS ? count : UI_ITEMS;
        for (int group = 0; group < p->column.count; group++)
        {
                string_address fitted = ui_fit(ui, (string_address)name(group), UI_LABEL_LARGE, p->column_w - ui_dev(ui, 24),
                                               p->names[group], sizeof(p->names[group]));

                if (fitted != (string_address)p->names[group])
                        string_copy_bounded((string_address)p->names[group], fitted, sizeof(p->names[group]));
                p->column.items[group] = p->names[group];
        }
        p->column_scroll.content = p->column.count * ui_dev(ui, UI_ROW);
}

// The group chosen kept in sight in a column that does not fit: true when it moved.
static b32 ui_page_column_reveal(const struct ui address_to ui, struct ui_page address_to p, int group)
{
        return ui_scroll_reveal(&p->column_scroll, group * ui_dev(ui, UI_ROW), ui_dev(ui, UI_ROW));
}

// A key in the column: the group it moves to (Up, Down, Home, End, three for a page), or -1 when it is not one of them.
static int ui_page_column_key(const struct ui_page address_to p, int key, int group)
{
        int last = p->column.count - 1;

        switch (key)
        {
        case UI_KEY_UP: group--; break;
        case UI_KEY_DOWN: group++; break;
        case UI_KEY_HOME: group = 0; break;
        case UI_KEY_END: group = last; break;
        case UI_KEY_PAGE_UP: group -= UI_COLUMN_PAGE; break;
        case UI_KEY_PAGE_DOWN: group += UI_COLUMN_PAGE; break;
        default: return -1;
        }
        return group < 0 ? 0 : group > last ? last : group;
}

/*
        The column drawn: the chosen group under the accent's pip, which slides to another however it is chosen, ringed
        while the keys are in it; none chosen (disabled) while a search lists the rows of every group.
*/
static fn ui_page_column(struct ui address_to ui, struct ui_page address_to p, int chosen, b32 keys, b32 disabled)
{
        struct ui_control address_to c = &p->column;

        if (!p->column_on)
                return;
        ui->top = p->top;
        ui->bottom = p->bottom;
        if (c->kind != UI_TABS)
        {
                c->kind = UI_TABS, c->variant = UI_TABS_VERTICAL;
                c->at = chosen;
        }
        c->x = UI_PAD;
        c->y = (p->top - p->column_scroll.scroll) / ui->scale;
        c->w = p->column_w / ui->scale;
        c->h = UI_ROW;
        c->disabled = disabled;
        c->focused = keys;
        c->item = ui_control_hit(ui, c, p->pointer_x, p->pointer_y);
        c->hover = c->item >= 0;
        if (chosen != c->at)
                ui_tabs_choose(ui, c, chosen); // placed at its first frame, moving after
        ui_control_draw(ui, c);
        ui_scroll_draw(ui, &p->column_scroll);
}

// A press on the column's group: its ripple, from the press.
static fn ui_page_column_press(const struct ui address_to ui, struct ui_page address_to p, int group, int x, int y)
{
        struct ui_control address_to c = &p->column;

        ui_kit_ripple_start(ui, c, x, y, ui_dev(ui, c->x), ui_dev(ui, c->y + group * c->h), ui_dev(ui, c->w), ui_dev(ui, c->h), 8);
}

/*
        The page's own parts: cleared to the surface, the title in the title role, the search field (its text what the
        app typed in it, ringed while it has the keys), and the status line at the foot, quiet, broken at a blank where
        it does not fit one line.
*/
static fn ui_page_draw(struct ui address_to ui, struct ui_page address_to p, const char address_to searched, b32 keys)
{
        int pad = ui_dev(ui, UI_PAD), left = pad + ui_dev(ui, UI_PAGE_INSET), room = p->width - left - pad, foot = ui_dev(ui, UI_PAGE_FOOT);

        ui->top = 0;
        ui->bottom = p->height;
        ui_clear(ui);
        if (p->title)
                ui_kit_text(ui, left, ui_kit_baseline(ui, ui_dev(ui, 8) + (p->top - ui_dev(ui, 8) - ui_dev(ui, 28)) / 2, UI_TITLE_LARGE),
                            (string_address)p->title, UI_TITLE_LARGE, ui_role(UI_ON_SURFACE), 0);
        if (p->search.items[0])
        {
                struct ui_control address_to c = &p->search;

                if (c->kind != UI_INPUT)
                {
                        c->kind = UI_INPUT, c->variant = UI_FIELD_BARE;
                        ui_control_start(c);
                }
                c->w = UI_SEARCH_W, c->h = UI_SEARCH_H;
                c->x = (p->width - pad) / ui->scale - c->w, c->y = UI_SEARCH_Y;
                // Its text is the text widget's when it has one (c->edit), else what the app typed in it.
                if (!c->edit)
                        c->source = searched ? searched : "";
                c->focused = keys;
                ui_control_draw(ui, c);
        }
        ui->top = p->bottom;
        if (p->status && p->status[0])
        {
                unsigned int quiet = ui_role(UI_ON_SURFACE_VARIANT);
                positive length = string_length((string_address)p->status), cut = 0;
                char first[256];

                if (ui_kit_measure(ui, (string_address)p->status, UI_LABEL_MEDIUM) <= room || length >= sizeof(first))
                {
                        ui_kit_text(ui, left, ui_kit_baseline(ui, p->bottom + (foot - ui_dev(ui, 16)) / 2, UI_LABEL_MEDIUM), (string_address)p->status,
                                    UI_LABEL_MEDIUM, quiet, 0);
                        return;
                }
                for (positive at = 1; at < length; at++)
                {
                        if (p->status[at] != ' ')
                                continue;
                        memory_copy(first, p->status, at);
                        first[at] = 0;
                        if (ui_kit_measure(ui, (string_address)first, UI_LABEL_MEDIUM) > room)
                                break;
                        cut = at;
                }
                cut = cut ? cut : length;
                memory_copy(first, p->status, cut);
                first[cut] = 0;
                ui_kit_text(ui, left, ui_kit_baseline(ui, p->bottom + foot / 2 - ui_dev(ui, 16), UI_LABEL_MEDIUM), (string_address)first,
                            UI_LABEL_MEDIUM, quiet, 0);
                ui_kit_text(ui, left, ui_kit_baseline(ui, p->bottom + foot / 2, UI_LABEL_MEDIUM), (string_address)p->status + cut + (cut < length),
                            UI_LABEL_MEDIUM, quiet, 0);
        }
}

/*
        A page of text in the body: lines in the body role, a line's height apart and padded, drawn where they are in
        view of the region; and the region's height, for what it scrolls over.
*/
static int ui_lines_height(const struct ui address_to ui, int count)
{
        return count * ui_dev(ui, UI_LINE) + 2 * ui_dev(ui, UI_LINES_PAD);
}

static fn ui_lines_draw(struct ui address_to ui, const struct ui_scroll address_to s, int left, int count,
                        const char address_to (*line)(int at, char address_to into, positive room))
{
        int height = ui_dev(ui, UI_LINE), top = s->y - s->scroll + ui_dev(ui, UI_LINES_PAD);
        char text[512];

        ui->top = s->y;
        ui->bottom = s->y + s->view;
        for (int at = 0; at < count; at++)
        {
                int y = top + at * height;

                if (y + height <= ui->top || y >= ui->bottom)
                        continue;
                ui_kit_text(ui, left + ui_dev(ui, UI_INSET), ui_kit_baseline(ui, y, UI_BODY_MEDIUM), (string_address)line(at, text, sizeof(text)),
                            UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0);
        }
        ui_scroll_draw(ui, s);
}

// A key on a page of text: Up and Down a line, Page Up and Down a view, Home and End; true when it was one, moved when it moved.
static b32 ui_lines_key(const struct ui address_to ui, struct ui_scroll address_to s, int key, b32 address_to moved)
{
        int line = ui_dev(ui, UI_LINE), to;

        switch (key)
        {
        case UI_KEY_UP: to = s->scroll - line; break;
        case UI_KEY_DOWN: to = s->scroll + line; break;
        case UI_KEY_PAGE_UP: to = s->scroll - s->view; break;
        case UI_KEY_PAGE_DOWN: to = s->scroll + s->view; break;
        case UI_KEY_HOME: to = 0; break;
        case UI_KEY_END: to = s->content; break;
        default: return false;
        }
        *moved = ui_scroll_set(s, to);
        return true;
}

/*
        One app's window and loop. The runner claims the standard descriptors,
        takes the window, sets its title, name, glass, scale and ground, and calls
        open. Then it runs the loop: events, closing, focus, ground, the size
        asked for by the hand, a due picture,
        the app's tick and its one descriptor, and the wait. An app keeps its
        state and its pictures; it does not repeat the loop.
*/
struct ui_app
{
        struct window_row address_to own;
        struct ui address_to ui;
        string_address missing;         // logged when the desktop has no window for it
        string_address title;
        string_address name;            // what the compositor remembers where it was put by, or none (windows_name)
        unsigned int glass;
        b32 address_to dirty;           // a picture is due: frame paints it
        b32 address_to focused;         // the app's own: the keys are its
        unsigned int address_to ground; // the app's own: the window's ground, as the loop last saw it
        unsigned int asked_w, asked_h;  // the size the hand last asked for
        string_address font, bold;      // the faces to draw in, or the UI_FONT defaults
        b32 no_bold;                    // true: no bold face is loaded; the default is the bold face
        string_address nofont;          // logged, with the pid, when the faces do not load
        int (*open)(void);              // the app's own start; -1 runs the loop, else the status to leave with
        b32 (*frame)(void);             // paints the due picture; false: it is due again in 50 ms
        b32 (*event)(const struct window_event address_to event); // false: quit
        b32 (*focus)(b32 focused);      // the keys came or went, optional; false: quit
        void (*changed)(void);          // optional: run after a ground or focus change, which also makes the picture due
        void (*size)(unsigned int width, unsigned int height);     // the hand asked for this size
        long (*tick)(long timeout, long address_to other);         // the app's timer; 0 runs the loop again now; other: a descriptor
        void (*ready)(void);            // other is readable
        void (*close)(void);            // before the window is left
        // Added for the terminal, which is an app on this loop. Each is optional, and none set is the loop above.
        void (*pass)(void);             // runs at the top of each pass, before the events
        void (*closing)(void);          // the window is closing: runs each pass, and the loop goes on; unset, the loop ends
        b32 (*done)(void);              // checked after tick: true ends the loop before the wait
        b32 own_faces;                  // the app opens its engine and loads its faces in open; the runner loads neither
        // The kit: its controls moved this frame, in rows top to bottom (all of it while the theme turns); the app
        // marks them to be drawn again, as the next frame comes a display period on.
        void (*moving)(int top, int bottom, b32 all);
        // A menu the app has open (ui_popup), whose wake the loop waits on too, and what it chose.
        struct ui_popup address_to popup;
        void (*chose)(int id);
        b32 mono;                       // the kit's mono face is loaded too (a tag, a code well, a key)
};

// Every row is due: the picture, and the app's changed hook when it has one.
static fn ui_app_changed(struct ui_app address_to app)
{
        *app->dirty = true;
        if (app->changed)
                app->changed();
}

static int ui_app_run(struct ui_app address_to app)
{
        struct windows address_to table;
        unsigned int row;
        int status;
        b32 running = true;

        claim_standard_descriptors();
        if (windows_take(app->own) < 0)
        {
                log_direct((address_any)app->missing, string_length((const_string)app->missing));
                return 1;
        }
        table = app->own->table;
        row = app->own->row;
        app->ui->scale = table->scale >= 1 && table->scale <= 8 ? (int)table->scale : 1;
        if (!app->own_faces)
        {
                if (!ui_engine_open(app->ui) || !ui_face_load(&app->ui->faces[0], app->font ? app->font : (string_address)UI_FONT_REGULAR))
                {
                        compositor_kmsg(app->nofont, (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                        windows_leave(app->own);
                        return 1;
                }
                app->ui->regular = &app->ui->faces[0];
                app->ui->bold = !app->no_bold && ui_face_load(&app->ui->faces[1], app->bold ? app->bold : (string_address)UI_FONT_BOLD)
                                    ? &app->ui->faces[1] : null;
                app->ui->mono = app->mono && ui_face_load(&app->ui->face_mono, (string_address)UI_FONT_MONO) ? &app->ui->face_mono : null;
        }
        // The theme the compositor keeps in the table, taken at once the first time.
        ui_kit.themed = __atomic_load_n(&table->themed, __ATOMIC_ACQUIRE);
        // A table the compositor has not themed yet is the standard: dark, white.
        ui_kit_theme(ui_kit.themed ? table->theme : UI_THEME_DARK, ui_kit.themed ? table->accent : UI_ACCENT_STANDARD, table->motion);
        string_copy_bounded((string_address)table->title[row], app->title, WINDOWS_TITLE);
        windows_name(app->own, (const char *)app->name);
        table->glass[row] = app->glass;
        *app->ground = table->ground[row];
        ui_inks(app->ui, *app->ground ? *app->ground : 0x101012u);
        // A window that is not glass is the kit's surface, and its inks are the surface's.
        app->ui->opaque = !(app->glass & WINDOW_GLASS);
        ui_kit_inks(app->ui);
        *app->focused = false;
        status = app->open();
        if (status >= 0)
        {
                windows_leave(app->own);
                return status;
        }

        while (running)
        {
                struct window_event event;
                long timeout = -1, other = -1, ready;

                if (app->pass)
                        app->pass();
                while (running && windows_event(app->own, &event))
                        if (!app->event(&event))
                                running = false;
                if (app->popup && app->popup->open)
                {
                        int chosen = ui_popup_pump(app->popup);

                        if (chosen != UI_MENU_NOTHING && app->chose)
                                app->chose(chosen);
                }

                if (table->state[row] & WINDOW_CLOSING)
                {
                        if (app->closing)
                                app->closing();
                        else
                                running = false;
                }

                if ((table->focus == row) != *app->focused)
                {
                        *app->focused = table->focus == row;
                        ui_app_changed(app);
                        if (app->focus && !app->focus(*app->focused))
                                running = false;
                }
                if (table->ground[row] && table->ground[row] != *app->ground)
                {
                        *app->ground = table->ground[row];
                        ui_inks(app->ui, *app->ground);
                        ui_app_changed(app);
                }
                // A new theme (the compositor counted it up and knocked): the whole picture turns to it.
                if (__atomic_load_n(&table->themed, __ATOMIC_ACQUIRE) != ui_kit.themed)
                {
                        ui_kit.themed = __atomic_load_n(&table->themed, __ATOMIC_ACQUIRE);
                        ui_kit_theme(table->theme, table->accent, table->motion);
                        ui_kit_inks(app->ui);
                        ui_app_changed(app);
                }
                if (table->want_width[row] && table->want_height[row] &&
                    (table->want_width[row] != app->asked_w || table->want_height[row] != app->asked_h))
                {
                        app->asked_w = table->want_width[row];
                        app->asked_h = table->want_height[row];
                        ui_kit_settle();
                        app->size(app->asked_w, app->asked_h);
                }

                if (!running)
                        break;

                /*
                        The kit's clock: a frame is drawn at its time, and while anything in it moves (a spring, a
                        transition, a ripple, the theme turning) the next is due a display period after it, the
                        picture marked due again. Nothing moving, the loop waits on the window and draws nothing.
                */
                if (*app->dirty)
                {
                        positive period = table->period ? table->period : 16666667u, now = clock_monotonic_nanoseconds();

                        if (ui_kit.busy && now < (positive)ui_kit.drawn + period)
                                timeout = (long)((positive)ui_kit.drawn + period - now);
                        else
                        {
                                ui_kit.now = (b64)now;
                                ui_kit.busy = 0;
                                ui_kit.damage_top = ui_kit.damage_bottom = 0;
                                ui_kit_turn();
                                if (!app->frame())
                                        timeout = 50000000;
                                else
                                        ui_kit.drawn = (b64)now;
                                if (ui_kit.busy)
                                {
                                        *app->dirty = true;
                                        if (app->moving)
                                                app->moving(ui_kit.damage_top, ui_kit.damage_bottom, !ui_tween_still(&ui_kit.turn));
                                        timeout = timeout < 0 || timeout > (long)period ? (long)period : timeout;
                                }
                        }
                }
                if (app->tick)
                {
                        timeout = app->tick(timeout, &other);
                        if (timeout == 0)
                                continue;
                }
                if (app->done && app->done())
                        break;

                ready = windows_wait_also(app->own, app->popup && app->popup->open ? app->popup->own.wake : -1, other, timeout);
                if (ready < 0)
                        break;
                if (other >= 0)
                        app->ready();
        }

        if (app->close)
                app->close();
        windows_leave(app->own);
        return 0;
}

/*
        The value of an option of the command line: the argument after the one at at, when the argument at at is name
        exactly ("-f", "-m" and the like), or null when it is not, or no argument follows it. The caller moves past
        the value when it takes one.
*/
static string_address ui_argument(positive at, string_address name)
{
        string_address argument;

        if (at + 1 >= (positive)program_argument_count())
                return null;
        argument = program_argument(at);
        for (positive i = 0;; i++)
        {
                if (argument[i] != name[i])
                        return null;
                if (!argument[i])
                        return program_argument(at + 1);
        }
}

/*
        A window's place in the table, centred on the primary screen (windows_output's index 0, the output itself
        where the table lists no screens): x by its width, and y by its height, raised offset pixels above the
        middle (offset 0 for the middle itself).
*/
static void ui_centre(struct windows address_to table, unsigned int row, int width, int height, int offset)
{
        int x, y, screen_width, screen_height;

        windows_output(table, 0, &x, &y, &screen_width, &screen_height);
        table->x[row] = x + (screen_width - width) / 2;
        table->y[row] = y + (screen_height - height) / 2 - offset;
}

// Whether a process is the program name: its command name is name and a newline, as /proc/PID/comm has it.
static b32 ui_is_program(unsigned int pid, string_address name)
{
        p8 path[40] = "/proc/", comm[16] = {0};
        bipolar handle;
        positive length = string_length(name);

        path[6 + positive_into_string(path + 6, (positive)pid)] = 0;
        string_copy((string_address)path + string_length((string_address)path), (string_address) "/comm");
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return false;
        system_read_once((b32)handle, comm, sizeof(comm) - 1);
        system_close(handle);
        return length < sizeof(comm) - 1 && !memory_compare(comm, name, length) && comm[length] == '\n';
}

/*
        Another window of the program name that has shown a picture is lifted: the compositor is told to raise it, as a
        second start asks. With put_away, the program is sent SIGTERM instead, and the compositor sees its door close as
        it does any program's end. The window own is not another. True when there was one.
*/
static b32 ui_lift_other(const struct window_row address_to own, string_address name, b32 put_away)
{
        struct windows address_to table = own->table;

        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                unsigned int pid = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);
                char door_name[48];
                bipolar door;

                if (row == own->row || !pid || !__atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE) ||
                    !ui_is_program(pid, name))
                        continue;

                if (put_away)
                {
                        system_call_2(syscall(kill), (positive)pid, SIGTERM);
                        return true;
                }
                __atomic_add_fetch(&table->lift[row], 1, __ATOMIC_RELEASE);
                windows_door_name(door_name, row, 0);
                door = system_open_at(AT_FDCWD, (string_address)door_name, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                if (door >= 0)
                {
                        system_write_once((b32)door, "", 1);
                        system_close(door);
                }
                return true;
        }
        return false;
}

/* ---- text: a field, a text area and the code view, over text.c's documents ---- */

/*
        One widget for every place text is typed: a field (one line, the kit's UI_INPUT draws the well and this draws
        what is in it), a text area (lines, wrapped, in the UI's face) and the code view (lines in the mono, a gutter of
        line numbers, the tokenizer's colours, soft wrap on or off, zoom). The document, its history and selections are
        text.c's; this is the hand and the pixels.

        The hand: a click puts the caret, a drag selects, Shift with a click extends, a double click takes a word and a
        drag after it whole words, a triple click a line and whole lines, Alt with a click adds a caret, Alt and Shift
        with a drag selects a box of columns, the middle button pastes PRIMARY, the right button asks the app for a
        context menu (ui_text_asks), a finger's tap is a caret and a finger's drag scrolls (thrown, it slows as the
        kit's lists do), and the wheel scrolls (Ctrl and the wheel zoom the code view). The keys are the keymap's
        (text.c, overridden by /root/canvas.text.keys); a key that is no binding types its character, and AltGr always
        types. The caret blinks only while the widget has the keys, never while it moves, and ui_text_tick says when
        it next needs a frame, so an idle widget wakes nothing. Colours are the kit's roles: the selection, the caret
        (the accent), the text, the placeholder (the text at 36%), an error's underline.

        Glyphs land on whole pixels: the code view lays its characters on a grid of whole cells (font_cells, and
        font_draw for a cluster with marks), and a field and an area draw a line with the kit's text, whose caret and
        selection follow the same pen.
*/
#define UI_TEXT_FIELD 0
#define UI_TEXT_AREA 1
#define UI_TEXT_CODE 2
#define UI_TEXT_CLUSTERS 2048 // the clusters of a line laid out at most: past them a line is drawn to there
#define UI_TEXT_BLINK_NS 530000000ull
#define UI_TEXT_CLICKS_NS 450000000ull
#define UI_TEXT_ROWS 512

// What the widget asks of the app after an event: ui_text_event answers these bits.
#define UI_TEXT_CHANGED 1u    // the text changed
#define UI_TEXT_MOVED 2u      // only the carets or the view
#define UI_TEXT_MENU 4u       // the context menu, at menu_x, menu_y
#define UI_TEXT_SUBMIT 8u     // Enter in a field
#define UI_TEXT_CANCEL 16u    // Escape in a field with nothing to cancel
#define UI_TEXT_COMMAND 32u   // a command the app does (find, save, ...): command
#define UI_TEXT_PASTE 64u     // a paste was asked of the compositor

struct ui_text
{
        struct doc doc;
        int mode;
        const struct doc_keymap address_to keymap;
        const struct window_row address_to own; // the window, for the clipboard
        const char address_to placeholder;
        const char address_to comment;          // a line comment, for the comment command
        b32 focused, read_only, wrap, numbers;
        int size;                               // logical pixels of the text
        int left, top, right, bottom, gutter;   // the last draw's text box, device pixels
        int line_height, cell;
        p64 top_line;
        int top_row;
        b64 scroll_x;
        int clicks, click_x, click_y;
        positive click_at;
        b32 dragging, drag_box, touch;
        int drag_kind;                          // 1 characters, 2 words, 3 lines
        p64 drag_start, drag_end;
        p64 box_line, box_column;
        struct ui_drag kinetic;
        positive blink_at;
        b32 caret_off;
        p32 asks, command;
        int menu_x, menu_y;
        int zoom;
        p8 preedit[64];                         // composition: text an input method is making, drawn at the caret
        p32 preedit_length;
};

/*
        The keys every text widget of a process reads: the defaults (text.c's doc_default_keys) and the lines of
        /root/canvas.text.keys over them, read the first time a widget asks. The file is the person's own; a line that is
        not a binding is skipped, so a mistake in it costs that line and not the keys.
*/
#define UI_TEXT_KEYS_PATH "/root/canvas.text.keys"

static const struct doc_keymap address_to ui_text_keymap(void)
{
        static struct doc_keymap keys;
        static b32 read;

        if (!read)
        {
                static char text[16384];
                bipolar handle = system_open_at(AT_FDCWD, (string_address)UI_TEXT_KEYS_PATH, FILE_READ | O_CLOEXEC);
                bipolar got = handle >= 0 ? system_read_once((b32)handle, text, sizeof(text) - 1) : 0;

                if (handle >= 0)
                        system_close(handle);
                doc_keymap_defaults(&keys);
                if (got > 0)
                        doc_keymap_read(&keys, text, (positive)got);
                read = true;
        }
        return &keys;
}

// A widget over text (length bytes, kept by the caller while it lives: a field's typed text is the widget's own).
static b32 ui_text_open(struct ui_text address_to t, int mode, const p8 address_to text, p64 length, const struct doc_keymap address_to keymap)
{
        memory_zero(t, sizeof(*t));
        if (!doc_open(&t->doc, text, length))
                return false;
        t->mode = mode;
        t->keymap = keymap;
        t->doc.single_line = mode == UI_TEXT_FIELD;
        t->doc.auto_close = mode == UI_TEXT_CODE;
        t->doc.normalize_paste = true;
        t->size = mode == UI_TEXT_CODE ? 13 : 15;
        t->numbers = mode == UI_TEXT_CODE;
        t->wrap = mode == UI_TEXT_AREA;
        t->comment = "//";
        return true;
}

static fn ui_text_close(struct ui_text address_to t)
{
        doc_close(&t->doc);
}

// The whole text into into, room bytes at most, a NUL after it: its length. What a field's owner reads it as.
static positive ui_text_string(const struct ui_text address_to t, p8 address_to into, positive room)
{
        positive length = room ? doc_read(&t->doc, 0, into, room - 1) : 0;

        if (room)
                into[length] = 0;
        return length;
}

// The text replaced, the caret at its end: what an app that sets a field does.
static fn ui_text_set(struct ui_text address_to t, const p8 address_to text, positive length)
{
        doc_replace(&t->doc, 0, doc_length(&t->doc), text, length);
        doc_select_set(&t->doc, doc_length(&t->doc), doc_length(&t->doc));
}

/*
        A line laid out: each cluster's offset in the document and its left edge, in device pixels from the line's start,
        the last entry the line's end. The display text a line is drawn as goes into shown: a tab as spaces, a control
        as its control picture, a byte that is not UTF-8 as U+FFFD. Answers how many clusters.
*/
struct ui_text_layout
{
        p64 offset[UI_TEXT_CLUSTERS + 1];
        int x[UI_TEXT_CLUSTERS + 1];
        p32 shown_at[UI_TEXT_CLUSTERS + 1]; // where each cluster starts in shown
        p8 shown[UI_TEXT_CLUSTERS * 8];
        p32 shown_length;
        p32 count;
};

static const font_face address_to ui_text_face(const struct ui address_to ui, const struct ui_text address_to t)
{
        return t->mode == UI_TEXT_CODE && ui->mono ? ui->mono : ui->regular;
}

static p32 ui_text_em(const struct ui address_to ui, const struct ui_text address_to t)
{
        int size = t->size + t->zoom;

        size = size < 6 ? 6 : size > 72 ? 72 : size;
        return (p32)(size * ui->scale) << 6;
}

static fn ui_text_layout_line(struct ui address_to ui, const struct ui_text address_to t, p64 line, struct ui_text_layout address_to l)
{
        const font_face address_to faces[2] = {ui_text_face(ui, t), ui->regular};
        p32 em = ui_text_em(ui, t);
        p64 at = doc_line_start(&t->doc, line), stop = doc_line_end(&t->doc, line), column = 0;
        struct ui_pen pen;
        int cell;

        l->count = 0;
        l->shown_length = 0;
        // No face loaded (a program that never draws): nothing to lay out.
        if (!faces[0])
        {
                l->offset[0] = at;
                l->x[0] = 0;
                l->shown_at[0] = 0;
                l->shown[0] = 0;
                return;
        }
        cell = (int)font_metrics_at(faces[0], em).cell_width;
        ui_pen_start(faces, em, &pen);
        while (at < stop && l->count < UI_TEXT_CLUSTERS)
        {
                p64 next = doc_cluster_end(&t->doc, at);
                p8 cluster[32];
                positive size = doc_read(&t->doc, at, cluster, next - at < sizeof(cluster) ? (positive)(next - at) : sizeof(cluster));
                p32 code, width = doc_cluster_width(&t->doc, cluster, size, column);
                p8 shown[32];
                positive shown_size = 0;

                doc_decode(cluster, size, &code);
                if (code == '\t')
                        for (p32 k = 0; k < width && shown_size < sizeof(shown); k++)
                                shown[shown_size++] = ' ';
                else if (code & DOC_INVALID)
                        shown_size = memory_utf8_encode(shown, 4, 0xfffd);
                else if (code < 0x20 || code == 0x7f)
                        shown_size = memory_utf8_encode(shown, 4, code == 0x7f ? 0x2421 : 0x2400 + code);
                else if (code >= 0x80 && code < 0xa0)
                        shown_size = memory_utf8_encode(shown, 4, 0xfffd);
                else
                        memory_copy(shown, cluster, shown_size = size);
                l->offset[l->count] = at;
                l->shown_at[l->count] = l->shown_length;
                if (t->mode == UI_TEXT_CODE)
                        l->x[l->count] = (int)column * cell;
                else
                {
                        l->x[l->count] = ui_pen_width(&pen);
                        for (positive k = 0; k < shown_size;)
                        {
                                p32 one;

                                k = font_decode(shown, shown_size, k, &one);
                                ui_pen_step(ui, faces, em, one, &pen);
                        }
                }
                memory_copy(l->shown + l->shown_length, shown, shown_size);
                l->shown_length += (p32)shown_size;
                column += width;
                l->count++;
                at = next;
        }
        l->offset[l->count] = at;
        l->shown_at[l->count] = l->shown_length;
        l->x[l->count] = t->mode == UI_TEXT_CODE ? (int)column * cell : ui_pen_width(&pen);
        l->shown[l->shown_length] = 0;
}

// The x of an offset on its laid line (its cluster's left edge).
static int ui_text_x_of(const struct ui_text_layout address_to l, p64 offset)
{
        for (p32 k = 0; k <= l->count; k++)
                if (l->offset[k] >= offset)
                        return l->x[k];
        return l->x[l->count];
}

// The offset nearest an x on a laid line: the boundary whose edge is closest.
static p64 ui_text_at_x(const struct ui_text_layout address_to l, int x)
{
        for (p32 k = 0; k < l->count; k++)
                if (x < (l->x[k] + l->x[k + 1]) / 2)
                        return l->offset[k];
        return l->offset[l->count];
}

// The metrics the widget draws with, worked out again for the face, the size and the zoom.
static fn ui_text_metrics(struct ui address_to ui, struct ui_text address_to t)
{
        font_metrics m;

        if (!ui_text_face(ui, t))
                return;
        m = font_metrics_at(ui_text_face(ui, t), ui_text_em(ui, t));

        t->line_height = t->mode == UI_TEXT_FIELD ? (int)m.cell_height : (int)m.cell_height + (int)m.cell_height / 4;
        t->cell = (int)m.cell_width;
}

// The rows a view shows, and the document line and row the pointer at y is on.
static int ui_text_rows(const struct ui_text address_to t)
{
        return t->line_height ? (t->bottom - t->top) / t->line_height : 1;
}

/*
        The view keeps the primary caret in sight: lines scroll so it is between the top and the bottom row, columns so
        it is inside the text box (a field scrolls sideways only).
*/
static fn ui_text_reveal(struct ui address_to ui, struct ui_text address_to t)
{
        static struct ui_text_layout l;
        p64 caret = t->doc.selections[t->doc.primary].active, line = doc_line_of(&t->doc, caret);
        int rows = ui_text_rows(t), width = t->right - t->left, x;

        if (t->mode != UI_TEXT_FIELD)
        {
                if (line < t->top_line)
                        t->top_line = line;
                else if (rows > 0 && line >= t->top_line + (p64)rows)
                        t->top_line = line - (p64)rows + 1;
        }
        if (t->wrap && t->mode != UI_TEXT_FIELD)
        {
                t->scroll_x = 0;
                return;
        }
        ui_text_layout_line(ui, t, line, &l);
        x = ui_text_x_of(&l, caret);
        if (x - t->scroll_x < 0)
                t->scroll_x = x - width / 4 > 0 ? x - width / 4 : 0;
        else if (x - t->scroll_x > width - 2 * ui->scale)
                t->scroll_x = x - width + width / 4;
}

// The document offset under device pixels x, y of the widget's text box (clamped to it).
static p64 ui_text_hit(struct ui address_to ui, struct ui_text address_to t, int x, int y)
{
        static struct ui_text_layout l;
        p64 line = t->mode == UI_TEXT_FIELD ? 0 : t->top_line + (p64)((y > t->top ? y - t->top : 0) / (t->line_height ? t->line_height : 1));

        line = line >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : line;
        ui_text_layout_line(ui, t, line, &l);
        return ui_text_at_x(&l, x - t->left + (int)t->scroll_x);
}

// The caret is drawn for the next half second whatever the blink says: it is moving, or just typed at.
static fn ui_text_wake(struct ui_text address_to t)
{
        t->blink_at = clock_monotonic_nanoseconds();
        t->caret_off = false;
}

/*
        The blink: when the widget next needs a frame (0 for never: it has not the keys), the caret turned when its time
        is up. The app asks after each pass and sleeps till then; nothing blinks in a window without the keys.
*/
static positive ui_text_tick(struct ui_text address_to t, positive now, b32 address_to redraw)
{
        positive due;

        if (!t->focused || t->read_only)
                return 0;
        due = t->blink_at + UI_TEXT_BLINK_NS;
        if (now >= due)
        {
                t->caret_off = !t->caret_off;
                t->blink_at = now;
                *redraw = true;
                due = now + UI_TEXT_BLINK_NS;
        }
        return due;
}

// A selection's text to a selection of the clipboard.
static fn ui_text_copy(struct ui_text address_to t, unsigned int which)
{
        p8 address_to text;
        positive room, length = doc_copy_text(&t->doc, &text, &room);

        if (length && t->own)
                windows_clip_offer(t->own, which, text, length);
        array_store_release(text, room, length);
}

// What the clipboard said: a paste that may be read is typed in as one undo.
static p32 ui_text_clip(struct ui_text address_to t, const struct window_event address_to event)
{
        if (event->b == WINDOWS_CLIP_TAKEN || event->b == WINDOWS_CLIP_DECLINED)
                windows_clip_done((unsigned int)event->a);
        if (event->b == WINDOWS_CLIP_READY && t->own && !t->read_only)
        {
                p8 address_to bytes;
                unsigned long length;

                if (windows_clip_read(t->own, (unsigned int)event->a, &bytes, &length) > 0)
                {
                        doc_paste(&t->doc, bytes, length);
                        windows_clip_free(bytes, length);
                        ui_text_wake(t);
                        return UI_TEXT_CHANGED;
                }
        }
        return 0;
}

// A command of the keymap that needs the view or the clipboard; the rest go to text.c's doc_command.
static p32 ui_text_command(struct ui address_to ui, struct ui_text address_to t, p32 command)
{
        p64 version = t->doc.version;
        int page = ui_text_rows(t) > 1 ? ui_text_rows(t) - 1 : 1;

        switch (command)
        {
        case DOC_CMD_COPY:
                ui_text_copy(t, WINDOWS_CLIP_CLIPBOARD);
                return 0;
        case DOC_CMD_CUT:
                if (t->read_only)
                        return 0;
                ui_text_copy(t, WINDOWS_CLIP_CLIPBOARD);
                doc_erase_selections(&t->doc);
                break;
        case DOC_CMD_PASTE:
                if (t->own && !t->read_only)
                        windows_clip_ask(t->own, WINDOWS_CLIP_CLIPBOARD);
                return UI_TEXT_PASTE;
        case DOC_CMD_PAGE_UP: case DOC_CMD_PAGE_DOWN: case DOC_CMD_SELECT_PAGE_UP: case DOC_CMD_SELECT_PAGE_DOWN:
        {
                b32 up = command == DOC_CMD_PAGE_UP || command == DOC_CMD_SELECT_PAGE_UP;

                doc_move_vertical(&t->doc, up ? -page : page, command == DOC_CMD_SELECT_PAGE_UP || command == DOC_CMD_SELECT_PAGE_DOWN);
                t->top_line = up ? (t->top_line > (p64)page ? t->top_line - (p64)page : 0) : t->top_line + (p64)page;
                break;
        }
        case DOC_CMD_ZOOM_IN: case DOC_CMD_ZOOM_OUT: case DOC_CMD_ZOOM_RESET:
                if (t->mode != UI_TEXT_CODE)
                        return 0;
                t->zoom = command == DOC_CMD_ZOOM_RESET ? 0 : t->zoom + (command == DOC_CMD_ZOOM_IN ? 1 : -1);
                t->zoom = t->zoom < -6 ? -6 : t->zoom > 40 ? 40 : t->zoom;
                ui_text_metrics(ui, t);
                return UI_TEXT_MOVED;
        case DOC_CMD_WRAP:
                if (t->mode == UI_TEXT_FIELD)
                        return 0;
                t->wrap = !t->wrap;
                return UI_TEXT_MOVED;
        case DOC_CMD_MENU:
        {
                // The menu at the caret, for a key that asks for it.
                static struct ui_text_layout l;
                p64 caret = t->doc.selections[t->doc.primary].active, line = doc_line_of(&t->doc, caret);

                ui_text_layout_line(ui, t, line, &l);
                t->menu_x = t->left + ui_text_x_of(&l, caret) - (int)t->scroll_x;
                t->menu_y = t->top + (int)(line >= t->top_line ? line - t->top_line + 1 : 1) * t->line_height;
                return UI_TEXT_MENU;
        }
        case DOC_CMD_FOLD: case DOC_CMD_UNFOLD:
                doc_fold(&t->doc, doc_line_of(&t->doc, t->doc.selections[t->doc.primary].active), command == DOC_CMD_FOLD);
                return UI_TEXT_MOVED;
        default:
                if (t->read_only && command >= DOC_CMD_BACKSPACE && command <= DOC_CMD_SORT_DESCENDING)
                        return 0;
                if (t->mode == UI_TEXT_FIELD && (command == DOC_CMD_NEWLINE || command == DOC_CMD_MOVE_LINE_UP || command == DOC_CMD_MOVE_LINE_DOWN ||
                                                 command == DOC_CMD_COPY_LINE_UP || command == DOC_CMD_COPY_LINE_DOWN || command == DOC_CMD_LINE_BELOW ||
                                                 command == DOC_CMD_LINE_ABOVE || command == DOC_CMD_TAB))
                        return command == DOC_CMD_NEWLINE ? UI_TEXT_SUBMIT : 0;
                if (!doc_command(&t->doc, command, t->comment))
                {
                        t->command = command;
                        return UI_TEXT_COMMAND;
                }
        }
        ui_text_wake(t);
        ui_text_reveal(ui, t);
        return t->doc.version != version ? UI_TEXT_CHANGED : UI_TEXT_MOVED;
}

// A key: the keymap's command, or the character typed. AltGr always types; a key with Control or Alt that is no
// binding is not typed.
static p32 ui_text_key(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event)
{
        p32 flags = event->flags, character = (p32)event->b, modifiers = 0, command;

        if (!(flags & WINDOW_KEY_DOWN))
                return 0;
        if (!(flags & WINDOW_KEY_ALTGR))
        {
                modifiers = (flags & WINDOW_KEY_SHIFT ? DOC_MOD_SHIFT : 0) | (flags & WINDOW_KEY_CONTROL ? DOC_MOD_CONTROL : 0) |
                            (flags & WINDOW_KEY_ALT ? DOC_MOD_ALT : 0) | (flags & WINDOW_KEY_SUPER ? DOC_MOD_SUPER : 0);
                command = t->keymap ? doc_keymap_find(t->keymap, (p32)event->a, character, modifiers) : DOC_CMD_NONE;
                if (command == DOC_CMD_SINGLE && t->mode == UI_TEXT_FIELD && t->doc.selection_count == 1 && !doc_any_selected(&t->doc))
                        return UI_TEXT_CANCEL;
                if (command != DOC_CMD_NONE)
                        return ui_text_command(ui, t, command);
                if (modifiers & (DOC_MOD_CONTROL | DOC_MOD_ALT | DOC_MOD_SUPER))
                        return 0;
        }
        if (t->read_only || character < 0x20 || character == 0x7f)
                return 0;
        {
                p8 bytes[4];
                positive size = memory_utf8_encode(bytes, sizeof(bytes), character);
                p64 version = t->doc.version;

                t->doc.now = clock_monotonic_nanoseconds();
                doc_type(&t->doc, bytes, size);
                ui_text_wake(t);
                ui_text_reveal(ui, t);
                return t->doc.version != version ? UI_TEXT_CHANGED : UI_TEXT_MOVED;
        }
}

/*
        The pointer and the wheel. x and y are the window's device pixels; the widget's box is where it was last drawn.
        Answers what changed, and UI_TEXT_MENU for the right button (menu_x and menu_y say where).
*/
static p32 ui_text_pointer(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y)
{
        b32 inside = x >= t->left - t->gutter && x < t->right && y >= t->top && y < t->bottom;
        b32 shift = (event->flags & WINDOW_KEY_SHIFT) != 0, alt = (event->flags & WINDOW_KEY_ALT) != 0;
        positive now = clock_monotonic_nanoseconds();

        if (event->type == WINDOW_EVENT_WHEEL)
        {
                if (!inside)
                        return 0;
                if ((event->flags & WINDOW_KEY_CONTROL) && t->mode == UI_TEXT_CODE)
                        return ui_text_command(ui, t, event->a > 0 ? DOC_CMD_ZOOM_IN : DOC_CMD_ZOOM_OUT);
                if (event->b)
                        t->scroll_x = t->scroll_x - (b64)event->a * t->cell * 3 / 120 > 0 ? t->scroll_x - (b64)event->a * t->cell * 3 / 120 : 0;
                else
                {
                        b64 lines = -(b64)event->a * 3 / 120, to = (b64)t->top_line + (lines ? lines : event->a < 0 ? 1 : -1);

                        t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
                }
                return UI_TEXT_MOVED;
        }
        if (event->type == WINDOW_EVENT_BUTTON)
        {
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

                if (!down)
                {
                        b32 was = t->dragging;

                        t->dragging = false;
                        if (t->touch && ui_drag_let_go(&t->kinetic, now))
                                return UI_TEXT_MOVED;
                        if (was && doc_any_selected(&t->doc))
                                ui_text_copy(t, WINDOWS_CLIP_PRIMARY);
                        return 0;
                }
                if (!inside)
                        return 0;
                if (event->a == 0x111)
                {
                        p64 at = ui_text_hit(ui, t, x, y);
                        b32 in_selection = false;

                        for (positive s = 0; s < t->doc.selection_count; s++)
                                in_selection |= at >= doc_sel_start(&t->doc.selections[s]) && at <= doc_sel_end(&t->doc.selections[s]) &&
                                                doc_any_selected(&t->doc);
                        if (!in_selection)
                                doc_select_set(&t->doc, at, at);
                        t->menu_x = x, t->menu_y = y;
                        return UI_TEXT_MENU | UI_TEXT_MOVED;
                }
                if (event->a == 0x112)
                {
                        p64 at = ui_text_hit(ui, t, x, y);

                        if (t->read_only || !t->own)
                                return 0;
                        doc_select_set(&t->doc, at, at);
                        windows_clip_ask(t->own, WINDOWS_CLIP_PRIMARY);
                        return UI_TEXT_PASTE | UI_TEXT_MOVED;
                }
                if (event->a != 0x110)
                        return 0;
                t->touch = (event->flags & WINDOW_KEY_TOUCH) != 0;
                if (t->touch)
                {
                        // A finger: a caret where it lands, and a drag scrolls rather than selects.
                        ui_drag_start(&t->kinetic, y, now);
                        doc_select_set(&t->doc, ui_text_hit(ui, t, x, y), ui_text_hit(ui, t, x, y));
                        t->dragging = true;
                        ui_text_wake(t);
                        return UI_TEXT_MOVED;
                }
                {
                        b32 again = now - t->click_at < UI_TEXT_CLICKS_NS && (t->click_x - x) * (t->click_x - x) + (t->click_y - y) * (t->click_y - y) <= 16 * ui->scale * ui->scale;
                        p64 at = ui_text_hit(ui, t, x, y), start = at, finish = at;

                        t->clicks = again ? t->clicks % 3 + 1 : 1;
                        t->click_at = now, t->click_x = x, t->click_y = y;
                        t->drag_box = false;
                        if (t->clicks == 2)
                                doc_word_at(&t->doc, at, &start, &finish);
                        else if (t->clicks == 3)
                        {
                                p64 line = doc_line_of(&t->doc, at);

                                start = doc_line_start(&t->doc, line);
                                finish = doc_line_start(&t->doc, line + 1);
                        }
                        if (alt && shift && t->mode != UI_TEXT_FIELD)
                        {
                                t->drag_box = true;
                                t->box_line = doc_line_of(&t->doc, at);
                                t->box_column = doc_column(&t->doc, at);
                                doc_select_box(&t->doc, t->box_line, t->box_column, t->box_line, t->box_column);
                        }
                        else if (alt && t->clicks == 1)
                                doc_select_add(&t->doc, at, at);
                        else if (shift && t->clicks == 1)
                        {
                                struct doc_selection address_to primary = &t->doc.selections[t->doc.primary];

                                primary->active = at;
                                doc_normalize(&t->doc);
                                doc_seal(&t->doc);
                        }
                        else
                                doc_select_set(&t->doc, start, finish);
                        t->drag_kind = t->clicks;
                        t->drag_start = start, t->drag_end = finish;
                        t->dragging = true;
                        ui_text_wake(t);
                        return UI_TEXT_MOVED;
                }
        }
        if (event->type == WINDOW_EVENT_MOTION && t->dragging)
        {
                if (t->touch)
                {
                        int moved = ui_drag_follow(&t->kinetic, y, now), lines = t->line_height ? -moved / t->line_height : 0;
                        b64 to = (b64)t->top_line + lines;

                        if (lines)
                                t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
                        return lines ? UI_TEXT_MOVED : 0;
                }
                {
                        p64 at = ui_text_hit(ui, t, x, y), start = at, finish = at;
                        struct doc_selection address_to primary = &t->doc.selections[t->doc.primary];

                        if (t->drag_box)
                        {
                                doc_select_box(&t->doc, t->box_line, t->box_column, doc_line_of(&t->doc, at), doc_column(&t->doc, at));
                                return UI_TEXT_MOVED;
                        }
                        if (t->drag_kind == 2)
                                doc_word_at(&t->doc, at, &start, &finish);
                        else if (t->drag_kind == 3)
                        {
                                p64 line = doc_line_of(&t->doc, at);

                                start = doc_line_start(&t->doc, line), finish = doc_line_start(&t->doc, line + 1);
                        }
                        // The selection grows by its kind from where it began: back to the start of a word or a line
                        // before it, on to the end of one after it.
                        if (start < t->drag_start)
                                primary->anchor = t->drag_end, primary->active = start;
                        else
                                primary->anchor = t->drag_start, primary->active = finish > t->drag_end ? finish : t->drag_end;
                        if (t->drag_kind == 1)
                                primary->active = at;
                        doc_normalize(&t->doc);
                        ui_text_reveal(ui, t);
                        ui_text_wake(t);
                        return UI_TEXT_MOVED;
                }
        }
        return 0;
}

// A thrown finger's scroll, a frame at a time: whether it still moves.
static b32 ui_text_fling(struct ui_text address_to t, positive now)
{
        int moved = ui_drag_step(&t->kinetic, now), lines = t->line_height ? -moved / t->line_height : 0;
        b64 to = (b64)t->top_line + lines;

        if (lines)
                t->top_line = to < 0 ? 0 : (p64)to >= doc_line_count(&t->doc) ? doc_line_count(&t->doc) - 1 : (p64)to;
        return t->kinetic.flying;
}

// Any event of the window the widget may take: keys only while it has the keys; the pointer within its box.
static p32 ui_text_event(struct ui address_to ui, struct ui_text address_to t, const struct window_event address_to event, int x, int y)
{
        t->asks = 0;
        if (event->type == WINDOW_EVENT_KEY)
                return t->focused ? t->asks = ui_text_key(ui, t, event) : 0;
        if (event->type == WINDOW_EVENT_CLIP)
                return t->asks = ui_text_clip(t, event);
        return t->asks = ui_text_pointer(ui, t, event, x, y);
}

/* ---- drawing ---- */

// The colour of a role of the tokenizer's, from the kit's roles: the text, and the accent and status colours for the rest.
static unsigned int ui_text_role(int role)
{
        switch (role)
        {
        case DOC_ROLE_KEYWORD: return ui_role(UI_PRIMARY);
        case DOC_ROLE_TYPE: return ui_role(UI_INFO);
        case DOC_ROLE_STRING: return ui_role(UI_SUCCESS);
        case DOC_ROLE_NUMBER: return ui_role(UI_WARNING);
        case DOC_ROLE_COMMENT: return ui_role(UI_ON_SURFACE_VARIANT);
        case DOC_ROLE_PREPROCESSOR: return ui_role(UI_WARNING);
        case DOC_ROLE_FUNCTION: return ui_role(UI_ON_SURFACE);
        case DOC_ROLE_VARIABLE: return ui_role(UI_INFO);
        case DOC_ROLE_INVALID: return ui_role(UI_ERROR);
        default: return ui_role(UI_ON_SURFACE);
        }
}

// Whether offset is inside any selection (an empty one: none).
static b32 ui_text_selected(const struct ui_text address_to t, p64 offset)
{
        for (positive s = 0; s < t->doc.selection_count; s++)
        {
                const struct doc_selection address_to sel = &t->doc.selections[s];

                if (offset >= doc_sel_start(sel) && offset < doc_sel_end(sel))
                        return true;
        }
        return false;
}

/*
        One laid line on the picture at its baseline: the selection behind it, then the text, then each caret on it.
        Proportional: the kit's text, cut to the box. The code view: whole cells, a run of one colour at a time.
*/
static fn ui_text_draw_line(struct ui address_to ui, struct ui_text address_to t, const struct ui_text_layout address_to l, p64 line,
                            int y, int baseline, unsigned int ground)
{
        int origin = t->left - (int)t->scroll_x;
        unsigned int selection = ui_role(UI_SELECTION), text = ui_role(UI_ON_SURFACE);
        static struct doc_token tokens[512];
        positive token_count = t->mode == UI_TEXT_CODE ? doc_tokens(&t->doc, line, tokens, 512) : 0;

        // The selection: one rectangle a run of selected clusters, and a sliver past the end of a line the selection
        // goes on from.
        for (p32 k = 0; k < l->count;)
        {
                p32 from = k;

                while (k < l->count && ui_text_selected(t, l->offset[k]))
                        k++;
                if (k > from)
                        ui_fill(ui, origin + l->x[from], y, l->x[k] - l->x[from], t->line_height,
                                ui_tone(selection & 0xffffffu, selection >> 24));
                else
                        k++;
        }
        if (ui_text_selected(t, l->offset[l->count]) && t->mode != UI_TEXT_FIELD)
                ui_fill(ui, origin + l->x[l->count], y, t->cell / 2 + ui->scale, t->line_height, ui_tone(selection & 0xffffffu, selection >> 24));

        if (t->mode != UI_TEXT_CODE)
                ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, origin, baseline, (string_address)l->shown,
                          t->size + t->zoom, text & 0xffffffu, 255, t->left, t->right, 0);
        else
        {
                // A run is the clusters of one role; each cluster of a single code point one cell is part of a run drawn
                // with font_cells, any other (marks, wide, emoji) a font_draw of its own at its cell.
                const font_face address_to faces[2] = {ui_text_face(ui, t), ui->regular};
                font_target target = {ui->pixels, ui->pitch, ui->width, ui->bottom};
                font_metrics grid = font_metrics_at(faces[0], ui_text_em(ui, t));
                p32 codes[256];
                positive token = 0;

                for (p32 k = 0; k < l->count;)
                {
                        p64 offset = l->offset[k] - l->offset[0];
                        unsigned int ink = text;
                        p32 run = 0, start = k;

                        while (token < token_count && tokens[token].start + tokens[token].length <= offset)
                                token++;
                        if (token < token_count && tokens[token].start <= offset)
                                ink = ui_text_role(tokens[token].role);
                        while (k < l->count && run < 256)
                        {
                                p32 code;
                                positive size = l->shown_at[k + 1] - l->shown_at[k];
                                p64 here = l->offset[k] - l->offset[0];
                                unsigned int ink_here = text;
                                positive look = token;

                                while (look < token_count && tokens[look].start + tokens[look].length <= here)
                                        look++;
                                if (look < token_count && tokens[look].start <= here)
                                        ink_here = ui_text_role(tokens[look].role);
                                if (ink_here != ink || (k > start && ui_text_selected(t, l->offset[k]) != ui_text_selected(t, l->offset[start])))
                                        break;
                                if (doc_decode(l->shown + l->shown_at[k], size, &code) != size || l->x[k + 1] - l->x[k] != t->cell)
                                {
                                        if (k > start)
                                                break;
                                        if (origin + l->x[k] >= t->left - t->cell && origin + l->x[k] < t->right)
                                                font_draw(ui->engine, faces, 2, l->shown + l->shown_at[k], size, ui_text_em(ui, t), 0, &target,
                                                          (b32)(origin + l->x[k]) << 16, baseline, ink & 0xffffffu, FONT_UNKNOWN);
                                        k++;
                                        run = 0;
                                        goto next;
                                }
                                codes[run++] = code;
                                k++;
                        }
                        if (run && origin + l->x[start] < t->right && origin + l->x[k] > t->left - t->cell)
                        {
                                unsigned int paper = ui_text_selected(t, l->offset[start])
                                                         ? window_mix(ground, selection & 0xffffffu, (selection >> 24) * 256 / 255)
                                                         : ground;

                                font_cells(ui->engine, faces, 2, codes, run, &grid, ui_text_em(ui, t), 0, &target, origin + l->x[start],
                                           y + (t->line_height - grid.cell_height) / 2, ink & 0xffffffu, paper);
                        }
                next:;
                }
        }
        // Squiggles under what a diagnostic marks: a wave in the error's or the warning's colour.
        for (positive r = 0; r < t->doc.range_count; r++)
        {
                const struct doc_range address_to range = &t->doc.ranges[r];
                unsigned int colour;

                if (range->kind != DOC_RANGE_ERROR && range->kind != DOC_RANGE_WARNING && range->kind != DOC_RANGE_INFO)
                        continue;
                if (range->finish <= l->offset[0] || range->start > l->offset[l->count])
                        continue;
                colour = ui_role(range->kind == DOC_RANGE_ERROR ? UI_ERROR : range->kind == DOC_RANGE_WARNING ? UI_WARNING : UI_INFO);
                for (int x = origin + ui_text_x_of(l, range->start), stop = origin + ui_text_x_of(l, range->finish); x < stop; x++)
                        ui_fill(ui, x, baseline + 2 * ui->scale + ((x / (2 * ui->scale)) & 1) * ui->scale, 1, ui->scale, ui_tone(colour & 0xffffffu, 255));
        }
        // The carets on this line, in the accent, unless it is the blink's off half; a composition's text after the
        // primary, underlined.
        if (t->focused && !t->caret_off)
                for (positive s = 0; s < t->doc.selection_count; s++)
                {
                        p64 caret = t->doc.selections[s].active;

                        if (caret < l->offset[0] || caret > l->offset[l->count])
                                continue;
                        ui_fill(ui, origin + ui_text_x_of(l, caret), y + ui->scale, t->doc.overwrite ? t->cell : 2 * ui->scale,
                                t->line_height - 2 * ui->scale, ui_tone(ui_role(UI_PRIMARY) & 0xffffffu, t->doc.overwrite ? 110 : 255));
                }
        if (t->preedit_length && t->focused)
        {
                p64 caret = t->doc.selections[t->doc.primary].active;

                if (caret >= l->offset[0] && caret <= l->offset[l->count])
                {
                        int x = origin + ui_text_x_of(l, caret);
                        int wide = ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, x, baseline, t->preedit,
                                             t->size + t->zoom, text & 0xffffffu, 255, t->left, t->right, 0);

                        ui_fill(ui, x, baseline + 2 * ui->scale, wide, ui->scale, ui_tone(text & 0xffffffu, 255));
                }
        }
}

/*
        The widget drawn into the box x, y, width by height (device pixels), its ground already there (a field's well,
        a window's paper): ground is that colour, for the code view's cells. The rows from the top line down, wrapped
        when wrap is on, with the gutter's line numbers on the left of a code view.
*/
static fn ui_text_draw(struct ui address_to ui, struct ui_text address_to t, int x, int y, int width, int height, unsigned int ground)
{
        static struct ui_text_layout l;
        p64 lines = doc_line_count(&t->doc), line = t->mode == UI_TEXT_FIELD ? 0 : t->top_line;
        font_metrics m;
        int row_y;

        ui_text_metrics(ui, t);
        m = font_metrics_at(ui_text_face(ui, t), ui_text_em(ui, t));
        t->gutter = 0;
        if (t->numbers)
        {
                int digits = 1;

                for (p64 n = lines; n >= 10; n /= 10)
                        digits++;
                t->gutter = (digits + 2) * t->cell;
        }
        t->left = x + t->gutter, t->top = y, t->right = x + width, t->bottom = y + height;
        if (t->mode == UI_TEXT_FIELD)
                t->top = y + (height - t->line_height) / 2, t->bottom = t->top + t->line_height;
        if (!doc_length(&t->doc) && t->placeholder && !t->preedit_length)
        {
                unsigned int text = ui_role(UI_ON_SURFACE);

                ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, t->left, t->top + m.baseline, (string_address)t->placeholder,
                          t->size + t->zoom, text & 0xffffffu, 92, t->left, t->right, 0);
        }
        for (row_y = t->top; row_y + t->line_height <= t->bottom + (t->mode == UI_TEXT_FIELD) && line < lines; line++)
        {
                p64 hidden_to;
                int baseline = row_y + (t->line_height - (int)m.cell_height) / 2 + (int)m.baseline;

                if (doc_hidden(&t->doc, line))
                        continue;
                ui_text_layout_line(ui, t, line, &l);
                // The current line, lit faintly, in a code view with one caret.
                if (t->mode == UI_TEXT_CODE && t->focused && t->doc.selection_count == 1 &&
                    doc_line_of(&t->doc, t->doc.selections[0].active) == line && !doc_any_selected(&t->doc))
                        ui_fill(ui, t->left, row_y, t->right - t->left, t->line_height, ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 10));
                if (t->numbers)
                {
                        char number[24];
                        positive digits = positive_into_string((string_address)number, (positive)(line + 1));
                        b32 current = doc_line_of(&t->doc, t->doc.selections[t->doc.primary].active) == line;
                        int wide = (int)digits * t->cell;

                        number[digits] = 0;
                        ui_run_in(ui, (const font_face address_to const[]){ui_text_face(ui, t), ui->regular}, t->left - t->cell - wide, baseline,
                                  (string_address)number, t->size + t->zoom, ui_role(UI_ON_SURFACE) & 0xffffffu, current ? 220 : 110, x, t->left, 0);
                        if (doc_folded_at(&t->doc, line, &hidden_to))
                                ui_chevron(ui, x + t->cell / 2, row_y + t->line_height / 2, t->cell / 3, ui_tone(ui_role(UI_ON_SURFACE) & 0xffffffu, 160));
                }
                if (!t->wrap || t->mode == UI_TEXT_FIELD)
                {
                        ui_text_draw_line(ui, t, &l, line, row_y, baseline, ground);
                        row_y += t->line_height;
                        continue;
                }
                // Wrapped: the line cut where it would pass the box's width, at a blank when one is near, each row
                // after the first indented as the line is.
                {
                        static struct ui_text_layout part;
                        p32 from = 0, room = (p32)(t->right - t->left);
                        int indent = 0;

                        while (indent < (int)l.count && (l.shown[l.shown_at[indent]] == ' '))
                                indent++;
                        indent = l.x[indent] < (int)room / 2 ? l.x[indent] : 0;
                        do
                        {
                                p32 to = from, blank = 0;
                                int offset_x = l.x[from] - (from ? indent : 0);

                                while (to < l.count && l.x[to + 1] - offset_x <= (int)room)
                                {
                                        if (l.shown[l.shown_at[to]] == ' ')
                                                blank = to + 1;
                                        to++;
                                }
                                if (to < l.count && blank > from)
                                        to = blank;
                                to = to > from ? to : from + 1;
                                to = to > l.count ? l.count : to;
                                part.count = to - from;
                                for (p32 k = 0; k <= part.count; k++)
                                {
                                        part.offset[k] = l.offset[from + k];
                                        part.x[k] = l.x[from + k] - offset_x;
                                        part.shown_at[k] = l.shown_at[from + k] - l.shown_at[from];
                                }
                                part.shown_length = l.shown_at[to] - l.shown_at[from];
                                memory_copy(part.shown, l.shown + l.shown_at[from], part.shown_length);
                                part.shown[part.shown_length] = 0;
                                ui_text_draw_line(ui, t, &part, line, row_y, row_y + (t->line_height - (int)m.cell_height) / 2 + (int)m.baseline, ground);
                                row_y += t->line_height;
                                from = to;
                        } while (from < l.count && row_y + t->line_height <= t->bottom);
                }
        }
}

/*
        A kit field (UI_INPUT) with a text widget behind it: the kit draws the well, the glow and the label, and asks
        this for what is inside it, the box being the well's text area. ui_control.edit names the widget.
*/
static fn ui_text_field_draw(struct ui address_to ui, struct ui_control address_to c, int left, int right, int top, int bottom)
{
        struct ui_text address_to t = c->edit;

        t->focused = c->focused;
        t->placeholder = c->items[0];
        ui_text_draw(ui, t, left, top, right - left, bottom - top, ui_role(UI_SURFACE_HIGH) & 0xffffffu);
}

#endif // MOONWATER_CANVAS_UI
