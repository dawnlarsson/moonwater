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
};

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
static unsigned int ui_over(unsigned int pixel, unsigned int under)
{
        unsigned int rest = 255 - (pixel >> 24), out = 0;

        if (!(pixel >> 24))
                return under;
        if (!rest)
                return pixel;
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                unsigned int channel = (pixel >> shift & 255) + ((under >> shift & 255) * rest + 127) / 255;

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
                out |= (((tone >> shift & 255) * cover + 127) / 255) << shift;
        return out;
}

static fn ui_dot(struct ui address_to ui, int x, int y, unsigned int tone)
{
        unsigned int address_to at;

        if (x < 0 || x >= ui->width || y < ui->top || y >= ui->bottom || y >= ui->height)
                return;
        at = ui->pixels + (unsigned long)y * ui->pitch + (unsigned long)x;
        address_to at = ui_over(tone, address_to at);
}

// The whole picture clear: the glass alone.
static fn ui_clear(struct ui address_to ui)
{
        memory_fill_u32_rect(ui->pixels, (positive)ui->pitch, (positive)ui->width, (positive)ui->height, 0);
}

static fn ui_fill(struct ui address_to ui, int x, int y, int width, int height, unsigned int tone)
{
        int x2 = x + width, y2 = y + height;

        x = x < 0 ? 0 : x;
        y = y < ui->top ? ui->top : y;
        x2 = x2 > ui->width ? ui->width : x2;
        y2 = y2 > ui->bottom ? ui->bottom : y2;
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
                for (int column = 0; column < width; column++)
                {
                        unsigned int cover = ui_inside(column, row, width, height, radius);

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
        A run of text drawn with its pen at x on the baseline in colour (alpha
        255 is opaque ink, less a fainter one), and its width answered. Only
        the columns from low to high are drawn: a field's text is cut at its
        well, and the rest of the run is not.
*/
static int ui_run(struct ui address_to ui, int x, int baseline, string_address text, int size, b32 bold,
                  unsigned int colour, unsigned int alpha, int low, int high)
{
        const font_face address_to faces[2] = {bold && ui->bold ? ui->bold : ui->regular, ui->regular};
        p32 em = (p32)(size * ui->scale) << 6;
        positive length = string_length(text);
        unsigned int scratch_pixel = 0;
        font_target nowhere = {&scratch_pixel, 1, 1, 1};
        int width = (int)(font_draw(ui->engine, faces, 2, (const p8 address_to)text, length, em, 0, &nowhere, 0, 0,
                                    0xffffffu, 0) >> 16);
        font_metrics metrics = font_metrics_at(faces[0], em);
        int above = metrics.baseline + 2 * ui->scale, rows = metrics.cell_height + 4 * ui->scale;
        int left = 2 * ui->scale, wide = width + 2 * left;
        b32 light = window_light(colour); // light ink: drawn white on black, so the coverage is the light text's own
        unsigned int paper = light ? 0 : 0xffffffu;

        if (!alpha || !ui->scratch)
                return width;
        wide = wide > ui->scratch_width ? ui->scratch_width : wide;
        rows = rows > UI_SCRATCH_ROWS * ui->scale ? UI_SCRATCH_ROWS * ui->scale : rows;

        memory_fill_u32_rect(ui->scratch, (positive)wide, (positive)wide, (positive)rows, paper);
        {
                font_target line = {ui->scratch, (positive)wide, wide, rows};

                font_draw(ui->engine, faces, 2, (const p8 address_to)text, length, em, 0, &line, (b32)left << 16, above,
                          light ? 0xffffffu : 0, paper);
        }

        // The coverage, as the ink at that alpha, over the picture.
        for (int row = 0; row < rows; row++)
                for (int column = 0; column < wide; column++)
                {
                        unsigned int value = ui->scratch[(unsigned long)row * (unsigned long)wide + (unsigned long)column] >> 8 & 255;
                        unsigned int cover = light ? value : 255 - value;
                        int at = x - left + column;

                        if (cover && at >= low && at < high)
                                ui_dot(ui, at, baseline - above + row, ui_tone(colour, cover * alpha / 255));
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

#endif // MOONWATER_CANVAS_UI
