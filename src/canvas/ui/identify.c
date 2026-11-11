/*
        The identify overlay: `moonwater canvas identify` shows, on each screen, its number as a badge in the screen's
        top left corner for three seconds: black text on a white square, a third of an em from the screen's edges, whole
        on the screen. The number is the one the settings' Display map gives that screen (windows_screen_numbers,
        window.c), so the number on a screen and the number on its rectangle on the map are the same.

            identify INDEX

        One window per screen, started by the verb with the screen's index in the table's list (windows_output's). The
        window is the badge and nothing else: a floating window the size of the badge, placed by WINDOW_PLACE_AT at its
        corner of its screen, opaque. The rest of the screen is the desktop, not frosted. The compositor gives it the
        frame of a floating window (its corners and its line). It has no close button and no tab (WINDOW_NO_CLOSE,
        WINDOW_NO_TAB), and it asks for no keys: Alt-Tab and the refocus pass over it (compositor_cycles), so it
        normally has none. It asks for no grab (WINDOW_NO_GRAB): the badge is the whole window, so a press on its top
        band is the badge's press, not the titlebar's (a drag, or a double click that fills the screen).

        It goes away after IDENTIFY_SECONDS, or at once when the badge is pressed, or when a key goes down while it has
        the keys (should it have them). Each screen's window goes on its own: a press on one leaves the others.
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

// The badge as it is fitted to a screen: size is its text's points, em its size in pixels at the screen's scale, x and
// y its corner's margin from the screen's top left, and width and height the badge's own, which is the window's.
struct identify_badge
{
        int size, em, x, y, width, height;
};

static struct identify
{
        struct window_row own;
        struct ui ui;
        unsigned int screen;   // the table's index of this window's screen
        unsigned int number;   // the number drawn: the screen's, from windows_screen_numbers
        char text[2];          // number as one digit, which is all eight screens need, and its null
        struct identify_badge badge;
        struct ui_view view;   // the badge's picture: a column the window's size, its figure centred in it
        int width, height;     // the badge's size, the picture's
        unsigned int ground;   // the window's ground, as the compositor gives it (ui_app_run reads it)
        struct ui_buffers buffers; // the badge's two buffers (the kit's): its picture is drawn whole each frame
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

// The window's size is the badge's, fitted at its start, so the compositor's size is not taken; the scratch holds a
// digit at this scale, whatever the badge's width.
static fn identify_size(unsigned int width, unsigned int height)
{
        unsigned int room = (unsigned int)IDENTIFY_SIZE * (unsigned int)identify.ui.scale / 100;

        (void)width, (void)height;
        ui_scratch_fit(&identify.ui, room);
        identify.dirty = true;
}

/*
        The badge for a screen of width by height: the largest text size from IDENTIFY_SIZE down that fits the screen with
        its margin, so the badge is never cut by an edge; a screen smaller than the lowest size gets that. Its em is the size
        at the screen's scale, so it grows with the scale as the rest of the kit does. Its padding is a sixth of the em, and
        its margin a third of it.
*/
static void identify_badge_fit(struct ui address_to ui, int width, int height, struct identify_badge address_to badge)
{
        const font_face address_to faces[2] = {ui->bold ? ui->bold : ui->regular, ui->regular};

        for (int size = IDENTIFY_SIZE;; size -= 2)
        {
                int em = size * ui->scale / 100, pad = em / 6, margin = em / 3;
                int least_w = WINDOW_MIN_WIDTH * ui->scale / 100, least_h = WINDOW_MIN_HEIGHT * ui->scale / 100;
                int wide = ui_advance(ui, faces, identify.text, 1, (p32)em << 6) + 2 * pad, tall = em + 2 * pad;

                // The compositor makes no window smaller than WINDOW_MIN_WIDTH by WINDOW_MIN_HEIGHT: the badge is padded to
                // that where the screen has room for it with its margin, and is the size it was fitted to where it has not.
                if (wide < least_w && least_w + 2 * margin <= width)
                        wide = least_w;
                if (tall < least_h && least_h + 2 * margin <= height)
                        tall = least_h;
                if ((wide + 2 * margin <= width && tall + 2 * margin <= height) || size <= IDENTIFY_BADGE_LOWEST)
                {
                        badge->size = size, badge->em = em;
                        badge->x = margin, badge->y = margin;
                        badge->width = wide, badge->height = tall;
                        return;
                }
        }
}

// One picture: the badge, white over the whole picture, with the number in its middle. The picture is the badge's size,
// width by height, drawn at stride pixels a row into pixels.
static void identify_paint(struct ui address_to ui, unsigned int address_to pixels, int stride, int width, int height)
{
        ui_begin(ui, pixels, stride, width, height);
        ui_view_begin(&identify.view, ui, width, -height);
        ui_col(.class = "badge", .paint = IDENTIFY_PAPER, .grow = 1)
                ui_text(identify.text, .class = "figure", .size = identify.badge.size, .ink = IDENTIFY_INK | 0xff000000u, .drop = IDENTIFY_DIGIT, .grow = 1);
        ui_view_end(&identify.view);
        ui_view_draw(ui, &identify.view, false);
}

// The badge's picture, drawn whole into the buffer the kit gives (the badge has no rows to draw apart).
static fn identify_picture(unsigned int address_to pixels, int top, int bottom)
{
        (void)top, (void)bottom;
        identify_paint(&identify.ui, pixels, identify.own.table->stride, identify.width, identify.height);
}

// One picture: the badge, whole, into the buffer the compositor is not reading (ui_buffers_present). False when that buffer
// is still being read, and the picture is due again.
// The badge's picture, whole: its band is the whole window.
static fn identify_draw(unsigned int address_to pixels)
{
        identify_picture(pixels, 0, identify.height);
}

static b32 identify_frame(void)
{
        return ui_app_present(&identify.own, &identify.buffers.shown, &identify.dirty, identify.width, identify.height, identify_draw);
}

/*
        The window's start: the badge fitted to its screen, the window placed at the badge's corner of that screen and sized
        as the badge (WINDOW_PLACE_AT with the screen and the size; the compositor reads the placement with the first
        picture), and its time set. The picture is the frame's own.
*/
static int identify_open(void)
{
        struct windows address_to table = identify.own.table;
        unsigned int row = identify.own.row;
        int x, y, width, height;

        identify.screen = windows_output(table, identify.screen, &x, &y, &width, &height);
        identify.number = identify_number(table, identify.screen);
        identify.text[0] = (char)('0' + identify.number % 10), identify.text[1] = 0;
        identify_badge_fit(&identify.ui, width, height, &identify.badge);
        identify.width = identify.badge.width, identify.height = identify.badge.height;
        {
                struct window_place place;

                memory_zero(&place, sizeof(place));
                place.how = WINDOW_PLACE_AT | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE;
                place.x = identify.badge.x, place.y = identify.badge.y;
                place.width = (unsigned int)identify.badge.width, place.height = (unsigned int)identify.badge.height;
                place.screen = identify.screen;
                windows_place_set(table, row, &place); // the placement the compositor reads with the first picture
        }
        identify_size((unsigned int)identify.width, (unsigned int)identify.height);
        identify.until = (positive)ui_kit.now + (positive)IDENTIFY_SECONDS * 1000000000ull;
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
        positive now = (positive)ui_kit.now;

        (void)other;
        if (now < identify.until && (timeout < 0 || (long)(identify.until - now) < timeout))
                timeout = (long)(identify.until - now);
        return timeout;
}

static b32 identify_done(void)
{
        return (positive)ui_kit.now >= identify.until;
}

static struct ui_app identify_app = {
        .own = &identify.own,
        .ui = &identify.ui,
        .missing = (string_address) "identify: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Identify",
        .glass = WINDOW_NO_CLOSE | WINDOW_NO_TAB | WINDOW_NO_GRAB,
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
        ui_buffers_start(&identify.buffers);
        if (program_argument_count() > 1)
        {
                string_address word = program_argument(1);
                identify.screen = (unsigned int)string_digits_max(word, 4, null);
        }
        return ui_app_run(&identify_app);
}

#endif // MOONWATER_CANVAS_IDENTIFY
