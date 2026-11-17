/*
        test/checks/compositor_hand.c: the compositor and the hand: a held button, the cursor, close buttons, popups
        and menus, dropdowns, the tray, a tab's reach, the launcher button, the pointer's motion, prediction, the
        wheel, volume, notices, the chrome.

        Rows of the compositor's pointer.c, input.c, manage.c and compose.c. One part of the program that the
        terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model kit's sections share (checks.c
        opens and closes it). The terminal's program, the kit's frames, the draw list and the GPU model's kit run with
        the four faces: fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

/*
        A held button (compositor_pointer_press): while any button is down the pointer is a little larger, by half the squeeze the
        settings give, and with none down it is its own size; it eases from one to the other over CP_SWAY_NS (compositor_sway_at) and
        does not jump. The setting off, or a squeeze of 0, leaves it its size. At every
        size the pixel the pointer is on is still a pixel of the shape's own hot cell.
*/
static void check_compositor_click(void)
{
    struct compositor_pointer p;
    struct compositor_sway sway = {0, 0, 0};
    int hot = 1, sizes[8] = {128, 192, 205, 256, 320, 384, 512, 576}, half;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.scale = 200;
    compositor.card = -1;
    compositor.cursor.squeeze = 25;

    compositor.cursor.click_off = 1;
    compositor_pointer_press(COMPOSITOR_BTN_LEFT, 1);
    check("press: off, a held button leaves the pointer its size", compositor.cursor.press.to == 0 && compositor.cursor.held == 1);
    compositor_pointer_press(COMPOSITOR_BTN_LEFT, 0);
    compositor.cursor.click_off = 0;
    compositor.cursor.squeeze = 0;
    compositor_pointer_press(COMPOSITOR_BTN_LEFT, 1);
    check("press: a squeeze of 0 leaves it its size too", compositor.cursor.press.to == 0);
    compositor_pointer_press(COMPOSITOR_BTN_LEFT, 0);
    compositor.cursor.squeeze = 25;

    compositor_pointer_press(COMPOSITOR_BTN_LEFT, 1);
    check("press: the left button held is on its way to an eighth larger, and has not jumped there",
          compositor.cursor.press.to == 32 && compositor.cursor.press.from == 0 && compositor.cursor.press.at != 0);
    compositor.cursor.press.at = 0; // arrived
    compositor_pointer_now(&p);
    check("press: there, a pointer of two pixels a cell is two and a quarter", p.scale == 2 * 288);
    compositor_pointer_press(COMPOSITOR_BTN_RIGHT, 1);
    check("press: the right button with the left is the same size", compositor.cursor.press.to == 32);
    compositor_pointer_press(COMPOSITOR_BTN_LEFT, 0);
    check("press: the right button alone is the same size too, every button the same click",
          compositor.cursor.press.to == 32);
    compositor_pointer_press(COMPOSITOR_BTN_RIGHT, 0);
    check("press: every button let go, it is on its way back to its own size", compositor.cursor.press.to == 0 && compositor.cursor.held == 0);
    compositor.cursor.press.at = 0;

    compositor_sway_to(&sway, 100, 1000);
    half = compositor_sway_at(&sway, 1000 + CP_SWAY_NS / 2);
    check("press: a value on its way starts where it was, is between at half its time, and is there and at rest when its time is up",
          compositor_sway_at(&sway, 1000) == 0 && half > 0 && half < 100 && sway.at == 1000 &&
              compositor_sway_at(&sway, 1000 + CP_SWAY_NS) == 100 && sway.at == 0);
    compositor_sway_to(&sway, 0, 5000);
    compositor_sway_to(&sway, 50, 5000 + CP_SWAY_NS / 2);
    check("press: sent elsewhere on its way, it leaves from where it had got to", sway.from == 100 - half && sway.to == 50);

    for (unsigned int shape = 0; shape < CP_SHAPES; shape++)
        for (unsigned int size = 0; size < sizeof(sizes) / sizeof(sizes[0]); size++)
        {
            unsigned char fill[CP_SPAN_MOST], edge[CP_SPAN_MOST];
            struct compositor_pointer at = {.shape = shape, .scale = sizes[size]};
            int hot_x, hot_y, span = compositor_pointer_span(&at, &hot_x, &hot_y);

            compositor_pointer_rows(&at, span, hot_x, hot_y, hot_y, 1, fill, edge, CP_SPAN_MOST);
            hot &= span <= CP_PLANE && hot_x < span && hot_y < span && (fill[hot_x] || edge[hot_x]);
        }
    check("press: at a half (50 percent), three quarters, four fifths (80 percent), once, one and a quarter (125 percent), one and a half, twice "
          "and two and a quarter times, the pixel the pointer is on is the shape's own, for every shape", hot);
}

/*
        The cursor style (compositor.c): classic draws the bitmap with its tip on the pointer; dynamic draws one filled shape and nothing
        in it, a circle of CP_DYN_RADIUS cells about the pointer whatever the state, a rounded rectangle one wide and three tall over
        text, and a lens along the way a window's edge moves over the edge; a fast hand stretches it along its motion and squashes it across, a slow one does not, and the stretch lets go; a touch
        is always dynamic; and the setting rows say dynamic and 25 when nothing is kept.
*/
static int settings_row_named(const char address_to label);

static void check_compositor_cursor_style(void)
{
    static unsigned int plane[CP_PLANE * CP_PLANE];
    static unsigned int wide[CP_PLANE * 2 * CP_PLANE];
    struct compositor_target t = {plane, CP_PLANE, CP_PLANE, CP_PLANE, 0, 0, CP_PLANE, CP_PLANE};
    struct compositor_target margin = {wide, CP_PLANE * 2, CP_PLANE, CP_PLANE, 0, 0, CP_PLANE, CP_PLANE};
    struct compositor_pointer p = {.shape = CP_ARROW, .scale = 256}, circle, other;
    const unsigned int fill = 0xffffffffu, edge = 0xff000000u;
    unsigned int sentinel = 0x5a5a5a5au;
    int row_at, column_at, sentinel_ok = 1, squeeze_row, style_row, touched, mouse, same = 1, hot_x, hot_y;

    compositor_zero();
    compositor.scale = 100;
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 16, 16, &p, fill, edge);
    check("cursor style: classic draws the arrow's bitmap with its tip on the pointer, the pixel right of the tip clear",
          plane[16 * 64 + 16] == edge && plane[17 * 64 + 16] == edge && plane[18 * 64 + 17] == fill && plane[16 * 64 + 17] == 0);

    // Every bitmap at once and twice its size: its own pixels and no others, each ink whole, inside its cell.
    for (unsigned int shape = 0; shape < CP_SHAPES; shape++)
        for (int size = 1; size <= 2; size++)
        {
            int fills = 0, edges = 0, drawn_fill = 0, drawn_edge = 0, others = 0;

            for (int at = 0; at < CP_CELL * CP_CELL; at++)
                fills += compositor_cursor_art[shape][at / CP_CELL][at % CP_CELL] == 'o', edges += compositor_cursor_art[shape][at / CP_CELL][at % CP_CELL] == '#';
            p = (struct compositor_pointer){.shape = shape, .scale = size * 256};
            compositor_pointer_span(&p, &hot_x, &hot_y);
            memory_zero(plane, sizeof(plane));
            compositor_cursor_draw(&t, hot_x, hot_y, &p, fill, edge);
            for (row_at = 0; row_at < CP_PLANE; row_at++)
                for (column_at = 0; column_at < CP_PLANE; column_at++)
                {
                    unsigned int pixel = plane[row_at * CP_PLANE + column_at];

                    drawn_fill += pixel == fill, drawn_edge += pixel == edge;
                    others += pixel && ((pixel != fill && pixel != edge) || row_at >= CP_CELL * size || column_at >= CP_CELL * size);
                }
            same &= drawn_fill == fills * size * size && drawn_edge == edges * size * size && !others && edges > 0;
        }
    check("cursor style: every classic shape at once and twice its size is its bitmap's own pixels, each ink whole and nothing between, inside its cell", same);

    /*
        At a scale between, nothing of a bitmap is left out: the middle of every cell with ink is a pixel of the picture's square, and that
        pixel has ink. The hot pixel's place was the cells before the hot cell rounded down, and at four fifths the square started a cell
        in, past the tip of the resize arrows (CP_RESIZE_H's first column, CP_MOVE's).
    */
    {
        static const int between[] = {128, 192, 205, 320, 384};
        int whole = 1;

        for (unsigned int shape = 0; shape < CP_SHAPES; shape++)
            for (unsigned int at = 0; at < sizeof(between) / sizeof(between[0]); at++)
            {
                unsigned char covers[2][CP_SPAN_MOST];
                int span;

                p = (struct compositor_pointer){.shape = shape, .scale = between[at]};
                span = compositor_pointer_span(&p, &hot_x, &hot_y);
                for (int cy = 0; cy < CP_CELL; cy++)
                    for (int cx = 0; cx < CP_CELL; cx++)
                    {
                        long long across = ((long long)(cx - compositor_cursor_hot[shape][0]) * 256 + 128) * between[at];
                        long long down = ((long long)(cy - compositor_cursor_hot[shape][1]) * 256 + 128) * between[at];
                        int px = hot_x + (int)(across >= 0 ? across / 65536 : -((-across + 65535) / 65536));
                        int py = hot_y + (int)(down >= 0 ? down / 65536 : -((-down + 65535) / 65536));

                        if (compositor_cursor_art[shape][cy][cx] == '.')
                            continue;
                        if (px < 0 || py < 0 || px >= span || py >= span)
                        {
                            whole = 0;
                            continue;
                        }
                        compositor_pointer_rows(&p, span, hot_x, hot_y, py, 1, covers[0], covers[1], CP_SPAN_MOST);
                        whole &= covers[0][px] || covers[1][px];
                    }
            }
        check("cursor style: at a half, three quarters, four fifths, one and a quarter and one and a half times, every cell of a bitmap with ink is in its square and drawn",
              whole);
    }

    /*
        Below a whole scale a line of one cell is a pixel's worth of its ink: the arrow's outline down its left side, a cell wide, is a
        whole pixel of the edge ink at four fifths and at a half (it was 80 percent of one at four fifths), and the dynamic circle's line
        across its outline right of its middle is a pixel's worth at a half (it was half of one).
    */
    {
        unsigned char covers[2][CP_SPAN_MOST];
        int lined = 1, span, ink = 0;

        for (int scale = 128; scale <= 205; scale += 77)
        {
            p = (struct compositor_pointer){.shape = CP_ARROW, .scale = scale};
            span = compositor_pointer_span(&p, &hot_x, &hot_y);
            for (int row = 1; row < 12 * scale / 256; row++)
            {
                compositor_pointer_rows(&p, span, hot_x, hot_y, row, 1, covers[0], covers[1], CP_SPAN_MOST);
                lined &= covers[1][0] == 255;
            }
        }
        p = (struct compositor_pointer){.dyn = 1, .scale = 128, .hw = 8 * 256, .hh = 8 * 256, .c = 8 * 256};
        span = compositor_pointer_span(&p, &hot_x, &hot_y);
        compositor_pointer_rows(&p, span, hot_x, hot_y, hot_y, 1, covers[0], covers[1], CP_SPAN_MOST);
        for (int column = hot_x; column < span; column++)
            ink += covers[1][column];
        check("cursor style: below a whole scale the arrow's outline, a cell wide, is a whole pixel of its ink at four fifths and a half", lined);
        check("cursor style: and the dynamic circle's line at a half its size is a pixel's worth of its ink", ink >= 240 && ink <= 270);
    }
    check("cursor style: the hand's pointer is its fingertip and the arrow's its tip, the top of each",
          compositor_cursor_hot[CP_HAND][0] == 5 && compositor_cursor_hot[CP_HAND][1] == 0 && compositor_cursor_art[CP_HAND][0][5] == '#' &&
              compositor_cursor_art[CP_HAND][0][4] == '.' && compositor_cursor_art[CP_ARROW][0][0] == '#' && compositor_cursor_art[CP_ARROW][0][1] == '.');
    same = 1;

    compositor.cursor.style = 1;
    compositor.cursor.dyn = 1;
    compositor_pointer_now(&circle);
    check("cursor style: dynamic is a circle of CP_DYN_RADIUS cells with the pointer at the middle of its cell",
          circle.dyn && circle.hw == 8 * 256 && circle.hh == 8 * 256 && circle.c == 8 * 256 && !circle.grow &&
              compositor_pointer_span(&circle, &hot_x, &hot_y) == CP_DYN_CELL && hot_x == CP_DYN_CELL / 2 && hot_y == CP_DYN_CELL / 2);
    for (unsigned int shape = 0; shape < CP_SHAPES; shape++)
        if (shape != CP_TEXT && (shape < CP_RESIZE_H || shape > CP_RESIZE_NESW))
        {
            compositor.cursor.shape = shape;
            compositor_pointer_now(&other);
            same &= !memory_compare(&other, &circle, sizeof(circle));
        }
    check("cursor style: and the same one filled shape whatever the state but text and a window's edge, with no mark in it", same);
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 32, 32, &circle, fill, edge);
    check("cursor style: the circle is all fill inside, its line at 8 from the middle in the edge ink in four directions, and clear past it "
          "and at the square's corner",
          plane[32 * 64 + 32] == fill && plane[32 * 64 + 38] == fill && plane[36 * 64 + 36] == fill && (plane[32 * 64 + 40] >> 24) &&
              !(plane[32 * 64 + 40] & 0xffffffu) && (plane[32 * 64 + 24] >> 24) && (plane[24 * 64 + 32] >> 24) && (plane[40 * 64 + 32] >> 24) &&
              plane[32 * 64 + 41] == 0 && plane[32 * 64 + 23] == 0 && plane[39 * 64 + 39] == 0);

    compositor.cursor.shape = CP_TEXT;
    compositor_pointer_now(&other);
    check("cursor style: over text the shape is on its way to the rectangle", compositor.cursor.morph.to == 256 && compositor.cursor.morph.at != 0);
    compositor.cursor.morph.at = 0;
    compositor_pointer_now(&other);
    check("cursor style: which is taller than a terminal cell, one wide and three tall, with round corners",
          other.hw == 4 * 256 && other.hh == 12 * 256 && other.c == 2 * 256);
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 32, 32, &other, fill, edge);
    check("cursor style: it is filled, 4 from the middle across and 12 up and down, clear past that and at its corner",
          plane[32 * 64 + 32] == fill && plane[42 * 64 + 34] == fill && (plane[32 * 64 + 36] >> 24) && plane[32 * 64 + 37] == 0 &&
              (plane[44 * 64 + 32] >> 24) && plane[45 * 64 + 32] == 0 && plane[44 * 64 + 36] == 0 && (plane[20 * 64 + 32] >> 24) &&
              plane[19 * 64 + 32] == 0);
    compositor.cursor.stretch = (struct compositor_sway){CP_DYN_STRETCH, CP_DYN_STRETCH, 0};
    compositor.cursor.ux = 0, compositor.cursor.uy = 64;
    compositor_pointer_now(&other);
    check("cursor style: a fast hand over text stretches the rectangle no longer than its square",
          other.grow > 0 && other.hh * (256 + other.grow) / 256 <= CP_DYN_CELL / 2 * 256);
    compositor.cursor.stretch = (struct compositor_sway){0, 0, 0};

    compositor.cursor.shape = CP_RESIZE_H;
    compositor_pointer_now(&other);
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 32, 32, &other, fill, edge);
    check("cursor style: over a window's side edge the shape is a lens along it, filled, 10 out to either point and 5 thick, round above "
          "and below and pointed at its ends",
          other.lens && !other.grow && plane[32 * 64 + 32] == fill && plane[35 * 64 + 32] == fill && (plane[32 * 64 + 41] >> 24) &&
              plane[32 * 64 + 43] == 0 && (plane[32 * 64 + 23] >> 24) && plane[32 * 64 + 21] == 0 && (plane[37 * 64 + 32] >> 24) &&
              plane[38 * 64 + 32] == 0 && plane[35 * 64 + 40] == 0 && plane[29 * 64 + 24] == 0);
    {
        int size = other.scale;

        compositor.cursor.squeeze = 25;
        compositor_pointer_press(COMPOSITOR_BTN_LEFT, 1);
        compositor.cursor.press.at = 0;
        compositor_pointer_now(&other);
        check("cursor style: a button held over a window's edge leaves the lens its size, nothing eased", other.scale == size);
        compositor_pointer_press(COMPOSITOR_BTN_LEFT, 0);
        compositor.cursor.press = (struct compositor_sway){0, 0, 0};
        compositor.cursor.style = 0, compositor.cursor.dyn = 0;
        compositor_pointer_press(COMPOSITOR_BTN_LEFT, 1);
        compositor.cursor.press.at = 0;
        compositor_pointer_now(&other);
        check("cursor style: and the classic resize arrow its size too", other.scale == compositor_cursor_size());
        compositor_pointer_press(COMPOSITOR_BTN_LEFT, 0);
        compositor.cursor.press = (struct compositor_sway){0, 0, 0};
        compositor.cursor.style = 1, compositor.cursor.dyn = 1;
    }
    compositor.cursor.shape = CP_RESIZE_V;
    compositor_pointer_now(&other);
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 32, 32, &other, fill, edge);
    check("cursor style: over a top or bottom edge it is the lens stood up", (plane[41 * 64 + 32] >> 24) && plane[43 * 64 + 32] == 0 &&
                                                                                   (plane[32 * 64 + 37] >> 24) && plane[32 * 64 + 38] == 0);
    compositor.cursor.shape = CP_RESIZE_NWSE;
    compositor_pointer_now(&other);
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 32, 32, &other, fill, edge);
    check("cursor style: and over a corner it lies along the diagonal", plane[36 * 64 + 36] == fill && plane[26 * 64 + 38] == 0);
    compositor.cursor.shape = CP_ARROW;
    compositor.cursor.morph = (struct compositor_sway){0, 0, 0};

    other = circle;
    other.grow = CP_DYN_STRETCH, other.ux = 64, other.uy = 0;
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, 32, 32, &other, fill, edge);
    check("cursor style: stretched its most it is 12 cells out along the motion and not 14, and 5 across it and not 6",
          (plane[32 * 64 + 44] >> 24) && plane[32 * 64 + 46] == 0 && (plane[37 * 64 + 32] >> 24) && plane[38 * 64 + 32] == 0);

    for (unsigned int at = 0; at < CP_PLANE * 2 * CP_PLANE; at++)
        wide[at] = sentinel;
    compositor_cursor_draw(&margin, 0, 0, &other, fill, edge);
    compositor_cursor_draw(&margin, 63, 63, &other, fill, edge);
    for (row_at = 0; row_at < CP_PLANE; row_at++)
        for (column_at = CP_PLANE; column_at < CP_PLANE * 2; column_at++)
            sentinel_ok &= wide[row_at * CP_PLANE * 2 + column_at] == sentinel;
    check("cursor style: a stretched shape at the plane's edge is clipped to the plane: nothing is written past its width", sentinel_ok);

    compositor.cursor.last_ns = 1000000000ull;
    compositor.pointer_x = 10;
    compositor_cursor_moved(1000000000ull + 10000000ull);
    check("cursor style: a slow hand (1000 pixels a second) stretches nothing", compositor.cursor.stretch.to == 0 && compositor.cursor.last_x == 10);
    compositor.pointer_x = 60;
    compositor_cursor_moved(1000000000ull + 12000000ull);
    check("cursor style: a motion sooner than a sample's time is counted with the next", compositor.cursor.stretch.to == 0 && compositor.cursor.last_x == 10);
    compositor.pointer_x = 110;
    compositor_cursor_moved(1000000000ull + 20000000ull);
    check("cursor style: a fast hand (10000 pixels a second) stretches it its most, along the way it went",
          compositor.cursor.stretch.to == 256 && compositor.cursor.ux == 64 && compositor.cursor.uy == 0);
    compositor.pointer_y = 30;
    compositor.pointer_x = 110 - 40;
    compositor_cursor_moved(1000000000ull + 30000000ull);
    check("cursor style: and one between (5000) part of the way, along its own way",
          compositor.cursor.stretch.to == (5000 - CP_DYN_SLOW) * 256 / (CP_DYN_FAST - CP_DYN_SLOW) && compositor.cursor.ux == -51 && compositor.cursor.uy == 38);
    compositor_cursor_tick(1000000000ull + 30000000ull + CP_DYN_IDLE_NS - 1);
    same = compositor.cursor.stretch.to != 0;
    compositor_cursor_tick(1000000000ull + 30000000ull + CP_DYN_IDLE_NS);
    check("cursor style: the stretch lets go when nothing has moved for CP_DYN_IDLE_NS, and not before", same && compositor.cursor.stretch.to == 0);
    compositor.cursor.stretch = (struct compositor_sway){0, 0, 0};
    check("cursor style: and with every value at rest the pointer asks for no time of its own", compositor_cursor_due() == 0);

    compositor.touch.hidden = 1;
    compositor.cursor.style = 0;
    touched = compositor_pointer_dynamic() == 1;
    compositor.touch.hidden = 0;
    mouse = compositor_pointer_dynamic() == 0;
    check("cursor style: a touch was the last input and draws the dynamic cursor whatever the style; a mouse draws the style's", touched && mouse);

    squeeze_row = settings_row_named("Pointer squeeze (percent, or default)");
    style_row = settings_row_named("Cursor style");
    check("cursor style: the style row is dynamic when nothing is kept, and the squeeze row is 25 percent",
          style_row >= 0 && squeeze_row >= 0 && string_equals((string_address)settings_rows[style_row].standard, (string_address)"dynamic") &&
              string_equals((string_address)settings_rows[squeeze_row].standard, (string_address)"25"));
}

/*
        The pointer's picture as it was drawn before its covers were a table: compositor_pointer_row, a row at a time with its divides
        a pixel, and the draw that mixed each row's fill and edge by memory_mix_u32_row. The table (compositor_pointer_cover), the rows
        made without a divide a pixel (compositor_pointer_rows) and the draw from them (compositor_cursor_draw, by
        memory_mix_const_u32_rect) are held to it bit for bit.
*/
static void pointer_row_reference(const struct compositor_pointer *p, int row, int span, int hot_x, int hot_y, unsigned char *fill, unsigned char *edge)
{
    int n = p->dyn ? 1 : p->scale < 4 * 256 ? 4 : 2, all = n * n;
    long long step = (32768ll << 16) / (all * p->scale);
    long long ox = hot_x * 65536ll / p->scale + (p->dyn ? 128 : -256 * compositor_cursor_hot[p->shape][0]);
    long long oy = hot_y * 65536ll / p->scale + (p->dyn ? 128 : -256 * compositor_cursor_hot[p->shape][1]);

    for (int column = 0; column < span; column++)
    {
        int inks[3] = {0, 0, 0};

        if (p->dyn)
        {
            compositor_dyn_cover(p, ((2ll * column + 1) * step >> 16) - ox, ((2ll * row + 1) * step >> 16) - oy, &fill[column], &edge[column]);
            continue;
        }
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++)
            {
                long long u = ((2ll * ((column * n + i) * n + j) + 1) * step >> 16) - ox, v = ((2ll * ((row * n + j) * n + i) + 1) * step >> 16) - oy;
                char ink = u >= 0 && v >= 0 && u < CP_CELL * 256 && v < CP_CELL * 256 ? compositor_cursor_art[p->shape][v >> 8][u >> 8] : '.';

                inks[ink == '#' ? 2 : ink == 'o']++;
            }
        if (p->scale < 256)
        {
            int lined = inks[2] * 255 * 256 / (all * p->scale), filled = inks[1] * 255 * 256 / (all * p->scale);

            lined = lined < 255 ? lined : 255;
            filled = filled < 255 - lined ? filled : 255 - lined;
            edge[column] = (unsigned char)lined;
            fill[column] = (unsigned char)(filled ? filled * 255 / (255 - lined) : 0);
            continue;
        }
        edge[column] = (unsigned char)(inks[2] * 255 / all);
        fill[column] = (unsigned char)(inks[1] ? inks[1] * 255 / (all - inks[2]) : 0);
    }
}

// The draw a row at a time, each row's covers the reference's (made once a picture into covers, the fill's span rows then the edge's).
static void cursor_draw_reference(const struct compositor_target *t, int x, int y, const struct compositor_pointer *p, unsigned int fill, unsigned int edge,
                                  const unsigned char *made)
{
    unsigned int inks[2][CP_SPAN_MOST];
    int hot_x, hot_y, span = compositor_pointer_span(p, &hot_x, &hot_y), left = x - hot_x, top = y - hot_y;
    int x1 = left > t->x1 ? left : t->x1, x2 = left + span < t->x2 ? left + span : t->x2;
    int y1 = top > t->y1 ? top : t->y1, y2 = top + span < t->y2 ? top + span : t->y2;

    if (x2 <= x1 || y2 <= y1)
        return;
    memory_fill_u32_rect(inks[0], CP_SPAN_MOST, (unsigned long)(x2 - x1), 1, fill & 0xffffffu);
    memory_fill_u32_rect(inks[1], CP_SPAN_MOST, (unsigned long)(x2 - x1), 1, edge & 0xffffffu);
    for (int row = y1; row < y2; row++)
    {
        unsigned int *pixels = t->pixels + (unsigned long)(row - t->oy) * t->pitch + (unsigned long)(x1 - t->ox);
        const unsigned char *covers[2] = {made + (unsigned long)(row - top) * span, made + (unsigned long)span * span + (unsigned long)(row - top) * span};

        for (int ink = 0; ink < 2; ink++)
            memory_mix_u32_row(pixels, inks[ink], covers[ink] + (x1 - left), (positive)(x2 - x1));
        for (int at = x1 - left; (fill >> 24) && at < x2 - left; at++)
            pixels[at - (x1 - left)] |= (covers[1][at] + (covers[0][at] * (255u - covers[1][at]) + 127) / 255) << 24;
    }
}

/*
        Every picture the settings make, at every size the slider gives (50 to 300 percent in steps of 5) at a desktop of 100 and of
        200 and shaken at 100, and pressed (a quarter smaller): each classic shape, the dynamic circle, its rectangle over text and the
        circle stretched its most. Its covers, made once into the table and read by the GPU's tables, are compositor_pointer_row's, and
        drawn over a ground of every colour, part of it past the target's edge, in both looks and as the plane's picture with its
        alpha, the pixels are what the row at a time drew. A band that finds the table being made draws from its own rows, the same
        pixels; and a picture drawn after another is its own and not the table's last.
*/
#define POINTER_TABLE_SIDE (CP_SPAN_MOST + 32)
static void check_compositor_pointer_table(void)
{
    static unsigned int ground[POINTER_TABLE_SIDE * POINTER_TABLE_SIDE], got[POINTER_TABLE_SIDE * POINTER_TABLE_SIDE],
        want[POINTER_TABLE_SIDE * POINTER_TABLE_SIDE];
    static unsigned char reference[2 * CP_SPAN_MOST * CP_SPAN_MOST];
    static const int desktop[][2] = {{1, 0}, {2, 0}, {1, 1}}; // whole pixels a logical one, shaken
    static const unsigned int inks[3][2] = {{0xffffffu, 0u}, {0u, 0xffffffu}, {0xffffffffu, 0xff000000u}};
    unsigned long pictures = 0, covers_wrong = 0, pixels_wrong = 0, own_wrong = 0, tables = 0, owns = 0;

    for (unsigned int at = 0; at < POINTER_TABLE_SIDE * POINTER_TABLE_SIDE; at++)
        ground[at] = (at * 2654435761u) ^ (at << 7);
    for (unsigned int d = 0; d < sizeof(desktop) / sizeof(desktop[0]); d++)
        for (int percent = 50; percent <= 300; percent += 5)
            for (unsigned int kind = 0; kind < CP_SHAPES + 3; kind++)
                for (int pressed = 0; pressed < 2; pressed++)
                {
                    int base = (desktop[d][0] * 256 * percent + 50) / 100, scale = desktop[d][1] ? 3 * base : base, hot_x, hot_y, span;
                    struct compositor_pointer p = {.shape = kind < CP_SHAPES ? kind : 0, .dyn = kind >= CP_SHAPES};
                    const unsigned char *table;

                    scale = pressed ? scale * 192 / 256 : scale;
                    if (p.dyn)
                    {
                        p.hw = p.hh = p.c = CP_DYN_RADIUS * 256;
                        if (kind == CP_SHAPES + 1)
                            p.hw = CP_DYN_TEXT_W * 256, p.hh = CP_DYN_TEXT_H * 256, p.c = CP_DYN_TEXT_R * 256;
                        if (kind == CP_SHAPES + 2)
                            p.grow = CP_DYN_STRETCH, p.ux = -51, p.uy = 38;
                        scale = scale < CP_SPAN_MOST / CP_DYN_CELL * 256 ? scale : CP_SPAN_MOST / CP_DYN_CELL * 256;
                    }
                    else
                        scale = scale < CP_SPAN_MOST / CP_CELL * 256 ? scale : CP_SPAN_MOST / CP_CELL * 256;
                    p.scale = scale;
                    span = compositor_pointer_span(&p, &hot_x, &hot_y);
                    for (int row = 0; row < span; row++)
                        pointer_row_reference(&p, row, span, hot_x, hot_y, reference + (unsigned long)row * span,
                                              reference + (unsigned long)span * span + (unsigned long)row * span);
                    table = compositor_pointer_cover(&p);
                    pictures++;
                    tables += table != 0;
                    covers_wrong += !table || memory_compare(table, reference, 2 * (positive)span * (positive)span) != 0;
                    // Drawn a third of it past the target's top left, so the clip cuts it, in each look and as the plane's picture.
                    for (unsigned int look = 0; look < 3; look++)
                    {
                        struct compositor_target t = {got, POINTER_TABLE_SIDE, POINTER_TABLE_SIDE, POINTER_TABLE_SIDE, 0, 0, POINTER_TABLE_SIDE,
                                                      POINTER_TABLE_SIDE, 0, 0, 0};
                        struct compositor_target r = t;
                        int x = hot_x - span / 3, y = hot_y - span / 3;

                        r.pixels = want;
                        memory_copy(got, ground, sizeof(ground));
                        memory_copy(want, ground, sizeof(ground));
                        compositor_cursor_draw(&t, x, y, &p, inks[look][0], inks[look][1]);
                        cursor_draw_reference(&r, x, y, &p, inks[look][0], inks[look][1], reference);
                        pixels_wrong += memory_compare(got, want, sizeof(got)) != 0;
                        // As a band that finds the table being made, at every twenty fifth percent: its own rows, the same pixels.
                        if (percent % 25)
                            continue;
                        memory_copy(got, ground, sizeof(ground));
                        __atomic_store_n(&compositor_pointer_table.busy, 1, __ATOMIC_RELEASE);
                        __atomic_store_n(&compositor_pointer_table.made, 0, __ATOMIC_RELEASE);
                        compositor_cursor_draw(&t, x, y, &p, inks[look][0], inks[look][1]);
                        __atomic_store_n(&compositor_pointer_table.busy, 0, __ATOMIC_RELEASE);
                        own_wrong += memory_compare(got, want, sizeof(got)) != 0;
                        owns++;
                    }
                }
    check("pointer table: every picture the slider makes (50 to 300 percent, three desktops, pressed, every shape) is made into the table",
          pictures == 3 * 51 * (CP_SHAPES + 3) * 2 && tables == pictures);
    check("pointer table: its covers, the fill's and then the edge's, are compositor_pointer_row's, bit for bit", covers_wrong == 0);
    check("pointer table: drawn from it over a ground of every colour and cut by the target, in both looks and as the plane's, the pixels are the row at a time's",
          pixels_wrong == 0);
    check("pointer table: and a band that finds it being made draws the same pixels from its own rows", owns > 0 && own_wrong == 0);
    {
        struct compositor_pointer arrow = {.shape = CP_ARROW, .scale = 512}, hand = {.shape = CP_HAND, .scale = 512};
        const unsigned char *first = compositor_pointer_cover(&arrow), *again, *other;
        unsigned char was = first ? first[1] : 0;
        int hot_x, hot_y, span = compositor_pointer_span(&hand, &hot_x, &hot_y);

        // A mark in the table, which it keeps while the picture asked for is the one it holds, and loses when another is made.
        if (first)
            ((unsigned char *)first)[1] = (unsigned char)(was ^ 0x5a);
        again = compositor_pointer_cover(&arrow);
        check("pointer table: a picture asked for again is the table as it was, not made again", first && again == first && again[1] == (unsigned char)(was ^ 0x5a));
        other = compositor_pointer_cover(&hand);
        for (int row = 0; row < span; row++)
            pointer_row_reference(&hand, row, span, hot_x, hot_y, reference + (unsigned long)row * span,
                                  reference + (unsigned long)span * span + (unsigned long)row * span);
        check("pointer table: and another picture is made anew, its own covers", other && !memory_compare(other, reference, 2 * (positive)span * (positive)span));
        again = compositor_pointer_cover(&arrow);
        check("pointer table: and the first, asked for after it, is made again without the mark", again && again[1] == was);
    }
}

/*
        A window that asked for no close button (WINDOW_NO_CLOSE) has none drawn or hit, and one that asked to be put
        away by a press outside it (WINDOW_DISMISS) is asked to go by a press on the desktop or on another window, and
        by none inside it.
*/
static void check_compositor_dismiss(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct compositor_frame frame;
    int bx, by, side;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("dismiss: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS | WINDOW_NO_CLOSE | WINDOW_DISMISS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    table->x[2] = 180;
    table->y[2] = 140;
    table->width[2] = 100;
    table->height[2] = 60;
    glass_paint(2, 0, 0, 100, 60, 0x404040u, true);
    compositor_present();
    compositor_focus(1);

    compositor_frame_of(1, &frame);
    check("dismiss: a window that asked for none has no close button", !compositor_close_box(&frame, &bx, &by, &side));
    compositor_frame_of(2, &frame);
    check("dismiss: and another has one", compositor_close_box(&frame, &bx, &by, &side));

    compositor.pointer_x = 60;
    compositor.pointer_y = 40;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("dismiss: a press on the window is not asked to go", !(table->state[1] & WINDOW_CLOSING));
    compositor.pointer_x = 165;
    compositor.pointer_y = 35;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("dismiss: nor is one on the corner where a close button would be", !(table->state[1] & WINDOW_CLOSING));
    compositor.pointer_x = 5;
    compositor.pointer_y = 5;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("dismiss: a press on the empty desktop asks it to go", (table->state[1] & WINDOW_CLOSING) && compositor.focus == 1);

    table->state[1] = compositor.state[1] = 0;
    compositor_focus(1);
    compositor.pointer_x = 200;
    compositor.pointer_y = 160;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("dismiss: and so does one on another window, which has the keys then",
          (table->state[1] & WINDOW_CLOSING) && !(table->state[2] & WINDOW_CLOSING) && compositor.focus == 2);

    table->state[1] = compositor.state[1] = 0;
    compositor_focus(2);
    compositor.pointer_x = 5;
    compositor.pointer_y = 5;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("dismiss: a window that did not ask is not asked to go by a press elsewhere", !(table->state[1] & WINDOW_CLOSING) && !(table->state[2] & WINDOW_CLOSING));
}

/*
        The close button is the compositor's, drawn over the window's top right corner: a press on it closes the window even
        where the window's own kit shows a control (a hand over a button, an I-beam over a field) under it, as the minimise
        button does. The top of a window is its titlebar only where the kit shows no control there.
*/
static void check_compositor_close_over_control(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct compositor_frame frame;
    int bx, by, side;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("close over a control: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[2] = 50;
    table->y[2] = 30;
    table->width[2] = 120;
    table->height[2] = 80;
    glass_paint(2, 0, 0, 120, 80, 0x404040u, true);
    compositor_present();
    compositor_focus(2);
    compositor_frame_of(2, &frame);
    if (!compositor_close_box(&frame, &bx, &by, &side))
    {
        check("close over a control: the window has a close button", false);
        return;
    }
    table->pointer_shape[2] = WINDOW_POINTER_HAND;
    compositor.pointer_x = bx + side / 2;
    compositor.pointer_y = by + side / 2;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("close over a control: a press on the X closes the window, though the kit shows a hand under it", (table->state[2] & WINDOW_CLOSING) != 0);
}

/*
        A popup's keys. While it is open, the topmost popup of the window with the keys has them. Once a press has asked
        it to go it has none, and the window has them; and a popup whose window went is nobody's, so the window that
        takes its row has the keys and not the popup still on the screen.
*/
static void check_compositor_popup(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;

    // compositor_popup_parent is no part of the compositor struct: it is cleared here and after, so no popup is left for
    // a later section to take a row of the compositor for one.
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("popup keys: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);

    // A popup of row 3 for row 1 (popup_parent is the row + 1), 10 pixels in from the window's top left, 60 by 40.
    table->popup_parent[3] = 2;
    table->popup_x[3] = 10;
    table->popup_y[3] = 10;
    table->popup_w[3] = 0;
    table->popup_h[3] = 0;
    table->popup_flags[3] = WINDOW_POPUP_POINT;
    table->width[3] = 60;
    table->height[3] = 40;
    table->glass[3] = WINDOW_NO_CLOSE;
    glass_paint(3, 0, 0, 60, 40, 0x303030, true);
    compositor_present();
    check("popup keys: a popup of the window with the keys has them while it is open",
          compositor_popup_parent[3] == 1 && compositor_popup_top(1) == 3);

    compositor.pointer_x = 5;
    compositor.pointer_y = 5;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.press = COMPOSITOR_NONE;
    check("popup keys: a press outside it asks it to go", (table->state[3] & WINDOW_CLOSING) != 0);
    check("popup keys: a popup asked to go has none, and the window has them", compositor_popup_top(1) == 1 && compositor.focus == 1);

    // The window's door goes while the popup asked to go is still on the screen: its link is cut, and the keys go to the window
    // or to nothing, never to the popup on its way out.
    compositor_popup_leave(1);
    compositor_refocus();
    check("popup keys: a popup asked to go, still on the screen when its window's door goes, is given no keys", compositor.focus != 3);

    // The window goes (its program hung up) while that popup is still on the screen, and a new window takes its row.
    compositor_popup_leave(1);
    compositor_stack_remove(1);
    compositor.showing[1] = 0;
    compositor.focus = COMPOSITOR_NONE;
    table->asked[1] = 1;
    table->x[1] = 60;
    table->y[1] = 40;
    table->width[1] = 100;
    table->height[1] = 70;
    glass_paint(1, 0, 0, 100, 70, 0x202020, true);
    compositor_present();
    check("popup keys: a window that takes the row of a popup's window has the keys, not the popup",
          compositor.focus == 1 && compositor_popup_top(1) == 1);
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        A press outside an open popup asks it to go and goes no further: not to the window beneath it, the desktop or a tab,
        by a mouse or by a finger alike (touch). A press on the popup itself still reaches the popup.
*/
static void check_compositor_popup_press(b32 touch)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("popup press: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->state[1] = table->state[3] = 0;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);

    // A popup of row 3 for row 1: 10 pixels in from the window's top left, so on the screen 60 to 120 by 40 to 80.
    table->popup_parent[3] = 2;
    table->popup_x[3] = 10;
    table->popup_y[3] = 10;
    table->popup_w[3] = 0;
    table->popup_h[3] = 0;
    table->popup_flags[3] = WINDOW_POPUP_POINT;
    table->width[3] = 60;
    table->height[3] = 40;
    table->glass[3] = WINDOW_NO_CLOSE;
    glass_paint(3, 0, 0, 60, 40, 0x303030, true);
    compositor_present();

    compositor.touching = touch;
    compositor.pointer_x = 70;
    compositor.pointer_y = 50;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    if (touch)
        check("popup press: a finger on the popup still reaches the popup", compositor.grab == 3);
    else
        check("popup press: a press on the popup still reaches the popup", compositor.grab == 3);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);

    // Inside window 1 (50 to 170 by 30 to 110) and well outside the popup and its edge grip: the popup is asked to go and
    // the press is its window's no further. (Just outside the popup's edge is the popup's own grip, not the window's.)
    compositor.pointer_x = 150;
    compositor.pointer_y = 100;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    if (touch)
    {
        check("popup press: a finger outside an open popup asks it to go", (table->state[3] & WINDOW_CLOSING) != 0);
        check("popup press: and it does not reach the window beneath (no grab, no focus change)",
              compositor.grab == COMPOSITOR_NONE && compositor.focus == 1);
    }
    else
    {
        check("popup press: a mouse press outside an open popup asks it to go", (table->state[3] & WINDOW_CLOSING) != 0);
        check("popup press: and it does not reach the window beneath (no grab, no focus change)",
              compositor.grab == COMPOSITOR_NONE && compositor.focus == 1);
    }
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.touching = 0;
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        A menu opened, shown, closed and opened again on the row a program took (ui_popmenu_shows, with the row taken the way
        windows_take takes it): every open's picture is the menu drawn whole, and is the one the compositor places as a popup
        of its window, at the size the menu is. Every other open moves the hot item before the compositor has taken the first
        picture, which the present must wait for and then show again: a move the compositor was not ready for is shown.
*/
/*
        A menu opened by a finger and dragged over its items: as the hot item moves (each move a picture the compositor takes),
        the ground the compositor gives the menu's row and the inks the menu draws in stay where they were. The ground is the
        scene under the menu, not the menu's own highlight, so a move of the highlight changes neither (a change would
        redraw the text in a new ink each move: a flicker).
*/
static void check_popup_ground_steady(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static struct ui_popup popup;
    struct window_row parent_row;
    struct ui parent_ui = terminal.ui;
    struct windows address_to table;
    unsigned int first_ground = 0, first_ink = 0;
    int moves = 0, ground_moves = 0, ink_moves = 0;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("popup ground: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);
    parent_ui.scale = 100;
    parent_ui.regular = parent_ui.bold = terminal.plain[0];
    parent_ui.scratch = null, parent_ui.scratch_width = 0, parent_ui.scratch_scale = 0;
    memory_zero(&parent_row, sizeof(parent_row));
    parent_row.table = table;
    parent_row.row = 1;

    windows_reset_row(table, 3);
    __atomic_store_n(&table->pid[3], 1u, __ATOMIC_RELEASE);
    memory_zero(&popup, sizeof(popup));
    ui_popmenu_add(&popup.menu, "Copy", "Ctrl+C", 0, 1);
    ui_popmenu_add(&popup.menu, "Paste", "Ctrl+V", 0, 2);
    ui_popmenu_add(&popup.menu, null, null, UI_MENU_SEPARATOR, 0);
    ui_popmenu_add(&popup.menu, "Select all", "Ctrl+A", 0, 3);
    popup.own.table = table;
    popup.own.row = 3;
    popup.own.door = -1;
    popup.own.wake = -1;
    popup.own.buffer = compositor.buffer;
    popup.own.stride = GLASS_SIDE;
    popup.own.output_width = popup.own.output_height = GLASS_SIDE;
    if (!ui_popmenu_shows(&popup, &parent_row, &parent_ui, 10, 10, 0, 0, WINDOW_POPUP_POINT, 1))
    {
        check("popup ground: the menu opens", false);
        return;
    }
    compositor_take(3);
    compositor_present();
    // The menu's pump takes the kit's clock: over a window that has not drawn since (idle), a press's ripple and its fade run on the
    // time now, not on the parent's last frame, so the menu does not stay busy with a ripple that never ages.
    ui_kit.now = 1000;
    ui_popmenu_pump(&popup);
    check("popup: the menu's pump takes the kit's clock, not the parent's last frame's", ui_kit.now > 1000);
    /* A motion over the menu is in the menu's own coordinates: the pump leaves the app window's pointer and the shape it sends as they
       were, or the window lit what was under the place the pointer had on the menu (fbfcb90b). */
    ui_kit.pointer_x = 300, ui_kit.pointer_y = 200, ui_kit_shape = WINDOW_POINTER_HAND;
    compositor_post(3, WINDOW_EVENT_MOTION, 0, 12, 14);
    ui_popmenu_pump(&popup);
    check("popup: a motion over the menu leaves the window's pointer and the shape it sends where they were",
          ui_kit.pointer_x == 300 && ui_kit.pointer_y == 200 && ui_kit_shape == WINDOW_POINTER_HAND && popup.x == 12 && popup.y == 14);
    ui_kit.pointer_x = ui_kit.pointer_y = 0, ui_kit_shape = WINDOW_POINTER_ARROW;
    for (int hot = 0; hot < 4; hot++)
    {
        if (hot == 2)
            continue;
        popup.menu.hot = hot;
        ui_popmenu_present(&popup);
        compositor_take(3);
        compositor_present();
        ui_popmenu_pump(&popup);
        compositor_take(3);
        compositor_present();
        ui_popmenu_pump(&popup);
        if (!first_ground)
            first_ground = table->ground[3] & 0xffffffu, first_ink = popup.ui.ground & 0xffffffu;
        moves++;
        ground_moves += (table->ground[3] & 0xffffffu) != first_ground;
        ink_moves += (popup.ui.ground & 0xffffffu) != first_ink;
    }
    check("popup ground: a finger moving the hot item over the menu leaves the ground the compositor gives it where it was",
          moves > 0 && ground_moves == 0);
    check("popup ground: and the inks the menu draws in stay as they were", moves > 0 && ink_moves == 0);
    popup.open = false;
    compositor_popup_leave(3);
    compositor_leave(3);
}

/*
        A dropdown's labels, on every open: the open popup's picture carries each label, read back from the picture. The popup
        takes the ui of the page it opens from, and the page last painted a band of its rows (settings' partial paint): so
        every open here comes from a ui with a band of its own, none, the top rows or the middle rows, and the picture must
        still hold each item's label (ink in its column at its row).
*/
static void check_dropdown_labels(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static struct ui_popup popup;
    struct window_row parent_row;
    struct ui parent_ui = terminal.ui;
    struct windows address_to table;
    int missing = 0, labels_missing = 0, opens = 0;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("dropdown labels: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);
    parent_ui.scale = 100;
    parent_ui.regular = parent_ui.bold = terminal.plain[0];
    parent_ui.scratch = null, parent_ui.scratch_width = 0, parent_ui.scratch_scale = 0;
    memory_zero(&parent_row, sizeof(parent_row));
    parent_row.table = table;
    parent_row.row = 1;
    for (int open = 0; open < 12; open++)
    {
        int band = open % 3, pad = UI_MENU_PAD, inset = UI_MENU_INSET, row_h;
        static const unsigned int bands[3][2] = {{0, 0}, {0, 20}, {60, 80}};

        // The page's last paint: a band of its rows (none is the whole, which is no band).
        parent_ui.band_top = (int)bands[band][0], parent_ui.band_bottom = (int)bands[band][1];
        windows_reset_row(table, 3);
        __atomic_store_n(&table->pid[3], 1u, __ATOMIC_RELEASE);
        memory_zero(&popup, sizeof(popup));
        ui_popmenu_add(&popup.menu, "Copy", null, 0, 1);
        ui_popmenu_add(&popup.menu, "Paste", null, 0, 2);
        ui_popmenu_add(&popup.menu, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(&popup.menu, "Select all", null, 0, 3);
        popup.own.table = table;
        popup.own.row = 3;
        popup.own.door = -1;
        popup.own.wake = -1;
        popup.own.buffer = compositor.buffer;
        popup.own.stride = GLASS_SIDE;
        popup.own.output_width = popup.own.output_height = GLASS_SIDE;
        // Both of the popup's pictures start blank, so a label read back is one this open drew (a picture left from an
        // earlier open holds its labels, and a check that read it would pass whatever this open drew).
        for (int slot = 0; slot < 2; slot++)
            memory_zero((void address_to)windows_pixels(&popup.own, (unsigned int)slot), sizeof(unsigned int) * GLASS_SIDE * GLASS_SIDE);
        opens++;
        if (!ui_popmenu_shows(&popup, &parent_row, &parent_ui, 10, 10, 0, 0, WINDOW_POPUP_POINT, 0))
        {
            missing++;
            continue;
        }
        compositor_take(3);
        compositor_present();
        ui_popmenu_pump(&popup);
        compositor_take(3);
        compositor_present();
        ui_popmenu_pump(&popup);
        if (!compositor.showing[3] || compositor_popup_parent[3] != 1)
        {
            missing++;
        }
        else
        {
            const unsigned int address_to front = windows_pixels(&popup.own, (unsigned int)popup.shown);
            int y = pad;

            row_h = popmenu_row(&popup.menu);
            for (int at = 0; at < popup.menu.count; at++)
            {
                int ink = 0;

                if (popup.menu.items[at].flags & UI_MENU_SEPARATOR)
                {
                    y += ui_popmenu_start(&popup.menu, at + 1) - ui_popmenu_start(&popup.menu, at);
                    continue;
                }
                for (int yy = y + row_h / 4; yy < y + 3 * row_h / 4; yy++)
                    for (int xx = pad + inset; xx < pad + inset + 40; xx++)
                        ink += (front[yy * GLASS_SIDE + xx] >> 24) != 0;
                labels_missing += ink == 0;
                y += row_h;
            }
        }
        popup.open = false;
        compositor_popup_leave(3);
        compositor_leave(3);
    }
    check("dropdown labels: every open draws its popup on the screen, whole (none missing)", missing == 0 && opens == 12);
    check("dropdown labels: every label of every open is drawn in its row, read back from the picture (none missing)",
          labels_missing == 0);
}

/*
        A dropdown's selected item, read back from the menu's picture: the label on the highlight it is drawn on, hovered (or chosen
        by the keys) and pressed (the press's ripple over the highlight at its peak). A glass menu on a light ground and on a dark
        one, and an opaque menu in the light theme and the dark. Each label reads at UI_READABLE (4.7:1) or more on its highlight.
*/
static unsigned int dropdown_label_ratio(const unsigned int address_to picture, unsigned int under, int top, int row)
{
    int left = UI_MENU_PAD + UI_MENU_INSET;
    unsigned int background = ui_over(picture[(top + row / 2) * 600 + left - 2], under), best = 0;

    for (int yy = top + row / 4; yy < top + 3 * row / 4; yy++)
        for (int xx = left; xx < left + 40; xx++)
        {
            unsigned int r = ui_contrast(ui_over(picture[yy * 600 + xx], under), background);

            best = r > best ? r : best;
        }
    return best;
}

static void check_dropdown_selected(void)
{
    static unsigned int picture[600 * 300];
    static struct ui_popmenu m;
    static const unsigned int grounds[4] = {0xf7f7f8u, 0x101012u, 0xf7f7f8u, 0x18181bu};
    static const int glass[4] = {1, 1, 0, 0};
    unsigned int ratio[4][2];

    for (int k = 0; k < 4; k++)
    {
        struct ui g = terminal.ui;
        struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0};
        unsigned int under = 0xff000000u | grounds[k];
        int row, y;

        g.scale = 100;
        g.regular = g.bold = terminal.plain[0];
        g.scratch = null, g.scratch_width = 0, g.scratch_scale = 0;
        ui_scratch_fit(&g, 640);
        g.band_top = g.band_bottom = 0;
        g.glass = glass[k], g.opaque = !glass[k];
        ui_kit_theme(grounds[k] > 0x808080u ? UI_THEME_LIGHT : UI_THEME_DARK, UI_ACCENT_DAWN, 0);
        ui_kit.shown = ui_kit.to; // the theme as it is now, not eased from the last one
        ui_kit_settle();
        ui_inks(&g, grounds[k]);
        if (!glass[k])
            under = 0xff000000u | ui_role(UI_SURFACE);
        m.count = 0;
        ui_popmenu_add(&m, "Copy", null, 0, 1);
        ui_popmenu_add(&m, "Paste", null, 0, 2);
        ui_popmenu_add(&m, "Select all", null, 0, 3);
        m.hot = 1, m.touch = 0, m.typed_length = 0;
        ui_popmenu_to(&m, 0);
        m.ripple.live = 0, m.ripple_at = -1, m.moving = 0;
        memory_zero(&m.press, sizeof(m.press));
        ui_popmenu_size(&g, &m, 300);
        row = popmenu_row(&m), y = UI_MENU_PAD * g.scale / 100 + row;
        memory_zero(picture, sizeof(picture));
        ui_begin(&g, picture, 600, m.width, m.height);
        ui_popmenu_draw(&g, &m);
        ratio[k][0] = dropdown_label_ratio(picture, under, y, row);
        // Pressed on the item: the ripple is drawn at its peak (a little over 300 ms from the press), over the highlight.
        ui_kit.now = 1000000000ll;
        ui_popmenu_event(&g, &m, &press, m.width / 2, y + row / 2);
        memory_zero(picture, sizeof(picture));
        ui_begin(&g, picture, 600, m.width, m.height);
        ui_popmenu_draw(&g, &m);
        ui_kit.now += 300000000ll;
        memory_zero(picture, sizeof(picture));
        ui_begin(&g, picture, 600, m.width, m.height);
        ui_popmenu_draw(&g, &m);
        ratio[k][1] = dropdown_label_ratio(picture, under, y, row);
    }
    check("dropdown selected: a glass menu on a light ground, the hovered item's label reads on its highlight (4.7:1 or more)",
          ratio[0][0] >= UI_READABLE);
    check("dropdown selected: a glass menu on a light ground, the pressed item's label reads on its ripple (4.7:1 or more)",
          ratio[0][1] >= UI_READABLE);
    check("dropdown selected: a glass menu on a dark ground, the hovered item's label reads on its highlight (4.7:1 or more)",
          ratio[1][0] >= UI_READABLE);
    check("dropdown selected: a glass menu on a dark ground, the pressed item's label reads on its ripple (4.7:1 or more)",
          ratio[1][1] >= UI_READABLE);
    check("dropdown selected: an opaque menu in the light theme, the hovered item's label reads on its highlight (4.7:1 or more)",
          ratio[2][0] >= UI_READABLE);
    check("dropdown selected: an opaque menu in the light theme, the pressed item's label reads on its ripple (4.7:1 or more)",
          ratio[2][1] >= UI_READABLE);
    check("dropdown selected: an opaque menu in the dark theme, the hovered item's label reads on its highlight (4.7:1 or more)",
          ratio[3][0] >= UI_READABLE);
    check("dropdown selected: an opaque menu in the dark theme, the pressed item's label reads on its ripple (4.7:1 or more)",
          ratio[3][1] >= UI_READABLE);
}

/*
        A dropdown closes when it is scrolled. A wheel or a touchpad's scroll over the menu's own window closes it (the menu's row
        answers UI_MENU_GONE, as Escape does), and a scroll the app's window receives closes it too (ui_popmenu_scroll), once: a
        second one finds it closed. A motion that is not a scroll leaves it open, and so does a stop (WINDOW_WHEEL_STOP, a touchpad's
        fingers lifting), which is a wheel event that turns nothing.
*/
static void check_dropdown_scroll(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static struct ui_popup popup;
    struct window_row parent_row;
    struct windows address_to table;
    struct ui parent_ui = terminal.ui;
    struct window_event wheel = {WINDOW_EVENT_WHEEL, 0, 120, 0}, sideways = {WINDOW_EVENT_WHEEL, 0, 120, 1};
    struct window_event stop = {WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP, 0, 0};
    struct window_event motion = {WINDOW_EVENT_MOTION, 0, 5, 5};
    int answer[4] = {UI_MENU_NOTHING, UI_MENU_NOTHING, UI_MENU_NOTHING, UI_MENU_GONE}, again = UI_MENU_NOTHING, kept = UI_MENU_NOTHING;
    int stop_app = UI_MENU_GONE, stop_then = UI_MENU_NOTHING;
    b32 shown[4] = {false, false, false, false}, closed[4] = {false, false, false, false}, kept_open = false;
    b32 stop_own_open = false, stop_app_open = false;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("dropdown scroll: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);
    parent_ui.scale = 100;
    parent_ui.regular = parent_ui.bold = terminal.plain[0];
    parent_ui.scratch = null, parent_ui.scratch_width = 0, parent_ui.scratch_scale = 0;
    memory_zero(&parent_row, sizeof(parent_row));
    parent_row.table = table;
    parent_row.row = 1;
    for (int round = 0; round < 4; round++)
    {
        windows_reset_row(table, 3);
        __atomic_store_n(&table->pid[3], 1u, __ATOMIC_RELEASE);
        memory_zero(&popup, sizeof(popup));
        ui_popmenu_add(&popup.menu, "Copy", null, 0, 1);
        ui_popmenu_add(&popup.menu, "Paste", null, 0, 2);
        ui_popmenu_add(&popup.menu, "Select all", null, 0, 3);
        popup.own.table = table;
        popup.own.row = 3;
        popup.own.door = -1;
        popup.own.wake = -1;
        popup.own.buffer = compositor.buffer;
        popup.own.stride = GLASS_SIDE;
        popup.own.output_width = popup.own.output_height = GLASS_SIDE;
        shown[round] = ui_popmenu_shows(&popup, &parent_row, &parent_ui, 10, 10, 0, 0, WINDOW_POPUP_POINT, 0) && popup.open;
        compositor_take(3);
        compositor_present();
        ui_popmenu_pump(&popup);
        if (round == 0)
        {
            // A wheel over the menu: the compositor sends it to the menu's own row.
            compositor_post(3, WINDOW_EVENT_WHEEL, 0, 120, 0);
            answer[0] = ui_popmenu_pump(&popup);
        }
        else if (round == 1)
        {
            // A touchpad's sideways scroll over the menu.
            compositor_post(3, WINDOW_EVENT_WHEEL, 0, 120, 1);
            answer[1] = ui_popmenu_pump(&popup);
        }
        else if (round == 2)
        {
            // A scroll the app's window receives (the app's loop asks ui_popmenu_scroll), then one more: the menu is closed by the first.
            answer[2] = ui_popmenu_scroll(&popup, &wheel);
            again = ui_popmenu_scroll(&popup, &sideways);
        }
        else
        {
            // A touchpad's fingers lifting, as the compositor says it: a stop to the menu's own row, and one the app's window receives.
            // Neither turns anything, and the menu is open after both; the scroll that comes after them closes it.
            compositor_post(3, WINDOW_EVENT_WHEEL, WINDOW_WHEEL_PRECISE | WINDOW_WHEEL_STOP, 0, 0);
            answer[3] = ui_popmenu_pump(&popup);
            stop_own_open = popup.open;
            stop_app = ui_popmenu_scroll(&popup, &stop);
            stop_app_open = popup.open;
            stop_then = ui_popmenu_scroll(&popup, &wheel);
        }
        closed[round] = !popup.open;
        popup.open = false;
        compositor_popup_leave(3);
        compositor_leave(3);
    }
    // A motion is not a scroll: the menu stays open over it.
    windows_reset_row(table, 3);
    __atomic_store_n(&table->pid[3], 1u, __ATOMIC_RELEASE);
    memory_zero(&popup, sizeof(popup));
    ui_popmenu_add(&popup.menu, "Copy", null, 0, 1);
    popup.own.table = table;
    popup.own.row = 3;
    popup.own.door = -1;
    popup.own.wake = -1;
    popup.own.buffer = compositor.buffer;
    popup.own.stride = GLASS_SIDE;
    popup.own.output_width = popup.own.output_height = GLASS_SIDE;
    ui_popmenu_shows(&popup, &parent_row, &parent_ui, 10, 10, 0, 0, WINDOW_POPUP_POINT, 0);
    kept = ui_popmenu_scroll(&popup, &motion);
    kept_open = popup.open;
    popup.open = false;
    compositor_popup_leave(3);
    compositor_leave(3);

    check("dropdown scroll: a menu is open before each scroll, so each one is a close", shown[0] && shown[1] && shown[2]);
    check("dropdown scroll: a wheel over the menu's own window closes it, answered as Escape is (UI_MENU_GONE)",
          closed[0] && answer[0] == UI_MENU_GONE);
    check("dropdown scroll: a touchpad's sideways scroll over the menu closes it too", closed[1] && answer[1] == UI_MENU_GONE);
    check("dropdown scroll: a scroll the app's window receives closes its open menu, and a second scroll finds it closed",
          closed[2] && answer[2] == UI_MENU_GONE && again == UI_MENU_NOTHING);
    check("dropdown scroll: a motion that is not a scroll leaves the menu open", kept == UI_MENU_NOTHING && kept_open);
    check("dropdown scroll: a touchpad's lift over the menu's own window (a stop, which turns nothing) leaves the menu open",
          shown[3] && answer[3] == UI_MENU_NOTHING && stop_own_open);
    check("dropdown scroll: a touchpad's lift the app's window receives leaves the menu open too, and the scroll after it closes it",
          stop_app == UI_MENU_NOTHING && stop_app_open && stop_then == UI_MENU_GONE && closed[3]);
}

static void check_popup_reopen(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static unsigned int expect[GLASS_SIDE * GLASS_SIDE];
    static struct ui_popup popup;
    struct window_row parent_row;
    struct ui parent_ui = terminal.ui;
    struct windows address_to table;
    int blank = 0, partial = 0, missing = 0, stale = 0, on_screen_lost = 0, opens = 200;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("popup reopen: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);

    parent_ui.scale = 100;
    parent_ui.regular = parent_ui.bold = terminal.plain[0];
    parent_ui.scratch = null, parent_ui.scratch_width = 0, parent_ui.scratch_scale = 0;
    memory_zero(&parent_row, sizeof(parent_row));
    parent_row.table = table;
    parent_row.row = 1;
    for (int open = 0; open < opens; open++)
    {
        struct ui picture_ui;
        const unsigned int *front;
        int variant = open % 3, hot = variant ? 1 : -1, differ = 0, drawn = 0;

        // The row as windows_take leaves it: reset, and its program's pid.
        windows_reset_row(table, 3);
        __atomic_store_n(&table->pid[3], 1u, __ATOMIC_RELEASE);
        memory_zero(&popup.menu, sizeof(popup.menu));
        ui_popmenu_add(&popup.menu, "Copy", "Ctrl+C", 0, 1);
        ui_popmenu_add(&popup.menu, "Paste", "Ctrl+V", 0, 2);
        ui_popmenu_add(&popup.menu, null, null, UI_MENU_SEPARATOR, 0);
        ui_popmenu_add(&popup.menu, "Select all", "Ctrl+A", 0, 3);
        popup.own.table = table;
        popup.own.row = 3;
        popup.own.door = -1;
        popup.own.wake = -1;
        popup.own.buffer = compositor.buffer;
        popup.own.stride = GLASS_SIDE;
        popup.own.output_width = popup.own.output_height = GLASS_SIDE;
        if (!ui_popmenu_shows(&popup, &parent_row, &parent_ui, 10, 10, 0, 0, WINDOW_POPUP_POINT, 0))
        {
            missing++;
            continue;
        }
        // Three kinds of open: the menu as it came (0); a key moves the hot item before the compositor has taken its first
        // picture (1); the compositor takes the picture and the glass gives the menu its ground, the app is woken and its
        // inks are presented in flight, and only then a key moves the hot item (2).
        if (variant == 1)
        {
            popup.menu.hot = hot;
            ui_popmenu_present(&popup);
        }
        compositor_take(3);
        compositor_present();
        if (variant == 2)
        {
            ui_popmenu_pump(&popup);
            popup.menu.hot = hot;
            ui_popmenu_present(&popup);
        }
        // Each knock of the compositor's takes wakes the app, which pumps the menu, and a picture it was not ready for is
        // presented then, until the menu has nothing left to show.
        for (int round = 0; round < 3; round++)
        {
            compositor_take(3);
            compositor_present();
            ui_popmenu_pump(&popup);
        }
        compositor_take(3);
        compositor_present();

        if (!compositor.showing[3] || compositor_popup_parent[3] != 1)
        {
            missing++;
        }
        else if (compositor.w[3] != (unsigned int)popup.menu.width || compositor.h[3] != (unsigned int)popup.menu.height)
        {
            partial++;
        }
        else
        {
            picture_ui = popup.ui;
            popup.menu.hot = hot;
            ui_begin(&picture_ui, expect, GLASS_SIDE, popup.menu.width, popup.menu.height);
            ui_popmenu_draw(&picture_ui, &popup.menu);
            front = windows_pixels(&popup.own, (unsigned int)popup.shown);
            for (int y = 0; y < popup.menu.height; y++)
                for (int x = 0; x < popup.menu.width; x++)
                {
                    unsigned int pixel = front[y * GLASS_SIDE + x];
                    int sx = compositor.x[3] + x, sy = compositor.y[3] + y;

                    drawn += expect[y * GLASS_SIDE + x] != 0;
                    differ += pixel != expect[y * GLASS_SIDE + x];
                    // An opaque pixel of the picture is its own colour on the screen, whatever glass is under it.
                    if (pixel >> 24 == 255 && sx >= 0 && sy >= 0 && sx < GLASS_SIDE && sy < GLASS_SIDE &&
                        glass_at(sx, sy) != (pixel & 0xffffffu))
                        on_screen_lost++;
                }
            if (differ && !drawn)
                blank++;
            else if (differ)
                stale += hot >= 0;
        }

        // The program's door closes and the compositor lets the row go, as compositor_door does.
        popup.open = false;
        compositor_popup_leave(3);
        compositor_leave(3);
    }
    string_format(log, "  popup reopen: %p opens, %p missing, %p partial, %p blank, %p opaque pixels not on the screen, %p with a move not shown\n",
                  (positive)opens, (positive)missing, (positive)partial, (positive)blank, (positive)on_screen_lost, (positive)stale);
    check("popup reopen: every menu opened is on the screen, whole and as a popup of its window (blank, partial or missing: none)",
          !missing && !partial && !blank);
    check("popup reopen: every opaque pixel of a menu is on the screen as it is drawn", on_screen_lost == 0);
    check("popup reopen: a move of the hot item is shown when the compositor was not ready for the present it made",
          stale == 0);
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        A menu is not resized. Just outside its left edge, where a window's grip is (a pointer's, and a finger's wider one), the
        menu's edge is no grip and no resize: the edge answers no resize and shows no indicator, and a press there asks the
        menu to go and goes no further, to the window beneath, which takes neither the press nor the focus.
*/
static void check_compositor_popup_grip(b32 touch)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    int tab = COMPOSITOR_NONE;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("popup grip: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);

    // A menu of row 3 for row 1 at 10 pixels in from the window's top left: on the screen 60 to 120 by 40 to 80.
    table->popup_parent[3] = 2;
    table->popup_x[3] = 10;
    table->popup_y[3] = 10;
    table->popup_w[3] = 0;
    table->popup_h[3] = 0;
    table->popup_flags[3] = WINDOW_POPUP_POINT;
    table->width[3] = 60;
    table->height[3] = 40;
    table->glass[3] = WINDOW_NO_CLOSE;
    glass_paint(3, 0, 0, 60, 40, 0x303030, true);
    compositor_present();

    // The band just outside the menu's left edge, inside window 1: a finger's grip reaches further out than a pointer's.
    compositor.touching = touch;
    compositor.pointer_x = touch ? 52 : 57;
    compositor.pointer_y = 60;
    if (touch)
        check("popup grip: a finger just outside a menu's edge is no grip of it",
              compositor_edge_zone(3, compositor.pointer_x, compositor.pointer_y) == 0);
    else
        check("popup grip: a pointer just outside a menu's edge is no grip of it",
              compositor_edge_zone(3, compositor.pointer_x, compositor.pointer_y) == 0);
    check("popup grip: what is there is the window beneath's picture, not the menu's",
          compositor_under(compositor.pointer_x, compositor.pointer_y, &tab) == 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    if (touch)
        check("popup grip: a finger there asks the menu to go", (table->state[3] & WINDOW_CLOSING) != 0);
    else
        check("popup grip: a press there asks the menu to go", (table->state[3] & WINDOW_CLOSING) != 0);
    check("popup grip: and it is no resize of the window beneath, nor a grab or a focus change",
          compositor.resize < 0 && compositor.grab == COMPOSITOR_NONE && compositor.focus == 1 && compositor.lit_edges == 0);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor.touching = 0;
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        A press outside a menu takes what the hand had in hand in the window that owns the menu: a drag of it, and a button held
        in it, are let go, and the window hears the release as one that is no click (WINDOW_KEY_CANCEL). Not what a window that
        owns no menu holds.
*/
static void check_compositor_popup_holds(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int at;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("popup holds: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = WINDOW_GLASS;
    glass_paint(1, 0, 0, 120, 80, 0, true);
    compositor_present();
    compositor_focus(1);

    table->popup_parent[3] = 2;
    table->popup_x[3] = 10;
    table->popup_y[3] = 10;
    table->popup_w[3] = 0;
    table->popup_h[3] = 0;
    table->popup_flags[3] = WINDOW_POPUP_POINT;
    table->width[3] = 60;
    table->height[3] = 40;
    table->glass[3] = WINDOW_NO_CLOSE;
    glass_paint(3, 0, 0, 60, 40, 0x303030, true);
    compositor_present();

    // Window 1's left button is held in it and its drag is in the hand (the menu was opened over it by a press with the
    // left held); a press outside the menu, on window 1 itself, is the one that dismisses it.
    compositor.grab = 1;
    compositor.button_down = 1;
    compositor.button_row[0] = 1;
    compositor.drag = 1;
    compositor.drag_x = compositor.drag_y = 0;
    compositor.pointer_x = 150;
    compositor.pointer_y = 100;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    at = (table->event_head[1] + WINDOWS_EVENTS - 1) % WINDOWS_EVENTS;
    check("popup holds: a press outside the menu asks it to go", (table->state[3] & WINDOW_CLOSING) != 0);
    check("popup holds: the drag the menu was over is let go", compositor.drag == COMPOSITOR_NONE && compositor.grab == COMPOSITOR_NONE);
    check("popup holds: the button held in its window is let go", (compositor.button_down & 1) == 0);
    check("popup holds: its window hears the release as one that is no click",
          table->event_type[1][at] == WINDOW_EVENT_BUTTON && (table->event_flags[1][at] & WINDOW_KEY_CANCEL) != 0);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        A glass menu is composed as glass (compositor_glass_pass): where its picture is clear it shows what is under it, tinted
        and blurred, and where it is drawn it is its own colour. A menu's edge has no line: the window beneath is what is
        outside it.
*/
static void check_compositor_glass_popup(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("glass popup: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->state[1] = table->state[3] = 0;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    table->glass[1] = 0;
    glass_paint(1, 0, 0, 120, 80, 0xff2060c0u, true);
    compositor_present();
    compositor_focus(1);

    // A glass menu of row 1, 10 pixels in from its top left: on the screen 60 to 120 by 40 to 80, clear but for a square.
    table->popup_parent[3] = 2;
    table->popup_x[3] = 10;
    table->popup_y[3] = 10;
    table->popup_w[3] = 0;
    table->popup_h[3] = 0;
    table->popup_flags[3] = WINDOW_POPUP_POINT;
    table->width[3] = 60;
    table->height[3] = 40;
    table->glass[3] = WINDOW_NO_CLOSE | WINDOW_GLASS;
    glass_paint(3, 0, 0, 60, 40, 0, true);
    glass_paint(3, 10, 10, 20, 20, 0xff00ff00u, false);
    compositor_present();
    {
        unsigned int clear = glass_at(65, 45), drawn = glass_at(80, 60), outside = glass_at(59, 60);

        check("glass menu: where its picture is clear it is glass over what is under it, tinted, not the window's colour nor black",
              clear != 0x2060c0u && clear != 0);
        check("glass menu: where it is drawn it is its own colour", drawn == 0x00ff00u);
        check("glass menu: it has its line at its edge, as a window does, and the window beneath is what is outside that",
              outside == compositor_line_unlit() && glass_at(58, 60) == 0x2060c0u);
    }
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        The tray (the clock and the battery) pressed twice quickly opens the settings: a window named settings that is
        up is brought up and given the keys, and with none the script is asked to start it. One press, a press of the
        middle button, and two too far apart do nothing.
*/
/*
        A tab's finger reach: under a finger a tab answers UI_TARGET (48 logical px) past its inner edge, toward the middle of its
        screen (the old reach was 20 px, so a point 30 px out missed it). The points are the tab's inner edge plus 20, 30 and 50 px,
        along the band toward the middle: 20 and 30 px land on the tab and 50 px does not.
*/
static void check_compositor_tab_reach(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct taskbar_view view;
    int sx, sy, sw, sh, dir, hit, at20, at30, at50;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("tab reach: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 20;
    table->y[1] = 20;
    table->width[1] = 100;
    table->height[1] = 60;
    glass_paint(1, 0, 0, 100, 60, 0x404040u, true);
    compositor_present();
    compositor_focus(1);

    view = compositor_band_view();
    if (!taskbar_slot(&view, 0, &sx, &sy, &sw, &sh))
    {
        check("tab reach: the band has a tab", false);
        return;
    }
    // A bottom band's tab reaches up, toward the middle of the screen, by the finger's target (taskbar_hit_reach); the band
    // of this fixture is at the bottom, which is checked rather than assumed.
    dir = view.position == TASKBAR_BOTTOM ? 1 : 0;
    compositor.touching = 1;
    hit = compositor_band_hit(sx + sw / 2, sy + sh / 2);
    at20 = compositor_band_hit(sx + sw / 2, sy - 20);
    at30 = compositor_band_hit(sx + sw / 2, sy - 30);
    at50 = compositor_band_hit(sx + sw / 2, sy - 50);
    compositor.touching = 0;
    check("tab reach: the fixture's band is at the bottom, so its tab reaches up toward the middle", dir);
    check("tab reach: a finger 20 and 30 px above a tab on the bottom band is on that tab (UI_TARGET reach, 48 px)",
          hit != TASKBAR_HIT_NONE && at20 == hit && at30 == hit);
    check("tab reach: a finger 50 px above it is not on that tab", at50 != hit);
}

static void check_compositor_tray(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct taskbar_view view;
    int tx, ty, tw, th;

    compositor_zero();
    taskbar_classic(); // the tray these rows were written for: every item after the tasks, the clock its cell
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("tray: the table is mapped", false);
        taskbar_standard();
        return;
    }
    table = compositor.table;
    table->x[1] = 20;
    table->y[1] = 20;
    table->width[1] = 100;
    table->height[1] = 60;
    glass_paint(1, 0, 0, 100, 60, 0x404040u, true);
    table->x[2] = 140;
    table->y[2] = 20;
    table->width[2] = 100;
    table->height[2] = 60;
    memory_copy((void *)table->name[2], "settings", sizeof("settings"));
    glass_paint(2, 0, 0, 100, 60, 0x606060u, true);
    compositor_present();
    compositor_focus(1);

    view = compositor_band_view();
    if (!taskbar_slot(&view, -2, &tx, &ty, &tw, &th))
    {
        check("tray: the band has a tray", false);
        taskbar_standard();
        return;
    }
    compositor.pointer_x = tx + tw / 2;
    compositor.pointer_y = ty + th / 2;
    compositor_pointer_clamp();

    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    check("tray: one press does nothing", compositor.focus == 1 && !compositor.settings_asked);
    compositor_button(COMPOSITOR_BTN_MIDDLE, 1);
    compositor_button(COMPOSITOR_BTN_MIDDLE, 0);
    check("tray: nor does the middle button after it", compositor.focus == 1 && !compositor.settings_asked);

    compositor.cells = (struct taskbar_cells){0};
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    check("tray: two presses bring up the settings window that is open, and ask for no other", compositor.focus == 2 && !compositor.settings_asked);

    compositor_focus(1);
    compositor.cells = (struct taskbar_cells){0};
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    taskbar_cell_clicks(&compositor.cells, TASKBAR_TRAY, TASKBAR_BUTTON_LEFT)->at -= UI_CLICK_NS + 1;
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    check("tray: two too far apart are two single presses", compositor.focus == 1 && !compositor.settings_asked);

    memory_zero((void *)table->name[2], WINDOWS_NAME);
    compositor.cells = (struct taskbar_cells){0};
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    check("tray: with no settings window open, two presses ask the script for one", compositor.settings_asked == 1 && compositor.focus == 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_button(COMPOSITOR_BTN_LEFT, 0);
    check("tray: a third press right after the pair starts a new one", compositor.settings_asked == 1);
        taskbar_standard();
}

/*
        A tab's menu (taskbar_tab_menu_open). The walk that Force stop makes over the processes first, as tables: the root of a
        program is the highest process above the window's that is no system or desktop one, and one step lower where its parent is a
        terminal (the shell stays); what is under it is the members; a system process has no root. Then the menu itself, on the
        fixture of the tray's rows: a right press on a tab (a finger's long press the same) opens it beside the tab with Exit and,
        for a window whose process can be stopped, Force stop, saying how many windows the program has when it has more than one;
        Exit asks the window to close as a middle press does; Force stop stops every process of the program and then ends them
        (a fork made while the walk was under way too), none of the others; and what is chosen is done to the window the menu was
        opened for only while the row has the pid it had. The /proc is a directory of the check's own, and the signals are recorded.
*/
static int tab_signals[64][2];
static int tab_signal_count, tab_fork_at;
static const char *tab_fork_dir;

static void tab_signal_write(const char *root, int pid, const char *comm, int ppid)
{
    char path[160], text[96];
    unsigned long at;
    bipolar handle;

    string_copy((string_address)path, (string_address)root), at = string_length((string_address)path);
    path[at++] = '/', at += positive_into_string((p8 *)path + at, (positive)pid), path[at] = 0;
    system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
    string_copy((string_address)path + at, (string_address) "/stat");
    at = 0;
    at += positive_into_string((p8 *)text + at, (positive)pid), text[at++] = ' ', text[at++] = '(';
    string_copy((string_address)text + at, (string_address)comm), at += string_length((string_address)comm);
    text[at++] = ')', text[at++] = ' ', text[at++] = 'S', text[at++] = ' ';
    at += positive_into_string((p8 *)text + at, (positive)ppid);
    string_copy((string_address)text + at, (string_address) " 1 1 0 -1 4194560 100 0 0 0 1 1\n");
    at += string_length((string_address)text + at);
    handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (handle >= 0)
        system_write_all((b32)handle, text, at), system_close(handle);
}

static void tab_signal_remove(const char *root, int pid)
{
    char path[160];
    unsigned long at;

    string_copy((string_address)path, (string_address)root), at = string_length((string_address)path);
    path[at++] = '/', at += positive_into_string((p8 *)path + at, (positive)pid), path[at] = 0;
    string_copy((string_address)path + at, (string_address) "/stat");
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
    path[at] = 0;
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0x200);
}

// The check's signals: each one kept, and the first stop forks a child of 92 (a process made while the walk is under way).
static void tab_signal_note(int pid, unsigned int signal)
{
    if (tab_signal_count < 64)
        tab_signals[tab_signal_count][0] = pid, tab_signals[tab_signal_count][1] = (int)signal, tab_signal_count++;
    if (tab_fork_dir && signal == TASKBAR_SIGSTOP && !tab_fork_at)
        tab_fork_at = 1, tab_signal_write(tab_fork_dir, 99, "worker", 92);
}

static int tab_signalled(int pid, unsigned int signal)
{
    int count = 0;

    for (int at = 0; at < tab_signal_count; at++)
        count += tab_signals[at][0] == pid && (unsigned int)tab_signals[at][1] == signal;
    return count;
}

static void check_taskbar_tab_menu(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static const struct taskbar_proc walk[] = {
        {1, 0, TASKBAR_PROC_SYSTEM},   {40, 1, TASKBAR_PROC_SYSTEM},  {50, 1, TASKBAR_PROC_DESKTOP}, {51, 50, TASKBAR_PROC_PLAIN},
        {60, 51, TASKBAR_PROC_PLAIN},  {61, 60, TASKBAR_PROC_PLAIN},  {62, 61, TASKBAR_PROC_PLAIN},   {63, 62, TASKBAR_PROC_PLAIN},
        {70, 51, TASKBAR_PROC_PLAIN},  {80, 40, TASKBAR_PROC_PLAIN},  {81, 80, TASKBAR_PROC_PLAIN},   {82, 81, TASKBAR_PROC_SYSTEM},
        {83, 82, TASKBAR_PROC_PLAIN},  {90, 1, TASKBAR_PROC_DESKTOP}, {91, 90, TASKBAR_PROC_PLAIN},   {95, 96, TASKBAR_PROC_PLAIN},
        {96, 95, TASKBAR_PROC_PLAIN}};
    int members[32], count, all = (int)(sizeof(walk) / sizeof(walk[0]));
    b32 root_ok, tree_ok, system_ok, loop_ok;
    char root[96];
    struct windows address_to table;
    struct taskbar_view view;
    int x, y, w, h, tab, opened, two, labelled, exited, forced, kept, gone, finger;

    // The walk over tables.
    root_ok = taskbar_stop_root(walk, all, 63) == 60 && taskbar_stop_root(walk, all, 60) == 60 && taskbar_stop_root(walk, all, 70) == 70 &&
              taskbar_stop_root(walk, all, 91) == 91 && taskbar_stop_root(walk, all, 81) == 80 && taskbar_stop_root(walk, all, 80) == 80;
    count = taskbar_stop_tree(walk, all, 60, members, 32);
    tree_ok = count == 4 && taskbar_stop_has(members, count, 60) && taskbar_stop_has(members, count, 61) && taskbar_stop_has(members, count, 62) &&
              taskbar_stop_has(members, count, 63) && !taskbar_stop_has(members, count, 51) && !taskbar_stop_has(members, count, 70);
    count = taskbar_stop_tree(walk, all, 80, members, 32);
    system_ok = taskbar_stop_root(walk, all, 1) == 0 && taskbar_stop_root(walk, all, 40) == 0 && taskbar_stop_root(walk, all, 82) == 0 &&
                taskbar_stop_root(walk, all, 12345) == 0 && count == 2 && taskbar_stop_has(members, count, 81) && !taskbar_stop_has(members, count, 82) &&
                !taskbar_stop_has(members, count, 83) && taskbar_stop_tree(walk, all, 1, members, 32) == 0;
    // A table that loops on itself (a table made by hand) ends, and the members are bounded by the room given.
    loop_ok = taskbar_stop_root(walk, all, 96) != 0 && taskbar_stop_tree(walk, all, 60, members, 2) == 2;

    // The menu, on the tray's fixture: two windows of the program under 60 (the processes 61 and 62).
    compositor_zero();
    taskbar_classic();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    // The band's face, which the check of the tabs' text loaded before this one is called: a menu is written with it.
    compositor.face = band_face, compositor.engine = &band_engine;
    if (!glass_table_map())
    {
        check("tab menu: the table is mapped", false);
        taskbar_standard();
        return;
    }
    table = compositor.table;
    for (int row = 1; row <= 2; row++)
    {
        table->x[row] = 20 + 60 * row;
        table->y[row] = 20;
        table->width[row] = 50;
        table->height[row] = 60;
        glass_paint(row, 0, 0, 50, 60, 0x404040u + (unsigned int)row, true);
    }
    compositor_present();
    compositor_focus(1);
    table->pid[1] = 61, table->pid[2] = 62; // 62 is the program's second window

    string_copy((string_address)root, (string_address) "/tmp/moonwater-check-tabs-");
    positive_into_string((p8 *)root + string_length((string_address)root), (positive)system_call(syscall(getpid)));
    system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)root, 0700);
    taskbar_proc_root = root;
    tab_signal_write(root, 1, "init", 0), tab_signal_write(root, 40, "moonwater", 1), tab_signal_write(root, 50, "terminal", 1);
    tab_signal_write(root, 51, "sh", 50), tab_signal_write(root, 60, "start.sh", 51), tab_signal_write(root, 61, "firefox", 60);
    tab_signal_write(root, 62, "firefox", 61), tab_signal_write(root, 63, "gpu (x)", 62), tab_signal_write(root, 70, "vim", 51);
    tab_signal_write(root, 33, "Xwayland", 40), tab_signal_write(root, 92, "helper", 63);
    taskbar_signal = tab_signal_note;

    // The compositor's kit has no theme of its own until a menu takes the table's (taskbar_menu_ui): none here either.
    memory_zero(&ui_kit.shown, sizeof(ui_kit.shown)), memory_zero(&ui_kit.to, sizeof(ui_kit.to)), ui_kit.made = 0;
    view = compositor_band_view();
    tab = -1;
    for (unsigned int each = 0; each < view.count; each++)
        tab = compositor.listed[each] == 1 ? (int)each : tab;
    opened = view.count == 2 && tab >= 0 && taskbar_slot(&view, tab, &x, &y, &w, &h);
    if (opened)
    {
        compositor.pointer_x = x + w / 2, compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
    }
    two = opened && taskbar_menu.open && taskbar_menu.row == 1 && taskbar_menu.pid == 61 && taskbar_menu.menu.count == 2 &&
          taskbar_menu.menu.items[0].id == TASKBAR_MENU_EXIT && taskbar_menu.menu.items[1].id == TASKBAR_MENU_STOP;
    labelled = two && string_equals((string_address)taskbar_menu.menu.items[1].label, (string_address) "Force stop (2 windows)") &&
               string_equals((string_address)taskbar_menu.menu.items[0].label, (string_address) "Exit");
    check("tab menu: a right press on a tab opens its menu beside it with Exit and Force stop", two);
    {
        // Its picture on the screen: drawn, a ground and its words' ink, not a square of black.
        int rect[4];
        unsigned int black = 0, first = 0, other = 0, all = 0, bright = 0;

        if (two && taskbar_menu_rect(rect))
        {
            compositor_damage(rect[0], rect[1], rect[2], rect[3]);
            compositor_present();
            for (int py = rect[1]; py < rect[1] + rect[3] && py < GLASS_SIDE; py++)
                for (int px = rect[0]; px < rect[0] + rect[2] && px < GLASS_SIDE; px++)
                {
                    unsigned int at = screen[py * GLASS_SIDE + px] & 0xffffffu;

                    if (py >= 0 && px >= 0)
                    {
                        all++, black += at == 0;
                        bright += (at >> 16 & 255) + (at >> 8 & 255) + (at & 255) > 3 * 160; // the words' ink on the dark standard's ground
                        if (all == 1)
                            first = at;
                        other += at != first;
                    }
                }
        }
        string_format(log, "  tab menu: its square %b,%b %bx%b on the screen, %p of %p pixels black, %p not the first's colour %p, %p of its words' ink\n",
                      (bipolar)rect[0], (bipolar)rect[1], (bipolar)rect[2], (bipolar)rect[3], (positive)black, (positive)all, (positive)other, (positive)first,
                      (positive)bright);
        check("tab menu: its picture is on the screen, a ground and its words, not black", all > 0 && black * 2 < all && other > 0 && bright > 40);
    }
    check("tab menu: Force stop says how many windows the program has when it has more than one", labelled);

    // Exit: the window is asked to close as a middle press asks it, once, and nothing is stopped.
    tab_signal_count = 0;
    if (two)
        compositor_menu_chose(TASKBAR_MENU_EXIT);
    exited = two && (compositor.state[1] & WINDOW_CLOSING) && compositor.close_asks[1] == 1 && !(compositor.state[2] & WINDOW_CLOSING) &&
             !taskbar_menu.open && tab_signal_count == 0;
    check("tab menu: Exit asks the tab's window to close, as its close button does, and no other, and puts the menu away", exited);

    // Force stop: the program's own processes only, each stopped and then ended, and one forked during the walk too.
    tab_fork_dir = root, tab_fork_at = 0;
    if (two)
    {
        compositor_menu_chose(TASKBAR_MENU_STOP);
    }
    forced = two && tab_signalled(60, TASKBAR_SIGSTOP) == 1 && tab_signalled(61, TASKBAR_SIGSTOP) == 1 && tab_signalled(62, TASKBAR_SIGSTOP) == 1 &&
             tab_signalled(63, TASKBAR_SIGSTOP) == 1 && tab_signalled(92, TASKBAR_SIGSTOP) == 1 && tab_signalled(99, TASKBAR_SIGSTOP) == 1 &&
             tab_signalled(60, TASKBAR_SIGKILL) == 1 && tab_signalled(61, TASKBAR_SIGKILL) == 1 && tab_signalled(62, TASKBAR_SIGKILL) == 1 &&
             tab_signalled(63, TASKBAR_SIGKILL) == 1 && tab_signalled(92, TASKBAR_SIGKILL) == 1 && tab_signalled(99, TASKBAR_SIGKILL) == 1;
    kept = true;
    for (int at2 = 0; at2 < tab_signal_count; at2++)
        kept &= tab_signals[at2][0] == 60 || tab_signals[at2][0] == 61 || tab_signals[at2][0] == 62 || tab_signals[at2][0] == 63 ||
                tab_signals[at2][0] == 92 || tab_signals[at2][0] == 99;
    check("tab menu: Force stop stops every process of the program, those it forks while the walk is under way too, and then ends them", forced);
    check("tab menu: and no process of the shell, its other jobs, the terminal or the machine is sent a signal", kept && tab_signal_count == 12);
    // The stops come before every kill.
    {
        int last_stop = -1, first_kill = tab_signal_count;

        for (int at2 = 0; at2 < tab_signal_count; at2++)
        {
            if (tab_signals[at2][1] == (int)TASKBAR_SIGSTOP)
                last_stop = at2;
            else if (first_kill == tab_signal_count)
                first_kill = at2;
        }
        check("tab menu: every stop is sent before the first kill", last_stop >= 0 && last_stop < first_kill);
    }
    tab_fork_dir = null;

    // The X server's window (the pid is Xwayland's: its row has not been given its client's), pid 1's and none: Exit and no Force stop.
    compositor_menu_close();
    gone = 1;
    view = compositor_band_view();
    for (unsigned int at2 = 0; at2 < view.count; at2++)
    {
        int row = (int)compositor.listed[at2];

        for (int pick = 0; pick < 3; pick++)
        {
            static const unsigned int pids[3] = {1, 33, 0};

            table->pid[row] = pids[pick];
            if (taskbar_slot(&view, (int)at2, &x, &y, &w, &h))
            {
                compositor.pointer_x = x + w / 2, compositor.pointer_y = y + h / 2;
                compositor_pointer_clamp();
                compositor_button(COMPOSITOR_BTN_RIGHT, 1);
                compositor_button(COMPOSITOR_BTN_RIGHT, 0);
                gone &= taskbar_menu.open && taskbar_menu.menu.count == 1 && taskbar_menu.menu.items[0].id == TASKBAR_MENU_EXIT;
                compositor_menu_close();
            }
        }
    }
    check("tab menu: the machine's own process (pid 1, the X server's, none) has Exit and no Force stop", gone);

    // A row that has another pid than the menu was opened for takes neither.
    table->pid[1] = 61, table->pid[2] = 62;
    for (int row = 1; row <= 2; row++)
        compositor.state[row] &= ~(unsigned int)WINDOW_CLOSING, compositor.close_asks[row] = 0;
    view = compositor_band_view();
    tab = -1;
    for (unsigned int each = 0; each < view.count; each++)
        tab = compositor.listed[each] == 1 ? (int)each : tab;
    if (taskbar_slot(&view, tab, &x, &y, &w, &h))
    {
        compositor.pointer_x = x + w / 2, compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
        table->pid[1] = 77; // the row was let go of and taken by another program while the menu was open
        tab_signal_count = 0;
        compositor_menu_chose(TASKBAR_MENU_STOP);
        compositor_menu_chose(TASKBAR_MENU_EXIT);
    }
    check("tab menu: a row that is another program's than when the menu opened is neither stopped nor asked to close",
          tab_signal_count == 0 && !(compositor.state[1] & WINDOW_CLOSING));

    // A finger's long press is the right button there too: a point above the tab, within the finger's reach, opens the menu.
    table->pid[1] = 61;
    if (taskbar_slot(&view, tab, &x, &y, &w, &h))
    {
        compositor.pointer_x = x + w / 2, compositor.pointer_y = y - 20;
        compositor_pointer_clamp();
        compositor.touching = 1;
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor.touching = 0;
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
    }
    finger = taskbar_menu.open && taskbar_menu.row == 1;
    check("tab menu: a finger's long press just above a tab opens it too", finger);
    compositor_menu_close();

    check("tab menu: the walk's root is the program's, not the terminal's shell, and stops below the machine", root_ok);
    check("tab menu: what is under a root is it and what descends from it, and no process of the terminal's shell besides", tree_ok);
    check("tab menu: a system process has no root, a system process in the tree is skipped with what is under it, and pid 1 has no tree", system_ok);
    check("tab menu: a table that loops ends the walk, and the members are as many as there is room for", loop_ok);

    // Put the check's directory away.
    for (int pid = 1; pid < 100; pid++)
        tab_signal_remove(root, pid);
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)root, 0x200);
    taskbar_proc_root = "/proc";
    taskbar_signal = taskbar_signal_send;
    taskbar_standard();
}

/*
        The programs pinned to the taskbar. The line that keeps them: a word with blanks, a quote, a semicolon or nothing in it is written
        in quotes and read back as it was; a line with a fault (an app_id with a slash, an entry of fewer than three words, a first word
        that is not a path, an unclosed quote, a byte below a blank, a lone ; where an entry is not, more entries or bytes than the line
        holds) is none. A program is pinned from a process of its window by what the system says of it: a kit program as its path, a native
        program by its command line with its executable for a first word that is no path, a bowl's by its exposed launcher or not at all, and a
        command line with a byte below a blank or one that was cut not at all. Then the tasks: a pinned program with no window is a task
        after the windows' that is drawn as one put away, with its app_id's last part for its title, gone while a window says the app_id; its
        menu has Unpin alone; Pin to taskbar and Unpin from taskbar are in a window's, keep the line in the file the verb keeps it in, and
        are read back from it; and a press on a task with no window starts the program: its words a line each in a new file, once in a while.
*/
static void tab_make_file(const char *path, const char *bytes, unsigned long length)
{
    bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    if (handle >= 0)
        system_write_all((b32)handle, bytes, length), system_close(handle);
}

static void tab_make_dir(const char *path)
{
    system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
}

static void tab_make_link(const char *target, const char *path)
{
    system_call_3(syscall(symlinkat), (positive)target, (positive)AT_FDCWD, (positive)path);
}

static void tab_remove_tree(const char *path)
{
    char child[256];
    bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_DIRECTORY | O_CLOEXEC);
    char buffer[2048];

    if (handle >= 0)
    {
        for (bipolar got = system_read_directory(handle, buffer, sizeof(buffer)); got > 0; got = system_read_directory(handle, buffer, sizeof(buffer)))
            for (positive at = 0; at < (positive)got;)
            {
                const struct linux_dirent64 address_to record = (const struct linux_dirent64 address_to)(buffer + at);

                at += record->d_reclen;
                if (record->d_name[0] == '.' && (!record->d_name[1] || (record->d_name[1] == '.' && !record->d_name[2])))
                    continue;
                string_copy((string_address)child, (string_address)path), string_append_bounded((string_address)child, (string_address) "/", sizeof(child));
                string_append_bounded((string_address)child, (string_address)record->d_name, sizeof(child));
                tab_remove_tree(child);
            }
        system_close(handle);
    }
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0x200);
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
}

// A fake process of the table: its stat (named, a parent), the executable it ran as, its command line (the bytes, as many) and its mount view.
static void tab_fake_process(const char *root, int pid, const char *comm, int ppid, const char *exe, const char *cmdline, unsigned long length, const char *view)
{
    char dir[200], path[240];
    unsigned long at;

    tab_signal_write(root, pid, comm, ppid);
    string_copy((string_address)dir, (string_address)root), at = string_length((string_address)dir);
    dir[at++] = '/', at += positive_into_string((p8 *)dir + at, (positive)pid), dir[at] = 0;
    string_copy((string_address)path, (string_address)dir), string_append_bounded((string_address)path, (string_address) "/exe", sizeof(path));
    if (exe)
        tab_make_link(exe, path);
    string_copy((string_address)path, (string_address)dir), string_append_bounded((string_address)path, (string_address) "/cmdline", sizeof(path));
    if (cmdline)
        tab_make_file(path, cmdline, length);
    string_copy((string_address)path, (string_address)dir), string_append_bounded((string_address)path, (string_address) "/ns", sizeof(path));
    tab_make_dir(path);
    string_append_bounded((string_address)path, (string_address) "/mnt", sizeof(path));
    if (view)
        tab_make_link(view, path);
}

static bool tab_words_are(const struct taskbar_pin *pin, int count, const char *const *words)
{
    bool same = pin->count == count;

    for (int at = 0; same && at < count; at++)
        same = string_equals((string_address)taskbar_pin_word(pin, at), (string_address)words[at]);
    return same;
}

static void check_taskbar_pins(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static const char *const good[] = {"a /b /c", "a /b /c x ; d /e /f 'g h'", "app.id-2+x /x /y 'it'\\''s' ;", "a /b /c ''", "a /b /c    x",
                                       "org.mozilla.firefox /usr/lib/firefox/firefox /usr/lib/firefox/firefox --new-window --class=a_b,c@d%e"};
    static const char *const bad[] = {";", "a /b", "a b /c", "a /b c", "a/b /x /y", "a /b /c 'unclosed", "a /b /c x;y", "a /b /c ;; d /e /f", "a /b /c 'x\001y'",
                                      "a /b /c \"x\"", "; a /b /c", "a /b /c ; ; d /e /f", ".. /b /c ~", "a /b /c \\n", "a /b /c ('x')"};
    static const char *const words[] = {"x y", "it's", "", ";", "--a=b", "un\303\257", "'", "a'b'c", "$(rm)", "\\"};
    char line[SETTING_PINS_MOST + 64], word[SETTING_PINS_WORD], root[96], path[200], second[96];
    positive at = 0, bad_at = 0;
    bool grammar = true, refused = true, quoted = true, long_ok, many_ok = true;
    struct taskbar_pin pin, other;
    struct windows address_to table;
    struct taskbar_view view;
    int x, y, w, h, tab, ghost, pids = 0;

    for (unsigned int each = 0; each < sizeof(good) / sizeof(good[0]); each++)
        grammar &= setting_pins_valid(good[each], null);
    for (unsigned int each = 0; each < sizeof(bad) / sizeof(bad[0]); each++)
        refused &= !setting_pins_valid(bad[each], &bad_at);
    // Each word written is read back as it was, alone and among others.
    line[0] = 0;
    for (unsigned int each = 0; each < sizeof(words) / sizeof(words[0]); each++)
        quoted &= setting_pins_quote(line, &at, sizeof(line), words[each]);
    {
        positive from = 0;

        for (unsigned int each = 0; each < sizeof(words) / sizeof(words[0]); each++)
            quoted &= setting_pins_next(line, &from, word, sizeof(word)) == SETTING_PINS_WORD_NEXT && string_equals((string_address)word, (string_address)words[each]);
        quoted &= setting_pins_next(line, &from, word, sizeof(word)) == SETTING_PINS_END;
    }
    // A line of bytes: 2048 are the most, and 16 programs; a seventeenth or the 2049th byte is none.
    string_copy((string_address)line, (string_address) "a /b /c");
    for (int each = 0; each < 20; each++)
    {
        positive stop = string_length((string_address)line);

        if (stop + 20 >= sizeof(line))
            break;
        string_copy((string_address)line + stop, (string_address) " ; a /b /c");
        many_ok &= setting_pins_valid(line, null) == (each + 2 <= SETTING_PINS_ENTRIES);
    }
    string_copy((string_address)line, (string_address) "a /b /c ");
    for (at = 8; at < SETTING_PINS_MOST + 1; at++)
        line[at] = 'x';
    line[SETTING_PINS_MOST + 1] = 0;
    long_ok = !setting_pins_valid(line, &bad_at) && bad_at == SETTING_PINS_MOST;
    check("taskbar pinned: lines of programs are lines", grammar);
    check("taskbar pinned: a line with an app_id with a slash, an entry of two words, a word that is no path, an unclosed quote, a byte below a blank or a stray ; is none", refused);
    check("taskbar pinned: a word with blanks, a quote, a semicolon, nothing or a backslash is written in quotes and read back as it was", quoted);
    check("taskbar pinned: at most 16 programs and 2048 bytes", many_ok && long_ok);

    // The programs as a line says them, and the line they make: the same, the title the last part of the app_id.
    taskbar_pins_set("org.mozilla.firefox /usr/lib/firefox/firefox /usr/lib/firefox/firefox --new-window 'a b' ; settings /settings /settings");
    check("taskbar pinned: a line is two programs, their words and their titles", taskbar_pins.count == 2 && string_equals((string_address)taskbar_pins.pin[0].app, (string_address) "org.mozilla.firefox") &&
          string_equals((string_address)taskbar_pins.pin[0].title, (string_address) "firefox") && taskbar_pins.pin[0].count == 5 &&
          string_equals((string_address)taskbar_pin_word(&taskbar_pins.pin[0], 3), (string_address) "--new-window") &&
          string_equals((string_address)taskbar_pin_word(&taskbar_pins.pin[0], 4), (string_address) "a b") && string_equals((string_address)taskbar_pins.pin[1].title, (string_address) "settings"));
    taskbar_pins_line(line, sizeof(line), null, null);
    check("taskbar pinned: the line they make is the line they came from", string_equals((string_address)line, (string_address) "org.mozilla.firefox /usr/lib/firefox/firefox /usr/lib/firefox/firefox --new-window 'a b' ; settings /settings /settings"));
    taskbar_pins_set("a /b /c ; a b");
    check("taskbar pinned: a line with a fault is none of them, and so is none", !taskbar_pins.count);
    taskbar_pins_set("none");
    check("taskbar pinned: none is none", !taskbar_pins.count);

    // The fake system: /proc, the settings and /run under a directory of the check's own.
    string_copy((string_address)root, (string_address) "/tmp/moonwater-check-pins-");
    positive_into_string((p8 *)root + string_length((string_address)root), (positive)system_call(syscall(getpid)));
    tab_remove_tree(root);
    tab_make_dir(root);
    for (int each = 0; each < 5; each++)
    {
        static const char *const names[5] = {"/proc", "/root", "/run", "/bin", "/proc/self"};

        string_copy((string_address)path, (string_address)root), string_append_bounded((string_address)path, (string_address)names[each], sizeof(path));
        tab_make_dir(path);
    }
    string_copy((string_address)second, (string_address)root), string_append_bounded((string_address)second, (string_address) "/proc", sizeof(second));
    {
        static char proc_root[112], run_root[112], bowl_bin[112], settings_root[112];
        char own[64];
        long got = (long)compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive) "/proc/self/ns/mnt", (positive)own, sizeof(own) - 1, 0, 0);

        own[got > 0 ? got : 0] = 0;
        string_copy((string_address)proc_root, (string_address)second);
        string_copy((string_address)run_root, (string_address)root), string_append_bounded((string_address)run_root, (string_address) "/run", sizeof(run_root));
        string_copy((string_address)bowl_bin, (string_address)root), string_append_bounded((string_address)bowl_bin, (string_address) "/bin/", sizeof(bowl_bin));
        string_copy((string_address)settings_root, (string_address)root);
        taskbar_proc_root = proc_root, taskbar_run_root = run_root, taskbar_bowl_bin = bowl_bin, compositor_settings_root = settings_root;
        // Processes: 61 a program as it was started (a bare name, arguments with a blank and a quote), 62 a kit program, 63 and 64 a bowl's,
        // 65 with a newline in an argument, 66 started by a relative path, 67 whose command line is longer than is read, 68 with none.
        tab_fake_process(proc_root, 61, "firefox", 40, "/usr/lib/firefox/firefox", "firefox\0--new-window\0a b\0it's\0", 30, own);
        tab_fake_process(proc_root, 62, "settings", 1, "/settings", "/settings\0-f\0/x.ttf\0", 20, own);
        tab_fake_process(proc_root, 63, "bowlapp", 40, "/usr/bin/bowlapp", "bowlapp\0--x\0", 12, "mnt:[1]");
        tab_fake_process(proc_root, 64, "lostapp", 40, "/usr/bin/lostapp", "/usr/bin/lostapp\0", 17, "mnt:[1]");
        tab_fake_process(proc_root, 65, "newline", 40, "/usr/bin/newline", "newline\0a\nb\0", 12, own);
        tab_fake_process(proc_root, 66, "tool", 40, "/opt/tool/tool", "./tool\0go\0", 10, own);
        tab_fake_process(proc_root, 68, "none", 40, "/usr/bin/none", null, 0, own);
        {
            static char big[2400];

            for (int each = 0; each < 2200; each++)
                big[each] = each % 8 == 7 ? 0 : 'a';
            tab_fake_process(proc_root, 67, "huge", 40, "/usr/bin/huge", big, 2200, own);
        }
        string_copy((string_address)path, (string_address)bowl_bin), string_append_bounded((string_address)path, (string_address) "bowlapp", sizeof(path));
        tab_make_file(path, "#!/bowl @/bowls/arch /usr/bin/bowlapp\n", 38);
    }
    taskbar_pins_set("none");
    string_copy((string_address)compositor.app_id[5], (string_address) "firefox"), string_copy((string_address)compositor.app_id[6], (string_address) "settings");
    string_copy((string_address)compositor.app_id[7], (string_address) "bowlapp"), string_copy((string_address)compositor.app_id[8], (string_address) "lostapp");
    string_copy((string_address)compositor.app_id[9], (string_address) "newline"), string_copy((string_address)compositor.app_id[10], (string_address) "tool");
    string_copy((string_address)compositor.app_id[11], (string_address) "huge"), string_copy((string_address)compositor.app_id[12], (string_address) "none");
    string_copy((string_address)compositor.app_id[13], (string_address) "a/b");
    {
        static const char *const native[] = {"firefox", "/usr/lib/firefox/firefox", "/usr/lib/firefox/firefox", "--new-window", "a b", "it's"};
        static const char *const kit[] = {"settings", "/settings", "/settings"};
        const char *bowl[4] = {"bowlapp", "/usr/bin/bowlapp", "", "--x"};
        static const char *const relative[] = {"tool", "/opt/tool/tool", "/opt/tool/tool", "go"};
        char launcher[200];

        string_copy((string_address)launcher, (string_address)taskbar_bowl_bin), string_append_bounded((string_address)launcher, (string_address) "bowlapp", sizeof(launcher));
        bowl[2] = launcher;
        check("taskbar pinned: a program is pinned by its app_id, its executable and its command line, the first word its executable's when it was a bare name",
              taskbar_pin_capture(5, 61, &pin) && tab_words_are(&pin, 6, native) && string_equals((string_address)pin.title, (string_address) "firefox"));
        check("taskbar pinned: a kit program is pinned as its path alone", taskbar_pin_capture(6, 62, &pin) && tab_words_are(&pin, 3, kit));
        check("taskbar pinned: a program of another mount view is pinned by the launcher its bowl exposed, and by none when it has none",
              taskbar_pin_capture(7, 63, &pin) && tab_words_are(&pin, 4, bowl) && !taskbar_pin_capture(8, 64, &other));
        check("taskbar pinned: a command line with a byte below a blank, one that is cut, one that is not there and an app_id that is none are not pinned",
              !taskbar_pin_capture(9, 65, &other) && !taskbar_pin_capture(11, 67, &other) && !taskbar_pin_capture(12, 68, &other) && !taskbar_pin_capture(13, 61, &other));
        check("taskbar pinned: a program started by a relative path is pinned by its executable", taskbar_pin_capture(10, 66, &pin) && tab_words_are(&pin, 4, relative));
    }

    // The menu on the fixture of the tray's, with one window (61, firefox).
    compositor_zero();
    taskbar_classic();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    compositor.face = band_face, compositor.engine = &band_engine;
    if (!glass_table_map())
    {
        check("taskbar pinned: the table is mapped", false);
        taskbar_standard();
        return;
    }
    table = compositor.table;
    // One window, the band is no wider than a pinned program's task is to have room beside it.
    table->x[1] = 80;
    table->y[1] = 20;
    table->width[1] = 50;
    table->height[1] = 60;
    glass_paint(1, 0, 0, 50, 60, 0x404041u, true);
    windows_app_id_set(table, 1, "firefox");
    table->pid[1] = 61;
    compositor_identity_take(1);
    compositor_present();
    compositor_focus(1);
    taskbar_signal = tab_signal_note;
    tab_signal_write(taskbar_proc_root, 40, "moonwater", 1), tab_signal_write(taskbar_proc_root, 1, "init", 0);

    view = compositor_band_view();
    tab = -1;
    for (unsigned int each = 0; each < view.count; each++)
        tab = compositor.listed[each] == 1 ? (int)each : tab;
    // A right press on firefox's tab: Exit, Force stop, and Pin to taskbar.
    if (tab >= 0 && taskbar_slot(&view, tab, &x, &y, &w, &h))
    {
        compositor.pointer_x = x + w / 2, compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
    }
    check("taskbar pinned: a window's menu has Pin to taskbar after Exit and Force stop", taskbar_menu.open && taskbar_menu.menu.count == 4 &&
          taskbar_menu.menu.items[0].id == TASKBAR_MENU_EXIT && taskbar_menu.menu.items[1].id == TASKBAR_MENU_STOP && taskbar_menu.menu.items[3].id == TASKBAR_MENU_PIN &&
          string_equals((string_address)taskbar_menu.menu.items[3].label, (string_address) "Pin to taskbar"));
    if (taskbar_menu.open)
        compositor_menu_chose(TASKBAR_MENU_PIN);
    {
        char kept[SETTING_PINS_MOST + 16];
        bipolar got;

        taskbar_pins_path(path, sizeof(path));
        got = file_slurp_regular_at(AT_FDCWD, (string_address)path, (p8 *)kept, sizeof(kept) - 1, 0);
        kept[got > 0 ? got : 0] = 0;
        check("taskbar pinned: Pin to taskbar keeps the program in the file of the verb, as the line it is, and as the pinned program",
              got > 0 && string_equals((string_address)kept, (string_address) "firefox /usr/lib/firefox/firefox /usr/lib/firefox/firefox --new-window 'a b' 'it'\\''s'\n") &&
              taskbar_pins.count == 1 && string_equals((string_address)taskbar_pins.pin[0].app, (string_address) "firefox"));
    }
    // With its window there the program has no task of its own; its window's menu says Unpin from taskbar.
    view = compositor_band_view();
    ghost = view.count;
    if (tab >= 0 && taskbar_slot(&view, tab, &x, &y, &w, &h))
    {
        compositor.pointer_x = x + w / 2, compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
    }
    check("taskbar pinned: a pinned program with a window has no task of its own, and its window's menu says Unpin from taskbar",
          ghost == 1 && taskbar_menu.open && taskbar_menu.menu.count == 4 && taskbar_menu.menu.items[3].id == TASKBAR_MENU_UNPIN &&
              string_equals((string_address)taskbar_menu.menu.items[3].label, (string_address) "Unpin from taskbar"));
    compositor_menu_close();

    // The window gone (another program in its row): the pinned program is a task after the windows', drawn as one put away.
    windows_app_id_set(table, 1, "other");
    compositor_identity_take(1);
    view = compositor_band_view();
    check("taskbar pinned: with no window of its app_id a pinned program is a task after the windows', away, titled by its program",
          view.count == 2 && compositor.listed[1] == TASKBAR_GHOST && (view.marks[1] & COMPOSITOR_TAB_AWAY) && string_equals((string_address)view.titles[1], (string_address) "firefox") &&
              string_equals((string_address)view.apps[1], (string_address) "firefox") && taskbar_pin_of_tab(1) == 0);
    ghost = taskbar_slot(&view, 1, &x, &y, &w, &h);
    check("taskbar pinned: that task has its place in the band", ghost);
    if (ghost)
    {
        compositor.pointer_x = x + w / 2, compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
    }
    check("taskbar pinned: its menu has Unpin from taskbar alone", ghost && taskbar_menu.open && taskbar_menu.menu.count == 1 &&
          taskbar_menu.menu.items[0].id == TASKBAR_MENU_UNPIN);
    compositor_menu_close();

    // A left press on it starts the program: its words a line each in a file new to the run directory, and not again at once.
    if (ghost)
    {
        char started[200], buffer[1024], content[400];
        bipolar handle, got;
        int files = 0, again = 0;

        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        content[0] = 0;
        handle = system_open_at(AT_FDCWD, (string_address)taskbar_run_root, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        got = handle >= 0 ? system_read_directory(handle, buffer, sizeof(buffer)) : 0;
        for (positive each = 0; got > 0 && each < (positive)got;)
        {
            const struct linux_dirent64 address_to record = (const struct linux_dirent64 address_to)(buffer + each);

            each += record->d_reclen;
            if (!string_compare_max((string_address)record->d_name, (string_address) "pin.", 4))
            {
                bipolar length;

                files++;
                string_copy((string_address)started, (string_address)taskbar_run_root), string_append_bounded((string_address)started, (string_address) "/", sizeof(started));
                string_append_bounded((string_address)started, (string_address)record->d_name, sizeof(started));
                length = file_slurp_regular_at(AT_FDCWD, (string_address)started, (p8 *)content, sizeof(content) - 1, 0);
                content[length > 0 ? length : 0] = 0;
                system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)started, 0);
            }
        }
        if (handle >= 0)
            system_close(handle);
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        handle = system_open_at(AT_FDCWD, (string_address)taskbar_run_root, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        got = handle >= 0 ? system_read_directory(handle, buffer, sizeof(buffer)) : 0;
        for (positive each = 0; got > 0 && each < (positive)got;)
        {
            const struct linux_dirent64 address_to record = (const struct linux_dirent64 address_to)(buffer + each);

            each += record->d_reclen;
            again += !string_compare_max((string_address)record->d_name, (string_address) "pin.", 4);
        }
        if (handle >= 0)
            system_close(handle);
        check("taskbar pinned: a left press on the task of a pinned program that has no window leaves its words, a line each, in a new file, and once", files == 1 &&
              string_equals((string_address)content, (string_address) "/usr/lib/firefox/firefox\n--new-window\na b\nit's\n") && !again);
    }

    // Unpin from the task, and the file says none; and the file read as the settings are (the pinned programs are what it says).
    if (ghost)
    {
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
        if (taskbar_menu.open)
            compositor_menu_chose(TASKBAR_MENU_UNPIN);
    }
    {
        char kept[64];
        bipolar got;

        taskbar_pins_path(path, sizeof(path));
        got = file_slurp_regular_at(AT_FDCWD, (string_address)path, (p8 *)kept, sizeof(kept) - 1, 0);
        kept[got > 0 ? got : 0] = 0;
        check("taskbar pinned: Unpin from taskbar takes the program from the line, which says none, and the task goes", !taskbar_pins.count &&
              string_equals((string_address)kept, (string_address) "none\n") && compositor_band_view().count == 1);
    }
    tab_make_file(path, "settings /settings /settings\n", 29);
    taskbar_settings_set("bottom", "primary", "");
    check("taskbar pinned: the settings read the file, so the verb and the menu agree", taskbar_pins.count == 1 && string_equals((string_address)taskbar_pins.pin[0].app, (string_address) "settings"));
    // A window that is no program's own (pid 1, the machine's) is not pinned from.
    (void)pids;
    taskbar_pins_set("none");

    tab_remove_tree(root);
    taskbar_proc_root = "/proc", taskbar_run_root = "/run/moonwater", taskbar_bowl_bin = "/bowls/bin/", compositor_settings_root = "";
    taskbar_signal = taskbar_signal_send;
    taskbar_standard();
}

/*
        The panel's launcher button pressed. With the button on, a left press on it asks the script for the launcher (launcher_button)
        and writes the rectangle it opens beside, the panel's edge and the screen in the window table first; a press of another button
        does nothing; a press while the launcher is up puts it away and asks for no other; and the button is lit while the launcher is
        up. With the button off the same place is the empty desktop, and nothing is asked.
*/
static void check_compositor_launcher(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    static const char *const words[4] = {"bottom", "top", "left", "right"};
    struct windows address_to table;
    struct taskbar_view view;
    int ok_asked = 1, ok_table = 1, ok_other = 1, ok_toggle = 1, ok_lit = 1, ok_off = 1;

    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
    for (int at = 0; at < 4; at++)
    {
        int x, y, w, h, rect[4];

        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = screen;
        compositor.scanout_pitch = GLASS_SIDE * 4;
        if (!glass_table_map())
        {
            check("launcher button press: the table is mapped", false);
            return;
        }
        table = compositor.table;
        taskbar_tray_set("");
        taskbar_settings_set(words[at], "primary", "battery none\n");
        taskbar_launcher_set(1);
        table->x[1] = 60;
        table->y[1] = 60;
        table->width[1] = 100;
        table->height[1] = 60;
        glass_paint(1, 0, 0, 100, 60, 0x404040u, true);
        compositor_present();
        compositor_focus(1);

        view = compositor_band_view();
        if (!taskbar_slot(&view, TASKBAR_LAUNCHER, &x, &y, &w, &h) || !taskbar_anchor(&view, TASKBAR_LAUNCHER, &rect[0], &rect[1], &rect[2], &rect[3]))
        {
            check("launcher button press: the band has a button", false);
            return;
        }
        compositor.pointer_x = x + w / 2;
        compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();

        // A left press asks for the launcher once, and says where beside.
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        ok_asked &= compositor.launcher_asked == 1 && compositor.focus == 1 && !compositor.settings_asked;
        ok_table &= table->button_x == rect[0] && table->button_y == rect[1] && table->button_w == (unsigned int)rect[2] &&
                    table->button_h == (unsigned int)rect[3] && table->button_edge == taskbar_position() && table->button_screen == 0;

        // Another button, and the button of a pointer that is off the button, ask for nothing.
        compositor_button(COMPOSITOR_BTN_RIGHT, 1);
        compositor_button(COMPOSITOR_BTN_RIGHT, 0);
        compositor_button(COMPOSITOR_BTN_MIDDLE, 1);
        compositor_button(COMPOSITOR_BTN_MIDDLE, 0);
        compositor.pointer_x = GLASS_SIDE / 2;
        compositor.pointer_y = GLASS_SIDE / 2 - 20;
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        ok_other &= compositor.launcher_asked == 1;
        compositor.pointer_x = x + w / 2;
        compositor.pointer_y = y + h / 2;
        compositor_pointer_clamp();

        // The launcher up (a glass window with no close button that asks to be put away by a press elsewhere, which has the keys):
        // the button is lit, and a press on it puts the launcher away and asks for no other.
        view = compositor_band_view();
        ok_lit &= !view.launcher_lit && compositor_launcher_row() < 0;
        table->x[2] = 100;
        table->y[2] = 100;
        table->width[2] = 80;
        table->height[2] = 50;
        table->glass[2] = WINDOW_GLASS | WINDOW_NO_CLOSE | WINDOW_DISMISS;
        glass_paint(2, 0, 0, 80, 50, 0, true);
        compositor_present();
        compositor_focus(2);
        view = compositor_band_view();
        ok_lit &= compositor_launcher_row() == 2 && view.launcher_lit;
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        ok_toggle &= (table->state[2] & WINDOW_CLOSING) && compositor.launcher_asked == 1;
        view = compositor_band_view();
        ok_lit &= compositor_launcher_row() < 0 && !view.launcher_lit;

        // Gone, a press asks again.
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        ok_toggle &= compositor.launcher_asked == 2;

        // With the button off the same place asks nothing.
        taskbar_launcher_set(0);
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);
        ok_off &= compositor.launcher_asked == 2;
        compositor_call(syscall(munmap), (positive)table,
                        ((sizeof(struct windows) + 4095) & ~4095ul) + (((unsigned long)GLASS_SIDE * GLASS_SIDE * 4 + 4095) & ~4095ul) * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
    }
    check("launcher button press: a left press on it asks the script for the launcher once, at each position of the panel", ok_asked);
    check("launcher button press: and the table holds the rectangle it opens beside, the panel's edge and the screen", ok_table);
    check("launcher button press: another button, or a press off the button, asks for nothing", ok_other);
    check("launcher button press: with the launcher up a press on it puts the launcher away and asks for no other, and gone it asks again", ok_toggle);
    check("launcher button press: the button is lit while the launcher is up and not before or after", ok_lit);
    check("launcher button press: with the button off the place where it was asks nothing", ok_off);
    taskbar_launcher_set(0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
    for (int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor_popup_parent[row] = -1;
}

/*
        The pointer's motion, as a window hears it. A window that said it wants
        none (windows_hover) is told nothing of a motion over it, and the program
        is not woken for it; one held down, or pressed over, is told all the same.
*/
static void check_compositor_hover(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int before;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("hover: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_present();

    /* Each motion is read by the program before the next: a motion behind an unread one replaces it. */
    compositor.pointer_x = 55;
    compositor.pointer_y = 40;
    table->hover_off[1] = 1;
    before = table->event_head[1];
    compositor_motion();
    check("hover: a window that asked for none is not told of a motion over it", table->event_head[1] == before);

    table->hover_off[1] = 0;
    compositor.pointer_x = 56;
    compositor_motion();
    check("hover: a window that wants the motion is told of it", table->event_head[1] == before + 1);
    table->event_tail[1] = table->event_head[1];

    table->hover_off[1] = 1;
    compositor.pointer_x = 57;
    compositor.grab = 1;
    compositor_motion();
    check("hover: a window with a button held over it is told of the motion", table->event_head[1] == before + 2);
    table->event_tail[1] = table->event_head[1];
    compositor.grab = COMPOSITOR_NONE;
    compositor.pointer_x = 58;
    compositor_motion();
    check("hover: and nothing once the button is let go", table->event_head[1] == before + 2);

    /* A release that a full ring dropped is owed to the window and goes when the program has caught up: it was left holding the press
       (a thumb, a pull never let go) for good. */
    {
        unsigned int last;
        int dropped, landed, once;

        table->event_tail[1] = table->event_head[1];
        while (table->event_head[1] - table->event_tail[1] < WINDOWS_EVENTS)
            compositor_post(1, WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 30, 'a');
        dropped = !compositor_post(1, WINDOW_EVENT_BUTTON, 0, COMPOSITOR_BTN_LEFT, 0);
        compositor_owed_pass();
        dropped = dropped && table->event_head[1] - table->event_tail[1] == WINDOWS_EVENTS;
        table->event_tail[1] = table->event_head[1];
        compositor_owed_pass();
        last = (table->event_head[1] - 1) % WINDOWS_EVENTS;
        landed = table->event_head[1] - table->event_tail[1] == 1 && table->event_type[1][last] == WINDOW_EVENT_BUTTON &&
                 !(table->event_flags[1][last] & WINDOW_KEY_DOWN) && table->event_a[1][last] == COMPOSITOR_BTN_LEFT;
        compositor_owed_pass();
        once = table->event_head[1] - table->event_tail[1] == 1;
        table->event_tail[1] = table->event_head[1];
        check("input: a button's release that a full ring dropped is posted once the program has caught up, and once", dropped && landed && once);
    }

    /* A key's release that a full ring dropped is owed the same way, and goes before the next event of the row: the key was left down
       (repeating, or a modifier held on every key after it) for good. */
    {
        unsigned int at;
        int dropped, landed, after;

        table->event_tail[1] = table->event_head[1];
        while (table->event_head[1] - table->event_tail[1] < WINDOWS_EVENTS)
            compositor_post(1, WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 30, 'a');
        dropped = !compositor_post(1, WINDOW_EVENT_KEY, WINDOW_KEY_SHIFT, 42, 0);
        table->event_tail[1] = table->event_head[1];
        compositor_post(1, WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 30, 'a');
        at = table->event_tail[1] % WINDOWS_EVENTS;
        landed = table->event_head[1] - table->event_tail[1] == 2 && table->event_type[1][at] == WINDOW_EVENT_KEY &&
                 (table->event_flags[1][at] & (WINDOW_KEY_SHIFT | WINDOW_KEY_DOWN)) == WINDOW_KEY_SHIFT && table->event_a[1][at] == 42;
        at = (table->event_tail[1] + 1) % WINDOWS_EVENTS;
        after = table->event_a[1][at] == 30 && (table->event_flags[1][at] & WINDOW_KEY_DOWN);
        table->event_tail[1] = table->event_head[1];
        compositor_owed_pass();
        check("input: a key's release that a full ring dropped is posted before the row's next event, with its flags, and once",
              dropped && landed && after && table->event_head[1] == table->event_tail[1]);
    }

    /* A device's queue the kernel dropped (SYN_DROPPED) may have held the releases: what the compositor holds down that no device has
       down is let go (a pipe stands for the device, and its state, which no pipe has, is nothing down). The window was left with Shift
       and a key held, and the press of the button holding every motion and turn of the wheel to it. */
    {
        int device[2] = {-1, -1};
        b32 held, released, key_up = false, shift_up = false, button_up = false;

        if (system_call_2(syscall(pipe2), (positive)device, O_NONBLOCK))
        {
            check("input: the device is made", false);
            return;
        }
        compositor.inputs[0] = device[0];
        compositor.input_count = 1;
        compositor.pointer_x = 60, compositor.pointer_y = 90;
        compositor_focus(1);
        table->event_tail[1] = table->event_head[1];
        {
            struct compositor_input_event down[5] = {{0, 0, COMPOSITOR_EV_KEY, (unsigned short)COMPOSITOR_BTN_LEFT, 1},
                                                     {0, 0, COMPOSITOR_EV_SYN, 0, 0},
                                                     {0, 0, COMPOSITOR_EV_KEY, 42, 1},
                                                     {0, 0, COMPOSITOR_EV_KEY, 30, 1},
                                                     {0, 0, COMPOSITOR_EV_SYN, 0, 0}};

            system_write_once(device[1], down, sizeof down);
        }
        compositor_input_read(0);
        held = (compositor.button_down & 1) && compositor.grab == 1 && (compositor_modifiers() & WINDOW_KEY_SHIFT) &&
               (compositor.keys_down[30 >> 3] >> (30 & 7) & 1);
        table->event_tail[1] = table->event_head[1];
        {
            struct compositor_input_event dropped[2] = {{0, 0, COMPOSITOR_EV_SYN, (unsigned short)COMPOSITOR_SYN_DROPPED, 0},
                                                        {0, 0, COMPOSITOR_EV_SYN, 0, 0}};

            system_write_once(device[1], dropped, sizeof dropped);
        }
        compositor_input_read(0);
        for (unsigned int at = table->event_tail[1]; at != table->event_head[1]; at++)
        {
            unsigned int cell = at % WINDOWS_EVENTS, flags = table->event_flags[1][cell];

            if (table->event_type[1][cell] == WINDOW_EVENT_KEY && !(flags & WINDOW_KEY_DOWN))
            {
                key_up |= table->event_a[1][cell] == 30 && !(flags & WINDOW_KEY_SHIFT);
                shift_up |= table->event_a[1][cell] == 42;
            }
            button_up |= table->event_type[1][cell] == WINDOW_EVENT_BUTTON && !(flags & WINDOW_KEY_DOWN) &&
                         table->event_a[1][cell] == COMPOSITOR_BTN_LEFT;
        }
        released = !compositor.button_down && compositor.grab == COMPOSITOR_NONE && !(compositor_modifiers() & WINDOW_KEY_SHIFT) &&
                   !(compositor.keys_down[30 >> 3] >> (30 & 7) & 1);
        check("input: a device's dropped reports let go of what no device has down: the button where it was pressed, Shift and the key",
              held && released && key_up && shift_up && button_up);
        table->event_tail[1] = table->event_head[1];
        compositor.inputs[0] = -1;
        compositor.input_count = 0;
        system_close(device[0]);
        system_close(device[1]);
    }
}

/*
        The prediction a frame ahead (compositor.c's predict). The mouse's cursor is drawn at its pointer led by one frame of its
        last two places' motion, no further than the cap, and its clicks are where the pointer is; a finger or a pen is delivered
        where it will be. A stopped hand is back on its newest place once a frame has gone by; a lifted finger, or a pen out of
        range, has no lead; and touch and pen honour their row (the mouse is always predicted).
*/
static struct windows address_to predict_setup(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    compositor.cursor.soft = 1;
    compositor.period = 1000000000ull / 60;
    if (!glass_table_map())
        return null;
    table = compositor.table;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_present();
    return table;
}

// The mouse's report moved the hand to x, y. The mouse is not predicted (its lead is always zero), so the pointer is that
// place, and the cursor and the window under it are told it. The time is not used.
static void predict_mouse_to(int x, int y, positive now)
{
    (void)now;
    compositor.real_x = x;
    compositor.real_y = y;
    compositor_predict_mouse(0, 0);
    compositor_motion();
}

// A finger's device reported the place x, y (in 1/1024 px) at when, read by the compositor at now: the same fit the pointer's lead
// takes, with the finger as the owner. The pointer is that place led; the window under it is not told.
static void predict_finger_fx(long long x, long long y, positive when, positive now)
{
    compositor_pointer_sync();
    compositor.owner = COMPOSITOR_PREDICT_TOUCH;
    compositor.real_x = (int)(x / COMPOSITOR_PREDICT_FX);
    compositor.real_y = (int)(y / COMPOSITOR_PREDICT_FX);
    compositor_predict_sample(COMPOSITOR_PREDICT_TOUCH, x, y, when, now);
    compositor_pointer_shown();
}

// A finger at x, y, reported and read at now, and the window under it told.
static void predict_finger_to(int x, int y, positive now)
{
    predict_finger_fx((long long)x * COMPOSITOR_PREDICT_FX, (long long)y * COMPOSITOR_PREDICT_FX, now, now);
    compositor_motion();
}

// The lead of a steady finger moving step pixels every 10 ms at a frame of period, after two places, in pixels.
static int predict_lead_at(positive period, int step)
{
    const positive when = 5000000000ull, ms = 1000000ull;

    compositor.period = period;
    memory_zero(compositor.predict, sizeof(compositor.predict));
    predict_finger_to(60, 50, when);
    predict_finger_to(60 + step, 50, when + 10 * ms);
    return compositor.pointer_x - compositor.real_x;
}

static void check_compositor_predict(void)
{
    struct windows address_to table = predict_setup();
    const positive t0 = 1000000000ull, ms = 1000000ull;
    long value;
    int lead_x, lead_y;

    if (!table)
    {
        check("predict: the table is mapped", false);
        return;
    }

    /* The mouse is not predicted: a moving mouse has no lead at all, its cursor is on its place, and it is never sampled. */
    predict_mouse_to(100, 50, t0);
    check("predict: a first place is the cursor's place", compositor.cursor.x == 100 && compositor.cursor.y == 50);
    predict_mouse_to(102, 50, t0 + 10 * ms);
    check("predict: a moving mouse produces no lead: the cursor and the pointer are on its real place, 102",
          compositor.cursor.x == 102 && compositor.pointer_x == 102 && compositor.real_x == 102 && compositor.cursor.y == 50);
    predict_mouse_to(112, 50, t0 + 20 * ms);
    check("predict: a fast mouse produces no lead either, so no cap is reached", compositor.cursor.x == 112 && compositor.pointer_x == 112);
    check("predict: the mouse keeps no places, so it has no lead to hold or expire", compositor.predict[COMPOSITOR_PREDICT_MOUSE].count == 0);

    /* Through the report, as the mouse's counts come in: the same, with no lead. */
    compositor.flat = 1;
    compositor.sensitivity = 100;
    compositor.raw_x = 3, compositor.raw_y = 0;
    compositor_report(t0 + 30 * ms, t0 + 30 * ms);
    compositor_motion();
    check("predict: a mouse report moves the cursor with the pointer, and the pointer is the real place",
          compositor.cursor.x == compositor.pointer_x && compositor.pointer_x == compositor.real_x && compositor.pointer_x > 112);

    /* A diagonal mouse: no lead in either axis. */
    memory_zero(compositor.predict, sizeof(compositor.predict));
    predict_mouse_to(20, 60, t0 + 100 * ms);
    predict_mouse_to(30, 70, t0 + 110 * ms);
    lead_x = compositor.pointer_x - compositor.real_x;
    lead_y = compositor.pointer_y - compositor.real_y;
    check("predict: a diagonal mouse produces no lead in either axis", lead_x == 0 && lead_y == 0 && compositor.cursor.x == 30 && compositor.cursor.y == 70);

    /* A frame at 120 Hz: the same 2 px in 10 ms is 1 px. The distance a steady motion is led, in pixels, at 60 and at 120 Hz. */
    check("predict: the frame-ahead distance at 60 Hz is 2, 3 and the cap 4 px for 1, 2 and 5 px in 10 ms (1.7, 3.3 and past the cap)",
          predict_lead_at(1000000000ull / 60, 1) == 2 && predict_lead_at(1000000000ull / 60, 2) == 3 &&
              predict_lead_at(1000000000ull / 60, 5) == 4);
    check("predict: the frame-ahead distance at 120 Hz is 1, 2 and the cap 4 px for 1, 2 and 5 px in 10 ms (0.8, 1.7 and past the cap)",
          predict_lead_at(1000000000ull / 120, 1) == 1 && predict_lead_at(1000000000ull / 120, 2) == 2 &&
              predict_lead_at(1000000000ull / 120, 5) == 4);
    compositor.period = 1000000000ull / 60;

    /* A finger: its place is where it will be, so the window it reaches is told the led place; the press is at it too. */
    memory_zero(compositor.predict, sizeof(compositor.predict));
    compositor_touch_motion(100, 60, t0 + 200 * ms);
    compositor_touch_motion(102, 60, t0 + 210 * ms);
    check("predict: a finger is delivered where it will be: 3 px past its last place, and the window is told 55 across it",
          compositor.pointer_x == 105 && compositor.told_x == 55 && compositor.told_row == 1);
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
    check("predict: a finger's press is delivered where it will be too", compositor.told_x == 55 && compositor.pointer_x == 105);
    compositor_predict_clear(COMPOSITOR_PREDICT_TOUCH);
    check("predict: a finger that lifts drops its lead at once: the windows are told its last place", compositor.predict[COMPOSITOR_PREDICT_TOUCH].lead_x == 0 &&
          compositor.pointer_x == 102 && compositor.told_x == 52);
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);

    /* A mouse's click is at its place, which is where its cursor is: the mouse has no lead. The hand is in the window's body. */
    predict_mouse_to(102, 70, t0 + 300 * ms);
    predict_mouse_to(104, 70, t0 + 310 * ms);
    check("predict: the mouse's cursor is on its real place, with no lead",
          compositor.cursor.x == 104 && compositor.pointer_x == 104 && compositor.real_x == 104);
    compositor_press(COMPOSITOR_BTN_LEFT, 1);
    check("predict: a mouse's click is delivered at its place, 54 in and 40 down", compositor.told_x == 54 && compositor.told_y == 40 &&
          compositor.cursor.x == 104);
    compositor_press(COMPOSITOR_BTN_LEFT, 0);

    /* A pen is delivered where it will be, and a pen out of range has no lead. */
    memory_zero(compositor.predict, sizeof(compositor.predict));
    compositor_predict_place(COMPOSITOR_PREDICT_PEN, 60, 90, t0 + 400 * ms);
    compositor_predict_place(COMPOSITOR_PREDICT_PEN, 62, 90, t0 + 410 * ms);
    check("predict: a pen is delivered 3 px past its last place", compositor.pointer_x == 65 && compositor.pointer_y == 90);
    compositor_predict_clear(COMPOSITOR_PREDICT_PEN);
    check("predict: a pen out of range drops its lead at once", compositor.pointer_x == 62 && compositor.predict[COMPOSITOR_PREDICT_PEN].lead_x == 0);

    /* The touch row: off leaves a finger on its last place, and the mouse is led all the same (it has no row). */
    memory_zero(compositor.predict, sizeof(compositor.predict));
    compositor_touch_motion(200, 60, t0 + 520 * ms);
    compositor_touch_motion(202, 60, t0 + 530 * ms);
    check("predict: a finger is led while its row is on", compositor.pointer_x == 205);
    compositor_predict_set(COMPOSITOR_PREDICT_TOUCH, 0);
    compositor_touch_motion(204, 60, t0 + 540 * ms);
    check("predict: with touch off a finger is not led", compositor.pointer_x == 204 && compositor.predict[COMPOSITOR_PREDICT_TOUCH].lead_x == 0);
    predict_mouse_to(110, 50, t0 + 550 * ms);
    predict_mouse_to(112, 50, t0 + 560 * ms);
    check("predict: with touch off the mouse is on its pointer, as it always is", compositor.cursor.x == 112);
    compositor_predict_set(COMPOSITOR_PREDICT_TOUCH, 1);
    memory_zero(compositor.predict, sizeof(compositor.predict));

    /* The touch row: live to the compositor, on by default, and off is the word that turns it off. The mouse has no row. */
    {
        const struct settings_row address_to touch = setting_by_path((string_address)SETTING_PREDICT_TOUCH_PATH);

        check("predict: the touch row is a live switch, on by default, that a word of off turns off",
              touch && touch->kind == SETTINGS_SWITCH && touch->live == SETTING_LIVE_COMPOSITOR && touch->how == SETTING_PLAIN &&
                  string_equals((string_address)touch->standard, (string_address)"on") && !setting_kept(touch, (string_address)"", &value) &&
                  value == 0 && setting_kept(touch, (string_address)"off", &value) && value == 1);
    }
}

/*
        The prediction at the root of input: a window the hand takes is moved to where the pointer is led, so a steady
        mouse's window ends where the prediction says; a press or a release is at the real place, the one the hand is on,
        so a click lands where the hand was even while the pointer is led past it.
*/
static void check_compositor_predict_drag(void)
{
    struct windows address_to table = predict_setup();
    const positive t0 = 3000000000ull, ms = 1000000ull;

    if (!table)
    {
        check("predict drag: the table is mapped", false);
        return;
    }
    memory_zero(compositor.predict, sizeof(compositor.predict));
    compositor.x[1] = 50, compositor.y[1] = 30, compositor.w[1] = 120, compositor.h[1] = 80;
    compositor.showing[1] = 1;
    compositor.stack[0] = 1, compositor.stacked = 1;

    /* A press in the title band takes the window; the hand then moves 2 px in each 10 ms, and the window moves with the pointer. */
    predict_mouse_to(80, 34, t0);
    compositor_press(COMPOSITOR_BTN_LEFT, 1);
    check("predict drag: a press in a window's title takes the window in the hand", compositor.drag == 1);
    for (int i = 1; i <= 5; i++)
        predict_mouse_to(80 + 2 * i, 34, t0 + i * 10 * ms);
    check("predict drag: a window dragged by a steady mouse is where the hand is: the hand and the pointer are at 90, the grip 30 in, so the window is 60 across",
          compositor.real_x == 90 && compositor.pointer_x == 90 && compositor.x[1] == 60);

    /* The release is a click at the real place: the window is let go where the hand is, 60 across. */
    compositor_press(COMPOSITOR_BTN_LEFT, 0);
    check("predict drag: a release that lets the window go is at the real place, so it is 60 across", compositor.drag == COMPOSITOR_NONE && compositor.x[1] == 60);

    /* A steady mouse over the window's picture, pressed: the window is told the press where the hand is, 44 in, not 47. */
    memory_zero(compositor.predict, sizeof(compositor.predict));
    predict_mouse_to(100, 60, t0 + 200 * ms);
    predict_mouse_to(102, 60, t0 + 210 * ms);
    predict_mouse_to(104, 60, t0 + 220 * ms);
    compositor_press(COMPOSITOR_BTN_LEFT, 1);
    check("predict drag: a press over the picture is told at the real place, 44 in, and the pointer is where the hand is",
          compositor.told_row == 1 && compositor.told_x == 44 && compositor.pointer_x == 104);
    compositor_press(COMPOSITOR_BTN_LEFT, 0);
    memory_zero(compositor.predict, sizeof(compositor.predict));
}

/*
        The error table. A synthetic hand (steady at a speed, stopped, turned back, wiggled by 1 to 3 px, or jittered) moves in
        milli-pixels over time in nanoseconds. Its device reports at jittered times, each with the counts the hand moved, go
        through compositor_report at their device times and are read in batches (a read every 4 ms). At each of 90 frames the
        pointer is judged against where the hand is one frame later. Three shows are judged: none (the hand's real place), the
        model before this change (written out below, with the integer places and arrival times main used), and the pointer as
        compositor.c now leads it. Errors are RMS, in thousandths of a pixel.
*/
enum predict_motion { PREDICT_STEADY, PREDICT_STOP, PREDICT_TURN, PREDICT_WIGGLE, PREDICT_JITTER };
struct predict_hand
{
    enum predict_motion kind;
    long long x0, v, amp, period, turn, seed; // milli-pixels, milli-pixels a millisecond, nanoseconds
};

static long long predict_hand_at(const struct predict_hand address_to h, long long t)
{
    long long ph, t4, tri, bucket, hash;

    switch (h->kind)
    {
    case PREDICT_STEADY:
        return h->x0 + h->v * t / 1000000;
    case PREDICT_STOP:
        return h->x0 + h->v * (t < h->turn ? t : h->turn) / 1000000;
    case PREDICT_TURN:
        if (t <= h->turn)
            return h->x0 + h->v * t / 1000000;
        return h->x0 + h->v * h->turn / 1000000 - h->v * (t - h->turn) / 1000000;
    case PREDICT_WIGGLE:
        ph = t % h->period;
        t4 = ph * 4000 / h->period;
        tri = t4 <= 1000 ? t4 : t4 <= 3000 ? 2000 - t4 : t4 - 4000;
        return h->x0 + h->amp * tri / 1000;
    case PREDICT_JITTER:
        bucket = t / 8000000;
        hash = (bucket * 2654435761ll + h->seed * 40503ll) & 0x7fffffffll;
        return h->x0 + hash % (2 * h->amp + 1) - h->amp;
    }
    return h->x0;
}

// The model before this change: the newest two integer places of the class, the lead their velocity over one frame (integer
// division, two places a millisecond to 50 ms apart), no further than 4 px, and cleared a frame after the newest place.
struct predict_old
{
    long long x[2], at[2];
    unsigned int count;
    long long last, lead;
};

static void predict_old_sample(struct predict_old address_to tr, long long x, long long now, long long period)
{
    if (tr->count && tr->x[tr->count - 1] == x)
        return;
    if (tr->count == 2)
        tr->x[0] = tr->x[1], tr->at[0] = tr->at[1], tr->count = 1;
    tr->x[tr->count] = x, tr->at[tr->count] = now, tr->count++, tr->last = now, tr->lead = 0;
    if (tr->count == 2 && tr->at[1] - tr->at[0] >= 1000000 && tr->at[1] - tr->at[0] <= 50000000)
    {
        long long dx = (tr->x[1] - tr->x[0]) * period / (tr->at[1] - tr->at[0]);

        tr->lead = dx > 4 ? 4 : dx < -4 ? -4 : dx;
    }
}

// The hand's trace judged: out[0..2] are the RMS errors of none, the model before and the model now, out[3..4] the mean lead of
// the two models, all in thousandths of a pixel. The mouse has no lead, so the trace is a finger's: its counts become its place
// (at the gain they were made at), and the fit a finger's places take is the one judged.
static void predict_table_case(const struct predict_hand address_to h, long long gain_percent, long long interval_us,
                               long long jitter_us, long long out[5])
{
    enum { FRAMES = 90, REPORTS = 1600 };
    static long long when[REPORTS], arrive[REPORTS], counts[REPORTS];
    const long long period = (long long)compositor.period;
    long long err[3] = {0, 0, 0}, lead_sum[2] = {0, 0}, sent = 0, lcg = h->seed * 6364136223846793ll + 1, tt = 0, real = 0;
    struct predict_old old;
    long long real_fx = 0;
    int n = 0, next = 0;

    memory_zero(&old, sizeof(old));
    memory_zero(compositor.predict, sizeof(compositor.predict));
    compositor.width = compositor.height = 6000; /* the screen the hand walks across: from 2000 px, 900 px back and 3000 px on at most */
    compositor.accel_x = compositor.accel_y = 0;
    compositor.real_x = compositor.real_y = 0;
    compositor.flat = 1;
    compositor.sensitivity = (int)gain_percent;
    when[n++] = 0;
    while (tt < FRAMES * period + 3 * period && n < REPORTS)
    {
        long long j = 0;

        lcg = (long long)((unsigned long long)lcg * 6364136223846793005ull + 1442695040888963407ull);
        if (jitter_us)
            j = (long long)(((unsigned long long)lcg >> 33) % (unsigned long long)(2 * jitter_us + 1)) - jitter_us;
        tt += (interval_us + j) * 1000;
        when[n++] = tt;
    }
    for (int k = 0; k < n; k++)
    {
        long long target = predict_hand_at(h, when[k]) * 1000 / (gain_percent * 10), c;

        c = (target - sent + (target >= sent ? 500 : -500)) / 1000;
        sent += c * 1000;
        counts[k] = c;
        arrive[k] = (when[k] + 3999999) / 4000000 * 4000000;
    }
    for (int f = 1; f <= FRAMES; f++)
    {
        long long now = f * period, truth = predict_hand_at(h, now + period), e;
        long long last_real;

        while (next < n && arrive[next] <= now)
        {
            real_fx += (long long)counts[next] * gain_percent * COMPOSITOR_PREDICT_FX / 100;
            predict_finger_fx(real_fx, 0, (positive)when[next], (positive)arrive[next]);
            real = real_fx / COMPOSITOR_PREDICT_FX;
            predict_old_sample(&old, real, arrive[next], period);
            next++;
        }
        if (old.lead && now >= old.last + period)
            old.lead = 0;
        compositor_predict_expire((positive)now);
        last_real = real;
        e = last_real * 1000 - truth;
        err[0] += e * e;
        e = (last_real + old.lead) * 1000 - truth;
        err[1] += e * e;
        e = compositor.pointer_x * 1000 - truth;
        err[2] += e * e;
        lead_sum[0] += (old.lead < 0 ? -old.lead : old.lead) * 1000;
        lead_sum[1] += (compositor.pointer_x - compositor.real_x < 0 ? compositor.real_x - compositor.pointer_x : compositor.pointer_x - compositor.real_x) * 1000;
    }
    for (int i = 0; i < 3; i++)
        out[i] = (long long)compositor_isqrt((unsigned long long)(err[i] / FRAMES));
    out[3] = lead_sum[0] / FRAMES, out[4] = lead_sum[1] / FRAMES;
}

static void check_compositor_predict_table(void)
{
    struct windows address_to table = predict_setup();
    static const struct
    {
        const char address_to name;
        struct predict_hand hand;
        long long gain;
    } cases[] = {
        {"steady 0.1 px/ms", {PREDICT_STEADY, 2000000, 100, 0, 0, 0, 1}, 100},
        {"steady 0.5 px/ms", {PREDICT_STEADY, 2000000, 500, 0, 0, 0, 2}, 100},
        {"steady 2 px/ms", {PREDICT_STEADY, 2000000, 2000, 0, 0, 0, 3}, 100},
        {"steady 0.5 px/ms, gain 1.5", {PREDICT_STEADY, 2000000, 500, 0, 0, 0, 4}, 150},
        {"steady 0.1 px/ms, gain 1.5", {PREDICT_STEADY, 2000000, 100, 0, 0, 0, 11}, 150},
        {"stop after 2 px/ms", {PREDICT_STOP, 2000000, 2000, 0, 0, 300000000ll, 5}, 100},
        {"turn at 1 px/ms", {PREDICT_TURN, 2000000, 1000, 0, 0, 300000000ll, 6}, 100},
        {"wiggle +-1 px, 60 ms", {PREDICT_WIGGLE, 2000000, 0, 1000, 60000000ll, 0, 7}, 100},
        {"wiggle +-2 px, 60 ms", {PREDICT_WIGGLE, 2000000, 0, 2000, 60000000ll, 0, 7}, 100},
        {"wiggle +-1.5 px, 120 ms", {PREDICT_WIGGLE, 2000000, 0, 1500, 120000000ll, 0, 8}, 100},
        {"jitter +-2 px", {PREDICT_JITTER, 2000000, 0, 2000, 0, 0, 9}, 100},
    };
    long long out[5];

    if (!table)
    {
        check("predict table: the table is mapped", false);
        return;
    }
    for (unsigned int i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        predict_table_case(&cases[i].hand, cases[i].gain, 8000, 2000, out);
        string_format(log, "  predict table: %s  none %p  before %p  now %p  thousandths of a pixel RMS; lead before %p now %p\n",
                      cases[i].name, out[0], out[1], out[2], out[3], out[4]);
        if (i == 0)
            check("predict table: a slow steady hand is judged better than before", out[2] < out[1]);
        else if (i == 4)
            check("predict table: a slow steady hand at gain 1.5 is judged better than before", out[2] < out[1]);
        else if (i == 6)
            check("predict table: a turn is level with before, within 1%: the step back over a lead is no lead", out[2] <= out[1] + out[1] / 100);
        else if (i >= 7 && i <= 9)
            check("predict table: a wiggle of 1 to 3 px adds no more than 0.1 px to none, and leads by at most 0.1 px",
                  out[2] <= out[0] + 100 && out[4] <= 100);
        else if (i == 10)
            check("predict table: jitter adds no more than 0.15 px to none, and leads by at most 0.1 px",
                  out[2] <= out[0] + 150 && out[4] <= 100);
        else
            check("predict table: a steady, stopped or fast hand is no worse than before", out[2] <= out[1]);
    }
    compositor.width = compositor.height = GLASS_SIDE;
}

/*
        The lead's model: a wiggle is no motion, a fit takes its places' device times (two places read in one batch are led by
        the time between them, not by the arrivals), and a step back over a lead clears the lead on its axis at once.
*/
static void check_compositor_predict_model(void)
{
    struct windows address_to table = predict_setup();
    const positive t0 = 4000000000ull, ms = 1000000ull;
    int led = 0;

    if (!table)
    {
        check("predict model: the table is mapped", false);
        return;
    }
    memory_zero(compositor.predict, sizeof(compositor.predict));
    /* The first step of a wiggle is a step, and no later one: the step back over it ends its lead, and a wiggle after that is no motion. */
    for (int i = 0; i < 12; i++)
    {
        predict_finger_to(100 + (i & 1) * 2, 50, t0 + i * 8 * ms);
        led |= i >= 2 && compositor.pointer_x != compositor.real_x;
    }
    check("predict model: a wiggle of 2 px back and forth every 8 ms is no motion once it has turned, so no later report is led", !led);

    memory_zero(compositor.predict, sizeof(compositor.predict));
    predict_finger_fx(100 * COMPOSITOR_PREDICT_FX, 50 * COMPOSITOR_PREDICT_FX, t0, t0 + 20 * ms);
    predict_finger_fx(102 * COMPOSITOR_PREDICT_FX, 50 * COMPOSITOR_PREDICT_FX, t0 + 10 * ms, t0 + 20 * ms);
    check("predict model: two places read in one batch are led by their device times: 3 px past 102, 2 px in 10 ms",
          compositor.pointer_x == 105 && compositor.real_x == 102);

    memory_zero(compositor.predict, sizeof(compositor.predict));
    predict_finger_to(100, 50, t0 + 100 * ms);
    predict_finger_to(102, 50, t0 + 110 * ms);
    predict_finger_to(104, 50, t0 + 120 * ms);
    check("predict model: a steady finger is led by 3 px before the turn", compositor.pointer_x == 107);
    predict_finger_to(103, 50, t0 + 130 * ms);
    check("predict model: a step back over the lead clears the lead on its axis at once: the pointer is the hand's place",
          compositor.pointer_x == 103 && compositor.real_x == 103);
    memory_zero(compositor.predict, sizeof(compositor.predict));
}

/*
        A window's client's own picture of the pointer (compositor_picture_take, the copy compositor_picture_poll makes of what
        wayland_cursor_picture names): worn at the pointer with its hotspot on it, drawn into the headless screen by the software path
        (the scene's pixels) and painted into the plane's buffer by the same copy, at the desktop's scale over the buffer's, no larger
        than the plane, its colours mixed by their alpha, a colour past its alpha taken at it, gone where the client hid the pointer, and
        refused (the arrow) past the sizes it may be read at. The pixels below are premultiplied ARGB8888, as a client's buffer is.
*/
static unsigned int picture_mix(unsigned int ground, unsigned int ink, unsigned int cover)
{
    unsigned int out = 0;

    for (int shift = 0; shift < 24; shift += 8)
        out |= (((ground >> shift & 255) * (255 - cover) + (ink >> shift & 255) * cover + 127) / 255) << shift;
    return out;
}

// The picture's copy taken, worn and drawn with the pointer at x, y, and the frame composed.
static fn picture_wear(const unsigned int address_to pixels, long width, long height, long stride, int opaque, long hot_x, long hot_y,
                       long scale, int x, int y)
{
    compositor_picture.worn = compositor_picture_take((const unsigned char address_to)pixels, stride, width, height, opaque, hot_x, hot_y,
                                                      scale, compositor.scale);
    compositor.pointer_x = x, compositor.pointer_y = y;
    compositor_cursor_move();
    compositor_present();
}

static void check_compositor_picture(void)
{
    static unsigned int picture[4][6] = {
        {0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u},
        {0xff00ff00u, 0x80808080u, 0, 0xff0000ffu, 0, 0},
        {0x80800000u, 0x80800000u, 0x80800000u, 0x80800000u, 0x80800000u, 0x80800000u},
        {0x40ff0000u, 0x40ff0000u, 0x40ff0000u, 0x40ff0000u, 0x40ff0000u, 0x40ff0000u}};
    static unsigned int doubled[8][12], large[128][128], wide[100][200], plane[CP_PLANE * CP_PLANE];
    struct windows address_to table = predict_setup();
    const unsigned int ground = 0x404040u;
    struct compositor_pointer now;
    struct compositor_target t = {plane, CP_PLANE, CP_PLANE, CP_PLANE, 0, 0, CP_PLANE, CP_PLANE};
    int hot_x, hot_y, span, same = 1;

    if (!table)
    {
        check("picture: the table is mapped", false);
        return;
    }
    picture_wear(&picture[0][0], 6, 4, 24, 0, 2, 1, 1, 100, 60);
    check("picture: a client's picture is worn with its hotspot on the pointer, its opaque pixels its own colours",
          compositor_picture.worn && compositor.cursor.drawn && compositor.cursor.drawn_as.picture == compositor_picture.worn &&
              glass_at(98, 59) == 0xff0000u && glass_at(103, 59) == 0xff0000u && glass_at(98, 60) == 0x00ff00u && glass_at(101, 60) == 0x0000ffu);
    check("picture: its translucent pixels are mixed over what is under them by their alpha, and its clear ones leave it",
          glass_at(99, 60) == picture_mix(ground, 0xffffffu, 0x80) && glass_at(100, 61) == picture_mix(ground, 0xff0000u, 0x80) &&
              glass_at(100, 60) == ground && glass_at(102, 60) == ground);
    check("picture: a colour past its alpha, which no premultiplied picture has, is taken at its alpha",
          glass_at(98, 62) == picture_mix(ground, 0xff0000u, 0x40) && glass_at(103, 62) == picture_mix(ground, 0xff0000u, 0x40));
    check("picture: nothing of it is drawn past its square", glass_at(97, 59) == ground && glass_at(104, 59) == ground &&
                                                                 glass_at(98, 58) == ground && glass_at(98, 63) == ground);

    // The plane's buffer, painted as compositor_cursor_paint paints it: the same copy, premultiplied, at the square's corner.
    compositor_pointer_now(&now);
    span = compositor_pointer_span(&now, &hot_x, &hot_y);
    memory_zero(plane, sizeof(plane));
    compositor_cursor_draw(&t, hot_x - compositor_cursor_inset(span), hot_y - compositor_cursor_inset(span), &now,
                           compositor_cursor_ink(0, 0xff000000u), compositor_cursor_ink(1, 0xff000000u));
    for (int y = 0; y < CP_PLANE; y++)
        for (int x = 0; x < CP_PLANE; x++)
            same &= plane[y * CP_PLANE + x] == (x >= 6 || y >= 4 ? 0u : y == 3 ? 0x40400000u : picture[y][x]);
    check("picture: the plane is given the same copy, premultiplied, its hotspot the plane's place, and clear round it",
          span == 6 && hot_x == 2 && hot_y == 1 && same);
#ifdef MOONWATER_GPU_C
    check("picture: the GPU's scene does not draw a client's picture, and says why (the processor composes that present)",
          !compositor_gpu_cursor(100, 60, &now, compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0)) &&
              compositor_gscene.why == COMPOSITOR_GPU_WHY_POINTER);
#endif

    compositor.pointer_x = 140, compositor.pointer_y = 90;
    compositor_motion();
    compositor_present();
    check("picture: moved, it is drawn where the pointer went and gone from where it was",
          glass_at(138, 89) == 0xff0000u && glass_at(98, 59) == ground && glass_at(101, 60) == ground);
    compositor.cursor.client_hidden = 1;
    compositor_cursor_move();
    compositor_present();
    check("picture: a client that hid the pointer (set_cursor with no surface) has none over its window",
          glass_at(138, 89) == ground && glass_at(139, 90) == ground && !compositor.cursor.drawn);
    compositor.cursor.client_hidden = 0;

    // Twice the pixels at a buffer's scale of 2 are the same picture at a desktop of 100, its hotspot given in the buffer's pixels.
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 12; x++)
            doubled[y][x] = picture[y / 2][x / 2];
    picture_wear(&doubled[0][0], 12, 8, 48, 0, 4, 2, 2, 100, 60);
    check("picture: a buffer of scale 2 is half its pixels each way at a desktop of 100, its hotspot with it",
          glass_at(98, 59) == 0xff0000u && glass_at(101, 60) == 0x0000ffu && glass_at(99, 60) == picture_mix(ground, 0xffffffu, 0x80) &&
              glass_at(104, 59) == ground && glass_at(98, 63) == ground);
    compositor.scale = 200;
    picture_wear(&picture[0][0], 6, 4, 24, 0, 2, 1, 1, 100, 60);
    check("picture: and one of scale 1 is twice them at a desktop of 200",
          glass_at(96, 58) == 0xff0000u && glass_at(107, 59) == 0xff0000u && glass_at(98, 60) == picture_mix(ground, 0xffffffu, 0x80) &&
              glass_at(99, 61) == picture_mix(ground, 0xffffffu, 0x80) && glass_at(100, 60) == ground && glass_at(102, 60) == 0x0000ffu &&
              glass_at(103, 61) == 0x0000ffu && glass_at(106, 60) == ground && glass_at(108, 58) == ground && compositor_picture.span == 12);
    compositor.scale = 100;

    // A picture larger than the plane is made smaller to fit, each pixel the mean of those under it.
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            large[y][x] = (x + y) & 1 ? 0xffffffffu : 0;
    picture_wear(&large[0][0], 128, 128, 512, 0, 127, 127, 1, 100, 160);
    check("picture: one larger than the plane is made smaller to fit it, each pixel the mean of the four under it",
          compositor_picture.width == CP_PLANE && compositor_picture.height == CP_PLANE && compositor_picture.span == CP_PLANE &&
              compositor_picture.hot_x == 63 && compositor_picture.hot_y == 63 && compositor_picture.premultiplied[0] == 0x80808080u &&
              compositor_picture.premultiplied[CP_PLANE * CP_PLANE - 1] == 0x80808080u && glass_at(90, 100) == picture_mix(ground, 0xffffffu, 0x80));
    for (int y = 0; y < 100; y++)
        for (int x = 0; x < 200; x++)
            wide[y][x] = 0xff00ff00u;
    picture_wear(&wide[0][0], 200, 100, 800, 0, 0, 0, 1, 100, 60);
    check("picture: and keeps its shape", compositor_picture.width == CP_PLANE && compositor_picture.height == CP_PLANE / 2);
    check("picture: one past the most read either way, a scale past the most, or rows shorter than its width is not worn: the arrow is",
          !compositor_picture_take((const unsigned char address_to)&large[0][0], 4 * (CP_PICTURE_SOURCE_MOST + 1), CP_PICTURE_SOURCE_MOST + 1, 1, 0, 0, 0, 1, 100) &&
              !compositor_picture_take((const unsigned char address_to)&large[0][0], 512, 128, 128, 0, 0, 0, CP_PICTURE_SCALE_MOST + 1, 100) &&
              !compositor_picture_take((const unsigned char address_to)&large[0][0], 511, 128, 128, 0, 0, 0, 1, 100) &&
              !compositor_picture_take(null, 512, 128, 128, 0, 0, 0, 1, 100));

    // A hotspot outside the picture: the picture is drawn where the client put it, beside the pointer, and no further than the plane.
    picture_wear(&picture[0][0], 6, 4, 24, 0, -3, 2, 1, 100, 60);
    check("picture: a hotspot left of the picture has the picture right of the pointer, where the client put it",
          glass_at(103, 58) == 0xff0000u && glass_at(102, 58) == ground && glass_at(106, 59) == 0x0000ffu && compositor_picture.span == 9 &&
              compositor_picture.hot_x == 0 && compositor_picture.hot_y == 2);
    picture_wear(&picture[0][0], 6, 4, 24, 0, -1000, 0, 1, 100, 60);
    check("picture: and one further out than the plane reaches is taken at its edge",
          compositor_picture.span == CP_PLANE && glass_at(100 + CP_PLANE - 6, 60) == 0xff0000u);

    // An opaque format's top byte is not its alpha.
    picture[1][2] = 0x00123456u;
    picture_wear(&picture[0][0], 6, 4, 24, 1, 2, 1, 1, 100, 60);
    check("picture: an XRGB8888 picture is opaque whatever its top byte", glass_at(100, 60) == 0x123456u && glass_at(99, 60) == 0x808080u);
    picture[1][2] = 0;

    compositor_picture.worn = 0;
    compositor_cursor_move();
    compositor_present();
    check("picture: worn no longer, the pointer is the compositor's arrow again",
          compositor.cursor.drawn && !compositor.cursor.drawn_as.picture && glass_at(100, 60) == compositor_cursor_ink(1, 0) && glass_at(99, 59) == ground);
    compositor.cursor.soft = 0;
}

/*
        The software pointer over a scanout only written is composed aside (compositor_cursor_compose): its square's scene drawn into
        memory of its own, the pointer over it there, and the square copied out, where the pointer was mixed into the scanout in place
        and every pixel it wrote was a read of the scanout's write-combined memory. Bit for bit the frame the mix in place made: each
        frame here is presented with the pointer, then again without it and the pointer mixed in place over that, and the two are
        compared whole, at a window's middle, across a window's edge, cut by the screen's corner, and for a client's own picture.
*/
static unsigned int aside_shot[GLASS_SIDE * GLASS_SIDE];

static b32 pointer_aside_same(int x, int y)
{
    struct compositor_target t = {compositor.scanout, GLASS_SIDE, GLASS_SIDE, GLASS_SIDE, 0, 0, GLASS_SIDE, GLASS_SIDE};
    b32 drew;

    compositor.pointer_x = x, compositor.pointer_y = y;
    compositor_cursor_move();
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    memory_copy(aside_shot, compositor.scanout, sizeof(aside_shot));
    compositor.cursor.drawn = 0;
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    drew = memory_compare(aside_shot, compositor.scanout, sizeof(aside_shot)) != 0;
    compositor.cursor.drawn = 1;
    compositor_cursor_draw(&t, compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as, compositor_cursor_ink(0, 0),
                           compositor_cursor_ink(1, 0));
    return drew && !memory_compare(aside_shot, compositor.scanout, sizeof(aside_shot));
}

static void check_compositor_pointer_aside(void)
{
    static unsigned int picture[8][8];
    struct windows address_to table = predict_setup();

    if (!table)
    {
        check("pointer aside: the table is mapped", false);
        return;
    }
    check("pointer aside: over a window's middle, the frame is the one the pointer mixed in place made, bit for bit", pointer_aside_same(110, 70));
    check("pointer aside: across a window's edge and its frame", pointer_aside_same(48, 28));
    check("pointer aside: cut by the screen's corner", pointer_aside_same(GLASS_SIDE - 3, GLASS_SIDE - 2));
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            picture[y][x] = (x + y) & 1 ? 0xff20c040u : 0x80402010u;
    compositor_picture.worn = compositor_picture_take((const unsigned char address_to)&picture[0][0], 32, 8, 8, 0, 3, 3, 1, 100);
    check("pointer aside: and a client's own picture", compositor_picture.worn && pointer_aside_same(100, 60));
    compositor_picture.worn = 0;
    compositor.cursor.soft = 0;
}

/*
        The wheel through the compositor's reader, from a pipe that stands for a device in slot 0: what a notch
        becomes in the ring of the window under the pointer, what moonwater debug is told of it, and a swipe at a
        window that is not reading while it comes, which its ring of WINDOWS_EVENTS events used to cut off.
*/
static int wheel_pipe[2] = {-1, -1};

/*
        An event as the device writes one. A pipe made while its user holds more than pipe-user-pages-soft is two
        pages, not sixteen, which a swipe of 150 notches does not fit in, and the events that did not fit were lost:
        what does not fit is read by the compositor first, as a device's events are read while it reports more.
*/
static fn wheel_feed(unsigned short type, unsigned short code, int value)
{
        struct compositor_input_event event = {0, 0, type, code, value};

        while (system_write_once(wheel_pipe[1], &event, sizeof(event)) == -COMPOSITOR_EAGAIN)
                compositor_input_read(0);
}

// A notch as the kernel says one of a wheel that reports in 120ths: the notch, the 120ths, the end of the report.
static fn wheel_notch(int sign)
{
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL, sign);
        wheel_feed(COMPOSITOR_EV_REL, COMPOSITOR_REL_WHEEL_HI, sign * 120);
        wheel_feed(COMPOSITOR_EV_SYN, 0, 0);
}

// The wheel the program of row has not read yet: the sum of its turns, how many events they are, and the last.
static int wheel_unread(int row, unsigned int address_to events, int address_to last)
{
        struct windows address_to table = compositor.table;
        int sum = 0;

        address_to events = 0;
        address_to last = 0;
        for (unsigned int at = table->event_tail[row]; at != table->event_head[row]; at++)
                if (table->event_type[row][at % WINDOWS_EVENTS] == WINDOW_EVENT_WHEEL)
                {
                        sum += table->event_a[row][at % WINDOWS_EVENTS];
                        address_to last = table->event_a[row][at % WINDOWS_EVENTS];
                        (address_to events)++;
                }
        return sum;
}

/*
        The tray's volume cell and the volume keys (compositor.c, taskbar.c) against a page and two files made here: the cell is
        there only when the server's page is, at the start of the tray and before the battery's, and is what a point there is
        (TASKBAR_VOLUME); a key or a notch of the wheel over it is five percent, written to the volume file whole, a held key
        repeats, the mute key throws the mute and a volume key unmutes; none of it does anything with no server; the number is
        shown for a moment; and the page is not believed for a moment after the desktop's own change.
*/
static fn volume_put_page(unsigned int volume, unsigned int muted, unsigned int magic)
{
        static struct audio_state page;
        bipolar handle;

        memory_zero(&page, sizeof(page));
        page.magic = magic, page.version = AUDIO_STATE_VERSION, page.bytes = sizeof(page), page.seq = 2;
        page.volume = volume, page.muted = muted;
        string_copy_bounded((string_address)page.device, (string_address) "Speakers", sizeof(page.device));
        string_copy_bounded((string_address)page.name, (string_address) "alsa_output.test", sizeof(page.name));
        handle = system_open_at_mode(AT_FDCWD, (string_address) "/tmp/mw-volume-state", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle >= 0)
        {
                system_write_once(handle, &page, sizeof(page));
                system_close(handle);
        }
}

#if MOONWATER_UI_MIXER
// The transport of a check for the control client: what is sent is kept, read as the engine reads it, and answered; used by the compositor's volume and the mixer.
struct mixer_sent
{
        unsigned long command, index, volume, channels;
        int mute;
        char text[64];
};

static struct mixer_sent mixer_log[16];
static int mixer_logged, mixer_fail_send;
static long mixer_timeout; // the wait the last answer was asked to be waited for
static unsigned char mixer_reply[32];
static long mixer_reply_have, mixer_reply_at;

// The clock of the fake connection stands still, so a call's budget is the whole of its timeout on every wait it makes.
static long mixer_fake_now(void *context)
{
        (void)context;
        return 0;
}

// What the mixer sends is read back as the engine reads it: the command, the tag, and the fields of the five that change a volume or a mute.
static long mixer_fake_send(void *context, const unsigned char *bytes, long count, long timeout_ms)
{
        const unsigned char *payload = bytes + 20;
        struct mixer_sent *sent = &mixer_log[mixer_logged < 16 ? mixer_logged : 15];
        long at = 10;
        unsigned long command = wire_be32(payload + 1), tag = wire_be32(payload + 6);

        (void)context, (void)count, (void)timeout_ms;
        if (mixer_fail_send)
                return -1;
        memory_zero(sent, sizeof(*sent));
        sent->command = command;
        if (payload[at] == 'L')
                sent->index = wire_be32(payload + at + 1), at += 5;
        if (payload[at] == 't')
        {
                string_copy_bounded((string_address)sent->text, (string_address)(payload + at + 1), sizeof(sent->text));
                at += 2 + (long)string_length((string_address)(payload + at + 1));
        }
        if (payload[at] == 'v')
                sent->channels = payload[at + 1], sent->volume = wire_be32(payload + at + 2);
        else if (payload[at] == '1' || payload[at] == '0')
                sent->mute = payload[at] == '1';
        mixer_logged++;
        // Its answer: a bare reply with the tag.
        memory_zero(mixer_reply, sizeof(mixer_reply));
        mixer_reply[3] = 10, mixer_reply[4] = mixer_reply[5] = mixer_reply[6] = mixer_reply[7] = 0xff;
        mixer_reply[20] = 'L', mixer_reply[24] = (unsigned char)PULSE_COMMAND_REPLY;
        mixer_reply[25] = 'L', mixer_reply[26] = (unsigned char)(tag >> 24), mixer_reply[27] = (unsigned char)(tag >> 16), mixer_reply[28] = (unsigned char)(tag >> 8),
        mixer_reply[29] = (unsigned char)tag;
        mixer_reply_have = 30, mixer_reply_at = 0;
        return 0;
}

static long mixer_fake_receive(void *context, unsigned char *bytes, long count, long timeout_ms)
{
        (void)context;
        mixer_timeout = timeout_ms;
        if (mixer_reply_have - mixer_reply_at < count)
                return -1;
        memory_copy(bytes, mixer_reply + mixer_reply_at, (positive)count);
        mixer_reply_at += count;
        return 0;
}
#endif

static int volume_file(const char address_to path, char address_to into, positive room)
{
        compositor_text(path, into, (unsigned int)room);
        return into[0] != 0;
}

// A page with a microphone and, when flags is not 0, one program recording from it in a stream with those flags; capture says whether recording is allowed and muted whether the microphone is.
static fn mic_put_page(unsigned int capture, unsigned int muted, unsigned int flags)
{
        static struct audio_state page;
        bipolar handle;

        memory_zero(&page, sizeof(page));
        page.magic = AUDIO_STATE_MAGIC, page.version = AUDIO_STATE_VERSION, page.bytes = sizeof(page), page.seq = 2;
        page.volume = 52429;
        string_copy_bounded((string_address)page.name, (string_address) "alsa_output.test", sizeof(page.name));
        string_copy_bounded((string_address)page.input, (string_address) "Test microphone", sizeof(page.input));
        string_copy_bounded((string_address)page.input_name, (string_address) "alsa_input.test", sizeof(page.input_name));
        page.capture = capture, page.input_muted = muted, page.input_volume = 65536;
        for (int k = 0; k < AUDIO_STATE_RECORDS; k++)
                page.record[k].index = 0xffffffffu;
        if (flags)
                page.record[1] = (struct audio_state_stream){3, 8, 65536, flags, 0, 0, 200, 1000, "Chromium", "Meet", ""};
        handle = system_open_at_mode(AT_FDCWD, (string_address) "/tmp/mw-volume-state", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle >= 0)
        {
                system_write_once(handle, &page, sizeof(page));
                system_close(handle);
        }
}

// The page put and looked at (nothing held back), and whether the tray has a microphone cell then.
static int mic_cell(unsigned int capture, unsigned int muted, unsigned int flags)
{
        struct taskbar_view view;
        int x, y, w, h;

        mic_put_page(capture, muted, flags);
        compositor_volume_unmap();
        compositor_volume.hold_until = 0, compositor_volume.mic_flash_until = 0;
        compositor_volume_look(clock_monotonic_nanoseconds());
        view = compositor_taskbar_view();
        return taskbar_slot(&view, TASKBAR_MIC, &x, &y, &w, &h);
}

/*
        The notices dot (taskbar.c's cell, compositor.c's watch of the board) and its popup (notices.c): the dot is in the tray while the board
        holds a notice, the cells after it move over, a look at a board that has not moved answers 0, a press on the dot leaves its rectangle
        for the popup, and the popup lists the errors first, then the warnings, and then the two rows; Dismiss all takes the kernel notices off.
*/
#if MOONWATER_UI_NOTICES && MOONWATER_UI_TASKBAR
#define NOTICES_SHOT_W 480
#define NOTICES_SHOT_H 240
static unsigned int notices_shot[NOTICES_SHOT_W * NOTICES_SHOT_H];

// A picture as raw ARGB words, row by row, into a file of the scratch directory: the rig's screenshot (the file is turned to PNG outside).
static void notices_shot_write(const char *path, const unsigned int *pixels, int width, int height, int pitch)
{
    bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    if (handle < 0)
        return;
    for (int y = 0; y < height; y++)
        system_write_once(handle, pixels + (long)y * pitch, (positive)width * 4);
    system_close(handle);
}

static void check_notices_dot(void)
{
        struct windows *table;
        struct taskbar_view view;
        int x, y, w, h, tx, ty, tw, th, mx, my, mw, mh;

        compositor_zero();
        taskbar_classic(); // the tray these rows were written for: every item after the tasks, the clock its cell
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = 2560, compositor.height = 1440;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.hold = -1;
        compositor.holds = ~0ul;
        compositor.told_row = COMPOSITOR_NONE;
        if (!glass_table_map())
        {
                check("notices: the table is made", false);
                taskbar_standard();
                return;
        }
        table = compositor.table;
        taskbar_settings_set("bottom", "primary", "battery none\n");
        taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
        taskbar_volume_set(0, 0, 0, 0);
        taskbar_mic_set(0, 0);
        compositor_notices_look();
        view = compositor_taskbar_view();
        check("notices: a board with nothing on it has no dot, and no cell in the tray", !taskbar.notices_item && !taskbar_slot(&view, TASKBAR_NOTICES, &x, &y, &w, &h));

        windows_issue_set(table, "usb", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_KERNEL_USB, 2, 30, "usb 1-1: device descriptor read error");
        check("notices: a warning on the board is seen by the next look, which changes the tray",
              compositor_notices_look() == 1 && taskbar.notices_item && taskbar.notices_severity == WINDOWS_ISSUE_WARNING);
        view = compositor_taskbar_view();
        check("notices: the dot is a cell of the tray, at its leading end", taskbar_slot(&view, TASKBAR_NOTICES, &x, &y, &w, &h) &&
              taskbar_slot(&view, -2, &tx, &ty, &tw, &th) && x == tx && w > 0 && h > 0);
        check("notices: a look at a board that has not moved answers 0", compositor_notices_look() == 0);
        // The tray grows to the left with the microphone's cell: the dot is then the tray's new leading cell, and the microphone starts after it.
        taskbar_mic_set(1, 0);
        view = compositor_taskbar_view();
        check("notices: the microphone's cell starts after the dot, which is the tray's leading cell", taskbar_slot(&view, TASKBAR_NOTICES, &x, &y, &w, &h) &&
              taskbar_slot(&view, -2, &tx, &ty, &tw, &th) && taskbar_slot(&view, TASKBAR_MIC, &mx, &my, &mw, &mh) && mx == x + w && x == tx);
        taskbar_mic_set(0, 0);
        view = compositor_taskbar_view();
        taskbar_slot(&view, TASKBAR_NOTICES, &x, &y, &w, &h);
        check("notices: a press on the dot's centre is the dot", taskbar_hit_reach(&view, x + w / 2, y + h / 2, 0) == TASKBAR_NOTICES);
        compositor_notices_open(x + w / 2, y + h / 2);
        check("notices: a press on the dot leaves its rectangle for the popup", table->button_x == x && (int)table->button_w == w && table->button_edge == TASKBAR_BOTTOM);
        table->button_w = 0;

        windows_issue_set(table, "wifi", WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_KERNEL_WIFI, 1, 0, "wlan0: firmware crashed");
        check("notices: an error turns the dot red", compositor_notices_look() == 1 && taskbar.notices_severity == WINDOWS_ISSUE_ERROR);

        notices.own.table = table;
        notices_board();
        // The popup drawn as the app draws it (its glass over a dark ground), and the tray's band with the dot in it, as the rig's shots.
        ui_popmenu_size(&terminal.ui, &notices.menu, NOTICES_SHOT_H);
        for (int at = 0; at < NOTICES_SHOT_W * NOTICES_SHOT_H; at++)
            notices_shot[at] = 0xff202428u;
        ui_begin(&terminal.ui, notices_shot, NOTICES_SHOT_W, NOTICES_SHOT_W, NOTICES_SHOT_H);
        ui_popmenu_draw(&terminal.ui, &notices.menu);
        notices_shot_write("/tmp/notices-menu-480x240.raw", notices_shot, NOTICES_SHOT_W, NOTICES_SHOT_H, NOTICES_SHOT_W);
        {
            struct taskbar_view drawn;
            int rect[4], bx, by, bw, bh;

            drawn = band_view(TASKBAR_BOTTOM, null);
            taskbar_band(&drawn, &bx, &by, &bw, &bh);
            taskbar_draw(&drawn, 1ul << TASKBAR_TAB_BITS, band_glass, null, rect);
            notices_shot_write("/tmp/notices-band-1920x24.raw", band_pixels, BAND_W, COMPOSITOR_PANEL, BAND_W);
        }
        /*
                The dot is drawn in the chrome's roles, which follow the table's theme, and not in the kit's (ui_role): the kit makes its roles in
                a window's own process and the compositor never does, so there they were all zero and the dot (and the microphone) was black on
                either theme. The kit's roles are zeroed here as the compositor has them, and the middle of the dot is the theme's error or
                warning colour, light and dark, and the two themes' are not the same colour.
        */
        {
            struct ui_theme kept = ui_kit.shown;
            unsigned int middle[2][2] = {{0, 0}, {0, 0}}, want[2][2] = {{0, 0}, {0, 0}};
            b32 right = true;

            memory_zero(&ui_kit.shown, sizeof(ui_kit.shown));
            for (unsigned int dark = 0; dark < 2; dark++)
                for (unsigned int severity = 0; severity < 2; severity++)
                {
                    struct taskbar_view drawn = band_view(TASKBAR_BOTTOM, null);
                    int rect[4], bx, by, bw, bh, dx, dy, dw, dh;

                    table->theme = dark ? UI_THEME_DARK : UI_THEME_LIGHT, table->accent = 0xffffffu;
                    __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
                    taskbar_notices_set(1, severity ? WINDOWS_ISSUE_ERROR : WINDOWS_ISSUE_WARNING);
                    taskbar_band(&drawn, &bx, &by, &bw, &bh);
                    taskbar_draw(&drawn, 1ul << TASKBAR_TAB_BITS, band_glass, null, rect);
                    right &= taskbar_slot(&drawn, TASKBAR_NOTICES, &dx, &dy, &dw, &dh) != 0;
                    middle[dark][severity] = band_pixels[(unsigned long)(dy + dh / 2 - by) * BAND_W + (unsigned long)(dx + dw / 2 - bx)] & 0xffffffu;
                    want[dark][severity] = compositor_ink_role(severity ? UI_ERROR : UI_WARNING) & 0xffffffu;
                }
            ui_kit.shown = kept;
            taskbar_notices_set(1, WINDOWS_ISSUE_ERROR);
            check("notices: the dot is the theme's warning colour and error colour, light and dark, with the kit's roles unmade as in the compositor",
                  right && middle[0][0] == want[0][0] && middle[0][1] == want[0][1] && middle[1][0] == want[1][0] && middle[1][1] == want[1][1] &&
                      want[0][0] && want[0][1] && want[1][0] && want[1][1]);
            check("notices: the light theme's dot is not the dark theme's, for a warning and for an error",
                  middle[0][0] != middle[1][0] && middle[0][1] != middle[1][1] && middle[0][0] != middle[0][1]);
        }
        check("notices: the popup lists the error first, then the warning, then a rule and the two rows",
              notices.menu.count == 5 && string_equals((string_address)notices.menu.items[0].keys, (string_address)"Error") &&
                  string_equals((string_address)notices.menu.items[1].keys, (string_address)"Warning") &&
                  notices.menu.items[2].flags == UI_MENU_SEPARATOR && notices.menu.items[3].id == NOTICES_ID_OPEN &&
                  notices.menu.items[4].id == NOTICES_ID_DISMISS);
        // How often and how long ago, in the form the number takes: the error came once 0 s ago and the warning twice 30 s ago.
        {
            char said[4][96];

            notices_said(said[0], sizeof(said[0]), "wifi", "wlan0: firmware crashed", 1, 59);
            notices_said(said[1], sizeof(said[1]), "wifi", "wlan0: firmware crashed", 1, 125);
            notices_said(said[2], sizeof(said[2]), "usb", "read error", 2, 0);
            notices_said(said[3], sizeof(said[3]), "usb", "read error", 12, 3600);
            check("notices: a kernel notice says once or N times, and just now or N min ago, never 1 times or 0 min ago",
                  string_equals((string_address)said[0], (string_address) "wifi: wlan0: firmware crashed (once, just now)") &&
                      string_equals((string_address)said[1], (string_address) "wifi: wlan0: firmware crashed (once, 2 min ago)") &&
                      string_equals((string_address)said[2], (string_address) "usb: read error (2 times, just now)") &&
                      string_equals((string_address)said[3], (string_address) "usb: read error (12 times, 60 min ago)") &&
                      memory_search((address_any)notices.menu.items[0].label, string_length((string_address)notices.menu.items[0].label), (address_any) "(once, just now)", 16) &&
                      memory_search((address_any)notices.menu.items[1].label, string_length((string_address)notices.menu.items[1].label), (address_any) "(2 times, just now)", 19));
        }

        /*
                The popup's labels are drawn through its own kit's scratch line, which only ui_scratch_fit maps, and each app maps its
                own at its size (the kit's size, ui_app_size). The rows above were drawn through the terminal's kit, which has one; a
                popup whose size hook left its kit unmapped drew its rows with no text, an empty window. Here the popup's kit is the
                terminal's with the scratch taken away, sized as notices_open sizes it, and its picture must have ink in it beyond the
                rule the menu draws.
        */
        {
            static unsigned int picture[GLASS_SIDE * GLASS_SIDE];
            int ink = 0, hover = 0;

            notices.ui = terminal.ui;
            notices.ui.scratch = null, notices.ui.scratch_width = 0, notices.ui.scratch_scale = 0;
            notices.ui.glass = 1, notices.ui.opaque = 0, notices.ui.scale = 100;
            ui_popmenu_size(&notices.ui, &notices.menu, NOTICES_SHOT_H);
            ui_app_size(&notices_app, (unsigned int)notices.menu.width, (unsigned int)notices.menu.height);
            check("notices: the popup's kit has a scratch line at least as wide as the popup once it is sized",
                  notices.ui.scratch != null && notices.ui.scratch_width >= notices.width);
            notices_draw(picture);
            for (int y = 0; y < notices.height; y++)
                for (int x = 0; x < notices.width; x++)
                    ink += (picture[y * GLASS_SIDE + x] >> 24) != 0;
            check("notices: the popup draws its labels, not an empty window (the picture has ink beyond the rule)", ink > 4 * notices.width);
            // The hot item (a hover) is drawn over its row and its label stays: the same labels, the highlight under them.
            notices.menu.hot = 0;
            notices_draw(picture);
            for (int y = 0; y < notices.height; y++)
                for (int x = 0; x < notices.width; x++)
                    hover += (picture[y * GLASS_SIDE + x] >> 24) != 0;
            check("notices: a hovered item keeps its label drawn", hover >= ink);
            notices.menu.hot = -1;

            /*
                    The severity is the item's colour: the error's and the warning's, the taskbar's dot in it at the item's start and the
                    word in it at the item's end, on a light glass of the light theme and a dark glass of the dark one, each from its
                    lightest to its dimmest, at rest and hot. The word stands out 4.5 to 1 from what it is drawn on and the dot, a shape,
                    3 to 1; both are red or amber (the dot more so), and the word is not the quiet grey of the menu's other keys.
            */
            {
                struct ui_theme kept = ui_kit.shown;
                static unsigned int shot[720 * 240];
                static const unsigned int grounds[2][3] = {{0xf7f7f8u, 0xe0e0e2u, 0xb4b4b4u}, {0x101012u, 0x2a2a2eu, 0x5c5c5cu}};
                b32 readable = true, coloured = true, drawn = notices.width <= 720 && notices.height <= 240;

                for (unsigned int dark = 0; dark < 2 && drawn; dark++)
                    for (int g = 0; g < 3; g++)
                        for (int hot = -1; hot < 2; hot++)
                        {
                            // The popup drawn as notices_draw draws it, into a picture as wide as it is, with no item hot and with each.
                            ui_theme_make(&ui_kit.shown, dark, UI_ACCENT_STANDARD);
                            ui_inks(&notices.ui, grounds[dark][g]);
                            ui_begin(&notices.ui, shot, 720, notices.width, notices.height);
                            notices.menu.hot = hot;
                            ui_popmenu_draw(&notices.ui, &notices.menu);
                            for (int k = 0; k < 2; k++)
                            {
                                const struct ui_menu_item address_to item = &notices.menu.items[k];
                                const struct ui_node address_to n = &notices.menu.tree.node[item->node];
                                unsigned int word = ui_popmenu_tone(&notices.ui, item->flags, hot == k, UI_READABLE);
                                unsigned int mark = ui_popmenu_tone(&notices.ui, item->flags, hot == k, UI_MENU_SEEN);
                                unsigned int ground = notices.ui.ground, well = ui_popmenu_hot_fill(&notices.ui), cover = well >> 24;
                                unsigned int under = hot == k ? window_mix(ground, well & 0xffffffu, cover + (cover >> 7)) : ground;
                                int cx = n->x + ui_dev(&notices.ui, UI_MENU_PAD + 15), cy = n->y + n->h / 2, inked = 0;

                                readable = readable && word && ui_contrast(word, under) >= 450 && ui_contrast(mark, under) >= 300;
                                for (int which = 0; which < 2; which++)
                                {
                                    unsigned int tone = which ? mark : word, r = tone >> 16 & 255, gr = tone >> 8 & 255, b = tone & 255;
                                    unsigned int by = which ? 60 : 24;

                                    coloured = coloured && (k ? r > b + by && gr > b + by / 2 && r >= gr : r > gr + by && r > b + by);
                                }
                                coloured = coloured && (word & 0xffffffu) != (notices.ui.quiet & 0xffffffu);
                                for (int y = n->y; y < n->y + n->h; y++)
                                    for (int x = n[2].x; x < n[2].x + n[2].w && x < notices.width; x++)
                                        inked += shot[y * 720 + x] == (0xff000000u | (word & 0xffffffu));
                                drawn = drawn && shot[cy * 720 + cx] == (0xff000000u | (mark & 0xffffffu)) && inked > 8;
                            }
                        }
                notices.menu.hot = -1;
                ui_kit.shown = kept;
                check("notices: an item's severity is the error's or the warning's colour, red or amber and not the quiet grey of other keys",
                      coloured && notices.menu.items[0].flags == UI_MENU_ERROR && notices.menu.items[1].flags == UI_MENU_WARNING);
                check("notices: its word stands out 4.5 to 1 and its dot 3 to 1 from a light and a dark glass, the lightest to the dimmest, at rest and hot", readable);
                check("notices: the taskbar's dot is drawn in it at the item's start, and the severity's word at the item's end", drawn);
            }
        }

        notices_dismiss();
        view = compositor_taskbar_view();
        check("notices: Dismiss all takes the kernel notices off the board, and the look after it takes the dot away",
              compositor_notices_look() == 1 && !taskbar.notices_item && !taskbar_slot(&view, TASKBAR_NOTICES, &x, &y, &w, &h));
        taskbar_standard();
}
#endif

static void check_compositor_volume(void)
{
        static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
        struct taskbar_view view;
        char text[16], other[16];
        int x, y, w, h, cx, cy, cw, ch, bx, by, bw, bh, before;
        positive now = clock_monotonic_nanoseconds();

        compositor_volume_path[0] = "/tmp/mw-volume-state", compositor_volume_path[1] = "/tmp/mw-volume.file", compositor_volume_path[2] = "/tmp/mw-volume.mute";
        compositor_volume_path[3] = "/tmp/mw-volume-no-socket";
        memory_zero(&compositor_ctl, sizeof(compositor_ctl));
        compositor_ctl_again = 0;
        for (int k = 0; k < 3; k++)
                system_remove_at(AT_FDCWD, (string_address)compositor_volume_path[k], 0);
        compositor_volume_unmap();
        memory_zero(&compositor_volume, sizeof(compositor_volume));
        compositor_zero();
        taskbar_classic(); // the tray these rows were written for: every item after the tasks, the clock its cell
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.hold = -1;
        compositor.holds = ~0ul;
        compositor.told_row = COMPOSITOR_NONE;
        compositor.scanout = screen;
        compositor.scanout_pitch = GLASS_SIDE * 4;
        if (!glass_table_map())
        {
                check("volume: the table is made", false);
                taskbar_standard();
                return;
        }
        compositor.width = 2560, compositor.height = 1440;
        taskbar_settings_set("bottom", "primary", "battery none\n");
        taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
        taskbar_volume_set(0, 0, 0, 0);
        view = compositor_taskbar_view();
        taskbar_slot(&view, -2, &x, &y, &w, &h);
        before = w;

        // No page: no cell, and the keys have nothing to turn.
        compositor_volume_look(now);
        check("volume: with no server there is no cell in the tray", !compositor_volume.present && !taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch));
        compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
        check("volume: and a volume key writes nothing", !volume_file(compositor_volume_path[1], text, sizeof(text)) && !volume_file(compositor_volume_path[2], text, sizeof(text)));

        // A page that is not the server's is none.
        volume_put_page(52429, 0, 0);
        compositor_volume_unmap();
        compositor_volume_look(now);
        check("volume: a page that is not the server's is no server", !compositor_volume.present);

        // The server's: the cell is at the start of the tray, a cell wide more than the tray was, and a point in it is the volume.
        volume_put_page(52429, 0, AUDIO_STATE_MAGIC);
        compositor_volume_unmap();
        compositor_volume_look(now);
        view = compositor_taskbar_view();
        check("volume: the page says 80 percent, not muted, and the cell is in the tray", compositor_volume.present && compositor_volume.percent == 80 && !compositor_volume.muted &&
                                                                                           taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch));
        taskbar_slot(&view, -2, &x, &y, &w, &h);
        taskbar_slot(&view, -3, &bx, &by, &bw, &bh);
        check("volume: the tray is a cell longer, the cell at its start and the clock's after it", w == before + cw && cx == x && bx == cx + cw && cx + cw <= x + w);
        check("volume: a point in the cell is the volume, a point in the clock's is the tray",
              taskbar_hit_reach(&view, cx + cw / 2, cy + ch / 2, 0) == TASKBAR_VOLUME && taskbar_hit_reach(&view, bx + bw / 2, by + bh / 2, 0) == -2);
        taskbar_settings_set("bottom", "primary", "battery BAT0\n");
        taskbar.battery_item = 1;
        view = compositor_taskbar_view();
        taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch);
        taskbar_slot(&view, -4, &bx, &by, &bw, &bh);
        check("volume: with a battery the cell is before it", bx == cx + cw);
        taskbar.battery_item = 0;
        taskbar_settings_set("left", "primary", "battery none\n");
        view = compositor_taskbar_view();
        taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch);
        taskbar_slot(&view, -3, &bx, &by, &bw, &bh);
        check("volume: on a side band the cell is above the clock", cy + ch == by && cx == bx);
        taskbar_settings_set("bottom", "primary", "battery none\n");
        taskbar_tray_set("{time.24h.hour}:{time.24h.min}");
        view = compositor_taskbar_view();
        // A format of the person's keeps the cell (the owner's e8cccd1): at the tray's start, the format's text after it, and no clock cell.
        check("volume: a tray of the person's own format keeps the cell, at the tray's start with the format's text after it, a point in it the volume",
              taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch) && taskbar_slot(&view, -2, &x, &y, &w, &h) && taskbar_slot(&view, -9, &bx, &by, &bw, &bh) &&
                  cx == x && bx == cx + cw && bx + bw == x + w && cw > 0 && bw > 0 && !taskbar_slot(&view, -3, &w, &h, &w, &h) &&
                  taskbar_hit_reach(&view, cx + cw / 2, cy + ch / 2, 0) == TASKBAR_VOLUME);
        taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
        view = compositor_taskbar_view();
        taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch);

        // The cell drawn: the speaker in it with a wave more for a louder volume, a cross when it is muted, the number when it was just changed.
        {
                static unsigned int cell_lit[sizeof(band_pixels) / sizeof(unsigned int)];
                static const char *const titles[3] = {"a", "b", "c"};
                static const int says[5][4] = {{1, 0, 0, 0}, {1, 30, 0, 0}, {1, 80, 0, 0}, {1, 80, 1, 0}, {1, 80, 0, 1}};
                int counts[5], rect[4];

                taskbar_settings_set("bottom", "primary", "battery none\n");
                taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
                for (int variant = 0; variant < 5; variant++)
                {
                        struct taskbar_view drawn;
                        int tx, ty, tw, th, vx, vy, vw, vh, dx, dy, dw, dh, count = 0;

                        taskbar_volume_set(says[variant][0], says[variant][1], says[variant][2], says[variant][3]);
                        drawn = band_view(TASKBAR_BOTTOM, titles);
                        taskbar_band(&drawn, &dx, &dy, &dw, &dh);
                        taskbar_draw(&drawn, 1ul << TASKBAR_TAB_BITS, band_glass, null, rect);
                        memory_copy(cell_lit, band_pixels, sizeof(band_pixels));
                        taskbar_slot(&drawn, -2, &tx, &ty, &tw, &th);
                        taskbar_slot(&drawn, TASKBAR_VOLUME, &vx, &vy, &vw, &vh);
                        band_glass(null, rect[0], rect[1], rect[2], rect[3]);
                        taskbar_glass_tab(&drawn, rect[0], rect[1], rect[2], dx, dy, dw, tx, ty, tw, th);
                        for (int y = vy - dy; y < vy - dy + vh; y++)
                                for (int x = vx - dx; x < vx - dx + vw; x++)
                                        count += look_luma(cell_lit[y * dw + x]) != look_luma(band_pixels[y * dw + x]);
                        counts[variant] = count;
                }
                check("volume: the speaker is drawn in the cell: more of it with each wave, a cross for muted, and the number for a moment",
                      counts[0] > 20 && counts[1] > counts[0] + 8 && counts[2] > counts[1] + 8 && counts[3] > counts[0] + 8 && counts[4] > 20 && counts[4] != counts[2]);
                taskbar_volume_set(1, 80, 0, 0);
                view = compositor_taskbar_view();
        }

        // The cell pressed: the table holds its rectangle, the edge its band is along and its screen for the popup to open beside, and a
        // popup that is up (the press has put it away) is not asked for again, so the cell opens it and closes it.
        {
                struct windows address_to table = compositor.table;
                int ax, ay, aw, ah;

                view = compositor_taskbar_view();
                taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch);
                taskbar_anchor(&view, TASKBAR_VOLUME, &ax, &ay, &aw, &ah);
                table->button_w = 0, table->button_h = 0;
                compositor_volume_open(cx + cw / 2, cy + ch / 2);
                check("volume: a press on the cell leaves its rectangle (along the cell, across the band), the edge and the screen for the popup",
                      table->button_x == ax && table->button_y == ay && (int)table->button_w == aw && (int)table->button_h == ah && table->button_edge == TASKBAR_BOTTOM &&
                          table->button_screen == 0 && aw == cw && ah == COMPOSITOR_PANEL);
                table->button_w = 0;
                compositor.showing[5] = 1;
                string_copy_bounded((string_address)table->name[5], (string_address) "mixer-quick", WINDOWS_NAME);
                check("volume: the popup, up, is found by its name", compositor_quick_row() == 5);
                compositor_volume_open(cx + cw / 2, cy + ch / 2);
                check("volume: and a press on the cell with it up asks for no other", table->button_w == 0);
                compositor.showing[5] = 0;
                table->name[5][0] = 0;
                check("volume: gone, it is not found", compositor_quick_row() == COMPOSITOR_NONE);
                compositor_volume_open(0, 0);
                check("volume: a press that is not on the cell leaves the table as it was", table->button_w == 0);
        }

        // The keys: five percent a press, written whole; a held key repeats; the mute key throws the mute; a volume key unmutes.
        compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
        check("volume: Volume Up is 85, written as 85 and a newline, and the mute file is not touched",
              volume_file(compositor_volume_path[1], text, sizeof(text)) && string_equals((string_address)text, (string_address) "85\n") &&
                  !volume_file(compositor_volume_path[2], other, sizeof(other)) && compositor_volume.percent == 85);
        check("volume: and the number is shown in the tray for a moment", taskbar.volume_item && taskbar.volume_percent == 85 && taskbar.volume_flash && compositor_volume.flash_until > now);
        compositor_key(0, CK_KEY_VOLUME_UP, 2, 0);
        compositor_key(0, CK_KEY_VOLUME_DOWN, 1, 0);
        compositor_key(0, CK_KEY_VOLUME_DOWN, 1, 0);
        volume_file(compositor_volume_path[1], text, sizeof(text));
        check("volume: a held key repeats and Volume Down is five less", string_equals((string_address)text, (string_address) "80\n") && compositor_volume.percent == 80);
        compositor_key(0, CK_KEY_MUTE, 1, 0);
        check("volume: Mute throws the mute on and leaves the volume", volume_file(compositor_volume_path[2], text, sizeof(text)) && string_equals((string_address)text, (string_address) "on\n") &&
                                                                          compositor_volume.muted && compositor_volume.percent == 80 && taskbar.volume_muted);
        compositor_key(0, CK_KEY_MUTE, 2, 0);
        check("volume: and a held Mute is one press", compositor_volume.muted);
        compositor_key(0, CK_KEY_MUTE, 1, 0);
        volume_file(compositor_volume_path[2], text, sizeof(text));
        check("volume: Mute again throws it off", string_equals((string_address)text, (string_address) "off\n") && !compositor_volume.muted);
        compositor_key(0, CK_KEY_MUTE, 1, 0);
        compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
        volume_file(compositor_volume_path[2], text, sizeof(text));
        volume_file(compositor_volume_path[1], other, sizeof(other));
        check("volume: a volume key unmutes, and takes its step", string_equals((string_address)text, (string_address) "off\n") && string_equals((string_address)other, (string_address) "85\n") &&
                                                                     !compositor_volume.muted);
#if MOONWATER_UI_MIXER
        // Told to the server at once over its socket (a fake here): the sink by the name the page gives it, the volume as the percent of 65536
        // and the mute as it is thrown, before the files; what the files hold is the same, and a socket that fails costs nothing but its speed.
        {
                char at_text[16];

                compositor_ctl.transport = (struct ctl_transport){0, mixer_fake_send, mixer_fake_receive, mixer_fake_now};
                compositor_ctl.open = 1;
                compositor_ctl_again = 0; // the keys before this one tried a socket that is not there, and none is tried for two seconds after
                mixer_logged = 0, mixer_fail_send = 0;
                compositor_key(0, CK_KEY_VOLUME_DOWN, 1, 0);
                check("volume: a volume key is told to the server by the sink's name, as the percent of 65536, and the file holds it too",
                      mixer_logged == 1 && mixer_log[0].command == CTL_COMMAND_SET_SINK_VOLUME && string_equals((string_address)mixer_log[0].text, (string_address) "alsa_output.test") &&
                          mixer_log[0].channels == 2 && mixer_log[0].volume == 80u * 65536 / 100 && volume_file(compositor_volume_path[1], at_text, sizeof(at_text)) &&
                          string_equals((string_address)at_text, (string_address) "80\n"));
                check("volume: and the server is waited for 20 ms at most, not the mixer's 300", mixer_timeout == COMPOSITOR_VOLUME_WAIT_MS && COMPOSITOR_VOLUME_WAIT_MS == 20);
                compositor_key(0, CK_KEY_MUTE, 1, 0);
                check("volume: the mute key is told as the request that mutes, by the same name, with no volume sent",
                      mixer_logged == 2 && mixer_log[1].command == CTL_COMMAND_SET_SINK_MUTE && mixer_log[1].mute == 1 && string_equals((string_address)mixer_log[1].text, (string_address) "alsa_output.test"));
                compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
                check("volume: a volume key on a muted machine tells the volume and then the mute off",
                      mixer_logged == 4 && mixer_log[2].command == CTL_COMMAND_SET_SINK_VOLUME && mixer_log[2].volume == 85u * 65536 / 100 && mixer_log[3].command == CTL_COMMAND_SET_SINK_MUTE &&
                          mixer_log[3].mute == 0);
                mixer_logged = 0;
                mixer_fail_send = 1;
                compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
                volume_file(compositor_volume_path[1], at_text, sizeof(at_text));
                check("volume: a socket that fails is let go and the files are written all the same", !compositor_ctl.open && string_equals((string_address)at_text, (string_address) "90\n") &&
                                                                                                          compositor_volume.percent == 90 && compositor_ctl_again > clock_monotonic_nanoseconds());
                mixer_fail_send = 0;
                compositor_ctl.transport = (struct ctl_transport){0, mixer_fake_send, mixer_fake_receive, mixer_fake_now};
                compositor_key(0, CK_KEY_VOLUME_DOWN, 1, 0);
                volume_file(compositor_volume_path[1], at_text, sizeof(at_text));
                check("volume: and none is tried again before its time, while the files go on", mixer_logged == 0 && !compositor_ctl.open && string_equals((string_address)at_text, (string_address) "85\n"));
                compositor_ctl_again = 0;
                compositor_key(0, CK_KEY_VOLUME_DOWN, 1, 0);
                check("volume: with its time up a socket that is not there is tried, fails, and costs the keys nothing", mixer_logged == 0 && !compositor_ctl.open &&
                                                                                                                         compositor_ctl_again > clock_monotonic_nanoseconds() &&
                                                                                                                         compositor_volume.percent == 80);
                // and a page with no sink name (a server that is starting) is told nothing
                compositor_ctl.transport = (struct ctl_transport){0, mixer_fake_send, mixer_fake_receive, mixer_fake_now};
                compositor_ctl.open = 1;
                compositor_volume.copy.name[0] = 0;
                compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
                check("volume: a page with no name for the sink is told nothing, and the file is written", mixer_logged == 0 && compositor_ctl.open && compositor_volume.percent == 85);
                string_copy_bounded((string_address)compositor_volume.copy.name, (string_address) "alsa_output.test", sizeof(compositor_volume.copy.name));
                compositor_ctl.open = 0;
                compositor_ctl_again = ~(positive)0; // the checks that follow tell nobody
        }
#endif

        // The ends: 100 is the most, and a press at it writes nothing; 0 is the least.
        for (int k = 0; k < 5; k++)
                compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
        volume_file(compositor_volume_path[1], text, sizeof(text));
        check("volume: 100 is the most", string_equals((string_address)text, (string_address) "100\n") && compositor_volume.percent == 100);
        system_remove_at(AT_FDCWD, (string_address)compositor_volume_path[1], 0);
        compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
        check("volume: and a press at 100 writes no file", !volume_file(compositor_volume_path[1], text, sizeof(text)) && compositor_volume.percent == 100);
        for (int k = 0; k < 21; k++)
                compositor_key(0, CK_KEY_VOLUME_DOWN, 1, 0);
        volume_file(compositor_volume_path[1], text, sizeof(text));
        check("volume: 0 is the least", string_equals((string_address)text, (string_address) "0\n") && compositor_volume.percent == 0);

        // The page is not believed for a moment after the desktop's change, and is believed after it.
        volume_put_page(52429, 0, AUDIO_STATE_MAGIC);
        compositor_volume_unmap();
        compositor_volume_look(clock_monotonic_nanoseconds());
        check("volume: a page looked at at once after our own change does not undo it", compositor_volume.percent == 0);
        compositor_volume_look(clock_monotonic_nanoseconds() + 2 * COMPOSITOR_VOLUME_HOLD_NS);
        check("volume: and one a moment later is what the server says", compositor_volume.percent == 80 && !compositor_volume.flash_until);

        // The wheel over the cell: five percent a notch, up raising it; elsewhere it is not the volume's.
        compositor.pointer_x = cx + cw / 2, compositor.pointer_y = cy + ch / 2;
        compositor_volume.hold_until = 0;
        compositor_wheel(120, 0, 0);
        volume_file(compositor_volume_path[1], text, sizeof(text));
        check("volume: a notch up over the cell is five more", string_equals((string_address)text, (string_address) "85\n") && compositor_volume.percent == 85);
        compositor_wheel(-120, 0, 0);
        compositor_wheel(-120, 0, 0);
        check("volume: and a notch down five less", compositor_volume.percent == 75);
        compositor_wheel(60, 0, 0);
        check("volume: half a notch is nothing yet", compositor_volume.percent == 75);
        compositor_wheel(60, 0, 0);
        check("volume: and the other half is the notch", compositor_volume.percent == 80);
        compositor.pointer_x = 1280, compositor.pointer_y = 700;
        compositor_wheel(120, 0, 0);
        check("volume: a notch elsewhere is not the volume's", compositor_volume.percent == 80);
        compositor.pointer_x = cx + cw / 2, compositor.pointer_y = cy + ch / 2;
        compositor_wheel(120, 1, 0);
        check("volume: and nor is one turned sideways", compositor_volume.percent == 80);

        // The microphone: a cell in the tray, before the volume's, while a program records from one (not one that records what plays, nor a paused
        // one, nor with recording turned off); the microphone key and a middle press throw its mute, over the socket and in the file.
        {
                static unsigned int mic_lit[sizeof(band_pixels) / sizeof(unsigned int)];
                static const char *const titles[3] = {"a", "b", "c"};
                struct windows address_to table = compositor.table;
                int mx, my, mw, mh, vx, vy, vw, vh, ax, ay, aw, ah, counts[2], rect[4];

                compositor_volume_path[4] = "/tmp/mw-mic.mute";
                system_remove_at(AT_FDCWD, (string_address)compositor_volume_path[4], 0);
                taskbar_settings_set("bottom", "primary", "battery none\n");
                taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
                check("mic: with no program recording there is no cell", !mic_cell(1, 0, 0) && compositor_volume.present && !compositor_volume.recording);
                check("mic: a program that records what plays is none", !mic_cell(1, 0, AUDIO_STATE_PLAYING | AUDIO_STATE_MONITOR));
                check("mic: nor is a paused one", !mic_cell(1, 0, AUDIO_STATE_PLAYING | AUDIO_STATE_CORKED));
                check("mic: nor one when recording is turned off", !mic_cell(0, 0, AUDIO_STATE_PLAYING));
                check("mic: one that records from a microphone puts the cell in the tray", mic_cell(1, 0, AUDIO_STATE_PLAYING) && compositor_volume.recording == 1 && taskbar.mic_item);
                view = compositor_taskbar_view();
                taskbar_slot(&view, -2, &x, &y, &w, &h);
                taskbar_slot(&view, TASKBAR_MIC, &mx, &my, &mw, &mh);
                taskbar_slot(&view, TASKBAR_VOLUME, &vx, &vy, &vw, &vh);
                taskbar_slot(&view, -3, &bx, &by, &bw, &bh);
                check("mic: the cell is at the start of the tray, the volume's after it and the clock's after that, the tray two cells longer than without them",
                      mx == x && vx == mx + mw && bx == vx + vw && w == before + mw + vw && taskbar_hit_reach(&view, mx + mw / 2, my + mh / 2, 0) == TASKBAR_MIC &&
                          taskbar_hit_reach(&view, vx + vw / 2, vy + vh / 2, 0) == TASKBAR_VOLUME);
                taskbar_settings_set("left", "primary", "battery none\n");
                view = compositor_taskbar_view();
                taskbar_slot(&view, TASKBAR_MIC, &mx, &my, &mw, &mh);
                taskbar_slot(&view, TASKBAR_VOLUME, &vx, &vy, &vw, &vh);
                check("mic: on a side band the cell is above the volume's", my + mh == vy && mx == vx);
                taskbar_settings_set("bottom", "primary", "battery none\n");
                taskbar_tray_set("{time.24h.hour}:{time.24h.min}");
                view = compositor_taskbar_view();
                // A format of the person's keeps the cell (the owner's e8cccd1): at the tray's start, the volume's after it and the format's text after that.
                check("mic: a tray of the person's own format keeps the cell, at the tray's start, the volume's after it and the format's text after that",
                      taskbar_slot(&view, TASKBAR_MIC, &mx, &my, &mw, &mh) && taskbar_slot(&view, TASKBAR_VOLUME, &vx, &vy, &vw, &vh) &&
                          taskbar_slot(&view, -2, &x, &y, &w, &h) && taskbar_slot(&view, -9, &bx, &by, &bw, &bh) && mx == x && vx == mx + mw && bx == vx + vw &&
                          mw > 0 && taskbar_hit_reach(&view, mx + mw / 2, my + mh / 2, 0) == TASKBAR_MIC);
                taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
                check("mic: and when the server goes the cell goes with it", mic_cell(1, 0, AUDIO_STATE_PLAYING) && (volume_put_page(52429, 0, 0), compositor_volume_unmap(), compositor_volume.hold_until = 0,
                                                                                                                        compositor_volume_look(clock_monotonic_nanoseconds()), !taskbar.mic_item));

                // The cell drawn: the microphone in it, struck when the microphone is muted.
                mic_cell(1, 0, AUDIO_STATE_PLAYING);
                for (int variant = 0; variant < 2; variant++)
                {
                        struct taskbar_view drawn;
                        int tx, ty, tw, th, dx, dy, dw, dh, count = 0;

                        taskbar_mic_set(1, variant);
                        drawn = band_view(TASKBAR_BOTTOM, titles);
                        taskbar_band(&drawn, &dx, &dy, &dw, &dh);
                        taskbar_draw(&drawn, 1ul << TASKBAR_TAB_BITS, band_glass, null, rect);
                        memory_copy(mic_lit, band_pixels, sizeof(band_pixels));
                        taskbar_slot(&drawn, -2, &tx, &ty, &tw, &th);
                        taskbar_slot(&drawn, TASKBAR_MIC, &mx, &my, &mw, &mh);
                        band_glass(null, rect[0], rect[1], rect[2], rect[3]);
                        taskbar_glass_tab(&drawn, rect[0], rect[1], rect[2], dx, dy, dw, tx, ty, tw, th);
                        for (int y2 = my - dy; y2 < my - dy + mh; y2++)
                                for (int x2 = mx - dx; x2 < mx - dx + mw; x2++)
                                        count += look_luma(mic_lit[y2 * dw + x2]) != look_luma(band_pixels[y2 * dw + x2]);
                        counts[variant] = count;
                }
                check("mic: the microphone is drawn in the cell, and muted a diagonal more", counts[0] > 20 && counts[1] > counts[0] + 4);
                taskbar_mic_set(1, 0);

                // Pressed: the table holds the cell's rectangle for the popup, which a popup that is up does not ask for again.
                mic_cell(1, 0, AUDIO_STATE_PLAYING);
                view = compositor_taskbar_view();
                taskbar_slot(&view, TASKBAR_MIC, &mx, &my, &mw, &mh);
                taskbar_anchor(&view, TASKBAR_MIC, &ax, &ay, &aw, &ah);
                table->button_w = 0, table->button_h = 0;
                compositor_mic_open(mx + mw / 2, my + mh / 2);
                check("mic: a press on the cell leaves its rectangle, the edge and the screen for the popup",
                      table->button_x == ax && table->button_y == ay && (int)table->button_w == aw && (int)table->button_h == ah && table->button_edge == TASKBAR_BOTTOM &&
                          table->button_screen == 0 && aw == mw && ah == COMPOSITOR_PANEL && ax == mx);
                table->button_w = 0;
                compositor.showing[5] = 1;
                string_copy_bounded((string_address)table->name[5], (string_address) "mixer-quick", WINDOWS_NAME);
                compositor_mic_open(mx + mw / 2, my + mh / 2);
                check("mic: and with a popup up it asks for no other", table->button_w == 0);
                compositor.showing[5] = 0;
                table->name[5][0] = 0;

                // The key: the mute thrown and written whole; a held key is one press; with no microphone, or no server, nothing.
                mic_cell(1, 0, AUDIO_STATE_PLAYING);
                compositor_key(0, CK_KEY_MICMUTE, 1, 0);
                check("mic: the microphone key throws the mute on, written as on and a newline, and the cell is struck",
                      volume_file(compositor_volume_path[4], text, sizeof(text)) && string_equals((string_address)text, (string_address) "on\n") && compositor_volume.mic_muted && taskbar.mic_muted);
                compositor_key(0, CK_KEY_MICMUTE, 2, 0);
                check("mic: and a held key is one press", compositor_volume.mic_muted);
                compositor_key(0, CK_KEY_MICMUTE, 1, 0);
                volume_file(compositor_volume_path[4], text, sizeof(text));
                check("mic: pressed again it throws it off", string_equals((string_address)text, (string_address) "off\n") && !compositor_volume.mic_muted);
                mic_cell(1, 1, AUDIO_STATE_PLAYING);
                compositor_mic_step();
                volume_file(compositor_volume_path[4], text, sizeof(text));
                check("mic: a muted microphone is unmuted by the same (a middle press is the same step)", string_equals((string_address)text, (string_address) "off\n") && !compositor_volume.mic_muted);
                check("mic: without a program recording the cell is there for a moment to see it thrown, and goes",
                      !mic_cell(1, 0, 0) && (compositor_mic_step(), taskbar.mic_item && taskbar.mic_muted && compositor_volume.mic_flash_until > clock_monotonic_nanoseconds()) &&
                          (compositor_volume.hold_until = 0, compositor_volume_look(clock_monotonic_nanoseconds() + 2 * COMPOSITOR_VOLUME_FLASH_NS), !taskbar.mic_item));
                mic_put_page(1, 0, AUDIO_STATE_PLAYING);
                compositor_volume_unmap();
                compositor_volume.hold_until = 0;
                compositor_volume_look(clock_monotonic_nanoseconds());
                compositor_volume.copy.input_name[0] = 0;
                compositor_volume.hold_until = clock_monotonic_nanoseconds() + 10 * COMPOSITOR_VOLUME_HOLD_NS; // the page is not read again
                system_remove_at(AT_FDCWD, (string_address)compositor_volume_path[4], 0);
                compositor_key(0, CK_KEY_MICMUTE, 1, 0);
                check("mic: with no microphone named the key does nothing", !volume_file(compositor_volume_path[4], text, sizeof(text)));
#if MOONWATER_UI_MIXER
                // Told to the server over its socket, by the source's name, before the file.
                mic_cell(1, 0, AUDIO_STATE_PLAYING);
                compositor_ctl.transport = (struct ctl_transport){0, mixer_fake_send, mixer_fake_receive, mixer_fake_now};
                compositor_ctl.open = 1;
                compositor_ctl_again = 0;
                mixer_logged = 0, mixer_fail_send = 0;
                compositor_key(0, CK_KEY_MICMUTE, 1, 0);
                check("mic: the key is told to the server as the request that mutes the source, by its name, with the file the same",
                      mixer_logged == 1 && mixer_log[0].command == CTL_COMMAND_SET_SOURCE_MUTE && mixer_log[0].mute == 1 && string_equals((string_address)mixer_log[0].text, (string_address) "alsa_input.test") &&
                          volume_file(compositor_volume_path[4], text, sizeof(text)) && string_equals((string_address)text, (string_address) "on\n"));
                check("mic: waiting for the server at most as long as the volume's", mixer_timeout == COMPOSITOR_VOLUME_WAIT_MS);
                mixer_fail_send = 1;
                compositor_key(0, CK_KEY_MICMUTE, 1, 0);
                volume_file(compositor_volume_path[4], text, sizeof(text));
                check("mic: a socket that fails is let go and the file is written all the same", !compositor_ctl.open && string_equals((string_address)text, (string_address) "off\n") &&
                                                                                                    compositor_ctl_again > clock_monotonic_nanoseconds());
                mixer_fail_send = 0;
                compositor_ctl.open = 0;
                compositor_ctl_again = ~(positive)0;
#endif
                system_remove_at(AT_FDCWD, (string_address)compositor_volume_path[4], 0);
                compositor_volume_path[4] = SETTING_AUDIO_SOURCE_MUTE_PATH;
                taskbar_mic_set(0, 0);
                volume_put_page(52429, 0, AUDIO_STATE_MAGIC);
                compositor_volume_unmap();
                compositor_volume.hold_until = 0;
                compositor_volume_look(clock_monotonic_nanoseconds());
        }

        // The server goes: the cell goes with it, and the keys have nothing to turn.
        volume_put_page(52429, 0, 0);
        compositor_volume_unmap();
        compositor_volume.hold_until = 0;
        compositor_volume_look(clock_monotonic_nanoseconds());
        view = compositor_taskbar_view();
        check("volume: a server that is gone takes the cell away", !compositor_volume.present && !taskbar_slot(&view, TASKBAR_VOLUME, &cx, &cy, &cw, &ch));
        for (int k = 0; k < 3; k++)
                system_remove_at(AT_FDCWD, (string_address)compositor_volume_path[k], 0);
        compositor_key(0, CK_KEY_VOLUME_UP, 1, 0);
        check("volume: and a key then writes nothing", !volume_file(compositor_volume_path[1], text, sizeof(text)));
        compositor_volume_unmap();
        compositor_volume_path[0] = AUDIO_STATE_PATH, compositor_volume_path[1] = SETTING_AUDIO_VOLUME_PATH, compositor_volume_path[2] = SETTING_AUDIO_MUTE_PATH;
        compositor_volume_path[3] = CTL_SOCKET;
        memory_zero(&compositor_ctl, sizeof(compositor_ctl));
        taskbar_volume_set(0, 0, 0, 0);
        taskbar_standard();
}

/*
        The chrome at the default theme: the line of the window with the keys and of one without, the desktop and each
        window's close and minimise buttons, all in one scene, which is pinned by a hash of the whole output so that a
        changed default pixel is seen. The tabs' inks the band takes for a tab drawn without a font are pinned too.
*/
#define CHROME_SCENE_HASH 0x71b1c00du // the moon's glow a touch at rest since it is always there (TASKBAR_MOON_REST_GLOW)
static void check_compositor_chrome(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    unsigned int hash = 2166136261u;
    struct windows address_to table;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("chrome: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    table->x[2] = 180, table->y[2] = 110, table->width[2] = 60, table->height[2] = 60;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    glass_paint(2, 0, 0, 60, 60, 0x404040u, true);
    compositor.focus = 1;
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    check("chrome: on the dark standard the window with the keys has its line at a7a7a7, and the one without the outline's 52525b",
          glass_at(49, 70) == 0xa7a7a7u && glass_at(179, 140) == 0x52525bu && glass_at(100, 29) == 0xa7a7a7u);
    for (unsigned int at = 0; at < GLASS_SIDE * GLASS_SIDE; at++)
        hash = (hash ^ (screen[at] & 0xffffffu)) * 16777619u;
    string_format(log, "  chrome: the default scene hashes to %p\n", (positive)hash);
    check("chrome: the default scene, desktop, lines, close and minimise buttons, is the picture pinned by its hash", hash == CHROME_SCENE_HASH);
#if MOONWATER_UI_TASKBAR
    check("chrome: a tab drawn without a font is black, the panel's ink when away and 38383c when lit",
          taskbar_tab_ink(-1, 0) == 0u && taskbar_tab_ink(0, 0) == 0u && taskbar_tab_ink(0, COMPOSITOR_TAB_AWAY) == 0x141416u &&
              taskbar_tab_ink(0, COMPOSITOR_TAB_LIT) == 0x38383cu);
#endif
}

/*
        A frameless window with no shadow is told from the rest by its line alone. Two windows over the default wallpaper on the
        light theme and on the dark, the one at 50, 30 with the keys: its line stands out 3 to 1 or more from the line of the one
        without them, and that line stands out 2 to 1 or more from the wallpaper, so a dark window without the keys does not
        vanish into it (its paper, 111113, is 1.13 to 1 on the wallpaper). Every accent of 4096 keeps the 3 to 1, light and dark.
*/
static void check_compositor_focus_line(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    unsigned int lit[2] = {0, 0}, unlit[2] = {0, 0}, wall[2] = {0, 0};
    b32 accents = true;

    for (unsigned int dark = 0; dark < 2; dark++)
    {
        struct windows address_to table;

        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
        compositor.width = compositor.height = GLASS_SIDE;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = screen;
        compositor.scanout_pitch = GLASS_SIDE * 4;
        if (!glass_table_map())
        {
            check("focus line: the table is mapped", false);
            return;
        }
        table = compositor.table;
        table->themed = 1, table->theme = dark ? UI_THEME_DARK : UI_THEME_LIGHT, table->accent = UI_ACCENT_STANDARD;
        table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
        table->x[2] = 180, table->y[2] = 110, table->width[2] = 60, table->height[2] = 60;
        glass_paint(1, 0, 0, 120, 80, dark ? 0x111113u : 0xf7f7f8u, true);
        glass_paint(2, 0, 0, 60, 60, dark ? 0x111113u : 0xf7f7f8u, true);
        compositor.focus = 1;
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        lit[dark] = glass_at(49, 70) | 0xff000000u, unlit[dark] = glass_at(179, 140) | 0xff000000u, wall[dark] = glass_at(5, 5) | 0xff000000u;
        string_format(log, "  focus line: %s: with the keys %p, without %p, %p hundredths apart; without on the wallpaper %p hundredths\n",
                      dark ? "dark" : "light", (positive)(lit[dark] & 0xffffffu), (positive)(unlit[dark] & 0xffffffu),
                      (positive)ui_contrast(lit[dark], unlit[dark]), (positive)ui_contrast(unlit[dark], wall[dark]));
    }
    // Every accent of four bits a channel, set on the table and read back through the lines the frames are given.
    for (unsigned int k = 0; k < 4096; k++)
        for (unsigned int dark = 0; dark < 2; dark++)
        {
            compositor.table->themed = 1, compositor.table->theme = dark ? UI_THEME_DARK : UI_THEME_LIGHT;
            compositor.table->accent = (k >> 8 & 15) * 0x110000u | (k >> 4 & 15) * 0x1100u | (k & 15) * 0x11u;
            accents &= ui_contrast(compositor_line_lit() | 0xff000000u, compositor_line_unlit() | 0xff000000u) >= 300;
        }
    compositor.table->themed = 0;
    check("focus line: on the light theme the window with the keys has a line 3 to 1 or more from the line of one without them",
          wall[0] == (COMPOSITOR_INK_DESKTOP | 0xff000000u) && ui_contrast(lit[0], unlit[0]) >= 300);
    check("focus line: on the dark theme too, and the line of a window without the keys is 2 to 1 or more on the dark default wallpaper",
          wall[1] == (COMPOSITOR_INK_DESKTOP | 0xff000000u) && ui_contrast(lit[1], unlit[1]) >= 300 && ui_contrast(unlit[1], wall[1]) >= 200);
    check("focus line: every accent, light and dark, has its line with the keys 3 to 1 or more from the line without them", accents);
}

/*
        The minimise button: a dash in the close button's ink, to the left of the close disc of a window that has room
        for both. A press on it puts the window away, and neither the press nor its release reaches the window or
        starts a drag. A window too narrow for two discs, and one with no close button, has none: its press is its own.
*/
static void check_compositor_minimise(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    struct compositor_frame frame;
    int bx, by, side;
    unsigned int head;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = screen;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("minimise: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50, table->y[1] = 30, table->width[1] = 120, table->height[1] = 80;
    table->x[2] = 180, table->y[2] = 110, table->width[2] = 60, table->height[2] = 60;
    table->x[3] = 20, table->y[3] = 150, table->width[3] = 120, table->height[3] = 60;
    table->glass[3] = WINDOW_NO_CLOSE;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    glass_paint(2, 0, 0, 60, 60, 0x404040u, true);
    glass_paint(3, 0, 0, 120, 60, 0x404040u, true);
    compositor.focus = 1;
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();

    // The close disc is where it was; the dash is 3 px in from each side of the disc to its left, two pixels thick.
    compositor_frame_of(1, &frame);
    check("minimise: the close disc of a window with room for both is where it was",
          compositor_close_box(&frame, &bx, &by, &side) && bx == 151 && by == 35 && side == 14);
    check("minimise: a dash in the close button's ink sits to the left of the close disc, two pixels thick",
          glass_at(139, 41) == glass_at(139, 42) && glass_at(139, 42) != 0x404040u && glass_at(139, 40) == 0x404040u &&
              glass_at(139, 43) == 0x404040u && glass_at(134, 42) == 0x404040u && glass_at(144, 42) == 0x404040u);

    compositor.pointer_x = 139, compositor.pointer_y = 42;
    head = table->event_head[1];
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
    check("minimise: a press on the dash puts its window away",
          (compositor.state[1] & WINDOW_MINIMIZED) && (table->state[1] & WINDOW_MINIMIZED));
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);
    check("minimise: the press and its release are no drag, no grab and no event to the window",
          compositor.drag == COMPOSITOR_NONE && compositor.grab == COMPOSITOR_NONE && table->event_head[1] == head &&
              (compositor.state[1] & WINDOW_MINIMIZED));

    // Window 2 is too narrow for two discs: a press where its dash would be starts its drag, and nothing is put away.
    compositor.pointer_x = 209, compositor.pointer_y = 122;
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
    check("minimise: a window with no room for two discs has none, and its press is its own",
          !(compositor.state[2] & WINDOW_MINIMIZED) && compositor.drag == 2);
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);

    // Window 3 has no close button, so no minimise button either.
    compositor.pointer_x = 109, compositor.pointer_y = 162;
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 1);
    compositor_touch_button(COMPOSITOR_BTN_LEFT, 0);
    check("minimise: a window with no close button has no minimise button", !(compositor.state[3] & WINDOW_MINIMIZED));
}

/*
        The close and minimise buttons are one look. Over a plain window of the kit, its dark paper 111113 and its light f7f7f8,
        at 100 and 200 percent, every pixel of both buttons is the paper, the theme's text ink (ededef on the dark paper, 18181b
        on the light), or a mix of the two at a stroke's edge: the strokes are the ink and no halo's box is seen round either.
        Over a busy picture (stripes of black, white and blue) neither draws anything off its strokes: every pixel of both
        buttons that the X's strokes or the dash do not cover is the picture's own.
*/
static unsigned int glyph_stripe(int x, int y)
{
    return (x / 3 + y / 5) % 3 == 0 ? 0x000000u : (x / 3) % 3 == 1 ? 0xffffffu : 0x2a7fd4u;
}

static void check_compositor_glyphs(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct compositor_frame frame;
    int plain = 1, inked = 1, scenes = 0, x, y, side;
    unsigned int stroked = 0, changed = 0;

    for (int scale = 100; scale <= 200; scale += 100)
        for (int dark = 0; dark < 2; dark++)
        {
            unsigned int paper = dark ? 0x111113u : 0xf7f7f8u, ink = dark ? 0xededefu : 0x18181bu;

            compositor_zero();
            compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
            compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
            compositor.width = compositor.height = GLASS_SIDE;
            compositor.scale = scale;
            compositor.card = -1;
            compositor.scanout = screen;
            compositor.scanout_pitch = GLASS_SIDE * 4;
            if (!glass_table_map())
            {
                check("glyphs: the table is mapped", false);
                return;
            }
            compositor.table->x[1] = 30, compositor.table->y[1] = 30, compositor.table->width[1] = 200, compositor.table->height[1] = 120;
            glass_paint(1, 0, 0, 200, 120, paper, true);
            compositor.focus = 1;
            compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
            compositor_present();
            compositor_frame_of(1, &frame);
            for (int which = 0; which < 2; which++)
            {
                int bx, by, half = compositor_px1(scale), in = COMPOSITOR_MINIMIZE_IN * scale / 100;

                if (!(which ? compositor_minimize_box(&frame, &bx, &by, &side) : compositor_close_box(&frame, &bx, &by, &side)))
                {
                    plain = 0;
                    continue;
                }
                scenes++;
                for (y = by; y < by + side; y++)
                    for (x = bx; x < bx + side; x++)
                    {
                        unsigned int shown = glass_at(x, y), edge = 0;

                        for (unsigned int k = 0; k <= 16; k++)
                            edge |= shown == compositor_mix(paper, ink, k * 255 / 16);
                        plain &= edge;
                    }
                // The X's middle is wholly stroke, and so are the dash's top row at its middle and its bottom row at its start.
                inked &= glass_at(bx + side / 2, which ? by + side / 2 - half : by + side / 2) == ink;
                inked &= !which || glass_at(bx + in, by + side / 2 + half - 1) == ink;
            }
        }
    string_format(log, "  glyphs: %p buttons over the kit's plain papers\n", (positive)scenes);
    check("glyphs: over a plain window of the kit, dark and light, at 100 and 200 percent, the close and minimise buttons are the paper and the "
          "theme's text ink alone, with no halo's box round either",
          scenes == 8 && plain);
    check("glyphs: the X's strokes and the dash are the theme's text ink, ededef on the dark paper and 18181b on the light", scenes == 8 && inked);

    // A busy picture under both buttons, at 100 percent: off its strokes neither draws anything.
    {
        unsigned int address_to pixels = windows_buffer(compositor.table, 1, 0);
        int bx, by, mx, my, half, in;

        compositor.scale = 100;
        for (y = 0; y < 120; y++)
            for (x = 0; x < 200; x++)
                pixels[y * GLASS_SIDE + x] = glyph_stripe(x, y);
        compositor.table->buffer_index[1] = 0, compositor.table->damage_width[1] = 0, compositor.table->damage_height[1] = 120;
        compositor.table->frame[1]++;
        compositor_take(1);
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        compositor_frame_of(1, &frame);
        if (!compositor_close_box(&frame, &bx, &by, &side) || !compositor_minimize_box(&frame, &mx, &my, &side))
        {
            check("glyphs: the busy window has both buttons", false);
            return;
        }
        half = compositor_px1(100), in = COMPOSITOR_MINIMIZE_IN;
        for (int which = 0; which < 2; which++)
        {
            int ox = which ? mx : bx, oy = which ? my : by;

            for (y = oy; y < oy + side; y++)
                for (x = ox; x < ox + side; x++)
                {
                    unsigned int under = glyph_stripe(x - frame.x, y - frame.y), shown = glass_at(x, y);
                    // The strokes: the dash's bar, and every pixel the X's strokes cover any of (sixteen samples a pixel, as it is drawn).
                    int stroke = which ? x >= ox + in && x < ox + side - in && y >= oy + side / 2 - half && y < oy + side / 2 + half
                                       : compositor_close_sample(side, half, x - ox, y - oy) > 0;

                    stroked += stroke && shown != under;
                    changed += !stroke && shown != under;
                }
        }
        string_format(log, "  glyphs: over the stripes %p pixels of the strokes are drawn and %p round them\n", (positive)stroked, (positive)changed);
        check("glyphs: over a busy picture neither button draws anything off its strokes, the X no more than the dash",
              stroked > 20 && changed == 0);
    }
}

