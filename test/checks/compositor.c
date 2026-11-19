/*
        test/checks/compositor.c: the compositor's rows: a first show, glass, partial presents and runs, scanout,
        screens and scales, corners and turns, the snap preview, shared edges, Alt-Tab, the desktop, the taskbar and
        its launcher, bands.

        Rows of the compositor's compose.c, glass.c, damage.c, scanout.c, present.c and outputs.c. One part of the
        program that the terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model kit's sections
        share (checks.c opens and closes it). The terminal's program, the kit's frames, the draw list and the GPU
        model's kit run with the four faces: fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf
        fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

/*
        A program that shows twice before the compositor has looked at either
        has its other buffer back all the same. The compositor reads the row
        when it is knocked on, so it finds the second picture and has never
        shown the first: shown is the last frame, or the program waits for a
        buffer that never comes back and draws nothing again.
*/
static fn check_take(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);

        compositor_zero();
        compositor.table = table_at;
        compositor.width = compositor.height = 256;
        compositor.scale = 100;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        table_at->width[2] = table_at->height[2] = 16;
        table_at->x[2] = table_at->y[2] = 40;
        table_at->frame[2] = 2;
        table_at->buffer_index[2] = 1;
        string_copy((string_address)table_at->title[2], (string_address) "two");
        table_at->asked[2] = 1;

        compositor_take(2);
        check("compositor: a window that showed twice before it was looked at has the other buffer back",
              table_at->shown[2] == 2 && compositor.front[2] == 1 && compositor.showing[2]);
        check("compositor: a window that shows is put where it asked, on top, with the keys and a tab",
              compositor.x[2] == 40 && compositor.y[2] == 40 && compositor.stacked == 1 && compositor.stack[0] == 2 &&
                  table_at->focus == 2 && compositor_windows() == 1 && compositor.listed[0] == 2);

        // And the ordinary case, a buffer at a time; a title changed with no picture is taken on the knock.
        table_at->frame[2] = 3;
        table_at->buffer_index[2] = 0;
        compositor_take(2);
        check("compositor: the next picture is taken from the buffer it names", table_at->shown[2] == 3 && compositor.front[2] == 0);
        string_copy((string_address)table_at->title[2], (string_address) "renamed");
        compositor_take(2);
        check("compositor: a title with no picture is taken", !memory_compare(compositor.title[2], "renamed", 8) &&
                                                                   table_at->shown[2] == 3);

        // Its door closed: gone from the screen and the tabs, and the row free.
        table_at->pid[2] = 77;
        compositor_leave(2);
        check("compositor: a window whose program has gone is off the screen and its row is free",
              !compositor.showing[2] && !compositor.stacked && compositor.focus == COMPOSITOR_NONE && !table_at->pid[2] &&
                  table_at->focus == WINDOWS);
}

/*
        A window's first show lands: 96 percent of its size at its first present, eased up to all of it once its
        time is past, and then nothing is damaged, so the loop goes idle. A window shown again does not land, and
        reduced motion lands nothing.
*/
static fn check_land_setup(struct windows address_to table, unsigned int motion)
{
        compositor_zero();
        windows_scratch_zero(table);
        compositor.table = table;
        compositor.width = compositor.height = 512;
        compositor.scale = 100;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        table->width[2] = 200;
        table->height[2] = 100;
        table->x[2] = 100;
        table->y[2] = 100;
        table->frame[2] = 2;
        table->buffer_index[2] = 1;
        table->motion = motion;
        string_copy((string_address)table->title[2], (string_address) "land");
}

static fn check_land(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct compositor_frame frame;
        positive now;

        check_land_setup(table_at, 0);
        compositor_take(2);
        compositor_frame_of(2, &frame);
        check("compositor: a window's first frame is at 96 percent of its size, about its centre",
              compositor.landing[2] && compositor.w[2] == 200 && frame.width == 192 && frame.height == 96 &&
                  frame.x == compositor.x[2] + 4 && frame.y == compositor.y[2] + 2 && frame.source_w == 200 &&
                  frame.source_h == 100);

        now = clock_monotonic_nanoseconds();
        compositor.land_at[2] = now - 85 * UI_MS;
        compositor_frame_of(2, &frame);
        check("compositor: half way through its landing the window is between 96 and 100 percent",
              compositor.landing[2] && frame.width > 192 && frame.width < 200 && frame.height >= 96 && frame.height <= 100);

        compositor.damages = 0;
        compositor.land_at[2] = now - 200 * UI_MS;
        compositor_land_keep();
        check("compositor: once its time is past the window has landed, and the frame that lands it is damaged",
              !compositor.landing[2] && compositor.damages > 0);
        compositor_frame_of(2, &frame);
        check("compositor: a landed window is drawn at its own size and not scaled",
              frame.width == 200 && frame.height == 100 && frame.x == compositor.x[2] && frame.y == compositor.y[2] &&
                  !frame.source_w);
        compositor.damages = 0;
        compositor_land_keep();
        check("compositor: a landed window damages nothing, so the compositor is idle", compositor.damages == 0);

        table_at->frame[2] = 3;
        table_at->buffer_index[2] = 0;
        compositor_take(2);
        check("compositor: a window shown again does not land", !compositor.landing[2]);

        check_land_setup(table_at, 1);
        compositor_take(2);
        compositor_frame_of(2, &frame);
        check("compositor: with reduced motion a window's first picture is at its size at once",
              !compositor.landing[2] && frame.width == 200 && frame.height == 100 && !frame.source_w);
}

/*
        Glass. A window that asks for it is what is under it, blurred and
        tinted, with its premultiplied picture over that: an opaque pixel
        is its own colour, a clear one the tint of the blur, and one between
        the two mixed. The backdrop is blurred again only where the scene
        under the window changed, as far as the blur reaches from the change
        -- and what that leaves is, byte for byte, what blurring all of it
        from nothing makes. A change of the window's own picture, or of a
        window over it, blurs nothing again.
*/
#define GLASS_SIDE 256

static unsigned int address_to glass_table_map(void)
{
        unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
        unsigned long buffer = ((unsigned long)GLASS_SIDE * GLASS_SIDE * 4 + 4095) & ~4095ul;
        long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 3, 0x4022, (positive)-1, 0);

        if (mapped < 0 && mapped > -4096)
                return null;
        compositor.table = (struct windows address_to)mapped;
        compositor.table->motion = 1; // a window's first picture is drawn at its size: these checks compare pictures
        compositor.pixels = compositor.table->pixels = header;
        compositor.buffer = compositor.table->buffer = buffer;
        compositor.table->stride = compositor.table->output_width = compositor.table->output_height = GLASS_SIDE;
        return (unsigned int address_to)mapped;
}

// A rectangle of a row's buffer 0 in one colour, shown as the row's next picture with that as its damage.
static fn glass_paint(int row, int x, int y, int width, int height, unsigned int colour, b32 whole)
{
        struct windows address_to table = compositor.table;
        unsigned int address_to pixels = windows_buffer(table, row, 0);

        for (int line = y; line < y + height; line++)
                for (int column = x; column < x + width; column++)
                        pixels[line * GLASS_SIDE + column] = colour;
        table->buffer_index[row] = 0;
        table->damage_x[row] = x;
        table->damage_y[row] = y;
        table->damage_width[row] = whole ? 0 : width;
        table->damage_height[row] = height;
        table->frame[row]++;
        compositor_take(row);
}

static unsigned int glass_at(int x, int y)
{
        return compositor.scanout[y * GLASS_SIDE + x] & 0xffffffu;
}

// A premultiplied pixel over an opaque one, each colour rounded to the nearest: what memory_glass_over_u32 gives.
static unsigned int glass_over(unsigned int pixel, unsigned int under)
{
        unsigned int rest = 255 - (pixel >> 24), out = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                unsigned int product = (under >> shift & 255) * rest + 128, channel = (pixel >> shift & 255) + ((product + (product >> 8)) >> 8);

                out |= (channel > 255 ? 255 : channel) << shift;
        }
        return out;
}

// A clear pixel of glass with its grain taken off again, which never took it past a byte's end.
static unsigned int glass_ungrained(unsigned int pixel, unsigned int side, int x, int y)
{
        const unsigned int address_to grain = compositor_grain_row(side, x, y);

        return pixel - grain[0] + grain[64];
}

/*
        Partial is whole. A present of only what changed is the same screen as a
        present of the whole output: the windows and their corners, the close
        buttons (whose X is the mean under its box, so a change anywhere in the
        box changes all of it), the glass under them, and the band of tabs, whose
        glass reaches a little past its own rows. Steps paint, move, raise and
        change the corners of the windows at random, each followed by a present
        of its damage and then of the whole output, and the two screens must agree.
*/
static void check_compositor_partial(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE], band[GLASS_SIDE * GLASS_SIDE], made[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;
    unsigned int seed = 7, mismatched = 0, steps = 0;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = made;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    compositor.band = band;
    if (!glass_table_map())
    {
        check("partial: the table is mapped", false);
        return;
    }
    table = compositor.table;
    for (int row = 1; row <= 4; row++)
    {
        table->x[row] = row == 4 ? 130 : 10 + 40 * row;
        table->y[row] = row == 4 ? 100 : 6 + 20 * row;
        table->width[row] = row == 4 ? 120 : 150;
        table->height[row] = row == 4 ? 120 : 120;
        table->glass[row] = row == 2 ? WINDOW_GLASS : 0;
        glass_paint(row, 0, 0, table->width[row], table->height[row], row == 1 ? 0x2060c0u : row == 2 ? 0xc04020u : row == 3 ? 0x20c060u : 0xa0a0a0u, true);
    }
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();

    for (int step = 0; step < 300; step++)
    {
        int row;
        unsigned int op;

        seed = seed * 1664525u + 1013904223u;
        row = 1 + (int)((seed >> 16) % 3);
        op = (seed >> 8) % 6;
        if (op == 5)
        {
                // The bottom of the window that stands nearest the band, where the band's glass reaches up to.
                row = 4;
                glass_paint(row, (int)((seed >> 4) % 100), 100 + (int)((seed >> 12) % 20), 1 + (int)((seed >> 20) % 40),
                            1 + (int)((seed >> 24) % 4), seed * 40503u, false);
        }
        else if (op == 0)
                glass_paint(row, (int)((seed >> 4) % 100), (int)((seed >> 12) % 90), 1 + (int)((seed >> 20) % 40),
                            1 + (int)((seed >> 24) % 30), seed * 2654435761u, false);
        else if (op == 1)
        {
                // The close button's box, from its corner in: a change in part of it.
                glass_paint(row, 120 + (int)((seed >> 4) % 20), 3 + (int)((seed >> 12) % 14), 1 + (int)((seed >> 16) % 6),
                            1 + (int)((seed >> 20) % 6), seed * 2246822519u, false);
        }
        else if (op == 2)
        {
                compositor_damage_row(row);
                compositor_place(row, 6 + (int)((seed >> 4) % 120), 4 + (int)((seed >> 12) % 100));
                compositor_damage_row(row);
        }
        else if (op == 3)
                compositor_raise(row);
        else
                compositor_focus(row);
        compositor_present();
        memory_copy(screen, made, sizeof(screen));
        compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
        compositor_present();
        steps++;
        if (memory_compare(made, screen, sizeof(screen)))
                mismatched++;
    }
    compositor.band = 0;
    check("partial: every change presented alone is the screen the whole output makes", mismatched == 0 && steps == 300);
}

/*
        A picture the card shows straight from its buffer (a window on the overlay, or one filling the output) is
        said taken only by the present that shows it, and a present comes only for damage: a picture that changed no
        pixel still asks for one, or its program waits for the other buffer until something else is damaged.
*/
static void check_compositor_held(void)
{
    struct windows address_to table;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.direct_row = compositor.plane_row = compositor.flip_row = -1;
    compositor.width = compositor.height = GLASS_SIDE;
    compositor.scale = 100;
    compositor.card = -1;
    static unsigned int made[GLASS_SIDE * GLASS_SIDE];
    compositor.scanout = made;
    compositor.scanout_pitch = GLASS_SIDE * 4;
    if (!glass_table_map())
    {
        check("held: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 30, table->y[1] = 20, table->width[1] = 150, table->height[1] = 120;
    glass_paint(1, 0, 0, 150, 120, 0x2060c0u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    // On the overlay now: the next picture, of nothing changed, into the other buffer.
    compositor.direct_row = 1;
    table->buffer_index[1] = 1;
    table->damage_x[1] = 0, table->damage_y[1] = 0, table->damage_width[1] = 150, table->damage_height[1] = 0;
    table->frame[1]++;
    compositor_take(1);
    check("held: a picture shown straight is not said taken before the present that shows it",
          table->shown[1] != table->frame[1] && (rows_low(&compositor.held_rows) & 2u));
    check("held: and one that changed no pixel still asks for that present", compositor.damages > 0);
    compositor.direct_row = -1;
    compositor.held_rows = rows_from(0);
    // Not shown straight, and no job of the card reading it in place (compositor_card_reads): the next picture is taken at once.
    table->buffer_index[1] = 0;
    table->frame[1]++;
    compositor_take(1);
    check("held: a picture no card job reads in place is said taken at once, and not held",
          !compositor_card_reads(1) && table->shown[1] == table->frame[1] && !(rows_low(&compositor.held_rows) & 2u));
    // A job of the card still reading the row's picture in place (the late present's): the next picture is held as the direct row's is.
    compositor_card.on = 1, compositor_card.rows = rows_from(1u << 1);
    table->buffer_index[1] = 1;
    table->frame[1]++;
    compositor_take(1);
    check("held: a picture a card job may still read in place is not said taken, and is held for the job's end",
          compositor_card_reads(1) && table->shown[1] != table->frame[1] && (rows_low(&compositor.held_rows) & 2u) &&
              compositor.taken_held[1] == table->frame[1]);
    compositor_card.on = 0, compositor_card.rows = rows_from(0);
    compositor.held_rows = rows_from(0);
}

/*
        Runs are the same screen. A window composed again and again has its picture read for runs, which are then filled where
        they were copied (compositor_runs_learn): the screen must be the one the copy makes, byte for byte, whatever is done
        with the windows. The same steps (windows of flat bands, rectangles and noise that move, are raised, are repainted
        in part and are presented in part and whole, over a screen with corners, overlaps and windows cut by it) are run
        twice from the same start, once as the compositor is and once with every window's runs forgotten before each present,
        so that every compose is the copy; the screens must agree after every step, and some picture must have been composed
        from its runs.
*/
#define RUNS_W 640
#define RUNS_H 480

static fn runs_paint(int row, unsigned int seed, int x, int y, int width, int height, b32 whole)
{
        struct windows address_to table = compositor.table;
        unsigned int address_to pixels = windows_buffer(table, row, 0);

        for (int line = y; line < y + height; line++)
                for (int column = x; column < x + width; column++)
                {
                        unsigned int mix = (unsigned int)(line / 24) * 2654435761u + seed;
                        unsigned int colour = 0x00101010u + (mix >> 9 & 0x3f3f3f);

                        // Bands of one colour broken by a rectangle, a text-like patch of noise and a thin stripe.
                        if (column > 40 + (int)(mix >> 5 & 63) && column < 200 + (int)(mix >> 11 & 127) && (line / 24) % 3 == 1)
                                colour ^= 0x00ff00ffu;
                        if ((line / 24) % 4 == 2 && column > 60 && column < 60 + (int)(mix >> 3 & 255))
                                colour = (unsigned int)(column * 40503u + line * 9973u + seed) >> 3 & 0xffffffu;
                        if ((column + (int)seed) % 97 == 0)
                                colour = 0x00ffffffu;
                        pixels[line * RUNS_W + column] = colour;
                }
        table->buffer_index[row] = 0;
        table->damage_x[row] = x;
        table->damage_y[row] = y;
        table->damage_width[row] = whole ? 0 : width;
        table->damage_height[row] = height;
        table->frame[row]++;
        compositor_take(row);
}

// One run of the steps; with forgetting, every window's runs are dropped before each present. The screen's hash after each step is
// kept, and the answer is how many presents ended with a window whose runs were learned.
static positive runs_steps(unsigned int address_to out, b32 forgetting, p64 address_to hashes)
{
        unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
        unsigned long buffer = ((unsigned long)RUNS_W * RUNS_H * 4 + 4095) & ~4095ul;
        long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 3, 0x4022, (positive)-1, 0);
        unsigned int seed = 11;
        positive used = 0;

        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.width = RUNS_W;
        compositor.height = RUNS_H;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = out;
        compositor.scanout_pitch = RUNS_W * 4;
        if (mapped < 0 && mapped > -4096)
                return 0;
        compositor.table = (struct windows address_to)mapped;
        compositor.table->motion = 1;
        compositor.pixels = compositor.table->pixels = header;
        compositor.buffer = compositor.table->buffer = buffer;
        compositor.table->stride = compositor.table->output_width = RUNS_W;
        compositor.table->output_height = RUNS_H;
        for (int row = 1; row <= 3; row++)
        {
                compositor.table->x[row] = 20 + 70 * row;
                compositor.table->y[row] = 10 + 40 * row;
                compositor.table->width[row] = row == 1 ? 400 : row == 2 ? 330 : 390;
                compositor.table->height[row] = row == 1 ? 300 : row == 2 ? 290 : 270;
                runs_paint(row, (unsigned int)row * 977u, 0, 0, (int)compositor.table->width[row], (int)compositor.table->height[row], true);
        }
        compositor_damage(0, 0, RUNS_W, RUNS_H);
        compositor_present();

        for (int step = 0; step < 240; step++)
        {
                int row;
                unsigned int op;

                seed = seed * 1664525u + 1013904223u;
                row = 1 + (int)((seed >> 16) % 3);
                op = (seed >> 8) % 8;
                if (op == 0)
                {
                        compositor_damage_row(row);
                        compositor_place(row, (int)((seed >> 4) % 300), (int)((seed >> 12) % 200) - 20);
                        compositor_damage_row(row);
                }
                else if (op == 1)
                        compositor_raise(row);
                else if (op == 2)
                        runs_paint(row, seed, (int)((seed >> 4) % 200), (int)((seed >> 12) % 200), 20 + (int)((seed >> 20) % 100),
                                   10 + (int)((seed >> 24) % 60), false);
                else if (op == 3)
                        runs_paint(row, seed >> 3, 0, 0, (int)compositor.table->width[row], (int)compositor.table->height[row], true);
                else if (op == 4)
                        compositor_damage((int)((seed >> 4) % 400), (int)((seed >> 12) % 300), 30 + (int)((seed >> 20) % 200),
                                          20 + (int)((seed >> 24) % 150));
                else
                        compositor_damage(0, 0, RUNS_W, RUNS_H);
                if (forgetting)
                        for (int each = 1; each <= 3; each++)
                                compositor_runs_forget(each);
                compositor_present();
                for (int each = 1; each <= 3; each++)
                        used += compositor.runs_state[each] == 1;
                hashes[step] = 1469598103934665603ull;
                for (positive at = 0; at < (positive)RUNS_W * RUNS_H; at++)
                        hashes[step] = (hashes[step] ^ out[at]) * 1099511628211ull;
        }
        // Four presents of everything, so each window's picture is composed enough times to have its runs (unless forgetting).
        for (int settle = 0; settle < 4; settle++)
        {
                compositor_damage(0, 0, RUNS_W, RUNS_H);
                compositor_present();
        }
        return used;
}

// A slice of a scene composed apart, kept in the flat screen the check compares.
static unsigned int address_to runs_flat;

static void runs_flush(const struct compositor_target *slice, int *first, int *last)
{
        *first = slice->y1;
        *last = slice->y2 - 1;
        for (int y = slice->y1; y < slice->y2; y++)
                memory_copy(runs_flat + (positive)y * RUNS_W + slice->x1,
                            slice->pixels + (positive)(y - slice->oy) * slice->pitch + (positive)(slice->x1 - slice->ox),
                            (positive)(slice->x2 - slice->x1) * 4);
}

static void check_compositor_runs(void)
{
        static unsigned int screen[RUNS_H * RUNS_W], flat[4][RUNS_H * RUNS_W], slices[RUNS_W * COMPOSITOR_SLICE * COMPOSITOR_BANDS];
        static p64 made[240], copied[240];
        struct compositor_frame frames[COMPOSITOR_FRAMES_FOR(COMPOSITOR_ROWS_FIRST)];
        struct compositor_apart apart = {runs_flush, slices, RUNS_W};
        unsigned int rows[COMPOSITOR_ROWS_FIRST];
        unsigned int count = 0, with = 0;
        positive used = runs_steps(screen, false, made);
        int spare[2];

        runs_steps(screen, true, copied);
        check("runs: some window was composed from its runs", used > 20);
        check("runs: the screen composed from runs is the screen the copy makes, after every step",
              !memory_compare(made, copied, sizeof(made)));

        // The same windows composed straight and apart (in slices of a band, handed to a flush), from their runs and from their
        // copies: the screen is the same each way. The steps ran last with their runs, and left the windows' runs learned.
        runs_steps(screen, false, made);
        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor_visible(compositor.stack[at]))
                {
                        rows[count] = compositor.stack[at];
                        compositor_frame_of(compositor.stack[at], &frames[count++]);
                }
        compositor_damage(0, 0, RUNS_W, RUNS_H);
        compositor_runs_note(frames, rows, count);
        for (unsigned int at = 0; at < count; at++)
                with += frames[at].runs != 0;
        for (int pass = 0; pass < 4; pass++)
        {
                struct compositor_target target = {flat[pass], RUNS_W, RUNS_W, RUNS_H, 0, 0, RUNS_W, RUNS_H, 0, 0, 0};

                // Passes 0 and 1 compose with the runs, straight and apart; 2 and 3 with every frame's runs taken away.
                if (pass == 2)
                        for (unsigned int at = 0; at < count; at++)
                                frames[at].runs = 0;
                runs_flat = flat[pass];
                compositor_scene_banded(&target, frames, count, pass & 1 ? &apart : 0, spare);
        }
        check("runs: composed from the runs and from the copies, straight and apart, the screen is one",
              with > 0 && !memory_compare(flat[0], flat[2], sizeof(flat[0])) && !memory_compare(flat[1], flat[3], sizeof(flat[1])));
}

/*
        Two screens, one space. A is 320 by 192 at the left and B, taller at 256
        by 240, to its right; the scanout is the space, 576 by 240, and B's
        bottom rows under A's are no screen's. The pointer crosses where the
        two meet and stops beside A's bottom; a window dragged by its top
        across the edge is composed on both while it straddles and is B's when
        it is let go; a drop just past the shared edge arranges nothing, and
        one at B's free right edge fills B's right half; maximized it fills its
        own screen; the band is the primary's; the table lists both by name;
        and when B goes its windows and the pointer are carried onto A.
*/
#define SCREENS_W 576
#define SCREENS_H 240
#define SCREENS_A_W 320
#define SCREENS_A_H 192
#define SCREENS_B_W 256

static fn screens_paint(int row, int width, int height, unsigned int colour)
{
        struct windows address_to table = compositor.table;
        unsigned int address_to pixels = windows_buffer(table, row, 0);

        for (int line = 0; line < height; line++)
                for (int column = 0; column < width; column++)
                        pixels[line * SCREENS_A_W + column] = colour;
        table->width[row] = (unsigned int)width;
        table->height[row] = (unsigned int)height;
        table->buffer_index[row] = 0;
        table->damage_width[row] = 0;
        table->damage_height[row] = height;
        table->frame[row]++;
        compositor_take(row);
}

// The pointer taken to x, y as a hand would take it, with a button down or up after when one is named.
static fn screens_hand(int x, int y, int button)
{
        compositor.pointer_x = x;
        compositor.pointer_y = y;
        compositor_motion();
        if (button)
                compositor_button(COMPOSITOR_BTN_LEFT, button > 0);
}

static void check_compositor_screens(void)
{
    static unsigned int scanout[SCREENS_W * SCREENS_H];
    struct windows address_to table;
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)SCREENS_A_W * SCREENS_H * 4 + 4095) & ~4095ul;
    long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 3, 0x4022, (positive)-1, 0);
    struct compositor_frame band;
    int was[2][4], x, y, w, h;
    char names[2][WINDOWS_OUTPUT_NAME] = {"A-1", "B-1"};

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = SCREENS_W;
    compositor.height = SCREENS_H;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = scanout;
    compositor.scanout_pitch = SCREENS_W * 4;
    compositor.outputs = 2;
    compositor.screen[0][2] = SCREENS_A_W, compositor.screen[0][3] = SCREENS_A_H;
    compositor.screen[1][0] = SCREENS_A_W, compositor.screen[1][2] = SCREENS_B_W, compositor.screen[1][3] = SCREENS_H;
    memory_copy(compositor.output[0].name, names[0], sizeof(names[0]));
    memory_copy(compositor.output[1].name, names[1], sizeof(names[1]));
    compositor.stride = compositor.most_w = SCREENS_A_W;
    compositor.room_h = compositor.most_h = SCREENS_H;
    //  B dark at the start (arranged off): the largest a window may be is A, and the table made then has to have room
    //  for B, which an arrangement lights later and a window then fills.
    compositor.most_h = SCREENS_A_H;
    check("screens: a window's buffer has room for the tallest screen connected at the start, lit or dark",
          compositor_buffer_room() / 4 / SCREENS_A_W >= SCREENS_H);
    compositor.most_h = SCREENS_H;
    if (mapped < 0 && mapped > -4096)
    {
        check("screens: the table is mapped", false);
        return;
    }
    table = compositor.table = (struct windows address_to)mapped;
    table->motion = 1; // a window's first picture is drawn at its size: these checks compare pictures
    compositor.pixels = table->pixels = header;
    compositor.buffer = table->buffer = buffer;
    table->stride = SCREENS_A_W;
    compositor_table_outputs();
    check("screens: the table lists both, the primary first, by name and place",
          table->outputs == 2 && !string_compare((string_address)table->output_name[1], (string_address) "B-1") &&
              table->output_x[1] == SCREENS_A_W && table->output_w[1] == SCREENS_B_W && table->output_h[1] == SCREENS_H &&
              table->output_width == SCREENS_A_W && table->output_height == SCREENS_H);
    windows_output(table, 1, &x, &y, &w, &h);
    check("screens: windows_output reads a screen of the list, and the primary for one past it",
          x == SCREENS_A_W && y == 0 && w == SCREENS_B_W && h == SCREENS_H &&
              windows_output(table, 7, &x, &y, &w, &h) == 0 && x == 0 && w == SCREENS_A_W);
    check("screens: windows_output_at reads the screen a point is on, the second for a point on it, and the primary for one on none",
          windows_output_at(table, SCREENS_A_W + 5, 5, &x, &y, &w, &h) == 1 && x == SCREENS_A_W && w == SCREENS_B_W &&
              windows_output_at(table, SCREENS_A_W - 1, SCREENS_H - 1, &x, &y, &w, &h) == 0 && x == 0 && w == SCREENS_A_W &&
              windows_output_at(table, -5, 5, &x, &y, &w, &h) == 0 && windows_output_at(table, SCREENS_A_W + SCREENS_B_W, 5, &x, &y, &w, &h) == 0);

    table->x[1] = 30, table->y[1] = 30;
    screens_paint(1, 60, 40, 0x2060c0u);
    table->x[2] = 10, table->y[2] = 4;
    screens_paint(2, 40, 20, 0xc06020u);
    compositor_damage(0, 0, SCREENS_W, SCREENS_H);
    compositor_present();

    // The pointer.
    screens_hand(310, 50, 0);
    screens_hand(330, 50, 0);
    check("screens: the pointer crosses where the screens meet", compositor.pointer_x == 330 && compositor.pointer_y == 50);
    screens_hand(330, 230, 0);
    screens_hand(310, 230, 0);
    check("screens: and stops beside the shorter screen's bottom, on the screen it was on",
          compositor.pointer_x == SCREENS_A_W && compositor.pointer_y == 230);
    screens_hand(900, 900, 0);
    check("screens: and at the space's edge", compositor.pointer_x == SCREENS_W - 1 && compositor.pointer_y == SCREENS_H - 1);
    compositor.tablet_x = 300, compositor.tablet_y = 232, compositor.tablet_have = 3;
    compositor_report(0, 0);
    compositor_motion();
    check("screens: a tablet's point under the shorter screen goes to the nearest screen",
          compositor.pointer_x == SCREENS_A_W && compositor.pointer_y == 232);

    // A drag across the edge, by the window's top.
    screens_hand(40, 35, 1);
    screens_hand(280, 35, 0);
    compositor_present();
    check("screens: a window dragged across the edge is on both while it straddles",
          compositor.x[1] == 270 && (scanout[50 * SCREENS_W + 310] & 0xffffffu) == 0x2060c0u &&
              (scanout[50 * SCREENS_W + 325] & 0xffffffu) == 0x2060c0u);
    screens_hand(420, 35, 0);
    screens_hand(420, 35, -1);
    compositor_present();
    check("screens: and let go wholly on the second, it is the second screen's",
          compositor.x[1] == 410 && table->x[1] == 410 && compositor.arranged[1] == COMPOSITOR_FLOATING &&
              compositor_screen_of(1) == 1 && (scanout[50 * SCREENS_W + 430] & 0xffffffu) == 0x2060c0u &&
              (scanout[50 * SCREENS_W + 310] & 0xffffffu) != 0x2060c0u);

    // Where a drag ends.
    compositor.press = COMPOSITOR_NONE;
    screens_hand(20, 8, 1);
    screens_hand(200, 50, 0);
    screens_hand(318, 50, 0);
    screens_hand(318, 50, -1);
    check("screens: a drop just short of the shared edge arranges nothing", compositor.arranged[2] == COMPOSITOR_FLOATING);
    compositor.press = COMPOSITOR_NONE;
    screens_hand(compositor.x[2] + 5, compositor.y[2] + 4, 1);
    screens_hand(450, 50, 0);
    screens_hand(SCREENS_W - 2, 50, 0);
    screens_hand(SCREENS_W - 2, 50, -1);
    check("screens: one at the second screen's free right edge fills its right half",
          compositor.arranged[2] == COMPOSITOR_RIGHT && compositor.x[2] == SCREENS_A_W + SCREENS_B_W / 2 && compositor.y[2] == 0 &&
              table->want_width[2] == SCREENS_B_W / 2 && table->want_height[2] == SCREENS_H);
    compositor_arrange(1, COMPOSITOR_MAXIMIZED);
    check("screens: maximized, a window fills its own screen",
          compositor.x[1] == SCREENS_A_W && compositor.y[1] == 0 && table->want_width[1] == SCREENS_B_W &&
              table->want_height[1] == SCREENS_H);

#if MOONWATER_UI_TASKBAR
    // The band is the primary's.
    compositor_band_frame(&band);
    check("screens: the band is along the primary screen's bottom",
          band.x == 0 && band.width == SCREENS_A_W && band.y + band.height == SCREENS_A_H && compositor.listing == 2);
    taskbar_settings_set("bottom", "all", "");
    compositor.band_at = 1;
    compositor_band_frame(&band);
    compositor.band_at = 0;
    check("screens: with taskbar display all the second screen has a band of its own, listing the windows on it",
          band.x == SCREENS_A_W && band.width == SCREENS_B_W && band.y + band.height == SCREENS_H && compositor.listing == 2);
    compositor_band_frame(&band);
    check("screens: and the primary's lists none of them", band.x == 0 && compositor.listing == 0);
    //  Dragged from B to A nowhere near either band, a window's tab is drawn on A's band, which nothing of the drag
    //  reached: a band left as it was shows a tab it lost, and a press on it is another window's.
    {
        static unsigned int kept[SCREENS_A_W * 64];
        unsigned int changed = 0;
        int top, high;

        compositor_raise(2);
        compositor_focus(2);
        compositor_damage(0, 0, SCREENS_W, SCREENS_H);
        compositor_present();
        compositor_band_frame(&band);
        top = band.y, high = band.height < 64 ? band.height : 64;
        for (int line = 0; line < high; line++)
            memory_copy(kept + line * SCREENS_A_W, scanout + (top + line) * SCREENS_W, SCREENS_A_W * 4);
        compositor.press = COMPOSITOR_NONE;
        screens_hand(compositor.x[2] + 5, compositor.y[2] + 4, 1);
        screens_hand(200, 60, 0);
        screens_hand(60, 60, 0);
        screens_hand(60, 60, -1);
        compositor_present();
        for (int line = 0; line < high; line++)
            changed += memory_compare(kept + line * SCREENS_A_W, scanout + (top + line) * SCREENS_W, SCREENS_A_W * 4) != 0;
        compositor.band_at = 0;
        compositor_band_frame(&band);
        check("screens: a window dragged from the second screen to the primary, far from both bands, is drawn on the primary's band",
              compositor_screen_of(2) == 0 && compositor.listing == 1 && compositor.y[2] + (int)compositor.h[2] < top &&
                  changed > 0);
        compositor_arrange_on(2, COMPOSITOR_RIGHT, 1);
    }
    taskbar_settings_set("bottom", "primary", "");
#else
    (void)band;
#endif

    // B goes: its windows and the pointer are carried onto A.
    memory_copy(was, compositor.screen, sizeof(was));
    screens_hand(500, 200, 0);
    compositor.outputs = 1;
    compositor.width = SCREENS_A_W, compositor.height = SCREENS_A_H;
    compositor.most_h = SCREENS_A_H;
    compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])names, 2);
    check("screens: a window that filled the screen that went fills the one left",
          compositor.x[1] == 0 && compositor.y[1] == 0 && table->want_width[1] == SCREENS_A_W && table->want_height[1] == SCREENS_A_H);
    /* The window that fills the screen now covers the half of it that was on the one left, so that half floats back
       (compositor_tile_others), as a half a maximized window covers wholly does. */
    check("screens: one that filled half of it floats back where the window that fills the one left covers it, and the pointer is on it",
          compositor.arranged[2] == COMPOSITOR_FLOATING && compositor.pointer_x < SCREENS_A_W && compositor.pointer_y < SCREENS_A_H);
    compositor_float(1);
    check("screens: and the place it floated at before is carried too", compositor.x[1] + 60 <= SCREENS_A_W &&
                                                                            compositor.y[1] + 40 <= SCREENS_A_H);
    /* A drag shows the target under the pointer as a tint, and a drop on it takes the target; off every target nothing
       is shown and nothing is taken. Row 2 is on the screen A now, the only one. */
    compositor.press = COMPOSITOR_NONE;
    screens_hand(compositor.x[2] + 5, compositor.y[2] + 4, 1);
    screens_hand(100, 100, 0);
    check("snap: a window dragged into the middle of a screen shows no target",
          compositor.drag_moved && compositor.preview[2] == 0);
    screens_hand(1, 1, 0);
    check("snap: a corner shows its quarter, and the corner wins over the top and the left",
          compositor.preview_row == 2 && compositor.preview[0] == 0 && compositor.preview[1] == 0 &&
              compositor.preview[2] == SCREENS_A_W / 2 && compositor.preview[3] == SCREENS_A_H / 2);
    screens_hand(100, 120, 0);
    check("snap: dragged off the target before it is let go, the tint is gone at once", compositor.preview[2] == 0);
    screens_hand(1, 1, 0);
    screens_hand(1, 1, -1);
    check("snap: let go on the quarter, the window takes it, and the size asked is the quarter",
          compositor.arranged[2] == COMPOSITOR_TOP_LEFT && compositor.x[2] == 0 && compositor.y[2] == 0 &&
              table->want_width[2] == SCREENS_A_W / 2 && table->want_height[2] == SCREENS_A_H / 2 && compositor.preview[2] == 0);
    compositor.press = COMPOSITOR_NONE;
    screens_hand(compositor.x[2] + 5, compositor.y[2] + 4, 1);
    screens_hand(100, 150, 0);
    screens_hand(100, 150, -1);
    check("snap: let go in the middle, the window that was arranged floats where it is and takes no target",
          compositor.arranged[2] == COMPOSITOR_FLOATING && compositor.preview[2] == 0);
    compositor.press = COMPOSITOR_NONE;
    screens_hand(compositor.x[2] + 5, compositor.y[2] + 4, 1);
    screens_hand(150, 190, 0);
    check("snap: the bottom edge alone shows no target (only its corners are targets)", compositor.preview[2] == 0);
    screens_hand(150, 190, -1);
    check("snap: let go on the bottom edge, the window stays floating where the hand put it",
          compositor.arranged[2] == COMPOSITOR_FLOATING && compositor.preview[2] == 0);
    //  A window the card shows straight has its newest picture taken into its other buffer and held for the flip
    //  that lets go of the one shown (flip_row its row). The screens taken again before that flip show the
    //  compositor's scanout (compositor_scanout_own, then the crtcs set and compositor_flip_done), and with two
    //  screens nothing flips again: the held picture is said taken then, or its program waits on it for good.
    {
        unsigned int held;

        compositor.direct_row = compositor.flip_row = 1;
        compositor.flip_buffer = 0;
        compositor.front[1] = 0;
        table->buffer_index[1] = 1;
        table->frame[1]++;
        compositor_take(1);
        held = rows_has(&compositor.held_rows, 1);
        compositor_scanout_own();
        compositor_flip_done();
        check("screens: a picture held for a direct window's flip is said taken once the screens are taken again",
              held && table->shown[1] == table->frame[1] && !rows_any(&compositor.held_rows) && compositor.direct_row < 0);
    }
    compositor_call(syscall(munmap), (positive)mapped, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
    compositor_zero();

    // A hotplug is any uevent of the drm subsystem: its lines are each ended by a null, and only a whole line counts.
    {
        static const char card[] = "change@/devices/pci0000:00/0000:00:02.0/drm/card0\0ACTION=change\0SUBSYSTEM=drm\0HOTPLUG=1";
        static const char usb[] = "add@/devices/usb1\0ACTION=add\0SUBSYSTEM=usb\0DEVTYPE=usb_device";
        static const char near[] = "change@/x\0SUBSYSTEM=drmx\0NOTSUBSYSTEM=drm\0SUBSYSTEM=dr";
        check("screens: a drm uevent is a reason to look at the screens again, and no other is",
              compositor_uevent_drm(card, (long)sizeof(card)) && !compositor_uevent_drm(usb, (long)sizeof(usb)) &&
                  !compositor_uevent_drm(near, (long)sizeof(near)) && !compositor_uevent_drm(card, 60));
    }
}

/*
        A scale changed in a displays line is taken by the screens as they are driven: the same screens at the same modes
        are no new space, so compositor_space_take took nothing and the scale waited for the next start. A is 320 by 192
        and B 256 by 240, the first lit at 100 percent and the second at 100; their lines then ask 150 and 75. Each screen's
        scale, the desktop's (the primary's) and the table's follow at once, a window is drawn at its screen's scale and its
        program knocked when the picture is next made, a window that fills the room fills the room the band leaves at the
        new scale, every pixel is drawn again, and a line that asks what the screens have changes nothing.
*/
static void check_compositor_scales(void)
{
    static unsigned int scanout[SCREENS_W * SCREENS_H];
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)SCREENS_A_W * SCREENS_H * 4 + 4095) & ~4095ul;
    long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 3, 0x4022, (positive)-1, 0);
    struct windows address_to table;
    char names[2][WINDOWS_OUTPUT_NAME] = {"A-1", "B-1"};
    int first_whole = 0;

    compositor_zero();
    memory_zero(compositor_display, sizeof(compositor_display));
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = SCREENS_W;
    compositor.height = SCREENS_H;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = scanout;
    compositor.scanout_pitch = SCREENS_W * 4;
    compositor.outputs = 2;
    compositor.screen[0][2] = SCREENS_A_W, compositor.screen[0][3] = SCREENS_A_H;
    compositor.screen[1][0] = SCREENS_A_W, compositor.screen[1][2] = SCREENS_B_W, compositor.screen[1][3] = SCREENS_H;
    memory_copy(compositor.output[0].name, names[0], sizeof(names[0]));
    memory_copy(compositor.output[1].name, names[1], sizeof(names[1]));
    compositor.output[0].scale = compositor.output[1].scale = 100;
    for (unsigned int row = 0; row < COMPOSITOR_ROWS; row++)
        compositor.wake[row] = -1; // a knock goes nowhere: no program reads it here
    compositor.stride = compositor.most_w = SCREENS_A_W;
    compositor.room_h = compositor.most_h = SCREENS_H;
    if (mapped < 0 && mapped > -4096)
    {
        check("scales: the table is mapped", false);
        return;
    }
    table = compositor.table = (struct windows address_to)mapped;
    table->motion = 1;
    compositor.pixels = table->pixels = header;
    compositor.buffer = table->buffer = buffer;
    table->stride = SCREENS_A_W;
    table->scale = 100;
    table->pid[1] = table->pid[2] = 7;
    compositor_table_outputs();
    table->x[1] = 30, table->y[1] = 30;
    screens_paint(1, 60, 40, 0x2060c0u);
    table->x[2] = 10, table->y[2] = 4;
    screens_paint(2, 40, 20, 0xc06020u);
    compositor.x[2] = SCREENS_A_W + 20, compositor.y[2] = 20, table->x[2] = SCREENS_A_W + 20, table->y[2] = 20;
    compositor_damage(0, 0, SCREENS_W, SCREENS_H);
    compositor_present();
    compositor_knock();
    check("scales: before the lines ask anything every screen, the desktop and the table are at 100 percent and a window is too",
          compositor.scale == 100 && table->scale == 100 && compositor_row_scale(1) == 100 && compositor_row_scale(2) == 100 &&
              table->window_scale[1] == 100 && table->window_scale[2] == 100);
    check("scales: and nothing waits to be drawn or to be told", !compositor.damages && !rows_any(&compositor.woken));

    check("scales: no line asking another scale takes nothing, and damages nothing", compositor_scales_take() == 0 && !compositor.damages);

    memory_copy(compositor_display[0].name, "A-1", 4);
    compositor_display[0].scale = 150;
    memory_copy(compositor_display[1].name, "B-1", 4);
    compositor_display[1].scale = 75;
    compositor_displays = 2;
    check("scales: lines that ask another scale are taken as they are, with no new screens", compositor_scales_take() == 1);
    check("scales: each screen is at the scale its line asks", compositor.output[0].scale == 150 && compositor.output[1].scale == 75);
    check("scales: the desktop's scale is the primary's, and the table has it for the kit to read",
          compositor.scale == 150 && table->scale == 150);
    check("scales: a window is drawn at the scale of the screen it is on", compositor_row_scale(1) == 150 && compositor_row_scale(2) == 75);
    for (unsigned int at = 0; at < compositor.damages; at++)
        first_whole |= compositor.damage[at][0] == 0 && compositor.damage[at][1] == 0 && compositor.damage[at][2] == SCREENS_W &&
                       compositor.damage[at][3] == SCREENS_H;
    check("scales: every pixel is drawn again", first_whole);
    compositor_present();
    check("scales: the table says each window's scale once it is drawn, and each program is knocked for it",
          table->window_scale[1] == 150 && table->window_scale[2] == 75 && (rows_low(&compositor.woken) & 6u) == 6u);
    compositor_knock();
    check("scales: a line that asks what the screens have changes nothing and damages nothing",
          compositor_scales_take() == 0 && !compositor.damages && compositor.scale == 150 && table->scale == 150);
    compositor_display[0].scale = 100;
    compositor_scales_take();
    check("scales: and back to 100 percent it is again, the table with it", compositor.scale == 100 && table->scale == 100 &&
                                                                                compositor_row_scale(1) == 100);

#if MOONWATER_UI_TASKBAR
    {
        struct compositor_frame band;
        int room[4];
        unsigned int thin, was_h;

        compositor_band_frame(&band);
        thin = (unsigned int)band.height;
        compositor_room(compositor.screen[0], room);
        compositor_arrange_rect(1, COMPOSITOR_ROOM, room);
        was_h = compositor.h[1];
        compositor_display[0].scale = 200;
        compositor_scales_take();
        compositor_band_frame(&band);
        compositor_room(compositor.screen[0], room);
        check("scales: the band is drawn at the scale of its screen", (unsigned int)band.height > thin);
        check("scales: a window that fills the room fills the room the band leaves at the new scale",
              compositor.arranged[1] == COMPOSITOR_ROOM && (int)compositor.h[1] == room[3] && compositor.h[1] < was_h);
    }
#endif
    compositor_displays = 0;
    memory_zero(compositor_display, sizeof(compositor_display));
    compositor_call(syscall(munmap), (positive)mapped, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
    compositor_zero();
}

/*
        A window's corners and its line at every scale, on the screen it is on. The line is COMPOSITOR_LINE logical pixels
        cut to whole ones, a pixel at the least, and the corners are UI_RADIUS logical pixels at the scale: 4 device pixels
        at 50 percent, 6 at 75. At 50 and 75 percent the cut alone is no pixel, and a window with no line was drawn square
        (its radius was kept only where it had a line), so it lost its corners with its outline. A window is probed on
        what is on the screen: the outline's corner is the desktop's, the line is along the top, the desktop is beyond
        the line, and the contents' own corner pixel is cut by the arc. The scales are the six the settings offer, on one
        screen and with two at their own; the close and minimise buttons are drawn at each, and the assist's thumbnail and
        the grid's tile have the radius and line of their screen.
*/
#define CORNERS_SIDE GLASS_SIDE
#define CORNERS_PICTURE 0x404040u
static const int corner_scales[] = {50, 75, 100, 125, 150, 200};

// A floating row's frame at its scale, and the bits of what the screen shows of it that are as they should be.
static unsigned int corner_probe(int row, int scale, unsigned int *geometry)
{
    struct compositor_frame frame;
    const unsigned int *screen = compositor.scanout;
    unsigned int pitch = compositor.scanout_pitch / 4, ground = compositor_ground & 0xffffffu;
    unsigned int edge = compositor_line_unlit() & 0xffffffu, bits = 0; // the probed windows have not the keys
    int line = scale / 100 > 1 ? scale / 100 : 1, radius = UI_RADIUS * scale / 100, x, y, w, h;

    compositor_frame_of(row, &frame);
    x = frame.x, y = frame.y, w = frame.width, h = frame.height;
    *geometry = frame.scale == scale && frame.radius == radius && frame.line == line && compositor_radius(&frame) == radius;
    bits |= (screen[(y - line) * (int)pitch + (x - line)] & 0xffffffu) == ground;
    bits |= ((screen[(y - line) * (int)pitch + x + w / 2] & 0xffffffu) == edge) << 1;
    bits |= ((screen[(y - line - 1) * (int)pitch + x + w / 2] & 0xffffffu) == ground) << 2;
    bits |= ((screen[y * (int)pitch + x] & 0xffffffu) != CORNERS_PICTURE) << 3;
    bits |= ((screen[(y + h / 2) * (int)pitch + x] & 0xffffffu) == CORNERS_PICTURE) << 4;
    return bits;
}

// A compositor of CORNERS_SIDE square, a window of width by 60 on the first screen at 20, 40 and, with two screens (each half
// the width), one 80 by 60 on the second at 150, 40; the first screen at scale a and the second at b; painted, and drawn.
static unsigned int address_to corner_scene(unsigned int *scanout, int outputs, int a, int b, int width)
{
    unsigned int address_to mapped;

    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.told_row = COMPOSITOR_NONE;
    compositor.shared_row = compositor.mark_row = compositor.preview_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = compositor.height = CORNERS_SIDE;
    compositor.scale = a;
    compositor.card = -1;
    compositor.scanout = scanout;
    compositor.scanout_pitch = CORNERS_SIDE * 4;
    compositor.outputs = (unsigned int)outputs;
    if (outputs == 2)
    {
        compositor.screen[0][2] = CORNERS_SIDE / 2, compositor.screen[0][3] = CORNERS_SIDE;
        compositor.screen[1][0] = CORNERS_SIDE / 2, compositor.screen[1][2] = CORNERS_SIDE / 2, compositor.screen[1][3] = CORNERS_SIDE;
        compositor.output[0].scale = a, compositor.output[1].scale = b;
    }
    else if (outputs == 1)
    {
        compositor.screen[0][2] = compositor.screen[0][3] = CORNERS_SIDE;
        compositor.output[0].scale = a;
    }
    mapped = glass_table_map();
    if (!mapped)
        return null;
    compositor.table->scale = (unsigned int)a;
    compositor.table->x[1] = 20, compositor.table->y[1] = 40, compositor.table->width[1] = (unsigned int)width, compositor.table->height[1] = 60;
    compositor.table->x[2] = 150, compositor.table->y[2] = 40, compositor.table->width[2] = 80, compositor.table->height[2] = 60;
    glass_paint(1, 0, 0, width, 60, CORNERS_PICTURE, true);
    if (outputs == 2)
        glass_paint(2, 0, 0, 80, 60, CORNERS_PICTURE, true);
    for (unsigned int at = 0; at < CORNERS_SIDE * CORNERS_SIDE; at++)
        scanout[at] = 0x123456u;
    compositor_damage(0, 0, CORNERS_SIDE, CORNERS_SIDE);
    compositor_present();
    return mapped;
}

static void corner_scene_end(unsigned int address_to mapped)
{
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)GLASS_SIDE * GLASS_SIDE * 4 + 4095) & ~4095ul;

    compositor_call(syscall(munmap), (positive)mapped, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
    compositor_zero();
}

static void check_compositor_corner_scales(void)
{
    static unsigned int scanout[CORNERS_SIDE * CORNERS_SIDE];
    static const int pairs[][2] = {{50, 100}, {100, 50}, {75, 200}, {200, 75}, {100, 200}, {150, 100}, {125, 50}};
    unsigned int geometry = ~0u, shown = ~0u, both = ~0u, buttons = ~0u, thumbs = ~0u;
    unsigned int address_to mapped;

    // One screen, each scale: the frame and the screen.
    for (unsigned int at = 0; at < array_count(corner_scales); at++)
    {
        unsigned int fine = 0, bits;

        mapped = corner_scene(scanout, 0, corner_scales[at], corner_scales[at], 80);
        if (!mapped)
        {
            check("corner scales: the table is mapped", false);
            return;
        }
        bits = corner_probe(1, corner_scales[at], &fine);
        geometry &= fine ? ~0u : ~(1u << at);
        shown &= bits == 31u ? ~0u : ~(1u << at);
        if (!fine || bits != 31u)
            string_format(log, "  corner scales: %p percent: geometry %p, screen bits %p\n", (positive)corner_scales[at],
                          (positive)fine, (positive)bits);
        corner_scene_end(mapped);
    }
    check("corner scales: a window's corners are UI_RADIUS to its scale (4 device pixels at 50 percent, 6 at 75) and its line a pixel "
          "at the least, at 50, 75, 100, 125, 150 and 200 percent",
          geometry == ~0u);
    check("corner scales: presented, the outline's corner is the desktop's, the line runs along the top with the desktop beyond it, "
          "and the contents' corner is cut by the arc, at every scale",
          shown == ~0u);

    // Two screens each at its own scale: the window on each has the radius and line of its own screen's.
    for (unsigned int at = 0; at < array_count(pairs); at++)
    {
        unsigned int fine_a = 0, fine_b = 0, bits_a, bits_b;

        mapped = corner_scene(scanout, 2, pairs[at][0], pairs[at][1], 80);
        if (!mapped)
        {
            check("corner scales: the table is mapped for two screens", false);
            return;
        }
        bits_a = corner_probe(1, pairs[at][0], &fine_a);
        bits_b = corner_probe(2, pairs[at][1], &fine_b);
        both &= fine_a && fine_b && bits_a == 31u && bits_b == 31u ? ~0u : ~(1u << at);
        if (!fine_a || !fine_b || bits_a != 31u || bits_b != 31u)
            string_format(log, "  corner scales: %p and %p percent: geometry %p %p, screen bits %p %p\n", (positive)pairs[at][0],
                          (positive)pairs[at][1], (positive)fine_a, (positive)fine_b, (positive)bits_a, (positive)bits_b);
        corner_scene_end(mapped);
    }
    check("corner scales: on two screens at 50 and 100, 100 and 50, 75 and 200, 200 and 75, 100 and 200, 150 and 100, 125 and 50 "
          "percent a window has the radius and line of its own screen's scale, and the corner and the line drawn",
          both == ~0u);

    // The close and the minimise buttons: drawn, in their boxes, at every scale.
    for (unsigned int at = 0; at < array_count(corner_scales); at++)
    {
        struct compositor_frame frame;
        int bx = 0, by = 0, side = 0, mx = 0, my = 0, mside = 0, seen = 0, dash = 0;

        mapped = corner_scene(scanout, 0, corner_scales[at], corner_scales[at], 200);
        if (!mapped)
            return;
        compositor_frame_of(1, &frame);
        if (compositor_close_box(&frame, &bx, &by, &side) && compositor_minimize_box(&frame, &mx, &my, &mside))
        {
            for (int y = by; y < by + side; y++)
                for (int x = bx; x < bx + side; x++)
                    seen += (scanout[y * CORNERS_SIDE + x] & 0xffffffu) != CORNERS_PICTURE;
            for (int x = mx; x < mx + mside; x++)
                dash += (scanout[(my + mside / 2) * CORNERS_SIDE + x] & 0xffffffu) != CORNERS_PICTURE;
        }
        buttons &= side > 0 && mside > 0 && seen > 0 && dash > 0 ? ~0u : ~(1u << at);
        corner_scene_end(mapped);
    }
    check("corner scales: the close button and the minimise dash are drawn in their boxes at every scale", buttons == ~0u);

    // The assist's thumbnail and the grid's tile: the radius and the line of their screen's scale, a pixel at the least.
    for (unsigned int at = 0; at < array_count(corner_scales); at++)
    {
        static struct compositor_frame frames[COMPOSITOR_FRAMES_FOR(COMPOSITOR_ROWS_FIRST)];
        int line = corner_scales[at] / 100 > 1 ? corner_scales[at] / 100 : 1, radius = UI_RADIUS * corner_scales[at] / 100;
        unsigned int added, ok;

        mapped = corner_scene(scanout, 1, corner_scales[at], corner_scales[at], 80);
        if (!mapped)
            return;
        compositor.assist_on = compositor.grid_on = 1;
        compositor.assist_count = 1, compositor.assist_rows[0] = 1, compositor.grid_screen = 0, compositor.assist_screen = 0;
        compositor.assist_rect[0][0] = 40, compositor.assist_rect[0][1] = 100, compositor.assist_rect[0][2] = 60;
        compositor.assist_rect[0][3] = 40;
        compositor.overlay_goal = 100;
        memory_zero(frames, sizeof(frames));
        added = compositor_overlays(frames, 0);
        ok = added > 3 && frames[0].radius == radius && frames[0].line == line && frames[3].radius == radius && frames[3].line == line;
        thumbs &= ok ? ~0u : ~(1u << at);
        corner_scene_end(mapped);
    }
    check("corner scales: an assist thumbnail and a grid tile have the radius and line of their screen's scale at every scale",
          thumbs == ~0u);
}

/*
        A screen Canvas turns in software, a frame at a time. The turn is every pixel of the rectangle where the turn puts it,
        in tiles of 16 pixels square whatever the rectangle's edges (a rectangle of the damage is no multiple of anything),
        and nothing outside the rectangle's panel rectangle is stored; each rectangle of a frame's damage is turned alone, so
        two far apart (the pointer's old place and its new one) are not the box that holds them both, which was most of the
        screen; and the picture is turned from the machine's memory, the mirror of what the card holds, and never read from
        the card's own (about ten megabytes a second on a real card: a frame of 2560 by 1080 turned from it took a second).
*/
#define TURN_SIDE 96
#define TURN_W 70
#define TURN_H 45

static unsigned int turn_scene[TURN_SIDE * TURN_SIDE], turn_mirror[TURN_SIDE * TURN_SIDE], turn_panel[TURN_SIDE * TURN_SIDE];

// The panel's pixel a picture pixel turns into, by the one rule every turn has (windows_rotate_at).
static unsigned int turn_expected(unsigned int degrees, unsigned int lw, unsigned int lh, unsigned int x, unsigned int y)
{
    unsigned int px, py;

    windows_rotate_at(lw, lh, degrees, x, y, &px, &py);
    return py * TURN_SIDE + px;
}

static void check_compositor_turn(void)
{
    static const unsigned int degrees_listed[3] = {90, 180, 270};
    static const unsigned int rects[5][4] = {{0, 0, TURN_W, TURN_H}, {3, 5, 34, 47 - 4}, {16, 16, 32, 32}, {17, 1, 69, 44}, {0, 44, 70, 45}};
    unsigned int wrong = 0, outside = 0, stored = 0;

    // Every rectangle, every turn: each pixel is where the rule puts it, and nothing outside the rectangle is stored.
    for (unsigned int turn = 0; turn < 3; turn++)
        for (unsigned int which = 0; which < 5; which++)
        {
            unsigned int degrees = degrees_listed[turn], x0 = rects[which][0], y0 = rects[which][1], x1 = rects[which][2],
                         y1 = rects[which][3];

            for (unsigned int at = 0; at < TURN_SIDE * TURN_SIDE; at++)
                turn_scene[at] = 0x01000000u + at, turn_panel[at] = 0xdead0000u;
            windows_rotate_u32(turn_panel, TURN_SIDE, turn_scene, TURN_SIDE, TURN_W, TURN_H, degrees, x0, y0, x1, y1);
            for (unsigned int y = y0; y < y1; y++)
                for (unsigned int x = x0; x < x1; x++)
                    wrong += turn_panel[turn_expected(degrees, TURN_W, TURN_H, x, y)] != turn_scene[y * TURN_SIDE + x];
            for (unsigned int at = 0; at < TURN_SIDE * TURN_SIDE; at++)
                stored += turn_panel[at] != 0xdead0000u;
            outside += stored != (x1 - x0) * (y1 - y0);
            stored = 0;
        }
    check("turn: every pixel of a rectangle, whatever its edges are, is where the turn puts it, in every turn", wrong == 0);
    check("turn: and no pixel but those of the rectangle is stored", outside == 0);

    // A frame: one screen turned by Canvas, its scene a pattern, its panel a sentinel.
    for (unsigned int turn = 0; turn < 3; turn++)
    {
        unsigned int degrees = degrees_listed[turn], between = 0, near_a = 0, near_b = 0, from_card = 0;
        unsigned short clips[2][4] = {{2, 2, 14, 10}, {TURN_W - 12, TURN_H - 9, TURN_W - 2, TURN_H - 2}};

        compositor_zero();
        memory_zero(compositor_turns, sizeof(compositor_turns));
        compositor.card = -1;
        compositor.outputs = 1;
        compositor.screen[0][2] = TURN_W, compositor.screen[0][3] = TURN_H;
        compositor.output[0].turned = 2, compositor.output[0].degrees = degrees;
        compositor.width = TURN_W, compositor.height = TURN_H;
        compositor_turns[0].pixels = turn_panel;
        compositor_turns[0].pitch = TURN_SIDE * 4;
        compositor_turns[0].width = TURN_SIDE, compositor_turns[0].height = TURN_SIDE;
        for (unsigned int at = 0; at < TURN_SIDE * TURN_SIDE; at++)
            turn_scene[at] = 0x02000000u + at, turn_mirror[at] = 0x03000000u + at, turn_panel[at] = 0xdead0000u;

        // The card's own memory (the scanout) holds what a read of it must never be taken for.
        compositor.scanout = turn_scene;
        compositor.scanout_pitch = TURN_SIDE * 4;
        compositor.changed = 1;
        compositor.mirror = turn_mirror;
        compositor.mirror_pitch = TURN_SIDE;
        compositor_turn_frame((const unsigned short (*)[4])clips, 2, 0);
        for (unsigned int y = 0; y < TURN_H; y++)
            for (unsigned int x = 0; x < TURN_W; x++)
            {
                unsigned int got = turn_panel[turn_expected(degrees, TURN_W, TURN_H, x, y)];
                int in_a = x >= clips[0][0] && x < clips[0][2] && y >= clips[0][1] && y < clips[0][3];
                int in_b = x >= clips[1][0] && x < clips[1][2] && y >= clips[1][1] && y < clips[1][3];

                if (in_a || in_b)
                {
                    near_a += in_a && got != turn_mirror[y * TURN_SIDE + x];
                    near_b += in_b && got != turn_mirror[y * TURN_SIDE + x];
                    from_card += got == turn_scene[y * TURN_SIDE + x];
                }
                else
                    between += got != 0xdead0000u;
            }
        check("turn: the damage's two rectangles far apart are turned, each from the mirror", near_a + near_b == 0);
        check("turn: and nothing between them is turned, as the box that holds them both would", between == 0);
        check("turn: and the card's own memory is not read for it", from_card == 0);
    }

    // A frame turned with no mirror, the scanout the machine's memory, is turned from the scanout.
    {
        unsigned short clip[1][4] = {{0, 0, TURN_W, TURN_H}};
        unsigned int wrong_scanout = 0;

        compositor_zero();
        compositor.card = -1;
        compositor.outputs = 1;
        compositor.screen[0][2] = TURN_W, compositor.screen[0][3] = TURN_H;
        compositor.output[0].turned = 2, compositor.output[0].degrees = 90;
        compositor_turns[0].pixels = turn_panel;
        compositor_turns[0].pitch = TURN_SIDE * 4;
        for (unsigned int at = 0; at < TURN_SIDE * TURN_SIDE; at++)
            turn_scene[at] = 0x02000000u + at, turn_panel[at] = 0xdead0000u;
        compositor.scanout = turn_scene;
        compositor.scanout_pitch = TURN_SIDE * 4;
        compositor_turn_frame((const unsigned short (*)[4])clip, 1, 0);
        for (unsigned int y = 0; y < TURN_H; y++)
            for (unsigned int x = 0; x < TURN_W; x++)
                wrong_scanout += turn_panel[turn_expected(90, TURN_W, TURN_H, x, y)] != turn_scene[y * TURN_SIDE + x];
        check("turn: with no mirror (the scanout is the machine's memory) a frame is turned from the scanout", wrong_scanout == 0);
    }

    // A card whose driver no list names keeps the scene in the machine's memory while a screen is turned, and not otherwise.
    {
        unsigned int *kept;

        compositor_zero();
        compositor.card = 3; // never used: the decision asks the driver's name
        memory_copy(compositor.driver, "i915", 5);
        compositor.width = 128, compositor.height = 64;
        compositor.scanout = turn_scene, compositor.scanout_pitch = 128 * 4;
        compositor.outputs = 1;
        check("turn: a card no list names, nothing turned, composes straight to its memory", compositor_changed_ready() == 0 && !compositor.mirror);
        compositor.output[0].turned = 2;
        check("turn: and with a screen turned in software it keeps the scene and a mirror of what the card holds",
              compositor_changed_ready() == 1 && compositor.composing && compositor.mirror && compositor.mirror != compositor.scanout &&
                  compositor.mirror_stale);
        kept = compositor.mirror;
        check("turn: and asks nothing again while that is so", compositor_changed_ready() == 1 && compositor.mirror == kept);
        compositor.output[0].turned = 0;
        check("turn: and gives it back when none is, composing straight again",
              compositor_changed_ready() == 0 && !compositor.mirror && !compositor.composing);
        compositor.output[0].turned = 1;
        check("turn: a screen the card turns reads nothing and keeps nothing", compositor_changed_ready() == 0 && !compositor.mirror);
        memory_copy(compositor.driver, "amdgpu", 7);
        compositor.output[0].turned = 0;
        compositor.changed = 0;
        check("turn: a card of the apart list keeps its mirror as it did", compositor_changed_ready() == 1 && compositor.mirror != compositor.scanout);
        compositor_apart_drop();
    }

    // The space taken again at another shape: what waited to be drawn in the space before is gone, the whole new one is to be drawn.
    {
        compositor_zero();
        compositor.width = 256, compositor.height = 64;
        compositor_damage(0, 40, 256, 24);
        compositor_damage(10, 10, 20, 20);
        compositor.width = 64, compositor.height = 256;
        compositor_damage_space();
        check("turn: the space taken at another shape leaves only the whole of it to be drawn",
              compositor.damages == 1 && compositor.damage[0][0] == 0 && compositor.damage[0][1] == 0 &&
                  compositor.damage[0][2] == 64 && compositor.damage[0][3] == 256);
    }

    // And what is composed then is the new space and nothing past it: a band's rectangle along the bottom of a tall space
    // (rows 232 to 256 of 64 by 256) waiting when the space becomes 256 by 64 is rows past the scanout's end, which the
    // present stored into whatever the memory after it was (red with the rectangle kept: the sentinels after the space).
    {
        static unsigned int arena[256 * 256];
        unsigned int smashed = 0, ground = 0;

        for (unsigned int at = 0; at < 256 * 256; at++)
            arena[at] = at < 256 * 64 ? 0u : 0xfeedfeedu;
        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = COMPOSITOR_NONE;
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
        compositor.card = -1;
        compositor.scale = 100;
        compositor.width = 64, compositor.height = 256;
        compositor_damage(0, 232, 64, 24);
        compositor.width = 256, compositor.height = 64;
        compositor.scanout = arena;
        compositor.scanout_pitch = 256 * 4;
        compositor_damage_space();
        compositor_present();
        for (unsigned int at = 256 * 64; at < 256 * 256; at++)
            smashed += arena[at] != 0xfeedfeedu;
        for (unsigned int at = 0; at < 256 * 64; at++)
            ground += arena[at] != 0;
        check("turn: the space taken at another shape composes the new space and stores nothing past its end",
              smashed == 0 && ground * 10 >= 256 * 64 * 9);
    }
    compositor_zero();
    memory_zero(compositor_turns, sizeof(compositor_turns));
}

/*
        The rate a screen's line asks, taken as the screen's mode: a rate is the first mode the screen lists at the size it
        has and that rate, and the preferred rate (what the settings' first, recommended rate writes) is the first it lists
        at that size. The mode the screen has is the one it was switched to, so keeping it for a preferred rate was no
        switch back: the first change worked and the recommended rate never took after another. A screen with no line keeps
        its mode.
*/
static void check_compositor_refresh(void)
{
    static const unsigned int listed[4][3] = {{1920, 1080, 60}, {1920, 1080, 50}, {1280, 720, 60}, {1280, 720, 50}};
    struct compositor_mode modes[4], mode;

    memory_zero(modes, sizeof(modes));
    for (unsigned int at = 0; at < 4; at++)
    {
        modes[at].hdisplay = (unsigned short)listed[at][0], modes[at].vdisplay = (unsigned short)listed[at][1];
        modes[at].vrefresh = listed[at][2], modes[at].clock = 100000 + at;
    }
    check("refresh: the preferred rate is the first mode the screen lists at the size, and a size it does not list has none",
          windows_mode_at(listed, 4, 1920, 1080, 0) == 0 && windows_mode_at(listed, 4, 1280, 720, 0) == 2 &&
              windows_mode_at(listed, 4, 1024, 768, 0) == 4 && windows_mode_at(listed, 4, 1920, 1080, 50) == 1);

    memory_zero(compositor_display, sizeof(compositor_display));
    memory_copy(compositor_display[0].name, "A-1", 4);
    compositor_displays = 1;

    compositor_display[0].refresh = 50;
    mode = modes[0];
    compositor_refresh_pick("A-1", 4, modes, &mode);
    check("refresh: a rate a line asks is the mode at it", mode.vrefresh == 50 && mode.clock == modes[1].clock);

    // The screen is at the 50 Hz mode now, and the line is the recommended rate again.
    compositor_display[0].refresh = 0;
    mode = modes[1];
    compositor_refresh_pick("A-1", 4, modes, &mode);
    check("refresh: the preferred rate a screen at another rate is asked goes back to the first mode it lists at the size",
          mode.vrefresh == 60 && mode.clock == modes[0].clock);
    mode = modes[3];
    compositor_refresh_pick("A-1", 4, modes, &mode);
    check("refresh: at another size it is that size's first", mode.vrefresh == 60 && mode.hdisplay == 1280 && mode.clock == modes[2].clock);
    mode = modes[0];
    compositor_refresh_pick("A-1", 4, modes, &mode);
    check("refresh: a screen already at its first mode keeps it", mode.clock == modes[0].clock);

    compositor_display[0].refresh = 60;
    mode = modes[1];
    compositor_refresh_pick("A-1", 4, modes, &mode);
    check("refresh: and a rate asked by number after another is as it was", mode.vrefresh == 60 && mode.clock == modes[0].clock);

    // The settings write the recommended rate as no line at all once nothing else of the screen is asked, so a screen
    // with none is asked the preferred rate too.
    compositor_display[0].refresh = 50;
    mode = modes[1];
    compositor_refresh_pick("B-1", 4, modes, &mode);
    check("refresh: a screen with no line is at the fastest mode it lists at its size, as a line of the preferred rate says",
          mode.vrefresh == 60 && mode.clock == modes[0].clock);
    mode = modes[0];
    compositor_refresh_pick("B-1", 4, modes, &mode);
    check("refresh: and a screen with none that is there already stays", mode.clock == modes[0].clock);
    // A mode the kernel's command line forced at a size is the screen's own there, whatever the order the card lists.
    modes[1].type = COMPOSITOR_MODE_USERDEF;
    mode = modes[1];
    compositor_refresh_pick("B-1", 4, modes, &mode);
    check("refresh: and a mode forced on the command line at its size is kept, line or none",
          mode.clock == modes[1].clock && mode.type == COMPOSITOR_MODE_USERDEF);
    compositor_display[0].refresh = 0;
    mode = modes[0];
    compositor_refresh_pick("A-1", 4, modes, &mode);
    check("refresh: also for a line of the preferred rate", mode.clock == modes[1].clock);
    modes[1].type = 0;

    // The rates a screen lists are those at the size of its panel, whether it is laid out turned or not.
    {
        static struct compositor_connected found;

        memory_zero(&found, sizeof(found));
        found.mode[0] = modes[1];
        found.size[0][0] = 1920, found.size[0][1] = 1080;
        compositor_rates_take(&found, 0, modes, 4);
        check("refresh: a screen lists the rates it has at its size, the fastest first",
              found.hz_count[0] == 2 && found.hz[0][0] == 60 && found.hz[0][1] == 50);
        memory_zero(&found, sizeof(found));
        found.mode[0] = modes[1];
        found.size[0][0] = 1080, found.size[0][1] = 1920; // the same screen turned a quarter: laid out the other way round
        compositor_rates_take(&found, 0, modes, 4);
        check("refresh: and a screen turned a quarter lists the same, not the none that a size no mode has gives",
              found.hz_count[0] == 2 && found.hz[0][0] == 60 && found.hz[0][1] == 50);
    }

    // A 144 Hz monitor whose preferred mode, listed first, is its 60 Hz one: the preferred rate is its fastest at the size, 144, and its
    // rates are listed the fastest first; an interlaced mode at the size is no rate of it.
    {
        static const unsigned int fast[4] = {60, 144, 120, 200};
        struct compositor_mode listed_fast[4];
        static struct compositor_connected found;

        memory_zero(listed_fast, sizeof(listed_fast));
        for (unsigned int at = 0; at < 4; at++)
            listed_fast[at].hdisplay = 2560, listed_fast[at].vdisplay = 1440, listed_fast[at].vrefresh = fast[at], listed_fast[at].clock = 200000 + at;
        listed_fast[0].type = 8;                        // DRM_MODE_TYPE_PREFERRED: the kernel lists it first
        listed_fast[3].flags = COMPOSITOR_MODE_INTERLACED; // its refresh is its fields'
        compositor_display[0].refresh = 0;
        mode = listed_fast[1];
        compositor_refresh_pick("B-1", 4, listed_fast, &mode);
        check("refresh: a 144 Hz monitor that prefers its 60 Hz mode and has no line is at 144", mode.vrefresh == 144 && mode.clock == listed_fast[1].clock);
        mode = listed_fast[0];
        compositor_refresh_pick("A-1", 4, listed_fast, &mode);
        check("refresh: and a line of the preferred rate takes it from 60 to 144", mode.vrefresh == 144 && mode.clock == listed_fast[1].clock);
        compositor_display[0].refresh = 60;
        compositor_refresh_pick("A-1", 4, listed_fast, &mode);
        check("refresh: while a line asking 60 by number is at 60", mode.vrefresh == 60 && mode.clock == listed_fast[0].clock);
        memory_zero(&found, sizeof(found));
        found.mode[0] = listed_fast[1];
        compositor_rates_take(&found, 0, listed_fast, 4);
        check("refresh: its rates are 144, 120 and 60, the fastest first, and the interlaced one is none",
              found.hz_count[0] == 3 && found.hz[0][0] == 144 && found.hz[0][1] == 120 && found.hz[0][2] == 60);
    }

    compositor_displays = 0;
    memory_zero(compositor_display, sizeof(compositor_display));
}

/*
        A tint (frame->preview) of the scene is a tint on its target and on nothing else, and what is after its frame in the
        list is over it (the grid's tints under the assist's thumbnails): over the desktop, the target is the kit's selection
        at its own alpha, a frame of the window that carries it is as it is over the tint, and a window above it is as it is
        too. Everything else is the desktop, untouched. (The snap preview of a window is not one of these: compositor_present
        takes it off the window into a frame of its own after the windows and the tabs, compositor_preview_lift.)
*/
static void check_compositor_preview(void)
{
    static unsigned int pixels[64 * 40], want[64 * 40], window[16 * 8], over[8 * 8];
    struct compositor_target t = {pixels, 64, 64, 40, 0, 0, 64, 40, 0, 0, 0};
    struct compositor_frame frames[2];
    const unsigned int ink = 0x40ffffffu, ground = compositor_ground;
    unsigned int wrong = 0;

    for (unsigned int at = 0; at < 64 * 40; at++)
        pixels[at] = ground;
    for (unsigned int at = 0; at < 16 * 8; at++)
        window[at] = 0xc06020u;
    for (unsigned int at = 0; at < 8 * 8; at++)
        over[at] = 0x204080u;
    frames[0] = (struct compositor_frame){.x = 10, .y = 4, .width = 16, .height = 8, .scale = 100, .pixels = window, .pitch = 16,
                                          .preview = {0, 0, 32, 20}, .preview_ink = ink};
    frames[1] = (struct compositor_frame){.x = 0, .y = 0, .width = 8, .height = 8, .scale = 100, .pixels = over, .pitch = 8};
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 64; x++)
        {
            unsigned int e = ground;

            if (x < 32 && y < 20)
                e = compositor_mix(e, ink & 0xffffffu, ink >> 24);
            if (x >= 10 && x < 26 && y >= 4 && y < 12)
                e = 0xc06020u;
            if (x < 8 && y < 8)
                e = 0x204080u;
            want[y * 64 + x] = e;
        }
    compositor_scene(&t, frames, 2);
    for (unsigned int at = 0; at < 64 * 40; at++)
        wrong += pixels[at] != want[at];
    check("preview: the tint is on its target and nowhere else, the window and the one over it are as they are",
          wrong == 0 && compositor_mix(ground, ink & 0xffffffu, ink >> 24) != ground);
}

/*
        The snap's tint reads on the wallpaper it is over, on both themes: its edge, the tint composed over the wallpaper,
        against the wallpaper itself is COMPOSITOR_SNAP_READ or more, on the dark wallpaper the default desktop has and on
        two light ones, with the dark theme and the light one (the standard white accent on each).
*/
static unsigned int address_to layout_table_map(int side);

static void check_compositor_snap_tint(void)
{
    static const unsigned int grounds[3] = {0x1e1e1eu, 0xf7f7f8u, 0xe4e4e7u};
    unsigned int ground_was = compositor_ground, least = ~0u;
    int every = 1;

    if (!layout_table_map(256))
    {
        check("snap tint: the table is mapped", false);
        return;
    }
    for (int theme = 0; theme < 2; theme++)
    {
        compositor.table->themed = 1, compositor.table->theme = theme ? UI_THEME_DARK : UI_THEME_LIGHT;
        compositor.table->accent = UI_ACCENT_STANDARD;
        for (int at = 0; at < 3; at++)
        {
            unsigned int ground = grounds[at], ink, edge, read;

            compositor_ground = ground;
            ink = compositor_snap_ink();
            edge = compositor_mix(ground, ink & 0xffffffu, ink >> 24) | 0xff000000u;
            read = ui_contrast(edge, ground | 0xff000000u);
            string_format(log, "  snap tint: %s theme, wallpaper %p: tint %p at %p percent, edge %p hundredths\n",
                          theme ? "dark" : "light", (positive)ground, (positive)(ink & 0xffffffu),
                          (positive)((ink >> 24) * 100 / 255), (positive)read);
            if (read < least)
                least = read;
            every = every && read >= COMPOSITOR_SNAP_READ;
        }
    }
    compositor_ground = ground_was;
    check("snap tint: its edge reads COMPOSITOR_SNAP_READ or more on both themes, on the dark wallpaper and on two light ones",
          every && least >= COMPOSITOR_SNAP_READ);
}

/*
        A snap preview held over its target costs what the drag costs. Over the window dragged the tint is made only where
        the window does not cover it: its contents are copied over the tint, so those pixels are not tinted at all, and the
        rest are the pixels the whole tint and redraw made (checked against that reference, over a rounded window with a
        line, a square one and one with neither, inks at a cover of 0, 255 and between, and a window above). The pointer
        stopped is idle: once presented nothing is damaged, and a target held is damaged once, and only it.
*/
#define HELD_W 96
#define HELD_H 64
#define HELD_OUT_W 640
#define HELD_OUT_H 400
static void check_compositor_preview_held(void)
{
    static unsigned int pixels[HELD_W * HELD_H], want[HELD_W * HELD_H], wall[HELD_W * HELD_H];
    static unsigned int window[40 * 30], above[12 * 10];
    struct compositor_target t = {pixels, HELD_W, HELD_W, HELD_H, 0, 0, HELD_W, HELD_H, 0, 0, 0};
    struct compositor_target reference = {want, HELD_W, HELD_W, HELD_H, 0, 0, HELD_W, HELD_H, 0, 0, 0};
    const unsigned int inks[4] = {0x40ffffffu, 0x00102030u, 0xff102030u, 0x9f0a0b0cu};
    struct compositor_frame frames[3], plain[3];
    int radii[3];
    unsigned int wrong = 0;
    unsigned long counted = 0, expected = 0;

    for (unsigned int at = 0; at < HELD_W * HELD_H; at++)
        wall[at] = at * 2654435761u;
    for (unsigned int at = 0; at < 40 * 30; at++)
        window[at] = 0xc06020u + at;
    for (unsigned int at = 0; at < 12 * 10; at++)
        above[at] = 0x204080u;
    for (int config = 0; config < 3; config++)
        for (int k = 0; k < 4; k++)
        {
            const unsigned int ink = inks[k];
            const int line = config == 2 ? 0 : 4, x = 20, y = 12, w = 40, h = 30;
            unsigned long before;

            frames[0] = (struct compositor_frame){.x = 0, .y = 0, .width = HELD_W, .height = HELD_H, .scale = 100, .pixels = wall, .pitch = HELD_W};
            frames[1] = (struct compositor_frame){.x = x, .y = y, .width = w, .height = h, .scale = 100, .pixels = window, .pitch = 40,
                                                  .radius = config == 2 ? 0 : 10, .line = line, .edge = 0x335577u,
                                                  .square = config == 1 ? 15 : 0, .preview = {0, 0, 48, 32}, .preview_ink = ink};
            frames[2] = (struct compositor_frame){.x = 30, .y = 20, .width = 12, .height = 10, .scale = 100, .pixels = above, .pitch = 12};
            memory_copy(plain, frames, sizeof(frames));
            plain[1].preview[2] = 0;

            // The reference: the scene with no tint, the whole target tinted per pixel, then the window and the one over it drawn over it.
            // The tint's pixel rule is memory_mix_u32_row's, which the card's mix records follow too: a cover of none leaves the pixel
            // as it is, its top byte with it (compositor_mix's own answer there is the pixel with its top byte cleared).
            memory_copy(want, wall, sizeof(wall));
            compositor_scene(&reference, plain, 3);
            for (int py = 0; py < 32; py++)
                for (int px = 0; px < 48; px++)
                    if (ink >> 24)
                        want[py * HELD_W + px] = compositor_mix(want[py * HELD_W + px], ink & 0xffffffu, ink >> 24);
            {
                struct compositor_target cell = compositor_clipped(&reference, 0, 0, 48, 32);

                compositor_radii(plain, 3, radii);
                for (unsigned int each = 1; each < 3; each++)
                    compositor_draw_with(&cell, &plain[each], radii[each]);
            }

            memory_copy(pixels, wall, sizeof(wall));
            before = compositor_tint_pixels;
            compositor_scene(&t, frames, 3);
            counted += compositor_tint_pixels - before;
            for (unsigned int at = 0; at < HELD_W * HELD_H; at++)
                wrong += pixels[at] != want[at];

            compositor_radii(frames, 3, radii);
            {
                int ri = radii[1] > line ? radii[1] - line : 0;

                for (int py = 0; py < 32; py++)
                    for (int px = 0; px < 48; px++)
                    {
                        int cross = (py >= y && py < y + h && px >= x + ri && px < x + w - ri) ||
                                    (py >= y + ri && py < y + h - ri && px >= x && px < x + w);

                        expected += !cross;
                    }
            }
        }
    check("preview held: over a window the tint and redraw leave the same pixels as the whole tint, at every ink and corner",
          wrong == 0);
    check("preview held: a window over its target tints only the pixels it does not cover",
          counted == expected && expected > 0);

    // The pointer goes onto the top-left quarter of a dragged window's output and stops there.
    {
        static unsigned int scanout[HELD_OUT_W * HELD_OUT_H];
        unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
        unsigned long buffer = ((unsigned long)HELD_OUT_W * HELD_OUT_H * 4 + 4095) & ~4095ul;
        long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 3, 0x4022, (positive)-1, 0);
        struct windows address_to table;
        int target[4];

        if (mapped < 0 && mapped > -4096)
        {
            check("preview held: the table is mapped", false);
            return;
        }
        compositor_zero();
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.preview_row = COMPOSITOR_NONE;
        compositor.mark_row = compositor.mark_with = compositor.shared_row = COMPOSITOR_NONE;
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
        compositor.width = HELD_OUT_W;
        compositor.height = HELD_OUT_H;
        compositor.scale = 100;
        compositor.card = -1;
        compositor.scanout = scanout;
        compositor.scanout_pitch = HELD_OUT_W * 4;
        compositor.outputs = 1;
        compositor.screen[0][2] = HELD_OUT_W, compositor.screen[0][3] = HELD_OUT_H;
        compositor.stride = compositor.most_w = HELD_OUT_W;
        compositor.room_h = compositor.most_h = HELD_OUT_H;
        table = compositor.table = (struct windows address_to)mapped;
        table->motion = 1;
        compositor.pixels = table->pixels = header;
        compositor.buffer = table->buffer = buffer;
        table->stride = HELD_OUT_W;
        table->output_width = HELD_OUT_W;
        table->output_height = HELD_OUT_H;

        // Row 2 is shown at 200 by 140, floating, and dragged by its own offset, so the window stays where it is.
        {
            unsigned int address_to pixels = windows_buffer(table, 2, 0);

            table->pid[2] = 2;
            table->x[2] = 60, table->y[2] = 50, table->width[2] = 200, table->height[2] = 140;
            for (unsigned int y = 0; y < 140; y++)
                for (unsigned int x = 0; x < 200; x++)
                    pixels[y * HELD_OUT_W + x] = 0x203040u;
            memory_copy(table->title[2], "held", 5);
            table->buffer_index[2] = 0;
            table->damage_width[2] = 0;
            table->damage_height[2] = 140;
            table->frame[2] = 1;
            compositor.showing[2] = 1;
            compositor.stack[0] = 2, compositor.stacked = 1;
            compositor.x[2] = 60, compositor.y[2] = 50, compositor.w[2] = 200, compositor.h[2] = 140;
            compositor_take(2);
        }
        compositor_damage(0, 0, HELD_OUT_W, HELD_OUT_H);
        compositor_present();

        compositor.drag = 2;
        compositor.drag_moved = 1;
        compositor.button_down = 1;
        compositor.button_row[0] = 2;
        compositor.drag_x = 300 - 60, compositor.drag_y = 300 - 50;
        compositor.pointer_x = 300, compositor.pointer_y = 300;
        compositor_motion();
        compositor_present();
        check("preview held: the window dragged off every target shows none, and the pointer's stop is idle",
              compositor.preview[2] == 0 && compositor.damages == 0);

        // The pointer moves onto the left half: that half is damaged, and nothing else is. (A target on the top edge
        // brings the grid up, which is damaged with it; that is checked with the grid.)
        /* The window is taken to the left edge by the hand (its rectangle touches the edge), which is what a target is now. */
        compositor.pointer_x = 2, compositor.pointer_y = 200;
        compositor.drag_x = 2, compositor.drag_y = 200 - 50;
        compositor_preview(2);
        target[0] = 0, target[1] = 0, target[2] = HELD_OUT_W / 2, target[3] = HELD_OUT_H;
        check("preview held: a target entered is damaged as its own rectangle, and no other",
              compositor.damages == 1 && compositor.damage[0][0] == target[0] && compositor.damage[0][1] == target[1] &&
                  compositor.damage[0][2] == target[0] + target[2] && compositor.damage[0][3] == target[1] + target[3] &&
                  compositor.preview[2] == target[2]);
        compositor_preview(2);
        check("preview held: the pointer held still over the target damages nothing more", compositor.damages == 1);

        // The frame loop: present what is damaged, then nothing is damaged again while the pointer stays put.
        compositor_present();
        compositor_land_keep();
        check("preview held: once presented, with the pointer still on the target, the compositor has nothing to do",
              compositor.damages == 0 && compositor.preview[2] == target[2] && compositor.preview_row == 2);
        compositor_call(syscall(munmap), (positive)mapped, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 0, 0, 0, 0);
        compositor_zero();
    }
}

/*
        A scene that reads what it is composed into, composed into a target only written (a scanout): the rows it reads are
        composed aside, a slice at a time, and copied out (compositor_scene_band), so the picture is bit for bit the one the
        scene composed in place makes, and nothing reads the target. Over a blended window (its premultiplied picture over a
        round window with a line and the wallpaper), a snap preview's tint over a round window with the window drawn again
        over it, a shared edge's mark, the two together, and neither; over the whole target, which is bands on threads, and a
        damage inside it, which is one band at an offset. A read is counted where a path that reads its target meets one
        only written (compositor_reading); the scene composed in place into such a target is the count's control.
*/
#define ASIDE_W 640
#define ASIDE_H 160
static void check_compositor_aside(void)
{
    static unsigned int got[ASIDE_W * ASIDE_H], want[ASIDE_W * ASIDE_H], wall[ASIDE_W * ASIDE_H];
    static unsigned int window[200 * 90], glassy[120 * 70];
    struct compositor_frame frames[4];
    unsigned long wrong = 0, read = 0, control = 0;
    static const int damage[2][4] = {{0, 0, ASIDE_W, ASIDE_H}, {37, 21, 411, 117}};
    static const int preview_rect[4] = {40, 20, 320, 110}, mark_rect[4] = {290, 100, 200, 40};

    for (unsigned int at = 0; at < ASIDE_W * ASIDE_H; at++)
        wall[at] = (at * 2654435761u) >> 8;
    for (unsigned int at = 0; at < 200 * 90; at++)
        window[at] = 0xc06020u + at * 77u;
    // Premultiplied: each colour at most its alpha, the alpha every value.
    for (unsigned int at = 0; at < 120 * 70; at++)
    {
        unsigned int a = at * 37u & 255u, c = (at * 91u) % (a + 1);

        glassy[at] = a << 24 | c << 16 | (c / 2) << 8 | (a - c);
    }
    for (int kind = 0; kind < 5; kind++)
        for (int d = 0; d < 2; d++)
        {
            struct compositor_target t = {got, ASIDE_W, ASIDE_W, ASIDE_H, damage[d][0], damage[d][1], damage[d][2], damage[d][3], 0, 0, 0};
            struct compositor_target r = t;
            int rows[2];

            frames[0] = (struct compositor_frame){.x = 0, .y = 0, .width = ASIDE_W, .height = ASIDE_H, .scale = 100, .pixels = wall, .pitch = ASIDE_W};
            frames[1] = (struct compositor_frame){.x = 60, .y = 30, .width = 200, .height = 90, .scale = 100, .pixels = window, .pitch = 200,
                                                  .radius = 12, .line = 2, .edge = 0x335577u};
            frames[2] = (struct compositor_frame){.x = 300, .y = 50, .width = 120, .height = 70, .scale = 100, .pixels = glassy, .pitch = 120,
                                                  .radius = 9, .line = 1, .edge = 0x112233u, .blend = kind == 0 || kind == 3};
            frames[3] = (struct compositor_frame){.x = 500, .y = 10, .width = 100, .height = 60, .scale = 100, .pixels = window, .pitch = 200,
                                                  .radius = 6, .line = 1, .edge = 0x445566u};
            if (kind == 1 || kind == 3)
            {
                memory_copy(frames[1].preview, preview_rect, sizeof(preview_rect));
                frames[1].preview_ink = 0x9f0a0b0cu, frames[1].preview_radius = 10, frames[1].preview_line = 3, frames[1].preview_edge = 0xc0405060u;
            }
            if (kind == 2 || kind == 3)
            {
                memory_copy(frames[2].mark, mark_rect, sizeof(mark_rect));
                frames[2].mark_ink = 0x80ffffffu;
            }
            r.pixels = want;
            memory_copy(want, wall, sizeof(wall));
            memory_copy(got, wall, sizeof(wall));
            compositor_scene(&r, frames, 4);
            compositor_out_reads = 0;
            compositor_scene_banded(&t, frames, 4, 0, rows);
            read += compositor_out_reads;
            wrong += memory_compare(got, want, sizeof(got)) != 0;
            // The control: the same scene composed in place into a target only written reads it, wherever it reads.
            if (kind < 4)
            {
                struct compositor_target in_place = t;

                in_place.out = 1;
                compositor_out_reads = 0;
                compositor_scene(&in_place, frames, 4);
                control += compositor_out_reads != 0;
            }
        }
    check("aside: a scene composed into a target only written is the picture composed in place, bit for bit, over a blended "
          "window, a tint and its redraw, a mark, both, and neither, over the whole target and a damage inside it",
          wrong == 0);
    check("aside: and nothing reads the target it is composed into", read == 0);
    check("aside: where the same scene composed in place into it reads it in every case that reads", control == 8);
}

/*
        A blended frame with the ground alone under it in the damage is a piece of the scene's plan, laid over a row of the
        ground (compositor_blend_alone, memory_over_under_u32_rect), where every blended frame was drawn the painter's way:
        the damage filled with the ground and every frame drawn over the last. The picture is the painter's, bit for bit,
        here drawn as the scene drew it before (ground_painted), for a blended window with a round corner and a line under
        an opaque one that covers part of it; with a snap preview's tint over it; with a shared edge's mark over the one
        above; with an opaque window under it that meets it in some rows only, so a band or a slice that holds those rows
        draws the two the painter's way and the others the plan's; the blended window partly off the target; drawn at
        another size (a landing); with square corners; and two blended windows side by side; the blended window 300 wide, so
        a target only written takes the out variant's streamed rows. Over the whole target, a
        damage inside it, and one that cuts a round corner. Composed in place (compositor_scene) and in bands into a target
        only written (compositor_scene_banded), which reads nothing; and composed in place into such a target, the plan
        reads nothing where every blended window has the ground alone under it and no tint or mark is drawn.
*/
#define GROUND_W 640
#define GROUND_H 160
static void ground_painted(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count)
{
    int radii[COMPOSITOR_FRAMES_FOR(COMPOSITOR_ROWS_FIRST)];

    compositor_radii(frames, count, radii);
    compositor_fill(t, t->x1, t->y1, t->x2 - t->x1, t->y2 - t->y1, compositor_ground);
    for (unsigned int each = 0; each < count; each++)
        compositor_draw_with(t, &frames[each], radii[each]);
    compositor_preview_draw(t, frames, count, radii);
}

static void check_compositor_ground(void)
{
    static unsigned int got[GROUND_W * GROUND_H], want[GROUND_W * GROUND_H], banded[GROUND_W * GROUND_H];
    static unsigned int glassy[300 * 90], window[200 * 90];
    static const int damage[3][4] = {{0, 0, GROUND_W, GROUND_H}, {37, 21, 411, 117}, {50, 20, 40, 40}};
    static const int preview_rect[4] = {40, 20, 320, 110}, mark_rect[4] = {200, 60, 120, 30};
    unsigned int ground_was = compositor_ground;
    unsigned long wrong = 0, wrong_banded = 0, read = 0, quiet = 0, quiet_asked = 0, loud = 0;

    compositor_ground = 0x00314159u;
    // The blended window is 300 wide, past the 256 words a row the out variant streams from.
    for (unsigned int at = 0; at < 300 * 90; at++)
    {
        // Premultiplied, every alpha, a run of the clear and a run of the opaque among them.
        unsigned int a = at % 300 < 30 ? 0 : at % 300 < 60 ? 255 : at * 37u & 255u, c = (at * 91u) % (a + 1);

        glassy[at] = a << 24 | c << 16 | (c / 2) << 8 | (a - c);
    }
    for (unsigned int at = 0; at < 200 * 90; at++)
        window[at] = 0xc06020u + at * 77u;
    for (int kind = 0; kind < 8; kind++)
        for (int d = 0; d < 3; d++)
        {
            struct compositor_target t = {got, GROUND_W, GROUND_W, GROUND_H, damage[d][0], damage[d][1], damage[d][0] + damage[d][2],
                                          damage[d][1] + damage[d][3], 0, 0, 0};
            struct compositor_target r = t, b = t;
            struct compositor_frame frames[4];
            unsigned int count = 0;
            int rows[2];

            if (kind == 3)
                frames[count++] = (struct compositor_frame){.x = 20, .y = 100, .width = 300, .height = 50, .scale = 100, .pixels = window,
                                                            .pitch = 200, .radius = 6, .line = 1, .edge = 0x445566u};
            frames[count++] = (struct compositor_frame){.x = kind == 4 ? -40 : 60, .y = kind == 4 ? -10 : 30, .width = 300, .height = 90,
                                                        .scale = 100, .pixels = glassy, .pitch = 300, .radius = 12, .line = 2,
                                                        .edge = 0x335577u, .blend = 1, .square = kind == 6 ? 5 : 0,
                                                        .source_w = kind == 5 ? 150 : 0, .source_h = kind == 5 ? 70 : 0};
            if (kind == 1)
            {
                memory_copy(frames[count - 1].preview, preview_rect, sizeof(preview_rect));
                frames[count - 1].preview_ink = 0x9f0a0b0cu, frames[count - 1].preview_radius = 10, frames[count - 1].preview_line = 3,
                                 frames[count - 1].preview_edge = 0xc0405060u;
            }
            if (kind == 7)
                frames[count++] = (struct compositor_frame){.x = 400, .y = 40, .width = 120, .height = 70, .scale = 100, .pixels = glassy + 50,
                                                            .pitch = 300, .radius = 9, .line = 1, .edge = 0x112233u, .blend = 1};
            frames[count++] = (struct compositor_frame){.x = 200, .y = 70, .width = 100, .height = 60, .scale = 100, .pixels = window,
                                                        .pitch = 200, .radius = 6, .line = 1, .edge = 0x445566u};
            if (kind == 2)
            {
                memory_copy(frames[count - 1].mark, mark_rect, sizeof(mark_rect));
                frames[count - 1].mark_ink = 0x80ffffffu;
            }
            compositor_corner_masks_ready(frames, count);
            for (unsigned int at = 0; at < GROUND_W * GROUND_H; at++)
                want[at] = got[at] = banded[at] = 0x5a5a5a5au + at * 2654435761u;
            r.pixels = want;
            ground_painted(&r, frames, count);
            compositor_scene(&t, frames, count);
            wrong += memory_compare(got, want, sizeof(got)) != 0;
            b.pixels = banded, b.out = 1;
            compositor_out_reads = 0;
            compositor_scene_banded(&b, frames, count, 0, rows);
            read += compositor_out_reads;
            wrong_banded += memory_compare(banded, want, sizeof(banded)) != 0;
            // In place into a target only written: nothing is read where the ground alone is under every blended window and
            // nothing mixes over the target; with a window under it in the damage, something is.
            {
                struct compositor_target in_place = t;

                in_place.pixels = banded, in_place.out = 1;
                compositor_out_reads = 0;
                compositor_scene(&in_place, frames, count);
                if (kind == 3 && d != 2)
                    loud += compositor_out_reads != 0;
                else if (kind != 1 && kind != 2)
                    quiet += compositor_out_reads == 0, quiet_asked++;
            }
        }
    compositor_ground = ground_was;
    check("ground: a blended window with the ground alone under it is the painter's picture, bit for bit, composed in place, "
          "with a round corner, a line, an opaque window over it, a tint, a mark, a window under it in some rows, partly off the "
          "target, landing, square corners and two side by side, over the whole target, a damage inside it and one through a corner",
          wrong == 0);
    check("ground: and composed in bands into a target only written, the same picture", wrong_banded == 0);
    check("ground: which those bands do not read", read == 0);
    check("ground: composed in place into a target only written, the plan reads nothing where the ground alone is under every "
          "blended window and nothing mixes over the target",
          quiet == quiet_asked && quiet_asked == 6 * 3 - 2);
    check("ground: and reads it where a window lies under the blended one in the damage", loud == 2);
}

/*
        The chip over a target that does not meet its box: a slice of a scene composed apart above or below it, a target left
        or right of it, writes nothing, and one that meets it by a row or a column has that row or column of it and nothing
        else. The target is a page with a page that faults on each side, so a count made of a clip that went negative (a width
        or height of none or less handed to the mix) faults rather than passes.
*/
static void check_compositor_chip_clip(void)
{
    static unsigned char cover[40 * 10];
    struct compositor_chip was = compositor.chip;
    long mapped = compositor_call(syscall(mmap), 0, 3 * 4096, 3, 0x4022, (positive)-1, 0);
    // Where the target's 64 by 16 pixels lie on the desktop, against the chip's box at 100, 100, 40 by 10.
    static const int places[][2] = {{100, 200}, {100, 74}, {20, 100}, {200, 100}, {100, 109}, {139, 96}, {60, 84}};
    static const int meets[][4] = {{0}, {0}, {0}, {0}, {100, 109, 140, 110}, {139, 100, 140, 110}, {0}};
    unsigned int wrong = 0;

    if (mapped < 0 && mapped > -4096)
    {
        check("chip clip: the target's pages are mapped", false);
        return;
    }
    unsigned int *page = (unsigned int *)(mapped + 4096);

    compositor_call(syscall(mprotect), (positive)mapped, 4096, 0, 0, 0, 0);
    compositor_call(syscall(mprotect), (positive)(mapped + 2 * 4096), 4096, 0, 0, 0, 0);
    for (unsigned int at = 0; at < sizeof(cover); at++)
        cover[at] = (unsigned char)(at * 37u | 1u);
    memory_zero(&compositor.chip, sizeof(compositor.chip));
    compositor.chip.kind = COMPOSITOR_CHIP_HINT, compositor.chip.rel_x = 100, compositor.chip.rel_y = 100;
    compositor.chip.w = 40, compositor.chip.h = 10, compositor.chip.alpha = 255, compositor.chip.cover = cover;
    for (unsigned int k = 0; k < sizeof(places) / sizeof(places[0]); k++)
    {
        struct compositor_target t = {page, 64, 4096, 4096, places[k][0], places[k][1], places[k][0] + 64, places[k][1] + 16,
                                      places[k][0], places[k][1], 0};

        for (unsigned int at = 0; at < 1024; at++)
            page[at] = 0x5a5a5a5au;
        compositor_chip_draw(&t);
        for (unsigned int at = 0; at < 1024; at++)
        {
            int x = places[k][0] + (int)(at % 64), y = places[k][1] + (int)(at / 64);
            int inside = x >= meets[k][0] && x < meets[k][2] && y >= meets[k][1] && y < meets[k][3];

            wrong += inside ? page[at] == 0x5a5a5a5au : page[at] != 0x5a5a5a5au;
        }
    }
    compositor.chip = was;
    compositor_call(syscall(munmap), (positive)mapped, 3 * 4096, 0, 0, 0, 0);
    check("chip clip: a target above, below, left or right of the chip, or past its corner, has nothing of it, and one that meets it "
          "by a row or a column has that and no more",
          wrong == 0);
}


/* The arrangement rows written before the split ask for: every tray item after the tasks and the clock a cell of the tray, as `taskbar item ...
   after` and `taskbar clock tray` set; and the standard one back. */
static void taskbar_classic(void)
{
    taskbar.tray_before = 0, taskbar.clock_tray = 1;
    taskbar_changed();
}

static void taskbar_standard(void)
{
    taskbar.tray_before = 1u << 1 | 1u << 2, taskbar.clock_tray = 0;
    taskbar.moon_zone = TASKBAR_ZONE_CENTRE, taskbar.tasks_zone = TASKBAR_ZONE_SPLIT, taskbar.tasks_right = 0;
    taskbar_changed();
}

/*
        A shared edge: two windows that meet edge to edge with the same extent take a press anywhere along their edge, from
        one end to the other, as one resize, one grown as the other shrinks and both still meeting. The indicator is a tint
        across the whole of that edge and nowhere else. An edge that does not meet exactly, and a corner, are the window's
        own.
*/
#define SHARED_W 640
#define SHARED_H 400
// The program of row answers the size it was asked with a picture of exactly that size, as a window does once it has drawn.
static void shared_answer(int row)
{
    struct windows address_to table = compositor.table;
    unsigned int address_to pixels = windows_buffer(table, row, 0);
    unsigned int width = table->want_width[row], height = table->want_height[row];

    for (unsigned int line = 0; line < height; line++)
        for (unsigned int column = 0; column < width; column++)
            pixels[line * SHARED_W + column] = 0xff202020u + (unsigned int)row;
    table->width[row] = width;
    table->height[row] = height;
    table->buffer_index[row] = 0;
    table->damage_width[row] = 0;
    table->damage_height[row] = height;
    table->frame[row]++;
    compositor_take(row);
}
static void check_compositor_shared(void)
{
    static unsigned int scanout[SHARED_W * SHARED_H], plain[SHARED_W * SHARED_H], marked[SHARED_W * SHARED_H];
    unsigned long header = (sizeof(struct windows) + 4095) & ~4095ul;
    unsigned long buffer = ((unsigned long)SHARED_W * SHARED_H * 4 + 4095) & ~4095ul;
    long mapped = compositor_call(syscall(mmap), 0, header + buffer * 2 * COMPOSITOR_ROWS_FIRST, 3, 0x4022, (positive)-1, 0);
    struct windows address_to table;
    struct compositor_frame frames[2];
    struct compositor_target t;
    int mark[4], differs = 0, outside = 0;

    // The moon and the tasks at the band's start, so the shared edge in the middle of the screen is no pill's down to its last row.
    taskbar.moon_zone = TASKBAR_ZONE_START, taskbar.tasks_zone = TASKBAR_ZONE_START;
    taskbar_changed();
    if (mapped < 0 && mapped > -4096)
    {
        check("shared: the table is mapped", false);
        taskbar_standard();
        return;
    }
    compositor_zero();
    compositor.focus = compositor.hover = compositor.grab = compositor.drag = COMPOSITOR_NONE;
    compositor.resize = compositor.press = compositor.keys_to = compositor.preview_row = COMPOSITOR_NONE;
    compositor.direct_row = compositor.flip_row = compositor.plane_row = -1;
    compositor.width = SHARED_W;
    compositor.height = SHARED_H;
    compositor.scale = 100;
    compositor.card = -1;
    compositor.scanout = scanout;
    compositor.scanout_pitch = SHARED_W * 4;
    compositor.outputs = 1;
    compositor.output[0].scale = 100;
    compositor.screen[0][2] = SHARED_W, compositor.screen[0][3] = SHARED_H;
    compositor.stride = compositor.most_w = SHARED_W;
    compositor.room_h = compositor.most_h = SHARED_H;
    table = compositor.table = (struct windows address_to)mapped;
    table->motion = 1;
    compositor.pixels = table->pixels = header;
    compositor.buffer = table->buffer = buffer;
    table->stride = SHARED_W;
    table->output_width = SHARED_W;
    table->output_height = SHARED_H;
    compositor.mark_row = compositor.mark_with = compositor.shared_row = COMPOSITOR_NONE;

    /* Two windows, shown, the left half and the right half: they meet on the middle edge, over its whole height. */
    table->pid[2] = 2, table->pid[3] = 3;
    compositor.showing[2] = compositor.showing[3] = 1;
    compositor.stack[0] = 2, compositor.stack[1] = 3, compositor.stacked = 2;
    compositor_arrange(2, COMPOSITOR_LEFT);
    compositor_arrange(3, COMPOSITOR_RIGHT);
    check("shared: two halves meet on the middle edge, the whole height of each",
          compositor.x[2] + (int)compositor.w[2] == compositor.x[3] && compositor.y[2] == compositor.y[3] &&
              compositor.h[2] == compositor.h[3] && compositor.w[2] == SHARED_W / 2);
    check("shared: the window on the far side of an edge is the one that meets it, and an edge that meets nothing is none",
          compositor_meets(3, COMPOSITOR_EDGE_LEFT) == 2 && compositor_meets(2, COMPOSITOR_EDGE_RIGHT) == 3 &&
              compositor_meets(2, COMPOSITOR_EDGE_LEFT) == COMPOSITOR_NONE && compositor_meets(3, COMPOSITOR_EDGE_TOP) == COMPOSITOR_NONE);
    compositor.h[3] = SHARED_H - 40;
    check("shared: an edge that meets with a different extent is not shared", compositor_meets(3, COMPOSITOR_EDGE_LEFT) == COMPOSITOR_NONE);
    compositor.h[3] = SHARED_H;

    /* Hovering the edge shows the indicator over the topmost of the two, the same strip along the whole of it. */
    screens_hand(SHARED_W / 2 + 1, SHARED_H / 2, 0);
    check("shared: over the edge the indicator is the strip across all of it, from its top end to its bottom end, over the topmost window",
          compositor.mark_row == 3 && compositor.mark_with == 2 && compositor.mark[0] == SHARED_W / 2 - 2 &&
              compositor.mark[1] == 0 && compositor.mark[2] == 4 && compositor.mark[3] == SHARED_H);
    {
        int same = 1;
        static const int along[] = {0, 3, 20, SHARED_H / 4 - 1, SHARED_H - 21, SHARED_H - 2, SHARED_H - 1};

        for (unsigned int at = 0; at < array_count(along); at++)
        {
            screens_hand(SHARED_W / 2 + 1, SHARED_H / 2, 0);
            screens_hand(SHARED_W / 2 + 1, along[at], 0);
            same &= compositor.mark_row == 3 && compositor.mark[0] == SHARED_W / 2 - 2 && compositor.mark[1] == 0 &&
                    compositor.mark[2] == 4 && compositor.mark[3] == SHARED_H;
        }
        check("shared: and it is the same strip over the first row of the edge, the top quarter, the last quarter and the last row", same);
    }

    /* A press anywhere along the edge takes both: the one pressed, and the window it is shared with. */
    screens_hand(SHARED_W / 2 + 1, SHARED_H / 2, 0);
    screens_hand(SHARED_W / 2 + 1, SHARED_H / 2, 1);
    check("shared: a press on the edge resizes both: the one pressed and the one it shares its edge with",
          compositor.resize == 3 && compositor.shared_row == 2 && compositor.shared_lo == 2 && compositor.shared_hi == 3 &&
              compositor.edges[2] == COMPOSITOR_EDGE_RIGHT && compositor.edges[3] == COMPOSITOR_EDGE_LEFT);
    screens_hand(SHARED_W / 2 + 21, SHARED_H / 2, 0);
    check("shared: dragged 20 px, one window grows 20 and the other shrinks 20 from its near edge, and they still meet",
          compositor.w[2] == SHARED_W / 2 + 20 && compositor.x[3] == SHARED_W / 2 + 20 && compositor.w[3] == SHARED_W / 2 - 20 &&
              compositor.x[2] + (int)compositor.w[2] == compositor.x[3] && compositor.x[3] + (int)compositor.w[3] == SHARED_W);
    screens_hand(4, SHARED_H / 2, 0);
    check("shared: dragged past the least a window is, the left one stops at it and the right keeps its far edge",
          compositor.w[2] == WINDOW_MIN_WIDTH * compositor.scale / 100 && compositor.x[2] + (int)compositor.w[2] == compositor.x[3] &&
              compositor.x[3] + (int)compositor.w[3] == SHARED_W);
    screens_hand(4, SHARED_H / 2, -1);
    check("shared: let go, the resize ends and the indicator goes",
          compositor.resize == COMPOSITOR_NONE && compositor.mark[2] == 0 && compositor.mark_row == COMPOSITOR_NONE);

    /* The indicator is drawn on that edge's whole length and nowhere else: the scene with it and without it differ there only. */
    compositor_arrange(2, COMPOSITOR_LEFT);
    compositor_arrange(3, COMPOSITOR_RIGHT);
    screens_hand(SHARED_W / 2 + 1, SHARED_H / 2, 0);
    memory_copy(mark, compositor.mark, sizeof(mark));
    compositor_frame_of(compositor.stack[0], &frames[0]);
    compositor_frame_of(compositor.stack[1], &frames[1]);
    t = (struct compositor_target){marked, SHARED_W, SHARED_W, SHARED_H, 0, 0, SHARED_W, SHARED_H, 0, 0, 0};
    compositor_scene(&t, frames, 2);
    compositor_mark_clear();
    compositor_frame_of(compositor.stack[0], &frames[0]);
    compositor_frame_of(compositor.stack[1], &frames[1]);
    t.pixels = plain;
    compositor_scene(&t, frames, 2);
    for (int y = 0; y < SHARED_H; y++)
        for (int x = 0; x < SHARED_W; x++)
            if (marked[y * SHARED_W + x] != plain[y * SHARED_W + x])
            {
                differs++;
                if (x < mark[0] || x >= mark[0] + mark[2] || y < mark[1] || y >= mark[1] + mark[3])
                    outside++;
            }
    check("shared: the indicator changes pixels on the edge's whole length, and no pixel outside it",
          differs > 0 && outside == 0 && differs <= mark[2] * mark[3]);
    check("shared: and it changes the first row and the last row of the edge, which the middle half left as they were",
          marked[0 * SHARED_W + SHARED_W / 2] != plain[0 * SHARED_W + SHARED_W / 2] &&
              marked[(SHARED_H - 1) * SHARED_W + SHARED_W / 2] != plain[(SHARED_H - 1) * SHARED_W + SHARED_W / 2] &&
              marked[20 * SHARED_W + SHARED_W / 2 - 1] != plain[20 * SHARED_W + SHARED_W / 2 - 1]);

    /* A press at the top quarter, the first row and the last row of the edge takes both as it does in the middle: the screen's
       own top and bottom are no grip of a window, so the end of the edge there is not a corner. */
    {
        static const int along[] = {20, 0, SHARED_H - 1, SHARED_H - 30};
        int all = 1;

        for (unsigned int at = 0; at < array_count(along); at++)
        {
            screens_hand(SHARED_W / 2 + 1, along[at], 1);
            all &= compositor.resize == 3 && compositor.shared_row == 2 && compositor.edges[2] == COMPOSITOR_EDGE_RIGHT &&
                   compositor.edges[3] == COMPOSITOR_EDGE_LEFT;
            screens_hand(SHARED_W / 2 + 1, along[at], -1);
            compositor.press = COMPOSITOR_NONE;
        }
        check("shared: a press on the top quarter, the first row, the last row and the last quarter of the edge takes both windows", all);
    }

    /* Two quarters, one above the other, meet on a horizontal edge: its middle half is shared the same way. */
    compositor_arrange(2, COMPOSITOR_TOP_LEFT);
    compositor_arrange(3, COMPOSITOR_BOTTOM_LEFT);
    screens_hand(SHARED_W / 4, SHARED_H / 2 + 1, 0);
    check("shared: two quarters one above the other share their horizontal edge, and all of it shows the indicator",
          compositor.mark_row == 3 && compositor.mark_with == 2 && compositor.mark[0] == 0 &&
              compositor.mark[1] == SHARED_H / 2 - 2 && compositor.mark[2] == SHARED_W / 2 && compositor.mark[3] == 4);
    screens_hand(1, SHARED_H / 2 + 1, 1);
    check("shared: a press on the first column of that edge takes both quarters too, the screen's own left being no grip",
          compositor.resize == 3 && compositor.shared_row == 2 && compositor.shared_vertical == 0);
    screens_hand(1, SHARED_H / 2 + 1, -1);
    compositor.press = COMPOSITOR_NONE;
    screens_hand(SHARED_W / 4, SHARED_H / 2 + 1, 0);
    screens_hand(SHARED_W / 4, SHARED_H / 2 + 1, 1);
    check("shared: a press on it takes both quarters, and dragged down the upper grows as the lower shrinks",
          compositor.resize == 3 && compositor.shared_row == 2 && compositor.shared_lo == 2 && compositor.shared_vertical == 0 &&
              compositor.edges[2] == COMPOSITOR_EDGE_BOTTOM && compositor.edges[3] == COMPOSITOR_EDGE_TOP);
    screens_hand(SHARED_W / 4, SHARED_H / 2 + 31, 0);
    check("shared: dragged 30 px down, the upper is 230 high and the lower starts at 230 and is 170 high",
          compositor.h[2] == SHARED_H / 2 + 30 && compositor.y[3] == SHARED_H / 2 + 30 && compositor.h[3] == SHARED_H / 2 - 30);
    screens_hand(SHARED_W / 4, SHARED_H / 2 + 31, -1);

    /* The programs answer. Pressed on the left window's edge, the left one is the hand's and the right one is the other:
       its picture of the size asked must not take its edge away, or the two stop meeting. Each drag is answered, as a
       window answers the motion it was asked for. */
    compositor_arrange(2, COMPOSITOR_LEFT);
    compositor_arrange(3, COMPOSITOR_RIGHT);
    compositor.stack[0] = 3, compositor.stack[1] = 2;
    shared_answer(2), shared_answer(3);
    screens_hand(SHARED_W / 2 - 1, SHARED_H / 2, 0);
    screens_hand(SHARED_W / 2 - 1, SHARED_H / 2, 1);
    check("shared answered: a press on the left window's edge takes both, the left one the hand's and the right one the other",
          compositor.resize == 2 && compositor.shared_row == 3 && compositor.shared_lo == 2 && compositor.shared_hi == 3 &&
              compositor.edges[2] == COMPOSITOR_EDGE_RIGHT && compositor.edges[3] == COMPOSITOR_EDGE_LEFT);
    screens_hand(SHARED_W / 2 + 19, SHARED_H / 2, 0);
    shared_answer(2), shared_answer(3);
    check("shared answered: dragged 20 px and both answered, they still meet, and the indicator is on the new edge",
          compositor.w[2] == SHARED_W / 2 + 20 && compositor.x[3] == SHARED_W / 2 + 20 && compositor.w[3] == SHARED_W / 2 - 20 &&
              compositor.x[2] + (int)compositor.w[2] == compositor.x[3] && compositor.x[3] + (int)compositor.w[3] == SHARED_W &&
              compositor.mark[0] == compositor.x[3] - 2 && compositor.mark[2] == 4);
    screens_hand(SHARED_W / 2 + 49, SHARED_H / 2, 0);
    shared_answer(2), shared_answer(3);
    check("shared answered: dragged 50 px and answered again, the two still meet and fill the width",
          compositor.w[2] == SHARED_W / 2 + 50 && compositor.x[2] + (int)compositor.w[2] == compositor.x[3] &&
              compositor.x[3] + (int)compositor.w[3] == SHARED_W);
    screens_hand(4, SHARED_H / 2, 0);
    shared_answer(2), shared_answer(3);
    check("shared answered: dragged to the least, the left stops at the least and the right keeps its far edge",
          compositor.w[2] == WINDOW_MIN_WIDTH * compositor.scale / 100 && compositor.x[2] + (int)compositor.w[2] == compositor.x[3] &&
              compositor.x[3] + (int)compositor.w[3] == SHARED_W);
    screens_hand(4, SHARED_H / 2, -1);
    shared_answer(2), shared_answer(3);
    check("shared answered: let go and answered, both keep the sizes they were let go at and still meet",
          compositor.resize == COMPOSITOR_NONE && compositor.shared_row == COMPOSITOR_NONE &&
              compositor.w[2] == WINDOW_MIN_WIDTH * compositor.scale / 100 && compositor.x[2] + (int)compositor.w[2] == compositor.x[3] &&
              compositor.x[3] + (int)compositor.w[3] == SHARED_W);
    taskbar_standard();
}

/*
        Alt-Tab, ended by a window leaving. A window that goes while an Alt-Tab
        is under way ends it, since the stack is one shorter; the window it
        stopped on has the keys, and what letting go of Alt does to it (bring it
        back if it was put away) has to happen then too. Otherwise the keys sit
        on a window that is not on the screen and nothing brings it back.
*/
static void check_compositor_cycle(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE];
    struct windows address_to table;

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
        check("cycle: the table is mapped", false);
        return;
    }
    table = compositor.table;
    for (int row = 1; row <= 3; row++)
    {
        table->asked[row] = 1;
        table->x[row] = 10 + 40 * row;
        table->y[row] = 10 + 20 * row;
        table->width[row] = 120;
        table->height[row] = 80;
        glass_paint(row, 0, 0, 120, 80, 0x404040u * (unsigned)row, true);
    }
    compositor_present();

    /* Alt-Tab twice: from 3 to 2, then to 1, which is put away. */
    compositor_focus_step();
    compositor_focus_step();
    compositor.state[1] |= WINDOW_MINIMIZED;
    table->state[1] = compositor.state[1];
    check("cycle: an Alt-Tab stopped on a put-away window has the keys", compositor.focus == 1 && compositor.cycle == 1);

    /* Window 3 goes while the Alt-Tab is under way. */
    compositor_leave(3);
    check("cycle: the Alt-Tab is over, and the window it stopped on is back on the screen with the keys",
          compositor.cycle == 0 && compositor.focus == 1 && !(compositor.state[1] & WINDOW_MINIMIZED) && compositor.showing[1]);
}

/*
        The desktop's ink and the pointer's look, which `moonwater canvas background` and
        `moonwater canvas cursor` keep. A colour is parsed as the verb keeps it; once the ink is
        another none of the default's is left on the screen, round the corners too; a tab of
        glass over any ink has a title that can be read; and the pointer is drawn in the colours
        and at the size it was asked, on the plane only where the plane holds it.
*/
static int look_luma(unsigned int colour)
{
        return (int)((299 * (colour >> 16 & 255) + 587 * (colour >> 8 & 255) + 114 * (colour & 255)) / 1000);
}

/* The tray's format (`taskbar tray`): what setting_tray_valid takes and refuses, what the tray says for a format (a {name}
   filled in, the braces doubled, UTF-8 whole), that a format of the person's is one text over the whole tray (no clock
   or battery cell of its own), that the standard and default are the tray as it always was, and that a long one leaves
   the tabs half of the band. */
static void check_taskbar_tray(void)
{
    static const char *const good[] = {SETTING_TASKBAR_TRAY_STANDARD, "{time.24h.hour}:{time.24h.min}", "{{x}} {host}",
                                       "{date.weekday} {date.day} {date.monthname}  {cpu.load}  {mem.used}%  {temp}C", "no names at all",
                                       "\xe6\x97\xa5\xe6\x9c\xac {battery.percent}", "\xed\x9f\xbf \xf4\x8f\xbf\xbf", "", null};
    static const char *const bad[] = {"{nope}", "{battery", "}", "a}b", "{}", "{ battery}", "tab\there", "\xff", "\xe6\x97",
                                      "{time.24h.hour}{", "\xe0\x80\x80", "\xe0\x90\x80", "\xed\xa0\x80", "\xf0\x80\x80\x80",
                                      "\xf4\x90\x80\x80", null};
    char long_format[SETTING_TRAY_MOST + 8];
    struct taskbar_view view;
    int x, y, w, h, bx, by, bw, bh, cx, cy, cw, ch;
    positive where = 99;

    for (int at = 0; good[at]; at++)
        check("tray format: a format that names what there is and is whole text is taken", setting_tray_valid(good[at], null));
    for (int at = 0; bad[at]; at++)
        check("tray format: one that names what is not there, is not paired, or is not printable text is refused", !setting_tray_valid(bad[at], &where));
    setting_tray_valid("ab{nope}", &where);
    check("tray format: a refusal says where it goes wrong", where == 2);
    for (unsigned int at = 0; at < sizeof(long_format) - 1; at++)
        long_format[at] = 'a';
    long_format[SETTING_TRAY_MOST] = 0;
    check("tray format: the most bytes is taken", setting_tray_valid(long_format, null));
    long_format[SETTING_TRAY_MOST] = 'a';
    long_format[SETTING_TRAY_MOST + 1] = 0;
    check("tray format: and one more is not", !setting_tray_valid(long_format, null));

    compositor_zero();
    compositor.width = 2560;
    compositor.height = 1440;
    compositor.scale = 100;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
    check("tray format: the standard is the tray as it always was", !taskbar.tray_custom);
    taskbar_tray_set("default");
    check("tray format: and so is default", !taskbar.tray_custom);
    taskbar_tray_set("T{{{time.24h.hour}:{time.24h.min}}}!\n");
    check("tray format: a format of the person's is drawn as a text of its own", taskbar.tray_custom &&
          taskbar.tray_text[0] == 'T' && taskbar.tray_text[1] == '{' && taskbar.tray_text[4] == ':' && taskbar.tray_text[7] == '}' &&
          taskbar.tray_text[8] == '!' && !taskbar.tray_text[9] && taskbar.tray_chars == 9);
    check("tray format: the hour and the minute are two digits", taskbar.tray_text[2] >= '0' && taskbar.tray_text[2] <= '2' &&
          taskbar.tray_text[3] >= '0' && taskbar.tray_text[3] <= '9' && taskbar.tray_text[5] >= '0' && taskbar.tray_text[5] <= '5' &&
          taskbar.tray_text[6] >= '0' && taskbar.tray_text[6] <= '9');
    view = compositor_taskbar_view();
    check("tray format: the tray is one slot, with no clock or battery cell inside it", taskbar_slot(&view, -2, &x, &y, &w, &h) &&
          !taskbar_slot(&view, -3, &cx, &cy, &cw, &ch) && !taskbar_slot(&view, -4, &bx, &by, &bw, &bh));
    {
        int vx, vy, vw, vh, mx, my, mw, mh, nx, ny, nw, nh;

        taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD_12H);
        taskbar_volume_set(1, 50, 0, 0);
        taskbar_mic_set(1, 0);
        taskbar_notices_set(1, WINDOWS_ISSUE_WARNING);
        view = compositor_taskbar_view();
        check("tray format: 12h clock keeps notices, mic, and volume cells",
              taskbar_slot(&view, TASKBAR_NOTICES, &nx, &ny, &nw, &nh) &&
              taskbar_slot(&view, TASKBAR_MIC, &mx, &my, &mw, &mh) &&
              taskbar_slot(&view, TASKBAR_VOLUME, &vx, &vy, &vw, &vh) &&
              taskbar_hit_reach(&view, nx + nw / 2, ny + nh / 2, 0) == TASKBAR_NOTICES &&
              taskbar_hit_reach(&view, mx + mw / 2, my + mh / 2, 0) == TASKBAR_MIC &&
              taskbar_hit_reach(&view, vx + vw / 2, vy + vh / 2, 0) == TASKBAR_VOLUME);
        taskbar_volume_set(0, 0, 0, 0);
        taskbar_mic_set(0, 0);
        taskbar_notices_set(0, 0);
    }
    taskbar_tray_set("\xe6\x97\xa5\xe6\x9c\xac {host}");
    check("tray format: a character is counted once, not by its bytes", taskbar.tray_custom && taskbar.tray_chars >= 3);
    {
        // Fifteen host names of 31 bytes fill the tray's text to its end, and the three {{ after them have no room: the
        // text ends inside its buffer and what follows it is untouched (it took one byte and its end past it for each).
        // /proc/version's first line stands for a host name that long; the format is 96 bytes, which the setting takes.
        char wide[SETTING_TRAY_MOST + 1];
        int length = 0, ended = 0;

        for (int each = 0; each < 15; each++)
            for (const char *name = "{host}"; *name; name++)
                wide[length++] = *name;
        for (int each = 0; each < 6; each++)
            wide[length++] = '{';
        wide[length] = 0;
        taskbar_host_file = "/proc/version";
        taskbar.launcher = 0x5a5a5a5a;
        taskbar_tray_set(wide);
        for (unsigned int at = 0; at < sizeof(taskbar.tray_text); at++)
            ended |= !taskbar.tray_text[at];
        check("tray format: fifteen long host names and three braces end inside the tray's text, and nothing after it is written",
              setting_tray_valid(wide, null) && taskbar.tray_custom && ended && taskbar.launcher == 0x5a5a5a5a &&
                  taskbar.tray_chars < (int)sizeof(taskbar.tray_text));
        taskbar.launcher = 0;
        taskbar_host_file = "/proc/sys/kernel/hostname";
    }
    taskbar_tray_set("{nope} is refused where it is kept, and if the file is edited by hand it is the standard");
    check("tray format: a file that holds what is refused is the standard", !taskbar.tray_custom);
    for (unsigned int at = 0; at < sizeof(long_format) - 1; at++)
        long_format[at] = 'W';
    long_format[SETTING_TRAY_MOST] = 0;
    taskbar_tray_set(long_format);
    view = compositor_taskbar_view();
    taskbar_band(&view, &bx, &by, &bw, &bh);
    check("tray format: a long one leaves the tabs half of the band", taskbar_slot(&view, -2, &x, &y, &w, &h) && w <= bw / 2);
    taskbar_tray_set("");
}

/* The taskbar's geometry with no card and no font: where the band sits for each position, that the tray (here the battery, the clock
   being the moon's) is inside it and after the moon along it, that a point in the tray is the tray and a point off the band is none,
   and that none is shown when there is none to show. */
static void check_taskbar_geometry(void)
{
    static const char *const positions[4] = {"bottom", "top", "left", "right"};
    static const int band[4][4] = {{0, 1440 - COMPOSITOR_PANEL, 2560, COMPOSITOR_PANEL}, {0, 0, 2560, COMPOSITOR_PANEL}, {0, 0, COMPOSITOR_PANEL, 1440},
                                   {2560 - COMPOSITOR_PANEL, 0, COMPOSITOR_PANEL, 1440}};

    for (int at = 0; at < 4; at++)
    {
        struct taskbar_view view;
        int x, y, w, h, tx, ty, tw, th, bx, by, bw, bh;

        compositor_zero();
        compositor.width = 2560;
        compositor.height = 1440;
        compositor.scale = 100;
        int mx, my, mw, mh;

        taskbar_settings_set(positions[at], "primary", "battery none\n");
        view = compositor_taskbar_view();
        check("taskbar: with the battery none, no battery cell is drawn", !taskbar_slot(&view, -4, &bx, &by, &bw, &bh));
        taskbar.battery_item = 1, taskbar.battery_percent = 50;
        taskbar_changed();
        view = compositor_taskbar_view();
        taskbar_band(&view, &x, &y, &w, &h);
        check("taskbar: the band is on its own edge of a 2560 by 1440 output, as thick as the panel", x == band[at][0] && y == band[at][1] && w == band[at][2] && h == band[at][3]);
        check("taskbar: the tray is inside the band, after the moon along it", taskbar_slot(&view, -2, &tx, &ty, &tw, &th) && taskbar_slot(&view, TASKBAR_LAUNCHER, &mx, &my, &mw, &mh) &&
              tx >= x && ty >= y && tx + tw <= x + w && ty + th <= y + h && (at < 2 ? tx >= mx + mw : ty >= my + mh));
        check("taskbar: a point in the tray is the tray, a point off the band is none", taskbar_hit_reach(&view, tx + tw / 2, ty + th / 2, 0) == -2 &&
              taskbar_hit_reach(&view, 1280, 700, 0) == TASKBAR_HIT_NONE);
        taskbar.battery_item = 0;
    }

    /* The battery follows the "battery" line of the kept settings (/root/tune, as the battery verb writes it): none
       shows nothing, a name that is not a battery shows nothing, and auto (no line) shows the first battery there is.
       A battery is drawn just before the clock, so its cell ends where the clock's begins. */
    compositor_zero();
    compositor.width = 2560;
    compositor.height = 1440;
    compositor.scale = 100;
    {
        struct battery_state state;
        int present;

        memory_zero(&state, sizeof(state));
        present = battery_read((string_address)0, &state) && state.present;
        taskbar_settings_set("bottom", "primary", "");
        {
            struct taskbar_view view = compositor_taskbar_view();
            int ax, ay, aw, ah, clx, cly, clw, clh;

            check("taskbar: auto draws a battery cell exactly when the machine has a battery to read",
                  taskbar_slot(&view, -4, &ax, &ay, &aw, &ah) == present);
            if (present)
                check("taskbar: an auto battery's cell is just left of the clock",
                      taskbar_slot(&view, -3, &clx, &cly, &clw, &clh) && ax + aw == clx && ay == cly && ah == clh);
        }
    }
    taskbar_settings_set("bottom", "primary", "battery nosuchbattery\n");
    {
        struct taskbar_view view = compositor_taskbar_view();
        int bx, by, bw, bh;

        check("taskbar: a battery the machine does not have is not drawn", !taskbar_slot(&view, -4, &bx, &by, &bw, &bh));
    }
    taskbar_settings_set("bottom", "primary", "battery none\n");
    {
        struct taskbar_view view = compositor_taskbar_view();
        int bx, by, bw, bh;

        check("taskbar: the battery set to none is not drawn", !taskbar_slot(&view, -4, &bx, &by, &bw, &bh));
    }
}

/* A language that reads from the right turns the compositor's chrome over: a window's close button is at its top left and the minimise
   button on its inside, and a bottom band is turned over its length: the first task to the moon's left and the second to its right, the
   third further out on the left, and the tray (here the battery) outermost at the left. Each box is the mirror, in the window or the band, of the one a language that reads from the left
   has, and a press at the mirrored place is on the same thing. */
static void check_rtl_chrome(void)
{
    struct compositor_frame frame;
    struct taskbar_view view;
    static const unsigned char lengths[3] = {6, 9, 12};
    int bx = 0, by = 0, side = 0, mx = 0, my = 0, mside = 0, left[8][4], mirrored[8][4], slots[5] = {TASKBAR_LAUNCHER, 0, 1, 2, -2}, taken = 0;
    int band_x, band_y, band_w, band_h;
    b32 turned = true, hit = true, order;

    language_directory = "languages/";
    memory_zero(&frame, sizeof(frame));
    // The window has a tab, so it has a minimise button (compositor_frame_of: none for WINDOW_NO_TAB).
    frame.x = 100, frame.y = 50, frame.width = 800, frame.height = 600, frame.scale = 100, frame.close = 1, frame.minimize = 1;
    language_take(0);
    compositor_close_box(&frame, &bx, &by, &side);
    compositor_minimize_box(&frame, &mx, &my, &mside);
    check("rtl chrome: reading from the left the close button is at the window's top right and the minimise button to its left",
          side > 0 && bx + side < frame.x + frame.width && bx > frame.x + frame.width / 2 && mside == side && mx < bx);
    language_take('a' | 'r' << 8);
    {
        int rx = 0, ry = 0, rside = 0, rmx = 0, rmy = 0, rmside = 0;

        compositor_close_box(&frame, &rx, &ry, &rside);
        compositor_minimize_box(&frame, &rmx, &rmy, &rmside);
        check("rtl chrome: reading from the right they are the mirror of those in the window: the close button at its top left, the minimise button to its right",
              rside == side && rmside == mside && ry == by && rmy == my && rx == 2 * frame.x + frame.width - bx - side &&
                  rmx == 2 * frame.x + frame.width - mx - mside && rx < frame.x + frame.width / 2 && rmx > rx);
    }
    language_take(0);

    compositor_zero();
    compositor.width = 2560, compositor.height = 1440, compositor.scale = 100;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_launcher_set(1);
    taskbar.battery_item = 1, taskbar.battery_percent = 50;
    for (int mode = 0; mode < 2; mode++)
    {
        language_take(mode ? ('a' | 'r' << 8) : 0);
        view = compositor_taskbar_view();
        view.count = 3, view.lengths = lengths;
        taskbar_band(&view, &band_x, &band_y, &band_w, &band_h);
        for (int at = 0; at < 5; at++)
        {
            int x, y, w, h;

            if (!taskbar_slot(&view, slots[at], &x, &y, &w, &h))
                x = y = w = h = -1;
            (mode ? mirrored : left)[at][0] = x, (mode ? mirrored : left)[at][1] = y, (mode ? mirrored : left)[at][2] = w, (mode ? mirrored : left)[at][3] = h;
            if (mode && w > 0)
                hit &= taskbar_hit_reach(&view, x + w / 2, y + h / 2, 0) == slots[at];
            taken += mode && w > 0;
        }
    }
    language_take(0);
    for (int at = 0; at < 5; at++)
        turned &= left[at][2] > 0 && mirrored[at][2] == left[at][2] && mirrored[at][1] == left[at][1] && mirrored[at][3] == left[at][3] &&
                  mirrored[at][0] == 2 * band_x + band_w - left[at][0] - left[at][2];
    // Reading from the left: the second task, the moon, the first, the third, the tray; from the right the same turned over.
    order = left[2][0] < left[0][0] && left[0][0] < left[1][0] && left[1][0] < left[3][0] && left[3][0] < left[4][0] &&
            mirrored[2][0] > mirrored[0][0] && mirrored[0][0] > mirrored[1][0] && mirrored[1][0] > mirrored[3][0] && mirrored[3][0] > mirrored[4][0];
    taskbar.battery_item = 0;
    check("rtl chrome: a bottom band's moon, three tasks and tray are each the mirror in the band of what they are reading from the left", turned && taken == 5);
    check("rtl chrome: the split is turned over: the first task to the moon's left, the second to its right, the third further out, the tray outermost at the left", order);
    check("rtl chrome: a press at the middle of each is on what it is", hit);
    taskbar_launcher_set(0);
}

/* The band's titles written with a face: three tabs (the one with the keys, one, one put away, the last a title too
   long for its tab) on a 1920 by 1080 output, over a glass that is a ramp of the output whatever rectangle is asked
   for, the tabs' slots alone redone, so the clock and the minute are not in the picture. */
#define BAND_W 1920
#define BAND_H 1080
static unsigned int band_pixels[BAND_W * COMPOSITOR_PANEL], band_glass_pixels[(BAND_W + 64) * (COMPOSITOR_PANEL + 64)];
static font_engine band_engine;
static p8 band_atlas[128 * 128];
static font_face band_face;
static const char *const band_titles[3] = {"shell", "Settings \xe2\x80\x94 T\xc3\xa4st", "AVATAR To Wa: a title too long"};
static const unsigned char band_marks[3] = {COMPOSITOR_TAB_LIT, 0, COMPOSITOR_TAB_AWAY};

static int band_glass(void *context, int x, int y, int width, int height)
{
    (void)context;
    for (int row = 0; row < height; row++)
        for (int column = 0; column < width; column++)
            band_glass_pixels[row * width + column] = (unsigned int)((x + column) * 0x000103 + (y + row) * 0x050201) & 0xffffffu;
    return width;
}

/* The view of a band at a position with the three tabs, its pixels cleared: titles, else the three above, which its
   tabs fit. */
// The tabs' titles, app_ids, icons and sequences a check's view points at (the compositor's are its rows'): zeroed, as many as a check lists.
#define CHECK_TABS 512
static const char *check_tab_titles[CHECK_TABS], *check_tab_apps[CHECK_TABS];
static const unsigned int *check_tab_icons[CHECK_TABS];
static unsigned int check_tab_seqs[CHECK_TABS];

static void check_view_tabs(struct taskbar_view *view)
{
    memory_zero(check_tab_titles, sizeof(check_tab_titles)), memory_zero(check_tab_apps, sizeof(check_tab_apps));
    memory_zero(check_tab_icons, sizeof(check_tab_icons)), memory_zero(check_tab_seqs, sizeof(check_tab_seqs));
    view->titles = check_tab_titles, view->apps = check_tab_apps, view->icons = check_tab_icons, view->icon_seqs = check_tab_seqs;
}

static struct taskbar_view band_view(unsigned int position, const char *const *titles)
{
    struct taskbar_view view;
    static unsigned char lengths[3];

    titles = titles ? titles : band_titles;

    memory_zero(&view, sizeof(view));
    check_view_tabs(&view);
    memory_zero(band_pixels, sizeof(band_pixels));
    // A band drawn for its bytes is drawn settled: no highlight still gliding from a band an earlier row drew on the real clock.
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    view.width = BAND_W;
    view.height = BAND_H;
    view.scale = 100;
    view.position = position;
    view.pixels = band_pixels;
    view.band_bytes = sizeof(band_pixels);
    view.glass = band_glass_pixels;
    view.face = &band_face;
    view.engine = &band_engine;
    view.count = 3;
    view.marks = band_marks;
    view.lengths = lengths;
    for (unsigned int at = 0; at < 3; at++)
        view.titles[at] = titles[at], lengths[at] = compositor_title_length(titles[at], WINDOWS_TITLE);
    return view;
}

/* A band at position with its tabs fitted to titles, drawn into into: written with them, or with none for bare (the
   glass alone, the tabs the same). */
static void band_draw(unsigned int position, const char *const *titles, int bare, unsigned int *into)
{
    struct taskbar_view view = band_view(position, titles);
    int rect[4];

    for (unsigned int at = 0; bare && at < 3; at++)
        view.titles[at] = "";
    taskbar_draw(&view, 7, band_glass, null, rect);
    memory_copy(into, band_pixels, sizeof(band_pixels));
}

static p64 band_hash(p64 hash)
{
    for (unsigned int at = 0; at < sizeof(band_pixels) / sizeof(band_pixels[0]); at++)
        for (unsigned int shift = 0; shift < 32; shift += 8)
            hash = (hash ^ (band_pixels[at] >> shift & 255)) * 0x100000001b3ull;
    return hash;
}

/*
        The taskbar slides away while a window is fullscreen, when `taskbar fullscreen on` says so, and comes back when none is.
        Off, which is the default, the band stays where it is over a fullscreen window and nothing is damaged. On, the band's place
        moves off its edge over UI_WINDOW_NS on the kit's ease: half way through it is part of the way (a slide that jumps is a red
        row), at its end it is off the output and takes no press where it was, and each step damages the band where it was and where
        it goes. A slide still going is damaged after each present, so the loop does not go idle half way, and one that has ended is
        not. A fullscreen window that is put away (minimised) does not count, and the band comes back when none is fullscreen.
*/
static void fullscreen_band(int *rect)
{
    struct taskbar_view view = compositor_taskbar_view();

    taskbar_band(&view, &rect[0], &rect[1], &rect[2], &rect[3]);
}

// Whether a band's rectangle is off the output on its own edge: nothing of it is on the screen.
static int fullscreen_off(const int *rect)
{
    switch (taskbar_position())
    {
    case TASKBAR_TOP:
        return rect[1] + rect[3] <= 0;
    case TASKBAR_LEFT:
        return rect[0] + rect[2] <= 0;
    case TASKBAR_RIGHT:
        return rect[0] >= compositor.width;
    default:
        return rect[1] >= compositor.height;
    }
}

// Whether one damaged rectangle holds all of rect's part on the output (damage is cut to the output), so a step that damages
// the band's old and new places has damaged both. A rectangle with none of it on the output has nothing to damage.
static int fullscreen_covers(const int *rect)
{
    int x1 = rect[0] < 0 ? 0 : rect[0], y1 = rect[1] < 0 ? 0 : rect[1];
    int x2 = rect[0] + rect[2] > compositor.width ? compositor.width : rect[0] + rect[2];
    int y2 = rect[1] + rect[3] > compositor.height ? compositor.height : rect[1] + rect[3];

    if (x2 <= x1 || y2 <= y1)
        return 1;
    for (unsigned int at = 0; at < compositor.damages; at++)
        if (compositor.damage[at][0] <= x1 && compositor.damage[at][1] <= y1 && compositor.damage[at][2] >= x2 && compositor.damage[at][3] >= y2)
            return 1;
    return 0;
}

static void check_taskbar_fullscreen(void)
{
    static const char *const words[4] = {"bottom", "top", "left", "right"};
    static struct windows *table_scratch;
    struct windows *const table_at = windows_scratch_table(&table_scratch);
    const positive half = UI_WINDOW_NS / 2, whole = UI_WINDOW_NS + 1000000ull;
    int off_stays = 1, between = 1, at_end = 1, takes_no_press = 1, damage_old_new = 1, keeps = 1, ends_idle = 1, back = 1,
        minimised_stays = 1;

    for (int at = 0; at < 4; at++)
    {
        struct taskbar_view view;
        int base[4], rect[4];
        // The clock only goes on: each position's steps are later than the last one's (the band's slide is kept between them).
        positive t0 = 1000000000ull + (positive)at * 4000000000ull, t1 = t0 + whole, t2, t3;

        compositor_zero();
        windows_scratch_zero(table_at);
        compositor.table = table_at;
        compositor.width = BAND_W;
        compositor.height = BAND_H;
        compositor.scale = 100;
        taskbar_settings_set(words[at], "primary", "battery none\n");
        taskbar_tray_set("");
        compositor.showing[1] = 1;
        compositor.fullscreen[1] = 1;
        fullscreen_band(base);

        // Off: a fullscreen window leaves the band where it is, through a whole slide's time, and nothing is damaged.
        taskbar_hide_set(0);
        compositor.damages = 0;
        compositor_taskbar_slide(t0);
        compositor_taskbar_slide(t0 + half);
        compositor_taskbar_slide(t0 + whole);
        fullscreen_band(rect);
        off_stays &= memory_compare(rect, base, sizeof(rect)) == 0 && compositor.damages == 0 && taskbar_shift() == 0;

        // On: the band moves. The slide is only begun at the first step; half way it is part of the way and its old and new places
        // are damaged, and a slide still going is damaged after a present.
        taskbar_hide_set(1);
        compositor_taskbar_slide(t1);
        compositor.damages = 0;
        compositor_taskbar_slide(t1 + half);
        between &= taskbar_shift() > 0 && taskbar_shift() < UI_ONE;
        fullscreen_band(rect);
        damage_old_new &= fullscreen_covers(base) && fullscreen_covers(rect);
        compositor.damages = 0;
        compositor_taskbar_keep(t1 + half);
        keeps &= compositor.damages > 0;
        compositor_taskbar_slide(t1 + whole);
        compositor.damages = 0;
        compositor_taskbar_keep(t1 + whole);
        ends_idle &= compositor.damages == 0;

        // At its end the band is off the output, and a press where it was is none.
        view = compositor_taskbar_view();
        fullscreen_band(rect);
        at_end &= taskbar_shift() == UI_ONE && fullscreen_off(rect);
        takes_no_press &= taskbar_hit_reach(&view, base[0] + base[2] / 2, base[1] + base[3] / 2, 0) == TASKBAR_HIT_NONE;

        // Back: fullscreen is over, and the band slides to its place and is there.
        t2 = t1 + 2 * whole;
        compositor.fullscreen[1] = 0;
        compositor_taskbar_slide(t2);
        compositor_taskbar_slide(t2 + half);
        between &= taskbar_shift() > 0 && taskbar_shift() < UI_ONE;
        compositor_taskbar_slide(t2 + whole);
        fullscreen_band(rect);
        back &= taskbar_shift() == 0 && memory_compare(rect, base, sizeof(rect)) == 0;

        // A window put away is not fullscreen: the band stays.
        t3 = t2 + 2 * whole;
        compositor.fullscreen[1] = 1;
        compositor.state[1] = WINDOW_MINIMIZED;
        compositor_taskbar_slide(t3);
        compositor_taskbar_slide(t3 + whole);
        minimised_stays &= taskbar_shift() == 0;
    }
    taskbar_hide_set(0);
    check("taskbar fullscreen: off, a fullscreen window leaves the band where it is, and nothing is damaged", off_stays);
    check("taskbar fullscreen: on, the band is part of the way off its edge half way through the slide, both ways", between);
    check("taskbar fullscreen: on, at the end of the slide the band is off the output and a press where it was is none", at_end && takes_no_press);
    check("taskbar fullscreen: each step damages the band where it was and where it goes", damage_old_new);
    check("taskbar fullscreen: a slide still going is damaged after each present, and one that has ended is not", keeps && ends_idle);
    check("taskbar fullscreen: when none is fullscreen the band comes back to its place", back);
    check("taskbar fullscreen: a fullscreen window that is put away does not hide the band", minimised_stays);
}

// A tab's length, written out again: the padding, the icon (16 logical pixels, or what the tab's height leaves with two above and below),
// the kit's gap between an icon and its words and the title's characters at a width each, or the icon and the padding alone for no title;
// never under the tab's height.
static int tab_oracle(int high, int pad, int scale, int characters)
{
    int icon = 16 * scale / 100 > high - 4 * scale / 100 ? high - 4 * scale / 100 : 16 * scale / 100, wide;

    icon = icon < 1 ? 1 : icon;
    wide = 2 * pad + icon + (characters ? UI_GAP2 * scale / 100 + characters * high * 308 / 1000 : 0);
    return wide < high ? high : wide;
}

// The longest a tab may be: its icon, the gap and eight times the band's thickness.
static int tab_longest(int high, int thick, int scale)
{
    int icon = 16 * scale / 100 > high - 4 * scale / 100 ? high - 4 * scale / 100 : 16 * scale / 100;

    return 8 * thick + (icon < 1 ? 1 : icon) + UI_GAP1 * scale / 100;
}

/*
        The moon: the launcher's button, bare on the band (no glass, no padding), as long and as high as the band is thick, and in the band's
        middle along it in the standard arrangement (the tasks split round it), on every edge and at every scale. It is a slot of its own to
        hit (and across the band's whole thickness, so the screen's last row is the moon), to anchor a popup and to damage; drawn alone it
        writes nothing outside its square, and it is drawn otherwise while the launcher is up.
*/
static unsigned int launcher_band_pixels[BAND_W * 3 * COMPOSITOR_PANEL];

static void check_taskbar_launcher(void)
{
    static const unsigned int positions[4] = {TASKBAR_BOTTOM, TASKBAR_TOP, TASKBAR_LEFT, TASKBAR_RIGHT};
    static const char *const words[4] = {"bottom", "top", "left", "right"};
    static const int scales[4] = {50, 100, 150, 200};
    static const unsigned char lengths[3] = {5, 15, 30};
    int shaped = 1, middle = 1, hits = 1, anchored = 1, damaged = 1, outside = 1, lit_differs = 1;

    compositor_zero();
    compositor.width = BAND_W, compositor.height = BAND_H, compositor.scale = 100;
    taskbar_tray_set("");
    {
        int own = taskbar_bit(TASKBAR_LAUNCHER) != taskbar_bit(-2) && taskbar_bit(-2) != taskbar_bit(TASKBAR_TAB_BITS - 1);

        for (int tab = 0; tab < TASKBAR_TAB_BITS; tab++)
            own &= taskbar_bit(TASKBAR_LAUNCHER) != taskbar_bit(tab);
        check("moon: its redo bit is its own, not the tray's and not a tab's", own);
    }
    for (int at = 0; at < 4; at++)
        for (int each = 0; each < 4; each++)
        {
            struct taskbar_view view;
            int scale = scales[each], vertical = at >= 2, thick = COMPOSITOR_PANEL * taskbar_scaled(scale) / 100, x, y, w, h, bx, by, bw, bh, tx, ty, tw, th;

            memory_zero(&view, sizeof(view));
            view.width = BAND_W, view.height = BAND_H, view.scale = scale, view.position = positions[at], view.count = 3, view.lengths = lengths;
            taskbar_settings_set(words[at], "primary", "battery none\n");
            taskbar_band(&view, &bx, &by, &bw, &bh);
            if (!taskbar_slot(&view, TASKBAR_LAUNCHER, &x, &y, &w, &h))
            {
                shaped = middle = hits = anchored = damaged = 0;
                continue;
            }
            shaped &= w == thick && h == thick && x >= bx && y >= by && x + w <= bx + bw && y + h <= by + bh;
            middle &= vertical ? (y + h / 2) - (by + bh / 2) >= -1 && (y + h / 2) - (by + bh / 2) <= 1
                               : (x + w / 2) - (bx + bw / 2) >= -1 && (x + w / 2) - (bx + bw / 2) <= 1;
            hits &= taskbar_hit_reach(&view, x + w / 2, y + h / 2, 0) == TASKBAR_LAUNCHER && taskbar_hit_reach(&view, x + w / 2, y + h / 2, 4) == TASKBAR_LAUNCHER;
            // The edge of the screen beside the moon is the moon: a press at the band's outermost row or column.
            if (positions[at] == TASKBAR_BOTTOM)
                hits &= taskbar_hit_reach(&view, x + w / 2, by + bh - 1, 0) == TASKBAR_LAUNCHER;
            anchored &= taskbar_anchor(&view, TASKBAR_LAUNCHER, &tx, &ty, &tw, &th) &&
                        (vertical ? tx == bx && tw == bw && ty == y && th == h : tx == x && tw == w && ty == by && th == bh);
            {
                int hurt[1][4] = {{x + w / 2, y + h / 2, x + w / 2 + 1, y + h / 2 + 1}};
                unsigned long redo = taskbar_meets(&view, hurt, 1, 0);

                damaged &= (redo & taskbar_bit(TASKBAR_LAUNCHER)) != 0 && !(redo & taskbar_bit(-2));
            }
            {
                unsigned int shot[2] = {0, 0};
                int rect[4];

                for (int lit = 0; lit < 2; lit++)
                {
                    struct taskbar_view drawn = view;

                    memory_zero(launcher_band_pixels, sizeof(launcher_band_pixels));
                    drawn.pixels = launcher_band_pixels, drawn.band_bytes = sizeof(launcher_band_pixels), drawn.glass = band_glass_pixels;
                    drawn.launcher_lit = (unsigned int)lit;
                    if (taskbar_draw(&drawn, taskbar_bit(TASKBAR_LAUNCHER), band_glass, null, rect) != 1)
                    {
                        outside = 0;
                        continue;
                    }
                    // The rim, a pixel in from the moon's top middle.
                    shot[lit] = launcher_band_pixels[(unsigned long)(y - by + 1) * (unsigned long)bw + (unsigned long)(x - bx + w / 2)];
                    for (int row = 0; row < bh; row++)
                        for (int column = 0; column < bw; column++)
                            if ((column + bx < x || column + bx >= x + w || row + by < y || row + by >= y + h) &&
                                launcher_band_pixels[(unsigned long)row * (unsigned long)bw + (unsigned long)column])
                                outside = 0;
                }
                lit_differs &= shot[0] != shot[1];
            }
        }
    check("moon: it is as long and as high as the band is thick, inside the band, at every position and scale", shaped);
    check("moon: with the tasks split round it, it is in the band's middle along it, to a pixel", middle);
    check("moon: a point on it is the moon, with a finger's reach too, and so is the screen's edge beside it", hits);
    check("moon: its anchor is its length along the edge and the band's whole thickness across it", anchored);
    check("moon: damage over it redoes its slot and not the tray's, on a bit of its own", damaged);
    check("moon: drawn alone it writes nothing outside its square, and its rim is brighter while the launcher is up", outside && lit_differs);
    check("moon: in the shapes the panel draws with no font it is the lit tab's ink when lit and the shadow's when not",
          taskbar_tab_ink(TASKBAR_LAUNCHER, COMPOSITOR_TAB_LIT) == taskbar_tab_ink(0, COMPOSITOR_TAB_LIT) &&
          taskbar_tab_ink(TASKBAR_LAUNCHER, 0) == taskbar_tab_ink(0, 0) && taskbar_tab_ink(-2, COMPOSITOR_TAB_LIT) == taskbar_tab_ink(-2, 0));
    taskbar_settings_set("bottom", "primary", "battery none\n");
}

/*
        The band's arrangement. In the standard one the tasks are split round the moon, a task to its right and the next to its left, each
        further out than the last; the sound and the microphone are a pill at the far left and the notices and the battery one at the far
        right; every pill is inside the band, none meets another, and between two of them is the kit's gap twice. The same holds at three
        output widths with 3 and 8 tasks, and with 30 the tasks are each their icon alone and still clear of the moon and the trays. A point
        in the middle of anything drawn is that thing, and a point between two pills is nothing. The other arrangements: all in the middle
        (the moon, then the tasks, then the tray), the moon at the start and the tasks at the end; and reading from the right, the split
        turned over (the first task to the moon's left).
*/
static int centre_slots(const struct taskbar_view *view, int *rect, int which)
{
    return taskbar_slot(view, which, &rect[0], &rect[1], &rect[2], &rect[3]);
}

static void check_taskbar_centre(void)
{
    static const unsigned char lengths[30] = {5, 15, 30, 8, 12, 20, 6, 10, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};
    static const int widths[3] = {1280, 1920, 3840}, counts[3] = {3, 8, 30};
    b32 alternate = true, trays = true, apart = true, inside = true, bare = true, hits = true, gaps_none = true;

    language_directory = "languages/";
    compositor_zero();
    compositor.width = BAND_W, compositor.height = BAND_H, compositor.scale = 100;
    taskbar_tray_set("");
    taskbar_volume_set(1, 50, 0, 0);
    taskbar_mic_set(1, 0);
    taskbar_notices_set(1, WINDOWS_ISSUE_WARNING);
    for (int wi = 0; wi < 3; wi++)
        for (int ci = 0; ci < 3; ci++)
        {
            struct taskbar_view view;
            int moon[4], band[4], slot[TASKBAR_TAB_BITS][4], before[4], after[4], count = counts[ci], gap = TASKBAR_GAP * 2, pills[64][4], listed = 0, which;

            memory_zero(&view, sizeof(view));
            view.width = widths[wi], view.height = 1080, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = (unsigned int)count, view.lengths = lengths;
            taskbar_settings_set("bottom", "primary", "battery none\n");
            taskbar_band(&view, &band[0], &band[1], &band[2], &band[3]);
            if (!centre_slots(&view, moon, TASKBAR_LAUNCHER) || !centre_slots(&view, before, TASKBAR_TRAY_BEFORE) || !centre_slots(&view, after, TASKBAR_TRAY))
            {
                alternate = trays = false;
                continue;
            }
            for (int at = 0; at < count; at++)
                if (!centre_slots(&view, slot[at], at))
                    alternate = false;
            // A task to the moon's right, the next to its left, each further out than the last on its side.
            for (int at = 0; at < count && alternate; at++)
            {
                int right = at % 2 == 0;

                alternate &= right ? slot[at][0] >= moon[0] + moon[2] : slot[at][0] + slot[at][2] <= moon[0];
                if (at >= 2)
                    alternate &= right ? slot[at][0] >= slot[at - 2][0] + slot[at - 2][2] : slot[at][0] + slot[at][2] <= slot[at - 2][0];
            }
            // The sound and the microphone outermost at the left, the notices and the battery at the right.
            for (int at = 0; at < count; at++)
                trays &= before[0] + before[2] <= slot[at][0] && after[0] >= slot[at][0] + slot[at][2];
            // The pills, in order along the band: inside it, and the kit's gap twice between each and the next.
            while (listed < 64 && taskbar_plate(&view, listed, &which, &pills[listed][0], &pills[listed][1], &pills[listed][2], &pills[listed][3]))
                listed++;
            for (int k = 0; k < listed; k++)
            {
                inside &= pills[k][0] >= band[0] && pills[k][0] + pills[k][2] <= band[0] + band[2] && pills[k][1] >= band[1] && pills[k][1] + pills[k][3] <= band[1] + band[3];
                if (k)
                {
                    apart &= pills[k][0] - (pills[k - 1][0] + pills[k - 1][2]) >= gap;
                    // A point in the middle of the space between two pills is nothing.
                    gaps_none &= taskbar_hit_reach(&view, (pills[k - 1][0] + pills[k - 1][2] + pills[k][0]) / 2, pills[k][1] + pills[k][3] / 2, 0) == TASKBAR_HIT_NONE;
                }
            }
            // A point in the middle of anything drawn is it.
            hits &= taskbar_hit_reach(&view, moon[0] + moon[2] / 2, moon[1] + moon[3] / 2, 0) == TASKBAR_LAUNCHER;
            for (int at = 0; at < count; at++)
                hits &= taskbar_hit_reach(&view, slot[at][0] + slot[at][2] / 2, slot[at][1] + slot[at][3] / 2, 0) == at;
            {
                static const int cells[3] = {TASKBAR_MIC, TASKBAR_VOLUME, TASKBAR_NOTICES};

                for (int k = 0; k < 3; k++)
                {
                    int cell[4];

                    hits &= centre_slots(&view, cell, cells[k]) && taskbar_hit_reach(&view, cell[0] + cell[2] / 2, cell[1] + cell[3] / 2, 0) == cells[k];
                }
            }
            // Thirty: each task its icon alone, its padding either side of it.
            if (count == 30 && widths[wi] <= 1920)
                for (int at = 0; at < count; at++)
                    bare &= slot[at][2] <= 16 + 2 * TASKBAR_PADDING_STANDARD;
        }
    check("taskbar arrangement: split round the moon, the first task to its right and the next to its left, each further out than the last, at three widths and 3, 8 and 30 tasks", alternate);
    check("taskbar arrangement: the sound and the microphone are the outermost pill at the left, the notices and the battery at the right", trays);
    check("taskbar arrangement: every pill is inside the band, the kit's gap twice from the next, and none meets another", inside && apart);
    check("taskbar arrangement: a point in the middle of the moon, a task or a tray cell is it, and a point between two pills is nothing", hits && gaps_none);
    check("taskbar arrangement: thirty tasks are each their icon alone, still clear of the moon and the trays", bare);
    // Two hundred windows on a band of 1920: every one has a tab, the icons that fit a strip with a chevron at each end that has more,
    // the window with the keys always in it, a press on a chevron a page on, the wheel a tab, and none past the band.
    {
        static unsigned char strip_marks[CHECK_TABS], strip_lengths[CHECK_TABS];
        struct taskbar_view view;
        int shown = 0, lit_shown = 0, inside_band = 1, ends_first = 0, ends_later = 0, band[4], cell[4], paged, stepped, back, on;
        static __typeof__(taskbar) kept_taskbar;

        kept_taskbar = taskbar;
        taskbar_arrange_set(TASKBAR_ZONE_CENTRE, TASKBAR_ZONE_START, 0, 0, 1u << 1 | 1u << 2, 0, 0, 60, 0);
        memory_zero(&view, sizeof(view));
        check_view_tabs(&view);
        view.width = 1920, view.height = 1080, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 200, view.lengths = strip_lengths;
        view.marks = strip_marks;
        memory_zero(strip_marks, sizeof(strip_marks));
        for (int at = 0; at < 200; at++)
            strip_lengths[at] = 12, check_tab_titles[at] = "a window";
        strip_marks[150] = COMPOSITOR_TAB_LIT;
        taskbar_strip_from[0] = taskbar_strip_from[1] = 0, taskbar_strip_follow = 1, taskbar_strip_lit[0] = taskbar_strip_lit[1] = -1;
        taskbar_changed();
        taskbar_band(&view, &band[0], &band[1], &band[2], &band[3]);
        for (int at = 0; at < 200; at++)
            if (centre_slots(&view, cell, at))
            {
                shown++, lit_shown |= at == 150;
                inside_band &= cell[0] >= band[0] && cell[0] + cell[2] <= band[0] + band[2];
            }
        back = centre_slots(&view, cell, TASKBAR_STRIP_BACK), on = centre_slots(&view, cell, TASKBAR_STRIP_ON);
        ends_first = back && on;
        check("taskbar strip: 200 windows on a band of 1920 are a strip of the icons it has room for, every one inside the band, a chevron at each end",
              shown > 20 && shown < 200 && inside_band && ends_first);
        check("taskbar strip: the window with the keys is in the strip", lit_shown);
        // A page back from it, and the wheel a tab on: the strip moves and the keys' window may leave it, until the keys move.
        {
            int from = taskbar_strip_from[1];

            paged = taskbar_strip_page(-1) && taskbar_strip_from[1] < from;
            from = taskbar_strip_from[1];
            stepped = taskbar_strip_step(1) && taskbar_strip_from[1] == from + 1;
        }
        // Scrolled to its start: no chevron back, one on.
        taskbar_strip_step(-1000);
        taskbar_changed();
        ends_later = !centre_slots(&view, cell, TASKBAR_STRIP_BACK) && centre_slots(&view, cell, TASKBAR_STRIP_ON) && centre_slots(&view, cell, 0);
        check("taskbar strip: a chevron's page and the wheel's tab move the strip, and at its start it has no chevron back and shows the first window",
              paged && stepped && ends_later);
        // Forty windows fit as icons: no strip.
        view.count = 40;
        taskbar_changed();
        shown = 0;
        for (int at = 0; at < 40; at++)
            shown += centre_slots(&view, cell, at);
        check("taskbar strip: forty windows on a band of 1920 are forty icons, and no chevron", shown == 40 && !centre_slots(&view, cell, TASKBAR_STRIP_BACK) &&
              !centre_slots(&view, cell, TASKBAR_STRIP_ON));
        taskbar = kept_taskbar;
        taskbar_strip_from[0] = taskbar_strip_from[1] = 0, taskbar_strip_follow = 1;
        taskbar_changed();
    }

    // The earlier arrangements, and the split read from the right.
    {
        struct taskbar_view view;
        int moon[4], first[4], second[4], tray[4];
        b32 together, ends, turned;

        memory_zero(&view, sizeof(view));
        view.width = 1920, view.height = 1080, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 3, view.lengths = lengths;
        taskbar.tasks_zone = TASKBAR_ZONE_CENTRE, taskbar.tray_before = 0;
        together = centre_slots(&view, moon, TASKBAR_LAUNCHER) && centre_slots(&view, first, 0) && centre_slots(&view, second, 2) && centre_slots(&view, tray, TASKBAR_TRAY) &&
                   moon[0] + moon[2] <= first[0] && first[0] < second[0] && second[0] + second[2] <= tray[0];
        taskbar.moon_zone = TASKBAR_ZONE_START, taskbar.tasks_zone = TASKBAR_ZONE_END;
        ends = centre_slots(&view, moon, TASKBAR_LAUNCHER) && centre_slots(&view, tray, TASKBAR_TRAY) && moon[0] < 40 && tray[0] + tray[2] > 1920 - 40;
        taskbar.moon_zone = TASKBAR_ZONE_CENTRE, taskbar.tasks_zone = TASKBAR_ZONE_SPLIT, taskbar.tray_before = 1u << 1 | 1u << 2;
        language_take('a' | 'r' << 8);
        turned = centre_slots(&view, moon, TASKBAR_LAUNCHER) && centre_slots(&view, first, 0) && first[0] + first[2] <= moon[0];
        language_take(0);
        check("taskbar arrangement: all in the middle is the moon, then the tasks, then the tray; the moon at the start and the tasks at the end are at the band's ends; read from the right the split is turned over",
              together && ends && turned);
    }
    taskbar_volume_set(0, 0, 0, 0);
    taskbar_mic_set(0, 0);
    taskbar_notices_set(0, 0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
}

/*
        `taskbar tasks grouped separate`: each task a plate of its own, the kit's gap twice between them, split about the moon by turns as
        grouped ones are, a press on each its own and the highlight still moving from one to the next. `taskbar edge`: the band in from the
        screen's edge by as many pixels at its scale, out past it by fewer than 0 but no further than shows TASKBAR_EDGE_KEEP of a pill,
        and what a press finds and what the launcher opens beside it go with it.
*/
static void check_bar_options(void)
{
    static unsigned char lengths[4] = {5, 15, 30, 8};
    struct taskbar_view view;
    int bx, by, bw, bh, x[4][4], which, px, py, pw, ph, plates = 0, gap = TASKBAR_GAP;
    b32 apart = 1, split, pressed = 1, edges = 1, held, followed = 1;

    compositor_zero();
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    memory_zero(&view, sizeof(view));
    view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 4, view.lengths = lengths;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_standard();
    taskbar_bar_set(1, 0);
    for (int at = 0; at < 4; at++)
        taskbar_slot(&view, at, &x[at][0], &x[at][1], &x[at][2], &x[at][3]);
    for (int at = 0; taskbar_plate(&view, at, &which, &px, &py, &pw, &ph); at++)
        plates += which >= 0;
    // Task 0 right of the moon, 1 left, 2 right of 0, 3 left of 1: each pair on a side the gap twice apart.
    apart = plates == 4 && x[2][0] - (x[0][0] + x[0][2]) == 2 * gap && x[1][0] - (x[3][0] + x[3][2]) == 2 * gap;
    split = x[0][0] > BAND_W / 2 && x[1][0] < BAND_W / 2;
    for (int at = 0; at < 4; at++)
        pressed &= taskbar_hit_reach(&view, x[at][0] + x[at][2] / 2, x[at][1] + x[at][3] / 2, 0) == at &&
                   taskbar_hit_reach(&view, x[at][0] + x[at][2] + gap, x[at][1] + x[at][3] / 2, 0) != at;
    taskbar_bar_set(0, 0);
    taskbar_band(&view, &bx, &by, &bw, &bh);
    for (int each = 0; each < 4; each++)
    {
        static const int asked[4] = {16, 32, -12, 8};
        int ex, ey, ew, eh, mx, my, mw, mh;

        taskbar_bar_set(0, asked[each]);
        taskbar_band(&view, &ex, &ey, &ew, &eh);
        edges &= ey == by - asked[each] && eh == bh;
        followed &= taskbar_anchor(&view, TASKBAR_LAUNCHER, &mx, &my, &mw, &mh) && my == ey &&
                    taskbar_hit_reach(&view, mx + mw / 2, ey + eh / 2, 0) == TASKBAR_LAUNCHER;
    }
    // Past the edge, no further than shows TASKBAR_EDGE_KEEP of a pill.
    taskbar_bar_set(0, TASKBAR_EDGE_LEAST);
    {
        int ex, ey, ew, eh, inset = taskbar_inset(100), high = bh - 2 * inset;

        taskbar_band(&view, &ex, &ey, &ew, &eh);
        held = ey > by && ey - by <= high * (100 - TASKBAR_EDGE_KEEP) / 100 + inset && BAND_H - (ey + inset) >= high * TASKBAR_EDGE_KEEP / 100;
    }
    taskbar_bar_set(0, 0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
    check("taskbar tasks separate: each task a plate of its own, the kit's gap twice between them", apart);
    check("taskbar tasks separate: still split about the moon by turns", split);
    check("taskbar tasks separate: a press finds each task, and none in the gap after it", pressed);
    check("taskbar edge: the band in from the screen's edge by the pixels asked, or out past it", edges);
    check("taskbar edge: out, held to show the share of a pill that keeps it usable", held);
    check("taskbar edge: the moon's press and the launcher's anchor go with it", followed);
}

/*
        The wallpaper as the ground: once the rows are read, `canvas wallpaper moon` makes it for the space, and what the ground was
        filled with is copied from it (a rectangle, a pixel under a blended picture); made again only when a row, the theme or the space
        changes, the whole space damaged then and nothing at a present after; `plain` lets it go and the ground is the colour again.
*/
static void check_wallpaper_ground(void)
{
    static unsigned int target_pixels[320 * 200];
    static struct windows *table_scratch;
    struct windows *const table_at = windows_scratch_table(&table_scratch);
    struct compositor_target t;
    unsigned int damages;
    b32 made, copied, kept, plain, glow;

    compositor_zero();
    windows_scratch_zero(table_at);
    table_at->themed = 1, table_at->theme = UI_THEME_DARK, table_at->accent = 0x5b7fa6u;
    compositor.table = table_at, compositor.width = 320, compositor.height = 200;
    // With no file of any row (a live boot), the rows read are the standards: the moon, dimmed, 45 percent seas, the glow.
    compositor_wall_rows.moon = 0, compositor_wall_rows.ready = 0;
    {
        bipolar any = system_open_at(AT_FDCWD, (string_address)SETTING_WALLPAPER_PATH, O_RDONLY | O_CLOEXEC);

        if (any >= 0)
            system_close(any);
        compositor_wall_read();
        check("wallpaper: with no file of its rows the ground is the moon, dimmed, its seas 45 percent, with the glow",
              any >= 0 || (compositor_wall_rows.ready && compositor_wall_rows.moon && compositor_wall_rows.dimmed && compositor_wall_rows.maria == 45 &&
                           compositor_wall_rows.glow == 1));
    }
    compositor_wall_rows.moon = 1, compositor_wall_rows.dimmed = 1, compositor_wall_rows.maria = 45, compositor_wall_rows.glow = 1, compositor_wall_rows.ready = 1;
    compositor_wall_keep();
    // Made on its thread: nothing shown till it is done, then taken.
    made = !compositor_wall && compositor_wall_making();
    compositor_wall_finish();
    made &= !compositor_wall_making() && compositor_wall && compositor_wall_w == 320 && compositor_wall_h == 200 && compositor.damages > 0;
    made &= compositor_wall[100 * 320 + 160] != compositor_wall[5 * 320 + 5];
    memory_zero(&t, sizeof(t));
    t.pixels = target_pixels, t.pitch = 320, t.width = 320, t.height = 200, t.x1 = 0, t.y1 = 0, t.x2 = 320, t.y2 = 200;
    compositor_ground_fill(&t, 40, 30, 200, 150);
    copied = target_pixels[100 * 320 + 160] == compositor_wall[100 * 320 + 160] && target_pixels[30 * 320 + 40] == compositor_wall[30 * 320 + 40] &&
             compositor_ground_at(10, 10) == compositor_wall[10 * 320 + 10];
    compositor.damages = 0;
    damages = compositor.damages;
    compositor_wall_keep();
    kept = compositor.damages == damages;
    // The glow along the edges too: another wallpaper.
    {
        unsigned int before = compositor_wall[2 * 320 + 160];

        compositor_wall_rows.glow = 2;
        compositor_wall_keep();
        compositor_wall_finish();
        glow = compositor_wall && compositor.damages > 0 && compositor_wall[2 * 320 + 160] != before;
    }
    compositor_wall_rows.moon = 0;
    compositor_wall_keep();
    compositor_ground_fill(&t, 40, 30, 200, 150);
    plain = !compositor_wall && target_pixels[100 * 320 + 160] == compositor_ground && compositor_ground_at(10, 10) == compositor_ground;
    compositor_wall_rows.moon = 1, compositor_wall_rows.glow = 1, compositor_wall_rows.ready = 0;
    compositor_zero();
    check("wallpaper: once the rows are read the moon is made for the space and the space damaged", made);
    check("wallpaper: the ground is copied from it, a rectangle and a pixel alike", copied);
    check("wallpaper: kept: a present after makes nothing and damages nothing", kept);
    check("wallpaper: a row changed makes it again (the glow along the edges)", glow);
    check("wallpaper: plain lets it go, the ground the colour again", plain);
}

/*
        The moon's motion. It grows about its middle while the pointer is over it, over the kit's ease, its plate with it, and back when the
        pointer leaves; what a press finds stays the disc at rest. With reduced motion it is grown at once. It turns a step at a time on the
        real clock while a hand has been about, the loop woken for the next step and no sooner, and rests two minutes after the last. And the
        damage it makes of itself meets the moon alone, so no other plate of the band is made again for it.
*/
static void check_moon_motion(void)
{
    static unsigned char lengths[3] = {5, 15, 30};
    struct taskbar_view view;
    int sx, sy, sw, sh, which, px, py, pw, ph, damage[1][4];
    positive now = 5000000000000ull;
    b32 rest, half, full, hit, back, reduced, turns, due, idle, own, glow_rest, inside = 1;

    compositor_zero();
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    memory_zero(&view, sizeof(view));
    view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 3, view.lengths = lengths;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_standard();
    taskbar.glides = 1, taskbar.moon_seen = 0, taskbar.moon_step = 0;
    taskbar_spin_set(0); // the growth alone first, no turn
    ui_kit.test_clock = 1000000000ll;
    taskbar_slot(&view, TASKBAR_LAUNCHER, &sx, &sy, &sw, &sh);
    taskbar_moon_hover(-1);
    taskbar_moon_tick(now, 0);
    rest = taskbar_moon_share() == 0 && taskbar_moon_more(&view) == 0;
    glow_rest = taskbar_moon_glow_level() == (b64)UI_ONE * TASKBAR_MOON_REST_GLOW / 100;
    // The pointer over it: half way through, between; after, grown by the whole share and its plate with it.
    taskbar_moon_hover(0);
    taskbar_moon_tick(now, 0);
    ui_kit.test_clock += (b64)ui_paced(TASKBAR_MOON_GROW_MS * UI_MS) / 2;
    half = taskbar_moon_tick(now, 0) && taskbar_moon_share() > 0 && taskbar_moon_share() < UI_ONE;
    ui_kit.test_clock += 1000000000ll;
    taskbar_moon_tick(now, 0);
    full = taskbar_moon_share() == UI_ONE && taskbar_moon_more(&view) == (sw * TASKBAR_MOON_GROW + 199) / 200;
    {
        int found = 0;

        for (int at = 0; taskbar_plate(&view, at, &which, &px, &py, &pw, &ph); at++)
            if (which == TASKBAR_LAUNCHER)
                found = pw == sw + 2 * taskbar_moon_more(&view) && px == sx - taskbar_moon_more(&view);
        full &= found;
    }
    // Grown whole, at three scales, no plate of the band is past the band: its pixels are the band's, and past it there are none.
    {
        static unsigned int band_pixels[4096 * 400];
        static struct compositor_frame tabs[64];
        int scales[3] = {100, 150, 300};

        for (int k = 0; k < 3; k++)
        {
            struct compositor_frame band;
            int ax, ay, aw, ah;
            unsigned int count;

            memory_zero(&band, sizeof(band));
            view.scale = scales[k];
            if (!taskbar_anchor(&view, TASKBAR_LAUNCHER, &ax, &ay, &aw, &ah) || view.width > 4096 || ah > 400)
            {
                inside = 0;
                continue;
            }
            band.x = 0, band.y = ay, band.width = view.width, band.height = ah, band.pixels = band_pixels, band.pitch = (unsigned long)view.width;
            count = compositor_panel_tabs(&band, tabs);
            for (unsigned int at = 0; at < count; at++)
                inside &= tabs[at].x >= band.x && tabs[at].y >= band.y && tabs[at].x + tabs[at].width <= band.x + band.width &&
                          tabs[at].y + tabs[at].height <= band.y + band.height;
        }
        view.scale = 100;
    }
    // A press just past the disc at rest, inside the grown one, is not the moon's.
    hit = taskbar_hit_reach(&view, sx - 1, sy + sh / 2, 0) != TASKBAR_LAUNCHER && taskbar_hit_reach(&view, sx + sw / 2, sy + sh / 2, 0) == TASKBAR_LAUNCHER;
    taskbar_moon_hover(-1);
    taskbar_moon_tick(now, 0);
    ui_kit.test_clock += 1000000000ll;
    taskbar_moon_tick(now, 0);
    back = taskbar_moon_share() == 0 && !taskbar_moon_tick(now, 0);
    // The launcher up grows it as the pointer does; with reduced motion at once.
    taskbar.glides = 0;
    taskbar_moon_tick(now, 1);
    reduced = taskbar_moon_share() == UI_ONE;
    taskbar_moon_tick(now, 0);
    reduced &= taskbar_moon_share() == 0;
    taskbar.glides = 1;
    taskbar_spin_set(TASKBAR_MOON_SPIN);
    // The turn: a hand seen, a step taken, the next due within a step; two minutes on, at rest and not due.
    taskbar_moon_seen(now);
    taskbar.moon_step = 0;
    turns = taskbar_moon_tick(now, 0) && taskbar.moon_step != 0 && taskbar.moon_turn >= 0 && taskbar.moon_turn < UI_ONE;
    due = taskbar_moon_due(now) > now && taskbar_moon_due(now) <= now + 1000000000ull / TASKBAR_MOON_STEPS + 1000000ull;
    idle = !taskbar_moon_due(now + TASKBAR_MOON_IDLE + 1) && taskbar_moon_due(now + TASKBAR_MOON_IDLE - 1000000000ull);
    taskbar_spin_set(0);
    idle &= !taskbar_moon_due(now);
    taskbar_spin_set(TASKBAR_MOON_SPIN);
    // Its own damage meets the moon and nothing else; the same rectangle from under the band meets the tasks beside it too.
    damage[0][0] = sx - 2, damage[0][1] = sy - 2, damage[0][2] = sx + sw + 2, damage[0][3] = sy + sh + 2;
    taskbar_moon_damaged(0, damage[0]);
    own = taskbar_meets(&view, damage, 1, 0) == taskbar_bit(TASKBAR_LAUNCHER);
    // Solid (a window under the band): the moon's own damage is the moon's alone, and the same rectangle from under the band is nothing's.
    int solid_own = taskbar_meets(&view, damage, 1, 1) == taskbar_bit(TASKBAR_LAUNCHER);
    taskbar_bands[0].moon_own = 0;
    own &= taskbar_meets(&view, damage, 1, 0) != taskbar_bit(TASKBAR_LAUNCHER);
    solid_own &= taskbar_meets(&view, damage, 1, 1) == 0;
    check("moon: on a solid band its own damage redoes the moon and a window's damage under the band redoes nothing", solid_own);
    ui_kit.test_clock = 0;
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    taskbar_moon_hover(-1);
    check("moon: at rest it is its square, its plate the same", rest);
    check("moon: at rest a touch of its glow is there, TASKBAR_MOON_REST_GLOW percent of the hover's", glow_rest);
    check("moon: grown whole at 100, 150 and 300 percent, every plate of the band is inside the band (its pixels are the band's)", inside);
    check("moon: the pointer over it grows it over the kit's ease, half way between", half);
    check("moon: grown by its share, its plate as far round it as the disc", full);
    check("moon: a press finds the disc at rest, not the grown one", hit);
    check("moon: it settles back when the pointer leaves, and asks for nothing then", back);
    check("moon: the launcher up grows it too, with reduced motion at once", reduced);
    check("moon: it turns a step at a time on the real clock while a hand is about", turns);
    check("moon: the loop is woken for the next step and no sooner", due);
    check("moon: two minutes after the last hand, or with no spin, it rests and wakes nothing", idle);
    check("moon: what it damages of itself meets the moon alone, not the tasks beside it", own);
}

/*
        The wallpaper's moon turning: with the motion on and a hand about, the clock's next step makes a step on the wallpaper's thread, taken
        with each moon's square alone its damage and the rest of the picture as it was; with the motion off (reduced, or none) nothing is
        made and the loop is not woken for it.
*/
static void check_wallpaper_turn(void)
{
    static struct windows *table_scratch;
    struct windows *const table_at = windows_scratch_table(&table_scratch);
    b32 stepped, square, rest, quiet;
    unsigned int before_corner;
    positive now = clock_monotonic_nanoseconds();

    compositor_zero();
    windows_scratch_zero(table_at);
    table_at->themed = 1, table_at->theme = UI_THEME_DARK, table_at->accent = 0x5b7fa6u;
    compositor.table = table_at, compositor.width = 320, compositor.height = 200;
    compositor_wall_rows.moon = 1, compositor_wall_rows.dimmed = 1, compositor_wall_rows.maria = 45, compositor_wall_rows.glow = 1, compositor_wall_rows.ready = 1;
    taskbar.glides = 1, taskbar.moon_at = 0;
    taskbar_moon_seen(now);
    compositor_wall_keep();
    compositor_wall_finish();
    before_corner = compositor_wall ? compositor_wall[2 * 320 + 2] : 0;
    // The clock a step on: a step made, its damage the moon's square alone, outside it the same pixels.
    compositor_wall_stepped -= 1;
    compositor.damages = 0;
    compositor_wall_keep();
    stepped = compositor_wall_job.pixels != null && compositor_wall_job.step;
    compositor_wall_finish();
    square = compositor.damages == 1 && compositor.damage[0][1] > 0 && compositor_wall && compositor_wall[2 * 320 + 2] == before_corner;
    stepped &= compositor_wall_turn == compositor_wall_turn_of(compositor_wall_step_now());
    // The motion off: nothing made, nothing due.
    taskbar.glides = 0;
    compositor_wall_stepped -= 1;
    compositor.damages = 0;
    compositor_wall_keep();
    rest = compositor_wall_job.pixels == null && compositor.damages == 0;
    quiet = compositor_wall_due(now) == 0;
    taskbar.glides = 1;
    compositor_wall_rows.moon = 0;
    compositor_wall_keep();
    compositor_wall_rows.moon = 1, compositor_wall_rows.ready = 0;
    compositor_zero();
    check("wallpaper turn: with the motion on and a hand about, the clock's next step turns the moon", stepped);
    check("wallpaper turn: a step damages the moon's square alone, the rest of the picture as it was", square);
    check("wallpaper turn: with the motion off nothing is made", rest);
    check("wallpaper turn: and the loop is not woken for it", quiet);
}

/*
        The band's arrangement, opened from the moon's menu: the moon ringed with the rest; an item pressed and let go elsewhere moves
        there from the machine's own device and not from a virtual one (WINDOW_EVENT_VIRTUAL: uinput, uhid, the on-screen keyboard, the
        headless pipe), which is counted and moves nothing; and Done keeps the arrangement through the `taskbar` rows' files and words (in
        a directory of the check's own), which the rows read back as the arrangement it was, and the band leaves its arrangement.
*/
static void check_taskbar_edit(void)
{
    static unsigned char lengths[3] = {5, 15, 30};
    static char root[64], path[160], word[32];
    struct taskbar_view view;
    int sx, sy, sw, sh, zx, zy, zw, zh, moon_before, tasks_before;
    b32 menu = 1, virtual_refused, moved, kept, read_back, left, dropped_after_done;

    compositor_zero();
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    memory_zero(&view, sizeof(view));
    view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 3, view.lengths = lengths;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_standard();
    // The menu's words: the clock's other format, as `time format` takes it.
    menu &= string_equals((string_address)taskbar_clock_other(), (string_address)(taskbar.clock_12h ? "24h" : "12h"));
    taskbar_edit_set(1);
    taskbar_slot(&view, TASKBAR_LAUNCHER, &sx, &sy, &sw, &sh);
    {
        const struct ui_node address_to start = taskbar_node(&view, TASKBAR_ZONE - TASKBAR_ZONE_START);

        zx = start ? start->x : 0, zy = start ? start->y : 0, zw = start ? start->w : 0, zh = start ? start->h : 0;
    }
    menu &= taskbar_edit_item(&view, sx + sw / 2, sy + sh / 2) == TASKBAR_LAUNCHER && zw > 0;
    // A virtual device: the press and its release are refused and counted, and the moon stays in the middle.
    compositor_edit_press(&view, 0, sx + sw / 2, sy + sh / 2, 0);
    compositor_edit_release(&view, zx + 2, zy + zh / 2, 0);
    virtual_refused = taskbar.moon_zone == TASKBAR_ZONE_CENTRE && !compositor_edit_drag.held;
    // The machine's own: the moon pressed and let go at the band's start is there, the tasks no longer split round it.
    compositor_edit_press(&view, 0, sx + sw / 2, sy + sh / 2, 1);
    compositor_edit_release(&view, zx + 2, zy + zh / 2, 1);
    moved = taskbar.moon_zone == TASKBAR_ZONE_START && taskbar.tasks_zone != TASKBAR_ZONE_SPLIT;
    // Done, into the check's own directory of rows: each row's file holds its word, and the row reads it as the place it is.
    string_copy((string_address)root, (string_address) "/tmp/moonwater-check-edit-");
    positive_into_string((p8 *)root + string_length((string_address)root), (positive)system_call(syscall(getpid)));
    system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)root, 0700);
    string_copy((string_address)path, (string_address)root), string_copy((string_address)path + string_length((string_address)path), (string_address) "/root");
    system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
    compositor_settings_root = root;
    kept = compositor_edit_done();
    left = !taskbar.editing;
    // A press that Done ends holds nothing after it: the arrangement reopened, its release (the press's own device) drops nothing.
    taskbar_edit_set(1);
    compositor_edit_press(&view, 0, sx + sw / 2, sy + sh / 2, 1);
    compositor_edit_done();
    taskbar_edit_set(1);
    moon_before = taskbar.moon_zone, tasks_before = taskbar.tasks_zone;
    compositor_edit_release(&view, zx + 2, zy + zh / 2, 1);
    dropped_after_done = !compositor_edit_drag.held && taskbar.moon_zone == moon_before && taskbar.tasks_zone == tasks_before;
    taskbar_edit_set(0);
    read_back = 1;
    {
        static const char *const paths[2] = {SETTING_TASKBAR_MOON_PATH, SETTING_TASKBAR_TASKS_PATH};
        static const char *const want[2] = {"start", "center"};

        for (int at = 0; at < 2; at++)
        {
            bipolar got;
            long value = -1;

            string_copy((string_address)path, (string_address)root), string_copy((string_address)path + string_length((string_address)path), (string_address)paths[at]);
            got = file_slurp_regular_at(AT_FDCWD, (string_address)path, (p8 *)word, sizeof(word) - 1, 0);
            word[got > 0 ? got : 0] = 0;
            if (got > 0 && word[got - 1] == '\n')
                word[got - 1] = 0;
            read_back &= got > 0 && string_equals((string_address)word, (string_address)want[at]) && compositor_setting_kept(paths[at], word, &value);
            if (at == 0)
                read_back &= value == TASKBAR_ZONE_START;
            system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        }
        // The tray items' rows too, each its side.
        for (int at = 0; at < 6; at++)
        {
            static const char *const items[6] = {SETTING_TASKBAR_ITEM_NOTICES_PATH, SETTING_TASKBAR_ITEM_MIC_PATH, SETTING_TASKBAR_ITEM_SOUND_PATH,
                                                 SETTING_TASKBAR_ITEM_BATTERY_PATH, SETTING_TASKBAR_ITEM_TEXT_PATH, SETTING_TASKBAR_ITEM_CLOCK_PATH};
            bipolar got;

            string_copy((string_address)path, (string_address)root), string_copy((string_address)path + string_length((string_address)path), (string_address)items[at]);
            got = file_slurp_regular_at(AT_FDCWD, (string_address)path, (p8 *)word, sizeof(word) - 1, 0);
            read_back &= got > 0;
            system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        }
    }
    string_copy((string_address)path, (string_address)root), string_copy((string_address)path + string_length((string_address)path), (string_address) "/root");
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0x200); // AT_REMOVEDIR
    system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)root, 0x200);
    compositor_settings_root = "";
    taskbar_standard();
    taskbar_edit_set(0);
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    check("taskbar edit: the moon is an item of the arrangement, and the menu's clock item is the other format", menu);
    check("taskbar edit: a press of a virtual device (WINDOW_EVENT_VIRTUAL) is refused and moves nothing", virtual_refused);
    check("taskbar edit: a press that Done ends, then a release after Edit reopens, drops nothing", dropped_after_done);
    check("taskbar edit: the moon pressed and let go at the band's start by the machine's own device is there", moved);
    check("taskbar edit: Done keeps every row of the arrangement and the band leaves it", kept && left);
    check("taskbar edit: the rows read back what Done kept (the moon start, the tasks center, each tray item's side)", read_back);
}

/*
        Where the clock is (`taskbar clock`), in twelve hours as in twenty-four (`time format`): in the moon, with no cell of its own in the
        tray and the tray the standard one (no format of its own, which a twelve-hour clock used to make, and drew the clock as a cell of
        text beside the moon); none, the moon alone; a cell of the tray, inside its pill after the tasks, as long as its text in the hours
        it is read in; or a pill of its own after the tray's, outside it. Each place in both formats.
*/
static void check_clock_places(void)
{
    static unsigned char lengths[3] = {5, 15, 30};
    b32 moon = 1, none = 1, tray = 1, tab = 1, standard = 1, longer = 1;

    for (int twelve = 0; twelve < 2; twelve++)
    {
        int widths[2] = {0, 0};

        taskbar_tray_set("");
        taskbar_clock_set(twelve ? "{time.12h.hour}:{time.24h.min} {time.ampm}" : "{time.24h.hour}:{time.24h.min}");
        standard &= !taskbar.tray_custom && taskbar.clock_chars == (twelve ? (taskbar.clock_text[1] == ':' ? 7 : 8) : 5);
        for (int place = 0; place < 4; place++)
        {
            struct taskbar_view view;
            int cx, cy, cw, ch, px, py, pw, ph, mx, my, mw, mh;
            b32 clock, pill, moon_at;

            memory_zero(&view, sizeof(view));
            view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 3, view.lengths = lengths;
            taskbar_settings_set("bottom", "primary", "battery none\n");
            taskbar_arrange_set(0, 0, 0, 0, 1u << 1 | 1u << 2, 0, place, 60, twelve);
            clock = taskbar_slot(&view, -3, &cx, &cy, &cw, &ch) && cw > 0;
            pill = taskbar_slot(&view, TASKBAR_TRAY, &px, &py, &pw, &ph) && pw > 0;
            moon_at = taskbar_slot(&view, TASKBAR_LAUNCHER, &mx, &my, &mw, &mh) && mw > 0;
            standard &= taskbar.clock_12h == twelve;
            if (place == 0)
                moon &= moon_at && !clock && !taskbar.clock_tray && !taskbar.clock_tab && !taskbar.clock_none;
            else if (place == 1)
                none &= moon_at && !clock && taskbar.clock_none;
            else if (place == 2)
                tray &= clock && pill && cx >= px && cx + cw <= px + pw, widths[0] = cw;
            else
                tab &= clock && (!pill || cx >= px + pw || cx + cw <= px), widths[1] = cw;
        }
        longer &= widths[0] == widths[1] && widths[0] > 0;
    }
    taskbar_clock_set("");
    taskbar_arrange_set(0, 0, 0, 0, 1u << 1 | 1u << 2, 0, 0, 60, 0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
    check("clock: in the moon by default, in twelve hours as in twenty-four, and no cell of the tray says it", moon);
    check("clock: the tray stays the standard in both formats, the clock's own text the time in the hours it is read in", standard);
    check("clock: none is the moon alone, no clock anywhere", none);
    check("clock: in the tray it is a cell inside the pill after the tasks", tray);
    check("clock: as a tab it is a pill of its own, outside the tray's", tab);
    check("clock: as long as its text in the tray and as a tab alike", longer);
}

/*
        The launcher opens over the moon: centred on the moon along the band wherever the arrangement puts it (the band's start, its
        middle or its end), off the band on the screen's side, and held inside the screen, so at the start it is at the screen's left
        edge, not past it. A side band's is centred on the moon down the band.
*/
static void check_launcher_centred(void)
{
    static unsigned char lengths[3] = {5, 15, 30};
    static const int zones[3] = {TASKBAR_ZONE_START, TASKBAR_ZONE_CENTRE, TASKBAR_ZONE_END};
    b32 centred = 1, held = 1, beside = 1, side = 1;

    for (int each = 0; each < 3; each++)
        for (unsigned int position = TASKBAR_BOTTOM; position <= TASKBAR_RIGHT; position++)
        {
            static const char *const words[4] = {"bottom", "top", "left", "right"};
            struct taskbar_view view;
            struct ui_anchor anchor;
            int x, y, width = 480, height = 360, vertical = position == TASKBAR_LEFT || position == TASKBAR_RIGHT;

            memory_zero(&view, sizeof(view));
            memory_zero(&anchor, sizeof(anchor));
            view.width = BAND_W, view.height = BAND_W * 9 / 16, view.scale = 100, view.position = position, view.count = 3, view.lengths = lengths;
            taskbar_settings_set(words[position], "primary", "battery none\n");
            taskbar.moon_zone = zones[each], taskbar.tasks_zone = zones[each];
            taskbar_changed();
            if (!taskbar_anchor(&view, TASKBAR_LAUNCHER, &anchor.button[0], &anchor.button[1], &anchor.button[2], &anchor.button[3]))
            {
                centred = held = beside = side = 0;
                continue;
            }
            anchor.screen[2] = view.width, anchor.screen[3] = view.height, anchor.edge = position;
            ui_anchor_beside(&anchor, width, height, &x, &y);
            held &= x >= 0 && y >= 0 && x + width <= view.width && y + height <= view.height;
            if (!vertical)
            {
                int middle = anchor.button[0] + anchor.button[2] / 2, want = middle - width / 2;

                want = want < 0 ? 0 : want + width > view.width ? view.width - width : want;
                centred &= x == want;
                beside &= position == TASKBAR_BOTTOM ? y + height == anchor.button[1] : y == anchor.button[1] + anchor.button[3];
            }
            else
            {
                int middle = anchor.button[1] + anchor.button[3] / 2, want = middle - height / 2;

                want = want < 0 ? 0 : want + height > view.height ? view.height - height : want;
                side &= y == want && (position == TASKBAR_LEFT ? x == anchor.button[0] + anchor.button[2] : x + width == anchor.button[0]);
            }
        }
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_standard();
    check("launcher: it opens centred on the moon along a bottom or top band, the moon at the start, the middle or the end", centred);
    check("launcher: off the band on the screen's side, its edge on the band's", beside);
    check("launcher: and inside the screen, at its edge where the moon is too near it to centre on", held);
    check("launcher: on a side band it is centred on the moon down the band, beside it", side);
}

/*
        The highlight of the task with the keys moves to the next one's when the keys do, as the kit's tabs move their pip: half way through
        its 350 ms it is between the two, it is still moving (taskbar_moving, which keeps the band drawn), and after them it is over the new
        task exactly and has stopped. A task that keeps the keys while the band is laid out again has it put where the task is now, at once.
*/
static void check_taskbar_highlight(void)
{
    static unsigned char lengths[3] = {5, 15, 30};
    static unsigned char marks[3];
    struct taskbar_view view;
    struct taskbar_lit address_to lit = &taskbar_bands[0].lit;
    int rect[4], one[4], other[4], band[4];
    b64 between;
    b32 moved, settled, still;

    compositor_zero();
    memory_zero(&taskbar_bands, sizeof(taskbar_bands));
    memory_zero(&view, sizeof(view));
    memory_zero(band_pixels, sizeof(band_pixels));
    view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.position = TASKBAR_BOTTOM, view.count = 3, view.lengths = lengths, view.marks = marks;
    view.pixels = band_pixels, view.band_bytes = sizeof(band_pixels), view.glass = band_glass_pixels;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_band(&view, &band[0], &band[1], &band[2], &band[3]);
    centre_slots(&view, one, 0), centre_slots(&view, other, 1);
    ui_kit.test_clock = 1000000000ll;
    marks[0] = COMPOSITOR_TAB_LIT, marks[1] = marks[2] = 0;
    taskbar_draw(&view, ~0ul, band_glass, null, rect);
    ui_kit.test_clock += 1000000000ll;
    taskbar_draw(&view, ~0ul, band_glass, null, rect);
    marks[0] = 0, marks[1] = COMPOSITOR_TAB_LIT;
    taskbar_draw(&view, ~0ul, band_glass, null, rect);
    ui_kit.test_clock += (b64)ui_paced(TASKBAR_LIT_MS * UI_MS) / 2;
    taskbar_draw(&view, ~0ul, band_glass, null, rect);
    between = lit->at.from + (lit->at.to - lit->at.from) / 2;
    moved = taskbar_moving((positive)ui_kit.test_clock) && lit->at.to == ui_q(other[0] - band[0]) &&
            ui_tween_at(&lit->at, &(b32){0}) != lit->at.to && ui_tween_at(&lit->at, &(b32){0}) != lit->at.from;
    (void)between;
    ui_kit.test_clock += (b64)ui_paced(TASKBAR_LIT_MS * UI_MS);
    taskbar_draw(&view, ~0ul, band_glass, null, rect);
    settled = !taskbar_moving((positive)ui_kit.test_clock) && ui_tween_at(&lit->at, &(b32){0}) == ui_q(other[0] - band[0]) &&
              ui_tween_at(&lit->length, &(b32){0}) == ui_q(other[2]);
    // The same task keeps the keys while its title grows, which moves it: the highlight is put where the task is now, not moved there.
    lengths[1] = 25;
    taskbar_draw(&view, ~0ul, band_glass, null, rect);
    centre_slots(&view, other, 1);
    still = !taskbar_moving((positive)ui_kit.test_clock) && ui_tween_at(&lit->at, &(b32){0}) == ui_q(other[0] - band[0]);
    lengths[1] = 15;
    ui_kit.test_clock = 0;
    check("taskbar highlight: when the keys go to another task the highlight moves to it on the kit's ease, and the band is kept drawn while it moves", moved);
    check("taskbar highlight: after 350 ms it is over the new task exactly and has stopped", settled);
    check("taskbar highlight: a task that keeps the keys while the band is laid out again has it put where the task is now, at once", still);
}

/*
        The band's tree is kept, not made at every question: asked again with nothing changed, a slot, a point and the band are read from the tree
        that is there (a number left in it is still in it), and it is made again when what it was made from is not what it is: a title's length, a
        view that no longer says the lengths, its screen's place in the space. A band on a second screen has a tree of its own.
*/
static void check_taskbar_tree(void)
{
    static unsigned char lengths[3] = {5, 15, 30};
    struct taskbar_view view;
    struct ui_tree address_to tree, address_to other;
    int x, y, w, h, first, bx, by, bw, bh;
    b32 kept, grew, bare, moved;

    compositor_zero();
    memory_zero(&view, sizeof(view));
    view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.count = 3, view.lengths = lengths;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_slot(&view, 0, &x, &y, &first, &h);
    tree = taskbar_tree(&view);
    tree->node[0].spare = 77;
    kept = taskbar_slot(&view, 1, &x, &y, &w, &h) && taskbar_hit_reach(&view, x + 1, y + 1, 0) == 1 && taskbar_tree(&view) == tree;
    taskbar_band(&view, &bx, &by, &bw, &bh);
    check("taskbar tree: asked again with nothing changed, the band is read from the tree it has, which is not made again", kept && bw == BAND_W && tree->node[0].spare == 77);

    lengths[0] = 30;
    grew = taskbar_slot(&view, 0, &x, &y, &w, &h) && w > first && !tree->node[0].spare;
    lengths[0] = 5, view.lengths = null, tree->node[0].spare = 77;
    bare = taskbar_slot(&view, 0, &x, &y, &w, &h) && w == 8 * COMPOSITOR_PANEL + 16 + UI_GAP2 && !tree->node[0].spare;
    view.lengths = lengths;
    taskbar_band(&view, &bx, &by, &bw, &bh);
    view.x = 2560, view.y = 100, tree->node[0].spare = 77;
    taskbar_band(&view, &bx, &by, &bw, &bh);
    moved = bx == 2560 && by == 100 + BAND_H - COMPOSITOR_PANEL && !tree->node[0].spare && taskbar_slot(&view, 0, &x, &y, &w, &h) && w == first && x > 2560;
    check("taskbar tree: a title that grew, a view that no longer says the lengths and a screen put elsewhere are each a tree made again: the tab longer, as long as a tab may be, and on that screen",
          grew && bare && moved);

    view.x = view.y = 0;
    taskbar_tree(&view)->node[0].spare = 77;
    compositor.band_at = 1, view.x = BAND_W;
    other = taskbar_tree(&view);
    compositor.band_at = 0, view.x = 0;
    check("taskbar tree: a band on a second screen has a tree of its own, and asking of it leaves the first band's as it is",
          other != tree && taskbar_tree(&view) == tree && tree->node[0].spare == 77);
}

/*
        `taskbar scale` and `taskbar padding`. The band is as thick as the screen's scale and the taskbar's own make it (24 logical pixels at
        the two together), on every edge, and the memory the compositor keeps for it (taskbar_bytes) holds it; the gap between the items and at
        the band's ends is fixed (4 logical pixels at that scale) and the padding is the room round the text inside each tab, either side of it
        (0 puts the tab's edges at its text), and what is outside the ranges is the standard. The rows' words are what the compositor reads: a
        whole number in each row's range. A whole band drawn at the largest scale stays inside its memory.
*/
static void check_taskbar_scale(void)
{
    static const unsigned char few[3] = {5, 15, 30};
    static const int screens[3] = {100, 150, 200}, scales[6] = {50, 75, 100, 125, 150, 200}, paddings[5] = {0, 1, 4, 10, 24};
    static const unsigned int positions[4] = {TASKBAR_BOTTOM, TASKBAR_TOP, TASKBAR_LEFT, TASKBAR_RIGHT};
    static const char *const words[4] = {"bottom", "top", "left", "right"};
    b32 thick_ok = true, bytes_ok = true, gap_ok = true, ends_ok = true, pad_ok = true, standard_ok = true, drawn = true, rows_ok = true;
    long number = -1;

    compositor_zero();
    compositor.width = BAND_W, compositor.height = BAND_H, compositor.scale = 100;
    taskbar_tray_set("");
    taskbar_launcher_set(1);
    for (int at = 0; at < 4; at++)
        for (int screen = 0; screen < 3; screen++)
            for (int each = 0; each < 6; each++)
            {
                struct taskbar_view view;
                int bx, by, bw, bh, thick, want = COMPOSITOR_PANEL * (screens[screen] * scales[each] / 100) / 100;
                unsigned long band = 0, glass = 0;

                memory_zero(&view, sizeof(view));
                view.width = BAND_W, view.height = BAND_H, view.scale = screens[screen], view.position = positions[at];
                view.count = 3, view.lengths = few;
                taskbar_settings_set(words[at], "primary", "battery none\n");
                taskbar_scale_set(scales[each]);
                taskbar_padding_set(TASKBAR_PADDING_STANDARD);
                taskbar_band(&view, &bx, &by, &bw, &bh);
                thick = positions[at] == TASKBAR_LEFT || positions[at] == TASKBAR_RIGHT ? bw : bh;
                thick_ok &= thick == want && taskbar_thick(screens[screen]) == want;
                taskbar_bytes(BAND_W, BAND_H, screens[screen], &band, &glass);
                bytes_ok &= band >= (unsigned long)bw * (unsigned long)bh * 4 && glass >= (unsigned long)bw * (unsigned long)bh * 4;
            }

    // The tasks of a side meet, the pills are the kit's gap twice apart (the moon and the pill of the tasks to its right), and the padding is the
    // room round a task's icon and title: the first task (a title of five characters) is the text's width, the gap after the icon, the icon and
    // the padding either side, never under its height; all whatever the padding.
    for (int screen = 0; screen < 3; screen++)
        for (int each = 0; each < 6; each++)
            for (int pad = 0; pad < 5; pad++)
            {
                struct taskbar_view view;
                int bx, by, bw, bh, x0, y0, w0, h0, x2, y2, w2, h2, kx, ky, kw, kh, px, py, pw, ph;
                int eff = screens[screen] * scales[each] / 100, inset = 2 * eff / 100 > 1 ? 2 * eff / 100 : 1;
                int high = COMPOSITOR_PANEL * eff / 100 - 2 * inset, gap = TASKBAR_GAP * eff / 100;
                int wide = tab_oracle(high, paddings[pad] * eff / 100, eff, 5);

                memory_zero(&view, sizeof(view));
                view.width = BAND_W, view.height = BAND_H, view.scale = screens[screen], view.position = TASKBAR_BOTTOM;
                view.count = 3, view.lengths = few;
                taskbar_settings_set("bottom", "primary", "battery none\n");
                taskbar_scale_set(scales[each]);
                taskbar_padding_set(paddings[pad]);
                taskbar_band(&view, &bx, &by, &bw, &bh);
                if (!taskbar_slot(&view, 0, &x0, &y0, &w0, &h0) || !taskbar_slot(&view, 2, &x2, &y2, &w2, &h2) ||
                    !taskbar_slot(&view, TASKBAR_LAUNCHER, &kx, &ky, &kw, &kh) || !taskbar_slot(&view, TASKBAR_TABS, &px, &py, &pw, &ph))
                {
                    gap_ok = ends_ok = pad_ok = false;
                    continue;
                }
                gap_ok &= x2 == x0 + w0;
                ends_ok &= px - (kx + kw) == 2 * gap;
                // Where the side has the room for its tasks whole (a quarter of the band holds two); a side too short shares it out, none wider.
                pad_ok &= 2 * wide <= bw / 4 ? w0 == wide : w0 <= wide;
            }

    // What is outside the choices and the range is the standard.
    {
        struct taskbar_view view;
        int bx, by, bw, bh, x0, y0, w0, h0, x1, y1, w1, h1;
        static const int bad_scales[4] = {0, 49, 201, -100}, bad_paddings[3] = {-1, 25, 1000};

        memory_zero(&view, sizeof(view));
        view.width = BAND_W, view.height = BAND_H, view.scale = 100, view.position = TASKBAR_BOTTOM;
        view.count = 3, view.lengths = few;
        taskbar_settings_set("bottom", "primary", "battery none\n");
        for (int at = 0; at < 4; at++)
        {
            taskbar_scale_set(bad_scales[at]);
            taskbar_band(&view, &bx, &by, &bw, &bh);
            standard_ok &= bh == COMPOSITOR_PANEL;
        }
        taskbar_scale_set(TASKBAR_SCALE_STANDARD);
        for (int at = 0; at < 3; at++)
        {
            taskbar_padding_set(bad_paddings[at]);
            taskbar_slot(&view, 0, &x0, &y0, &w0, &h0);
            taskbar_slot(&view, 2, &x1, &y1, &w1, &h1);
            standard_ok &= w0 == 5 * (COMPOSITOR_PANEL - 4) * 308 / 1000 + 2 * TASKBAR_PADDING_STANDARD + 16 + UI_GAP2 && x1 == x0 + w0;
        }
    }

    // The rows' words: a scale is any whole number from 50 to 200 and a padding one from 0 to 24, and nothing else is kept.
    compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "100", &number); /* the row's reset word: not kept, the standard */
    rows_ok &= number == TASKBAR_SCALE_STANDARD;
    rows_ok &= compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "50", &number) && number == 50;
    rows_ok &= compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "75", &number) && number == 75;
    rows_ok &= compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "137", &number) && number == 137;
    rows_ok &= compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "200", &number) && number == 200;
    rows_ok &= !compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "49", &number) && number == TASKBAR_SCALE_STANDARD;
    rows_ok &= !compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "201", &number) && !compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "", &number) &&
               !compositor_setting_kept(SETTING_TASKBAR_SCALE_PATH, "1.5", &number);
    rows_ok &= compositor_setting_kept(SETTING_TASKBAR_PADDING_PATH, "0", &number) && number == 0;
    rows_ok &= compositor_setting_kept(SETTING_TASKBAR_PADDING_PATH, "24", &number) && number == 24;
    rows_ok &= !compositor_setting_kept(SETTING_TASKBAR_PADDING_PATH, "25", &number) && number == TASKBAR_PADDING_STANDARD;
    rows_ok &= !compositor_setting_kept(SETTING_TASKBAR_PADDING_PATH, "-1", &number) && !compositor_setting_kept(SETTING_TASKBAR_PADDING_PATH, "x", &number);

    // A whole band at the largest scale on a 1920 screen is drawn, every slot of it, inside the memory the band is given: twice the standard's.
    {
        struct taskbar_view view = band_view(TASKBAR_BOTTOM, null);
        int rect[4];

        memory_zero(launcher_band_pixels, sizeof(launcher_band_pixels));
        view.pixels = launcher_band_pixels, view.band_bytes = sizeof(launcher_band_pixels);
        taskbar_settings_set("bottom", "primary", "battery none\n");
        taskbar_scale_set(TASKBAR_SCALE_MOST);
        taskbar_padding_set(TASKBAR_PADDING_MOST);
        taskbar_volume_set(1, 50, 0, 0);
        taskbar_mic_set(1, 0);
        taskbar_notices_set(1, WINDOWS_ISSUE_ERROR);
        drawn &= taskbar_draw(&view, ~0ul, band_glass, null, rect) == 1;
        // The band's last row is drawn (it is 48 rows, twice the standard's), and the memory has no more than that.
        {
            int ink = 0;

            for (int column = 0; column < BAND_W; column++)
                ink += launcher_band_pixels[(unsigned long)(2 * COMPOSITOR_PANEL - 1) * BAND_W + (unsigned long)column] != 0;
            drawn &= ink > 0;
        }
    }
    taskbar_notices_set(0, 0);
    taskbar_mic_set(0, 0);
    taskbar_volume_set(0, 0, 0, 0);
    taskbar_scale_set(TASKBAR_SCALE_STANDARD);
    taskbar_padding_set(TASKBAR_PADDING_STANDARD);
    taskbar_launcher_set(0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
    check("taskbar scale: the band is as thick as the screen's scale and the taskbar's make it, on every edge, at 50 to 200 percent of each", thick_ok);
    check("taskbar scale: the memory kept for the band and its glass holds it at every scale", bytes_ok);
    check("taskbar padding: the tasks of a side meet whatever the padding, at the scale", gap_ok);
    check("taskbar padding: the moon and the tasks' pill beside it are the kit's gap twice apart, whatever the padding", ends_ok);
    check("taskbar padding: a task is its text's width, the gap after its icon, the icon and the padding either side, from 0 to 24 pixels, at every scale", pad_ok);
    check("taskbar scale: a scale and a padding outside their ranges are the standard", standard_ok);
    check("taskbar scale: the rows' words are the numbers the compositor reads, any in range", rows_ok);
    check("taskbar scale: a whole band at 200 percent, the widest padding and every cell is drawn inside its memory", drawn);
}

/* A bottom and a top band in the standard theme (dark, with white) are the bytes pinned here: three tabs, each the window
   symbol and its title (pinned again when a tab took an icon before its title, and when the tabs became glass pills split about the
   moon, the clock in it at a check's time; before that the tabs were their titles alone).
   The unthemed table had a tint of its own, which no shell draws: the compositor themes its table at start. */
#define BAND_UPRIGHT 0xc346e9c1c817ba4cull

/* The cut of a title to room: what it gives fits, a cut ends in "..." after a whole character, the next character more
   would not fit, and what fits whole is not cut. */
static int band_fit_ok(const struct taskbar_view *view, const char *title, int room)
{
    const font_face address_to faces[1] = {view->face};
    p32 em = 11 << 6;
    char fitted[WINDOWS_TITLE + 4], longer[WINDOWS_TITLE + 4];
    positive length = string_length((string_address)title), n = ui_cell_fit(taskbar_cell(view, view->width), faces, title, em, room, fitted, sizeof(fitted)), next;
    int width = (int)(font_extent(view->engine, faces, 1, (const p8 address_to)fitted, n, em, 0) >> 16);

    if (n == length && !memory_compare(fitted, title, length))
        return width <= room || length <= 1;
    if (n > 3 && !memory_compare(fitted + n - 3, "...", 3))
    {
        next = n - 3 + 1;
        while (next < length && ((unsigned char)title[next] & 0xc0) == 0x80)
            next++;
        memory_copy(longer, title, next);
        memory_copy(longer + next, "...", 3);
        return width <= room && !memory_compare(fitted, title, n - 3) && ((unsigned char)title[n - 3] & 0xc0) != 0x80 &&
               (next >= length || (int)(font_extent(view->engine, faces, 1, (const p8 address_to)longer, next + 3, em, 0) >> 16) > room);
    }
    // The first character alone, when no beginning fits with the dots.
    memory_copy(longer, title, n);
    memory_copy(longer + n, "...", 3);
    return n < length && !memory_compare(fitted, title, n) &&
           (int)(font_extent(view->engine, faces, 1, (const p8 address_to)longer, n + 3, em, 0) >> 16) > room;
}

/*
        The kit's symbols (ui_icon), the lettered tile (ui_icon_tile) and a program's own picture (ui_icon_scale, ui_icon_picture), drawn
        into a picture of their own: every symbol of the table draws at every size the band asks, inside its square and in the ink it is
        given, and no two are the same marks; the network's bars and the battery's bar follow their value; a tile's colour is its
        name's and takes a white letter; a picture is made the size asked and laid over what is under it by its alpha, inside the rows
        and columns the picture has.
*/
#define ICON_FIELD 96
static unsigned int icon_field[ICON_FIELD * ICON_FIELD], icon_other[ICON_FIELD * ICON_FIELD];

static struct ui icon_ui(unsigned int *pixels)
{
    struct ui ui;

    memory_zero(&ui, sizeof(ui));
    for (unsigned int at = 0; at < ICON_FIELD * ICON_FIELD; at++)
        pixels[at] = 0x101010u;
    ui.pixels = pixels, ui.pitch = ICON_FIELD, ui.width = ICON_FIELD, ui.height = ICON_FIELD, ui.top = 0, ui.bottom = ICON_FIELD, ui.scale = 100;
    return ui;
}

// How many pixels of a field are not the ground, how many are the ink whole, and whether any is outside the square size to a side at 16, 16.
static int icon_count(const unsigned int *pixels, int size, unsigned int ink, int *whole, int *outside)
{
    int drawn = 0;

    *whole = *outside = 0;
    for (int y = 0; y < ICON_FIELD; y++)
        for (int x = 0; x < ICON_FIELD; x++)
        {
            unsigned int pixel = pixels[y * ICON_FIELD + x] & 0xffffffu;

            if (pixel == 0x101010u)
                continue;
            drawn++, *whole += pixel == (ink & 0xffffffu);
            *outside += x < 16 || y < 16 || x >= 16 + size || y >= 16 + size;
        }
    return drawn;
}

static void check_icons(void)
{
    static const int sizes[5] = {8, 16, 24, 32, 48};
    static unsigned int picture[WINDOWS_ICON * WINDOWS_ICON], scaled[UI_ICON_MOST * UI_ICON_MOST];
    struct ui ui, other;
    int whole, outside, drawn, every = 1, inside = 1, inked = 1, apart = 1, bars = 1, cell = 1, last = -1, same = 1;
    char letter[5];

    for (int which = 0; which < UI_ICON_COUNT; which++)
        for (int each = 0; each < 5; each++)
        {
            ui = icon_ui(icon_field);
            ui_icon(&ui, which, 16, 16, sizes[each], which == UI_ICON_BATTERY ? 60 : 3, 0xff3a8f5cu);
            drawn = icon_count(icon_field, sizes[each], 0xff3a8f5cu, &whole, &outside);
            every &= drawn >= sizes[each] / 2, inside &= !outside, inked &= whole > 0 || sizes[each] < 16;
            // No two symbols are the same marks at 16, the size a tab draws them.
            for (int against = 0; sizes[each] == 16 && against < which; against++)
            {
                int differ = 0;

                other = icon_ui(icon_other);
                ui_icon(&other, against, 16, 16, 16, against == UI_ICON_BATTERY ? 60 : 3, 0xff3a8f5cu);
                for (unsigned int at = 0; at < ICON_FIELD * ICON_FIELD; at++)
                    differ += icon_field[at] != icon_other[at];
                apart &= differ >= 8;
            }
        }
    check("icons: every symbol of the table draws at 8, 16, 24, 32 and 48 pixels", every);
    check("icons: and nothing of any is outside its square", inside);
    check("icons: from 16 pixels up each has pixels that are the ink it was given whole, so it is the theme's colour and no other (at 8 a dot is two pixels across and all edge)", inked);
    check("icons: no two symbols are the same marks at the size a tab draws them", apart);
    ui = icon_ui(icon_field);
    ui_icon(&ui, UI_ICON_COUNT, 16, 16, 16, 0, 0xffffffffu), ui_icon(&ui, -1, 16, 16, 16, 0, 0xffffffffu), ui_icon(&ui, UI_ICON_FILES, 16, 16, 0, 0, 0xffffffffu);
    check("icons: a symbol the table has not got, and a size of nothing, draw nothing", icon_count(icon_field, 16, 0xffffffffu, &whole, &outside) == 0);
    for (int value = 0; value <= 4; value++)
    {
        ui = icon_ui(icon_field);
        ui_icon(&ui, UI_ICON_NETWORK, 16, 16, 32, value, 0xffffffffu);
        drawn = icon_count(icon_field, 32, 0xffffffffu, &whole, &outside);
        bars &= whole > last && (value || !whole) && (drawn > whole) == (value < 4), last = whole;
    }
    check("icons: the network's bars are lit one more for each of its four levels, the others faint, none lit at none and none faint at four", bars);
    last = -1;
    for (int value = 0; value <= 100; value += 20)
    {
        ui = icon_ui(icon_field);
        ui_icon(&ui, UI_ICON_BATTERY, 16, 16, 32, value, 0xffffffffu);
        icon_count(icon_field, 32, 0xffffffffu, &whole, &outside);
        cell &= whole > last && !outside, last = whole;
    }
    ui = icon_ui(icon_field), other = icon_ui(icon_other);
    ui_icon(&ui, UI_ICON_BATTERY, 16, 16, 32, 100, 0xffffffffu), ui_icon(&other, UI_ICON_BATTERY, 16, 16, 32, 250, 0xffffffffu);
    for (unsigned int at = 0; at < ICON_FIELD * ICON_FIELD; at++)
        same &= icon_field[at] == icon_other[at];
    check("icons: the battery's bar is longer for every twenty percent more, inside its cell, and a percent past a hundred is a hundred", cell && same);
    check("icons: the kit's programs have their symbols by the app_id they say, and a program that is none of the kit's has none",
          ui_icon_of("settings") == UI_ICON_SETTINGS && ui_icon_of("files") == UI_ICON_FILES && ui_icon_of("terminal") == UI_ICON_TERMINAL &&
          ui_icon_of("editor") == UI_ICON_EDITOR && ui_icon_of("mixer") == UI_ICON_MIXER && ui_icon_of("launcher") == UI_ICON_APPS &&
          ui_icon_of("org.mozilla.firefox") < 0 && ui_icon_of("") < 0 && ui_icon_of(null) < 0 && ui_icon_of("Settings") < 0);

    // The tile: its colour is the name's, dark enough for white; its letter is the last word's first letter or digit.
    {
        static const char *const names[6] = {"org.mozilla.firefox", "org.gnome.Nautilus", "steam", "a", "com.example.App-2", "z.z.z"};
        int toned = 1, distinct = 0, lettered;

        for (int at = 0; at < 6; at++)
        {
            unsigned int tone = ui_icon_tone(names[at]);

            toned &= tone == ui_icon_tone(names[at]) && (tone >> 16 & 255) >= 40 && (tone >> 16 & 255) <= 140 && (tone >> 8 & 255) >= 40 && (tone >> 8 & 255) <= 140 &&
                     (tone & 255) >= 40 && (tone & 255) <= 140 && look_luma(tone) < 140;
            for (int against = 0; against < at; against++)
                distinct += tone != ui_icon_tone(names[against]);
        }
        check("icons: a tile's colour is its name's, the same each time, every colour of it between 40 and 140 so a white letter stands on it, and six names are six colours",
              toned && distinct == 15);
        ui_icon_letter("org.mozilla.firefox", letter), lettered = letter[0] == 'F' && !letter[1];
        ui_icon_letter("steam", letter), lettered &= letter[0] == 'S' && !letter[1];
        ui_icon_letter("org.kde.3dviewer", letter), lettered &= letter[0] == '3' && !letter[1];
        ui_icon_letter("org.example.\xc3\xa9" "diteur", letter), lettered &= string_equals((string_address)letter, (string_address) "\xc3\xa9");
        ui_icon_letter("trailing.", letter), lettered &= letter[0] == 'T';
        ui_icon_letter("...", letter), lettered &= letter[0] == '?' && !letter[1];
        ui_icon_letter("org.example.\xe2\x82", letter), lettered &= letter[0] == '?' && !letter[1];
        ui_icon_letter("", letter), lettered &= letter[0] == '?';
        ui_icon_letter(null, letter), lettered &= letter[0] == '?';
        check("icons: a tile's letter is the first letter or digit of the last word of the name, a small letter made a capital, a character past ASCII whole or not at all, and a question mark for a name with none",
              lettered);
        ui = icon_ui(icon_field);
        ui.engine = &band_engine, ui.regular = &band_face;
        ui_icon_tile(&ui, 16, 16, 32, "org.mozilla.firefox");
        drawn = icon_count(icon_field, 32, 0xff000000u | ui_icon_tone("org.mozilla.firefox"), &whole, &outside);
        last = 0;
        for (unsigned int at = 0; at < ICON_FIELD * ICON_FIELD; at++)
            last += (icon_field[at] & 0xffffffu) == 0xffffffu;
        check("icons: a tile is a square of its name's colour with round corners, nothing outside it, and its letter on it in white",
              !outside && whole > 32 * 32 / 2 && drawn < 32 * 32 && (icon_field[16 * ICON_FIELD + 16] & 0xffffffu) == 0x101010u && last > 8 && last < whole);
    }

    // A program's picture: made the size asked, and laid over what is under it.
    {
        int halved = 1, copied = 1, doubled = 1, over, kept = 1;

        for (unsigned int y = 0; y < WINDOWS_ICON; y++)
            for (unsigned int x = 0; x < WINDOWS_ICON; x++)
                picture[y * WINDOWS_ICON + x] = 0xff000000u | ((x & 1) ? 0x640000u : 0xc80000u) | (y / 2) << 8 | ((y & 1) ? 10u : 20u);
        ui_icon_scale(scaled, 16, picture);
        for (unsigned int at = 0; at < 16 * 16; at++)
            halved &= scaled[at] == (0xff96000fu | (at / 16) << 8);
        ui_icon_scale(scaled, 32, picture);
        for (unsigned int at = 0; at < 32 * 32; at++)
            copied &= scaled[at] == picture[at];
        ui_icon_scale(scaled, 64, picture);
        for (unsigned int y = 0; y < 64; y++)
            for (unsigned int x = 0; x < 64; x++)
                doubled &= scaled[y * 64 + x] == picture[(y / 2) * WINDOWS_ICON + x / 2];
        scaled[0] = 0x12345678u;
        ui_icon_scale(scaled, 0, picture), ui_icon_scale(scaled, UI_ICON_MOST + 1, picture);
        check("icons: a picture made 16 to a side is the mean of each two by two, made 32 it is itself, made 64 each pixel is four, and a size of nothing or past the largest writes nothing",
              halved && copied && doubled && scaled[0] == 0x12345678u);
        // Over: an opaque pixel is itself, a clear one leaves what is under it, a half one is the two mixed.
        for (unsigned int at = 0; at < 16 * 16; at++)
            scaled[at] = at % 3 == 0 ? 0xffff0000u : at % 3 == 1 ? 0u : 0x80008000u;
        ui = icon_ui(icon_field);
        ui_icon_picture(&ui, 16, 16, 16, scaled);
        over = (icon_field[16 * ICON_FIELD + 16] & 0xffffffu) == 0xff0000u && (icon_field[16 * ICON_FIELD + 17] & 0xffffffu) == 0x101010u &&
               (icon_field[16 * ICON_FIELD + 18] & 0xffffffu) == 0x088808u;
        icon_count(icon_field, 16, 0, &whole, &outside);
        check("icons: a picture is laid over what is under it by its alpha, an opaque pixel whole, a clear one not at all, a half one half, and nothing outside its square", over && !outside);
        // Cut to the rows the ui may write, and not drawn at all where it would pass the picture's width.
        ui = icon_ui(icon_field);
        ui.top = 20, ui.bottom = 24;
        ui_icon_picture(&ui, 16, 16, 16, scaled);
        for (int y = 0; y < ICON_FIELD; y++)
            for (int x = 0; x < ICON_FIELD; x++)
                kept &= (y >= 20 && y < 24) || icon_field[y * ICON_FIELD + x] == 0x101010u;
        drawn = icon_count(icon_field, 16, 0, &whole, &outside);
        ui = icon_ui(icon_other);
        ui_icon_picture(&ui, ICON_FIELD - 8, 16, 16, scaled), ui_icon_picture(&ui, -1, 16, 16, scaled), ui_icon_picture(&ui, 16, 16, UI_PICTURE_MOST + 1, scaled);
        check("icons: a picture is cut to the rows the ui may write, and one that would pass the picture's side or is too large is not drawn",
              kept && drawn > 0 && icon_count(icon_other, 16, 0, &whole, &outside) == 0);
    }
}

/*
        A tab is its window's icon and then its title. The icon's element is the padding in from the tab's leading end, in the middle
        of the tab across the band, the side its scale gives, and the title has what is left; reading from the right the icon is at
        the tab's right end. What is drawn there is the window's own picture where it has one, the kit's symbol in the tab's ink for
        a kit program, the lettered tile for another program that says who it is, and the window symbol for one that says nothing;
        a picture that changes is drawn again, and a tab too short for its icon draws none outside itself.
*/
/*
        The band never writes past its buffer. The compositor's crash at accd044 was a row stored one row past the bottom of a
        1080-row buffer, and the band's cells are text the kit draws into the band's own picture (ui_cell_line, ui_cell_turned) from
        each element's rectangle. Here the band is drawn on every edge, at 100 to 200 percent, with three long titles, the tray's
        cells and a format of the person's, from 40 to 2000 pixels long, into a buffer of exactly the band's bytes (view->band_bytes)
        that ends at a protected page: a store past its last pixel ends the check there, as the crash ended the compositor.
*/
static unsigned int band_guard_glass_pixels[2100 * 300];

static int band_guard_glass(void *context, int x, int y, int width, int height)
{
    (void)context;
    if (width <= 0 || height <= 0 || (long)width * height > (long)(sizeof(band_guard_glass_pixels) / sizeof(band_guard_glass_pixels[0])))
        return 0;
    for (int row = 0; row < height; row++)
        for (int column = 0; column < width; column++)
            band_guard_glass_pixels[row * width + column] = (unsigned int)((x + column) * 0x000103 + (y + row) * 0x050201) & 0xffffffu;
    return width;
}

/*
        A frame recorded into a window's list says what changed (the header's damage) inside its picture: a box whose columns run
        past the window's edges (a shadow, a ripple, a lit row's reach) is drawn cut to the picture, and the damage is cut with it, so
        the compositor's reader (window_list_valid) takes the list. The owner's machine refused 31 lists for damage outside the picture.
*/
static void check_scene_damage_cut(void)
{
    static unsigned int picture[200 * 100];
    static struct ui_draw_list scene_list;
    struct ui ui;
    long mapped = (long)system_call_6(syscall(mmap), 0, WINDOW_LIST_BYTES, 3, 0x4022, (positive)-1, 0);
    unsigned char address_to bytes = (unsigned char address_to)mapped;
    const unsigned int address_to head;
    b32 cut;

    if (mapped < 0 && mapped > -4096)
    {
        check("scene damage: the list's memory is mapped", false);
        return;
    }
    memory_zero(&ui, sizeof(ui));
    memory_zero(&scene_list, sizeof(scene_list));
    ui.scale = 100;
    ui_draw_list_init(&scene_list, bytes, WINDOW_LIST_BYTES, WINDOW_LIST_ROOM);
    ui_begin(&ui, picture, 200, 200, 100);
    ui_scene_begin(&ui, &scene_list);
    ui_kit_fill(&ui, &(struct ui_box){-20 * 256, 10 * 256, 230 * 256, 30 * 256, 0, 0}, 0xff336699u, null);
    ui_kit_fill(&ui, &(struct ui_box){50 * 256, 80 * 256, 90 * 256, 140 * 256, 8 * 256, 8 * 256}, 0x80ffffffu, null);
    ui_scene_end(&ui, &scene_list);
    head = (const unsigned int address_to)bytes;
    cut = (int)head[UI_DRAW_LIST_DX0] >= 0 && (int)head[UI_DRAW_LIST_DY0] >= 0 && (int)head[UI_DRAW_LIST_DX1] <= 200 && (int)head[UI_DRAW_LIST_DY1] <= 100 &&
          (int)head[UI_DRAW_LIST_DX1] > (int)head[UI_DRAW_LIST_DX0] && (int)head[UI_DRAW_LIST_DY1] > (int)head[UI_DRAW_LIST_DY0];
    check("scene damage: a frame whose boxes run past its picture's edges says damage inside the picture, and the reader takes its list",
          cut && window_list_valid(bytes, 200, 100));
    system_call_2(syscall(munmap), (positive)mapped, WINDOW_LIST_BYTES);
}

static void check_taskbar_guard(void)
{
    static const char *const long_titles[3] = {"A window whose title runs on well past any tab the band could ever give it, and then on again",
                                               "\xd8\xa7\xd9\x84\xd9\x86\xd8\xa7\xd9\x81\xd8\xb0\xd8\xa9 a title with Arabic in it and more words after them",
                                               "short"};
    static const char *const edges[4] = {"bottom", "top", "left", "right"};
    static const int scales[4] = {100, 125, 150, 200}, lengths[8] = {40, 97, 160, 333, 640, 1080, 1366, 2000};
    static struct windows *guard_table_scratch;
    struct windows *const guard_table_at = windows_scratch_table(&guard_table_scratch);
    struct windows address_to kept_table = compositor.table;
    int drawn = 0, tried = 0, rect[4], outside = 0, named_outside = 0, laid = 0;

    windows_scratch_zero(guard_table_at);
    guard_table_at->themed = 1, guard_table_at->theme = 1, guard_table_at->accent = 0xffffffu;
    compositor.table = guard_table_at;
    taskbar_volume_set(1, 50, 0, 0);
    taskbar_mic_set(1, 0);
    taskbar_notices_set(1, WINDOWS_ISSUE_WARNING);
    for (int custom = 0; custom < 2; custom++)
    {
        taskbar_tray_set(custom ? "{time.24h.hour}:{time.24h.min} and more words of the person's own" : SETTING_TASKBAR_TRAY_STANDARD);
        for (unsigned int position = TASKBAR_BOTTOM; position <= TASKBAR_RIGHT; position++)
            for (int each = 0; each < 4; each++)
                for (int at = 0; at < 8; at++)
                {
                    struct taskbar_view view;
                    int bx, by, bw, bh;
                    positive bytes, room;
                    long mapped;

                    taskbar_settings_set(edges[position], "primary", "battery none\n");
                    view = band_view(position, long_titles);
                    view.scale = scales[each];
                    if (position >= TASKBAR_LEFT)
                        view.height = lengths[at];
                    else
                        view.width = lengths[at];
                    view.glass = band_guard_glass_pixels;
                    taskbar_band(&view, &bx, &by, &bw, &bh);
                    if (bw <= 0 || bh <= 0)
                        continue;
                    bytes = (positive)bw * (positive)bh * 4, room = (bytes + 4095) & ~4095ul;
                    mapped = (long)system_call_6(syscall(mmap), 0, room + 4096, 3, 0x4022, (positive)-1, 0);
                    if (mapped < 0 && mapped > -4096)
                        continue;
                    if (system_call_3(syscall(mprotect), (positive)mapped + room, 4096, 0) != 0)
                    {
                        system_call_2(syscall(munmap), (positive)mapped, room + 4096);
                        continue;
                    }
                    // The band's last pixel is the last before the protected page.
                    view.pixels = (unsigned int address_to)(mapped + (long)(room - bytes));
                    view.band_bytes = bytes;
                    tried++;
                    drawn += taskbar_draw(&view, ~0ul, band_guard_glass, null, rect) != 0;
                    system_call_2(syscall(munmap), (positive)mapped, room + 4096);
                    // What the band lays out is inside it: a tab or cell it has not the room for is given no room (no rectangle), not one
                    // past its end, and none past it is named, so no press, point or draw finds a rectangle that is not wholly inside it.
                    {
                        const struct ui_tree address_to t = taskbar_tree(&view);
                        const struct ui_node address_to band = &t->node[1];

                        for (int k = 2; k < t->count; k++)
                        {
                            const struct ui_node address_to n = &t->node[k];
                            b32 past = n->w > 0 && n->h > 0 && !(n->x >= band->x && n->y >= band->y && n->x + n->w <= band->x + band->w &&
                                                                   n->y + n->h <= band->y + band->h);

                            laid += n->w > 0 && n->h > 0;
                            outside += past;
                            named_outside += past && n->id != 0;
                        }
                    }
                }
    }
    /*
        A cell's text is inside its rectangle (ui_cell_line's target): a letter under twelve stacked marks, in a cell 20 pixels high, is
        inked in the cell and nowhere below it or right of it. Its em is 11/20 of the cell's height, and the marks reach no lower than
        its last row (the count is in the log), so a target a row taller than the cell is not seen here: no text reaches that row.
    */
    {
        static unsigned int cell[(20 + 8) * (240 + 8)];
        static const char stacked[] = "g\xcc\xa3\xcc\xb1\xcc\xa3\xcc\xb1\xcc\xa3\xcc\xb1\xcc\xa3\xcc\xb1\xcc\xa3\xcc\xb1\xcc\xa3\xcc\xb1";
        struct taskbar_view face_view = band_view(TASKBAR_BOTTOM, null);
        struct ui cell_ui;
        const unsigned int sentinel = 0x5a5a5a5au;
        int last_row = 0, spilled = 0, inside = 0;

        memory_zero(&cell_ui, sizeof(cell_ui));
        memory_fill_u32(cell, sizeof(cell) / sizeof(cell[0]), sentinel);
        cell_ui.pixels = cell, cell_ui.pitch = 248, cell_ui.width = 248, cell_ui.height = 28, cell_ui.scale = 100;
        cell_ui.engine = face_view.engine, cell_ui.regular = face_view.face;
        ui_cell_line(&cell_ui, 0, 0, 240, 20, stacked, 0xffffffffu, 0xff000000u);
        for (int y = 0; y < 28; y++)
            for (int x = 0; x < 248; x++)
            {
                b32 inked = cell[y * 248 + x] != sentinel;

                last_row += inked && y == 19 && x < 240;
                inside += inked && y < 20 && x < 240;
                spilled += inked && (y >= 20 || x >= 240);
            }
        string_format(log, "  taskbar guard: a cell's stacked marks ink %p pixels inside it, %p of its last row, and %p outside it\n", (positive)inside,
                      (positive)last_row, (positive)spilled);
        check("taskbar guard: a cell's text, a letter under twelve stacked marks, is inked inside its rectangle and nowhere below or beside it",
              inside > 0 && spilled == 0);
    }
    taskbar_tray_set(SETTING_TASKBAR_TRAY_STANDARD);
    taskbar_volume_set(0, 0, 0, 0);
    taskbar_mic_set(0, 0);
    taskbar_notices_set(0, 0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
    compositor.table = kept_table;
    check("taskbar guard: a band on every edge at 100 to 200 percent, long titles, the tray's cells and a format of the person's, 40 to 2000 pixels long, drawn into exactly its bytes before a protected page, writes nothing past them",
          tried == 2 * 4 * 4 * 8 && drawn > tried / 2);
    string_format(log, "  taskbar guard: %p rectangles laid out, %p past their band, %p of those named\n", (positive)laid, (positive)outside, (positive)named_outside);
    check("taskbar guard: on every edge, scale and length from 40 to 2000 pixels each rectangle the band lays out is wholly inside it, and none past it is named",
          laid > 0 && outside == 0 && named_outside == 0);
}

static void check_taskbar_identity(void)
{
    static unsigned int first[BAND_W * COMPOSITOR_PANEL], second[BAND_W * COMPOSITOR_PANEL], picture[WINDOWS_ICON * WINDOWS_ICON], again[WINDOWS_ICON * WINDOWS_ICON];
    static const char *const words[4] = {"bottom", "top", "left", "right"};
    static const int scales[3] = {100, 150, 200};
    static const unsigned int rows[3] = {4, 9, 17};
    static struct windows *standard_table_scratch;
    struct windows *const standard_table_at = windows_scratch_table(&standard_table_scratch);
    struct windows address_to kept_table = compositor.table;
    struct taskbar_view view;
    int placed = 1, mirrored = 1, rect[4], x, y, w, h, bx, by, bw, bh, had_launcher = taskbar.launcher;

    windows_scratch_zero(standard_table_at);
    standard_table_at->themed = 1, standard_table_at->theme = 1, standard_table_at->accent = 0xffffffu;
    compositor.table = standard_table_at;
    taskbar_launcher_set(0);
    for (unsigned int position = TASKBAR_BOTTOM; position <= TASKBAR_RIGHT; position++)
        for (int each = 0; each < 3; each++)
            for (int rtl = 0; rtl < 2; rtl++)
            {
                int m = position >= TASKBAR_LEFT, scale = scales[each], thick = COMPOSITOR_PANEL * scale / 100, high = thick - 2 * (2 * scale / 100 > 1 ? 2 * scale / 100 : 1);
                int gap = UI_GAP2 * scale / 100;
                int pad = TASKBAR_PADDING_STANDARD * scale / 100, side = 16 * scale / 100 > high - 4 * scale / 100 ? high - 4 * scale / 100 : 16 * scale / 100;
                const struct ui_tree address_to t;

                language_take(rtl ? ('a' | 'r' << 8) : 0);
                taskbar_settings_set(words[position], "primary", "battery none\n");
                view = band_view(position, null);
                view.scale = scale;
                t = taskbar_tree(&view);
                for (int tab = 0; tab < 3; tab++)
                {
                    const struct ui_node address_to n = taskbar_node(&view, tab), address_to icon = n && n->first >= 0 ? &t->node[n->first] : null;
                    const struct ui_node address_to label = icon && icon->next >= 0 ? &t->node[icon->next] : null;
                    int turned = rtl && !m;

                    if (!n || !icon || !label)
                    {
                        placed = 0;
                        continue;
                    }
                    // Across the band the icon is in the middle of the tab; along it, the padding in from the leading end, which is the far end reading from the right.
                    placed &= icon->w == side && icon->h == side && (m ? icon->x - n->x : icon->y - n->y) == (high - side) / 2 &&
                              (m ? icon->y - n->y == pad : turned ? n->x + n->w - (icon->x + icon->w) == pad : icon->x - n->x == pad);
                    // The title has all the icon leaves, to the tab's other end, and the tab's whole height.
                    placed &= m ? label->y == icon->y + icon->h + gap && label->y + label->h == n->y + n->h - pad && label->w == high
                                : turned ? label->x == n->x + pad && label->x + label->w == icon->x - gap && label->h == high
                                         : label->x == icon->x + icon->w + gap && label->x + label->w == n->x + n->w - pad && label->h == high;
                    mirrored &= !turned || icon->x > label->x;
                }
            }
    language_take(0);
    check("taskbar identity: a tab's icon is the padding in from its leading end and in its middle across the band, 16 pixels at the scale, and its title has the rest less the gap and the padding, on every edge at 100 to 200 percent",
          placed);
    check("taskbar identity: reading from the right the icon is at the right end of its tab and the title to its left", mirrored);

    // What is drawn in the icon's element, on a bottom band: a picture, a tile, a kit symbol, the window symbol.
    taskbar_settings_set("bottom", "primary", "battery none\n");
    for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
        picture[at] = 0xffe66000u, again[at] = 0xff0060e6u;
    {
        const struct ui_tree address_to t;
        const struct ui_node address_to icon;
        unsigned int tone = ui_icon_tone("org.mozilla.firefox");
        int pictured = 1, tiled = 0, symbol, plain, changed = 0, ix, iy, differ = 0, labelled = 0;

        view = band_view(TASKBAR_BOTTOM, null);
        view.rows = rows, view.icons[0] = picture, view.icon_seqs[0] = 2, view.apps[0] = "org.example.Pictured";
        view.apps[1] = "org.mozilla.firefox", view.apps[2] = "settings";
        taskbar_draw(&view, 7, band_glass, null, rect);
        memory_copy(first, band_pixels, sizeof(first));
        taskbar_band(&view, &bx, &by, &bw, &bh);
        t = taskbar_tree(&view);
        icon = &t->node[taskbar_node(&view, 0)->first], ix = icon->x - bx, iy = icon->y - by;
        for (int row = 0; row < icon->h; row++)
            for (int column = 0; column < icon->w; column++)
                pictured &= (first[(iy + row) * bw + ix + column] & 0xffffffu) == 0xe66000u;
        icon = &t->node[taskbar_node(&view, 1)->first];
        for (int row = 0; row < icon->h; row++)
            for (int column = 0; column < icon->w; column++)
                tiled += (first[(icon->y - by + row) * bw + icon->x - bx + column] & 0xffffffu) == tone;
        check("taskbar identity: a window with a picture has it in its tab's icon, every pixel of it", pictured);
        check("taskbar identity: a program with an app_id and no picture has its lettered tile, in its name's colour", tiled > icon->w * icon->h / 2 && tiled < icon->w * icon->h);
        // The kit's program: the symbol ui_icon draws, in the tab's own ink, where the tab with no identity draws the window symbol.
        view = band_view(TASKBAR_BOTTOM, null);
        taskbar_draw(&view, 7, band_glass, null, rect);
        memory_copy(second, band_pixels, sizeof(second));
        icon = &t->node[taskbar_node(&view, 2)->first];
        for (int row = 0; row < icon->h; row++)
            for (int column = 0; column < icon->w; column++)
                differ += first[(icon->y - by + row) * bw + icon->x - bx + column] != second[(icon->y - by + row) * bw + icon->x - bx + column];
        symbol = differ > 4;
        taskbar_slot(&view, 2, &x, &y, &w, &h);
        // A tab with no title is its icon alone: the same tab drawn with no titles differs from the titled one only past the icon.
        view = band_view(TASKBAR_BOTTOM, null);
        for (unsigned int at = 0; at < 3; at++)
            view.titles[at] = "";
        taskbar_draw(&view, 7, band_glass, null, rect);
        plain = 1;
        for (int tab = 0; tab < 3; tab++)
        {
            const struct ui_node address_to n = taskbar_node(&view, tab);

            icon = &t->node[n->first];
            for (int row = 0; row < n->h; row++)
                for (int column = 0; column < n->w; column++)
                {
                    int px = n->x - bx + column, py = n->y - by + row, in_icon = px >= icon->x - bx && px < icon->x - bx + icon->w;
                    int same = band_pixels[py * bw + px] == second[py * bw + px];

                    plain &= !in_icon || same, labelled += !in_icon && !same;
                }
        }
        check("taskbar identity: a kit program's tab draws its own symbol where a window that says nothing draws the window symbol", symbol);
        check("taskbar identity: the title is written past the icon and never on it", plain && labelled > 30);
        // The picture changes: a new sequence is a new picture in the tab, and the old sequence with other pixels is the one kept.
        view = band_view(TASKBAR_BOTTOM, null);
        view.rows = rows, view.icons[0] = again, view.icon_seqs[0] = 4;
        taskbar_draw(&view, 7, band_glass, null, rect);
        icon = &t->node[taskbar_node(&view, 0)->first];
        changed = (band_pixels[(icon->y - by + 2) * bw + icon->x - bx + 2] & 0xffffffu) == 0x0060e6u;
        check("taskbar identity: a window's new picture, a sequence on, is the one its tab draws next", changed);
        // A window whose picture has no sequence has none: its tab is its tile or its symbol.
        view = band_view(TASKBAR_BOTTOM, null);
        view.rows = rows, view.icons[0] = again, view.icon_seqs[0] = 0;
        taskbar_draw(&view, 7, band_glass, null, rect);
        check("taskbar identity: a picture with no sequence is no picture", (band_pixels[(icon->y - by + 2) * bw + icon->x - bx + 2] & 0xffffffu) != 0x0060e6u);
    }

    // A band too short for its tabs' icons: nothing of a tab is drawn outside it, at any length.
    {
        int held = 1, slots[4][4];

        for (int length = 40; length < 400; length += 7)
        {
            view = band_view(TASKBAR_BOTTOM, null);
            view.width = length, view.rows = rows, view.icons[0] = picture, view.icon_seqs[0] = 2, view.apps[1] = "org.mozilla.firefox";
            for (unsigned int at = 0; at < BAND_W * COMPOSITOR_PANEL; at++)
                band_pixels[at] = 0x00abcdefu;
            taskbar_draw(&view, 7, band_glass, null, rect);
            taskbar_band(&view, &bx, &by, &bw, &bh);
            for (int slot = 0; slot < 4; slot++)
                if (!taskbar_slot(&view, slot < 3 ? slot : -2, &slots[slot][0], &slots[slot][1], &slots[slot][2], &slots[slot][3]))
                    slots[slot][2] = slots[slot][3] = 0;
            for (int row = 0; row < bh; row++)
                for (int column = 0; column < bw; column++)
                {
                    int in = 0;

                    for (int slot = 0; slot < 4; slot++)
                        in |= column >= slots[slot][0] - bx && column < slots[slot][0] - bx + slots[slot][2] && row >= slots[slot][1] - by && row < slots[slot][1] - by + slots[slot][3];
                    held &= in || band_pixels[row * bw + column] == 0x00abcdefu;
                }
        }
        check("taskbar identity: on a band from 40 to 400 pixels long nothing of a tab, its icon or its title is drawn outside the tab", held);
    }
    compositor.table = kept_table;
    taskbar_launcher_set(had_launcher);
}

static void check_taskbar_text(string_address face_path)
{
    static p8 bytes[1 << 19];
    static unsigned int lit[BAND_W * COMPOSITOR_PANEL], bare[BAND_W * COMPOSITOR_PANEL];
    static const char *const sides[3] = {"TTTTTTTT", "shell", "AVATAR To Wa: a title too long"};
    static const char *const cuts[4] = {"AVATAR To Wa: a title too long", "Settings \xe2\x80\x94 T\xc3\xa4st",
                                        "\xc3\xa5\xc3\xa4\xc3\xb6\xc3\xa5\xc3\xa4\xc3\xb6\xc3\xa5\xc3\xa4\xc3\xb6 W", "W"};
    bipolar got = face_path ? file_slurp_regular_at(AT_FDCWD, face_path, bytes, sizeof(bytes), 0) : -1;
    int rect[4], turns = 1, inked = 1, inside = 1, tall = 1, tops = 1, fits = 1, sized = 1, flat[6] = {0}, along[3] = {0};

    if (got <= 0 || font_load(&band_face, bytes, (positive)got, 0) != 0)
    {
        check("taskbar text: the face the tabs are written in loads", false);
        return;
    }
    font_start(&band_engine, band_atlas, 128, 128);
    // A title too long for its tab is cut with dots after it on a bottom or top band, as on a side band: no letter is
    // cut in half at the tab's edge. The same tab drawn with the title fitted beforehand is the same bytes.
    {
        static unsigned int slot_long[200 * 20], slot_fit[200 * 20];
        static const char title[] = "AVATAR To Wa: a title too long";
        struct taskbar_view view = band_view(TASKBAR_BOTTOM, null);
        const font_face address_to faces[1] = {view.face};
        p32 em = (p32)(20 * 11 / 20) << 6;
        char fitted[WINDOWS_TITLE + 4];
        positive n = ui_cell_fit(taskbar_cell(&view, view.width), faces, title, em, 60, fitted, sizeof(fitted));
        bipolar same = 1;

        fitted[n] = 0;
        view.pixels = slot_long;
        ui_cell_line(taskbar_cell(&view, 200), 0, 0, 60, 20, title, 0xffffffu, 0x202020u);
        view.pixels = slot_fit;
        ui_cell_line(taskbar_cell(&view, 200), 0, 0, 60, 20, fitted, 0xffffffu, 0x202020u);
        for (int at = 0; at < 200 * 20; at++)
            same &= slot_long[at] == slot_fit[at];
        check("taskbar text: a bottom band's title too long for its tab is cut with dots, as a side band's is",
              n < sizeof(title) - 1 && same);
    }
    // The band is drawn in the standard theme, as the compositor draws it: dark, with white, the table themed.
    static struct windows *standard_table_scratch;
    struct windows *const standard_table_at = windows_scratch_table(&standard_table_scratch);
    struct windows address_to kept_table = compositor.table;

    windows_scratch_zero(standard_table_at);
    standard_table_at->themed = 1, standard_table_at->theme = 1, standard_table_at->accent = 0xffffffu;
    compositor.table = standard_table_at;
    taskbar.moon_at = 1794865620ll; // the moon and the clock in it at one time, so the bytes are the same at any hour
    for (int edge = 0; edge < 2; edge++)
    {
        p64 hash = 0xcbf29ce484222325ull;

        taskbar_text_set(edge);
        for (unsigned int position = TASKBAR_BOTTOM; position <= TASKBAR_TOP; position++)
        {
            struct taskbar_view view = band_view(position, null);

            taskbar_settings_set(position == TASKBAR_TOP ? "top" : "bottom", "primary", "battery none\n");
            check("taskbar text: a bottom and a top band draw their tabs", taskbar_draw(&view, 7, band_glass, null, rect) == 1);
            hash = band_hash(hash);
        }
        if (hash != BAND_UPRIGHT)
            string_format(log, "  the bottom and top bands hash to %p\n", (positive)hash);
        check("taskbar text: a bottom and a top band are the bytes they were before a side band could turn its text, with center and with edge",
              hash == BAND_UPRIGHT);
        // The way a band turns: center puts a left band's tops to its right, toward the middle, and a right band's to
        // its left; edge the other way; a bottom or top band turns nothing.
        turns &= taskbar_turn(TASKBAR_LEFT) == (edge ? FONT_TOPS_LEFT : FONT_TOPS_RIGHT) &&
                 taskbar_turn(TASKBAR_RIGHT) == (edge ? FONT_TOPS_RIGHT : FONT_TOPS_LEFT) && !taskbar_turn(TASKBAR_BOTTOM) &&
                 !taskbar_turn(TASKBAR_TOP);
    }
    compositor.table = kept_table;
    taskbar.moon_at = 0;
    check("taskbar text: center turns a side band's tops toward the middle of the output, edge toward its edge, a bottom or top band none", turns);

    /* A band on each side with each word, after a bottom one: every tab is written in (the band with its titles is not
       the band without them there), and nothing is written outside the tabs; the text of a tab is taller than it is
       wide, and as long and as thick as the bottom band's of the same title is wide and high, to a pixel; and in a tab
       of capital Ts the column with the most ink, their bars, is on the side the tops face: the right for a left band
       with center and a right band with edge. The moon and the tasks are at the band's start, so a side band's tasks have
       its length as a bottom band's have, and no title the bottom band writes whole is cut there. */
    taskbar.moon_zone = TASKBAR_ZONE_START, taskbar.tasks_zone = TASKBAR_ZONE_START;
    taskbar_changed();
    for (unsigned int position = TASKBAR_BOTTOM; position <= TASKBAR_RIGHT; position += position == TASKBAR_BOTTOM ? 2 : 1)
        for (int edge = 0; edge < 2; edge++)
        {
            static const char *const words[4] = {"bottom", "top", "left", "right"};
            struct taskbar_view view;
            int bx, by, bw, bh, slot[3][4], tops_right = (position == TASKBAR_LEFT) != edge;

            taskbar_settings_set(words[position], "primary", "battery none\n");
            taskbar_text_set(edge);
            band_draw(position, sides, 0, lit);
            band_draw(position, sides, 1, bare);
            view = band_view(position, sides);
            taskbar_band(&view, &bx, &by, &bw, &bh);
            for (int at = 0; at < 3; at++)
            {
                taskbar_slot(&view, at, &slot[at][0], &slot[at][1], &slot[at][2], &slot[at][3]);
                slot[at][0] -= bx, slot[at][1] -= by;
            }
            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++)
                {
                    int in = 0;

                    for (int at = 0; at < 3; at++)
                        in |= x >= slot[at][0] && x < slot[at][0] + slot[at][2] && y >= slot[at][1] && y < slot[at][1] + slot[at][3];
                    inside &= in || lit[y * bw + x] == bare[y * bw + x];
                }
            for (int at = 0; at < 3; at++)
            {
                int left = bw, right = -1, top = bh, bottom = -1, column[BAND_W] = {0}, most = 0;

                for (int y = slot[at][1]; y < slot[at][1] + slot[at][3]; y++)
                    for (int x = slot[at][0]; x < slot[at][0] + slot[at][2]; x++)
                    {
                        int ink = look_luma(lit[y * bw + x]) - look_luma(bare[y * bw + x]);

                        ink = ink < 0 ? -ink : ink;
                        if (!ink)
                            continue;
                        left = x < left ? x : left, right = x > right ? x : right;
                        top = y < top ? y : top, bottom = y > bottom ? y : bottom;
                        column[x - slot[at][0]] += ink;
                    }
                inked &= right >= 0;
                if (position == TASKBAR_BOTTOM)
                {
                    flat[2 * at] = right - left, flat[2 * at + 1] = bottom - top, along[at] = slot[at][2];
                    continue;
                }
                tall &= bottom - top > right - left;
                // A side band shorter than the bottom one gives a tab less room, and its title is cut to it (the fits row below holds
                // that): the size is held for a tab as long as the bottom band's.
                if (slot[at][3] < along[at])
                    continue;
                if (bottom - top - flat[2 * at] > 1 || flat[2 * at] - (bottom - top) > 1 || right - left - flat[2 * at + 1] > 1 || flat[2 * at + 1] - (right - left) > 1)
                    string_format(log, "  taskbar text: tab %p on side %p (edge %p) is %p by %p, the bottom band's %p by %p\n", (positive)at, (positive)position,
                                  (positive)edge, (positive)(bottom - top), (positive)(right - left), (positive)flat[2 * at], (positive)flat[2 * at + 1]),
                    string_format(log, "  taskbar text: its slot %p by %p at %p,%p, the bottom's %p long; ink from %p to %p\n", (positive)slot[at][2], (positive)slot[at][3],
                                  (positive)slot[at][0], (positive)slot[at][1], (positive)along[at], (positive)top, (positive)bottom);
                sized &= bottom - top - flat[2 * at] <= 1 && flat[2 * at] - (bottom - top) <= 1 &&
                         right - left - flat[2 * at + 1] <= 1 && flat[2 * at + 1] - (right - left) <= 1;
                for (int x = 1; x < slot[at][2]; x++)
                    most = column[x] > column[most] ? x : most;
                if (!at)
                    tops &= tops_right ? most + slot[at][0] > (left + right) / 2 : most + slot[at][0] < (left + right) / 2;
            }
            for (unsigned int at = 0; position != TASKBAR_BOTTOM && at < sizeof(cuts) / sizeof(cuts[0]); at++)
                for (int room = 0; room < 240; room += 3)
                    fits &= band_fit_ok(&view, cuts[at], room);
        }
    taskbar_standard();
    check("taskbar text: every tab of a band on a side is written in, both sides, both ways", inked);
    check("taskbar text: and nothing is written outside its tabs", inside);
    check("taskbar text: a side tab's text is taller than it is wide", tall);
    check("taskbar text: and each is the size of the bottom band's: as long as that is wide and as thick as that is high, to a pixel, so a title that fits its tab is not cut",
          sized && flat[0] > flat[1] && flat[1] > 4);
    check("taskbar text: the bars of a tab of capital Ts are on the side their tops face: right for a left band's center and a right band's edge", tops);
    check("taskbar text: a title is cut to fit its tab with dots after a whole character, the longest that fits, or whole when it fits", fits);

    /* The tray of a band on a side is the bottom band's: the battery's slot and then the clock's, each as long along
       the band as the bottom band's is wide, and both written turned, taller than wide and as long as the bottom
       band's text is wide, to a pixel. The battery is set here, charging and full, its widest text. The ink is the
       tray against its glass alone, made again over the same glass. */
    {
        int along[2][2] = {{0, 0}, {0, 0}}, lengths[3][2], laid = 1, written = 1, turned = 1;

        taskbar_classic(); // the tray these rows were written for: every item after the tasks, the clock its cell
        for (unsigned int position = TASKBAR_BOTTOM; position <= TASKBAR_RIGHT; position += position == TASKBAR_BOTTOM ? 2 : 1)
        {
            static const char *const words[4] = {"bottom", "top", "left", "right"};
            struct taskbar_view view;
            int bx, by, bw, bh, slot[2][4], tray[4], vertical = position != TASKBAR_BOTTOM;

            taskbar_settings_set(words[position], "primary", "battery none\n");
            taskbar_text_set(0);
            taskbar.battery_item = 1, taskbar.battery_percent = 100, taskbar.battery_charging = 1;
            view = band_view(position, sides);
            taskbar_band(&view, &bx, &by, &bw, &bh);
            laid &= taskbar_draw(&view, 1ul << TASKBAR_TAB_BITS, band_glass, null, rect) == 1;
            memory_copy(lit, band_pixels, sizeof(band_pixels));
            laid &= taskbar_slot(&view, -2, &tray[0], &tray[1], &tray[2], &tray[3]);
            band_glass(null, rect[0], rect[1], rect[2], rect[3]);
            taskbar_glass_tab(&view, rect[0], rect[1], rect[2], bx, by, bw, tray[0], tray[1], tray[2], tray[3]);
            for (int at = 0; at < 2; at++)
            {
                int left = bw, right = -1, top = bh, bottom = -1;

                laid &= taskbar_slot(&view, -3 - at, &slot[at][0], &slot[at][1], &slot[at][2], &slot[at][3]);
                lengths[position == TASKBAR_BOTTOM ? 0 : position - 1][at] = vertical ? slot[at][3] : slot[at][2];
                along[vertical][at] = vertical ? slot[at][1] : slot[at][0];
                for (int y = slot[at][1] - by; y < slot[at][1] - by + slot[at][3]; y++)
                    for (int x = slot[at][0] - bx; x < slot[at][0] - bx + slot[at][2]; x++)
                        if (look_luma(lit[y * bw + x]) != look_luma(band_pixels[y * bw + x]))
                        {
                            left = x < left ? x : left, right = x > right ? x : right;
                            top = y < top ? y : top, bottom = y > bottom ? y : bottom;
                        }
                written &= right >= 0;
                if (!vertical)
                    flat[2 * at] = right - left, flat[2 * at + 1] = bottom - top;
                else
                    turned &= bottom - top > right - left && bottom - top - flat[2 * at] <= 1 &&
                              flat[2 * at] - (bottom - top) <= 1 && right - left - flat[2 * at + 1] <= 1 &&
                              flat[2 * at + 1] - (right - left) <= 1;
            }
        }
        laid &= along[0][1] < along[0][0] && along[1][1] < along[1][0];
        for (int side = 1; side < 3; side++)
            laid &= lengths[side][0] == lengths[0][0] && lengths[side][1] == lengths[0][1];
        check("taskbar text: a side band's tray is the bottom band's, the battery before the clock and each as long", laid);
        check("taskbar text: its clock and battery are written in", written);
        check("taskbar text: turned, taller than wide and as long and thick as the bottom band's are wide and high, to a pixel", turned);
        taskbar.battery_item = 0;
        taskbar_standard();
    }
    taskbar_text_set(0);
    taskbar_settings_set("bottom", "primary", "battery none\n");
}

/*
        A row of the log for a wallpaper made cold (ui_moon_night): its wall time and its CPU time (every thread's), and each stage's
        (ui_moon_stage_wall and ui_moon_stage_cpu): the sky, the surface's grid, the disc, the craters and the rest.
*/
static void wallpaper_bench_row(int width, int height, const char address_to look, positive wall, positive cpu)
{
    string_format(log, "  wallpaper bench: %p by %p %s cold in %p ms, %p ms of CPU; the sky %p ms (%p of CPU), the grid %p (%p), the disc %p (%p), the craters %p (%p)\n",
                  (positive)width, (positive)height, (string_address)look, wall / 1000000, cpu / 1000000, ui_moon_stage_wall[0] / 1000000,
                  ui_moon_stage_cpu[0] / 1000000, ui_moon_stage_wall[1] / 1000000, ui_moon_stage_cpu[1] / 1000000, ui_moon_stage_wall[2] / 1000000,
                  ui_moon_stage_cpu[2] / 1000000, ui_moon_stage_wall[3] / 1000000, ui_moon_stage_cpu[3] / 1000000);
}

/*
        A bench of the moon's turn (a row of the log, not a check's verdict): one step of it is the moon's square drawn again and nothing
        else, the clock on it too; what that takes at 100 and 200 percent, and so what the turn costs a second at TASKBAR_MOON_STEPS steps
        a second. At rest the turn costs nothing: no step is taken and the loop is not woken for one (check_moon_motion).
*/
static void check_moon_bench(void)
{
    for (int scale = 100; scale <= 200; scale += 100)
    {
        struct taskbar_view view = band_view(TASKBAR_BOTTOM, null);
        int rect[4], steps = 200, drawn = 1;
        positive began, took;

        if (scale == 200)
            view.pixels = launcher_band_pixels, view.band_bytes = sizeof(launcher_band_pixels), view.scale = 200;
        taskbar_settings_set("bottom", "primary", "battery none\n");
        taskbar_draw(&view, ~0ul, band_glass, null, rect);
        began = clock_monotonic_nanoseconds();
        for (int at = 0; at < steps; at++)
        {
            taskbar.moon_step = 1000000 + at, taskbar.moon_turn = (b64)at * UI_ONE / (TASKBAR_MOON_SPIN * TASKBAR_MOON_STEPS);
            drawn &= taskbar_draw(&view, taskbar_bit(TASKBAR_LAUNCHER), band_glass, null, rect) == 1;
        }
        took = clock_monotonic_nanoseconds() - began;
        // And a second turn: each step's disc is kept from the first, a copy of its square.
        {
            positive second = clock_monotonic_nanoseconds(), again;

            for (int at = 0; at < steps; at++)
            {
                taskbar.moon_step = at, taskbar.moon_turn = (b64)at * UI_ONE / (TASKBAR_MOON_SPIN * TASKBAR_MOON_STEPS);
                taskbar_draw(&view, taskbar_bit(TASKBAR_LAUNCHER), band_glass, null, rect);
            }
            second = clock_monotonic_nanoseconds();
            for (int at = 0; at < steps; at++)
            {
                taskbar.moon_step = at, taskbar.moon_turn = (b64)at * UI_ONE / (TASKBAR_MOON_SPIN * TASKBAR_MOON_STEPS);
                taskbar_draw(&view, taskbar_bit(TASKBAR_LAUNCHER), band_glass, null, rect);
            }
            again = clock_monotonic_nanoseconds() - second;
            string_format(log, "  moon bench: the second turn at %p percent, each step's disc kept: %p us a step, %p us of each second\n", (positive)scale,
                          again / (positive)steps / 1000, again / (positive)steps * TASKBAR_MOON_STEPS / 1000);
        }
        string_format(log, "  moon bench: a step of the turn at %p percent (a %p pixel moon, the clock on it) draws in %p us; %p steps a second cost %p us of each second, %p in a thousand\n",
                      (positive)scale, (positive)taskbar_thick(scale), took / (positive)steps / 1000, (positive)TASKBAR_MOON_STEPS,
                      took / (positive)steps * TASKBAR_MOON_STEPS / 1000, took / (positive)steps * TASKBAR_MOON_STEPS / 1000000);
        check("moon bench: each step of the turn draws the moon", drawn && rect[2] < view.width / 4);
    }
    taskbar.moon_turn = 0, taskbar.moon_step = 0;
    taskbar_moon_bake_halt(); // the moon drawn below by the check itself: the bake's thread ended first (the sphere's tables are one for all)
    // Its parts at 32 pixels: the disc, the rim's light and the clock.
    {
        static unsigned int field[64 * 64];
        static const struct ui_moon_look look = {0xff0c0d10u, 0xfff7f5efu, 0, 0xb4e8e8e8u, UI_MOON_RIM | UI_MOON_FULL_SQUARE, 0, 0};
        struct ui ui;
        positive at[4];

        memory_zero(&ui, sizeof(ui));
        ui.pixels = field, ui.pitch = 64, ui.width = 64, ui.height = 64, ui.bottom = 64, ui.scale = 100;
        at[0] = clock_monotonic_nanoseconds();
        for (int k = 0; k < 50; k++)
            ui_moon(&ui, 0, 0, 32, UI_ONE / 2, (b64)k * UI_ONE / 300, &look);
        at[1] = clock_monotonic_nanoseconds();
        for (int k = 0; k < 50; k++)
            ui_moon_rim(&ui, 0, 0, 32, 0xffffffu, 1.2);
        at[2] = clock_monotonic_nanoseconds();
        for (int k = 0; k < 50; k++)
            ui_moon_label(&ui, &band_engine, &band_face, 0, 0, 32, "21", "47", 0.6, 0xffffffu);
        at[3] = clock_monotonic_nanoseconds();
        string_format(log, "  moon bench: at 32 pixels the disc draws in %p us, the rim's light in %p us, the clock in %p us\n", (at[1] - at[0]) / 50000,
                      (at[2] - at[1]) / 50000, (at[3] - at[2]) / 50000);
    }
    // The wallpaper whole, cold, a row a stage: dark and dimmed, the glow, at 1080p and 4K; and light at 1080p.
    {
        static unsigned int wall[3840 * 2160];
        static const int sizes[3][3] = {{1920, 1080, 1}, {1920, 1080, 0}, {3840, 2160, 1}};

        for (int k = 0; k < 3; k++)
        {
            struct ui ui;
            positive at[2], then[2];

            memory_zero(&ui, sizeof(ui));
            ui.pixels = wall, ui.pitch = (unsigned long)sizes[k][0], ui.width = sizes[k][0], ui.height = sizes[k][1], ui.bottom = sizes[k][1], ui.scale = 100;
            ui_moon_stage_clock(at);
            ui_moon_night(&ui, 0, sizes[k][2] ? UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | UI_MOON_PAPER_DIM : 0, 0x5b7fa6u, 1, 0);
            ui_moon_stage_clock(then);
            wallpaper_bench_row(sizes[k][0], sizes[k][1], sizes[k][2] ? "dark and dimmed" : "light", then[0] - at[0], then[1] - at[1]);
        }
    }
}

#ifdef WALLPAPER_BENCH
/*
        The wallpaper's bench alone (checks.c built with -DWALLPAPER_BENCH='"directory/"'): each look made cold at 1080p and 4K, its wall time
        and the process's CPU time (all its threads), and each picture written to the directory as a PPM, so a change to the renderer can be
        timed and its pictures compared with the ones before it. Then the program ends.
*/
static void wallpaper_bench(string_address into)
{
    static unsigned int wall[3840 * 2160];
    static unsigned char rgb[3840 * 2160 * 3];
    static const struct
    {
        const char *name;
        int width, height;
        unsigned int options;
    } looks[] = {{"wall-1080p-dark-dimmed.ppm", 1920, 1080, UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | UI_MOON_PAPER_DIM},
                 {"wall-1080p-light.ppm", 1920, 1080, 0},
                 {"wall-1080p-dark-inverted.ppm", 1920, 1080, UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS},
                 {"wall-4k-dark-dimmed.ppm", 3840, 2160, UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | UI_MOON_PAPER_DIM},
                 {"wall-4k-light.ppm", 3840, 2160, 0}};

    // The button's moon: its map made (cold), then a 32 pixel disc at fifty steps of its turn.
    {
        static unsigned int field[64 * 64];
        static const struct ui_moon_look look = {0xff0c0d10u, 0xfff7f5efu, 0, 0xb4e8e8e8u, UI_MOON_RIM | UI_MOON_FULL_SQUARE, 0, 0};
        struct ui ui;
        positive at[3];

        memory_zero(&ui, sizeof(ui));
        ui.pixels = field, ui.pitch = 64, ui.width = 64, ui.height = 64, ui.bottom = 64, ui.scale = 100;
        at[0] = clock_monotonic_nanoseconds();
        taskbar_moon_bake_halt(); // the moon drawn here and not by the taskbar: the bake's thread ended first (the sphere's tables are one for all)
        ui_moon_map_for(32);
        at[1] = clock_monotonic_nanoseconds();
        for (int k = 0; k < 50; k++)
            ui_moon(&ui, 0, 0, 32, UI_ONE / 2, (b64)k * UI_ONE / 300, &look);
        at[2] = clock_monotonic_nanoseconds();
        string_format(log, "  moon bench: the button's map made in %p ms, the 32 pixel disc in %p us a step\n", (at[1] - at[0]) / 1000000, (at[2] - at[1]) / 50000);
    }
    for (unsigned int k = 0; k < sizeof(looks) / sizeof(looks[0]); k++)
    {
        struct ui ui;
        positive at[2], then[2];
        char path[600], head[64];
        positive said;
        bipolar handle;

        memory_zero(&ui, sizeof(ui));
        ui.pixels = wall, ui.pitch = (unsigned long)looks[k].width, ui.width = looks[k].width, ui.height = looks[k].height, ui.bottom = looks[k].height,
        ui.scale = 100;
        ui_moon_stage_clock(at);
        ui_moon_night(&ui, 0, looks[k].options, 0x5b7fa6u, 1, 0);
        ui_moon_stage_clock(then);
        wallpaper_bench_row(looks[k].width, looks[k].height, looks[k].name, then[0] - at[0], then[1] - at[1]);
        if (!into[0])
            continue;
        for (long at = 0; at < (long)looks[k].width * looks[k].height; at++)
            rgb[at * 3] = (unsigned char)(wall[at] >> 16), rgb[at * 3 + 1] = (unsigned char)(wall[at] >> 8), rgb[at * 3 + 2] = (unsigned char)wall[at];
        string_copy(string_copy((string_address)path, into) + string_length(into), (string_address)looks[k].name);
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle < 0)
            continue;
        string_copy((string_address)head, (string_address) "P6\n");
        said = 3;
        said += positive_into_string((p8 *)head + said, (positive)looks[k].width);
        head[said++] = ' ';
        said += positive_into_string((p8 *)head + said, (positive)looks[k].height);
        string_copy((string_address)head + said, (string_address) "\n255\n");
        said += 5;
        system_write_all((b32)handle, head, said);
        system_write_all((b32)handle, rgb, (positive)looks[k].width * (positive)looks[k].height * 3);
        system_close(handle);
    }
}
#endif

static int settings_row_named(const char address_to label);

static void check_compositor_look(void)
{
    static unsigned int screen[GLASS_SIDE * GLASS_SIDE], glass[64 * 32], band[64 * 32];
    static const char *const good[] = {"#1e1e1e", "1e1e1e", "#FFFFFF", "abc", "#0af", "000000"};
    static const long made[] = {0x1e1e1e, 0x1e1e1e, 0xffffff, 0xaabbcc, 0x00aaff, 0};
    static const char *const bad[] = {"", "#", "12345", "1234567", "#12345g", "red", "##123456", "default", "12 456", "#1e1e1e "};
    static const unsigned int grounds[] = {0x000000, 0xffffff, 0x808080, 0x777777, 0x8c8c8c, 0xff0000, 0x00ff00,
                                           0x0000ff, 0xffff00, 0x336699, 0x1e1e1e, 0xe0e0e0};
    struct windows address_to table;
    unsigned int strays = 0, thin = 0;
    int parsed = 1;

    for (unsigned int at = 0; at < sizeof(good) / sizeof(good[0]); at++)
        parsed &= window_colour_parse((string_address)good[at]) == made[at];
    for (unsigned int at = 0; at < sizeof(bad) / sizeof(bad[0]); at++)
        parsed &= window_colour_parse((string_address)bad[at]) < 0;
    check("look: a colour is #rrggbb or rrggbb or the three digits of either, and nothing else", parsed);

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
        check("look: the table is mapped", false);
        return;
    }
    table = compositor.table;
    table->x[1] = 50;
    table->y[1] = 30;
    table->width[1] = 120;
    table->height[1] = 80;
    glass_paint(1, 0, 0, 120, 80, 0x404040u, true);
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    check("look: the desktop is the default ink with nothing kept", glass_at(2, 2) == COMPOSITOR_INK_DESKTOP &&
          glass_at(200, 200) == COMPOSITOR_INK_DESKTOP && compositor_ground == COMPOSITOR_INK_DESKTOP);

    compositor_ground = 0x336699u;
    compositor_damage(0, 0, GLASS_SIDE, GLASS_SIDE);
    compositor_present();
    for (unsigned int at = 0; at < GLASS_SIDE * GLASS_SIDE; at++)
        strays += (screen[at] & 0xffffffu) == COMPOSITOR_INK_DESKTOP;
    check("look: with another ink none of the default is left, behind the round corners too",
          strays == 0 && glass_at(2, 2) == 0x336699u && glass_at(200, 200) == 0x336699u && glass_at(100, 60) == 0x404040u);
    compositor_ground = COMPOSITOR_INK_DESKTOP;

    // A tab of glass over each ink, lit and not: its title's ink is far enough from it to be read.
    compositor.glass = glass;
    compositor.band = band;
    for (unsigned int at = 0; at < sizeof(grounds) / sizeof(grounds[0]); at++)
        for (unsigned int lit = 0; lit < 2; lit++)
        {
            unsigned int tab, ink;

            for (unsigned int pixel = 0; pixel < 64 * 32; pixel++)
                glass[pixel] = grounds[at];
            struct taskbar_view tv;

            memory_zero(&tv, sizeof(tv));
            tv.pixels = band;
            tv.glass = glass;
            tab = taskbar_glass_tab(&tv, 0, 0, 64, 0, 0, 64, 4, 4, 40, 20);
            ink = window_contrast(tab);
            thin += look_luma(ink) - look_luma(tab) < 100 && look_luma(tab) - look_luma(ink) < 100;
        }
    compositor.glass = compositor.band = 0;
    check("look: a title on a tab of glass is legible over a black, a white, a middling or a coloured desktop", thin == 0);

    /*
        The glass's text, every side and every mean the compositor can tint a glass window to (steps of 15 in each
        channel): the ink, the quiet ink and the ink on each glass well stand out UI_READABLE (4.7:1: the kit's 4.5:1 with
        a margin for the compositor's grain) from the ground. Before the fix the quiet ink fell to 3.5:1 on the dark glass at
        its brightest and 4.1:1 on the light glass at its dimmest, and the ink on a 40% well to 2.5:1 on the dark and 3.8:1
        on the light.
    */
    {
        static const unsigned int percents[] = {16, 24, 36, 40};
        struct ui probe;
        unsigned int short_ink = 0, short_quiet = 0, short_well = 0, tried = 0;

        memory_zero(&probe, sizeof(probe));
        probe.glass = true;
        for (unsigned int side = 0; side < 2; side++)
            for (unsigned int r = 0; r < 256; r += 15)
                for (unsigned int g = 0; g < 256; g += 15)
                    for (unsigned int b = 0; b < 256; b += 15)
                    {
                        unsigned int ground = compositor_glass_tint(r << 16 | g << 8 | b, side);

                        ui_inks(&probe, ground);
                        tried++;
                        short_ink += ui_contrast(probe.ink, ground) < UI_READABLE;
                        short_quiet += ui_contrast(probe.quiet, ground) < UI_READABLE;
                        for (unsigned int k = 0; k < sizeof(percents) / sizeof(percents[0]); k++)
                        {
                            unsigned int well = ui_well(&probe, UI_SECONDARY_CONTAINER, percents[k]);
                            unsigned int alpha = (well >> 24) + ((well >> 24) >> 7);

                            short_well += ui_contrast(probe.ink, window_mix(ground, probe.ink, alpha)) < UI_READABLE;
                        }
                    }
        check("look: on glass the ink, the quiet ink and the ink on each well stand out UI_READABLE from the ground at every tint",
              tried > 0 && short_ink == 0 && short_quiet == 0 && short_well == 0);
    }

    // The pointer: white inside black by default and light, the other way dark; one size for each setting.
    check("look: the pointer is white with a black outline unless it is dark",
          compositor_cursor_ink(0, 0) == 0xffffffu && compositor_cursor_ink(1, 0) == 0u &&
          compositor_cursor_ink(0, 0xff000000u) == 0xffffffffu && compositor_cursor_ink(1, 0xff000000u) == 0xff000000u);
    compositor.cursor.look = 1;
    check("look: light is the default's colours", compositor_cursor_ink(0, 0) == 0xffffffu && compositor_cursor_ink(1, 0) == 0u);
    compositor.cursor.look = 2;
    check("look: dark is black with a white outline", compositor_cursor_ink(0, 0) == 0u && compositor_cursor_ink(1, 0) == 0xffffffu &&
          compositor_cursor_ink(0, 0xff000000u) == 0xff000000u && compositor_cursor_ink(1, 0xff000000u) == 0xffffffffu);
    {
        static unsigned int plane[64 * 64];
        struct compositor_target t = {plane, 64, 64, 64, 0, 0, 64, 64};
        struct compositor_pointer arrow = {.shape = CP_ARROW, .scale = 2 * 256};
        unsigned int edge = 0, fill = 0, other = 0;

        for (unsigned int at = 0; at < 64 * 64; at++)
            plane[at] = 0x555555u;
        compositor_cursor_draw(&t, 0, 0, &arrow, compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));
        for (unsigned int at = 0; at < 64 * 64; at++)
        {
            edge += plane[at] == 0xffffffu;
            fill += plane[at] == 0u;
            other += plane[at] != 0xffffffu && plane[at] != 0u && plane[at] != 0x555555u;
        }
        check("look: a dark arrow's outline is white and its fill black, the 53 and 81 pixels of the arrow each four at twice its size",
              edge == 4 * 53 && fill == 4 * 81 && other == 0);
    }

    /*
        The pointer's size: a percent of the desktop's own size for it (whole device pixels a logical pixel), in 256ths of a pixel a
        cell to the nearest, three times that shaken; the plane holds up to two pixels a cell and the scene draws more.
    */
    {
        struct compositor_pointer p;
        int hot_x, hot_y;

        compositor.scale = 200;
        compositor_pointer_now(&p);
        check("look: with no size read yet the pointer is 100 percent of the desktop's scale, two pixels a cell at 200", p.scale == 2 * 256);
        compositor.cursor.size = 50;
        compositor_pointer_now(&p);
        check("look: and the percent that was asked of it: 50 at a desktop of 200 is one pixel a cell", p.scale == 256);
        compositor.cursor.handle[0] = 7;
        check("look: at one pixel a cell the pointer is the plane's", !compositor_cursor_in_scene());
        compositor.cursor.magnified = 1;
        compositor_pointer_now(&p);
        check("look: shaken it is three times its size, which the plane does not hold: it is drawn", p.scale == 3 * 256 && compositor_cursor_in_scene());
        compositor.cursor.magnified = 0;
        compositor.cursor.size = 200;
        compositor_pointer_now(&p);
        check("look: a plane is too small for 200 percent at a desktop of 200, four pixels a cell, which is drawn", p.scale == 4 * 256 && compositor_cursor_in_scene());
        compositor.cursor.size = 150;
        check("look: 150 percent there, three pixels a cell, is drawn, the dynamic shape's cell being larger than the plane at it", compositor_cursor_in_scene());
        compositor.cursor.size = 100;
        check("look: and 100 percent, two pixels a cell, is the plane's", !compositor_cursor_in_scene());
        compositor.cursor.size = 0;
        compositor.scale = 300;
        check("look: with no size, a desktop at three times has a pointer the plane does not hold either", compositor_cursor_in_scene());

        // Between whole sizes: 80 percent, the standard, is four fifths of the desktop's own, 204.8 256ths a cell rounded to 205 at a
        // desktop of 100 and 409.6 rounded to 410 at 200; shaken, three times that.
        compositor.cursor.size = 80;
        compositor.scale = 100;
        compositor_pointer_now(&p);
        check("look: 80 percent at a desktop of 100 is 0.8 pixels a cell, 205 in 256ths to the nearest", p.scale == 205);
        compositor.scale = 200;
        compositor_pointer_now(&p);
        check("look: and at a desktop of 200 is 1.6 pixels a cell, 410 in 256ths", p.scale == 410);
        compositor.scale = 100;
        compositor.cursor.magnified = 1;
        compositor_pointer_now(&p);
        check("look: shaken, 80 percent is three times its size, 615 in 256ths, which the scene draws", p.scale == 3 * 205 && compositor_cursor_in_scene());
        compositor.cursor.magnified = 0;
        compositor.cursor.size = 200;
        compositor_pointer_now(&p);
        check("look: the plane holds up to two pixels a cell, 200 percent at a desktop of 100", p.scale == 512 && !compositor_cursor_in_scene());
        compositor.cursor.size = 205;
        check("look: and the scene takes any more, 205 percent", compositor_cursor_in_scene());
        compositor.cursor.size = 300;
        compositor.scale = 200;
        compositor.cursor.magnified = 1;
        compositor.cursor.dyn = 1;
        compositor_pointer_now(&p);
        check("look: shaken at 300 percent of a desktop of 200 the dynamic shape is no larger than the largest picture, 12 pixels a cell",
              p.scale == CP_SPAN_MOST / CP_DYN_CELL * 256 && compositor_pointer_span(&p, &hot_x, &hot_y) == CP_SPAN_MOST);
        compositor.cursor.magnified = 0;
        compositor.cursor.dyn = 0;
        compositor.cursor.size = 0;
        compositor.scale = 200;
        compositor.cursor.handle[0] = 0;
        compositor.cursor.soft = 1;
        check("look: with no plane the pointer is drawn", compositor_cursor_in_scene());
        compositor.cursor.soft = 0;
    }

    /*
        What the settings keep of it: a slider of percents, 50 to 300 in steps of 5, 80 when nothing is kept; a file kept before it was
        a percent reads as it meant, auto as 100 and a whole N as N times 100, which a typed word does not; and the size the session
        bus tells a program that draws its own pointer is 24 pixels times the percent over 100.
    */
    {
        int row = settings_row_named("Cursor size (percent, or default)");
        const struct settings_row address_to size = row >= 0 ? &settings_rows[row] : null;
        long value = 0;
        p8 kept[16], shown[48];

        check("look: the pointer's size is a slider of 50 to 300 percent in steps of 5, 80 when nothing is kept",
              size && size->kind == SETTINGS_SLIDER && size->type == SETTING_NUMBER && size->low == 50 && size->high == 300 &&
                  settings_slider_step(size) == 5 && string_equals((string_address)size->standard, (string_address) "80") &&
                  string_equals((string_address)size->unit, (string_address) "percent") && !setting_kept(size, (string_address) "", &value) &&
                  value == 80 && compositor_setting_kept(SETTING_CURSOR_SIZE_PATH, "", &value) == 0 && value == 80);
        check("look: a size kept before it was a percent reads as it meant: 2 as 200, 4 as 400, 1 as 100, and auto, the standard size, as the standard 80",
              size && setting_kept(size, (string_address) "2", &value) && value == 200 && setting_kept(size, (string_address) "4", &value) &&
                  value == 400 && setting_kept(size, (string_address) "1", &value) && value == 100 &&
                  setting_kept(size, (string_address) "auto", &value) && value == 80 &&
                  compositor_setting_kept(SETTING_CURSOR_SIZE_PATH, "2", &value) && value == 200 &&
                  !string_compare(setting_shown(size, (string_address) "2", shown, sizeof(shown)), (string_address) "200 percent") &&
                  !string_compare(setting_shown(size, (string_address) "auto", shown, sizeof(shown)), (string_address) "80 percent") &&
                  !string_compare(setting_shown(size, (string_address) "", shown, sizeof(shown)), (string_address) "80 percent"));
        check("look: and those words typed are refused, as a number outside 50 to 300 is",
              size && !setting_parse(size, (string_address) "2", kept, sizeof(kept), &value) &&
                  !setting_parse(size, (string_address) "auto", kept, sizeof(kept), &value) &&
                  setting_parse(size, (string_address) "125", kept, sizeof(kept), &value) && value == 125);
        check("look: a program that draws its own pointer is told 24 pixels times the percent: 19 at 80, 24 at 100, 72 at 300, 48 for a 2 kept before, 19 for an auto kept before (the standard)",
              setting_cursor_pixels((string_address) "") == 19 && setting_cursor_pixels((string_address) "100") == 24 &&
                  setting_cursor_pixels((string_address) "300") == 72 && setting_cursor_pixels((string_address) "50") == 12 &&
                  setting_cursor_pixels((string_address) "2") == 48 && setting_cursor_pixels((string_address) "auto") == 19);
    }
}


#ifdef TASKBAR_PICTURES
/*
        Pictures of the band (built only with -DTASKBAR_PICTURES='"directory/"', never by a check): for each look the bottom band of a
        1920 by 1080 output at 100 and 200 percent, light and dark, with 3 and 12 windows, and a strip of frames of the moon turning
        and the highlight moving; and the moon as a wallpaper, 1080p and 4K, light and dark. Each is a PPM in device pixels: a wash for
        the wallpaper, the band's plates over it as the compositor puts them (each a pill, its pixels the band's, its corners covered
        by sixteen samples a pixel as compositor_corner_mask's are). The glass under the band is the wash, which is smooth, as a
        wallpaper blurred is. Then the program ends.
*/
#define PICTURES_W 3840
#define PICTURES_THICK 64
#define PICTURES_FRAMES 5
static unsigned int pictures_band[PICTURES_W * PICTURES_THICK], pictures_glass_pixels[(PICTURES_W + 64) * (PICTURES_THICK + 64)];
static unsigned char pictures_out[PICTURES_W * 120 * 3 * PICTURES_FRAMES];
static unsigned int pictures_wall[3840 * 2160];
static int pictures_dark, pictures_height, pictures_width;

static unsigned int pictures_wash(int x, int y)
{
    int r, g, b, along = x * 256 / (pictures_width ? pictures_width : 1), down = y * 256 / (pictures_height ? pictures_height : 1);

    if (pictures_dark)
        r = 18 + along / 6 + down / 16, g = 26 + along / 10, b = 52 + (256 - along) / 5;
    else
        r = 196 + along / 6, g = 206 + (256 - along) / 12, b = 228 - down / 16;
    r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
    return (unsigned int)(r << 16 | g << 8 | b);
}

static int pictures_glass(void *context, int x, int y, int width, int height)
{
    (void)context;
    for (int row = 0; row < height; row++)
        for (int column = 0; column < width; column++)
            pictures_glass_pixels[row * width + column] = pictures_wash(x + column, y + row);
    return width;
}

// How much of a pixel a pill covers: sixteen samples.
static int pictures_cover(int px, int py, int x, int y, int w, int h)
{
    int r = (w < h ? w : h) * 8, inside = 0;

    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++)
        {
            int fx = (px - x) * 16 + sx * 4 + 2, fy = (py - y) * 16 + sy * 4 + 2, cx, cy;

            if (fx < 0 || fy < 0 || fx >= w * 16 || fy >= h * 16)
                continue;
            cx = fx < r ? r : fx > w * 16 - r ? w * 16 - r : fx;
            cy = fy < r ? r : fy > h * 16 - r ? h * 16 - r : fy;
            inside += (fx - cx) * (fx - cx) + (fy - cy) * (fy - cy) <= r * r;
        }
    return inside;
}

static void pictures_write(const char *path, const unsigned char *rgb, int width, int height)
{
    char header[64];
    positive length = 3;
    bipolar handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    if (handle < 0)
        return;
    string_copy((string_address)header, (string_address) "P6\n");
    length += positive_into_string((string_address)header + length, (positive)width);
    header[length++] = ' ';
    length += positive_into_string((string_address)header + length, (positive)height);
    string_copy((string_address)header + length, (string_address) "\n255\n");
    length += 5;
    system_write_all((b32)handle, header, length);
    system_write_all((b32)handle, rgb, (positive)width * (positive)height * 3);
    system_close(handle);
}

// The bottom of the screen with the band on it, appended to pictures_out at *at: rows of the screen's width.
static int pictures_shot(const struct taskbar_view *view, positive *at)
{
    int bx, by, bw, bh, top, picture_h, plates[40][5], count = 0, which;

    taskbar_band(view, &bx, &by, &bw, &bh);
    while (count < 40 && taskbar_plate(view, count, &which, &plates[count][0], &plates[count][1], &plates[count][2], &plates[count][3]))
        count++;
    picture_h = bh + 16 * view->scale / 100;
    top = view->height - picture_h;
    for (int y = 0; y < picture_h; y++)
        for (int x = 0; x < view->width; x++)
        {
            unsigned int pixel = pictures_wash(x, top + y);
            int oy = top + y;

            for (int p = 0; p < count; p++)
            {
                int sx = plates[p][0], sy = plates[p][1], sw = plates[p][2], sh = plates[p][3];

                if (x < sx || x >= sx + sw || oy < sy || oy >= sy + sh)
                    continue;
                pixel = window_mix(pixel, pictures_band[(oy - by) * bw + (x - bx)], (unsigned int)(pictures_cover(x, oy, sx, sy, sw, sh) * 16));
            }
            pictures_out[(*at)++] = (unsigned char)(pixel >> 16), pictures_out[(*at)++] = (unsigned char)(pixel >> 8), pictures_out[(*at)++] = (unsigned char)pixel;
        }
    return picture_h;
}

// The band arranged one way: the moon's place, the tasks' place, whether the newest task is at their right, which items are before them.
struct pictures_arrange
{
    int moon, tasks, right;
    unsigned int before;
    int editing, clock_tray;
};

static font_face *pictures_bold; // the bold face the moon's clock is in, when it loaded

static void pictures_one(string_address directory, const char *name, const struct pictures_arrange *arrange, int scale, int dark, int count, int strip)
{
    static const char *const titles[12] = {"Terminal", "Settings", "Files \xe2\x80\x94 /home", "Editor: notes.txt", "Mixer", "Firefox",
                                           "Notices", "Gallery", "Terminal 2", "build.log", "Chromium", "Mail"};
    static const char *const apps[12] = {"terminal", "settings", "files", "editor", "mixer", "org.mozilla.firefox", "notices", "gallery",
                                         "terminal", "editor", "chromium", "org.gnome.Geary"};
    static unsigned char marks[CHECK_TABS], lengths[CHECK_TABS];
    static unsigned int rows[CHECK_TABS];
    static struct windows *table_scratch;
    struct windows *const table_at = windows_scratch_table(&table_scratch);
    struct taskbar_view view;
    char path[512];
    int rect[4], high = 0;
    positive at = 0;

    windows_scratch_zero(table_at);
    table_at->themed = 1, table_at->theme = dark ? UI_THEME_DARK : UI_THEME_LIGHT, table_at->accent = dark ? UI_ACCENT_STANDARD : 0x2563ebu;
    compositor.table = table_at;
    compositor.band_at = 0;
    pictures_dark = dark;
    taskbar.moon_zone = arrange->moon, taskbar.tasks_zone = arrange->tasks, taskbar.tasks_right = arrange->right;
    taskbar.tray_before = arrange->before, taskbar.editing = arrange->editing, taskbar.clock_tray = arrange->clock_tray;
    taskbar_settings_set("bottom", "primary", "battery none\n");
    taskbar_launcher_set(1);
    taskbar_volume_set(1, 60, 0, 0);
    taskbar_notices_set(count > 3, WINDOWS_ISSUE_WARNING);
    taskbar_mic_set(count > 3, 0);
    taskbar.battery_item = 1, taskbar.battery_percent = 82, taskbar.battery_charging = 0;
    memory_zero(&view, sizeof(view));
    check_view_tabs(&view);
    view.width = 1920 * scale / 100, view.height = 1080 * scale / 100, view.scale = scale, view.position = TASKBAR_BOTTOM;
    pictures_height = view.height, pictures_width = view.width;
    view.pixels = pictures_band, view.band_bytes = sizeof(pictures_band), view.glass = pictures_glass_pixels;
    view.face = &band_face, view.bold = pictures_bold, view.engine = &band_engine, view.count = (unsigned int)count, view.marks = marks, view.lengths = lengths, view.rows = rows;
    for (int k = 0; k < count; k++)
    {
        view.titles[k] = titles[k % 12], view.apps[k] = apps[k % 12];
        lengths[k] = compositor_title_length(titles[k % 12], WINDOWS_TITLE), rows[k] = (unsigned int)k;
        marks[k] = k == 1 ? COMPOSITOR_TAB_LIT : k == 2 ? COMPOSITOR_TAB_AWAY : 0;
    }
    // 21:47 on 16 November 2026, UTC: the moon's minute.
    taskbar.moon_at = 1794865620ll;
    ui_kit.test_clock = 1000000000ll;
    memory_zero(pictures_band, sizeof(pictures_band));
    taskbar_changed();
    taskbar_draw(&view, ~0ul, pictures_glass, null, rect);
    ui_kit.test_clock += 1000000000ll;
    taskbar_draw(&view, ~0ul, pictures_glass, null, rect);
    if (!strip)
        high = pictures_shot(&view, &at);
    else
    {
        // The keys go from the second tab to the first; each frame a quarter of the highlight's way on, and the moon 12 minutes on.
        marks[1] = 0, marks[0] = COMPOSITOR_TAB_LIT;
        for (int frame = 0; frame < PICTURES_FRAMES; frame++)
        {
            taskbar_draw(&view, ~0ul, pictures_glass, null, rect);
            high += pictures_shot(&view, &at);
            ui_kit.test_clock += (b64)ui_paced(TASKBAR_LIT_MS * UI_MS) / (PICTURES_FRAMES - 1);
            taskbar.moon_at += 12 * 60;
        }
    }
    string_copy(string_copy((string_address)path, directory) + string_length(directory), (string_address)name);
    pictures_write(path, pictures_out, view.width, high);
    ui_kit.test_clock = 0;
    taskbar.moon_at = 0, taskbar.editing = 0, taskbar.tray_before = 0, taskbar.tasks_right = 0;
    taskbar.moon_zone = TASKBAR_ZONE_CENTRE, taskbar.tasks_zone = TASKBAR_ZONE_SPLIT, taskbar.tray_before = 1u << 1 | 1u << 2, taskbar.clock_tray = 0;
}

// A star's place and brightness from its number: the same sky every time.
static unsigned int pictures_hash(unsigned int n)
{
    n = (n ^ 61) ^ (n >> 16), n *= 9, n ^= n >> 4, n *= 0x27d4eb2d, n ^= n >> 15;
    return n;
}

static void pictures_rgb(const unsigned int *pixels, unsigned char *rgb, long count)
{
    for (long at = 0; at < count; at++)
        rgb[3 * at] = (unsigned char)(pixels[at] >> 16), rgb[3 * at + 1] = (unsigned char)(pixels[at] >> 8), rgb[3 * at + 2] = (unsigned char)pixels[at];
}

static unsigned char pictures_rgb_out[3840 * 2160 * 3];

static void pictures_save(string_address directory, const char *name, const unsigned int *pixels, int width, int height)
{
    char path[512];

    pictures_rgb(pixels, pictures_rgb_out, (long)width * height);
    string_copy(string_copy((string_address)path, directory) + string_length(directory), (string_address)name);
    pictures_write(path, pictures_rgb_out, width, height);
}

// A field of the picture's buffer for moons on their own: the dark of space round them.
static struct ui pictures_field(int width, int height)
{
    struct ui ui;

    for (long at = 0; at < (long)width * height; at++)
        pictures_wall[at] = 0x050608u;
    memory_zero(&ui, sizeof(ui));
    ui.pixels = pictures_wall, ui.pitch = (unsigned long)width, ui.width = width, ui.height = height, ui.top = 0, ui.bottom = height, ui.scale = 100;
    return ui;
}

static const struct ui_moon_look pictures_space = {0xff050608u, 0xfff7f5efu, 0, 0xb4e8e8e8u, UI_MOON_RIM | UI_MOON_EARTHSHINE, 0};

// The moon wallpaper (ui_moon_night) at a size: light or dark, at a strength of fumes.
static void pictures_night(string_address directory, const char *name, int width, int height, int dark, decimal glow, decimal edges)
{
    struct ui ui;

    memory_zero(&ui, sizeof(ui));
    ui.pixels = pictures_wall, ui.pitch = (unsigned long)width, ui.width = width, ui.height = height, ui.top = 0, ui.bottom = height, ui.scale = 100;
    ui_moon_night(&ui, 0, dark ? UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS : 0, 0x5b7fa6u, glow, edges);
    pictures_save(directory, name, pictures_wall, width, height);
}

static font_face band_bold;

// A line of the clock's readings, appended to text at *at: a name and the least contrast in hundredths.
static void pictures_say(char *text, positive *at, const char *name, unsigned int read)
{
    string_copy((string_address)text + *at, (string_address)name);
    *at += string_length((string_address)name);
    text[(*at)++] = ' ';
    *at += positive_into_string((string_address)text + *at, (positive)read);
    text[(*at)++] = '\n';
    text[*at] = 0;
}

static void band_pictures(string_address face_path)
{
    static p8 bytes[1 << 19], bold_bytes[1 << 19];
    static const struct pictures_arrange standard = {TASKBAR_ZONE_CENTRE, TASKBAR_ZONE_SPLIT, 0, 1u << 1 | 1u << 2, 0, 0};
    static char readings[4096];
    positive said = 0;
    bipolar got = face_path ? file_slurp_regular_at(AT_FDCWD, face_path, bytes, sizeof(bytes), 0) : -1;
    bipolar bold = program_argument_count() > 4 ? file_slurp_regular_at(AT_FDCWD, program_argument(4), bold_bytes, sizeof(bold_bytes), 0) : -1;
    char into[512], path[600];

    if (got <= 0 || font_load(&band_face, bytes, (positive)got, 0) != 0)
        return;
    pictures_bold = bold > 0 && font_load(&band_bold, bold_bytes, (positive)bold, 0) == 0 ? &band_bold : null;
    font_start(&band_engine, band_atlas, 128, 128);
    compositor_zero();
    // Every window a tab: 40 windows, their icons alone, and 200, a strip with its chevrons, at 100 percent, dark and light.
    pictures_one((string_address)TASKBAR_PICTURES, "overflow-40-dark.ppm", &standard, 100, 1, 40, 0);
    pictures_one((string_address)TASKBAR_PICTURES, "overflow-200-dark.ppm", &standard, 100, 1, 200, 0);
    pictures_one((string_address)TASKBAR_PICTURES, "overflow-200-light.ppm", &standard, 100, 0, 200, 0);
    // The tasks grouped and separate, and the bar at -12, 0 and +16 from the screen's edge: dark at 200 percent.
    {
        static const char *const names[5] = {"options-grouped.ppm", "options-separate.ppm", "options-edge-minus-12.ppm", "options-edge-0.ppm",
                                             "options-edge-plus-16.ppm"};
        static const int separate[5] = {0, 1, 0, 0, 0}, edges[5] = {0, 0, -12, 0, 16};

        for (int each = 0; each < 5; each++)
        {
            taskbar_bar_set(separate[each], edges[each]);
            pictures_one((string_address)TASKBAR_PICTURES, names[each], &standard, 200, 1, 5, 0);
        }
        taskbar_bar_set(0, 0);
    }
    // The clock's four places, dark at 200 percent in twelve hours: in the moon, none, in the tray and a pill of its own.
    {
        static const char *const names[4] = {"clock-moon.ppm", "clock-none.ppm", "clock-tray.ppm", "clock-tab.ppm"};
        struct pictures_arrange each = standard;

        taskbar_clock_set("{time.12h.hour}:{time.24h.min} {time.ampm}");
        for (int place = 0; place < 4; place++)
        {
            taskbar_arrange_set(TASKBAR_ZONE_CENTRE, 0, 0, 0, 1u << 1 | 1u << 2, 0, place, 60, 1);
            each.clock_tray = taskbar.clock_tray;
            pictures_one((string_address)TASKBAR_PICTURES, names[place], &each, 200, 1, 4, 0);
        }
        taskbar_arrange_set(TASKBAR_ZONE_CENTRE, 0, 0, 0, 1u << 1 | 1u << 2, 0, 0, 60, 0);
        taskbar_clock_set("");
        return;
    }
    string_copy(string_copy((string_address)into, (string_address)TASKBAR_PICTURES) + string_length((string_address)TASKBAR_PICTURES), (string_address) "round9/");
    system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)into, 0755);
    // The bar, light and dark, at 100 and 200 percent, with 8 tasks, its moon as it is and in ink; the clock's reading of each.
    for (int ink = 0; ink < 2; ink++)
        for (int dark = 0; dark < 2; dark++)
            for (int scale = 100; scale <= 200; scale += 100)
            {
                char name[64];
                positive length;

                string_copy((string_address)name, (string_address)(dark ? "bar-dark-" : "bar-light-"));
                length = string_length((string_address)name);
                length += positive_into_string((string_address)name + length, (positive)scale);
                string_copy((string_address)name + length, (string_address)(ink ? "-ink-moon.ppm" : "-realistic-moon.ppm"));
                taskbar.moon_ink = ink;
                pictures_one((string_address)into, name, &standard, scale, dark, 8, 0);
                pictures_say(readings, &said, name, (unsigned int)taskbar.clock_read);
            }
    taskbar.moon_ink = 0;
    // The clock on the moon close up, eight times larger (a pixel a block of eight): at 32 and 64 pixels, on a light glass and a dark
    // one, the moon as it is and in ink, the digits at the standard bulge.
    {
        static const unsigned int grounds[2] = {0xdcdedcu, 0x1d1a2eu};
        static const int sizes[2] = {32, 64};
        int width = 4 * 80, height = 2 * 80;
        struct ui ui;

        memory_zero(&ui, sizeof(ui));
        ui.pixels = pictures_wall, ui.pitch = (unsigned long)width, ui.width = width, ui.height = height, ui.top = 0, ui.bottom = height, ui.scale = 100;
        for (int z = 0; z < 2; z++)
            for (int k = 0; k < 4; k++)
                for (int y = 0; y < 80; y++)
                    for (int x = 0; x < 80; x++)
                        pictures_wall[(long)(z * 80 + y) * width + k * 80 + x] = grounds[k & 1];
        for (int z = 0; z < 2; z++)
            for (int k = 0; k < 4; k++)
            {
                int size = sizes[z], left = k * 80 + (80 - size) / 2, top = z * 80 + (80 - size) / 2, dark = k & 1, ink = k >> 1;
                unsigned int paper, outline, read;
                char name[64];
                positive length;

                if (ink)
                {
                    ui_moon_paper_tones(dark ? UI_MOON_PAPER_DARK : 0, &paper, &outline);
                    for (int y = 0; y < size; y++)
                        for (int x = 0; x < size; x++)
                            pictures_wall[(long)(top + y) * width + left + x] = outline;
                    ui_moon_paper(&ui, left, top, size, UI_ONE * 7 / 60, dark ? UI_MOON_PAPER_DARK : 0, 0);
                }
                else
                {
                    static const struct ui_moon_look bar = {0xff0c0d10u, 0xfff7f5efu, 0, 0xb4e8e8e8u, UI_MOON_RIM | UI_MOON_FULL_SQUARE, 0};

                    taskbar_moon_bake_halt(); // the moon drawn here and not by the taskbar: the bake's thread ended first (the sphere's tables are one for all)
                    ui_moon(&ui, left, top, size, UI_ONE / 2, UI_ONE * 7 / 60, &bar);
                }
                // The round frame the compositor cuts the disc out with: what is outside the circle is the ground again.
                for (int y = 0; y < size; y++)
                    for (int x = 0; x < size; x++)
                    {
                        decimal dx = x + 0.5 - size / 2.0, dy = y + 0.5 - size / 2.0, d = square_root(dx * dx + dy * dy), cover = size / 2.0 + 0.5 - d;
                        unsigned int address_to at = pictures_wall + (long)(top + y) * width + left + x;

                        if (cover < 1)
                            *at = window_mix(grounds[dark], *at, (unsigned int)((cover < 0 ? 0 : cover) * 256));
                    }
                ui_moon_rim(&ui, left, top, size, 0x5b7fa6u, 0.9);
                read = ui_moon_label(&ui, &band_engine, pictures_bold ? pictures_bold : &band_face, left, top, size, "17", "24", 0.6, 0x5b7fa6u);
                string_copy((string_address)name, (string_address)(ink ? "clock-ink-" : "clock-realistic-"));
                length = string_length((string_address)name);
                string_copy((string_address)name + length, (string_address)(dark ? "dark-" : "light-"));
                length = string_length((string_address)name);
                length += positive_into_string((string_address)name + length, (positive)size);
                name[length] = 0;
                pictures_say(readings, &said, name, read);
            }
        pictures_save((string_address)into, "clock-closeups.ppm", pictures_wall, width, height);
        {
            static unsigned int big[4 * 80 * 8 * 2 * 80 * 8];
            int bw = width * 8, bh = height * 8;

            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++)
                    big[(long)y * bw + x] = pictures_wall[(long)(y / 8) * width + x / 8];
            pictures_save((string_address)into, "clock-closeups-8x.ppm", big, bw, bh);
        }
    }
    // The wallpaper in ink: light and dark, 1080p and 4K, with no fumes, subtle ones and stronger ones; and an ultrawide.
    // Light, dark (the ink light on the dark) and dark dimmed (the real arrangement, dim), at 1080p and 4K, with the glow; an ultrawide.
    for (int mode = 0; mode < 3; mode++)
        for (int big = 0; big < 2; big++)
        {
            char name[96];
            static const char *const modes[3] = {"wallpaper-light-", "wallpaper-dark-inverted-", "wallpaper-dark-"};
            if (mode == 1)
                continue;
            struct ui ui;
            int width = big ? 3840 : 1920, height = big ? 2160 : 1080;

            string_copy((string_address)name, (string_address)modes[mode]);
            string_copy((string_address)name + string_length((string_address)name), (string_address)(big ? "4k-glow.ppm" : "1080p-glow.ppm"));
            memory_zero(&ui, sizeof(ui));
            ui.pixels = pictures_wall, ui.pitch = (unsigned long)width, ui.width = width, ui.height = height, ui.top = 0, ui.bottom = height, ui.scale = 100;
            ui_moon_night(&ui, 0, mode == 0 ? 0 : UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | (mode == 2 ? UI_MOON_PAPER_DIM : 0), 0x5b7fa6u, 1, 0);
            pictures_save((string_address)into, name, pictures_wall, width, height);
        }
    // The dimmed moon's maria at three shares of the highlands' tone, side by side to choose from.
    {
        static const decimal seas[3] = {0.35, 0.45, 0.55};
        static const char *const names[3] = {"wallpaper-dark-maria-35-percent-1080p.ppm", "wallpaper-dark-maria-45-percent-1080p.ppm",
                                             "wallpaper-dark-maria-55-percent-1080p.ppm"};

        for (int k = 0; k < 3; k++)
        {
            struct ui ui;

            ui_moon_dim_seas = seas[k];
            memory_zero(&ui, sizeof(ui));
            ui.pixels = pictures_wall, ui.pitch = 1920, ui.width = 1920, ui.height = 1080, ui.top = 0, ui.bottom = 1080, ui.scale = 100;
            ui_moon_night(&ui, 0, UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS | UI_MOON_PAPER_DIM, 0x5b7fa6u, 1, 0);
            pictures_save((string_address)into, names[k], pictures_wall, 1920, 1080);
        }
        ui_moon_dim_seas = 0.45;
    }
    {
        bipolar handle;

        string_copy(string_copy((string_address)path, (string_address)into) + string_length((string_address)into), (string_address) "clock-contrast.txt");
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle >= 0)
        {
            system_write_all((b32)handle, readings, said);
            system_close(handle);
        }
    }
}
#endif




