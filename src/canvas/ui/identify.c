/*
        The identify overlay: `moonwater canvas identify` shows, on each screen, its number as a badge in the screen's
        top left corner for three seconds: black text on a white square, flat, the whole of it inside the screen. The number
        is the one the settings' Display map gives that screen (windows_screen_numbers, window.c), so the number on a
        screen and the number on its rectangle on the map are the same.

            identify INDEX

        One window per screen, started by the verb with the screen's index in the table's list (windows_output's). The
        window is glass (WINDOW_GLASS): the screen is frosted behind it, and the badge is the one opaque thing on it. It has
        no close button and no tab (WINDOW_NO_CLOSE, WINDOW_NO_TAB), and it is placed by the
        compositor's own placement, fullscreen on its screen (WINDOW_PLACE_FULLSCREEN with WINDOW_PLACE_SCREEN), so it
        covers the screen, the taskbar too, and is on top as a window is when it first shows. It asks for no keys, and
        Alt-Tab and the refocus pass over it (compositor_cycles), so it normally has none.

        It goes away after IDENTIFY_SECONDS, or at once when its screen is pressed (any button), or when a key goes down
        while it has the keys (should it have them). Each screen's window goes on its own: a press on one leaves the
        others for the rest of their time.
*/
#ifndef MOONWATER_CANVAS_IDENTIFY
#define MOONWATER_CANVAS_IDENTIFY

// How long the window stays, from its first picture, in seconds.
#define IDENTIFY_SECONDS 3
// The badge's text size in the kit's points, at most: the kit's scratch line takes UI_SCRATCH_ROWS (96) rows at scale 1,
// which this fits. A screen too small for it gets a smaller one, down to IDENTIFY_BADGE_LOWEST.
#define IDENTIFY_SIZE 48
#define IDENTIFY_BADGE_LOWEST 8
// A digit's height is about this many hundredths of its size, so its middle is the badge's middle.
#define IDENTIFY_DIGIT 36
// The badge's colours: black text on white, which is 21:1 (WCAG 4.5:1 is the readable floor). Flat, no outline.
#define IDENTIFY_INK 0x000000u
#define IDENTIFY_PAPER 0xffffffffu

static struct identify
{
        struct window_row own;
        struct ui ui;
        unsigned int screen;   // the table's index of this window's screen
        unsigned int number;   // the number drawn: the screen's, from windows_screen_numbers
        char text[2];          // number as one digit, which is all eight screens need, and its null
        int width, height;     // the screen's size, the picture's
        unsigned int ground;   // the glass's ground, as the compositor gives it
        int shown;             // the buffer the last picture went into, or -1 for none yet
        b32 dirty, focused;
        positive until;        // the monotonic clock's time the window goes at
} identify;

// The number is one digit: a screen limit past nine would need a wider text here and in identify_paint.
_Static_assert(WINDOWS_OUTPUTS <= 9, "identify draws one digit per screen");

// The number a screen is shown with: windows_screen_numbers' for the screen windows_output names by index (the primary's
// when the list has no such screen), so it is the number the window is put on.
static unsigned int identify_number(const struct windows address_to table, unsigned int screen)
{
        unsigned int numbers[WINDOWS_OUTPUTS], count = windows_screen_numbers(table, numbers);
        int x, y, width, height;

        screen = windows_output(table, screen, &x, &y, &width, &height);
        return numbers[screen < count ? screen : 0];
}

static fn identify_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = identify.own.table;
        unsigned int room = (unsigned int)IDENTIFY_SIZE * (unsigned int)identify.ui.scale;

        identify.width = (int)(width > table->output_width ? table->output_width : width);
        identify.height = (int)(height > table->output_height ? table->output_height : height);
        // The scratch is at least one em wide, whatever the screen: a run is cut to the scratch's width, and a digit of
        // this size is not as wide as its em, so a screen narrower than the digit still gets all of it, centred.
        ui_scratch_fit(&identify.ui, width > room ? width : room);
        identify.dirty = true;
}

/*
        The badge: a white square in the top left corner, the number centred in it. Its em is the size in points at the
        screen's scale, so it grows with the scale as the rest of the kit does; its padding is a sixth of the em, and it
        keeps a third of an em from the screen's edges. The size is the largest from IDENTIFY_SIZE down that fits the
        screen with its margin, so the badge is never cut by an edge; a screen smaller than the lowest size gets that.
*/
struct identify_badge
{
        int size, em, x, y, width, height;
};

static void identify_badge_fit(struct ui address_to ui, int width, int height, struct identify_badge address_to badge)
{
        const font_face address_to faces[2] = {ui->bold ? ui->bold : ui->regular, ui->regular};

        for (int size = IDENTIFY_SIZE;; size -= 2)
        {
                int em = size * ui->scale, pad = em / 6, margin = em / 3;
                int wide = ui_advance(ui, faces, identify.text, 1, (p32)em << 6) + 2 * pad;

                if ((wide + 2 * margin <= width && em + 2 * pad + 2 * margin <= height) || size <= IDENTIFY_BADGE_LOWEST)
                {
                        badge->size = size, badge->em = em;
                        badge->x = margin, badge->y = margin;
                        badge->width = wide, badge->height = em + 2 * pad;
                        return;
                }
        }
}

// One picture: the badge over the glass in the corner, the rest of the picture clear. The picture is width by height,
// drawn at stride pixels a row into pixels.
static void identify_paint(struct ui address_to ui, unsigned int address_to pixels, int stride, int width, int height)
{
        struct identify_badge badge;
        int wide, baseline;

        identify_badge_fit(ui, width, height, &badge);
        ui_begin(ui, pixels, stride, width, height);
        ui_fill(ui, badge.x, badge.y, badge.width, badge.height, IDENTIFY_PAPER);
        wide = ui_run(ui, 0, 0, identify.text, badge.size, true, IDENTIFY_INK, 0, 0, ui->width);
        baseline = badge.y + badge.height / 2 + badge.em * IDENTIFY_DIGIT / 100;
        ui_run(ui, badge.x + (badge.width - wide) / 2, baseline, identify.text, badge.size, true, IDENTIFY_INK, 255, badge.x,
               badge.x + badge.width);
}

// One picture: the number in the middle of the screen, into the buffer the compositor is not reading. False when that
// buffer is still being read, and the picture is due again.
static b32 identify_frame(void)
{
        struct windows address_to table = identify.own.table;
        unsigned int row = identify.own.row;
        int slot = identify.shown < 0 ? 0 : 1 - identify.shown;

        if (identify.shown >= 0 && !windows_ready(&identify.own))
                return false;
        identify_paint(&identify.ui, windows_buffer(table, row, (unsigned int)slot), table->stride, identify.width, identify.height);
        table->width[row] = (unsigned int)identify.width;
        table->height[row] = (unsigned int)identify.height;
        table->damage_width[row] = 0;
        windows_show(&identify.own, (unsigned int)slot);
        identify.shown = slot;
        identify.dirty = false;
        return true;
}

// The window's start: placed fullscreen on its screen (the compositor reads the placement with the first picture), its
// size the screen's, and its time set. The picture is the frame's own.
static int identify_open(void)
{
        struct windows address_to table = identify.own.table;
        unsigned int row = identify.own.row;
        int x, y, width, height;

        identify.screen = windows_output(table, identify.screen, &x, &y, &width, &height);
        identify.number = identify_number(table, identify.screen);
        identify.text[0] = (char)('0' + identify.number % 10), identify.text[1] = 0;
        table->place_x[row] = table->place_y[row] = 0;
        table->place_width[row] = table->place_height[row] = 0;
        table->place_screen[row] = identify.screen;
        __atomic_store_n(&table->place[row], WINDOW_PLACE_FULLSCREEN | WINDOW_PLACE_SCREEN, __ATOMIC_RELEASE);
        identify_size((unsigned int)width, (unsigned int)height);
        identify.until = clock_monotonic_nanoseconds() + (positive)IDENTIFY_SECONDS * 1000000000ull;
        identify_frame();
        return -1;
}

// A press (any button) or a key going down puts the window away; the keys are its only while it has them.
static b32 identify_event(const struct window_event address_to event)
{
        if ((event->type == WINDOW_EVENT_BUTTON || event->type == WINDOW_EVENT_KEY) && (event->flags & WINDOW_KEY_DOWN))
                return false;
        return true;
}

// The time left shortens the wait to it, so the window is gone when it is due.
static long identify_tick(long timeout, long address_to other)
{
        positive now = clock_monotonic_nanoseconds();

        (void)other;
        if (now < identify.until && (timeout < 0 || (long)(identify.until - now) < timeout))
                timeout = (long)(identify.until - now);
        return timeout;
}

static b32 identify_done(void)
{
        return clock_monotonic_nanoseconds() >= identify.until;
}

static struct ui_app identify_app = {
        .own = &identify.own,
        .ui = &identify.ui,
        .missing = (string_address) "identify: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Identify",
        .glass = WINDOW_GLASS | WINDOW_NO_CLOSE | WINDOW_NO_TAB,
        .nofont = (string_address) "identify: no font to draw with, pid ",
        .dirty = &identify.dirty,
        .focused = &identify.focused,
        .ground = &identify.ground,
        .open = identify_open,
        .frame = identify_frame,
        .event = identify_event,
        .size = identify_size,
        .tick = identify_tick,
        .done = identify_done,
};

// The screen's index is the argument, in decimal; anything that is not one is the primary's.
static b32 identify_main()
{
        memory_zero(&identify, sizeof(identify));
        identify.shown = -1;
        if (program_argument_count() > 1)
        {
                string_address word = program_argument(1);
                unsigned int index = 0;

                for (positive at = 0; word[at] >= '0' && word[at] <= '9' && at < 4; at++)
                        index = index * 10 + (unsigned int)(word[at] - '0');
                identify.screen = index;
        }
        return ui_app_run(&identify_app);
}

#endif // MOONWATER_CANVAS_IDENTIFY
