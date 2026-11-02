/*
        The identify overlay: `moonwater canvas identify` shows, on each screen, its number in the middle of the screen for
        three seconds. The number is the one the settings' Display map gives that screen (windows_screen_numbers, window.c),
        so the number on a screen and the number on its rectangle on the map are the same.

            identify INDEX

        One window per screen, started by the verb with the screen's index in the table's list (windows_output's). The
        window is glass (WINDOW_GLASS): the screen is frosted behind it and the number is drawn in the ink that stands out
        from that glass. It has no close button and no tab (WINDOW_NO_CLOSE, WINDOW_NO_TAB), and it is placed by the
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
// The digit's size in the kit's points: the kit's scratch line takes UI_SCRATCH_ROWS (96) rows at scale 1, which this fits.
#define IDENTIFY_SIZE 72
// A digit's height is about this many hundredths of its size, so its middle is the window's middle.
#define IDENTIFY_DIGIT 36

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

// The number in the middle of a picture of width by height, drawn at stride pixels a row into pixels, in the ink for the
// glass. Its digit's middle is the picture's middle, and its size is the scale's.
static void identify_paint(struct ui address_to ui, unsigned int address_to pixels, int stride, int width, int height)
{
        int wide, baseline;

        ui_begin(ui, pixels, stride, width, height);
        wide = ui_run(ui, 0, 0, identify.text, IDENTIFY_SIZE, true, ui->ink, 0, 0, ui->width);
        baseline = ui->height / 2 + IDENTIFY_SIZE * ui->scale * IDENTIFY_DIGIT / 100;
        ui_run(ui, (ui->width - wide) / 2, baseline, identify.text, IDENTIFY_SIZE, true, ui->ink, 255, 0, ui->width);
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
