/*
        test/checks/settings.c: the settings window's model, its rows and its pages.

        One part of the program that the terminal's, the kit frames', the GPU kit's, the draw list's and the GPU model
        kit's sections share (checks.c opens and closes it). The terminal's program, the kit's frames, the draw list
        and the GPU model's kit run with the four faces: fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf
        fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

/*
        The settings window's model, which is the moonwater command's words:
        each row's value read out of what its verb says on a machine (the
        lines below are a guest's, escapes and all), a row a machine has
        none of told from one whose verb said something nobody understood,
        and the command that changes a row made of words and never a line
        for a shell. And the system's pieces (ui.c) draw: a run of text
        lights its pixels at each size the window uses, in either face, a
        switch puts its knob on its side, and a rounded rectangle's corner is
        covered in part and its middle whole.
*/
// The keys a field's checks press, as the compositor numbers them in a key's a.
#define FIELD_KEY_ESCAPE 1
#define FIELD_KEY_BACKSPACE 14
#define FIELD_KEY_TAB 15
#define FIELD_KEY_A 30
#define FIELD_KEY_Z 44
#define FIELD_KEY_ENTER 28
#define FIELD_KEY_HOME 102
#define FIELD_KEY_UP 103
#define FIELD_KEY_LEFT 105
#define FIELD_KEY_RIGHT 106
#define FIELD_KEY_END 107
#define FIELD_KEY_DOWN 108
#define FIELD_KEY_DELETE 111

#if MOONWATER_UI_SETTINGS
static unsigned int settings_parsed(unsigned int row, const char address_to said, char address_to value)
{
        return settings_parse(&settings_rows[row], said, string_length((string_address)said), value, SETTINGS_VALUE);
}

// How many choices a row offers: settings_choice answers each in turn until there are no more.
static positive settings_options(const struct settings_row address_to row)
{
        char word[SETTINGS_VALUE];
        positive count = 0;

        while (settings_choice(row, count, word, sizeof(word)))
                count++;
        return count;
}

static int settings_row_named(const char address_to label)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].label && string_equals((string_address)settings_rows[at].label, (string_address)label))
                        return (int)at;
        return 0;
}

// Whether the row to ask is the one named and no other: the rows are as many bits as the words hold, which is more than one word.
static b32 settings_unread_only(positive row)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_unread_is(at) != (at == row))
                        return false;
        return true;
}

// One key as the window sends it: a character as its code point, in b.
static b32 settings_type_code(unsigned int code)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 0, (int)code};

        return settings_key(&event);
}

// One key as the window sends it: a key in a, none in b.
// An event as the loop delivers it to the settings: the window's pointer is the last motion's place (the kit's ui_kit_track), so a
// motion is recorded before the settings take it. The clock is the check's own, so this is not ui_event_context.
static b32 settings_event_at(const struct window_event address_to event)
{
        ui_kit_track(event);
        return settings_event(event);
}

// The filter's key is the word given (the kit's key of the field, folded and trimmed).
static b32 check_filter_says(const char address_to word)
{
        p8 key[SETTINGS_FILTER];

        settings_filter_text(key, sizeof(key));
        return string_equals((string_address)key, (string_address)word);
}

static b32 settings_press(int key)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, key, 0};

        return settings_key(&event);
}

static positive settings_lit(const unsigned int address_to pixels, positive count)
{
        positive lit = 0;

        for (positive at = 0; at < count; at++)
                lit += pixels[at] >> 24 > 64;
        return lit;
}

/*
        The field a row is typed in, as the checks read and set it: on the kit's field (UI_TEXT_REFUSED is one of its
        words) the engine's text and caret, opened as the row opens it; before it a typed buffer whose caret was its end.
*/
#ifdef UI_TEXT_REFUSED
static positive settings_held(char address_to into, positive room)
{
        into[0] = 0;
        return settings_editing() >= 0 ? ui_text_string(&settings.edit, (p8 address_to)into, room) : 0;
}

static fn settings_hold(int row, const char address_to text)
{
        const struct settings_item address_to item = settings_item(row);

        settings_edit_begin(row, text, item && item->kind == SETTINGS_NETWORK);
}

static p64 settings_caret(void)
{
        return settings_editing() >= 0 ? settings.edit.doc.selections[settings.edit.doc.primary].active : 0;
}
#else
static positive settings_held(char address_to into, positive room)
{
        string_copy_bounded((string_address)into, (string_address)settings.typed, room);
        return string_length((string_address)into);
}

static fn settings_hold(int row, const char address_to text)
{
        ui_field_begin(&settings.edit, &settings.own, (const char address_to) "", row);
        string_copy_bounded((string_address)settings.typed, (string_address)text, sizeof(settings.typed));
}

static p64 settings_caret(void)
{
        return string_length((string_address)settings.typed);
}
#endif

// Whether the window keeps nothing of what was typed in a field that is done with (a passphrase above all).
static b32 settings_kept_nothing(void)
{
#ifdef UI_TEXT_REFUSED
        return settings_editing() < 0 && !settings.edit.doc.added && !settings.edit.doc.selections && !settings.edit.preedit_length;
#else
        b32 wiped = true;

        for (positive at = 0; at < sizeof(settings.typed); at++)
                wiped &= settings.typed[at] == 0;
        return wiped;
#endif
}

static b32 settings_holds(const char address_to want)
{
        char held[SETTINGS_TYPED];

        settings_held(held, sizeof(held));
        return string_equals((string_address)held, (string_address)want);
}

static b32 settings_chord(int key, unsigned int code, unsigned int flags)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | flags, key, (int)code};

        return settings_key(&event);
}

/*
        A row put where a person who types in it has it: the rows of its group listed in a window of the size Settings opens
        at, the keys on the row, and the row in view. A key that comes while the row typed in is out of view ends the typing
        (settings_key), as a press outside its field does, so a check that types in a row shows the row first. The reveal is
        a glide, and the list is put where the glide ends.
*/
static fn settings_show(int row)
{
        settings.ui.scale = 100;
        settings.width = SETTINGS_WIDTH;
        settings.height = SETTINGS_HEIGHT;
        settings_filter_clear();
        settings.page.chosen = settings_group_of(row);
        settings.list.focus = row;
        settings_refilter();
        ui_scroll_set(&settings.list.scroll, (int)(scroll_aim(&settings.list.scroll) >> 8));
}
#endif

/*
        The press and the drag of the system's own look (ui.c): what is pressed shows it from the moment it goes down;
        a press that moves past the slop is a drag and no click; one taken back is no click; a finger reaches a small
        shape from UI_TARGET away; a list thrown goes on, slower and slower, and stops, and one let go still goes
        nowhere.
*/
/*
        The kit's spans past a chunk (ui.c's UI_KIT_LINE): a fill 9000 device pixels wide is drawn in every pixel of both its rows, not cut
        where a chunk ends; and a name's tone (ui_icon_tone) is told apart by what lies past its 256th byte.
*/
#define CHECK_WIDE_SPAN 9000
static unsigned int check_wide_pixels[2 * (CHECK_WIDE_SPAN + 100)];

#if MOONWATER_UI_ANY && defined(UI_TARGET)
static void check_kit_wide_span(void)
{
        struct ui ui;
        struct ui_box box;
        unsigned int argb = 0xff336699u;
        int drawn = 0;

        memory_zero(check_wide_pixels, sizeof(check_wide_pixels));
        memory_zero(&ui, sizeof(ui));
        ui.scale = 100, ui.top = 0, ui.bottom = 2, ui.pixels = check_wide_pixels, ui.pitch = CHECK_WIDE_SPAN + 100;
        ui.width = CHECK_WIDE_SPAN + 100, ui.height = 2;
        box.x0 = 0, box.y0 = 0, box.x1 = CHECK_WIDE_SPAN * 256, box.y1 = 2 * 256, box.rx = 0, box.ry = 0;
        ui_kit_fill(&ui, &box, argb, null);
        for (int x = 0; x < CHECK_WIDE_SPAN; x++)
                drawn += check_wide_pixels[x] == argb && check_wide_pixels[CHECK_WIDE_SPAN + 100 + x] == argb;
        check("kit spans: a fill 9000 device pixels wide is drawn in full, every pixel of both its rows, past the chunk's 8192", drawn == CHECK_WIDE_SPAN);
}

static void check_icon_tone_tail(void)
{
        char long_a[301], long_b[301];

        memory_fill(long_a, 'q', 300), memory_fill(long_b, 'q', 300);
        long_a[300] = long_b[300] = 0;
        long_a[299] = 'x', long_b[299] = 'y';
        check("icons: two names 300 bytes long that agree in their first 256 get two tones, the tail is in the tone",
              ui_icon_tone(long_a) != ui_icon_tone(long_b));
}

static void check_ui_press(void)
{
        struct ui ui = {0};
        struct ui_press press = {0, 0, -1, false, false, false};
        struct ui_press drag;
        struct ui_body list;
        int clicked, total = 0, steps = 0, moved = 0;
        positive now = 1000000000ull;

        ui.scale = 100;
        ui_press_begin(&press, 10, 10, 3, true);
        check("ui press: what a press went down on is pressed at once", ui_pressed(&press, 3) && !ui_pressed(&press, 4));
        check("ui press: a finger that shakes under the slop is still the press",
              !ui_press_move(&ui, &press, 14, 13) && ui_pressed(&press, 3));
        check("ui press: past the slop it is a drag, pressed no more, and no click when it comes up",
              ui_press_move(&ui, &press, 10, 19) && !ui_pressed(&press, 3) && ui_press_end(&press, false) == -1);
        // One slop for every press (the apps and the terminal's finger take this rule): UI_SLOP logical pixels at the scale.
        ui.scale = 200;
        ui_press_begin(&press, 10, 10, 3, true);
        check("ui press: the slop is UI_SLOP logical pixels at the window's scale, for every press alike",
              !ui_press_move(&ui, &press, 10 + UI_SLOP * 2 - 1, 10) && ui_press_move(&ui, &press, 10 + UI_SLOP * 2, 10));
        ui.scale = 100;
        // The wheel's rule for every list and page: a horizontal notch turns nothing, and the region is not touched.
        {
                struct ui_scroll sideways_s;
                struct window_event sideways = {WINDOW_EVENT_WHEEL, 0, 120, 1};

                memory_zero(&sideways_s, sizeof(sideways_s));
                check("ui wheel: a horizontal notch turns no list, and the list is left where it was",
                      !ui_wheel_turn(&ui, &sideways_s, &sideways) && scroll_at(&sideways_s) == 0 && !scroll_gliding(&sideways_s) && sideways_s.notch == 0);
        }
        ui_press_begin(&press, 10, 10, 3, false);
        clicked = ui_press_end(&press, false);
        ui_press_begin(&press, 10, 10, 3, true);
        check("ui press: up where it went down it is a click, and taken back it is none",
              clicked == 3 && ui_press_end(&press, true) == -1 && !ui_pressed(&press, 3));
        // The release is the click only on the target the press went down on, for a pointer's press as for a list's row and a menu's choice.
        ui_press_begin(&press, 10, 10, 3, false);
        check("ui press: a release off the target it went down on is no click", ui_press_release(&press, false, 4) == -1);
        ui_press_begin(&press, 10, 10, 3, false);
        check("ui press: a release on the target it went down on is the click", ui_press_release(&press, false, 3) == 3);
        check("ui press: a finger reaches a 20 pixel shape from 10 pixels off it, a pointer does not",
              ui_hit(&ui, true, 90, 95, 100, 100, 20, 20) && !ui_hit(&ui, false, 90, 95, 100, 100, 20, 20) &&
                  ui_hit(&ui, false, 105, 105, 100, 100, 20, 20) && !ui_hit(&ui, true, 85, 95, 100, 100, 20, 20));

        // A finger's press on a button is the kit's press too: moved past the slop and back, its lift clicks nothing; unmoved, it does.
        {
                static struct ui_control button;
                struct ui_control address_to got_drag, address_to got_tap;

                memory_zero(&button, sizeof(button));
                button.kind = UI_BUTTON, button.label = "Go", button.x = 10, button.y = 10, button.w = 100, button.h = 48;
                ui_control_start(&button);
                ui_kit_pointer(&ui, &button, 1, WINDOW_EVENT_BUTTON, 40, 30, true, WINDOW_KEY_TOUCH);
                ui_kit_pointer(&ui, &button, 1, WINDOW_EVENT_MOTION, 40, 200, false, WINDOW_KEY_TOUCH);
                ui_kit_pointer(&ui, &button, 1, WINDOW_EVENT_MOTION, 40, 30, false, WINDOW_KEY_TOUCH);
                got_drag = ui_kit_pointer(&ui, &button, 1, WINDOW_EVENT_BUTTON, 40, 30, false, WINDOW_KEY_TOUCH);
                ui_kit_pointer(&ui, &button, 1, WINDOW_EVENT_BUTTON, 40, 30, true, WINDOW_KEY_TOUCH);
                got_tap = ui_kit_pointer(&ui, &button, 1, WINDOW_EVENT_BUTTON, 40, 30, false, WINDOW_KEY_TOUCH);
                check("ui press: a finger's press on a button dragged past the slop and back is no click", got_drag == null);
                check("ui press: a finger's press and lift on a button, unmoved, clicks it", got_tap == &button);
        }
        (ui_kit.now = (b64)(now), ui_press_track_start(&drag, 0));
        for (int step = 1; step <= 5; step++)
                moved += (ui_kit.now = (b64)(now += 10000000ull), ui_press_track(&drag, step * 10));
        check("ui press: a dragged list follows the finger pixel for pixel", moved == 50);
        // The release's speed is the hand's velocity (1000 px a second here), and the throw it sets is the body's (ui_body_free): it carries
        // 0.7 of that speed and falls by e in 0.26 s, so it covers about 0.18 s of it (the body's numbers, where the old tracker's throw
        // went at the full speed and fell by e in 0.325 s, 325 px).
        memory_zero(&list, sizeof(list));
        ui_body_free(&list, ui_press_velocity(&drag, now), now, 100, true);
        check("ui press: let go moving, it is thrown", ui_body_going(&list));
        while (ui_body_going(&list) && steps < 1000)
                total += (ui_kit.now = (b64)(now += UI_BODY_FRAME_NS), (int)ui_body_given(&list, now)), steps++;
        check("ui press: and it goes on the way it went, slowing, and stops", total > 150 && total < 200 && steps < 200);
        // The same throw stepped at 60 Hz and at 120 Hz covers the same distance: the body integrates its own steps, so a frame does not
        // change the distance (the floor per step made the faster steps slow down less, before the body).
        {
                struct ui_body throw_at[2];
                int covered[2] = {0, 0};

                for (int which = 0; which < 2; which++)
                {
                        positive frame = which ? 8333333ul : 16666667ul, at = 0;

                        memory_zero(&throw_at[which], sizeof(throw_at[which]));
                        ui_body_free(&throw_at[which], 600, at, 100, true);
                        while (ui_body_going(&throw_at[which]) && at < 5000000000ul)
                                covered[which] += (int)ui_body_given(&throw_at[which], at += frame);
                }
                check("ui press: a finger's throw of 600 pixels a second covers the same distance (about 79) at 60 Hz steps and at 120 Hz",
                      covered[0] > 70 && covered[0] < 90 && (covered[0] > covered[1] ? covered[0] - covered[1] : covered[1] - covered[0]) <= 1);
        }
        (ui_kit.now = (b64)(now), ui_press_track_start(&drag, 0));
        (ui_kit.now = (b64)(now += 10000000ull), ui_press_track(&drag, 30));
        now += 200000000ull;
        memory_zero(&list, sizeof(list));
        ui_body_free(&list, ui_press_velocity(&drag, now), now, 100, true);
        check("ui press: a finger that stopped before it lifted throws nothing", !ui_body_going(&list));
}
#endif

/*
        The launcher's model: what a text names, in what order, and what a row
        starts. The names of PATH are a fixture here, so the list is a plain
        function of them; no key that would start a program is pressed.
*/
#if MOONWATER_UI_LAUNCHER
/* The loop's way to a launcher event: the kit's event context first (the window's pointer is the last motion's place). */
static b32 launcher_send(const struct window_event address_to event)
{
        ui_event_context(event);
        return launcher_event(event);
}

static b32 launcher_typed(unsigned int code)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 0, (int)code};

        return launcher_send(&event); // as the loop sends it: the layout of the list first
}

static b32 launcher_pressed(int key, unsigned int flags)
{
        struct window_event event = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | flags, key, 0};

        return launcher_send(&event);
}

/*
        The launcher's field as the checks read it: its text and its caret. On the kit's field (UI_TEXT_REFUSED is one of
        its words) they are the engine's; before it they were a typed buffer, whose caret was always its end, so the same
        checks build on either and are red on the buffer.
*/
#ifdef UI_TEXT_REFUSED
static fn launcher_clear(void)
{
        ui_text_set(launcher_field(), (const p8 address_to) "", 0);
        launcher_take();
        launcher_changed();
}

static positive launcher_held(char address_to into, positive room)
{
        return ui_text_string(launcher_field(), (p8 address_to)into, room);
}

static p64 launcher_caret(void)
{
        return launcher_field()->doc.selections[launcher_field()->doc.primary].active;
}
#else
static fn launcher_clear(void)
{
        launcher.text[0] = 0;
        launcher_changed();
}

static positive launcher_held(char address_to into, positive room)
{
        string_copy_bounded((string_address)into, (string_address)launcher.text, room);
        return string_length((string_address)into);
}

static p64 launcher_caret(void)
{
        return string_length((string_address)launcher.text);
}
#endif

static b32 launcher_holds(const char address_to want)
{
        char held[LAUNCHER_TEXT];

        launcher_held(held, sizeof(held));
        return string_equals((string_address)held, (string_address)want);
}

static fn launcher_type_text(const char address_to text)
{
        launcher_clear();
        for (positive at = 0; text[at]; at++)
                launcher_typed((unsigned char)text[at]);
}

// Whether the list is exactly these rows: a letter for a kind (w window, p program, c command) and the name or index.
static b32 launcher_list_is(const char address_to want)
{
        char got[256] = "";

        for (positive at = 0; at < launcher.entries; at++)
        {
                const struct launcher_entry address_to entry = &launcher.entry[at];

                string_append_bounded((string_address)got, (string_address)(entry->kind == LAUNCHER_WINDOW ? "w:" : entry->kind == LAUNCHER_PROGRAM ? "p:" : "c"), sizeof(got));
                if (entry->kind == LAUNCHER_WINDOW)
                        string_append_bounded((string_address)got, (string_address)launcher_programs[entry->index].label, sizeof(got));
                else if (entry->kind == LAUNCHER_PROGRAM)
                        string_append_bounded((string_address)got, (string_address)launcher_name(entry->index), sizeof(got));
                string_append_bounded((string_address)got, (string_address) ",", sizeof(got));
        }
        return string_equals((string_address)got, (string_address)want);
}

// The text of a file, into into: its length, 0 for none.
static positive file_text(const char address_to path, char address_to into, positive room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ), got = 0;

        if (handle >= 0)
        {
                got = system_read_once(handle, into, room - 1);
                system_close(handle);
        }
        got = got < 0 ? 0 : got;
        into[got] = 0;
        return (positive)got;
}

// Whether the file has this many lines, waited for up to two seconds: a start is a grandchild's, which the check does not wait on.
static b32 launcher_waits(const char address_to path, int lines)
{
        positive begin = clock_monotonic_nanoseconds();
        char text[512];

        for (;;)
        {
                int seen = 0;

                for (positive at = 0, length = file_text(path, text, sizeof(text)); at < length; at++)
                        seen += text[at] == '\n';
                if (seen >= lines)
                        return true;
                if (clock_monotonic_nanoseconds() - begin > 2000000000ull)
                        return false;
                system_call(syscall(sched_yield));
        }
}

// Whether the file is exactly this text, after the lines it was waited for are all there.
static b32 launcher_started_is(const char address_to path, const char address_to want)
{
        char text[512];

        file_text(path, text, sizeof(text));
        return string_equals((string_address)text, (string_address)want);
}

static fn launcher_check_path(char address_to path, const char address_to directory, const char address_to name)
{
        string_copy((string_address)path, (string_address) "/tmp/mw-launcher-check/");
        string_append_bounded((string_address)path, (string_address)directory, 96);
        string_append_bounded((string_address)path, (string_address) "/", 96);
        string_append_bounded((string_address)path, (string_address)name, 96);
}

static fn check_launcher(void)
{
        static char names[10][LAUNCHER_NAME];
        static unsigned char from[10];
        static unsigned short hashed[LAUNCHER_HASH];
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        static const char address_to const fixture[] = {"ls", "lsblk", "less", "also", "cat", "false", "lsof", "xls", "term-x"};
        string_address words[LAUNCHER_WORDS];
        char full[LAUNCHER_FULL];
        positive count;

        for (positive at = 0; at < sizeof(fixture) / sizeof(fixture[0]); at++)
                string_copy((string_address)names[at], (string_address)fixture[at]);
        memory_zero(&launcher, sizeof(launcher));
        launcher.names = (char address_to)names;
        launcher.count = sizeof(fixture) / sizeof(fixture[0]);
        launcher.from = from;
        launcher.hashed = hashed;
        launcher.dirs[0] = "/bin";
        launcher.dir_count = 1;
        launcher.own.table = table_at;
        table_at->output_width = 1920;
        table_at->output_height = 1080;
        launcher.ui.scale = 100;
        launcher.regular = "/fonts/ui.ttf";
        launcher.mono = "/fonts/mono.ttf";
        launcher.mono_bold = "/fonts/mono-bold.ttf";
        launcher.menu_app = -1, launcher.generation = ~0u;
        // The registry of the kit's programs alone: no bowl is there.
        ui_apps_bowls = "/tmp/mw-launcher-no-bowls", ui_apps.read = false;
        ui_apps_fresh(1);

        check("launcher: a plain word is not a command line",
              !launcher_command_line("") && !launcher_command_line("ls") && !launcher_command_line("htop") &&
                  !launcher_command_line("x-terminal"));
        check("launcher: a blank, a pipe, a redirect, a list, a quote or an expansion makes one",
              launcher_command_line("ls -l") && launcher_command_line("a|b") && launcher_command_line("a>b") &&
                  launcher_command_line("a<b") && launcher_command_line("a;b") && launcher_command_line("a&") &&
                  launcher_command_line("echo $HOME") && launcher_command_line("\"q\"") && launcher_command_line("'q'") &&
                  launcher_command_line("*.c") && launcher_command_line("a`b`") && launcher_command_line("(a)"));
        check("launcher: an assignment and a path are one too", launcher_command_line("A=1") && launcher_command_line("./run") &&
                                                                     launcher_command_line("/bin/ls"));

        check("launcher: a text ranks as the same, as a start, as inside, or not at all",
              launcher_rank("ls", 2, "ls", 2) == 3 && launcher_rank("ls", 2, "lsblk", 5) == 2 &&
                  launcher_rank("ls", 2, "false", 5) == 1 && launcher_rank("ls", 2, "cat", 3) == 0 &&
                  launcher_rank("lsblkx", 6, "lsblk", 5) == 0 && launcher_rank("", 0, "ls", 2) == 0);
        check("launcher: case does not matter, on either side",
              launcher_rank("TERM", 4, "Terminal", 8) == 2 && launcher_rank("kernel LOG", 10, "Kernel log", 10) == 3 &&
                  launcher_rank("inal", 4, "Terminal", 8) == 1);
        check("launcher: case does not matter past ASCII either: Latin-1, Greek and Cyrillic rank as their lower case does",
              launcher_rank("\xc3\x89" "C", 3, "\xc3\xa9" "cran", 6) == 2 && launcher_rank("\xc3\x89", 2, "\xc3\xa9", 2) == 3 &&
                  launcher_rank("\xce\x91\xce\x98", 4, "\xce\xb1\xce\xb8\xce\xb7\xce\xbd\xce\xb1", 10) == 2 &&
                  launcher_rank("\xd0\x9f\xd0\xa0\xd0\x98", 6, "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", 12) == 2 &&
                  launcher_rank("\xd0\xa0\xd0\x98", 4, "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", 12) == 1 &&
                  launcher_rank("\xd0\x9f", 2, "\xc3\xa9", 2) == 0);
        check("launcher: a desktop program is named by its label and by its other words",
              launcher_program_rank("pref", 4, &launcher_programs[0]) == 2 && launcher_program_rank("set", 3, &launcher_programs[0]) == 2 &&
                  launcher_program_rank("dmesg", 5, &launcher_programs[2]) == 3 && launcher_program_rank("xyz", 3, &launcher_programs[1]) == 0);
        check("launcher: the nearer name is listed first: better rank, then shorter, then alphabetical",
              launcher_before(3, "ls", 2, "lsof") && !launcher_before(2, "lsof", 3, "ls") && launcher_before(2, "lsof", 2, "lsblk") &&
                  launcher_before(2, "less", 2, "lsof") && !launcher_before(2, "lsof", 2, "lsof"));
        check("launcher: the system's machinery and the desktop's own names are not programs of PATH",
              launcher_hidden_name("compositor") && launcher_hidden_name("init") && launcher_hidden_name("launcher") &&
                  launcher_hidden_name("settings") && launcher_hidden_name("terminal") && launcher_hidden_name("klog") &&
                  !launcher_hidden_name("ls") && !launcher_hidden_name("setting"));

        launcher_type_text("");
        check("launcher: with nothing typed there are no rows, and the grid is the desktop's programs in their order",
              launcher_list_is("") && launcher.app_count == 6 && launcher.apps[0] == 0 && launcher.apps[1] == 1 && launcher.apps[2] == 2 &&
                  launcher.apps[3] == 3 && launcher.apps[4] == 4 && launcher.apps[5] == 5); // Editor (f197336fd, 11-20) is the sixth kit program
        launcher_type_text("ls");
        check("launcher: a plain word lists the names it is, then starts, then is inside, the shorter first, and the command last",
              launcher_list_is("p:ls,p:lsof,p:lsblk,p:xls,p:also,p:false,c,") && launcher.list.focus == 0);
        launcher_type_text("ls -l");
        check("launcher: a command line is the command row alone", launcher_list_is("c,"));
        launcher_type_text("xyzzy");
        check("launcher: a word no program is named by is the command row alone", launcher_list_is("c,"));
        launcher_type_text("a|b");
        check("launcher: a pipe is the command row alone", launcher_list_is("c,"));
        launcher_type_text("term");
        check("launcher: the desktop's programs a word names come before PATH's, and the command last",
              launcher_list_is("w:Terminal,p:term-x,c,"));
        launcher_type_text("kernel log");
        check("launcher: the whole name of a desktop program is that program even with a blank in it, and the command after",
              launcher_list_is("w:Kernel log,c,"));
        launcher_type_text("kernel log x");
        check("launcher: more than the name is a command line", launcher_list_is("c,"));
        launcher_type_text("SET");
        check("launcher: case is not what names a program", launcher_list_is("w:Settings,c,"));
        check("launcher: a text that is none of the names of PATH lists no more than a desktop program and the command",
              launcher.entries == 2 && launcher.entry[0].kind == LAUNCHER_WINDOW);

        // The list never has more rows than the window has room for.
        {
                static const char address_to const many[] = {"ab1", "ab2", "ab3", "ab4", "ab5", "ab6", "ab7", "ab8", "ab9"};

                for (positive at = 0; at < 9; at++)
                        string_copy((string_address)names[at], (string_address)many[at]);
                launcher.count = 9;
                launcher_type_text("ab");
                check("launcher: the list is no longer than its rows, and its last row is the command's",
                      launcher.entries == LAUNCHER_ROWS && launcher.entry[LAUNCHER_ROWS - 1].kind == LAUNCHER_COMMAND &&
                          launcher_list_is("p:ab1,p:ab2,p:ab3,p:ab4,p:ab5,p:ab6,c,"));
        }

        // A name is kept once, the first directory that has it winning.
        launcher.count = 0;
        check("launcher: a name is kept", launcher_keep("ls", 0) && launcher_keep("cat", 0));
        check("launcher: and not again, from another directory", !launcher_keep("ls", 1) && !launcher_keep("cat", 2) &&
                                                                    launcher_keep("ps", 1) && launcher.count == 3);
        check("launcher: each keeps the directory it came from first",
              from[0] == 0 && from[1] == 0 && from[2] == 1 && string_equals((string_address)launcher_name(2), (string_address) "ps"));
        check("launcher: a name too long for the table is left out",
              !launcher_keep("a-name-that-is-longer-than-any-the-table-keeps", 0) && launcher.count == 3);

        // Keys: typing, moving and clearing, none of which starts a program.
        launcher.count = sizeof(fixture) / sizeof(fixture[0]);
        for (positive at = 0; at < sizeof(fixture) / sizeof(fixture[0]); at++)
                string_copy((string_address)names[at], (string_address)fixture[at]);
        launcher_type_text(" ls");
        check("launcher: a blank typed first is typed, and the list is for the text without it",
              launcher_holds(" ls") && string_equals((string_address)launcher.text, (string_address) "ls") &&
                  launcher_list_is("p:ls,p:lsof,p:lsblk,p:xls,p:also,p:false,c,"));
        launcher_type_text("ls");
        launcher_pressed(UI_KEY_DOWN, 0);
        check("launcher: Down moves to the next row", launcher.list.focus == 1);
        launcher_pressed(UI_KEY_UP, 0);
        launcher_pressed(UI_KEY_UP, 0);
        check("launcher: Up from the first row stays on it: the kit's list keys are held at the ends, they do not go round", launcher.list.focus == 0);
        launcher_pressed(UI_KEY_N, WINDOW_KEY_CONTROL);
        check("launcher: Control-N is Down", launcher.list.focus == 1);
        for (int at = 0; at < 2 * LAUNCHER_ROWS; at++)
                launcher_pressed(UI_KEY_DOWN, 0);
        check("launcher: Down on the last row stays on it", launcher.list.focus == (int)launcher.entries - 1);
        launcher_pressed(UI_KEY_UP, 0);
        check("launcher: Up from the last row is the row above it", launcher.list.focus == (int)launcher.entries - 2);
        launcher_pressed(UI_KEY_DOWN, 0);
        launcher_typed('x');
        check("launcher: a key typed starts the list again from its top", launcher.list.focus == 0 && string_equals((string_address)launcher.text, (string_address) "lsx"));
        launcher_pressed(FIELD_KEY_BACKSPACE, 0);
        check("launcher: Backspace takes a character back", string_equals((string_address)launcher.text, (string_address) "ls") && launcher.entries == 7);
        launcher_pressed(UI_KEY_TAB, 0);
        check("launcher: Tab takes the highlighted name into the field", string_equals((string_address)launcher.text, (string_address) "ls"));
        launcher_pressed(UI_KEY_DOWN, 0);
        launcher_pressed(UI_KEY_TAB, 0);
        check("launcher: and the name of the row it is on", string_equals((string_address)launcher.text, (string_address) "lsof"));
        launcher_type_text("ls -l -a");
        launcher_pressed(FIELD_KEY_BACKSPACE, WINDOW_KEY_CONTROL);
        check("launcher: Control-Backspace takes the last word", launcher_holds("ls -l -"));
        launcher_pressed(FIELD_KEY_BACKSPACE, WINDOW_KEY_CONTROL | WINDOW_KEY_SHIFT);
        check("launcher: Control-Shift-Backspace clears the field to the caret, and the grid is the desktop's",
              launcher_holds("") && launcher.text[0] == 0 && !launcher.entries && launcher.app_count == (int)launcher_offered_count());
        check("launcher: Escape closes it", !launcher_pressed(FIELD_KEY_ESCAPE, 0));
        launcher_type_text("caf");
        launcher_typed(0xe9);
        launcher_pressed(FIELD_KEY_BACKSPACE, 0);
        check("launcher: Backspace takes a whole character back, not a byte of one", string_equals((string_address)launcher.text, (string_address) "caf"));

        // The field is the kit's: the caret moves, a selection is typed over, a word goes at once, and undo brings it back.
        launcher_type_text("lsof");
        launcher_pressed(FIELD_KEY_LEFT, 0);
        launcher_pressed(FIELD_KEY_LEFT, 0);
        launcher_typed(' ');
        check("launcher: Left moves the caret, and a blank is typed where it is", launcher_holds("ls of") && launcher_caret() == 3);
        launcher_pressed(FIELD_KEY_HOME, 0);
        launcher_typed('x');
        launcher_pressed(FIELD_KEY_END, 0);
        launcher_typed('!');
        check("launcher: Home and End put the caret at the ends", launcher_holds("xls of!") && launcher_caret() == 7);
        launcher_pressed(FIELD_KEY_LEFT, WINDOW_KEY_SHIFT);
        launcher_pressed(FIELD_KEY_LEFT, WINDOW_KEY_SHIFT);
        launcher_pressed(FIELD_KEY_LEFT, WINDOW_KEY_SHIFT);
        launcher_typed('k');
        check("launcher: Shift and the arrows select, and typing replaces what is selected", launcher_holds("xls k"));
        launcher_pressed(FIELD_KEY_LEFT, WINDOW_KEY_CONTROL);
        check("launcher: Control and Left go a word at a time", launcher_caret() == 4);
        {
                struct window_event all = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, FIELD_KEY_A, 1};
                struct window_event undo = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_KEY_CONTROL, FIELD_KEY_Z, 26};

                launcher_key(&all);
                launcher_typed('t');
                launcher_typed('e');
                check("launcher: Control-A selects the whole field, and a letter typed is the field", launcher_holds("te"));
                launcher_key(&undo);
                check("launcher: Control-Z undoes the typing", !launcher_holds("te"));
        }
        launcher_type_text("kernel log");
        check("launcher: a blank between words is typed, and names the program", launcher_list_is("w:Kernel log,c,"));
        launcher_type_text("ls");
        launcher_pressed(FIELD_KEY_DOWN, 0);
        check("launcher: Down is the list's, not the field's", launcher.list.focus == 1 && launcher_holds("ls"));
        launcher_type_text("");

        // What a row starts.
        launcher_type_text("ls");
        count = launcher_words(&launcher.entry[1], words, full, sizeof(full));
        check("launcher: a program of PATH is run by its full name in a terminal drawn in the monospace face, not through a shell",
              count == 7 && string_equals(words[0], (string_address) "/terminal") && string_equals(words[2], (string_address) "/fonts/mono.ttf") &&
                  string_equals(words[4], (string_address) "/fonts/mono-bold.ttf") && string_equals(words[5], (string_address) "-x") &&
                  string_equals(words[6], (string_address) "/bin/lsof") && !words[7]);
        launcher_type_text("ls -l | head");
        count = launcher_words(&launcher.entry[0], words, full, sizeof(full));
        check("launcher: a command is the machine's shell given the text, in a terminal",
              count == 9 && string_equals(words[0], (string_address) "/terminal") && string_equals(words[5], (string_address) "-x") &&
                  string_equals(words[6], (string_address) "/shell") && string_equals(words[7], (string_address) "-c") &&
                  string_equals(words[8], (string_address) "ls -l | head") && !words[9]);
        launcher_type_text("");
        {
                struct launcher_entry settings = {LAUNCHER_WINDOW, 0}, klog = {LAUNCHER_WINDOW, 2};

                count = launcher_words(&settings, words, full, sizeof(full));
                check("launcher: the settings are a window of their own in the bar's face",
                      count == 3 && string_equals(words[0], (string_address) "/settings") && string_equals(words[1], (string_address) "-f") &&
                          string_equals(words[2], (string_address) "/fonts/ui.ttf") && !words[3]);
                count = launcher_words(&klog, words, full, sizeof(full));
                check("launcher: the kernel log is a window drawn in the monospace face",
                      count == 5 && string_equals(words[0], (string_address) "/klog") && string_equals(words[2], (string_address) "/fonts/mono.ttf") &&
                          string_equals(words[4], (string_address) "/fonts/mono-bold.ttf"));
        }
        launcher.height = 0;
        launcher.asked_beside = true; // the grid and its power row are the beside launcher's (ad788933c)
        launcher.width = LAUNCHER_WIDTH * launcher.ui.scale / 100;
        launcher_changed();
        {
                int per = launcher_per(), lines = (launcher.app_count + per - 1) / per;

                check("launcher: the picture's height follows the grid's lines of tiles, and has the power row under it",
                      launcher.height == 2 * UI_PAD + UI_TARGET + UI_GAP2 + lines * UI_TILE_H + UI_GAP2 + UI_ROW);
        }
        // Every line of the grid is in the middle of the bar, a short last line on its own; Up and Down go to the nearest tile across.
        {
                struct ui_list address_to g = &launcher.grid;
                int wide, per, total = g->total, both;

                launcher_layout();
                wide = ui_dev(&launcher.ui, g->tiles), per = ui_list_per(&launcher.ui, g);
                for (int count = 1; count <= per + 2; count++)
                {
                        int last = (count - 1) / per * per, in_line = count - last;

                        g->total = count;
                        both = ui_list_tile_x(&launcher.ui, g, last) - g->x == (g->w - in_line * wide) / 2 &&
                               ui_list_tile_x(&launcher.ui, g, 0) - g->x == (g->w - (count < per ? count : per) * wide) / 2;
                        check("launcher: a line of the grid's tiles is in the middle of the bar, a short last line in the middle on its own (1, 5, a full line and 2)",
                              count != 1 && count != 5 && count != per + 2 ? true : both);
                }
                g->total = per + 2, g->focus = per - 1;
                {
                        b32 moved = false;
                        int near = ui_list_tile_near(&launcher.ui, g, 1, ui_list_tile_middle(&launcher.ui, g, per - 1));

                        ui_list_key(&launcher.ui, g, UI_KEY_DOWN, &moved);
                        check("launcher: Down from the end of a full line goes to the nearest tile across of the short line under it, and Up back to the line above",
                              moved && g->focus == near && near == per + 1 && (ui_list_key(&launcher.ui, g, UI_KEY_UP, &moved), moved && g->focus / per == 0));
                }
                g->total = total, g->focus = 0;
        }
        // The field's text is in the middle of its well while it fits (the caret, a click and the selection go by its origin), and one
        // longer than the well scrolls from its start edge as before; the launcher's field is a centred one.
        {
                static struct ui_text centred;
                static struct ui_text_layout line;

                centred.left = 10, centred.right = 110, centred.centred = true, centred.scroll_x = 7;
                line.width = 40, line.base = 0;
                check("launcher: a centred field's line that fits begins where it is in the middle, either way it reads; a longer one scrolls from its start",
                      ui_text_origin(&centred, &line) == 40 && (line.base = 1, ui_text_origin(&centred, &line) == 40) &&
                          (line.width = 300, line.base = 0, ui_text_origin(&centred, &line) == 3) && launcher_field()->centred);
        }
        launcher_type_text("ls");
#ifdef UI_TARGET
        // A finger: a row shown pressed as it lands and run only as it lifts there; slid off the row, nothing is run.
        {
                int row_y = launcher.ui.scale * (UI_PAD + UI_TARGET + UI_GAP2) / 100 + launcher.ui.scale * UI_ROW * 3 / 200;
                struct window_event finger = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, LAUNCHER_WIDTH * launcher.ui.scale / 100 / 2, row_y};

                launcher.width = LAUNCHER_WIDTH * launcher.ui.scale / 100;
                struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
                struct window_event lift = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
                b32 pressed;

                launcher_send(&finger);
                launcher_send(&land);
                pressed = ui_pressed(&launcher.list.press, 1) && launcher.list.focus == 1;
                finger.b += 3 * UI_ROW * launcher.ui.scale / 100;
                launcher_send(&finger);
                check("launcher: a finger shows the row it lands on pressed, and run nothing as it slides off",
                      pressed && !ui_pressed(&launcher.list.press, 1) && launcher_send(&lift) && !launcher.list.press.down);
                check("launcher: a row is a finger's least height", UI_ROW >= 40);
        }
#endif

        // The power row is the beside launcher's (ad788933c, 11-20: a centred launcher shows the recents and no grid or power row).
        launcher.asked_beside = true;
        // The power buttons under the list are the bar's view (launcher_view): here the launcher hands the view its events, starts what
        // it fires and nothing else, and keeps the rest of its keys. What a start is, is a line in a file (the fixture's shell_exec_file).
        {
                static const char started_path[] = "/tmp/mw-launcher-started";
                char said[512];
                int ax, ay, bx, by;
                struct window_event motion, land, lift;

                system_remove_at(AT_FDCWD, started_path, 0);
                launcher.width = LAUNCHER_WIDTH * launcher.ui.scale / 100;
                launcher_type_text("ls");
                launcher_layout();
                check("launcher: the list leaves room for the power row, whose buttons are the kit's, Power off then Restart, in the middle of the bar",
                      launcher.view.group.count == 2 && string_equals((string_address)launcher.view.group.button[0].label, (string_address) "Power off") &&
                          string_equals((string_address)launcher.view.group.button[1].label, (string_address) "Restart") &&
                          launcher.view.group.button[0].x - (LAUNCHER_WIDTH - launcher.view.group.button[1].x - launcher.view.group.button[1].w) <= 1 &&
                          launcher.view.group.button[0].x - (LAUNCHER_WIDTH - launcher.view.group.button[1].x - launcher.view.group.button[1].w) >= -1 &&
                          launcher.view.group.button[0].y >= UI_PAD + UI_TARGET + UI_GAP2 + (int)launcher.entries * UI_ROW);
                check("launcher: Shift-Tab gives the keys to the buttons, the first of them, and leaves the field as it is",
                      launcher_pressed(UI_KEY_TAB, WINDOW_KEY_SHIFT) && launcher.view.group.keys == 1 && launcher_holds("ls"));
                launcher_typed('x');
                check("launcher: a letter typed with the keys on the buttons is the field's, and takes the keys back", launcher_holds("lsx") && !launcher.view.group.keys);
                launcher_pressed(UI_KEY_TAB, WINDOW_KEY_SHIFT);
                check("launcher: Escape with the keys on the buttons closes the bar, as it does anywhere", !launcher_pressed(UI_KEY_ESCAPE, 0));
                launcher_type_text("ls");

                // Power off from the keys: the first Enter arms, a held Enter's repeats confirm nothing, the next Enter starts it once.
                launcher.power_runs = 0;
                launcher_pressed(UI_KEY_TAB, WINDOW_KEY_SHIFT);
                launcher_pressed(UI_KEY_ENTER, 0);
                for (int repeat = 0; repeat < 40; repeat++)
                        launcher_pressed(UI_KEY_ENTER, WINDOW_KEY_REPEAT);
                check("launcher: a first Enter on Power off arms it and a held Enter's repeats start nothing",
                      launcher.view.group.confirm.armed && launcher.view.group.confirm.what == 1 && !launcher.power_runs && file_text(started_path, said, sizeof(said)) == 0);
                launcher_pressed(UI_KEY_ESCAPE, 0);
                launcher_type_text("ls");
                check("launcher: a key that is not the press drops what was armed, and the keys are the field's again", !launcher.view.group.confirm.armed && !launcher.view.group.keys);
                launcher_pressed(UI_KEY_TAB, WINDOW_KEY_SHIFT);
                launcher_pressed(UI_KEY_ENTER, 0);
                check("launcher: the next Enter starts the shell's poweroff, once, and the bar goes",
                      !launcher_pressed(UI_KEY_ENTER, 0) && launcher.power_runs == 1 && launcher_waits(started_path, 1) &&
                          launcher_started_is(started_path, "/shell -c poweroff \n"));
                // Restart, by Right, and Space.
                launcher_type_text("ls");
                launcher.view.group.keys = 0;
                launcher_pressed(UI_KEY_TAB, WINDOW_KEY_SHIFT);
                launcher_pressed(UI_KEY_RIGHT, 0);
                launcher_pressed(UI_KEY_SPACE, 0);
                check("launcher: Right and Space on Restart arm it, and the second Space starts the shell's reboot, once and not poweroff",
                      launcher.view.group.confirm.armed && launcher.view.group.confirm.what == 2 && !launcher_pressed(UI_KEY_SPACE, 0) &&
                          launcher.power_runs == 2 && launcher_waits(started_path, 2) && launcher_started_is(started_path, "/shell -c poweroff \n/shell -c reboot \n"));

                // The pointer: a click arms, a second click starts; a click elsewhere drops it; a finger does the same.
                system_remove_at(AT_FDCWD, started_path, 0);
                launcher.power_runs = 0;
                launcher_type_text("ls");
                launcher_layout();
                ax = launcher.view.group.button[0].x + 10, ay = launcher.view.group.button[0].y + 10;
                bx = launcher.view.group.button[1].x + 10, by = launcher.view.group.button[1].y + 10;
                motion = (struct window_event){WINDOW_EVENT_MOTION, 0, ax, ay};
                land = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                lift = (struct window_event){WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
                launcher_send(&motion);
                launcher_send(&land);
                check("launcher: a click on Power off arms it and starts nothing", launcher_send(&lift) && launcher.view.group.confirm.armed && !launcher.power_runs);
                motion.b = 2, motion.a = 2;
                launcher_send(&motion);
                launcher_send(&land);
                launcher_send(&lift);
                check("launcher: a click elsewhere drops it", !launcher.view.group.confirm.armed);
                motion.a = bx, motion.b = by;
                launcher_send(&motion);
                launcher_send(&land);
                launcher_send(&lift);
                check("launcher: a click on Restart arms Restart and not another", launcher.view.group.confirm.armed && launcher.view.group.confirm.what == 2);
                launcher_send(&land);
                check("launcher: the next click on it starts the shell's reboot once, and the bar goes",
                      !launcher_send(&lift) && launcher.power_runs == 1 && launcher_waits(started_path, 1) && launcher_started_is(started_path, "/shell -c reboot \n"));
                launcher_type_text("ls");
                launcher_layout();
                motion = (struct window_event){WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, ax, ay};
                land = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
                lift = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
                launcher_send(&motion);
                launcher_send(&land);
                launcher_send(&lift);
                launcher_send(&land);
                check("launcher: two taps of a finger on Power off start the shell's poweroff once",
                      !launcher_send(&lift) && launcher.power_runs == 2 && launcher_waits(started_path, 2) &&
                          launcher_started_is(started_path, "/shell -c reboot \n/shell -c poweroff \n"));
                system_remove_at(AT_FDCWD, started_path, 0);
        }

        // PATH read from real directories: a program of any name length, once, and nothing that is not one.
        {
                static unsigned short scan_hashed[LAUNCHER_HASH + LAUNCHER_DIRENTS / 2];
                static const char address_to const made[] = {"/tmp/mw-launcher-check", "/tmp/mw-launcher-check/one", "/tmp/mw-launcher-check/two"};
                static const char address_to const files[][3] = {{"one", "a", "\x01"}, {"one", "bb", "\x01"}, {"one", "uname", "\x01"},
                                                                  {"one", "plain", "\x00"}, {"one", "x y", "\x01"}, {"one", "compositor", "\x01"},
                                                                  {"one", "settings", "\x01"}, {"two", "a", "\x01"}, {"two", "zz", "\x01"}};
                char path[96];
                b32 found = true;

                for (positive at = 0; at < 3; at++)
                        system_make_directory_at(AT_FDCWD, made[at], 0755);
                system_make_directory_at(AT_FDCWD, "/tmp/mw-launcher-check/one/subdirectory", 0755);
                for (positive at = 0; at < sizeof(files) / sizeof(files[0]); at++)
                {
                        bipolar handle;

                        launcher_check_path(path, files[at][0], files[at][1]);
                        handle = system_open_at_mode(AT_FDCWD, path, FILE_WRITE, files[at][2][0] ? 0755 : 0644);
                        if (handle >= 0)
                                system_close(handle);
                        else
                                found = false;
                        system_change_mode_at(AT_FDCWD, path, files[at][2][0] ? 0755 : 0644);
                }
                memory_zero(&launcher, sizeof(launcher));
                launcher.names = (char address_to)names;
                launcher.from = from;
                launcher.hashed = scan_hashed;
                memory_zero(scan_hashed, sizeof(scan_hashed));
                launcher_scan("/tmp/mw-launcher-check/one:/tmp/mw-launcher-check/nowhere::/tmp/mw-launcher-check/two");
                {
                        static const char address_to const wanted[] = {"a", "bb", "uname", "zz"};
                        positive same = 0;

                        for (positive at = 0; at < sizeof(wanted) / sizeof(wanted[0]); at++)
                        {
                                b32 there = false;

                                for (positive each = 0; each < launcher.count; each++)
                                        there |= string_equals((string_address)launcher_name(each), (string_address)wanted[at]);
                                found &= there;
                        }
                        for (positive at = 0; at < launcher.count; at++)
                                same += string_equals((string_address)launcher_name(at), (string_address) "a");
                        check("launcher: PATH is read from its directories, names of every length among them",
                              found && launcher.dir_count == 3);
                        check("launcher: and nothing but programs: not a directory, a file that cannot be run, a name with a blank, the machinery or the desktop's own",
                              launcher.count == 4);
                        check("launcher: a name in two directories is listed once, with the first directory", same == 1 && from[0] == 0);
                }
                //      A PATH that repeats a directory sixteen times, and then has another: the repeats are one directory, so the
                //      other is read too (the directories it reads are counted once each, and a PATH holds at most sixteen).
                {
                        static const char one[] = "/tmp/mw-launcher-check/one:";
                        char repeated[LAUNCHER_PATH];
                        positive used = 0;

                        for (positive each = 0; each < 16; each++, used += string_length((string_address)one))
                                string_copy((string_address)repeated + used, (string_address)one);
                        string_copy((string_address)repeated + used, (string_address)"/tmp/mw-launcher-check/two");
                        memory_zero(&launcher, sizeof(launcher));
                        launcher.names = (char address_to)names;
                        launcher.from = from;
                        launcher.hashed = scan_hashed;
                        memory_zero(scan_hashed, sizeof(scan_hashed));
                        launcher_scan(repeated);
                        check("launcher: repeated PATH directories are one each, so the directory after them is read",
                              launcher.dir_count == 2 && launcher.count == 4 &&
                                  string_equals((string_address)launcher_name(3), (string_address)"zz"));
                }
                //      A PATH with more directories than the list has room for, and a PATH longer than its buffer: each
                //      directory it names is read, the last one too (the two directories that do not exist are read as none).
                for (positive wide_case = 0; wide_case < 2; wide_case++)
                {
                        char wide[1024];
                        positive used = 0;

                        for (positive each = 0; each < 20; each++)
                        {
                                const char address_to const prefix = wide_case ? "/tmp/mw-launcher-check/gone-" : "/nowhere/a";
                                positive size = string_length((string_address)prefix);

                                memory_copy(wide + used, prefix, size);
                                used += size;
                                wide[used++] = (char)('0' + each / 10);
                                wide[used++] = (char)('0' + each % 10);
                                wide[used++] = ':';
                        }
                        string_copy((string_address)wide + used, (string_address)"/tmp/mw-launcher-check/two");
                        memory_zero(&launcher, sizeof(launcher));
                        launcher.names = (char address_to)names;
                        launcher.from = from;
                        launcher.hashed = scan_hashed;
                        memory_zero(scan_hashed, sizeof(scan_hashed));
                        launcher_scan(wide);
                        if (wide_case)
                                check("launcher: a PATH longer than its buffer is read to its end, its last directory too",
                                      launcher.dir_count == 21 && launcher.count == 2 && (string_equals((string_address)launcher_name(0), (string_address)"zz") || string_equals((string_address)launcher_name(1), (string_address)"zz")));
                        else
                                check("launcher: a PATH of more directories than the list had room for is read to its last",
                                      launcher.dir_count == 21 && launcher.count == 2 && (string_equals((string_address)launcher_name(0), (string_address)"zz") || string_equals((string_address)launcher_name(1), (string_address)"zz")));
                }
                for (positive at = 0; at < sizeof(files) / sizeof(files[0]); at++)
                {
                        launcher_check_path(path, files[at][0], files[at][1]);
                        system_remove_at(AT_FDCWD, path, 0);
                }
                system_remove_at(AT_FDCWD, "/tmp/mw-launcher-check/one/subdirectory", 0x200);
                system_remove_at(AT_FDCWD, made[2], 0x200);
                system_remove_at(AT_FDCWD, made[1], 0x200);
                system_remove_at(AT_FDCWD, made[0], 0x200);
        }
        launcher.asked_beside = false;
}

/*
        The launcher opened beside the panel's button (-a). The bar ends where a bottom panel's button starts, starts where a top
        panel's ends, is beside a left or right panel's with its top at the button's top, its left at the button's for the other two,
        and is all of it on its screen at every scale and screen, a screen past the first included; what the table says is taken once
        and only when it says something; the bar's height is held to the room the panel leaves; and a bar above a bottom panel is put
        again when its height changes, which is the only edge whose bar moves with it.
*/
static fn check_launcher_anchor(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        static const unsigned int edges[4] = {TASKBAR_BOTTOM, TASKBAR_TOP, TASKBAR_LEFT, TASKBAR_RIGHT};
        static const int scales[6] = {50, 75, 100, 125, 150, 200};
        static const int screens[2][4] = {{0, 0, 1920, 1080}, {1920, 0, 1280, 720}};
        int beside = 1, inside = 1, kept = 1;

        for (int on = 0; on < 2; on++)
                for (int at = 0; at < 4; at++)
                        for (int each = 0; each < 6; each++)
                        {
                                int scale = scales[each], thick = COMPOSITOR_PANEL * scale / 100, gap = UI_GAP1 * scale / 100;
                                int high = thick - 3 * scale / 100, width = LAUNCHER_WIDTH * scale / 100, height = 300 * scale / 100;
                                const int *screen = screens[on];
                                int right = screen[0] + screen[2], bottom = screen[1] + screen[3], x, y;
                                int button[4];

                                switch (edges[at])
                                {
                                case TASKBAR_BOTTOM: button[0] = screen[0] + gap, button[1] = bottom - thick, button[2] = thick, button[3] = thick; break;
                                case TASKBAR_TOP: button[0] = screen[0] + gap, button[1] = screen[1], button[2] = thick, button[3] = thick; break;
                                case TASKBAR_LEFT: button[0] = screen[0], button[1] = screen[1] + gap, button[2] = thick, button[3] = thick; break;
                                default: button[0] = right - thick, button[1] = screen[1] + gap, button[2] = thick, button[3] = thick; break;
                                }
                                ui_anchor_place(ui_panel_side(edges[at]), 0, button, screen, width, height, &x, &y);
                                beside &= edges[at] == TASKBAR_BOTTOM ? y + height == button[1] && x == button[0]
                                          : edges[at] == TASKBAR_TOP  ? y == button[1] + button[3] && x == button[0]
                                          : edges[at] == TASKBAR_LEFT ? x == button[0] + button[2] && y == button[1]
                                                                      : x + width == button[0] && y == button[1];
                                inside &= x >= screen[0] && y >= screen[1] && x + width <= right && y + height <= bottom;
                        }
        // A bar wider and taller than the room is kept on the screen by its top left; a button near the right edge pulls the bar left.
        {
                int button[4] = {1900, 1056, 21, 24}, x, y;

                ui_anchor_place(UI_ABOVE, 0, button, screens[0], 560, 300, &x, &y);
                inside &= x == 1920 - 560 && y == 1056 - 300;
                ui_anchor_place(UI_ABOVE, 0, button, screens[0], 560, 1200, &x, &y);
                inside &= x == 1920 - 560 && y == 0;
                ui_anchor_place(UI_LEFT, 0, (int[4]){1896, 4, 24, 21}, screens[0], 2400, 300, &x, &y);
                inside &= x == 0 && y == 4;
        }
        check("launcher beside the button: the bar is above a bottom panel's button, below a top panel's and beside a side panel's, at every scale and on any screen", beside);
        check("launcher beside the button: and all of it is on its screen, a wide or tall bar by its top left", inside);

        // The table's press: taken once, and only a whole one.
        memory_zero(&launcher, sizeof(launcher));
        launcher.own.table = table_at;
        launcher.own.row = 3;
        launcher.ui.scale = 100;
        table_at->output_width = 1920;
        table_at->output_height = 1080;
        table_at->outputs = 2;
        for (int screen = 0; screen < 2; screen++)
                table_at->output_x[screen] = screens[screen][0], table_at->output_y[screen] = screens[screen][1],
                table_at->output_w[screen] = (unsigned int)screens[screen][2], table_at->output_h[screen] = (unsigned int)screens[screen][3];
        table_at->button_x = 1944, table_at->button_y = 696, table_at->button_w = 21, table_at->button_h = 24;
        table_at->button_edge = TASKBAR_BOTTOM, table_at->button_screen = 1;
        ui_anchor_take(&launcher.anchor, launcher.own.table, launcher.asked_beside);
        kept &= !launcher.anchor.anchored;
        launcher.asked_beside = true;
        table_at->button_w = 0;
        ui_anchor_take(&launcher.anchor, launcher.own.table, launcher.asked_beside);
        kept &= !launcher.anchor.anchored;
        table_at->button_w = 21, table_at->button_edge = TASKBAR_RIGHT + 1;
        ui_anchor_take(&launcher.anchor, launcher.own.table, launcher.asked_beside);
        kept &= !launcher.anchor.anchored;
        table_at->button_edge = TASKBAR_BOTTOM, table_at->button_screen = 2;
        ui_anchor_take(&launcher.anchor, launcher.own.table, launcher.asked_beside);
        kept &= !launcher.anchor.anchored;
        table_at->button_screen = 1;
        ui_anchor_take(&launcher.anchor, launcher.own.table, launcher.asked_beside);
        kept &= launcher.anchor.anchored && launcher.anchor.edge == TASKBAR_BOTTOM && launcher.anchor.number == 1 && launcher.anchor.button[0] == 1944 &&
                launcher.anchor.button[1] == 696 && launcher.anchor.button[2] == 21 && launcher.anchor.button[3] == 24 && launcher.anchor.screen[0] == 1920 &&
                launcher.anchor.screen[2] == 1280 && launcher.anchor.screen[3] == 720;
        check("launcher beside the button: what the table says is taken once, with the screen it names, and only when it says something whole", kept);

        launcher.asked_beside = true; // the beside launcher: its power row is under the list (ad788933c)
        // The room: a bar beside a top or bottom panel is held to what the panel leaves, a side panel's to the output.
        launcher.height = 0;
        launcher.width = 560;
        launcher_changed();
        {
                int per = launcher_per(), lines = (launcher.app_count + per - 1) / per;

                kept = launcher.app_count > 0 && launcher.height == 2 * UI_PAD + UI_TARGET + UI_GAP2 + lines * UI_TILE_H + UI_GAP2 + UI_ROW;
        }
        launcher.anchor.button[1] = 100;
        launcher.anchor.screen[1] = 0;
        kept &= launcher_most_height() == 100;
        launcher_changed();
        kept &= launcher.height == 100;
        launcher.anchor.edge = TASKBAR_TOP;
        launcher.anchor.button[1] = 0;
        launcher.anchor.screen[1] = 0, launcher.anchor.screen[3] = 720, launcher.anchor.button[3] = 24;
        kept &= launcher_most_height() == 720 - 24;
        launcher.anchor.edge = TASKBAR_LEFT;
        kept &= launcher_most_height() == 1080;
        launcher.anchor.anchored = false;
        kept &= launcher_most_height() == 1080;
        check("launcher beside the button: its height is held to the room the panel leaves above or below it, and to the output beside a side panel", kept);

        // The place it asks: the placement columns of its row, from its screen's top left, with the size.
        launcher.anchor.anchored = true;
        launcher.anchor.edge = TASKBAR_BOTTOM;
        launcher.anchor.number = 1;
        // The moon in the middle of its screen's band: the launcher centred on it, 360 in from the screen's left.
        launcher.anchor.button[0] = 2546, launcher.anchor.button[1] = 696, launcher.anchor.button[2] = 28, launcher.anchor.button[3] = 24;
        launcher.anchor.screen[0] = 1920, launcher.anchor.screen[1] = 0, launcher.anchor.screen[2] = 1280, launcher.anchor.screen[3] = 720;
        launcher.width = 560, launcher.height = 200;
        ui_anchor_set(&launcher.anchor, launcher.own.table, launcher.own.row, launcher.width, launcher.height);
        kept = table_at->place[3] == (WINDOW_PLACE_AT | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE) && table_at->place_x[3] == 360 && table_at->place_y[3] == 496 &&
               table_at->place_width[3] == 560 && table_at->place_height[3] == 200 && table_at->place_screen[3] == 1;
        check("launcher beside the button: it asks for its place and size as a placement, counted from its screen's top left", kept);

        // Moved again with a new height only above a bottom panel, and only once shown.
        launcher.buffers.shown = -1;
        launcher.height = 0;
        string_copy_bounded((string_address)launcher.text, (string_address) "zzzz", sizeof(launcher.text));
        launcher.moved = false;
        launcher_changed();
        kept = !launcher.moved;
        launcher.buffers.shown = 0;
        launcher.text[0] = 0;
        launcher_changed();
        kept &= launcher.moved;
        launcher.moved = false;
        launcher_changed();
        kept &= !launcher.moved;
        launcher.anchor.edge = TASKBAR_TOP;
        string_copy_bounded((string_address)launcher.text, (string_address) "zzzz", sizeof(launcher.text));
        launcher_changed();
        kept &= !launcher.moved;
        launcher.anchor.edge = TASKBAR_BOTTOM;
        launcher.anchor.anchored = false;
        launcher.text[0] = 0;
        launcher_changed();
        kept &= !launcher.moved;
        check("launcher beside the button: a bar above a bottom panel is put again when its height changes, once it is shown, and no other is", kept);
        {
                // The gap is half the button's thickness on every edge, at 1x and at 2x (launcher_anchor_gap): the old gap was the whole button.
                struct ui_anchor g = {.anchored = true, .edge = TASKBAR_BOTTOM, .button = {600, 696, 24, 24}};
                struct ui_anchor g2 = {.anchored = true, .edge = TASKBAR_BOTTOM, .button = {600, 672, 48, 48}};
                struct ui_anchor s = {.anchored = true, .edge = TASKBAR_LEFT, .button = {0, 600, 24, 24}};
                struct ui_anchor s2 = {.anchored = true, .edge = TASKBAR_RIGHT, .button = {1856, 600, 48, 48}};
                check("launcher gap: half the button's thickness on a bottom panel, at 1x (12 of 24) and at 2x (24 of 48)",
                      launcher_anchor_gap(&g) == 12 && launcher_anchor_gap(&g2) == 24);
                check("launcher gap: half the button's width on a side panel, at 1x and at 2x, and the same for either side",
                      launcher_anchor_gap(&s) == 12 && launcher_anchor_gap(&s2) == 24);
        }
}
#endif

/*
        The settings table (src/sh/registry.c): every plain row is one verb, with its usage, its key, its file
        and a standard that its own words take; a word is kept normalised or refused with a number's range
        kept to the edge; the longest verb wins; and what the compositor takes from the files of the rows it
        applies: the shake off means a shake finds nothing, the delay is the time the pointer stays large,
        the sensitivity and the acceleration make the pixels, the repeat is the kept numbers or the device's.
*/
static fn check_registry(void)
{
        positive plain = 0, labels_equal = 0;
        char joined[96], key[48];
        long value;
        p8 kept[32];

        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];

                for (positive other = at + 1; other < SETTINGS_ROWS; other++)
                        labels_equal += string_equals((string_address)row->label, (string_address)settings_rows[other].label);
                if (row->how != SETTING_PLAIN)
                        continue;
                plain++;
                joined[0] = key[0] = 0;
                for (positive word = 0; word < setting_words(row); word++)
                {
                        string_append_bounded((string_address)joined, (string_address)(word ? " " : ""), sizeof(joined));
                        string_append_bounded((string_address)joined, (string_address)row->read[word], sizeof(joined));
                        check("registry: a plain row asks the verb it sets", row->set[word] && string_equals((string_address)row->read[word], (string_address)row->set[word]));
                }
                string_copy_bounded((string_address)key, (string_address)row->read[setting_words(row) - 1], sizeof(key));
                string_append_bounded((string_address)key, (string_address)" ", sizeof(key));
                check("registry: a plain row's key is its last word", row->key && string_equals((string_address)row->key, (string_address)key));
                if (row->usage)
                {
                        //      The usage begins with the verb, or with its words but the last and names that one in brackets (canvas cursor [size ...).
                        positive whole = string_length((string_address)joined), cut = whole;
                        char bracketed[48] = "[";

                        while (cut && joined[cut - 1] != ' ')
                                cut--;
                        string_append_bounded((string_address)bracketed, (string_address)joined + cut, sizeof(bracketed));
                        check("registry: and its usage begins with its verb",
                              (!string_compare_max((string_address)row->usage, (string_address)joined, whole) &&
                               (!row->usage[whole] || row->usage[whole] == ' ')) ||
                                  (cut && !string_compare_max((string_address)row->usage, (string_address)joined, cut) &&
                                   string_first_of_or_end((string_address)row->usage, '[')[0] &&
                                   !string_compare_max(string_first_of_or_end((string_address)row->usage, '[') , (string_address)bracketed, string_length((string_address)bracketed))));
                        check("registry: and it says itself plainly", row->help &&
                              !string_first_of_or_end((string_address)row->usage, '\'')[0] && !string_first_of_or_end((string_address)row->help, '\'')[0]);
                }
                else
                {
                        //      A row with no line of its own is said by the line of the row of its verb above it (cursor look by cursor size).
                        positive covered = 0, cut = string_length((string_address)joined);

                        while (cut && joined[cut - 1] != ' ')
                                cut--;
                        for (positive other = 0; other < SETTINGS_ROWS; other++)
                                covered += settings_rows[other].usage && !string_compare_max((string_address)settings_rows[other].usage, (string_address)joined, cut ? cut - 1 : 0) &&
                                           (cut ? settings_rows[other].usage[cut - 1] == ' ' : 0);
                        check("registry: and one with no usage is covered by the usage of a row with its verb but the last word", covered > 0);
                }
                check("registry: and it has a file under /root and a standard",
                      row->path && !string_compare_max((string_address)row->path, (string_address) "/root/", 6) && row->standard);
                check("registry: and its standard is one of its words", setting_parse(row, (string_address)row->standard, kept, sizeof(kept), &value) &&
                      value == setting_standard(row));
                check("registry: default puts it back", setting_parse(row, (string_address) "default", kept, sizeof(kept), &value) && value == setting_standard(row));
                check("registry: and a word with nothing in it, with blanks, or in capitals is none",
                      !setting_parse(row, (string_address) "", kept, sizeof(kept), &value) && !setting_parse(row, (string_address) "1 ", kept, sizeof(kept), &value) &&
                      !setting_parse(row, (string_address) "ON", kept, sizeof(kept), &value) &&
                          // -1 is a word only of a row whose least is under 0 (taskbar edge)
                          (row->low < 0 || !setting_parse(row, (string_address) "-1", kept, sizeof(kept), &value)));
                if (row->type == SETTING_NUMBER)
                {
                        char edge[16];

                        check("registry: a number's standard is in its range", setting_standard(row) >= row->low && setting_standard(row) <= row->high);
                        // A least under 0 is written with its minus (taskbar edge's).
                        if (row->low < 0)
                                edge[0] = '-', positive_into_string((p8 address_to)edge + 1, (positive)-row->low);
                        else
                                positive_into_string((p8 address_to)edge, (positive)row->low);
                        check("registry: its least is a word", setting_parse(row, (string_address)edge, kept, sizeof(kept), &value) && value == row->low && !string_compare(kept, (string_address)edge));
                        positive_into_string((p8 address_to)edge, (positive)row->high);
                        check("registry: its greatest is a word", setting_parse(row, (string_address)edge, kept, sizeof(kept), &value) && value == row->high);
                        positive_into_string((p8 address_to)edge, (positive)row->high + 1);
                        check("registry: one past it is none", !setting_parse(row, (string_address)edge, kept, sizeof(kept), &value));
                        if (row->low - 1 < 0)
                                edge[0] = '-', positive_into_string((p8 address_to)edge + 1, (positive)(1 - row->low));
                        else
                                positive_into_string((p8 address_to)edge, (positive)row->low - 1);
                        check("registry: nor one before it", !setting_parse(row, (string_address)edge, kept, sizeof(kept), &value));
                        check("registry: nor a zero in front, a sign, a fraction or nine digits and more",
                              !setting_parse(row, (string_address) "0150", kept, sizeof(kept), &value) && !setting_parse(row, (string_address) "+150", kept, sizeof(kept), &value) &&
                              !setting_parse(row, (string_address) "1.5", kept, sizeof(kept), &value) && !setting_parse(row, (string_address) "1000000000", kept, sizeof(kept), &value) &&
                              !setting_parse(row, (string_address) "18446744073709551617", kept, sizeof(kept), &value));
                }
        }
        check("registry: more than the six new rows are plain, and no two rows are labelled alike", plain >= 6 && !labels_equal);

        /* The keyboard row's menu names each layout once; the help names the other spellings of three as well
           (gb, sv and nb are uk, se and no: host.c's locale_keyboards and the compositor's table take both). A
           layout the help names that the menu does not is one of those three with its twin in the menu, and
           every word of the menu is in the help. */
        {
                const struct settings_row address_to row = null;
                positive menu = 0, named = 0, aliases = 0;
                p8 menu_in_help = true, others_twinned = true;
                string_address spelt[] = {(string_address) "gb", (string_address) "sv", (string_address) "nb"};
                string_address twin[] = {(string_address) "uk", (string_address) "se", (string_address) "no"};

                for (positive at = 0; at < SETTINGS_ROWS; at++)
                        row = string_equals((string_address)settings_rows[at].label, (string_address) "Keyboard") ? &settings_rows[at] : row;
                check("registry: the keyboard row is found", row && row->choices && row->help);
                for (const char address_to word = row ? row->choices : ""; *word; menu++)
                {
                        p8 one[8];
                        positive size = (positive)(string_first_of_or_end((string_address)word, ' ') - (string_address)word);

                        string_copy_bounded(one, (string_address)word, size < sizeof(one) ? size + 1 : sizeof(one));
                        menu_in_help &= setting_place(row->help, (string_address)one) >= 0;
                        word += size + (word[size] == ' ');
                }
                for (const char address_to word = row ? row->help : ""; *word && *word != '['; named++)
                {
                        p8 one[8];
                        positive size = (positive)(string_first_of_or_end((string_address)word, ' ') - (string_address)word);

                        string_copy_bounded(one, (string_address)word, size < sizeof(one) ? size + 1 : sizeof(one));
                        if (setting_place(row->choices, (string_address)one) < 0)
                        {
                                p8 twinned = false;

                                aliases++;
                                for (positive at = 0; at < 3; at++)
                                        twinned |= string_equals((string_address)one, spelt[at]) && setting_place(row->choices, twin[at]) >= 0;
                                others_twinned &= twinned;
                        }
                        word += size + (word[size] == ' ');
                }
                check("registry: every layout the keyboard menu names is in its help", menu && menu_in_help);
                check("registry: the help names only gb, sv and nb besides, each beside its twin in the menu",
                      others_twinned && aliases == 3 && named == menu + aliases);
        }

        /* The longest verb that the words start with is the row. */
        {
                string_address delay[] = {(string_address) "canvas", (string_address) "cursor", (string_address) "shake", (string_address) "delay", (string_address) "500"};
                string_address cursor[] = {(string_address) "canvas", (string_address) "cursor"};
                positive used = 0;
                int found = setting_find(delay, 5, &used);

                check("registry: canvas cursor shake delay 500 is the delay with a word", found >= 0 && used == 4 &&
                      string_equals((string_address)settings_rows[found].read[3], (string_address) "delay"));
                found = setting_find(delay, 3, &used);
                check("registry: canvas cursor shake is the switch", found >= 0 && used == 3 && !settings_rows[found].read[3]);
                check("registry: canvas cursor and no more is no plain row's", setting_find(cursor, 2, &used) < 0);
                check("registry: nor are the words of a verb written by hand", setting_find(delay + 1, 1, &used) < 0);
        }

        /* Normalised as they are kept, and said as they are shown. */
        {
                p8 shown[48];
                int rate = -1, shake = -1;

                for (positive at = 0; at < SETTINGS_ROWS; at++)
                {
                        rate = string_equals((string_address)settings_rows[at].path ? (string_address)settings_rows[at].path : (string_address) "", (string_address)SETTING_REPEAT_RATE_PATH) ? (int)at : rate;
                        shake = string_equals((string_address)settings_rows[at].path ? (string_address)settings_rows[at].path : (string_address) "", (string_address)SETTING_SHAKE_PATH) ? (int)at : shake;
                }
                check("registry: the rows are found by their files", rate >= 0 && shake >= 0);
                check("registry: a rate of off is kept as off and means none", setting_parse(&settings_rows[rate], (string_address) "off", kept, sizeof(kept), &value) &&
                      !string_compare(kept, (string_address) "off") && value == 0);
                check("registry: a number is shown with its unit, off without",
                      !string_compare(setting_shown(&settings_rows[rate], (string_address) "45", shown, sizeof(shown)), (string_address) "45 per second") &&
                      !string_compare(setting_shown(&settings_rows[rate], (string_address) "off", shown, sizeof(shown)), (string_address) "off") &&
                      !string_compare(setting_shown(&settings_rows[rate], (string_address) "", shown, sizeof(shown)), (string_address) "30 per second") &&
                      !string_compare(setting_shown(&settings_rows[rate], (string_address) "999", shown, sizeof(shown)), (string_address) "30 per second"));
                check("registry: a switch is on with nothing kept, and its off is 1 for who applies it",
                      !string_compare(setting_shown(&settings_rows[shake], (string_address) "", shown, sizeof(shown)), (string_address) "on") &&
                      !setting_kept(&settings_rows[shake], (string_address) "", &value) && value == 0 &&
                      setting_kept(&settings_rows[shake], (string_address) "off", &value) && value == 1 &&
                      !setting_kept(&settings_rows[shake], (string_address) "default", &value) && value == 0 &&
                      !setting_kept(&settings_rows[shake], (string_address) "maybe", &value) && value == 0);
                check("registry: a rate kept is a rate, a bad one is the standard",
                      setting_kept(&settings_rows[rate], (string_address) "45", &value) && value == 45 &&
                      !setting_kept(&settings_rows[rate], (string_address) "9999", &value) && value == 30 &&
                      setting_kept(&settings_rows[rate], (string_address) "off", &value) && value == 0);
        }

        /* The compositor, from the words the files hold. */
        {
                unsigned long long now = 1000000000ull;
                int found = 0;

                compositor.cursor.magnified = compositor.cursor.magnify = 0;
                compositor_input_settings_set("", "", "", "", "", "");
                check("compositor: nothing kept is what it always did: a shake finds, the pointer stays large 1200 ms, the gain is the curve, no repeat is set",
                      !compositor.cursor.shake_off && compositor.cursor.shake_ns == COMPOSITOR_MAGNIFIED_NS && compositor.sensitivity == 100 &&
                      !compositor.flat && !compositor.repeat_kept);
                compositor_magnify(5);
                check("compositor: and a shaken pointer is large until 1200 ms after", compositor.cursor.magnified && compositor.cursor.magnified_until == 5 + 1200000000ull);
                compositor.cursor.magnified = 0;
                compositor.shake = (struct compositor_shake){0};
                for (int step = 0; step < 12; step++) { found += compositor_shaken(step & 1 ? -8 : 8, now); now += 50000000ull; }
                check("compositor: a shake is found", found == 2);

                compositor_input_settings_set("off", "300", "150", "off", "300", "off");
                check("compositor: shake off, delay 300, sensitivity 150, acceleration off, repeat delay 300 and no repeat are taken",
                      compositor.cursor.shake_off && compositor.cursor.shake_ns == 300000000ull && compositor.sensitivity == 150 &&
                      compositor.flat && compositor.repeat_kept == 3 && compositor.repeat_delay == 300 && compositor.repeat_rate == 0);
                compositor.shake = (struct compositor_shake){0};
                found = 0;
                now = 1000000000ull;
                for (int step = 0; step < 12; step++) { found += compositor_shaken(step & 1 ? -8 : 8, now); now += 50000000ull; }
                check("compositor: with shake off no shake finds the pointer", found == 0);
                compositor.cursor.shake_off = 0;
                compositor_magnify(7);
                check("compositor: and the delay is the time it stays large", compositor.cursor.magnified_until == 7 + 300000000ull);
                compositor.cursor.magnified = 0;

                /* A hand moving 10 counts slowly (a second between reports) and 1000 counts quickly (a millisecond). */
                compositor.pointer_x = compositor.pointer_y = 0;
                compositor.raw_x = 10; compositor.raw_y = 0; compositor.stamp = 1; compositor.accel_x = compositor.accel_y = 0;
                compositor_input_settings_set("", "", "200", "", "", "");
                compositor_report(1000000000ull, 0);
                check("compositor: a sensitivity of 200 doubles the pixels of a slow hand", compositor.real_x == 20);
                compositor.pointer_x = 0; compositor.raw_x = 1000; compositor.stamp = 1000000000ull; compositor.accel_x = 0;
                compositor_input_settings_set("", "", "100", "", "", "");
                compositor_report(1000000000ull + 1000000ull, 0);
                check("compositor: the curve makes a fast hand two and a half times", compositor.real_x == 2500);
                compositor.pointer_x = 0; compositor.raw_x = 1000; compositor.stamp = 1000000000ull; compositor.accel_x = 0;
                compositor_input_settings_set("", "", "100", "off", "", "");
                compositor_report(1000000000ull + 1000000ull, 0);
                check("compositor: with acceleration off it is one to one", compositor.real_x == 1000);
                compositor.pointer_x = 0; compositor.raw_x = 1000; compositor.stamp = 1000000000ull; compositor.accel_x = 0;
                compositor_input_settings_set("", "", "50", "off", "", "");
                compositor_report(1000000000ull + 1000000ull, 0);
                check("compositor: and the sensitivity scales that", compositor.real_x == 500);

                compositor_input_settings_set("maybe", "5", "9", "x", "1", "0");
                check("compositor: a word that is none is as if nothing were kept", !compositor.cursor.shake_off && compositor.cursor.shake_ns == COMPOSITOR_MAGNIFIED_NS &&
                      compositor.sensitivity == 100 && !compositor.flat && !compositor.repeat_kept);
                compositor_input_settings_set("", "", "", "", "", "");
        }
}

#if MOONWATER_UI_SETTINGS
/*
        A choice's menu in the checks: its keys go through settings_press and settings_type_code, which hand them to the menu
        while it is open (settings_key); a point is the popup's own pixels, answered as the runner answers (settings_menu_answer).
*/
static b32 settings_menu_press(int key)
{
        settings_press(key);
        return true;
}

static void settings_menu_type(int c)
{
        settings_type_code(c);
}

// A pointer event at a point of the menu (its own pixels); the event's flags say whether it is a press, a release or a finger's.
static void settings_menu_point(const struct window_event address_to event, int x, int y)
{
        if (settings.page.menu_row >= 0)
                settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, event, x, y));
}

static void settings_menu_gone(void)
{
        ui_popmenu_close(&settings.popup);
        settings_chose(UI_MENU_GONE);
}

// An event to Settings as the app's loop gives it (ui_app_run): the kit's context for it first, the time it is handled at being the
// real one, which is what a finger's speed is made of when events come one after another.
static b32 settings_event_real(const struct window_event address_to event)
{
        deliver_context(event);
        return settings_event(event);
}

/*
        The programs the desktop opens when it starts (registry.c's setting_autostart_valid, the verb desktop autostart): what a line is,
        its limits and a path found in it. It is what desktop startup began with (desktop_startup_read); Settings has no row of it: the
        Boot group's list is desktop startup's (check_startup_table).
*/
static fn check_autostart(void)
{
        static const char address_to const good[] = {"/klog /terminal", "/terminal", "  /klog   /bowls/bin/fire-fox_2.0+x  ", "/a/b/c"};
        static const char address_to const bad[] = {"", "   ", "klog", "/", "/klog /klog", "/a//b", "/a/./b", "/a/../b", "/a/..", "/a/.", "/a/",
                                                     "/klog;rm", "/a b'c", "/x$(y)", "/k\tlog", "none"};
        static char line[90000];
        positive bad_at = 0;
        b32 grammar = true, refused = true, many = true;

        for (positive each = 0; each < sizeof(good) / sizeof(good[0]); each++)
                grammar &= setting_autostart_valid(good[each], null);
        for (positive each = 0; each < sizeof(bad) / sizeof(bad[0]); each++)
                refused &= !setting_autostart_valid(bad[each], &bad_at);
        check("autostart: the standard is none (the desktop starts with no windows), and lines of absolute paths are lines", grammar &&
              !SETTING_AUTOSTART_STANDARD[0]);
        check("autostart: nothing, a word that is no path, a path twice, an empty, . or .. part, or a byte a shell reads is none", refused);
        setting_autostart_valid("/klog /a /klog", &bad_at);
        check("autostart: and the fault is where the path named twice starts", bad_at == 9);
        //      No cap: 100 programs are a line, and the 100th named again is the fault where it starts.
        {
                positive used = 0, last = 0;

                for (int each = 0; each < 100; each++)
                {
                        if (each)
                                line[used++] = ' ';
                        last = used;
                        used += string_length(string_copy((string_address)line + used, (string_address) "/bowls/bin/program-"));
                        line[used++] = (char)('0' + each / 10), line[used++] = (char)('0' + each % 10);
                        line[used] = 0;
                        many &= setting_autostart_valid(line, null);
                }
                check("autostart: 100 programs are a line, each found in it", many && setting_autostart_has(line, "/bowls/bin/program-00") &&
                      setting_autostart_has(line, "/bowls/bin/program-99"));
                line[used++] = ' ';
                memory_copy(line + used, line + last, 21);
                line[used + 21] = 0;
                check("autostart: and the 100th named again is none, the fault where it starts", !setting_autostart_valid(line, &bad_at) && bad_at == used);
        }
        //      And a long list: 2000 programs of 40 bytes each (81999 bytes), and one path of 64 KiB.
        {
                positive used = 0;
                b32 long_ok;

                for (int each = 0; each < 2000; each++)
                {
                        if (each)
                                line[used++] = ' ';
                        used += string_length(string_copy((string_address)line + used, (string_address) "/usr/lib/moonwater/a-long-program-"));
                        for (int digit = 1000; digit; digit /= 10)
                                line[used++] = (char)('0' + each / digit % 10);
                        used += string_length(string_copy((string_address)line + used, (string_address) "/x"));
                }
                line[used] = 0;
                long_ok = used == 81999 && setting_autostart_valid(line, null) && setting_autostart_has(line, "/usr/lib/moonwater/a-long-program-1999/x");
                line[0] = '/';
                for (positive at = 1; at < 65536; at++)
                        line[at] = at % 128 ? 'x' : '/';
                line[65536] = 0;
                long_ok &= setting_autostart_valid(line, null) && setting_autostart_path(line);
                check("autostart: a long list is a line, and so is a path of 64 KiB", long_ok);
        }
        check("autostart: a path is found in a line as a whole word", setting_autostart_has("/klog /terminal", "/terminal") &&
              !setting_autostart_has("/klog /terminal", "/term") && !setting_autostart_has("/klog /terminal", "/klog /terminal"));
        check("autostart: no row of the table is desktop autostart's or startup's any more (the Boot group lists desktop startup)",
              !settings_row_named("Programs started with the desktop") && !settings_row_named("Add a program to start (its path)") &&
                  !settings_row_named("Remove a program from the start (its path)"));
}

// The startup jobs waiting (settings_startup_ask), a job's words a blank apart and the jobs a | apart.
static fn startup_jobs_said(char address_to into, positive room)
{
        positive made = 0;
        into[0] = 0;
        for (positive at = 0; at < settings.startup_jobs_used && made + 2 < room; at++)
        {
                char c = settings.startup_jobs[at];
                if (c)
                        into[made++] = c;
                else if (at + 1 < settings.startup_jobs_used && settings.startup_jobs[at + 1])
                        into[made++] = ' ';
                else if (at + 2 < settings.startup_jobs_used)
                        into[made++] = '|', at++;
        }
        into[made] = 0;
}

/*
        The Boot group's list is desktop startup's (settings_feed_startup): its entries in their order, one ordered group with a grip, a
        remove button and a switch each, an app by its picture (its symbol until the picture is made) and its name with its command the
        note, a command by the terminal's symbol and itself, and after them the add row with its two ways. What the person does is the
        verb's words, places from 1, kept in order and none merged: a move by the kit (a drag) and by Alt+Down, a remove by Delete, a
        switch turned, a command typed (longer than any of the window's other buffers, whole) and the picker's line.
*/
static fn check_startup_table(void)
{
        static const char said[] = "[Moonwater] startup 1 on app settings /settings\n"
                                   "\x1b[1m[Moonwater]\x1b[0m startup 2 off command echo hi | tr a-z A-Z\n"
                                   "[Moonwater] startup 3 on app gone.App /bowls/bin/gone --x\n";
        static char typed[400], jobs[1200];
        struct ui_row row;
        struct window_event key = {0};
        const struct settings_item address_to items;
        int first = SETTINGS_LIST_ID(SETTINGS_STARTUP_LIST, 0), group, was_job = settings.job, add_row;
        bipolar was_child = settings.runner.child;
        b32 described, unbounded;

        // No job begins here: the runner is held busy (and no feed is under way, which a change would end).
        settings.runner.child = 1, settings.job = 0, settings.startup_jobs_used = 0;
        memory_copy(settings_large->said, said, sizeof(said) - 1), settings.runner.length = sizeof(said) - 1, settings.runner.cut_lines = 0;
        settings_feed_startup();
        items = settings.item[SETTINGS_STARTUP_LIST];
        check("startup table: the Boot group's feed is desktop startup's, read into its entries in order and the add row after them",
              string_equals((string_address)settings_feeds[SETTINGS_STARTUP_LIST].group, (string_address) "Boot") &&
                  string_equals((string_address)settings_feeds[SETTINGS_STARTUP_LIST].read[1], (string_address) "startup") &&
                  settings.item_count[SETTINGS_STARTUP_LIST] == 4 && items[0].kind == SETTINGS_STARTUP && items[1].kind == SETTINGS_STARTUP &&
                  items[2].kind == SETTINGS_STARTUP && items[3].kind == SETTINGS_ADD && items[0].app == ui_apps_find("settings") && items[0].app >= 0 &&
                  items[1].app < 0 && items[2].app < 0 && string_equals((string_address)items[2].label, (string_address) "gone.App"));
        memory_zero(&row, sizeof(row)), settings_describe(first, &row);
        described = row.order == SETTINGS_STARTUP_ORDER && row.remove && row.columns && row.columns->count == SETTINGS_STARTUP_COLUMNS &&
                    row.cell_check == 1u << SETTINGS_STARTUP_ON_COLUMN && row.cell_on == 1u << SETTINGS_STARTUP_ON_COLUMN && row.tile_picture &&
                    (row.icon || row.tile_name) && string_equals((string_address)row.label, (string_address) "Settings") &&
                    row.cells[1] && string_equals((string_address)row.cells[1], (string_address) "/settings");
        check("startup table: an app is a row of the ordered table, removable, its picture and name, its command, and its On check on", described);
        memory_zero(&row, sizeof(row)), settings_describe(first + 1, &row);
        check("startup table: a command (its line's terminal escapes out) is the terminal's symbol and itself, its On check off",
              row.order == SETTINGS_STARTUP_ORDER && row.remove && row.cell_check == 1u << SETTINGS_STARTUP_ON_COLUMN && !row.cell_on &&
                  row.icon == UI_ICON_TERMINAL + 1 && !row.tile_picture && !row.cells[1] && string_equals((string_address)row.label, (string_address) "echo hi | tr a-z A-Z"));
        memory_zero(&row, sizeof(row)), settings_describe(first + 3, &row);
        check("startup table: the add row adds a program or a command, its two ways a program and a command, and is no entry",
              row.kind == UI_ROW_ADD && !row.order && !row.remove && string_equals((string_address)row.words, (string_address) "Program|Command") &&
                  string_equals((string_address)row.label, (string_address) "Add a program or command") &&
                  string_equals((string_address)settings_text(first + 3), (string_address)WORDS(settings_startup_add_keys)));

        // The person's changes, on the Boot group as it is shown.
        group = settings_group_of(first), settings.page.chosen = group, settings.page.pane = 1, settings.page.menu_row = -1;
        settings_rebuild();
        settings.list.moved = true, settings.list.moved_from = 0, settings.list.moved_to = 2, settings.list.moved_order = SETTINGS_STARTUP_ORDER;
        settings_managed();
        check("startup table: a drag of the first past the third is move 1 3, and the list is in that order at once, the keys on it",
              settings.startup_jobs_used && (startup_jobs_said(jobs, sizeof(jobs)), string_equals((string_address)jobs, (string_address) "move 1 3")) &&
                  items[2].app == ui_apps_find("settings") && items[0].app < 0 && settings.list.focus == first + 2);
        settings.list.focus = first;
        key.type = WINDOW_EVENT_KEY, key.flags = WINDOW_KEY_DOWN | WINDOW_KEY_ALT, key.a = UI_KEY_DOWN;
        settings_key(&key);
        key.flags = WINDOW_KEY_DOWN, key.a = UI_KEY_DELETE, settings.list.focus = first;
        settings_key(&key);
        settings_startup_apply(SETTINGS_STARTUP_TURN, 0, 0);
        described = settings_startup_entries() == 2 && settings_startup_entry(&items[0], &(struct setting_startup){0}) &&
                    settings.startup_text[items[0].line_at] == 'o' && settings.startup_text[items[0].line_at + 1] == 'n' &&
                    settings.startup_text[items[0].line_at + 2] == ' ';
        settings.list.focus = first;
        settings_key(&key);
        startup_jobs_said(jobs, sizeof(jobs));
        check("startup table: Alt+Down is move 1 2, Delete is remove 1, the switch is on 1, each after the one before, and the same verb twice is two",
              described && string_equals((string_address)jobs, (string_address) "move 1 3|move 1 2|remove 1|on 1|remove 1") && settings_startup_entries() == 1);
        settings.startup_jobs_used = 0;
        // A command typed in the add row: the field has no most, and the whole line is asked.
        for (positive at = 0; at + 1 < sizeof(typed); at++)
                typed[at] = at % 10 == 9 ? ' ' : (char)('a' + at % 26);
        typed[sizeof(typed) - 1] = 0;
        add_row = SETTINGS_LIST_ID(SETTINGS_STARTUP_LIST, settings.item_count[SETTINGS_STARTUP_LIST] - 1);
        settings_startup_way(add_row, 1);
        unbounded = settings_editing() == add_row && settings.edit.most == 0;
        memory_zero(&row, sizeof(row)), settings_describe(add_row, &row);
        unbounded &= row.kind == UI_ROW_FIELD && string_equals((string_address)row.label, (string_address) "Command");
        ui_text_set(&settings.edit, (const p8 address_to)typed, sizeof(typed) - 1);
        settings_edit_commit();
        startup_jobs_said(jobs, sizeof(jobs));
        check("startup table: the Command way is the add row as a field with no most, and what is typed (399 bytes) is add command and all of it",
              unbounded && settings_editing() < 0 && !memory_compare(jobs, "add command ", 12) && string_length((string_address)jobs) == 12 + 399 &&
                  !memory_compare(jobs + 12, typed, 399));
        settings.startup_jobs_used = 0;
        {
                char line[] = "org.example.Tool\t/bowls/bin/tool '--name=It'\\''s here'\n", none[] = "no tab here\n";
                b32 taken = settings_picker_take(line, sizeof(line) - 1), refused = !settings_picker_take(none, sizeof(none) - 1);
                startup_jobs_said(jobs, sizeof(jobs));
                check("startup table: the picker's line is add app, its id and its command whole; a line with no tab is none",
                      taken && refused && string_equals((string_address)jobs, (string_address) "add app org.example.Tool /bowls/bin/tool '--name=It'\\''s here'"));
        }
        settings.startup_jobs_used = 0, settings.item_count[SETTINGS_STARTUP_LIST] = 0, settings.runner.length = 0;
        settings.runner.child = was_child, settings.job = was_job;
        settings.page.chosen = 0, settings_rebuild();
}

/*
        A line of desktop startup (registry.c's setting_startup_entry): on or off, an app's id and its command or a command; its parts are
        found in place. Another first word, an id with a byte no registry id has, an app with no command, or a line's end inside is none.
*/
static fn check_startup_entry(void)
{
        static const char address_to good[] = {"on app files /files", "off command echo hello  world | tr a-z A-Z", "on app org.gnome.Maps flatpak run x"};
        static const char address_to bad[] = {"", "on", "maybe app files /files", "on app files", "on app fi/les /x", "on command ", "on thing x",
                                             "on command a\nb"};
        struct setting_startup e;
        b32 parsed = true, refused = true;

        for (positive at = 0; at < sizeof(good) / sizeof(good[0]); at++)
                parsed &= setting_startup_entry(good[at], string_length((string_address)good[at]), &e);
        setting_startup_entry(good[1], string_length((string_address)good[1]), &e);
        parsed &= !e.on && !e.app && e.command_length == 30 && !memory_compare(e.command, "echo hello  world | tr a-z A-Z", 30);
        setting_startup_entry(good[0], string_length((string_address)good[0]), &e);
        parsed &= e.on && e.app && e.id_length == 5 && !memory_compare(e.id, "files", 5) && e.command_length == 6;
        for (positive at = 0; at < sizeof(bad) / sizeof(bad[0]); at++)
                refused &= !setting_startup_entry(bad[at], string_length((string_address)bad[at]), &e);
        check("startup: on and off, an app's id and its command or a command, are entries whose parts are found in place (a command as typed)", parsed);
        check("startup: another first word, an id with a slash, an app or a command with no command, or a line's end inside is none", refused);
}

static fn check_settings(string_address regular, string_address bold)
{
        check_registry();
        if (!settings_map())
        {
                check("settings: the buffers map", false);
                return;
        }
        // The window's own engine and faces: a row that measures its words (a key that moves the column, a list's rows) has to find
        // them. Without them font_extent is given a null engine.
        {
                static font_face faces[2];
                static p8 atlas[1024 * 1024];
                positive size = 0, bytes = compositor_map_file(regular, &size), bold_size = 0, bold_bytes = bold ? compositor_map_file(bold, &bold_size) : 0;
                long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);

                if (!bytes || (engine < 0 && engine > -4096) || font_load(&faces[0], (const p8 address_to)bytes, size, 0) != 0)
                {
                        check("settings: the window's face loads", false);
                        return;
                }
                settings.ui.engine = (font_engine address_to)engine;
                font_start(settings.ui.engine, atlas, 1024, 1024);
                settings.ui.regular = &faces[0];
                settings.ui.bold = bold_bytes && font_load(&faces[1], (const p8 address_to)bold_bytes, bold_size, 0) == 0 ? &faces[1] : null;
                settings.ui.scale = 100;
        }
        // After the map: a list typed is split in the window's own buffers.
        check_autostart();
        check_startup_entry();
        check_startup_table();
        char value[SETTINGS_VALUE], next[SETTINGS_VALUE];
        string_address words[SETTINGS_WORDS];

        // The window as it opens: the keys in the rows, no list open.
        settings.page.pane = 1;
        settings.page.menu_row = -1;
        int name = settings_row_named("Name"), keyboard = settings_row_named("Keyboard"),
            zone = settings_row_named("Time zone"), ntp = settings_row_named("Set the clock from the network"),
            clock = settings_row_named("Clock"), wifi = settings_row_named("Wi-Fi"),
            prefer = settings_row_named("The internet prefers"), tls = settings_row_named("Names over TLS"),
            power = settings_row_named("Power profile"), bright = settings_row_named("Brightness"),
            boost = settings_row_named("Boost"), bluetooth = settings_row_named("Bluetooth");
        positive count;

        check("settings: a verb's word after its label and its escapes",
              settings_parsed(keyboard, "\x1b[1m[Moonwater]\x1b[0m keyboard us\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "us"));
        // A device chooses what it is called, and device prints that after `; "` on the device's line: a name that says "this
        // machine has no" is a name and takes no row of the USB group away.
        {
                int usb = settings_row_named("New devices");

                check("settings: a device's own name does not take the Devices rows away",
                      usb >= 0 &&
                          settings_parsed(usb, "[Moonwater] device lock\n[Moonwater]   1-1 1234:5678/-@1-1 allowed (rule); \"x this machine has no y\"\n",
                                          value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "lock") &&
                          settings_parsed(usb, "[Moonwater] device lock\n[Moonwater]   this machine has no usb\n", value) != SETTINGS_READ);
        }
        // The class rows are gone (a press on the Devices list allows or blocks a device, and device class is the command), and the
        // list is fed by device list: its feed is the Devices group's, and its rows are not a class switch.
        check("settings: the class rows are gone and the Devices list is fed by device list",
              settings_row_named("Storage") == 0 && settings_row_named("Audio") == 0 && settings_row_named("Hubs") == 0 &&
                  settings_row_named("Other devices") == 0 && settings_row_named("New USB devices") == 0 &&
                  settings_feeds[2].group != null && string_equals((string_address)settings_feeds[2].group, (string_address)"Devices") &&
                  string_equals((string_address)settings_feeds[2].read[0], (string_address)"device") &&
                  string_equals((string_address)settings_feeds[2].read[1], (string_address)"list") &&
                  settings_feeds[2].kind == SETTINGS_FEED_HARDWARE);
        check("settings: a name is the whole of its line", settings_parsed(name, "patient-cricket\n", value) == SETTINGS_READ &&
                                                               string_equals((string_address)value, (string_address) "patient-cricket"));
        check("settings: a zone is the word before its comma",
              settings_parsed(zone, "[Moonwater] timezone Europe/Stockholm, Central European Time (auto, from Cloudflare)\n"
                                    "  local 2026-10-13 17:55:49 CEST\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "Europe/Stockholm"));
        check("settings: ntp is on or off, before the server",
              settings_parsed(ntp, "[Moonwater] ntp on, pool.ntp.org, sampling on, synchronised\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "on"));
        check("settings: the clock is the rest of its indented line",
              settings_parsed(clock, "[Moonwater] timezone Europe/Stockholm\n  local 2026-10-13 17:55:49 CEST\n  utc   x\n",
                              value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "2026-10-13 17:55:49 CEST"));
        check("settings: a radio the machine has no hardware for is not on this machine",
              settings_parsed(wifi, "[Moonwater] wifi on\n[Moonwater] wifi: no wireless hardware found\n", value) == SETTINGS_ABSENT);
        check("settings: and one it has is a switch", settings_parsed(wifi, "[Moonwater] wifi off\n", value) == SETTINGS_READ &&
                                                          string_equals((string_address)value, (string_address) "off"));
        check("settings: the link the internet prefers",
              settings_parsed(prefer, "[Moonwater] internet prefers wired\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "wired"));
        check("settings: names over TLS", settings_parsed(tls, "[Moonwater] dns tls only\n", value) == SETTINGS_READ &&
                                              string_equals((string_address)value, (string_address) "only"));
        {
                // Network discovery's lines, as the verb says them: a connection's addresses are the rest of its line, none says none,
                // and the how line after it is no row's value (the first line that has the key is the value's).
                int wired_ip = settings_row_named("Wired IP address"), wifi_ip = settings_row_named("Wi-Fi IP address");
                static const char said[] = "discovery on\nnat endpoint-independent\n"
                                           "addresses \"eth0\" ethernet 192.168.109.27/22 private\n"
                                           "wired address 192.168.109.27; 10.0.0.9; 2001:db8::7\n"
                                           "wired address how up with a link on \"eth0\"\n"
                                           "wifi address none\nwifi address how no wifi interface\nlearned now\n";

                check("settings: a connection's IP address is the rest of its line, and none says none",
                      wired_ip >= 0 && wifi_ip >= 0 && wired_ip != wifi_ip &&
                          settings_rows[wired_ip].kind == SETTINGS_INFO && settings_rows[wifi_ip].kind == SETTINGS_INFO &&
                          (settings_rows[wired_ip].flags & SETTINGS_TICK) && (settings_rows[wifi_ip].flags & SETTINGS_TICK) &&
                          settings_parsed(wired_ip, said, value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "192.168.109.27; 10.0.0.9; 2001:db8::7") &&
                          settings_parsed(wifi_ip, said, value) == SETTINGS_READ && string_equals((string_address)value, (string_address) "none"));
        }
        check("settings: a machine with no power profile has none",
              settings_parsed(power, "[Moonwater] this machine has no power profile\n", value) == SETTINGS_ABSENT);
        check("settings: a profile not set here is the platform's",
              settings_parsed(power, "[Moonwater] power (not set here); platform profile balanced; governor powersave\n", value) ==
                      SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "balanced"));
        check("settings: a brightness is its percent",
              settings_parsed(bright, "[Moonwater] brightness 50% (50 of 100)\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "50%"));
        // The brightness is a kit slider over 0 to 100 percent: the keys step it, a drag writes each value it holds, a release writes the
        // value it was left at, and a display with no backlight is greyed out with its reason.
        {
                struct ui_row described;
                unsigned char was = settings.state[bright];
                bipolar child_was = settings.runner.child;
                settings.runner.child = 1; // a job that is running keeps the changes queued, as the window has them while one runs
                char had[SETTINGS_VALUE];

                memory_copy(had, settings.value[bright], SETTINGS_VALUE);
                settings.state[bright] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[bright], (string_address) "40%", SETTINGS_VALUE);
                memory_zero(&described, sizeof(described));
                settings_describe(bright, &described);
                check("settings: brightness is a slider over 0 to 100 percent, at its number, in percent",
                      settings_rows[bright].kind == SETTINGS_SLIDER && described.kind == UI_ROW_SLIDER && described.at == 40 && described.low == 0 &&
                          described.high == 100 && described.words && string_equals((string_address)described.words, (string_address) "%"));
                settings_activate(bright, 1);
                check("settings: the keys step brightness a step up, and write it", settings.queued == 1 && settings.queue[0].row == bright &&
                          string_equals((string_address)settings.queue[0].value, (string_address) "45"));
                settings.queued = 0;
                settings.list.slide = bright + 1, settings.list.slid_value = 60;
                settings_slid();
                check("settings: a drag writes brightness live, the value it holds", settings.queued == 1 && settings.queue[0].row == bright &&
                          string_equals((string_address)settings.queue[0].value, (string_address) "60"));
                settings_slid();
                check("settings: and holding the same value writes it once", settings.queued == 1);
                settings.list.slide = 0, settings.list.slid = bright + 1, settings.list.slid_value = 70;
                settings_slid();
                check("settings: a release writes the value it was left at", settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "70"));
                settings.queued = 0;
                settings_live_sent = -1;
                settings.state[bright] = SETTINGS_ABSENT;
                memory_zero(&described, sizeof(described));
                settings_describe(bright, &described);
                check("settings: a display with no backlight has brightness greyed out, with the reason, and no slider",
                      described.dim && described.kind != UI_ROW_SLIDER && described.about && described.about[0]);
                check("settings: a machine with no backlight says so, and the row is absent",
                      settings_parsed(bright, "[Moonwater] this machine has no backlight\n", value) == SETTINGS_ABSENT);
                settings.state[bright] = was;
                memory_copy(settings.value[bright], had, SETTINGS_VALUE);
                settings.queued = 0;
                settings.list.slide = 0, settings.list.slid = 0;
                settings.runner.child = child_was;
        }
        check("settings: boost is after a semicolon, and a machine without one has none",
              settings_parsed(boost, "[Moonwater] cpu: 8 online; boost off; smt on\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "off") &&
                  settings_parsed(boost, "[Moonwater] cpu: 2 online; smt notsupported\n", value) == SETTINGS_ABSENT);
        {
                static const char no_profile[] = "[Moonwater] this machine has no power profile\n";

                check("settings: a verb that did not run is not a machine that has none of the thing",
                      settings_read(&settings_rows[power], "", 0, value, SETTINGS_VALUE, true) == SETTINGS_FAILED &&
                          settings_read(&settings_rows[power], "", 0, value, SETTINGS_VALUE, false) == SETTINGS_ABSENT &&
                          settings_read(&settings_rows[power], no_profile, sizeof(no_profile) - 1, value, SETTINGS_VALUE, true) ==
                              SETTINGS_ABSENT);
        }
        check("settings: a verb that says nothing understood is a failure, not a value",
              settings_parsed(keyboard, "moonwater: something else\n", value) == SETTINGS_FAILED && !value[0]);

        // Spacing the verbs do not mean: more than one blank after a label, a tab for a blank, blanks at the end of a value.
        check("settings: a value after more than one blank is the word after them",
              settings_parsed(keyboard, "[Moonwater] keyboard   us\n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "us"));
        check("settings: a tab is a blank", settings_parsed(keyboard, "[Moonwater] keyboard\tus\n", value) == SETTINGS_READ &&
                                                string_equals((string_address)value, (string_address) "us"));
        check("settings: blanks at the end of a name are not its name",
              settings_parsed(name, "patient-cricket   \n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "patient-cricket"));
        check("settings: and blanks at the end of a switch's word are not its word",
              settings_parsed(wifi, "[Moonwater] wifi off  \n", value) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address) "off"));

        // Changes made through the rows, with a verb running: each waits for it, and no verb is started (child is a stand-in).
        settings.runner.child = 1;
        settings.queued = 0;
        ui_field_close(&settings.edit);
        settings.job = clock;
        settings.job_sets = false;
        settings.state[wifi] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[wifi], (string_address) "busy", SETTINGS_VALUE);
        settings_activate(wifi, -2);
        check("settings: Left throws a switch off, whatever its value said",
              settings.queued == 1 && settings.queue[0].row == wifi && string_equals((string_address)settings.queue[0].value, (string_address) "off"));
        settings.queued = 0;
        settings.job = clock;
        settings.job_sets = true;
        settings.state[clock] = SETTINGS_READ;
        settings_activate(clock, 1);
        check("settings: a clock set that is running is not asked for again", settings.queued == 0);
        settings.job_sets = false;
        settings.state[bluetooth] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[bluetooth], (string_address) "off", SETTINGS_VALUE);
        settings_activate(wifi, 1);
        settings_activate(bluetooth, 1);
        check("settings: a change made while another waits is not lost",
              settings.queued == 2 && settings.queue[0].row == wifi && settings.queue[1].row == bluetooth &&
                  string_equals((string_address)settings.queue[1].value, (string_address) "on"));
        settings_activate(wifi, -2);
        check("settings: and a change to a row waiting replaces its change", settings.queued == 2 &&
                  string_equals((string_address)settings.queue[0].value, (string_address) "off"));
        settings.queued = 0;
        //      A switch pressed is where it goes, at work (the kit's working switch), until its row is read back: a second press takes it
        //      back, the read after the change reads its row first, and read back it is what was read and at rest.
        {
                static const char said[] = "[Moonwater] bluetooth on\n";
                struct ui_row turned;
                b32 going, back, first, rest;

                memory_zero(settings.turning, sizeof(settings.turning)), memory_zero(settings.unread, sizeof(settings.unread));
                settings.state[bluetooth] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[bluetooth], (string_address) "off", SETTINGS_VALUE);
                settings_activate(bluetooth, 1);
                memory_zero(&turned, sizeof(turned)), settings_describe(bluetooth, &turned);
                going = settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "on") && turned.kind == UI_ROW_SWITCH &&
                        turned.on && turned.working;
                settings_activate(bluetooth, 1);
                memory_zero(&turned, sizeof(turned)), settings_describe(bluetooth, &turned);
                back = settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "off") && !turned.on && turned.working;
                settings_activate(bluetooth, 1);
                settings.queued = 0; // its change ran; the read of every row follows: here the first row and the switch's, unread
                settings_unread_set(0, 1), settings_unread_set((positive)bluetooth, 1);
                first = settings_unread_first() == (positive)bluetooth;
                memory_copy(settings_large->said, said, sizeof(said) - 1), settings.runner.length = sizeof(said) - 1;
                settings.job = bluetooth, settings.job_sets = false;
                settings_complete(false, 0);
                memory_zero(&turned, sizeof(turned)), settings_describe(bluetooth, &turned);
                rest = !settings_turning_is(bluetooth) && string_equals((string_address)settings.value[bluetooth], (string_address) "on") && turned.on &&
                       !turned.working;
                check("settings: a switch pressed is where it goes and at work while its verb waits, and a second press takes it back", going && back);
                check("settings: its row is read back first after the change, and then it is what was read, at rest", first && rest);
                memory_zero(settings.turning, sizeof(settings.turning)), memory_zero(settings.unread, sizeof(settings.unread));
                settings.runner.length = 0, settings.job = clock;
        }
        //      The window's least width lays every group out in turn: the keys stay on their row and the rows where they were scrolled (each
        //      group's rebuild put the keys on its first row, so a value read back, which asks for the least again, lost the person's row).
        {
                int kept = settings_row_named("Mount kept disks"), focus, scrolled;
                settings_show(kept);
                focus = settings.list.focus, scrolled = settings.list.scroll.scroll;
                settings.least_key = 0, settings_least();
                check("settings: the least width's walk of every group leaves the keys on their row, the rows where they were and the group shown",
                      focus == kept && settings.list.focus == focus && settings.list.scroll.scroll == scrolled && settings.page.chosen == settings_group_of(kept));
        }
        /*
                A deliberate change (registry.c's deliberate: the SSH server on, root let in, a key added) is made by a key or a press a
                device plugged into the machine sent, never by one a virtual device sent (WINDOW_EVENT_VIRTUAL: uinput, uhid, the on-screen
                keyboard, the headless pipe), which the status refuses: Enter on SSH server as each sends it, the server turned off by
                either, the same verbs typed in All commands, and a key added. A row that is not deliberate takes either.
        */
        {
                int server = settings_row_named("SSH server"), add = settings_row_named("Add a key (label, then the public key)");
                struct window_event virtual_enter = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN | WINDOW_EVENT_VIRTUAL, UI_KEY_ENTER, 0};
                struct window_event plugged_enter = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_ENTER, 0};
                string_address refusal = (string_address)WORDS(settings_virtual_refused);
                b32 refused, taken, off, typed, typed_root, typed_port, added, added_plugged, other;

                settings.popup.open = 0, ui_field_close(&settings.edit), (void)0, settings.page.card = false, settings_filter_clear(), (void)0;
                settings.page.menu_row = -1, ui_confirm_drop(&settings.confirm);
                settings.state[server] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[server], (string_address) "off", SETTINGS_VALUE);
                settings.page.chosen = settings_group_of(server);
                settings_refilter();
                settings.page.pane = 1;
                settings.list.focus = server;
                settings_say((string_address) "");
                settings_event_at(&virtual_enter);
                refused = settings.queued == 0 && string_equals((string_address)settings.status, refusal);
                settings_event_at(&plugged_enter);
                taken = settings.queued == 1 && settings.queue[0].row == server &&
                        string_equals((string_address)settings.queue[0].value, (string_address) "on");
                settings.queued = 0;
                string_copy_bounded((string_address)settings.value[server], (string_address) "on", SETTINGS_VALUE);
                settings_event_at(&virtual_enter);
                off = settings.queued == 1 && settings.queue[0].row == server &&
                      string_equals((string_address)settings.queue[0].value, (string_address) "off");
                settings.queued = 0;
                // Between events no change is a hand's: the commands typed in All commands, as Enter would run them.
                settings_say((string_address) "");
                typed = !settings_run(SETTINGS_ANY, "ssh on") && settings.queued == 0 && string_equals((string_address)settings.status, refusal);
                settings_say((string_address) "");
                typed_root = !settings_run(SETTINGS_ANY, "ssh root on") && settings.queued == 0 &&
                             string_equals((string_address)settings.status, refusal);
                typed_port = settings_run(SETTINGS_ANY, "ssh port 2222") && settings.queued == 1;
                settings.queued = 0;
                settings_say((string_address) "");
                settings_change(add, "laptop ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl");
                added = settings.queued == 0 && string_equals((string_address)settings.status, refusal);
                settings.hand = true;
                settings_change(add, "laptop ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl");
                added_plugged = settings.queued == 1 && settings.queue[0].row == add;
                settings.hand = false;
                settings.queued = 0;
                settings.state[wifi] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[wifi], (string_address) "off", SETTINGS_VALUE);
                settings_activate(wifi, 1);
                other = settings.queued == 1 && settings.queue[0].row == wifi;
                settings.queued = 0;
                string_format(log, "  settings deliberate: virtual Enter %p, plugged Enter %p, off %p, typed %p %p %p, a key %p %p, another row %p\n",
                              (positive)refused, (positive)taken, (positive)off, (positive)typed, (positive)typed_root, (positive)typed_port,
                              (positive)added, (positive)added_plugged, (positive)other);
                check("settings: the SSH server is turned on by an Enter a plugged keyboard sent and not by a virtual device's, which the status refuses",
                      server >= 0 && refused && taken);
                check("settings: and it is turned off by either, and a row that is not deliberate takes a change with no hand", off && other);
                check("settings: ssh on and ssh root on typed in All commands are refused without a hand, and ssh port is not",
                      typed && typed_root && typed_port);
                check("settings: a key is added by a hand and not without one", add >= 0 && added && added_plugged);
                settings_say((string_address) "");
        }
        // A slider row (the taskbar scale): the kit's slider over the row's range at the number the verb said, moved a step by the keys inside
        // its range, and changed once to the value the hand left it at. Its unit is a percent, which the row says as the sign the kit writes
        // against the number (125%), and not as the registry's word for it.
        {
                int scale = settings_row_named("Taskbar scale (percent)");
                struct ui_row described;

                settings.state[scale] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[scale], (string_address) "125", SETTINGS_VALUE);
                memory_zero(&described, sizeof(described));
                settings_describe(scale, &described);
                check("settings: the taskbar scale is a slider row over 50 to 200 at the number the verb said, in percent",
                      described.kind == UI_ROW_SLIDER && described.at == 125 && described.low == 50 && described.high == 200 && described.step == 5 &&
                          described.words && string_equals((string_address)described.words, (string_address) "%"));
                check("settings: the keys step a slider row, which is no field and no choice", settings_steps(scale) &&
                          settings_rows[scale].kind == SETTINGS_SLIDER);
                settings_activate(scale, 1);
                check("settings: Right moves a slider a step on", settings.queued == 1 && settings.queue[0].row == scale &&
                          string_equals((string_address)settings.queue[0].value, (string_address) "130"));
                settings.queued = 0;
                string_copy_bounded((string_address)settings.value[scale], (string_address) "200", SETTINGS_VALUE);
                settings_activate(scale, 1);
                string_copy_bounded((string_address)settings.value[scale], (string_address) "50", SETTINGS_VALUE);
                settings_activate(scale, -1);
                check("settings: and a slider is held inside its range by the keys", settings.queued == 0);
                settings.list.slid = scale + 1, settings.list.slid_value = 80;
                settings_slid();
                check("settings: a slider the hand let go changes its row to the value it was left at, once", settings.queued == 1 &&
                          settings.queue[0].row == scale && string_equals((string_address)settings.queue[0].value, (string_address) "80") &&
                          settings.list.slid == 0);
                settings_slid();
                check("settings: and asks nothing more", settings.queued == 1);
                settings.queued = 0;
        }
        settings.runner.child = 0;
        settings.queued = 0;

        settings_next(&settings_rows[ntp], "on", 1, next, sizeof(next));
        check("settings: a switch on goes off", string_equals((string_address)next, (string_address) "off"));
        settings_next(&settings_rows[keyboard], "us", 1, next, sizeof(next));
        check("settings: a choice is not moved a step at a time, it opens a list", !next[0]);
        settings_next(&settings_rows[bright], "50%", -1, next, sizeof(next));
        check("settings: a slider is no word to step: brightness is a slider, so its keys move it through the row and no word is next", !next[0] && settings_rows[bright].kind == SETTINGS_SLIDER);

        count = settings_command(&settings_rows[name], (string_address) "/moonwater", "a b; rm -rf /", words, SETTINGS_WORDS, false);
        check("settings: a name typed is one word of the command, whatever is in it",
              count == 3 && string_equals(words[1], (string_address) "name") &&
                  string_equals(words[2], (string_address) "a b; rm -rf /") && !words[3]);
        count = settings_command(&settings_rows[prefer], (string_address) "/moonwater", "wifi", words, SETTINGS_WORDS, false);
        check("settings: a choice's verb, then its word", count == 4 && string_equals(words[1], (string_address) "priority") &&
                                                             string_equals(words[2], (string_address) "internet") &&
                                                             string_equals(words[3], (string_address) "wifi"));
        count = settings_command(&settings_rows[clock], (string_address) "/moonwater", "ignored", words, SETTINGS_WORDS, false);
        check("settings: an action is its verb alone", count == 3 && string_equals(words[2], (string_address) "sync") && !words[3]);
        count = settings_command(&settings_rows[tls], (string_address) "/moonwater", "", words, SETTINGS_WORDS, true);
        check("settings: and what is asked is the verb that shows it", count == 3 && string_equals(words[1], (string_address) "dns") &&
                                                                           string_equals(words[2], (string_address) "tls"));

        // The rows a person flips beyond the first: what each says, and the words that set it.
        {
                int modes = settings_row_named("Screen modes"),
                    server = settings_row_named("Time server (auto for the pool)"),
                    sampling = settings_row_named("Keep the lowest-delay sample"),
                    nts = settings_row_named("Authenticated time (NTS)"),
                    nts_servers = settings_row_named("NTS servers (or default)"),
                    dns = settings_row_named("Name servers (auto for the network)"),
                    smt = settings_row_named("Simultaneous threads (SMT)"), charge = settings_row_named("Charge limit (20 to 100, or off)"),
                    desktop = settings_row_named("Start the desktop"), mount = settings_row_named("Mount kept disks");

                check("settings: the screen modes", settings_parsed(modes, "[Moonwater] modes preferred, kept across boots\n", value) == SETTINGS_READ &&
                                                        string_equals((string_address)value, (string_address) "preferred"));
                check("settings: the time server is a word, and sampling a switch",
                      settings_parsed(server, "[Moonwater] ntp server pool.ntp.org\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "pool.ntp.org") &&
                          settings_parsed(sampling, "[Moonwater] ntp sampling off\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "off"));
                check("settings: authenticated time is its word, before the servers",
                      settings_parsed(nts, "[Moonwater] ntp nts only, servers time.cloudflare.com:4460 nts.ntp.se; the clock was last set by nts\n",
                                      value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "only"));
                check("settings: and its servers are the words from there to the semicolon",
                      settings_parsed(nts_servers,
                                      "[Moonwater] ntp nts only, servers time.cloudflare.com:4460 nts.ntp.se; the clock was last set by nts\n",
                                      value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "time.cloudflare.com:4460 nts.ntp.se"));
                check("settings: the name servers are the addresses before their word in brackets",
                      settings_parsed(dns, "[Moonwater] dns 1.1.1.1 9.9.9.9 (kept)\n[Moonwater] dns tls off\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "1.1.1.1 9.9.9.9"));
                check("settings: the charge limit is its number, and a battery without one has none",
                      settings_parsed(charge, "[Moonwater] battery 80%, Charging; charging stops at 80%\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "80") &&
                          settings_parsed(charge, "[Moonwater] battery 90%, Full; this battery has no charge limit\n", value) == SETTINGS_ABSENT &&
                          settings_parsed(charge, "[Moonwater] this machine has no battery\n", value) == SETTINGS_ABSENT);
                check("settings: SMT is on or off, and a machine that cannot say so has none",
                      settings_parsed(smt, "[Moonwater] cpu: 8 online; boost on; smt on\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "on") &&
                          settings_parsed(smt, "[Moonwater] cpu: 2 online; smt notsupported\n", value) == SETTINGS_ABSENT);
                check("settings: what the machine starts at boot",
                      settings_parsed(desktop, "[Moonwater] init canvas on\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "on") &&
                          settings_parsed(mount, "[Moonwater] init mount off\n", value) == SETTINGS_READ &&
                          string_equals((string_address)value, (string_address) "off"));

                count = settings_command(&settings_rows[dns], (string_address) "/moonwater", "1.1.1.1 9.9.9.9", words, SETTINGS_WORDS, false);
                check("settings: a list of servers is the words of the command, one each",
                      count == 4 && string_equals(words[1], (string_address) "dns") && string_equals(words[2], (string_address) "1.1.1.1") &&
                          string_equals(words[3], (string_address) "9.9.9.9") && !words[4]);
                count = settings_command(&settings_rows[nts_servers], (string_address) "/moonwater", "a:1 b", words, SETTINGS_WORDS, false);
                check("settings: and after three words of the verb", count == 6 && string_equals(words[3], (string_address) "servers") &&
                                                                         string_equals(words[4], (string_address) "a:1") &&
                                                                         string_equals(words[5], (string_address) "b"));
                count = settings_command(&settings_rows[charge], (string_address) "/moonwater", "80", words, SETTINGS_WORDS, false);
                check("settings: a charge limit is charge limit and the number",
                      count == 4 && string_equals(words[2], (string_address) "limit") && string_equals(words[3], (string_address) "80"));

                // The desktop's colour and the pointer: what canvas background and canvas cursor say, and the words that set them.
                {
                        int ink = settings_row_named("Background"), colour = settings_row_named("Background colour (rrggbb or default)"),
                            size = settings_row_named("Cursor size (percent, or default)"), look = settings_row_named("Cursor look");

                        check("settings: the desktop colour has a choice of inks and a field, and the pointer a size slider and a look",
                              settings_rows[ink].kind == SETTINGS_CHOICE && (settings_rows[ink].flags & SETTINGS_INK) &&
                                  settings_rows[colour].kind == SETTINGS_FIELD && !(settings_rows[colour].flags & SETTINGS_INK) &&
                                  settings_rows[size].kind == SETTINGS_SLIDER && settings_rows[look].kind == SETTINGS_CHOICE &&
                                  settings_group_of(colour) == settings_group_of(ink) &&
                                  settings_group_of(size) == settings_group_of(look) && settings_group_of(size) != settings_group_of(ink) &&
                                  settings_same_verb(&settings_rows[ink], &settings_rows[colour]) &&
                                  !settings_same_verb(&settings_rows[size], &settings_rows[look]) &&
                                  !settings_same_verb(&settings_rows[ink], &settings_rows[size]) &&
                                  settings_options(&settings_rows[ink]) == sizeof(settings_inks) / sizeof(settings_inks[0]));
                        check("settings: the first ink is the default",
                              string_equals((string_address)settings_inks[0].name, (string_address) "default") &&
                                  string_equals((string_address)settings_inks[0].colour, (string_address) "1e1e1e"));
                        check("settings: the field says the colour as the verb does, and the choice by the ink's name",
                              settings_read(&settings_rows[colour], "[Moonwater] background #1e1e1e\n", 30, value, SETTINGS_VALUE, false) ==
                                      SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "#1e1e1e") &&
                                  settings_read(&settings_rows[ink], "[Moonwater] background #1e1e1e\n", 30, value, SETTINGS_VALUE, false) ==
                                      SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "default") &&
                                  settings_read(&settings_rows[ink], "[Moonwater] background #163a26\n", 30, value, SETTINGS_VALUE, false) ==
                                      SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "forest") &&
                                  settings_read(&settings_rows[ink], "[Moonwater] background #123456\n", 30, value, SETTINGS_VALUE, false) ==
                                      SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "#123456"));
                        check("settings: the pointer's size, a percent, and its look are each one word of the one line the verb says",
                              settings_parsed(size, "[Moonwater] cursor size 80 percent, look dark\n", value) == SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "80") &&
                                  settings_parsed(look, "[Moonwater] cursor size 80 percent, look dark\n", value) == SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "dark") &&
                                  settings_parsed(size, "[Moonwater] cursor size 300 percent, look auto\n", value) == SETTINGS_READ &&
                                  string_equals((string_address)value, (string_address) "300"));
                        {
                                struct ui_row described;
                                unsigned char state = settings.state[size];
                                char had[SETTINGS_VALUE];

                                memory_copy(had, settings.value[size], SETTINGS_VALUE);
                                settings.state[size] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[size], (string_address) "400", SETTINGS_VALUE);
                                memory_zero(&described, sizeof(described));
                                settings_describe(size, &described);
                                check("settings: the pointer's size is a slider of 50 to 300 in steps of 5, in percent, and a 4 kept before (400) rests at its end",
                                      described.kind == UI_ROW_SLIDER && described.at == 300 && described.low == 50 && described.high == 300 &&
                                          described.step == 5 && described.words && string_equals((string_address)described.words, (string_address) "%"));
                                settings.state[size] = state;
                                memory_copy(settings.value[size], had, SETTINGS_VALUE);
                        }
                        count = settings_command(&settings_rows[ink], (string_address) "/moonwater", "plum", words, SETTINGS_WORDS, false);
                        check("settings: an ink is set as its rrggbb, after canvas background",
                              count == 4 && string_equals(words[1], (string_address) "canvas") &&
                                  string_equals(words[2], (string_address) "background") && string_equals(words[3], (string_address) "3b1f4a") &&
                                  !words[4]);
                        count = settings_command(&settings_rows[ink], (string_address) "/moonwater", "default", words, SETTINGS_WORDS, false);
                        check("settings: and the default ink is its colour", count == 4 && string_equals(words[3], (string_address) "1e1e1e"));
                        count = settings_command(&settings_rows[colour], (string_address) "/moonwater", "a0b1c2", words, SETTINGS_WORDS, false);
                        check("settings: a colour typed is the word the verb is run with, whatever it is",
                              count == 4 && string_equals(words[3], (string_address) "a0b1c2"));
                        count = settings_command(&settings_rows[size], (string_address) "/moonwater", "150", words, SETTINGS_WORDS, false);
                        check("settings: the pointer's size is canvas cursor size and its percent",
                              count == 5 && string_equals(words[2], (string_address) "cursor") && string_equals(words[3], (string_address) "size") &&
                                  string_equals(words[4], (string_address) "150") && !words[5]);
                        count = settings_command(&settings_rows[look], (string_address) "/moonwater", "light", words, SETTINGS_WORDS, false);
                        check("settings: and its look is canvas cursor look and its word",
                              count == 5 && string_equals(words[3], (string_address) "look") && string_equals(words[4], (string_address) "light"));
                        count = settings_command(&settings_rows[look], (string_address) "/moonwater", "", words, SETTINGS_WORDS, true);
                        check("settings: and what is asked of the pointer's look is canvas cursor look, which says it alone",
                              count == 4 && string_equals(words[1], (string_address) "canvas") && string_equals(words[2], (string_address) "cursor") &&
                                  string_equals(words[3], (string_address) "look"));
                }
        }

        // All commands: the rows of what moonwater help says, as the window reads them.
        {
                static const char help[] =
"Moonwater\n"
"\n"
"Machine\n"
"  language [CODE|default]           the language the windows Canvas draws itself speak; bare lists them, machine-translated ones marked [en]\n"
"  name [NEW|random]                 what this machine is called, like space-wizard\n"
"\n"
"\n"
"Canvas\n"
"  canvas                            whether Canvas is on, and the screens it draws on\n"
"  canvas arrange [NAME[@X,Y|@off]...|default]\n"
"                                    where each screen is: names left to right, the first the primary [default]\n"
"  canvas background [COLOUR|default]\n"
"                                    the desktop colour, rrggbb [1e1e1e]\n"
"  canvas caret blink [slow|normal|fast|off]\n"
"                                    how the caret blinks, or off for a solid caret [normal]\n"
"  canvas caret thickness [thin|normal|thick]\n"
"                                    how wide the caret is: 1, 2 or 3 pixels at 100%; a block cursor is a whole cell [normal]\n"
"  canvas clipboard [on|off]         copy and paste between windows; off empties the clipboard [on]\n"
"  canvas clipboard history [N|default]\n"
"                                    copies the compositor keeps in memory, never on a disk, 0 to 32 [0]\n"
"  canvas gpu [on|off]               draw and compose on the GPU where Canvas has a driver for it, on the processor elsewhere; alpha [off]\n"
"  canvas identify                   show the number of each screen for three seconds\n"
"  canvas motion [full|reduced]      reduced: the windows Canvas draws change at once, with no spring, stretch or ripple [full]\n"
"  canvas record [SECONDS]           record the presents of the compositor for SECONDS (1 to 10, 3 by default)\n"
"  canvas record report              the report of the presents: the paths, flips, late and held presents\n"
"  canvas text scale [PERCENT|default]\n"
"                                    how large the text of the windows Canvas draws is, 50 to 300 percent [100]\n"
"  canvas theme [light|dark|auto]    the colours of the windows Canvas draws itself; auto is light from 7 to 19 by the clock [dark]\n"
"  canvas theme accent [COLOUR|reset]\n"
"                                    what the controls fill with, rrggbb; the text on it is picked to be read [ffffff]\n"
"  canvas windows [forget]           where windows open again, from where the hand put them; forget empties it\n"
"  canvas windows remember [on|off]  a window opens where the hand last put it, kept across boots [on]\n"
"\n"
"\n"
"Display\n"
"  brightness [N%|+N|-N]             the screen backlight\n"
"  canvas modes [largest|preferred]  which mode a screen is driven at [largest]\n"
"\n"
"\n"
"Input\n"
"  canvas cursor [size [PERCENT|default]|look [light|dark|auto]]\n"
"                                    the pointer: its size, 50 to 300 percent of the size the desktop scale gives it, and its colours [80, auto]\n"
"  canvas cursor style [classic|dynamic]\n"
"                                    the classic pointers, or one filled shape that stretches with a fast hand and is a rounded block over text [dynamic]\n"
"  canvas cursor squeeze [PERCENT|default]\n"
"                                    how far a pointer shrinks while a button is held, 0 to 50 percent, 0 for none; the right button grows it by half that [25]\n"
"  canvas cursor shake [on|off]      a shake of the hand makes the pointer large for a moment [on]\n"
"  canvas cursor shake delay [MS|default]\n"
"                                    how long it stays large after the last shake, 100 to 10000 ms [1200]\n"
"  canvas cursor click [on|off]      a held button changes how large the pointer is: the left shrinks it, the right grows it [on]\n"
"  canvas predict touch [on|off]     a finger or a pen is taken a frame ahead of where it is, to the windows it reaches [on]\n"
"  canvas touch [on|off]             a circle under each finger on a touch screen, fading for a second after it lifts [on]\n"
"  canvas touch left [off|windows|launcher]\n"
"                                    a swipe in from the left edge runs the window switcher, or opens the launcher [off]\n"
"  canvas touch right [off|windows|volume]\n"
"                                    a swipe in from the right edge runs the window switcher, or opens the volume popup [off]\n"
"  canvas wheel smooth [on|off]      a notch runs to its step (two and a half rows) at once and eases out; off moves a whole step at once [on]\n"
"  keyboard [LAYOUT|list|xkb]        us uk gb de se sv no nb dk fi fr es it ar il ir [us]\n"
"  keyboard onscreen [auto|off|on]   a keyboard on the screen while a text field has the keys: auto when no keyboard is plugged in, off, or on [auto]\n"
"  keyboard repeat delay [MS|default]\n"
"                                    how long a key is held before it repeats, 100 to 2000 ms [250]\n"
"  keyboard repeat rate [PER-SECOND|off|default]\n"
"                                    repeats a second of a held key, 1 to 100, or none [30]\n"
"  keyboard start                    exit 0 when the desktop starts the on-screen keyboard: the setting on, or auto with a touch screen plugged in or no keyboard\n"
"  mouse acceleration [on|off]       the pointer outruns a fast hand; off is one to one [on]\n"
"  mouse sensitivity [PERCENT|default]\n"
"                                    how far the pointer goes for the hand, 10 to 500 percent [100]\n"
"\n"
"\n"
"Scrolling\n"
"  canvas scroll momentum [on|off]   a drag or a trackpad hand carries on past its release and slows; off, the content stops where the fingers leave it, for the guest only: momentum from a patched host is not stopped (no phase signal yet) [on]\n"
"  canvas scroll natural [on|off]    a wheel and a trackpad move the content with the fingers, as on a touch screen; off, the other way, as a wheel mouse does [off]\n"
"  canvas scroll rubber [on|off]     past the top or the bottom the content stretches and springs back; off, it stops at the end [on]\n"
"  canvas trackpad speed [PERCENT|slow|normal|fast|default]\n"
"                                    how far a wheel notch and a trackpad scroll move the content, 25 to 400 percent: a notch is two and a half rows, a stream notch of a trackpad 8 px at 100 and more as its notches come faster; slow, normal and fast are 60, 100 and 160 [100]\n"
"\n"
"\n"
"Sound\n"
"  audio                             the volume and the mute, the sound card and the cards there are, the programs that play, the microphone and the programs that record, and whether the sound server starts\n"
"  audio capture [on|off]            let programs record from the microphone; off ends every recording and refuses new ones [on]\n"
"  audio device [NAME|default]       the sound card that plays, by the name bare audio lists; default picks the first that is not HDMI [default]\n"
"  audio input mute [on|off]         silence the microphone, and keep its volume [off]\n"
"  audio input volume [PERCENT|default]\n"
"                                    the volume of the microphone, 0 to 100 percent [100]\n"
"  audio mute [on|off]               silence everything that plays, and keep the volume [off]\n"
"  audio server [on|off]             the server programs play their sound through; off keeps it from starting with the desktop [on]\n"
"  audio source [NAME|default]       the microphone programs record from, by the name bare audio lists; default picks the first [default]\n"
"  audio test                        play a second of 440 Hz through the sound server and say what became of it: no server, no card, a mixer that is off, muted, or played on this card\n"
"  audio volume [PERCENT|default]    the volume of everything that plays, 0 to 100 percent [100]\n"
"\n"
"\n"
"Taskbar\n"
"  taskbar display [primary|all]     the screens the taskbar is on: the primary, or all, each listing its windows [primary]\n"
"  taskbar fullscreen [on|off]       the taskbar slides away while a window is fullscreen, and comes back when none is [on]\n"
"  taskbar launcher [on|off]         a tab with the launcher mark at the start of the taskbar that opens the launcher beside it and puts it away again, as the logo key alone does [on]\n"
"  taskbar padding [PX|default]      the room inside each taskbar button before what it shows and after it (the icon and title of a task, the moon, a tray item), 0 to 24 pixels at the taskbar scale [12]\n"
"  taskbar position [bottom|top|left|right]\n"
"                                    the edge of the screen the taskbar sits on [bottom]\n"
"  taskbar scale [PERCENT|default]   how large the taskbar and what it shows are, 50 to 200 percent of the screen scale [100]\n"
"  taskbar text [center|edge]        on a left or right taskbar, which way the tops of the letters face: the middle of its screen or its edge [center]\n"
"  taskbar tray [FORMAT|default]     what the tray shows: text with {battery} {battery.percent} {battery.state} {time.24h.hour} {time.24h.min} {time.12h.hour} {time.ampm} {date.year} {date.month} {date.day} {date.weekday} {date.monthname} {cpu.load} {mem.used} {temp} {host} in it; {{ and }} are the braces [the battery, then the clock]\n"
"\n"
"\n"
"Time\n"
"  ntp [on|off]                      set the clock from the network [on]\n"
"  ntp nts [on|off|only]             authenticated time (RFC 8915) first; only refuses plain [on]\n"
"  ntp nts servers [NAME[:PORT]...|default]\n"
"                                    who is asked for it, up to four\n"
"  ntp sampling [on|off]             keep the lowest-delay sample of five [on]\n"
"  ntp server [NAME|auto]            who is asked first: a name or address [auto]\n"
"  time [sync]                       the clock; sync sets it and the zone now\n"
"  time date [off|ymd|dmy|mdy]       the date beside the clock on the tray, year first, day first or month first, and in GNOME programs [off]\n"
"  time format [24h|12h]             the hours the clock is read in, on the tray and in GNOME programs [24h]\n"
"  timezone [ZONE|list]              IANA name, country, +1 or POSIX; manual\n"
"  timezone auto                     from the network, one Cloudflare request\n"
"                                    per network joined [auto]\n"
"\n"
"\n"
"Network\n"
"  airplane [on|off]                 every radio at once\n"
"  dns [ADDRESS...|auto]             up to three servers names are asked of, or the network gives them [auto]\n"
"  dns tls [on|off|only]             names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]\n"
"  priority [internet [wired|wifi]]  which link wins when both are up [wired]\n"
"  wired [on|off]                    the wired links; off keeps them down\n"
"\n"
"\n"
"Waterlink\n"
"  link [pair|NAME]                  machines linked by name, with a code\n"
"\n"
"\n"
"Wi-Fi\n"
"  wifi [on|off]                     the radio, saved networks and those in range [off]\n"
"  wifi add SSID [PASSWORD|-] [wpa2]\n"
"                                    remember a network, joined once wifi is on; asks for\n"
"                                    the password, - reads it from stdin; wpa2 lets\n"
"                                    a tight tier join it without WPA3\n"
"  wifi remove SSID                  forget a saved network, and leave it\n"
"\n"
"\n"
"Bluetooth\n"
"  bluetooth [on|off]                the radio and the remembered devices [off]\n"
"  bluetooth add NAME                keep the name of a device, found once the radio is on\n"
"  bluetooth remove NAME             forget a bluetooth device\n"
"  bluetooth share [on|off]          the internet of this machine, shared with the Bluetooth devices that pair with it; needs the Bluetooth network tools [off]\n"
"  bluetooth share [on|off]          the internet of this machine over Bluetooth to the devices that pair with it; off until on (root)\n"
"\n"
"\n"
"Devices\n"
"  device [lock|unlock]              unlock lets a new device in; lock holds it until it is allowed [unlock]\n"
"  device [lock|unlock]              lock keeps what is plugged in now, and holds a new device until it is allowed [unlock]\n"
"  device allow|deny ID|PORT         let one device in, or hold it, now and at every plug;\n"
"                                    ID is VID:PID, or VID:PID/SERIAL as device shows it\n"
"  device apply                      judge every device again now, as boot does\n"
"  device class NAME [allow|deny]    what a new device no rule names may bring up: storage hid audio video net bluetooth hub other [deny]\n"
"  device forget ID|PORT             drop the rule of one device\n"
"  device list                       the devices the system sees, each allowed, blocked or waiting\n"
"  device log [N]                    the last decisions, the newest last [20]\n"
"\n"
"\n"
"Power\n"
"  battery [NAME|auto|none]          which battery the Battery row reads: auto is the first present one, none is no battery [auto]\n"
"  charge [limit N|limit off]        where the battery stops charging, 20 to 100\n"
"  cpu [boost|smt on|off]            turbo and SMT, kept across boots\n"
"  cpu online|offline N              hotplug one processor\n"
"  power [performance|balanced|powersave]\n"
"                                    profile and CPU governor, kept across boots\n"
"\n"
"\n"
"Boot\n"
"  bind                              the events of the machine, and what each runs\n"
"  bind EVENT [COMMAND]              one event; empty puts the default back\n"
"  bind exit [add|remove ...]        what runs when the machine stops\n"
"  bind init [add|remove ...]        what runs at boot\n"
"  bind init canvas [on|off]         start Canvas at boot [on]\n"
"  bind init mount [on|off]          mount kept disks at boot [on]\n"
"\n"
"\n"
"All commands\n"
"  bios [reboot]                     whether firmware setup is on offer; reboot goes\n"
"  clipboard [primary]               what the clipboard holds and who copied it, never the text (root)\n"
"  clipboard clear                   empty both selections and the history (root)\n"
"  clipboard get [primary]           the copied text: exact to a file or a pipe, escaped to a terminal (root)\n"
"  clipboard set [primary] [TEXT]    copy TEXT, or standard input (root)\n"
"  clipboard watch [primary]         a line each time something is copied, the text escaped (root)\n"
"  debug                             what the desktop is doing and why (root): the card, the screens, the windows and their scroll bodies, the pointer, the input devices and the wheel\n"
"  desktop [on|off]                  the desktop, now and at every start; off leaves the text console [on]\n"
"  desktop start|rest                begin the chosen desktop now; rest is the text console until the next start, the choice kept as it is\n"
"  help [VERB]                       these commands, or the page of one\n"
"  latency [reset]                   what the input handlers cost a report, and who has the display\n"
"  notices [clear]                   the errors and warnings of the kernel the desktop has (root): clear dismisses them\n"
"  security [trust|block|SWITCH ...]\n"
"                                    keys TLS trusts and refuses besides the built-in ones, and the switches of TLS and bowls\n"
"  sensors                           the processor use, clocks, GPU load, heat, fans and power of this machine, read now\n"
"  settings [gallery]                the settings window, on the desktop of the compositor; gallery, every control of the UI kit\n"
"  setup                             where it runs: install, update, use, live\n"
"  sleep | hibernate                 suspend to RAM or to disk\n"
"  status                            this session as one page\n"
"  window                            the windows on the desktop: name, pid, place, size, title\n"
"  window move PID|NAME PLACE...     put the windows of a program, or of a name, where PLACE says\n"
"  window PLACE... COMMAND...        run COMMAND, its first window put where PLACE says:\n"
"                                    center, at X,Y, maximized, fullscreen, screen N,\n"
"                                    size WxH, name NAME (what it is remembered by)\n"
"  wipe [yes]                        forget /home and /root, keep the machine; yes\n"
"                                    is for a script, which cannot be asked\n"
"\n"
"    Settings stay in the image this session started from.\n"
"    install takes the settings of this session; update keeps those of the disk.\n"
"    The rules are /root/rules/NAME.rover over the image's (moonwater rover).\n"
                    ;
                string_address more[SETTINGS_WORDS];
                int status = -1, wipe = -1, servers = -1, sleep = -1, hibernate = -1, setup = -1, desktop = -1, link = -1, any = SETTINGS_ANY;
                positive rows;

                settings.moonwater = (string_address) "/moonwater";
                settings_help(help, sizeof(help) - 1);
                rows = settings.generic_count;
                for (positive at = 0; at < rows; at++)
                {
                        const char address_to command = settings_large->pool + settings.generic[at].command;
                        int row = (int)at + SETTINGS_ANY + 1;

                        status = string_equals((string_address)command, (string_address) "status") ? row : status;
                        wipe = string_equals((string_address)command, (string_address) "wipe [yes]") ? row : wipe;
                        servers = string_equals((string_address)command, (string_address) "ntp nts servers [NAME[:PORT]...|default]") ? row : servers;
                        sleep = string_equals((string_address)command, (string_address) "sleep") ? row : sleep;
                        hibernate = string_equals((string_address)command, (string_address) "hibernate") ? row : hibernate;
                        setup = string_equals((string_address)command, (string_address) "setup") ? row : setup;
                        desktop = string_equals((string_address)command, (string_address) "desktop [on|off]") ? row : desktop;
                        link = string_equals((string_address)command, (string_address) "link [pair|NAME]") ? row : link;
                }
                check("settings: help's main page is a row to each command, and its notes are none",
                      rows > 40 && rows <= SETTINGS_GENERATED && !settings.truncated && status > 0 && wipe > 0 && setup > 0 && desktop > 0 && link > 0);
                check("settings: a command with nothing after its verb takes nothing, one with brackets takes words",
                      !settings_takes(status) && !settings_takes(setup) && settings_takes(wipe) && settings_takes(desktop) && settings_takes(link) &&
                          settings_takes(any));
                check("settings: the verb is the leading words in small letters",
                      settings.generic[servers - any - 1].verb == sizeof("ntp nts servers") - 1 &&
                          settings.generic[desktop - any - 1].verb == sizeof("desktop") - 1 &&
                          settings.generic[status - any - 1].verb == sizeof("status") - 1);
                check("settings: sleep | hibernate is two rows with one text",
                      sleep > 0 && hibernate == sleep + 1 &&
                          string_equals((string_address)settings_text(sleep), (string_address)settings_text(hibernate)) &&
                          string_equals((string_address)settings_text(sleep), (string_address) "suspend to RAM or to disk"));
                check("settings: a line indented far under a row is more of its text",
                      string_equals((string_address)settings_text(wipe),
                                    (string_address) "forget /home and /root, keep the machine; yes is for a script, which cannot be asked"));
                check("settings: a command too long to leave two blanks has its text on the next line",
                      servers > 0 && string_equals((string_address)settings_text(servers), (string_address) "who is asked for it, up to four"));

                // More commands than the table holds: the first rows, and it says so.
                {
                        static char many[SETTINGS_GENERATED * 48 + 64];
                        positive length = 0;

                        for (positive at = 0; at < SETTINGS_GENERATED + 8; at++)
                        {
                                many[length++] = ' ';
                                many[length++] = ' ';
                                memory_copy(many + length, "verb ", 5);
                                length += 5;
                                length += positive_into_string((p8 address_to)many + length, at);
                                memory_copy(many + length, "                    does it\n", 28);
                                length += 28;
                        }
                        settings_help(many, length);
                        check("settings: more commands than rows leaves the first ones and says there were more",
                              settings.generic_count == SETTINGS_GENERATED && settings.truncated);
                }
                settings_help("Moonwater\n\nnothing\n", 19);
                check("settings: a page with no rows is no rows", settings.generic_count == 0 && !settings.truncated);
                settings_help(help, sizeof(help) - 1);

                // Words: blanks part them, quotes hold blanks, nothing else is read.
                count = settings_generic_words(any, "bind init add \"echo hi there\" ; rm -rf /", more, SETTINGS_WORDS);
                check("settings: a typed command is words, a pair of quotes holds one, and a semicolon is a word",
                      count == 9 && string_equals(more[0], (string_address) "/moonwater") && string_equals(more[1], (string_address) "bind") &&
                          string_equals(more[4], (string_address) "echo hi there") && string_equals(more[5], (string_address) ";") && !more[9]);
                count = settings_generic_words(desktop, "off", more, SETTINGS_WORDS);
                check("settings: a row's verb comes before what is typed", count == 3 && string_equals(more[1], (string_address) "desktop") &&
                                                                              string_equals(more[2], (string_address) "off"));
                count = settings_generic_words(any, "a b c d e f g h i j k l m n o p q r s t u v w x y z aa bb cc dd ee ff gg hh ii jj kk ll mm nn", more, SETTINGS_WORDS);
                check("settings: more words than a command takes are none", count == 0);
                count = settings_generic_words(any, "name 'a \"b' \"it's\"", more, SETTINGS_WORDS);
                check("settings: the other quote is a letter inside a pair", count == 4 && string_equals(more[2], (string_address) "a \"b") &&
                                                                              string_equals(more[3], (string_address) "it's"));

                // What asks twice, by the words that would run and not by the row.
                count = settings_generic_words(desktop, "off", more, SETTINGS_WORDS);
                check("settings: desktop off asks twice and desktop on does not",
                      settings_asks_for(more + 1, count - 1) &&
                          (settings_generic_words(desktop, "on", more, SETTINGS_WORDS), !settings_asks_for(more + 1, 2)));
                settings_generic_words(any, "setup install /dev/sda yes", more, SETTINGS_WORDS);
                check("settings: setup install asks twice, typed in any row", settings_asks_for(more + 1, 4));
                settings_generic_words(any, "setup update usb yes", more, SETTINGS_WORDS);
                check("settings: and an update from a stick, which writes the build the machine starts",
                      settings_asks_for(more + 1, 4));
                settings_generic_words(any, "setup rollback yes", more, SETTINGS_WORDS);
                check("settings: and a rollback, which starts the build it had again", settings_asks_for(more + 1, 3));
                settings_generic_words(any, "wipe", more, SETTINGS_WORDS);
                check("settings: and wipe, bare", settings_asks_for(more + 1, 1));
                settings_generic_words(any, "link group leave home", more, SETTINGS_WORDS);
                check("settings: and link group leave", settings_asks_for(more + 1, 4));
                settings_generic_words(any, "link group home", more, SETTINGS_WORDS);
                check("settings: but not link group", !settings_asks_for(more + 1, 3));
                settings_generic_words(any, "sleep", more, SETTINGS_WORDS);
                check("settings: sleep asks, status does not",
                      settings_asks_for(more + 1, 1) && (settings_generic_words(any, "status", more, SETTINGS_WORDS), !settings_asks_for(more + 1, 1)));
                check("settings: a row whose verb alone ends what a person has says so, one that needs a word typed does not",
                      settings_row_asks(wipe) && settings_row_asks(sleep) && !settings_row_asks(status) && !settings_row_asks(desktop));

                // The second Enter: the first says so and runs nothing, a key between asks again, the same Enter after it runs.
                // The command's row is in view, as it is for whoever types in it (settings_show).
                settings.runner.child = 1;
                settings.queued = 0;
                settings.job = clock;
                settings.job_sets = false;
                ui_field_close(&settings.edit);
                ui_confirm_drop(&settings.confirm);
                settings_show(wipe);
                settings_activate(wipe, 1);
                check("settings: Enter on a command with words to type opens its field", settings_editing() == wipe && settings_holds(""));
                settings_type_code('y');
                settings_type_code('e');
                settings_type_code('s');
                settings_press(UI_KEY_ENTER);
                check("settings: a command that ends what a person has is not run by the first Enter, which says what would run",
                      settings.queued == 0 && settings_editing() == wipe &&
                          string_equals((string_address)settings.status, (string_address) "press again to confirm: moonwater wipe yes"));
                settings_type_code('x');
                settings_press(UI_KEY_BACKSPACE);
                settings_press(UI_KEY_ENTER);
                check("settings: a key between the two Enters asks again", settings.queued == 0 && settings_editing() == wipe);
                settings_press(UI_KEY_ENTER);
                check("settings: and the second Enter runs it, words and all",
                      settings.queued == 1 && settings.queue[0].row == wipe && string_equals((string_address)settings.queue[0].value, (string_address) "yes") &&
                          settings_editing() < 0);
                settings.queued = 0;
                settings_activate(sleep, 1);
                check("settings: a command with no words to type asks on its own Enter", settings.queued == 0 && settings_editing() < 0 &&
                                                                                          settings.confirm.armed && settings.confirm.what == sleep + 1);
                settings_activate(sleep, 1);
                check("settings: and runs on the second", settings.queued == 1 && settings.queue[0].row == sleep);
                settings.queued = 0;
                ui_confirm_drop(&settings.confirm);
                settings_activate(status, 1);
                check("settings: one that asks nothing is run by its Enter", settings.queued == 1 && settings.queue[0].row == status);
                settings.queued = 0;

                // The any row: the verb is in what is typed, and nothing typed is no command.
                settings_show(any);
                settings_activate(any, 1);
                check("settings: the any row opens a field", settings_editing() == any);
                settings_press(UI_KEY_ENTER);
                check("settings: and nothing typed in it runs nothing and says so", settings.queued == 0 && settings_editing() == any &&
                          string_equals((string_address)settings.status, (string_address) "type a command: its verb, then what follows it"));
                for (const char address_to letter = "name lane-any"; *letter; letter++)
                        settings_type_code((unsigned int)*letter);
                settings_press(UI_KEY_ENTER);
                check("settings: a command typed in it runs", settings.queued == 1 && settings.queue[0].row == any &&
                          string_equals((string_address)settings.queue[0].value, (string_address) "name lane-any") && settings_editing() < 0);
                settings.queued = 0;
                // A command is run by Enter alone: a key that comes once its row has scrolled out of view ends the typing as a press
                // outside its field does, and what was typed is not run by it (a plain value's field is set by either).
                {
                        b32 hidden;

                        settings_show(any);
                        settings_activate(any, 1);
                        for (const char address_to letter = "name lane-out"; *letter; letter++)
                                settings_type_code((unsigned int)*letter);
                        ui_scroll_set(&settings.list.scroll, ui_scroll_most(&settings.list.scroll));
                        hidden = settings_editing() == any && settings_holds("name lane-out") && !settings_edit_shown();
                        settings_press(UI_KEY_DOWN);
                        check("settings: a command typed in a row that scrolled out of view is not run by the next key, which ends its typing",
                              hidden && settings.queued == 0 && settings_editing() < 0);
                }
                settings.queued = 0;
                settings.runner.child = 0;

                // The filter: letters typed with no field open, Backspace, and Escape clearing before it closes.
                settings.ui.scale = 100;
                ui_field_close(&settings.edit);
                settings_filter_clear();
                settings.page.chosen = 0;
                settings.page.pane = 1;
                settings_refilter();
                {
                        b32 here = settings.list.count != 0;

                        for (positive at = 0; at < settings.list.count; at++)
                                here &= settings_group_of(settings.list.id[at]) == 0;
                        check("settings: with nothing typed the rows are those of the group chosen, and not every row",
                              here && settings.list.count < settings_rows_count() &&
                                  string_equals((string_address)settings_group_name(0), (string_address) "System") &&
                                  string_equals((string_address)settings_group_name(settings_groups() - 1), (string_address) SETTINGS_ALL_HEADER));
                }
                settings_type_code('W');
                settings_type_code('i');
                settings_type_code('f');
                settings_type_code('i');
                check("settings: letters typed with no field open are the filter, in small letters",
                      check_filter_says("wifi") && settings.list.count < settings_rows_count());
                {
                        b32 structured = false, generic = false, each = true;

                        for (positive at = 0; at < settings.list.count; at++)
                        {
                                structured |= settings.list.id[at] == settings_row_named("Wi-Fi");
                                generic |= settings.list.id[at] > SETTINGS_ANY;
                                each &= settings_matches(settings.list.id[at]);
                        }
                        check("settings: the rows that show are those that mention it, the switch and the commands both", structured && generic && each);
                }
                check("settings: the focus goes to the first row that shows", settings.list.focus == settings.list.id[0]);
                settings_press(UI_KEY_BACKSPACE);
                check("settings: Backspace takes a letter off it", check_filter_says("wif"));
                check("settings: Escape clears it and keeps the window", settings_press(UI_KEY_ESCAPE) && !settings_filter_length() &&
                                                                              settings.list.count < settings_rows_count() &&
                                                                              settings_group_of(settings.list.id[0]) == settings.page.chosen);
                check("settings: and Escape with none typed closes it", !settings_press(UI_KEY_ESCAPE));
                for (const char address_to letter = "zzzzqq"; *letter; letter++)
                        settings_type_code((unsigned int)*letter);
                check("settings: a filter nothing matches shows no row", settings.list.count == 0);
                settings_press(UI_KEY_ESCAPE);
                settings_type_code(' ');
                check("settings: a blank is no letter of it", !settings_filter_length());

                // The groups at the left, the rows at the right, and the keys between them.
                settings.ui.scale = 100;
                settings.width = SETTINGS_WIDTH;
                settings.height = SETTINGS_HEIGHT;
                settings.page.chosen = 0;
                settings.page.pane = 1;
                ui_field_close(&settings.edit);
                settings.runner.child = 1;
                settings.queued = 0;
                settings_refilter();
                {
                        int canvas_group = 1, all = settings_groups() - 1, scale = settings_row_named("Background");
                        int first = settings.list.id[0];

                        check("settings: the first group is the first section, and its first row has the keys",
                              settings.page.chosen == 0 && first == SETTINGS_NAME_ROW);
                        settings_press(UI_KEY_TAB);
                        check("settings: Tab goes to the column of groups", settings.page.pane == 0 && settings.page.chosen == 0);
                        settings_press(UI_KEY_DOWN);
                        check("settings: Down in the column chooses the next group, and its rows are at the right at once",
                              settings_group_of(settings.list.id[0]) == settings.page.chosen && settings.page.chosen == 1 &&
                                  settings.list.focus == settings.list.id[0] && settings.page.pane == 0);
                        settings_press(UI_KEY_END);
                        check("settings: End in the column chooses All commands, whose rows are the commands",
                              settings.page.chosen == all && settings.list.id[0] == SETTINGS_ANY);
                        settings_press(UI_KEY_DOWN);
                        check("settings: and Down at the last group stays", settings.page.chosen == all);
                        settings_press(UI_KEY_HOME);
                        settings_press(UI_KEY_UP);
                        check("settings: Home and Up at the first group stay at it", settings.page.chosen == 0);
                        settings_press(UI_KEY_PAGE_DOWN);
                        check("settings: Page Down moves three groups", settings.page.chosen == 3);
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter in the column goes to the rows, on the first", settings.page.pane == 1 && settings.list.focus == settings.list.id[0] &&
                                  settings_group_of(settings.list.focus) == 3);
                        settings_press(UI_KEY_DOWN);
                        check("settings: Down in the rows moves among the rows of the group only",
                              settings.page.chosen == 3 && settings.list.focus == settings.list.id[1]);
                        settings.list.focus = settings_row_named("Name servers (auto for the network)");
                        settings_press(UI_KEY_LEFT);
                        check("settings: Left on a row with no Left of its own goes back to the column", settings.page.pane == 0 && settings.page.chosen == 3);
                        settings_press(UI_KEY_RIGHT);
                        check("settings: and Right in the column goes to the rows again", settings.page.pane == 1);
                        settings_press(UI_KEY_TAB);
                        settings_press(UI_KEY_TAB);
                        check("settings: Tab twice is a way back to where it was", settings.page.pane == 1 && settings.page.chosen == 3);
                        settings_press(UI_KEY_TAB);
                        settings_type_code('a');
                        check("settings: a letter typed in the column is the filter, and the keys are in the rows",
                              settings.page.pane == 1 && settings_filter_length() == 1 && settings.list.count > settings_rows_count() / 4);
                        settings_press(UI_KEY_TAB);
                        settings_press(UI_KEY_UP);
                        check("settings: moving in the column clears the filter and shows the group chosen",
                              !settings_filter_length() && settings.page.chosen == 2 && settings_group_of(settings.list.id[0]) == 2);
                        settings_press(UI_KEY_ENTER);
                        {
                                int clock_row = settings_row_named("Set the clock from the network");

                                settings.list.focus = clock_row;
                                settings.state[clock_row] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[clock_row], (string_address) "on", SETTINGS_VALUE);
                                settings_press(UI_KEY_LEFT);
                                check("settings: Left on a switch goes back to the column and throws nothing",
                                      settings.page.pane == 0 && settings.queued == 0);
                        }
                        settings.queued = 0;
                        settings.runner.child = 0;
                        settings.page.pane = 1;
                        settings_group_choose(0);
                        settings.ui.scale = 100;
                        {
                                const struct ui_page address_to page = &settings.page;
                                int side = page->column_w, pad = ui_px(&settings.ui, UI_PAD), row = ui_px(&settings.ui, UI_ROW);
                                int where = ui_page_column_hit(&settings.ui, page, pad + 5, page->top + 5),
                                    second = ui_page_column_hit(&settings.ui, page, pad + 5, page->top + row + 5);

                                check("settings: a point on a group names it, and one past the column's rows (the middle of its track) or past the groups none",
                                      where == 0 && second == 1 && ui_page_column_hit(&settings.ui, page, pad + side + 2, page->top + 5) == 0 &&
                                          ui_page_column_hit(&settings.ui, page, page->seam, page->top + 5) == -1 &&
                                          ui_page_column_hit(&settings.ui, page, pad + 5, page->top - 1) == -1 &&
                                          ui_page_column_hit(&settings.ui, page, pad + 5, page->top + settings_groups() * row) == -1);
                                check("settings: the column is a third of the window at most and the rows start after it",
                                      side <= ui_px(&settings.ui, UI_COLUMN_MOST) && page->left > pad + side &&
                                          ui_list_hit(&settings.ui, &settings.list, page->left + 4, page->top + ui_px(&settings.ui, UI_SECTION) + 4) == settings.list.id[0] &&
                                          ui_list_hit(&settings.ui, &settings.list, pad + 4, page->top + ui_px(&settings.ui, UI_SECTION) + 4) == -1);
                        }
                        // A narrow window keeps the column inside half of it, and the rows a place to the right of it.
                        for (int width = WINDOW_MIN_WIDTH; width <= 400; width += 38)
                        {
                                settings.width = width;
                                settings_layout();
                                check("settings: a narrow window has its column within half and its rows right of it",
                                      settings.page.column_w <= width / 2 && settings.page.left >= settings.page.column_w &&
                                          settings.page.right == width - ui_px(&settings.ui, UI_PAD));
                        }
                        settings.width = SETTINGS_WIDTH;
                        settings_layout();
                }
                settings.runner.child = 0;
        }

        // The pieces, drawn.
        {
                static unsigned int pixels[320 * 80], scratch[400 * UI_SCRATCH_ROWS];
                static font_face faces[2];
                static p8 atlas[512 * 512];
                positive size = 0, bytes = compositor_map_file(regular, &size), bold_size = 0,
                         bold_bytes = bold ? compositor_map_file(bold, &bold_size) : 0;
                long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
                struct ui ui = {pixels, 320, 320, 80, 0, 80, 100};
                b32 drawn = true;

                if (!bytes || (engine < 0 && engine > -4096) || font_load(&faces[0], (const p8 address_to)bytes, size, 0) != 0)
                {
                        check("ui: the face loads", false);
                        return;
                }
                ui.engine = (font_engine address_to)engine;
                font_start(ui.engine, atlas, 512, 512);
                ui.regular = &faces[0];
                ui.bold = bold_bytes && font_load(&faces[1], (const p8 address_to)bold_bytes, bold_size, 0) == 0 ? &faces[1] : null;
                ui.scratch = scratch;
                ui.scratch_width = 400;
                ui_inks(&ui, 0x101012u);

                // The name field, typed in as the verbs take a name, and the timezone field, which takes any text.
                settings_hold(name, "");
                settings.status[0] = 0;
                settings_press(UI_KEY_ENTER);
                check("settings: Enter on an empty name says so and keeps the old one",
                      settings_editing() < 0 && string_equals((string_address)settings.status, (string_address) "nothing typed: the value is kept"));
                settings_hold(name, "");
                settings_type_code(0xe9);
                check("settings: a name takes letters, digits and hyphens only, and says so",
                      settings_holds("") && string_equals((string_address)settings.status, (string_address) "a name is letters, digits and hyphens"));
                for (int letter = 0; letter < 31; letter++)
                        settings_type_code('a');
                check("settings: a name typed to 31 letters is kept", settings_holds("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
                settings_type_code('a');
                check("settings: and a 32nd is refused with the limit, not kept",
                      settings_holds("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") &&
                          string_equals((string_address)settings.status, (string_address) "a name is under 32 characters"));
                // The name as a field of the kit's: the caret moves in it, a selection is typed over, Delete and
                // Control-Backspace take what they reach, and a blank is refused as a name refuses it.
                settings_hold(name, "patient-cricket");
                settings_press(FIELD_KEY_HOME);
                settings_chord(FIELD_KEY_RIGHT, 0, WINDOW_KEY_CONTROL);
                check("settings: the name field opens with the name and the caret moves by words in it",
                      settings_holds("patient-cricket") && settings_caret() == 7);
                settings_chord(FIELD_KEY_END, 0, WINDOW_KEY_SHIFT);
                settings_type_code('-');
                settings_type_code('m');
                settings_type_code('o');
                settings_type_code('t');
                settings_type_code('h');
                check("settings: Shift and End select to the end, and typing replaces it", settings_holds("patient-moth") && settings_caret() == 12);
                settings_press(FIELD_KEY_LEFT);
                settings_press(FIELD_KEY_LEFT);
                settings_press(FIELD_KEY_DELETE);
                check("settings: Left moves the caret back, and Delete takes the character after it", settings_holds("patient-moh") && settings_caret() == 10);
                settings.status[0] = 0;
                settings_type_code(' ');
                check("settings: a blank is refused in a name, and said", settings_holds("patient-moh") &&
                          string_equals((string_address)settings.status, (string_address) "a name is letters, digits and hyphens"));
                settings_chord(FIELD_KEY_BACKSPACE, 0, WINDOW_KEY_CONTROL);
                check("settings: Control-Backspace takes the word before the caret", settings_holds("patient-h") && settings_caret() == 8);
                settings_chord(FIELD_KEY_Z, 26, WINDOW_KEY_CONTROL);
                check("settings: Control-Z gives it back", settings_holds("patient-moh"));
                settings_press(FIELD_KEY_ESCAPE);
                check("settings: Escape leaves the field as it was", settings_editing() < 0);
                // The time zone's row is in view from here on, as it is for whoever types in it (settings_show).
                settings_show(zone);
                settings_hold(zone, "");
                settings_type_code(0xe9);
                settings_type_code(0xfc);
                settings_press(UI_KEY_BACKSPACE);
                check("settings: backspace takes a whole character of several bytes", settings_holds("\xc3\xa9"));
                settings_type_code(0xfc);
                settings_press(FIELD_KEY_LEFT);
                settings_type_code('x');
                check("settings: Left steps over a whole character of several bytes", settings_holds("\xc3\xa9x\xc3\xbc"));
                // A field kept as it is, or typed to what it is, sends nothing; a change does.
                settings.runner.child = 1;
                settings.queued = 0;
                settings.state[zone] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[zone], (string_address) "Europe/Stockholm", SETTINGS_VALUE);
                settings_hold(zone, "Europe/Stockholm");
                settings_press(UI_KEY_ENTER);
                check("settings: a field typed to what it is sends nothing", settings.queued == 0 && settings_editing() < 0);
                // Opened on what the value is (nothing to start from), then typed to Berlin: that is what the field holds.
                settings_hold(zone, "");
                for (const char address_to letter = "Europe/Berlin"; *letter; letter++)
                        settings_type_code((unsigned int)*letter);
                settings_press(UI_KEY_ENTER);
                check("settings: and a field typed to another is a change to it", settings.queued == 1 && settings.queue[0].row == zone &&
                          string_equals((string_address)settings.queue[0].value, (string_address) "Europe/Berlin"));
                settings.queued = 0;
                string_copy_bounded((string_address)settings.value[zone], (string_address) "Europe/Berlin", SETTINGS_VALUE);
                settings_hold(zone, "Europe/Berlin");
                settings_type_code(' ');
                settings_press(FIELD_KEY_HOME);
                settings_type_code(' ');
                settings_press(UI_KEY_ENTER);
                check("settings: blanks typed at either end of a value are typed, and are no part of the value it is set to",
                      settings.queued == 0 && settings_editing() < 0);
                settings.runner.child = 0;
                settings.queued = 0;
                settings_hold(zone, "");
                settings_press(FIELD_KEY_ESCAPE);
                ui_field_close(&settings.edit);
                settings.page.dirty = false;

                // A field's text stays in its well: a name longer than the well is shown from its end, not drawn past it. The
                // field is the kit's, bare, as a row's field is: its text is its engine's, the caret at its end.
                ui_clear(&ui);
                {
                        static struct ui_control field;
                        positive outside = 0;

                        memory_zero(&field, sizeof(field));
                        field.kind = UI_INPUT, field.variant = UI_FIELD_BARE, field.focused = 1;
                        field.x = 10, field.y = 10, field.w = 100, field.h = 40;
#ifdef UI_TEXT_REFUSED
                        static struct ui_text text;

                        ui_field_open(&text, null, "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww");
                        ui_text_reveal(&ui, &text);
                        field.edit = &text;
                        ui_control_draw(&ui, &field);
                        ui_text_reveal(&ui, &text);
                        ui_clear(&ui);
#else
                        field.source = "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww";
#endif
                        ui_control_draw(&ui, &field);
                        for (int row = 0; row < 80; row++)
                                for (int column = 0; column < 320; column++)
                                        outside += (column < 10 || column >= 110) && (pixels[row * 320 + column] >> 24) > 64;
                        check("ui: a field's text stays inside its well, a name longer than the well shows its end", outside == 0);
                }

                // The kit's type roles: a list's label and note, a header, a title, regular and bold.
                for (int role_at = 0; role_at < 4; role_at++)
                {
                        static const int roles[4] = {UI_LABEL_MEDIUM, UI_BODY_MEDIUM, UI_TITLE_SMALL, UI_TITLE_LARGE};

                        ui_clear(&ui);
                        ui_kit_text(&ui, 10, 40, (string_address) "Settings", roles[role_at], 0xff000000u | ui.ink, 0);
                        drawn &= settings_lit(pixels, 320 * 80) > (positive)(ui_types[roles[role_at]].size * 6);
                }
                check("ui: a run of text lights its pixels in every role, in either face", drawn);
                check("ui: a role's baseline puts its text in the lower half of its line box, as a row's label is laid",
                      ui_kit_baseline(&ui, 100, UI_BODY_MEDIUM) > 100 + ui_types[UI_BODY_MEDIUM].line / 2 &&
                          ui_kit_baseline(&ui, 100, UI_BODY_MEDIUM) < 100 + ui_types[UI_BODY_MEDIUM].line);

                // A list's switch row is the kit's switch: on, its bars are the accent across the pill; off, the control-off colour.
                ui_clear(&ui);
                {
                        static struct ui_control on, off;

                        memory_zero(&on, sizeof(on));
                        memory_zero(&off, sizeof(off));
                        on.kind = off.kind = UI_SWITCH, on.label = off.label = "";
                        on.on = 1, on.x = 10, off.x = 160, on.y = off.y = 0, on.w = off.w = 100, on.h = off.h = 48;
                        ui_control_draw(&ui, &on);
                        ui_control_draw(&ui, &off);
                        check("ui: a switch on is the accent across its pill, one off the controls' rest colour",
                              (pixels[24 * 320 + 84] | 0xff000000u) == (ui_role(UI_PRIMARY) | 0xff000000u) &&
                                  (pixels[24 * 320 + 234] | 0xff000000u) == (ui_role(UI_CONTROL_OFF) | 0xff000000u));
                        // At work (its change asked and not answered): still on, its fill quiet, and the spinner in the pill's middle.
                        {
                                static struct ui_control busy;
                                unsigned int centre;
                                b32 spun = false;

                                memory_zero(&busy, sizeof(busy));
                                busy.kind = UI_SWITCH, busy.label = "", busy.on = 1, busy.working = 1, busy.x = 10, busy.w = 100, busy.h = 48;
                                ui_clear(&ui);
                                ui_control_draw(&ui, &busy);
                                centre = pixels[24 * 320 + 10 + 100 - UI_SWITCH_W / 2] | 0xff000000u; // the pill's middle, inside the spinner's ring
                                for (int y = 24 - 8; y <= 24 + 8; y++)
                                        for (int x = 10 + 100 - UI_SWITCH_W / 2 - 8; x <= 10 + 100 - UI_SWITCH_W / 2 + 8; x++)
                                                spun |= (pixels[y * 320 + x] | 0xff000000u) != centre;
                                check("ui: a switch at work is on, its fill quieter than the accent, with the spinner in its pill",
                                      centre != (ui_role(UI_PRIMARY) | 0xff000000u) && centre != (ui_role(UI_CONTROL_OFF) | 0xff000000u) && spun);
                        }
                }

                ui_clear(&ui);
                ui_round(&ui, 10, 10, 100, 50, UI_RADIUS, ui_tone(0xffffffu, 255));
                check("ui: a rounded corner is covered in part, its middle whole",
                      pixels[10 * 320 + 10] >> 24 < 64 && pixels[30 * 320 + 60] >> 24 == 255 && pixels[10 * 320 + 60] >> 24 == 255);

                // What a command said, as a card: its lines kept, and cut to the window's width.
                {
                        static const char said[] = "\x1b[1m[Moonwater]\x1b[0m Canvas is on: the kernel desktop, 2 windows\n  HDMI-1: 1920 by 1080, 60 Hz   \n\n\n";
                        static const char wide[] = "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
                                                   "seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four\n";
                        int room;
                        b32 within = true;

                        settings.ui = ui;
                        settings.width = 300;
                        settings.height = 160; // the card runs to the window's bottom, so this keeps its view at the 100 px it had
                        settings.page.card = false;
                        memory_copy(settings_large->said, said, sizeof(said) - 1);
                        settings.runner.length = sizeof(said) - 1;
                        settings_card_fill();
                        check("settings: a card keeps the lines, without the label or the escapes or the blanks that end them",
                              settings.card_text.length == sizeof("Canvas is on: the kernel desktop, 2 windows\n  HDMI-1: 1920 by 1080, 60 Hz") - 1 &&
                                  !memory_compare(settings_large->card, "Canvas is on: the kernel desktop, 2 windows\n  HDMI-1: 1920 by 1080, 60 Hz",
                                                  settings.card_text.length));
                        memory_copy(settings_large->said, wide, sizeof(wide) - 1);
                        settings.runner.length = sizeof(wide) - 1;
                        settings_card_fill();
                        ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                        room = settings.width - 2 * ui_px(&settings.ui, UI_PAD) - 2 * ui_px(&settings.ui, 14);
                        for (int at = 0; at < settings.card_text.count; at++)
                                within &= ui_lines_width(&settings.ui, &settings.card_text, settings_large->card_at[at], settings_large->card_at[at] + settings_large->card_size[at]) <= room;
                        check("settings: a line wider than the window is cut into more, each within it", settings.card_text.count >= 3 && within);
                        {
                                // The same words at another text size are wrapped again: more lines at 200 percent, each within the window.
                                int lines = settings.card_text.count;
                                b32 big = true;

                                ui_text_percent = 200;
                                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                                for (int at = 0; at < settings.card_text.count; at++)
                                        big &= ui_lines_width(&settings.ui, &settings.card_text, settings_large->card_at[at], settings_large->card_at[at] + settings_large->card_size[at]) <= room;
                                check("settings: a card is wrapped again when the text is another size, into more lines at 200 percent, each within the window",
                                      settings.card_text.count > lines && big);
                                ui_text_percent = 0;
                                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                        }
                        {
                                // A word with no blank in it, wider than the window, in wide Cyrillic capitals (a lead byte alone is a narrower mark than the letter): cut between characters.
                                char tall[160];
                                positive length = 0;
                                b32 whole = true;

                                for (int again = 0; again < 24; again++, length += 6)
                                        memory_copy(tall + length, "\xd0\xa8\xd0\x96\xd0\xa9", 6);
                                memory_copy(settings_large->said, tall, length);
                                settings.runner.length = length;
                                settings_card_fill();
                                // At each of 24 widths, one pixel apart: a character a pixel too wide for a line is where a cut inside it was made.
                                for (int extra = 0; extra < 24; extra++)
                                {
                                        settings.width = 300 + extra;
                                        ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                                        for (int at = 0; at < settings.card_text.count; at++)
                                                whole &= ((p8)settings_large->card[settings_large->card_at[at]] & 0xc0) != 0x80;
                                        whole &= settings.card_text.count >= 2;
                                }
                                settings.width = 300;
                                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                                check("settings: a word wider than the window is cut between characters, never inside one, at any width", whole);
                                memory_copy(settings_large->said, wide, sizeof(wide) - 1);
                                settings.runner.length = sizeof(wide) - 1;
                                settings_card_fill();
                                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                        }
                        {
                                // One line of 1400 bytes with blanks in it, no newline: each line the wrap makes is measured whole (the
                                // whole run's own width, not a probe of its first bytes) and must fit the window.
                                static char long_line[1400], run[1400];
                                positive length = 0;
                                b32 within_whole = true;

                                for (; length + 40 < sizeof(long_line); length += 40)
                                        memory_copy(long_line + length, "alpha beta gamma delta epsilon zeta eta theta iota ", 40);
                                memory_copy(settings_large->said, long_line, length);
                                settings.runner.length = length;
                                settings.width = 300;
                                settings_card_fill();
                                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                                room = settings.width - 2 * ui_px(&settings.ui, UI_PAD) - 2 * ui_px(&settings.ui, 14);
                                for (int at = 0; at < settings.card_text.count; at++)
                                {
                                        int size = settings_large->card_size[at];

                                        memory_copy(run, settings_large->card + settings_large->card_at[at], size);
                                        run[size] = 0;
                                        within_whole &= ui_kit_measure(&settings.ui, (string_address)run, UI_BODY_MEDIUM) <= room;
                                }
                                check("settings: a line of more than 512 bytes with no newline is cut into lines each within the window, measured whole",
                                      settings.card_text.count >= 4 && within_whole);
                                settings.width = 300;
                        }
                        settings_card_open("moonwater status");
                        settings_press(UI_KEY_DOWN);
                        check("settings: a card is scrolled by the keys", settings.page.card && settings.card.scroll > 0);
                        check("settings: Escape on it goes back to the list, and keeps the window",
                              settings_press(UI_KEY_ESCAPE) && !settings.page.card);
                        settings.card.scroll = 0;
                }

                // A choice opens a list of all its choices, and the list has every key.
                {
                        int kb = settings_row_named("Keyboard"), shown;
                        struct window_event over = {WINDOW_EVENT_MOTION, 0, 0, 0}, press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                        struct window_event wheel = {WINDOW_EVENT_WHEEL, 0, 120, 0};

                        settings.width = SETTINGS_WIDTH;
                        settings.height = SETTINGS_HEIGHT;
                        settings.page.card = false;
                        ui_field_close(&settings.edit);
                        settings_filter_clear();
                        settings.page.chosen = settings_group_of(kb);
                        settings.page.pane = 1;
                        settings.runner.child = 1;
                        settings.queued = 0;
                        settings_refilter();
                        settings.list.focus = kb;
                        settings.state[kb] = SETTINGS_READ;
                        string_copy_bounded((string_address)settings.value[kb], (string_address) "uk", SETTINGS_VALUE);
                        // The list is a popup, a window of its own on a row the compositor gives it, and one that takes no row does
                        // not open: the check stands in for the compositor (popup_fake), with a screen as tall as the settings window.
                        popup_fake.screen = SETTINGS_HEIGHT, popup_fake.themed = ui_kit.themed;

                        settings_press(UI_KEY_LEFT);
                        check("settings: Left on a closed choice goes back to the groups and does not move it",
                              settings.page.pane == 0 && settings.queued == 0 && settings.page.menu_row < 0);
                        settings.page.pane = 1;
                        settings_press(UI_KEY_RIGHT);
                        check("settings: and Right on it does nothing", settings.page.pane == 1 && settings.queued == 0 && settings.page.menu_row < 0);

                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter on a choice opens a list of every choice, the one it has now the one the keys are on",
                              settings.page.menu_row == kb && settings.popup.menu.count == 13 && settings.popup.menu.hot == 1 && ui_popmenu_top(&settings.popup.menu) == 0 &&
                                  settings.popup.menu.width >= ui_px(&settings.ui, UI_MENU_LEAST) && settings.popup.menu.height > 0);
                        settings_menu_press(UI_KEY_DOWN);
                        check("settings: Down in the list goes to the next choice", settings.popup.menu.hot == 2);
                        settings_menu_press(UI_KEY_END);
                        check("settings: End goes to the last choice", settings.popup.menu.hot == 12);
                        settings_menu_press(UI_KEY_DOWN);
                        check("settings: Down past the last goes round to the first, as every menu's keys do", settings.popup.menu.hot == 0);
                        settings_menu_press(UI_KEY_UP);
                        check("settings: Up from the first goes round to the last", settings.popup.menu.hot == 12);
                        settings_menu_press(UI_KEY_HOME);
                        check("settings: Home goes to the first, and Up past it wraps", settings.popup.menu.hot == 0);
                        settings_menu_press(UI_KEY_PAGE_DOWN);
                        check("settings: Page Down goes a list's height on, to the last when it all shows", settings.popup.menu.hot == 12);
                        settings_menu_press(UI_KEY_PAGE_UP);
                        check("settings: and Page Up back to the first", settings.popup.menu.hot == 0);
                        settings_menu_type('d');
                        check("settings: a letter goes to the first choice that starts with it (de)", settings.popup.menu.hot == 2);
                        settings_menu_type('e');
                        check("settings: letters typed in a moment go on together (de), and a letter nothing starts with leaves the keys", settings.popup.menu.hot == 2);
                        settings_menu_type('q');
                        check("settings: a letter nothing starts with leaves the keys where they are", settings.popup.menu.hot == 2);
                        settings_press(UI_KEY_TAB);
                        settings_menu_press(UI_KEY_TAB);
                        check("settings: the list takes every key: no group is chosen, no row moved, nothing is typed as a filter",
                              settings.page.menu_row == kb && settings.page.pane == 1 && settings.page.chosen == settings_group_of(kb) && !settings_filter_length() &&
                                  settings.queued == 0);
                        check("settings: Escape closes it and leaves the row as it was, and keeps the window",
                              settings_menu_press(UI_KEY_ESCAPE) && settings.page.menu_row < 0 && settings.queued == 0 &&
                                  string_equals((string_address)settings.value[kb], (string_address) "uk"));

                        settings_press(UI_KEY_ENTER);
                        settings_menu_type('i');
                        settings_menu_press(UI_KEY_ENTER);
                        check("settings: Enter on a choice sets it, as the verb is told, and closes the list",
                              settings.page.menu_row < 0 && settings.queued == 1 && settings.queue[0].row == kb &&
                                  settings.queue[0].value[0] == 'i');
                        settings.queued = 0;
                        settings_press(UI_KEY_SPACE);
                        settings_menu_press(UI_KEY_SPACE);
                        check("settings: Space is Enter in the list, and the choice a row has already sends nothing",
                              settings.page.menu_row < 0 && settings.queued == 0);

                        // The mouse: a press and its release on a choice set it (the release is where it went down), a press
                        // released elsewhere sets nothing, and a press outside the menu asks it to go (the compositor's).
                        settings_press(UI_KEY_ENTER);
                        {
                                struct window_event press = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, 0x110, 0}, lift = {WINDOW_EVENT_BUTTON, 0, 0x110, 0},
                                      move = {WINDOW_EVENT_MOTION, 0, 0, 0};
                                int row = popmenu_row(&settings.popup.menu), pad = ui_px(&settings.ui, UI_MENU_PAD);

                                settings_menu_point(&move, settings.popup.menu.width / 2, pad + 3 * row + row / 2 + 4);
                                check("settings: the pointer over a choice puts the keys on it", settings.popup.menu.hot == 3);
                                settings_menu_point(&press, settings.popup.menu.width / 2, pad + 3 * row + row / 2);
                                check("settings: a press on a choice sets nothing yet", settings.page.menu_row == kb && settings.queued == 0);
                                settings_menu_point(&lift, settings.popup.menu.width / 2, pad + 5 * row + row / 2);
                                check("settings: a press released on another choice sets nothing", settings.page.menu_row == kb && settings.queued == 0);
                                settings_menu_point(&press, settings.popup.menu.width / 2, pad + 3 * row + row / 2);
                                settings_menu_point(&lift, settings.popup.menu.width / 2, pad + 3 * row + row / 2);
                                check("settings: a press and its release on a choice sets it and closes the list", settings.page.menu_row < 0 && settings.queued == 1 &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "se"));
                        }
                        settings.queued = 0;
                        settings_press(UI_KEY_ENTER);
                        settings_menu_gone();
                        check("settings: a press outside the list asks it to go: no change, no group chosen, nothing activated",
                              settings.page.menu_row < 0 && settings.queued == 0 && settings.page.chosen == settings_group_of(kb) && settings.page.pane == 1 &&
                                  settings_editing() < 0);
                        // A mouse's press on a row that the compositor takes back (a release with WINDOW_KEY_CANCEL) is no click: the
                        // row's list does not open. Let go unmoved, the same press opens it.
                        {
                                struct window_event over = {WINDOW_EVENT_MOTION, 0, 0, 0};
                                struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                                struct window_event taken = {WINDOW_EVENT_BUTTON, WINDOW_KEY_CANCEL, UI_POINTER_LEFT, 0};
                                struct window_event lift = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
                                int pos = settings_pos(kb);

                                over.a = (settings.page.left + settings.page.right) / 2;
                                over.b = ui_list_row_y(&settings.list, pos) + ui_px(&settings.ui, UI_ROW) / 2;
                                settings_event_at(&over);
                                settings_event_at(&land);
                                settings_event_at(&taken);
                                check("settings: a mouse's press the compositor takes back opens nothing, and is no click",
                                      settings.page.menu_row < 0 && settings.queued == 0 && !settings.page.press.down);
                                settings_event_at(&land);
                                settings_event_at(&lift);
                                check("settings: a mouse's press and lift on a choice row, unmoved, opens its list", settings.page.menu_row == kb);
                                settings_menu_gone();
                        }

#ifdef UI_TARGET
                        /*
                                A finger: a choice's menu opened by a finger has choices a finger's height, and a release on a
                                choice sets it.
                        */
                        {
                                struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
                                struct window_event lift = {WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0};
                                struct window_event finger = {WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, 0, 0};
                                int pos = settings_pos(kb), row;

                                finger.a = (settings.page.left + settings.page.right) / 2;
                                finger.b = ui_list_row_y(&settings.list, pos) + ui_px(&settings.ui, UI_ROW) / 2;
                                settings_event_at(&finger);
                                settings_event_at(&land);
                                check("settings: a finger on a choice row shows it pressed at once and opens nothing yet",
                                      settings.page.menu_row < 0 && ui_pressed(&settings.page.press, kb));
                                settings_event_at(&lift);
                                check("settings: lifted there, it opens the list, its choices a finger's 44 pixels high",
                                      settings.page.menu_row == kb && settings.popup.menu.touch && !ui_pressed(&settings.page.press, kb) &&
                                          settings.popup.menu.height == (13 * ui_px(&settings.ui, UI_MENU_TOUCH_ROW) + 2 * ui_px(&settings.ui, UI_MENU_PAD) > (int)popup_fake.screen
                                                                         ? (int)popup_fake.screen
                                                                         : 13 * ui_px(&settings.ui, UI_MENU_TOUCH_ROW) + 2 * ui_px(&settings.ui, UI_MENU_PAD)));
                                row = popmenu_row(&settings.popup.menu);
                                settings_menu_point(&(struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, 0x110, 0},
                                                    settings.popup.menu.width / 2, ui_px(&settings.ui, UI_MENU_PAD) + 3 * row + row / 2);
                                settings_menu_point(&(struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, 0x110, 0},
                                                    settings.popup.menu.width / 2, ui_px(&settings.ui, UI_MENU_PAD) + 3 * row + row / 2);
                                check("settings: a finger on a choice sets it as it lifts, not as it lands",
                                      settings.page.menu_row < 0 && settings.queued == 1 &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "se"));
                                settings.queued = 0;
                                // What a press lands on shows pressed for a mouse and a finger alike (settings_press_show): a row by its
                                // number, and a press on no row shows none.
                                ui_page_press_show(&settings.ui, &settings.page, &settings.list,3, 0, 0);
                                check("settings: a press that lands on a row shows that row pressed, and one on nothing shows none",
                                      settings.list.down == 3 && (ui_page_press_show(&settings.ui, &settings.page, &settings.list,-1, 0, 0), settings.list.down == 3));
                                settings.list.down = -1;

                                settings.height = 220;
                                settings_refilter();
                                settings_layout();
                                ui_scroll_set(settings.page.body, 0);
                                finger.a = (settings.page.left + settings.page.right) / 2;
                                finger.b = settings.page.top + ui_px(&settings.ui, 60);
                                settings_event_real(&finger);
                                settings_event_real(&land);
                                for (int step = 1; step <= 6; step++)
                                {
                                        finger.b -= ui_px(&settings.ui, 10); // short of the end, so the lift has room to throw
                                        settings_event_real(&finger);
                                }
                                check("settings: a finger dragged up the rows moves them its way, and opens nothing",
                                      settings.page.menu_row < 0 && settings.queued == 0 && settings_editing() < 0);
                                settings_event_real(&lift);
                                check("settings: let go while moving, the rows are thrown, and a press stops them",
                                      scroll_gliding(&settings.list.scroll) && (settings_event_real(&land), !scroll_gliding(&settings.list.scroll)));
                                lift.flags |= WINDOW_KEY_CANCEL;
                                settings_event_at(&lift);
                                lift.flags &= ~WINDOW_KEY_CANCEL;
                                check("settings: a finger's press the compositor took back does nothing", settings.queued == 0 && settings.page.menu_row < 0);
                                // The rows thrown again, and the finger put down where a row is passing: it stops them and presses none, so
                                // no row shows pressed, and its lift there is no click (the keys stay on the row they were on, nothing opens).
                                {
                                        int over = -1, focus;

                                        ui_scroll_set(settings.page.body, 0);
                                        finger.b = settings.page.top + ui_px(&settings.ui, 60);
                                        settings_event_real(&finger);
                                        settings_event_real(&land);
                                        for (int step = 1; step <= 6; step++)
                                        {
                                                finger.b -= ui_px(&settings.ui, 10);
                                                settings_event_real(&finger);
                                        }
                                        settings_event_real(&lift);
                                        // A place with rows under it for the next row's height of the throw, so that a row is under the finger
                                        // when it lands two events on, however far the rows have gone by then.
                                        for (int y = settings.list.y; y + ui_px(&settings.ui, UI_ROW) < settings.list.y + settings.list.h && over < 0; y++)
                                        {
                                                b32 rows = true;

                                                for (int ahead = 0; ahead <= ui_px(&settings.ui, UI_ROW) && rows; ahead++)
                                                        rows = ui_list_hit(&settings.ui, &settings.list, finger.a, y + ahead) >= 0;
                                                if (rows)
                                                        over = y;
                                        }
                                        finger.b = over;
                                        settings_event_real(&finger);
                                        focus = settings.list.focus;
                                        check("settings: a finger that lands on rows still thrown stops them and presses none of them",
                                              over >= 0 && scroll_gliding(&settings.list.scroll) &&
                                                  (settings_event_real(&land), !scroll_gliding(&settings.list.scroll)) && settings.page.press.down &&
                                                  settings.page.press.target < 0 && settings.list.down < 0);
                                        settings_event_real(&lift);
                                        check("settings: and its lift there is no click: the keys stay where they were and nothing opens",
                                              settings.list.focus == focus && settings.queued == 0 && settings.page.menu_row < 0 && settings_editing() < 0);
                                }
                                settings.height = SETTINGS_HEIGHT;
                                settings_refilter();
                                ui_scroll_set(settings.page.body, 0);
                        }
#endif

                        // A list taller than the room is cut, and its bar shows what is cut: the keys' choice and the first in view go together.
                        // The room is its screen's and not the settings window's (the popup is sized to the screen it stands on,
                        // ui_popmenu_shows), so the screen is made as short as the window here.
                        settings.height = 220;
                        popup_fake.screen = 220;
                        settings_refilter();
                        settings_press(UI_KEY_ENTER);
                        check("settings: a list with less room than it needs shows what fits, within the screen",
                              settings.popup.menu.rows_shown >= 1 && settings.popup.menu.rows_shown < 13 && settings.popup.menu.height <= (int)popup_fake.screen);
                        settings_menu_press(UI_KEY_END);
                        check("settings: End on it scrolls to the last, which then shows",
                              settings.popup.menu.hot == 12 && ui_popmenu_top(&settings.popup.menu) == 13 - settings.popup.menu.rows_shown);
                        settings_menu_press(UI_KEY_HOME);
                        check("settings: Home scrolls back to the first", settings.popup.menu.hot == 0 && ui_popmenu_top(&settings.popup.menu) == 0);
                        settings_menu_gone();

                        // Only a choice with a state to read opens, and a value that is none of its words starts the keys on the first.
                        settings.height = SETTINGS_HEIGHT;
                        popup_fake.screen = SETTINGS_HEIGHT;
                        settings.state[kb] = SETTINGS_ABSENT;
                        settings_press(UI_KEY_ENTER);
                        check("settings: a choice the machine has none of opens no list", settings.page.menu_row < 0);
                        settings.state[kb] = SETTINGS_READ;
                        string_copy_bounded((string_address)settings.value[kb], (string_address) "xx", SETTINGS_VALUE);
                        settings_press(UI_KEY_ENTER);
                        check("settings: a value that is none of the choices starts the keys on the first", settings.page.menu_row == kb && settings.popup.menu.hot == 0);
                        settings_menu_press(UI_KEY_ESCAPE);

                        // The desktop's colour: a list of named inks, with the one in force the keys' and a letter going by name,
                        // a field that keeps what is typed as it is, and the pointer's rows found by what they are called.
                        {
                                int ink = settings_row_named("Background"), colour = settings_row_named("Background colour (rrggbb or default)"),
                                    size = settings_row_named("Cursor size (percent, or default)"), look = settings_row_named("Cursor look");

                                settings.page.chosen = settings_group_of(ink);
                                settings_refilter();
                                settings.page.pane = 1;
                                settings.list.focus = ink;
                                settings.state[ink] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[ink], (string_address) "forest", SETTINGS_VALUE);
                                settings_press(UI_KEY_ENTER);
                                check("settings: the inks open as a list, the one in force the keys'",
                                      settings.page.menu_row == ink && settings.popup.menu.count == 9 && settings.popup.menu.hot == 5);
                                settings_menu_type('p');
                                settings_menu_press(UI_KEY_ENTER);
                                check("settings: an ink chosen goes to the verb by its name, set as its colour there",
                                      settings.page.menu_row < 0 && settings.queued == 1 && settings.queue[0].row == ink &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "plum"));
                                settings.queued = 0;

                                settings.list.focus = colour;
                                settings.state[colour] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[colour], (string_address) "#1e1e1e", SETTINGS_VALUE);
                                settings_press(UI_KEY_ENTER);
                                check("settings: Enter on the colour field opens it on the colour as the verb says it",
                                      settings_editing() == colour && settings_holds("#1e1e1e"));
                                settings_press(UI_KEY_ENTER);
                                check("settings: and Enter on what it already is sends nothing", settings_editing() < 0 && settings.queued == 0);
                                settings_press(UI_KEY_ENTER);
                                for (positive at = 0; at < 7; at++)
                                        settings_press(UI_KEY_BACKSPACE);
                                for (const char address_to c = "20ff00"; address_to c; c++)
                                        settings_type_code((unsigned char)address_to c);
                                settings_press(UI_KEY_ENTER);
                                check("settings: a colour typed is kept on Enter, as typed, for the verb to refuse or take",
                                      settings_editing() < 0 && settings.queued == 1 && settings.queue[0].row == colour &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "20ff00"));
                                settings.queued = 0;

                                // The pointer's rows are the Input group's.
                                settings.page.chosen = settings_group_of(size);
                                settings_refilter();
                                settings.page.pane = 1;
                                settings.list.focus = size;
                                settings.state[size] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[size], (string_address) "80", SETTINGS_VALUE);
                                settings_press(UI_KEY_ENTER);
                                check("settings: the pointer's size is a slider that Enter steps on from 80 percent to 85",
                                      settings.queued == 1 && settings.queue[0].row == size &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "85"));
                                settings.queued = 0;
                                settings.list.focus = look;
                                settings.state[look] = SETTINGS_READ;
                                string_copy_bounded((string_address)settings.value[look], (string_address) "auto", SETTINGS_VALUE);
                                settings_press(UI_KEY_ENTER);
                                settings_type_code('d');
                                settings_press(UI_KEY_ENTER);
                                check("settings: and its look from auto, light and dark",
                                      settings.queued == 1 && settings.queue[0].row == look &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "dark"));
                                settings.queued = 0;

                                // With the ink's group shown, what is typed lists the rows of the other group too.
                                settings.page.chosen = settings_group_of(ink);
                                settings_refilter();
                                for (const char address_to c = "cursor"; address_to c; c++)
                                        settings_type_code((unsigned char)address_to c);
                                check("settings: typing cursor lists the pointer's rows, from whatever group is shown, and not the ink's",
                                      settings_pos(size) >= 0 && settings_pos(look) >= 0 && settings_pos(ink) < 0);
                                settings_press(UI_KEY_ESCAPE);
                                for (const char address_to c = "background"; address_to c; c++)
                                        settings_type_code((unsigned char)address_to c);
                                check("settings: and typing background lists the colour's, both rows",
                                      settings_pos(ink) >= 0 && settings_pos(colour) >= 0 && settings_pos(size) < 0);
                                settings_press(UI_KEY_ESCAPE);
                                settings.page.chosen = 0;
                                settings_refilter();
                        }
                        popup_fake.screen = 0;
                        settings.runner.child = 0;
                        settings.queued = 0;
                }

                // The feeds: the networks in range and the devices kept are rows that come and go as the verb is read again.
                {
                        static const char air[] =
                            "[Moonwater] wifi on\n[Moonwater]   HomeNet\n[Moonwater]   Old Cafe\n[Moonwater]   Far Away\n"
                            "[Moonwater] * HomeNet    -48 dBm ####  WPA2     5 GHz ch 36\n"
                            "[Moonwater]   Guest      -61 dBm ###.  open     2.4 GHz ch 6\n"
                            "[Moonwater] + Old Cafe   -80 dBm #...  WPA2/3   5 GHz ch 100\n"
                            "[Moonwater]   Corp -x    -70 dBm ##..  802.1X   5 GHz ch 40\n"
                            "[Moonwater]   and 4 more\n"
                            "[Moonwater] * joined  + saved  moonwater wifi add SSID asks for its password\n";
                        static const char again[] =
                            "[Moonwater] wifi off\n[Moonwater]   HomeNet\n[Moonwater]   Old Cafe\n[Moonwater]   Far Away\n"
                            "[Moonwater]   Newcomer   -40 dBm ####  WPA3     5 GHz ch 44\n"
                            "[Moonwater] * HomeNet    -50 dBm ###.  WPA2     5 GHz ch 36\n"
                            "[Moonwater] + Old Cafe   -82 dBm #...  WPA2/3   5 GHz ch 100\n"
                            "[Moonwater]   Guest      -62 dBm ###.  open     2.4 GHz ch 6\n"
                            "[Moonwater]   Corp -x    -70 dBm ##..  802.1X   5 GHz ch 40\n";
                        static const char silent[] =
                            "[Moonwater] wifi on\n[Moonwater]   HomeNet\n[Moonwater] no networks heard (a new scan needs root)\n";
                        static const char tooth[] = "[Moonwater] bluetooth on\n[Moonwater]   Keyboard K1\n[Moonwater]   Mouse\n";
                        int wifi = settings_row_named("Wi-Fi"), bt = settings_row_named("Bluetooth"), battery = settings_row_named("Battery"),
                            clock_row = settings_row_named("Clock"), wifi_group = settings_group_of(wifi),
                            bt_group = settings_group_of(bt);
                        struct settings_item address_to items = settings.item[0], address_to devices = settings.item[1];
                        struct windows address_to table = (struct windows address_to)compositor_call(
                            syscall(mmap), 0, (sizeof(struct windows) + 4095) & ~(positive)4095, 3, 0x4022, (positive)-1, 0);
                        int row_of;

                        check("settings: Wi-Fi and Bluetooth are groups of their own, each with the switch first",
                              wifi_group != bt_group && wifi_group != settings_group_of(settings_row_named("Wired")) &&
                                  string_equals((string_address)settings_group_name(wifi_group), (string_address) "Wi-Fi") &&
                                  string_equals((string_address)settings_group_name(bt_group), (string_address) "Bluetooth"));
                        settings.page.chosen = wifi_group;
                        // The table of feeds (settings_feeds) is Wi-Fi, Bluetooth, Devices, Waterlink, Boot, Power, Time and Network, in that order:
                        // the first five are lists, each feed's place its list's.
                        check("settings: the feeds are of Wi-Fi, Bluetooth, Devices, Waterlink, Boot, Power, Time and Network and no others",
                              settings_feed_of_group() == 0 && (settings.page.chosen = bt_group, settings_feed_of_group() == 1) &&
                                  (settings.page.chosen = settings_group_of(settings_row_named("New devices")), settings_feed_of_group() == 2) &&
                                  (settings.page.chosen = settings_group_of(settings_row_named("Pair a machine")), settings_feed_of_group() == 3) &&
                                  (settings.page.chosen = settings_group_of(settings_row_named("Start the desktop")), settings_feed_of_group() == SETTINGS_STARTUP_LIST) &&
                                  (settings.page.chosen = settings_group_of(battery), settings_feed_of_group() == 5) &&
                                  (settings.page.chosen = settings_group_of(clock_row), settings_feed_of_group() == 6) &&
                                  (settings.page.chosen = settings_group_of(settings_row_named("Wired IP address")), settings_feed_of_group() == 7) &&
                                  (settings.page.chosen = 0, settings_feed_of_group() < 0) && SETTINGS_FEEDS == 8);

                        // What moonwater wifi says, as the list: marker, name (with blanks), dBm, bars, what it asks for.
                        settings.page.chosen = wifi_group;
                        settings.list.focus = wifi;
                        memory_copy(settings_large->said, air, sizeof(air) - 1);
                        settings.runner.length = sizeof(air) - 1;
                        settings_feed_wifi();
                        items = settings.item[0], devices = settings.item[1]; // a feed may grow a list: its rows are read where they are now
                        check("settings: the networks heard are rows, strongest first as the verb says, then a saved one not heard",
                              settings.item_count[0] == 5 && string_equals((string_address)items[0].label, (string_address) "HomeNet") &&
                                  string_equals((string_address)items[1].label, (string_address) "Guest") &&
                                  string_equals((string_address)items[2].label, (string_address) "Old Cafe") &&
                                  string_equals((string_address)items[3].label, (string_address) "Corp -x") &&
                                  string_equals((string_address)items[4].label, (string_address) "Far Away") && !(items[4].flags & SETTINGS_ITEM_HEARD));
                        check("settings: a network's signal, bars and what it asks for are read from its row",
                              items[0].dbm == -48 && items[0].bars == 4 && string_equals((string_address)items[0].security, (string_address) "WPA2") &&
                                  items[1].dbm == -61 && items[1].bars == 3 && (items[1].flags & SETTINGS_ITEM_OPEN) &&
                                  items[2].dbm == -80 && items[2].bars == 1 && string_equals((string_address)items[2].security, (string_address) "WPA2/3"));
                        check("settings: the one joined and the ones saved are marked, a saved name among the heard marks that one",
                              (items[0].flags & SETTINGS_ITEM_JOINED) && (items[0].flags & SETTINGS_ITEM_SAVED) && !(items[1].flags & SETTINGS_ITEM_SAVED) &&
                                  (items[2].flags & SETTINGS_ITEM_SAVED) && (items[4].flags & SETTINGS_ITEM_SAVED));
                        check("settings: what moonwater cannot join is marked, and so is what it can", (items[3].flags & SETTINGS_ITEM_CANNOT) &&
                                  !(items[0].flags & SETTINGS_ITEM_CANNOT) && !(items[1].flags & SETTINGS_ITEM_CANNOT) && !(items[2].flags & SETTINGS_ITEM_CANNOT));
                        memory_copy(settings_large->said, silent, sizeof(silent) - 1);
                        settings.runner.length = sizeof(silent) - 1;
                        settings_feed_wifi();
                        check("settings: a line that is about no network is a row of its own, after the saved ones",
                              settings.item_count[0] == 2 && items[0].kind == SETTINGS_NETWORK && items[1].kind == SETTINGS_NOTE &&
                                  !memory_compare(items[1].label, "no networks heard", 17));

                        // Read again: the keys stay on the network by its name, the view stays, an unchanged list draws nothing.
                        settings.item_count[0] = 0;
                        settings.page.pane = 1;
                        ui_field_close(&settings.edit);
                        settings.page.menu_row = -1;
                        settings.feed_list = 0;
                        settings.value[wifi][0] = 0;
                        memory_copy(settings_large->said, air, sizeof(air) - 1);
                        settings.runner.length = sizeof(air) - 1;
                        settings_feed_read(false, false);
                        check("settings: a feed read puts its rows in the group, after the group's own, and sets its switch from the same words",
                              settings.item_count[0] == 5 && settings.state[wifi] == SETTINGS_READ &&
                                  string_equals((string_address)settings.value[wifi], (string_address) "on") &&
                                  settings.list.count == 1 + 5 && settings.list.id[0] == wifi);
                        settings.list.focus = (int)SETTINGS_LIST + 1; // Guest
                        settings.page.dirty = false;
                        settings_feed_read(false, false);
                        check("settings: a reading that says what the last said changes nothing and draws nothing", !settings.page.dirty && settings.list.focus == (int)SETTINGS_LIST + 1);
                        settings.list.scroll.scroll = 0;
                        memory_copy(settings_large->said, again, sizeof(again) - 1);
                        settings.runner.length = sizeof(again) - 1;
                        settings_feed_read(false, false);
                        row_of = settings.list.focus;
                        check("settings: a list that moves under the keys does not move them: they stay on the network of that name",
                              settings.item_count[0] == 6 && settings_item(row_of) && string_equals((string_address)settings_item(row_of)->label, (string_address) "Guest") &&
                                  row_of != (int)SETTINGS_LIST + 1 && settings.page.dirty && settings_pos(row_of) >= 0 && settings.page.pane == 1);
                        check("settings: and the switch follows what the verb says now", string_equals((string_address)settings.value[wifi], (string_address) "off"));
                        memory_copy(settings_large->said, silent, sizeof(silent) - 1);
                        settings.runner.length = sizeof(silent) - 1;
                        settings_feed_read(false, false);
                        check("settings: a network gone from under the keys leaves them on a row of the list", settings_item(settings.list.focus) != null &&
                                  settings_pos(settings.list.focus) >= 0);
                        settings.feed_due = 0;
                        settings_feed_read(true, false);
                        check("settings: a reading that was stopped changes no row, and the next is later", settings.item_count[0] == 2 &&
                                  settings.feed_due > (positive)ui_kit.now);
                        memory_copy(settings_large->said, air, sizeof(air) - 1);
                        settings.runner.length = sizeof(air) - 1;
                        settings_feed_read(false, false);
                        ui_field_begin(&settings.edit, &settings.own, (const char address_to) "", (int)SETTINGS_LIST + 2);
                        settings.page.dirty = false;
                        memory_copy(settings_large->said, again, sizeof(again) - 1);
                        settings.runner.length = sizeof(again) - 1;
                        settings_feed_read(false, false);
                        check("settings: nothing is changed under a passphrase being typed", settings.item_count[0] == 5 &&
                                  string_equals((string_address)items[1].label, (string_address) "Guest") && !settings.page.dirty);
                        ui_field_close(&settings.edit);

                        // What a person does to a row of the list.
                        settings.runner.child = 1;
                        settings.queued = 0;
                        settings.page.pane = 1;
                        memory_copy(settings_large->said, air, sizeof(air) - 1);
                        settings.runner.length = sizeof(air) - 1;
                        settings_feed_read(false, false);
                        settings.list.focus = (int)SETTINGS_LIST + 1; // Guest, open
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter on an open network joins it, by the verb's own words and no passphrase",
                              settings.queued == 1 && settings.queue[0].row == SETTINGS_JOB_ACT &&
                                  string_equals((string_address)settings.queue[0].value, (string_address) "j\nGuest") && settings_editing() < 0);
                        settings.queued = 0;
                        settings.list.focus = (int)SETTINGS_LIST; // HomeNet, joined
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter on the one joined sends nothing and says so", settings.queued == 0 && settings.status[0]);
                        settings.list.focus = (int)SETTINGS_LIST + 3; // Corp, 802.1X
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter on a network moonwater cannot join sends nothing and says so", settings.queued == 0 && settings_editing() < 0 &&
                                  string_length((string_address)settings.status) > 10);
                        settings.list.focus = (int)SETTINGS_LIST + 2; // Old Cafe, WPA2/3, saved
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter on a network that asks for a passphrase opens a field for it", settings_editing() == (int)SETTINGS_LIST + 2 &&
                                  settings.queued == 0 && string_equals((string_address)settings.joining, (string_address) "Old Cafe"));
                        for (const char address_to letter = "hunter22"; *letter; letter++)
                                settings_type_code((unsigned int)*letter);
                        settings_press(UI_KEY_ESCAPE);
                        {
                                b32 wiped = settings_kept_nothing();

                                check("settings: Escape leaves it: nothing is sent and nothing typed is kept", settings_editing() < 0 && settings.queued == 0 && wiped);
                        }
                        settings_press(UI_KEY_ENTER);
                        for (const char address_to letter = "hunter22"; *letter; letter++)
                                settings_type_code((unsigned int)*letter);
                        settings_press(UI_KEY_ENTER);
                        {
                                b32 wiped = settings_kept_nothing();

                                check("settings: Enter in it joins with the passphrase, and keeps none of it here",
                                      settings_editing() < 0 && settings.queued == 1 && settings.queue[0].row == SETTINGS_JOB_ACT &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "j\nOld Cafe\nhunter22") && wiped);
                        }
                        settings.queued = 0;

                        // The words that run it: the passphrase goes to the verb's input after "-", and is in no word.
                        {
                                string_address words[SETTINGS_WORDS];
                                const char address_to pass;

                                settings.moonwater = (string_address) "/moonwater";
                                string_copy_bounded((string_address)settings.job_value, (string_address) "j\nOld Cafe\nhunter22", sizeof(settings.job_value));
                                pass = settings_act_words(words);
                                check("settings: a join is wifi add, the name as one word, - for the passphrase, which goes to the input",
                                      words[0] && string_equals(words[1], (string_address) "wifi") && string_equals(words[2], (string_address) "add") &&
                                          string_equals(words[3], (string_address) "Old Cafe") && string_equals(words[4], (string_address) "-") && !words[5] &&
                                          pass && string_equals((string_address)pass, (string_address) "hunter22"));
                                string_copy_bounded((string_address)settings.job_value, (string_address) "j\nGuest", sizeof(settings.job_value));
                                pass = settings_act_words(words);
                                check("settings: an open network is joined with no passphrase and no -", words[0] && !pass && !words[4] &&
                                          string_equals(words[3], (string_address) "Guest"));
                                string_copy_bounded((string_address)settings.job_value, (string_address) "f\nOld Cafe", sizeof(settings.job_value));
                                pass = settings_act_words(words);
                                check("settings: forgetting is wifi remove and the name", words[0] && !pass && string_equals(words[2], (string_address) "remove") &&
                                          string_equals(words[3], (string_address) "Old Cafe") && !words[4]);
                                // A device of the Devices list is denied or allowed by its name: x and v, as a press on its row asks (settings_port_set).
                                string_copy_bounded((string_address)settings.job_value, (string_address) "x\n1-1", sizeof(settings.job_value));
                                pass = settings_act_words(words);
                                check("settings: denying a device is device deny and its name, and allowing it device allow",
                                      words[0] && !pass && string_equals(words[1], (string_address) "device") && string_equals(words[2], (string_address) "deny") &&
                                          string_equals(words[3], (string_address) "1-1") && !words[4] &&
                                          (string_copy_bounded((string_address)settings.job_value, (string_address) "v\n1-1", sizeof(settings.job_value)),
                                           settings_act_words(words), words[0] && string_equals(words[1], (string_address) "device") &&
                                                                          string_equals(words[2], (string_address) "allow") &&
                                                                          string_equals(words[3], (string_address) "1-1") && !words[4]));
                                string_copy_bounded((string_address)settings.job_value, (string_address) "q\nwhatever", sizeof(settings.job_value));
                                settings_act_words(words);
                                check("settings: and a change that is none of these runs nothing", !words[0]);
                                string_copy_bounded((string_address)settings.job_value, (string_address) "j\n", sizeof(settings.job_value));
                                settings_act_words(words);
                                check("settings: nor one with no name", !words[0]);
                        }

                        // Delete forgets, when pressed twice on the same name.
                        settings.list.focus = (int)SETTINGS_LIST + 2;
                        settings_press(UI_KEY_DELETE);
                        check("settings: Delete on a saved network asks to be pressed again", settings.queued == 0 && settings.confirm.armed);
                        settings_press(UI_KEY_DELETE);
                        check("settings: and the second Delete forgets it", settings.queued == 1 &&
                                  string_equals((string_address)settings.queue[0].value, (string_address) "f\nOld Cafe"));
                        settings.queued = 0;
                        settings_press(UI_KEY_DELETE);
                        settings_press(UI_KEY_DOWN);
                        settings_press(UI_KEY_DELETE);
                        check("settings: a key between two Deletes, or another network, starts it over", settings.queued == 0);
                        settings.list.focus = (int)SETTINGS_LIST + 1; // Guest, not saved
                        settings_press(UI_KEY_DELETE);
                        settings_press(UI_KEY_DELETE);
                        check("settings: Delete on a network that is not saved does nothing", settings.queued == 0);
                        // The kit's managed rows: a saved network has the remove button, which forgets it as Delete does (asking first),
                        // and one that is not saved has none.
                        {
                                struct ui_row saved_row, heard_row;
                                b32 asked;
                                memory_zero(&saved_row, sizeof(saved_row)), settings_describe((int)SETTINGS_LIST + 2, &saved_row);
                                memory_zero(&heard_row, sizeof(heard_row)), settings_describe((int)SETTINGS_LIST + 1, &heard_row);
                                settings.list.removed = (int)SETTINGS_LIST + 2 + 1, settings_managed();
                                asked = settings.queued == 0;
                                settings.list.removed = (int)SETTINGS_LIST + 2 + 1, settings_managed();
                                check("settings: a saved network has a remove button, which asks and then forgets it; one not saved has none",
                                      saved_row.remove && !heard_row.remove && asked && settings.queued == 1 &&
                                          string_equals((string_address)settings.queue[0].value, (string_address) "f\nOld Cafe"));
                                settings.queued = 0;
                        }

                        // Devices: the names kept, and a row that keeps another.
                        settings.page.chosen = bt_group;
                        settings.item_count[1] = 0;
                        settings.feed_list = 1;
                        memory_copy(settings_large->said, tooth, sizeof(tooth) - 1);
                        settings.runner.length = sizeof(tooth) - 1;
                        settings_feed_read(false, false);
                        check("settings: the devices kept are rows, and a last row keeps one more",
                              settings.item_count[1] == 3 && string_equals((string_address)devices[0].label, (string_address) "Keyboard K1") &&
                                  devices[0].kind == SETTINGS_DEVICE && devices[2].kind == SETTINGS_ADD &&
                                  string_equals((string_address)settings.value[bt], (string_address) "on"));
                        settings.list.focus = (int)SETTINGS_LIST_ID(1, 2);
                        settings_press(UI_KEY_ENTER);
                        for (const char address_to letter = "Pen"; *letter; letter++)
                                settings_type_code((unsigned int)*letter);
                        settings_press(UI_KEY_ENTER);
                        check("settings: Enter on it opens a field, and the name typed is kept by bluetooth add", settings.queued == 1 &&
                                  string_equals((string_address)settings.queue[0].value, (string_address) "a\nPen") && settings_editing() < 0);
                        settings.queued = 0;
                        settings.list.focus = (int)SETTINGS_LIST_ID(1, 1);
                        settings_press(UI_KEY_DELETE);
                        settings_press(UI_KEY_DELETE);
                        check("settings: and Delete twice on a device forgets it", settings.queued == 1 &&
                                  string_equals((string_address)settings.queue[0].value, (string_address) "d\nMouse"));
                        settings.queued = 0;
                        // The kit's managed rows: a device kept has its remove button, which forgets it as Delete does (the first press
                        // asks), and the last row is the kit's add row, the plus and its words.
                        {
                                struct ui_row kept_row, add_row;
                                b32 asked;
                                memory_zero(&kept_row, sizeof(kept_row)), settings_describe((int)SETTINGS_LIST_ID(1, 0), &kept_row);
                                memory_zero(&add_row, sizeof(add_row)), settings_describe((int)SETTINGS_LIST_ID(1, 2), &add_row);
                                check("settings: a device kept has a remove button, and the add row is the kit's",
                                      kept_row.remove && !kept_row.order && add_row.kind == UI_ROW_ADD && !add_row.remove);
                                settings.list.removed = (int)SETTINGS_LIST_ID(1, 0) + 1, settings_managed();
                                asked = settings.queued == 0 && !settings.list.removed;
                                settings.list.removed = (int)SETTINGS_LIST_ID(1, 0) + 1, settings_managed();
                                check("settings: its remove button asks first, and pressed again forgets the device by bluetooth remove",
                                      asked && settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "d\nKeyboard K1"));
                                settings.queued = 0;
                        }
                        settings.runner.child = 0;

                        // The rows that change by themselves, and when a feed is run.
                        {
                                int charge = settings_row_named("Charge limit (20 to 100, or off)");
                                char value[SETTINGS_VALUE];

                                check("settings: a verb that two rows ask is one run", settings_same_verb(&settings_rows[battery], &settings_rows[charge]) &&
                                          !settings_same_verb(&settings_rows[wifi], &settings_rows[bt]) &&
                                          settings_same_verb(&settings_rows[wifi], &settings_rows[wifi]));
                                check("settings: the battery says its charge and state, and is not a row that is set",
                                      settings_parsed(battery, "[Moonwater] battery 80%, Charging; charging stops at 80%\n", value) == SETTINGS_READ &&
                                          string_equals((string_address)value, (string_address) "80%, Charging") &&
                                          settings_parsed(battery, "[Moonwater] this machine has no battery\n", value) == SETTINGS_ABSENT &&
                                          settings_rows[battery].kind == SETTINGS_INFO);
                                memory_zero(settings.unread, sizeof(settings.unread));
                                settings.page.chosen = settings_group_of(battery);
                                settings_feed_rows();
                                check("settings: a rows feed asks again the rows of its group that change by themselves, and those only",
                                      settings_unread_only((positive)battery));
                                memory_zero(settings.unread, sizeof(settings.unread));
                                settings.page.chosen = settings_group_of(clock_row);
                                settings_feed_rows();
                                check("settings: the clock is such a row, and the time server beside it is not",
                                      settings_unread_only((positive)clock_row));
                                memory_zero(settings.unread, sizeof(settings.unread));
                        }
                        {
                                // The first reading's count: the rows a verb reads, and not the rows the Display map and the notices
                                // read from the compositor's table, which no verb asks and which are never unread or failed.
                                unsigned char kept[SETTINGS_ROWS];

                                memory_copy(kept, settings.state, sizeof(kept));
                                for (positive at = 0; at < SETTINGS_ROWS; at++)
                                        settings.state[at] = settings_rows[at].output == SETTINGS_OUT_NONE ? SETTINGS_READ : SETTINGS_UNREAD;
                                check("settings: the rows the compositor's table reads are not counted as unread: every verb row read, none counted",
                                      settings_not_understood() == 0);
                                settings.state[battery] = SETTINGS_FAILED;
                                check("settings: a verb row that is not understood is counted once", settings_not_understood() == 1);
                                memory_copy(settings.state, kept, sizeof(kept));
                        }
                        settings.own.table = table;
                        settings.page.chosen = bt_group;
                        settings.page.card = false;
                        ui_field_close(&settings.edit);
                        settings_filter_clear();
                        settings.queued = 0;
                        settings.help = 2;
                        settings.runner.child = 0;
                        memory_zero(settings.unread, sizeof(settings.unread));
                        ui_kit.now = 2000000000ull, settings.feed_due = ui_kit.now + 5000000000ull;
                        {
                                long wait = settings_feed_tick();

                                check("settings: a feed not yet due waits the time that is left", wait > 4000000000l && wait <= 5000000000l && settings.runner.child == 0);
                        }
                        table->state[settings.own.row] = WINDOW_MINIMIZED;
                        check("settings: a feed is not run, and not waited for, while the window is put away", settings_feed_tick() == -1);
                        table->state[settings.own.row] = 0;
                        settings.page.chosen = 0;
                        check("settings: nor while the group shown has none", settings_feed_tick() == -1);
                        settings.page.chosen = bt_group;
                        ui_text_set(settings_filter_field(), (const p8 address_to) "x", 1), ui_text_search_changed(settings_filter_field());
                        check("settings: nor while what is typed filters the rows of every group", settings_feed_tick() == -1);
                        settings_filter_clear();
                        ui_field_begin(&settings.edit, &settings.own, (const char address_to) "", 0);
                        check("settings: nor while a field is typed in", settings_feed_tick() == -1);
                        ui_field_close(&settings.edit);
                        settings.page.card = true;
                        check("settings: nor while a card fills the window", settings_feed_tick() == -1);
                        settings.page.card = false;
                        settings.queued = 1;
                        check("settings: nor while a change waits", settings_feed_tick() == -1);
                        settings.queued = 0;
                        settings.runner.child = 1;
                        check("settings: nor while another verb runs", settings_feed_tick() == -1);
                        settings.runner.child = 0;
                        settings_unread_set(0, 1);
                        check("settings: nor while rows are still to be asked", settings_feed_tick() == -1);
                        memory_zero(settings.unread, sizeof(settings.unread));
                        settings.feed_due = 0;
                        settings.moonwater = (string_address) "/nonexistent/moonwater-check";
                        settings_feed_tick();
                        check("settings: a feed due is run: its verb is started, as a reading and not a change",
                              settings.runner.child > 0 && settings.job == SETTINGS_JOB_FEED && !settings.job_sets && settings.feed_list == 1);
                        settings_feed_drop();
                        // The killed child is not waited for by the drop: it is reaped by settings_reap when it has gone, polled here as the
                        // window's timers poll it (settings_tick), so the check polls too.
                        for (int tries = 0; settings.runner.child > 0 && tries < 200; tries++)
                        {
                                timespec nap = {0, 10000000};

                                if (settings_reap())
                                        break;
                                system_call_2(syscall(nanosleep), (positive)&nap, 0);
                        }
                        check("settings: a change that arrives drops a reading under way, which is reaped once it has gone and had again later",
                              settings.runner.child <= 0 && !settings.runner.ending && settings.feed_due > (positive)ui_kit.now);
                        settings.moonwater = (string_address) "/moonwater";
                        compositor_call(syscall(munmap), (positive)table, (sizeof(struct windows) + 4095) & ~(positive)4095, 0, 0, 0, 0);
                        settings.own.table = null, settings.page.buffers.shown = -1;
                        settings.page.chosen = 0;
                        settings.item_count[0] = 0;
                        settings.item_count[1] = 0;
                        settings.page.pane = 1;
                        settings_refilter();
                }
                compositor_call(syscall(munmap), (positive)engine, sizeof(font_engine), 0, 0, 0, 0);
                // The window's faces go with the engine they were drawn with: what is laid out after this is laid out with no face, as a
                // screen of words alone is, and measures nothing (a wrapped text measures its words when it is laid out).
                settings.ui.engine = null, settings.ui.regular = settings.ui.bold = settings.ui.mono = null;
        }
}
#endif

#if MOONWATER_UI_SETTINGS
/*
        The arrow keys on the settings window's rows, as the window receives
        them. Up and Down move the focus between the rows of the group and
        turn nothing; Left and Right throw the focused switch off and on, and
        Space and Enter turn it over. The verb is not run: a change the window
        asks for is in its queue, and the value the verb would give is put
        back by the check the way the window's reading puts it.
*/
static b32 settings_turned(int row, const char address_to want)
{
        b32 turned = settings.queued == 1 && settings.queue[0].row == row &&
                     string_equals((string_address)settings.queue[0].value, (string_address)want);

        settings.queued = 0;
        if (turned)
                string_copy_bounded((string_address)settings.value[row], (string_address)want, SETTINGS_VALUE);
        return turned;
}

/*
        The settings window at its edges. A value that changed under a field opened on it, and Enter in that field with
        nothing typed, keeps the new value: the field's own text is what was kept, and it is not sent back over the change.
        A filter that matches no row lists none, and Enter or Space in it changes no row that is not shown. A row that
        filters out while it has the focus is not the focus, and a value typed to its full length is kept whole.
*/
static fn check_settings_edges(void)
{
        int zone = settings_row_named("Time zone"), wired = settings_row_named("Wired");

        settings.ui.scale = 100;
        settings.width = SETTINGS_WIDTH;
        settings.height = SETTINGS_HEIGHT;
        settings.page.pane = 1;
        ui_field_close(&settings.edit);
        settings.page.menu_row = -1;
        settings.page.card = 0;
        settings.runner.child = 1;
        settings.job = 0;
        settings.job_sets = false;
        settings.queued = 0;
        ui_confirm_drop(&settings.confirm);
        settings.page.chosen = settings_group_of(zone);
        settings_refilter();
        settings.list.focus = zone;
        settings.state[zone] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[zone], (string_address) "Europe/Oslo", SETTINGS_VALUE);

        settings_activate(zone, 1);
        check("settings edge: Enter on a plain value opens its field", settings_editing() == zone);
        string_copy_bounded((string_address)settings.value[zone], (string_address) "Europe/Paris", SETTINGS_VALUE);
        settings_press(UI_KEY_ENTER);
        check("settings edge: Enter in a field nothing was typed in sends nothing, and the value that changed under it stays",
              settings.queued == 0 && settings_editing() < 0 && string_equals((string_address)settings.value[zone], (string_address) "Europe/Paris"));
        settings.queued = 0;

        settings_hold(zone, "");
        for (positive at = 0; at < SETTINGS_VALUE + 40; at++)
                settings_type_code('a' + (unsigned int)(at % 26));
        settings_press(UI_KEY_ENTER);
        check("settings edge: a value typed past its length is kept at its length, sent whole",
              settings.queued == 1 && settings.queue[0].row == zone && string_length((string_address)settings.queue[0].value) == SETTINGS_VALUE - 1);
        settings.queued = 0;

        settings.state[wired] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[wired], (string_address) "off", SETTINGS_VALUE);
        settings.list.focus = wired;
        for (positive at = 0; at < 4; at++)
                settings_type_code('q');
        check("settings edge: a filter that matches no row lists none", settings.list.count == 0);
        check("settings edge: a focus on a row that filtered out is not kept", settings.list.count == 0 || settings_pos(settings.list.focus) >= 0);
        settings_press(UI_KEY_ENTER);
        settings_press(UI_KEY_SPACE);
        check("settings edge: Enter and Space in an empty list change no row, shown or not",
              settings.queued == 0 && string_equals((string_address)settings.value[wired], (string_address) "off"));
        settings_press(UI_KEY_ESCAPE);

        ui_field_close(&settings.edit);
        settings.queued = 0;
        settings.runner.child = 0;
        settings_filter_clear();
        settings_refilter();
}

/*
        A verb's child that will not exit on its kill (snug-asteroid's `moonwater canvas modes`, stuck in the kernel in state D) neither
        holds the settings' close nor stops every verb after it. A child stopped with SIGSTOP stands in for it: SIGTERM, which a pairing's
        close sends, is held pending while it is stopped, so it does not exit, as a D child does not on SIGKILL. The close must return at
        once (the old one waited for it for ever), and a killed child that has not exited SETTINGS_LEFT_NS later is left: its output
        closed, the verb complete as one that took too long, the next verb free to run, and the child reaped when it does exit.
*/
static fn check_settings_stuck_child(void)
{
        int ends[2] = {-1, -1};
        bipolar child;
        positive status = 0, before, took;

        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) != 0)
        {
                check("settings stuck: a pipe for the child", false);
                return;
        }
        child = system_fork();
        if (child == 0)
        {
                system_call_2(syscall(kill), (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), SIGSTOP);
                system_call_1(syscall(exit), 0);
        }
        system_close(ends[1]);
        system_call_4(syscall(wait4), (positive)child, (positive)&status, 2, 0);
        settings.runner.child = child;
        settings.runner.output = ends[0];
        settings.runner.ending = 0;
        settings.job = 0;
        settings.job_sets = false;
        settings_pair_on = true;
        before = (positive)ui_kit_clock();
        settings_close();
        took = (positive)ui_kit_clock() - before;
        settings_pair_on = false;
        check("settings stuck: the close of a window whose verb's child will not exit returns within a second",
              took < 1000000000ull);

        // The same child, its kill sent and not taken: left once SETTINGS_LEFT_NS has gone since the kill.
        settings.runner.ending = 2;
        settings.runner.began = (positive)ui_kit.now > (positive)SETTINGS_LEFT_NS + 1 ? (positive)ui_kit.now - (positive)SETTINGS_LEFT_NS - 1 : 0; // the kit's clock here is short of the window the kill waits out
        settings.state[0] = SETTINGS_READ;
        {
                long other = 0;

                settings_tick(-1, &other);
        }
        // Left, the job is free: the feed the window runs next (settings_feed_tick, once the job is gone) may start in the same tick.
        check("settings stuck: a killed child that has not exited is left, and the verb after it is free to run",
              settings.runner.child != child && !settings.runner.ending);
        if (settings.runner.child > 0)
        {
                system_call_2(syscall(kill), (positive)settings.runner.child, SIGKILL);
                system_call_4(syscall(wait4), (positive)settings.runner.child, 0, 0, 0);
                settings.runner.child = 0;
        }
        check("settings stuck: the row of a verb left reads as failed", settings.state[0] == SETTINGS_FAILED);
        check("settings stuck: the child left is kept to be reaped", settings_left[0] == child);
        // It goes once it runs again, and is reaped then.
        system_call_2(syscall(kill), (positive)child, SIGKILL);
        system_call_2(syscall(kill), (positive)child, SIGCONT);
        for (int tries = 0; tries < 200 && settings_left_reap(); tries++)
        {
                timespec nap = {0, 10000000};

                system_call_2(syscall(nanosleep), (positive)&nap, 0);
        }
        check("settings stuck: a child left is reaped when it exits", settings_left[0] <= 0);
        settings.runner.child = 0;
        settings.runner.output = -1;
}

static fn check_settings_keys(void)
{
        int wired = settings_row_named("Wired"), at = 0, below = 0;

        settings.ui.scale = 100;
        settings.width = SETTINGS_WIDTH;
        settings.height = SETTINGS_HEIGHT;
        settings.page.pane = 1;
        settings_filter_clear();
        ui_field_close(&settings.edit);
        settings.page.menu_row = -1;
        settings.page.card = 0;
        //      A feed left running by an earlier check would be finished by a change: this one is not a feed.
        settings.runner.child = 1;
        settings.job = 0;
        settings.job_sets = false;
        settings.queued = 0;
        ui_confirm_drop(&settings.confirm);
        settings.page.chosen = settings_group_of(wired);
        settings_refilter();
        while (at < (int)settings.list.count && settings.list.id[at] != wired)
                at++;
        below = at + 1 < (int)settings.list.count ? settings.list.id[at + 1] : wired;

        //      A switch: Down and Up move between rows and turn nothing; Right does nothing and Left goes back to the column;
        //      Enter or Space turns it over (Right in the column goes to the rows again).
        settings.list.focus = wired;
        settings.state[wired] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[wired], (string_address)"off", SETTINGS_VALUE);
        check("settings keys: the Wired switch has a row below it in its group", below != wired);

        settings_press(UI_KEY_DOWN);
        check("settings keys: Down moves the focus to the row below and turns nothing",
              settings.list.focus == below && !settings.queued && string_equals((string_address)settings.value[wired], (string_address)"off"));
        settings_press(UI_KEY_UP);
        check("settings keys: Up moves the focus back to the switch and turns nothing",
              settings.list.focus == wired && !settings.queued && string_equals((string_address)settings.value[wired], (string_address)"off"));
        settings_press(UI_KEY_RIGHT);
        check("settings keys: Right on the switch turns nothing and keeps the focus and the rows",
              settings.list.focus == wired && settings.page.pane == 1 && !settings.queued &&
                  string_equals((string_address)settings.value[wired], (string_address)"off"));
        settings_press(UI_KEY_LEFT);
        check("settings keys: Left on the switch goes back to the column and turns nothing",
              settings.page.pane == 0 && settings.list.focus == wired && !settings.queued &&
                  string_equals((string_address)settings.value[wired], (string_address)"off"));
        settings_press(UI_KEY_RIGHT);
        check("settings keys: Right in the column goes to the rows, on the switch", settings.page.pane == 1 && settings.list.focus == wired && !settings.queued);
        settings_press(UI_KEY_SPACE);
        check("settings keys: Space turns the switch on and keeps the focus", settings_turned(wired, "on") && settings.list.focus == wired);
        settings_press(UI_KEY_ENTER);
        check("settings keys: Enter turns the switch off again and keeps the focus", settings_turned(wired, "off") && settings.list.focus == wired);
        settings_press(UI_KEY_DOWN);
        check("settings keys: and Down leaves the switch after all of them", settings.list.focus == below && !settings.queued);

        //      The Battery row's words are the battery's: a charge limit the battery has none of does not take the row away.
        {
                char value[SETTINGS_VALUE];
                unsigned int state = settings_parsed((unsigned int)settings_row_named("Battery"),
                                                     "[Moonwater] battery 90%, Full; this battery has no charge limit\n", value);

                check("settings: the Battery row keeps its value when the verb says the battery has no charge limit",
                      state == SETTINGS_READ && string_equals((string_address)value, (string_address)"90%, Full"));
        }

        //      Commands are not states: a join, a forget and a command typed in the Run any row, each waiting behind a
        //      verb that is running, are all kept and run in order; a change to a row waiting replaces it, as it did.
        settings.runner.child = 1;
        settings.job = 0;
        settings.job_sets = false;
        settings.queued = 0;
        settings_act('j', "alpha", "first passphrase");
        settings_act('j', "beta", "second passphrase");
        settings_act('f', "gamma", null);
        check("settings: three network changes waiting behind a verb are all kept", settings.queued == 3);
        settings_act('f', "gamma", null);
        check("settings: the same command asked again while it waits is done once", settings.queued == 3);
        settings_run(SETTINGS_ANY, "wifi scan");
        settings_run(SETTINGS_ANY, "bluetooth scan");
        check("settings: two commands typed in the Run any row, waiting behind a verb, are both kept", settings.queued == 5);
        settings_change(0, "mw-one");
        settings_change(0, "mw-two");
        check("settings: a second change to a row waiting replaces the first", settings.queued == 6 &&
              string_equals((string_address)settings.queue[5].value, (string_address)"mw-two"));
        settings.queued = 0;

        settings.runner.child = 0;
        settings.queued = 0;
        settings.page.chosen = 0;
        settings.page.pane = 1;
        settings_refilter();
}

/*
        The settings window's picture as its loop makes it, frame by frame:
        a frame draws only the rows that changed into the buffer the
        compositor is not reading, and the kit asks for the rows of what is
        still moving at the next. Switches thrown down a group with Space
        and Down as fast as a held key repeats, the verbs' answers coming
        between the frames: the buffer shown after every frame is the whole
        picture drawn at that frame's time, and so is the other buffer once
        it is drawn again with nothing new.
*/
#define SETTINGS_FRAMES_W (2 * SETTINGS_WIDTH) // the buffers hold the window at scale 2, and the gallery (1092 by 1538)
#define SETTINGS_FRAMES_H 1600
#define SETTINGS_FRAMES_PERIOD 16666667ull
#define SETTINGS_FRAMES_MS 1000000ull

static unsigned int settings_frames_whole[SETTINGS_FRAMES_W * SETTINGS_FRAMES_H];
static p8 settings_frames_kept[sizeof(settings)], settings_frames_kit[sizeof(ui_kit)];
static struct ui_list_slot settings_frames_controls[UI_LIST_POOL];

static struct
{
        struct windows address_to table;
        positive now, frames, moving, asked;
        b32 wrong;
        string_address scenario;
} settings_frames;

// The buffer in slot against the whole picture at the kit's time, everything the drawing moves put back after it. A drawing
// sends the window no event and so grows no field's document: the bytes alone are the whole of what it moves (settings_kept).
static b32 settings_frames_same(int slot, string_address when)
{
        const unsigned int address_to shown = windows_buffer(settings_frames.table, settings.own.row, (unsigned int)slot);

        memory_copy(settings_frames_kept, &settings, sizeof(settings));
        memory_copy(settings_frames_kit, &ui_kit, sizeof(ui_kit));
        if (settings.list.pool)
                memory_copy(settings_frames_controls, settings.list.pool, sizeof(settings_frames_controls));
        settings_draw_rows(settings_frames_whole, 0, 0);
        memory_copy(&settings, settings_frames_kept, sizeof(settings));
        memory_copy(&ui_kit, settings_frames_kit, sizeof(ui_kit));
        if (settings.list.pool)
                memory_copy(settings.list.pool, settings_frames_controls, sizeof(settings_frames_controls));
        // The picture is the window's size, which a resize makes less than the buffer's.
        for (int y = 0; y < settings.height; y++)
                for (int x = 0; x < settings.width; x++)
                        if (shown[y * SETTINGS_FRAMES_W + x] != settings_frames_whole[y * SETTINGS_FRAMES_W + x])
                        {
                                int at = ui_list_hit(&settings.ui, &settings.list, settings.list.x + 1, y);
                                int group = ui_page_column_hit(&settings.ui, &settings.page, ui_dev(&settings.ui, UI_PAD) + 1, y);
                                b32 in_column = x < settings.list.x && group >= 0;

                                string_format(log, "  settings frames (%s): %s, frame %p, buffer %p, %b by %b: pixel %b,%b is %p, the whole draw %p (%s %s)\n",
                                              settings_frames.scenario, when, settings_frames.frames, (positive)slot, (bipolar)settings.width,
                                              (bipolar)settings.height, (bipolar)x, (bipolar)y,
                                              (positive)shown[y * SETTINGS_FRAMES_W + x], (positive)settings_frames_whole[y * SETTINGS_FRAMES_W + x],
                                              (string_address)(in_column ? "the column's group" : "row"),
                                              (string_address)(in_column ? settings_group_name(group) : at >= 0 ? settings_label(at) : "none"));
                                return false;
                        }
        return true;
}

// One pass of the kit's loop (ui_app_run) at now: a frame when the picture is due and a display period has gone since the last.
/*
        Calls per draw (the kit's counter, ui_kit_calls: the painters the apps call, counted in ui.c): the first draw of each app in
        the run prints how many painter calls it made, and a row says it made some. Each app has its own row.
*/
static b32 kit_draw_seen[5];

static b32 kit_draw_first(int app)
{
        if (kit_draw_seen[app])
                return false;
        kit_draw_seen[app] = true;
        return true;
}

static void kit_draw_print(const char address_to app, unsigned long calls)
{
        char digits[24];
        positive at = sizeof(digits) - 1;

        digits[at] = 0;
        do
        {
                digits[--at] = (char)('0' + calls % 10);
                calls /= 10;
        } while (calls && at > 0);
        string_format(log, "  kit calls: one draw of %s makes %s painter calls\n", (string_address)app, (string_address)(digits + at));
}

static fn settings_frames_pass(void)
{
        positive now = settings_frames.now;

        ui_kit.test_clock = (b64)now;
        if (!settings.page.dirty || (ui_kit.busy && now < (positive)ui_kit.drawn + SETTINGS_FRAMES_PERIOD))
                return;
        ui_kit.now = (b64)now;
        ui_kit.busy = 0;
        ui_kit.damage_top = ui_kit.damage_bottom = 0;
        ui_kit_turn();
        {
                unsigned long was = ui_kit_calls;

                if (!settings_frame())
                        return;
                if (kit_draw_first(0))
                {
                        kit_draw_print("settings", ui_kit_calls - was);
                        check("kit calls: a draw of settings is made of painter calls", ui_kit_calls - was > 0);
                }
        }
        ui_kit.drawn = (b64)now;
        if (ui_kit.busy)
        {
                settings.page.dirty = true;
                ui_app_moved(&settings_app, ui_kit.damage_top, ui_kit.damage_bottom, !ui_tween_still(&ui_kit.turn));
                settings_frames.moving++;
        }
        // The compositor takes the picture at once, and what it shows is the buffer named.
        settings_frames.table->shown[settings.own.row] = settings_frames.table->frame[settings.own.row];
        settings_frames.frames++;
        if (!settings_frames.wrong)
                settings_frames.wrong = !settings_frames_same(settings.page.buffers.shown, (string_address) "shown");
}

// The loop until a time: a pass now, then a frame each display period while anything moves.
static fn settings_frames_until(positive until)
{
        settings_frames_pass();
        while (settings.page.dirty && ui_kit.busy && (positive)ui_kit.drawn + SETTINGS_FRAMES_PERIOD <= until)
        {
                settings_frames.now = (positive)ui_kit.drawn + SETTINGS_FRAMES_PERIOD;
                settings_frames_pass();
        }
        settings_frames.now = until > settings_frames.now ? until : settings_frames.now;
}

/*
        A scenario: from the row named (in the rows, or in the column of groups when column is set), first and then second
        every gap ms, keys times (second 0 for none); each change answered by its verb answer ms after the key that asked
        for it (the value it set read back, as settings_finished takes a reading that changed). Then the loop until
        nothing moves, the buffer shown and the other compared, the other drawn again with nothing new.
*/
#define SETTINGS_FRAMES_KEYS 24

static fn settings_frames_scenario(string_address name, b32 switches_only, b32 column, const char address_to from, int first,
                                   int second, int keys, positive gap, positive answer)
{
        positive start = settings_frames.now, presses[SETTINGS_FRAMES_KEYS * 2], answers[SETTINGS_FRAMES_KEYS];
        int pressed = 0, answered = 0, count = 0;

        settings_frames.scenario = name;
        settings_frames.wrong = false;
        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].kind == SETTINGS_SWITCH)
                        settings.state[at] = SETTINGS_READ,
                        string_copy_bounded((string_address)settings.value[at], (string_address)(at % 3 ? "off" : "on"), SETTINGS_VALUE);
        settings.page.chosen = settings_group_of(settings_row_named(from));
        settings_refilter();
        if (switches_only)
        {
                for (positive at = 0; at < SETTINGS_ROWS; at++)
                        if (settings_rows[at].kind == SETTINGS_SWITCH && settings.state[at] == SETTINGS_READ)
                                settings.list.id[count++] = (unsigned short)at;
                settings.list.count = count;
                settings.list.scroll.scroll = 0;
                ui_list_layout(&settings.ui, &settings.list);
                ui_page_redraw(&settings.page);
        }
        settings.list.focus = settings_row_named(from);
        ui_page_reveal(&settings.ui, &settings.page, &settings.list);
        settings.page.pane = !column;
        ui_page_redraw(&settings.page);
        settings_frames_until(start + 1000 * SETTINGS_FRAMES_MS);
        start = settings_frames.now;

        keys = keys > SETTINGS_FRAMES_KEYS ? SETTINGS_FRAMES_KEYS : keys;
        for (int k = 0; k < keys; k++)
        {
                presses[2 * k] = start + (positive)k * gap * SETTINGS_FRAMES_MS;
                presses[2 * k + 1] = presses[2 * k] + gap * SETTINGS_FRAMES_MS / 2;
                answers[k] = presses[2 * k] + answer * SETTINGS_FRAMES_MS;
        }
        // The keys and the answers in the order of their times.
        while (pressed < 2 * keys || answered < keys)
                if (pressed < 2 * keys && (answered >= pressed / 2 + (pressed % 2) || presses[pressed] <= answers[answered]))
                {
                        settings_frames_until(presses[pressed]);
                        if (pressed % 2 == 0 || second)
                                settings_press(pressed % 2 ? second : first);
                        pressed++;
                        settings_frames_pass();
                }
                else
                {
                        settings_frames_until(answers[answered]);
                        answered++;
                        if (settings.queued)
                        {
                                int row = settings.queue[0].row;

                                string_copy_bounded((string_address)settings.value[row], (string_address)settings.queue[0].value, SETTINGS_VALUE);
                                settings_turning(row, false, false); // the answer is the verb run and its row read back (settings_complete's read)
                                settings.queued--;
                                memory_copy(settings.queue, settings.queue + 1, settings.queued * sizeof(settings.queue[0]));
                                settings_say((string_address) "done");
                                settings_reading_changed(row);
                        }
                        settings_frames_pass();
                }
        settings_frames_until(settings_frames.now + 2000 * SETTINGS_FRAMES_MS);
        check("settings frames: the loop comes to rest", !settings.page.dirty);
        // The buffer not shown, drawn again with nothing new, then the first again: each is the whole picture then.
        for (int again = 0; again < 2 && !settings_frames.wrong; again++)
        {
                settings.page.dirty = true;
                settings_frames_until(settings_frames.now + SETTINGS_FRAMES_PERIOD);
                settings_frames.wrong = !settings_frames_same(settings.page.buffers.shown, (string_address)(again ? "at rest, the first again" : "at rest, the other"));
        }
        check("settings frames: every buffer shown is the whole picture drawn at its time", !settings_frames.wrong);
        settings.runner.child = 0;
        settings.queued = 0;
}

/*
        A script: steps at their times from the start (ms), each a key, the pointer put on a row (at x), on a segment
        or a swatch of a row, or on a group of the column, the button down or up, the wheel, a verb's answer, the theme
        turned (as the loop takes a new theme from the table) or the window resized; the loop runs between them, then
        until nothing moves, and every buffer shown is compared as in a scenario.
*/
#define SF_KEY 1
#define SF_ROW 2
#define SF_PART 3
#define SF_GROUP 4
#define SF_DOWN 5
#define SF_UP 6
#define SF_WHEEL 7
#define SF_ANSWER 8
#define SF_THEME 9
#define SF_SIZE 10
#define SF_FINGER_DOWN 11
#define SF_FINGER_MOVE 12
#define SF_FINGER_UP 13
#define SF_CARD 14
static int settings_frames_finger_y;     // where the finger went down, device pixels
struct settings_frames_step
{
        int ms, what;
        const char address_to row;
        int a, b;
};

static fn settings_frames_event(unsigned int type, unsigned int flags, int a, int b)
{
        struct window_event event = {type, flags, a, b};

        ui_kit_touch = (flags & WINDOW_KEY_TOUCH) != 0; // the loop's context for the event (the clock is the harness's own)
        settings_event_at(&event);
}

static fn settings_frames_play(string_address name, int group, const struct settings_frames_step address_to steps, int count)
{
        positive start, frames = settings_frames.frames, moving = settings_frames.moving, asked = settings_frames.asked;

        settings_frames.scenario = name;
        settings_frames.wrong = false;
        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        settings.list.hover = settings.list.down = -1;
        settings.page.chosen = group;
        settings.list.scroll.scroll = 0;
        settings_refilter();
        settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
        start = settings_frames.now;
        for (int at = 0; at < count; at++)
        {
                const struct settings_frames_step address_to s = &steps[at];
                int row = s->row ? settings_row_named(s->row) : -1, place = row >= 0 ? ui_list_at(&settings.list, row) : -1;
                int y = place >= 0 ? ui_list_row_y(&settings.list, place) + ui_dev(&settings.ui, UI_ROW) / 2 : 0;

                settings_frames_until(start + (positive)s->ms * SETTINGS_FRAMES_MS);
                switch (s->what)
                {
                case SF_KEY: settings_press(s->a); break;
                case SF_ROW: settings_frames_event(WINDOW_EVENT_MOTION, 0, s->a * settings.ui.scale / 100, y); break;
                case SF_PART:
                {
                        const struct ui_control address_to c = ui_list_control(&settings.list, row, 1);
                        int x = !c ? 0 : c->kind == UI_TABS ? ui_tab_left(&settings.ui, c, s->a) + ui_tab_width(&settings.ui, c, s->a) / 2
                                                            : ui_swatches_left(&settings.ui, c) + s->a * (UI_SWATCH + UI_SWATCH_GAP) + UI_SWATCH / 2;

                        settings_frames_event(WINDOW_EVENT_MOTION, 0, x * settings.ui.scale / 100, y);
                        break;
                }
                case SF_GROUP:
                        settings_frames_event(WINDOW_EVENT_MOTION, 0, ui_dev(&settings.ui, UI_PAD) + 20,
                                              settings.page.top + s->a * ui_dev(&settings.ui, UI_ROW) + ui_dev(&settings.ui, UI_ROW) / 2 -
                                                  settings.page.column_scroll.scroll);
                        break;
                case SF_DOWN: settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0); break;
                case SF_UP: settings_frames_event(WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0); break;
                case SF_WHEEL: settings_frames_event(WINDOW_EVENT_WHEEL, 0, s->a, 0); break;
                case SF_ANSWER:
                        if (settings.queued)
                        {
                                int changed = settings.queue[0].row;

                                string_copy_bounded((string_address)settings.value[changed], (string_address)settings.queue[0].value, SETTINGS_VALUE);
                                settings_turning(changed, false, false); // the answer is the verb run and its row read back (settings_complete's read)
                                settings_frames.asked++;
                                settings.queued--;
                                memory_copy(settings.queue, settings.queue + 1, settings.queued * sizeof(settings.queue[0]));
                                settings_say((string_address) "done");
                                settings_reading_changed(changed);
                        }
                        break;
                case SF_THEME:
                        ui_kit_theme((unsigned int)s->a, (unsigned int)s->b ? (unsigned int)s->b : UI_ACCENT_STANDARD, 0);
                        ui_kit_inks(&settings.ui);
                        ui_page_redraw(&settings.page);
                        break;
                case SF_SIZE:
                        ui_kit_settle();
                        settings_size((unsigned int)(s->a * settings.ui.scale / 100), (unsigned int)(s->b * settings.ui.scale / 100));
                        break;
                // A finger: it lands on the named row (its touch moves first, so the page knows where it is), moves a
                // logical pixels from where it landed (negative up), and lifts.
                case SF_CARD:
                {
                        static const char card[] = "[Moonwater] wifi on\n[Moonwater] * home-net      -48 dBm ###. WPA2-PSK 5GHz 36\n"
                                                   "[Moonwater]   cafe          -61 dBm ##.. open 2.4GHz 6\n"
                                                   "[Moonwater] + neighbour     -77 dBm #... WPA3-SAE 2.4GHz 1\n[Moonwater]   office\n"
                                                   "[Moonwater]   hall          -80 dBm #... open 2.4GHz 11\n[Moonwater]   garden      -84 dBm ...  WPA2 2.4GHz 3\n"
                                                   "[Moonwater]   loft          -88 dBm ...  WPA2 5GHz 40\n[Moonwater]   shed          -90 dBm ...  open 2.4GHz 9\n"
                                                   "[Moonwater]   annex         -91 dBm ...  WPA2 2.4GHz 2\n[Moonwater]   barn          -93 dBm ...  WPA2 2.4GHz 7\n"
                                                   "[Moonwater]   tower         -95 dBm ...  WPA2 5GHz 44\n[Moonwater]   kiln         -96 dBm ...  open 2.4GHz 5\n"
                                                   "[Moonwater]   mill          -97 dBm ...  WPA2 2.4GHz 8\n[Moonwater]   forge        -98 dBm ...  WPA2 5GHz 48\n";

                        static const char entry[] = "[Moonwater]   network    -60 dBm ##.. open 2.4GHz 6\n";
                        positive length = 0;

                        memory_copy(settings_large->said, card, sizeof(card) - 1);
                        length = sizeof(card) - 1;
                        for (int at = 0; at < 40 && length + sizeof(entry) < sizeof(settings_large->said); at++)
                                memory_copy(settings_large->said + length, entry, sizeof(entry) - 1), length += sizeof(entry) - 1;
                        settings.runner.length = length;
                        settings_card_open("Scan");
                        break;
                }
                case SF_FINGER_DOWN:
                        settings_frames_finger_y = row >= 0 ? y : settings.page.top + ui_dev(&settings.ui, 60);
                        settings_frames_event(WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, (settings.page.left + settings.page.right) / 2, y);
                        settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0);
                        break;
                case SF_FINGER_MOVE:
                        settings_frames_event(WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, (settings.page.left + settings.page.right) / 2,
                                              settings_frames_finger_y + s->a * settings.ui.scale / 100);
                        break;
                case SF_FINGER_UP: settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0); break;
                }
                settings_frames_pass();
        }
        settings_frames_until(settings_frames.now + 4000 * SETTINGS_FRAMES_MS);
        check("settings frames: the loop comes to rest after a script", !settings.page.dirty);
        for (int again = 0; again < 2 && !settings_frames.wrong; again++)
        {
                settings.page.dirty = true;
                settings_frames_until(settings_frames.now + SETTINGS_FRAMES_PERIOD);
                settings_frames.wrong = !settings_frames_same(settings.page.buffers.shown, (string_address)(again ? "at rest, the first again" : "at rest, the other"));
        }
        check("settings frames: every buffer a script shows is the whole picture drawn at its time", !settings_frames.wrong);
        string_format(log, "  settings frames (%s): %p frames, %p of them moving, %p changes answered\n", name,
                      settings_frames.frames - frames, settings_frames.moving - moving, settings_frames.asked - asked);
        // The pointer leaves, and nothing is held down.
        settings_frames_event(WINDOW_EVENT_MOTION, 0, -100, -100);
        settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
        settings.runner.child = 0;
        settings.queued = 0;
}

/*
        The gallery (moonwater settings -g) drawn by the same loop: every control the hand works pressed at its middle
        and let go (a slider dragged on the way), the page scrolled while they move, and every buffer shown the whole
        picture at its time. The spinners and the running bar never rest, so neither does the loop.
*/
static fn settings_frames_gallery(void)
{
        positive start;
        int pressed = 0;

        settings_frames.scenario = (string_address) "the gallery";
        settings_frames.wrong = false;
        gallery.on = 1;
        gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
        settings_size(GALLERY_W, GALLERY_H);
        settings_frames_until(settings_frames.now + 500 * SETTINGS_FRAMES_MS);
        start = settings_frames.now;
        for (int at = 0; at < gallery.count && !settings_frames.wrong; at++)
        {
                const struct ui_control address_to c = &gallery.control[at];
                int kind = c->kind, x = c->x + c->w / 2, y = c->y + (kind == UI_RADIOS ? UI_RADIO_PITCH * 2 : c->h) / 2;

                if (kind != UI_BUTTON && kind != UI_SWITCH && kind != UI_CHECKBOX && kind != UI_RADIOS && kind != UI_SLIDER && kind != UI_TABS)
                        continue;
                settings_frames_until(start + (positive)pressed * 300 * SETTINGS_FRAMES_MS);
                settings_frames_event(WINDOW_EVENT_MOTION, 0, x, y);
                settings_frames_pass();
                settings_frames_until(settings_frames.now + 20 * SETTINGS_FRAMES_MS);
                settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0);
                settings_frames_pass();
                if (kind == UI_SLIDER)
                        for (int step = 1; step <= 6; step++)
                        {
                                settings_frames_until(settings_frames.now + 16 * SETTINGS_FRAMES_MS);
                                settings_frames_event(WINDOW_EVENT_MOTION, 0, x - 60 + step * 25, y + 3);
                                settings_frames_pass();
                        }
                settings_frames_until(settings_frames.now + 90 * SETTINGS_FRAMES_MS);
                settings_frames_event(WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0);
                settings_frames_pass();
                // Every third, the page scrolled a notch while what was let go still moves, and back.
                if (pressed % 3 == 2)
                {
                        settings_frames_until(settings_frames.now + 40 * SETTINGS_FRAMES_MS);
                        settings_frames_event(WINDOW_EVENT_WHEEL, 0, -120, 0);
                        settings_frames_pass();
                        settings_frames_until(settings_frames.now + 40 * SETTINGS_FRAMES_MS);
                        settings_frames_event(WINDOW_EVENT_WHEEL, 0, 120, 0);
                        settings_frames_pass();
                }
                pressed++;
        }
        settings_frames_until(settings_frames.now + 600 * SETTINGS_FRAMES_MS);
        string_format(log, "  settings frames (the gallery): %p controls pressed\n", (positive)pressed);
        check("settings frames: every buffer the gallery shows is the whole picture drawn at its time", !settings_frames.wrong && pressed > 10);
        settings_frames_event(WINDOW_EVENT_MOTION, 0, -100, -100);
        gallery.on = 0;
        settings_size(SETTINGS_WIDTH * settings.ui.scale / 100, SETTINGS_HEIGHT * settings.ui.scale / 100);
        settings_frames_until(settings_frames.now + 200 * SETTINGS_FRAMES_MS);
}

/*
        The list's scroll as the wheel turns it down a notch at a time, frame by frame, to its last row: the drawn scroll never
        passes the last row, the aim never passes it, and once it is there it does not come back. Then it settles there.
*/
struct settings_end_probe
{
        int peak, returned, reached;
        b64 peak_at;
};

static fn settings_end_look(struct settings_end_probe address_to p, int most)
{
        const struct ui_scroll address_to s = &settings.list.scroll;
        b64 over = scroll_at(s) - (b64)most * UI_SCROLL_SCALE;

        p->peak_at = over > p->peak_at ? over : p->peak_at;
        p->peak = s->scroll - most > p->peak ? s->scroll - most : p->peak;
        if (s->scroll == most)
                p->reached = 1;
        else if (p->reached && s->scroll < most)
                p->returned++;
}

// The list of the group with the fewest rows, at every window height from 200 to 900: where its rows fit the view, it does not
// scroll and shows no bar. The bottom pad under the last row is room for the last row when the rows overflow, not a scroll
// of a list that fits.
static fn settings_fit_check(void)
{
        int group = 0, fewest = 1 << 30, thumb_top = 0, thumb_length = 0, seen = 0;
        b32 still = true;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        settings.list.hover = settings.list.down = -1;
        for (int g = 0; g < settings_groups(); g++)
        {
                settings.page.chosen = g;
                settings_refilter();
                if (settings.list.count > 0 && settings.list.count < fewest)
                        fewest = settings.list.count, group = g;
        }
        settings.page.chosen = group;
        settings_refilter();
        for (int height = 200; height <= 900; height += 8)
        {
                settings_size(SETTINGS_WIDTH, (unsigned int)height);
                if (settings.list.content <= settings.list.scroll.view)
                {
                        seen++;
                        still = still && ui_scroll_most(&settings.list.scroll) == 0 && !ui_scroll_thumb(&settings.ui, &settings.list.scroll, &thumb_top, &thumb_length);
                }
        }
        string_format(log, "  settings fit: group %p, %p rows, fitting at %p window heights\n", (positive)group, (positive)fewest, (positive)seen);
        check("settings list: a list whose rows fit its window does not scroll and shows no bar, at any window height", seen > 0 && still);
}

// The group column's view ends where the page's body does: the window's bottom edge, with the rows that fit above it. The
// column ended at the status band's top while the body ran to the edge, so 40 pixels of scroll range sat under the last group.
static fn settings_column_fit_check(void)
{
        struct ui_scroll address_to col = &settings.page.column_scroll;
        int heights[2] = {520, 560};
        b32 ends = true;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.ui.scale = 100;
        for (int k = 0; k < 2; k++)
        {
                settings_size(SETTINGS_WIDTH, (unsigned int)heights[k]);
                ends = ends && settings.page.bottom == settings.height && col->y + col->view == settings.page.bottom;
        }
        check("settings column: the group column's view ends at the window's bottom edge, where the body does, at 520 and 560", ends);
}

/*
        Choosing a group while the list is moving (a glide, or a spring it had from a pull at its end) puts the list at its top
        and stops it there: the jump is the scroll's, so the glide's aim and position are cleared with the scroll. A write to the
        scroll alone left the glide running, and its next frame put the list back where it had been.
*/
static fn settings_group_glide_check(void)
{
        struct ui_scroll address_to list = &settings.list.scroll;
        int other;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.ui.scale = 100;
        settings_size(SETTINGS_WIDTH, 300);
        other = settings.page.chosen == 0 ? 1 : 0;
        ui_scroll_set(list, 50);
        ui_body_notch(&list->body, (b64)150 << UI_Q, ui_kit_time());
        settings_group_choose(other);
        check("settings groups: choosing a group while the list glides puts the list at its top and stops the glide",
              list->scroll == 0 && scroll_at(list) == 0 && scroll_aim(list) == 0 && !scroll_gliding(list));
}

/*
        The band's drawn share past an end, 256ths of a device pixel: how far a bounded region's content is drawn past the end it is
        pulled past (edge -1 the top, 1 the bottom), and 0 inside its ends. It is the drawn position's distance past the end, the
        rubber band's (scroll_shown), so a pull of the list moves its rows by this and nothing else.
*/
static b64 band_past(const struct ui_scroll address_to s, int address_to edge)
{
    b64 most = scroll_most256(s), shown = scroll_shown(s);

    *edge = 0;
    if (shown < 0)
        return *edge = -1, -shown;
    if (shown > most)
        return *edge = 1, shown - most;
    return 0;
}

// The band's share as whole device pixels, signed: negative past the top, positive past the bottom (the content's drawn move).
static int band_px(const struct ui_scroll address_to s)
{
        int edge = 0;
        int px = (int)((band_past(s, &edge) + 128) >> 8);

        return edge < 0 ? -px : px;
}

// The most the band draws a bounded region past an end: a view's band, the hand's own reach (UI_SCROLL_VIEW_PAST views), in device pixels.
static int band_view_px(const struct ui_scroll address_to s)
{
    b64 most = scroll_most256(s), reach = (b64)(s->view > 0 ? s->view : 0) * UI_SCROLL_SCALE;

    return (int)((scroll_band256(s, most + reach) - most + 128) >> 8);
}

// The group column (the settings' tabs on end) takes a pull at its ends as a list does: a wheel turn at its bottom moves it past that
// edge by the band, a finger pulled past its top or bottom moves it by the band from that edge, and its items are drawn at its scroll.
static fn settings_column_check(void)
{
        struct ui_scroll address_to col = &settings.page.column_scroll;
        int most, edge = 0, x, y;
        b32 wheel_ok = false, pull_bottom_ok = false, pull_top_ok = false, items_ok = false;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        settings.ui.scale = 100;
        ui_kit_settle();
        settings_size(SETTINGS_WIDTH, 300);
        ui_kit.now += 1000000000ll, ui_kit.busy = 0;
        most = ui_scroll_most(col);
        x = ui_dev(&settings.ui, UI_PAD) + 10, y = settings.page.top + 30;
        ui_kit.pointer_x = x, ui_kit.pointer_y = y;
        ui_scroll_set(col, most);
        {
                struct window_event turn = {WINDOW_EVENT_WHEEL, 0, -120, 0};

                ui_page_wheel(&settings.ui, &settings.page, &turn);
                // The notch's target is followed a frame at a time (the body's notch), as the kit draws it: eight frames on.
                for (int frame = 0; frame < 8; frame++)
                {
                        ui_kit.now += 16666667, ui_kit.busy = 0;
                        ui_scroll_frame(&settings.ui, col);
                }
        }
        wheel_ok = band_past(col, &edge) > 0 && edge > 0 && col->scroll > most;
        ui_scroll_set(col, most);
        ui_scroll_drag(col, 150);
        pull_bottom_ok = band_past(col, &edge) > 0 && edge > 0 && col->scroll - most == band_px(col);
        items_ok = col->scroll - most == band_px(col) && col->scroll > most;
        ui_scroll_set(col, 0);
        ui_scroll_drag(col, -150);
        pull_top_ok = band_past(col, &edge) > 0 && edge < 0 && col->scroll == band_px(col) && col->scroll < 0;
        items_ok = items_ok && col->scroll < 0;
        ui_scroll_let_go(col, 0);
        ui_scroll_set(col, 0);
        check("settings column: a wheel turned at its bottom moves the groups by the band from that edge, as a list's does", wheel_ok);
        check("settings column: a finger pulled past its bottom or top moves the groups by the band from that edge, and its items are drawn at its scroll",
              pull_bottom_ok && pull_top_ok && items_ok);
}

// The settings list's scroll ends at its rows: its maximum is the rows minus the view, to the pixel, however it is reached (a wheel
// turned down past the end, the bar paged down to it, a finger pulled far past it and let go), and the pad is clear space beneath
// the list, between its bottom edge and the window's, not scroll distance.
static fn settings_end_check(void)
{
        int group = 0, best = -1, expected = 0, track_top = 0, track_height = 0, last = 0;
        b32 wheel_ok = true, bar_ok = true, finger_ok = true, clear_ok = true, rows_ok = true;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        settings.list.hover = settings.list.down = -1;
        for (int g = 0; g < settings_groups(); g++)
        {
                settings.page.chosen = g;
                settings_refilter();
                if (settings.list.count > best)
                        best = settings.list.count, group = g;
        }
        settings.page.chosen = group;
        settings_refilter();
        last = settings.list.count - 1;
        for (int scale = 1; scale <= 2; scale++)
        {
                int x, y, bar_x, bar_y;
                b32 pulled;

                settings.ui.scale = scale * 100;
                ui_kit_settle();
                settings_size(SETTINGS_WIDTH * scale, 300 * scale);
                expected = settings.list.content - settings.list.scroll.view;
                rows_ok = rows_ok && expected > 0 && ui_scroll_most(&settings.list.scroll) == expected && settings.list.scroll.content == settings.list.content;
                ui_scroll_track(&settings.ui, &settings.list.scroll, &track_top, &track_height);
                clear_ok = clear_ok && settings.height == settings.list.y + settings.list.scroll.view && track_top == settings.list.y &&
                           track_top + track_height == settings.height &&
                           settings.list.top[last] + ui_dev(&settings.ui, UI_ROW) - expected <= settings.list.scroll.view;
                x = settings.list.x + settings.list.w / 2, y = settings.list.y + settings.list.scroll.view / 2;

                // The wheel: notches down, a frame between each, until it settles past the end.
                ui_scroll_set(settings.page.body, 0);
                settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
                settings_frames_event(WINDOW_EVENT_MOTION, 0, x, y);
                for (int k = 0; k < 400; k++)
                {
                        settings_frames_event(WINDOW_EVENT_WHEEL, 0, -120, 0);
                        settings_frames_until(settings_frames.now + 16 * SETTINGS_FRAMES_MS);
                }
                settings_frames_until(settings_frames.now + 3000 * SETTINGS_FRAMES_MS);
                wheel_ok = wheel_ok && settings.list.scroll.scroll == expected && !scroll_gliding(&settings.list.scroll) && !scroll_back(&settings.list.scroll);

                // The bar: presses on its track below the thumb, each pages a view, until it reaches the end.
                ui_scroll_set(settings.page.body, 0);
                settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
                ui_scroll_track(&settings.ui, &settings.list.scroll, &track_top, &track_height);
                bar_x = settings.list.scroll.x + UI_SCROLL_WIDTH * scale / 2;
                bar_y = track_top + track_height - 2 * scale;
                for (int k = 0; k < 100; k++)
                {
                        settings_frames_event(WINDOW_EVENT_MOTION, 0, bar_x, bar_y);
                        settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0);
                        settings_frames_event(WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0);
                        settings_frames_until(settings_frames.now + 500 * SETTINGS_FRAMES_MS);
                }
                settings_frames_until(settings_frames.now + 3000 * SETTINGS_FRAMES_MS);
                // The presses page down until the thumb is at the end: the last page may stop short of the pixel, the thumb under the press.
                bar_ok = bar_ok && settings.list.scroll.scroll >= expected - settings.list.scroll.view / 4 && settings.list.scroll.scroll <= expected &&
                         !settings.list.scroll.held;

                // A finger: down on the list, dragged up past the end in steps, and let go; the list settles at the end. The pull is
                // ten views long (60 steps of 40 px), so the list is put eight views above its end first: the pull ends two views
                // past the end however many rows the longest group has, and the row says so (pulled) before it looks where it rests.
                ui_scroll_set(settings.page.body, expected > 8 * settings.list.scroll.view ? expected - 8 * settings.list.scroll.view : 0);
                settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
                settings_frames_event(WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y);
                settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0);
                for (int k = 0; k < 60; k++)
                {
                        y -= 40 * scale;
                        settings_frames_event(WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y);
                        settings_frames_until(settings_frames.now + 16 * SETTINGS_FRAMES_MS);
                }
                pulled = scroll_at(&settings.list.scroll) > (b64)expected * 256;
                settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0);
                settings_frames_until(settings_frames.now + 3000 * SETTINGS_FRAMES_MS);
                finger_ok = finger_ok && pulled && settings.list.scroll.scroll == expected && !scroll_back(&settings.list.scroll);
                string_format(log, "  settings end (scale %p): rows %p, view %p, maximum %p, wheel and bar at %p\n", (positive)scale,
                              (positive)settings.list.content, (positive)settings.list.scroll.view, (positive)expected, (positive)settings.list.scroll.scroll);
        }
        string_format(log, "  settings end: rows %b, wheel %b, bar %b, finger %b\n", (bipolar)rows_ok, (bipolar)wheel_ok, (bipolar)bar_ok, (bipolar)finger_ok);
        check("settings list: the rows are the scroll's content, and its maximum is the rows minus the view to the pixel", rows_ok);
        check("settings list: a wheel turned down to the end, the bar paged to it and a finger pulled past it and let go each stop at that maximum",
              wheel_ok && bar_ok && finger_ok);
        check("settings list: the view runs to the window's bottom edge, its scroll track has the view's ends, and the last row ends inside the view at the end", clear_ok);
}

static fn settings_wheel_end_check(string_address name, positive gap, int notches)
{
        struct settings_end_probe p = {0, 0, 0, -(1ll << 40)};
        int most, group = 0, best = -1;
        b32 settled;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        settings.list.hover = settings.list.down = -1;
        for (int g = 0; g < settings_groups(); g++)
        {
                settings.page.chosen = g;
                settings_refilter();
                if (settings.list.count > best)
                        best = settings.list.count, group = g;
        }
        settings.page.chosen = group;
        settings_refilter();
        ui_scroll_set(settings.page.body, 0);
        settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
        most = ui_scroll_most(&settings.list.scroll);
        settings_frames_event(WINDOW_EVENT_MOTION, 0, (settings.page.left + settings.page.right) / 2, (settings.page.top + settings.page.bottom) / 2);
        for (int k = 0; k < notches; k++)
        {
                settings_frames_event(WINDOW_EVENT_WHEEL, 0, -120, 0);
                settings_frames_pass();
                for (positive t = 0; t < gap; t += 16)
                {
                        settings_frames_until(settings_frames.now + 16 * SETTINGS_FRAMES_MS);
                        settings_end_look(&p, most);
                }
        }
        settings_frames_until(settings_frames.now + 3000 * SETTINGS_FRAMES_MS);
        settings_end_look(&p, most);
        settled = !scroll_gliding(&settings.list.scroll) && !scroll_back(&settings.list.scroll) && settings.list.scroll.scroll == most;
        string_format(log, "  settings wheel end (%s): %p rows, last row at %p, scroll %p, drawn past it by %b, at past it by %b/256, back %p frames\n",
                      name, (positive)settings.list.count, (positive)most, (positive)settings.list.scroll.scroll, (bipolar)p.peak,
                      (bipolar)p.peak_at, (positive)p.returned);
        // The band's asymptote is a view past the end: a pull of any number of notches draws no more than that, and springs home once.
        check("settings wheel end: a wheel turned down to the last row draws the list past it by a band no more than a view's, and it does not come back up",
              p.peak > 0 && p.peak <= settings.list.scroll.view && p.returned == 0);
        check("settings wheel end: it settles on the last row with nothing moving", settled);
}

// A finger flicks the list down: it lands at the list's middle, moves up the rows by step pixels a frame for steps frames
// from start pixels above the end, and lets go. The drawn scroll is watched to the end of the glide.


static b32 settings_flick_check(string_address name, int start, int step, int steps)
{
        struct settings_end_probe p = {0, 0, 0, -(1ll << 40)};
        int most, group = 0, best = -1, x, y, turned = 0;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.pane = 1, settings.queued = 0, settings.runner.child = 1, settings.job = 0, settings.job_sets = false, ui_confirm_drop(&settings.confirm);
        settings.list.hover = settings.list.down = -1;
        for (int g = 0; g < settings_groups(); g++)
        {
                settings.page.chosen = g;
                settings_refilter();
                if (settings.list.count > best)
                        best = settings.list.count, group = g;
        }
        settings.page.chosen = group;
        settings_refilter();
        ui_scroll_set(settings.page.body, 0);
        settings_frames_until(settings_frames.now + 1000 * SETTINGS_FRAMES_MS);
        most = ui_scroll_most(&settings.list.scroll);
        ui_scroll_set(settings.page.body, most - start);
        settings_frames_until(settings_frames.now + 100 * SETTINGS_FRAMES_MS);
        x = (settings.page.left + settings.page.right) / 2, y = (settings.page.top + settings.page.bottom) / 2;
        settings_frames_event(WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y);
        settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0);
        settings_frames_pass();
        for (int k = 0; k < steps; k++)
        {
                y -= step;
                settings_frames_until(settings_frames.now + 16 * SETTINGS_FRAMES_MS);
                settings_frames_event(WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, x, y);
                settings_end_look(&p, most);
        }
        // Let go: from here the list may only move toward the end it is going to, and must rest there. The drag's own give past
        // the end (a finger holding it there) is the pull's, and is not counted.
        p.returned = p.reached = 0;
        turned = 0;
        settings_frames_event(WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH, UI_POINTER_LEFT, 0);
        // A throw falls by e in 500 ms (UI_FLING_TAU_NS), so its last half pixel takes seconds: the settle is watched for 6 s.
        for (positive t = 0; t < 6000 * SETTINGS_FRAMES_MS; t += 16 * SETTINGS_FRAMES_MS)
        {
                int before = settings.list.scroll.scroll;

                settings_frames_until(settings_frames.now + 16 * SETTINGS_FRAMES_MS);
                settings_end_look(&p, most);
                // Once the carry has turned toward the end (the spring home from its overshoot, or the coast into it), a move away from it is a return.
                if ((before < most && settings.list.scroll.scroll > before) || (before > most && settings.list.scroll.scroll < before))
                        turned = 1;
                else if (turned && ((before < most && settings.list.scroll.scroll < before - 2) || (before > most && settings.list.scroll.scroll > before + 2)))
                        p.returned++; // two pixels: a frame's rounding of the drawn place is not a return
        }
        string_format(log, "  settings flick (%s): %p rows, last row at %p, drawn past it by %b at most, moved back %p frames, rest at %p\n",
                      name, (positive)settings.list.count, (positive)most, (bipolar)p.peak, (positive)p.returned,
                      (positive)settings.list.scroll.scroll);
        // A flick is let go with its speed: the body carries 0.7 of it for 260 ms, so a flick that carries past the last row draws the band
        // there (a view's, at most) and springs home to it; one that stops short of it is carried to it or stops short, with no return.
        return p.peak <= settings.list.scroll.view && p.returned == 0 && settings.list.scroll.scroll == most && !scroll_back(&settings.list.scroll);
}

/*
        The Display section (settings.c): the map's view and tiles, the snap band the compositor draws (COMPOSITOR_SNAP,
        in device pixels) for a drop, the dropdowns' words and what each lists for a fake table, and the section's rows: the
        map first, no typed row, and no window of its own.
*/
// A row has count choices, the first two being want0 and want1.
static b32 check_display_section_words(int row, const char address_to want0, const char address_to want1, positive count)
{
        char word[SETTINGS_VALUE];

        if (row < 0 || settings_options(&settings_rows[row]) != count)
                return false;
        return settings_choice(&settings_rows[row], 0, word, sizeof(word)) && string_equals((string_address)word, (string_address)want0) &&
               settings_choice(&settings_rows[row], 1, word, sizeof(word)) && string_equals((string_address)word, (string_address)want1);
}

// The map placed as the page's frame places it (settings_map_above), in a window 700 wide under a band 100 device pixels high: where
// the rows of the display section have its panel.
static fn display_map_place(void)
{
        ui_view_begin(settings.page.view, &settings.ui, 700, 0);
        ui_host(null, .h = 100 * 100 / settings.ui.scale);
        settings_map_above();
        ui_view_end(settings.page.view);
}

// The map's screens as the window has them now (its nodes, its graph in the panel, a camera on them), and where screen k is drawn.
static fn display_map_screen(int k, int address_to x, int address_to y, int address_to w, int address_to h)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        int x1, y1;

        settings_map_load(number);
        settings_map_frame();
        if (!settings.map.graph.homed)
                ui_graph_home(&settings.ui, &settings.map.graph);
        ui_graph_box(&settings.map.graph, k, settings.map.node[k].x, settings.map.node[k].y, x, y, &x1, &y1);
        *w = x1 - *x, *h = y1 - *y;
}

static fn check_display_section(void)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        struct windows address_to saved = settings.own.table;
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        int screen_row, turn_row, refresh_row, modes_row;
        char text[SETTINGS_VALUE];
        b32 typed = false, window = false;

        // The dropdowns, from a fake table: three screens, the second at the left (number 1), with 144 Hz first (recommended).
        windows_scratch_zero(table_at);
        table_at->outputs = 3, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 100;
        table_at->output_x[0] = 1920, table_at->output_y[0] = 0, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
        table_at->output_x[1] = 0, table_at->output_y[1] = 0, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
        table_at->output_x[2] = 3840, table_at->output_y[2] = 0, table_at->output_w[2] = 1280, table_at->output_h[2] = 720;
        memory_copy(table_at->output_name[0], "DP-2", 5), memory_copy(table_at->output_name[1], "DP-1", 5), memory_copy(table_at->output_name[2], "HDMI-A-1", 9);
        table_at->output_rates[1] = 3, table_at->output_hz[1][0] = 144, table_at->output_hz[1][1] = 75, table_at->output_hz[1][2] = 60, table_at->output_now[1] = 75;
        table_at->output_turn[1] = 90, table_at->output_software[1] = 1;
        settings.own.table = table_at;
        settings.map.display = -1;
        check("display section: the screens are numbered left to right, the one at the left is Screen 1",
              windows_screen_numbers(table_at, number) == 3 && number[1] == 1 && number[0] == 2 && number[2] == 3);

        screen_row = settings_output_row(SETTINGS_OUT_SCREEN);
        turn_row = settings_output_row(SETTINGS_OUT_TURN);
        refresh_row = settings_output_row(SETTINGS_OUT_REFRESH);
        modes_row = -1;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (string_equals((string_address)settings_rows[at].label, (string_address) "Screen modes"))
                        modes_row = (int)at;
        check("display section: every Display row exists once: screen, rotation, refresh and modes",
              screen_row >= 0 && turn_row >= 0 && refresh_row >= 0 && modes_row >= 0);

        // The screen dropdown lists the screens by number; the rotation has exactly the four plain choices.
        check("display section: the Screen dropdown lists the three screens in number order",
              settings_options(&settings_rows[screen_row]) == 3 && settings_choice(&settings_rows[screen_row], 0, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "Screen 1: DP-1"));
        check("display section: the rotation dropdown has exactly the four plain choices, Normal to Turned left",
              check_display_section_words(turn_row, "Normal", "Turned right", 4) && settings_choice(&settings_rows[turn_row], 2, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "Upside down") && settings_choice(&settings_rows[turn_row], 3, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "Turned left"));
        check("display section: a screen turned by Moonwater says so under its rotation",
              settings_map_chosen() == 1 && table_at->output_software[1] == 1);
        // The map's nodes are the table's screens as the desk has them (a turned screen's rectangle is already turned there), each with
        // its number in the middle, its name and its size under it, and the primary (the table's first) marked with its size.
        {
                unsigned int count = settings_map_load(number);

                check("display section: the map's screens are the table's rectangles, numbered as the dropdown numbers them",
                      count == 3 && settings.map.graph.nodes == 3 && settings.map.node[1].x == 0 && settings.map.node[1].w == 1920 &&
                          settings.map.node[1].h == 1080 && settings.map.node[2].x == 3840 && settings.map.node[2].w == 1280 &&
                          string_equals((string_address)settings.map.node[1].label, (string_address) "1") &&
                          string_equals((string_address)settings.map.node[2].line[0], (string_address) "HDMI-A-1"));
                check("display section: a screen says its size under its name, and the primary is marked and says so",
                      (settings.map.node[0].flags & UI_GRAPH_MARKED) && !(settings.map.node[2].flags & UI_GRAPH_MARKED) &&
                          string_equals((string_address)settings.map.node[2].line[1], (string_address) "1280 \xc3\x97 720") &&
                          string_equals((string_address)settings.map.node[0].line[1], (string_address) "1920 \xc3\x97 1080, primary"));
        }

        // The refresh dropdown lists only the rates the screen lists, the first marked recommended, and shows the one it is at.
        check("display section: the refresh dropdown lists only the screen's three rates, the first recommended",
              settings_options(&settings_rows[refresh_row]) == 3 && settings_choice(&settings_rows[refresh_row], 0, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "144 Hz (recommended)") && settings_choice(&settings_rows[refresh_row], 1, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "75 Hz") && !settings_choice(&settings_rows[refresh_row], 3, text, sizeof(text)));
        check("display section: the refresh dropdown shows the rate the screen is at", string_equals((string_address)settings_shown_value(refresh_row), (string_address) "75 Hz"));

        // The Scale dropdown: six presets in plain words, the one a 1920 screen suggests (100%) marked, and the row showing the scale
        // its screen's line asks. A screen's native width suggests 150% at 4K and 200% at 5K or 6K.
        {
                int scale_row = -1;
                unsigned int width;

                for (positive at = 0; at < SETTINGS_ROWS; at++)
                        if (settings_rows[at].output == SETTINGS_OUT_SCALE)
                                scale_row = (int)at;
                check("display section: the Scale row is one row of the Display section", scale_row >= 0 && settings_rows[scale_row].section == null &&
                      settings_output_row(SETTINGS_OUT_SCALE) == scale_row);
                check("display section: the Scale dropdown lists six presets in percent, the 100% marked as a 1920 screen suggests it",
                      settings_options(&settings_rows[scale_row]) == 6 && settings_choice(&settings_rows[scale_row], 0, text, sizeof(text)) &&
                          string_equals((string_address)text, (string_address) "50%") &&
                          settings_choice(&settings_rows[scale_row], 2, text, sizeof(text)) &&
                          string_equals((string_address)text, (string_address) "100% (recommended)") &&
                          settings_choice(&settings_rows[scale_row], 5, text, sizeof(text)) && string_equals((string_address)text, (string_address) "200%") &&
                          !settings_choice(&settings_rows[scale_row], 6, text, sizeof(text)));
                table_at->output_asked_scale[1] = 150;
                check("display section: the Scale row shows the scale its screen's line asks, in percent",
                      string_equals((string_address)settings_shown_value(scale_row), (string_address) "150%"));
                check("display section: the choice that is the screen's scale is found by its label", settings_choice_index(scale_row, "150%") == 4 &&
                      settings_choice_index(scale_row, "75%") == 1 && settings_choice_index(scale_row, "60%") == -1);
                table_at->output_asked_scale[1] = 100;
                width = table_at->output_w[1];
                table_at->output_w[1] = 3840;
                check("display section: a 4K screen suggests 150%, marked in the dropdown", settings_scale_suggested(table_at, 1) == 150 &&
                      settings_choice(&settings_rows[scale_row], 4, text, sizeof(text)) && string_equals((string_address)text, (string_address) "150% (recommended)"));
                table_at->output_w[1] = 5120;
                check("display section: a 5K screen suggests 200%", settings_scale_suggested(table_at, 1) == 200);
                table_at->output_w[1] = width;
        }
        memory_copy(table_at->output_model[1], "Dell U2720Q", 12);
        check("display section: a screen whose EDID names it shows the name, its connector in brackets after it",
              string_equals((string_address)settings_shown_value(screen_row), (string_address) "Screen 1: Dell U2720Q [DP-1]"));
        memory_zero(table_at->output_model[1], sizeof(table_at->output_model[1]));

        // What the compositor did not do is said on the row it was asked of: a rate the screen does not offer, a turn it could
        // not do, and a place the file does not read (the Screen row). Nothing refused reads plain.
        check("display section: a row whose setting was not refused reads plain: the rate it is at", string_equals((string_address)settings_row_text(refresh_row, 1), (string_address) "75 Hz"));
        table_at->output_refused[1] = WINDOWS_REFUSED_RATE, table_at->output_asked_hz[1] = 144;
        check("display section: a refresh rate the screen does not offer is said on its row, with the rate it is kept at",
              string_equals((string_address)settings_row_text(refresh_row, 1), (string_address) "75 Hz (144 Hz is not offered)"));
        table_at->output_refused[1] = WINDOWS_REFUSED_TURN, table_at->output_asked_turn[1] = 90, table_at->output_turn[1] = 0;
        check("display section: a turn the compositor could not do is said on the rotation row, with the turn it is at",
              string_equals((string_address)settings_row_text(turn_row, 1), (string_address) "Normal (Turned right could not be done)"));
        table_at->output_refused[1] = WINDOWS_REFUSED_PLACE;
        check("display section: a place the file does not read is said on the Screen row",
              string_equals((string_address)settings_row_text(screen_row, 1), (string_address) "Screen 1: DP-1 (its place in the file is not read)"));
        table_at->output_refused[1] = 0, table_at->output_turn[1] = 90;

        // The compositor's own answer for the table, from its state: a line asking 144 Hz and 90 degrees of a screen at 75 Hz, not
        // turned, is refused both; the same screen at 144 Hz and turned by software is refused neither; a bad y is a place refused.
        {
                static struct windows *made_scratch;
                struct windows *const made_at = windows_scratch_table(&made_scratch);

                memory_zero(compositor_display, sizeof(compositor_display));
                memory_copy(compositor_display[0].name, "DP-1", 5);
                compositor_display[0].refresh = 144, compositor_display[0].rotation = 90, compositor_display[0].scale = 150;
                compositor_displays = 1;
                compositor.output[0].mode.vrefresh = 75, compositor.output[0].turned = 0, compositor.output[0].degrees = 90;
                compositor_table_refusals(made_at, 0, "DP-1");
                check("display section: the compositor says a rate and a turn it did not do, with what each line asked",
                      made_at->output_refused[0] == (WINDOWS_REFUSED_RATE | WINDOWS_REFUSED_TURN) && made_at->output_asked_hz[0] == 144 &&
                          made_at->output_asked_turn[0] == 90);
                compositor.output[0].mode.vrefresh = 144, compositor.output[0].turned = 2;
                compositor_table_refusals(made_at, 0, "DP-1");
                check("display section: a rate it set and a turn it did in software are refused nothing",
                      made_at->output_refused[0] == 0);
                compositor_display[0].bad = WINDOWS_DISPLAY_Y;
                compositor_table_refusals(made_at, 0, "DP-1");
                check("display section: a place whose y the file does not read is refused as a place", made_at->output_refused[0] == WINDOWS_REFUSED_PLACE);
                compositor_table_refusals(made_at, 0, "HDMI-A-1");
                check("display section: a screen with no line asks for nothing and is refused nothing", made_at->output_refused[0] == 0 && made_at->output_asked_hz[0] == 0);
                check("display section: the table says the scale a screen's line asks, 100 when it has none", made_at->output_asked_scale[0] == 100);
                compositor_displays = 1;
                compositor_table_refusals(made_at, 0, "DP-1");
                check("display section: the table says the scale a screen's line asks, in percent", made_at->output_asked_scale[0] == 150);
                compositor_displays = 0;
                memory_zero(compositor_display, sizeof(compositor_display));
        }

        // The screen modes say plain words, and what they are read as is the verb's.
        check("display section: Screen modes says Biggest picture and Recommended, and the verb's words are kept",
              string_equals((string_address)settings_label_of(modes_row, "largest"), (string_address) "Biggest picture") &&
                  string_equals((string_address)settings_label_of(modes_row, "preferred"), (string_address) "Recommended"));

        // The section's rows: the map's Screen row first, no typed row among them, and the identify action is no row of the list.
        check("display section: the Screen row, which the map is for, starts the Display section", string_equals((string_address)settings_rows[screen_row].section, (string_address) "Display"));
        for (positive at = (positive)screen_row; at < SETTINGS_ROWS && (at == (positive)screen_row || !settings_rows[at].section); at++)
        {
                if (settings_rows[at].kind == SETTINGS_FIELD)
                        typed = true;
                if (settings_rows[at].usage && string_has_prefix((string_address)settings_rows[at].usage, "settings displays"))
                        window = true;
        }
        check("display section: no row of the section is a typed field", !typed);
        check("display section: no separate displays window: no row runs settings displays", !window);

        // The map is shown above the Display group only: its panel is placed at the page's top, the list under it.
        settings.page.chosen = settings_group_of(screen_row), settings_filter_clear(), settings.page.card = false;
        settings.page.top = 100, settings.page.left = 0, settings.page.right = 700;
        settings.ui.scale = 100;
        check("display section: the map is on for the Display group and off for another", settings_map_on() && settings.page.chosen == settings_group_of(screen_row));
        display_map_place();
        check("display section: the map's panel is placed at the top of the page", settings.map.panel.y == 100 && settings.map.panel.h > 0 && settings.map.panel.w > 0);
        // The Screen dropdown counts the screens in the same reading order as identify does: a 2 by 2 arrangement, listed bottom
        // right first, is Screen 1 top left, Screen 2 top right, Screen 3 bottom left and Screen 4 bottom right.
        windows_scratch_zero(table_at);
        table_at->outputs = 4, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 100;
        table_at->output_x[0] = 1920, table_at->output_y[0] = 1080, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
        table_at->output_x[1] = 0, table_at->output_y[1] = 1080, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
        table_at->output_x[2] = 1920, table_at->output_y[2] = 0, table_at->output_w[2] = 1920, table_at->output_h[2] = 1080;
        table_at->output_x[3] = 0, table_at->output_y[3] = 0, table_at->output_w[3] = 1920, table_at->output_h[3] = 1080;
        memory_copy(table_at->output_name[0], "DP-4", 5), memory_copy(table_at->output_name[1], "DP-3", 5);
        memory_copy(table_at->output_name[2], "DP-2", 5), memory_copy(table_at->output_name[3], "DP-1", 5);
        settings.map.display = -1;
        check("display section: the Screen dropdown of a 2 by 2 arrangement reads top left first, then top right, bottom left and bottom right",
              settings_options(&settings_rows[screen_row]) == 4 && settings_choice(&settings_rows[screen_row], 0, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "Screen 1: DP-1") &&
                  settings_choice(&settings_rows[screen_row], 1, text, sizeof(text)) && string_equals((string_address)text, (string_address) "Screen 2: DP-2") &&
                  settings_choice(&settings_rows[screen_row], 2, text, sizeof(text)) && string_equals((string_address)text, (string_address) "Screen 3: DP-3") &&
                  settings_choice(&settings_rows[screen_row], 3, text, sizeof(text)) && string_equals((string_address)text, (string_address) "Screen 4: DP-4"));
        check("display section: the map's numbers and the dropdown's are the same screens (Screen 2 is DP-2, top right)",
              windows_screen_numbers(table_at, number) == 4 && number[2] == 2 && number[1] == 3 && number[3] == 1 && number[0] == 4);

        // A finger on the map, on the bottom left screen (table index 1): the press chooses it and takes it, a move drags it, and no press
        // of the page starts (no row is pressed, and the finger is not the page's), so a drag never scrolls the list under it. Where it
        // would land is never over another screen, and the desk stays in one piece. The release is taken back (WINDOW_KEY_CANCEL): a
        // release would write the desk, which runs a verb.
        {
                struct window_event touch;
                int tx, ty, tw, th, nx, ny, nw, nh;
                b32 taken;
                bipolar child_before;

                gallery.on = 0, settings.page.menu_row = -1, ui_field_close(&settings.edit), (void)0, settings.page.card = false, settings_filter_clear();
                settings.map.display = -1, settings.map.keys = false, settings.list.down = -1, settings.map.sent = false;
                ui_graph_start(&settings.map.graph);
                display_map_screen(1, &tx, &ty, &tw, &th);
                check("display section: the map frames the four screens in its panel, under its buttons",
                      settings.map.graph.homed && tx >= settings.map.panel.x && ty >= settings.map.panel.y + settings.map.graph.top &&
                          ty + th <= settings.map.panel.y + settings.map.panel.h && tw > 20 && th > 10);
                touch = (struct window_event){WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, tx + tw / 2, ty + th / 2};
                settings_event_at(&touch);
                touch = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                taken = settings_event_at(&touch);
                check("display section: a finger's press on a screen of the map is the map's: it chooses the screen and takes it",
                      taken && settings.map.display == 1 && settings.map.graph.held == 1);
                check("display section: a finger's press on the map starts no press of the page, so a drag there never scrolls",
                      settings.list.down < 0);
                touch = (struct window_event){WINDOW_EVENT_MOTION, WINDOW_KEY_TOUCH, tx + tw / 2 + tw * 3, ty + th / 2};
                settings_event_at(&touch);
                check("display section: a finger dragged on the map moves the screen it took, never over another and against one, the desk in one piece",
                      settings.map.graph.moving && settings.map.graph.at_x != settings.map.node[1].x && settings.list.down < 0 &&
                          ui_graph_clear(&settings.map.graph, 1, settings.map.graph.at_x, settings.map.graph.at_y) &&
                          ui_graph_held(&settings.map.graph, 1, settings.map.graph.at_x, settings.map.graph.at_y));
                check("display section: while a screen is dragged the caption says to let it go", string_equals((string_address)settings_map_hint(4), (string_address) "Let go to place the screen"));
                child_before = settings.runner.child;
                touch = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_TOUCH | WINDOW_KEY_CANCEL, UI_POINTER_LEFT, 0};
                settings_event_at(&touch);
                check("display section: a drag taken back writes nothing", settings.map.graph.held < 0 && settings.runner.child == child_before && !settings.map.sent);

                // A mouse on the top right screen (table index 2): the same press chooses and takes it, and a press on a screen is no press
                // of the identify button (which would run the verb, a child process; none is started).
                display_map_screen(2, &tx, &ty, &tw, &th);
                touch = (struct window_event){WINDOW_EVENT_MOTION, 0, tx + tw / 2, ty + th / 2};
                settings_event_at(&touch);
                touch = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                taken = settings_event_at(&touch);
                check("display section: a mouse's press on a screen of the map chooses it and takes it, and runs no verb",
                      taken && settings.map.display == 2 && settings.map.graph.held == 2 && settings.runner.child == child_before);
                touch = (struct window_event){WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
                settings_event_at(&touch);
                check("display section: a press let go where it went down writes nothing", settings.map.graph.held < 0 && !settings.map.sent);

                // The ground: a press on the grid beside the screens pans the view, every screen with it, and the wheel zooms it about the
                // pointer; the button that shows every screen frames them again where they were.
                touch = (struct window_event){WINDOW_EVENT_MOTION, 0, settings.map.panel.x + 4, settings.map.panel.y + settings.map.panel.h - 4};
                settings_event_at(&touch);
                touch = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                settings_event_at(&touch);
                touch = (struct window_event){WINDOW_EVENT_MOTION, 0, settings.map.panel.x + 34, settings.map.panel.y + settings.map.panel.h - 14};
                taken = settings_event_at(&touch);
                display_map_screen(2, &nx, &ny, &nw, &nh);
                check("display section: a press on the grid pans the view, the screens with it", taken && settings.map.graph.panning == 1 &&
                      nx == tx + 30 && ny == ty - 10 && nw == tw && settings.map.graph.held < 0);
                touch = (struct window_event){WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
                settings_event_at(&touch);
                touch = (struct window_event){WINDOW_EVENT_WHEEL, 0, 120, 0};
                settings_event_at(&touch);
                display_map_screen(2, &nx, &ny, &nw, &nh);
                check("display section: the wheel over the map zooms it", nw > tw && !settings.map.graph.panning);
                {
                        const struct ui_control address_to fit = &settings.map.fit;
                        int bx = 0, by = 0, bw = 0, bh = 0;

                        ui_control_row(fit, &bx, &by, &bw, &bh);
                        touch = (struct window_event){WINDOW_EVENT_MOTION, 0, bx + bw / 2, by + bh / 2};
                        settings_event_at(&touch);
                        touch = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                        settings_event_at(&touch);
                        touch = (struct window_event){WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
                        settings_event_at(&touch);
                        display_map_screen(2, &nx, &ny, &nw, &nh);
                        check("display section: Show all screens frames every screen again", bw > 0 && nx == tx + 0 && nw == tw && ny == ty);
                }
                settings.map.display = -1;
        }

        // The desk written whole when a screen is let go: every screen's name and place in the table's order, the desk's top left at 0, 0
        // (the top left screen put to the left of the others moves them all right), and what the map shows meanwhile is that layout,
        // seen where it was: the camera moves with the desk's new top left, so the screens that did not move stay where they are.
        {
                char words[SETTINGS_TYPED];
                int tx, ty, tw, th, nx, ny, nw, nh;
                positive length;

                display_map_screen(0, &tx, &ty, &tw, &th);
                length = settings_map_layout(3, -1920, 1080, words, sizeof(words));
                check("display section: a screen let go writes every screen's place, in the table's order, the desk's top left at 0, 0",
                      length && string_equals((string_address)words, (string_address) "DP-4 place 3840 1080 DP-3 place 1920 1080 DP-2 place 3840 0 DP-1 place 0 1080") &&
                          settings.map.sent_x[3] == 0 && settings.map.sent_y[3] == 1080 && settings.map.sent_x[0] == 3840);
                // What settings_map_place keeps of it (the verb itself is not run here).
                settings.map.sent = true, settings.map.sent_at = (positive)ui_kit.now, settings.map.sent_count = 4;
                settings.map.shown_x[3] = -1920, settings.map.shown_y[3] = 1080;
                display_map_screen(0, &nx, &ny, &nw, &nh);
                check("display section: while the table has not caught up the map shows the layout sent, the screens that did not move where they were",
                      settings.map.sent && settings.map.node[0].x == 3840 && nx == tx && ny == ty && nw == tw);
                for (int k = 0; k < 4; k++)
                        table_at->output_x[k] = settings.map.sent_x[k], table_at->output_y[k] = settings.map.sent_y[k];
                display_map_screen(0, &nx, &ny, &nw, &nh);
                check("display section: once the table has the layout it is the table's, and nothing on the map moves", !settings.map.sent && nx == tx && ny == ty);
                // A command that moves the whole desk (every screen by as much) moves nothing on the map either.
                for (int k = 0; k < 4; k++)
                        table_at->output_x[k] -= 1920;
                display_map_screen(0, &nx, &ny, &nw, &nh);
                check("display section: a desk the table moves whole stays still on the map", nx == tx && ny == ty);
                table_at->output_x[0] = 1920, table_at->output_y[0] = 1080, table_at->output_x[1] = 0, table_at->output_y[1] = 1080;
                table_at->output_x[2] = 1920, table_at->output_y[2] = 0, table_at->output_x[3] = 0, table_at->output_y[3] = 0;
                display_map_screen(0, &nx, &ny, &nw, &nh);
        }

        // The empty states, read plainly: no screen, one screen (its rows are words, not menus), one rate, and no rate at all.
        {
                struct ui_row row;

                windows_scratch_zero(table_at);
                settings.map.display = -1;
                check("display section: with no screen the map's caption says so", string_equals((string_address)settings_map_hint(0), (string_address) "No screen is connected."));
                memory_zero(&row, sizeof(row));
                settings_describe_output(screen_row, &row);
                check("display section: with no screen the Screen row says so, in words", string_equals((string_address)row.value, (string_address) "No screen is connected"));
                table_at->outputs = 1, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 100;
                table_at->output_w[0] = 1920, table_at->output_h[0] = 1080, table_at->output_rates[0] = 1, table_at->output_hz[0][0] = 60, table_at->output_now[0] = 60;
                memory_copy(table_at->output_name[0], "DP-1", 5);
                check("display section: with one screen the map's caption says so", string_equals((string_address)settings_map_hint(1), (string_address) "Only one screen is connected."));
                memory_zero(&row, sizeof(row));
                settings_describe_output(screen_row, &row);
                check("display section: with one screen the Screen row is words, not a menu", row.kind == UI_ROW_PLAIN && string_equals((string_address)row.value, (string_address) "Screen 1: DP-1"));
                memory_zero(&row, sizeof(row));
                settings_describe_output(refresh_row, &row);
                check("display section: a screen with one rate shows it in words, not a menu", row.kind == UI_ROW_PLAIN && string_equals((string_address)row.value, (string_address) "60 Hz (recommended)"));
                table_at->output_rates[0] = 0;
                memory_zero(&row, sizeof(row));
                settings_describe_output(refresh_row, &row);
                check("display section: a screen that lists no rate says so, in words", string_equals((string_address)row.value, (string_address) "The screen lists no rates"));
        }

        // The map's drag at a HiDPI scale (the kit at 2, the table at 2): the press chooses the screen and a motion drags it, as
        // at scale 1. The panel and the screens are device pixels, the button's box logical.
        {
                struct window_event event;
                int tx, ty, tw, th, before_x, before_y;

                windows_scratch_zero(table_at);
                table_at->outputs = 4, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 200;
                table_at->output_x[0] = 1920, table_at->output_y[0] = 1080, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
                table_at->output_x[1] = 0, table_at->output_y[1] = 1080, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
                table_at->output_x[2] = 1920, table_at->output_y[2] = 0, table_at->output_w[2] = 1920, table_at->output_h[2] = 1080;
                table_at->output_x[3] = 0, table_at->output_y[3] = 0, table_at->output_w[3] = 1920, table_at->output_h[3] = 1080;
                memory_copy(table_at->output_name[0], "DP-4", 5), memory_copy(table_at->output_name[1], "DP-3", 5);
                memory_copy(table_at->output_name[2], "DP-2", 5), memory_copy(table_at->output_name[3], "DP-1", 5);
                settings.ui.scale = 200;
                display_map_place();
                ui_graph_start(&settings.map.graph);
                display_map_screen(2, &tx, &ty, &tw, &th);
                settings.popup.open = 0, ui_field_close(&settings.edit), (void)0, settings.page.card = false, settings_filter_clear(), (void)0, settings.map.keys = false;
                settings.map.display = -1, settings.map.sent = false;
                event = (struct window_event){WINDOW_EVENT_MOTION, 0, tx + tw / 2, ty + th / 2};
                settings_event_at(&event);
                event = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                settings_event_at(&event);
                check("display section: at scale 2 a press on a screen of the map chooses it and takes it",
                      settings.map.display == 2 && settings.map.graph.held == 2);
                before_x = settings.map.graph.at_x, before_y = settings.map.graph.at_y;
                event = (struct window_event){WINDOW_EVENT_MOTION, 0, tx + tw / 2 + tw * 2, ty + th / 2 + 40};
                settings_event_at(&event);
                check("display section: at scale 2 a motion over the map drags the screen it took, where it would land moves",
                      settings.map.graph.held == 2 && settings.map.graph.moving && (settings.map.graph.at_x != before_x || settings.map.graph.at_y != before_y));
                ui_graph_let_go(&settings.map.graph);
                settings.map.display = -1;
                settings.ui.scale = 100, table_at->scale = 100;
                display_map_place();
        }

        // The keyboard on the map. The four screens of the 2 by 2 (index 0 bottom right, number 4; index 3 top left, number 1): Up
        // from the Screen row goes to the map, the arrows choose a screen by its number (clamped), and Down goes back to the rows.
        // A key's Shift step is checked on its own (settings_map_stepped), since a sent change would run the verb.
        {
                struct window_event key;

                windows_scratch_zero(table_at);
                table_at->outputs = 4, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 100;
                table_at->output_x[0] = 1920, table_at->output_y[0] = 1080, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
                table_at->output_x[1] = 0, table_at->output_y[1] = 1080, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
                table_at->output_x[2] = 1920, table_at->output_y[2] = 0, table_at->output_w[2] = 1920, table_at->output_h[2] = 1080;
                table_at->output_x[3] = 0, table_at->output_y[3] = 0, table_at->output_w[3] = 1920, table_at->output_h[3] = 1080;
                memory_copy(table_at->output_name[0], "DP-4", 5), memory_copy(table_at->output_name[1], "DP-3", 5);
                memory_copy(table_at->output_name[2], "DP-2", 5), memory_copy(table_at->output_name[3], "DP-1", 5);
                settings.map.display = 0, settings.map.keys = false, settings.map.sent = false;
                ui_graph_let_go(&settings.map.graph);
                settings.page.pane = 1, settings.list.focus = screen_row, ui_field_close(&settings.edit), (void)0, settings.page.card = false;
                settings_filter_clear(), (void)0, settings.page.menu_row = -1;
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_UP, 0};
                settings_key(&key);
                check("display section: Up from the Screen row goes up to the map, which then has the keys", settings.map.keys && settings.list.focus == screen_row);
                check("display section: the map's caption says what its keys do while it has them",
                      string_equals((string_address)settings_map_hint(4), (string_address) "Arrows choose a screen. Shift and an arrow moves it. Enter identifies. Down leaves."));
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_LEFT, 0};
                settings_key(&key);
                check("display section: Left chooses the screen before it by number (number 4 to 3)", settings.map.keys && settings.map.display == 1);
                settings_key(&key);
                settings_key(&key);
                settings_key(&key);
                check("display section: Left stops at Screen 1 (the top left screen, index 3)", settings.map.keys && settings.map.display == 3);
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_RIGHT, 0};
                settings_key(&key);
                check("display section: Right chooses the screen after it by number (number 1 to 2)", settings.map.keys && settings.map.display == 2);
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_DOWN, 0};
                settings_key(&key);
                check("display section: Down leaves the map for the rows, the Screen row keeping the focus", !settings.map.keys && settings.page.pane == 1 && settings.list.focus == screen_row);
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_TAB, 0};
                settings_key(&key);
                check("display section: Tab from the rows goes on to the column, as it does elsewhere", settings.page.pane == 0 && !settings.map.keys);
                settings.page.pane = 1;
                // The graph's keys on the map: F shows every screen again, and Space held makes a press pan.
                settings.map.keys = true;
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, 33, 'f'};
                settings.map.graph.zoom = settings.map.graph.least;
                settings_key(&key);
                check("display section: F on the map shows every screen again, and the map keeps the keys", settings.map.keys && settings.map.graph.zoom > settings.map.graph.least);
                key = (struct window_event){WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_SPACE, ' '};
                settings_key(&key);
                check("display section: Space held on the map makes a press pan", settings.map.keys && settings.map.graph.space);
                key = (struct window_event){WINDOW_EVENT_KEY, 0, UI_KEY_SPACE, ' '};
                settings_key(&key);
                check("display section: and let go it no longer does", !settings.map.graph.space);
                settings.map.keys = false;
        }

        // A Shift and an arrow moves the chosen screen a step, from where it was sent while the table has not caught up, and from the
        // table once it has: two screens side by side, the right one chosen, stepped along the other's side and never off it.
        {
                int x, y;
                char words[SETTINGS_TYPED];

                windows_scratch_zero(table_at);
                table_at->outputs = 2, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 100;
                table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
                table_at->output_x[1] = 1920, table_at->output_w[1] = 1920, table_at->output_h[1] = 1080;
                memory_copy(table_at->output_name[0], "DP-1", 5), memory_copy(table_at->output_name[1], "DP-2", 5);
                settings.map.display = 1, settings.map.keys = true, settings.map.sent = false;
                check("display section: a Shift and an arrow steps the chosen screen by a step along its neighbour", settings_map_stepped(0, -1, &x, &y) && x == 1920 && y == -10);
                // As settings_map_place keeps it: the desk's top left is 0, 0, so the other screen is ten lower.
                check("display section: the step is written as the whole desk", settings_map_layout(1, x, y, words, sizeof(words)) &&
                      string_equals((string_address)words, (string_address) "DP-1 place 0 10 DP-2 place 1920 0"));
                settings.map.sent = true, settings.map.sent_at = (positive)ui_kit.now, settings.map.sent_count = 2;
                check("display section: a held key steps from where the screen was sent, not from the table again",
                      settings_map_stepped(0, -1, &x, &y) && x == 1920 && y == -10 && settings.map.node[0].y == 10);
                table_at->output_y[0] = 10;
                check("display section: once the table has the place a key sent, the next step is from the table",
                      settings_map_stepped(0, 1, &x, &y) && !settings.map.sent && x == 1920 && y == 10);
                check("display section: a step off the desk is no move: the screen stays against its neighbour", !settings_map_stepped(1, 0, &x, &y));
                check("display section: a step that moves nothing is no move", !settings_map_stepped(0, 0, &x, &y));
                table_at->outputs = 1;
                check("display section: a lone screen has nothing to step against", !settings_map_stepped(1, 0, &x, &y));
                settings.map.keys = false;
                check("display section: with one screen the caption says so, though the keys are on the map", string_equals((string_address)settings_map_hint(1), (string_address) "Only one screen is connected."));
        }

        /*
                The desk written whole (canvas display NAME place X Y ...): its screens' lines go first in the order named (a screen
                with no line has none, a later line of a screen named goes), every other line after them as it was, and the compositor
                lays the placed screens in that order, so the first named is the first laid, the primary. A file that does not fit is
                refused.
        */
        {
                static const char names[3][WINDOWS_OUTPUT_NAME] = {"Virtual-2", "eDP-1", "HDMI-A-1"}, pair[2][WINDOWS_OUTPUT_NAME] = {"DP-1", "DP-2"};
                static const char file[] = "HDMI-A-1 rotation=90\n\nVirtual-2 rotation=90 x=5 y=6\nbad_name x=1 y=2\nVirtual-2 rotation=0\n";
                struct windows_display list[WINDOWS_DISPLAYS];
                char led[256], pinned[128], few[10];
                unsigned int listed;
                int made;

                made = windows_displays_lead(led, sizeof(led), file, names, 3);
                check("display section: a desk written whole puts its screens' lines first in the order named, the rest after them as they were",
                      made > 0 && made == (int)string_length((string_address)led) &&
                          string_equals((string_address)led, (string_address) "Virtual-2 rotation=90 x=5 y=6\nHDMI-A-1 rotation=90\n\nbad_name x=1 y=2\n"));
                check("display section: a file the lines do not fit in is refused", windows_displays_lead(few, sizeof(few), file, names, 3) == -1);
                listed = windows_displays_read("DP-2 x=1920 y=0\nDP-1 x=0 y=0\n", list, WINDOWS_DISPLAYS);
                made = (int)windows_arrange_pinned("", pair, 2, list, listed, pinned, sizeof(pinned));
                check("display section: the compositor lays placed screens in the order of their lines",
                      made > 0 && string_equals((string_address)pinned, (string_address) "DP-2@1920,0 DP-1@0,0"));
                made = windows_displays_lead(led, sizeof(led), "DP-2 x=1920 y=0\nDP-1 x=0 y=0\n", pair, 2);
                listed = windows_displays_read(led, list, WINDOWS_DISPLAYS);
                check("display section: so a desk written in its laid order is laid in that order, the first named the primary",
                      made > 0 && windows_arrange_pinned("", pair, 2, list, listed, pinned, sizeof(pinned)) > 0 &&
                          string_equals((string_address)pinned, (string_address) "DP-1@0,0 DP-2@1920,0"));
        }

        // A screen's scale, in the displays file: one of the six presets in percent, 100 the default (not written), judged the
        // same by the file's reader, the verb's word and the setter; a malformed one is bad and kept at 100.
        {
                static const char *const presets[6] = {"50", "75", "100", "125", "150", "200"};
                static const unsigned int percent[6] = {50, 75, 100, 125, 150, 200};
                static const char file[] = "HDMI-A-1 scale=75\nVirtual-2 rotation=90 scale=200 x=1 y=2\neDP-1 scale=100\nVirtual-1 scale=60\n";
                struct windows_display line, scaled[WINDOWS_DISPLAYS], back[WINDOWS_DISPLAYS];
                char text[256], set_text[256], word[32];
                unsigned int count, written, length, each;
                int value, set_made;

                for (each = 0; each < 6; each++)
                {
                        length = 16;
                        memory_copy(word, "Virtual-2 scale=", 16);
                        while (presets[each][length - 16])
                                word[length] = presets[each][length - 16], length++;
                        windows_display_line(word, length, &line);
                        check("display section: each scale preset reads as a screen's scale, and the verb's word judges the same value",
                              line.bad == 0 && line.scale == percent[each] &&
                                  windows_display_value(WINDOWS_DISPLAY_SCALE, presets[each], (unsigned int)string_length((const_string)presets[each]), &value) &&
                                  value == (int)percent[each]);
                }
                windows_display_line("Virtual-2 scale=60", (unsigned int)string_length((const_string)"Virtual-2 scale=60"), &line);
                check("display section: a scale that is no preset is bad, is kept at 100, and the verb refuses it",
                      line.scale == 100 && line.bad == WINDOWS_DISPLAY_SCALE && !windows_display_value(WINDOWS_DISPLAY_SCALE, "60", 2, &value));
                check("display section: the verb takes no percent sign and no decimal point as a scale",
                      !windows_display_value(WINDOWS_DISPLAY_SCALE, "100%", 4, &value) && !windows_display_value(WINDOWS_DISPLAY_SCALE, "1.5", 3, &value));
                count = windows_displays_read(file, scaled, WINDOWS_DISPLAYS);
                check("display section: a file's scales are read per screen: 75 and 200 as set, 100 for a line without one or with a bad one",
                      count == 4 && scaled[0].scale == 75 && scaled[1].scale == 200 && scaled[2].scale == 100 && scaled[3].scale == 100 &&
                          scaled[3].bad == WINDOWS_DISPLAY_SCALE);
                written = windows_displays_write(text, sizeof(text), scaled, count);
                check("display section: a write puts scale= after refresh, and writes no line for a screen at 100 with nothing else",
                      written && !string_compare((string_address)text, (string_address)"HDMI-A-1 rotation=0 refresh=preferred scale=75\nVirtual-2 rotation=90 refresh=preferred scale=200 x=1 y=2\n"));
                count = windows_displays_read(text, back, WINDOWS_DISPLAYS);
                check("display section: the file round-trips: read back, the scales are the same settings",
                      count == 2 && back[0].scale == 75 && back[1].scale == 200 && back[1].rotation == 90 && back[1].placed);
                memory_fill(&line, 0, sizeof(line));
                line.scale = 150;
                set_made = windows_displays_set(set_text, sizeof(set_text), "HDMI-A-1 rotation=90\n", "HDMI-A-1", WINDOWS_DISPLAY_SCALE, &line);
                check("display section: setting a scale keeps the screen's other fields",
                      set_made > 0 && strstr(set_text, "rotation=90") && strstr(set_text, "scale=150"));
                line.scale = 100;
                set_made = windows_displays_set(set_text, sizeof(set_text), "HDMI-A-1 rotation=90 scale=150\n", "HDMI-A-1", WINDOWS_DISPLAY_SCALE, &line);
                check("display section: setting 100 writes the line back without a scale",
                      set_made > 0 && strstr(set_text, "rotation=90") && !strstr(set_text, "scale="));
        }

        // The per-screen scale: a window is drawn at the scale of the screen it is mostly on (a window straddling the seam takes
        // the screen with more of it), and the screen's own scale is its displays line's; the desktop's is the primary's.
        {
                unsigned int saved_outputs = compositor.outputs;
                int saved_scale = compositor.scale;

                compositor.outputs = 2;
                compositor.screen[0][0] = 0, compositor.screen[0][1] = 0, compositor.screen[0][2] = 1920, compositor.screen[0][3] = 1080;
                compositor.screen[1][0] = 1920, compositor.screen[1][1] = 0, compositor.screen[1][2] = 1920, compositor.screen[1][3] = 1080;
                compositor.output[0].scale = 100, compositor.output[1].scale = 150;
                compositor.scale = compositor.output[0].scale;
                compositor.x[1] = 2000, compositor.y[1] = 100, compositor.w[1] = 800, compositor.h[1] = 600;
                check("display section: a window on the screen at 150% is drawn at 150%, and that screen's scale is its own",
                      compositor_row_scale(1) == 150 && compositor_output_scale(1) == 150);
                compositor.x[1] = 1500;
                check("display section: a window mostly on the screen at 100% is drawn at 100%, though it reaches the other",
                      compositor_row_scale(1) == 100);
                compositor.x[1] = 1500, compositor.w[1] = 1200;
                check("display section: a window straddling the seam is drawn at the scale of the screen with more of it",
                      compositor_row_scale(1) == 150);
                compositor.x[1] = 5000, compositor.w[1] = 300;
                check("display section: a window off every screen is drawn at the scale of the nearest one", compositor_row_scale(1) == 150);
                compositor.outputs = saved_outputs;
                compositor.scale = saved_scale;
        }

        // A screen's monitor name, from its EDID: the descriptor tagged FC, its text ended by a newline, its blanks at the end
        // dropped; a block with no EDID header, no such descriptor, or too short for one names nothing, and a room cuts the name.
        {
                unsigned char edid[128];
                char named[WINDOWS_OUTPUT_MODEL];
                static const char dell[] = "DELL U2720Q\n   ";

                memory_zero(edid, sizeof(edid));
                edid[1] = edid[2] = edid[3] = edid[4] = edid[5] = edid[6] = 0xff;
                memory_copy(edid + 77, dell, sizeof(dell) - 1);
                edid[72 + 3] = 0xfc;
                check("display section: a screen's monitor name is the text of its EDID's FC descriptor, blanks dropped",
                      compositor_edid_name(edid, 128, named, sizeof(named)) == 11 && string_equals((string_address)named, (string_address) "DELL U2720Q"));
                check("display section: a monitor name cut by the room it has keeps what fits, and a null after it",
                      compositor_edid_name(edid, 128, named, 4) == 3 && string_equals((string_address)named, (string_address) "DEL"));
                edid[0] = 1;
                check("display section: a block with no EDID header names nothing", compositor_edid_name(edid, 128, named, sizeof(named)) == 0 && !named[0]);
                edid[0] = 0, edid[72 + 3] = 0xfe;
                check("display section: an EDID with no FC descriptor names nothing", compositor_edid_name(edid, 128, named, sizeof(named)) == 0 && !named[0]);
                check("display section: a block shorter than an EDID names nothing", compositor_edid_name(edid, 64, named, sizeof(named)) == 0);
        }

        settings.page.pane = 0, settings.map.keys = false, settings.map.sent = false;
        ui_graph_start(&settings.map.graph);
        settings.own.table = saved;
        settings.map.display = -1;
}

// What is typed in the filter, as the keys would have left it: folded to lower case, and the rows that have it listed.
static fn settings_groups_filter(const char address_to text, positive length)
{
        length = length < SETTINGS_FILTER - 1 ? length : SETTINGS_FILTER - 1;
        ui_text_set(settings_filter_field(), (const p8 address_to)text, length);
        ui_text_search_changed(settings_filter_field());
        settings_rebuild();
}

static int settings_groups_named(const char address_to name)
{
        for (int at = 0; at < settings_groups(); at++)
                if (string_equals((string_address)settings_group_name(at), (string_address)name))
                        return at;
        return -1;
}

/*
        Where the rows are listed. A row is in the group a person looks for it in, by what it does and not by the program that
        applies it: the keyboard, the mouse, the pointer and touch are Input, how a wheel, a trackpad and a drag move the content is Scrolling, how the desktop is drawn is Canvas, the clock and its
        zone are Time, and the screens are Display. The Canvas header went with the row it was on (the removed Scale) and left
        the desktop's colour, theme, caret, pointer, clipboard and windows listed under Machine, with the keyboard and the zone.
        Every row of the table is checked by its verb's family, the moved ones by name, and each is found in its group and by
        the filter, which lists the rows of every group.
*/
/*
        The rows under a drive's row as the settings list them (settings_drive_rows), its partitions open and at rest, each described
        and its words kept (a row's words are made as it is described). The list's places, the lines under the rows and the drive's
        fold are the window's own, so they are put back as they were.
*/
#define DRIVE_PARTS 40
static struct drive_part
{
        char label[SETTINGS_ITEM_LABEL], aside[48], amount[24], share[24];
        int kind, swatch, expander;
        b32 dim;
        b64 reveal;
} drive_parts[DRIVE_PARTS];

static int drive_parts_of(int row)
{
        static int ids[CHECK_LIST_MOST];
        static struct settings_sub subs[CHECK_SUBS_MOST];
        int ids_count = settings.list.count < CHECK_LIST_MOST ? settings.list.count : CHECK_LIST_MOST;
        int subs_count = settings_sub_count < CHECK_SUBS_MOST ? settings_sub_count : CHECK_SUBS_MOST, count;
        unsigned char expanded = settings_exp_get(row);
        b64 fold = settings_fold_reveal(row);

        memory_copy(ids, settings.list.id, sizeof(int) * (positive)ids_count), memory_copy(subs, settings_sub, sizeof(*subs) * (positive)subs_count);
        memory_zero(drive_parts, sizeof(drive_parts));
        ui_list_fold(&settings.list, settings_fold_key(row), true), settings_sub_count = 0;
        int list_count = settings.list.count;
        ui_list_begin(&settings.list); // the rows are listed from the list's first id, as a rebuild lists them (ui_list_add appends)
        count = settings_drive_rows(row, 0);
        for (int at = 0; at < count && at < DRIVE_PARTS; at++)
        {
                struct ui_row described;
                struct drive_part address_to part = &drive_parts[at];

                ui_list_describe(&settings.list, settings.list.id[at], &described);
                string_copy_bounded((string_address)part->label, (string_address)(described.label ? described.label : ""), sizeof(part->label));
                string_copy_bounded((string_address)part->aside, (string_address)(described.aside ? described.aside : ""), sizeof(part->aside));
                string_copy_bounded((string_address)part->amount, (string_address)(described.amount ? described.amount : ""), sizeof(part->amount));
                string_copy_bounded((string_address)part->share, (string_address)(described.share ? described.share : ""), sizeof(part->share));
                part->kind = described.kind, part->swatch = described.swatch, part->expander = described.expander, part->dim = described.dim;
                part->reveal = described.reveal;
        }
        settings.list.count = list_count;
        memory_copy(settings.list.id, ids, sizeof(int) * (positive)ids_count), memory_copy(settings_sub, subs, sizeof(*subs) * (positive)subs_count);
        settings_sub_count = subs_count, ui_list_fold(&settings.list, settings_fold_key(row), expanded);
        return count;
}

// A row of them: its label, its aside, its amount and its share as the row says them (an empty word for none).
static b32 drive_part_says(int at, const char address_to label, const char address_to aside, const char address_to amount, const char address_to share)
{
        const struct drive_part address_to part = &drive_parts[at];

        return part->kind == UI_ROW_SHARE && string_equals((string_address)part->label, (string_address)label) &&
               string_equals((string_address)part->aside, (string_address)aside) && string_equals((string_address)part->amount, (string_address)amount) &&
               string_equals((string_address)part->share, (string_address)share);
}

/*
        The device list's feed, read as device list says it: a drive with four segments (EFI, Linux, swap and free room) is a
        note with its strip; a drive with no partition table is one unpartitioned segment; a USB device's line has no segments
        and reads as it always did, and a press on it is a port's, not a note's.
*/
/*
        A screen's size, kept by name in the displays file and chosen on the Displays page, and the primary screen.

        The file: mode=WxH and primary=yes read, written back the same and judged; a screen made the primary is the only one. The
        compositor: every size a screen lists, each once and the most pixels first, the preferred marked; a size asked that the
        screen is not at said refused in the table; and the screen whose line says primary=yes laid first, wherever it is on the desk.
        The page: the Resolution row lists the sizes with the screen's own marked and shows the one it is at; a size chosen is sent
        by name and waits to be kept, the menu's first item keeping it, and goes back to the size before when its time is up or when
        the compositor says the screen did not take it. The Primary display row makes the chosen screen the primary.
*/
static fn check_display_modes(void)
{
        struct windows address_to saved = settings.own.table;
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct windows_display list[4], change = {0};
        char text[SETTINGS_VALUE], out[512], work[512];
        int size_row = settings_output_row(SETTINGS_OUT_SIZE), primary_row = settings_output_row(SETTINGS_OUT_PRIMARY), made;
        unsigned int count;

        // The file.
        count = windows_displays_read("HDMI-A-2 mode=1920x1080 primary=yes\nDP-1 mode=preferred\nDP-2 mode=12x\n", list, 4);
        check("display modes: a line's mode=WxH and primary=yes are read",
              count == 3 && list[0].width == 1920 && list[0].height == 1080 && list[0].primary && !list[0].bad);
        check("display modes: mode=preferred is no size asked, and a malformed size is bad",
              list[1].width == 0 && !list[1].bad && !list[1].primary && (list[2].bad & WINDOWS_DISPLAY_MODE));
        made = (int)windows_displays_write(out, sizeof(out), list, 1);
        check("display modes: a line written back says its mode and that it is the primary",
              made > 0 && string_equals((string_address)out, (string_address) "HDMI-A-2 rotation=0 refresh=preferred mode=1920x1080 primary=yes\n"));
        made = windows_displays_primary(out, work, sizeof(out), "HDMI-A-2 mode=1920x1080 primary=yes\nDP-1 rotation=90\n", "DP-1");
        count = made > 0 ? windows_displays_read(out, list, 4) : 0;
        check("display modes: a screen made the primary is the only one, and the one that was keeps its other fields",
              count == 2 && !list[0].primary && list[0].width == 1920 &&
                  windows_display_of(list, count, "DP-1", 4) && windows_display_of(list, count, "DP-1", 4)->primary &&
                  windows_display_of(list, count, "DP-1", 4)->rotation == 90);
        change.width = 1280, change.height = 720;
        made = windows_displays_set(out, sizeof(out), "", "eDP-1", WINDOWS_DISPLAY_MODE, &change);
        check("display modes: a screen with no line gets one of its size alone", made > 0 && string_equals((string_address)out,
              (string_address) "eDP-1 rotation=0 refresh=preferred mode=1280x720\n"));
        {
                unsigned int w = 1, h = 1;

                check("display modes: a size is WIDTHxHEIGHT of whole numbers, and preferred is none",
                      windows_display_size("3840x2160", 9, &w, &h) && w == 3840 && h == 2160 && windows_display_size("preferred", 9, &w, &h) && !w &&
                          !windows_display_size("0x10", 4, &w, &h) && !windows_display_size("1920", 4, &w, &h) && !windows_display_size("99999x1", 7, &w, &h));
        }

        // The compositor: the sizes a screen lists, and what it says of a size asked.
        {
                static struct compositor_connected found;
                struct compositor_mode modes[6] = {{0}};
                static struct windows *made_scratch;
                struct windows *const made_at = windows_scratch_table(&made_scratch);
                int rects[WINDOWS_OUTPUTS][4];
                unsigned char order[WINDOWS_OUTPUTS];
                unsigned int lit;

                memory_zero(&found, sizeof(found));
                modes[0].hdisplay = 1920, modes[0].vdisplay = 1080, modes[0].vrefresh = 60, modes[0].type = COMPOSITOR_MODE_PREFERRED;
                modes[1].hdisplay = 1920, modes[1].vdisplay = 1080, modes[1].vrefresh = 50;
                modes[2].hdisplay = 2560, modes[2].vdisplay = 1440, modes[2].vrefresh = 60;
                modes[3].hdisplay = 1024, modes[3].vdisplay = 768, modes[3].vrefresh = 60;
                modes[4].hdisplay = 1920, modes[4].vdisplay = 1080, modes[4].vrefresh = 30, modes[4].flags = 16u; // interlaced
                modes[5].hdisplay = 1280, modes[5].vdisplay = 1024, modes[5].vrefresh = 60;
                compositor_sizes_take(&found, 0, modes, 6);
                check("display modes: a screen's sizes are each once, the most pixels first, and the preferred is marked",
                      found.size_count[0] == 4 && found.sizes[0][0][0] == 2560 && found.sizes[0][1][0] == 1920 && found.sizes[0][2][0] == 1280 &&
                          found.sizes[0][3][0] == 1024 && found.size_preferred[0] == 1);

                memory_zero(compositor_display, sizeof(compositor_display));
                memory_copy(compositor_display[0].name, "HDMI-A-2", 9);
                compositor_display[0].width = 2560, compositor_display[0].height = 1440, compositor_display[0].scale = 100;
                compositor_displays = 1;
                compositor.output[0].mode.hdisplay = 1920, compositor.output[0].mode.vdisplay = 1080;
                compositor_table_refusals(made_at, 0, "HDMI-A-2");
                check("display modes: a size asked that the screen is not at is refused in the table, with the size asked",
                      (made_at->output_refused[0] & WINDOWS_REFUSED_SIZE) && made_at->output_asked_w[0] == 2560 && made_at->output_asked_h[0] == 1440);
                compositor.output[0].mode.hdisplay = 2560, compositor.output[0].mode.vdisplay = 1440;
                compositor_table_refusals(made_at, 0, "HDMI-A-2");
                check("display modes: the size asked, taken, is refused nothing", !(made_at->output_refused[0] & WINDOWS_REFUSED_SIZE));

                // Two screens side by side, the second named the primary: it is laid first, and stays where it is.
                memory_zero(&found, sizeof(found));
                found.count = 2;
                memory_copy(found.name[0], "DP-1", 5), memory_copy(found.name[1], "HDMI-A-2", 9);
                found.size[0][0] = 1920, found.size[0][1] = 1080, found.size[1][0] = 1280, found.size[1][1] = 1024;
                compositor_display[0].primary = 1;
                lit = compositor_space_lay(&found, 0, rects, order);
                check("display modes: the screen whose line says primary=yes is laid first, and keeps its place on the desk",
                      lit == 2 && order[0] == 1 && order[1] == 0 && rects[1][0] != rects[0][0]);
                compositor_display[0].primary = 0;
                lit = compositor_space_lay(&found, 0, rects, order);
                check("display modes: with no screen the primary, the first laid is", lit == 2 && order[0] == 0);
                compositor_displays = 0;
                memory_zero(compositor_display, sizeof(compositor_display));
                memory_zero(&compositor.output[0].mode, sizeof(compositor.output[0].mode));
        }

        // The page.
        windows_scratch_zero(table_at);
        table_at->outputs = 2, table_at->output_width = 1920, table_at->output_height = 1080, table_at->scale = 100;
        table_at->output_x[0] = 0, table_at->output_w[0] = 1920, table_at->output_h[0] = 1080;
        table_at->output_x[1] = 1920, table_at->output_w[1] = 1024, table_at->output_h[1] = 768;
        memory_copy(table_at->output_name[0], "HDMI-A-2", 9), memory_copy(table_at->output_name[1], "DP-1", 5);
        table_at->output_sizes[0] = 3, table_at->output_size_preferred[0] = 0;
        table_at->output_size[0][0][0] = 1920, table_at->output_size[0][0][1] = 1080;
        table_at->output_size[0][1][0] = 1280, table_at->output_size[0][1][1] = 720;
        table_at->output_size[0][2][0] = 1024, table_at->output_size[0][2][1] = 768;
        table_at->output_mode_w[0] = 1024, table_at->output_mode_h[0] = 768;
        table_at->output_mode_w[1] = 1024, table_at->output_mode_h[1] = 768;
        settings.own.table = table_at;
        settings.map.display = 0;
        settings.runner.child = 1, settings.queued = 0, settings_resize.on = false;
        check("display modes: the Resolution and Primary display rows are rows of the Display section", size_row >= 0 && primary_row >= 0 &&
              settings_group_of(size_row) == settings_group_of(settings_output_row(SETTINGS_OUT_SCREEN)) &&
              settings_group_of(primary_row) == settings_group_of(size_row));
        check("display modes: the Resolution dropdown lists every size the screen lists, its own marked recommended",
              settings_options(&settings_rows[size_row]) == 3 && settings_choice(&settings_rows[size_row], 0, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "1920 × 1080 (recommended)") &&
                  settings_choice(&settings_rows[size_row], 2, text, sizeof(text)) && string_equals((string_address)text, (string_address) "1024 × 768"));
        check("display modes: the Resolution row shows the size the screen is at",
              string_equals((string_address)settings_shown_value(size_row), (string_address) "1024 × 768"));
        settings_output_apply(size_row, "1920 × 1080 (recommended)");
        check("display modes: a size chosen is sent by the screen's name, and waits to be kept",
              settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "HDMI-A-2 mode 1920x1080") &&
                  settings_resize.on && settings_resize.width == 1920 && settings_resize.old_w == 0);
        check("display modes: while it waits, the dropdown's first item keeps it, with the seconds left",
              settings_options(&settings_rows[size_row]) == 4 && settings_choice(&settings_rows[size_row], 0, text, sizeof(text)) &&
                  string_equals((string_address)text, (string_address) "Keep 1920 × 1080 (back in 15 s)"));
        settings.queued = 0;
        settings_output_apply(size_row, "Keep 1920 × 1080 (back in 15 s)");
        check("display modes: the keep item keeps it, and sends nothing", !settings_resize.on && settings.queued == 0);

        // Not kept in time: the size before goes back (preferred, as the line asked none).
        settings_output_apply(size_row, "1280 × 720");
        settings.queued = 0;
        settings_resize.due = (positive)ui_kit.now;
        (void)settings_keep_tick();
        check("display modes: a size not kept in time goes back to the size the line asked before",
              !settings_resize.on && settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "HDMI-A-2 mode preferred"));
        settings.queued = 0;

        // Refused by the screen: back at once, and a line that asked a size goes back to that size.
        table_at->output_asked_w[0] = 1280, table_at->output_asked_h[0] = 720;
        settings_output_apply(size_row, "1920 × 1080 (recommended)");
        settings.queued = 0;
        table_at->output_asked_w[0] = 1920, table_at->output_asked_h[0] = 1080, table_at->output_refused[0] = WINDOWS_REFUSED_SIZE;
        (void)settings_keep_tick();
        check("display modes: a size the compositor says the screen did not take goes back at once, to the size asked before",
              !settings_resize.on && settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "HDMI-A-2 mode 1280x720"));
        check("display modes: a size refused is said on the Resolution row, with the size asked",
              string_equals((string_address)settings_row_text(size_row, 0), (string_address) "1024 × 768 (1920 × 1080 is not offered)"));
        settings.queued = 0, table_at->output_refused[0] = 0;

        // The primary: the table's first screen is, another is made so by name.
        check("display modes: the Primary display row says the table's first screen is the primary",
              string_equals((string_address)settings_shown_value(primary_row), (string_address) "Primary"));
        settings.map.display = 1;
        check("display modes: and that another is not", string_equals((string_address)settings_shown_value(primary_row), (string_address) "Not primary"));
        settings_output_apply(primary_row, "Primary");
        check("display modes: making a screen the primary sends its name", settings.queued == 1 &&
              string_equals((string_address)settings.queue[0].value, (string_address) "DP-1 primary"));
        settings.queued = 0;
        settings.map.display = 0;
        settings_output_apply(primary_row, "Not primary");
        check("display modes: the primary is not made less than primary on its own: another is made the primary", settings.queued == 0);

        settings.runner.child = 0, settings.queued = 0, settings_resize.on = false;
        settings.map.display = -1;
        settings.own.table = saved;
}

static fn check_settings_device_strip(void)
{
        static const char text[] = "  vda drive 2048 MB view only\n"
                "    part 2048 536870912 efi -\n"
                "    part 1050624 1073741824 linux -\n"
                "    part 3148800 536870912 swap -\n"
                "    part 4245504 1073741824 free -\n"
                "  sdb drive 512 MB view only\n"
                "    part 0 536870912 unpartitioned -\n"
                "  1-1 usb 0627:0001 Stick allowed\n";
        struct settings_item address_to items = settings.item[2];
        positive length = sizeof(text) - 1;

        for (positive at = 0; at < length; at++)
                settings_large->said[at] = text[at];
        settings.runner.length = length;
        settings_feed_hardware();
        items = settings.item[2]; // the feed may grow the list: its rows are read where they are now
        check("settings: a drive's partitions are its strip, in order, with free room",
              settings.item_count[2] == 3 && items[0].kind == SETTINGS_NOTE && items[0].segment_count == 4 &&
                  items[0].segment_role[0] == 0 && items[0].segment_role[1] == 1 && items[0].segment_role[2] == 2 &&
                  items[0].segment_role[3] == 4 && items[0].segment_bytes[1] == 1073741824ull);
        // The drive's line is not one label: its name is at the row's left, and its state is the row's right; its size is in its heading.
        {
                struct ui_row line;

                memory_zero(&line, sizeof(line));
                settings_describe((int)SETTINGS_LIST_ID(2, 0), &line);
                check("settings: a drive's line is its name and after the word drive what the line says of it, its size (in GB from 1000 MB) in its heading",
                      string_equals((string_address)items[0].label, (string_address) "vda") && string_equals((string_address)items[0].size, (string_address) "2 GB") &&
                          line.note && string_equals((string_address)line.note, (string_address) "drive, view only") && !line.value && line.tall &&
                          string_equals((string_address)items[1].label, (string_address) "sdb") && string_equals((string_address)items[1].size, (string_address) "512 MB"));
        }
        check("settings: a drive with no partition table is one unpartitioned segment",
              items[1].kind == SETTINGS_NOTE && items[1].segment_count == 1 && items[1].segment_role[0] == 5 &&
                  items[1].segment_bytes[0] == 536870912ull);
        check("settings: a USB device's line has no strip, and is a port a press allows or blocks",
              items[2].kind == SETTINGS_PORT && items[2].segment_count == 0 && (items[2].flags & SETTINGS_ITEM_JOINED) != 0);
        {
                struct ui_row line;

                memory_zero(&line, sizeof(line));
                settings_describe((int)SETTINGS_LIST_ID(2, 2), &line);
                check("settings: a USB device's line is its name, then USB and its port, its state the tab chosen and not said again, and its port is what a press allows or blocks",
                      string_equals((string_address)items[2].label, (string_address) "Stick") && string_equals((string_address)items[2].port, (string_address) "1-1") &&
                          line.kind == UI_ROW_TABS && line.at == 0 && line.note && string_equals((string_address)line.note, (string_address) "USB 1-1"));
                // A port held until it is allowed shows deny chosen, as one blocked does: its line says waiting, which the tabs cannot.
                string_copy_bounded((string_address)items[2].state, (string_address) "waiting", sizeof(items[2].state));
                items[2].flags &= ~SETTINGS_ITEM_JOINED;
                memory_zero(&line, sizeof(line));
                settings_describe((int)SETTINGS_LIST_ID(2, 2), &line);
                check("settings: a USB device waiting to be allowed has deny chosen and says waiting, which the tabs cannot tell from blocked",
                      line.kind == UI_ROW_TABS && line.at == 1 && line.note && string_equals((string_address)line.note, (string_address) "USB 1-1, waiting"));
        }
        {
                // The rows under the drive: the disclosure, then each partition, then the free room; a disk with no table has none.
                int bare = drive_parts_of((int)SETTINGS_LIST_ID(2, 1)), count = drive_parts_of((int)SETTINGS_LIST_ID(2, 0));

                check("settings: a drive's partitions are rows under it: the disclosure says Partitions and how many, then each partition its role's word where it has no name, its size and its share, and the free room last",
                      count == 5 && bare == 0 && drive_part_says(0, "Partitions", "", "", "3") && drive_parts[0].expander == UI_ROW_EXPANDER_OPEN &&
                          drive_part_says(1, "EFI system", "", "536 MB", "16.7%") && drive_parts[1].swatch == UI_STRIP_EFI + 1 &&
                          drive_part_says(2, "Linux data", "", "1 GB", "33.3%") && drive_part_says(3, "Swap", "", "536 MB", "16.7%") &&
                          drive_part_says(4, "Free space", "", "1 GB", "33.3%") && drive_parts[4].swatch == UI_STRIP_FREE + 1 && drive_parts[4].reveal == UI_ONE);
        }
        {
                // A drive's heading: the share its partitions take, free room not counted, and that in bytes of the whole, as the sizes are
                // said; the whole is what the part lines add up to where that is more than the line's MB. A disk with no table says so.
                static const char quarter[] = "  vdh drive 1000 MB view only\n    part 0 1048576 free -\n    part 1048576 248951424 efi -\n"
                                              "    part 250000000 500000000 linux -\n    part 750000000 250000000 free -\n"
                                              "  vdi drive 999 MB view only\n    part 0 999999999 unpartitioned -\n";
                positive length = sizeof(quarter) - 1;
                struct ui_row line, bare;
                b32 said;

                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = quarter[at];
                settings.runner.length = length;
                settings_feed_hardware();
                memory_zero(&line, sizeof(line)), memory_zero(&bare, sizeof(bare));
                // The heading's words are made as the row is described, so each row's are read before the next is described.
                settings_describe((int)SETTINGS_LIST_ID(2, 0), &line);
                said = line.share && line.amount && string_equals((string_address)line.share, (string_address) "75% partitioned") &&
                       string_equals((string_address)line.amount, (string_address) "750 MB / 1 GB");
                settings_describe((int)SETTINGS_LIST_ID(2, 1), &bare);
                check("settings: a drive's heading says the share its partitions take and USED / TOTAL; one with no table says so and its size",
                      items[0].total == 1000000000ull && items[1].total == 999999999ull && said && bare.share && bare.amount &&
                          string_equals((string_address)bare.share, (string_address) "No partition table") && string_equals((string_address)bare.amount, (string_address) "999 MB"));
        }
        {
                // The host shows one partition of this drive and says three more are not shown: they are a quiet row after it.
                static const char more[] = "  vde drive 1 MB view only\n    part 2048 1048576 linux -\n    more 3\n";
                positive length = sizeof(more) - 1;
                int count;

                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = more[at];
                settings.runner.length = length;
                settings_feed_hardware();
                count = drive_parts_of((int)SETTINGS_LIST_ID(2, 0));
                check("settings: the partitions the host did not show are a quiet row after the drive's partitions",
                      settings.item_count[2] == 1 && items[0].segment_more == 3 && items[0].segment_count == 1 && count == 3 &&
                          drive_part_says(0, "Partitions", "", "", "1") && drive_part_says(1, "Linux data", "", "1 MB", "100.0%") &&
                          drive_part_says(2, "3 more not shown", "", "", "") && drive_parts[2].dim && !drive_parts[2].swatch);
        }
        {
                //      Twenty-three ports fill the rows: the drive after them has no row, so its parts are no one's strip, and the
                //      port before it keeps none. The row after the last is a note: one more is not shown. A drive with thirteen
                //      segments keeps all of them (the host sends up to 33).
                static char rows[2048];
                static const char part[] = "    part 2048 2097152 free -\n";
                positive length = 0;

                for (positive each = 0; each < 23; each++)
                        for (const char address_to line = "  1-1 usb 0627:0001 Stick allowed\n"; *line; line++)
                                rows[length++] = *line;
                for (const char address_to line = "  vdb drive 1 MB view only\n    part 2048 1048576 linux -\n"; *line; line++)
                        rows[length++] = *line;
                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = rows[at];
                settings.runner.length = length;
                settings_feed_hardware();
                items = settings.item[2]; // the feed may move the list: its rows are read where they are now
                // The feed lists the internal drive before the ports (the device sections), so the drive is the first row with its strip, and
                // the 23 ports after it keep no strip; nothing is cut at this many rows.
                check("settings: an internal drive is the first row with its strip, and the ports after it keep none",
                      settings.item_count[2] == 24 && items[0].kind == SETTINGS_NOTE && items[0].segment_count == 1 &&
                          items[1].kind == SETTINGS_PORT && items[1].segment_count == 0 && items[23].kind == SETTINGS_PORT && items[23].segment_count == 0);
                length = 0;
                for (const char address_to line = "  vdc drive 1024 MB view only\n"; *line; line++)
                        rows[length++] = *line;
                for (positive each = 0; each < 13; each++)
                        for (const char address_to line = part; *line; line++)
                                rows[length++] = *line;
                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = rows[at];
                settings.runner.length = length;
                settings_feed_hardware();
                check("settings: a drive with thirteen segments keeps every one of them",
                      settings.item_count[2] == 1 && items[0].kind == SETTINGS_NOTE && items[0].segment_count == 13 &&
                          items[0].segment_bytes[12] == 2097152ull);
                // Free room no bigger than the partitions' alignment (the MiB before a GPT disk's first partition) has no segment.
                length = 0;
                for (const char address_to line = "  vdf drive 1024 MB view only\n    part 0 1048576 free -\n    part 1048576 536870912 efi -\n"
                                                  "    part 537919488 1048575 free -\n    part 538968064 2097152 free -\n";
                     *line; line++)
                        rows[length++] = *line;
                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = rows[at];
                settings.runner.length = length;
                settings_feed_hardware();
                check("settings: free room no bigger than a MiB, the alignment a partition table keeps, is no segment, and free room larger than that is one",
                      settings.item_count[2] == 1 && items[0].segment_count == 2 && items[0].segment_role[0] == 0 && items[0].segment_role[1] == 4 &&
                          items[0].segment_bytes[1] == 2097152ull);
                // A partition's name is the host's, a blank of it an underscore on the line; "-" is no name.
                length = 0;
                for (const char address_to line = "  vdg drive 1024 MB view only\n    part 1048576 536870912 efi EFI_system_partition\n"
                                                  "    part 537919488 4096000000 other Basic_data_partition\n    part 4633919488 1000000 linux -\n";
                     *line; line++)
                        rows[length++] = *line;
                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = rows[at];
                settings.runner.length = length;
                settings_feed_hardware();
                {
                        int count = drive_parts_of((int)SETTINGS_LIST_ID(2, 0));

                        check("settings: a partition's row is the partition's name, and its role's word after a name that does not say it",
                              count == 4 && string_equals((string_address)items[0].segment_name[0], (string_address) "EFI system partition") &&
                                  string_equals((string_address)drive_parts[1].label, (string_address) "EFI system partition") && !drive_parts[1].aside[0] &&
                                  drive_part_says(2, "Basic data partition", "Other", "4 GB", "88.4%") &&
                                  string_equals((string_address)drive_parts[3].label, (string_address) "Linux data") && !drive_parts[3].aside[0]);
                }
                // What the line says is on a partition after its name (fs TYPE LABEL NODE, which Files reads) is no part of its name.
                length = 0;
                for (const char address_to line = "  vdg drive 1024 MB view only\n    part 1048576 536870912 efi EFI_system_partition fs vfat BOOT vdg1\n"
                                                  "    part 537919488 4096000000 other Basic_data_partition fs ntfs My_Files vdg2\n"
                                                  "    part 4633919488 1000000 linux - fs - - vdg3\n";
                     *line; line++)
                        rows[length++] = *line;
                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = rows[at];
                settings.runner.length = length;
                settings_feed_hardware();
                {
                        int count = drive_parts_of((int)SETTINGS_LIST_ID(2, 0));

                        check("settings: what a part line says after the partition's name (its file system and node) is not read as its name",
                              count == 4 && items[0].segment_count == 3 && string_equals((string_address)items[0].segment_name[0], (string_address) "EFI system partition") &&
                                  string_equals((string_address)items[0].segment_name[1], (string_address) "Basic data partition") && !items[0].segment_name[2][0] &&
                                  drive_part_says(2, "Basic data partition", "Other", "4 GB", "88.4%") &&
                                  string_equals((string_address)drive_parts[3].label, (string_address) "Linux data"));
                }
                // A reading that lists nothing is a note that says so.
                settings.runner.length = 0;
                settings_feed_hardware();
                check("settings: a device list with nothing in it is one note that says no drive or USB device was found",
                      settings.item_count[2] == 1 && items[0].kind == SETTINGS_NOTE &&
                          string_equals((string_address)items[0].label, (string_address) "No drives or USB devices found"));
                length = 0;
                for (const char address_to line = "  1-2 usb 0627:0001 "; *line; line++)
                        rows[length++] = *line;
                for (positive each = 0; each < 80; each++)
                        rows[length++] = 'x';
                for (const char address_to line = " blocked\n"; *line; line++)
                        rows[length++] = *line;
                for (positive at = 0; at < length; at++)
                        settings_large->said[at] = rows[at];
                settings.runner.length = length;
                settings_feed_hardware();
                // The state is the line's last word and a field of its own, which the row shows beside its allow and deny: the label
                // is what comes before it, cut to its room, and no cut of the label reaches the state.
                {
                        struct ui_row described;

                        memory_zero(&described, sizeof(described));
                        settings_describe((int)SETTINGS_LIST_ID(2, 0), &described);
                        check("settings: a device line too long for its label keeps its state, which deny chosen shows, and its port",
                              settings.item_count[2] == 1 && items[0].kind == SETTINGS_PORT && string_length((string_address)items[0].label) == 80 &&
                                  string_equals((string_address)items[0].state, (string_address) "blocked") &&
                                  described.kind == UI_ROW_TABS && described.at == 1 && described.note &&
                                  string_equals((string_address)described.note, (string_address) "USB 1-2"));
                }
        }
}

/*
        A drive's rows as the kit lays them out and draws them, in the window with the shipped faces. Every segment of a disk of sixteen
        partitions and the free room round them has a leaf at least UI_SEGMENT_LEAST wide where the strip has the room for that, and
        each is the strip over the count where it has not; a partition of 512 MB on a disk of 1 TB is drawn in its colour; free room
        is the bar's track, in no partition's colour; every role's colour stands out from the track and the surface in both themes;
        the heading's share starts where the strip does and its amount ends where the strip does. Under the drive's row its partitions
        are rows of the list: shut when the window starts, the disclosure alone; opened and shut by a press, by Enter and by Right and
        Left, on the kit's fold; open, a row for each partition with its square, its size and its share in columns that line up, the
        partitions not shown and the free room last, the shares adding up to the whole; remembered by the drive's name when the list is
        read again; and reading from the right every part of them is where it is reading from the left, turned over.
*/
static struct
{
        struct ui_node segment[UI_STRIP_MOST], share, amount;
        b32 has_segment[UI_STRIP_MOST];
        struct ui_row row;
        int at, id, top, height, strip_x, strip_w;
        // The share rows after the drive's (its disclosure, and its partitions while they show): each one's id, place and parts.
        int parts, part_id[DRIVE_PARTS], part_top[DRIVE_PARTS], part_height[DRIVE_PARTS], part_swatch_role[DRIVE_PARTS];
        struct ui_node part_swatch[DRIVE_PARTS], part_label[DRIVE_PARTS], part_amount[DRIVE_PARTS], part_share[DRIVE_PARTS];
} drive;
/*
        The settings window's record, kept and put back around a check that sends it events. A copy of its bytes is right for
        everything in it but what owns memory: the documents of its two text fields (the filter's and a row's, each nine arrays
        that are reserved again as they fill and unmapped when the field closes). Put back as bytes, a document the check grew or
        closed in between names blocks that are gone: the filter's history reached 256 groups once the table had two more Network
        rows for the checks to type at, its groups moved to a larger block, the bytes put back named the block before the move, and
        the next filter typed read unmapped memory (a segmentation fault, which took every row after it). So a document is never
        put back. Each field keeps the document it has now, with the text it had when it was kept, or none when it had none.
*/
struct settings_kept
{
        p8 bytes[sizeof(settings)];
        p8 filter[SETTINGS_VALUE], edit[SETTINGS_VALUE];
};

static fn settings_record_keep(struct settings_kept address_to kept)
{
        memory_copy(kept->bytes, &settings, sizeof(settings));
        ui_text_string(&settings.filter_edit, kept->filter, sizeof(kept->filter));
        ui_text_string(&settings.edit, kept->edit, sizeof(kept->edit));
}

// A field whose bytes were just put back, given the document it had before they were (now) and the text it was kept with.
static fn settings_kept_field(struct ui_text address_to field, struct doc now, const p8 address_to text)
{
        // doc_open names the separators and doc_close zeroes them: a document that is open has them.
        struct doc kept = field->doc;

        field->doc = now;
        if (!kept.separators)
        {
                if (now.separators)
                        doc_close(&field->doc);
        }
        else if (now.separators)
                ui_text_set(field, text, string_length((string_address)text));
        else if (doc_open(&field->doc, text, string_length((string_address)text)))
        {
                // Closed in between and opened again on its text, a field's document as ui_text_open makes one.
                field->doc.single_line = kept.single_line;
                field->doc.auto_close = kept.auto_close;
                field->doc.normalize_paste = kept.normalize_paste;
        }
}

static fn settings_record_put_back(const struct settings_kept address_to kept)
{
        struct doc filter = settings.filter_edit.doc, edit = settings.edit.doc;

        memory_copy(&settings, kept->bytes, sizeof(settings));
        settings_kept_field(&settings.filter_edit, filter, kept->filter);
        settings_kept_field(&settings.edit, edit, kept->edit);
}

/*
        The row that holds it. Between a keep and a put back the filter is set until its history has moved to a larger block,
        however many groups the checks before left in it, and the row's field, open when kept, is closed; put back, the filter
        has the text it was kept with and takes another, and the row's field is open on its own text again.
*/
static fn check_settings_kept(void)
{
        static struct settings_kept kept;
        struct ui_text address_to filter = settings_filter_field();
        p8 said[SETTINGS_VALUE];
        positive room, turns = 0;
        b32 moved, same, taken, opened;

        ui_text_set(filter, (const p8 address_to) "wired", 5);
        ui_field_close(&settings.edit);
        ui_field_begin(&settings.edit, &settings.own, "kept text", 0);
        settings_record_keep(&kept);
        room = filter->doc.group_room;
        while (filter->doc.group_room <= room && turns < 65536)
                ui_text_set(filter, (const p8 address_to)(turns++ & 1 ? "a" : "b"), 1);
        moved = room && filter->doc.group_room > room;
        ui_field_close(&settings.edit);
        settings_record_put_back(&kept);
        ui_text_string(&settings.filter_edit, said, sizeof(said));
        same = string_equals((string_address)said, (string_address) "wired");
        ui_text_set(&settings.filter_edit, (const p8 address_to) "bluetooth", 9);
        ui_text_string(&settings.filter_edit, said, sizeof(said));
        taken = string_equals((string_address)said, (string_address) "bluetooth");
        ui_text_string(&settings.edit, said, sizeof(said));
        opened = settings.edit.open && settings.edit.doc.single_line && string_equals((string_address)said, (string_address) "kept text");
        string_format(log, "  settings kept: the filter's history moved after %p sets (from %p groups of room), same %p, taken %p, the field opened %p\n",
                      turns, room, (positive)same, (positive)taken, (positive)opened);
        check("settings kept: a record put back after its filter's history moved to a larger block has the filter's text and takes another",
              moved && same && taken);
        check("settings kept: and a row's field closed in between is open on the text it was kept with", opened);
        ui_field_close(&settings.edit);
        settings_filter_clear();
}

static struct settings_kept drive_settings_kept;
static p8 drive_kit_kept[sizeof(ui_kit)], drive_items_kept[sizeof(settings.item[2])];
static unsigned char drive_expanded_kept[SETTINGS_ALL], drive_live_kept[SETTINGS_ALL];
static b64 drive_fold_kept[SETTINGS_ALL];

// The device list as the host says it, fed to the Devices list.
static fn drive_feed(const char address_to text)
{
        positive length = string_length((string_address)text);

        for (positive at = 0; at < length; at++)
                settings_large->said[at] = text[at];
        settings.runner.length = length;
        settings_feed_hardware();
}

// The Devices group in the window, width logical pixels by 760 at a scale, read from the right or not, drawn whole.
static fn drive_show(int width, int scale, b32 rtl)
{
        ui_kit.rtl = rtl ? 1 : 0;
        ui_kit_settle();
        settings.ui.scale = scale * 100;
        settings_size((unsigned int)(width * scale), (unsigned int)(760 * scale));
        settings.page.chosen = settings_groups_named("Devices");
        settings_filter_clear();
        settings.page.card = 0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1;
        settings.focused = 0, settings.list.hover = -1;
        settings_refilter();
        settings.list.scroll.scroll = 0;
        ui_kit.now = 5000000000ull;
        ui_kit.busy = 0;
        settings_draw_rows(settings_frames_whole, 0, 0);
}

// The first drive's row laid out where the list has it, and the share rows after it: their parts kept in drive. False: no drive.
static b32 drive_lay(void)
{
        memory_zero(&drive, sizeof(drive));
        for (int at = 0; at < settings.list.count; at++)
        {
                ui_list_describe(&settings.list, settings.list.id[at], &drive.row);
                if (!drive.row.tall)
                        continue;
                drive.at = at, drive.id = settings.list.id[at];
                drive.top = ui_list_row_y(&settings.list, at), drive.height = settings.list.tall[at];
                ui_row_tree(&settings.ui, &settings.list, drive.id, &drive.row, drive.height, true);
                ui_tree_origin(&ui_row_parts.tree, settings.list.x, drive.top);
                for (int k = 0; k < UI_STRIP_MOST; k++)
                        if (ui_row_parts.segment[k])
                                drive.segment[k] = *ui_row_parts.segment[k], drive.has_segment[k] = true;
                if (ui_row_parts.share && ui_row_parts.amount)
                        drive.share = *ui_row_parts.share, drive.amount = *ui_row_parts.amount;
                drive.strip_x = settings.list.x + ui_dev(&settings.ui, UI_INSET), drive.strip_w = settings.list.w - 2 * ui_dev(&settings.ui, UI_INSET);
                for (int next = at + 1; next < settings.list.count && drive.parts < DRIVE_PARTS; next++)
                {
                        struct ui_row row;
                        int k = drive.parts;

                        ui_list_describe(&settings.list, settings.list.id[next], &row);
                        if (row.kind != UI_ROW_SHARE)
                                break;
                        drive.part_id[k] = settings.list.id[next], drive.part_top[k] = ui_list_row_y(&settings.list, next);
                        drive.part_height[k] = settings.list.tall[next], drive.part_swatch_role[k] = row.swatch - 1;
                        ui_row_tree(&settings.ui, &settings.list, drive.part_id[k], &row, drive.part_height[k], true);
                        ui_tree_origin(&ui_row_parts.tree, settings.list.x, drive.part_top[k]);
                        drive.part_swatch[k] = *ui_row_parts.swatch, drive.part_label[k] = *ui_row_parts.label;
                        drive.part_amount[k] = *ui_row_parts.amount, drive.part_share[k] = *ui_row_parts.share;
                        drive.parts++;
                }
                return true;
        }
        return false;
}

static unsigned int drive_pixel(int x, int y)
{
        return settings_frames_whole[(positive)y * SETTINGS_FRAMES_W + (positive)x] & 0xffffffu;
}

/*
        The headers of the sections of the Devices group as the list has them, in order, the empty word for a section with none, and the
        header of the section the row with a label is in: the list as it is laid out (ui_list_layout), its places' starts and their rows'
        headers. The headers are kept a line each in drive_headers.
*/
static char drive_headers[8][48];

// The place in the list of the row with a label, or -1.
static int drive_place_of(const char address_to label)
{
        for (int at = 0; at < settings.list.count; at++)
        {
                struct ui_row row;

                ui_list_describe(&settings.list, settings.list.id[at], &row);
                if (row.label && string_equals((string_address)row.label, (string_address)label))
                        return at;
        }
        return -1;
}

static int drive_sections(void)
{
        int count = 0;

        for (int at = 0; at < settings.list.count && count < 8; at++)
                if (settings.list.starts[at] & UI_LIST_STARTS)
                {
                        struct ui_row row;

                        ui_list_describe(&settings.list, settings.list.id[at], &row);
                        string_copy_bounded((string_address)drive_headers[count++], (string_address)(row.header ? row.header : ""), sizeof(drive_headers[0]));
                }
        return count;
}

// The header of the section a row with this label is in ("" none), and the row itself described; null when no row has the label.
static const char address_to drive_section_of(const char address_to label, struct ui_row address_to found)
{
        static char header[48];

        header[0] = 0;
        for (int at = 0; at < settings.list.count; at++)
        {
                struct ui_row row;

                ui_list_describe(&settings.list, settings.list.id[at], &row);
                if (settings.list.starts[at] & UI_LIST_STARTS)
                        string_copy_bounded((string_address)header, (string_address)(row.header ? row.header : ""), sizeof(header));
                if (row.label && string_equals((string_address)row.label, (string_address)label))
                {
                        *found = row;
                        return header;
                }
        }
        return null;
}

// Every drive of the list shut (the window as it starts), or open, by their names, as a press leaves them; the reading then shows them so.
static fn drive_fold_run(b64 for_ns);
static fn drive_open_all(const char address_to text, b32 open)
{
        // The open names are a list that grows on its first keep (settings_drives_open is null until then): zero what it holds, if it holds any.
        if (settings_drives_open)
                memory_zero(settings_drives_open, sizeof(*settings_drives_open) * settings_drives_open_room);
        drive_feed(text);
        for (int at = 0; open && at < settings.item_count[2]; at++)
                if (settings_item_storage(&settings.item[2][at]))
                        settings_drive_keep(settings_drive_name(&settings.item[2][at]), true);
        drive_feed(text);
        drive_fold_run(2000000000); // the kit's fold follows the name, and its spring runs to rest before the rows are laid out
}

// The shares of the rows under a drive (drive_parts_of), in tenths of a percent, added up: each as its row says it, 42.2% being 422.
static int drive_tenths(int count)
{
        int sum = 0;

        for (int at = 1; at < count; at++)
        {
                int whole = 0, tenth = 0;
                const char address_to text = drive_parts[at].share;

                while (*text >= '0' && *text <= '9')
                        whole = whole * 10 + (*text++ - '0');
                if (*text == '.' && text[1] >= '0' && text[1] <= '9')
                        tenth = text[1] - '0';
                sum += whole * 10 + tenth;
        }
        return sum;
}

// A press of the pointer on a place of the window: its motion there, the button down and up.
static fn drive_click(int x, int y)
{
        struct window_event over = {WINDOW_EVENT_MOTION, 0, x, y};
        struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
        struct window_event lift = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};

        settings_event_at(&over);
        settings_event_at(&land);
        settings_event_at(&lift);
}

// The kit's fold run from now for a time, a frame at a time, as the window's loop steps it (settings_frame).
static fn drive_fold_run(b64 for_ns)
{
        b64 from = (b64)ui_kit.now;

        for (b64 at = from; at <= from + for_ns; at += 16666667)
        {
                ui_kit.now = (positive)at;
                ui_kit.busy = 0;
                ui_list_fold_step(&settings.ui, &settings.list); // the frame steps the folds (settings_frame does, once a frame)
        }
}

/*
        The action rows and a row's copies, drawn into the window's pixels as the drive rows are (check_settings_drive_rows runs this
        on its window). Every action of the table is the kit's button at its row's end; a press, Enter and a finger's tap each run it,
        once: while its verb waits it says Running, greyed, and a second asks nothing. The right button on a row with a value opens
        its copies, and so do the menu key and Shift-F10; Copy value puts the value as the row shows it on the clipboard, byte for
        byte, and Copy label and value the label with it. A virtual device's right button copies too, and changes nothing.
*/
static int actions_first(unsigned char kind, b32 reads, b32 deliberate)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].kind == kind && settings_rows[at].output == SETTINGS_OUT_NONE && (settings_rows[at].read[0] != null) == reads &&
                    (!deliberate || settings_rows[at].deliberate))
                        return (int)at;
        return -1;
}

static positive actions_clock = 5000000000ull;

// A row's group in the window, the keys on the row and the row in view, drawn whole.
static fn actions_show(int row)
{
        ui_kit.rtl = 0;
        ui_kit_settle();
        settings.ui.scale = 100;
        settings_size(SETTINGS_WIDTH, 760);
        settings_filter_clear();
        settings.page.card = 0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1;
        settings.focused = 0, settings.list.hover = -1, settings.page.pane = 1;
        settings.page.chosen = settings_group_of(row);
        settings_refilter();
        settings.list.focus = row;
        ui_list_reveal(&settings.ui, &settings.list);
        ui_scroll_set(&settings.list.scroll, (int)(scroll_aim(&settings.list.scroll) >> 8));
        // Drawn twice, the second a second on, when what the first set going (a button's fill, which comes in over .2s) is at rest.
        for (int frame = 0; frame < 2; frame++)
        {
                actions_clock += 1000000000ull;
                ui_kit.now = actions_clock;
                ui_kit.busy = 0;
                settings_draw_rows(settings_frames_whole, 0, 0);
        }
}

// The middle of a row as the list has it, in the window's pixels.
static fn actions_middle(int row, int address_to x, int address_to y)
{
        int at = settings_pos(row);

        *x = (settings.page.left + settings.page.right) / 2;
        *y = at >= 0 ? ui_list_row_y(&settings.list, at) + settings.list.tall[at] / 2 : -1;
}

// A press of a button at a point, as the hand sends it (flags: a finger's, a virtual device's).
static fn actions_press(int button, int x, int y, unsigned int flags)
{
        struct window_event over = {WINDOW_EVENT_MOTION, flags & WINDOW_KEY_TOUCH, x, y};
        struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | flags, button, 0};
        struct window_event lift = {WINDOW_EVENT_BUTTON, flags, button, 0};

        settings_event_at(&over);
        settings_event_at(&land);
        settings_event_at(&lift);
}

// An item of the menu open, chosen by a press on it (flags as above), as the popup's runner answers it.
static fn actions_pick(int k, unsigned int flags)
{
        int row = popmenu_row(&settings.popup.menu), x = settings.popup.menu.width / 2;
        int y = ui_px(&settings.ui, UI_MENU_PAD) + ui_popmenu_start(&settings.popup.menu, k) + row / 2;
        struct window_event over = {WINDOW_EVENT_MOTION, flags & WINDOW_KEY_TOUCH, x, y};
        struct window_event land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN | flags, UI_POINTER_LEFT, 0};
        struct window_event lift = {WINDOW_EVENT_BUTTON, flags, UI_POINTER_LEFT, 0};

        settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, &over, x, y));
        settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, &land, x, y));
        settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, &lift, x, y));
}

// The place in the menu open of the item whose id is id, -1 none.
static int actions_item(int id)
{
        for (int k = 0; k < settings.popup.menu.count; k++)
                if (settings.popup.menu.items[k].id == id && !(settings.popup.menu.items[k].flags & UI_MENU_SEPARATOR))
                        return k;
        return -1;
}

// A point in a cell of a row of columns: k the column, as the header lays it out, a few pixels in from its start.
static fn actions_cell(const struct ui_columns address_to c, int row, int k, int address_to x, int address_to y)
{
        int at = settings_pos(row);
        *x = c->at[k] + ui_dev(&settings.ui, 6);
        *y = at >= 0 ? ui_list_row_y(&settings.list, at) + settings.list.tall[at] / 2 : -1;
}

// What the window last put on the clipboard is want, exactly, and it was put there since serial.
static b32 actions_clipboard(unsigned int serial, const char address_to want)
{
        char got[512];
        long size;

        if (settings.own.table->clip_offer_serial[settings.own.row] == serial)
                return false;
        size = (long)system_call_4(syscall(pread64), (positive)settings.own.table->clip_offer_fd[settings.own.row], (positive)got, sizeof(got) - 1, 0);
        if (size < 0)
                return false;
        got[size] = 0;
        return string_equals((string_address)got, (string_address)want);
}

// Whether the menu open is a row's copies: its two items in order, each choosable.
static b32 actions_copies(int row)
{
        const struct ui_popmenu address_to m = &settings.popup.menu;

        return settings.popup.open && settings.popup.list == &settings.list && settings.popup.list_row == row && m->count == 2 && m->items[0].id == SETTINGS_COPY_VALUE &&
               m->items[1].id == SETTINGS_COPY_BOTH && !(m->items[0].flags & UI_MENU_DISABLED) &&
               string_equals((string_address)m->items[0].label, (string_address)WORDS(settings_copy_value)) &&
               string_equals((string_address)m->items[1].label, (string_address)WORDS(settings_copy_both));
}

static fn check_settings_actions(void)
{
        int discover = actions_first(SETTINGS_ACTION, false, false), clock = actions_first(SETTINGS_ACTION, true, false);
        int info = actions_first(SETTINGS_INFO, true, false), server = actions_first(SETTINGS_SWITCH, true, true), name = SETTINGS_NAME_ROW;
        int x = 0, y = 0, actions = 0, buttons = 0;
        b32 drawn = false, pressed = false, once = false, busy = false, entered = false, tapped = false, stops = false, clocked = false;
        b32 opened = false, copied = false, both = false, keyed = false, shifted = false, plain = false, virtual_copy = false, field = false;
        unsigned int serial;
        struct ui_row row;
        char line[256];

        if (discover < 0 || clock < 0 || info < 0 || server < 0)
        {
                check("settings actions: the table has an action, a clock, an info row and a deliberate switch", false);
                return;
        }
        settings.runner.child = 1, settings.job = SETTINGS_JOB_HELP, settings.job_sets = false, settings.queued = 0; // a stand-in: no verb is started
        popup_fake.screen = SETTINGS_HEIGHT, popup_fake.themed = ui_kit.themed;
        ui_confirm_drop(&settings.confirm);

        // Every action of the table is a button at its row's end, with words, its label beside it.
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                if (settings_rows[at].kind != SETTINGS_ACTION || settings_rows[at].output != SETTINGS_OUT_NONE)
                        continue;
                actions++;
                settings.state[at] = SETTINGS_READ;
                memory_zero(&row, sizeof(row));
                settings_describe((int)at, &row);
                buttons += row.kind == UI_ROW_BUTTON && row.words && row.words[0] && row.label && !row.value && !row.dim;
        }
        check("settings actions: every action row of the table is a button at the row's end with its words, the row's label beside it",
              actions >= 3 && buttons == actions);

        // Drawn: the kit's outlined button, its outline and its words in the pixels at the row's end.
        actions_show(discover);
        {
                const struct ui_control address_to c = ui_list_control(&settings.list, discover, 1);
                positive inked = 0;

                if (c && c->kind == UI_BUTTON && c->variant == UI_BUTTON_PRIMARY && c->w > 0 && c->h > 0 && c->x > 8)
                {
                        unsigned int ground = settings_frames_whole[(positive)(c->y + c->h / 2) * SETTINGS_FRAMES_W + (positive)(c->x - 6)];

                        for (int py = c->y; py < c->y + c->h; py++)
                                for (int px = c->x; px < c->x + c->w; px++)
                                        inked += settings_frames_whole[(positive)py * SETTINGS_FRAMES_W + (positive)px] != ground;
                        drawn = string_equals((string_address)c->label, (string_address)WORDS(settings_button_discover)) && inked > (positive)(c->w * c->h / 2);
                        x = c->x + c->w / 2, y = c->y + c->h / 2;
                }
                string_format(log, "  settings actions: the button %p by %p, %p pixels inked\n", (positive)(c ? c->w : 0), (positive)(c ? c->h : 0), inked);
        }
        check("settings actions: Discover now draws the kit's button, filled in the accent, with its words at its row's end", drawn);

        // A press on the button runs it, once: a second press while it waits asks nothing, and the button says Running, greyed.
        actions_press(UI_POINTER_LEFT, x, y, 0);
        pressed = settings.queued == 1 && settings.queue[0].row == discover;
        actions_press(UI_POINTER_LEFT, x, y, 0);
        once = settings.queued == 1;
        memory_zero(&row, sizeof(row));
        settings_describe(discover, &row);
        actions_show(discover);
        busy = row.kind == UI_ROW_BUTTON && row.dim && string_equals((string_address)row.words, (string_address)WORDS(settings_button_running)) &&
               ui_list_control(&settings.list, discover, 1)->disabled;
        // So is it while it runs.
        settings.queued = 0, settings.job = discover, settings.job_sets = true;
        actions_press(UI_POINTER_LEFT, x, y, 0);
        busy = busy && settings.queued == 0;
        settings.job = SETTINGS_JOB_HELP, settings.job_sets = false;
        check("settings actions: a press on the button runs the row's verb", pressed);
        check("settings actions: and while it waits or runs a second press asks nothing, the button saying Running and greyed", once && busy);
        // Enter on the row, and a finger's tap, run it the same: once each.
        settings.queued = 0;
        actions_show(discover);
        settings_press(UI_KEY_ENTER);
        entered = settings.queued == 1 && settings.queue[0].row == discover;
        settings_press(UI_KEY_ENTER);
        entered = entered && settings.queued == 1;
        settings.queued = 0;
        actions_show(discover);
        actions_press(UI_POINTER_LEFT, x, y, WINDOW_KEY_TOUCH);
        tapped = settings.queued == 1 && settings.queue[0].row == discover;
        actions_press(UI_POINTER_LEFT, x, y, WINDOW_KEY_TOUCH);
        tapped = tapped && settings.queued == 1;
        settings.queued = 0;
        check("settings actions: Enter on the row runs it, and a finger's tap on it, each once while it waits", entered && tapped);
        // A pairing that waits says Stop and is not greyed (a press stops the wait); the clock's reading is said before its button.
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_pair_row((int)at))
                {
                        settings.job = (int)at, settings.job_sets = true;
                        memory_zero(&row, sizeof(row));
                        settings_describe((int)at, &row);
                        stops = row.kind == UI_ROW_BUTTON && !row.dim && string_equals((string_address)row.words, (string_address)WORDS(settings_button_stop));
                        settings.job = SETTINGS_JOB_HELP, settings.job_sets = false;
                }
        settings.state[clock] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[clock], (string_address) "2026-11-16 09:30:00 CET", SETTINGS_VALUE);
        memory_zero(&row, sizeof(row));
        settings_describe(clock, &row);
        clocked = row.kind == UI_ROW_BUTTON && row.note && string_equals((string_address)row.note, (string_address) "2026-11-16 09:30:00 CET") &&
                  string_equals((string_address)row.words, (string_address)WORDS(settings_button_set));
        check("settings actions: a pairing that waits says Stop, not greyed, and the clock says its reading before its button", stops && clocked);

        // The right button on an info row opens its copies at the pointer; Copy value puts its line on the clipboard as it is.
        // The copies go to the window the row is in: the table names the process that owns the row, and a forked group is another process.
        if (settings.own.table->capacity <= settings.own.row)
                settings.own.table->capacity = 8; // the scratch table has room for no row until it is given some, as canvas.c gives its clip table eight
        if (settings.own.table->rows <= settings.own.row)
                settings.own.table->rows = 8;
        settings.own.table->pid[settings.own.row] = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
        settings.state[info] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[info], (string_address) "203.0.113.7 via stun, 2 min ago", SETTINGS_VALUE);
        actions_show(info);
        actions_middle(info, &x, &y);
        actions_press(UI_POINTER_RIGHT, x, y, 0);
        opened = actions_copies(info) && settings.popup.menu.hot < 0;
        serial = settings.own.table->clip_offer_serial[settings.own.row];
        actions_pick(0, 0);
        copied = !settings.popup.open && !settings.popup.list && actions_clipboard(serial, "203.0.113.7 via stun, 2 min ago") &&
                 string_equals((string_address)settings.status, (string_address)WORDS(settings_copied)) && settings.queued == 0;
        actions_press(UI_POINTER_RIGHT, x, y, 0);
        serial = settings.own.table->clip_offer_serial[settings.own.row];
        actions_pick(1, 0);
        language_format(line, sizeof(line), WORDS(settings_copy_line), settings_label_window(info), "203.0.113.7 via stun, 2 min ago");
        both = actions_clipboard(serial, line) && settings.queued == 0;
        check("settings copies: the right button on a row with a value opens Copy value and Copy label and value at the pointer", opened);
        check("settings copies: Copy value puts the row's value on the clipboard, exactly", copied);
        check("settings copies: and Copy label and value its label and its value", both);
        // A copy the clipboard declines says it could not be copied, and not that it was: the same row, its window no longer this process's.
        {
                unsigned int owner = settings.own.table->pid[settings.own.row];
                b32 declined_says = false;

                settings.own.table->pid[settings.own.row] = 0;
                actions_show(info);
                actions_middle(info, &x, &y);
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                actions_pick(0, 0);
                declined_says = string_equals((string_address)settings.status, (string_address)WORDS(settings_copy_failed)) &&
                                !string_equals((string_address)settings.status, (string_address)WORDS(settings_copied));
                settings.own.table->pid[settings.own.row] = owner;
                check("settings copies: a copy the clipboard declines says it could not be copied, and not that it was copied", declined_says);
        }
        // The menu key, and Shift-F10, open them under the row the keys are on, the first lit: Enter copies.
        actions_show(info);
        settings_press(SETTINGS_KEY_MENU);
        keyed = actions_copies(info) && settings.popup.menu.hot == 0;
        serial = settings.own.table->clip_offer_serial[settings.own.row];
        settings_press(UI_KEY_ENTER);
        keyed = keyed && !settings.popup.list && actions_clipboard(serial, "203.0.113.7 via stun, 2 min ago") && settings.queued == 0;
        settings_chord(SETTINGS_KEY_F10, 0, WINDOW_KEY_SHIFT);
        shifted = actions_copies(info);
        settings_press(UI_KEY_ESCAPE);
        shifted = shifted && !settings.popup.open && !settings.popup.list && settings.queued == 0;
        check("settings copies: the menu key opens them under the row the keys are on, the first lit, and Enter copies", keyed);
        check("settings copies: Shift-F10 opens them too, and Escape closes them with nothing done", shifted);
        // A row with no value offers its words alone, and runs nothing.
        actions_show(discover);
        actions_middle(discover, &x, &y);
        actions_press(UI_POINTER_RIGHT, x, y, 0);
        plain = settings.popup.open && settings.popup.list == &settings.list && actions_item(UI_LIST_MENU_COPY) == 0 && actions_item(UI_LIST_MENU_REMOVE) < 0 &&
                settings.queued == 0;
        serial = settings.own.table->clip_offer_serial[settings.own.row];
        actions_pick(0, 0);
        plain = plain && actions_clipboard(serial, settings_label_window(discover)) && settings.queued == 0;
        check("settings copies: the right button on a row with no value offers Copy of its words alone, and runs nothing", plain);

        // A virtual device's right button on the SSH server's switch copies its word, and the switch is as it was.
        settings.state[server] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[server], (string_address) "off", SETTINGS_VALUE);
        actions_show(server);
        actions_middle(server, &x, &y);
        actions_press(UI_POINTER_RIGHT, x, y, WINDOW_EVENT_VIRTUAL);
        virtual_copy = actions_copies(server);
        serial = settings.own.table->clip_offer_serial[settings.own.row];
        actions_pick(0, WINDOW_EVENT_VIRTUAL);
        virtual_copy = virtual_copy && actions_clipboard(serial, "off") && settings.queued == 0 && settings.state[server] == SETTINGS_READ &&
                       string_equals((string_address)settings.value[server], (string_address) "off");
        check("settings copies: a virtual device's right button copies a switch's word and changes nothing", virtual_copy);

        // A row typed in: its field's menu is the kit's items, then the row's copies, which copy what the row holds and keep the typing.
        settings.state[name] = SETTINGS_READ;
        string_copy_bounded((string_address)settings.value[name], (string_address) "patient-cricket", SETTINGS_VALUE);
        actions_show(name);
        settings_activate(name, 1);
        settings_chord(SETTINGS_KEY_F10, 0, WINDOW_KEY_SHIFT);
        {
                const struct ui_popmenu address_to m = &settings.popup.menu;

                field = settings_editing() == name && settings.popup.open && settings.popup.field == &settings.edit && settings.popup.list == &settings.list && m->count > 3 &&
                        m->items[0].id == UI_TEXT_MENU_UNDO && (m->items[m->count - 3].flags & UI_MENU_SEPARATOR) &&
                        m->items[m->count - 2].id == SETTINGS_COPY_VALUE && m->items[m->count - 1].id == SETTINGS_COPY_BOTH;
        }
        serial = settings.own.table->clip_offer_serial[settings.own.row];
        ui_popmenu_close(&settings.popup);
        settings_chose(SETTINGS_COPY_VALUE);
        field = field && actions_clipboard(serial, "patient-cricket") && settings_editing() == name && settings.queued == 0;
        check("settings copies: a row typed in has its field's items and then the row's copies, which copy its value and keep the typing", field);

        ui_popmenu_close(&settings.popup);
        settings.popup.list = null, settings.popup.list_row = -1;
        settings_edit_end();

        // File-explorer-style details lists & base context menu actions for External devices and Waterlink linked machines.
        {
                static const char ext_out[] =
                    "  1-2 usb 0627:0002 Zebra blocked\n"
                    "  1-1 usb 0627:0001 Alpha allowed\n";
                static const char link_out[] =
                    "  zeta  11223344...  may run files\n"
                    "      heard 5s ago from 10.0.0.2:41900  next: moonwater link zeta\n"
                    "  alpha  a1b2c3d4...  may run terminal files log\n"
                    "      open now from 192.168.1.5:41900  next: moonwater link alpha\n";
                unsigned short saved_c2 = settings.item_count[2], saved_c3 = settings.item_count[3];
                int ext_row = (int)SETTINGS_LIST_ID(2, 0);
                int link_row = (int)SETTINGS_LIST_ID(3, 0);
                // The copies go to the window the row is in: as the copies block above gives its scratch table room, a row and the process that owns it.
                if (settings.own.table->capacity <= settings.own.row)
                        settings.own.table->capacity = 8;
                if (settings.own.table->rows <= settings.own.row)
                        settings.own.table->rows = 8;
                settings.own.table->pid[settings.own.row] = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
                b32 ext_table = false, ext_sort = false, ext_menu = false;
                b32 link_table = false, link_menu = false, link_remove = false, link_del = false, link_named = false, link_details = false;

                // External devices: details columns, dense rows, column sort, right-click Allow/Deny + Copy + Copy row.
                memory_copy(settings_large->said, ext_out, sizeof(ext_out) - 1);
                settings.runner.length = sizeof(ext_out) - 1;
                settings_feed_hardware();
                settings.page.chosen = settings_group_of(ext_row);
                settings_rebuild();
                actions_show(ext_row);
                {
                        int at = settings_pos(ext_row);
                        struct ui_row described;

                        ui_list_describe(&settings.list, ext_row, &described);
                        ext_table = at >= 0 && (settings.list.starts[at] & UI_LIST_COLUMNS) != 0 &&
                                    described.columns == &settings_large->columns[2] &&
                                    settings.list.tall[at] == ui_dev(&settings.ui, UI_ROW_DENSE_TABS) &&
                                    described.cells[1] && string_equals((string_address)described.cells[1], (string_address) "USB 1-2") &&
                                    described.cells[2] && string_equals((string_address)described.cells[2], (string_address)WORDS(settings_state_blocked));
                        if (at >= 0)
                        {
                                int hx = settings_large->columns[2].at[0] + 12;
                                int hy = ui_list_row_y(&settings.list, at) - ui_dev(&settings.ui, UI_COLUMNS_H) / 2;

                                actions_press(UI_POINTER_LEFT, hx, hy, 0);
                                // The sort orders the rows in view (the kit's ids); the items stay as the feed gave them, so Alpha is the first row shown.
                                int alpha = -1, first = -1;
                                for (int k = 0; k < settings.item_count[2]; k++)
                                        if (string_equals((string_address)settings.item[2][k].label, (string_address) "Alpha"))
                                                alpha = k;
                                ext_sort = settings_large->columns[2].sorted == 0 && !settings_large->columns[2].descending && alpha >= 0;
                                for (int k = 0; k < settings.item_count[2] && ext_sort; k++)
                                {
                                        int pos = settings_pos(SETTINGS_LIST_ID(2, k));
                                        if (k != alpha && settings.item[2][k].where == 1 && pos >= 0)
                                                ext_sort = settings_pos(SETTINGS_LIST_ID(2, alpha)) < pos;
                                }
                                first = alpha;
                                ext_sort = ext_sort && first >= 0;
                                if (alpha >= 0)
                                        ext_row = (int)SETTINGS_LIST_ID(2, alpha); // the menu is on the row the sort shows first, Alpha (the items stay in the feed's order)
                        }
                }
                actions_show(ext_row);
                // The right button on the row's second cell (USB 1-1): the app's Allow or Deny first, then Copy and that column's title,
                // the row's own Copy and Copy row.
                actions_cell(&settings_large->columns[2], ext_row, 1, &x, &y);
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                {
                        const struct ui_popmenu address_to m = &settings.popup.menu;
                        char named[96];

                        language_format(named, sizeof(named), WORDS(menu_copy_named), settings_large->columns[2].title[1]);
                        ext_menu = settings.popup.open && settings.popup.list == &settings.list && settings.popup.list_row == ext_row &&
                                   m->count == 5 && m->items[0].id == SETTINGS_MENU_ACT && (m->items[1].flags & UI_MENU_SEPARATOR) &&
                                   m->items[2].id == UI_LIST_MENU_COPY_CELL && string_equals((string_address)m->items[2].label, (string_address)named) &&
                                   m->items[3].id == UI_LIST_MENU_COPY && m->items[4].id == UI_LIST_MENU_COPY_ROW;
                }
                serial = settings.own.table->clip_offer_serial[settings.own.row];
                actions_pick(2, 0);
                ext_menu = ext_menu && actions_clipboard(serial, "USB 1-1");
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                serial = settings.own.table->clip_offer_serial[settings.own.row];
                actions_pick(3, 0);
                ext_menu = ext_menu && actions_clipboard(serial, "Alpha");
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                serial = settings.own.table->clip_offer_serial[settings.own.row];
                actions_pick(4, 0);
                ext_menu = ext_menu && actions_clipboard(serial, "Alpha: USB 1-1, allowed");
                settings.queued = 0;
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                actions_pick(0, 0);
                ext_menu = ext_menu && settings.queued == 1 && settings.queue[0].row == SETTINGS_JOB_ACT &&
                           string_equals((string_address)settings.queue[0].value, (string_address) "x\n1-1");
                settings.queued = 0;

                // Waterlink linked machines: structured Name/Access/Status details table, dense 28px rows, Copy / Copy row / Remove + Delete twice.
                memory_copy(settings_large->said, link_out, sizeof(link_out) - 1);
                settings.runner.length = sizeof(link_out) - 1;
                settings_feed_links();
                settings.page.chosen = settings_group_of(link_row);
                settings_rebuild();
                actions_show(link_row);
                {
                        int at = settings_pos(link_row);
                        struct ui_row described;

                        ui_list_describe(&settings.list, link_row, &described);
                        link_table = at >= 0 && (settings.list.starts[at] & UI_LIST_COLUMNS) != 0 &&
                                     described.columns == &settings_large->columns[3] &&
                                     settings.list.tall[at] == ui_dev(&settings.ui, UI_ROW_DENSE) &&
                                     string_equals((string_address)settings.item[3][0].label, (string_address) "zeta") &&
                                     string_equals((string_address)settings.item[3][0].access, (string_address) "run files") &&
                                     string_equals((string_address)settings.item[3][0].detail, (string_address) "heard 5s ago from 10.0.0.2:41900");
                }
                // The right button on the Address cell: Copy Address, the row's Copy and Copy row, and Remove (the row has no button: its
                // removal is its menu's); on the Name cell, Copy Name takes the place of Copy, the same words.
                actions_cell(&settings_large->columns[3], link_row, 1, &x, &y);
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                {
                        const struct ui_popmenu address_to m = &settings.popup.menu;
                        char named[96];

                        language_format(named, sizeof(named), WORDS(menu_copy_named), settings_large->columns[3].title[1]);
                        link_menu = settings.popup.open && settings.popup.list == &settings.list && settings.popup.list_row == link_row &&
                                    m->count == 5 && m->items[0].id == UI_LIST_MENU_COPY_CELL && string_equals((string_address)m->items[0].label, (string_address)named) &&
                                    m->items[1].id == UI_LIST_MENU_COPY && m->items[2].id == UI_LIST_MENU_COPY_ROW &&
                                    (m->items[3].flags & UI_MENU_SEPARATOR) && m->items[4].id == UI_LIST_MENU_REMOVE;
                }
                serial = settings.own.table->clip_offer_serial[settings.own.row];
                actions_pick(0, 0);
                link_menu = link_menu && actions_clipboard(serial, "10.0.0.2:41900");
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                serial = settings.own.table->clip_offer_serial[settings.own.row];
                actions_pick(1, 0);
                link_menu = link_menu && actions_clipboard(serial, "zeta");
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                serial = settings.own.table->clip_offer_serial[settings.own.row];
                actions_pick(2, 0);
                link_menu = link_menu && actions_clipboard(serial, "zeta: 10.0.0.2:41900");
                {
                        int nx, ny;
                        actions_cell(&settings_large->columns[3], link_row, 0, &nx, &ny);
                        actions_press(UI_POINTER_RIGHT, nx, ny, 0);
                        link_named = settings.popup.open && actions_item(UI_LIST_MENU_COPY_CELL) == 0 && actions_item(UI_LIST_MENU_COPY) < 0 &&
                                     actions_item(UI_LIST_MENU_COPY_ROW) == 1;
                        serial = settings.own.table->clip_offer_serial[settings.own.row];
                        actions_pick(0, 0);
                        link_named = link_named && actions_clipboard(serial, "zeta");
                }
                // A machine has no fold: its address is its row's cell and its grants its checks, and a press on it opens nothing under it.
                {
                        struct ui_row described;
                        int before = settings.list.count, my = ui_list_row_y(&settings.list, settings_pos(link_row)) + settings.list.tall[settings_pos(link_row)] / 2;

                        ui_list_describe(&settings.list, link_row, &described);
                        actions_press(UI_POINTER_LEFT, (settings.page.left + settings.page.right) / 2, my, 0);
                        actions_show(link_row);
                        link_details = !described.expander && described.cells[1] && string_equals((string_address)described.cells[1], (string_address) "10.0.0.2:41900") &&
                                       described.cell_check == 0xfu << SETTINGS_LINK_GRANT_COLUMN && settings.list.count == before;
                }
                settings.queued = 0;
                actions_press(UI_POINTER_RIGHT, x, y, 0);
                actions_pick(4, 0);
                {
                        string_address words[SETTINGS_WORDS];

                        link_remove = settings.queued == 1 && settings.queue[0].row == SETTINGS_JOB_ACT &&
                                      string_equals((string_address)settings.queue[0].value, (string_address) "r\nzeta");
                        settings.moonwater = (string_address) "/moonwater";
                        string_copy_bounded((string_address)settings.job_value, (string_address) "r\nzeta", sizeof(settings.job_value));
                        settings_act_words(words);
                        link_remove = link_remove && words[0] && string_equals(words[1], (string_address) "link") &&
                                      string_equals(words[2], (string_address) "remove") && string_equals(words[3], (string_address) "zeta") && !words[4];
                }
                settings.queued = 0;
                ui_confirm_drop(&settings.confirm);
                settings.list.focus = link_row + 1; // alpha
                settings_press(UI_KEY_DELETE);
                link_del = settings.queued == 0 && settings.confirm.armed;
                settings_press(UI_KEY_DELETE);
                link_del = link_del && settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "r\nalpha");

                check("settings lists: External devices use file-explorer-style columns, dense rows, and sortable headers", ext_table && ext_sort);
                check("settings lists: External devices right-click menu offers Allow/Deny, Copy Port (the cell under the hand), Copy, and Copy row", ext_menu);
                check("settings lists: Waterlink linked machines use file-explorer-style columns and dense 28px rows", link_table);
                check("settings lists: Waterlink's right-click menu offers Copy Address, Copy, Copy row, and Remove (link remove)", link_menu && link_remove);
                check("settings lists: Delete twice on a Waterlink linked machine confirms and removes it", link_del);
                // A cell that is a check (the kit's, ui_row.cell_check): Waterlink's grants, a column each. Drawn as a box, on in the accent
                // (zeta's Files) and a ring off (its Log); a press on a box is the list's toggled, which Settings takes at once as the grant's verb
                // (Shell: link allow zeta shell); a press in its cell but off the box's reach is not, and a press on it let go off it is not.
                {
                        b32 toggles = false, misses = true, drawn_on = false, ringed = false;
                        const struct ui_columns address_to c = &settings_large->columns[3];
                        int at, bx, by, side = ui_dev(&settings.ui, UI_CELL_CHECK), on_k = SETTINGS_LINK_GRANT_COLUMN, press_k = SETTINGS_LINK_GRANT_COLUMN + 2,
                                        off_k = SETTINGS_LINK_GRANT_COLUMN + 3;
                        positive accent = 0;

                        settings.queued = 0, ui_confirm_drop(&settings.confirm), settings_link_global_grants = 0x0fu;
                        settings_rebuild();
                        actions_show(link_row);
                        at = settings_pos(link_row), bx = ui_cell_check_x(&settings.ui, c, on_k), by = ui_list_row_y(&settings.list, at) + (settings.list.tall[at] - side) / 2;
                        for (int py = by; py < by + side; py++)
                                for (int px = bx; px < bx + side; px++)
                                        accent += (settings_frames_whole[(positive)py * SETTINGS_FRAMES_W + (positive)px] & 0xffffffu) == (ui_role(UI_PRIMARY) & 0xffffffu);
                        drawn_on = accent >= (positive)(side * side / 3);
                        bx = ui_cell_check_x(&settings.ui, c, press_k);
                        settings.list.toggled = 0;
                        actions_press(UI_POINTER_LEFT, bx + side / 2, by + side / 2, 0);
                        toggles = settings.list.toggled == 0 && settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "l\nzeta\nshell");
                        settings.queued = 0, settings.item[3][0].grants &= (unsigned char)~4u;
                        actions_show(link_row);
                        if (c->at[press_k] + ui_columns_end(c, press_k) > bx + side + ui_dev(&settings.ui, UI_CELL_CHECK_REACH) + 2)
                                actions_press(UI_POINTER_LEFT, bx + side + ui_dev(&settings.ui, UI_CELL_CHECK_REACH) + 1, by + side / 2, 0), misses = settings.queued == 0;
                        {
                                struct window_event over = {WINDOW_EVENT_MOTION, 0, bx + side / 2, by + side / 2}, land = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
                                struct window_event away = {WINDOW_EVENT_MOTION, 0, 2, by + side / 2}, lift = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
                                settings_event_at(&over), settings_event_at(&land), settings_event_at(&away), settings_event_at(&lift);
                                misses = misses && settings.queued == 0 && settings.list.cell_down == 0;
                        }
                        settings.queued = 0;
                        actions_show(link_row);
                        bx = ui_cell_check_x(&settings.ui, c, off_k);
                        {
                                positive ring = 0, filled = 0;
                                for (int py = by; py < by + side; py++)
                                        for (int px = bx; px < bx + side; px++)
                                        {
                                                unsigned int pixel = settings_frames_whole[(positive)py * SETTINGS_FRAMES_W + (positive)px] & 0xffffffu;
                                                filled += pixel == (ui_role(UI_PRIMARY) & 0xffffffu);
                                                ring += py == by + side / 2 && (px == bx || px == bx + 1) && pixel != (settings_frames_whole[(positive)py * SETTINGS_FRAMES_W + (positive)(bx + side / 2)] & 0xffffffu);
                                        }
                                ringed = filled == 0 && ring > 0;
                        }
                        settings.list.toggled = 0;
                        settings_rebuild();
                        check("kit cell checks: a cell that is a check is a box, filled in the accent when on and a ring when off", drawn_on && ringed);
                        check("kit cell checks: a press on a grant's box is the list's toggled, which Settings takes as that grant's verb", toggles);
                        check("kit cell checks: a press off the box's reach, or let go off it, turns nothing", misses);
                }
                check("settings lists: the right button on a cell offers Copy and its column's title, which copies that cell, and on the Name cell takes Copy's place", link_menu && link_named);
                check("settings lists: a machine has no fold: its address is its row's, its grants its checks, and a press opens nothing under it", link_details);
                //      The kit's managed rows: a machine linked has the remove button (link remove, asking first); after them the add row,
                //      whose ways are the two rows that are no rows of their own now (.way): a code shown (link pair) and one typed.
                {
                        int pair = settings_way_row(1), typed = settings_way_row(2), add_id = (int)SETTINGS_LIST_ID(3, 2), saved_job = settings.job;
                        b32 saved_sets = settings.job_sets, asked, ways_ok, field_ok, stop_ok;
                        struct ui_row linked, add;

                        settings.queued = 0, ui_confirm_drop(&settings.confirm);
                        memory_zero(&linked, sizeof(linked)), settings_describe(link_row, &linked);
                        memory_zero(&add, sizeof(add)), settings_describe(add_id, &add);
                        check("settings lists: Waterlink's two way rows are no rows of the list, and after the machines is the add row with their two ways",
                              pair >= 0 && typed >= 0 && settings_pos(pair) < 0 && settings_pos(typed) < 0 && settings_pos(add_id) >= 0 &&
                                  settings.item_count[3] == 3 && settings.item[3][2].kind == SETTINGS_ADD && add.kind == UI_ROW_ADD &&
                                  string_equals((string_address)add.words, (string_address) "Show a code|Type another machine's code") &&
                                  string_equals((string_address)add.label, (string_address) "Link a machine"));
                        //      Its grants are checks in their columns, on where the machine may (zeta: run files), and a check turned is the
                        //      grant's verb, a hand's only (allowing is deliberate); its state is the Status column.
                        {
                                unsigned int grants = 0xfu << SETTINGS_LINK_GRANT_COLUMN, on = 0x3u << SETTINGS_LINK_GRANT_COLUMN;
                                b32 cells, virtual_kept, turned;
                                settings_link_global_grants = 0x0fu; // this machine allows every grant
                                memory_zero(&linked, sizeof(linked)), settings_describe(link_row, &linked);
                                cells = linked.cell_check == grants && linked.cell_on == on && linked.cells[1] &&
                                        string_equals((string_address)linked.cells[1], (string_address) "10.0.0.2:41900") && !linked.expander;
                                settings.hand = false;
                                settings.list.toggled = link_row + 1, settings.list.toggled_cell = SETTINGS_LINK_GRANT_COLUMN + 2, settings_managed();
                                virtual_kept = settings.queued == 0 && !(settings.item[3][0].grants & 4u) && !settings.list.toggled;
                                settings.hand = true;
                                settings.list.toggled = link_row + 1, settings.list.toggled_cell = SETTINGS_LINK_GRANT_COLUMN + 2, settings_managed();
                                settings.hand = false;
                                turned = settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "l\nzeta\nshell") &&
                                         (settings.item[3][0].grants & 4u);
                                check("settings lists: a machine linked's grants are checks in their columns, and a check a hand turns is link allow",
                                      cells && virtual_kept && turned);
                                settings.queued = 0, settings.item[3][0].grants &= (unsigned char)~4u;
                                // A grant this machine allows none (shell, off globally) is a dim check, off, and a hand's press on it asks nothing.
                                {
                                        b32 dimmed;
                                        settings_link_global_grants = 0x0bu;
                                        memory_zero(&linked, sizeof(linked)), settings_describe(link_row, &linked);
                                        dimmed = linked.cell_check == grants && linked.cell_dim == 4u << SETTINGS_LINK_GRANT_COLUMN && !(linked.cell_on & 4u << SETTINGS_LINK_GRANT_COLUMN);
                                        settings.hand = true;
                                        settings.list.toggled = link_row + 1, settings.list.toggled_cell = SETTINGS_LINK_GRANT_COLUMN + 2, settings_managed();
                                        settings.hand = false, settings_link_global_grants = 0x0fu;
                                        check("settings lists: a grant this machine allows none is a dim check in a machine's row, and a press on it changes nothing",
                                              dimmed && settings.queued == 0 && !(settings.item[3][0].grants & 4u));
                                }
                                memory_zero(&linked, sizeof(linked)), settings_describe(link_row, &linked);
                        }
                        settings.list.removed = link_row + 1, settings_managed();
                        asked = settings.queued == 0;
                        {
                                struct ui_row asking;
                                memory_zero(&asking, sizeof(asking)), settings_describe(link_row, &asking);
                                asked = asked && asking.cells[1] && string_equals((string_address)asking.cells[1], (string_address)WORDS(settings_remove_again));
                        }
                        settings.list.removed = link_row + 1, settings_managed();
                        check("settings lists: a machine linked has a remove button, whose first press is said in its row, and the second is link remove",
                              linked.remove && asked && settings.queued == 1 && string_equals((string_address)settings.queue[0].value, (string_address) "r\nzeta"));
                        settings.queued = 0;
                        settings_way_taken(add_id, 0);
                        ways_ok = settings.queued == 1 && settings.queue[0].row == pair;
                        settings.queued = 0;
                        settings_way_taken(add_id, 1);
                        memory_zero(&add, sizeof(add)), settings_describe(add_id, &add);
                        field_ok = settings_editing() == add_id && add.kind == UI_ROW_FIELD &&
                                   string_equals((string_address)add.label, (string_address)language_of(settings_rows[typed].label));
                        ui_text_set(&settings.edit, (const p8 address_to) "laptop 4821", 11);
                        settings_edit_commit();
                        field_ok = field_ok && settings_editing() < 0 && settings.queued == 1 && settings.queue[0].row == typed &&
                                   string_equals((string_address)settings.queue[0].value, (string_address) "laptop 4821");
                        settings.queued = 0;
                        settings.job = pair, settings.job_sets = true;
                        memory_zero(&add, sizeof(add)), settings_describe(add_id, &add);
                        stop_ok = add.kind == UI_ROW_ADD && !add.words && string_equals((string_address)add.label, (string_address)WORDS(settings_link_stop));
                        //      link pair's words as waterlink's command.c says them: the code is the word after code, and the card of it opens.
                        {
                                static const char paired[] = "[Moonwater] code ABC-DEF; this machine is called left\n"
                                                             "[Moonwater] on the other machine, run: moonwater link left ABC-DEF\n";
                                positive kept_length = settings.runner.length;
                                bipolar kept_child = settings.runner.child;
                                memory_copy(settings_large->said, paired, sizeof(paired) - 1), settings.runner.length = sizeof(paired) - 1;
                                settings.runner.child = kept_child > 0 ? kept_child : 1; // the pairing is under way (no child is run here)
                                settings_pair_on = false, settings_waiting_say();
                                check("settings lists: a pairing's code is read from link pair's code line, and its card opens with it",
                                      settings_pair_on && settings.page.card && string_equals((string_address)settings_pair_code, (string_address) "ABC-DEF"));
                                settings_pair_on = false, settings.page.card = false, settings.runner.length = kept_length, settings.runner.child = kept_child;
                        }
                        settings.job = saved_job, settings.job_sets = saved_sets;
                        check("settings lists: Show a code is link pair's job, the other way the add row as a field whose name and code are link's, "
                              "and a pairing that waits makes the row Stop pairing",
                              ways_ok && field_ok && stop_ok);
                }

                settings.item_count[2] = saved_c2, settings.item_count[3] = saved_c3;
                memory_zero(settings_large->columns, sizeof(settings_large->columns));
                ui_popmenu_close(&settings.popup);
                settings.popup.list = null, settings.popup.list_row = -1;
        }
        popup_fake.screen = 0;
        settings.queued = 0, settings.runner.child = 0, settings.job_sets = false;
}

static fn check_settings_drive_rows(string_address regular, string_address bold)
{
        static font_face faces[2];
        static p8 atlas[1024 * 1024];
        static char sixteen[4096], nvme[1024], moved[1024];
        positive size = 0, bytes = compositor_map_file(regular, &size), bold_size = 0, bold_bytes = bold ? compositor_map_file(bold, &bold_size) : 0;
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        positive buffer = (positive)SETTINGS_FRAMES_W * SETTINGS_FRAMES_H * 4;
        long table = compositor_call(syscall(mmap), 0, WINDOWS_HEADER + 2 * buffer, 3, 0x4022, (positive)-1, 0);
        struct windows address_to window = (struct windows address_to)table;
        b32 least = true, fills = true, equal = true, drawn = false, free_track = false, apart = true, listed = false, last = false, tall = true, mirrored = true;
        b32 shut = false, pressed = false, keyed = false, sums = true, headed = false, columns = false, kept = false;
        int heights[2] = {0, 0}, widths[] = {SETTINGS_WIDTH, 420, 300};

        if (!bytes || (engine < 0 && engine > -4096) || (table < 0 && table > -4096) || !settings_map() ||
            font_load(&faces[0], (const p8 address_to)bytes, size, 0) != 0)
        {
                check("settings drive rows: the face, the engine and the window load", false);
                return;
        }
        settings_record_keep(&drive_settings_kept);
        memory_copy(drive_kit_kept, &ui_kit, sizeof(ui_kit));
        memory_copy(drive_items_kept, settings.item[2], sizeof(drive_items_kept));
        window->stride = SETTINGS_FRAMES_W, window->buffer = buffer, window->output_width = 4096, window->output_height = 4096;
        settings.own = (struct window_row){window, 0, -1, -1, WINDOWS_HEADER + 2 * buffer, buffer, SETTINGS_FRAMES_W, 4096, 4096};
        memory_zero(&settings.ui, sizeof(settings.ui));
        settings.ui.engine = (font_engine address_to)engine;
        font_start(settings.ui.engine, atlas, 1024, 1024);
        settings.ui.regular = &faces[0];
        settings.ui.bold = bold_bytes && font_load(&faces[1], (const p8 address_to)bold_bytes, bold_size, 0) == 0 ? &faces[1] : null;
        settings.ui.scale = 100, settings.ui.opaque = 1;
        settings.list.pool = null, settings.page.buffers.shown = -1, settings.page.pane = 0;
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 0);
        ui_inks(&settings.ui, 0x101012u);
        ui_kit_inks(&settings.ui);
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);

        // A disk of 2 TB: sixteen partitions, the free room before each and after the last (33 segments), one of 1 TB and some of 1 MiB,
        // and four partitions more that the host does not show.
        {
                unsigned long long at = 0, sizes[16] = {1048576ull, 536870912ull, 1000000000000ull, 1048576ull, 8000000000ull, 200000000000ull,
                                                        4000000000ull, 60000000000ull, 33554432ull, 300000000000ull, 2000000000ull, 120000000000ull,
                                                        16000000000ull, 100000000000ull, 50000000000ull, 10000000000ull};
                static const char address_to const roles[16] = {"other", "efi", "linux", "other", "swap", "linux", "linux", "linux",
                                                                 "other", "other", "other", "linux", "swap", "other", "linux", "other"};
                char number[24];

                string_copy_bounded((string_address)sixteen, (string_address) "  sdb drive 2000398 MB view only\n", sizeof(sixteen));
                for (int k = 0; k <= 16; k++)
                {
                        unsigned long long free = 2097152ull * (unsigned long long)(k + 1);

                        string_append_bounded((string_address)sixteen, (string_address) "    part ", sizeof(sixteen));
                        number[positive_into_string((p8 address_to)number, (positive)at)] = 0;
                        string_append_bounded((string_address)sixteen, (string_address)number, sizeof(sixteen));
                        string_append_bounded((string_address)sixteen, (string_address) " ", sizeof(sixteen));
                        number[positive_into_string((p8 address_to)number, (positive)free)] = 0;
                        string_append_bounded((string_address)sixteen, (string_address)number, sizeof(sixteen));
                        string_append_bounded((string_address)sixteen, (string_address) " free -\n", sizeof(sixteen));
                        at += free;
                        if (k == 16)
                                break;
                        string_append_bounded((string_address)sixteen, (string_address) "    part ", sizeof(sixteen));
                        number[positive_into_string((p8 address_to)number, (positive)at)] = 0;
                        string_append_bounded((string_address)sixteen, (string_address)number, sizeof(sixteen));
                        string_append_bounded((string_address)sixteen, (string_address) " ", sizeof(sixteen));
                        number[positive_into_string((p8 address_to)number, (positive)sizes[k])] = 0;
                        string_append_bounded((string_address)sixteen, (string_address)number, sizeof(sixteen));
                        string_append_bounded((string_address)sixteen, (string_address) " ", sizeof(sixteen));
                        string_append_bounded((string_address)sixteen, (string_address)roles[k], sizeof(sixteen));
                        string_append_bounded((string_address)sixteen, (string_address)(k == 1 ? " EFI_system_partition\n" : " -\n"), sizeof(sixteen));
                        at += sizes[k];
                }
                string_append_bounded((string_address)sixteen, (string_address) "    more 4\n", sizeof(sixteen));
        }
        // A disk of 1 TB with an EFI partition of 512 MB, its data, and the free room after them.
        string_copy_bounded((string_address)nvme,
                            (string_address) "  nvme0n1 drive 1000204 MB view only\n"
                                             "    part 1048576 536870912 efi EFI_system_partition\n"
                                             "    part 537919488 16777216 other Microsoft_reserved_partition\n"
                                             "    part 554696704 900000000000 other Basic_data_partition\n"
                                             "    part 900554696704 69000000000 linux Linux_filesystem\n"
                                             "    part 969554696704 30650189312 free -\n",
                            sizeof(nvme));

        // Every segment has a leaf: at least the least wide where 33 of them fit (the window's width, at scales 1 and 2), the strip over the
        // count each where they do not (420 and 300 wide); and the segments with their gaps are the strip, whole. The drive's row is as tall
        // as its line, its heading and its strip whatever the width, and its disclosure is the row after it.
        drive_open_all(sixteen, false);
        for (int w = 0; w < 3; w++)
                for (int scale = 1; scale <= 2; scale++)
                {
                        int sum = 0, smallest = 1 << 30, largest = 0, gap;

                        drive_show(widths[w], scale, false);
                        gap = ui_dev(&settings.ui, UI_SEGMENT_GAP);
                        if (!drive_lay() || drive.row.strip_count != 33)
                        {
                                least = fills = equal = tall = false;
                                continue;
                        }
                        for (int k = 0; k < 33; k++)
                        {
                                int width = drive.has_segment[k] ? drive.segment[k].w : 0;

                                sum += width;
                                smallest = width < smallest ? width : smallest;
                                largest = width > largest ? width : largest;
                                least &= drive.has_segment[k] && width > 0;
                        }
                        fills &= sum + 32 * gap == drive.strip_w;
                        // At the least the segments are shared by bytes: the largest is the widest, and ten times the least where the strip has
                        // twice the room the least widths take (the window's least width leaves a 16 partition strip only a little more than that).
                        if (33 * ui_dev(&settings.ui, UI_SEGMENT_LEAST) + 32 * gap <= drive.strip_w)
                                least &= smallest >= ui_dev(&settings.ui, UI_SEGMENT_LEAST) && largest > smallest &&
                                         (drive.strip_w < 2 * (33 * ui_dev(&settings.ui, UI_SEGMENT_LEAST) + 32 * gap) || largest > 10 * smallest);
                        else
                                equal &= largest - smallest <= 1;
                        tall &= drive.top + drive.height == drive.segment[0].y + drive.segment[0].h + ui_dev(&settings.ui, UI_DRIVE_BELOW) && drive.parts == 1 &&
                                drive.part_top[0] == drive.top + drive.height &&
                                drive.part_height[0] == ui_dev(&settings.ui, UI_ROW) * UI_ROW_SHARE_HIGH / UI_ROW_LEAST;
                        if (scale == 1 && w < 2)
                                heights[w] = drive.height;
                }
        check("settings drive rows: every segment of a drive of 16 partitions and the free room round them has a leaf, at least the least wide where the strip has room for that",
              least);
        check("settings drive rows: the segments and their gaps are the strip whole, and where 33 at the least do not fit each is the strip over the count",
              equal && fills);
        // The window's least width keeps a 16 partition strip wide enough for its least widths, so the rule for one too narrow is held here
        // to the widths alone: 33 segments in 200 pixels at a least of 8 are each 200 over 33, the first ones a pixel wider.
        {
                unsigned long long bytes[33];
                int widths[33], sum = 0, low = 1 << 30, high = 0;

                for (int k = 0; k < 33; k++)
                        bytes[k] = k % 3 ? 1ull << 20 : 1ull << 40;
                ui_strip_widths(bytes, 33, 200, 8, widths);
                for (int k = 0; k < 33; k++)
                        sum += widths[k], low = widths[k] < low ? widths[k] : low, high = widths[k] > high ? widths[k] : high;
                check("settings drive rows: a strip too narrow for its segments at the least gives each the strip over the count, whatever their bytes",
                      sum == 200 && low == 200 / 33 && high - low <= 1 && widths[0] >= widths[32]);
        }
        check("settings drive rows: a drive's row is as tall as its line, its heading and its strip, at every width, and its disclosure, a share row's height, is the row after it",
              tall && heights[0] == heights[1] && heights[0] > 0);

        // Open, the 16 partitions are rows after the disclosure, in the disk's order, then the partitions not shown, then the free room.
        drive_open_all(sixteen, true);
        drive_show(SETTINGS_WIDTH, 1, false);
        if (drive_lay() && drive.parts == 19)
        {
                struct ui_row row;

                last = true;
                for (int k = 1; k < 19 && last; k++)
                {
                        ui_list_describe(&settings.list, drive.part_id[k], &row);
                        last = row.kind == UI_ROW_SHARE && drive.part_top[k] == drive.part_top[k - 1] + drive.part_height[k - 1] &&
                               (k < 17 ? row.swatch > 0 && row.swatch - 1 < UI_STRIP_FREE
                                : k == 17 ? row.dim && string_equals((string_address)row.label, (string_address) "4 more not shown")
                                          : row.swatch == UI_STRIP_FREE + 1 && string_equals((string_address)row.label, (string_address) "Free space"));
                }
        }
        check("settings drive rows: open, a drive of 16 partitions has a row for each after its disclosure, then the partitions not shown, then the free room, the last",
              last);

        // Reading from the right, the strip, the heading and the open rows' parts are where they are reading from the left, turned over.
        for (int w = 0; w < 2; w++)
        {
                static struct ui_node ltr[UI_STRIP_MOST + 2 + 4 * DRIVE_PARTS];
                int count = 0, parts;

                drive_show(widths[w], 1, false);
                if (!drive_lay())
                {
                        mirrored = false;
                        continue;
                }
                parts = drive.parts;
                for (int k = 0; k < UI_STRIP_MOST; k++)
                        ltr[k] = drive.segment[k];
                ltr[UI_STRIP_MOST] = drive.share, ltr[UI_STRIP_MOST + 1] = drive.amount;
                for (int k = 0; k < parts; k++)
                {
                        ltr[UI_STRIP_MOST + 2 + 4 * k] = drive.part_swatch[k], ltr[UI_STRIP_MOST + 3 + 4 * k] = drive.part_label[k];
                        ltr[UI_STRIP_MOST + 4 + 4 * k] = drive.part_amount[k], ltr[UI_STRIP_MOST + 5 + 4 * k] = drive.part_share[k];
                }
                drive_show(widths[w], 1, true);
                mirrored &= drive_lay() && drive.parts == parts && parts == 19;
                for (int k = 0; k < UI_STRIP_MOST + 2 + 4 * parts && mirrored; k++)
                {
                        int part = (k - UI_STRIP_MOST - 2) / 4, which = (k - UI_STRIP_MOST - 2) % 4;
                        const struct ui_node address_to was = &ltr[k];
                        const struct ui_node address_to now = k < UI_STRIP_MOST ? &drive.segment[k] : k == UI_STRIP_MOST ? &drive.share
                                                            : k == UI_STRIP_MOST + 1 ? &drive.amount
                                                            : which == 0 ? &drive.part_swatch[part] : which == 1 ? &drive.part_label[part]
                                                            : which == 2 ? &drive.part_amount[part] : &drive.part_share[part];

                        count += was->w > 0;
                        mirrored &= now->x == settings.width - was->x - was->w && now->y == was->y && now->w == was->w && now->h == was->h;
                }
                mirrored &= count >= 33 + 2 + 3 * 19;
        }
        check("settings drive rows: reading from the right a drive's strip, its heading and its open partitions' rows are those from the left, turned over", mirrored);

        // The shares of the rows under a drive add up to the whole, to within a twentieth of a percent a row: the 16 partitions with those
        // not shown and the free room, and a disk of 1 TB.
        {
                int count = drive_parts_of((int)SETTINGS_LIST_ID(2, 0)), sum = drive_tenths(count);

                sums &= count == 19 && sum >= 1000 - (count - 1) / 2 && sum <= 1000 + (count - 1) / 2;
                drive_open_all(nvme, false);
                count = drive_parts_of((int)SETTINGS_LIST_ID(2, 0)), sum = drive_tenths(count);
                sums &= count == 6 && sum >= 1000 - (count - 1) / 2 && sum <= 1000 + (count - 1) / 2;
        }
        check("settings drive rows: the shares of a drive's rows add up to 100 percent, to within the rounding of each", sums);

        // Shut when the window starts: the disclosure alone after the drive, its chevron pointing right and how many partitions there are.
        drive_show(SETTINGS_WIDTH, 1, false);
        if (drive_lay() && drive.parts == 1)
        {
                struct ui_row row;

                ui_list_describe(&settings.list, drive.part_id[0], &row);
                shut = row.expander == UI_ROW_EXPANDER_SHUT && string_equals((string_address)row.label, (string_address) "Partitions") && row.share &&
                       string_equals((string_address)row.share, (string_address) "4") && !settings_exp_get(drive.id) && settings_fold_reveal(drive.id) == 0;
        }
        check("settings drive rows: a drive's partitions are shut when the window starts: the disclosure alone, Partitions and how many", shut);

        // The heading: the share at the strip's start and the amount at its end, saying 97% partitioned and USED / TOTAL.
        headed = drive_lay() && drive.row.share && drive.row.amount && string_equals((string_address)drive.row.share, (string_address) "97% partitioned") &&
                 string_equals((string_address)drive.row.amount, (string_address) "969 GB / 1.0 TB") && drive.share.x == drive.strip_x &&
                 drive.amount.x + drive.amount.w == drive.strip_x + drive.strip_w && drive.amount.w > 0;
        check("settings drive rows: the heading says the share partitioned at the strip's start and USED / TOTAL at its end", headed);

        // A press on the disclosure opens the partitions on the kit's fold: listed at once at no height, part way 100 ms in, whole at rest.
        // A press again shuts them; Enter with the keys on it opens them and shuts them, and Right opens and Left shuts.
        {
                int disclosure = drive.part_id[0], tops = drive.part_top[0] + drive.part_height[0] / 2, hits = 0;
                b64 mid;

                ui_kit.motion = 0, ui_kit.now = 6000000000ull;
                drive_click(settings.list.x + settings.list.w / 2, tops);
                drive_fold_run(0);
                drive_show(SETTINGS_WIDTH, 1, false), ui_kit.now = 6000000000ull;
                                hits += settings_exp_get(drive.id) && drive_lay() && drive.parts == 6 && drive.part_height[1] == 0;
                drive_fold_run(100000000);
                mid = settings_fold_reveal(drive.id);
                                hits += mid > 0 && mid < UI_ONE;
                drive_fold_run(2000000000);
                drive_show(SETTINGS_WIDTH, 1, false);
                hits += drive_lay() && drive.parts == 6 && settings_fold_reveal(drive.id) == UI_ONE &&
                        drive.part_height[5] == ui_dev(&settings.ui, UI_ROW) * UI_ROW_SHARE_HIGH / UI_ROW_LEAST;
                ui_kit.now = 9000000000ull;
                drive_click(settings.list.x + settings.list.w / 2, tops);
                drive_fold_run(2000000000);
                drive_show(SETTINGS_WIDTH, 1, false);
                hits += !settings_exp_get(drive.id) && drive_lay() && drive.parts == 1;
                                pressed = hits == 4;
                // The keys: on the disclosure, with the rows the window's pane.
                ui_kit.motion = 1, settings.page.pane = 1, settings.focused = 1;
                settings.list.focus = disclosure;
                hits = 0;
                settings_press(UI_KEY_ENTER), drive_fold_run(0);
                hits += settings_exp_get(drive.id) && settings_fold_reveal(drive.id) == UI_ONE;
                settings_press(UI_KEY_SPACE), drive_fold_run(0);
                hits += !settings_exp_get(drive.id) && settings_fold_reveal(drive.id) == 0;
                settings_press(UI_KEY_RIGHT), drive_fold_run(0);
                hits += settings_exp_get(drive.id);
                settings_press(UI_KEY_RIGHT), drive_fold_run(0);
                hits += settings_exp_get(drive.id);
                settings_press(UI_KEY_LEFT), drive_fold_run(0);
                hits += !settings_exp_get(drive.id) && settings.list.focus == disclosure;
                keyed = hits == 5;
                ui_kit.motion = 0, settings.page.pane = 0, settings.focused = 0;
        }
        check("settings drive rows: a press on the disclosure opens the partitions on the kit's fold, and a press again shuts them", pressed);
        check("settings drive rows: Enter or Space on the disclosure opens and shuts the partitions, Right opens them and Left shuts them", keyed);

        // Open, a row for each partition and the free room last: each one's square in its role's colour (the free room's the track's), and its
        // amount and its share each in a column whose end is the same in every row, the shares' at the strip's end.
        drive_open_all(nvme, true);
        drive_show(SETTINGS_WIDTH, 1, false);
        if (drive_lay() && drive.row.strip_count == 5 && drive.has_segment[0] && drive.parts == 6)
        {
                const struct ui_node address_to efi = &drive.segment[0];

                drawn = efi->w >= ui_dev(&settings.ui, UI_SEGMENT_LEAST) &&
                        drive_pixel(efi->x + efi->w / 2, efi->y + efi->h / 2) == (ui_role(ui_strip_colour(UI_STRIP_EFI)) & 0xffffffu);
                listed = drive.part_swatch_role[5] == UI_STRIP_FREE;
                columns = true;
                for (int k = 1; k < 6; k++)
                {
                        const struct ui_node address_to mark = &drive.part_swatch[k];
                        int side = ui_dev(&settings.ui, UI_SHARE_SWATCH);

                        listed &= drive.part_swatch_role[k] >= 0 &&
                                  drive_pixel(mark->x + side / 2, mark->y + mark->h / 2) == (ui_role(ui_strip_colour((unsigned int)drive.part_swatch_role[k])) & 0xffffffu);
                        columns &= drive.part_amount[k].x + drive.part_amount[k].w == drive.part_amount[1].x + drive.part_amount[1].w &&
                                   drive.part_share[k].x + drive.part_share[k].w == drive.strip_x + drive.strip_w && drive.part_amount[k].w > 0;
                }
        }
        check("settings drive rows: a partition of 512 MB on a disk of 1 TB is drawn, at least the least wide, in its role's colour", drawn);
        check("settings drive rows: open, each partition's row has a square of its role's colour, and the free room's, the last, the track's", listed);
        check("settings drive rows: the sizes and the shares of the rows are each a column whose end is the same in every row, the shares' at the strip's end", columns);

        // The partitions stay open by the drive's name when the list is read again and another drive comes before it: its row moves.
        string_copy_bounded((string_address)moved, (string_address) "  sda drive 1000 MB view only\n", sizeof(moved));
        string_append_bounded((string_address)moved, (string_address)nvme, sizeof(moved));
        drive_feed(moved);
        drive_show(SETTINGS_WIDTH, 1, false);
        kept = drive_lay() && drive.id == (int)SETTINGS_LIST_ID(2, 1) && settings_exp_get(drive.id) && drive.parts == 6 &&
               !settings_exp_get((int)SETTINGS_LIST_ID(2, 0));
        check("settings drive rows: a drive's partitions stay open by its name when the list is read again and its row moves", kept);

        // Free room is the bar's track: a free segment of the least width is the track's colour at its middle, as the gap beside it is,
        // which is no partition's colour, and the window's surface is above the bar.
        drive_open_all(sixteen, false);
        drive_show(SETTINGS_WIDTH, 1, false);
        if (drive_lay() && drive.has_segment[0] && drive.has_segment[1] && drive.row.strip_roles[0] == UI_STRIP_FREE)
        {
                const struct ui_node address_to room = &drive.segment[0], address_to next = &drive.segment[1];
                unsigned int ground = drive_pixel(room->x + room->w + (next->x - room->x - room->w) / 2, room->y + room->h / 2);
                unsigned int middle = drive_pixel(room->x + room->w / 2, room->y + room->h / 2);

                free_track = room->w == ui_dev(&settings.ui, UI_SEGMENT_LEAST) && middle == ground && middle == (ui_role(UI_SURFACE_HIGHEST) & 0xffffffu) &&
                             drive_pixel(room->x + room->w / 2, room->y - 1) == (ui_role_surface() & 0xffffffu);
                for (int role = 0; role < 6; role++)
                        free_track &= role == UI_STRIP_FREE || middle != (ui_role(ui_strip_colour((unsigned int)role)) & 0xffffffu);
        }
        check("settings drive rows: free room is the bar's track, in no partition's colour, and the gaps between segments are the track too", free_track);

        // Every role's colour stands out from the track and from the window's surface by 3:1 at the least, in both themes, and no two roles
        // share a colour.
        for (int theme = 0; theme < 2; theme++)
        {
                ui_kit_theme(theme ? UI_THEME_LIGHT : UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
                for (int role = 0; role < 6; role++)
                {
                        unsigned int colour = ui_role(ui_strip_colour((unsigned int)role)) | 0xff000000u;

                        if (role == UI_STRIP_FREE)
                                continue;
                        apart &= ui_contrast(colour, ui_role(UI_SURFACE_HIGHEST) | 0xff000000u) >= 300 && ui_contrast(colour, ui_role_surface()) >= 300;
                        for (int other = 0; other < role; other++)
                                apart &= other == UI_STRIP_FREE || ui_strip_colour((unsigned int)other) != ui_strip_colour((unsigned int)role);
                }
        }
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        check("settings drive rows: every role's colour stands out from the track and from the surface by 3:1, light and dark, and each is its own", apart);

        /*
                The Devices group in its sections: New devices, Internal storage (an NVMe, a SATA and a virtio disk, and a drive whose host
                says nothing of where it is), External devices (a USB stick with two partitions, its bar under its allow and deny, an SD card,
                a removable SATA disk and a USB keyboard), each section's in the order the host said them.
        */
        {
                static const char machine[] = "  1-1 usb 0781:5581 SanDisk Ultra allowed\n    drive sdc 30752 MB\n    part 0 1048576 free -\n"
                                              "    part 1048576 30000000000 other Ventoy\n    part 30001048576 33554432 efi VTOYEFI\n"
                                              "    part 30034603008 717396992 free -\n"
                                              "  1-2 usb 046d:c31c Logitech USB Keyboard allowed\n"
                                              "  mmcblk0 drive 63864 MB view only\n    part 0 4194304 free -\n    part 4194304 63859805696 other -\n"
                                              "    where external sd\n"
                                              "  nvme0n1 drive 512110 MB view only\n    part 0 512110000000 linux Linux_filesystem\n    where internal nvme\n"
                                              "  sda drive 1000204 MB view only\n    part 0 1000204000000 other Basic_data_partition\n    where internal ata\n"
                                              "  sdd drive 2000 MB view only\n    part 0 2000000000 unpartitioned -\n    where external ata\n"
                                              "  vda drive 20000 MB view only\n    part 0 20000000000 linux -\n    where internal virtio\n"
                                              "  vdb drive 1000 MB view only\n    part 0 1000000000 linux -\n";
                struct ui_row row;
                const char address_to section;
                b32 inside = true, outside = true, stick = false, card = false, ordered = false, alone = false, held = false;
                int sections, devices = settings_groups_named("Devices");

                drive_open_all(machine, false);
                drive_show(SETTINGS_WIDTH, 1, false);
                for (int k = 0; k < 4; k++)
                {
                        static const char address_to const internal[4] = {"nvme0n1", "sda", "vda", "vdb"};

                        section = drive_section_of(internal[k], &row);
                        inside &= section && string_equals((string_address)section, (string_address) "Internal storage") && row.tall;
                }
                for (int k = 0; k < 3; k++)
                {
                        static const char address_to const external[3] = {"Logitech USB Keyboard", "mmcblk0", "sdd"};

                        section = drive_section_of(external[k], &row);
                        outside &= section && string_equals((string_address)section, (string_address) "External devices");
                }
                check("settings devices: an NVMe, a SATA and a virtio disk, and one the host says nothing of, are under Internal storage; a removable disk and a keyboard under External devices",
                      inside && outside);
                section = drive_section_of("SanDisk Ultra", &row);
                stick = section && string_equals((string_address)section, (string_address) "External devices") && row.kind == UI_ROW_TABS && row.tall &&
                        row.strip_count == 3 && row.note && string_equals((string_address)row.note, (string_address) "USB drive 1-1") && row.share &&
                        string_equals((string_address)row.share, (string_address) "98% partitioned");
                if (stick)
                {
                        int at = drive_place_of("SanDisk Ultra");
                        struct ui_row next;

                        // The row after the stick's is its disclosure: two partitions.
                        ui_list_describe(&settings.list, settings.list.id[at + 1], &next);
                        stick = at >= 0 && next.kind == UI_ROW_SHARE && next.share && string_equals((string_address)next.share, (string_address) "2");
                }
                check("settings devices: a USB stick with partitions is under External devices, its allow and deny on its line and its heading, its bar and its partitions under it",
                      stick);
                section = drive_section_of("mmcblk0", &row);
                card = section && string_equals((string_address)section, (string_address) "External devices") && row.tall && row.note &&
                       string_equals((string_address)row.note, (string_address) "SD card, view only");
                check("settings devices: an SD card is under External devices, and says it is an SD card", card);
                sections = drive_sections();
                ordered = sections >= 3 && string_equals((string_address)drive_headers[sections - 3], (string_address) "Devices") &&
                          string_equals((string_address)drive_headers[sections - 2], (string_address) "Internal storage") &&
                          string_equals((string_address)drive_headers[sections - 1], (string_address) "External devices") &&
                          settings.page.chosen == devices;
                check("settings devices: the group is New devices, then Internal storage, then External devices", ordered);
                // A press on the stick's strip allows or blocks nothing; one on its deny tab blocks it. The stick's row is scrolled to the
                // list's top, and drawn there (its tabs are placed as they are drawn); a change of a row (not a feed's reading, which a change
                // would end) is running, so a change waits in the queue.
                {
                        int queued = (settings.runner.child = 1, settings.job = SETTINGS_JOB_ACT, settings.queued), at = drive_place_of("SanDisk Ultra"), y;
                        struct ui_control address_to tabs = at >= 0 ? ui_list_control(&settings.list, settings.list.id[at], 1) : null;

                        if (at >= 0)
                        {
                                settings.list.scroll.scroll = settings.list.top[at];
                                ui_scroll_clamp(&settings.list.scroll);
                                settings_draw_rows(settings_frames_whole, 0, 0);
                                y = ui_list_row_y(&settings.list, at) + settings.list.tall[at] - ui_dev(&settings.ui, 10);
                                drive_click(settings.list.x + settings.list.w / 2, y);
                                held = settings.queued == queued && tabs && tabs->kind == UI_TABS && y < settings.list.y + settings.list.h;
                        }
                        if (held)
                        {
                                settings_draw_rows(settings_frames_whole, 0, 0);
                                drive_click(ui_dev(&settings.ui, tabs->x + tabs->w * 3 / 4), ui_dev(&settings.ui, tabs->y + tabs->h / 2));
                                held = settings.queued == queued + 1 && settings.queue[queued].row == SETTINGS_JOB_ACT &&
                                       string_equals((string_address)settings.queue[queued].value, (string_address) "x\n1-1");
                        }
                        settings.queued = 0, settings.runner.child = 0;
                }
                check("settings devices: a press on a USB stick's strip allows or blocks nothing, and one on its deny tab blocks it", held);
                // A section with no rows has no header: drives alone, and USB devices alone.
                drive_open_all("  nvme0n1 drive 512110 MB view only\n    part 0 512110000000 linux -\n    where internal nvme\n", false);
                drive_show(SETTINGS_WIDTH, 1, false);
                sections = drive_sections();
                alone = sections >= 2 && string_equals((string_address)drive_headers[sections - 1], (string_address) "Internal storage");
                for (int k = 0; k < sections; k++)
                        alone &= !string_equals((string_address)drive_headers[k], (string_address) "External devices");
                drive_open_all("  1-2 usb 046d:c31c Logitech USB Keyboard allowed\n", false);
                drive_show(SETTINGS_WIDTH, 1, false);
                sections = drive_sections();
                alone &= sections >= 2 && string_equals((string_address)drive_headers[sections - 1], (string_address) "External devices");
                for (int k = 0; k < sections; k++)
                        alone &= !string_equals((string_address)drive_headers[k], (string_address) "Internal storage");
                drive_open_all("", false);
                drive_show(SETTINGS_WIDTH, 1, false);
                sections = drive_sections();
                alone &= sections >= 2 && !drive_headers[sections - 1][0];
                for (int k = 0; k < sections; k++)
                        alone &= !string_equals((string_address)drive_headers[k], (string_address) "Internal storage") &&
                                 !string_equals((string_address)drive_headers[k], (string_address) "External devices");
                check("settings devices: a section with no rows is not drawn, and a list with nothing in it says so under no header", alone);
        }

        // The action rows and a row's copies, on the same window.
        check_settings_actions();
        memory_zero(settings_drives_open, sizeof(settings_drives_open));
        ui_kit.rtl = 0;
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);
        memory_copy(settings.item[2], drive_items_kept, sizeof(drive_items_kept));
        settings_record_put_back(&drive_settings_kept);
        memory_copy(&ui_kit, drive_kit_kept, sizeof(ui_kit));
}

static fn check_settings_groups(void)
{
        static const char address_to names[] = {"System", "Canvas", "Display", "Input", "Scrolling", "Sound", "Taskbar", "Time",
                                                "Network", "Waterlink", "Wi-Fi", "Bluetooth", "Devices", "Power", "Boot", "SSH"};
        static const struct
        {
                const char address_to words[3];
                const char address_to group;
        } families[] = {
            {{"name"}, "System"}, {{"canvas", "background"}, "Canvas"}, {{"canvas", "theme"}, "Canvas"},
            {{"canvas", "motion"}, "Canvas"}, {{"canvas", "caret"}, "Canvas"}, {{"canvas", "text"}, "Canvas"}, {{"canvas", "windows"}, "Canvas"}, {{"canvas", "border"}, "Canvas"},
            {{"canvas", "clipboard"}, "Canvas"}, {{"canvas", "gpu"}, "Display"}, {{"canvas", "display"}, "Display"}, {{"canvas", "modes"}, "Display"}, {{"canvas", "vrr"}, "Display"}, {{"canvas", "cadence"}, "Display"},
            {{"brightness"}, "Display"}, {{"keyboard"}, "Input"}, {{"mouse"}, "Input"}, {{"canvas", "cursor"}, "Input"},
            {{"canvas", "wheel"}, "Input"}, {{"canvas", "trackpad"}, "Scrolling"}, {{"canvas", "scroll"}, "Scrolling"}, {{"canvas", "predict"}, "Input"}, {{"canvas", "touch"}, "Input"}, {{"audio"}, "Sound"},
            {{"taskbar"}, "Taskbar"}, {{"timezone"}, "Time"}, {{"ntp"}, "Time"}, {{"time"}, "Time"}, {{"wired"}, "Network"},
            {{"airplane"}, "Network"}, {{"priority"}, "Network"}, {{"dns"}, "Network"}, {{"link"}, "Waterlink"}, {{"wifi"}, "Wi-Fi"},
            {{"bluetooth"}, "Bluetooth"}, {{"device"}, "Devices"}, {{"charge"}, "Power"}, {{"battery"}, "Power"}, {{"power"}, "Power"},
            {{"cpu"}, "Power"}, {{"init"}, "Boot"}, {{"ssh"}, "SSH"}};
        static const struct
        {
                const char address_to label;
                const char address_to group;
        } placed[] = {
            {"Keyboard", "Input"}, {"On-screen keyboard", "Input"}, {"Key repeat delay (ms, or default)", "Input"},
            {"Key repeat rate (per second, off or default)", "Input"}, {"Mouse sensitivity (percent, or default)", "Input"},
            {"Mouse acceleration", "Input"}, {"Smooth mouse wheel", "Input"}, {"Cursor size (percent, or default)", "Input"}, {"Cursor look", "Input"},
            {"Cursor style", "Input"}, {"Pointer squeeze (percent, or default)", "Input"},
            {"Shake the pointer to find it", "Input"}, {"Large pointer stays (ms, or default)", "Input"},
            {"Pointer squeezes on click", "Input"},
            {"Predict touch and pen", "Input"}, {"Show touch area", "Input"}, {"Left edge", "Input"}, {"Right edge", "Input"},
            {"Natural direction", "Scrolling"}, {"Scroll speed", "Scrolling"}, {"Momentum", "Scrolling"}, {"Rubber band", "Scrolling"},
            {"Time zone", "Time"},
            {"Volume (percent, or default)", "Sound"}, {"Mute", "Sound"}, {"Output device (name, or default)", "Sound"}, {"Sound server", "Sound"},
            {"Allow recording", "Sound"}, {"Input device (name, or default)", "Sound"}, {"Input volume (percent, or default)", "Sound"}, {"Input mute", "Sound"},
            {"Background", "Canvas"}, {"Background colour (rrggbb or default)", "Canvas"}, {"Theme", "Canvas"},
            {"Accent", "Canvas"}, {"Accent colour (rrggbb or reset)", "Canvas"}, {"Motion", "Canvas"},
            {"Caret blink speed", "Canvas"}, {"Caret thickness", "Canvas"}, {"Text size (percent, or default)", "Canvas"},
            {"Resize border width (pixels, or default)", "Canvas"},
            {"Open windows where they were last put", "Canvas"},
            {"Clipboard", "Canvas"}, {"Clipboard history (copies kept)", "Canvas"},
            {"Screen", "Display"}, {"Resolution", "Display"}, {"Primary display", "Display"}, {"Screen modes", "Display"},
            {"Variable refresh rate", "Display"}, {"Game frame cadence", "Display"}, {"Brightness", "Display"}, {"GPU rendering (alpha)", "Display"}, {"GPU samples tiled client buffers", "Display"}};
        const int count = (int)(sizeof(names) / sizeof(names[0]));
        b32 order = settings_groups() == count + 1, family = true, by_name = true, listed = true, apart = true, found = true,
            every = true, bare = true, screens = true;

        for (int at = 0; at < count && at < settings_groups(); at++)
                order &= string_equals((string_address)settings_group_name(at), (string_address)names[at]);
        order &= string_equals((string_address)settings_group_name(settings_groups() - 1), (string_address)SETTINGS_ALL_HEADER);
        check("settings groups: System, Canvas, Display, Input, Scrolling, Sound, Taskbar, Time, Network, Waterlink, Wi-Fi, Bluetooth, Devices, Power, Boot, SSH, then All commands", order);

        // Each row in the group of its verb's family: the groups are what the rows do.
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];
                const char address_to group = settings_group_name(settings_group_of((int)at));

                for (positive of = 0; of < sizeof(families) / sizeof(families[0]); of++)
                {
                        positive word = 0;
                        const char address_to wanted = families[of].group;

                        if (!row->read[0])
                                break;
                        while (word < 3 && families[of].words[word] && row->read[word] &&
                               string_equals((string_address)row->read[word], (string_address)families[of].words[word]))
                                word++;
                        if (word == 0 || (word < 3 && families[of].words[word]))
                                continue;
                        // The machine's name is shown under Waterlink, beside the switch that links it: the one row of Machine's
                        // family that is not Machine's.
                        if (string_equals((string_address)row->label, (string_address) "This machine"))
                                break;
                        if (!string_equals((string_address)group, (string_address)wanted))
                        {
                                family = false;
                                string_format(log, "  settings groups: %s (%s) is listed under %s, and its verb is %s's\n",
                                              (string_address)row->label, (string_address)row->read[0], (string_address)group, (string_address)wanted);
                        }
                        break;
                }
                // The kernel notices row is a row with an output and no screen: its group is System, not Display.
                if (row->output && row->output != SETTINGS_OUT_NOTICES && !string_equals((string_address)group, (string_address) "Display"))
                        screens = false;
        }
        check("settings groups: every row is in the group of what its verb does (the keyboard's, the mouse's, the pointer's, the clock's)", family);
        check("settings groups: no row of the display options is listed under Machine, or anywhere but Display", screens);

        settings.ui.scale = 100;
        settings.page.card = 0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.pane = 1;
        settings_filter_clear(), (void)0;
        for (positive at = 0; at < sizeof(placed) / sizeof(placed[0]); at++)
        {
                int row = settings_row_named(placed[at].label), want = settings_groups_named(placed[at].group);
                const char address_to first = placed[at].label;
                positive word = 0;

                if (!string_equals((string_address)settings_rows[row].label, (string_address)placed[at].label) || want < 0)
                {
                        by_name = false;
                        continue;
                }
                by_name &= settings_group_of(row) == want;
                settings_filter_clear(), (void)0;
                settings.page.chosen = want;
                settings_rebuild();
                listed &= settings_pos(row) >= 0;
                // Listed under the group's own name, and not under Machine (the first group) unless it is Machine's.
                settings.page.chosen = 0;
                settings_rebuild();
                apart &= (settings_pos(row) >= 0) == (want == 0);
                // Found by the filter from the group it is not in: by the first word of its label.
                while (first[word] && first[word] != ' ')
                        word++;
                settings_groups_filter(first, word);
                found &= settings_pos(row) >= 0;
                settings_filter_clear(), (void)0;
        }
        check("settings groups: the rows moved out of Machine are named in the groups of what they do (Input, Scrolling, Canvas, Time, Display)", by_name);
        check("settings groups: each is listed when its group is chosen", listed);
        check("settings groups: and none of them is listed under Machine", apart);
        check("settings groups: and the filter finds each by the first word of its label, from the group chosen", found);

        // System holds what the machine is called, its build, its language, its kernel notices, and the About section (logo and links).
        settings.page.chosen = 0;
        settings_filter_clear(), (void)0;
        settings_rebuild();
        {
                b32 has_notices = false, has_brand = false, has_discord = false, has_github = false, about_ok = true;

                for (int at = 0; at < settings.list.count; at++)
                {
                        int id = settings.list.id[at];
                        const struct settings_row address_to row = id < SETTINGS_ANY ? &settings_rows[id] : null;

                        if (settings_about_of(id) >= 0)
                        {
                                struct ui_row desc;

                                memory_zero(&desc, sizeof(desc));
                                settings_describe(id, &desc);
                                if (settings_about_of(id) == SETTINGS_ABOUT_BRAND)
                                        has_brand = desc.paint == settings_about_logo_paint && desc.picture_w > 0 && desc.picture > 0 &&
                                                    desc.label && string_equals((string_address)desc.label, (string_address) "Made By Dawn Larsson") &&
                                                    desc.about && string_equals((string_address)desc.about, (string_address) "dawning.dev");
                                else if (settings_about_of(id) == SETTINGS_ABOUT_DISCORD)
                                        has_discord = desc.kind == UI_ROW_BUTTON && desc.label &&
                                                      string_equals((string_address)desc.label, (string_address) "Discord") &&
                                                      string_equals((string_address)settings_copy_text(id), (string_address) "https://discord.gg/nwvFVGYJhR");
                                else if (settings_about_of(id) == SETTINGS_ABOUT_GITHUB)
                                        has_github = desc.kind == UI_ROW_BUTTON && desc.label &&
                                                     string_equals((string_address)desc.label, (string_address) "GitHub") &&
                                                     string_equals((string_address)settings_copy_text(id), (string_address) "https://github.com/dawnlarsson/moonwater/issues");
                                continue;
                        }
                        if (row && row->output == SETTINGS_OUT_NOTICES)
                        {
                                has_notices = true;
                                continue;
                        }
                        bare &= row && row->read[0] && (string_equals((string_address)row->read[0], (string_address) "name") ||
                                                        string_equals((string_address)row->read[0], (string_address) "language") ||
                                                        string_equals((string_address)row->read[0], (string_address) "version") ||
                                                        string_equals((string_address)row->read[0], (string_address) "machine"));
                }
                about_ok = has_notices && has_brand && has_discord && has_github && settings_system_group() == 0;
                check("settings groups: System merges Machine and System notices with the Moonwater logo, Made By Dawn Larsson / dawning.dev, Discord and GitHub links",
                      bare && about_ok && settings.list.count > 0);
        }

        // The filter lists the rows of every group: each row by the first word of its label, and each group's rows by its name. A row
        // that is a way of a list's add row (.way: Waterlink's code shown and code typed) is that add row, which a word of its label finds.
        {
                int saved_links = settings.item_count[3];
                settings.runner.length = 0, settings_feed_links();
                for (positive at = 0; at < SETTINGS_ROWS; at++)
                {
                        const char address_to label = settings_rows[at].label;
                        int shown = settings_rows[at].way ? (int)SETTINGS_LIST_ID(3, settings.item_count[3] - 1) : (int)at;
                        positive word = 0;

                        while (label[word] && label[word] != ' ')
                                word++;
                        settings_groups_filter(label, word);
                        every &= settings_pos(shown) >= 0 && (!settings_rows[at].way || settings_pos((int)at) < 0);
                }
                for (int group = 0; group < settings_groups() - 1; group++)
                {
                        const char address_to name = settings_group_name(group);

                        settings_groups_filter(name, string_length((string_address)name));
                        for (positive at = 0; at < SETTINGS_ROWS; at++)
                                if (settings_group_of((int)at) == group && !settings_rows[at].way)
                                        every &= settings_pos((int)at) >= 0;
                }
                settings.item_count[3] = saved_links;
        }
        check("settings groups: the filter lists every row of every group, by a word of its label and by its group's name (a way row as its add row)", every);

        settings_filter_clear(), (void)0;
        settings.page.chosen = 0;
        settings_refilter();
}

/*
        The Settings window is one flush surface. The column's rows run from the window's edge to the middle of the column's scroll
        track, the body's rows from there to the window's real right edge, and each scroll track lies across the end of the surface it
        belongs to (the column's across the join, the body's over the blank end of its rows): the rows' hover, press and the row the
        keys are on are as wide as their list, and the words and controls keep the page's margins, so nothing is drawn under a track.
        Drawn whole at the scale in use: the keys in the rows on one of the first, and on a long group (so there is a thumb) on the
        third, and no pixel past the margins but the rows' fills and the thumbs.
*/
// The window drawn whole, twice, two seconds apart on the kit's clock: the pip, the hover and the thumb's ink are where they settle.
static fn settings_seam_draw(void)
{
        ui_kit.now += 2000000000ll;
        ui_kit.busy = 0;
        settings_draw_rows(settings_frames_whole, 0, 0);
        ui_kit.now += 2000000000ll;
        ui_kit.busy = 0;
        settings_draw_rows(settings_frames_whole, 0, 0);
}

/*
        A press on a row's switch, or on a dropdown's own box and arrow, acts on that control. A row with an expander (its lines fold: the GPU's,
        or a row with an issue) has a strip at its end for the chevron and the badge, and a press in it opens and closes the lines; a press on
        the switch or the dropdown beside that strip is the control's. Every group is laid out at this scale, and every switch's centre is
        tested against its row's band and against the expander strip; the GPU rendering switch (a row with an expander) is pressed for real
        and must change the switch, not fold the row; a dropdown's arrow and value (a row given an issue, so that it has an expander) are
        outside the strip. Nothing is left changed: the window, the kit and the board are put back as they were.
*/
static fn check_settings_press(int x, int y)
{
        struct window_event event = {WINDOW_EVENT_MOTION, 0, x, y};

        settings.runner.child = 1; // a press that changes a setting queues it: no job starts here
        settings_event_at(&event);
        event = (struct window_event){WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0};
        settings_event_at(&event);
        event = (struct window_event){WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0};
        settings_event_at(&event);
        settings.runner.child = 0;
}

static fn check_settings_clicks(void)
{
        // The window, the kit and the board as they are, put back at the end: the presses leave their ripples and holds in the kit.
        static struct settings_kept kept_settings;
        static p8 kept_kit[sizeof(ui_kit)], kept_board[sizeof(settings_board)];
        struct ui address_to ui = &settings.ui;
        struct ui_list address_to list = &settings.list;
        static struct windows *click_table_scratch;
        struct windows *const click_table_at = windows_scratch_table(&click_table_scratch);
        int before = settings.page.chosen, scroll_before = list->scroll.scroll, seen = 0, wrong = 0, strips = 0;
        int gpu = settings_row_named("GPU rendering (alpha)"), keyboard = settings_row_named("Keyboard");
        unsigned char gpu_fold = gpu >= 0 ? settings_exp_get(gpu) : 0;
        positive queued = settings.queued;
        b32 gpu_pressed = false, gpu_ok = false, arrow_ok = false, value_ok = false, hover_ok = false;

        settings_record_keep(&kept_settings);
        memory_copy(kept_kit, &ui_kit, sizeof(ui_kit));
        memory_copy(kept_board, &settings_board, sizeof(settings_board));

        settings.page.menu_row = -1;
        ui_field_close(&settings.edit);
        for (int group = 0; group < settings_groups(); group++)
        {
                settings_group_choose(group);
                settings_layout();
                for (int at = 0; at < list->count; at++)
                {
                        int id = list->id[at], across, wide, px, py;
                        struct ui_row row;

                        if (id >= (int)SETTINGS_ALL)
                                continue;
                        ui_list_describe(list, id, &row);
                        if (row.kind != UI_ROW_SWITCH)
                                continue;
                        ui_scroll_set(&list->scroll, list->top[at]);
                        ui_list_across(list, &across, &wide);
                        px = across + wide - ui_dev(ui, UI_INSET) - ui_dev(ui, 26);
                        py = ui_list_row_y(list, at) + list->tall[at] / 2;
                        if (py < list->y || py >= list->y + list->h)
                                continue;
                        seen++;
                        if (ui_list_hit(ui, list, px, py) != id)
                        {
                                wrong++;
                                string_format(log, "  settings click: %s's switch is not its own row's\n", settings_label_shown(id));
                                log_flush();
                        }
                        if (ui_list_expander_at(ui, list, id, px))
                        {
                                strips++;
                                string_format(log, "  settings click: %s's switch is in its expander's strip\n", settings_label_shown(id));
                                log_flush();
                        }
                }
        }
        check("settings click: a switch's centre is its own row's, in every group, at this scale", seen > 0 && wrong == 0);
        check("settings click: a switch's centre is not in its row's expander strip, in every group, at this scale", strips == 0);

        // The GPU rendering switch, read as "off" for the press (a switch is drawn only for a row that is read), scrolled into view.
        if (gpu >= 0)
        {
                unsigned int state_before = settings.state[gpu];
                char value_before[SETTINGS_VALUE];
                int at, px, py, across, wide;

                string_copy_bounded((string_address)value_before, (string_address)settings.value[gpu], sizeof(value_before));
                settings.state[gpu] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[gpu], (string_address) "off", SETTINGS_VALUE);
                settings_group_choose(settings_group_of(gpu));
                settings_layout();
                at = ui_list_at(list, gpu);
                if (at >= 0)
                {
                        ui_scroll_set(&list->scroll, list->top[at]);
                        ui_list_across(list, &across, &wide);
                        px = across + wide - ui_dev(ui, UI_INSET) - ui_dev(ui, 26);
                        py = ui_list_row_y(list, at) + list->tall[at] / 2;
                        check_settings_press(px, py);
                        gpu_pressed = true;
                        gpu_ok = settings_exp_get(gpu) == gpu_fold && settings.queued == queued + 1;
                        if (settings_exp_get(gpu) != gpu_fold)
                                settings_toggle(gpu);
                        settings.queued = queued;
                }
                settings.state[gpu] = state_before;
                string_copy_bounded((string_address)settings.value[gpu], (string_address)value_before, SETTINGS_VALUE);
                settings_layout();
        }
        check("settings click: a press on the GPU rendering switch changes the switch, and does not fold its lines", gpu_pressed && gpu_ok);

        // A dropdown with an expander: the Keyboard row, given a warning on its key (the code is GPU_LISTS only because one is needed; the
        // row's badge reads the severity) so that it has one. Its arrow and its value are left of the strip.
        if (keyboard >= 0)
        {
                int at, right, strip_left, py, gap = ui_dev(ui, 10), badge_w = 0;
                struct ui_row row;
                struct ui_control measure;
                struct window_event event;
                unsigned char keyboard_fold = settings_exp_get(keyboard);

                windows_issue_set(click_table_at, "keyboard", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, 0, 0, "");
                windows_issues_read(click_table_at, &settings_board);
                settings_group_choose(settings_group_of(keyboard));
                settings_layout();
                at = ui_list_at(list, keyboard);
                if (at >= 0)
                {
                        ui_scroll_set(&list->scroll, list->top[at]);
                        py = ui_list_row_y(list, at) + list->tall[at] / 2;
                        // Where ui_list_row draws the chevron and the badge (from the row's end): the dropdown's arrow and value are left of them.
                        ui_list_describe(list, keyboard, &row);
                        if (row.badge)
                        {
                                memory_zero(&measure, sizeof(measure));
                                measure.kind = UI_BADGE, measure.label = row.badge_words;
                                ui_control_size(ui, &measure);
                                badge_w = ui_dev(ui, measure.w) + gap;
                        }
                        right = list->x + list->w - ui_dev(ui, UI_INSET);
                        strip_left = right - badge_w - (2 * ui_dev(ui, 4) + gap);
                        // The arrow's box and the value's are left of the strip, so a press on either is the dropdown's (settings_press_at asks
                        // the strip first). Geometry only: a press would open the menu, and a menu is not left behind by a check.
                        arrow_ok = !ui_list_expander_at(ui, list, keyboard, strip_left - ui_dev(ui, 4));
                        value_ok = !ui_list_expander_at(ui, list, keyboard, strip_left - ui_dev(ui, 24));
                        // Hover: the pointer over the arrow lights the dropdown's row, as it does on any row.
                        event = (struct window_event){WINDOW_EVENT_MOTION, 0, strip_left - ui_dev(ui, 4), py};
                        settings_event_at(&event);
                        hover_ok = settings.list.hover == keyboard;
                }
                windows_issue_clear(click_table_at, "keyboard", WINDOWS_ISSUE_ANY);
                windows_issues_read(click_table_at, &settings_board);
                settings_layout();
        }
        check("settings click: a dropdown's arrow is not in its row's expander strip, so a press on it is the dropdown's", arrow_ok);
        check("settings click: a dropdown's value is not in its row's expander strip, so a press on it is the dropdown's", value_ok);
        check("settings click: the pointer over a dropdown's arrow lights its row", hover_ok);

        settings_group_choose(before);
        settings_layout();
        ui_scroll_set(&list->scroll, scroll_before);
        settings_layout();
        settings_record_put_back(&kept_settings);
        memory_copy(&ui_kit, kept_kit, sizeof(ui_kit));
        memory_copy(&settings_board, kept_board, sizeof(settings_board));
        ui_page_redraw(&settings.page);
}

/*
        The settings window's rows held to what a change and a press know of them: a row changed while a verb runs waits, and nine
        rows wait in turn (none is refused, and a second change to a waiting row replaces its value); a board's lines past the
        ones the window keeps say how many are not shown on the last line; and a press held on a row goes when the list under it
        gains a row, so its release clicks nothing (a device that comes in while the press is down).
*/
static fn check_settings_queue_rows(void)
{
        int rows[9], found = 0, waiting = 0;
        b32 in_order = true;

        settings.runner.child = 1; // a verb runs: each change waits for it, and none starts
        settings.queued = 0;
        for (int row = 0; row < SETTINGS_ROWS && found < 9; row++)
                if (!settings_rows[row].deliberate)
                        rows[found++] = row;
        for (int k = 0; k < found; k++)
                settings_change(rows[k], "x");
        waiting = settings.queued;
        for (int k = 0; k < found && k < settings.queued; k++)
                in_order &= settings.queue[k].row == rows[k];
        check("settings queue: nine rows changed while a verb runs all wait, in order, and none is refused", found == 9 && waiting == 9 && in_order);
        settings_change(rows[0], "y");
        check("settings queue: a second change to a row that waits replaces its value and takes no place",
              settings.queued == 9 && settings.queue[0].row == rows[0] && string_equals((string_address)settings.queue[0].value, (string_address) "y"));
        settings.runner.child = 0;
        settings.queued = 0;
}

static fn check_settings_board_lines(void)
{
        int saved = settings_sub_count, count = 0;

        settings_sub_count = 0, settings_sub_owner = 0;
        for (int k = 0; k < 90; k++)
                count = settings_sub_put(0, false, null, "a line", count);
        check("settings board: the board grows with its lines, so 90 lines are all kept (none left out, the last is the last)",
              settings_sub_count == 90 && string_equals((string_address)settings_sub[89].text, (string_address) "a line"));
        settings_sub_count = saved;
}

static fn check_settings_layout_press(void)
{
        const int list = 0;
        static struct settings_item kept_items[CHECK_LIST_ROWS];
        int kept_count = settings.item_count[list];
        positive kept = (positive)(kept_count < CHECK_LIST_ROWS ? kept_count : CHECK_LIST_ROWS);

        if (kept)
                memory_copy(kept_items, settings.item[list], sizeof(*kept_items) * kept);
        //      The list's rows are the product's to make (settings_item_slot reserves and clears each one): the list can
        //      have no room yet, and six rows are written below, the press's five and the one that comes in.
        for (positive at = 0; at < 6; at++)
                if (!settings_item_slot(list, at))
                {
                        check("settings list: the press rows have room", false);
                        return;
                }
        settings.item_count[list] = 5;
        for (int at = 0; at < 5; at++)
                string_copy_bounded((string_address)settings.item[list][at].label, (string_address) "device", sizeof(settings.item[list][at].label));
        settings_rebuild();
        ui_press_begin(&settings.page.press, 10, 10, 3, false);
        // A device comes in at the top while the press on position 3 is held: the rows move down one.
        for (int at = settings.item_count[list]; at > 0; at--)
                settings.item[list][at] = settings.item[list][at - 1];
        string_copy_bounded((string_address)settings.item[list][0].label, (string_address) "new device", sizeof(settings.item[list][0].label));
        settings.item_count[list]++;
        settings_rebuild();
        check("settings list: a row that comes in under a held press lets the press go, so its release clicks nothing (the new row is not clicked)",
              !settings.page.press.down && ui_press_release(&settings.page.press, false, 3) == -1);
        if (kept)
                memory_copy(settings.item[list], kept_items, sizeof(*kept_items) * kept);
        settings.item_count[list] = kept_count;
        ui_press_end(&settings.page.press, true);
        settings_rebuild();
}

static fn check_settings_rows_moved(void)
{
        check_settings_queue_rows();
        check_settings_board_lines();
        check_settings_layout_press();
}

static fn check_settings_seam(void)
{
        struct ui address_to ui = &settings.ui;
        struct ui_page address_to page = &settings.page;
        struct ui_list address_to list = &settings.list;
        const int width = settings.width, track = UI_SCROLL_WIDTH * ui->scale / 100;
        int seam = page->seam, middle = page->column_scroll.x + track / 2, row = ui_dev(ui, UI_ROW);
        int y, gaps = 0, past = 0, left_bar = 0, thumb_top = 0, thumb_length = 0, under = 0, stray = 0;
        unsigned int paper;
        b32 geometry, hits, joined, ring_ends, bar_over, margins;

        settings.focused = 1, settings.page.pane = 1, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings_filter_clear(), (void)0;
        settings_group_choose(0);
        ui_scroll_set(&list->scroll, 0);
        ui_scroll_set(&page->column_scroll, 0);
        settings_layout();
        list->focus = list->id[0];
        geometry = seam >= middle - 1 && seam <= middle && list->row_x == seam && list->row_x + list->row_w == width && list->x == page->left &&
                   list->x + list->w == page->right && page->left > seam && page->right < width - track;
        y = ui_list_row_y(list, 0) + 2;
        hits = ui_page_column_hit(ui, page, seam - 1, page->top + 5) == 0 && ui_page_column_hit(ui, page, seam, page->top + 5) == -1 &&
               ui_list_hit(ui, list, seam, y) == list->id[0] && ui_list_hit(ui, list, seam - 1, y) == -1 &&
               ui_list_hit(ui, list, width - 1, y) == list->id[0] && ui_list_hit(ui, list, width, y) == -1 &&
               ui_page_region(ui, page, seam - 1, y) == &page->column_scroll && ui_page_region(ui, page, seam, y) == page->body;
        check("settings seam: the seam is the middle of the column's track; the column's rows end there, the list's begin and run to the window's edge, and its words keep the page's margins",
              geometry);
        check("settings seam: a point just left of the seam is the column's, at the seam the list's, and the window's last pixel is a row's", hits);

        // The keys on the first row, the column's first group chosen: one band of lit pixels from the window's left edge to its right edge.
        settings_seam_draw();
        paper = settings_frames_whole[(page->top - 3) * SETTINGS_FRAMES_W + width - 1];
        y = ui_list_row_y(list, 0) + 1;
        for (int x = 0; x < width; x++)
                gaps += settings_frames_whole[y * SETTINGS_FRAMES_W + x] == paper;
        joined = y >= page->top && y < page->top + row && gaps == 0;
        check("settings seam: the chosen group's row and the row the keys are on make one lit band from the window's left edge to its right edge", joined);

        // A long group, the keys on its third row, which lies in the thumb's reach: the row's ring spans from the seam to the edge,
        // the thumb is drawn over it, and the track beside the thumb is the row's, not the window's.
        settings_group_choose(settings_group_of(settings_row_named("Background")));
        ui_scroll_set(&list->scroll, 0);
        settings_layout();
        list->focus = list->id[2];
        settings_seam_draw();
        paper = settings_frames_whole[(page->top - 3) * SETTINGS_FRAMES_W + width - 1];
        y = ui_list_row_y(list, 2);
        if (!ui_scroll_thumb(ui, &list->scroll, &thumb_top, &thumb_length))
                thumb_top = thumb_length = 0;
        for (int x = seam; x < width; x++)
                past += settings_frames_whole[(y + 1) * SETTINGS_FRAMES_W + x] == paper;
        ring_ends = past == 0 && settings_frames_whole[(y + 1) * SETTINGS_FRAMES_W + seam - 3 * ui->scale / 100 - 1] == paper && y > page->top + row &&
                    settings_frames_whole[(y + 1) * SETTINGS_FRAMES_W + width - 1] != paper;
        {
                int bar = list->scroll.x, middle_y = y + row / 2;
                unsigned int beside = settings_frames_whole[middle_y * SETTINGS_FRAMES_W + bar + 1], thumb = settings_frames_whole[middle_y * SETTINGS_FRAMES_W + bar + track / 2];

                left_bar = beside != paper && settings_frames_whole[middle_y * SETTINGS_FRAMES_W + bar - 1] != paper;
                bar_over = thumb_length > 0 && y >= thumb_top && y + row <= thumb_top + thumb_length && left_bar && thumb != beside;
        }
        check("settings seam: the row the keys are on is lit from the seam to the window's last pixel, and not left of the seam", ring_ends);
        check("settings seam: the list's track is drawn over the row's blank end: the row is lit on both sides of the thumb, and the thumb is over it", bar_over);

        // The column's track lies across the seam, so its right half is over the list's rows: the row the keys are on is lit there, and the
        // column's thumb is drawn over that row, not under it (the bars are the last things the page draws).
        {
                int column_top = 0, column_length = 0, middle_y = y + row / 2, inside, beside;
                b32 column_over;

                page->column_scroll.content = page->column_scroll.view * 2;
                settings_seam_draw();
                if (!ui_scroll_thumb(ui, &page->column_scroll, &column_top, &column_length))
                        column_top = column_length = 0;
                inside = settings_frames_whole[middle_y * SETTINGS_FRAMES_W + seam + 1];
                beside = settings_frames_whole[middle_y * SETTINGS_FRAMES_W + seam + 3 * ui->scale / 100];
                column_over = column_length > 0 && middle_y >= column_top && middle_y < column_top + column_length && beside != paper && inside != beside;
                check("settings seam: the column's thumb, in the half of its track that is over the list, is drawn over the row the keys are on", column_over);
                settings_layout();
        }

        // Nothing but the rows' fills and the thumbs is drawn past the margins: between the list's words and the window's edge (but for the
        // track), and between the seam and the list's left margin, on every row but the one the keys are on.
        list->focus = -1;
        settings.page.pane = 0;
        settings.focused = 0;
        settings_seam_draw();
        paper = settings_frames_whole[(page->top - 3) * SETTINGS_FRAMES_W + width - 1];
        for (int row_y = list->y; row_y < list->y + list->h; row_y++)
        {
                for (int x = page->right; x < width; x++)
                        if (!(x >= list->scroll.x + 2 && x < list->scroll.x + track - 2) && settings_frames_whole[row_y * SETTINGS_FRAMES_W + x] != paper)
                        {
                                stray++;
                                if (stray == 1)
                                        string_format(log, "  seam stray: first at %b,%b is %p\n", (bipolar)x, (bipolar)row_y, (positive)settings_frames_whole[row_y * SETTINGS_FRAMES_W + x]);
                        }
                for (int x = seam + 1; x < list->x; x++)
                        if (!(x >= page->column_scroll.x + 2 && x < page->column_scroll.x + track - 2) && settings_frames_whole[row_y * SETTINGS_FRAMES_W + x] != paper)
                                stray++;
        }
        for (int row_y = list->y; row_y < list->y + list->h; row_y++)
                under += settings_frames_whole[row_y * SETTINGS_FRAMES_W + list->scroll.x + track / 2] != paper;
        margins = stray == 0 && under > 0;
        string_format(log, "  settings seam (scale %p): seam %b (track middle %b), rows %b to %b, words %b to %b; %p unlit pixels in the join band, %p in the ring, %p stray pixels in the margins, thumb %b at %b\n",
                      (positive)ui->scale / 100, (bipolar)seam, (bipolar)middle, (bipolar)list->row_x, (bipolar)(list->row_x + list->row_w), (bipolar)list->x,
                      (bipolar)(list->x + list->w), (positive)gaps, (positive)past, (positive)stray, (bipolar)thumb_length, (bipolar)thumb_top);
        check("settings seam: no pixel is drawn in the margins between the words and the window's edge or the seam, but the thumb in its track", margins);
        settings.page.pane = 1;
        settings.focused = 1;
        settings_group_choose(0);
}

/*
        The page's search field, as the settings frames just drew it: the kit's field, the title band's whole height (UI_PAGE_TOP
        from the window's top: it melts into the band, with no gap above or below), and clear of the close button
        and the minimise button beside it at the top right (their width and their inset from the edge). The band is the
        field's box, where the page's frame has it: what it draws is in the band, above the rows the list repaints.
*/
static fn check_settings_search(void)
{
        const struct ui_control address_to c = &settings.page.search;
        int width = settings.width * 100 / settings.ui.scale;

        check("settings search: the page's field is the kit's, the title band high, and drawn", c->kind == UI_INPUT && c->h == UI_PAGE_TOP && c->items[0] != null);
        check("settings search: it fills the title band, from its top to its bottom",
              c->y == 0 && c->y + c->h == UI_PAGE_TOP);
        check("settings search: it is clear of the close button and its minimise beside it, at the top right",
              width - (c->x + c->w) >= 2 * (UI_CLOSE + UI_CLOSE_IN));
}

/*
        The Settings window's least size. It asks for it (the table's least_width and least_height are its least, logical pixels at its
        scale), and it is drawn no smaller whatever it is asked. At its least width, at 100 and 200 percent and both ways it reads, every
        group lays out with nothing over anything else and nothing past the window: in each row its parts side by side inside it, each
        control as wide as it is drawn; the title whole beside the search field; the column of groups, its bar and the rows side by side;
        and every element of the frame inside the window.
*/
static b32 settings_least_row(int id)
{
        struct ui address_to ui = &settings.ui;
        struct ui_list address_to l = &settings.list;
        const struct ui_tree address_to t = &ui_row_parts.tree;
        const struct ui_node address_to root = &t->node[0];
        struct ui_control address_to c = ui_list_control(l, id, 1);
        struct ui_row row;
        b32 good = true;

        ui_list_describe(l, id, &row);
        if (row.tall)
                return true;
        ui_row_tree(ui, l, id, &row, ui_dev(ui, UI_ROW), true);
        for (int a = 1; a < t->count; a++)
        {
                const struct ui_node address_to n = &t->node[a];

                if (n->children || n->w <= 0)
                        continue;
                good &= n->x >= root->x && n->x + n->w <= root->x + root->w;
                for (int b = a + 1; b < t->count; b++)
                {
                        const struct ui_node address_to m = &t->node[b];

                        if (!m->children && m->w > 0 && n->x < m->x + m->w && m->x < n->x + n->w)
                                good = false;
                }
        }
        if (ui_row_parts.control && c && c->kind != UI_INPUT)
                good &= ui_row_parts.control->w >= ui_dev(ui, c->w);
        if (ui_row_parts.label && ui_row_parts.label_one)
                good &= ui_row_parts.label->w >= (ui_row_parts.label_one < ui_row_label_least(ui) ? ui_row_parts.label_one : ui_row_label_least(ui));
        return good;
}

static fn settings_least_check(void)
{
        struct ui address_to ui = &settings.ui;
        struct windows address_to table = settings.own.table;
        b32 asked = true, rows = true, frame = true;
        int kept_scale = ui->scale;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        for (int scale = 1; scale <= 2; scale++)
                for (int rtl = 0; rtl < 2; rtl++)
                {
                        const struct ui_tree address_to frame_tree = &settings_large->view.tree;
                        int title = 0;

                        ui->scale = scale * 100, ui_kit.rtl = rtl;
                        ui_kit_settle();
                        settings_size(SETTINGS_WIDTH * scale, SETTINGS_HEIGHT * scale);
                        settings_size(100, 100);
                        asked &= settings.least_w > 0 && settings.width == settings.least_w && settings.height == settings.least_h &&
                                 table->least_width[settings.own.row] == (unsigned int)((settings.least_w * 100 + ui->scale - 1) / ui->scale) &&
                                 table->least_height[settings.own.row] == (unsigned int)((settings.least_h * 100 + ui->scale - 1) / ui->scale);
                        string_format(log, "  settings least: %b by %b at scale %b%s (%b by %b logical)\n", (bipolar)settings.least_w, (bipolar)settings.least_h,
                                      (bipolar)scale, (string_address)(rtl ? ", reading from the right" : ""), (bipolar)table->least_width[settings.own.row],
                                      (bipolar)table->least_height[settings.own.row]);
                        for (int group = 0; group < settings_groups(); group++)
                        {
                                const struct ui_page address_to p = &settings.page;
                                b32 apart;

                                settings.page.chosen = group;
                                settings_refilter();
                                settings.page.title = language_of("Settings");
                                ui_page_view(ui, &settings.page);
                                for (int at = 0; at < settings.list.count; at++)
                                        if (!settings_least_row(settings.list.id[at]))
                                        {
                                                if (rows)
                                                        string_format(log, "  settings least: at scale %b, group %b, row %b overlaps or runs past its end\n", (bipolar)scale,
                                                                      (bipolar)group, (bipolar)settings.list.id[at]);
                                                rows = false;
                                        }
                                title = ui_kit_measure(ui, (string_address)language_of("Settings"), UI_TITLE_LARGE);
                                apart = rtl ? p->list_box.x + p->list_box.w <= p->column_bar.x && p->column_bar.x + p->column_bar.w <= p->column_box.x
                                            : p->column_box.x + p->column_box.w <= p->column_bar.x && p->column_bar.x + p->column_bar.w <= p->list_box.x;
                                frame &= apart && p->title_box.w >= title && p->list_box.w >= ui_list_least(ui, &settings.list);
                                frame &= rtl ? ui_dev(ui, p->search.x + p->search.w) <= p->title_box.x : p->title_box.x + p->title_box.w <= ui_dev(ui, p->search.x);
                                for (int at = 0; at < frame_tree->count; at++)
                                        frame &= frame_tree->node[at].x >= 0 && frame_tree->node[at].x + frame_tree->node[at].w <= settings.width;
                        }
                }
        ui_kit.rtl = 0, ui->scale = kept_scale;
        ui_kit_settle();
        settings.page.chosen = 0;
        settings_size(SETTINGS_WIDTH * ui->scale / 100, SETTINGS_HEIGHT * ui->scale / 100);
        settings_refilter();
        check("settings least: the window asks for its least size (the table's least_width and least_height) and is drawn no smaller, asked smaller", asked);
        check("settings least: at its least width every row of every group has its parts side by side inside it, at 100 and 200 percent, either way it reads", rows);
        check("settings least: and its frame is whole: the title beside the search field, the column, its bar and the rows side by side, nothing past the window", frame);
}

/*
        The rows that show a setting (settings.c's settings_tries), in the Input group of a window tall enough to show them whole. The
        pointer's row draws, at its end, the pointer the style, size and look rows say, as the compositor does: classic, the arrow at the
        start of the pair is 21 cells of the size tall and 12 across (the desktop's pixels a logical pixel times the size's percent), and
        at 200 percent it is 200 over 80 times the arrow at 80; dynamic, the circle at rest is 16 cells across; and a press held on the row
        draws it smaller by the squeeze (25 percent, the pointer squeezing on a click). The double-click row lights its time apart in the
        selected tab's pill when two clicks on it come within the double-click row's time, and not when they come after it, which a
        longer time makes a pair within; Enter on it is no click. Enter on the key repeat's row opens its field, and Enter in it closes it
        and changes no setting.
*/
static int settings_tries_values(const char address_to path, const char address_to value)
{
        int row = settings_row_of(path);

        if (row >= 0)
                settings.state[row] = SETTINGS_READ, string_copy_bounded((string_address)settings.value[row], (string_address)value, SETTINGS_VALUE);
        return row;
}

// The pointer's row drawn, and the box round what is not the row's ground in its part at the row's end (device pixels), from the part's
// start side to the middle of the gap after the first pointer: that pointer's height, and its width.
static int settings_tries_pointer(int address_to wide)
{
        struct ui address_to ui = &settings.ui;
        int id = SETTINGS_TRY + SETTINGS_TRY_POINTER, at, y, top = 1 << 30, bottom = -1, left = 1 << 30, right = -1, cell, across;
        const struct ui_node address_to n;
        struct ui_row row;
        unsigned int ground;
        b32 dynamic = !settings_value_is(SETTING_CURSOR_STYLE_PATH, "classic");

        settings_rebuild();
        at = settings_pos(id);
        if (at < 0)
                return -1;
        ui_scroll_set(&settings.list.scroll, settings.list.top[at]);
        settings_layout();
        settings.list.focus = -1, settings.list.hover = -1;
        ui_kit.now += 2000000000ll, ui_kit.busy = 0;
        settings_draw_rows(settings_frames_whole, 0, 0);
        y = ui_list_row_y(&settings.list, at);
        ui_list_describe(&settings.list, id, &row);
        ui_row_tree(ui, &settings.list, id, &row, settings.list.tall[at], true);
        ui_tree_origin(&ui_row_parts.tree, settings.list.x, y);
        if (!(n = ui_row_parts.picture))
                return -1;
        cell = settings_try_cell(ui->scale, settings.list.down == id, dynamic);
        across = (dynamic ? 16 : 12) * cell / 256 + (dynamic ? 8 : 9) * cell / 256 + ui_px(ui, UI_GAP4);
        ground = settings_frames_whole[(positive)n->y * SETTINGS_FRAMES_W + (positive)n->x];
        for (int py = n->y; py < n->y + n->h; py++)
                for (int px = n->x + n->w - across; px < n->x + n->w - across + (dynamic ? 16 : 12) * cell / 256 + ui_px(ui, UI_GAP4) / 2; px++)
                        if (settings_frames_whole[(positive)py * SETTINGS_FRAMES_W + (positive)px] != ground)
                        {
                                top = py < top ? py : top, bottom = py > bottom ? py : bottom;
                                left = px < left ? px : left, right = px > right ? px : right;
                        }
        *wide = right >= left ? right - left + 1 : 0;
        return bottom >= top ? bottom - top + 1 : 0;
}

static fn settings_tries_check(void)
{
        struct ui address_to ui = &settings.ui;
        int input = settings_groups_named("Input"), cell, tall80, tall200, wide80, wide200, ring_h, ring_w, squeezed_h, squeezed_w, arrow_w;
        int click_row = settings_row_of(SETTING_DOUBLE_CLICK_PATH), click = SETTINGS_TRY + SETTINGS_TRY_CLICK, repeat = SETTINGS_TRY + SETTINGS_TRY_REPEAT;
        b32 lit_within = false, lit_after = true, lit_longer = false, keyed = false, field = false;
        char kept[6][SETTINGS_VALUE];
        static const char address_to const paths[6] = {SETTING_CURSOR_STYLE_PATH, SETTING_CURSOR_SIZE_PATH, SETTING_CURSOR_LOOK_PATH, SETTING_DOUBLE_CLICK_PATH,
                                                       SETTING_SQUEEZE_PATH, SETTING_CURSOR_CLICK_PATH};

        for (int k = 0; k < 6; k++)
                string_copy_bounded((string_address)kept[k], (string_address)settings.value[settings_row_of(paths[k])], SETTINGS_VALUE);
        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings.page.chosen = input;
        settings_size(SETTINGS_WIDTH * ui->scale / 100, SETTINGS_FRAMES_H);
        settings_tries_values(SETTING_CURSOR_LOOK_PATH, "auto");
        settings_tries_values(SETTING_SQUEEZE_PATH, "25");
        settings_tries_values(SETTING_CURSOR_CLICK_PATH, "on");
        settings_tries_values(SETTING_CURSOR_STYLE_PATH, "classic");
        settings_tries_values(SETTING_CURSOR_SIZE_PATH, "80");
        tall80 = settings_tries_pointer(&wide80);
        settings_tries_values(SETTING_CURSOR_SIZE_PATH, "200");
        tall200 = settings_tries_pointer(&wide200);
        settings.list.down = SETTINGS_TRY + SETTINGS_TRY_POINTER;
        squeezed_h = settings_tries_pointer(&squeezed_w);
        settings.list.down = -1;
        settings_tries_values(SETTING_CURSOR_STYLE_PATH, "dynamic");
        ring_h = settings_tries_pointer(&ring_w);
        settings_tries_values(SETTING_CURSOR_SIZE_PATH, "80");
        cell = (compositor_px1(ui->scale) * 256 * 80 + 50) / 100;
        arrow_w = 12 * cell / 256;
        string_format(log, "  settings tries: the arrow %b by %b at 80, %b by %b at 200, %b by %b pressed; the circle %b by %b at 200\n", (bipolar)wide80,
                      (bipolar)tall80, (bipolar)wide200, (bipolar)tall200, (bipolar)squeezed_w, (bipolar)squeezed_h, (bipolar)ring_w, (bipolar)ring_h);
        check("settings tries: the pointer's row draws the classic arrow 21 cells of the size tall, two and a half times that at 200 percent, and the dynamic circle 16 across",
              tall80 >= 21 * cell / 256 - 1 && tall80 <= 21 * cell / 256 + 2 && wide80 >= arrow_w - 1 && wide80 <= arrow_w + 2 &&
                  tall200 * 80 >= tall80 * 200 - 3 * 80 && tall200 * 80 <= tall80 * 200 + 3 * 80 && ring_h != tall200 && ring_w >= ring_h - 1 &&
                  ring_w <= ring_h + 1 && ring_h * 256 >= 16 * cell * 200 / 80 - 2 * 256 && ring_h * 256 <= 16 * cell * 200 / 80 + 3 * 256);
        check("settings tries: and a press held on the row draws the pointer smaller, by the squeeze", squeezed_h > 0 && squeezed_h < tall200 && squeezed_w < wide200);

        // The double-click row: two presses on it 300 ms apart with 450 ms to come in light it; 620 ms apart do not, until the time is 700.
        settings_tries_values(SETTING_DOUBLE_CLICK_PATH, "450");
        settings_rebuild();
        if (click_row >= 0 && settings_pos(click) >= 0)
        {
                int at = settings_pos(click), x = settings.list.x + settings.list.w / 2, y;

                ui_scroll_set(&settings.list.scroll, settings.list.top[at]);
                settings_layout();
                y = ui_list_row_y(&settings.list, at) + settings.list.tall[at] / 2;
                for (int pair = 0; pair < 3; pair++)
                {
                        static const unsigned int apart[3] = {300, 620, 620};
                        unsigned int primary = ui_role(UI_PRIMARY) | 0xff000000u;
                        b32 pill = false;
                        struct ui_row row;

                        if (pair == 2)
                                settings_tries_values(SETTING_DOUBLE_CLICK_PATH, "700");
                        settings_click_try.at = 0, settings_click_try.apart = 0;
                        ui_kit.stamp = 0;
                        ui_kit.now += 5000000000ll;
                        settings_try_press(click, x, y);
                        ui_kit.now += (b64)apart[pair] * 1000000ll;
                        settings_try_press(click, x, y);
                        ui_kit.busy = 0;
                        settings.list.focus = -1, settings.list.hover = -1;
                        settings_draw_rows(settings_frames_whole, 0, 0);
                        // The pill is the part at the row's end: a pixel inside its round end, on its middle line, is the accent.
                        ui_list_describe(&settings.list, click, &row);
                        ui_row_tree(ui, &settings.list, click, &row, settings.list.tall[at], true);
                        ui_tree_origin(&ui_row_parts.tree, settings.list.x, ui_list_row_y(&settings.list, at));
                        if (ui_row_parts.picture)
                                pill = settings_frames_whole[(positive)(ui_row_parts.picture->y + ui_row_parts.picture->h / 2) * SETTINGS_FRAMES_W +
                                                             (positive)(ui_row_parts.picture->x + ui_px(ui, 10))] == primary;
                        if (pair == 0)
                                lit_within = settings_click_try.apart == 300 && pill;
                        else if (pair == 1)
                                lit_after = settings_click_try.apart == 620 && (pill || !row.value);
                        else
                                lit_longer = settings_click_try.apart == 620 && pill;
                }
        }
        settings_click_try.at = 0, settings_click_try.apart = 0;
        settings.runner.child = 1;
        settings_activate(click, 1), settings_activate(click, 1);
        keyed = !settings_click_try.apart && !settings_click_try.at;
        check("settings tries: the double-click row lights its time apart when two clicks on it come within the double-click time, and not after it",
              lit_within && !lit_after && lit_longer);
        check("settings tries: and Enter on it is no click", keyed);

        // The key repeat's field: Enter opens it, typed in and Enter closes it, and nothing is changed.
        {
                positive queued = settings.queued;

                settings.runner.child = 1;
                settings_activate(repeat, 1);
                field = settings_editing() == repeat;
                ui_text_set(&settings.edit, (const p8 address_to) "kkkk", 4);
                settings_edit_commit();
                field &= settings_editing() < 0 && settings.queued == queued;
        }
        check("settings tries: Enter on the key repeat's row opens its field, and Enter in it closes it and changes no setting", field);
        for (int k = 0; k < 6; k++)
                settings_tries_values(paths[k], kept[k]);
        settings_size(SETTINGS_WIDTH * ui->scale / 100, SETTINGS_HEIGHT * ui->scale / 100);
}

/*
        The groups in parts (registry.c's part): a group's rows are listed in the parts it names, in the table's order, every row in the
        part of the last row at or before it that starts one (or the group's own, under its name), and the list heads each part with its
        name over its first row and nowhere else: Input is Keyboard, Mouse, Pointer, Touch and Edge swipes. What is typed in the filter
        lists a part's header only over a row it lists, so a part none of whose rows is listed has no header. A row that says what it
        does has it under its label: the row is a row and a line of the quiet role taller, and its switch is in the middle of it.
*/
static fn check_settings_parts(void)
{
        struct ui address_to ui = &settings.ui;
        static const char address_to const input_parts[] = {"Keyboard", "Mouse", "Pointer", "Touch", "Edge swipes"};
        b32 ordered = true, headed = true, input = true, filtered = true, tall = false, centred = false;
        int parts = 0;

        settings_filter_clear(), (void)0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1, settings.page.card = 0;
        settings_size(SETTINGS_WIDTH * ui->scale / 100, SETTINGS_HEIGHT * ui->scale / 100);
        for (int group = 0; group < settings_groups() - 1; group++)
        {
                int last = -1;

                settings.page.chosen = group;
                settings_rebuild();
                for (int at = 0; at < settings.list.count; at++)
                {
                        int id = settings.list.id[at], start;
                        struct ui_row row;

                        // A row that shows a setting is in the part of the row it is beside, and may be the part's first.
                        if (id >= SETTINGS_ANY && settings_try_of(id) < 0)
                                continue;
                        ui_list_describe(&settings.list, id, &row);
                        start = id < SETTINGS_ANY ? settings_section_start(id) : row.section;
                        ordered &= start >= last && settings_group_of(start) == group && (id >= SETTINGS_ANY || start <= id);
                        for (int between = start + 1; between <= id && id < SETTINGS_ANY; between++)
                                ordered &= !settings_rows[between].part && !settings_rows[between].section;
                        headed &= ((settings.list.starts[at] & UI_LIST_HEADED) != 0) == (start != last) &&
                                  string_equals((string_address)row.header, (string_address)language_of(settings_rows[start].part ? settings_rows[start].part
                                                                                                                                  : settings_section(id)));
                        if (start != last && string_equals((string_address)settings_group_name(group), (string_address) "Input"))
                                input &= parts < (int)(sizeof(input_parts) / sizeof(input_parts[0])) && settings_rows[start].part &&
                                         string_equals((string_address)settings_rows[start].part, (string_address)input_parts[parts++]);
                        last = start;
                }
        }
        check("settings parts: each group's rows are listed in the parts it names, in order, each part headed by its name over its first row alone",
              ordered && headed);
        check("settings parts: Input is in five parts, Keyboard, Mouse, Pointer, Touch and Edge swipes", input && parts == 5);
        // A filter that lists the key repeat rows from Keyboard alone heads them Keyboard, and no part over nothing.
        settings.page.chosen = 0;
        settings_groups_filter("repeat", 6);
        {
                int heads = 0;

                // No row that shows a setting is listed while the filter lists rows: it would head a part with no row of the part.
                for (int at = 0; at < settings.list.count; at++)
                        filtered &= settings_try_of(settings.list.id[at]) < 0;
                for (int at = 0; at < settings.list.count; at++)
                        if (settings.list.starts[at] & UI_LIST_HEADED)
                        {
                                struct ui_row row;

                                ui_list_describe(&settings.list, settings.list.id[at], &row);
                                heads++;
                                filtered &= settings.list.id[at] < (int)SETTINGS_ALL && settings_matches(settings.list.id[at]);
                                filtered &= heads > 1 || string_equals((string_address)row.header, (string_address) "Keyboard");
                        }
                filtered &= heads >= 1 && settings.list.count >= 2;
        }
        settings_filter_clear(), (void)0;
        check("settings parts: the filter heads only the parts of the rows it lists: \"repeat\" lists Keyboard's two, under Keyboard", filtered);
        // Mouse acceleration says what it does under its label: a row and a line taller, its switch in the middle of it.
        {
                int row_id = -1, at;

                for (positive each = 0; each < SETTINGS_ROWS; each++)
                        if (settings_row_is((int)each, SETTING_ACCELERATION_PATH))
                                row_id = (int)each;
                settings.page.chosen = settings_group_of(row_id);
                settings_rebuild();
                at = settings_pos(row_id);
                if (at >= 0)
                {
                        struct ui_row row;
                        const struct ui_row_parts address_to p = &ui_row_parts;

                        ui_list_describe(&settings.list, row_id, &row);
                        tall = row.about && settings.list.whole[at] == ui_dev(ui, UI_ROW) + ui_wrap_pitch(ui, UI_LABEL_MEDIUM, ui_types[UI_LABEL_MEDIUM].size);
                        ui_row_tree(ui, &settings.list, row_id, &row, settings.list.tall[at], true);
                        centred = p->toggle && p->about_shown && p->label && 2 * p->toggle->y + p->toggle->h == 2 * p->tree.node[0].y + p->tree.node[0].h &&
                                  p->label->h == p->tree.node[0].h;
                }
        }
        check("settings parts: a row that says what it does is a row and a line of the quiet role tall, the line under its label, its switch in the middle",
              tall && centred);
        settings_tries_check();
        settings.page.chosen = 0;
        settings_refilter();
}

#ifdef PAGE_DUMP
#include PAGE_DUMP_FILE
#endif
static fn check_settings_frames(string_address regular, string_address bold)
{
        static font_face faces[2];
        static p8 atlas[1024 * 1024];
        positive size = 0, bytes = compositor_map_file(regular, &size), bold_size = 0, bold_bytes = bold ? compositor_map_file(bold, &bold_size) : 0;
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        positive buffer = (positive)SETTINGS_FRAMES_W * SETTINGS_FRAMES_H * 4;
        long table = compositor_call(syscall(mmap), 0, WINDOWS_HEADER + 2 * buffer, 3, 0x4022, (positive)-1, 0);
        struct settings_frames_saved
        {
                struct ui ui;
                struct window_row own;
                int width, height, shown, group;
                struct ui_list_slot address_to pool;
        } saved = {settings.ui, settings.own, settings.width, settings.height, settings.page.buffers.shown, settings.page.chosen, settings.list.pool};

        if (!bytes || (engine < 0 && engine > -4096) || (table < 0 && table > -4096) || !settings_map() ||
            font_load(&faces[0], (const p8 address_to)bytes, size, 0) != 0)
        {
                check("settings frames: the face, the engine and the window load", false);
                return;
        }
        settings_frames.table = (struct windows address_to)table;
        settings_frames.table->stride = SETTINGS_FRAMES_W;
        settings_frames.table->buffer = buffer;
        settings_frames.table->output_width = 4096;
        settings_frames.table->output_height = 4096;
        settings.own = (struct window_row){settings_frames.table, 0, -1, -1, WINDOWS_HEADER + 2 * buffer, buffer, SETTINGS_FRAMES_W, 4096, 4096};
        memory_zero(&settings.ui, sizeof(settings.ui));
        settings.ui.engine = (font_engine address_to)engine;
        font_start(settings.ui.engine, atlas, 1024, 1024);
        settings.ui.regular = &faces[0];
        settings.ui.bold = bold_bytes && font_load(&faces[1], (const p8 address_to)bold_bytes, bold_size, 0) == 0 ? &faces[1] : null;
        settings.ui.scale = 100;
        settings.ui.opaque = 1;
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 0); // full motion, the theme put at once
        ui_inks(&settings.ui, 0x101012u);
        ui_kit_inks(&settings.ui);
        ui_kit_settle();
        check_settings_edges();
        check_settings_stuck_child();
        ui_buffers_start(&settings.page.buffers);
        settings.focused = 1;
        settings_frames.now = 5000000000ull;
        ui_kit.drawn = 0;
        ui_kit.busy = 0;
        for (int scale = 1; scale <= 2; scale++)
        {
        // The window at a scale: placed again, its size the window's own at it, drawn whole once.
        settings.ui.scale = scale * 100;
        ui_kit_settle();
        settings_size(SETTINGS_WIDTH * scale, SETTINGS_HEIGHT * scale);
        settings_refilter();
        settings_frames_pass();
        check_settings_search();
        check_settings_seam();
        check_settings_rows_moved();
        check_settings_clicks();

        settings_frames_scenario((string_address) "Canvas, Space and Down every 33 ms, answered at 25", false, false,
                                 "Open windows where they were last put", UI_KEY_SPACE, UI_KEY_DOWN, 6, 33, 25);
        settings_frames_scenario((string_address) "Input, Space and Down every 33 ms, answered at 25", false, false,
                                 "Shake the pointer to find it", UI_KEY_SPACE, UI_KEY_DOWN, 6, 33, 25);
        settings_frames_scenario((string_address) "switches, Space and Down every 33 ms, answered at 25", true, false,
                                 "Shake the pointer to find it", UI_KEY_SPACE, UI_KEY_DOWN, 12, 33, 25);
        settings_frames_scenario((string_address) "switches, Space and Down every 33 ms, answered at 140", true, false,
                                 "Shake the pointer to find it", UI_KEY_SPACE, UI_KEY_DOWN, 12, 33, 140);
        settings_frames_scenario((string_address) "switches, Space and Down every 120 ms, answered at 60", true, false,
                                 "Shake the pointer to find it", UI_KEY_SPACE, UI_KEY_DOWN, 12, 120, 60);
        settings_frames_scenario((string_address) "switches, Space and Down every 50 ms, answered at 500", true, false,
                                 "Shake the pointer to find it", UI_KEY_SPACE, UI_KEY_DOWN, 12, 50, 500);
        // The column of groups: Down a group at a time, slowly and as fast as a held key repeats, and back up.
        settings_frames_scenario((string_address) "the column, Down every 500 ms", false, true, "Name", UI_KEY_DOWN, 0, 8, 500, 0);
        settings_frames_scenario((string_address) "the column, Down every 33 ms", false, true, "Name", UI_KEY_DOWN, 0, 8, 33, 0);
        settings_frames_scenario((string_address) "the column, Up every 50 ms from the last group", false, true, "Mount kept disks",
                                 UI_KEY_UP, 0, 8, 50, 0);

        // The pointer, the wheel, the theme and the size, with the rows read as a machine says them.
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];

                if (row->kind == SETTINGS_SWITCH)
                        continue;
                settings.state[at] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[at],
                                    (string_address)(settings_row_is((int)at, SETTING_THEME_PATH) ? "dark" : settings_row_is((int)at, SETTING_ACCENT_PATH) ? "amber"
                                                     : settings_row_is((int)at, SETTING_MOTION_PATH) ? "full" : row->standard ? row->standard : "1"),
                                    SETTINGS_VALUE);
        }
        {
                // Canvas holds the desktop's drawing; the pointer, the wheel and touch are Input's, so Input has its own scripts.
                int canvas = settings_group_of(settings_row_named("Background")), input = settings_group_of(settings_row_named("Cursor size (percent, or default)"));
                static const struct settings_frames_step hover[] = {
                    {0, SF_ROW, "Background", 450, 0}, {30, SF_ROW, "Background colour (rrggbb or default)", 450, 0},
                    {90, SF_ROW, "Theme", 300, 0}, {120, SF_ROW, "Accent", 300, 0}, {150, SF_ROW, "Motion", 300, 0},
                    {180, SF_ROW, "Caret blink speed", 450, 0}, {400, SF_ROW, "Caret thickness", 450, 0}, {420, SF_ROW, "Background", 450, 0}};
                static const struct settings_frames_step press[] = {
                    {0, SF_KEY, null, UI_KEY_END, 0},
                    {5, SF_ROW, "Open windows where they were last put", 200, 0}, {20, SF_DOWN, null, 0, 0}, {140, SF_UP, null, 0, 0},
                    {200, SF_ANSWER, null, 0, 0}, {260, SF_ROW, "Clipboard", 600, 0}, {280, SF_DOWN, null, 0, 0},
                    {300, SF_UP, null, 0, 0}, {320, SF_ROW, "Clipboard", 400, 0}, {330, SF_DOWN, null, 0, 0}, {340, SF_UP, null, 0, 0},
                    {360, SF_ANSWER, null, 0, 0}, {380, SF_ANSWER, null, 0, 0}};
                static const struct settings_frames_step input_press[] = {
                    {0, SF_KEY, null, UI_KEY_END, 0},
                    {5, SF_ROW, "Shake the pointer to find it", 200, 0}, {20, SF_DOWN, null, 0, 0}, {140, SF_UP, null, 0, 0},
                    {200, SF_ANSWER, null, 0, 0}, {260, SF_ROW, "Show touch area", 600, 0}, {280, SF_DOWN, null, 0, 0},
                    {300, SF_UP, null, 0, 0}, {320, SF_ROW, "Smooth mouse wheel", 400, 0}, {330, SF_DOWN, null, 0, 0}, {340, SF_UP, null, 0, 0},
                    {360, SF_ANSWER, null, 0, 0}, {380, SF_ANSWER, null, 0, 0}};
                static const struct settings_frames_step parts[] = {
                    {0, SF_PART, "Theme", 0, 0}, {20, SF_DOWN, null, 0, 0}, {60, SF_UP, null, 0, 0}, {90, SF_ANSWER, null, 0, 0},
                    {150, SF_PART, "Theme", 2, 0}, {160, SF_DOWN, null, 0, 0}, {170, SF_UP, null, 0, 0}, {175, SF_ANSWER, null, 0, 0},
                    {200, SF_PART, "Accent", 4, 0}, {210, SF_DOWN, null, 0, 0}, {230, SF_UP, null, 0, 0}, {240, SF_ANSWER, null, 0, 0},
                    {260, SF_PART, "Motion", 1, 0}, {270, SF_DOWN, null, 0, 0}, {280, SF_UP, null, 0, 0}, {300, SF_ANSWER, null, 0, 0},
                    {320, SF_PART, "Motion", 0, 0}, {330, SF_DOWN, null, 0, 0}, {340, SF_UP, null, 0, 0}, {350, SF_ANSWER, null, 0, 0}};
                static const struct settings_frames_step wheel[] = {
                    {0, SF_ROW, "Theme", 450, 0}, {10, SF_KEY, null, UI_KEY_END, 0}, {12, SF_KEY, null, UI_KEY_UP, 0},
                    {14, SF_KEY, null, UI_KEY_UP, 0}, {20, SF_KEY, null, UI_KEY_SPACE, 0},
                    {40, SF_ANSWER, null, 0, 0}, {60, SF_WHEEL, null, 120, 0}, {80, SF_WHEEL, null, 120, 0}, {100, SF_DOWN, null, 0, 0},
                    {130, SF_WHEEL, null, -120, 0}, {150, SF_UP, null, 0, 0}, {170, SF_WHEEL, null, 360, 0}, {190, SF_WHEEL, null, -720, 0}};
                static const struct settings_frames_step turns[] = {
                    {0, SF_ROW, "Theme", 450, 0}, {10, SF_WHEEL, null, -120, 0}, {20, SF_WHEEL, null, -120, 0},
                    {30, SF_WHEEL, null, -120, 0}, {40, SF_WHEEL, null, -120, 0}, {200, SF_WHEEL, null, -360, 0},
                    {230, SF_WHEEL, null, 120, 0}, {600, SF_WHEEL, null, 720, 0}, {900, SF_KEY, null, UI_KEY_END, 0},
                    {1000, SF_KEY, null, UI_KEY_HOME, 0}, {1400, SF_KEY, null, UI_KEY_DOWN, 0},
                    {1420, SF_KEY, null, UI_KEY_DOWN, 0}, {1440, SF_WHEEL, null, -240, 0}};
                static const struct settings_frames_step column[] = {
                    {0, SF_GROUP, null, 3, 0}, {40, SF_GROUP, null, 4, 0}, {80, SF_DOWN, null, 0, 0}, {200, SF_UP, null, 0, 0},
                    {230, SF_GROUP, null, 6, 0}, {240, SF_DOWN, null, 0, 0}, {250, SF_UP, null, 0, 0}, {260, SF_GROUP, null, 1, 0},
                    {270, SF_DOWN, null, 0, 0}, {300, SF_UP, null, 0, 0}, {500, SF_GROUP, null, 0, 0}};
                static const struct settings_frames_step theme[] = {
                    {0, SF_ROW, "Background", 450, 0}, {20, SF_KEY, null, UI_KEY_DOWN, 0}, {40, SF_THEME, null, UI_THEME_LIGHT, 0},
                    {60, SF_ROW, "Background", 450, 0}, {100, SF_DOWN, null, 0, 0}, {120, SF_UP, null, 0, 0},
                    {300, SF_THEME, null, UI_THEME_DARK, 0}, {320, SF_THEME, null, UI_THEME_DARK, 0x40c0ff}, {340, SF_GROUP, null, 2, 0},
                    {350, SF_DOWN, null, 0, 0}, {360, SF_UP, null, 0, 0}, {700, SF_THEME, null, UI_THEME_DARK, 0}};
                static const struct settings_frames_step pull[] = {
                    {0, SF_FINGER_DOWN, "Background", 0, 0}, {20, SF_FINGER_MOVE, null, 10, 0}, {40, SF_FINGER_MOVE, null, 25, 0},
                    {60, SF_FINGER_MOVE, null, 45, 0}, {80, SF_FINGER_MOVE, null, 70, 0}, {100, SF_FINGER_MOVE, null, 95, 0},
                    {140, SF_FINGER_MOVE, null, 100, 0}, {300, SF_FINGER_MOVE, null, 60, 0}, {360, SF_FINGER_MOVE, null, 20, 0},
                    {420, SF_FINGER_UP, null, 0, 0}, {900, SF_FINGER_DOWN, "Clipboard", 0, 0}, {920, SF_FINGER_MOVE, null, -20, 0},
                    {940, SF_FINGER_MOVE, null, -60, 0}, {960, SF_FINGER_MOVE, null, -110, 0}, {1000, SF_FINGER_MOVE, null, -150, 0},
                    {1200, SF_FINGER_MOVE, null, -90, 0}, {1260, SF_FINGER_UP, null, 0, 0}};
                static const struct settings_frames_step card_pull[] = {
                    {0, SF_CARD, null, 0, 0}, {40, SF_FINGER_DOWN, null, 0, 0}, {60, SF_FINGER_MOVE, null, 14, 0},
                    {80, SF_FINGER_MOVE, null, 40, 0}, {100, SF_FINGER_MOVE, null, 75, 0}, {160, SF_FINGER_MOVE, null, 60, 0},
                    {240, SF_FINGER_UP, null, 0, 0}, {600, SF_FINGER_DOWN, null, 0, 0}, {620, SF_FINGER_MOVE, null, -30, 0},
                    {640, SF_FINGER_MOVE, null, -100, 0}, {660, SF_FINGER_MOVE, null, -160, 0}, {720, SF_FINGER_MOVE, null, -140, 0},
                    {900, SF_FINGER_UP, null, 0, 0}};
                static const struct settings_frames_step size[] = {
                    {0, SF_KEY, null, UI_KEY_END, 0},
                    {5, SF_ROW, "Open windows where they were last put", 600, 0}, {10, SF_DOWN, null, 0, 0}, {30, SF_UP, null, 0, 0},
                    {40, SF_ANSWER, null, 0, 0}, {60, SF_SIZE, null, 640, 470}, {90, SF_GROUP, null, 5, 0}, {100, SF_DOWN, null, 0, 0},
                    {120, SF_SIZE, null, 690, 515}, {130, SF_UP, null, 0, 0}, {300, SF_SIZE, null, SETTINGS_WIDTH, SETTINGS_HEIGHT}};
                static const struct settings_frames_step menu[] = {
                    {0, SF_ROW, "Caret blink speed", 450, 0}, {10, SF_DOWN, null, 0, 0}, {30, SF_UP, null, 0, 0},
                    {60, SF_KEY, null, UI_KEY_DOWN, 0}, {80, SF_KEY, null, UI_KEY_DOWN, 0}, {200, SF_KEY, null, UI_KEY_ENTER, 0},
                    {220, SF_ANSWER, null, 0, 0}, {300, SF_KEY, null, UI_KEY_ENTER, 0}, {320, SF_KEY, null, UI_KEY_ESCAPE, 0}};
                static const struct settings_frames_step input_menu[] = {
                    {0, SF_ROW, "Keyboard", 450, 0}, {10, SF_DOWN, null, 0, 0}, {30, SF_UP, null, 0, 0},
                    {60, SF_KEY, null, UI_KEY_DOWN, 0}, {80, SF_KEY, null, UI_KEY_DOWN, 0}, {200, SF_KEY, null, UI_KEY_ENTER, 0},
                    {220, SF_ANSWER, null, 0, 0}, {300, SF_KEY, null, UI_KEY_ENTER, 0}, {320, SF_KEY, null, UI_KEY_ESCAPE, 0}};

                settings_frames_play((string_address) "the pointer down the rows", canvas, hover, sizeof(hover) / sizeof(hover[0]));
                settings_frames_play((string_address) "switches pressed and let go", canvas, press, sizeof(press) / sizeof(press[0]));
                settings_frames_play((string_address) "segments and swatches pressed", canvas, parts, sizeof(parts) / sizeof(parts[0]));
                settings_frames_play((string_address) "the wheel while a switch moves", canvas, wheel, sizeof(wheel) / sizeof(wheel[0]));
                settings_frames_play((string_address) "wheel turns, then keys reveal", canvas, turns, sizeof(turns) / sizeof(turns[0]));
                settings_frames_play((string_address) "the column hovered and pressed", canvas, column, sizeof(column) / sizeof(column[0]));
                settings_frames_play((string_address) "the theme turned while things move", canvas, theme, sizeof(theme) / sizeof(theme[0]));
                settings_frames_play((string_address) "the window resized while things move", canvas, size, sizeof(size) / sizeof(size[0]));
                settings_frames_play((string_address) "a finger pulls the rows past their ends and lets go", canvas, pull, sizeof(pull) / sizeof(pull[0]));
                settings_frames_play((string_address) "a finger pulls the card past its ends and lets go", canvas, card_pull, sizeof(card_pull) / sizeof(card_pull[0]));
                settings_frames_play((string_address) "a choice's menu", canvas, menu, sizeof(menu) / sizeof(menu[0]));
                settings_frames_play((string_address) "Input: switches pressed and let go", input, input_press, sizeof(input_press) / sizeof(input_press[0]));
                settings_frames_play((string_address) "Input: a choice's menu", input, input_menu, sizeof(input_menu) / sizeof(input_menu[0]));
        }
        }
        settings.ui.scale = 100;
        ui_kit_settle();
        settings_size(SETTINGS_WIDTH, 300);
        settings_wheel_end_check((string_address) "a tall window, the wheel a notch at a time", 66, 70);
        settings_wheel_end_check((string_address) "a tall window, notches 150 ms apart", 150, 70);
        settings_wheel_end_check((string_address) "a tall window, notches 300 ms apart", 300, 70);
        settings.ui.scale = 200;
        ui_kit_settle();
        settings_size(SETTINGS_WIDTH * 2, 300 * 2);
        settings_wheel_end_check((string_address) "scale 2, the wheel a notch at a time", 66, 70);
        settings.ui.scale = 100;
        ui_kit_settle();
        settings_size(SETTINGS_WIDTH, 300);
        check("settings flick: a gentle flick 300 px above the last row settles on it, its carry at most a view's band past it, no return",
              settings_flick_check((string_address) "gentle, 300 px above", 300, 30, 3));
        check("settings flick: a flick 150 px above the last row settles on it, its carry at most a view's band past it, no return",
              settings_flick_check((string_address) "medium, 150 px above", 150, 25, 4));
        check("settings flick: a hard flick 100 px above the last row, let go past it, settles on it with no return past a view's band",
              settings_flick_check((string_address) "hard, 100 px above", 100, 40, 6));
        settings_size(SETTINGS_WIDTH, 300);
        settings_fit_check();
        settings_column_check();
        settings_column_fit_check();
        settings_group_glide_check();
        settings_end_check();
        settings_size(SETTINGS_WIDTH, SETTINGS_HEIGHT);
        settings_least_check();
        check_settings_parts();
        settings_frames_gallery();
#ifdef PAGE_DUMP
        page_dump();
#endif
        string_format(log, "  settings frames: %p frames drawn and compared, at scales 1 and 2\n", settings_frames.frames);

        settings.ui = saved.ui;
        settings.own = saved.own;
        settings.width = saved.width, settings.height = saved.height, settings.page.buffers.shown = saved.shown, settings.page.chosen = saved.group;
        settings.list.pool = saved.pool;
        settings_layout();
        compositor_call(syscall(munmap), (positive)table, WINDOWS_HEADER + 2 * buffer, 0, 0, 0, 0);
        compositor_call(syscall(munmap), (positive)engine, sizeof(font_engine), 0, 0, 0, 0);
}

// A fresh directory for a fake power supply tree, under /tmp.
static b32 battery_fresh(p8 address_to root)
{
        string_copy_bounded(root, (string_address)"/tmp/battery-XXXXXX", 64);
        return !is_null(mkdtemp((char *)root));
}

static fn fresh_failed(void)
{
        check("battery: a temporary tree can be made", false);
}

// The fake tree's own directories and files, kept so that they are taken away again: the term lane builds without sensors.
static p8 battery_dirs[8][96];
static p8 battery_files[32][160];
static positive battery_dir_count, battery_file_count;

static fn battery_put(string_address root, string_address name, string_address leaf, string_address text)
{
        p8 directory[96], path[160];
        bipolar handle;
        b32 known = false;

        string_join(directory, sizeof(directory), root, "/", name);
        for (positive at = 0; at < battery_dir_count; at++)
                known |= string_equals((string_address)battery_dirs[at], (string_address)directory);
        if (!known && battery_dir_count < 8)
        {
                string_copy_bounded((string_address)battery_dirs[battery_dir_count++], (string_address)directory, sizeof(battery_dirs[0]));
                system_make_directory_at(AT_FDCWD, directory, 0755);
        }
        string_join(path, sizeof(path), directory, leaf);
        //      A file is made with its mode: openat without one takes whatever the register holds.
        handle = system_open_at_mode(AT_FDCWD, path, FILE_CREATE | FILE_WRITE | O_CLOEXEC, 0644);
        if (handle >= 0)
        {
                system_call_3(syscall(write), (positive)handle, (positive)text, string_length(text));
                system_close((positive)handle);
        }
        if (battery_file_count < 32)
                string_copy_bounded((string_address)battery_files[battery_file_count++], (string_address)path, sizeof(battery_files[0]));
}

static fn battery_clear(string_address root)
{
        for (positive at = 0; at < battery_file_count; at++)
                system_remove_at(AT_FDCWD, (string_address)battery_files[at], 0);
        for (positive at = 0; at < battery_dir_count; at++)
                system_remove_at(AT_FDCWD, (string_address)battery_dirs[at], AT_REMOVEDIR);
        battery_file_count = 0;
        battery_dir_count = 0;
        system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
}

static b32 battery_listed(struct battery_name address_to names, positive count, string_address name)
{
        for (positive at = 0; at < count && at < BATTERY_LIST; at++)
                if (string_equals((string_address)names[at].text, name))
                        return true;
        return false;
}

static fn check_battery(void)
{
        p8 root[64];
        struct battery_state state;
        struct battery_name names[BATTERY_LIST];
        positive count;
        b32 found;
        char value[SETTINGS_VALUE];
        int battery_row = settings_row_named("Battery");

        //      Two batteries, both reading capacity: each by its name, and the default one present.
        if (!battery_fresh(root))
                {
                        fresh_failed();
                        return;
                }
        battery_put(root, "BAT0", "/type", "Battery\n");
        battery_put(root, "BAT0", "/present", "1\n");
        battery_put(root, "BAT0", "/capacity", "73\n");
        battery_put(root, "BAT0", "/status", "Discharging\n");
        battery_put(root, "BAT1", "/type", "Battery\n");
        battery_put(root, "BAT1", "/present", "1\n");
        battery_put(root, "BAT1", "/capacity", "40\n");
        battery_put(root, "BAT1", "/status", "Charging\n");
        battery_root = (string_address)root;
        count = battery_list(names, BATTERY_LIST);
        check("battery: BAT0 and BAT1 are both listed", count == 2 && battery_listed(names, count, "BAT0") && battery_listed(names, count, "BAT1"));
        found = battery_read("BAT1", &state);
        check("battery: BAT1 is read by its name, 40 percent and charging",
              found && state.present && state.percent == 40 && state.charging == BATTERY_CHARGING && string_equals((string_address)state.name, (string_address)"BAT1"));
        found = battery_read("BAT0", &state);
        check("battery: BAT0 is read by its name, 73 percent and discharging",
              found && state.present && state.percent == 73 && state.charging == BATTERY_DISCHARGING);
        found = battery_read(null, &state);
        check("battery: with no name the default is a present battery",
              found && state.present && (string_equals((string_address)state.name, (string_address)"BAT0") ||
                                         string_equals((string_address)state.name, (string_address)"BAT1")));
        battery_clear(root);

        //      No capacity file: the percent is charge over full charge, or energy over full energy, and nothing else.
        if (!battery_fresh(root))
                {
                        fresh_failed();
                        return;
                }
        battery_put(root, "BAT0", "/type", "Battery\n");
        battery_put(root, "BAT0", "/present", "1\n");
        battery_put(root, "BAT0", "/charge_now", "300000\n");
        battery_put(root, "BAT0", "/charge_full", "1000000\n");
        battery_put(root, "BAT0", "/status", "Not charging\n");
        battery_put(root, "BAT1", "/type", "Battery\n");
        battery_put(root, "BAT1", "/present", "1\n");
        battery_put(root, "BAT1", "/energy_now", "1\n");
        battery_put(root, "BAT1", "/energy_full", "3\n");
        battery_root = (string_address)root;
        found = battery_read("BAT0", &state);
        check("battery: with no capacity, charge_now over charge_full is the percent (30), and Not charging is its state",
              found && state.present && state.percent == 30 && state.charging == BATTERY_NOT_CHARGING);
        found = battery_read("BAT1", &state);
        check("battery: with no capacity or charge, energy over full energy is the percent (33), and no status is unknown",
              found && state.present && state.percent == 33 && state.charging == BATTERY_UNKNOWN);
        battery_clear(root);

        //      The settings row takes the same reading, as its words.
        if (!battery_fresh(root))
                {
                        fresh_failed();
                        return;
                }
        battery_put(root, "BAT0", "/type", "Battery\n");
        battery_put(root, "BAT0", "/present", "1\n");
        battery_put(root, "BAT0", "/charge_now", "300000\n");
        battery_put(root, "BAT0", "/charge_full", "1000000\n");
        battery_put(root, "BAT0", "/status", "Not charging\n");
        battery_root = (string_address)root;
        check("settings: the Battery row reads a battery with no capacity file, as 30 percent and Not charging",
              settings_read(&settings_rows[battery_row], "", 0, value, SETTINGS_VALUE, false) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address)"30%, Not charging"));
        check("settings: a chosen battery of none is absent",
              settings_battery((string_address)"none", value, SETTINGS_VALUE) == SETTINGS_ABSENT);
        check("settings: a chosen battery that is gone falls back to the first present one",
              settings_battery((string_address)"BATX", value, SETTINGS_VALUE) == SETTINGS_READ &&
                  string_equals((string_address)value, (string_address)"30%, Not charging"));
        check("battery: none is not a battery", !battery_read((string_address)"none", &state) && !state.present);
        {
                int picker = settings_row_named("Battery shown");
                char pick[SETTINGS_VALUE], word[SETTINGS_VALUE];
                unsigned int state = settings_parsed((unsigned int)picker, "[Moonwater] battery BAT1\n", pick);

                check("settings: the Battery picker reads the battery the verb keeps", state == SETTINGS_READ && string_equals((string_address)pick, (string_address)"BAT1"));
                state = settings_parsed((unsigned int)picker, "[Moonwater] battery auto\n", pick);
                check("settings: and auto when nothing is kept", state == SETTINGS_READ && string_equals((string_address)pick, (string_address)"auto"));
                settings.battery_menu_count = 1;
                string_copy_bounded((string_address)settings.battery_menu[0].text, (string_address)"BAT0", sizeof(settings.battery_menu[0].text));
                check("settings: the picker's choices are auto, each battery listed, then none",
                      settings_options(&settings_rows[picker]) == 3 && settings_choice(&settings_rows[picker], 0, word, SETTINGS_VALUE) &&
                          string_equals((string_address)word, (string_address)"auto") &&
                          settings_choice(&settings_rows[picker], 1, word, SETTINGS_VALUE) &&
                          string_equals((string_address)word, (string_address)"BAT0") &&
                          settings_choice(&settings_rows[picker], 2, word, SETTINGS_VALUE) &&
                          string_equals((string_address)word, (string_address)"none") &&
                          !settings_choice(&settings_rows[picker], 3, word, SETTINGS_VALUE));
                settings.battery_menu_count = 0;
        }
        battery_clear(root);

        //      A machine with no battery: an empty tree, and one with only a mains supply.
        if (!battery_fresh(root))
                {
                        fresh_failed();
                        return;
                }
        battery_root = (string_address)root;
        check("battery: an empty directory lists no batteries", battery_list(names, BATTERY_LIST) == 0);
        found = battery_read(null, &state);
        check("battery: with none, the default is not present and reads no percent",
              !found && !state.present && state.percent == -1 && state.charging == BATTERY_UNKNOWN);
        check("settings: the Battery row is absent on a machine with no battery",
              settings_read(&settings_rows[battery_row], "", 0, value, SETTINGS_VALUE, false) == SETTINGS_ABSENT);
        battery_put(root, "AC", "/type", "Mains\n");
        battery_put(root, "AC", "/online", "1\n");
        check("battery: a mains supply is not a battery", battery_list(names, BATTERY_LIST) == 0 && !battery_read((string_address)"AC", &state));
        battery_clear(root);

        //      A Deck-style tree: the mains and USB entries, a mouse's battery (scope Device), a UPS, a battery not present, and
        //      the one present, whichever order the directory gives them in.
        if (!battery_fresh(root))
        {
                fresh_failed();
                return;
        }
        battery_put(root, "AC", "/type", "Mains\n");
        battery_put(root, "AC", "/online", "1\n");
        battery_put(root, "ucsi-source", "/type", "USB\n");
        battery_put(root, "hid-mouse", "/type", "Battery\n");
        battery_put(root, "hid-mouse", "/scope", "Device\n");
        battery_put(root, "hid-mouse", "/present", "1\n");
        battery_put(root, "hid-mouse", "/capacity", "55\n");
        battery_put(root, "ups0", "/type", "UPS\n");
        battery_put(root, "ups0", "/present", "1\n");
        battery_put(root, "ups0", "/capacity", "90\n");
        battery_put(root, "ups0", "/status", "Discharging\n");
        battery_put(root, "BATX", "/type", "Battery\n");
        battery_put(root, "BATX", "/present", "0\n");
        battery_put(root, "BAT1", "/type", "Battery\n");
        battery_put(root, "BAT1", "/present", "1\n");
        battery_put(root, "BAT1", "/capacity", "80\n");
        battery_put(root, "BAT1", "/status", "Discharging\n");
        battery_root = (string_address)root;
        count = battery_list(names, BATTERY_LIST);
        check("battery: the Device-scope mouse battery, the UPS, BATX and BAT1 are all listed (four, each once)",
              count == 4 && battery_listed(names, count, "hid-mouse") && battery_listed(names, count, "ups0") &&
                  battery_listed(names, count, "BATX") && battery_listed(names, count, "BAT1") && !battery_listed(names, count, "AC"));
        check("battery: the UPS is listed as a UPS and the batteries as batteries",
              names[0].kind != BATTERY_KIND_NONE && battery_word_value("ups0", "/type", 16, battery_kind_words) == BATTERY_KIND_UPS &&
                  battery_word_value("BAT1", "/type", 16, battery_kind_words) == BATTERY_KIND_BATTERY &&
                  battery_word_value("hid-mouse", "/type", 16, battery_kind_words) == BATTERY_KIND_BATTERY &&
                  battery_word_value("AC", "/type", 16, battery_kind_words) == BATTERY_KIND_NONE);
        found = battery_read(null, &state);
        check("battery: the default is a present battery of type Battery, the mouse's or BAT1, and never BATX or the UPS",
              found && state.present &&
                  (string_equals((string_address)state.name, (string_address)"BAT1") || string_equals((string_address)state.name, (string_address)"hid-mouse")));
        found = battery_read("BAT1", &state);
        check("battery: BAT1 by its name is 80 percent and discharging", found && state.present && state.percent == 80 && state.charging == BATTERY_DISCHARGING);
        found = battery_read("hid-mouse", &state);
        check("battery: a Device-scope mouse battery is read by its name, 55 percent", found && state.present && state.percent == 55);
        found = battery_read("ups0", &state);
        check("battery: a UPS is read by its name, with capacity as its percent (90)", found && state.present && state.percent == 90 &&
                  state.charging == BATTERY_DISCHARGING);
        found = battery_read("BATX", &state);
        check("battery: BATX is a battery that is not present, with no percent", found && !state.present && state.percent == -1);
        check("battery: a name with a slash does not leave the directory", !battery_read("../AC", &state));
        check("battery: a mains supply is not read as a battery", !battery_read("AC", &state));
        battery_clear(root);

        battery_root = (string_address)BATTERY_ROOT;
        return;
}
/*
        A window that reads from the right is the window that reads from the left, mirrored. Every view the kit draws is
        drawn twice at the same size and the same time, once with ui_kit.rtl clear and once set, and the second picture
        must be the first one flipped about its vertical middle, pixel for pixel, with the places named in rtl_keep
        left out (a check mark, a clock's hands and a play triangle are not mirrored: their box is, their picture is
        not). The text is what a mirror cannot judge, a glyph is not its own mirror image: so the faces are made here, one
        block a code point, as wide as its advance and as tall as a capital, the same width for every code point. A line
        of text is then a bar, and a bar laid at the start of a box on the right is the mirror of a bar laid at the start
        of the same box on the left, when the width of the text is what the layout believes it is. The font engine's own
        bidirectional text, shaping and fallback are held by CHECK_font_shape; this holds the kit's layout to its sides.
*/
static p8 rtl_font_bytes[1 << 15];
static positive rtl_font_size;

static fn rtl_u16(positive address_to at, p32 value)
{
        rtl_font_bytes[(*at)++] = (p8)(value >> 8);
        rtl_font_bytes[(*at)++] = (p8)value;
}

static fn rtl_u32(positive address_to at, p32 value)
{
        rtl_u16(at, value >> 16);
        rtl_u16(at, value & 0xffff);
}

// The font: a block of 1000 by 700 units, in a 1000 unit em, for every code point from U+0020 to U+2FFF (and the missing glyph).
static fn rtl_font_make(void)
{
        static const p32 tags[9] = {FONT_TAG('O', 'S', '/', '2'), FONT_TAG('c', 'm', 'a', 'p'), FONT_TAG('g', 'l', 'y', 'f'),
                                    FONT_TAG('h', 'e', 'a', 'd'), FONT_TAG('h', 'h', 'e', 'a'), FONT_TAG('h', 'm', 't', 'x'),
                                    FONT_TAG('l', 'o', 'c', 'a'), FONT_TAG('m', 'a', 'x', 'p'), FONT_TAG('p', 'o', 's', 't')};
        positive at = 12 + 16 * 9, start[9], length[9];

        memory_zero(rtl_font_bytes, sizeof(rtl_font_bytes));
        rtl_font_bytes[1] = 1;
        rtl_font_bytes[5] = 9;
        for (positive table = 0; table < 9; table++)
        {
                start[table] = at;
                switch (table)
                {
                case 0: // OS/2, version 4, typo metrics used
                        rtl_u16(&at, 4);
                        at = start[table] + 62;
                        rtl_u16(&at, 0x80), rtl_u16(&at, 0x20), rtl_u16(&at, 0x2fff);
                        rtl_u16(&at, 800), rtl_u16(&at, (p32)-200 & 0xffff), rtl_u16(&at, 0);
                        rtl_u16(&at, 800), rtl_u16(&at, 200);
                        rtl_u32(&at, 1), rtl_u32(&at, 0);
                        rtl_u16(&at, 500), rtl_u16(&at, 700);
                        at = start[table] + 96;
                        break;
                case 1: // cmap: format 4, (3, 1); U+0020 to U+2FFF through the glyph array, all of them glyph 1
                        rtl_u16(&at, 0), rtl_u16(&at, 1), rtl_u16(&at, 3), rtl_u16(&at, 1), rtl_u32(&at, 12);
                        rtl_u16(&at, 4), rtl_u16(&at, 32 + 2 * 0x2fe0), rtl_u16(&at, 0);
                        rtl_u16(&at, 4), rtl_u16(&at, 4), rtl_u16(&at, 1), rtl_u16(&at, 0);
                        rtl_u16(&at, 0x2fff), rtl_u16(&at, 0xffff), rtl_u16(&at, 0);
                        rtl_u16(&at, 0x20), rtl_u16(&at, 0xffff);
                        rtl_u16(&at, 0), rtl_u16(&at, 1);
                        rtl_u16(&at, 4), rtl_u16(&at, 0);
                        for (p32 code = 0x20; code <= 0x2fff; code++)
                                rtl_u16(&at, 1);
                        break;
                case 2: // glyf: the block, twice
                        for (positive glyph = 0; glyph < 2; glyph++)
                        {
                                rtl_u16(&at, 1), rtl_u16(&at, 0), rtl_u16(&at, 0), rtl_u16(&at, 1000), rtl_u16(&at, 700);
                                rtl_u16(&at, 3), rtl_u16(&at, 0);
                                for (positive point = 0; point < 4; point++)
                                        rtl_font_bytes[at++] = 1;
                                rtl_u16(&at, 0), rtl_u16(&at, 0), rtl_u16(&at, 1000), rtl_u16(&at, 0);
                                rtl_u16(&at, 0), rtl_u16(&at, 700), rtl_u16(&at, 0), rtl_u16(&at, (p32)-700 & 0xffff);
                                at = (at + 3) & ~(positive)3;
                        }
                        break;
                case 3: // head: 1000 units, a long loca
                        rtl_u32(&at, 0x00010000), rtl_u32(&at, 0x00010000), rtl_u32(&at, 0), rtl_u32(&at, 0x5f0f3cf5);
                        rtl_u16(&at, 0), rtl_u16(&at, 1000);
                        at = start[table] + 50;
                        rtl_u16(&at, 1), rtl_u16(&at, 0);
                        break;
                case 4: // hhea
                        rtl_u32(&at, 0x00010000), rtl_u16(&at, 800), rtl_u16(&at, (p32)-200 & 0xffff), rtl_u16(&at, 0);
                        rtl_u16(&at, 1000);
                        at = start[table] + 34;
                        rtl_u16(&at, 2);
                        break;
                case 5: // hmtx
                        rtl_u16(&at, 1000), rtl_u16(&at, 0), rtl_u16(&at, 1000), rtl_u16(&at, 0);
                        break;
                case 6: // loca
                        rtl_u32(&at, 0), rtl_u32(&at, 36), rtl_u32(&at, 72);
                        break;
                case 7: // maxp
                        rtl_u32(&at, 0x00005000), rtl_u16(&at, 2);
                        break;
                default: // post
                        rtl_u32(&at, 0x00030000), rtl_u32(&at, 0);
                        rtl_u16(&at, (p32)-100 & 0xffff), rtl_u16(&at, 50), rtl_u32(&at, 0);
                        at = start[table] + 32;
                        break;
                }
                length[table] = at - start[table];
                at = (at + 3) & ~(positive)3;
        }
        for (positive table = 0; table < 9; table++)
        {
                positive record = 12 + 16 * table;

                rtl_u32(&record, tags[table]);
                rtl_u32(&record, 0);
                rtl_u32(&record, (p32)start[table]);
                rtl_u32(&record, (p32)length[table]);
        }
        rtl_font_size = at;
}

// The places a mirror does not turn over: a picture whose box moves and whose drawing does not (LTR coordinates of the first picture).
struct rtl_keep
{
        int left, top, right, bottom;
};

static struct rtl_keep rtl_keeps[64];
static unsigned int rtl_first_pair[2];
static int rtl_keep_count;

static void rtl_keep_add(int x, int y, int w, int h)
{
        if (rtl_keep_count < (int)(sizeof(rtl_keeps) / sizeof(rtl_keeps[0])))
                rtl_keeps[rtl_keep_count++] = (struct rtl_keep){x, y, x + w, y + h};
}

static inline b32 rtl_kept(int x, int y)
{
        for (int k = 0; k < rtl_keep_count; k++)
                if (x >= rtl_keeps[k].left && x < rtl_keeps[k].right && y >= rtl_keeps[k].top && y < rtl_keeps[k].bottom)
                        return true;
        return false;
}

// Two pixels of one cover, give or take a level in a channel: the painter's rounding of a round corner's cover is not the same at a box's
// two ends (one level, on one row of a pill of the 56 pixels of a switch at scale 2); a box a pixel off its place is far more.
static inline b32 rtl_close(unsigned int a, unsigned int b)
{
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                int one = (int)(a >> shift & 255), other = (int)(b >> shift & 255);

                if (one - other > 1 || other - one > 1)
                        return false;
        }
        return true;
}

// How many pixels of the second picture are not the first one flipped, and the first of them (set in the result's pointers).
static positive rtl_compare(const unsigned int address_to left, const unsigned int address_to right, positive stride, int width, int height,
                            int address_to first_x, int address_to first_y)
{
        positive wrong = 0;

        for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                        if (!rtl_close(left[(positive)y * stride + (positive)x], right[(positive)y * stride + (positive)(width - 1 - x)]) && !rtl_kept(width - 1 - x, y))
                        {
                                unsigned int a = left[(positive)y * stride + (positive)x], b = right[(positive)y * stride + (positive)(width - 1 - x)];

                                if (!wrong)
                                        *first_x = x, *first_y = y, rtl_first_pair[0] = a, rtl_first_pair[1] = b;
                                wrong++;
                        }
        return wrong;
}

static unsigned int rtl_first[SETTINGS_FRAMES_W * SETTINGS_FRAMES_H];
static int rtl_width, rtl_height; // the size of the picture a view made
static int rtl_pressed[400][400];  // what a press at each place was on, left to right
static positive rtl_pressed_wrong;
static struct settings_kept rtl_settings_kept;
static p8 rtl_kit_kept[sizeof(ui_kit)];

// The window in group (a number, or -1 for the kit's gallery) drawn whole at the kit's time, right to left or not.
static fn rtl_draw(int group, int scale, b32 rtl)
{
        ui_kit.rtl = rtl ? 1 : 0;
        ui_kit_settle();
        settings.ui.scale = scale * 100;
        gallery.on = group == -1;
        if (group == -2)
        {
                // The page of a command's answer, the card: its lines from the right, its bar at the left.
                static const char said[] = "Canvas is on: the kernel desktop, 2 windows\n  HDMI-1: 1920 by 1080, 60 Hz\nthe third line of it";

                settings_size((unsigned int)(SETTINGS_WIDTH * scale), (unsigned int)(SETTINGS_HEIGHT * scale));
                memory_copy(settings_large->said, said, sizeof(said) - 1);
                settings.runner.length = sizeof(said) - 1;
                settings_card_fill();
                settings_card_open("moonwater status");
        }
        else if (group == -3)
        {
                // A menu: a check, a radio, a submenu, a header, accelerators, a separator, a choice that is off.
                static struct ui_popmenu menu;
                struct ui menu_ui = settings.ui;

                memory_zero(&menu, sizeof(menu));
                ui_popmenu_add(&menu, "Section", null, UI_MENU_HEADER, 0);
                ui_popmenu_add(&menu, "Copy", "Ctrl+C", 0, 1);
                ui_popmenu_add(&menu, "Wrap", null, UI_MENU_CHECK | UI_MENU_ON, 2);
                ui_popmenu_add(&menu, "", null, UI_MENU_SEPARATOR, 0);
                ui_popmenu_add(&menu, "Dark", null, UI_MENU_RADIO | UI_MENU_ON, 3);
                ui_popmenu_add(&menu, "Light", null, UI_MENU_RADIO, 4);
                ui_popmenu_add(&menu, "More", null, UI_MENU_SUBMENU, 5);
                ui_popmenu_add(&menu, "Undo", null, UI_MENU_DISABLED, 6);
                menu.hot = 1;
                ui_popmenu_size(&menu_ui, &menu, 0);
                ui_begin(&menu_ui, settings_frames_whole, SETTINGS_FRAMES_W, menu.width, menu.height);
                ui_popmenu_draw(&menu_ui, &menu);
                rtl_width = menu.width, rtl_height = menu.height;
                return;
        }
        else if (group == -4)
        {
                // The launcher, glass, with nothing typed: its field's words and its list, and the buttons under it.
                static char names[6][LAUNCHER_NAME];
                static unsigned char from[6];
                static unsigned short hashed[LAUNCHER_HASH];
                static struct windows *table_scratch;
                struct windows *const table_at = windows_scratch_table(&table_scratch);
                static const char address_to const fixture[] = {"ls", "lsblk", "lsof", "less", "false", "also"};

                for (positive at = 0; at < 6; at++)
                        string_copy((string_address)names[at], (string_address)fixture[at]);
                memory_zero(&launcher, sizeof(launcher));
                windows_scratch_zero(table_at);
                launcher.names = (char address_to)names, launcher.count = 6, launcher.from = from, launcher.hashed = hashed;
                launcher.dirs[0] = "/usr/bin", launcher.dir_count = 1;
                table_at->output_width = 1920, table_at->output_height = 1080, table_at->stride = SETTINGS_FRAMES_W;
                launcher.own.table = table_at;
                launcher.ui = settings.ui;
                launcher.ui.opaque = 0, launcher.ui.glass = 1;
                ui_inks(&launcher.ui, 0x2b2b2eu);
                launcher.width = LAUNCHER_WIDTH;
                launcher_layout();
                ui_kit.now = 5000000000ull;
                launcher_draw(settings_frames_whole);
                rtl_width = launcher.width, rtl_height = launcher.height;
                return;
        }
        if (group == -1)
        {
                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
                settings.width = ui_px(&settings.ui, GALLERY_W);
                settings.height = ui_px(&settings.ui, GALLERY_H);
        }
        else if (group >= 0)
        {
                settings_size((unsigned int)(SETTINGS_WIDTH * scale), (unsigned int)(SETTINGS_HEIGHT * scale));
                settings.page.chosen = group;
                settings_filter_clear(), (void)0;
                settings.page.card = 0, ui_field_close(&settings.edit), (void)0, settings.page.menu_row = -1;
                settings.focused = 0;
                settings_refilter();
        }
        ui_kit.now = 5000000000ull;
        ui_kit.busy = 0;
        settings_draw_rows(settings_frames_whole, 0, 0);
        rtl_width = settings.width, rtl_height = settings.height;
}

// A picture as a PPM in the work directory: turned over when asked, and the pixels that are not the other picture's mirror red.
static fn rtl_ppm(const p8 address_to base, string_address tail, const unsigned int address_to pixels, const unsigned int address_to other, b32 turned)
{
        p8 path[128], head[32];
        static p8 row[3 * SETTINGS_FRAMES_W];
        positive length = 0;
        bipolar handle;

        string_copy_bounded(path, (string_address)base, sizeof(path));
        string_append_bounded((string_address)path, tail, sizeof(path));
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle < 0)
                return;
        string_copy(head, (string_address) "P6\n");
        length = 3;
        length += positive_into_string(head + length, (positive)rtl_width);
        head[length++] = ' ';
        length += positive_into_string(head + length, (positive)rtl_height);
        string_copy(head + length, (string_address) "\n255\n");
        length += 5;
        system_write_once((b32)handle, head, length);
        for (int y = 0; y < rtl_height; y++)
        {
                for (int x = 0; x < rtl_width; x++)
                {
                        unsigned int pixel = pixels[(positive)y * SETTINGS_FRAMES_W + (positive)(turned ? rtl_width - 1 - x : x)];

                        if (other && !rtl_close(other[(positive)y * SETTINGS_FRAMES_W + (positive)(rtl_width - 1 - x)], pixel) && !rtl_kept(x, y))
                                pixel = 0xffff0000u;
                        row[x * 3] = (p8)(pixel >> 16), row[x * 3 + 1] = (p8)(pixel >> 8), row[x * 3 + 2] = (p8)pixel;
                }
                system_write_once((b32)handle, row, (positive)rtl_width * 3);
        }
        system_close(handle);
}

// One view at one scale: its picture right to left against the one left to right, mirrored.
static b32 rtl_view(string_address name, int group, int scale)
{
        int x = 0, y = 0;
        positive wrong;

        rtl_draw(group, scale, false);
        memory_copy(rtl_first, settings_frames_whole, sizeof(rtl_first));
        // What a press at each place of the Settings window is on (a row, a group of the column), every 5 pixels across and down, left to right.
        if (group >= 0)
                for (int gy = 0; gy < rtl_height / 5 && gy < 400; gy++)
                        for (int gx = 0; gx < rtl_width / 5 && gx < 400; gx++)
                                rtl_pressed[gy][gx] = ui_page_target(&settings.ui, &settings.page, &settings.list, gx * 5, gy * 5);
        rtl_keep_count = 0;
        rtl_draw(group, scale, true);
        wrong = rtl_compare(rtl_first, settings_frames_whole, SETTINGS_FRAMES_W, rtl_width, rtl_height, &x, &y);
        if (group >= 0)
                for (int gy = 0; gy < rtl_height / 5 && gy < 400; gy++)
                        for (int gx = 0; gx < rtl_width / 5 && gx < 400; gx++)
                                if (ui_page_target(&settings.ui, &settings.page, &settings.list, rtl_width - 1 - gx * 5, gy * 5) != rtl_pressed[gy][gx])
                                {
                                        if (!rtl_pressed_wrong)
                                                string_format(log, "  rtl mirror: a press at %b,%b of %s at scale %b is on %b, at the mirrored place on %b\n", (bipolar)(gx * 5),
                                                              (bipolar)(gy * 5), name, (bipolar)scale, (bipolar)rtl_pressed[gy][gx],
                                                              (bipolar)ui_page_target(&settings.ui, &settings.page, &settings.list, rtl_width - 1 - gx * 5, gy * 5));
                                        rtl_pressed_wrong++;
                                }
        if (wrong)
        {
                p8 path[96];
                positive named = 0;

                string_format(log, "  rtl mirror: %s at scale %b: %p pixels are not the mirror of the picture left to right, the first at %b,%b (%p, %p)\n",
                              name, (bipolar)scale, wrong, (bipolar)x, (bipolar)y, (positive)rtl_first_pair[0], (positive)rtl_first_pair[1]);
                // The picture right to left with the places that are not the mirror in red, and the left to right one turned over, for whoever looks
                // (written to the work directory when there is one).
                for (const char address_to prefix = "scratch/rtl-"; *prefix; prefix++)
                        path[named++] = (p8)*prefix;
                for (positive k = 0; name[k] && named < 80; k++)
                        path[named++] = (p8)(name[k] == ' ' || name[k] == '\'' || name[k] == '/' ? '-' : name[k]);
                path[named] = 0;
                string_append_bounded((string_address)path, (string_address)(scale == 2 ? "-2" : "-1"), sizeof(path));
                rtl_ppm(path, (string_address) "-rtl.ppm", settings_frames_whole, rtl_first, false);
                rtl_ppm(path, (string_address) "-ltr.ppm", rtl_first, null, true);
        }
        return !wrong;
}

/*
        The text's size (canvas text scale, ui_text_percent). The block face makes a run of n letters exactly n ems wide and a
        capital 0.7 em tall, so the size a run is drawn and measured at is read off its width and its capital. Held: the
        size at 100 percent (and with none taken) is what it was before there was a setting, bit for bit, at every scale; at any
        other it is the size times the percent, in a run's width and a capital's height; a list's row is 40 up to 100 percent
        and grows with the text above it; the table's number is taken in its range and nothing outside it; the two views are
        still each other's mirror at 150 and at 300 percent (the layout believes the width the pen makes, or the mirror fails)
        and a press is on the same thing at the mirrored place.
*/
static fn check_text_scale(void)
{
        static const unsigned int percents[] = {50, 100, 150, 200, 300};
        static const int scales[] = {100, 150, 200, 300};
        static struct windows *taken_scratch;
        struct windows *const taken_at = windows_scratch_table(&taken_scratch);
        b32 same = true, wide = true, tall = true, rows = true, range = true, mirrored = true, pressed = true, published = false;
        b32 placed = false, lines = true, field = true, cut = false;
        unsigned int kept_percent = ui_text_percent;
        int kept_scale = settings.ui.scale;

        for (int scale = 0; scale < 4; scale++)
        {
                settings.ui.scale = scales[scale];
                for (int size = 8; size <= 40; size++)
                {
                        ui_text_percent = 0;
                        same &= ui_em(&settings.ui, size) == ((p32)(size * settings.ui.scale) << 6) / 100;
                        ui_text_percent = 100;
                        same &= ui_em(&settings.ui, size) == ((p32)(size * settings.ui.scale) << 6) / 100;
                }
                for (int at = 0; at < 5; at++)
                {
                        // The kit's body text is 14, the label 12, the title 22: four letters of each.
                        static const int roles[] = {UI_BODY_MEDIUM, UI_LABEL_MEDIUM, UI_TITLE_LARGE};
                        ui_text_percent = percents[at];
                        for (int role = 0; role < 3; role++)
                        {
                                int size = ui_types[roles[role]].size;
                                int em = size * settings.ui.scale * (int)percents[at] / 10000;
                                int measured = ui_kit_measure(&settings.ui, (string_address) "XXXX", roles[role]);
                                int capital = ui_capital(&settings.ui, size);

                                wide &= measured >= 4 * em - 4 && measured <= 4 * em + 4;
                                tall &= capital >= em * 7 / 10 - 1 && capital <= em * 7 / 10 + 2;
                        }
                }
        }
        settings.ui.scale = kept_scale;
        for (int at = 0; at < 5; at++)
        {
                ui_text_percent = percents[at];
                rows &= ui_row_height() == (percents[at] > 100 ? 40 * (int)percents[at] / 100 : 40);
        }
        ui_text_percent = 0;
        rows &= ui_row_height() == 40;
        for (unsigned int value = 0; value < 400; value++)
        {
                taken_at->text_scale = value;
                ui_kit_text_take(taken_at);
                range &= ui_text_percent == (value >= 50 && value <= 300 ? value : 0);
        }
        // A size taken that is another places every control again, once; the same size again does not.
        {
                b64 epoch;

                taken_at->text_scale = 0;
                ui_kit_text_take(taken_at);
                epoch = ui_kit.epoch;
                taken_at->text_scale = 150;
                ui_kit_text_take(taken_at);
                placed = ui_kit.epoch == epoch + 1;
                ui_kit_text_take(taken_at);
                placed = placed && ui_kit.epoch == epoch + 1;
                taken_at->text_scale = 0;
                ui_kit_text_take(taken_at);
                placed = placed && ui_kit.epoch == epoch + 2;
        }
        // A line box's baseline is the face's own at the size the text is drawn at, and a text field's em is the size and its zoom at it.
        for (int at = 0; at < 3; at++)
        {
                static const int roles[] = {UI_BODY_MEDIUM, UI_LABEL_MEDIUM, UI_TITLE_LARGE};
                static struct ui_text probe;

                for (int size = 0; size < 3; size++)
                {
                        const struct ui_type address_to t = &ui_types[roles[at]];
                        font_metrics m;

                        ui_text_percent = percents[size * 2];
                        m = font_metrics_at(settings.ui.regular, ui_em(&settings.ui, t->size));
                        lines &= ui_kit_baseline(&settings.ui, 10, roles[at]) == 10 + (t->line * settings.ui.scale / 100 - (int)m.cell_height) / 2 + (int)m.baseline;
                        probe.size = t->size, probe.zoom = at;
                        field &= ui_text_em(&settings.ui, &probe) == ui_em(&settings.ui, t->size + at);
                }
        }
        // The same words are cut again for another size: whole at 100 percent in a room a tenth wider than they are, and cut at 300.
        {
                char fitted[64];
                string_address text = (string_address) "XXXXXXXXXX";
                string_address one, other;
                int room;

                ui_text_percent = 100;
                room = 11 * ui_kit_measure(&settings.ui, text, UI_BODY_MEDIUM) / 10;
                one = ui_fit(&settings.ui, text, UI_BODY_MEDIUM, room, fitted, sizeof(fitted));
                ui_text_percent = 300;
                other = ui_fit(&settings.ui, text, UI_BODY_MEDIUM, room, fitted, sizeof(fitted));
                cut = one == text && other == (string_address)fitted && string_length(other) < 10 + 3;
                ui_text_percent = 100;
                cut = cut && ui_fit(&settings.ui, text, UI_BODY_MEDIUM, room, fitted, sizeof(fitted)) == text;
        }
        ui_text_percent = 0;
        check("text size: at 100 percent, and with none taken, a run is drawn at the size and scale alone, as it was before there was a setting, at four scales and every size from 8 to 40",
              same);
        check("text size: a run of four letters is four ems wide and a capital 0.7 em tall at the size times the percent, at 50, 100, 150, 200 and 300 percent and four scales, for the body, the label and the title",
              wide && tall);
        check("text size: a list's row is 40 logical pixels up to 100 percent and grows with the text above it", rows);
        check("text size: the table's number is taken from 50 to 300 and 0 (100) otherwise", range);
        check("text size: a size that is another places the controls again, once, and the same size does not", placed);
        check("text size: a line box's baseline is the face's at the size the text is drawn at, and a text field's em is its size and zoom at it", lines && field);
        check("text size: a label is cut again for another size, and not given the cut of the one before", cut);
        // The compositor's side: a change of the size counts the table's theme up once, the same size not at all, and 100 is 0.
        {
                struct windows address_to saved = compositor.table;
                unsigned int before;

                windows_scratch_zero(taken_at);
                compositor.table = taken_at;
                before = taken_at->themed;
                compositor_text_publish(150);
                compositor_text_publish(150);
                published = taken_at->text_scale == 150 && taken_at->themed == before + 1;
                compositor_text_publish(0);
                published = published && taken_at->text_scale == 0 && taken_at->themed == before + 2;
                compositor.table = saved;
        }
        check("text size: the compositor publishes the size into the table and knocks the windows once for a change", published);
        // The pages, mirrored, at 150 and at 300 percent: a width the layout believes is not the one the pen makes is a mirror that fails.
        // (Not the kit's gallery: it is a showcase of controls at places of its own, which text wider than they were made for runs over.)
        for (int size = 0; size < 2; size++)
        {
                ui_text_percent = size ? 300 : 150;
                ui_kit_settle();
                if (ui_cuts)
                        memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);
                rtl_pressed_wrong = 0;
                string_format(log, "  text size %b percent: the pages, mirrored\n", (bipolar)ui_text_percent);
                mirrored &= rtl_view("a card", -2, 1) && rtl_view("a menu", -3, 1) && rtl_view("the launcher", -4, 1);
                for (int scale = 1; scale <= (size ? 1 : 2); scale++)
                        for (int group = 0; group < settings_groups(); group++)
                                mirrored &= rtl_view(settings_group_name(group), group, scale);
                pressed &= !rtl_pressed_wrong;
        }
        ui_text_percent = kept_percent;
        ui_kit_settle();
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);
        check("text size: at 150 and 300 percent every group of the Settings window, a card, a menu and the launcher, right to left, are their pictures left to right flipped",
              mirrored);
        check("text size: and a press at the mirrored place is on the same thing at 150 and 300 percent", pressed);
}

static fn check_rtl_mirror(void)
{
        static font_face face;
        static p8 atlas[1024 * 1024];
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        positive buffer = (positive)SETTINGS_FRAMES_W * SETTINGS_FRAMES_H * 4;
        long table = compositor_call(syscall(mmap), 0, WINDOWS_HEADER + 2 * buffer, 3, 0x4022, (positive)-1, 0);
        b32 gallery, groups = true;

        rtl_font_make();
        if ((engine < 0 && engine > -4096) || (table < 0 && table > -4096) || !settings_map() ||
            font_load(&face, rtl_font_bytes, rtl_font_size, 0) != 0)
        {
                check("rtl mirror: the block face, the engine and the window load", false);
                return;
        }
        settings_record_keep(&rtl_settings_kept);
        memory_copy(rtl_kit_kept, &ui_kit, sizeof(ui_kit));
        settings_frames.table = (struct windows address_to)table;
        settings_frames.table->stride = SETTINGS_FRAMES_W;
        settings_frames.table->buffer = buffer;
        settings_frames.table->output_width = 4096;
        settings_frames.table->output_height = 4096;
        settings.own = (struct window_row){settings_frames.table, 0, -1, -1, WINDOWS_HEADER + 2 * buffer, buffer, SETTINGS_FRAMES_W, 4096, 4096};
        memory_zero(&settings.ui, sizeof(settings.ui));
        settings.ui.engine = (font_engine address_to)engine;
        font_start(settings.ui.engine, atlas, 1024, 1024);
        settings.ui.regular = &face;
        settings.ui.bold = null;
        settings.ui.scale = 100;
        settings.ui.opaque = 1;
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 1);
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_STANDARD, 0);
        ui_inks(&settings.ui, 0x101012u);
        ui_kit_inks(&settings.ui);
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];

                if (row->kind == SETTINGS_SWITCH)
                        continue;
                settings.state[at] = SETTINGS_READ;
                string_copy_bounded((string_address)settings.value[at], (string_address)(row->standard ? row->standard : "1"), SETTINGS_VALUE);
        }
        settings.page.buffers.shown = -1;
        settings.page.pane = 0;
        // The cuts the kit remembers are of the faces the checks before this one drew with.
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);
        gallery = rtl_view("the kit's gallery", -1, 1);
        groups &= rtl_view("a card", -2, 1) && rtl_view("a card", -2, 2) && rtl_view("a menu", -3, 1) && rtl_view("a menu", -3, 2) &&
                  rtl_view("the launcher", -4, 1);
        for (int scale = 1; scale <= 2; scale++)
                for (int group = 0; group < settings_groups(); group++)
                        groups &= rtl_view(settings_group_name(group), group, scale);
        check("rtl mirror: the kit's gallery, every control of it, right to left is its picture left to right flipped", gallery);
        check("rtl mirror: every group of the Settings window, its card, a menu and the launcher, right to left is its picture left to right flipped", groups);
        check("rtl mirror: a press at a place of the Settings window right to left is on what a press at the mirrored place left to right is on, every group at scales 1 and 2",
              !rtl_pressed_wrong);
        // The Devices group with drives in it, their partitions open: a disk of Windows and Linux, and one with more partitions than its
        // strip shows.
        {
                static p8 kept[sizeof(settings.item[2])];
                unsigned short count = settings.item_count[2];
                int devices = settings_groups_named("Devices");
                b32 drives;

                memory_copy(kept, settings.item[2], sizeof(kept));
                drive_open_all("  1-1 usb 0781:5581 SanDisk Ultra allowed\n"
                           "  nvme0n1 drive 1000204 MB view only\n    part 1048576 536870912 efi EFI_system_partition\n"
                           "    part 537919488 16777216 other Microsoft_reserved_partition\n    part 554696704 900000000000 other Basic_data_partition\n"
                           "    part 900554696704 69000000000 linux Linux_filesystem\n    part 969554696704 30650189312 free -\n"
                           "  sdb drive 64000 MB view only\n    part 0 2097152 free -\n    part 2097152 1048576 other BIOS_boot_partition\n"
                           "    part 3145728 536870912 efi -\n    part 540016640 8000000000 swap Linux_swap\n    part 8540016640 4000000 free -\n"
                           "    part 8544016640 40000000000 linux Linux_root\n    part 48544016640 15000000000 other Media\n    more 2\n",
                               true);
                drives = rtl_view("a drive's strip and its partitions", devices, 1) && rtl_view("a drive's strip and its partitions", devices, 2);
                memory_copy(settings.item[2], kept, sizeof(kept));
                settings.item_count[2] = count;
                memory_zero(settings_drives_open, sizeof(settings_drives_open));
                settings_drive_follow();
                check("rtl mirror: a drive's heading, its strip and its open partitions, right to left, are their picture left to right flipped, at scales 1 and 2", drives);
        }
        check_text_scale();
        // A language that reads from the right, taken while the window is open, places it the other way at once: no resize, no other group.
        {
                static struct windows *language_table_scratch;
                struct windows *const language_table_at = windows_scratch_table(&language_table_scratch);
                int seam;

                language_directory = "languages/";
                language_listen(settings_language);
                rtl_draw(0, 1, false);
                seam = settings.page.seam;
                windows_scratch_zero(language_table_at);
                language_table_at->language = 'a' | 'r' << 8;
                ui_kit_language_take(language_table_at);
                check("rtl mirror: Arabic taken while the Settings window is open puts its column at the right, at once",
                      language_rtl() && ui_rtl() && seam < settings.width / 2 && settings.page.seam > settings.width / 2 && settings.page.left < settings.page.seam);
                language_table_at->language = 0;
                ui_kit_language_take(language_table_at);
                check("rtl mirror: and English puts it back at the left", !language_rtl() && !ui_rtl() && settings.page.seam < settings.width / 2);
        }
        ui_kit.rtl = 0;
        if (ui_cuts)
                memory_zero(ui_cuts, sizeof(struct ui_cut) * UI_CUTS);
        settings_record_put_back(&rtl_settings_kept);
        memory_copy(&ui_kit, rtl_kit_kept, sizeof(ui_kit));
}

#endif

// A code point as UTF-8, into bytes; the count of bytes it takes.
static positive render_glyph(p8 address_to bytes, p32 code)
{
        if (code < 0x80)
        {
                bytes[0] = (p8)code;
                return 1;
        }
        if (code < 0x800)
        {
                bytes[0] = (p8)(0xc0 | code >> 6);
                bytes[1] = (p8)(0x80 | (code & 63));
                return 2;
        }
        bytes[0] = (p8)(0xe0 | code >> 12);
        bytes[1] = (p8)(0x80 | (code >> 6 & 63));
        bytes[2] = (p8)(0x80 | (code & 63));
        return 3;
}

// Whether a cell of the frame is drawn: some pixel is not the paper, and the cell is not the picture of a missing glyph.
static p8 render_drawn(const p32 address_to pixels, p32 paper, p32 row, p32 column)
{
        p8 lit = false, missing = true;

        for (p32 y = 0; y < terminal.cell_h; y++)
                for (p32 x = 0; x < terminal.cell_w; x++)
                {
                        p32 at = (row * terminal.cell_h + y) * terminal.pitch + column * terminal.cell_w + x;
                        p32 gone = (7 * terminal.cell_h + y) * terminal.pitch + 20 * terminal.cell_w + x;

                        lit = lit || pixels[at] != paper;
                        missing = missing && pixels[at] == pixels[gone];
                }
        return lit && !missing;
}

/*
        A forced frame, made while the compositor has not taken the last picture, is drawn into the
        buffer the compositor has not taken, never into the one it still shows: the picture on the
        screen does not change until the next one is taken, so the frame leaves the buffer on the
        screen exactly as it was.
*/
static fn check_forced(void)
{
        struct windows address_to table = terminal.own.table;
        positive bytes = (positive)terminal.pitch * terminal.height * 4;
        p32 address_to kept = render_map(bytes);
        int shown_before, shown_after, shown_on;
        b32 framed;

        if (!kept)
        {
                check("forced: a picture to keep the screen in", false);
                return;
        }

        // The compositor takes the first picture, which is the one on the screen.
        table->shown[0] = table->frame[0];
        terminal.forced = false;
        cursor_hide();
        term_bytes((const p8 address_to) "\x1b[H\x1b[2Kfirst", 13);
        cursor_show();
        {
                unsigned long was = ui_kit_calls;

                framed = terminal_frame();
                if (kit_draw_first(4))
                {
                        kit_draw_print("terminal", ui_kit_calls - was);
                        check("kit calls: a frame of the terminal is made of painter calls", ui_kit_calls - was > 0);
                }
        }
        table->shown[0] = table->frame[0];
        shown_on = terminal.shown;
        check("forced: the first picture is made and taken", framed && terminal.shown >= 0 && windows_ready(&terminal.own));

        // A second picture the compositor has not taken: it still shows the first.
        cursor_hide();
        term_bytes((const p8 address_to) "\x1b[H\x1b[2Ksecond", 14);
        cursor_show();
        framed = terminal_frame();
        table->shown[0] = table->frame[0] - 1;
        shown_before = terminal.shown;
        check("forced: the second picture is made and not taken", framed && shown_before != shown_on);
        memory_copy(kept, windows_buffer(table, 0, shown_on), bytes);

        // A third, forced: it goes somewhere else.
        cursor_hide();
        term_bytes((const p8 address_to) "\x1b[H\x1b[2Kthird", 13);
        cursor_show();
        terminal.forced = true;
        framed = terminal_frame();
        shown_after = terminal.shown;
        check("forced: the third is made over the second", framed && shown_after == shown_before);
        check("forced: the buffer the compositor shows is not drawn into",
              !memory_compare(kept, windows_buffer(table, 0, shown_on), bytes));

        table->shown[0] = table->frame[0];
        terminal.forced = false;
}

/*
        The frames' mapping cannot be made: the address space is at a limit
        the frames' old mapping does not make room under. The frame is then
        refused, and nothing is drawn through the pointers the mapping left
        behind; when the limit is lifted the frames are made again and the
        next frame is drawn.
*/
static fn check_frames_refused(void)
{
        struct { unsigned long current, maximum; } was, limit;
        p8 statm[64] = {0};
        positive pages = 0;
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address)"/proc/self/statm", statm, sizeof(statm) - 1);
        positive mapped = 3 * (positive)terminal.frame_bytes;
        b32 refused;

        for (bipolar at = 0; at < got && statm[at] >= '0' && statm[at] <= '9'; at++)
                pages = pages * 10 + (positive)(statm[at] - '0');
        if (got <= 0 || !mapped || pages * 4096 < mapped + 4096)
        {
                check("frames: the address space to test with is read", false);
                return;
        }

        system_call_4(syscall(prlimit64), 0, 9, 0, (positive)&was);
        limit.current = pages * 4096 - mapped - 4096;
        limit.maximum = was.maximum;
        system_call_4(syscall(prlimit64), 0, 9, (positive)&limit, 0);

        //      qemu-user keeps RLIMIT_AS for itself: it takes the call and sets
        //      nothing, and the statm it answers is its own. Read back, a
        //      limit that is not the one set is said, and the refusal is not
        //      tried against a limit nobody keeps.
        {
                struct { unsigned long current, maximum; } now = {0, 0};

                system_call_4(syscall(prlimit64), 0, 9, 0, (positive)&now);
                if (now.current != limit.current)
                {
                        system_call_4(syscall(prlimit64), 0, 9, (positive)&was, 0);
                        string_format(log, "  frames: a frame whose pictures cannot be mapped NOT RUN -- the "
                                           "address-space limit did not take (qemu-user keeps RLIMIT_AS for itself)\n");
                        check("frames: and the next frame, with the address space back, is drawn",
                              terminal_frames() && terminal_frame());
                        return;
                }
        }

        // The old mapping goes, and the new one cannot be made.
        refused = !terminal_frames();
        cursor_hide();
        term_bytes((const p8 address_to)"\x1b[H\x1b[2Krefused", 16);
        cursor_show();
        refused = refused && !terminal_frame();

        system_call_4(syscall(prlimit64), 0, 9, (positive)&was, 0);
        check("frames: a frame whose pictures cannot be mapped is refused, not drawn through a lost mapping",
              refused && !terminal.frames && !terminal.frame[2]);
        check("frames: and the next frame, with the address space back, is drawn", terminal_frames() && terminal_frame());
        terminal.forced = false;
}

/*
        A shell that is gone says how it ended in its cells, and that line is
        drawn even when the compositor has not taken the last picture: the frame
        waits for it, and is drawn at once once it is forced past the wait.
*/
/*
        A line written at one width and drawn at a narrower one folds into
        whole rows, one after the other: the line that says how a shell ended,
        36 characters, written at 44 columns and drawn at 25, is its first 25
        characters on one row and the rest on the next, with no blank row
        between them.
*/
static fn check_fold(void)
{
        unsigned int was_columns = window->columns, was_rows = window->rows;
        const struct window_cell address_to cells;
        b32 found = false, adjacent = false;

        window->columns = 44;
        window->rows = 6;
        regrid(-1);
        render_surface();
        term_bytes((const p8 address_to)"\r\n$ ls\r\n", 8);
        term_child_killed(9, (string_address)"KILL", false);

        window->columns = 25;
        regrid(-1);
        terminal_view_live();
        render_surface();
        terminal.forced = true;
        terminal_frame();
        cells = terminal.frame[terminal.shown];
        for (unsigned int r = 0; r + 1 < ROWS && !found; r++)
        {
                const struct window_cell address_to row = cells + (unsigned long)r * COLUMNS;

                if (row[0].character != '[' || row[1].character != 's')
                        continue;
                found = row[COLUMNS - 1].character == '9';
                adjacent = found && row[COLUMNS].character == ' ' && row[COLUMNS + 1].character == '(';
        }
        check("fold: a 36 character line at 25 columns is its first 25 characters on a row", found);
        check("fold: and the rest on the next row, with no blank row between", adjacent);

        terminal.forced = false;
        window->columns = was_columns;
        window->rows = was_rows;
        regrid(-1);
        terminal_view_live();
        render_surface();
}

