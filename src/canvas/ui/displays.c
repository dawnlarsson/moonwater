/*
        The displays, as a window on the compositor in userspace.

        The screens the desktop spans, drawn as a field at the top of the
        window in the place the table says each one is, scaled to fit it. A
        screen is dragged into place: where it lets go it snaps to the edge of
        a neighbour it is beside (displays_snap), and the arrangement is
        applied when the button is let go, never on the way. The rows under
        the field are the settings of the screen chosen: its rotation (0, 90,
        180, 270, and whether Canvas turns it in software or the card does),
        its refresh (preferred, or a rate the screen lists at its size) and
        its position, which the field changes and the row only shows.

        What is written is /root/canvas.displays, one line for each screen
        that has a setting of its own, in the form the compositor reads:

            NAME rotation=0|90|180|270 refresh=preferred|HZ x=X y=Y

        A screen with no setting keeps the line it had, if the file had one,
        so a change to one screen does not drop another's. The compositor
        watches /root and reads the file again when it is written.

        The keys. Tab chooses the next screen, Up and Down the row, Enter or
        Space changes the row (a rotation or a refresh turns to its next
        choice), and Escape closes the window. A press on a screen chooses it
        and a drag moves it; a press on a row changes it. Position is never
        changed from a row: it is the field's.

        One window: a second start finds the first by the program's name and
        lifts it, as the settings window does.

            displays [-f FONT.ttf] [-b BOLD.ttf]

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_DISPLAYS
#define MOONWATER_CANVAS_DISPLAYS

#define DISPLAYS_PATH "/root/canvas.displays"
#define DISPLAYS_WIDTH 560  // logical pixels: the window; the field and the rows are the kit's
#define DISPLAYS_HEIGHT 440
#define DISPLAYS_MARGIN 16  // logical pixels, from the window's edges to the field and the rows
#define DISPLAYS_FIELD 232  // the field's height, logical
#define DISPLAYS_GAP 16     // between the field and the rows, logical
#define DISPLAYS_ROW 40     // a row's height, logical
#define DISPLAYS_ROWS 3     // rotation, refresh, position
#define DISPLAYS_SNAP 8     // logical pixels a drop is pulled to a neighbour's edge (the compositor's COMPOSITOR_SNAP)
#define DISPLAYS_LINE 160   // bytes of one screen's line
#define DISPLAYS_TEXT 256   // bytes of the status line
#define DISPLAYS_FILE 4096  // bytes of the file as it is read: its lines of screens this window does not change are kept
#define DISPLAYS_BUTTON_LEFT 0x110
#define DISPLAYS_KEY_ESCAPE 1
#define DISPLAYS_KEY_TAB 15
#define DISPLAYS_KEY_ENTER 28
#define DISPLAYS_KEY_SPACE 57
#define DISPLAYS_KEY_ENTER_PAD 96
#define DISPLAYS_KEY_UP 103
#define DISPLAYS_KEY_DOWN 108
#define DISPLAYS_ROTATION_ROW 0
#define DISPLAYS_REFRESH_ROW 1
#define DISPLAYS_POSITION_ROW 2

// One screen as the window holds it: the table's rectangle (device pixels, before any turn), its turn, whether
// Canvas does the turn, the refresh asked for (0 is preferred), the rate it is at and the rates it lists at its size.
struct displays_screen
{
        char name[WINDOWS_OUTPUT_NAME];
        int x, y, width, height;
        unsigned int turn, software;
        unsigned int refresh, now;
        unsigned int rates[WINDOWS_OUTPUT_HZ], rate_count;
        b32 edited;                  // a setting of its own was changed here: its line in the file is written from it
};

// The field's scale and origin: a table point at (x, y) is at ox + (x - bx) * f / 1000 in device pixels.
struct displays_view
{
        long bx, by, f, ox, oy;
};

static struct
{
        struct window_row own;
        struct ui ui;
        struct displays_screen screen[WINDOWS_OUTPUTS];
        unsigned int count;
        int selected, row;                // the screen the rows show, and the row the keys are on
        int width, height, shown;
        unsigned int ground;
        b32 focused, dirty;
        b32 dragging;
        int grab_x, grab_y;               // where on the screen the drag took hold, in table pixels
        int target_x, target_y;           // where a screen dragged now would be let go at, snapped
        int pointer_x, pointer_y;
        int field_x, field_y, field_w, field_h;
        char status[DISPLAYS_TEXT];
        char file[DISPLAYS_FILE];         // the file as it was read at the start
        positive file_length;
} displays;

static long displays_abs(long value)
{
        return value < 0 ? -value : value;
}

// A screen's size once its turn is taken: a quarter turn swaps its width and height.
static void displays_turned(const struct displays_screen address_to s, int address_to width, int address_to height)
{
        b32 quarter = s->turn == 90 || s->turn == 270;

        *width = quarter ? s->height : s->width;
        *height = quarter ? s->width : s->height;
}

// The table's rectangle of every screen: the field is drawn to fit it.
static void displays_bounds(const struct displays_screen address_to screens, unsigned int count, long address_to x0,
                            long address_to y0, long address_to x1, long address_to y1)
{
        *x0 = *y0 = 0;
        *x1 = *y1 = 1;
        for (unsigned int at = 0; at < count; at++)
        {
                int width, height;

                displays_turned(&screens[at], &width, &height);
                if (at == 0 || screens[at].x < *x0)
                        *x0 = screens[at].x;
                if (at == 0 || screens[at].y < *y0)
                        *y0 = screens[at].y;
                if (at == 0 || screens[at].x + width > *x1)
                        *x1 = screens[at].x + width;
                if (at == 0 || screens[at].y + height > *y1)
                        *y1 = screens[at].y + height;
        }
}

/*
        The field's view: the screens' bounding box scaled to the largest
        thousandths that fit fw by fh, centred in it. Integer arithmetic only,
        so a checks lane can state the exact rectangles.
*/
static void displays_view_of(const struct displays_screen address_to screens, unsigned int count, int fx, int fy, int fw,
                             int fh, struct displays_view address_to view)
{
        long x0, y0, x1, y1, bw, bh, by_width, by_height, f, across, down;

        displays_bounds(screens, count, &x0, &y0, &x1, &y1);
        bw = x1 - x0 > 0 ? x1 - x0 : 1;
        bh = y1 - y0 > 0 ? y1 - y0 : 1;
        across = (long)fw * 1000 / bw;
        down = (long)fh * 1000 / bh;
        f = across < down ? across : down;
        f = f < 1 ? 1 : f;
        by_width = bw * f / 1000;
        by_height = bh * f / 1000;
        view->bx = x0;
        view->by = y0;
        view->f = f;
        view->ox = fx + ((long)fw - by_width) / 2;
        view->oy = fy + ((long)fh - by_height) / 2;
}

static int displays_field_x(const struct displays_view address_to view, long x)
{
        return (int)(view->ox + (x - view->bx) * view->f / 1000);
}

static int displays_field_y(const struct displays_view address_to view, long y)
{
        return (int)(view->oy + (y - view->by) * view->f / 1000);
}

static long displays_table_x(const struct displays_view address_to view, long px)
{
        return view->bx + (px - view->ox) * 1000 / view->f;
}

static long displays_table_y(const struct displays_view address_to view, long py)
{
        return view->by + (py - view->oy) * 1000 / view->f;
}

// A screen's rectangle on the field, in device pixels; a screen is never less than a pixel.
static void displays_tile(const struct displays_view address_to view, const struct displays_screen address_to s, int address_to x,
                          int address_to y, int address_to width, int address_to height)
{
        int w, h;

        displays_turned(s, &w, &h);
        *x = displays_field_x(view, s->x);
        *y = displays_field_y(view, s->y);
        *width = (int)((long)w * view->f / 1000);
        *height = (int)((long)h * view->f / 1000);
        *width = *width < 1 ? 1 : *width;
        *height = *height < 1 ? 1 : *height;
}

/*
        The snap of a screen dragged to (x, y): where a side of it lies within
        margin of the side of a neighbour it overlaps, it is pulled to that
        side, nearest first; left, right, above and below each. A corner
        touches both sides at once. Answers whether it moved.
*/
static b32 displays_snap(const struct displays_screen address_to screens, unsigned int count, unsigned int index, int address_to x,
                         int address_to y, int margin)
{
        int width, height, best_x = *x, best_y = *y;
        long dist_x = margin + 1, dist_y = margin + 1;
        b32 moved = false;

        displays_turned(&screens[index], &width, &height);
        for (unsigned int at = 0; at < count; at++)
        {
                int other_w, other_h;
                long d;

                if (at == index)
                        continue;
                displays_turned(&screens[at], &other_w, &other_h);
                if (*y < screens[at].y + other_h && *y + height > screens[at].y)
                {
                        d = displays_abs(*x - (screens[at].x + other_w));
                        if (d <= margin && d < dist_x)
                                dist_x = d, best_x = screens[at].x + other_w;
                        d = displays_abs(*x + width - screens[at].x);
                        if (d <= margin && d < dist_x)
                                dist_x = d, best_x = screens[at].x - width;
                }
                if (*x < screens[at].x + other_w && *x + width > screens[at].x)
                {
                        d = displays_abs(*y - (screens[at].y + other_h));
                        if (d <= margin && d < dist_y)
                                dist_y = d, best_y = screens[at].y + other_h;
                        d = displays_abs(*y + height - screens[at].y);
                        if (d <= margin && d < dist_y)
                                dist_y = d, best_y = screens[at].y - height;
                }
        }
        if (best_x != *x || best_y != *y)
                moved = true;
        *x = best_x;
        *y = best_y;
        return moved;
}

// Appends text at at (and a null after it), as far as room allows: its length.
static positive displays_add(char address_to into, positive room, positive at, const char address_to text)
{
        for (positive n = 0; text[n] && at + 1 < room; n++)
                into[at++] = text[n];
        into[at] = 0;
        return at;
}

static positive displays_add_number(char address_to into, positive room, positive at, long value)
{
        char digits[24];
        positive count = 0;
        unsigned long magnitude = value < 0 ? (unsigned long)(-(value + 1)) + 1 : (unsigned long)value;

        if (value < 0)
                at = displays_add(into, room, at, "-");
        do
                digits[count++] = (char)('0' + magnitude % 10), magnitude /= 10;
        while (magnitude && count < sizeof(digits));
        while (count && at + 1 < room)
                into[at++] = digits[--count];
        into[at] = 0;
        return at;
}

// A screen's line in the file's form, with its newline: the length, or 0 when room does not take it.
static positive displays_line(char address_to into, positive room, const struct displays_screen address_to s)
{
        positive at = 0;

        at = displays_add(into, room, at, s->name);
        at = displays_add(into, room, at, " rotation=");
        at = displays_add_number(into, room, at, (long)s->turn);
        at = displays_add(into, room, at, " refresh=");
        at = s->refresh ? displays_add_number(into, room, at, (long)s->refresh) : displays_add(into, room, at, "preferred");
        at = displays_add(into, room, at, " x=");
        at = displays_add_number(into, room, at, (long)s->x);
        at = displays_add(into, room, at, " y=");
        at = displays_add_number(into, room, at, (long)s->y);
        at = displays_add(into, room, at, "\n");
        return at + 1 < room ? at : 0;
}

// The turn after a turn: 0, 90, 180, 270, 0.
static unsigned int displays_turn_next(unsigned int turn)
{
        return turn >= 270 ? 0 : (turn + 90);
}

// The refresh after one: preferred, then each rate the screen lists at its size in order, then preferred again.
static unsigned int displays_refresh_next(const struct displays_screen address_to s)
{
        for (unsigned int at = 0; at < s->rate_count; at++)
                if (s->refresh == s->rates[at])
                        return at + 1 < s->rate_count ? s->rates[at + 1] : 0;
        return s->rate_count ? s->rates[0] : 0;
}

// What a screen's rotation is called in the row: its degrees, and whether Canvas or the card turns it.
static const char address_to displays_turn_word(const struct displays_screen address_to s)
{
        return s->turn == 0 ? "hardware" : s->software ? "software: Canvas turns the picture" : "hardware: the card turns it";
}

// The file as it is now, whole: its lines are written back as they are, except those of screens changed here.
static void displays_file_read(void)
{
        bipolar handle;
        long got;

        displays.file_length = 0;
        handle = system_open_at(AT_FDCWD, (string_address)DISPLAYS_PATH, FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return;
        got = system_read_once((b32)handle, displays.file, sizeof(displays.file) - 1);
        system_close(handle);
        displays.file_length = got > 0 ? (positive)got : 0;
        displays.file[displays.file_length] = 0;
}

// Copies bytes and a newline to into at at, as far as room allows: the new length.
static positive displays_put(char address_to into, positive room, positive at, const char address_to bytes, positive length)
{
        for (positive n = 0; n < length && at + 2 < room; n++)
                into[at++] = bytes[n];
        if (at + 1 < room)
                into[at++] = '\n';
        return at;
}

// The file as the window means it: the lines it had, each replaced by its screen's line when that screen was changed
// here, and a line for each changed screen the file did not have. Answers whether it was written.
static positive displays_compose(char address_to text, positive room)
{
        unsigned char wrote[WINDOWS_OUTPUTS] = {0};
        positive at = 0, start = 0;

        while (start < displays.file_length)
        {
                const char address_to line = displays.file + start;
                positive length = 0;
                int screen = -1;

                while (start + length < displays.file_length && line[length] != '\n')
                        length++;
                for (unsigned int named = 0; named < displays.count && screen < 0; named++)
                {
                        positive name = string_length((string_address)displays.screen[named].name);

                        if (length > name && line[name] == ' ' && memory_compare((address_any)line, (address_any)displays.screen[named].name, name) == 0)
                                screen = (int)named;
                }
                if (screen >= 0 && displays.screen[screen].edited)
                {
                        at += displays_line(text + at, room - at, &displays.screen[screen]);
                        wrote[screen] = 1;
                }
                else
                        at = displays_put(text, room, at, line, length);
                start += length + 1;
        }
        for (unsigned int screen = 0; screen < displays.count; screen++)
                if (displays.screen[screen].edited && !wrote[screen])
                        at += displays_line(text + at, room - at, &displays.screen[screen]);
        return at;
}

static b32 displays_write(void)
{
        char text[DISPLAYS_FILE + DISPLAYS_LINE * WINDOWS_OUTPUTS];
        positive at = displays_compose(text, sizeof(text));
        bipolar handle;

        handle = system_open_at_mode(AT_FDCWD, (string_address)DISPLAYS_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (handle < 0)
        {
                displays_add(displays.status, sizeof(displays.status), 0, "could not write " DISPLAYS_PATH);
                return false;
        }
        if (system_write_all((b32)handle, text, (positive)at) < 0)
        {
                system_close(handle);
                displays_add(displays.status, sizeof(displays.status), 0, "could not write " DISPLAYS_PATH);
                return false;
        }
        system_close(handle);
        displays_add(displays.status, sizeof(displays.status), 0, "saved to " DISPLAYS_PATH);
        return true;
}

static void displays_changed(int screen)
{
        displays.screen[screen].edited = true;
        displays_write();
        displays.dirty = true;
}

// The screens the window shows, from the table: each one's rectangle, its name, the rates it lists and its turn.
static void displays_load(void)
{
        struct windows address_to table = displays.own.table;
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);

        displays.count = count > WINDOWS_OUTPUTS ? WINDOWS_OUTPUTS : count;
        for (unsigned int at = 0; at < displays.count; at++)
        {
                struct displays_screen address_to s = &displays.screen[at];
                int x, y, width, height;

                windows_output(table, at, &x, &y, &width, &height);
                s->x = x, s->y = y, s->width = width, s->height = height;
                memory_copy(s->name, table->output_name[at], WINDOWS_OUTPUT_NAME);
                s->name[WINDOWS_OUTPUT_NAME - 1] = 0;
                s->turn = table->output_turn[at];
                s->software = table->output_software[at];
                s->now = table->output_now[at];
                s->refresh = 0;
                s->rate_count = table->output_rates[at] > WINDOWS_OUTPUT_HZ ? WINDOWS_OUTPUT_HZ : table->output_rates[at];
                memory_copy(s->rates, table->output_hz[at], sizeof(s->rates));
                s->edited = false;
        }
        displays.selected = displays.count ? 0 : -1;
        displays_file_read();
}

// The field's rectangle in device pixels, and its view of the screens.
static void displays_field(struct displays_view address_to view)
{
        displays_view_of(displays.screen, displays.count, displays.field_x, displays.field_y, displays.field_w, displays.field_h, view);
}

// The screen whose tile holds (px, py), or -1: the one on top, the last drawn.
static int displays_tile_at(int px, int py)
{
        struct displays_view view;

        displays_field(&view);
        for (int at = (int)displays.count - 1; at >= 0; at--)
        {
                int x, y, width, height;

                displays_tile(&view, &displays.screen[at], &x, &y, &width, &height);
                if (px >= x && px < x + width && py >= y && py < y + height)
                        return at;
        }
        return -1;
}

// The top of the rows, and a row's height, in device pixels: the rows sit one under the other below the field.
static int displays_rows_top(void)
{
        return displays.field_y + displays.field_h + ui_px(&displays.ui, DISPLAYS_GAP);
}

static int displays_row_height(void)
{
        return ui_px(&displays.ui, DISPLAYS_ROW);
}

// The row under (px, py), or -1.
static int displays_row_at(int px, int py)
{
        for (int at = 0; at < DISPLAYS_ROWS; at++)
        {
                int y = displays_rows_top() + at * displays_row_height();

                if (px >= displays.field_x && px < displays.field_x + displays.field_w && py >= y && py < y + displays_row_height())
                        return at;
        }
        return -1;
}

// The window's layout for its size: the field across the top, the rows under it.
static void displays_layout(void)
{
        int margin = ui_px(&displays.ui, DISPLAYS_MARGIN);

        displays.field_x = margin;
        displays.field_y = margin;
        displays.field_w = displays.width - 2 * margin;
        displays.field_h = ui_px(&displays.ui, DISPLAYS_FIELD);
}

// The value a row shows, as a word: the rotation in degrees, the refresh as preferred or in Hz, the position.
static void displays_row_value(int row, char address_to into, positive room)
{
        const struct displays_screen address_to s = displays.selected >= 0 ? &displays.screen[displays.selected] : null;

        positive at = 0;

        into[0] = 0;
        if (!s)
                return;
        if (row == DISPLAYS_ROTATION_ROW)
        {
                at = displays_add_number(into, room, 0, (long)s->turn);
                displays_add(into, room, at, " degrees");
        }
        else if (row == DISPLAYS_REFRESH_ROW && s->refresh)
        {
                at = displays_add_number(into, room, 0, (long)s->refresh);
                displays_add(into, room, at, " Hz");
        }
        else if (row == DISPLAYS_REFRESH_ROW)
                displays_add(into, room, 0, "preferred");
        else
        {
                at = displays_add(into, room, 0, "x ");
                at = displays_add_number(into, room, at, (long)s->x);
                at = displays_add(into, room, at, ", y ");
                displays_add_number(into, room, at, (long)s->y);
        }
}

// A row's label: its name, and for the rotation the word for who turns it.
static void displays_row_label(int row, char address_to into, positive room)
{
        const struct displays_screen address_to s = displays.selected >= 0 ? &displays.screen[displays.selected] : null;
        positive at = 0;

        if (row == DISPLAYS_ROTATION_ROW)
        {
                at = displays_add(into, room, 0, "Rotation: ");
                displays_add(into, room, at, s ? displays_turn_word(s) : "");
                return;
        }
        displays_add(into, room, 0, row == DISPLAYS_REFRESH_ROW ? "Refresh" : "Position");
}

// Each screen's line in the field: its name, its rate, and its turn's word.
static void displays_draw(unsigned int address_to pixels, unsigned int stride)
{
        struct ui address_to ui = &displays.ui;
        struct displays_view view;
        int radius = ui_px(ui, 8), pad = ui_px(ui, UI_INSET), line = ui_px(ui, ui_types[UI_LABEL_MEDIUM].line);
        int rows = displays_rows_top(), height = displays_row_height();

        ui_begin(ui, pixels, stride, displays.width, displays.height);
        ui_fill(ui, 0, 0, displays.width, displays.height, ui_role_surface());
        ui_round(ui, displays.field_x, displays.field_y, displays.field_w, displays.field_h, radius, ui_role(UI_SURFACE_LOW) | 0xff000000u);
        displays_field(&view);
        for (unsigned int at = 0; at < displays.count; at++)
        {
                struct displays_screen moved = displays.screen[at];
                int x, y, width, h;
                char text[64];
                positive length;

                if (displays.dragging && (int)at == displays.selected)
                        moved.x = displays.target_x, moved.y = displays.target_y;
                displays_tile(&view, &moved, &x, &y, &width, &h);
                ui_round(ui, x, y, width, h, radius, ui_role(UI_SURFACE_HIGHEST) | 0xff000000u);
                if ((int)at == displays.selected)
                        ui_fill(ui, x, y, width, h, (ui_role(UI_PRIMARY) & 0x00ffffffu) | 0x40000000u);
                ui_kit_text(ui, x + pad, ui_kit_baseline(ui, y + pad, UI_LABEL_MEDIUM), (string_address)moved.name, UI_LABEL_MEDIUM,
                            ui_role(UI_ON_SURFACE), 0);
                length = displays_add_number(text, sizeof(text), 0, (long)moved.now);
                displays_add(text, sizeof(text), length, " Hz");
                ui_kit_text(ui, x + pad, ui_kit_baseline(ui, y + pad + line, UI_LABEL_MEDIUM), (string_address)text, UI_LABEL_MEDIUM,
                            ui_role(UI_ON_SURFACE_VARIANT), 0);
                if (moved.turn)
                {
                        displays_add(text, sizeof(text), displays_add_number(text, sizeof(text), 0, (long)moved.turn), " degrees");
                        ui_kit_text(ui, x + pad, ui_kit_baseline(ui, y + pad + 2 * line, UI_LABEL_MEDIUM), (string_address)text,
                                    UI_LABEL_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0);
                }
        }
        for (int row = 0; row < DISPLAYS_ROWS; row++)
        {
                int y = rows + row * height;
                char label[96], value[64];

                if (row == displays.row)
                        ui_fill(ui, displays.field_x, y, displays.field_w, height, ui_role(UI_SELECTION));
                displays_row_label(row, label, sizeof(label));
                displays_row_value(row, value, sizeof(value));
                ui_kit_text(ui, displays.field_x + pad, ui_kit_baseline(ui, y + (height - ui_px(ui, ui_types[UI_BODY_MEDIUM].line)) / 2, UI_BODY_MEDIUM),
                            (string_address)label, UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0);
                ui_kit_text(ui, displays.field_x + displays.field_w - pad - ui_kit_measure(ui, (string_address)value, UI_BODY_MEDIUM),
                            ui_kit_baseline(ui, y + (height - ui_px(ui, ui_types[UI_BODY_MEDIUM].line)) / 2, UI_BODY_MEDIUM),
                            (string_address)value, UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0);
        }
        {
                int y = rows + DISPLAYS_ROWS * height + ui_px(ui, DISPLAYS_GAP);
                const char address_to hint = displays.dragging ? "Let go to place the screen" : "Drag a screen to move it. Enter changes the row.";

                ui_kit_text(ui, displays.field_x, ui_kit_baseline(ui, y, UI_LABEL_MEDIUM), (string_address)hint, UI_LABEL_MEDIUM,
                            ui_role(UI_ON_SURFACE_VARIANT), 0);
                if (displays.status[0])
                        ui_kit_text(ui, displays.field_x, ui_kit_baseline(ui, y + line, UI_LABEL_MEDIUM), (string_address)displays.status,
                                    UI_LABEL_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0);
        }
}

// The window's size: the layout for it, and its picture due.
static void displays_size(unsigned int width, unsigned int height)
{
        displays.width = (int)width;
        displays.height = (int)height;
        ui_scratch_fit(&displays.ui, width);
        displays_layout();
        displays.dirty = true;
}

// A row changed by the keys or a press: a rotation turns to its next, a refresh to its next rate.
static void displays_turn_row(int row)
{
        struct displays_screen address_to s = displays.selected >= 0 ? &displays.screen[displays.selected] : null;

        if (!s || row == DISPLAYS_POSITION_ROW)
                return;
        if (row == DISPLAYS_ROTATION_ROW)
                s->turn = displays_turn_next(s->turn);
        else
                s->refresh = displays_refresh_next(s);
        displays_changed(displays.selected);
}

// A drag's place as the pointer is: the screen's own offset kept, snapped to a neighbour's edge.
static void displays_drag_to(void)
{
        struct displays_view view;
        int tx, ty, margin;

        displays_field(&view);
        tx = (int)(displays_table_x(&view, displays.pointer_x) - displays.grab_x);
        ty = (int)(displays_table_y(&view, displays.pointer_y) - displays.grab_y);
        margin = (int)((long)ui_px(&displays.ui, DISPLAYS_SNAP) * 1000 / view.f);
        displays_snap(displays.screen, displays.count, (unsigned int)displays.selected, &tx, &ty, margin);
        displays.target_x = tx;
        displays.target_y = ty;
        displays.dirty = true;
}

static void displays_press(void)
{
        int at = displays_tile_at(displays.pointer_x, displays.pointer_y), row;

        if (at >= 0)
        {
                struct displays_view view;
                const struct displays_screen address_to s = &displays.screen[at];

                displays_field(&view);
                displays.selected = at;
                displays.dragging = true;
                displays.grab_x = (int)(displays_table_x(&view, displays.pointer_x) - s->x);
                displays.grab_y = (int)(displays_table_y(&view, displays.pointer_y) - s->y);
                displays.target_x = s->x;
                displays.target_y = s->y;
        }
        else if ((row = displays_row_at(displays.pointer_x, displays.pointer_y)) >= 0)
        {
                displays.row = row;
                displays_turn_row(row);
        }
        displays.dirty = true;
}

// Let go: a screen that moved is placed where its drop snapped to, and written; nothing else is.
static void displays_release(void)
{
        struct displays_screen address_to s;

        if (!displays.dragging)
                return;
        displays.dragging = false;
        s = &displays.screen[displays.selected];
        if (displays.target_x != s->x || displays.target_y != s->y)
        {
                s->x = displays.target_x;
                s->y = displays.target_y;
                displays_changed(displays.selected);
        }
        displays.dirty = true;
}

// A key: Tab chooses the next screen, Up and Down the row, Enter and Space change it. False when it closes the window.
static b32 displays_key(int code)
{
        if (code == DISPLAYS_KEY_ESCAPE)
                return false;
        if (code == DISPLAYS_KEY_TAB && displays.count)
                displays.selected = (displays.selected + 1) % (int)displays.count;
        else if (code == DISPLAYS_KEY_UP)
                displays.row = (displays.row + DISPLAYS_ROWS - 1) % DISPLAYS_ROWS;
        else if (code == DISPLAYS_KEY_DOWN)
                displays.row = (displays.row + 1) % DISPLAYS_ROWS;
        else if ((code == DISPLAYS_KEY_ENTER || code == DISPLAYS_KEY_ENTER_PAD || code == DISPLAYS_KEY_SPACE) && displays.selected >= 0)
                displays_turn_row(displays.row);
        displays.dirty = true;
        return true;
}

static b32 displays_event(const struct window_event address_to event)
{
        if (event->type == WINDOW_EVENT_MOTION)
        {
                displays.pointer_x = event->a, displays.pointer_y = event->b;
                if (displays.dragging)
                        displays_drag_to();
                return true;
        }
        if (event->type == WINDOW_EVENT_BUTTON && event->a == DISPLAYS_BUTTON_LEFT)
        {
                if (event->flags & WINDOW_KEY_DOWN)
                        displays_press();
                else
                        displays_release();
                return true;
        }
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN))
                return displays_key(event->a);
        return true;
}

static b32 displays_frame(void)
{
        struct windows address_to table = displays.own.table;
        unsigned int row = displays.own.row;
        int slot = displays.shown < 0 ? 0 : 1 - displays.shown;

        if (displays.shown >= 0 && !windows_ready(&displays.own))
                return false;
        displays_draw(windows_buffer(table, row, (unsigned int)slot), displays.own.stride);
        table->width[row] = (unsigned int)displays.width;
        table->height[row] = (unsigned int)displays.height;
        table->damage_width[row] = 0;
        windows_show(&displays.own, (unsigned int)slot);
        displays.shown = slot;
        displays.dirty = false;
        return true;
}

// The window's start: lifted when one is open, else the screens read from the table and the window centred.
static int displays_start(void)
{
        struct windows address_to table = displays.own.table;
        unsigned int row = displays.own.row;
        int width = ui_px(&displays.ui, DISPLAYS_WIDTH), height = ui_px(&displays.ui, DISPLAYS_HEIGHT);
        int room = (int)table->output_height - ui_px(&displays.ui, 2 * (24 + 24));

        if (ui_lift_other(&displays.own, (string_address) "displays", false))
                return 0;
        displays_load();
        displays_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_centre(table, row, displays.width, displays.height, ui_px(&displays.ui, 12));
        return -1;
}

static struct ui_app displays_app = {
    .own = &displays.own,
    .ui = &displays.ui,
    .missing = (string_address) "displays: no window: the compositor's desktop is not up\n",
    .title = (string_address) "Displays",
    .name = (string_address) "displays",
    .glass = 0,
    .nofont = (string_address) "displays: no font to draw with, pid ",
    .dirty = &displays.dirty,
    .focused = &displays.focused,
    .ground = &displays.ground,
    .open = displays_start,
    .frame = displays_frame,
    .event = displays_event,
    .size = displays_size,
};

/*
        The displays program: -f and -b are the faces to draw in, as the settings window's are. One window: a second
        start finds the first by the program's name and lifts it.
*/
static b32 displays_main()
{
        for (positive at = 1; at + 1 < (positive)program_argument_count(); at++)
        {
                string_address value;

                if ((value = ui_argument(at, (string_address) "-f")))
                        displays_app.font = value;
                else if ((value = ui_argument(at, (string_address) "-b")))
                        displays_app.bold = value;
                else
                        continue;
                at++;
        }
        return ui_app_run(&displays_app);
}

#endif // MOONWATER_CANVAS_DISPLAYS
