/*
        The system's own look: what a window Moonwater draws for itself is
        made of, so the settings and every window like it after them look
        like one system.

        A system window is glass (WINDOW_GLASS, window.c): its picture is
        premultiplied ARGB over what is under it, blurred and tinted by the
        compositor, which says what the tint came to as the window's ground.
        Everything here is drawn over that in the ground's own inks, taken
        from window_contrast so text stands out whatever is behind the
        glass:

            ink         text, the knob of a switch that is off, a switch on
            quiet       a value, a header: ink most of the way to the ground
            lit         the row with the focus: the ink at a seventh
            ring        round the row with the focus, and a field being typed in

        The pieces: a title, a section header, a row (a label at the left and
        its value, a switch or a choice at the right), a switch, a value that
        is a step or a choice (a chevron), the bars of a signal, the list a
        choice opens over the rows, a field to type in, and the ring round
        whatever has the focus. Rows stand on the glass with space between
        sections: no box and no rule, and the one panel is the list that
        floats over them. Sizes are logical pixels, times the scale the
        compositor draws at. Text is font.c's, Inter; a run is drawn as
        coverage into a scratch line and laid over the picture in the ink,
        so it is premultiplied like everything else and the glass shows
        between the strokes.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_UI
#define MOONWATER_CANVAS_UI

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

// The whole picture clear: the glass alone; within a repaint's band, the glass of its rows.
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
                                     (positive)(bottom - top), 0);
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
static int ui_run(struct ui address_to ui, int x, int baseline, string_address text, int size, b32 bold,
                  unsigned int colour, unsigned int alpha, int low, int high)
{
        const font_face address_to faces[2] = {bold && ui->bold ? ui->bold : ui->regular, ui->regular};
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
                                unsigned int cover = light ? value : 255 - value, bit = cover & 63;

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

// The window's title, at the top left, where the compositor's drag takes the window.
static fn ui_title(struct ui address_to ui, int x, int y, int height, string_address text)
{
        ui_text(ui, x, ui_baseline(ui, y, height, UI_TITLE), text, UI_TITLE, true, ui->ink, 255);
}

// A section's header: small and quiet, over its rows.
static fn ui_header(struct ui address_to ui, int x, int y, int height, string_address text)
{
        ui_text(ui, x, y + height - 8 * ui->scale, text, UI_SMALL, true, ui->quiet, 255);
}

// The row with the focus: lit, with a ring round it.
static fn ui_focus(struct ui address_to ui, int x, int y, int width, int height, b32 ring)
{
        int radius = (UI_RADIUS - 2) * ui->scale;

        ui_round(ui, x, y, width, height, radius, ui->lit);
        if (ring)
                ui_outline(ui, x, y, width, height, radius, UI_RING * ui->scale, ui->ring);
}

/*
        A switch, its right edge at right and its middle on middle: a pill,
        filled with the ink when on with a knob of the ground at the right,
        and faint when off with an ink knob at the left. A switch that cannot
        be thrown is drawn at half.
*/
static fn ui_switch(struct ui address_to ui, int right, int middle, b32 on, b32 enabled)
{
        int width = UI_SWITCH_W * ui->scale, height = UI_SWITCH_H * ui->scale, x = right - width,
            y = middle - height / 2, knob = height - 4 * ui->scale;
        unsigned int strength = enabled ? 255 : 110;

        ui_round(ui, x, y, width, height, height / 2,
                 on ? ui_tone(ui->ink, 235 * strength / 255) : ui_tone(ui->ink, 46 * strength / 255));
        if (!on)
                ui_outline(ui, x, y, width, height, height / 2, ui->scale, ui_tone(ui->ink, 70 * strength / 255));
        ui_round(ui, on ? x + width - 2 * ui->scale - knob : x + 2 * ui->scale, y + 2 * ui->scale, knob, knob, knob / 2,
                 on ? ui_tone(ui->ground, 255) : ui_tone(ui->ink, 230 * strength / 255));
}

// What a value says about itself: nothing, that Left and Right move it (a step), or that it opens a list (a choice).
#define UI_VALUE_PLAIN 0
#define UI_VALUE_STEP 1
#define UI_VALUE_LIST 2

/*
        A chevron pointing down, centred on cx and cy, size to a side: the
        mark of a value that opens a list of its choices.
*/
static fn ui_chevron(struct ui address_to ui, int cx, int cy, int size, unsigned int tone)
{
        int thick = ui->scale + 1;

        for (int step = 0; step <= size; step++)
        {
                ui_fill(ui, cx - size + step, cy - size / 2 + step, thick, ui->scale, tone);
                ui_fill(ui, cx + size - step - thick + 1, cy - size / 2 + step, thick, ui->scale, tone);
        }
}

/*
        A value at the right of a row, its right edge at right: quiet, and
        with marks that say what it is: a step has Left and Right marks
        round it, a choice a chevron after it. Answers how wide it came to.
*/
static int ui_value(struct ui address_to ui, int right, int baseline, string_address text, int marks, b32 enabled)
{
        unsigned int alpha = enabled ? 255 : 120;
        int width = ui_measure(ui, text, UI_TEXT, false), gap = 8 * ui->scale, mark = 0;

        if (marks == UI_VALUE_LIST)
        {
                int size = 4 * ui->scale;

                ui_chevron(ui, right - size, baseline - ui_capital(ui, UI_TEXT) / 2, size, ui_tone(ui->quiet, alpha));
                ui_text(ui, right - 2 * size - gap - width, baseline, text, UI_TEXT, false, ui->ink, alpha);
                return width + 2 * size + gap;
        }
        if (marks == UI_VALUE_STEP)
        {
                mark = ui_measure(ui, (string_address) "\xe2\x80\xba", UI_TEXT, true);
                ui_text(ui, right - mark, baseline, (string_address) "\xe2\x80\xba", UI_TEXT, true, ui->quiet, alpha);
                ui_text(ui, right - mark - gap - width - gap - mark, baseline, (string_address) "\xe2\x80\xb9", UI_TEXT, true,
                        ui->quiet, alpha);
                right -= mark + gap;
        }
        ui_text(ui, right - width, baseline, text, UI_TEXT, false, marks ? ui->ink : ui->quiet, alpha);
        return width + (marks ? 2 * (mark + gap) : 0);
}

/*
        The bars of a signal, its right edge at right and their middle on
        middle: four, as many lit as bars says and the rest faint.
*/
static fn ui_signal(struct ui address_to ui, int right, int middle, int bars)
{
        int width = 3 * ui->scale, gap = 2 * ui->scale, bottom = middle + 7 * ui->scale;

        for (int bar = 0; bar < 4; bar++)
        {
                int height = (4 + 3 * bar) * ui->scale;

                ui_round(ui, right - 4 * width - 3 * gap + bar * (width + gap), bottom - height, width, height, ui->scale,
                         ui_tone(ui->ink, bar < bars ? 235 : 55));
        }
}

/*
        The panel of a list that opens over the rows, x, y, width by height:
        the ground itself, so what is under it does not show through the
        text, lit a little and with a faint line round it.
*/
static fn ui_menu(struct ui address_to ui, int x, int y, int width, int height)
{
        int radius = UI_RADIUS * ui->scale;

        ui_round(ui, x, y, width, height, radius, ui_tone(ui->ground, 255));
        ui_round(ui, x, y, width, height, radius, ui_tone(ui->ink, 22));
        ui_outline(ui, x, y, width, height, radius, ui->scale, ui_tone(ui->ink, 70));
}

/*
        One choice of such a list, x, y, width by height: lit when it is the
        one the keys are on, a dot before it when it is the value now, its
        text after, cut to the band's end.
*/
static fn ui_option(struct ui address_to ui, int x, int y, int width, int height, string_address text, b32 lit, b32 current)
{
        int dot = 6 * ui->scale, inset = 12 * ui->scale;

        if (lit)
                ui_round(ui, x, y, width, height, (UI_RADIUS - 2) * ui->scale, ui_tone(ui->ink, 56));
        if (current)
                ui_round(ui, x + inset, y + (height - dot) / 2, dot, dot, dot / 2, ui_tone(ui->ink, 235));
        ui_run(ui, x + inset + dot + 10 * ui->scale, ui_baseline(ui, y, height, UI_TEXT), text, UI_TEXT, false, ui->ink, 255,
               x, x + width - 6 * ui->scale);
}

/*
        A run of text in a field, from left to right on baseline, at size. Typed
        in, a text wider than the well shows its end, the caret after it: the
        start is cut off at the well's edge.
*/
static fn ui_field_text(struct ui address_to ui, int left, int right, int baseline, string_address text, int size, b32 typing)
{
        int shift = 0, drawn;

        if (typing)
        {
                int measured = ui_measure(ui, text, size, false) + 2 * ui->scale;

                shift = measured > right - left ? measured - (right - left) : 0;
        }
        drawn = ui_run(ui, left - shift, baseline, text, size, false, ui->ink, 255, left, right);
        if (typing)
        {
                int capital = ui_capital(ui, size);

                ui_fill(ui, left - shift + drawn + ui->scale, baseline - capital - 2 * ui->scale, ui->scale,
                        capital + 4 * ui->scale, ui_tone(ui->ink, 255));
        }
}

/*
        A field to type in, x, y, width by height: a faint well, ringed while
        it is typed in, the text at its left and, while typed in, the caret
        after the text.
*/
static fn ui_field(struct ui address_to ui, int x, int y, int width, int height, string_address text, b32 typing)
{
        int radius = (UI_RADIUS - 2) * ui->scale, pad = 8 * ui->scale;
        int baseline = ui_baseline(ui, y, height, UI_TEXT);

        ui_round(ui, x, y, width, height, radius, ui_tone(ui->ink, typing ? 30 : 18));
        if (typing)
                ui_outline(ui, x, y, width, height, radius, UI_RING * ui->scale, ui->ring);
        ui_field_text(ui, x + pad, x + width - pad, baseline, text, UI_TEXT, typing);
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

static fn ui_press_fill(struct ui address_to ui, int x, int y, int width, int height)
{
        ui_round(ui, x, y, width, height, (UI_RADIUS - 2) * ui->scale, ui_tone(ui->ink, 40));
}

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
};

// A colour 0xaarrggbb as a tween's value: each channel premultiplied, 8.8.
static b64 ui_tween_colour(unsigned int argb)
{
        p64 a = argb >> 24, out = a << 56 | a << 48;

        for (int shift = 0; shift < 24; shift += 8)
                out |= (((p64)(argb >> shift & 255) * a * 256 + 127) / 255) << (shift * 2);
        return (b64)out;
}

// And back: 0xaarrggbb, not premultiplied, rounded.
static unsigned int ui_tween_argb(b64 value)
{
        p64 v = (p64)value, a = v >> 48 & 0xffff;
        unsigned int out = (unsigned int)((a + 128) >> 8) << 24;

        if (!a)
                return 0;
        for (int shift = 0; shift < 24; shift += 8)
        {
                p64 c = ((v >> (shift * 2) & 0xffff) * 256 + a / 2) / a;

                out |= (unsigned int)((c + 128) >> 8 > 255 ? 255 : (c + 128) >> 8) << shift;
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
        }
        string_copy_bounded((string_address)table->title[row], app->title, WINDOWS_TITLE);
        windows_name(app->own, (const char *)app->name);
        table->glass[row] = app->glass;
        *app->ground = table->ground[row];
        ui_inks(app->ui, *app->ground ? *app->ground : 0x101012u);
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
                if (table->want_width[row] && table->want_height[row] &&
                    (table->want_width[row] != app->asked_w || table->want_height[row] != app->asked_h))
                {
                        app->asked_w = table->want_width[row];
                        app->asked_h = table->want_height[row];
                        app->size(app->asked_w, app->asked_h);
                }

                if (!running)
                        break;

                if (*app->dirty && !app->frame())
                        timeout = 50000000;
                if (app->tick)
                {
                        timeout = app->tick(timeout, &other);
                        if (timeout == 0)
                                continue;
                }
                if (app->done && app->done())
                        break;

                ready = windows_wait(app->own, other, timeout);
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

#endif // MOONWATER_CANVAS_UI
