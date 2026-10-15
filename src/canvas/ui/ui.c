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
        is a step or a choice (a chevron), the list a choice opens over the
        rows, a field to type in, and the ring round whatever has the focus. Rows
        stand on the glass with space between sections: no box and no rule,
        and the one panel is the list that floats over them. Sizes are logical pixels, times the scale
        the compositor draws at. Text is font.c's, Inter; a run is drawn as
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
                        cover = ui_inside(column, row, width, height, radius);

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
                        outer_cover = ui_inside(column, row, width, height, radius);
                        inner_cover = ui_inside(column - thickness, row - thickness, width - 2 * thickness,
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
        A field to type in, x, y, width by height: a faint well, ringed while
        it is typed in, the text at its left and, while typed in, the caret
        after the text.
*/
static fn ui_field(struct ui address_to ui, int x, int y, int width, int height, string_address text, b32 typing)
{
        int radius = (UI_RADIUS - 2) * ui->scale, pad = 8 * ui->scale;
        int baseline = ui_baseline(ui, y, height, UI_TEXT);
        int left = x + pad, right = x + width - pad, shift = 0, drawn;

        ui_round(ui, x, y, width, height, radius, ui_tone(ui->ink, typing ? 30 : 18));
        if (typing)
                ui_outline(ui, x, y, width, height, radius, UI_RING * ui->scale, ui->ring);
        // Typed in, a text wider than the well shows its end, the caret after it: the start is cut off at the well's edge.
        if (typing)
        {
                int measured = ui_measure(ui, text, UI_TEXT, false) + 2 * ui->scale;

                shift = measured > right - left ? measured - (right - left) : 0;
        }
        drawn = ui_run(ui, left - shift, baseline, text, UI_TEXT, false, ui->ink, 255, left, right);
        if (typing)
        {
                int capital = ui_capital(ui, UI_TEXT);

                ui_fill(ui, left - shift + drawn + ui->scale, baseline - capital - 2 * ui->scale, ui->scale,
                        capital + 4 * ui->scale, ui_tone(ui->ink, 255));
        }
}

/*
        One app's window and loop. The runner takes the window, sets its title,
        glass, scale and ground, and calls open. Then it runs the loop: events,
        closing, focus, ground, the size asked for by the hand, a due picture,
        the app's tick and its one descriptor, and the wait. An app keeps its
        state and its pictures; it does not repeat the loop.
*/
struct ui_app
{
        struct window_row address_to own;
        struct ui address_to ui;
        string_address missing;         // logged when the desktop has no window for it
        string_address title;
        unsigned int glass;
        b32 address_to dirty;           // a picture is due: frame paints it
        b32 address_to focused;         // the app's own: the keys are its
        unsigned int address_to ground; // the app's own: the window's ground, as the loop last saw it
        unsigned int asked_w, asked_h;  // the size the hand last asked for
        int (*open)(void);              // the app's own start; -1 runs the loop, else the status to leave with
        b32 (*frame)(void);             // paints the due picture; false: it is due again in 50 ms
        b32 (*event)(const struct window_event address_to event); // false: quit
        b32 (*focus)(b32 focused);      // the keys came or went, optional; false: quit
        void (*changed)(void);          // every row is due, after a ground change or a focus change
        void (*size)(unsigned int width, unsigned int height);     // the hand asked for this size
        long (*tick)(long timeout, long address_to other);         // the app's timer; 0 runs the loop again now; other: a descriptor
        void (*ready)(void);            // other is readable
        void (*close)(void);            // before the window is left
};

static int ui_app_run(struct ui_app address_to app)
{
        struct windows address_to table;
        unsigned int row;
        int status;
        b32 running = true;

        if (windows_take(app->own) < 0)
        {
                log_direct((address_any)app->missing, string_length((const_string)app->missing));
                return 1;
        }
        table = app->own->table;
        row = app->own->row;
        app->ui->scale = table->scale >= 1 && table->scale <= 8 ? (int)table->scale : 1;
        string_copy((string_address)table->title[row], app->title);
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

                while (running && windows_event(app->own, &event))
                        if (!app->event(&event))
                                running = false;

                if (table->state[row] & WINDOW_CLOSING)
                        running = false;

                if ((table->focus == row) != *app->focused)
                {
                        *app->focused = table->focus == row;
                        app->changed();
                        if (app->focus && !app->focus(*app->focused))
                                running = false;
                }
                if (table->ground[row] && table->ground[row] != *app->ground)
                {
                        *app->ground = table->ground[row];
                        ui_inks(app->ui, *app->ground);
                        app->changed();
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

#endif // MOONWATER_CANVAS_UI
