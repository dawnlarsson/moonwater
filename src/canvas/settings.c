/*
        The settings, as a window on the compositor in userspace.

        `settings` is a front end to the moonwater command and to nothing
        else: every value it shows is what a verb says when it is asked
        (moonwater name, moonwater keyboard, ...), read from the verb's
        output as a person would read it, and every change is the verb run
        with the new value. What the command keeps is what the window shows
        the next time it asks, so the two never disagree, and a value the
        command refuses is refused in the window in the command's words.

        The rows are the switches and choices of the command that a person
        sets by hand: the machine's name, the keyboard, the time zone, the
        clock and whether the network sets it, the wired link, the radios
        and airplane mode, which link the internet prefers, names over TLS,
        the power profile, the backlight and the processor's boost. A row
        the machine has nothing for (no backlight, no boost switch) says so
        and does nothing. Nothing here ends the desktop, the session or the
        machine.

        It is glass (window.c, WINDOW_GLASS) drawn with the system's own
        pieces (ui.c). The keys: Up and Down or Tab and Shift-Tab move, Enter
        or Space changes a row (a switch is thrown, a choice moves on, a
        field is typed in and Enter keeps it, Escape leaves it as it was),
        Left and Right move a choice or a step either way, and Escape closes
        the window, as Alt-F4 and its close button do. A press on a row is
        Enter on it, and the wheel scrolls.

        One window: a second start finds the first by the program's name and
        lifts it -- raised, brought back if it was put away, given the keys --
        and ends.

            settings [-f FONT.ttf] [-b BOLD.ttf] [-m MOONWATER]

        -m is the command it runs, /moonwater by default.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_SETTINGS
#define MOONWATER_CANVAS_SETTINGS

#define SETTINGS_SWITCH 1u
#define SETTINGS_CHOICE 2u
#define SETTINGS_FIELD 3u
#define SETTINGS_STEP 4u
#define SETTINGS_ACTION 5u

// What a row's value is: not asked yet, said, not on this machine, asked and not found in what was said.
#define SETTINGS_UNREAD 0u
#define SETTINGS_READ 1u
#define SETTINGS_ABSENT 2u
#define SETTINGS_FAILED 3u

#define SETTINGS_VALUE 64
#define SETTINGS_SAID 4096
#define SETTINGS_STATUS 160
#define SETTINGS_WORDS 8
#define SETTINGS_WAIT_NS 20000000000ll

// In logical pixels, times the scale.
#define SETTINGS_WIDTH 520
#define SETTINGS_TOP 60     // the title's band
#define SETTINGS_HEADER 34  // a section's header over its card
#define SETTINGS_GAP 10     // under a card
#define SETTINGS_FOOT 40    // the line that says what happened
#define SETTINGS_FIELD_W 220

#define SETTINGS_KEY_ESCAPE 1
#define SETTINGS_KEY_BACKSPACE 14
#define SETTINGS_KEY_TAB 15
#define SETTINGS_KEY_ENTER 28
#define SETTINGS_KEY_SPACE 57
#define SETTINGS_KEY_HOME 102
#define SETTINGS_KEY_UP 103
#define SETTINGS_KEY_PAGE_UP 104
#define SETTINGS_KEY_LEFT 105
#define SETTINGS_KEY_RIGHT 106
#define SETTINGS_KEY_END 107
#define SETTINGS_KEY_DOWN 108
#define SETTINGS_KEY_PAGE_DOWN 109
#define SETTINGS_KEY_ENTER_PAD 96
#define SETTINGS_BUTTON_LEFT 0x110

struct settings_row
{
        const char address_to section; // the header this row starts a card under, or none
        const char address_to label;
        unsigned char kind;
        unsigned char lacks;           // a machine may have none: no key said is no value, not a failure
        unsigned char line;            // the value is the rest of the line and not a word
        const char address_to read[3]; // what is asked, after moonwater
        const char address_to key;     // the words the value follows ("" is the whole first line)
        const char address_to also;    // and where it is when that is said with nothing after it
        const char address_to set[3];  // what changes it; the value goes after
        const char address_to choices; // a choice's words, or a step's two, a space between
};

static const struct settings_row settings_rows[] = {
    {"Machine", "Name", SETTINGS_FIELD, 0, 1, {"name"}, "", null, {"name"}, null},
    {null, "Keyboard", SETTINGS_CHOICE, 0, 0, {"keyboard"}, "keyboard ", null, {"keyboard"}, "us uk de se no dk fi fr es it"},
    {null, "Time zone", SETTINGS_FIELD, 0, 0, {"timezone"}, "timezone ", null, {"timezone"}, null},
    {"Time", "Set the clock from the network", SETTINGS_SWITCH, 0, 0, {"ntp"}, "ntp ", null, {"ntp"}, null},
    {null, "Clock (Enter sets it now)", SETTINGS_ACTION, 0, 1, {"time"}, "local ", null, {"time", "sync"}, null},
    {"Network", "Wired", SETTINGS_SWITCH, 0, 0, {"wired"}, "wired ", null, {"wired"}, null},
    {null, "Wi-Fi", SETTINGS_SWITCH, 0, 0, {"wifi"}, "wifi ", null, {"wifi"}, null},
    {null, "Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth"}, "bluetooth ", null, {"bluetooth"}, null},
    {null, "Airplane mode", SETTINGS_SWITCH, 0, 0, {"airplane"}, "airplane ", null, {"airplane"}, null},
    {null, "The internet prefers", SETTINGS_CHOICE, 0, 0, {"priority"}, "internet prefers ", null,
     {"priority", "internet"}, "wired wifi"},
    {null, "Names over TLS", SETTINGS_CHOICE, 0, 0, {"dns", "tls"}, "dns tls ", null, {"dns", "tls"}, "off on only"},
    {"Power", "Power profile", SETTINGS_CHOICE, 1, 0, {"power"}, "power ", "profile ", {"power"},
     "balanced performance powersave"},
    {null, "Brightness", SETTINGS_STEP, 1, 0, {"brightness"}, "brightness ", null, {"brightness"}, "-10 +10"},
    {null, "Boost", SETTINGS_SWITCH, 1, 0, {"cpu"}, "boost ", null, {"cpu", "boost"}, null},
};

#define SETTINGS_ROWS (sizeof(settings_rows) / sizeof(settings_rows[0]))

/* ---- the model: what a verb said, and what to run ---- */

/*
        One line of what a verb said, as words: escapes taken out, the
        "[Moonwater] " a verb starts its lines with and the blanks that
        indent them taken off. Answers the line's length, and where the
        next line starts.
*/
static positive settings_line(const char address_to said, positive length, positive address_to at, char address_to into,
                              positive room)
{
        positive made = 0;

        while (address_to at < length && said[address_to at] != '\n')
        {
                char c = said[address_to at];

                address_to at += 1;
                if (c == '\t')
                        c = ' ';
                // An escape sequence: its introducer, its parameters and its final letter.
                if (c == 0x1b)
                {
                        if (address_to at < length && said[address_to at] == '[')
                        {
                                address_to at += 1;
                                while (address_to at < length && (said[address_to at] < 0x40 || said[address_to at] > 0x7e))
                                        address_to at += 1;
                        }
                        address_to at += address_to at < length;
                        continue;
                }
                if ((unsigned char)c < ' ' || c == 0x7f || made + 1 >= room)
                        continue;
                into[made++] = c;
        }
        address_to at += address_to at < length;
        into[made] = 0;

        {
                static const char label[] = "[Moonwater] ";
                positive skip = 0;

                if (made >= sizeof(label) - 1 && !memory_compare(into, label, sizeof(label) - 1))
                        skip = sizeof(label) - 1;
                while (skip < made && into[skip] == ' ')
                        skip++;
                // To the left, a byte at a time, which is front to back.
                for (positive at_byte = 0; skip && at_byte + skip <= made; at_byte++)
                        into[at_byte] = into[at_byte + skip];
                made -= skip;
        }
        return made;
}

// Where key is in a line, at the start or after a blank or a semicolon; the line's length when it is not.
static positive settings_find(const char address_to line, positive length, const char address_to key)
{
        positive size = string_length((string_address)key);

        for (positive at = 0; at + size <= length; at++)
                if ((!at || line[at - 1] == ' ' || line[at - 1] == ';') && !memory_compare(line + at, key, size))
                        return at;
        return length;
}

// What a line of a machine that has none of a thing says.
static b32 settings_absent(const char address_to line, positive length)
{
        return settings_find(line, length, "this machine has no") < length ||
               settings_find(line, length, "no wireless hardware") < length;
}

/*
        A verb's output as a row's value, into value: the word after the
        row's key (or with line the rest of the line; with no key, the first
        line), or after its second key when the first is said with nothing
        after it. Answers the state that leaves the row in.
*/
static unsigned int settings_parse(const struct settings_row address_to row, const char address_to said, positive length,
                                   char address_to value, positive room)
{
        char line[SETTINGS_STATUS];
        positive at = 0, found = 0;
        b32 absent = false;

        value[0] = 0;
        while (at < length)
        {
                positive size = settings_line(said, length, &at, line, sizeof(line));

                if (!size)
                        continue;
                absent |= settings_absent(line, size);

                for (positive which = 0; which < 2 && !found; which++)
                {
                        const char address_to key = which ? row->also : row->key;
                        positive where, start, stop;

                        if (!key)
                                continue;
                        where = key[0] ? settings_find(line, size, key) : 0;
                        if (where >= size)
                                continue;
                        start = where + string_length((string_address)key);
                        while (start < size && line[start] == ' ')
                                start++;
                        stop = start;
                        while (stop < size && (row->line || (line[stop] != ' ' && line[stop] != ',' && line[stop] != ';' &&
                                                             line[stop] != '(')))
                                stop++;
                        while (stop > start && line[stop - 1] == ' ')
                                stop--;
                        if (stop == start)
                                continue;
                        stop = stop - start >= room ? start + room - 1 : stop;
                        memory_copy(value, line + start, stop - start);
                        value[stop - start] = 0;
                        found = 1;
                }
        }

        return absent ? SETTINGS_ABSENT : found ? SETTINGS_READ : row->lacks ? SETTINGS_ABSENT : SETTINGS_FAILED;
}

// The word of a list (words a blank apart) at index, into into; false past its end.
static b32 settings_word(const char address_to words, positive index, char address_to into, positive room)
{
        while (words && *words)
        {
                positive size = 0;

                while (words[size] && words[size] != ' ')
                        size++;
                if (!index--)
                {
                        size = size >= room ? room - 1 : size;
                        memory_copy(into, words, size);
                        into[size] = 0;
                        return true;
                }
                words += size + (words[size] == ' ');
        }
        return false;
}

/*
        What a switch, a choice or a step goes to from value, one way (1) or
        the other (-1): a switch to the other of on and off, a choice to the
        next or the last of its words (from a value that is none of them, to
        the first or the last), a step to its word for that way.
*/
static fn settings_next(const struct settings_row address_to row, const char address_to value, int way, char address_to into,
                        positive room)
{
        char word[SETTINGS_VALUE];
        positive count = 0, at;

        into[0] = 0;
        if (row->kind == SETTINGS_SWITCH)
        {
                string_copy_bounded((string_address)into,
                                    (string_address)(way ? (string_equals((string_address)value, (string_address) "on") ? "off" : "on")
                                                         : value),
                                    room);
                return;
        }
        if (row->kind == SETTINGS_STEP)
        {
                settings_word(row->choices, way > 0 ? 1 : 0, into, room);
                return;
        }
        if (row->kind != SETTINGS_CHOICE)
                return;

        while (settings_word(row->choices, count, word, sizeof(word)))
                count++;
        if (!count)
                return;
        for (at = 0; at < count; at++)
                if (settings_word(row->choices, at, word, sizeof(word)) && string_equals((string_address)word, (string_address)value))
                        break;
        at = at == count ? (way > 0 ? count - 1 : 0) : at;
        settings_word(row->choices, (at + count + (way > 0 ? 1 : count - 1)) % count, into, room);
}

/*
        The words that change a row: the command, the row's verb and the
        value (none for an action), into words with a null after them.
        Never a shell's: a name typed is one word, whatever is in it.
        Answers how many.
*/
static positive settings_command(const struct settings_row address_to row, string_address command, const char address_to value,
                                 string_address address_to words, positive room, b32 asking)
{
        positive count = 0;
        const char address_to const address_to verb = asking ? row->read : row->set;

        if (room < 6)
                return 0;
        words[count++] = command;
        for (positive at = 0; at < 3 && verb[at]; at++)
                words[count++] = (string_address)verb[at];
        if (!asking && row->kind != SETTINGS_ACTION)
                words[count++] = (string_address)value;
        words[count] = null;
        return count;
}

/* ---- the program ---- */

static struct
{
        struct window_row own;
        struct ui ui;
        font_face face[2];
        string_address moonwater;
        char value[SETTINGS_ROWS][SETTINGS_VALUE];
        unsigned char state[SETTINGS_ROWS];
        int top[SETTINGS_ROWS]; // where each row is in the list, from the list's top
        int content;            // how tall the list is
        int focus, editing, scroll;
        char typed[SETTINGS_VALUE];
        char status[SETTINGS_STATUS];
        int width, height, shown;
        unsigned int asked_w, asked_h, ground;
        int pointer_x, pointer_y;
        b32 focused, dirty;

        // The verb being run: its process, the read end of its output, what it said, when it began, for which row.
        bipolar child;
        b32 output;
        char said[SETTINGS_SAID];
        positive said_length, began;
        int job;
        b32 job_sets;
        char job_value[SETTINGS_VALUE];
        positive unread;  // rows to ask, a bit each
        int pending;      // a row to change once the verb running is done, or -1
        char pending_value[SETTINGS_VALUE];
        b32 told;         // the first reading is said in the machine's log
} settings;

static int settings_px(int logical)
{
        return logical * settings.ui.scale;
}

// Where each row is: a header over the first of a section, a gap under the last.
static fn settings_layout(void)
{
        int y = 0;

        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                if (settings_rows[at].section)
                        y += settings_px(SETTINGS_HEADER);
                settings.top[at] = y;
                y += settings_px(UI_ROW);
                if (at + 1 == SETTINGS_ROWS || settings_rows[at + 1].section)
                        y += settings_px(SETTINGS_GAP);
        }
        settings.content = y;
}

static int settings_list_top(void)
{
        return settings_px(SETTINGS_TOP);
}

static int settings_list_bottom(void)
{
        return settings.height - settings_px(SETTINGS_FOOT);
}

static fn settings_scroll_to(int scroll)
{
        int most = settings.content - (settings_list_bottom() - settings_list_top());

        most = most < 0 ? 0 : most;
        scroll = scroll > most ? most : scroll;
        scroll = scroll < 0 ? 0 : scroll;
        if (scroll != settings.scroll)
        {
                settings.scroll = scroll;
                settings.dirty = true;
        }
}

// The row with the focus kept in sight.
static fn settings_reveal(void)
{
        int top = settings.top[settings.focus], height = settings_px(UI_ROW), room = settings_list_bottom() - settings_list_top();

        // A section's first row brings its header with it.
        if (settings_rows[settings.focus].section)
        {
                top -= settings_px(SETTINGS_HEADER);
                height += settings_px(SETTINGS_HEADER);
        }
        if (top < settings.scroll)
                settings_scroll_to(top);
        else if (top + height > settings.scroll + room)
                settings_scroll_to(top + height - room);
}

/* ---- the verbs ---- */

static fn settings_say(string_address text)
{
        string_copy_bounded((string_address)settings.status, text, sizeof(settings.status));
        settings.dirty = true;
}

// A verb run with its output to a pipe this end reads, its input nothing; false when it could not be.
static b32 settings_start(string_address address_to words, positive count)
{
        int ends[2] = {-1, -1};
        bipolar child;

        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC | O_NONBLOCK) != 0)
                return false;

        child = system_fork();
        if (child == 0)
        {
                bipolar quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ | O_CLOEXEC);

                if (quiet >= 0)
                        system_call_3(syscall(dup3), (positive)quiet, 0, 0);
                system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                system_call_3(syscall(dup3), (positive)ends[1], 2, 0);
                // As its own: not reading from where this program writes.
                (void)shell_exec_file(words[0], words, count, program_environment_list());
                system_call_1(syscall(exit), 127);
        }
        system_close(ends[1]);
        if (child < 0)
        {
                system_close(ends[0]);
                return false;
        }

        settings.child = child;
        settings.output = ends[0];
        settings.said_length = 0;
        settings.began = clock_monotonic_nanoseconds();
        return true;
}

// The next verb to run: a change first, then the rows not yet asked.
static fn settings_next_job(void)
{
        string_address words[SETTINGS_WORDS];
        positive count;

        while (settings.child <= 0)
        {
                if (settings.pending >= 0)
                {
                        settings.job = settings.pending;
                        settings.job_sets = true;
                        memory_copy(settings.job_value, settings.pending_value, sizeof(settings.job_value));
                        settings.pending = -1;
                }
                else if (settings.unread)
                {
                        settings.job = (int)bits_trailing_zeros(settings.unread);
                        settings.unread &= settings.unread - 1;
                        settings.job_sets = false;
                }
                else
                        return;

                count = settings_command(&settings_rows[settings.job], settings.moonwater, settings.job_value, words,
                                         SETTINGS_WORDS, !settings.job_sets);
                if (count && settings_start(words, count))
                        return;

                if (settings.job_sets)
                        settings_say((string_address) "the moonwater command could not be run");
                else
                        settings.state[settings.job] = SETTINGS_FAILED;
                settings.dirty = true;
        }
}

// Every row asked again: a change to one can move another (airplane mode turns the radios off).
static fn settings_ask_all(void)
{
        settings.unread = ((positive)1 << SETTINGS_ROWS) - 1;
}

// A row changed to value, now or once the verb running has finished.
static fn settings_change(int row, const char address_to value)
{
        settings.pending = row;
        string_copy_bounded((string_address)settings.pending_value, (string_address)value, sizeof(settings.pending_value));
        settings_say((string_address) "...");
        settings_next_job();
}

// The verb running has finished, or taken too long: what it said, put where it belongs.
static fn settings_finished(b32 killed)
{
        positive status = 0;
        int row = settings.job;

        if (killed)
                system_call_2(syscall(kill), (positive)settings.child, SIGKILL);
        system_wait4_retry(settings.child, address_of status, 0, null);
        system_close(settings.output);
        settings.child = 0;
        settings.output = -1;

        if (settings.job_sets)
        {
                char line[SETTINGS_STATUS];
                positive at = 0, size = 0;

                // The verb's own first words, which say what it did or why it would not.
                while (at < settings.said_length && !size)
                        size = settings_line(settings.said, settings.said_length, &at, line, sizeof(line));
                if (killed)
                        settings_say((string_address) "moonwater took too long, and was stopped");
                else if (size)
                        settings_say((string_address)line);
                else
                        settings_say((string_address)(wait_status_code(status) ? "moonwater refused" : "done"));
                settings_ask_all();
        }
        else
        {
                settings.state[row] = killed ? SETTINGS_FAILED
                                             : (unsigned char)settings_parse(&settings_rows[row], settings.said, settings.said_length,
                                                                             settings.value[row], SETTINGS_VALUE);
                settings.dirty = true;
        }

        settings_next_job();

        // The first reading of every row, said once where the machine's log is: how many said nothing.
        if (!settings.told && !settings.unread && settings.child <= 0 && settings.pending < 0)
        {
                positive failed = 0;

                for (positive at = 0; at < SETTINGS_ROWS; at++)
                        failed += settings.state[at] == SETTINGS_FAILED || settings.state[at] == SETTINGS_UNREAD;
                settings.told = true;
                compositor_kmsg((string_address) "settings: rows unread or not understood when first asked: ", failed,
                                (string_address) "");
        }
}

// What the verb running has said, read; its end, heard.
static fn settings_drain(void)
{
        for (;;)
        {
                bipolar got;

                if (settings.said_length >= sizeof(settings.said))
                {
                        char spill[256];

                        got = system_read_once(settings.output, spill, sizeof(spill));
                }
                else
                        got = system_read_once(settings.output, settings.said + settings.said_length,
                                               sizeof(settings.said) - settings.said_length);
                if (got > 0)
                {
                        settings.said_length += settings.said_length < sizeof(settings.said) ? (positive)got : 0;
                        continue;
                }
                if (got == 0 || (got != -EAGAIN && got != -EINTR))
                        settings_finished(false);
                return;
        }
}

/* ---- what a person does ---- */

// A row changed one way or the other: a switch thrown, a choice or a step moved, a field opened, an action run.
static fn settings_activate(int row, int way)
{
        const struct settings_row address_to which = &settings_rows[row];
        char next[SETTINGS_VALUE];

        if (settings.state[row] == SETTINGS_ABSENT || settings.state[row] == SETTINGS_UNREAD)
                return;

        switch (which->kind)
        {
        case SETTINGS_FIELD:
                settings.editing = row;
                string_copy_bounded((string_address)settings.typed, (string_address)settings.value[row], sizeof(settings.typed));
                settings.dirty = true;
                return;
        case SETTINGS_ACTION:
                settings_change(row, "");
                return;
        case SETTINGS_SWITCH:
                if (way > 1 || way < -1)
                {
                        // Right is on and Left off, whatever it was.
                        if (string_equals((string_address)settings.value[row], (string_address)(way > 0 ? "on" : "off")))
                                return;
                        way = 1;
                }
                break;
        }

        settings_next(which, settings.value[row], way, next, sizeof(next));
        if (next[0])
                settings_change(row, next);
}

static fn settings_move(int by)
{
        int focus = settings.focus + by;

        focus = focus < 0 ? 0 : focus >= (int)SETTINGS_ROWS ? (int)SETTINGS_ROWS - 1 : focus;
        if (focus != settings.focus)
        {
                settings.focus = focus;
                settings.dirty = true;
        }
        settings_reveal();
}

// A key in a field being typed in: Enter keeps it, Escape leaves it, Backspace takes a character.
static fn settings_type(const struct window_event address_to event)
{
        positive length = string_length((string_address)settings.typed);

        switch (event->a)
        {
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
                if (length)
                        settings_change(settings.editing, settings.typed);
                settings.editing = -1;
                break;
        case SETTINGS_KEY_ESCAPE:
                settings.editing = -1;
                break;
        case SETTINGS_KEY_BACKSPACE:
                while (length && ((unsigned char)settings.typed[length - 1] & 0xc0) == 0x80)
                        length--;
                settings.typed[length ? length - 1 : 0] = 0;
                break;
        default:
        {
                unsigned int c = (unsigned int)event->b;
                p8 bytes[4];
                positive size = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;

                if (c < ' ' || c == 0x7f || (event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) ||
                    length + size >= sizeof(settings.typed))
                        return;
                if (size == 1)
                        bytes[0] = (p8)c;
                else if (size == 2)
                        bytes[0] = (p8)(0xc0 | c >> 6), bytes[1] = (p8)(0x80 | (c & 63));
                else if (size == 3)
                        bytes[0] = (p8)(0xe0 | c >> 12), bytes[1] = (p8)(0x80 | (c >> 6 & 63)), bytes[2] = (p8)(0x80 | (c & 63));
                else
                        bytes[0] = (p8)(0xf0 | c >> 18), bytes[1] = (p8)(0x80 | (c >> 12 & 63)),
                        bytes[2] = (p8)(0x80 | (c >> 6 & 63)), bytes[3] = (p8)(0x80 | (c & 63));
                memory_copy(settings.typed + length, bytes, size);
                settings.typed[length + size] = 0;
        }
        }
        settings.dirty = true;
}

// A key: false when it closes the window.
static b32 settings_key(const struct window_event address_to event)
{
        b32 shift = (event->flags & WINDOW_KEY_SHIFT) != 0;

        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;
        if (settings.editing >= 0)
        {
                settings_type(event);
                return true;
        }

        switch (event->a)
        {
        case SETTINGS_KEY_ESCAPE:
                return false;
        case SETTINGS_KEY_UP:
                settings_move(-1);
                break;
        case SETTINGS_KEY_DOWN:
                settings_move(1);
                break;
        case SETTINGS_KEY_TAB:
                settings_move(shift ? -1 : 1);
                break;
        case SETTINGS_KEY_HOME:
                settings_move(-(int)SETTINGS_ROWS);
                break;
        case SETTINGS_KEY_END:
                settings_move((int)SETTINGS_ROWS);
                break;
        case SETTINGS_KEY_PAGE_UP:
                settings_move(-5);
                break;
        case SETTINGS_KEY_PAGE_DOWN:
                settings_move(5);
                break;
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
        case SETTINGS_KEY_SPACE:
                settings_activate(settings.focus, 1);
                break;
        case SETTINGS_KEY_LEFT:
                if (settings_rows[settings.focus].kind != SETTINGS_FIELD && settings_rows[settings.focus].kind != SETTINGS_ACTION)
                        settings_activate(settings.focus, settings_rows[settings.focus].kind == SETTINGS_SWITCH ? -2 : -1);
                break;
        case SETTINGS_KEY_RIGHT:
                if (settings_rows[settings.focus].kind != SETTINGS_FIELD && settings_rows[settings.focus].kind != SETTINGS_ACTION)
                        settings_activate(settings.focus, settings_rows[settings.focus].kind == SETTINGS_SWITCH ? 2 : 1);
                break;
        }
        return true;
}

// The row under a point of the window, or -1.
static int settings_hit(int x, int y)
{
        int list = y - settings_list_top() + settings.scroll;

        if (y < settings_list_top() || y >= settings_list_bottom() || x < settings_px(UI_PAD) ||
            x >= settings.width - settings_px(UI_PAD))
                return -1;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (list >= settings.top[at] && list < settings.top[at] + settings_px(UI_ROW))
                        return (int)at;
        return -1;
}

static b32 settings_event(const struct window_event address_to event)
{
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return settings_key(event);
        case WINDOW_EVENT_MOTION:
                settings.pointer_x = event->a;
                settings.pointer_y = event->b;
                break;
        case WINDOW_EVENT_BUTTON:
        {
                int row = settings_hit(settings.pointer_x, settings.pointer_y);

                if (!(event->flags & WINDOW_KEY_DOWN) || event->a != SETTINGS_BUTTON_LEFT)
                        break;
                if (settings.editing >= 0 && row != settings.editing)
                {
                        settings.editing = -1;
                        settings.dirty = true;
                }
                if (row < 0)
                        break;
                settings.focus = row;
                settings.dirty = true;
                if (settings.editing != row)
                {
                        // A step or a choice pressed left of its value's middle goes back.
                        int middle = settings.width - settings_px(UI_PAD + 14 + 40);
                        unsigned char kind = settings_rows[row].kind;

                        settings_activate(row, (kind == SETTINGS_STEP || kind == SETTINGS_CHOICE) && settings.pointer_x < middle
                                                   ? -1
                                                   : 1);
                }
                break;
        }
        case WINDOW_EVENT_WHEEL:
                if (!event->b)
                        settings_scroll_to(settings.scroll - event->a * settings_px(UI_ROW) / 120);
                break;
        }
        return true;
}

/* ---- the picture ---- */

static fn settings_draw_row(struct ui address_to ui, positive at, int y)
{
        const struct settings_row address_to row = &settings_rows[at];
        int left = settings_px(UI_PAD), right = settings.width - settings_px(UI_PAD), inset = settings_px(14);
        int height = settings_px(UI_ROW), baseline = ui_baseline(ui, y, height, UI_TEXT);
        unsigned int state = settings.state[at];
        b32 usable = state == SETTINGS_READ || state == SETTINGS_FAILED;
        string_address shown = (string_address)settings.value[at];

        if ((int)at == settings.focus)
                ui_focus(ui, left + settings_px(3), y + settings_px(2), right - left - settings_px(6), height - settings_px(4),
                         settings.focused);
        ui_text(ui, left + inset, baseline, (string_address)row->label, UI_TEXT, false, ui->ink, usable ? 255 : 130);

        if (state == SETTINGS_UNREAD)
                shown = (string_address) "...";
        else if (state == SETTINGS_ABSENT)
                shown = (string_address) "not on this machine";
        else if (state == SETTINGS_FAILED || !shown[0])
                shown = (string_address) "unknown";

        if ((int)at == settings.editing)
        {
                int width = settings_px(SETTINGS_FIELD_W);

                ui_field(ui, right - inset - width, y + settings_px(7), width, height - settings_px(14),
                         (string_address)settings.typed, true);
                return;
        }

        if (row->kind == SETTINGS_SWITCH && state == SETTINGS_READ)
                ui_switch(ui, right - inset, y + height / 2, string_equals(shown, (string_address) "on"), true);
        else
                ui_value(ui, right - inset, baseline, shown, state == SETTINGS_READ &&
                                                                 (row->kind == SETTINGS_CHOICE || row->kind == SETTINGS_STEP),
                         usable);
}

static fn settings_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &settings.ui;
        int left = settings_px(UI_PAD), width = settings.width - 2 * settings_px(UI_PAD);
        int list_top = settings_list_top(), list_bottom = settings_list_bottom();
        positive first = 0;

        ui->pixels = pixels;
        ui->pitch = settings.own.table->stride;
        ui->width = settings.width;
        ui->height = settings.height;
        ui->top = 0;
        ui->bottom = settings.height;
        ui_clear(ui);
        ui_title(ui, left + settings_px(6), settings_px(8), list_top - settings_px(8), (string_address) "Settings");

        ui->top = list_top;
        ui->bottom = list_bottom;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                int y = list_top + settings.top[at] - settings.scroll;

                if (settings_rows[at].section)
                {
                        positive last = at;

                        while (last + 1 < SETTINGS_ROWS && !settings_rows[last + 1].section)
                                last++;
                        ui_header(ui, left + settings_px(6), y - settings_px(SETTINGS_HEADER), settings_px(SETTINGS_HEADER),
                                  (string_address)settings_rows[at].section);
                        ui_card(ui, left, y, width, (int)(last - at + 1) * settings_px(UI_ROW));
                        first = at;
                }
                if (at != first)
                        ui_rule(ui, left + settings_px(14), y, width - settings_px(14));
                settings_draw_row(ui, at, y);
        }

        ui->top = list_bottom;
        ui->bottom = settings.height;
        ui_text(ui, left + settings_px(6), ui_baseline(ui, list_bottom, settings_px(SETTINGS_FOOT), UI_SMALL),
                (string_address)settings.status, UI_SMALL, false, ui->quiet, 255);
}

// One picture, into the buffer the compositor is not reading; false when there is none yet.
static b32 settings_frame(void)
{
        struct windows address_to table = settings.own.table;
        unsigned int row = settings.own.row;
        int slot = settings.shown < 0 ? 0 : 1 - settings.shown;

        if (settings.shown >= 0 && !windows_ready(&settings.own))
                return false;

        settings_draw(windows_buffer(table, row, (unsigned int)slot));
        table->width[row] = (unsigned int)settings.width;
        table->height[row] = (unsigned int)settings.height;
        table->damage_width[row] = 0;
        windows_show(&settings.own, (unsigned int)slot);
        settings.shown = slot;
        settings.dirty = false;
        return true;
}

/* ---- the program ---- */

// Whether a process is this program: its name.
static b32 settings_is(unsigned int pid)
{
        p8 path[40] = "/proc/", name[16] = {0};
        bipolar handle;

        path[6 + positive_into_string(path + 6, (positive)pid)] = 0;
        string_copy((string_address)path + string_length((string_address)path), (string_address) "/comm");
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return false;
        system_read_once((b32)handle, name, sizeof(name) - 1);
        system_close(handle);
        return !memory_compare(name, "settings\n", 9);
}

/*
        Another settings window already up -- one that has shown a picture --
        is lifted, and this one goes: true when that is what happened.
*/
static b32 settings_lift_other(void)
{
        struct windows address_to table = settings.own.table;

        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                unsigned int pid = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);
                char name[48];
                bipolar door;

                if (row == settings.own.row || !pid || !__atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE) ||
                    !settings_is(pid))
                        continue;

                __atomic_add_fetch(&table->lift[row], 1, __ATOMIC_RELEASE);
                windows_door_name(name, row, 0);
                door = system_open_at(AT_FDCWD, (string_address)name, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                if (door >= 0)
                {
                        system_write_once((b32)door, "", 1);
                        system_close(door);
                }
                return true;
        }
        return false;
}

static b32 settings_fonts(string_address regular, string_address bold)
{
        static p8 atlas[512 * 512];
        positive size = 0, bytes = regular ? compositor_map_file(regular, &size) : 0;
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x22, (positive)-1, 0);

        if (!bytes || (engine < 0 && engine > -4096) || font_load(&settings.face[0], (const p8 address_to)bytes, size, 0) != 0)
                return false;
        settings.ui.engine = (font_engine address_to)engine;
        font_start(settings.ui.engine, atlas, 512, 512);
        settings.ui.regular = &settings.face[0];
        settings.ui.bold = null;

        bytes = bold ? compositor_map_file(bold, &size) : 0;
        if (bytes && font_load(&settings.face[1], (const p8 address_to)bytes, size, 0) == 0)
                settings.ui.bold = &settings.face[1];
        return true;
}

// The size the window is drawn at: its own, or what the compositor asked, never past the output.
static fn settings_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = settings.own.table;
        unsigned int scratch = width + 8u * (unsigned int)settings.ui.scale;

        settings.width = (int)(width > table->output_width ? table->output_width : width);
        settings.height = (int)(height > table->output_height ? table->output_height : height);
        if (scratch > (unsigned int)settings.ui.scratch_width)
        {
                long mapped = compositor_call(syscall(mmap), 0,
                                              (positive)scratch * UI_SCRATCH_ROWS * (positive)settings.ui.scale * 4, 3, 0x22,
                                              (positive)-1, 0);

                if (!(mapped < 0 && mapped > -4096))
                {
                        if (settings.ui.scratch)
                                compositor_call(syscall(munmap), (positive)settings.ui.scratch,
                                                (positive)settings.ui.scratch_width * UI_SCRATCH_ROWS *
                                                    (positive)settings.ui.scale * 4,
                                                0, 0, 0, 0);
                        settings.ui.scratch = (unsigned int address_to)mapped;
                        settings.ui.scratch_width = (int)scratch;
                }
        }
        settings_scroll_to(settings.scroll);
        settings.dirty = true;
}

static b32 settings_main()
{
        string_address regular = (string_address) "/usr/share/fonts/inter/Inter-Regular.ttf",
                       bold = (string_address) "/usr/share/fonts/inter/Inter-Bold.ttf";
        struct windows address_to table;
        unsigned int row;
        b32 running = true;

        claim_standard_descriptors();
        memory_zero(&settings, sizeof(settings));
        settings.moonwater = (string_address) "/moonwater";
        settings.editing = settings.pending = -1;
        settings.shown = -1;
        settings.output = -1;

        for (positive at = 1; at + 1 < (positive)program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (option[0] != '-' || !option[1] || option[2])
                        continue;
                if (option[1] == 'f')
                        regular = program_argument(++at);
                else if (option[1] == 'b')
                        bold = program_argument(++at);
                else if (option[1] == 'm')
                        settings.moonwater = program_argument(++at);
        }

        if (!settings_fonts(regular, bold))
        {
                compositor_kmsg((string_address) "settings: no font to draw with (-f FONT.ttf), pid ",
                                (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }

        if (windows_take(&settings.own) < 0)
        {
                log_direct(str("settings: no window: the compositor's desktop is not up\n"));
                return 1;
        }
        table = settings.own.table;
        row = settings.own.row;

        if (settings_lift_other())
        {
                windows_leave(&settings.own);
                return 0;
        }

        settings.ui.scale = table->scale >= 1 && table->scale <= 8 ? (int)table->scale : 1;
        settings_layout();
        {
                int width = settings_px(SETTINGS_WIDTH), height = settings_list_top() + settings.content + settings_px(SETTINGS_FOOT);
                int room = (int)table->output_height - settings_px(2 * (24 + 24));

                settings_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
                table->x[row] = ((int)table->output_width - settings.width) / 2;
                table->y[row] = ((int)table->output_height - settings.height) / 2 - settings_px(12);
        }
        string_copy((string_address)table->title[row], (string_address) "Settings");
        table->glass[row] = WINDOW_GLASS;
        settings.ground = table->ground[row];
        ui_inks(&settings.ui, settings.ground ? settings.ground : 0x101012u);
        settings_say((string_address) "");

        settings_ask_all();
        settings_next_job();
        settings_frame();

        while (running)
        {
                struct window_event event;
                long timeout = -1, ready;

                while (windows_event(&settings.own, &event))
                        if (!settings_event(&event))
                                running = false;

                if (table->state[row] & WINDOW_CLOSING)
                        running = false;

                if ((table->focus == row) != settings.focused)
                {
                        settings.focused = table->focus == row;
                        settings.dirty = true;
                }
                if (table->ground[row] && table->ground[row] != settings.ground)
                {
                        settings.ground = table->ground[row];
                        ui_inks(&settings.ui, settings.ground);
                        settings.dirty = true;
                }
                if (table->want_width[row] && table->want_height[row] &&
                    (table->want_width[row] != settings.asked_w || table->want_height[row] != settings.asked_h))
                {
                        settings.asked_w = table->want_width[row];
                        settings.asked_h = table->want_height[row];
                        settings_size(settings.asked_w, settings.asked_h);
                }

                if (!running)
                        break;

                if (settings.dirty && !settings_frame())
                        timeout = 50000000;

                if (settings.child > 0)
                {
                        positive now = clock_monotonic_nanoseconds(), left;

                        if (now - settings.began >= (positive)SETTINGS_WAIT_NS)
                        {
                                settings_finished(true);
                                continue;
                        }
                        left = (positive)SETTINGS_WAIT_NS - (now - settings.began);
                        timeout = timeout < 0 || (positive)timeout > left ? (long)left : timeout;
                }

                ready = windows_wait(&settings.own, settings.child > 0 ? settings.output : -1, timeout);
                if (ready < 0)
                        break;
                if (settings.child > 0)
                        settings_drain();
        }

        if (settings.child > 0)
        {
                positive status = 0;

                system_call_2(syscall(kill), (positive)settings.child, SIGKILL);
                system_wait4_retry(settings.child, address_of status, 0, null);
        }
        windows_leave(&settings.own);
        return 0;
}

#endif // MOONWATER_CANVAS_SETTINGS
